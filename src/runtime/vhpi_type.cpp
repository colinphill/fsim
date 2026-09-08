// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_type.hpp"

namespace fsim::runtime {

namespace {

constexpr std::uint32_t maximum_constraint_depth = 64;
constexpr std::uint32_t maximum_constraint_nodes = 4096;
constexpr std::size_t maximum_constraint_children = 1024;

bool declaration_kind(const VhdlVhpiObjectKind kind) noexcept {
  switch (kind) {
    case VhdlVhpiObjectKind::Signal:
    case VhdlVhpiObjectKind::Variable:
    case VhdlVhpiObjectKind::Constant:
    case VhdlVhpiObjectKind::File:
    case VhdlVhpiObjectKind::Port:
    case VhdlVhpiObjectKind::Generic:
    case VhdlVhpiObjectKind::Alias:
    case VhdlVhpiObjectKind::RecordElement:
    case VhdlVhpiObjectKind::ArrayElement:
      return true;
    default:
      return false;
  }
}

}  // namespace

VhdlVhpiTypeSystem::VhdlVhpiTypeSystem(
    VhdlVhpiObjectRegistry& objects) noexcept
    : objects_(&objects) {}

VhdlVhpiTypeError VhdlVhpiTypeSystem::object_error(
    const VhdlVhpiObjectError error) noexcept {
  switch (error) {
    case VhdlVhpiObjectError::InvalidSimulation:
      return VhdlVhpiTypeError::InvalidSimulation;
    case VhdlVhpiObjectError::CrossSimulation:
      return VhdlVhpiTypeError::CrossSimulation;
    case VhdlVhpiObjectError::StaleHandle:
      return VhdlVhpiTypeError::StaleHandle;
    case VhdlVhpiObjectError::ReleasedHandle:
      return VhdlVhpiTypeError::ReleasedHandle;
    default:
      return VhdlVhpiTypeError::InvalidHandle;
  }
}

VhdlVhpiTypeError VhdlVhpiTypeSystem::validate_constraint(
    const VhdlVhpiConstraint& constraint,
    const std::uint32_t depth,
    std::uint32_t& nodes) noexcept {
  if (depth > maximum_constraint_depth) {
    return VhdlVhpiTypeError::DepthLimit;
  }
  if (++nodes > maximum_constraint_nodes) {
    return VhdlVhpiTypeError::NodeLimit;
  }
  if (static_cast<std::uint32_t>(constraint.kind)
      > static_cast<std::uint32_t>(VhdlVhpiConstraintKind::Record)) {
    return VhdlVhpiTypeError::InvalidConstraint;
  }
  switch (constraint.kind) {
    case VhdlVhpiConstraintKind::None:
      return !constraint.range && constraint.children.empty()
          ? VhdlVhpiTypeError::None
          : VhdlVhpiTypeError::InvalidConstraint;
    case VhdlVhpiConstraintKind::Range: {
      if (!constraint.range || !constraint.children.empty()) {
        return VhdlVhpiTypeError::InvalidConstraint;
      }
      const auto& range = *constraint.range;
      if (static_cast<std::uint32_t>(range.direction)
          > static_cast<std::uint32_t>(VhdlVhpiDirection::Downto)) {
        return VhdlVhpiTypeError::InvalidRange;
      }
      const bool ordered = range.direction == VhdlVhpiDirection::To
          ? range.left <= range.right
          : range.left >= range.right;
      if (range.is_null == ordered) {
        return VhdlVhpiTypeError::InvalidRange;
      }
      return VhdlVhpiTypeError::None;
    }
    case VhdlVhpiConstraintKind::Array:
    case VhdlVhpiConstraintKind::Record:
      if (constraint.range || constraint.children.empty()
          || constraint.children.size() > maximum_constraint_children) {
        return VhdlVhpiTypeError::InvalidConstraint;
      }
      for (const auto& child : constraint.children) {
        const auto error = validate_constraint(child, depth + 1U, nodes);
        if (error != VhdlVhpiTypeError::None) {
          return error;
        }
      }
      return VhdlVhpiTypeError::None;
  }
  return VhdlVhpiTypeError::InvalidConstraint;
}

VhdlVhpiTypeError VhdlVhpiTypeSystem::publish(
    const fsim_vhpi_handle_v1 type,
    const VhdlVhpiTypeDescriptor& descriptor) {
  std::scoped_lock lock{mutex_};
  const auto metadata = objects_->lookup_object(type);
  if (!metadata) {
    return object_error(metadata.error);
  }
  if (metadata.value.kind != VhdlVhpiObjectKind::Type
      && metadata.value.kind != VhdlVhpiObjectKind::Subtype) {
    return VhdlVhpiTypeError::InvalidTypeObject;
  }
  if (types_.contains(type)) {
    return VhdlVhpiTypeError::AlreadyPublished;
  }
  if (static_cast<std::uint32_t>(descriptor.scalar_kind)
      > static_cast<std::uint32_t>(VhdlVhpiScalarKind::Logic9)) {
    return VhdlVhpiTypeError::InvalidScalarKind;
  }
  if (metadata.value.kind == VhdlVhpiObjectKind::Type) {
    if (descriptor.base_type != 0U) {
      return VhdlVhpiTypeError::InvalidBaseType;
    }
  } else {
    if (descriptor.base_type == 0U) {
      return VhdlVhpiTypeError::InvalidBaseType;
    }
    const auto base_metadata =
        objects_->lookup_object(descriptor.base_type);
    if (!base_metadata) {
      return object_error(base_metadata.error);
    }
    if ((base_metadata.value.kind != VhdlVhpiObjectKind::Type
            && base_metadata.value.kind != VhdlVhpiObjectKind::Subtype)
        || !types_.contains(descriptor.base_type)) {
      return VhdlVhpiTypeError::InvalidBaseType;
    }
  }
  if (descriptor.resolved) {
    const auto resolution =
        objects_->lookup_object(descriptor.resolution_function);
    if (!resolution) {
      return object_error(resolution.error);
    }
    if (resolution.value.kind != VhdlVhpiObjectKind::Subprogram) {
      return VhdlVhpiTypeError::InvalidResolution;
    }
  } else if (descriptor.resolution_function != 0U) {
    return VhdlVhpiTypeError::InvalidResolution;
  }

  std::uint32_t nodes{};
  const auto constraint_error =
      validate_constraint(descriptor.constraint, 1, nodes);
  if (constraint_error != VhdlVhpiTypeError::None) {
    return constraint_error;
  }
  try {
    types_.emplace(type, descriptor);
  } catch (...) {
    return VhdlVhpiTypeError::ResourceLimit;
  }
  return VhdlVhpiTypeError::None;
}

VhdlVhpiTypeResult VhdlVhpiTypeSystem::query(
    const fsim_vhpi_handle_v1 type) const {
  std::scoped_lock lock{mutex_};
  const auto metadata = objects_->lookup_object(type);
  if (!metadata) {
    return {{}, {}, object_error(metadata.error)};
  }
  const auto found = types_.find(type);
  if (found == types_.end()) {
    return {{}, {}, VhdlVhpiTypeError::NotFound};
  }
  return {type, found->second, {}};
}

VhdlVhpiTypeHandleResult VhdlVhpiTypeSystem::base_type(
    const fsim_vhpi_handle_v1 type) const {
  std::scoped_lock lock{mutex_};
  const auto metadata = objects_->lookup_object(type);
  if (!metadata) {
    return {{}, object_error(metadata.error)};
  }
  auto current = type;
  for (std::uint32_t depth = 0; depth <= maximum_constraint_depth; ++depth) {
    const auto found = types_.find(current);
    if (found == types_.end()) {
      return {{}, VhdlVhpiTypeError::NotFound};
    }
    if (found->second.base_type == 0U) {
      return {current, {}};
    }
    current = found->second.base_type;
  }
  return {{}, VhdlVhpiTypeError::DepthLimit};
}

VhdlVhpiTypeError VhdlVhpiTypeSystem::bind_declaration(
    const fsim_vhpi_handle_v1 declaration,
    const fsim_vhpi_handle_v1 type) {
  std::scoped_lock lock{mutex_};
  const auto declaration_metadata = objects_->lookup_object(declaration);
  if (!declaration_metadata) {
    return object_error(declaration_metadata.error);
  }
  if (!declaration_kind(declaration_metadata.value.kind)) {
    return VhdlVhpiTypeError::InvalidDeclaration;
  }
  const auto type_metadata = objects_->lookup_object(type);
  if (!type_metadata) {
    return object_error(type_metadata.error);
  }
  if (!types_.contains(type)) {
    return VhdlVhpiTypeError::InvalidBaseType;
  }
  if (declarations_.contains(declaration)) {
    return VhdlVhpiTypeError::AlreadyPublished;
  }
  try {
    declarations_.emplace(declaration, type);
  } catch (...) {
    return VhdlVhpiTypeError::ResourceLimit;
  }
  return VhdlVhpiTypeError::None;
}

VhdlVhpiDeclarationTypeResult VhdlVhpiTypeSystem::declaration_type(
    const fsim_vhpi_handle_v1 declaration) const {
  std::scoped_lock lock{mutex_};
  const auto metadata = objects_->lookup_object(declaration);
  if (!metadata) {
    return {{}, {}, {}, object_error(metadata.error)};
  }
  const auto binding = declarations_.find(declaration);
  if (binding == declarations_.end()) {
    return {{}, {}, {}, VhdlVhpiTypeError::NotFound};
  }
  const auto type = types_.find(binding->second);
  if (type == types_.end()) {
    return {{}, {}, {}, VhdlVhpiTypeError::NotFound};
  }
  return {declaration, binding->second, type->second, {}};
}

}  // namespace fsim::runtime
