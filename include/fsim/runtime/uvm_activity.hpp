// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/uvm_component.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

enum class SystemVerilogUvmActivityKind : std::uint8_t {
  Graph,
  PhaseState,
  Objection,
  Drain,
  Connection,
  Transaction,
  Fifo,
  Quiescence,
  Callback,
  Sequence,
  RegisterModel,
  Configuration,
};

enum class SystemVerilogUvmActivityAction : std::uint8_t {
  Created,
  Connected,
  Bound,
  Started,
  Updated,
  Completed,
  Cancelled,
  Raised,
  Dropped,
  Scheduled,
  Failed,
};

struct SystemVerilogUvmActivityEvent {
  SystemVerilogUvmActivityKind kind{SystemVerilogUvmActivityKind::Graph};
  SystemVerilogUvmActivityAction action{
      SystemVerilogUvmActivityAction::Updated};
  std::string identity;
  std::string detail;
  SystemVerilogUvmRootHandle root{};
  std::uint64_t value{};
  SimulationTick time{};
  std::uint64_t delta{};
  std::uint64_t sequence{};
};

struct SystemVerilogUvmActivityFailure {
  std::string diagnostic_code;
  SystemVerilogUvmActivityEvent event;
  std::uint64_t observer{};
  std::string message;
};

struct SystemVerilogUvmActivityLimits {
  std::size_t maximum_events{1U << 20U};
  std::size_t maximum_observers{512};
  std::size_t maximum_callbacks_per_event{512};
  std::size_t maximum_reentry_depth{64};
  std::size_t maximum_failures{65'536};
  std::size_t maximum_identity_bytes{4'096};
  std::size_t maximum_detail_bytes{16'384};
};

class SystemVerilogUvmActivityError final : public std::runtime_error {
 public:
  SystemVerilogUvmActivityError(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }

 private:
  std::string code_;
};

class SystemVerilogUvmActivityService final {
 public:
  using Observer = std::function<void(const SystemVerilogUvmActivityEvent&)>;

  explicit SystemVerilogUvmActivityService(
      SystemVerilogUvmActivityLimits limits = {});
  SystemVerilogUvmActivityService(
      Scheduler& scheduler,
      SystemVerilogUvmActivityLimits limits = {});

  void set_scheduler(Scheduler& scheduler) noexcept { scheduler_ = &scheduler; }
  [[nodiscard]] std::uint64_t add_observer(Observer observer);
  void remove_observer(std::uint64_t token) noexcept;
  void publish(SystemVerilogUvmActivityEvent event);
  [[nodiscard]] std::span<const SystemVerilogUvmActivityEvent> events()
      const noexcept {
    return events_;
  }
  [[nodiscard]] std::span<const SystemVerilogUvmActivityFailure> failures()
      const noexcept {
    return failures_;
  }
  void clear_events() noexcept { events_.clear(); }
  void clear_failures() noexcept { failures_.clear(); }
  [[nodiscard]] const SystemVerilogUvmActivityLimits& limits() const
      noexcept {
    return limits_;
  }

 private:
  Scheduler* scheduler_{};
  SystemVerilogUvmActivityLimits limits_;
  std::map<std::uint64_t, Observer> observers_;
  std::vector<SystemVerilogUvmActivityEvent> events_;
  std::vector<SystemVerilogUvmActivityFailure> failures_;
  std::uint64_t next_observer_{1};
  std::uint64_t next_sequence_{};
  std::size_t reentry_depth_{};
};

}  // namespace fsim::runtime
