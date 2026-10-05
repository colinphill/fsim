// SPDX-License-Identifier: Apache-2.0

#include "native_frontier_a2_blocking_test_support.hpp"
#include "native_frontier_generic_update_failure_interposer.hpp"

#include "fsim/runtime/packed_value.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fsim::tests::app::a2_blocking_failure {
namespace {

using fsim::app::Simulation;
using fsim::app::SimulationEngine;
using fsim::project::Optimization;
using fsim::runtime::Logic4;
using fsim::runtime::PackedLogic4;
using fsim::runtime::RunResult;
using fsim::runtime::RunStatus;
using fsim::runtime::SchedulerPhase;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::runtime::simir::ProcessId;
using fsim::runtime::simir::SignalId;
using fsim::tests::app::a2_blocking::FixtureKind;
using fsim::tests::app::a2_blocking::ScopedEnvironment;
using fsim::tests::app::a2_blocking::TemporaryDirectory;
using fsim::tests::app::a2_blocking::find_signals;
using fsim::tests::app::a2_blocking::make_config;
using fsim::tests::app::a2_blocking::require;
using fsim::tests::app::a2_blocking::snapshot_all;
using fsim::tests::app::frontier::allocation_failure::clear;
using fsim::tests::app::frontier::allocation_failure::clear_observer;
using fsim::tests::app::frontier::allocation_failure::injected;
using fsim::tests::app::frontier::allocation_failure::reset;
using fsim::tests::app::frontier::allocation_failure::arm_when;
using fsim::tests::app::frontier::allocation_failure::set_observer;

using NativeAccess = NativeRegionAllocationTestAccess;
using Cut = NativeAccess::Cut;
using SignalSnapshot = NativeAccess::SignalSnapshot;
using SemanticSnapshot = NativeAccess::SemanticSnapshot;
using BlockingFailureSample = NativeAccess::BlockingFailureSample;
using BlockingFailurePredicateContext
    = NativeAccess::BlockingFailurePredicateContext;

struct FailureObserverContext final {
    const Simulation* simulation { };
    const Cut* cut { };
    SignalId root_signal { };
    SignalId middle_signal { };
    const SignalSnapshot* middle_before { };
    BlockingFailureSample* sample { };
};

void observe_failure(void* const context) noexcept
{
    auto& probe = *static_cast<FailureObserverContext*>(context);
    NativeAccess::sample_blocking_failure(*probe.simulation, *probe.cut,
        probe.root_signal, probe.middle_signal, *probe.middle_before,
        *probe.sample);
}

struct FailureObserverCleanup final {
    ~FailureObserverCleanup()
    {
        clear_observer();
        clear();
    }
};

struct FailureRun final {
    bool injected { };
    bool private_cut_captured { };
    bool exact_precommit_cut { };
    Cut cut;
    BlockingFailureSample sample;
    SignalSnapshot root_after;
    SignalSnapshot middle_after;
    std::vector<SemanticSnapshot> semantics;
};

[[nodiscard]] std::vector<SemanticSnapshot> run_interpreter_reference(
    const Optimization optimization,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path, optimization,
        FixtureKind::positive);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the parsed blocking reference must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    const auto signals = find_signals(simulation, FixtureKind::positive);
    simulation.start();
    const auto completed = simulation.run();
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the interpreter blocking reference must finish at the fixture stop");
    return snapshot_all(simulation, signals);
}

[[nodiscard]] FailureRun run_precommit_allocation_cut(
    const Optimization optimization,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path, optimization,
        FixtureKind::positive);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the parsed blocking allocation fixture must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the blocking fault fixture must install compiled executors");

    const auto signals = find_signals(simulation, FixtureKind::positive);
    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "the parsed blocking fault fixture must warm before allocation failure");
    simulation.await_all_native_compilation();

    const auto component = NativeAccess::component_for_signal(
        simulation, signals.root);
    require(signals.middle.has_value()
            && NativeAccess::component_for_signal(simulation, *signals.middle)
                == component
            && NativeAccess::component_for_signal(simulation, signals.sink)
                == component,
        "the blocking fault outputs must share their certified component");
    const auto root_owner = NativeAccess::output_owner(simulation,
        component, signals.root,
        fsim::runtime::simir::RegionOutputPublicationKind::blocking_immediate);
    const auto middle_owner = NativeAccess::output_owner(simulation,
        component, *signals.middle,
        fsim::runtime::simir::RegionOutputPublicationKind::blocking_immediate);
    const auto root_before = NativeAccess::snapshot(
        simulation, component, signals.root, root_owner);
    const auto middle_before = NativeAccess::snapshot(
        simulation, component, *signals.middle, middle_owner);
    require(!root_before.raw_record_present
            && !middle_before.raw_record_present
            && !root_before.materialization_pending
            && !middle_before.materialization_pending,
        "the private-stage fault fixture starts with aliased, materialized owner values");
    const auto eval_before = NativeAccess::forwarding_evaluations(simulation);
    FailureRun result;
    result.cut.component = component;
    result.cut.root = root_owner;
    result.cut.middle = middle_owner;
    result.cut.eval_before = eval_before;
    result.cut.rows[0U].signal = signals.root;
    result.cut.rows[0U].owner = root_owner;
    result.cut.rows[0U].before = root_before;
    result.cut.rows[0U].predicted = "0";

    BlockingFailurePredicateContext predicate { &simulation,
        signals.root, *signals.middle, root_owner,
        middle_owner, component, &root_before, &middle_before,
        eval_before, &result.cut, false };

    FailureObserverContext observer { &simulation, &result.cut,
        signals.root, *signals.middle, &middle_before, &result.sample };
    reset();
    set_observer(&observe_failure, &observer);
    arm_when(&NativeAccess::select_precommit_blocking_allocation,
        &predicate);
    bool threw_bad_alloc { };
    RunResult completed;
    {
        FailureObserverCleanup cleanup;
        try {
            completed = simulation.run();
        } catch (const std::bad_alloc&) {
            threw_bad_alloc = true;
        }
    }
    result.injected = injected();
    clear_observer();
    clear();
    result.private_cut_captured = predicate.matched
        && result.cut.captured;
    if (!result.injected) {
        return result;
    }

    result.exact_precommit_cut = result.private_cut_captured
        && result.cut.row_count == 1U
        && result.cut.rows[0U].signal == signals.root
        && result.cut.middle_receipt.process == middle_owner
        && NativeAccess::precedes(result.cut.root_receipt.key,
            result.cut.middle_receipt.key)
        && result.cut.root_receipt.key.order == root_owner
        && result.cut.middle_receipt.key.order == middle_owner
        && result.sample.observed
        && result.sample.exact_middle_frontier
        && result.sample.middle_receipt_unchanged
        && result.sample.root_only_private_row
        && result.sample.middle_unpublished
        && result.sample.middle_roles_unchanged
        && result.sample.middle_direct_planes_unchanged
        && result.sample.blocking_output_staged
        && result.sample.journal_row_count == 1U
        && result.sample.forwarding_attempts
            == result.cut.forwarding_attempts_at_cut;
    if (!result.exact_precommit_cut) {
        return result;
    }

    require(!threw_bad_alloc,
        "a precommit blocking-publication allocation failure falls back without escaping");
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the checked middle continuation must finish the parsed blocking fixture");
    result.root_after = NativeAccess::snapshot(
        simulation, component, signals.root, root_owner);
    result.middle_after = NativeAccess::snapshot(
        simulation, component, *signals.middle, middle_owner);
    require(NativeAccess::journal_empty(simulation, result.cut),
        "decline flushes the earlier committed root journal before checked continuation");
    require(result.root_after.current == result.cut.rows[0U].predicted
            && result.root_after.stored == result.cut.rows[0U].predicted
            && result.root_after.owner == result.cut.rows[0U].predicted
            && result.root_after.raw == result.cut.rows[0U].predicted
            && !result.root_after.raw_record_present
            && result.root_after.previous == result.cut.rows[0U].before.current
            && std::ranges::equal(result.root_after.direct_aval,
                result.root_after.current_value.aval_words())
            && std::ranges::equal(result.root_after.direct_bval,
                result.root_after.current_value.bval_words())
            && result.root_after.direct_last_aval
                == result.cut.rows[0U].before.direct_aval.front()
            && result.root_after.direct_last_bval
                == result.cut.rows[0U].before.direct_bval.front()
            && result.root_after.event == result.cut.rows[0U].hidden.event
            && result.root_after.transaction
                == result.cut.rows[0U].hidden.transaction
            && result.root_after.origin_domain
                == result.cut.rows[0U].hidden.origin_domain
            && result.root_after.origin_phase
                == result.cut.rows[0U].hidden.origin_phase
            && result.root_after.event_round
                == result.cut.rows[0U].hidden.event_round
            && result.root_after.revision
                == result.cut.rows[0U].hidden.revision
            && !result.root_after.materialization_pending,
        "the precommit decline materializes the root value, refreshes direct current planes, and preserves its metadata");
    require(result.middle_after.current == result.cut.rows[0U].predicted
            && result.middle_after.stored == result.cut.rows[0U].predicted
            && result.middle_after.owner == result.cut.rows[0U].predicted
            && result.middle_after.raw == result.cut.rows[0U].predicted
            && !result.middle_after.raw_record_present
            && result.middle_after.direct_aval
                == result.root_after.direct_aval
            && result.middle_after.direct_bval
                == result.root_after.direct_bval
            && result.middle_after.direct_last_aval
                == middle_before.direct_aval.front()
            && result.middle_after.direct_last_bval
                == middle_before.direct_bval.front()
            && result.middle_after.previous == middle_before.current
            && result.middle_after.event != middle_before.event
            && result.middle_after.transaction != middle_before.transaction
            && result.middle_after.revision > middle_before.revision
            && result.middle_after.origin_domain
                == static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
            && result.middle_after.origin_phase
                == static_cast<std::uint32_t>(SchedulerPhase::active),
        "the checked middle write commits once with its new value and event metadata");
    require(NativeAccess::native_resume_count(simulation, root_owner)
                == result.cut.root_resume_count_at_cut
            && NativeAccess::native_resume_count(simulation, middle_owner)
                == result.cut.middle_resume_count_at_cut + 1U,
        "the failure does not replay the committed root and resumes middle once through its checked callback");
    require(!NativeAccess::readiness_receipt(
                simulation, component, root_owner)
            && !NativeAccess::readiness_receipt(
                simulation, component, middle_owner),
        "the checked continuation retires both the private root and original middle receipts");
    require(NativeAccess::forwarding_declines(simulation)
                >= result.sample.blocking_declines + 1U,
        "the failed blocking publication is counted as a forwarding decline before checked continuation");
    result.semantics = snapshot_all(simulation, signals);
    return result;
}

struct ControlRun final {
    std::vector<SemanticSnapshot> semantics;
    std::uint64_t suppressions_before { };
    std::uint64_t suppressions_after { };
    std::uint64_t root_resume_delta { };
    std::uint64_t middle_resume_delta { };
    std::size_t hook_calls { };
    bool forced { };
    bool private_row { };
};

[[nodiscard]] ControlRun run_exclusion_control(
    const SimulationEngine engine,
    const bool force_root,
    const bool install_observer,
    const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    const auto config = make_config(temporary.path, Optimization::o0,
        FixtureKind::positive);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the blocking exclusion control must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        engine, fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    if (engine == SimulationEngine::compiled) {
        simulation.await_all_native_compilation();
        require(simulation.compiled_process_count() != 0U,
            "the blocking exclusion control must install compiled executors");
    }
    const auto signals = find_signals(simulation, FixtureKind::positive);
    ProcessId root_owner { };
    ProcessId middle_owner { };
    std::size_t hook_calls { };
    if (force_root) {
        simulation.force_signal(signals.root,
            PackedLogic4 { 1U, Logic4::one });
        require(simulation.signal_is_forced(signals.root),
            "the force exclusion must be active before certification");
    }
    if (install_observer) {
        simulation.set_signal_change_hook(
            [&hook_calls](const SignalId, const PackedLogic4&,
                const fsim::runtime::SimulationTick, const std::uint64_t) {
                ++hook_calls;
            });
    }
    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "the exclusion control must settle startup before its stimulus");
    if (engine == SimulationEngine::compiled) {
        require(signals.middle.has_value(),
            "the exclusion fixture retains its middle output");
        root_owner = NativeAccess::unique_whole_writer_for_signal(
            simulation, signals.root);
        middle_owner = NativeAccess::unique_whole_writer_for_signal(
            simulation, *signals.middle);
        require(root_owner != middle_owner,
            "the blocking exclusion control retains distinct owner callbacks");
    }
    const auto root_resumes_before = engine == SimulationEngine::compiled
        ? NativeAccess::native_resume_count(simulation, root_owner) : 0U;
    const auto middle_resumes_before = engine == SimulationEngine::compiled
        ? NativeAccess::native_resume_count(simulation, middle_owner) : 0U;
    const auto suppressions_before
        = NativeAccess::private_fanout_suppressions(simulation);
    const auto completed = simulation.run();
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the exclusion control must finish through checked execution");
    ControlRun result;
    result.suppressions_before = suppressions_before;
    result.suppressions_after
        = NativeAccess::private_fanout_suppressions(simulation);
    result.hook_calls = hook_calls;
    result.forced = force_root && simulation.signal_is_forced(signals.root);
    result.private_row
        = NativeAccess::has_blocking_private_rows(simulation, signals.root);
    result.semantics = snapshot_all(simulation, signals);
    if (engine == SimulationEngine::compiled) {
        const auto root_resumes_after
            = NativeAccess::native_resume_count(simulation, root_owner);
        const auto middle_resumes_after
            = NativeAccess::native_resume_count(simulation, middle_owner);
        require(root_resumes_after >= root_resumes_before
                && middle_resumes_after >= middle_resumes_before,
            "checked callback counters remain monotonic over the stimulus");
        result.root_resume_delta
            = root_resumes_after - root_resumes_before;
        result.middle_resume_delta
            = middle_resumes_after - middle_resumes_before;
    }
    require(result.semantics.size() == 4U
            && result.semantics[1U].raw_driver_records_absent
            && result.semantics[2U].raw_driver_records_absent,
        "force and observation controls preserve driverless output-record semantics");
    return result;
}

void test_precommit_failure()
{
    for (const auto optimization : { Optimization::o0, Optimization::o2 }) {
        const auto reference = run_interpreter_reference(optimization,
            optimization == Optimization::o0
                ? "blocking-reference-o0" : "blocking-reference-o2");
        const auto result = run_precommit_allocation_cut(optimization,
            optimization == Optimization::o0
                ? "blocking-precommit-o0" : "blocking-precommit-o2");
        require(result.injected && result.exact_precommit_cut,
            "the injected allocator cut reaches staged blocking output before publication");
        require(result.semantics == reference,
            "the precommit failure and checked continuation preserve interpreter values, raw-record absence, and event metadata");
    }
}

void test_force_and_observation_exclusions()
{
    for (const bool force_root : { true, false }) {
        for (const bool install_observer : { true, false }) {
            if (force_root == install_observer) {
                continue;
            }
            const auto suffix = force_root ? "force-control" : "observer-control";
            const auto interpreter = run_exclusion_control(
                SimulationEngine::interpreter, force_root,
                install_observer, std::string { suffix } + "-interpreter");
            const auto compiled = run_exclusion_control(
                SimulationEngine::compiled, force_root,
                install_observer, std::string { suffix } + "-compiled");
            require(compiled.semantics == interpreter.semantics,
                "force and installed-observer fallback match interpreter semantics");
            require(compiled.suppressions_before
                        == compiled.suppressions_after
                    && !compiled.private_row,
                "force and installed-observer controls do not publish a private blocking row");
            require(compiled.root_resume_delta == 1U
                    && compiled.middle_resume_delta
                        == (force_root ? 0U : 1U),
                "the excluded blocking route resumes its affected processes through checked callbacks");
            require(compiled.forced == force_root,
                "the force control retains the requested external force state");
            if (force_root) {
                require(compiled.semantics[1U].current == "1",
                    "the force exclusion control keeps the candidate output forced through its stimulus");
            }
            if (install_observer) {
                require(compiled.hook_calls != 0U,
                    "the observer exclusion uses a live public change hook");
            }
        }
    }
}

} // namespace
} // namespace fsim::tests::app::a2_blocking_failure

int main()
{
    using fsim::tests::app::a2_blocking::ScopedEnvironment;
    using fsim::tests::app::a2_blocking_failure::test_force_and_observation_exclusions;
    using fsim::tests::app::a2_blocking_failure::test_precommit_failure;
    try {
        ScopedEnvironment region_kernel { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave { "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
        ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
        ScopedEnvironment native_counts { "FSIM_PROFILE_NATIVE_PROCESS_COUNTS", "1" };
        ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };
        ScopedEnvironment wide_single {
            "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
        ScopedEnvironment wide_disjoint {
            "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", nullptr };
        test_precommit_failure();
        test_force_and_observation_exclusions();
    } catch (const std::exception& error) {
        std::cerr << "A2 blocking allocation witness failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
