// SPDX-License-Identifier: Apache-2.0
#include "native_frontier_generic_update_failure_interposer.hpp"
#include "native_frontier_generic_update_logic9_failure_test.hpp"
#include "native_frontier_generic_update_test_support.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
    const std::uint32_t width, const std::size_t trial)
{
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

struct ObserverContext final {
    GenericWholeWriteFixture* fixture { };
    FailureSample sample;
};

struct FailureTrialResult final {
    bool injected { };
    bool post_entry_failure { };
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
        "the checked suffix has a distinct stimulus signal");
    const auto root = std::ranges::find(snapshot.signals, root_input,
        &GenericSignalSnapshot::signal);
    const auto suffix = std::ranges::find(snapshot.signals,
        checked_suffix_input, &GenericSignalSnapshot::signal);
    require(root != snapshot.signals.end()
            && suffix != snapshot.signals.end()
            && root->current == value
            && suffix->current == value,
        "both independent stimulus signals publish the same value before their shared Active batch");
}

void require_published_values(
    const GenericFixtureSnapshot& before,
    const GenericFixtureSnapshot& snapshot,
    const PackedLogic4& input,
    const std::span<const runtime::simir::SignalId> outputs,
    const runtime::simir::SignalId checked_suffix)
{
    const auto expected = inverse(input);
    for (const auto signal : outputs) {
        const auto found = std::ranges::find(snapshot.signals, signal,
            &GenericSignalSnapshot::signal);
        const auto original = std::ranges::find(before.signals, signal,
            &GenericSignalSnapshot::signal);
        require(found != snapshot.signals.end()
                && original != before.signals.end()
                && found->current == expected
                && found->stored == expected
                && found->raw_driver == original->raw_driver,
            "retry publication writes each full-width unresolved native output without changing its raw driver record");
    }
    const auto suffix = std::ranges::find(snapshot.signals, checked_suffix,
        &GenericSignalSnapshot::signal);
    const auto original_suffix = std::ranges::find(before.signals,
        checked_suffix, &GenericSignalSnapshot::signal);
    require(suffix != snapshot.signals.end()
            && original_suffix != before.signals.end()
            && suffix->current == input
            && suffix->stored == input
            && suffix->raw_driver == original_suffix->raw_driver,
        "the checked suffix publishes its unresolved full-width input value exactly once");
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
        "the injected failure is observed inside a borrowed generic frontier with its V2 runtime present");
    require(!probe.frontier_tasks_truncated
            && probe.frontier_task_count <= generic_frontier_probe_task_capacity
            && probe.frontier_cursor == 0U
            && probe.frontier_end == probe.frontier_task_count,
        "the no-allocation observer captures the complete scheduler-authored frontier task span");
    require(probe.frontier_process_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && probe.frontier_phase == runtime::SchedulerPhase::active
            && probe.frontier_systemverilog_round == 0U
            && probe.frontier_task_count == 1U
            && probe.frontier_tasks[0U].stable_order == expected_process,
        "the native callback borrows only the root task's Generic Active round-zero key");
    require(probe.runtime_generation == probe.frontier_generation
            && probe.runtime_time == probe.frontier_time
            && probe.runtime_delta == probe.frontier_delta
            && probe.runtime_process_domain
                == runtime::simir::ProcessSchedulingDomain::generic
            && probe.runtime_phase == runtime::SchedulerPhase::active
            && probe.runtime_systemverilog_round == 0U,
        "the generated frame is bound to the exact borrowed Generic Active key slot");
    require(probe.runtime_task_count > 0U
            && probe.runtime_task_cursor == probe.runtime_task_count
            && !probe.runtime_original_tasks_truncated
            && probe.runtime_original_task_count == probe.runtime_task_count
            && probe.runtime_task_count == probe.frontier_task_count
            && probe.runtime_task_count == 1U,
        "the generated entry consumed the complete one-task root offer with its retained exact key");
    require(probe.pending_write_count == whole_write_sites
            && probe.pending_write_count == probe.staged_event_count
            && probe.generic_update_ack_count == 0U,
        "the failure occurs after all native outputs are staged and before the host ACK");
    require(probe.native_member_dispatches > runtime_dispatches_before,
        "the generated member body actually ran before the injected host-staging failure");
    for (std::size_t index = 0U;
         index < probe.pending_update_sizes.size(); ++index) {
        require(probe.pending_update_sizes[index] >= pending_sizes_before[index]
                && probe.pending_update_sizes[index]
                    <= pending_sizes_before[index] + whole_write_sites,
            "the observer sees only the bounded native-output append prefix at the failure point");
    }

    std::size_t matching_starts { };
    for (std::size_t start = 0U;
         start <= probe.frontier_task_count - probe.runtime_task_count;
         ++start) {
        bool matches = true;
        for (std::size_t index = 0U;
             index < probe.runtime_task_count; ++index) {
            if (!same_task_key(probe.runtime_original_tasks[index],
                    probe.frontier_tasks[start + index])) {
                matches = false;
                break;
            }
        }
        if (matches) {
            ++matching_starts;
        }
    }
    require(matching_starts == 1U,
        "the retained native prefix keys are an exact unique contiguous span of the borrowed scheduler tasks");
    require(probe.runtime_original_tasks[0U].stable_order == expected_process,
        "the native prefix begins with the fixture's exact process-ordered task");
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
    const auto value = input_value(test_width, trial);
    const auto before = compiled.snapshot();
    const auto before_reference = reference.snapshot();
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

    if (injected && threw_bad_alloc) {
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
                "the separately scheduled checked suffix does not run before the failed root prefix is retried");
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
            require(compiled.native_member_dispatches()
                        > dispatches_after_failure,
                "the scheduler reoffers the failed task prefix to the generated entry after rollback");
        }
    } else {
        require(!threw_bad_alloc,
            "a propagated allocation failure cannot bypass the scheduler retry witness");
        require(first_result.has_value()
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
        "the foreign checked suffix has its own scheduled input");
    require_scheduled_inputs(compiled_cut, compiled.input_signal(),
        *compiled.checked_suffix_input_signal(), value);
    require_scheduled_inputs(checked_cut, reference.input_signal(),
        *reference.checked_suffix_input_signal(), value);
    require(compiled_cut.signals == checked_cut.signals,
        "native retry and checked execution retain identical values and event metadata before Update");
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
        *compiled.checked_suffix_signal());
    require_published_values(before_reference, reference_final, value,
        reference.output_signals(), *reference.checked_suffix_signal());
    require(final.pending_update_sizes == before.pending_update_sizes
            && reference_final.pending_update_sizes
                == before_reference.pending_update_sizes,
        "successful Update publication retires every native and checked pending value");
    return { injected, post_entry_failure };
}

void run_optimization(const GenericFixtureMode mode)
{
    GenericWholeWriteFixture compiled {
        mode, test_width, whole_write_sites, true, false, true };
    GenericWholeWriteFixture reference {
        GenericFixtureMode::interpreter, test_width, whole_write_sites,
        true, false, true };
    compiled.start_and_settle();
    reference.start_and_settle();
    bool found_post_entry_failure { };
    std::size_t post_entry_failure_count { };
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
        if (!trial.injected) {
            terminal_non_injected_cut = cut;
            break;
        }
    }
    require(found_post_entry_failure,
        "the deterministic allocation sweep reaches a post-entry generic batch before ACK and verifies rollback/reoffer");
    require(post_entry_failure_count > 0U
            && first_post_entry_cut.has_value()
            && terminal_non_injected_cut.has_value()
            && *terminal_non_injected_cut > *first_post_entry_cut
            && attempted_cuts == *terminal_non_injected_cut + 1U,
        "the allocation sweep continues beyond its first post-entry failure through later cuts and ends on a non-injected trial");
}

struct SharedInputTrialResult final {
    bool injected { };
    bool legacy_frontier_failure { };
    bool post_backend_staging_failure { };
    bool pre_frontier_failure { };
    bool optional_update_reservation_fallback { };
};

[[nodiscard]] SharedInputTrialResult run_shared_input_failure_trial(
    const std::size_t allocation_cut,
    const std::size_t trial,
    GenericWholeWriteFixture& compiled,
    GenericWholeWriteFixture& reference)
{
    const auto value = input_value(test_width, trial);
    const auto time = static_cast<runtime::SimulationTick>(trial + 1U);
    const auto before = compiled.snapshot();
    const auto before_reference = reference.snapshot();
    const auto suffix_resumes_before = compiled.checked_suffix_resumes();
    const auto reference_suffix_resumes_before
        = reference.checked_suffix_resumes();
    const auto pending_sizes_before = compiled.pending_update_sizes();
    const auto generic_attempts_before = before.generic_projected_region_attempts;
    const auto generic_backend_runs_before
        = before.generic_projected_region_backend_runs;
    const auto generic_completions_before
        = before.generic_projected_region_completions;
    const auto generic_failures_before
        = before.generic_projected_region_failures;
    const auto generic_declines_before
        = before.generic_projected_region_declines;
    ObserverContext observer { &compiled, { } };
    bool arm_callback_ran { };
    bool arm_preceded_member { };

    require(!compiled.checked_suffix_input_signal().has_value(),
        "the shared-input sweep keeps the root and checked suffix on one stimulus");
    require(compiled.process_id() > 0U,
        "the root task follows the fixture's inert process-order anchor");
    const auto old_input = std::ranges::find(before.signals,
        compiled.input_signal(), &GenericSignalSnapshot::signal);
    require(old_input != before.signals.end() && old_input->current != value,
        "each shared-input sweep wave publishes an edge that wakes the root");

    allocation_failure::reset();
    compiled.schedule_input(value, time, 0U, [&] {
        arm_callback_ran = true;
        arm_preceded_member = compiled.native_member_dispatches()
            == before.native_member_dispatches;
        allocation_failure::set_observer(&observe_failure, &observer);
        allocation_failure::arm(allocation_cut);
    });
    reference.schedule_input(value, time);

    std::optional<runtime::RunResult> first_result;
    std::exception_ptr other_failure;
    bool threw_bad_alloc { };
    {
        ClearFailureOnExit clear_failure;
        try {
            first_result = compiled.run_to_pre_update_cut();
        } catch (const std::bad_alloc&) {
            threw_bad_alloc = true;
        } catch (...) {
            other_failure = std::current_exception();
        }
    }
    const bool injected = allocation_failure::injected();
    allocation_failure::clear_observer();
    allocation_failure::clear();
    if (other_failure) {
        std::rethrow_exception(other_failure);
    }
    require(arm_callback_ran && arm_preceded_member,
        "the allocation cut arms before any member body in the scheduled wave");
    require(!threw_bad_alloc || injected,
        "only an interposer-injected allocation may propagate as bad_alloc");

    bool legacy_frontier_failure { };
    bool post_backend_staging_failure { };
    bool pre_frontier_failure { };
    bool optional_update_reservation_fallback { };
    if (injected && threw_bad_alloc) {
        require(threw_bad_alloc && observer.sample.observed,
            "every injected allocation is propagated and observed, never converted to decline");
        const auto after_failure = compiled.snapshot();
        require_output_state_unchanged(before, after_failure, std::nullopt);
        require(after_failure.pending_update_sizes == pending_sizes_before
                && after_failure.pending_update_signals
                    == before.pending_update_signals
                && after_failure.pending_update_values
                    == before.pending_update_values,
            "an injected failure leaves output roles and pending Update rows unchanged before retry");
        require(after_failure.checked_suffix_resumes == suffix_resumes_before
                && after_failure.native_member_dispatches
                    == before.native_member_dispatches,
            "neither checked suffix work nor generated-frontier dispatch occurs before the failed root is retried");
        require(after_failure.generic_projected_region_completions
                    == generic_completions_before
                && after_failure.generic_projected_region_declines
                    == generic_declines_before,
            "the failed shared-input attempt neither completes a backend nor falls back as a decline");

        const auto& probe = observer.sample.frontier;
        if (probe.frontier_present) {
            constexpr std::uint64_t generic_projected_region_tag
                = std::uint64_t { 1U } << 60U;
            // Process 0 registers its inert anchor cohort first; root and
            // suffix share the next static sensitivity cohort.
            constexpr std::uint64_t shared_input_static_cohort_id { 1U };
            const auto root_process = compiled.process_id();
            const auto checked_suffix_process
                = static_cast<ProcessId>(root_process + 1U);
            require(!probe.runtime_found
                    && !probe.frontier_tasks_truncated
                    && probe.frontier_cursor == 0U
                    && probe.frontier_end
                        == probe.frontier_task_count
                    && probe.frontier_task_count == 2U
                    && probe.frontier_tasks[0U].stable_order
                        == root_process
                    && probe.frontier_tasks[0U].payload
                        == (generic_projected_region_tag
                            | static_cast<std::uint64_t>(root_process))
                    && probe.frontier_tasks[1U].stable_order
                        == static_cast<runtime::StableOrder>(
                            checked_suffix_process)
                    && probe.frontier_tasks[1U].payload
                        == shared_input_static_cohort_id
                    && probe.frontier_tasks[0U].sequence
                        < probe.frontier_tasks[1U].sequence
                    && probe.frontier_process_domain
                        == runtime::simir::ProcessSchedulingDomain::generic
                    && probe.frontier_phase
                        == runtime::SchedulerPhase::active
                    && probe.frontier_systemverilog_round == 0U,
                "the V1-only provider keeps the exact ordered root and shared-input suffix keys without an installed V2 runtime");
            require(probe.runtime_task_count == 0U
                    && probe.runtime_task_cursor == 0U
                    && probe.runtime_original_task_count == 0U
                    && probe.pending_write_count == 0U
                    && probe.staged_event_count == 0U
                    && probe.generic_update_ack_count == 0U
                    && probe.native_member_dispatches
                        == before.native_member_dispatches,
                "the installed Generic V2 runtime has not entered a frame or dispatched a member before the legacy backend failure");
            require(after_failure.generic_projected_region_attempts
                        == generic_attempts_before + 1U
                    && after_failure.generic_projected_region_failures
                        == generic_failures_before + 1U,
                "a failure inside the Generic callback increments the backend-failure path exactly once");
            require(after_failure.generic_projected_region_backend_runs
                        >= generic_backend_runs_before
                    && after_failure.generic_projected_region_backend_runs
                        <= generic_backend_runs_before + 1U,
                "an injected exception may happen before the legacy backend body or after it while pending rows are still private");
            legacy_frontier_failure = true;
            post_backend_staging_failure
                = after_failure.generic_projected_region_backend_runs
                    == generic_backend_runs_before + 1U;
        } else {
            require(after_failure.generic_projected_region_attempts
                        == generic_attempts_before
                    && after_failure.generic_projected_region_failures
                        == generic_failures_before
                    && after_failure.generic_projected_region_backend_runs
                        == generic_backend_runs_before,
                "a scheduler failure before a borrowed frontier does not claim a Generic backend failure");
            pre_frontier_failure = true;
        }

        const auto retried = compiled.retry_frontier_attempt();
        require(retried.status == RunStatus::stopped,
            "the scheduler retries the retained work after the injected failure");
        const auto after_retry = compiled.snapshot();
        require(after_retry.native_member_dispatches
                    == before.native_member_dispatches
                && after_retry.generic_projected_region_backend_runs
                    == after_failure.generic_projected_region_backend_runs + 1U
                && after_retry.generic_projected_region_completions
                    == generic_completions_before + 1U
                && after_retry.generic_projected_region_failures
                    == generic_failures_before
                        + (legacy_frontier_failure ? 1U : 0U)
                && after_retry.generic_projected_region_declines
                    == generic_declines_before,
            "retry completes the checked compiled backend once, preserving failure classification and avoiding native dispatch or decline");
    } else if (injected) {
        require(observer.sample.observed,
            "a swallowed injected allocation is still captured by the allocation observer");
        require(!observer.sample.frontier.runtime_found,
            "the optional reservation fallback belongs to the V1-only provider path without a V2 runtime");
        require(first_result.has_value()
                && first_result->status == RunStatus::stopped,
            "the optional Update-ticket reservation fallback completes the Active wave and reaches the pre-Update cut");
        const auto completed = compiled.snapshot();
        require(completed.native_member_dispatches
                    == before.native_member_dispatches
                && completed.generic_projected_region_attempts
                    == generic_attempts_before + 1U
                && completed.generic_projected_region_backend_runs
                    == generic_backend_runs_before + 1U
                && completed.generic_projected_region_completions
                    == generic_completions_before + 1U
                && completed.generic_projected_region_failures
                    == generic_failures_before
                && completed.generic_projected_region_declines
                    == generic_declines_before
                && completed.checked_suffix_resumes
                    == suffix_resumes_before + 1U,
            "the optional reservation fallback completes root and suffix once without failure, decline, or generated-frontier dispatch");
        require(completed.pending_update_sizes[0U]
                    == pending_sizes_before[0U] + whole_write_sites + 1U
                && completed.pending_update_sizes[1U]
                    == pending_sizes_before[1U] + whole_write_sites + 1U,
            "the completed optional fallback retains exactly the ordered root and checked-suffix Update rows");
        optional_update_reservation_fallback = true;
    } else {
        require(!threw_bad_alloc && first_result.has_value()
                && first_result->status == RunStatus::stopped,
            "the first non-injected cut completes the ordinary Active wave");
        const auto completed = compiled.snapshot();
        require(completed.native_member_dispatches
                    == before.native_member_dispatches
                && completed.generic_projected_region_attempts
                    == generic_attempts_before + 1U
                && completed.generic_projected_region_backend_runs
                    == generic_backend_runs_before + 1U
                && completed.generic_projected_region_completions
                    == generic_completions_before + 1U
                && completed.generic_projected_region_failures
                    == generic_failures_before
                && completed.generic_projected_region_declines
                    == generic_declines_before,
            "the terminal trial completes one checked-backend invocation with no injected failure, decline, or generated-frontier dispatch");
    }

    const auto reference_cut = reference.run_to_pre_update_cut();
    require(reference_cut.status == RunStatus::stopped,
        "the checked reference reaches the matching pre-Update cut");
    const auto compiled_cut = compiled.snapshot();
    const auto checked_cut = reference.snapshot();
    require_output_state_unchanged(before, compiled_cut, std::nullopt);
    require_output_state_unchanged(before_reference, checked_cut, std::nullopt);
    const auto compiled_input = std::ranges::find(compiled_cut.signals,
        compiled.input_signal(), &GenericSignalSnapshot::signal);
    const auto checked_input = std::ranges::find(checked_cut.signals,
        reference.input_signal(), &GenericSignalSnapshot::signal);
    require(compiled_input != compiled_cut.signals.end()
            && checked_input != checked_cut.signals.end()
            && compiled_input->current == value
            && checked_input->current == value,
        "both executions publish the same changed input before their root and checked-suffix work");
    require(compiled_cut.signals == checked_cut.signals
            && compiled_cut.pending_update_signals
                == checked_cut.pending_update_signals
            && compiled_cut.pending_update_values
                == checked_cut.pending_update_values,
        "shared-input retry matches checked role values, event stamps, and ordered deferred writes");
    require(compiled_cut.pending_update_sizes[0U]
                == pending_sizes_before[0U] + whole_write_sites + 1U
            && compiled_cut.pending_update_sizes[1U]
                == pending_sizes_before[1U] + whole_write_sites + 1U
            && compiled.checked_suffix_resumes()
                == suffix_resumes_before + 1U
            && reference.checked_suffix_resumes()
                == reference_suffix_resumes_before + 1U,
        "the root and checked suffix each stage exactly one ordered result in the same-input wave");

    const auto published = compiled.resume_update_publication();
    const auto reference_published = reference.resume_update_publication();
    require((published.status == RunStatus::completed
                || published.status == RunStatus::time_limit)
            && (reference_published.status == RunStatus::completed
                || reference_published.status == RunStatus::time_limit),
        "both shared-input executions publish the deferred Update rows");
    const auto final = compiled.snapshot();
    const auto reference_final = reference.snapshot();
    require(final.signals == reference_final.signals,
        "post-retry current/LAST/stored/owner values and event metadata match the interpreter");
    require_published_values(before, final, value, compiled.output_signals(),
        *compiled.checked_suffix_signal());
    require_published_values(before_reference, reference_final, value,
        reference.output_signals(), *reference.checked_suffix_signal());
    require(final.pending_update_sizes == before.pending_update_sizes
            && reference_final.pending_update_sizes
                == before_reference.pending_update_sizes,
        "the successful Update leaves no pending native or checked rows");
    return { injected, legacy_frontier_failure,
        post_backend_staging_failure, pre_frontier_failure,
        optional_update_reservation_fallback };
}

void run_shared_input_legacy_backend_failure(const GenericFixtureMode mode)
{
    GenericFixturePolicy legacy_provider_policy;
    legacy_provider_policy.v1_only_region_backend_provider = true;
    GenericWholeWriteFixture compiled {
        mode, test_width, whole_write_sites, true, false, false,
        runtime::simir::ValueKind::logic4, legacy_provider_policy };
    GenericWholeWriteFixture reference {
        GenericFixtureMode::interpreter,
        test_width, whole_write_sites, true, false, false };
    compiled.start_and_settle();
    reference.start_and_settle();
    require(!compiled.snapshot().frontier.found,
        "the shared-input legacy sweep creates no V2 frontier runtime");
    bool found_legacy_frontier_failure { };
    bool found_post_backend_staging_failure { };
    bool found_pre_frontier_failure { };
    std::size_t optional_update_reservation_fallbacks { };
    std::size_t injected_trials { };
    std::optional<std::size_t> first_failure_cut;
    std::optional<std::size_t> terminal_non_injected_cut;
    std::size_t attempted_cuts { };
    for (std::size_t cut = 0U; cut < maximum_allocation_cut; ++cut) {
        const auto trial = run_shared_input_failure_trial(
            cut, cut, compiled, reference);
        ++attempted_cuts;
        if (trial.injected) {
            ++injected_trials;
            if (!first_failure_cut) {
                first_failure_cut = cut;
            }
            found_legacy_frontier_failure
                = found_legacy_frontier_failure
                    || trial.legacy_frontier_failure;
            found_post_backend_staging_failure
                = found_post_backend_staging_failure
                    || trial.post_backend_staging_failure;
            found_pre_frontier_failure
                = found_pre_frontier_failure || trial.pre_frontier_failure;
            optional_update_reservation_fallbacks
                += trial.optional_update_reservation_fallback ? 1U : 0U;
        } else {
            terminal_non_injected_cut = cut;
            break;
        }
    }
    require(found_legacy_frontier_failure,
        "the shared-input sweep injects inside the borrowed Generic callback and observes the legacy backend failure channel");
    require(found_post_backend_staging_failure,
        "the shared-input sweep reaches an injected allocation after the legacy backend body ran but before its completion/publication");
    require(found_pre_frontier_failure,
        "the shared-input sweep also covers allocation failure before any frontier is borrowed");
    require(optional_update_reservation_fallbacks > 0U,
        "the shared-input sweep separately observes a caught optional Update-ticket reservation failure that still completes the wave");
    require(injected_trials > 1U,
        "the shared-input sweep exercises multiple distinct injected allocation cuts rather than one pinned offset");
    require(first_failure_cut.has_value()
            && terminal_non_injected_cut.has_value()
            && *terminal_non_injected_cut > *first_failure_cut
            && attempted_cuts == *terminal_non_injected_cut + 1U,
        "the shared-input allocation sweep continues through every injected cut to a terminal non-injected trial");
}

void run_generic_frontier_failure_retry()
{
#if defined(FSIM_HAS_LLVM)
    run_optimization(GenericFixtureMode::llvm_o0);
    run_optimization(GenericFixtureMode::llvm_o2);
    run_shared_input_legacy_backend_failure(GenericFixtureMode::llvm_o0);
    run_shared_input_legacy_backend_failure(GenericFixtureMode::llvm_o2);
#else
    throw std::runtime_error {
        "the generic native-frontier failure witness requires LLVM"
    };
#endif
}

} // namespace

} // namespace fsim::tests::app::frontier

int main()
{
    try {
        fsim::tests::app::frontier::run_generic_frontier_failure_retry();
        fsim::tests::app::frontier::run_generic_frontier_logic9_failure_retry();
    } catch (const std::exception& error) {
        std::cerr << "generic frontier failure test failure: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
