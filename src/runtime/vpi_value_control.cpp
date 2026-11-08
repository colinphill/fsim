// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/vpi_value_control.hpp"

#include "fsim/runtime/scheduler_events.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <limits>
#include <mutex>
#include <string_view>
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
      <= static_cast<unsigned>(
          SystemVerilogVpiDelayPolicy::ModifiedTransport);
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
    SimulationTick due{};
    // False when nobody holds a handle: the record goes once the write
    // is no longer pending, so applied writes do not accumulate.
    bool retained{true};
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

// The scheduler event of a pending delayed write (#128). The payload names the
// control and the record; the record (object, value, kind) is saved with the
// foreign state, so the event is plain data.
struct SystemVerilogVpiValueControlEvents {
  using Impl = SystemVerilogVpiValueControl::Impl;
  struct Payload {
    Impl* owner{};
    std::uint64_t id{};
  };
  struct Domain {
    static constexpr std::string_view name{"vpi_value_control"};
  };
  struct Codec
      : OwnedFieldsSchedulerEventCodec<Payload, Impl, Domain, &Payload::owner,
                                       &Payload::id> {
    static std::uint64_t cancel_key(const Payload& payload) {
      return payload.id;
    }
  };

  static Scheduler::Task task(Impl* owner, const std::uint64_t id) {
    return Scheduler::Task{
        detail::make_scheduler_task_descriptor<Payload, &dispatch>(
            Payload{owner, id})};
  }

  static void dispatch(Scheduler&, const Payload& scheduled) {
    Impl* const state = scheduled.owner;
    const auto id = scheduled.id;
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
    if (!found->second.retained) {
      state->records.erase(found);
      return;
    }
    found->second.value_error = write_error;
    found->second.status =
        write_error == SystemVerilogVpiValueError::None
        ? SystemVerilogVpiScheduledWriteStatus::Applied
        : SystemVerilogVpiScheduledWriteStatus::Failed;
  }

  static void register_kind() {
    static_cast<void>(register_scheduler_event_kind<Payload, &dispatch, Codec>(
        "vpi.scheduled_write", SchedulerEventClass::data));
  }
};

namespace {
[[maybe_unused]] const bool vpi_value_control_kinds_registered =
    (SystemVerilogVpiValueControlEvents::register_kind(), true);
}  // namespace

void SystemVerilogVpiValueControl::bind_event_owner(
    SchedulerEventOwners& owners) {
  owners.bind(SystemVerilogVpiValueControlEvents::Domain::name, impl_.get());
}

bool SystemVerilogVpiValueControl::bind_restored_write(
    const std::uint64_t id, const ScheduledTaskHandle& handle) {
  std::scoped_lock lock{impl_->mutex};
  const auto found = impl_->records.find(id);
  if (found == impl_->records.end()) {
    return false;
  }
  found->second.scheduler_handle = handle;
  return true;
}

namespace {

// The payload of a stored value as plain fields.
void encode_value(const SystemVerilogVpiStoredValue& value,
    SystemVerilogVpiSavedWrite& out) {
  struct Encoder {
    SystemVerilogVpiSavedWrite& out;
    void operator()(const std::monostate&) const { out.value_kind = 0U; }
    void operator()(const PackedBit2& value) const {
      out.value_kind = 1U;
      out.text = value.to_msb_string();
    }
    void operator()(const PackedLogic4& value) const {
      out.value_kind = 2U;
      out.text = value.to_msb_string();
    }
    void operator()(const PackedLogic9& value) const {
      out.value_kind = 3U;
      out.text = value.to_msb_string();
    }
    void operator()(const double value) const {
      out.value_kind = 4U;
      out.bits = std::bit_cast<std::uint64_t>(value);
    }
    void operator()(const float value) const {
      out.value_kind = 5U;
      out.bits = std::bit_cast<std::uint32_t>(value);
    }
    void operator()(const std::string& value) const {
      out.value_kind = 6U;
      out.text = value;
    }
    void operator()(const std::uint64_t value) const {
      out.value_kind = 7U;
      out.bits = value;
    }
  };
  std::visit(Encoder{out}, value.payload);
  out.has_strength = value.strength.has_value();
  if (value.strength) {
    out.strength_zero = static_cast<std::uint8_t>(value.strength->zero);
    out.strength_one = static_cast<std::uint8_t>(value.strength->one);
  }
}

std::optional<SystemVerilogVpiStoredValue> decode_value(
    const SystemVerilogVpiSavedWrite& in) {
  SystemVerilogVpiStoredValue value;
  switch (in.value_kind) {
    case 0U:
      return std::nullopt;
    case 1U:
      value.payload = PackedBit2::from_msb_string(in.text);
      break;
    case 2U:
      value.payload = PackedLogic4::from_msb_string(in.text);
      break;
    case 3U:
      value.payload = PackedLogic9::from_msb_string(in.text);
      break;
    case 4U:
      value.payload = std::bit_cast<double>(in.bits);
      break;
    case 5U:
      value.payload = std::bit_cast<float>(static_cast<std::uint32_t>(in.bits));
      break;
    case 6U:
      value.payload = in.text;
      break;
    default:
      value.payload = in.bits;
      break;
  }
  if (in.has_strength) {
    value.strength = SystemVerilogVpiDriveStrength{
        static_cast<SystemVerilogVpiStrengthRank>(in.strength_zero),
        static_cast<SystemVerilogVpiStrengthRank>(in.strength_one)};
  }
  return value;
}

}  // namespace

SystemVerilogVpiSavedWrites SystemVerilogVpiValueControl::save_pending()
    const {
  SystemVerilogVpiSavedWrites saved;
  std::scoped_lock lock{impl_->mutex};
  saved.next_id = impl_->next_id;
  for (const auto& [id, record] : impl_->records) {
    if (record.status != SystemVerilogVpiScheduledWriteStatus::Pending) {
      continue;
    }
    SystemVerilogVpiSavedWrite write;
    write.id = id;
    if (const auto object = impl_->registry->lookup(record.object)) {
      write.object = object.value->full_name;
    }
    write.kind = static_cast<std::uint8_t>(record.kind);
    if (record.value) {
      encode_value(*record.value, write);
    }
    write.due = record.due;
    write.retained = record.retained;
    saved.writes.push_back(std::move(write));
  }
  // The table is unordered; the file's bytes must not depend on it.
  std::ranges::sort(saved.writes, [](const auto& left, const auto& right) {
    return left.id < right.id;
  });
  return saved;
}

bool SystemVerilogVpiValueControl::restore_pending(
    SystemVerilogVpiSavedWrites saved, std::string& error) {
  std::scoped_lock lock{impl_->mutex};
  impl_->records.clear();
  for (auto& write : saved.writes) {
    const auto object = impl_->registry->find(write.object);
    if (!object) {
      error = "the object '" + write.object + "' of a pending vpi_put_value "
          "is not in this design";
      return false;
    }
    if (write.kind
        > static_cast<std::uint8_t>(SystemVerilogVpiWriteKind::Release)) {
      error = "a pending vpi_put_value has an unknown kind";
      return false;
    }
    Impl::Record record;
    record.object = object.value->handle;
    record.kind = static_cast<SystemVerilogVpiWriteKind>(write.kind);
    record.value = decode_value(write);
    record.status = SystemVerilogVpiScheduledWriteStatus::Pending;
    record.due = write.due;
    record.retained = write.retained;
    impl_->records.emplace(write.id, std::move(record));
  }
  impl_->next_id = std::max(impl_->next_id, saved.next_id);
  return true;
}

void SystemVerilogVpiValueControl::reset_records() {
  std::scoped_lock lock{impl_->mutex};
  impl_->records.clear();
}

SystemVerilogVpiScheduleWriteResult
SystemVerilogVpiValueControl::schedule(
    const fsim_vpi_handle_v1 object,
    const SystemVerilogVpiWriteKind kind,
    std::optional<SystemVerilogVpiStoredValue> value,
    const SimulationTick delay,
    const SystemVerilogVpiDelayPolicy policy,
    const bool retain_handle) {
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

  const auto due = impl_->scheduler->now() + delay;
  std::vector<std::uint64_t> superseded;
  if (policy != SystemVerilogVpiDelayPolicy::Transport) {
    for (const auto& [id, record] : impl_->records) {
      if (record.status == SystemVerilogVpiScheduledWriteStatus::Pending
          && record.object == object
          && (policy == SystemVerilogVpiDelayPolicy::Inertial
              || record.due > due)) {
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
          due,
          retain_handle,
      });
  if (!inserted) {
    return schedule_failure(
        SystemVerilogVpiWriteControlError::ResourceLimit);
  }

  try {
    position->second.scheduler_handle =
        impl_->scheduler->schedule_after_cancelable(
            delay,
            SchedulerPhase::update,
            impl_->stable_order_base + id,
            SystemVerilogVpiValueControlEvents::task(impl_.get(), id));
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
    if (old->second.retained) {
      old->second.status =
          SystemVerilogVpiScheduledWriteStatus::Cancelled;
    } else {
      impl_->records.erase(old);
    }
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

SystemVerilogVpiWriteControlError SystemVerilogVpiValueControl::forget(
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
    // A released handle of a pending event is no longer valid (38.38).
    if (!found->second.retained) {
      return SystemVerilogVpiWriteControlError::NotFound;
    }
    found->second.retained = false;
  } else {
    impl_->records.erase(found);
  }
  return SystemVerilogVpiWriteControlError::None;
}

std::uint64_t SystemVerilogVpiValueControl::owner() const noexcept {
  return impl_ ? impl_->owner : 0U;
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
