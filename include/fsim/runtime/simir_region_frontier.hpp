// SPDX-License-Identifier: Apache-2.0
// Shared scheduler and event records used by the typed native-frontier ABI.
// The V2 header aliases these stable records and defines typed planes/frame.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fsim::runtime::simir {

inline constexpr std::uint32_t kRegionFrontierAbiVersionV1 = 1U;

enum class RegionFrontierStatusV1 : std::uint32_t {
    quiescent = 0U,
    need_scheduler_keys = 1U,
    cut_before_key = 2U,
    boundary_publication = 3U,
    stopped = 4U,
    decline_before_mutation = 5U,
    stale_generation = 6U,
    yield_before_task = 7U,
};

enum RegionFrontierMemberFlagsV1 : std::uint32_t {
    waiting_on_static = 1U << 0U,
    queued = 1U << 1U,
    executing = 1U << 2U,
    queued_key_valid = 1U << 3U,
    pending_activation = 1U << 4U,
};

/// A complete scheduler identity. Member tasks in one offered frontier share
/// the slot fields; stable_order and sequence remain scheduler-authored.
struct RegionFrontierKeyV1 {
    std::uint64_t time { };
    std::uint64_t delta { };
    std::uint64_t systemverilog_round { };
    std::uint64_t stable_order { };
    std::uint64_t sequence { };
    std::uint32_t process_domain { };
    std::uint32_t phase { };
};

struct RegionFrontierSlotV1 {
    std::uint64_t time { };
    std::uint64_t delta { };
    std::uint64_t systemverilog_round { };
    std::uint32_t process_domain { };
    std::uint32_t phase { };
};

/// Indexed by immutable compiled member order. Trigger masks are maintained
/// even while a process is not waiting or is already queued. The queued key
/// is not replaced by a later transition that coalesces into the process.
struct RegionFrontierMemberV1 {
    std::uint32_t process_id { };
    std::uint32_t flags { };
    std::uint64_t static_trigger_mask { };
    RegionFrontierKeyV1 queued_key;
    RegionFrontierKeyV1 activation_origin;
    RegionFrontierKeyV1 pending_activation_origin;
};

enum RegionFrontierPlaneFlagsV1 : std::uint32_t {
    certified_internal_single_owner = 1U << 0U,
    read_only_boundary_port = 1U << 1U,
};

/// Internal current/LAST/stored/owner pointers are mutable only for a
/// certified single-owner Logic4 slot. Boundary inputs stay read-only.
struct RegionFrontierPlaneV1 {
    std::uint32_t signal_id { };
    std::uint32_t owner_process_id { };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t flags { };
    std::uint32_t metadata_index { };
    const std::uint64_t* boundary_aval { };
    const std::uint64_t* boundary_bval { };
    std::uint64_t* current_aval { };
    std::uint64_t* current_bval { };
    std::uint64_t* previous_aval { };
    std::uint64_t* previous_bval { };
    std::uint64_t* stored_aval { };
    std::uint64_t* stored_bval { };
    std::uint64_t* owner_aval { };
    std::uint64_t* owner_bval { };
};

/// Mirrors the authoritative runtime event/transaction metadata. Native
/// internal commits update this in scheduler-key order, including unchanged
/// transactions and each changed-value revision.
struct RegionFrontierSignalMetadataV1 {
    std::uint64_t event_time { };
    std::uint64_t event_delta { };
    std::uint64_t transaction_time { };
    std::uint64_t transaction_delta { };
    std::uint64_t value_revision { };
    std::uint64_t systemverilog_round { };
    std::uint32_t event_process_domain { };
    std::uint32_t event_phase { };
    std::uint8_t event_valid { };
    std::uint8_t transaction_valid { };
    std::uint8_t reserved[6] { };
};

/// Ordered diagnostic notifications for native internal transactions. Duplicate
/// signal slots are retained because one signal can commit more than once in a
/// scheduler prefix. `changed` records current-value change for event/fanout
/// behavior; `state_changed` records any current/stored/owner role-value
/// change. Each record also proves a transaction occurred, so the host marks
/// transaction metadata dirty even when both bits are zero. The host drains
/// this log for scheduler bookkeeping only, never to replay value publication.
struct RegionFrontierCommittedSignalV1 {
    std::uint32_t signal_slot { };
    std::uint32_t changed { };
    std::uint32_t state_changed { };
};

enum class RegionFrontierEventKindV1 : std::uint32_t {
    member_activation = 0U,
    internal_commit = 1U,
    boundary_commit = 2U,
};

/// Layout-compatible prefix of SchedulerBatchFrontierEntry. This storage is
/// borrowed from the active scheduler callback; the generated entry consumes
/// only [scheduler_task_cursor, scheduler_task_count) and never sorts,
/// synthesizes a following key, or infers that the scheduler queue is empty.
struct RegionFrontierSchedulerTaskV1 {
    std::uint64_t stable_order { };
    std::uint64_t sequence { };
    std::uint64_t payload { };
};

inline constexpr std::uint32_t kRegionFrontierPayloadKindShiftV1 = 56U;
inline constexpr std::uint64_t kRegionFrontierPayloadIndexMaskV1
    = (UINT64_C(1) << kRegionFrontierPayloadKindShiftV1) - 1U;

[[nodiscard]] constexpr std::uint64_t encode_region_frontier_payload_v1(
    const RegionFrontierEventKindV1 kind, const std::uint64_t index) noexcept
{
    return (static_cast<std::uint64_t>(kind)
            << kRegionFrontierPayloadKindShiftV1)
        | index;
}

enum RegionFrontierPendingWriteFlagsV1 : std::uint32_t {
    pending_active = 1U << 0U,
    pending_value_ready = 1U << 1U,
    pending_key_assigned = 1U << 2U,
    pending_internal_target = 1U << 3U,
    pending_boundary_target = 1U << 4U,
    pending_committed = 1U << 5U,
};

/// A private value plus its original source/owner. The host key issuer fills
/// commit_key only after an atomic compact scheduler reservation succeeds.
struct RegionFrontierPendingWriteV1 {
    std::uint32_t member_index { };
    std::uint32_t signal_slot { };
    std::uint32_t source_instruction { };
    std::uint32_t update_kind { };
    std::uint32_t flags { };
    std::uint32_t reserved { };
    RegionFrontierKeyV1 commit_key;
    RegionFrontierKeyV1 origin;
    std::uint64_t* aval { };
    std::uint64_t* bval { };
    std::uint32_t width { };
    std::uint32_t word_count { };
};

/// Pending fresh-key work is appended in source enqueue order. The wrapper
/// stable-sorts by stable_order (preserving same-owner order) before its one
/// atomic compact reservation. Descriptor indices address members or pending
/// write slots according to kind.
struct RegionFrontierStagedEventV1 {
    std::uint32_t kind { };
    std::uint32_t descriptor_index { };
    std::uint64_t stable_order { };
    RegionFrontierKeyV1 origin;
};

struct RegionFrontierFanoutEdgeV1 {
    std::uint32_t signal_slot { };
    std::uint32_t member_index { };
    std::uint64_t trigger_mask { };
};

struct RegionFrontierMemberLayoutV1 {
    std::uint32_t process_id { };
    std::uint32_t first_write_site { };
    std::uint32_t write_site_count { };
    std::uint32_t max_pending_writes { };
    std::uint32_t max_staged_events { };
    std::uint32_t reserved[3] { };
};

struct RegionFrontierSignalLayoutV1 {
    std::uint32_t signal_id { };
    std::uint32_t owner_process_id { };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t flags { };
    std::uint32_t metadata_index { };
};

/// One static WriteUpdate lowering site in immutable member/source order.
struct RegionFrontierWriteSiteV1 {
    std::uint32_t member_index { };
    std::uint32_t signal_slot { };
    std::uint32_t source_instruction { };
    std::uint32_t update_kind { };
    std::uint32_t event_kind { };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t pending_slot { };
};

/// Immutable compiler certificate and frame-allocation recipe. Descriptor
/// pointers are compiler-owned and remain valid with the executor; runtime
/// binds the per-instance A4 planes into mutable RegionFrontierPlaneV1 slots.
struct RegionFrontierLayoutV1 {
    std::uint32_t abi_version { kRegionFrontierAbiVersionV1 };
    std::uint32_t struct_size { };
    std::uint64_t certificate_generation { };
    std::uint64_t component_generation { };
    std::uint32_t member_count { };
    std::uint32_t readiness_word_count { };
    std::uint32_t plane_count { };
    std::uint32_t metadata_count { };
    std::uint32_t fanout_edge_count { };
    std::uint32_t write_site_count { };
    std::uint32_t pending_write_capacity { };
    std::uint32_t staged_event_capacity { };
    std::uint32_t max_commit_fanout_events { };
    std::uint32_t committed_signal_capacity { };
    std::uint32_t reserved_capacity { };
    const RegionFrontierMemberLayoutV1* members { };
    const RegionFrontierSignalLayoutV1* signals { };
    const RegionFrontierWriteSiteV1* write_sites { };
    const std::uint32_t* max_member_write_counts { };
    const std::uint32_t* max_member_staged_event_counts { };
    const RegionFrontierFanoutEdgeV1* fanout_edges { };
};

enum class RegionFrontierCutKindV1 : std::uint8_t {
    unknown = 0U,
    same_slot_key = 1U,
    closed_prefix = 2U,
};

/// `closed_prefix` authenticates only the borrowed task span; it makes no
/// statement about later same-slot work in the scheduler. The entry may
/// consume no task beyond this span, including newly staged work until the
/// scheduler issues it in a fresh frontier. `same_slot_key` supplies the full
/// external key before which the step must yield. The scheduler frontier
/// generation is separate from component/certificate generations.
struct RegionFrontierCutV1 {
    std::uint64_t scheduler_frontier_generation { };
    RegionFrontierKeyV1 next_key;
    RegionFrontierCutKindV1 kind { RegionFrontierCutKindV1::unknown };
    std::uint8_t reserved[7] { };
};

/// Persistent preallocated state for one certified component. The scheduler
/// task span is borrowed only for this call; staged events and values survive
/// a stop or failed atomic key reservation for exact retry without body replay.
struct RegionFrontierFrameV1 {
    std::uint32_t abi_version { kRegionFrontierAbiVersionV1 };
    std::uint32_t struct_size { };
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
    std::uint32_t plane_count { };
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
    RegionFrontierMemberV1* members { };
    const RegionFrontierSchedulerTaskV1* scheduler_tasks { };
    RegionFrontierPlaneV1* planes { };
    RegionFrontierSignalMetadataV1* metadata { };
    const RegionFrontierFanoutEdgeV1* fanout_edges { };
    const RegionFrontierPlaneV1* const* port_planes { };
    RegionFrontierPendingWriteV1* pending_writes { };
    RegionFrontierStagedEventV1* staged_events { };
    RegionFrontierCommittedSignalV1* committed_signals { };
    std::uint64_t* native_frontier_member_dispatches { };
    const std::uint32_t* stop_requested { };
    RegionFrontierSlotV1 slot;
    RegionFrontierCutV1 cut;
};

/// Generated callback entry is emitted `nounwind`; host callbacks are not
/// emitted into it. boundary_publication leaves the current boundary task and
/// pending descriptor untouched until the host acknowledges a successful
/// publication by setting pending_committed.
using RegionFrontierStepEntryV1 = RegionFrontierStatusV1 (*)(
    RegionFrontierFrameV1*) noexcept;

static_assert(std::is_standard_layout_v<RegionFrontierKeyV1>);
static_assert(std::is_trivially_copyable_v<RegionFrontierKeyV1>);
static_assert(std::is_standard_layout_v<RegionFrontierFrameV1>);
static_assert(std::is_trivially_copyable_v<RegionFrontierFrameV1>);
static_assert(sizeof(RegionFrontierSchedulerTaskV1) == 24U);
static_assert(offsetof(RegionFrontierSchedulerTaskV1, sequence) == 8U);
static_assert(offsetof(RegionFrontierSchedulerTaskV1, payload) == 16U);
static_assert(offsetof(RegionFrontierFrameV1, ready_words)
    > offsetof(RegionFrontierFrameV1, saved_body_pc));

} // namespace fsim::runtime::simir
