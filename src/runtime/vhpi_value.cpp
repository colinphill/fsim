// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_value.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_literal_size = 4096;
constexpr std::size_t maximum_literals = 1U << 16U;
constexpr std::size_t maximum_units = 4096;

bool valid_name(const std::string& name) {
  return !name.empty() && name.size() <= maximum_literal_size
      && name.find('\0') == std::string::npos;
}

}  // namespace

VhdlVhpiValueSystem::VhdlVhpiValueSystem(
    VhdlVhpiObjectRegistry& objects,
    VhdlVhpiTypeSystem& types) noexcept
    : objects_(&objects), types_(&types) {}

VhdlVhpiValueError VhdlVhpiValueSystem::type_error(
    const VhdlVhpiTypeError error) noexcept {
  switch (error) {
    case VhdlVhpiTypeError::InvalidSimulation:
      return VhdlVhpiValueError::InvalidSimulation;
    case VhdlVhpiTypeError::CrossSimulation:
      return VhdlVhpiValueError::CrossSimulation;
    case VhdlVhpiTypeError::StaleHandle:
      return VhdlVhpiValueError::StaleHandle;
    case VhdlVhpiTypeError::ReleasedHandle:
      return VhdlVhpiValueError::ReleasedHandle;
    case VhdlVhpiTypeError::InvalidDeclaration:
      return VhdlVhpiValueError::InvalidDeclaration;
    default:
      return VhdlVhpiValueError::InvalidHandle;
  }
}

VhdlVhpiValueError VhdlVhpiValueSystem::validate_profile(
    const VhdlVhpiTypeDescriptor& type,
    const VhdlVhpiValueProfile& profile) const {
  if (profile.type == 0U) {
    return VhdlVhpiValueError::InvalidType;
  }
  if (type.scalar_kind == VhdlVhpiScalarKind::Enumeration) {
    if (profile.enumeration_literals.empty()
        || profile.enumeration_literals.size() > maximum_literals
        || !profile.physical_units.empty()
        || profile.designated_subtype != 0U) {
      return VhdlVhpiValueError::InvalidProfile;
    }
    std::unordered_set<std::string> literals;
    try {
      for (const auto& literal : profile.enumeration_literals) {
        if (!valid_name(literal) || !literals.insert(literal).second) {
          return VhdlVhpiValueError::InvalidProfile;
        }
      }
    } catch (...) {
      return VhdlVhpiValueError::ResourceLimit;
    }
    return VhdlVhpiValueError::None;
  }
  if (type.scalar_kind == VhdlVhpiScalarKind::Physical) {
    if (profile.physical_units.empty()
        || profile.physical_units.size() > maximum_units
        || !profile.enumeration_literals.empty()
        || profile.designated_subtype != 0U
        || profile.physical_units.front().primary_multiplier != 1U) {
      return VhdlVhpiValueError::InvalidProfile;
    }
    std::unordered_set<std::string> units;
    try {
      for (const auto& unit : profile.physical_units) {
        if (!valid_name(unit.name) || unit.primary_multiplier == 0U
            || !units.insert(unit.name).second) {
          return VhdlVhpiValueError::InvalidProfile;
        }
      }
    } catch (...) {
      return VhdlVhpiValueError::ResourceLimit;
    }
    return VhdlVhpiValueError::None;
  }
  if (type.scalar_kind == VhdlVhpiScalarKind::Access) {
    if (!profile.enumeration_literals.empty()
        || !profile.physical_units.empty()
        || profile.designated_subtype == 0U) {
      return VhdlVhpiValueError::InvalidProfile;
    }
    const auto designated = types_->query(profile.designated_subtype);
    return designated ? VhdlVhpiValueError::None
                      : type_error(designated.error);
  }
  return profile.enumeration_literals.empty()
          && profile.physical_units.empty()
          && profile.designated_subtype == 0U
      ? VhdlVhpiValueError::None
      : VhdlVhpiValueError::InvalidProfile;
}

VhdlVhpiValueError VhdlVhpiValueSystem::validate_value(
    const Entry& entry,
    const VhdlVhpiValuePayload& value) const {
  const auto kind = entry.type.scalar_kind;
  if (kind == VhdlVhpiScalarKind::Boolean
      || kind == VhdlVhpiScalarKind::Bit) {
    return std::holds_alternative<bool>(value)
        ? VhdlVhpiValueError::None
        : VhdlVhpiValueError::TypeMismatch;
  }
  if (kind == VhdlVhpiScalarKind::Character) {
    const auto* character = std::get_if<char32_t>(&value);
    if (character == nullptr) {
      return VhdlVhpiValueError::TypeMismatch;
    }
    return *character <= 0x10ffffU
            && !(*character >= 0xd800U && *character <= 0xdfffU)
        ? VhdlVhpiValueError::None
        : VhdlVhpiValueError::RangeViolation;
  }
  if (kind == VhdlVhpiScalarKind::Integer) {
    const auto* integer = std::get_if<std::int64_t>(&value);
    if (integer == nullptr) {
      return VhdlVhpiValueError::TypeMismatch;
    }
    if (entry.type.constraint.kind == VhdlVhpiConstraintKind::Range
        && entry.type.constraint.range) {
      const auto& range = *entry.type.constraint.range;
      if (range.is_null) {
        return VhdlVhpiValueError::RangeViolation;
      }
      const bool inside = range.direction == VhdlVhpiDirection::To
          ? *integer >= range.left && *integer <= range.right
          : *integer <= range.left && *integer >= range.right;
      if (!inside) {
        return VhdlVhpiValueError::RangeViolation;
      }
    }
    return VhdlVhpiValueError::None;
  }
  if (kind == VhdlVhpiScalarKind::Real) {
    const auto* real = std::get_if<double>(&value);
    return real != nullptr && std::isfinite(*real)
        ? VhdlVhpiValueError::None
        : real == nullptr ? VhdlVhpiValueError::TypeMismatch
                          : VhdlVhpiValueError::RangeViolation;
  }
  if (kind == VhdlVhpiScalarKind::Time) {
    return std::holds_alternative<std::uint64_t>(value)
        ? VhdlVhpiValueError::None
        : VhdlVhpiValueError::TypeMismatch;
  }
  if (kind == VhdlVhpiScalarKind::Enumeration) {
    const auto* enumeration =
        std::get_if<VhdlVhpiEnumerationValue>(&value);
    if (enumeration == nullptr) {
      return VhdlVhpiValueError::TypeMismatch;
    }
    return enumeration->position < entry.profile.enumeration_literals.size()
        ? VhdlVhpiValueError::None
        : VhdlVhpiValueError::InvalidPosition;
  }
  if (kind == VhdlVhpiScalarKind::Physical) {
    const auto* physical = std::get_if<VhdlVhpiPhysicalValue>(&value);
    if (physical == nullptr) {
      return VhdlVhpiValueError::TypeMismatch;
    }
    return physical->unit_position < entry.profile.physical_units.size()
        ? VhdlVhpiValueError::None
        : VhdlVhpiValueError::InvalidUnit;
  }
  if (kind == VhdlVhpiScalarKind::Access) {
    const auto* access = std::get_if<VhdlVhpiAccessValue>(&value);
    if (access == nullptr) {
      return VhdlVhpiValueError::TypeMismatch;
    }
    if (access->object == 0U) {
      return VhdlVhpiValueError::None;
    }
    const auto target = types_->declaration_type(access->object);
    if (!target) {
      return type_error(target.error);
    }
    return target.type == entry.profile.designated_subtype
        ? VhdlVhpiValueError::None
        : VhdlVhpiValueError::InvalidAccess;
  }
  return VhdlVhpiValueError::InvalidType;
}

VhdlVhpiValueError VhdlVhpiValueSystem::validate_live(
    const fsim_vhpi_handle_v1 declaration) const {
  const auto type = types_->declaration_type(declaration);
  return type ? VhdlVhpiValueError::None : type_error(type.error);
}

VhdlVhpiValueError VhdlVhpiValueSystem::validate_profile_for_type(
    const fsim_vhpi_handle_v1 type,
    const VhdlVhpiValueProfile& profile) const {
  if (profile.type != type) {
    return VhdlVhpiValueError::InvalidType;
  }
  const auto descriptor = types_->query(type);
  if (!descriptor) {
    return type_error(descriptor.error);
  }
  return validate_profile(descriptor.descriptor, profile);
}

VhdlVhpiValueError VhdlVhpiValueSystem::validate_typed_value(
    const fsim_vhpi_handle_v1 type,
    const VhdlVhpiValueProfile& profile,
    const VhdlVhpiValuePayload& value) const {
  if (profile.type != type) {
    return VhdlVhpiValueError::InvalidType;
  }
  const auto descriptor = types_->query(type);
  if (!descriptor) {
    return type_error(descriptor.error);
  }
  const auto profile_error = validate_profile(descriptor.descriptor, profile);
  if (profile_error != VhdlVhpiValueError::None) {
    return profile_error;
  }
  const Entry entry{descriptor.descriptor, profile, value};
  return validate_value(entry, value);
}

VhdlVhpiValueError VhdlVhpiValueSystem::bind(
    const fsim_vhpi_handle_v1 declaration,
    const VhdlVhpiValueProfile& profile,
    const VhdlVhpiValuePayload& initial) {
  std::scoped_lock lock{mutex_};
  const auto declaration_type = types_->declaration_type(declaration);
  if (!declaration_type) {
    return type_error(declaration_type.error);
  }
  if (declaration_type.type != profile.type) {
    return VhdlVhpiValueError::InvalidType;
  }
  if (entries_.contains(declaration)) {
    return VhdlVhpiValueError::AlreadyBound;
  }
  const auto profile_error =
      validate_profile(declaration_type.descriptor, profile);
  if (profile_error != VhdlVhpiValueError::None) {
    return profile_error;
  }
  Entry entry{declaration_type.descriptor, profile, initial};
  const auto value_error = validate_value(entry, initial);
  if (value_error != VhdlVhpiValueError::None) {
    return value_error;
  }
  try {
    entries_.emplace(declaration, std::move(entry));
  } catch (...) {
    return VhdlVhpiValueError::ResourceLimit;
  }
  return VhdlVhpiValueError::None;
}

VhdlVhpiValueResult VhdlVhpiValueSystem::read(
    const fsim_vhpi_handle_v1 declaration) const {
  std::scoped_lock lock{mutex_};
  const auto live_error = validate_live(declaration);
  if (live_error != VhdlVhpiValueError::None) {
    return {false, live_error};
  }
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return {false, VhdlVhpiValueError::NotBound};
  }
  return {found->second.value, {}};
}

VhdlVhpiValueError VhdlVhpiValueSystem::write(
    const fsim_vhpi_handle_v1 declaration,
    const VhdlVhpiValuePayload& value) {
  std::scoped_lock lock{mutex_};
  const auto live_error = validate_live(declaration);
  if (live_error != VhdlVhpiValueError::None) {
    return live_error;
  }
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return VhdlVhpiValueError::NotBound;
  }
  const auto error = validate_value(found->second, value);
  if (error != VhdlVhpiValueError::None) {
    return error;
  }
  found->second.value = value;
  return VhdlVhpiValueError::None;
}

VhdlVhpiValueProfileResult VhdlVhpiValueSystem::profile(
    const fsim_vhpi_handle_v1 declaration) const {
  std::scoped_lock lock{mutex_};
  const auto live_error = validate_live(declaration);
  if (live_error != VhdlVhpiValueError::None) {
    return {{}, live_error};
  }
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return {{}, VhdlVhpiValueError::NotBound};
  }
  return {found->second.profile, {}};
}

VhdlVhpiTextResult VhdlVhpiValueSystem::enumeration_literal(
    const fsim_vhpi_handle_v1 declaration,
    const std::span<char> buffer) const {
  std::scoped_lock lock{mutex_};
  const auto live_error = validate_live(declaration);
  if (live_error != VhdlVhpiValueError::None) {
    return {{}, live_error};
  }
  const auto found = entries_.find(declaration);
  if (found == entries_.end()) {
    return {{}, VhdlVhpiValueError::NotBound};
  }
  const auto* enumeration =
      std::get_if<VhdlVhpiEnumerationValue>(&found->second.value);
  if (enumeration == nullptr
      || enumeration->position
          >= found->second.profile.enumeration_literals.size()) {
    return {{}, VhdlVhpiValueError::TypeMismatch};
  }
  const auto& literal =
      found->second.profile.enumeration_literals[enumeration->position];
  if (buffer.size() < literal.size()) {
    return {literal.size(), VhdlVhpiValueError::BufferTooSmall};
  }
  std::ranges::copy(literal, buffer.begin());
  return {literal.size(), {}};
}

}  // namespace fsim::runtime
