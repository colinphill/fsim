// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

namespace fsim::runtime::simir {

namespace {

[[nodiscard]] constexpr std::uint64_t encode_frontier_payload_v2(
    const RegionFrontierEventKindV2 kind,
    const std::uint64_t index) noexcept
{
    return (static_cast<std::uint64_t>(kind)
            << kRegionFrontierPayloadKindShiftV2)
        | index;
}

[[nodiscard]] bool same_frontier_key(
    const RegionFrontierKeyV2& left,
    const RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool is_write_event(const std::uint32_t kind) noexcept
{
    return kind == static_cast<std::uint32_t>(
                       RegionFrontierEventKindV2::internal_commit)
        || kind == static_cast<std::uint32_t>(
                       RegionFrontierEventKindV2::boundary_commit);
}

[[nodiscard]] bool write_flags_match_site(
    const std::uint32_t flags,
    const RegionFrontierWriteSiteV2& site) noexcept
{
    constexpr auto required_value_flags
        = RegionFrontierPendingWriteFlagsV2::pending_active
        | RegionFrontierPendingWriteFlagsV2::pending_value_ready;

    const auto expected_target
        = site.event_kind == static_cast<std::uint32_t>(
                RegionFrontierEventKindV2::internal_commit)
        ? static_cast<std::uint32_t>(
              RegionFrontierPendingWriteFlagsV2::pending_internal_target)
        : static_cast<std::uint32_t>(
              RegionFrontierPendingWriteFlagsV2::pending_boundary_target);
    return flags == (required_value_flags | expected_target);
}

[[nodiscard]] bool encoded_native_payload_is_valid(
    const std::uint64_t payload,
    const RegionFrontierLayoutV2& layout) noexcept
{
    const auto kind = payload >> kRegionFrontierPayloadKindShiftV2;
    const auto index = payload & kRegionFrontierPayloadIndexMaskV2;
    if (kind == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::member_activation)) {
        return index < layout.member_count;
    }
    if (kind != static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::internal_commit)
        && kind != static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::boundary_commit)) {
        return false;
    }
    if (index >= layout.pending_write_capacity
        || (layout.write_site_count != 0U && layout.write_sites == nullptr)) {
        return false;
    }
    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        if (site.pending_slot == index) {
            return site.event_kind == kind
                && site.member_index < layout.member_count;
        }
    }
    return false;
}

[[nodiscard]] bool translated_task_payload_matches(
    const std::uint64_t original_payload,
    const std::uint64_t translated_payload,
    const std::uint64_t systemverilog_wave_payload,
    const RegionFrontierLayoutV2& layout,
    const std::span<const RegionFrontierMemberV2> members) noexcept
{
    if ((original_payload & systemverilog_wave_payload) == 0U) {
        return original_payload == translated_payload
            && encoded_native_payload_is_valid(original_payload, layout);
    }

    const auto process_value = original_payload
        & ~systemverilog_wave_payload;
    if (process_value > std::numeric_limits<ProcessId>::max()
        || layout.members == nullptr
        || members.size() != layout.member_count) {
        return false;
    }
    const auto process = static_cast<ProcessId>(process_value);
    auto local_member = UINT32_MAX;
    for (std::uint32_t member_index = 0U;
         member_index < layout.member_count; ++member_index) {
        if (layout.members[member_index].process_id != process) {
            continue;
        }
        if (local_member != UINT32_MAX) {
            return false;
        }
        local_member = member_index;
    }
    if (local_member == UINT32_MAX
        || members[local_member].process_id != process) {
        return false;
    }
    return translated_payload == encode_frontier_payload_v2(
        RegionFrontierEventKindV2::member_activation, local_member);
}

} // namespace

bool Interpreter::Impl::RegionFrontierComponentRuntime::commit_staged_events(
    Scheduler::SystemVerilogGroupBatchReservation& reservation,
    std::shared_ptr<void> owner_lifetime,
    const SchedulerBatchFrontier& frontier) noexcept
{
    const auto trace_decline = [&](const std::size_t event_index,
                                   const std::source_location caller,
                                   const char* reason = "guard") noexcept {
        if (owner == nullptr || !owner->systemverilog_wave_profile_enabled) {
            return;
        }
        std::fprintf(stderr,
            "fsim-profile: sv-region-recertification "
            "event=staged-issue-rejection component=%zu caller='%s:%u' "
            "function='%s' reason=%s invalidated=%u reservation=%u "
            "runtime_generation=%llu task_count=%u task_cursor=%u "
            "pending=%u staged=%u staged_capacity=%u committed=%u "
            "frontier_tasks=%zu frontier_end=%zu event_index=%zu\n",
            component, caller.file_name(), caller.line(), caller.function_name(),
            reason, static_cast<unsigned>(invalidated),
            static_cast<unsigned>(static_cast<bool>(reservation)),
            static_cast<unsigned long long>(runtime_generation),
            frame.scheduler_task_count, frame.scheduler_task_cursor,
            frame.pending_write_count, frame.staged_event_count,
            frame.staged_event_capacity, frame.committed_signal_count,
            frontier.tasks.size(), frontier.end, event_index);
        if (event_index >= staged_events.size()
            || event_index >= frame.staged_event_count) {
            return;
        }
        const auto& event = staged_events[event_index];
        std::fprintf(stderr,
            "fsim-profile: sv-region-recertification "
            "event=staged-issue-event component=%zu index=%zu kind=%u "
            "descriptor=%u stable_order=%llu origin_time=%llu "
            "origin_delta=%llu origin_round=%llu origin_order=%llu "
            "origin_sequence=%llu origin_domain=%u origin_phase=%u\n",
            component, event_index, event.kind, event.descriptor_index,
            static_cast<unsigned long long>(event.stable_order),
            static_cast<unsigned long long>(event.origin.time),
            static_cast<unsigned long long>(event.origin.delta),
            static_cast<unsigned long long>(event.origin.systemverilog_round),
            static_cast<unsigned long long>(event.origin.stable_order),
            static_cast<unsigned long long>(event.origin.sequence),
            event.origin.process_domain, event.origin.phase);
        if (event.kind == static_cast<std::uint32_t>(
                              RegionFrontierEventKindV2::member_activation)
            && event.descriptor_index < members.size()) {
            const auto& member = members[event.descriptor_index];
            std::fprintf(stderr,
                "fsim-profile: sv-region-recertification "
                "event=staged-issue-member component=%zu index=%u "
                "process=%u flags=%u origin_matches=%u "
                "queued_order=%llu queued_sequence=%llu\n",
                component, event.descriptor_index, member.process_id, member.flags,
                static_cast<unsigned>(same_frontier_key(
                    event.origin, member.pending_activation_origin)),
                static_cast<unsigned long long>(member.queued_key.stable_order),
                static_cast<unsigned long long>(member.queued_key.sequence));
        } else if (is_write_event(event.kind)
            && event.descriptor_index < pending_writes.size()) {
            const auto& write = pending_writes[event.descriptor_index];
            std::fprintf(stderr,
                "fsim-profile: sv-region-recertification "
                "event=staged-issue-write component=%zu index=%u flags=%u "
                "member=%u signal_slot=%u source_instruction=%u update_kind=%u "
                "value_kind=%u width=%u word_count=%u plane_count=%u "
                "reserved=%u origin_matches=%u\n",
                component, event.descriptor_index, write.flags, write.member_index,
                write.signal_slot, write.source_instruction, write.update_kind,
                static_cast<unsigned>(write.value_kind), write.width,
                write.word_count, write.plane_count,
                write.reserved,
                static_cast<unsigned>(same_frontier_key(event.origin, write.origin)));
        }
    };
    const auto cancel_and_decline = [&](const std::size_t event_index = SIZE_MAX,
                                       const std::source_location caller
                                           = std::source_location::current()) noexcept {
        trace_decline(event_index, caller);
        reservation.cancel();
        return false;
    };
    const auto& current_frame = frame;
    if (!reservation || !owner_lifetime || owner == nullptr || invalidated
        || !frame_initialized || !backend || !backend->executor) {
        return cancel_and_decline();
    }

    const auto& layout = backend->executor->layout();
    if (!region_frontier_frame_header_valid_v2(current_frame)
        || current_frame.generic_update_ack_count != 0U
        || layout.abi_version != kRegionFrontierAbiVersionV2
        || layout.struct_size != sizeof(RegionFrontierLayoutV2)
        || !region_frontier_layout_header_valid_v2(layout)
        || layout.reserved0 != 0U || layout.reserved_capacity != 0U
        || layout.execution_mode != RegionFrontierExecutionModeV2::systemverilog_active
        || layout.member_count != current_frame.member_count
        || layout.member_count != members.size()
        || layout.members == nullptr
        || layout.signal_slot_count == 0U || layout.signals == nullptr
        || (layout.member_count != 0U
            && layout.max_member_staged_event_counts == nullptr)
        || current_frame.readiness_word_count
            != layout.readiness_word_count
        || current_frame.ready_words != ready_words.data()
        || ready_words.size() != layout.readiness_word_count
        || (layout.write_site_count != 0U && layout.write_sites == nullptr)) {
        return cancel_and_decline();
    }
    const auto event_count
        = static_cast<std::size_t>(current_frame.staged_event_count);
    auto maximum_member_event_count = std::uint32_t { 0U };
    for (std::uint32_t member_index = 0U;
         member_index < layout.member_count; ++member_index) {
        maximum_member_event_count = std::max(
            maximum_member_event_count,
            layout.max_member_staged_event_counts[member_index]);
    }
    const auto minimum_event_capacity = std::max(
        maximum_member_event_count, layout.max_commit_fanout_events);
    if (layout.certificate_generation
            != current_frame.certificate_generation
        || layout.component_generation != current_frame.component_generation
        || current_frame.runtime_generation == 0U
        || current_frame.runtime_generation
            != current_frame.bound_runtime_generation
        || current_frame.member_count != layout.member_count
        || current_frame.members != members.data()
        || members.size() != layout.member_count
        || layout.members == nullptr
        || current_frame.pending_write_capacity
            != layout.pending_write_capacity
        || current_frame.pending_writes != pending_writes.data()
        || pending_writes.size() != layout.pending_write_capacity
        || pending_plane_offsets.size() != layout.pending_write_capacity
        || current_frame.signal_slot_count != layout.signal_slot_count
        || current_frame.planes != planes.data()
        || planes.size() != layout.signal_slot_count
        || current_frame.port_planes
            != (port_planes.empty() ? nullptr : port_planes.data())
        || port_planes.size() != layout.signal_slot_count
        || current_frame.fanout_edge_count != layout.fanout_edge_count
        || current_frame.fanout_edges
            != (fanout_edges.empty() ? nullptr : fanout_edges.data())
        || fanout_edges.size() != layout.fanout_edge_count
        || current_frame.metadata_count != layout.metadata_count
        || current_frame.metadata
            != (metadata.empty() ? nullptr : metadata.data())
        || metadata.size() != layout.metadata_count
        || current_frame.pending_write_count
            > current_frame.pending_write_capacity
        || current_frame.staged_event_capacity
            > layout.staged_event_capacity
        || current_frame.staged_event_capacity < minimum_event_capacity
        || current_frame.staged_events != staged_events.data()
        || (current_frame.staged_event_capacity != 0U
            && current_frame.staged_events == nullptr)
        || staged_events.size() != current_frame.staged_event_capacity
        || compact_members.size() != current_frame.staged_event_capacity
        || issued_sequences.size() != current_frame.staged_event_capacity
        || event_count > current_frame.staged_event_capacity
        || current_frame.committed_signal_capacity
            != layout.committed_signal_capacity
        || current_frame.committed_signal_count
            > current_frame.committed_signal_capacity
        || current_frame.committed_signals != committed_signals.data()
        || committed_signals.size()
            != current_frame.committed_signal_capacity
        || current_frame.scheduler_task_cursor
            > current_frame.scheduler_task_count
        || current_frame.scheduler_task_count
            > current_frame.scheduler_task_capacity
        || current_frame.scheduler_tasks != scheduler_tasks.data()
        || scheduler_tasks.size() < current_frame.scheduler_task_count
        || original_scheduler_tasks.size()
            < current_frame.scheduler_task_count
        || current_frame.scheduler_frontier_generation == 0U
        || current_frame.scheduler_frontier_generation != frontier.generation
        || current_frame.cut.scheduler_frontier_generation != frontier.generation
        || frontier.phase != SchedulerPhase::active
        || frontier.cursor != 0U
        || frontier.end != frontier.tasks.size()
        || frontier.tasks.size() != current_frame.scheduler_task_count
        || current_frame.slot.time != frontier.time
        || current_frame.slot.delta != frontier.delta
        || current_frame.slot.systemverilog_round
            != frontier.systemverilog_round
        || current_frame.slot.process_domain
            != static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
        || current_frame.slot.phase
            != static_cast<std::uint32_t>(SchedulerPhase::active)) {
        return cancel_and_decline();
    }

    for (std::size_t signal_slot = 0U;
         signal_slot < layout.signal_slot_count; ++signal_slot) {
        const auto& signal = layout.signals[signal_slot];
        const auto& plane = planes[signal_slot];
        if (port_planes[signal_slot] != &plane
            || signal.signal_id != plane.signal_id
            || signal.owner_process_id != plane.owner_process_id
            || signal.value_kind != plane.value_kind
            || signal.width != plane.width
            || signal.word_count != plane.word_count
            || signal.plane_count != plane.plane_count
            || signal.flags != plane.flags
            || signal.metadata_index != plane.metadata_index
            || !region_frontier_plane_bindings_valid_v2(plane)) {
            return cancel_and_decline();
        }
    }

    for (std::size_t index = 0U; index < frontier.tasks.size(); ++index) {
        const auto& scheduler_task = frontier.tasks[index];
        const auto& original_task = original_scheduler_tasks[index];
        const auto& copied_task = scheduler_tasks[index];
        if (scheduler_task.stable_order != original_task.stable_order
            || scheduler_task.sequence != original_task.sequence
            || scheduler_task.payload != original_task.payload
            || copied_task.stable_order != original_task.stable_order
            || copied_task.sequence != original_task.sequence
            || !translated_task_payload_matches(original_task.payload,
                copied_task.payload, systemverilog_wave_payload, layout,
                members)) {
            return cancel_and_decline();
        }
    }

    if (event_count == 0U) {
        const std::span<const SystemVerilogCompactBatchMember> no_members;
        const std::span<std::uint64_t> no_sequences;
        if (!reservation.commit_compact(
                no_members, *this, std::move(owner_lifetime), no_sequences)) {
            trace_decline(SIZE_MAX, std::source_location::current(),
                "reservation-commit-empty");
            return false;
        }
        frame.staged_event_count = 0U;
        return true;
    }

    const auto target_systemverilog_round
        = reservation.target_systemverilog_round();
    if (!target_systemverilog_round) {
        return cancel_and_decline();
    }

    if (layout.members == nullptr || layout.signals == nullptr
        || (layout.write_site_count != 0U && layout.write_sites == nullptr)) {
        return cancel_and_decline();
    }

    // Stable insertion sort avoids an allocation at this post-native point.
    // Equal stable-order events retain their generated append order, which is
    // their source enqueue order and therefore their scheduler tie order.
    for (std::size_t index = 1U; index < event_count; ++index) {
        const auto event = staged_events[index];
        auto insertion = index;
        while (insertion > 0U
            && event.stable_order
                < staged_events[insertion - 1U].stable_order) {
            staged_events[insertion] = staged_events[insertion - 1U];
            --insertion;
        }
        staged_events[insertion] = event;
    }

    // Authenticate every target before committing any scheduler entry. The
    // generated entry already validates the frame before native mutation; this
    // host-side pass ensures that only well-formed issued keys are installed.
    for (std::size_t index = 0U; index < event_count; ++index) {
        const auto& event = staged_events[index];
        if (event.kind == static_cast<std::uint32_t>(
                              RegionFrontierEventKindV2::member_activation)) {
            if (event.descriptor_index >= layout.member_count) {
                return cancel_and_decline(index);
            }
            const auto& member = members[event.descriptor_index];
            const auto& expected_member
                = layout.members[event.descriptor_index];
            const auto ready_word_index
                = static_cast<std::size_t>(event.descriptor_index / 64U);
            constexpr auto queued_flags
                = RegionFrontierMemberFlagsV2::queued
                | RegionFrontierMemberFlagsV2::queued_key_valid;
            constexpr auto member_required_flags
                = RegionFrontierMemberFlagsV2::waiting_on_static
                | RegionFrontierMemberFlagsV2::pending_activation;
            if (member.process_id != expected_member.process_id
                || event.stable_order != expected_member.process_id
                || (member.flags & member_required_flags)
                    != member_required_flags
                || (member.flags & queued_flags) != 0U
                || (member.flags
                        & RegionFrontierMemberFlagsV2::executing)
                    != 0U
                || ready_word_index >= ready_words.size()
                || !same_frontier_key(
                    event.origin, member.pending_activation_origin)) {
                return cancel_and_decline(index);
            }
        } else if (is_write_event(event.kind)) {
            if (event.descriptor_index >= layout.pending_write_capacity) {
                return cancel_and_decline(index);
            }
            const RegionFrontierWriteSiteV2* expected_site = nullptr;
            for (std::size_t site_index = 0U;
                 site_index < layout.write_site_count; ++site_index) {
                const auto& site = layout.write_sites[site_index];
                if (site.pending_slot == event.descriptor_index) {
                    expected_site = &site;
                    break;
                }
            }
            const bool boundary_site = expected_site != nullptr
                && expected_site->event_kind == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::boundary_commit);
            if (expected_site == nullptr
                || expected_site->event_kind != event.kind
                || expected_site->member_index >= layout.member_count
                || expected_site->signal_slot >= layout.signal_slot_count
                || expected_site->value_kind
                    != layout.signals[expected_site->signal_slot].value_kind
                || (boundary_site
                    ? expected_site->width
                        > layout.signals[expected_site->signal_slot].width
                    : expected_site->width
                        != layout.signals[expected_site->signal_slot].width)
                || (boundary_site
                    ? expected_site->word_count
                        > layout.signals[expected_site->signal_slot].word_count
                    : expected_site->word_count
                        != layout.signals[expected_site->signal_slot].word_count)
                || expected_site->plane_count
                    != layout.signals[expected_site->signal_slot].plane_count
                || !region_frontier_value_shape_valid_v2(
                    expected_site->value_kind, expected_site->width,
                    expected_site->word_count, expected_site->plane_count)) {
                if (owner->systemverilog_wave_profile_enabled) {
                    std::uint32_t checked_terms = 1U;
                    std::uint32_t failed_terms = expected_site == nullptr ? 1U : 0U;
                    if (expected_site != nullptr) {
                        checked_terms |= 2U | 4U | 8U | 256U;
                        failed_terms |= expected_site->event_kind != event.kind ? 2U : 0U;
                        failed_terms |= expected_site->member_index >= layout.member_count ? 4U : 0U;
                        failed_terms |= expected_site->signal_slot >= layout.signal_slot_count ? 8U : 0U;
                        failed_terms |= !region_frontier_value_shape_valid_v2(
                            expected_site->value_kind, expected_site->width,
                            expected_site->word_count, expected_site->plane_count) ? 256U : 0U;
                        std::fprintf(stderr,
                            "fsim-profile: sv-region-recertification "
                            "event=staged-issue-expected-site component=%zu "
                            "event_index=%zu pending_slot=%u event_kind=%u "
                            "member=%u signal_slot=%u source_instruction=%u "
                            "update_kind=%u value_kind=%u width=%u words=%u planes=%u\n",
                            component, index, expected_site->pending_slot,
                            expected_site->event_kind, expected_site->member_index,
                            expected_site->signal_slot, expected_site->source_instruction,
                            expected_site->update_kind,
                            static_cast<unsigned>(expected_site->value_kind),
                            expected_site->width, expected_site->word_count,
                            expected_site->plane_count);
                        if (expected_site->signal_slot < layout.signal_slot_count) {
                            const auto& target = layout.signals[expected_site->signal_slot];
                            checked_terms |= 16U | 32U | 64U | 128U;
                            failed_terms |= expected_site->value_kind != target.value_kind ? 16U : 0U;
                            const bool width_fits = boundary_site
                                ? expected_site->width <= target.width
                                : expected_site->width == target.width;
                            const bool words_fit = boundary_site
                                ? expected_site->word_count <= target.word_count
                                : expected_site->word_count == target.word_count;
                            failed_terms |= !width_fits ? 32U : 0U;
                            failed_terms |= !words_fit ? 64U : 0U;
                            failed_terms |= expected_site->plane_count != target.plane_count ? 128U : 0U;
                            std::fprintf(stderr,
                                "fsim-profile: sv-region-recertification "
                                "event=staged-issue-layout-target component=%zu "
                                "signal_slot=%u signal=%u owner=%u value_kind=%u "
                                "width=%u words=%u planes=%u flags=%u metadata=%u\n",
                                component, expected_site->signal_slot, target.signal_id,
                                target.owner_process_id,
                                static_cast<unsigned>(target.value_kind), target.width,
                                target.word_count, target.plane_count, target.flags,
                                target.metadata_index);
                        }
                    }
                    std::fprintf(stderr,
                        "fsim-profile: sv-region-recertification "
                        "event=staged-issue-site-terms component=%zu event_index=%zu "
                        "checked_mask=%u failed_mask=%u "
                        "bits=site1_kind2_member4_signal8_value16_width32_words64_planes128_shape256\n",
                        component, index, checked_terms, failed_terms);
                }
                return cancel_and_decline(index);
            }
            const auto& write = pending_writes[event.descriptor_index];
            const auto& owner_member
                = layout.members[expected_site->member_index];
            const auto& target_signal
                = layout.signals[expected_site->signal_slot];
            const bool internal_target
                = expected_site->event_kind == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::internal_commit);
            const auto expected_target_flags = internal_target
                ? static_cast<std::uint32_t>(
                    RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                : static_cast<std::uint32_t>(
                    RegionFrontierPlaneFlagsV2::read_only_boundary_port);
            const auto offset
                = event.descriptor_index < pending_plane_offsets.size()
                ? pending_plane_offsets[event.descriptor_index]
                : std::numeric_limits<std::size_t>::max();
            const bool plane_words_fit
                = expected_site->plane_count != 0U
                && static_cast<std::size_t>(expected_site->word_count)
                    <= std::numeric_limits<std::size_t>::max()
                        / expected_site->plane_count;
            const auto plane_words = plane_words_fit
                ? static_cast<std::size_t>(expected_site->word_count)
                    * expected_site->plane_count
                : std::numeric_limits<std::size_t>::max();
            const bool pending_storage_fits
                = plane_words_fit
                && offset <= pending_plane_words.size()
                && plane_words <= pending_plane_words.size() - offset;
            bool pending_pointers_match = pending_storage_fits;
            if (pending_pointers_match) {
                for (std::size_t plane = 0U;
                     plane < expected_site->plane_count; ++plane) {
                    if (write.value_planes[plane]
                        != pending_plane_words.data() + offset
                            + plane * expected_site->word_count) {
                        pending_pointers_match = false;
                        break;
                    }
                }
                for (std::size_t plane = expected_site->plane_count;
                     plane < 4U; ++plane) {
                    if (write.value_planes[plane] != nullptr) {
                        pending_pointers_match = false;
                        break;
                    }
                }
            }
            std::array<std::span<const std::uint64_t>, 4U> value_planes;
            if (pending_pointers_match) {
                for (std::size_t plane = 0U;
                     plane < expected_site->plane_count; ++plane) {
                    value_planes[plane] = std::span<const std::uint64_t> {
                        write.value_planes[plane], expected_site->word_count };
                }
            }
            if (event.stable_order != owner_member.process_id
                || write.member_index != expected_site->member_index
                || write.signal_slot != expected_site->signal_slot
                || write.source_instruction
                    != expected_site->source_instruction
                || write.update_kind != expected_site->update_kind
                || write.value_kind != expected_site->value_kind
                || write.width != expected_site->width
                || write.word_count != expected_site->word_count
                || write.plane_count != expected_site->plane_count
                || write.reserved != 0U
                || !region_frontier_pending_write_bindings_valid_v2(write)
                || !pending_pointers_match
                || !region_frontier_plane_words_canonical_v2(
                    write.value_kind, write.width, write.word_count,
                    value_planes)
                || target_signal.flags != expected_target_flags
                || target_signal.owner_process_id
                    != (internal_target ? owner_member.process_id : UINT32_MAX)
                || !write_flags_match_site(write.flags, *expected_site)
                || !same_frontier_key(event.origin, write.origin)) {
                return cancel_and_decline(index);
            }
        } else {
            return cancel_and_decline(index);
        }

        for (std::size_t previous_index = 0U;
             previous_index < index; ++previous_index) {
            const auto& previous = staged_events[previous_index];
            const bool duplicate_activation
                = event.kind == static_cast<std::uint32_t>(
                                     RegionFrontierEventKindV2::member_activation)
                && previous.kind == event.kind
                && previous.descriptor_index == event.descriptor_index;
            const bool duplicate_write
                = is_write_event(event.kind) && is_write_event(previous.kind)
                && previous.descriptor_index == event.descriptor_index;
            if (duplicate_activation || duplicate_write) {
                return cancel_and_decline(index);
            }
        }

        compact_members[index] = {
            event.stable_order,
            encode_frontier_payload_v2(
                static_cast<RegionFrontierEventKindV2>(event.kind),
                event.descriptor_index),
        };
    }

    const auto members_to_commit
        = std::span<const SystemVerilogCompactBatchMember> {
            compact_members.data(), event_count };
    const auto sequences_to_fill
        = std::span<std::uint64_t> { issued_sequences.data(), event_count };
    if (!reservation.commit_compact(members_to_commit, *this,
            std::move(owner_lifetime), sequences_to_fill)) {
        trace_decline(SIZE_MAX, std::source_location::current(),
            "reservation-commit-events");
        return false;
    }

    // commit_compact returns one fresh sequence per input event. All target
    // descriptors were validated above; these assignments are POD stores and
    // cannot fail after the scheduler has made the ticket visible.
    for (std::size_t index = 0U; index < event_count; ++index) {
        const auto& event = staged_events[index];
        RegionFrontierKeyV2 issued_key;
        issued_key.time = current_frame.slot.time;
        issued_key.delta = current_frame.slot.delta;
        issued_key.systemverilog_round = *target_systemverilog_round;
        issued_key.stable_order = event.stable_order;
        issued_key.sequence = issued_sequences[index];
        issued_key.process_domain = current_frame.slot.process_domain;
        issued_key.phase = current_frame.slot.phase;

        if (event.kind == static_cast<std::uint32_t>(
                              RegionFrontierEventKindV2::member_activation)) {
            auto& member = members[event.descriptor_index];
            member.queued_key = issued_key;
            member.flags |= RegionFrontierMemberFlagsV2::queued
                | RegionFrontierMemberFlagsV2::queued_key_valid;
            ready_words[event.descriptor_index / 64U]
                |= UINT64_C(1) << (event.descriptor_index % 64U);
        } else {
            auto& write = pending_writes[event.descriptor_index];
            write.commit_key = issued_key;
            write.flags |= RegionFrontierPendingWriteFlagsV2::pending_key_assigned;
        }
    }

    frame.staged_event_count = 0U;
    return true;
}

} // namespace fsim::runtime::simir
