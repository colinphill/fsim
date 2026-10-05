// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_frontier.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace frontier = fsim::runtime::simir;

// The generated LLVM entry uses byte offsets and fixed-width loads. The
// supported Windows and Linux ABIs use 8-byte pointers, size_t, and uint64_t.
static_assert(sizeof(void*) == 8U, "frontier ABI requires 64-bit pointers");
static_assert(sizeof(std::size_t) == 8U, "frontier ABI requires 64-bit size_t");
static_assert(sizeof(std::uint32_t) == 4U, "uint32_t width changed");
static_assert(sizeof(std::uint64_t) == 8U, "uint64_t width changed");
static_assert(alignof(std::size_t) == 8U,
    "frontier ABI size_t alignment changed");
static_assert(alignof(void*) == 8U, "frontier ABI pointer alignment changed");
static_assert(alignof(std::uint64_t) == 8U,
    "frontier ABI word alignment changed");

#define FSIM_FRONTIER_ASSERT_LAYOUT(type) \
    static_assert(std::is_standard_layout_v<type>, \
        #type " must remain standard-layout for LLVM offsetof"); \
    static_assert(std::is_trivially_copyable_v<type>, \
        #type " must remain trivially copyable across the generated entry")
#define FSIM_FRONTIER_ASSERT_SIZE(type, expected) \
    static_assert(sizeof(type) == (expected), #type " ABI size changed")
#define FSIM_FRONTIER_ASSERT_OFFSET(type, member, expected) \
    static_assert(offsetof(type, member) == (expected), \
        #type "." #member " ABI offset changed")

FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierKeyV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierSlotV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierMemberV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierPlaneV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierSignalMetadataV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierCommittedSignalV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierSchedulerTaskV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierPendingWriteV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierStagedEventV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierFanoutEdgeV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierMemberLayoutV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierSignalLayoutV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierWriteSiteV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierLayoutV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierCutV1);
FSIM_FRONTIER_ASSERT_LAYOUT(frontier::RegionFrontierFrameV1);

FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierKeyV1, 48U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierSlotV1, 32U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierMemberV1, 160U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierPlaneV1, 104U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierSignalMetadataV1, 64U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierCommittedSignalV1, 12U);
static_assert(alignof(frontier::RegionFrontierCommittedSignalV1) == 4U,
    "committed signal record alignment changed");
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierSchedulerTaskV1, 24U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierPendingWriteV1, 144U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierStagedEventV1, 64U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierFanoutEdgeV1, 16U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierMemberLayoutV1, 32U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierSignalLayoutV1, 24U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierWriteSiteV1, 32U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierLayoutV1, 120U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierCutV1, 64U);
FSIM_FRONTIER_ASSERT_SIZE(frontier::RegionFrontierFrameV1, 312U);

FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierKeyV1, time, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierKeyV1, delta, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierKeyV1, systemverilog_round, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierKeyV1, stable_order, 24U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierKeyV1, sequence, 32U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierKeyV1, process_domain, 40U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierKeyV1, phase, 44U);

FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierSlotV1, time, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierSlotV1, delta, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSlotV1, systemverilog_round, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSlotV1, process_domain, 24U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierSlotV1, phase, 28U);

FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierMemberV1, process_id, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierMemberV1, flags, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberV1, static_trigger_mask, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberV1, queued_key, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberV1, activation_origin, 64U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberV1, pending_activation_origin, 112U);

FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierPlaneV1, signal_id, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, owner_process_id, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierPlaneV1, width, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierPlaneV1, word_count, 12U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierPlaneV1, flags, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, metadata_index, 20U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, boundary_aval, 24U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, boundary_bval, 32U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, current_aval, 40U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, current_bval, 48U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, previous_aval, 56U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, previous_bval, 64U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, stored_aval, 72U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, stored_bval, 80U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, owner_aval, 88U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV1, owner_bval, 96U);

FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, event_time, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, event_delta, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, transaction_time, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, transaction_delta, 24U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, value_revision, 32U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, systemverilog_round, 40U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, event_process_domain, 48U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, event_phase, 52U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, event_valid, 56U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, transaction_valid, 57U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalMetadataV1, reserved, 58U);

FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierCommittedSignalV1, signal_slot, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierCommittedSignalV1, changed, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierCommittedSignalV1, state_changed, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSchedulerTaskV1, stable_order, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSchedulerTaskV1, sequence, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSchedulerTaskV1, payload, 16U);

FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, member_index, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, signal_slot, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, source_instruction, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, update_kind, 12U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierPendingWriteV1, flags, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, reserved, 20U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, commit_key, 24U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, origin, 72U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, aval, 120U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, bval, 128U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierPendingWriteV1, width, 136U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV1, word_count, 140U);

FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierStagedEventV1, kind, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierStagedEventV1, descriptor_index, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierStagedEventV1, stable_order, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierStagedEventV1, origin, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFanoutEdgeV1, signal_slot, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFanoutEdgeV1, member_index, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFanoutEdgeV1, trigger_mask, 8U);

FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberLayoutV1, process_id, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberLayoutV1, first_write_site, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberLayoutV1, write_site_count, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberLayoutV1, max_pending_writes, 12U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberLayoutV1, max_staged_events, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierMemberLayoutV1, reserved, 20U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV1, signal_id, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV1, owner_process_id, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV1, width, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV1, word_count, 12U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV1, flags, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV1, metadata_index, 20U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV1, member_index, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV1, signal_slot, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV1, source_instruction, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV1, update_kind, 12U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV1, event_kind, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV1, width, 20U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV1, word_count, 24U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV1, pending_slot, 28U);

FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, abi_version, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, struct_size, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, certificate_generation, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, component_generation, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, member_count, 24U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, readiness_word_count, 28U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, plane_count, 32U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, metadata_count, 36U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, fanout_edge_count, 40U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, write_site_count, 44U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, pending_write_capacity, 48U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, staged_event_capacity, 52U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, max_commit_fanout_events, 56U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, committed_signal_capacity, 60U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, reserved_capacity, 64U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, members, 72U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, signals, 80U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, write_sites, 88U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, max_member_write_counts, 96U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, max_member_staged_event_counts, 104U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV1, fanout_edges, 112U);

FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierCutV1,
    scheduler_frontier_generation, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierCutV1, next_key, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierCutV1, kind, 56U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierCutV1, reserved, 57U);

FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, abi_version, 0U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, struct_size, 4U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, runtime_generation, 8U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, bound_runtime_generation, 16U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, certificate_generation, 24U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, component_generation, 32U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, scheduler_frontier_generation, 40U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, member_count, 48U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, scheduler_task_count, 52U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, scheduler_task_cursor, 56U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, scheduler_task_capacity, 60U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, readiness_word_count, 64U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, plane_count, 68U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, metadata_count, 72U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, fanout_edge_count, 76U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, committed_signal_capacity, 80U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, committed_signal_count, 84U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, pending_write_capacity, 88U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, pending_write_count, 92U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, staged_event_capacity, 96U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, staged_event_count, 100U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, current_member, 104U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, current_pending_write, 108U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, current_commit_changed, 112U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, saved_body_pc, 116U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, ready_words, 120U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, members, 128U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, scheduler_tasks, 136U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, planes, 144U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, metadata, 152U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, fanout_edges, 160U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, port_planes, 168U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, pending_writes, 176U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, staged_events, 184U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, committed_signals, 192U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, native_frontier_member_dispatches, 200U);
FSIM_FRONTIER_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV1, stop_requested, 208U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, slot, 216U);
FSIM_FRONTIER_ASSERT_OFFSET(frontier::RegionFrontierFrameV1, cut, 248U);

static_assert(frontier::kRegionFrontierAbiVersionV1 == 1U,
    "region frontier ABI version changed without updating this test");
static_assert(sizeof(frontier::RegionFrontierStatusV1) == 4U);
static_assert(sizeof(frontier::RegionFrontierCutKindV1) == 1U);
static_assert(sizeof(frontier::RegionFrontierEventKindV1) == 4U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV1::quiescent) == 0U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV1::need_scheduler_keys) == 1U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV1::cut_before_key) == 2U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV1::boundary_publication) == 3U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV1::stopped) == 4U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV1::decline_before_mutation) == 5U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV1::stale_generation) == 6U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV1::yield_before_task) == 7U);
static_assert(static_cast<std::uint8_t>(frontier::RegionFrontierCutKindV1::unknown) == 0U);
static_assert(static_cast<std::uint8_t>(frontier::RegionFrontierCutKindV1::same_slot_key) == 1U);
static_assert(static_cast<std::uint8_t>(frontier::RegionFrontierCutKindV1::closed_prefix) == 2U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierEventKindV1::member_activation) == 0U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierEventKindV1::internal_commit) == 1U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierEventKindV1::boundary_commit) == 2U);
static_assert(frontier::RegionFrontierMemberFlagsV1::waiting_on_static == 1U);
static_assert(frontier::RegionFrontierMemberFlagsV1::queued == 2U);
static_assert(frontier::RegionFrontierMemberFlagsV1::executing == 4U);
static_assert(frontier::RegionFrontierMemberFlagsV1::queued_key_valid == 8U);
static_assert(frontier::RegionFrontierMemberFlagsV1::pending_activation == 16U);
static_assert(
    frontier::RegionFrontierPlaneFlagsV1::certified_internal_single_owner
    == 1U);
static_assert(frontier::RegionFrontierPlaneFlagsV1::read_only_boundary_port
    == 2U);
static_assert(frontier::RegionFrontierPendingWriteFlagsV1::pending_active == 1U);
static_assert(
    frontier::RegionFrontierPendingWriteFlagsV1::pending_value_ready == 2U);
static_assert(
    frontier::RegionFrontierPendingWriteFlagsV1::pending_key_assigned == 4U);
static_assert(
    frontier::RegionFrontierPendingWriteFlagsV1::pending_internal_target == 8U);
static_assert(
    frontier::RegionFrontierPendingWriteFlagsV1::pending_boundary_target == 16U);
static_assert(
    frontier::RegionFrontierPendingWriteFlagsV1::pending_committed == 32U);
static_assert(frontier::kRegionFrontierPayloadKindShiftV1 == 56U);
static_assert(frontier::kRegionFrontierPayloadIndexMaskV1 == UINT64_C(0x00FFFFFFFFFFFFFF));
static_assert(frontier::RegionFrontierFrameV1{}.abi_version
    == frontier::kRegionFrontierAbiVersionV1);
static_assert(frontier::RegionFrontierLayoutV1{}.abi_version
    == frontier::kRegionFrontierAbiVersionV1);

using ExpectedRegionFrontierStepEntryV1 =
    frontier::RegionFrontierStatusV1 (*)(frontier::RegionFrontierFrameV1*) noexcept;
static_assert(std::is_same_v<frontier::RegionFrontierStepEntryV1,
    ExpectedRegionFrontierStepEntryV1>,
    "generated entry must keep its typed, noexcept frame ABI");
static_assert(std::is_nothrow_invocable_r_v<frontier::RegionFrontierStatusV1,
    frontier::RegionFrontierStepEntryV1, frontier::RegionFrontierFrameV1*>,
    "generated entry must remain callable without exceptions");

int main()
{
    return 0;
}

#undef FSIM_FRONTIER_ASSERT_OFFSET
#undef FSIM_FRONTIER_ASSERT_SIZE
#undef FSIM_FRONTIER_ASSERT_LAYOUT
