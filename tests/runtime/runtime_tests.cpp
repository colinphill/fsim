// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
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
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void test_logic() {
  using namespace fsim::runtime;

  require(to_logic4(Logic9::l) == Logic4::zero, "L must collapse to zero");
  require(to_logic4(Logic9::h) == Logic4::one, "H must collapse to one");
  require(to_logic4(Logic9::w) == Logic4::x, "W must collapse to X");
  require(resolve(Logic4::z, Logic4::one) == Logic4::one,
          "Z must be the four-state identity");
  require(resolve(Logic4::zero, Logic4::one) == Logic4::x,
          "conflicting four-state drivers must resolve to X");
  require(resolve(Logic9::l, Logic9::h) == Logic9::w,
          "weak conflict must resolve to W");
  require(resolve(Logic9::z, Logic9::h) == Logic9::h,
          "Z must be the std_logic identity");

  constexpr std::array states{
      Logic9::u,
      Logic9::x,
      Logic9::zero,
      Logic9::one,
      Logic9::z,
      Logic9::w,
      Logic9::l,
      Logic9::h,
      Logic9::dont_care};
  constexpr std::array<std::string_view, 9> golden{
      "UUUUUUUUU",
      "UXXXXXXXX",
      "UX0X0000X",
      "UXX11111X",
      "UX01ZWLHX",
      "UX01WWWWX",
      "UX01LWLWX",
      "UX01HWWHX",
      "UXXXXXXXX"};
  for (std::size_t left = 0; left < states.size(); ++left) {
    for (std::size_t right = 0; right < states.size(); ++right) {
      require(
          to_char(resolve(states[left], states[right]))
              == golden[left][right],
          "std_logic resolution table must match IEEE std_logic_1164");
    }
  }
}

void test_packed_values() {
  using namespace fsim::runtime;

  auto bits = PackedBit2::from_msb_string("101001");
  require(bits.width() == 6, "two-state width");
  require(bits.get(0), "rightmost input digit must be bit zero");
  bits.set(1, true);
  require(bits.to_msb_string() == "101011", "two-state mutation");

  const auto four = PackedLogic4::from_msb_string("10XZ");
  require(four.get(0) == Logic4::z, "four-state Z encoding");
  require(four.get(1) == Logic4::x, "four-state X encoding");
  require(four.to_msb_string() == "10XZ", "four-state round trip");

  const auto word_value = PackedLogic4::from_aval_bval(
      5, ~std::uint64_t{0}, UINT64_C(0b10100));
  require(
      word_value.to_msb_string() == "X1X11",
      "four-state word factory encoding");
  require(
      word_value.low_word()
          == Logic4Word{5, UINT64_C(0b11111), UINT64_C(0b10100)},
      "four-state word factory must mask unused bits");
  require(
      word_value.aval_words().size() == 1
          && word_value.bval_words().size() == 1,
      "four-state values up to 64 bits must expose one packed word");

  for (const auto invalid_width : {std::size_t{0}, std::size_t{65}}) {
    try {
      (void)PackedLogic4::from_aval_bval(invalid_width, 0, 0);
      throw std::runtime_error(
          "invalid four-state word width was accepted");
    } catch (const std::invalid_argument&) {
    }
  }
  std::string wide_text(65, '0');
  wide_text.front() = 'Z';
  wide_text.back() = 'X';
  const auto wide_value = PackedLogic4::from_msb_string(wide_text);
  require(
      wide_value.to_msb_string() == wide_text
          && wide_value.aval_words().size() == 2
          && wide_value.bval_words().size() == 2,
      "wide four-state values must retain multi-word storage");
  try {
    (void)wide_value.low_word();
    throw std::runtime_error(
        "wide four-state value exposed a single low word");
  } catch (const std::invalid_argument&) {
  }

  const auto nine = PackedLogic9::from_msb_string("U01ZWLH-");
  require(nine.to_msb_string() == "U01ZWLH-", "nine-state round trip");
  require(collapse_to_logic4(nine).to_msb_string() == "X01ZX01X",
          "nine-state collapse");

  const std::vector four_drivers = {
      PackedLogic4::from_msb_string("ZZ01"),
      PackedLogic4::from_msb_string("10Z1"),
  };
  require(resolve(std::span<const PackedLogic4>(four_drivers))
              .to_msb_string() == "1001",
          "packed four-state resolution");
}

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
      "postponed", "next-delta", "time-4"};
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

void test_checked_vhdl_integer_operations() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto integer = [](const std::int32_t value) {
    return PackedLogic4::from_aval_bval(
        32,
        static_cast<std::uint32_t>(value),
        0);
  };

  Interpreter interpreter;
  std::array<SignalId, 4> outputs{};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    outputs[index] = interpreter.add_signal(
        {"top.integer_" + std::to_string(index), integer(0)});
  }
  Process process;
  process.id = 0;
  process.name = "checked_integer_success";
  process.register_count = 7;
  process.operations = {
      LoadConstant{0, integer(-5)},
      LoadConstant{1, integer(3)},
      IntegerBinary{IntegerBinaryOperator::add, 2, 0, 1},
      IntegerBinary{IntegerBinaryOperator::modulo, 3, 0, 1},
      IntegerBinary{IntegerBinaryOperator::remainder, 4, 0, 1},
      IntegerUnary{IntegerUnaryOperator::absolute, 5, 0},
      IntegerBinary{IntegerBinaryOperator::power, 6, 1, 1},
      IntegerCheck{2, -2, 2},
      WriteBlocking{outputs[0], 2},
      WriteBlocking{outputs[1], 3},
      WriteBlocking{outputs[2], 4},
      WriteBlocking{outputs[3], 5},
      Halt{}};
  (void)interpreter.add_process(std::move(process));
  require(
      interpreter.run().status == RunStatus::completed,
      "checked integer success process completes");
  const std::array expected{-2, 1, -2, 5};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    require(
        interpreter.signal_value(outputs[index])
            == integer(expected[index]),
        "checked integer arithmetic follows VHDL signed semantics");
  }

  const auto expect_failure =
      [&](std::vector<Operation> operations,
          const std::string_view expected_message) {
        Interpreter failing;
        Process candidate;
        candidate.id = 0;
        candidate.name = "checked_integer_failure";
        candidate.register_count = 4;
        candidate.operations = std::move(operations);
        (void)failing.add_process(std::move(candidate));
        try {
          (void)failing.run();
          throw std::runtime_error(
              "checked VHDL integer failure was not reported");
        } catch (const InterpreterError& error) {
          require(
              std::string_view{error.what()}.find(expected_message)
                  != std::string_view::npos,
              "checked VHDL integer failure message");
        }
      };
  expect_failure(
      {
          LoadConstant{
              0, integer(std::numeric_limits<std::int32_t>::max())},
          LoadConstant{1, integer(1)},
          IntegerBinary{IntegerBinaryOperator::add, 2, 0, 1},
          Halt{}},
      "VHDL integer arithmetic overflow");
  expect_failure(
      {
          LoadConstant{0, integer(7)},
          LoadConstant{1, integer(0)},
          IntegerBinary{IntegerBinaryOperator::divide, 2, 0, 1},
          Halt{}},
      "VHDL integer division by zero");
  expect_failure(
      {
          LoadConstant{
              0, integer(std::numeric_limits<std::int32_t>::min())},
          IntegerUnary{IntegerUnaryOperator::negate, 1, 0},
          Halt{}},
      "VHDL integer arithmetic overflow");
  expect_failure(
      {
          LoadConstant{0, integer(2)},
          LoadConstant{1, integer(-1)},
          IntegerBinary{IntegerBinaryOperator::power, 2, 0, 1},
          Halt{}},
      "VHDL integer exponent must be nonnegative");
  expect_failure(
      {
          LoadConstant{0, integer(8)},
          IntegerCheck{0, -5, 7},
          Halt{}},
      "VHDL integer subtype range check failed");
  auto unknown = integer(0);
  unknown.set(4, Logic4::x);
  expect_failure(
      {
          LoadConstant{0, std::move(unknown)},
          IntegerUnary{IntegerUnaryOperator::absolute, 1, 0},
          Halt{}},
      "VHDL integer operand contains an unknown or high-impedance value");
}

void test_simir_wide_extract_and_concatenate() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto low = interpreter.add_signal(
      {"top.low", PackedLogic4(2, Logic4::zero)});
  const auto high = interpreter.add_signal(
      {"top.high", PackedLogic4(2, Logic4::zero)});
  const auto joined = interpreter.add_signal(
      {"top.joined", PackedLogic4(69, Logic4::zero)});
  const auto source =
      "1" + std::string(62, '0') + "XZ";

  Process process;
  process.id = 0;
  process.name = "wide_extract_and_concatenate";
  process.register_count = 4;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string(source)},
      Extract{1, 0, 0, 2},
      WriteBlocking{low, 1},
      Extract{2, 0, 63, 2},
      WriteBlocking{high, 2},
      Concatenate{3, {0, 2, 1}, 69},
      WriteBlocking{joined, 3},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "wide extract and concatenate process completes");
  require(
      interpreter.signal_value(low).to_msb_string() == "XZ"
          && interpreter.signal_value(high).to_msb_string() == "10",
      "wide extraction crosses normalized packed positions");
  require(
      interpreter.signal_value(joined).to_msb_string()
          == source + "10XZ",
      "wide concatenation preserves source operand order");
}

void test_simir_insert_and_partial_writes() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto inserted = interpreter.add_signal(
      {"top.inserted", PackedLogic4(8, Logic4::zero)});
  const auto blocking = interpreter.add_signal(
      {"top.blocking", PackedLogic4(8, Logic4::zero)});
  const auto updated = interpreter.add_signal(
      {"top.updated", PackedLogic4::from_msb_string("11110000")});
  const auto ordered = interpreter.add_signal(
      {"top.ordered", PackedLogic4(8, Logic4::zero)});
  const auto intervening = interpreter.add_signal(
      {"top.intervening", PackedLogic4(8, Logic4::zero)});
  const auto delayed = interpreter.add_signal(
      {"top.delayed", PackedLogic4::from_msb_string("10101010")});
  const auto wide = interpreter.add_signal(
      {"top.wide", PackedLogic4(70, Logic4::zero)});
  const auto wide_partial = interpreter.add_signal(
      {"top.wide_partial", PackedLogic4(70, Logic4::zero)});

  Process process;
  process.id = 0;
  process.name = "insert_and_partial_writes";
  process.register_count = 8;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("00000000")},
      LoadConstant{1, PackedLogic4::from_msb_string("XZ")},
      Insert{0, 0, 1, 3},
      WriteBlocking{inserted, 0},
      LoadConstant{2, PackedLogic4::from_msb_string("11")},
      WriteBlockingSlice{blocking, 2, 0},
      LoadConstant{3, PackedLogic4::from_msb_string("10")},
      WriteBlockingSlice{blocking, 3, 2},
      WriteUpdateSlice{updated, 2, 0},
      LoadConstant{4, PackedLogic4::from_msb_string("00")},
      WriteUpdateSlice{updated, 4, 4},
      WriteUpdateSlice{ordered, 2, 0},
      LoadConstant{5, PackedLogic4::from_msb_string("10101010")},
      WriteUpdate{ordered, 5},
      WriteUpdateSlice{ordered, 1, 4},
      WriteUpdateSlice{intervening, 2, 0},
      WriteBlocking{intervening, 5},
      WriteUpdateSlice{intervening, 1, 4},
      WriteAfterSlice{delayed, 1, 2, 5},
      WriteAfterSlice{delayed, 3, 4, 5},
      LoadConstant{6, PackedLogic4(70, Logic4::zero)},
      LoadConstant{7, PackedLogic4::from_msb_string("XZ10")},
      Insert{6, 6, 7, 64},
      WriteBlocking{wide, 6},
      WriteUpdateSlice{wide_partial, 7, 64},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 5,
      "partial-write process completes after its delayed updates");
  require(
      interpreter.signal_value(inserted).to_msb_string()
          == "000XZ000",
      "Insert replaces only its normalized destination range");
  require(
      interpreter.signal_value(blocking).to_msb_string()
          == "00001011",
      "blocking partial writes observe prior active-phase writes");
  require(
      interpreter.signal_value(updated).to_msb_string()
          == "11000011",
      "disjoint update writes merge in stable source order");
  require(
      interpreter.signal_value(ordered).to_msb_string()
          == "10XZ1010",
      "whole and partial updates obey last-assignment ordering");
  require(
      interpreter.signal_value(intervening).to_msb_string()
          == "10XZ1011",
      "partial updates merge at commit after intervening blocking writes");
  require(
      interpreter.signal_value(delayed).to_msb_string()
          == "1010XZ10",
      "same-time delayed partial writes merge during the update phase");
  require(
      interpreter.signal_value(wide).to_msb_string()
          == "00XZ10" + std::string(64, '0'),
      "wide Insert retains packed bits outside the selected range");
  require(
      interpreter.signal_value(wide_partial).to_msb_string()
          == "00XZ10" + std::string(64, '0'),
      "wide partial update uses the arbitrary-width kernel");
}

void test_simir_force_release() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"top.forced", PackedLogic4::from_msb_string("0")});
  interpreter.force_signal(signal, PackedLogic4::from_msb_string("1"));
  require(interpreter.signal_is_forced(signal), "force state");
  interpreter.deposit_signal(signal, PackedLogic4::from_msb_string("0"));
  require(interpreter.signal_value(signal).to_msb_string() == "1",
          "force must mask underlying deposits");
  interpreter.release_signal(signal);
  require(!interpreter.signal_is_forced(signal), "release state");
  require(interpreter.signal_value(signal).to_msb_string() == "0",
          "release must publish the last underlying value");
}

void test_simir_design_stop_identity() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter design_stop;
  Process process;
  process.id = 0;
  process.name = "finisher";
  process.operations.emplace_back(Stop{});
  (void)design_stop.add_process(std::move(process));
  require(
      !design_stop.stopped_by_design(),
      "design-stop identity must start clear");
  const auto result = design_stop.run();
  require(
      result.status == RunStatus::stopped
          && design_stop.stopped_by_design(),
      "Stop must be distinguishable from an external scheduler stop");

  Interpreter external_stop;
  external_stop.scheduler().request_stop();
  require(
      external_stop.run().status == RunStatus::stopped
          && !external_stop.stopped_by_design(),
      "external stop must not masquerade as language-level completion");
}

void test_simir_pause_resume_lifecycle() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto before = interpreter.add_signal(
      {"top.before", PackedLogic4::from_msb_string("0")});
  const auto after = interpreter.add_signal(
      {"top.after", PackedLogic4::from_msb_string("0")});
  const auto final_hit = interpreter.add_signal(
      {"top.final_hit", PackedLogic4::from_msb_string("0")});

  Process initial;
  initial.id = 0;
  initial.name = "initial";
  initial.register_count = 1;
  initial.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{before, 0},
      Pause{},
      WriteBlocking{after, 0},
      Halt{}};
  (void)interpreter.add_process(std::move(initial));

  Process final;
  final.id = 1;
  final.name = "final";
  final.register_count = 1;
  final.initialize = false;
  final.final = true;
  final.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{final_hit, 0},
      Halt{}};
  (void)interpreter.add_process(std::move(final));

  const auto paused = interpreter.run();
  require(
      paused.status == RunStatus::stopped
          && !interpreter.stopped_by_design()
          && interpreter.signal_value(before).to_msb_string() == "1"
          && interpreter.signal_value(after).to_msb_string() == "0"
          && interpreter.signal_value(final_hit).to_msb_string() == "0",
      "Pause must stop externally before its continuation or finals run");

  interpreter.scheduler().clear_stop();
  const auto resumed = interpreter.run();
  require(
      resumed.status == RunStatus::completed
          && !interpreter.stopped_by_design()
          && interpreter.signal_value(after).to_msb_string() == "1"
          && interpreter.signal_value(final_hit).to_msb_string() == "1",
      "clearing the pause must resume at the next operation and run finals");
  require(
      interpreter.run().status == RunStatus::completed
          && interpreter.signal_value(final_hit).to_msb_string() == "1",
      "resume completion must not rerun final processes");
}

void test_simir_final_process_lifecycle() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter natural;
  const auto value = natural.add_signal(
      {"top.value", PackedLogic4::from_msb_string("00000000")});
  Process initial;
  initial.id = 0;
  initial.name = "initial";
  initial.register_count = 1;
  initial.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("00000001")},
      WriteUpdate{value, 0},
      Halt{}};
  (void)natural.add_process(std::move(initial));

  Process final;
  final.id = 1;
  final.name = "final";
  final.register_count = 3;
  final.initialize = false;
  final.final = true;
  final.operations = {
      ReadSignal{0, value},
      LoadConstant{1, PackedLogic4::from_msb_string("00000001")},
      Binary{BinaryOperator::add_unsigned, 2, 0, 1},
      WriteBlocking{value, 2},
      Halt{}};
  (void)natural.add_process(std::move(final));

  const auto natural_result = natural.run();
  require(
      natural_result.status == RunStatus::completed
          && natural.signal_value(value).to_msb_string() == "00000010",
      "final process must run once after ordinary updates quiesce");
  require(
      natural.run().status == RunStatus::completed
          && natural.signal_value(value).to_msb_string() == "00000010",
      "a final process must not run again on a later run call");

  Interpreter design_stop;
  const auto stopped_value = design_stop.add_signal(
      {"top.stopped_value", PackedLogic4::from_msb_string("0")});
  Process pending;
  pending.id = 0;
  pending.name = "pending";
  pending.operations = {
      WaitFor{1},
      Halt{}};
  (void)design_stop.add_process(std::move(pending));
  Process stopper;
  stopper.id = 1;
  stopper.name = "stopper";
  stopper.operations = {Stop{}};
  (void)design_stop.add_process(std::move(stopper));
  Process stop_final;
  stop_final.id = 2;
  stop_final.name = "stop_final";
  stop_final.register_count = 1;
  stop_final.initialize = false;
  stop_final.final = true;
  stop_final.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{stopped_value, 0},
      Halt{}};
  (void)design_stop.add_process(std::move(stop_final));
  const auto stopped_result = design_stop.run();
  require(
      stopped_result.status == RunStatus::stopped
          && design_stop.stopped_by_design()
          && stopped_result.time == 0
          && design_stop.signal_value(stopped_value).to_msb_string() == "1",
      "design Stop must discard ordinary future work, execute finals, and "
      "preserve stopped identity");

  Interpreter invalid;
  Process invalid_final;
  invalid_final.id = 0;
  invalid_final.name = "invalid_final";
  invalid_final.final = true;
  bool rejected = false;
  try {
    (void)invalid.add_process(std::move(invalid_final));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(
      rejected,
      "a final SimIR process must be excluded from time-zero initialization");
}

void test_simir_alternate_executor_context_and_boundaries() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto make_process =
      [](const SignalId input, const SignalId output) {
        Process process;
        process.id = 0;
        process.name = "alternate_executor";
        process.register_count = 3;
        process.operations = {
            ReadSignal{0, input},
            WriteBlocking{output, 0},
            LoadConstant{1, PackedLogic4::from_msb_string("0")},
            WriteUpdate{output, 1},
            WaitFor{5},
            LoadConstant{2, PackedLogic4::from_msb_string("1")},
            WriteAfter{output, 2, 2},
            Halt{},
        };
        return process;
      };

  Interpreter reference;
  const auto reference_input = reference.add_signal(
      {"top.input", PackedLogic4::from_msb_string("1")});
  const auto reference_output = reference.add_signal(
      {"top.output", PackedLogic4::from_msb_string("X")});
  (void)reference.add_process(
      make_process(reference_input, reference_output));
  std::vector<std::pair<SimulationTick, std::string>> reference_changes;
  reference.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        if (signal == reference_output) {
          reference_changes.emplace_back(time, value.to_msb_string());
        }
      });
  const auto reference_result = reference.run();

  class SignalExecutor final : public ProcessExecutor {
  public:
    SignalExecutor(const SignalId input, const SignalId output)
        : input_(input), output_(output) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override {
      starts.push_back(start);
      if (calls == 0) {
        require(start == 0, "alternate executor initial PC");
        const auto input = context.read_signal_word(input_);
        context.write_blocking_word(output_, input);
        context.write_update_word(
            output_, Logic4Word{1, 0, 0});
        ++calls;
        return {4, 5};
      }
      require(calls == 1 && start == 5, "alternate executor resumed PC");
      context.write_after_word(
          output_, Logic4Word{1, 1, 0}, 2);
      ++calls;
      return {7, 8};
    }

    std::size_t calls{};
    std::vector<InstructionIndex> starts;

  private:
    SignalId input_{};
    SignalId output_{};
  };

  Interpreter alternate;
  const auto alternate_input = alternate.add_signal(
      {"top.input", PackedLogic4::from_msb_string("1")});
  const auto alternate_output = alternate.add_signal(
      {"top.output", PackedLogic4::from_msb_string("X")});
  const auto alternate_process = alternate.add_process(
      make_process(alternate_input, alternate_output));
  auto executor =
      std::make_unique<SignalExecutor>(
          alternate_input, alternate_output);
  auto* const executor_probe = executor.get();
  alternate.set_process_executor(
      alternate_process, std::move(executor));
  std::vector<std::pair<SimulationTick, std::string>> alternate_changes;
  alternate.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        if (signal == alternate_output) {
          alternate_changes.emplace_back(time, value.to_msb_string());
        }
      });
  const auto alternate_result = alternate.run();

  require(
      reference_result.status == RunStatus::completed
          && alternate_result.status == reference_result.status
          && alternate_result.time == reference_result.time
          && alternate_result.delta == reference_result.delta,
      "alternate executor must preserve run completion state");
  require(
      alternate.signal_value(alternate_output)
              == reference.signal_value(reference_output)
          && alternate_changes == reference_changes,
      "alternate executor context writes must match the interpreter");
  require(
      executor_probe->calls == 2
          && executor_probe->starts
              == std::vector<InstructionIndex>{0, 5},
      "alternate executor frame must resume at the canonical PC");

  class BoundaryExecutor final : public ProcessExecutor {
  public:
    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext&,
        const InstructionIndex start) override {
      starts.push_back(start);
      if (start == 0) {
        return {0, 1};
      }
      if (start == 1) {
        return {1, 2};
      }
      require(start == 2, "stop executor resume PC");
      return {2, 3};
    }

    std::vector<InstructionIndex> starts;
  };

  const auto add_wait_yield_stop = [](Interpreter& interpreter) {
    Process process;
    process.id = 0;
    process.name = "wait_yield_stop";
    process.operations = {WaitFor{1}, Yield{}, Stop{}};
    return interpreter.add_process(std::move(process));
  };

  Interpreter reference_stop;
  (void)add_wait_yield_stop(reference_stop);
  const auto reference_stop_result = reference_stop.run();

  Interpreter alternate_stop;
  const auto alternate_stop_process =
      add_wait_yield_stop(alternate_stop);
  auto boundary_executor = std::make_unique<BoundaryExecutor>();
  auto* const boundary_probe = boundary_executor.get();
  alternate_stop.set_process_executor(
      alternate_stop_process, std::move(boundary_executor));
  const auto alternate_stop_result = alternate_stop.run();
  require(
      alternate_stop_result.status == reference_stop_result.status
          && alternate_stop_result.time == reference_stop_result.time
          && alternate_stop_result.delta == reference_stop_result.delta
          && alternate_stop.stopped_by_design()
          && reference_stop.stopped_by_design(),
      "alternate WaitFor/Yield/Stop boundaries must match the interpreter");
  require(
      boundary_probe->starts
          == std::vector<InstructionIndex>{0, 1, 2},
      "alternate boundary executor resume sequence");
}

void test_simir_alternate_executor_dynamic_wait() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  struct Change {
    SimulationTick time{};
    std::uint64_t delta{};
    std::string value;

    bool operator==(const Change&) const = default;
  };

  const auto make_driver = [](const SignalId trigger) {
    Process process;
    process.id = 0;
    process.name = "dynamic_wait_driver";
    process.register_count = 1;
    process.operations = {
        LoadConstant{0, PackedLogic4::from_msb_string("0")},
        WaitFor{1},
        WriteBlocking{trigger, 0},
        WaitFor{1},
        LoadConstant{0, PackedLogic4::from_msb_string("1")},
        WriteBlocking{trigger, 0},
        WaitFor{1},
        LoadConstant{0, PackedLogic4::from_msb_string("0")},
        WriteBlocking{trigger, 0},
        Halt{},
    };
    return process;
  };
  const auto make_waiter =
      [](const SignalId trigger, const SignalId output) {
        Process process;
        process.id = 1;
        process.name = "dynamic_waiter";
        process.register_count = 1;
        process.operations = {
            WaitOn{
                {trigger, trigger},
                {EdgeKind::posedge, EdgeKind::posedge}},
            ReadSignal{0, trigger},
            WriteUpdate{output, 0},
            WaitOn{{trigger}, {EdgeKind::negedge}},
            ReadSignal{0, trigger},
            WriteUpdate{output, 0},
            Halt{},
        };
        return process;
      };

  Interpreter reference;
  const auto reference_trigger = reference.add_signal(
      {"top.trigger", PackedLogic4::from_msb_string("1")});
  const auto reference_output = reference.add_signal(
      {"top.output", PackedLogic4::from_msb_string("X")});
  (void)reference.add_process(make_driver(reference_trigger));
  (void)reference.add_process(
      make_waiter(reference_trigger, reference_output));
  std::vector<Change> reference_changes;
  reference.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        if (signal == reference_output) {
          reference_changes.push_back(
              {time, reference.scheduler().delta(),
               value.to_msb_string()});
        }
      });
  const auto reference_result = reference.run();

  class DynamicWaitExecutor final : public ProcessExecutor {
  public:
    DynamicWaitExecutor(
        const SignalId trigger, const SignalId output)
        : trigger_(trigger), output_(output) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override {
      starts.push_back(start);
      if (start == 0) {
        return {0, 1};
      }
      if (start == 1) {
        context.write_update_word(
            output_, context.read_signal_word(trigger_));
        return {3, 4};
      }
      require(start == 4, "dynamic-wait executor resume PC");
      context.write_update_word(
          output_, context.read_signal_word(trigger_));
      return {6, 7};
    }

    std::vector<InstructionIndex> starts;

  private:
    SignalId trigger_{};
    SignalId output_{};
  };

  Interpreter alternate;
  const auto alternate_trigger = alternate.add_signal(
      {"top.trigger", PackedLogic4::from_msb_string("1")});
  const auto alternate_output = alternate.add_signal(
      {"top.output", PackedLogic4::from_msb_string("X")});
  (void)alternate.add_process(make_driver(alternate_trigger));
  const auto alternate_waiter = alternate.add_process(
      make_waiter(alternate_trigger, alternate_output));
  auto executor = std::make_unique<DynamicWaitExecutor>(
      alternate_trigger, alternate_output);
  auto* const executor_probe = executor.get();
  alternate.set_process_executor(
      alternate_waiter, std::move(executor));
  std::vector<Change> alternate_changes;
  alternate.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        if (signal == alternate_output) {
          alternate_changes.push_back(
              {time, alternate.scheduler().delta(),
               value.to_msb_string()});
        }
      });
  const auto alternate_result = alternate.run();

  require(
      reference_result.status == RunStatus::completed
          && alternate_result.status == reference_result.status
          && alternate_result.time == reference_result.time
          && alternate_result.delta == reference_result.delta
          && alternate_result.callbacks_executed
              == reference_result.callbacks_executed,
      "alternate dynamic wait must preserve run completion state");
  const std::vector<Change> expected{
      {2, 1, "1"},
      {3, 1, "0"},
  };
  require(
      reference_changes == expected
          && alternate_changes == reference_changes
          && alternate.signal_value(alternate_output)
              == reference.signal_value(reference_output),
      "alternate dynamic wait wakeups must match the interpreter");
  require(
      executor_probe->starts
          == std::vector<InstructionIndex>{0, 1, 4},
      "alternate dynamic-wait frame resume sequence");

  Interpreter invalid_dynamic_edge;
  const auto vector_trigger = invalid_dynamic_edge.add_signal(
      {"top.vector_trigger",
       PackedLogic4::from_msb_string("00000000")});
  Process invalid_waiter;
  invalid_waiter.id = 0;
  invalid_waiter.name = "invalid_dynamic_edge";
  invalid_waiter.operations = {
      WaitOn{{vector_trigger}, {EdgeKind::posedge}}, Halt{}};
  (void)invalid_dynamic_edge.add_process(
      std::move(invalid_waiter));
  try {
    (void)invalid_dynamic_edge.run();
    throw std::runtime_error(
        "a vector dynamic edge wait was accepted");
  } catch (const InterpreterError& error) {
    require(
        std::string_view{error.what()}.find(
            "WaitOn edge requires a scalar signal")
            != std::string_view::npos,
        "dynamic edge width diagnostic");
  }
}

void test_simir_timed_dynamic_wait_rearm() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto trigger = interpreter.add_signal(
      {"top.trigger", PackedLogic4::from_msb_string("0")});
  const auto observed = interpreter.add_signal(
      {"top.observed", PackedLogic4::from_msb_string("0")});

  Process driver;
  driver.id = 0;
  driver.name = "timed_wait_driver";
  driver.register_count = 1;
  driver.operations = {
      WaitFor{1},
      LoadConstant{
          0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{trigger, 0},
      Halt{},
  };
  (void)interpreter.add_process(std::move(driver));

  WaitOn initial_wait{{trigger}};
  initial_wait.timeout = 2;
  initial_wait.timeout_result = 0;
  WaitOn rearmed_wait{{trigger}};
  rearmed_wait.timeout = 2;
  rearmed_wait.timeout_result = 0;
  rearmed_wait.timeout_origin = 0;
  Process waiter;
  waiter.id = 1;
  waiter.name = "timed_wait_observer";
  waiter.register_count = 2;
  waiter.operations = {
      std::move(initial_wait),
      Branch{
          0, 4, 2, UnknownBranchPolicy::error},
      std::move(rearmed_wait),
      Jump{1},
      LoadConstant{
          1, PackedLogic4::from_msb_string("1")},
      WriteBlocking{observed, 1},
      Halt{},
  };
  (void)interpreter.add_process(std::move(waiter));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed
          && result.time == 2,
      "a false event wake must preserve the original WaitOn timeout");
  require(
      interpreter.signal_value(observed).to_msb_string() == "1",
      "the WaitOn timeout result register must select timeout resumption");

  Interpreter invalid_rearm;
  const auto invalid_trigger = invalid_rearm.add_signal(
      {"top.trigger", PackedLogic4::from_msb_string("0")});
  WaitOn invalid_origin{{invalid_trigger}};
  invalid_origin.timeout = 1;
  invalid_origin.timeout_result = 0;
  WaitOn invalid_continuation{{invalid_trigger}};
  invalid_continuation.timeout = 2;
  invalid_continuation.timeout_result = 0;
  invalid_continuation.timeout_origin = 0;
  Process invalid_process;
  invalid_process.id = 0;
  invalid_process.name = "invalid_timed_wait_rearm";
  invalid_process.register_count = 1;
  invalid_process.operations = {
      std::move(invalid_origin),
      std::move(invalid_continuation),
      Halt{},
  };
  (void)invalid_rearm.add_process(
      std::move(invalid_process));
  try {
    (void)invalid_rearm.run();
    throw std::runtime_error{
        "a mismatched WaitOn timeout rearm was accepted"};
  } catch (const InterpreterError& error) {
    require(
        std::string_view{error.what()}.find(
            "WaitOn timeout rearm does not match its origin")
            != std::string_view::npos,
        "mismatched WaitOn timeout rearm diagnostic");
  }
}

void test_simir_alternate_executor_scheduled_word_writes() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto make_process =
      [](const SignalId update_output,
         const SignalId zero_delay_output,
         const SignalId delayed_output) {
        Process process;
        process.id = 0;
        process.name = "scheduled_word_writes";
        process.register_count = 1;
        process.operations = {
            LoadConstant{0, PackedLogic4::from_msb_string("1")},
            WriteUpdate{update_output, 0},
            WriteAfter{zero_delay_output, 0, 0},
            WriteAfter{delayed_output, 0, 3},
            Halt{},
        };
        return process;
      };

  struct Change {
    SignalId signal{};
    SimulationTick time{};
    std::uint64_t delta{};
    SchedulerPhase phase = SchedulerPhase::active;
    std::string value;

    bool operator==(const Change&) const = default;
  };

  Interpreter reference;
  const auto reference_update = reference.add_signal(
      {"top.update", PackedLogic4::from_msb_string("0")});
  const auto reference_zero = reference.add_signal(
      {"top.zero_delay", PackedLogic4::from_msb_string("0")});
  const auto reference_delayed = reference.add_signal(
      {"top.delayed", PackedLogic4::from_msb_string("0")});
  (void)reference.add_process(make_process(
      reference_update, reference_zero, reference_delayed));
  std::vector<Change> reference_changes;
  reference.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        const auto phase = reference.scheduler().current_phase();
        require(
            phase.has_value(),
            "reference scheduled write must occur in a phase");
        reference_changes.push_back(
            {signal,
             time,
             reference.scheduler().delta(),
             *phase,
             value.to_msb_string()});
      });
  const auto reference_result = reference.run();

  class ScheduledExecutor final : public ProcessExecutor {
  public:
    ScheduledExecutor(
        const SignalId update_output,
        const SignalId zero_delay_output,
        const SignalId delayed_output)
        : update_output_(update_output),
          zero_delay_output_(zero_delay_output),
          delayed_output_(delayed_output) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override {
      require(start == 0, "scheduled executor initial PC");
      const Logic4Word one{1, 1, 0};
      context.write_update_word(update_output_, one);
      context.write_after_word(zero_delay_output_, one, 0);
      context.write_after_word(delayed_output_, one, 3);
      require(
          context.read_signal_word(update_output_).aval == 0
              && context.read_signal_word(zero_delay_output_).aval == 0
              && context.read_signal_word(delayed_output_).aval == 0,
          "scheduled writes must not be visible during the active process");
      return {4, 5};
    }

  private:
    SignalId update_output_{};
    SignalId zero_delay_output_{};
    SignalId delayed_output_{};
  };

  Interpreter alternate;
  const auto alternate_update = alternate.add_signal(
      {"top.update", PackedLogic4::from_msb_string("0")});
  const auto alternate_zero = alternate.add_signal(
      {"top.zero_delay", PackedLogic4::from_msb_string("0")});
  const auto alternate_delayed = alternate.add_signal(
      {"top.delayed", PackedLogic4::from_msb_string("0")});
  const auto alternate_process = alternate.add_process(make_process(
      alternate_update, alternate_zero, alternate_delayed));
  alternate.set_process_executor(
      alternate_process,
      std::make_unique<ScheduledExecutor>(
          alternate_update, alternate_zero, alternate_delayed));
  std::vector<Change> alternate_changes;
  alternate.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        const auto phase = alternate.scheduler().current_phase();
        require(
            phase.has_value(),
            "alternate scheduled write must occur in a phase");
        alternate_changes.push_back(
            {signal,
             time,
             alternate.scheduler().delta(),
             *phase,
             value.to_msb_string()});
      });
  const auto alternate_result = alternate.run();

  const std::vector<Change> expected_changes = {
      {reference_update, 0, 0, SchedulerPhase::update, "1"},
      {reference_zero, 0, 0, SchedulerPhase::update, "1"},
      {reference_delayed, 3, 0, SchedulerPhase::update, "1"},
  };
  require(
      reference_changes == expected_changes,
      "interpreted update and delayed writes must commit in update phases");
  require(
      alternate_result.status == reference_result.status
          && alternate_result.time == reference_result.time
          && alternate_result.delta == reference_result.delta
          && alternate_result.callbacks_executed
              == reference_result.callbacks_executed
          && alternate_changes == reference_changes,
      "external scheduled word writes must match interpreter scheduling");
  require(
      alternate.signal_value(alternate_update).to_msb_string() == "1"
          && alternate.signal_value(alternate_zero).to_msb_string() == "1"
          && alternate.signal_value(alternate_delayed).to_msb_string() == "1",
      "external scheduled word writes must publish their final values");
}

void test_simir_alternate_executor_zero_delay_and_frame() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto make_process =
      [](const SignalId active_output, const SignalId inactive_output) {
        Process process;
        process.id = 0;
        process.name = "zero_delay_frame";
        process.register_count = 1;
        process.operations = {
            LoadConstant{0, PackedLogic4::from_msb_string("1")},
            WriteBlocking{active_output, 0},
            WaitFor{0},
            WriteBlocking{inactive_output, 0},
            Halt{},
        };
        return process;
      };

  using Change =
      std::pair<SignalId, std::pair<SchedulerPhase, std::string>>;

  Interpreter reference;
  const auto reference_active = reference.add_signal(
      {"top.active_output", PackedLogic4::from_msb_string("0")});
  const auto reference_inactive = reference.add_signal(
      {"top.inactive_output", PackedLogic4::from_msb_string("0")});
  (void)reference.add_process(
      make_process(reference_active, reference_inactive));
  std::vector<Change> reference_changes;
  reference.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick) {
        const auto phase = reference.scheduler().current_phase();
        require(phase.has_value(), "reference change must occur in a phase");
        reference_changes.emplace_back(
            signal, std::pair{*phase, value.to_msb_string()});
      });
  const auto reference_result = reference.run();

  class ZeroDelayExecutor final : public ProcessExecutor {
  public:
    ZeroDelayExecutor(
        Scheduler& scheduler,
        const SignalId active_output,
        const SignalId inactive_output)
        : scheduler_(scheduler),
          active_output_(active_output),
          inactive_output_(inactive_output) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        const InstructionIndex start) override {
      starts.push_back(start);
      const auto phase = scheduler_.current_phase();
      require(phase.has_value(), "alternate resume must occur in a phase");
      phases.push_back(*phase);
      if (start == 0) {
        frame_register_ = PackedLogic4::from_msb_string("1");
        context.write_blocking(active_output_, frame_register_);
        return {2, 3};
      }
      require(start == 3, "zero-delay executor resumed at the wrong PC");
      context.write_blocking(inactive_output_, frame_register_);
      return {4, 5};
    }

    std::vector<InstructionIndex> starts;
    std::vector<SchedulerPhase> phases;

  private:
    Scheduler& scheduler_;
    SignalId active_output_{};
    SignalId inactive_output_{};
    PackedLogic4 frame_register_ =
        PackedLogic4::from_msb_string("X");
  };

  Interpreter alternate;
  const auto alternate_active = alternate.add_signal(
      {"top.active_output", PackedLogic4::from_msb_string("0")});
  const auto alternate_inactive = alternate.add_signal(
      {"top.inactive_output", PackedLogic4::from_msb_string("0")});
  const auto alternate_process = alternate.add_process(
      make_process(alternate_active, alternate_inactive));
  auto executor = std::make_unique<ZeroDelayExecutor>(
      alternate.scheduler(), alternate_active, alternate_inactive);
  auto* const executor_probe = executor.get();
  alternate.set_process_executor(
      alternate_process, std::move(executor));
  std::vector<Change> alternate_changes;
  alternate.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick) {
        const auto phase = alternate.scheduler().current_phase();
        require(phase.has_value(), "alternate change must occur in a phase");
        alternate_changes.emplace_back(
            signal, std::pair{*phase, value.to_msb_string()});
      });
  const auto alternate_result = alternate.run();

  const std::vector<Change> expected_changes = {
      {reference_active, {SchedulerPhase::active, "1"}},
      {reference_inactive, {SchedulerPhase::inactive, "1"}},
  };
  require(
      reference_changes == expected_changes,
      "WaitFor{0} must resume the interpreter in the inactive phase");
  require(
      reference_result.status == RunStatus::completed
          && reference_result.time == 0
          && reference_result.delta == 0
          && alternate_result.status == reference_result.status
          && alternate_result.time == reference_result.time
          && alternate_result.delta == reference_result.delta
          && alternate_result.callbacks_executed
              == reference_result.callbacks_executed
          && alternate_changes == reference_changes,
      "alternate WaitFor{0} behavior must match the interpreter");
  require(
      executor_probe->starts
              == std::vector<InstructionIndex>{0, 3}
          && executor_probe->phases
              == std::vector<SchedulerPhase>{
                  SchedulerPhase::active, SchedulerPhase::inactive}
          && alternate.signal_value(alternate_inactive)
              == reference.signal_value(reference_inactive),
      "alternate frame state and resume PC must persist across WaitFor{0}");
}

void test_simir_alternate_executor_cpp_exception_containment() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  class ContainingExecutor final : public ProcessExecutor {
  public:
    explicit ContainingExecutor(const SignalId signal) : signal_(signal) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override {
      std::exception_ptr callback_failure;
      const auto callback = [&]() noexcept {
        try {
          context.write_blocking_word(
              signal_, Logic4Word{2, UINT64_C(0b10), 0});
        } catch (...) {
          callback_failure = std::current_exception();
        }
      };
      callback();
      require(
          callback_failure != nullptr,
          "failing context callback was not contained");
      std::rethrow_exception(callback_failure);
    }

  private:
    SignalId signal_{};
  };

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"top.callback_failure", PackedLogic4::from_msb_string("0")});
  Process process;
  process.id = 0;
  process.name = "callback_failure";
  process.operations = {Halt{}};
  const auto process_id =
      interpreter.add_process(std::move(process));
  interpreter.set_process_executor(
      process_id, std::make_unique<ContainingExecutor>(signal));

  try {
    (void)interpreter.run();
    throw std::runtime_error(
        "contained executor callback failure was not rethrown");
  } catch (const std::invalid_argument& error) {
    require(
        std::string_view{error.what()}.find("width mismatch")
            != std::string_view::npos,
        "contained executor callback diagnostic");
  }
  require(
      !interpreter.scheduler().running()
          && interpreter.signal_value(signal).to_msb_string() == "0",
      "executor exception must leave the scheduler and signal state valid");
}

void test_simir_alternate_executor_validation() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  class FixedExecutor final : public ProcessExecutor {
  public:
    explicit FixedExecutor(const ProcessResumeResult result)
        : result_(result) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext&,
        InstructionIndex) override {
      return result_;
    }

  private:
    ProcessResumeResult result_;
  };

  Interpreter invalid_boundary;
  Process process;
  process.id = 0;
  process.name = "invalid_boundary";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      Halt{},
  };
  const auto process_id =
      invalid_boundary.add_process(std::move(process));
  invalid_boundary.set_process_executor(
      process_id,
      std::make_unique<FixedExecutor>(ProcessResumeResult{0, 1}));
  try {
    (void)invalid_boundary.run();
    throw std::runtime_error(
        "a non-boundary executor result was accepted");
  } catch (const InterpreterError& error) {
    require(
        error.instruction() == 0
            && std::string_view{error.what()}.find(
                   "not a kernel boundary")
                != std::string_view::npos,
        "invalid executor boundary diagnostic");
  }

  Interpreter late_install;
  Process halt;
  halt.id = 0;
  halt.name = "late_install";
  halt.operations = {Halt{}};
  const auto halt_id = late_install.add_process(std::move(halt));
  late_install.start();
  try {
    late_install.set_process_executor(
        halt_id,
        std::make_unique<FixedExecutor>(
            ProcessResumeResult{0, 1}));
    throw std::runtime_error("late executor installation was accepted");
  } catch (const std::logic_error& error) {
    require(
        std::string_view{error.what()}.find("after start")
            != std::string_view::npos,
        "late executor installation diagnostic");
  }
}

void test_simir_alternate_executor_event_replacement_and_cancel() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  class Producer final : public ProcessExecutor {
  public:
    explicit Producer(const SignalId event) : event_(event) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override {
      ProcessResumeResult result{0, 1};
      switch (state_++) {
      case 0:
        context.notify_event(
            event_, 5, EventNotificationKind::timed);
        context.notify_event(
            event_, 7, EventNotificationKind::timed);
        context.notify_event(
            event_, 3, EventNotificationKind::timed);
        result.external.kind = ExternalSuspendKind::wait_for;
        result.external.delay = 4;
        break;
      case 1:
        context.notify_event(
            event_, 2, EventNotificationKind::timed);
        result.external.kind = ExternalSuspendKind::wait_for;
        result.external.delay = 1;
        break;
      case 2:
        context.cancel_event(event_);
        result.external.kind = ExternalSuspendKind::wait_for;
        result.external.delay = 1;
        break;
      default:
        context.notify_event(
            event_, 0, EventNotificationKind::immediate);
        result.external.kind = ExternalSuspendKind::halt;
        break;
      }
      return result;
    }

  private:
    SignalId event_{};
    std::size_t state_{};
  };

  class Consumer final : public ProcessExecutor {
  public:
    Consumer(
        const SignalId event,
        std::size_t& notifications)
        : event_(event), notifications_(notifications) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext&,
        InstructionIndex) override {
      if (initialized_) {
        ++notifications_;
      }
      initialized_ = true;
      ProcessResumeResult result{0, 1};
      result.external.kind = ExternalSuspendKind::wait_on;
      result.external.sensitivity.push_back(
          {event_, EdgeKind::any});
      return result;
    }

  private:
    SignalId event_{};
    std::size_t& notifications_;
    bool initialized_{};
  };

  Interpreter interpreter;
  const auto event = interpreter.add_signal(
      {"event", PackedLogic4::from_msb_string("0")});

  Process producer;
  producer.id = 0;
  producer.name = "event_producer";
  producer.operations = {Halt{}};
  const auto producer_id =
      interpreter.add_process(std::move(producer));

  Process consumer;
  consumer.id = 1;
  consumer.name = "event_consumer";
  consumer.operations = {Halt{}};
  const auto consumer_id =
      interpreter.add_process(std::move(consumer));

  std::size_t notifications = 0;
  interpreter.set_process_executor(
      producer_id, std::make_unique<Producer>(event));
  interpreter.set_process_executor(
      consumer_id,
      std::make_unique<Consumer>(event, notifications));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 6,
      "replaced and canceled event notifications must not run");
  require(
      notifications == 2,
      "only the earliest timed and final immediate event must trigger");
}

void test_simir_alternate_executor_notify_delayed() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  class Producer final : public ProcessExecutor {
  public:
    Producer(
        const SignalId event,
        bool& duplicate_rejected)
        : event_(event),
          duplicate_rejected_(duplicate_rejected) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override {
      ProcessResumeResult result{0, 1};
      if (state_++ == 0) {
        context.notify_event(
            event_, 2, EventNotificationKind::delayed);
        try {
          context.notify_event(
              event_, 1, EventNotificationKind::delayed);
        } catch (const std::logic_error&) {
          duplicate_rejected_ = true;
        }
        result.external.kind = ExternalSuspendKind::wait_for;
        result.external.delay = 1;
      } else {
        context.cancel_event(event_);
        context.notify_event(
            event_, 0, EventNotificationKind::delayed);
        result.external.kind = ExternalSuspendKind::halt;
      }
      return result;
    }

  private:
    SignalId event_{};
    bool& duplicate_rejected_;
    std::size_t state_{};
  };

  class Consumer final : public ProcessExecutor {
  public:
    Consumer(
        const SignalId event,
        std::size_t& notifications)
        : event_(event), notifications_(notifications) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext&,
        InstructionIndex) override {
      ProcessResumeResult result{0, 1};
      if (initialized_) {
        ++notifications_;
        result.external.kind = ExternalSuspendKind::halt;
      } else {
        initialized_ = true;
        result.external.kind = ExternalSuspendKind::wait_on;
        result.external.sensitivity.push_back(
            {event_, EdgeKind::any});
      }
      return result;
    }

  private:
    SignalId event_{};
    std::size_t& notifications_;
    bool initialized_{};
  };

  Interpreter interpreter;
  const auto event = interpreter.add_signal(
      {"delayed_event", PackedLogic4::from_msb_string("0")});

  Process producer;
  producer.id = 0;
  producer.name = "notify_delayed_producer";
  producer.operations = {Halt{}};
  const auto producer_id =
      interpreter.add_process(std::move(producer));

  Process consumer;
  consumer.id = 1;
  consumer.name = "notify_delayed_consumer";
  consumer.operations = {Halt{}};
  const auto consumer_id =
      interpreter.add_process(std::move(consumer));

  bool duplicate_rejected = false;
  std::size_t notifications = 0;
  interpreter.set_process_executor(
      producer_id,
      std::make_unique<Producer>(
          event, duplicate_rejected));
  interpreter.set_process_executor(
      consumer_id,
      std::make_unique<Consumer>(event, notifications));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 2,
      "canceled notify_delayed entries must drain harmlessly");
  require(
      duplicate_rejected,
      "notify_delayed must reject an event with a pending notification");
  require(
      notifications == 1,
      "the replacement delta notify_delayed must trigger exactly once");
}

void test_simir_alternate_executor_primitive_channel_updates() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  class ChannelExecutor final : public ProcessExecutor {
  public:
    ChannelExecutor(
        const SignalId output,
        std::vector<std::uint64_t>& updates)
        : output_(output), updates_(updates) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override {
      context.request_channel_update(10);
      context.request_channel_update(10);
      context.request_channel_update(20);
      ProcessResumeResult result{0, 1};
      result.external.kind = ExternalSuspendKind::halt;
      return result;
    }

    void update_channel(
        const std::uint64_t channel,
        ProcessExecutionContext& context) override {
      updates_.push_back(channel);
      context.write_update(
          output_,
          PackedLogic4::from_msb_string(
              channel == 10 ? "01"
              : channel == 20 ? "10"
                              : "11"));
      if (channel == 10) {
        context.request_channel_update(10);
        context.request_channel_update(30);
      }
    }

  private:
    SignalId output_{};
    std::vector<std::uint64_t>& updates_;
  };

  Interpreter interpreter;
  const auto output = interpreter.add_signal(
      {"channel_output", PackedLogic4::from_msb_string("00")});
  Process process;
  process.id = 0;
  process.name = "primitive_channel_requester";
  process.operations = {Halt{}};
  const auto process_id =
      interpreter.add_process(std::move(process));

  std::vector<std::uint64_t> updates;
  std::size_t commits = 0;
  interpreter.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4&,
          const SimulationTick) {
        if (signal == output) {
          ++commits;
        }
      });
  interpreter.set_process_executor(
      process_id,
      std::make_unique<ChannelExecutor>(output, updates));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed
          && result.time == 0,
      "updates requested during the update phase must enter the next delta");
  require(
      updates == std::vector<std::uint64_t>{10, 20, 30},
      "primitive-channel updates must be deduplicated and stably ordered");
  require(
      commits == 2,
      "an update-phase request must commit in a later delta");
  require(
      interpreter.signal_value(output).to_msb_string() == "11",
      "primitive-channel writes must commit through the common update phase");
}

void test_simir_alternate_executor_signal_event_window() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  class Producer final : public ProcessExecutor {
  public:
    Producer(
        const SignalId signal,
        bool& expired)
        : signal_(signal), expired_(expired) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override {
      ProcessResumeResult result{0, 1};
      if (state_++ == 0) {
        context.write_update(
            signal_, PackedLogic4::from_msb_string("1"));
        result.external.kind = ExternalSuspendKind::wait_for;
        result.external.delay = 2;
      } else {
        expired_ = !context.signal_event(signal_);
        result.external.kind = ExternalSuspendKind::halt;
      }
      return result;
    }

  private:
    SignalId signal_{};
    bool& expired_;
    std::size_t state_{};
  };

  class Consumer final : public ProcessExecutor {
  public:
    Consumer(
        const SignalId signal,
        bool& observed)
        : signal_(signal), observed_(observed) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override {
      observed_ = context.signal_event(signal_);
      ProcessResumeResult result{0, 1};
      result.external.kind = ExternalSuspendKind::halt;
      return result;
    }

  private:
    SignalId signal_{};
    bool& observed_;
  };

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"event_window", PackedLogic4::from_msb_string("0")});

  Process producer;
  producer.id = 0;
  producer.name = "signal_event_producer";
  producer.operations = {Halt{}};
  const auto producer_id =
      interpreter.add_process(std::move(producer));

  Process consumer;
  consumer.id = 1;
  consumer.name = "signal_event_consumer";
  consumer.initialize = false;
  consumer.static_sensitivity.push_back(
      {signal, EdgeKind::any});
  consumer.operations = {WaitSensitivity{}};
  const auto consumer_id =
      interpreter.add_process(std::move(consumer));

  bool observed = false;
  bool expired = false;
  interpreter.set_process_executor(
      producer_id,
      std::make_unique<Producer>(signal, expired));
  interpreter.set_process_executor(
      consumer_id,
      std::make_unique<Consumer>(signal, observed));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 2,
      "signal event-window test must reach its later observation");
  require(
      observed,
      "a signal event must be visible in the awakened evaluation delta");
  require(
      expired,
      "a signal event must expire after its awakened evaluation delta");
}

void test_simir_alternate_executor_event_lists() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  class Producer final : public ProcessExecutor {
  public:
    explicit Producer(const std::array<SignalId, 3> events)
        : events_(events) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext& context,
        InstructionIndex) override {
      for (std::size_t index = 0; index < events_.size(); ++index) {
        context.notify_event(
            events_[index],
            static_cast<SimulationTick>(index + 1),
            EventNotificationKind::timed);
      }
      ProcessResumeResult result{0, 1};
      result.external.kind = ExternalSuspendKind::halt;
      return result;
    }

  private:
    std::array<SignalId, 3> events_;
  };

  class ListConsumer final : public ProcessExecutor {
  public:
    ListConsumer(
        std::vector<SignalId> events,
        const bool wait_all,
        std::size_t& wakeups)
        : events_(std::move(events)),
          wait_all_(wait_all),
          wakeups_(wakeups) {}

    [[nodiscard]] ProcessResumeResult resume(
        ProcessExecutionContext&,
        InstructionIndex) override {
      ProcessResumeResult result{0, 1};
      if (initialized_) {
        ++wakeups_;
        result.external.kind = ExternalSuspendKind::halt;
        return result;
      }
      initialized_ = true;
      result.external.kind = ExternalSuspendKind::wait_on;
      result.external.wait_all = wait_all_;
      for (const auto event : events_) {
        result.external.sensitivity.push_back(
            {event, EdgeKind::any});
      }
      return result;
    }

  private:
    std::vector<SignalId> events_;
    bool wait_all_{};
    std::size_t& wakeups_;
    bool initialized_{};
  };

  Interpreter interpreter;
  const std::array events{
      interpreter.add_signal(
          {"event_a", PackedLogic4::from_msb_string("0")}),
      interpreter.add_signal(
          {"event_b", PackedLogic4::from_msb_string("0")}),
      interpreter.add_signal(
          {"event_c", PackedLogic4::from_msb_string("0")}),
  };

  Process producer;
  producer.id = 0;
  producer.name = "event_list_producer";
  producer.operations = {Halt{}};
  const auto producer_id =
      interpreter.add_process(std::move(producer));

  Process or_consumer;
  or_consumer.id = 1;
  or_consumer.name = "event_or_consumer";
  or_consumer.operations = {Halt{}};
  const auto or_id =
      interpreter.add_process(std::move(or_consumer));

  Process and_consumer;
  and_consumer.id = 2;
  and_consumer.name = "event_and_consumer";
  and_consumer.operations = {Halt{}};
  const auto and_id =
      interpreter.add_process(std::move(and_consumer));

  std::size_t or_wakeups = 0;
  std::size_t and_wakeups = 0;
  interpreter.set_process_executor(
      producer_id, std::make_unique<Producer>(events));
  interpreter.set_process_executor(
      or_id,
      std::make_unique<ListConsumer>(
          std::vector<SignalId>{events[0], events[1]},
          false,
          or_wakeups));
  interpreter.set_process_executor(
      and_id,
      std::make_unique<ListConsumer>(
          std::vector<SignalId>{
              events[0], events[1], events[2]},
          true,
          and_wakeups));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 3,
      "event-list waits must retain deterministic timed progress");
  require(
      or_wakeups == 1,
      "an OR-list wait must wake on its first event");
  require(
      and_wakeups == 1,
      "an AND-list wait must wake only after every event");
}

void test_simir_assertion_metadata() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter nonfatal_interpreter;
  std::vector<std::string> nonfatal_reports;
  nonfatal_interpreter.set_report_hook(
      [&nonfatal_reports](
          const ProcessId,
          const std::string_view message,
          const AssertionSeverity severity,
          const SourceLocation& source,
          const SimulationTick,
          const std::uint64_t) {
        require(
            severity == AssertionSeverity::error
                && source.path == "nonfatal.sv",
            "nonfatal assertion severity and source");
        nonfatal_reports.emplace_back(message);
      });
  Process nonfatal_process;
  nonfatal_process.id = 0;
  nonfatal_process.name = "nonfatal-assertion";
  nonfatal_process.register_count = 1;
  nonfatal_process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      Assert{
          0,
          "continued",
          AssertionSeverity::error,
          SourceLocation{"nonfatal.sv", 4, 3}},
      Halt{},
  };
  (void)nonfatal_interpreter.add_process(
      std::move(nonfatal_process));
  const auto nonfatal_result = nonfatal_interpreter.run();
  require(
      nonfatal_result.status == RunStatus::completed
          && nonfatal_reports
              == std::vector<std::string>{"continued"},
      "a nonfatal SimIR assertion must report once and continue");

  Interpreter interpreter;
  Process process;
  process.id = 0;
  process.name = "assertion";
  process.register_count = 1;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("0")},
      Assert{
          0,
          "metadata survived",
          AssertionSeverity::failure,
          SourceLocation{"assertions.sv", 17, 9}},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));
  try {
    (void)interpreter.run();
    throw std::runtime_error("a false SimIR assertion was accepted");
  } catch (const AssertionError& error) {
    require(error.process() == 0 && error.instruction() == 1,
            "assertion process and instruction");
    require(error.severity() == AssertionSeverity::failure,
            "assertion severity");
    require(
        !error.reported(),
        "a raw fatal assertion is reported by its boundary handler");
    require(
        error.source().path == "assertions.sv"
            && error.source().line == 17
            && error.source().column == 9,
        "assertion source location");
    require(
        std::string_view{error.what()}.find("metadata survived")
            != std::string_view::npos,
        "assertion message");
  }
}

void test_simir_execution_point_ordering() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto signal = interpreter.add_signal(
      {"value", PackedLogic4::from_msb_string("X")});
  for (ProcessId id = 0; id < 2; ++id) {
    Process process;
    process.id = id;
    process.name = "process_" + std::to_string(id);
    process.register_count = 1;
    process.operations = {
        DebugPoint{
            DebugPointKind::statement,
            SourceLocation{
                "execution_points.sv",
                static_cast<std::uint32_t>(10 + id),
                3}},
        DebugPoint{
            DebugPointKind::call,
            SourceLocation{
                "execution_points.sv",
                static_cast<std::uint32_t>(20 + id),
                7}},
        LoadConstant{
            0,
            PackedLogic4::from_msb_string(id == 0 ? "0" : "1")},
        WriteBlocking{signal, 0},
        Halt{},
    };
    (void)interpreter.add_process(std::move(process));
  }
  std::vector<ProcessId> points;
  std::vector<ExecutionPoint> all_points;
  interpreter.set_execution_point_hook(
      [&](Scheduler& scheduler, const ExecutionPoint& point) {
        all_points.push_back(point);
        if (point.kind == ExecutionPointKind::statement) {
          points.push_back(point.process);
          scheduler.request_stop();
        }
      });
  interpreter.start();
  const auto first = interpreter.run();
  require(first.status == RunStatus::stopped,
          "first statement safe point must stop");
  require(interpreter.signal_value(signal).to_msb_string() == "X",
          "a source stop occurs before its statement");

  interpreter.scheduler().clear_stop();
  const auto second = interpreter.run();
  require(second.status == RunStatus::stopped,
          "second statement safe point must stop");
  require(interpreter.signal_value(signal).to_msb_string() == "0",
          "the lower-ID process must finish before the next process stops");

  interpreter.scheduler().clear_stop();
  const auto third = interpreter.run();
  require(third.status == RunStatus::completed,
          "execution-point continuation must complete");
  require(interpreter.signal_value(signal).to_msb_string() == "1",
          "the later process must resume after the earlier process");
  require(points == std::vector<ProcessId>{0, 1},
          "execution-point process ordering");
  std::vector<ExecutionPoint> source_points;
  for (const auto& point : all_points) {
    if (point.kind == ExecutionPointKind::statement
        || point.kind == ExecutionPointKind::call) {
      source_points.push_back(point);
    }
  }
  require(
      source_points.size() == 4
          && source_points[0].kind == ExecutionPointKind::statement
          && source_points[1].kind == ExecutionPointKind::call
          && source_points[1].source.line == 20
          && source_points[1].source.column == 7
          && source_points[2].kind == ExecutionPointKind::statement
          && source_points[3].kind == ExecutionPointKind::call
          && source_points[3].source.line == 21,
      "call execution-point kind and source ordering");
}

void test_simir_display_output() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  Process process;
  process.name = "display";
  process.operations = {
      Display{"first", true},
      Display{"postponed", true, true},
      WaitFor{2},
      Display{"tail", false},
      Halt{},
  };
  const auto process_id = interpreter.add_process(std::move(process));
  Process second_process;
  second_process.id = 1;
  second_process.name = "second-strobe";
  second_process.operations = {
      Display{"second-postponed", true, true},
      Halt{},
  };
  const auto second_process_id =
      interpreter.add_process(std::move(second_process));
  struct Event {
    ProcessId process{};
    std::string text;
    bool newline{};
    SimulationTick time{};
    std::uint64_t delta{};
    SchedulerPhase phase{SchedulerPhase::active};
  };
  std::vector<Event> events;
  interpreter.set_output_hook(
      [&events, &interpreter](
          const ProcessId process_value,
          const std::string_view text,
          const bool newline,
          const SimulationTick time,
          const std::uint64_t delta) {
        events.push_back(
            {
                process_value,
                std::string{text},
                newline,
                time,
                delta,
                interpreter.scheduler()
                    .current_phase()
                    .value_or(SchedulerPhase::active)});
      });
  interpreter.start();
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "display process must complete");
  require(
      events.size() == 4
          && events[0].process == process_id
          && events[0].text == "first"
          && events[0].newline
          && events[0].time == 0
          && events[0].delta == 0
          && events[0].phase == SchedulerPhase::active
          && events[1].process == process_id
          && events[1].text == "postponed"
          && events[1].newline
          && events[1].time == 0
          && events[1].delta == 0
          && events[1].phase == SchedulerPhase::postponed
          && events[2].process == second_process_id
          && events[2].text == "second-postponed"
          && events[2].newline
          && events[2].time == 0
          && events[2].delta == 0
          && events[2].phase == SchedulerPhase::postponed
          && events[3].process == process_id
          && events[3].text == "tail"
          && !events[3].newline
          && events[3].time == 2
          && events[3].delta == 0
          && events[3].phase == SchedulerPhase::active,
      "immediate/postponed display hook ordering and metadata");

  Interpreter report_interpreter;
  Process report_process;
  report_process.name = "reports";
  report_process.register_count = 6;
  report_process.operations = {
      Report{
          "warning",
          AssertionSeverity::warning,
          SourceLocation{"report.vhd", 9, 5}},
      Report{
          "error",
          AssertionSeverity::error,
          SourceLocation{"report.vhd", 10, 5}},
      LoadConstant{0, PackedLogic4::from_msb_string("10xz")},
      FormatDisplay{
          0,
          OutputFormat::binary,
          "v=",
          "!",
          true,
          false},
      LoadConstant{
          2, PackedLogic4::from_msb_string("11111111")},
      FormatDisplay{
          2,
          OutputFormat::decimal,
          "d=",
          "",
          true,
          false,
          true},
      LoadConstant{3, PackedLogic4::from_msb_string("10xz")},
      FormatDisplay{
          3,
          OutputFormat::decimal,
          "u=",
          "",
          true,
          false,
          false},
      LoadConstant{
          1, PackedLogic4::from_msb_string("10100101")},
      FormatDisplay{
          1,
          OutputFormat::hexadecimal,
          "h=",
          "",
          true,
          false},
      FormatDisplay{
          1,
          OutputFormat::character,
          "c=",
          "",
          true,
          false},
      LoadConstant{
          4, PackedLogic4::from_msb_string(
                 "01110100011001010111001101110100")},
      FormatDisplay{
          4,
          OutputFormat::string,
          "s=",
          "",
          true,
          false},
      LoadConstant{
          5, PackedLogic4::from_msb_string("0000000010100101")},
      FormatDisplay{
          5,
          OutputFormat::hexadecimal,
          "z=",
          "",
          true,
          false,
          false,
          true},
      FormatDisplay{
          1,
          OutputFormat::hexadecimal,
          "width=",
          "",
          true,
          false,
          false,
          false,
          6},
      FormatDisplay{
          1,
          OutputFormat::hexadecimal,
          "left=",
          "!",
          true,
          false,
          false,
          false,
          6,
          true},
      FormatDisplay{
          2,
          OutputFormat::decimal,
          "zero=",
          "",
          true,
          false,
          true,
          false,
          6,
          false,
          true},
      WaitFor{7},
      TimeDisplay{"time=", "", true, false, 4, false, true},
      Halt{},
  };
  const auto report_process_id =
      report_interpreter.add_process(std::move(report_process));
  struct ObservedReport {
    ProcessId process{};
    std::string message;
    AssertionSeverity severity{AssertionSeverity::note};
    SourceLocation source;
    SimulationTick time{};
    std::uint64_t delta{};
  };
  std::vector<ObservedReport> reports;
  std::vector<std::string> formatted_output;
  report_interpreter.set_report_hook(
      [&reports](
          const ProcessId process_value,
          const std::string_view message,
          const AssertionSeverity severity,
          const SourceLocation& source,
          const SimulationTick time,
          const std::uint64_t delta) {
        reports.push_back(
            {
                process_value,
                std::string{message},
                severity,
                source,
                time,
                delta});
      });
  report_interpreter.set_output_hook(
      [&formatted_output](
          const ProcessId,
          const std::string_view text,
          const bool,
          const SimulationTick,
          const std::uint64_t) {
        formatted_output.emplace_back(text);
      });
  report_interpreter.start();
  const auto report_result = report_interpreter.run();
  require(
      report_result.status == RunStatus::completed
          && reports.size() == 2
          && reports[0].process == report_process_id
          && reports[0].message == "warning"
          && reports[0].severity == AssertionSeverity::warning
          && reports[0].source.line == 9
          && reports[0].time == 0
          && reports[0].delta == 0
          && reports[1].severity == AssertionSeverity::error
          && formatted_output
              == std::vector<std::string>{
                  "v=10xz!", "d=-1", "u=x", "h=a5", "c=\xA5",
                  "s=test", "z=a5", "width=    a5",
                  "left=a5    !", "zero=-00001", "time=0007"},
      "nonfatal report hook severity, source, and ordering");

  Interpreter monitor_replacement_interpreter;
  const auto monitored_signal =
      monitor_replacement_interpreter.add_signal(
          {"watched", PackedLogic4::from_msb_string("0")});
  Process monitor_replacement_process;
  monitor_replacement_process.name = "monitor-replacement";
  monitor_replacement_process.register_count = 1;
  monitor_replacement_process.operations = {
      MonitorInstall{
          {
              MonitorValue{
                  MonitorValueKind::signal,
                  monitored_signal,
                  OutputFormat::binary,
                  "value="},
          },
          "",
          true},
      MonitorInstall{{}, "literal replacement", true},
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteBlocking{monitored_signal, 0},
      Halt{},
  };
  static_cast<void>(
      monitor_replacement_interpreter.add_process(
          std::move(monitor_replacement_process)));
  std::vector<std::string> replacement_output;
  monitor_replacement_interpreter.set_output_hook(
      [&replacement_output](
          const ProcessId,
          const std::string_view text,
          const bool,
          const SimulationTick,
          const std::uint64_t) {
        replacement_output.emplace_back(text);
      });
  monitor_replacement_interpreter.start();
  const auto monitor_replacement_result =
      monitor_replacement_interpreter.run();
  require(
      monitor_replacement_result.status == RunStatus::completed
          && replacement_output
              == std::vector<std::string>{"literal replacement"},
      "literal monitor replacement cancels the previous watched list");

  Interpreter failure_interpreter;
  Process failure_process;
  failure_process.name = "failure-report";
  failure_process.operations = {
      Report{
          "terminal",
          AssertionSeverity::failure,
          SourceLocation{"failure.vhd", 12, 7}},
      Halt{}};
  const auto failure_process_id =
      failure_interpreter.add_process(std::move(failure_process));
  std::size_t failure_reports{};
  failure_interpreter.set_report_hook(
      [&failure_reports](
          const ProcessId,
          const std::string_view,
          const AssertionSeverity,
          const SourceLocation&,
          const SimulationTick,
          const std::uint64_t) {
        ++failure_reports;
      });
  failure_interpreter.start();
  bool caught_failure = false;
  try {
    static_cast<void>(failure_interpreter.run());
  } catch (const AssertionError& error) {
    caught_failure =
        error.process() == failure_process_id
        && error.instruction() == 0
        && error.severity() == AssertionSeverity::failure
        && error.source().path == "failure.vhd"
        && std::string_view{error.what()}.find("terminal")
            != std::string_view::npos;
  }
  require(
      caught_failure && failure_reports == 1,
      "failure report callback-before-termination semantics");
}

void test_deterministic_random_values() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto run_sequence =
      [](const std::uint64_t seed) {
        Interpreter interpreter{{1000, 32}, seed};
        std::array<SignalId, 4> outputs{};
        for (std::size_t index = 0; index < outputs.size(); ++index) {
          outputs[index] = interpreter.add_signal(
              {
                  "random_" + std::to_string(index),
                  PackedLogic4(32, Logic4::zero)});
        }
        Process process;
        process.name = "random-sequence";
        process.register_count = 6;
        process.operations = {
            RandomValue{
                0,
                RandomKind::urandom,
                std::nullopt,
                std::nullopt},
            WriteBlocking{outputs[0], 0},
            RandomValue{
                1,
                RandomKind::random,
                std::nullopt,
                std::nullopt},
            WriteBlocking{outputs[1], 1},
            LoadConstant{
                2, PackedLogic4::from_aval_bval(32, 9, 0)},
            RandomValue{
                3,
                RandomKind::urandom_range,
                2,
                std::nullopt},
            WriteBlocking{outputs[2], 3},
            LoadConstant{
                4, PackedLogic4::from_aval_bval(32, 3, 0)},
            LoadConstant{
                5, PackedLogic4::from_aval_bval(32, 9, 0)},
            RandomValue{
                3,
                RandomKind::urandom_range,
                4,
                5},
            WriteBlocking{outputs[3], 3},
            Halt{},
        };
        static_cast<void>(
            interpreter.add_process(std::move(process)));
        interpreter.start();
        const auto result = interpreter.run();
        require(
            result.status == RunStatus::completed,
            "random sequence must complete");
        std::array<Logic4Word, 4> values{};
        for (std::size_t index = 0; index < outputs.size(); ++index) {
          values[index] =
              interpreter.signal_value(outputs[index]).low_word();
        }
        return values;
      };

  const auto first = run_sequence(42);
  const auto repeated = run_sequence(42);
  const auto changed_seed = run_sequence(43);
  require(
      first == repeated,
      "the same project seed must reproduce the random sequence");
  require(
      first[0] != changed_seed[0],
      "a changed project seed must change the process stream");
  require(
      first[2].bval == 0 && first[2].aval <= 9,
      "one-bound urandom_range inclusive bounds");
  require(
      first[3].bval == 0
          && first[3].aval >= 3 && first[3].aval <= 9,
      "two-bound urandom_range and reversed-bound normalization");

  const auto run_after_optional_unknown =
      [](const bool include_unknown) {
        Interpreter interpreter{{1000, 32}, 77};
        const auto random_output = interpreter.add_signal(
            {"random", PackedLogic4(32, Logic4::zero)});
        const auto unknown_output = interpreter.add_signal(
            {"unknown", PackedLogic4(32, Logic4::zero)});
        Process process;
        process.name = "unknown-bound";
        process.register_count = 3;
        if (include_unknown) {
          process.operations.emplace_back(
              LoadConstant{0, PackedLogic4(32, Logic4::x)});
          process.operations.emplace_back(
              RandomValue{
                  1,
                  RandomKind::urandom_range,
                  0,
                  std::nullopt});
          process.operations.emplace_back(
              WriteBlocking{unknown_output, 1});
        }
        process.operations.emplace_back(
            RandomValue{
                2,
                RandomKind::urandom,
                std::nullopt,
                std::nullopt});
        process.operations.emplace_back(
            WriteBlocking{random_output, 2});
        process.operations.emplace_back(Halt{});
        static_cast<void>(
            interpreter.add_process(std::move(process)));
        interpreter.start();
        static_cast<void>(interpreter.run());
        return std::pair{
            interpreter.signal_value(random_output),
            interpreter.signal_value(unknown_output)};
      };
  const auto [after_unknown, unknown_result] =
      run_after_optional_unknown(true);
  const auto [without_unknown, unused] =
      run_after_optional_unknown(false);
  static_cast<void>(unused);
  require(
      unknown_result
              == PackedLogic4(32, Logic4::x)
          && after_unknown == without_unknown,
      "unknown range bounds return X without consuming the stream");

  Interpreter per_process{{1000, 32}, 99};
  const auto first_process_output = per_process.add_signal(
      {"first", PackedLogic4(32, Logic4::zero)});
  const auto second_process_output = per_process.add_signal(
      {"second", PackedLogic4(32, Logic4::zero)});
  for (ProcessId id = 0; id < 2; ++id) {
    Process process;
    process.id = id;
    process.name = "random-process-" + std::to_string(id);
    process.register_count = 1;
    process.operations = {
        RandomValue{
            0,
            RandomKind::urandom,
            std::nullopt,
            std::nullopt},
        WriteBlocking{
            id == 0 ? first_process_output : second_process_output,
            0},
        Halt{},
    };
    static_cast<void>(
        per_process.add_process(std::move(process)));
  }
  per_process.start();
  static_cast<void>(per_process.run());
  require(
      per_process.signal_value(first_process_output)
          != per_process.signal_value(second_process_output),
      "stable process IDs derive independent random streams");
}

void test_transition_delay_selection() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const TransitionDelays delays{7, 11, 13};
  require(
      transition_delay(
          PackedLogic4::from_msb_string("0000"),
          PackedLogic4::from_msb_string("1000"),
          delays)
          == 7,
      "a rising transition selects the rise delay");
  require(
      transition_delay(
          PackedLogic4::from_msb_string("1111"),
          PackedLogic4::from_msb_string("1011"),
          delays)
          == 11,
      "a falling transition selects the fall delay");
  require(
      transition_delay(
          PackedLogic4::from_msb_string("1111"),
          PackedLogic4::from_msb_string("11Z1"),
          delays)
          == 13,
      "a high-impedance transition selects the turnoff delay");
  require(
      transition_delay(
          PackedLogic4::from_msb_string("0000"),
          PackedLogic4::from_msb_string("00X0"),
          delays)
          == 7,
      "a transition to unknown selects the shortest delay");
  require(
      transition_delay(
          PackedLogic4::from_msb_string("0000"),
          PackedLogic4::from_msb_string("1Z00"),
          delays)
          == 7,
      "a packed mixed transition selects the shortest applicable delay");
  require(
      !transition_delay(
          PackedLogic4::from_msb_string("10XZ"),
          PackedLogic4::from_msb_string("10XZ"),
          delays),
      "an unchanged packed value has no transition delay");

  bool rejected = false;
  try {
    static_cast<void>(
        transition_delay(
            PackedLogic4::from_msb_string("0"),
            PackedLogic4::from_msb_string("00"),
            delays));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "transition-delay width mismatch must be rejected");
}

void test_simir_inertial_transition_writes() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter pulse_filter{{1000, 32}};
  const auto output = pulse_filter.add_signal(
      {"output", PackedLogic4::from_msb_string("0")});
  Process pulse;
  pulse.name = "pulse-filter";
  pulse.register_count = 2;
  pulse.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteInertial{output, 0, {5, 7, 9}},
      WaitFor{2},
      LoadConstant{1, PackedLogic4::from_msb_string("0")},
      WriteInertial{output, 1, {5, 7, 9}},
      WaitFor{10},
      WriteInertial{output, 0, {5, 7, 9}},
      WaitFor{6},
      Halt{},
  };
  static_cast<void>(pulse_filter.add_process(std::move(pulse)));
  std::vector<SimulationTick> pulse_changes;
  pulse_filter.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4&,
          const SimulationTick time) {
        if (signal == output) {
          pulse_changes.push_back(time);
        }
      });
  pulse_filter.start();
  const auto pulse_result = pulse_filter.run();
  require(
      pulse_result.status == RunStatus::completed
          && pulse_filter.signal_value(output)
              == PackedLogic4::from_msb_string("1")
          && pulse_changes == std::vector<SimulationTick>{17},
      "a superseded inertial pulse is rejected before the later rise");

  Interpreter rejected_only{{1000, 32}};
  const auto rejected_output = rejected_only.add_signal(
      {"rejected-output", PackedLogic4::from_msb_string("0")});
  Process rejected_pulse;
  rejected_pulse.name = "rejected-only";
  rejected_pulse.register_count = 2;
  rejected_pulse.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteInertial{rejected_output, 0, {5, 7, 9}},
      WaitFor{2},
      LoadConstant{1, PackedLogic4::from_msb_string("0")},
      WriteInertial{rejected_output, 1, {5, 7, 9}},
      Halt{},
  };
  static_cast<void>(
      rejected_only.add_process(std::move(rejected_pulse)));
  rejected_only.start();
  const auto rejected_result = rejected_only.run();
  require(
      rejected_result.status == RunStatus::completed
          && rejected_result.time == 2
          && rejected_only.signal_value(rejected_output)
              == PackedLogic4::from_msb_string("0")
          && !rejected_only.scheduler().has_pending(),
      "a rejected pulse removes its cancelled future timestamp");

  Interpreter packed{{1000, 32}};
  const auto vector_output = packed.add_signal(
      {"vector", PackedLogic4::from_msb_string("0000")});
  Process vector;
  vector.name = "packed-transition";
  vector.register_count = 2;
  vector.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1Z00")},
      WriteInertial{vector_output, 0, {5, 7, 3}},
      WaitFor{4},
      LoadConstant{1, PackedLogic4::from_msb_string("11")},
      WriteInertialSlice{vector_output, 1, 1, {2, 6, 8}},
      WaitFor{3},
      Halt{},
  };
  static_cast<void>(packed.add_process(std::move(vector)));
  std::vector<SimulationTick> packed_changes;
  packed.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4&,
          const SimulationTick time) {
        if (signal == vector_output) {
          packed_changes.push_back(time);
        }
      });
  packed.start();
  const auto packed_result = packed.run();
  require(
      packed_result.status == RunStatus::completed
          && packed.signal_value(vector_output)
              == PackedLogic4::from_msb_string("1110")
          && packed_changes
              == std::vector<SimulationTick>{3, 6},
      "packed whole/slice writes use the shortest changed transition");
}

void test_simir_projected_writes() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter stable_rhs{{1000, 32}};
  const auto stable_output = stable_rhs.add_signal(
      {"stable-rhs", PackedLogic4::from_msb_string("0")});
  Process stable;
  stable.name = "stable-rhs";
  stable.register_count = 1;
  stable.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteInertial{stable_output, 0, {5, 5, 5}},
      WaitFor{2},
      WriteInertial{stable_output, 0, {5, 5, 5}},
      WaitFor{4},
      Halt{},
  };
  static_cast<void>(stable_rhs.add_process(std::move(stable)));
  std::vector<SimulationTick> stable_changes;
  stable_rhs.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4&,
          const SimulationTick time) {
        if (signal == stable_output) {
          stable_changes.push_back(time);
        }
      });
  stable_rhs.start();
  static_cast<void>(stable_rhs.run());
  require(
      stable_changes == std::vector<SimulationTick>{5},
      "an unchanged continuous RHS preserves its pending inertial time");

  Interpreter default_inertial{{1000, 32}};
  const auto default_output = default_inertial.add_signal(
      {"default-inertial", PackedLogic4::from_msb_string("0")});
  Process rejected;
  rejected.name = "default-inertial";
  rejected.register_count = 2;
  rejected.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteProjected{
          default_output,
          0,
          5,
          5,
          ProjectedDelayMode::inertial},
      WaitFor{2},
      LoadConstant{1, PackedLogic4::from_msb_string("0")},
      WriteProjected{
          default_output,
          1,
          5,
          5,
          ProjectedDelayMode::inertial},
      Halt{},
  };
  static_cast<void>(
      default_inertial.add_process(std::move(rejected)));
  default_inertial.start();
  const auto default_result = default_inertial.run();
  require(
      default_result.time == 7
          && default_inertial.signal_value(default_output)
              == PackedLogic4::from_msb_string("0")
          && !default_inertial.scheduler().has_pending(),
      "default VHDL inertial rejection removes a short pulse");

  const auto run_explicit_rejection =
      [](const SimulationTick second_write_time) {
        Interpreter interpreter{{1000, 32}};
        const auto output = interpreter.add_signal(
            {"explicit-rejection",
             PackedLogic4::from_msb_string("0")});
        Process process;
        process.name = "explicit-rejection";
        process.register_count = 2;
        process.operations = {
            LoadConstant{0, PackedLogic4::from_msb_string("1")},
            WriteProjected{
                output,
                0,
                5,
                2,
                ProjectedDelayMode::inertial},
            WaitFor{second_write_time},
            LoadConstant{1, PackedLogic4::from_msb_string("0")},
            WriteProjected{
                output,
                1,
                5,
                2,
                ProjectedDelayMode::inertial},
            Halt{},
        };
        static_cast<void>(
            interpreter.add_process(std::move(process)));
        std::vector<SimulationTick> changes;
        interpreter.set_signal_change_hook(
            [&](const SignalId changed,
                const PackedLogic4&,
                const SimulationTick time) {
              if (changed == output) {
                changes.push_back(time);
              }
            });
        interpreter.start();
        static_cast<void>(interpreter.run());
        return changes;
      };
  require(
      run_explicit_rejection(2).empty(),
      "a pulse exactly equal to the VHDL rejection limit is rejected");
  require(
      run_explicit_rejection(3)
          == std::vector<SimulationTick>{5, 8},
      "a pulse longer than an explicit VHDL rejection limit is retained");

  Interpreter transport{{1000, 32}};
  const auto transport_output = transport.add_signal(
      {"transport", PackedLogic4::from_msb_string("0")});
  Process transported;
  transported.name = "transport";
  transported.register_count = 2;
  transported.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      WriteProjected{
          transport_output,
          0,
          5,
          0,
          ProjectedDelayMode::transport},
      WaitFor{2},
      LoadConstant{1, PackedLogic4::from_msb_string("0")},
      WriteProjected{
          transport_output,
          1,
          5,
          0,
          ProjectedDelayMode::transport},
      Halt{},
  };
  static_cast<void>(transport.add_process(std::move(transported)));
  std::vector<SimulationTick> transport_changes;
  transport.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4&,
          const SimulationTick time) {
        if (signal == transport_output) {
          transport_changes.push_back(time);
        }
      });
  transport.start();
  static_cast<void>(transport.run());
  require(
      transport_changes == std::vector<SimulationTick>{5, 7},
      "transport projected writes preserve a short pulse");

  Interpreter packed{{1000, 32}};
  const auto packed_output = packed.add_signal(
      {"packed-projected", PackedLogic4::from_msb_string("0000")});
  Process vector;
  vector.name = "packed-projected";
  vector.register_count = 4;
  vector.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10")},
      WriteProjectedSlice{
          packed_output,
          0,
          0,
          5,
          0,
          ProjectedDelayMode::transport},
      LoadConstant{1, PackedLogic4::from_msb_string("01")},
      WriteProjectedSlice{
          packed_output,
          1,
          0,
          7,
          0,
          ProjectedDelayMode::transport},
      LoadConstant{2, PackedLogic4::from_msb_string("00")},
      WriteProjectedSlice{
          packed_output,
          2,
          0,
          10,
          4,
          ProjectedDelayMode::inertial},
      LoadConstant{3, PackedLogic4::from_msb_string("11")},
      WriteProjectedSlice{
          packed_output,
          3,
          2,
          3,
          0,
          ProjectedDelayMode::transport},
      Halt{},
  };
  static_cast<void>(packed.add_process(std::move(vector)));
  std::vector<std::pair<
      SimulationTick, std::string>> packed_changes;
  packed.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        if (signal == packed_output) {
          packed_changes.emplace_back(
              time, value.to_msb_string());
        }
      });
  packed.start();
  static_cast<void>(packed.run());
  require(
      packed_changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {3, "1100"}, {5, "1110"}, {7, "1100"}},
      "projected vector and slice transactions are edited per scalar "
      "subelement");

  Interpreter waveform{{1000, 32}};
  const auto waveform_output = waveform.add_signal(
      {"atomic-waveform", PackedLogic4::from_msb_string("0")});
  Process atomic;
  atomic.name = "atomic-waveform";
  atomic.register_count = 2;
  atomic.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      LoadConstant{1, PackedLogic4::from_msb_string("0")},
      WriteProjectedWaveform{
          waveform_output,
          {{0, 5}, {1, 8}},
          5,
          ProjectedDelayMode::inertial},
      Halt{},
  };
  static_cast<void>(waveform.add_process(std::move(atomic)));
  std::vector<SimulationTick> waveform_changes;
  waveform.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4&,
          const SimulationTick time) {
        if (signal == waveform_output) {
          waveform_changes.push_back(time);
        }
      });
  waveform.start();
  static_cast<void>(waveform.run());
  require(
      waveform_changes == std::vector<SimulationTick>{5, 8}
          && !waveform.scheduler().has_pending(),
      "all new inertial waveform transactions are marked and retained "
      "atomically");

  Interpreter waveform_slice{{1000, 32}};
  const auto waveform_slice_output = waveform_slice.add_signal(
      {"atomic-slice-waveform",
       PackedLogic4::from_msb_string("0000")});
  Process atomic_slice;
  atomic_slice.name = "atomic-slice-waveform";
  atomic_slice.register_count = 2;
  atomic_slice.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("10")},
      LoadConstant{1, PackedLogic4::from_msb_string("01")},
      WriteProjectedWaveformSlice{
          waveform_slice_output,
          {{0, 2}, {1, 5}},
          1,
          0,
          ProjectedDelayMode::transport},
      Halt{},
  };
  static_cast<void>(
      waveform_slice.add_process(std::move(atomic_slice)));
  std::vector<std::pair<SimulationTick, std::string>>
      waveform_slice_changes;
  waveform_slice.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        if (signal == waveform_slice_output) {
          waveform_slice_changes.emplace_back(
              time, value.to_msb_string());
        }
      });
  waveform_slice.start();
  static_cast<void>(waveform_slice.run());
  require(
      waveform_slice_changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {2, "0100"}, {5, "0010"}},
      "atomic transport waveforms reconstruct packed slices");

  Interpreter invalid_waveform{{1000, 32}};
  const auto invalid_output = invalid_waveform.add_signal(
      {"invalid-waveform", PackedLogic4::from_msb_string("0")});
  Process invalid;
  invalid.name = "invalid-waveform";
  invalid.register_count = 2;
  invalid.operations = {
      LoadConstant{0, PackedLogic4::from_msb_string("1")},
      LoadConstant{1, PackedLogic4::from_msb_string("0")},
      WriteProjectedWaveform{
          invalid_output,
          {{0, 5}, {1, 5}},
          0,
          ProjectedDelayMode::transport},
      Halt{},
  };
  static_cast<void>(
      invalid_waveform.add_process(std::move(invalid)));
  invalid_waveform.start();
  bool rejected_invalid_waveform = false;
  try {
    static_cast<void>(invalid_waveform.run());
  } catch (const std::invalid_argument&) {
    rejected_invalid_waveform = true;
  }
  require(
      rejected_invalid_waveform,
      "nonascending projected waveforms must fail at the runtime boundary");
}

void test_vcd() {
  using namespace fsim::runtime;

  std::ostringstream output;
  VcdWriter writer(output, "1ps", 256);
  const auto clock = writer.declare_signal("top.clock", 1);
  const auto bus = writer.declare_signal("top.core.bus", 4);
  writer.begin();
  writer.change(clock, Logic4::zero);
  writer.change(bus, PackedLogic9::from_msb_string("10WZ"));
  writer.change(clock, Logic4::zero); // Suppressed.
  writer.set_time(7);
  writer.change(clock, Logic4::one);
  writer.flush();

  const auto text = output.str();
  require(text.find("$timescale 1ps $end") != std::string::npos,
          "VCD timescale");
  require(text.find("$scope module top $end") != std::string::npos,
          "VCD top scope");
  require(text.find("$scope module core $end") != std::string::npos,
          "VCD nested scope");
  require(text.find("b10xz") != std::string::npos,
          "VCD nine-state mapping");
  require(text.find("#7") != std::string::npos, "VCD time marker");
}

} // namespace

int main() {
  try {
    test_logic();
    test_packed_values();
    test_scheduler_phase_order();
    test_scheduler_stop_resume();
    test_scheduler_time_limit_before_future_event();
    test_scheduler_safe_point_scheduling();
    test_scheduler_delta_limit();
    test_simir();
    test_simir_permanent_wait();
    test_simir_update_coalescing();
    test_resolved_driver_slots();
    test_simir_expressions_and_edges();
    test_simir_noninitializing_static_process();
    test_simir_wide_truth_and_comparison();
    test_simir_wildcard_case_matching();
    test_simir_wildcard_equality();
    test_simir_wide_reduction_and_shift();
    test_simir_signed_shift_counts();
    test_simir_wide_unsigned_arithmetic();
    test_simir_wide_signed_arithmetic();
    test_checked_vhdl_integer_operations();
    test_simir_wide_extract_and_concatenate();
    test_simir_insert_and_partial_writes();
    test_simir_force_release();
    test_simir_design_stop_identity();
    test_simir_pause_resume_lifecycle();
    test_simir_final_process_lifecycle();
    test_simir_alternate_executor_context_and_boundaries();
    test_simir_alternate_executor_dynamic_wait();
    test_simir_timed_dynamic_wait_rearm();
    test_simir_alternate_executor_scheduled_word_writes();
    test_simir_alternate_executor_zero_delay_and_frame();
    test_simir_alternate_executor_cpp_exception_containment();
    test_simir_alternate_executor_validation();
    test_simir_alternate_executor_event_replacement_and_cancel();
    test_simir_alternate_executor_notify_delayed();
    test_simir_alternate_executor_primitive_channel_updates();
    test_simir_alternate_executor_signal_event_window();
    test_simir_alternate_executor_event_lists();
    test_simir_assertion_metadata();
    test_simir_execution_point_ordering();
    test_simir_display_output();
    test_deterministic_random_values();
    test_transition_delay_selection();
    test_simir_inertial_transition_writes();
    test_simir_projected_writes();
    test_vcd();
  } catch (const std::exception &error) {
    std::cerr << "runtime test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "runtime tests passed\n";
  return EXIT_SUCCESS;
}
