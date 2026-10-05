// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <ranges>

namespace fsim::runtime::simir {
namespace {

bool value_matches_kind(const ValueKind kind, const PackedLogic4& value) noexcept
{
    switch (kind) {
    case ValueKind::logic4:
        return !value.is_logic9();
    case ValueKind::logic9:
        return value.is_logic9();
    }
    return false;
}

} // namespace

std::optional<std::size_t>
Interpreter::Impl::VhdlProjectedReadinessTask::member_index(
    const ProcessId process) const noexcept
{
    std::optional<std::size_t> result;
    for (std::size_t index = 0U; index < members.size(); ++index) {
        if (members[index].process != process) {
            continue;
        }
        if (result) {
            return std::nullopt;
        }
        result = index;
    }
    return result;
}

void Interpreter::Impl::VhdlProjectedReadinessTask::clear_member(
    const ProcessId process) noexcept
{
    const auto index = member_index(process);
    if (!index) {
        return;
    }
    members[*index].receipt = { };
    members[*index].static_trigger_mask = 0U;
    if (*index < ticket_member_offsets.size()) {
        ticket_member_offsets[*index]
            = std::numeric_limits<std::size_t>::max();
    }
}

SchedulerBatchResult
Interpreter::Impl::VhdlProjectedReadinessTask::execute(
    Scheduler& active_scheduler,
    const std::span<const std::uint64_t> payloads)
{
    SchedulerBatchResult declined;
    if (owner == nullptr || invalidated || payloads.empty()
        || component >= owner->vhdl_projected_readiness_by_component.size()
        || owner->vhdl_projected_readiness_by_component[component].get()
            != this
        || runtime_generation == 0U
        || runtime_generation != owner->region_runtime_generation
        || component >= owner->region_kernel_backends_by_component.size()
        || owner->region_kernel_backends_by_component[component] != backend
        || component >= owner->region_kernel_backend_generation_by_component.size()
        || owner->region_kernel_backend_generation_by_component[component]
            != runtime_generation
        || !backend || !backend->executor || !owner->region_graph
        || !owner->region_graph->component_epochs_current(component)
        || backend->kernel.members.size() != members.size()
        || ticket_member_offsets.size() != members.size()
        || members.size() < 2U || members.size() > 64U) {
        return declined;
    }

    const auto frontier = active_scheduler.current_generic_batch_frontier();
    const SchedulerBatchGroupKey expected_group {
        runtime_generation, static_cast<std::uint64_t>(component) + 1U };
    if (!frontier || frontier->generation == 0U
        || frontier->phase != SchedulerPhase::active
        || frontier->time != active_scheduler.now()
        || frontier->delta != active_scheduler.delta()
        || active_scheduler.current_phase() != SchedulerPhase::active
        || frontier->cursor != 0U
        || frontier->end != frontier->tasks.size()
        || frontier->tasks.size() != payloads.size()
        || frontier->tasks.size() > frontier->ticket_members.size()
        || frontier->compact_group_key != expected_group
        || frontier->ticket_member_offset > members.size()
        || frontier->ticket_members.empty()
        || frontier->ticket_members.size()
            > members.size() - frontier->ticket_member_offset) {
        return declined;
    }

    std::ranges::fill(ticket_member_offsets,
        std::numeric_limits<std::size_t>::max());

    std::size_t current_ticket_receipts { };
    for (const auto& queued : members) {
        if (!queued.receipt.valid) {
            if (queued.static_trigger_mask != 0U) {
                return declined;
            }
            continue;
        }
        if (queued.receipt.time == frontier->time
            && queued.receipt.delta == frontier->delta
            && queued.receipt.phase == SchedulerPhase::active) {
            ++current_ticket_receipts;
        }
    }
    if (current_ticket_receipts != frontier->ticket_members.size()) {
        return declined;
    }

    for (std::size_t index = 0U;
         index < frontier->ticket_members.size(); ++index) {
        const auto& ticket_member = frontier->ticket_members[index];
        const auto payload = ticket_member.payload;
        const auto process_word = payload
            & ~owner->generic_projected_region_payload;
        if ((payload & owner->generic_projected_region_payload) == 0U
            || process_word > std::numeric_limits<ProcessId>::max()) {
            return declined;
        }
        const auto process = static_cast<ProcessId>(process_word);
        const auto member = member_index(process);
        if (!member || process >= owner->processes.size()
            || process >= owner->region_component_by_process.size()
            || owner->region_component_by_process[process] != component
            || process >= owner->region_graph->processes().size()) {
            return declined;
        }
        const auto& queued = members[*member];
        const auto& state = owner->processes[process];
        const auto& node = owner->region_graph->processes()[process];
        const auto& receipt = queued.receipt;
        if (*member >= backend->kernel.members.size()
            || backend->kernel.members[*member].process != process
            || ticket_member_offsets[*member]
                != std::numeric_limits<std::size_t>::max()
            || node.update_kind != RegionUpdateKind::vhdl_projected
            || node.scheduling_domain != ProcessSchedulingDomain::generic
            || !state.queued || !state.waiting_on_static
            || state.static_trigger_mask == 0U
            || queued.static_trigger_mask != state.static_trigger_mask
            || ticket_member.order != process
            || (index != 0U
                && !(frontier->ticket_members[index - 1U].order
                        < ticket_member.order
                    || (frontier->ticket_members[index - 1U].order
                            == ticket_member.order
                        && frontier->ticket_members[index - 1U].sequence
                            < ticket_member.sequence)))
            || !receipt.valid || receipt.time != frontier->time
            || receipt.delta != frontier->delta
            || receipt.phase != SchedulerPhase::active
            || receipt.stable_order != ticket_member.order
            || receipt.sequence != ticket_member.sequence
            || receipt.payload != payload
            || (index < frontier->tasks.size()
                && (frontier->tasks[index].payload != payload
                    || frontier->tasks[index].stable_order
                        != ticket_member.order
                    || frontier->tasks[index].sequence
                        != ticket_member.sequence
                    || payloads[index] != payload))) {
            return declined;
        }
        ticket_member_offsets[*member]
            = frontier->ticket_member_offset + index;
    }

    for (std::size_t member = 0U; member < members.size(); ++member) {
        const auto& queued = members[member];
        const bool receipt_is_current_ticket = queued.receipt.valid
            && queued.receipt.time == frontier->time
            && queued.receipt.delta == frontier->delta
            && queued.receipt.phase == SchedulerPhase::active;
        if (receipt_is_current_ticket
                != (ticket_member_offsets[member]
                    != std::numeric_limits<std::size_t>::max())
            || (!queued.receipt.valid
                && queued.static_trigger_mask != 0U)) {
            return declined;
        }
        if (!receipt_is_current_ticket) {
            if (!queued.receipt.valid) {
                continue;
            }
            const bool strictly_future
                = queued.receipt.time > frontier->time
                || (queued.receipt.time == frontier->time
                    && queued.receipt.delta > frontier->delta);
            const auto process = queued.process;
            if (!strictly_future
                || queued.receipt.phase != SchedulerPhase::active
                || queued.receipt.stable_order != process
                || queued.receipt.payload
                    != (owner->generic_projected_region_payload
                        | static_cast<std::uint64_t>(process))
                || process >= owner->processes.size()
                || !owner->processes[process].queued
                || !owner->processes[process].waiting_on_static
                || owner->processes[process].static_trigger_mask == 0U
                || queued.static_trigger_mask
                    != owner->processes[process].static_trigger_mask) {
                return declined;
            }
            continue;
        }
        const auto ticket_offset = ticket_member_offsets[member];
        if (ticket_offset < frontier->ticket_member_offset) {
            return declined;
        }
        const auto suffix_index
            = ticket_offset - frontier->ticket_member_offset;
        if (suffix_index >= frontier->ticket_members.size()) {
            return declined;
        }
        const auto& ticket_member = frontier->ticket_members[suffix_index];
        if (ticket_member.order != queued.process
            || ticket_member.sequence != queued.receipt.sequence
            || ticket_member.payload != queued.receipt.payload
            || ticket_member.payload
                != (owner->generic_projected_region_payload
                    | static_cast<std::uint64_t>(queued.process))) {
            return declined;
        }
    }

    auto result = owner->execute_generic_projected_region(
        payloads, 0U, this);
    if (result.executed > payloads.size()) {
        invalidated = true;
        throw std::logic_error {
            "VHDL projected compact callback consumed beyond its prefix"
        };
    }
    for (std::size_t index = 0U; index < result.executed; ++index) {
        const auto process = static_cast<ProcessId>(payloads[index]
            & ~owner->generic_projected_region_payload);
        clear_member(process);
    }
    return result;
}

detail::SchedulerTaskDescriptor
Interpreter::Impl::VhdlProjectedReadinessTask::make_fallback_descriptor(
    const std::uint64_t payload) noexcept
{
    if (owner == nullptr
        || (payload & owner->generic_projected_region_payload) == 0U
        || (payload & ~owner->generic_projected_region_payload)
            > std::numeric_limits<ProcessId>::max()) {
        return { };
    }
    const auto process = static_cast<ProcessId>(
        payload & ~owner->generic_projected_region_payload);
    const auto member = member_index(process);
    if (!member) {
        return { };
    }
    const auto& queued = members[*member];
    if (!queued.receipt.valid
        || queued.receipt.phase != SchedulerPhase::active
        || queued.receipt.stable_order != process
        || queued.receipt.payload != payload
        || queued.static_trigger_mask == 0U) {
        return { };
    }
    return detail::make_scheduler_task_descriptor<FallbackPayload,
        &VhdlProjectedReadinessTask::dispatch_fallback>(
            FallbackPayload { this, payload });
}

void Interpreter::Impl::VhdlProjectedReadinessTask::dispatch_fallback(
    Scheduler& active_scheduler, const FallbackPayload& fallback)
{
    auto* const task = fallback.task;
    if (task == nullptr || task->owner == nullptr
        || (fallback.payload & task->owner->generic_projected_region_payload)
            == 0U) {
        throw std::logic_error {
            "invalid VHDL projected compact fallback descriptor"
        };
    }
    const auto process_word = fallback.payload
        & ~task->owner->generic_projected_region_payload;
    if (process_word > std::numeric_limits<ProcessId>::max()
        || process_word >= task->owner->processes.size()) {
        throw std::logic_error {
            "VHDL projected compact fallback has an invalid process"
        };
    }
    const auto process = static_cast<ProcessId>(process_word);
    auto& state = task->owner->get_process(process);
    const auto member = task->member_index(process);
    const auto phase = active_scheduler.current_phase();
    if (!member || !task->members[*member].receipt.valid
        || task->members[*member].receipt.phase != SchedulerPhase::active
        || task->members[*member].receipt.stable_order != process
        || task->members[*member].receipt.payload != fallback.payload
        || task->members[*member].receipt.time != active_scheduler.now()
        || task->members[*member].receipt.delta != active_scheduler.delta()
        || task->members[*member].static_trigger_mask == 0U
        || !phase || *phase != SchedulerPhase::active
        || !state.queued || !state.waiting_on_static
        || state.static_trigger_mask == 0U
        || state.program().scheduling_domain()
            != ProcessSchedulingDomain::generic) {
        throw std::logic_error {
            "VHDL projected compact fallback key is not current"
        };
    }
    task->clear_member(process);
    state.queued = false;
    state.waiting_on_static = false;
    task->owner->remove_dynamic_wait(state);
    task->owner->execute(process);
}

bool Interpreter::Impl::generic_projected_region_member_eligible(
    const ProcessId id) const
{
    if (!systemverilog_region_kernel_enabled || !region_graph
        || id >= processes.size() || id >= region_component_by_process.size()
        || processes.is_compact_constant(id)
        || process_profile_enabled || execution_point_hook
        || signal_change_hook || native_signal_observation_required_hook
        || native_signal_observation_any_hook || stored_signal_change_hook
        || driver_change_hook || event_trigger_hook
        || container_object_change_hook || container_element_change_hook
        || scalar_signal_change_hook || monitor
        || update_profile_enabled || native_phase_profile_enabled
        || native_process_count_profile_enabled
        || native_update_profile_enabled
        || scheduler.trace_hook_installed() || cohort_overflow_scratch_in_use) {
        return false;
    }
    const auto component = region_component_by_process[id];
    if (component >= region_activation_programs.size()
        || !region_activation_programs[component]
        || component >= region_graph->certificate_inventory()
                .components.size()
        || !region_graph->component_epochs_current(component)
        || id >= region_graph->processes().size()) {
        return false;
    }
    const auto& node = region_graph->processes()[id];
    const bool generic_update
        = node.update_kind == RegionUpdateKind::generic;
    const auto* frontier_runtime
        = component < region_frontier_runtime_by_component.size()
            ? region_frontier_runtime_by_component[component].get()
            : nullptr;
    const bool generic_frontier_selected
        = generic_update && frontier_runtime != nullptr
        && frontier_runtime->execution_mode
            == RegionFrontierExecutionModeV2::generic_deferred_update;
    if (generic_frontier_selected) {
        if (frontier_runtime->owner != this
            || frontier_runtime->component != component
            || frontier_runtime->invalidated
            || !frontier_runtime->frame_initialized
            || frontier_runtime->runtime_generation
                != region_runtime_generation
            || !frontier_runtime->backend
            || !frontier_runtime->backend->executor) {
            return false;
        }
    } else if (component >= region_kernel_backends_by_component.size()
        || !region_kernel_backends_by_component[component]
        || component >= region_kernel_backend_generation_by_component.size()
        || region_kernel_backend_generation_by_component[component]
            != region_runtime_generation) {
        return false;
    }
    const auto& state = processes[id];
    const auto& program = state.program();
    if (program.scheduling_domain() != ProcessSchedulingDomain::generic
        || node.scheduling_domain != ProcessSchedulingDomain::generic
        || (!generic_update
            && node.update_kind != RegionUpdateKind::vhdl_projected)
        || !node.pure || node.dependencies_unknown
        || node.cyclic_or_dependent_on_cycle
        || state.execution_phase != SchedulerPhase::active
        || !state.waiting_on_static || state.waiting_on_signal
        || state.suspended || state.halted
        || state.has_active_wait_timeout()
        || state.static_trigger_mask == 0U || !state.executor
        || !process_region_kernel_eligible(id)
        || !state.executor->cohort_manages_process_state()) {
        return false;
    }
    const auto& operations = program.operations();
    if (operations.size() < 2U || state.pc != operations.size() - 1U
        || !operation_holds<WaitSensitivity>(
            operations.expanded(operations.size() - 2U))) {
        return false;
    }
    const auto jump_operation = operations.expanded(operations.size() - 1U);
    const auto* const jump = operation_get_if<Jump>(&jump_operation);
    return jump != nullptr && jump->target == 0U;
}

bool Interpreter::Impl::queue_generic_projected_region(
    const ProcessId id)
{
    if (!generic_projected_region_member_eligible(id)) {
        return false;
    }
    const auto component = region_component_by_process[id];
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    const auto cohort = id < static_sensitivity_cohort_by_process.size()
        ? static_sensitivity_cohort_by_process[id]
        : no_cohort;
    const bool generic_update
        = region_graph->processes()[id].update_kind
            == RegionUpdateKind::generic;
    if (!generic_update && cohort != no_cohort
        && (cohort >= static_sensitivity_cohorts.size()
            || static_sensitivity_cohorts[cohort].members.size() > 1U)) {
        return false;
    }

    auto& state = get_process(id);
    const auto runtime = generic_update
        && component < region_frontier_runtime_by_component.size()
        ? region_frontier_runtime_by_component[component]
        : std::shared_ptr<RegionFrontierComponentRuntime> { };
    const bool generic_compact_ready
        = runtime && runtime->owner == this
        && runtime->component == component
        && !runtime->invalidated && runtime->frame_initialized
        && runtime->runtime_generation != 0U
        && runtime->runtime_generation == region_runtime_generation
        && runtime->execution_mode
            == RegionFrontierExecutionModeV2::generic_deferred_update
        && runtime->backend && runtime->backend->executor
        && runtime->members.size()
            == runtime->backend->executor->layout().member_count
        && runtime->members.size() != 0U
        && runtime->generic_queued_members.size() == runtime->members.size()
        && runtime->generic_queued_ready_words.size()
            == (runtime->members.size() + 63U) / 64U;
    const auto vhdl_ticket
        = !generic_update
            && component < vhdl_projected_readiness_by_component.size()
        ? vhdl_projected_readiness_by_component[component]
        : std::shared_ptr<VhdlProjectedReadinessTask> { };
    const bool vhdl_compact_ready
        = vhdl_ticket && vhdl_ticket->owner == this
        && vhdl_ticket->component == component
        && !vhdl_ticket->invalidated
        && vhdl_ticket->runtime_generation != 0U
        && vhdl_ticket->runtime_generation == region_runtime_generation
        && component < region_kernel_backends_by_component.size()
        && vhdl_ticket->backend
        && vhdl_ticket->backend
            == region_kernel_backends_by_component[component]
        && component < region_kernel_backend_generation_by_component.size()
        && region_kernel_backend_generation_by_component[component]
            == vhdl_ticket->runtime_generation
        && vhdl_ticket->members.size() > 1U
        && vhdl_ticket->members.size() <= 64U
        && vhdl_ticket->members.size()
            == vhdl_ticket->backend->kernel.members.size()
        && vhdl_ticket->ticket_member_offsets.size()
            == vhdl_ticket->members.size();
    std::optional<std::size_t> vhdl_member_index;
    if (vhdl_compact_ready) {
        vhdl_member_index = vhdl_ticket->member_index(id);
        if (!vhdl_member_index) {
            vhdl_ticket->invalidated = true;
            return false;
        }
    }
    std::size_t member_index = std::numeric_limits<std::size_t>::max();
    if (generic_compact_ready) {
        const auto& layout = runtime->backend->executor->layout();
        for (std::size_t index = 0U; index < layout.member_count; ++index) {
            if (layout.members[index].process_id == id) {
                if (member_index != std::numeric_limits<std::size_t>::max()) {
                    runtime->invalidate();
                    return false;
                }
                member_index = index;
            }
        }
        if (member_index == std::numeric_limits<std::size_t>::max()) {
            return false;
        }
    }

    if (state.queued) {
        if (generic_compact_ready
            && member_index < runtime->generic_queued_members.size()) {
            auto& queued = runtime->generic_queued_members[member_index];
            const auto bit = UINT64_C(1) << (member_index % 64U);
            const bool ready = (runtime->generic_queued_ready_words[
                member_index / 64U] & bit) != 0U;
            if (queued.receipt.valid != ready
                || (!queued.receipt.valid
                    && queued.static_trigger_mask != 0U)) {
                runtime->invalidate();
                return false;
            }
            if (queued.receipt.valid) {
                if (queued.receipt.payload
                        != (generic_projected_region_payload
                            | static_cast<std::uint64_t>(id))
                    || queued.receipt.stable_order != id
                    || queued.receipt.phase != SchedulerPhase::active
                    || queued.static_trigger_mask == 0U) {
                    runtime->invalidate();
                    return false;
                }
                queued.static_trigger_mask |= state.static_trigger_mask;
            }
        } else if (vhdl_compact_ready && vhdl_member_index) {
            auto& queued = vhdl_ticket->members[*vhdl_member_index];
            if (queued.receipt.valid) {
                if (queued.receipt.payload
                        != (generic_projected_region_payload
                            | static_cast<std::uint64_t>(id))
                    || queued.receipt.stable_order != id
                    || queued.receipt.phase != SchedulerPhase::active
                    || queued.static_trigger_mask == 0U) {
                    vhdl_ticket->invalidated = true;
                    return false;
                }
                queued.static_trigger_mask |= state.static_trigger_mask;
            } else if (queued.static_trigger_mask != 0U) {
                vhdl_ticket->invalidated = true;
                return false;
            }
        }
        return true;
    }

    if (generic_compact_ready) {
        auto& queued = runtime->generic_queued_members[member_index];
        const auto ready_bit = UINT64_C(1) << (member_index % 64U);
        if (queued.receipt.valid
            || (runtime->generic_queued_ready_words[member_index / 64U]
                & ready_bit) != 0U) {
            runtime->invalidate();
            return false;
        }
        const auto group = SchedulerBatchGroupKey {
            runtime->runtime_generation,
            static_cast<std::uint64_t>(component) + 1U,
        };
        if (group) {
            SchedulerGenericKeyReceipt receipt;
            const auto payload = generic_projected_region_payload
                | static_cast<std::uint64_t>(id);
            const bool compacted
                = scheduler.schedule_generic_next_delta_readiness_member(
                    *runtime, group, runtime->members.size(), id, payload,
                    std::static_pointer_cast<void>(runtime), &receipt);
            if (compacted) {
                if (!receipt.valid || receipt.phase != SchedulerPhase::active
                    || receipt.stable_order != id
                    || receipt.payload != payload) {
                    // The ticket is visible. Record its actual issued key and
                    // queued state before failing closed. A malformed receipt
                    // violates the scheduler contract and cannot be recovered
                    // by fabricating a key for checked fallback.
                    queued.receipt = receipt;
                    queued.static_trigger_mask = state.static_trigger_mask;
                    runtime->generic_queued_ready_words[
                        member_index / 64U] |= ready_bit;
                    state.queued = true;
                    runtime->invalidate();
                    throw std::logic_error {
                        "scheduler returned an invalid Generic key receipt"
                    };
                }
                queued.receipt = receipt;
                queued.static_trigger_mask = state.static_trigger_mask;
                runtime->generic_queued_ready_words[
                    member_index / 64U] |= ready_bit;
                state.queued = true;
                return true;
            }
        }
    }

    if (vhdl_compact_ready && vhdl_member_index) {
        auto& queued = vhdl_ticket->members[*vhdl_member_index];
        if (queued.receipt.valid || queued.static_trigger_mask != 0U) {
            vhdl_ticket->invalidated = true;
            throw std::logic_error {
                "stale VHDL projected compact key receipt"
            };
        }
        const auto group = SchedulerBatchGroupKey {
            vhdl_ticket->runtime_generation,
            static_cast<std::uint64_t>(component) + 1U,
        };
        const auto payload = generic_projected_region_payload
            | static_cast<std::uint64_t>(id);
        if (group) {
            SchedulerGenericKeyReceipt receipt;
            const bool compacted
                = scheduler.schedule_generic_next_delta_readiness_member(
                    *vhdl_ticket, group, vhdl_ticket->members.size(), id,
                    payload, std::static_pointer_cast<void>(vhdl_ticket),
                    &receipt);
            if (compacted) {
                if (!receipt.valid || receipt.phase != SchedulerPhase::active
                    || receipt.time != scheduler.now()
                    || scheduler.delta()
                        == std::numeric_limits<std::uint64_t>::max()
                    || receipt.delta != scheduler.delta() + 1U
                    || receipt.stable_order != id
                    || receipt.payload != payload) {
                    queued.receipt = receipt;
                    queued.static_trigger_mask = state.static_trigger_mask;
                    state.queued = true;
                    vhdl_ticket->invalidated = true;
                    throw std::logic_error {
                        "scheduler returned an invalid VHDL projected key receipt"
                    };
                }
                queued.receipt = receipt;
                queued.static_trigger_mask = state.static_trigger_mask;
                state.queued = true;
                return true;
            }
        }
    }

    scheduler.schedule_next_delta_batchable(
        SchedulerPhase::active, id, *this,
        generic_projected_region_payload | static_cast<std::uint64_t>(id),
        [this, id](Scheduler&) {
            auto& member = get_process(id);
            member.queued = false;
            member.waiting_on_static = false;
            remove_dynamic_wait(member);
            execute(id);
        });
    state.queued = true;
    return true;
}

void Interpreter::Impl::merge_generic_frontier_ready_mask(
    const ProcessId process,
    const std::uint64_t trigger_mask) noexcept
{
    if (trigger_mask == 0U || process >= region_component_by_process.size()) {
        return;
    }
    const auto component = region_component_by_process[process];
    if (component < vhdl_projected_readiness_by_component.size()) {
        const auto& vhdl_ticket
            = vhdl_projected_readiness_by_component[component];
        if (vhdl_ticket && vhdl_ticket->owner == this
            && vhdl_ticket->component == component
            && !vhdl_ticket->invalidated
            && vhdl_ticket->runtime_generation != 0U
            && vhdl_ticket->runtime_generation == region_runtime_generation
            && process < processes.size() && processes[process].queued) {
            const auto member = vhdl_ticket->member_index(process);
            if (!member) {
                vhdl_ticket->invalidated = true;
                return;
            }
            auto& queued = vhdl_ticket->members[*member];
            if (!queued.receipt.valid) {
                if (queued.static_trigger_mask != 0U) {
                    vhdl_ticket->invalidated = true;
                }
                return;
            }
            if (queued.receipt.payload
                    != (generic_projected_region_payload
                        | static_cast<std::uint64_t>(process))
                || queued.receipt.stable_order != process
                || queued.receipt.phase != SchedulerPhase::active
                || queued.static_trigger_mask == 0U) {
                vhdl_ticket->invalidated = true;
                return;
            }
            queued.static_trigger_mask |= trigger_mask;
            return;
        }
    }
    if (component >= region_frontier_runtime_by_component.size()) {
        return;
    }
    const auto& runtime = region_frontier_runtime_by_component[component];
    if (!runtime || runtime->owner != this || runtime->invalidated
        || runtime->runtime_generation == 0U
        || runtime->runtime_generation != region_runtime_generation
        || runtime->execution_mode
            != RegionFrontierExecutionModeV2::generic_deferred_update
        || !runtime->backend || !runtime->backend->executor
        || process >= processes.size() || !processes[process].queued) {
        return;
    }
    const auto& layout = runtime->backend->executor->layout();
    if (runtime->generic_queued_members.size() != layout.member_count
        || runtime->generic_queued_ready_words.size()
            != (layout.member_count + 63U) / 64U) {
        runtime->invalidate();
        return;
    }
    for (std::size_t member = 0U; member < layout.member_count; ++member) {
        if (layout.members[member].process_id != process) {
            continue;
        }
        auto& queued = runtime->generic_queued_members[member];
        const auto bit = UINT64_C(1) << (member % 64U);
        const bool ready = (runtime->generic_queued_ready_words[
            member / 64U] & bit) != 0U;
        if (!queued.receipt.valid) {
            if (ready || queued.static_trigger_mask != 0U) {
                runtime->invalidate();
            }
            return;
        }
        if (queued.receipt.payload
                != (generic_projected_region_payload
                    | static_cast<std::uint64_t>(process))
            || queued.receipt.stable_order != process
            || queued.receipt.phase != SchedulerPhase::active
            || queued.static_trigger_mask == 0U || !ready) {
            runtime->invalidate();
            return;
        }
        queued.static_trigger_mask |= trigger_mask;
        return;
    }
}

SchedulerBatchResult Interpreter::Impl::execute_generic_projected_region(
    const std::span<const std::uint64_t> payloads,
    const std::size_t frontier_offset,
    VhdlProjectedReadinessTask* const ticket_runtime)
{
    SchedulerBatchResult result;
    bool publication_staging_started { };
    const auto decline = [&]() {
        if (systemverilog_wave_profile_enabled) {
            ++generic_projected_region_declines;
        }
        return result;
    };
    if (systemverilog_wave_profile_enabled) {
        ++generic_projected_region_attempts;
    }
    const auto frontier = scheduler.current_generic_batch_frontier();
    if (payloads.empty() || scheduler.stop_requested()
        || scheduler.current_phase() != SchedulerPhase::active
        || !frontier || frontier->generation == 0U
        || frontier->phase != SchedulerPhase::active
        || frontier->time != scheduler.now()
        || frontier->delta != scheduler.delta()
        || frontier_offset > frontier->tasks.size()
        || payloads.size() > frontier->tasks.size() - frontier_offset
        || frontier->end != frontier->tasks.size()) {
        return decline();
    }
    const auto first_payload = payloads.front();
    if ((first_payload & generic_projected_region_payload) == 0U) {
        return decline();
    }
    const auto first_process = static_cast<ProcessId>(
        first_payload & ~generic_projected_region_payload);
    if (!generic_projected_region_member_eligible(first_process)
        || first_process >= region_component_by_process.size()) {
        return decline();
    }
    const auto component = region_component_by_process[first_process];
    if (ticket_runtime != nullptr
        && (ticket_runtime->owner != this
            || ticket_runtime->component != component
            || ticket_runtime->invalidated
            || component >= vhdl_projected_readiness_by_component.size()
            || vhdl_projected_readiness_by_component[component].get()
                != ticket_runtime
            || ticket_runtime->runtime_generation == 0U
            || ticket_runtime->runtime_generation != region_runtime_generation)) {
        return decline();
    }
    if (component >= region_activation_programs.size()
        || !region_activation_programs[component]
        || component >= region_graph->certificate_inventory()
                .components.size()) {
        return decline();
    }
    const auto& certificate = region_graph->certificate_inventory()
                                  .components.at(component);
    const bool generic_update
        = region_graph->processes()[first_process].update_kind
            == RegionUpdateKind::generic;
    const auto frontier_runtime
        = component < region_frontier_runtime_by_component.size()
            ? region_frontier_runtime_by_component[component]
            : std::shared_ptr<RegionFrontierComponentRuntime> { };
    const bool generic_frontier_selected
        = generic_update && frontier_runtime
        && frontier_runtime->execution_mode
            == RegionFrontierExecutionModeV2::generic_deferred_update;
    const auto backend_entry
        = component < region_kernel_backends_by_component.size()
            ? region_kernel_backends_by_component[component]
            : std::shared_ptr<RegionKernelBackendEntry> { };
    const RegionConeActivationKernel* kernel_pointer { };
    if (generic_frontier_selected) {
        if (!frontier_runtime->backend
            || !frontier_runtime->backend->executor) {
            return decline();
        }
        kernel_pointer = &frontier_runtime->backend->kernel;
    } else {
        if (!backend_entry || !backend_entry->executor) {
            return decline();
        }
        kernel_pointer = &backend_entry->kernel;
    }
    // Each selected executor entry owns its immutable kernel for the duration
    // of this synchronous scheduler callback.
    const auto& kernel = *kernel_pointer;
    const bool generic_boundary_only
        = generic_update
        && certificate.status
            == RegionComponentCertificateStatus::no_internal_state
        && certificate.structural_internal_signal_candidates.empty();
    if ((certificate.status
            != RegionComponentCertificateStatus::structural_candidate
            && !generic_boundary_only)
        || kernel.program.scheduling_domain
            != ProcessSchedulingDomain::generic
        || kernel.outputs.empty()) {
        return decline();
    }

    std::size_t ready_count { };
    for (; ready_count < payloads.size(); ++ready_count) {
        const auto payload = payloads[ready_count];
        const auto& frontier_task
            = frontier->tasks[frontier_offset + ready_count];
        if (payload != frontier_task.payload
            || (payload & generic_projected_region_payload) == 0U) {
            break;
        }
        const auto process = static_cast<ProcessId>(
            payload & ~generic_projected_region_payload);
        if (process >= region_component_by_process.size()
            || region_component_by_process[process] != component
            || !generic_projected_region_member_eligible(process)
            || region_graph->processes()[process].update_kind
                != region_graph->processes()[first_process].update_kind
            || !get_process(process).queued
            || std::ranges::find(kernel.members, process,
                   &RegionConeKernelMember::process)
                == kernel.members.end()) {
            break;
        }
    }
    if (ready_count == 0U) {
        return decline();
    }

    if (generic_frontier_selected) {
        if (auto native = execute_generic_region_frontier_prefix(
                *frontier_runtime, *frontier, payloads,
                frontier_offset, ready_count)) {
            return *native;
        }
        // The V2 helper returns null only after a non-mutating decline and
        // rolls back its prepared frame/update-vector prefix. Preserve the
        // original per-task checked callback instead of trying V1 execution.
        return decline();
    }

    auto& workspace_entry = *backend_entry;
    if (!workspace_entry.try_enter()) {
        return decline();
    }
    struct BackendLease {
        RegionKernelBackendEntry& entry;
        ~BackendLease() { entry.leave(); }
    } lease { workspace_entry };

    try {
        if (!workspace_entry.generic_workspace) {
            workspace_entry.generic_workspace
                = std::make_unique<RegionKernelGenericWorkspace>(kernel);
        }
        auto& workspace = *workspace_entry.generic_workspace;
        auto& activation = workspace.activation;
        struct WorkspaceCleanup {
            Impl& owner;
            RegionKernelGenericWorkspace& workspace;
            bool committed { };

            ~WorkspaceCleanup()
            {
                if (committed) {
                    return;
                }
                for (const auto process : workspace.prepared_processes) {
                    if (process < owner.processes.size()
                        && owner.processes[process].executor) {
                        owner.processes[process].executor
                            ->cancel_region_completion_native();
                    }
                }
                workspace.prepared_processes.clear();
                workspace.activation.discard_wave();
                workspace.final_debug_states.clear();
                for (auto& waveform : workspace.projected_values) {
                    waveform.clear();
                }
            }
        } cleanup { *this, workspace };

        auto& prefix = workspace.prefix;
        prefix.frontier_generation = frontier->generation;
        prefix.frontier_cursor = frontier->cursor + frontier_offset;
        prefix.frontier_end = frontier->end;
        prefix.time = frontier->time;
        prefix.delta = frontier->delta;
        prefix.phase = frontier->phase;
        prefix.systemverilog_round = 0U;
        prefix.process_domain = ProcessSchedulingDomain::generic;
        prefix.tasks.clear();
        for (std::size_t index = 0U; index < ready_count; ++index) {
            const auto& task
                = frontier->tasks[frontier_offset + index];
            const auto process = static_cast<ProcessId>(
                payloads[index] & ~generic_projected_region_payload);
            prefix.tasks.push_back({
                prefix.frontier_cursor + index,
                { process, get_process(process).static_trigger_mask,
                    { ProcessSchedulingDomain::generic,
                        SchedulerPhase::active, frontier->time,
                        frontier->delta, task.stable_order,
                        task.sequence, 0U } },
            });
        }

        const auto is_alias_signal = [&](const SignalId signal) {
            return std::ranges::any_of(
                region_graph->signal_alias_families(),
                [&](const RegionSignalAliasFamilyDescriptor& family) {
                    return family.proxy == signal
                        || std::ranges::any_of(family.leaves,
                            [signal](const RegionSignalAliasLeaf& leaf) {
                                return leaf.signal == signal;
                            });
                });
        };
        workspace.boundary_inputs.clear();
        for (const auto& input : kernel.inputs) {
            if (input.width == 0U || input.signal >= signals.size()
                || input.signal >= region_graph->signals().size()
                || is_alias_signal(input.signal)) {
                return decline();
            }
            const auto& signal = get_signal(input.signal);
            const auto& graph_signal
                = region_graph->signals()[input.signal].descriptor;
            if (signal.value_kind != input.value_kind
                || graph_signal.value_kind != input.value_kind
                || signal.initial_value.width() != input.width
                || graph_signal.width != input.width
                || !value_matches_kind(
                    input.value_kind, signal.initial_value)) {
                return decline();
            }
            if (!input.internal) {
                auto value = logical_signal_value(input.signal);
                if (value.width() != input.width
                    || !value_matches_kind(input.value_kind, value)) {
                    return decline();
                }
                workspace.boundary_inputs.push_back(std::move(value));
            }
        }
        workspace.internal_seeds.clear();
        for (const auto signal : kernel.internal_signals) {
            if (signal >= region_graph->signals().size()
                || signal >= signals.size() || is_alias_signal(signal)) {
                return decline();
            }
            const auto& writers = region_graph->signals()[signal].writers;
            if (writers.size() != 1U) {
                return decline();
            }
            const auto owner = writers.front().process;
            if (signal >= driver_values.size()) {
                return decline();
            }
            const auto* const raw = driver_values[signal].find(owner);
            const auto current = logical_signal_value(signal);
            const auto& descriptor = get_signal(signal);
            const auto& graph_signal
                = region_graph->signals()[signal].descriptor;
            if (descriptor.initial_value.width() != current.width()
                || graph_signal.width != current.width()
                || descriptor.value_kind != graph_signal.value_kind
                || !value_matches_kind(descriptor.value_kind, current)
                || (raw != nullptr
                    && (raw->value.width() != current.width()
                        || !value_matches_kind(
                            descriptor.value_kind, raw->value)))) {
                return decline();
            }
            workspace.internal_seeds.push_back({ signal, owner, current,
                signal_last_values.at(signal),
                raw == nullptr ? current : raw->value });
        }
        activation.reset_internal_state(workspace.internal_seeds);
        const auto& image = activation.begin_wave_reusable(
            prefix, workspace.boundary_inputs);

        const auto publications = activation.expected_publication_bindings();
        const auto expected_publications
            = activation.expected_publication_count();
        if (publications.size() != expected_publications
            || (!generic_update
                && expected_publications > workspace.projected_values.size())) {
            activation.discard_wave();
            return decline();
        }
        if (generic_update) {
            for (const auto* const binding : publications) {
                if (binding == nullptr || binding->signal >= signals.size()) {
                    activation.discard_wave();
                    return decline();
                }
                const auto output_begin
                    = static_cast<std::uint64_t>(binding->offset);
                const auto output_end
                    = output_begin + binding->width;
                const bool matching_module_path
                    = std::ranges::any_of(module_paths,
                        [&](const ModulePath& path) {
                            return std::ranges::binary_search(
                                       path.drivers, binding->owner)
                                && std::ranges::any_of(path.destinations,
                                    [&](const ModulePathTerminal& terminal) {
                                        const auto terminal_begin
                                            = static_cast<std::uint64_t>(
                                                terminal.offset);
                                        const auto terminal_end
                                            = terminal_begin + terminal.width;
                                        return terminal.signal == binding->signal
                                            && terminal_begin < output_end
                                            && output_begin < terminal_end;
                                    });
                        });
                // A matching timed path can create scheduler tasks during
                // ordinary staging. Keep that owner/range on the checked
                // route; unrelated module paths do not affect admission.
                if (matching_module_path) {
                    activation.discard_wave();
                    return decline();
                }
                const auto& signal = get_signal(binding->signal);
                if (signal.value_kind == ValueKind::logic9
                    && signal.resolution == ResolutionKind::std_logic
                    && signal.initial_value.width() <= 64U) {
                    if (binding->signal
                            >= direct_single_driver_logic9_word_scratch.size()
                        || direct_single_driver_logic9_word_scratch[
                               binding->signal]
                                   .active
                            == DirectSingleDriverLogic9WordUpdate::native) {
                        // The checked path first drains this native word mask
                        // to the ordered Update queue. Avoid a
                        // non-rollbackable pre-entry mutation if the native
                        // attempt later fails.
                        activation.discard_wave();
                        return decline();
                    }
                }
            }
        }

        workspace.prepared_processes.clear();
        workspace.completion_storage_identities.clear();
        workspace.final_debug_states.clear();
        for (std::size_t index = 0U; index < ready_count; ++index) {
            const auto process = prefix.tasks[index].member.process;
            const auto member = std::ranges::find(kernel.members, process,
                &RegionConeKernelMember::process);
            if (member == kernel.members.end()) {
                activation.discard_wave();
                return decline();
            }
            auto& state = get_process(process);
            const auto& operations = state.program().operations();
            const auto wait_instruction = static_cast<InstructionIndex>(
                operations.size() - 2U);
            const auto jump_instruction = static_cast<InstructionIndex>(
                operations.size() - 1U);
            if (member->final_debug_state) {
                workspace.final_debug_states.push_back({ process,
                    member->final_debug_state->source,
                    member->final_debug_state->scope });
            }
            const void* storage_identity { };
            if (!state.executor->prepare_region_completion_native(
                    process, wait_instruction, jump_instruction,
                    member->register_bindings,
                    kernel.program.register_count, &storage_identity)) {
                activation.discard_wave();
                for (const auto prepared : workspace.prepared_processes) {
                    get_process(prepared).executor
                        ->cancel_region_completion_native();
                }
                workspace.prepared_processes.clear();
                return decline();
            }
            workspace.prepared_processes.push_back(process);
            if (storage_identity == nullptr
                || std::ranges::find(
                       workspace.completion_storage_identities,
                       storage_identity)
                    != workspace.completion_storage_identities.end()) {
                activation.discard_wave();
                for (const auto prepared : workspace.prepared_processes) {
                    get_process(prepared).executor
                        ->cancel_region_completion_native();
                }
                workspace.prepared_processes.clear();
                return decline();
            }
            workspace.completion_storage_identities.push_back(
                storage_identity);
            if (member->final_debug_state) {
                state.cold().current_scope.reserve(
                    member->final_debug_state->scope.size());
            }
        }

        const auto generic_output_is_valid
            = [&](const RegionConeOutputBinding& output) {
                if (!generic_update
                    || output.update_kind != RegionUpdateKind::generic
                    || output.domain != SignalUpdateDomain::generic
                    || output.projected_mode
                        != ProjectedDelayMode::inertial
                    || output.projected_delay != 0U
                    || output.projected_rejection != 0U) {
                    return false;
                }
                const auto& signal_node
                    = region_graph->signals()[output.signal];
                const auto signal_width
                    = signal_node.descriptor.width;
                const auto& signal_state = get_signal(output.signal);
                if (signal_width == 0U || output.offset > signal_width
                    || output.width > signal_width - output.offset
                    || signal_state.initial_value.width() != signal_width
                    || driven_values[output.signal].width() != signal_width
                    || output.offset > std::numeric_limits<std::uint32_t>::max()
                        - output.width
                    || signal_node.writers.empty()
                    || signal_node.writers_unknown
                    || signal_node.dynamic_fork_writers
                    || signal_node.drivers == RegionDriverClass::unknown
                    || signal_state.event_variable
                    || signal_state.has_implicit_driver
                    || signal_state.has_charge_strength
                    || signal_state.systemverilog_scalar
                        != SystemVerilogScalarKind::None
                    || owned_driver_active(output.signal)
                    || external_driver_values[output.signal]
                    || forced_driver_values[output.signal]
                    || forced_values[output.signal]
                    || is_alias_signal(output.signal)
                    || !value_matches_kind(output.value_kind,
                        signal_state.initial_value)
                    || !value_matches_kind(output.value_kind,
                        driven_values[output.signal])) {
                    return false;
                }

                const auto owner_node
                    = output.owner < region_graph->processes().size()
                    ? &region_graph->processes()[output.owner]
                    : nullptr;
                if (owner_node == nullptr
                    || owner_node->scheduling_domain
                        != ProcessSchedulingDomain::generic
                    || owner_node->update_kind != RegionUpdateKind::generic) {
                    return false;
                }
                const auto& owner_program
                    = processes.program_view(output.owner);
                if (output.source_instruction
                    >= owner_program.operations().size()) {
                    return false;
                }
                const auto source_operation
                    = owner_program.operations().expanded(
                        output.source_instruction);
                bool source_contract_matches { };
                if (const auto* const write
                    = operation_get_if<WriteUpdate>(&source_operation)) {
                    source_contract_matches
                        = write->signal == output.signal
                        && write->domain == SignalUpdateDomain::generic
                        && output.offset == 0U
                        && output.width == signal_width;
                } else if (const auto* const slice
                    = operation_get_if<WriteUpdateSlice>(&source_operation)) {
                    source_contract_matches
                        = slice->signal == output.signal
                        && slice->domain == SignalUpdateDomain::generic
                        && slice->offset == output.offset;
                }
                if (!source_contract_matches) {
                    return false;
                }
                const auto covers_range = [&](const std::uint32_t offset,
                                              const std::uint32_t width) {
                    if (width == 0U) {
                        return offset == 0U && output.offset == 0U
                            && output.width == signal_width;
                    }
                    return offset == output.offset
                        && width == output.width;
                };
                const bool graph_owner_matches
                    = std::ranges::any_of(signal_node.writers,
                        [&](const RegionAccess& writer) {
                            return writer.process == output.owner
                                && covers_range(writer.offset, writer.width);
                        });
                const bool program_owner_matches
                    = std::ranges::any_of(owner_program.driver_regions(),
                        [&](const Process::DriverRegion& writer) {
                            return writer.signal == output.signal
                                && (writer.whole
                                        ? output.offset == 0U
                                            && output.width == signal_width
                                        : covers_range(writer.offset,
                                              writer.width));
                        });
                if (!graph_owner_matches || !program_owner_matches) {
                    return false;
                }
                for (const auto& writer : signal_node.writers) {
                    if (writer.process >= region_graph->processes().size()) {
                        return false;
                    }
                    const auto& writer_node
                        = region_graph->processes()[writer.process];
                    if (writer_node.scheduling_domain
                            != ProcessSchedulingDomain::generic
                        || writer_node.update_kind
                            != RegionUpdateKind::generic) {
                        return false;
                    }
                }

                bool driver_records_valid { true };
                driver_values[output.signal].for_each_in_process_order(
                    [&](const DriverRecord& driver) {
                        const bool graph_driver
                            = std::ranges::any_of(signal_node.writers,
                                [&](const RegionAccess& writer) {
                                    return writer.process == driver.process;
                                });
                        if (!graph_driver
                            || driver.value.width() != signal_width
                            || !value_matches_kind(output.value_kind,
                                driver.value)) {
                            driver_records_valid = false;
                        }
                    });
                if (!driver_records_valid) {
                    return false;
                }

                const bool partial
                    = output.offset != 0U || output.width != signal_width;
                if (partial && std::ranges::any_of(kernel.inputs,
                        [&](const RegionConeKernelInput& input) {
                            return input.signal == output.signal;
                        })) {
                    return false;
                }
                return true;
            };
        const auto publication_is_active
            = [&](const RegionConeOutputBinding& output) {
                return std::ranges::find(publications, &output)
                    != publications.end();
            };
        const bool valid_outputs = std::ranges::all_of(
            kernel.outputs, [&](const RegionConeOutputBinding& output) {
                if (output.owner >= processes.size()
                    || output.signal >= signals.size()
                    || output.signal >= region_graph->signals().size()
                    || output.signal >= driven_values.size()
                    || output.signal >= external_driver_values.size()
                    || output.signal >= forced_driver_values.size()
                    || output.signal >= forced_values.size()
                    || output.signal >= driver_values.size()
                    || output.signal >= module_path_destination_mask.size()
                    || output.width == 0U
                    || output.value_kind
                        != region_graph->signals()[output.signal]
                            .descriptor.value_kind
                    || output.value_kind
                        != get_signal(output.signal).value_kind
                    || get_signal(output.signal).initial_value.width()
                        != region_graph->signals()[output.signal]
                            .descriptor.width
                    || driven_values[output.signal].width()
                        != region_graph->signals()[output.signal]
                            .descriptor.width
                    || get_signal(output.signal).resolution
                        != region_graph->signals()[output.signal]
                            .descriptor.resolution
                    || output.offset
                        > std::numeric_limits<std::uint32_t>::max()
                            - output.width) {
                    return false;
                }
                if (generic_update) {
                    return generic_output_is_valid(output);
                }

                if (output.offset != 0U
                    || output.update_kind
                        != RegionUpdateKind::vhdl_projected
                    || output.domain != SignalUpdateDomain::generic
                    || output.projected_mode
                        != ProjectedDelayMode::inertial
                    || output.projected_delay != 0U
                    || output.projected_rejection != 0U
                    || region_graph->signals()[output.signal]
                            .descriptor.width != output.width
                    || get_signal(output.signal).initial_value.width()
                        != output.width
                    || get_signal(output.signal).resolution
                        != ResolutionKind::none
                    || get_signal(output.signal).event_variable
                    || get_signal(output.signal).has_implicit_driver
                    || get_signal(output.signal).has_charge_strength
                    || get_signal(output.signal).systemverilog_scalar
                        != SystemVerilogScalarKind::None
                    || module_path_destination_mask[output.signal] != 0U
                    || owned_driver_active(output.signal)
                    || external_driver_values[output.signal]
                    || forced_driver_values[output.signal]
                    || forced_values[output.signal]
                    || !driver_values[output.signal].empty()
                    || !value_matches_kind(output.value_kind,
                        get_signal(output.signal).initial_value)
                    || !value_matches_kind(output.value_kind,
                        driven_values[output.signal])) {
                    return false;
                }
                const auto& signal_node
                    = region_graph->signals()[output.signal];
                if (signal_node.drivers != RegionDriverClass::single_whole
                    || signal_node.writers.size() != 1U
                    || signal_node.writers.front().process != output.owner
                    || signal_node.writers.front().offset != 0U
                    || (signal_node.writers.front().width != 0U
                        && signal_node.writers.front().width
                            != output.width)
                    || output.owner >= region_graph->processes().size()
                    || region_graph->processes()[output.owner].update_kind
                        != RegionUpdateKind::vhdl_projected) {
                    return false;
                }
                const auto& owner_program
                    = processes.program_view(output.owner);
                if (output.source_instruction
                    >= owner_program.operations().size()) {
                    return false;
                }
                const auto source_operation
                    = owner_program.operations().expanded(
                        output.source_instruction);
                const auto* const projected
                    = operation_get_if<WriteProjected>(&source_operation);
                if (projected == nullptr
                    || projected->signal != output.signal
                    || projected->delay != output.projected_delay
                    || projected->rejection != output.projected_rejection
                    || projected->mode != output.projected_mode) {
                    return false;
                }
                // A pending update on an inactive sibling is outside this
                // callback's publication prefix and cannot conflict with it.
                const bool active_publication
                    = publication_is_active(output);
                if (active_publication
                    && std::ranges::any_of(pending_updates,
                        [&](const PendingUpdate& pending) {
                            return pending.signal == output.signal;
                        })) {
                    return false;
                }
                if (active_publication) {
                    for (std::size_t bit = 0U; bit < output.width; ++bit) {
                        const auto projected_driver = projected_drivers.find(
                            ProjectedDriverKey { output.owner, output.signal,
                                static_cast<std::uint32_t>(bit) });
                        if (projected_driver != projected_drivers.end()
                            && !projected_driver->second.transactions.empty()) {
                            return false;
                        }
                    }
                }
                return !is_alias_signal(output.signal);
            });
        if (!valid_outputs) {
            activation.discard_wave();
            for (const auto prepared : workspace.prepared_processes) {
                get_process(prepared).executor
                    ->cancel_region_completion_native();
            }
            workspace.prepared_processes.clear();
            return decline();
        }

        if (expected_publications
                > std::numeric_limits<std::size_t>::max()
                    - pending_updates.size()
            || expected_publications
                > std::numeric_limits<std::size_t>::max()
                    - pending_update_values.size()) {
            activation.discard_wave();
            for (const auto prepared : workspace.prepared_processes) {
                get_process(prepared).executor
                    ->cancel_region_completion_native();
            }
            workspace.prepared_processes.clear();
            return decline();
        }
        const auto update_count = pending_updates.size()
            + expected_publications;
        const auto value_count = pending_update_values.size()
            + expected_publications;
        pending_updates.reserve(update_count);
        pending_update_values.reserve(value_count);

        if (!workspace_entry.executor->execute(image)) {
            std::exception_ptr backend_failure;
            if (auto* const failure_backend
                = dynamic_cast<RegionKernelFailureBackend*>(
                    workspace_entry.executor.get())) {
                backend_failure = failure_backend->take_failure();
            }
            activation.discard_wave();
            for (const auto prepared : workspace.prepared_processes) {
                get_process(prepared).executor
                    ->cancel_region_completion_native();
            }
            workspace.prepared_processes.clear();
            if (backend_failure) {
                if (systemverilog_wave_profile_enabled) {
                    ++generic_projected_region_failures;
                }
                result.failure = std::move(backend_failure);
                return result;
            }
            return decline();
        }
        if (systemverilog_wave_profile_enabled) {
            ++generic_projected_region_backend_runs;
        }
        const auto registers = workspace_entry.executor
                                   ->activation_registers();
        if (registers.size() < kernel.program.register_count) {
            activation.discard_wave();
            for (const auto prepared : workspace.prepared_processes) {
                get_process(prepared).executor
                    ->cancel_region_completion_native();
            }
            workspace.prepared_processes.clear();
            return decline();
        }
        activation.stage_kernel_outputs(image, registers);
        const auto pending = activation.pending_publications();
        if (pending.size() != expected_publications
            || (!generic_update
                && pending.size() > workspace.projected_values.size())) {
            activation.discard_wave();
            for (const auto prepared : workspace.prepared_processes) {
                get_process(prepared).executor
                    ->cancel_region_completion_native();
            }
            workspace.prepared_processes.clear();
            return decline();
        }
        for (std::size_t index = 0U; index < pending.size(); ++index) {
            const auto& publication = pending[index];
            const auto& binding = publication.binding;
            if (publication.value.width() != binding.width
                || binding.owner >= processes.size()
                || binding.signal >= signals.size()
                || binding.signal >= region_graph->signals().size()
                || !value_matches_kind(
                    binding.value_kind, publication.value)
                || binding.value_kind != get_signal(binding.signal).value_kind
                || binding.value_kind
                    != region_graph->signals()[binding.signal]
                        .descriptor.value_kind
                || get_signal(binding.signal).initial_value.width()
                    != region_graph->signals()[binding.signal]
                        .descriptor.width
                || binding.offset
                    > region_graph->signals()[binding.signal]
                        .descriptor.width
                || binding.width
                    > region_graph->signals()[binding.signal]
                        .descriptor.width - binding.offset) {
                activation.discard_wave();
                for (const auto prepared : workspace.prepared_processes) {
                    get_process(prepared).executor
                        ->cancel_region_completion_native();
                }
                workspace.prepared_processes.clear();
                return decline();
            }
            if (!generic_update) {
                auto& waveform = workspace.projected_values[index];
                waveform.clear();
                waveform.push_back({ publication.value,
                    binding.projected_delay });
            }
        }
        for (const auto process : workspace.prepared_processes) {
            if (!get_process(process).executor
                     ->stage_region_completion_native(registers)) {
                activation.discard_wave();
                for (const auto prepared : workspace.prepared_processes) {
                    get_process(prepared).executor
                        ->cancel_region_completion_native();
                }
                workspace.prepared_processes.clear();
                return decline();
            }
        }

        if (!pending.empty()) {
            // Reserve the ordinary generic Update callback before staging
            // any owner writes. Generic writes retain their exact owner and
            // range; VHDL projected writes keep their inertial path.
            const auto pending_update_count = pending_updates.size();
            const auto pending_value_count = pending_update_values.size();
            try {
                schedule_update_commit();
                for (std::size_t index = 0U; index < pending.size(); ++index) {
                    const auto& publication = pending[index];
                    publication_staging_started = true;
                    if (generic_update) {
                        const auto& owner_program = processes.program_view(
                            publication.binding.owner);
                        const auto source_operation
                            = owner_program.operations().expanded(
                                publication.binding.source_instruction);
                        if (operation_holds<WriteUpdateSlice>(
                                source_operation)) {
                            stage_update_slice(
                                publication.binding.owner,
                                publication.binding.signal,
                                publication.value,
                                publication.binding.offset);
                        } else if (operation_holds<WriteUpdate>(
                                       source_operation)) {
                            stage_update(
                                publication.binding.owner,
                                publication.binding.signal,
                                publication.value,
                                SignalUpdateDomain::generic);
                        } else {
                            throw std::logic_error {
                                "generic region output lost its source update"
                            };
                        }
                    } else {
                        schedule_projected_waveform(
                            publication.binding.owner,
                            publication.binding.signal,
                            workspace.projected_values[index], std::nullopt,
                            publication.binding.projected_rejection,
                            publication.binding.projected_mode);
                    }
                }
            } catch (...) {
                // Generic staging preflight rejects only matching module-path
                // timing arcs and native Logic9 masks whose drain cannot be
                // rolled back. Queue storage is reserved above, so a failure
                // here can truncate the still-uncommitted Update entries
                // before the scheduler takes the checked member route.
                while (pending_updates.size() > pending_update_count) {
                    pending_updates.pop_back();
                }
                while (pending_update_values.size() > pending_value_count) {
                    pending_update_values.pop_back();
                }
                publication_staging_started = false;
                throw;
            }
        }

        for (const auto process : workspace.prepared_processes) {
            auto& member = get_process(process);
            member.executor->commit_region_completion_native();
            member.queued = false;
            member.waiting_on_static = false;
            member.static_trigger_mask = 0U;
            member.waiting_on_static = true;
            member.status = ProcessStatus::waiting;
        }
        for (auto& debug : workspace.final_debug_states) {
            auto& process = get_process(debug.process);
            process.clear_frontier_debug_token();
            auto& cold = process.cold();
            cold.current_source = std::move(debug.source);
            cold.current_scope.swap(debug.scope);
        }
        workspace.prepared_processes.clear();
        activation.discard_wave();
        workspace.final_debug_states.clear();
        for (auto& waveform : workspace.projected_values) {
            waveform.clear();
        }
        cleanup.committed = true;

        result.executed = ready_count;
        if (systemverilog_wave_profile_enabled) {
            ++generic_projected_region_completions;
            generic_projected_region_members += ready_count;
            generic_projected_region_publications += pending.size();
        }
        return result;
    } catch (const std::bad_alloc&) {
        if (systemverilog_wave_profile_enabled) {
            ++generic_projected_region_failures;
        }
        // A failed allocation leaves no checked member accepted. Report the
        // failure to the scheduler so it reoffers the original task prefix
        // after the workspace cleanup has rolled back native preparation.
        result.failure = std::current_exception();
        return result;
    } catch (...) {
        if (systemverilog_wave_profile_enabled) {
            ++generic_projected_region_failures;
        }
        if (publication_staging_started) {
            result.failure = std::current_exception();
            return result;
        }
        return decline();
    }
}

} // namespace fsim::runtime::simir
