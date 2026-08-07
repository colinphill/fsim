// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_driver.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::uint8_t k_transaction_identity = 0xd3U;
constexpr std::uint8_t k_operation_identity = 0xd4U;

[[nodiscard]] bool write_owner_kind(
    const VhdlVhpiObjectKind kind) noexcept {
  return kind == VhdlVhpiObjectKind::Process
      || kind == VhdlVhpiObjectKind::Subprogram;
}

}  // namespace

void VhdlVhpiDriverSystem::refresh_visible_locked(
    const fsim_vhpi_handle_v1 signal) {
  auto& state = signals_.at(signal);
  if (state.forces.empty()) {
    state.value = state.resolved_value;
    return;
  }
  state.value =
      operations_.at(state.forces.back()).descriptor.value;
}

VhdlVhpiDriverError VhdlVhpiDriverSystem::cancel_transaction(
    const std::uint64_t transaction_identity,
    const fsim_vhpi_handle_v1 owner) {
  std::scoped_lock lock{mutex_};
  const auto identity_error =
      validate_identity(transaction_identity, k_transaction_identity);
  if (identity_error != VhdlVhpiDriverError::None) {
    return identity_error;
  }
  const auto owner_object = objects_->lookup_object(owner);
  if (!owner_object) {
    return object_error(owner_object.error);
  }
  if (!write_owner_kind(owner_object.value.kind)) {
    return VhdlVhpiDriverError::InvalidOwner;
  }
  for (auto& [driver_identity, driver] : drivers_) {
    static_cast<void>(driver_identity);
    const auto found = std::ranges::find(
        driver.transactions,
        transaction_identity,
        [](const TransactionEntry& entry) {
          return entry.descriptor.identity;
        });
    if (found == driver.transactions.end()) {
      continue;
    }
    if (driver.descriptor.source != owner) {
      return VhdlVhpiDriverError::InvalidOwner;
    }
    scheduler_->cancel(found->task);
    driver.transactions.erase(found);
    return VhdlVhpiDriverError::None;
  }
  return VhdlVhpiDriverError::InvalidTransaction;
}

VhdlVhpiWriteResult VhdlVhpiDriverSystem::prepare_write(
    const fsim_vhpi_handle_v1 signal,
    const fsim_vhpi_handle_v1 owner,
    const VhdlVhpiWriteKind kind,
    const std::uint64_t target_force,
    const PackedLogic9& value,
    const SimulationTick delay) {
  const auto signal_object = objects_->lookup_object(signal);
  if (!signal_object) {
    return {{}, object_error(signal_object.error)};
  }
  if (signal_object.value.kind != VhdlVhpiObjectKind::Signal) {
    return {{}, VhdlVhpiDriverError::InvalidSignal};
  }
  const auto owner_object = objects_->lookup_object(owner);
  if (!owner_object) {
    return {{}, object_error(owner_object.error)};
  }
  if (!write_owner_kind(owner_object.value.kind)) {
    return {{}, VhdlVhpiDriverError::InvalidOwner};
  }
  const auto found_signal = signals_.find(signal);
  if (found_signal == signals_.end()) {
    return {{}, VhdlVhpiDriverError::InvalidSignal};
  }
  if (kind != VhdlVhpiWriteKind::Release
      && value.width() != found_signal->second.width) {
    return {{}, VhdlVhpiDriverError::InvalidValue};
  }
  if (delay
      > std::numeric_limits<SimulationTick>::max() - scheduler_->now()) {
    return {{}, VhdlVhpiDriverError::InvalidWaveform};
  }
  if (kind == VhdlVhpiWriteKind::Release) {
    const auto target_error =
        validate_identity(target_force, k_operation_identity);
    if (target_error != VhdlVhpiDriverError::None) {
      return {{}, target_error};
    }
    const auto target = operations_.find(target_force);
    if (target == operations_.end()
        || target->second.descriptor.kind != VhdlVhpiWriteKind::Force
        || target->second.descriptor.signal != signal
        || target->second.descriptor.owner != owner
        || !target->second.descriptor.active) {
      return {{}, VhdlVhpiDriverError::InvalidOperation};
    }
  }
  if (next_operation_ == 0U) {
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }
  const auto identity =
      make_identity(k_operation_identity, next_operation_++);
  VhdlVhpiWriteDescriptor descriptor{
      identity,
      signal,
      owner,
      kind,
      target_force,
      value,
      scheduler_->now() + delay,
      delay != 0U,
      false,
      false,
      false};
  try {
    operations_.emplace(identity, WriteEntry{descriptor, {}});
  } catch (...) {
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }
  if (delay == 0U) {
    commit_write(identity);
  } else {
    try {
      operations_.at(identity).task =
          scheduler_->schedule_after_cancelable(
              delay,
              SchedulerPhase::update,
              next_ordinal_++,
              [this, identity](Scheduler&) {
                std::scoped_lock lock{mutex_};
                commit_write(identity);
              });
    } catch (...) {
      operations_.erase(identity);
      return {{}, VhdlVhpiDriverError::ResourceLimit};
    }
  }
  return {operations_.at(identity).descriptor, VhdlVhpiDriverError::None};
}

VhdlVhpiWriteResult VhdlVhpiDriverSystem::deposit(
    const fsim_vhpi_handle_v1 signal,
    const fsim_vhpi_handle_v1 owner,
    const PackedLogic9& value,
    const SimulationTick delay) {
  std::scoped_lock lock{mutex_};
  return prepare_write(
      signal, owner, VhdlVhpiWriteKind::Deposit, 0U, value, delay);
}

VhdlVhpiWriteResult VhdlVhpiDriverSystem::force(
    const fsim_vhpi_handle_v1 signal,
    const fsim_vhpi_handle_v1 owner,
    const PackedLogic9& value,
    const SimulationTick delay) {
  std::scoped_lock lock{mutex_};
  return prepare_write(
      signal, owner, VhdlVhpiWriteKind::Force, 0U, value, delay);
}

VhdlVhpiWriteResult VhdlVhpiDriverSystem::release(
    const std::uint64_t force_identity,
    const fsim_vhpi_handle_v1 owner,
    const SimulationTick delay) {
  std::scoped_lock lock{mutex_};
  const auto identity_error =
      validate_identity(force_identity, k_operation_identity);
  if (identity_error != VhdlVhpiDriverError::None) {
    return {{}, identity_error};
  }
  const auto target = operations_.find(force_identity);
  if (target == operations_.end()
      || target->second.descriptor.kind != VhdlVhpiWriteKind::Force) {
    return {{}, VhdlVhpiDriverError::InvalidOperation};
  }
  return prepare_write(
      target->second.descriptor.signal,
      owner,
      VhdlVhpiWriteKind::Release,
      force_identity,
      PackedLogic9{},
      delay);
}

void VhdlVhpiDriverSystem::commit_write(
    const std::uint64_t identity) {
  auto found = operations_.find(identity);
  if (found == operations_.end()
      || found->second.descriptor.canceled
      || found->second.descriptor.completed) {
    return;
  }
  auto& descriptor = found->second.descriptor;
  auto signal = signals_.find(descriptor.signal);
  if (signal == signals_.end()) {
    descriptor.pending = false;
    descriptor.completed = true;
    return;
  }
  switch (descriptor.kind) {
  case VhdlVhpiWriteKind::Deposit:
    signal->second.resolved_value = descriptor.value;
    break;
  case VhdlVhpiWriteKind::Force:
    signal->second.forces.push_back(identity);
    descriptor.active = true;
    break;
  case VhdlVhpiWriteKind::Release: {
    const auto force = operations_.find(descriptor.target_force);
    if (force != operations_.end()
        && force->second.descriptor.active) {
      const auto layer = std::ranges::find(
          signal->second.forces, descriptor.target_force);
      if (layer != signal->second.forces.end()) {
        signal->second.forces.erase(layer);
      }
      force->second.descriptor.active = false;
      descriptor.active = true;
    }
    break;
  }
  }
  descriptor.pending = false;
  descriptor.completed = true;
  refresh_visible_locked(descriptor.signal);
}

VhdlVhpiWriteResult VhdlVhpiDriverSystem::operation(
    const std::uint64_t identity) const {
  std::scoped_lock lock{mutex_};
  const auto identity_error =
      validate_identity(identity, k_operation_identity);
  if (identity_error != VhdlVhpiDriverError::None) {
    return {{}, identity_error};
  }
  const auto found = operations_.find(identity);
  if (found == operations_.end()) {
    return {{}, VhdlVhpiDriverError::InvalidOperation};
  }
  return {found->second.descriptor, VhdlVhpiDriverError::None};
}

VhdlVhpiDriverError VhdlVhpiDriverSystem::cancel_operation(
    const std::uint64_t identity,
    const fsim_vhpi_handle_v1 owner) {
  std::scoped_lock lock{mutex_};
  const auto identity_error =
      validate_identity(identity, k_operation_identity);
  if (identity_error != VhdlVhpiDriverError::None) {
    return identity_error;
  }
  const auto found = operations_.find(identity);
  if (found == operations_.end()) {
    return VhdlVhpiDriverError::InvalidOperation;
  }
  const auto owner_object = objects_->lookup_object(owner);
  if (!owner_object) {
    return object_error(owner_object.error);
  }
  if (found->second.descriptor.owner != owner) {
    return VhdlVhpiDriverError::InvalidOwner;
  }
  if (!found->second.descriptor.pending) {
    return VhdlVhpiDriverError::AlreadyCompleted;
  }
  scheduler_->cancel(found->second.task);
  found->second.descriptor.pending = false;
  found->second.descriptor.canceled = true;
  return VhdlVhpiDriverError::None;
}

}  // namespace fsim::runtime
