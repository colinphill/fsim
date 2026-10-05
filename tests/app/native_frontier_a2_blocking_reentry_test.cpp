// SPDX-License-Identifier: Apache-2.0

#include "native_frontier_a2_blocking_test_support.hpp"
#include "native_frontier_generic_update_failure_interposer.hpp"

#include "fsim/runtime/packed_value.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::app::a2_blocking_reentry {
namespace {

using fsim::app::Simulation;
using fsim::app::SimulationEngine;
using fsim::project::Optimization;
using fsim::runtime::Logic4;
using fsim::runtime::PackedLogic4;
using fsim::runtime::RunResult;
using fsim::runtime::RunStatus;
using fsim::runtime::SchedulerBatchResult;
using fsim::runtime::SchedulerBatchTask;
using fsim::runtime::SchedulerPhase;
using fsim::runtime::SchedulerSystemVerilogKeyReceipt;
using fsim::runtime::simir::NativeRegionAllocationTestAccess;
using fsim::runtime::simir::ProcessId;
using fsim::runtime::simir::SignalId;
using fsim::runtime::Scheduler;
using fsim::runtime::SchedulerTraceKind;
using fsim::runtime::SchedulerTraceRecord;
using fsim::tests::app::a2_blocking::FixtureKind;
using fsim::tests::app::a2_blocking::ScopedEnvironment;
using fsim::tests::app::a2_blocking::Signals;
using fsim::tests::app::a2_blocking::TemporaryDirectory;
using fsim::tests::app::a2_blocking::find_signals;
using fsim::tests::app::a2_blocking::make_config;
using fsim::tests::app::a2_blocking::require;
using fsim::tests::app::a2_blocking::snapshot_all;
using fsim::tests::app::frontier::allocation_failure::arm_when;
using fsim::tests::app::frontier::allocation_failure::clear;
using fsim::tests::app::frontier::allocation_failure::clear_observer;
using fsim::tests::app::frontier::allocation_failure::injected;
using fsim::tests::app::frontier::allocation_failure::reset;
using fsim::tests::app::frontier::allocation_failure::set_observer;

using NativeAccess = NativeRegionAllocationTestAccess;
using Cut = NativeAccess::Cut;
using Key = NativeAccess::Key;
using Receipt = NativeAccess::Receipt;
using SignalSnapshot = NativeAccess::SignalSnapshot;
using SemanticSnapshot = NativeAccess::SemanticSnapshot;
using BlockingFailureSample = NativeAccess::BlockingFailureSample;
using BlockingFailurePredicateContext
    = NativeAccess::BlockingFailurePredicateContext;

void write_reentry_stimulus(const fsim::project::Config& config)
{
    require(!config.source_sets.empty()
            && !config.source_sets.front().files.empty(),
        "the reentry fixture must expose its generated source path");
    std::ofstream output { config.source_sets.front().files.front(),
        std::ios::binary | std::ios::trunc };
    output << "module native_frontier_a2_blocking_positive(output logic sink);\n"
           << "  logic source;\n"
           << "  logic root;\n"
           << "  logic middle;\n"
           << "  always_comb begin root = ~source; end\n"
           << "  always_comb begin middle = root; end\n"
           << "  always_comb begin sink = ~middle; end\n"
           << "  initial begin source = 1'b0; #1 source = 1'b1;"
              " #1 source = 1'b0; #1 $finish; end\n"
           << "endmodule\n";
    require(static_cast<bool>(output),
        "could not write the two-stimulus blocking reentry fixture");
}

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

struct TraceProbe final {
    ProcessId root { };
    ProcessId middle { };
    Receipt pending_sink;
    std::size_t sink_begins { };
    std::size_t sink_ends { };
    std::size_t producer_replays { };

    [[nodiscard]] static bool matches(const SchedulerTraceRecord& record,
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
            && record.systemverilog && record.phase
            && record.time == probe.pending_sink.key.time
            && (record.order == probe.root || record.order == probe.middle)) {
            ++probe.producer_replays;
        }
        if (record.kind == SchedulerTraceKind::task_begin
            && matches(record, probe.pending_sink.key)) {
            ++probe.sink_begins;
        }
        if (record.kind == SchedulerTraceKind::task_end
            && matches(record, probe.pending_sink.key)) {
            ++probe.sink_ends;
        }
    }
};

struct ForeignActiveCutTask final : SchedulerBatchTask {
    static constexpr std::uint64_t payload = 0xa2f00dU;

    Simulation* simulation { };
    Signals signals;
    ProcessId root_owner { };
    ProcessId middle_owner { };
    const SignalSnapshot* root_before { };
    const SignalSnapshot* middle_before { };
    std::uint64_t eval_before { };
    std::uint64_t suppressions_before { };
    std::optional<Receipt>* root_receipt { };
    std::optional<Receipt>* middle_receipt { };
    Cut* cut { };
    SchedulerSystemVerilogKeyReceipt issued_receipt;
    Key middle_key_at_enqueue;
    bool scheduled { };
    bool executed { };
    bool exact_frontier { };
    bool cut_captured { };
    bool fallback_called { };

    SchedulerBatchResult execute(Scheduler& scheduler,
        const std::span<const std::uint64_t> payloads) override
    {
        executed = true;
        const auto frontier = scheduler.current_batch_frontier();
        if (payloads.size() != 1U || payloads.front() != payload
            || !frontier || frontier->generation == 0U
            || frontier->tasks.size() != 1U || frontier->cursor != 0U
            || frontier->end != 1U || !issued_receipt.valid) {
            return { payloads.size(), { } };
        }
        const auto& task = frontier->tasks.front();
        exact_frontier = frontier->phase == issued_receipt.phase
            && frontier->time == issued_receipt.time
            && frontier->delta == issued_receipt.delta
            && frontier->systemverilog_round
                == issued_receipt.systemverilog_round
            && task.stable_order == issued_receipt.stable_order
            && task.sequence == issued_receipt.sequence
            && task.payload == payload
            && issued_receipt.time == middle_key_at_enqueue.time
            && issued_receipt.delta == middle_key_at_enqueue.delta
            && issued_receipt.systemverilog_round
                == middle_key_at_enqueue.round
            && issued_receipt.phase == SchedulerPhase::active
            && issued_receipt.stable_order
                == middle_key_at_enqueue.order
            && issued_receipt.sequence > middle_key_at_enqueue.sequence
            && root_receipt != nullptr && root_receipt->has_value()
            && (*root_receipt)->process == root_owner
            && middle_receipt != nullptr && middle_receipt->has_value()
            && (*middle_receipt)->process == middle_owner
            && issued_receipt.stable_order == middle_owner;
        if (exact_frontier && simulation != nullptr && root_before != nullptr
            && middle_before != nullptr && root_receipt != nullptr
            && middle_receipt != nullptr && root_receipt->has_value()
            && middle_receipt->has_value() && cut != nullptr) {
            cut_captured = NativeAccess::try_capture_positive_cut(
                *simulation, scheduler, frontier->phase, signals.root,
                *signals.middle, signals.sink, *root_before, *middle_before,
                **root_receipt, **middle_receipt, eval_before,
                suppressions_before, *cut, 2U);
        }
        if (cut_captured) {
            scheduler.request_stop();
        }
        return { payloads.size(), { } };
    }
};

struct ReentryResult final {
    bool fault_injected { };
    bool fault_cut_exact { };
    bool reentry_cut_exact { };
    std::vector<SemanticSnapshot> semantics;
};

[[nodiscard]] std::vector<SemanticSnapshot> run_interpreter_reference(
    const Optimization optimization, const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    auto config = make_config(temporary.path, optimization,
        FixtureKind::positive);
    write_reentry_stimulus(config);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the parsed blocking reentry reference must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    const auto signals = find_signals(simulation, FixtureKind::positive);
    simulation.start();
    const auto completed = simulation.run();
    require(completed.status == RunStatus::stopped && completed.time == 3U,
        "the interpreter reentry reference must finish at time three");
    return snapshot_all(simulation, signals);
}

[[nodiscard]] ReentryResult run_fault_then_native_reentry(
    const Optimization optimization, const std::string_view suffix)
{
    TemporaryDirectory temporary { suffix };
    fsim::diagnostic::Engine diagnostics;
    auto config = make_config(temporary.path, optimization,
        FixtureKind::positive);
    write_reentry_stimulus(config);
    auto project = fsim::app::build_project(config, diagnostics);
    require(project.has_value(),
        "the parsed blocking reentry fixture must elaborate");
    Simulation simulation(std::move(*project), config.run.max_deltas,
        SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    simulation.await_all_native_compilation();
    require(simulation.compiled_process_count() != 0U,
        "the reentry fixture must install compiled executors");

    const auto signals = find_signals(simulation, FixtureKind::positive);
    simulation.start();
    const auto warm = simulation.run(0U);
    require(warm.status == RunStatus::time_limit,
        "the reentry fixture must warm before the first changed input");
    simulation.await_all_native_compilation();

    const auto first_component
        = NativeAccess::component_for_signal(simulation, signals.root);
    require(signals.middle.has_value()
            && NativeAccess::component_for_signal(simulation, *signals.middle)
                == first_component
            && NativeAccess::component_for_signal(simulation, signals.sink)
                == first_component,
        "the blocking reentry chain must share its certified component");
    const auto root_owner = NativeAccess::output_owner(simulation,
        first_component, signals.root,
        fsim::runtime::simir::RegionOutputPublicationKind::blocking_immediate);
    const auto middle_owner = NativeAccess::output_owner(simulation,
        first_component, *signals.middle,
        fsim::runtime::simir::RegionOutputPublicationKind::blocking_immediate);
    const auto root_before_first = NativeAccess::snapshot(
        simulation, first_component, signals.root, root_owner);
    const auto middle_before_first = NativeAccess::snapshot(
        simulation, first_component, *signals.middle, middle_owner);
    require(!root_before_first.raw_record_present
            && !middle_before_first.raw_record_present
            && !root_before_first.materialization_pending
            && !middle_before_first.materialization_pending,
        "the first native attempt starts from aliased, materialized owners");

    Cut fault_cut;
    fault_cut.component = first_component;
    fault_cut.root = root_owner;
    fault_cut.middle = middle_owner;
    fault_cut.eval_before = NativeAccess::forwarding_evaluations(simulation);
    fault_cut.rows[0U].signal = signals.root;
    fault_cut.rows[0U].owner = root_owner;
    fault_cut.rows[0U].before = root_before_first;
    fault_cut.rows[0U].predicted = "0";
    BlockingFailurePredicateContext predicate { &simulation,
        signals.root, *signals.middle, root_owner, middle_owner,
        first_component, &root_before_first, &middle_before_first,
        fault_cut.eval_before, &fault_cut, false };
    BlockingFailureSample failure_sample;
    FailureObserverContext observer { &simulation, &fault_cut,
        signals.root, *signals.middle, &middle_before_first,
        &failure_sample };
    reset();
    set_observer(&observe_failure, &observer);
    arm_when(&NativeAccess::select_precommit_blocking_allocation,
        &predicate);
    bool escaped_bad_alloc { };
    RunResult first_time;
    {
        FailureObserverCleanup cleanup;
        try {
            first_time = simulation.run(1U);
        } catch (const std::bad_alloc&) {
            escaped_bad_alloc = true;
        }
    }
    ReentryResult result;
    result.fault_injected = injected();
    clear_observer();
    clear();
    result.fault_cut_exact = result.fault_injected
        && !escaped_bad_alloc
        && first_time.status == RunStatus::time_limit
        && first_time.time == 1U
        && predicate.matched && fault_cut.captured
        && fault_cut.row_count == 1U
        && fault_cut.rows[0U].signal == signals.root
        && fault_cut.middle_receipt.process == middle_owner
        && fault_cut.root_receipt.key.time == 1U
        && fault_cut.middle_receipt.key.time == 1U
        && fault_cut.root_receipt.key.order == root_owner
        && fault_cut.middle_receipt.key.order == middle_owner
        && NativeAccess::precedes(fault_cut.root_receipt.key,
            fault_cut.middle_receipt.key)
        && failure_sample.observed
        && failure_sample.exact_middle_frontier
        && failure_sample.middle_receipt_unchanged
        && failure_sample.root_only_private_row
        && failure_sample.middle_unpublished
        && failure_sample.middle_roles_unchanged
        && failure_sample.middle_direct_planes_unchanged
        && failure_sample.blocking_output_staged
        && failure_sample.journal_row_count == 1U
        && failure_sample.forwarding_attempts
            == fault_cut.forwarding_attempts_at_cut;
    require(result.fault_cut_exact,
        "the first input must hit the authentic precommit private-prefix failure cut");
    const auto root_after_fault = NativeAccess::snapshot(
        simulation, first_component, signals.root, root_owner);
    const auto middle_after_fault = NativeAccess::snapshot(
        simulation, first_component, *signals.middle, middle_owner);
    require(NativeAccess::journal_empty(simulation, fault_cut)
            && root_after_fault.current == fault_cut.rows[0U].predicted
            && root_after_fault.stored == fault_cut.rows[0U].predicted
            && root_after_fault.owner == fault_cut.rows[0U].predicted
            && root_after_fault.raw == fault_cut.rows[0U].predicted
            && !root_after_fault.raw_record_present
            && root_after_fault.previous == root_before_first.current
            && std::ranges::equal(root_after_fault.direct_aval,
                root_after_fault.current_value.aval_words())
            && std::ranges::equal(root_after_fault.direct_bval,
                root_after_fault.current_value.bval_words())
            && root_after_fault.direct_last_aval
                == root_before_first.direct_aval.front()
            && root_after_fault.direct_last_bval
                == root_before_first.direct_bval.front()
            && root_after_fault.event == fault_cut.rows[0U].hidden.event
            && root_after_fault.transaction
                == fault_cut.rows[0U].hidden.transaction
            && root_after_fault.revision == fault_cut.rows[0U].hidden.revision
            && middle_after_fault.current == "0"
            && middle_after_fault.stored == "0"
            && middle_after_fault.owner == "0"
            && middle_after_fault.raw == "0"
            && !middle_after_fault.raw_record_present
            && middle_after_fault.previous == middle_before_first.current
            && middle_after_fault.event != middle_before_first.event
            && middle_after_fault.transaction
                != middle_before_first.transaction
            && middle_after_fault.revision > middle_before_first.revision
            && middle_after_fault.origin_domain
                == static_cast<std::uint32_t>(
                    fsim::runtime::simir::ProcessSchedulingDomain::systemverilog)
            && middle_after_fault.origin_phase
                == static_cast<std::uint32_t>(SchedulerPhase::active)
            && !NativeAccess::readiness_receipt(
                simulation, first_component, root_owner)
            && !NativeAccess::readiness_receipt(
                simulation, first_component, middle_owner)
            && NativeAccess::native_resume_count(simulation, root_owner)
                == fault_cut.root_resume_count_at_cut
            && NativeAccess::native_resume_count(simulation, middle_owner)
                == fault_cut.middle_resume_count_at_cut + 1U
            && NativeAccess::forwarding_declines(simulation)
                >= failure_sample.blocking_declines + 1U,
        "the failed native attempt flushes root once and checks the original middle callback once");

    // The time-one checked continuation is complete. Snapshot this committed
    // state before the independent time-two input so the second native run
    // must prove fresh work, rather than replaying the first private row.
    const auto component
        = NativeAccess::component_for_signal(simulation, signals.root);
    require(component == first_component
            && NativeAccess::component_for_signal(simulation, *signals.middle)
                == component
            && NativeAccess::component_for_signal(simulation, signals.sink)
                == component,
        "the checked continuation must leave the same parsed component available for reentry");
    const auto root_before_reentry = NativeAccess::snapshot(
        simulation, component, signals.root, root_owner);
    const auto middle_before_reentry = NativeAccess::snapshot(
        simulation, component, *signals.middle, middle_owner);
    require(root_before_reentry.current == "0"
            && middle_before_reentry.current == "0"
            && root_before_reentry.raw == root_before_reentry.current
            && middle_before_reentry.raw == middle_before_reentry.current
            && root_before_reentry.event && middle_before_reentry.event,
        "the first checked continuation must publish zero before the second stimulus");
    const auto eval_before_reentry
        = NativeAccess::forwarding_evaluations(simulation);
    const auto suppressions_before_reentry
        = NativeAccess::private_fanout_suppressions(simulation);
    const auto root_resumes_before_reentry
        = NativeAccess::native_resume_count(simulation, root_owner);
    const auto middle_resumes_before_reentry
        = NativeAccess::native_resume_count(simulation, middle_owner);

    Cut reentry_cut;
    std::optional<Receipt> root_receipt;
    std::optional<Receipt> middle_receipt;
    ForeignActiveCutTask foreign_cut_task;
    foreign_cut_task.simulation = &simulation;
    foreign_cut_task.signals = signals;
    foreign_cut_task.root_owner = root_owner;
    foreign_cut_task.middle_owner = middle_owner;
    foreign_cut_task.root_before = &root_before_reentry;
    foreign_cut_task.middle_before = &middle_before_reentry;
    foreign_cut_task.eval_before = eval_before_reentry;
    foreign_cut_task.suppressions_before = suppressions_before_reentry;
    foreign_cut_task.root_receipt = &root_receipt;
    foreign_cut_task.middle_receipt = &middle_receipt;
    foreign_cut_task.cut = &reentry_cut;
    const auto safe_point = simulation.add_safe_point_hook(
        [&simulation, signals, component, root_owner, middle_owner,
            root_before_reentry, middle_before_reentry, eval_before_reentry,
            suppressions_before_reentry, &reentry_cut, &root_receipt,
            &middle_receipt, &foreign_cut_task](Scheduler& scheduler,
                const SchedulerPhase phase) {
            if (reentry_cut.captured || scheduler.now() != 2U
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
            if (foreign_cut_task.scheduled) {
                return;
            }
            Cut root_only_cut;
            if (!NativeAccess::try_capture_root_private_prefix(simulation,
                    scheduler, phase, signals.root, *signals.middle,
                    signals.sink, root_before_reentry, middle_before_reentry,
                    *root_receipt, eval_before_reentry,
                    suppressions_before_reentry, root_only_cut, 2U)
                || root_only_cut.root_receipt != *root_receipt
                || root_only_cut.middle_receipt != *middle_receipt) {
                return;
            }
            foreign_cut_task.middle_key_at_enqueue = middle_receipt->key;
            // Share the middle's stable order but let the scheduler issue a
            // later sequence. The callback then separates middle publication
            // from the higher-order sink without installing a trace observer.
            scheduler.schedule_systemverilog_group_batchable(
                SchedulerPhase::active, middle_receipt->key.order,
                foreign_cut_task, ForeignActiveCutTask::payload,
                [&foreign_cut_task](Scheduler&) {
                    foreign_cut_task.fallback_called = true;
                }, { }, &foreign_cut_task.issued_receipt);
            foreign_cut_task.scheduled = true;
        });
    const auto second_time = simulation.run(2U);
    simulation.remove_safe_point_hook(safe_point);
    const Key foreign_key {
        foreign_cut_task.issued_receipt.time,
        foreign_cut_task.issued_receipt.delta,
        foreign_cut_task.issued_receipt.systemverilog_round,
        foreign_cut_task.issued_receipt.stable_order,
        foreign_cut_task.issued_receipt.sequence,
        reentry_cut.middle_receipt.key.domain,
        static_cast<std::uint32_t>(foreign_cut_task.issued_receipt.phase),
    };
    result.reentry_cut_exact = second_time.status == RunStatus::stopped
        && second_time.time == 2U
        && reentry_cut.captured && reentry_cut.row_count == 2U
        && reentry_cut.root_receipt.process == root_owner
        && reentry_cut.middle_receipt.process == middle_owner
        && reentry_cut.root_receipt.key.time == 2U
        && reentry_cut.middle_receipt.key.time == 2U
        && reentry_cut.sink_receipt.key.time == 2U
        && reentry_cut.root_receipt.generation == reentry_cut.generation
        && reentry_cut.middle_receipt.generation == reentry_cut.generation
        && reentry_cut.sink_receipt.generation == reentry_cut.generation
        && NativeAccess::precedes(reentry_cut.root_receipt.key,
            reentry_cut.middle_receipt.key)
        && NativeAccess::precedes(reentry_cut.middle_receipt.key,
            reentry_cut.sink_receipt.key)
        && reentry_cut.rows[0U].signal == signals.root
        && reentry_cut.rows[1U].signal == *signals.middle
        && reentry_cut.rows[0U].callback_key
            == reentry_cut.root_receipt.key
        && reentry_cut.rows[1U].callback_key
            == reentry_cut.middle_receipt.key
        && reentry_cut.rows[0U].callback_key.order == root_owner
        && reentry_cut.rows[1U].callback_key.order == middle_owner
        // Journal callback_order is an ordinal, while callback_key carries
        // the scheduler's stable process order and insertion sequence.
        && reentry_cut.rows[0U].callback_order == 1U
        && reentry_cut.rows[1U].callback_order == 2U
        && reentry_cut.rows[0U].predicted == "1"
        && reentry_cut.rows[1U].predicted == "1"
        && reentry_cut.eval_after > eval_before_reentry
        && reentry_cut.suppressions_after
            >= suppressions_before_reentry + 2U
        && reentry_cut.root_resume_count_at_cut
            == root_resumes_before_reentry
        && reentry_cut.middle_resume_count_at_cut
            == middle_resumes_before_reentry
        && foreign_cut_task.scheduled && foreign_cut_task.executed
        && foreign_cut_task.exact_frontier
        && foreign_cut_task.cut_captured
        && !foreign_cut_task.fallback_called
        && foreign_cut_task.issued_receipt.valid
        && foreign_key.domain
            == reentry_cut.middle_receipt.key.domain
        && foreign_key.phase == reentry_cut.middle_receipt.key.phase
        && NativeAccess::precedes(reentry_cut.middle_receipt.key,
            foreign_key)
        && NativeAccess::precedes(foreign_key,
            reentry_cut.sink_receipt.key)
        && foreign_cut_task.issued_receipt.time == 2U
        && foreign_cut_task.issued_receipt.delta
            == reentry_cut.middle_receipt.key.delta
        && foreign_cut_task.issued_receipt.systemverilog_round
            == reentry_cut.middle_receipt.key.round
        && foreign_cut_task.issued_receipt.stable_order
            == reentry_cut.middle_receipt.key.order
        && foreign_cut_task.issued_receipt.sequence
            > reentry_cut.middle_receipt.key.sequence;
    require(result.reentry_cut_exact,
        "the later input must produce a fresh two-row native prefix under new original keys");
    require(reentry_cut.root_receipt.key != fault_cut.root_receipt.key
            && reentry_cut.middle_receipt.key != fault_cut.middle_receipt.key,
        "the later native prefix must use different scheduler keys from the failed input");

    const auto observed_root
        = simulation.read_signal(signals.root).to_msb_string();
    require(observed_root == "1"
            && NativeAccess::journal_empty(simulation, reentry_cut)
            && NativeAccess::receipt_unchanged(simulation,
                reentry_cut.sink_receipt),
        "public observation flushes the reentered rows and retains the sink key");
    for (const auto& row : reentry_cut.rows) {
        const auto after = NativeAccess::snapshot(simulation,
            reentry_cut.component, row.signal, row.owner);
        require(after.current == row.predicted
                && after.stored == row.predicted
                && after.owner == row.predicted
                && after.raw == row.predicted
                && !after.raw_record_present
                && after.previous == row.before.current
                && std::ranges::equal(after.direct_aval,
                    after.current_value.aval_words())
                && std::ranges::equal(after.direct_bval,
                    after.current_value.bval_words())
                && after.direct_last_aval == row.before.direct_aval.front()
                && after.direct_last_bval == row.before.direct_bval.front()
                && after.event == row.hidden.event
                && after.transaction == row.hidden.transaction
                && after.origin_domain == row.hidden.origin_domain
                && after.origin_phase == row.hidden.origin_phase
                && after.event_round == row.hidden.event_round
                && after.revision == row.hidden.revision
                && !after.materialization_pending,
            "the reentry role flush changes values without republishing metadata");
    }

    TraceProbe trace { root_owner, middle_owner, reentry_cut.sink_receipt };
    NativeAccess::set_trace_hook(simulation, &trace, &TraceProbe::receive);
    simulation.clear_stop();
    const auto completed = simulation.run();
    NativeAccess::set_trace_hook(simulation, nullptr, nullptr);
    require(completed.status == RunStatus::stopped && completed.time == 3U
            && trace.sink_begins == 1U && trace.sink_ends == 1U
            && trace.producer_replays == 0U
            && NativeAccess::native_resume_count(simulation, root_owner)
                == reentry_cut.root_resume_count_at_cut
            && NativeAccess::native_resume_count(simulation, middle_owner)
                == reentry_cut.middle_resume_count_at_cut,
        "the retained sink activation runs once without replaying the native producers");
    result.semantics = snapshot_all(simulation, signals);
    return result;
}

void test_native_reentry_after_precommit_failure()
{
    for (const auto optimization : { Optimization::o0, Optimization::o2 }) {
        const auto suffix = optimization == Optimization::o0
            ? std::string_view { "blocking-reentry-o0" }
            : std::string_view { "blocking-reentry-o2" };
        const auto reference = run_interpreter_reference(optimization,
            std::string { suffix } + "-interpreter");
        const auto result = run_fault_then_native_reentry(optimization,
            std::string { suffix } + "-compiled");
        require(result.fault_injected && result.fault_cut_exact
                && result.reentry_cut_exact,
            "the one-shot precommit failure must be followed by a successful native reentry");
        require(result.semantics == reference,
            "native reentry after the checked failure continuation must match interpreter values and event metadata");
    }
}

} // namespace
} // namespace fsim::tests::app::a2_blocking_reentry

int main()
{
    using fsim::tests::app::a2_blocking::ScopedEnvironment;
    using fsim::tests::app::a2_blocking_reentry::
        test_native_reentry_after_precommit_failure;
    try {
        ScopedEnvironment region_kernel { "FSIM_ENABLE_SV_REGION_KERNEL", "1" };
        ScopedEnvironment local_wave { "FSIM_ENABLE_SV_LOCAL_WAVE", "1" };
        ScopedEnvironment wave_profile { "FSIM_PROFILE_SV_WAVES", "1" };
        ScopedEnvironment native_counts {
            "FSIM_PROFILE_NATIVE_PROCESS_COUNTS", "1" };
        ScopedEnvironment jit_profile { "FSIM_PROFILE_JIT", nullptr };
        ScopedEnvironment wide_single {
            "FSIM_ENABLE_A4_WIDE_SINGLE_OWNER_COMMIT", nullptr };
        ScopedEnvironment wide_disjoint {
            "FSIM_ENABLE_A4_WIDE_DISJOINT_OWNER_COMMIT", nullptr };
        test_native_reentry_after_precommit_failure();
    } catch (const std::exception& error) {
        std::cerr << "A2 blocking native-reentry witness failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
