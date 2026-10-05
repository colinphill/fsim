// SPDX-License-Identifier: Apache-2.0
#include "native_frontier_generic_update_failure_interposer.hpp"
#include "native_frontier_generic_update_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::app::frontier {
namespace {

using runtime::Logic4;
using runtime::Logic9;
using runtime::PackedLogic4;
using runtime::RunStatus;
using runtime::simir::ProcessId;
using allocation_failure::FailureSample;

constexpr std::uint32_t test_width { 129U };
constexpr std::size_t whole_write_sites { 4U };
constexpr std::size_t maximum_allocation_cut { 256U };

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

[[nodiscard]] PackedLogic4 input_value(
    const std::uint32_t width, const std::size_t trial,
    const runtime::simir::ValueKind value_kind)
{
    if (value_kind == runtime::simir::ValueKind::logic9) {
        constexpr std::array states {
            Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::z,
            Logic9::w, Logic9::l, Logic9::h, Logic9::dont_care
        };
        auto value = PackedLogic4 { width, Logic4::zero }.promoted_to_logic9();
        for (std::uint32_t bit = 0U; bit < width; ++bit) {
            value.set_logic9(bit,
                states[(static_cast<std::size_t>(bit) + trial)
                    % states.size()]);
        }
        return value;
    }
    return PackedLogic4 { width,
        trial % 2U == 0U ? Logic4::one : Logic4::zero };
}

[[nodiscard]] PackedLogic4 inverse(const PackedLogic4& input)
{
    PackedLogic4 result { input.width(), Logic4::zero };
    for (std::size_t bit = 0U; bit < input.width(); ++bit) {
        result.set(bit, runtime::logic_not(input.get(bit)));
    }
    return result;
}

[[nodiscard]] PackedLogic4 expected_output(
    const PackedLogic4& input,
    const runtime::simir::ValueKind value_kind)
{
    return value_kind == runtime::simir::ValueKind::logic9
        ? input : inverse(input);
}

struct ObserverContext final {
    GenericWholeWriteFixture* fixture { };
    FailureSample sample;
};

struct FailureTrialResult final {
    bool injected { };
    bool post_entry_failure { };
    bool recovered_optional_injection { };
};

void observe_failure(void* const context) noexcept
{
    auto& probe = *static_cast<ObserverContext*>(context);
    probe.sample.frontier = probe.fixture->probe_frontier_state();
    probe.sample.observed = true;
}

struct ClearFailureOnExit final {
    ~ClearFailureOnExit()
    {
        allocation_failure::clear_observer();
        allocation_failure::clear();
    }
};

void require_output_state_unchanged(
    const GenericFixtureSnapshot& before,
    const GenericFixtureSnapshot& after,
    const std::optional<runtime::simir::SignalId> checked_suffix_input)
{
    require(before.signals.size() == after.signals.size()
            && before.signals.size() >= whole_write_sites + 2U,
        "the generic failure fixture snapshots all native and checked outputs");
    for (std::size_t index = 1U; index < before.signals.size(); ++index) {
        if (checked_suffix_input
            && before.signals[index].signal == *checked_suffix_input) {
            continue;
        }
        require(before.signals[index] == after.signals[index],
            "an unacknowledged generic attempt leaves current/LAST/stored, owner, and event metadata unchanged");
    }
}

void require_scheduled_inputs(
    const GenericFixtureSnapshot& snapshot,
    const runtime::simir::SignalId root_input,
    const runtime::simir::SignalId checked_suffix_input,
    const PackedLogic4& value)
{
    require(root_input != checked_suffix_input,
        "the checked suffix has an independent stimulus signal");
    const auto root = std::ranges::find(snapshot.signals, root_input,
        &GenericSignalSnapshot::signal);
    const auto suffix = std::ranges::find(snapshot.signals,
        checked_suffix_input, &GenericSignalSnapshot::signal);
    require(root != snapshot.signals.end()
            && suffix != snapshot.signals.end()
            && root->current == value
            && suffix->current == value,
        "both independent inputs publish the same value before their Active work");
}

void require_published_values(
    const GenericFixtureSnapshot& before,
    const GenericFixtureSnapshot& snapshot,
    const PackedLogic4& input,
    const std::span<const runtime::simir::SignalId> outputs,
    const runtime::simir::SignalId checked_suffix,
    const runtime::simir::ValueKind value_kind)
{
    const auto expected = expected_output(input, value_kind);
    for (const auto signal : outputs) {
        const auto found = std::ranges::find(snapshot.signals, signal,
            &GenericSignalSnapshot::signal);
        const auto original = std::ranges::find(before.signals, signal,
            &GenericSignalSnapshot::signal);
        require(found != snapshot.signals.end()
                && original != before.signals.end()
                && found->value_kind == value_kind
                && found->current == expected
                && found->last == original->current
                && found->stored == expected
                && found->raw_driver == original->raw_driver
                && found->raw_driver_process == original->raw_driver_process
                && found->event.has_value()
                && found->transaction.has_value()
                && found->event_domain
                    == runtime::simir::ProcessSchedulingDomain::generic
                && found->event_phase == runtime::SchedulerPhase::active
                && found->systemverilog_round == 0U,
            "retry publication writes each full-width unresolved native output without changing its raw driver record");
    }
    const auto suffix = std::ranges::find(snapshot.signals, checked_suffix,
        &GenericSignalSnapshot::signal);
    const auto original_suffix = std::ranges::find(before.signals,
        checked_suffix, &GenericSignalSnapshot::signal);
    require(suffix != snapshot.signals.end()
            && original_suffix != before.signals.end()
            && suffix->value_kind == value_kind
            && suffix->current == input
            && suffix->last == original_suffix->current
            && suffix->stored == input
            && suffix->raw_driver == original_suffix->raw_driver
            && suffix->raw_driver_process
                == original_suffix->raw_driver_process
            && suffix->event.has_value()
            && suffix->transaction.has_value()
            && suffix->event_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && suffix->event_phase == runtime::SchedulerPhase::active
            && suffix->systemverilog_round == 0U,
        "the checked suffix publishes its unresolved full-width input value exactly once");
}

void require_logic9_pending_writes(
    const GenericFrontierProbe& probe,
    const PackedLogic4& expected,
    const std::span<const runtime::simir::SignalId> outputs,
    const ProcessId expected_process)
{
    require(expected.is_logic9() && expected.width() == test_width,
        "the wide failure witness supplies an explicitly typed Logic9 value");
    require(!probe.pending_writes_truncated
            && probe.captured_pending_write_count == outputs.size()
            && probe.pending_write_count == outputs.size()
            && probe.staged_event_count == outputs.size(),
        "the failure probe captures every generated Logic9 write descriptor");
    require(probe.runtime_task_count == 1U
            && probe.runtime_original_task_count == 1U,
        "the four Logic9 write sites retain the single generated member's original task key");
    const auto& task = probe.runtime_original_tasks[0U];
    for (std::size_t write = 0U; write < outputs.size(); ++write) {
        const auto& descriptor = probe.pending_writes[write];
        require(!descriptor.words_truncated
                && descriptor.member_index == 0U
                && descriptor.member_process_id == expected_process
                && descriptor.signal_slot < probe.runtime_signal_slot_count
                && descriptor.signal_id == outputs[write]
                && descriptor.signal_owner_process_id == expected_process
                && descriptor.source_instruction
                    == static_cast<std::uint32_t>(2U + write)
                && descriptor.value_kind
                    == runtime::simir::ValueKind::logic9
                && descriptor.width == expected.width()
                && descriptor.word_count
                    == expected.logic9_plane_words(0U).size()
                && descriptor.plane_count == 4U,
            "each generated write descriptor preserves its Logic9 destination, source instruction, and four-plane extent");
        require(descriptor.origin_time == probe.runtime_time
                && descriptor.origin_delta == probe.runtime_delta
                && descriptor.origin_round == 0U
                && descriptor.origin_stable_order == task.stable_order
                && descriptor.origin_sequence == task.sequence
                && descriptor.origin_process_domain
                    == static_cast<std::uint32_t>(
                        runtime::simir::ProcessSchedulingDomain::generic)
                && descriptor.origin_phase
                    == static_cast<std::uint32_t>(
                        runtime::SchedulerPhase::active),
            "each Logic9 write retains the exact Generic Active activation key as its origin");
        for (std::size_t plane = 0U; plane < 4U; ++plane) {
            const auto expected_words = expected.logic9_plane_words(plane);
            require(std::ranges::equal(
                        std::span<const std::uint64_t> {
                            descriptor.value_planes[plane].data(),
                            descriptor.word_count },
                        expected_words),
                "every Logic9 pending write retains all four expected wide plane words before Update");
        }
    }
}

[[nodiscard]] bool same_task_key(const GenericTaskKeySnapshot& left,
    const GenericTaskKeySnapshot& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.sequence == right.sequence && left.payload == right.payload;
}

void require_failure_frontier_probe(
    const GenericFrontierProbe& probe,
    const std::uint64_t runtime_dispatches_before,
    const std::array<std::size_t, 2U>& pending_sizes_before,
    const ProcessId expected_process)
{
    require(probe.frontier_present && probe.runtime_found,
        "the allocation observer captures a borrowed generic frontier with its V2 runtime present");
    require(!probe.frontier_tasks_truncated
            && probe.frontier_task_count <= generic_frontier_probe_task_capacity
            && probe.frontier_task_count == 1U
            && probe.frontier_cursor == 0U
            && probe.frontier_end == probe.frontier_task_count
            && probe.frontier_tasks[0U].stable_order == expected_process,
        "the no-allocation observer captures the exact root-only scheduler-authored task span");
    require(probe.frontier_process_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && probe.frontier_phase == runtime::SchedulerPhase::active
            && probe.frontier_systemverilog_round == 0U,
        "the failure belongs to a Generic Active round-zero callback");
    require(probe.runtime_generation == probe.frontier_generation
            && probe.runtime_time == probe.frontier_time
            && probe.runtime_delta == probe.frontier_delta
            && probe.runtime_process_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && probe.runtime_phase == runtime::SchedulerPhase::active
            && probe.runtime_systemverilog_round == 0U,
        "the generated frame is bound to the exact borrowed Generic Active key slot");
    require(probe.runtime_task_count == 1U
            && probe.runtime_task_cursor == probe.runtime_task_count
            && !probe.runtime_original_tasks_truncated
            && probe.runtime_original_task_count == probe.runtime_task_count
            && probe.runtime_task_count == probe.frontier_task_count,
        "the generated entry consumed the complete root-only offer with its retained exact key");
    require(probe.pending_write_count == whole_write_sites
            && probe.pending_write_count == probe.staged_event_count
            && probe.generic_update_ack_count == 0U,
        "the generated entry has staged every native output before host staging and ACK");
    require(probe.native_member_dispatches > runtime_dispatches_before,
        "the generated member body actually ran before the observed allocation cut");
    for (std::size_t index = 0U;
         index < probe.pending_update_sizes.size(); ++index) {
        require(probe.pending_update_sizes[index] >= pending_sizes_before[index]
                && probe.pending_update_sizes[index]
                    <= pending_sizes_before[index] + whole_write_sites,
            "the observer sees only the bounded native-output append prefix at the failure point");
    }

    require(same_task_key(probe.runtime_original_tasks[0U],
                probe.frontier_tasks[0U])
            && probe.runtime_original_tasks[0U].stable_order
                == expected_process,
        "the retained key exactly matches the sole root task without a synthesized suffix key");
}

void require_same_retry_prefix(const GenericFrontierProbe& failed,
    const GenericFrontierProbe& retried)
{
    require(retried.runtime_found && !retried.frontier_present
            && retried.runtime_original_task_count
                == failed.runtime_original_task_count
            && retried.runtime_task_count == failed.runtime_task_count
            && retried.runtime_task_cursor == retried.runtime_task_count
            && !retried.runtime_original_tasks_truncated,
        "retry retains the same complete generated task-prefix shape after the scheduler reoffers it");
    for (std::size_t index = 0U;
         index < failed.runtime_original_task_count; ++index) {
        require(same_task_key(failed.runtime_original_tasks[index],
                    retried.runtime_original_tasks[index]),
            "retry uses each original stable-order, sequence, and payload key without synthesis");
    }
    require(retried.runtime_time == failed.frontier_time
            && retried.runtime_delta == failed.frontier_delta
            && retried.runtime_systemverilog_round == 0U
            && retried.runtime_process_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && retried.runtime_phase == runtime::SchedulerPhase::active,
        "retry re-enters the same Generic Active time/delta/round slot");
    require(retried.pending_write_count == whole_write_sites
            && retried.staged_event_count == whole_write_sites
            && retried.generic_update_ack_count == whole_write_sites
            && retried.native_member_dispatches
                > failed.native_member_dispatches,
        "retry reexecutes the real generated member and reaches a complete Update ACK");
}

[[nodiscard]] FailureTrialResult run_failure_trial(
    const std::size_t allocation_cut,
    GenericWholeWriteFixture& compiled,
    GenericWholeWriteFixture& reference,
    const std::size_t trial)
{
    const auto value_kind = compiled.value_kind();
    const auto value = input_value(test_width, trial, value_kind);
    if (value_kind == runtime::simir::ValueKind::logic9) {
        constexpr std::array states {
            Logic9::u, Logic9::x, Logic9::zero, Logic9::one, Logic9::z,
            Logic9::w, Logic9::l, Logic9::h, Logic9::dont_care
        };
        std::array<bool, states.size()> observed_states { };
        for (std::size_t bit = 0U; bit < value.width(); ++bit) {
            for (std::size_t state = 0U; state < states.size(); ++state) {
                observed_states[state]
                    = observed_states[state]
                    || value.get_logic9(bit) == states[state];
            }
        }
        require(std::ranges::all_of(observed_states,
                    [](const bool observed) { return observed; }),
            "each wide Logic9 failure stimulus carries all nine values");
    }
    const auto before = compiled.snapshot();
    const auto before_reference = reference.snapshot();
    const auto suffix_input = compiled.checked_suffix_input_signal();
    const auto old_input = std::ranges::find(before.signals,
        compiled.input_signal(), &GenericSignalSnapshot::signal);
    const auto old_suffix_input = suffix_input
        ? std::ranges::find(before.signals, *suffix_input,
            &GenericSignalSnapshot::signal)
        : before.signals.end();
    const auto reference_suffix_input = reference.checked_suffix_input_signal();
    const auto old_reference_input = std::ranges::find(before_reference.signals,
        reference.input_signal(), &GenericSignalSnapshot::signal);
    const auto old_reference_suffix_input = reference_suffix_input
        ? std::ranges::find(before_reference.signals,
            *reference_suffix_input, &GenericSignalSnapshot::signal)
        : before_reference.signals.end();
    require(suffix_input.has_value()
            && reference_suffix_input.has_value()
            && old_input != before.signals.end()
            && old_suffix_input != before.signals.end()
            && old_reference_input != before_reference.signals.end()
            && old_reference_suffix_input != before_reference.signals.end()
            && old_input->current != value
            && old_suffix_input->current != value
            && old_reference_input->current != value
            && old_reference_suffix_input->current != value,
        "each Logic9 trial changes both independent input signals and wakes real work");
    const auto suffix_resumes_before = compiled.checked_suffix_resumes();
    const auto reference_suffix_resumes_before
        = reference.checked_suffix_resumes();
    ObserverContext observer { &compiled, { } };
    std::array<std::size_t, 2U> sizes_before_frontier { };
    std::uint64_t dispatches_before_frontier { };
    std::uint64_t runtime_dispatches_before_frontier { };
    bool arm_callback_ran { };
    bool arm_preceded_member { };
    bool post_entry_failure { };
    std::uint64_t dispatches_after_failure { };
    GenericFrontierProbe failed_attempt_probe;

    require(compiled.process_id() > 0U,
        "the armed Active callback sorts before the eligible member's scheduler key");
    allocation_failure::reset();
    compiled.schedule_input(value,
        static_cast<runtime::SimulationTick>(trial + 1U),
        0U, [&] {
            arm_callback_ran = true;
            sizes_before_frontier = compiled.pending_update_sizes();
            dispatches_before_frontier = compiled.native_member_dispatches();
            runtime_dispatches_before_frontier
                = compiled.probe_frontier_state().native_member_dispatches;
            arm_preceded_member
                = dispatches_before_frontier == before.native_member_dispatches;
            allocation_failure::set_observer(&observe_failure, &observer);
            allocation_failure::arm(allocation_cut);
        });
    reference.schedule_input(value,
        static_cast<runtime::SimulationTick>(trial + 1U));

    std::optional<runtime::RunResult> first_result;
    std::exception_ptr failure;
    bool threw_bad_alloc { };
    {
        ClearFailureOnExit clear_failure;
        try {
            first_result = compiled.run_to_pre_update_cut();
        } catch (const std::bad_alloc&) {
            threw_bad_alloc = true;
        } catch (...) {
            failure = std::current_exception();
        }
    }
    const bool injected = allocation_failure::injected();
    allocation_failure::clear_observer();
    allocation_failure::clear();
    if (failure) {
        std::rethrow_exception(failure);
    }
    require(!threw_bad_alloc || injected,
        "the run propagates bad_alloc only when the armed interposer injected it");
    require(arm_callback_ran && arm_preceded_member,
        "the failpoint arms before any generated member dispatch in the input wave");

    bool recovered_optional_injection { };
    if (injected && threw_bad_alloc) {
        require(observer.sample.observed,
            "a propagated injected allocation failure is observed at its allocation site");
        const auto after_failure = compiled.snapshot();
        require_output_state_unchanged(before, after_failure,
            compiled.checked_suffix_input_signal());
        failed_attempt_probe = observer.sample.frontier;
        const bool generated_entry_reached = observer.sample.observed
            && failed_attempt_probe.runtime_found
            && failed_attempt_probe.native_member_dispatches
                > runtime_dispatches_before_frontier
            && after_failure.native_member_dispatches
                > dispatches_before_frontier;
        post_entry_failure = generated_entry_reached
            && failed_attempt_probe.frontier_present
            && failed_attempt_probe.pending_write_count > 0U
            && failed_attempt_probe.pending_write_count
                == failed_attempt_probe.staged_event_count
            && failed_attempt_probe.generic_update_ack_count == 0U
            && after_failure.pending_update_sizes == sizes_before_frontier;
        dispatches_after_failure = after_failure.native_member_dispatches;
        if (post_entry_failure) {
            require_failure_frontier_probe(failed_attempt_probe,
                runtime_dispatches_before_frontier, sizes_before_frontier,
                compiled.process_id());
            if (value_kind == runtime::simir::ValueKind::logic9) {
                require_logic9_pending_writes(failed_attempt_probe, value,
                    compiled.output_signals(), compiled.process_id());
            }
            require(after_failure.pending_update_sizes
                        == sizes_before_frontier,
                "failed generated staging restores both ordinary Update vectors to their checkpoints");
            require(after_failure.pending_update_signals
                        == before.pending_update_signals
                    && after_failure.pending_update_values
                        == before.pending_update_values,
                "rollback removes every partial signal/value append and retains the prior pending payload");
            require(after_failure.frontier.generic_update_ack_count == 0U,
                "failure leaves the generated generic batch unacknowledged");
            require(after_failure.checked_suffix_resumes
                        == suffix_resumes_before,
                "the same-batch checked suffix does not run before the failed prefix is retried");
            const auto after_failure_probe = compiled.probe_frontier_state();
            require(after_failure_probe.runtime_found
                    && after_failure_probe.runtime_task_count == 0U
                    && after_failure_probe.runtime_task_cursor == 0U
                    && after_failure_probe.pending_write_count == 0U
                    && after_failure_probe.staged_event_count == 0U
                    && after_failure_probe.generic_update_ack_count == 0U,
                "the exception path clears the unacknowledged private frame after rolling host vectors back");
        }

        const auto retry = compiled.retry_frontier_attempt();
        require(retry.status == RunStatus::stopped,
            "the original offered prefix retries to the pre-Update stop after rollback");
        if (post_entry_failure) {
            const auto retried_probe = compiled.probe_frontier_state();
            require_same_retry_prefix(failed_attempt_probe, retried_probe);
            if (value_kind == runtime::simir::ValueKind::logic9) {
                require_logic9_pending_writes(retried_probe, value,
                    compiled.output_signals(), compiled.process_id());
            }
            require(compiled.native_member_dispatches()
                        > dispatches_after_failure,
                "the scheduler reoffers the failed task prefix to the generated entry after rollback");
        }
    } else if (injected) {
        recovered_optional_injection = true;
        require(observer.sample.observed,
            "an internally recovered allocation failure is observed at its allocation site");
        const auto& reservation_probe = observer.sample.frontier;
        require_failure_frontier_probe(reservation_probe,
            runtime_dispatches_before_frontier, sizes_before_frontier,
            compiled.process_id());
        require_logic9_pending_writes(reservation_probe, value,
            compiled.output_signals(), compiled.process_id());
        require(reservation_probe.pending_update_sizes
                    == sizes_before_frontier,
            "the optional Update-ticket reservation fails before appending any pending native row");

        require(first_result.has_value()
                && first_result->status == RunStatus::stopped,
            "the ordinary Update callback fallback reaches the pre-Update cut after the optional reservation failure");
        const auto successful_cut = compiled.snapshot();
        require(successful_cut.native_member_dispatches
                    == before.native_member_dispatches + 1U,
            "the recovered allocation cut completes exactly one native member dispatch");
        require_output_state_unchanged(before, successful_cut,
            compiled.checked_suffix_input_signal());
        const auto acknowledged_probe = compiled.probe_frontier_state();
        require(acknowledged_probe.runtime_found
                && !acknowledged_probe.frontier_present
                && acknowledged_probe.runtime_task_count == 1U
                && acknowledged_probe.runtime_task_cursor
                    == acknowledged_probe.runtime_task_count
                && acknowledged_probe.pending_write_count
                    == whole_write_sites
                && acknowledged_probe.staged_event_count
                    == whole_write_sites
                && acknowledged_probe.generic_update_ack_count
                    == whole_write_sites
                && acknowledged_probe.native_member_dispatches
                    == runtime_dispatches_before_frontier + 1U,
            "the recovered optional failure completes the exact generated write batch and host ACK");
        require_logic9_pending_writes(acknowledged_probe, value,
            compiled.output_signals(), compiled.process_id());
        require(successful_cut.frontier.found
                && successful_cut.frontier.generic_update_ack_count
                    == whole_write_sites,
            "the stopped native frame exposes the full Logic9 ACK before Update publication");
    } else {
        require(!threw_bad_alloc && first_result.has_value()
                && first_result->status == RunStatus::stopped,
            "a non-injected trial reaches the Active stop before deferred Update publication");
    }

    const auto reference_cut = reference.run_to_pre_update_cut();
    require(reference_cut.status == RunStatus::stopped,
        "the checked reference reaches the same pre-Update stop");
    const auto compiled_cut = compiled.snapshot();
    const auto checked_cut = reference.snapshot();
    require_output_state_unchanged(before, compiled_cut,
        compiled.checked_suffix_input_signal());
    require_output_state_unchanged(before_reference, checked_cut,
        reference.checked_suffix_input_signal());
    require(compiled.checked_suffix_input_signal().has_value()
            && reference.checked_suffix_input_signal().has_value(),
        "the typed checked suffix uses a separate input signal");
    require_scheduled_inputs(compiled_cut, compiled.input_signal(),
        *compiled.checked_suffix_input_signal(), value);
    require_scheduled_inputs(checked_cut, reference.input_signal(),
        *reference.checked_suffix_input_signal(), value);
    require(compiled_cut.signals == checked_cut.signals,
        "native retry and checked execution retain identical values and event metadata before Update");
    if (compiled_cut.pending_update_signals
            != checked_cut.pending_update_signals
        || compiled_cut.pending_update_values
            != checked_cut.pending_update_values) {
        std::fprintf(stderr,
            "generic diagnostic cut=%zu trial=%zu injected=%d threw=%d post=%d compiled_rows=%zu checked_rows=%zu compiled_dispatches=%llu checked_suffix=%zu\n",
            allocation_cut, trial, static_cast<int>(injected),
            static_cast<int>(threw_bad_alloc),
            static_cast<int>(post_entry_failure),
            compiled_cut.pending_update_signals.size(),
            checked_cut.pending_update_signals.size(),
            static_cast<unsigned long long>(compiled_cut.native_member_dispatches),
            compiled.checked_suffix_resumes());
        for (const auto signal : compiled_cut.pending_update_signals) {
            std::fprintf(stderr, " compiled_signal=%u", signal);
        }
        std::fprintf(stderr, "\n");
        for (const auto signal : checked_cut.pending_update_signals) {
            std::fprintf(stderr, " checked_signal=%u", signal);
        }
        std::fprintf(stderr, "\n");
    }
    require(compiled_cut.pending_update_signals
                == checked_cut.pending_update_signals
            && compiled_cut.pending_update_values
                == checked_cut.pending_update_values,
        "the deferred native and checked batches contain the same ordered signal/value rows");
    require(compiled_cut.pending_update_sizes[0U]
                == sizes_before_frontier[0U] + whole_write_sites + 1U
            && compiled_cut.pending_update_sizes[1U]
                == sizes_before_frontier[1U] + whole_write_sites + 1U,
        "the successful retry stages every native output plus one checked-suffix write");
    const auto expected_native_value = expected_output(value, value_kind);
    const auto pending_signal_start
        = before.pending_update_signals.size();
    const auto pending_value_start
        = before.pending_update_values.size();
    for (std::size_t write = 0U; write < whole_write_sites; ++write) {
        require(compiled_cut.pending_update_signals[pending_signal_start + write]
                    == compiled.output_signals()[write]
                && compiled_cut.pending_update_values[pending_value_start + write]
                    == expected_native_value,
            "the acknowledged retry appends typed native pending rows in write-site order");
    }
    require(compiled_cut.pending_update_signals[
                pending_signal_start + whole_write_sites]
                == *compiled.checked_suffix_signal()
            && compiled_cut.pending_update_values[
                pending_value_start + whole_write_sites] == value,
        "the checked suffix follows the native pending rows with the original typed input");
    require(compiled.checked_suffix_resumes()
                == suffix_resumes_before + 1U
            && reference.checked_suffix_resumes()
                == reference_suffix_resumes_before + 1U,
        "the checked suffix resumes exactly once across failure and retry");

    const auto published = compiled.resume_update_publication();
    const auto reference_published = reference.resume_update_publication();
    require((published.status == RunStatus::completed
                || published.status == RunStatus::time_limit)
            && (reference_published.status == RunStatus::completed
                || reference_published.status == RunStatus::time_limit),
        "both schedulers publish the queued Update after the stopped Active cut");
    const auto final = compiled.snapshot();
    const auto reference_final = reference.snapshot();
    require(final.signals == reference_final.signals,
        "post-failure publication matches checked current/LAST/stored/owner and event metadata");
    require_published_values(before, final, value, compiled.output_signals(),
        *compiled.checked_suffix_signal(), value_kind);
    require_published_values(before_reference, reference_final, value,
        reference.output_signals(), *reference.checked_suffix_signal(),
        value_kind);
    require(final.pending_update_sizes == before.pending_update_sizes
            && reference_final.pending_update_sizes
                == before_reference.pending_update_sizes,
        "successful Update publication retires every native and checked pending value");
    return { injected, post_entry_failure, recovered_optional_injection };
}

void run_optimization(const GenericFixtureMode mode,
    const runtime::simir::ValueKind value_kind)
{
    GenericWholeWriteFixture compiled {
        mode, test_width, whole_write_sites, true, false, true, value_kind };
    GenericWholeWriteFixture reference {
        GenericFixtureMode::interpreter, test_width, whole_write_sites, true,
        false, true, value_kind };
    compiled.start_and_settle();
    reference.start_and_settle();
    bool found_post_entry_failure { };
    std::size_t post_entry_failure_count { };
    std::size_t recovered_optional_injection_count { };
    std::optional<std::size_t> first_post_entry_cut;
    std::optional<std::size_t> terminal_non_injected_cut;
    std::size_t attempted_cuts { };
    for (std::size_t cut = 0U; cut < maximum_allocation_cut; ++cut) {
        const auto trial = run_failure_trial(
            cut, compiled, reference, cut);
        ++attempted_cuts;
        if (trial.post_entry_failure) {
            found_post_entry_failure = true;
            ++post_entry_failure_count;
            if (!first_post_entry_cut) {
                first_post_entry_cut = cut;
            }
        }
        if (trial.recovered_optional_injection) {
            ++recovered_optional_injection_count;
        }
        if (!trial.injected) {
            terminal_non_injected_cut = cut;
            break;
        }
    }
    require(found_post_entry_failure,
        "the deterministic allocation sweep reaches a post-entry generic batch before ACK and verifies rollback/reoffer");
    require(recovered_optional_injection_count > 0U,
        "the deterministic allocation sweep covers an optional Update-ticket reservation failure recovered by the ordinary callback path");
    require(post_entry_failure_count > 0U
            && first_post_entry_cut.has_value()
            && terminal_non_injected_cut.has_value()
            && *terminal_non_injected_cut > *first_post_entry_cut
            && attempted_cuts == *terminal_non_injected_cut + 1U,
        "the allocation sweep continues beyond its first post-entry failure through later cuts and ends on a non-injected trial");
}

void run_generic_frontier_logic9_failure_retry_impl()
{
#if defined(FSIM_HAS_LLVM)
    run_optimization(GenericFixtureMode::llvm_o0,
        runtime::simir::ValueKind::logic9);
    run_optimization(GenericFixtureMode::llvm_o2,
        runtime::simir::ValueKind::logic9);
#else
    throw std::runtime_error {
        "the generic native-frontier failure witness requires LLVM"
    };
#endif
}

} // namespace

void run_generic_frontier_logic9_failure_retry()
{
    run_generic_frontier_logic9_failure_retry_impl();
}

} // namespace fsim::tests::app::frontier
