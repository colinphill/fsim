// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_association.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <ranges>
#include <unordered_set>

namespace fsim::runtime {
namespace {

constexpr std::uint8_t k_association_identity = 0xc7U;
std::atomic<std::uint32_t> next_association_system{1U};

[[nodiscard]] VhdlVhpiAssociationError object_error(
    const VhdlVhpiObjectError error) noexcept {
  return error == VhdlVhpiObjectError::CrossSimulation
      ? VhdlVhpiAssociationError::CrossSimulation
      : VhdlVhpiAssociationError::InvalidHandle;
}

}  // namespace

VhdlVhpiAssociationSystem::VhdlVhpiAssociationSystem(
    VhdlVhpiObjectRegistry& objects) noexcept
    : objects_(&objects),
      system_identity_(
          next_association_system.fetch_add(1U, std::memory_order_relaxed)
          & 0x00ffffffU) {
  if (system_identity_ == 0U) {
    system_identity_ =
        next_association_system.fetch_add(1U, std::memory_order_relaxed)
        & 0x00ffffffU;
  }
}

bool VhdlVhpiAssociationSystem::valid_owner_kind(
    const VhdlVhpiObjectKind kind) noexcept {
  return kind == VhdlVhpiObjectKind::Entity
      || kind == VhdlVhpiObjectKind::Architecture
      || kind == VhdlVhpiObjectKind::Component
      || kind == VhdlVhpiObjectKind::Block
      || kind == VhdlVhpiObjectKind::Generate
      || kind == VhdlVhpiObjectKind::Region;
}

bool VhdlVhpiAssociationSystem::class_matches(
    const VhdlVhpiAssociationClass object_class,
    const VhdlVhpiObjectKind object_kind) noexcept {
  switch (object_class) {
  case VhdlVhpiAssociationClass::Constant:
    return object_kind == VhdlVhpiObjectKind::Constant;
  case VhdlVhpiAssociationClass::Signal:
    return object_kind == VhdlVhpiObjectKind::Signal;
  case VhdlVhpiAssociationClass::Variable:
    return object_kind == VhdlVhpiObjectKind::Variable;
  case VhdlVhpiAssociationClass::File:
    return object_kind == VhdlVhpiObjectKind::File;
  case VhdlVhpiAssociationClass::Type:
    return object_kind == VhdlVhpiObjectKind::Type
        || object_kind == VhdlVhpiObjectKind::Subtype;
  }
  return false;
}

VhdlVhpiAssociationResult<fsim_vhpi_handle_v1>
VhdlVhpiAssociationSystem::root_of(
    fsim_vhpi_handle_v1 object) const {
  for (std::uint32_t depth = 0; depth < 1'024; ++depth) {
    const auto metadata = objects_->lookup_object(object);
    if (!metadata) {
      return {{}, object_error(metadata.error)};
    }
    if (metadata.value.parent == 0U) {
      return {metadata.value.handle, VhdlVhpiAssociationError::None};
    }
    object = metadata.value.parent;
  }
  return {{}, VhdlVhpiAssociationError::ResourceLimit};
}

std::uint64_t VhdlVhpiAssociationSystem::make_identity(
    const std::uint32_t local) const noexcept {
  return (static_cast<std::uint64_t>(k_association_identity) << 56U)
      | (static_cast<std::uint64_t>(system_identity_) << 32U)
      | local;
}

VhdlVhpiAssociationError VhdlVhpiAssociationSystem::validate_identity(
    const std::uint64_t identity) const noexcept {
  if (identity == 0U
      || static_cast<std::uint8_t>(identity >> 56U)
          != k_association_identity
      || static_cast<std::uint32_t>(identity) == 0U) {
    return VhdlVhpiAssociationError::InvalidHandle;
  }
  const auto owner =
      static_cast<std::uint32_t>((identity >> 32U) & 0x00ffffffU);
  return owner == system_identity_ ? VhdlVhpiAssociationError::None
                                   : VhdlVhpiAssociationError::CrossSimulation;
}

VhdlVhpiAssociationError VhdlVhpiAssociationSystem::publish(
    const fsim_vhpi_handle_v1 owner,
    const std::span<const VhdlVhpiAssociationProfile> profiles) {
  if (objects_ == nullptr || !objects_->valid()) {
    return VhdlVhpiAssociationError::InvalidSimulation;
  }
  if (profiles.empty()) {
    return VhdlVhpiAssociationError::InvalidProfile;
  }
  const auto owner_metadata = objects_->lookup_object(owner);
  if (!owner_metadata) {
    return object_error(owner_metadata.error);
  }
  if (!valid_owner_kind(owner_metadata.value.kind)) {
    return VhdlVhpiAssociationError::InvalidOwner;
  }
  const auto root = root_of(owner);
  if (!root) {
    return root.error;
  }

  std::vector<VhdlVhpiAssociationDescriptor> pending;
  std::unordered_set<fsim_vhpi_handle_v1> formals;
  try {
    pending.reserve(profiles.size());
    for (const auto& profile : profiles) {
      if (static_cast<std::uint32_t>(profile.kind)
              > static_cast<std::uint32_t>(
                  VhdlVhpiAssociationKind::Port)
          || static_cast<std::uint32_t>(profile.mode)
              > static_cast<std::uint32_t>(
                  VhdlVhpiAssociationMode::Linkage)
          || static_cast<std::uint32_t>(profile.object_class)
              > static_cast<std::uint32_t>(
                  VhdlVhpiAssociationClass::Type)
          || static_cast<std::uint32_t>(profile.actual_kind)
              > static_cast<std::uint32_t>(
                  VhdlVhpiActualKind::Disconnected)) {
        return VhdlVhpiAssociationError::InvalidProfile;
      }
      if (!formals.emplace(profile.formal).second) {
        return VhdlVhpiAssociationError::DuplicateFormal;
      }
      const auto formal = objects_->lookup_object(profile.formal);
      if (!formal || !class_matches(
              profile.object_class, formal.value.kind)) {
        return formal.error == VhdlVhpiObjectError::CrossSimulation
            ? VhdlVhpiAssociationError::CrossSimulation
            : VhdlVhpiAssociationError::InvalidFormal;
      }
      const auto formal_root = root_of(profile.formal);
      if (!formal_root || formal_root.value != root.value) {
        return formal_root
            ? VhdlVhpiAssociationError::CrossRoot : formal_root.error;
      }
      if (profile.kind == VhdlVhpiAssociationKind::Port
          && profile.object_class != VhdlVhpiAssociationClass::Signal) {
        return VhdlVhpiAssociationError::InvalidProfile;
      }
      if (profile.actual_kind == VhdlVhpiActualKind::Object) {
        const auto actual = objects_->lookup_object(profile.actual);
        if (!actual || !class_matches(
                profile.object_class, actual.value.kind)) {
          return actual.error == VhdlVhpiObjectError::CrossSimulation
              ? VhdlVhpiAssociationError::CrossSimulation
              : VhdlVhpiAssociationError::InvalidActual;
        }
        const auto actual_root = root_of(profile.actual);
        if (!actual_root || actual_root.value != root.value) {
          return actual_root
              ? VhdlVhpiAssociationError::CrossRoot : actual_root.error;
        }
      } else if (profile.actual != 0U
                 || (profile.actual_kind
                         == VhdlVhpiActualKind::Disconnected
                     && profile.kind
                         != VhdlVhpiAssociationKind::Port)) {
        return VhdlVhpiAssociationError::InvalidActual;
      }
      pending.push_back(VhdlVhpiAssociationDescriptor{
          0,
          owner,
          root.value,
          profile.kind,
          profile.formal,
          profile.actual,
          profile.actual_kind,
          profile.mode,
          profile.object_class,
          0,
          profile.object_user_data,
          profile.call_user_data});
    }
  } catch (...) {
    return VhdlVhpiAssociationError::ResourceLimit;
  }

  std::scoped_lock lock{mutex_};
  if (owners_.contains(owner)) {
    return VhdlVhpiAssociationError::AlreadyPublished;
  }
  if (next_association_ == 0U
      || profiles.size()
          > static_cast<std::size_t>(
              std::numeric_limits<std::uint32_t>::max()
              - next_association_)) {
    return VhdlVhpiAssociationError::ResourceLimit;
  }
  std::vector<std::uint64_t> identities;
  try {
    identities.reserve(pending.size());
    for (auto& descriptor : pending) {
      descriptor.identity = make_identity(next_association_++);
      descriptor.ordinal = next_ordinal_++;
      identities.push_back(descriptor.identity);
      associations_.emplace(descriptor.identity, descriptor);
    }
    owners_.emplace(owner, std::move(identities));
  } catch (...) {
    for (const auto& descriptor : pending) {
      associations_.erase(descriptor.identity);
    }
    return VhdlVhpiAssociationError::ResourceLimit;
  }
  return VhdlVhpiAssociationError::None;
}

VhdlVhpiAssociationDescriptorResult
VhdlVhpiAssociationSystem::association(
    const std::uint64_t identity) const {
  const auto error = validate_identity(identity);
  if (error != VhdlVhpiAssociationError::None) {
    return {{}, error};
  }
  std::scoped_lock lock{mutex_};
  const auto found = associations_.find(identity);
  return found == associations_.end()
      ? VhdlVhpiAssociationDescriptorResult{
            {}, VhdlVhpiAssociationError::NotFound}
      : VhdlVhpiAssociationDescriptorResult{
            found->second, VhdlVhpiAssociationError::None};
}

VhdlVhpiAssociationListResult VhdlVhpiAssociationSystem::associations(
    const fsim_vhpi_handle_v1 owner,
    const VhdlVhpiAssociationKind kind) const {
  const auto checked_owner = objects_->lookup_object(owner);
  if (!checked_owner) {
    return {{}, object_error(checked_owner.error)};
  }
  if (static_cast<std::uint32_t>(kind)
      > static_cast<std::uint32_t>(VhdlVhpiAssociationKind::Port)) {
    return {{}, VhdlVhpiAssociationError::InvalidProfile};
  }
  std::scoped_lock lock{mutex_};
  const auto found = owners_.find(owner);
  if (found == owners_.end()) {
    return {{}, VhdlVhpiAssociationError::NotFound};
  }
  std::vector<VhdlVhpiAssociationDescriptor> result;
  try {
    for (const auto identity : found->second) {
      const auto& descriptor = associations_.at(identity);
      if (descriptor.kind == kind) {
        result.push_back(descriptor);
      }
    }
  } catch (...) {
    return {{}, VhdlVhpiAssociationError::ResourceLimit};
  }
  return {std::move(result), VhdlVhpiAssociationError::None};
}

VhdlVhpiAssociationError VhdlVhpiAssociationSystem::set_user_data(
    const std::uint64_t identity,
    const std::uint64_t object_user_data,
    const std::uint64_t call_user_data) {
  const auto error = validate_identity(identity);
  if (error != VhdlVhpiAssociationError::None) {
    return error;
  }
  std::scoped_lock lock{mutex_};
  const auto found = associations_.find(identity);
  if (found == associations_.end()) {
    return VhdlVhpiAssociationError::NotFound;
  }
  found->second.object_user_data = object_user_data;
  found->second.call_user_data = call_user_data;
  return VhdlVhpiAssociationError::None;
}

}  // namespace fsim::runtime
