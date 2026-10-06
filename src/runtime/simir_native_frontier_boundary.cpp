// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "simir_builtin_process_executor_capability.hpp"

#include <algorithm>

namespace fsim::runtime::simir {

namespace {

[[nodiscard]] bool frontier_role_matches(
    const AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const std::size_t writable_ordinal,
    const SignalId signal,
    const ProcessId owner,
    const PackedPlaneRole role,
    const RegionFrontierValueKindV2 kind,
    const std::size_t word_count,
    std::span<std::uint64_t* const, 4U> bound) noexcept
{
    std::array<std::span<std::uint64_t>, 4U> words;
    const auto plane_count = region_frontier_required_plane_count_v2(kind);
    if (plane_count == 0U || !lease.plane_words_at(
            writable_ordinal, signal, role, owner, words)) {
        return false;
    }
    for (std::size_t plane = 0U; plane < words.size(); ++plane) {
        if (plane < plane_count) {
            if (words[plane].size() != word_count
                || words[plane].data() == nullptr
                || words[plane].data() != bound[plane]) {
                return false;
            }
        } else if (!words[plane].empty() || bound[plane] != nullptr) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_frontier_key(
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

[[nodiscard]] bool exact_full_alias_leaf_projection(
    const RegionSignalAliasFamilyDescriptor& family,
    const std::uint32_t offset,
    const std::uint32_t width,
    const SignalId expected_leaf) noexcept
{
    if (!family.complete || !family.proxy_writable
        || family.width == 0U || family.leaves.empty()
        || offset >= family.width || width == 0U
        || width > family.width - offset) {
        return false;
    }

    const auto range_end = offset + width;
    const RegionSignalAliasLeaf* projected_leaf { };
    for (std::size_t ordinal = 0U;
         ordinal < family.leaves.size(); ++ordinal) {
        const auto& leaf = family.leaves[ordinal];
        if (leaf.ordinal != ordinal || leaf.width == 0U
            || leaf.offset >= family.width
            || leaf.width > family.width - leaf.offset) {
            return false;
        }

        const auto leaf_end = leaf.offset + leaf.width;
        if (offset >= leaf_end || leaf.offset >= range_end) {
            continue;
        }
        if (projected_leaf != nullptr) {
            return false;
        }
        projected_leaf = &leaf;
    }

    return projected_leaf != nullptr
        && projected_leaf->signal == expected_leaf
        && projected_leaf->offset == offset
        && projected_leaf->width == width;
}

} // namespace

bool Interpreter::Impl::native_boundary_slice_matches_alias_family(
    const SignalId proxy,
    const std::uint32_t offset,
    const std::uint32_t width,
    const SignalId expected_leaf) const noexcept
{
    if (!region_graph || proxy >= signals.size()
        || expected_leaf >= signals.size()
        || proxy >= signal_container_aggregate_aliases.size()
        || proxy >= signal_container_element_aliases.size()
        || proxy >= signal_container_aliases.size()) {
        return false;
    }

    const RegionSignalAliasFamilyDescriptor* family { };
    for (const auto& candidate : region_graph->signal_alias_families()) {
        if (candidate.proxy != proxy) {
            continue;
        }
        if (family != nullptr) {
            return false;
        }
        family = &candidate;
    }
    if (family == nullptr || !family->complete
        || !family->proxy_readable || !family->proxy_writable
        || family->width != signals[proxy].initial_value.width()
        || signals[proxy].value_kind != ValueKind::logic4
        || signals[proxy].initial_value.is_logic9()
        || signals[proxy].resolution != ResolutionKind::sv_wire
        || family->object >= container_objects.size()
        || family->object >= container_aggregate_signal_aliases.size()
        || family->object >= container_element_signal_aliases.size()
        || family->object >= container_signal_aliases.size()) {
        return false;
    }

    const auto& aggregate = container_aggregate_signal_aliases[family->object];
    const auto& container = container_objects[family->object];
    const auto& aliases = container_element_signal_aliases[family->object];
    const auto& type = container.initial_value.type;
    if (!aggregate || aggregate->object != family->object
        || aggregate->signal != proxy || !aggregate->readable
        || !aggregate->writable || container.slice_alias
        || container_signal_aliases[family->object]
        || !type.fixed || type.dimensions.empty()
        || type.element_kind != ContainerElementKind::Packed
        || type.two_state || type.element_width == 0U
        || aliases.empty() || aliases.size() != family->leaves.size()
        || aliases.size() != container.initial_value.elements.size()
        || aliases.size()
            != static_cast<std::size_t>(family->width / type.element_width)
        || family->width % type.element_width != 0U
        || signal_container_aggregate_aliases[proxy] != family->object
        || signal_container_element_aliases[proxy]
        || !signal_container_aliases[proxy].empty()) {
        return false;
    }

    const auto& proxy_value = signals[proxy].initial_value;
    if (proxy_value.width() != family->width || width == 0U
        || offset >= family->width || width > family->width - offset) {
        return false;
    }

    const RegionSignalAliasLeaf* selected_leaf { };
    const auto element_width = static_cast<std::size_t>(type.element_width);
    for (std::size_t ordinal = 0U; ordinal < aliases.size(); ++ordinal) {
        const auto& alias = aliases[ordinal];
        const auto& leaf = family->leaves[ordinal];
        if (!alias || alias->object != family->object
            || alias->ordinal != ordinal || !alias->readable
            || !alias->writable || leaf.ordinal != ordinal
            || leaf.signal != alias->signal
            || leaf.width != type.element_width
            || alias->signal >= signals.size()
            || alias->signal >= signal_container_element_aliases.size()
            || alias->signal >= signal_container_aggregate_aliases.size()
            || alias->signal >= signal_container_aliases.size()) {
            return false;
        }

        const auto expected_offset
            = (aliases.size() - ordinal - 1U) * element_width;
        const auto& leaf_signal = signals[alias->signal];
        if (expected_offset > std::numeric_limits<std::uint32_t>::max()
            || leaf.offset != expected_offset
            || leaf_signal.initial_value.width() != type.element_width
            || leaf_signal.value_kind != ValueKind::logic4
            || leaf_signal.initial_value.is_logic9()
            || leaf_signal.resolution != ResolutionKind::sv_wire
            || signal_container_element_aliases[alias->signal]
                != std::optional<std::pair<ContainerObjectId, std::size_t>> {
                    std::pair { family->object, ordinal } }
            || signal_container_aggregate_aliases[alias->signal]
            || !signal_container_aliases[alias->signal].empty()) {
            return false;
        }

        if (alias->signal == expected_leaf) {
            if (selected_leaf != nullptr) {
                return false;
            }
            selected_leaf = &leaf;
        }
    }

    return selected_leaf != nullptr && selected_leaf->offset == offset
        && selected_leaf->width == width
        && exact_full_alias_leaf_projection(
            *family, offset, width, expected_leaf);
}

void Interpreter::Impl::RegionFrontierComponentRuntime::
    publish_boundary_commit(
        const SchedulerBatchFrontierEntry& task,
        const std::uint32_t pending_slot,
        AuthoritativeSignalPlanes::FrontierWriteLease& lease)
{
    boundary_callback_started_slot
        = std::numeric_limits<std::uint32_t>::max();
    const auto backend_pin = backend;
    const auto fail_closed = [this](const std::source_location caller
                                       = std::source_location::current()) {
        invalidate(caller);
        throw std::logic_error {
            "invalid native frontier boundary publication"
        };
    };

    try {
        if (owner == nullptr || !frame_initialized || backend_pin == nullptr
            || backend_pin->executor == nullptr || invalidated
            || runtime_generation == 0U
            || runtime_generation != owner->region_runtime_generation
            || !owner->region_graph
            || !owner->region_graph->component_epochs_current(component)
            || component
                >= owner->region_authoritative_state_by_component.size()
            || !owner->region_authoritative_state_by_component[component]
            || !authoritative_state
            || owner->region_authoritative_state_by_component[component].get()
                != authoritative_state.get()
            || !owner->region_authoritative_state_by_component[component]
                    ->valid()
            || owner->region_authoritative_state_by_component[component]
                    ->generation() != runtime_generation
            || !region_frontier_frame_header_valid_v2(frame)
            || frame.generic_update_ack_count != 0U
            || frame.runtime_generation != runtime_generation
            || frame.bound_runtime_generation != runtime_generation
            || pending_slot >= pending_writes.size()
            || pending_slot >= pending_plane_offsets.size()
            || frame.pending_write_capacity != pending_writes.size()
            || frame.pending_writes != pending_writes.data()
            || frame.pending_write_count > frame.pending_write_capacity
            || frame.signal_slot_count != planes.size()
            || frame.planes != planes.data()
            || frame.metadata_count != metadata.size()
            || frame.metadata
                != (metadata.empty() ? nullptr : metadata.data())
            || frame.current_pending_write != pending_slot) {
            fail_closed();
        }

        auto& write = pending_writes[pending_slot];
        constexpr std::uint32_t required_flags
            = pending_active | pending_value_ready | pending_key_assigned
            | pending_boundary_target;
        constexpr std::uint32_t known_flags
            = pending_active | pending_value_ready | pending_key_assigned
            | pending_internal_target | pending_boundary_target
            | pending_committed;
        if ((write.flags & required_flags) != required_flags
            || (write.flags & pending_internal_target) != 0U
            || (write.flags & (pending_internal_target
                                  | pending_boundary_target))
                != pending_boundary_target
            || (write.flags & ~known_flags) != 0U || write.reserved != 0U) {
            fail_closed();
        }

        const auto& layout = backend_pin->executor->layout();
        if (!region_frontier_layout_header_valid_v2(layout)
            || layout.reserved0 != 0U || layout.reserved_capacity != 0U
            || layout.execution_mode != RegionFrontierExecutionModeV2::systemverilog_active
            || frame.certificate_generation
                != layout.certificate_generation
            || frame.component_generation != layout.component_generation
            || frame.member_count != layout.member_count
            || frame.signal_slot_count != layout.signal_slot_count
            || frame.metadata_count != layout.metadata_count
            || layout.write_sites == nullptr || layout.signals == nullptr
            || layout.members == nullptr
            || pending_slot >= frame.pending_write_capacity
            || write.member_index >= layout.member_count
            || write.signal_slot >= layout.signal_slot_count
            || write.member_index >= backend_pin->kernel.members.size()
            || write.signal_slot >= planes.size()) {
            fail_closed();
        }

        const RegionFrontierWriteSiteV2* site { };
        for (std::size_t index = 0U;
             index < layout.write_site_count; ++index) {
            const auto& candidate = layout.write_sites[index];
            if (candidate.pending_slot != pending_slot) {
                continue;
            }
            if (site != nullptr) {
                fail_closed();
            }
            site = &candidate;
        }
        if (site == nullptr
            || site->member_index != write.member_index
            || site->signal_slot != write.signal_slot
            || site->source_instruction != write.source_instruction
            || site->update_kind != write.update_kind
            || site->event_kind
                != static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::boundary_commit)
            || site->width != write.width
            || site->word_count != write.word_count
            || site->value_kind != write.value_kind
            || site->plane_count != write.plane_count) {
            fail_closed();
        }

        const auto& signal_layout = layout.signals[write.signal_slot];
        const auto& plane = planes[write.signal_slot];
        if (signal_layout.signal_id >= owner->signals.size()) {
            fail_closed();
        }
        const auto& runtime_signal
            = owner->signals[signal_layout.signal_id];
        const auto runtime_signal_width
            = runtime_signal.initial_value.width();
        const auto runtime_signal_word_count
            = static_cast<std::size_t>(runtime_signal_width) / 64U
            + (runtime_signal_width % 64U != 0U ? 1U : 0U);
        if (signal_layout.signal_id != plane.signal_id
            || signal_layout.width != runtime_signal_width
            || signal_layout.word_count != runtime_signal_word_count
            || signal_layout.value_kind != write.value_kind
            || signal_layout.plane_count != write.plane_count
            || (signal_layout.flags & read_only_boundary_port) == 0U
            || (signal_layout.flags & certified_internal_single_owner) != 0U
            || signal_layout.metadata_index
                != std::numeric_limits<std::uint32_t>::max()
            || (plane.flags & read_only_boundary_port) == 0U
            || (plane.flags & certified_internal_single_owner) != 0U
            || plane.value_kind != write.value_kind
            || plane.width != runtime_signal_width
            || plane.word_count != runtime_signal_word_count
            || plane.plane_count != write.plane_count
            || !region_frontier_plane_bindings_valid_v2(plane)
            || !region_frontier_pending_write_bindings_valid_v2(write)
            || !region_frontier_value_shape_valid_v2(write.value_kind,
                write.width, write.word_count, write.plane_count)
            || site->update_kind
                != static_cast<std::uint32_t>(
                    RegionUpdateKind::systemverilog_active)
            || write.update_kind != site->update_kind) {
            fail_closed();
        }

        const auto process = static_cast<ProcessId>(
            layout.members[write.member_index].process_id);
        if (process != backend_pin->kernel.members[write.member_index].process
            || signal_layout.owner_process_id
                != std::numeric_limits<std::uint32_t>::max()
            || plane.owner_process_id != signal_layout.owner_process_id
            || task.stable_order != static_cast<StableOrder>(process)
            || task.sequence != write.commit_key.sequence
            || task.payload
                != encode_region_frontier_payload_v1(
                    RegionFrontierEventKindV2::boundary_commit,
                    pending_slot)
            || write.commit_key.stable_order
                != static_cast<std::uint64_t>(task.stable_order)
            || write.commit_key.time != frame.slot.time
            || write.commit_key.delta != frame.slot.delta
            || write.commit_key.systemverilog_round
                != frame.slot.systemverilog_round
            || write.commit_key.process_domain != frame.slot.process_domain
            || write.commit_key.phase != frame.slot.phase
            || frame.slot.process_domain
                != static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            || frame.slot.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)
            || write.origin.process_domain
                != static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            || write.origin.phase
                != static_cast<std::uint32_t>(SchedulerPhase::active)) {
            fail_closed();
        }

        const auto expected_kind = write.value_kind
            == RegionFrontierValueKindV2::logic4
            ? ValueKind::logic4 : ValueKind::logic9;
        if (runtime_signal.value_kind != expected_kind
            || runtime_signal.initial_value.is_logic9()
                != (write.value_kind == RegionFrontierValueKindV2::logic9)
            || process >= owner->processes.size()) {
            fail_closed();
        }

        const RegionConeOutputBinding* output_binding { };
        for (const auto& candidate : backend_pin->kernel.outputs) {
            if (candidate.owner != process
                || candidate.signal != signal_layout.signal_id
                || candidate.source_instruction
                    != write.source_instruction) {
                continue;
            }
            if (output_binding != nullptr) {
                fail_closed();
            }
            output_binding = &candidate;
        }
        const auto output_signal_width = output_binding != nullptr
            && output_binding->signal_width != 0U
            ? output_binding->signal_width
            : output_binding != nullptr ? output_binding->width : 0U;
        if (output_binding == nullptr
            || output_signal_width != runtime_signal_width
            || output_binding->offset > runtime_signal_width
            || output_binding->width
                > runtime_signal_width - output_binding->offset
            || output_binding->width != write.width
            || output_binding->value_kind != expected_kind
            || output_binding->domain
                != SignalUpdateDomain::systemverilog_active
            || output_binding->update_kind
                != RegionUpdateKind::systemverilog_active) {
            fail_closed();
        }

        const auto source_program = owner->processes.program_view(process);
        const auto& operations = source_program.operations();
        if (write.source_instruction >= operations.size()) {
            fail_closed();
        }
        // Inspect the current override without copying the operation. These
        // write types have only a signal binding to expand; all other fields
        // are already exact in the stored operation.
        const auto& source_operation
            = operations.at(write.source_instruction);
        const auto* const whole_write
            = operation_get_if<WriteUpdate>(&source_operation);
        const auto* const slice_write
            = operation_get_if<WriteUpdateSlice>(&source_operation);
        bool source_matches { };
        bool slice_publication { };
        SignalId publication_signal = static_cast<SignalId>(
            signal_layout.signal_id);
        std::size_t publication_offset { };
        if (whole_write != nullptr) {
            source_matches
                = operations.signal(whole_write->signal)
                    == signal_layout.signal_id
                && whole_write->domain
                    == SignalUpdateDomain::systemverilog_active
                && output_binding->offset == 0U
                && output_binding->width == runtime_signal_width;
        } else if (slice_write != nullptr
            && slice_write->domain
                == SignalUpdateDomain::systemverilog_active) {
            const auto source_signal = operations.signal(slice_write->signal);
            if (source_signal == signal_layout.signal_id
                && slice_write->offset == output_binding->offset
                && slice_write->offset <= runtime_signal_width
                && write.width
                    <= runtime_signal_width - slice_write->offset) {
                source_matches = true;
                const bool partial_output
                    = slice_write->offset != 0U
                    || write.width != runtime_signal_width;
                if (partial_output) {
                    bool matching_driver_region { };
                    for (const auto& region
                         : source_program.driver_regions()) {
                        if (region.signal == source_signal
                            && !region.whole
                            && region.offset == slice_write->offset
                            && region.width == write.width) {
                            matching_driver_region = true;
                        }
                    }
                    if (!matching_driver_region) {
                        fail_closed();
                    }
                    slice_publication = true;
                    publication_signal = source_signal;
                    publication_offset = slice_write->offset;
                }
            } else if (output_binding->offset == 0U
                && owner->native_boundary_slice_matches_alias_family(
                           source_signal, slice_write->offset,
                           write.width,
                           static_cast<SignalId>(signal_layout.signal_id))) {
                source_matches = true;
                slice_publication = true;
                publication_signal = source_signal;
                publication_offset = slice_write->offset;
            }
        }
        if (!source_matches) {
            fail_closed();
        }

        const auto word_offset = pending_plane_offsets[pending_slot];
        const auto plane_count
            = region_frontier_required_plane_count_v2(write.value_kind);
        if (word_offset == std::numeric_limits<std::size_t>::max()
            || plane_count == 0U
            || write.plane_count != plane_count
            || word_offset > pending_plane_words.size()
            || write.word_count
                > (pending_plane_words.size() - word_offset) / plane_count) {
            fail_closed();
        }
        for (std::size_t value_plane = 0U;
             value_plane < plane_count; ++value_plane) {
            if (write.value_planes[value_plane]
                != pending_plane_words.data() + word_offset
                    + value_plane * write.word_count) {
                fail_closed();
            }
        }
        std::array<std::span<const std::uint64_t>, 4U> pending_value;
        for (std::size_t value_plane = 0U;
             value_plane < plane_count; ++value_plane) {
            pending_value[value_plane] = {
                write.value_planes[value_plane], write.word_count };
        }
        if (!region_frontier_plane_words_canonical_v2(write.value_kind,
                write.width, write.word_count, pending_value)) {
            fail_closed();
        }

        if ((write.flags & pending_committed) != 0U) {
            return;
        }
        const auto descriptor_member = write.member_index;
        const auto descriptor_signal = write.signal_slot;
        const auto descriptor_source = write.source_instruction;
        const auto descriptor_update_kind = write.update_kind;
        const auto descriptor_flags = write.flags;
        const auto descriptor_width = write.width;
        const auto descriptor_word_count = write.word_count;
        const auto descriptor_value_kind = write.value_kind;
        const auto descriptor_plane_count = write.plane_count;
        const auto descriptor_key = write.commit_key;
        const auto descriptor_origin = write.origin;
        const auto descriptor_value_planes = std::array {
            write.value_planes[0U], write.value_planes[1U],
            write.value_planes[2U], write.value_planes[3U] };
        if (!lease.active()) {
            fail_closed();
        }
        if (writable_signals.empty()) {
            fail_closed();
        }
        bool planes_strictly_ordered = true;
        for (std::size_t slot = 1U; slot < planes.size(); ++slot) {
            if (planes[slot - 1U].signal_id >= planes[slot].signal_id) {
                planes_strictly_ordered = false;
                break;
            }
        }
        for (std::size_t writable_ordinal = 0U;
             writable_ordinal < writable_signals.size();
             ++writable_ordinal) {
            const auto& writable = writable_signals[writable_ordinal];
            const RegionFrontierPlaneV2* writable_plane { };
            if (planes_strictly_ordered) {
                const auto candidate = std::ranges::lower_bound(planes,
                    writable.signal, std::ranges::less { },
                    &RegionFrontierPlaneV2::signal_id);
                if (candidate != planes.end()
                    && candidate->signal_id == writable.signal) {
                    writable_plane = &*candidate;
                }
            } else {
                // Unordered descriptors retain the full uniqueness check.
                for (const auto& candidate : planes) {
                    if (candidate.signal_id != writable.signal) {
                        continue;
                    }
                    if (writable_plane != nullptr) {
                        fail_closed();
                    }
                    writable_plane = &candidate;
                }
            }
            if (writable_plane == nullptr
                || writable_plane->owner_process_id != writable.owner
                || (writable_plane->flags
                        & certified_internal_single_owner)
                    == 0U
                || (writable_plane->flags & read_only_boundary_port) != 0U
                || !frontier_role_matches(lease, writable_ordinal,
                    writable.signal,
                    writable.owner, PackedPlaneRole::current,
                    writable_plane->value_kind,
                    writable_plane->word_count,
                    writable_plane->current_planes)
                || !frontier_role_matches(lease, writable_ordinal,
                    writable.signal,
                    writable.owner, PackedPlaneRole::previous,
                    writable_plane->value_kind,
                    writable_plane->word_count,
                    writable_plane->previous_planes)
                || !frontier_role_matches(lease, writable_ordinal,
                    writable.signal,
                    writable.owner, PackedPlaneRole::stored,
                    writable_plane->value_kind,
                    writable_plane->word_count,
                    writable_plane->stored_planes)
                || !frontier_role_matches(lease, writable_ordinal,
                    writable.signal,
                    writable.owner, PackedPlaneRole::owner,
                    writable_plane->value_kind,
                    writable_plane->word_count,
                    writable_plane->owner_planes)) {
                fail_closed();
            }
        }

        PackedLogic4 value;
        if (write.value_kind == RegionFrontierValueKindV2::logic4) {
            value = PackedLogic4::from_word_planes(write.width,
                pending_value[0U], pending_value[1U]);
        } else {
            value = PackedLogic4::from_logic9_word_planes(write.width,
                pending_value[0U], pending_value[1U], pending_value[2U],
                pending_value[3U]);
        }
        const SignalChangeOrigin origin {
            static_cast<ProcessSchedulingDomain>(
                write.origin.process_domain),
            static_cast<SchedulerPhase>(write.origin.phase)
        };

        const auto can_preserve_selected_member_sync = [&]()
            -> FrontierBoundarySyncRejectReason {
            using Reject = FrontierBoundarySyncRejectReason;
            if (owner == nullptr || !backend_pin || !backend_pin->executor
                || !owner->region_graph) {
                return Reject::runtime_or_backend_missing;
            }
            if (runtime_generation == 0U
                || runtime_generation != owner->region_runtime_generation) {
                return Reject::runtime_generation_stale;
            }
            if (component >= owner->region_frontier_runtime_by_component.size()
                || owner->region_frontier_runtime_by_component[component].get()
                    != this) {
                return Reject::component_runtime_stale;
            }
            if (component >= owner->region_authoritative_state_by_component.size()
                || !authoritative_state || !authoritative_state->valid()
                || owner->region_authoritative_state_by_component[component].get()
                    != authoritative_state.get()) {
                return Reject::authoritative_state_stale;
            }
            if (!owner->region_graph->component_epochs_current(component)) {
                return Reject::component_epoch_stale;
            }
            if (layout.execution_mode
                != RegionFrontierExecutionModeV2::systemverilog_active) {
                return Reject::wrong_execution_mode;
            }
            if (!member_sync_private_entry) {
                return Reject::nonprivate_entry;
            }
            if (!member_sync_workset_available) {
                return Reject::unavailable_workset;
            }
            if (member_sync_force_full) {
                return Reject::prior_force_full;
            }
            if (publication_signal != signal_layout.signal_id) {
                return Reject::publication_signal_mismatch;
            }

            const auto signal_id = static_cast<SignalId>(publication_signal);
            if (signal_id >= owner->signals.size()) {
                return Reject::signal_id_out_of_range;
            }
            if (signal_id >= owner->external_driver_values.size()
                || signal_id >= owner->forced_values.size()
                || signal_id >= owner->forced_masks.size()
                || signal_id >= owner->forced_driver_values.size()
                || signal_id >= owner->forced_driver_masks.size()
                || signal_id >= owner->signal_transaction_observed.size()
                || signal_id >= owner->dynamic_fanout.size()) {
                return Reject::signal_effect_vectors_misaligned;
            }
            if (signal_id >= owner->signal_container_aliases.size()
                || signal_id >= owner->signal_container_element_aliases.size()
                || signal_id >= owner->signal_container_aggregate_aliases.size()) {
                return Reject::container_vectors_misaligned;
            }
            if (signal_id >= owner->signal_writer_counts.size()
                || signal_id >= owner->stable_single_writer_processes.size()
                || signal_id >= owner->driver_values.size()
                || signal_id >= owner->module_path_destination_mask.size()) {
                return Reject::ownership_vectors_misaligned;
            }
            if (owner->native_signal_dependencies_unknown
                || owner->native_signal_dependency_mask.size()
                    != owner->signals.size()
                || owner->native_signal_non_range_dependency_mask.size()
                    != owner->signals.size()) {
                return Reject::dependency_masks_unavailable;
            }
            if (owner->static_fanout_dirty
                || owner->static_fanout_category_spans.size()
                    != owner->signals.size()
                || owner->static_fanout_offsets.size()
                    != owner->signals.size() + 1U) {
                return Reject::static_fanout_index_unavailable;
            }

            const auto& signal = owner->signals[signal_id];
            if (signal.value_kind != ValueKind::logic4) {
                return Reject::wrong_value_kind;
            }
            if (signal.resolution != ResolutionKind::sv_wire) {
                return Reject::wrong_resolution;
            }
            if (signal.initial_value.width() > 64U) {
                return Reject::signal_too_wide;
            }
            if (signal.initial_value.is_logic9()) {
                return Reject::logic9_value;
            }
            if (signal.event_variable) {
                return Reject::event_variable;
            }
            if (signal.has_implicit_driver) {
                return Reject::implicit_driver;
            }
            if (signal.has_charge_strength) {
                return Reject::charge_strength;
            }
            if (signal.systemverilog_scalar
                != SystemVerilogScalarKind::None) {
                return Reject::systemverilog_scalar;
            }
            if (owner->external_driver_values[signal_id]) {
                return Reject::external_driver;
            }
            if (owner->forced_values[signal_id]) {
                return Reject::forced_value;
            }
            if (owner->forced_masks[signal_id]) {
                return Reject::forced_mask;
            }
            if (owner->forced_driver_values[signal_id]) {
                return Reject::forced_driver_value;
            }
            if (owner->forced_driver_masks[signal_id]) {
                return Reject::forced_driver_mask;
            }
            if (owner->signal_transaction_observed[signal_id]) {
                return Reject::transaction_observed;
            }
            if (!owner->dynamic_fanout[signal_id].empty()) {
                return Reject::dynamic_fanout;
            }
            if (!owner->signal_container_aliases[signal_id].empty()) {
                return Reject::container_alias;
            }
            if (owner->signal_container_element_aliases[signal_id]) {
                return Reject::container_element_alias;
            }
            if (owner->signal_container_aggregate_aliases[signal_id]) {
                return Reject::container_aggregate_alias;
            }
            const auto static_writer_count
                = owner->signal_writer_counts[signal_id];
            const bool multiowner_slice
                = slice_publication && static_writer_count > 1U;
            if (static_writer_count != 1U && !multiowner_slice) {
                return Reject::writer_count;
            }
            const auto no_stable_writer
                = std::numeric_limits<ProcessId>::max();
            if (multiowner_slice) {
                if (owner->stable_single_writer_processes[signal_id]
                    != no_stable_writer) {
                    return Reject::stable_writer_mismatch;
                }
            } else if (owner->stable_single_writer_processes[signal_id]
                != process) {
                return Reject::stable_writer_mismatch;
            }
            if (owner->module_path_destination_mask[signal_id] != 0U) {
                return Reject::module_path_destination;
            }
            if (owner->native_signal_non_range_dependency_mask[signal_id] != 0U) {
                return Reject::nonrange_dependency;
            }
            if (owner->requires_sampled_values) {
                if (owner->sampled_value_dependencies_unknown) {
                    return Reject::sampled_values_unknown;
                }
                if (owner->sampled_value_dependency_mask.size()
                    != owner->signals.size()) {
                    return Reject::sampled_mask_unavailable;
                }
                if (owner->sampled_value_dependency_mask[signal_id] != 0U) {
                    return Reject::sampled_dependency;
                }
            }
            if (owner->monitor_watches(signal_id)) {
                return Reject::monitor_watch;
            }
            if (owner->has_bidirectional_switches) {
                return Reject::bidirectional_switches;
            }

            for (const auto& sensitivity
                 : owner->static_fanout_for(signal_id)) {
                if (sensitivity.process >= owner->processes.size()) {
                    return Reject::fanout_process_out_of_range;
                }
                const auto* const state
                    = owner->processes.full_state_if_present(
                        sensitivity.process);
                if (state && state->executor
                    && dynamic_cast<const detail::BuiltinProcessExecutorCapability*>(
                        state->executor.get()) == nullptr) {
                    return Reject::unsealed_fanout_executor;
                }
            }

            const auto& drivers = owner->driver_values[signal_id];
            if (multiowner_slice) {
                if (owner->owned_driver_active(signal_id)) {
                    return Reject::active_owned_driver;
                }
                if (!owner->process_signal_access_inventory_complete
                    || !owner->region_graph
                    || !owner->region_graph->certificate_inventory()
                            .access_inventory_complete) {
                    return Reject::writer_graph_unavailable;
                }
                const auto graph_signals
                    = owner->region_graph->signals();
                const auto graph_processes
                    = owner->region_graph->processes();
                if (signal_id >= graph_signals.size()
                    || graph_processes.size() != owner->processes.size()) {
                    return Reject::writer_graph_unavailable;
                }
                const auto& graph_signal = graph_signals[signal_id];
                if (graph_signal.writers_unknown
                    || graph_signal.dynamic_fork_writers
                    || (graph_signal.drivers != RegionDriverClass::resolved
                        && graph_signal.drivers
                            != RegionDriverClass::disjoint_partial)
                    || graph_signal.descriptor.width
                        != signal.initial_value.width()
                    || graph_signal.descriptor.value_kind != signal.value_kind
                    || graph_signal.descriptor.resolution != signal.resolution
                    || graph_signal.descriptor.implicit_driver
                        != signal.has_implicit_driver
                    || graph_signal.descriptor.event_variable
                        != signal.event_variable) {
                    return Reject::writer_graph_shape;
                }
                if (drivers.size() != static_writer_count) {
                    return Reject::driver_count;
                }
                const auto* const current_state
                    = owner->processes.full_state_if_present(process);
                if (current_state == nullptr || !current_state->executor
                    || dynamic_cast<const detail::BuiltinProcessExecutorCapability*>(
                        current_state->executor.get()) == nullptr) {
                    return Reject::unsealed_current_writer;
                }

                const auto& graph_writers = graph_signal.writers;
                std::size_t graph_owner_count { };
                ProcessId previous_owner { };
                bool have_previous_owner { };
                for (const auto& writer : graph_writers) {
                    if (writer.process >= graph_processes.size()
                        || (have_previous_owner
                            && writer.process < previous_owner)) {
                        return Reject::writer_graph_shape;
                    }
                    if (!have_previous_owner
                        || writer.process != previous_owner) {
                        ++graph_owner_count;
                        previous_owner = writer.process;
                        have_previous_owner = true;
                    }
                }
                if (graph_owner_count != static_writer_count
                    || graph_owner_count != drivers.size()) {
                    return Reject::driver_owner_set_mismatch;
                }

                std::size_t graph_writer_index { };
                bool owner_set_matches { true };
                bool current_owner_present { };
                bool nondefault_strength { };
                bool scalar_driver_regions { };
                drivers.for_each_in_process_order(
                    [&](const DriverRecord& record) {
                        while (graph_writer_index < graph_writers.size()
                            && graph_writers[graph_writer_index].process
                                < record.process) {
                            const auto missing_owner
                                = graph_writers[graph_writer_index].process;
                            owner_set_matches = false;
                            do {
                                ++graph_writer_index;
                            } while (graph_writer_index < graph_writers.size()
                                && graph_writers[graph_writer_index].process
                                    == missing_owner);
                        }
                        if (graph_writer_index >= graph_writers.size()
                            || graph_writers[graph_writer_index].process
                                != record.process) {
                            owner_set_matches = false;
                        } else {
                            current_owner_present |= record.process == process;
                            do {
                                ++graph_writer_index;
                            } while (graph_writer_index < graph_writers.size()
                                && graph_writers[graph_writer_index].process
                                    == record.process);
                        }
                        nondefault_strength |= record.strength
                            != DriveStrength { };
                        scalar_driver_regions |= static_cast<bool>(
                            record.scalar_regions);
                    });
                owner_set_matches &= graph_writer_index == graph_writers.size();
                if (nondefault_strength) {
                    return Reject::driver_strength;
                }
                if (scalar_driver_regions) {
                    return Reject::scalar_driver_regions;
                }
                if (!owner_set_matches) {
                    return Reject::driver_owner_set_mismatch;
                }
                if (!current_owner_present) {
                    return Reject::driver_record_missing;
                }
            } else {
                if (drivers.size() != 1U) {
                    return Reject::driver_count;
                }
                const auto* const driver = drivers.find(process);
                if (driver == nullptr) {
                    return Reject::driver_record_missing;
                }
                if (driver->strength != DriveStrength { }) {
                    return Reject::driver_strength;
                }
                if (driver->scalar_regions) {
                    return Reject::scalar_driver_regions;
                }
            }
            const auto program = owner->processes.program_view(process);
            if (program.switch_source()) {
                return Reject::process_switch_source;
            }
            if (program.switch_target()) {
                return Reject::process_switch_target;
            }
            if (program.switch_bidirectional()) {
                return Reject::process_bidirectional_switch;
            }

            if (owner->process_profile_enabled) {
                return Reject::process_profile_enabled;
            }
            if (owner->execution_point_hook) {
                return Reject::execution_point_hook;
            }
            if (owner->scheduler.trace_hook_installed()) {
                return Reject::scheduler_trace_hook;
            }
            if (owner->driver_change_hook) {
                return Reject::driver_change_hook;
            }
            if (owner->signal_change_hook) {
                return Reject::signal_change_hook;
            }
            if (owner->stored_signal_change_hook) {
                return Reject::stored_signal_change_hook;
            }
            if (owner->scalar_signal_change_hook) {
                return Reject::scalar_signal_change_hook;
            }
            if (owner->container_object_change_hook) {
                return Reject::container_object_change_hook;
            }
            if (owner->container_element_change_hook) {
                return Reject::container_element_change_hook;
            }
            if (owner->native_signal_observation_any_hook) {
                return Reject::native_observation_any_hook;
            }
            if (owner->native_signal_observation_required_hook) {
                return Reject::native_observation_required_hook;
            }

            return Reject::admitted;
        };
        const auto boundary_sync_decision
            = can_preserve_selected_member_sync();
        const bool preserve_selected_member_sync
            = boundary_sync_decision
                == FrontierBoundarySyncRejectReason::admitted;
        if (owner->systemverilog_wave_profile_enabled) {
            if (preserve_selected_member_sync) {
                ++owner->systemverilog_wave_profile_boundary_sync_eligible;
            } else {
                ++owner->systemverilog_wave_profile_boundary_sync_conservative;
                const auto reject_index = static_cast<std::size_t>(
                    boundary_sync_decision);
                if (reject_index != 0U
                    && reject_index
                        < owner->systemverilog_wave_profile_boundary_sync_first_rejects
                              .size()) {
                    ++owner->systemverilog_wave_profile_boundary_sync_first_rejects[
                        reject_index];
                }
                if (boundary_sync_decision
                    == FrontierBoundarySyncRejectReason::writer_count) {
                    const auto signal_id = publication_signal;
                    const auto static_count
                        = owner->signal_writer_counts[signal_id];
                    const auto& drivers = owner->driver_values[signal_id];
                    const auto table_size = drivers.size();
                    const auto static_bucket = static_count == 0U ? 0U : 1U;
                    const auto table_bucket = table_size == 0U
                        ? 0U : table_size == 1U ? 1U : 2U;
                    const auto counts_equal = table_size == static_count;
                    const auto current_present = drivers.find(process) != nullptr;
                    const auto publication_bucket = slice_publication ? 1U : 0U;
                    std::size_t histogram_index = static_bucket;
                    histogram_index = histogram_index * 3U + table_bucket;
                    histogram_index = histogram_index * 2U
                        + static_cast<std::size_t>(counts_equal);
                    histogram_index = histogram_index * 2U
                        + static_cast<std::size_t>(current_present);
                    histogram_index = histogram_index * 2U
                        + publication_bucket;
                    ++owner->systemverilog_wave_profile_boundary_sync_writer_inventory[
                        histogram_index];
                }
            }
        }

        // Checked commit remains responsible for value resolution, events,
        // publication, and exact fanout. The selected journal omits only the
        // redundant boundary full-sync cause when the target and all static
        // fanout executors have the current private proof. Existing trigger
        // masks are OR-idempotent; newly queued readiness is receipt-imported.
        end_alias_bind_lease();
        if (!preserve_selected_member_sync) {
            require_full_member_sync(
                FrontierMemberSyncFullReason::boundary_publication);
        }
        lease.release();
        boundary_callback_started_slot = pending_slot;
        try {
            if (slice_publication) {
                owner->commit_driver_slice(process, publication_signal,
                    std::move(value), publication_offset, origin, false);
            } else {
                owner->commit_driver(process,
                    static_cast<SignalId>(signal_layout.signal_id),
                    std::move(value), origin, false);
            }
        } catch (...) {
            // The checked path can throw after a raw-owner or current-value
            // side effect. Keep the original pending words for diagnosis but
            // fail-stop the frame so neither its body nor this commit replays.
            invalidate();
            throw;
        }

        if (backend != backend_pin || pending_slot >= pending_writes.size()
            || frame.pending_writes != pending_writes.data()
            || frame.current_pending_write != pending_slot) {
            fail_closed();
        }
        auto& acknowledged_write = pending_writes[pending_slot];
        if (acknowledged_write.member_index != descriptor_member
            || acknowledged_write.signal_slot != descriptor_signal
            || acknowledged_write.source_instruction != descriptor_source
            || acknowledged_write.update_kind != descriptor_update_kind
            || acknowledged_write.flags != descriptor_flags
            || acknowledged_write.width != descriptor_width
            || acknowledged_write.word_count != descriptor_word_count
            || acknowledged_write.value_kind != descriptor_value_kind
            || acknowledged_write.plane_count != descriptor_plane_count
            || !std::ranges::equal(acknowledged_write.value_planes,
                descriptor_value_planes)
            || !same_frontier_key(
                acknowledged_write.commit_key, descriptor_key)
            || !same_frontier_key(acknowledged_write.origin,
                descriptor_origin)
            || (acknowledged_write.flags & pending_committed) != 0U) {
            fail_closed();
        }
        acknowledged_write.flags |= pending_committed;
        boundary_callback_started_slot
            = std::numeric_limits<std::uint32_t>::max();
    } catch (...) {
        invalidate();
        throw;
    }
}

} // namespace fsim::runtime::simir
