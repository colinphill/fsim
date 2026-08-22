// SPDX-License-Identifier: Apache-2.0
#include "../../src/app/application_internal.hpp"

#include "fsim/runtime/simir_coverage.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace fsim;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

runtime::simir::Process covered_process(const std::uint32_t counter)
{
    runtime::simir::Process process;
    process.id = 0U;
    process.name = "covered";
    process.static_sensitivity = { { 0U } };
    process.operations = {
        runtime::simir::CodeCoverageHit {
            { 0x1020304050607080ULL, 0x0102030405060708ULL },
            runtime::CodeCoverageMetric::Statement,
            { counter } },
        runtime::simir::WaitSensitivity { },
        runtime::simir::Jump { 0U },
    };
    return process;
}

runtime::simir::Process debug_covered_process()
{
    auto process = covered_process(0U);
    process.operations = {
        runtime::simir::DebugPoint {
            runtime::simir::DebugPointKind::statement,
            { "coverage_debug.sv", 17U, 5U },
            "top.covered" },
        runtime::simir::CodeCoverageHit {
            { 0x1020304050607080ULL, 0x0102030405060708ULL },
            runtime::CodeCoverageMetric::Statement,
            { 0U } },
        runtime::simir::WaitSensitivity { },
        runtime::simir::Jump { 0U },
    };
    return process;
}

void run_debug_engine(const bool compiled)
{
    auto program = debug_covered_process();
    const std::array<std::uint32_t, 1> signal_widths { 1U };
    std::unique_ptr<compiler::LlvmJit> jit;
    std::optional<compiler::JitProcessHandle> handle;
    if (compiled) {
        compiler::LlvmJitOptions options;
        options.optimization = compiler::JitOptimizationLevel::o0;
        options.debug_instrumentation = true;
        jit = std::make_unique<compiler::LlvmJit>(std::move(options));
        jit->add_process("coverage_debug", program, signal_widths);
        handle = jit->lookup("coverage_debug");
    }

    runtime::simir::Interpreter interpreter;
    (void)interpreter.add_signal({ "top.coverage_trigger",
        runtime::PackedLogic4::from_msb_string("0") });
    const auto process = interpreter.add_process(program);
    interpreter.set_code_coverage_counters({ 0U });
    if (compiled) {
        interpreter.set_process_executor(
            process,
            std::make_unique<
                app::application_detail::LlvmProcessExecutor>(
                *jit,
                *handle,
                interpreter.process_program(process),
                signal_widths,
                std::span<const runtime::simir::ValueKind> { },
                std::span<const runtime::simir::ResolutionKind> { }));
    }

    std::vector<runtime::simir::ExecutionPoint> points;
    interpreter.set_execution_point_hook(
        [&](runtime::Scheduler& scheduler,
            const runtime::simir::ExecutionPoint& point) {
            points.push_back(point);
            if (points.size() == 1U) {
                scheduler.request_stop();
            }
        });
    const auto paused = interpreter.run();
    require(paused.status == runtime::RunStatus::stopped,
        "Debug execution must pause at the statement boundary");
    require(points.size() == 1U
            && points.front().instruction == 0U
            && points.front().kind
                == runtime::simir::ExecutionPointKind::statement
            && points.front().source.path == "coverage_debug.sv"
            && points.front().source.line == 17U
            && points.front().source.column == 5U
            && points.front().scope == "top.covered",
        "Debug execution must preserve its exact source observation");
    const auto before = app::application_detail::debug_code_coverage_snapshot(
        interpreter.process_program(process),
        interpreter.code_coverage_counters());
    require(before.ok() && before.observations.size() == 1U
            && before.observations.front().point
                == runtime::CodeCoveragePointId {
                    0x1020304050607080ULL, 0x0102030405060708ULL }
            && before.observations.front().counter == runtime::CodeCoverageCounterId { 0U } && before.observations.front().hits == 0U,
        "A Debug pause before the statement must not manufacture a hit");

    interpreter.scheduler().clear_stop();
    const auto completed = interpreter.run();
    require(completed.status == runtime::RunStatus::completed,
        "Debug execution must resume through the covered statement");
    const auto after = app::application_detail::debug_code_coverage_snapshot(
        interpreter.process_program(process),
        interpreter.code_coverage_counters());
    require(after.observations.size() == 1U
            && after.observations.front().instruction == 1U
            && after.observations.front().point
                == before.observations.front().point
            && after.observations.front().metric
                == before.observations.front().metric
            && after.observations.front().counter
                == before.observations.front().counter
            && after.observations.front().hits == 1U,
        "Debug execution must retain point identity and record one hit");
}

void test_debug_snapshot_validation()
{
    auto representative = debug_covered_process();
    auto instance = debug_covered_process();
    auto* instance_hit = runtime::simir::operation_get_if<
        runtime::simir::CodeCoverageHit>(&instance.operations[1U]);
    require(instance_hit != nullptr, "Debug fixture lost its coverage hit");
    instance_hit->counter = { 5U };
    const std::vector signals {
        runtime::simir::Signal { "top.coverage_trigger",
            runtime::PackedLogic4::from_msb_string("0") }
    };
    require(runtime::simir::share_process_operations(
                representative, instance, signals, nullptr),
        "Debug coverage instances must share their immutable body");
    std::vector<std::uint64_t> counters(6U);
    counters[5U] = 7U;
    const auto compact = app::application_detail::debug_code_coverage_snapshot(
        instance, counters);
    require(compact.ok() && compact.observations.size() == 1U
            && compact.observations.front().counter
                == runtime::CodeCoverageCounterId { 5U }
            && compact.observations.front().hits == 7U,
        "Debug observation must resolve the effective instance counter");

    const auto unavailable
        = app::application_detail::debug_code_coverage_snapshot(
            representative, { });
    require(unavailable.error
            == app::application_detail::
                DebugCodeCoverageSnapshotError::counter_out_of_range,
        "Debug observation must reject unavailable counter storage");
    const auto bounded
        = app::application_detail::debug_code_coverage_snapshot(
            representative, counters, 0U);
    require(bounded.error
            == app::application_detail::
                DebugCodeCoverageSnapshotError::resource_limit,
        "Debug observation must enforce its point ceiling");

    auto duplicate = representative;
    duplicate.operations = {
        duplicate.operations[1U],
        duplicate.operations[1U],
    };
    const auto duplicate_snapshot
        = app::application_detail::debug_code_coverage_snapshot(
            duplicate, counters);
    require(duplicate_snapshot.error
            == app::application_detail::
                DebugCodeCoverageSnapshotError::duplicate_point,
        "Debug observation must reject duplicate point identities");
}

void run_compiled(
    const compiler::JitOptimizationLevel optimization,
    const std::uint64_t initial,
    const std::uint64_t expected,
    const bool expect_overflow)
{
    auto representative = covered_process(0U);
    auto instance = covered_process(5U);
    const std::vector signals {
        runtime::simir::Signal {
            "top.coverage_trigger",
            runtime::PackedLogic4::from_msb_string("0") }
    };
    require(runtime::simir::share_process_operations(
                representative,
                instance,
                signals,
                nullptr),
        "coverage-equivalent instances must share their native body");

    compiler::LlvmJit jit {
        compiler::LlvmJitOptions { optimization, { } }
    };
    const auto symbol = std::string { "coverage_" }
        + std::string { compiler::to_string(optimization) }
        + (expect_overflow ? "_overflow" : "_increment");
    const std::array<std::uint32_t, 1> signal_widths { 1U };
    jit.add_process(symbol, representative, signal_widths);
    const auto handle = jit.lookup(symbol);

    runtime::simir::Interpreter interpreter;
    (void)interpreter.add_signal(signals[0]);
    const auto process = interpreter.add_process(instance);
    std::vector<std::uint64_t> counters(6U);
    counters[5U] = initial;
    interpreter.set_code_coverage_counters(std::move(counters));
    std::size_t overflow_calls { };
    interpreter.set_code_coverage_overflow_hook(
        [&](const runtime::CodeCoverageCounterId counter) {
            require(counter.value == 5U,
                "compiled overflow must retain the instance counter");
            ++overflow_calls;
        });
    interpreter.set_process_executor(
        process,
        std::make_unique<app::application_detail::LlvmProcessExecutor>(
            jit,
            handle,
            interpreter.process_program(process),
            signal_widths,
            std::span<const runtime::simir::ValueKind> { },
            std::span<const runtime::simir::ResolutionKind> { }));

    const auto result = interpreter.run();
    require(result.status == runtime::RunStatus::completed,
        "compiled coverage process must complete");
    require(interpreter.code_coverage_counters()[5U] == expected,
        "compiled coverage must update the effective instance counter");
    require(overflow_calls == (expect_overflow ? 1U : 0U),
        "compiled coverage overflow must be reported exactly once");
    require(interpreter.code_coverage_counter_overflowed({ 5U })
            == expect_overflow,
        "compiled coverage saturation state must match the interpreter");
}

void run_compiled_failure(
    const bool configure_counters,
    const std::string_view expected)
{
    auto process_program = covered_process(5U);
    compiler::LlvmJit jit;
    constexpr std::string_view symbol = "coverage_failure";
    const std::array<std::uint32_t, 1> signal_widths { 1U };
    jit.add_process(symbol, process_program, signal_widths);
    const auto handle = jit.lookup(symbol);

    runtime::simir::Interpreter interpreter;
    (void)interpreter.add_signal({ "top.coverage_trigger",
        runtime::PackedLogic4::from_msb_string("0") });
    const auto process = interpreter.add_process(process_program);
    if (configure_counters) {
        interpreter.set_code_coverage_counters({ 0U });
    }
    interpreter.set_process_executor(
        process,
        std::make_unique<app::application_detail::LlvmProcessExecutor>(
            jit,
            handle,
            interpreter.process_program(process),
            signal_widths,
            std::span<const runtime::simir::ValueKind> { },
            std::span<const runtime::simir::ResolutionKind> { }));
    try {
        (void)interpreter.run();
        require(false, "compiled coverage accepted an invalid counter service");
    } catch (const runtime::simir::InterpreterError& error) {
        require(std::string_view { error.what() }.find(expected)
                != std::string_view::npos,
            "compiled coverage failure diagnostic mismatch");
    }
}

} // namespace

int main()
{
    using fsim::compiler::JitOptimizationLevel;
    run_compiled(JitOptimizationLevel::o0, 41U, 42U, false);
    run_compiled(JitOptimizationLevel::o1, 41U, 42U, false);
    run_compiled(JitOptimizationLevel::o2, 41U, 42U, false);
    run_compiled(
        JitOptimizationLevel::o2,
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max(),
        true);
    run_compiled_failure(false, "service is unavailable");
    run_compiled_failure(true, "counter is out of range");
    run_debug_engine(false);
    run_debug_engine(true);
    test_debug_snapshot_validation();
    require(
        fsim::app::application_detail::jit_optimization(
            fsim::project::Optimization::o3)
            == JitOptimizationLevel::o2,
        "application O3 must retain the qualified optimized native profile");
    std::cout << "compiled code coverage application tests passed\n";
}
