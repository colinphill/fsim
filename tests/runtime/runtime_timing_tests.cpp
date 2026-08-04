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

} // namespace fsim::tests::runtime
