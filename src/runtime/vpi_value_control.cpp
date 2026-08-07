// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_value_control.hpp"

#include <atomic>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::runtime {

namespace {

constexpr std::size_t maximum_scheduled_writes = 65'536;
std::atomic<std::uint64_t> next_write_control_owner{1};

bool valid_write_kind(const SystemVerilogVpiWriteKind kind) {
  return static_cast<unsigned>(kind)
      <= static_cast<unsigned>(SystemVerilogVpiWriteKind::Release);
}

bool valid_delay_policy(const SystemVerilogVpiDelayPolicy policy) {
  return static_cast<unsigned>(policy)
      <= static_cast<unsigned>(SystemVerilogVpiDelayPolicy::Transport);
}

SystemVerilogVpiValueError apply_write(
    SystemVerilogVpiObjectRegistry& registry,
    const fsim_vpi_handle_v1 object,
    const SystemVerilogVpiWriteKind kind,
    const SystemVerilogVpiStoredValue* const value) {
  switch (kind) {
    case SystemVerilogVpiWriteKind::Deposit:
      return value
          ? registry.deposit_value(object, *value)
          : SystemVerilogVpiValueError::UnsupportedFormat;
    case SystemVerilogVpiWriteKind::Force:
      return value
          ? registry.force_value(object, *value)
          : SystemVerilogVpiValueError::UnsupportedFormat;
    case SystemVerilogVpiWriteKind::Release:
      return registry.release_forced_value(object);
  }
  return SystemVerilogVpiValueError::UnsupportedFormat;
}

SystemVerilogVpiScheduleWriteResult schedule_failure(
    const SystemVerilogVpiWriteControlError error,
    const SystemVerilogVpiValueError value_error =
        SystemVerilogVpiValueError::None) {
  SystemVerilogVpiScheduleWriteResult result;
  result.error = error;
  result.value_error = value_error;
  return result;
}

SystemVerilogVpiScheduledWriteResult status_failure(
    const SystemVerilogVpiWriteControlError error) {
  SystemVerilogVpiScheduledWriteResult result;
  result.error = error;
  return result;
}

}  // namespace

struct SystemVerilogVpiValueControl::Impl {
  struct Record {
    fsim_vpi_handle_v1 object{};
    SystemVerilogVpiWriteKind kind{
        SystemVerilogVpiWriteKind::Deposit};
    std::optional<SystemVerilogVpiStoredValue> value;
    SystemVerilogVpiScheduledWriteStatus status{
        SystemVerilogVpiScheduledWriteStatus::Pending};
    SystemVerilogVpiValueError value_error{
        SystemVerilogVpiValueError::None};
    ScheduledTaskHandle scheduler_handle;
  };

  std::uint64_t owner{};
  std::uint64_t next_id{1};
  StableOrder stable_order_base{};
  SystemVerilogVpiObjectRegistry* registry{};
  Scheduler* scheduler{};
  mutable std::mutex mutex;
  std::unordered_map<std::uint64_t, Record> records;
};

SystemVerilogVpiValueControl::SystemVerilogVpiValueControl(
    SystemVerilogVpiObjectRegistry& registry,
    Scheduler& scheduler,
    const StableOrder stable_order_base)
    : impl_(std::make_shared<Impl>()) {
  impl_->owner =
      next_write_control_owner.fetch_add(1, std::memory_order_relaxed);
  impl_->stable_order_base = stable_order_base;
  impl_->registry = &registry;
  impl_->scheduler = &scheduler;
}

SystemVerilogVpiValueControl::~SystemVerilogVpiValueControl() {
  if (!impl_) {
    return;
  }
  std::scoped_lock lock{impl_->mutex};
  for (auto& [id, record] : impl_->records) {
    (void)id;
    if (record.status == SystemVerilogVpiScheduledWriteStatus::Pending) {
      impl_->scheduler->cancel(record.scheduler_handle);
      record.status = SystemVerilogVpiScheduledWriteStatus::Cancelled;
    }
  }
}

SystemVerilogVpiValueError SystemVerilogVpiValueControl::apply(
    const fsim_vpi_handle_v1 object,
    const SystemVerilogVpiWriteKind kind,
    std::optional<SystemVerilogVpiStoredValue> value) {
  if (!impl_ || !valid_write_kind(kind)
      || ((kind == SystemVerilogVpiWriteKind::Release) == value.has_value())) {
    return SystemVerilogVpiValueError::UnsupportedFormat;
  }
  return apply_write(
      *impl_->registry, object, kind, value ? &*value : nullptr);
}

SystemVerilogVpiScheduleWriteResult
SystemVerilogVpiValueControl::schedule(
    const fsim_vpi_handle_v1 object,
    const SystemVerilogVpiWriteKind kind,
    std::optional<SystemVerilogVpiStoredValue> value,
    const SimulationTick delay,
    const SystemVerilogVpiDelayPolicy policy) {
  if (!impl_) {
    return schedule_failure(
        SystemVerilogVpiWriteControlError::InvalidControl);
  }
  if (!valid_write_kind(kind) || !valid_delay_policy(policy)
      || ((kind == SystemVerilogVpiWriteKind::Release)
          == value.has_value())) {
    return schedule_failure(
        SystemVerilogVpiWriteControlError::InvalidRequest);
  }
  const auto preflight = impl_->registry->validate_value_write(
      object, value ? &*value : nullptr);
  if (preflight != SystemVerilogVpiValueError::None) {
    return schedule_failure(
        SystemVerilogVpiWriteControlError::None, preflight);
  }
  if (delay
      > std::numeric_limits<SimulationTick>::max()
          - impl_->scheduler->now()) {
    return schedule_failure(
        SystemVerilogVpiWriteControlError::ResourceLimit);
  }

  std::scoped_lock lock{impl_->mutex};
  if (impl_->records.size() >= maximum_scheduled_writes
      || impl_->next_id == 0U
      || impl_->next_id
          > std::numeric_limits<StableOrder>::max()
              - impl_->stable_order_base) {
    return schedule_failure(
        SystemVerilogVpiWriteControlError::ResourceLimit);
  }

  std::vector<std::uint64_t> superseded;
  if (policy == SystemVerilogVpiDelayPolicy::Inertial) {
    for (const auto& [id, record] : impl_->records) {
      if (record.status == SystemVerilogVpiScheduledWriteStatus::Pending
          && record.object == object) {
        superseded.push_back(id);
      }
    }
  }

  const auto id = impl_->next_id++;
  auto [position, inserted] = impl_->records.emplace(
      id,
      Impl::Record{
          object,
          kind,
          std::move(value),
          SystemVerilogVpiScheduledWriteStatus::Pending,
          SystemVerilogVpiValueError::None,
          {},
      });
  if (!inserted) {
    return schedule_failure(
        SystemVerilogVpiWriteControlError::ResourceLimit);
  }

  const std::weak_ptr<Impl> weak = impl_;
  try {
    position->second.scheduler_handle =
        impl_->scheduler->schedule_after_cancelable(
            delay,
            SchedulerPhase::update,
            impl_->stable_order_base + id,
            [weak, id](Scheduler&) {
              const auto state = weak.lock();
              if (!state) {
                return;
              }
              fsim_vpi_handle_v1 scheduled_object{};
              SystemVerilogVpiWriteKind scheduled_kind{
                  SystemVerilogVpiWriteKind::Deposit};
              SystemVerilogVpiStoredValue scheduled_value;
              bool scheduled_has_value = false;
              {
                std::scoped_lock callback_lock{state->mutex};
                const auto found = state->records.find(id);
                if (found == state->records.end()
                    || found->second.status
                        != SystemVerilogVpiScheduledWriteStatus::Pending) {
                  return;
                }
                scheduled_object = found->second.object;
                scheduled_kind = found->second.kind;
                if (found->second.value) {
                  scheduled_value = *found->second.value;
                  scheduled_has_value = true;
                }
              }

              const auto write_error = apply_write(
                  *state->registry,
                  scheduled_object,
                  scheduled_kind,
                  scheduled_has_value ? &scheduled_value : nullptr);
              std::scoped_lock callback_lock{state->mutex};
              const auto found = state->records.find(id);
              if (found == state->records.end()
                  || found->second.status
                      != SystemVerilogVpiScheduledWriteStatus::Pending) {
                return;
              }
              found->second.value_error = write_error;
              found->second.status =
                  write_error == SystemVerilogVpiValueError::None
                  ? SystemVerilogVpiScheduledWriteStatus::Applied
                  : SystemVerilogVpiScheduledWriteStatus::Failed;
            });
  } catch (...) {
    impl_->records.erase(position);
    return schedule_failure(
        SystemVerilogVpiWriteControlError::ResourceLimit);
  }

  for (const auto old_id : superseded) {
    const auto old = impl_->records.find(old_id);
    if (old == impl_->records.end()
        || old->second.status
            != SystemVerilogVpiScheduledWriteStatus::Pending) {
      continue;
    }
    impl_->scheduler->cancel(old->second.scheduler_handle);
    old->second.status =
        SystemVerilogVpiScheduledWriteStatus::Cancelled;
  }

  SystemVerilogVpiScheduleWriteResult result;
  result.value = {impl_->owner, id};
  return result;
}

SystemVerilogVpiWriteControlError SystemVerilogVpiValueControl::cancel(
    const SystemVerilogVpiScheduledWriteHandle handle) {
  if (!impl_) {
    return SystemVerilogVpiWriteControlError::InvalidControl;
  }
  if (!handle) {
    return SystemVerilogVpiWriteControlError::InvalidHandle;
  }
  if (handle.owner != impl_->owner) {
    return SystemVerilogVpiWriteControlError::CrossControl;
  }
  std::scoped_lock lock{impl_->mutex};
  const auto found = impl_->records.find(handle.id);
  if (found == impl_->records.end()) {
    return SystemVerilogVpiWriteControlError::NotFound;
  }
  if (found->second.status
      != SystemVerilogVpiScheduledWriteStatus::Pending) {
    return SystemVerilogVpiWriteControlError::NotPending;
  }
  impl_->scheduler->cancel(found->second.scheduler_handle);
  found->second.status =
      SystemVerilogVpiScheduledWriteStatus::Cancelled;
  return SystemVerilogVpiWriteControlError::None;
}

SystemVerilogVpiScheduledWriteResult
SystemVerilogVpiValueControl::status(
    const SystemVerilogVpiScheduledWriteHandle handle) const {
  if (!impl_) {
    return status_failure(
        SystemVerilogVpiWriteControlError::InvalidControl);
  }
  if (!handle) {
    return status_failure(
        SystemVerilogVpiWriteControlError::InvalidHandle);
  }
  if (handle.owner != impl_->owner) {
    return status_failure(
        SystemVerilogVpiWriteControlError::CrossControl);
  }
  std::scoped_lock lock{impl_->mutex};
  const auto found = impl_->records.find(handle.id);
  if (found == impl_->records.end()) {
    return status_failure(
        SystemVerilogVpiWriteControlError::NotFound);
  }
  SystemVerilogVpiScheduledWriteResult result;
  result.status = found->second.status;
  result.value_error = found->second.value_error;
  return result;
}

SystemVerilogVpiWriteControlError SystemVerilogVpiValueControl::release(
    const SystemVerilogVpiScheduledWriteHandle handle) {
  if (!impl_) {
    return SystemVerilogVpiWriteControlError::InvalidControl;
  }
  if (!handle) {
    return SystemVerilogVpiWriteControlError::InvalidHandle;
  }
  if (handle.owner != impl_->owner) {
    return SystemVerilogVpiWriteControlError::CrossControl;
  }
  std::scoped_lock lock{impl_->mutex};
  const auto found = impl_->records.find(handle.id);
  if (found == impl_->records.end()) {
    return SystemVerilogVpiWriteControlError::NotFound;
  }
  if (found->second.status
      == SystemVerilogVpiScheduledWriteStatus::Pending) {
    return SystemVerilogVpiWriteControlError::NotPending;
  }
  impl_->records.erase(found);
  return SystemVerilogVpiWriteControlError::None;
}

std::size_t SystemVerilogVpiValueControl::pending() const noexcept {
  if (!impl_) {
    return 0;
  }
  std::scoped_lock lock{impl_->mutex};
  std::size_t result{};
  for (const auto& [id, record] : impl_->records) {
    (void)id;
    result += record.status
        == SystemVerilogVpiScheduledWriteStatus::Pending;
  }
  return result;
}

}  // namespace fsim::runtime
