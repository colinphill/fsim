// SPDX-License-Identifier: Apache-2.0
// Scratch-only LLVM lowering for certified internal Logic4/Logic9 commits.
#include "region_frontier_codegen_v2.hpp"

#include "logic9_word_lowering.hpp"

#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::runtime::simir::scratch {
namespace {

    using Builder = llvm::IRBuilder<>;

    [[nodiscard]] llvm::IntegerType* i8_type(Builder& builder)
    {
        return llvm::Type::getInt8Ty(builder.getContext());
    }

    [[nodiscard]] llvm::IntegerType* i32_type(Builder& builder)
    {
        return llvm::Type::getInt32Ty(builder.getContext());
    }

    [[nodiscard]] llvm::IntegerType* i64_type(Builder& builder)
    {
        return llvm::Type::getInt64Ty(builder.getContext());
    }

    [[nodiscard]] llvm::Type* byte_pointer_type(Builder& builder)
    {
        return llvm::PointerType::getUnqual(builder.getContext());
    }

    [[nodiscard]] llvm::Value* byte_offset_pointer(
        Builder& builder, llvm::Value* base, llvm::Value* offset)
    {
        return builder.CreateInBoundsGEP(i8_type(builder), base, offset);
    }

    [[nodiscard]] llvm::Value* field_pointer(
        Builder& builder, llvm::Value* base, const std::size_t offset)
    {
        return byte_offset_pointer(builder, base,
            llvm::ConstantInt::get(i64_type(builder), offset));
    }

    [[nodiscard]] llvm::Value* indexed_record_pointer(
        Builder& builder, llvm::Value* base, llvm::Value* index,
        const std::size_t record_size)
    {
        const auto offset = builder.CreateMul(
            builder.CreateZExt(index, i64_type(builder)),
            llvm::ConstantInt::get(i64_type(builder), record_size));
        return byte_offset_pointer(builder, base, offset);
    }

    [[nodiscard]] constexpr std::size_t pointer_array_element_offset(
        const std::size_t array_offset, const std::uint32_t plane_index)
    {
        return array_offset + sizeof(std::uint64_t*) * plane_index;
    }

    [[nodiscard]] llvm::Value* load_field(
        Builder& builder, llvm::Value* base, const std::size_t offset,
        llvm::Type* type, const llvm::Twine& name)
    {
        return builder.CreateLoad(type, field_pointer(builder, base, offset), name);
    }

    void store_field(Builder& builder, llvm::Value* base,
        const std::size_t offset, llvm::Value* value)
    {
        builder.CreateStore(value, field_pointer(builder, base, offset));
    }

    [[nodiscard]] llvm::Value* load_word(
        Builder& builder, llvm::Value* plane, const std::uint32_t word_index,
        const llvm::Twine& name)
    {
        auto* const words = builder.CreateBitCast(plane,
            llvm::PointerType::getUnqual(builder.getContext()));
        auto* const pointer = builder.CreateInBoundsGEP(i64_type(builder), words,
            llvm::ConstantInt::get(i64_type(builder), word_index));
        return builder.CreateLoad(i64_type(builder), pointer, name);
    }

    void store_word(Builder& builder, llvm::Value* plane,
        const std::uint32_t word_index, llvm::Value* value)
    {
        auto* const words = builder.CreateBitCast(plane,
            llvm::PointerType::getUnqual(builder.getContext()));
        auto* const pointer = builder.CreateInBoundsGEP(i64_type(builder), words,
            llvm::ConstantInt::get(i64_type(builder), word_index));
        builder.CreateStore(value, pointer);
    }

    [[nodiscard]] llvm::Value* load_frame_pointer(
        Builder& builder, llvm::Value* frame, const std::size_t offset,
        const llvm::Twine& name)
    {
        return load_field(builder, frame, offset, byte_pointer_type(builder), name);
    }

    void copy_key_fields(Builder& builder, llvm::Value* destination,
        llvm::Value* source)
    {
        constexpr auto time_offset = offsetof(RegionFrontierKeyV2, time);
        constexpr auto delta_offset = offsetof(RegionFrontierKeyV2, delta);
        constexpr auto round_offset
            = offsetof(RegionFrontierKeyV2, systemverilog_round);
        constexpr auto stable_offset = offsetof(RegionFrontierKeyV2, stable_order);
        constexpr auto sequence_offset = offsetof(RegionFrontierKeyV2, sequence);
        constexpr auto domain_offset = offsetof(RegionFrontierKeyV2, process_domain);
        constexpr auto phase_offset = offsetof(RegionFrontierKeyV2, phase);

        for (const auto field_offset : { time_offset, delta_offset, round_offset,
                 stable_offset, sequence_offset }) {
            const auto value = load_field(builder, source, field_offset,
                i64_type(builder), "origin.key.i64");
            store_field(builder, destination, field_offset, value);
        }
        for (const auto field_offset : { domain_offset, phase_offset }) {
            const auto value = load_field(builder, source, field_offset,
                i32_type(builder), "origin.key.i32");
            store_field(builder, destination, field_offset, value);
        }
    }

    [[nodiscard]] bool layout_is_well_formed(
        const RegionFrontierLayoutV2& layout)
    {
        if (!region_frontier_layout_header_valid_v2(layout)
            || layout.struct_size != sizeof(RegionFrontierLayoutV2)
            || layout.member_count == 0U
            || layout.reserved0 != 0U
            || layout.execution_mode
                != RegionFrontierExecutionModeV2::systemverilog_active
            || layout.reserved_capacity != 0U
            || layout.members == nullptr
            || (layout.signal_slot_count != 0U && layout.signals == nullptr)
            || (layout.write_site_count != 0U && layout.write_sites == nullptr)
            || (layout.fanout_edge_count != 0U && layout.fanout_edges == nullptr)
            || layout.max_member_write_counts == nullptr
            || layout.max_member_staged_event_counts == nullptr
            || static_cast<std::uint64_t>(layout.readiness_word_count)
                != (static_cast<std::uint64_t>(layout.member_count) + 63U) / 64U
            || layout.pending_write_capacity == 0U
            || layout.write_site_count != layout.pending_write_capacity
            || static_cast<std::uint64_t>(layout.staged_event_capacity)
                != static_cast<std::uint64_t>(layout.write_site_count)
                    + layout.fanout_edge_count
            || layout.committed_signal_capacity
                != layout.pending_write_capacity) {
            return false;
        }

        std::vector<std::uint32_t> fanout_bound(layout.signal_slot_count);
        std::vector<std::uint32_t> member_write_count(layout.member_count);
        std::vector<std::uint8_t> metadata_index_seen(layout.metadata_count, 0U);
        const auto known_plane_flags
            = RegionFrontierPlaneFlagsV2::certified_internal_single_owner
            | RegionFrontierPlaneFlagsV2::read_only_boundary_port;
        for (std::size_t index = 0U; index < layout.signal_slot_count; ++index) {
            const auto& signal = layout.signals[index];
            const auto expected_words = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(signal.width) + 63U) / 64U);
            const auto flags = signal.flags
                & (RegionFrontierPlaneFlagsV2::certified_internal_single_owner
                    | RegionFrontierPlaneFlagsV2::read_only_boundary_port);
            const bool is_internal
                = flags == RegionFrontierPlaneFlagsV2::certified_internal_single_owner;
            const bool is_boundary
                = flags == RegionFrontierPlaneFlagsV2::read_only_boundary_port;
            if ((signal.flags & ~known_plane_flags) != 0U
                || (!is_internal && !is_boundary)
                || !region_frontier_value_shape_valid_v2(signal.value_kind,
                    signal.width, signal.word_count, signal.plane_count)
                || signal.word_count != expected_words
                || (is_internal && (signal.metadata_index >= layout.metadata_count
                    || signal.owner_process_id == UINT32_MAX))
                || (is_boundary
                    && (signal.metadata_index != UINT32_MAX
                        || signal.owner_process_id != UINT32_MAX))) {
                return false;
            }
            for (std::size_t prior = 0U; prior < index; ++prior) {
                if (layout.signals[prior].signal_id == signal.signal_id) {
                    return false;
                }
            }
            if (is_internal) {
                if (metadata_index_seen[signal.metadata_index] != 0U) {
                    return false;
                }
                metadata_index_seen[signal.metadata_index] = 1U;
            }
        }
        if (std::ranges::any_of(metadata_index_seen,
                [](const std::uint8_t seen) { return seen == 0U; })) {
            return false;
        }

        std::vector<std::uint8_t> pending_slot_seen(
            layout.pending_write_capacity, 0U);
        for (std::size_t index = 0U; index < layout.write_site_count; ++index) {
            const auto& site = layout.write_sites[index];
            if (site.member_index >= layout.member_count
                || site.signal_slot >= layout.signal_slot_count) {
                return false;
            }
            const auto& signal = layout.signals[site.signal_slot];
            const auto signal_flags = signal.flags;
            const bool is_internal
                = (signal_flags
                      & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                != 0U;
            const bool is_boundary
                = (signal_flags
                      & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
                != 0U;
            const auto expected_event_kind = is_internal
                ? RegionFrontierEventKindV2::internal_commit
                : RegionFrontierEventKindV2::boundary_commit;
            const bool site_shape_matches_signal = is_internal
                ? site.width == signal.width
                    && site.word_count == signal.word_count
                : site.width <= signal.width;
            // Boundary sites remain host-published, but their static shape and
            // original commit class belong to the same certified write-site set.
            if ((!is_internal && !is_boundary)
                || (is_internal
                    && signal.owner_process_id
                        != layout.members[site.member_index].process_id)
                || !site_shape_matches_signal
                || !region_frontier_value_shape_valid_v2(site.value_kind,
                    site.width, site.word_count, site.plane_count)
                || site.value_kind != signal.value_kind
                || site.plane_count != signal.plane_count
                || site.update_kind != static_cast<std::uint32_t>(RegionUpdateKind::systemverilog_active)
                || site.pending_slot >= layout.pending_write_capacity
                || site.pending_slot != index
                || pending_slot_seen[site.pending_slot] != 0U
                || site.event_kind != static_cast<std::uint32_t>(expected_event_kind)) {
                return false;
            }
            pending_slot_seen[site.pending_slot] = 1U;
            ++member_write_count[site.member_index];
        }
        if (std::ranges::any_of(pending_slot_seen,
                [](const std::uint8_t seen) { return seen == 0U; })) {
            return false;
        }
        for (std::size_t index = 0U; index < layout.member_count; ++index) {
            const auto& member = layout.members[index];
            if (member.write_site_count != member_write_count[index]
                || member.max_pending_writes != member_write_count[index]
                || member.max_staged_events != member_write_count[index]
                || layout.max_member_write_counts[index]
                    != member_write_count[index]
                || layout.max_member_staged_event_counts[index]
                    != member_write_count[index]
                || member.first_write_site > layout.write_site_count
                || member.write_site_count
                    > layout.write_site_count - member.first_write_site) {
                return false;
            }
            for (std::uint32_t offset = 0U;
                 offset < member.write_site_count; ++offset) {
                if (layout.write_sites[member.first_write_site + offset]
                        .member_index != index) {
                    return false;
                }
            }
            for (std::size_t prior = 0U; prior < index; ++prior) {
                if (layout.members[prior].process_id == member.process_id) {
                    return false;
                }
            }
        }

        for (std::size_t index = 0U; index < layout.fanout_edge_count; ++index) {
            const auto& edge = layout.fanout_edges[index];
            if (edge.signal_slot >= layout.signal_slot_count
                || edge.member_index >= layout.member_count) {
                return false;
            }
            if (index != 0U) {
                const auto& previous = layout.fanout_edges[index - 1U];
                if (edge.signal_slot < previous.signal_slot) {
                    return false;
                }
            }
            ++fanout_bound[edge.signal_slot];
        }

        const auto maximum_fanout = fanout_bound.empty()
            ? 0U
            : *std::max_element(fanout_bound.begin(), fanout_bound.end());
        return layout.max_commit_fanout_events >= maximum_fanout;
    }

    [[nodiscard]] bool fanout_ranges_are_well_formed(
        const RegionFrontierLayoutV2& layout,
        const std::span<const RegionFrontierFanoutRangeSpan> edge_range_spans,
        const std::span<const RegionFrontierFanoutSensitivityRange> ranges)
    {
        if (edge_range_spans.size() != layout.fanout_edge_count) {
            return false;
        }

        std::size_t expected_first_range { };
        for (std::size_t edge_index = 0U;
             edge_index < edge_range_spans.size(); ++edge_index) {
            const auto& edge = layout.fanout_edges[edge_index];
            const auto& span = edge_range_spans[edge_index];
            const auto first_range = static_cast<std::size_t>(span.first_range);
            const auto range_count = static_cast<std::size_t>(span.range_count);
            if (range_count == 0U || first_range != expected_first_range
                || first_range > ranges.size()
                || range_count > ranges.size() - first_range) {
                return false;
            }

            const auto signal_width = layout.signals[edge.signal_slot].width;
            for (std::size_t range_index = first_range;
                 range_index < first_range + range_count; ++range_index) {
                const auto& range = ranges[range_index];
                if (range.width == 0U || range.offset > signal_width
                    || range.width > signal_width - range.offset) {
                    return false;
                }
            }
            expected_first_range += range_count;
        }
        return expected_first_range == ranges.size();
    }

    [[nodiscard]] llvm::Value* emit_sensitivity_range_changed(
        Builder& builder, const RegionFrontierSignalLayoutV2& signal,
        const RegionFrontierFanoutSensitivityRange& range,
        const std::array<std::array<std::vector<llvm::Value*>, 4U>, 4U>& old_roles,
        const std::array<std::vector<llvm::Value*>, 4U>& new_value,
        llvm::Value* whole_signal_changed)
    {
        if (range.offset == 0U && range.width == signal.width) {
            return whole_signal_changed;
        }

        llvm::Value* changed
            = llvm::ConstantInt::getFalse(builder.getContext());
        const auto range_begin = static_cast<std::uint64_t>(range.offset);
        const auto range_end = range_begin + range.width;
        const auto first_word = static_cast<std::uint32_t>(range_begin / 64U);
        const auto last_word = static_cast<std::uint32_t>(
            (range_end - 1U) / 64U);
        for (std::uint32_t word = first_word; word <= last_word; ++word) {
            const auto word_begin = static_cast<std::uint64_t>(word) * 64U;
            const auto low_bit = static_cast<unsigned int>(
                std::max(range_begin, word_begin) - word_begin);
            const auto high_bit = static_cast<unsigned int>(
                std::min(range_end, word_begin + 64U) - word_begin);
            const auto low_mask = low_bit == 0U
                ? UINT64_MAX
                : UINT64_MAX << low_bit;
            const auto high_mask = high_bit == 64U
                ? UINT64_MAX
                : (UINT64_C(1) << high_bit) - 1U;
            const auto mask = llvm::ConstantInt::get(
                i64_type(builder), low_mask & high_mask);
            for (std::uint32_t value_plane = 0U;
                 value_plane < signal.plane_count; ++value_plane) {
                const auto changed_word = builder.CreateAnd(
                    builder.CreateXor(old_roles[0U][value_plane][word],
                        new_value[value_plane][word],
                        "sensitivity.plane.diff"),
                    mask, "sensitivity.range.diff");
                changed = builder.CreateOr(changed,
                    builder.CreateICmpNE(changed_word,
                        llvm::ConstantInt::get(i64_type(builder), 0U),
                        "sensitivity.range.plane.changed"),
                    "sensitivity.range.changed");
            }
        }
        return changed;
    }

    void emit_one_signal_commit(Builder& builder,
        const RegionFrontierLayoutV2& layout,
        const std::span<const RegionFrontierFanoutRangeSpan> edge_range_spans,
        const std::span<const RegionFrontierFanoutSensitivityRange> ranges,
        const std::uint32_t signal_slot, llvm::Value* write_index,
        llvm::Value* frame, llvm::Value* frame_planes,
        llvm::Value* frame_metadata, llvm::Value* frame_members,
        llvm::Value* frame_events, llvm::Value* frame_fanout_edges,
        llvm::Value* frame_committed_signals, llvm::BasicBlock* done)
    {
        const auto& signal = layout.signals[signal_slot];
        const auto plane = indexed_record_pointer(builder, frame_planes,
            llvm::ConstantInt::get(i32_type(builder), signal_slot),
            sizeof(RegionFrontierPlaneV2));
        const auto write = indexed_record_pointer(
            builder,
            load_frame_pointer(builder, frame,
                offsetof(RegionFrontierFrameV2, pending_writes), "writes.ptr"),
            write_index, sizeof(RegionFrontierPendingWriteV2));

        constexpr std::array<std::size_t, 4U> role_array_offsets {
            offsetof(RegionFrontierPlaneV2, current_planes),
            offsetof(RegionFrontierPlaneV2, previous_planes),
            offsetof(RegionFrontierPlaneV2, stored_planes),
            offsetof(RegionFrontierPlaneV2, owner_planes),
        };
        std::array<std::array<llvm::Value*, 4U>, 4U> role_planes { };
        std::array<llvm::Value*, 4U> pending_planes { };
        for (std::uint32_t value_plane = 0U;
             value_plane < signal.plane_count; ++value_plane) {
            for (std::uint32_t role = 0U; role < role_array_offsets.size(); ++role) {
                role_planes[role][value_plane] = load_field(builder, plane,
                    pointer_array_element_offset(role_array_offsets[role],
                        value_plane), byte_pointer_type(builder),
                    "role.value.plane");
            }
            pending_planes[value_plane] = load_field(builder, write,
                pointer_array_element_offset(
                    offsetof(RegionFrontierPendingWriteV2, value_planes),
                    value_plane), byte_pointer_type(builder),
                "pending.value.plane");
        }

        // Capture every source and old-role word before touching storage. This
        // preserves the authenticated stored/owner alias case for all planes.
        std::array<std::array<std::vector<llvm::Value*>, 4U>, 4U> old_roles;
        std::array<std::vector<llvm::Value*>, 4U> new_value;
        for (std::uint32_t role = 0U; role < 4U; ++role) {
            for (std::uint32_t value_plane = 0U;
                 value_plane < signal.plane_count; ++value_plane) {
                old_roles[role][value_plane].reserve(signal.word_count);
                if (role == 0U) {
                    new_value[value_plane].reserve(signal.word_count);
                }
            }
        }
        llvm::Value* changed = llvm::ConstantInt::getFalse(builder.getContext());
        llvm::Value* state_changed
            = llvm::ConstantInt::getFalse(builder.getContext());
        for (std::uint32_t word = 0U; word < signal.word_count; ++word) {
            llvm::Value* current_word_changed
                = llvm::ConstantInt::getFalse(builder.getContext());
            llvm::Value* stored_word_changed
                = llvm::ConstantInt::getFalse(builder.getContext());
            llvm::Value* owner_word_changed
                = llvm::ConstantInt::getFalse(builder.getContext());
            for (std::uint32_t value_plane = 0U;
                 value_plane < signal.plane_count; ++value_plane) {
                for (std::uint32_t role = 0U; role < 4U; ++role) {
                    old_roles[role][value_plane].push_back(load_word(builder,
                        role_planes[role][value_plane], word,
                        "old.role.word"));
                }
                new_value[value_plane].push_back(load_word(builder,
                    pending_planes[value_plane], word, "new.value.word"));
                current_word_changed = builder.CreateOr(current_word_changed,
                    builder.CreateICmpNE(old_roles[0U][value_plane].back(),
                        new_value[value_plane].back(), "current.plane.changed"));
                stored_word_changed = builder.CreateOr(stored_word_changed,
                    builder.CreateICmpNE(old_roles[2U][value_plane].back(),
                        new_value[value_plane].back(), "stored.plane.changed"));
                owner_word_changed = builder.CreateOr(owner_word_changed,
                    builder.CreateICmpNE(old_roles[3U][value_plane].back(),
                        new_value[value_plane].back(), "owner.plane.changed"));
            }
            changed = builder.CreateOr(changed, current_word_changed,
                "current.value.changed");
            state_changed = builder.CreateOr(state_changed,
                builder.CreateOr(current_word_changed,
                    builder.CreateOr(stored_word_changed, owner_word_changed)),
                "role.state.changed");
        }

        for (std::uint32_t value_plane = 0U;
             value_plane < signal.plane_count; ++value_plane) {
            for (std::uint32_t word = 0U; word < signal.word_count; ++word) {
                store_word(builder, role_planes[2U][value_plane], word,
                    new_value[value_plane][word]);
                store_word(builder, role_planes[3U][value_plane], word,
                    new_value[value_plane][word]);
            }
        }

        store_field(builder, frame,
            offsetof(RegionFrontierFrameV2, current_commit_changed),
            builder.CreateZExt(changed, i32_type(builder)));
        const auto committed_count = load_field(builder, frame,
            offsetof(RegionFrontierFrameV2, committed_signal_count),
            i32_type(builder), "committed.signal.count");
        const auto committed_record = indexed_record_pointer(builder,
            frame_committed_signals, committed_count,
            sizeof(RegionFrontierCommittedSignalV2));
        store_field(builder, committed_record,
            offsetof(RegionFrontierCommittedSignalV2, signal_slot),
            llvm::ConstantInt::get(i32_type(builder), signal_slot));
        store_field(builder, committed_record,
            offsetof(RegionFrontierCommittedSignalV2, changed),
            builder.CreateZExt(changed, i32_type(builder)));
        store_field(builder, committed_record,
            offsetof(RegionFrontierCommittedSignalV2, state_changed),
            builder.CreateZExt(state_changed, i32_type(builder)));
        store_field(builder, frame,
            offsetof(RegionFrontierFrameV2, committed_signal_count),
            builder.CreateAdd(committed_count,
                llvm::ConstantInt::get(i32_type(builder), 1U)));

        const auto metadata_index = load_field(builder, plane,
            offsetof(RegionFrontierPlaneV2, metadata_index),
            i32_type(builder), "metadata.index");
        const auto metadata = indexed_record_pointer(builder, frame_metadata,
            metadata_index, sizeof(RegionFrontierSignalMetadataV2));
        const auto slot_time = load_field(builder, frame,
            offsetof(RegionFrontierFrameV2, slot)
                + offsetof(RegionFrontierSlotV2, time),
            i64_type(builder), "commit.time");
        const auto slot_delta = load_field(builder, frame,
            offsetof(RegionFrontierFrameV2, slot)
                + offsetof(RegionFrontierSlotV2, delta),
            i64_type(builder), "commit.delta");
        const auto next_delta = builder.CreateAdd(slot_delta,
            llvm::ConstantInt::get(i64_type(builder), 1U), "stamp.delta");
        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, transaction_time), slot_time);
        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, transaction_delta), next_delta);
        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, transaction_valid),
            llvm::ConstantInt::get(i8_type(builder), 1U));

        auto* const changed_block = llvm::BasicBlock::Create(builder.getContext(),
            "internal.commit.changed." + std::to_string(signal_slot),
            builder.GetInsertBlock()->getParent());
        auto* const unchanged_block = llvm::BasicBlock::Create(builder.getContext(),
            "internal.commit.unchanged." + std::to_string(signal_slot),
            builder.GetInsertBlock()->getParent());
        builder.CreateCondBr(changed, changed_block, unchanged_block);

        builder.SetInsertPoint(unchanged_block);
        builder.CreateBr(done);

        builder.SetInsertPoint(changed_block);
        for (std::uint32_t value_plane = 0U;
             value_plane < signal.plane_count; ++value_plane) {
            for (std::uint32_t word = 0U; word < signal.word_count; ++word) {
                store_word(builder, role_planes[1U][value_plane], word,
                    old_roles[0U][value_plane][word]);
                store_word(builder, role_planes[0U][value_plane], word,
                    new_value[value_plane][word]);
            }
        }

        auto* const revision_pointer = field_pointer(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, value_revision));
        const auto old_revision = builder.CreateLoad(i64_type(builder),
            revision_pointer, "old.value.revision");
        const auto incremented_revision = builder.CreateAdd(old_revision,
            llvm::ConstantInt::get(i64_type(builder), 1U), "next.value.revision");
        const auto wrapped_revision = builder.CreateSelect(
            builder.CreateICmpEQ(incremented_revision,
                llvm::ConstantInt::get(i64_type(builder), 0U)),
            llvm::ConstantInt::get(i64_type(builder), 1U), incremented_revision,
            "nonzero.value.revision");
        builder.CreateStore(wrapped_revision, revision_pointer);

        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, event_time), slot_time);
        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, event_delta), next_delta);
        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, event_valid),
            llvm::ConstantInt::get(i8_type(builder), 1U));
        const auto origin = field_pointer(builder, write,
            offsetof(RegionFrontierPendingWriteV2, origin));
        const auto origin_domain = load_field(builder, origin,
            offsetof(RegionFrontierKeyV2, process_domain),
            i32_type(builder), "origin.process.domain");
        const auto origin_phase = load_field(builder, origin,
            offsetof(RegionFrontierKeyV2, phase),
            i32_type(builder), "origin.phase");
        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, event_process_domain),
            origin_domain);
        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, event_phase), origin_phase);
        const auto is_systemverilog = builder.CreateICmpEQ(origin_domain,
            llvm::ConstantInt::get(i32_type(builder),
                static_cast<std::uint32_t>(ProcessSchedulingDomain::systemverilog)));
        const auto slot_round = load_field(builder, frame,
            offsetof(RegionFrontierFrameV2, slot)
                + offsetof(RegionFrontierSlotV2, systemverilog_round),
            i64_type(builder), "commit.sv.round");
        store_field(builder, metadata,
            offsetof(RegionFrontierSignalMetadataV2, systemverilog_round),
            builder.CreateSelect(is_systemverilog, slot_round,
                llvm::ConstantInt::get(i64_type(builder), 0U)));

        const auto commit_key = field_pointer(builder, write,
            offsetof(RegionFrontierPendingWriteV2, commit_key));
        const auto member_array = frame_members;
        // Per-instance trigger masks are copied into frame.fanout_edges after the
        // runtime generation check. Read those mutable bindings here; the compiled
        // layout's fanout array is immutable shared code metadata.
        // The compiler groups the immutable topology by signal. The entry
        // preflight authenticates those slots before mutation, so commits
        // visit only this signal's readers while retaining instance masks.
        std::uint32_t first_edge = 0U;
        while (first_edge < layout.fanout_edge_count
            && layout.fanout_edges[first_edge].signal_slot < signal_slot) {
            ++first_edge;
        }
        auto end_edge = first_edge;
        while (end_edge < layout.fanout_edge_count
            && layout.fanout_edges[end_edge].signal_slot == signal_slot) {
            ++end_edge;
        }
        if (first_edge == end_edge) {
            builder.CreateBr(done);
            return;
        }
        auto* const function = builder.GetInsertBlock()->getParent();
        auto* const preheader = builder.GetInsertBlock();
        auto* const fanout_condition = llvm::BasicBlock::Create(
            builder.getContext(), "internal.commit.fanout.condition", function);
        auto* const fanout_body = llvm::BasicBlock::Create(
            builder.getContext(), "internal.commit.fanout.body", function);
        auto* const fanout_next = llvm::BasicBlock::Create(
            builder.getContext(), "internal.commit.fanout.next", function);
        auto* const fanout_finished = llvm::BasicBlock::Create(
            builder.getContext(), "internal.commit.fanout.finished", function);
        builder.CreateBr(fanout_condition);

        builder.SetInsertPoint(fanout_condition);
        auto* const edge_index = builder.CreatePHI(i32_type(builder), 2U,
            "fanout.edge.index");
        edge_index->addIncoming(
            llvm::ConstantInt::get(i32_type(builder), first_edge),
            preheader);
        builder.CreateCondBr(builder.CreateICmpULT(edge_index,
                                 llvm::ConstantInt::get(
                                     i32_type(builder), end_edge)),
            fanout_body, fanout_finished);

        builder.SetInsertPoint(fanout_body);
        const auto edge = indexed_record_pointer(builder, frame_fanout_edges,
            edge_index, sizeof(RegionFrontierFanoutEdgeV2));
        const auto member_index = load_field(builder, edge,
            offsetof(RegionFrontierFanoutEdgeV2, member_index),
            i32_type(builder), "fanout.member.index");
        const auto trigger_mask = load_field(builder, edge,
            offsetof(RegionFrontierFanoutEdgeV2, trigger_mask),
            i64_type(builder), "fanout.trigger.mask");
        auto* const invalid_fanout_edge = llvm::BasicBlock::Create(
            builder.getContext(), "internal.commit.fanout.invalid-edge",
            function);
        auto* const sensitivity_join = llvm::BasicBlock::Create(
            builder.getContext(), "internal.commit.fanout.sensitivity-join",
            function);
        auto* const sensitivity_dispatch = builder.CreateSwitch(edge_index,
            invalid_fanout_edge, end_edge - first_edge);
        std::vector<std::pair<llvm::BasicBlock*, llvm::Value*>>
            sensitivity_cases;
        sensitivity_cases.reserve(end_edge - first_edge);
        for (std::uint32_t static_edge = first_edge;
             static_edge < end_edge; ++static_edge) {
            auto* const sensitivity_case = llvm::BasicBlock::Create(
                builder.getContext(),
                "internal.commit.fanout.sensitivity." + std::to_string(static_edge),
                function);
            sensitivity_dispatch->addCase(llvm::ConstantInt::get(
                i32_type(builder), static_edge), sensitivity_case);
            builder.SetInsertPoint(sensitivity_case);

            const auto& range_span = edge_range_spans[static_edge];
            llvm::Value* this_edge_changed
                = llvm::ConstantInt::getFalse(builder.getContext());
            for (std::uint32_t range_offset = 0U;
                 range_offset < range_span.range_count; ++range_offset) {
                const auto& range = ranges[
                    static_cast<std::size_t>(range_span.first_range)
                    + range_offset];
                this_edge_changed = builder.CreateOr(this_edge_changed,
                    emit_sensitivity_range_changed(builder, signal, range,
                        old_roles, new_value, changed),
                    "fanout.range.any.changed");
            }
            builder.CreateBr(sensitivity_join);
            sensitivity_cases.emplace_back(sensitivity_case,
                this_edge_changed);
        }
        builder.SetInsertPoint(invalid_fanout_edge);
        builder.CreateUnreachable();
        builder.SetInsertPoint(sensitivity_join);
        auto* const edge_sensitivity_changed = builder.CreatePHI(
            llvm::Type::getInt1Ty(builder.getContext()),
            static_cast<unsigned int>(sensitivity_cases.size()),
            "fanout.edge.sensitivity.changed");
        for (const auto& [sensitivity_case, range_changed] : sensitivity_cases) {
            edge_sensitivity_changed->addIncoming(
                range_changed, sensitivity_case);
        }
        const auto member = indexed_record_pointer(builder, member_array,
            member_index, sizeof(RegionFrontierMemberV2));
        const auto previous_trigger_mask = load_field(builder, member,
            offsetof(RegionFrontierMemberV2, static_trigger_mask),
            i64_type(builder), "member.trigger.mask");
        store_field(builder, member,
            offsetof(RegionFrontierMemberV2, static_trigger_mask),
            builder.CreateSelect(edge_sensitivity_changed,
                builder.CreateOr(previous_trigger_mask, trigger_mask),
                previous_trigger_mask));

        const auto member_flags = load_field(builder, member,
            offsetof(RegionFrontierMemberV2, flags), i32_type(builder),
            "member.flags");
        const auto waiting = builder.CreateICmpNE(
            builder.CreateAnd(member_flags,
                llvm::ConstantInt::get(i32_type(builder),
                    RegionFrontierMemberFlagsV2::waiting_on_static)),
            llvm::ConstantInt::get(i32_type(builder), 0U));
        const auto queued = builder.CreateICmpNE(
            builder.CreateAnd(member_flags,
                llvm::ConstantInt::get(i32_type(builder),
                    RegionFrontierMemberFlagsV2::queued)),
            llvm::ConstantInt::get(i32_type(builder), 0U));
        const auto pending_activation = builder.CreateICmpNE(
            builder.CreateAnd(member_flags,
                llvm::ConstantInt::get(i32_type(builder),
                    RegionFrontierMemberFlagsV2::pending_activation)),
            llvm::ConstantInt::get(i32_type(builder), 0U));
        const auto has_trigger_mask = builder.CreateICmpNE(trigger_mask,
            llvm::ConstantInt::get(i64_type(builder), 0U));
        const auto enqueue = builder.CreateAnd(edge_sensitivity_changed,
            builder.CreateAnd(has_trigger_mask,
                builder.CreateAnd(waiting,
                    builder.CreateAnd(builder.CreateNot(queued),
                        builder.CreateNot(pending_activation)))));
        auto* const append = llvm::BasicBlock::Create(builder.getContext(),
            "internal.commit.fanout.append", function);
        auto* const after_append = llvm::BasicBlock::Create(builder.getContext(),
            "internal.commit.fanout.after-append", function);
        builder.CreateCondBr(enqueue, append, after_append);

        builder.SetInsertPoint(append);
        const auto event_count = load_field(builder, frame,
            offsetof(RegionFrontierFrameV2, staged_event_count),
            i32_type(builder), "staged.event.count");
        const auto staged_event = indexed_record_pointer(builder, frame_events,
            event_count, sizeof(RegionFrontierStagedEventV2));
        store_field(builder, staged_event,
            offsetof(RegionFrontierStagedEventV2, kind),
            llvm::ConstantInt::get(i32_type(builder),
                static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::member_activation)));
        store_field(builder, staged_event,
            offsetof(RegionFrontierStagedEventV2, descriptor_index), member_index);
        const auto member_process_id = load_field(builder, member,
            offsetof(RegionFrontierMemberV2, process_id),
            i32_type(builder), "member.process.id");
        store_field(builder, staged_event,
            offsetof(RegionFrontierStagedEventV2, stable_order),
            builder.CreateZExt(member_process_id, i64_type(builder)));
        const auto staged_origin = field_pointer(builder, staged_event,
            offsetof(RegionFrontierStagedEventV2, origin));
        copy_key_fields(builder, staged_origin, commit_key);
        const auto pending_origin = field_pointer(builder, member,
            offsetof(RegionFrontierMemberV2, pending_activation_origin));
        copy_key_fields(builder, pending_origin, commit_key);
        store_field(builder, member,
            offsetof(RegionFrontierMemberV2, flags),
            builder.CreateOr(member_flags,
                llvm::ConstantInt::get(i32_type(builder),
                    RegionFrontierMemberFlagsV2::pending_activation)));
        store_field(builder, frame,
            offsetof(RegionFrontierFrameV2, staged_event_count),
            builder.CreateAdd(event_count,
                llvm::ConstantInt::get(i32_type(builder), 1U)));
        builder.CreateBr(after_append);

        builder.SetInsertPoint(after_append);
        builder.CreateBr(fanout_next);
        builder.SetInsertPoint(fanout_next);
        auto* const next_index = builder.CreateAdd(edge_index,
            llvm::ConstantInt::get(i32_type(builder), 1U),
            "fanout.next.edge.index");
        builder.CreateBr(fanout_condition);
        edge_index->addIncoming(next_index, fanout_next);

        builder.SetInsertPoint(fanout_finished);
        builder.CreateBr(done);
    }

} // namespace

EmitCertifiedInternalCommitV2 make_region_frontier_internal_commit_emitter_v2(
    const RegionFrontierLayoutV2& layout,
    const std::span<const RegionFrontierFanoutRangeSpan> edge_range_spans,
    const std::span<const RegionFrontierFanoutSensitivityRange> ranges)
{
    if (layout.execution_mode
        == RegionFrontierExecutionModeV2::generic_deferred_update) {
        // Generic Update values remain private until the host stages them in
        // the ordinary scheduler queue. This callback is unreachable because
        // generic task preflight admits member activations only.
        return [](Builder&, llvm::Value*, llvm::Value*) { };
    }
    if (layout.execution_mode
        != RegionFrontierExecutionModeV2::systemverilog_active) {
        throw std::invalid_argument {
            "invalid region frontier execution mode"
        };
    }
    if (!layout_is_well_formed(layout)) {
        throw std::invalid_argument {
            "invalid region frontier commit layout"
        };
    }
    if (!fanout_ranges_are_well_formed(layout, edge_range_spans, ranges)) {
        throw std::invalid_argument {
            "invalid region frontier fanout sensitivity ranges"
        };
    }

    const auto* const layout_pointer = &layout;
    const std::vector<RegionFrontierFanoutRangeSpan> captured_edge_ranges {
        edge_range_spans.begin(), edge_range_spans.end() };
    const std::vector<RegionFrontierFanoutSensitivityRange> captured_ranges {
        ranges.begin(), ranges.end() };
    return [layout_pointer, captured_edge_ranges, captured_ranges](Builder& builder,
               llvm::Value* pending_write_index, llvm::Value* frame) {
        const auto& captured_layout = *layout_pointer;
        auto* const function = builder.GetInsertBlock()->getParent();
        auto& context = builder.getContext();
        auto* const i32 = i32_type(builder);
        auto* const frame_planes = load_frame_pointer(builder, frame,
            offsetof(RegionFrontierFrameV2, planes), "planes.ptr");
        auto* const frame_metadata = load_frame_pointer(builder, frame,
            offsetof(RegionFrontierFrameV2, metadata), "metadata.ptr");
        auto* const frame_members = load_frame_pointer(builder, frame,
            offsetof(RegionFrontierFrameV2, members), "members.ptr");
        auto* const frame_events = load_frame_pointer(builder, frame,
            offsetof(RegionFrontierFrameV2, staged_events), "events.ptr");
        auto* const frame_fanout_edges = load_frame_pointer(builder, frame,
            offsetof(RegionFrontierFrameV2, fanout_edges), "fanout.edges.ptr");
        auto* const frame_committed_signals = load_frame_pointer(builder, frame,
            offsetof(RegionFrontierFrameV2, committed_signals),
            "committed.signals.ptr");
        auto* const writes = load_frame_pointer(builder, frame,
            offsetof(RegionFrontierFrameV2, pending_writes), "writes.ptr");
        auto* const write = indexed_record_pointer(builder, writes,
            pending_write_index, sizeof(RegionFrontierPendingWriteV2));
        const auto signal_slot = load_field(builder, write,
            offsetof(RegionFrontierPendingWriteV2, signal_slot), i32,
            "write.signal.slot");

        auto* const done = llvm::BasicBlock::Create(context,
            "internal.commit.done", function);
        auto* const invalid = llvm::BasicBlock::Create(context,
            "internal.commit.invalid", function);
        auto* const dispatch = builder.CreateSwitch(signal_slot, invalid,
            captured_layout.signal_slot_count);
        std::vector<llvm::BasicBlock*> cases(
            captured_layout.signal_slot_count, nullptr);
        for (std::uint32_t signal_slot_index = 0U;
            signal_slot_index < captured_layout.signal_slot_count;
            ++signal_slot_index) {
            if ((captured_layout.signals[signal_slot_index].flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                == 0U) {
                continue;
            }
            auto* const block = llvm::BasicBlock::Create(context,
                "internal.commit.signal." + std::to_string(signal_slot_index),
                function);
            cases[signal_slot_index] = block;
            dispatch->addCase(llvm::ConstantInt::get(i32,
                                  signal_slot_index),
                block);
        }
        builder.SetInsertPoint(invalid);
        builder.CreateUnreachable();

        for (std::uint32_t signal_slot_index = 0U;
            signal_slot_index < captured_layout.signal_slot_count;
            ++signal_slot_index) {
            if ((captured_layout.signals[signal_slot_index].flags
                    & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
                == 0U) {
                continue;
            }
            builder.SetInsertPoint(cases[signal_slot_index]);
            emit_one_signal_commit(builder, captured_layout,
                captured_edge_ranges, captured_ranges,
                signal_slot_index, pending_write_index, frame, frame_planes,
                frame_metadata, frame_members, frame_events,
                frame_fanout_edges, frame_committed_signals, done);
        }
        builder.SetInsertPoint(done);
    };
}

void validate_region_frontier_internal_commit_layout_v2(
    const RegionFrontierLayoutV2& layout)
{
    if (layout.execution_mode
        == RegionFrontierExecutionModeV2::generic_deferred_update) {
        return;
    }
    if (!layout_is_well_formed(layout)) {
        throw std::invalid_argument {
            "invalid region frontier commit layout"
        };
    }
}

} // namespace fsim::runtime::simir::scratch
