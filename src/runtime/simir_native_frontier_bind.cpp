// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

namespace {

[[nodiscard]] bool bind_writable_role(
    const AuthoritativeSignalPlanes::FrontierWriteLease& lease,
    const SignalId signal,
    const ProcessId process,
    const PackedPlaneRole role,
    const RegionFrontierValueKindV2 kind,
    const std::size_t word_count,
    std::span<std::uint64_t*, 4U> bound) noexcept
{
    std::array<std::span<std::uint64_t>, 4U> words;
    const auto plane_count = region_frontier_required_plane_count_v2(kind);
    if (plane_count == 0U || !lease.plane_words(signal, role, process, words)) {
        return false;
    }
    for (std::size_t plane = 0U; plane < words.size(); ++plane) {
        if (plane < plane_count) {
            if (words[plane].size() != word_count
                || words[plane].data() == nullptr) {
                return false;
            }
            bound[plane] = words[plane].data();
        } else if (!words[plane].empty()) {
            return false;
        }
    }
    return true;
}

} // namespace

bool Interpreter::Impl::bind_generic_frontier_read_snapshots(
    RegionFrontierComponentRuntime& runtime)
{
    if (runtime.owner != this || !runtime.frame_initialized
        || runtime.invalidated || !runtime.backend
        || !runtime.backend->executor
        || runtime.backend->executor->layout().execution_mode
            != RegionFrontierExecutionModeV2::generic_deferred_update
        || !region_graph || runtime.runtime_generation == 0U
        || runtime.runtime_generation != region_runtime_generation
        || runtime.component
            >= region_graph->certificate_inventory().components.size()
        || !region_graph->component_epochs_current(runtime.component)) {
        return false;
    }

    const auto& layout = runtime.backend->executor->layout();
    auto& frame = runtime.frame;
    if (!region_frontier_layout_header_valid_v2(layout)
        || layout.execution_mode
            != RegionFrontierExecutionModeV2::generic_deferred_update
        || !region_frontier_frame_header_valid_v2(frame)
        || frame.generic_update_ack_count != 0U
        || layout.metadata_count != 0U || frame.metadata_count != 0U
        || frame.metadata != nullptr
        || layout.signal_slot_count != runtime.planes.size()
        || frame.signal_slot_count != runtime.planes.size()
        || frame.planes != runtime.planes.data()
        || frame.port_planes != runtime.port_planes.data()
        || runtime.port_planes.size() != runtime.planes.size()
        || runtime.generic_snapshot_plane_offsets.size()
            != runtime.planes.size()
        || layout.signals == nullptr) {
        return false;
    }

    for (std::size_t slot = 0U; slot < runtime.planes.size(); ++slot) {
        const auto& descriptor = layout.signals[slot];
        const auto signal_id = static_cast<SignalId>(descriptor.signal_id);
        if (descriptor.flags
                != RegionFrontierPlaneFlagsV2::read_only_boundary_port
            || descriptor.metadata_index != UINT32_MAX
            || descriptor.width == 0U
            || descriptor.word_count
                != (static_cast<std::uint64_t>(descriptor.width) + 63U) / 64U
            || descriptor.plane_count
                != region_frontier_required_plane_count_v2(
                    descriptor.value_kind)
            || signal_id >= signals.size()
            || signal_id >= region_graph->signals().size()
            || (signals[signal_id].value_kind != ValueKind::logic4
                && signals[signal_id].value_kind != ValueKind::logic9)
            || descriptor.value_kind
                != region_frontier_value_kind_v2(
                    signals[signal_id].value_kind)
            || signals[signal_id].initial_value.width() != descriptor.width
            || region_graph->signals()[signal_id].descriptor.value_kind
                != signals[signal_id].value_kind
            || region_graph->signals()[signal_id].descriptor.width
                != descriptor.width
            || !region_frontier_value_shape_valid_v2(
                descriptor.value_kind, descriptor.width,
                descriptor.word_count, descriptor.plane_count)) {
            return false;
        }

        const bool internal = std::ranges::find(
            runtime.backend->kernel.internal_signals, signal_id)
            != runtime.backend->kernel.internal_signals.end();
        if (internal) {
            const auto output = std::ranges::find(
                runtime.backend->kernel.outputs, signal_id,
                &RegionConeOutputBinding::signal);
            if (output == runtime.backend->kernel.outputs.end()
                || std::ranges::count(runtime.backend->kernel.outputs,
                       signal_id, &RegionConeOutputBinding::signal) != 1
                || output->owner != descriptor.owner_process_id
                || output->offset != 0U
                || output->width != descriptor.width
                || output->value_kind != signals[signal_id].value_kind
                || output->domain != SignalUpdateDomain::generic
                || output->update_kind != RegionUpdateKind::generic) {
                return false;
            }
        } else if (descriptor.owner_process_id != UINT32_MAX) {
            return false;
        }

        const auto& source = logical_signal_value(signal_id);
        const bool logic9
            = descriptor.value_kind == RegionFrontierValueKindV2::logic9;
        if (source.width() != descriptor.width
            || source.is_logic9() != logic9) {
            return false;
        }
        const auto offset = runtime.generic_snapshot_plane_offsets[slot];
        const auto word_count = static_cast<std::size_t>(descriptor.word_count);
        const auto plane_count = static_cast<std::size_t>(descriptor.plane_count);
        if (plane_count == 0U
            || word_count > std::numeric_limits<std::size_t>::max()
                / plane_count) {
            return false;
        }
        const auto required_words = word_count * plane_count;
        if (offset > runtime.generic_snapshot_words.size()
            || required_words
                > runtime.generic_snapshot_words.size() - offset) {
            return false;
        }
        std::array<std::span<const std::uint64_t>, 4U> copied { };
        std::array<std::uint64_t*, 4U> destinations { };
        for (std::size_t value_plane = 0U;
             value_plane < plane_count; ++value_plane) {
            const auto source_words = logic9
                ? source.logic9_plane_words(value_plane)
                : value_plane == 0U ? source.aval_words()
                                    : source.bval_words();
            if (source_words.size() != word_count) {
                return false;
            }
            auto* const destination = runtime.generic_snapshot_words.data()
                + offset + value_plane * word_count;
            std::ranges::copy(source_words, destination);
            copied[value_plane] = std::span<const std::uint64_t> {
                destination, word_count };
            destinations[value_plane] = destination;
        }
        if (!region_frontier_plane_words_canonical_v2(
                descriptor.value_kind, descriptor.width,
                descriptor.word_count, copied)) {
            return false;
        }

        auto& plane = runtime.planes[slot];
        plane = { };
        plane.signal_id = descriptor.signal_id;
        plane.owner_process_id = descriptor.owner_process_id;
        plane.value_kind = descriptor.value_kind;
        plane.width = descriptor.width;
        plane.word_count = descriptor.word_count;
        plane.plane_count = descriptor.plane_count;
        plane.flags = descriptor.flags;
        plane.metadata_index = descriptor.metadata_index;
        for (std::size_t value_plane = 0U;
             value_plane < plane_count; ++value_plane) {
            plane.boundary_planes[value_plane]
                = destinations[value_plane];
        }
        runtime.port_planes[slot] = &plane;
    }
    return true;
}

bool Interpreter::Impl::RegionFrontierComponentRuntime::
    bind_frame_planes_and_metadata(
        AuthoritativeSignalPlanes::FrontierWriteLease& lease) noexcept
{
    if (owner == nullptr || !frame_initialized || invalidated
        || !lease.active() || !backend || !backend->executor
        || !authoritative_state || !authoritative_state->valid()
        || runtime_generation == 0U
        || authoritative_state->generation() != runtime_generation
        || runtime_generation != owner->region_runtime_generation
        || !owner->region_graph
        || !owner->region_graph->component_epochs_current(component)
        || component >= owner->region_authoritative_state_by_component.size()
        || owner->region_authoritative_state_by_component[component]
            != authoritative_state
        || !region_frontier_frame_header_valid_v2(frame)
        || frame.generic_update_ack_count != 0U
        || frame.runtime_generation != runtime_generation
        || frame.bound_runtime_generation != runtime_generation
        || frame.committed_signal_count != 0U
        || frame.planes != planes.data()
        || frame.signal_slot_count != planes.size()
        || frame.port_planes != port_planes.data()
        || port_planes.size() != planes.size()
        || frame.metadata_count != metadata.size()
        || frame.metadata != (metadata.empty() ? nullptr : metadata.data())) {
        return false;
    }

    const auto& layout = backend->executor->layout();
    if (layout.abi_version != kRegionFrontierAbiVersionV2
        || !region_frontier_layout_header_valid_v2(layout)
        || layout.reserved0 != 0U || layout.reserved_capacity != 0U
        || layout.execution_mode != RegionFrontierExecutionModeV2::systemverilog_active
        || layout.certificate_generation != frame.certificate_generation
        || layout.component_generation != frame.component_generation
        || layout.signals == nullptr || layout.signal_slot_count != planes.size()
        || layout.metadata_count != metadata.size()
        || writable_signals.size() != metadata.size()) {
        return false;
    }

    // Binding changes only runtime-owned descriptors. A rejected binding has
    // no signal effects, and the caller must not invoke the entry on failure.
    // All boundary borrows expire at the next checked callback: this helper
    // must run again before another generated entry can read those pointers.
    std::size_t internal_index { };
    for (std::size_t slot = 0U; slot < planes.size(); ++slot) {
        const auto& descriptor = layout.signals[slot];
        const auto signal_id = static_cast<SignalId>(descriptor.signal_id);
        if (signal_id >= owner->signals.size()
            || descriptor.width == 0U
            || descriptor.word_count
                != (static_cast<std::size_t>(descriptor.width) + 63U) / 64U
            || (slot != 0U
                && layout.signals[slot - 1U].signal_id
                    >= descriptor.signal_id)) {
            return false;
        }
        // get_signal() can materialize a pending packed mirror. The binder
        // must read the raw slot and authoritative dense planes instead.
        const auto& signal = owner->signals[signal_id];
        if ((signal.value_kind != ValueKind::logic4
                && signal.value_kind != ValueKind::logic9)
            || region_frontier_value_kind_v2(signal.value_kind)
                != descriptor.value_kind
            || signal.initial_value.is_logic9()
                != (descriptor.value_kind == RegionFrontierValueKindV2::logic9)
            || signal.initial_value.width() != descriptor.width
            || descriptor.plane_count
                != region_frontier_required_plane_count_v2(
                    descriptor.value_kind)
            || !region_frontier_value_shape_valid_v2(
                descriptor.value_kind, descriptor.width,
                descriptor.word_count, descriptor.plane_count)) {
            return false;
        }

        RegionFrontierPlaneV2 bound;
        bound.signal_id = descriptor.signal_id;
        bound.owner_process_id = descriptor.owner_process_id;
        bound.value_kind = descriptor.value_kind;
        bound.width = descriptor.width;
        bound.word_count = descriptor.word_count;
        bound.plane_count = descriptor.plane_count;
        bound.flags = descriptor.flags;
        bound.metadata_index = descriptor.metadata_index;
        if (descriptor.flags == certified_internal_single_owner) {
            if (internal_index >= writable_signals.size()
                || descriptor.metadata_index != internal_index
                || writable_signals[internal_index].signal != signal_id
                || writable_signals[internal_index].owner
                    != descriptor.owner_process_id
                || signal_id
                    >= owner->region_authoritative_component_by_signal.size()
                || owner->region_authoritative_component_by_signal[signal_id]
                    != component
                || signal_id >= owner->signal_events.size()
                || signal_id >= owner->signal_event_scheduling_stamps.size()
                || signal_id >= owner->signal_transactions.size()
                || signal_id >= owner->signal_value_revisions.size()
                || !bind_writable_role(lease, signal_id,
                    descriptor.owner_process_id, PackedPlaneRole::current,
                    descriptor.value_kind, descriptor.word_count,
                    bound.current_planes)
                || !bind_writable_role(lease, signal_id,
                    descriptor.owner_process_id, PackedPlaneRole::previous,
                    descriptor.value_kind, descriptor.word_count,
                    bound.previous_planes)
                || !bind_writable_role(lease, signal_id,
                    descriptor.owner_process_id, PackedPlaneRole::stored,
                    descriptor.value_kind, descriptor.word_count,
                    bound.stored_planes)
                || !bind_writable_role(lease, signal_id,
                    descriptor.owner_process_id, PackedPlaneRole::owner,
                    descriptor.value_kind, descriptor.word_count,
                    bound.owner_planes)) {
                return false;
            }

            RegionFrontierSignalMetadataV2 copied;
            const auto& event = owner->signal_events[signal_id];
            const auto& transaction = owner->signal_transactions[signal_id];
            if (event) {
                const auto& stamp
                    = owner->signal_event_scheduling_stamps[signal_id];
                copied.event_time = event->first;
                copied.event_delta = event->second;
                copied.systemverilog_round = stamp.systemverilog_round;
                copied.event_process_domain
                    = static_cast<std::uint32_t>(stamp.origin.process_domain);
                copied.event_phase
                    = static_cast<std::uint32_t>(stamp.origin.phase);
                copied.event_valid = 1U;
            }
            if (transaction) {
                copied.transaction_time = transaction->first;
                copied.transaction_delta = transaction->second;
                copied.transaction_valid = 1U;
            }
            copied.value_revision = owner->signal_value_revisions[signal_id];
            metadata[internal_index++] = copied;
        } else if (descriptor.flags == read_only_boundary_port) {
            if (descriptor.metadata_index != UINT32_MAX
                || descriptor.owner_process_id != UINT32_MAX
                || signal_id >= owner->direct_signal_materialization_pending.size()) {
                return false;
            }
            std::array<std::span<const std::uint64_t>, 4U> boundary;
            const auto plane_count = descriptor.plane_count;
            if (owner->direct_signal_materialization_pending[signal_id] != 0U) {
                if (signal_id >= owner->direct_wide_signal_offsets.size()) {
                    return false;
                }
                const auto offset = static_cast<std::size_t>(
                    owner->direct_wide_signal_offsets[signal_id]);
                if (offset > owner->direct_wide_signal_aval.size()
                    || offset > owner->direct_wide_signal_bval.size()
                    || descriptor.word_count
                        > owner->direct_wide_signal_aval.size() - offset
                    || descriptor.word_count
                        > owner->direct_wide_signal_bval.size() - offset) {
                    return false;
                }
                boundary[0U] = std::span<const std::uint64_t> {
                    owner->direct_wide_signal_aval }.subspan(
                    offset, descriptor.word_count);
                boundary[1U] = std::span<const std::uint64_t> {
                    owner->direct_wide_signal_bval }.subspan(
                    offset, descriptor.word_count);
                if (plane_count == 4U) {
                    if (offset > owner->direct_wide_signal_logic9_plane2.size()
                        || offset > owner->direct_wide_signal_logic9_plane3.size()
                        || descriptor.word_count
                            > owner->direct_wide_signal_logic9_plane2.size() - offset
                        || descriptor.word_count
                            > owner->direct_wide_signal_logic9_plane3.size() - offset) {
                        return false;
                    }
                    boundary[2U] = std::span<const std::uint64_t> {
                        owner->direct_wide_signal_logic9_plane2 }.subspan(
                        offset, descriptor.word_count);
                    boundary[3U] = std::span<const std::uint64_t> {
                        owner->direct_wide_signal_logic9_plane3 }.subspan(
                        offset, descriptor.word_count);
                }
            } else if (plane_count == 2U) {
                boundary[0U] = signal.initial_value.aval_words();
                boundary[1U] = signal.initial_value.bval_words();
            } else if (plane_count == 4U) {
                for (std::size_t plane_index = 0U;
                     plane_index < plane_count; ++plane_index) {
                    boundary[plane_index]
                        = signal.initial_value.logic9_plane_words(plane_index);
                }
            } else {
                return false;
            }
            if (!region_frontier_plane_words_canonical_v2(
                    descriptor.value_kind, descriptor.width,
                    descriptor.word_count, boundary)) {
                return false;
            }
            for (std::size_t plane_index = 0U;
                 plane_index < plane_count; ++plane_index) {
                bound.boundary_planes[plane_index]
                    = boundary[plane_index].data();
            }
        } else {
            return false;
        }
        if (!region_frontier_plane_bindings_valid_v2(bound)) {
            return false;
        }
        planes[slot] = bound;
        port_planes[slot] = &planes[slot];
    }
    return internal_index == metadata.size();
}

} // namespace fsim::runtime::simir
