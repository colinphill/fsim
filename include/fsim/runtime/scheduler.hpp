// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::runtime {

using SimulationTick = std::uint64_t;
using StableOrder = std::uint64_t;
using RuntimeSignalId = std::uint32_t;

enum class SchedulerPhase : std::uint8_t {
    active = 0,
    inactive = 1,
    update = 2,
    observed = 3,
    reactive = 4,
    re_inactive = 5,
    re_update = 6,
    postponed = 7,
};

[[nodiscard]] const char *phase_name(SchedulerPhase phase) noexcept;

// Return the scheduler region owned by an elaborated process declaration.
// Elaboration guarantees that at most one of these region flags is set.
[[nodiscard]] constexpr SchedulerPhase process_execution_phase(
    const bool observed,
    const bool reactive,
    const bool postponed) noexcept
{
    return postponed ? SchedulerPhase::postponed
        : reactive ? SchedulerPhase::reactive
        : observed ? SchedulerPhase::observed
        : SchedulerPhase::active;
}

struct SchedulerOptions {
  std::uint64_t max_delta_cycles = 100'000;
  std::size_t recent_signal_capacity = 32;
};

enum class RunStatus {
  completed,
  stopped,
  time_limit,
};

struct RunResult {
  RunStatus status = RunStatus::completed;
  SimulationTick time = 0;
  std::uint64_t delta = 0;
  std::uint64_t callbacks_executed = 0;
  /// Exact INTEGER status supplied by a language-level simulator-control
  /// procedure, when one was supplied. External stop requests leave it empty.
  std::optional<std::int64_t> simulator_status;
};

struct SchedulerBatchResult {
  std::size_t executed { };
  std::exception_ptr failure;
};

class Scheduler;
class CancellationSlots;

namespace detail {

inline constexpr std::size_t scheduler_task_payload_size
    = 4U * sizeof(std::uint64_t);

/// Compact, trivially-copyable task payload for runtime-owned scheduler work.
/// Public users should schedule callbacks with Scheduler::Task instead.
struct SchedulerTaskDescriptor {
  using Invoke = void (*)(Scheduler &, const std::byte *);

  Invoke invoke { };
  std::array<std::byte, scheduler_task_payload_size> payload { };

  void operator()(Scheduler &scheduler) const
  {
    if (invoke == nullptr) {
      throw std::logic_error("cannot invoke an empty scheduler task descriptor");
    }
    invoke(scheduler, payload.data());
  }
};

template <typename Payload,
          void (*Dispatch)(Scheduler &, const Payload &)>
[[nodiscard]] SchedulerTaskDescriptor make_scheduler_task_descriptor(
    const Payload &payload)
{
  static_assert(std::is_trivially_copyable_v<Payload>);
  static_assert(std::is_default_constructible_v<Payload>);
  static_assert(sizeof(Payload) <= scheduler_task_payload_size);

  SchedulerTaskDescriptor descriptor;
  descriptor.invoke = +[](Scheduler &scheduler, const std::byte *bytes) {
    Payload decoded { };
    std::memcpy(&decoded, bytes, sizeof(Payload));
    Dispatch(scheduler, decoded);
  };
  std::memcpy(descriptor.payload.data(), &payload, sizeof(Payload));
  return descriptor;
}

} // namespace detail

/// Stable, caller-owned executor for adjacent opt-in scheduler tasks.
/// Implementations consume only a leading payload prefix and contain any
/// exception in the returned result. The executor must outlive queued tasks.
class SchedulerBatchTask {
public:
  virtual ~SchedulerBatchTask() = default;
  [[nodiscard]] virtual SchedulerBatchResult execute(
      Scheduler&, std::span<const std::uint64_t> payloads) = 0;
};

class ScheduledTaskHandle {
public:
  ScheduledTaskHandle() = default;

  [[nodiscard]] explicit operator bool() const noexcept;

private:
  friend class Scheduler;

  explicit ScheduledTaskHandle(
      std::weak_ptr<const CancellationSlots> owner,
      std::size_t slot,
      std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::weak_ptr<const CancellationSlots> owner_;
  std::size_t slot_ { };
  std::uint64_t generation_ { };
};

/// Raised before executing a delta cycle beyond max_delta_cycles.
class DeltaCycleLimitError final : public std::runtime_error {
public:
  DeltaCycleLimitError(SimulationTick time, std::uint64_t limit,
                       std::vector<StableOrder> pending_orders,
                       std::vector<RuntimeSignalId> recent_signals);

  [[nodiscard]] SimulationTick time() const noexcept { return time_; }
  [[nodiscard]] std::uint64_t limit() const noexcept { return limit_; }
  [[nodiscard]] const std::vector<StableOrder> &pending_orders() const noexcept {
    return pending_orders_;
  }
  [[nodiscard]] const std::vector<RuntimeSignalId> &
  recent_signals() const noexcept {
    return recent_signals_;
  }

private:
  SimulationTick time_{};
  std::uint64_t limit_{};
  std::vector<StableOrder> pending_orders_;
  std::vector<RuntimeSignalId> recent_signals_;
};

/// A deterministic, single-thread event scheduler.
///
/// Tasks are ordered first by SchedulerPhase, then by StableOrder, and finally
/// by insertion sequence. A task scheduled into an already-completed phase at
/// the current timestamp is deferred to the next delta cycle. Explicit
/// schedule_next_delta calls always defer by one delta.
class Scheduler {
public:
  using Task = std::function<void(Scheduler &)>;
  using SlotStartHook = std::function<void(Scheduler &)>;
  using SafePointHook = std::function<void(Scheduler &, SchedulerPhase)>;
  using SafePointHookToken = std::uint64_t;
  using DiscardHook = void (*)(void *) noexcept;
  using DiscardHookToken = std::uint64_t;

  explicit Scheduler(SchedulerOptions options = {});
  ~Scheduler();
  /// Moving from a running or releasing scheduler throws std::logic_error.
  Scheduler(Scheduler &&);
  /// An assignment during either scheduler's run or cleanup has no effect.
  Scheduler &operator=(Scheduler &&) noexcept;
  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;

  void schedule_at(SimulationTick time, SchedulerPhase phase,
                   StableOrder stable_order, Task task);
  void schedule_after(SimulationTick delay, SchedulerPhase phase,
                      StableOrder stable_order, Task task);
  [[nodiscard]] ScheduledTaskHandle
  schedule_after_cancelable(SimulationTick delay, SchedulerPhase phase,
                            StableOrder stable_order, Task task);
  void cancel(const ScheduledTaskHandle &handle) noexcept;
  void schedule(SchedulerPhase phase, StableOrder stable_order, Task task);
  void schedule_next_delta(SchedulerPhase phase, StableOrder stable_order,
                           Task task);
  void schedule_next_delta_batchable(
      SchedulerPhase phase, StableOrder stable_order,
      SchedulerBatchTask& batch_task, std::uint64_t batch_payload,
      Task fallback_task);

  /// Schedule compact runtime-owned work without a per-entry callback object.
  /// The descriptor payload must not outlive its owning runtime context.
  void schedule_internal_at(
      SimulationTick time, SchedulerPhase phase, StableOrder stable_order,
      detail::SchedulerTaskDescriptor task);
  void schedule_internal(
      SchedulerPhase phase, StableOrder stable_order,
      detail::SchedulerTaskDescriptor task);
  void schedule_internal_next_delta(
      SchedulerPhase phase, StableOrder stable_order,
      detail::SchedulerTaskDescriptor task);

  /// Record a changed signal for a possible delta-limit diagnostic.
  void note_signal_change(RuntimeSignalId signal);

  [[nodiscard]] RunResult
  run(std::optional<SimulationTick> until = std::nullopt);

  void request_stop() noexcept;
  void clear_stop() noexcept;
  [[nodiscard]] bool stop_requested() const noexcept;

  /// Discard all queued work without changing the current simulation time.
  /// This is used after a terminal design stop before scheduling final-only
  /// lifecycle work.
  void discard_pending();
  /// Discard queued work, rewind time/delta identity, and clear a stop request.
  /// Reset is valid only while the scheduler is not running.
  void reset();

  [[nodiscard]] bool has_pending() const noexcept;
  [[nodiscard]] std::optional<SimulationTick>
  next_pending_time() const noexcept;
  [[nodiscard]] bool running() const noexcept;
  /// True only while invoking the hook between two completed scheduler phases.
  [[nodiscard]] bool at_safe_point() const noexcept;
  [[nodiscard]] SimulationTick now() const noexcept;
  [[nodiscard]] std::uint64_t delta() const noexcept;
  [[nodiscard]] std::optional<SchedulerPhase> current_phase() const noexcept;
  /// Monotonic identity changed whenever work enters the phase currently
  /// being executed. Batch executors use it to stop before crossing newly
  /// inserted canonical work.
  [[nodiscard]] std::uint64_t current_phase_revision() const noexcept;

  /// Observe a newly loaded simulation time slot before any phase executes.
  void set_slot_start_hook(SlotStartHook hook);
  /// Replace the legacy safe-point hook without changing its dispatch order.
  void set_safe_point_hook(SafePointHook hook);
  /// Add an observer after the legacy hook. Registry changes during dispatch
  /// take effect at the next safe point.
  [[nodiscard]] SafePointHookToken add_safe_point_hook(SafePointHook hook);
  void remove_safe_point_hook(SafePointHookToken token) noexcept;
  /// Observe discarded work after its task objects have been released.
  /// The callback must not change this hook registry while it is invoked.
  [[nodiscard]] DiscardHookToken add_discard_hook(
      void *context, DiscardHook hook);
  void remove_discard_hook(DiscardHookToken token) noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime
