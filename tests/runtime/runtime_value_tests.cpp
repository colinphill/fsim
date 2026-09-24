// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "../../src/runtime/simir_cohort_snapshot_pool.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cfenv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace fsim::tests::runtime {

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

class ScopedEnvironment final {
public:
  ScopedEnvironment(
      std::string name, std::optional<std::string> value)
      : name_(std::move(name))
      , previous_(read()) {
    if (set(value) != 0) {
      throw std::runtime_error("unable to update test environment");
    }
  }

  ScopedEnvironment(const ScopedEnvironment &) = delete;
  ScopedEnvironment &operator=(const ScopedEnvironment &) = delete;

  ~ScopedEnvironment() { (void)set(previous_); }

private:
  [[nodiscard]] std::optional<std::string> read() const {
    if (const char *value = std::getenv(name_.c_str())) {
      return std::string{value};
    }
    return std::nullopt;
  }

  int set(const std::optional<std::string> &value) const {
#if defined(_WIN32)
    return ::_putenv_s(name_.c_str(), value ? value->c_str() : "");
#else
    return value ? ::setenv(name_.c_str(), value->c_str(), 1)
                 : ::unsetenv(name_.c_str());
#endif
  }

  std::string name_;
  std::optional<std::string> previous_;
};

struct SchedulerTaskProbe {
  std::vector<std::uint64_t> *values;
  std::uint64_t value;
};

void record_scheduler_task_probe(
    fsim::runtime::Scheduler &,
    const SchedulerTaskProbe &probe) {
  probe.values->push_back(probe.value);
}

struct SchedulerTaskTimingProbe {
  std::vector<std::string> *events;
  std::uint64_t value;
};

void record_scheduler_task_timing_probe(
    fsim::runtime::Scheduler &scheduler,
    const SchedulerTaskTimingProbe &probe) {
  probe.events->push_back(
      std::to_string(scheduler.now()) + ":"
      + std::to_string(scheduler.delta()) + ":"
      + std::to_string(probe.value));
}

} // namespace

void test_scheduler_phase_order() {
  using namespace fsim::runtime;

  Scheduler scheduler;
  std::vector<std::string> events;
  scheduler.schedule(
      SchedulerPhase::active, 2, [&](Scheduler &runtime) {
        events.emplace_back("active-2");
        runtime.schedule(SchedulerPhase::inactive, 2,
                         [&](Scheduler &) { events.emplace_back("inactive"); });
        runtime.schedule(SchedulerPhase::update, 2, [&](Scheduler &update) {
          events.emplace_back("update");
          update.note_signal_change(7);
          update.schedule(SchedulerPhase::active, 1, [&](Scheduler &) {
            events.emplace_back("next-delta");
          });
        });
        runtime.schedule(SchedulerPhase::observed, 2, [&](Scheduler&) {
            events.emplace_back("observed");
        });
        runtime.schedule(SchedulerPhase::reactive, 2, [&](Scheduler& reactive) {
            events.emplace_back("reactive");
            reactive.schedule(SchedulerPhase::inactive, 2, [&](Scheduler&) {
                events.emplace_back("re-inactive");
            });
            reactive.schedule(SchedulerPhase::update, 2, [&](Scheduler&) {
                events.emplace_back("re-update");
            });
        });
        runtime.schedule(SchedulerPhase::postponed, 2, [&](Scheduler &) {
          events.emplace_back("postponed");
        });
      });
  scheduler.schedule(SchedulerPhase::active, 1, [&](Scheduler &) {
    events.emplace_back("active-1");
  });
  scheduler.schedule_at(4, SchedulerPhase::active, 0, [&](Scheduler &) {
    events.emplace_back("time-4");
  });

  const auto result = scheduler.run();
  require(result.status == RunStatus::completed, "scheduler must complete");
  require(result.time == 4, "scheduler must advance to future event");
  const std::vector<std::string> expected = {
      "active-1", "active-2", "inactive", "update",
      "observed", "reactive", "re-inactive", "re-update",
      "postponed", "next-delta", "time-4"
  };
  require(events == expected, "scheduler phase ordering");

  Scheduler stable_order;
  std::vector<std::string> ordered_events;
  constexpr std::array phases = {
      SchedulerPhase::active,
      SchedulerPhase::inactive,
      SchedulerPhase::update,
      SchedulerPhase::observed,
      SchedulerPhase::reactive,
      SchedulerPhase::re_inactive,
      SchedulerPhase::re_update,
      SchedulerPhase::postponed,
  };
  const auto add_ordered_event = [&](const SchedulerPhase phase,
                                     const StableOrder order,
                                     const std::string &label) {
    const auto event = std::to_string(static_cast<unsigned>(phase))
        + ":" + label;
    stable_order.schedule(phase, order, [&, event](Scheduler &) {
      ordered_events.push_back(event);
    });
  };
  for (const auto phase : phases) {
    add_ordered_event(phase,
        std::numeric_limits<StableOrder>::max(), "max-first");
    add_ordered_event(phase, StableOrder { 1 } << 63U, "high-first");
    add_ordered_event(phase, 0U, "zero");
    add_ordered_event(phase, StableOrder { 1 } << 32U, "middle");
    add_ordered_event(phase, StableOrder { 1 } << 63U, "high-second");
    add_ordered_event(phase,
        std::numeric_limits<StableOrder>::max(), "max-second");
  }
  const auto stable_result = stable_order.run();
  std::vector<std::string> expected_ordered_events;
  for (const auto phase : phases) {
    const auto prefix = std::to_string(static_cast<unsigned>(phase)) + ":";
    expected_ordered_events.push_back(prefix + "zero");
    expected_ordered_events.push_back(prefix + "middle");
    expected_ordered_events.push_back(prefix + "high-first");
    expected_ordered_events.push_back(prefix + "high-second");
    expected_ordered_events.push_back(prefix + "max-first");
    expected_ordered_events.push_back(prefix + "max-second");
  }
  require(stable_result.status == RunStatus::completed
              && stable_result.callbacks_executed == 48U
              && ordered_events == expected_ordered_events,
          "all regions order the full StableOrder range then insertion sequence");

  Scheduler delta_queues;
  std::vector<std::string> delta_events;
  std::function<void(Scheduler &, std::uint64_t)> schedule_wave;
  schedule_wave = [&](Scheduler &runtime, const std::uint64_t wave) {
    for (const auto phase : phases) {
      const auto phase_index = static_cast<unsigned>(phase);
      runtime.schedule_next_delta(phase, phase_index,
          [&, wave, phase, phase_index](Scheduler &scheduled) {
            require(scheduled.delta() == wave
                        && scheduled.current_phase() == phase,
                    "rotated region queue retains its phase and delta");
            delta_events.push_back(
                std::to_string(wave) + ":"
                + std::to_string(phase_index));
            if (phase == SchedulerPhase::active && wave < 2U) {
              schedule_wave(scheduled, wave + 1U);
            }
          });
    }
  };
  schedule_wave(delta_queues, 0U);
  const auto delta_result = delta_queues.run();
  std::vector<std::string> expected_delta_events;
  for (std::uint64_t wave = 0; wave <= 2U; ++wave) {
    for (const auto phase : phases) {
      expected_delta_events.push_back(
          std::to_string(wave) + ":"
          + std::to_string(static_cast<unsigned>(phase)));
    }
  }
  require(delta_result.status == RunStatus::completed
              && delta_result.callbacks_executed == 24U
              && delta_events == expected_delta_events,
          "all eight region queues retain canonical order across delta waves");
}

void test_scheduler_stop_resume() {
  using namespace fsim::runtime;

  Scheduler scheduler;
  int count = 0;
  scheduler.schedule(SchedulerPhase::active, 0, [&](Scheduler &runtime) {
    ++count;
    runtime.request_stop();
  });
  scheduler.schedule(SchedulerPhase::active, 1,
                     [&](Scheduler &) { ++count; });

  require(scheduler.run().status == RunStatus::stopped,
          "stop request must stop at a callback boundary");
  require(count == 1, "pending callback must be retained");
  scheduler.clear_stop();
  require(scheduler.run().status == RunStatus::completed,
          "scheduler must resume");
  require(count == 2, "retained callback must execute after resume");
}

void test_scheduler_ownership_and_failure_containment() {
  using namespace fsim::runtime;

  Scheduler owner;
  Scheduler unrelated;
  int count = 0;
  const auto throwing = owner.schedule_after_cancelable(
      0, SchedulerPhase::active, 0,
      [](Scheduler&) { throw std::runtime_error("scheduled failure"); });
  const auto retained = owner.schedule_after_cancelable(
      0, SchedulerPhase::active, 1,
      [&](Scheduler&) { count += 10; });
  const auto cross_owner = owner.schedule_after_cancelable(
      0, SchedulerPhase::active, 2,
      [&](Scheduler&) { ++count; });
  const auto cancelled = owner.schedule_after_cancelable(
      0, SchedulerPhase::active, 3,
      [&](Scheduler&) { count += 100; });
  unrelated.cancel(cross_owner);
  owner.cancel(cancelled);
  require(
      throwing && retained && cross_owner && !cancelled,
      "cancelable handles retain owner identity before execution");

  bool caught = false;
  try {
    (void)owner.run();
  } catch (const std::runtime_error& error) {
    caught = std::string_view{error.what()} == "scheduled failure";
  }
  require(
      caught && !throwing && retained && cross_owner && !owner.running(),
      "callback failures propagate once and release scheduler run state");
  require(
      owner.run().status == RunStatus::completed && count == 11
          && !retained && !cross_owner,
      "pending callbacks survive a contained failure in stable order");

  const auto discarded = owner.schedule_after_cancelable(
      1, SchedulerPhase::active, 0,
      [&](Scheduler&) { ++count; });
  require(discarded && owner.has_pending(),
          "future cancelable work exposes a live handle");
  owner.discard_pending();
  require(
      !discarded && !owner.has_pending(),
      "discarding work invalidates every pending handle");

  ScheduledTaskHandle expired;
  {
    Scheduler transient;
    expired = transient.schedule_after_cancelable(
        1, SchedulerPhase::active, 0,
        [](Scheduler&) {});
    require(
        static_cast<bool>(expired),
        "a scheduled handle is live while its owner exists");
  }
  require(!expired, "a handle expires with its owning scheduler");

  Scheduler moving;
  const auto moved = moving.schedule_after_cancelable(
      1, SchedulerPhase::active, 0,
      [](Scheduler&) {});
  Scheduler destination = std::move(moving);
  moving.cancel(moved);
  require(
      static_cast<bool>(moved),
      "a moved-from scheduler cannot cancel transferred work");
  destination.cancel(moved);
  require(!moved, "scheduler moves preserve cancelable-work ownership");

  Scheduler reuse;
  std::weak_ptr<int> cancelled_payload;
  ScheduledTaskHandle stale;
  {
    auto payload = std::make_shared<int>(17);
    cancelled_payload = payload;
    stale = reuse.schedule_after_cancelable(
        1, SchedulerPhase::active, 0,
        [payload](Scheduler&) { (void)payload; });
  }
  reuse.cancel(stale);
  require(!stale && cancelled_payload.expired(),
          "cancellation releases a captured payload immediately");

  Scheduler reentrant;
  std::vector<ScheduledTaskHandle> spawned;
  int spawned_count = 0;
  ScheduledTaskHandle source;
  {
    auto payload = std::shared_ptr<int>(new int(1), [&](int* value) {
      delete value;
      for (int index = 0; index < 128; ++index) {
        spawned.push_back(reentrant.schedule_after_cancelable(
            1, SchedulerPhase::active, static_cast<StableOrder>(index),
            [&](Scheduler&) { ++spawned_count; }));
      }
    });
    source = reentrant.schedule_after_cancelable(
        1, SchedulerPhase::active, 0,
        [payload](Scheduler&) { (void)payload; });
  }
  reentrant.cancel(source);
  require(!source && spawned.size() == 128U,
          "cancellation completes its slot before destroying a reentrant payload");
  require(reentrant.run().status == RunStatus::completed
              && spawned_count == 128,
          "tasks scheduled by a cancelled payload retain valid slots");

  Scheduler discarding;
  ScheduledTaskHandle rejected_during_discard;
  ScheduledTaskHandle discarded_reentrant;
  {
    auto payload = std::shared_ptr<int>(new int(1), [&](int* value) {
      delete value;
      rejected_during_discard = discarding.schedule_after_cancelable(
          1, SchedulerPhase::active, 0,
          [](Scheduler&) { throw std::runtime_error("discard reentry ran"); });
    });
    discarded_reentrant = discarding.schedule_after_cancelable(
        1'000, SchedulerPhase::active, 0,
        [payload](Scheduler&) { (void)payload; });
  }
  discarding.discard_pending();
  require(!discarded_reentrant && !rejected_during_discard
              && !discarding.has_pending(),
          "discard rejects work scheduled by a destroyed task payload");

  Scheduler nested_discard;
  ScheduledTaskHandle rejected_after_nested_reset;
  {
    auto first = std::shared_ptr<int>(new int(1), [&](int* value) {
      delete value;
      nested_discard.reset();
    });
    auto second = std::shared_ptr<int>(new int(2), [&](int* value) {
      delete value;
      rejected_after_nested_reset = nested_discard.schedule_after_cancelable(
          1, SchedulerPhase::active, 0,
          [](Scheduler&) { throw std::runtime_error("nested discard reentry ran"); });
    });
    const auto first_handle = nested_discard.schedule_after_cancelable(
        1, SchedulerPhase::active, 0,
        [first](Scheduler&) { (void)first; });
    const auto second_handle = nested_discard.schedule_after_cancelable(
        2, SchedulerPhase::active, 0,
        [second](Scheduler&) { (void)second; });
    require(first_handle && second_handle,
            "nested discard starts with two live handles");
  }
  nested_discard.discard_pending();
  require(!rejected_after_nested_reset && !nested_discard.has_pending(),
          "nested reset keeps the outer discard guard active");

  Scheduler move_source;
  Scheduler move_destination;
  bool source_alive_during_move = false;
  {
    auto payload = std::shared_ptr<int>(new int(1), [&](int* value) {
      delete value;
      source_alive_during_move = static_cast<bool>(
          move_source.schedule_after_cancelable(
              1, SchedulerPhase::active, 0, [](Scheduler&) {}));
    });
    const auto handle = move_destination.schedule_after_cancelable(
        1, SchedulerPhase::active, 0,
        [payload](Scheduler&) { (void)payload; });
    require(static_cast<bool>(handle),
            "move destination starts with queued work");
  }
  move_destination = std::move(move_source);
  require(source_alive_during_move,
          "move assignment releases old payloads while the source is alive");

  Scheduler running_move;
  Scheduler move_peer;
  int move_callback_count = 0;
  running_move.schedule_after(1, SchedulerPhase::active, 0,
                              [&](Scheduler& active) {
    active = std::move(move_peer);
    move_peer = std::move(active);
    ++move_callback_count;
    active.schedule_after(1, SchedulerPhase::active, 0,
                          [&](Scheduler&) { ++move_callback_count; });
  });
  require(running_move.run().status == RunStatus::completed
              && move_callback_count == 2,
          "move assignment during a callback preserves the running state");

  Scheduler running_move_construct;
  bool active_move_construct_rejected = false;
  int active_move_construct_callbacks = 0;
  running_move_construct.schedule_after(
      1, SchedulerPhase::active, 0, [&](Scheduler& active) {
        try {
          Scheduler moved(std::move(active));
          static_cast<void>(moved);
        } catch (const std::logic_error&) {
          active_move_construct_rejected = true;
        }
        active.schedule_after(
            1, SchedulerPhase::active, 0,
            [&](Scheduler&) { ++active_move_construct_callbacks; });
      });
  require(running_move_construct.run().status == RunStatus::completed
              && active_move_construct_rejected
              && active_move_construct_callbacks == 1,
          "move construction during a callback is rejected without damaging the run");

  ScheduledTaskHandle rejected_during_destruction;
  {
    Scheduler dying;
    auto payload = std::shared_ptr<int>(new int(1), [&](int* value) {
      delete value;
      rejected_during_destruction = dying.schedule_after_cancelable(
          1, SchedulerPhase::active, 0,
          [](Scheduler&) { throw std::runtime_error("destructor reentry ran"); });
    });
    static_cast<void>(dying.schedule_after_cancelable(
        1'000, SchedulerPhase::active, 0,
        [payload](Scheduler&) { (void)payload; }));
  }
  require(!rejected_during_destruction,
          "destruction rejects work from a destroyed task payload");

  int reused_count = 0;
  auto live = reuse.schedule_after_cancelable(
      1, SchedulerPhase::active, 0,
      [&](Scheduler&) { ++reused_count; });
  reuse.cancel(stale);
  require(static_cast<bool>(live),
          "an old handle cannot cancel a reused slot");
  require(reuse.run().status == RunStatus::completed && reused_count == 1,
          "reused cancellation slot executes the new task");
  require(!live, "completed task invalidates its handle");

  for (int iteration = 0; iteration < 250'000; ++iteration) {
    auto handle = reuse.schedule_after_cancelable(
        1, SchedulerPhase::active, 0,
        [](Scheduler&) { throw std::runtime_error("cancelled task ran"); });
    reuse.cancel(handle);
    require(!handle, "cancelled handle must expire during slot reuse");
  }
  live = reuse.schedule_after_cancelable(
      1, SchedulerPhase::active, 0,
      [&](Scheduler&) { ++reused_count; });
  reuse.cancel(stale);
  require(live && reuse.run().status == RunStatus::completed
              && reused_count == 2,
          "repeated cancellation retains the latest task only");

  const auto before_reset = reuse.schedule_after_cancelable(
      1, SchedulerPhase::active, 0,
      [](Scheduler&) { throw std::runtime_error("reset task ran"); });
  reuse.reset();
  live = reuse.schedule_after_cancelable(
      1, SchedulerPhase::active, 0,
      [&](Scheduler&) { ++reused_count; });
  reuse.cancel(before_reset);
  require(!before_reset && live
              && reuse.run().status == RunStatus::completed
              && reused_count == 3,
          "reset invalidates old handles without cancelling reused slots");

  struct DiscardProbe {
    int calls { };

    static void notify(void* context) noexcept {
      ++static_cast<DiscardProbe*>(context)->calls;
    }
  };
  struct ReentrantDiscardProbe {
    Scheduler* scheduler { };
    int calls { };
    ScheduledTaskHandle rejected_schedule;

    static void notify(void* context) noexcept {
      auto& probe = *static_cast<ReentrantDiscardProbe*>(context);
      ++probe.calls;
      probe.scheduler->reset();
      probe.rejected_schedule = probe.scheduler->schedule_after_cancelable(
          1, SchedulerPhase::active, 0, [](Scheduler&) {});
    }
  };

  DiscardProbe probe;
  ReentrantDiscardProbe reentrant_discard_probe;
  Scheduler hook_reentry;
  reentrant_discard_probe.scheduler = &hook_reentry;
  const auto reentrant_hook = hook_reentry.add_discard_hook(
      &reentrant_discard_probe, &ReentrantDiscardProbe::notify);
  hook_reentry.schedule_after(
      1, SchedulerPhase::active, 0, [](Scheduler&) {});
  hook_reentry.discard_pending();
  require(reentrant_discard_probe.calls == 1
              && !reentrant_discard_probe.rejected_schedule
              && !hook_reentry.has_pending(),
          "discard hooks cannot recursively reset or schedule during cleanup");
  hook_reentry.reset();
  require(reentrant_discard_probe.calls == 2
              && !reentrant_discard_probe.rejected_schedule
              && !hook_reentry.has_pending(),
          "reset notifies once while nested hook reset remains guarded");
  hook_reentry.remove_discard_hook(reentrant_hook);

  Scheduler observed_discard;
  const auto discard_token = observed_discard.add_discard_hook(
      &probe, &DiscardProbe::notify);
  observed_discard.schedule_after(
      1, SchedulerPhase::active, 0, [](Scheduler&) {});
  observed_discard.discard_pending();
  observed_discard.reset();
  require(probe.calls == 2,
          "discard and reset notify registered owners after queue release");
  observed_discard.remove_discard_hook(discard_token);
  observed_discard.discard_pending();
  require(probe.calls == 2,
          "removed discard observers do not receive later notifications");

  {
    Scheduler moving_with_hook;
    (void)moving_with_hook.add_discard_hook(
        &probe, &DiscardProbe::notify);
    moving_with_hook.schedule_after(
        1, SchedulerPhase::active, 0, [](Scheduler&) {});
    Scheduler moved_with_hook = std::move(moving_with_hook);
    require(!moved_with_hook.has_pending() && probe.calls == 3,
            "moving an observed scheduler releases its queued work");
  }
  require(probe.calls == 3,
          "moved scheduler teardown does not retain a stale discard observer");

  Scheduler moving_reentrant_hook;
  reentrant_discard_probe.scheduler = &moving_reentrant_hook;
  (void)moving_reentrant_hook.add_discard_hook(
      &reentrant_discard_probe, &ReentrantDiscardProbe::notify);
  moving_reentrant_hook.schedule_after(
      1, SchedulerPhase::active, 0, [](Scheduler&) {});
  Scheduler moved_reentrant_hook = std::move(moving_reentrant_hook);
  require(reentrant_discard_probe.calls == 3
              && !reentrant_discard_probe.rejected_schedule
              && !moved_reentrant_hook.has_pending(),
          "move cleanup keeps its reentrancy guard active through notification");

  {
    Scheduler dying_with_hook;
    (void)dying_with_hook.add_discard_hook(
        &probe, &DiscardProbe::notify);
    dying_with_hook.schedule_after(
        1, SchedulerPhase::active, 0, [](Scheduler&) {});
  }
  require(probe.calls == 4,
          "scheduler teardown notifies owners after queue release");

  {
    Scheduler dying_with_reentrant_hook;
    reentrant_discard_probe.scheduler = &dying_with_reentrant_hook;
    (void)dying_with_reentrant_hook.add_discard_hook(
        &reentrant_discard_probe, &ReentrantDiscardProbe::notify);
    dying_with_reentrant_hook.schedule_after(
        1, SchedulerPhase::active, 0, [](Scheduler&) {});
  }
  require(reentrant_discard_probe.calls == 4
              && !reentrant_discard_probe.rejected_schedule,
          "destruction keeps its reentrancy guard active through notification");

  {
    Scheduler assigned_with_hook;
    (void)assigned_with_hook.add_discard_hook(
        &probe, &DiscardProbe::notify);
    Scheduler replacement;
    assigned_with_hook = std::move(replacement);
    assigned_with_hook.reset();
    require(probe.calls == 6,
            "move assignment keeps the destination discard observer");
  }
  require(probe.calls == 7,
          "an assigned scheduler still notifies its owner at teardown");

  Scheduler ordered;
  std::vector<int> observed;
  for (int order = 127; order >= 0; --order) {
    auto handle = ordered.schedule_after_cancelable(
        1, SchedulerPhase::active, static_cast<StableOrder>(order),
        [&, order](Scheduler&) { observed.push_back(order); });
    if ((order & 1) != 0)
      ordered.cancel(handle);
  }
  require(ordered.run().status == RunStatus::completed,
          "compacted cancellation queue completes");
  require(observed.size() == 64U,
          "compaction excludes every cancelled task");
  for (std::size_t index = 0; index < observed.size(); ++index)
    require(observed[index] == static_cast<int>(2U * index),
            "compaction preserves stable scheduler order");

  Scheduler sparse_future;
  for (SimulationTick delay = 1; delay <= 100'000; ++delay) {
    auto handle = sparse_future.schedule_after_cancelable(
        delay, SchedulerPhase::active, 0,
        [](Scheduler&) { throw std::runtime_error("sparse task ran"); });
    sparse_future.cancel(handle);
  }
  require(!sparse_future.has_pending()
              && sparse_future.run().status == RunStatus::completed
              && sparse_future.now() == 0U,
          "cancelled distinct future buckets do not advance simulation time");
}

void test_scheduler_batch_contract() {
  using namespace fsim::runtime;

  class RecordingBatch final : public SchedulerBatchTask {
  public:
    [[nodiscard]] SchedulerBatchResult execute(
        Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override {
      calls.emplace_back(payloads.begin(), payloads.end());
      const auto consumed = std::min(limit, payloads.size());
      for (std::size_t index = 0; index < consumed; ++index)
        executed.push_back(payloads[index]);
      if (stop_once) {
        stop_once = false;
        scheduler.request_stop();
      }
      if (fail_once) {
        fail_once = false;
        return {consumed,
                std::make_exception_ptr(
                    std::runtime_error("contained batch failure"))};
      }
      return {consumed, {}};
    }

    std::size_t limit = std::numeric_limits<std::size_t>::max();
    bool stop_once = false;
    bool fail_once = false;
    std::vector<std::vector<std::uint64_t>> calls;
    std::vector<std::uint64_t> executed;
  };

  const auto schedule_batch = [](
                                  Scheduler& scheduler,
                                  RecordingBatch& batch,
                                  const StableOrder order,
                                  const std::uint64_t payload,
                                  std::vector<std::uint64_t>& fallbacks) {
    scheduler.schedule_next_delta_batchable(
        SchedulerPhase::active, order, batch, payload,
        [payload, &fallbacks](Scheduler&) {
          fallbacks.push_back(payload);
        });
  };

  Scheduler ordered;
  RecordingBatch first_batch;
  RecordingBatch second_batch;
  std::vector<std::uint64_t> fallbacks;
  schedule_batch(ordered, first_batch, 30U, 3U, fallbacks);
  schedule_batch(ordered, first_batch, 10U, 1U, fallbacks);
  schedule_batch(ordered, first_batch, 20U, 2U, fallbacks);
  const SchedulerTaskProbe split_batch_payload { &fallbacks, 99U };
  ordered.schedule_internal_at(
      0U, SchedulerPhase::active, 25U,
      detail::make_scheduler_task_descriptor<
          SchedulerTaskProbe, record_scheduler_task_probe>(
          split_batch_payload));
  schedule_batch(ordered, second_batch, 40U, 4U, fallbacks);
  schedule_batch(ordered, second_batch, 50U, 5U, fallbacks);
  const auto ordered_result = ordered.run();
  require(
      ordered_result.status == RunStatus::completed
          && ordered_result.callbacks_executed == 6U
          && first_batch.calls
              == std::vector<std::vector<std::uint64_t>> {
                  {1U, 2U}, {3U}}
          && first_batch.executed
              == std::vector<std::uint64_t> {1U, 2U, 3U}
          && second_batch.calls
              == std::vector<std::vector<std::uint64_t>> {{4U, 5U}}
          && fallbacks == std::vector<std::uint64_t> {99U},
      "scheduler batches only adjacent canonically ordered tasks");

  Scheduler partial;
  RecordingBatch partial_batch;
  partial_batch.limit = 1U;
  std::vector<std::uint64_t> partial_fallbacks;
  for (std::uint64_t value = 1U; value <= 3U; ++value)
    schedule_batch(
        partial, partial_batch, value, value, partial_fallbacks);
  const auto partial_result = partial.run();
  require(
      partial_result.status == RunStatus::completed
          && partial_result.callbacks_executed == 3U
          && partial_batch.calls
              == std::vector<std::vector<std::uint64_t>> {
                  {1U, 2U, 3U}, {2U, 3U}, {3U}}
          && partial_batch.executed
              == std::vector<std::uint64_t> {1U, 2U, 3U}
          && partial_fallbacks.empty(),
      "a partially consumed batch requeues its untouched suffix");

  Scheduler stopped;
  RecordingBatch stopping_batch;
  stopping_batch.limit = 1U;
  stopping_batch.stop_once = true;
  std::vector<std::uint64_t> stopped_fallbacks;
  schedule_batch(stopped, stopping_batch, 1U, 1U, stopped_fallbacks);
  schedule_batch(stopped, stopping_batch, 2U, 2U, stopped_fallbacks);
  require(
      stopped.run().status == RunStatus::stopped
          && stopping_batch.executed == std::vector<std::uint64_t> {1U}
          && stopped.has_pending(),
      "a batch stop retains its unexecuted suffix");
  stopped.clear_stop();
  require(
      stopped.run().status == RunStatus::completed
          && stopping_batch.executed
              == std::vector<std::uint64_t> {1U, 2U},
      "a stopped scheduler resumes the retained batch suffix");

  Scheduler failed;
  RecordingBatch failing_batch;
  failing_batch.limit = 1U;
  failing_batch.fail_once = true;
  std::vector<std::uint64_t> failed_fallbacks;
  schedule_batch(failed, failing_batch, 1U, 1U, failed_fallbacks);
  schedule_batch(failed, failing_batch, 2U, 2U, failed_fallbacks);
  bool caught = false;
  try {
    (void)failed.run();
  } catch (const std::runtime_error& error) {
    caught = std::string_view {error.what()}
        == "contained batch failure";
  }
  require(
      caught && failing_batch.executed == std::vector<std::uint64_t> {1U}
          && failed.has_pending(),
      "a contained batch failure retains its unexecuted suffix");
  require(
      failed.run().status == RunStatus::completed
          && failing_batch.executed
              == std::vector<std::uint64_t> {1U, 2U},
      "scheduler resumes after a contained batch failure");

  Scheduler internal;
  std::vector<std::string> internal_events;
  internal.schedule_internal_at(
      3U, SchedulerPhase::observed, 30U,
      detail::make_scheduler_task_descriptor<
          SchedulerTaskTimingProbe, record_scheduler_task_timing_probe>(
          SchedulerTaskTimingProbe { &internal_events, 30U }));
  internal.schedule(SchedulerPhase::active, 0U,
      [&](Scheduler &runtime) {
        runtime.schedule_internal(
            SchedulerPhase::active, 10U,
            detail::make_scheduler_task_descriptor<
                SchedulerTaskTimingProbe,
                record_scheduler_task_timing_probe>(
                SchedulerTaskTimingProbe { &internal_events, 10U }));
        runtime.schedule_internal_next_delta(
            SchedulerPhase::active, 20U,
            detail::make_scheduler_task_descriptor<
                SchedulerTaskTimingProbe,
                record_scheduler_task_timing_probe>(
                SchedulerTaskTimingProbe { &internal_events, 20U }));
      });
  const auto internal_result = internal.run();
  require(internal_result.status == RunStatus::completed
              && internal_result.callbacks_executed == 4U
              && internal_events
                  == std::vector<std::string> {
                      "0:0:10", "0:1:20", "3:0:30" },
          "typed tasks retain callback ordering, deltas, and absolute time");
}

void test_scheduler_time_limit_before_future_event() {
  using namespace fsim::runtime;

  Scheduler scheduler;
  scheduler.schedule_at(
      10, SchedulerPhase::active, 0, [](Scheduler&) {});

  const auto limited = scheduler.run(4);
  require(
      limited.status == RunStatus::time_limit,
      "scheduler must report an intermediate time limit");
  require(
      limited.time == 4 && scheduler.now() == 4,
      "scheduler must advance to a time limit before the next future event");
  require(scheduler.has_pending(), "future event must remain pending");

  const auto completed = scheduler.run();
  require(
      completed.status == RunStatus::completed && completed.time == 10,
      "scheduler must resume from the intermediate time limit");
}

void test_scheduler_safe_point_scheduling() {
  using namespace fsim::runtime;

  Scheduler scheduler;
  int callbacks = 0;
  bool scheduled = false;
  scheduler.schedule(
      SchedulerPhase::active, 0,
      [&](Scheduler&) { ++callbacks; });
  scheduler.set_safe_point_hook(
      [&](Scheduler& runtime, const SchedulerPhase phase) {
        if (phase == SchedulerPhase::active && !scheduled) {
          scheduled = true;
          runtime.schedule(
              SchedulerPhase::active, 1,
              [&](Scheduler&) { ++callbacks; });
        }
      });
  const auto result = scheduler.run();
  require(result.status == RunStatus::completed,
          "safe-point scheduled work must complete");
  require(callbacks == 2,
          "work scheduled into a completed phase must run next delta");

  Scheduler composed;
  std::vector<std::string> hook_events;
  Scheduler::SafePointHookToken removed_observer { };
  Scheduler::SafePointHookToken added_observer { };
  bool replaced_primary = false;
  composed.set_safe_point_hook(
      [&](Scheduler &runtime, const SchedulerPhase phase) {
        hook_events.push_back(
            "primary:" + std::to_string(static_cast<unsigned>(phase)));
        require(runtime.at_safe_point(),
                "composed hooks run at the existing safe point");
        if (phase == SchedulerPhase::active && !replaced_primary) {
          replaced_primary = true;
          runtime.remove_safe_point_hook(removed_observer);
          added_observer = runtime.add_safe_point_hook(
              [&](Scheduler &added_runtime,
                  const SchedulerPhase added_phase) {
                require(added_runtime.at_safe_point(),
                        "added observers run at the existing safe point");
                hook_events.push_back(
                    "added:" + std::to_string(
                        static_cast<unsigned>(added_phase)));
              });
          runtime.set_safe_point_hook(
              [&](Scheduler &replacement_runtime,
                  const SchedulerPhase replacement_phase) {
                require(replacement_runtime.at_safe_point(),
                        "replacement hook runs at the existing safe point");
                hook_events.push_back(
                    "replacement:" + std::to_string(
                        static_cast<unsigned>(replacement_phase)));
              });
        }
      });
  static_cast<void>(composed.add_safe_point_hook(
      [&](Scheduler &runtime, const SchedulerPhase phase) {
        require(runtime.at_safe_point(),
                "registered observers run at the existing safe point");
        hook_events.push_back(
            "first:" + std::to_string(static_cast<unsigned>(phase)));
      }));
  removed_observer = composed.add_safe_point_hook(
      [&](Scheduler &runtime, const SchedulerPhase phase) {
        require(runtime.at_safe_point(),
                "removed observer stays alive for its dispatch snapshot");
        hook_events.push_back(
            "removed:" + std::to_string(static_cast<unsigned>(phase)));
      });
  composed.schedule(
      SchedulerPhase::active, 0U, [](Scheduler &) { });
  const auto composed_result = composed.run();
  std::vector<std::string> expected_hook_events {
      "primary:0", "first:0", "removed:0" };
  for (unsigned phase = 1U; phase < 8U; ++phase) {
    expected_hook_events.push_back("replacement:" + std::to_string(phase));
    expected_hook_events.push_back("first:" + std::to_string(phase));
    expected_hook_events.push_back("added:" + std::to_string(phase));
  }
  require(composed_result.status == RunStatus::completed
              && hook_events == expected_hook_events,
          "safe-point hook mutation applies after the active dispatch snapshot");
  composed.remove_safe_point_hook(added_observer);

  Scheduler throwing_hook;
  throwing_hook.schedule(
      SchedulerPhase::active, 0U, [](Scheduler &) { });
  const auto throwing_token = throwing_hook.add_safe_point_hook(
      [](Scheduler &, SchedulerPhase) {
        throw std::runtime_error("safe-point observer failure");
      });
  bool caught_hook_failure = false;
  try {
    static_cast<void>(throwing_hook.run());
  } catch (const std::runtime_error &error) {
    caught_hook_failure = std::string_view { error.what() }
        == "safe-point observer failure";
  }
  throwing_hook.remove_safe_point_hook(throwing_token);
  require(caught_hook_failure && !throwing_hook.running()
              && !throwing_hook.at_safe_point()
              && throwing_hook.run().status == RunStatus::completed,
          "a throwing observer restores scheduler state and permits resume");
}

void test_scheduler_delta_limit() {
  using namespace fsim::runtime;

  Scheduler scheduler({3, 4});
  std::function<void(Scheduler &)> oscillate;
  oscillate = [&](Scheduler &runtime) {
    runtime.note_signal_change(42);
    runtime.schedule_next_delta(SchedulerPhase::active, 9, oscillate);
  };
  scheduler.schedule(SchedulerPhase::active, 9, oscillate);
  try {
    (void)scheduler.run();
    throw std::runtime_error("delta limit did not fire");
  } catch (const DeltaCycleLimitError &error) {
    require(error.time() == 0, "delta error time");
    require(error.limit() == 3, "delta error limit");
    require(!error.pending_orders().empty() &&
                error.pending_orders().front() == 9,
            "delta error pending process");
    require(!error.recent_signals().empty() &&
                error.recent_signals().back() == 42,
            "delta error recent signal");
  }
}

void test_simir() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"top.q", PackedLogic4::from_msb_string("0")});
  Process process;
  process.id = 0;
  process.name = "driver";
  process.register_count = 1;
  process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("1")});
  process.operations.emplace_back(WriteUpdate{signal, 0});
  process.operations.emplace_back(WaitFor{5});
  process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("0")});
  process.operations.emplace_back(WriteBlocking{signal, 0});
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));

  std::vector<std::pair<SimulationTick, std::string>> changes;
  interpreter.set_signal_change_hook(
      [&](SignalId, const PackedLogic4 &value, SimulationTick time) {
        changes.emplace_back(time, value.to_msb_string());
      });
  const auto result = interpreter.run();
  require(result.status == RunStatus::completed, "SimIR run must complete");
  require(interpreter.signal_value(signal).to_msb_string() == "0",
          "SimIR final signal value");
  require(changes ==
              std::vector<std::pair<SimulationTick, std::string>>{
                  {0, "1"}, {5, "0"}},
          "SimIR update/delay behavior");
}

void test_simir_permanent_wait() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"top.unreachable", PackedLogic4::from_msb_string("0")});
  Process process;
  process.id = 0;
  process.name = "permanent_wait";
  process.register_count = 1;
  process.operations = {
      WaitForever{},
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{signal, 0},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  std::vector<ExecutionPoint> points;
  interpreter.set_execution_point_hook(
      [&](Scheduler&, const ExecutionPoint& point) {
        points.push_back(point);
      });
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "a permanently suspended process leaves the design quiescent");
  require(
      interpreter.signal_value(signal).to_msb_string() == "0",
      "operations after a permanent wait must remain unreachable");
  require(
      points.size() == 1
          && points.front().kind
              == ExecutionPointKind::process_suspend
          && points.front().instruction == 0,
      "a permanent wait remains debugger-visible as process suspension");
}

void test_simir_update_coalescing() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"top.q", PackedLogic4::from_msb_string("X")});
  Process process;
  process.id = 0;
  process.name = "two_nbas";
  process.register_count = 2;
  process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("0")});
  process.operations.emplace_back(WriteUpdate{signal, 0});
  process.operations.emplace_back(
      LoadConstant{1, PackedLogic4::from_msb_string("1")});
  process.operations.emplace_back(WriteUpdate{signal, 1});
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));

  std::vector<std::string> changes;
  interpreter.set_signal_change_hook(
      [&](SignalId, const PackedLogic4& value, SimulationTick) {
        changes.push_back(value.to_msb_string());
      });
  const auto result = interpreter.run();
  require(result.status == RunStatus::completed, "coalesced run completes");
  require(
      changes == std::vector<std::string>{"1"},
      "one update phase must publish only the final value per signal");

  Interpreter ordered;
  const auto cross_process = ordered.add_signal(
      {"top.cross_process", PackedLogic4::from_msb_string("X")});
  const auto zero_delay = ordered.add_signal(
      {"top.zero_delay", PackedLogic4::from_msb_string("X")});
  const auto equal_deadline = ordered.add_signal(
      {"top.equal_deadline", PackedLogic4::from_msb_string("X")});
  const auto whole_then_slice = ordered.add_signal(
      {"top.whole_then_slice", PackedLogic4::from_msb_string("XXXX")});
  const auto slice_then_whole = ordered.add_signal(
      {"top.slice_then_whole", PackedLogic4::from_msb_string("XXXX")});

  const auto add_writer =
      [&](const ProcessId id,
          const std::string_view name,
          const PackedLogic4& value,
          const Operation& write) {
        Process writer;
        writer.id = id;
        writer.name = std::string{name};
        writer.register_count = 1;
        writer.operations = {
            LoadConstant{0, value},
            write,
            Halt{}};
        (void)ordered.add_process(std::move(writer));
      };
  add_writer(
      0,
      "cross_process_first",
      PackedLogic4::from_msb_string("0"),
      WriteUpdate{cross_process, 0});
  add_writer(
      1,
      "cross_process_last",
      PackedLogic4::from_msb_string("1"),
      WriteUpdate{cross_process, 0});

  Process zero_writer;
  zero_writer.id = 2;
  zero_writer.name = "zero_delay_order";
  zero_writer.register_count = 2;
  zero_writer.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      WriteUpdate{zero_delay, 0},
      LoadConstant{1, PackedLogic4::from_msb_string("1")},
      WriteAfter{zero_delay, 1, 0},
      Halt{}};
  (void)ordered.add_process(std::move(zero_writer));

  add_writer(
      3,
      "equal_deadline_first",
      PackedLogic4::from_msb_string("0"),
      WriteAfter{equal_deadline, 0, 5});
  add_writer(
      4,
      "equal_deadline_last",
      PackedLogic4::from_msb_string("1"),
      WriteAfter{equal_deadline, 0, 5});
  add_writer(
      5,
      "whole_before_slice",
      PackedLogic4::from_msb_string("1010"),
      WriteUpdate{whole_then_slice, 0});
  add_writer(
      6,
      "slice_after_whole",
      PackedLogic4::from_msb_string("11"),
      WriteUpdateSlice{whole_then_slice, 0, 1});
  add_writer(
      7,
      "slice_before_whole",
      PackedLogic4::from_msb_string("11"),
      WriteUpdateSlice{slice_then_whole, 0, 1});
  add_writer(
      8,
      "whole_after_slice",
      PackedLogic4::from_msb_string("1010"),
      WriteUpdate{slice_then_whole, 0});

  struct OrderedChange {
    SignalId signal{};
    std::string value;
    SimulationTick time{};
  };
  std::vector<OrderedChange> ordered_changes;
  ordered.set_signal_change_hook(
      [&](const SignalId changed,
          const PackedLogic4& value,
          const SimulationTick time) {
        ordered_changes.push_back(
            {changed, value.to_msb_string(), time});
      });
  const auto ordered_result = ordered.run();
  require(
      ordered_result.status == RunStatus::completed
          && ordered_result.time == 5,
      "ordered NBA scenarios complete through their last deadline");
  require(
      ordered.signal_value(cross_process).to_msb_string() == "1",
      "stable process order gives the later same-slot NBA precedence");
  require(
      ordered.signal_value(zero_delay).to_msb_string() == "1",
      "a zero-delay NBA joins the current update slot after an immediate "
      "NBA from the same process");
  require(
      ordered.signal_value(equal_deadline).to_msb_string() == "1",
      "equal future deadlines retain stable process ordering");
  require(
      ordered.signal_value(whole_then_slice).to_msb_string() == "1110",
      "a later partial NBA overrides its overlapping whole-value bits");
  require(
      ordered.signal_value(slice_then_whole).to_msb_string() == "1010",
      "a later whole-value NBA overrides an earlier partial assignment");
  for (const auto target :
       {cross_process,
        zero_delay,
        equal_deadline,
        whole_then_slice,
        slice_then_whole}) {
    require(
        std::ranges::count_if(
            ordered_changes,
            [&](const OrderedChange& change) {
              return change.signal == target;
            })
            == 1,
        "each coalesced target publishes exactly one committed change");
  }
  require(
      std::ranges::any_of(
          ordered_changes,
          [&](const OrderedChange& change) {
            return change.signal == equal_deadline
                && change.time == 5
                && change.value == "1";
          }),
      "the equal-deadline winner commits at the requested future time");
}

void test_mixed_signal_id_alignment();
void test_force_release_word_boundary();

void test_resolved_driver_slots() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  ScopedEnvironment direct_word_commit_enabled{
      "FSIM_DISABLE_DIRECT_WORD_COMMIT", std::nullopt};

  Interpreter interpreter;
  const auto whole = interpreter.add_signal(
      {
          "top.whole",
          PackedLogic4::from_msb_string("ZZZZ"),
          ResolutionKind::sv_wire});
  const auto sliced = interpreter.add_signal(
      {
          "top.sliced",
          PackedLogic4::from_msb_string("ZZZZ"),
          ResolutionKind::sv_wire});
  const auto standard_logic = interpreter.add_signal(
      {
          "top.standard_logic",
          PackedLogic4::from_msb_string("X"),
          ResolutionKind::std_logic});
  const auto strength_resolved = interpreter.add_signal(
      {
          "top.strength_resolved",
          PackedLogic4::from_msb_string("Z"),
          ResolutionKind::sv_wire});
  const auto switch_pair = interpreter.add_signal(
      {
          "top.switch_pair",
          PackedLogic4::from_msb_string("ZZ"),
          ResolutionKind::sv_wire});
  const auto switch_triplet = interpreter.add_signal(
      {
          "top.switch_triplet",
          PackedLogic4::from_msb_string("ZZZ"),
          ResolutionKind::sv_wire});

  Process first;
  first.id = 0;
  first.name = "first_driver";
  first.register_count = 4;
  first.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0000")},
      WriteUpdate{whole, 0},
      LoadConstant{1, PackedLogic4::from_msb_string("ZZZZ")},
      WriteAfter{whole, 1, 5},
      LoadConstant{2, PackedLogic4::from_msb_string("10")},
      WriteUpdateSlice{sliced, 2, 0},
      LoadConstant{3, PackedLogic4::from_msb_string("0")},
      WriteUpdate{standard_logic, 3},
      Halt{}};
  (void)interpreter.add_process(std::move(first));

  Process second;
  second.id = 1;
  second.name = "second_driver";
  second.register_count = 5;
  second.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1111")},
      WriteUpdate{whole, 0},
      LoadConstant{1, PackedLogic4::from_msb_string("0011")},
      WriteAfter{whole, 1, 3},
      LoadConstant{2, PackedLogic4::from_msb_string("11")},
      WriteUpdateSlice{sliced, 2, 2},
      LoadConstant{3, PackedLogic4::from_msb_string("01")},
      WriteAfterSlice{sliced, 3, 0, 4},
      LoadConstant{4, PackedLogic4::from_msb_string("Z")},
      WriteUpdate{standard_logic, 4},
      Halt{}};
  (void)interpreter.add_process(std::move(second));

  Process weak_zero;
  weak_zero.id = 2;
  weak_zero.name = "weak_zero_driver";
  weak_zero.register_count = 1;
  weak_zero.drive_strength = {
      StrengthRank::weak, StrengthRank::weak};
  weak_zero.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      WriteUpdate{strength_resolved, 0},
      Halt{}};
  (void)interpreter.add_process(std::move(weak_zero));

  Process strong_one;
  strong_one.id = 3;
  strong_one.name = "strong_one_driver";
  strong_one.register_count = 1;
  strong_one.drive_strength = {
      StrengthRank::strong, StrengthRank::strong};
  strong_one.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteUpdate{strength_resolved, 0},
      Halt{}};
  (void)interpreter.add_process(std::move(strong_one));

  const auto rejects_process = [&](Process process) {
    try {
      (void)interpreter.add_process(std::move(process));
    } catch (const std::invalid_argument&) {
      return true;
    }
    return false;
  };
  Process invalid_strength;
  invalid_strength.id = 4;
  invalid_strength.name = "invalid_strength";
  invalid_strength.drive_strength.zero =
      static_cast<StrengthRank>(255);
  invalid_strength.operations = {Halt{}};
  require(
      rejects_process(std::move(invalid_strength)),
      "runtime construction rejects an invalid strength rank");
  Process incomplete_switch;
  incomplete_switch.id = 4;
  incomplete_switch.name = "incomplete_switch";
  incomplete_switch.switch_bidirectional = true;
  incomplete_switch.operations = {Halt{}};
  require(
      rejects_process(std::move(incomplete_switch)),
      "runtime construction rejects an incomplete transmission edge");
  Process incompatible_switch;
  incompatible_switch.id = 4;
  incompatible_switch.name = "incompatible_switch";
  incompatible_switch.switch_source = switch_pair;
  incompatible_switch.switch_target = switch_triplet;
  incompatible_switch.switch_bidirectional = true;
  incompatible_switch.operations = {Halt{}};
  require(
      rejects_process(std::move(incompatible_switch)),
      "runtime construction rejects incompatible transmission widths");
  Process invalid_switch_region;
  invalid_switch_region.id = 4;
  invalid_switch_region.name = "invalid_switch_region";
  invalid_switch_region.switch_source = switch_pair;
  invalid_switch_region.switch_target = switch_triplet;
  invalid_switch_region.switch_source_offset = 1;
  invalid_switch_region.switch_target_offset = 0;
  invalid_switch_region.switch_width = 2;
  invalid_switch_region.switch_bidirectional = true;
  invalid_switch_region.operations = { Halt { } };
  require(
      rejects_process(std::move(invalid_switch_region)),
      "runtime construction rejects an out-of-range transmission region");

  struct Change {
    SignalId signal{};
    std::string value;
    SimulationTick time{};
  };
  std::vector<Change> changes;
  interpreter.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        changes.push_back(
            {signal, value.to_msb_string(), time});
      });
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 5,
      "resolved driver simulation reaches its final transaction");
  require(
      interpreter.signal_value(whole).to_msb_string() == "0011",
      "a released wire driver exposes the other process slot");
  require(
      interpreter.driver_value(0, whole).to_msb_string() == "ZZZZ"
          && interpreter.driver_value(1, whole).to_msb_string()
              == "0011",
      "whole-signal process driver slots retain independent values");
  require(
      interpreter.signal_value(sliced).to_msb_string() == "11XX",
      "partial driver slots resolve disjoint and overlapping packed bits");
  require(
      interpreter.signal_value(strength_resolved).to_msb_string() == "1",
      "the strongest opposing Verilog driver determines the visible value");
  require(
      interpreter.driver_value(0, sliced).to_msb_string() == "ZZ10"
          && interpreter.driver_value(1, sliced).to_msb_string()
              == "1101",
      "slice writes update only the issuing process's full driver slot");
  require(
      interpreter.signal_value(standard_logic).to_msb_string() == "0",
      "the supported std_logic 0/1/X/Z subset uses standard resolution");

  const auto whole_changes =
      [&] {
        std::vector<std::pair<std::string, SimulationTick>> observed;
        for (const auto& change : changes) {
          if (change.signal == whole) {
            observed.emplace_back(change.value, change.time);
          }
        }
        return observed;
      }();
  require(
      whole_changes
          == std::vector<std::pair<std::string, SimulationTick>>{
              {"XXXX", 0}, {"00XX", 3}, {"0011", 5}},
      "NBA and future driver updates resolve once per destination slot");

  Interpreter forced;
  const auto forced_signal = forced.add_signal(
      {
          "top.forced",
          PackedLogic4::from_msb_string("Z"),
          ResolutionKind::sv_wire});
  Process forced_first;
  forced_first.id = 0;
  forced_first.name = "forced_first";
  forced_first.register_count = 1;
  forced_first.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      WriteAfter{forced_signal, 0, 2},
      Halt{}};
  (void)forced.add_process(std::move(forced_first));
  Process forced_second;
  forced_second.id = 1;
  forced_second.name = "forced_second";
  forced_second.register_count = 1;
  forced_second.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("Z")},
      WriteAfter{forced_signal, 0, 2},
      Halt{}};
  (void)forced.add_process(std::move(forced_second));
  forced.force_signal(
      forced_signal,
      PackedLogic4::from_msb_string("1"));
  const auto forced_result = forced.run();
  require(
      forced_result.status == RunStatus::completed
          && forced.signal_value(forced_signal).to_msb_string() == "1",
      "a force masks resolved driver activity through completion");
  require(
      forced.driver_value(0, forced_signal).to_msb_string() == "0"
          && forced.driver_value(1, forced_signal).to_msb_string() == "Z",
      "resolved drivers continue updating beneath a force");
  forced.release_signal(forced_signal);
  require(
      forced.signal_value(forced_signal).to_msb_string() == "0",
      "force release publishes the latest resolved underlying value");

  Interpreter regional;
  const auto regional_source = regional.add_signal(
      { "top.regional_source",
          PackedLogic4::from_msb_string("ZZZZZZZZ"),
          ResolutionKind::sv_wire });
  const auto regional_target = regional.add_signal(
      { "top.regional_target",
          PackedLogic4::from_msb_string("ZZZZZZZZ"),
          ResolutionKind::sv_wire });
  const auto regional_self = regional.add_signal(
      { "top.regional_self",
          PackedLogic4::from_msb_string("ZZZZZZZZ"),
          ResolutionKind::sv_wire });
  Process regional_source_driver;
  regional_source_driver.id = 0;
  regional_source_driver.name = "regional_source_driver";
  regional_source_driver.register_count = 1;
  regional_source_driver.operations = {
      LoadConstant { 0, PackedLogic4::from_msb_string("ZZZZ0011") },
      WriteUpdate { regional_source, 0 },
      Halt { }
  };
  (void)regional.add_process(std::move(regional_source_driver));
  Process regional_target_driver;
  regional_target_driver.id = 1;
  regional_target_driver.name = "regional_target_driver";
  regional_target_driver.register_count = 1;
  regional_target_driver.operations = {
      LoadConstant { 0, PackedLogic4::from_msb_string("ZZZZ1010") },
      WriteUpdate { regional_target, 0 },
      Halt { }
  };
  (void)regional.add_process(std::move(regional_target_driver));
  Process regional_connection;
  regional_connection.id = 2;
  regional_connection.name = "regional_connection";
  regional_connection.switch_source = regional_source;
  regional_connection.switch_target = regional_target;
  regional_connection.switch_source_offset = 4;
  regional_connection.switch_target_offset = 0;
  regional_connection.switch_width = 4;
  regional_connection.switch_bidirectional = true;
  regional_connection.initialize = false;
  regional_connection.operations = { Halt { } };
  (void)regional.add_process(std::move(regional_connection));
  Process regional_self_driver;
  regional_self_driver.id = 3;
  regional_self_driver.name = "regional_self_driver";
  regional_self_driver.register_count = 1;
  regional_self_driver.operations = {
      LoadConstant { 0, PackedLogic4::from_msb_string("1100ZZZZ") },
      WriteUpdate { regional_self, 0 },
      Halt { }
  };
  (void)regional.add_process(std::move(regional_self_driver));
  Process regional_self_connection;
  regional_self_connection.id = 4;
  regional_self_connection.name = "regional_self_connection";
  regional_self_connection.switch_source = regional_self;
  regional_self_connection.switch_target = regional_self;
  regional_self_connection.switch_source_offset = 0;
  regional_self_connection.switch_target_offset = 4;
  regional_self_connection.switch_width = 4;
  regional_self_connection.switch_bidirectional = true;
  regional_self_connection.initialize = false;
  regional_self_connection.operations = { Halt { } };
  (void)regional.add_process(std::move(regional_self_connection));
  const auto regional_result = regional.run();
  require(
      regional_result.status == RunStatus::completed
          && regional.signal_value(regional_source).to_msb_string()
              == "10100011"
          && regional.signal_value(regional_target).to_msb_string()
              == "ZZZZ1010",
      "a selected transmission region propagates resolved drivers in both directions");
  require(
      regional.signal_value(regional_self).to_msb_string()
          == "11001100",
      "disjoint selected regions of one net form a bidirectional physical alias");

  Interpreter promoted;
  const auto promoted_signal = promoted.add_signal(
      { "top.promoted",
          PackedLogic4::from_msb_string("Z"),
          ResolutionKind::sv_wire });
  const auto add_promoted_driver = [&](const ProcessId id,
                                       const std::string_view name,
                                       const std::string_view value) {
    Process process;
    process.id = id;
    process.name = std::string { name };
    process.register_count = 1U;
    process.operations = {
        LoadConstant { 0U, PackedLogic4::from_msb_string(value) },
        WriteUpdate { promoted_signal, 0U },
        Halt { },
    };
    (void)promoted.add_process(std::move(process));
  };
  add_promoted_driver(0U, "inline_driver", "0");
  add_promoted_driver(1U, "overflow_driver", "1");
  const auto promoted_result = promoted.run();
  require(
      promoted_result.status == RunStatus::completed,
      "the inline-to-overflow promotion simulation completes");
  require(
      promoted.signal_value(promoted_signal).to_msb_string() == "X",
      "two opposing drivers resolve after inline-to-overflow promotion");
  require(
      promoted.driver_value(0U, promoted_signal).to_msb_string() == "0",
      "overflow promotion retains the inline process driver value");
  require(
      promoted.driver_value(1U, promoted_signal).to_msb_string() == "1",
      "overflow promotion retains the second process driver value");

  class DelayedDriverWriteExecutor final : public ProcessExecutor {
  public:
    DelayedDriverWriteExecutor(
        const SignalId signal, const Logic4Word value)
        : signal_ { signal }
        , value_ { value }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex) override
    {
      context.write_update_word(signal_, value_);
      ProcessResumeResult result { 0U, 1U };
      result.external.kind = ExternalSuspendKind::halt;
      return result;
    }

  private:
    SignalId signal_ { };
    Logic4Word value_ { };
  };

  Interpreter user_first;
  const auto user_first_signal = user_first.add_signal(
      { "top.user_first",
          PackedLogic4::from_msb_string("Z"),
          ResolutionKind::sv_user_first });
  std::array<SignalId, 4U> user_first_triggers { };
  for (std::size_t index = 0; index < user_first_triggers.size(); ++index) {
    user_first_triggers[index] = user_first.add_signal(
        { "top.user_first_trigger" + std::to_string(index),
            PackedLogic4::from_msb_string("0") });
  }
  constexpr std::array<Logic4Word, 4U> user_first_values {
      Logic4Word { 1U, 0U, 0U },
      Logic4Word { 1U, 1U, 0U },
      Logic4Word { 1U, 1U, 1U },
      Logic4Word { 1U, 0U, 1U },
  };
  std::array<ProcessId, 4U> user_first_processes { };
  for (std::size_t index = 0; index < user_first_processes.size(); ++index) {
    const auto id = static_cast<ProcessId>(index);
    Process process;
    process.id = id;
    process.name = "user_first_process" + std::to_string(id);
    process.static_sensitivity.push_back(
        { user_first_triggers[index], EdgeKind::any });
    process.operations = { WaitSensitivity { }, Halt { } };
    process.initialize = false;
    user_first_processes[index] =
        user_first.add_process(std::move(process));
    user_first.set_process_executor(
        user_first_processes[index],
        std::make_unique<DelayedDriverWriteExecutor>(
            user_first_signal, user_first_values[index]));
  }
  for (std::size_t reverse_index = 0;
      reverse_index < user_first_triggers.size(); ++reverse_index) {
    const auto index = user_first_triggers.size() - 1U - reverse_index;
    const auto time = static_cast<SimulationTick>(reverse_index + 1U);
    user_first.schedule_signal_at(
        user_first_triggers[index],
        PackedLogic4::from_msb_string("1"), time, 0U);
  }
  const auto user_first_result = user_first.run();
  require(
      user_first_result.status == RunStatus::completed
          && user_first_result.time == 4U,
      "the four-driver reverse slot creation simulation completes");
  require(
      user_first.driver_value(3U, user_first_signal).to_msb_string() == "Z"
          && user_first.driver_value(2U, user_first_signal).to_msb_string()
              == "X"
          && user_first.driver_value(1U, user_first_signal).to_msb_string()
              == "1"
          && user_first.driver_value(0U, user_first_signal).to_msb_string()
              == "0",
      "four lazy driver slots retain values after reverse ProcessId insertion");
  require(
      user_first.signal_value(user_first_signal).to_msb_string() == "0",
      "sv_user_first merges multiple overflow entries by ProcessId");

  Interpreter grown_route;
  const auto direct_signal = grown_route.add_signal(
      { "top.direct_after_growth",
          PackedLogic4::from_msb_string("Z"),
          ResolutionKind::sv_wire });
  Process direct_writer;
  direct_writer.id = 0U;
  direct_writer.name = "direct_writer";
  direct_writer.driver_regions.push_back(
      { direct_signal, 0U, 0U, true });
  direct_writer.operations = { Halt { } };
  const auto direct_writer_id
      = grown_route.add_process(std::move(direct_writer));
  for (std::size_t index = 0U; index < 256U; ++index) {
    (void)grown_route.add_signal(
        { "top.growth" + std::to_string(index),
            PackedLogic4::from_msb_string("0") });
  }
  grown_route.set_process_executor(
      direct_writer_id,
      std::make_unique<DelayedDriverWriteExecutor>(
          direct_signal, Logic4Word { 1U, 1U, 0U }));
  const auto grown_route_result = grown_route.run();
  require(
      grown_route_result.status == RunStatus::completed,
      "the direct single-driver growth simulation completes");
  require(
      grown_route.driver_value(0U, direct_signal).to_msb_string() == "1",
      "the direct single-driver route updates its process slot after growth");
  require(
      grown_route.signal_value(direct_signal).to_msb_string() == "1",
      "the direct single-driver publication matches its slot after growth");

  Interpreter lazy_mirror;
  const auto lazy_signal = lazy_mirror.add_signal(
      { "top.lazy_mirror",
          PackedLogic4::from_msb_string("ZZZZ"),
          ResolutionKind::sv_wire });
  const auto update_trigger = lazy_mirror.add_signal(
      { "top.lazy_mirror_trigger",
          PackedLogic4::from_msb_string("0") });

  Process mirror_reader;
  mirror_reader.id = 0U;
  mirror_reader.name = "lazy_mirror_reader";
  mirror_reader.register_count = 2U;
  mirror_reader.debug_locals.resize(2U);
  mirror_reader.debug_locals[0U].name = "previous";
  mirror_reader.debug_locals[0U].type_name = "logic";
  mirror_reader.debug_locals[0U].register_id = 0U;
  mirror_reader.debug_locals[0U].width = 4U;
  mirror_reader.debug_locals[1U].name = "published";
  mirror_reader.debug_locals[1U].type_name = "logic";
  mirror_reader.debug_locals[1U].register_id = 1U;
  mirror_reader.debug_locals[1U].width = 4U;
  mirror_reader.static_sensitivity.push_back(
      { lazy_signal, EdgeKind::any });
  mirror_reader.operations = {
      WaitSensitivity { },
      SignalLastValue { 0U, lazy_signal },
      ReadSignal { 1U, lazy_signal },
      Halt { },
  };
  const auto mirror_reader_id
      = lazy_mirror.add_process(std::move(mirror_reader));

  class LazyMirrorNativeWriter final : public ProcessExecutor {
  public:
    LazyMirrorNativeWriter(
        const SignalId signal, const ProcessId process)
        : signal_ { signal }
        , process_ { process }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
      if (resumes_++ == 0U) {
        require(start == 0U,
                "lazy-mirror native writer starts at its wait boundary");
        const std::array slots {
            ProcessUpdateSlotView {
                signal_, 4U, 1U, &slot_active_, &slot_aval_,
                &slot_bval_, &slot_mask_ }
        };
        std::array<std::uint64_t, 1U> active_words { 1U };
        const std::array batches {
            ProcessUpdateSlotBatch { process_, slots, active_words }
        };
        require(
            context.write_validated_update_slot_batches(batches),
            "lazy-mirror full slot batch is accepted");
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::wait_sensitivity;
        return result;
      }

      require(start == 1U,
              "lazy-mirror native writer resumes after its trigger wait");
      ScopedEnvironment disable_direct_word_commit {
          "FSIM_DISABLE_DIRECT_WORD_COMMIT", std::string { "1" } };
      const std::array updates {
          ProcessUpdateWord {
              signal_, Logic4Word { 2U, 0b11U, 0U }, 1U, true }
      };
      context.write_validated_update_words(updates);
      ProcessResumeResult result { 1U, 2U };
      result.external.kind = ExternalSuspendKind::halt;
      return result;
    }

  private:
    SignalId signal_ { };
    ProcessId process_ { };
    std::uint32_t slot_active_ { 1U };
    std::uint64_t slot_aval_ { 0b1010U };
    std::uint64_t slot_bval_ { };
    std::uint64_t slot_mask_ { 0b1111U };
    std::uint32_t resumes_ { };
  };

  Process native_writer;
  native_writer.id = 1U;
  native_writer.name = "lazy_mirror_native_writer";
  native_writer.static_sensitivity.push_back(
      { update_trigger, EdgeKind::any });
  native_writer.driver_regions.push_back(
      { lazy_signal, 0U, 0U, true });
  native_writer.operations = { WaitSensitivity { }, Halt { } };
  const auto native_writer_id
      = lazy_mirror.add_process(std::move(native_writer));
  lazy_mirror.set_process_executor(
      native_writer_id,
      std::make_unique<LazyMirrorNativeWriter>(
          lazy_signal, native_writer_id));

  const auto native_result = lazy_mirror.run();
  require(native_result.status == RunStatus::completed,
          "eligible narrow native signal publication completes");
  require(
      lazy_mirror.read_debug_local(mirror_reader_id, 0U).to_msb_string()
              == "ZZZZ"
          && lazy_mirror.read_debug_local(mirror_reader_id, 1U)
                  .to_msb_string()
              == "1010",
      "generic SignalLastValue and ReadSignal materialize previous and published values");
  require(
      lazy_mirror.signal_value(lazy_signal).to_msb_string() == "1010"
          && lazy_mirror.stored_signal_value(lazy_signal).to_msb_string()
              == "1010"
          && lazy_mirror.driver_value(native_writer_id, lazy_signal)
                  .to_msb_string()
              == "1010",
      "generic signal, stored, and driver queries see the materialized native word");

  lazy_mirror.deposit_signal(
      update_trigger, PackedLogic4::from_msb_string("1"));
  const auto partial_result = lazy_mirror.run();
  require(partial_result.status == RunStatus::completed,
          "the native partial update after materialization completes");
  require(
      lazy_mirror.signal_value(lazy_signal).to_msb_string() == "1110"
          && lazy_mirror.stored_signal_value(lazy_signal).to_msb_string()
              == "1110"
          && lazy_mirror.driver_value(native_writer_id, lazy_signal)
                  .to_msb_string()
              == "1110",
      "a later partial native update remains coherent across all signal views");

  lazy_mirror.force_signal(
      lazy_signal, PackedLogic4::from_msb_string("0001"));
  require(
      lazy_mirror.signal_value(lazy_signal).to_msb_string() == "0001"
          && lazy_mirror.stored_signal_value(lazy_signal).to_msb_string()
              == "1110"
          && lazy_mirror.driver_value(native_writer_id, lazy_signal)
                  .to_msb_string()
              == "1110",
      "force changes the published value while retaining driven and driver values");
  lazy_mirror.release_signal(lazy_signal);
  require(
      lazy_mirror.signal_value(lazy_signal).to_msb_string() == "1110",
      "release republishes the latest materialized driver value");

  Interpreter stale_route;
  const auto stale_signal = stale_route.add_signal(
      { "top.stale_route",
          PackedLogic4::from_msb_string("ZZZZ"),
          ResolutionKind::sv_wire });
  const auto stale_trigger = stale_route.add_signal(
      { "top.stale_route_trigger", PackedLogic4::from_msb_string("0") });

  class NativeThenFallbackWriter final : public ProcessExecutor {
  public:
    NativeThenFallbackWriter(
        const SignalId signal, const ProcessId process)
        : signal_ { signal }
        , process_ { process }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
      if (resumes_++ == 0U) {
        require(start == 0U, "stale-route writer starts at its wait");
        const std::array slots {
            ProcessUpdateSlotView {
                signal_, 4U, 1U, &slot_active_, &slot_aval_,
                &slot_bval_, &slot_mask_ }
        };
        std::array<std::uint64_t, 1U> active_words { 1U };
        const std::array batches {
            ProcessUpdateSlotBatch { process_, slots, active_words }
        };
        require(
            context.write_validated_update_slot_batches(batches),
            "stale-route full slot batch is accepted");
        ProcessResumeResult result { 0U, 1U };
        result.external.kind = ExternalSuspendKind::wait_sensitivity;
        return result;
      }

      require(start == 1U, "stale-route writer resumes after its trigger");
      ScopedEnvironment disable_direct_word_commit {
          "FSIM_DISABLE_DIRECT_WORD_COMMIT", std::string { "1" } };
      const std::array updates {
          ProcessUpdateWord {
              signal_, Logic4Word { 2U, 0b11U, 0U }, 1U, true }
      };
      context.write_validated_update_words(updates);
      ProcessResumeResult result { 1U, 2U };
      result.external.kind = ExternalSuspendKind::halt;
      return result;
    }

  private:
    SignalId signal_ { };
    ProcessId process_ { };
    std::uint32_t slot_active_ { 1U };
    std::uint64_t slot_aval_ { 0b1010U };
    std::uint64_t slot_bval_ { };
    std::uint64_t slot_mask_ { 0b1111U };
    std::uint32_t resumes_ { };
  };

  Process stale_writer;
  stale_writer.id = 0U;
  stale_writer.name = "stale_route_writer";
  stale_writer.driver_regions.push_back(
      { stale_signal, 0U, 0U, true });
  stale_writer.static_sensitivity.push_back(
      { stale_trigger, EdgeKind::any });
  stale_writer.operations = { WaitSensitivity { }, Halt { } };
  const auto stale_writer_id = stale_route.add_process(std::move(stale_writer));
  stale_route.set_process_executor(
      stale_writer_id,
      std::make_unique<NativeThenFallbackWriter>(
          stale_signal, stale_writer_id));
  stale_route.schedule_signal_at(
      stale_trigger, PackedLogic4::from_msb_string("1"), 1U, 0U);
  const auto stale_result = stale_route.run();
  require(
      stale_result.status == RunStatus::completed,
      "native publication followed by forced fallback completes");
  require(
      stale_route.signal_value(stale_signal).to_msb_string() == "1110"
          && stale_route.stored_signal_value(stale_signal).to_msb_string()
              == "1110"
          && stale_route.driver_value(stale_writer_id, stale_signal)
                  .to_msb_string()
              == "1110",
      "FSIM_DISABLE_DIRECT_WORD_COMMIT=1 partial writes preserve the current, stored, and driver values after native publication");
  test_mixed_signal_id_alignment();
  test_force_release_word_boundary();
}

void test_mixed_signal_id_alignment() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto initial_time = encode_systemverilog_scalar_payload(
      SystemVerilogScalarValue::time(7U));
  const auto published_time = SystemVerilogScalarValue::time(19U);
  const auto published_time_payload =
      encode_systemverilog_scalar_payload(published_time);
  require(
      initial_time && published_time_payload,
      "mixed signal scalar payloads encode");

  Interpreter interpreter;
  const auto narrow = interpreter.add_signal(
      { "top.mixed.narrow", PackedLogic4::from_msb_string("0") });
  const auto charged = interpreter.add_signal(Signal{
      "top.mixed.charged", PackedLogic4::from_msb_string("Z"),
      ResolutionKind::sv_wire, ValueKind::logic4, std::nullopt, {},
      StrengthRank::weak });
  const auto logic9 = interpreter.add_signal(Signal{
      "top.mixed.logic9", PackedLogic4::from_logic9_msb_string("U"),
      ResolutionKind::none, ValueKind::logic9 });
  const auto scalar = interpreter.add_signal(Signal{
      "top.mixed.scalar", initial_time.value, ResolutionKind::none,
      ValueKind::logic4, std::nullopt, {}, std::nullopt, std::nullopt,
      SystemVerilogScalarKind::Time });
  Signal event_signal{
      "top.mixed.event", PackedLogic4::from_msb_string("0") };
  event_signal.event_variable = true;
  const auto named_event =
      interpreter.add_signal(std::move(event_signal));
  const auto wide_value = PackedLogic4{ 129U, Logic4::one };
  const auto wide = interpreter.add_signal(
      { "top.mixed.wide", PackedLogic4{ 129U, Logic4::zero } });
  const auto event_seen = interpreter.add_signal(
      { "top.mixed.event_seen", PackedLogic4::from_msb_string("0") });
  require(
      narrow == 0U && charged == 1U && logic9 == 2U && scalar == 3U
          && named_event == 4U && wide == 5U && event_seen == 6U,
      "mixed signal IDs preserve their append order across metadata kinds");

  Process waiter;
  waiter.id = 0U;
  waiter.name = "mixed_signal_event_waiter";
  waiter.register_count = 1U;
  waiter.operations = {
      WaitOn{ { named_event } },
      LoadConstant{ 0U, PackedLogic4::from_msb_string("1") },
      WriteBlocking{ event_seen, 0U },
      Halt{ }};
  (void)interpreter.add_process(std::move(waiter));

  Process publisher;
  publisher.id = 1U;
  publisher.name = "mixed_signal_publisher";
  publisher.register_count = 5U;
  publisher.register_value_kinds.assign(
      publisher.register_count, ValueKind::logic4);
  publisher.register_value_kinds[0] = ValueKind::logic9;
  publisher.operations = {
      LoadConstant{ 0U, PackedLogic4::from_logic9_msb_string("H") },
      WriteBlocking{ logic9, 0U },
      LoadConstant{ 1U, PackedLogic4::from_msb_string("1") },
      WriteBlocking{ narrow, 1U },
      LoadConstant{ 2U, published_time_payload.value },
      WriteBlocking{ scalar, 2U },
      LoadConstant{ 3U, wide_value },
      WriteBlocking{ wide, 3U },
      LoadConstant{ 4U, PackedLogic4::from_msb_string("1") },
      WriteBlocking{ named_event, 4U },
      Halt{ }};
  (void)interpreter.add_process(std::move(publisher));

  Process weak_driver;
  weak_driver.id = 2U;
  weak_driver.name = "mixed_signal_weak_driver";
  weak_driver.register_count = 1U;
  weak_driver.drive_strength = { StrengthRank::weak, StrengthRank::weak };
  weak_driver.operations = {
      LoadConstant{ 0U, PackedLogic4::from_msb_string("0") },
      WriteUpdate{ charged, 0U },
      Halt{ }};
  const auto weak_driver_id =
      interpreter.add_process(std::move(weak_driver));

  Process strong_driver;
  strong_driver.id = 3U;
  strong_driver.name = "mixed_signal_strong_driver";
  strong_driver.register_count = 1U;
  strong_driver.drive_strength = {
      StrengthRank::strong, StrengthRank::strong };
  strong_driver.operations = {
      LoadConstant{ 0U, PackedLogic4::from_msb_string("1") },
      WriteUpdate{ charged, 0U },
      Halt{ }};
  const auto strong_driver_id =
      interpreter.add_process(std::move(strong_driver));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "mixed signal publication and event wait complete");
  require(
      interpreter.signal_value(narrow).to_msb_string() == "1"
          && interpreter.signal_value(logic9).is_logic9()
          && interpreter.signal_value(logic9).get_logic9(0U) == Logic9::h
          && interpreter.scalar_signal_value(scalar) == published_time
          && interpreter.signal_value(wide) == wide_value
          && interpreter.signal_value(named_event).to_msb_string() == "1"
          && interpreter.signal_value(event_seen).to_msb_string() == "1",
      "mixed signal values remain aligned through publication and named-event resumption");
  require(
      interpreter.signal_value(charged).to_msb_string() == "1"
          && interpreter.driver_value(weak_driver_id, charged)
                 .to_msb_string() == "0"
          && interpreter.driver_value(strong_driver_id, charged)
                 .to_msb_string() == "1",
      "charged signal resolution keeps the weak and strong driver slots aligned");

  interpreter.force_signal(charged, PackedLogic4::from_msb_string("0"));
  require(
      interpreter.signal_is_forced(charged)
          && interpreter.signal_value(charged).to_msb_string() == "0"
          && interpreter.stored_signal_value(charged).to_msb_string() == "1",
      "force on a mixed charged signal preserves its resolved stored value");
  interpreter.release_signal(charged);
  require(
      !interpreter.signal_is_forced(charged)
          && interpreter.signal_value(charged).to_msb_string() == "1",
      "release restores the resolved mixed charged signal value");
}

void test_force_release_word_boundary() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal_64 = interpreter.add_signal(
      { "top.force_word_64", PackedLogic4{ 64U, Logic4::zero } });
  const auto signal_65 = interpreter.add_signal(
      { "top.force_word_65", PackedLogic4{ 65U, Logic4::zero } });

  auto driven_64 = PackedLogic4{ 64U, Logic4::zero };
  driven_64.set(0U, Logic4::one);
  auto driven_65 = PackedLogic4{ 65U, Logic4::zero };
  driven_65.set(0U, Logic4::one);
  driven_65.set(63U, Logic4::one);
  const auto forced_64 = PackedLogic4{ 64U, Logic4::one };

  Process writer;
  writer.id = 0U;
  writer.name = "force_word_boundary_writer";
  writer.register_count = 2U;
  writer.operations = {
      WaitFor{ 1U },
      LoadConstant{ 0U, driven_64 },
      WriteBlocking{ signal_64, 0U },
      LoadConstant{ 1U, driven_65 },
      WriteBlocking{ signal_65, 1U },
      Halt{ }};
  const auto writer_id = interpreter.add_process(std::move(writer));

  interpreter.force_signal(signal_64, forced_64);
  interpreter.force_signal_slice(
      signal_65, PackedLogic4::from_msb_string("10"), 63U);

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 1U,
      "64/65-bit forced signals accept an underlying write while running");
  require(
      interpreter.signal_is_forced(signal_64)
          && interpreter.signal_value(signal_64) == forced_64
          && interpreter.stored_signal_value(signal_64) == driven_64
          && interpreter.driver_value(writer_id, signal_64) == driven_64,
      "64-bit full force retains the updated stored and driver words");
  require(
      interpreter.signal_is_forced(signal_65)
          && interpreter.signal_value(signal_65).get(0U) == Logic4::one
          && interpreter.signal_value(signal_65).get(63U) == Logic4::zero
          && interpreter.signal_value(signal_65).get(64U) == Logic4::one
          && interpreter.stored_signal_value(signal_65) == driven_65
          && interpreter.driver_value(writer_id, signal_65) == driven_65,
      "65-bit force across bit 63/64 masks only the selected bits");

  interpreter.release_signal(signal_64);
  interpreter.release_signal_slice(signal_65, 63U, 2U);
  require(
      !interpreter.signal_is_forced(signal_64)
          && interpreter.signal_value(signal_64) == driven_64
          && !interpreter.signal_is_forced(signal_65)
          && interpreter.signal_value(signal_65) == driven_65,
      "64-bit and crossing 65-bit force release republish driven values");
}

void test_simir_expressions_and_edges() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto clock = interpreter.add_signal(
      {"top.clock", PackedLogic4::from_msb_string("0")});
  const auto edge_seen = interpreter.add_signal(
      {"top.edge_seen", PackedLogic4::from_msb_string("0")});
  const auto expression = interpreter.add_signal(
      {"top.expression", PackedLogic4::from_msb_string("0000")});

  Process edge_process;
  edge_process.id = 0;
  edge_process.name = "posedge_observer";
  edge_process.register_count = 1;
  edge_process.static_sensitivity.push_back({clock, EdgeKind::posedge});
  edge_process.operations.emplace_back(WaitSensitivity{});
  edge_process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("1")});
  edge_process.operations.emplace_back(WriteBlocking{edge_seen, 0});
  edge_process.operations.emplace_back(Jump{0});
  (void)interpreter.add_process(std::move(edge_process));

  Process expression_process;
  expression_process.id = 1;
  expression_process.name = "expression";
  expression_process.register_count = 4;
  expression_process.operations.emplace_back(
      LoadConstant{0, PackedLogic4::from_msb_string("0011")});
  expression_process.operations.emplace_back(
      LoadConstant{1, PackedLogic4::from_msb_string("0001")});
  expression_process.operations.emplace_back(
      Binary{BinaryOperator::add_unsigned, 2, 0, 1});
  expression_process.operations.emplace_back(UnaryNot{3, 2});
  expression_process.operations.emplace_back(WriteBlocking{expression, 3});
  expression_process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(expression_process));

  interpreter.schedule_signal_at(
      clock, PackedLogic4::from_msb_string("1"), 5, 0);
  interpreter.schedule_signal_at(
      clock, PackedLogic4::from_msb_string("0"), 10, 0);
  const auto result = interpreter.run();
  require(result.status == RunStatus::completed,
          "edge-sensitive SimIR run must complete");
  require(interpreter.signal_value(edge_seen).to_msb_string() == "1",
          "posedge must activate a waiting process");
  require(interpreter.signal_value(expression).to_msb_string() == "1011",
      "SimIR add and unary-not operations");
}

void test_simir_noninitializing_static_process() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto trigger = interpreter.add_signal(
      {"top.trigger", PackedLogic4::from_msb_string("0")});
  const auto observed = interpreter.add_signal(
      {"top.observed", PackedLogic4::from_msb_string("0")});

  Process process;
  process.id = 0;
  process.name = "dont_initialize";
  process.register_count = 1;
  process.static_sensitivity.push_back({trigger, EdgeKind::any});
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{observed, 0},
      Halt{},
  };
  process.initialize = false;
  (void)interpreter.add_process(std::move(process));
  interpreter.schedule_signal_at(
      trigger, PackedLogic4::from_msb_string("1"), 1, 0);

  const auto before_event = interpreter.run(0);
  require(
      before_event.status == RunStatus::time_limit
          && interpreter.signal_value(observed).to_msb_string() == "0",
      "a noninitializing static process must not execute at time zero");
  const auto after_event = interpreter.run();
  require(
      after_event.status == RunStatus::completed
          && after_event.time == 1
          && interpreter.signal_value(observed).to_msb_string() == "1",
      "a noninitializing static process must wake on its sensitivity");
}

void test_simir_initial_static_wait_activation_order() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto run = [](const bool writer_first) {
    Interpreter interpreter;
    const auto trigger = interpreter.add_signal(
        {"top.trigger", PackedLogic4::from_msb_string("0")});
    const auto observed = interpreter.add_signal(
        {"top.observed", PackedLogic4::from_msb_string("0")});

    const auto add_writer = [&] {
      Process process;
      process.id = static_cast<ProcessId>(
          writer_first ? 0U : 1U);
      process.name = "writer";
      process.register_count = 1U;
      process.operations = {
          LoadConstant {0U, PackedLogic4::from_msb_string("1")},
          WriteBlocking {trigger, 0U},
          Halt { },
      };
      (void)interpreter.add_process(std::move(process));
    };
    const auto add_observer = [&] {
      Process process;
      process.id = static_cast<ProcessId>(
          writer_first ? 1U : 0U);
      process.name = "observer";
      process.register_count = 1U;
      process.static_sensitivity.push_back(
          {trigger, EdgeKind::any});
      process.operations = {
          WaitSensitivity { },
          ReadSignal {0U, trigger},
          WriteBlocking {observed, 0U},
          Halt { },
      };
      (void)interpreter.add_process(std::move(process));
    };

    if (writer_first) {
      add_writer();
      add_observer();
    } else {
      add_observer();
      add_writer();
    }
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && interpreter.signal_value(observed).to_msb_string() == "1",
        "an initial static wait observes a time-zero writer regardless of "
        "process order");
  };

  run(false);
  run(true);
}

void test_simir_cohort_discard_before_final_scheduling() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto clock = interpreter.add_signal(
      { "top.discard_clock",
          PackedLogic4::from_msb_string("0"),
          ResolutionKind::sv_wire });
  const auto final_marker = interpreter.add_signal(
      { "top.final_marker", PackedLogic4::from_msb_string("0") });

  struct CohortProbe {
    std::vector<ProcessId> member_order;
    std::vector<std::size_t> batch_sizes;
    bool update_scheduled { };
  } probe;

  class SnapshotExecutor final : public ProcessExecutor {
  public:
    SnapshotExecutor(
        const ProcessId id,
        const SignalId clock,
        CohortProbe& probe)
        : id_ { id }
        , clock_ { clock }
        , probe_ { probe }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
      require(
          start == 0U || start == 1U,
          "discarded cohort member resumes at its static wait boundary");
      probe_.member_order.push_back(id_);
      if (id_ == 1U && !probe_.update_scheduled) {
        probe_.update_scheduled = true;
        context.write_update_word(clock_, Logic4Word { 1U, 0U, 0U });
      }
      ProcessResumeResult result { 0U, 1U };
      result.external.kind = ExternalSuspendKind::wait_sensitivity;
      return result;
    }

    [[nodiscard]] std::size_t resume_cohort(
        const std::span<ProcessCohortResumeEntry> entries) override
    {
      probe_.batch_sizes.push_back(entries.size());
      for (auto& entry : entries) {
        auto& executor = *static_cast<SnapshotExecutor*>(entry.executor);
        entry.result = executor.resume(
            *entry.context, entry.start_instruction);
      }
      return entries.size();
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
      return &probe_;
    }

  private:
    ProcessId id_ { };
    SignalId clock_ { };
    CohortProbe& probe_;
  };

  Process stopper;
  stopper.id = 0U;
  stopper.name = "stop_after_pending_cohort_snapshot";
  stopper.operations = { Yield { }, Yield { }, Stop { } };
  (void)interpreter.add_process(std::move(stopper));

  for (ProcessId id = 1U; id <= 3U; ++id) {
    Process member;
    member.id = id;
    member.name = "top.discard_member" + std::to_string(id);
    member.static_sensitivity.push_back({ clock, EdgeKind::any });
    member.operations = { WaitSensitivity { }, Jump { 0U } };
    member.initialize = false;
    const auto added = interpreter.add_process(std::move(member));
    interpreter.set_process_executor(
        added, std::make_unique<SnapshotExecutor>(id, clock, probe));
  }

  Process trigger;
  trigger.id = 4U;
  trigger.name = "trigger_current_cohort_snapshot";
  trigger.register_count = 1U;
  trigger.operations = {
      LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
      WriteBlocking { clock, 0U },
      Halt { },
  };
  (void)interpreter.add_process(std::move(trigger));

  Process final;
  final.id = 5U;
  final.name = "final_after_cohort_discard";
  final.register_count = 1U;
  final.initialize = false;
  final.final = true;
  final.operations = {
      LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
      WriteBlocking { final_marker, 0U },
      Halt { },
  };
  (void)interpreter.add_process(std::move(final));

  std::vector<std::string> clock_changes;
  interpreter.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick) {
        if (signal == clock) {
          clock_changes.push_back(value.to_msb_string());
        }
      });

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::stopped
          && interpreter.stopped_by_design()
          && result.time == 0U,
      "design stop preserves identity after final scheduling");
  // The member's zero drive conflicts with the trigger's one drive.
  require(
      clock_changes == std::vector<std::string> { "1", "X" }
          && probe.update_scheduled,
      "the first cohort wake commits a second edge for the pending delta snapshot");
  require(
      probe.batch_sizes == std::vector<std::size_t> { 3U }
          && probe.member_order == std::vector<ProcessId> { 1U, 2U, 3U },
      "discard removes the pending cohort snapshot without duplicate or "
      "reordered member execution");
  require(
      interpreter.signal_value(final_marker).to_msb_string() == "1",
      "final scheduling runs after pending cohort wakes are discarded");
  require(
      interpreter.run().status == RunStatus::stopped
          && probe.member_order == std::vector<ProcessId> { 1U, 2U, 3U },
      "a discarded cohort snapshot cannot execute on a later run");
}

void test_simir_cohort_snapshot_pool() {
  using namespace fsim::runtime::simir;

  CohortSnapshotPool snapshots;
  const std::array<ProcessId, 2> members { 4U, 7U };
  const auto stale = snapshots.acquire(members);
  snapshots.release(stale);
  const auto current = snapshots.acquire(members);
  int calls = 0;
  require(!snapshots.consume(stale, [&](auto) { ++calls; }),
          "a recycled cohort slot rejects its stale generation");
  require(snapshots.consume(current, [&](const auto ready) {
    require(ready.size() == 2U && ready[0] == 4U && ready[1] == 7U,
            "a cohort snapshot retains its ordered members");
    ++calls;
  }) && calls == 1,
          "a cohort snapshot is consumed once");
  require(!snapshots.consume(current, [&](auto) { ++calls; }),
          "a consumed cohort token cannot run again");

  const auto throwing = snapshots.acquire(members);
  bool caught = false;
  try {
    (void)snapshots.consume(throwing, [](auto) {
      throw std::runtime_error("cohort snapshot failure");
    });
  } catch (const std::runtime_error&) {
    caught = true;
  }
  require(caught && !snapshots.consume(throwing, [&](auto) { ++calls; }),
          "a failing consumer retires its cohort snapshot");

  const auto active = snapshots.acquire(members);
  const auto pending = snapshots.acquire(members);
  require(snapshots.consume(active, [&](const auto ready) {
    snapshots.discard();
    require(ready.size() == 2U && ready[1] == 7U,
            "discard preserves an in-flight cohort member span");
  }), "an active cohort survives reentrant discard");
  require(!snapshots.consume(pending, [&](auto) { ++calls; }),
          "discard invalidates other pending cohort snapshots");
}

void test_simir_cohort_external_discard_and_reset() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  for (const bool reset : { false, true }) {
    Interpreter interpreter;
    const auto clock = interpreter.add_signal(
        { "top.external_discard_clock",
            PackedLogic4::from_msb_string("0") });
    std::array<SignalId, 2> markers;
    for (ProcessId id = 0U; id < markers.size(); ++id) {
      markers[id] = interpreter.add_signal(
          { "top.external_discard_marker" + std::to_string(id),
              PackedLogic4::from_msb_string("0") });
      Process member;
      member.id = id;
      member.name = "external_discard_member" + std::to_string(id);
      member.register_count = 1U;
      member.static_sensitivity.push_back({ clock, EdgeKind::any });
      member.operations = {
          WaitSensitivity { },
          LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
          WriteBlocking { markers[id], 0U },
          Halt { },
      };
      (void)interpreter.add_process(std::move(member));
    }

    bool stop_at_first_edge = true;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4&,
            const SimulationTick) {
          if (signal == clock && stop_at_first_edge) {
            stop_at_first_edge = false;
            interpreter.scheduler().request_stop();
          }
        });
    interpreter.schedule_signal_at(
        clock, PackedLogic4::from_msb_string("1"), 1U, 0U);
    const auto stopped = interpreter.run();
    require(stopped.status == RunStatus::stopped
                && interpreter.scheduler().has_pending(),
            "external stop leaves a pending static cohort wake");
    if (reset) {
      interpreter.scheduler().reset();
    } else {
      interpreter.scheduler().discard_pending();
      interpreter.scheduler().clear_stop();
    }
    interpreter.schedule_signal_at(
        clock, PackedLogic4::from_msb_string("0"),
        reset ? 1U : 2U, 0U);
    const auto resumed = interpreter.run();
    require(resumed.status == RunStatus::completed,
            "a fresh edge runs after external scheduler cleanup");
    for (const auto marker : markers) {
      require(interpreter.signal_value(marker).to_msb_string() == "1",
              "external cleanup leaves cohort members eligible to wake");
    }
  }
}

void test_simir_large_cohort_scratch_reuse() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  constexpr std::size_t member_count = 513U;
  Interpreter interpreter;
  const auto clock = interpreter.add_signal(
      { "top.large_cohort_clock", PackedLogic4::from_msb_string("0") });
  std::vector<std::size_t> resume_counts(member_count);
  std::size_t cohort_calls { };

  class LargeExecutor final : public ProcessExecutor {
  public:
    LargeExecutor(
        ProcessId id, std::vector<std::size_t>& counts,
        std::size_t& cohort_calls)
        : id_ { id }
        , counts_ { counts }
        , cohort_calls_ { cohort_calls }
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext&, InstructionIndex) override
    {
      ++counts_[id_];
      ProcessResumeResult result { 0U, 1U };
      result.external.kind = ExternalSuspendKind::wait_sensitivity;
      return result;
    }

    [[nodiscard]] std::size_t resume_cohort(
        const std::span<ProcessCohortResumeEntry> entries) override
    {
      require(entries.size() == 513U,
              "the large static cohort uses one ordered overflow batch");
      ++cohort_calls_;
      for (auto& entry : entries) {
        auto& executor = *static_cast<LargeExecutor*>(entry.executor);
        entry.result = executor.resume(
            *entry.context, entry.start_instruction);
      }
      return entries.size();
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
      return &counts_;
    }

  private:
    ProcessId id_ { };
    std::vector<std::size_t>& counts_;
    std::size_t& cohort_calls_;
  };

  for (std::size_t index = 0U; index < member_count; ++index) {
    const auto id = static_cast<ProcessId>(index);
    Process member;
    member.id = id;
    member.name = "large_cohort_member" + std::to_string(index);
    member.static_sensitivity.push_back({ clock, EdgeKind::any });
    member.operations = { WaitSensitivity { }, Jump { 0U } };
    member.initialize = false;
    const auto added = interpreter.add_process(std::move(member));
    interpreter.set_process_executor(
        added, std::make_unique<LargeExecutor>(
            id, resume_counts, cohort_calls));
  }
  interpreter.schedule_signal_at(
      clock, PackedLogic4::from_msb_string("1"), 1U, 0U);
  interpreter.schedule_signal_at(
      clock, PackedLogic4::from_msb_string("0"), 2U, 0U);
  require(interpreter.run().status == RunStatus::completed
              && cohort_calls == 2U
              && std::ranges::all_of(resume_counts,
                  [](const auto count) { return count == 2U; }),
          "large cohort scratch serves repeated wakes without lost members");
}

void test_simir_static_sensitivity_cohort() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  test_simir_cohort_snapshot_pool();
  test_simir_cohort_external_discard_and_reset();
  test_simir_large_cohort_scratch_reuse();

  Interpreter interpreter;
  const auto clock = interpreter.add_signal(
      {"top.clock", PackedLogic4::from_msb_string("0")});
  std::array<SignalId, 3> observed;
  for (std::size_t index = 0; index < observed.size(); ++index) {
    observed[index] = interpreter.add_signal(
        {"top.u" + std::to_string(index) + ".observed",
         PackedLogic4::from_msb_string("0")});
    Process process;
    process.id = static_cast<ProcessId>(index);
    process.name = "top.u" + std::to_string(index) + ".clocked";
    process.register_count = 1;
    process.static_sensitivity.push_back({clock, EdgeKind::posedge});
    process.operations = {
        LoadConstant{0, PackedLogic4::from_msb_string("1")},
        WriteBlocking{observed[index], 0},
        WaitSensitivity{},
        Jump{0},
    };
    process.initialize = false;
    (void)interpreter.add_process(std::move(process));
  }
  interpreter.schedule_signal_at(
      clock, PackedLogic4::from_msb_string("1"), 5, 0);

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 5,
      "an exact-sensitivity cohort completes at the triggering time");
  for (const auto signal : observed) {
    require(
        interpreter.signal_value(signal).to_msb_string() == "1",
        "every cross-hierarchy cohort member wakes exactly once");
  }

  struct CohortProbe {
    std::vector<std::size_t> batches;
  } probe;
  class CohortExecutor final : public ProcessExecutor {
  public:
    CohortExecutor(const SignalId observed, CohortProbe& probe)
        : observed_(observed)
        , probe_(probe)
    {
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
      require(start == 0U, "cohort executor starts at its static wait body");
      context.write_blocking_word(observed_, Logic4Word { 1U, 1U, 0U });
      ProcessResumeResult result { 0U, 1U };
      result.external.kind = ExternalSuspendKind::wait_sensitivity;
      return result;
    }

    [[nodiscard]] std::size_t resume_cohort(
        const std::span<ProcessCohortResumeEntry> entries) override
    {
      probe_.batches.push_back(entries.size());
      for (auto& entry : entries) {
        auto& executor = *static_cast<CohortExecutor*>(entry.executor);
        entry.result = executor.resume(
            *entry.context, entry.start_instruction);
      }
      return entries.size();
    }

    [[nodiscard]] const void* cohort_domain() const noexcept override
    {
      return &probe_;
    }

  private:
    SignalId observed_ { };
    CohortProbe& probe_;
  };

  Interpreter mixed;
  const auto mixed_clock = mixed.add_signal(
      {"top.clock", PackedLogic4::from_msb_string("0")});
  std::array<SignalId, 5> mixed_observed;
  for (std::size_t index = 0; index < mixed_observed.size(); ++index) {
    mixed_observed[index] = mixed.add_signal(
        {"top.branch" + std::to_string(index) + ".observed",
         PackedLogic4::from_msb_string("0")});
    Process process;
    process.id = static_cast<ProcessId>(index);
    process.name = "top.branch" + std::to_string(index) + ".clocked";
    process.register_count = 1U;
    process.static_sensitivity.push_back(
        {mixed_clock, EdgeKind::posedge});
    process.operations = {
        WaitSensitivity { },
        Jump { 0U },
    };
    if (index == 2U) {
      process.operations = {
          LoadConstant { 0U, PackedLogic4::from_msb_string("1") },
          WriteBlocking { mixed_observed[index], 0U },
          WaitSensitivity { },
          Jump { 0U },
      };
    }
    process.initialize = false;
    const auto id = mixed.add_process(std::move(process));
    if (index != 2U) {
      mixed.set_process_executor(id,
          std::make_unique<CohortExecutor>(mixed_observed[index], probe));
    }
  }
  mixed.schedule_signal_at(
      mixed_clock, PackedLogic4::from_msb_string("1"), 7U, 0U);
  const auto mixed_result = mixed.run();
  require(
      mixed_result.status == RunStatus::completed
          && mixed_result.time == 7U,
      "mixed native/interpreted hierarchy cohort completes at its event");
  require(
      probe.batches == std::vector<std::size_t> { 2U, 2U },
      "native cohort runs span hierarchy while preserving an interpreted member");
  for (const auto signal : mixed_observed) {
    require(
        mixed.signal_value(signal).to_msb_string() == "1",
        "every mixed hierarchy cohort member executes in canonical order");
  }

  class RearmProbeExecutor final : public ProcessExecutor {
  public:
    RearmProbeExecutor(
        std::uint32_t& resumes,
        const std::optional<SignalId> blocking_target = std::nullopt)
        : resumes_(resumes), blocking_target_(blocking_target) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override {
      require(
          start == 0U || start == 1U,
          "re-arm probe resumes at its static wait loop");
      ++resumes_;
      if (blocking_target_) {
        context.write_blocking_word(
            *blocking_target_, Logic4Word {1U, 0U, 0U});
      }
      ProcessResumeResult result {0U, 1U};
      result.external.kind = ExternalSuspendKind::wait_sensitivity;
      return result;
    }

  private:
    std::uint32_t& resumes_;
    std::optional<SignalId> blocking_target_;
  };

  const auto add_waiting_probe = [](
                                     Interpreter& runtime,
                                     const ProcessId id,
                                     const SignalId sensitivity,
                                     std::unique_ptr<ProcessExecutor> executor) {
    Process process;
    process.id = id;
    process.name = "top.probe" + std::to_string(id);
    process.static_sensitivity.push_back({sensitivity, EdgeKind::any});
    process.operations = {WaitSensitivity {}, Jump {0U}};
    process.initialize = false;
    const auto added = runtime.add_process(std::move(process));
    runtime.set_process_executor(added, std::move(executor));
  };

  Interpreter rearm;
  const auto rearm_first = rearm.add_signal(
      {"top.rearm_first", PackedLogic4::from_msb_string("0")});
  const auto rearm_second = rearm.add_signal(
      {"top.rearm_second", PackedLogic4::from_msb_string("0")});
  std::uint32_t first_resumes { };
  std::uint32_t second_resumes { };
  add_waiting_probe(rearm, 0U, rearm_first,
      std::make_unique<RearmProbeExecutor>(first_resumes));
  add_waiting_probe(rearm, 1U, rearm_second,
      std::make_unique<RearmProbeExecutor>(second_resumes, rearm_first));
  rearm.schedule_signal_at(
      rearm_first, PackedLogic4::from_msb_string("1"), 11U, 0U);
  rearm.schedule_signal_at(
      rearm_second, PackedLogic4::from_msb_string("1"), 11U, 1U);
  const auto rearm_result = rearm.run();
  require(
      rearm_result.status == RunStatus::completed
          && rearm_result.time == 11U
          && first_resumes == 2U && second_resumes == 1U,
      "a later blocking write observes an earlier process re-armed at its "
      "static wait boundary");

  Interpreter exact_rearm;
  const auto exact_trigger = exact_rearm.add_signal(
      {"top.exact_trigger", PackedLogic4::from_msb_string("0")});
  std::uint32_t exact_first_resumes { };
  std::uint32_t exact_second_resumes { };
  add_waiting_probe(exact_rearm, 0U, exact_trigger,
      std::make_unique<RearmProbeExecutor>(exact_first_resumes));
  add_waiting_probe(exact_rearm, 1U, exact_trigger,
      std::make_unique<RearmProbeExecutor>(
          exact_second_resumes, exact_trigger));
  exact_rearm.schedule_signal_at(
      exact_trigger, PackedLogic4::from_msb_string("1"), 12U, 0U);
  const auto exact_rearm_result = exact_rearm.run();
  require(
      exact_rearm_result.status == RunStatus::completed
          && exact_rearm_result.time == 12U
          && exact_first_resumes == 2U
          && exact_second_resumes == 1U,
      "a later exact-cohort member observes an earlier member re-armed at "
      "its static wait boundary");

  Interpreter coalesced;
  const auto coalesced_later = coalesced.add_signal(
      {"top.coalesced_later", PackedLogic4::from_msb_string("0")});
  const auto coalesced_first = coalesced.add_signal(
      {"top.coalesced_first", PackedLogic4::from_msb_string("0")});
  std::uint32_t first_group_resumes { };
  std::uint32_t later_group_resumes { };
  add_waiting_probe(coalesced, 0U, coalesced_first,
      std::make_unique<RearmProbeExecutor>(
          first_group_resumes, coalesced_later));
  add_waiting_probe(coalesced, 1U, coalesced_later,
      std::make_unique<RearmProbeExecutor>(later_group_resumes));
  coalesced.schedule_signal_at(
      coalesced_later, PackedLogic4::from_msb_string("1"), 13U, 0U);
  coalesced.schedule_signal_at(
      coalesced_first, PackedLogic4::from_msb_string("1"), 13U, 1U);
  const auto coalesced_result = coalesced.run();
  require(
      coalesced_result.status == RunStatus::completed
          && coalesced_result.time == 13U
          && first_group_resumes == 1U && later_group_resumes == 1U,
      "a blocking wake coalesces while a later dispatch group remains queued");

  struct ForkedFanoutProbe {
    std::vector<std::pair<InstructionIndex, std::uint64_t>> activations;
  } forked_fanout_probe;
  class ForkedFanoutExecutor final : public ProcessExecutor {
  public:
    explicit ForkedFanoutExecutor(
        ForkedFanoutProbe& probe,
        const std::optional<InstructionIndex> branch = std::nullopt)
        : probe_(probe)
        , branch_(branch)
    {
    }

    [[nodiscard]] std::unique_ptr<ProcessExecutor> fork_clone(
        const InstructionIndex branch) override
    {
      return std::make_unique<ForkedFanoutExecutor>(probe_, branch);
    }

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override
    {
      if (!branch_) {
        if (start == 0U) {
          return { 0U, 1U };
        }
        if (start == 1U) {
          // LoadConstant at 1 has no effect on the fork probe; stop at Halt.
          return { 2U, 3U };
        }
        throw std::logic_error {
          "forked fanout parent resumed at an unexpected instruction"
        };
      }

      if (start == *branch_) {
        return { start, start + 1U };
      }
      if (start == *branch_ + 1U || start == *branch_ + 2U) {
        probe_.activations.emplace_back(
            *branch_, context.static_trigger_mask());
        return { start, start + 1U };
      }
      throw std::logic_error {
        "forked fanout child resumed at an unexpected instruction"
      };
    }

  private:
    ForkedFanoutProbe& probe_;
    std::optional<InstructionIndex> branch_;
  };

  Interpreter forked_fanout;
  const auto forked_clock = forked_fanout.add_signal(
      { "top.forked_clock", PackedLogic4::from_msb_string("0") });
  Process fork_parent;
  fork_parent.id = 0U;
  fork_parent.name = "forked_static_fanout";
  fork_parent.register_count = 1U;
  fork_parent.static_sensitivity = {
      { forked_clock, EdgeKind::posedge },
      { forked_clock, EdgeKind::posedge },
      { forked_clock, EdgeKind::negedge },
  };
  // A nonempty trigger-region table enables per-sensitivity trigger bits.
  fork_parent.static_trigger_regions.push_back(
      { 1U, 2U, UINT64_C(0x7) });
  fork_parent.operations = {
      Fork { { 3U, 6U, 9U }, ForkJoinKind::none },
      LoadConstant { 0U, PackedLogic4::from_msb_string("0") },
      Halt { },
      WaitSensitivity { },
      WaitSensitivity { },
      ForkEnd { },
      WaitSensitivity { },
      WaitSensitivity { },
      ForkEnd { },
      WaitSensitivity { },
      WaitSensitivity { },
      ForkEnd { },
  };
  const auto fork_parent_id = forked_fanout.add_process(
      std::move(fork_parent));
  forked_fanout.set_process_executor(
      fork_parent_id,
      std::make_unique<ForkedFanoutExecutor>(forked_fanout_probe));
  forked_fanout.schedule_signal_at(
      forked_clock, PackedLogic4::from_msb_string("1"), 1U, 0U);
  forked_fanout.schedule_signal_at(
      forked_clock, PackedLogic4::from_msb_string("0"), 2U, 0U);
  const auto forked_fanout_result = forked_fanout.run();
  const std::vector<std::pair<InstructionIndex, std::uint64_t>>
      expected_forked_activations {
          { 3U, UINT64_C(0x3) },
          { 6U, UINT64_C(0x3) },
          { 9U, UINT64_C(0x3) },
          { 3U, UINT64_C(0x4) },
          { 6U, UINT64_C(0x4) },
          { 9U, UINT64_C(0x4) },
      };
  require(
      forked_fanout_result.status == RunStatus::completed
          && forked_fanout_result.time == 2U
          && forked_fanout_probe.activations == expected_forked_activations,
      "forked static fanout rebuild preserves child order and duplicate mixed-edge masks");

  test_simir_cohort_discard_before_final_scheduling();
}

void test_simir_wide_truth_and_comparison() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto greater = interpreter.add_signal(
      {"top.greater", PackedLogic4::from_msb_string("0")});
  const auto logical_not_known = interpreter.add_signal(
      {"top.logical_not_known", PackedLogic4::from_msb_string("X")});
  const auto logical_not_unknown = interpreter.add_signal(
      {"top.logical_not_unknown", PackedLogic4::from_msb_string("0")});
  const auto case_equal_unknown = interpreter.add_signal(
      {"top.case_equal_unknown", PackedLogic4::from_msb_string("0")});
  const auto case_equal_distinct = interpreter.add_signal(
      {"top.case_equal_distinct", PackedLogic4::from_msb_string("1")});
  const auto case_not_equal_distinct = interpreter.add_signal(
      {"top.case_not_equal_distinct",
       PackedLogic4::from_msb_string("0")});

  Process process;
  process.id = 0;
  process.name = "wide_truth_and_comparison";
  process.register_count = 12;
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string(
                 "1" + std::string(64, '0'))},
      LoadConstant{
          1, PackedLogic4::from_msb_string(
                 "0" + std::string(64, '1'))},
      Binary{BinaryOperator::greater_unsigned, 2, 0, 1},
      WriteBlocking{greater, 2},
      LogicalNot{3, 0},
      WriteBlocking{logical_not_known, 3},
      LoadConstant{
          4, PackedLogic4::from_msb_string(
                 "X" + std::string(64, '0'))},
      LogicalNot{5, 4},
      WriteBlocking{logical_not_unknown, 5},
      LoadConstant{
          6, PackedLogic4::from_msb_string(
                 "X" + std::string(64, '0'))},
      LoadConstant{
          7, PackedLogic4::from_msb_string(
                 "X" + std::string(64, '0'))},
      Binary{BinaryOperator::case_equal, 8, 6, 7},
      WriteBlocking{case_equal_unknown, 8},
      LoadConstant{
          9, PackedLogic4::from_msb_string(
                 "Z" + std::string(64, '0'))},
      Binary{BinaryOperator::case_equal, 10, 6, 9},
      WriteBlocking{case_equal_distinct, 10},
      UnaryNot{11, 10},
      WriteBlocking{case_not_equal_distinct, 11},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));
  const auto result = interpreter.run();
  require(result.status == RunStatus::completed,
          "wide comparison process completes");
  require(interpreter.signal_value(greater).to_msb_string() == "1",
          "wide unsigned comparison uses high bits");
  require(
      interpreter.signal_value(logical_not_known).to_msb_string()
          == "0",
      "known one dominates wide logical negation");
  require(
      interpreter.signal_value(logical_not_unknown).to_msb_string()
          == "X",
      "wide unknown-only truth value remains unknown");
  require(
      interpreter.signal_value(case_equal_unknown).to_msb_string()
          == "1",
      "wide case equality matches identical unknown bits");
  require(
      interpreter.signal_value(case_equal_distinct).to_msb_string()
              == "0"
          && interpreter.signal_value(case_not_equal_distinct)
                  .to_msb_string()
              == "1",
      "wide case equality distinguishes X from Z");
}

void test_simir_wildcard_case_matching() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  std::array<SignalId, 8> results{};
  for (std::size_t index = 0; index < results.size(); ++index) {
    results[index] = interpreter.add_signal(
        {"top.wildcard_" + std::to_string(index),
         PackedLogic4::from_msb_string("X")});
  }

  struct Match {
    BinaryOperator operation;
    std::string_view lhs;
    std::string_view rhs;
  };
  const std::array matches{
      Match{BinaryOperator::casez_equal, "10Z1", "1011"},
      Match{BinaryOperator::casez_equal, "10X1", "1011"},
      Match{BinaryOperator::casez_equal, "1011", "10Z1"},
      Match{BinaryOperator::casez_equal, "10X1", "10X1"},
      Match{BinaryOperator::casex_equal, "10X1", "1011"},
      Match{BinaryOperator::casex_equal, "10Z1", "1001"},
      Match{BinaryOperator::casex_equal, "11X1", "10Z1"},
      Match{BinaryOperator::casex_equal, "1101", "1001"},
  };
  Process process;
  process.id = 0;
  process.name = "wildcard_case_matching";
  process.register_count = 3;
  for (std::size_t index = 0; index < matches.size(); ++index) {
    process.operations.emplace_back(LoadConstant{
        0, PackedLogic4::from_msb_string(matches[index].lhs)});
    process.operations.emplace_back(LoadConstant{
        1, PackedLogic4::from_msb_string(matches[index].rhs)});
    process.operations.emplace_back(
        Binary{matches[index].operation, 2, 0, 1});
    process.operations.emplace_back(WriteBlocking{results[index], 2});
  }
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));

  require(
      interpreter.run().status == RunStatus::completed,
      "wildcard case comparison process completes");
  const std::array expected{"1", "0", "1", "1", "1", "1", "0", "0"};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(
        interpreter.signal_value(results[index]).to_msb_string()
            == expected[index],
        "casez/casex wildcard truth table");
  }
}

void test_simir_wildcard_equality() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  std::array<SignalId, 9> results{};
  for (std::size_t index = 0; index < results.size(); ++index) {
    results[index] = interpreter.add_signal(
        {"top.wildcard_equality_" + std::to_string(index),
         PackedLogic4::from_msb_string("0")});
  }

  struct Comparison {
    BinaryOperator operation;
    std::string_view lhs;
    std::string_view rhs;
  };
  const std::array comparisons{
      Comparison{BinaryOperator::wildcard_equal, "1001", "10X1"},
      Comparison{BinaryOperator::wildcard_equal, "10Z1", "10Z1"},
      Comparison{BinaryOperator::wildcard_equal, "10X1", "1011"},
      Comparison{BinaryOperator::wildcard_equal, "10Z1", "1011"},
      Comparison{BinaryOperator::wildcard_equal, "1101", "10Z1"},
      Comparison{BinaryOperator::wildcard_equal, "X101", "0001"},
      Comparison{BinaryOperator::wildcard_equal, "1001", "1001"},
      Comparison{BinaryOperator::wildcard_equal, "1001", "1101"},
      Comparison{BinaryOperator::equal, "X0", "X1"},
  };
  Process process;
  process.id = 0;
  process.name = "wildcard_equality";
  process.register_count = 3;
  for (std::size_t index = 0; index < comparisons.size(); ++index) {
    process.operations.emplace_back(LoadConstant{
        0,
        PackedLogic4::from_msb_string(comparisons[index].lhs)});
    process.operations.emplace_back(LoadConstant{
        1,
        PackedLogic4::from_msb_string(comparisons[index].rhs)});
    process.operations.emplace_back(Binary{
        comparisons[index].operation, 2, 0, 1});
    process.operations.emplace_back(WriteBlocking{results[index], 2});
  }
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));

  require(
      interpreter.run().status == RunStatus::completed,
      "wildcard equality process completes");
  const std::array expected{
      "1", "1", "X", "X", "0", "X", "1", "0", "X"};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(
        interpreter.signal_value(results[index]).to_msb_string()
            == expected[index],
        "one-sided wildcard equality truth table");
  }
}

void test_simir_vhdl_matching_equality() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  struct Comparison {
    std::string_view lhs;
    std::string_view rhs;
    std::string_view expected;
  };
  const std::array comparisons{
      Comparison{"10LH", "1001", "1"},
      Comparison{"10LH", "10--", "1"},
      Comparison{"----", "UXZW", "1"},
      Comparison{"UXZW", "----", "1"},
      Comparison{"UXZW", "UXZW", "0"},
      Comparison{"10LH", "1010", "0"},
      Comparison{"01", "01", "1"},
      Comparison{"01", "11", "0"},
  };
  Interpreter interpreter;
  std::array<SignalId, comparisons.size()> results{};
  Process process;
  process.id = 0;
  process.name = "vhdl_matching_equality";
  process.register_count = 3;
  process.register_value_kinds = {
      ValueKind::logic9, ValueKind::logic9, ValueKind::logic4};
  for (std::size_t index = 0; index < comparisons.size(); ++index) {
    results[index] = interpreter.add_signal(
        {"top.vhdl_match_" + std::to_string(index),
         PackedLogic4::from_msb_string("X")});
    process.operations.emplace_back(LoadConstant{
        0,
        PackedLogic4::from_logic9_msb_string(comparisons[index].lhs)});
    process.operations.emplace_back(LoadConstant{
        1,
        PackedLogic4::from_logic9_msb_string(comparisons[index].rhs)});
    process.operations.emplace_back(Binary{
        BinaryOperator::vhdl_match_equal, 2, 0, 1});
    process.operations.emplace_back(WriteBlocking{results[index], 2});
  }
  process.operations.emplace_back(Halt{});
  (void)interpreter.add_process(std::move(process));
  require(
      interpreter.run().status == RunStatus::completed,
      "VHDL matching equality process completes");
  for (std::size_t index = 0; index < comparisons.size(); ++index) {
    const auto observed =
        interpreter.signal_value(results[index]).to_msb_string();
    if (observed != comparisons[index].expected) {
      throw std::runtime_error(
          "VHDL matching truth-table row " + std::to_string(index)
          + " expected " + std::string{comparisons[index].expected}
          + " but observed " + observed);
    }
  }
}

void test_simir_wide_reduction_and_shift() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto reduced_and = interpreter.add_signal(
      {"top.reduced_and", PackedLogic4::from_msb_string("X")});
  const auto reduced_or = interpreter.add_signal(
      {"top.reduced_or", PackedLogic4::from_msb_string("X")});
  const auto reduced_xor = interpreter.add_signal(
      {"top.reduced_xor", PackedLogic4::from_msb_string("0")});
  const auto one_hot = interpreter.add_signal(
      {"top.one_hot", PackedLogic4::from_msb_string("0")});
  const auto one_hot_or_zero = interpreter.add_signal(
      {"top.one_hot_or_zero", PackedLogic4::from_msb_string("0")});
  const auto one_count = interpreter.add_signal(
      {"top.one_count", PackedLogic4(32, Logic4::zero)});
  const auto selected_count = interpreter.add_signal(
      {"top.selected_count", PackedLogic4(32, Logic4::zero)});
  const auto shifted_left = interpreter.add_signal(
      {"top.shifted_left", PackedLogic4(65, Logic4::x)});
  const auto shifted_right = interpreter.add_signal(
      {"top.shifted_right", PackedLogic4(65, Logic4::x)});
  const auto shifted_unknown = interpreter.add_signal(
      {"top.shifted_unknown", PackedLogic4(65, Logic4::zero)});
  const auto shifted_oversized = interpreter.add_signal(
      {"top.shifted_oversized", PackedLogic4(65, Logic4::x)});
  const auto shifted_arithmetic = interpreter.add_signal(
      {"top.shifted_arithmetic", PackedLogic4(65, Logic4::x)});
  const auto shifted_arithmetic_oversized =
      interpreter.add_signal(
          {"top.shifted_arithmetic_oversized",
           PackedLogic4(65, Logic4::x)});
  const auto shifted_arithmetic_left = interpreter.add_signal(
      {"top.shifted_arithmetic_left", PackedLogic4(65, Logic4::x)});
  const auto shifted_arithmetic_left_oversized =
      interpreter.add_signal(
          {"top.shifted_arithmetic_left_oversized",
           PackedLogic4(65, Logic4::x)});
  const auto rotated_left = interpreter.add_signal(
      {"top.rotated_left", PackedLogic4(65, Logic4::x)});
  const auto rotated_right = interpreter.add_signal(
      {"top.rotated_right", PackedLogic4(65, Logic4::x)});
  const auto rotated_full_width = interpreter.add_signal(
      {"top.rotated_full_width", PackedLogic4(65, Logic4::x)});

  const auto source_text =
      "1" + std::string(63, '0') + "Z";
  Process process;
  process.id = 0;
  process.name = "wide_reduction_and_shift";
  process.register_count = 22;
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string(source_text)},
      LoadConstant{
          1, PackedLogic4::from_msb_string("0000001")},
      Reduction{ReductionOperator::bit_and, 2, 0},
      WriteBlocking{reduced_and, 2},
      Reduction{ReductionOperator::bit_or, 3, 0},
      WriteBlocking{reduced_or, 3},
      Reduction{ReductionOperator::bit_xor, 4, 0},
      WriteBlocking{reduced_xor, 4},
      Reduction{ReductionOperator::one_hot, 18, 0},
      WriteBlocking{one_hot, 18},
      Reduction{
          ReductionOperator::one_hot_or_zero, 19, 0},
      WriteBlocking{one_hot_or_zero, 19},
      CountOnes{20, 0},
      WriteBlocking{one_count, 20},
      CountBits{21, 0, 0x9U},
      WriteBlocking{selected_count, 21},
      Shift{ShiftOperator::logical_left, 5, 0, 1},
      WriteBlocking{shifted_left, 5},
      Shift{ShiftOperator::logical_right, 6, 0, 1},
      WriteBlocking{shifted_right, 6},
      LoadConstant{
          7, PackedLogic4::from_msb_string("00000X1")},
      Shift{ShiftOperator::logical_left, 8, 0, 7},
      WriteBlocking{shifted_unknown, 8},
      LoadConstant{
          9, PackedLogic4::from_msb_string("1000001")},
      Shift{ShiftOperator::logical_right, 10, 0, 9},
      WriteBlocking{shifted_oversized, 10},
      Shift{ShiftOperator::arithmetic_right, 11, 0, 1},
      WriteBlocking{shifted_arithmetic, 11},
      Shift{ShiftOperator::arithmetic_right, 12, 0, 9},
      WriteBlocking{shifted_arithmetic_oversized, 12},
      Shift{ShiftOperator::arithmetic_left, 13, 0, 1},
      WriteBlocking{shifted_arithmetic_left, 13},
      Shift{ShiftOperator::arithmetic_left, 14, 0, 9},
      WriteBlocking{shifted_arithmetic_left_oversized, 14},
      Shift{ShiftOperator::rotate_left, 15, 0, 1},
      WriteBlocking{rotated_left, 15},
      Shift{ShiftOperator::rotate_right, 16, 0, 1},
      WriteBlocking{rotated_right, 16},
      Shift{ShiftOperator::rotate_left, 17, 0, 9},
      WriteBlocking{rotated_full_width, 17},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "wide reduction and shift process completes");
  require(
      interpreter.signal_value(reduced_and).to_msb_string() == "0"
          && interpreter.signal_value(reduced_or).to_msb_string()
              == "1"
          && interpreter.signal_value(reduced_xor).to_msb_string()
              == "X",
      "wide four-state reductions honor controlling values");
  require(
      interpreter.signal_value(one_hot).to_msb_string() == "1"
          && interpreter.signal_value(one_hot_or_zero)
                 .to_msb_string()
              == "1",
      "wide one-hot reductions count exact one bits and ignore X/Z");
  require(
      interpreter.signal_value(one_count).low_word().aval == 1
          && interpreter.signal_value(one_count).low_word().bval == 0,
      "wide count-ones counts exact one bits and ignores X/Z");
  require(
      interpreter.signal_value(selected_count).low_word().aval == 64
          && interpreter.signal_value(selected_count)
                 .low_word()
                 .bval
              == 0,
      "wide count-bits selects exact zero and Z states");
  require(
      interpreter.signal_value(shifted_left).to_msb_string()
          == std::string(63, '0') + "Z0",
      "wide logical left shift crosses packed storage words");
  require(
      interpreter.signal_value(shifted_right).to_msb_string()
          == "01" + std::string(63, '0'),
      "wide logical right shift crosses packed storage words");
  require(
      interpreter.signal_value(shifted_unknown).to_msb_string()
          == std::string(65, 'X'),
      "wide shift with an unknown amount produces all unknown bits");
  require(
      interpreter.signal_value(shifted_oversized).to_msb_string()
          == std::string(65, '0'),
      "wide oversized shift produces zero");
  require(
      interpreter.signal_value(shifted_arithmetic).to_msb_string()
          == "11" + std::string(63, '0'),
      "wide arithmetic right shift replicates the sign bit");
  require(
      interpreter.signal_value(shifted_arithmetic_oversized)
              .to_msb_string()
          == std::string(65, '1'),
      "wide oversized arithmetic right shift fills with the sign bit");
  require(
      interpreter.signal_value(shifted_arithmetic_left)
              .to_msb_string()
          == std::string(63, '0') + "ZZ",
      "wide arithmetic left shift fills with the rightmost element");
  require(
      interpreter.signal_value(shifted_arithmetic_left_oversized)
              .to_msb_string()
          == std::string(65, 'Z'),
      "wide oversized arithmetic left shift fills with the rightmost element");
  require(
      interpreter.signal_value(rotated_left).to_msb_string()
          == std::string(63, '0') + "Z1",
      "wide rotate left wraps the leftmost element");
  require(
      interpreter.signal_value(rotated_right).to_msb_string()
          == "Z1" + std::string(63, '0'),
      "wide rotate right wraps the rightmost element");
  require(
      interpreter.signal_value(rotated_full_width).to_msb_string()
          == source_text,
      "wide rotate reduces its amount modulo the operand width");
}

void test_simir_signed_shift_counts() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  std::array<SignalId, 6> outputs{};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    outputs[index] = interpreter.add_signal({
        "top.signed_shift_" + std::to_string(index),
        PackedLogic4(65, Logic4::x)});
  }

  const auto source =
      "1" + std::string(63, '0') + "Z";
  Process process;
  process.id = 0;
  process.name = "signed_shift_counts";
  process.register_count = 8;
  process.operations = {
      LoadConstant{
          0, PackedLogic4::from_msb_string(source)},
      LoadConstant{
          1,
          PackedLogic4::from_msb_string(
              std::string(70, '1'))},
      Shift{
          ShiftOperator::logical_left, 2, 0, 1, true},
      WriteBlocking{outputs[0], 2},
      Shift{
          ShiftOperator::logical_right, 3, 0, 1, true},
      WriteBlocking{outputs[1], 3},
      Shift{
          ShiftOperator::arithmetic_left, 4, 0, 1, true},
      WriteBlocking{outputs[2], 4},
      Shift{
          ShiftOperator::arithmetic_right, 5, 0, 1, true},
      WriteBlocking{outputs[3], 5},
      Shift{
          ShiftOperator::rotate_left, 6, 0, 1, true},
      WriteBlocking{outputs[4], 6},
      Shift{
          ShiftOperator::rotate_right, 7, 0, 1, true},
      WriteBlocking{outputs[5], 7},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "signed-count shift process completes");
  const std::array expected{
      "01" + std::string(63, '0'),
      std::string(63, '0') + "Z0",
      "11" + std::string(63, '0'),
      std::string(63, '0') + "ZZ",
      "Z1" + std::string(63, '0'),
      std::string(63, '0') + "Z1"};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    require(
        interpreter.signal_value(outputs[index]).to_msb_string()
            == expected[index],
        "negative arbitrary-width shift count reverses its operation");
  }
}

void test_simir_wide_unsigned_arithmetic() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  std::array<SignalId, 9> outputs{};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    outputs[index] = interpreter.add_signal({
        "top.arithmetic_" + std::to_string(index),
        PackedLogic4(65, Logic4::zero)});
  }

  const auto lhs =
      "1" + std::string(64, '0');
  const auto rhs =
      std::string(63, '0') + "11";
  Process process;
  process.id = 0;
  process.name = "wide_unsigned_arithmetic";
  process.register_count = 15;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string(lhs)},
      LoadConstant{1, PackedLogic4::from_msb_string(rhs)},
      Binary{BinaryOperator::add_unsigned, 2, 0, 1},
      WriteBlocking{outputs[0], 2},
      Binary{BinaryOperator::subtract_unsigned, 3, 0, 1},
      WriteBlocking{outputs[1], 3},
      Binary{BinaryOperator::multiply_unsigned, 4, 0, 1},
      WriteBlocking{outputs[2], 4},
      Binary{BinaryOperator::divide_unsigned, 5, 0, 1},
      WriteBlocking{outputs[3], 5},
      Binary{BinaryOperator::modulo_unsigned, 6, 0, 1},
      WriteBlocking{outputs[4], 6},
      LoadConstant{
          7,
          PackedLogic4::from_msb_string(
              "X" + std::string(64, '0'))},
      Binary{BinaryOperator::add_unsigned, 8, 7, 1},
      WriteBlocking{outputs[5], 8},
      LoadConstant{9, PackedLogic4(65, Logic4::zero)},
      Binary{BinaryOperator::divide_unsigned, 10, 0, 9},
      WriteBlocking{outputs[6], 10},
      LoadConstant{
          11,
          PackedLogic4::from_msb_string(
              std::string(63, '0') + "11")},
      LoadConstant{
          12,
          PackedLogic4::from_msb_string(
              std::string(62, '0') + "100")},
      Binary{BinaryOperator::power_unsigned, 13, 11, 12},
      WriteBlocking{outputs[7], 13},
      Binary{BinaryOperator::power_unsigned, 14, 7, 12},
      WriteBlocking{outputs[8], 14},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "wide unsigned arithmetic process completes");
  const std::array expected{
      "1" + std::string(62, '0') + "11",
      "0" + std::string(62, '1') + "01",
      lhs,
      "0" + std::string{"01010101010101010101010101010101"
                        "01010101010101010101010101010101"},
      std::string(64, '0') + "1",
      std::string(65, 'X'),
      std::string(65, 'X'),
      std::string(58, '0') + "1010001",
      std::string(65, 'X')};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    require(
        interpreter.signal_value(outputs[index]).to_msb_string()
            == expected[index],
        "wide unsigned arithmetic result");
  }
}

void test_simir_wide_signed_arithmetic() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto signed_value =
      [](const std::int64_t value, const std::size_t width) {
        PackedLogic4 result(width, Logic4::zero);
        const auto encoded = static_cast<std::uint64_t>(value);
        for (std::size_t bit = 0; bit < width; ++bit) {
          const bool one =
              bit < 64
                  ? ((encoded >> bit) & UINT64_C(1)) != 0
                  : value < 0;
          result.set(bit, one ? Logic4::one : Logic4::zero);
        }
        return result;
      };

  Interpreter interpreter;
  std::array<SignalId, 10> outputs{};
  for (std::size_t index = 0; index < 6; ++index) {
    outputs[index] = interpreter.add_signal(
        {"top.signed_" + std::to_string(index),
         PackedLogic4(65, Logic4::zero)});
  }
  outputs[6] = interpreter.add_signal(
      {"top.signed_less", PackedLogic4(1, Logic4::zero)});
  outputs[7] = interpreter.add_signal(
      {"top.signed_greater", PackedLogic4(1, Logic4::zero)});
  outputs[8] = interpreter.add_signal(
      {"top.signed_overflow", PackedLogic4(65, Logic4::zero)});
  outputs[9] = interpreter.add_signal(
      {"top.signed_unknown", PackedLogic4(65, Logic4::zero)});

  auto minimum = PackedLogic4(65, Logic4::zero);
  minimum.set(64, Logic4::one);
  auto unknown = PackedLogic4(65, Logic4::zero);
  unknown.set(37, Logic4::x);

  Process process;
  process.id = 0;
  process.name = "wide_signed_arithmetic";
  process.register_count = 15;
  process.operations = {
      LoadConstant{0, signed_value(-5, 65)},
      LoadConstant{1, signed_value(3, 65)},
      Binary{BinaryOperator::add_signed, 2, 0, 1},
      WriteBlocking{outputs[0], 2},
      Binary{BinaryOperator::subtract_signed, 3, 0, 1},
      WriteBlocking{outputs[1], 3},
      Binary{BinaryOperator::multiply_signed, 4, 0, 1},
      WriteBlocking{outputs[2], 4},
      Binary{BinaryOperator::divide_signed, 5, 0, 1},
      WriteBlocking{outputs[3], 5},
      Binary{BinaryOperator::remainder_signed, 6, 0, 1},
      WriteBlocking{outputs[4], 6},
      Binary{BinaryOperator::modulo_signed, 7, 0, 1},
      WriteBlocking{outputs[5], 7},
      Binary{BinaryOperator::less_signed, 8, 0, 1},
      WriteBlocking{outputs[6], 8},
      Binary{BinaryOperator::greater_signed, 9, 0, 1},
      WriteBlocking{outputs[7], 9},
      LoadConstant{10, std::move(minimum)},
      LoadConstant{11, signed_value(-1, 65)},
      Binary{BinaryOperator::divide_signed, 12, 10, 11},
      WriteBlocking{outputs[8], 12},
      LoadConstant{13, std::move(unknown)},
      Binary{BinaryOperator::divide_signed, 14, 13, 1},
      WriteBlocking{outputs[9], 14},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "wide signed arithmetic process completes");
  const std::array expected{
      signed_value(-2, 65),
      signed_value(-8, 65),
      signed_value(-15, 65),
      signed_value(-1, 65),
      signed_value(-2, 65),
      signed_value(1, 65)};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(
        interpreter.signal_value(outputs[index]) == expected[index],
        "wide signed arithmetic result");
  }
  require(
      interpreter.signal_value(outputs[6]).to_msb_string() == "1"
          && interpreter.signal_value(outputs[7]).to_msb_string()
              == "0",
      "wide signed relational ordering");
  require(
      interpreter.signal_value(outputs[8]).to_msb_string()
          == "1" + std::string(64, '0'),
      "signed minimum divided by negative one wraps at fixed width");
  require(
      interpreter.signal_value(outputs[9]).to_msb_string()
          == std::string(65, 'X'),
      "unknown signed arithmetic produces an all-X result");
}

} // namespace fsim::tests::runtime
