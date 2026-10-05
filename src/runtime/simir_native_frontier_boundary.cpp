// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

namespace {

[[nodiscard]] bool frontier_role_matches(
    const AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const SignalId signal,
    const ProcessId owner,
    const PackedPlaneRole role,
    const RegionFrontierValueKindV2 kind,
    const std::size_t word_count,
    std::span<std::uint64_t* const, 4U> bound) noexcept
{
    std::array<std::span<std::uint64_t>, 4U> words;
    const auto plane_count = region_frontier_required_plane_count_v2(kind);
    if (plane_count == 0U || !lease.plane_words(signal, role, owner, words)) {
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

        const auto process_program = owner->get_process(process).program();
        const auto& operations = process_program.operations();
        if (write.source_instruction >= operations.size()) {
            fail_closed();
        }
        const auto source_operation
            = operations.expanded(write.source_instruction);
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
                = whole_write->signal == signal_layout.signal_id
                && whole_write->domain
                    == SignalUpdateDomain::systemverilog_active
                && output_binding->offset == 0U
                && output_binding->width == runtime_signal_width;
        } else if (slice_write != nullptr
            && slice_write->domain
                == SignalUpdateDomain::systemverilog_active) {
            if (slice_write->signal == signal_layout.signal_id
                && slice_write->offset == output_binding->offset
                && slice_write->offset <= runtime_signal_width
                && write.width
                    <= runtime_signal_width - slice_write->offset) {
                source_matches = true;
                const bool partial_output
                    = slice_write->offset != 0U
                    || write.width != runtime_signal_width;
                if (partial_output) {
                    const auto source_program
                        = owner->processes.program_view(process);
                    bool matching_driver_region { };
                    for (const auto& region
                         : source_program.driver_regions()) {
                        if (region.signal == slice_write->signal
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
                    publication_signal = slice_write->signal;
                    publication_offset = slice_write->offset;
                }
            } else if (output_binding->offset == 0U
                && owner->native_boundary_slice_matches_alias_family(
                           slice_write->signal, slice_write->offset,
                           write.width,
                           static_cast<SignalId>(signal_layout.signal_id))) {
                source_matches = true;
                slice_publication = true;
                publication_signal = slice_write->signal;
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
        for (const auto& writable : writable_signals) {
            const RegionFrontierPlaneV2* writable_plane { };
            for (const auto& candidate : planes) {
                if (candidate.signal_id != writable.signal) {
                    continue;
                }
                if (writable_plane != nullptr) {
                    fail_closed();
                }
                writable_plane = &candidate;
            }
            if (writable_plane == nullptr
                || writable_plane->owner_process_id != writable.owner
                || (writable_plane->flags
                        & certified_internal_single_owner)
                    == 0U
                || (writable_plane->flags & read_only_boundary_port) != 0U
                || !frontier_role_matches(lease, writable.signal,
                    writable.owner, PackedPlaneRole::current,
                    writable_plane->value_kind,
                    writable_plane->word_count,
                    writable_plane->current_planes)
                || !frontier_role_matches(lease, writable.signal,
                    writable.owner, PackedPlaneRole::previous,
                    writable_plane->value_kind,
                    writable_plane->word_count,
                    writable_plane->previous_planes)
                || !frontier_role_matches(lease, writable.signal,
                    writable.owner, PackedPlaneRole::stored,
                    writable_plane->value_kind,
                    writable_plane->word_count,
                    writable_plane->stored_planes)
                || !frontier_role_matches(lease, writable.signal,
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

        // Checked commit may invoke user observers and detach authoritative
        // roles, so no A4 write lease may remain active across this call.
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
