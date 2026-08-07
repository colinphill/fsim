// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_composite.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <unordered_set>
#include <utility>

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_dimensions = 16;
constexpr std::size_t maximum_fields = 4096;
constexpr std::size_t maximum_field_name = 4096;
constexpr std::size_t maximum_elements = 1U << 20U;
constexpr std::uint32_t maximum_depth = 64;
constexpr std::uint32_t maximum_nodes = 1U << 20U;

bool equal_range(const VhdlVhpiRange& lhs, const VhdlVhpiRange& rhs) {
  return lhs.left == rhs.left && lhs.right == rhs.right
      && lhs.direction == rhs.direction && lhs.is_null == rhs.is_null;
}

bool equal_ranges(
    const std::span<const VhdlVhpiRange> lhs,
    const std::span<const VhdlVhpiRange> rhs) {
  return lhs.size() == rhs.size()
      && std::ranges::equal(lhs, rhs, equal_range);
}

std::string canonical_field_name(const std::string& input) {
  if (input.empty() || input.size() > maximum_field_name
      || input.find('\0') != std::string::npos) {
    return {};
  }
  if (input.front() == '\\') {
    return input.size() >= 3U && input.back() == '\\' ? input
                                                        : std::string{};
  }
  if (std::isalpha(static_cast<unsigned char>(input.front())) == 0
      || input.back() == '_') {
    return {};
  }
  std::string result;
  result.reserve(input.size());
  bool underscore{};
  for (const char value : input) {
    if (value == '_') {
      if (underscore) {
        return {};
      }
      underscore = true;
      result.push_back(value);
      continue;
    }
    if (std::isalnum(static_cast<unsigned char>(value)) == 0) {
      return {};
    }
    underscore = false;
    result.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(value))));
  }
  return result;
}

}  // namespace

VhdlVhpiCompositeSystem::VhdlVhpiCompositeSystem(
    VhdlVhpiTypeSystem& types,
    VhdlVhpiValueSystem& values) noexcept
    : types_(&types), values_(&values) {}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::type_error(
    const VhdlVhpiTypeError error) noexcept {
  switch (error) {
    case VhdlVhpiTypeError::InvalidSimulation:
      return VhdlVhpiCompositeError::InvalidSimulation;
    case VhdlVhpiTypeError::CrossSimulation:
      return VhdlVhpiCompositeError::CrossSimulation;
    case VhdlVhpiTypeError::StaleHandle:
      return VhdlVhpiCompositeError::StaleHandle;
    case VhdlVhpiTypeError::ReleasedHandle:
      return VhdlVhpiCompositeError::ReleasedHandle;
    case VhdlVhpiTypeError::InvalidDeclaration:
      return VhdlVhpiCompositeError::InvalidDeclaration;
    default:
      return VhdlVhpiCompositeError::InvalidHandle;
  }
}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::value_error(
    const VhdlVhpiValueError error) noexcept {
  switch (error) {
    case VhdlVhpiValueError::None:
      return VhdlVhpiCompositeError::None;
    case VhdlVhpiValueError::InvalidSimulation:
      return VhdlVhpiCompositeError::InvalidSimulation;
    case VhdlVhpiValueError::CrossSimulation:
      return VhdlVhpiCompositeError::CrossSimulation;
    case VhdlVhpiValueError::StaleHandle:
      return VhdlVhpiCompositeError::StaleHandle;
    case VhdlVhpiValueError::ReleasedHandle:
      return VhdlVhpiCompositeError::ReleasedHandle;
    case VhdlVhpiValueError::ResourceLimit:
      return VhdlVhpiCompositeError::ResourceLimit;
    default:
      return VhdlVhpiCompositeError::TypeMismatch;
  }
}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::validate_ranges(
    const std::span<const VhdlVhpiRange> ranges,
    std::size_t& elements) noexcept {
  if (ranges.empty() || ranges.size() > maximum_dimensions) {
    return VhdlVhpiCompositeError::InvalidShape;
  }
  elements = 1;
  for (const auto& range : ranges) {
    if (static_cast<std::uint32_t>(range.direction)
        > static_cast<std::uint32_t>(VhdlVhpiDirection::Downto)) {
      return VhdlVhpiCompositeError::InvalidRange;
    }
    const bool ordered = range.direction == VhdlVhpiDirection::To
        ? range.left <= range.right
        : range.left >= range.right;
    if (range.is_null == ordered) {
      return VhdlVhpiCompositeError::InvalidRange;
    }
    if (range.is_null) {
      elements = 0;
      continue;
    }
    const auto difference = range.direction == VhdlVhpiDirection::To
        ? static_cast<std::uint64_t>(range.right)
            - static_cast<std::uint64_t>(range.left)
        : static_cast<std::uint64_t>(range.left)
            - static_cast<std::uint64_t>(range.right);
    if (difference >= maximum_elements) {
      return VhdlVhpiCompositeError::ResourceLimit;
    }
    const auto extent = static_cast<std::size_t>(difference + 1U);
    if (elements != 0U
        && extent > maximum_elements / elements) {
      return VhdlVhpiCompositeError::ResourceLimit;
    }
    elements *= extent;
  }
  return VhdlVhpiCompositeError::None;
}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::validate_member_type(
    const fsim_vhpi_handle_v1 type,
    const VhdlVhpiValueProfile& scalar_profile) const {
  const auto semantic = types_->query(type);
  if (!semantic) {
    return type_error(semantic.error);
  }
  const bool composite =
      semantic.descriptor.scalar_kind == VhdlVhpiScalarKind::Array
      || semantic.descriptor.scalar_kind == VhdlVhpiScalarKind::Record;
  if (composite) {
    return descriptors_.contains(type)
        && scalar_profile.type == 0U
        && scalar_profile.enumeration_literals.empty()
        && scalar_profile.physical_units.empty()
        && scalar_profile.designated_subtype == 0U
        ? VhdlVhpiCompositeError::None
        : VhdlVhpiCompositeError::InvalidDescriptor;
  }
  return value_error(values_->validate_profile_for_type(type, scalar_profile));
}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::publish_type(
    const VhdlVhpiCompositeTypeDescriptor& descriptor) {
  std::scoped_lock lock{mutex_};
  const auto semantic = types_->query(descriptor.type);
  if (!semantic) {
    return type_error(semantic.error);
  }
  if (descriptors_.contains(descriptor.type)) {
    return VhdlVhpiCompositeError::AlreadyPublished;
  }
  if (static_cast<std::uint32_t>(descriptor.kind)
      > static_cast<std::uint32_t>(VhdlVhpiCompositeKind::Record)) {
    return VhdlVhpiCompositeError::InvalidDescriptor;
  }
  if ((descriptor.kind == VhdlVhpiCompositeKind::Array
          && semantic.descriptor.scalar_kind != VhdlVhpiScalarKind::Array)
      || (descriptor.kind == VhdlVhpiCompositeKind::Record
          && semantic.descriptor.scalar_kind != VhdlVhpiScalarKind::Record)) {
    return VhdlVhpiCompositeError::InvalidType;
  }
  if (descriptor.kind == VhdlVhpiCompositeKind::Array) {
    if (descriptor.element_type == 0U || !descriptor.fields.empty()
        || (descriptor.unconstrained && !descriptor.dimensions.empty())
        || (!descriptor.unconstrained && descriptor.dimensions.empty())) {
      return VhdlVhpiCompositeError::InvalidDescriptor;
    }
    if (!descriptor.unconstrained) {
      std::size_t elements{};
      const auto shape_error =
          validate_ranges(descriptor.dimensions, elements);
      if (shape_error != VhdlVhpiCompositeError::None) {
        return shape_error;
      }
    }
    const auto member_error = validate_member_type(
        descriptor.element_type, descriptor.element_scalar_profile);
    if (member_error != VhdlVhpiCompositeError::None) {
      return member_error;
    }
  } else {
    if (descriptor.unconstrained || !descriptor.dimensions.empty()
        || descriptor.element_type != 0U
        || descriptor.element_scalar_profile.type != 0U
        || descriptor.fields.empty()
        || descriptor.fields.size() > maximum_fields) {
      return VhdlVhpiCompositeError::InvalidDescriptor;
    }
    std::unordered_set<std::string> names;
    try {
      for (const auto& field : descriptor.fields) {
        const auto name = canonical_field_name(field.name);
        if (name.empty() || !names.insert(name).second) {
          return VhdlVhpiCompositeError::InvalidDescriptor;
        }
        const auto member_error =
            validate_member_type(field.type, field.scalar_profile);
        if (member_error != VhdlVhpiCompositeError::None) {
          return member_error;
        }
      }
    } catch (...) {
      return VhdlVhpiCompositeError::ResourceLimit;
    }
  }
  try {
    descriptors_.emplace(descriptor.type, descriptor);
  } catch (...) {
    return VhdlVhpiCompositeError::ResourceLimit;
  }
  return VhdlVhpiCompositeError::None;
}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::validate_value(
    const fsim_vhpi_handle_v1 type,
    const VhdlVhpiValueProfile& scalar_profile,
    const VhdlVhpiCompositeValue& value,
    const std::uint32_t depth,
    std::uint32_t& nodes) const {
  if (depth > maximum_depth) {
    return VhdlVhpiCompositeError::DepthLimit;
  }
  if (++nodes > maximum_nodes) {
    return VhdlVhpiCompositeError::NodeLimit;
  }
  const auto descriptor = descriptors_.find(type);
  if (descriptor == descriptors_.end()) {
    const auto* scalar = std::get_if<VhdlVhpiValuePayload>(&value.value);
    return scalar == nullptr
        ? VhdlVhpiCompositeError::TypeMismatch
        : value_error(
              values_->validate_typed_value(type, scalar_profile, *scalar));
  }
  if (descriptor->second.kind == VhdlVhpiCompositeKind::Array) {
    const auto* array = std::get_if<VhdlVhpiArrayValue>(&value.value);
    if (array == nullptr) {
      return VhdlVhpiCompositeError::TypeMismatch;
    }
    if (!descriptor->second.unconstrained
        && !equal_ranges(array->dimensions, descriptor->second.dimensions)) {
      return VhdlVhpiCompositeError::InvalidShape;
    }
    std::size_t elements{};
    const auto shape_error = validate_ranges(array->dimensions, elements);
    if (shape_error != VhdlVhpiCompositeError::None) {
      return shape_error;
    }
    if (elements != array->elements.size()) {
      return VhdlVhpiCompositeError::InvalidShape;
    }
    for (const auto& element : array->elements) {
      const auto error = validate_value(
          descriptor->second.element_type,
          descriptor->second.element_scalar_profile,
          element,
          depth + 1U,
          nodes);
      if (error != VhdlVhpiCompositeError::None) {
        return error;
      }
    }
    return VhdlVhpiCompositeError::None;
  }
  const auto* record = std::get_if<VhdlVhpiRecordValue>(&value.value);
  if (record == nullptr
      || record->fields.size() != descriptor->second.fields.size()) {
    return VhdlVhpiCompositeError::InvalidShape;
  }
  for (std::size_t index = 0; index < record->fields.size(); ++index) {
    const auto& field = descriptor->second.fields[index];
    const auto error = validate_value(
        field.type,
        field.scalar_profile,
        record->fields[index],
        depth + 1U,
        nodes);
    if (error != VhdlVhpiCompositeError::None) {
      return error;
    }
  }
  return VhdlVhpiCompositeError::None;
}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::bind(
    const fsim_vhpi_handle_v1 declaration,
    const VhdlVhpiCompositeValue& initial) {
  std::scoped_lock lock{mutex_};
  const auto declaration_type = types_->declaration_type(declaration);
  if (!declaration_type) {
    return type_error(declaration_type.error);
  }
  if (!descriptors_.contains(declaration_type.type)) {
    return VhdlVhpiCompositeError::InvalidType;
  }
  if (entries_.contains(declaration)) {
    return VhdlVhpiCompositeError::AlreadyBound;
  }
  std::uint32_t nodes{};
  const auto validation = validate_value(
      declaration_type.type, {}, initial, 1, nodes);
  if (validation != VhdlVhpiCompositeError::None) {
    return validation;
  }
  try {
    entries_.emplace(
        declaration, Entry{declaration_type.type, initial});
  } catch (...) {
    return VhdlVhpiCompositeError::ResourceLimit;
  }
  return VhdlVhpiCompositeError::None;
}

VhdlVhpiCompositeResult VhdlVhpiCompositeSystem::read(
    const fsim_vhpi_handle_v1 declaration) const {
  std::scoped_lock lock{mutex_};
  const auto live = types_->declaration_type(declaration);
  if (!live) {
    return {{}, type_error(live.error)};
  }
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return {{}, VhdlVhpiCompositeError::NotFound};
  }
  return {found->second.value, {}};
}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::write(
    const fsim_vhpi_handle_v1 declaration,
    const VhdlVhpiCompositeValue& value) {
  std::scoped_lock lock{mutex_};
  const auto live = types_->declaration_type(declaration);
  if (!live) {
    return type_error(live.error);
  }
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return VhdlVhpiCompositeError::NotFound;
  }
  std::uint32_t nodes{};
  const auto validation =
      validate_value(found->second.type, {}, value, 1, nodes);
  if (validation != VhdlVhpiCompositeError::None) {
    return validation;
  }
  try {
    auto candidate = value;
    found->second.value = std::move(candidate);
  } catch (...) {
    return VhdlVhpiCompositeError::ResourceLimit;
  }
  return VhdlVhpiCompositeError::None;
}

VhdlVhpiCompositeBufferResult VhdlVhpiCompositeSystem::read_members(
    const fsim_vhpi_handle_v1 declaration,
    const std::span<VhdlVhpiCompositeValue> buffer) const {
  std::scoped_lock lock{mutex_};
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return {{}, VhdlVhpiCompositeError::NotFound};
  }
  const std::vector<VhdlVhpiCompositeValue>* members{};
  if (const auto* array =
          std::get_if<VhdlVhpiArrayValue>(&found->second.value.value)) {
    members = &array->elements;
  } else if (const auto* record =
                 std::get_if<VhdlVhpiRecordValue>(
                     &found->second.value.value)) {
    members = &record->fields;
  } else {
    return {{}, VhdlVhpiCompositeError::TypeMismatch};
  }
  if (buffer.size() < members->size()) {
    return {members->size(), VhdlVhpiCompositeError::BufferTooSmall};
  }
  try {
    std::ranges::copy(*members, buffer.begin());
  } catch (...) {
    return {members->size(), VhdlVhpiCompositeError::ResourceLimit};
  }
  return {members->size(), {}};
}

VhdlVhpiArrayOffsetResult VhdlVhpiCompositeSystem::array_offset_locked(
    const Entry& entry,
    const std::span<const std::int64_t> indices) const {
  const auto* array = std::get_if<VhdlVhpiArrayValue>(&entry.value.value);
  if (array == nullptr || indices.size() != array->dimensions.size()) {
    return {{}, VhdlVhpiCompositeError::InvalidIndex};
  }
  std::size_t offset{};
  for (std::size_t dimension = 0; dimension < indices.size(); ++dimension) {
    const auto& range = array->dimensions[dimension];
    if (range.is_null) {
      return {{}, VhdlVhpiCompositeError::InvalidIndex};
    }
    const bool inside = range.direction == VhdlVhpiDirection::To
        ? indices[dimension] >= range.left
            && indices[dimension] <= range.right
        : indices[dimension] <= range.left
            && indices[dimension] >= range.right;
    if (!inside) {
      return {{}, VhdlVhpiCompositeError::InvalidIndex};
    }
    const auto difference = range.direction == VhdlVhpiDirection::To
        ? static_cast<std::uint64_t>(indices[dimension])
            - static_cast<std::uint64_t>(range.left)
        : static_cast<std::uint64_t>(range.left)
            - static_cast<std::uint64_t>(indices[dimension]);
    const auto extent_difference =
        range.direction == VhdlVhpiDirection::To
        ? static_cast<std::uint64_t>(range.right)
            - static_cast<std::uint64_t>(range.left)
        : static_cast<std::uint64_t>(range.left)
            - static_cast<std::uint64_t>(range.right);
    offset = offset * static_cast<std::size_t>(extent_difference + 1U)
        + static_cast<std::size_t>(difference);
  }
  return {offset, {}};
}

VhdlVhpiArrayOffsetResult VhdlVhpiCompositeSystem::array_offset(
    const fsim_vhpi_handle_v1 declaration,
    const std::span<const std::int64_t> indices) const {
  std::scoped_lock lock{mutex_};
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return {{}, VhdlVhpiCompositeError::NotFound};
  }
  return array_offset_locked(found->second, indices);
}

VhdlVhpiCompositeError VhdlVhpiCompositeSystem::write_array_element(
    const fsim_vhpi_handle_v1 declaration,
    const std::span<const std::int64_t> indices,
    const VhdlVhpiCompositeValue& value) {
  std::scoped_lock lock{mutex_};
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return VhdlVhpiCompositeError::NotFound;
  }
  const auto descriptor = descriptors_.find(found->second.type);
  if (descriptor == descriptors_.end()
      || descriptor->second.kind != VhdlVhpiCompositeKind::Array) {
    return VhdlVhpiCompositeError::InvalidType;
  }
  const auto offset = array_offset_locked(found->second, indices);
  if (!offset) {
    return offset.error;
  }
  std::uint32_t nodes{};
  const auto validation = validate_value(
      descriptor->second.element_type,
      descriptor->second.element_scalar_profile,
      value,
      1,
      nodes);
  if (validation != VhdlVhpiCompositeError::None) {
    return validation;
  }
  try {
    auto candidate = found->second.value;
    auto& array = std::get<VhdlVhpiArrayValue>(candidate.value);
    array.elements[offset.value] = value;
    found->second.value = std::move(candidate);
  } catch (...) {
    return VhdlVhpiCompositeError::ResourceLimit;
  }
  return VhdlVhpiCompositeError::None;
}

}  // namespace fsim::runtime
