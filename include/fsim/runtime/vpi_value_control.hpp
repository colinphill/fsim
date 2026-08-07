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

enum class SystemVerilogVpiDelayPolicy {
  Inertial,
  Transport,
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
      SystemVerilogVpiDelayPolicy policy);

  [[nodiscard]] SystemVerilogVpiWriteControlError cancel(
      SystemVerilogVpiScheduledWriteHandle handle);
  [[nodiscard]] SystemVerilogVpiScheduledWriteResult status(
      SystemVerilogVpiScheduledWriteHandle handle) const;
  [[nodiscard]] SystemVerilogVpiWriteControlError release(
      SystemVerilogVpiScheduledWriteHandle handle);
  [[nodiscard]] std::size_t pending() const noexcept;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace fsim::runtime
