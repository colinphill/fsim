// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
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

void test_simir_assertion_metadata() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

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
        LoadConstant{
            0,
            PackedLogic4::from_msb_string(id == 0 ? "0" : "1")},
        WriteBlocking{signal, 0},
        Halt{},
    };
    (void)interpreter.add_process(std::move(process));
  }
  std::vector<ProcessId> points;
  interpreter.set_execution_point_hook(
      [&](Scheduler& scheduler, const ExecutionPoint& point) {
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
    test_simir_update_coalescing();
    test_simir_expressions_and_edges();
    test_simir_force_release();
    test_simir_design_stop_identity();
    test_simir_alternate_executor_context_and_boundaries();
    test_simir_alternate_executor_dynamic_wait();
    test_simir_alternate_executor_scheduled_word_writes();
    test_simir_alternate_executor_zero_delay_and_frame();
    test_simir_alternate_executor_cpp_exception_containment();
    test_simir_alternate_executor_validation();
    test_simir_assertion_metadata();
    test_simir_execution_point_ordering();
    test_vcd();
  } catch (const std::exception &error) {
    std::cerr << "runtime test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "runtime tests passed\n";
  return EXIT_SUCCESS;
}
