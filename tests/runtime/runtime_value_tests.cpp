// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/runtime/vcd_writer.hpp"

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

} // namespace

#include "runtime_value_primitives_tests.tpp"
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
  ordered.schedule_next_delta(
      SchedulerPhase::active, 25U,
      [&](Scheduler&) { fallbacks.push_back(99U); });
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

void test_resolved_driver_slots() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

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

void test_simir_static_sensitivity_cohort() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

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
