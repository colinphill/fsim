// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/dpi_callback.hpp"
#include "fsim/runtime/scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogDpiTaskError {
  None,
  InvalidName,
  DuplicateName,
  UnknownName,
  InvalidScope,
  ArityMismatch,
  DirectionMismatch,
  Disabled,
  SchedulerFailure,
  Exception,
  StaleTask,
};

enum class SystemVerilogDpiTaskStatus {
  Pending,
  Suspended,
  Completed,
  Cancelled,
  Failed,
  Disabled,
};

enum class SystemVerilogDpiTaskActionKind {
  Complete,
  Suspend,
};

struct SystemVerilogDpiTaskAction {
  SystemVerilogDpiTaskActionKind kind{
      SystemVerilogDpiTaskActionKind::Complete};
  SimulationTick delay{};
};

using SystemVerilogDpiImportedTask = std::function<SystemVerilogDpiTaskAction(
    SystemVerilogDpiCallbackFrame&, std::size_t)>;

struct SystemVerilogDpiTaskHandle {
  std::uint64_t simulation{};
  std::uint32_t slot{};
  std::uint32_t epoch{};

  friend bool operator==(
      const SystemVerilogDpiTaskHandle&,
      const SystemVerilogDpiTaskHandle&) = default;
};

struct SystemVerilogDpiTaskStartResult {
  SystemVerilogDpiTaskHandle value;
  SystemVerilogDpiTaskError error{SystemVerilogDpiTaskError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == SystemVerilogDpiTaskError::None;
  }
};

struct SystemVerilogDpiTaskResult {
  SystemVerilogDpiTaskStatus status{SystemVerilogDpiTaskStatus::Pending};
  std::vector<std::vector<PackedLogic4>> values;
  SystemVerilogDpiTaskError error{SystemVerilogDpiTaskError::None};
  std::string message;
};

class SystemVerilogDpiImportedTaskRegistry final {
 public:
  SystemVerilogDpiImportedTaskRegistry(
      Scheduler& scheduler,
      const SystemVerilogDpiScopeRegistry& scopes) noexcept;
  ~SystemVerilogDpiImportedTaskRegistry();
  SystemVerilogDpiImportedTaskRegistry(
      const SystemVerilogDpiImportedTaskRegistry&) = delete;
  SystemVerilogDpiImportedTaskRegistry& operator=(
      const SystemVerilogDpiImportedTaskRegistry&) = delete;
  SystemVerilogDpiImportedTaskRegistry(
      SystemVerilogDpiImportedTaskRegistry&&) = delete;
  SystemVerilogDpiImportedTaskRegistry& operator=(
      SystemVerilogDpiImportedTaskRegistry&&) = delete;

  [[nodiscard]] SystemVerilogDpiTaskError register_task(
      std::string linkage_name,
      SystemVerilogDpiScopeHandle scope,
      std::vector<SystemVerilogDpiTransferMode> directions,
      SystemVerilogDpiImportedTask task);
  [[nodiscard]] SystemVerilogDpiTaskStartResult start(
      std::string_view linkage_name,
      std::vector<std::vector<PackedLogic4>> arguments,
      SystemVerilogDpiCallbackContext& context);
  [[nodiscard]] std::optional<SystemVerilogDpiTaskResult> result(
      SystemVerilogDpiTaskHandle handle) const;
  [[nodiscard]] bool cancel(SystemVerilogDpiTaskHandle handle) noexcept;

 private:
  struct Entry {
    SystemVerilogDpiScopeHandle scope;
    std::vector<SystemVerilogDpiTransferMode> directions;
    SystemVerilogDpiImportedTask task;
  };

  struct Invocation {
    std::uint32_t epoch{1};
    Entry entry;
    SystemVerilogDpiTaskStatus status{SystemVerilogDpiTaskStatus::Pending};
    std::vector<std::vector<PackedLogic4>> values;
    SystemVerilogDpiTaskError error{SystemVerilogDpiTaskError::None};
    std::string message;
    SystemVerilogDpiCallbackContext* context{};
    ScheduledTaskHandle pending;
    std::size_t resume_count{};
  };

  [[nodiscard]] Invocation* invocation(
      SystemVerilogDpiTaskHandle handle) noexcept;
  [[nodiscard]] const Invocation* invocation(
      SystemVerilogDpiTaskHandle handle) const noexcept;
  [[nodiscard]] SystemVerilogDpiTaskHandle handle(
      std::size_t slot) const noexcept;
  void schedule(std::size_t slot, SimulationTick delay) noexcept;
  void resume(std::size_t slot) noexcept;
  void fail(
      Invocation& invocation,
      SystemVerilogDpiTaskError error,
      std::string message = {}) noexcept;

  Scheduler* scheduler_{};
  const SystemVerilogDpiScopeRegistry* scopes_{};
  std::unordered_map<std::string, Entry> tasks_;
  std::deque<Invocation> invocations_;
};

}  // namespace fsim::runtime
