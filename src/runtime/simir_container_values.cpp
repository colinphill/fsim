// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_container_helpers.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <numeric>
#include <utility>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] std::size_t active_element_count(
    const ContainerValue& value) noexcept {
  switch (value.type.element_kind) {
  case ContainerElementKind::Packed:
  case ContainerElementKind::Scalar: return value.elements.size();
  case ContainerElementKind::String: return value.string_elements.size();
  case ContainerElementKind::Container:
  case ContainerElementKind::Aggregate: return value.nested_elements.size();
  }
  return 0;
}

[[nodiscard]] std::size_t fixed_element_count(
    const ContainerType& type) {
  const auto limit = maximum_container_elements(type);
  if (!type.dimensions.empty()) {
    std::size_t result = 1;
    for (const auto& [left, right] : type.dimensions) {
      const auto distance = left >= right
          ? static_cast<std::int64_t>(left) - right
          : static_cast<std::int64_t>(right) - left;
      const auto count = static_cast<std::size_t>(distance + 1);
      if (count > limit || result > limit / count) {
        throw std::length_error{
            "SimIR static array exceeds its owning-storage budget"};
      }
      result *= count;
    }
    return result;
  }
  const auto distance = type.index_left >= type.index_right
      ? static_cast<std::int64_t>(type.index_left) - type.index_right
      : static_cast<std::int64_t>(type.index_right) - type.index_left;
  const auto result = static_cast<std::size_t>(distance + 1);
  if (result > limit) {
    throw std::length_error{
        "SimIR static array exceeds its owning-storage budget"};
  }
  return result;
}

[[nodiscard]] std::size_t checked_storage_add(
    const std::size_t left,
    const std::size_t right) {
  if (right > maximum_container_storage_bytes
      || left > maximum_container_storage_bytes - right) {
    throw std::length_error{
        "SimIR container exceeds its recursive owning-storage budget"};
  }
  return left + right;
}

}  // namespace

bool associative_index_key_less(
    const ContainerType& type,
    const PackedLogic4& left,
    const PackedLogic4& right) {
  return associative_index_key_less_impl(type, left, right);
}

std::size_t container_value_size(const ContainerValue& value) noexcept {
  return active_element_count(value);
}

bool sparse_container_eligible(const ContainerType& type) noexcept {
  return type.fixed && !type.associative && !type.queue
      && (type.element_kind == ContainerElementKind::Packed
          || type.element_kind == ContainerElementKind::Scalar
          || type.element_kind == ContainerElementKind::String)
      && !type.aggregate_value && !type.vhdl_shaped;
}

std::uint64_t sparse_container_element_count(
    const ContainerType& type) noexcept {
  const auto extent = [](const std::int32_t left, const std::int32_t right) {
    const auto distance = left >= right
        ? static_cast<std::int64_t>(left) - right
        : static_cast<std::int64_t>(right) - left;
    return static_cast<std::uint64_t>(distance) + 1U;
  };
  if (type.dimensions.empty()) {
    return extent(type.index_left, type.index_right);
  }
  std::uint64_t result = 1U;
  for (const auto& [left, right] : type.dimensions) {
    const auto count = extent(left, right);
    if (result > std::numeric_limits<std::uint64_t>::max() / count) {
      return std::numeric_limits<std::uint64_t>::max();
    }
    result *= count;
  }
  return result;
}

namespace {

[[nodiscard]] auto sparse_key_position(
    const ContainerValue& value, const std::uint64_t ordinal) {
  return std::ranges::lower_bound(
      value.keys, ordinal, std::less<> { },
      [](const PackedLogic4& key) {
        return key.known_unsigned_value().value_or(0U);
      });
}

// Words of one plane of a sparse value's compact image.
[[nodiscard]] std::uint64_t compact_plane_words(const ContainerType& type) {
  const auto count = sparse_container_element_count(type);
  if (count > std::numeric_limits<std::uint64_t>::max() / 64U
          / std::max<std::uint64_t>(type.element_width, 1U)) {
    return std::numeric_limits<std::uint64_t>::max() / 2U;
  }
  return (count * type.element_width + 63U) / 64U;
}

// `width` (at most 64) bits of `plane` from bit `position`.
[[nodiscard]] std::uint64_t read_plane_bits(
    const std::uint64_t* const plane, const std::uint64_t words,
    const std::uint64_t position, const std::uint32_t width) {
  const auto word = position / 64U;
  const auto shift = position % 64U;
  auto bits = plane[word] >> shift;
  if (shift != 0U && word + 1U < words) {
    bits |= plane[word + 1U] << (64U - shift);
  }
  return width == 64U ? bits : bits & ((std::uint64_t { 1 } << width) - 1U);
}

// Store `width` (at most 64) bits of `bits` in `plane` from bit `position`.
void write_plane_bits(
    std::uint64_t* const plane, const std::uint64_t position,
    const std::uint32_t width, std::uint64_t bits) {
  const auto mask = width == 64U ? ~std::uint64_t { }
                                 : (std::uint64_t { 1 } << width) - 1U;
  bits &= mask;
  const auto word = position / 64U;
  const auto shift = position % 64U;
  plane[word] = (plane[word] & ~(mask << shift)) | (bits << shift);
  if (shift + width > 64U) {
    const auto rest = shift + width - 64U;
    const auto high_mask = (std::uint64_t { 1 } << rest) - 1U;
    plane[word + 1U]
        = (plane[word + 1U] & ~high_mask) | (bits >> (64U - shift));
  }
}

[[nodiscard]] PackedLogic4 read_compact_element(
    const ContainerValue& value, const std::uint64_t ordinal) {
  const auto width = value.type.element_width;
  const auto words = value.compact_planes.size() / 2U;
  const auto* const aval = value.compact_planes.data();
  const auto* const bval = aval + words;
  if (width <= 64U) {
    const auto position = ordinal * width;
    return PackedLogic4::from_aval_bval(width,
        read_plane_bits(aval, words, position, width),
        read_plane_bits(bval, words, position, width));
  }
  const auto chunks = (width + 63U) / 64U;
  std::vector<std::uint64_t> a(chunks);
  std::vector<std::uint64_t> b(chunks);
  for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
    const auto bits = std::min<std::uint32_t>(64U, width - chunk * 64U);
    const auto position
        = ordinal * width + static_cast<std::uint64_t>(chunk) * 64U;
    a[chunk] = read_plane_bits(aval, words, position, bits);
    b[chunk] = read_plane_bits(bval, words, position, bits);
  }
  return PackedLogic4::from_word_planes(width, a, b);
}

void write_compact_element(
    ContainerValue& value, const std::uint64_t ordinal,
    const PackedLogic4& element) {
  if (element.is_logic9()) {
    throw std::invalid_argument{
        "sparse SimIR array element does not match its type"};
  }
  const auto width = value.type.element_width;
  const auto words = value.compact_planes.size() / 2U;
  auto* const aval = value.compact_planes.data();
  auto* const bval = aval + words;
  const auto element_aval = element.aval_words();
  const auto element_bval = element.bval_words();
  const auto chunks = (width + 63U) / 64U;
  for (std::uint32_t chunk = 0U; chunk < chunks; ++chunk) {
    const auto bits = std::min<std::uint32_t>(64U, width - chunk * 64U);
    const auto position
        = ordinal * width + static_cast<std::uint64_t>(chunk) * 64U;
    write_plane_bits(aval, position, bits,
        chunk < element_aval.size() ? element_aval[chunk] : 0U);
    write_plane_bits(bval, position, bits,
        chunk < element_bval.size() ? element_bval[chunk] : 0U);
  }
}

// Replace the stored elements of a packed sparse value by its compact image
// once they cost more than the image, while the image fits the budget.
void compact_when_dense(ContainerValue& value) {
  if (!value.compact_planes.empty()) {
    return;
  }
  const auto words = compact_plane_words(value.type);
  const auto image_bytes = words > maximum_container_storage_bytes / 16U
      ? maximum_container_storage_bytes + 1U
      : static_cast<std::size_t>(words) * 16U;
  const auto stored_bytes
      = (value.elements.size() + 1U) * 2U * sizeof(PackedLogic4);
  if (stored_bytes < image_bytes
      || image_bytes > maximum_container_storage_bytes) {
    return;
  }
  const auto count = sparse_container_element_count(value.type);
  const auto background = value.associative_default.value_or(
      initial_packed_element(value.type));
  ContainerValue image;
  image.type = value.type;
  image.associative_default = value.associative_default;
  image.compact_planes.assign(static_cast<std::size_t>(words) * 2U, 0U);
  // The background repeats every lcm(width, 64) bits: write one period of
  // elements, then copy its words through both planes.
  const auto width = std::max<std::uint64_t>(value.type.element_width, 1U);
  const auto period_words = width / std::gcd(width, std::uint64_t { 64 });
  const auto period_elements = period_words * 64U / width;
  for (std::uint64_t ordinal = 0U;
       ordinal < std::min(count, period_elements + 1U); ++ordinal) {
    write_compact_element(image, ordinal, background);
  }
  for (std::size_t plane = 0U; plane < 2U; ++plane) {
    auto* const base = image.compact_planes.data()
        + plane * static_cast<std::size_t>(words);
    for (auto word = period_words; word < words; ++word) {
      base[word] = base[word - period_words];
    }
  }
  if (count * width % 64U != 0U) {
    // Clear the bits past the last element so equal images compare equal.
    const auto used = count * width % 64U;
    for (std::size_t plane = 0U; plane < 2U; ++plane) {
      image.compact_planes[plane * static_cast<std::size_t>(words)
          + static_cast<std::size_t>(words) - 1U]
          &= (std::uint64_t { 1 } << used) - 1U;
    }
  }
  for (std::size_t index = 0U; index < value.keys.size(); ++index) {
    write_compact_element(image,
        value.keys[index].known_unsigned_value().value_or(0U),
        value.elements[index]);
  }
  value = std::move(image);
}

void require_sparse_ordinal(
    const ContainerValue& value, const std::uint64_t ordinal,
    const ContainerElementKind kind) {
  if (!value.type.sparse
      || ordinal >= sparse_container_element_count(value.type)) {
    throw std::out_of_range{"sparse SimIR array ordinal is out of range"};
  }
  const bool packed = value.type.element_kind == ContainerElementKind::Packed
      || value.type.element_kind == ContainerElementKind::Scalar;
  if (packed != (kind != ContainerElementKind::String)) {
    throw std::invalid_argument{
        "sparse SimIR array element does not match its type"};
  }
}

// Store `element` at `ordinal` in the sorted (keys, stored) pairs, keeping
// only elements that differ from `background`, so equal values have one
// representation. Returns whether the element changed.
template <typename Element>
bool write_sparse_element(
    ContainerValue& value, std::vector<Element>& stored_elements,
    const std::uint64_t ordinal, Element element,
    const Element& background) {
  const auto key = sparse_key_position(value, ordinal);
  const auto position = key - value.keys.begin();
  const auto stored = stored_elements.begin() + position;
  const bool present
      = key != value.keys.end() && key->known_unsigned_value() == ordinal;
  if (element == background) {
    if (!present) {
      return false;
    }
    value.keys.erase(key);
    stored_elements.erase(stored);
    return true;
  }
  if (present) {
    if (*stored == element) {
      return false;
    }
    *stored = std::move(element);
    return true;
  }
  if (stored_elements.size() >= maximum_container_elements(value.type)) {
    throw std::length_error{
        "sparse SimIR array exceeds its owning-storage budget"};
  }
  value.keys.insert(key, PackedLogic4::from_aval_bval(32U, ordinal, 0U));
  stored_elements.insert(stored, std::move(element));
  return true;
}

}  // namespace

PackedLogic4 read_sparse_container_element(
    const ContainerValue& value, const std::uint64_t ordinal) {
  require_sparse_ordinal(value, ordinal, ContainerElementKind::Packed);
  if (!value.compact_planes.empty()) {
    return read_compact_element(value, ordinal);
  }
  const auto key = sparse_key_position(value, ordinal);
  if (key != value.keys.end() && key->known_unsigned_value() == ordinal) {
    return value.elements[static_cast<std::size_t>(
        key - value.keys.begin())];
  }
  return value.associative_default.value_or(
      initial_packed_element(value.type));
}

bool write_sparse_container_element(
    ContainerValue& value, const std::uint64_t ordinal,
    PackedLogic4 element) {
  require_sparse_ordinal(value, ordinal, ContainerElementKind::Packed);
  if (element.width() != value.type.element_width) {
    throw std::invalid_argument{
        "sparse SimIR array element does not match its type"};
  }
  if (value.compact_planes.empty()) {
    compact_when_dense(value);
  }
  if (!value.compact_planes.empty()) {
    if (read_compact_element(value, ordinal) == element) {
      return false;
    }
    write_compact_element(value, ordinal, element);
    return true;
  }
  const auto background = value.associative_default.value_or(
      initial_packed_element(value.type));
  return write_sparse_element(
      value, value.elements, ordinal, std::move(element), background);
}

PackedLogic4 sparse_container_background(const ContainerValue& value) {
  return value.associative_default.value_or(
      initial_packed_element(value.type));
}

std::string sparse_container_string_background(const ContainerValue& value) {
  return value.associative_default_text.value_or(std::string { });
}

std::uint64_t container_element_count(const ContainerValue& value) {
  if (value.type.sparse) {
    return sparse_container_element_count(value.type);
  }
  return container_value_size(value);
}

ContainerValue dense_container_image(const ContainerValue& value) {
  if (!value.type.sparse) {
    return value;
  }
  ContainerValue image;
  image.type = value.type;
  image.type.sparse = false;
  const auto count = sparse_container_element_count(value.type);
  if (value.type.element_kind == ContainerElementKind::String) {
    image.string_elements.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
      image.string_elements.push_back(
          read_sparse_container_string(value, ordinal));
    }
    return image;
  }
  image.elements.reserve(static_cast<std::size_t>(count));
  ContainerElementCursor cursor{value};
  for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
    image.elements.push_back(cursor.at(ordinal));
  }
  return image;
}

void store_dense_container_image(ContainerValue& value, ContainerValue image) {
  if (!value.type.sparse) {
    image.type = value.type;
    value = std::move(image);
    return;
  }
  if (value.type.element_kind == ContainerElementKind::String) {
    for (std::size_t ordinal = 0U; ordinal < image.string_elements.size();
         ++ordinal) {
      if (read_sparse_container_string(value, ordinal)
          != image.string_elements[ordinal]) {
        static_cast<void>(write_sparse_container_string(
            value, ordinal, std::move(image.string_elements[ordinal])));
      }
    }
    return;
  }
  for (std::size_t ordinal = 0U; ordinal < image.elements.size(); ++ordinal) {
    if (read_sparse_container_element(value, ordinal)
        != image.elements[ordinal]) {
      static_cast<void>(write_sparse_container_element(
          value, ordinal, std::move(image.elements[ordinal])));
    }
  }
}

std::uint64_t container_size_method(const ContainerValue& value) {
  if (!value.type.sparse) {
    return container_value_size(value);
  }
  auto first = value.type;
  if (!first.dimensions.empty()) {
    first.dimensions.resize(1U);
  }
  return sparse_container_element_count(first);
}

PackedLogic4 container_packed_element(
    const ContainerValue& value, const std::uint64_t ordinal) {
  if (value.type.sparse) {
    return read_sparse_container_element(value, ordinal);
  }
  if (ordinal >= value.elements.size()) {
    throw std::out_of_range{"SimIR container element ordinal is out of range"};
  }
  return value.elements[static_cast<std::size_t>(ordinal)];
}

void set_container_packed_element(
    ContainerValue& value, const std::uint64_t ordinal,
    PackedLogic4 element) {
  if (value.type.sparse) {
    static_cast<void>(
        write_sparse_container_element(value, ordinal, std::move(element)));
    return;
  }
  if (ordinal >= value.elements.size()) {
    throw std::out_of_range{"SimIR container element ordinal is out of range"};
  }
  value.elements[static_cast<std::size_t>(ordinal)] = std::move(element);
}

std::string container_string_element(
    const ContainerValue& value, const std::uint64_t ordinal) {
  if (value.type.sparse) {
    return read_sparse_container_string(value, ordinal);
  }
  if (ordinal >= value.string_elements.size()) {
    throw std::out_of_range{"SimIR container element ordinal is out of range"};
  }
  return value.string_elements[static_cast<std::size_t>(ordinal)];
}

void set_container_string_element(
    ContainerValue& value, const std::uint64_t ordinal,
    std::string element) {
  if (value.type.sparse) {
    static_cast<void>(
        write_sparse_container_string(value, ordinal, std::move(element)));
    return;
  }
  if (ordinal >= value.string_elements.size()) {
    throw std::out_of_range{"SimIR container element ordinal is out of range"};
  }
  value.string_elements[static_cast<std::size_t>(ordinal)]
      = std::move(element);
}

namespace {

template <typename Element, typename Visit>
bool visit_keyed_pairs(
    const ContainerValue& lhs, const std::vector<Element>& left_elements,
    const Element& left_background,
    const ContainerValue& rhs, const std::vector<Element>& right_elements,
    const Element& right_background, const Visit& visit) {
  const auto count = sparse_container_element_count(lhs.type);
  const auto ordinal_at = [](const ContainerValue& value, const std::size_t index) {
    return index < value.keys.size()
        ? value.keys[index].known_unsigned_value().value_or(0U)
        : std::numeric_limits<std::uint64_t>::max();
  };
  std::size_t left{};
  std::size_t right{};
  std::uint64_t visited{};
  while (left < lhs.keys.size() || right < rhs.keys.size()) {
    const auto left_ordinal = ordinal_at(lhs, left);
    const auto right_ordinal = ordinal_at(rhs, right);
    const auto ordinal = std::min(left_ordinal, right_ordinal);
    const auto& left_element = left_ordinal == ordinal
        ? left_elements[left] : left_background;
    const auto& right_element = right_ordinal == ordinal
        ? right_elements[right] : right_background;
    ++visited;
    if (!visit(std::optional<std::uint64_t>{ordinal}, left_element,
            right_element)) {
      return false;
    }
    left += left_ordinal == ordinal ? 1U : 0U;
    right += right_ordinal == ordinal ? 1U : 0U;
  }
  if (visited < count) {
    return visit(std::optional<std::uint64_t>{}, left_background,
        right_background);
  }
  return true;
}

}  // namespace

bool visit_sparse_element_pairs(
    const ContainerValue& lhs, const ContainerValue& rhs,
    const std::function<bool(std::optional<std::uint64_t>,
        const PackedLogic4&, const PackedLogic4&)>& visit) {
  if (!lhs.compact_planes.empty() || !rhs.compact_planes.empty()) {
    const auto count = sparse_container_element_count(lhs.type);
    ContainerElementCursor left{lhs};
    ContainerElementCursor right{rhs};
    for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
      if (!visit(ordinal, left.at(ordinal), right.at(ordinal))) {
        return false;
      }
    }
    return true;
  }
  return visit_keyed_pairs(lhs, lhs.elements, sparse_container_background(lhs),
      rhs, rhs.elements, sparse_container_background(rhs), visit);
}

bool visit_sparse_string_pairs(
    const ContainerValue& lhs, const ContainerValue& rhs,
    const std::function<bool(std::optional<std::uint64_t>,
        const std::string&, const std::string&)>& visit) {
  return visit_keyed_pairs(lhs, lhs.string_elements,
      sparse_container_string_background(lhs), rhs, rhs.string_elements,
      sparse_container_string_background(rhs), visit);
}

PackedLogic4 ContainerElementCursor::at(const std::uint64_t ordinal) {
  if (!value_.type.sparse) {
    return value_.elements.at(static_cast<std::size_t>(ordinal));
  }
  if (!value_.compact_planes.empty()) {
    return read_compact_element(value_, ordinal);
  }
  while (next_key_ < value_.keys.size()
      && value_.keys[next_key_].known_unsigned_value().value_or(0U)
          < ordinal) {
    ++next_key_;
  }
  if (next_key_ < value_.keys.size()
      && value_.keys[next_key_].known_unsigned_value() == ordinal) {
    return value_.elements[next_key_];
  }
  return value_.associative_default.value_or(
      initial_packed_element(value_.type));
}

std::string read_sparse_container_string(
    const ContainerValue& value, const std::uint64_t ordinal) {
  require_sparse_ordinal(value, ordinal, ContainerElementKind::String);
  const auto key = sparse_key_position(value, ordinal);
  if (key != value.keys.end() && key->known_unsigned_value() == ordinal) {
    return value.string_elements[static_cast<std::size_t>(
        key - value.keys.begin())];
  }
  return value.associative_default_text.value_or(std::string { });
}

bool write_sparse_container_string(
    ContainerValue& value, const std::uint64_t ordinal,
    std::string element) {
  require_sparse_ordinal(value, ordinal, ContainerElementKind::String);
  const auto background
      = value.associative_default_text.value_or(std::string { });
  return write_sparse_element(
      value, value.string_elements, ordinal, std::move(element), background);
}

std::size_t container_value_storage_bytes(const ContainerValue& value) {
  auto result = checked_storage_add(
      value.elements.size() * sizeof(PackedLogic4),
      value.keys.size() * sizeof(PackedLogic4));
  result = checked_storage_add(
      result, value.string_keys.size() * sizeof(std::string));
  for (const auto& key : value.string_keys) {
    result = checked_storage_add(result, key.size());
  }
  result = checked_storage_add(
      result, value.string_elements.size() * sizeof(std::string));
  for (const auto& element : value.string_elements) {
    result = checked_storage_add(result, element.size());
  }
  result = checked_storage_add(
      result, value.compact_planes.size() * sizeof(std::uint64_t));
  result = checked_storage_add(
      result, value.nested_elements.size() * sizeof(ContainerValue));
  for (const auto& element : value.nested_elements) {
    result = checked_storage_add(
        result, container_value_storage_bytes(element));
  }
  return result;
}

void validate_container_value(const ContainerValue& value) {
  const auto packed_storage =
      value.type.element_kind == ContainerElementKind::Packed
      || value.type.element_kind == ContainerElementKind::Scalar;
  if (packed_storage && value.type.element_width == 0) {
    throw std::invalid_argument{
        "SimIR packed/scalar container element width must be positive"};
  }
  if (value.type.element_kind == ContainerElementKind::Scalar) {
    const auto expected_width =
        value.type.scalar_kind == SystemVerilogScalarKind::ShortReal ? 32U
                                                                    : 64U;
    if (value.type.scalar_kind == SystemVerilogScalarKind::None
        || value.type.element_width != expected_width) {
      throw std::invalid_argument{"SimIR scalar container profile is invalid"};
    }
  } else if (value.type.scalar_kind != SystemVerilogScalarKind::None) {
    throw std::invalid_argument{
        "non-scalar SimIR container has a scalar kind"};
  }
  if (value.type.element_kind == ContainerElementKind::Container
      && value.type.element_types.size() != 1) {
    throw std::invalid_argument{
        "nested SimIR container requires one element profile"};
  }
  if (value.type.element_kind == ContainerElementKind::Aggregate
      && (value.type.element_types.empty()
          || (!value.type.member_names.empty()
              && value.type.member_names.size()
                  != value.type.element_types.size()))) {
    throw std::invalid_argument{
        "SimIR aggregate container member profile is invalid"};
  }
  if (value.type.aggregate_value
      && (value.type.element_kind != ContainerElementKind::Aggregate
          || value.type.queue || value.type.associative
          || value.type.fixed || value.type.maximum_elements
          || !value.type.dimensions.empty())) {
    throw std::invalid_argument{
        "SimIR aggregate value profile is invalid"};
  }
  const auto size = active_element_count(value);
  if (size > maximum_container_elements(value.type)
      || (value.type.maximum_elements
          && size > *value.type.maximum_elements)) {
    throw std::length_error{
        "SimIR container exceeds its owning-storage or declared queue limit"};
  }
  if (!value.type.queue && value.type.maximum_elements) {
    throw std::invalid_argument{
        "a non-queue container cannot have a queue bound"};
  }
  if (static_cast<unsigned>(value.type.queue)
          + static_cast<unsigned>(value.type.associative)
          + static_cast<unsigned>(value.type.fixed)
      > 1U) {
    throw std::invalid_argument{
        "a SimIR container kind must be unambiguous"};
  }
  if (!value.compact_planes.empty()
      && (!value.type.sparse || !packed_storage || !value.keys.empty()
          || !value.elements.empty()
          || value.compact_planes.size()
              != compact_plane_words(value.type) * 2U)) {
    throw std::invalid_argument{"SimIR compact array storage is invalid"};
  }
  if (value.type.sparse) {
    const auto count = sparse_container_element_count(value.type);
    if (!sparse_container_eligible(value.type)
        || count > (std::uint64_t { 1 } << 32U)
        || value.keys.size() != size) {
      throw std::invalid_argument{"SimIR sparse array storage is invalid"};
    }
    for (std::size_t index = 0; index < value.keys.size(); ++index) {
      const auto& key = value.keys[index];
      const auto ordinal = key.known_unsigned_value();
      if (key.width() != 32U || key.is_logic9() || !ordinal
          || std::ranges::any_of(key.bval_words(),
                 [](const std::uint64_t word) { return word != 0; })
          || *ordinal >= count
          || (index != 0
              && value.keys[index - 1].known_unsigned_value() >= ordinal)) {
        throw std::invalid_argument{
            "SimIR sparse array ordinals must be known, ordered, and in "
            "range"};
      }
    }
  } else if (value.type.fixed) {
    const auto count = fixed_element_count(value.type);
    if (count == 0) {
      throw std::length_error{"SimIR static array has no materialized elements"};
    }
    if (size != count) {
      throw std::invalid_argument{
          "SimIR static-array storage does not match its declared range"};
    }
  }
  if (value.type.associative) {
      if (!value.type.string_indices && value.type.index_width == 0) {
          throw std::invalid_argument {
              "SimIR integral associative-array index width must be positive"
          };
      }
    if ((value.type.string_indices
             ? value.string_keys.size() : value.keys.size()) != size) {
      throw std::invalid_argument{
          "SimIR associative-array keys and elements must be paired"};
    }
    if (value.type.string_indices) {
      if (!value.keys.empty()) {
        throw std::invalid_argument{
            "string-indexed SimIR associative array has packed keys"};
      }
      for (std::size_t index = 0;
           index < value.string_keys.size(); ++index) {
        const auto& key = value.string_keys[index];
        if (key.size() > maximum_string_bytes) {
          throw std::invalid_argument{
              "SimIR associative-array string key is invalid"};
        }
        if (index != 0
            && !(value.string_keys[index - 1] < key)) {
          throw std::invalid_argument{
              "SimIR associative-array string keys must be unique and ordered"};
        }
      }
    } else {
      if (!value.string_keys.empty()) {
        throw std::invalid_argument{
            "integral-indexed SimIR associative array has string keys"};
      }
      for (std::size_t index = 0; index < value.keys.size(); ++index) {
          const auto& key = value.keys[index];
          const bool unknown = std::ranges::any_of(
              key.bval_words(),
              [](const std::uint64_t word) { return word != 0; });
          if (key.width() != value.type.index_width
              || key.is_logic9() || unknown) {
              throw std::invalid_argument {
                  "SimIR associative-array key does not match its type"
              };
          }
        if (index != 0
            && !associative_index_key_less(
                value.type, value.keys[index - 1], key)) {
          throw std::invalid_argument{
              "SimIR associative-array keys must be unique and ordered"};
        }
      }
    }
  } else if ((!value.keys.empty() && !value.type.sparse)
             || !value.string_keys.empty()) {
    throw std::invalid_argument{
        "non-associative SimIR containers cannot contain keys"};
  }
  if (packed_storage
      && (!value.string_elements.empty()
          || !value.nested_elements.empty())) {
    throw std::invalid_argument{
        "packed/scalar SimIR container has inactive element storage"};
  }
  if (value.type.element_kind == ContainerElementKind::String
      && (!value.elements.empty() || !value.nested_elements.empty())) {
    throw std::invalid_argument{
        "string SimIR container has inactive element storage"};
  }
  if ((value.type.element_kind == ContainerElementKind::Container
       || value.type.element_kind == ContainerElementKind::Aggregate)
      && (!value.elements.empty() || !value.string_elements.empty())) {
    throw std::invalid_argument{
        "nested SimIR container has inactive element storage"};
  }
  // Only a two-state or scalar element type rules out unknown bits.
  const bool known_elements = value.type.two_state
      || value.type.element_kind == ContainerElementKind::Scalar;
  for (const auto& element : value.elements) {
    if (element.width() != value.type.element_width
        || element.is_logic9()
        || (known_elements
            && std::ranges::any_of(element.bval_words(),
                [](const std::uint64_t word) { return word != 0; }))) {
      throw std::invalid_argument{
          "SimIR container element does not match its type"};
    }
  }
  for (const auto& element : value.string_elements) {
    if (element.size() > maximum_string_bytes) {
      throw std::invalid_argument{
          "SimIR container string element exceeds the byte limit"};
    }
  }
  if (aggregate_box(value.type)
      && value.nested_elements.size() != value.type.element_types.size()) {
    throw std::invalid_argument{
        "SimIR aggregate value does not match its member profile"};
  }
  for (std::size_t index = 0;
       index < value.nested_elements.size(); ++index) {
    const auto& element = value.nested_elements[index];
    const auto& expected = aggregate_box(value.type)
        ? value.type.element_types[index]
        : value.type.element_kind == ContainerElementKind::Container
              ? value.type.element_types.front()
              : aggregate_element_type(value.type);
    if (element.type != expected) {
      throw std::invalid_argument{
          "nested SimIR container element profile mismatch"};
    }
    validate_container_value(element);
  }
  if (container_value_storage_bytes(value)
      > maximum_container_storage_bytes) {
    throw std::length_error{
        "SimIR container exceeds its recursive owning-storage budget"};
  }
}

bool new_initializer_compatible(
    const ContainerType& initializer, const ContainerType& target) {
  if (initializer == target) {
    return true;
  }
  auto as_dynamic = initializer;
  if (initializer.fixed && initializer.dimensions.size() <= 1U) {
    as_dynamic.fixed = false;
    as_dynamic.sparse = false;
    as_dynamic.dimensions.clear();
    as_dynamic.index_left = target.index_left;
    as_dynamic.index_right = target.index_right;
    return as_dynamic == target;
  }
  if (!initializer.queue) {
    return false;
  }
  as_dynamic.queue = false;
  as_dynamic.maximum_elements.reset();
  return as_dynamic == target;
}

std::optional<ContainerType>
nested_element_container_type(const ContainerType& type) {
  if (type.element_kind == ContainerElementKind::Container
      && type.element_types.size() == 1U) {
    return type.element_types.front();
  }
  if (type.element_kind == ContainerElementKind::Aggregate
      && !type.aggregate_value) {
    return aggregate_element_type(type);
  }
  return std::nullopt;
}

ContainerValue default_container_value(const ContainerType& type) {
  ContainerValue result;
  result.type = type;
  if (aggregate_box(type)) {
    result.nested_elements.reserve(type.element_types.size());
    for (const auto& member : type.element_types) {
      result.nested_elements.push_back(default_container_value(member));
    }
  } else if (type.fixed && !type.sparse) {
    const auto count = fixed_element_count(type);
    switch (type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
      result.elements.assign(count, initial_packed_element(type));
      break;
    case ContainerElementKind::String:
      result.string_elements.resize(count);
      break;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
      result.nested_elements.reserve(count);
      for (std::size_t index = 0; index < count; ++index) {
        append_default_element(result);
      }
      break;
    }
  }
  validate_container_value(result);
  return result;
}

}  // namespace fsim::runtime::simir
