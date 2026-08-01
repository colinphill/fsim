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

void test_simir_dynamic_packed_indices() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto integer = [](const std::int32_t value) {
    return PackedLogic4::from_aval_bval(
        32, static_cast<std::uint32_t>(value), 0);
  };

  Interpreter interpreter;
  const auto descending = interpreter.add_signal(
      {"top.dynamic.descending", PackedLogic4(1, Logic4::zero)});
  const auto ascending = interpreter.add_signal(
      {"top.dynamic.ascending",
       PackedLogic4::from_logic9_msb_string("X"),
       ResolutionKind::none,
       ValueKind::logic9});
  const auto inserted = interpreter.add_signal(
      {"top.dynamic.inserted",
       PackedLogic4::from_logic9_msb_string("00000000"),
       ResolutionKind::none,
       ValueKind::logic9});
  const auto blocking = interpreter.add_signal(
      {"top.dynamic.blocking", PackedLogic4(8, Logic4::zero)});
  const auto updated = interpreter.add_signal(
      {"top.dynamic.updated", PackedLogic4(8, Logic4::zero)});
  const auto delayed = interpreter.add_signal(
      {"top.dynamic.delayed", PackedLogic4(8, Logic4::zero)});
  const auto inertial = interpreter.add_signal(
      {"top.dynamic.inertial", PackedLogic4(8, Logic4::zero)});
  const auto projected = interpreter.add_signal(
      {"top.dynamic.projected",
       PackedLogic4::from_logic9_msb_string("00000000"),
       ResolutionKind::none,
       ValueKind::logic9});
  const auto waveform = interpreter.add_signal(
      {"top.dynamic.waveform",
       PackedLogic4::from_logic9_msb_string("00000000"),
       ResolutionKind::none,
       ValueKind::logic9});

  Process process;
  process.id = 0;
  process.name = "dynamic_packed_indices";
  process.register_count = 15;
  process.register_value_kinds.assign(
      process.register_count, ValueKind::logic4);
  process.register_value_kinds[0] = ValueKind::logic9;
  process.register_value_kinds[4] = ValueKind::logic9;
  process.register_value_kinds[5] = ValueKind::logic9;
  process.register_value_kinds[13] = ValueKind::logic9;
  process.register_value_kinds[14] = ValueKind::logic9;
  process.operations = {
      LoadConstant{
          0,
          PackedLogic4::from_logic9_msb_string("UX01ZWLH")},
      LoadConstant{1, integer(5)},
      DynamicExtract{2, 0, DynamicIndex{1, 7, 0, 0}},
      WriteBlocking{descending, 2},
      LoadConstant{3, integer(3)},
      DynamicExtract{4, 0, DynamicIndex{3, 3, 10, 0}},
      WriteBlocking{ascending, 4},
      LoadConstant{
          5, PackedLogic4::from_logic9_msb_string("H")},
      DynamicInsert{0, 0, 5, DynamicIndex{3, -2, 5, 0}},
      WriteBlocking{inserted, 0},
      LoadConstant{6, PackedLogic4::from_msb_string("1")},
      LoadConstant{7, integer(6)},
      WriteBlockingDynamicSlice{
          blocking, 6, DynamicIndex{7, 7, 0, 0}},
      LoadConstant{8, integer(5)},
      WriteUpdateDynamicSlice{
          updated, 6, DynamicIndex{8, 7, 0, 0}},
      LoadConstant{9, integer(4)},
      WriteAfterDynamicSlice{
          delayed, 6, DynamicIndex{9, 7, 0, 0}, 3},
      LoadConstant{10, integer(3)},
      WriteInertialDynamicSlice{
          inertial,
          6,
          DynamicIndex{10, 7, 0, 0},
          TransitionDelays{2, 2, 2}},
      LoadConstant{11, integer(2)},
      WriteProjectedDynamicSlice{
          projected,
          5,
          DynamicIndex{11, 7, 0, 0},
          1,
          0,
          ProjectedDelayMode::transport},
      LoadConstant{12, integer(1)},
      LoadConstant{
          13, PackedLogic4::from_logic9_msb_string("L")},
      LoadConstant{
          14, PackedLogic4::from_logic9_msb_string("H")},
      WriteProjectedWaveformDynamicSlice{
          waveform,
          {
              ProjectedWaveformElement{13, 1},
              ProjectedWaveformElement{14, 4},
          },
          DynamicIndex{12, 7, 0, 0},
          0,
          ProjectedDelayMode::transport},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 4,
      "dynamic packed-index process completes after timed writes");
  require(
      interpreter.signal_value(descending).to_msb_string() == "0",
      "dynamic extraction follows a descending source range");
  require(
      interpreter.signal_value(ascending).to_msb_string() == "U",
      "dynamic extraction follows an ascending source range");
  require(
      interpreter.signal_value(inserted).to_msb_string()
          == "UX01ZHLH",
      "dynamic Logic9 insertion changes only the normalized element");
  require(
      interpreter.signal_value(blocking).to_msb_string()
              == "01000000"
          && interpreter.signal_value(updated).to_msb_string()
              == "00100000"
          && interpreter.signal_value(delayed).to_msb_string()
              == "00010000"
          && interpreter.signal_value(inertial).to_msb_string()
              == "00001000",
      "dynamic immediate, update, delayed, and inertial writes use "
      "captured offsets");
  require(
      interpreter.signal_value(projected).to_msb_string()
              == "00000H00",
      "dynamic projected Logic9 write preserves the exact element state");
  require(
      interpreter.signal_value(waveform).to_msb_string()
              == "000000H0",
      "dynamic projected Logic9 waveform preserves exact element states");

  const auto expect_failure =
      [&](PackedLogic4 index,
          const std::string_view expected_message) {
        Interpreter failing;
        Process candidate;
        candidate.name = "dynamic_index_failure";
        candidate.register_count = 3;
        candidate.operations = {
            LoadConstant{
                0, PackedLogic4::from_msb_string("1010")},
            LoadConstant{1, std::move(index)},
            DynamicExtract{
                2, 0, DynamicIndex{1, 3, 0, 0}},
            Halt{}};
        (void)failing.add_process(std::move(candidate));
        try {
          (void)failing.run();
          throw std::runtime_error(
              "dynamic packed-index failure was not reported");
        } catch (const InterpreterError& error) {
          require(
              std::string_view{error.what()}.find(expected_message)
                  != std::string_view::npos,
              "dynamic packed-index failure message");
        }
      };
  expect_failure(
      integer(4), "dynamic packed index is outside the declared range");
  auto unknown = integer(0);
  unknown.set(0, Logic4::x);
  expect_failure(
      std::move(unknown),
      "dynamic packed index contains an unknown or high-impedance value");
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

  const auto selected = interpreter.add_signal(
      {"top.selected_force", PackedLogic4::from_msb_string("1010")});
  interpreter.force_signal_slice(
      selected, PackedLogic4::from_msb_string("11"), 1);
  require(
      interpreter.signal_value(selected).to_msb_string() == "1110",
      "selected force must preserve surrounding driven bits");
  interpreter.deposit_signal(
      selected, PackedLogic4::from_msb_string("0001"));
  require(
      interpreter.signal_value(selected).to_msb_string() == "0111",
      "selected force must mask only its region over new driver values");
  interpreter.release_signal_slice(selected, 2, 1);
  require(
      interpreter.signal_is_forced(selected)
          && interpreter.signal_value(selected).to_msb_string() == "0011",
      "partial release must reveal only the released driven bit");
  interpreter.release_signal_slice(selected, 1, 1);
  require(
      !interpreter.signal_is_forced(selected)
          && interpreter.signal_value(selected).to_msb_string() == "0001",
      "final selected release must reveal the complete underlying value");
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

void test_simir_nested_calls() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto output = interpreter.add_signal(
      {"top.call_result", PackedLogic4::from_aval_bval(8, 0, 0)});
  const CallStack stack{0, 1, 2};

  Process process;
  process.id = 0;
  process.name = "nested_calls";
  process.register_count = 6;
  process.operations = {
      LoadConstant{0, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{1, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{2, PackedLogic4::from_aval_bval(32, 0, 0)},
      LoadConstant{3, PackedLogic4::from_aval_bval(8, 10, 0)},
      LoadConstant{4, PackedLogic4::from_aval_bval(8, 0, 0)},
      Call{9, 6, stack},
      WriteBlocking{output, 4},
      Halt{},
      Halt{},
      DebugPoint{
          DebugPointKind::call,
          SourceLocation{"nested_calls.simir", 1, 1}},
      LoadConstant{5, PackedLogic4::from_aval_bval(8, 0, 0)},
      Call{15, 12, stack},
      CopyRegister{4, 5},
      Return{stack},
      Halt{},
      LoadConstant{5, PackedLogic4::from_aval_bval(8, 42, 0)},
      Return{stack},
  };
  (void)interpreter.add_process(std::move(process));

  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed,
      "nested SimIR calls must complete");
  require(
      interpreter.signal_value(output).to_msb_string() == "00101010",
      "nested SimIR calls must return through the persistent call stack");

  const auto word =
      [](const std::uint64_t aval, const std::uint64_t bval = 0) {
        return PackedLogic4::from_aval_bval(
            32, aval, bval);
      };
  const auto expect_call_failure =
      [&](std::vector<Operation> operations,
          const std::string_view expected) {
        Interpreter failing;
        Process invalid;
        invalid.id = 0;
        invalid.name = "invalid_call_stack";
        invalid.register_count = 2;
        invalid.operations = std::move(operations);
        (void)failing.add_process(std::move(invalid));
        try {
          (void)failing.run();
          throw std::runtime_error(
              "invalid SimIR call stack was accepted");
        } catch (const InterpreterError& error) {
          require(
              std::string_view{error.what()}.find(expected)
                  != std::string_view::npos,
              "SimIR call-stack diagnostic");
        }
      };
  const CallStack one_entry_stack{0, 1, 1};
  expect_call_failure(
      {
          LoadConstant{0, word(0, 1)},
          LoadConstant{1, word(0)},
          Return{one_entry_stack},
          Halt{},
      },
      "call-stack pointer is unknown");
  expect_call_failure(
      {
          LoadConstant{0, word(1)},
          LoadConstant{1, word(0)},
          Call{3, 3, one_entry_stack},
          Halt{},
      },
      "call-stack capacity is exhausted");
  expect_call_failure(
      {
          LoadConstant{0, word(0)},
          LoadConstant{1, word(0)},
          Return{one_entry_stack},
          Halt{},
      },
      "call-stack underflow");
  expect_call_failure(
      {
          LoadConstant{0, word(1)},
          LoadConstant{1, word(99)},
          Return{one_entry_stack},
          Halt{},
      },
      "call-stack return target is invalid");
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

void test_simir_mutable_strings() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  Interpreter interpreter;
  const auto object = interpreter.add_string_object(
      StringObject{"top.title", {}});

  Process process;
  process.id = 0;
  process.name = "mutable_strings";
  process.register_count = 5;
  process.string_register_count = 4;
  process.debug_locals = {
      DebugLocal{
          "equal", "logic", 0, 1, {}, {}, {},
          ValueKind::logic4, {}},
      DebugLocal{
          "length", "int", 1, 32, {}, {}, {},
          ValueKind::logic4, {}},
      DebugLocal{
          "first", "byte", 3, 8, {}, {}, {},
          ValueKind::logic4, {}},
  };
  process.debug_string_locals = {
      DebugStringLocal{"copy", 3, {}},
  };
  process.operations = {
      LoadStringConstant{0, "fsim"},
      LoadStringConstant{1, "-v1"},
      ConcatenateStrings{2, {0, 1}},
      WriteStringObject{object, 2},
      ReadStringObject{3, object},
      CompareStrings{0, 2, 3, false},
      StringLength{1, 3},
      LoadConstant{
          2, PackedLogic4::from_aval_bval(32, 0, 0)},
      StringIndex{3, 3, 2, true},
      LoadConstant{
          4, PackedLogic4::from_aval_bval(8, 'F', 0)},
      StringReplaceByte{3, 2, 4, true},
      WriteStringObject{object, 3},
      Halt{},
  };
  (void)interpreter.add_process(std::move(process));
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed
          && interpreter.string_object_value(object) == "Fsim-v1",
      "mutable string object read, value-copy, concatenation, index, and "
      "replacement");
  require(
      interpreter.read_debug_local(0, 0).to_msb_string() == "1"
          && interpreter.read_debug_local(0, 1).low_word().aval == 7
          && interpreter.read_debug_local(0, 2).low_word().aval == 'f'
          && interpreter.read_debug_string_local(0, 0) == "Fsim-v1",
      "mutable string comparison, length, indexing, and debugger values");

  try {
    Interpreter invalid;
    (void)invalid.add_string_object(
        StringObject{
            "oversize",
            std::string(maximum_string_bytes + 1, 'x')});
    throw std::runtime_error{"oversize string object was accepted"};
  } catch (const std::length_error& error) {
    require(
        std::string_view{error.what()}.find("byte limit")
            != std::string_view::npos,
        "oversize string object diagnostic");
  }

  Interpreter invalid_index;
  Process bad;
  bad.id = 0;
  bad.name = "invalid_string_index";
  bad.register_count = 2;
  bad.string_register_count = 1;
  bad.operations = {
      LoadStringConstant{0, "x"},
      LoadConstant{
          0, PackedLogic4::from_aval_bval(32, 1, 0)},
      StringIndex{1, 0, 0, true},
      Halt{},
  };
  (void)invalid_index.add_process(std::move(bad));
  try {
    (void)invalid_index.run();
    throw std::runtime_error{"out-of-range string index was accepted"};
  } catch (const InterpreterError& error) {
    require(
        std::string_view{error.what()}.find("outside the byte range")
            != std::string_view::npos,
        "out-of-range string index diagnostic");
  }
}

void test_simir_fork_process_lifecycle() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto run_fork = [](
      std::vector<Operation> operations,
      const std::string_view expected,
      const SimulationTick expected_time,
      const std::uint64_t expected_register) {
    Interpreter interpreter;
    const auto output = interpreter.add_signal(
        Signal{"fork.output", PackedLogic4::from_msb_string("00")});
    Process process;
    process.id = 0;
    process.name = "fork_lifecycle";
    process.register_count = 1;
    process.debug_locals = {
        DebugLocal{
            "shared", "logic [1:0]", 0, 2, {}, {}, {},
            ValueKind::logic4, {}},
    };
    for (auto& operation : operations) {
      if (auto* write = fsim::runtime::simir::operation_get_if<WriteBlocking>(&operation)) {
        write->signal = output;
      }
    }
    process.operations = std::move(operations);
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && result.time == expected_time
            && interpreter.signal_value(output).to_msb_string()
                == expected
            && interpreter.read_debug_local(0, 0).low_word().aval
                == expected_register,
        "fork lifecycle, shared frame, and deterministic completion");
  };

  run_fork(
      {
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 0, 0)},
          Fork{{4, 7}, ForkJoinKind::all},
          WriteBlocking{0, 0},
          Halt{},
          WaitFor{2},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 1, 0)},
          ForkEnd{},
          WaitFor{1},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 2, 0)},
          ForkEnd{},
      },
      "01", 2, 1);

  run_fork(
      {
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 0, 0)},
          Fork{{5, 8}, ForkJoinKind::any},
          WriteBlocking{0, 0},
          WaitFork{},
          Halt{},
          WaitFor{1},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 1, 0)},
          ForkEnd{},
          WaitFor{2},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 2, 0)},
          ForkEnd{},
      },
      "01", 2, 2);

  run_fork(
      {
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 0, 0)},
          Fork{{6, 9}, ForkJoinKind::none},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 3, 0)},
          WaitFork{},
          WriteBlocking{0, 0},
          Halt{},
          WaitFor{2},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 1, 0)},
          ForkEnd{},
          WaitFor{1},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 2, 0)},
          ForkEnd{},
      },
      "01", 2, 1);

  run_fork(
      {
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 0, 0)},
          Fork{{5}, ForkJoinKind::none},
          DisableFork{},
          WaitFor{2},
          Halt{},
          WaitFor{1},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 3, 0)},
          WriteBlocking{0, 0},
          ForkEnd{},
      },
      "00", 2, 0);

  run_fork(
      {
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 0, 0)},
          Fork{{4}, ForkJoinKind::any},
          DisableFork{},
          Halt{},
          Fork{{7}, ForkJoinKind::none},
          ForkEnd{},
          Halt{},
          WaitFor{1},
          LoadConstant{0, PackedLogic4::from_aval_bval(2, 3, 0)},
          WriteBlocking{0, 0},
          ForkEnd{},
      },
      "00", 0, 0);

  {
    Interpreter interpreter;
    const auto trigger = interpreter.add_signal(
        Signal{"fork.trigger", PackedLogic4::from_msb_string("0")});
    const auto observed = interpreter.add_signal(
        Signal{"fork.observed", PackedLogic4::from_msb_string("0")});
    Process process;
    process.id = 0;
    process.name = "fork_static_sensitivity";
    process.register_count = 1;
    process.static_sensitivity = {{trigger, EdgeKind::any}};
    process.operations = {
        Fork{{3, 7}, ForkJoinKind::all},
        Halt{},
        Halt{},
        WaitSensitivity{},
        LoadConstant{0, PackedLogic4::from_msb_string("1")},
        WriteBlocking{observed, 0},
        ForkEnd{},
        WaitFor{1},
        LoadConstant{0, PackedLogic4::from_msb_string("1")},
        WriteBlocking{trigger, 0},
        ForkEnd{},
    };
    (void)interpreter.add_process(std::move(process));
    const auto result = interpreter.run();
    require(
        result.status == RunStatus::completed
            && result.time == 1
            && interpreter.signal_value(observed).to_msb_string() == "1",
        "dynamic fork children register inherited static sensitivity");
  }

  const auto expect_malformed = [](
      std::vector<Operation> operations,
      const std::string_view message) {
    Interpreter interpreter;
    Process process;
    process.id = 0;
    process.name = "malformed_fork";
    process.operations = std::move(operations);
    (void)interpreter.add_process(std::move(process));
    bool rejected = false;
    try {
      (void)interpreter.run();
    } catch (const InterpreterError& error) {
      rejected = std::string_view{error.what()}.find(message)
          != std::string_view::npos;
    }
    require(rejected, "malformed fork SimIR must be rejected");
  };
  expect_malformed(
      {ForkEnd{}, Halt{}},
      "ForkEnd requires a dynamically spawned fork child");
  expect_malformed(
      {Fork{{2, 2}, ForkJoinKind::all}, Halt{}, ForkEnd{}},
      "fork branch entry is duplicated");
  expect_malformed(
      {Fork{{1}, ForkJoinKind::all}, ForkEnd{}},
      "fork branch must follow its parent continuation");
  expect_malformed(
      {Fork{{}, ForkJoinKind::all}},
      "fork parent continuation is outside the operation stream");
  expect_malformed(
      {
          Fork{{2}, static_cast<ForkJoinKind>(99)},
          Halt{}, ForkEnd{},
      },
      "fork has an invalid join kind");
}

} // namespace fsim::tests::runtime
