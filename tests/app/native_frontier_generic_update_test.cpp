// SPDX-License-Identifier: Apache-2.0
#include "native_frontier_generic_update_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::tests::app::frontier {
namespace {

using runtime::Logic4;
using runtime::Logic9;
using runtime::PackedLogic4;
using runtime::simir::ResolutionKind;
using runtime::RunStatus;
using runtime::SchedulerBatchCompactionStats;
using runtime::simir::RegionFrontierExecutionModeV2;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

void require_two_member_generic_ticket(
    const SchedulerBatchCompactionStats& before,
    const SchedulerBatchCompactionStats& after)
{
    require(after.tickets >= before.tickets
            && after.tickets - before.tickets == 1U
            && after.members >= before.members
            && after.members - before.members == 2U
            && after.entries_elided >= before.entries_elided
            && after.entries_elided - before.entries_elided == 1U,
        "the two Generic members share one physical ticket and elide one queue entry");
    require(after.generic_readiness_ticket_queue_insertions
                >= before.generic_readiness_ticket_queue_insertions
            && after.generic_readiness_ticket_queue_insertions
                - before.generic_readiness_ticket_queue_insertions == 1U
            && after.generic_readiness_ticket_members
                >= before.generic_readiness_ticket_members
            && after.generic_readiness_ticket_members
                - before.generic_readiness_ticket_members == 2U
            && after.generic_readiness_ticket_members_elided
                >= before.generic_readiness_ticket_members_elided
            && after.generic_readiness_ticket_members_elided
                - before.generic_readiness_ticket_members_elided == 1U,
        "scheduler-owned Generic counters confirm one insertion for two logical members");
}

void require_captured_two_member_keys(
    const GenericFrontierEntryCapture& capture,
    const std::span<const GenericTaskKeySnapshot> original_keys)
{
    require(capture.called && capture.frontier_present
            && !capture.tasks_truncated
            && capture.frontier_cursor == 0U
            && capture.frontier_end == 2U
            && capture.borrowed_task_count == 2U
            && capture.frame_task_count == 2U
            && capture.frame_task_cursor == 2U
            && capture.status
                == runtime::simir::RegionFrontierStatusV2::generic_update_batch_ready,
        "the genuine Generic entry consumes both exact offered keys in one callback");
    require(original_keys.size() == 2U,
        "the runtime retains both original scheduler keys");
    for (std::size_t index = 0U; index < original_keys.size(); ++index) {
        const auto& offered = capture.borrowed_tasks[index];
        require(offered.process_id == index + 1U
                && offered.key == original_keys[index]
                && offered.key.stable_order == offered.process_id,
            "each compiled member keeps its scheduler-authored order, sequence, and payload");
    }
}

[[nodiscard]] PackedLogic4 input_pattern(const std::uint32_t width)
{
    constexpr std::array states {
        Logic4::zero, Logic4::one, Logic4::x, Logic4::z
    };
    PackedLogic4 value { width, Logic4::zero };
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        value.set(bit, states[(bit + 1U) % states.size()]);
    }
    return value;
}

[[nodiscard]] PackedLogic4 invert(const PackedLogic4& source)
{
    PackedLogic4 result { source.width(), Logic4::zero };
    for (std::size_t bit = 0U; bit < source.width(); ++bit) {
        result.set(bit, runtime::logic_not(source.get(bit)));
    }
    return result;
}

[[nodiscard]] PackedLogic4 logic9_pattern(
    const std::uint32_t width, const std::size_t state_offset = 0U)
{
    constexpr std::array states {
        Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::z,
        Logic9::w, Logic9::l, Logic9::h, Logic9::dont_care
    };
    auto value = PackedLogic4 { width, Logic4::zero }.promoted_to_logic9();
    for (std::uint32_t bit = 0U; bit < width; ++bit) {
        value.set_logic9(bit,
            states[(static_cast<std::size_t>(bit) + state_offset)
                % states.size()]);
    }
    return value;
}

void require_deferred_output_state_unchanged(
    const GenericFixtureSnapshot& before,
    const GenericFixtureSnapshot& cut,
    const std::span<const runtime::simir::SignalId> output_signals)
{
    require(cut.signals.size() == before.signals.size(),
        "the fixture snapshots every output signal");
    for (const auto signal : output_signals) {
        require(cut.signals.at(signal) == before.signals.at(signal),
            "generated generic execution keeps current/LAST/stored, raw owner, and metadata private until Update");
    }
}

void require_boundary_value(
    const GenericFixtureSnapshot& cut,
    const runtime::simir::SignalId signal,
    const PackedLogic4& value,
    const std::string_view description)
{
    const auto found = std::ranges::find(cut.frontier.boundary_planes,
        signal, &GenericBoundaryPlaneSnapshot::signal);
    require(found != cut.frontier.boundary_planes.end(),
        description);
    const auto expected_kind = value.is_logic9()
        ? runtime::simir::ValueKind::logic9
        : runtime::simir::ValueKind::logic4;
    const auto expected_plane_count = value.is_logic9() ? 4U : 2U;
    const auto expected_word_count = value.is_logic9()
        ? value.logic9_plane_words(0U).size()
        : value.aval_words().size();
    require(found->value_kind == expected_kind
            && found->width == value.width()
            && found->word_count == expected_word_count
            && found->plane_count == expected_plane_count,
        "generic boundary descriptor has the source value's typed plane shape");
    for (std::size_t plane = 0U; plane < expected_plane_count; ++plane) {
        const auto expected_words = value.is_logic9()
            ? value.logic9_plane_words(plane)
            : plane == 0U ? value.aval_words() : value.bval_words();
        require(std::ranges::equal(found->words[plane], expected_words),
            description);
    }
    for (std::size_t plane = expected_plane_count; plane < 4U; ++plane) {
        require(found->words[plane].empty(),
            "unused generic boundary planes remain absent");
    }
}

[[nodiscard]] const GenericLogic9SidecarSnapshot& logic9_sidecar_for(
    const GenericFixtureSnapshot& snapshot,
    const runtime::simir::SignalId signal)
{
    const auto found = std::ranges::find(snapshot.logic9_sidecars,
        signal, &GenericLogic9SidecarSnapshot::signal);
    require(found != snapshot.logic9_sidecars.end(),
        "every Logic9 signal has a passive sidecar snapshot");
    return *found;
}

void require_pending_suffix(
    const GenericFixtureSnapshot& before,
    const GenericFixtureSnapshot& cut,
    const std::span<const runtime::simir::SignalId> signals,
    const std::span<const PackedLogic4> values)
{
    require(signals.size() == values.size(),
        "every whole WriteUpdate has one expected value");
    const auto descriptor_offset = before.pending_update_signals.size();
    const auto value_offset = before.pending_update_values.size();
    require(cut.pending_update_signals.size()
                == descriptor_offset + signals.size()
            && cut.pending_update_values.size()
                == value_offset + values.size(),
        "the generated body appends only its whole-write descriptors and values");
    for (std::size_t index = 0U; index < signals.size(); ++index) {
        require(cut.pending_update_signals[descriptor_offset + index]
                    == signals[index]
                && cut.pending_update_values[value_offset + index]
                    == values[index],
            "typed whole-write descriptors and all value planes preserve member and source-instruction order");
    }
}

void require_checked_word_stage(
    const GenericFixtureSnapshot& before,
    const GenericFixtureSnapshot& cut,
    const std::span<const runtime::simir::SignalId> signals,
    const std::span<const PackedLogic4> values)
{
    require(signals.size() == values.size(),
        "every checked whole WriteUpdate has one expected word value");
    require(!before.update_commit_scheduled
            && before.unresolved_word_updates.empty()
            && before.direct_single_driver_word_updates.empty(),
        "the settled fixture has no deferred word writes before stimulation");
    require(cut.update_commit_scheduled,
        "the checked word path has queued its ordinary deferred Update callback");
    require(cut.pending_update_sizes == before.pending_update_sizes
            && cut.pending_update_signals == before.pending_update_signals
            && cut.pending_update_values == before.pending_update_values,
        "checked wide word staging does not fabricate packed pending descriptors");
    require(cut.unresolved_word_updates.size()
                + cut.direct_single_driver_word_updates.size()
                == signals.size(),
        "the ordinary Update callback owns exactly the checked whole-write signal set");

    for (std::size_t index = 0U; index < signals.size(); ++index) {
        const auto signal = signals[index];
        const auto unresolved = std::ranges::find(
            cut.unresolved_word_updates,
            signal,
            &GenericStagedWordUpdateSnapshot::signal);
        const auto direct = std::ranges::find(
            cut.direct_single_driver_word_updates,
            signal,
            &GenericStagedWordUpdateSnapshot::signal);
        const bool found_unresolved
            = unresolved != cut.unresolved_word_updates.end();
        const bool found_direct
            = direct != cut.direct_single_driver_word_updates.end();
        require(found_unresolved != found_direct,
            "each checked output is staged exactly once in its production unresolved or direct-owner route");
        const auto& staged = found_direct ? *direct : *unresolved;
        require(staged.value.has_value() && *staged.value == values[index],
            "the queued checked Update scratch contains the complete expected value before publication");
    }
}

void require_retained_frame_ack(
    const GenericFrontierProbe& probe,
    const std::size_t expected_members,
    const std::size_t expected_writes,
    const runtime::SimulationTick expected_time = 1U)
{
    require(probe.runtime_found && !probe.frontier_present,
        "the generated callback retains only copied frame evidence after its borrowed scheduler span ends");
    require(!probe.frontier_tasks_truncated
            && probe.runtime_original_task_count == expected_members
            && !probe.runtime_original_tasks_truncated,
        "the fixed-size probe captures the complete retained original task-key prefix");
    require(probe.runtime_task_count == expected_members
            && probe.runtime_task_cursor == expected_members,
        "the retained generic frame records the exact consumed member prefix");
    require(probe.pending_write_count == expected_writes
            && probe.staged_event_count == expected_writes
            && probe.generic_update_ack_count == expected_writes,
        "the host ACK covers every staged whole-write event after Update values are secured");
    require(probe.runtime_time == expected_time
            && probe.runtime_systemverilog_round == 0U
            && probe.runtime_process_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && probe.runtime_phase == runtime::SchedulerPhase::active,
        "the retained frame slot preserves the exact generic Active round-zero origin");
}

void require_native_interleaved_root_stage(
    const GenericFixtureSnapshot& staged,
    const GenericFrontierEntryCapture& entry,
    const GenericFrontierProbe& probe,
    const runtime::simir::SignalId expected_signal,
    const PackedLogic4& expected_value,
    const std::uint64_t dispatches_before)
{
    require(staged.frontier.found
            && staged.frontier.execution_mode
                == RegionFrontierExecutionModeV2::generic_deferred_update
            && staged.frontier.generic_update_ack_count == 1U
            && staged.frontier.mutable_plane_roles_absent
            && staged.native_member_dispatches == dispatches_before + 1U,
        "the interleaver observes one acknowledged native Generic root write");
    require(entry.called && entry.frontier_present
            && !entry.tasks_truncated
            && entry.frontier_cursor == 0U
            && entry.frontier_end == 1U
            && entry.borrowed_task_count == 1U
            && entry.frame_task_count == 1U
            && entry.frame_task_cursor == 1U
            && entry.status
                == runtime::simir::RegionFrontierStatusV2::generic_update_batch_ready
            && entry.borrowed_tasks[0U].process_id == 1U
            && entry.borrowed_tasks[0U].key.stable_order == 1U,
        "the generated entry consumes the exact single root key before the foreign callback");

    require_retained_frame_ack(probe, 1U, 1U);
    const auto& root_key = entry.borrowed_tasks[0U].key;
    require(probe.runtime_generation == entry.generation
            && probe.runtime_time == entry.time
            && probe.runtime_delta == entry.delta
            && probe.runtime_phase == entry.phase
            && probe.runtime_process_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && probe.native_member_dispatches == dispatches_before + 1U
            && probe.runtime_original_task_count == 1U
            && probe.runtime_original_tasks[0U] == root_key,
        "the retained Generic frame preserves the full borrowed root key and slot");

    const auto& pending = probe.pending_writes[0U];
    const auto expected_aval = expected_value.aval_words();
    const auto expected_bval = expected_value.bval_words();
    require(probe.captured_pending_write_count == 1U
            && !probe.pending_writes_truncated
            && pending.member_index == 0U
            && pending.member_process_id == 1U
            && pending.signal_id == expected_signal
            && pending.value_kind == runtime::simir::ValueKind::logic4
            && pending.width == expected_value.width()
            && pending.word_count
                == static_cast<std::uint32_t>(expected_aval.size())
            && pending.plane_count == 2U
            && !pending.words_truncated
            && pending.origin_time == entry.time
            && pending.origin_delta == entry.delta
            && pending.origin_round == 0U
            && pending.origin_stable_order == root_key.stable_order
            && pending.origin_sequence == root_key.sequence
            && pending.origin_process_domain == static_cast<std::uint32_t>(
                runtime::simir::ProcessSchedulingDomain::generic)
            && pending.origin_phase == static_cast<std::uint32_t>(
                runtime::SchedulerPhase::active),
        "the staged root descriptor carries its original member, signal, and scheduler origin");
    for (std::size_t word = 0U; word < expected_aval.size(); ++word) {
        require(pending.value_planes[0U][word] == expected_aval[word]
                && pending.value_planes[1U][word] == expected_bval[word],
            "the native root descriptor contains the expected complete Logic4 value");
    }
}

struct PositiveRun final {
    std::vector<GenericSignalSnapshot> final_signals;
};

struct DisplayHookCall final {
    runtime::simir::ProcessId process { };
    std::string text;
    bool newline { };
    runtime::SimulationTick time { };
    std::uint64_t delta { };
};

[[nodiscard]] PositiveRun run_positive_case(
    const GenericFixtureMode mode, const std::uint32_t width,
    GenericFixturePolicy policy = { })
{
    constexpr std::size_t whole_write_sites { 2U };
    const bool expected_region_kernel_enabled
        = policy.region_kernel_enabled.value_or(true);
    GenericWholeWriteFixture fixture { mode, width, whole_write_sites,
        false, false, false, runtime::simir::ValueKind::logic4,
        std::move(policy) };
    if (mode != GenericFixtureMode::interpreter) {
        require(fixture.region_kernel_enabled()
                == expected_region_kernel_enabled,
            "compiled fixture startup uses the explicitly requested or default-on region-kernel policy");
    }
    fixture.start_and_settle();
    const auto before = fixture.snapshot();
    const auto baseline_dispatches = fixture.native_member_dispatches();
    const auto input = input_pattern(width);
    if (width >= 3U) {
        require(input.bval_words()[0U] != 0U,
            "wide stimulus includes X/Z bits carried by the Logic4 bval plane");
    }
    fixture.schedule_input(input, 1U, 0U);

    const auto cut_result = fixture.run_to_pre_update_cut();
    require(cut_result.status == RunStatus::stopped,
        "the fixture stops at the Active safe point before ordinary Update publication");

    const auto cut = fixture.snapshot();
    const auto outputs = fixture.output_signals();
    require_deferred_output_state_unchanged(before, cut, outputs);
    const auto expected = invert(input);
    const std::array<PackedLogic4, whole_write_sites> pending_values {
        expected, expected
    };
    const bool native = mode != GenericFixtureMode::interpreter
        && expected_region_kernel_enabled;
    if (mode == GenericFixtureMode::interpreter || native) {
        require(cut.pending_update_sizes[0U]
                    == before.pending_update_sizes[0U] + whole_write_sites
                && cut.pending_update_sizes[1U]
                    == before.pending_update_sizes[1U] + whole_write_sites,
            "each interpreted or generated whole WriteUpdate has one detached packed descriptor and value");
        require_pending_suffix(before, cut, outputs, pending_values);
    } else {
        require(width > 64U,
            "the region-disabled packed-callback fixture exercises the wide checked word-staging route");
        require_checked_word_stage(before, cut, outputs, pending_values);
    }

    if (native) {
        require_retained_frame_ack(
            fixture.probe_frontier_state(), 1U, whole_write_sites);
        require(cut.frontier.found,
            "the production LLVM provider installed a generic frontier runtime");
        require(cut.frontier.execution_mode
                    == RegionFrontierExecutionModeV2::generic_deferred_update,
            "the production provider selected a generic deferred-Update V2 plan");
        require(cut.frontier.mutable_plane_roles_absent,
            "generic execution binds no writable current/LAST/stored/owner planes");
        require_boundary_value(cut, fixture.input_signal(), input,
            "generic boundary words equal the current Active input snapshot");
        require(cut.native_member_dispatches > baseline_dispatches,
            "the production generated frontier entry dispatches the generic member");
    } else {
        require(!cut.frontier.found,
            "the checked reference or region-disabled LLVM route has no frontier runtime");
        require(cut.native_member_dispatches == baseline_dispatches,
            "the checked process path does not increment generated frontier dispatches");
    }

    const auto resumed = fixture.resume_update_publication();
    require(resumed.status == RunStatus::completed
            || resumed.status == RunStatus::time_limit,
        "deferred generic Update publication completes through the ordinary scheduler");
    const auto final = fixture.snapshot();
    require(final.signals.size() == before.signals.size(),
        "the final snapshot retains the complete fixture signal set");
    for (const auto output_signal : outputs) {
        const auto& output = final.signals[output_signal];
        require(output.current == expected
                && output.stored == expected
                && output.raw_driver == before.signals[output_signal].raw_driver
                && output.raw_driver_process
                    == before.signals[output_signal].raw_driver_process,
            "ordinary Update publishes the whole-variable value while preserving its non-driving process provenance");
        require(output.transaction.has_value()
                && output.event.has_value()
                && output.event_domain
                    == runtime::simir::ProcessSchedulingDomain::generic
                && output.event_phase == runtime::SchedulerPhase::active
                && output.systemverilog_round == 0U,
            "the publication retains generic Active origin and round-zero transaction/event metadata");
    }
    if (whole_write_sites > 1U) {
        require(final.signals[outputs[0U]].event
                    == final.signals[outputs[1U]].event,
            "same-delta whole writes retain the same generic event stamp while descriptor order preserves source instruction order");
    }
    return { final.signals };
}

[[nodiscard]] PositiveRun run_display_effect_case(
    const GenericFixtureMode mode, std::vector<DisplayHookCall>& calls)
{
    constexpr std::uint32_t width { 65U };
    constexpr std::size_t whole_write_sites { 2U };
    GenericFixturePolicy policy;
    policy.region_kernel_enabled = true;
    policy.include_display_effect = true;
    GenericWholeWriteFixture fixture { mode, width, whole_write_sites,
        false, false, false, runtime::simir::ValueKind::logic4,
        std::move(policy) };
    fixture.start_and_settle();
    const auto before = fixture.snapshot();
    const auto baseline_dispatches = fixture.native_member_dispatches();
    fixture.set_output_hook(
        [&calls](const runtime::simir::ProcessId process,
            const std::string_view text, const bool newline,
            const runtime::SimulationTick time, const std::uint64_t delta) {
            calls.push_back({ process, std::string { text }, newline,
                time, delta });
        });

    const auto input = input_pattern(width);
    require(input != before.signals[fixture.input_signal()].current,
        "the selected stimulus changes the watched input");
    fixture.schedule_input(input, 1U, 0U);
    const auto cut_result = fixture.run_to_pre_update_cut();
    require(cut_result.status == RunStatus::stopped,
        "the effectful checked process reaches the pre-Update cut");
    const auto cut = fixture.snapshot();
    const auto outputs = fixture.output_signals();
    require_deferred_output_state_unchanged(before, cut, outputs);
    const auto expected = invert(input);
    require(expected != before.signals[outputs.front()].current,
        "the selected stimulus changes the output value");
    const std::array<PackedLogic4, whole_write_sites> pending_values {
        expected, expected
    };
    if (mode == GenericFixtureMode::interpreter) {
        require(cut.pending_update_sizes[0U]
                    == before.pending_update_sizes[0U] + whole_write_sites
                && cut.pending_update_sizes[1U]
                    == before.pending_update_sizes[1U] + whole_write_sites,
            "the interpreted effectful process appends two ordinary packed writes");
        require_pending_suffix(before, cut, outputs, pending_values);
    } else {
        require_checked_word_stage(before, cut, outputs, pending_values);
    }
    require(!cut.frontier.found
            && cut.native_member_dispatches == baseline_dispatches,
        "adding Display excludes this member from native frontier execution");
    require(cut.process_graph_pure.has_value()
            && !*cut.process_graph_pure,
        "RegionGraph marks the Display-bearing member non-pure");
    require(cut.generic_projected_region_attempts
                == before.generic_projected_region_attempts,
        "the effectful Generic member stays on ordinary checked execution");
    require(calls.size() == 1U
            && calls.front().process == fixture.process_id()
            && calls.front().text == "generic frontier effect probe"
            && !calls.front().newline
            && calls.front().time == 1U,
        "one changed input invokes the Display effect exactly once");

    const auto resumed = fixture.resume_update_publication();
    require(resumed.status == RunStatus::completed
            || resumed.status == RunStatus::time_limit,
        "the effectful checked writes publish through the ordinary scheduler");
    const auto final = fixture.snapshot();
    require(calls.size() == 1U,
        "resuming ordinary Update does not repeat the Display effect");
    for (const auto output_signal : outputs) {
        const auto& output = final.signals[output_signal];
        require(output.current == expected && output.stored == expected
                && output.raw_driver == before.signals[output_signal].raw_driver
                && output.raw_driver_process
                    == before.signals[output_signal].raw_driver_process,
            "ordinary Update publishes the output while preserving its driver provenance");
        require(output.transaction.has_value() && output.event.has_value()
                && output.event_domain
                    == runtime::simir::ProcessSchedulingDomain::generic
                && output.event_phase == runtime::SchedulerPhase::active
                && output.systemverilog_round == 0U,
            "the checked update retains its generic Active event and transaction metadata");
    }
    return { final.signals };
}

void run_display_effect_fallback_check()
{
    const auto interpreter_control = run_positive_case(
        GenericFixtureMode::interpreter, 65U);
    std::vector<DisplayHookCall> interpreter_calls;
    const auto interpreter_effect = run_display_effect_case(
        GenericFixtureMode::interpreter, interpreter_calls);
    const auto o0_control = run_positive_case(GenericFixtureMode::llvm_o0, 65U);
    std::vector<DisplayHookCall> o0_calls;
    const auto o0_effect = run_display_effect_case(
        GenericFixtureMode::llvm_o0, o0_calls);
    const auto o2_control = run_positive_case(GenericFixtureMode::llvm_o2, 65U);
    std::vector<DisplayHookCall> o2_calls;
    const auto o2_effect = run_display_effect_case(
        GenericFixtureMode::llvm_o2, o2_calls);
    require(interpreter_calls.size() == 1U
            && o0_calls.size() == 1U && o2_calls.size() == 1U,
        "interpreter, LLVM O0, and LLVM O2 each invoke Display once");
    require(interpreter_calls.front().time == 1U
            && o0_calls.front().time == 1U && o2_calls.front().time == 1U
            && interpreter_calls.front().time == o0_calls.front().time
            && interpreter_calls.front().time == o2_calls.front().time
            && interpreter_calls.front().delta == o0_calls.front().delta
            && interpreter_calls.front().delta == o2_calls.front().delta,
        "the one Display call keeps the same time/delta stamp in all execution modes");
    require(interpreter_effect.final_signals
                == interpreter_control.final_signals
            && o0_effect.final_signals == o0_control.final_signals
            && o2_effect.final_signals == o2_control.final_signals,
        "the added effect preserves each mode's complete passive state");
    require(o0_effect.final_signals == interpreter_effect.final_signals
            && o2_effect.final_signals == interpreter_effect.final_signals,
        "effectful checked execution matches interpreter state at O0 and O2");
}

[[nodiscard]] PositiveRun run_old_current_chain_case(
    const GenericFixtureMode mode, const std::uint32_t width)
{
    GenericFixturePolicy policy;
    policy.capture_generic_frontier = true;
    GenericWholeWriteFixture fixture { mode, width, 2U, false, true,
        false, runtime::simir::ValueKind::logic4, policy, false, true };
    fixture.start_and_settle();
    const auto before = fixture.snapshot();
    const auto before_generic_ticket_stats
        = fixture.generic_batch_compaction_stats();
    const auto baseline_dispatches = fixture.native_member_dispatches();
    const auto input = input_pattern(width);
    require(input.bval_words()[0U] != 0U,
        "wide chain stimulus includes X/Z bits carried by the Logic4 bval plane");
    fixture.schedule_input(input, 1U, 0U);

    const auto cut_result = fixture.run_to_pre_update_cut();
    require(cut_result.status == RunStatus::stopped,
        "the chain fixture stops after both same-prefix members and before Update");

    const auto cut = fixture.snapshot();
    const auto output_signals = fixture.output_signals();
    require_deferred_output_state_unchanged(before, cut, output_signals);
    require(cut.pending_update_sizes[0U]
                == before.pending_update_sizes[0U] + 2U
            && cut.pending_update_sizes[1U]
                == before.pending_update_sizes[1U] + 2U,
        "the same-prefix root and child each append one deferred whole write");
    const auto inverted = invert(input);
    const std::array<PackedLogic4, 2U> first_cut_values {
        inverted, before.signals[output_signals[0U]].current
    };
    require_pending_suffix(before, cut, output_signals, first_cut_values);

    const bool native = mode != GenericFixtureMode::interpreter;
    if (native) {
        require(before.region_completion_prepare_calls == 0U
                && before.region_completion_stage_calls == 0U
                && before.region_completion_commit_calls == 0U
                && cut.region_completion_prepare_calls == 0U
                && cut.region_completion_stage_calls == 0U
                && cut.region_completion_commit_calls == 0U,
            "the already parked LLVM members require no completion prepare, stage, or commit in Generic V2");
        require_retained_frame_ack(
            fixture.probe_frontier_state(), 2U, 2U);
        require(cut.frontier.found
                && cut.frontier.execution_mode
                    == RegionFrontierExecutionModeV2::generic_deferred_update,
            "the production provider selects the generic deferred-Update plan for the connected members");
        require(cut.frontier.mutable_plane_roles_absent,
            "generic chain evaluation binds no writable current/LAST/stored/owner planes");
        require_boundary_value(cut, fixture.input_signal(), input,
            "the generic boundary retains the external Active input snapshot");
        require_boundary_value(cut, output_signals[0U],
            before.signals[output_signals[0U]].current,
            "the later same-prefix member reads the internal signal's old current snapshot, not the root's staged write");
        require(cut.native_member_dispatches >= baseline_dispatches + 2U,
            "the real generated entry dispatches both members in the same offered Active prefix");
        const auto original_keys = fixture.generic_frontier_task_keys();
        require_captured_two_member_keys(
            fixture.frontier_entry_capture(), original_keys);
        require_two_member_generic_ticket(before_generic_ticket_stats,
            fixture.generic_batch_compaction_stats());
    } else {
        require(!cut.frontier.found,
            "the interpreter reference uses checked execution for the connected chain");
    }

    const auto resumed = fixture.resume_update_publication();
    require(resumed.status == RunStatus::completed
            || resumed.status == RunStatus::time_limit,
        "ordinary Update and the child delta complete after native staging");
    const auto final = fixture.snapshot();
    if (native) {
        require(final.region_completion_prepare_calls == 0U
                && final.region_completion_stage_calls == 0U
                && final.region_completion_commit_calls == 0U,
            "ordinary deferred Update publication leaves parked executor completion storage untouched");
    }
    require(final.signals[output_signals[0U]].current == inverted
            && final.signals[output_signals[1U]].current == inverted,
        "the child first observes old current in the offered prefix, then observes the committed root value in the next delta");
    for (const auto output_signal : output_signals) {
        const auto& output = final.signals[output_signal];
        require(output.stored == inverted
                && output.raw_driver == before.signals[output_signal].raw_driver
                && output.raw_driver_process
                    == before.signals[output_signal].raw_driver_process
                && output.transaction.has_value()
                && output.event.has_value()
                && output.event_domain
                    == runtime::simir::ProcessSchedulingDomain::generic
                && output.event_phase == runtime::SchedulerPhase::active
                && output.systemverilog_round == 0U,
            "both chain publications retain checked generic Active Update semantics and metadata");
    }
    require(final.signals[output_signals[0U]].event
                < final.signals[output_signals[1U]].event,
        "later-delta child publication follows the root member's earlier source-order event");
    return { final.signals };
}

[[nodiscard]] std::vector<GenericSignalSnapshot>
run_interleaved_late_invalidation_case(const GenericFixtureMode mode)
{
    constexpr std::uint32_t width { 65U };
    GenericFixturePolicy policy;
    policy.capture_generic_frontier
        = mode != GenericFixtureMode::interpreter;
    GenericWholeWriteFixture fixture { mode, width, 2U, false, true,
        false, runtime::simir::ValueKind::logic4, policy, false, true };
    fixture.start_and_settle();
    const auto before = fixture.snapshot();
    const auto before_ticket_stats = fixture.generic_batch_compaction_stats();
    const auto baseline_native_dispatches = fixture.native_member_dispatches();
    const bool is_native = mode != GenericFixtureMode::interpreter;
    const auto input = input_pattern(width);
    bool interleaver_ran { };
    std::uint64_t native_dispatches_at_interleaver { };
    SchedulerBatchCompactionStats ticket_stats_at_interleaver;
    GenericFrontierEntryCapture root_entry_at_interleaver;
    std::optional<GenericFrontierProbe> root_probe_at_interleaver;
    GenericQueuedMemberSnapshot child_receipt_before_invalidation;
    GenericQueuedMemberSnapshot child_receipt_after_invalidation;
    bool child_receipt_captured_before_invalidation { };
    bool child_receipt_captured_after_invalidation { };
    std::optional<GenericFixtureSnapshot> root_stage_before_intervention;
    const auto interleaved_middle = invert(input);
    const auto output_signals = fixture.output_signals();
    const std::array<runtime::simir::SignalId, 1U> checked_root_signal {
        output_signals[0U]
    };
    const std::array<PackedLogic4, 1U> checked_root_value {
        interleaved_middle
    };
    // The root is process 1 and the child is process 2. The foreign key uses
    // the child's stable order and was inserted before the readiness receipts;
    // root order 1 therefore runs first, while its earlier sequence at order 2
    // places the foreign callback before the child's receipt. The callback
    // snapshots the root's deferred Update staging before observation/deposit.
    // The second deposit sensitizes only the already-queued child through its
    // middle input; it must merge in ProcessState after observation invalidates
    // the native sidecar, without replacing the child's original queued receipt.
    fixture.schedule_input(input, 1U, 0U, [&] {
        interleaver_ran = true;
        root_entry_at_interleaver = fixture.frontier_entry_capture();
        const auto before_observation = fixture.probe_frontier_state();
        root_probe_at_interleaver = before_observation;
        native_dispatches_at_interleaver = fixture.native_member_dispatches();
        root_stage_before_intervention = fixture.snapshot();
        const auto before_end = before_observation.generic_queued_members.begin()
            + static_cast<std::ptrdiff_t>(
                before_observation.generic_queued_member_count);
        const auto before_child = std::ranges::find_if(
            before_observation.generic_queued_members.begin(), before_end,
            [](const auto& member) { return member.process_id == 2U; });
        if (before_child != before_end && before_child->receipt_valid
            && before_child->ready && before_child->process_queued) {
            child_receipt_before_invalidation = *before_child;
            child_receipt_captured_before_invalidation = true;
        }
        fixture.observe_signal_and_stop(fixture.output_signals()[0U]);
        fixture.deposit_signal(fixture.output_signals()[0U], interleaved_middle);
        ticket_stats_at_interleaver
            = fixture.generic_batch_compaction_stats();
        const auto after_observation = fixture.probe_frontier_state();
        const auto after_end = after_observation.generic_queued_members.begin()
            + static_cast<std::ptrdiff_t>(
                after_observation.generic_queued_member_count);
        const auto after_child = std::ranges::find_if(
            after_observation.generic_queued_members.begin(), after_end,
            [](const auto& member) { return member.process_id == 2U; });
        if (after_child != after_end && after_child->receipt_valid
            && after_child->ready && after_child->process_queued) {
            child_receipt_after_invalidation = *after_child;
            child_receipt_captured_after_invalidation = true;
        }
    }, 2U);
    const auto stopped = fixture.run_to_pre_update_cut();
    require(stopped.status == RunStatus::stopped && interleaver_ran,
        "the foreign Generic key stops between the two queued member keys");
    require(root_stage_before_intervention.has_value(),
        "the foreign callback captures root staging before observation");
    require_pending_suffix(before, *root_stage_before_intervention,
        checked_root_signal, checked_root_value);
    require_deferred_output_state_unchanged(before,
        *root_stage_before_intervention, output_signals);
    if (is_native) {
        require(root_probe_at_interleaver.has_value(),
            "the native root probe is captured before late observation");
        require_native_interleaved_root_stage(
            *root_stage_before_intervention, root_entry_at_interleaver,
            *root_probe_at_interleaver, checked_root_signal[0U],
            checked_root_value[0U], baseline_native_dispatches);
    }

    const auto cut = fixture.snapshot();
    require(cut.signals[output_signals[0U]].current == interleaved_middle
            && cut.signals[output_signals[1U]]
                == before.signals[output_signals[1U]],
        "the foreign deposit changes only the child-sensitive middle before Update");
    require_pending_suffix(before, cut, checked_root_signal,
        checked_root_value);
    if (is_native) {
        require(native_dispatches_at_interleaver
                    == baseline_native_dispatches + 1U
                && cut.native_member_dispatches
                    == baseline_native_dispatches + 1U
                && cut.frontier.found
                && cut.frontier.execution_mode
                    == RegionFrontierExecutionModeV2::generic_deferred_update
                && cut.frontier.generic_update_ack_count == 1U,
            "late observation retains the acknowledged native root while invalidating only future member dispatch");
        require(cut.process_executor_resume_calls
                    == before.process_executor_resume_calls,
            "the native root has not been replayed through its checked executor at the foreign key");
        const auto probe = fixture.probe_frontier_state();
        require_native_interleaved_root_stage(cut,
            root_entry_at_interleaver, probe, checked_root_signal[0U],
            checked_root_value[0U], baseline_native_dispatches);
        require(probe.runtime_found
                && !probe.runtime_graph_epochs_current
                && probe.pending_write_count == 1U
                && probe.staged_event_count == 1U
                && probe.generic_update_ack_count == 1U
                && probe.generic_queued_member_count == 2U
                && !probe.generic_queued_members_truncated,
            "the late observation retains the single native root commit and compact-ticket sidecars after revoking graph epochs");
        const auto queued_end = probe.generic_queued_members.begin()
            + static_cast<std::ptrdiff_t>(probe.generic_queued_member_count);
        const auto root = std::ranges::find_if(
            probe.generic_queued_members.begin(), queued_end,
            [](const auto& member) { return member.process_id == 1U; });
        const auto child = std::ranges::find_if(
            probe.generic_queued_members.begin(), queued_end,
            [](const auto& member) { return member.process_id == 2U; });
        // These loops are admitted without static_trigger_regions, so the
        // scheduler represents every sensitivity with one saturated
        // full-activation bit; a later trigger is an idempotent OR.
        require(root != queued_end
                && !root->ready && !root->receipt_valid
                && !root->process_queued
                && child != queued_end
                && child->ready && child->receipt_valid
                && child->process_queued
                && child->key.stable_order == 2U
                && child->key.payload != 0U
                && child->phase == runtime::SchedulerPhase::active
                && child_receipt_captured_before_invalidation
                && child_receipt_captured_after_invalidation
                && child_receipt_after_invalidation.key
                    == child_receipt_before_invalidation.key
                && child_receipt_after_invalidation.static_trigger_mask
                    == child_receipt_before_invalidation.static_trigger_mask
                && child_receipt_after_invalidation.process_static_trigger_mask
                    == child_receipt_before_invalidation.process_static_trigger_mask
                && child->key == child_receipt_before_invalidation.key
                && child->key == child_receipt_after_invalidation.key
                && child->static_trigger_mask
                    == child_receipt_before_invalidation.static_trigger_mask
                && child->static_trigger_mask
                    == child_receipt_after_invalidation.static_trigger_mask
                && child->time == child_receipt_before_invalidation.time
                && child->time == child_receipt_after_invalidation.time
                && child->delta == child_receipt_before_invalidation.delta
                && child->delta == child_receipt_after_invalidation.delta
                && child->phase == child_receipt_before_invalidation.phase
                && child->phase == child_receipt_after_invalidation.phase
                && child_receipt_before_invalidation.static_trigger_mask
                    == runtime::simir::Process::full_static_trigger_mask
                && child->process_static_trigger_mask
                    == child->static_trigger_mask
                && child->process_static_trigger_mask
                    == child_receipt_before_invalidation
                        .process_static_trigger_mask
                && child->process_static_trigger_mask
                    == child_receipt_after_invalidation
                        .process_static_trigger_mask
                && (child->process_static_trigger_mask
                    & child_receipt_before_invalidation
                        .process_static_trigger_mask)
                    == child_receipt_before_invalidation
                        .process_static_trigger_mask
                && (child->process_static_trigger_mask
                    & child->static_trigger_mask)
                    == child->static_trigger_mask,
            "late observation preserves the child's original receipt and saturated full-activation trigger mask");
    }

    const auto sentinel = fixture.run_to_completion();
    require(sentinel.status == RunStatus::stopped,
        "the queued maximum-order Active cut sentinel stops after the retained ticket suffix");
    const auto resumed = fixture.resume_update_publication();
    require(resumed.status == RunStatus::completed
            || resumed.status == RunStatus::time_limit,
        "the preserved ticket suffix and deferred Update finish after the cut sentinel resumes");
    const auto final = fixture.snapshot();
    if (is_native) {
        require(final.native_member_dispatches == baseline_native_dispatches + 1U,
            "late invalidation keeps the already-executed root native and sends only the child suffix to checked execution");
        require(final.process_executor_resume_calls
                    == before.process_executor_resume_calls + 1U,
            "the retained child executes exactly once after the root's native callback");
        require_two_member_generic_ticket(before_ticket_stats,
            ticket_stats_at_interleaver);
        const auto final_probe = fixture.probe_frontier_state();
        require(final_probe.runtime_found
                && final_probe.generic_queued_member_count == 2U,
            "the invalidated runtime remains readable after its compact ticket is consumed");
        const auto final_end = final_probe.generic_queued_members.begin()
            + static_cast<std::ptrdiff_t>(
                final_probe.generic_queued_member_count);
        const auto final_child = std::ranges::find_if(
            final_probe.generic_queued_members.begin(), final_end,
            [](const auto& member) { return member.process_id == 2U; });
        require(final_child != final_end && !final_child->ready
                && !final_child->receipt_valid && !final_child->process_queued,
            "checked fallback retires the stale sidecar receipt after consuming the live merged ProcessState mask");
    }
    return final.signals;
}

[[nodiscard]] PositiveRun run_parked_query_decline_case(
    const GenericFixtureMode mode, const std::uint32_t width)
{
    GenericWholeWriteFixture fixture { mode, width, 2U, false, false,
        false, runtime::simir::ValueKind::logic4, { }, false, true, false };
    fixture.start_and_settle();
    const auto before = fixture.snapshot();
    const auto baseline_dispatches = fixture.native_member_dispatches();
    const auto before_ticket_stats = fixture.generic_batch_compaction_stats();
    const auto input = input_pattern(width);
    fixture.schedule_input(input, 1U, 0U);
    const auto cut_result = fixture.run_to_pre_update_cut();
    require(cut_result.status == RunStatus::stopped,
        "an executor without a valid parked-state certificate falls back before Update");

    const auto cut = fixture.snapshot();
    const auto cut_ticket_stats = fixture.generic_batch_compaction_stats();
    const auto outputs = fixture.output_signals();
    require_deferred_output_state_unchanged(before, cut, outputs);
    const std::array<PackedLogic4, 2U> expected_pending {
        invert(input), invert(input)
    };
    require_checked_word_stage(before, cut, outputs, expected_pending);
    require(cut.frontier.found
            && cut.native_member_dispatches == baseline_dispatches
            && cut_ticket_stats.generic_readiness_ticket_queue_insertions
                == before_ticket_stats.generic_readiness_ticket_queue_insertions + 1U
            && cut_ticket_stats.generic_readiness_ticket_members
                == before_ticket_stats.generic_readiness_ticket_members + 1U
            && cut_ticket_stats.generic_readiness_ticket_members_elided
                == before_ticket_stats.generic_readiness_ticket_members_elided
            && cut.process_executor_resume_calls
                == before.process_executor_resume_calls + 1U,
        "a compact Generic readiness ticket reaches the frontier and its failed parked-state proof preserves one checked callback");
    require(cut.process_executor_resume_calls
                == before.process_executor_resume_calls + 1U,
        "the V2 decline runs the original compiled process callback exactly once");
    require(cut.region_completion_prepare_calls
                == before.region_completion_prepare_calls
            && cut.region_completion_stage_calls
                == before.region_completion_stage_calls
            && cut.region_completion_commit_calls
                == before.region_completion_commit_calls,
        "the checked fallback uses ordinary process resume and does not invoke native region-completion staging");

    const auto resumed = fixture.resume_update_publication();
    require(resumed.status == RunStatus::completed
            || resumed.status == RunStatus::time_limit,
        "the checked fallback's deferred Update completes exactly once");
    const auto final = fixture.snapshot();
    for (const auto output_signal : outputs) {
        const auto& output = final.signals[output_signal];
        require(output.current == invert(input)
                && output.stored == invert(input)
                && output.event.has_value()
                && output.transaction.has_value()
                && output.event_domain
                    == runtime::simir::ProcessSchedulingDomain::generic
                && output.event_phase == runtime::SchedulerPhase::active
                && output.systemverilog_round == 0U,
            "checked fallback preserves exact whole-write value and Generic Active metadata");
    }
    return { final.signals };
}

struct Logic9Run final {
    std::vector<std::vector<GenericSignalSnapshot>> waves;
};

[[nodiscard]] Logic9Run run_logic9_sequence(
    const GenericFixtureMode mode, const std::uint32_t width,
    const std::span<const PackedLogic4> inputs, const bool old_current_chain,
    const std::optional<std::size_t> std_logic_output = std::nullopt)
{
    GenericFixturePolicy policy;
    policy.region_kernel_enabled = true;
    policy.local_wave_enabled = false;
    if (std_logic_output) {
        policy.output_resolution = std::pair {
            *std_logic_output, ResolutionKind::std_logic };
    }
    GenericWholeWriteFixture fixture { mode, width, 2U, false,
        old_current_chain, false, runtime::simir::ValueKind::logic9,
        std::move(policy) };
    fixture.start_and_settle();
    Logic9Run result;
    result.waves.reserve(inputs.size());
    const bool native = mode != GenericFixtureMode::interpreter;

    for (std::size_t wave = 0U; wave < inputs.size(); ++wave) {
        const auto& input = inputs[wave];
        require(input.is_logic9() && input.width() == width,
            "Logic9 sequence provides a typed stimulus at the fixture width");
        const auto before = fixture.snapshot();
        const auto baseline_dispatches = fixture.native_member_dispatches();
        const auto outputs = fixture.output_signals();
        if (std_logic_output) {
            const auto signal = outputs[*std_logic_output];
            const auto& output = before.signals[signal];
            const auto& sidecar = logic9_sidecar_for(before, signal);
            require(input != before.signals[fixture.input_signal()].current,
                "each resolved Logic9 stimulus changes the current input and wakes the parked writer");
            require(output.resolution == ResolutionKind::std_logic
                    && output.current
                        == before.signals[fixture.input_signal()].current
                    && output.raw_driver
                        == std::optional<PackedLogic4> {
                            before.signals[fixture.input_signal()].current }
                    && !output.direct_signal_materialization_pending
                    && output.raw_driver_process == fixture.process_id()
                    && sidecar.unlisted && !sidecar.generic_blocked
                    && sidecar.mask == 0U && sidecar.list_occurrences == 0U
                    && before.native_logic9_sidecar_count == 0U,
                "the std_logic Logic9 output starts with its original owner and an unlisted sidecar row");
        }
        const auto expected_time
            = static_cast<runtime::SimulationTick>(wave + 1U);
        fixture.schedule_input(input, expected_time, 0U);
        const auto cut_result = fixture.run_to_pre_update_cut();
        require(cut_result.status == RunStatus::stopped,
            "Logic9 generic execution stops before deferred Update publication");

        const auto cut = fixture.snapshot();
        require_deferred_output_state_unchanged(before, cut, outputs);
        require(cut.pending_update_sizes[0U]
                    == before.pending_update_sizes[0U] + outputs.size()
                && cut.pending_update_sizes[1U]
                    == before.pending_update_sizes[1U] + outputs.size(),
            "each Logic9 whole WriteUpdate appends one deferred value and descriptor");
        const auto old_internal = before.signals[outputs[0U]].current;
        const std::array<PackedLogic4, 2U> pending_values {
            input, old_current_chain ? old_internal : input
        };
        require_pending_suffix(before, cut, outputs, pending_values);

        if (std_logic_output) {
            const auto signal = outputs[*std_logic_output];
            const auto& sidecar_before = logic9_sidecar_for(before, signal);
            const auto& sidecar_cut = logic9_sidecar_for(cut, signal);
            require(cut.signals[signal].resolution
                        == ResolutionKind::std_logic
                    && !cut.signals[signal].direct_signal_materialization_pending
                    && sidecar_cut.generic_blocked
                    && !sidecar_cut.unlisted && sidecar_cut.mask == 0U
                    && sidecar_cut.planes == sidecar_before.planes
                    && sidecar_cut.list_occurrences == 1U
                    && cut.native_logic9_sidecar_count == 1U,
                "the deferred Generic Logic9 write checkpoints one clean std_logic sidecar row before Update");
        }

        if (native) {
            require_retained_frame_ack(fixture.probe_frontier_state(),
                old_current_chain ? 2U : 1U, outputs.size(), expected_time);
            const auto probe = fixture.probe_frontier_state();
            require(!probe.pending_writes_truncated
                    && probe.captured_pending_write_count == outputs.size(),
                "the no-allocation probe retains every active Logic9 pending write");
            for (std::size_t write = 0U; write < outputs.size(); ++write) {
                const auto& descriptor = probe.pending_writes[write];
                const auto& expected = pending_values[write];
                const auto expected_source_instruction
                    = old_current_chain ? (write == 0U ? 2U : 1U)
                                        : static_cast<std::uint32_t>(2U + write);
                const auto expected_member_process_id
                    = old_current_chain && write == 1U ? 2U : 1U;
                const auto expected_layout_owner_process_id
                    = std_logic_output && *std_logic_output == write
                    ? std::numeric_limits<std::uint32_t>::max()
                    : expected_member_process_id;
                require(!descriptor.words_truncated
                        && descriptor.member_index
                            == (old_current_chain && write == 1U ? 1U : 0U)
                        && descriptor.value_kind
                            == runtime::simir::ValueKind::logic9
                        && descriptor.width == width
                        && descriptor.source_instruction
                            == expected_source_instruction
                        && descriptor.word_count
                            == expected.logic9_plane_words(0U).size()
                        && descriptor.plane_count == 4U
                        && descriptor.signal_slot
                            < probe.runtime_signal_slot_count
                        && descriptor.signal_id == outputs[write]
                        && descriptor.signal_owner_process_id
                            == expected_layout_owner_process_id
                        && descriptor.member_process_id
                            == expected_member_process_id,
                    "the retained V2 descriptor records its typed destination and four-plane shape");
                for (std::size_t plane = 0U; plane < 4U; ++plane) {
                    require(std::ranges::equal(
                                std::span<const std::uint64_t> {
                                    descriptor.value_planes[plane]
                                        .data(), descriptor.word_count },
                                expected.logic9_plane_words(plane)),
                        "the generated pending Logic9 entry preserves every expected plane word before Update");
                }
                require(descriptor.origin_time
                            == static_cast<std::uint64_t>(wave + 1U)
                        && descriptor.origin_delta == probe.runtime_delta
                        && descriptor.origin_round == 0U
                        && descriptor.origin_process_domain
                            == static_cast<std::uint32_t>(
                                runtime::simir::ProcessSchedulingDomain::generic)
                        && descriptor.origin_phase
                            == static_cast<std::uint32_t>(
                                runtime::SchedulerPhase::active),
                    "the pending Logic9 value keeps its Generic Active round-zero origin");
            }
            require(cut.frontier.found
                    && cut.frontier.execution_mode
                        == RegionFrontierExecutionModeV2::generic_deferred_update
                    && cut.frontier.mutable_plane_roles_absent,
                "Logic9 generic execution stages with read-only roles and the typed deferred-Update plan");
            require_boundary_value(cut, fixture.input_signal(), input,
                "Logic9 external input words are copied into read-only boundary planes");
            if (old_current_chain) {
                require_boundary_value(cut, outputs[0U], old_internal,
                    "the later same-prefix Logic9 member reads old current, not the root's private pending planes");
            }
            require(cut.native_member_dispatches
                    >= baseline_dispatches + (old_current_chain ? 2U : 1U),
                "the real LLVM frontier entry dispatches the Logic9 member prefix");
        } else {
            require(!cut.frontier.found,
                "Logic9 interpreter reference remains on checked execution");
        }

        const auto resumed = fixture.resume_update_publication();
        require(resumed.status == RunStatus::completed
                || resumed.status == RunStatus::time_limit,
            "the ordinary Generic Update publishes staged Logic9 whole writes");
        const auto final = fixture.snapshot();
        if (std_logic_output) {
            const auto signal = outputs[*std_logic_output];
            const auto& sidecar_before = logic9_sidecar_for(before, signal);
            const auto& sidecar_after = logic9_sidecar_for(final, signal);
            require(sidecar_after.unlisted
                    && !sidecar_after.generic_blocked
                    && sidecar_after.mask == 0U
                    && sidecar_after.planes == sidecar_before.planes
                    && sidecar_after.list_occurrences == 0U
                    && !final.signals[signal].direct_signal_materialization_pending
                    && final.native_logic9_sidecar_count == 0U,
                "ordinary Update drains the Generic Logic9 sidecar row without changing its private planes");
        }
        for (const auto output_signal : outputs) {
            const auto& output = final.signals[output_signal];
            const bool resolved_output
                = output.resolution == ResolutionKind::std_logic;
            const bool value_changed
                = before.signals[output_signal].current != input;
            require(output.value_kind == runtime::simir::ValueKind::logic9
                    && output.current == input
                    && output.stored == input
                    && (resolved_output
                        ? output.raw_driver == std::optional<PackedLogic4> { input }
                            && output.raw_driver_process
                                == fixture.process_id()
                        : output.raw_driver
                                == before.signals[output_signal].raw_driver
                            && output.raw_driver_process
                                == before.signals[output_signal].raw_driver_process)
                    && output.transaction.has_value(),
                "all nine-state whole-write planes publish through the original typed owner");
            if (value_changed) {
                require(output.event.has_value()
                        && output.event_domain
                            == runtime::simir::ProcessSchedulingDomain::generic
                        && output.event_phase == runtime::SchedulerPhase::active
                        && output.systemverilog_round == 0U,
                    "a changed Logic9 value records its Generic Active round-zero event");
            } else {
                require(output.event == before.signals[output_signal].event
                        && output.value_revision
                            == before.signals[output_signal].value_revision,
                    "an equal-current Logic9 transaction creates no value event or revision");
            }
        }
        result.waves.push_back(final.signals);
    }
    return result;
}

void run_logic9_width(const std::uint32_t width)
{
    const std::array<PackedLogic4, 1U> inputs { logic9_pattern(width) };
    const auto interpreter = run_logic9_sequence(
        GenericFixtureMode::interpreter, width, inputs, false);
    const auto o0 = run_logic9_sequence(
        GenericFixtureMode::llvm_o0, width, inputs, false);
    const auto o2 = run_logic9_sequence(
        GenericFixtureMode::llvm_o2, width, inputs, false);
    require(o0.waves == interpreter.waves && o2.waves == interpreter.waves,
        "Logic9 O0/O2 whole-write planes and metadata match the checked reference");
}

void run_logic9_all_states()
{
    constexpr std::array states {
        Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::z,
        Logic9::w, Logic9::l, Logic9::h, Logic9::dont_care
    };
    std::array<PackedLogic4, states.size()> inputs {
        logic9_pattern(1U, 0U), logic9_pattern(1U, 1U),
        logic9_pattern(1U, 2U), logic9_pattern(1U, 3U),
        logic9_pattern(1U, 4U), logic9_pattern(1U, 5U),
        logic9_pattern(1U, 6U), logic9_pattern(1U, 7U),
        logic9_pattern(1U, 8U)
    };
    for (std::size_t index = 0U; index < states.size(); ++index) {
        inputs[index].set_logic9(0U, states[index]);
    }
    const auto interpreter = run_logic9_sequence(
        GenericFixtureMode::interpreter, 1U, inputs, false);
    const auto o0 = run_logic9_sequence(
        GenericFixtureMode::llvm_o0, 1U, inputs, false);
    const auto o2 = run_logic9_sequence(
        GenericFixtureMode::llvm_o2, 1U, inputs, false);
    require(o0.waves == interpreter.waves && o2.waves == interpreter.waves,
        "all nine Logic9 scalar values match checked Generic Update at O0 and O2");
}

void run_logic9_std_logic_outputs()
{
    constexpr std::array states {
        Logic9::x, Logic9::zero, Logic9::one, Logic9::z, Logic9::w,
        Logic9::l, Logic9::h, Logic9::dont_care, Logic9::u
    };
    std::array<PackedLogic4, states.size()> scalar_inputs {
        logic9_pattern(1U, 0U), logic9_pattern(1U, 1U),
        logic9_pattern(1U, 2U), logic9_pattern(1U, 3U),
        logic9_pattern(1U, 4U), logic9_pattern(1U, 5U),
        logic9_pattern(1U, 6U), logic9_pattern(1U, 7U),
        logic9_pattern(1U, 8U)
    };
    for (std::size_t index = 0U; index < states.size(); ++index) {
        scalar_inputs[index].set_logic9(0U, states[index]);
    }
    constexpr std::size_t resolved_output_index { 1U };
    const auto interpreter = run_logic9_sequence(
        GenericFixtureMode::interpreter, 1U, scalar_inputs, false,
        resolved_output_index);
    const auto o0 = run_logic9_sequence(
        GenericFixtureMode::llvm_o0, 1U, scalar_inputs, false,
        resolved_output_index);
    const auto o2 = run_logic9_sequence(
        GenericFixtureMode::llvm_o2, 1U, scalar_inputs, false,
        resolved_output_index);
    require(o0.waves == interpreter.waves && o2.waves == interpreter.waves,
        "all nine Logic9 values publish through a native narrow std_logic owner with checked parity");

    const std::array<PackedLogic4, 1U> wide_inputs {
        logic9_pattern(64U)
    };
    const auto wide_interpreter = run_logic9_sequence(
        GenericFixtureMode::interpreter, 64U, wide_inputs, false,
        resolved_output_index);
    const auto wide_o0 = run_logic9_sequence(
        GenericFixtureMode::llvm_o0, 64U, wide_inputs, false,
        resolved_output_index);
    const auto wide_o2 = run_logic9_sequence(
        GenericFixtureMode::llvm_o2, 64U, wide_inputs, false,
        resolved_output_index);
    require(wide_o0.waves == wide_interpreter.waves
            && wide_o2.waves == wide_interpreter.waves,
        "64-bit Logic9 std_logic owner publication preserves all four planes at O0/O2");
}

void run_logic9_old_current_chain()
{
    constexpr std::uint32_t width { 129U };
    const std::array<PackedLogic4, 1U> inputs { logic9_pattern(width) };
    const auto interpreter = run_logic9_sequence(
        GenericFixtureMode::interpreter, width, inputs, true);
    const auto o0 = run_logic9_sequence(
        GenericFixtureMode::llvm_o0, width, inputs, true);
    const auto o2 = run_logic9_sequence(
        GenericFixtureMode::llvm_o2, width, inputs, true);
    require(o0.waves == interpreter.waves && o2.waves == interpreter.waves,
        "Logic9 same-prefix consumers preserve old-current semantics at O0/O2");
}

void run_width(const std::uint32_t width)
{
    const auto interpreter = run_positive_case(
        GenericFixtureMode::interpreter, width);
    const auto o0 = run_positive_case(GenericFixtureMode::llvm_o0, width);
    const auto o2 = run_positive_case(GenericFixtureMode::llvm_o2, width);
    require(o0.final_signals == interpreter.final_signals
            && o2.final_signals == interpreter.final_signals,
        "native O0 and O2 generic whole-write results and metadata match checked interpretation");
}

void run_old_current_chain(const std::uint32_t width)
{
    const auto interpreter = run_old_current_chain_case(
        GenericFixtureMode::interpreter, width);
    const auto o0 = run_old_current_chain_case(
        GenericFixtureMode::llvm_o0, width);
    const auto o2 = run_old_current_chain_case(
        GenericFixtureMode::llvm_o2, width);
    require(o0.final_signals == interpreter.final_signals
            && o2.final_signals == interpreter.final_signals,
        "native O0/O2 old-current prefix snapshots and final delta results match checked interpretation");
}

void run_generic_ticket_interleaving_and_late_invalidation()
{
    const auto interpreter = run_interleaved_late_invalidation_case(
        GenericFixtureMode::interpreter);
    const auto o0 = run_interleaved_late_invalidation_case(
        GenericFixtureMode::llvm_o0);
    const auto o2 = run_interleaved_late_invalidation_case(
        GenericFixtureMode::llvm_o2);
    require(o0 == interpreter && o2 == interpreter,
        "O0/O2 ticket prefix plus checked suffix matches interpreter values and metadata");
}

[[nodiscard]] std::vector<GenericSignalSnapshot>
run_future_generic_receipt_case(const GenericFixtureMode mode)
{
    constexpr std::uint32_t width { 65U };
    GenericFixturePolicy policy;
    policy.capture_generic_frontier
        = mode != GenericFixtureMode::interpreter;
    GenericWholeWriteFixture fixture { mode, width, 2U, false, true,
        false, runtime::simir::ValueKind::logic4, policy };
    fixture.start_and_settle();

    const auto initial_input = input_pattern(width);
    const auto retriggered_input = invert(initial_input);
    bool interleaver_ran { };
    std::optional<GenericFrontierProbe> before_foreign;
    std::optional<GenericFrontierProbe> after_foreign;
    SchedulerBatchCompactionStats tickets_before_foreign;
    SchedulerBatchCompactionStats tickets_after_foreign;
    std::optional<runtime::SchedulerOrderKey> foreign_key;
    std::optional<runtime::SchedulerOrderKey> future_stop_key;
    GenericFrontierEntryCapture root_entry_before_foreign;
    GenericTaskKeySnapshot future_root_key;
    runtime::SimulationTick future_root_time { };
    std::uint64_t future_root_delta { };
    std::uint64_t native_dispatches_after_current_ticket { };
    fixture.schedule_input(initial_input, 1U, 0U, [&] {
        interleaver_ran = true;
        root_entry_before_foreign = fixture.frontier_entry_capture();
        before_foreign = fixture.probe_frontier_state();
        tickets_before_foreign = fixture.generic_batch_compaction_stats();

        // This ordinary boundary stimulus requeues the already-consumed root
        // for the next delta without observing the graph or invalidating its
        // V2 component. The child retains its current ticket key.
        fixture.deposit_signal(fixture.input_signal(), retriggered_input);
        future_stop_key = fixture.schedule_next_delta_active_stop(1U);

        after_foreign = fixture.probe_frontier_state();
        tickets_after_foreign = fixture.generic_batch_compaction_stats();
    }, 2U, &foreign_key);

    const auto stopped = fixture.run_to_pre_update_cut();
    require(stopped.status == RunStatus::stopped && interleaver_ran
            && before_foreign.has_value() && after_foreign.has_value(),
        "the external Active key runs between the original Generic member receipts");

    if (mode != GenericFixtureMode::interpreter) {
        const auto find_member = [](const GenericFrontierProbe& probe,
                                    const runtime::simir::ProcessId process)
            -> const GenericQueuedMemberSnapshot* {
            const auto end = probe.generic_queued_members.begin()
                + static_cast<std::ptrdiff_t>(
                    probe.generic_queued_member_count);
            const auto found = std::ranges::find_if(
                probe.generic_queued_members.begin(), end,
                [process](const auto& member) {
                    return member.process_id == process;
                });
            return found == end ? nullptr : &*found;
        };
        const auto* const root_before = find_member(
            *before_foreign, 1U);
        const auto* const child_before = find_member(
            *before_foreign, 2U);
        const auto* const root_after = find_member(
            *after_foreign, 1U);
        const auto* const child_after = find_member(
            *after_foreign, 2U);
        require(before_foreign->runtime_found
                && before_foreign->runtime_graph_epochs_current
                && before_foreign->generic_queued_member_count == 2U
                && !before_foreign->generic_queued_members_truncated
                && root_before != nullptr && !root_before->process_queued
                && !root_before->ready && !root_before->receipt_valid
                && child_before != nullptr && child_before->process_queued
                && child_before->ready && child_before->receipt_valid
                && after_foreign->runtime_found
                && !after_foreign->runtime_invalidated
                && after_foreign->runtime_graph_epochs_current
                && after_foreign->generic_queued_member_count == 2U
                && !after_foreign->generic_queued_members_truncated
                && root_after != nullptr && root_after->process_queued
                && root_after->ready && root_after->receipt_valid
                && root_after->key.stable_order == 1U
                && root_after->key.payload
                    == root_entry_before_foreign.borrowed_tasks[0U].key.payload
                && foreign_key.has_value()
                && future_stop_key.has_value()
                && root_entry_before_foreign.called
                && root_entry_before_foreign.frontier_present
                && root_entry_before_foreign.borrowed_task_count == 1U
                && root_entry_before_foreign.frame_task_count == 1U
                && root_entry_before_foreign.frame_task_cursor == 1U
                && root_entry_before_foreign.status
                    == runtime::simir::RegionFrontierStatusV2::generic_update_batch_ready
                && root_entry_before_foreign.borrowed_tasks[0U].process_id == 1U
                && root_entry_before_foreign.borrowed_tasks[0U].key.stable_order
                    == 1U
                && root_entry_before_foreign.borrowed_tasks[0U].key.stable_order
                    < foreign_key->order
                && foreign_key->order
                    == child_before->key.stable_order
                && foreign_key->sequence
                    < child_before->key.sequence
                && root_after->key.sequence > foreign_key->sequence
                && root_after->time == child_before->time
                && child_before->delta
                    != std::numeric_limits<std::uint64_t>::max()
                && root_after->delta == child_before->delta + 1U
                && root_after->phase == runtime::SchedulerPhase::active
                && root_after->static_trigger_mask != 0U
                && root_after->static_trigger_mask
                    == root_after->process_static_trigger_mask
                && child_after != nullptr && child_after->process_queued
                && child_after->ready && child_after->receipt_valid
                && child_after->key == child_before->key
                && child_after->time == child_before->time
                && child_after->delta == child_before->delta
                && child_after->phase == child_before->phase
                && child_after->static_trigger_mask
                    == child_before->static_trigger_mask,
            "the foreign input issues a next-delta root receipt and preserves "
            "the child's current original key");
        future_root_key = root_after->key;
        future_root_time = root_after->time;
        future_root_delta = root_after->delta;
        require(tickets_after_foreign.generic_readiness_ticket_queue_insertions
                    == tickets_before_foreign.generic_readiness_ticket_queue_insertions
                        + 1U
                && tickets_after_foreign.generic_readiness_ticket_members
                    == tickets_before_foreign.generic_readiness_ticket_members
                        + 1U,
            "the retrigger creates one scheduler-owned Generic future ticket member");
        require(future_stop_key->order == 1U
                && future_stop_key->sequence > root_after->key.sequence,
            "the later same-order Active cut has an exact scheduler key "
            "after the future root receipt");

        const auto entry = fixture.frontier_entry_capture();
        const auto cut_probe = fixture.probe_frontier_state();
        native_dispatches_after_current_ticket
            = cut_probe.native_member_dispatches;
        const auto* const root_at_cut = find_member(cut_probe, 1U);
        const auto* const child_at_cut = find_member(cut_probe, 2U);
        require(cut_probe.runtime_found
                && !cut_probe.runtime_invalidated
                && cut_probe.runtime_graph_epochs_current
                && cut_probe.native_member_dispatches
                    == after_foreign->native_member_dispatches + 1U
                && cut_probe.runtime_task_count == 1U
                && cut_probe.runtime_task_cursor == 1U
                && cut_probe.runtime_original_task_count == 1U
                && !cut_probe.runtime_original_tasks_truncated
                && cut_probe.runtime_original_tasks[0U]
                    == child_before->key
                && root_at_cut != nullptr && root_at_cut->process_queued
                && root_at_cut->ready && root_at_cut->receipt_valid
                && root_at_cut->key == root_after->key
                && root_at_cut->time == root_after->time
                && root_at_cut->delta == root_after->delta
                && root_at_cut->phase == root_after->phase
                && child_at_cut != nullptr && !child_at_cut->process_queued
                && !child_at_cut->ready && !child_at_cut->receipt_valid,
            "V2 consumes only the offered child key while the exact future "
            "root receipt remains queued");
        require(entry.called && entry.frontier_present
                && !entry.tasks_truncated
                && entry.phase == runtime::SchedulerPhase::active
                && entry.borrowed_task_count == 1U
                && entry.frame_task_count == 1U
                && entry.frame_task_cursor == 1U
                && entry.status
                    == runtime::simir::RegionFrontierStatusV2::generic_update_batch_ready
                && entry.borrowed_tasks[0U].process_id == 2U
                && entry.borrowed_tasks[0U].key == child_before->key,
            "the real Generic V2 entry authenticates the child's original "
            "current key without importing the future root ticket");
    }

    const auto future_stop = fixture.run_to_completion();
    require(future_stop.status == RunStatus::stopped,
        "the later same-order Active cut stops after the future root prefix");
    if (mode != GenericFixtureMode::interpreter) {
        const auto future_entry = fixture.frontier_entry_capture();
        const auto future_probe = fixture.probe_frontier_state();
        const auto future_end = future_probe.generic_queued_members.begin()
            + static_cast<std::ptrdiff_t>(std::min<std::size_t>(
                future_probe.generic_queued_member_count,
                future_probe.generic_queued_members.size()));
        const auto future_root = std::ranges::find_if(
            future_probe.generic_queued_members.begin(),
            future_end,
            [](const auto& member) { return member.process_id == 1U; });
        const auto future_child = std::ranges::find_if(
            future_probe.generic_queued_members.begin(),
            future_end,
            [](const auto& member) { return member.process_id == 2U; });
        require(future_entry.called && future_entry.frontier_present
                && !future_entry.tasks_truncated
                && future_entry.phase == runtime::SchedulerPhase::active
                && future_entry.time == future_root_time
                && future_entry.delta == future_root_delta
                && future_entry.borrowed_task_count == 1U
                && future_entry.borrowed_tasks[0U].process_id == 1U
                && future_entry.borrowed_tasks[0U].key.stable_order
                    == future_stop_key->order
                && future_entry.borrowed_tasks[0U].key.sequence
                    < future_stop_key->sequence
                && future_entry.borrowed_tasks[0U].key
                    == future_root_key
                && future_entry.frame_task_count == 1U
                && future_entry.frame_task_cursor == 1U
                && future_entry.status
                    == runtime::simir::RegionFrontierStatusV2::generic_update_batch_ready
                && future_probe.native_member_dispatches
                    == native_dispatches_after_current_ticket + 1U
                && future_probe.runtime_task_count == 1U
                && future_probe.runtime_task_cursor == 1U
                && future_probe.runtime_original_task_count == 1U
                && !future_probe.runtime_original_tasks_truncated
                && future_probe.runtime_original_tasks[0U] == future_root_key
                && !future_probe.generic_queued_members_truncated
                && future_root != future_end
                && !future_root->process_queued && !future_root->ready
                && !future_root->receipt_valid
                && future_child != future_end
                && future_child->process_queued && future_child->ready
                && future_child->receipt_valid
                && future_child->key.stable_order == 2U
                && future_child->time == future_entry.time
                && future_child->delta == future_entry.delta
                && future_stop_key->order < future_child->key.stable_order,
            "the future root key is consumed once before the foreign cut; "
            "the child suffix remains queued");
    }

    const auto completed = fixture.run_to_completion();
    require(completed.status == RunStatus::completed
            || completed.status == RunStatus::time_limit,
        "the next-delta retrigger and deferred Generic Update drain normally");
    return fixture.snapshot().signals;
}

void run_generic_future_sidecar_authentication()
{
    const auto interpreter = run_future_generic_receipt_case(
        GenericFixtureMode::interpreter);
    const auto o0 = run_future_generic_receipt_case(
        GenericFixtureMode::llvm_o0);
    const auto o2 = run_future_generic_receipt_case(
        GenericFixtureMode::llvm_o2);
    require(o0 == interpreter && o2 == interpreter,
        "O0 and O2 preserve checked values and metadata across a future "
        "Generic receipt beside a current ticket suffix");
}

void run_parked_query_decline_fallback()
{
    constexpr std::uint32_t width { 129U };
    const auto interpreter = run_positive_case(
        GenericFixtureMode::interpreter, width);
    const auto o0 = run_parked_query_decline_case(
        GenericFixtureMode::llvm_o0, width);
    const auto o2 = run_parked_query_decline_case(
        GenericFixtureMode::llvm_o2, width);
    require(o0.final_signals == interpreter.final_signals
            && o2.final_signals == interpreter.final_signals,
        "parked-certificate decline preserves checked current/LAST/stored, raw-owner, and transaction/event results at O0/O2");
}

void run_generic_region_policy_checks()
{
    constexpr std::uint32_t width { 65U };
    const auto reference = run_positive_case(
        GenericFixtureMode::interpreter, width);

    const GenericFixturePolicy region_only { true, false };
    const auto o0_local_wave_off = run_positive_case(
        GenericFixtureMode::llvm_o0, width, region_only);
    const auto o2_local_wave_off = run_positive_case(
        GenericFixtureMode::llvm_o2, width, region_only);
    require(o0_local_wave_off.final_signals == reference.final_signals
            && o2_local_wave_off.final_signals == reference.final_signals,
        "the Generic frontier remains native with SystemVerilog local-wave state disabled and matches checked interpretation");

    const GenericFixturePolicy region_disabled { false, false };
    const auto o0_checked = run_positive_case(
        GenericFixtureMode::llvm_o0, width, region_disabled);
    const auto o2_checked = run_positive_case(
        GenericFixtureMode::llvm_o2, width, region_disabled);
    require(o0_checked.final_signals == reference.final_signals
            && o2_checked.final_signals == reference.final_signals,
        "disabling region kernels keeps the same LLVM process executor on the checked route with unchanged parity");
}

} // namespace

void run_native_frontier_generic_update_tests()
{
#if defined(FSIM_HAS_LLVM)
    run_width(1U);
    run_width(65U);
    run_width(129U);
    run_old_current_chain(129U);
    run_generic_ticket_interleaving_and_late_invalidation();
    run_generic_future_sidecar_authentication();
    run_parked_query_decline_fallback();
    run_logic9_all_states();
    run_logic9_std_logic_outputs();
    run_logic9_width(65U);
    run_logic9_width(129U);
    run_logic9_width(256U);
    run_logic9_width(1024U);
    run_logic9_old_current_chain();
    run_generic_region_policy_checks();
    run_display_effect_fallback_check();
#else
    throw std::runtime_error {
        "the generic native-frontier witness requires LLVM"
    };
#endif
}
} // namespace fsim::tests::app::frontier

int main()
{
    try {
        fsim::tests::app::frontier::run_native_frontier_generic_update_tests();
    } catch (const std::exception& error) {
        std::cerr << "generic native-frontier test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
