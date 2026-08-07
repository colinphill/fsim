// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class VhdlVhpiPhase : std::uint32_t {
  Update,
  Synchronization,
  ReadOnly,
  NextTime,
  Save,
  Restart,
  Reset,
  Terminal,
};

enum class VhdlVhpiTimeError {
  None,
  InvalidSimulation,
  InvalidProfile,
  InvalidPhase,
  InvalidCallback,
  CrossSimulation,
  Removed,
  ResourceLimit,
};

struct VhdlVhpiTimeProfile {
  std::int32_t unit_exponent{};
  std::int32_t precision_exponent{};
};

struct VhdlVhpiTimeSnapshot {
  SimulationTick ticks{};
  std::uint64_t delta{};
  VhdlVhpiTimeProfile profile;
  std::optional<SchedulerPhase> scheduler_phase;
  std::optional<SimulationTick> next_time;
};

struct VhdlVhpiPhaseEvent {
  VhdlVhpiPhase phase{VhdlVhpiPhase::Update};
  SimulationTick time{};
  std::uint64_t delta{};
  std::optional<SimulationTick> next_time;
};

using VhdlVhpiPhaseCallback =
    std::function<void(const VhdlVhpiPhaseEvent&)>;

struct VhdlVhpiCallbackDescriptor {
  std::uint64_t identity{};
  VhdlVhpiPhase phase{VhdlVhpiPhase::Update};
  std::uint64_t ordinal{};
  bool repeat{};
  bool active{};
  std::uint64_t invocations{};
};

template <typename T>
struct VhdlVhpiTimeResult {
  T value;
  VhdlVhpiTimeError error{VhdlVhpiTimeError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiTimeError::None;
  }
};

using VhdlVhpiTimeQueryResult =
    VhdlVhpiTimeResult<VhdlVhpiTimeSnapshot>;
using VhdlVhpiCallbackResult =
    VhdlVhpiTimeResult<VhdlVhpiCallbackDescriptor>;

class VhdlVhpiTimeSystem final {
 public:
  VhdlVhpiTimeSystem(
      std::uint64_t simulation_identity,
      Scheduler& scheduler,
      VhdlVhpiTimeProfile profile);
  ~VhdlVhpiTimeSystem();

  VhdlVhpiTimeSystem(const VhdlVhpiTimeSystem&) = delete;
  VhdlVhpiTimeSystem& operator=(const VhdlVhpiTimeSystem&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] VhdlVhpiTimeQueryResult query() const;
  [[nodiscard]] VhdlVhpiCallbackResult register_callback(
      VhdlVhpiPhase phase,
      bool repeat,
      VhdlVhpiPhaseCallback callback);
  [[nodiscard]] VhdlVhpiCallbackResult callback(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiTimeError remove_callback(
      std::uint64_t identity);
  [[nodiscard]] VhdlVhpiTimeError notify(VhdlVhpiPhase phase);

 private:
  struct CallbackEntry {
    VhdlVhpiCallbackDescriptor descriptor;
    VhdlVhpiPhaseCallback callback;
  };

  [[nodiscard]] static bool valid_profile(
      const VhdlVhpiTimeProfile& profile) noexcept;
  [[nodiscard]] static bool valid_phase(VhdlVhpiPhase phase) noexcept;
  [[nodiscard]] std::uint64_t make_identity(
      std::uint32_t local) const noexcept;
  [[nodiscard]] VhdlVhpiTimeError validate_identity(
      std::uint64_t identity) const noexcept;
  void safe_point(SchedulerPhase phase);
  void dispatch(
      VhdlVhpiPhase phase,
      std::optional<SimulationTick> next_time = std::nullopt);

  std::uint64_t simulation_identity_{};
  std::uint32_t system_identity_{};
  Scheduler* scheduler_{};
  VhdlVhpiTimeProfile profile_;
  std::uint32_t next_callback_{1};
  std::uint64_t next_ordinal_{};
  std::optional<SimulationTick> announced_next_time_;
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, CallbackEntry> callbacks_;
};

}  // namespace fsim::runtime
