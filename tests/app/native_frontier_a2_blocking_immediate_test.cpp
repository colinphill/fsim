// SPDX-License-Identifier: Apache-2.0

#include "native_frontier_a2_blocking_test_support.hpp"

#include "fsim/app/application.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>



namespace {

using fsim::app::Simulation;
using fsim::runtime::RunStatus;
using fsim::runtime::Scheduler;
using fsim::runtime::SchedulerPhase;
using fsim::runtime::SchedulerTraceKind;
using fsim::runtime::SchedulerTraceRecord;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::runtime::simir::ProcessId;
using fsim::runtime::simir::SignalId;
using fsim::tests::app::a2_blocking::FixtureKind;
using fsim::tests::app::a2_blocking::ScopedEnvironment;
using fsim::tests::app::a2_blocking::TemporaryDirectory;
using fsim::tests::app::a2_blocking::find_signals;
using fsim::tests::app::a2_blocking::fixture_name;
using fsim::tests::app::a2_blocking::make_config;
using fsim::tests::app::a2_blocking::require;
using fsim::tests::app::a2_blocking::snapshot_all;
using NativeAccess = NativeRegionAllocationTestAccess;
using Key = NativeAccess::Key;
using Receipt = NativeAccess::Receipt;
using Snapshot = NativeAccess::SignalSnapshot;
using SemanticSnapshot = NativeAccess::SemanticSnapshot;
using Cut = NativeAccess::Cut;

struct RunResult {
    std::vector<SemanticSnapshot> semantics;
    bool cut { };
    bool private_blocking_row { };
};

struct TraceProbe {
    ProcessId root { };
    Receipt pending;
    std::size_t pending_begins { };
    std::size_t pending_ends { };
    std::size_t root_replays { };

    static bool matches(const SchedulerTraceRecord& record,
        const Key& key) noexcept
    {
        return record.systemverilog && record.phase
            && record.time == key.time && record.delta == key.delta
            && record.systemverilog_round == key.round
            && record.order == key.order && record.sequence == key.sequence
            && static_cast<std::uint32_t>(*record.phase) == key.phase;
    }

    static void receive(void* const context,
        const SchedulerTraceRecord& record) noexcept
    {
        auto& probe = *static_cast<TraceProbe*>(context);
        if ((record.kind == SchedulerTraceKind::task_begin
                || record.kind == SchedulerTraceKind::batch_begin)
            && record.systemverilog && record.phase && record.time == 1U
            && record.order == probe.root) {
            ++probe.root_replays;
        }
        if (record.kind == SchedulerTraceKind::task_begin
            && matches(record, probe.pending.key)) {
            ++probe.pending_begins;
        }
        if (record.kind == SchedulerTraceKind::task_end
            && matches(record, probe.pending.key)) {
            ++probe.pending_ends;
        }
    }
};

[[nodiscard]] RunResult run_case(
    const FixtureKind kind,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine,
    const std::string_view suffix,
    const bool expect_private = true)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path, optimization, kind);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(), "parsed blocking fixture must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    if (engine == fsim::app::SimulationEngine::compiled) {
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "compiled blocking fixture must install native processes");
    }
    const auto signals = find_signals(simulation, kind);
    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "blocking fixture must settle its initial state before time one");

    RunResult result;
    if (kind == FixtureKind::positive
        && engine == fsim::app::SimulationEngine::compiled
        && expect_private) {
        const auto component = NativeAccess::component_for_signal(
            simulation, signals.root);
        require(NativeAccess::component_for_signal(simulation, *signals.middle)
                    == component
                && NativeAccess::component_for_signal(simulation, signals.sink)
                    == component,
            "parsed blocking chain must share a certified component");
        const auto root_owner = NativeAccess::output_owner(simulation,
            component, signals.root,
            fsim::runtime::simir::RegionOutputPublicationKind::blocking_immediate);
        const auto middle_owner = NativeAccess::output_owner(simulation,
            component, *signals.middle,
            fsim::runtime::simir::RegionOutputPublicationKind::blocking_immediate);
        const auto sink_owner = NativeAccess::output_owner(simulation,
            component, signals.sink,
            fsim::runtime::simir::RegionOutputPublicationKind::blocking_immediate);
        const auto root_before = NativeAccess::snapshot(
            simulation, component, signals.root, root_owner);
        const auto middle_before = NativeAccess::snapshot(
            simulation, component, *signals.middle, middle_owner);
        require(!root_before.raw_record_present
                && !middle_before.raw_record_present,
            "the certified blocking output owners have no raw DriverTable records");
        const auto eval_before = NativeAccess::forwarding_evaluations(simulation);
        const auto suppressions_before
            = NativeAccess::private_fanout_suppressions(simulation);
        Cut cut;
        std::optional<Receipt> root_receipt;
        std::optional<Receipt> middle_receipt;
        const auto hook = simulation.add_safe_point_hook(
            [&simulation, signals, component, root_owner, middle_owner,
                root_before, middle_before, eval_before, suppressions_before,
                &cut, &root_receipt, &middle_receipt](Scheduler& scheduler,
                const SchedulerPhase phase) {
                if (cut.captured || scheduler.now() != 1U
                    || phase != SchedulerPhase::active) {
                    return;
                }
                if (!root_receipt) {
                    root_receipt = NativeAccess::readiness_receipt(
                        simulation, component, root_owner);
                }
                if (!middle_receipt) {
                    middle_receipt = NativeAccess::readiness_receipt(
                        simulation, component, middle_owner);
                }
                if (!root_receipt || !middle_receipt) {
                    return;
                }
                if (NativeAccess::try_capture_positive_cut(simulation,
                        scheduler, phase, signals.root, *signals.middle,
                        signals.sink, root_before, middle_before,
                        *root_receipt, *middle_receipt,
                        eval_before, suppressions_before, cut)) {
                    scheduler.request_stop();
                }
            });
        const auto stopped = simulation.run(1U);
        simulation.remove_safe_point_hook(hook);
        require(stopped.status == RunStatus::stopped && stopped.time == 1U
                && cut.captured && cut.row_count == 2U,
            "blocking chain must stop with its exact private role prefix and sink key");
        require(cut.rows[0U].signal == signals.root
                && cut.rows[1U].signal == *signals.middle
                && cut.root_receipt.process == root_owner
                && cut.middle_receipt.process == middle_owner
                && cut.rows[0U].callback_key == cut.root_receipt.key
                && cut.rows[1U].callback_key == cut.middle_receipt.key
                && cut.rows[0U].callback_key.order == root_owner
                && cut.rows[1U].callback_key.order == middle_owner
                && cut.rows[0U].callback_order < cut.rows[1U].callback_order,
            "private blocking rows must retain original callback order and owners");
        require(NativeAccess::precedes(cut.root_receipt.key,
                    cut.middle_receipt.key)
                && NativeAccess::precedes(cut.middle_receipt.key,
                    cut.sink_receipt.key)
                && cut.sink_receipt.process == sink_owner
                && cut.sink_receipt.key.time == 1U,
            "the original producer callback must precede the authentic queued boundary key");

        const auto observed = simulation.read_signal(signals.root).to_msb_string();
        require(observed == cut.rows[0U].predicted
                && NativeAccess::journal_empty(simulation, cut)
                && NativeAccess::receipt_unchanged(simulation, cut.sink_receipt),
            "public observation must flush private roles while preserving the boundary key");
        for (const auto& row : cut.rows) {
            const auto after = NativeAccess::snapshot(simulation,
                cut.component, row.signal, row.owner);
            require(after.current == row.predicted
                    && after.stored == row.predicted
                    && after.owner == row.predicted
                    && after.raw == row.predicted
                    && after.previous == row.before.current
                    && after.event == row.hidden.event
                    && after.transaction == row.hidden.transaction
                    && after.event_round == row.hidden.event_round
                    && after.revision == row.hidden.revision
                    && !after.materialization_pending,
                "role flush must publish values without replaying metadata");
        }

        TraceProbe trace { root_owner, cut.sink_receipt };
        NativeAccess::set_trace_hook(simulation, &trace, &TraceProbe::receive);
        simulation.clear_stop();
        const auto completed = simulation.run();
        NativeAccess::set_trace_hook(simulation, nullptr, nullptr);
        require(completed.status == RunStatus::stopped && completed.time == 3U
                && trace.pending_begins == 1U && trace.pending_ends == 1U
                && trace.root_replays == 0U,
            "checked boundary tail must consume its original key once without producer replay");
        result.cut = true;
    } else {
        const auto suppressions_before
            = NativeAccess::private_fanout_suppressions(simulation);
        const auto completed = simulation.run();
        require(completed.status == RunStatus::stopped && completed.time == 3U,
            "blocking control must complete through ordinary simulation");
        if (engine == fsim::app::SimulationEngine::compiled) {
            result.private_blocking_row
                = NativeAccess::has_blocking_private_rows(simulation,
                    signals.root);
            require(NativeAccess::private_fanout_suppressions(simulation)
                        == suppressions_before,
                "unsupported blocking shape must not publish a private role row");
            require(!result.private_blocking_row,
                "unsupported blocking shape must remain on checked publication");
        }
    }
    result.semantics = snapshot_all(simulation, signals);
    if (kind == FixtureKind::positive) {
        require(result.semantics.size() == 4U
                && result.semantics[1U].raw_driver_records_absent
                && result.semantics[2U].raw_driver_records_absent,
            "engine snapshots preserve the absent raw-record state of blocking outputs");
    }
    return result;
}

void test_positive()
{
    const auto interpreter = run_case(FixtureKind::positive,
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::interpreter, "positive-interpreter");
    const auto o0 = run_case(FixtureKind::positive,
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::compiled, "positive-o0");
    const auto o2 = run_case(FixtureKind::positive,
        fsim::project::Optimization::o2,
        fsim::app::SimulationEngine::compiled, "positive-o2");
    require(o0.semantics == interpreter.semantics
            && o2.semantics == interpreter.semantics,
        "blocking O0/O2 must preserve interpreter values, drivers, and event metadata");
    require(o0.cut && o2.cut,
        "both compiled optimization modes must use original-key private blocking writes");
}

void test_checked_positive_fallback()
{
    ScopedEnvironment local_wave_off { "FSIM_ENABLE_SV_LOCAL_WAVE", "0" };
    const auto interpreter = run_case(FixtureKind::positive,
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::interpreter,
        "checked-positive-interpreter", false);
    const auto o0 = run_case(FixtureKind::positive,
        fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::compiled,
        "checked-positive-o0", false);
    const auto o2 = run_case(FixtureKind::positive,
        fsim::project::Optimization::o2,
        fsim::app::SimulationEngine::compiled,
        "checked-positive-o2", false);
    require(o0.semantics == interpreter.semantics
            && o2.semantics == interpreter.semantics,
        "local-wave-disabled blocking chain must preserve interpreter semantics");
    require(!o0.private_blocking_row && !o2.private_blocking_row
            && !o0.cut && !o2.cut,
        "local-wave-disabled blocking chain must use checked publication");
}

void test_checked_controls()
{
    constexpr std::array cases {
        FixtureKind::repeated_write,
        FixtureKind::partial_write,
        FixtureKind::mixed_boundary,
        FixtureKind::unsupported_effect };
    for (const auto kind : cases) {
        const auto interpreter = run_case(kind,
            fsim::project::Optimization::o0,
            fsim::app::SimulationEngine::interpreter,
            fixture_name(kind) + "-interpreter");
        const auto compiled = run_case(kind,
            fsim::project::Optimization::o0,
            fsim::app::SimulationEngine::compiled,
            fixture_name(kind) + "-compiled");
        require(compiled.semantics == interpreter.semantics,
            "unsupported blocking shapes must retain checked interpreter parity");
    }
}

} // namespace

int main()
{
    try {
        ScopedEnvironment region_kernel { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave { "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
        ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
        ScopedEnvironment native_counts { "FSIM_PROFILE_NATIVE_PROCESS_COUNTS", "1" };
        ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };
        ScopedEnvironment wide_single { "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
        ScopedEnvironment wide_disjoint { "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", nullptr };
        test_positive();
        test_checked_positive_fallback();
        test_checked_controls();
    } catch (const std::exception& error) {
        std::cerr << "A2 blocking-immediate witness failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
