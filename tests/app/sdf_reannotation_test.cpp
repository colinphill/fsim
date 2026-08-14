// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_reannotation.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error { std::string { message } };
}

void require_diagnostic(
    const std::vector<fsim::frontend::Diagnostic>& diagnostics,
    const std::string_view code)
{
    require(std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.code == code;
    }),
        "expected cataloged SDF reannotation diagnostic");
}

constexpr std::array<std::string_view, 4> instances {
    "alpha.u0", "alpha.u1", "beta.u0", "beta.u1"
};

fsim::elaboration::ElaboratedDesign make_design(
    const std::array<fsim::runtime::SimulationTick, 4>& delays,
    const std::int64_t check_limit,
    const bool change_topology = false)
{
    using namespace fsim;
    using namespace runtime::simir;
    elaboration::ElaboratedDesignState state;
    state.top = "alpha";
    state.roots = { "alpha", "beta" };
    for (std::size_t index = 0; index < instances.size(); ++index) {
        for (const auto suffix : { ".source", ".output" }) {
            const auto id = static_cast<SignalId>(state.signal_info.size());
            const auto name = std::string { instances[index] } + suffix;
            elaboration::SignalInfo info;
            info.id = id;
            info.name = name;
            info.width = 1U;
            info.type_name = "logic";
            info.source_domain = frontend::ValueDomain::Logic4;
            state.signal_info.push_back(std::move(info));
            state.signals.emplace_back(name,
                runtime::PackedLogic4::from_msb_string("0"));
            state.signal_names.emplace_back(name, id);
        }
    }

    Process initial;
    initial.id = 0U;
    initial.name = "initial-source";
    initial.register_count = 1U;
    initial.driver_regions = { { 0U, 0U, 1U, true } };
    initial.operations = {
        LoadConstant { 0U, runtime::PackedLogic4::from_msb_string("1") },
        WriteUpdate { 0U, 0U }, Halt { }
    };
    state.processes.push_back(std::move(initial));

    Process concurrent;
    concurrent.id = 1U;
    concurrent.name = "concurrent-sources";
    concurrent.register_count = 1U;
    concurrent.driver_regions = { { 2U, 0U, 1U, true },
        { 4U, 0U, 1U, true }, { 6U, 0U, 1U, true } };
    concurrent.operations = { WaitFor { 2U },
        LoadConstant { 0U, runtime::PackedLogic4::from_msb_string("1") },
        WriteUpdate { 2U, 0U }, WriteUpdate { 4U, 0U },
        WriteUpdate { 6U, 0U }, Halt { } };
    state.processes.push_back(std::move(concurrent));

    for (std::size_t index = 0; index < instances.size(); ++index) {
        const auto source = static_cast<SignalId>(index * 2U);
        const auto output = source + 1U;
        const auto driver_id = static_cast<ProcessId>(index + 2U);
        Process driver;
        driver.id = driver_id;
        driver.name = "path-driver-" + std::to_string(index);
        driver.register_count = 1U;
        driver.static_sensitivity = { { source, EdgeKind::any } };
        driver.driver_regions = { { output, 0U, 1U, true } };
        driver.initialize = false;
        driver.operations = { ReadSignal { 0U, source },
            WriteUpdate { output, 0U }, WaitSensitivity { }, Jump { 0U } };
        state.processes.push_back(std::move(driver));

        elaboration::VerilogSpecifyPathInfo path;
        path.id = static_cast<elaboration::VerilogSpecifyPathId>(index);
        path.identity = "sdf:iopath:" + std::string { instances[index] }
            + ":" + std::to_string(index);
        path.instance = instances[index];
        path.sources = { { change_topology && index == 0U ? 2U : source,
            0U, 1U } };
        path.destinations = { { output, 0U, 1U } };
        path.drivers = { driver_id };
        path.delays = { delays[index] };
        state.verilog_specify_paths.push_back(std::move(path));
    }

    ModuleTimingCheck check;
    check.id = 0U;
    check.identity = "sdf:timingcheck:alpha.u0:hold:0";
    check.kind = ModuleTimingCheckKind::hold;
    check.reference.terminal = { 0U, 0U, 1U };
    check.data.emplace();
    check.data->terminal = { 2U, 0U, 1U };
    check.limits = { check_limit };
    state.verilog_timing_checks.push_back(std::move(check));

    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "reannotation design fixture must validate");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfDriveTimingApplication>
make_application(
    const std::array<fsim::runtime::SimulationTick, 4>& delays,
    const std::int64_t check_limit,
    const std::string_view identity,
    const bool change_topology = false)
{
    using namespace fsim::app;
    auto scheduling = std::make_shared<const SdfSchedulingApplication>(
        nullptr, make_design(delays, check_limit, change_topology),
        std::vector<SdfScheduledTimingTarget> { },
        "scheduling-" + std::string { identity });
    return std::make_shared<const SdfDriveTimingApplication>(
        std::move(scheduling), std::vector<SdfDriveTimingBinding> { },
        "drive-" + std::string { identity });
}

fsim::app::SdfReannotationLayer layer(
    const std::uint64_t file_precedence,
    const std::uint64_t cell_precedence,
    std::string file,
    std::string root,
    std::string pattern,
    std::shared_ptr<const fsim::app::SdfDriveTimingApplication> timing)
{
    return { file_precedence, cell_precedence, std::move(file),
        std::move(root), std::move(pattern), std::move(timing) };
}

void test_ordered_files_wildcards_and_roots()
{
    using namespace fsim::app;
    const auto baseline = make_application({ 9U, 9U, 9U, 9U }, 1, "base");
    const auto alpha = make_application({ 3U, 4U, 30U, 40U }, 2, "alpha");
    const auto alpha_override
        = make_application({ 5U, 50U, 50U, 50U }, 5, "override");
    const auto beta = make_application({ 60U, 60U, 7U, 8U }, 6, "beta");
    std::vector<SdfReannotationLayer> layers {
        layer(1U, 0U, "alpha.sdf", "alpha", "u*", alpha),
        layer(2U, 0U, "override.sdf", "alpha", "u0", alpha_override),
        layer(1U, 1U, "beta.sdf", "beta", "*", beta)
    };
    const auto result = apply_sdf_reannotation(baseline, layers, 7U);
    require(result.ok(), "ordered multi-file reannotation must publish");
    require(result.application->generation() == 7U,
        "reannotation generation must be exact");
    require(result.application->pending_event_policy()
                == SdfPendingEventPolicy::PreserveScheduledTiming
            && result.application->timing_check_state_policy()
                == SdfTimingCheckStatePolicy::PreserveHistory,
        "runtime reannotation policies must be explicit");
    const auto paths = result.application->design().verilog_specify_paths();
    require(paths[0].delays == std::vector<fsim::runtime::SimulationTick> { 5U }
            && paths[1].delays
                == std::vector<fsim::runtime::SimulationTick> { 4U }
            && paths[2].delays
                == std::vector<fsim::runtime::SimulationTick> { 7U }
            && paths[3].delays
                == std::vector<fsim::runtime::SimulationTick> { 8U },
        "file and cell precedence must select exact root-local targets");
    require(result.application->design().verilog_timing_checks()[0].limits
            == std::vector<std::int64_t> { 5 },
        "higher-precedence file must replace timing-check limits");
    require(result.application->revisions().size() == 5U,
        "each selected path or timing check must publish one revision");

    std::ranges::reverse(layers);
    const auto reversed = apply_sdf_reannotation(baseline, layers, 7U);
    require(reversed.ok()
            && reversed.application->semantic_identity()
                == result.application->semantic_identity()
            && std::ranges::equal(reversed.application->revisions(),
                result.application->revisions()),
        "explicit precedence must make input file order deterministic");
}

void test_duplicate_conflict_and_rollback()
{
    using namespace fsim::app;
    const auto baseline = make_application({ 9U, 9U, 9U, 9U }, 1, "base");
    const auto same = make_application({ 3U, 9U, 9U, 9U }, 2, "same");
    const auto conflict
        = make_application({ 4U, 9U, 9U, 9U }, 3, "conflict");
    const std::vector<SdfReannotationLayer> duplicate_layers {
        layer(1U, 0U, "a.sdf", "alpha", "u0", same),
        layer(1U, 0U, "b.sdf", "alpha", "u0", same)
    };
    const auto duplicate
        = apply_sdf_reannotation(baseline, duplicate_layers, 1U);
    require(!duplicate.ok() && !duplicate.application,
        "equal-precedence duplicate must roll back");
    require_diagnostic(
        duplicate.diagnostics, "FSIM-SDF-REANNOTATION-002");

    const std::vector<SdfReannotationLayer> conflict_layers {
        layer(1U, 0U, "a.sdf", "alpha", "u0", same),
        layer(1U, 0U, "b.sdf", "alpha", "u0", conflict)
    };
    const auto conflict_result
        = apply_sdf_reannotation(baseline, conflict_layers, 1U);
    require(!conflict_result.ok() && !conflict_result.application,
        "equal-precedence conflict must roll back");
    require_diagnostic(
        conflict_result.diagnostics, "FSIM-SDF-REANNOTATION-003");
    require(baseline->scheduling()->design().verilog_specify_paths()[0].delays
            == std::vector<fsim::runtime::SimulationTick> { 9U },
        "failed transaction must leave immutable baseline unchanged");

    const auto mismatched
        = make_application({ 3U, 9U, 9U, 9U }, 2, "mismatch", true);
    const std::array topology_layer {
        layer(1U, 0U, "bad.sdf", "alpha", "u0", mismatched)
    };
    const auto topology
        = apply_sdf_reannotation(baseline, topology_layer, 1U);
    require(!topology.ok(), "topology-changing layer must reject");
    require_diagnostic(
        topology.diagnostics, "FSIM-SDF-REANNOTATION-003");
}

void test_resources_and_missing_scopes()
{
    using namespace fsim::app;
    const auto baseline = make_application({ 9U, 9U, 9U, 9U }, 1, "base");
    const auto timing = make_application({ 3U, 4U, 5U, 6U }, 2, "layer");
    const std::array valid {
        layer(1U, 0U, "one.sdf", "alpha", "*", timing)
    };
    auto limited = apply_sdf_reannotation(baseline, valid, 1U,
        { .max_files = 1U,
            .max_targets = 1U,
            .max_pattern_bytes = 1U << 20U,
            .max_identity_bytes = 1U << 20U });
    require(!limited.ok(), "target ceiling must reject atomically");
    require_diagnostic(limited.diagnostics, "FSIM-SDF-REANNOTATION-004");

    const std::array missing {
        layer(1U, 0U, "none.sdf", "alpha", "missing*", timing)
    };
    const auto missing_result
        = apply_sdf_reannotation(baseline, missing, 1U);
    require(!missing_result.ok(), "unmatched wildcard cell must reject");
    require_diagnostic(
        missing_result.diagnostics, "FSIM-SDF-REANNOTATION-001");

    const std::array bad_root {
        layer(1U, 0U, "root.sdf", "unknown", "*", timing)
    };
    const auto root_result
        = apply_sdf_reannotation(baseline, bad_root, 1U);
    require(!root_result.ok(), "unknown elaboration root must reject");
    require_diagnostic(
        root_result.diagnostics, "FSIM-SDF-REANNOTATION-001");
}

void test_safe_point_pending_events_and_check_history()
{
    using namespace fsim;
    using namespace app;
    using namespace runtime;
    using namespace runtime::simir;
    const auto baseline = make_application({ 5U, 5U, 5U, 5U }, 1, "runtime");
    const auto replacement
        = make_application({ 1U, 1U, 1U, 1U }, 5, "replacement");
    const std::array layers {
        layer(1U, 0U, "live.sdf", "alpha", "*", replacement),
        layer(1U, 0U, "live.sdf", "beta", "*", replacement)
    };
    const auto transaction
        = apply_sdf_reannotation(baseline, layers, 11U);
    require(transaction.ok(), "live reannotation transaction must publish");

    auto interpreter = baseline->scheduling()->design().create_interpreter();
    const auto dummy = interpreter->add_signal(
        { "reannotation.safe-point", PackedLogic4::from_msb_string("0") });
    std::vector<std::pair<SimulationTick, SignalId>> changes;
    interpreter->set_signal_change_hook([&](const SignalId signal,
                                            const PackedLogic4& value,
                                            const SimulationTick time) {
        if ((signal == 1U || signal == 3U || signal == 5U || signal == 7U)
            && value.to_msb_string() == "1") {
            changes.emplace_back(time, signal);
        }
    });
    std::size_t reports { };
    interpreter->set_report_hook([&](const ProcessId,
                                     const std::string_view,
                                     const AssertionSeverity,
                                     const SourceLocation&,
                                     const SimulationTick,
                                     const std::uint64_t) { ++reports; });
    interpreter->start();
    const auto outside
        = commit_sdf_reannotation(*transaction.application, *interpreter);
    require(!outside.ok(),
        "started interpreter must reject reannotation outside safe point");
    require_diagnostic(
        outside.diagnostics, "FSIM-SDF-REANNOTATION-001");

    bool committed { };
    std::string observed_identity;
    interpreter->scheduler().set_safe_point_hook(
        [&](Scheduler& scheduler, const SchedulerPhase phase) {
            if (!committed && scheduler.now() == 1U
                && phase == SchedulerPhase::postponed) {
                require(scheduler.at_safe_point(),
                    "annotation hook must expose its safe-point state");
                const auto first = commit_sdf_reannotation(
                    *transaction.application, *interpreter);
                const auto second = commit_sdf_reannotation(
                    *transaction.application, *interpreter);
                require(first.ok() && second.ok()
                        && first.generation == 11U
                        && second.generation == first.generation,
                    "all same-safe-point observers must see one generation");
                observed_identity = transaction.application->semantic_identity();
                committed = true;
            }
        });
    interpreter->schedule_signal_at(dummy,
        PackedLogic4::from_msb_string("1"), 1U, 0U);
    const auto run = interpreter->run();
    require(run.status == RunStatus::completed && committed,
        "safe-point reannotation run must complete and commit");
    require(!interpreter->scheduler().at_safe_point()
            && observed_identity
                == transaction.application->semantic_identity(),
        "safe-point state must close after deterministic publication");

    std::ranges::sort(changes);
    require(std::ranges::find(
                changes, std::pair<SimulationTick, SignalId> { 5U, 1U })
            != changes.end(),
        "pending path event must retain its originally scheduled delay");
    require(std::ranges::find(changes,
                std::pair<SimulationTick, SignalId> { 3U, 3U })
                != changes.end()
            && std::ranges::find(changes,
                   std::pair<SimulationTick, SignalId> { 3U, 5U })
                != changes.end()
            && std::ranges::find(changes,
                   std::pair<SimulationTick, SignalId> { 3U, 7U })
                != changes.end(),
        "concurrent post-commit paths must observe new timing together");
    require(reports == 1U,
        "pre-commit timing-check reference history must survive reannotation (reports="
            + std::to_string(reports) + ")");
}
}

int main()
{
    try {
        test_ordered_files_wildcards_and_roots();
        test_duplicate_conflict_and_rollback();
        test_resources_and_missing_scopes();
        test_safe_point_pending_events_and_check_history();
        std::cout << "sdf reannotation tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "sdf reannotation tests failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
