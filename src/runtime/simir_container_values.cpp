// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] bool aggregate_box(const ContainerType& type) noexcept {
  return type.element_kind == ContainerElementKind::Aggregate
      && type.aggregate_value;
}

[[nodiscard]] ContainerType aggregate_element_type(
    const ContainerType& type) {
  auto result = type;
  result.queue = false;
  result.associative = false;
  result.fixed = false;
  result.aggregate_value = true;
  result.maximum_elements.reset();
  result.dimensions.clear();
  result.index_left = 0;
  result.index_right = 0;
  return result;
}

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

[[nodiscard]] bool key_less(
    const ContainerType& type,
    const PackedLogic4& left,
    const PackedLogic4& right) {
    if (type.signed_indices) {
        const auto lhs_negative = left.get(type.index_width - 1U) == Logic4::one;
        const auto rhs_negative = right.get(type.index_width - 1U) == Logic4::one;
        if (lhs_negative != rhs_negative)
            return lhs_negative;
    }
    const auto lhs = left.aval_words();
    const auto rhs = right.aval_words();
    for (auto index = lhs.size(); index != 0; --index) {
        if (lhs[index - 1U] != rhs[index - 1U]) {
            return lhs[index - 1U] < rhs[index - 1U];
        }
    }
    return false;
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

[[nodiscard]] PackedLogic4 initial_packed_element(
    const ContainerType& type) {
  if (type.element_kind == ContainerElementKind::Scalar
      || type.two_state) {
    return PackedLogic4(type.element_width, Logic4::zero);
  }
  return PackedLogic4{type.element_width, Logic4::x};
}

void append_default_element(ContainerValue& value) {
  switch (value.type.element_kind) {
  case ContainerElementKind::Packed:
  case ContainerElementKind::Scalar:
    value.elements.push_back(initial_packed_element(value.type));
    return;
  case ContainerElementKind::String:
    value.string_elements.emplace_back();
    return;
  case ContainerElementKind::Container:
    value.nested_elements.push_back(
        default_container_value(value.type.element_types.front()));
    return;
  case ContainerElementKind::Aggregate:
    value.nested_elements.push_back(
        default_container_value(aggregate_element_type(value.type)));
    return;
  }
}

}  // namespace

std::size_t container_value_size(const ContainerValue& value) noexcept {
  return active_element_count(value);
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
  if (value.type.fixed) {
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
            && !key_less(value.type, value.keys[index - 1], key)) {
          throw std::invalid_argument{
              "SimIR associative-array keys must be unique and ordered"};
        }
      }
    }
  } else if (!value.keys.empty() || !value.string_keys.empty()) {
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
  for (const auto& element : value.elements) {
    const bool contains_unknown = std::ranges::any_of(
        element.bval_words(),
        [](const std::uint64_t word) { return word != 0; });
    if (element.width() != value.type.element_width
        || element.is_logic9()
        || ((value.type.two_state
             || value.type.element_kind == ContainerElementKind::Scalar)
            && contains_unknown)) {
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

ContainerValue default_container_value(const ContainerType& type) {
  ContainerValue result;
  result.type = type;
  if (aggregate_box(type)) {
    result.nested_elements.reserve(type.element_types.size());
    for (const auto& member : type.element_types) {
      result.nested_elements.push_back(default_container_value(member));
    }
  } else if (type.fixed) {
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
