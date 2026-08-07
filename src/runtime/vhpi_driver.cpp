// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vhpi_driver.hpp"

#include "fsim/runtime/logic.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <ranges>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::uint8_t k_driver_identity = 0xd1U;
constexpr std::uint8_t k_source_identity = 0xd2U;
constexpr std::uint8_t k_transaction_identity = 0xd3U;
constexpr std::size_t k_max_width = 1U << 20U;
constexpr std::size_t k_max_waveform_elements = 1U << 16U;
std::atomic<std::uint32_t> next_driver_system{1U};

[[nodiscard]] bool source_kind(const VhdlVhpiObjectKind kind) noexcept {
  return kind == VhdlVhpiObjectKind::Process
      || kind == VhdlVhpiObjectKind::Subprogram;
}

[[nodiscard]] bool valid_mode(const VhdlVhpiDelayMode mode) noexcept {
  return static_cast<std::uint32_t>(mode)
      <= static_cast<std::uint32_t>(VhdlVhpiDelayMode::Transport);
}

}  // namespace

VhdlVhpiDriverSystem::VhdlVhpiDriverSystem(
    VhdlVhpiObjectRegistry& objects,
    VhdlVhpiTypeSystem& types,
    Scheduler& scheduler) noexcept
    : objects_(&objects), types_(&types), scheduler_(&scheduler),
      system_identity_(
          next_driver_system.fetch_add(1U, std::memory_order_relaxed)
          & 0x00ffffffU) {
  if (system_identity_ == 0U) {
    system_identity_ =
        next_driver_system.fetch_add(1U, std::memory_order_relaxed)
        & 0x00ffffffU;
  }
}

VhdlVhpiDriverSystem::~VhdlVhpiDriverSystem() {
  std::scoped_lock lock{mutex_};
  for (auto& [identity, driver] : drivers_) {
    static_cast<void>(identity);
    for (const auto& transaction : driver.transactions) {
      scheduler_->cancel(transaction.task);
    }
  }
  for (const auto& [identity, operation] : operations_) {
    static_cast<void>(identity);
    if (operation.descriptor.pending) {
      scheduler_->cancel(operation.task);
    }
  }
}

VhdlVhpiDriverError VhdlVhpiDriverSystem::object_error(
    const VhdlVhpiObjectError error) noexcept {
  switch (error) {
  case VhdlVhpiObjectError::None:
    return VhdlVhpiDriverError::None;
  case VhdlVhpiObjectError::InvalidSimulation:
    return VhdlVhpiDriverError::InvalidSimulation;
  case VhdlVhpiObjectError::CrossSimulation:
    return VhdlVhpiDriverError::CrossSimulation;
  case VhdlVhpiObjectError::StaleHandle:
    return VhdlVhpiDriverError::StaleHandle;
  case VhdlVhpiObjectError::ReleasedHandle:
    return VhdlVhpiDriverError::ReleasedHandle;
  case VhdlVhpiObjectError::ResourceLimit:
    return VhdlVhpiDriverError::ResourceLimit;
  default:
    return VhdlVhpiDriverError::InvalidHandle;
  }
}

std::uint64_t VhdlVhpiDriverSystem::make_identity(
    const std::uint8_t kind, const std::uint32_t local) const noexcept {
  return (static_cast<std::uint64_t>(kind) << 56U)
      | (static_cast<std::uint64_t>(system_identity_) << 32U)
      | local;
}

VhdlVhpiDriverError VhdlVhpiDriverSystem::validate_identity(
    const std::uint64_t identity, const std::uint8_t kind) const noexcept {
  if (identity == 0U || static_cast<std::uint8_t>(identity >> 56U) != kind
      || static_cast<std::uint32_t>(identity) == 0U) {
    return kind == k_transaction_identity
        ? VhdlVhpiDriverError::InvalidTransaction
        : VhdlVhpiDriverError::InvalidDriver;
  }
  const auto owner =
      static_cast<std::uint32_t>((identity >> 32U) & 0x00ffffffU);
  return owner == system_identity_ ? VhdlVhpiDriverError::None
                                   : VhdlVhpiDriverError::CrossSimulation;
}

VhdlVhpiDriverDescriptorResult VhdlVhpiDriverSystem::create_driver(
    const fsim_vhpi_handle_v1 signal,
    const fsim_vhpi_handle_v1 source,
    const PackedLogic9& initial) {
  std::scoped_lock lock{mutex_};
  if (initial.empty() || initial.width() > k_max_width) {
    return {{}, VhdlVhpiDriverError::InvalidValue};
  }
  const auto signal_object = objects_->lookup_object(signal);
  if (!signal_object) {
    return {{}, object_error(signal_object.error)};
  }
  if (signal_object.value.kind != VhdlVhpiObjectKind::Signal) {
    return {{}, VhdlVhpiDriverError::InvalidSignal};
  }
  const auto source_object = objects_->lookup_object(source);
  if (!source_object) {
    return {{}, object_error(source_object.error)};
  }
  if (!source_kind(source_object.value.kind)) {
    return {{}, VhdlVhpiDriverError::InvalidSource};
  }
  const auto declaration_type = types_->declaration_type(signal);
  if (!declaration_type
      || declaration_type.descriptor.scalar_kind
          != VhdlVhpiScalarKind::Logic9) {
    return {{}, VhdlVhpiDriverError::InvalidType};
  }

  auto found_signal = signals_.find(signal);
  if (found_signal != signals_.end()) {
    if (found_signal->second.width != initial.width()) {
      return {{}, VhdlVhpiDriverError::InvalidValue};
    }
    if (!found_signal->second.resolved) {
      return {{}, VhdlVhpiDriverError::MultipleDrivers};
    }
    for (const auto identity : found_signal->second.drivers) {
      if (drivers_.at(identity).descriptor.source == source) {
        return {{}, VhdlVhpiDriverError::InvalidSource};
      }
    }
  }
  if (next_driver_ == 0U || next_source_ == 0U) {
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }
  const auto identity = make_identity(k_driver_identity, next_driver_++);
  const auto source_identity =
      make_identity(k_source_identity, next_source_++);
  VhdlVhpiDriverDescriptor descriptor{
      identity,
      source_identity,
      signal,
      source,
      next_ordinal_++,
      initial};
  try {
    drivers_.emplace(identity, DriverEntry{descriptor, {}});
    if (found_signal == signals_.end()) {
      SignalEntry entry{
          declaration_type.type,
          declaration_type.descriptor.resolved,
          initial.width(),
          initial,
          initial,
          {identity},
          {}};
      signals_.emplace(signal, std::move(entry));
    } else {
      found_signal->second.drivers.push_back(identity);
      resolve_signal_locked(signal);
    }
  } catch (...) {
    drivers_.erase(identity);
    if (found_signal != signals_.end()
        && !found_signal->second.drivers.empty()
        && found_signal->second.drivers.back() == identity) {
      found_signal->second.drivers.pop_back();
    }
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }
  return {std::move(descriptor), VhdlVhpiDriverError::None};
}

void VhdlVhpiDriverSystem::resolve_signal_locked(
    const fsim_vhpi_handle_v1 signal) {
  auto& state = signals_.at(signal);
  if (!state.resolved || state.drivers.size() == 1U) {
    state.resolved_value =
        drivers_.at(state.drivers.front()).descriptor.value;
    refresh_visible_locked(signal);
    return;
  }
  PackedLogic9 result{state.width, Logic9::z};
  std::vector<Logic9> values;
  values.reserve(state.drivers.size());
  for (std::size_t bit = 0; bit < state.width; ++bit) {
    values.clear();
    for (const auto identity : state.drivers) {
      values.push_back(drivers_.at(identity).descriptor.value.get(bit));
    }
    result.set(bit, resolve(values));
  }
  state.resolved_value = std::move(result);
  refresh_visible_locked(signal);
}

VhdlVhpiDriverDescriptorResult VhdlVhpiDriverSystem::driver(
    const std::uint64_t identity) const {
  std::scoped_lock lock{mutex_};
  const auto error = validate_identity(identity, k_driver_identity);
  if (error != VhdlVhpiDriverError::None) {
    return {{}, error};
  }
  const auto found = drivers_.find(identity);
  if (found == drivers_.end()) {
    return {{}, VhdlVhpiDriverError::InvalidDriver};
  }
  const auto signal = objects_->lookup_object(found->second.descriptor.signal);
  if (!signal) {
    return {{}, object_error(signal.error)};
  }
  const auto source = objects_->lookup_object(found->second.descriptor.source);
  if (!source) {
    return {{}, object_error(source.error)};
  }
  return {found->second.descriptor, VhdlVhpiDriverError::None};
}

VhdlVhpiDriverLogicResult VhdlVhpiDriverSystem::signal_value(
    const fsim_vhpi_handle_v1 signal) const {
  std::scoped_lock lock{mutex_};
  const auto object = objects_->lookup_object(signal);
  if (!object) {
    return {PackedLogic9{}, object_error(object.error)};
  }
  const auto found = signals_.find(signal);
  if (found == signals_.end()) {
    return {PackedLogic9{}, VhdlVhpiDriverError::InvalidSignal};
  }
  return {found->second.value, VhdlVhpiDriverError::None};
}

VhdlVhpiDriverListResult VhdlVhpiDriverSystem::drivers(
    const fsim_vhpi_handle_v1 signal) const {
  std::scoped_lock lock{mutex_};
  const auto object = objects_->lookup_object(signal);
  if (!object) {
    return {{}, object_error(object.error)};
  }
  const auto found = signals_.find(signal);
  if (found == signals_.end()) {
    return {{}, VhdlVhpiDriverError::InvalidSignal};
  }
  std::vector<VhdlVhpiDriverDescriptor> result;
  try {
    result.reserve(found->second.drivers.size());
    for (const auto identity : found->second.drivers) {
      result.push_back(drivers_.at(identity).descriptor);
    }
  } catch (...) {
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }
  return {std::move(result), VhdlVhpiDriverError::None};
}

VhdlVhpiSourceListResult VhdlVhpiDriverSystem::sources(
    const fsim_vhpi_handle_v1 signal) const {
  const auto driver_list = drivers(signal);
  if (!driver_list) {
    return {{}, driver_list.error};
  }
  std::vector<VhdlVhpiSourceDescriptor> result;
  try {
    result.reserve(driver_list.value.size());
    for (const auto& entry : driver_list.value) {
      result.push_back(VhdlVhpiSourceDescriptor{
          entry.source_identity,
          entry.identity,
          entry.signal,
          entry.source,
          entry.ordinal});
    }
  } catch (...) {
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }
  return {std::move(result), VhdlVhpiDriverError::None};
}

VhdlVhpiTransactionListResult VhdlVhpiDriverSystem::transactions(
    const std::uint64_t driver_identity) const {
  std::scoped_lock lock{mutex_};
  const auto error =
      validate_identity(driver_identity, k_driver_identity);
  if (error != VhdlVhpiDriverError::None) {
    return {{}, error};
  }
  const auto found = drivers_.find(driver_identity);
  if (found == drivers_.end()) {
    return {{}, VhdlVhpiDriverError::InvalidDriver};
  }
  std::vector<VhdlVhpiTransactionDescriptor> result;
  try {
    result.reserve(found->second.transactions.size());
    for (const auto& transaction : found->second.transactions) {
      result.push_back(transaction.descriptor);
    }
  } catch (...) {
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }
  return {std::move(result), VhdlVhpiDriverError::None};
}

VhdlVhpiTransactionListResult VhdlVhpiDriverSystem::schedule_waveform(
    const std::uint64_t driver_identity,
    const std::span<const VhdlVhpiWaveformElement> elements,
    const SimulationTick rejection,
    const VhdlVhpiDelayMode mode) {
  std::scoped_lock lock{mutex_};
  const auto identity_error =
      validate_identity(driver_identity, k_driver_identity);
  if (identity_error != VhdlVhpiDriverError::None) {
    return {{}, identity_error};
  }
  const auto found = drivers_.find(driver_identity);
  if (found == drivers_.end()) {
    return {{}, VhdlVhpiDriverError::InvalidDriver};
  }
  if (!valid_mode(mode) || elements.empty()
      || elements.size() > k_max_waveform_elements) {
    return {{}, VhdlVhpiDriverError::InvalidWaveform};
  }
  if (mode == VhdlVhpiDelayMode::Inertial
      && rejection > elements.front().delay) {
    return {{}, VhdlVhpiDriverError::InvalidRejection};
  }
  auto previous = elements.front().delay;
  for (std::size_t index = 0; index < elements.size(); ++index) {
    if (elements[index].value.width()
            != found->second.descriptor.value.width()
        || (index != 0U && elements[index].delay <= previous)
        || elements[index].delay
            > std::numeric_limits<SimulationTick>::max()
                - scheduler_->now()) {
      return {{}, VhdlVhpiDriverError::InvalidWaveform};
    }
    previous = elements[index].delay;
  }
  if (next_transaction_ == 0U
      || elements.size()
          > static_cast<std::size_t>(
              std::numeric_limits<std::uint32_t>::max()
              - next_transaction_)) {
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }

  auto& pending = found->second.transactions;
  const auto first_time = scheduler_->now() + elements.front().delay;
  const auto first_deleted = std::lower_bound(
      pending.begin(),
      pending.end(),
      first_time,
      [](const TransactionEntry& transaction,
         const SimulationTick time) {
        return transaction.descriptor.time < time;
      });
  for (auto transaction = first_deleted;
       transaction != pending.end();
       ++transaction) {
    scheduler_->cancel(transaction->task);
  }
  pending.erase(first_deleted, pending.end());

  const auto old_count = pending.size();
  std::vector<std::uint64_t> new_ids;
  try {
    new_ids.reserve(elements.size());
    pending.reserve(old_count + elements.size());
    for (std::size_t index = 0; index < elements.size(); ++index) {
      const auto identity =
          make_identity(k_transaction_identity, next_transaction_++);
      new_ids.push_back(identity);
      pending.push_back(TransactionEntry{
          VhdlVhpiTransactionDescriptor{
              identity,
              driver_identity,
              found->second.descriptor.signal,
              elements[index].value,
              scheduler_->now() + elements[index].delay,
              rejection,
              mode,
              static_cast<std::uint32_t>(index)},
          {}});
    }
  } catch (...) {
    pending.resize(old_count);
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }

  if (mode == VhdlVhpiDelayMode::Inertial && old_count != 0U) {
    std::vector<bool> retained(pending.size(), false);
    for (std::size_t index = old_count; index < pending.size(); ++index) {
      retained[index] = true;
    }
    const auto threshold = first_time - rejection;
    for (std::size_t index = 0; index < old_count; ++index) {
      retained[index] = pending[index].descriptor.time < threshold;
    }
    for (std::size_t index = pending.size() - 1U; index-- > 0U;) {
      if (!retained[index] && retained[index + 1U]
          && pending[index].descriptor.value
              == pending[index + 1U].descriptor.value) {
        retained[index] = true;
      }
    }
    for (std::size_t index = old_count; index-- > 0U;) {
      if (!retained[index]) {
        scheduler_->cancel(pending[index].task);
        pending.erase(
            pending.begin() + static_cast<std::ptrdiff_t>(index));
      }
    }
  }

  for (std::size_t index = 0; index < new_ids.size(); ++index) {
    const auto identity = new_ids[index];
    const auto transaction = std::ranges::find(
        pending, identity,
        [](const TransactionEntry& entry) {
          return entry.descriptor.identity;
        });
    if (transaction == pending.end()) {
      return {{}, VhdlVhpiDriverError::ResourceLimit};
    }
    try {
      transaction->task = scheduler_->schedule_after_cancelable(
          elements[index].delay,
          SchedulerPhase::update,
          found->second.descriptor.ordinal,
          [this, driver_identity, identity](Scheduler&) {
            commit_transaction(driver_identity, identity);
          });
    } catch (...) {
      pending.erase(transaction);
      return {{}, VhdlVhpiDriverError::ResourceLimit};
    }
  }

  std::vector<VhdlVhpiTransactionDescriptor> result;
  try {
    result.reserve(new_ids.size());
    for (const auto identity : new_ids) {
      const auto transaction = std::ranges::find(
          pending, identity,
          [](const TransactionEntry& entry) {
            return entry.descriptor.identity;
          });
      if (transaction != pending.end()) {
        result.push_back(transaction->descriptor);
      }
    }
  } catch (...) {
    return {{}, VhdlVhpiDriverError::ResourceLimit};
  }
  return {std::move(result), VhdlVhpiDriverError::None};
}

void VhdlVhpiDriverSystem::commit_transaction(
    const std::uint64_t driver_identity,
    const std::uint64_t transaction_identity) {
  std::scoped_lock lock{mutex_};
  const auto found = drivers_.find(driver_identity);
  if (found == drivers_.end()) {
    return;
  }
  auto& pending = found->second.transactions;
  const auto transaction = std::ranges::find(
      pending, transaction_identity,
      [](const TransactionEntry& entry) {
        return entry.descriptor.identity;
      });
  if (transaction == pending.end()) {
    return;
  }
  found->second.descriptor.value = transaction->descriptor.value;
  const auto signal = found->second.descriptor.signal;
  pending.erase(transaction);
  resolve_signal_locked(signal);
}

}  // namespace fsim::runtime
