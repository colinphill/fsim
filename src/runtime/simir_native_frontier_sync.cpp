// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <cstdio>

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

} // namespace

void Interpreter::Impl::RegionFrontierComponentRuntime::invalidate(
    const std::source_location caller) noexcept
{
    const bool first_transition = !invalidated;
    invalidated = true;
    clear_alias_certificate();
    if (!first_transition || owner == nullptr
        || !owner->systemverilog_wave_profile_enabled) {
        return;
    }
    std::fprintf(stderr,
        "fsim-profile: sv-region-recertification event=frontier-invalidation "
        "component=%zu runtime_generation=%llu caller='%s:%u' "
        "function='%s'\n",
        component, static_cast<unsigned long long>(runtime_generation),
        caller.file_name(), caller.line(), caller.function_name());
}

void Interpreter::Impl::RegionFrontierComponentRuntime::
    synchronize_committed_state(
        AuthoritativeSignalPlanes::FrontierWriteLease& lease) noexcept
{
    auto invalidate_local_cache = [this]() noexcept {
        if (owner == nullptr
            || component >= owner->region_local_wave_state_by_component.size()) {
            return;
        }
        const auto& local
            = owner->region_local_wave_state_by_component[component];
        if (!local || local->generation != runtime_generation) {
            return;
        }
        local->seeded = false;
        if (runtime_generation == owner->region_runtime_generation
            && component
                < owner->region_authoritative_state_by_component.size()) {
            const auto& authoritative
                = owner->region_authoritative_state_by_component[component];
            if (authoritative && authoritative_state
                && authoritative.get() == authoritative_state.get()
                && authoritative->valid()
                && authoritative->generation() == runtime_generation) {
                local->authoritative_revision
                    = authoritative->values().revision();
                return;
            }
        }
        local->authoritative_revision = 0U;
    };
    auto fail_closed = [this](const std::source_location caller
                                 = std::source_location::current()) noexcept {
        invalidate(caller);
    };

    const auto committed_count
        = static_cast<std::size_t>(frame.committed_signal_count);
    if (committed_count == 0U) {
        return;
    }

    if (owner == nullptr) {
        fail_closed();
        return;
    }
    auto& state = *owner;
    auto& native_frame = frame;
    if (!lease.active() || backend == nullptr
        || backend->executor == nullptr
        || !region_frontier_frame_header_valid_v2(native_frame)
        || native_frame.generic_update_ack_count != 0U
        || native_frame.committed_signal_count
            > native_frame.committed_signal_capacity
        || native_frame.committed_signal_count > committed_signals.size()
        || native_frame.committed_signals != committed_signals.data()
        || native_frame.signal_slot_count != planes.size()
        || native_frame.planes != planes.data()
        || native_frame.metadata_count != metadata.size()
        || native_frame.metadata != metadata.data()) {
        invalidate_local_cache();
        fail_closed();
        return;
    }

    const auto& layout = backend->executor->layout();
    if (!region_frontier_layout_header_valid_v2(layout)
        || layout.reserved0 != 0U || layout.reserved_capacity != 0U
        || layout.execution_mode != RegionFrontierExecutionModeV2::systemverilog_active
        || native_frame.certificate_generation
            != layout.certificate_generation
        || native_frame.component_generation != layout.component_generation
        || layout.signal_slot_count != planes.size()
        || layout.signals == nullptr
        || layout.metadata_count != metadata.size()
        || layout.member_count != members.size()
        || layout.write_site_count != pending_writes.size()) {
        invalidate_local_cache();
        fail_closed();
        return;
    }

    const auto signal_count = state.signals.size();
    if (state.signal_events.size() < signal_count
        || state.signal_event_scheduling_stamps.size() < signal_count
        || state.signal_transactions.size() < signal_count
        || state.signal_value_revisions.size() < signal_count
        || state.direct_signal_materialization_pending.size() < signal_count
        || state.direct_signal_aval.size() < signal_count
        || state.direct_signal_bval.size() < signal_count
        || state.direct_signal_last_aval.size() < signal_count
        || state.direct_signal_last_bval.size() < signal_count
        || state.direct_wide_signal_offsets.size() < signal_count
        || state.direct_wide_signal_aval.size()
            != state.direct_wide_signal_bval.size()) {
        invalidate_local_cache();
        fail_closed();
        return;
    }

    // Validate the entire ordered log before publishing any host bookkeeping.
    // The generated entry and runtime builder already establish these bounds;
    // this second check keeps a malformed frame from partially syncing a log.
    bool any_state_changed { };
    for (std::size_t index = 0U; index < committed_count; ++index) {
        const auto& committed = committed_signals[index];
        if (committed.changed > 1U || committed.state_changed > 1U
            || (committed.changed != 0U && committed.state_changed == 0U)
            || committed.signal_slot >= native_frame.signal_slot_count) {
            invalidate_local_cache();
            fail_closed();
            return;
        }

        const auto& plane = planes[committed.signal_slot];
        const auto& layout_signal = layout.signals[committed.signal_slot];
        if (layout_signal.signal_id != plane.signal_id
            || layout_signal.owner_process_id != plane.owner_process_id
            || layout_signal.value_kind != plane.value_kind
            || layout_signal.width != plane.width
            || layout_signal.word_count != plane.word_count
            || layout_signal.plane_count != plane.plane_count
            || layout_signal.flags != plane.flags
            || layout_signal.metadata_index != plane.metadata_index
            || plane.flags
                != static_cast<std::uint32_t>(
                    RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
            || plane.metadata_index >= native_frame.metadata_count
            || plane.metadata_index >= metadata.size()
            || !region_frontier_plane_bindings_valid_v2(plane)) {
            invalidate_local_cache();
            fail_closed();
            return;
        }

        const auto signal = static_cast<SignalId>(plane.signal_id);
        if (signal >= signal_count
            || plane.value_kind
                != region_frontier_value_kind_v2(state.signals[signal].value_kind)
            || state.signals[signal].initial_value.is_logic9()
                != (plane.value_kind == RegionFrontierValueKindV2::logic9)
            || plane.plane_count
                != region_frontier_required_plane_count_v2(plane.value_kind)
            || !region_frontier_value_shape_valid_v2(plane.value_kind,
                plane.width, plane.word_count, plane.plane_count)) {
            invalidate_local_cache();
            fail_closed();
            return;
        }

        if (component >= state.region_authoritative_state_by_component.size()
            || !authoritative_state
            || state.region_authoritative_state_by_component[component].get()
                != authoritative_state.get()
            || !authoritative_state->valid()
            || authoritative_state->generation() != runtime_generation
            || !authoritative_state->values().packed_signal_slots_bound(signal)
            || !authoritative_state->values().packed_owner_slot_bound(signal,
                static_cast<ProcessId>(plane.owner_process_id))) {
            invalidate_local_cache();
            fail_closed();
            return;
        }

        const auto wide_offset
            = static_cast<std::size_t>(state.direct_wide_signal_offsets[signal]);
        if (wide_offset > state.direct_wide_signal_aval.size()
            || plane.word_count
                > state.direct_wide_signal_aval.size() - wide_offset
            || wide_offset > state.direct_wide_signal_bval.size()
            || plane.word_count
                > state.direct_wide_signal_bval.size() - wide_offset
            || (plane.plane_count == 4U
                && (wide_offset > state.direct_wide_signal_logic9_plane2.size()
                    || plane.word_count
                        > state.direct_wide_signal_logic9_plane2.size() - wide_offset
                    || wide_offset > state.direct_wide_signal_logic9_plane3.size()
                    || plane.word_count
                        > state.direct_wide_signal_logic9_plane3.size() - wide_offset))) {
            invalidate_local_cache();
            fail_closed();
            return;
        }
        if (plane.width <= 64U && plane.word_count != 1U) {
            invalidate_local_cache();
            fail_closed();
            return;
        }
        if (plane.value_kind == RegionFrontierValueKindV2::logic9
            && plane.width <= 64U
            && (signal >= state.direct_signal_logic9_plane0.size()
                || signal >= state.direct_signal_logic9_plane1.size()
                || signal >= state.direct_signal_logic9_plane2.size()
                || signal >= state.direct_signal_logic9_plane3.size()
                || signal >= state.direct_signal_last_logic9_plane0.size()
                || signal >= state.direct_signal_last_logic9_plane1.size()
                || signal >= state.direct_signal_last_logic9_plane2.size()
                || signal >= state.direct_signal_last_logic9_plane3.size())) {
            invalidate_local_cache();
            fail_closed();
            return;
        }

        const auto& signal_metadata = metadata[plane.metadata_index];
        if (signal_metadata.transaction_valid != 1U
            || signal_metadata.event_valid > 1U
            || signal_metadata.event_process_domain
                > static_cast<std::uint32_t>(
                    ProcessSchedulingDomain::systemverilog)
            || signal_metadata.event_phase
                > static_cast<std::uint32_t>(SchedulerPhase::postponed)
            || (committed.changed != 0U
                && signal_metadata.event_valid == 0U)) {
            invalidate_local_cache();
            fail_closed();
            return;
        }

        if (!frontier_role_matches(lease, signal,
                static_cast<ProcessId>(plane.owner_process_id),
                PackedPlaneRole::current, plane.value_kind,
                plane.word_count, plane.current_planes)
            || !frontier_role_matches(lease, signal,
                static_cast<ProcessId>(plane.owner_process_id),
                PackedPlaneRole::previous, plane.value_kind,
                plane.word_count, plane.previous_planes)
            || !frontier_role_matches(lease, signal,
                static_cast<ProcessId>(plane.owner_process_id),
                PackedPlaneRole::stored, plane.value_kind,
                plane.word_count, plane.stored_planes)
            || !frontier_role_matches(lease, signal,
                static_cast<ProcessId>(plane.owner_process_id),
                PackedPlaneRole::owner, plane.value_kind,
                plane.word_count, plane.owner_planes)) {
            invalidate_local_cache();
            fail_closed();
            return;
        }
        std::array<std::span<const std::uint64_t>, 4U> current_words;
        std::array<std::span<const std::uint64_t>, 4U> previous_words;
        std::array<std::span<const std::uint64_t>, 4U> stored_words;
        std::array<std::span<const std::uint64_t>, 4U> owner_words;
        for (std::size_t value_plane = 0U;
             value_plane < plane.plane_count; ++value_plane) {
            current_words[value_plane] = {
                plane.current_planes[value_plane], plane.word_count };
            previous_words[value_plane] = {
                plane.previous_planes[value_plane], plane.word_count };
            stored_words[value_plane] = {
                plane.stored_planes[value_plane], plane.word_count };
            owner_words[value_plane] = {
                plane.owner_planes[value_plane], plane.word_count };
        }
        if (!region_frontier_plane_words_canonical_v2(plane.value_kind,
                plane.width, plane.word_count, current_words)
            || !region_frontier_plane_words_canonical_v2(plane.value_kind,
                plane.width, plane.word_count, previous_words)
            || !region_frontier_plane_words_canonical_v2(plane.value_kind,
                plane.width, plane.word_count, stored_words)
            || !region_frontier_plane_words_canonical_v2(plane.value_kind,
                plane.width, plane.word_count, owner_words)) {
            invalidate_local_cache();
            fail_closed();
            return;
        }

        if (committed.state_changed != 0U) {
            any_state_changed = true;
            // The certified whole-owner Logic4/Logic9 path commits the same
            // canonical value to current, stored, and raw owner. Verify every
            // present plane before leaving the packed roles authoritative.
            for (std::size_t word = 0U; word < plane.word_count; ++word) {
                for (std::size_t value_plane = 0U;
                     value_plane < plane.plane_count; ++value_plane) {
                    if (plane.current_planes[value_plane][word]
                            != plane.stored_planes[value_plane][word]
                        || plane.current_planes[value_plane][word]
                            != plane.owner_planes[value_plane][word]) {
                        invalidate_local_cache();
                        fail_closed();
                        return;
                    }
                }
            }
        }
    }

    for (std::size_t index = 0U; index < committed_count; ++index) {
        const auto& committed = committed_signals[index];
        const auto& plane = planes[committed.signal_slot];
        const auto signal = static_cast<SignalId>(plane.signal_id);
        const auto& signal_metadata = metadata[plane.metadata_index];

        if (committed.state_changed != 0U) {
            lease.note_value_change(signal);
        }
        state.scheduler.note_signal_transaction(signal);
        if (committed.changed != 0U) {
            state.scheduler.note_signal_change(signal);
        }

        state.signal_transactions[signal] = std::pair {
            SimulationTick { signal_metadata.transaction_time },
            signal_metadata.transaction_delta
        };

        if (signal_metadata.event_valid != 0U) {
            state.signal_events[signal] = std::pair {
                SimulationTick { signal_metadata.event_time },
                signal_metadata.event_delta
            };
            state.signal_event_scheduling_stamps[signal]
                = SignalEventSchedulingStamp {
                    SignalChangeOrigin {
                        static_cast<ProcessSchedulingDomain>(
                            signal_metadata.event_process_domain),
                        static_cast<SchedulerPhase>(
                            signal_metadata.event_phase)
                    },
                    signal_metadata.systemverilog_round
                };
        } else {
            state.signal_events[signal].reset();
            state.signal_event_scheduling_stamps[signal]
                = SignalEventSchedulingStamp { };
        }
        state.signal_value_revisions[signal]
            = signal_metadata.value_revision;

        const auto wide_offset = static_cast<std::size_t>(
            state.direct_wide_signal_offsets[signal]);
        std::copy_n(plane.current_planes[0U], plane.word_count,
            state.direct_wide_signal_aval.begin()
                + static_cast<std::ptrdiff_t>(wide_offset));
        std::copy_n(plane.current_planes[1U], plane.word_count,
            state.direct_wide_signal_bval.begin()
                + static_cast<std::ptrdiff_t>(wide_offset));
        if (plane.plane_count == 4U) {
            std::copy_n(plane.current_planes[2U], plane.word_count,
                state.direct_wide_signal_logic9_plane2.begin()
                    + static_cast<std::ptrdiff_t>(wide_offset));
            std::copy_n(plane.current_planes[3U], plane.word_count,
                state.direct_wide_signal_logic9_plane3.begin()
                    + static_cast<std::ptrdiff_t>(wide_offset));
        }
        if (plane.value_kind == RegionFrontierValueKindV2::logic4
            && plane.width <= 64U) {
            state.direct_signal_aval[signal] = plane.current_planes[0U][0U];
            state.direct_signal_bval[signal] = plane.current_planes[1U][0U];
            state.direct_signal_last_aval[signal]
                = plane.previous_planes[0U][0U];
            state.direct_signal_last_bval[signal]
                = plane.previous_planes[1U][0U];
        } else if (plane.value_kind == RegionFrontierValueKindV2::logic9
            && plane.width <= 64U) {
            state.direct_signal_logic9_plane0[signal]
                = plane.current_planes[0U][0U];
            state.direct_signal_logic9_plane1[signal]
                = plane.current_planes[1U][0U];
            state.direct_signal_logic9_plane2[signal]
                = plane.current_planes[2U][0U];
            state.direct_signal_logic9_plane3[signal]
                = plane.current_planes[3U][0U];
            state.direct_signal_last_logic9_plane0[signal]
                = plane.previous_planes[0U][0U];
            state.direct_signal_last_logic9_plane1[signal]
                = plane.previous_planes[1U][0U];
            state.direct_signal_last_logic9_plane2[signal]
                = plane.previous_planes[2U][0U];
            state.direct_signal_last_logic9_plane3[signal]
                = plane.previous_planes[3U][0U];
        }
        // Public PackedLogic4 roles remain bound to the locked A4 planes.
        // Keep them authoritative through late observation instead of arming
        // the scalar materializer, which cannot carry wide or Logic9 planes.
        state.direct_signal_materialization_pending[signal] = 0U;
    }

    if (any_state_changed) {
        invalidate_local_cache();
    }

    native_frame.committed_signal_count = 0U;
}

} // namespace fsim::runtime::simir
