// SPDX-License-Identifier: Apache-2.0
#include "native_frontier_generic_update_test_support.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::app::frontier {
namespace {

using runtime::Logic4;
using runtime::PackedLogic4;
using runtime::RunStatus;
using runtime::simir::RegionFrontierStatusV2;
using runtime::simir::ValueKind;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

struct PartialPrefixRun final {
    GenericFixtureSnapshot before;
    GenericFixtureSnapshot cut;
    GenericFixtureSnapshot final;
    GenericFrontierEntryCapture entry;
    GenericFrontierProbe probe;
    runtime::simir::SignalId root_output { };
    runtime::simir::SignalId suffix_output { };
    runtime::simir::ProcessId root_process { };
    runtime::simir::ProcessId suffix_process { };
};

[[nodiscard]] PartialPrefixRun run_case(const GenericFixtureMode mode)
{
    GenericFixturePolicy policy;
    policy.region_kernel_enabled = true;
    GenericWholeWriteFixture fixture(mode, 1U, 1U, false, false, true,
        ValueKind::logic4, policy, true);
    const auto suffix_output = fixture.cohort_slice_suffix_signal();
    const auto suffix_process = fixture.cohort_slice_suffix_process_id();
    const auto suffix_input = fixture.checked_suffix_input_signal();
    const auto root_outputs = fixture.output_signals();
    require(suffix_output.has_value() && suffix_process.has_value()
            && suffix_input.has_value() && root_outputs.size() == 1U
            && *suffix_output != root_outputs.front()
            && *suffix_input != fixture.input_signal(),
        "the fixture builds one whole-write root and one LLVM slice suffix");

    fixture.start_and_settle();
    PartialPrefixRun result;
    result.before = fixture.snapshot();
    const auto ticket_stats_before
        = fixture.generic_batch_compaction_stats();
    result.root_output = root_outputs.front();
    result.suffix_output = *suffix_output;
    result.root_process = fixture.process_id();
    result.suffix_process = *suffix_process;

    const PackedLogic4 input { 1U, Logic4::one };
    // Deposits between scheduler runs use the ordinary Generic batchable
    // queue path. The component-only compact ticket path is callback-scoped
    // and intentionally offers just the component member, so it cannot prove
    // a two-task borrowed frontier with the unrelated LLVM slice suffix.
    fixture.deposit_signal(fixture.input_signal(), input);
    fixture.deposit_signal(*suffix_input, input);
    const auto stopped
        = fixture.run_to_pre_update_cut_after_external_deposits();
    require(stopped.status == RunStatus::stopped,
        "the maximum-order Active sentinel stops the ordinary two-task batch before Update publication");
    result.cut = fixture.snapshot();
    result.entry = fixture.frontier_entry_capture();
    result.probe = fixture.probe_frontier_state();

    const std::array<runtime::simir::SignalId, 2U> outputs {
        result.root_output, result.suffix_output
    };
    for (const auto signal : outputs) {
        require(result.cut.signals.at(signal)
                == result.before.signals.at(signal),
            "both components keep current/LAST/stored, raw owner, and metadata unchanged until Update");
    }

    require(result.cut.pending_update_sizes[0U] == 2U
            && result.cut.pending_update_sizes[1U] == 2U
            && result.cut.pending_update_signals.size() == 2U
            && result.cut.pending_update_signals[0U] == result.root_output
            && result.cut.pending_update_signals[1U] == result.suffix_output,
        "the whole-write root prefix stages before the distinct partial-slice suffix");
    require(result.cut.pending_update_values.size() == 2U
            && result.cut.pending_update_values[0U]
                == PackedLogic4 { 1U, Logic4::zero }
            && result.cut.pending_update_values[1U] == input,
        "the two deferred values preserve task and source order");
    require(!result.before.signals.at(result.root_output).raw_driver
            && !result.before.signals.at(result.root_output).raw_driver_process
            && !result.before.signals.at(result.suffix_output).raw_driver
            && !result.before.signals.at(result.suffix_output).raw_driver_process,
        "ResolutionKind::none output registration creates no raw DriverRecord for either whole or partial writer");

    if (mode != GenericFixtureMode::interpreter) {
        require(result.entry.called && result.entry.frontier_present
                && !result.entry.tasks_truncated
                && result.entry.borrowed_task_count == 2U,
            "the genuine provider entry observes both adjacent scheduler tasks in its borrowed Generic frontier");
        const auto ticket_stats_after
            = fixture.generic_batch_compaction_stats();
        require(ticket_stats_after.generic_readiness_ticket_queue_insertions
                    == ticket_stats_before.generic_readiness_ticket_queue_insertions
                && ticket_stats_after.generic_readiness_ticket_members
                    == ticket_stats_before.generic_readiness_ticket_members
                && ticket_stats_after.generic_readiness_ticket_members_elided
                    == ticket_stats_before.generic_readiness_ticket_members_elided,
            "between-run deposits reach the two-task entry through ordinary Generic batchable admission");
        require(result.entry.borrowed_tasks[0U].process_id
                    == result.root_process
                && result.entry.borrowed_tasks[1U].process_id
                    == result.suffix_process
                && result.entry.borrowed_tasks[0U].key.payload
                    == ((UINT64_C(1) << 60U)
                        | static_cast<std::uint64_t>(result.root_process))
                && result.entry.borrowed_tasks[1U].key.payload
                    == ((UINT64_C(1) << 60U)
                        | static_cast<std::uint64_t>(result.suffix_process))
                && (result.entry.borrowed_tasks[0U].key.stable_order
                        < result.entry.borrowed_tasks[1U].key.stable_order
                    || (result.entry.borrowed_tasks[0U].key.stable_order
                            == result.entry.borrowed_tasks[1U].key.stable_order
                        && result.entry.borrowed_tasks[0U].key.sequence
                            < result.entry.borrowed_tasks[1U].key.sequence))
                && result.entry.borrowed_tasks[0U].key
                    != result.entry.borrowed_tasks[1U].key,
            "the borrowed frontier retains distinct root-before-suffix lexicographic scheduler keys");
        require(result.entry.frame_task_count == 1U
                && result.entry.frame_task_cursor == 1U
                && result.entry.status
                    == RegionFrontierStatusV2::generic_update_batch_ready,
            "the V2 entry consumes exactly the root component prefix and leaves the suffix for the checked legacy activation");
        require(result.probe.runtime_found && !result.probe.frontier_present
                && result.entry.generation != 0U
                && result.probe.runtime_generation == result.entry.generation
                && result.probe.runtime_time == result.entry.time
                && result.probe.runtime_delta == result.entry.delta
                && result.entry.phase == runtime::SchedulerPhase::active
                && result.probe.runtime_systemverilog_round == 0U
                && result.probe.runtime_process_domain
                    == runtime::simir::ProcessSchedulingDomain::generic
                && result.probe.runtime_phase == runtime::SchedulerPhase::active,
            "the retained V2 frame preserves its exact Generic Active round-zero slot");
        require(result.probe.runtime_task_count == 1U
                && result.probe.runtime_task_cursor == 1U
                && result.probe.runtime_original_task_count == 1U
                && !result.probe.runtime_original_tasks_truncated
                && result.probe.runtime_original_tasks[0U]
                    == result.entry.borrowed_tasks[0U].key,
            "the retained consumed-prefix receipt matches the exact offered root task key");
        require(result.cut.native_member_dispatches
                    == result.before.native_member_dispatches + 1U
                && result.cut.generic_projected_region_backend_runs
                    == result.before.generic_projected_region_backend_runs + 1U
                && result.cut.generic_projected_region_completions
                    == result.before.generic_projected_region_completions + 1U
                && result.cut.generic_projected_region_members
                    == result.before.generic_projected_region_members + 1U,
            "one native root dispatch is followed by exactly one legacy LLVM region activation for the slice suffix");
        require(result.cut.frontier.found
                && result.cut.frontier.generic_update_ack_count == 1U,
            "the root's deferred whole-write row receives one successful host ACK while the slice remains an ordinary legacy publication");
    } else {
        require(!result.entry.called,
            "the interpreter reference does not install the observing native provider");
    }

    const auto resumed = fixture.resume_update_publication();
    require(resumed.status == RunStatus::completed
            || resumed.status == RunStatus::time_limit,
        "the ordinary Update phase completes both publications");
    result.final = fixture.snapshot();

    PackedLogic4 expected_slice { 2U, Logic4::z };
    expected_slice.set(1U, Logic4::one);
    require(result.final.signals.at(result.root_output).current
                == PackedLogic4 { 1U, Logic4::zero }
            && result.final.signals.at(result.root_output).raw_driver_process
                == result.before.signals.at(result.root_output).raw_driver_process
            && result.final.signals.at(result.root_output).raw_driver
                == result.before.signals.at(result.root_output).raw_driver
            && result.final.signals.at(result.suffix_output).current
                == expected_slice
            && result.final.signals.at(result.suffix_output).raw_driver_process
                == result.before.signals.at(result.suffix_output).raw_driver_process
            && result.final.signals.at(result.suffix_output).raw_driver
                == result.before.signals.at(result.suffix_output).raw_driver,
        "Update publishes both values while preserving the absent unresolved-output raw-driver records");
    return result;
}

} // namespace

void run_native_frontier_generic_partial_prefix_tests()
{
#if defined(FSIM_HAS_LLVM)
    const auto reference = run_case(GenericFixtureMode::interpreter);
    const auto o0 = run_case(GenericFixtureMode::llvm_o0);
    const auto o2 = run_case(GenericFixtureMode::llvm_o2);
    require(o0.final.signals == reference.final.signals
            && o2.final.signals == reference.final.signals,
        "O0/O2 partial-prefix publication, owner, and metadata match checked interpretation");
#else
    throw std::runtime_error {
        "the Generic partial-prefix witness requires LLVM"
    };
#endif
}

} // namespace fsim::tests::app::frontier

int main()
{
    try {
        fsim::tests::app::frontier::
            run_native_frontier_generic_partial_prefix_tests();
    } catch (const std::exception& error) {
        std::cerr << "generic native-frontier partial-prefix test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
