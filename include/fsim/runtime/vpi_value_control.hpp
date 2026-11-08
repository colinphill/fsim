// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/vpi_object.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogVpiWriteKind {
  Deposit,
  Force,
  Release,
};

// IEEE 1800-2023 38.34: vpiInertialDelay removes every scheduled event on
// the object, vpiTransportDelay (modified transport) removes the events
// scheduled for later times, and vpiPureTransportDelay removes none.
enum class SystemVerilogVpiDelayPolicy {
  Inertial,
  Transport,
  ModifiedTransport,
};

enum class SystemVerilogVpiScheduledWriteStatus {
  Pending,
  Applied,
  Cancelled,
  Failed,
};

enum class SystemVerilogVpiWriteControlError {
  None,
  InvalidControl,
  InvalidRequest,
  InvalidHandle,
  CrossControl,
  NotFound,
  NotPending,
  ResourceLimit,
};

struct SystemVerilogVpiScheduledWriteHandle {
  std::uint64_t owner{};
  std::uint64_t id{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return owner != 0U && id != 0U;
  }

  friend bool operator==(
      const SystemVerilogVpiScheduledWriteHandle&,
      const SystemVerilogVpiScheduledWriteHandle&) = default;
};

struct SystemVerilogVpiScheduleWriteResult {
  SystemVerilogVpiScheduledWriteHandle value;
  SystemVerilogVpiWriteControlError error{
      SystemVerilogVpiWriteControlError::None};
  SystemVerilogVpiValueError value_error{
      SystemVerilogVpiValueError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiWriteControlError::None
        && value_error == SystemVerilogVpiValueError::None
        && static_cast<bool>(value);
  }
};

struct SystemVerilogVpiScheduledWriteResult {
  SystemVerilogVpiScheduledWriteStatus status{
      SystemVerilogVpiScheduledWriteStatus::Pending};
  SystemVerilogVpiValueError value_error{
      SystemVerilogVpiValueError::None};
  SystemVerilogVpiWriteControlError error{
      SystemVerilogVpiWriteControlError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogVpiWriteControlError::None;
  }
};

// One pending delayed write as a run-state file holds it (#128; IEEE 1800-2023
// 38.34 vpi_put_value with a delay): the object by its full name (a handle
// number belongs to one process), the value as plain fields, and the time it is
// due. A write that is no longer pending is not saved: its handle does not
// survive a restart (38.36.3).
struct SystemVerilogVpiSavedWrite {
  std::uint64_t id{};
  std::string object;
  std::uint8_t kind{};
  // 0 no value (a release), 1 PackedBit2, 2 PackedLogic4, 3 PackedLogic9,
  // 4 double, 5 float, 6 string, 7 unsigned integer.
  std::uint8_t value_kind{};
  std::string text;
  std::uint64_t bits{};
  bool has_strength{};
  std::uint8_t strength_zero{};
  std::uint8_t strength_one{};
  SimulationTick due{};
  bool retained{};
};

struct SystemVerilogVpiSavedWrites {
  std::uint64_t next_id{};
  std::vector<SystemVerilogVpiSavedWrite> writes;
};

struct SystemVerilogVpiValueControlEvents;

class SystemVerilogVpiValueControl final {
 public:
  SystemVerilogVpiValueControl(
      SystemVerilogVpiObjectRegistry& registry,
      Scheduler& scheduler,
      StableOrder stable_order_base = 0);
  ~SystemVerilogVpiValueControl();

  SystemVerilogVpiValueControl(
      const SystemVerilogVpiValueControl&) = delete;
  SystemVerilogVpiValueControl& operator=(
      const SystemVerilogVpiValueControl&) = delete;
  SystemVerilogVpiValueControl(
      SystemVerilogVpiValueControl&&) = delete;
  SystemVerilogVpiValueControl& operator=(
      SystemVerilogVpiValueControl&&) = delete;

  [[nodiscard]] SystemVerilogVpiValueError apply(
      fsim_vpi_handle_v1 object,
      SystemVerilogVpiWriteKind kind,
      std::optional<SystemVerilogVpiStoredValue> value = std::nullopt);

  [[nodiscard]] SystemVerilogVpiScheduleWriteResult schedule(
      fsim_vpi_handle_v1 object,
      SystemVerilogVpiWriteKind kind,
      std::optional<SystemVerilogVpiStoredValue> value,
      SimulationTick delay,
      SystemVerilogVpiDelayPolicy policy,
      bool retain_handle = true);

  [[nodiscard]] SystemVerilogVpiWriteControlError cancel(
      SystemVerilogVpiScheduledWriteHandle handle);
  [[nodiscard]] SystemVerilogVpiScheduledWriteResult status(
      SystemVerilogVpiScheduledWriteHandle handle) const;
  [[nodiscard]] SystemVerilogVpiWriteControlError release(
      SystemVerilogVpiScheduledWriteHandle handle);
  // Drop the handle without affecting the event: a pending write keeps
  // running and its record goes once it is no longer pending
  // (vpi_release_handle on a vpiSchedEvent, IEEE 1800-2023 38.34).
  [[nodiscard]] SystemVerilogVpiWriteControlError forget(
      SystemVerilogVpiScheduledWriteHandle handle);
  [[nodiscard]] std::size_t pending() const noexcept;
  [[nodiscard]] std::uint64_t owner() const noexcept;

  // Run-state save and restart of pending delayed writes (#128). A pending
  // write is the scheduler event kind "vpi.scheduled_write" (data: owner and
  // record id); the record itself is saved with the foreign state. These bind
  // the owner domain "vpi_value_control" for the event codec and give a
  // restored event's handle back to its record (IEEE 1800-2023 38.34 vpi_put_value
  // with a delay).
  void bind_event_owner(SchedulerEventOwners& owners);
  [[nodiscard]] bool bind_restored_write(
      std::uint64_t id, const ScheduledTaskHandle& handle);
  // The pending records, in id order, for the run-state file.
  [[nodiscard]] SystemVerilogVpiSavedWrites save_pending() const;
  // Replaces every record with the saved pending ones, before the scheduler's
  // events are restored (each restored "vpi.scheduled_write" event then finds
  // its record through bind_restored_write). False, with `error`, when an
  // object of a record is not in the design.
  [[nodiscard]] bool restore_pending(
      SystemVerilogVpiSavedWrites saved, std::string& error);
  // Drops every record: a reset cancels all scheduled events (IEEE 1800-2023
  // D.8 b) and the handles of the old run are gone with them.
  void reset_records();

 private:
  friend struct SystemVerilogVpiValueControlEvents;
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace fsim::runtime
