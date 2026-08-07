// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/scheduler.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <sstream>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::size_t phase_count = 5;

[[nodiscard]] constexpr std::size_t phase_index(SchedulerPhase phase) {
  return static_cast<std::size_t>(phase);
}

struct Entry {
  StableOrder order{};
  std::uint64_t sequence{};
  Scheduler::Task task;
  std::shared_ptr<bool> active;

  [[nodiscard]] bool is_cancelled() const noexcept {
    return active && !*active;
  }
};

struct WorkQueue {
  std::vector<Entry> entries;
  std::size_t cursor{};
  bool needs_sort{};

  [[nodiscard]] bool empty() const noexcept {
    return std::none_of(
        entries.begin() + static_cast<std::ptrdiff_t>(cursor),
        entries.end(),
        [](const Entry &entry) { return !entry.is_cancelled(); });
  }

  void push(Entry entry) {
    entries.push_back(std::move(entry));
    needs_sort = true;
  }

  Entry pop() {
    if (needs_sort) {
      std::stable_sort(entries.begin() + static_cast<std::ptrdiff_t>(cursor),
                       entries.end(), [](const Entry &lhs, const Entry &rhs) {
                         if (lhs.order != rhs.order) {
                           return lhs.order < rhs.order;
                         }
                         return lhs.sequence < rhs.sequence;
                       });
      needs_sort = false;
    }
    while (cursor < entries.size() && entries[cursor].is_cancelled()) {
      ++cursor;
    }
    return std::move(entries.at(cursor++));
  }

  void clear_consumed() {
    if (empty()) {
      entries.clear();
      cursor = 0;
      needs_sort = false;
    }
  }

  void cancel_pending() noexcept {
    for (auto index = cursor; index < entries.size(); ++index) {
      if (entries[index].active) *entries[index].active = false;
    }
  }

  [[nodiscard]] std::vector<StableOrder> pending_orders() const {
    std::vector<StableOrder> result;
    result.reserve(entries.size() - cursor);
    for (auto index = cursor; index < entries.size(); ++index) {
      if (!entries[index].is_cancelled()) {
        result.push_back(entries[index].order);
      }
    }
    return result;
  }
};

struct Bucket {
  std::array<WorkQueue, phase_count> queues;

  [[nodiscard]] bool empty() const noexcept {
    return std::all_of(queues.begin(), queues.end(),
                       [](const WorkQueue &queue) { return queue.empty(); });
  }

  void cancel_pending() noexcept {
    for (auto& queue : queues) queue.cancel_pending();
  }
};

struct CurrentSlot {
  SimulationTick time{};
  std::uint64_t delta{};
  std::size_t phase{};
  Bucket current;
  Bucket next_delta;
};

[[nodiscard]] std::string delta_error_message(SimulationTick time,
                                              std::uint64_t limit) {
  std::ostringstream message;
  message << "maximum delta-cycle count (" << limit
          << ") exceeded at simulation tick " << time;
  return message.str();
}

} // namespace

const char *phase_name(SchedulerPhase phase) noexcept {
  switch (phase) {
  case SchedulerPhase::active:
    return "active";
  case SchedulerPhase::inactive:
    return "inactive";
  case SchedulerPhase::update:
    return "update";
  case SchedulerPhase::reactive:
    return "reactive";
  case SchedulerPhase::postponed:
    return "postponed";
  }
  return "unknown";
}

DeltaCycleLimitError::DeltaCycleLimitError(
    SimulationTick time, std::uint64_t limit,
    std::vector<StableOrder> pending_orders,
    std::vector<RuntimeSignalId> recent_signals)
    : std::runtime_error(delta_error_message(time, limit)), time_(time),
      limit_(limit), pending_orders_(std::move(pending_orders)),
      recent_signals_(std::move(recent_signals)) {}

struct Scheduler::Impl {
  explicit Impl(SchedulerOptions scheduler_options)
      : options(scheduler_options) {
    if (options.max_delta_cycles == 0) {
      throw std::invalid_argument("max_delta_cycles must be greater than zero");
    }
  }

  SchedulerOptions options;
  std::map<SimulationTick, Bucket> future;
  std::optional<CurrentSlot> current;
  SimulationTick now{};
  std::uint64_t callbacks{};
  std::uint64_t next_sequence{};
  bool in_run{};
  bool in_callback{};
  std::atomic_bool stop{false};
  std::shared_ptr<const void> owner = std::make_shared<const bool>(true);
  SafePointHook safe_point_hook;
  std::vector<RuntimeSignalId> recent_signals;
  std::size_t recent_signal_cursor{};

  [[nodiscard]] Entry make_entry(
      StableOrder order,
      Task task,
      std::shared_ptr<bool> active = {}) {
    if (!task) {
      throw std::invalid_argument("cannot schedule an empty task");
    }
    if (next_sequence == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error("scheduler insertion sequence overflow");
    }
    return Entry{
        order, next_sequence++, std::move(task), std::move(active)};
  }

  void load_next_slot() {
    auto first = future.begin();
    CurrentSlot slot;
    slot.time = first->first;
    slot.current = std::move(first->second);
    future.erase(first);
    now = slot.time;
    current.emplace(std::move(slot));
  }

  [[nodiscard]] std::vector<StableOrder> pending_next_orders() const {
    std::vector<StableOrder> result;
    if (!current) {
      return result;
    }
    for (const auto &queue : current->next_delta.queues) {
      auto orders = queue.pending_orders();
      result.insert(result.end(), orders.begin(), orders.end());
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
  }
};

Scheduler::Scheduler(SchedulerOptions options)
    : impl_(std::make_unique<Impl>(options)) {}

Scheduler::~Scheduler() = default;
Scheduler::Scheduler(Scheduler &&) noexcept = default;
Scheduler &Scheduler::operator=(Scheduler &&) noexcept = default;

void Scheduler::schedule_at(SimulationTick time, SchedulerPhase phase,
                            StableOrder stable_order, Task task) {
  if (time < impl_->now) {
    throw std::invalid_argument("cannot schedule an event in the past");
  }
  auto entry = impl_->make_entry(stable_order, std::move(task));
  const auto index = phase_index(phase);
  if (index >= phase_count) {
    throw std::invalid_argument("invalid scheduler phase");
  }

  if (impl_->current && time == impl_->current->time) {
    const bool phase_finished = index < impl_->current->phase;
    auto &bucket =
        phase_finished ? impl_->current->next_delta : impl_->current->current;
    bucket.queues[index].push(std::move(entry));
    return;
  }
  impl_->future[time].queues[index].push(std::move(entry));
}

void Scheduler::schedule_after(SimulationTick delay, SchedulerPhase phase,
                               StableOrder stable_order, Task task) {
  if (delay > std::numeric_limits<SimulationTick>::max() - impl_->now) {
    throw std::overflow_error("simulation time overflow while scheduling event");
  }
  schedule_at(impl_->now + delay, phase, stable_order, std::move(task));
}

ScheduledTaskHandle Scheduler::schedule_after_cancelable(
    const SimulationTick delay,
    const SchedulerPhase phase,
    const StableOrder stable_order,
    Task task) {
  if (delay > std::numeric_limits<SimulationTick>::max() - impl_->now) {
    throw std::overflow_error("simulation time overflow while scheduling event");
  }
  const auto time = impl_->now + delay;
  const auto active = std::make_shared<bool>(true);
  auto entry =
      impl_->make_entry(stable_order, std::move(task), active);
  const auto index = phase_index(phase);
  if (index >= phase_count) {
    throw std::invalid_argument("invalid scheduler phase");
  }

  if (impl_->current && time == impl_->current->time) {
    const bool phase_finished = index < impl_->current->phase;
    auto &bucket =
        phase_finished ? impl_->current->next_delta : impl_->current->current;
    bucket.queues[index].push(std::move(entry));
  } else {
    impl_->future[time].queues[index].push(std::move(entry));
  }
  return ScheduledTaskHandle{active, impl_->owner};
}

void Scheduler::cancel(const ScheduledTaskHandle &handle) noexcept {
  if (!impl_ || !handle.active_) return;
  const auto owner = handle.owner_.lock();
  if (owner && owner.get() == impl_->owner.get()) {
    *handle.active_ = false;
  }
}

void Scheduler::schedule(SchedulerPhase phase, StableOrder stable_order,
                         Task task) {
  schedule_at(impl_->now, phase, stable_order, std::move(task));
}

void Scheduler::schedule_next_delta(SchedulerPhase phase,
                                    StableOrder stable_order, Task task) {
  auto entry = impl_->make_entry(stable_order, std::move(task));
  const auto index = phase_index(phase);
  if (index >= phase_count) {
    throw std::invalid_argument("invalid scheduler phase");
  }
  if (impl_->current) {
    impl_->current->next_delta.queues[index].push(std::move(entry));
    return;
  }

  // Before a run begins, "next delta" means the initial delta at now.
  impl_->future[impl_->now].queues[index].push(std::move(entry));
}

void Scheduler::note_signal_change(RuntimeSignalId signal) {
  const auto capacity = impl_->options.recent_signal_capacity;
  if (capacity == 0) {
    return;
  }
  if (impl_->recent_signals.size() < capacity) {
    impl_->recent_signals.push_back(signal);
    return;
  }
  impl_->recent_signals[impl_->recent_signal_cursor] = signal;
  impl_->recent_signal_cursor =
      (impl_->recent_signal_cursor + 1) % impl_->recent_signals.size();
}

RunResult Scheduler::run(std::optional<SimulationTick> until) {
  if (impl_->in_run) {
    throw std::logic_error("Scheduler::run is not reentrant");
  }
  if (until && *until < impl_->now) {
    throw std::invalid_argument("run time limit is before the current time");
  }

  struct RunGuard {
    bool &running;
    ~RunGuard() { running = false; }
  };
  impl_->in_run = true;
  RunGuard guard{impl_->in_run};
  const auto initial_callbacks = impl_->callbacks;

  auto result = [&](RunStatus status) {
    return RunResult{status,
                     impl_->now,
                     impl_->current ? impl_->current->delta : 0,
                     impl_->callbacks - initial_callbacks};
  };

  while (true) {
    if (impl_->stop.load(std::memory_order_relaxed)) {
      return result(RunStatus::stopped);
    }

    if (!impl_->current) {
      while (!impl_->future.empty()
             && impl_->future.begin()->second.empty()) {
        impl_->future.erase(impl_->future.begin());
      }
      if (impl_->future.empty()) {
        if (until && impl_->now < *until) {
          impl_->now = *until;
          return result(RunStatus::time_limit);
        }
        return result(RunStatus::completed);
      }
      if (until && impl_->future.begin()->first > *until) {
        impl_->now = *until;
        return result(RunStatus::time_limit);
      }
      impl_->load_next_slot();
    }

    auto &slot = *impl_->current;
    if (slot.phase == phase_count) {
      if (slot.next_delta.empty()) {
        impl_->current.reset();
        continue;
      }
      if (slot.delta + 1 >= impl_->options.max_delta_cycles) {
        throw DeltaCycleLimitError(
            slot.time, impl_->options.max_delta_cycles,
            impl_->pending_next_orders(), impl_->recent_signals);
      }
      slot.current = std::move(slot.next_delta);
      slot.next_delta = {};
      ++slot.delta;
      slot.phase = 0;
      continue;
    }

    auto &queue = slot.current.queues[slot.phase];
    const auto phase = static_cast<SchedulerPhase>(slot.phase);
    while (!queue.empty()) {
      if (impl_->stop.load(std::memory_order_relaxed)) {
        return result(RunStatus::stopped);
      }
      auto entry = queue.pop();
      if (entry.active) *entry.active = false;
      impl_->in_callback = true;
      try {
        entry.task(*this);
      } catch (...) {
        impl_->in_callback = false;
        throw;
      }
      impl_->in_callback = false;
      ++impl_->callbacks;
    }
    queue.clear_consumed();

    ++slot.phase;
    if (impl_->safe_point_hook) {
      // Advance the phase cursor before exposing the safe point. A callback
      // that schedules into the just-completed phase must enter the next
      // delta rather than an already-consumed queue.
      impl_->safe_point_hook(*this, phase);
    }
  }
}

void Scheduler::request_stop() noexcept {
  impl_->stop.store(true, std::memory_order_relaxed);
}

void Scheduler::clear_stop() noexcept {
  impl_->stop.store(false, std::memory_order_relaxed);
}

bool Scheduler::stop_requested() const noexcept {
  return impl_->stop.load(std::memory_order_relaxed);
}

void Scheduler::discard_pending() {
  if (impl_->in_run || impl_->in_callback) {
    throw std::logic_error(
        "cannot discard scheduler work while it is running");
  }
  if (impl_->current) {
    impl_->current->current.cancel_pending();
    impl_->current->next_delta.cancel_pending();
  }
  for (auto& [time, bucket] : impl_->future) {
    (void)time;
    bucket.cancel_pending();
  }
  impl_->current.reset();
  impl_->future.clear();
}

void Scheduler::reset() {
  discard_pending();
  impl_->now = 0;
  impl_->callbacks = 0;
  impl_->next_sequence = 0;
  impl_->recent_signals.clear();
  impl_->recent_signal_cursor = 0;
  impl_->stop.store(false, std::memory_order_relaxed);
}

bool Scheduler::has_pending() const noexcept {
  if (impl_->current) {
    return true;
  }
  return std::any_of(
      impl_->future.begin(),
      impl_->future.end(),
      [](const auto &entry) { return !entry.second.empty(); });
}

std::optional<SimulationTick>
Scheduler::next_pending_time() const noexcept {
  for (const auto& [time, bucket] : impl_->future) {
    if (!bucket.empty()) {
      return time;
    }
  }
  return std::nullopt;
}

bool Scheduler::running() const noexcept { return impl_->in_run; }
SimulationTick Scheduler::now() const noexcept { return impl_->now; }
std::uint64_t Scheduler::delta() const noexcept {
  return impl_->current ? impl_->current->delta : 0;
}

std::optional<SchedulerPhase> Scheduler::current_phase() const noexcept {
  if (!impl_->current || impl_->current->phase >= phase_count) {
    return std::nullopt;
  }
  return static_cast<SchedulerPhase>(impl_->current->phase);
}

void Scheduler::set_safe_point_hook(SafePointHook hook) {
  impl_->safe_point_hook = std::move(hook);
}

} // namespace fsim::runtime
