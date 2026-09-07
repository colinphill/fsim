// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application_tf.hpp"
#include "fsim/runtime/scheduler.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace fsim::app {

inline constexpr std::uint32_t kMaxTfSchedulerCalls = 1U << 16U;
inline constexpr std::uint32_t kMaxTfSchedulerCallbacks = 1U << 16U;

enum class TfSchedulerError {
  None,
  InvalidCoordinator,
  Binding,
  InvalidHandle,
  CrossCoordinator,
  InactiveScheduler,
  Reentrant,
  Invocation,
  Scheduling,
  Publication,
  ResourceLimit,
  Overflow,
  Allocation,
};

enum class TfSchedulerCallbackKind {
  Call,
  Reactivate,
  ReadWriteSynchronize,
  ReadOnlySynchronize,
};

struct TfSchedulerCallHandle {
  std::uint64_t owner{};
  std::uint64_t id{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return owner != 0U && id != 0U;
  }

  friend bool operator==(
      const TfSchedulerCallHandle&,
      const TfSchedulerCallHandle&) = default;
};

struct TfSchedulerPublication {
  TfSchedulerCallHandle call;
  TfSchedulerCallbackKind callback{TfSchedulerCallbackKind::Call};
  runtime::TfInstanceIdentity instance;
  runtime::SimulationTick time{};
  std::uint64_t delta{};
  runtime::SchedulerPhase phase{runtime::SchedulerPhase::active};
  std::span<const runtime::TfArgumentUpdate> argument_updates;
  std::span<const runtime::TfControlEffect> control_effects;
  std::span<const runtime::TfDelayRequest> delay_requests;
  std::span<const runtime::TfSynchronizationRequest>
      synchronization_requests;
};

using TfSchedulerPublishHook =
    std::function<bool(const TfSchedulerPublication&)>;

struct TfSchedulerBindResult {
  TfSchedulerCallHandle value;
  TfSchedulerError error{TfSchedulerError::None};
  TfApplicationError application_error{TfApplicationError::None};
  runtime::TfCallError call_error{runtime::TfCallError::None};
  std::vector<runtime::TfControlEffect> lifecycle_effects;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfSchedulerError::None && static_cast<bool>(value);
  }
};

struct TfSchedulerInvokeResult {
  TfSchedulerError error{TfSchedulerError::None};
  runtime::TfCallError call_error{runtime::TfCallError::None};
  std::int32_t callback_value{};
  std::optional<runtime::TfFunctionResult> function_result;
  std::uint32_t argument_updates{};
  std::uint32_t control_effects{};
  std::uint32_t callbacks_scheduled{};

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfSchedulerError::None;
  }
};

struct TfSchedulerCallSnapshot {
  TfSchedulerCallHandle handle;
  TfSchedulerError error{TfSchedulerError::None};
  runtime::TfCallError last_call_error{runtime::TfCallError::None};
  std::uint64_t invocations{};
  std::uint64_t callback_failures{};
  std::uint32_t pending_callbacks{};
  std::vector<runtime::TfArgumentValue> values;
  std::optional<runtime::TfControlEffectKind> terminal_effect;

  [[nodiscard]] explicit operator bool() const noexcept {
    return error == TfSchedulerError::None;
  }
};

class TfSchedulerCoordinator final {
public:
  explicit TfSchedulerCoordinator(
      runtime::Scheduler& scheduler,
      runtime::StableOrder stable_order_base = 0,
      TfSchedulerPublishHook publish = {});
  ~TfSchedulerCoordinator();

  TfSchedulerCoordinator(const TfSchedulerCoordinator&) = delete;
  TfSchedulerCoordinator& operator=(const TfSchedulerCoordinator&) = delete;
  TfSchedulerCoordinator(TfSchedulerCoordinator&&) = delete;
  TfSchedulerCoordinator& operator=(TfSchedulerCoordinator&&) = delete;

  [[nodiscard]] TfSchedulerBindResult bind(
      TfApplicationRegistry& registry,
      frontend::StandardRevision profile,
      std::string_view name,
      std::span<const runtime::TfArgument> arguments = {},
      runtime::TfInstanceIdentity instance = {},
      runtime::TfTimeProfile time_profile = {},
      const runtime::TfContextProfile& context_profile = {}) noexcept;

  [[nodiscard]] TfSchedulerBindResult adopt(
      runtime::TfBindResult binding) noexcept;

  [[nodiscard]] TfSchedulerInvokeResult invoke(
      TfSchedulerCallHandle call,
      std::span<const runtime::TfArgumentValue> values = {}) noexcept;

  [[nodiscard]] TfSchedulerCallSnapshot snapshot(
      TfSchedulerCallHandle call) const noexcept;

  [[nodiscard]] std::uint32_t call_count() const noexcept;
  [[nodiscard]] std::uint32_t pending_callbacks() const noexcept;

  // Public only so translation-unit helpers can name the opaque state type.
  struct Impl;

private:
  std::shared_ptr<Impl> impl_;
};

}  // namespace fsim::app
