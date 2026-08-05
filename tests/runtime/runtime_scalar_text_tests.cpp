// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/systemverilog_scalar.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <cfenv>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

}  // namespace

void test_systemverilog_scalar_text_and_time() {
  using namespace fsim::runtime;
  using Error = SystemVerilogScalarError;
  using Format = SystemVerilogScalarTextFormat;
  using Function = SystemVerilogTimeFunction;

  const auto general = format_systemverilog_scalar(
      SystemVerilogScalarValue::real(1.25));
  SystemVerilogScalarFormatOptions fixed;
  fixed.format = Format::Fixed;
  fixed.precision = 2;
  fixed.minimum_width = 8;
  fixed.padding = '0';
  const auto padded = format_systemverilog_scalar(
      SystemVerilogScalarValue::real(-1.25), fixed);
  SystemVerilogScalarFormatOptions time_format;
  time_format.format = Format::Time;
  time_format.suffix = " ps";
  const auto time_text = format_systemverilog_scalar(
      SystemVerilogScalarValue::time(UINT64_C(9007199254740993)), time_format);
  require(
      general.text == "1.25" && padded.text == "-0001.25"
          && time_text.text == "9007199254740993 ps",
      "scalar display formatting is exact, padded, and locale independent");
  fixed.precision = 65;
  require(
      format_systemverilog_scalar(
          SystemVerilogScalarValue::real(1.0), fixed).error
          == Error::ResourceLimit,
      "scalar formatting enforces precision and output budgets");

  const auto scanned_short = scan_systemverilog_scalar(
      " 1.25 ", SystemVerilogScalarKind::ShortReal);
  const auto scanned_plus = scan_systemverilog_scalar(
      "+1.5", SystemVerilogScalarKind::Realtime);
  const auto scanned_time = scan_systemverilog_scalar(
      "2.5", SystemVerilogScalarKind::Time);
  const auto scanned_exact = scan_systemverilog_scalar(
      "9007199254740993", SystemVerilogScalarKind::Time);
  require(
      scanned_short.value.as_shortreal() == 1.25F
          && scanned_plus.value.as_real() == 1.5
          && scanned_time.value.as_time() == 3
          && scanned_exact.value.as_time() == UINT64_C(9007199254740993),
      "display, scan, and file text conversion share checked scalar values");
  require(
      scan_systemverilog_scalar(
          "1.0junk", SystemVerilogScalarKind::Real).error
              == Error::InvalidText
          && scan_systemverilog_scalar(
                 "12345", SystemVerilogScalarKind::Time,
                 SystemVerilogScalarRounding::NearestAwayFromZero, 4).error
              == Error::ResourceLimit
          && scan_systemverilog_scalar(
                 "nan", SystemVerilogScalarKind::Real).error
              == Error::Nonfinite,
      "scalar scanning rejects malformed, unbounded, and nonfinite text");

  const SystemVerilogTimeContext context{
      .time_unit_femtoseconds = 1'000'000,
      .time_precision_femtoseconds = 1'000,
      .project_resolution_femtoseconds = 1'000};
  const auto exact_delay = scale_systemverilog_delay(
      SystemVerilogScalarValue::time(2), context);
  const auto half_delay = scale_systemverilog_delay(
      SystemVerilogScalarValue::realtime(0.0005), context);
  const auto scheduled = schedule_systemverilog_delay(
      10, SystemVerilogScalarValue::realtime(1.25), context);
  require(
      exact_delay.ticks == 2000 && half_delay.ticks == 1
          && scheduled.ticks == 1260,
      "delay scaling rounds once at declared precision before scheduling");
  require(
      scale_systemverilog_delay(
          SystemVerilogScalarValue::real(-0.25), context).error
              == Error::NegativeDelay
          && schedule_systemverilog_delay(
                 std::numeric_limits<std::uint64_t>::max(),
                 SystemVerilogScalarValue::time(1), context).error
              == Error::Overflow
          && scale_systemverilog_delay(
                 SystemVerilogScalarValue::time(1), {10, 6, 2}).error
              == Error::InvalidTimeContext,
      "delay scheduling rejects negative, overflowing, and malformed contexts");

  const auto time = systemverilog_time_function(Function::Time, 2500, context);
  const auto stime = systemverilog_time_function(
      Function::Stime, UINT64_C(0x100000001) * 1000, context);
  const auto realtime = systemverilog_time_function(
      Function::Realtime, 2500, context);
  require(
      time.value.as_time() == 3 && stime.value.as_time() == 1
          && realtime.value.as_real() == 2.5,
      "$time, $stime, and $realtime share exact project-tick scaling");

  const auto previous_rounding = std::fegetround();
  if (std::fesetround(FE_DOWNWARD) == 0) {
    const auto rejected = systemverilog_time_function(
        Function::Realtime, 1, context);
    (void)std::fesetround(previous_rounding);
    require(
        rejected.error == Error::UnsupportedRoundingMode,
        "$realtime rejects a non-nearest host rounding environment");
  }
}

void test_systemverilog_scalar_execution_surfaces() {
  using namespace fsim::runtime;
  using namespace fsim::runtime::simir;

  const auto negative_zero = SystemVerilogScalarValue::real(-0.0);
  const auto zero_payload = encode_systemverilog_scalar_payload(negative_zero);
  const auto initial_time = SystemVerilogScalarValue::time(
      UINT64_C(9007199254740993));
  const auto time_payload = encode_systemverilog_scalar_payload(initial_time);
  require(zero_payload && time_payload, "scalar payload encoding succeeds");

  Interpreter interpreter;
  const auto real_signal = interpreter.add_signal(Signal{
      "top.real_value", zero_payload.value, ResolutionKind::none,
      ValueKind::logic4, std::nullopt, {}, std::nullopt, std::nullopt,
      SystemVerilogScalarKind::Real});
  const auto time_signal = interpreter.add_signal(Signal{
      "top.time_value", time_payload.value, ResolutionKind::none,
      ValueKind::logic4, std::nullopt, {}, std::nullopt, std::nullopt,
      SystemVerilogScalarKind::Time});

  const auto written = SystemVerilogScalarValue::real(3.5);
  const auto written_payload = encode_systemverilog_scalar_payload(written);
  Process process;
  process.id = 0;
  process.name = "scalar_transport";
  process.register_count = 1;
  process.debug_locals.push_back(DebugLocal{
      "value", "real", 0, 64, {}, {}, {}, ValueKind::logic4, {},
      SystemVerilogScalarKind::Real});
  process.operations = {
      LoadConstant{0, written_payload.value},
      WriteBlocking{real_signal, 0},
      Halt{}};
  (void)interpreter.add_process(std::move(process));

  std::vector<SystemVerilogScalarValue> changes;
  interpreter.set_scalar_signal_change_hook(
      [&](const SignalId, const SystemVerilogScalarValue& value,
          const SimulationTick) { changes.push_back(value); });
  interpreter.schedule_scalar_signal_after(
      time_signal, SystemVerilogScalarValue::time(
          UINT64_C(9007199254740995)), 2);
  const auto result = interpreter.run();
  require(
      result.status == RunStatus::completed && result.time == 2
          && interpreter.scalar_signal_value(real_signal) == written
          && interpreter.scalar_signal_value(time_signal).bits
              == UINT64_C(9007199254740995)
          && changes.size() == 2,
      "interpreter signals and typed callbacks preserve scalar payloads");
  require(
      interpreter.read_debug_scalar_local(0, 0) == written,
      "scalar debugger inspection decodes the canonical register payload");
  interpreter.write_debug_scalar_local(0, 0, negative_zero);
  require(
      interpreter.read_debug_scalar_local(0, 0).bits == negative_zero.bits,
      "scalar debugger mutation preserves negative-zero bits");
  const auto snapshots = interpreter.scalar_signal_snapshots();
  require(
      snapshots.size() == 2 && snapshots[0].signal == real_signal
          && snapshots[1].value.bits == UINT64_C(9007199254740995),
      "scalar trace snapshots are deterministic and exact beyond 2^53");
  require(
      [&] {
        try {
          interpreter.deposit_scalar_signal(
              real_signal, SystemVerilogScalarValue::time(1));
        } catch (const std::invalid_argument&) {
          return true;
        }
        return false;
      }(),
      "typed scalar mutation rejects kind mismatches");

  std::ostringstream trace;
  VcdWriter writer(trace, "1ps");
  const auto real_handle = writer.declare_systemverilog_scalar(
      "top.real_value", SystemVerilogScalarKind::Real);
  const auto time_handle = writer.declare_systemverilog_scalar(
      "top.time_value", SystemVerilogScalarKind::Time);
  writer.begin();
  writer.change(real_handle, negative_zero);
  writer.change(time_handle, initial_time);
  writer.flush();
  const auto vcd = trace.str();
  require(
      vcd.find("$var real 1") != std::string::npos
          && vcd.find("r-0 ") != std::string::npos
          && vcd.find("$var wire 64") != std::string::npos
          && vcd.find("b0000000000100000000000000000000000000000000000000000000000000001")
              != std::string::npos,
      "VCD policy emits real-family values as real and time as exact ticks");
}

}  // namespace fsim::tests::runtime
