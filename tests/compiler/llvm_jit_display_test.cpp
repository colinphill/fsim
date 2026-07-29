// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_display_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol) {
  LlvmJitOptions options;
  options.optimization = level;
  LlvmJit jit(options);
  Process process;
  process.id = 13;
  process.name = std::string{symbol};
  process.register_count = 3;
  process.operations = {
      Display{"hello", true},
      Display{"tail", false},
      Display{"postponed", true, true},
      Report{
          "warning",
          AssertionSeverity::warning,
          SourceLocation{"report.vhd", 7, 5}},
      LoadConstant{0, PackedLogic4::from_msb_string("10xz")},
      FormatDisplay{
          0,
          OutputFormat::binary,
          "v=",
          "!",
          true,
          false},
      TimeDisplay{"time=", "", true, false, 4, false, true},
      MonitorInstall{
          {
              MonitorValue{
                  MonitorValueKind::signal,
                  0,
                  OutputFormat::hexadecimal,
                  "m="},
          },
          "",
          true},
      MonitorControl{false},
      LoadConstant{2, PackedLogic4::from_msb_string("0")},
      Assert{
          2,
          "nonfatal assertion",
          AssertionSeverity::error,
          SourceLocation{"assertion.sv", 9, 3}},
      RandomValue{
          1,
          RandomKind::urandom,
          std::nullopt,
          std::nullopt},
      Halt{},
  };
  const std::array<std::uint32_t, 1> signal_widths{8};
  jit.add_process(symbol, process, signal_widths);

  TestRuntime runtime;
  auto descriptor = abi(runtime);
  assert(
      jit.execute(jit.lookup(symbol), descriptor)
      == JitExecutionStatus::completed);
  assert(
      runtime.output == std::vector<std::string>({"hello", "tail"}));
  assert(
      runtime.output_processes
      == std::vector<std::uint32_t>({13, 13}));
  assert(
      runtime.output_newlines == std::vector<bool>({true, false}));
  assert(
      runtime.postponed_output
      == std::vector<std::string>({"postponed"}));
  assert(
      runtime.report_instructions
      == std::vector<std::uint32_t>({3, 10}));
  assert(
      runtime.formatted_instructions
      == std::vector<std::uint32_t>({5}));
  assert(runtime.formatted_values.size() == 1);
  assert(
      runtime.time_instructions
      == std::vector<std::uint32_t>({6}));
  assert(
      runtime.monitor_install_instructions
      == std::vector<std::uint32_t>({7}));
  assert(
      runtime.monitor_control_instructions
      == std::vector<std::uint32_t>({8}));
  assert(
      runtime.random_instructions
      == std::vector<std::uint32_t>({11}));

  TestRuntime short_runtime;
  auto short_descriptor = abi(short_runtime);
  short_descriptor.struct_size = static_cast<std::uint32_t>(
      offsetof(fsim_jit_runtime_v1, write_output));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_descriptor);
      },
      "write_output");

  TestRuntime short_postponed_runtime;
  auto short_postponed_descriptor = abi(short_postponed_runtime);
  short_postponed_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, schedule_output));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_postponed_descriptor);
      },
      "schedule_output");

  TestRuntime short_report_runtime;
  auto short_report_descriptor = abi(short_report_runtime);
  short_report_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, write_report));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_report_descriptor);
      },
      "write_report");

  TestRuntime short_formatted_runtime;
  auto short_formatted_descriptor = abi(short_formatted_runtime);
  short_formatted_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, write_formatted));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_formatted_descriptor);
      },
      "write_formatted");

  TestRuntime short_time_runtime;
  auto short_time_descriptor = abi(short_time_runtime);
  short_time_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, write_time));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_time_descriptor);
      },
      "write_time");

  TestRuntime short_monitor_install_runtime;
  auto short_monitor_install_descriptor =
      abi(short_monitor_install_runtime);
  short_monitor_install_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, install_monitor));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_monitor_install_descriptor);
      },
      "install_monitor");

  TestRuntime short_monitor_control_runtime;
  auto short_monitor_control_descriptor =
      abi(short_monitor_control_runtime);
  short_monitor_control_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, control_monitor));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_monitor_control_descriptor);
      },
      "control_monitor");

  TestRuntime short_random_runtime;
  auto short_random_descriptor = abi(short_random_runtime);
  short_random_descriptor.struct_size =
      static_cast<std::uint32_t>(
          offsetof(fsim_jit_runtime_v1, random_value));
  expect_error(
      [&] {
        (void)jit.execute(
            jit.lookup(symbol), short_random_descriptor);
      },
      "random_value");
}

} // namespace fsim::tests::compiler

