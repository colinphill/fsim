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

void run_debug_engine()
{
    auto program = debug_covered_process();
    runtime::simir::Interpreter interpreter;
    (void)interpreter.add_signal({ "top.coverage_trigger",
        runtime::PackedLogic4::from_msb_string("0") });
    const auto process = interpreter.add_process(program);
    interpreter.set_code_coverage_counters({ 0U });

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

} // namespace

int main()
{
    run_debug_engine();
    test_debug_snapshot_validation();
    std::cout << "code coverage application tests passed\n";
}
