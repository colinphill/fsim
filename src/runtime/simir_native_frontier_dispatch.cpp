// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_execution_context.hpp"
#include "simir_region_frontier_trusted_entry.hpp"

#include <cstdio>
#include <new>

namespace fsim::runtime::simir {

namespace {

constexpr std::uint64_t kSystemVerilogWavePayload
    = UINT64_C(1) << 61U;

[[nodiscard]] bool frontier_is_current(
    const Scheduler& scheduler,
    const SchedulerBatchFrontier& frontier,
    const std::span<const std::uint64_t> payloads) noexcept
{
    const auto current = scheduler.current_batch_frontier();
    if (!current || frontier.generation == 0U
        || current->generation != frontier.generation
        || current->time != frontier.time
        || current->delta != frontier.delta
        || current->phase != frontier.phase
        || current->systemverilog_round != frontier.systemverilog_round
        || frontier.phase != SchedulerPhase::active
        || frontier.cursor != 0U
        || frontier.end != frontier.tasks.size()
        || frontier.tasks.size() != payloads.size()
        || scheduler.now() != frontier.time
        || scheduler.delta() != frontier.delta
        || scheduler.current_phase() != SchedulerPhase::active
        || scheduler.systemverilog_round()
            != frontier.systemverilog_round) {
        return false;
    }
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        const auto& task = frontier.tasks[index];
        if (task.payload != payloads[index]
            || (index != 0U
                && !(frontier.tasks[index - 1U].stable_order
                        < task.stable_order
                    || (frontier.tasks[index - 1U].stable_order
                            == task.stable_order
                        && frontier.tasks[index - 1U].sequence
                            < task.sequence)))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] RegionFrontierKeyV1 frontier_key(
    const SchedulerBatchFrontier& frontier,
    const SchedulerBatchFrontierEntry& task) noexcept
{
    return {
        frontier.time,
        frontier.delta,
        frontier.systemverilog_round,
        task.stable_order,
        task.sequence,
        static_cast<std::uint32_t>(
            ProcessSchedulingDomain::systemverilog),
        static_cast<std::uint32_t>(SchedulerPhase::active),
    };
}

[[nodiscard]] bool same_frontier_key(
    const RegionFrontierKeyV1& left,
    const RegionFrontierKeyV1& right) noexcept
{
    return left.time == right.time && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool decode_wave_payload(
    const std::uint64_t payload,
    ProcessId& process) noexcept
{
    if ((payload & kSystemVerilogWavePayload) == 0U) {
        return false;
    }
    const auto process_id = payload & ~kSystemVerilogWavePayload;
    if (process_id > std::numeric_limits<ProcessId>::max()) {
        return false;
    }
    process = static_cast<ProcessId>(process_id);
    return true;
}

} // namespace

void Interpreter::Impl::record_systemverilog_readiness_key(
    const ProcessId process,
    const std::size_t component,
    const std::size_t member_index,
    const std::uint64_t generation,
    const SchedulerSystemVerilogKeyReceipt& receipt,
    const std::uint64_t trigger_mask) noexcept
{
    if (generation == 0U
        || process >= region_readiness_queued_by_process.size()) {
        return;
    }

    RegionFrontierKeyV1 key;
    const bool receipt_key_valid = receipt.valid
        && receipt.phase == SchedulerPhase::active
        && receipt.stable_order == process
        && receipt.systemverilog_round != 0U;
    if (receipt_key_valid) {
        key.time = receipt.time;
        key.delta = receipt.delta;
        key.systemverilog_round = receipt.systemverilog_round;
        key.stable_order = receipt.stable_order;
        key.sequence = receipt.sequence;
        key.process_domain = static_cast<std::uint32_t>(
            ProcessSchedulingDomain::systemverilog);
        key.phase = static_cast<std::uint32_t>(receipt.phase);
    }

    auto& queued = region_readiness_queued_by_process[process];
    const bool existing_identity = queued.generation == generation
        && queued.component == component && queued.member == member_index;
    if (queued.generation != 0U && !existing_identity) {
        if (component < region_frontier_runtime_by_component.size()) {
            const auto& runtime
                = region_frontier_runtime_by_component[component];
            if (runtime && runtime->runtime_generation == generation) {
                runtime->invalidate();
            }
        }
        return;
    }

    if (queued.generation == 0U) {
        queued.component = component;
        queued.member = member_index;
        queued.generation = generation;
    }
    queued.static_trigger_mask |= trigger_mask;
    if (receipt_key_valid) {
        if (queued.key_valid && !same_frontier_key(queued.queued_key, key)) {
            if (component < region_frontier_runtime_by_component.size()) {
                const auto& runtime
                    = region_frontier_runtime_by_component[component];
                if (runtime && runtime->runtime_generation == generation) {
                    runtime->invalidate();
                }
            }
            return;
        }
        queued.queued_key = key;
        queued.key_valid = true;
    }

    if (component >= region_frontier_runtime_by_component.size()) {
        return;
    }
    const auto& runtime = region_frontier_runtime_by_component[component];
    if (!runtime || runtime->owner != this || runtime->component != component
        || runtime->runtime_generation != generation
        || !runtime->frame_initialized || !runtime->scheduler_state_seeded
        || runtime->invalidated) {
        return;
    }
    if (member_index >= runtime->members.size()
        || member_index / 64U >= runtime->ready_words.size()
        || runtime->frame.members != runtime->members.data()
        || runtime->frame.ready_words != runtime->ready_words.data()
        || runtime->members[member_index].process_id != process) {
        runtime->invalidate();
        return;
    }

    runtime->mark_member_sync_write(static_cast<std::uint32_t>(member_index));
    auto& member = runtime->members[member_index];
    member.static_trigger_mask |= trigger_mask;
    if (!queued.key_valid) {
        // The process has already been queued by the ordinary scheduler, but
        // this runtime cannot represent its exact scheduler key. Keep the
        // checked task authoritative and prevent a stale native frame from
        // overwriting it.
        runtime->invalidate();
        return;
    }

    constexpr auto queued_flags = RegionFrontierMemberFlagsV1::queued
        | RegionFrontierMemberFlagsV1::queued_key_valid;
    const auto present_flags = member.flags & queued_flags;
    if (present_flags == queued_flags) {
        if (!same_frontier_key(member.queued_key, queued.queued_key)) {
            runtime->invalidate();
            return;
        }
    } else if (present_flags != 0U
        || (member.flags & (RegionFrontierMemberFlagsV1::executing
                               | RegionFrontierMemberFlagsV1::pending_activation))
            != 0U
        || (member.flags
                & RegionFrontierMemberFlagsV1::waiting_on_static) == 0U) {
        runtime->invalidate();
        return;
    } else {
        member.flags |= queued_flags;
        member.queued_key = queued.queued_key;
    }
    if (component >= region_readiness_mask_by_component.size()) {
        runtime->invalidate();
        return;
    }
    const auto& mask = region_readiness_mask_by_component[component];
    if (mask.generation != generation
        || member_index / 64U >= mask.word_count
        || mask.offset > region_readiness_mask_words.size()
        || mask.word_count
            > region_readiness_mask_words.size() - mask.offset) {
        runtime->invalidate();
        return;
    }
    region_readiness_mask_words[mask.offset + member_index / 64U]
        |= UINT64_C(1) << (member_index % 64U);
    runtime->ready_words[member_index / 64U]
        |= UINT64_C(1) << (member_index % 64U);
}

void Interpreter::Impl::merge_systemverilog_readiness_mask(
    const ProcessId process,
    const std::uint64_t trigger_mask) noexcept
{
    if (trigger_mask == 0U) {
        return;
    }
    if (process < region_readiness_queued_by_process.size()) {
        auto& queued = region_readiness_queued_by_process[process];
        if (queued.generation != 0U
            && queued.generation == region_runtime_generation) {
            queued.static_trigger_mask |= trigger_mask;
        }
    }
    if (process >= region_component_by_process.size()
        || process >= region_readiness_member_index_by_process.size()) {
        return;
    }
    const auto component = region_component_by_process[process];
    if (component >= region_frontier_runtime_by_component.size()) {
        return;
    }
    const auto& runtime = region_frontier_runtime_by_component[component];
    const auto member_index
        = region_readiness_member_index_by_process[process];
    if (!runtime || runtime->owner != this || runtime->component != component
        || runtime->runtime_generation != region_runtime_generation
        || !runtime->frame_initialized || !runtime->scheduler_state_seeded
        || runtime->invalidated || member_index >= runtime->members.size()
        || runtime->members[member_index].process_id != process) {
        return;
    }
    runtime->mark_member_sync_write(static_cast<std::uint32_t>(member_index));
    runtime->members[member_index].static_trigger_mask |= trigger_mask;
}

SchedulerBatchResult
Interpreter::Impl::RegionFrontierComponentRuntime::execute(
    Scheduler& active_scheduler,
    const std::span<const std::uint64_t> payloads)
{
    SchedulerBatchResult result;
    if (owner == nullptr || payloads.empty()) {
        return result;
    }
    if (execution_mode
        == RegionFrontierExecutionModeV2::generic_deferred_update) {
        const auto frontier = active_scheduler.current_generic_batch_frontier();
        if (!frontier || frontier->tasks.size() != payloads.size()) {
            return result;
        }
        const auto dispatched = owner->execute_generic_region_frontier_prefix(
            *this, *frontier, payloads, 0U, payloads.size(), true);
        return dispatched.value_or(result);
    }
    if (execution_mode
        != RegionFrontierExecutionModeV2::systemverilog_active) {
        return result;
    }
    const auto frontier = active_scheduler.current_batch_frontier();
    if (!frontier) {
        return result;
    }
    const auto dispatched = owner->try_execute_region_frontier_component(
        *this, *frontier, payloads, payloads.size());
    return dispatched.value_or(result);
}

void Interpreter::Impl::RegionFrontierComponentRuntime::
    clear_generic_queued_member(const std::size_t member) noexcept
{
    if (member >= generic_queued_members.size()
        || member / 64U >= generic_queued_ready_words.size()) {
        return;
    }
    generic_queued_ready_words[member / 64U]
        &= ~(UINT64_C(1) << (member % 64U));
    generic_queued_members[member] = { };
}

std::optional<SchedulerBatchResult>
Interpreter::Impl::try_execute_region_frontier_prefix(
    const SchedulerBatchFrontier& frontier,
    const std::span<const std::uint64_t> payloads)
{
    if (payloads.empty() || !frontier_is_current(scheduler, frontier, payloads)) {
        return std::nullopt;
    }
    ProcessId first_process { };
    if (!decode_wave_payload(payloads.front(), first_process)
        || first_process >= region_component_by_process.size()) {
        return std::nullopt;
    }
    const auto component = region_component_by_process[first_process];
    if (component == no_systemverilog_update_slot
        || component >= region_frontier_runtime_by_component.size()) {
        return std::nullopt;
    }
    const auto runtime = region_frontier_runtime_by_component[component];
    if (!runtime || runtime->owner != this
        || runtime->component != component) {
        return std::nullopt;
    }

    std::size_t prefix_count { };
    for (std::size_t index = 0U;
         index < payloads.size()
             && prefix_count
                 < RegionFrontierComponentRuntime::scheduler_task_capacity;
         ++index) {
        ProcessId process { };
        if (!decode_wave_payload(payloads[index], process)
            || process >= region_component_by_process.size()
            || region_component_by_process[process] != component) {
            break;
        }
        ++prefix_count;
    }
    if (prefix_count == 0U) {
        return std::nullopt;
    }

    // A certified flattened cone uses the original borrowed scheduler keys
    // without entering the per-member V2 frame. Authenticate its contiguous
    // borrowed seed prefix before touching the bank or the V2 write lease.
    bool forwarding_attempted { };
    std::array<ProcessId,
        RegionFrontierComponentRuntime::scheduler_task_capacity>
        authenticated_processes { };
    // Forwarding has its own certificate and private state. A V2 frame may
    // be unseeded at first admission or invalidated by a previously consumed
    // flattened prefix; neither condition disqualifies its continuation.
    const auto v2_frame_has_no_pending_effects = [&] {
        const auto& frame = runtime->frame;
        return frame.scheduler_task_cursor == frame.scheduler_task_count
            && frame.pending_write_count == 0U
            && frame.current_pending_write == UINT32_MAX
            && frame.staged_event_count == 0U
            && frame.committed_signal_count == 0U
            && frame.generic_update_ack_count == 0U;
    };
    const bool forwarding_available = systemverilog_region_kernel_enabled
        && systemverilog_local_wave_enabled && region_graph
        && component
            < region_graph->certificate_inventory().components.size()
        && region_graph->component_epochs_current(component)
        && !scheduler.trace_hook_installed()
        && !cohort_overflow_scratch_in_use
        && runtime->runtime_generation == region_runtime_generation
        && !runtime->in_use.test(std::memory_order_acquire)
        && v2_frame_has_no_pending_effects()
        && component < region_activation_programs.size()
        && region_activation_programs[component]
        && region_activation_programs[component]->forwarding_kernel
        && prefix_count <= cohort_overflow_contexts.capacity();
    if (forwarding_available) {
        // Eligibility calls executor virtuals. Keep the shared scratch
        // guarded during discovery, as the ordinary wave consumer does.
        auto& contexts = cohort_overflow_contexts;
        cohort_overflow_scratch_in_use = true;
        contexts.clear();
        struct ClearContexts {
            std::vector<ExecutionContext>& contexts;
            bool& in_use;
            ~ClearContexts()
            {
                contexts.clear();
                in_use = false;
            }
        } clear { contexts, cohort_overflow_scratch_in_use };
        std::size_t authenticated_count { };
        const void* domain { };
        for (std::size_t index = 0U; index < prefix_count; ++index) {
            ProcessId process { };
            if (!decode_wave_payload(payloads[index], process)
                || process >= region_readiness_queued_by_process.size()
                || process >= region_readiness_member_index_by_process.size()
                || process >= processes.size()
                || !systemverilog_wave_member_eligible(process)) {
                break;
            }
            const auto& task = frontier.tasks[index];
            const auto& queued = region_readiness_queued_by_process[process];
            const auto& state = get_process(process);
            const auto* const member_domain = state.executor->cohort_domain();
            if (task.payload != payloads[index]
                || task.stable_order != process || !state.queued
                || (domain != nullptr && member_domain != domain)
                || !queued.key_valid
                || queued.generation != region_runtime_generation
                || queued.component != component
                || queued.member
                    != region_readiness_member_index_by_process[process]
                || !same_frontier_key(
                    queued.queued_key, frontier_key(frontier, task))) {
                break;
            }
            domain = member_domain;
            authenticated_processes[index] = process;
            ++authenticated_count;
        }
        const auto current_frontier = scheduler.current_batch_frontier();
        bool receipts_still_current = authenticated_count != 0U
            && frontier_is_current(scheduler, frontier, payloads)
            && current_frontier
            && current_frontier->tasks.size() == frontier.tasks.size()
            && current_frontier->cursor == frontier.cursor
            && current_frontier->end == frontier.end
            && component < region_frontier_runtime_by_component.size()
            && region_frontier_runtime_by_component[component].get()
                == runtime.get()
            && runtime->runtime_generation == region_runtime_generation
            && !runtime->in_use.test(std::memory_order_acquire)
            && v2_frame_has_no_pending_effects()
            && region_graph
            && region_graph->component_epochs_current(component)
            && component < region_activation_programs.size()
            && region_activation_programs[component]
            && region_activation_programs[component]->forwarding_kernel;
        for (std::size_t index = 0U;
             receipts_still_current && index < authenticated_count; ++index) {
            const auto process = authenticated_processes[index];
            const auto& task = frontier.tasks[index];
            const auto& current_task = current_frontier->tasks[index];
            const auto& queued = region_readiness_queued_by_process[process];
            receipts_still_current = current_task.payload == task.payload
                && current_task.stable_order == task.stable_order
                && current_task.sequence == task.sequence
                && get_process(process).queued
                && queued.key_valid
                && queued.generation == region_runtime_generation
                && queued.component == component
                && queued.member
                    == region_readiness_member_index_by_process[process]
                && same_frontier_key(
                    queued.queued_key, frontier_key(frontier, task));
        }
        if (receipts_still_current) {
            for (std::size_t index = 0U;
                 index < authenticated_count; ++index) {
                contexts.emplace_back(*this, authenticated_processes[index]);
            }

            forwarding_attempted = true;
            std::size_t forwarded_members { };
            std::exception_ptr forwarding_failure;
            if (execute_region_forwarding_prefix(component,
                    *region_activation_programs[component], contexts,
                    forwarded_members, forwarding_failure)) {
                if (forwarded_members == 0U
                    || forwarded_members > authenticated_count) {
                    throw std::logic_error {
                        "flattened region forwarding returned an invalid "
                        "scheduler prefix"
                    };
                }
                SchedulerBatchResult result;
                result.executed = forwarded_members;
                // This early return bypasses the ordinary wave's consumed
                // prefix cleanup. Retire only these original-key receipts;
                // the helper deliberately invalidates the old seeded V2
                // frame, so a later callback uses forwarding or checked
                // execution until quiet recertification installs a new one.
                clear_systemverilog_readiness_prefix(
                    frontier, forwarded_members);
                if (systemverilog_wave_profile_enabled) {
                    ++systemverilog_wave_profile_v2_selected_forwarding_prefixes;
                    systemverilog_wave_profile_v2_selected_forwarding_members
                        += static_cast<std::uint64_t>(forwarded_members);
                }
                return result;
            }
            if (forwarding_failure) {
                if (forwarded_members > authenticated_count) {
                    throw std::logic_error {
                        "failed flattened forwarding returned an invalid "
                        "scheduler prefix"
                    };
                }
                SchedulerBatchResult result;
                result.executed = forwarded_members;
                result.failure = std::move(forwarding_failure);
                clear_systemverilog_readiness_prefix(
                    frontier, forwarded_members);
                return result;
            }
        }
    }

    // A declined private continuation can retain applied A4 role rows. Its
    // checked callback flushes those rows before reading them; do not let a
    // per-member native frame inspect the older public planes in between.
    if (component < region_local_wave_state_by_component.size()) {
        const auto& local = region_local_wave_state_by_component[component];
        if (local && local->forwarding_results) {
            const auto& bank = *local->forwarding_results;
            if (bank.active || bank.role_journal_enabled
                || bank.private_epoch_retired
                || !bank.applied_role_mutations.empty()
                || !bank.applied_role_metadata.empty()) {
                return SchedulerBatchResult { };
            }
        }
    }
    if (const auto v2_result = try_execute_region_frontier_component(
            *runtime, frontier, payloads, prefix_count)) {
        return v2_result;
    }
    // The old wave callback would attempt the same forwarding backend again
    // after a V2 decline. A single checked fallback is the conservative
    // result of a real forwarding attempt at this exact borrowed prefix.
    if (forwarding_attempted) {
        return SchedulerBatchResult { };
    }
    return std::nullopt;
}

std::optional<SchedulerBatchResult>
Interpreter::Impl::try_execute_region_frontier_component(
    RegionFrontierComponentRuntime& runtime,
    const SchedulerBatchFrontier& frontier,
    const std::span<const std::uint64_t> payloads,
    const std::size_t offered_task_prefix_count)
{
    auto task_prefix_count = offered_task_prefix_count;
    if (task_prefix_count == 0U || task_prefix_count > payloads.size()
        || !frontier_is_current(scheduler, frontier, payloads)
        || task_prefix_count > RegionFrontierComponentRuntime::scheduler_task_capacity
        || cohort_overflow_scratch_in_use || !runtime.try_enter()) {
        return std::nullopt;
    }
    AuthoritativeSignalPlanes::FrontierWriteLease lease;
    struct LeaveRuntime {
        RegionFrontierComponentRuntime& runtime;
        AuthoritativeSignalPlanes::FrontierWriteLease& lease;
        ~LeaveRuntime()
        {
            runtime.end_alias_bind_lease();
            lease.release();
            runtime.leave();
        }
    } leave_runtime { runtime, lease };
    runtime.clear_canonical_values_binding_receipt();

    const auto component = runtime.component;
    const auto backend_pin = runtime.backend;
    if (runtime.owner != this || runtime.invalidated
        || !runtime.frame_initialized || !backend_pin
        || !backend_pin->executor || !region_graph
        || component >= region_frontier_runtime_by_component.size()
        || region_frontier_runtime_by_component[component].get() != &runtime
        || component >= region_authoritative_state_by_component.size()
        || !runtime.authoritative_state
        || region_authoritative_state_by_component[component]
            != runtime.authoritative_state
        || !runtime.authoritative_state->valid()
        || runtime.authoritative_state->generation()
            != runtime.runtime_generation
        || runtime.runtime_generation != region_runtime_generation
        || !region_graph->component_epochs_current(component)
        || !region_local_wave_component_eligible(
            component, backend_pin->kernel)
        || !frontier_is_current(scheduler, frontier, payloads)) {
        return std::nullopt;
    }

    const auto& layout = backend_pin->executor->layout();
    auto shared_runtime = region_frontier_runtime_by_component[component];
    if (!shared_runtime || shared_runtime.get() != &runtime
        || layout.abi_version != kRegionFrontierAbiVersionV2
        || !region_frontier_layout_header_valid_v2(layout)
        || layout.struct_size != sizeof(RegionFrontierLayoutV2)
        || layout.member_count != runtime.members.size()
        || layout.member_count != backend_pin->kernel.members.size()
        || layout.signal_slot_count != runtime.planes.size()
        || !region_frontier_frame_header_valid_v2(runtime.frame)
        || runtime.frame.generic_update_ack_count != 0U
        || runtime.frame.signal_slot_count != layout.signal_slot_count
        || runtime.frame.planes != runtime.planes.data()
        || runtime.frame.scheduler_task_capacity
            != RegionFrontierComponentRuntime::scheduler_task_capacity
        || runtime.frame.scheduler_tasks != runtime.scheduler_tasks.data()
        || runtime.scheduler_tasks.size()
            != RegionFrontierComponentRuntime::scheduler_task_capacity
        || runtime.original_scheduler_tasks.size()
            != RegionFrontierComponentRuntime::scheduler_task_capacity
        || runtime.frame.staged_event_capacity != runtime.staged_events.size()
        || runtime.frame.staged_event_capacity != runtime.compact_members.size()
        || runtime.frame.staged_event_capacity != runtime.issued_sequences.size()
        || task_prefix_count > runtime.scheduler_tasks.size()
        || runtime.frame.staged_event_count != 0U
        || runtime.frame.committed_signal_count != 0U
        || runtime.frame.scheduler_task_cursor
            > runtime.frame.scheduler_task_count) {
        return std::nullopt;
    }

    const auto member_for_process = [&](const ProcessId process)
        -> std::optional<std::uint32_t> {
        if (layout.members == nullptr
            || process >= region_readiness_member_index_by_process.size()) {
            return std::nullopt;
        }
        const auto member_index
            = region_readiness_member_index_by_process[process];
        if (member_index >= layout.member_count
            || layout.members[member_index].process_id != process) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(member_index);
    };

    // Match the generated preflight's cumulative staged-event bounds. A
    // single certified task fits the frame, but a borrowed group may not.
    // Keep the original order and leave the first over-budget task and its
    // suffix in the scheduler rather than invalidating a reusable runtime.
    std::uint32_t staged_event_budget { };
    std::size_t budgeted_prefix_count { };
    bool prefix_budget_full { };
    bool prefix_budget_valid = layout.members != nullptr
        && layout.max_member_staged_event_counts != nullptr;
    for (std::size_t index = 0U;
         prefix_budget_valid && index < offered_task_prefix_count; ++index) {
        const auto payload = frontier.tasks[index].payload;
        std::optional<std::uint32_t> member;
        std::uint32_t task_event_bound { };
        ProcessId process { };
        if (decode_wave_payload(payload, process)) {
            member = member_for_process(process);
            prefix_budget_valid = member.has_value();
        } else {
            const auto kind = payload >> kRegionFrontierPayloadKindShiftV2;
            const auto task_index = payload & kRegionFrontierPayloadIndexMaskV2;
            if (kind == static_cast<std::uint32_t>(
                            RegionFrontierEventKindV2::member_activation)) {
                prefix_budget_valid = task_index < layout.member_count;
                if (prefix_budget_valid) {
                    member = static_cast<std::uint32_t>(task_index);
                }
            } else if (kind == static_cast<std::uint32_t>(
                                   RegionFrontierEventKindV2::internal_commit)
                || kind == static_cast<std::uint32_t>(
                               RegionFrontierEventKindV2::boundary_commit)) {
                prefix_budget_valid = task_index < layout.pending_write_capacity
                    && task_index < runtime.pending_writes.size()
                    && layout.write_sites != nullptr;
                bool matching_site { };
                if (prefix_budget_valid && task_index < layout.write_site_count
                    && layout.write_sites[task_index].pending_slot == task_index) {
                    matching_site = layout.write_sites[task_index].event_kind == kind;
                } else {
                    // Provider layouts may permute their unique pending slots.
                    // Keep their acceptance while the usual dense table takes
                    // a single authenticated row lookup.
                    for (std::uint32_t site_index = 0U;
                         prefix_budget_valid && site_index < layout.write_site_count;
                         ++site_index) {
                        const auto& site = layout.write_sites[site_index];
                        if (site.pending_slot != task_index) {
                            continue;
                        }
                        matching_site = site.event_kind == kind;
                        break;
                    }
                }
                prefix_budget_valid = prefix_budget_valid && matching_site;
                if (kind == static_cast<std::uint32_t>(
                                RegionFrontierEventKindV2::internal_commit)) {
                    task_event_bound = layout.max_commit_fanout_events;
                }
            } else {
                prefix_budget_valid = false;
            }
        }
        if (!prefix_budget_valid) {
            break;
        }
        if (member) {
            task_event_bound = layout.max_member_staged_event_counts[*member];
        }
        if (!prefix_budget_full) {
            if (task_event_bound
                > runtime.frame.staged_event_capacity - staged_event_budget) {
                prefix_budget_full = true;
            } else {
                staged_event_budget += task_event_bound;
                ++budgeted_prefix_count;
            }
        }
    }
    // A malformed payload must still encounter the original authentication
    // and checked fallback. Do not hide it behind a shortened native offer.
    if (prefix_budget_valid) {
        task_prefix_count = budgeted_prefix_count;
        if (task_prefix_count == 0U) {
            return std::nullopt;
        }
        if (systemverilog_wave_profile_enabled
            && task_prefix_count != offered_task_prefix_count) {
            ++systemverilog_wave_profile_native_frontier_budget_trims;
            if (systemverilog_wave_profile_native_frontier_budget_trims <= 16U) {
                std::fprintf(stderr,
                    "fsim-profile: sv-region-recertification "
                    "event=native-prefix-budget component=%zu offered=%zu "
                    "selected=%zu staged_budget=%u staged_capacity=%u "
                    "max_commit_fanout=%u\n",
                    component, offered_task_prefix_count, task_prefix_count,
                    staged_event_budget, runtime.frame.staged_event_capacity,
                    layout.max_commit_fanout_events);
                struct PrefixBudgetTaskTrace {
                    const char* task_kind { "unknown" };
                    std::uint64_t payload { };
                    std::uint32_t payload_kind { UINT32_MAX };
                    std::uint64_t payload_index { UINT32_MAX };
                    std::uint32_t member_index { UINT32_MAX };
                    std::uint32_t process_id { UINT32_MAX };
                    std::uint32_t pending_slot { UINT32_MAX };
                    std::uint32_t write_site_index { UINT32_MAX };
                    std::uint32_t signal_slot { UINT32_MAX };
                    std::uint32_t task_bound { UINT32_MAX };
                    std::uint32_t member_staged_bound { UINT32_MAX };
                    std::uint32_t global_fanout_bound { UINT32_MAX };
                    std::uint32_t site_fanout_bound { UINT32_MAX };
                    std::uint32_t budget_before { };
                    std::uint32_t budget_after { };
                    std::uint64_t budget_if_included { };
                    bool over_capacity { };
                    bool internal_commit { };
                };
                const auto make_task_trace = [&](const std::uint64_t payload,
                                                 const std::uint32_t budget_before) {
                    PrefixBudgetTaskTrace trace;
                    trace.payload = payload;
                    trace.budget_before = budget_before;
                    ProcessId process { };
                    std::optional<std::uint32_t> member;
                    if (decode_wave_payload(payload, process)) {
                        trace.task_kind = "wave-member";
                        trace.process_id = process;
                        member = member_for_process(process);
                    } else {
                        const auto kind = payload
                            >> kRegionFrontierPayloadKindShiftV2;
                        const auto task_index = payload
                            & kRegionFrontierPayloadIndexMaskV2;
                        trace.payload_kind = static_cast<std::uint32_t>(kind);
                        trace.payload_index = task_index;
                        if (kind == static_cast<std::uint32_t>(
                                        RegionFrontierEventKindV2::member_activation)) {
                            trace.task_kind = "member-activation";
                            if (task_index < layout.member_count) {
                                member = static_cast<std::uint32_t>(task_index);
                            }
                        } else if (kind == static_cast<std::uint32_t>(
                                               RegionFrontierEventKindV2::internal_commit)) {
                            trace.task_kind = "internal-commit";
                            trace.internal_commit = true;
                            trace.global_fanout_bound
                                = layout.max_commit_fanout_events;
                            trace.task_bound = layout.max_commit_fanout_events;
                            if (task_index <= UINT32_MAX) {
                                trace.pending_slot
                                    = static_cast<std::uint32_t>(task_index);
                            }
                        } else if (kind == static_cast<std::uint32_t>(
                                               RegionFrontierEventKindV2::boundary_commit)) {
                            trace.task_kind = "boundary-commit";
                            trace.task_bound = 0U;
                            if (task_index <= UINT32_MAX) {
                                trace.pending_slot
                                    = static_cast<std::uint32_t>(task_index);
                            }
                        }
                        if (trace.pending_slot != UINT32_MAX
                            && layout.write_sites != nullptr) {
                            for (std::uint32_t site_index = 0U;
                                 site_index < layout.write_site_count;
                                 ++site_index) {
                                const auto& site = layout.write_sites[site_index];
                                if (site.pending_slot != trace.pending_slot) {
                                    continue;
                                }
                                trace.write_site_index = site_index;
                                trace.signal_slot = site.signal_slot;
                                break;
                            }
                        }
                    }
                    if (member && *member < layout.member_count
                        && layout.max_member_staged_event_counts != nullptr) {
                        trace.member_index = *member;
                        trace.member_staged_bound
                            = layout.max_member_staged_event_counts[*member];
                        trace.task_bound = trace.member_staged_bound;
                        if (layout.members != nullptr) {
                            trace.process_id = layout.members[*member].process_id;
                        }
                    }
                    if (trace.internal_commit
                        && trace.signal_slot < layout.signal_slot_count
                        && runtime.fanout_edges.size()
                            == layout.fanout_edge_count) {
                        std::uint32_t signal_edge_count { };
                        for (const auto& edge : runtime.fanout_edges) {
                            if (edge.signal_slot == trace.signal_slot) {
                                ++signal_edge_count;
                            }
                        }
                        trace.site_fanout_bound = signal_edge_count;
                    }
                    const auto staged_capacity
                        = runtime.frame.staged_event_capacity;
                    trace.over_capacity = budget_before <= staged_capacity
                        && trace.task_bound
                            > staged_capacity - budget_before;
                    return trace;
                };
                const auto print_task_trace = [&](const char* const role,
                                                  PrefixBudgetTaskTrace trace,
                                                  const bool first) {
                    trace.budget_after = first
                        ? trace.task_bound : trace.budget_before;
                    trace.budget_if_included
                        = static_cast<std::uint64_t>(trace.budget_before)
                        + trace.task_bound;
                    std::fprintf(stderr,
                        "fsim-profile: sv-region-recertification "
                        "event=native-prefix-budget-task component=%zu role=%s "
                        "type=%s payload=%llu payload_kind=%u payload_index=%llu "
                        "member=%u process=%u pending_slot=%u write_site=%u "
                        "signal_slot=%u task_bound=%u member_staged_bound=%u "
                        "global_fanout_bound=%u site_fanout_bound=%u "
                        "budget_before=%u budget_after=%u "
                        "budget_if_included=%llu staged_capacity=%u "
                        "over_capacity=%u\n",
                        component, role, trace.task_kind,
                        static_cast<unsigned long long>(trace.payload),
                        trace.payload_kind,
                        static_cast<unsigned long long>(trace.payload_index),
                        trace.member_index, trace.process_id, trace.pending_slot,
                        trace.write_site_index, trace.signal_slot,
                        trace.task_bound, trace.member_staged_bound,
                        trace.global_fanout_bound, trace.site_fanout_bound,
                        trace.budget_before, trace.budget_after,
                        static_cast<unsigned long long>(
                            trace.budget_if_included),
                        runtime.frame.staged_event_capacity,
                        static_cast<std::uint32_t>(trace.over_capacity));
                };
                const auto first_task = make_task_trace(
                    frontier.tasks[0U].payload, 0U);
                print_task_trace("first", first_task, true);
                if (task_prefix_count < offered_task_prefix_count) {
                    const auto blocked_task = make_task_trace(
                        frontier.tasks[task_prefix_count].payload,
                        staged_event_budget);
                    print_task_trace("first-blocked", blocked_task, false);
                }
            }
        }
    }

    SchedulerBatchFrontier execution_frontier = frontier;
    execution_frontier.end = task_prefix_count;
    execution_frontier.tasks = frontier.tasks.first(task_prefix_count);

    // A native step can stage fresh keys, so reserve the full certified frame
    // budget before entering generated code. Failure here is pre-mutation and
    // leaves the checked batch path available.
    const SchedulerBatchGroupKey group_key {
        frontier.generation,
        static_cast<std::uint64_t>(component) + 1U,
    };
    Scheduler::SystemVerilogGroupBatchReservation reservation;
    try {
        reservation = scheduler.reserve_systemverilog_compact_group_batch(
            SchedulerPhase::active, runtime, group_key,
            runtime.frame.staged_event_capacity);
    } catch (const std::bad_alloc&) {
        return std::nullopt;
    }
    if (!reservation) {
        return std::nullopt;
    }

    auto& authoritative_values = runtime.authoritative_state->values();
    if (!authoritative_values.try_acquire_frontier_write_lease(
            authoritative_values.revision(), runtime.writable_signals, lease,
            runtime.writable_layout_indices)) {
        reservation.cancel();
        return std::nullopt;
    }

    const auto decline_before_entry = [&](const std::source_location caller
                                             = std::source_location::current())
        -> std::optional<SchedulerBatchResult> {
        runtime.end_alias_bind_lease();
        lease.release();
        reservation.cancel();
        runtime.frame.scheduler_task_count = 0U;
        runtime.frame.scheduler_task_cursor = 0U;
        runtime.invalidate(caller);
        return std::nullopt;
    };
    const auto decline_pristine_unseeded_frame = [&]()
        -> std::optional<SchedulerBatchResult> {
        const auto& frame = runtime.frame;
        const bool pristine = !runtime.scheduler_state_seeded
            && frame.scheduler_task_count == 0U
            && frame.scheduler_task_cursor == 0U
            && frame.pending_write_count == 0U
            && frame.current_pending_write == UINT32_MAX
            && frame.staged_event_count == 0U
            && frame.committed_signal_count == 0U
            && frame.generic_update_ack_count == 0U
            && runtime.boundary_callback_started_slot
                == std::numeric_limits<std::uint32_t>::max()
            && runtime.native_member_dispatches == 0U;
        if (!pristine) {
            return decline_before_entry();
        }
        runtime.end_alias_bind_lease();
        lease.release();
        reservation.cancel();
        return std::nullopt;
    };

    const auto queued_receipt_is_valid = [&](const ProcessId process,
                                              const std::size_t member_index,
                                              const RegionReadinessQueueMember& queued) {
        return queued.component == component
            && queued.member == member_index
            && queued.generation == runtime.runtime_generation
            && queued.key_valid
            && queued.queued_key.stable_order == process
            && queued.queued_key.systemverilog_round != 0U
            && queued.queued_key.process_domain
                == static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            && queued.queued_key.phase
                == static_cast<std::uint32_t>(SchedulerPhase::active);
    };

    constexpr auto no_raw_activation
        = std::numeric_limits<std::size_t>::max();
    constexpr auto multiple_raw_activations = no_raw_activation - 1U;
    if (runtime.raw_activation_summary.size() != layout.member_count
        || runtime.raw_activation_summary.size() != runtime.members.size()
        || layout.members == nullptr
        || backend_pin->kernel.members.size() != layout.member_count
        || frontier.tasks.size() > multiple_raw_activations) {
        return decline_before_entry();
    }
    std::ranges::fill(runtime.raw_activation_summary, no_raw_activation);
    // The process-to-member map is a generation-published unique partition.
    // Validate each current component row against every retained identity
    // before using it to index task activations.
    for (std::size_t member_index = 0U;
         member_index < layout.member_count;
         ++member_index) {
        const auto process
            = backend_pin->kernel.members[member_index].process;
        if (process >= processes.size()
            || process >= region_readiness_member_index_by_process.size()
            || region_readiness_member_index_by_process[process]
                != member_index
            || layout.members[member_index].process_id != process
            || runtime.members[member_index].process_id != process) {
            return decline_before_entry();
        }
    }
    // Inspect the complete borrowed frontier, not just the selected prefix:
    // duplicate and out-of-prefix activations must retain their old effect on
    // the per-member preflight. Resolve indices only against this frontier so
    // runtime scratch retains no pointer into the borrowed task span.
    for (std::size_t task_index = 0U;
         task_index < frontier.tasks.size();
         ++task_index) {
        ProcessId process { };
        if (!decode_wave_payload(frontier.tasks[task_index].payload, process)
            || process >= region_readiness_member_index_by_process.size()) {
            continue;
        }
        const auto member_index
            = region_readiness_member_index_by_process[process];
        if (member_index >= layout.member_count
            || layout.members[member_index].process_id != process) {
            // A foreign component may have the same local ordinal. It is not
            // an activation of this component and remains ignored.
            continue;
        }
        auto& summary = runtime.raw_activation_summary[member_index];
        if (summary == no_raw_activation) {
            summary = task_index;
        } else {
            summary = multiple_raw_activations;
        }
    }
    const auto raw_activation_for_member =
        [&](const std::size_t member_index,
            const SchedulerBatchFrontierEntry*& match)
        -> std::optional<std::size_t> {
        match = nullptr;
        if (member_index >= runtime.raw_activation_summary.size()) {
            return std::nullopt;
        }
        const auto summary = runtime.raw_activation_summary[member_index];
        if (summary == no_raw_activation) {
            return 0U;
        }
        if (summary == multiple_raw_activations) {
            return 2U;
        }
        if (summary >= frontier.tasks.size()) {
            return std::nullopt;
        }
        match = &frontier.tasks[summary];
        return 1U;
    };

    // A missing key receipt can be an ordinary scheduler cut, but it must be
    // detected before binding or seeding any frame state. The checked path
    // remains authoritative until the exact task is offered or a producer
    // supplies its scheduler-authored key.
    bool has_untracked_queued_member_before_bind { };
    bool members_eligible_before_bind { true };
    // The current graph snapshot maps each process to one member and rejects
    // duplicate membership. Checking every layout row against its kernel row
    // below preserves duplicate and ordering rejection without rescanning the
    // whole layout once per process.
    if (!runtime.scheduler_state_seeded
        && !process_signal_access_inventory_complete) {
        return decline_before_entry();
    }
    for (std::size_t member_index = 0U;
         member_index < backend_pin->kernel.members.size();
         ++member_index) {
        const auto process = backend_pin->kernel.members[member_index].process;
        if (process >= processes.size()
            || process >= region_readiness_queued_by_process.size()) {
            return decline_before_entry();
        }
        if (!runtime.scheduler_state_seeded) {
            if (process >= region_graph->processes().size()
                || processes.is_compact_constant(process)
                || !process_signal_access_is_complete(process)) {
                return decline_before_entry();
            }
            if (layout.members == nullptr
                || member_index >= layout.member_count
                || process >= region_readiness_member_index_by_process.size()) {
                return decline_before_entry();
            }
            const auto mapped_member
                = region_readiness_member_index_by_process[process];
            const auto& graph_process = region_graph->processes()[process];
            if (mapped_member >= layout.member_count
                || mapped_member != member_index
                || layout.members[member_index].process_id != process
                || graph_process.process != process || !graph_process.pure
                || graph_process.dependencies_unknown
                || graph_process.cyclic_or_dependent_on_cycle
                || graph_process.scheduling_domain
                    != ProcessSchedulingDomain::systemverilog
                || graph_process.update_kind
                    != RegionUpdateKind::systemverilog_active
                || region_graph->capability_epoch(process) != 1U
                || !backend_pin->kernel.members[member_index]
                        .all_registers_definitely_defined
                || member_index / 64U >= runtime.ready_words.size()) {
                return decline_before_entry();
            }
        }
        const auto& state = get_process(process);
        if (!runtime.scheduler_state_seeded) {
            const bool member_eligible
                = systemverilog_wave_member_eligible(process)
                && process_region_kernel_eligible(process)
                && state.region_kernel_completion_has_no_persistent_registers
                && !state.waiting_on_signal && !state.suspended
                && !state.halted && state.waiting_on_static;
            members_eligible_before_bind = members_eligible_before_bind
                && member_eligible;
        }
        const auto& queued = region_readiness_queued_by_process[process];
        if (queued.generation != 0U
            && (queued.component != component
                || queued.member != member_index
                || queued.generation != runtime.runtime_generation)) {
            return decline_before_entry();
        }
        const bool receipt_valid = queued_receipt_is_valid(
            process, member_index, queued);
        if (queued.key_valid && !receipt_valid) {
            return decline_before_entry();
        }
        if (!state.queued) {
            if (queued.generation != 0U) {
                return decline_before_entry();
            }
            if (runtime.scheduler_state_seeded) {
                continue;
            }
        }
        const SchedulerBatchFrontierEntry* matching_task { };
        const auto matching_task_count
            = raw_activation_for_member(member_index, matching_task);
        if (!matching_task_count) {
            return decline_before_entry();
        }
        if (*matching_task_count > 1U) {
            return decline_before_entry();
        }
        if (!state.queued) {
            if (*matching_task_count != 0U) {
                return decline_before_entry();
            }
            continue;
        }
        if (!runtime.scheduler_state_seeded
            && *matching_task_count == 1U && matching_task != nullptr
            && queued.key_valid
            && !same_frontier_key(queued.queued_key,
                frontier_key(frontier, *matching_task))) {
            return decline_before_entry();
        }
        bool frame_key_valid { };
        if (runtime.scheduler_state_seeded
            && member_index < runtime.members.size()) {
            constexpr auto queued_flags = RegionFrontierMemberFlagsV1::queued
                | RegionFrontierMemberFlagsV1::queued_key_valid;
            const auto& member = runtime.members[member_index];
            const auto key_flags = member.flags & queued_flags;
            if (key_flags != 0U && key_flags != queued_flags) {
                return decline_before_entry();
            }
            frame_key_valid = member.process_id == process
                && key_flags == queued_flags
                && member.queued_key.stable_order == process
                && member.queued_key.systemverilog_round != 0U
                && member.queued_key.process_domain
                    == static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog)
                && member.queued_key.phase
                    == static_cast<std::uint32_t>(SchedulerPhase::active);
            if (key_flags == queued_flags && !frame_key_valid) {
                return decline_before_entry();
            }
        }
        if (*matching_task_count == 0U && !receipt_valid && !frame_key_valid) {
            has_untracked_queued_member_before_bind = true;
        }
    }
    if (has_untracked_queued_member_before_bind) {
        runtime.end_alias_bind_lease();
        lease.release();
        reservation.cancel();
        runtime.frame.scheduler_task_count = 0U;
        runtime.frame.scheduler_task_cursor = 0U;
        return std::nullopt;
    }
    if (!runtime.scheduler_state_seeded && !members_eligible_before_bind) {
        // Capability checks are repeated on each offer. When the unseeded
        // frame is untouched, use checked execution for this offer and leave
        // the runtime available if the same cohort becomes eligible later.
        return decline_pristine_unseeded_frame();
    }

    if (!runtime.bind_frame_planes_and_metadata(lease)) {
        return decline_before_entry();
    }

    auto& frame = runtime.frame;
    frame.scheduler_frontier_generation = frontier.generation;
    frame.scheduler_task_count = static_cast<std::uint32_t>(task_prefix_count);
    frame.scheduler_task_cursor = 0U;
    frame.slot.time = frontier.time;
    frame.slot.delta = frontier.delta;
    frame.slot.systemverilog_round = frontier.systemverilog_round;
    frame.slot.process_domain = static_cast<std::uint32_t>(
        ProcessSchedulingDomain::systemverilog);
    frame.slot.phase = static_cast<std::uint32_t>(SchedulerPhase::active);
    frame.cut = { };
    frame.cut.scheduler_frontier_generation = frontier.generation;
    frame.cut.kind = RegionFrontierCutKindV1::closed_prefix;
    runtime.stop_requested = scheduler.stop_requested() ? 1U : 0U;

    const auto translate_payload = [&](const std::uint64_t payload,
                                       std::uint64_t& translated) {
        ProcessId process { };
        if (!decode_wave_payload(payload, process)) {
            translated = payload;
            return true;
        }
        const auto member = member_for_process(process);
        if (!member) {
            return false;
        }
        translated = encode_region_frontier_payload_v1(
            RegionFrontierEventKindV1::member_activation, *member);
        return true;
    };

    for (std::size_t index = 0U; index < task_prefix_count; ++index) {
        const auto& source = frontier.tasks[index];
        auto& original = runtime.original_scheduler_tasks[index];
        original = source;
        auto& target = runtime.scheduler_tasks[index];
        target.stable_order = source.stable_order;
        target.sequence = source.sequence;
        if (!translate_payload(source.payload, target.payload)) {
            return decline_before_entry();
        }
    }

    const auto activation_is_valid = [&](const ProcessId process,
                                         const SchedulerBatchFrontierEntry& task,
                                         const SchedulerBatchFrontier& task_frontier) {
        const auto member_index = member_for_process(process);
        if (!member_index || process >= processes.size()) {
            return false;
        }
        const auto& member = runtime.members[*member_index];
        auto& state = get_process(process);
        const auto key = frontier_key(task_frontier, task);
        if (member.process_id != process || !state.queued
            || (!state.waiting_on_static && !state.queued)
            || state.waiting_on_signal
            || state.suspended || state.halted
            || !state.region_kernel_completion_has_no_persistent_registers
            || !backend_pin->kernel.members[*member_index]
                    .all_registers_definitely_defined
            || (member.flags & RegionFrontierMemberFlagsV1::executing) != 0U) {
            return false;
        }
        if (process >= region_readiness_queued_by_process.size()) {
            return false;
        }
        const auto& queued_sidecar
            = region_readiness_queued_by_process[process];
        if (queued_sidecar.generation != 0U
            && (queued_sidecar.component != component
                || queued_sidecar.member != *member_index
                || queued_sidecar.generation != runtime.runtime_generation)) {
            return false;
        }
        if (queued_sidecar.key_valid
            && !same_frontier_key(queued_sidecar.queued_key, key)) {
            return false;
        }
        constexpr auto queued_flags
            = RegionFrontierMemberFlagsV1::queued
            | RegionFrontierMemberFlagsV1::queued_key_valid;
        const auto present_flags = member.flags & queued_flags;
        if (present_flags == queued_flags) {
            if (!same_frontier_key(member.queued_key, key)) {
                return false;
            }
        } else if (present_flags != 0U
            || (member.flags
                    & RegionFrontierMemberFlagsV1::pending_activation)
                != 0U) {
            return false;
        }
        const auto word = static_cast<std::size_t>(*member_index / 64U);
        return word < runtime.ready_words.size();
    };
    const auto read_activation = [&](const ProcessId process,
                                     const SchedulerBatchFrontierEntry& task,
                                     const SchedulerBatchFrontier& task_frontier,
                                     const bool initial_seed) {
        if (!activation_is_valid(process, task, task_frontier)) {
            return false;
        }
        const auto member_index = member_for_process(process);
        if (!member_index) {
            return false;
        }
        runtime.mark_member_sync_write(static_cast<std::uint32_t>(*member_index));
        auto& member = runtime.members[*member_index];
        auto& state = get_process(process);
        const auto key = frontier_key(task_frontier, task);
        constexpr auto queued_flags
            = RegionFrontierMemberFlagsV1::queued
            | RegionFrontierMemberFlagsV1::queued_key_valid;
        const auto present_flags = member.flags & queued_flags;
        member.static_trigger_mask |= state.static_trigger_mask;
        const auto& queued_sidecar
            = region_readiness_queued_by_process[process];
        member.static_trigger_mask |= queued_sidecar.static_trigger_mask;
        if (present_flags == queued_flags) {
            runtime.ready_words[*member_index / 64U]
                |= UINT64_C(1) << (*member_index % 64U);
            return true;
        }
        member.flags |= RegionFrontierMemberFlagsV1::waiting_on_static
            | queued_flags;
        member.queued_key = key;
        if (initial_seed) {
            member.activation_origin = key;
        }
        auto& sidecar = region_readiness_queued_by_process[process];
        sidecar.component = component;
        sidecar.member = *member_index;
        sidecar.generation = runtime.runtime_generation;
        sidecar.queued_key = key;
        sidecar.key_valid = true;
        sidecar.static_trigger_mask = member.static_trigger_mask;
        runtime.ready_words[*member_index / 64U]
            |= UINT64_C(1) << (*member_index % 64U);
        return true;
    };
    const auto import_queued_receipt = [&](const ProcessId process,
                                            const bool initial_seed) {
        const auto member_index = member_for_process(process);
        if (!member_index || process >= processes.size()
            || process >= region_readiness_queued_by_process.size()) {
            return false;
        }
        const auto& queued = region_readiness_queued_by_process[process];
        if (!queued_receipt_is_valid(process, *member_index, queued)) {
            return false;
        }
        auto& state = get_process(process);
        runtime.mark_member_sync_write(static_cast<std::uint32_t>(*member_index));
        auto& member = runtime.members[*member_index];
        if (member.process_id != process || !state.queued
            || !state.waiting_on_static || state.waiting_on_signal
            || state.suspended || state.halted
            || !state.region_kernel_completion_has_no_persistent_registers
            || !backend_pin->kernel.members[*member_index]
                    .all_registers_definitely_defined
            || (member.flags & (RegionFrontierMemberFlagsV1::executing
                                   | RegionFrontierMemberFlagsV1::pending_activation))
                != 0U
            || *member_index / 64U >= runtime.ready_words.size()) {
            return false;
        }
        constexpr auto queued_flags
            = RegionFrontierMemberFlagsV1::queued
            | RegionFrontierMemberFlagsV1::queued_key_valid;
        const auto present_flags = member.flags & queued_flags;
        if (present_flags == queued_flags) {
            if (!same_frontier_key(member.queued_key, queued.queued_key)) {
                return false;
            }
        } else if (present_flags != 0U) {
            return false;
        } else {
            member.flags |= queued_flags;
            member.queued_key = queued.queued_key;
        }
        member.flags |= RegionFrontierMemberFlagsV1::waiting_on_static;
        member.static_trigger_mask |= state.static_trigger_mask
            | queued.static_trigger_mask;
        if (initial_seed) {
            member.activation_origin = queued.queued_key;
        }
        runtime.ready_words[*member_index / 64U]
            |= UINT64_C(1) << (*member_index % 64U);
        return true;
    };

    if (!runtime.scheduler_state_seeded) {
        runtime.require_full_member_sync(FrontierMemberSyncFullReason::seed);
        // Check the component before seeding any frame member. An ordinary
        // activation outside this native prefix is importable only when its
        // exact scheduler key was retained by a producer receipt.
        bool has_untracked_queued_member { };
        for (std::size_t member_index = 0U;
             member_index < backend_pin->kernel.members.size();
             ++member_index) {
            const auto process
                = backend_pin->kernel.members[member_index].process;
            if (process >= processes.size()
                || process >= region_readiness_queued_by_process.size()
                || process >= region_readiness_member_index_by_process.size()
                || layout.members == nullptr
                || member_index >= layout.member_count) {
                return decline_before_entry();
            }
            const auto mapped_member
                = region_readiness_member_index_by_process[process];
            if (mapped_member >= layout.member_count
                || mapped_member != member_index
                || layout.members[member_index].process_id != process
                || !systemverilog_wave_member_eligible(process)
                || !process_region_kernel_eligible(process)
                || !backend_pin->kernel.members[member_index]
                        .all_registers_definitely_defined
                || member_index / 64U >= runtime.ready_words.size()) {
                return decline_before_entry();
            }
            const auto& state = get_process(process);
            if (!state.region_kernel_completion_has_no_persistent_registers
                || state.waiting_on_signal || state.suspended || state.halted
                || !state.waiting_on_static) {
                return decline_before_entry();
            }

            const SchedulerBatchFrontierEntry* matching_task { };
            const auto matching_task_count
                = raw_activation_for_member(member_index, matching_task);
            if (!matching_task_count) {
                return decline_before_entry();
            }
            const auto& queued
                = region_readiness_queued_by_process[process];
            if (queued.generation != 0U
                && (queued.component != component
                    || queued.member != member_index
                    || queued.generation != runtime.runtime_generation)) {
                return decline_before_entry();
            }
            if (state.queued) {
                if (*matching_task_count > 1U) {
                    return decline_before_entry();
                }
                if (*matching_task_count == 0U) {
                    has_untracked_queued_member
                        = has_untracked_queued_member
                        || !queued_receipt_is_valid(
                            process, member_index, queued);
                } else if (queued.key_valid
                    && !same_frontier_key(queued.queued_key,
                        frontier_key(frontier, *matching_task))) {
                    return decline_before_entry();
                }
            } else if (*matching_task_count != 0U) {
                return decline_before_entry();
            } else if (queued.generation != 0U) {
                return decline_before_entry();
            }
        }
        if (has_untracked_queued_member) {
            runtime.clear_canonical_values_binding_receipt();
            runtime.end_alias_bind_lease();
            lease.release();
            reservation.cancel();
            frame.scheduler_task_count = 0U;
            frame.scheduler_task_cursor = 0U;
            return std::nullopt;
        }

        std::ranges::fill(runtime.ready_words, UINT64_C(0));
        for (std::size_t member_index = 0U;
             member_index < backend_pin->kernel.members.size();
             ++member_index) {
            const auto process = backend_pin->kernel.members[member_index].process;
            if (process >= processes.size()
                || process >= region_readiness_queued_by_process.size()
                || layout.members[member_index].process_id != process
                || !systemverilog_wave_member_eligible(process)
                || !process_region_kernel_eligible(process)
                || !backend_pin->kernel.members[member_index]
                        .all_registers_definitely_defined) {
                return decline_before_entry();
            }
            const auto& state = get_process(process);
            if (!state.region_kernel_completion_has_no_persistent_registers
                || state.waiting_on_signal || state.suspended || state.halted
                || !state.waiting_on_static) {
                return decline_before_entry();
            }
            auto& member = runtime.members[member_index];
            member.process_id = process;
            member.flags = RegionFrontierMemberFlagsV1::waiting_on_static;
            member.static_trigger_mask = state.static_trigger_mask;
            const SchedulerBatchFrontierEntry* matching_task { };
            const auto matching_task_count
                = raw_activation_for_member(member_index, matching_task);
            if (!matching_task_count) {
                return decline_before_entry();
            }
            if (state.queued) {
                if (*matching_task_count == 1U && matching_task != nullptr) {
                    if (!read_activation(process, *matching_task,
                            frontier, true)) {
                        return decline_before_entry();
                    }
                } else if (*matching_task_count == 0U
                    && !import_queued_receipt(process, true)) {
                    return decline_before_entry();
                } else if (*matching_task_count > 1U) {
                    return decline_before_entry();
                }
            } else if (*matching_task_count != 0U) {
                return decline_before_entry();
            }
        }
        runtime.scheduler_state_seeded = true;
    } else {
        // Authenticate queued state across the whole component before any
        // frame member is changed. Host fanout may enqueue a member outside
        // this native prefix; the producer receipt carries its exact key.
        bool has_untracked_queued_member { };
        for (std::size_t member_index = 0U;
             member_index < runtime.members.size();
             ++member_index) {
            const auto process = runtime.members[member_index].process_id;
            if (process >= processes.size()
                || process >= region_readiness_queued_by_process.size()) {
                return decline_before_entry();
            }
            const auto& state = get_process(process);
            const auto& member = runtime.members[member_index];
            const auto& queued
                = region_readiness_queued_by_process[process];
            const SchedulerBatchFrontierEntry* matching_task { };
            const auto matching_task_count
                = raw_activation_for_member(member_index, matching_task);
            if (!matching_task_count) {
                return decline_before_entry();
            }
            constexpr auto queued_flags
                = RegionFrontierMemberFlagsV1::queued
                | RegionFrontierMemberFlagsV1::queued_key_valid;
            const auto member_queue_state = member.flags & queued_flags;
            if ((queued.generation != 0U
                    && (queued.component != component
                        || queued.member != member_index
                        || queued.generation != runtime.runtime_generation))
                || *matching_task_count > 1U) {
                return decline_before_entry();
            }
            if (!state.queued) {
                if (*matching_task_count != 0U || member_queue_state != 0U
                    || queued.generation != 0U) {
                    return decline_before_entry();
                }
                continue;
            }

            if (*matching_task_count == 0U) {
                const bool frame_key_valid = member_queue_state == queued_flags
                    && member.queued_key.stable_order == process
                    && member.queued_key.systemverilog_round != 0U
                    && member.queued_key.process_domain
                        == static_cast<std::uint32_t>(
                            ProcessSchedulingDomain::systemverilog)
                    && member.queued_key.phase
                        == static_cast<std::uint32_t>(SchedulerPhase::active);
                const bool receipt_valid = queued_receipt_is_valid(
                    process, member_index, queued);
                if ((member_queue_state != 0U
                        && member_queue_state != queued_flags)
                    || (member_queue_state == queued_flags
                        && !frame_key_valid)
                    || (queued.key_valid && !receipt_valid)) {
                    return decline_before_entry();
                }
                if (!frame_key_valid && !receipt_valid) {
                    has_untracked_queued_member = true;
                } else if (frame_key_valid && receipt_valid
                    && !same_frontier_key(
                        member.queued_key, queued.queued_key)) {
                    return decline_before_entry();
                }
                continue;
            }

            if (!activation_is_valid(process, *matching_task, frontier)) {
                return decline_before_entry();
            }
            const auto key = frontier_key(frontier, *matching_task);
            if (member_queue_state == queued_flags
                && !same_frontier_key(member.queued_key, key)) {
                return decline_before_entry();
            }
            if (queued.key_valid
                && !same_frontier_key(queued.queued_key, key)) {
                return decline_before_entry();
            }
        }
        if (has_untracked_queued_member) {
            runtime.clear_canonical_values_binding_receipt();
            runtime.end_alias_bind_lease();
            lease.release();
            reservation.cancel();
            frame.scheduler_task_count = 0U;
            frame.scheduler_task_cursor = 0U;
            return std::nullopt;
        }

        // Apply raw activations and receipt imports only after every member
        // has passed validation. Keep activation_origin until the generated
        // loop consumes the authenticated queued key.
        for (std::size_t task_index = 0U;
             task_index < frontier.tasks.size(); ++task_index) {
            ProcessId process { };
            if (!decode_wave_payload(
                    frontier.tasks[task_index].payload, process)) {
                continue;
            }
            if (!member_for_process(process)) {
                continue;
            }
            if (!read_activation(process, frontier.tasks[task_index],
                    frontier, false)) {
                return decline_before_entry();
            }
        }
        for (std::size_t member_index = 0U;
             member_index < runtime.members.size(); ++member_index) {
            const auto process = runtime.members[member_index].process_id;
            const SchedulerBatchFrontierEntry* matching_task { };
            const auto matching_task_count
                = raw_activation_for_member(member_index, matching_task);
            if (!matching_task_count) {
                return decline_before_entry();
            }
            if (*matching_task_count == 0U
                && get_process(process).queued
                && (runtime.members[member_index].flags
                        & RegionFrontierMemberFlagsV1::queued) == 0U
                && !import_queued_receipt(process, false)) {
                return decline_before_entry();
            }
        }
    }

    for (std::size_t index = 0U; index < task_prefix_count; ++index) {
        if (runtime.original_scheduler_tasks[index].payload
            != frontier.tasks[index].payload
            || runtime.scheduler_tasks[index].stable_order
                != frontier.tasks[index].stable_order
            || runtime.scheduler_tasks[index].sequence
                != frontier.tasks[index].sequence) {
            return decline_before_entry();
        }
    }

    const auto checked_entry = backend_pin->executor->step_entry();
    if (checked_entry == nullptr) {
        return decline_before_entry();
    }
    const auto* const trusted_capability
        = dynamic_cast<const detail::RegionFrontierTrustedEntryCapability*>(
              backend_pin->executor.get());
    const auto trusted_view = trusted_capability != nullptr
        ? trusted_capability->trusted_entry()
        : detail::RegionFrontierTrustedEntryView { };
    const bool trusted_entry_available = trusted_view.entry != nullptr
        && trusted_view.layout == &layout
        && runtime.alias_certificate_storage_available;
    const auto* const canonical_values_capability
        = dynamic_cast<const detail::RegionFrontierCanonicalValuesEntryCapability*>(
              backend_pin->executor.get());
    const auto canonical_values_view = canonical_values_capability != nullptr
        ? canonical_values_capability->canonical_values_entries()
        : detail::RegionFrontierCanonicalValuesEntryView { };
    const bool canonical_values_view_matches =
        canonical_values_capability != nullptr
        && canonical_values_view.layout == &layout;
    const auto canonical_values_entry = canonical_values_view_matches
        ? canonical_values_view.canonical_values_entry : nullptr;
    const auto alias_and_canonical_values_entry
        = canonical_values_view_matches
        ? canonical_values_view.alias_and_canonical_values_entry : nullptr;
    const auto* const descriptor_shapes_capability
        = dynamic_cast<const detail::RegionFrontierDescriptorShapesEntryCapability*>(
            backend_pin->executor.get());
    const auto descriptor_shapes_view = descriptor_shapes_capability != nullptr
        ? descriptor_shapes_capability->descriptor_shapes_entry()
        : detail::RegionFrontierDescriptorShapesEntryView { };
    const auto descriptor_shapes_entry = descriptor_shapes_view.layout == &layout
        ? descriptor_shapes_view.entry : nullptr;
    const auto* const member_sync_capability
        = dynamic_cast<const detail::RegionFrontierMemberSyncEntryCapability*>(
              backend_pin->executor.get());
    const auto member_sync_view = member_sync_capability != nullptr
        ? member_sync_capability->member_sync_entry()
        : detail::RegionFrontierMemberSyncEntryView { };
    const bool member_sync_view_matches = member_sync_view.entry != nullptr
        && member_sync_view.layout == &layout
        && member_sync_view.entry == alias_and_canonical_values_entry
        && layout.execution_mode
            == RegionFrontierExecutionModeV2::systemverilog_active;
    const bool descriptor_shapes_member_sync_view_matches
        = descriptor_shapes_entry != nullptr
        && member_sync_view.layout == &layout
        && member_sync_view.descriptor_shapes_entry == descriptor_shapes_entry
        && layout.execution_mode
            == RegionFrontierExecutionModeV2::systemverilog_active;
    const bool alias_only_available = trusted_entry_available;

    const auto dispatch_count_before = runtime.native_member_dispatches;
    struct NativeFrontierDispatchProfile {
        bool enabled;
        std::uint64_t& profile_count;
        std::uint64_t& dispatch_count;
        std::uint64_t count_before;

        ~NativeFrontierDispatchProfile()
        {
            if (enabled) {
                profile_count += dispatch_count - count_before;
            }
        }
    } profile_dispatches { systemverilog_wave_profile_enabled,
        systemverilog_wave_profile_native_frontier_member_dispatches,
        runtime.native_member_dispatches, dispatch_count_before };
    SchedulerBatchResult result;
    bool native_mutation_seen { };
    bool staged_events_issued { };
    const auto fail_stop = [&](const std::size_t consumed,
                               const char* message,
                               const std::source_location caller
                                   = std::source_location::current()) {
        runtime.invalidate(caller);
        result.executed = std::min(consumed, task_prefix_count);
        if (native_mutation_seen && result.executed == 0U
            && task_prefix_count != 0U) {
            // Entry starts from cursor zero with empty event/transaction logs.
            // Any new staged effect at cursor zero therefore belongs to the
            // first offered task, which must be retired to prevent replay.
            result.executed = 1U;
        }
        try {
            throw std::logic_error { message };
        } catch (...) {
            result.failure = std::current_exception();
        }
        return result;
    };
    for (;;) {
        // The receipt is minted by the most recent successful bind and is
        // consumed once per generated call. Boundary publication can rebind
        // before continuing this loop, so do not cache its result outside.
        bool descriptor_shapes_binding_valid { };
        const bool canonical_values_binding_valid
            = runtime.consume_canonical_values_binding_receipt(
                *backend_pin, layout, canonical_values_entry,
                alias_and_canonical_values_entry, lease,
                descriptor_shapes_entry, &descriptor_shapes_binding_valid);
        const bool canonical_values_only_available
            = canonical_values_binding_valid
            && canonical_values_entry != nullptr;
        const bool alias_and_canonical_values_available
            = canonical_values_binding_valid
            && alias_and_canonical_values_entry != nullptr;
        const bool will_use_alias_and_canonical
            = alias_and_canonical_values_available;
        const bool will_use_descriptor_shapes
            = will_use_alias_and_canonical && descriptor_shapes_binding_valid;
        const auto alias_geometry_entry
            = will_use_descriptor_shapes ? descriptor_shapes_entry
            : will_use_alias_and_canonical
                ? alias_and_canonical_values_entry
                : (alias_only_available ? trusted_view.entry : nullptr);
        const bool alias_geometry_entry_available
            = alias_geometry_entry != nullptr
            && runtime.alias_certificate_storage_available;
        if (!alias_geometry_entry_available) {
            runtime.clear_alias_certificate();
        }
        const auto alias_context = runtime.make_alias_certificate_context(
            *backend_pin, checked_entry,
            alias_geometry_entry_available ? alias_geometry_entry : nullptr);
        const bool certificate_matches = alias_geometry_entry_available
            && runtime.alias_certificate_matches(alias_context,
                will_use_alias_and_canonical);
        bool use_alias_geometry = certificate_matches
            && frame.staged_event_count == 0U;
        bool certificate_confirmation_pending { };
        if (!use_alias_geometry && alias_geometry_entry_available
            && frame.staged_event_count == 0U) {
            certificate_confirmation_pending
                = runtime.stage_alias_certificate(alias_context);
            if (certificate_confirmation_pending) {
                use_alias_geometry
                    = runtime.prove_alias_geometry_sorted();
            }
        }
        const bool use_alias_and_canonical_values
            = will_use_alias_and_canonical && use_alias_geometry;
        const bool use_alias_only
            = alias_only_available && !use_alias_and_canonical_values
            && use_alias_geometry;
        const bool use_canonical_values_only
            = canonical_values_only_available
            && !use_alias_and_canonical_values && !use_alias_only;
        const bool selected_alias_geometry
            = use_alias_and_canonical_values || use_alias_only;
        const bool use_descriptor_shapes
            = will_use_descriptor_shapes && use_alias_and_canonical_values;
        const auto selected_entry = use_descriptor_shapes
            ? descriptor_shapes_entry
            : use_alias_and_canonical_values
            ? alias_and_canonical_values_entry
            : use_canonical_values_only ? canonical_values_entry
            : use_alias_only ? trusted_view.entry : checked_entry;
        const char* const selected_route = use_descriptor_shapes
            ? "alias+canonical+descriptor-shapes"
            : use_alias_and_canonical_values
            ? "alias+canonical"
            : use_canonical_values_only ? "canonical-only"
            : use_alias_only ? "alias-only" : "checked";
        if (systemverilog_wave_profile_enabled) {
            if (selected_alias_geometry) {
                ++systemverilog_wave_profile_alias_trusted_entries;
            } else {
                ++systemverilog_wave_profile_alias_checked_entries;
                if (!alias_only_available
                    && !alias_and_canonical_values_available) {
                    ++systemverilog_wave_profile_alias_unavailable_entries;
                }
                if (frame.staged_event_count != 0U) {
                    ++systemverilog_wave_profile_alias_forced_staged_entries;
                }
            }
            if (use_canonical_values_only) {
                ++systemverilog_wave_profile_canonical_values_only_entries;
            } else if (use_alias_and_canonical_values) {
                ++systemverilog_wave_profile_alias_and_canonical_values_entries;
            }
        }
        if (use_descriptor_shapes) {
            ++runtime.descriptor_shapes_entries;
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_descriptor_shapes_entries;
            }
        }
        if (selected_alias_geometry) {
            ++runtime.alias_trusted_entries;
        } else {
            ++runtime.alias_checked_entries;
        }
        if (use_canonical_values_only) {
            ++runtime.canonical_values_only_entries;
        } else if (use_alias_and_canonical_values) {
            ++runtime.alias_and_canonical_values_entries;
        }
        if (systemverilog_wave_profile_enabled
            && (systemverilog_wave_profile_native_frontier_alias_entry_rows
                    < 16U
                || (selected_alias_geometry
                    && !systemverilog_wave_profile_native_frontier_alias_trusted_entry_seen))) {
            ++systemverilog_wave_profile_native_frontier_alias_entry_rows;
            if (selected_alias_geometry) {
                systemverilog_wave_profile_native_frontier_alias_trusted_entry_seen
                    = true;
            }
            std::fprintf(stderr,
                "fsim-profile: sv-frontier-alias-entry component=%zu "
                "offered=%zu selected=%s pre_staged=%u checked_total=%llu "
                "trusted_total=%llu canonical_only_total=%llu "
                "alias_canonical_total=%llu certificate_valid=%u "
                "host_geometry_attempts=%llu host_geometry_successes=%llu "
                "host_geometry_failures=%llu\n",
                component, task_prefix_count, selected_route,
                frame.staged_event_count,
                static_cast<unsigned long long>(runtime.alias_checked_entries),
                static_cast<unsigned long long>(runtime.alias_trusted_entries),
                static_cast<unsigned long long>(
                    runtime.canonical_values_only_entries),
                static_cast<unsigned long long>(
                    runtime.alias_and_canonical_values_entries),
                static_cast<unsigned>(runtime.alias_certificate_valid),
                static_cast<unsigned long long>(
                    runtime.alias_sorted_proof_attempts),
                static_cast<unsigned long long>(
                    runtime.alias_sorted_proof_successes),
                static_cast<unsigned long long>(
                    runtime.alias_sorted_proof_failures));
        }
        runtime.member_sync_private_entry
            = ((use_descriptor_shapes
                   && descriptor_shapes_member_sync_view_matches)
                  || (use_alias_and_canonical_values && !use_descriptor_shapes
                      && member_sync_view_matches))
            && runtime.member_sync_workset_available;
        const auto native_cursor_before = frame.scheduler_task_cursor;
        RegionFrontierStatusV2 status { };
        try {
            status = selected_entry(&frame);
        } catch (...) {
            runtime.clear_alias_certificate();
            runtime.clear_canonical_values_binding_receipt();
            throw;
        }
        if (status != RegionFrontierStatusV2::decline_before_mutation
            && status != RegionFrontierStatusV2::stale_generation
            && runtime.member_sync_private_entry) {
            runtime.collect_member_sync_writes(native_cursor_before);
        } else {
            runtime.require_full_member_sync(
                status == RegionFrontierStatusV2::decline_before_mutation
                        || status == RegionFrontierStatusV2::stale_generation
                    ? FrontierMemberSyncFullReason::uncertain_status_or_generation
                    : FrontierMemberSyncFullReason::nonprivate_entry);
        }
        if (certificate_confirmation_pending) {
            const bool confirmed
                = runtime.confirm_alias_certificate(alias_context, status);
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_alias_confirmation_attempts;
                if (confirmed) {
                    ++systemverilog_wave_profile_alias_confirmation_successes;
                } else {
                    ++systemverilog_wave_profile_alias_confirmation_failures;
                }
            }
        }
        if (selected_alias_geometry
            && (status == RegionFrontierStatusV2::decline_before_mutation
                || status == RegionFrontierStatusV2::stale_generation)) {
            runtime.clear_alias_certificate();
        }
        const auto consumed
            = static_cast<std::size_t>(frame.scheduler_task_cursor);
        native_mutation_seen = native_mutation_seen || consumed != 0U
            || runtime.native_member_dispatches != dispatch_count_before
            || frame.committed_signal_count != 0U
            || frame.staged_event_count != 0U;
        if (consumed > task_prefix_count
            || frame.scheduler_task_count != task_prefix_count
            || frame.scheduler_frontier_generation != frontier.generation) {
            return fail_stop(task_prefix_count,
                "native region frontier returned an invalid cursor");
        }

        if (status == RegionFrontierStatusV2::decline_before_mutation
            || status == RegionFrontierStatusV2::stale_generation) {
            if (native_mutation_seen || frame.staged_event_count != 0U) {
                return fail_stop(consumed,
                    "native region frontier declined after consuming work");
            }
            if (systemverilog_wave_profile_enabled) {
                std::size_t member_binding_mismatches { };
                std::size_t signal_binding_mismatches { };
                const bool member_bindings_present = layout.members != nullptr;
                const bool signal_bindings_present = layout.signals != nullptr;
                if (member_bindings_present) {
                    for (std::size_t index = 0U;
                         index < runtime.members.size(); ++index) {
                        member_binding_mismatches += runtime.members[index].process_id
                            != layout.members[index].process_id;
                    }
                }
                if (signal_bindings_present) {
                    for (std::size_t index = 0U;
                         index < runtime.planes.size(); ++index) {
                        const auto& actual = runtime.planes[index];
                        const auto& expected = layout.signals[index];
                        signal_binding_mismatches += actual.signal_id != expected.signal_id
                            || actual.owner_process_id != expected.owner_process_id
                            || actual.value_kind != expected.value_kind
                            || actual.width != expected.width
                            || actual.word_count != expected.word_count
                            || actual.plane_count != expected.plane_count
                            || actual.flags != expected.flags
                            || actual.metadata_index != expected.metadata_index;
                    }
                }
                std::fprintf(stderr,
                    "fsim-profile: sv-region-recertification "
                    "event=native-entry-rejection component=%zu status=%u "
                    "status_name=%s runtime_generation=%llu "
                    "expected_runtime_generation=%llu frame_generation=%llu "
                    "bound_generation=%llu certificate_generation=%llu "
                    "expected_certificate_generation=%llu "
                    "component_generation=%llu expected_component_generation=%llu "
                    "scheduler_generation=%llu expected_scheduler_generation=%llu "
                    "cut_generation=%llu cut_kind=%u task_count=%u task_cursor=%u "
                    "pending=%u staged=%u committed=%u current_member=%u "
                    "current_pending=%u current_changed=%u saved_body_pc=%u "
                    "generic_ack=%u slot_domain=%u slot_phase=%u "
                    "member_bindings_present=%u member_binding_mismatches=%zu "
                    "signal_bindings_present=%u signal_binding_mismatches=%zu\n",
                    component, static_cast<unsigned>(status),
                    status == RegionFrontierStatusV2::stale_generation
                        ? "stale_generation" : "decline_before_mutation",
                    static_cast<unsigned long long>(runtime.runtime_generation),
                    static_cast<unsigned long long>(region_runtime_generation),
                    static_cast<unsigned long long>(frame.runtime_generation),
                    static_cast<unsigned long long>(frame.bound_runtime_generation),
                    static_cast<unsigned long long>(frame.certificate_generation),
                    static_cast<unsigned long long>(layout.certificate_generation),
                    static_cast<unsigned long long>(frame.component_generation),
                    static_cast<unsigned long long>(layout.component_generation),
                    static_cast<unsigned long long>(frame.scheduler_frontier_generation),
                    static_cast<unsigned long long>(frontier.generation),
                    static_cast<unsigned long long>(frame.cut.scheduler_frontier_generation),
                    static_cast<unsigned>(frame.cut.kind),
                    frame.scheduler_task_count, frame.scheduler_task_cursor,
                    frame.pending_write_count, frame.staged_event_count,
                    frame.committed_signal_count, frame.current_member,
                    frame.current_pending_write, frame.current_commit_changed,
                    frame.saved_body_pc, frame.generic_update_ack_count,
                    frame.slot.process_domain, frame.slot.phase,
                    static_cast<unsigned>(member_bindings_present),
                    member_binding_mismatches,
                    static_cast<unsigned>(signal_bindings_present),
                    signal_binding_mismatches);
            }
            runtime.invalidate();
            runtime.end_alias_bind_lease();
            lease.release();
            reservation.cancel();
            return std::nullopt;
        }

        if (status == RegionFrontierStatusV2::boundary_publication) {
            runtime.synchronize_committed_state(lease);
            if (runtime.invalidated) {
                return fail_stop(consumed,
                    "native region frontier commit log was invalid");
            }
            if (frame.staged_event_count != 0U) {
                if (!runtime.commit_staged_events(reservation,
                        std::static_pointer_cast<void>(shared_runtime),
                        execution_frontier)) {
                    runtime.invalidate();
                    return fail_stop(consumed,
                        "native region frontier failed to issue staged keys");
                }
                staged_events_issued = true;
            }
            if (!synchronize_frontier_process_states(runtime)) {
                return fail_stop(consumed,
                    "native region frontier returned invalid member state");
            }
            const auto cursor = static_cast<std::size_t>(
                frame.scheduler_task_cursor);
            if (cursor >= execution_frontier.tasks.size()) {
                return fail_stop(cursor,
                    "native region frontier boundary has no scheduler task");
            }
            const auto task = execution_frontier.tasks[cursor];
            const auto pending_slot = frame.current_pending_write;
            // Do not hold the scheduler's single outstanding reservation
            // across checked publication: commit_driver may reserve ordinary
            // fanout tickets itself.
            reservation.cancel();
            // Unknown observer/provider effects in publication cannot inherit
            // a selected-set proof. The next synchronization is conservative.
            try {
                runtime.publish_boundary_commit(task, pending_slot, lease);
            } catch (...) {
                const bool callback_started
                    = runtime.boundary_callback_started_slot == pending_slot;
                const bool acknowledged
                    = pending_slot < runtime.pending_writes.size()
                    && (runtime.pending_writes[pending_slot].flags
                        & pending_committed) != 0U;
                runtime.invalidate();
                result.executed = cursor
                    + static_cast<std::size_t>(
                        callback_started || acknowledged);
                result.failure = std::current_exception();
                return result;
            }
            bool component_still_eligible { };
            try {
                component_still_eligible
                    = region_local_wave_component_eligible(
                        component, backend_pin->kernel);
            } catch (...) {
                const bool callback_completed
                    = pending_slot < runtime.pending_writes.size()
                    && (runtime.pending_writes[pending_slot].flags
                        & pending_committed) != 0U;
                runtime.invalidate();
                result.executed = cursor
                    + static_cast<std::size_t>(callback_completed);
                result.failure = std::current_exception();
                return result;
            }
            if (runtime.invalidated || !region_graph
                || !region_graph->component_epochs_current(component)
                || !component_still_eligible
                || component >= region_authoritative_state_by_component.size()
                || region_authoritative_state_by_component[component]
                    != runtime.authoritative_state
                || !runtime.authoritative_state->valid()) {
                result.executed = cursor;
                return result;
            }
            if (staged_events_issued) {
                // This reservation issued the consumed prefix's pending work.
                // The acknowledged boundary and remaining scheduler suffix
                // now take the checked path rather than entering native code
                // without a fresh pre-mutation reservation.
                result.executed = cursor;
                return result;
            }
            if (scheduler.stop_requested()) {
                result.executed = cursor;
                return result;
            }
            try {
                reservation
                    = scheduler.reserve_systemverilog_compact_group_batch(
                        SchedulerPhase::active, runtime, group_key,
                        runtime.frame.staged_event_capacity);
            } catch (...) {
                // Native work and the checked boundary callback have already
                // completed. Let the scheduler retire the acknowledged
                // boundary through the checked fallback path; do not unwind
                // through the batch executor and replay the consumed prefix.
                runtime.invalidate();
                result.executed = cursor;
                return result;
            }
            if (!reservation) {
                result.executed = cursor;
                return result;
            }
            auto& values = runtime.authoritative_state->values();
            if (!values.try_acquire_frontier_write_lease(
                    values.revision(), runtime.writable_signals, lease,
                    runtime.writable_layout_indices)
                || !runtime.bind_frame_planes_and_metadata(lease)) {
                runtime.invalidate();
                reservation.cancel();
                result.executed = cursor;
                return result;
            }
            runtime.stop_requested = scheduler.stop_requested() ? 1U : 0U;
            continue;
        }

        runtime.synchronize_committed_state(lease);
        if (runtime.invalidated) {
            return fail_stop(consumed,
                "native region frontier commit log was invalid");
        }
        if (!synchronize_frontier_process_states(runtime)) {
            return fail_stop(consumed,
                "native region frontier returned invalid member state");
        }

        if (frame.staged_event_count != 0U) {
            if (status != RegionFrontierStatusV2::need_scheduler_keys
                && status != RegionFrontierStatusV2::yield_before_task
                && status != RegionFrontierStatusV2::cut_before_key
                && status != RegionFrontierStatusV2::stopped
                && status != RegionFrontierStatusV2::quiescent) {
                return fail_stop(consumed,
                    "native region frontier staged keys with an invalid status");
            }
            if (!runtime.commit_staged_events(reservation,
                    std::static_pointer_cast<void>(shared_runtime),
                    execution_frontier)) {
                runtime.invalidate();
                return fail_stop(consumed,
                    "native region frontier failed to issue staged keys");
            }
            staged_events_issued = true;
            if (!synchronize_frontier_process_states(runtime)) {
                return fail_stop(consumed,
                    "native region frontier returned invalid issued state");
            }
        }

        switch (status) {
        case RegionFrontierStatusV2::need_scheduler_keys:
        case RegionFrontierStatusV2::cut_before_key:
        case RegionFrontierStatusV2::yield_before_task:
        case RegionFrontierStatusV2::quiescent:
        case RegionFrontierStatusV2::stopped:
            result.executed = static_cast<std::size_t>(
                frame.scheduler_task_cursor);
            if (result.executed > task_prefix_count) {
                return fail_stop(task_prefix_count,
                    "native region frontier returned an invalid consumed prefix");
            }
            return result;
        case RegionFrontierStatusV2::boundary_publication:
        case RegionFrontierStatusV2::decline_before_mutation:
        case RegionFrontierStatusV2::stale_generation:
            break;
        default:
            return fail_stop(consumed,
                "native region frontier returned an unknown status");
        }
        return fail_stop(consumed,
            "native region frontier status escaped dispatch");
    }
}

bool Interpreter::Impl::synchronize_frontier_process_states(
    RegionFrontierComponentRuntime& runtime) noexcept
{
    if (runtime.owner != this || !runtime.backend
        || !runtime.backend->executor || !runtime.frame_initialized
        || runtime.invalidated || !region_graph
        || runtime.runtime_generation != region_runtime_generation
        || runtime.component
            >= region_graph->certificate_inventory().components.size()
        || !region_graph->component_epochs_current(runtime.component)) {
        return false;
    }
    const auto& kernel = runtime.backend->kernel;
    const auto& layout = runtime.backend->executor->layout();
    const auto descriptor_index = runtime.component;
    if (kernel.members.size() != runtime.members.size()
        || layout.member_count != runtime.members.size()
        || runtime.frame.members != runtime.members.data()
        || runtime.frame.ready_words != runtime.ready_words.data()
        || runtime.frame.readiness_word_count != runtime.ready_words.size()
        || descriptor_index >= region_readiness_mask_by_component.size()) {
        return false;
    }
    const auto& descriptor
        = region_readiness_mask_by_component[descriptor_index];
    if (descriptor.generation != runtime.runtime_generation
        || descriptor.word_count != runtime.ready_words.size()
        || descriptor.offset > region_readiness_mask_words.size()
        || descriptor.word_count
            > region_readiness_mask_words.size() - descriptor.offset) {
        return false;
    }

    const auto consumed_activation_key_matches = [
        &runtime, &layout](const ProcessId process,
                           const RegionFrontierKeyV1& queued_key) noexcept {
        const auto& frame = runtime.frame;
        const auto consumed_count
            = static_cast<std::size_t>(frame.scheduler_task_cursor);
        if (consumed_count > frame.scheduler_task_count
            || consumed_count > runtime.scheduler_tasks.size()
            || consumed_count > runtime.original_scheduler_tasks.size()
            || layout.members == nullptr) {
            return false;
        }
        for (std::size_t task_index = 0U;
             task_index < consumed_count; ++task_index) {
            const auto payload = runtime.scheduler_tasks[task_index].payload;
            const auto kind = static_cast<std::uint32_t>(
                payload >> kRegionFrontierPayloadKindShiftV1);
            const auto member_index = static_cast<std::size_t>(
                payload & kRegionFrontierPayloadIndexMaskV1);
            if (kind != static_cast<std::uint32_t>(
                    RegionFrontierEventKindV1::member_activation)
                || member_index >= layout.member_count
                || layout.members[member_index].process_id != process) {
                continue;
            }
            const auto& original
                = runtime.original_scheduler_tasks[task_index];
            const RegionFrontierKeyV1 consumed_key {
                frame.slot.time,
                frame.slot.delta,
                frame.slot.systemverilog_round,
                original.stable_order,
                original.sequence,
                frame.slot.process_domain,
                frame.slot.phase,
            };
            if (same_frontier_key(queued_key, consumed_key)) {
                return true;
            }
        }
        return false;
    };

    const bool selected_sync = runtime.member_sync_workset_available
        && runtime.member_sync_private_entry && !runtime.member_sync_force_full;
    const auto selected_members = runtime.member_sync_workset.selected();
    const auto synchronization_count = selected_sync
        ? selected_members.size() : runtime.members.size();

    constexpr std::uint32_t known_member_flags
        = RegionFrontierMemberFlagsV1::waiting_on_static
        | RegionFrontierMemberFlagsV1::queued
        | RegionFrontierMemberFlagsV1::executing
        | RegionFrontierMemberFlagsV1::queued_key_valid
        | RegionFrontierMemberFlagsV1::pending_activation;
    // Validate the entire selected set before copying any ProcessState.
    // Initial, checked and uncertain routes retain the complete member set.
    // Ordinary fanout can enqueue a member while a native prefix is active;
    // its producer receipt must have imported the exact key into the frame.
    for (std::size_t position = 0U; position < synchronization_count; ++position) {
        const auto index = selected_sync ? selected_members[position] : position;
        if (index >= runtime.members.size()) {
            return false;
        }
        const auto& source = kernel.members[index];
        const auto& member = runtime.members[index];
        if (source.process >= processes.size()
            || layout.members == nullptr
            || layout.members[index].process_id != source.process
            || member.process_id != source.process
            || (member.flags & ~known_member_flags) != 0U
            || (member.flags & RegionFrontierMemberFlagsV1::executing) != 0U
            || (member.flags
                    & RegionFrontierMemberFlagsV1::waiting_on_static) == 0U
            || !source.all_registers_definitely_defined) {
            return false;
        }
        const bool queued
            = (member.flags & RegionFrontierMemberFlagsV1::queued) != 0U;
        const bool key_valid
            = (member.flags & RegionFrontierMemberFlagsV1::queued_key_valid)
                != 0U;
        const auto ready_word = index / 64U;
        const auto ready_mask = UINT64_C(1) << (index % 64U);
        if (queued != key_valid || ready_word >= runtime.ready_words.size()
            || (((runtime.ready_words[ready_word] & ready_mask) != 0U)
                != queued)) {
            return false;
        }
        if (queued
            && (member.queued_key.stable_order != source.process
                || member.queued_key.systemverilog_round == 0U
                || member.queued_key.process_domain
                    != static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::systemverilog)
                || member.queued_key.phase
                    != static_cast<std::uint32_t>(SchedulerPhase::active))) {
            return false;
        }

        auto& state = get_process(source.process);
        if (!state.region_kernel_completion_has_no_persistent_registers
            || !state.executor || state.halted || state.suspended
            || state.waiting_on_signal) {
            return false;
        }
        if (source.process >= region_readiness_queued_by_process.size()) {
            return false;
        }
        const auto& queued_sidecar
            = region_readiness_queued_by_process[source.process];
        if (queued_sidecar.generation != 0U
            && (queued_sidecar.component != runtime.component
                || queued_sidecar.member != index
                || queued_sidecar.generation != runtime.runtime_generation)) {
            return false;
        }
        if (state.queued && !queued
            && (!queued_sidecar.key_valid
                || !consumed_activation_key_matches(
                    source.process, queued_sidecar.queued_key))) {
            return false;
        }
        const auto& final_debug = source.final_debug_state;
        if (final_debug
            && !state.frontier_debug_matches(
                *final_debug, runtime.runtime_generation)
            && state.cold().current_scope.capacity()
                < final_debug->scope.size()) {
            return false;
        }
    }

    for (std::size_t position = 0U; position < synchronization_count; ++position) {
        const auto index = selected_sync ? selected_members[position] : position;
        if (index >= runtime.members.size()) {
            return false;
        }
        const auto& source = kernel.members[index];
        const auto& member = runtime.members[index];
        const bool queued
            = (member.flags & RegionFrontierMemberFlagsV1::queued) != 0U;
        const auto ready_word = index / 64U;
        const auto ready_mask = UINT64_C(1) << (index % 64U);
        auto& state = get_process(source.process);
        state.region_kernel_completion_boundary_validated = true;
        state.waiting_on_static = true;
        state.queued = queued;
        state.static_trigger_mask = member.static_trigger_mask;
        state.status = ProcessStatus::waiting;
        const auto& final_debug = source.final_debug_state;
        if (final_debug
            && !state.frontier_debug_matches(
                *final_debug, runtime.runtime_generation)) {
            auto& cold = state.cold();
            const bool source_matches
                = cold.current_source == final_debug->source;
            const bool scope_matches
                = cold.current_scope == final_debug->scope;
            if (!source_matches || !scope_matches) {
                state.clear_frontier_debug_token();
                if (!source_matches) {
                    cold.current_source = final_debug->source;
                }
                if (!scope_matches) {
                    cold.current_scope.assign(final_debug->scope);
                }
            }
            state.remember_frontier_debug(
                *final_debug, runtime.runtime_generation);
        }

        auto& queued_sidecar
            = region_readiness_queued_by_process[source.process];
        auto& mask_word = region_readiness_mask_words[
            descriptor.offset + ready_word];
        if (queued) {
            queued_sidecar.component = runtime.component;
            queued_sidecar.member = index;
            queued_sidecar.generation = runtime.runtime_generation;
            queued_sidecar.queued_key = member.queued_key;
            queued_sidecar.static_trigger_mask = member.static_trigger_mask;
            queued_sidecar.key_valid = true;
            mask_word |= ready_mask;
        } else {
            if (queued_sidecar.generation != 0U) {
                queued_sidecar = { };
            }
            mask_word &= ~ready_mask;
        }
    }
    if (selected_sync) {
        ++runtime.member_sync_selected_passes;
        runtime.member_sync_selected_members += synchronization_count;
        runtime.member_sync_selected_total_members += runtime.members.size();
    } else {
        ++runtime.member_sync_full_passes;
        runtime.member_sync_full_members += synchronization_count;
    }
    if (systemverilog_wave_profile_enabled) {
        if (selected_sync) {
            ++systemverilog_wave_profile_member_sync_selected_passes;
            systemverilog_wave_profile_member_sync_selected_members
                += synchronization_count;
            systemverilog_wave_profile_member_sync_selected_total_members
                += runtime.members.size();
        } else {
            ++systemverilog_wave_profile_member_sync_full_passes;
            systemverilog_wave_profile_member_sync_full_members
                += synchronization_count;
            auto reasons = runtime.member_sync_full_reasons;
            if (!runtime.member_sync_private_entry) {
                reasons |= static_cast<std::uint8_t>(
                    FrontierMemberSyncFullReason::nonprivate_entry);
            }
            if (!runtime.member_sync_workset_available) {
                reasons |= static_cast<std::uint8_t>(
                    FrontierMemberSyncFullReason::unavailable_workset);
            }
            // Each completed full pass appears once, including bucket zero
            // for any unlabelled cause. Individual reason bits are not disjoint.
            ++systemverilog_wave_profile_member_sync_full_reason_passes[reasons];
            systemverilog_wave_profile_member_sync_full_reason_members[reasons]
                += synchronization_count;
        }
    }
    runtime.member_sync_workset.clear();
    runtime.member_sync_force_full = false;
    runtime.member_sync_full_reasons = 0U;
    return true;
}

} // namespace fsim::runtime::simir
