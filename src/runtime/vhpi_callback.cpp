// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_callback.hpp"

#include <algorithm>
#include <atomic>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::uint8_t k_callback_identity = 0xc2U;
constexpr std::size_t maximum_callbacks = 65'536;
std::atomic<std::uint32_t> next_callback_system{1U};

[[nodiscard]] VhdlVhpiCallbackError object_error(
    const VhdlVhpiObjectError error) noexcept {
  if (error == VhdlVhpiObjectError::CrossSimulation) {
    return VhdlVhpiCallbackError::CrossSimulation;
  }
  return VhdlVhpiCallbackError::InvalidObject;
}

}  // namespace

VhdlVhpiCallbackSystem::VhdlVhpiCallbackSystem(
    VhdlVhpiObjectRegistry& objects, Scheduler& scheduler) noexcept
    : objects_(&objects),
      scheduler_(&scheduler),
      system_identity_(
          next_callback_system.fetch_add(1U, std::memory_order_relaxed)
          & 0x00ffffffU) {
  if (system_identity_ == 0U) {
    system_identity_ =
        next_callback_system.fetch_add(1U, std::memory_order_relaxed)
        & 0x00ffffffU;
  }
}

VhdlVhpiCallbackSystem::~VhdlVhpiCallbackSystem() {
  teardown();
}

bool VhdlVhpiCallbackSystem::valid_kind(
    const VhdlVhpiCallbackKind kind) noexcept {
  return static_cast<std::uint32_t>(kind)
      <= static_cast<std::uint32_t>(VhdlVhpiCallbackKind::EndOfReset);
}

bool VhdlVhpiCallbackSystem::lifecycle_kind(
    const VhdlVhpiCallbackKind kind) noexcept {
  return static_cast<std::uint32_t>(kind)
      >= static_cast<std::uint32_t>(
          VhdlVhpiCallbackKind::StartOfSimulation);
}

bool VhdlVhpiCallbackSystem::valid() const noexcept {
  return objects_ != nullptr && scheduler_ != nullptr
      && objects_->valid();
}

VhdlVhpiCallbackError VhdlVhpiCallbackSystem::validate_object(
    const VhdlVhpiCallbackKind kind,
    const fsim_vhpi_handle_v1 object) const {
  if (lifecycle_kind(kind)) {
    return object == 0U ? VhdlVhpiCallbackError::None
                        : VhdlVhpiCallbackError::InvalidRequest;
  }
  if (object == 0U) {
    return VhdlVhpiCallbackError::None;
  }
  const auto metadata = objects_->lookup_object(object);
  if (!metadata) {
    return object_error(metadata.error);
  }
  const auto object_kind = metadata.value.kind;
  switch (kind) {
  case VhdlVhpiCallbackKind::Signal:
  case VhdlVhpiCallbackKind::Transaction:
    return object_kind == VhdlVhpiObjectKind::Signal
        ? VhdlVhpiCallbackError::None
        : VhdlVhpiCallbackError::InvalidObject;
  case VhdlVhpiCallbackKind::Process:
    return object_kind == VhdlVhpiObjectKind::Process
        ? VhdlVhpiCallbackError::None
        : VhdlVhpiCallbackError::InvalidObject;
  case VhdlVhpiCallbackKind::Assertion:
    return object_kind == VhdlVhpiObjectKind::Process
            || object_kind == VhdlVhpiObjectKind::Subprogram
            || object_kind == VhdlVhpiObjectKind::Region
        ? VhdlVhpiCallbackError::None
        : VhdlVhpiCallbackError::InvalidObject;
  case VhdlVhpiCallbackKind::Event:
    return VhdlVhpiCallbackError::None;
  case VhdlVhpiCallbackKind::StartOfSimulation:
  case VhdlVhpiCallbackKind::EndOfSimulation:
  case VhdlVhpiCallbackKind::StartOfSave:
  case VhdlVhpiCallbackKind::EndOfSave:
  case VhdlVhpiCallbackKind::StartOfRestart:
  case VhdlVhpiCallbackKind::EndOfRestart:
  case VhdlVhpiCallbackKind::StartOfReset:
  case VhdlVhpiCallbackKind::EndOfReset:
    break;
  }
  return VhdlVhpiCallbackError::InvalidKind;
}

std::uint64_t VhdlVhpiCallbackSystem::make_identity(
    const std::uint32_t local) const noexcept {
  return (static_cast<std::uint64_t>(k_callback_identity) << 56U)
      | (static_cast<std::uint64_t>(system_identity_) << 32U)
      | local;
}

VhdlVhpiCallbackError VhdlVhpiCallbackSystem::validate_identity(
    const std::uint64_t identity) const noexcept {
  if (identity == 0U
      || static_cast<std::uint8_t>(identity >> 56U)
          != k_callback_identity
      || static_cast<std::uint32_t>(identity) == 0U) {
    return VhdlVhpiCallbackError::InvalidCallback;
  }
  const auto owner =
      static_cast<std::uint32_t>((identity >> 32U) & 0x00ffffffU);
  return owner == system_identity_ ? VhdlVhpiCallbackError::None
                                   : VhdlVhpiCallbackError::CrossSimulation;
}

VhdlVhpiCallbackRegistrationResult
VhdlVhpiCallbackSystem::register_callback(
    VhdlVhpiCallbackRegistration registration) {
  if (!valid()) {
    return {{}, VhdlVhpiCallbackError::InvalidSimulation};
  }
  if (!valid_kind(registration.kind)) {
    return {{}, VhdlVhpiCallbackError::InvalidKind};
  }
  if (!registration.callback) {
    return {{}, VhdlVhpiCallbackError::InvalidRequest};
  }
  const auto checked_object =
      validate_object(registration.kind, registration.object);
  if (checked_object != VhdlVhpiCallbackError::None) {
    return {{}, checked_object};
  }

  std::scoped_lock lock{mutex_};
  if (torn_down_) {
    return {{}, VhdlVhpiCallbackError::TornDown};
  }
  if (callbacks_.size() >= maximum_callbacks
      || next_callback_ == 0U) {
    return {{}, VhdlVhpiCallbackError::ResourceLimit};
  }
  const auto identity = make_identity(next_callback_++);
  VhdlVhpiCallbackDescriptor descriptor{
      identity,
      registration.kind,
      registration.object,
      next_ordinal_++,
      registration.repeat,
      VhdlVhpiCallbackStatus::Active,
      0,
      registration.user_data};
  try {
    callbacks_.emplace(
        identity,
        CallbackEntry{descriptor, std::move(registration.callback)});
  } catch (...) {
    return {{}, VhdlVhpiCallbackError::ResourceLimit};
  }
  return {descriptor, VhdlVhpiCallbackError::None};
}

VhdlVhpiCallbackStatusResult VhdlVhpiCallbackSystem::status(
    const std::uint64_t identity) const {
  const auto identity_error = validate_identity(identity);
  if (identity_error != VhdlVhpiCallbackError::None) {
    return {{}, identity_error};
  }
  std::scoped_lock lock{mutex_};
  const auto found = callbacks_.find(identity);
  if (found == callbacks_.end()) {
    return {{}, VhdlVhpiCallbackError::InvalidCallback};
  }
  return {found->second.descriptor, VhdlVhpiCallbackError::None};
}

VhdlVhpiCallbackError VhdlVhpiCallbackSystem::remove_callback(
    const std::uint64_t identity) {
  const auto identity_error = validate_identity(identity);
  if (identity_error != VhdlVhpiCallbackError::None) {
    return identity_error;
  }
  VhdlVhpiCallback callback;
  {
    std::scoped_lock lock{mutex_};
    const auto found = callbacks_.find(identity);
    if (found == callbacks_.end()) {
      return VhdlVhpiCallbackError::InvalidCallback;
    }
    if (found->second.descriptor.status
        != VhdlVhpiCallbackStatus::Active) {
      return VhdlVhpiCallbackError::NotActive;
    }
    found->second.descriptor.status =
        VhdlVhpiCallbackStatus::Removed;
    callback = std::move(found->second.callback);
  }
  callback = {};
  return VhdlVhpiCallbackError::None;
}

void VhdlVhpiCallbackSystem::invoke(
    const std::uint64_t identity,
    const VhdlVhpiCallbackKind kind,
    const VhdlVhpiEventData& data) {
  VhdlVhpiCallback callback;
  VhdlVhpiCallbackEvent event;
  {
    std::scoped_lock lock{mutex_};
    const auto found = callbacks_.find(identity);
    if (found == callbacks_.end()
        || found->second.descriptor.status
            != VhdlVhpiCallbackStatus::Active) {
      return;
    }
    try {
      callback = found->second.callback;
      event = VhdlVhpiCallbackEvent{
          kind,
          identity,
          objects_->simulation_identity(),
          found->second.descriptor.user_data,
          scheduler_->now(),
          scheduler_->delta(),
          data};
    } catch (...) {
      found->second.descriptor.status =
          VhdlVhpiCallbackStatus::CallbackFailed;
      return;
    }
    ++found->second.descriptor.invocations;
    if (!found->second.descriptor.repeat) {
      found->second.descriptor.status =
          VhdlVhpiCallbackStatus::Fired;
    }
  }

  bool failed{};
  try {
    callback(event);
  } catch (...) {
    failed = true;
  }
  if (!failed) {
    return;
  }
  std::scoped_lock lock{mutex_};
  const auto found = callbacks_.find(identity);
  if (found != callbacks_.end()
      && found->second.descriptor.status
          != VhdlVhpiCallbackStatus::Removed
      && found->second.descriptor.status
          != VhdlVhpiCallbackStatus::TornDown) {
    found->second.descriptor.status =
        VhdlVhpiCallbackStatus::CallbackFailed;
  }
}

VhdlVhpiCallbackError VhdlVhpiCallbackSystem::publish(
    const VhdlVhpiCallbackKind kind,
    const VhdlVhpiEventData& data) {
  if (!valid()) {
    return VhdlVhpiCallbackError::InvalidSimulation;
  }
  if (!valid_kind(kind)) {
    return VhdlVhpiCallbackError::InvalidKind;
  }
  if (!lifecycle_kind(kind) && data.object == 0U) {
    return VhdlVhpiCallbackError::InvalidRequest;
  }
  const auto checked_object = validate_object(kind, data.object);
  if (checked_object != VhdlVhpiCallbackError::None) {
    return checked_object;
  }
  if (lifecycle_kind(kind)
      && (data.related_identity != 0U || data.value
          || !data.message.empty() || data.source
          || data.severity != 0U)) {
    return VhdlVhpiCallbackError::InvalidRequest;
  }

  struct Pending {
    std::uint64_t identity{};
    std::uint64_t ordinal{};
  };
  std::vector<Pending> pending;
  VhdlVhpiEventData copied_data;
  {
    std::scoped_lock lock{mutex_};
    if (torn_down_) {
      return VhdlVhpiCallbackError::TornDown;
    }
    try {
      copied_data = data;
      pending.reserve(callbacks_.size());
      for (const auto& [identity, entry] : callbacks_) {
        if (entry.descriptor.status
                == VhdlVhpiCallbackStatus::Active
            && entry.descriptor.kind == kind
            && (entry.descriptor.object == 0U
                || entry.descriptor.object == data.object)) {
          pending.push_back({identity, entry.descriptor.ordinal});
        }
      }
    } catch (...) {
      return VhdlVhpiCallbackError::ResourceLimit;
    }
  }
  std::ranges::sort(
      pending,
      {},
      &Pending::ordinal);
  for (const auto& callback : pending) {
    invoke(callback.identity, kind, copied_data);
  }
  return VhdlVhpiCallbackError::None;
}

void VhdlVhpiCallbackSystem::teardown() noexcept {
  {
    std::scoped_lock lock{mutex_};
    if (torn_down_) {
      return;
    }
    torn_down_ = true;
  }
  while (true) {
    VhdlVhpiCallback callback;
    {
      std::scoped_lock lock{mutex_};
      auto selected = callbacks_.end();
      for (auto position = callbacks_.begin();
           position != callbacks_.end(); ++position) {
        if (!position->second.callback) {
          continue;
        }
        if (selected == callbacks_.end()
            || position->second.descriptor.ordinal
                < selected->second.descriptor.ordinal) {
          selected = position;
        }
      }
      if (selected == callbacks_.end()) {
        return;
      }
      if (selected->second.descriptor.status
          == VhdlVhpiCallbackStatus::Active) {
        selected->second.descriptor.status =
            VhdlVhpiCallbackStatus::TornDown;
      }
      callback = std::move(selected->second.callback);
    }
    callback = {};
  }
}

std::size_t VhdlVhpiCallbackSystem::registrations() const {
  std::scoped_lock lock{mutex_};
  return callbacks_.size();
}

}  // namespace fsim::runtime
