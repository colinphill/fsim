// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_special.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::uint8_t k_file_identity = 0xf1U;
constexpr std::uint8_t k_lease_identity = 0xf2U;
constexpr std::size_t k_max_logic_width = 1U << 20U;
constexpr std::size_t k_max_logical_name = 4096U;
constexpr std::size_t k_max_provenance = 4096U;

std::atomic<std::uint32_t> next_special_system{1U};

[[nodiscard]] bool caller_kind(const VhdlVhpiObjectKind kind) noexcept {
  return kind == VhdlVhpiObjectKind::Process
      || kind == VhdlVhpiObjectKind::Subprogram;
}

}  // namespace

VhdlVhpiSpecialSystem::VhdlVhpiSpecialSystem(
    VhdlVhpiObjectRegistry& objects,
    VhdlVhpiTypeSystem& types) noexcept
    : objects_(&objects), types_(&types),
      system_identity_(
          next_special_system.fetch_add(1U, std::memory_order_relaxed)
          & 0x00ffffffU) {
  if (system_identity_ == 0U) {
    system_identity_ =
        next_special_system.fetch_add(1U, std::memory_order_relaxed)
        & 0x00ffffffU;
  }
}

VhdlVhpiSpecialError VhdlVhpiSpecialSystem::object_error(
    const VhdlVhpiObjectError error) noexcept {
  switch (error) {
  case VhdlVhpiObjectError::None:
    return VhdlVhpiSpecialError::None;
  case VhdlVhpiObjectError::InvalidSimulation:
    return VhdlVhpiSpecialError::InvalidSimulation;
  case VhdlVhpiObjectError::CrossSimulation:
    return VhdlVhpiSpecialError::CrossSimulation;
  case VhdlVhpiObjectError::StaleHandle:
    return VhdlVhpiSpecialError::StaleHandle;
  case VhdlVhpiObjectError::ReleasedHandle:
    return VhdlVhpiSpecialError::ReleasedHandle;
  case VhdlVhpiObjectError::ResourceLimit:
    return VhdlVhpiSpecialError::ResourceLimit;
  default:
    return VhdlVhpiSpecialError::InvalidHandle;
  }
}

bool VhdlVhpiSpecialSystem::valid_file_access(
    const VhdlVhpiFileAccess access) noexcept {
  return static_cast<std::uint32_t>(access)
      <= static_cast<std::uint32_t>(VhdlVhpiFileAccess::Append);
}

bool VhdlVhpiSpecialSystem::permits(
    const VhdlVhpiFileAccess granted,
    const VhdlVhpiFileAccess requested) noexcept {
  if (!valid_file_access(requested)) {
    return false;
  }
  if (granted == VhdlVhpiFileAccess::ReadWrite) {
    return requested != VhdlVhpiFileAccess::Append;
  }
  return granted == requested;
}

std::uint64_t VhdlVhpiSpecialSystem::make_identity(
    const std::uint8_t kind, const std::uint32_t local) const noexcept {
  return (static_cast<std::uint64_t>(kind) << 56U)
      | (static_cast<std::uint64_t>(system_identity_) << 32U)
      | local;
}

VhdlVhpiSpecialError VhdlVhpiSpecialSystem::validate_identity(
    const std::uint64_t identity, const std::uint8_t kind) const noexcept {
  if (identity == 0U || static_cast<std::uint8_t>(identity >> 56U) != kind
      || static_cast<std::uint32_t>(identity) == 0U) {
    return VhdlVhpiSpecialError::InvalidResource;
  }
  const auto owner =
      static_cast<std::uint32_t>((identity >> 32U) & 0x00ffffffU);
  return owner == system_identity_ ? VhdlVhpiSpecialError::None
                                   : VhdlVhpiSpecialError::CrossSimulation;
}

VhdlVhpiFileResult VhdlVhpiSpecialSystem::open_file(
    const fsim_vhpi_handle_v1 declaration,
    const VhdlVhpiFileAccess access,
    std::string logical_name) {
  std::scoped_lock lock{mutex_};
  if (!objects_->valid()) {
    return {{}, VhdlVhpiSpecialError::InvalidSimulation};
  }
  if (!valid_file_access(access) || logical_name.empty()
      || logical_name.size() > k_max_logical_name
      || logical_name.find('\0') != std::string::npos) {
    return {{}, VhdlVhpiSpecialError::InvalidAccess};
  }
  const auto object = objects_->lookup_object(declaration);
  if (!object) {
    return {{}, object_error(object.error)};
  }
  if (object.value.kind != VhdlVhpiObjectKind::File) {
    return {{}, VhdlVhpiSpecialError::InvalidDeclaration};
  }
  const auto declaration_type = types_->declaration_type(declaration);
  if (!declaration_type
      || declaration_type.descriptor.scalar_kind
          != VhdlVhpiScalarKind::File) {
    return {{}, VhdlVhpiSpecialError::InvalidType};
  }
  if (open_files_.contains(declaration)) {
    return {{}, VhdlVhpiSpecialError::AlreadyOpen};
  }
  if (next_file_ == 0U) {
    return {{}, VhdlVhpiSpecialError::ResourceLimit};
  }
  const auto identity = make_identity(k_file_identity, next_file_++);
  VhdlVhpiFileState state{
      identity,
      declaration,
      declaration_type.type,
      access,
      std::move(logical_name),
      true};
  try {
    files_.emplace(identity, FileEntry{state});
    open_files_.emplace(declaration, identity);
  } catch (...) {
    files_.erase(identity);
    return {{}, VhdlVhpiSpecialError::ResourceLimit};
  }
  return {std::move(state), VhdlVhpiSpecialError::None};
}

VhdlVhpiFileResult VhdlVhpiSpecialSystem::query_file(
    const std::uint64_t identity,
    const VhdlVhpiFileAccess requested_access) const {
  std::scoped_lock lock{mutex_};
  const auto identity_error =
      validate_identity(identity, k_file_identity);
  if (identity_error != VhdlVhpiSpecialError::None) {
    return {{}, identity_error};
  }
  const auto found = files_.find(identity);
  if (found == files_.end() || !found->second.state.open) {
    return {{}, VhdlVhpiSpecialError::NotOpen};
  }
  if (!permits(found->second.state.access, requested_access)) {
    return {{}, VhdlVhpiSpecialError::InvalidAccess};
  }
  return {found->second.state, VhdlVhpiSpecialError::None};
}

VhdlVhpiSpecialError VhdlVhpiSpecialSystem::close_file(
    const std::uint64_t identity) {
  std::scoped_lock lock{mutex_};
  const auto identity_error =
      validate_identity(identity, k_file_identity);
  if (identity_error != VhdlVhpiSpecialError::None) {
    return identity_error;
  }
  const auto found = files_.find(identity);
  if (found == files_.end() || !found->second.state.open) {
    return VhdlVhpiSpecialError::NotOpen;
  }
  found->second.state.open = false;
  open_files_.erase(found->second.state.declaration);
  return VhdlVhpiSpecialError::None;
}

VhdlVhpiSpecialError VhdlVhpiSpecialSystem::register_protected(
    const fsim_vhpi_handle_v1 declaration) {
  std::scoped_lock lock{mutex_};
  const auto object = objects_->lookup_object(declaration);
  if (!object) {
    return object_error(object.error);
  }
  if (object.value.kind != VhdlVhpiObjectKind::Variable) {
    return VhdlVhpiSpecialError::InvalidDeclaration;
  }
  const auto declaration_type = types_->declaration_type(declaration);
  if (!declaration_type
      || declaration_type.descriptor.scalar_kind
          != VhdlVhpiScalarKind::Protected) {
    return VhdlVhpiSpecialError::InvalidType;
  }
  try {
    if (!protected_.emplace(declaration, ProtectedEntry{}).second) {
      return VhdlVhpiSpecialError::AlreadyBound;
    }
  } catch (...) {
    return VhdlVhpiSpecialError::ResourceLimit;
  }
  return VhdlVhpiSpecialError::None;
}

VhdlVhpiProtectedLeaseResult
VhdlVhpiSpecialSystem::acquire_protected(
    const fsim_vhpi_handle_v1 declaration,
    const fsim_vhpi_handle_v1 caller,
    const VhdlVhpiProtectedAccess access) {
  std::scoped_lock lock{mutex_};
  if (static_cast<std::uint32_t>(access)
      > static_cast<std::uint32_t>(
          VhdlVhpiProtectedAccess::Exclusive)) {
    return {0, VhdlVhpiSpecialError::InvalidAccess};
  }
  const auto caller_object = objects_->lookup_object(caller);
  if (!caller_object) {
    return {0, object_error(caller_object.error)};
  }
  if (!caller_kind(caller_object.value.kind)) {
    return {0, VhdlVhpiSpecialError::InvalidAccess};
  }
  const auto found = protected_.find(declaration);
  if (found == protected_.end()) {
    return {0, VhdlVhpiSpecialError::NotBound};
  }
  if (found->second.exclusive != 0U
      || (access == VhdlVhpiProtectedAccess::Exclusive
          && found->second.shared != 0U)) {
    return {0, VhdlVhpiSpecialError::Busy};
  }
  if (next_lease_ == 0U) {
    return {0, VhdlVhpiSpecialError::ResourceLimit};
  }
  const auto lease = make_identity(k_lease_identity, next_lease_++);
  try {
    leases_.emplace(lease, LeaseEntry{declaration, caller, access});
  } catch (...) {
    return {0, VhdlVhpiSpecialError::ResourceLimit};
  }
  if (access == VhdlVhpiProtectedAccess::Exclusive) {
    found->second.exclusive = lease;
  } else {
    ++found->second.shared;
  }
  return {lease, VhdlVhpiSpecialError::None};
}

VhdlVhpiSpecialError VhdlVhpiSpecialSystem::release_protected(
    const std::uint64_t lease, const fsim_vhpi_handle_v1 caller) {
  std::scoped_lock lock{mutex_};
  const auto identity_error =
      validate_identity(lease, k_lease_identity);
  if (identity_error != VhdlVhpiSpecialError::None) {
    return identity_error;
  }
  const auto found = leases_.find(lease);
  if (found == leases_.end()) {
    return VhdlVhpiSpecialError::InvalidResource;
  }
  const auto caller_object = objects_->lookup_object(caller);
  if (!caller_object) {
    return object_error(caller_object.error);
  }
  if (found->second.caller != caller) {
    return VhdlVhpiSpecialError::InvalidAccess;
  }
  auto protected_entry = protected_.find(found->second.declaration);
  if (protected_entry == protected_.end()) {
    return VhdlVhpiSpecialError::NotBound;
  }
  if (found->second.access == VhdlVhpiProtectedAccess::Exclusive) {
    protected_entry->second.exclusive = 0U;
  } else if (protected_entry->second.shared != 0U) {
    --protected_entry->second.shared;
  }
  leases_.erase(found);
  return VhdlVhpiSpecialError::None;
}

VhdlVhpiSpecialError VhdlVhpiSpecialSystem::publish_resolver(
    const VhdlVhpiResolverDescriptor& descriptor) {
  std::scoped_lock lock{mutex_};
  if (descriptor.provenance.empty()
      || descriptor.provenance.size() > k_max_provenance
      || descriptor.provenance.find('\0') != std::string::npos) {
    return VhdlVhpiSpecialError::InvalidResolver;
  }
  const auto type = types_->query(descriptor.type);
  if (!type) {
    return VhdlVhpiSpecialError::InvalidType;
  }
  if (!type.descriptor.resolved
      || type.descriptor.resolution_function != descriptor.subprogram) {
    return VhdlVhpiSpecialError::InvalidResolver;
  }
  const auto resolver_object =
      objects_->lookup_object(descriptor.subprogram);
  if (!resolver_object) {
    return object_error(resolver_object.error);
  }
  if (resolver_object.value.kind != VhdlVhpiObjectKind::Subprogram) {
    return VhdlVhpiSpecialError::InvalidResolver;
  }
  try {
    if (!resolvers_.emplace(descriptor.type, descriptor).second) {
      return VhdlVhpiSpecialError::AlreadyBound;
    }
  } catch (...) {
    return VhdlVhpiSpecialError::ResourceLimit;
  }
  return VhdlVhpiSpecialError::None;
}

VhdlVhpiResolverResult VhdlVhpiSpecialSystem::resolver(
    const fsim_vhpi_handle_v1 type) const {
  std::scoped_lock lock{mutex_};
  const auto found = resolvers_.find(type);
  if (found == resolvers_.end()) {
    return {{}, VhdlVhpiSpecialError::NotBound};
  }
  return {found->second, VhdlVhpiSpecialError::None};
}

VhdlVhpiSpecialError VhdlVhpiSpecialSystem::bind_logic(
    const fsim_vhpi_handle_v1 declaration,
    const PackedLogic9& initial) {
  std::scoped_lock lock{mutex_};
  if (initial.empty() || initial.width() > k_max_logic_width) {
    return VhdlVhpiSpecialError::InvalidValue;
  }
  const auto object = objects_->lookup_object(declaration);
  if (!object) {
    return object_error(object.error);
  }
  if (object.value.kind != VhdlVhpiObjectKind::Signal
      && object.value.kind != VhdlVhpiObjectKind::Variable
      && object.value.kind != VhdlVhpiObjectKind::Constant) {
    return VhdlVhpiSpecialError::InvalidDeclaration;
  }
  const auto declaration_type = types_->declaration_type(declaration);
  if (!declaration_type
      || declaration_type.descriptor.scalar_kind
          != VhdlVhpiScalarKind::Logic9) {
    return VhdlVhpiSpecialError::InvalidType;
  }
  try {
    if (!logic_
             .emplace(
                 declaration,
                 LogicEntry{declaration_type.type, initial})
             .second) {
      return VhdlVhpiSpecialError::AlreadyBound;
    }
  } catch (...) {
    return VhdlVhpiSpecialError::ResourceLimit;
  }
  return VhdlVhpiSpecialError::None;
}

VhdlVhpiLogicResult VhdlVhpiSpecialSystem::read_logic(
    const fsim_vhpi_handle_v1 declaration) const {
  std::scoped_lock lock{mutex_};
  const auto found = logic_.find(declaration);
  if (found == logic_.end()) {
    return {PackedLogic9{}, VhdlVhpiSpecialError::NotBound};
  }
  const auto object = objects_->lookup_object(declaration);
  if (!object) {
    return {PackedLogic9{}, object_error(object.error)};
  }
  return {found->second.value, VhdlVhpiSpecialError::None};
}

VhdlVhpiSpecialError VhdlVhpiSpecialSystem::write_logic(
    const fsim_vhpi_handle_v1 declaration,
    const PackedLogic9& value) {
  std::scoped_lock lock{mutex_};
  const auto found = logic_.find(declaration);
  if (found == logic_.end()) {
    return VhdlVhpiSpecialError::NotBound;
  }
  const auto object = objects_->lookup_object(declaration);
  if (!object) {
    return object_error(object.error);
  }
  if (value.width() != found->second.value.width()) {
    return VhdlVhpiSpecialError::InvalidValue;
  }
  try {
    auto replacement = value;
    found->second.value = std::move(replacement);
  } catch (...) {
    return VhdlVhpiSpecialError::ResourceLimit;
  }
  return VhdlVhpiSpecialError::None;
}

VhdlVhpiLogicBufferResult VhdlVhpiSpecialSystem::encode_logic(
    const fsim_vhpi_handle_v1 declaration,
    const std::span<std::uint8_t> buffer) const {
  std::scoped_lock lock{mutex_};
  const auto found = logic_.find(declaration);
  if (found == logic_.end()) {
    return {0, VhdlVhpiSpecialError::NotBound};
  }
  const auto object = objects_->lookup_object(declaration);
  if (!object) {
    return {0, object_error(object.error)};
  }
  const auto required = found->second.value.width();
  if (buffer.size() < required) {
    return {required, VhdlVhpiSpecialError::BufferTooSmall};
  }
  for (std::size_t index = 0; index < required; ++index) {
    buffer[index] = static_cast<std::uint8_t>(
        found->second.value.get(required - index - 1U));
  }
  return {required, VhdlVhpiSpecialError::None};
}

}  // namespace fsim::runtime
