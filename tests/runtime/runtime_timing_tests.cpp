// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/logic.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_vital.hpp"
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

namespace fsim::tests::runtime {

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

} // namespace

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

void test_vital_timing_checks() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  constexpr std::array<Logic9, 9> states{
      Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::z,
      Logic9::w, Logic9::l, Logic9::h, Logic9::dont_care};
  constexpr std::array<std::size_t, 9> classes{0, 0, 1, 2, 0, 0, 1, 2, 0};
  constexpr std::array<std::array<std::uint16_t, 3>, 3> masks{{
      {{0U, 0xDA08U, 0xB504U}},
      {{0xA150U, 0U, 0x8145U}},
      {{0xC2A0U, 0x828AU, 0U}},
  }};
  for (std::size_t previous = 0; previous < states.size(); ++previous) {
    for (std::size_t current = 0; current < states.size(); ++current) {
      require(
          vital_edge_symbol_mask(states[previous], states[current])
              == masks[classes[previous]][classes[current]],
          "VITAL edge symbols normalize every nine-state transition");
    }
  }

  Interpreter interpreter{{1000, 32}};
  const auto test = interpreter.add_signal(
      {"vital.test",
       PackedLogic4::from_logic9_msb_string("0"),
       ResolutionKind::none,
       ValueKind::logic9});
  const auto violation = interpreter.add_signal(
      {"vital.violation",
       PackedLogic4::from_logic9_msb_string("0"),
       ResolutionKind::none,
       ValueKind::logic9});

  Process checker;
  checker.name = "vital-period-pulse";
  checker.register_count = 2;
  checker.register_value_kinds = {ValueKind::logic9, ValueKind::logic9};
  checker.static_sensitivity = {{test, EdgeKind::any}};
  checker.driver_regions = {{violation, 0, 1, true}};
  VitalTimingCheck check;
  check.destination = 0;
  check.kind = VitalTimingCheckKind::period_pulse;
  check.test_signal = test;
  check.limits = {10, 6, 6, 0};
  check.message = "period/pulse violation";
  auto report_without_x = check;
  report_without_x.destination = 1;
  report_without_x.x_on = false;
  report_without_x.message = "period/pulse report without X";
  checker.operations = {
      check,
      report_without_x,
      WriteUpdate{violation, 0},
      WaitSensitivity{},
      Jump{0}};
  static_cast<void>(interpreter.add_process(std::move(checker)));

  Process stimulus;
  stimulus.id = 1;
  stimulus.name = "vital-stimulus";
  stimulus.register_count = 2;
  stimulus.register_value_kinds = {
      ValueKind::logic9, ValueKind::logic9};
  stimulus.driver_regions = {{test, 0, 1, true}};
  stimulus.operations = {
      LoadConstant{0, PackedLogic4::from_logic9_msb_string("1")},
      LoadConstant{1, PackedLogic4::from_logic9_msb_string("0")},
      WriteAfter{test, 0, 5},
      WriteAfter{test, 1, 10},
      WriteAfter{test, 0, 20},
      WaitFor{25},
      Halt{}};
  static_cast<void>(interpreter.add_process(std::move(stimulus)));

  std::vector<std::pair<SimulationTick, std::string>> changes;
  std::size_t reports{};
  std::size_t reports_without_x{};
  interpreter.set_signal_change_hook(
      [&](const SignalId signal,
          const PackedLogic4& value,
          const SimulationTick time) {
        if (signal == violation) {
          changes.emplace_back(time, value.to_msb_string());
        }
      });
  interpreter.set_report_hook(
      [&](ProcessId,
          std::string_view message,
          AssertionSeverity,
          const SourceLocation&,
          SimulationTick,
          std::uint64_t) {
        if (message == "period/pulse violation") ++reports;
        if (message == "period/pulse report without X") {
          ++reports_without_x;
        }
      });
  interpreter.start();
  static_cast<void>(interpreter.run());
  require(
      changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {10, "X"}, {20, "0"}},
      "VITAL period/pulse state persists and clears deterministically");
  require(reports == 1, "VITAL timing violations report exactly once");
  require(
      reports_without_x == 1,
      "VITAL MsgOn reporting is independent of XOn result corruption");
}

void test_vital_delay_scheduling() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  struct Result {
    std::vector<std::pair<SimulationTick, std::string>> changes;
    std::size_t reports{};
  };
  const auto run = [](
                       const VitalDelayKind kind,
                       const VitalDelayShape shape,
                       const VitalGlitchMode mode,
                       const std::array<SimulationTick, 6>& defaults,
                       const std::vector<std::array<SimulationTick, 6>>& paths,
                       const std::vector<std::pair<SimulationTick, Logic9>>& stimulus,
                       const std::string_view output_map = "UX01ZWLH-",
                       const bool x_on = true,
                       const bool negative_preemption = false,
                       const bool reject_fast_path = false,
                       const bool ignore_default_delay = false) {
    Interpreter interpreter{{1000, 64}};
    const auto input = interpreter.add_signal(
        {"vital.input", PackedLogic4::from_logic9_msb_string("0"),
         ResolutionKind::none, ValueKind::logic9});
    const auto output = interpreter.add_signal(
        {"vital.output", PackedLogic4::from_logic9_msb_string("U"),
         ResolutionKind::none, ValueKind::logic9});

    constexpr RegisterId source_register = 0U;
    constexpr RegisterId default_begin = 1U;
    constexpr RegisterId map_register = 7U;
    constexpr RegisterId condition_register = 8U;
    constexpr RegisterId last_event_register = 9U;
    constexpr RegisterId path_begin = 10U;
    constexpr std::size_t maximum_paths = 2U;
    constexpr std::size_t register_count =
        path_begin + maximum_paths * 6U;

    Process delay_process;
    delay_process.name = "vital-delay";
    delay_process.register_count = register_count;
    delay_process.register_value_kinds.assign(
        register_count, ValueKind::logic4);
    delay_process.register_value_kinds[source_register] = ValueKind::logic9;
    delay_process.register_value_kinds[map_register] = ValueKind::logic9;
    delay_process.static_sensitivity = {{input, EdgeKind::any}};
    delay_process.driver_regions = {{output, 0, 1, true}};
    delay_process.operations.emplace_back(ReadSignal{source_register, input});
    for (std::size_t index = 0; index < defaults.size(); ++index) {
      delay_process.operations.emplace_back(LoadConstant{
          static_cast<RegisterId>(default_begin + index),
          PackedLogic4::from_aval_bval(64U, defaults[index], 0U)});
    }
    delay_process.operations.emplace_back(LoadConstant{
        map_register,
        PackedLogic4::from_logic9_msb_string(output_map)});
    delay_process.operations.emplace_back(LoadConstant{
        condition_register, PackedLogic4::from_aval_bval(1U, 1U, 0U)});
    delay_process.operations.emplace_back(
        SignalLastEvent{last_event_register, input});

    VitalDelay delay;
    delay.kind = kind;
    delay.shape = shape;
    delay.output = output;
    delay.source = source_register;
    delay.output_map = map_register;
    delay.mode = mode;
    delay.x_on = x_on;
    delay.message_on = true;
    delay.negative_preemption = negative_preemption;
    delay.reject_fast_path = reject_fast_path;
    delay.ignore_default_delay = ignore_default_delay;
    delay.message = "VITAL delay glitch";
    for (std::size_t index = 0; index < defaults.size(); ++index) {
      delay.default_delays[index] =
          static_cast<RegisterId>(default_begin + index);
    }
    require(paths.size() <= maximum_paths, "VITAL test path capacity");
    for (std::size_t path_index = 0; path_index < paths.size(); ++path_index) {
      VitalPathCandidate candidate;
      candidate.input_change_time = last_event_register;
      candidate.condition = condition_register;
      for (std::size_t delay_index = 0; delay_index < 6U; ++delay_index) {
        const auto id = static_cast<RegisterId>(
            path_begin + path_index * 6U + delay_index);
        delay_process.operations.emplace_back(LoadConstant{
            id,
            PackedLogic4::from_aval_bval(
                64U, paths[path_index][delay_index], 0U)});
        candidate.delays[delay_index] = id;
      }
      delay.paths.push_back(candidate);
    }
    delay_process.operations.emplace_back(delay);
    delay_process.operations.emplace_back(WaitSensitivity{});
    delay_process.operations.emplace_back(Jump{0U});
    static_cast<void>(interpreter.add_process(std::move(delay_process)));

    Process driver;
    driver.id = 1U;
    driver.name = "vital-delay-stimulus";
    driver.register_count = stimulus.size();
    driver.register_value_kinds.assign(stimulus.size(), ValueKind::logic9);
    driver.driver_regions = {{input, 0, 1, true}};
    for (std::size_t index = 0; index < stimulus.size(); ++index) {
      PackedLogic4 value(1U);
      value.fill(stimulus[index].second);
      driver.operations.emplace_back(LoadConstant{
          static_cast<RegisterId>(index),
          std::move(value)});
      driver.operations.emplace_back(WriteAfter{
          input, static_cast<RegisterId>(index), stimulus[index].first});
    }
    driver.operations.emplace_back(Halt{});
    static_cast<void>(interpreter.add_process(std::move(driver)));

    Result result;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal,
            const PackedLogic4& value,
            const SimulationTick time) {
          if (signal == output) {
            result.changes.emplace_back(time, value.to_msb_string());
          }
        });
    interpreter.set_report_hook(
        [&](ProcessId, std::string_view, AssertionSeverity,
            const SourceLocation&, SimulationTick, std::uint64_t) {
          ++result.reports;
        });
    interpreter.start();
    static_cast<void>(interpreter.run());
    return result;
  };

  const auto single = run(
      VitalDelayKind::wire, VitalDelayShape::single,
      VitalGlitchMode::transport, {5, 0, 0, 0, 0, 0}, {},
      {{2, Logic9::one}, {4, Logic9::zero}});
  require(
      single.changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {5, "0"}, {7, "1"}, {9, "0"}},
      "VitalWireDelay scalar uses transport event propagation");

  const auto zero_delay = run(
      VitalDelayKind::signal, VitalDelayShape::single,
      VitalGlitchMode::transport, {0, 0, 0, 0, 0, 0}, {},
      {{2, Logic9::one}});
  require(
      zero_delay.changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {0, "0"}, {2, "1"}},
      "VitalSignalDelay zero delay commits through deterministic delta updates");

  bool overflow_rejected{};
  try {
    static_cast<void>(run(
        VitalDelayKind::signal, VitalDelayShape::single,
        VitalGlitchMode::transport,
        {std::numeric_limits<SimulationTick>::max(), 0, 0, 0, 0, 0}, {},
        {{2, Logic9::one}}));
  } catch (const std::overflow_error&) {
    overflow_rejected = true;
  }
  require(
      overflow_rejected,
      "VitalSignalDelay rejects checked simulation-time overflow");

  const auto delay01 = run(
      VitalDelayKind::wire, VitalDelayShape::delay01,
      VitalGlitchMode::transport, {3, 7, 0, 0, 0, 0}, {},
      {{10, Logic9::one}, {20, Logic9::zero}});
  require(
      delay01.changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {7, "0"}, {13, "1"}, {27, "0"}},
      "VitalWireDelay01 selects rise and fall delays");

  const auto delay01z = run(
      VitalDelayKind::wire, VitalDelayShape::delay01z,
      VitalGlitchMode::transport, {2, 3, 4, 5, 6, 7}, {},
      {{10, Logic9::z}, {20, Logic9::one}}, "UX0HZWLH-");
  require(
      delay01z.changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {7, "0"}, {14, "Z"}, {25, "H"}},
      "VitalWireDelay01Z selects Z transitions and applies OutputMap");

  const std::vector<std::array<SimulationTick, 6>> path_delays{
      {10, 10, 0, 0, 0, 0}, {4, 4, 0, 0, 0, 0}};
  const auto selected_path = run(
      VitalDelayKind::path, VitalDelayShape::single,
      VitalGlitchMode::transport, {10, 0, 0, 0, 0, 0}, path_delays,
      {{20, Logic9::one}});
  require(
      selected_path.changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {10, "0"}, {24, "1"}},
      "simultaneous VITAL candidates select the shortest effective path");

  const auto on_detect = run(
      VitalDelayKind::path, VitalDelayShape::single,
      VitalGlitchMode::on_detect, {10, 0, 0, 0, 0, 0}, path_delays,
      {{2, Logic9::one}});
  require(
      on_detect.changes
          == std::vector<std::pair<SimulationTick, std::string>>{
              {2, "X"}, {10, "1"}},
      "OnDetect injects X immediately without negative preemption");
  require(on_detect.reports == 1U, "OnDetect reports one detected glitch");

  const auto on_event = run(
      VitalDelayKind::path, VitalDelayShape::single,
      VitalGlitchMode::on_event, {10, 0, 0, 0, 0, 0}, path_delays,
      {{2, Logic9::one}});
  require(
      on_event.changes
          == std::vector<std::pair<SimulationTick, std::string>>{{10, "X"}},
      "OnEvent injects X at the projected event boundary");

  const auto transport = run(
      VitalDelayKind::path, VitalDelayShape::single,
      VitalGlitchMode::transport, {10, 0, 0, 0, 0, 0}, path_delays,
      {{2, Logic9::one}}, "UX01ZWLH-", false);
  require(
      transport.changes
          == std::vector<std::pair<SimulationTick, std::string>>{{10, "1"}},
      "VitalTransport retains ordered path transactions without X injection");
  require(
      transport.reports == 1U,
      "MsgOn reports independently when XOn is disabled");

  const auto inertial = run(
      VitalDelayKind::path, VitalDelayShape::single,
      VitalGlitchMode::inertial, {10, 0, 0, 0, 0, 0}, path_delays,
      {{2, Logic9::one}}, "UX01ZWLH-", false);
  require(
      inertial.changes
          == std::vector<std::pair<SimulationTick, std::string>>{{10, "1"}},
      "VitalInertial rejects the superseded default transaction");

  const auto negative_preemption = run(
      VitalDelayKind::path, VitalDelayShape::single,
      VitalGlitchMode::transport, {10, 0, 0, 0, 0, 0}, path_delays,
      {{2, Logic9::one}}, "UX01ZWLH-", false, true);
  require(
      negative_preemption.changes
          == std::vector<std::pair<SimulationTick, std::string>>{{6, "1"}},
      "NegPreemptOn permits a faster path to replace a later transaction");

  const auto reject_fast_path = run(
      VitalDelayKind::path, VitalDelayShape::delay01,
      VitalGlitchMode::transport, {10, 10, 0, 0, 0, 0}, path_delays,
      {{2, Logic9::one}}, "UX01ZWLH-", false, true, true);
  require(
      reject_fast_path.changes
          == std::vector<std::pair<SimulationTick, std::string>>{{10, "0"}},
      "RejectFastPath preserves the already projected slower path");

  const auto ignored_default = run(
      VitalDelayKind::path, VitalDelayShape::single,
      VitalGlitchMode::transport, {10, 0, 0, 0, 0, 0}, {},
      {{20, Logic9::one}}, "UX01ZWLH-", false, false, false, true);
  require(
      ignored_default.changes.empty(),
      "a null path range with IgnoreDefaultDelay schedules no transaction");
}

void test_vital_memory_declaration() {
  using namespace fsim::runtime::simir;

  const auto expect_rejection = [](const auto& action, const char* message) {
    bool rejected{};
    try {
      action();
    } catch (const std::exception&) {
      rejected = true;
    }
    require(rejected, message);
  };

  auto wide = make_vital_memory(3U, 65U, 8U);
  require(
      wide.words.size() == 3U && wide.word_width == 65U
          && wide.subword_width == 8U && wide.bits_per_enable == 9U,
      "VITAL memory preserves arbitrary-width geometry");
  require(
      wide.words[0].to_msb_string() == std::string(65U, 'U'),
      "VITAL memory words initialize to the UX01 uninitialized value");

  load_vital_memory_text(wide, "-- comment\n@1 1f 2_a # tail\n", false);
  require(
      wide.words[0].to_msb_string() == std::string(65U, 'U')
          && wide.words[1].to_msb_string()
              == std::string(57U, '0') + "00011111"
          && wide.words[2].to_msb_string()
              == std::string(57U, '0') + "00101010",
      "VITAL hexadecimal memory loading honors addresses and ascending words");

  auto binary = make_vital_memory(2U, 8U, 8U);
  load_vital_memory_text(binary, "@0 101x 01u1", true);
  require(
      binary.words[0].to_msb_string() == "0000101X"
          && binary.words[1].to_msb_string() == "000001U1",
      "VITAL binary memory loading preserves UX01 data");

  expect_rejection(
      [] { static_cast<void>(make_vital_memory(0U, 8U, 8U)); },
      "VITAL memory rejects zero-sized geometry");
  expect_rejection(
      [] { static_cast<void>(make_vital_memory(1U, 8U, 9U)); },
      "VITAL memory rejects a subword wider than its word");
  auto sparse = make_vital_memory(std::uint64_t{1U} << 40U, 8U, 8U);
  require(
      sparse.word_count == (std::uint64_t{1U} << 40U)
          && sparse.words.empty() && sparse.sparse_words.empty()
          && vital_memory_word(
                 static_cast<const VitalMemoryState&>(sparse),
                 (std::size_t{1U} << 39U)).to_msb_string() == "UUUUUUUU",
      "large logical VITAL memories use unbounded sparse default storage");
  vital_memory_word(sparse, std::size_t{1U} << 39U) =
      fsim::runtime::PackedLogic4::from_msb_string("10100101");
  require(
      sparse.sparse_words.size() == 1U
          && vital_memory_word(
                 static_cast<const VitalMemoryState&>(sparse),
                 std::size_t{1U} << 39U).to_msb_string() == "10100101"
          && vital_memory_word(
                 static_cast<const VitalMemoryState&>(sparse),
                 (std::size_t{1U} << 39U) + 1U).to_msb_string()
              == "UUUUUUUU",
      "sparse VITAL writes materialize only the addressed word");
  fsim::runtime::PackedLogic4 sparse_output(8U);
  sparse_output.fill(fsim::runtime::Logic9::zero);
  const auto sparse_input = sparse_output;
  VitalMemoryPortFlag sparse_flag;
  apply_vital_memory_table_actions(
      sparse, sparse_output, sparse_input, 0U, 0U, 8U,
      VitalMemoryTableResult{
          'c', 'S', fsim::runtime::PackedLogic4{8U},
          fsim::runtime::PackedLogic4{8U}, std::nullopt, false},
      sparse_flag);
  require(
      sparse.sparse_words.empty()
          && vital_memory_word(
                 static_cast<const VitalMemoryState&>(sparse), 0U)
                 .to_msb_string() == "XXXXXXXX"
          && vital_memory_word(
                 static_cast<const VitalMemoryState&>(sparse),
                 std::size_t{1U} << 39U).to_msb_string() == "XXXXXXXX",
      "whole-memory corruption updates the sparse default transactionally");
  expect_rejection(
      [] {
        static_cast<void>(make_vital_memory(
            1U, std::numeric_limits<std::uint32_t>::max(), 8U));
      },
      "VITAL memory rejects one word beyond the host resource budget");
  expect_rejection(
      [] {
        auto memory = make_vital_memory(1U, 4U, 4U);
        load_vital_memory_text(memory, "@1 0", false);
      },
      "VITAL memory loading rejects an out-of-range address");
  expect_rejection(
      [] {
        auto memory = make_vital_memory(1U, 4U, 4U);
        load_vital_memory_text(memory, "10", false);
      },
      "VITAL memory loading rejects nonzero data wider than the word");
  expect_rejection(
      [] {
        auto memory = make_vital_memory(1U, 8U, 8U);
        load_vital_memory_text(memory, "102q", true);
      },
      "VITAL memory loading rejects invalid digits");
  auto transactional = make_vital_memory(3U, 8U, 8U);
  load_vital_memory_text(transactional, "@1 aa", false);
  expect_rejection(
      [&] { load_vital_memory_text(transactional, "@1 55 @2 2q", false); },
      "VITAL memory loading rejects a late malformed token");
  require(
      transactional.words[1].to_msb_string() == "10101010"
          && transactional.words[2].to_msb_string() == "UUUUUUUU",
      "failed VITAL memory loading leaves all words unchanged");

  const auto descending = fsim::runtime::PackedLogic4::from_msb_string(
      "0101");
  const auto descending_decoded = decode_vital_memory_address(
      descending, descending, 16U);
  require(
      descending_decoded.state == VitalMemoryAddressState::good
          && descending_decoded.value == 5U,
      "a descending VHDL address range decodes left-to-right");
  const auto ascending = fsim::runtime::PackedLogic4::from_msb_string(
      "0011");
  const auto ascending_decoded = decode_vital_memory_address(
      ascending, ascending, 16U);
  require(
      ascending_decoded.state == VitalMemoryAddressState::good
          && ascending_decoded.value == 3U,
      "an ascending VHDL address range uses the same declared-range order");

  const auto changed = decode_vital_memory_address(
      descending, ascending, 16U);
  require(
      changed.state == VitalMemoryAddressState::good_transition
          && changed.value == 3U,
      "a changed valid address is classified as a good transition");
  const auto unknown = fsim::runtime::PackedLogic4::from_logic9_msb_string(
      "0H01");
  const auto unknown_decoded = decode_vital_memory_address(
      unknown, unknown, 16U);
  require(
      unknown_decoded.state == VitalMemoryAddressState::unknown
          && !unknown_decoded.value,
      "weak and unknown address levels classify as unknown");
  const auto unknown_changed = decode_vital_memory_address(
      descending, unknown, 16U);
  require(
      unknown_changed.state
              == VitalMemoryAddressState::unknown_transition
          && !unknown_changed.value,
      "an unknown changed address retains transition state");
  const auto invalid = fsim::runtime::PackedLogic4::from_msb_string(
      "1111");
  const auto invalid_decoded = decode_vital_memory_address(
      invalid, invalid, 8U);
  require(
      invalid_decoded.state == VitalMemoryAddressState::invalid
          && !invalid_decoded.value,
      "a known out-of-range address is invalid");

  const auto wide_valid = fsim::runtime::PackedLogic4::from_msb_string(
      std::string(126U, '0') + "0101");
  const auto wide_valid_decoded = decode_vital_memory_address(
      wide_valid, wide_valid, 16U);
  require(
      wide_valid_decoded.state == VitalMemoryAddressState::good
          && wide_valid_decoded.value == 5U,
      "a wide address with leading zeroes decodes without a host-width cap");
  const auto wide_invalid = fsim::runtime::PackedLogic4::from_msb_string(
      "1" + std::string(129U, '0'));
  const auto wide_invalid_decoded = decode_vital_memory_address(
      wide_valid, wide_invalid, 16U);
  require(
      wide_invalid_decoded.state
              == VitalMemoryAddressState::invalid_transition
          && !wide_invalid_decoded.value,
      "a wide overflowing address is invalid without host arithmetic overflow");
  const auto wide_unknown =
      fsim::runtime::PackedLogic4::from_logic9_msb_string(
          "1" + std::string(128U, '0') + "X");
  const auto wide_unknown_decoded = decode_vital_memory_address(
      wide_unknown, wide_unknown, 16U);
  require(
      wide_unknown_decoded.state == VitalMemoryAddressState::unknown,
      "unknown address data takes precedence over an out-of-range prefix");

  expect_rejection(
      [&] {
        static_cast<void>(decode_vital_memory_address(
            descending,
            fsim::runtime::PackedLogic4::from_msb_string("101"),
            16U));
      },
      "VITAL address decoding rejects mismatched history widths");
  expect_rejection(
      [&] {
        static_cast<void>(decode_vital_memory_address(
            descending, descending, 0U));
      },
      "VITAL address decoding rejects an empty memory");

  const std::vector<VitalMemoryTableRow> first_match_rows{
      {{'S', '/'}, {}, 'g', 'G', 'l', 'd'},
      {{'S', '/'}, {}, 'g', 'G', 'w', 't'}};
  const auto first_match = lookup_vital_memory_table(
      first_match_rows,
      fsim::runtime::PackedLogic4::from_msb_string("00"),
      fsim::runtime::PackedLogic4::from_msb_string("01"),
      VitalMemoryAddressState::good,
      VitalMemoryAddressState::good_transition,
      130U);
  require(
      first_match.matched_row == 0U
          && first_match.memory_action == 'l'
          && first_match.data_action == 'd'
          && first_match.memory_corrupt_mask.to_msb_string()
              == std::string(130U, 'X')
          && first_match.data_corrupt_mask.to_msb_string()
              == std::string(130U, 'X'),
      "VITAL word tables select the first exact row and size corruption masks");

  const std::vector<VitalMemoryTableRow> flag_rows{
      {{'B'}, {}, 'S', '*', 'w', 'M'}};
  const auto flag_match = lookup_vital_memory_table(
      flag_rows,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("H"),
      fsim::runtime::PackedLogic4::from_logic9_msb_string("H"),
      VitalMemoryAddressState::unknown,
      VitalMemoryAddressState::unknown_transition,
      8U);
  require(
      flag_match.matched_row == 0U
          && flag_match.memory_action == 'w'
          && flag_match.data_action == 'M'
          && flag_match.memory_corrupt_mask.to_msb_string() == "00000000",
      "VITAL word tables match weak controls and steady/transition flags");

  const auto default_action = lookup_vital_memory_table(
      first_match_rows,
      fsim::runtime::PackedLogic4::from_msb_string("00"),
      fsim::runtime::PackedLogic4::from_msb_string("00"),
      VitalMemoryAddressState::good,
      VitalMemoryAddressState::good,
      8U);
  require(
      !default_action.matched_row
          && default_action.memory_action == 's'
          && default_action.data_action == 'S'
          && !default_action.invalid_input_symbol,
      "VITAL word tables retain memory and output when no row matches");

  const auto invalid_symbol = lookup_vital_memory_table(
      {{{'Z'}, {}, '-', '-', 'w', 'm'}},
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      fsim::runtime::PackedLogic4::from_msb_string("1"),
      VitalMemoryAddressState::good,
      VitalMemoryAddressState::good,
      8U);
  require(
      invalid_symbol.invalid_input_symbol
          && !invalid_symbol.matched_row
          && invalid_symbol.memory_action == 's'
          && invalid_symbol.data_action == 'S',
      "an invalid VITAL input symbol terminates lookup with default actions");

  expect_rejection(
      [&] {
        static_cast<void>(lookup_vital_memory_table(
            {{{'-', '-'}, {}, '-', '-', 's', 'S'}},
            fsim::runtime::PackedLogic4::from_msb_string("0"),
            fsim::runtime::PackedLogic4::from_msb_string("0"),
            VitalMemoryAddressState::good,
            VitalMemoryAddressState::good,
            8U));
      },
      "VITAL memory lookup rejects a row/control width mismatch");

  const std::vector<VitalMemoryTableRow> subword_rows{
      {{'-'}, {'1', '0'}, '-', '-', 'L', 'D'}};
  const auto subword_result = lookup_vital_memory_subword_table(
      subword_rows,
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      {fsim::runtime::PackedLogic4::from_msb_string("000"),
       fsim::runtime::PackedLogic4::from_msb_string("000")},
      {fsim::runtime::PackedLogic4::from_msb_string("101"),
       fsim::runtime::PackedLogic4::from_msb_string("010")},
      fsim::runtime::PackedLogic4::from_msb_string("0000000000"),
      fsim::runtime::PackedLogic4::from_msb_string("1100000011"),
      VitalMemoryAddressState::good,
      10U,
      4U);
  require(
      subword_result.subwords.size() == 3U
          && subword_result.subwords[0].matched_row == 0U
          && !subword_result.subwords[1].matched_row
          && subword_result.subwords[2].matched_row == 0U
          && subword_result.subwords[0]
                  .memory_corrupt_mask.to_msb_string()
              == "000000XXXX"
          && subword_result.subwords[1]
                  .memory_corrupt_mask.to_msb_string()
              == "0000000000"
          && subword_result.subwords[2]
                  .memory_corrupt_mask.to_msb_string()
              == "XX00000000"
          && subword_result.subwords[0]
                  .data_corrupt_mask.to_msb_string()
              == "000000XXXX"
          && subword_result.subwords[2]
                  .data_corrupt_mask.to_msb_string()
              == "XX00000000",
      "VITAL subword lookup independently matches enables and partial masks");

  const auto whole_word_subaction = lookup_vital_memory_subword_table(
      {{{}, {}, '-', '-', 'c', 'l'}},
      fsim::runtime::PackedLogic4{},
      fsim::runtime::PackedLogic4{},
      {}, {},
      fsim::runtime::PackedLogic4::from_msb_string("00000"),
      fsim::runtime::PackedLogic4::from_msb_string("00000"),
      VitalMemoryAddressState::good,
      5U,
      2U);
  require(
      whole_word_subaction.subwords.size() == 3U
          && std::ranges::all_of(
              whole_word_subaction.subwords,
              [](const auto& subword) {
                return subword.memory_corrupt_mask.to_msb_string() == "XXXXX"
                    && subword.data_corrupt_mask.to_msb_string() == "XXXXX";
              }),
      "lowercase subword table corruption actions cover the whole word");

  expect_rejection(
      [&] {
        static_cast<void>(lookup_vital_memory_subword_table(
            subword_rows,
            fsim::runtime::PackedLogic4::from_msb_string("0"),
            fsim::runtime::PackedLogic4::from_msb_string("0"),
            {fsim::runtime::PackedLogic4::from_msb_string("00")},
            {fsim::runtime::PackedLogic4::from_msb_string("00")},
            fsim::runtime::PackedLogic4::from_msb_string("0000000000"),
            fsim::runtime::PackedLogic4::from_msb_string("0000000000"),
            VitalMemoryAddressState::good,
            10U,
            4U));
      },
      "VITAL subword lookup rejects inconsistent enable dimensions");

  const auto action = [](const char memory_action,
                         const char data_action,
                         const std::string_view memory_mask = "00000000",
                         const std::string_view data_mask = "00000000") {
    return VitalMemoryTableResult{
        memory_action,
        data_action,
        fsim::runtime::PackedLogic4::from_msb_string(memory_mask),
        fsim::runtime::PackedLogic4::from_msb_string(data_mask),
        0U,
        false};
  };
  auto action_memory = make_vital_memory(2U, 8U, 4U);
  action_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("00001111");
  action_memory.words[1] =
      fsim::runtime::PackedLogic4::from_msb_string("11110000");
  auto action_output =
      fsim::runtime::PackedLogic4::from_msb_string("01010101");
  const auto action_input =
      fsim::runtime::PackedLogic4::from_msb_string("10101010");
  VitalMemoryPortFlag action_flag;
  apply_vital_memory_table_actions(
      action_memory, action_output, action_input, 0U, 0U, 8U,
      action('w', 'm'), action_flag);
  require(
      action_output.to_msb_string() == "00001111"
          && action_memory.words[0].to_msb_string() == "10101010"
          && action_flag.data_current == VitalMemoryPortState::read
          && action_flag.memory_current == VitalMemoryPortState::write,
      "VITAL data reads occur before same-call memory writes");

  action_output = fsim::runtime::PackedLogic4::from_msb_string("01010101");
  apply_vital_memory_table_actions(
      action_memory, action_output, action_input, 0U, 0U, 8U,
      action('s', 't'), action_flag);
  require(
      action_output.to_msb_string() == action_input.to_msb_string()
          && action_memory.words[0].to_msb_string()
              == action_input.to_msb_string(),
      "VITAL transfer and retention actions preserve memory");
  apply_vital_memory_table_actions(
      action_memory, action_output, action_input, 0U, 2U, 6U,
      action('0', '1'), action_flag);
  require(
      action_memory.words[0].to_msb_string() == "10000010"
          && action_output.to_msb_string() == "10111110",
      "VITAL constant actions update only the selected word range");
  apply_vital_memory_table_actions(
      action_memory, action_output, action_input, 0U, 2U, 6U,
      action('Z', 'Z'), action_flag);
  require(
      action_memory.words[0].to_msb_string() == "10ZZZZ10"
          && action_output.to_msb_string() == "10ZZZZ10"
          && action_flag.data_current == VitalMemoryPortState::high_z,
      "VITAL high-impedance actions preserve exact logic state");

  action_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("00000000");
  action_memory.words[1] =
      fsim::runtime::PackedLogic4::from_msb_string("11111111");
  action_output = fsim::runtime::PackedLogic4::from_msb_string("00000000");
  apply_vital_memory_table_actions(
      action_memory, action_output, action_input, 0U, 2U, 6U,
      action('C', 'D', "00XXXX00", "00XXXX00"), action_flag);
  require(
      action_memory.words[0].to_msb_string() == "00XXXX00"
          && action_memory.words[1].to_msb_string() == "11XXXX11"
          && action_output.to_msb_string() == "00XXXX00",
      "VITAL partial corruption covers every word or the selected output");
  apply_vital_memory_table_actions(
      action_memory, action_output, action_input, 0U, 0U, 8U,
      action('c', 'l', "XXXXXXXX", "XXXXXXXX"), action_flag);
  require(
      std::ranges::all_of(
          action_memory.words,
          [](const auto& word) { return word.to_msb_string() == "XXXXXXXX"; })
          && action_output.to_msb_string() == "XXXXXXXX",
      "VITAL whole-memory and whole-output corruption actions execute");

  action_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("00001111");
  action_output = fsim::runtime::PackedLogic4::from_msb_string("01010101");
  apply_vital_memory_table_actions(
      action_memory, action_output, action_input, 0U, 0U, 4U,
      action('E', 'E', "0000XXXX", "0000XXXX"), action_flag);
  require(
      action_memory.words[0].to_msb_string() == "0000XXXX"
          && action_output.to_msb_string() == "0000XXXX",
      "VITAL conditional corruption compares selected memory/data ranges");

  action_output = fsim::runtime::PackedLogic4::from_msb_string("01010101");
  action_flag.output_disable = false;
  apply_vital_memory_table_actions(
      action_memory, action_output, action_input, std::nullopt, 0U, 8U,
      action('s', 'S'), action_flag);
  require(
      action_output.to_msb_string() == "01010101"
          && action_flag.output_disable,
      "VITAL steady output disables scheduling without changing its value");

  for (const auto memory_action : std::string_view{"sldewcC LDE01Z"}) {
    if (memory_action == ' ') continue;
    auto probe = make_vital_memory(1U, 8U, 4U);
    auto output = fsim::runtime::PackedLogic4::from_msb_string("00000000");
    VitalMemoryPortFlag flag;
    apply_vital_memory_table_actions(
        probe, output, action_input, 0U, 0U, 4U,
        action(memory_action, 'M', "XXXXXXXX"), flag);
    require(
        flag.memory_current != VitalMemoryPortState::undefined,
        "every public VITAL memory action must be recognized");
  }
  for (const auto data_action : std::string_view{"ldem tLDE01ZSM"}) {
    if (data_action == ' ') continue;
    auto probe = make_vital_memory(1U, 8U, 4U);
    auto output = fsim::runtime::PackedLogic4::from_msb_string("00000000");
    VitalMemoryPortFlag flag;
    apply_vital_memory_table_actions(
        probe, output, action_input, 0U, 0U, 4U,
        action('s', data_action, "00000000", "XXXXXXXX"), flag);
    require(
        data_action == 'S' || flag.data_current != VitalMemoryPortState::undefined,
        "every public VITAL data action must be recognized");
  }

  auto state_memory = make_vital_memory(2U, 8U, 8U);
  state_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("00001111");
  state_memory.words[1] =
      fsim::runtime::PackedLogic4::from_msb_string("11110000");
  VitalMemoryTableState word_state;
  auto state_output =
      fsim::runtime::PackedLogic4::from_msb_string("00000000");
  const std::vector<VitalMemoryTableRow> state_rows{
      {{'-'}, {}, '*', '-', 's', 'm'},
      {{'-'}, {}, '-', '-', 's', 'm'}};
  const auto state_address0 = execute_vital_memory_word_table(
      state_memory, word_state, state_output,
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      fsim::runtime::PackedLogic4::from_msb_string("00000000"),
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      state_rows);
  require(
      state_address0.state == VitalMemoryAddressState::good
          && state_output.to_msb_string() == "00001111"
          && word_state.port_flags[0].data_current
              == VitalMemoryPortState::read
          && word_state.port_flags[0].data_previous
              == VitalMemoryPortState::undefined,
      "VITAL word-table state initializes and retains current/previous flags");
  static_cast<void>(execute_vital_memory_word_table(
      state_memory, word_state, state_output,
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      fsim::runtime::PackedLogic4::from_msb_string("00000000"),
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      state_rows));
  static_cast<void>(execute_vital_memory_word_table(
      state_memory, word_state, state_output,
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      fsim::runtime::PackedLogic4::from_msb_string("00000000"),
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      state_rows));
  require(
      word_state.port_flags[0].output_disable,
      "a fully steady VITAL word port suppresses redundant output scheduling");
  const auto state_address1 = execute_vital_memory_word_table(
      state_memory, word_state, state_output,
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      fsim::runtime::PackedLogic4::from_msb_string("00000000"),
      fsim::runtime::PackedLogic4::from_msb_string("1"),
      state_rows);
  require(
      state_address1.state == VitalMemoryAddressState::good_transition
          && state_address1.value == 1U
          && state_output.to_msb_string() == "11110000"
          && !word_state.port_flags[0].output_disable,
      "VITAL word-table state preserves address history across calls");

  auto subword_state_memory = make_vital_memory(1U, 8U, 4U);
  VitalMemoryTableState subword_state;
  auto subword_state_output =
      fsim::runtime::PackedLogic4::from_msb_string("00000000");
  const auto subword_state_address = execute_vital_memory_subword_table(
      subword_state_memory, subword_state, subword_state_output,
      fsim::runtime::PackedLogic4{},
      {fsim::runtime::PackedLogic4::from_msb_string("11")},
      action_input,
      fsim::runtime::PackedLogic4::from_msb_string("0"),
      {{{}, {'B'}, '-', '-', 'w', 't'}});
  require(
      subword_state_address.value == 0U
          && subword_state_memory.words[0].to_msb_string() == "10101010"
          && subword_state_output.to_msb_string() == "10101010"
          && subword_state.port_flags.size() == 2U
          && std::ranges::all_of(
              subword_state.port_flags,
              [](const auto& flag) {
                return flag.memory_current == VitalMemoryPortState::write
                    && flag.data_current == VitalMemoryPortState::read;
              }),
      "VITAL subword state preserves independent per-enable port flags");

  const auto port_flags = [](const VitalMemoryPortState memory_state,
                             const std::size_t count = 2U) {
    std::vector<VitalMemoryPortFlag> flags(count);
    for (auto& flag : flags) flag.memory_current = memory_state;
    return flags;
  };
  auto cross_memory = make_vital_memory(2U, 8U, 4U);
  cross_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("10100101");
  cross_memory.words[1] =
      fsim::runtime::PackedLogic4::from_msb_string("01011010");
  auto cross_output =
      fsim::runtime::PackedLogic4::from_msb_string("00000000");
  auto same_flags = port_flags(VitalMemoryPortState::read);
  const std::vector<VitalMemoryCrossPort> writing_port{
      {0U, port_flags(VitalMemoryPortState::write)}};
  apply_vital_memory_cross_ports(
      cross_memory, cross_output, same_flags, 0U, writing_port,
      VitalMemoryCrossPortMode::cross_read);
  require(
      cross_output.to_msb_string() == "10100101"
          && std::ranges::all_of(same_flags, [](const auto& flag) {
               return flag.memory_current == VitalMemoryPortState::read
                   && flag.data_current == VitalMemoryPortState::read
                   && !flag.output_disable;
             }),
      "VITAL cross-port reads forward the selected memory word");

  cross_output = fsim::runtime::PackedLogic4::from_msb_string("00000000");
  same_flags = port_flags(VitalMemoryPortState::write);
  apply_vital_memory_cross_ports(
      cross_memory, cross_output, same_flags, 0U, writing_port,
      VitalMemoryCrossPortMode::write_contention);
  require(
      cross_output.to_msb_string() == "XXXXXXXX"
          && cross_memory.words[0].to_msb_string() == "XXXXXXXX"
          && std::ranges::all_of(same_flags, [](const auto& flag) {
               return flag.memory_current == VitalMemoryPortState::corrupt
                   && flag.data_current == VitalMemoryPortState::corrupt;
             }),
      "same-address VITAL write contention corrupts memory and output");

  cross_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("10100101");
  cross_output = fsim::runtime::PackedLogic4::from_msb_string("00000000");
  same_flags = port_flags(VitalMemoryPortState::read);
  apply_vital_memory_cross_ports(
      cross_memory, cross_output, same_flags, 0U, writing_port,
      VitalMemoryCrossPortMode::read_write_contention);
  require(
      cross_output.to_msb_string() == "XXXXXXXX"
          && cross_memory.words[0].to_msb_string() == "XXXXXXXX",
      "read/write contention mode corrupts both memory and output");

  cross_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("10100101");
  cross_output = fsim::runtime::PackedLogic4::from_msb_string("00000000");
  same_flags = port_flags(VitalMemoryPortState::read);
  apply_vital_memory_cross_ports(
      cross_memory, cross_output, same_flags, 0U, writing_port,
      VitalMemoryCrossPortMode::cross_read_and_read_contention);
  require(
      cross_output.to_msb_string() == "XXXXXXXX"
          && cross_memory.words[0].to_msb_string() == "10100101",
      "read/read contention mode corrupts output without changing memory");

  cross_output = fsim::runtime::PackedLogic4::from_msb_string("01010101");
  same_flags = port_flags(VitalMemoryPortState::read);
  apply_vital_memory_cross_ports(
      cross_memory, cross_output, same_flags, 0U,
      {{1U, port_flags(VitalMemoryPortState::write)}},
      VitalMemoryCrossPortMode::cross_read_and_write_contention);
  require(
      cross_output.to_msb_string() == "01010101",
      "unrelated VITAL cross-port addresses do not interact");
  cross_output = fsim::runtime::PackedLogic4::from_msb_string("ZZZZZZZZ");
  apply_vital_memory_cross_ports(
      cross_memory, cross_output, same_flags, 0U, writing_port,
      VitalMemoryCrossPortMode::read_write_contention);
  require(
      cross_output.to_msb_string() == "ZZZZZZZZ"
          && cross_memory.words[0].to_msb_string() == "10100101",
      "a disabled high-impedance VITAL port ignores cross-port activity");

  auto pair_memory = make_vital_memory(2U, 8U, 4U);
  pair_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("00000000");
  pair_memory.words[1] =
      fsim::runtime::PackedLogic4::from_msb_string("11111111");
  apply_vital_memory_write_contention(
      pair_memory,
      {{0U, port_flags(VitalMemoryPortState::write)},
       {0U, port_flags(VitalMemoryPortState::write)},
       {1U, port_flags(VitalMemoryPortState::write)}});
  require(
      pair_memory.words[0].to_msb_string() == "XXXXXXXX"
          && pair_memory.words[1].to_msb_string() == "11111111",
      "pairwise VITAL write contention is deterministic and address-local");

  expect_rejection(
      [&] {
        auto output = fsim::runtime::PackedLogic4::from_msb_string("00000000");
        auto flags = port_flags(VitalMemoryPortState::read);
        apply_vital_memory_cross_ports(
            pair_memory, output, flags, 0U,
            {{0U, port_flags(VitalMemoryPortState::write, 1U)}},
            VitalMemoryCrossPortMode::cross_read);
      },
      "VITAL cross-port processing rejects inconsistent flag dimensions");

  const std::vector<VitalMemoryTableRow> violation_rows{
      {{'-', '0'}, {'X', '0'}, '-', '-', 'D', 'L'}};
  const auto violation_lookup = lookup_vital_memory_violation(
      violation_rows,
      fsim::runtime::PackedLogic4::from_msb_string("X0"),
      fsim::runtime::PackedLogic4::from_msb_string("00X0"),
      {2U, 2U}, 8U, 4U);
  require(
      violation_lookup.violation
          && violation_lookup.message_requested
          && violation_lookup.actions.matched_row == 0U
          && violation_lookup.actions.memory_action == 'D'
          && violation_lookup.actions.data_action == 'L'
          && violation_lookup.actions.memory_corrupt_mask.to_msb_string()
              == "XXXX0000"
          && violation_lookup.actions.data_corrupt_mask.to_msb_string()
              == "XXXX0000",
      "VITAL violation lookup aggregates sized vector flags into subword masks");
  const auto quiet_violation = lookup_vital_memory_violation(
      violation_rows,
      fsim::runtime::PackedLogic4::from_msb_string("X0"),
      fsim::runtime::PackedLogic4::from_msb_string("00X0"),
      {2U, 2U}, 8U, 4U, false);
  require(
      quiet_violation.violation && !quiet_violation.message_requested,
      "VITAL violation reporting is independent from X action selection");

  auto violation_memory = make_vital_memory(1U, 8U, 4U);
  violation_memory.words[0] =
      fsim::runtime::PackedLogic4::from_msb_string("10101010");
  auto violation_output =
      fsim::runtime::PackedLogic4::from_msb_string("01010101");
  auto violation_flags = port_flags(VitalMemoryPortState::read);
  const auto read_violation = apply_vital_memory_violation(
      violation_memory, violation_output, violation_flags, action_input, 0U,
      fsim::runtime::PackedLogic4::from_msb_string("X0"),
      fsim::runtime::PackedLogic4::from_msb_string("00X0"),
      {2U, 2U}, violation_rows, VitalMemoryPortType::read);
  require(
      read_violation.violation
          && violation_output.to_msb_string() == "XXXX1010"
          && violation_memory.words[0].to_msb_string() == "10101010"
          && std::ranges::all_of(violation_flags, [](const auto& flag) {
               return flag.data_current == VitalMemoryPortState::corrupt
                   && !flag.output_disable;
             }),
      "a read-port VITAL violation corrupts output but retains memory");

  violation_output =
      fsim::runtime::PackedLogic4::from_msb_string("01010101");
  violation_flags = port_flags(VitalMemoryPortState::write);
  const auto write_violation = apply_vital_memory_violation(
      violation_memory, violation_output, violation_flags, action_input, 0U,
      fsim::runtime::PackedLogic4::from_msb_string("X0"),
      fsim::runtime::PackedLogic4::from_msb_string("00X0"),
      {2U, 2U}, violation_rows, VitalMemoryPortType::write);
  require(
      write_violation.violation
          && violation_output.to_msb_string() == "01010101"
          && violation_memory.words[0].to_msb_string() == "XXXX1010",
      "a write-port VITAL violation corrupts memory but retains output");

  const auto no_violation = lookup_vital_memory_violation(
      violation_rows,
      fsim::runtime::PackedLogic4::from_msb_string("00"),
      fsim::runtime::PackedLogic4::from_msb_string("0000"),
      {2U, 2U}, 8U, 4U);
  require(
      !no_violation.violation && !no_violation.actions.matched_row
          && no_violation.actions.memory_action == 's'
          && no_violation.actions.data_action == 'S',
      "an unmatched VITAL violation table retains memory and output");
  const auto bad_violation_symbol = lookup_vital_memory_violation(
      {{{'1', '-'}, {'-', '-'}, '-', '-', 'c', 'l'}},
      fsim::runtime::PackedLogic4::from_msb_string("X0"),
      fsim::runtime::PackedLogic4::from_msb_string("0000"),
      {2U, 2U}, 8U, 4U);
  require(
      bad_violation_symbol.actions.invalid_input_symbol
          && !bad_violation_symbol.violation,
      "VITAL violation tables reject symbols outside X, zero, and don't-care");
  const auto invalid_address_violation = apply_vital_memory_violation(
      violation_memory, violation_output, violation_flags, action_input,
      std::nullopt,
      fsim::runtime::PackedLogic4::from_msb_string("X0"),
      fsim::runtime::PackedLogic4::from_msb_string("00X0"),
      {2U, 2U}, violation_rows, VitalMemoryPortType::read_write);
  require(
      !invalid_address_violation.violation,
      "VITAL violations ignore erroneous decoded addresses transactionally");
  expect_rejection(
      [&] {
        static_cast<void>(lookup_vital_memory_violation(
            violation_rows,
            fsim::runtime::PackedLogic4::from_msb_string("X0"),
            fsim::runtime::PackedLogic4::from_msb_string("00X0"),
            {3U, 2U}, 8U, 4U));
      },
      "VITAL violation lookup rejects inconsistent vector flag sizes");

  std::vector<VitalMemorySetupHoldEntry> setup_entries(4U);
  for (auto& entry : setup_entries) {
    entry.test_delay = 2U;
    entry.reference_delay = 3U;
    entry.limits = {5U, 5U, 5U, 5U};
    entry.check_enabled = false;
  }
  setup_entries[0].check_enabled = true;
  VitalMemoryVectorTimingState setup_state;
  static_cast<void>(evaluate_vital_memory_setup_hold(
      setup_state, 0U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      setup_entries, VitalMemoryTimingArc::cross, 1U,
      std::uint16_t{1U} << 0U));
  static_cast<void>(evaluate_vital_memory_setup_hold(
      setup_state, 5U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("01"),
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      setup_entries, VitalMemoryTimingArc::cross, 1U,
      std::uint16_t{1U} << 0U));
  const auto setup_violation = evaluate_vital_memory_setup_hold(
      setup_state, 7U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("01"),
      fsim::runtime::PackedLogic4::from_logic9_msb_string("01"),
      setup_entries, VitalMemoryTimingArc::cross, 1U,
      std::uint16_t{1U} << 0U);
  require(
      setup_violation.violation && setup_violation.message_requested
          && setup_violation.violated_checks == std::vector<std::size_t>{0U}
          && setup_violation.test_violations.to_msb_string() == "0X"
          && setup_violation.reference_violations.to_msb_string() == "0X",
      "VITAL cross-arc setup checks retain independent test/reference state");

  auto no_x_setup_entries = setup_entries;
  VitalMemoryVectorTimingState no_x_setup_state;
  static_cast<void>(evaluate_vital_memory_setup_hold(
      no_x_setup_state, 0U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      no_x_setup_entries, VitalMemoryTimingArc::cross, 1U,
      std::uint16_t{1U} << 0U, {true, true, true, true}, false, true));
  static_cast<void>(evaluate_vital_memory_setup_hold(
      no_x_setup_state, 5U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("01"),
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      no_x_setup_entries, VitalMemoryTimingArc::cross, 1U,
      std::uint16_t{1U} << 0U, {true, true, true, true}, false, true));
  const auto no_x_setup = evaluate_vital_memory_setup_hold(
      no_x_setup_state, 7U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("01"),
      fsim::runtime::PackedLogic4::from_logic9_msb_string("01"),
      no_x_setup_entries, VitalMemoryTimingArc::cross, 1U,
      std::uint16_t{1U} << 0U, {true, true, true, true}, false, true);
  require(
      no_x_setup.violation && no_x_setup.message_requested
          && no_x_setup.test_violations.to_msb_string() == "00"
          && no_x_setup.reference_violations.to_msb_string() == "00",
      "VITAL vector timing reports independently when XOn is disabled");

  VitalMemoryVectorTimingState subword_timing_state;
  std::vector<VitalMemorySetupHoldEntry> subword_entries(4U);
  for (auto& entry : subword_entries) entry.limits = {3U, 3U, 3U, 3U};
  const auto initialized_subword = evaluate_vital_memory_setup_hold(
      subword_timing_state, 0U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("0000"),
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      subword_entries, VitalMemoryTimingArc::subword, 2U,
      std::uint16_t{1U} << 0U);
  require(
      !initialized_subword.violation
          && subword_timing_state.elements.size() == 4U,
      "VITAL subword setup checks map each data bit to its enable reference");

  require(
      aggregate_vital_memory_violations(
          fsim::runtime::PackedLogic4::from_logic9_msb_string("X00X"),
          2U).to_msb_string() == "XX",
      "VITAL memory violations aggregate deterministically by subword");

  VitalMemoryVectorTimingState period_state;
  const std::vector<VitalMemoryPeriodPulseEntry> period_entries{
      {1U, 10U, 6U, 6U, true},
      {2U, 10U, 6U, 6U, false}};
  static_cast<void>(evaluate_vital_memory_period_pulse(
      period_state, 0U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      period_entries));
  static_cast<void>(evaluate_vital_memory_period_pulse(
      period_state, 5U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("01"),
      period_entries));
  const auto pulse_violation = evaluate_vital_memory_period_pulse(
      period_state, 10U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("00"),
      period_entries);
  require(
      pulse_violation.violation && pulse_violation.message_requested
          && pulse_violation.violated_checks == std::vector<std::size_t>{0U}
          && pulse_violation.test_violations.to_msb_string() == "0X",
      "VITAL vector period/pulse checks retain per-element thresholds and state");
  const auto quiet_period = evaluate_vital_memory_period_pulse(
      period_state, 12U,
      fsim::runtime::PackedLogic4::from_logic9_msb_string("01"),
      period_entries, false, false,
      VitalMemoryMessageFormat::vector_enumerated);
  require(
      quiet_period.violation && !quiet_period.message_requested
          && quiet_period.test_violations.to_msb_string() == "00",
      "VITAL period reporting and X propagation are independently controlled");

  expect_rejection(
      [&] {
        VitalMemoryVectorTimingState bad_state;
        static_cast<void>(evaluate_vital_memory_setup_hold(
            bad_state, 0U,
            fsim::runtime::PackedLogic4::from_msb_string("00"),
            fsim::runtime::PackedLogic4::from_msb_string("0"),
            {{0U, 0U, {1U, 1U, 1U, 1U}, true}},
            VitalMemoryTimingArc::parallel, 1U,
            std::uint16_t{1U} << 0U));
      },
      "VITAL parallel timing rejects mismatched vector widths");
}

} // namespace fsim::tests::runtime
