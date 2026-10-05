// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

namespace {

constexpr std::uint32_t kInvalidFrontierIndex = UINT32_MAX;
constexpr std::uint64_t kGenericProjectedRegionPayload
    = UINT64_C(1) << 60U;

[[nodiscard]] bool decode_generic_projected_region_payload(
    const std::uint64_t payload,
    ProcessId& process) noexcept
{
    if ((payload & kGenericProjectedRegionPayload) == 0U) {
        return false;
    }
    const auto process_wide = payload & ~kGenericProjectedRegionPayload;
    if (process_wide > std::numeric_limits<ProcessId>::max()
        || payload != (kGenericProjectedRegionPayload | process_wide)) {
        return false;
    }
    process = static_cast<ProcessId>(process_wide);
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

[[nodiscard]] bool scheduler_matches_frontier_key(
    const Scheduler& scheduler,
    const RegionFrontierKeyV2& key) noexcept
{
    const auto phase = scheduler.current_phase();
    return phase.has_value() && scheduler.now() == key.time
        && scheduler.delta() == key.delta
        && scheduler.systemverilog_round() == key.systemverilog_round
        && static_cast<std::uint32_t>(*phase) == key.phase
        && key.process_domain
            == static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
        && key.phase
            == static_cast<std::uint32_t>(SchedulerPhase::active);
}

template <typename Runtime>
[[nodiscard]] bool pending_words_match_storage(
    const Runtime& runtime,
    const std::uint32_t pending_slot,
    const RegionFrontierPendingWriteV2& write) noexcept
{
    if (pending_slot >= runtime.pending_plane_offsets.size()
        || runtime.frame.pending_write_capacity != runtime.pending_writes.size()
        || runtime.frame.pending_writes != runtime.pending_writes.data()
        || runtime.frame.pending_write_count
            > runtime.frame.pending_write_capacity) {
        return false;
    }
    const auto plane_count
        = region_frontier_required_plane_count_v2(write.value_kind);
    const auto offset = runtime.pending_plane_offsets[pending_slot];
    if (plane_count == 0U || write.plane_count != plane_count
        || offset == std::numeric_limits<std::size_t>::max()
        || offset > runtime.pending_plane_words.size()
        || write.word_count
            > (runtime.pending_plane_words.size() - offset) / plane_count) {
        return false;
    }
    for (std::size_t plane = 0U; plane < 4U; ++plane) {
        if (plane < plane_count) {
            if (write.value_planes[plane]
                != runtime.pending_plane_words.data() + offset
                    + plane * write.word_count) {
                return false;
            }
        } else if (write.value_planes[plane] != nullptr) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] const RegionFrontierWriteSiteV2* find_write_site(
    const RegionFrontierLayoutV2& layout,
    const std::uint32_t pending_slot) noexcept
{
    if (layout.write_sites == nullptr) {
        return nullptr;
    }
    const RegionFrontierWriteSiteV2* result { };
    for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
        const auto& site = layout.write_sites[index];
        if (site.pending_slot != pending_slot) {
            continue;
        }
        if (result != nullptr) {
            return nullptr;
        }
        result = &site;
    }
    return result;
}

[[nodiscard]] bool write_value_tail_is_valid(
    const RegionFrontierPendingWriteV2& write) noexcept
{
    if (!region_frontier_pending_write_bindings_valid_v2(write)) {
        return false;
    }
    std::array<std::span<const std::uint64_t>, 4U> words;
    for (std::size_t plane = 0U; plane < write.plane_count; ++plane) {
        words[plane] = { write.value_planes[plane], write.word_count };
    }
    return region_frontier_plane_words_canonical_v2(write.value_kind,
        write.width, write.word_count, words);
}

template <typename Runtime>
[[nodiscard]] bool write_is_well_formed(
    const Runtime& runtime,
    const RegionFrontierKeyV2& key,
    const std::uint32_t pending_slot,
    const RegionFrontierEventKindV2 expected_kind,
    const bool allow_acknowledged_boundary = false) noexcept
{
    if (runtime.owner == nullptr || !runtime.frame_initialized
        || runtime.backend == nullptr || runtime.backend->executor == nullptr
        || !region_frontier_frame_header_valid_v2(runtime.frame)
        || runtime.frame.generic_update_ack_count != 0U
        || runtime.frame.runtime_generation != runtime.runtime_generation
        || runtime.frame.bound_runtime_generation != runtime.runtime_generation
        || runtime.frame.pending_writes != runtime.pending_writes.data()
        || runtime.frame.signal_slot_count != runtime.planes.size()
        || runtime.frame.planes != runtime.planes.data()
        || runtime.frame.committed_signal_count != 0U
        || runtime.frame.member_count != runtime.members.size()
        || runtime.frame.pending_write_count
            > runtime.frame.pending_write_capacity
        || pending_slot >= runtime.pending_writes.size()
        || pending_slot >= runtime.frame.pending_write_capacity) {
        return false;
    }

    const auto& layout = runtime.backend->executor->layout();
    const auto& write = runtime.pending_writes[pending_slot];
    constexpr std::uint32_t required_flags
        = pending_active | pending_value_ready | pending_key_assigned;
    constexpr std::uint32_t known_flags
        = pending_active | pending_value_ready | pending_key_assigned
        | pending_internal_target | pending_boundary_target | pending_committed;
    const auto expected_target = expected_kind
            == RegionFrontierEventKindV2::internal_commit
        ? static_cast<std::uint32_t>(pending_internal_target)
        : static_cast<std::uint32_t>(pending_boundary_target);
    const auto* const site = find_write_site(layout, pending_slot);

    if (!region_frontier_layout_header_valid_v2(layout)
        || layout.reserved0 != 0U || layout.reserved_capacity != 0U
        || layout.execution_mode != RegionFrontierExecutionModeV2::systemverilog_active
        || layout.members == nullptr || layout.signals == nullptr
        || layout.write_sites == nullptr
        || layout.member_count != runtime.members.size()
        || layout.member_count != runtime.backend->kernel.members.size()
        || layout.signal_slot_count != runtime.planes.size()
        || layout.pending_write_capacity != runtime.pending_writes.size()
        || layout.write_site_count != runtime.pending_writes.size()
        || runtime.frame.certificate_generation
            != layout.certificate_generation
        || runtime.frame.component_generation != layout.component_generation
        || runtime.frame.metadata_count != runtime.metadata.size()
        || layout.metadata_count != runtime.metadata.size()
        || runtime.frame.metadata != (runtime.metadata.empty()
                ? nullptr : runtime.metadata.data())
        || runtime.frame.pending_write_count
            > runtime.frame.pending_write_capacity
        || write.member_index >= layout.member_count
        || write.member_index >= runtime.backend->kernel.members.size()
        || write.signal_slot >= layout.signal_slot_count || site == nullptr) {
        return false;
    }

    const auto& member_layout = layout.members[write.member_index];
    const auto& signal_layout = layout.signals[write.signal_slot];
    const bool acknowledged_boundary
        = expected_kind == RegionFrontierEventKindV2::boundary_commit
        && allow_acknowledged_boundary
        && (write.flags & pending_committed) != 0U;
    if (site->member_index != write.member_index
        || site->signal_slot != write.signal_slot
        || site->source_instruction != write.source_instruction
        || site->update_kind != write.update_kind
        || site->event_kind != static_cast<std::uint32_t>(expected_kind)
        || site->width != write.width || site->word_count != write.word_count
        || site->pending_slot != pending_slot
        || (write.flags & required_flags) != required_flags
        || (write.flags & (pending_internal_target | pending_boundary_target))
            != expected_target
        || (write.flags & ~known_flags) != 0U || write.reserved != 0U
        || ((write.flags & pending_committed) != 0U
            && !acknowledged_boundary)
        || write.update_kind
            != static_cast<std::uint32_t>(
                RegionUpdateKind::systemverilog_active)
        || write.width == 0U
        || write.word_count
            != (static_cast<std::size_t>(write.width) / 64U
                + (write.width % 64U != 0U ? 1U : 0U))
        || signal_layout.width == 0U
        || signal_layout.word_count
            != (static_cast<std::size_t>(signal_layout.width) / 64U
                + (signal_layout.width % 64U != 0U ? 1U : 0U))
        || write.value_kind != signal_layout.value_kind
        || write.plane_count != signal_layout.plane_count
        || site->value_kind != write.value_kind
        || site->plane_count != write.plane_count
        || !region_frontier_pending_write_bindings_valid_v2(write)
        || !pending_words_match_storage(runtime, pending_slot, write)
        || !write_value_tail_is_valid(write)
        || !same_frontier_key(write.commit_key, key)
        || write.commit_key.stable_order != member_layout.process_id
        || write.origin.process_domain
            != static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
        || write.origin.phase
            != static_cast<std::uint32_t>(SchedulerPhase::active)
        || write.signal_slot >= runtime.planes.size()
        || runtime.planes[write.signal_slot].signal_id
            != signal_layout.signal_id
        || runtime.planes[write.signal_slot].owner_process_id
            != signal_layout.owner_process_id
        || runtime.planes[write.signal_slot].value_kind
            != signal_layout.value_kind
        || runtime.planes[write.signal_slot].width != signal_layout.width
        || runtime.planes[write.signal_slot].word_count
            != signal_layout.word_count
        || runtime.planes[write.signal_slot].plane_count
            != signal_layout.plane_count) {
        return false;
    }

    const auto process = static_cast<ProcessId>(member_layout.process_id);
    if (process >= runtime.owner->processes.size()
        || process != runtime.backend->kernel.members[write.member_index].process
        || signal_layout.signal_id >= runtime.owner->signals.size()) {
        return false;
    }

    const auto& plane = runtime.planes[write.signal_slot];
    const bool is_internal
        = expected_kind == RegionFrontierEventKindV2::internal_commit;
    const bool internal_slot
        = signal_layout.flags
                == static_cast<std::uint32_t>(
                    RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
        && plane.flags == signal_layout.flags
        && signal_layout.metadata_index < layout.metadata_count;
    const bool boundary_slot
        = signal_layout.flags
                == static_cast<std::uint32_t>(
                    RegionFrontierPlaneFlagsV2::read_only_boundary_port)
        && plane.flags == signal_layout.flags
        && signal_layout.metadata_index == kInvalidFrontierIndex;
    const auto& signal = runtime.owner->signals[
        static_cast<SignalId>(signal_layout.signal_id)];
    const auto expected_value_kind = write.value_kind
            == RegionFrontierValueKindV2::logic4
        ? ValueKind::logic4 : ValueKind::logic9;
    const bool owner_matches_target = is_internal
        ? signal_layout.owner_process_id == process
            && plane.owner_process_id == process
        : signal_layout.owner_process_id == kInvalidFrontierIndex
            && plane.owner_process_id == kInvalidFrontierIndex;
    if (!owner_matches_target
        || (is_internal ? !internal_slot : !boundary_slot)
        || signal.value_kind != expected_value_kind
        || signal.initial_value.width() != signal_layout.width
        || (is_internal && write.width != signal_layout.width)
        || signal.initial_value.is_logic9()
            != (write.value_kind == RegionFrontierValueKindV2::logic9)
        || signal_layout.value_kind != write.value_kind
        || signal_layout.plane_count
            != region_frontier_required_plane_count_v2(write.value_kind)
        || !region_frontier_plane_bindings_valid_v2(plane)) {
        return false;
    }

    const RegionConeOutputBinding* output_binding { };
    for (const auto& candidate : runtime.backend->kernel.outputs) {
        if (candidate.owner != process
            || candidate.signal != signal_layout.signal_id
            || candidate.source_instruction != write.source_instruction) {
            continue;
        }
        if (output_binding != nullptr) {
            return false;
        }
        output_binding = &candidate;
    }
    const auto output_signal_width = output_binding != nullptr
            && output_binding->signal_width != 0U
        ? output_binding->signal_width
        : output_binding != nullptr ? output_binding->width : 0U;
    if (output_binding == nullptr
        || output_signal_width != signal_layout.width
        || output_binding->offset > signal_layout.width
        || output_binding->width
            > signal_layout.width - output_binding->offset
        || output_binding->width != write.width
        || (is_internal && output_binding->offset != 0U)
        || (is_internal && output_binding->width != signal_layout.width)
        || output_binding->value_kind != expected_value_kind
        || output_binding->domain
            != SignalUpdateDomain::systemverilog_active
        || output_binding->update_kind
            != RegionUpdateKind::systemverilog_active) {
        return false;
    }

    const auto process_program = runtime.owner->get_process(process).program();
    const auto& operations = process_program.operations();
    if (write.source_instruction >= operations.size()) {
        return false;
    }
    const auto source_operation
        = operations.expanded(write.source_instruction);
    const auto* const whole_write
        = operation_get_if<WriteUpdate>(&source_operation);
    const auto* const slice_write
        = operation_get_if<WriteUpdateSlice>(&source_operation);
    bool source_matches { };
    if (whole_write != nullptr) {
        source_matches
            = whole_write->signal == signal_layout.signal_id
            && whole_write->domain
                == SignalUpdateDomain::systemverilog_active
            && output_binding->offset == 0U
            && output_binding->width == signal_layout.width;
    } else if (slice_write != nullptr
        && slice_write->domain
            == SignalUpdateDomain::systemverilog_active) {
        if (slice_write->signal == signal_layout.signal_id
            && slice_write->offset == output_binding->offset
            && slice_write->offset <= signal_layout.width
            && write.width
                <= signal_layout.width - slice_write->offset) {
            source_matches = true;
            const bool partial_output
                = slice_write->offset != 0U
                || write.width != signal_layout.width;
            if (partial_output) {
                const auto source_program
                    = runtime.owner->processes.program_view(process);
                bool matching_driver_region { };
                for (const auto& region : source_program.driver_regions()) {
                    if (region.signal == slice_write->signal
                        && !region.whole
                        && region.offset == slice_write->offset
                        && region.width == write.width) {
                        matching_driver_region = true;
                    }
                }
                if (!matching_driver_region) {
                    return false;
                }
            }
        } else if (!is_internal
            && output_binding->offset == 0U
            && runtime.owner->native_boundary_slice_matches_alias_family(
                slice_write->signal, slice_write->offset, write.width,
                static_cast<SignalId>(signal_layout.signal_id))) {
            source_matches = true;
        }
    }
    return source_matches && runtime.frame.pending_write_count != 0U;
}

template <typename Runtime>
[[nodiscard]] bool member_is_well_formed(
    const Runtime& runtime,
    const std::uint32_t member_index,
    const RegionFrontierKeyV2& key) noexcept
{
    if (runtime.owner == nullptr || !runtime.frame_initialized
        || runtime.backend == nullptr || runtime.backend->executor == nullptr
        || !region_frontier_frame_header_valid_v2(runtime.frame)
        || runtime.frame.generic_update_ack_count != 0U
        || runtime.frame.runtime_generation != runtime.runtime_generation
        || runtime.frame.bound_runtime_generation != runtime.runtime_generation
        || runtime.frame.members != runtime.members.data()
        || runtime.frame.ready_words != runtime.ready_words.data()
        || runtime.frame.member_count != runtime.members.size()
        || runtime.frame.committed_signal_count != 0U
        || member_index >= runtime.members.size()
        || member_index >= runtime.backend->kernel.members.size()
        || member_index >= runtime.backend->executor->layout().member_count
        || runtime.backend->executor->layout().members == nullptr
        || runtime.backend->executor->layout().member_count
            != runtime.members.size()
        || runtime.frame.readiness_word_count != runtime.ready_words.size()) {
        return false;
    }

    const auto& member = runtime.members[member_index];
    const auto& member_layout
        = runtime.backend->executor->layout().members[member_index];
    constexpr std::uint32_t required_flags
        = RegionFrontierMemberFlagsV2::queued
        | RegionFrontierMemberFlagsV2::queued_key_valid
        | RegionFrontierMemberFlagsV2::waiting_on_static
        | RegionFrontierMemberFlagsV2::pending_activation;
    if (member.process_id != member_layout.process_id
        || member.process_id
            != runtime.backend->kernel.members[member_index].process
        || member.process_id >= runtime.owner->processes.size()
        || (member.flags & required_flags) != required_flags
        || (member.flags & RegionFrontierMemberFlagsV2::executing) != 0U
        || !same_frontier_key(member.queued_key, key)
        || member.queued_key.stable_order != member.process_id
        || member.queued_key.process_domain
            != static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog)
        || member.queued_key.phase
            != static_cast<std::uint32_t>(SchedulerPhase::active)) {
        return false;
    }

    const auto readiness_word = static_cast<std::size_t>(member_index) / 64U;
    if (readiness_word >= runtime.ready_words.size()) {
        return false;
    }
    const auto readiness_bit
        = UINT64_C(1) << (member_index % 64U);
    return (runtime.ready_words[readiness_word] & readiness_bit) != 0U;
}

template <typename Runtime>
[[noreturn]] void fallback_invariant_failure(Runtime& runtime,
    const std::source_location caller = std::source_location::current())
{
    runtime.invalidate(caller);
    throw std::logic_error { "invalid native frontier fallback descriptor" };
}

} // namespace

detail::SchedulerTaskDescriptor
Interpreter::Impl::RegionFrontierComponentRuntime::make_fallback_descriptor(
    const std::uint64_t payload) noexcept
{
    if (execution_mode
        == RegionFrontierExecutionModeV2::generic_deferred_update) {
        ProcessId generic_process { };
        if (!decode_generic_projected_region_payload(
                payload, generic_process)) {
            return { };
        }
        if (owner == nullptr || !frame_initialized
            || backend == nullptr || backend->executor == nullptr) {
            return { };
        }
        const auto& layout = backend->executor->layout();
        if (layout.execution_mode
                != RegionFrontierExecutionModeV2::generic_deferred_update
            || generic_queued_members.size() != layout.member_count
            || generic_queued_ready_words.size()
                != (static_cast<std::size_t>(layout.member_count) + 63U)
                    / 64U) {
            return { };
        }
        std::size_t member_index = std::numeric_limits<std::size_t>::max();
        for (std::size_t index = 0U; index < layout.member_count; ++index) {
            if (layout.members[index].process_id != generic_process) {
                continue;
            }
            if (member_index != std::numeric_limits<std::size_t>::max()) {
                return { };
            }
            member_index = index;
        }
        if (member_index == std::numeric_limits<std::size_t>::max()) {
            return { };
        }
        const auto& queued = generic_queued_members[member_index];
        const auto ready_bit = UINT64_C(1) << (member_index % 64U);
        if (!queued.receipt.valid
            || queued.receipt.phase != SchedulerPhase::active
            || queued.receipt.stable_order != generic_process
            || queued.receipt.payload != payload
            || queued.static_trigger_mask == 0U
            || (generic_queued_ready_words[member_index / 64U]
                & ready_bit) == 0U) {
            return { };
        }
        const GenericFallbackToken token { this, payload };
        return detail::make_scheduler_task_descriptor<
            GenericFallbackToken,
            &Interpreter::Impl::dispatch_generic_frontier_fallback>(token);
    }

    if (owner == nullptr || !frame_initialized || backend == nullptr
        || backend->executor == nullptr
        || execution_mode
            != RegionFrontierExecutionModeV2::systemverilog_active
        || !region_frontier_frame_header_valid_v2(frame)
        || frame.generic_update_ack_count != 0U
        || frame.runtime_generation != runtime_generation
        || frame.bound_runtime_generation != runtime_generation) {
        return { };
    }

    const auto kind_value = static_cast<std::uint32_t>(
        payload >> kRegionFrontierPayloadKindShiftV2);
    const auto index = payload & kRegionFrontierPayloadIndexMaskV2;
    if (index > std::numeric_limits<std::uint32_t>::max()) {
        return { };
    }
    const auto kind = static_cast<RegionFrontierEventKindV2>(kind_value);
    const auto descriptor_index = static_cast<std::uint32_t>(index);
    const RegionFrontierKeyV2* key { };
    if (kind == RegionFrontierEventKindV2::member_activation) {
        if (!member_is_well_formed(*this, descriptor_index,
                members.size() > descriptor_index
                    ? members[descriptor_index].queued_key
                    : RegionFrontierKeyV2 { })) {
            return { };
        }
        key = &members[descriptor_index].queued_key;
    } else if (kind == RegionFrontierEventKindV2::internal_commit
        || kind == RegionFrontierEventKindV2::boundary_commit) {
        if (descriptor_index >= pending_writes.size()) {
            return { };
        }
        const auto& write = pending_writes[descriptor_index];
        if (!write_is_well_formed(*this, write.commit_key,
                descriptor_index, kind,
                kind == RegionFrontierEventKindV2::boundary_commit)) {
            return { };
        }
        key = &write.commit_key;
    } else {
        return { };
    }
    if (key == nullptr) {
        return { };
    }

    // The full key remains in the immutable queued member/write descriptor
    // until this payload is consumed. The ticket payload itself is limited
    // to 32 bytes and carries only this runtime pointer plus the kind/index.
    const FallbackToken token { this, payload };
    return detail::make_scheduler_task_descriptor<
        FallbackToken,
        &Interpreter::Impl::dispatch_region_frontier_fallback>(token);
}

void Interpreter::Impl::dispatch_region_frontier_fallback(
    Scheduler& scheduler,
    const RegionFrontierComponentRuntime::FallbackToken& token)
{
    auto* const runtime = token.runtime;
    if (runtime == nullptr) {
        throw std::logic_error { "missing native frontier fallback runtime" };
    }
    struct InvalidateOnException {
        RegionFrontierComponentRuntime* runtime { };
        int exceptions { std::uncaught_exceptions() };
        ~InvalidateOnException()
        {
            if (runtime != nullptr
                && std::uncaught_exceptions() > exceptions) {
                runtime->invalidate();
            }
        }
    } fail_stop { runtime };
    if (runtime->owner == nullptr || !runtime->frame_initialized
        || runtime->backend == nullptr
        || runtime->backend->executor == nullptr) {
        fallback_invariant_failure(*runtime);
    }
    auto& owner = *runtime->owner;

    const auto payload = token.payload;
    const auto kind_value = static_cast<std::uint32_t>(
        payload >> kRegionFrontierPayloadKindShiftV2);
    const auto index_wide = payload & kRegionFrontierPayloadIndexMaskV2;
    if (index_wide > std::numeric_limits<std::uint32_t>::max()) {
        fallback_invariant_failure(*runtime);
    }
    const auto index = static_cast<std::uint32_t>(index_wide);
    const auto kind = static_cast<RegionFrontierEventKindV2>(kind_value);

    if (kind == RegionFrontierEventKindV2::member_activation) {
        if (!member_is_well_formed(*runtime, index,
                index < runtime->members.size()
                    ? runtime->members[index].queued_key
                    : RegionFrontierKeyV2 { })) {
            fallback_invariant_failure(*runtime);
        }
        auto& member = runtime->members[index];
        const auto queued_key = member.queued_key;
        if (!scheduler_matches_frontier_key(scheduler, queued_key)) {
            fallback_invariant_failure(*runtime);
        }

        const auto process_id = static_cast<ProcessId>(member.process_id);
        auto& process = owner.get_process(process_id);
        process.static_trigger_mask = member.static_trigger_mask;
        process.queued = true;
        process.waiting_on_static
            = (member.flags & RegionFrontierMemberFlagsV2::waiting_on_static)
            != 0U;

        const auto readiness_word = static_cast<std::size_t>(index) / 64U;
        const auto readiness_bit = UINT64_C(1) << (index % 64U);
        runtime->ready_words[readiness_word] &= ~readiness_bit;
        member.flags &= ~static_cast<std::uint32_t>(
            RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid
            | RegionFrontierMemberFlagsV2::pending_activation);
        member.flags |= RegionFrontierMemberFlagsV2::executing;
        member.queued_key = { };
        runtime->invalidate();

        try {
            owner.dispatch_systemverilog_wave_fallback(
                scheduler,
                SystemVerilogWaveFallbackPayload { &owner, process_id });
        } catch (...) {
            runtime->invalidate();
            throw;
        }
        return;
    }

    if (kind != RegionFrontierEventKindV2::internal_commit
        && kind != RegionFrontierEventKindV2::boundary_commit) {
        fallback_invariant_failure(*runtime);
    }
    if (index >= runtime->pending_writes.size()) {
        fallback_invariant_failure(*runtime);
    }

    auto& write = runtime->pending_writes[index];
    const auto key = write.commit_key;
    if (!scheduler_matches_frontier_key(scheduler, key)) {
        fallback_invariant_failure(*runtime);
    }

    // A boundary publication may already have completed through the checked
    // host adapter. The generated entry will normally observe that ACK and
    // retire it; a conservative fallback retires the same acknowledged
    // descriptor without replaying the publication.
    if (kind == RegionFrontierEventKindV2::boundary_commit
        && (write.flags & pending_committed) != 0U) {
        if (!write_is_well_formed(*runtime, key, index, kind, true)
            || runtime->frame.pending_write_count == 0U) {
            fallback_invariant_failure(*runtime);
        }
        write.flags &= ~static_cast<std::uint32_t>(pending_active);
        --runtime->frame.pending_write_count;
        runtime->frame.current_pending_write = kInvalidFrontierIndex;
        runtime->invalidate();
        return;
    }

    if (!write_is_well_formed(*runtime, key, index, kind)
        || !scheduler_matches_frontier_key(scheduler, key)) {
        fallback_invariant_failure(*runtime);
    }

    const auto& layout = runtime->backend->executor->layout();
    const auto& site = *find_write_site(layout, index);
    const auto process_id = static_cast<ProcessId>(
        layout.members[write.member_index].process_id);
    const auto signal_id = static_cast<SignalId>(
        layout.signals[write.signal_slot].signal_id);
    // Disable native re-entry before value ownership or checked callbacks can
    // allocate, observe, or demote the captured component.
    runtime->invalidate();
    std::array<std::span<const std::uint64_t>, 4U> value_planes;
    for (std::size_t plane = 0U; plane < write.plane_count; ++plane) {
        value_planes[plane] = { write.value_planes[plane], write.word_count };
    }
    if (!write_value_tail_is_valid(write)) {
        fallback_invariant_failure(*runtime);
    }
    PackedLogic4 value;
    if (write.value_kind == RegionFrontierValueKindV2::logic4) {
        value = PackedLogic4::from_word_planes(
            write.width, value_planes[0U], value_planes[1U]);
    } else {
        value = PackedLogic4::from_logic9_word_planes(write.width,
            value_planes[0U], value_planes[1U], value_planes[2U],
            value_planes[3U]);
    }
    const SignalChangeOrigin origin {
        static_cast<ProcessSchedulingDomain>(write.origin.process_domain),
        static_cast<SchedulerPhase>(write.origin.phase)
    };

    // The fallback consumes this original scheduler key through checked
    // owner semantics. It never consults A4 pointers and never reruns the
    // already-consumed native member body.
    (void)site;
    try {
        const auto process_program = owner.get_process(process_id).program();
        const auto& operations = process_program.operations();
        if (write.source_instruction >= operations.size()) {
            fallback_invariant_failure(*runtime);
        }
        const auto source_operation
            = operations.expanded(write.source_instruction);
        const auto* const slice_write
            = operation_get_if<WriteUpdateSlice>(&source_operation);
        if (kind == RegionFrontierEventKindV2::boundary_commit
            && slice_write != nullptr
            && slice_write->domain
                == SignalUpdateDomain::systemverilog_active
            && slice_write->signal != signal_id) {
            if (!owner.native_boundary_slice_matches_alias_family(
                    slice_write->signal, slice_write->offset, write.width,
                    signal_id)) {
                fallback_invariant_failure(*runtime);
            }
            owner.commit_driver_slice(process_id, slice_write->signal,
                std::move(value), slice_write->offset, origin, false);
        } else if (kind == RegionFrontierEventKindV2::boundary_commit
            && slice_write != nullptr
            && slice_write->domain
                == SignalUpdateDomain::systemverilog_active
            && slice_write->signal == signal_id
            && (slice_write->offset != 0U
                || write.width != owner.signals[signal_id]
                    .initial_value.width())) {
            owner.commit_driver_slice(process_id, slice_write->signal,
                std::move(value), slice_write->offset, origin, false);
        } else {
            owner.commit_driver(
                process_id, signal_id, std::move(value), origin, false);
        }
    } catch (...) {
        // A checked owner commit can throw after transaction/raw-driver effects.
        // The scheduler consumes the current callback; fail-stop the runtime
        // so neither this write nor its body can be replayed.
        runtime->invalidate();
        throw;
    }

    auto& retired_write = runtime->pending_writes[index];
    if (!same_frontier_key(retired_write.commit_key, key)
        || (retired_write.flags & pending_committed) != 0U
        || (retired_write.flags & pending_active) == 0U
        || runtime->frame.pending_write_count == 0U) {
        fallback_invariant_failure(*runtime);
    }
    retired_write.flags &= ~static_cast<std::uint32_t>(pending_active);
    retired_write.flags |= pending_committed;
    --runtime->frame.pending_write_count;
    runtime->frame.current_pending_write = kInvalidFrontierIndex;
}

void Interpreter::Impl::dispatch_generic_frontier_fallback(
    Scheduler& scheduler,
    const RegionFrontierComponentRuntime::GenericFallbackToken& token)
{
    auto* const runtime = token.runtime;
    if (runtime == nullptr || runtime->owner == nullptr) {
        throw std::logic_error { "missing Generic frontier fallback runtime" };
    }
    auto& owner = *runtime->owner;
    struct InvalidateOnException {
        RegionFrontierComponentRuntime* runtime { };
        int exceptions { std::uncaught_exceptions() };
        ~InvalidateOnException()
        {
            if (runtime != nullptr
                && std::uncaught_exceptions() > exceptions) {
                runtime->invalidate();
            }
        }
    } fail_stop { runtime };
    ProcessId process_id { };
    if (!decode_generic_projected_region_payload(token.payload, process_id)
        || runtime->execution_mode
            != RegionFrontierExecutionModeV2::generic_deferred_update
        || !runtime->frame_initialized || !runtime->backend
        || !runtime->backend->executor) {
        fallback_invariant_failure(*runtime);
    }
    const auto& layout = runtime->backend->executor->layout();
    if (layout.execution_mode
            != RegionFrontierExecutionModeV2::generic_deferred_update
        || runtime->generic_queued_members.size() != layout.member_count
        || runtime->generic_queued_ready_words.size()
            != (static_cast<std::size_t>(layout.member_count) + 63U) / 64U) {
        fallback_invariant_failure(*runtime);
    }
    std::size_t member_index = std::numeric_limits<std::size_t>::max();
    for (std::size_t index = 0U; index < layout.member_count; ++index) {
        if (layout.members[index].process_id != process_id) {
            continue;
        }
        if (member_index != std::numeric_limits<std::size_t>::max()) {
            fallback_invariant_failure(*runtime);
        }
        member_index = index;
    }
    if (member_index == std::numeric_limits<std::size_t>::max()) {
        fallback_invariant_failure(*runtime);
    }
    const auto& queued = runtime->generic_queued_members[member_index];
    const auto ready_bit = UINT64_C(1) << (member_index % 64U);
    const auto phase = scheduler.current_phase();
    if (!queued.receipt.valid
        || queued.receipt.phase != SchedulerPhase::active
        || queued.receipt.stable_order != process_id
        || queued.receipt.payload != token.payload
        || queued.static_trigger_mask == 0U
        || (runtime->generic_queued_ready_words[member_index / 64U]
            & ready_bit) == 0U
        || !phase || *phase != SchedulerPhase::active
        || scheduler.now() != queued.receipt.time
        || scheduler.delta() != queued.receipt.delta
        || process_id >= owner.processes.size()) {
        fallback_invariant_failure(*runtime);
    }
    auto& process = owner.get_process(process_id);
    if (!process.queued || !process.waiting_on_static
        || process.static_trigger_mask == 0U
        || process.program().scheduling_domain()
            != ProcessSchedulingDomain::generic) {
        fallback_invariant_failure(*runtime);
    }

    // The scheduler has already selected this compact member using its full
    // stored key. The process mask remains authoritative: it may contain
    // additional triggers merged after this pinned runtime was invalidated.
    // Consume only this member's sidecar, then execute the ordinary checked
    // body once; the compact ticket retains its suffix.
    runtime->clear_generic_queued_member(member_index);
    process.queued = false;
    process.waiting_on_static = false;
    owner.remove_dynamic_wait(process);
    owner.execute(process_id);
}

} // namespace fsim::runtime::simir
