// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_test_support.hpp"

namespace fsim::tests::compiler {

void test_display_at_level(
    const JitOptimizationLevel level,
    const std::string_view symbol)
{
    LlvmJitOptions options;
    options.optimization = level;
    LlvmJit jit(options);
    Process process;
    process.id = 13;
    process.name = std::string { symbol };
    process.register_count = 3;
    process.operations = {
        Display { "hello", true },
        Display { "tail", false },
        Display { "postponed", true, true },
        Report {
            "warning",
            AssertionSeverity::warning,
            SourceLocation { "report.vhd", 7, 5 } },
        LoadConstant { 0, PackedLogic4::from_msb_string("10xz") },
        FormatDisplay {
            0,
            OutputFormat::binary,
            "v=",
            "!",
            true,
            false },
        TimeDisplay { "time=", "", true, false, 4, false, true },
        MonitorInstall {
            {
                MonitorValue {
                    MonitorValueKind::signal,
                    0,
                    OutputFormat::hexadecimal,
                    "m=" },
            },
            "",
            true,
            false,
            std::nullopt },
        MonitorControl { false },
        LoadConstant { 2, PackedLogic4::from_msb_string("0") },
        Assert {
            2,
            "nonfatal assertion",
            AssertionSeverity::error,
            SourceLocation { "assertion.sv", 9, 3 } },
        RandomValue {
            1,
            RandomKind::urandom,
            std::nullopt,
            std::nullopt },
        Halt { },
    };
    const std::array<std::uint32_t, 1> signal_widths { 8 };
    jit.add_process(symbol, process, signal_widths);

    TestRuntime runtime;
    auto descriptor = abi(runtime);
    assert(
        jit.execute(jit.lookup(symbol), descriptor)
        == JitExecutionStatus::completed);
    assert(
        runtime.output == std::vector<std::string>({ "hello", "tail" }));
    assert(
        runtime.output_processes
        == std::vector<std::uint32_t>({ 13, 13 }));
    assert(
        runtime.output_newlines == std::vector<bool>({ true, false }));
    assert(
        runtime.postponed_output
        == std::vector<std::string>({ "postponed" }));
    assert(
        runtime.report_instructions
        == std::vector<std::uint32_t>({ 3, 10 }));
    assert(
        runtime.formatted_instructions
        == std::vector<std::uint32_t>({ 5 }));
    assert(runtime.formatted_values.size() == 1);
    assert(
        runtime.time_instructions
        == std::vector<std::uint32_t>({ 6 }));
    assert(
        runtime.monitor_install_instructions
        == std::vector<std::uint32_t>({ 7 }));
    assert(
        runtime.monitor_control_instructions
        == std::vector<std::uint32_t>({ 8 }));
    assert(
        runtime.random_instructions
        == std::vector<std::uint32_t>({ 11 }));

    TestRuntime short_runtime;
    auto short_descriptor = abi(short_runtime);
    auto short_services = copy_jit_services(short_descriptor);
    short_services.write_output = nullptr;
    short_descriptor.services = &short_services;
    expect_error(
        [&] {
            (void)jit.execute(
                jit.lookup(symbol), short_descriptor);
        },
        "write_output");

    TestRuntime short_postponed_runtime;
    auto short_postponed_descriptor = abi(short_postponed_runtime);
    auto short_postponed_services
        = copy_jit_services(short_postponed_descriptor);
    short_postponed_services.schedule_output = nullptr;
    short_postponed_descriptor.services = &short_postponed_services;
    expect_error(
        [&] {
            (void)jit.execute(
                jit.lookup(symbol), short_postponed_descriptor);
        },
        "schedule_output");

    TestRuntime short_report_runtime;
    auto short_report_descriptor = abi(short_report_runtime);
    auto short_report_services = copy_jit_services(short_report_descriptor);
    short_report_services.write_report = nullptr;
    short_report_descriptor.services = &short_report_services;
    expect_error(
        [&] {
            (void)jit.execute(
                jit.lookup(symbol), short_report_descriptor);
        },
        "write_report");

    TestRuntime short_formatted_runtime;
    auto short_formatted_descriptor = abi(short_formatted_runtime);
    auto short_formatted_services
        = copy_jit_services(short_formatted_descriptor);
    short_formatted_services.write_formatted = nullptr;
    short_formatted_descriptor.services = &short_formatted_services;
    expect_error(
        [&] {
            (void)jit.execute(
                jit.lookup(symbol), short_formatted_descriptor);
        },
        "write_formatted");

    TestRuntime short_time_runtime;
    auto short_time_descriptor = abi(short_time_runtime);
    auto short_time_services = copy_jit_services(short_time_descriptor);
    short_time_services.write_time = nullptr;
    short_time_descriptor.services = &short_time_services;
    expect_error(
        [&] {
            (void)jit.execute(
                jit.lookup(symbol), short_time_descriptor);
        },
        "write_time");

    TestRuntime short_monitor_install_runtime;
    auto short_monitor_install_descriptor = abi(short_monitor_install_runtime);
    auto short_monitor_install_services
        = copy_jit_services(short_monitor_install_descriptor);
    short_monitor_install_services.install_monitor = nullptr;
    short_monitor_install_descriptor.services
        = &short_monitor_install_services;
    expect_error(
        [&] {
            (void)jit.execute(
                jit.lookup(symbol), short_monitor_install_descriptor);
        },
        "install_monitor");

    TestRuntime short_monitor_control_runtime;
    auto short_monitor_control_descriptor = abi(short_monitor_control_runtime);
    auto short_monitor_control_services
        = copy_jit_services(short_monitor_control_descriptor);
    short_monitor_control_services.control_monitor = nullptr;
    short_monitor_control_descriptor.services
        = &short_monitor_control_services;
    expect_error(
        [&] {
            (void)jit.execute(
                jit.lookup(symbol), short_monitor_control_descriptor);
        },
        "control_monitor");

    TestRuntime short_random_runtime;
    auto short_random_descriptor = abi(short_random_runtime);
    auto short_random_services = copy_jit_services(short_random_descriptor);
    short_random_services.random_value = nullptr;
    short_random_descriptor.services = &short_random_services;
    expect_error(
        [&] {
            (void)jit.execute(
                jit.lookup(symbol), short_random_descriptor);
        },
        "random_value");

    Process invalid_scalar_format;
    invalid_scalar_format.id = 14;
    invalid_scalar_format.name = "invalid_scalar_format";
    invalid_scalar_format.register_count = 1;
    invalid_scalar_format.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string("0") },
        FormatDisplay { 0, OutputFormat::binary, { }, { }, true, false,
            false, false, 0, false, false,
            runtime::SystemVerilogScalarKind::Real },
        Halt { }
    };
    const std::array<std::uint32_t, 0> no_signals { };
    expect_error(
        [&] {
            jit.add_process(
                std::string { symbol } + "_invalid_scalar",
                invalid_scalar_format, no_signals);
        },
        "FormatDisplay scalar metadata is inconsistent");

    Process wide_monitor;
    wide_monitor.id = 15;
    wide_monitor.name = "wide_signal_monitor";
    wide_monitor.operations = {
        MonitorInstall {
            { MonitorValue { MonitorValueKind::signal, 0,
                OutputFormat::hexadecimal, "wide=" } },
            "", true, false, std::nullopt },
        Halt { }
    };
    const std::array<std::uint32_t, 1> wide_signal_widths { 80U };
    const auto wide_symbol = std::string { symbol } + "_wide_monitor";
    jit.add_process(wide_symbol, wide_monitor, wide_signal_widths);
    TestRuntime wide_runtime;
    auto wide_descriptor = abi(wide_runtime);
    assert(jit.execute(jit.lookup(wide_symbol), wide_descriptor)
        == JitExecutionStatus::completed);
    assert((wide_runtime.monitor_install_instructions
        == std::vector<std::uint32_t> { 0U }));

    Process wide_random;
    wide_random.id = 16;
    wide_random.name = "wide_random_bounds";
    wide_random.register_count = 3;
    wide_random.operations = {
        LoadConstant { 0, PackedLogic4::from_msb_string(
            std::string(64U, 'X') + std::string(28U, '0') + "1001") },
        LoadConstant { 1, PackedLogic4::from_msb_string(
            std::string(64U, 'Z') + std::string(30U, '0') + "11") },
        RandomValue { 2, RandomKind::urandom_range, 0, 1 },
        Halt { }
    };
    const auto random_symbol = std::string { symbol } + "_wide_random";
    jit.add_process(random_symbol, wide_random, no_signals);
    TestRuntime random_runtime;
    auto random_descriptor = abi(random_runtime);
    assert(jit.execute(jit.lookup(random_symbol), random_descriptor)
        == JitExecutionStatus::completed);
    assert(random_runtime.random_bounds.size() == 1U);
    const auto& bounds = random_runtime.random_bounds.front();
    assert((bounds[0] & UINT64_C(0xffffffff)) == 9U);
    assert((bounds[1] & UINT64_C(0xffffffff)) == 0U);
    assert((bounds[2] & UINT64_C(0xffffffff)) == 3U);
    assert((bounds[3] & UINT64_C(0xffffffff)) == 0U);
    assert((bounds[1] >> 32U) != 0U);
    assert((bounds[3] >> 32U) != 0U);
}

} // namespace fsim::tests::compiler
