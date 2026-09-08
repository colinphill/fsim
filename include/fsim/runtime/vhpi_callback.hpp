// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/vhpi_object.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace fsim::runtime {

enum class VhdlVhpiCallbackKind : std::uint32_t {
  Signal,
  Process,
  Event,
  Transaction,
  Assertion,
  StartOfSimulation,
  EndOfSimulation,
  StartOfSave,
  EndOfSave,
  StartOfRestart,
  EndOfRestart,
  StartOfReset,
  EndOfReset,
  ToolExecution,
};

enum class VhdlVhpiCallbackStatus : std::uint32_t {
  Active,
  Fired,
  Removed,
  CallbackFailed,
  TornDown,
};

enum class VhdlVhpiCallbackError {
  None,
  InvalidSimulation,
  InvalidKind,
  InvalidRequest,
  InvalidObject,
  CrossSimulation,
  InvalidCallback,
  NotActive,
  TornDown,
  ResourceLimit,
};

struct VhdlVhpiEventData {
  fsim_vhpi_handle_v1 object{};
  std::uint64_t related_identity{};
  std::optional<PackedLogic9> value;
  std::string message;
  std::optional<VhdlVhpiSourceLocation> source;
  std::uint32_t severity{};
};

struct VhdlVhpiCallbackEvent {
  VhdlVhpiCallbackKind kind{VhdlVhpiCallbackKind::Signal};
  std::uint64_t registration{};
  std::uint64_t simulation_identity{};
  std::uint64_t user_data{};
  SimulationTick time{};
  std::uint64_t delta{};
  VhdlVhpiEventData data;
};

using VhdlVhpiCallback =
    std::function<void(const VhdlVhpiCallbackEvent&)>;

struct VhdlVhpiCallbackRegistration {
  VhdlVhpiCallbackKind kind{VhdlVhpiCallbackKind::Signal};
  fsim_vhpi_handle_v1 object{};
  bool repeat{true};
  std::uint64_t user_data{};
  VhdlVhpiCallback callback;
};

struct VhdlVhpiCallbackDescriptor {
  std::uint64_t identity{};
  VhdlVhpiCallbackKind kind{VhdlVhpiCallbackKind::Signal};
  fsim_vhpi_handle_v1 object{};
  std::uint64_t ordinal{};
  bool repeat{};
  VhdlVhpiCallbackStatus status{VhdlVhpiCallbackStatus::Active};
  std::uint64_t invocations{};
  std::uint64_t user_data{};
};

template <typename T>
struct VhdlVhpiCallbackResult {
  T value;
  VhdlVhpiCallbackError error{VhdlVhpiCallbackError::None};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == VhdlVhpiCallbackError::None;
  }
};

using VhdlVhpiCallbackRegistrationResult =
    VhdlVhpiCallbackResult<VhdlVhpiCallbackDescriptor>;
using VhdlVhpiCallbackStatusResult =
    VhdlVhpiCallbackResult<VhdlVhpiCallbackDescriptor>;

class VhdlVhpiCallbackSystem final {
 public:
  VhdlVhpiCallbackSystem(
      VhdlVhpiObjectRegistry& objects, Scheduler& scheduler) noexcept;
  ~VhdlVhpiCallbackSystem();

  VhdlVhpiCallbackSystem(const VhdlVhpiCallbackSystem&) = delete;
  VhdlVhpiCallbackSystem& operator=(
      const VhdlVhpiCallbackSystem&) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] VhdlVhpiCallbackRegistrationResult register_callback(
      VhdlVhpiCallbackRegistration registration);
  [[nodiscard]] VhdlVhpiCallbackStatusResult status(
      std::uint64_t identity) const;
  [[nodiscard]] VhdlVhpiCallbackError remove_callback(
      std::uint64_t identity);
  [[nodiscard]] VhdlVhpiCallbackError publish(
      VhdlVhpiCallbackKind kind, const VhdlVhpiEventData& data = {});
  void teardown() noexcept;
  [[nodiscard]] std::size_t registrations() const;

 private:
  struct CallbackEntry {
    VhdlVhpiCallbackDescriptor descriptor;
    VhdlVhpiCallback callback;
  };

  [[nodiscard]] static bool valid_kind(
      VhdlVhpiCallbackKind kind) noexcept;
  [[nodiscard]] static bool lifecycle_kind(
      VhdlVhpiCallbackKind kind) noexcept;
  [[nodiscard]] VhdlVhpiCallbackError validate_object(
      VhdlVhpiCallbackKind kind, fsim_vhpi_handle_v1 object) const;
  [[nodiscard]] std::uint64_t make_identity(
      std::uint32_t local) const noexcept;
  [[nodiscard]] VhdlVhpiCallbackError validate_identity(
      std::uint64_t identity) const noexcept;
  void invoke(
      std::uint64_t identity,
      VhdlVhpiCallbackKind kind,
      const VhdlVhpiEventData& data);

  VhdlVhpiObjectRegistry* objects_{};
  Scheduler* scheduler_{};
  std::uint32_t system_identity_{};
  std::uint32_t next_callback_{1};
  std::uint64_t next_ordinal_{};
  bool torn_down_{};
  mutable std::mutex mutex_;
  std::unordered_map<std::uint64_t, CallbackEntry> callbacks_;
};

}  // namespace fsim::runtime
