// SPDX-License-Identifier: Apache-2.0
#include "simir_execution_context.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

void Interpreter::Impl::PureWaveScratch::clear() noexcept
{
    members.clear();
    task_ends.clear();
    task_snapshots.clear();
    copied_snapshot.clear();
    contexts.clear();
}

std::optional<std::size_t> Interpreter::Impl::try_execute_pure_wave(
    const std::span<const std::uint64_t> task_payloads,
    std::size_t& offered_tasks)
{
    offered_tasks = 0U;
    if (task_payloads.empty() || process_profile_enabled
        || !static_phase_batches_enabled || execution_point_hook) {
        return std::nullopt;
    }

    // Bound one admission attempt without changing the scheduler's queue or
    // the original pending cohort snapshots. A later task remains available
    // to its ordinary callback if the first incompatible task ends the span.
    constexpr std::size_t maximum_tasks = 256U;
    constexpr std::size_t maximum_members = 8192U;
    PureWaveScratch nested_scratch;
    const bool use_shared_scratch = !pure_wave_scratch_in_use;
    if (use_shared_scratch) {
        pure_wave_scratch_in_use = true;
    }
    auto& scratch = use_shared_scratch
        ? pure_wave_scratch : nested_scratch;
    scratch.clear();
    struct ClearScratch {
        PureWaveScratch& scratch;
        bool& in_use;
        bool borrowed;

        ~ClearScratch()
        {
            scratch.clear();
            if (borrowed) {
                in_use = false;
            }
        }
    } clear_scratch { scratch, pure_wave_scratch_in_use,
        use_shared_scratch };
    auto& members = scratch.members;
    auto& task_ends = scratch.task_ends;
    auto& task_snapshots = scratch.task_snapshots;
    auto& copied_snapshot = scratch.copied_snapshot;
    auto& contexts = scratch.contexts;
    std::optional<ProcessCohortNativeContext> native_context;
    const void* domain { };
    for (std::size_t task = 0U;
         task < task_payloads.size() && task_ends.size() < maximum_tasks;
         ++task) {
        const auto payload = task_payloads[task];
        // Leave a later installed graph kernel at the front of the next
        // offered task. The first task already had its own fused admission
        // attempt and may use this prepared-wave fallback if it declined.
        if (task != 0U && payload < fused_static_cohorts.size()) {
            const auto& fused = fused_static_cohorts[
                static_cast<std::size_t>(payload)];
            if (fused.certified && fused.executor) {
                break;
            }
        }
        if ((payload & native_static_region_payload) != 0U) {
            break;
        }
        copied_snapshot.clear();
        CohortSnapshotPool::Token snapshot;
        if ((payload & pure_wave_singleton_payload) != 0U) {
            const auto encoded = payload & ~pure_wave_singleton_payload;
            if (encoded > std::numeric_limits<ProcessId>::max()) {
                break;
            }
            copied_snapshot.push_back(static_cast<ProcessId>(encoded));
        } else {
            if (payload >= static_sensitivity_cohorts.size()) {
                break;
            }
            snapshot = static_sensitivity_cohorts[payload].pending;
            if (!cohort_snapshots.copy_pending(snapshot, copied_snapshot)) {
                break;
            }
        }
        if (copied_snapshot.empty()
            || copied_snapshot.size() > maximum_members - members.size()) {
            break;
        }
        bool admissible = true;
        const auto task_begin = members.size();
        std::optional<PureWavePreparedShape> task_shape;
        std::optional<std::pair<SignalId, SignalId>> task_and_inputs;
        for (const auto id : copied_snapshot) {
            if (id >= processes.size()) {
                admissible = false;
                break;
            }
            auto& state = processes[id];
            if (!state.queued || !state.waiting_on_static
                || state.status != ProcessStatus::waiting
                || state.suspended || state.halted || !state.executor
                || state.has_callable_frame_push
                || !state.pure_wave_operation_count_supported) {
                admissible = false;
                break;
            }
            if (!native_context) {
                contexts.emplace_back(*this, id);
                auto& context = contexts.front();
                native_context.emplace(ProcessCohortNativeContext {
                    this,
                    context.direct_signal_aval(),
                    context.direct_signal_bval(),
                    context.signal_writer_revision(),
                    context.supports_direct_word_updates(),
                    context.execution_points_enabled(),
                    context.direct_wide_signal_aval(),
                    context.direct_wide_signal_bval(),
                    context.direct_wide_signal_offsets(),
                    context.direct_signal_logic9_plane0(),
                    context.direct_signal_logic9_plane1(),
                    context.direct_signal_logic9_plane2(),
                    context.direct_signal_logic9_plane3(),
                    context.direct_wide_signal_logic9_plane2(),
                    context.direct_wide_signal_logic9_plane3(),
                });
                const PureWaveOwnerSignature signature {
                    {
                        native_context->owner,
                        native_context->signal_aval.data(),
                        native_context->signal_bval.data(),
                        native_context->wide_signal_aval.data(),
                        native_context->wide_signal_bval.data(),
                        native_context->wide_signal_offsets.data(),
                    },
                    {
                        native_context->signal_aval.size(),
                        native_context->signal_bval.size(),
                        native_context->wide_signal_aval.size(),
                        native_context->wide_signal_bval.size(),
                        native_context->wide_signal_offsets.size(),
                    },
                    native_context->signal_writer_revision,
                    native_context->supports_direct_word_updates,
                    native_context->execution_points_enabled,
                };
                if (!pure_wave_owner_signature) {
                    pure_wave_owner_signature = signature;
                } else if (*pure_wave_owner_signature != signature) {
                    if (pure_wave_owner_epoch
                        == std::numeric_limits<std::uint64_t>::max()) {
                        pure_wave_prepared_disabled = true;
                    } else {
                        ++pure_wave_owner_epoch;
                        pure_wave_owner_signature = signature;
                    }
                }
            }
            if (pure_wave_prepared_disabled) {
                admissible = false;
                break;
            }
            auto& slot = pure_wave_prepared_slots[id];
            const auto* prepared = slot.member;
            if (slot.executor != state.executor.get()
                || prepared == nullptr
                || !prepared->valid
                || prepared->generation != slot.generation
                || prepared->owner != native_context->owner
                || prepared->owner_epoch != pure_wave_owner_epoch) {
                const PureWaveResumeEntry entry {
                    id, state.executor.get(), state.pc,
                    &state.queued, &state.waiting_on_static,
                    &state.status,
                };
                prepared = state.executor->prepare_pure_wave_member(
                    entry, contexts.front(), *native_context,
                    pure_wave_owner_epoch);
                slot = { state.executor.get(), prepared,
                    prepared ? prepared->generation : 0U };
            }
            if (prepared == nullptr || !prepared->valid
                || prepared->process != id
                || prepared->executor != state.executor.get()
                || prepared->owner != native_context->owner
                || prepared->owner_epoch != pure_wave_owner_epoch
                || prepared->domain == nullptr
                || state.pc != prepared->resume_instruction
                || (domain != nullptr && domain != prepared->domain)) {
                admissible = false;
                break;
            }
            const auto shape = prepared->shape;
            if (shape > PureWavePreparedShape::wide_copy6
                || (task_shape && *task_shape != shape)
                || (shape != PureWavePreparedShape::logic4_bit_and
                    && copied_snapshot.size() != 1U)) {
                admissible = false;
                break;
            }
            const auto inputs = std::pair {
                prepared->and_lhs, prepared->and_rhs
            };
            if (shape == PureWavePreparedShape::logic4_bit_and
                && task_and_inputs && *task_and_inputs != inputs) {
                admissible = false;
                break;
            }
            task_shape = shape;
            task_and_inputs = inputs;
            domain = prepared->domain;
            members.push_back(prepared);
        }
        if (!admissible) {
            members.resize(task_begin);
            break;
        }
        task_ends.push_back(members.size());
        task_snapshots.push_back(snapshot);
    }
    if (task_ends.empty()) {
        return std::nullopt;
    }

    auto& first_context = contexts.front();

    // The executor may decline only before any mutation. Once called, an
    // exception is a fatal batch failure: retire every offered token and
    // clear its queued members rather than replaying a possibly executed
    // prefix through the generic scheduler path.
    offered_tasks = task_ends.size();
    try {
        const auto completion = members.front()->executor
            ->try_resume_prepared_pure_wave(
                members, task_ends, first_context, *native_context);
        if (!completion) {
            offered_tasks = 0U;
            return std::nullopt;
        }
        const auto completed = completion->completed_tasks;
        if (completed == 0U || completed > task_ends.size()) {
            throw std::logic_error(
                "pure wave executor returned an invalid task prefix");
        }
        offered_tasks = completed;
        if (!completion->updates_staged) {
            const auto member_end = task_ends[completed - 1U];
            for (std::size_t index = 0U; index < member_end; ++index) {
                const auto id = members[index]->process;
                ExecutionContext context { *this, id };
                members[index]->executor->flush_pure_wave_updates(context);
            }
        }
        std::size_t member_begin { };
        for (std::size_t task = 0U; task < completed; ++task) {
            const auto member_end = task_ends[task];
            if (task_snapshots[task]) {
                const auto cohort = static_cast<std::size_t>(
                    task_payloads[task]);
                auto& source = static_sensitivity_cohorts[cohort];
                cohort_snapshots.release(task_snapshots[task]);
                source.pending = { };
            }
            if (native_phase_profile_enabled) {
                if (member_end - member_begin == 1U) {
                    ++native_phase_profile_single_resumes;
                } else {
                    ++native_phase_profile_cohort_resumes;
                    native_phase_profile_cohort_members
                        += member_end - member_begin;
                }
            }
            for (auto index = member_begin; index < member_end; ++index) {
                const auto id = members[index]->process;
                auto& state = processes[id];
                if (native_process_count_profile_enabled) {
                    if (id >= native_process_resume_counts.size()) {
                        const auto size = static_cast<std::size_t>(id) + 1U;
                        native_process_resume_counts.resize(size);
                        native_process_single_resume_counts.resize(size);
                        native_process_single_static_wait_counts.resize(size);
                        native_process_cohort_resume_counts.resize(size);
                        native_process_cohort_static_wait_counts.resize(size);
                        native_process_word_fanout_ready_counts.resize(size);
                    }
                    ++native_process_resume_counts[id];
                    if (member_end - member_begin == 1U) {
                        ++native_process_single_resume_counts[id];
                        ++native_process_single_boundary_counts[
                            external_suspension_profile_index(
                                ExternalSuspendKind::validated_wait_sensitivity)];
                        ++native_process_single_static_wait_counts[id];
                    } else {
                        ++native_process_cohort_resume_counts[id];
                        ++native_process_cohort_boundary_counts[
                            external_suspension_profile_index(
                                ExternalSuspendKind::validated_wait_sensitivity)];
                        ++native_process_cohort_static_wait_counts[id];
                    }
                }
                state.pc = members[index]->resume_instruction;
                clear_wait_timeout(state);
                state.status = ProcessStatus::waiting;
                state.waiting_on_static = true;
                state.static_trigger_mask = 0U;
            }
            member_begin = member_end;
        }
        return completed;
    } catch (...) {
        const auto member_end = task_ends[offered_tasks - 1U];
        for (std::size_t task = 0U; task < offered_tasks; ++task) {
            if (!task_snapshots[task]) {
                continue;
            }
            const auto cohort = static_cast<std::size_t>(
                task_payloads[task]);
            auto& source = static_sensitivity_cohorts[cohort];
            cohort_snapshots.release(task_snapshots[task]);
            source.pending = { };
        }
        for (std::size_t index = 0U; index < member_end; ++index) {
            processes[members[index]->process].queued = false;
        }
        throw;
    }
}

} // namespace fsim::runtime::simir
