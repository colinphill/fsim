// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_frontier.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fsim::runtime::simir {

/// Version 2 is a sibling ABI, not a reinterpretation of RegionFrontier ABI V1.
/// The JITRuntime ABI is independent and is intentionally unchanged.
inline constexpr std::uint32_t kRegionFrontierAbiVersionV2 = 2U;

/// Fixed schema tag for the typed two-plane Logic4 / four-plane Logic9 contract.
/// The value is numeric and is not serialized as host-endian text.
inline constexpr std::uint32_t kRegionFrontierValuePlaneContractV2
    = 0x324c3950U; // "L9P2"

/// Scheduler task payload tags preserve their V1 bit encoding in V2; this
/// explicit alias keeps the generated V2 path independent of V1 type names.
inline constexpr std::uint32_t kRegionFrontierPayloadKindShiftV2
    = kRegionFrontierPayloadKindShiftV1;
inline constexpr std::uint64_t kRegionFrontierPayloadIndexMaskV2
    = kRegionFrontierPayloadIndexMaskV1;

enum class RegionFrontierEventKindV2 : std::uint32_t;

/// Generic deferred events are staged values, never scheduler payloads. This
/// overload preserves the shared V1 payload encoder call sites for the first
/// three V2 event numbers and emits an intentionally invalid tag for kind 3.
[[nodiscard]] constexpr std::uint64_t encode_region_frontier_payload_v1(
    const RegionFrontierEventKindV2 kind, const std::uint64_t index) noexcept;

enum class RegionFrontierStatusV2 : std::uint32_t {
    quiescent = 0U,
    need_scheduler_keys = 1U,
    cut_before_key = 2U,
    boundary_publication = 3U,
    stopped = 4U,
    decline_before_mutation = 5U,
    stale_generation = 6U,
    yield_before_task = 7U,
    /// The exact borrowed Generic Active prefix has been consumed and its
    /// whole-signal WriteUpdate values are ready for checked host staging.
    /// This status never represents an SV internal or boundary commit.
    generic_update_batch_ready = 8U,
};

/// A plan is specialized for one scheduler publication contract. Generic
/// mode evaluates only generic Active work and returns detached Update values;
/// it never mutates the authoritative value planes in the generated entry.
enum class RegionFrontierExecutionModeV2 : std::uint32_t {
    systemverilog_active = 0U,
    generic_deferred_update = 1U,
};

enum class RegionFrontierValueKindV2 : std::uint32_t {
    logic4 = 0U,
    logic9 = 1U,
};

inline constexpr std::uint32_t kRegionFrontierLogic4PlaneCountV2 = 2U;
inline constexpr std::uint32_t kRegionFrontierLogic9PlaneCountV2 = 4U;

/// The scheduler identity and event bookkeeping records retain their V1 byte
/// layouts and exact semantics. In particular, V2 does not mint keys, change
/// stable ordering, or reinterpret time/delta/round/domain/phase fields.
using RegionFrontierKeyV2 = RegionFrontierKeyV1;
using RegionFrontierSlotV2 = RegionFrontierSlotV1;
using RegionFrontierMemberV2 = RegionFrontierMemberV1;
using RegionFrontierSignalMetadataV2 = RegionFrontierSignalMetadataV1;
using RegionFrontierCommittedSignalV2 = RegionFrontierCommittedSignalV1;
using RegionFrontierSchedulerTaskV2 = RegionFrontierSchedulerTaskV1;
using RegionFrontierStagedEventV2 = RegionFrontierStagedEventV1;
using RegionFrontierFanoutEdgeV2 = RegionFrontierFanoutEdgeV1;
using RegionFrontierMemberLayoutV2 = RegionFrontierMemberLayoutV1;
using RegionFrontierCutV2 = RegionFrontierCutV1;
using RegionFrontierCutKindV2 = RegionFrontierCutKindV1;
/// V2 keeps the V1 event numbers and adds a host-staged generic Update event.
/// `generic_deferred_update` is a staged descriptor only; it is never a
/// Scheduler task payload and never authorizes a direct plane commit.
enum class RegionFrontierEventKindV2 : std::uint32_t {
    member_activation = 0U,
    internal_commit = 1U,
    boundary_commit = 2U,
    generic_deferred_update = 3U,
};

[[nodiscard]] constexpr std::uint64_t encode_region_frontier_payload_v1(
    const RegionFrontierEventKindV2 kind, const std::uint64_t index) noexcept
{
    if (kind == RegionFrontierEventKindV2::generic_deferred_update) {
        return ~std::uint64_t { 0U };
    }
    return (static_cast<std::uint64_t>(kind)
               << kRegionFrontierPayloadKindShiftV2)
        | index;
}
using RegionFrontierMemberFlagsV2 = RegionFrontierMemberFlagsV1;
using RegionFrontierPlaneFlagsV2 = RegionFrontierPlaneFlagsV1;
/// V2 flags duplicate the stable V1 bits and reserve bit 6 for writes that
/// must be staged through the ordinary Generic Update queue.
struct RegionFrontierPendingWriteFlagsV2 final {
    enum : std::uint32_t {
        pending_active = 1U << 0U,
        pending_value_ready = 1U << 1U,
        pending_key_assigned = 1U << 2U,
        pending_internal_target = 1U << 3U,
        pending_boundary_target = 1U << 4U,
        pending_committed = 1U << 5U,
        pending_generic_target = 1U << 6U,
    };
};

inline constexpr std::uint32_t kRegionFrontierGenericWriteFlagsV2
    = RegionFrontierPendingWriteFlagsV2::pending_active
    | RegionFrontierPendingWriteFlagsV2::pending_value_ready
    | RegionFrontierPendingWriteFlagsV2::pending_generic_target;

[[nodiscard]] constexpr std::uint32_t region_frontier_required_plane_count_v2(
    const RegionFrontierValueKindV2 kind) noexcept
{
    switch (kind) {
    case RegionFrontierValueKindV2::logic4:
        return kRegionFrontierLogic4PlaneCountV2;
    case RegionFrontierValueKindV2::logic9:
        return kRegionFrontierLogic9PlaneCountV2;
    }
    return 0U;
}

/// Width zero and unknown kinds are invalid. Plane count is per packed value;
/// word_count is the independent storage extent for each present plane.
[[nodiscard]] constexpr bool region_frontier_value_shape_valid_v2(
    const RegionFrontierValueKindV2 kind,
    const std::uint32_t width,
    const std::uint32_t word_count,
    const std::uint32_t plane_count) noexcept
{
    const auto required_plane_count = region_frontier_required_plane_count_v2(kind);
    if (width == 0U || required_plane_count == 0U
        || plane_count != required_plane_count) {
        return false;
    }
    const auto expected_words
        = (static_cast<std::uint64_t>(width) + 63U) / 64U;
    return expected_words == word_count;
}

/// Canonical Logic9 stores only codes 0..8. For each bit position, p3 may be
/// set only for code 8; any lower-plane bit alongside p3 is reserved (9..15).
[[nodiscard]] constexpr bool region_frontier_logic9_word_is_canonical_v2(
    const std::uint64_t plane0,
    const std::uint64_t plane1,
    const std::uint64_t plane2,
    const std::uint64_t plane3) noexcept
{
    return (plane3 & (plane0 | plane1 | plane2)) == 0U;
}

/// V2 value-bearing plane bindings. `boundary_planes` are the read-only
/// external port value, replacing the two scalar pointers in V1. Internal
/// port references resolve through `port_planes` to current_planes instead.
/// Current, previous/LAST, stored, and owner are independently bound mutable
/// A4 roles. The A4 binder authenticates whether owner aliases stored from its
/// lease layout before publishing a frame. Generated preflight permits exact
/// equality only for the stored/owner pointers of the same signal and plane;
/// it does not grant callers an alias permission. For Logic4, entries [2]
/// and [3] must be null; for Logic9, all four entries for the applicable
/// roles must be non-null. Each pointer addresses exactly word_count words.
/// The A4 binder supplies the two Logic9 offsets separately when creating
/// these pointers; the ABI never assumes a combined allocation.
struct RegionFrontierPlaneV2 {
    std::uint32_t signal_id { };
    std::uint32_t owner_process_id { };
    RegionFrontierValueKindV2 value_kind { RegionFrontierValueKindV2::logic4 };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t plane_count { };
    std::uint32_t flags { };
    std::uint32_t metadata_index { };
    const std::uint64_t* boundary_planes[4] { };
    std::uint64_t* current_planes[4] { };
    std::uint64_t* previous_planes[4] { };
    std::uint64_t* stored_planes[4] { };
    std::uint64_t* owner_planes[4] { };
};

/// Shape validation for a bound signal value. This checks pointer presence,
/// not allocation extent; the binder must also prove each span has word_count
/// elements before it exposes a frame to generated code.
[[nodiscard]] inline bool region_frontier_plane_bindings_valid_v2(
    const RegionFrontierPlaneV2& plane) noexcept
{
    if (!region_frontier_value_shape_valid_v2(
            plane.value_kind, plane.width, plane.word_count, plane.plane_count)) {
        return false;
    }
    const auto internal_flag = static_cast<std::uint32_t>(
        RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
    const auto boundary_flag = static_cast<std::uint32_t>(
        RegionFrontierPlaneFlagsV2::read_only_boundary_port);
    const bool internal = (plane.flags & internal_flag) != 0U;
    const bool boundary = (plane.flags & boundary_flag) != 0U;
    if (plane.flags != internal_flag && plane.flags != boundary_flag) {
        return false;
    }

    for (std::uint32_t index = 0U; index < 4U; ++index) {
        const bool present = index < plane.plane_count;
        const bool boundary_present = present && boundary;
        if ((plane.boundary_planes[index] != nullptr) != boundary_present) {
            return false;
        }
        const bool mutable_present = present && internal;
        if ((plane.current_planes[index] != nullptr) != mutable_present
            || (plane.previous_planes[index] != nullptr) != mutable_present
            || (plane.stored_planes[index] != nullptr) != mutable_present
            || (plane.owner_planes[index] != nullptr) != mutable_present) {
            return false;
        }
    }
    return true;
}

/// A pending write carries all of its value planes. The runtime binds these
/// private buffers before entry; a generated Logic9 write must not silently
/// drop planes 2 and 3. `value_kind`, width, word_count, and plane_count must
/// match both the immutable write site and the destination signal layout.
struct RegionFrontierPendingWriteV2 {
    std::uint32_t member_index { };
    std::uint32_t signal_slot { };
    std::uint32_t source_instruction { };
    std::uint32_t update_kind { };
    std::uint32_t flags { };
    std::uint32_t reserved { };
    RegionFrontierKeyV2 commit_key;
    RegionFrontierKeyV2 origin;
    RegionFrontierValueKindV2 value_kind { RegionFrontierValueKindV2::logic4 };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t plane_count { };
    std::uint64_t* value_planes[4] { };
};

/// Pending values obey the same typed plane-count rule as bound signal roles.
[[nodiscard]] inline bool region_frontier_pending_write_bindings_valid_v2(
    const RegionFrontierPendingWriteV2& pending) noexcept
{
    if (!region_frontier_value_shape_valid_v2(pending.value_kind,
            pending.width, pending.word_count, pending.plane_count)) {
        return false;
    }
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        const bool present = index < pending.plane_count;
        if ((pending.value_planes[index] != nullptr) != present) {
            return false;
        }
    }
    return true;
}

struct RegionFrontierSignalLayoutV2 {
    std::uint32_t signal_id { };
    std::uint32_t owner_process_id { };
    RegionFrontierValueKindV2 value_kind { RegionFrontierValueKindV2::logic4 };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t plane_count { };
    std::uint32_t flags { };
    std::uint32_t metadata_index { };
};

/// One static WriteUpdate lowering site in immutable member/source order.
struct RegionFrontierWriteSiteV2 {
    std::uint32_t member_index { };
    std::uint32_t signal_slot { };
    std::uint32_t source_instruction { };
    std::uint32_t update_kind { };
    std::uint32_t event_kind { };
    RegionFrontierValueKindV2 value_kind { RegionFrontierValueKindV2::logic4 };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t plane_count { };
    std::uint32_t pending_slot { };
};

/// Immutable compiler certificate and frame-allocation recipe. The extra
/// contract tag makes an accidental V1/V2 descriptor mix fail before any
/// typed value plane is read. Existing generation, scheduling, metadata,
/// fanout, and capacity fields retain their V1 meanings. `execution_mode`
/// reuses the old reserved1 word at offset 76; it adds no ABI bytes.
struct RegionFrontierLayoutV2 {
    std::uint32_t abi_version { kRegionFrontierAbiVersionV2 };
    std::uint32_t struct_size { };
    std::uint32_t value_plane_contract { kRegionFrontierValuePlaneContractV2 };
    std::uint32_t reserved0 { };
    std::uint64_t certificate_generation { };
    std::uint64_t component_generation { };
    std::uint32_t member_count { };
    std::uint32_t readiness_word_count { };
    std::uint32_t signal_slot_count { };
    std::uint32_t metadata_count { };
    std::uint32_t fanout_edge_count { };
    std::uint32_t write_site_count { };
    std::uint32_t pending_write_capacity { };
    std::uint32_t staged_event_capacity { };
    std::uint32_t max_commit_fanout_events { };
    std::uint32_t committed_signal_capacity { };
    std::uint32_t reserved_capacity { };
    RegionFrontierExecutionModeV2 execution_mode {
        RegionFrontierExecutionModeV2::systemverilog_active
    };
    const RegionFrontierMemberLayoutV2* members { };
    const RegionFrontierSignalLayoutV2* signals { };
    const RegionFrontierWriteSiteV2* write_sites { };
    const std::uint32_t* max_member_write_counts { };
    const std::uint32_t* max_member_staged_event_counts { };
    const RegionFrontierFanoutEdgeV2* fanout_edges { };
};

/// The persistent V2 frame is intentionally a distinct type. Its first fields
/// identify both its ABI generation and its value-plane contract before any
/// pointer is interpreted. Scheduler keys and metadata have the same semantic
/// layout as V1; only value-bearing descriptors use the V2 typed planes. The
/// generic ACK reuses the old reserved0 word at offset 12; it adds no ABI bytes.
struct RegionFrontierFrameV2 {
    std::uint32_t abi_version { kRegionFrontierAbiVersionV2 };
    std::uint32_t struct_size { };
    std::uint32_t value_plane_contract { kRegionFrontierValuePlaneContractV2 };
    /// Host acknowledgment for a generic deferred Update batch. Zero until
    /// all values and the ordinary Update callback are secured; then exactly
    /// the staged-event count. It is always zero for SV Active mode.
    std::uint32_t generic_update_ack_count { };
    std::uint64_t runtime_generation { };
    std::uint64_t bound_runtime_generation { };
    std::uint64_t certificate_generation { };
    std::uint64_t component_generation { };
    std::uint64_t scheduler_frontier_generation { };
    std::uint32_t member_count { };
    std::uint32_t scheduler_task_count { };
    std::uint32_t scheduler_task_cursor { };
    std::uint32_t scheduler_task_capacity { };
    std::uint32_t readiness_word_count { };
    std::uint32_t signal_slot_count { };
    std::uint32_t metadata_count { };
    std::uint32_t fanout_edge_count { };
    std::uint32_t committed_signal_capacity { };
    std::uint32_t committed_signal_count { };
    std::uint32_t pending_write_capacity { };
    std::uint32_t pending_write_count { };
    std::uint32_t staged_event_capacity { };
    std::uint32_t staged_event_count { };
    std::uint32_t current_member { UINT32_MAX };
    std::uint32_t current_pending_write { UINT32_MAX };
    std::uint32_t current_commit_changed { };
    std::uint32_t saved_body_pc { };
    std::uint64_t* ready_words { };
    RegionFrontierMemberV2* members { };
    const RegionFrontierSchedulerTaskV2* scheduler_tasks { };
    RegionFrontierPlaneV2* planes { };
    RegionFrontierSignalMetadataV2* metadata { };
    const RegionFrontierFanoutEdgeV2* fanout_edges { };
    const RegionFrontierPlaneV2* const* port_planes { };
    RegionFrontierPendingWriteV2* pending_writes { };
    RegionFrontierStagedEventV2* staged_events { };
    RegionFrontierCommittedSignalV2* committed_signals { };
    std::uint64_t* native_frontier_member_dispatches { };
    const std::uint32_t* stop_requested { };
    RegionFrontierSlotV2 slot;
    RegionFrontierCutV2 cut;
};

/// The shared V1 key/slot records encode Generic as process-domain zero and
/// Active as scheduler-phase zero. Keep those values explicit here because
/// the ABI header does not depend on the full scheduler/runtime declarations.
inline constexpr std::uint32_t kRegionFrontierGenericDomainV2 = 0U;
inline constexpr std::uint32_t kRegionFrontierSystemVerilogDomainV2 = 1U;
inline constexpr std::uint32_t kRegionFrontierActivePhaseV2 = 0U;

[[nodiscard]] constexpr bool region_frontier_execution_mode_valid_v2(
    const RegionFrontierExecutionModeV2 mode) noexcept
{
    return mode == RegionFrontierExecutionModeV2::systemverilog_active
        || mode == RegionFrontierExecutionModeV2::generic_deferred_update;
}

[[nodiscard]] constexpr bool region_frontier_status_valid_for_mode_v2(
    const RegionFrontierExecutionModeV2 mode,
    const RegionFrontierStatusV2 status) noexcept
{
    if (!region_frontier_execution_mode_valid_v2(mode)) {
        return false;
    }
    if (status == RegionFrontierStatusV2::generic_update_batch_ready) {
        return mode == RegionFrontierExecutionModeV2::generic_deferred_update;
    }
    if (status == RegionFrontierStatusV2::boundary_publication) {
        return mode == RegionFrontierExecutionModeV2::systemverilog_active;
    }
    if (status == RegionFrontierStatusV2::need_scheduler_keys
        && mode == RegionFrontierExecutionModeV2::generic_deferred_update) {
        return false;
    }
    switch (status) {
    case RegionFrontierStatusV2::quiescent:
    case RegionFrontierStatusV2::need_scheduler_keys:
    case RegionFrontierStatusV2::cut_before_key:
    case RegionFrontierStatusV2::stopped:
    case RegionFrontierStatusV2::decline_before_mutation:
    case RegionFrontierStatusV2::stale_generation:
    case RegionFrontierStatusV2::yield_before_task:
        return true;
    case RegionFrontierStatusV2::boundary_publication:
    case RegionFrontierStatusV2::generic_update_batch_ready:
        return false;
    }
    return false;
}

[[nodiscard]] constexpr bool region_frontier_slot_valid_for_mode_v2(
    const RegionFrontierExecutionModeV2 mode,
    const RegionFrontierSlotV2& slot) noexcept
{
    if (slot.phase != kRegionFrontierActivePhaseV2) {
        return false;
    }
    if (mode == RegionFrontierExecutionModeV2::systemverilog_active) {
        return slot.process_domain == kRegionFrontierSystemVerilogDomainV2;
    }
    return mode == RegionFrontierExecutionModeV2::generic_deferred_update
        && slot.process_domain == kRegionFrontierGenericDomainV2
        && slot.systemverilog_round == 0U;
}

[[nodiscard]] constexpr bool region_frontier_key_matches_slot_v2(
    const RegionFrontierKeyV2& key,
    const RegionFrontierSlotV2& slot) noexcept
{
    return key.time == slot.time && key.delta == slot.delta
        && key.systemverilog_round == slot.systemverilog_round
        && key.process_domain == slot.process_domain
        && key.phase == slot.phase;
}

[[nodiscard]] constexpr bool region_frontier_generic_key_valid_v2(
    const RegionFrontierKeyV2& key,
    const RegionFrontierSlotV2& slot) noexcept
{
    return region_frontier_slot_valid_for_mode_v2(
               RegionFrontierExecutionModeV2::generic_deferred_update, slot)
        && region_frontier_key_matches_slot_v2(key, slot);
}

[[nodiscard]] constexpr bool region_frontier_key_is_zero_v2(
    const RegionFrontierKeyV2& key) noexcept
{
    return key.time == 0U && key.delta == 0U
        && key.systemverilog_round == 0U && key.stable_order == 0U
        && key.sequence == 0U
        && key.process_domain == 0U && key.phase == 0U;
}

[[nodiscard]] constexpr bool region_frontier_generic_flags_valid_v2(
    const std::uint32_t flags) noexcept
{
    return flags == kRegionFrontierGenericWriteFlagsV2;
}

/// Both legal states are atomic: an entry result has no ACK, while host
/// staging ACKs the entire batch. A partial count can never retire work.
[[nodiscard]] constexpr bool region_frontier_generic_ack_counts_valid_v2(
    const std::uint32_t pending_count,
    const std::uint32_t event_count,
    const std::uint32_t ack_count) noexcept
{
    return pending_count == event_count
        && (ack_count == 0U || ack_count == event_count);
}

[[nodiscard]] constexpr bool region_frontier_generic_batch_ready_counts_valid_v2(
    const std::uint32_t pending_count,
    const std::uint32_t event_count,
    const std::uint32_t ack_count) noexcept
{
    return event_count != 0U && pending_count == event_count
        && ack_count == 0U;
}

[[nodiscard]] constexpr bool region_frontier_generic_retirement_ack_valid_v2(
    const std::uint32_t pending_count,
    const std::uint32_t event_count,
    const std::uint32_t ack_count) noexcept
{
    return event_count != 0U && pending_count == event_count
        && ack_count == event_count;
}

[[nodiscard]] constexpr bool region_frontier_generic_pending_header_valid_v2(
    const std::uint32_t flags, const std::uint32_t reserved,
    const RegionFrontierKeyV2& commit_key,
    const RegionFrontierKeyV2& origin,
    const RegionFrontierSlotV2& slot) noexcept
{
    return region_frontier_generic_flags_valid_v2(flags)
        && reserved == 0U && region_frontier_key_is_zero_v2(commit_key)
        && region_frontier_generic_key_valid_v2(origin, slot);
}

[[nodiscard]] constexpr bool region_frontier_generic_event_matches_site_v2(
    const RegionFrontierStagedEventV2& event,
    const RegionFrontierWriteSiteV2& site,
    const RegionFrontierSlotV2& slot) noexcept
{
    return event.kind == static_cast<std::uint32_t>(
               RegionFrontierEventKindV2::generic_deferred_update)
        && event.descriptor_index == site.pending_slot
        && event.stable_order == event.origin.stable_order
        && region_frontier_generic_key_valid_v2(event.origin, slot)
        && site.event_kind == static_cast<std::uint32_t>(
               RegionFrontierEventKindV2::generic_deferred_update)
        && site.update_kind == 0U
        && region_frontier_value_shape_valid_v2(site.value_kind,
            site.width, site.word_count, site.plane_count);
}

/// Joins the immutable site and member mapping with one staged event and
/// pending row. This authenticates descriptor identity and exact activation
/// origin for a shape-valid Logic4 or Logic9 value, but not pointed-to
/// allocation extents, aliasing, or word canonicality; the runtime must
/// validate those bounds before reading words.
[[nodiscard]] inline bool region_frontier_generic_write_descriptor_matches_v2(
    const RegionFrontierStagedEventV2& event,
    const RegionFrontierPendingWriteV2& pending,
    const RegionFrontierWriteSiteV2& site,
    const RegionFrontierSignalLayoutV2& signal,
    const RegionFrontierMemberLayoutV2& member,
    const std::uint32_t selected_member_index,
    const std::uint32_t selected_signal_slot,
    const RegionFrontierKeyV2& activation_origin,
    const RegionFrontierSlotV2& slot) noexcept
{
    const auto key_equal = [](const RegionFrontierKeyV2& left,
                              const RegionFrontierKeyV2& right) noexcept {
        return left.time == right.time && left.delta == right.delta
            && left.systemverilog_round == right.systemverilog_round
            && left.stable_order == right.stable_order
            && left.sequence == right.sequence
            && left.process_domain == right.process_domain
            && left.phase == right.phase;
    };
    return region_frontier_generic_event_matches_site_v2(event, site, slot)
        && region_frontier_generic_pending_header_valid_v2(pending.flags,
            pending.reserved, pending.commit_key, pending.origin, slot)
        && region_frontier_pending_write_bindings_valid_v2(pending)
        && key_equal(event.origin, activation_origin)
        && key_equal(pending.origin, activation_origin)
        && pending.member_index == site.member_index
        && pending.signal_slot == site.signal_slot
        && pending.source_instruction == site.source_instruction
        && pending.update_kind == site.update_kind
        && pending.value_kind == site.value_kind
        && pending.width == site.width
        && pending.word_count == site.word_count
        && pending.plane_count == site.plane_count
        && selected_member_index == site.member_index
        && selected_signal_slot == site.signal_slot
        && site.pending_slot >= member.first_write_site
        && static_cast<std::uint64_t>(site.pending_slot)
            < static_cast<std::uint64_t>(member.first_write_site)
                + member.write_site_count
        && signal.value_kind == site.value_kind
        && signal.width == site.width
        && signal.word_count == site.word_count
        && signal.plane_count == site.plane_count
        && signal.flags
            == RegionFrontierPlaneFlagsV2::read_only_boundary_port;
}

/// Generated/native entry code checks only these first eight bytes before it
/// loads the contract tag at offset 8. The scalar overload below assumes its
/// arguments have already been safely read; it must not be called by loading
/// the tag from an untrusted or potentially truncated frame first.
[[nodiscard]] constexpr bool region_frontier_frame_prefix_valid_v2(
    const std::uint32_t abi_version,
    const std::uint32_t struct_size) noexcept
{
    return abi_version == kRegionFrontierAbiVersionV2
        && struct_size == sizeof(RegionFrontierFrameV2);
}

[[nodiscard]] constexpr bool region_frontier_layout_prefix_valid_v2(
    const std::uint32_t abi_version,
    const std::uint32_t struct_size) noexcept
{
    return abi_version == kRegionFrontierAbiVersionV2
        && struct_size == sizeof(RegionFrontierLayoutV2);
}

/// Full typed-header validation after the ABI version and size prefix pass.
/// A V1 frame (or a truncated/future-layout V2 frame) is rejected before
/// pointer loads; V1 and V2 entry pointers are not type-compatible.
[[nodiscard]] constexpr bool region_frontier_frame_header_valid_v2(
    const std::uint32_t abi_version,
    const std::uint32_t struct_size,
    const std::uint32_t value_plane_contract) noexcept
{
    return region_frontier_frame_prefix_valid_v2(abi_version, struct_size)
        && value_plane_contract == kRegionFrontierValuePlaneContractV2;
}

[[nodiscard]] constexpr bool region_frontier_layout_header_valid_v2(
    const std::uint32_t abi_version,
    const std::uint32_t struct_size,
    const std::uint32_t value_plane_contract) noexcept
{
    return region_frontier_layout_prefix_valid_v2(abi_version, struct_size)
        && value_plane_contract == kRegionFrontierValuePlaneContractV2;
}

[[nodiscard]] constexpr bool region_frontier_frame_header_valid_v2(
    const RegionFrontierFrameV2& frame) noexcept
{
    return region_frontier_frame_header_valid_v2(
        frame.abi_version, frame.struct_size, frame.value_plane_contract);
}

[[nodiscard]] constexpr bool region_frontier_layout_header_valid_v2(
    const RegionFrontierLayoutV2& layout) noexcept
{
    return region_frontier_layout_header_valid_v2(
        layout.abi_version, layout.struct_size, layout.value_plane_contract);
}

using RegionFrontierStepEntryV2 = RegionFrontierStatusV2 (*)(
    RegionFrontierFrameV2*) noexcept;

static_assert(std::is_standard_layout_v<RegionFrontierPlaneV2>);
static_assert(std::is_trivially_copyable_v<RegionFrontierPlaneV2>);
static_assert(std::is_standard_layout_v<RegionFrontierPendingWriteV2>);
static_assert(std::is_trivially_copyable_v<RegionFrontierPendingWriteV2>);
static_assert(std::is_standard_layout_v<RegionFrontierSignalLayoutV2>);
static_assert(std::is_trivially_copyable_v<RegionFrontierSignalLayoutV2>);
static_assert(std::is_standard_layout_v<RegionFrontierWriteSiteV2>);
static_assert(std::is_trivially_copyable_v<RegionFrontierWriteSiteV2>);
static_assert(std::is_standard_layout_v<RegionFrontierLayoutV2>);
static_assert(std::is_trivially_copyable_v<RegionFrontierLayoutV2>);
static_assert(std::is_standard_layout_v<RegionFrontierFrameV2>);
static_assert(std::is_trivially_copyable_v<RegionFrontierFrameV2>);
static_assert(!std::is_same_v<RegionFrontierPlaneV1, RegionFrontierPlaneV2>);
static_assert(!std::is_same_v<RegionFrontierPendingWriteV1,
    RegionFrontierPendingWriteV2>);
static_assert(!std::is_same_v<RegionFrontierFrameV1, RegionFrontierFrameV2>);
static_assert(!std::is_convertible_v<RegionFrontierStepEntryV1,
    RegionFrontierStepEntryV2>);
static_assert(sizeof(RegionFrontierValueKindV2) == sizeof(std::uint32_t));
static_assert(sizeof(RegionFrontierExecutionModeV2) == sizeof(std::uint32_t));
static_assert(sizeof(RegionFrontierEventKindV2) == sizeof(std::uint32_t));
static_assert(static_cast<std::uint32_t>(RegionFrontierValueKindV2::logic4)
    == 0U);
static_assert(static_cast<std::uint32_t>(RegionFrontierValueKindV2::logic9)
    == 1U);
static_assert(static_cast<std::uint32_t>(RegionFrontierExecutionModeV2::systemverilog_active)
    == 0U);
static_assert(static_cast<std::uint32_t>(RegionFrontierExecutionModeV2::generic_deferred_update)
    == 1U);
static_assert(static_cast<std::uint32_t>(RegionFrontierEventKindV2::member_activation)
    == static_cast<std::uint32_t>(RegionFrontierEventKindV1::member_activation));
static_assert(static_cast<std::uint32_t>(RegionFrontierEventKindV2::internal_commit)
    == static_cast<std::uint32_t>(RegionFrontierEventKindV1::internal_commit));
static_assert(static_cast<std::uint32_t>(RegionFrontierEventKindV2::boundary_commit)
    == static_cast<std::uint32_t>(RegionFrontierEventKindV1::boundary_commit));
static_assert(static_cast<std::uint32_t>(RegionFrontierEventKindV2::generic_deferred_update)
    == 3U);
static_assert(static_cast<std::uint32_t>(
    RegionFrontierPendingWriteFlagsV2::pending_active)
    == static_cast<std::uint32_t>(
        RegionFrontierPendingWriteFlagsV1::pending_active));
static_assert(static_cast<std::uint32_t>(
    RegionFrontierPendingWriteFlagsV2::pending_value_ready)
    == static_cast<std::uint32_t>(
        RegionFrontierPendingWriteFlagsV1::pending_value_ready));
static_assert(static_cast<std::uint32_t>(
    RegionFrontierPendingWriteFlagsV2::pending_key_assigned)
    == static_cast<std::uint32_t>(
        RegionFrontierPendingWriteFlagsV1::pending_key_assigned));
static_assert(static_cast<std::uint32_t>(
    RegionFrontierPendingWriteFlagsV2::pending_internal_target)
    == static_cast<std::uint32_t>(
        RegionFrontierPendingWriteFlagsV1::pending_internal_target));
static_assert(static_cast<std::uint32_t>(
    RegionFrontierPendingWriteFlagsV2::pending_boundary_target)
    == static_cast<std::uint32_t>(
        RegionFrontierPendingWriteFlagsV1::pending_boundary_target));
static_assert(static_cast<std::uint32_t>(
    RegionFrontierPendingWriteFlagsV2::pending_committed)
    == static_cast<std::uint32_t>(
        RegionFrontierPendingWriteFlagsV1::pending_committed));
static_assert(RegionFrontierPendingWriteFlagsV2::pending_generic_target
    == (1U << 6U));

} // namespace fsim::runtime::simir
