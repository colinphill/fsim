// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

namespace {

constexpr std::uint64_t kGenericProjectedRegionPayload = UINT64_C(1) << 60U;
constexpr std::uint32_t kInvalidFrontierIndex = UINT32_MAX;

[[nodiscard]] bool same_generic_task(
    const SchedulerBatchFrontierEntry& left,
    const SchedulerBatchFrontierEntry& right) noexcept
{
    return left.stable_order == right.stable_order
        && left.sequence == right.sequence && left.payload == right.payload;
}

[[nodiscard]] RegionFrontierKeyV2 generic_frontier_key(
    const SchedulerGenericBatchFrontier& frontier,
    const SchedulerBatchFrontierEntry& task) noexcept
{
    return {
        frontier.time,
        frontier.delta,
        0U,
        task.stable_order,
        task.sequence,
        static_cast<std::uint32_t>(ProcessSchedulingDomain::generic),
        static_cast<std::uint32_t>(SchedulerPhase::active),
    };
}

[[nodiscard]] std::optional<std::uint32_t> generic_member_for_process(
    const RegionFrontierLayoutV2& layout, const ProcessId process) noexcept
{
    if (layout.members == nullptr) {
        return std::nullopt;
    }
    std::optional<std::uint32_t> result;
    for (std::uint32_t index = 0U; index < layout.member_count; ++index) {
        if (layout.members[index].process_id != process) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }
        result = index;
    }
    return result;
}

[[nodiscard]] bool generic_task_process(
    const std::uint64_t payload, ProcessId& process) noexcept
{
    if ((payload & kGenericProjectedRegionPayload) == 0U) {
        return false;
    }
    const auto decoded = payload & ~kGenericProjectedRegionPayload;
    if (decoded > std::numeric_limits<ProcessId>::max()) {
        return false;
    }
    process = static_cast<ProcessId>(decoded);
    return true;
}

template<typename Runtime>
void clear_generic_frame_step(Runtime& runtime) noexcept
{
    runtime.frame.scheduler_task_count = 0U;
    runtime.frame.scheduler_task_cursor = 0U;
    runtime.frame.pending_write_count = 0U;
    runtime.frame.staged_event_count = 0U;
    runtime.frame.committed_signal_count = 0U;
    runtime.frame.generic_update_ack_count = 0U;
    runtime.frame.current_member = kInvalidFrontierIndex;
    runtime.frame.current_pending_write = kInvalidFrontierIndex;
    runtime.frame.current_commit_changed = 0U;
}

} // namespace

std::optional<SchedulerBatchResult>
Interpreter::Impl::execute_generic_region_frontier_prefix(
    RegionFrontierComponentRuntime& runtime,
    const SchedulerGenericBatchFrontier& frontier,
    const std::span<const std::uint64_t> payloads,
    const std::size_t frontier_offset,
    const std::size_t task_prefix_count,
    const bool require_compact_receipts)
{
    if (payloads.empty() || task_prefix_count == 0U
        || task_prefix_count > payloads.size()
        || task_prefix_count
            > RegionFrontierComponentRuntime::scheduler_task_capacity
        || runtime.owner != this || runtime.invalidated
        || runtime.execution_mode
            != RegionFrontierExecutionModeV2::generic_deferred_update
        || !runtime.frame_initialized || !runtime.backend
        || !runtime.backend->executor || !region_graph
        || runtime.component >= region_frontier_runtime_by_component.size()
        || region_frontier_runtime_by_component[runtime.component].get()
            != &runtime
        || runtime.runtime_generation == 0U
        || runtime.runtime_generation != region_runtime_generation
        || !region_graph->component_epochs_current(runtime.component)
        || (require_compact_receipts && scheduler.trace_hook_installed())
        || cohort_overflow_scratch_in_use
        || !runtime.try_enter()) {
        return std::nullopt;
    }
    struct LeaveRuntime final {
        RegionFrontierComponentRuntime& runtime;
        ~LeaveRuntime() { runtime.leave(); }
    } leave_runtime { runtime };

    const auto current = scheduler.current_generic_batch_frontier();
    const auto& layout = runtime.backend->executor->layout();
    auto& frame = runtime.frame;
    const auto component_id = static_cast<std::uint64_t>(runtime.component);
    const bool compact_component_id_valid
        = static_cast<std::size_t>(component_id) == runtime.component
        && component_id != std::numeric_limits<std::uint64_t>::max();
    const auto expected_compact_group = SchedulerBatchGroupKey {
        runtime.runtime_generation,
        component_id + 1U,
    };
    if (!current || current->generation == 0U
        || current->generation != frontier.generation
        || current->time != frontier.time || current->delta != frontier.delta
        || current->phase != frontier.phase
        || current->cursor != frontier.cursor || current->end != frontier.end
        || current->tasks.size() != frontier.tasks.size()
        || (require_compact_receipts
            && (!compact_component_id_valid
                || !expected_compact_group
                || frontier.compact_group_key != expected_compact_group
                || current->compact_group_key
                    != frontier.compact_group_key
                || current->ticket_member_offset
                    != frontier.ticket_member_offset
                || current->ticket_members.data()
                    != frontier.ticket_members.data()
                || current->ticket_members.size()
                    != frontier.ticket_members.size()
                || frontier.ticket_members.empty()
                || frontier.ticket_members.size() > layout.member_count
                || frontier.ticket_member_offset
                    > std::numeric_limits<std::size_t>::max()
                        - frontier.ticket_members.size()
                || frontier.ticket_member_offset
                        + frontier.ticket_members.size()
                    > layout.member_count
                || frontier.ticket_members.size() < frontier.tasks.size()))
        || frontier.generation == 0U
        || frontier.phase != SchedulerPhase::active
        || frontier.cursor != 0U || frontier.end != frontier.tasks.size()
        || frontier_offset > frontier.tasks.size()
        || payloads.size() > frontier.tasks.size() - frontier_offset
        || task_prefix_count > frontier.tasks.size() - frontier_offset
        || scheduler.current_phase() != SchedulerPhase::active
        || scheduler.now() != frontier.time || scheduler.delta() != frontier.delta
        || layout.execution_mode
            != RegionFrontierExecutionModeV2::generic_deferred_update
        || !region_frontier_layout_header_valid_v2(layout)
        || !region_frontier_frame_header_valid_v2(frame)) {
        return std::nullopt;
    }
    if (frame.generic_update_ack_count != 0U) {
        // One V2 Generic prefix is published per borrowed scheduler callback.
        // If the outer owner asks again for a disjoint suffix of this same
        // frontier, leave it untouched for the checked path; the earlier
        // deferred outputs stay ordered in Update and cannot be replayed.
        if (frame.scheduler_frontier_generation == frontier.generation
            || !region_frontier_generic_retirement_ack_valid_v2(
                frame.pending_write_count, frame.staged_event_count,
                frame.generic_update_ack_count)) {
            return std::nullopt;
        }
        // The prior ACK is terminal for its original Active callback. A new
        // scheduler frontier may reuse this private frame only after the full
        // Update batch has been secured; never re-enter the old batch.
        clear_generic_frame_step(runtime);
    }
    for (std::size_t index = 0U; index < frontier.tasks.size(); ++index) {
        if (!same_generic_task(current->tasks[index], frontier.tasks[index])) {
            return std::nullopt;
        }
        if (index != 0U) {
            const auto& prior = frontier.tasks[index - 1U];
            const auto& task = frontier.tasks[index];
            if (!(prior.stable_order < task.stable_order
                    || (prior.stable_order == task.stable_order
                        && prior.sequence < task.sequence))) {
                return std::nullopt;
            }
        }
    }
    if (require_compact_receipts) {
        if (runtime.generic_ticket_member_offsets.size()
                != layout.member_count
            || region_readiness_member_index_by_process.size()
                < processes.size()) {
            return std::nullopt;
        }
        std::ranges::fill(runtime.generic_ticket_member_offsets,
            std::numeric_limits<std::size_t>::max());
        for (std::size_t index = 0U;
             index < frontier.ticket_members.size(); ++index) {
            const auto& ticket_member = frontier.ticket_members[index];
            ProcessId process { };
            if (!generic_task_process(ticket_member.payload, process)
                || ticket_member.order != static_cast<StableOrder>(process)
                || process >= region_readiness_member_index_by_process.size()
                || process >= region_component_by_process.size()
                || region_component_by_process[process] != runtime.component) {
                return std::nullopt;
            }
            const auto member_index
                = region_readiness_member_index_by_process[process];
            if (member_index >= layout.member_count
                || layout.members == nullptr
                || layout.members[member_index].process_id != process
                || runtime.generic_ticket_member_offsets[member_index]
                    != std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            runtime.generic_ticket_member_offsets[member_index]
                = frontier.ticket_member_offset + index;
            if (index != 0U) {
                const auto& prior = frontier.ticket_members[index - 1U];
                if (!(prior.order < ticket_member.order
                        || (prior.order == ticket_member.order
                            && prior.sequence < ticket_member.sequence))) {
                    return std::nullopt;
                }
            }
        }
        for (std::size_t index = 0U;
             index < frontier.tasks.size(); ++index) {
            const auto& task = frontier.tasks[index];
            const auto& ticket_member = frontier.ticket_members[index];
            if (task.stable_order != ticket_member.order
                || task.sequence != ticket_member.sequence
                || task.payload != ticket_member.payload) {
                return std::nullopt;
            }
        }
    }
    if (layout.member_count != runtime.members.size()
        || runtime.generic_parked_executors.size() != layout.member_count
        || (require_compact_receipts
            && (runtime.generic_queued_members.size()
                    != layout.member_count
                || runtime.generic_queued_ready_words.size()
                    != (static_cast<std::size_t>(layout.member_count) + 63U)
                        / 64U))
        || layout.signal_slot_count != runtime.planes.size()
        || layout.write_site_count != runtime.pending_writes.size()
        || layout.pending_write_capacity != runtime.pending_writes.size()
        || frame.staged_event_capacity != runtime.staged_events.size()
        || frame.staged_event_capacity > layout.staged_event_capacity
        || frame.generic_update_ack_count != 0U
        || frame.scheduler_task_capacity
            != RegionFrontierComponentRuntime::scheduler_task_capacity
        || frame.scheduler_tasks != runtime.scheduler_tasks.data()
        || frame.planes != runtime.planes.data()
        || frame.port_planes != runtime.port_planes.data()
        || frame.pending_writes != runtime.pending_writes.data()
        || frame.staged_events != runtime.staged_events.data()
        || frame.metadata_count != 0U || frame.metadata != nullptr
        || frame.pending_write_count != 0U
        || frame.staged_event_count != 0U
        || frame.committed_signal_count != 0U) {
        return std::nullopt;
    }

    const auto& kernel = runtime.backend->kernel;
    if (kernel.program.scheduling_domain != ProcessSchedulingDomain::generic
        || runtime.component >= region_graph->certificate_inventory()
                .components.size()
        || layout.member_count != kernel.members.size()) {
        return std::nullopt;
    }

    // Authenticate every queued member of this component against the complete
    // borrowed frontier before seeding a generated frame. Tasks beyond the
    // consumed prefix retain their original scheduler keys, but the generated
    // entry receives only the exact prefix selected by execute().
    if (runtime.generic_member_task_indices.size() != layout.member_count) {
        return std::nullopt;
    }
    std::ranges::fill(runtime.generic_member_task_indices,
        std::numeric_limits<std::size_t>::max());
    for (std::size_t member_index = 0U;
         member_index < layout.member_count; ++member_index) {
        const auto process = static_cast<ProcessId>(
            layout.members[member_index].process_id);
        if (process >= processes.size()
            || kernel.members[member_index].process != process
            || process >= region_component_by_process.size()
            || region_component_by_process[process] != runtime.component) {
            return std::nullopt;
        }
        const auto& state = get_process(process);
        if (!state.waiting_on_static || state.waiting_on_signal
            || state.suspended || state.halted
            || !state.region_kernel_completion_has_no_persistent_registers
            || !state.executor
            || !state.executor->region_kernel_completion_has_no_persistent_registers()
            || !kernel.members[member_index].all_registers_definitely_defined
            || kernel.members[member_index].final_debug_state) {
            return std::nullopt;
        }
        std::size_t task_count { };
        for (std::size_t task_index = 0U;
             task_index < frontier.tasks.size(); ++task_index) {
            ProcessId task_process { };
            if (generic_task_process(
                    frontier.tasks[task_index].payload, task_process)
                && task_process == process) {
                runtime.generic_member_task_indices[member_index]
                    = task_index;
                ++task_count;
            }
        }
        const bool has_task
            = runtime.generic_member_task_indices[member_index]
                != std::numeric_limits<std::size_t>::max();
        const bool has_ticket_member = require_compact_receipts
            && runtime.generic_ticket_member_offsets[member_index]
                != std::numeric_limits<std::size_t>::max();
        // A consumed root may already have a separately issued next-delta
        // receipt while this ticket still retains a later current-delta
        // suffix. Authenticate that sidecar below; it is not a current task.
        const bool has_future_ticket_receipt = require_compact_receipts
            && !has_ticket_member
            && runtime.generic_queued_members[member_index]
                .receipt.valid;
        if (task_count > 1U
            || (require_compact_receipts
                ? (state.queued
                        != (has_ticket_member || has_future_ticket_receipt)
                    || (has_task && !has_ticket_member)
                    || (has_task
                        && runtime.generic_ticket_member_offsets[member_index]
                            != frontier.ticket_member_offset
                                + runtime.generic_member_task_indices[
                                    member_index]))
                : state.queued != has_task)) {
            return std::nullopt;
        }
        if (require_compact_receipts) {
            const auto& queued
                = runtime.generic_queued_members[member_index];
            const auto ready_bit = UINT64_C(1) << (member_index % 64U);
            const bool ready = (runtime.generic_queued_ready_words[
                member_index / 64U] & ready_bit) != 0U;
            if (has_ticket_member) {
                const auto ticket_offset
                    = runtime.generic_ticket_member_offsets[member_index]
                        - frontier.ticket_member_offset;
                if (ticket_offset >= frontier.ticket_members.size()) {
                    return std::nullopt;
                }
                const auto& task = frontier.ticket_members[ticket_offset];
                const auto& receipt = queued.receipt;
                if (!ready || !receipt.valid
                    || receipt.time != frontier.time
                    || receipt.delta != frontier.delta
                    || receipt.phase != SchedulerPhase::active
                    || receipt.stable_order != process
                    || receipt.sequence != task.sequence
                    || receipt.payload != task.payload
                    || state.static_trigger_mask == 0U
                    || queued.static_trigger_mask
                        != state.static_trigger_mask) {
                    return std::nullopt;
                }
            } else if (has_future_ticket_receipt) {
                if (!ready || !queued.receipt.valid
                    || frontier.delta
                        == std::numeric_limits<std::uint64_t>::max()
                    || queued.receipt.time != frontier.time
                    || queued.receipt.delta != frontier.delta + 1U
                    || queued.receipt.phase != SchedulerPhase::active
                    || queued.receipt.stable_order != process
                    || queued.receipt.payload
                        != (generic_projected_region_payload
                            | static_cast<std::uint64_t>(process))
                    || !state.queued
                    || state.static_trigger_mask == 0U
                    || queued.static_trigger_mask
                        != state.static_trigger_mask) {
                    return std::nullopt;
                }
            } else if (ready || queued.receipt.valid
                || queued.static_trigger_mask != 0U) {
                return std::nullopt;
            }
        }
    }
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        const auto& task = frontier.tasks[frontier_offset + index];
        if (payloads[index] != task.payload) {
            return std::nullopt;
        }
        if (index >= task_prefix_count) {
            continue;
        }
        ProcessId process { };
        if (!generic_task_process(task.payload, process)) {
            return std::nullopt;
        }
        const auto member = generic_member_for_process(layout, process);
        if (!member
            || runtime.generic_member_task_indices[*member]
                != frontier_offset + index) {
            return std::nullopt;
        }
    }

    // A Generic Logic9 whole write may cross an existing direct std_logic
    // word update when it reaches the checked Update queue. The ordinary
    // blocker preserves that ordering by flushing each pending native mask,
    // but this frontier staging transaction intentionally does not duplicate
    // that fallible multi-row path. Leave any such active mask to the checked
    // evaluator before preparing executor completion state or entering JIT.
    using Logic9Stage = DirectSingleDriverLogic9WordUpdate;
    const bool has_active_native_logic9_mask = std::ranges::any_of(
        kernel.outputs, [&](const RegionConeOutputBinding& output) {
            if (output.value_kind != ValueKind::logic9
                || output.width > 64U || output.signal >= signals.size()
                || output.signal
                    >= direct_single_driver_logic9_word_scratch.size()) {
                return false;
            }
            const auto& signal = signals[output.signal];
            if (signal.value_kind != ValueKind::logic9
                || signal.resolution != ResolutionKind::std_logic
                || signal.initial_value.width() == 0U
                || signal.initial_value.width() > 64U) {
                return false;
            }
            const auto& staged
                = direct_single_driver_logic9_word_scratch[output.signal];
            return staged.active == Logic9Stage::native;
        });
    if (has_active_native_logic9_mask) {
        return std::nullopt;
    }

    if (!bind_generic_frontier_read_snapshots(runtime)) {
        return std::nullopt;
    }

    std::ranges::fill(runtime.ready_words, UINT64_C(0));
    for (std::size_t member_index = 0U;
         member_index < layout.member_count; ++member_index) {
        auto& member = runtime.members[member_index];
        member = { };
        member.process_id = layout.members[member_index].process_id;
        const auto process = static_cast<ProcessId>(member.process_id);
        const auto& state = get_process(process);
        member.static_trigger_mask = require_compact_receipts
            ? runtime.generic_queued_members[member_index].static_trigger_mask
            : state.static_trigger_mask;
        if (state.waiting_on_static) {
            member.flags |= RegionFrontierMemberFlagsV1::waiting_on_static;
        }
        const auto member_task_index
            = runtime.generic_member_task_indices[member_index];
        if (member_task_index != std::numeric_limits<std::size_t>::max()) {
            const auto& task = frontier.tasks[member_task_index];
            member.flags |= RegionFrontierMemberFlagsV1::queued
                | RegionFrontierMemberFlagsV1::queued_key_valid;
            member.queued_key = generic_frontier_key(frontier, task);
            member.activation_origin = member.queued_key;
        }
    }

    clear_generic_frame_step(runtime);
    frame.scheduler_frontier_generation = frontier.generation;
    frame.scheduler_task_count = static_cast<std::uint32_t>(task_prefix_count);
    frame.slot.time = frontier.time;
    frame.slot.delta = frontier.delta;
    frame.slot.systemverilog_round = 0U;
    frame.slot.process_domain
        = static_cast<std::uint32_t>(ProcessSchedulingDomain::generic);
    frame.slot.phase = static_cast<std::uint32_t>(SchedulerPhase::active);
    frame.cut = { };
    frame.cut.scheduler_frontier_generation = frontier.generation;
    frame.cut.kind = RegionFrontierCutKindV1::closed_prefix;
    runtime.stop_requested = scheduler.stop_requested() ? 1U : 0U;

    for (std::size_t index = 0U; index < task_prefix_count; ++index) {
        const auto& original = frontier.tasks[frontier_offset + index];
        ProcessId process { };
        if (!generic_task_process(original.payload, process)) {
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }
        const auto member = generic_member_for_process(layout, process);
        if (!member) {
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }
        runtime.original_scheduler_tasks[index] = original;
        auto& task = runtime.scheduler_tasks[index];
        task.stable_order = original.stable_order;
        task.sequence = original.sequence;
        task.payload = encode_region_frontier_payload_v1(
            RegionFrontierEventKindV2::member_activation, *member);
        const auto word = static_cast<std::size_t>(*member / 64U);
        if (word >= runtime.ready_words.size()) {
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }
        runtime.ready_words[word] |= UINT64_C(1) << (*member % 64U);
    }

    for (auto& write : runtime.pending_writes) {
        // A generic target bit is written by the generated body together
        // with active/value-ready on a site it actually executes. Unused
        // static output slots stay inactive (all flags clear).
        write.flags = 0U;
        write.reserved = 0U;
        write.commit_key = { };
        write.origin = { };
    }
    std::ranges::fill(runtime.pending_plane_words, UINT64_C(0));

    runtime.generic_prefix_processes.clear();
    runtime.generic_prefix_members.clear();
    const auto clear_prefix_processes = [&]() noexcept {
        runtime.generic_prefix_processes.clear();
        runtime.generic_prefix_members.clear();
    };
    for (std::size_t index = 0U; index < task_prefix_count; ++index) {
        ProcessId process { };
        if (!generic_task_process(
                frontier.tasks[frontier_offset + index].payload, process)) {
            clear_prefix_processes();
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }
        auto& state = get_process(process);
        const auto& operations = state.program().operations();
        if (operations.size() < 2U
                || operations.size() - 2U
                    > std::numeric_limits<InstructionIndex>::max()) {
            clear_prefix_processes();
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }
        const auto wait_instruction = static_cast<InstructionIndex>(
            operations.size() - 2U);
        const auto jump_instruction = static_cast<InstructionIndex>(
            operations.size() - 1U);
        const auto member = generic_member_for_process(layout, process);
        if (!member) {
            clear_prefix_processes();
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }

        // A cached cross-cast is valid only while the process executor's
        // lifecycle generation matches. Refresh it after a tracked deferred
        // installation, redirect, fork, replacement, or clear, before using
        // either cached pointer. Generation zero means its counter exhausted;
        // decline permanently rather than risk reviving a stale address.
        const auto lifecycle_generation
            = state.executor_lifecycle_generation;
        if (lifecycle_generation == 0U) {
            clear_prefix_processes();
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }
        auto& parked = runtime.generic_parked_executors[*member];
        if (parked.lifecycle_generation != lifecycle_generation) {
            auto* const executor = state.executor.get();
            parked.executor = executor;
            parked.capability = executor
                ? dynamic_cast<const RegionKernelParkedExecutor*>(executor)
                : nullptr;
            parked.lifecycle_generation = lifecycle_generation;
        }
        if (parked.executor != state.executor.get()
            || parked.capability == nullptr
            || !parked.capability->region_kernel_completion_is_parked_native(
                process, wait_instruction, jump_instruction,
                kernel.members[*member].register_bindings,
                kernel.program.register_count)) {
            clear_prefix_processes();
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }
        runtime.generic_prefix_processes.push_back(process);
        runtime.generic_prefix_members.push_back(*member);
    }

    const auto entry = runtime.backend->executor->step_entry();
    if (entry == nullptr) {
        clear_prefix_processes();
        clear_generic_frame_step(runtime);
        return std::nullopt;
    }
    const auto native_dispatches_before = runtime.native_member_dispatches;
    const auto status = entry(&frame);
    const auto native_dispatches_after = runtime.native_member_dispatches;
    if (systemverilog_wave_profile_enabled
        && native_dispatches_after >= native_dispatches_before) {
        systemverilog_wave_profile_native_frontier_member_dispatches
            += native_dispatches_after - native_dispatches_before;
    }
    if (frame.scheduler_task_cursor > task_prefix_count
        || frame.scheduler_task_cursor
            > runtime.generic_prefix_processes.size()
        || frame.pending_write_count > frame.pending_write_capacity
        || frame.staged_event_count > frame.staged_event_capacity
        || frame.committed_signal_count != 0U
        || frame.generic_update_ack_count != 0U
        || !region_frontier_status_valid_for_mode_v2(
            RegionFrontierExecutionModeV2::generic_deferred_update, status)) {
        clear_prefix_processes();
        clear_generic_frame_step(runtime);
        throw std::logic_error("invalid generic native-frontier result");
    }

    const bool has_updates = frame.pending_write_count != 0U
        || frame.staged_event_count != 0U;
    bool ack_contract_broken { };
    if (status == RegionFrontierStatusV2::decline_before_mutation
        || status == RegionFrontierStatusV2::stale_generation) {
        if (frame.scheduler_task_cursor != 0U || has_updates) {
            clear_prefix_processes();
            clear_generic_frame_step(runtime);
            throw std::logic_error(
                "generic frontier declined after consuming a task");
        }
        clear_prefix_processes();
        clear_generic_frame_step(runtime);
        return std::nullopt;
    }
    if (status == RegionFrontierStatusV2::generic_update_batch_ready) {
        if (!region_frontier_generic_batch_ready_counts_valid_v2(
                frame.pending_write_count, frame.staged_event_count,
                frame.generic_update_ack_count)
                || frame.scheduler_task_cursor == 0U) {
            clear_prefix_processes();
            clear_generic_frame_step(runtime);
            throw std::logic_error(
                "generic frontier returned an empty update prefix");
        }
        std::exception_ptr failure;
        if (!stage_generic_frontier_update_batch(runtime, failure)) {
            clear_prefix_processes();
            if (failure) {
                // The staging helper rolled back both host update vectors and
                // left ACK at zero. Generated writes live only in this frame,
                // so clear that private prefix before the scheduler reoffers
                // the exact original task span.
                clear_generic_frame_step(runtime);
                SchedulerBatchResult failed;
                failed.failure = std::move(failure);
                return failed;
            }
            clear_generic_frame_step(runtime);
            return std::nullopt;
        }
        if (!region_frontier_generic_retirement_ack_valid_v2(
                frame.pending_write_count, frame.staged_event_count,
                frame.generic_update_ack_count)) {
            // Host staging has committed its complete Update vector at this
            // point. Retire exactly the consumed prefix below, then surface
            // the invariant failure with that consumed count so it cannot be
            // replayed as a second publication.
            ack_contract_broken = true;
        }
    } else if (has_updates
        || frame.generic_update_ack_count != 0U) {
        clear_prefix_processes();
        clear_generic_frame_step(runtime);
        throw std::logic_error(
            "generic frontier exposed writes without the ready status");
    }

    const auto consumed = static_cast<std::size_t>(frame.scheduler_task_cursor);
    for (std::size_t index = 0U; index < consumed; ++index) {
        const auto process = runtime.generic_prefix_processes[index];
        auto& state = get_process(process);
        if (require_compact_receipts) {
            runtime.clear_generic_queued_member(
                runtime.generic_prefix_members[index]);
        }
        state.queued = false;
        state.waiting_on_static = false;
        state.static_trigger_mask = 0U;
        state.waiting_on_static = true;
        state.status = ProcessStatus::waiting;
    }
    runtime.generic_prefix_processes.clear();
    runtime.generic_prefix_members.clear();

    SchedulerBatchResult result;
    result.executed = consumed;
    if (ack_contract_broken) {
        runtime.invalidate();
        clear_generic_frame_step(runtime);
        // This path follows successful host publication, so preserve the
        // exact consumed count even if allocating the diagnostic exception
        // itself fails.
        try {
            throw std::logic_error("generic frontier ACK was not complete");
        } catch (...) {
            result.failure = std::current_exception();
        }
    }
    return result;
}

} // namespace fsim::runtime::simir
