// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <limits>
#include <optional>
#include <ranges>
#include <span>

namespace fsim::runtime::simir {

namespace {

constexpr std::uint64_t kGenericProjectedRegionPayload = UINT64_C(1) << 60U;

[[nodiscard]] bool frontier_keys_equal(
    const RegionFrontierKeyV2& left,
    const RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool decode_generic_process_payload(
    const std::uint64_t payload,
    ProcessId& process) noexcept
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

[[nodiscard]] RegionFrontierKeyV2 activation_key_for_task(
    const RegionFrontierSlotV2& slot,
    const SchedulerBatchFrontierEntry& task) noexcept
{
    return {
        slot.time,
        slot.delta,
        slot.systemverilog_round,
        task.stable_order,
        task.sequence,
        slot.process_domain,
        slot.phase,
    };
}

} // namespace

bool Interpreter::Impl::stage_generic_frontier_update_batch(
    RegionFrontierComponentRuntime& runtime,
    std::exception_ptr& failure) noexcept
{
    failure = std::exception_ptr { };
    auto& frame = runtime.frame;
    std::size_t saved_pending_update_count { };
    std::size_t saved_pending_value_count { };
    bool rollback_pending_vectors { };
    struct Logic9ScratchCheckpoint {
        SignalId signal { };
        DirectSingleDriverLogic9WordUpdate before;
    };
    struct Logic9SignalListCheckpoint {
        std::size_t index { };
        SignalId before { };
        SignalId after { };
    };
    std::vector<Logic9ScratchCheckpoint> logic9_scratch_checkpoints;
    std::vector<Logic9SignalListCheckpoint> logic9_signal_list_checkpoints;
    std::uint32_t saved_native_logic9_word_update_count { };
    std::size_t expected_native_logic9_word_update_count { };
    bool rollback_logic9_sidecar { };

    try {
        const auto backend_pin = runtime.backend;
        if (runtime.owner != this || runtime.invalidated
            || !runtime.frame_initialized || !backend_pin
            || !backend_pin->executor || !region_graph
            || runtime.component >= region_frontier_runtime_by_component.size()
            || region_frontier_runtime_by_component[runtime.component].get()
                != &runtime
            || runtime.execution_mode
                != RegionFrontierExecutionModeV2::generic_deferred_update
            || runtime.runtime_generation == 0U
            || runtime.runtime_generation != region_runtime_generation
            || !region_graph->component_epochs_current(runtime.component)
            || (process_profile_enabled || update_profile_enabled)) {
            return false;
        }

        const auto& layout = backend_pin->executor->layout();
        const auto& kernel = backend_pin->kernel;
        if (!region_frontier_layout_prefix_valid_v2(
                layout.abi_version, layout.struct_size)
            || !region_frontier_layout_header_valid_v2(layout)) {
            return false;
        }
        if (!region_frontier_frame_prefix_valid_v2(
                frame.abi_version, frame.struct_size)
            || !region_frontier_frame_header_valid_v2(frame)) {
            return false;
        }
        if (layout.reserved0 != 0U || layout.reserved_capacity != 0U
            || layout.execution_mode
                != RegionFrontierExecutionModeV2::generic_deferred_update
            || layout.member_count != kernel.members.size()
            || layout.member_count != runtime.members.size()
            || layout.signal_slot_count != runtime.planes.size()
            || layout.signal_slot_count != runtime.port_planes.size()
            || layout.readiness_word_count != runtime.ready_words.size()
            || layout.metadata_count != 0U
            || layout.committed_signal_capacity
                != runtime.committed_signals.size()
            || layout.pending_write_capacity
                != runtime.pending_writes.size()
            || layout.staged_event_capacity < frame.staged_event_count
            || layout.write_site_count != kernel.outputs.size()
            || (layout.member_count != 0U && layout.members == nullptr)
            || (layout.signal_slot_count != 0U && layout.signals == nullptr)
            || (layout.write_site_count != 0U && layout.write_sites == nullptr)
            || (layout.member_count != 0U
                && (layout.max_member_write_counts == nullptr
                    || layout.max_member_staged_event_counts == nullptr))
            || frame.runtime_generation != runtime.runtime_generation
            || frame.bound_runtime_generation != runtime.runtime_generation
            || frame.certificate_generation != layout.certificate_generation
            || frame.component_generation != layout.component_generation
            || !region_frontier_slot_valid_for_mode_v2(
                RegionFrontierExecutionModeV2::generic_deferred_update,
                frame.slot)
            || frame.member_count != layout.member_count
            || frame.signal_slot_count != layout.signal_slot_count
            || frame.readiness_word_count != layout.readiness_word_count
            || frame.readiness_word_count != runtime.ready_words.size()
            || frame.metadata_count != 0U
            || frame.fanout_edge_count != layout.fanout_edge_count
            || frame.fanout_edge_count != runtime.fanout_edges.size()
            || frame.committed_signal_capacity
                != layout.committed_signal_capacity
            || frame.committed_signal_capacity
                != runtime.committed_signals.size()
            || frame.pending_write_capacity != layout.pending_write_capacity
            || frame.pending_write_capacity != runtime.pending_writes.size()
            || frame.staged_event_capacity != runtime.staged_events.size()
            || frame.staged_event_capacity > layout.staged_event_capacity
            || frame.staged_event_capacity < frame.staged_event_count
            || frame.scheduler_task_capacity
                != RegionFrontierComponentRuntime::scheduler_task_capacity
            || frame.scheduler_task_capacity != runtime.scheduler_tasks.size()
            || frame.scheduler_task_count > layout.member_count
            || frame.members != runtime.members.data()
            || frame.scheduler_tasks != runtime.scheduler_tasks.data()
            || frame.scheduler_task_count > runtime.scheduler_tasks.size()
            || frame.scheduler_task_count
                > runtime.original_scheduler_tasks.size()
            || frame.scheduler_task_cursor == 0U
            || frame.scheduler_task_cursor > frame.scheduler_task_count
            || frame.planes != runtime.planes.data()
            || frame.port_planes != runtime.port_planes.data()
            || frame.ready_words != runtime.ready_words.data()
            || frame.metadata != (runtime.metadata.empty()
                    ? nullptr : runtime.metadata.data())
            || frame.fanout_edges != (runtime.fanout_edges.empty()
                    ? nullptr : runtime.fanout_edges.data())
            || frame.pending_writes != runtime.pending_writes.data()
            || frame.staged_events != runtime.staged_events.data()
            || frame.committed_signals != runtime.committed_signals.data()
            || frame.native_frontier_member_dispatches
                != &runtime.native_member_dispatches
            || frame.stop_requested != &runtime.stop_requested
            || frame.scheduler_frontier_generation == 0U
            || frame.cut.scheduler_frontier_generation
                != frame.scheduler_frontier_generation
            || frame.cut.kind != RegionFrontierCutKindV2::closed_prefix
            || frame.committed_signal_count != 0U
            || frame.current_member != UINT32_MAX
            || frame.current_pending_write != UINT32_MAX
            || frame.generic_update_ack_count != 0U
            || !region_frontier_generic_batch_ready_counts_valid_v2(
                frame.pending_write_count, frame.staged_event_count,
                frame.generic_update_ack_count)
            || frame.pending_write_count > frame.pending_write_capacity
            || frame.staged_event_count > frame.staged_event_capacity
            || runtime.pending_plane_offsets.size()
                != runtime.pending_writes.size()
            || (runtime.pending_plane_words.empty()
                && frame.pending_write_count != 0U)) {
            return false;
        }

        const auto& slots = layout.write_sites;
        const auto& members = layout.members;
        const auto& signal_layouts = layout.signals;
        const auto no_owner = std::numeric_limits<std::uint32_t>::max();
        const auto member_count = static_cast<std::size_t>(layout.member_count);
        const auto signal_count = static_cast<std::size_t>(layout.signal_slot_count);
        const auto site_count = static_cast<std::size_t>(layout.write_site_count);

        // The consumed-prefix keys must still be the scheduler-authored
        // records for this live Generic Active callback. The generated slot
        // and the copied original task records are checked against the
        // scheduler's current borrowed frontier before they authorize any
        // staged output.
        const auto current_frontier
            = scheduler.current_generic_batch_frontier();
        if (!current_frontier
            || current_frontier->generation
                != frame.scheduler_frontier_generation
            || current_frontier->time != frame.slot.time
            || current_frontier->delta != frame.slot.delta
            || current_frontier->phase != SchedulerPhase::active
            || current_frontier->cursor != 0U
            || current_frontier->end != current_frontier->tasks.size()
            || scheduler.current_phase() != SchedulerPhase::active
            || scheduler.now() != frame.slot.time
            || scheduler.delta() != frame.slot.delta
            || frame.scheduler_task_count > current_frontier->tasks.size()) {
            return false;
        }
        std::size_t matching_frontier_starts { };
        for (std::size_t start = 0U;
             start <= current_frontier->tasks.size()
                    - frame.scheduler_task_count;
             ++start) {
            bool matches = true;
            for (std::size_t index = 0U;
                 index < frame.scheduler_task_count; ++index) {
                const auto& actual = current_frontier->tasks[start + index];
                const auto& original
                    = runtime.original_scheduler_tasks[index];
                if (actual.stable_order != original.stable_order
                    || actual.sequence != original.sequence
                    || actual.payload != original.payload) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                ++matching_frontier_starts;
            }
        }
        if (matching_frontier_starts != 1U) {
            return false;
        }

        if (runtime.generic_snapshot_plane_offsets.size() != signal_count) {
            return false;
        }
        std::size_t expected_snapshot_word_offset { };
        for (std::size_t slot = 0U; slot < signal_count; ++slot) {
            const auto& signal = signal_layouts[slot];
            const auto& plane = runtime.planes[slot];
            const auto word_count = static_cast<std::size_t>(signal.word_count);
            const auto offset = runtime.generic_snapshot_plane_offsets[slot];
            if (!region_frontier_value_shape_valid_v2(
                    signal.value_kind, signal.width, signal.word_count,
                    signal.plane_count)
                || signal.flags
                    != RegionFrontierPlaneFlagsV2::read_only_boundary_port
                || (signal.value_kind != RegionFrontierValueKindV2::logic4
                    && signal.value_kind
                        != RegionFrontierValueKindV2::logic9)
                || signal.signal_id >= this->signals.size()
                || signal.signal_id >= driven_values.size()
                || plane.signal_id != signal.signal_id
                || plane.owner_process_id != signal.owner_process_id
                || plane.value_kind != signal.value_kind
                || plane.width != signal.width
                || plane.word_count != signal.word_count
                || plane.plane_count != signal.plane_count
                || plane.flags != signal.flags
                || plane.metadata_index != signal.metadata_index
                || runtime.port_planes[slot] != &plane
                || word_count > std::numeric_limits<std::size_t>::max()
                    / signal.plane_count) {
                return false;
            }
            const bool logic9_signal
                = signal.value_kind == RegionFrontierValueKindV2::logic9;
            const auto& live_signal = this->signals[signal.signal_id];
            if (live_signal.value_kind
                    != (logic9_signal ? ValueKind::logic9 : ValueKind::logic4)
                || live_signal.initial_value.width() != signal.width
                || live_signal.initial_value.is_logic9() != logic9_signal
                || driven_values[signal.signal_id].width() != signal.width
                || driven_values[signal.signal_id].is_logic9()
                    != logic9_signal) {
                return false;
            }
            const auto required_words
                = word_count * static_cast<std::size_t>(signal.plane_count);
            if (offset != expected_snapshot_word_offset
                || offset > runtime.generic_snapshot_words.size()
                || required_words
                    > runtime.generic_snapshot_words.size() - offset) {
                return false;
            }
            std::array<std::span<const std::uint64_t>, 4U> value_planes;
            for (std::size_t plane_index = 0U;
                 plane_index < value_planes.size(); ++plane_index) {
                if (plane_index < signal.plane_count) {
                    const auto plane_offset = offset + plane_index * word_count;
                    const auto* const expected
                        = runtime.generic_snapshot_words.data() + plane_offset;
                    if (plane.boundary_planes[plane_index] != expected) {
                        return false;
                    }
                    value_planes[plane_index]
                        = std::span<const std::uint64_t> {
                            expected, word_count };
                } else if (plane.boundary_planes[plane_index] != nullptr) {
                    return false;
                }
            }
            for (std::size_t role_plane = 0U; role_plane < 4U;
                 ++role_plane) {
                if (plane.current_planes[role_plane] != nullptr
                    || plane.previous_planes[role_plane] != nullptr
                    || plane.stored_planes[role_plane] != nullptr
                    || plane.owner_planes[role_plane] != nullptr) {
                    return false;
                }
            }
            if (!region_frontier_plane_words_canonical_v2(
                    signal.value_kind, signal.width, signal.word_count,
                    value_planes)) {
                return false;
            }
            expected_snapshot_word_offset += required_words;
        }
        if (expected_snapshot_word_offset
            != runtime.generic_snapshot_words.size()) {
            return false;
        }

        // Authenticate every original task before deriving any update origin
        // from the generated member/event rows. A stopped generated step may
        // have consumed only a prefix, so task authentication covers the full
        // offered span while output events below must map into the consumed
        // prefix.
        for (std::size_t task_index = 0U;
             task_index < frame.scheduler_task_count; ++task_index) {
            const auto& original
                = runtime.original_scheduler_tasks[task_index];
            ProcessId process { };
            if (!decode_generic_process_payload(original.payload, process)) {
                return false;
            }
            std::optional<std::uint32_t> member_index;
            for (std::size_t candidate = 0U;
                 candidate < member_count; ++candidate) {
                if (members[candidate].process_id != process) {
                    continue;
                }
                if (member_index) {
                    return false;
                }
                member_index = static_cast<std::uint32_t>(candidate);
            }
            if (!member_index) {
                return false;
            }
            const auto& translated = runtime.scheduler_tasks[task_index];
            if (translated.stable_order != original.stable_order
                || translated.sequence != original.sequence
                || translated.payload
                    != encode_region_frontier_payload_v1(
                        RegionFrontierEventKindV2::member_activation,
                        *member_index)) {
                return false;
            }
            if (task_index != 0U) {
                const auto& previous
                    = runtime.original_scheduler_tasks[task_index - 1U];
                if (!(previous.stable_order < original.stable_order
                        || (previous.stable_order == original.stable_order
                            && previous.sequence < original.sequence))) {
                    return false;
                }
            }
            for (std::size_t prior = 0U; prior < task_index; ++prior) {
                ProcessId prior_process { };
                if (!decode_generic_process_payload(
                        runtime.original_scheduler_tasks[prior].payload,
                        prior_process)
                    || prior_process == process) {
                    return false;
                }
            }
        }

        // Authenticate the complete immutable generic output contract before
        // consulting a generated pending row. Every site is a whole Logic4 or
        // Logic9 WriteUpdate with one exact original Process owner/source
        // operation; graph-internal Generic outputs remain read-only to this
        // ABI and are published through the ordinary Generic Update queue.
        for (std::size_t member_index = 0U;
             member_index < member_count; ++member_index) {
            if (members[member_index].process_id
                    != kernel.members[member_index].process
                || runtime.members[member_index].process_id
                    != members[member_index].process_id) {
                return false;
            }
        }
        for (std::size_t site_index = 0U;
             site_index < site_count; ++site_index) {
            const auto& site = slots[site_index];
            if (site.member_index >= member_count
                || site.signal_slot >= signal_count
                || site.pending_slot >= frame.pending_write_capacity
                || site.update_kind
                    != static_cast<std::uint32_t>(RegionUpdateKind::generic)
                || site.event_kind != static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::generic_deferred_update)
                || (site.value_kind != RegionFrontierValueKindV2::logic4
                    && site.value_kind != RegionFrontierValueKindV2::logic9)
                || !region_frontier_value_shape_valid_v2(
                    site.value_kind, site.width, site.word_count,
                    site.plane_count)) {
                return false;
            }

            const auto& member = members[site.member_index];
            const auto& signal = signal_layouts[site.signal_slot];
            const auto process = static_cast<ProcessId>(member.process_id);
            const auto signal_id = static_cast<SignalId>(signal.signal_id);
            const bool logic9_site
                = site.value_kind == RegionFrontierValueKindV2::logic9;
            const bool kernel_internal_signal
                = std::ranges::find(kernel.internal_signals, signal_id)
                != kernel.internal_signals.end();
            if (signal.flags
                    != RegionFrontierPlaneFlagsV2::read_only_boundary_port
                || signal.metadata_index != no_owner
                || (kernel_internal_signal
                    ? signal.owner_process_id != process
                    : signal.owner_process_id != no_owner)
                || signal.value_kind != site.value_kind
                || signal.width != site.width
                || signal.word_count != site.word_count
                || signal.plane_count != site.plane_count
                || signal_id >= this->signals.size()
                || signal_id >= driven_values.size()
                || signal_id >= direct_signal_materialization_pending.size()) {
                return false;
            }
            if (kernel_internal_signal
                && std::ranges::count(kernel.outputs, signal_id,
                       &RegionConeOutputBinding::signal)
                    != 1) {
                return false;
            }

            const RegionConeOutputBinding* selected_output { };
            for (const auto& output : kernel.outputs) {
                if (output.owner != process || output.signal != signal_id
                    || output.source_instruction
                        != site.source_instruction) {
                    continue;
                }
                if (selected_output != nullptr) {
                    return false;
                }
                selected_output = &output;
            }
            if (selected_output == nullptr
                || selected_output->offset != 0U
                || selected_output->width != site.width
                || selected_output->value_kind
                    != (logic9_site ? ValueKind::logic9 : ValueKind::logic4)
                || selected_output->domain != SignalUpdateDomain::generic
                || selected_output->update_kind != RegionUpdateKind::generic) {
                return false;
            }

            if (process >= processes.size()) {
                return false;
            }
            const auto program = processes.program_view(process);
            if (program.id() != process
                || program.scheduling_domain()
                    != ProcessSchedulingDomain::generic
                || site.source_instruction >= program.operations().size()) {
                return false;
            }
            const auto source_operation
                = program.operations().expanded(site.source_instruction);
            const auto* const write
                = operation_get_if<WriteUpdate>(&source_operation);
            if (write == nullptr || write->signal != signal_id
                || write->domain != SignalUpdateDomain::generic) {
                return false;
            }

            const auto& live_signal = this->signals[signal_id];
            if (live_signal.value_kind
                    != (logic9_site ? ValueKind::logic9 : ValueKind::logic4)
                || live_signal.initial_value.is_logic9() != logic9_site
                || live_signal.initial_value.width() != site.width
                || live_signal.systemverilog_scalar
                    != SystemVerilogScalarKind::None
                || driven_values[signal_id].width() != site.width
                || driven_values[signal_id].is_logic9() != logic9_site
                || direct_signal_materialization_pending[signal_id] != 0U) {
                return false;
            }

            const auto& member_output_range = members[site.member_index];
            const auto first_site
                = static_cast<std::uint64_t>(member_output_range.first_write_site);
            const auto end_site = first_site
                + static_cast<std::uint64_t>(member_output_range.write_site_count);
            if (site_index < first_site || site_index >= end_site
                || static_cast<std::uint64_t>(site.pending_slot) < first_site
                || static_cast<std::uint64_t>(site.pending_slot) >= end_site) {
                return false;
            }

            for (std::size_t prior = 0U; prior < site_index; ++prior) {
                if (slots[prior].pending_slot == site.pending_slot) {
                    return false;
                }
            }

            // `stage_update` routes a matching module-path output through a
            // different scheduler contract. Decline before touching either
            // generic pending vector so the ordinary process path owns it.
            const auto write_end = static_cast<std::uint64_t>(site.width);
            for (const auto& path : module_paths) {
                if (std::ranges::find(path.drivers, process)
                    == path.drivers.end()) {
                    continue;
                }
                for (const auto& destination : path.destinations) {
                    if (destination.signal != signal_id) {
                        continue;
                    }
                    const auto destination_begin
                        = static_cast<std::uint64_t>(destination.offset);
                    const auto destination_end = destination_begin
                        + static_cast<std::uint64_t>(destination.width);
                    if (destination_begin < write_end
                        && destination_end != destination_begin) {
                        return false;
                    }
                }
            }

            for (std::size_t prior_site = 0U;
                 prior_site < site_index; ++prior_site) {
                const auto& previous = slots[prior_site];
                if (previous.member_index == site.member_index
                    && previous.signal_slot == site.signal_slot
                    && previous.source_instruction
                        == site.source_instruction) {
                    return false;
                }
            }
        }

        if (frame.staged_event_count == 0U
            || frame.pending_write_count != frame.staged_event_count) {
            return false;
        }

        std::size_t previous_event_task_index { };
        std::uint32_t previous_event_source_instruction { };
        bool have_previous_event { };
        struct PreparedGenericUpdate {
            ProcessId process { };
            SignalId signal { };
            PackedLogic4 value;
        };
        std::vector<PreparedGenericUpdate> prepared;
        prepared.reserve(frame.staged_event_count);

        for (std::size_t event_index = 0U;
             event_index < frame.staged_event_count; ++event_index) {
            const auto& event = runtime.staged_events[event_index];
            if (event.descriptor_index >= frame.pending_write_capacity) {
                return false;
            }
            for (std::size_t prior = 0U; prior < event_index; ++prior) {
                if (runtime.staged_events[prior].descriptor_index
                    == event.descriptor_index) {
                    return false;
                }
            }

            const auto pending_slot
                = static_cast<std::size_t>(event.descriptor_index);
            const RegionFrontierWriteSiteV2* selected_site { };
            for (std::size_t site_index = 0U;
                 site_index < site_count; ++site_index) {
                if (slots[site_index].pending_slot == event.descriptor_index) {
                    if (selected_site != nullptr) {
                        return false;
                    }
                    selected_site = &slots[site_index];
                }
            }
            if (selected_site == nullptr) {
                return false;
            }

            const auto& site = *selected_site;
            const auto& pending = runtime.pending_writes[pending_slot];
            const auto& member = members[site.member_index];
            const auto& signal = signal_layouts[site.signal_slot];
            const auto process = static_cast<ProcessId>(member.process_id);
            const auto signal_id = static_cast<SignalId>(signal.signal_id);
            std::optional<std::size_t> origin_task_index;
            for (std::size_t task_index = 0U;
                 task_index < frame.scheduler_task_cursor; ++task_index) {
                ProcessId task_process { };
                if (!decode_generic_process_payload(
                        runtime.original_scheduler_tasks[task_index].payload,
                        task_process)) {
                    return false;
                }
                if (task_process != process) {
                    continue;
                }
                if (origin_task_index) {
                    return false;
                }
                origin_task_index = task_index;
            }
            if (!origin_task_index) {
                return false;
            }
            const auto expected_origin = activation_key_for_task(
                frame.slot,
                runtime.original_scheduler_tasks[*origin_task_index]);
            if (!frontier_keys_equal(
                    runtime.members[site.member_index].activation_origin,
                    expected_origin)
                || (have_previous_event
                    && (*origin_task_index < previous_event_task_index
                        || (*origin_task_index == previous_event_task_index
                            && site.source_instruction
                                <= previous_event_source_instruction)))) {
                return false;
            }
            previous_event_task_index = *origin_task_index;
            previous_event_source_instruction = site.source_instruction;
            have_previous_event = true;
            if (!region_frontier_generic_write_descriptor_matches_v2(
                    event, pending, site, signal, member, site.member_index,
                    site.signal_slot,
                    expected_origin,
                    frame.slot)
                || event.origin.process_domain
                    != static_cast<std::uint32_t>(
                        ProcessSchedulingDomain::generic)
                || event.origin.phase
                    != static_cast<std::uint32_t>(SchedulerPhase::active)
                || !frontier_keys_equal(
                    event.origin, expected_origin)
                || !frontier_keys_equal(
                    pending.origin, expected_origin)
                || pending.member_index != site.member_index
                || pending.signal_slot != site.signal_slot
                || pending.source_instruction != site.source_instruction
                || pending.value_kind != site.value_kind
                || pending.width != site.width
                || pending.word_count != site.word_count
                || pending.plane_count != site.plane_count) {
                return false;
            }

            const auto offset = runtime.pending_plane_offsets[pending_slot];
            const auto words = static_cast<std::size_t>(pending.word_count);
            if (offset == std::numeric_limits<std::size_t>::max()
                || words > std::numeric_limits<std::size_t>::max()
                    / pending.plane_count) {
                return false;
            }
            const auto required_words = words * pending.plane_count;
            if (offset > runtime.pending_plane_words.size()
                || required_words
                    > runtime.pending_plane_words.size() - offset) {
                return false;
            }
            std::array<std::span<const std::uint64_t>, 4U> value_planes;
            for (std::size_t plane_index = 0U;
                 plane_index < value_planes.size(); ++plane_index) {
                if (plane_index < pending.plane_count) {
                    const auto plane_offset = offset + plane_index * words;
                    const auto* const expected
                        = runtime.pending_plane_words.data() + plane_offset;
                    if (pending.value_planes[plane_index] != expected) {
                        return false;
                    }
                    value_planes[plane_index]
                        = std::span<const std::uint64_t> {
                            expected, words };
                } else if (pending.value_planes[plane_index] != nullptr) {
                    return false;
                }
            }
            if (!region_frontier_plane_words_canonical_v2(
                    pending.value_kind, pending.width,
                    pending.word_count, value_planes)) {
                return false;
            }

            auto value = pending.value_kind
                    == RegionFrontierValueKindV2::logic9
                ? PackedLogic4::from_logic9_word_planes(
                    pending.width, value_planes[0U], value_planes[1U],
                    value_planes[2U], value_planes[3U])
                : PackedLogic4::from_word_planes(
                    pending.width, value_planes[0U], value_planes[1U]);
            prepared.push_back(PreparedGenericUpdate {
                process,
                signal_id,
                std::move(value)
            });
        }

        std::size_t active_pending_count { };
        for (std::size_t pending_slot = 0U;
             pending_slot < runtime.pending_writes.size(); ++pending_slot) {
            const auto& pending = runtime.pending_writes[pending_slot];
            const bool referenced = std::ranges::any_of(
                runtime.staged_events.begin(),
                runtime.staged_events.begin()
                    + static_cast<std::ptrdiff_t>(frame.staged_event_count),
                [pending_slot](const RegionFrontierStagedEventV2& event) {
                    return event.descriptor_index == pending_slot;
                });
            if (referenced) {
                if (pending.flags != kRegionFrontierGenericWriteFlagsV2) {
                    return false;
                }
                ++active_pending_count;
            } else if (pending.flags != 0U) {
                return false;
            }
        }
        if (active_pending_count != frame.pending_write_count
            || prepared.size() != frame.staged_event_count) {
            return false;
        }

        // A narrow Logic9 generic write uses the existing sidecar to prevent
        // a later native word update from bypassing Generic Update ordering.
        // An already-active native row is retained by the checked route. The
        // private region has not published yet, so this decline does not
        // disturb that row. This helper admits only the clean unlisted to
        // generic_blocked transition and a previously blocked row.
        saved_native_logic9_word_update_count
            = native_logic9_word_update_count;
        if (native_logic9_word_update_count
            > native_logic9_word_update_signals.size()) {
            return false;
        }
        const auto narrow_logic9_sidecar_update_count
            = static_cast<std::size_t>(std::ranges::count_if(
                prepared, [&](const PreparedGenericUpdate& update) {
                    const auto& signal = signals[update.signal];
                    return update.value.is_logic9()
                        && signal.initial_value.width() <= 64U
                        && signal.resolution == ResolutionKind::std_logic;
                }));
        if (narrow_logic9_sidecar_update_count != 0U) {
            logic9_scratch_checkpoints.reserve(
                narrow_logic9_sidecar_update_count);
            logic9_signal_list_checkpoints.reserve(
                narrow_logic9_sidecar_update_count);
        }
        auto planned_native_logic9_count
            = static_cast<std::size_t>(native_logic9_word_update_count);
        for (const auto& update : prepared) {
            const auto& live_signal = signals[update.signal];
            if (!update.value.is_logic9()
                || live_signal.initial_value.width() > 64U
                || live_signal.resolution != ResolutionKind::std_logic) {
                continue;
            }
            if (update.signal
                >= direct_single_driver_logic9_word_scratch.size()) {
                return false;
            }
            if (std::ranges::any_of(logic9_scratch_checkpoints,
                    [&](const Logic9ScratchCheckpoint& checkpoint) {
                        return checkpoint.signal == update.signal;
                    })) {
                continue;
            }

            std::size_t queued_occurrences { };
            for (std::size_t index = 0U;
                 index < native_logic9_word_update_count; ++index) {
                if (native_logic9_word_update_signals[index]
                    == update.signal) {
                    ++queued_occurrences;
                }
            }
            const auto& scratch
                = direct_single_driver_logic9_word_scratch[update.signal];
            switch (scratch.active) {
            case DirectSingleDriverLogic9WordUpdate::unlisted:
                if (queued_occurrences != 0U
                    || planned_native_logic9_count
                        >= native_logic9_word_update_signals.size()
                    || planned_native_logic9_count
                        >= std::numeric_limits<std::uint32_t>::max()) {
                    return false;
                }
                logic9_signal_list_checkpoints.push_back({
                    planned_native_logic9_count,
                    native_logic9_word_update_signals[
                        planned_native_logic9_count],
                    update.signal });
                ++planned_native_logic9_count;
                break;
            case DirectSingleDriverLogic9WordUpdate::native:
                return false;
            case DirectSingleDriverLogic9WordUpdate::generic_blocked:
                if (queued_occurrences != 1U || scratch.mask != 0U) {
                    return false;
                }
                break;
            default:
                return false;
            }
            logic9_scratch_checkpoints.push_back({ update.signal, scratch });
        }
        expected_native_logic9_word_update_count
            = planned_native_logic9_count;

        if (frame.staged_event_count
                > std::numeric_limits<std::size_t>::max()
                    - pending_updates.size()
            || frame.staged_event_count
                > std::numeric_limits<std::size_t>::max()
                    - pending_update_values.size()) {
            return false;
        }
        constexpr auto max_packed_value_count
            = static_cast<std::size_t>(
                std::numeric_limits<std::uint32_t>::max())
                + std::size_t { 1U };
        if (pending_update_values.size() > max_packed_value_count
            || frame.staged_event_count
                > max_packed_value_count - pending_update_values.size()) {
            return false;
        }

        saved_pending_update_count = pending_updates.size();
        saved_pending_value_count = pending_update_values.size();
        pending_updates.reserve(saved_pending_update_count
            + prepared.size());
        pending_update_values.reserve(saved_pending_value_count
            + prepared.size());
        rollback_pending_vectors = true;

        // This may reserve the ordinary Update ticket. If it throws, no
        // values have been appended; if it succeeds and a later append throws,
        // rollback leaves at most an empty callback for the Active retry.
        schedule_update_commit();
        rollback_logic9_sidecar = true;
        for (auto& update : prepared) {
            stage_update(update.process, update.signal,
                std::move(update.value), SignalUpdateDomain::generic);
        }
        if (native_logic9_word_update_count
            != expected_native_logic9_word_update_count) {
            throw std::logic_error {
                "Generic Logic9 staging changed an unexpected native sidecar count"
            };
        }
        for (const auto& checkpoint : logic9_signal_list_checkpoints) {
            if (native_logic9_word_update_signals[checkpoint.index]
                != checkpoint.after) {
                throw std::logic_error {
                    "Generic Logic9 staging changed an unexpected sidecar row"
                };
            }
        }
        for (const auto& checkpoint : logic9_scratch_checkpoints) {
            const auto& staged
                = direct_single_driver_logic9_word_scratch[
                    checkpoint.signal];
            const auto expected_active
                = checkpoint.before.active
                        == DirectSingleDriverLogic9WordUpdate::unlisted
                ? DirectSingleDriverLogic9WordUpdate::generic_blocked
                : checkpoint.before.active;
            const auto expected_mask
                = checkpoint.before.active
                        == DirectSingleDriverLogic9WordUpdate::unlisted
                ? std::uint64_t { 0U } : checkpoint.before.mask;
            if (staged.planes != checkpoint.before.planes
                || staged.mask != expected_mask
                || staged.width != checkpoint.before.width
                || staged.process != checkpoint.before.process
                || staged.active != expected_active) {
                throw std::logic_error {
                    "Generic Logic9 staging changed an unexpected scratch row"
                };
            }
        }

        frame.generic_update_ack_count = frame.staged_event_count;
        rollback_pending_vectors = false;
        rollback_logic9_sidecar = false;
        return true;
    } catch (...) {
        if (rollback_pending_vectors) {
            while (pending_updates.size() > saved_pending_update_count) {
                pending_updates.pop_back();
            }
            while (pending_update_values.size() > saved_pending_value_count) {
                pending_update_values.pop_back();
            }
        }
        if (rollback_logic9_sidecar) {
            for (const auto& checkpoint : logic9_scratch_checkpoints) {
                direct_single_driver_logic9_word_scratch[
                    checkpoint.signal] = checkpoint.before;
            }
            for (const auto& checkpoint : logic9_signal_list_checkpoints) {
                native_logic9_word_update_signals[checkpoint.index]
                    = checkpoint.before;
            }
            native_logic9_word_update_count
                = saved_native_logic9_word_update_count;
        }
        frame.generic_update_ack_count = 0U;
        failure = std::current_exception();
        return false;
    }
}

} // namespace fsim::runtime::simir
