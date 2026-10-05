// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_execution_context.hpp"
#include "fsim/runtime/simir_region_activation.hpp"
#include "simir_region_activation_reference.hpp"

#include <bit>
#include <iostream>
#include <new>
#include <type_traits>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] PackedLogic4 coerce_region_value_kind(
    PackedLogic4 value, const ValueKind kind)
{
    if (kind == ValueKind::logic9) {
        return value.is_logic9()
            ? value
            : value.promoted_to_logic9();
    }
    return value.is_logic9()
        ? collapse_to_logic4(value)
        : value;
}

std::vector<PackedLogic4> evaluate_region_activation_kernel(
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationImage& image)
{
    const auto& program = kernel.program;
    const auto register_value_kinds
        = process_layout_detail::ProcessLayoutAccess::view(program.register_value_kinds);
    if (register_value_kinds.size() != program.register_count) {
        throw std::logic_error {
            "region activation kernel has incomplete register kinds"
        };
    }
    std::vector<PackedLogic4> registers(program.register_count,
        PackedLogic4 { 1U, Logic4::x });
    const auto write_register = [&](const RegisterId id,
                                    PackedLogic4 value) {
        if (id >= registers.size()) {
            throw std::out_of_range {
                "region activation kernel writes an invalid register"
            };
        }
        registers[id] = coerce_region_value_kind(
            std::move(value), register_value_kinds[id]);
    };
    for (const auto& input : image.register_inputs) {
        write_register(input.register_id, input.value);
    }

    std::vector<PackedLogic4> concatenate_operands;
    std::size_t maximum_operand_count { };
    for (std::size_t index = 0U; index < program.operations.size(); ++index) {
        const auto operation = program.operations.expanded(index);
        if (const auto* concatenate
            = operation_get_if<Concatenate>(&operation)) {
            maximum_operand_count = std::max(maximum_operand_count,
                concatenate->operands.size());
        }
    }
    concatenate_operands.reserve(maximum_operand_count);

    std::size_t pc { };
    std::size_t steps { };
    const auto maximum_steps = program.operations.size() * 2U + 1U;
    while (pc < program.operations.size()) {
        if (++steps > maximum_steps) {
            throw std::logic_error {
                "region activation kernel did not terminate"
            };
        }
        const auto operation = program.operations.expanded(pc);
        if (const auto* load = operation_get_if<LoadConstant>(&operation)) {
            write_register(load->destination, load->value);
        } else if (const auto* copy = operation_get_if<CopyRegister>(&operation)) {
            if (copy->source >= registers.size()) {
                throw std::out_of_range {
                    "region activation kernel reads an invalid register"
                };
            }
            write_register(copy->destination, registers[copy->source]);
        } else if (const auto* unary = operation_get_if<UnaryNot>(&operation)) {
            if (unary->source >= registers.size()) {
                throw std::out_of_range {
                    "region activation kernel reads an invalid register"
                };
            }
            write_register(unary->destination,
                unary_not(registers[unary->source]));
        } else if (const auto* binary = operation_get_if<Binary>(&operation)) {
            if (binary->lhs >= registers.size()
                || binary->rhs >= registers.size()) {
                throw std::out_of_range {
                    "region activation kernel reads an invalid register"
                };
            }
            write_register(binary->destination,
                binary_value(binary->operation,
                    registers[binary->lhs], registers[binary->rhs]));
        } else if (const auto* reduction
            = operation_get_if<Reduction>(&operation)) {
            if (reduction->source >= registers.size()) {
                throw std::out_of_range {
                    "region activation kernel reads an invalid register"
                };
            }
            write_register(reduction->destination,
                reduce_value(reduction->operation,
                    registers[reduction->source]));
        } else if (const auto* extract = operation_get_if<Extract>(&operation)) {
            if (extract->source >= registers.size()) {
                throw std::out_of_range {
                    "region activation kernel reads an invalid register"
                };
            }
            write_register(extract->destination,
                extract_value(registers[extract->source], extract->offset,
                    extract->width));
        } else if (const auto* select
            = operation_get_if<ConditionalSelect>(&operation)) {
            if (select->destination >= registers.size()
                || select->condition >= registers.size()
                || select->when_true >= registers.size()
                || select->when_false >= registers.size()) {
                throw std::out_of_range {
                    "region activation kernel reads an invalid select register"
                };
            }
            if (register_value_kinds[select->destination] != ValueKind::logic4
                || register_value_kinds[select->condition] != ValueKind::logic4
                || register_value_kinds[select->when_true] != ValueKind::logic4
                || register_value_kinds[select->when_false] != ValueKind::logic4) {
                throw std::logic_error {
                    "region activation select is not Logic4"
                };
            }
            write_register(select->destination,
                conditional_value(registers[select->condition],
                    registers[select->when_true],
                    registers[select->when_false]));
        } else if (const auto* concatenate
            = operation_get_if<Concatenate>(&operation)) {
            concatenate_operands.clear();
            for (const auto operand : concatenate->operands) {
                if (operand >= registers.size()) {
                    throw std::out_of_range {
                        "region activation kernel reads an invalid register"
                    };
                }
                concatenate_operands.push_back(registers[operand]);
            }
            write_register(concatenate->destination,
                concatenate_values(concatenate_operands,
                    concatenate->width));
        } else if (const auto* branch = operation_get_if<Branch>(&operation)) {
            if (branch->condition >= registers.size()) {
                throw std::out_of_range {
                    "region activation kernel branches on an invalid register"
                };
            }
            const auto condition = truth_value(registers[branch->condition]);
            if (condition == Logic4::one) {
                pc = branch->when_true;
                continue;
            }
            if (condition == Logic4::zero
                || branch->unknown_policy == UnknownBranchPolicy::when_false) {
                pc = branch->when_false;
                continue;
            }
            throw std::logic_error {
                "region activation readiness branch is unknown"
            };
        } else if (operation_holds<DebugPoint>(operation)) {
            // The final marker's diagnostic state is copied to each original
            // ProcessState after the scheduler batch is committed.
        } else if (operation_holds<Halt>(operation)) {
            break;
        } else {
            throw std::logic_error {
                "region activation kernel contains an unsupported operation"
            };
        }
        ++pc;
    }
    return registers;
}

} // namespace

static_assert(std::is_nothrow_move_assignable_v<SourceLocation>);

std::vector<PackedLogic4> evaluate_region_activation_kernel_reference(
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationImage& image)
{
    return evaluate_region_activation_kernel(kernel, image);
}

bool Interpreter::Impl::can_queue_systemverilog_wave(const ProcessId id) const
{
    return !cohort_overflow_scratch_in_use
        && systemverilog_wave_member_eligible(id);
}

SchedulerBatchGroupKey
Interpreter::Impl::systemverilog_wave_batch_group_key(
    const ProcessId id) const noexcept
{
    try {
        if (!region_graph || id >= region_component_by_process.size()
            || id >= region_graph->processes().size()
            || region_runtime_generation == 0U
            || region_graph->capability_epoch(id) != 1U) {
            return { };
        }
        const auto component = region_component_by_process[id];
        if (component >= region_activation_programs.size()
            || !region_activation_programs[component]
            || component >= std::numeric_limits<std::uint64_t>::max()) {
            return { };
        }
        return { region_runtime_generation,
            static_cast<std::uint64_t>(component) + 1U };
    } catch (...) {
        // This lookup is an optional, non-unwinding queue hint. Validation is
        // still performed by the existing wave consumer before any execution.
        return { };
    }
}

bool Interpreter::Impl::systemverilog_wave_member_eligible(
    const ProcessId id) const
{
    if (!process_signal_access_inventory_complete
        || !region_graph || id >= region_graph->processes().size()
        || id >= processes.size() || process_profile_enabled
        || execution_point_hook || processes.is_compact_constant(id)) {
        return false;
    }
    const auto& node = region_graph->processes()[id];
    const auto& state = processes[id];
    return node.pure && !node.dependencies_unknown
        && !node.cyclic_or_dependent_on_cycle
        && node.scheduling_domain == ProcessSchedulingDomain::systemverilog
        && node.update_kind == RegionUpdateKind::systemverilog_active
        && region_graph->capability_epoch(id) == 1U
        && state.execution_phase == SchedulerPhase::active
        && state.waiting_on_static && !state.waiting_on_signal
        && !state.suspended && !state.halted
        && state.executor && state.executor->cohort_domain() != nullptr
        && state.executor->cohort_manages_process_state();
}

bool Interpreter::Impl::region_local_wave_component_eligible(
    const std::size_t component,
    const RegionConeActivationKernel& kernel)
{
    if (!systemverilog_local_wave_enabled || !region_graph
        || scheduler.trace_hook_installed()
        || component >= region_graph->certificate_inventory().components.size()
        || component >= region_authoritative_state_by_component.size()
        || component >= region_local_wave_state_by_component.size()
        || !region_graph->component_epochs_current(component)) {
        return false;
    }
    const auto& certificate
        = region_graph->certificate_inventory().components[component];
    auto* const state = region_authoritative_state_by_component[component].get();
    const auto& local_state
        = region_local_wave_state_by_component[component];
    if (certificate.status
            != RegionComponentCertificateStatus::structural_candidate
        || certificate.members.empty() || kernel.internal_signals.empty()
        || state == nullptr || !state->valid()
        || state->generation() != region_runtime_generation
        || !local_state
        || local_state->generation != region_runtime_generation
        || !state->values().packed_slots_bound()
        || signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook || container_element_change_hook
        || native_signal_observation_any_hook
        || native_signal_observation_required_hook) {
        return false;
    }

    const auto& graph_signals = region_graph->signals();
    const auto& layout = state->values().layout();
    const bool indexed_output_bindings
        = local_state->output_index_ready
        && local_state->output_index_generation == local_state->generation
        && local_state->internal_output_indices.size()
            == kernel.internal_signals.size()
        && (local_state->activation_kernel_identity.matches(kernel)
            || local_state->frontier_kernel_identity.matches(kernel));
    for (std::size_t internal_index = 0U;
         internal_index < kernel.internal_signals.size(); ++internal_index) {
        const auto signal = kernel.internal_signals[internal_index];
        if (signal >= graph_signals.size() || signal >= signals.size()
            || signal >= region_authoritative_component_by_signal.size()
            || region_authoritative_component_by_signal[signal] != component
            || signal >= module_path_destination_mask.size()
            || module_path_destination_mask[signal] != 0U
            || signal >= direct_signal_materialization_pending.size()
            || direct_signal_materialization_pending[signal] != 0U
            || signal >= signal_transaction_observed.size()
            || signal_transaction_observed[signal]
            || (signal < sampled_history_keys_by_clock.size()
                && !sampled_history_keys_by_clock[signal].empty())
            || signal >= dynamic_fanout.size()
            || !dynamic_fanout[signal].empty()
            || signal >= signal_container_aliases.size()
            || !signal_container_aliases[signal].empty()
            || signal >= signal_container_element_aliases.size()
            || signal_container_element_aliases[signal]
            || signal >= signal_container_aggregate_aliases.size()
            || signal_container_aggregate_aliases[signal]
            || signal >= switch_endpoint_adjacency.size()
            || !switch_endpoint_adjacency[signal].empty()
            || signal >= signal_events.size()
            || signal >= signal_event_scheduling_stamps.size()
            || signal >= signal_transactions.size()
            || signal >= signal_value_revisions.size()
            || signal >= external_driver_values.size()
            || signal >= forced_values.size()
            || signal >= forced_masks.size()
            || signal >= forced_driver_values.size()
            || signal >= forced_driver_masks.size()
            || signal >= direct_signal_aval.size()
            || signal >= direct_signal_bval.size()
            || signal >= direct_signal_last_aval.size()
            || signal >= direct_signal_last_bval.size()
            || signal >= direct_wide_signal_offsets.size()) {
            return false;
        }

        const auto& descriptor = graph_signals[signal].descriptor;
        const auto& runtime_signal = signals[signal];
        if (graph_signals[signal].observations != RegionObservation::none
            || !layout.contains(signal)
            || graph_signals[signal].writers_unknown
            || graph_signals[signal].writers.size() != 1U
            || graph_signals[signal].writers.front().offset != 0U
            || (graph_signals[signal].writers.front().width != 0U
                && graph_signals[signal].writers.front().width
                    != descriptor.width)
            || descriptor.width == 0U
            || descriptor.value_kind != ValueKind::logic4
            || (descriptor.resolution != ResolutionKind::none
                && descriptor.resolution != ResolutionKind::sv_wire)
            || descriptor.implicit_driver || descriptor.external_driver
            || descriptor.event_variable || graph_signals[signal].partial_projected_transactions
            || runtime_signal.initial_value.width() != descriptor.width
            || runtime_signal.initial_value.is_logic9()
            || runtime_signal.has_implicit_driver
            || runtime_signal.has_charge_strength
            || runtime_signal.event_variable
            || runtime_signal.public_value_reference_exposed
            || runtime_signal.systemverilog_scalar
                != SystemVerilogScalarKind::None
            || (runtime_signal.resolution != ResolutionKind::none
                && runtime_signal.resolution != ResolutionKind::sv_wire)
            || external_driver_values[signal]
            || forced_values[signal] || forced_masks[signal]
            || forced_driver_values[signal] || forced_driver_masks[signal]
            || monitor_watches(signal)) {
            return false;
        }

        const auto& signal_layout = layout.signal(signal);
        const auto owners = layout.owners(signal);
        if (signal_layout.storage_class
                != SignalDriverStorageClass::single_owner
            || signal_layout.width != descriptor.width
            || signal_layout.value_kind != ValueKind::logic4
            || owners.size() != 1U
            || owners.front().process
                != graph_signals[signal].writers.front().process
            || !state->values().packed_signal_slots_bound(signal)
            || !state->values().packed_owner_slot_bound(
                signal, owners.front().process)) {
            return false;
        }
        const auto wide_offset = direct_wide_signal_offsets[signal];
        const auto wide_word_count
            = static_cast<std::size_t>(descriptor.width / 64U)
            + static_cast<std::size_t>(descriptor.width % 64U != 0U);
        if (wide_offset > direct_wide_signal_aval.size()
            || wide_word_count
                > direct_wide_signal_aval.size() - wide_offset
            || wide_offset > direct_wide_signal_bval.size()
            || wide_word_count
                > direct_wide_signal_bval.size() - wide_offset) {
            return false;
        }

        const auto member_is_component_member = [&](const ProcessId process) {
            return process < region_component_by_process.size()
                && region_component_by_process[process] == component;
        };
        for (const auto& fanout : static_fanout_for(signal)) {
            const bool whole_signal_range = fanout.width == 0U
                && fanout.offset == 0U;
            const bool valid_finite_range = fanout.width != 0U
                && fanout.offset < descriptor.width
                && fanout.width <= descriptor.width - fanout.offset;
            if (fanout.edge != EdgeKind::any
                || (!whole_signal_range && !valid_finite_range)
                || !member_is_component_member(fanout.process)) {
                return false;
            }
        }

        auto output = kernel.outputs.end();
        if (indexed_output_bindings) {
            const auto output_index
                = local_state->internal_output_indices[internal_index];
            if (output_index == RegionLocalWaveComponentState::no_output_index
                || output_index >= kernel.outputs.size()) {
                return false;
            }
            output = kernel.outputs.begin()
                + static_cast<std::ptrdiff_t>(output_index);
        } else {
            const auto output_count = std::ranges::count(kernel.outputs, signal,
                &RegionConeOutputBinding::signal);
            output = std::ranges::find(kernel.outputs, signal,
                &RegionConeOutputBinding::signal);
            if (output_count != 1U) {
                return false;
            }
        }
        if (output == kernel.outputs.end() || output->signal != signal
            || output->owner != owners.front().process
            || output->offset != 0U || output->width != descriptor.width
            || output->value_kind != ValueKind::logic4
            || output->domain != SignalUpdateDomain::systemverilog_active
            || output->update_kind != RegionUpdateKind::systemverilog_active) {
            return false;
        }
    }
    return true;
}

void Interpreter::Impl::dispatch_systemverilog_wave_fallback(
    Scheduler&,
    const SystemVerilogWaveFallbackPayload& payload)
{
    if (payload.owner == nullptr
        || payload.process >= payload.owner->processes.size()) {
        return;
    }
    auto& owner = *payload.owner;
    owner.clear_systemverilog_readiness_member(payload.process);
    auto& member = owner.get_process(payload.process);
    member.queued = false;
    member.waiting_on_static = false;
    owner.remove_dynamic_wait(member);
    owner.execute(payload.process);
}

bool Interpreter::Impl::schedule_systemverilog_grouped_fanout(
    const SignalId signal,
    const PackedLogic4& previous,
    const PackedLogic4& current,
    const SignalChangeOrigin origin,
    const RegionPreparedOutputBatchState* const prepared_batch,
    const std::size_t prepared_slot) noexcept
{
    const bool generated_mask = prepared_batch != nullptr;
    if (generated_mask
        && signal < region_grouped_fanout_by_signal.size()
        && prepared_batch->successor_masks_verified
        && prepared_batch->runtime_generation == region_runtime_generation
        && prepared_slot < prepared_batch->successor_masks.size()
        && prepared_slot < prepared_batch->expected_successor_masks.size()
        && prepared_batch->successor_masks[prepared_slot] == 0U
        && prepared_batch->expected_successor_masks[prepared_slot] == 0U
        && region_grouped_fanout_by_signal[signal].group_count == 0U
        && static_fanout_for(signal).empty()) {
        return true;
    }
    if (origin.process_domain != ProcessSchedulingDomain::systemverilog
        || origin.phase != SchedulerPhase::active
        || !process_signal_access_inventory_complete || !region_graph
        || region_runtime_generation == 0U
        || scheduler.trace_hook_installed()
        || !fanout_cohort_grouping_enabled
        || native_process_count_profile_enabled
        || signal >= region_grouped_fanout_by_signal.size()
        || signal >= dynamic_fanout.size()
        || (generated_mask
            && (!prepared_batch->successor_masks_verified
                || prepared_batch->runtime_generation
                    != region_runtime_generation
                || prepared_slot >= prepared_batch->successor_masks.size()
                || prepared_slot
                    >= prepared_batch->expected_successor_masks.size()
                || prepared_batch->successor_masks[prepared_slot]
                    != prepared_batch->expected_successor_masks[prepared_slot]))) {
        return false;
    }
    try {
        const auto& signal_groups = region_grouped_fanout_by_signal[signal];
        if (signal_groups.generation != region_runtime_generation
            || signal_groups.group_count == 0U
            || signal_groups.group_offset > region_grouped_fanout_groups.size()
            || signal_groups.group_count
                > region_grouped_fanout_groups.size()
                    - signal_groups.group_offset
            || !dynamic_fanout[signal].empty()
            || signal_change_hook || stored_signal_change_hook
            || driver_change_hook || scalar_signal_change_hook
            || container_object_change_hook || container_element_change_hook
            || native_signal_observation_any_hook
            || native_signal_observation_required_hook) {
            if (systemverilog_wave_profile_enabled
                && signal_groups.generation == region_runtime_generation
                && signal_groups.group_count != 0U) {
                ++systemverilog_wave_profile_p3_group_fanout_declines;
            }
            return false;
        }

        if (generated_mask && signal_groups.group_count != 1U) {
            return false;
        }

        auto& queued_members = region_grouped_readiness_member_scratch;
        auto& queued_receipts = region_grouped_readiness_receipt_scratch;
        auto& queued_readiness = region_grouped_readiness_queue_scratch;
        auto& member_changed
            = region_grouped_readiness_member_changed_scratch;
        auto& queued_processes = region_grouped_readiness_process_scratch;
        bool signal_has_partial_range { };

        // Validate every immutable range span before any scheduler or A4
        // readiness state is changed. A corrupt snapshot must take the exact
        // ordinary fanout path rather than partially dispatching this signal.
        for (std::size_t group_index = 0U;
             group_index < signal_groups.group_count; ++group_index) {
            const auto& group = region_grouped_fanout_groups[
                signal_groups.group_offset + group_index];
            if (group.generation != region_runtime_generation
                || group.member_count > member_changed.size()
                || group.member_offset > region_grouped_fanout_members.size()
                || group.member_count
                    > region_grouped_fanout_members.size()
                        - group.member_offset) {
                return false;
            }
            for (std::size_t offset = 0U;
                 offset < group.member_count; ++offset) {
                const auto& binding = region_grouped_fanout_members[
                    group.member_offset + offset];
                bool changed { };
                bool has_partial_range { };
                if (binding.process >= region_component_by_process.size()
                    || region_component_by_process[binding.process]
                        != group.component
                    || !match_region_fanout_sensitivity_ranges(
                        group.component, signal,
                        binding.sensitivity_range_generation,
                        binding.sensitivity_range_offset,
                        binding.sensitivity_range_count,
                        previous, current, changed,
                        &has_partial_range)) {
                    return false;
                }
                signal_has_partial_range
                    |= has_partial_range;
            }
        }

        // Partial-range maps use the prepared single-group transaction.
        // Unprepared or multi-group cases retain the exact ordinary fanout
        // route so a later group's validation cannot follow an earlier
        // group's queue/readiness mutation.
        if (signal_has_partial_range
            && (!generated_mask || signal_groups.group_count != 1U)) {
            return false;
        }

        for (std::size_t group_index = 0U;
             group_index < signal_groups.group_count; ++group_index) {
            const auto& group = region_grouped_fanout_groups[
                signal_groups.group_offset + group_index];
            if (group.generation != region_runtime_generation
                || group.component >= region_activation_programs.size()
                || group.component >= region_readiness_mask_by_component.size()
                || !region_activation_programs[group.component]
                || !region_graph->component_epochs_current(group.component)
                || group.member_count == 0U
                || group.member_count > queued_members.size()
                || group.member_offset > region_grouped_fanout_members.size()
                || group.member_count > region_grouped_fanout_members.size()
                    - group.member_offset
                || group.component
                    >= std::numeric_limits<std::uint64_t>::max()) {
                if (systemverilog_wave_profile_enabled) {
                    ++systemverilog_wave_profile_p3_group_fanout_declines;
                }
                return false;
            }
            if (generated_mask
                && (group.component != prepared_batch->component
                    || group.component
                        >= region_authoritative_state_by_component.size()
                    || group.member_count
                        != static_cast<std::size_t>(
                            std::popcount(prepared_batch
                                ->expected_successor_masks[prepared_slot])))) {
                return false;
            }
            auto* const generated_authoritative = generated_mask
                ? region_authoritative_state_by_component[group.component].get()
                : nullptr;
            if (generated_mask
                && (generated_authoritative == nullptr
                    || !generated_authoritative->valid()
                    || generated_authoritative->generation()
                        != region_runtime_generation)) {
                return false;
            }

            const SchedulerBatchGroupKey group_key {
                region_runtime_generation,
                static_cast<std::uint64_t>(group.component) + 1U };
            std::size_t queued_count { };
            for (std::size_t offset = 0U;
                 offset < group.member_count; ++offset) {
                const auto& binding = region_grouped_fanout_members[
                    group.member_offset + offset];
                if (binding.process >= processes.size()
                    || binding.process >= region_component_by_process.size()
                    || binding.process >= region_readiness_member_index_by_process.size()
                    || binding.process >= region_readiness_queued_by_process.size()
                    || region_component_by_process[binding.process]
                        != group.component
                    || region_readiness_member_index_by_process[binding.process]
                        != binding.readiness_member) {
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_p3_group_fanout_declines;
                    }
                    return false;
                }

                if (generated_mask
                    && (binding.static_trigger_mask == 0U
                        || binding.readiness_member
                            >= region_authoritative_state_by_component[
                                group.component]->readiness().member_count())) {
                    return false;
                }

                bool sensitivity_changed { };
                if (!match_region_fanout_sensitivity_ranges(
                        group.component, signal,
                        binding.sensitivity_range_generation,
                        binding.sensitivity_range_offset,
                        binding.sensitivity_range_count,
                        previous, current, sensitivity_changed)) {
                    return false;
                }
                member_changed[offset]
                    = sensitivity_changed ? 1U : 0U;
                if (!sensitivity_changed) {
                    continue;
                }

                auto& member = get_process(binding.process);
                if (!member.waiting_on_static || member.queued) {
                    continue;
                }
                if (!can_queue_systemverilog_wave(binding.process)) {
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_p3_group_fanout_declines;
                    }
                    return false;
                }
                const auto& mask_descriptor
                    = region_readiness_mask_by_component[group.component];
                if (binding.readiness_member / 64U
                        >= mask_descriptor.word_count
                    || mask_descriptor.generation != region_runtime_generation
                    || mask_descriptor.offset > region_readiness_mask_words.size()
                    || mask_descriptor.word_count
                        > region_readiness_mask_words.size()
                            - std::min(mask_descriptor.offset,
                                region_readiness_mask_words.size())) {
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_p3_group_fanout_declines;
                    }
                    return false;
                }
                const auto mask_index = mask_descriptor.offset
                    + binding.readiness_member / 64U;
                if (mask_index >= region_readiness_mask_words.size()
                    || region_readiness_queued_by_process[binding.process]
                           .generation != 0U
                    || (region_readiness_mask_words[mask_index]
                        & (UINT64_C(1)
                            << (binding.readiness_member % 64U))) != 0U) {
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_p3_group_fanout_declines;
                    }
                    return false;
                }

                if (queued_count >= queued_members.size()) {
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_p3_group_fanout_declines;
                    }
                    return false;
                }
                auto& queued = queued_members[queued_count];
                queued = { };
                queued.stable_order = binding.process;
                queued.payload = systemverilog_wave_payload | binding.process;
                queued.fallback_descriptor
                    = detail::make_scheduler_task_descriptor<
                        SystemVerilogWaveFallbackPayload,
                        &Interpreter::Impl::dispatch_systemverilog_wave_fallback>(
                            { this, binding.process });
                queued_readiness[queued_count] = { group.component,
                    binding.readiness_member, region_runtime_generation,
                    { }, 0U, false };
                queued_processes[queued_count] = binding.process;
                ++queued_count;
            }

            if (queued_count != 0U) {
                if (!scheduler.schedule_systemverilog_readiness_group(
                        SchedulerPhase::active, *this, group_key,
                        std::span { queued_members.data(), queued_count },
                        std::span { queued_receipts.data(), queued_count })) {
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_p3_group_fanout_declines;
                    }
                    return false;
                }

                for (std::size_t queued_index = 0U;
                     queued_index < queued_count; ++queued_index) {
                    const auto& readiness = queued_readiness[queued_index];
                    const auto& mask_descriptor
                        = region_readiness_mask_by_component[
                            readiness.component];
                    const auto mask_index = mask_descriptor.offset
                        + readiness.member / 64U;
                    region_readiness_mask_words[mask_index]
                        |= UINT64_C(1) << (readiness.member % 64U);
                    const auto process = queued_processes[queued_index];
                    auto& process_state = get_process(process);
                    process_state.queued = true;
                    record_systemverilog_readiness_key(process,
                        readiness.component, readiness.member,
                        readiness.generation, queued_receipts[queued_index],
                        process_state.static_trigger_mask);
                }
                if (systemverilog_wave_profile_enabled) {
                    ++systemverilog_wave_profile_p3_group_fanout_tickets;
                }
            }

            if (generated_mask) {
                for (std::size_t offset = 0U;
                     offset < group.member_count; ++offset) {
                    if (member_changed[offset] == 0U) {
                        continue;
                    }
                    const auto& binding = region_grouped_fanout_members[
                        group.member_offset + offset];
                    static_cast<void>(generated_authoritative->readiness().mark(
                        binding.readiness_member,
                        binding.static_trigger_mask));
                }
            }

            for (std::size_t offset = 0U;
                 offset < group.member_count; ++offset) {
                if (member_changed[offset] == 0U) {
                    continue;
                }
                const auto& binding = region_grouped_fanout_members[
                    group.member_offset + offset];
                auto& member = get_process(binding.process);
                std::uint64_t trigger_mask { };
                if (!region_take_ready(signal, binding.process, trigger_mask)) {
                    trigger_mask = binding.static_trigger_mask;
                }
                member.static_trigger_mask |= trigger_mask;
                merge_systemverilog_readiness_mask(
                    binding.process, trigger_mask);
                if (member.waiting_on_static
                    && systemverilog_wave_profile_enabled) {
                    ++systemverilog_wave_profile_a2_grouped_fanout_members;
                }
                if (systemverilog_wave_profile_enabled) {
                    ++systemverilog_wave_profile_p3_group_fanout_members;
                }
            }
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_p3_group_fanout_groups;
            }
        }
        return true;
    } catch (...) {
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_p3_group_fanout_declines;
        }
        return false;
    }
}

bool Interpreter::Impl::queue_systemverilog_wave(const ProcessId id)
{
    if (!can_queue_systemverilog_wave(id)) {
        return false;
    }
    auto& state = get_process(id);
    if (state.queued) {
        return true;
    }
    const auto group_key = systemverilog_wave_batch_group_key(id);
    RegionReadinessQueueMember readiness_member;
    bool readiness_candidate { };
    if (group_key && group_key.generation == region_runtime_generation
        && group_key.group > 0U) {
        const auto component = static_cast<std::size_t>(group_key.group - 1U);
        if (component < region_readiness_mask_by_component.size()
            && id < region_readiness_member_index_by_process.size()
            && id < region_readiness_queued_by_process.size()) {
            const auto member
                = region_readiness_member_index_by_process[id];
            const auto& descriptor
                = region_readiness_mask_by_component[component];
            const auto& queued = region_readiness_queued_by_process[id];
            const bool descriptor_valid
                = descriptor.generation == group_key.generation
                && descriptor.offset <= region_readiness_mask_words.size()
                && descriptor.word_count
                    <= region_readiness_mask_words.size() - descriptor.offset;
            if (descriptor_valid
                && member != std::numeric_limits<std::size_t>::max()
                && member / 64U < descriptor.word_count
                && queued.generation == 0U
                && (region_readiness_mask_words[
                        descriptor.offset + member / 64U]
                        & (UINT64_C(1) << (member % 64U))) == 0U) {
                readiness_member = { component, member,
                    group_key.generation, { }, 0U, false };
                readiness_candidate = true;
            }
        }
    }
    // Each process keeps its own ordering key. The scheduler offers only
    // contiguous entries, never crossing an outside writer or publication.
    Scheduler::Task fallback = [this, id](Scheduler&) {
        clear_systemverilog_readiness_member(id);
        auto& member = get_process(id);
        member.queued = false;
        member.waiting_on_static = false;
        remove_dynamic_wait(member);
        execute(id);
    };
    bool ticketed { };
    SchedulerSystemVerilogKeyReceipt receipt;
    if (readiness_candidate) {
        ticketed = scheduler.schedule_systemverilog_readiness_member(
            SchedulerPhase::active, id, *this,
            systemverilog_wave_payload | id, std::move(fallback),
            group_key, &receipt);
    } else {
        scheduler.schedule_systemverilog_group_batchable(
            SchedulerPhase::active, id, *this,
            systemverilog_wave_payload | id, std::move(fallback), group_key,
            &receipt);
    }
    if (ticketed) {
        const auto& descriptor = region_readiness_mask_by_component[
            readiness_member.component];
        const auto bit = UINT64_C(1) << (readiness_member.member % 64U);
        region_readiness_mask_words[
            descriptor.offset + readiness_member.member / 64U] |= bit;
    }
    state.queued = true;
    if (readiness_candidate) {
        record_systemverilog_readiness_key(id,
            readiness_member.component, readiness_member.member,
            readiness_member.generation, receipt, state.static_trigger_mask);
    }
    return true;
}

bool Interpreter::Impl::build_systemverilog_readiness_mask(
    const std::size_t component,
    const std::span<const ExecutionContext> contexts,
    const std::size_t member_count,
    const std::span<std::uint64_t> active_mask) const noexcept
{
    if (member_count == 0U || member_count > contexts.size()
        || component >= region_readiness_mask_by_component.size()) {
        return false;
    }
    const auto& descriptor
        = region_readiness_mask_by_component[component];
    if (descriptor.generation != region_runtime_generation
        || descriptor.word_count == 0U
        || descriptor.offset > region_readiness_mask_words.size()
        || descriptor.word_count
            > region_readiness_mask_words.size() - descriptor.offset
        || active_mask.size() != descriptor.word_count) {
        return false;
    }
    const auto pending_mask
        = std::span<const std::uint64_t> { region_readiness_mask_words }
              .subspan(descriptor.offset, descriptor.word_count);
    std::fill(active_mask.begin(), active_mask.end(), 0U);
    for (std::size_t index = 0U; index < member_count; ++index) {
        const auto process = contexts[index].process;
        if (process >= region_readiness_queued_by_process.size()) {
            std::fill(active_mask.begin(), active_mask.end(), 0U);
            return false;
        }
        const auto& queued = region_readiness_queued_by_process[process];
        if (queued.component != component
            || queued.generation != region_runtime_generation
            || queued.member / 64U >= pending_mask.size()) {
            std::fill(active_mask.begin(), active_mask.end(), 0U);
            return false;
        }
        const auto word = queued.member / 64U;
        const auto bit = UINT64_C(1) << (queued.member % 64U);
        if ((pending_mask[word] & bit) == 0U
            || (active_mask[word] & bit) != 0U) {
            std::fill(active_mask.begin(), active_mask.end(), 0U);
            return false;
        }
        active_mask[word] |= bit;
    }
    return true;
}

void Interpreter::Impl::clear_systemverilog_readiness_member(
    const ProcessId process,
    const RegionFrontierKeyV1* const consumed_key,
    const std::source_location caller) noexcept
{
    if (process >= region_readiness_queued_by_process.size()) {
        return;
    }
    const auto queued = region_readiness_queued_by_process[process];
    if (queued.generation == 0U) {
        return;
    }
    if (consumed_key != nullptr && queued.key_valid
        && (queued.queued_key.time != consumed_key->time
            || queued.queued_key.delta != consumed_key->delta
            || queued.queued_key.systemverilog_round
                != consumed_key->systemverilog_round
            || queued.queued_key.stable_order
                != consumed_key->stable_order
            || queued.queued_key.sequence != consumed_key->sequence
            || queued.queued_key.process_domain
                != consumed_key->process_domain
            || queued.queued_key.phase != consumed_key->phase)) {
        // This process was requeued while the checked activation ran. Keep
        // the newer scheduler receipt instead of clearing it with the old
        // consumed payload.
        return;
    }
    if (queued.component < region_frontier_runtime_by_component.size()) {
        const auto& runtime
            = region_frontier_runtime_by_component[queued.component];
        if (runtime && runtime->runtime_generation == queued.generation
            && runtime->scheduler_state_seeded) {
            runtime->invalidate(caller);
        }
    }
    if (queued.component < region_readiness_mask_by_component.size()) {
        const auto& descriptor
            = region_readiness_mask_by_component[queued.component];
        if (descriptor.generation == queued.generation
            && descriptor.offset <= region_readiness_mask_words.size()
            && descriptor.word_count
                <= region_readiness_mask_words.size() - descriptor.offset
            && queued.member / 64U < descriptor.word_count) {
            region_readiness_mask_words[
                descriptor.offset + queued.member / 64U]
                &= ~(UINT64_C(1) << (queued.member % 64U));
        }
    }
    region_readiness_queued_by_process[process] = { };
}

void Interpreter::Impl::clear_systemverilog_readiness_prefix(
    const SchedulerBatchFrontier& frontier,
    const std::size_t count,
    const std::source_location caller) noexcept
{
    if (count > frontier.tasks.size()) {
        return;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        const auto& task = frontier.tasks[index];
        if ((task.payload & systemverilog_wave_payload) != 0U) {
            const auto process_id
                = task.payload & ~systemverilog_wave_payload;
            if (process_id > std::numeric_limits<ProcessId>::max()) {
                continue;
            }
            const auto process = static_cast<ProcessId>(process_id);
            const RegionFrontierKeyV1 consumed_key {
                frontier.time,
                frontier.delta,
                frontier.systemverilog_round,
                task.stable_order,
                task.sequence,
                static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog),
                static_cast<std::uint32_t>(SchedulerPhase::active),
            };
            clear_systemverilog_readiness_member(process, &consumed_key, caller);
        }
    }
}

bool Interpreter::Impl::prepare_region_prepared_output_batch(
    const std::size_t component,
    const RegionConeActivationKernel& kernel,
    const RegionKernelActivationState& activation,
    const std::span<const RegionConeOutputBinding* const> publications,
    RegionPreparedOutputBatchState& batch,
    std::size_t& prepared_publication_count) noexcept
{
    prepared_publication_count = 0U;
    if (!systemverilog_local_wave_enabled || !region_graph
        || scheduler.trace_hook_installed()
        || component >= region_authoritative_state_by_component.size()
        || component >= region_local_wave_state_by_component.size()
        || component >= region_graph->certificate_inventory()
                           .components.size()
        || batch.active || batch.pending_tickets != 0U
        || batch.pending_dispatch_members != 0U
        || batch.descriptors.size() != kernel.internal_signals.size()
        || batch.slot_storage.size() != kernel.internal_signals.size()
        || publications.empty()
        || publications.size() > batch.ticket_slot_indices.capacity()
        || publications.size() > batch.dispatch_tokens.capacity()
        || publications.size() > batch.dispatch_member_pending.size()
        || publications.size() > batch.group_batch_members.capacity()
        || batch.current_internal_values.capacity()
            < kernel.internal_signals.size()
        || batch.ordered_prefix.capacity() < publications.size()
        || !process_signal_access_inventory_complete
        || !region_graph->certificate_inventory().access_inventory_complete
        || !region_graph->component_epochs_current(component)
        || signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook || container_element_change_hook
        || native_signal_observation_required_hook
        || native_signal_observation_any_hook || execution_point_hook
        || has_bidirectional_switches || !module_timing_checks.empty()) {
        return false;
    }

    auto* const authoritative
        = region_authoritative_state_by_component[component].get();
    auto* const local = region_local_wave_state_by_component[component].get();
    if (authoritative == nullptr || !authoritative->valid()
        || authoritative->generation() != region_runtime_generation
        || !authoritative->values().valid()
        || !authoritative->values().packed_slots_bound()
        || local == nullptr || local->generation != region_runtime_generation
        || !local->seeded
        || local->authoritative_revision
            != authoritative->values().revision()
        || region_graph->certificate_inventory().components[component].status
            != RegionComponentCertificateStatus::structural_candidate) {
        return false;
    }

    batch.current_internal_values.clear();
    batch.ordered_prefix.clear();
    batch.ticket_slot_indices.clear();
    batch.dispatch_tokens.clear();
    batch.successor_mapping_valid = false;
    batch.successor_masks_verified = false;
    std::ranges::fill(batch.successor_masks, 0U);
    std::ranges::fill(batch.expected_successor_masks, 0U);
    batch.successor_abi.abi_version
        = kRegionPreparedOutputSuccessorMasksAbiVersionV1;
    batch.successor_abi.struct_size = sizeof(batch.successor_abi);
    batch.successor_abi.slot_count = static_cast<std::uint32_t>(
        batch.successor_masks.size());
    batch.successor_abi.reserved = 0U;
    batch.successor_abi.member_masks = batch.successor_masks.data();
    batch.dispatch_tokens.resize(publications.size());
    std::ranges::fill(batch.ticket_pending, 0U);
    std::ranges::fill(batch.dispatch_member_pending, 0U);
    batch.group_batch_members.clear();
    batch.pending_dispatch_members = 0U;
    batch.group_ticket_active = false;
    batch.dispatch_group_key = { };
    for (auto& descriptor : batch.descriptors) {
        descriptor = { };
    }
    for (auto& slot : batch.slot_storage) {
        slot.changed = 0U;
        slot.value_ready = 0U;
        slot.transaction_ready = 0U;
        slot.next_last = { };
        slot.visible_mutation.words.clear();
    }
    for (std::size_t index = 0U; index < publications.size(); ++index) {
        batch.ticket_slot_indices.push_back(no_systemverilog_update_slot);
    }

    const auto internal_signals
        = std::span<const SignalId> { kernel.internal_signals };
    const auto graph_signals = region_graph->signals();
    const auto& values = authoritative->values();
    const auto& layout = values.layout();
    if (internal_signals.empty() || graph_signals.size() != signals.size()) {
        return false;
    }

    try {
        for (std::size_t index = 0U;
             index < internal_signals.size(); ++index) {
            const auto signal = internal_signals[index];
            if (signal >= signals.size() || signal >= graph_signals.size()
                || !layout.contains(signal)) {
                return false;
            }
            const auto output = std::ranges::find(kernel.outputs, signal,
                &RegionConeOutputBinding::signal);
            if (output == kernel.outputs.end()
                || output->offset != 0U || output->width == 0U
                || output->width > 64U
                || output->value_kind != ValueKind::logic4
                || output->domain
                    != SignalUpdateDomain::systemverilog_active
                || output->update_kind
                    != RegionUpdateKind::systemverilog_active) {
                return false;
            }

            auto current = values.current(signal);
            auto previous = values.previous(signal);
            auto stored = values.stored(signal);
            const auto owner_value = values.owner_value(signal, output->owner);
            const auto& internal = activation.internal_state(signal);
            if (current.width() != output->width || current.is_logic9()
                || previous != internal.previous
                || stored != current || owner_value != current
                || internal.signal != signal
                || internal.owner != output->owner
                || internal.value_kind != ValueKind::logic4
                || internal.current != current
                || internal.raw_driver != owner_value) {
                return false;
            }
            batch.current_internal_values.push_back(current);

            auto& descriptor = batch.descriptors[index];
            descriptor.struct_size = sizeof(RegionPreparedOutputSlotV1);
            descriptor.signal_id = signal;
            descriptor.owner_id = output->owner;
            descriptor.width = output->width;
            descriptor.word_count = 1U;
            descriptor.value_kind
                = RegionPreparedOutputValueKindV1::logic4;
        }

        const auto validate_selected_signal = [&](
            const RegionConeOutputBinding& binding,
            const std::size_t internal_index) {
            const auto signal = binding.signal;
            const auto process = binding.owner;
            const auto& runtime_signal = signals[signal];
            if (runtime_signal.resolution != ResolutionKind::sv_wire
                || runtime_signal.value_kind != ValueKind::logic4
                || runtime_signal.event_variable
                || runtime_signal.has_implicit_driver
                || runtime_signal.has_charge_strength
                || runtime_signal.public_value_reference_exposed
                || runtime_signal.systemverilog_scalar
                    != SystemVerilogScalarKind::None
                || runtime_signal.initial_value.width() != binding.width
                || runtime_signal.initial_value.is_logic9()
                || signal >= driven_values.size()
                || signal >= driver_values.size()
                || signal >= direct_single_driver_routes.size()
                || signal >= signal_transactions.size()
                || signal >= signal_transaction_observed.size()
                || signal >= direct_signal_materialization_pending.size()
                || signal >= external_driver_values.size()
                || signal >= forced_values.size()
                || signal >= forced_driver_values.size()
                || signal >= dynamic_fanout.size()
                || signal >= signal_container_aliases.size()
                || signal >= signal_container_element_aliases.size()
                || signal >= signal_container_aggregate_aliases.size()
                || signal >= switch_endpoint_adjacency.size()
                || signal >= module_path_destination_mask.size()
                || signal_transaction_observed[signal]
                || direct_signal_materialization_pending[signal] != 0U
                || external_driver_values[signal] || forced_values[signal]
                || forced_driver_values[signal]
                || (signal < sampled_history_keys_by_clock.size()
                    && !sampled_history_keys_by_clock[signal].empty())
                || !dynamic_fanout[signal].empty()
                || !signal_container_aliases[signal].empty()
                || signal_container_element_aliases[signal]
                || signal_container_aggregate_aliases[signal]
                || !switch_endpoint_adjacency[signal].empty()
                || module_path_destination_mask[signal] != 0U
                || has_dynamic_waits(signal) || monitor_watches(signal)) {
                return false;
            }
            const auto& route = direct_single_driver_routes[signal];
            const auto* record = direct_single_driver_record(signal);
            const auto& graph_signal = graph_signals[signal];
            const auto& signal_layout = layout.signal(signal);
            const auto owners = layout.owners(signal);
            const auto mask = layout.owner_mask_words(signal, process);
            if (!route.active || route.process != process || record == nullptr
                || record->strength != DriveStrength { }
                || driver_values[signal].size() != 1U
                || graph_signal.writers.size() != 1U
                || graph_signal.writers.front().process != process
                || signal_layout.storage_class
                    != SignalDriverStorageClass::single_owner
                || signal_layout.width != binding.width
                || signal_layout.value_kind != ValueKind::logic4
                || owners.size() != 1U || owners.front().process != process
                || mask.size() != 1U
                || mask.front() != (binding.width == 64U
                        ? std::numeric_limits<std::uint64_t>::max()
                        : (UINT64_C(1) << binding.width) - 1U)
                || !values.packed_signal_slots_bound(signal)
                || !values.packed_owner_slot_bound(signal, process)) {
                return false;
            }

            auto& slot = batch.slot_storage[internal_index];
            const auto previous_word
                = values.previous(signal).unchecked_low_word();
            const auto stored_word
                = values.stored(signal).unchecked_low_word();
            const auto owner_word
                = values.owner_value(signal, process).unchecked_low_word();
            slot.owner_mask[0U] = mask.front();
            slot.old_owner[0U] = owner_word.aval;
            slot.old_owner[1U] = owner_word.bval;
            slot.old_stored[0U] = stored_word.aval;
            slot.old_stored[1U] = stored_word.bval;
            // The prepared emitter writes old current into LAST only when
            // the resolved value changes. For an equal-value transaction,
            // retain the existing LAST value in the destination.
            slot.next_last[0U] = previous_word.aval;
            slot.next_last[1U] = previous_word.bval;
            values.prepare_owner_change_into(slot.visible_mutation,
                signal, process, values.owner_value(signal, process),
                values.current(signal), values.stored(signal));
            if (slot.visible_mutation.words.size() != 1U) {
                return false;
            }
            auto& word = slot.visible_mutation.words.front();
            auto& descriptor = batch.descriptors[internal_index];
            descriptor.selected = 1U;
            descriptor.owner_mask = slot.owner_mask.data();
            descriptor.old_current_aval = &word.old_current[0U];
            descriptor.old_current_bval = &word.old_current[1U];
            descriptor.old_owner_aval = slot.old_owner.data();
            descriptor.old_owner_bval = slot.old_owner.data() + 1U;
            descriptor.next_current_aval = &word.new_current[0U];
            descriptor.next_current_bval = &word.new_current[1U];
            descriptor.next_last_aval = slot.next_last.data();
            descriptor.next_last_bval = slot.next_last.data() + 1U;
            descriptor.next_stored_aval = &word.new_stored[0U];
            descriptor.next_stored_bval = &word.new_stored[1U];
            descriptor.next_owner_aval = &word.new_owner[0U];
            descriptor.next_owner_bval = &word.new_owner[1U];
            descriptor.changed = &slot.changed;
            descriptor.value_ready = &slot.value_ready;
            descriptor.transaction_ready = &slot.transaction_ready;
            return true;
        };

        for (std::size_t publication_index = 0U;
             publication_index < publications.size(); ++publication_index) {
            const auto* const binding = publications[publication_index];
            if (binding == nullptr) {
                return false;
            }
            const auto internal = std::ranges::lower_bound(
                internal_signals, binding->signal);
            if (internal == internal_signals.end()
                || *internal != binding->signal) {
                break;
            }
            const auto internal_index = static_cast<std::size_t>(
                internal - internal_signals.begin());
            const auto output = std::ranges::find(kernel.outputs,
                binding->signal, &RegionConeOutputBinding::signal);
            if (batch.descriptors[internal_index].selected != 0U
                || output == kernel.outputs.end() || *output != *binding
                || binding->owner != batch.descriptors[internal_index].owner_id
                || binding->width > 64U
                || binding->value_kind != ValueKind::logic4
                || binding->domain
                    != SignalUpdateDomain::systemverilog_active
                || binding->update_kind
                    != RegionUpdateKind::systemverilog_active
                || !validate_selected_signal(*binding, internal_index)) {
                break;
            }
            batch.ordered_prefix.push_back(*binding);
            batch.ticket_slot_indices[publication_index] = internal_index;
            ++prepared_publication_count;
        }
        if (prepared_publication_count == 0U) {
            return false;
        }
        batch.component = component;
        batch.runtime_generation = region_runtime_generation;
        batch.component_generation = authoritative->generation();
        batch.expected_value_revision = values.revision();
        batch.successor_mapping_valid
            = prepare_region_prepared_output_successors(
                component, kernel, batch);
        batch.pending_tickets = 0U;
        batch.sealed_tickets = 0U;
        batch.owner = this;
        if (batch.next_dispatch_group != 0U
            && region_runtime_generation != 0U) {
            batch.dispatch_group_key = {
                region_runtime_generation, batch.next_dispatch_group };
            batch.next_dispatch_group
                = batch.next_dispatch_group
                        == std::numeric_limits<std::uint64_t>::max()
                ? 0U : batch.next_dispatch_group + 1U;
        }
        batch.cancelled = false;
        batch.active = true;
        return true;
    } catch (...) {
        batch.cancel_unattached();
        prepared_publication_count = 0U;
        return false;
    }
}

bool Interpreter::Impl::prepare_region_prepared_output_successors(
    const std::size_t component,
    const RegionConeActivationKernel& kernel,
    RegionPreparedOutputBatchState& batch) noexcept
{
    batch.successor_mapping_valid = false;
    batch.successor_masks_verified = false;
    if (batch.expected_successor_masks.size()
            != kernel.internal_signals.size()
        || batch.successor_masks.size() != kernel.internal_signals.size()
        || batch.successor_mapping_by_slot.size()
            != kernel.internal_signals.size()
        || batch.successor_abi.slot_count
            != kernel.internal_signals.size()
        || batch.successor_abi.member_masks != batch.successor_masks.data()
        || batch.runtime_generation != region_runtime_generation
        || batch.component != component
        || component >= region_authoritative_state_by_component.size()) {
        return false;
    }

    std::ranges::fill(batch.expected_successor_masks, 0U);
    std::ranges::fill(batch.successor_mapping_by_slot,
        RegionPreparedSuccessorSlotMap { });
    for (std::size_t slot = 0U;
         slot < kernel.internal_signals.size(); ++slot) {
        const auto signal = kernel.internal_signals[slot];
        if (signal >= region_grouped_fanout_by_signal.size()
            || signal >= region_prepared_successor_by_signal.size()
            || signal >= dynamic_fanout.size()) {
            return false;
        }
        auto& slot_map = batch.successor_mapping_by_slot[slot];
        slot_map.signal = signal;

        const auto& fanout = region_grouped_fanout_by_signal[signal];
        if (fanout.group_count == 0U) {
            if (fanout.generation != 0U
                || !static_fanout_for(signal).empty()
                || !dynamic_fanout[signal].empty()) {
                return false;
            }
            continue;
        }
        if (fanout.generation != region_runtime_generation
            || fanout.group_count != 1U
            || fanout.group_offset >= region_grouped_fanout_groups.size()) {
            return false;
        }
        const auto& group
            = region_grouped_fanout_groups[fanout.group_offset];
        const auto& mapping
            = region_prepared_successor_by_signal[signal];
        if (group.generation != region_runtime_generation
            || group.component != component || group.member_count == 0U
            || group.member_count > 64U
            || group.member_offset > region_grouped_fanout_members.size()
            || group.member_count
                > region_grouped_fanout_members.size()
                    - group.member_offset
            || mapping.generation != region_runtime_generation
            || mapping.component != component
            || mapping.reader_count != group.member_count
            || mapping.reader_count > 64U
            || mapping.reader_offset
                > region_prepared_successor_readers.size()
            || mapping.reader_count
                > region_prepared_successor_readers.size()
                    - mapping.reader_offset) {
            return false;
        }
        const auto expected_mask = mapping.reader_count == 64U
            ? UINT64_MAX
            : (UINT64_C(1) << mapping.reader_count) - 1U;
        if (mapping.expected_mask != expected_mask) {
            return false;
        }
        slot_map.reader_offset = mapping.reader_offset;
        slot_map.reader_count = mapping.reader_count;
        slot_map.component = mapping.component;
        slot_map.generation = mapping.generation;
        slot_map.expected_mask = mapping.expected_mask;
        batch.expected_successor_masks[slot] = mapping.expected_mask;
    }
    batch.successor_mapping_valid = true;
    return true;
}

bool Interpreter::Impl::validate_region_prepared_output_successors(
    const RegionConeActivationKernel& kernel,
    const RegionPreparedOutputBatchState& batch) const noexcept
{
    if (!batch.successor_mapping_valid
        || batch.successor_masks.size() != kernel.internal_signals.size()
        || batch.expected_successor_masks.size()
            != kernel.internal_signals.size()
        || batch.successor_mapping_by_slot.size()
            != kernel.internal_signals.size()
        || batch.descriptors.size() != kernel.internal_signals.size()
        || batch.slot_storage.size() != kernel.internal_signals.size()
        || batch.successor_abi.abi_version
            != kRegionPreparedOutputSuccessorMasksAbiVersionV1
        || batch.successor_abi.struct_size != sizeof(batch.successor_abi)
        || batch.successor_abi.slot_count != kernel.internal_signals.size()
        || batch.successor_abi.reserved != 0U
        || batch.successor_abi.member_masks != batch.successor_masks.data()) {
        return false;
    }
    for (std::size_t slot = 0U;
         slot < kernel.internal_signals.size(); ++slot) {
        const auto& descriptor = batch.descriptors[slot];
        const auto& storage = batch.slot_storage[slot];
        if (descriptor.selected > 1U || storage.changed > 1U) {
            return false;
        }
        const auto expected = descriptor.selected != 0U
                && storage.changed != 0U
            ? batch.expected_successor_masks[slot] : 0U;
        const auto& mapping = batch.successor_mapping_by_slot[slot];
        if (mapping.signal != kernel.internal_signals[slot]
            || mapping.expected_mask
                != batch.expected_successor_masks[slot]
            || mapping.reader_count > 64U
            || (mapping.reader_count != 0U
                && (mapping.generation != batch.runtime_generation
                    || mapping.component != batch.component))
            || batch.successor_masks[slot] != expected) {
            return false;
        }
    }
    return true;
}

bool Interpreter::Impl::prepare_region_direct_ready_window(
    const RegionKernelActivationImage& image,
    const std::span<const PackedLogic4> boundary_inputs,
    const std::span<const std::uint64_t> readiness_mask,
    RegionPreparedOutputBatchState& batch) noexcept
{
    if (!batch.direct_ready_mapping_valid
        || batch.direct_input_sources.size()
            != batch.direct_input_slots.size()
        || batch.direct_input_slots.empty()
        || batch.direct_input_slots.size()
            > std::numeric_limits<std::uint32_t>::max()
        || batch.current_internal_values.size()
            != batch.direct_internal_count
        || boundary_inputs.size() != batch.direct_boundary_input_count
        || readiness_mask.empty()
        || batch.direct_ready_member_count
            > std::numeric_limits<std::uint32_t>::max()
        || readiness_mask.size()
            != (batch.direct_ready_member_count / 64U
                + (batch.direct_ready_member_count % 64U != 0U ? 1U : 0U))
        || readiness_mask.size()
            > std::numeric_limits<std::uint32_t>::max()
        || batch.direct_ready_member_count == 0U
        || image.generation == 0U
        || image.scheduler_prefix.frontier_generation == 0U) {
        return false;
    }

    for (std::size_t index = 0U;
         index < batch.direct_input_sources.size(); ++index) {
        const auto& source = batch.direct_input_sources[index];
        const PackedLogic4* value { };
        if (source.internal) {
            if (source.value_index
                >= batch.current_internal_values.size()) {
                return false;
            }
            value = &batch.current_internal_values[source.value_index];
        } else {
            if (source.value_index >= boundary_inputs.size()) {
                return false;
            }
            value = &boundary_inputs[source.value_index];
        }
        if (source.width == 0U || source.width > 64U
            || value->width() != source.width || value->is_logic9()) {
            return false;
        }
        const auto aval = value->aval_words();
        const auto bval = value->bval_words();
        if (aval.size() != 1U || bval.size() != 1U) {
            return false;
        }
        auto& descriptor = batch.direct_input_slots[index];
        descriptor = { };
        descriptor.struct_size = sizeof(RegionDirectReadyInputSlotV1);
        descriptor.signal_id = source.signal;
        descriptor.register_id = source.register_id;
        descriptor.width = source.width;
        descriptor.word_count = 1U;
        descriptor.aval = aval.data();
        descriptor.bval = bval.data();
    }

    auto& window = batch.direct_ready_window;
    window.abi_version = kRegionDirectReadyWindowAbiVersionV1;
    window.struct_size = sizeof(RegionDirectReadyWindowV1);
    window.activation_generation = image.generation;
    window.frontier_generation
        = image.scheduler_prefix.frontier_generation;
    window.member_count = static_cast<std::uint32_t>(
        batch.direct_ready_member_count);
    window.readiness_word_count = static_cast<std::uint32_t>(
        readiness_mask.size());
    window.readiness_mask = readiness_mask.data();
    window.input_slot_count = static_cast<std::uint32_t>(
        batch.direct_input_slots.size());
    window.input_slots = batch.direct_input_slots.data();
    return true;
}

void Interpreter::Impl::attach_prepared_output_ticket(
    const SystemVerilogUpdateToken& token,
    const std::shared_ptr<RegionPreparedOutputBatchState>& batch,
    const std::size_t slot) noexcept
{
    if (batch == nullptr || token.owner != this
        || token.slot >= systemverilog_update_slots.size()
        || slot >= batch->ticket_pending.size()
        || !batch->active || batch->cancelled
        || batch->ticket_pending[slot] != 0U) {
        std::terminate();
    }
    auto& update = systemverilog_update_slots[token.slot];
    if (!update.occupied || update.generation != token.generation
        || update.prepared_output_batch) {
        std::terminate();
    }
    update.prepared_output_batch = batch;
    update.prepared_output_slot = slot;
    update.prepared_output_dispatch_ordinal
        = no_systemverilog_update_slot;
    batch->ticket_pending[slot] = 1U;
    ++batch->pending_tickets;
}

void Interpreter::Impl::attach_prepared_output_group_member(
    const SystemVerilogUpdateToken& token,
    const std::shared_ptr<RegionPreparedOutputBatchState>& batch,
    const std::size_t prepared_slot,
    const std::size_t dispatch_ordinal) noexcept
{
    if (batch == nullptr || token.owner != this
        || token.slot >= systemverilog_update_slots.size()
        || dispatch_ordinal >= batch->dispatch_tokens.size()
        || dispatch_ordinal >= batch->dispatch_member_pending.size()
        || !batch->group_ticket_active
        || !batch->dispatch_group_key
        || batch->dispatch_member_pending[dispatch_ordinal] != 0U
        || (prepared_slot != no_systemverilog_update_slot
            && (prepared_slot >= batch->ticket_pending.size()
                || batch->ticket_pending[prepared_slot] != 0U))) {
        std::terminate();
    }
    auto& update = systemverilog_update_slots[token.slot];
    if (!update.occupied || update.generation != token.generation
        || update.prepared_output_batch) {
        std::terminate();
    }
    update.prepared_output_batch = batch;
    update.prepared_output_slot = prepared_slot;
    update.prepared_output_dispatch_ordinal = dispatch_ordinal;
    batch->dispatch_tokens[dispatch_ordinal] = token;
    batch->dispatch_member_pending[dispatch_ordinal] = 1U;
    ++batch->pending_dispatch_members;
    if (prepared_slot != no_systemverilog_update_slot) {
        batch->ticket_pending[prepared_slot] = 1U;
        ++batch->pending_tickets;
    }
    batch->active = true;
}

SchedulerBatchResult Interpreter::Impl::RegionPreparedOutputBatchState::execute(
    Scheduler& active_scheduler,
    const std::span<const std::uint64_t> payloads)
{
    [[maybe_unused]] const auto keep_alive = shared_from_this();
    if (owner == nullptr) {
        return { };
    }
    return owner->execute_prepared_output_group(
        *this, active_scheduler, payloads);
}

SchedulerBatchResult Interpreter::Impl::execute_prepared_output_group(
    RegionPreparedOutputBatchState& batch,
    Scheduler& active_scheduler,
    const std::span<const std::uint64_t> payloads)
{
    SchedulerBatchResult result;
    const auto frontier = active_scheduler.current_batch_frontier();
    if (!batch.group_ticket_active || !batch.active
        || !batch.dispatch_group_key
        || batch.dispatch_group_key.generation != batch.runtime_generation
        || batch.runtime_generation != region_runtime_generation
        || payloads.empty() || active_scheduler.stop_requested()
        || active_scheduler.current_phase() != SchedulerPhase::active
        || !frontier || frontier->generation == 0U
        || frontier->cursor != 0U || frontier->end != payloads.size()
        || frontier->tasks.size() != payloads.size()
        || frontier->phase != SchedulerPhase::active
        || frontier->time != active_scheduler.now()
        || frontier->delta != active_scheduler.delta()
        || frontier->systemverilog_round
            != active_scheduler.systemverilog_round()) {
        return result;
    }

    // Validate the whole scheduler-owned prefix before dispatching any member.
    // A stale batch declines at the first callback and keeps the ordinary
    // per-token fallback and suffix in the scheduler's existing ticket.
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        const auto ordinal = payloads[index];
        if (ordinal >= batch.dispatch_tokens.size()
            || ordinal >= batch.dispatch_member_pending.size()
            || batch.dispatch_member_pending[ordinal] == 0U
            || frontier->tasks[index].payload != ordinal) {
            return result;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (payloads[previous] == ordinal) {
                return result;
            }
        }
        const auto& token = batch.dispatch_tokens[ordinal];
        if (token.owner != this
            || token.slot >= systemverilog_update_slots.size()) {
            return result;
        }
        const auto& update = systemverilog_update_slots[token.slot];
        if (!update.occupied || update.generation != token.generation
            || update.prepared_output_batch.get() != &batch
            || update.prepared_output_dispatch_ordinal != ordinal
            || update.process != frontier->tasks[index].stable_order) {
            return result;
        }
    }

    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_prepared_output_group_dispatches;
        systemverilog_wave_profile_prepared_output_group_members
            += static_cast<std::uint64_t>(payloads.size());
    }

    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        if (active_scheduler.stop_requested()) {
            return result;
        }
        const auto ordinal = static_cast<std::size_t>(payloads[index]);
        const auto token = batch.dispatch_tokens[ordinal];
        if (token.owner != this
            || token.slot >= systemverilog_update_slots.size()) {
            return result;
        }
        const auto& update = systemverilog_update_slots[token.slot];
        if (!update.occupied || update.generation != token.generation
            || update.prepared_output_batch.get() != &batch
            || update.prepared_output_dispatch_ordinal != ordinal) {
            return result;
        }
        try {
            if (update.region_internal_output) {
                dispatch_region_internal_update(active_scheduler, token);
            } else {
                dispatch_systemverilog_update(active_scheduler, token);
            }
        } catch (...) {
            // Dispatch leaves the slot occupied when a pre-commit role flush
            // fails, so the scheduler must retain that original key.
            const bool consumed
                = token.slot >= systemverilog_update_slots.size()
                || !systemverilog_update_slots[token.slot].occupied
                || systemverilog_update_slots[token.slot].generation
                    != token.generation;
            result.executed = index + (consumed ? 1U : 0U);
            result.failure = std::current_exception();
            return result;
        }
        ++result.executed;
    }
    return result;
}

detail::SchedulerTaskDescriptor
Interpreter::Impl::RegionForwardingPrivateStageBatch::make_fallback_descriptor(
    const std::uint64_t payload) noexcept
{
    if (owner == nullptr) {
        return { };
    }
    if ((payload & private_output_payload_flag) != 0U) {
        const RegionForwardingPrivateOutputDispatchToken token {
            this, payload & payload_ordinal_mask };
        if (owner->systemverilog_wave_profile_enabled) {
            ++owner->systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors;
        }
        return detail::make_scheduler_task_descriptor<
            RegionForwardingPrivateOutputDispatchToken,
            &Interpreter::Impl::dispatch_region_forwarding_private_output>(token);
    }
    if (payload >= tokens.size()) {
        return { };
    }
    const auto token = tokens[static_cast<std::size_t>(payload)];
    bool internal_output { };
    if (token.owner == owner
        && token.slot < owner->systemverilog_update_slots.size()) {
        const auto& update = owner->systemverilog_update_slots[token.slot];
        internal_output = update.occupied
            && update.generation == token.generation
            && update.region_internal_output;
    }
    if (owner->systemverilog_wave_profile_enabled) {
        ++owner->systemverilog_wave_profile_region_forwarding_stage_fallback_descriptors;
    }
    return internal_output
        ? detail::make_scheduler_task_descriptor<
            SystemVerilogUpdateToken,
            &Interpreter::Impl::dispatch_region_internal_update>(token)
        : detail::make_scheduler_task_descriptor<
            SystemVerilogUpdateToken,
            &Interpreter::Impl::dispatch_systemverilog_update>(token);
}

void Interpreter::Impl::dispatch_region_forwarding_private_output(
    Scheduler&,
    const RegionForwardingPrivateOutputDispatchToken& token)
{
    auto* const batch = token.batch;
    if (batch == nullptr || batch->owner == nullptr
        || token.ordinal >= batch->private_outputs.size()) {
        return;
    }
    auto& owner = *batch->owner;
    auto& output = batch->private_outputs[
        static_cast<std::size_t>(token.ordinal)];
    if (!output.pending) {
        return;
    }

    const auto process = output.process;
    const auto signal = output.signal;
    const auto component = output.component;
    const auto generation = output.runtime_generation;
    const auto origin = output.origin;
    bool role_deferred { };
    bool publication_committed { };
    if (owner.systemverilog_wave_profile_enabled) {
        ++owner.systemverilog_wave_profile_region_forwarding_private_parent_dispatches;
    }

    try {
        if (component == batch->component
            && generation == batch->runtime_generation
            && owner.publish_region_internal_value(
                component, generation, process, signal, output.value, origin,
                nullptr, no_systemverilog_update_slot, &role_deferred,
                &publication_committed)) {
            output.pending = false;
            output.role_deferred = role_deferred;
            if (owner.systemverilog_wave_profile_enabled) {
                ++owner.systemverilog_wave_profile_a2_local_update_dispatches;
            }
            return;
        }
    } catch (...) {
        if (publication_committed) {
            output.pending = false;
        }
        throw;
    }

    // Keep the original batch row live until the required flush succeeds.
    // A failed barrier is a pre-commit failure and must leave this exact key
    // retryable by the scheduler.
    owner.require_region_forwarding_role_journal_flushed_for_signal(signal);
    output.pending = false;
    if (owner.systemverilog_wave_profile_enabled) {
        ++owner.systemverilog_wave_profile_region_forwarding_private_parent_fallbacks;
        ++owner.systemverilog_wave_profile_a2_ordinary_internal_updates;
        ++owner.systemverilog_wave_profile_a2_local_update_fallbacks;
    }
    owner.invalidate_region_local_wave_signal(signal);
    owner.commit_driver(process, signal, std::move(output.value), origin, false);
}

SchedulerBatchResult Interpreter::Impl::RegionForwardingPrivateStageBatch::execute(
    Scheduler& active_scheduler,
    const std::span<const std::uint64_t> payloads)
{
    if (owner == nullptr) {
        return { };
    }
    return owner->execute_region_forwarding_private_stage_batch(
        *this, active_scheduler, payloads);
}

SchedulerBatchResult
Interpreter::Impl::execute_region_forwarding_private_stage_batch(
    RegionForwardingPrivateStageBatch& batch,
    Scheduler& active_scheduler,
    const std::span<const std::uint64_t> payloads)
{
    SchedulerBatchResult result;
    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_region_forwarding_stage_attempts;
    }
    const auto decline = [&]() {
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_region_forwarding_stage_declines;
        }
        return result;
    };
    const auto frontier = active_scheduler.current_batch_frontier();
    if (batch.owner != this || batch.component == no_systemverilog_update_slot
        || batch.component >= region_local_wave_state_by_component.size()
        || batch.runtime_generation == 0U
        || batch.runtime_generation != region_runtime_generation
        || batch.time != active_scheduler.now()
        || payloads.empty() || payloads.size() > 64U
        || active_scheduler.stop_requested()
        || active_scheduler.trace_hook_installed()
        || active_scheduler.current_phase() != SchedulerPhase::active
        || !frontier || frontier->generation == 0U
        || frontier->cursor != 0U || frontier->end != payloads.size()
        || frontier->tasks.size() != payloads.size()
        || frontier->phase != SchedulerPhase::active
        || frontier->time != active_scheduler.now()
        || frontier->delta != active_scheduler.delta()
        || frontier->systemverilog_round
            != active_scheduler.systemverilog_round()) {
        return decline();
    }

    // The scheduler may offer only the prefix before a pre-existing foreign
    // key. Validate the entire offered prefix before dispatching any original
    // update token; a decline therefore leaves every fallback untouched.
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        const auto payload = payloads[index];
        const bool private_output
            = (payload & RegionForwardingPrivateStageBatch::
                    private_output_payload_flag) != 0U;
        const auto ordinal = payload
            & RegionForwardingPrivateStageBatch::payload_ordinal_mask;
        if (frontier->tasks[index].payload != payload) {
            return decline();
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (payloads[previous] == payload) {
                return decline();
            }
        }
        if (private_output) {
            if (ordinal >= batch.private_outputs.size()) {
                return decline();
            }
            const auto& output = batch.private_outputs[
                static_cast<std::size_t>(ordinal)];
            if (!output.pending || output.component != batch.component
                || output.runtime_generation != batch.runtime_generation
                || output.process != frontier->tasks[index].stable_order
                || output.signal >= signals.size()
                || output.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || output.origin.phase != SchedulerPhase::active
                || output.value.width() == 0U
                || output.value.is_logic9()) {
                return decline();
            }
        } else {
            if (ordinal >= batch.tokens.size()) {
                return decline();
            }
            const auto token = batch.tokens[
                static_cast<std::size_t>(ordinal)];
            if (token.owner != this
                || token.slot >= systemverilog_update_slots.size()) {
                return decline();
            }
            const auto& update = systemverilog_update_slots[token.slot];
            if (!update.occupied || update.generation != token.generation
                || update.process != frontier->tasks[index].stable_order
                || update.origin.process_domain
                    != ProcessSchedulingDomain::systemverilog
                || update.origin.phase != SchedulerPhase::active
                || (update.region_internal_output
                    && (update.region_local_component != batch.component
                        || update.region_local_generation
                            != batch.runtime_generation))
                || (!update.region_internal_output
                    && update.region_local_component
                        != no_systemverilog_update_slot)) {
                return decline();
            }
        }
        if (index != 0U
            && !(frontier->tasks[index - 1U].stable_order
                    < frontier->tasks[index].stable_order
                || (frontier->tasks[index - 1U].stable_order
                        == frontier->tasks[index].stable_order
                    && frontier->tasks[index - 1U].sequence
                        < frontier->tasks[index].sequence))) {
            return decline();
        }
    }

    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_region_forwarding_stage_dispatches;
    }
    // Each member retains its original stable key in the ticket. Dispatch one
    // member at a time so the scheduler can retire that key and compare the
    // untouched suffix against already-running foreign entries. Work created
    // by this callback is inserted into the next Active round.
    const auto dispatch_count = payloads.size() > 1U ? 1U : payloads.size();
    for (std::size_t index = 0U; index < dispatch_count; ++index) {
        if (active_scheduler.stop_requested()) {
            break;
        }
        const auto payload = payloads[index];
        try {
            if ((payload & RegionForwardingPrivateStageBatch::
                    private_output_payload_flag) != 0U) {
                const RegionForwardingPrivateOutputDispatchToken token {
                    &batch,
                    payload
                        & RegionForwardingPrivateStageBatch::
                            payload_ordinal_mask };
                dispatch_region_forwarding_private_output(
                    active_scheduler, token);
            } else {
                const auto token = batch.tokens[
                    static_cast<std::size_t>(payload)];
                const auto& update
                    = systemverilog_update_slots[token.slot];
                if (update.region_internal_output) {
                    dispatch_region_internal_update(active_scheduler, token);
                } else {
                    dispatch_systemverilog_update(active_scheduler, token);
                }
            }
        } catch (...) {
            bool consumed { };
            if ((payload & RegionForwardingPrivateStageBatch::
                    private_output_payload_flag) != 0U) {
                const auto ordinal = payload
                    & RegionForwardingPrivateStageBatch::payload_ordinal_mask;
                consumed = ordinal >= batch.private_outputs.size()
                    || !batch.private_outputs[
                        static_cast<std::size_t>(ordinal)].pending;
            } else {
                const auto ordinal = payload
                    & RegionForwardingPrivateStageBatch::payload_ordinal_mask;
                if (ordinal >= batch.tokens.size()) {
                    consumed = true;
                } else {
                    const auto token = batch.tokens[
                        static_cast<std::size_t>(ordinal)];
                    consumed = token.owner != this
                        || token.slot >= systemverilog_update_slots.size()
                        || !systemverilog_update_slots[token.slot].occupied
                        || systemverilog_update_slots[token.slot].generation
                            != token.generation;
                }
            }
            result.executed = index + (consumed ? 1U : 0U);
            result.failure = std::current_exception();
            break;
        }
        ++result.executed;
    }
    if (systemverilog_wave_profile_enabled) {
        systemverilog_wave_profile_region_forwarding_stage_members
            += static_cast<std::uint64_t>(result.executed);
    }
    return result;
}

bool Interpreter::Impl::try_flush_region_forwarding_role_journal(
    const std::size_t component) noexcept
{
    if (component >= region_local_wave_state_by_component.size()) {
        return true;
    }
    auto* const local
        = region_local_wave_state_by_component[component].get();
    if (local == nullptr || !local->forwarding_results) {
        return true;
    }
    auto& bank = *local->forwarding_results;
    if (bank.applied_role_mutations.empty()) {
        return bank.applied_role_metadata.empty();
    }
    if (bank.applied_role_mutations.size()
            != bank.applied_role_metadata.size()
        || region_forwarding_role_journal_nonempty_components == 0U
        || component >= region_authoritative_state_by_component.size()
        || component >= region_activation_programs.size()
        || !region_activation_programs[component]
        || local->generation == 0U
        || local->generation != bank.runtime_generation
        || local->generation != region_runtime_generation) {
        return false;
    }

    auto* const state
        = region_authoritative_state_by_component[component].get();
    if (state == nullptr || !state->valid()
        || state->generation() != local->generation
        || !state->values().requires_prewrite_unbind()
        || !state->values().packed_slots_bound()
        || !bank.role_flush_scratch.ready()) {
        return false;
    }

    const auto& values = state->values();
    const auto& layout = values.layout();
    const auto& outputs
        = region_activation_programs[component]->activation_kernel.outputs;
    for (std::size_t index = 0U;
         index < bank.applied_role_mutations.size(); ++index) {
        const auto& mutation = bank.applied_role_mutations[index];
        const auto& metadata = bank.applied_role_metadata[index];
        if (metadata.output_index >= outputs.size()
            || metadata.signal >= signals.size()
            || metadata.signal >= signal_events.size()
            || metadata.signal >= signal_event_scheduling_stamps.size()
            || metadata.signal >= signal_transactions.size()
            || metadata.signal >= signal_value_revisions.size()
            || metadata.signal >= direct_signal_materialization_pending.size()
            || direct_signal_materialization_pending[metadata.signal] != 0U
            || metadata.origin.process_domain
                != ProcessSchedulingDomain::systemverilog
            || metadata.origin.phase != SchedulerPhase::active
            || metadata.callback_systemverilog_round == 0U
            || mutation.signal != metadata.signal
            || mutation.words.empty()
            || metadata.expected_signal_event != signal_events[metadata.signal]
            || metadata.expected_transaction
                != signal_transactions[metadata.signal]
            || metadata.expected_value_revision
                != signal_value_revisions[metadata.signal]) {
            return false;
        }
        const auto& expected_stamp
            = metadata.expected_event_stamp;
        const auto& live_stamp
            = signal_event_scheduling_stamps[metadata.signal];
        if (expected_stamp.origin.process_domain
                != live_stamp.origin.process_domain
            || expected_stamp.origin.phase != live_stamp.origin.phase
            || expected_stamp.systemverilog_round
                != live_stamp.systemverilog_round) {
            return false;
        }
        const auto& output = outputs[metadata.output_index];
        if (output.signal != metadata.signal
            || output.owner != metadata.owner || output.offset != 0U
            || output.width == 0U || output.value_kind != ValueKind::logic4
            || output.domain != SignalUpdateDomain::systemverilog_active
            || output.update_kind != RegionUpdateKind::systemverilog_active
            || !layout.contains(metadata.signal)) {
            return false;
        }
        const auto& signal_layout = layout.signal(metadata.signal);
        const auto owners = layout.owners(metadata.signal);
        if (signal_layout.storage_class
                != SignalDriverStorageClass::single_owner
            || signal_layout.value_kind != ValueKind::logic4
            || signal_layout.width != output.width
            || owners.size() != 1U
            || owners.front().process != metadata.owner
            || mutation.owner_index != signal_layout.first_owner
            || (!mutation.has_owner && !mutation.owner_is_stored_alias)
            || mutation.words.size() != signal_layout.word_count
            || !mutation.any_state_changed
            || !state->values().packed_signal_slots_bound(metadata.signal)
            || !state->values().packed_owner_slot_bound(
                metadata.signal, metadata.owner)
            || metadata.output_index
                >= bank.prepared_role_mutations.size()
            || metadata.output_index
                >= bank.prepared_role_mutation_ready.size()
            || bank.prepared_role_mutation_ready[metadata.output_index] != 0U) {
            return false;
        }
        if (index != 0U
            && (bank.applied_role_metadata[index - 1U].callback_order
                    >= metadata.callback_order
                || bank.applied_role_metadata[index - 1U].signal
                    == metadata.signal)) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (bank.applied_role_metadata[previous].signal
                == metadata.signal) {
                return false;
            }
        }
        if (metadata.signal >= direct_wide_signal_offsets.size()) {
            return false;
        }
        const auto wide_offset = direct_wide_signal_offsets[metadata.signal];
        if (wide_offset > direct_wide_signal_aval.size()
            || signal_layout.word_count
                > direct_wide_signal_aval.size() - wide_offset
            || wide_offset > direct_wide_signal_bval.size()
            || signal_layout.word_count
                > direct_wide_signal_bval.size() - wide_offset) {
            return false;
        }
        if (signal_layout.width <= 64U
            && (metadata.signal >= direct_signal_aval.size()
                || metadata.signal >= direct_signal_bval.size()
                || metadata.signal >= direct_signal_last_aval.size()
                || metadata.signal >= direct_signal_last_bval.size())) {
            return false;
        }
    }

    if (!state->values().try_rebase_and_publish_group(
            bank.applied_role_mutations,
            state->values().revision(), bank.role_flush_scratch)) {
        return false;
    }

    for (const auto& mutation : bank.applied_role_mutations) {
        const auto& signal_layout = layout.signal(mutation.signal);
        const auto wide_offset = direct_wide_signal_offsets[mutation.signal];
        bool wrote_last { };
        for (const auto& word : mutation.words) {
            const auto word_index
                = word.signal_word - signal_layout.first_value_word;
            direct_wide_signal_aval[wide_offset + word_index]
                = word.new_current[0U];
            direct_wide_signal_bval[wide_offset + word_index]
                = word.new_current[1U];
            if (signal_layout.width <= 64U) {
                direct_signal_aval[mutation.signal]
                    = word.new_current[0U];
                direct_signal_bval[mutation.signal]
                    = word.new_current[1U];
                if (mutation.any_current_changed && !wrote_last) {
                    direct_signal_last_aval[mutation.signal]
                        = word.old_current[0U];
                    direct_signal_last_bval[mutation.signal]
                        = word.old_current[1U];
                    wrote_last = true;
                }
            }
        }
        direct_signal_materialization_pending[mutation.signal] = 0U;
    }
    static_assert(std::is_nothrow_move_assignable_v<
        AuthoritativeSignalPlanes::PreparedMutation>);
    for (std::size_t index = 0U;
         index < bank.applied_role_mutations.size(); ++index) {
        const auto output_index
            = bank.applied_role_metadata[index].output_index;
        bank.prepared_role_mutations[output_index]
            = std::move(bank.applied_role_mutations[index]);
        bank.prepared_role_mutation_ready[output_index] = 0U;
    }
    local->authoritative_revision = state->values().revision();
    local->seeded = false;
    bank.active = false;
    bank.role_journal_enabled = false;
    bank.private_epoch_retired = true;
    --region_forwarding_role_journal_nonempty_components;
    bank.applied_role_mutations.clear();
    bank.applied_role_metadata.clear();
    std::ranges::fill(bank.prepared_role_mutation_ready, 0U);
    note_region_authoritative_mirror();
    return true;
}

bool Interpreter::Impl::try_flush_all_region_forwarding_role_journals()
    noexcept
{
    if (region_forwarding_role_journal_nonempty_components == 0U) {
        region_forwarding_role_flush_pending_after_discard = false;
        return true;
    }
    for (std::size_t component = 0U;
         component < region_local_wave_state_by_component.size();
         ++component) {
        if (!try_flush_region_forwarding_role_journal(component)) {
            return false;
        }
    }
    region_forwarding_role_flush_pending_after_discard = false;
    return true;
}

bool Interpreter::Impl::try_publish_region_blocking_output(
    const std::size_t component,
    const ProcessId process,
    const std::size_t output_index,
    const PackedLogic4& value,
    bool& publication_committed,
    std::exception_ptr& failure)
{
    publication_committed = false;
    failure = { };
    struct Recipient {
        ProcessId process { };
        std::size_t member { };
        std::uint64_t expected_trigger_mask { };
        std::uint64_t raw_trigger_mask { };
        std::size_t reservation_index {
            std::numeric_limits<std::size_t>::max() };
        bool queued { };
    };
    std::vector<Recipient> recipients;
    std::vector<Scheduler::SystemVerilogGroupBatchMember> scheduled_members;
    std::vector<SchedulerSystemVerilogKeyReceipt> receipts;
    Scheduler::SystemVerilogGroupBatchReservation reservation;
    std::size_t recipient_count { };
    std::size_t scheduled_count { };
    std::uint64_t reserved_systemverilog_round { };

    const auto keys_equal = [](const RegionFrontierKeyV1& left,
                                const RegionFrontierKeyV1& right) noexcept {
        return left.time == right.time && left.delta == right.delta
            && left.systemverilog_round == right.systemverilog_round
            && left.stable_order == right.stable_order
            && left.sequence == right.sequence
            && left.process_domain == right.process_domain
            && left.phase == right.phase;
    };

    try {
        if (component >= region_activation_programs.size()
            || !region_activation_programs[component]
            || component >= region_local_wave_state_by_component.size()
            || !region_local_wave_state_by_component[component]
            || component >= region_authoritative_state_by_component.size()
            || component >= region_readiness_mask_by_component.size()
            || component >= region_frontier_runtime_by_component.size()
            || !region_graph || !region_graph->component_epochs_current(component)
            || scheduler.stop_requested() || scheduler.trace_hook_installed()
            || execution_point_hook || !process_signal_access_inventory_complete) {
            return false;
        }

        const auto& component_program = *region_activation_programs[component];
        if (!component_program.forwarding_kernel) {
            return false;
        }
        const auto& activation_kernel = component_program.activation_kernel;
        const auto& forwarding = *component_program.forwarding_kernel;
        const auto& execution_kernel = forwarding.execution_kernel;
        if (output_index >= activation_kernel.outputs.size()
            || output_index >= execution_kernel.outputs.size()
            || activation_kernel.outputs[output_index]
                != execution_kernel.outputs[output_index]) {
            return false;
        }
        const auto& output = execution_kernel.outputs[output_index];
        if (output.owner != process || output.offset != 0U
            || output.width == 0U || output.width != value.width()
            || output.value_kind != ValueKind::logic4 || value.is_logic9()
            || output.domain != SignalUpdateDomain::systemverilog_active
            || output.update_kind != RegionUpdateKind::systemverilog_active
            || output.publication_kind
                != RegionOutputPublicationKind::blocking_immediate
            || !std::ranges::binary_search(
                forwarding.internal_signals, output.signal)
            || output.signal >= signals.size()
            || output.signal >= region_grouped_fanout_by_signal.size()
            || static_fanout_indices_for(
                output.signal, EdgeKind::transaction).size() != 0U) {
            return false;
        }

        auto& local = *region_local_wave_state_by_component[component];
        auto* const authoritative
            = region_authoritative_state_by_component[component].get();
        if (local.generation != region_runtime_generation || !local.seeded
            || !local.forwarding_results || authoritative == nullptr
            || !authoritative->valid()
            || authoritative->generation() != region_runtime_generation
            || !authoritative->values().packed_slots_bound()
            || local.authoritative_revision
                != authoritative->values().revision()
            || process >= processes.size()
            || !process_region_kernel_eligible(process)
            || process >= region_component_by_process.size()
            || region_component_by_process[process] != component) {
            return false;
        }
        auto& bank = *local.forwarding_results;
        const auto frontier = scheduler.current_batch_frontier();
        if (bank.runtime_generation != region_runtime_generation
            || bank.member_indices.size() != 1U
            || bank.member_indices.front() >= forwarding.members.size()
            || forwarding.members[bank.member_indices.front()].process != process
            || bank.output_indices.size() != 1U
            || bank.output_indices.front() != output_index
            || output_index >= bank.output_values.size()
            || &bank.output_values[output_index] != &value) {
            return false;
        }

        // The first blocking output is published before the shared member
        // retirement marks this bank active. Admit that startup callback only
        // when its exact scheduler prefix and freshly prepared seed prove the
        // current member; an already-active continuation needs no startup
        // proof.
        if (!bank.active) {
            const auto member_index = bank.member_indices.front();
            if (member_index >= bank.member_active.size()
                || member_index >= bank.member_consumed.size()
                || bank.member_active[member_index] == 0U
                || bank.member_consumed[member_index] != 0U
                || bank.remaining_members == 0U
                || !bank.activation_seed_bank_valid
                || bank.activation_seed_bank_index
                    >= bank.internal_seed_banks.size()
                || bank.internal_seed_banks[
                       bank.activation_seed_bank_index].size()
                    != forwarding.internal_signals.size()
                || bank.internal_initial_revisions.size()
                    != forwarding.internal_signals.size()
                || !frontier || frontier->generation == 0U
                || frontier->phase != SchedulerPhase::active
                || frontier->time != scheduler.now()
                || frontier->delta != scheduler.delta()
                || frontier->systemverilog_round
                    != scheduler.systemverilog_round()
                || bank.time != frontier->time
                || bank.starting_delta != frontier->delta
                || bank.prefix.frontier_generation != frontier->generation
                || bank.prefix.frontier_cursor != frontier->cursor
                || bank.prefix.frontier_end != frontier->end
                || bank.prefix.time != frontier->time
                || bank.prefix.delta != frontier->delta
                || bank.prefix.phase != frontier->phase
                || bank.prefix.systemverilog_round
                    != frontier->systemverilog_round
                || bank.prefix.process_domain
                    != ProcessSchedulingDomain::systemverilog) {
                return false;
            }

            bool has_current_prefix_key { };
            for (const auto& prefix_task : bank.prefix.tasks) {
                if (prefix_task.member.process != process) {
                    continue;
                }
                if (prefix_task.task_ordinal < frontier->cursor) {
                    return false;
                }
                const auto task_index
                    = prefix_task.task_ordinal - frontier->cursor;
                if (task_index >= frontier->tasks.size()) {
                    return false;
                }
                const auto& task = frontier->tasks[task_index];
                const auto& origin = prefix_task.member.origin;
                if (task.stable_order != process
                    || task.payload != (systemverilog_wave_payload | process)
                    || origin.process_domain
                        != ProcessSchedulingDomain::systemverilog
                    || origin.phase != SchedulerPhase::active
                    || origin.time != frontier->time
                    || origin.delta != frontier->delta
                    || origin.systemverilog_round
                        != frontier->systemverilog_round
                    || origin.stable_order != task.stable_order
                    || origin.sequence != task.sequence) {
                    return false;
                }
                has_current_prefix_key = true;
                break;
            }
            if (!has_current_prefix_key) {
                return false;
            }
        }

        auto& process_state = get_process(process);
        const auto& process_node = region_graph->processes()[process];
        if (process_node.scheduling_domain
                != ProcessSchedulingDomain::systemverilog
            || process_node.update_kind
                != RegionUpdateKind::systemverilog_active
            || !process_node.pure || process_node.dependencies_unknown
            || process_state.halted || process_state.suspended
            || process_state.waiting_on_signal
            || process_state.has_active_wait_timeout()
            || process_state.pc
                != process_state.program().operations().size() - 1U) {
            return false;
        }

        const auto& signal = signals[output.signal];
        if (signal.initial_value.width() != output.width
            || signal.initial_value.is_logic9()
            || signal.value_kind != ValueKind::logic4
            || (signal.resolution != ResolutionKind::none
                && signal.resolution != ResolutionKind::sv_wire)
            || signal.has_implicit_driver || signal.has_charge_strength
            || signal.event_variable || signal.public_value_reference_exposed
            || signal.systemverilog_scalar != SystemVerilogScalarKind::None
            || output.signal >= signal_transaction_observed.size()
            || signal_transaction_observed[output.signal]
            || output.signal >= dynamic_fanout.size()
            || !dynamic_fanout[output.signal].empty()
            || output.signal >= signal_container_aliases.size()
            || !signal_container_aliases[output.signal].empty()
            || output.signal >= signal_container_element_aliases.size()
            || signal_container_element_aliases[output.signal]
            || output.signal >= signal_container_aggregate_aliases.size()
            || signal_container_aggregate_aliases[output.signal]
            || output.signal >= switch_endpoint_adjacency.size()
            || !switch_endpoint_adjacency[output.signal].empty()
            || output.signal >= module_path_destination_mask.size()
            || module_path_destination_mask[output.signal] != 0U
            || has_bidirectional_switches || !module_timing_checks.empty()
            || native_signal_has_runtime_dependency(output.signal, true)
            || monitor_watches(output.signal)) {
            return false;
        }

        if (!frontier || frontier->generation == 0U
            || frontier->phase != SchedulerPhase::active
            || frontier->time != scheduler.now()
            || frontier->delta != scheduler.delta()
            || frontier->systemverilog_round
                != scheduler.systemverilog_round()) {
            return false;
        }

        const auto& cached = local.activation.internal_state(output.signal);
        if (cached.current.width() != output.width
            || cached.current.is_logic9()) {
            return false;
        }
        PackedLogic4 previous = cached.current;
        const bool changed = previous != value;

        if (changed) {
            const auto& signal_groups
                = region_grouped_fanout_by_signal[output.signal];
            const auto raw_fanout = static_fanout_for(output.signal);
            if (raw_fanout.empty()) {
                if (signal_groups.generation != 0U
                    || signal_groups.group_count != 0U) {
                    return false;
                }
            } else {
                if (signal_groups.generation != region_runtime_generation
                    || signal_groups.group_count != 1U
                    || signal_groups.group_offset
                        >= region_grouped_fanout_groups.size()) {
                    return false;
                }
                const auto& group = region_grouped_fanout_groups[
                    signal_groups.group_offset];
                if (group.generation != region_runtime_generation
                    || group.component != component
                    || group.member_count == 0U
                    || group.member_offset
                        > region_grouped_fanout_members.size()
                    || group.member_count
                        > region_grouped_fanout_members.size()
                            - group.member_offset) {
                    return false;
                }
                const auto& mask_descriptor
                    = region_readiness_mask_by_component[component];
                if (mask_descriptor.generation != region_runtime_generation
                    || mask_descriptor.offset > region_readiness_mask_words.size()
                    || mask_descriptor.word_count
                        > region_readiness_mask_words.size()
                            - mask_descriptor.offset) {
                    return false;
                }

                recipient_count = group.member_count;
                recipients.resize(recipient_count);
                scheduled_members.resize(recipient_count);
                receipts.resize(recipient_count);
                for (std::size_t index = 0U;
                     index < recipient_count; ++index) {
                    const auto& binding = region_grouped_fanout_members[
                        group.member_offset + index];
                    if (binding.process >= processes.size()
                        || binding.process >= region_component_by_process.size()
                        || binding.process
                            >= region_readiness_member_index_by_process.size()
                        || binding.process
                            >= region_readiness_queued_by_process.size()
                        || binding.static_trigger_mask == 0U
                        || region_component_by_process[binding.process]
                            != component
                        || region_readiness_member_index_by_process[
                               binding.process]
                            != binding.readiness_member
                        || binding.readiness_member / 64U
                            >= mask_descriptor.word_count) {
                        return false;
                    }
                    recipients[index] = { binding.process,
                        binding.readiness_member,
                        binding.static_trigger_mask, 0U,
                        std::numeric_limits<std::size_t>::max(), false };
                    for (std::size_t prior = 0U; prior < index; ++prior) {
                        if (recipients[prior].process == binding.process) {
                            return false;
                        }
                    }
                }
                std::ranges::sort(std::span { recipients }.first(
                    recipient_count), { }, &Recipient::process);

                for (const auto& fanout : raw_fanout) {
                    const bool whole_signal_any
                        = fanout.edge == EdgeKind::any
                        && fanout.offset == 0U
                        && (fanout.width == 0U
                            || fanout.width == output.width);
                    if (!whole_signal_any
                        || fanout.static_trigger_mask == 0U) {
                        return false;
                    }
                    const auto readers
                        = std::span { recipients }.first(recipient_count);
                    const auto recipient = std::ranges::find(
                        readers, fanout.process, &Recipient::process);
                    if (recipient == readers.end()) {
                        return false;
                    }
                    recipient->raw_trigger_mask |= fanout.static_trigger_mask;
                }

                const auto readiness_word_offset = mask_descriptor.offset;
                auto& runtime = region_frontier_runtime_by_component[component];
                for (std::size_t index = 0U;
                     index < recipient_count; ++index) {
                    auto& recipient = recipients[index];
                    if (recipient.raw_trigger_mask
                        != recipient.expected_trigger_mask) {
                        return false;
                    }
                    auto& reader = get_process(recipient.process);
                    if (!systemverilog_wave_member_eligible(recipient.process)
                        || reader.halted || reader.suspended
                        || reader.waiting_on_signal
                        || reader.has_active_wait_timeout()
                        || (!reader.queued && !reader.waiting_on_static)) {
                        return false;
                    }
                    const auto word_index = readiness_word_offset
                        + recipient.member / 64U;
                    if (word_index >= region_readiness_mask_words.size()) {
                        return false;
                    }
                    const auto member_bit = UINT64_C(1)
                        << (recipient.member % 64U);
                    auto& queued
                        = region_readiness_queued_by_process[recipient.process];
                    recipient.queued = reader.queued;
                    if (reader.queued) {
                        if (!queued.key_valid
                            || queued.generation != region_runtime_generation
                            || queued.component != component
                            || queued.member != recipient.member
                            || queued.queued_key.time != scheduler.now()
                            || queued.queued_key.delta < scheduler.delta()
                            || queued.queued_key.systemverilog_round
                                < scheduler.systemverilog_round()
                            || queued.queued_key.stable_order
                                != recipient.process
                            || queued.queued_key.process_domain
                                != static_cast<std::uint32_t>(
                                    ProcessSchedulingDomain::systemverilog)
                            || queued.queued_key.phase
                                != static_cast<std::uint32_t>(
                                    SchedulerPhase::active)
                            || (region_readiness_mask_words[word_index]
                                & member_bit) == 0U) {
                            return false;
                        }
                    } else if (queued.generation != 0U || queued.key_valid
                        || (region_readiness_mask_words[word_index]
                            & member_bit) != 0U) {
                        return false;
                    }

                    if (runtime && runtime->owner == this
                        && runtime->component == component
                        && runtime->runtime_generation
                            == region_runtime_generation
                        && runtime->frame_initialized
                        && runtime->scheduler_state_seeded
                        && !runtime->invalidated) {
                        if (recipient.member >= runtime->members.size()
                            || recipient.member / 64U
                                >= runtime->ready_words.size()
                            || runtime->members[recipient.member].process_id
                                != recipient.process) {
                            return false;
                        }
                        const auto& runtime_member
                            = runtime->members[recipient.member];
                        constexpr auto queued_flags
                            = RegionFrontierMemberFlagsV1::queued
                            | RegionFrontierMemberFlagsV1::queued_key_valid;
                        const auto present_flags
                            = runtime_member.flags & queued_flags;
                        const auto forbidden_flags
                            = RegionFrontierMemberFlagsV1::executing
                            | RegionFrontierMemberFlagsV1::pending_activation;
                        if (recipient.queued) {
                            if (present_flags != queued_flags
                                || !keys_equal(runtime_member.queued_key,
                                    queued.queued_key)) {
                                return false;
                            }
                        } else if (present_flags != 0U
                            || (runtime_member.flags & forbidden_flags) != 0U
                            || (runtime_member.flags
                                & RegionFrontierMemberFlagsV1::waiting_on_static)
                                == 0U
                            || (runtime->ready_words[
                                    recipient.member / 64U]
                                & member_bit) != 0U) {
                            return false;
                        }
                    }

                    if (!recipient.queued) {
                        auto& scheduled = scheduled_members[scheduled_count];
                        scheduled.stable_order = recipient.process;
                        scheduled.payload = systemverilog_wave_payload
                            | recipient.process;
                        scheduled.fallback_descriptor
                            = detail::make_scheduler_task_descriptor<
                                SystemVerilogWaveFallbackPayload,
                                &Interpreter::Impl::dispatch_systemverilog_wave_fallback>(
                                    { this, recipient.process });
                        recipient.reservation_index = scheduled_count;
                        ++scheduled_count;
                    }
                }

                if (scheduled_count != 0U) {
                    reservation = scheduler.reserve_systemverilog_group_batch(
                        SchedulerPhase::active, *this,
                        { region_runtime_generation,
                            static_cast<std::uint64_t>(component) + 1U },
                        scheduled_count);
                    if (!reservation) {
                        return false;
                    }
                    const auto target_round
                        = reservation.target_systemverilog_round();
                    if (!target_round) {
                        return false;
                    }
                    reserved_systemverilog_round = *target_round;
                }
            }
        }

        const auto origin = capture_signal_change_origin(
            process, SignalUpdateDomain::systemverilog_active);
        if (!publish_region_internal_value(component,
                region_runtime_generation, process, output.signal, value,
                origin, nullptr, no_systemverilog_update_slot, nullptr,
                &publication_committed,
                RegionOutputPublicationKind::blocking_immediate, true)
            || !publication_committed) {
            return false;
        }

        if (!changed) {
            return true;
        }
        authoritative->fanout().mark_transition(output.signal, previous, value,
            EdgeKind::any, authoritative->readiness());
        if (scheduled_count != 0U
            && !reservation.commit(
                std::span { scheduled_members }.first(scheduled_count),
                std::span { receipts }.first(scheduled_count))) {
            try {
                throw std::logic_error {
                    "reserved blocking fanout commit failed after publication"
                };
            } catch (...) {
                failure = std::current_exception();
            }
            return false;
        }

        for (std::size_t index = 0U;
             index < recipient_count; ++index) {
            auto& recipient = recipients[index];
            std::uint64_t trigger_mask { };
            if (!region_take_ready(output.signal,
                    recipient.process, trigger_mask)) {
                trigger_mask = recipient.expected_trigger_mask;
            }
            auto& reader = get_process(recipient.process);
            reader.static_trigger_mask |= trigger_mask;
            if (!recipient.queued) {
                if (recipient.reservation_index >= scheduled_count
                    || !receipts[recipient.reservation_index].valid
                    || receipts[recipient.reservation_index].time
                        != frontier->time
                    || receipts[recipient.reservation_index].delta
                        != frontier->delta
                    || receipts[recipient.reservation_index]
                           .systemverilog_round
                        != reserved_systemverilog_round
                    || receipts[recipient.reservation_index].phase
                        != SchedulerPhase::active
                    || receipts[recipient.reservation_index].stable_order
                        != recipient.process
                    || receipts[recipient.reservation_index]
                           .systemverilog_round == 0U) {
                    try {
                        throw std::logic_error {
                            "reserved blocking fanout returned an invalid key"
                        };
                    } catch (...) {
                        failure = std::current_exception();
                    }
                    return false;
                }
                reader.queued = true;
                record_systemverilog_readiness_key(
                    recipient.process, component, recipient.member,
                    region_runtime_generation,
                    receipts[recipient.reservation_index],
                    reader.static_trigger_mask);
            }
            merge_systemverilog_readiness_mask(
                recipient.process, trigger_mask);
        }
        return true;
    } catch (...) {
        if (publication_committed) {
            failure = std::current_exception();
        }
        return false;
    }
}

bool Interpreter::Impl::execute_region_forwarding_prefix(
    const std::size_t component,
    const RegionConeProgram& program,
    const std::span<const ExecutionContext> contexts,
    std::size_t& executed_members,
    std::exception_ptr& backend_failure)
{
    executed_members = 0U;
    backend_failure = { };
    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_region_forwarding_attempts;
    }
    struct ForwardingProfileResult {
        Impl& owner;
        const std::exception_ptr& backend_failure;
        bool succeeded { };

        ~ForwardingProfileResult()
        {
            if (!owner.systemverilog_wave_profile_enabled || succeeded) {
                return;
            }
            if (backend_failure) {
                ++owner.systemverilog_wave_profile_region_kernel_failures;
            } else {
                ++owner.systemverilog_wave_profile_region_forwarding_declines;
            }
        }
    } profile_result { *this, backend_failure };
    const auto record_role_flush_failure = [&]() noexcept {
        try {
            throw std::logic_error {
                "private forwarding role state could not be materialized"
            };
        } catch (...) {
            backend_failure = std::current_exception();
        }
    };
    if (contexts.empty() || !program.forwarding_kernel || !region_graph
        || component >= region_local_wave_state_by_component.size()
        || component >= region_cone_forwarding_backends_by_component.size()
        || component >= region_authoritative_state_by_component.size()
        || component
            >= region_graph->certificate_inventory().components.size()) {
        return false;
    }

    const auto& forwarding = *program.forwarding_kernel;
    const auto& kernel = forwarding.execution_kernel;
    auto local_owner = region_local_wave_state_by_component[component];
    auto* const authoritative
        = region_authoritative_state_by_component[component].get();
    auto backend_entry
        = region_cone_forwarding_backends_by_component[component];
    if (!local_owner || !local_owner->forwarding_results
        || !backend_entry || !backend_entry->executor
        || local_owner->generation != region_runtime_generation
        || authoritative == nullptr || !authoritative->valid()
        || authoritative->generation() != region_runtime_generation
        || !region_graph->component_epochs_current(component)) {
        if (local_owner && local_owner->forwarding_results) {
            auto& stale_bank = *local_owner->forwarding_results;
            if ((!stale_bank.applied_role_mutations.empty()
                    || !stale_bank.applied_role_metadata.empty())
                && !try_flush_region_forwarding_role_journal(component)) {
                record_role_flush_failure();
                return false;
            }
            if (stale_bank.active) {
                stale_bank.discard();
            }
        }
        return false;
    }

    auto& local = *local_owner;
    auto& bank = *local.forwarding_results;
    const bool starting = !bank.active;
    if (starting
        && (bank.role_journal_enabled || bank.private_epoch_retired
            || !bank.applied_role_mutations.empty()
            || !bank.applied_role_metadata.empty())) {
        if (!try_flush_region_forwarding_role_journal(component)) {
            record_role_flush_failure();
            return false;
        }
        bank.role_journal_enabled = false;
        bank.private_epoch_retired = false;
        std::ranges::fill(bank.prepared_role_mutation_ready, 0U);
    }
    const bool stage_group_shape = bank.stage_batch_group_shape
        && component != std::numeric_limits<std::size_t>::max();
    std::size_t prepared_completion_count { };
    const auto cancel_prepared_completions = [&]() {
        for (std::size_t index = 0U;
             index < prepared_completion_count;
             ++index) {
            const auto member_index = bank.member_indices[index];
            get_process(kernel.members[member_index].process).executor
                ->cancel_region_completion_native();
        }
        prepared_completion_count = 0U;
    };
    const auto discard_and_decline = [&]() {
        cancel_prepared_completions();
        if ((!bank.applied_role_mutations.empty()
                || !bank.applied_role_metadata.empty())
            && !try_flush_region_forwarding_role_journal(component)) {
            record_role_flush_failure();
            return false;
        }
        bank.discard();
        return false;
    };
    const auto current_frontier = scheduler.current_batch_frontier();
    bool scheduler_committed { };
    bool discard_after_prefix { };
    bool blocking_publication_committed { };
    bool forwarded_members_retired { };
    std::exception_ptr blocking_publication_failure;
    const auto retire_forwarded_members = [&]() noexcept {
        if (forwarded_members_retired) {
            return;
        }
        for (std::size_t index = 0U;
             index < bank.member_indices.size();
             ++index) {
            const auto member_index = bank.member_indices[index];
            const auto& member = kernel.members[member_index];
            auto& state = get_process(member.process);
            state.region_kernel_completion_boundary_validated = true;
            state.queued = false;
            state.waiting_on_static = false;
            state.static_trigger_mask = 0U;
            state.waiting_on_static = true;
            state.status = ProcessStatus::waiting;
            if (member.final_debug_state) {
                // Capacity is reserved before output scheduling/publication.
                // Use this same retirement on success and committed failure.
                static_assert(std::is_nothrow_copy_assignable_v<SourceLocation>);
                auto& cold = state.cold();
                state.clear_frontier_debug_token();
                cold.current_source = member.final_debug_state->source;
                cold.current_scope.assign(
                    member.final_debug_state->scope);
            }
            bank.member_consumed[member_index] = 1U;
            if (bank.remaining_members == 0U) {
                std::terminate();
            }
            --bank.remaining_members;
        }
        if (starting) {
            bank.active = true;
        }
        if (discard_after_prefix || bank.remaining_members == 0U) {
            bank.discard();
        }
        executed_members = bank.member_indices.size();
        forwarded_members_retired = true;
    };
    try {
    if (starting && stage_group_shape) {
        bank.stage_batch.reset();
        bank.stage_batch = bank.acquire_stage_batch(this, component,
            region_runtime_generation, scheduler.now(), kernel.outputs.size());
    }
    if (!systemverilog_local_wave_enabled
        || !region_local_wave_component_eligible(
            component, program.activation_kernel)
        || kernel.members.size() != forwarding.members.size()
        || kernel.member_execution_order
            != forwarding.topological_member_indices
        || forwarding.topological_member_indices.size()
            != forwarding.members.size()
        || forwarding.internal_signals.empty()
        || !std::ranges::is_sorted(forwarding.internal_signals)
        || std::ranges::adjacent_find(forwarding.internal_signals)
            != forwarding.internal_signals.end()
        || kernel.outputs.empty()
        || bank.output_values.size() != kernel.outputs.size()
        || bank.member_active.size() != forwarding.members.size()
        || bank.member_consumed.size() != forwarding.members.size()
        || bank.seed_seen.size() != forwarding.members.size()
        || bank.internal_initial_revisions.size()
            != forwarding.internal_signals.size()
        || bank.readiness_mask.size()
            != (kernel.members.size() / 64U
                + (kernel.members.size() % 64U != 0U ? 1U : 0U))
        || backend_entry->provider_identity
            != region_kernel_backend_provider_identity
        || !same_region_kernel_mapping(
            backend_entry->kernel.execution_kernel, kernel)
        || backend_entry->kernel.execution_kernel.member_execution_order
            != kernel.member_execution_order
        || backend_entry->kernel.topological_member_indices
            != forwarding.topological_member_indices
        || backend_entry->kernel.members != forwarding.members
        || backend_entry->kernel.dependencies != forwarding.dependencies
        || backend_entry->kernel.internal_reads
            != forwarding.internal_reads
        || backend_entry->kernel.internal_signals
            != forwarding.internal_signals) {
        return starting ? false : discard_and_decline();
    }

    const auto component_of = [&](const ProcessId process) {
        return process < region_component_by_process.size()
            ? region_component_by_process[process]
            : no_systemverilog_update_slot;
    };
    const auto output_index_for_writer = [&](const std::size_t writer_index,
                                             const SignalId signal)
        -> std::optional<std::size_t> {
        if (writer_index >= forwarding.members.size()) {
            return std::nullopt;
        }
        const auto& writer = forwarding.members[writer_index];
        if (writer.output_begin > kernel.outputs.size()
            || writer.output_count
                > kernel.outputs.size() - writer.output_begin) {
            return std::nullopt;
        }
        std::optional<std::size_t> result;
        for (std::size_t output_index = writer.output_begin;
             output_index < writer.output_begin + writer.output_count;
             ++output_index) {
            if (kernel.outputs[output_index].signal != signal) {
                continue;
            }
            if (result) {
                return std::nullopt;
            }
            result = output_index;
        }
        return result;
    };
    if (component_of(contexts.front().process) != component) {
        return false;
    }

    const auto validate_member = [&](const std::size_t member_index,
                                     const bool require_queued) {
        if (member_index >= forwarding.members.size()
            || member_index >= kernel.members.size()) {
            return false;
        }
        const auto& forwarding_member = forwarding.members[member_index];
        const auto& kernel_member = kernel.members[member_index];
        const auto process = forwarding_member.process;
        if (process != kernel_member.process || process >= processes.size()
            || component_of(process) != component
            || !systemverilog_wave_member_eligible(process)
            || !process_region_kernel_eligible(process)) {
            return false;
        }
        const auto& state = get_process(process);
        const auto& operations = state.program().operations();
        if (!state.executor || !state.executor->cohort_manages_process_state()
            || state.halted || state.suspended || state.waiting_on_signal
            || state.has_active_wait_timeout()
            || state.pc != operations.size() - 1U
            || (!state.waiting_on_static && !state.queued)
            || (require_queued && !state.queued)
            || !kernel_member.all_registers_definitely_defined
            || !state.region_kernel_completion_has_no_persistent_registers
            || forwarding_member.output_begin > kernel.outputs.size()
            || forwarding_member.output_count
                > kernel.outputs.size() - forwarding_member.output_begin
            || forwarding_member.dependency_begin
                > forwarding.dependencies.size()
            || forwarding_member.dependency_count
                > forwarding.dependencies.size()
                    - forwarding_member.dependency_begin
            || forwarding_member.read_begin
                > forwarding.internal_reads.size()
            || forwarding_member.read_count
                > forwarding.internal_reads.size()
                    - forwarding_member.read_begin) {
            return false;
        }
        if (operations.size() < 2U) {
            return false;
        }
        const auto wait
            = operations.expanded(operations.size() - 2U);
        const auto jump
            = operations.expanded(operations.size() - 1U);
        const auto* const loop = operation_get_if<Jump>(&jump);
        if (!operation_holds<WaitSensitivity>(wait)
            || loop == nullptr || loop->target != 0U) {
            return false;
        }
        for (std::size_t output_index = forwarding_member.output_begin;
             output_index < forwarding_member.output_begin
                    + forwarding_member.output_count;
             ++output_index) {
            const auto& output = kernel.outputs[output_index];
            if (output.owner != process || output.signal >= signals.size()
                || output.width == 0U
                || output.value_kind != ValueKind::logic4
                || output.offset != 0U
                || output.domain != SignalUpdateDomain::systemverilog_active
                || output.update_kind
                    != RegionUpdateKind::systemverilog_active
                || signals[output.signal].initial_value.width() != output.width
                || signals[output.signal].initial_value.is_logic9()) {
                return false;
            }
        }
        return true;
    };

    if (starting) {
        const auto& certificate
            = region_graph->certificate_inventory().components[component];
        if (certificate.status
                != RegionComponentCertificateStatus::structural_candidate
            || certificate.members.size() != kernel.members.size()
            || !region_graph->certificate_inventory().access_inventory_complete
            || !process_signal_access_inventory_complete
            || bank.boundary_signals.size() != bank.boundary_values.size()
            || bank.boundary_signals.size() != bank.boundary_revisions.size()) {
            return false;
        }
        if (bank.topological_position.size() != forwarding.members.size()) {
            return false;
        }
        std::ranges::fill(bank.topological_position,
            forwarding.members.size());
        for (std::size_t position = 0U;
             position < forwarding.topological_member_indices.size();
             ++position) {
            const auto member_index
                = forwarding.topological_member_indices[position];
            if (member_index >= forwarding.members.size()
                || bank.topological_position[member_index]
                    != forwarding.members.size()) {
                return false;
            }
            bank.topological_position[member_index] = position;
        }
        if (forwarding.topological_member_indices.size()
            != forwarding.members.size()) {
            return false;
        }
        for (std::size_t member_index = 0U;
             member_index < forwarding.members.size(); ++member_index) {
            const auto& member = forwarding.members[member_index];
            const bool has_dependencies = member.dependency_count != 0U;
            if (member.process != kernel.members[member_index].process
                || !validate_member(member_index, false)
                || (!has_dependencies && member.depth != 0U)
                || (has_dependencies
                    && (member.depth == 0U || member.dependency_count == 0U))) {
                return false;
            }
            const auto graph_signals = region_graph->signals();
            std::uint32_t expected_depth { };
            for (std::size_t dependency_index = member.dependency_begin;
                 dependency_index < member.dependency_begin
                    + member.dependency_count;
                 ++dependency_index) {
                const auto& dependency
                    = forwarding.dependencies[dependency_index];
                if (dependency.edge != EdgeKind::any
                    || dependency.signal >= graph_signals.size()
                    || !std::ranges::binary_search(
                        forwarding.internal_signals, dependency.signal)
                    || dependency.writer_member_index
                        >= forwarding.members.size()
                    || bank.topological_position[
                           dependency.writer_member_index]
                        >= bank.topological_position[member_index]
                    || forwarding.members[
                           dependency.writer_member_index].depth
                        == std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                expected_depth = std::max(expected_depth,
                    forwarding.members[
                        dependency.writer_member_index].depth + 1U);
                const auto signal_width
                    = graph_signals[dependency.signal].descriptor.width;
                if (signal_width == 0U
                    || graph_signals[dependency.signal]
                            .descriptor.value_kind
                        != ValueKind::logic4
                    || (dependency.width == 0U
                        ? dependency.offset != 0U
                        : (dependency.offset >= signal_width
                            || dependency.width
                                > signal_width - dependency.offset))) {
                    return false;
                }
                const auto output_index = output_index_for_writer(
                    dependency.writer_member_index, dependency.signal);
                if (!output_index) {
                    return false;
                }
                const auto& output = kernel.outputs[*output_index];
                if (output.offset != 0U || output.width != signal_width
                    || output.value_kind != ValueKind::logic4) {
                    return false;
                }
            }
            if (has_dependencies && member.depth != expected_depth) {
                return false;
            }
            for (std::size_t read_index = member.read_begin;
                 read_index < member.read_begin + member.read_count;
                 ++read_index) {
                const auto& read = forwarding.internal_reads[read_index];
                if (read.signal >= graph_signals.size()
                    || !std::ranges::binary_search(
                        forwarding.internal_signals, read.signal)
                    || read.writer_member_index >= forwarding.members.size()
                    || bank.topological_position[read.writer_member_index]
                        >= bank.topological_position[member_index]
                    || !output_index_for_writer(read.writer_member_index,
                        read.signal)
                    || std::ranges::none_of(
                        std::span<const RegionConeForwardingDependency> {
                            forwarding.dependencies }
                            .subspan(member.dependency_begin,
                                member.dependency_count),
                        [&](const RegionConeForwardingDependency& dependency) {
                            return dependency.signal == read.signal
                                && dependency.writer_member_index
                                    == read.writer_member_index;
                        })) {
                    return false;
                }
            }
            if (!has_dependencies && member.read_count != 0U) {
                return false;
            }
        }
        bank.seed_indices.clear();
        std::ranges::fill(bank.seed_seen, 0U);
        for (const auto& context : contexts) {
            if (component_of(context.process) != component) {
                break;
            }
            const auto seed = std::ranges::find(forwarding.members,
                context.process, &RegionConeForwardingMember::process);
            if (seed == forwarding.members.end()) {
                return false;
            }
            const auto seed_index = static_cast<std::size_t>(
                seed - forwarding.members.begin());
            if (bank.seed_seen[seed_index] != 0U
                || !validate_member(seed_index, true)) {
                return false;
            }
            bank.seed_seen[seed_index] = 1U;
            bank.seed_indices.push_back(seed_index);
        }
        if (bank.seed_indices.empty()) {
            return false;
        }
        std::ranges::fill(bank.seed_seen, 0U);

        for (std::size_t index = 0U; index < kernel.inputs.size(); ++index) {
            const auto& input = kernel.inputs[index];
            if (input.internal || input.width == 0U
                || input.value_kind != ValueKind::logic4
                || index >= bank.boundary_signals.size()
                || input.signal != bank.boundary_signals[index]
                || input.signal >= signal_value_revisions.size()) {
                return false;
            }
            auto value = logical_signal_value(input.signal);
            if (value.width() != input.width || value.is_logic9()) {
                return false;
            }
            bank.boundary_revisions[index]
                = signal_value_revisions[input.signal];
            bank.boundary_values[index] = std::move(value);
        }

        auto& values = authoritative->values();
        const auto& graph_signals = region_graph->signals();
        if (bank.internal_seed_banks[0U].size()
                != forwarding.internal_signals.size()
            || bank.internal_seed_banks[1U].size()
                != forwarding.internal_signals.size()) {
            return false;
        }
        if (bank.activation_seed_bank_valid
            && bank.activation_seed_bank_index
                >= bank.internal_seed_banks.size()) {
            return false;
        }
        const auto seed_bank_index = bank.activation_seed_bank_valid
            ? 1U - bank.activation_seed_bank_index : 0U;
        auto& seed_bank = bank.internal_seed_banks[seed_bank_index];

        // Validate the complete immutable seed shape before touching the
        // inactive buffer. This keeps every decline away from the activation
        // state and lets reset_internal_state perform its own all-row check
        // before sharing any of these wide values.
        for (std::size_t index = 0U;
             index < forwarding.internal_signals.size(); ++index) {
            const auto signal = forwarding.internal_signals[index];
            if (signal >= graph_signals.size()
                || signal >= signal_value_revisions.size()
                || graph_signals[signal].writers.size() != 1U
                || graph_signals[signal].descriptor.width == 0U
                || graph_signals[signal].descriptor.value_kind
                    != ValueKind::logic4) {
                return false;
            }
            const auto owner = graph_signals[signal].writers.front().process;
            const auto& seed = seed_bank[index];
            const auto binding = std::ranges::find(kernel.outputs, signal,
                &RegionConeOutputBinding::signal);
            const auto width = graph_signals[signal].descriptor.width;
            if (binding == kernel.outputs.end()
                || seed.signal != signal || seed.owner != owner
                || binding->owner != owner || binding->width != width
                || binding->value_kind != ValueKind::logic4
                || std::ranges::any_of(kernel.outputs,
                    [&](const RegionConeOutputBinding& output) {
                        return output.signal == signal
                            && (output.owner != owner
                                || output.width != width
                                || output.value_kind != ValueKind::logic4);
                    })
                || seed.current.width() != width
                || seed.previous.width() != width
                || seed.raw_driver.width() != width
                || seed.current.is_logic9() || seed.previous.is_logic9()
                || seed.raw_driver.is_logic9()
                || !local.activation.can_publish_internal_update(
                    signal, owner, width)) {
                return false;
            }
            if (width > 128U) {
                const auto current_lease = values.plane_read_lease(
                    signal, PackedPlaneRole::current);
                const auto previous_lease = values.plane_read_lease(
                    signal, PackedPlaneRole::previous);
                const auto owner_lease = values.plane_read_lease(
                    signal, PackedPlaneRole::owner, owner);
                if (!current_lease || !previous_lease || !owner_lease
                    || current_lease.width() != width
                    || previous_lease.width() != width
                    || owner_lease.width() != width
                    || current_lease.is_logic9()
                    || previous_lease.is_logic9()
                    || owner_lease.is_logic9()) {
                    return false;
                }
            }
        }

        for (std::size_t index = 0U;
             index < forwarding.internal_signals.size(); ++index) {
            const auto signal = forwarding.internal_signals[index];
            const auto owner = graph_signals[signal].writers.front().process;
            auto& seed = seed_bank[index];
            const auto width = seed.current.width();
            if (width > 128U) {
                if (!values.try_copy_wide_logic4_role_into(signal,
                        PackedPlaneRole::current, owner, seed.current)
                    || !values.try_copy_wide_logic4_role_into(signal,
                        PackedPlaneRole::previous, owner, seed.previous)
                    || !values.try_copy_wide_logic4_role_into(signal,
                        PackedPlaneRole::owner, owner, seed.raw_driver)) {
                    return false;
                }
            } else {
                seed.current = values.current(signal);
                seed.previous = values.previous(signal);
                seed.raw_driver = values.owner_value(signal, owner);
            }
            bank.internal_initial_revisions[index]
                = signal_value_revisions[signal];
        }
        local.activation.reset_internal_state(seed_bank);
        bank.activation_seed_bank_index = seed_bank_index;
        bank.activation_seed_bank_valid = true;
        local.seeded = true;
        local.authoritative_revision = values.revision();

        if (!current_frontier || current_frontier->generation == 0U
            || current_frontier->phase != SchedulerPhase::active
            || current_frontier->time != scheduler.now()
            || current_frontier->delta != scheduler.delta()
            || current_frontier->systemverilog_round
                != scheduler.systemverilog_round()) {
            return false;
        }
        if (current_frontier->tasks.size() < bank.seed_indices.size()
            || current_frontier->cursor > current_frontier->end
            || bank.seed_indices.size() > current_frontier->end
                - current_frontier->cursor) {
            return false;
        }
        bank.prefix.frontier_generation = current_frontier->generation;
        bank.prefix.frontier_cursor = current_frontier->cursor;
        bank.prefix.frontier_end = current_frontier->end;
        bank.prefix.time = current_frontier->time;
        bank.prefix.delta = current_frontier->delta;
        bank.prefix.phase = current_frontier->phase;
        bank.prefix.systemverilog_round
            = current_frontier->systemverilog_round;
        bank.prefix.process_domain = ProcessSchedulingDomain::systemverilog;
        bank.prefix.tasks.clear();
        for (std::size_t index = 0U;
             index < bank.seed_indices.size(); ++index) {
            const auto& task = current_frontier->tasks[index];
            const auto process = contexts[index].process;
            if (task.stable_order != static_cast<StableOrder>(process)
                || task.payload != (systemverilog_wave_payload | process)
                || (index != 0U
                    && !(current_frontier->tasks[index - 1U].stable_order
                            < task.stable_order
                        || (current_frontier->tasks[index - 1U].stable_order
                            == task.stable_order
                        && current_frontier->tasks[index - 1U].sequence
                            < task.sequence)))) {
                return false;
            }
            bank.prefix.tasks.push_back({ current_frontier->cursor + index,
                { process, get_process(process).static_trigger_mask,
                    { ProcessSchedulingDomain::systemverilog,
                        current_frontier->phase, current_frontier->time,
                        current_frontier->delta,
                        task.stable_order, task.sequence,
                        current_frontier->systemverilog_round } } });
        }

        if (!backend_entry->try_enter()) {
            return false;
        }
        struct BackendLeave {
            RegionConeForwardingBackendEntry& entry;
            ~BackendLeave() { entry.leave(); }
        } backend_leave { *backend_entry };
        if (!backend_entry->executor->execute_forwarding(bank.prefix,
                bank.boundary_values, bank.output_values)) {
            if (auto* const failure_backend
                = dynamic_cast<RegionConeForwardingFailureBackend*>(
                    backend_entry->executor.get())) {
                backend_failure = failure_backend->take_failure();
            }
            if (backend_failure) {
                if ((!bank.applied_role_mutations.empty()
                        || !bank.applied_role_metadata.empty())
                    && !try_flush_region_forwarding_role_journal(component)) {
                    record_role_flush_failure();
                } else {
                    bank.discard();
                }
            }
            return false;
        }
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_region_forwarding_evaluations;
        }

        std::ranges::fill(bank.member_active, 0U);
        std::ranges::fill(bank.member_consumed, 0U);
        for (std::size_t output_index = 0U;
             output_index < kernel.outputs.size(); ++output_index) {
            const auto& binding = kernel.outputs[output_index];
            const auto& value = bank.output_values[output_index];
            if (binding.width == 0U
                || binding.value_kind != ValueKind::logic4
                || value.width() != binding.width || value.is_logic9()) {
                return false;
            }
        }

        // Prepare private internal-role rows while the original update
        // callbacks are still unobservable. Callback execution may then move
        // a fully prepared row into the applied journal without allocating
        // or acquiring an A4 write lease.
        bank.role_journal_enabled = false;
        std::ranges::fill(bank.prepared_role_mutation_ready, 0U);
        if (authoritative->values().requires_prewrite_unbind()) {
            bool eligible_role_journal = kernel.members.size() > 1U
                && forwarding.topological_member_indices.size()
                    == forwarding.members.size();
            for (std::size_t position = 0U;
                 eligible_role_journal
                     && position < forwarding.topological_member_indices.size();
                 ++position) {
                const auto member_index
                    = forwarding.topological_member_indices[position];
                if (member_index >= forwarding.members.size()) {
                    eligible_role_journal = false;
                    break;
                }
                const auto& member = forwarding.members[member_index];
                if (member.dependency_begin > forwarding.dependencies.size()
                    || member.dependency_count
                        > forwarding.dependencies.size()
                            - member.dependency_begin) {
                    eligible_role_journal = false;
                    break;
                }
                // A forest may contain any number of dependency-free roots.
                // Only members represented by authentic offered callbacks are
                // seeded below; each dependency remains strictly topological,
                // and callback-time value/revision checks preserve its cut.
                for (std::size_t dependency_index = member.dependency_begin;
                     dependency_index
                        < member.dependency_begin + member.dependency_count;
                     ++dependency_index) {
                    const auto& dependency
                        = forwarding.dependencies[dependency_index];
                    if (position == 0U
                        || dependency.writer_member_index
                            >= bank.topological_position.size()
                        || bank.topological_position[
                               dependency.writer_member_index] >= position) {
                        eligible_role_journal = false;
                        break;
                    }
                }
            }
            bool has_internal_role_output { };
            const auto& role_layout = authoritative->values().layout();
            for (std::size_t output_index = 0U;
                 output_index < kernel.outputs.size(); ++output_index) {
                const auto& output = kernel.outputs[output_index];
                if (!std::ranges::binary_search(
                        forwarding.internal_signals, output.signal)) {
                    continue;
                }
                has_internal_role_output = true;
                if (output.width == 0U || output.offset != 0U
                    || output.value_kind != ValueKind::logic4
                    || output.domain
                        != SignalUpdateDomain::systemverilog_active
                    || output.update_kind
                        != RegionUpdateKind::systemverilog_active
                    || !role_layout.contains(output.signal)) {
                    eligible_role_journal = false;
                    break;
                }
                const auto& signal_layout
                    = role_layout.signal(output.signal);
                const auto owners = role_layout.owners(output.signal);
                if (signal_layout.storage_class
                        != SignalDriverStorageClass::single_owner
                    || signal_layout.value_kind != ValueKind::logic4
                    || signal_layout.width != output.width
                    || signal_layout.word_count == 0U
                    || owners.size() != 1U
                    || owners.front().process != output.owner
                    || !authoritative->values().packed_signal_slots_bound(
                        output.signal)
                    || !authoritative->values().packed_owner_slot_bound(
                        output.signal, output.owner)) {
                    eligible_role_journal = false;
                    break;
                }
                if (std::ranges::any_of(
                        std::span<const RegionConeOutputBinding> {
                            kernel.outputs }
                            .first(output_index),
                        [&](const RegionConeOutputBinding& previous) {
                            return previous.signal == output.signal
                                && std::ranges::binary_search(
                                    forwarding.internal_signals,
                                    previous.signal);
                        })) {
                    eligible_role_journal = false;
                    break;
                }
                auto& mutation
                    = bank.prepared_role_mutations[output_index];
                try {
                    authoritative->values().prepare_owner_change_into(
                        mutation, output.signal, output.owner,
                        bank.output_values[output_index],
                        bank.output_values[output_index],
                        bank.output_values[output_index]);
                } catch (...) {
                    eligible_role_journal = false;
                    break;
                }
                if (mutation.signal != output.signal
                    || mutation.words.size() != signal_layout.word_count
                    || mutation.owner_index != signal_layout.first_owner
                    || (!mutation.has_owner
                        && !mutation.owner_is_stored_alias)
                    || mutation.preflighted) {
                    eligible_role_journal = false;
                    break;
                }
                bank.prepared_role_mutation_ready[output_index] = 1U;
            }
            if (eligible_role_journal && has_internal_role_output) {
                try {
                    authoritative->values().prepare_group_scratch(
                        bank.role_flush_scratch);
                    bank.role_journal_enabled = true;
                    bank.role_callback_order = 0U;
                } catch (...) {
                    eligible_role_journal = false;
                }
            }
            if (!eligible_role_journal) {
                std::ranges::fill(bank.prepared_role_mutation_ready, 0U);
                bank.role_journal_enabled = false;
            }
        }

        for (const auto seed_index : bank.seed_indices) {
            if (seed_index >= bank.member_active.size()) {
                return false;
            }
            bank.member_active[seed_index] = 1U;
        }
        for (const auto member_index : forwarding.topological_member_indices) {
            const auto& member = forwarding.members[member_index];
            if (bank.member_active[member_index] != 0U) {
                continue;
            }
            for (std::size_t dependency_index = member.dependency_begin;
                 dependency_index < member.dependency_begin
                    + member.dependency_count;
                 ++dependency_index) {
                const auto& dependency
                    = forwarding.dependencies[dependency_index];
                if (dependency.edge != EdgeKind::any) {
                    return false;
                }
                if (dependency.writer_member_index
                        >= forwarding.members.size()
                    || bank.topological_position[
                           dependency.writer_member_index]
                        >= bank.topological_position[member_index]) {
                    return false;
                }
                if (bank.member_active[dependency.writer_member_index] == 0U) {
                    continue;
                }
                const auto internal = std::ranges::find(
                    forwarding.internal_signals, dependency.signal);
                if (internal == forwarding.internal_signals.end()) {
                    return false;
                }
                const auto internal_index = static_cast<std::size_t>(
                    internal - forwarding.internal_signals.begin());
                const auto& old = bank.internal_seed_banks[
                    bank.activation_seed_bank_index][internal_index].current;
                const auto dependency_output_index = output_index_for_writer(
                    dependency.writer_member_index, dependency.signal);
                if (!dependency_output_index) {
                    return false;
                }
                const auto& binding = kernel.outputs[
                    *dependency_output_index];
                const auto& next
                    = bank.output_values[*dependency_output_index];
                const auto signal_width = old.width();
                bool changed { };
                if (next.width() != signal_width || next.is_logic9()
                    || old.is_logic9()) {
                    return false;
                }
                if (dependency.width == 0U) {
                    if (dependency.offset != 0U) {
                        return false;
                    }
                    changed = next != old;
                } else if (dependency.offset >= signal_width
                    || dependency.width
                        > signal_width - dependency.offset) {
                    return false;
                } else {
                    changed = next != old
                        && sensitivity_range_changed(old, next,
                            dependency.offset, dependency.width);
                }
                if (binding.width != signal_width) {
                    return false;
                }
                if (changed) {
                    bank.member_active[member_index] = 1U;
                    break;
                }
            }
        }
        bank.remaining_members = static_cast<std::size_t>(std::ranges::count(
            bank.member_active, std::uint8_t { 1U }));
        bank.runtime_generation = region_runtime_generation;
        bank.time = scheduler.now();
        bank.starting_delta = scheduler.delta();
    } else {
        if (bank.runtime_generation != region_runtime_generation
            || bank.time != scheduler.now()
            || scheduler.delta() < bank.starting_delta
            || bank.prefix.frontier_generation == 0U
            || !local.seeded || !bank.activation_seed_bank_valid
            || bank.activation_seed_bank_index
                >= bank.internal_seed_banks.size()
            || local.authoritative_revision
                != authoritative->values().revision()) {
            return discard_and_decline();
        }
        for (std::size_t index = 0U;
             index < bank.boundary_signals.size(); ++index) {
            const auto signal = bank.boundary_signals[index];
            if (signal >= signal_value_revisions.size()
                || signal_value_revisions[signal] != bank.boundary_revisions[index]) {
                return discard_and_decline();
            }
            auto current = logical_signal_value(signal);
            if (current != bank.boundary_values[index]) {
                return discard_and_decline();
            }
        }
    }

    if (!current_frontier || current_frontier->generation == 0U
        || current_frontier->phase != SchedulerPhase::active
        || current_frontier->time != scheduler.now()
        || current_frontier->delta != scheduler.delta()) {
        return discard_and_decline();
    }

    bank.member_indices.clear();
    for (const auto& context : contexts) {
        if (component_of(context.process) != component) {
            break;
        }
        const auto member = std::ranges::find(forwarding.members,
            context.process, &RegionConeForwardingMember::process);
        if (member == forwarding.members.end()) {
            if (bank.member_indices.empty()) {
                return discard_and_decline();
            }
            break;
        }
        const auto member_index = static_cast<std::size_t>(
            member - forwarding.members.begin());
        if (bank.member_active[member_index] == 0U
            || bank.member_consumed[member_index] != 0U
            || !validate_member(member_index, true)) {
            if (bank.member_indices.empty()) {
                return discard_and_decline();
            }
            discard_after_prefix = true;
            break;
        }
        const auto& forwarded_member = forwarding.members[member_index];
        for (std::size_t read_index = forwarded_member.read_begin;
             read_index < forwarded_member.read_begin
                + forwarded_member.read_count;
             ++read_index) {
            const auto& read = forwarding.internal_reads[read_index];
            const auto output_index = output_index_for_writer(
                read.writer_member_index, read.signal);
            const auto internal = std::ranges::lower_bound(
                forwarding.internal_signals, read.signal);
            if (!output_index
                || internal == forwarding.internal_signals.end()
                || *internal != read.signal
                || read.signal >= signal_value_revisions.size()) {
                return discard_and_decline();
            }
            const auto internal_index = static_cast<std::size_t>(
                internal - forwarding.internal_signals.begin());
            auto expected_revision
                = bank.internal_initial_revisions[internal_index];
            const auto& initial = bank.internal_seed_banks[
                bank.activation_seed_bank_index][internal_index].current;
            const auto& predicted = bank.output_values[*output_index];
            if (initial.width() != predicted.width()
                || initial.is_logic9() || predicted.is_logic9()) {
                return discard_and_decline();
            }
            if (predicted != initial) {
                ++expected_revision;
                if (expected_revision == 0U) {
                    expected_revision = 1U;
                }
            }
            bool current_matches_prediction { };
            bool matched_applied_role { };
            if (bank.role_journal_enabled) {
                for (std::size_t row_index = 0U;
                     row_index < bank.applied_role_metadata.size();
                     ++row_index) {
                    const auto& metadata
                        = bank.applied_role_metadata[row_index];
                    if (metadata.signal != read.signal) {
                        continue;
                    }
                    matched_applied_role = true;
                    current_matches_prediction
                        = row_index < bank.applied_role_mutations.size()
                        && metadata.output_index == *output_index
                        && metadata.expected_value_revision
                            == expected_revision
                        && bank.output_values[metadata.output_index]
                            == predicted;
                    break;
                }
            }
            if (!matched_applied_role) {
                const auto current
                    = authoritative->values().current(read.signal);
                current_matches_prediction = current == predicted;
            }
            if (!current_matches_prediction
                || signal_value_revisions[read.signal] != expected_revision) {
                // A join may arrive between two predecessor publications.
                // Never wait or consume a final-cut value at this key: run
                // the checked process against the exact live intermediate
                // values, then discard this now-stale forwarding bank.
                return discard_and_decline();
            }
        }
        bank.member_indices.push_back(member_index);
    }
    if (bank.member_indices.empty()) {
        return discard_and_decline();
    }

    bank.output_indices.clear();
    bank.completion_storage_identities.clear();
    bool blocking_immediate_callback { };
    std::size_t blocking_output_index
        = std::numeric_limits<std::size_t>::max();
    std::size_t member_position { };
    while (member_position < bank.member_indices.size()) {
        const auto member_index = bank.member_indices[member_position];
        const auto& forwarding_member = forwarding.members[member_index];
        if (forwarding_member.output_begin > kernel.outputs.size()
            || forwarding_member.output_count
                > kernel.outputs.size() - forwarding_member.output_begin) {
            return discard_and_decline();
        }
        auto blocking_output = std::numeric_limits<std::size_t>::max();
        bool has_blocking_output { };
        for (std::size_t index = forwarding_member.output_begin;
             index < forwarding_member.output_begin
                    + forwarding_member.output_count;
             ++index) {
            if (kernel.outputs[index].publication_kind
                == RegionOutputPublicationKind::blocking_immediate) {
                has_blocking_output = true;
                blocking_output = index;
                break;
            }
        }
        if (has_blocking_output) {
            const bool one_private_whole_output
                = forwarding_member.output_count == 1U
                && blocking_output == forwarding_member.output_begin
                && std::ranges::binary_search(forwarding.internal_signals,
                    kernel.outputs[blocking_output].signal);
            if (member_position != 0U || !one_private_whole_output) {
                if (member_position == 0U) {
                    return discard_and_decline();
                }
                bank.member_indices.resize(member_position);
                break;
            }
            blocking_immediate_callback = true;
            blocking_output_index = blocking_output;
            bank.member_indices.resize(1U);
            break;
        }

        for (std::size_t index = forwarding_member.output_begin;
             index < forwarding_member.output_begin
                    + forwarding_member.output_count;
             ++index) {
            bank.output_indices.push_back(index);
        }
        ++member_position;
    }
    if (blocking_immediate_callback) {
        bank.output_indices.push_back(blocking_output_index);
    }
    if (bank.member_indices.empty()) {
        return discard_and_decline();
    }
    bool completion_preflight { };
    for (const auto member_index : bank.member_indices) {
        const auto& member = kernel.members[member_index];
        auto& state = get_process(member.process);
        completion_preflight = completion_preflight
            || !state.region_kernel_completion_boundary_validated;
        if (member.final_debug_state) {
            if (state.cold().current_scope.capacity()
                < member.final_debug_state->scope.size()) {
                state.cold().current_scope.reserve(
                    member.final_debug_state->scope.size());
            }
        }
    }
    std::ranges::sort(bank.output_indices,
        [&](const std::size_t left, const std::size_t right) {
            const auto& lhs = kernel.outputs[left];
            const auto& rhs = kernel.outputs[right];
            if (lhs.owner != rhs.owner) {
                return lhs.owner < rhs.owner;
            }
            if (lhs.source_instruction != rhs.source_instruction) {
                return lhs.source_instruction < rhs.source_instruction;
            }
            return left < right;
        });
    for (std::size_t index = 1U; index < bank.output_indices.size(); ++index) {
        const auto& previous = kernel.outputs[bank.output_indices[index - 1U]];
        const auto& current = kernel.outputs[bank.output_indices[index]];
        if (previous.owner == current.owner
            && previous.source_instruction == current.source_instruction) {
            return discard_and_decline();
        }
    }

    std::size_t private_parent_member_index
        = std::numeric_limits<std::size_t>::max();
    bool private_parent_shape { };
    if (bank.member_indices.size() == 1U
        && bank.member_indices.front() < forwarding.members.size()) {
        const auto parent_index = bank.member_indices.front();
        const auto& parent = forwarding.members[parent_index];
        if (parent.output_count != 0U
            && parent.output_begin <= kernel.outputs.size()
            && parent.output_count
                <= kernel.outputs.size() - parent.output_begin) {
            const auto parent_outputs
                = std::span<const RegionConeOutputBinding> { kernel.outputs }
                      .subspan(parent.output_begin, parent.output_count);
            const auto output_signal_is_internal = [&](const SignalId signal) {
                return std::ranges::binary_search(
                    forwarding.internal_signals, signal);
            };
            const bool parent_outputs_are_private
                = std::ranges::all_of(parent_outputs,
                    [&](const RegionConeOutputBinding& output) {
                        return output.owner == parent.process
                            && output_signal_is_internal(output.signal)
                            && output.offset == 0U
                            && output.value_kind == ValueKind::logic4
                            && output.width > 0U
                            && output.domain
                                == SignalUpdateDomain::systemverilog_active
                            && output.update_kind
                                == RegionUpdateKind::systemverilog_active;
                    })
                && std::ranges::all_of(parent_outputs,
                    [&](const RegionConeOutputBinding& output) {
                        return std::ranges::count_if(parent_outputs,
                                   [&](const RegionConeOutputBinding& other) {
                                       return other.signal == output.signal;
                                   }) == 1;
                    });
            std::size_t direct_child_count { };
            std::size_t dependent_child_count { };
            for (const auto& child : forwarding.members) {
                if (child.dependency_begin > forwarding.dependencies.size()
                    || child.dependency_count
                        > forwarding.dependencies.size()
                            - child.dependency_begin) {
                    continue;
                }
                const auto child_dependencies
                    = std::span<const RegionConeForwardingDependency> {
                        forwarding.dependencies }
                          .subspan(child.dependency_begin,
                              child.dependency_count);
                if (std::ranges::none_of(child_dependencies,
                        [&](const RegionConeForwardingDependency& dependency) {
                            return dependency.writer_member_index
                                == parent_index;
                        })) {
                    continue;
                }
                ++direct_child_count;
                if (child.depth <= parent.depth
                    || child.depth - parent.depth != 1U
                    || child_dependencies.empty()) {
                    continue;
                }
                if (std::ranges::all_of(child_dependencies,
                        [&](const RegionConeForwardingDependency& dependency) {
                            return dependency.writer_member_index == parent_index
                                && std::ranges::any_of(parent_outputs,
                                    [&](const RegionConeOutputBinding& output) {
                                        return output.signal
                                            == dependency.signal;
                                    })
                                && dependency.edge == EdgeKind::any;
                        })) {
                    ++dependent_child_count;
                }
            }
            private_parent_shape = parent_outputs_are_private
                && direct_child_count != 0U
                && direct_child_count == dependent_child_count;
            if (private_parent_shape) {
                private_parent_member_index = parent_index;
            }
        }
    }

    bank.completion_storage_identities.clear();
    if (completion_preflight) {
        for (const auto member_index : bank.member_indices) {
            const auto& member = kernel.members[member_index];
            const auto process = member.process;
            const auto& operations = get_process(process).program().operations();
            const auto wait_instruction
                = static_cast<InstructionIndex>(operations.size() - 2U);
            const auto jump_instruction
                = static_cast<InstructionIndex>(operations.size() - 1U);
            const void* storage_identity { };
            const bool completion_prepared
                = get_process(process).executor->prepare_region_completion_native(
                    process, wait_instruction, jump_instruction,
                    member.register_bindings,
                    kernel.program.register_count, &storage_identity);
            if (!completion_prepared) {
                cancel_prepared_completions();
                return discard_and_decline();
            }
            // Count successful preparations before any operation that can
            // fail, so every exit can cancel this executor as well.
            ++prepared_completion_count;
            if (storage_identity == nullptr
                || std::ranges::find(bank.completion_storage_identities,
                       storage_identity)
                    != bank.completion_storage_identities.end()) {
                cancel_prepared_completions();
                return discard_and_decline();
            }
            bank.completion_storage_identities.push_back(storage_identity);
        }
    }

    Scheduler::SystemVerilogGroupBatchReservation group_reservation;
    Scheduler::InternalSystemVerilogBatchReservation fallback_reservation;
    std::size_t reserved_output_slots { };
    std::size_t stage_token_count { };
    bool output_slots_reserved { };
    bool group_reserved { };
    bool compact_group { };
    bool scheduled { };
    std::shared_ptr<RegionForwardingPrivateStageBatch>
        private_stage_batch;
    std::size_t private_output_record_begin { };
    std::size_t private_output_record_count { };
    bool private_output_records_appended { };
    bool private_parent_scheduled { };
    try {
        bank.update_tokens.clear();
        bank.stable_orders.clear();
        bank.tasks.clear();
        bank.compact_members.clear();
        if (!blocking_immediate_callback) {
            bank.update_slots.resize(bank.output_indices.size());
        }
        if (blocking_immediate_callback) {
            bool publication_committed { };
            std::exception_ptr publication_failure;
            const auto& output = kernel.outputs[blocking_output_index];
            if (!try_publish_region_blocking_output(component,
                    output.owner, blocking_output_index,
                    bank.output_values[blocking_output_index],
                    publication_committed, publication_failure)
                || !publication_committed) {
                if (!publication_committed) {
                    return discard_and_decline();
                }
                blocking_publication_failure = std::move(publication_failure);
                if (!blocking_publication_failure) {
                    try {
                        throw std::logic_error {
                            "blocking output committed without a complete fanout result"
                        };
                    } catch (...) {
                        blocking_publication_failure = std::current_exception();
                    }
                }
            }
            blocking_publication_committed = true;
            scheduler_committed = true;
            scheduled = true;
        } else if (!bank.output_indices.empty()) {
            const auto stage_batch = bank.stage_batch;
            const auto* const private_parent
                = private_parent_shape
                        && private_parent_member_index
                            < forwarding.members.size()
                ? &forwarding.members[private_parent_member_index]
                : nullptr;
            const bool private_parent_candidate
                = private_parent != nullptr
                && stage_group_shape && stage_batch
                && bank.member_indices.size() == 1U
                && bank.member_indices.front()
                    == private_parent_member_index
                && bank.output_indices.size()
                    == private_parent->output_count
                && stage_batch->private_outputs.size()
                    <= stage_batch->private_outputs.capacity()
                && bank.output_indices.size()
                    <= stage_batch->private_outputs.capacity()
                        - stage_batch->private_outputs.size()
                && bank.output_indices.size()
                    <= bank.compact_members.capacity()
                && stage_batch->private_outputs.size()
                    <= RegionForwardingPrivateStageBatch::
                           payload_ordinal_mask
                && bank.output_indices.size() - 1U
                    <= RegionForwardingPrivateStageBatch::
                           payload_ordinal_mask
                        - stage_batch->private_outputs.size()
                && std::ranges::all_of(bank.output_indices,
                    [&](const std::size_t output_index) {
                        return output_index >= private_parent->output_begin
                            && output_index - private_parent->output_begin
                                < private_parent->output_count;
                    });
            if (private_parent_candidate) {
                private_stage_batch = stage_batch;
                private_output_record_begin
                    = stage_batch->private_outputs.size();
                private_output_records_appended = true;
                bank.compact_members.clear();
                for (const auto output_index : bank.output_indices) {
                    const auto& output = kernel.outputs[output_index];
                    const auto private_ordinal
                        = stage_batch->private_outputs.size();
                    stage_batch->private_outputs.push_back({ output.owner,
                        output.signal, bank.output_values[output_index],
                        { ProcessSchedulingDomain::systemverilog,
                            SchedulerPhase::active },
                        component, region_runtime_generation, true });
                    ++private_output_record_count;
                    bank.compact_members.push_back({ output.owner,
                        RegionForwardingPrivateStageBatch::
                                private_output_payload_flag
                            | static_cast<std::uint64_t>(private_ordinal) });
                }

                const SchedulerBatchGroupKey private_group_key {
                    region_runtime_generation,
                    static_cast<std::uint64_t>(component) + 1U };
                auto private_reservation
                    = scheduler.reserve_systemverilog_compact_group_batch(
                        SchedulerPhase::active, *stage_batch,
                        private_group_key, bank.compact_members.size());
                if (private_reservation) {
                    if (private_reservation.commit_compact(
                            bank.compact_members, *stage_batch, stage_batch)) {
                        private_output_records_appended = false;
                        private_parent_scheduled = true;
                        scheduler_committed = true;
                        scheduled = true;
                        if (systemverilog_wave_profile_enabled) {
                            systemverilog_wave_profile_region_forwarding_private_parent_slots_elided
                                += static_cast<std::uint64_t>(
                                    bank.compact_members.size());
                        }
                    }
                }
                if (!private_parent_scheduled) {
                    stage_batch->private_outputs.resize(
                        private_output_record_begin);
                    private_output_record_count = 0U;
                    private_output_records_appended = false;
                    bank.compact_members.clear();
                    private_stage_batch.reset();
                }
            }

            if (!private_parent_scheduled) {
                const bool compact_shape_ready = stage_group_shape
                    && stage_batch
                    && bank.output_indices.size() > 1U
                    && bank.output_indices.size() <= 64U
                    && stage_batch->tokens.size()
                        <= stage_batch->tokens.capacity()
                    && bank.output_indices.size()
                        <= stage_batch->tokens.capacity()
                            - stage_batch->tokens.size()
                    && bank.output_indices.size()
                        <= bank.compact_members.capacity();
                reserve_systemverilog_update_slots(bank.update_slots);
                output_slots_reserved = true;
                reserved_output_slots = bank.update_slots.size();
                if (compact_shape_ready) {
                    stage_token_count = stage_batch->tokens.size();
                    const SchedulerBatchGroupKey stage_group_key {
                        region_runtime_generation,
                        static_cast<std::uint64_t>(component) + 1U };
                    group_reservation
                        = scheduler.reserve_systemverilog_compact_group_batch(
                            SchedulerPhase::active, *stage_batch,
                            stage_group_key, bank.output_indices.size());
                    group_reserved = static_cast<bool>(group_reservation);
                    compact_group = group_reserved;
                }
                if (!group_reserved) {
                    fallback_reservation
                        = scheduler.reserve_internal_systemverilog_batch_from_frontier(
                            current_frontier->generation,
                            bank.output_indices.size());
                    if (!fallback_reservation) {
                        cancel_systemverilog_update_slots(bank.update_slots);
                        output_slots_reserved = false;
                        cancel_prepared_completions();
                        return discard_and_decline();
                    }
                }
                for (std::size_t index = 0U;
                     index < bank.output_indices.size(); ++index) {
                    const auto output_index = bank.output_indices[index];
                    const auto& output = kernel.outputs[output_index];
                    const auto internal_output = std::ranges::find(
                        forwarding.internal_signals, output.signal)
                        != forwarding.internal_signals.end();
                    const auto token = commit_reserved_systemverilog_update(
                        bank.update_slots[index], output.owner, output.signal,
                        bank.output_values[output_index],
                        { ProcessSchedulingDomain::systemverilog,
                            SchedulerPhase::active },
                        std::nullopt,
                        internal_output,
                        internal_output ? component : no_systemverilog_update_slot,
                        internal_output ? region_runtime_generation : 0U);
                    bank.update_tokens.push_back(token);
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_region_forwarding_public_update_tokens;
                    }
                    if (compact_group) {
                        stage_batch->tokens.push_back(token);
                        bank.compact_members.push_back({ output.owner,
                            static_cast<std::uint64_t>(stage_token_count + index) });
                    }
                }
                output_slots_reserved = false;
                const auto prepare_ordered_fallback = [&]() {
                    for (std::size_t index = 0U;
                         index < bank.output_indices.size(); ++index) {
                        const auto& output
                            = kernel.outputs[bank.output_indices[index]];
                        const bool internal_output = std::ranges::find(
                            forwarding.internal_signals, output.signal)
                            != forwarding.internal_signals.end();
                        bank.stable_orders.push_back(output.owner);
                        bank.tasks.push_back(internal_output
                            ? detail::make_scheduler_task_descriptor<
                                SystemVerilogUpdateToken,
                                &Interpreter::Impl::dispatch_region_internal_update>(
                                    bank.update_tokens[index])
                            : detail::make_scheduler_task_descriptor<
                                SystemVerilogUpdateToken,
                                &Interpreter::Impl::dispatch_systemverilog_update>(
                                    bank.update_tokens[index]));
                    }
                };
                if (compact_group
                    && group_reservation.commit_compact(bank.compact_members,
                        *stage_batch, stage_batch)) {
                    group_reserved = false;
                    scheduler_committed = true;
                    scheduled = true;
                } else {
                    if (compact_group) {
                        stage_batch->tokens.resize(stage_token_count);
                        bank.compact_members.clear();
                        group_reserved = false;
                        compact_group = false;
                        fallback_reservation
                            = scheduler.reserve_internal_systemverilog_batch_from_frontier(
                                current_frontier->generation,
                                bank.output_indices.size());
                    }
                    prepare_ordered_fallback();
                    if (!fallback_reservation
                        || !fallback_reservation.commit(
                            bank.stable_orders, bank.tasks)) {
                        for (const auto& token : bank.update_tokens) {
                            release_systemverilog_update(token);
                        }
                        bank.update_tokens.clear();
                        cancel_prepared_completions();
                        return discard_and_decline();
                    }
                    scheduler_committed = true;
                    scheduled = true;
                }
            }
        } else {
            scheduled = true;
        }
    } catch (...) {
        if (blocking_publication_committed) {
            if (!blocking_publication_failure) {
                blocking_publication_failure = std::current_exception();
            }
            cancel_prepared_completions();
            scheduler_committed = true;
            scheduled = true;
            retire_forwarded_members();
            backend_failure = std::move(blocking_publication_failure);
            return false;
        }
        group_reservation.cancel();
        if (private_output_records_appended && private_stage_batch
            && private_output_record_begin + private_output_record_count
                == private_stage_batch->private_outputs.size()) {
            private_stage_batch->private_outputs.resize(
                private_output_record_begin);
            private_output_record_count = 0U;
            private_output_records_appended = false;
            bank.compact_members.clear();
        }
        if (group_reserved && bank.stage_batch
            && bank.stage_batch->tokens.size() >= stage_token_count) {
            bank.stage_batch->tokens.resize(stage_token_count);
        }
        for (const auto& token : bank.update_tokens) {
            release_systemverilog_update(token);
        }
        if (output_slots_reserved) {
            cancel_systemverilog_update_slots(
                std::span<const std::size_t> { bank.update_slots }
                    .first(reserved_output_slots));
        }
        cancel_prepared_completions();
        return discard_and_decline();
    }
    if (!scheduled) {
        return discard_and_decline();
    }
    // From here, either original output tasks are queued or the state-only
    // completion below is the accepted callback. Any exception must fail
    // stop rather than fall through and replay the source process.
    scheduler_committed = true;

    if (completion_preflight) {
        cancel_prepared_completions();
    }
    retire_forwarded_members();
    if (blocking_publication_failure) {
        backend_failure = std::move(blocking_publication_failure);
        return false;
    }
    profile_result.succeeded = true;
    if (systemverilog_wave_profile_enabled) {
        systemverilog_wave_profile_region_forwarding_member_consumptions
            += static_cast<std::uint64_t>(executed_members);
    }
    return true;
    } catch (...) {
        if (blocking_publication_committed) {
            cancel_prepared_completions();
            retire_forwarded_members();
            backend_failure = blocking_publication_failure
                ? std::move(blocking_publication_failure)
                : std::current_exception();
            return false;
        }
        if (scheduler_committed) {
            // Queued original-output tickets are now authoritative. Falling
            // through would replay the source process and duplicate them.
            std::terminate();
        }
        if (!scheduler_committed) {
            cancel_prepared_completions();
        }
        if ((!bank.applied_role_mutations.empty()
                || !bank.applied_role_metadata.empty())
            && !try_flush_region_forwarding_role_journal(component)) {
            record_role_flush_failure();
        } else {
            bank.discard();
        }
        return false;
    }
}

SchedulerBatchResult Interpreter::Impl::execute_systemverilog_wave(
    const std::span<const std::uint64_t> payloads)
{
    SchedulerBatchResult result;
    const auto frontier = scheduler.current_batch_frontier();
    if (payloads.empty() || scheduler.stop_requested()
        || cohort_overflow_scratch_in_use
        || scheduler.current_phase() != SchedulerPhase::active
        || !frontier || frontier->generation == 0U
        || frontier->cursor != 0U || frontier->end != payloads.size()
        || frontier->tasks.size() != payloads.size()
        || frontier->phase != SchedulerPhase::active
        || frontier->time != scheduler.now()
        || frontier->delta != scheduler.delta()
        || frontier->systemverilog_round != scheduler.systemverilog_round()) {
        return result;
    }
    for (std::size_t index = 0U; index < payloads.size(); ++index) {
        const auto& current = frontier->tasks[index];
        if (current.payload != payloads[index]
            || (index != 0U
                && !(frontier->tasks[index - 1U].stable_order
                        < current.stable_order
                    || (frontier->tasks[index - 1U].stable_order
                            == current.stable_order
                        && frontier->tasks[index - 1U].sequence
                            < current.sequence)))) {
            return result;
        }
    }
    if (const auto frontier_result
        = try_execute_region_frontier_prefix(*frontier, payloads)) {
        return *frontier_result;
    }
    auto& contexts = cohort_overflow_contexts;
    auto& entries = cohort_overflow_entries;
    // Eligibility calls executor virtuals while populating these shared
    // vectors. Raise the guard before discovery so nested admission declines
    // instead of clearing the vectors underneath this callback.
    cohort_overflow_scratch_in_use = true;
    contexts.clear();
    entries.clear();
    struct ClearScratch {
        std::vector<ExecutionContext>& contexts;
        std::vector<ProcessCohortResumeEntry>& entries;
        bool& in_use;
        ~ClearScratch()
        {
            entries.clear();
            contexts.clear();
            in_use = false;
        }
    } clear { contexts, entries, cohort_overflow_scratch_in_use };
    const void* domain { };
    for (const auto payload : payloads) {
        if ((payload & systemverilog_wave_payload) == 0U) {
            break;
        }
        const auto id = static_cast<ProcessId>(payload & ~systemverilog_wave_payload);
        if (!systemverilog_wave_member_eligible(id)
            || entries.size() == 64U
            || entries.size() == entries.capacity()
            || contexts.size() == contexts.capacity()) {
            break;
        }
        auto& member = get_process(id);
        if (!member.queued || (domain && member.executor->cohort_domain() != domain)) {
            break;
        }
        domain = member.executor->cohort_domain();
        contexts.emplace_back(*this, id);
        entries.push_back({ member.executor.get(), &contexts.back(), member.pc,
            { }, { }, &member.queued, &member.waiting_on_static, &member.status });
    }
    if (entries.empty()) {
        return result;
    }

    bool kernel_executed { };
    bool native_backend_attempted { };
    std::exception_ptr native_backend_failure;
    const auto take_native_backend_failure = [](
        RegionKernelBackend& executor) {
        if (auto* const failure_backend
            = dynamic_cast<RegionKernelFailureBackend*>(&executor)) {
            return failure_backend->take_failure();
        }
        return std::exception_ptr { };
    };
    const auto execute_region_backend = [&](
        RegionKernelBackendEntry& backend_entry,
        const std::size_t component,
        const RegionConeActivationKernel& kernel,
        const RegionKernelActivationImage& image,
        std::vector<RegionKernelInputPlane>& planes,
        std::vector<RegionKernelLogic4InputPlane>& legacy_planes) {
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_region_backend_attempts;
        }
        planes.clear();
        legacy_planes.clear();
        bool authoritative_input_planes_valid = true;
        bool has_internal_kernel_input { };
        bool legacy_logic4_input_eligible = true;
        RegionAuthoritativeComponentState* component_state { };
        if (component < region_authoritative_state_by_component.size()) {
            component_state
                = region_authoritative_state_by_component[component].get();
        }
        if (component_state == nullptr
            || !component_state->valid()
            || component_state->generation() != region_runtime_generation) {
            authoritative_input_planes_valid = false;
        }
        const auto graph_signals = region_graph->signals();
        for (const auto& input : kernel.inputs) {
            if (!input.internal) {
                continue;
            }
            has_internal_kernel_input = true;
            if (!authoritative_input_planes_valid
                || (input.value_kind != ValueKind::logic4
                    && input.value_kind != ValueKind::logic9)
                || input.width == 0U
                || input.signal >= graph_signals.size()
                || input.signal
                    >= region_authoritative_component_by_signal.size()
                || region_authoritative_component_by_signal[input.signal]
                    != component) {
                authoritative_input_planes_valid = false;
                break;
            }
            legacy_logic4_input_eligible
                = legacy_logic4_input_eligible
                && input.value_kind == ValueKind::logic4
                && input.width <= 64U;
            auto* const signal_state
                = region_authoritative_state_for_signal(input.signal);
            if (signal_state != component_state) {
                authoritative_input_planes_valid = false;
                break;
            }
            const auto& graph_signal = graph_signals[input.signal];
            const auto& layout = signal_state->values().layout();
            if (!layout.contains(input.signal)) {
                authoritative_input_planes_valid = false;
                break;
            }
            const auto& signal_layout = layout.signal(input.signal);
            if (graph_signal.descriptor.width != input.width
                || graph_signal.descriptor.value_kind != input.value_kind
                || signal_layout.width != input.width
                || signal_layout.value_kind != input.value_kind
                || signal_layout.storage_class
                    == SignalDriverStorageClass::resolved_table
                || graph_signal.writers.empty()) {
                authoritative_input_planes_valid = false;
                break;
            }
            const auto owners = layout.owners(input.signal);
            if (owners.empty()) {
                authoritative_input_planes_valid = false;
                break;
            }
            for (const auto& owner : owners) {
                const auto owner_member = std::ranges::find(kernel.members,
                    owner.process, &RegionConeKernelMember::process);
                const auto mask
                    = layout.owner_mask_words(input.signal, owner.process);
                if (owner_member == kernel.members.end()
                    || mask.size() != signal_layout.word_count
                    || std::ranges::all_of(mask,
                        [](const std::uint64_t word) {
                            return word == 0U;
                        })
                    || std::ranges::none_of(graph_signal.writers,
                        [&owner](const RegionAccess& writer) {
                            return writer.process == owner.process;
                        })) {
                    authoritative_input_planes_valid = false;
                    break;
                }
            }
            if (!authoritative_input_planes_valid
                || std::ranges::any_of(graph_signal.writers,
                    [&owners](const RegionAccess& writer) {
                        const auto owner = std::ranges::lower_bound(owners,
                            writer.process, std::ranges::less { },
                            &SignalDriverOwnerLayout::process);
                        return owner == owners.end()
                            || owner->process != writer.process;
                    })) {
                authoritative_input_planes_valid = false;
                break;
            }
            std::array<std::span<const std::uint64_t>, 4U> input_value_planes;
            if (!signal_state->values().current_planes(
                    input.signal, input_value_planes)
                || input_value_planes[0U].size() != signal_layout.word_count
                || input_value_planes[1U].size() != signal_layout.word_count
                || (input.value_kind == ValueKind::logic9
                    && (input_value_planes[2U].size()
                            != signal_layout.word_count
                        || input_value_planes[3U].size()
                            != signal_layout.word_count))
                || (input.value_kind == ValueKind::logic4
                    && (!input_value_planes[2U].empty()
                        || !input_value_planes[3U].empty()))) {
                authoritative_input_planes_valid = false;
                break;
            }
            const auto image_input = std::ranges::lower_bound(
                image.register_inputs, input.value_register,
                std::ranges::less { },
                &RegionKernelRegisterInput::register_id);
            if (image_input == image.register_inputs.end()
                || image_input->register_id != input.value_register
                || image_input->value.width() != input.width
                || image_input->value.is_logic9()
                    != (input.value_kind == ValueKind::logic9)) {
                authoritative_input_planes_valid = false;
                break;
            }
            bool planes_match_image = true;
            if (input.value_kind == ValueKind::logic4) {
                planes_match_image
                    = std::ranges::equal(input_value_planes[0U],
                          image_input->value.aval_words())
                    && std::ranges::equal(input_value_planes[1U],
                          image_input->value.bval_words());
            } else {
                for (std::size_t ordinal_plane = 0U;
                     ordinal_plane < 4U; ++ordinal_plane) {
                    planes_match_image = planes_match_image
                        && std::ranges::equal(input_value_planes[ordinal_plane],
                            image_input->value.logic9_plane_words(
                                ordinal_plane));
                }
            }
            if (!planes_match_image) {
                authoritative_input_planes_valid = false;
                break;
            }
            planes.push_back({ input.signal, input.value_register,
                input.width, input.value_kind, input_value_planes });
            if (legacy_logic4_input_eligible
                && input.value_kind == ValueKind::logic4) {
                legacy_planes.push_back({ input.signal, input.value_register,
                    input.width, input_value_planes[0U],
                    input_value_planes[1U] });
            }
        }
        std::ranges::sort(planes,
            std::ranges::less { },
            &RegionKernelInputPlane::signal);
        std::ranges::sort(legacy_planes,
            std::ranges::less { },
            &RegionKernelLogic4InputPlane::signal);
        authoritative_input_planes_valid
            = authoritative_input_planes_valid
            && has_internal_kernel_input
            && planes.size()
                == static_cast<std::size_t>(std::ranges::count_if(
                    kernel.inputs,
                    [](const RegionConeKernelInput& input) {
                        return input.internal;
                    }))
            && std::ranges::adjacent_find(
                planes,
                [](const RegionKernelInputPlane& left,
                    const RegionKernelInputPlane& right) {
                    return left.signal == right.signal;
                }) == planes.end();

        authoritative_input_planes_valid
            = authoritative_input_planes_valid
            && (!legacy_logic4_input_eligible
                || legacy_planes.size() == planes.size())
            && std::ranges::adjacent_find(
                legacy_planes,
                [](const RegionKernelLogic4InputPlane& left,
                    const RegionKernelLogic4InputPlane& right) {
                    return left.signal == right.signal;
                }) == legacy_planes.end();

        auto* const direct_input_backend
            = dynamic_cast<RegionKernelInputPlaneBackend*>(
                backend_entry.executor.get());
        auto* const legacy_direct_input_backend
            = dynamic_cast<RegionKernelLogic4InputBackend*>(
                backend_entry.executor.get());
        bool native_completed { };
        if (authoritative_input_planes_valid
            && direct_input_backend != nullptr) {
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_native_input_handoffs;
            }
            native_completed
                = direct_input_backend->execute_with_input_planes(
                    image, planes);
            if (!native_completed) {
                native_backend_failure = take_native_backend_failure(
                    *backend_entry.executor);
            }
            if (systemverilog_wave_profile_enabled && native_completed) {
                ++systemverilog_wave_profile_a4_native_input_completions;
            }
        }
        if (authoritative_input_planes_valid && !native_completed
            && !native_backend_failure
            && legacy_logic4_input_eligible
            && legacy_direct_input_backend != nullptr) {
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_native_input_handoffs;
            }
            native_completed
                = legacy_direct_input_backend
                      ->execute_with_logic4_input_planes(
                          image, legacy_planes);
            if (!native_completed) {
                native_backend_failure = take_native_backend_failure(
                    *backend_entry.executor);
            }
            if (systemverilog_wave_profile_enabled && native_completed) {
                ++systemverilog_wave_profile_a4_native_input_completions;
            }
        }
        planes.clear();
        legacy_planes.clear();
        // The ordinary entry revalidates the immutable image and resets
        // its private frame before entry. It is the checked fallback when
        // component planes are unavailable or rejected.
        if (!native_completed && !native_backend_failure) {
            native_completed = backend_entry.executor->execute(image);
            if (!native_completed) {
                native_backend_failure = take_native_backend_failure(
                    *backend_entry.executor);
            }
        }
        return native_completed;
    };

    const auto try_native_region = [&](
        const std::size_t component,
        const RegionConeActivationKernel& kernel,
        const std::shared_ptr<RegionKernelBackendEntry>& backend_entry) {
        if (std::ranges::any_of(kernel.outputs,
                [](const RegionConeOutputBinding& output) {
                    return output.publication_kind
                        != RegionOutputPublicationKind::update;
                })) {
            return false;
        }
        if (!backend_entry || !backend_entry->executor
            || !backend_entry->native_workspace
            || !backend_entry->try_enter()) {
            return false;
        }

        auto& workspace = *backend_entry->native_workspace;
        RegionKernelActivationState* activation_state
            = &workspace.activation;
        std::size_t prepared_completions { };
        bool update_slots_reserved { };
        auto group_reservation
            = Scheduler::SystemVerilogGroupBatchReservation { };
        auto ordered_reservation
            = Scheduler::InternalSystemVerilogBatchReservation { };
        bool prepared_output_executed { };
        std::size_t prepared_output_publications { };
        bool prepared_output_candidate { };
        auto reservation
            = Scheduler::InternalSystemVerilogBatchReservation { };
        const auto abandon = [&]() noexcept {
            group_reservation.cancel();
            ordered_reservation.cancel();
            reservation.cancel();
            if (workspace.prepared_output_batch) {
                workspace.prepared_output_batch->group_ticket_active = false;
            }
            for (std::size_t index = 0U;
                 index < prepared_completions; ++index) {
                const auto process = workspace.prefix.tasks[index]
                    .member.process;
                if (process < processes.size()
                    && processes[process].executor) {
                    processes[process].executor
                        ->cancel_region_completion_native();
                }
            }
            if (update_slots_reserved) {
                for (const auto& token : workspace.update_tokens) {
                    release_systemverilog_update(token);
                }
                cancel_systemverilog_update_slots(
                    workspace.reserved_update_slots);
            }
            if (workspace.prepared_output_batch) {
                workspace.prepared_output_batch->cancel_unattached();
            }
            activation_state->discard_wave();
            workspace.authoritative_logic4_inputs.clear();
            workspace.authoritative_input_planes.clear();
            backend_entry->leave();
        };

        try {
            const auto& certificate
                = region_graph->certificate_inventory()
                      .components.at(component);
            bool runtime_shape_valid
                = certificate.status
                    == RegionComponentCertificateStatus::structural_candidate
                && !kernel.members.empty()
                && module_path_destination_mask.size() == signals.size();
            for (const auto& member : kernel.members) {
                if (member.process >= processes.size()) {
                    runtime_shape_valid = false;
                    break;
                }
                const auto& state = processes[member.process];
                const auto& operations = state.program().operations();
                bool canonical_static_loop { };
                if (operations.size() >= 2U) {
                    const auto wait = operations.expanded(
                        operations.size() - 2U);
                    const auto jump_operation = operations.expanded(
                        operations.size() - 1U);
                    const auto* jump = operation_get_if<Jump>(
                        &jump_operation);
                    canonical_static_loop
                        = operation_holds<WaitSensitivity>(wait)
                        && jump != nullptr && jump->target == 0U;
                }
                if (!systemverilog_wave_member_eligible(member.process)
                    || !process_region_kernel_eligible(member.process)
                    || state.halted || state.suspended
                    || state.waiting_on_signal
                    || state.has_active_wait_timeout()
                    || state.pc != operations.size() - 1U
                    || !canonical_static_loop
                    || (!state.waiting_on_static && !state.queued)
                    || !state.executor
                    || !state.executor->cohort_manages_process_state()) {
                    runtime_shape_valid = false;
                    break;
                }
            }
            for (const auto& output : kernel.outputs) {
                if (output.signal >= module_path_destination_mask.size()
                    || module_path_destination_mask[output.signal] != 0U) {
                    runtime_shape_valid = false;
                    break;
                }
            }
            std::size_t ready_count { };
            while (runtime_shape_valid && ready_count < entries.size()
                && std::ranges::find(kernel.members,
                       contexts[ready_count].process,
                       &RegionConeKernelMember::process)
                    != kernel.members.end()) {
                ++ready_count;
            }
            if (!runtime_shape_valid || ready_count == 0U) {
                abandon();
                return false;
            }
            bool stateless_completion_group = true;
            bool stateless_completion_preflight { };
            for (std::size_t index = 0U; index < ready_count; ++index) {
                const auto process = contexts[index].process;
                const auto source_member = std::ranges::find(kernel.members,
                    process, &RegionConeKernelMember::process);
                if (source_member == kernel.members.end()
                    || !source_member->all_registers_definitely_defined
                    || !get_process(process)
                            .region_kernel_completion_has_no_persistent_registers) {
                    stateless_completion_group = false;
                    if (systemverilog_wave_profile_enabled
                        && source_member != kernel.members.end()
                        && !source_member->all_registers_definitely_defined) {
                        ++systemverilog_wave_profile_a2_completion_register_declines;
                    }
                    break;
                }
                stateless_completion_preflight
                    = stateless_completion_preflight
                    || !get_process(process)
                            .region_kernel_completion_boundary_validated;
            }
            bool local_wave_publication
                = region_local_wave_component_eligible(component, kernel);
            const auto local_wave_state_owner
                = component < region_local_wave_state_by_component.size()
                ? region_local_wave_state_by_component[component]
                : std::shared_ptr<RegionLocalWaveComponentState> { };
            auto* const local_wave_state = local_wave_state_owner.get();
            local_wave_publication = local_wave_publication
                && local_wave_state != nullptr
                && local_wave_state->generation == region_runtime_generation;
            if (local_wave_publication) {
                activation_state = &local_wave_state->activation;
            }

            auto& prefix = workspace.prefix;
            prefix.frontier_generation = frontier->generation;
            prefix.frontier_cursor = frontier->cursor;
            prefix.frontier_end = frontier->end;
            prefix.time = frontier->time;
            prefix.delta = frontier->delta;
            prefix.phase = frontier->phase;
            prefix.systemverilog_round = frontier->systemverilog_round;
            prefix.tasks.clear();
            for (std::size_t index = 0U; index < ready_count; ++index) {
                const auto& task = frontier->tasks[index];
                const auto id = contexts[index].process;
                prefix.tasks.push_back({
                    frontier->cursor + index,
                    { id, get_process(id).static_trigger_mask,
                        { ProcessSchedulingDomain::systemverilog,
                            frontier->phase, frontier->time, frontier->delta,
                            task.stable_order, task.sequence,
                            frontier->systemverilog_round } },
                });
            }

            workspace.boundary_inputs.clear();
            for (const auto& input : kernel.inputs) {
                if (!input.internal) {
                    workspace.boundary_inputs.push_back(
                        logical_signal_value(input.signal));
                }
            }
            workspace.internal_seeds.clear();
            auto* const authoritative_state
                = component < region_authoritative_state_by_component.size()
                ? region_authoritative_state_by_component[component].get()
                : nullptr;
            const bool authoritative_state_valid = local_wave_publication
                && authoritative_state != nullptr
                && authoritative_state->valid();
            const bool local_state_matches = local_wave_publication
                && authoritative_state_valid
                && local_wave_state->seeded
                && local_wave_state->authoritative_revision
                    == authoritative_state->values().revision();
            if (!local_state_matches) {
                for (const auto signal : kernel.internal_signals) {
                    const auto& writers
                        = region_graph->signals()[signal].writers;
                    if (writers.size() != 1U) {
                        abandon();
                        return false;
                    }
                    const auto owner = writers.front().process;
                    if (authoritative_state_valid) {
                        workspace.internal_seeds.push_back({ signal, owner,
                            authoritative_state->values().current(signal),
                            authoritative_state->values().previous(signal),
                            authoritative_state->values().owner_value(
                                signal, owner) });
                    } else {
                        const auto* raw = driver_values[signal].find(owner);
                        const auto current = logical_signal_value(signal);
                        workspace.internal_seeds.push_back({ signal, owner,
                            current, signal_last_values[signal],
                            raw == nullptr ? current : raw->value });
                    }
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_a2_internal_seed_reads;
                    }
                }
                activation_state->reset_internal_state(
                    workspace.internal_seeds);
                if (local_wave_publication) {
                    local_wave_state->seeded = true;
                    local_wave_state->authoritative_revision
                        = authoritative_state_valid
                        ? authoritative_state->values().revision() : 0U;
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_a2_internal_state_seeds;
                    }
                }
            } else if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a2_internal_state_reuses;
            }
            const bool readiness_mask_valid
                = build_systemverilog_readiness_mask(component, contexts,
                    ready_count, workspace.readiness_mask);
            auto* const direct_ready_backend
                = dynamic_cast<RegionKernelDirectReadyWindowBackend*>(
                    backend_entry->executor.get());
            const auto direct_batch = workspace.prepared_output_batch;
            const bool direct_ready_image_candidate
                = readiness_mask_valid && local_wave_publication
                && direct_ready_backend != nullptr
                && direct_ready_backend->supports_direct_ready_window()
                && direct_batch != nullptr
                && direct_batch->direct_ready_mapping_valid
                && !direct_batch->active
                && direct_batch->pending_tickets == 0U
                && direct_batch->pending_dispatch_members == 0U;
            const auto readiness_words = readiness_mask_valid
                ? std::span<const std::uint64_t> {
                      workspace.readiness_mask }
                : std::span<const std::uint64_t> { };
            const auto& image = direct_ready_image_candidate
                ? activation_state->begin_direct_wave_reusable(
                    prefix, workspace.boundary_inputs, readiness_words)
                : activation_state->begin_wave_reusable(
                    prefix, workspace.boundary_inputs, readiness_words);
            if (readiness_mask_valid && systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_readiness_mask_images;
            }
            const auto publication_bindings
                = activation_state->expected_publication_bindings();
            if (publication_bindings.size()
                != activation_state->expected_publication_count()) {
                abandon();
                return false;
            }

            workspace.completion_storage_identities.clear();
            for (std::size_t index = 0U; index < ready_count; ++index) {
                const auto process = contexts[index].process;
                const auto member = std::ranges::find(kernel.members, process,
                    &RegionConeKernelMember::process);
                if (member == kernel.members.end()) {
                    abandon();
                    return false;
                }
                if (member->final_debug_state) {
                    get_process(process).cold().current_scope.reserve(
                        member->final_debug_state->scope.size());
                }
                auto& state = get_process(process);
                const auto& operations = state.program().operations();
                const auto wait_instruction
                    = static_cast<InstructionIndex>(operations.size() - 2U);
                const auto jump_instruction
                    = static_cast<InstructionIndex>(operations.size() - 1U);
                if (stateless_completion_group
                    && !stateless_completion_preflight) {
                    continue;
                }
                const void* storage_identity { };
                if (!state.executor->prepare_region_completion_native(
                        process, wait_instruction, jump_instruction,
                        member->register_bindings,
                        kernel.program.register_count, &storage_identity)) {
                    abandon();
                    return false;
                }
                ++prepared_completions;
                if (storage_identity == nullptr
                    || std::ranges::find(
                           workspace.completion_storage_identities,
                           storage_identity)
                        != workspace.completion_storage_identities.end()) {
                    abandon();
                    return false;
                }
                workspace.completion_storage_identities.push_back(
                    storage_identity);
            }

            workspace.reserved_update_slots.resize(
                publication_bindings.size());
            workspace.update_tokens.clear();
            workspace.stable_orders.clear();
            workspace.tasks.clear();
            reserve_systemverilog_update_slots(
                workspace.reserved_update_slots);
            update_slots_reserved = true;
            for (std::size_t index = 0U;
                 index < publication_bindings.size(); ++index) {
                const auto slot_index
                    = workspace.reserved_update_slots[index];
                if (slot_index >= systemverilog_update_slots.size()
                    || !systemverilog_update_slots[slot_index].reserved) {
                    abandon();
                    return false;
                }
                const SystemVerilogUpdateToken token {
                    this, slot_index,
                    systemverilog_update_slots[slot_index].generation };
                workspace.update_tokens.push_back(token);
                workspace.stable_orders.push_back(
                    publication_bindings[index]->owner);
                const bool internal_output = std::ranges::find(
                    kernel.internal_signals,
                    publication_bindings[index]->signal)
                    != kernel.internal_signals.end();
                if (internal_output && local_wave_publication) {
                    workspace.tasks.push_back(
                        detail::make_scheduler_task_descriptor<
                            SystemVerilogUpdateToken,
                            &Interpreter::Impl::dispatch_region_internal_update>(
                                token));
                } else {
                    workspace.tasks.push_back(
                        detail::make_scheduler_task_descriptor<
                            SystemVerilogUpdateToken,
                            &Interpreter::Impl::dispatch_systemverilog_update>(
                                token));
                }
            }
            auto* const prepared_backend
                = dynamic_cast<RegionKernelPreparedOutputBackend*>(
                    backend_entry->executor.get());
            auto* const successor_backend
                = dynamic_cast<RegionKernelPreparedOutputSuccessorMaskBackend*>(
                    backend_entry->executor.get());
            if (local_wave_publication && prepared_backend != nullptr
                && workspace.prepared_output_batch
                && prepare_region_prepared_output_batch(component, kernel,
                    *activation_state, publication_bindings,
                    *workspace.prepared_output_batch,
                    prepared_output_publications)) {
                prepared_output_candidate = true;
            }
            if (prepared_output_candidate
                && publication_bindings.size() <= 64U
                && workspace.prepared_output_batch->dispatch_group_key) {
                auto& batch = *workspace.prepared_output_batch;
                for (std::size_t index = 0U;
                     index < publication_bindings.size(); ++index) {
                    const auto& binding = *publication_bindings[index];
                    batch.group_batch_members.push_back({
                        binding.owner,
                        static_cast<std::uint64_t>(index),
                        { },
                        workspace.tasks[index],
                        { } });
                }
                std::sort(batch.group_batch_members.begin(),
                    batch.group_batch_members.end(),
                    [](const Scheduler::SystemVerilogGroupBatchMember& left,
                        const Scheduler::SystemVerilogGroupBatchMember& right) {
                        return left.stable_order < right.stable_order
                            || (left.stable_order == right.stable_order
                                && left.payload < right.payload);
                    });
                group_reservation = scheduler.reserve_systemverilog_group_batch(
                    SchedulerPhase::active, batch,
                    batch.dispatch_group_key,
                    batch.group_batch_members.size());
                if (group_reservation) {
                    batch.group_ticket_active = true;
                } else {
                    batch.group_batch_members.clear();
                }
            }
            if (!group_reservation && local_wave_publication
                && !publication_bindings.empty()
                && publication_bindings.size() <= 64U) {
                try {
                    auto* const ticket_storage
                        = local_wave_state
                            ->acquire_private_update_ticket_storage();
                    if (ticket_storage != nullptr) {
                        auto owner_lifetime
                            = std::static_pointer_cast<void>(
                                local_wave_state_owner);
                        ordered_reservation = scheduler
                            .reserve_internal_systemverilog_ordered_ticket_from_frontier(
                                frontier->generation,
                                workspace.stable_orders, workspace.tasks,
                                *ticket_storage, std::move(owner_lifetime));
                    }
                } catch (const std::bad_alloc&) {
                    ordered_reservation.cancel();
                }
            }
            if (!group_reservation && !ordered_reservation) {
                reservation = scheduler
                    .reserve_internal_systemverilog_batch_from_frontier(
                        frontier->generation, publication_bindings.size());
            }
            if (!group_reservation && !ordered_reservation && !reservation) {
                abandon();
                return false;
            }

            native_backend_attempted = true;
            bool native_completed { };
            auto* const prepared_batch
                = workspace.prepared_output_batch.get();
            const bool direct_ready_window_candidate
                = prepared_output_candidate
                && prepared_batch != nullptr
                && direct_ready_image_candidate
                && direct_ready_backend != nullptr
                && prepare_region_direct_ready_window(image,
                    workspace.boundary_inputs, workspace.readiness_mask,
                    *prepared_batch);
            const bool successor_mask_candidate = prepared_output_candidate
                && prepared_batch != nullptr
                && successor_backend != nullptr
                && prepared_batch->successor_mapping_valid
                && prepared_batch->expected_successor_masks.size()
                    == kernel.internal_signals.size();
            const bool direct_ready_successor_mask_candidate
                = direct_ready_window_candidate
                && successor_mask_candidate
                && direct_ready_backend
                    ->supports_direct_ready_window_successor_masks();
            const bool successor_mask_only_candidate
                = successor_mask_candidate
                && !direct_ready_window_candidate;
            if (direct_ready_window_candidate) {
                auto& batch = *prepared_batch;
                if (systemverilog_wave_profile_enabled) {
                    ++systemverilog_wave_profile_direct_ready_window_attempts;
                }
                if (direct_ready_successor_mask_candidate) {
                    native_completed
                        = direct_ready_backend
                              ->execute_direct_ready_window_prepared_with_successor_masks(
                                  image, batch.direct_ready_window,
                                  batch.current_internal_values,
                                  batch.ordered_prefix, batch.abi_batch,
                                  batch.successor_abi);
                    if (!native_completed) {
                        native_backend_failure
                            = take_native_backend_failure(
                                *backend_entry->executor);
                        if (native_backend_failure) {
                            abandon();
                            return false;
                        }
                    }
                    if (native_completed
                        && validate_region_prepared_output_successors(
                            kernel, batch)) {
                        batch.successor_masks_verified = true;
                    } else {
                        native_completed = false;
                        batch.successor_masks_verified = false;
                    }
                } else {
                    native_completed
                        = direct_ready_backend
                              ->execute_direct_ready_window_prepared(
                                  image, batch.direct_ready_window,
                                  batch.current_internal_values,
                                  batch.ordered_prefix, batch.abi_batch);
                    if (!native_completed) {
                        native_backend_failure
                            = take_native_backend_failure(
                                *backend_entry->executor);
                        if (native_backend_failure) {
                            abandon();
                            return false;
                        }
                    }
                }
                if (native_completed) {
                    prepared_output_executed = true;
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_prepared_output_batches;
                        ++systemverilog_wave_profile_direct_ready_window_completions;
                        if (direct_ready_successor_mask_candidate) {
                            ++systemverilog_wave_profile_generated_successor_mask_batches;
                        }
                    }
                } else {
                    if (group_reservation) {
                        batch.cancel_prepared_outputs();
                    } else {
                        batch.cancel_unattached();
                    }
                    prepared_output_candidate = false;
                    prepared_output_publications = 0U;
                    activation_state->materialize_wave_inputs(
                        workspace.boundary_inputs);
                }
            } else if (successor_mask_candidate) {
                if (activation_state->direct_wave_active()) {
                    activation_state->materialize_wave_inputs(
                        workspace.boundary_inputs);
                }
                native_completed
                    = successor_backend
                          ->execute_internal_output_prefix_prepared_with_successor_masks(
                              image,
                              prepared_batch->current_internal_values,
                              prepared_batch->ordered_prefix,
                              prepared_batch->abi_batch,
                              prepared_batch->successor_abi);
                if (!native_completed) {
                    native_backend_failure = take_native_backend_failure(
                        *backend_entry->executor);
                    if (native_backend_failure) {
                        abandon();
                        return false;
                    }
                }
                if (native_completed
                    && validate_region_prepared_output_successors(
                        kernel, *prepared_batch)) {
                    prepared_batch->successor_masks_verified = true;
                    prepared_output_executed = true;
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_prepared_output_batches;
                        ++systemverilog_wave_profile_generated_successor_mask_batches;
                    }
                } else {
                    native_completed = false;
                    prepared_batch->successor_masks_verified = false;
                    if (group_reservation) {
                        prepared_batch->cancel_prepared_outputs();
                    } else {
                        prepared_batch->cancel_unattached();
                    }
                    prepared_output_candidate = false;
                    prepared_output_publications = 0U;
                }
            } else if (direct_ready_image_candidate) {
                activation_state->materialize_wave_inputs(
                    workspace.boundary_inputs);
            }
            if (!native_completed && prepared_output_candidate
                && !successor_mask_only_candidate
                && !direct_ready_window_candidate
                && prepared_batch != nullptr) {
                native_completed
                    = prepared_backend
                          ->execute_internal_output_prefix_prepared(
                              image,
                              prepared_batch->current_internal_values,
                              prepared_batch->ordered_prefix,
                              prepared_batch->abi_batch);
                if (!native_completed) {
                    native_backend_failure = take_native_backend_failure(
                        *backend_entry->executor);
                    if (native_backend_failure) {
                        abandon();
                        return false;
                    }
                }
                if (native_completed) {
                    prepared_output_executed = true;
                    if (systemverilog_wave_profile_enabled) {
                        ++systemverilog_wave_profile_prepared_output_batches;
                    }
                } else {
                    if (group_reservation) {
                        prepared_batch->cancel_prepared_outputs();
                    } else {
                        prepared_batch->cancel_unattached();
                    }
                    prepared_output_publications = 0U;
                }
            }
            if (!native_completed) {
                if (activation_state->direct_wave_active()) {
                    activation_state->materialize_wave_inputs(
                        workspace.boundary_inputs);
                }
                native_completed = execute_region_backend(
                    *backend_entry, component, kernel, image,
                    workspace.authoritative_input_planes,
                    workspace.authoritative_logic4_inputs);
            }
            if (!native_completed) {
                abandon();
                return false;
            }
            const auto registers
                = backend_entry->executor->activation_registers();
            if (registers.size() != kernel.program.register_count) {
                abandon();
                return false;
            }
            if (stateless_completion_group) {
                for (std::size_t index = 0U;
                     index < ready_count; ++index) {
                    const auto member = std::ranges::find(kernel.members,
                        contexts[index].process,
                        &RegionConeKernelMember::process);
                    if (member == kernel.members.end()) {
                        abandon();
                        return false;
                    }
                    for (const auto& binding : member->register_bindings) {
                        if (!binding.defined
                            || binding.activation_register >= registers.size()) {
                            abandon();
                            return false;
                        }
                        const auto& value
                            = registers[binding.activation_register];
                        if (value.width() != binding.width
                            || value.is_logic9()
                                != (binding.value_kind == ValueKind::logic9)) {
                            abandon();
                            return false;
                        }
                    }
                }
            }
            activation_state->stage_kernel_outputs(image, registers);
            const auto publications = activation_state->pending_publications();
            if (publications.size() != publication_bindings.size()) {
                abandon();
                return false;
            }
            if (prepared_output_executed) {
                const auto& prepared = *workspace.prepared_output_batch;
                if (!prepared.active
                    || prepared.ordered_prefix.size()
                        != prepared_output_publications
                    || prepared.ticket_slot_indices.size()
                        != publications.size()) {
                    abandon();
                    return false;
                }
                for (std::size_t index = 0U;
                     index < prepared_output_publications; ++index) {
                    const auto internal_index
                        = prepared.ticket_slot_indices[index];
                    if (internal_index >= prepared.descriptors.size()) {
                        abandon();
                        return false;
                    }
                    const auto& descriptor
                        = prepared.descriptors[internal_index];
                    const auto& output = publications[index];
                    if (output.binding.signal != descriptor.signal_id
                        || output.binding.owner != descriptor.owner_id
                        || output.value.width() != descriptor.width
                        || output.value.is_logic9()
                        || descriptor.selected != 1U
                        || descriptor.changed == nullptr
                        || descriptor.value_ready == nullptr
                        || descriptor.transaction_ready == nullptr
                        || descriptor.next_current_aval == nullptr
                        || descriptor.next_current_bval == nullptr
                        || descriptor.next_stored_aval == nullptr
                        || descriptor.next_stored_bval == nullptr
                        || descriptor.next_owner_aval == nullptr
                        || descriptor.next_owner_bval == nullptr
                        || *descriptor.value_ready != *descriptor.changed
                        || *descriptor.transaction_ready != 1U
                        || *descriptor.next_current_aval
                            != output.value.aval_words().front()
                        || *descriptor.next_current_bval
                            != output.value.bval_words().front()
                        || *descriptor.next_stored_aval
                            != output.value.aval_words().front()
                        || *descriptor.next_stored_bval
                            != output.value.bval_words().front()
                        || *descriptor.next_owner_aval
                            != output.value.aval_words().front()
                        || *descriptor.next_owner_bval
                            != output.value.bval_words().front()) {
                        abandon();
                        return false;
                    }
                }
            }
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_region_backend_runs;
            }

            if (stateless_completion_group) {
                if (stateless_completion_preflight) {
                    for (std::size_t index = 0U;
                         index < ready_count; ++index) {
                        get_process(contexts[index].process).executor
                            ->cancel_region_completion_native();
                    }
                    prepared_completions = 0U;
                }
            } else {
                for (std::size_t index = 0U; index < ready_count; ++index) {
                    const auto process = contexts[index].process;
                    if (!get_process(process).executor
                        ->stage_region_completion_native(registers)) {
                        abandon();
                        return false;
                    }
                }
            }

            for (std::size_t index = 0U;
                 index < publications.size(); ++index) {
                auto& token = workspace.update_tokens[index];
                const auto& binding = publications[index].binding;
                const auto signal_width = binding.signal_width == 0U
                    ? binding.width : binding.signal_width;
                const bool systemverilog_output
                    = binding.update_kind
                            == RegionUpdateKind::systemverilog_active
                    && binding.domain
                        == SignalUpdateDomain::systemverilog_active;
                const auto output_offset = systemverilog_output
                        && (binding.offset != 0U
                            || binding.width != signal_width)
                    ? std::optional<std::size_t> { binding.offset }
                    : std::nullopt;
                token = commit_reserved_systemverilog_update(
                    token.slot, binding.owner, binding.signal,
                    publications[index].value,
                    { publications[index].origin.process_domain,
                        publications[index].origin.phase },
                    output_offset,
                    std::ranges::find(kernel.internal_signals,
                        binding.signal)
                        != kernel.internal_signals.end(),
                    local_wave_publication
                        && std::ranges::find(kernel.internal_signals,
                            binding.signal)
                            != kernel.internal_signals.end()
                        ? component : no_systemverilog_update_slot,
                    local_wave_publication
                        && std::ranges::find(kernel.internal_signals,
                            binding.signal)
                            != kernel.internal_signals.end()
                        ? region_runtime_generation : 0U);
                if (group_reservation) {
                    auto& batch = *workspace.prepared_output_batch;
                    const auto prepared_slot = prepared_output_executed
                            && index < batch.ticket_slot_indices.size()
                        ? batch.ticket_slot_indices[index]
                        : no_systemverilog_update_slot;
                    attach_prepared_output_group_member(token,
                        workspace.prepared_output_batch, prepared_slot, index);
                } else if (prepared_output_executed
                    && index < workspace.prepared_output_batch
                                      ->ticket_slot_indices.size()) {
                    const auto prepared_slot
                        = workspace.prepared_output_batch
                              ->ticket_slot_indices[index];
                    if (prepared_slot != no_systemverilog_update_slot) {
                        attach_prepared_output_ticket(token,
                            workspace.prepared_output_batch, prepared_slot);
                    }
                }
            }
            update_slots_reserved = false;
            const bool used_group_ticket
                = static_cast<bool>(group_reservation);
            const bool used_ordered_ticket
                = static_cast<bool>(ordered_reservation);
            const bool reservation_committed = used_group_ticket
                ? group_reservation.commit(
                    workspace.prepared_output_batch->group_batch_members)
                : ordered_reservation
                ? ordered_reservation.commit_ordered_ticket()
                : reservation.commit(
                    workspace.stable_orders, workspace.tasks);
            if (!reservation_committed) {
                for (const auto& token : workspace.update_tokens) {
                    release_systemverilog_update(token);
                }
                if (workspace.prepared_output_batch) {
                    workspace.prepared_output_batch->group_ticket_active = false;
                }
                for (std::size_t index = 0U;
                     index < prepared_completions; ++index) {
                    get_process(prefix.tasks[index].member.process)
                        .executor->cancel_region_completion_native();
                }
                activation_state->discard_wave();
                backend_entry->leave();
                return false;
            }
            if (used_group_ticket) {
                workspace.prepared_output_batch->group_batch_members.clear();
            }
            if (systemverilog_wave_profile_enabled) {
                if (used_ordered_ticket) {
                    ++systemverilog_wave_profile_a3_private_update_ticket_entries;
                    systemverilog_wave_profile_a3_private_update_ticket_members
                        += publications.size();
                    systemverilog_wave_profile_a3_private_update_entries_elided
                        += publications.size() - 1U;
                } else if (local_wave_publication && !used_group_ticket) {
                    systemverilog_wave_profile_a3_private_update_fallback_members
                        += publications.size();
                }
            }

            for (std::size_t index = 0U; index < ready_count; ++index) {
                const auto process = contexts[index].process;
                auto& member = get_process(process);
                if (stateless_completion_group) {
                    member.region_kernel_completion_boundary_validated = true;
                } else {
                    member.executor->commit_region_completion_native();
                    member.region_kernel_completion_boundary_validated = false;
                }
                member.queued = false;
                member.waiting_on_static = false;
                member.static_trigger_mask = 0U;
                member.waiting_on_static = true;
                member.status = ProcessStatus::waiting;
                const auto source_member = std::ranges::find(kernel.members,
                    process, &RegionConeKernelMember::process);
                if (source_member != kernel.members.end()
                    && source_member->final_debug_state) {
                    const auto& scope
                        = source_member->final_debug_state->scope;
                    auto& cold = member.cold();
                    member.clear_frontier_debug_token();
                    auto& current_scope = cold.current_scope;
                    current_scope.resize(scope.size());
                    std::copy(scope.begin(), scope.end(),
                        current_scope.begin());
                    cold.current_source
                        = source_member->final_debug_state->source;
                }
            }
            activation_state->discard_wave();
            backend_entry->leave();
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_region_backend_completions;
                ++systemverilog_wave_profile_region_kernel_runs;
                systemverilog_wave_profile_region_kernel_members
                    += ready_count;
                systemverilog_wave_profile_region_kernel_publications
                    += publications.size();
                if (stateless_completion_group) {
                    ++systemverilog_wave_profile_a2_completion_fast_batches;
                    systemverilog_wave_profile_a2_completion_fast_members
                        += ready_count;
                    if (stateless_completion_preflight) {
                        systemverilog_wave_profile_a2_completion_preflights
                            += ready_count;
                    }
                }
            }
            result.executed = ready_count;
            kernel_executed = true;
            return true;
        } catch (...) {
            abandon();
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_region_kernel_failures;
            }
            return false;
        }
    };

    if (systemverilog_region_kernel_enabled && region_graph
        && !contexts.empty() && !region_component_by_process.empty()) {
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_region_kernel_attempts;
        }
        if (scheduler.trace_hook_installed()) {
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_region_trace_declines;
            }
        } else {
            const auto first_process = contexts.front().process;
            const auto component = first_process
                    < region_component_by_process.size()
                ? region_component_by_process[first_process]
                : std::numeric_limits<std::size_t>::max();
            if (component < region_activation_programs.size()
                && region_activation_programs[component]
                && region_graph->component_epochs_current(component)) {
                std::size_t forwarded_members { };
                std::exception_ptr forwarding_failure;
                if (execute_region_forwarding_prefix(component,
                        *region_activation_programs[component], contexts,
                        forwarded_members, forwarding_failure)) {
                    kernel_executed = true;
                    result.executed = forwarded_members;
                } else if (forwarding_failure) {
                    result.executed = forwarded_members;
                    result.failure = std::move(forwarding_failure);
                    clear_systemverilog_readiness_prefix(
                        *frontier, result.executed);
                    return result;
                } else if (component
                        < region_kernel_backends_by_component.size()
                    && component
                        < region_kernel_backend_generation_by_component.size()
                    && region_kernel_backend_generation_by_component[component]
                        == region_runtime_generation) {
                    if (!try_flush_region_forwarding_role_journal(
                            component)) {
                        throw std::logic_error {
                            "declined flattened prefix could not materialize "
                            "private role state before native activation"
                        };
                    }
                    (void)try_native_region(component,
                        region_activation_programs[component]
                            ->activation_kernel,
                        region_kernel_backends_by_component[component]);
                    if (native_backend_failure) {
                        if (systemverilog_wave_profile_enabled) {
                            ++systemverilog_wave_profile_region_kernel_failures;
                        }
                        result.failure = std::move(native_backend_failure);
                        return result;
                    }
                }
            }
        }
    }

    // A declined native prefix may still have an applied private role prefix.
    // Keep it hidden while an authenticated flattened continuation can consume
    // another original callback. Materialize only after that route and native
    // completion have both declined, before any checked executor reads values.
    if (!kernel_executed) {
        for (const auto& context : contexts) {
            if (context.process >= region_component_by_process.size()) {
                continue;
            }
            const auto component
                = region_component_by_process[context.process];
            if (component == no_systemverilog_update_slot) {
                continue;
            }
            if (!try_flush_region_forwarding_role_journal(component)) {
                throw std::logic_error {
                    "checked SystemVerilog fallback could not materialize private role state"
                };
            }
        }
    }

    // A declined singleton keeps its original scheduler task. Only an
    // accepted native activation may bypass that ordinary callback.
    if (entries.size() == 1U && !kernel_executed) {
        return result;
    }

    std::vector<RegionKernelPendingPublication> kernel_publications;
    std::vector<PackedLogic4> kernel_registers;
    std::span<const PackedLogic4> kernel_register_view;
    std::shared_ptr<RegionKernelBackendEntry> backend_in_use;
    bool backend_execution_succeeded { };
    std::size_t kernel_ready_count { };
    bool kernel_prepared { };
    const RegionConeActivationKernel* prepared_kernel { };
    const auto first_component = !contexts.empty()
            && contexts.front().process < region_component_by_process.size()
        ? region_component_by_process[contexts.front().process]
        : no_systemverilog_update_slot;
    const bool current_v1_backend
        = first_component < region_kernel_backends_by_component.size()
        && first_component
            < region_kernel_backend_generation_by_component.size()
        && region_kernel_backend_generation_by_component[first_component]
            == region_runtime_generation
        && region_kernel_backends_by_component[first_component];
    const bool use_ordered_frontier_fallback
        = region_kernel_backend_provider_supports_frontier
        && !current_v1_backend;
    if (!kernel_executed && !use_ordered_frontier_fallback
        && systemverilog_region_kernel_enabled && region_graph
        && !contexts.empty()
        && !region_component_by_process.empty()
        && !scheduler.trace_hook_installed()) {
        try {
            const auto first_process = contexts.front().process;
            const auto component = first_process
                    < region_component_by_process.size()
                ? region_component_by_process[first_process]
                : std::numeric_limits<std::size_t>::max();
            if (component < region_activation_programs.size()
                && region_activation_programs[component]
                && region_graph->component_epochs_current(component)) {
                const auto& certificate
                    = region_graph->certificate_inventory()
                          .components.at(component);
                const auto& kernel
                    = region_activation_programs[component]
                          ->activation_kernel;
                bool runtime_shape_valid
                    = certificate.status
                        == RegionComponentCertificateStatus::structural_candidate
                    && !kernel.members.empty();
                runtime_shape_valid = runtime_shape_valid
                    && module_path_destination_mask.size() == signals.size();
                for (const auto& member : kernel.members) {
                    if (member.process >= processes.size()) {
                        runtime_shape_valid = false;
                        break;
                    }
                    const auto& state = processes[member.process];
                    const auto& member_operations
                        = state.program().operations();
                    bool canonical_static_loop { };
                    if (member_operations.size() >= 2U) {
                        const auto wait
                            = member_operations.expanded(
                                member_operations.size() - 2U);
                        const auto jump_operation
                            = member_operations.expanded(
                                member_operations.size() - 1U);
                        const auto* jump
                            = operation_get_if<Jump>(&jump_operation);
                        canonical_static_loop
                            = operation_holds<WaitSensitivity>(wait)
                            && jump != nullptr && jump->target == 0U;
                    }
                    if (!systemverilog_wave_member_eligible(member.process)
                        || !process_region_kernel_eligible(member.process)
                        || state.halted || state.suspended
                        || state.waiting_on_signal
                        || state.has_active_wait_timeout()
                        || state.pc != member_operations.size() - 1U
                        || !canonical_static_loop
                        || (!state.waiting_on_static && !state.queued)
                        || !state.executor
                        || !state.executor->cohort_manages_process_state()) {
                        runtime_shape_valid = false;
                        break;
                    }
                }
                for (const auto& output : kernel.outputs) {
                    if (output.publication_kind
                            != RegionOutputPublicationKind::update
                        || output.signal
                            >= module_path_destination_mask.size()
                        || module_path_destination_mask[output.signal] != 0U) {
                        runtime_shape_valid = false;
                        break;
                    }
                }
                while (runtime_shape_valid
                    && kernel_ready_count < entries.size()
                    && std::ranges::find(kernel.members,
                           contexts[kernel_ready_count].process,
                           &RegionConeKernelMember::process)
                        != kernel.members.end()) {
                    ++kernel_ready_count;
                }
                if (runtime_shape_valid && kernel_ready_count != 0U) {
                    RegionKernelSchedulerPrefix prefix;
                    prefix.frontier_generation = frontier->generation;
                    prefix.frontier_cursor = frontier->cursor;
                    prefix.frontier_end = frontier->end;
                    prefix.time = frontier->time;
                    prefix.delta = frontier->delta;
                    prefix.phase = frontier->phase;
                    prefix.systemverilog_round
                        = frontier->systemverilog_round;
                    prefix.tasks.reserve(kernel_ready_count);
                    for (std::size_t index = 0U;
                         index < kernel_ready_count; ++index) {
                        const auto& task = frontier->tasks[index];
                        const auto id = contexts[index].process;
                        prefix.tasks.push_back({
                            frontier->cursor + index,
                            { id,
                                get_process(id).static_trigger_mask,
                                { ProcessSchedulingDomain::systemverilog,
                                    frontier->phase,
                                    frontier->time,
                                    frontier->delta,
                                    task.stable_order,
                                    task.sequence,
                                    frontier->systemverilog_round } },
                        });
                    }

                    std::vector<PackedLogic4> current_boundary_inputs;
                    current_boundary_inputs.reserve(kernel.inputs.size());
                    for (const auto& input : kernel.inputs) {
                        if (!input.internal) {
                            current_boundary_inputs.push_back(
                                logical_signal_value(input.signal));
                        }
                    }
                    std::vector<RegionKernelInternalSeed> internal_seed;
                    internal_seed.reserve(kernel.internal_signals.size());
                    for (const auto signal : kernel.internal_signals) {
                        const auto& writers
                            = region_graph->signals()[signal].writers;
                        if (writers.size() != 1U) {
                            throw std::logic_error {
                                "region kernel internal signal lost its sole owner"
                            };
                        }
                        const auto owner = writers.front().process;
                        const auto* raw = driver_values[signal].find(owner);
                        const auto current = logical_signal_value(signal);
                        internal_seed.push_back({ signal, owner,
                            current, signal_last_values[signal],
                            raw == nullptr ? current : raw->value });
                    }

                    RegionKernelActivationState activation(kernel,
                        internal_seed);
                    const auto image = activation.begin_wave(
                        prefix, current_boundary_inputs);
                    if (component
                            < region_kernel_backends_by_component.size()
                        && component
                            < region_kernel_backend_generation_by_component.size()
                        && region_kernel_backend_generation_by_component[
                               component]
                            == region_runtime_generation) {
                        const auto& backend_entry
                            = region_kernel_backends_by_component[component];
                        if (!native_backend_attempted && backend_entry
                            && backend_entry->executor
                            && backend_entry->try_enter()) {
                            backend_in_use = backend_entry;
                            bool completed { };
                            if (backend_entry->native_workspace) {
                                completed = execute_region_backend(
                                    *backend_entry, component, kernel, image,
                                    backend_entry->native_workspace
                                        ->authoritative_input_planes,
                                    backend_entry->native_workspace
                                        ->authoritative_logic4_inputs);
                            } else {
                                if (systemverilog_wave_profile_enabled) {
                                    ++systemverilog_wave_profile_region_backend_attempts;
                                }
                                completed = backend_entry->executor->execute(
                                    image);
                                if (!completed) {
                                    native_backend_failure
                                        = take_native_backend_failure(
                                            *backend_entry->executor);
                                }
                            }
                            const auto native_registers = completed
                                ? backend_entry->executor->activation_registers()
                                : std::span<const PackedLogic4> { };
                            if (completed
                                && native_registers.size()
                                    == kernel.program.register_count) {
                                backend_execution_succeeded = true;
                                kernel_register_view = native_registers;
                                if (systemverilog_wave_profile_enabled) {
                                    ++systemverilog_wave_profile_region_backend_runs;
                                }
                            } else {
                                backend_entry->leave();
                                backend_in_use.reset();
                            }
                        }
                    }
                    if (!backend_execution_succeeded) {
                        if (native_backend_failure) {
                            if (systemverilog_wave_profile_enabled) {
                                ++systemverilog_wave_profile_region_kernel_failures;
                            }
                            result.failure = std::move(
                                native_backend_failure);
                            return result;
                        }
                        kernel_registers
                            = evaluate_region_activation_kernel(kernel, image);
                        kernel_register_view = kernel_registers;
                    }
                    activation.stage_kernel_outputs(
                        image, kernel_register_view);
                    const auto publications
                        = activation.pending_publications();
                    kernel_publications.assign(
                        publications.begin(), publications.end());
                    prepared_kernel = &kernel;
                    kernel_prepared = true;
                }
            }
        } catch (...) {
            if (backend_in_use) {
                backend_in_use->leave();
                backend_in_use.reset();
            }
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_region_kernel_failures;
            }
        }
    }
    if (kernel_prepared) {
        std::vector<SystemVerilogUpdateToken> tokens;
        std::vector<StableOrder> stable_orders;
        std::vector<detail::SchedulerTaskDescriptor> tasks;
        std::vector<std::optional<RegionConeFinalDebugState>> final_debug_states;
        std::vector<std::unique_ptr<ProcessExecutor::PreparedRegionCompletion>>
            completions;
        std::vector<const void*> completion_storage_identities;
        bool scheduled { };
        try {
            tokens.reserve(kernel_publications.size());
            stable_orders.reserve(kernel_publications.size());
            tasks.reserve(kernel_publications.size());
            final_debug_states.reserve(kernel_ready_count);
            completions.reserve(kernel_ready_count);
            completion_storage_identities.reserve(kernel_ready_count);
            if (prepared_kernel == nullptr) {
                throw std::logic_error {
                    "prepared region kernel lost its activation program"
                };
            }
            for (std::size_t index = 0U;
                 index < kernel_ready_count; ++index) {
                const auto process = contexts[index].process;
                const auto member = std::ranges::find(prepared_kernel->members,
                    process, &RegionConeKernelMember::process);
                if (member == prepared_kernel->members.end()) {
                    throw std::logic_error {
                        "region kernel lost a scheduler-prefix member"
                    };
                }
                final_debug_states.push_back(member->final_debug_state);
                auto& state = get_process(process);
                const auto& member_operations = state.program().operations();
                if (!state.executor || member_operations.size() < 2U) {
                    throw std::logic_error {
                        "region kernel member lost its source executor boundary"
                    };
                }
                const auto wait_instruction
                    = static_cast<InstructionIndex>(
                        member_operations.size() - 2U);
                const auto jump_instruction
                    = static_cast<InstructionIndex>(
                        member_operations.size() - 1U);
                auto completion = state.executor->prepare_region_completion(
                    process, wait_instruction, jump_instruction,
                    member->register_bindings, kernel_register_view);
                if (!completion) {
                    throw std::logic_error {
                        "region kernel member declined register completion"
                    };
                }
                const auto* const storage_identity
                    = completion->storage_identity();
                if (storage_identity == nullptr
                    || std::ranges::find(completion_storage_identities,
                           storage_identity)
                        != completion_storage_identities.end()) {
                    throw std::logic_error {
                        "region kernel members share completion storage"
                    };
                }
                completion_storage_identities.push_back(storage_identity);
                completions.push_back(std::move(completion));
            }
            for (auto& publication : kernel_publications) {
                const auto process = publication.binding.owner;
                const auto update_domain = publication.binding.domain;
                if (process >= processes.size()
                    || update_domain
                        != SignalUpdateDomain::systemverilog_active) {
                    throw std::logic_error {
                        "region kernel publication lost its Active owner"
                    };
                }
                const auto captured = SignalChangeOrigin {
                    publication.origin.process_domain,
                    publication.origin.phase };
                const auto live
                    = capture_signal_change_origin(process, update_domain);
                if (captured.process_domain != live.process_domain
                    || captured.phase != live.phase) {
                    throw std::logic_error {
                        "region kernel publication origin is stale"
                    };
                }
                stable_orders.push_back(process);
            }
            for (auto& publication : kernel_publications) {
                const auto process = publication.binding.owner;
                const auto signal_width
                    = publication.binding.signal_width == 0U
                    ? publication.binding.width
                    : publication.binding.signal_width;
                std::optional<std::size_t> output_offset;
                if (publication.binding.offset != 0U
                    || publication.binding.width != signal_width) {
                    output_offset = publication.binding.offset;
                }
                const auto token = reserve_systemverilog_update(
                    process, publication.binding.signal,
                    std::move(publication.value), output_offset,
                    { publication.origin.process_domain,
                        publication.origin.phase });
                tokens.push_back(token);
                tasks.push_back(
                    detail::make_scheduler_task_descriptor<
                        SystemVerilogUpdateToken,
                        &Interpreter::Impl::dispatch_systemverilog_update>(
                        token));
            }
            scheduler.schedule_internal_systemverilog_batch_from_frontier(
                frontier->generation, stable_orders, tasks);
            scheduled = true;
        } catch (...) {
            for (const auto& token : tokens) {
                release_systemverilog_update(token);
            }
            if (backend_in_use) {
                backend_in_use->leave();
                backend_in_use.reset();
            }
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_region_kernel_failures;
            }
        }

        if (scheduled) {
            for (auto& completion : completions) {
                completion->commit();
            }
            if (backend_in_use) {
                backend_in_use->leave();
                backend_in_use.reset();
                if (systemverilog_wave_profile_enabled) {
                    ++systemverilog_wave_profile_region_backend_completions;
                }
            }
            for (std::size_t index = 0U; index < kernel_ready_count; ++index) {
                auto& member = get_process(contexts[index].process);
                // Checked completion writes the ordinary execution frame.
                // Revalidate it before a later stateless native completion.
                member.region_kernel_completion_boundary_validated = false;
                member.queued = false;
                member.waiting_on_static = false;
                member.static_trigger_mask = 0U;
                member.waiting_on_static = true;
                member.status = ProcessStatus::waiting;
                if (final_debug_states[index]) {
                    auto& cold = member.cold();
                    member.clear_frontier_debug_token();
                    cold.current_source = std::move(
                        final_debug_states[index]->source);
                    cold.current_scope.swap(
                        final_debug_states[index]->scope);
                }
            }
            result.executed = kernel_ready_count;
            kernel_executed = true;
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_region_kernel_runs;
                systemverilog_wave_profile_region_kernel_members
                    += kernel_ready_count;
                systemverilog_wave_profile_region_kernel_publications
                    += kernel_publications.size();
            }
        }
    }
    try {
        if (!kernel_executed) {
            // This interface forbids collapsed publication and reports an
            // accepted prefix. A failure never clears the scheduler-owned
            // unexecuted suffix.
            // Ordered cohort execution mutates native frames directly and
            // bypasses Impl::execute(id). Clear the cached completion proof
            // before offering the entries, including the unexecuted suffix.
            for (std::size_t index = 0U; index < entries.size(); ++index) {
                get_process(contexts[index].process)
                    .region_kernel_completion_boundary_validated = false;
            }
            result.executed
                = entries.front().executor->resume_ordered_cohort(entries);
            if (result.executed > entries.size()) {
                throw std::logic_error {
                    "ordered SV executor returned an invalid prefix"
                };
            }
            if (native_phase_profile_enabled) {
                ++native_phase_profile_cohort_resumes;
                native_phase_profile_cohort_members += result.executed;
            }
            for (std::size_t index = 0; index < result.executed; ++index) {
                auto& entry = entries[index];
                auto& member = get_process(contexts[index].process);
                if (entry.failure) {
                    if (!result.failure) {
                        result.failure = entry.failure;
                    }
                    continue;
                }
                if (native_process_count_profile_enabled) {
                    ++native_process_resume_counts[member.id];
                    ++native_process_cohort_resume_counts[member.id];
                    ++native_process_cohort_static_wait_counts[member.id];
                }
                try {
                    if (handle_executor_resume(member, entry.result)) {
                        throw std::logic_error {
                            "ordered SV wave did not reach its static wait"
                        };
                    }
                } catch (...) {
                    if (!result.failure) {
                        result.failure = std::current_exception();
                    }
                }
            }
        }
    } catch (...) {
        result.failure = std::current_exception();
    }
    clear_systemverilog_readiness_prefix(*frontier, result.executed);
    if (systemverilog_wave_profile_enabled) {
        ++systemverilog_wave_profile_calls;
        systemverilog_wave_profile_offered_members += entries.size();
        systemverilog_wave_profile_accepted_calls += result.executed != 0U;
        systemverilog_wave_profile_accepted_members += result.executed;
        systemverilog_wave_profile_declined_calls += result.executed == 0U;
        systemverilog_wave_profile_failed_calls += static_cast<bool>(result.failure);
        if (systemverilog_wave_profile_calls <= 16U) {
            std::cerr << "fsim-profile: sv-ordered-wave call=" << systemverilog_wave_profile_calls
                      << " time=" << scheduler.now()
                      << " round=" << scheduler.systemverilog_round()
                      << " offered=" << entries.size()
                      << " executed=" << result.executed
                      << " failure=" << static_cast<bool>(result.failure) << '\n';
        }
    }
    return result;
}

} // namespace fsim::runtime::simir
