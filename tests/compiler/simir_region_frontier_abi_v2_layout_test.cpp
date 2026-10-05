// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir_region_frontier.hpp"
#include "fsim/runtime/simir_region_frontier_v2.hpp"

#include <array>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace frontier = fsim::runtime::simir;

static_assert(sizeof(void*) == 8U, "frontier ABI requires 64-bit pointers");
static_assert(sizeof(std::size_t) == 8U, "frontier ABI requires 64-bit size_t");
static_assert(alignof(void*) == 8U, "frontier ABI pointer alignment changed");
static_assert(alignof(std::uint64_t) == 8U, "frontier word alignment changed");

#define FSIM_FRONTIER_V2_ASSERT_LAYOUT(type) \
    static_assert(std::is_standard_layout_v<type>, \
        #type " must remain standard-layout for LLVM offsetof"); \
    static_assert(std::is_trivially_copyable_v<type>, \
        #type " must remain trivially copyable across the generated entry")
#define FSIM_FRONTIER_V2_ASSERT_SIZE(type, expected) \
    static_assert(sizeof(type) == (expected), #type " ABI size changed")
#define FSIM_FRONTIER_V2_ASSERT_OFFSET(type, member, expected) \
    static_assert(offsetof(type, member) == (expected), \
        #type "." #member " ABI offset changed")

FSIM_FRONTIER_V2_ASSERT_LAYOUT(frontier::RegionFrontierPlaneV2);
FSIM_FRONTIER_V2_ASSERT_LAYOUT(frontier::RegionFrontierPendingWriteV2);
FSIM_FRONTIER_V2_ASSERT_LAYOUT(frontier::RegionFrontierSignalLayoutV2);
FSIM_FRONTIER_V2_ASSERT_LAYOUT(frontier::RegionFrontierWriteSiteV2);
FSIM_FRONTIER_V2_ASSERT_LAYOUT(frontier::RegionFrontierLayoutV2);
FSIM_FRONTIER_V2_ASSERT_LAYOUT(frontier::RegionFrontierFrameV2);

FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierPlaneV1, 104U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierPendingWriteV1, 144U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierLayoutV1, 120U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierFrameV1, 312U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierPlaneV2, 192U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierPendingWriteV2, 168U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierSignalLayoutV2, 32U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierWriteSiteV2, 40U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierLayoutV2, 128U);
FSIM_FRONTIER_V2_ASSERT_SIZE(frontier::RegionFrontierFrameV2, 320U);

static_assert(alignof(frontier::RegionFrontierPlaneV2) == 8U);
static_assert(alignof(frontier::RegionFrontierPendingWriteV2) == 8U);
static_assert(alignof(frontier::RegionFrontierSignalLayoutV2) == 4U);
static_assert(alignof(frontier::RegionFrontierWriteSiteV2) == 4U);
static_assert(alignof(frontier::RegionFrontierLayoutV2) == 8U);
static_assert(alignof(frontier::RegionFrontierFrameV2) == 8U);

FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierPlaneV2, signal_id, 0U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, owner_process_id, 4U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, value_kind, 8U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierPlaneV2, width, 12U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, word_count, 16U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, plane_count, 20U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierPlaneV2, flags, 24U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, metadata_index, 28U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, boundary_planes, 32U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, current_planes, 64U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, previous_planes, 96U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, stored_planes, 128U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPlaneV2, owner_planes, 160U);

FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, member_index, 0U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, signal_slot, 4U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, source_instruction, 8U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, update_kind, 12U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierPendingWriteV2, flags, 16U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, reserved, 20U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, commit_key, 24U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, origin, 72U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, value_kind, 120U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, width, 124U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, word_count, 128U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, plane_count, 132U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierPendingWriteV2, value_planes, 136U);

FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV2, signal_id, 0U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV2, owner_process_id, 4U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV2, value_kind, 8U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV2, width, 12U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV2, word_count, 16U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV2, plane_count, 20U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV2, flags, 24U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierSignalLayoutV2, metadata_index, 28U);

FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, member_index, 0U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, signal_slot, 4U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, source_instruction, 8U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, update_kind, 12U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, event_kind, 16U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, value_kind, 20U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, width, 24U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, word_count, 28U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, plane_count, 32U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierWriteSiteV2, pending_slot, 36U);

FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, abi_version, 0U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, struct_size, 4U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, value_plane_contract, 8U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, reserved0, 12U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, certificate_generation, 16U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, component_generation, 24U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, member_count, 32U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, readiness_word_count, 36U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, signal_slot_count, 40U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, metadata_count, 44U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, fanout_edge_count, 48U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, write_site_count, 52U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, pending_write_capacity, 56U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, staged_event_capacity, 60U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, max_commit_fanout_events, 64U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, committed_signal_capacity, 68U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, reserved_capacity, 72U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, execution_mode, 76U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, members, 80U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, signals, 88U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, write_sites, 96U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, max_member_write_counts, 104U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, max_member_staged_event_counts, 112U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierLayoutV2, fanout_edges, 120U);

FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierFrameV2, abi_version, 0U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierFrameV2, struct_size, 4U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, value_plane_contract, 8U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, generic_update_ack_count, 12U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, runtime_generation, 16U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, bound_runtime_generation, 24U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, certificate_generation, 32U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, component_generation, 40U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, scheduler_frontier_generation, 48U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, member_count, 56U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, scheduler_task_count, 60U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, scheduler_task_cursor, 64U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, scheduler_task_capacity, 68U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, readiness_word_count, 72U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, signal_slot_count, 76U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, metadata_count, 80U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, fanout_edge_count, 84U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, committed_signal_capacity, 88U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, committed_signal_count, 92U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, pending_write_capacity, 96U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, pending_write_count, 100U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, staged_event_capacity, 104U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, staged_event_count, 108U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, current_member, 112U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, current_pending_write, 116U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, current_commit_changed, 120U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, saved_body_pc, 124U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierFrameV2, ready_words, 128U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierFrameV2, members, 136U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, scheduler_tasks, 144U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierFrameV2, planes, 152U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, metadata, 160U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, fanout_edges, 168U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, port_planes, 176U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, pending_writes, 184U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, staged_events, 192U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, committed_signals, 200U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, native_frontier_member_dispatches, 208U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(
    frontier::RegionFrontierFrameV2, stop_requested, 216U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierFrameV2, slot, 224U);
FSIM_FRONTIER_V2_ASSERT_OFFSET(frontier::RegionFrontierFrameV2, cut, 256U);

static_assert(std::is_same_v<frontier::RegionFrontierKeyV1,
    frontier::RegionFrontierKeyV2>);
static_assert(std::is_same_v<frontier::RegionFrontierSlotV1,
    frontier::RegionFrontierSlotV2>);
static_assert(std::is_same_v<frontier::RegionFrontierMemberV1,
    frontier::RegionFrontierMemberV2>);
static_assert(std::is_same_v<frontier::RegionFrontierSignalMetadataV1,
    frontier::RegionFrontierSignalMetadataV2>);
static_assert(std::is_same_v<frontier::RegionFrontierCommittedSignalV1,
    frontier::RegionFrontierCommittedSignalV2>);
static_assert(std::is_same_v<frontier::RegionFrontierSchedulerTaskV1,
    frontier::RegionFrontierSchedulerTaskV2>);
static_assert(std::is_same_v<frontier::RegionFrontierCutV1,
    frontier::RegionFrontierCutV2>);
static_assert(!std::is_same_v<frontier::RegionFrontierPlaneV1,
    frontier::RegionFrontierPlaneV2>);
static_assert(!std::is_same_v<frontier::RegionFrontierPendingWriteV1,
    frontier::RegionFrontierPendingWriteV2>);
static_assert(!std::is_same_v<frontier::RegionFrontierFrameV1,
    frontier::RegionFrontierFrameV2>);
static_assert(!std::is_convertible_v<frontier::RegionFrontierStepEntryV1,
    frontier::RegionFrontierStepEntryV2>);
static_assert(frontier::kRegionFrontierAbiVersionV1 == 1U);
static_assert(frontier::kRegionFrontierAbiVersionV2 == 2U);
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierValueKindV2::logic4)
    == 0U);
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierValueKindV2::logic9)
    == 1U);
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierStatusV2::quiescent)
    == static_cast<std::uint32_t>(frontier::RegionFrontierStatusV1::quiescent));
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierStatusV2::need_scheduler_keys)
    == static_cast<std::uint32_t>(frontier::RegionFrontierStatusV1::need_scheduler_keys));
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierStatusV2::cut_before_key)
    == static_cast<std::uint32_t>(frontier::RegionFrontierStatusV1::cut_before_key));
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierStatusV2::boundary_publication)
    == static_cast<std::uint32_t>(frontier::RegionFrontierStatusV1::boundary_publication));
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierStatusV2::stopped)
    == static_cast<std::uint32_t>(frontier::RegionFrontierStatusV1::stopped));
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierStatusV2::decline_before_mutation)
    == static_cast<std::uint32_t>(frontier::RegionFrontierStatusV1::decline_before_mutation));
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierStatusV2::stale_generation)
    == static_cast<std::uint32_t>(frontier::RegionFrontierStatusV1::stale_generation));
static_assert(static_cast<std::uint32_t>(frontier::RegionFrontierStatusV2::yield_before_task)
    == static_cast<std::uint32_t>(frontier::RegionFrontierStatusV1::yield_before_task));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierStatusV2::generic_update_batch_ready) == 8U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierExecutionModeV2::systemverilog_active) == 0U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierExecutionModeV2::generic_deferred_update) == 1U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierEventKindV2::member_activation)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierEventKindV1::member_activation));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierEventKindV2::internal_commit)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierEventKindV1::internal_commit));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierEventKindV2::boundary_commit)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierEventKindV1::boundary_commit));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierEventKindV2::generic_deferred_update) == 3U);
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierPendingWriteFlagsV2::pending_active)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierPendingWriteFlagsV1::pending_active));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierPendingWriteFlagsV2::pending_value_ready)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierPendingWriteFlagsV1::pending_value_ready));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierPendingWriteFlagsV2::pending_key_assigned)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierPendingWriteFlagsV1::pending_key_assigned));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierPendingWriteFlagsV2::pending_internal_target)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierPendingWriteFlagsV1::pending_internal_target));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierPendingWriteFlagsV2::pending_boundary_target)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierPendingWriteFlagsV1::pending_boundary_target));
static_assert(static_cast<std::uint32_t>(
    frontier::RegionFrontierPendingWriteFlagsV2::pending_committed)
    == static_cast<std::uint32_t>(
        frontier::RegionFrontierPendingWriteFlagsV1::pending_committed));
static_assert(frontier::RegionFrontierPendingWriteFlagsV2::pending_generic_target
    == 1U << 6U);
static_assert(frontier::encode_region_frontier_payload_v1(
    frontier::RegionFrontierEventKindV2::generic_deferred_update, 0U)
    == UINT64_MAX);

static_assert(frontier::region_frontier_value_shape_valid_v2(
    frontier::RegionFrontierValueKindV2::logic4, 65U, 2U, 2U));
static_assert(frontier::region_frontier_value_shape_valid_v2(
    frontier::RegionFrontierValueKindV2::logic9, 65U, 2U, 4U));
static_assert(!frontier::region_frontier_value_shape_valid_v2(
    frontier::RegionFrontierValueKindV2::logic9, 65U, 2U, 2U));
static_assert(!frontier::region_frontier_value_shape_valid_v2(
    frontier::RegionFrontierValueKindV2::logic4, 65U, 1U, 2U));
static_assert(!frontier::region_frontier_value_shape_valid_v2(
    frontier::RegionFrontierValueKindV2::logic4, 0U, 0U, 2U));
static_assert(!frontier::region_frontier_value_shape_valid_v2(
    static_cast<frontier::RegionFrontierValueKindV2>(3U), 1U, 1U, 0U));
static_assert(frontier::region_frontier_logic9_word_is_canonical_v2(
    0U, 0U, 0U, UINT64_C(1)));
static_assert(!frontier::region_frontier_logic9_word_is_canonical_v2(
    UINT64_C(1), 0U, 0U, UINT64_C(1)));
static_assert(!frontier::region_frontier_logic9_word_is_canonical_v2(
    0U, UINT64_C(1), 0U, UINT64_C(1)));

static_assert(frontier::region_frontier_frame_prefix_valid_v2(
    frontier::kRegionFrontierAbiVersionV2,
    sizeof(frontier::RegionFrontierFrameV2)));
static_assert(!frontier::region_frontier_frame_prefix_valid_v2(
    frontier::kRegionFrontierAbiVersionV1,
    sizeof(frontier::RegionFrontierFrameV2)));
static_assert(!frontier::region_frontier_frame_prefix_valid_v2(
    frontier::kRegionFrontierAbiVersionV2,
    sizeof(frontier::RegionFrontierFrameV1)));
static_assert(frontier::region_frontier_frame_header_valid_v2(
    frontier::kRegionFrontierAbiVersionV2,
    sizeof(frontier::RegionFrontierFrameV2),
    frontier::kRegionFrontierValuePlaneContractV2));
static_assert(!frontier::region_frontier_frame_header_valid_v2(
    frontier::kRegionFrontierAbiVersionV1,
    sizeof(frontier::RegionFrontierFrameV2),
    frontier::kRegionFrontierValuePlaneContractV2));
static_assert(!frontier::region_frontier_frame_header_valid_v2(
    frontier::kRegionFrontierAbiVersionV2,
    sizeof(frontier::RegionFrontierFrameV1),
    frontier::kRegionFrontierValuePlaneContractV2));
static_assert(!frontier::region_frontier_frame_header_valid_v2(
    frontier::kRegionFrontierAbiVersionV2,
    sizeof(frontier::RegionFrontierFrameV2),
    frontier::kRegionFrontierAbiVersionV2));
static_assert(frontier::region_frontier_layout_prefix_valid_v2(
    frontier::kRegionFrontierAbiVersionV2,
    sizeof(frontier::RegionFrontierLayoutV2)));
static_assert(!frontier::region_frontier_layout_prefix_valid_v2(
    frontier::kRegionFrontierAbiVersionV1,
    sizeof(frontier::RegionFrontierLayoutV2)));
static_assert(!frontier::region_frontier_layout_prefix_valid_v2(
    frontier::kRegionFrontierAbiVersionV2,
    sizeof(frontier::RegionFrontierLayoutV1)));
static_assert(frontier::region_frontier_layout_header_valid_v2(
    frontier::kRegionFrontierAbiVersionV2,
    sizeof(frontier::RegionFrontierLayoutV2),
    frontier::kRegionFrontierValuePlaneContractV2));
static_assert(!frontier::region_frontier_layout_header_valid_v2(
    frontier::kRegionFrontierAbiVersionV1,
    sizeof(frontier::RegionFrontierLayoutV2),
    frontier::kRegionFrontierValuePlaneContractV2));

constexpr frontier::RegionFrontierSlotV2 kGenericActiveSlotV2 {
    11U, 7U, 0U, frontier::kRegionFrontierGenericDomainV2,
    frontier::kRegionFrontierActivePhaseV2
};
constexpr frontier::RegionFrontierKeyV2 kGenericActiveOriginV2 {
    11U, 7U, 0U, 42U, 9U, frontier::kRegionFrontierGenericDomainV2,
    frontier::kRegionFrontierActivePhaseV2
};
constexpr frontier::RegionFrontierKeyV2 kZeroFrontierKeyV2 { };
static_assert(frontier::region_frontier_execution_mode_valid_v2(
    frontier::RegionFrontierExecutionModeV2::systemverilog_active));
static_assert(frontier::region_frontier_execution_mode_valid_v2(
    frontier::RegionFrontierExecutionModeV2::generic_deferred_update));
static_assert(!frontier::region_frontier_execution_mode_valid_v2(
    static_cast<frontier::RegionFrontierExecutionModeV2>(9U)));
static_assert(frontier::region_frontier_status_valid_for_mode_v2(
    frontier::RegionFrontierExecutionModeV2::systemverilog_active,
    frontier::RegionFrontierStatusV2::boundary_publication));
static_assert(!frontier::region_frontier_status_valid_for_mode_v2(
    frontier::RegionFrontierExecutionModeV2::generic_deferred_update,
    frontier::RegionFrontierStatusV2::boundary_publication));
static_assert(!frontier::region_frontier_status_valid_for_mode_v2(
    frontier::RegionFrontierExecutionModeV2::systemverilog_active,
    frontier::RegionFrontierStatusV2::generic_update_batch_ready));
static_assert(frontier::region_frontier_status_valid_for_mode_v2(
    frontier::RegionFrontierExecutionModeV2::generic_deferred_update,
    frontier::RegionFrontierStatusV2::generic_update_batch_ready));
static_assert(!frontier::region_frontier_status_valid_for_mode_v2(
    frontier::RegionFrontierExecutionModeV2::generic_deferred_update,
    frontier::RegionFrontierStatusV2::need_scheduler_keys));
static_assert(frontier::region_frontier_status_valid_for_mode_v2(
    frontier::RegionFrontierExecutionModeV2::generic_deferred_update,
    frontier::RegionFrontierStatusV2::cut_before_key));
constexpr auto kFrontierStatusModeMatrixV2 = [] {
    constexpr std::array statuses {
        frontier::RegionFrontierStatusV2::quiescent,
        frontier::RegionFrontierStatusV2::need_scheduler_keys,
        frontier::RegionFrontierStatusV2::cut_before_key,
        frontier::RegionFrontierStatusV2::boundary_publication,
        frontier::RegionFrontierStatusV2::stopped,
        frontier::RegionFrontierStatusV2::decline_before_mutation,
        frontier::RegionFrontierStatusV2::stale_generation,
        frontier::RegionFrontierStatusV2::yield_before_task,
        frontier::RegionFrontierStatusV2::generic_update_batch_ready,
    };
    for (const auto status : statuses) {
        const auto value = static_cast<std::uint32_t>(status);
        const bool generic_expected = value != static_cast<std::uint32_t>(
                frontier::RegionFrontierStatusV2::need_scheduler_keys)
            && value != static_cast<std::uint32_t>(
                frontier::RegionFrontierStatusV2::boundary_publication);
        if (frontier::region_frontier_status_valid_for_mode_v2(
                frontier::RegionFrontierExecutionModeV2::generic_deferred_update,
                status) != generic_expected) {
            return false;
        }
        const bool systemverilog_expected = status !=
            frontier::RegionFrontierStatusV2::generic_update_batch_ready;
        if (frontier::region_frontier_status_valid_for_mode_v2(
                frontier::RegionFrontierExecutionModeV2::systemverilog_active,
                status) != systemverilog_expected) {
            return false;
        }
    }
    return !frontier::region_frontier_status_valid_for_mode_v2(
        static_cast<frontier::RegionFrontierExecutionModeV2>(99U),
        frontier::RegionFrontierStatusV2::quiescent);
}();
static_assert(kFrontierStatusModeMatrixV2);
static_assert(frontier::region_frontier_slot_valid_for_mode_v2(
    frontier::RegionFrontierExecutionModeV2::generic_deferred_update,
    kGenericActiveSlotV2));
static_assert(frontier::region_frontier_generic_key_valid_v2(
    kGenericActiveOriginV2, kGenericActiveSlotV2));
static_assert(!frontier::region_frontier_generic_key_valid_v2(
    frontier::RegionFrontierKeyV2 {
        11U, 7U, 1U, 42U, 9U,
        frontier::kRegionFrontierGenericDomainV2,
        frontier::kRegionFrontierActivePhaseV2 },
    kGenericActiveSlotV2));
static_assert(frontier::region_frontier_generic_flags_valid_v2(
    frontier::kRegionFrontierGenericWriteFlagsV2));
static_assert(!frontier::region_frontier_generic_flags_valid_v2(
    frontier::kRegionFrontierGenericWriteFlagsV2
        | frontier::RegionFrontierPendingWriteFlagsV2::pending_key_assigned));
static_assert(frontier::region_frontier_generic_ack_counts_valid_v2(0U, 0U, 0U));
static_assert(frontier::region_frontier_generic_ack_counts_valid_v2(2U, 2U, 0U));
static_assert(frontier::region_frontier_generic_ack_counts_valid_v2(2U, 2U, 2U));
static_assert(!frontier::region_frontier_generic_ack_counts_valid_v2(2U, 2U, 1U));
static_assert(!frontier::region_frontier_generic_ack_counts_valid_v2(1U, 2U, 0U));
static_assert(frontier::region_frontier_generic_batch_ready_counts_valid_v2(
    2U, 2U, 0U));
static_assert(!frontier::region_frontier_generic_batch_ready_counts_valid_v2(
    0U, 0U, 0U));
static_assert(!frontier::region_frontier_generic_batch_ready_counts_valid_v2(
    2U, 2U, 2U));
static_assert(!frontier::region_frontier_generic_batch_ready_counts_valid_v2(
    1U, 2U, 0U));
static_assert(frontier::region_frontier_generic_retirement_ack_valid_v2(
    2U, 2U, 2U));
static_assert(!frontier::region_frontier_generic_retirement_ack_valid_v2(
    2U, 2U, 0U));
static_assert(!frontier::region_frontier_generic_retirement_ack_valid_v2(
    2U, 2U, 1U));
static_assert(!frontier::region_frontier_generic_retirement_ack_valid_v2(
    0U, 0U, 0U));
static_assert(frontier::region_frontier_generic_pending_header_valid_v2(
    frontier::kRegionFrontierGenericWriteFlagsV2, 0U, kZeroFrontierKeyV2,
    kGenericActiveOriginV2, kGenericActiveSlotV2));
static_assert(!frontier::region_frontier_generic_pending_header_valid_v2(
    frontier::kRegionFrontierGenericWriteFlagsV2
        | frontier::RegionFrontierPendingWriteFlagsV2::pending_key_assigned,
    0U, kZeroFrontierKeyV2, kGenericActiveOriginV2, kGenericActiveSlotV2));
static_assert(!frontier::region_frontier_generic_pending_header_valid_v2(
    frontier::kRegionFrontierGenericWriteFlagsV2, 1U, kZeroFrontierKeyV2,
    kGenericActiveOriginV2, kGenericActiveSlotV2));
static_assert(!frontier::region_frontier_generic_pending_header_valid_v2(
    frontier::kRegionFrontierGenericWriteFlagsV2, 0U,
    frontier::RegionFrontierKeyV2 { 1U, 0U, 0U, 1U, 1U, 0U, 0U },
    kGenericActiveOriginV2, kGenericActiveSlotV2));

constexpr frontier::RegionFrontierWriteSiteV2 kGenericWriteSiteV2 {
    0U, 2U, 7U, 0U,
    static_cast<std::uint32_t>(
        frontier::RegionFrontierEventKindV2::generic_deferred_update),
    frontier::RegionFrontierValueKindV2::logic4, 1U, 1U, 2U, 0U
};
constexpr frontier::RegionFrontierStagedEventV2 kGenericStagedEventV2 {
    static_cast<std::uint32_t>(
        frontier::RegionFrontierEventKindV2::generic_deferred_update),
    0U, kGenericActiveOriginV2.stable_order, kGenericActiveOriginV2
};
static_assert(frontier::region_frontier_generic_event_matches_site_v2(
    kGenericStagedEventV2, kGenericWriteSiteV2, kGenericActiveSlotV2));
static_assert(!frontier::region_frontier_generic_event_matches_site_v2(
    frontier::RegionFrontierStagedEventV2 {
        static_cast<std::uint32_t>(
            frontier::RegionFrontierEventKindV2::member_activation),
        0U, kGenericActiveOriginV2.stable_order, kGenericActiveOriginV2 },
    kGenericWriteSiteV2, kGenericActiveSlotV2));
static_assert(!frontier::region_frontier_generic_event_matches_site_v2(
    frontier::RegionFrontierStagedEventV2 {
        static_cast<std::uint32_t>(
            frontier::RegionFrontierEventKindV2::generic_deferred_update),
        0U, kGenericActiveOriginV2.stable_order + 1U,
        kGenericActiveOriginV2 },
    kGenericWriteSiteV2, kGenericActiveSlotV2));

int main()
{
    const auto fail = [](const char* const message) {
        std::fprintf(stderr, "frontier V2 ABI check failed: %s\n", message);
        return 1;
    };
    std::uint64_t current_storage[4][2] { };
    std::uint64_t previous_storage[4][2] { };
    std::uint64_t stored_storage[4][2] { };
    std::uint64_t owner_storage[4][2] { };
    std::uint64_t boundary_storage[4][2] { };
    std::uint64_t pending_storage[4][2] { };
    frontier::RegionFrontierPlaneV2 internal;
    internal.value_kind = frontier::RegionFrontierValueKindV2::logic9;
    internal.width = 65U;
    internal.word_count = 2U;
    internal.plane_count = 4U;
    internal.flags = static_cast<std::uint32_t>(
        frontier::RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        internal.current_planes[index] = current_storage[index];
        internal.previous_planes[index] = previous_storage[index];
        internal.stored_planes[index] = stored_storage[index];
        internal.owner_planes[index] = owner_storage[index];
    }
    // This checks pointer-slot presence only; A4 lease tests authenticate
    // plane extent, split offsets, ownership, and aliases.
    if (!frontier::region_frontier_plane_bindings_valid_v2(internal)) {
        return fail("complete Logic9 internal role pointers should validate");
    }
    internal.owner_planes[3] = nullptr;
    if (frontier::region_frontier_plane_bindings_valid_v2(internal)) {
        return fail("missing Logic9 owner plane must reject");
    }

    frontier::RegionFrontierPlaneV2 boundary;
    boundary.value_kind = frontier::RegionFrontierValueKindV2::logic4;
    boundary.width = 65U;
    boundary.word_count = 2U;
    boundary.plane_count = 2U;
    boundary.flags = static_cast<std::uint32_t>(
        frontier::RegionFrontierPlaneFlagsV2::read_only_boundary_port);
    boundary.boundary_planes[0] = boundary_storage[0];
    boundary.boundary_planes[1] = boundary_storage[1];
    if (!frontier::region_frontier_plane_bindings_valid_v2(boundary)) {
        return fail("read-only Logic4 boundary planes should validate");
    }
    boundary.current_planes[0] = current_storage[0];
    if (frontier::region_frontier_plane_bindings_valid_v2(boundary)) {
        return fail("read-only boundary must reject mutable current planes");
    }

    frontier::RegionFrontierPendingWriteV2 pending;
    pending.value_kind = frontier::RegionFrontierValueKindV2::logic9;
    pending.width = 65U;
    pending.word_count = 2U;
    pending.plane_count = 4U;
    for (std::uint32_t index = 0U; index < 4U; ++index) {
        pending.value_planes[index] = pending_storage[index];
    }
    if (!frontier::region_frontier_pending_write_bindings_valid_v2(pending)) {
        return fail("complete Logic9 pending value planes should validate");
    }
    pending.value_planes[3] = nullptr;
    if (frontier::region_frontier_pending_write_bindings_valid_v2(pending)) {
        return fail("pending Logic9 value must reject a missing fourth plane");
    }

    std::uint64_t generic_pending_words[2] { };
    frontier::RegionFrontierPendingWriteV2 generic_pending;
    generic_pending.member_index = 0U;
    generic_pending.signal_slot = 2U;
    generic_pending.source_instruction = 7U;
    generic_pending.update_kind = 0U;
    generic_pending.flags = frontier::kRegionFrontierGenericWriteFlagsV2;
    generic_pending.origin = kGenericActiveOriginV2;
    generic_pending.value_kind = frontier::RegionFrontierValueKindV2::logic4;
    generic_pending.width = 1U;
    generic_pending.word_count = 1U;
    generic_pending.plane_count = 2U;
    generic_pending.value_planes[0] = &generic_pending_words[0];
    generic_pending.value_planes[1] = &generic_pending_words[1];

    frontier::RegionFrontierStagedEventV2 generic_event;
    generic_event.kind = static_cast<std::uint32_t>(
        frontier::RegionFrontierEventKindV2::generic_deferred_update);
    generic_event.descriptor_index = 0U;
    generic_event.stable_order = kGenericActiveOriginV2.stable_order;
    generic_event.origin = kGenericActiveOriginV2;

    const frontier::RegionFrontierWriteSiteV2 generic_site {
        0U, 2U, 7U, 0U,
        static_cast<std::uint32_t>(
            frontier::RegionFrontierEventKindV2::generic_deferred_update),
        frontier::RegionFrontierValueKindV2::logic4, 1U, 1U, 2U, 0U
    };
    const frontier::RegionFrontierSignalLayoutV2 generic_signal {
        33U, 15U, frontier::RegionFrontierValueKindV2::logic4, 1U, 1U, 2U,
        static_cast<std::uint32_t>(
            frontier::RegionFrontierPlaneFlagsV2::read_only_boundary_port),
        UINT32_MAX
    };
    const frontier::RegionFrontierMemberLayoutV2 generic_member {
        15U, 0U, 1U, 1U, 1U, { 0U, 0U, 0U }
    };
    const auto generic_descriptor_matches =
        [&](const frontier::RegionFrontierStagedEventV2& event,
            const frontier::RegionFrontierPendingWriteV2& write,
            const frontier::RegionFrontierWriteSiteV2& site,
            const frontier::RegionFrontierSignalLayoutV2& signal,
            const frontier::RegionFrontierMemberLayoutV2& member,
            const std::uint32_t member_index,
            const std::uint32_t signal_slot,
            const frontier::RegionFrontierKeyV2& activation_origin) {
            return frontier::region_frontier_generic_write_descriptor_matches_v2(
                event, write, site, signal, member, member_index, signal_slot,
                activation_origin, kGenericActiveSlotV2);
        };
    if (!generic_descriptor_matches(generic_event, generic_pending,
            generic_site, generic_signal, generic_member, 0U, 2U,
            kGenericActiveOriginV2)) {
        return fail("well-formed generic deferred descriptors should authenticate");
    }
    auto generic_bad_pending = generic_pending;
    generic_bad_pending.flags |=
        frontier::RegionFrontierPendingWriteFlagsV2::pending_key_assigned;
    if (generic_descriptor_matches(generic_event, generic_bad_pending,
            generic_site, generic_signal, generic_member, 0U, 2U,
            kGenericActiveOriginV2)) {
        return fail("generic deferred writes must reject assigned scheduler keys");
    }
    generic_bad_pending = generic_pending;
    generic_bad_pending.origin.sequence += 1U;
    if (generic_descriptor_matches(generic_event, generic_bad_pending,
            generic_site, generic_signal, generic_member, 0U, 2U,
            kGenericActiveOriginV2)) {
        return fail("generic pending origin must equal its full activation key");
    }
    auto generic_bad_event = generic_event;
    generic_bad_event.origin.sequence += 1U;
    if (generic_descriptor_matches(generic_bad_event, generic_pending,
            generic_site, generic_signal, generic_member, 0U, 2U,
            kGenericActiveOriginV2)) {
        return fail("generic event origin must equal its full activation key");
    }
    auto generic_bad_signal = generic_signal;
    generic_bad_signal.flags = static_cast<std::uint32_t>(
        frontier::RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
    if (generic_descriptor_matches(generic_event, generic_pending,
            generic_site, generic_bad_signal, generic_member, 0U, 2U,
            kGenericActiveOriginV2)) {
        return fail("generic output values must target read-only frame planes");
    }
    if (generic_descriptor_matches(generic_event, generic_pending,
            generic_site, generic_signal, generic_member, 1U, 2U,
            kGenericActiveOriginV2)
        || generic_descriptor_matches(generic_event, generic_pending,
            generic_site, generic_signal, generic_member, 0U, 1U,
            kGenericActiveOriginV2)) {
        return fail("generic event must use its certified member and signal slots");
    }
    generic_bad_pending = generic_pending;
    generic_bad_pending.source_instruction += 1U;
    if (generic_descriptor_matches(generic_event, generic_bad_pending,
            generic_site, generic_signal, generic_member, 0U, 2U,
            kGenericActiveOriginV2)) {
        return fail("generic pending write must match its source site");
    }
    generic_bad_pending = generic_pending;
    generic_bad_pending.value_planes[1] = nullptr;
    if (generic_descriptor_matches(generic_event, generic_bad_pending,
            generic_site, generic_signal, generic_member, 0U, 2U,
            kGenericActiveOriginV2)) {
        return fail("generic Logic4 write must bind both typed value planes");
    }
    return 0;
}
