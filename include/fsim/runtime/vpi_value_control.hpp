// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/vpi_object.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

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

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace fsim::runtime
