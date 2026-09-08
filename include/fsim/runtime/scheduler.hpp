// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
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

  [[nodiscard]] explicit operator bool() const noexcept {
    return active_ && token_ < active_->size() && (*active_)[token_] != 0U
        && !owner_.expired();
  }

private:
  friend class Scheduler;

  explicit ScheduledTaskHandle(
      std::shared_ptr<const std::vector<std::uint8_t>> active,
      std::uint64_t token,
      std::weak_ptr<const void> owner)
      : active_(std::move(active)), token_(token), owner_(std::move(owner)) {}

  std::shared_ptr<const std::vector<std::uint8_t>> active_;
  std::uint64_t token_ { };
  std::weak_ptr<const void> owner_;
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

  explicit Scheduler(SchedulerOptions options = {});
  ~Scheduler();
  Scheduler(Scheduler &&) noexcept;
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
  void set_safe_point_hook(SafePointHook hook);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace fsim::runtime
