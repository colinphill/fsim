// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace fsim::runtime::simir {

bool Interpreter::Impl::schedule_prepared_successor_readers(
    const SignalId signal,
    const PackedLogic4& previous,
    const PackedLogic4& current,
    const SignalChangeOrigin origin,
    const RegionPreparedOutputBatchState& batch,
    const std::size_t slot) noexcept
{
    const auto decline = [this]() noexcept {
        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_p3_group_fanout_declines;
        }
        return false;
    };
    if (origin.process_domain != ProcessSchedulingDomain::systemverilog
        || origin.phase != SchedulerPhase::active
        || !process_signal_access_inventory_complete || !region_graph
        || region_runtime_generation == 0U
        || scheduler.trace_hook_installed()
        || !fanout_cohort_grouping_enabled
        || native_process_count_profile_enabled
        || signal >= region_grouped_fanout_by_signal.size()
        || signal >= region_prepared_successor_by_signal.size()
        || signal >= dynamic_fanout.size()
        || signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook || container_element_change_hook
        || native_signal_observation_any_hook
        || native_signal_observation_required_hook
        || !batch.successor_masks_verified
        || !batch.successor_mapping_valid
        || batch.runtime_generation != region_runtime_generation
        || batch.successor_masks.size()
            != batch.successor_mapping_by_slot.size()
        || slot >= batch.successor_masks.size()
        || slot >= batch.expected_successor_masks.size()
        || slot >= batch.descriptors.size()
        || slot >= batch.slot_storage.size()
        || batch.component
            >= region_authoritative_state_by_component.size()
        || batch.descriptors[slot].signal_id != signal
        || batch.slot_storage[slot].changed == 0U) {
        return decline();
    }

    try {
        const auto& signal_mapping
            = region_prepared_successor_by_signal[signal];
        const auto& slot_mapping = batch.successor_mapping_by_slot[slot];
        const auto& fanout = region_grouped_fanout_by_signal[signal];
        if (signal_mapping.generation != region_runtime_generation
            || signal_mapping.component != batch.component
            || slot_mapping.signal != signal
            || slot_mapping.generation != signal_mapping.generation
            || slot_mapping.component != signal_mapping.component
            || slot_mapping.reader_offset != signal_mapping.reader_offset
            || slot_mapping.reader_count != signal_mapping.reader_count
            || slot_mapping.expected_mask != signal_mapping.expected_mask
            || batch.expected_successor_masks[slot]
                != signal_mapping.expected_mask
            || batch.successor_masks[slot]
                != signal_mapping.expected_mask
            || signal_mapping.reader_count == 0U
            || signal_mapping.reader_count > 64U
            || signal_mapping.expected_mask
                != (signal_mapping.reader_count == 64U
                        ? UINT64_MAX
                        : (UINT64_C(1) << signal_mapping.reader_count) - 1U)
            || signal_mapping.reader_offset
                > region_prepared_successor_readers.size()
            || signal_mapping.reader_count
                > region_prepared_successor_readers.size()
                    - signal_mapping.reader_offset
            || fanout.generation != region_runtime_generation
            || fanout.group_count != 1U
            || fanout.group_offset
                >= region_grouped_fanout_groups.size()
            || !dynamic_fanout[signal].empty()
            || !region_graph->component_epochs_current(batch.component)
            || batch.component >= region_activation_programs.size()
            || !region_activation_programs[batch.component]
            || batch.component
                >= region_readiness_mask_by_component.size()) {
            return decline();
        }

        const auto& group
            = region_grouped_fanout_groups[fanout.group_offset];
        if (group.generation != region_runtime_generation
            || group.component != batch.component
            || group.member_count != signal_mapping.reader_count
            || group.member_offset > region_grouped_fanout_members.size()
            || group.member_count
                > region_grouped_fanout_members.size()
                    - group.member_offset
            || group.component
                >= std::numeric_limits<std::uint64_t>::max()) {
            return decline();
        }

        auto* const authoritative
            = region_authoritative_state_by_component[group.component].get();
        const auto& queue_mask
            = region_readiness_mask_by_component[group.component];
        if (authoritative == nullptr || !authoritative->valid()
            || authoritative->generation() != region_runtime_generation
            || queue_mask.generation != region_runtime_generation
            || queue_mask.offset > region_readiness_mask_words.size()
            || queue_mask.word_count
                > region_readiness_mask_words.size()
                    - queue_mask.offset) {
            return decline();
        }

        constexpr std::size_t maximum_ticket_members = 64U;
        std::array<Scheduler::SystemVerilogGroupBatchMember,
            maximum_ticket_members> queued_members { };
        std::array<SchedulerSystemVerilogKeyReceipt,
            maximum_ticket_members> queued_receipts { };
        std::array<std::size_t, maximum_ticket_members>
            queued_reader_ordinals { };
        std::array<std::uint8_t, maximum_ticket_members> reader_matches { };
        std::array<ProcessState*, maximum_ticket_members> process_states { };
        std::array<const RegionPreparedSuccessorReaderBinding*,
            maximum_ticket_members> reader_bindings { };
        std::size_t queued_count { };

        for (std::size_t ordinal = 0U;
             ordinal < signal_mapping.reader_count; ++ordinal) {
            if ((signal_mapping.expected_mask
                    & (UINT64_C(1) << ordinal)) == 0U) {
                return decline();
            }
            const auto& binding = region_prepared_successor_readers[
                signal_mapping.reader_offset + ordinal];
            const auto member = binding.readiness_member;
            if (binding.process >= processes.size()
                || binding.process >= region_component_by_process.size()
                || binding.process
                    >= region_readiness_member_index_by_process.size()
                || binding.process
                    >= region_readiness_queued_by_process.size()
                || region_component_by_process[binding.process]
                    != group.component
                || region_readiness_member_index_by_process[binding.process]
                    != member
                || binding.static_trigger_mask == 0U
                || member >= authoritative->readiness().member_count()
                || binding.authoritative_member
                    >= authoritative->readiness().member_count()
                || binding.authoritative_readiness_word
                    != binding.authoritative_member / 64U
                || binding.authoritative_readiness_bit
                    != (UINT64_C(1)
                        << (binding.authoritative_member % 64U))
                || binding.readiness_word >= queue_mask.word_count
                || binding.readiness_word != member / 64U
                || binding.readiness_bit
                    != (UINT64_C(1) << (member % 64U))
                || binding.queued_mask_word
                    != queue_mask.offset + binding.readiness_word
                || binding.queued_mask_word
                    >= region_readiness_mask_words.size()) {
                return decline();
            }
            bool sensitivity_changed { };
            if (!match_region_fanout_sensitivity_ranges(
                    group.component, signal,
                    binding.sensitivity_range_generation,
                    binding.sensitivity_range_offset,
                    binding.sensitivity_range_count,
                    previous, current, sensitivity_changed)) {
                return decline();
            }
            reader_matches[ordinal]
                = sensitivity_changed ? 1U : 0U;
            reader_bindings[ordinal] = &binding;
        }

        // Validate the complete immutable reader/range map before inspecting
        // process scheduling state or constructing any queue entries.
        for (std::size_t ordinal = 0U;
             ordinal < signal_mapping.reader_count; ++ordinal) {
            const auto& binding = *reader_bindings[ordinal];
            auto& member_state = get_process(binding.process);
            if (member_state.id != binding.process) {
                return decline();
            }
            process_states[ordinal] = &member_state;
            if (reader_matches[ordinal] == 0U) {
                continue;
            }
            if (!member_state.waiting_on_static || member_state.queued) {
                continue;
            }
            if (!can_queue_systemverilog_wave(binding.process)
                || region_readiness_queued_by_process[binding.process]
                       .generation != 0U
                || (region_readiness_mask_words[binding.queued_mask_word]
                    & binding.readiness_bit) != 0U) {
                return decline();
            }
            if (queued_count == maximum_ticket_members) {
                return decline();
            }
            queued_reader_ordinals[queued_count] = ordinal;
            ++queued_count;
        }

        // The native mask is in kernel-member order. Scheduler tickets retain
        // stable ProcessId order, so order only the small set of admitted
        // ordinals; no graph/group lookup is needed on publication.
        std::sort(queued_reader_ordinals.begin(),
            queued_reader_ordinals.begin()
                + static_cast<std::ptrdiff_t>(queued_count),
            [&reader_bindings](const std::size_t left,
                const std::size_t right) noexcept {
                return reader_bindings[left]->process
                    < reader_bindings[right]->process;
            });
        for (std::size_t queued_index = 0U;
             queued_index < queued_count; ++queued_index) {
            const auto ordinal = queued_reader_ordinals[queued_index];
            const auto& binding = *reader_bindings[ordinal];
            auto& queued = queued_members[queued_index];
            queued.stable_order = binding.process;
            queued.payload = systemverilog_wave_payload | binding.process;
            queued.fallback_descriptor
                = detail::make_scheduler_task_descriptor<
                    SystemVerilogWaveFallbackPayload,
                    &Interpreter::Impl::dispatch_systemverilog_wave_fallback>(
                        { this, binding.process });
        }

        if (queued_count != 0U) {
            const SchedulerBatchGroupKey group_key {
                region_runtime_generation,
                static_cast<std::uint64_t>(group.component) + 1U };
            if (!scheduler.schedule_systemverilog_readiness_group(
                    SchedulerPhase::active, *this, group_key,
                    std::span { queued_members.data(), queued_count },
                    std::span { queued_receipts.data(), queued_count })) {
                return decline();
            }
            for (std::size_t queued_index = 0U;
                 queued_index < queued_count; ++queued_index) {
                const auto ordinal = queued_reader_ordinals[queued_index];
                const auto& binding = *reader_bindings[ordinal];
                auto& member_state = *process_states[ordinal];
                region_readiness_mask_words[binding.queued_mask_word]
                    |= binding.readiness_bit;
                member_state.queued = true;
                record_systemverilog_readiness_key(binding.process,
                    group.component, binding.readiness_member,
                    region_runtime_generation, queued_receipts[queued_index],
                    member_state.static_trigger_mask);
            }
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_p3_group_fanout_tickets;
            }
        }

        for (std::size_t ordinal = 0U;
             ordinal < signal_mapping.reader_count; ++ordinal) {
            if (reader_matches[ordinal] == 0U) {
                continue;
            }
            const auto& binding = *reader_bindings[ordinal];
            auto& member_state = *process_states[ordinal];
            static_cast<void>(authoritative->readiness().mark_mapped(
                binding.authoritative_member,
                binding.authoritative_readiness_word,
                binding.authoritative_readiness_bit,
                binding.static_trigger_mask));
            std::uint64_t trigger_mask { };
            if (!authoritative->readiness().take_mapped(
                    binding.authoritative_member,
                    binding.authoritative_readiness_word,
                    binding.authoritative_readiness_bit, trigger_mask)) {
                trigger_mask = binding.static_trigger_mask;
            }
            member_state.static_trigger_mask |= trigger_mask;
            merge_systemverilog_readiness_mask(
                binding.process, trigger_mask);
            if (trigger_mask != 0U && systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a4_ready_consumptions;
            }
            if (member_state.waiting_on_static
                && systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_a2_grouped_fanout_members;
            }
            if (systemverilog_wave_profile_enabled) {
                ++systemverilog_wave_profile_p3_group_fanout_members;
            }
        }

        if (systemverilog_wave_profile_enabled) {
            ++systemverilog_wave_profile_p3_group_fanout_groups;
            ++systemverilog_wave_profile_a3_mapped_successor_batches;
            systemverilog_wave_profile_a3_mapped_successor_readers
                += static_cast<std::uint64_t>(signal_mapping.reader_count);
        }
        return true;
    } catch (...) {
        return decline();
    }
}

} // namespace fsim::runtime::simir
