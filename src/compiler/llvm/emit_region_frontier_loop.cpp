// SPDX-License-Identifier: Apache-2.0
// Scratch-only LLVM emitter for a scheduler-authenticated SV event prefix.
#include "region_frontier_codegen.hpp"

#include <llvm/ADT/Twine.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace fsim::runtime::simir::scratch {

static llvm::Type* i8_type(llvm::IRBuilder<>& builder)
{
    return llvm::Type::getInt8Ty(builder.getContext());
}

static llvm::Type* i32_type(llvm::IRBuilder<>& builder)
{
    return llvm::Type::getInt32Ty(builder.getContext());
}

static llvm::Type* i64_type(llvm::IRBuilder<>& builder)
{
    return llvm::Type::getInt64Ty(builder.getContext());
}

static llvm::Value* byte_pointer(llvm::IRBuilder<>& builder,
    llvm::Value* base, llvm::Value* byte_offset)
{
    return builder.CreateInBoundsGEP(i8_type(builder), base, byte_offset);
}

static llvm::Value* constant_offset(llvm::IRBuilder<>& builder,
    llvm::Value* base, const std::size_t byte_offset)
{
    return byte_pointer(builder, base,
        llvm::ConstantInt::get(i64_type(builder), byte_offset));
}

static llvm::Value* load_at(llvm::IRBuilder<>& builder, llvm::Value* base,
    const std::size_t byte_offset, llvm::Type* type,
    const llvm::Twine& name)
{
    return builder.CreateLoad(type,
        constant_offset(builder, base, byte_offset), name);
}

static llvm::Value* store_at(llvm::IRBuilder<>& builder, llvm::Value* base,
    const std::size_t byte_offset, llvm::Value* value)
{
    return builder.CreateStore(value,
        constant_offset(builder, base, byte_offset));
}

static llvm::Value* indexed_pointer(llvm::IRBuilder<>& builder,
    llvm::Value* base, llvm::Value* index, const std::size_t element_size)
{
    const auto scaled = builder.CreateMul(builder.CreateZExt(index, i64_type(builder)),
        llvm::ConstantInt::get(i64_type(builder), element_size));
    return byte_pointer(builder, base, scaled);
}

static llvm::Value* load_array_field(llvm::IRBuilder<>& builder,
    llvm::Value* base, llvm::Value* index, const std::size_t element_size,
    const std::size_t field_offset, llvm::Type* type,
    const llvm::Twine& name)
{
    return builder.CreateLoad(type,
        constant_offset(builder,
            indexed_pointer(builder, base, index, element_size), field_offset),
        name);
}

static llvm::Value* store_array_field(llvm::IRBuilder<>& builder,
    llvm::Value* base, llvm::Value* index, const std::size_t element_size,
    const std::size_t field_offset, llvm::Value* value)
{
    return builder.CreateStore(value,
        constant_offset(builder,
            indexed_pointer(builder, base, index, element_size), field_offset));
}

static llvm::Value* load_frame(llvm::IRBuilder<>& builder,
    llvm::Value* frame, const std::size_t field_offset,
    llvm::Type* type, const llvm::Twine& name)
{
    return load_at(builder, frame, field_offset, type, name);
}

static llvm::Value* store_frame(llvm::IRBuilder<>& builder,
    llvm::Value* frame, const std::size_t field_offset, llvm::Value* value)
{
    return store_at(builder, frame, field_offset, value);
}

static llvm::Value* load_frame_pointer(llvm::IRBuilder<>& builder,
    llvm::Value* frame, const std::size_t field_offset,
    const llvm::Twine& name)
{
    return load_frame(builder, frame, field_offset,
        llvm::PointerType::getUnqual(builder.getContext()), name);
}

static llvm::Value* load_task_field(llvm::IRBuilder<>& builder,
    llvm::Value* tasks, llvm::Value* task_index,
    const std::size_t field_offset, const llvm::Twine& name)
{
    return load_array_field(builder, tasks, task_index,
        sizeof(RegionFrontierSchedulerTaskV1), field_offset,
        i64_type(builder), name);
}

static llvm::Value* member_field(llvm::IRBuilder<>& builder,
    llvm::Value* members, llvm::Value* member_index,
    const std::size_t field_offset, llvm::Type* type,
    const llvm::Twine& name)
{
    return load_array_field(builder, members, member_index,
        sizeof(RegionFrontierMemberV1), field_offset, type, name);
}

static llvm::Value* write_field(llvm::IRBuilder<>& builder,
    llvm::Value* writes, llvm::Value* write_index,
    const std::size_t field_offset, llvm::Type* type,
    const llvm::Twine& name)
{
    return load_array_field(builder, writes, write_index,
        sizeof(RegionFrontierPendingWriteV1), field_offset, type, name);
}

static llvm::Value* emit_equal(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right);

static llvm::Value* emit_and(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right);

static llvm::Value* emit_all_flags_set(llvm::IRBuilder<>& builder,
    llvm::Value* flags, const std::uint32_t mask);

static llvm::Value* pending_site_identity_matches(
    llvm::IRBuilder<>& builder, llvm::Value* write,
    const RegionFrontierWriteSiteV1& site)
{
    const auto i32 = i32_type(builder);
    auto* matches = emit_equal(builder,
        load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, member_index), i32,
            "site.member.index"),
        llvm::ConstantInt::get(i32, site.member_index));
    matches = emit_and(builder, matches, emit_equal(builder,
        load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, signal_slot), i32,
            "site.signal.slot"),
        llvm::ConstantInt::get(i32, site.signal_slot)));
    matches = emit_and(builder, matches, emit_equal(builder,
        load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, source_instruction), i32,
            "site.source.instruction"),
        llvm::ConstantInt::get(i32, site.source_instruction)));
    matches = emit_and(builder, matches, emit_equal(builder,
        load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, update_kind), i32,
            "site.update.kind"),
        llvm::ConstantInt::get(i32, site.update_kind)));
    matches = emit_and(builder, matches, emit_equal(builder,
        load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, width), i32,
            "site.width"),
        llvm::ConstantInt::get(i32, site.width)));
    return emit_and(builder, matches, emit_equal(builder,
        load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, word_count), i32,
            "site.word.count"),
        llvm::ConstantInt::get(i32, site.word_count)));
}

static llvm::Value* pending_target_flags_match(
    llvm::IRBuilder<>& builder, llvm::Value* flags,
    const RegionFrontierWriteSiteV1& site,
    const bool allow_committed = false)
{
    const auto i32 = i32_type(builder);
    const auto target_flag = site.event_kind
            == static_cast<std::uint32_t>(
                RegionFrontierEventKindV1::internal_commit)
        ? RegionFrontierPendingWriteFlagsV1::pending_internal_target
        : RegionFrontierPendingWriteFlagsV1::pending_boundary_target;
    const auto required = RegionFrontierPendingWriteFlagsV1::pending_active
        | RegionFrontierPendingWriteFlagsV1::pending_value_ready
        | RegionFrontierPendingWriteFlagsV1::pending_key_assigned
        | target_flag;
    auto* matches = emit_all_flags_set(builder, flags, required);
    const auto other_target = target_flag
            == RegionFrontierPendingWriteFlagsV1::pending_internal_target
        ? RegionFrontierPendingWriteFlagsV1::pending_boundary_target
        : RegionFrontierPendingWriteFlagsV1::pending_internal_target;
    matches = emit_and(builder, matches,
        builder.CreateICmpEQ(builder.CreateAnd(flags,
                                  llvm::ConstantInt::get(i32, other_target)),
            llvm::ConstantInt::get(i32, 0U)));
    constexpr auto known_flags
        = RegionFrontierPendingWriteFlagsV1::pending_active
        | RegionFrontierPendingWriteFlagsV1::pending_value_ready
        | RegionFrontierPendingWriteFlagsV1::pending_key_assigned
        | RegionFrontierPendingWriteFlagsV1::pending_internal_target
        | RegionFrontierPendingWriteFlagsV1::pending_boundary_target
        | RegionFrontierPendingWriteFlagsV1::pending_committed;
    matches = emit_and(builder, matches,
        builder.CreateICmpEQ(builder.CreateAnd(flags,
                                  llvm::ConstantInt::get(i32, ~known_flags)),
            llvm::ConstantInt::get(i32, 0U)));
    if (!allow_committed) {
        matches = emit_and(builder, matches,
            builder.CreateICmpEQ(builder.CreateAnd(flags,
                                      llvm::ConstantInt::get(i32,
                                          RegionFrontierPendingWriteFlagsV1::pending_committed)),
                llvm::ConstantInt::get(i32, 0U)));
    }
    return matches;
}

template<typename Field>
static llvm::Value* select_write_site_field(llvm::IRBuilder<>& builder,
    llvm::Value* pending_slot, const RegionFrontierLayoutV1& layout,
    Field&& field, const std::uint32_t invalid_value)
{
    llvm::Value* result = llvm::ConstantInt::get(i32_type(builder),
        invalid_value);
    for (std::size_t index = layout.write_site_count; index > 0U; --index) {
        const auto site_index = static_cast<std::uint32_t>(index - 1U);
        const auto selected = emit_equal(builder, pending_slot,
            llvm::ConstantInt::get(i32_type(builder), site_index));
        result = builder.CreateSelect(selected,
            llvm::ConstantInt::get(i32_type(builder),
                field(layout.write_sites[site_index])), result,
            "static.write.site.field");
    }
    return result;
}

static llvm::Value* pointer_ranges_disjoint(llvm::IRBuilder<>& builder,
    llvm::Value* first, llvm::Value* first_bytes,
    llvm::Value* second, llvm::Value* second_bytes)
{
    const auto i64 = i64_type(builder);
    const auto first_begin = builder.CreatePtrToInt(first, i64, "range.first");
    const auto second_begin = builder.CreatePtrToInt(second, i64, "range.second");
    const auto first_end = builder.CreateAdd(first_begin, first_bytes,
        "range.first.end");
    const auto second_end = builder.CreateAdd(second_begin, second_bytes,
        "range.second.end");
    const auto no_wrap = builder.CreateAnd(
        builder.CreateICmpUGE(first_end, first_begin),
        builder.CreateICmpUGE(second_end, second_begin));
    const auto separated = builder.CreateOr(
        builder.CreateICmpULE(first_end, second_begin),
        builder.CreateICmpULE(second_end, first_begin));
    return builder.CreateAnd(no_wrap, separated);
}

static llvm::Value* pointer_ranges_disjoint(llvm::IRBuilder<>& builder,
    llvm::Value* first, const std::uint64_t first_bytes,
    llvm::Value* second, const std::uint64_t second_bytes)
{
    const auto i64 = i64_type(builder);
    return pointer_ranges_disjoint(builder, first,
        llvm::ConstantInt::get(i64, first_bytes), second,
        llvm::ConstantInt::get(i64, second_bytes));
}

static llvm::Value* byte_count(llvm::IRBuilder<>& builder,
    llvm::Value* count, const std::size_t element_size)
{
    return builder.CreateMul(builder.CreateZExt(count, i64_type(builder)),
        llvm::ConstantInt::get(i64_type(builder), element_size),
        "frame.array.bytes");
}

static llvm::Value* frame_array_storage_is_valid(
    llvm::IRBuilder<>& builder, llvm::Value* pointer, llvm::Value* count,
    const std::size_t alignment)
{
    const auto is_empty = builder.CreateICmpEQ(count,
        llvm::ConstantInt::get(count->getType(), 0U));
    const auto nonnull = builder.CreateIsNotNull(pointer);
    const auto address = builder.CreatePtrToInt(pointer, i64_type(builder),
        "frame.array.address");
    const auto aligned = builder.CreateICmpEQ(
        builder.CreateAnd(address,
            llvm::ConstantInt::get(i64_type(builder), alignment - 1U)),
        llvm::ConstantInt::get(i64_type(builder), 0U));
    return builder.CreateOr(is_empty, builder.CreateAnd(nonnull, aligned));
}

static llvm::Value* pointer_ranges_disjoint_or_equal(
    llvm::IRBuilder<>& builder, llvm::Value* first,
    const std::uint64_t first_bytes, llvm::Value* second,
    const std::uint64_t second_bytes)
{
    const auto same_range = builder.CreateAnd(
        builder.CreateICmpEQ(first, second),
        llvm::ConstantInt::getTrue(builder.getContext()));
    return builder.CreateOr(same_range,
        pointer_ranges_disjoint(builder, first, first_bytes,
            second, second_bytes));
}

static llvm::Value* emit_equal(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right)
{
    return builder.CreateICmpEQ(left, right);
}

static llvm::Value* emit_and(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right)
{
    return builder.CreateAnd(left, right);
}

static llvm::Value* emit_flag_set(llvm::IRBuilder<>& builder,
    llvm::Value* flags, const std::uint32_t flag)
{
    return builder.CreateICmpNE(
        builder.CreateAnd(flags, llvm::ConstantInt::get(i32_type(builder), flag)),
        llvm::ConstantInt::get(i32_type(builder), 0U));
}

static llvm::Value* emit_all_flags_set(llvm::IRBuilder<>& builder,
    llvm::Value* flags, const std::uint32_t mask)
{
    return builder.CreateICmpEQ(
        builder.CreateAnd(flags,
            llvm::ConstantInt::get(i32_type(builder), mask)),
        llvm::ConstantInt::get(i32_type(builder), mask));
}

static llvm::Value* emit_slot_matches_key(llvm::IRBuilder<>& builder,
    llvm::Value* frame, llvm::Value* key_base,
    const bool allow_pending_next_round = false)
{
    const auto time = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV1, time), i64_type(builder), "key.time");
    const auto delta = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV1, delta), i64_type(builder), "key.delta");
    const auto round = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV1, systemverilog_round), i64_type(builder),
        "key.sv.round");
    const auto domain = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV1, process_domain), i32_type(builder),
        "key.process.domain");
    const auto phase = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV1, phase), i32_type(builder), "key.phase");
    const auto slot_offset = offsetof(RegionFrontierFrameV1, slot);
    auto* matches = emit_equal(builder, time,
        load_frame(builder, frame, slot_offset + offsetof(RegionFrontierSlotV1, time),
            i64_type(builder), "slot.time"));
    matches = emit_and(builder, matches, emit_equal(builder, delta,
        load_frame(builder, frame, slot_offset + offsetof(RegionFrontierSlotV1, delta),
            i64_type(builder), "slot.delta")));
    const auto slot_round = load_frame(builder, frame,
        slot_offset + offsetof(RegionFrontierSlotV1, systemverilog_round),
        i64_type(builder), "slot.sv.round");
    auto* round_matches = emit_equal(builder, round, slot_round);
    if (allow_pending_next_round) {
        // A compact reservation publishes new Active tasks into the pending
        // queue. Retained writes may therefore belong to the next round while
        // an unconsumed suffix of the current round is offered again. This
        // permission applies to retained descriptors only; execution of an
        // offered task still requires an exact current-slot key below.
        const auto can_advance = builder.CreateICmpNE(slot_round,
            llvm::ConstantInt::get(i64_type(builder), UINT64_MAX));
        const auto next_round = builder.CreateAdd(slot_round,
            llvm::ConstantInt::get(i64_type(builder), 1U));
        round_matches = builder.CreateOr(round_matches,
            builder.CreateAnd(can_advance,
                emit_equal(builder, round, next_round)));
    }
    matches = emit_and(builder, matches, round_matches);
    matches = emit_and(builder, matches, emit_equal(builder, domain,
        load_frame(builder, frame,
            slot_offset + offsetof(RegionFrontierSlotV1, process_domain),
            i32_type(builder), "slot.process.domain")));
    return emit_and(builder, matches, emit_equal(builder, phase,
        load_frame(builder, frame,
            slot_offset + offsetof(RegionFrontierSlotV1, phase),
            i32_type(builder), "slot.phase")));
}

static llvm::Value* emit_keys_equal(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right)
{
    constexpr std::size_t u64_fields[] {
        offsetof(RegionFrontierKeyV1, time),
        offsetof(RegionFrontierKeyV1, delta),
        offsetof(RegionFrontierKeyV1, systemverilog_round),
        offsetof(RegionFrontierKeyV1, stable_order),
        offsetof(RegionFrontierKeyV1, sequence),
    };
    constexpr std::size_t u32_fields[] {
        offsetof(RegionFrontierKeyV1, process_domain),
        offsetof(RegionFrontierKeyV1, phase),
    };
    llvm::Value* matches = llvm::ConstantInt::getTrue(builder.getContext());
    for (const auto offset : u64_fields) {
        matches = emit_and(builder, matches,
            emit_equal(builder,
                load_at(builder, left, offset, i64_type(builder), "left.key"),
                load_at(builder, right, offset, i64_type(builder), "right.key")));
    }
    for (const auto offset : u32_fields) {
        matches = emit_and(builder, matches,
            emit_equal(builder,
                load_at(builder, left, offset, i32_type(builder), "left.key"),
                load_at(builder, right, offset, i32_type(builder), "right.key")));
    }
    return matches;
}

static llvm::Value* emit_key_before(llvm::IRBuilder<>& builder,
    llvm::Value* left_stable, llvm::Value* left_sequence,
    llvm::Value* right_stable, llvm::Value* right_sequence)
{
    const auto stable_less = builder.CreateICmpULT(left_stable, right_stable);
    const auto stable_equal = emit_equal(builder, left_stable, right_stable);
    const auto sequence_less = builder.CreateICmpULT(left_sequence, right_sequence);
    return builder.CreateOr(stable_less,
        builder.CreateAnd(stable_equal, sequence_less), "key.before");
}

static void return_status(llvm::IRBuilder<>& builder,
    const RegionFrontierStatusV1 status)
{
    builder.CreateRet(llvm::ConstantInt::get(i32_type(builder),
        static_cast<std::uint32_t>(status)));
}

static llvm::Value* task_payload_kind(llvm::IRBuilder<>& builder,
    llvm::Value* payload)
{
    return builder.CreateTrunc(
        builder.CreateLShr(payload,
            llvm::ConstantInt::get(i64_type(builder),
                kRegionFrontierPayloadKindShiftV1)), i8_type(builder));
}

static llvm::Value* task_payload_index(llvm::IRBuilder<>& builder,
    llvm::Value* payload)
{
    const auto masked = builder.CreateAnd(payload,
        llvm::ConstantInt::get(i64_type(builder),
            kRegionFrontierPayloadIndexMaskV1));
    return builder.CreateTrunc(masked, i32_type(builder));
}

static llvm::Value* task_payload_index_wide(llvm::IRBuilder<>& builder,
    llvm::Value* payload)
{
    return builder.CreateAnd(payload,
        llvm::ConstantInt::get(i64_type(builder),
            kRegionFrontierPayloadIndexMaskV1));
}

/// Emits a native queue-order loop over the exact borrowed scheduler prefix.
/// The generated code validates the complete offered suffix before its first
/// store, dispatches compiled member bodies, performs compiled internal
/// commits, and leaves boundary publication to its original host adapter.
///
/// The body callbacks are compile-time emitters, never runtime callbacks.
/// Each member emitter stages values and appends one scheduler event for each
/// WriteUpdate into the preallocated arrays. Each commit emitter performs the
/// complete certified internal commit and fanout directly in generated code.
llvm::Function* emit_region_frontier_loop(llvm::Module& module,
    const std::string& symbol,
    const RegionFrontierLayoutV1& layout,
    const EmitCertifiedMemberBody& emit_member,
    const EmitCertifiedInternalCommit& emit_internal_commit)
{
    const auto member_count = static_cast<std::size_t>(layout.member_count);
    const auto max_member_write_counts = std::span<const std::uint32_t> {
        layout.max_member_write_counts, member_count };
    const auto max_member_staged_event_counts
        = std::span<const std::uint32_t> {
            layout.max_member_staged_event_counts, member_count };
    const auto max_commit_fanout_events = layout.max_commit_fanout_events;
    assert(member_count != 0U);
    assert(max_member_staged_event_counts.size() == member_count);
    assert(static_cast<bool>(emit_member));
    assert(static_cast<bool>(emit_internal_commit));

    auto max_member_writes = std::uint32_t { 0U };
    auto max_member_events = std::uint32_t { 0U };
    for (std::size_t i = 0U; i < member_count; ++i) {
        max_member_writes = std::max(max_member_writes,
            max_member_write_counts[i]);
        max_member_events = std::max(max_member_events,
            max_member_staged_event_counts[i]);
    }

    auto& context = module.getContext();
    auto* const i8 = llvm::Type::getInt8Ty(context);
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const frame_pointer = llvm::PointerType::getUnqual(context);
    auto* const function_type = llvm::FunctionType::get(
        i32, { frame_pointer }, false);
    auto* const function = llvm::Function::Create(function_type,
        llvm::GlobalValue::ExternalLinkage, symbol, module);
    auto* const frame = function->getArg(0U);
    frame->setName("instance_frame");

    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto* const preflight = llvm::BasicBlock::Create(context, "preflight", function);
    auto* const stale = llvm::BasicBlock::Create(context, "stale", function);
    auto* const decline = llvm::BasicBlock::Create(context, "decline", function);
    auto* const need_keys = llvm::BasicBlock::Create(context, "need.scheduler.keys", function);
    auto* const stopped = llvm::BasicBlock::Create(context, "stopped", function);
    auto* const preflight_tasks = llvm::BasicBlock::Create(
        context, "preflight.tasks", function);
    auto* const initialize_validation = llvm::BasicBlock::Create(
        context, "initialize.validation", function);
    auto* const load_validation_task = llvm::BasicBlock::Create(
        context, "load.validation.task", function);
    auto* const validate_activation = llvm::BasicBlock::Create(
        context, "validate.activation", function);
    auto* const validate_write = llvm::BasicBlock::Create(
        context, "validate.write", function);
    auto* const validate_boundary_ack = llvm::BasicBlock::Create(
        context, "validate.boundary.ack", function);
    auto* const validate_activation_capacity = llvm::BasicBlock::Create(
        context, "validate.activation.capacity", function);
    auto* const validate_write_plane = llvm::BasicBlock::Create(
        context, "validate.write.plane", function);
    auto* const validate_write_capacity = llvm::BasicBlock::Create(
        context, "validate.write.capacity", function);
    auto* const validate_boundary_capacity = llvm::BasicBlock::Create(
        context, "validate.boundary.capacity", function);
    auto* const ordered_validation_task = llvm::BasicBlock::Create(
        context, "ordered.validation.task", function);
    auto* const validate_advance = llvm::BasicBlock::Create(
        context, "validate.advance", function);
    auto* const dispatch_loop = llvm::BasicBlock::Create(
        context, "dispatch.loop", function);
    auto* const dispatch_task = llvm::BasicBlock::Create(
        context, "dispatch.task", function);
    auto* const dispatch_activation = llvm::BasicBlock::Create(
        context, "dispatch.activation", function);
    auto* const dispatch_internal = llvm::BasicBlock::Create(
        context, "dispatch.internal.commit", function);
    auto* const dispatch_boundary = llvm::BasicBlock::Create(
        context, "dispatch.boundary.commit", function);
    auto* const dispatch_invalid = llvm::BasicBlock::Create(
        context, "dispatch.invalid", function);
    auto* const cut_check = llvm::BasicBlock::Create(
        context, "cut.check", function);
    auto* const dispatch_ready = llvm::BasicBlock::Create(
        context, "dispatch.ready", function);
    auto* const capacity_ok = llvm::BasicBlock::Create(
        context, "capacity.ok", function);
    auto* const member_done = llvm::BasicBlock::Create(
        context, "member.done", function);
    auto* const internal_done = llvm::BasicBlock::Create(
        context, "internal.commit.done", function);
    auto* const boundary_ack = llvm::BasicBlock::Create(
        context, "boundary.ack", function);
    auto* const at_end = llvm::BasicBlock::Create(context, "at.end", function);
    auto* const cut_yield = llvm::BasicBlock::Create(
        context, "cut.yield", function);
    auto* const boundary_publication = llvm::BasicBlock::Create(
        context, "boundary.publication", function);
    auto* const capacity_yield = llvm::BasicBlock::Create(
        context, "capacity.yield", function);
    auto* const quiescent = llvm::BasicBlock::Create(
        context, "quiescent", function);

    llvm::IRBuilder<> builder { entry };
    builder.CreateCondBr(builder.CreateIsNotNull(frame), preflight, decline);

    builder.SetInsertPoint(preflight);
    const auto abi_version = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, abi_version), i32, "abi.version");
    const auto struct_size = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, struct_size), i32, "abi.size");
    const auto runtime_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, runtime_generation), i64,
        "runtime.generation");
    const auto bound_runtime_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, bound_runtime_generation), i64,
        "bound.runtime.generation");
    const auto certificate_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, certificate_generation), i64,
        "certificate.generation");
    const auto component_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, component_generation), i64,
        "component.generation");
    const auto scheduler_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_frontier_generation), i64,
        "scheduler.frontier.generation");
    const auto member_count_value = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, member_count), i32, "member.count");
    const auto task_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_task_count), i32,
        "scheduler.task.count");
    const auto task_cursor = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_task_cursor), i32,
        "scheduler.task.cursor");
    const auto task_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_task_capacity), i32,
        "scheduler.task.capacity");
    const auto readiness_words = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, readiness_word_count), i32,
        "readiness.words");
    const auto pending_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, pending_write_capacity), i32,
        "pending.write.capacity");
    const auto pending_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, pending_write_count), i32,
        "pending.write.count");
    const auto event_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, staged_event_capacity), i32,
        "staged.event.capacity");
    const auto event_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, staged_event_count), i32,
        "staged.event.count");
    const auto plane_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, plane_count), i32, "plane.count");
    const auto metadata_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, metadata_count), i32,
        "metadata.count");
    const auto fanout_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, fanout_edge_count), i32,
        "fanout.count");
    const auto committed_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, committed_signal_capacity), i32,
        "committed.signal.capacity");
    const auto committed_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, committed_signal_count), i32,
        "committed.signal.count");
    const auto cut_kind = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, cut)
            + offsetof(RegionFrontierCutV1, kind), i8, "cut.kind");
    const auto cut_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, cut)
            + offsetof(RegionFrontierCutV1, scheduler_frontier_generation),
        i64, "cut.scheduler.generation");

    const auto expected_i32 = [i32](llvm::IRBuilder<>&,
                                    const std::uint32_t value) {
        return llvm::ConstantInt::get(i32, value);
    };
    const auto expected_i64 = [i64](llvm::IRBuilder<>&,
                                    const std::uint64_t value) {
        return llvm::ConstantInt::get(i64, value);
    };
    auto* generations_match = builder.CreateAnd(
        builder.CreateICmpNE(runtime_generation, expected_i64(builder, 0U)),
        emit_equal(builder, runtime_generation, bound_runtime_generation));
    generations_match = emit_and(builder, generations_match,
        emit_equal(builder, certificate_generation,
            expected_i64(builder, layout.certificate_generation)));
    generations_match = emit_and(builder, generations_match,
        emit_equal(builder, component_generation,
            expected_i64(builder, layout.component_generation)));
    generations_match = emit_and(builder, generations_match,
        emit_equal(builder, scheduler_generation, cut_generation));

    const auto same_slot_cut = emit_equal(builder, cut_kind,
        llvm::ConstantInt::get(i8, static_cast<std::uint8_t>(
            RegionFrontierCutKindV1::same_slot_key)));
    const auto closed_prefix_cut = emit_equal(builder, cut_kind,
        llvm::ConstantInt::get(i8, static_cast<std::uint8_t>(
            RegionFrontierCutKindV1::closed_prefix)));
    const auto cut_known = builder.CreateOr(same_slot_cut, closed_prefix_cut);
    llvm::Value* cut_slot_matches = llvm::ConstantInt::getTrue(context);
    auto* const cut_key_pointer = constant_offset(builder, frame,
        offsetof(RegionFrontierFrameV1, cut)
            + offsetof(RegionFrontierCutV1, next_key));
    cut_slot_matches = emit_slot_matches_key(builder, frame, cut_key_pointer);

    const auto expected_member_count = expected_i32(builder,
        static_cast<std::uint32_t>(member_count));
    const auto expected_words = expected_i32(builder,
        static_cast<std::uint32_t>((member_count + 63U) / 64U));
    auto* static_ok = emit_equal(builder, abi_version,
        expected_i32(builder, kRegionFrontierAbiVersionV1));
    static_ok = emit_and(builder, static_ok, emit_equal(builder, struct_size,
        expected_i32(builder, sizeof(RegionFrontierFrameV1))));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        member_count_value, expected_member_count));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        plane_count, expected_i32(builder, layout.plane_count)));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        metadata_count, expected_i32(builder, layout.metadata_count)));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        fanout_count, expected_i32(builder, layout.fanout_edge_count)));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        readiness_words, expected_words));
    static_ok = emit_and(builder, static_ok,
        builder.CreateICmpULE(task_cursor, task_count));
    static_ok = emit_and(builder, static_ok,
        builder.CreateICmpULE(task_count, task_capacity));
    static_ok = emit_and(builder, static_ok,
        builder.CreateICmpULE(pending_count, pending_capacity));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        pending_capacity,
        expected_i32(builder, layout.pending_write_capacity)));
    static_ok = emit_and(builder, static_ok,
        builder.CreateICmpULE(event_count, event_capacity));
    static_ok = emit_and(builder, static_ok,
        builder.CreateICmpULE(event_capacity,
            expected_i32(builder, layout.staged_event_capacity)));
    static_ok = emit_and(builder, static_ok,
        emit_equal(builder, committed_capacity,
            expected_i32(builder, layout.committed_signal_capacity)));
    static_ok = emit_and(builder, static_ok,
        emit_equal(builder, committed_capacity,
            expected_i32(builder, layout.pending_write_capacity)));
    static_ok = emit_and(builder, static_ok,
        builder.CreateICmpULE(committed_count, committed_capacity));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        load_frame(builder, frame,
            offsetof(RegionFrontierFrameV1, slot)
                + offsetof(RegionFrontierSlotV1, process_domain), i32,
            "slot.domain"),
        expected_i32(builder, static_cast<std::uint32_t>(
                                  ProcessSchedulingDomain::systemverilog))));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        load_frame(builder, frame,
            offsetof(RegionFrontierFrameV1, slot)
                + offsetof(RegionFrontierSlotV1, phase), i32,
            "slot.phase"),
        expected_i32(builder, static_cast<std::uint32_t>(
                                  SchedulerPhase::active))));
    const auto event_bound = std::max(max_member_events,
        max_commit_fanout_events);
    static_ok = emit_and(builder, static_ok,
        builder.CreateICmpUGE(pending_capacity,
            expected_i32(builder, max_member_writes)));
    static_ok = emit_and(builder, static_ok,
        builder.CreateICmpUGE(event_capacity,
            expected_i32(builder, event_bound)));
    static_ok = emit_and(builder, static_ok, cut_known);
    static_ok = emit_and(builder, static_ok,
        builder.CreateOr(builder.CreateNot(same_slot_cut), cut_slot_matches));

    auto* const ready_words_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, ready_words), "ready.words.ptr");
    auto* const members_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, members), "members.ptr");
    auto* const tasks_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_tasks), "tasks.ptr");
    auto* const writes_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, pending_writes), "writes.ptr");
    auto* const staged_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, staged_events), "staged.events.ptr");
    auto* const committed_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, committed_signals),
        "committed.signals.ptr");
    auto* arrays_ok = builder.CreateAnd(
        builder.CreateIsNotNull(ready_words_ptr),
        builder.CreateIsNotNull(members_ptr));
    arrays_ok = emit_and(builder, arrays_ok,
        builder.CreateOr(emit_equal(builder, task_count,
                           expected_i32(builder, 0U)),
            builder.CreateIsNotNull(tasks_ptr)));
    arrays_ok = emit_and(builder, arrays_ok,
        builder.CreateOr(emit_equal(builder, pending_capacity,
                           expected_i32(builder, 0U)),
            builder.CreateIsNotNull(writes_ptr)));
    arrays_ok = emit_and(builder, arrays_ok,
        builder.CreateOr(emit_equal(builder, event_capacity,
                           expected_i32(builder, 0U)),
            builder.CreateIsNotNull(staged_ptr)));
    arrays_ok = emit_and(builder, arrays_ok,
        builder.CreateOr(emit_equal(builder, committed_capacity,
                           expected_i32(builder, 0U)),
            builder.CreateIsNotNull(committed_ptr)));
    static_ok = emit_and(builder, static_ok, arrays_ok);

    const auto planes_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, planes), "planes.ptr");
    const auto metadata_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, metadata), "metadata.ptr");
    const auto fanout_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, fanout_edges), "fanout.ptr");
    const auto port_planes_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, port_planes), "port.planes.ptr");
    const auto dispatch_counter_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, native_frontier_member_dispatches),
        "dispatch.counter.ptr");
    const auto stop_requested_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, stop_requested), "stop.requested.ptr");
    auto* state_arrays_ok = builder.CreateAnd(
        builder.CreateOr(emit_equal(builder, plane_count, expected_i32(builder, 0U)),
            builder.CreateAnd(builder.CreateIsNotNull(planes_ptr),
                builder.CreateIsNotNull(port_planes_ptr))),
        builder.CreateAnd(
            builder.CreateOr(emit_equal(builder, metadata_count,
                    expected_i32(builder, 0U)),
                builder.CreateIsNotNull(metadata_ptr)),
            builder.CreateOr(emit_equal(builder, fanout_count,
                    expected_i32(builder, 0U)),
                builder.CreateIsNotNull(fanout_ptr))));
    static_ok = emit_and(builder, static_ok, state_arrays_ok);

    struct FrameRange {
        llvm::Value* pointer { };
        llvm::Value* bytes { };
    };
    std::vector<FrameRange> frame_ranges;
    llvm::Value* frame_ranges_ok = llvm::ConstantInt::getTrue(context);
    const auto append_frame_range = [&](llvm::Value* pointer,
                                        llvm::Value* count,
                                        const std::size_t element_size,
                                        const std::size_t alignment) {
        frame_ranges_ok = emit_and(builder, frame_ranges_ok,
            frame_array_storage_is_valid(builder, pointer, count, alignment));
        frame_ranges.push_back({ pointer,
            byte_count(builder, count, element_size) });
    };
    const auto append_optional_frame_range = [&](llvm::Value* pointer,
                                                 const std::size_t element_size,
                                                 const std::size_t alignment) {
        const auto present = builder.CreateIsNotNull(pointer);
        append_frame_range(pointer, builder.CreateZExt(present, i32),
            element_size, alignment);
    };
    append_frame_range(frame, expected_i32(builder, 1U),
        sizeof(RegionFrontierFrameV1), alignof(RegionFrontierFrameV1));
    append_frame_range(ready_words_ptr, readiness_words,
        sizeof(std::uint64_t), alignof(std::uint64_t));
    append_frame_range(members_ptr, member_count_value,
        sizeof(RegionFrontierMemberV1), alignof(RegionFrontierMemberV1));
    append_frame_range(tasks_ptr, task_count,
        sizeof(RegionFrontierSchedulerTaskV1),
        alignof(RegionFrontierSchedulerTaskV1));
    append_frame_range(planes_ptr, plane_count,
        sizeof(RegionFrontierPlaneV1), alignof(RegionFrontierPlaneV1));
    append_frame_range(metadata_ptr, metadata_count,
        sizeof(RegionFrontierSignalMetadataV1),
        alignof(RegionFrontierSignalMetadataV1));
    append_frame_range(fanout_ptr, fanout_count,
        sizeof(RegionFrontierFanoutEdgeV1),
        alignof(RegionFrontierFanoutEdgeV1));
    append_frame_range(port_planes_ptr, plane_count, sizeof(void*),
        alignof(void*));
    append_frame_range(writes_ptr, pending_capacity,
        sizeof(RegionFrontierPendingWriteV1),
        alignof(RegionFrontierPendingWriteV1));
    append_frame_range(staged_ptr, event_capacity,
        sizeof(RegionFrontierStagedEventV1),
        alignof(RegionFrontierStagedEventV1));
    append_frame_range(committed_ptr, committed_capacity,
        sizeof(RegionFrontierCommittedSignalV1),
        alignof(RegionFrontierCommittedSignalV1));
    append_optional_frame_range(dispatch_counter_ptr, sizeof(std::uint64_t),
        alignof(std::uint64_t));
    append_optional_frame_range(stop_requested_ptr, sizeof(std::uint32_t),
        alignof(std::uint32_t));
    for (std::size_t left = 0U; left < frame_ranges.size(); ++left) {
        for (std::size_t right = left + 1U; right < frame_ranges.size(); ++right) {
            frame_ranges_ok = emit_and(builder, frame_ranges_ok,
                pointer_ranges_disjoint(builder,
                    frame_ranges[left].pointer, frame_ranges[left].bytes,
                    frame_ranges[right].pointer, frame_ranges[right].bytes));
        }
    }
    static_ok = emit_and(builder, static_ok, frame_ranges_ok);

    auto* const valid_generation = llvm::BasicBlock::Create(
        context, "generation.valid", function);
    builder.CreateCondBr(generations_match, valid_generation, stale);
    builder.SetInsertPoint(stale);
    return_status(builder, RegionFrontierStatusV1::stale_generation);

    builder.SetInsertPoint(valid_generation);
    builder.CreateCondBr(static_ok, preflight_tasks, decline);

    builder.SetInsertPoint(preflight_tasks);
    // A retained staged suffix is retried by the scheduler wrapper first.
    builder.CreateCondBr(builder.CreateICmpNE(event_count,
            expected_i32(builder, 0U)), need_keys, initialize_validation);
    builder.SetInsertPoint(initialize_validation);
    auto* const validate_index = builder.CreateAlloca(i32, nullptr,
        "validate.task.index");
    auto* const live_pending_count = builder.CreateAlloca(i64, nullptr,
        "validate.live.pending.count");
    auto* const live_event_count = builder.CreateAlloca(i64, nullptr,
        "validate.live.event.count");
    auto* const live_committed_count = builder.CreateAlloca(i64, nullptr,
        "validate.live.committed.count");
    auto* const slot_states = builder.CreateAlloca(i8,
        expected_i32(builder,
            std::max(layout.pending_write_capacity, 1U)),
        "validate.pending.slot.states");
    auto* const previous_stable_order = builder.CreateAlloca(i64, nullptr,
        "previous.stable.order");
    auto* const previous_sequence = builder.CreateAlloca(i64, nullptr,
        "previous.sequence");
    auto* const has_previous_task = builder.CreateAlloca(
        llvm::Type::getInt1Ty(context), nullptr, "has.previous.task");
    builder.CreateStore(task_cursor, validate_index);
    builder.CreateStore(expected_i64(builder, 0U), live_pending_count);
    builder.CreateStore(builder.CreateZExt(event_count, i64),
        live_event_count);
    builder.CreateStore(builder.CreateZExt(committed_count, i64),
        live_committed_count);
    builder.CreateStore(expected_i64(builder, 0U), previous_stable_order);
    builder.CreateStore(expected_i64(builder, 0U), previous_sequence);
    builder.CreateStore(llvm::ConstantInt::getFalse(context), has_previous_task);

    struct PlaneBuffer {
        llvm::Value* pointer { };
        std::uint64_t bytes { };
        std::uint32_t signal_slot { };
        std::uint32_t role { };
    };
    std::vector<PlaneBuffer> mutable_buffers;
    std::vector<PlaneBuffer> boundary_buffers;
    std::vector<PlaneBuffer> pending_buffers;
    llvm::Value* plane_map_ok = llvm::ConstantInt::getTrue(context);
    auto* const i8_pointer = llvm::PointerType::getUnqual(context);
    for (std::uint32_t member_index = 0U;
         member_index < layout.member_count; ++member_index) {
        auto* const member = indexed_pointer(builder, members_ptr,
            expected_i32(builder, member_index), sizeof(RegionFrontierMemberV1));
        plane_map_ok = emit_and(builder, plane_map_ok, emit_equal(builder,
            load_at(builder, member,
                offsetof(RegionFrontierMemberV1, process_id), i32,
                "mapped.member.process"),
            expected_i32(builder, layout.members[member_index].process_id)));
    }
    for (std::uint32_t edge_index = 0U;
         edge_index < layout.fanout_edge_count; ++edge_index) {
        const auto& expected_edge = layout.fanout_edges[edge_index];
        auto* const edge = indexed_pointer(builder, fanout_ptr,
            expected_i32(builder, edge_index),
            sizeof(RegionFrontierFanoutEdgeV1));
        plane_map_ok = emit_and(builder, plane_map_ok, emit_equal(builder,
            load_at(builder, edge,
                offsetof(RegionFrontierFanoutEdgeV1, signal_slot), i32,
                "mapped.fanout.signal"),
            expected_i32(builder, expected_edge.signal_slot)));
        plane_map_ok = emit_and(builder, plane_map_ok, emit_equal(builder,
            load_at(builder, edge,
                offsetof(RegionFrontierFanoutEdgeV1, member_index), i32,
                "mapped.fanout.member"),
            expected_i32(builder, expected_edge.member_index)));
        plane_map_ok = emit_and(builder, plane_map_ok,
            builder.CreateICmpNE(load_at(builder, edge,
                                     offsetof(RegionFrontierFanoutEdgeV1,
                                         trigger_mask), i64,
                                     "mapped.fanout.trigger.mask"),
                expected_i64(builder, 0U)));
    }
    const auto add_nonnull_aligned = [&](llvm::Value* pointer) {
        auto* valid = builder.CreateIsNotNull(pointer);
        const auto address = builder.CreatePtrToInt(pointer, i64,
            "plane.pointer.address");
        const auto aligned = builder.CreateICmpEQ(
            builder.CreateAnd(address,
                expected_i64(builder, alignof(std::uint64_t) - 1U)),
            expected_i64(builder, 0U));
        return emit_and(builder, valid, aligned);
    };
    const auto append_mutable_buffer = [&](const PlaneBuffer buffer) {
        for (const auto& frame_range : frame_ranges) {
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, buffer.pointer,
                    llvm::ConstantInt::get(i64, buffer.bytes),
                    frame_range.pointer, frame_range.bytes));
        }
        for (const auto& previous : mutable_buffers) {
            const bool stored_owner_pair
                = previous.signal_slot == buffer.signal_slot
                && ((previous.role == 4U && buffer.role == 6U)
                    || (previous.role == 6U && buffer.role == 4U)
                    || (previous.role == 5U && buffer.role == 7U)
                    || (previous.role == 7U && buffer.role == 5U));
            const auto disjoint = stored_owner_pair
                ? pointer_ranges_disjoint_or_equal(builder,
                    previous.pointer, previous.bytes,
                    buffer.pointer, buffer.bytes)
                : pointer_ranges_disjoint(builder,
                    previous.pointer, previous.bytes,
                    buffer.pointer, buffer.bytes);
            plane_map_ok = emit_and(builder, plane_map_ok, disjoint);
        }
        for (const auto& boundary : boundary_buffers) {
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, boundary.pointer,
                    boundary.bytes, buffer.pointer, buffer.bytes));
        }
        mutable_buffers.push_back(buffer);
    };
    const auto append_boundary_buffer = [&](const PlaneBuffer buffer) {
        for (const auto& frame_range : frame_ranges) {
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, buffer.pointer,
                    llvm::ConstantInt::get(i64, buffer.bytes),
                    frame_range.pointer, frame_range.bytes));
        }
        for (const auto& mutable_buffer : mutable_buffers) {
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, mutable_buffer.pointer,
                    mutable_buffer.bytes, buffer.pointer, buffer.bytes));
        }
        boundary_buffers.push_back(buffer);
    };

    const std::size_t role_offsets[] {
        offsetof(RegionFrontierPlaneV1, current_aval),
        offsetof(RegionFrontierPlaneV1, current_bval),
        offsetof(RegionFrontierPlaneV1, previous_aval),
        offsetof(RegionFrontierPlaneV1, previous_bval),
        offsetof(RegionFrontierPlaneV1, stored_aval),
        offsetof(RegionFrontierPlaneV1, stored_bval),
        offsetof(RegionFrontierPlaneV1, owner_aval),
        offsetof(RegionFrontierPlaneV1, owner_bval),
    };
    for (std::uint32_t signal_slot = 0U;
         signal_slot < layout.plane_count; ++signal_slot) {
        const auto& expected_plane = layout.signals[signal_slot];
        auto* const plane = indexed_pointer(builder, planes_ptr,
            expected_i32(builder, signal_slot), sizeof(RegionFrontierPlaneV1));
        auto* matches = emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, signal_id), i32,
                "mapped.signal.id"),
            expected_i32(builder, expected_plane.signal_id));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, owner_process_id), i32,
                "mapped.owner.process"),
            expected_i32(builder, expected_plane.owner_process_id)));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, width), i32,
                "mapped.width"),
            expected_i32(builder, expected_plane.width)));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, word_count), i32,
                "mapped.word.count"),
            expected_i32(builder, expected_plane.word_count)));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, flags), i32,
                "mapped.flags"),
            expected_i32(builder, expected_plane.flags)));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, metadata_index), i32,
                "mapped.metadata.index"),
            expected_i32(builder, expected_plane.metadata_index)));
        const auto port_plane = load_array_field(builder, port_planes_ptr,
            expected_i32(builder, signal_slot), sizeof(void*), 0U,
            i8_pointer, "mapped.port.plane");
        matches = emit_and(builder, matches,
            builder.CreateICmpEQ(port_plane, plane));

        const bool internal
            = (expected_plane.flags
                & RegionFrontierPlaneFlagsV1::certified_internal_single_owner)
            != 0U;
        const auto byte_count = static_cast<std::uint64_t>(
            expected_plane.word_count) * sizeof(std::uint64_t);
        if (internal) {
            for (std::uint32_t role = 0U; role < 8U; ++role) {
                const auto pointer = load_at(builder, plane, role_offsets[role],
                    i8_pointer, "mapped.mutable.plane");
                matches = emit_and(builder, matches,
                    add_nonnull_aligned(pointer));
                append_mutable_buffer({ pointer, byte_count,
                    signal_slot, role });
            }
        } else {
            // Read-only boundary planes may expose only their two immutable
            // input buffers. A non-null mutable A4 role would let malformed
            // frame data redirect a later internal write through a boundary
            // slot, so authenticate every mutable role as absent here.
            for (const auto role_offset : role_offsets) {
                const auto role_pointer = load_at(builder, plane,
                    role_offset, i8_pointer, "mapped.boundary.mutable.role");
                matches = emit_and(builder, matches,
                    builder.CreateIsNull(role_pointer));
            }
            const auto boundary_aval = load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, boundary_aval),
                i8_pointer, "mapped.boundary.aval");
            const auto boundary_bval = load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, boundary_bval),
                i8_pointer, "mapped.boundary.bval");
            matches = emit_and(builder, matches,
                add_nonnull_aligned(boundary_aval));
            matches = emit_and(builder, matches,
                add_nonnull_aligned(boundary_bval));
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, boundary_aval,
                    byte_count, boundary_bval, byte_count));
            append_boundary_buffer({ boundary_aval, byte_count,
                signal_slot, 8U });
            append_boundary_buffer({ boundary_bval, byte_count,
                signal_slot, 9U });
        }
        plane_map_ok = emit_and(builder, plane_map_ok, matches);
    }

    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        auto* const write = indexed_pointer(builder, writes_ptr,
            expected_i32(builder, site.pending_slot),
            sizeof(RegionFrontierPendingWriteV1));
        const auto write_aval = load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, aval), i8_pointer,
            "mapped.pending.aval");
        const auto write_bval = load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, bval), i8_pointer,
            "mapped.pending.bval");
        plane_map_ok = emit_and(builder, plane_map_ok,
            add_nonnull_aligned(write_aval));
        plane_map_ok = emit_and(builder, plane_map_ok,
            add_nonnull_aligned(write_bval));
        const auto byte_count = static_cast<std::uint64_t>(site.word_count)
            * sizeof(std::uint64_t);
        plane_map_ok = emit_and(builder, plane_map_ok,
            pointer_ranges_disjoint(builder, write_aval, byte_count,
                write_bval, byte_count));
        for (const auto& role : mutable_buffers) {
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, write_aval, byte_count,
                    role.pointer, role.bytes));
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, write_bval, byte_count,
                    role.pointer, role.bytes));
        }
        for (const auto& frame_range : frame_ranges) {
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, write_aval,
                    llvm::ConstantInt::get(i64, byte_count),
                    frame_range.pointer, frame_range.bytes));
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, write_bval,
                    llvm::ConstantInt::get(i64, byte_count),
                    frame_range.pointer, frame_range.bytes));
        }
        for (const auto& boundary : boundary_buffers) {
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, write_aval, byte_count,
                    boundary.pointer, boundary.bytes));
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, write_bval, byte_count,
                    boundary.pointer, boundary.bytes));
        }
        for (const auto& previous : pending_buffers) {
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, write_aval, byte_count,
                    previous.pointer, previous.bytes));
            plane_map_ok = emit_and(builder, plane_map_ok,
                pointer_ranges_disjoint(builder, write_bval, byte_count,
                    previous.pointer, previous.bytes));
        }
        pending_buffers.push_back({ write_aval, byte_count,
            site.signal_slot, 10U });
        pending_buffers.push_back({ write_bval, byte_count,
            site.signal_slot, 11U });
    }

    auto* const validate_plane_tails = llvm::BasicBlock::Create(
        context, "validate.plane.tails", function);
    auto* const initialize_slots = llvm::BasicBlock::Create(
        context, "initialize.pending.slots", function);
    auto* const validate_loop = llvm::BasicBlock::Create(
        context, "validate.loop", function);
    builder.CreateCondBr(plane_map_ok, validate_plane_tails, decline);
    builder.SetInsertPoint(validate_plane_tails);
    llvm::Value* tail_words_ok = llvm::ConstantInt::getTrue(context);
    const auto tail_is_canonical = [&](llvm::Value* pointer,
                                       const std::uint32_t width,
                                       const std::uint32_t word_count) {
        if (width % 64U == 0U) {
            return static_cast<llvm::Value*>(
                llvm::ConstantInt::getTrue(context));
        }
        const auto last_word = load_array_field(builder, pointer,
            expected_i32(builder, word_count - 1U), sizeof(std::uint64_t),
            0U, i64, "tail.word");
        const auto valid_mask = (UINT64_C(1) << (width % 64U)) - 1U;
        const auto high_bits = builder.CreateAnd(last_word,
            expected_i64(builder, ~valid_mask));
        return static_cast<llvm::Value*>(emit_equal(builder, high_bits,
            expected_i64(builder, 0U)));
    };
    for (std::uint32_t signal_slot = 0U;
         signal_slot < layout.plane_count; ++signal_slot) {
        const auto& expected_plane = layout.signals[signal_slot];
        if (expected_plane.word_count == 0U) {
            tail_words_ok = emit_and(builder, tail_words_ok,
                llvm::ConstantInt::getFalse(context));
            continue;
        }
        auto* const plane = indexed_pointer(builder, planes_ptr,
            expected_i32(builder, signal_slot), sizeof(RegionFrontierPlaneV1));
        const bool internal
            = (expected_plane.flags
                & RegionFrontierPlaneFlagsV1::certified_internal_single_owner)
            != 0U;
        if (internal) {
            for (const auto role_offset : role_offsets) {
                auto* const pointer = load_at(builder, plane, role_offset,
                    i8_pointer, "tail.mutable.plane");
                tail_words_ok = emit_and(builder, tail_words_ok,
                    tail_is_canonical(pointer, expected_plane.width,
                        expected_plane.word_count));
            }
        } else {
            auto* const boundary_aval = load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, boundary_aval), i8_pointer,
                "tail.boundary.aval");
            auto* const boundary_bval = load_at(builder, plane,
                offsetof(RegionFrontierPlaneV1, boundary_bval), i8_pointer,
                "tail.boundary.bval");
            tail_words_ok = emit_and(builder, tail_words_ok,
                tail_is_canonical(boundary_aval, expected_plane.width,
                    expected_plane.word_count));
            tail_words_ok = emit_and(builder, tail_words_ok,
                tail_is_canonical(boundary_bval, expected_plane.width,
                    expected_plane.word_count));
        }
    }
    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        auto* const write = indexed_pointer(builder, writes_ptr,
            expected_i32(builder, site.pending_slot),
            sizeof(RegionFrontierPendingWriteV1));
        auto* const aval = load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, aval), i8_pointer,
            "tail.pending.aval");
        auto* const bval = load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, bval), i8_pointer,
            "tail.pending.bval");
        tail_words_ok = emit_and(builder, tail_words_ok,
            tail_is_canonical(aval, site.width, site.word_count));
        tail_words_ok = emit_and(builder, tail_words_ok,
            tail_is_canonical(bval, site.width, site.word_count));
    }
    builder.CreateCondBr(tail_words_ok, initialize_slots, decline);
    builder.SetInsertPoint(initialize_slots);

    auto* slot_scan_block = builder.GetInsertBlock();
    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        builder.SetInsertPoint(slot_scan_block);
        const auto& site = layout.write_sites[site_index];
        auto* const write = indexed_pointer(builder, writes_ptr,
            expected_i32(builder, site.pending_slot),
            sizeof(RegionFrontierPendingWriteV1));
        const auto flags = load_at(builder, write,
            offsetof(RegionFrontierPendingWriteV1, flags), i32,
            "initial.write.flags");
        auto* const active_block = llvm::BasicBlock::Create(context,
            "initial.slot.active", function);
        auto* const inactive_block = llvm::BasicBlock::Create(context,
            "initial.slot.inactive", function);
        auto* const next_block = llvm::BasicBlock::Create(context,
            "initial.slot.next", function);
        builder.SetInsertPoint(slot_scan_block);
        builder.CreateCondBr(emit_flag_set(builder, flags,
                RegionFrontierPendingWriteFlagsV1::pending_active),
            active_block, inactive_block);
        builder.SetInsertPoint(inactive_block);
        auto* const inactive_state = builder.CreateInBoundsGEP(i8,
            slot_states, expected_i64(builder, site.pending_slot));
        builder.CreateStore(llvm::ConstantInt::get(i8, 0U), inactive_state);
        builder.CreateBr(next_block);

        builder.SetInsertPoint(active_block);
        const auto commit_key = constant_offset(builder, write,
            offsetof(RegionFrontierPendingWriteV1, commit_key));
        const bool is_boundary = site.event_kind
            == static_cast<std::uint32_t>(
                RegionFrontierEventKindV1::boundary_commit);
        auto* active_valid = pending_target_flags_match(builder, flags,
            site, is_boundary);
        active_valid = emit_and(builder, active_valid,
            pending_site_identity_matches(builder, write, site));
        active_valid = emit_and(builder, active_valid,
            emit_slot_matches_key(builder, frame, commit_key, true));
        const auto write_origin = constant_offset(builder, write,
            offsetof(RegionFrontierPendingWriteV1, origin));
        const auto member_pointer = indexed_pointer(builder, members_ptr,
            expected_i32(builder, site.member_index),
            sizeof(RegionFrontierMemberV1));
        const auto activation_origin = constant_offset(builder, member_pointer,
            offsetof(RegionFrontierMemberV1, activation_origin));
        active_valid = emit_and(builder, active_valid,
            emit_keys_equal(builder, write_origin, activation_origin));
        active_valid = emit_and(builder, active_valid, emit_equal(builder,
            load_at(builder, write_origin,
                offsetof(RegionFrontierKeyV1, process_domain), i32,
                "active.origin.domain"),
            expected_i32(builder, static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog))));
        active_valid = emit_and(builder, active_valid, emit_equal(builder,
            load_at(builder, write_origin,
                offsetof(RegionFrontierKeyV1, phase), i32,
                "active.origin.phase"),
            expected_i32(builder, static_cast<std::uint32_t>(
                SchedulerPhase::active))));
        auto* const active_ok = llvm::BasicBlock::Create(context,
            "initial.slot.active.valid", function);
        builder.CreateCondBr(active_valid, active_ok, decline);
        builder.SetInsertPoint(active_ok);
        auto* const active_state = builder.CreateInBoundsGEP(i8,
            slot_states, expected_i64(builder, site.pending_slot));
        builder.CreateStore(llvm::ConstantInt::get(i8, 1U), active_state);
        const auto active_count = builder.CreateLoad(i64, live_pending_count);
        builder.CreateStore(builder.CreateAdd(active_count,
            expected_i64(builder, 1U)), live_pending_count);
        builder.CreateBr(next_block);
        slot_scan_block = next_block;
    }
    builder.SetInsertPoint(slot_scan_block);
    const auto actual_pending_count = builder.CreateLoad(i64,
        live_pending_count, "actual.initial.pending.count");
    const auto declared_pending_count = builder.CreateZExt(pending_count, i64);
    builder.CreateCondBr(emit_equal(builder, actual_pending_count,
            declared_pending_count), validate_loop, decline);
    builder.SetInsertPoint(validate_loop);
    const auto scan_index = builder.CreateLoad(i32, validate_index,
        "task.index");
    builder.CreateCondBr(builder.CreateICmpULT(scan_index, task_count),
        load_validation_task, dispatch_loop);

    // Dispatch through an event-kind switch after a read-only full-span scan.
    builder.SetInsertPoint(load_validation_task);
    const auto validation_task_payload = load_task_field(builder, tasks_ptr,
        scan_index, offsetof(RegionFrontierSchedulerTaskV1, payload),
        "validation.payload");
    const auto validation_kind = task_payload_kind(builder,
        validation_task_payload);
    const auto task_stable = load_task_field(builder, tasks_ptr, scan_index,
        offsetof(RegionFrontierSchedulerTaskV1, stable_order), "task.stable");
    const auto task_sequence = load_task_field(builder, tasks_ptr, scan_index,
        offsetof(RegionFrontierSchedulerTaskV1, sequence), "task.sequence");
    const auto prior_stable = builder.CreateLoad(i64, previous_stable_order,
        "prior.stable.order");
    const auto prior_sequence = builder.CreateLoad(i64, previous_sequence,
        "prior.sequence");
    const auto has_prior_task = builder.CreateLoad(
        llvm::Type::getInt1Ty(context), has_previous_task, "has.prior.task");
    const auto strictly_after_prior = emit_key_before(builder,
        prior_stable, prior_sequence, task_stable, task_sequence);
    const auto order_ok = builder.CreateOr(builder.CreateNot(has_prior_task),
        strictly_after_prior);
    builder.CreateCondBr(order_ok, ordered_validation_task, decline);

    builder.SetInsertPoint(ordered_validation_task);
    const auto task_before_cut = emit_key_before(builder, task_stable,
        task_sequence,
        load_frame(builder, frame,
            offsetof(RegionFrontierFrameV1, cut)
                + offsetof(RegionFrontierCutV1, next_key)
                + offsetof(RegionFrontierKeyV1, stable_order), i64,
            "preflight.cut.stable"),
        load_frame(builder, frame,
            offsetof(RegionFrontierFrameV1, cut)
                + offsetof(RegionFrontierCutV1, next_key)
                + offsetof(RegionFrontierKeyV1, sequence), i64,
            "preflight.cut.sequence"));
    const auto contributes_to_prefix = builder.CreateOr(
        builder.CreateNot(same_slot_cut), task_before_cut);
    auto* const validation_switch = builder.CreateSwitch(validation_kind,
        decline, 3U);
    validation_switch->addCase(llvm::ConstantInt::get(i8,
            static_cast<std::uint8_t>(RegionFrontierEventKindV1::member_activation)),
        validate_activation);
    validation_switch->addCase(llvm::ConstantInt::get(i8,
            static_cast<std::uint8_t>(RegionFrontierEventKindV1::internal_commit)),
        validate_write);
    validation_switch->addCase(llvm::ConstantInt::get(i8,
            static_cast<std::uint8_t>(RegionFrontierEventKindV1::boundary_commit)),
        validate_boundary_ack);

    builder.SetInsertPoint(validate_activation);
    const auto activation_index_wide = task_payload_index_wide(builder,
        validation_task_payload);
    const auto activation_in_range = builder.CreateICmpULT(
        activation_index_wide, builder.CreateZExt(member_count_value, i64));
    const auto activation_index = builder.CreateTrunc(activation_index_wide, i32);
    auto* const validate_activation_fields = llvm::BasicBlock::Create(
        context, "validate.activation.fields", function);
    builder.CreateCondBr(activation_in_range, validate_activation_fields, decline);
    builder.SetInsertPoint(validate_activation_fields);
    const auto activation_flags = member_field(builder, members_ptr,
        activation_index, offsetof(RegionFrontierMemberV1, flags), i32,
        "activation.flags");
    auto* activation_ok = emit_all_flags_set(builder, activation_flags,
        RegionFrontierMemberFlagsV1::queued
            | RegionFrontierMemberFlagsV1::queued_key_valid);
    activation_ok = emit_and(builder, activation_ok,
        builder.CreateNot(emit_flag_set(builder, activation_flags,
            RegionFrontierMemberFlagsV1::executing)));
    auto* const queued_key_pointer = constant_offset(builder,
        indexed_pointer(builder, members_ptr, activation_index,
            sizeof(RegionFrontierMemberV1)),
        offsetof(RegionFrontierMemberV1, queued_key));
    activation_ok = emit_and(builder, activation_ok,
        emit_slot_matches_key(builder, frame, queued_key_pointer));
    const auto queued_stable = load_at(builder, queued_key_pointer,
        offsetof(RegionFrontierKeyV1, stable_order), i64, "queued.stable");
    const auto queued_sequence = load_at(builder, queued_key_pointer,
        offsetof(RegionFrontierKeyV1, sequence), i64, "queued.sequence");
    activation_ok = emit_and(builder, activation_ok,
        emit_equal(builder, queued_stable, task_stable));
    activation_ok = emit_and(builder, activation_ok,
        emit_equal(builder, queued_sequence, task_sequence));
    const auto ready_array = builder.CreateBitCast(ready_words_ptr,
        llvm::PointerType::getUnqual(builder.getContext()));
    const auto ready_word_index = builder.CreateUDiv(activation_index,
        expected_i32(builder, 64U));
    const auto ready_bit_index = builder.CreateURem(activation_index,
        expected_i32(builder, 64U));
    const auto ready_word_pointer = builder.CreateInBoundsGEP(i64, ready_array,
        builder.CreateZExt(ready_word_index, i64));
    const auto ready_word = builder.CreateLoad(i64, ready_word_pointer,
        "activation.ready.word");
    const auto ready_mask = builder.CreateShl(expected_i64(builder, 1U),
        builder.CreateZExt(ready_bit_index, i64));
    activation_ok = emit_and(builder, activation_ok,
        builder.CreateICmpNE(builder.CreateAnd(ready_word, ready_mask),
            expected_i64(builder, 0U)));
    builder.CreateCondBr(activation_ok,
        validate_activation_capacity, decline);

    builder.SetInsertPoint(validate_activation_capacity);
    llvm::Value* member_write_bound = expected_i32(builder, 0U);
    llvm::Value* member_event_bound = expected_i32(builder, 0U);
    for (std::size_t index = member_count; index > 0U; --index) {
        const auto member_index = static_cast<std::uint32_t>(index - 1U);
        const auto matches_member = emit_equal(builder, activation_index,
            expected_i32(builder, member_index));
        member_write_bound = builder.CreateSelect(matches_member,
            expected_i32(builder, max_member_write_counts[index - 1U]),
            member_write_bound);
        member_event_bound = builder.CreateSelect(matches_member,
            expected_i32(builder,
                max_member_staged_event_counts[index - 1U]),
            member_event_bound);
    }
    member_write_bound = builder.CreateSelect(contributes_to_prefix,
        member_write_bound, expected_i32(builder, 0U));
    member_event_bound = builder.CreateSelect(contributes_to_prefix,
        member_event_bound, expected_i32(builder, 0U));
    const auto pending_after_member_bound = builder.CreateAdd(
        builder.CreateLoad(i64, live_pending_count),
        builder.CreateZExt(member_write_bound, i64));
    const auto events_after_member_bound = builder.CreateAdd(
        builder.CreateLoad(i64, live_event_count),
        builder.CreateZExt(member_event_bound, i64));
    const auto member_capacity_ok = builder.CreateAnd(
        builder.CreateICmpULE(pending_after_member_bound,
            builder.CreateZExt(pending_capacity, i64)),
        builder.CreateICmpULE(events_after_member_bound,
            builder.CreateZExt(event_capacity, i64)));
    auto* const activation_site_capacity = llvm::BasicBlock::Create(
        context, "activation.site.capacity", function);
    auto* const activation_capacity_valid = llvm::BasicBlock::Create(
        context, "activation.capacity.valid", function);
    builder.CreateCondBr(member_capacity_ok,
        activation_site_capacity, decline);
    builder.SetInsertPoint(activation_site_capacity);
    llvm::Value* available_write_sites = llvm::ConstantInt::getTrue(context);
    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        const auto belongs_to_activation = emit_equal(builder, activation_index,
            expected_i32(builder, site.member_index));
        const auto creates_site = builder.CreateAnd(contributes_to_prefix,
            belongs_to_activation);
        auto* const state_pointer = builder.CreateInBoundsGEP(i8, slot_states,
            expected_i64(builder, site.pending_slot));
        const auto state = builder.CreateLoad(i8, state_pointer,
            "activation.pending.slot.state");
        const auto free = emit_equal(builder, state,
            llvm::ConstantInt::get(i8, 0U));
        available_write_sites = emit_and(builder, available_write_sites,
            builder.CreateOr(builder.CreateNot(creates_site), free));
    }
    builder.CreateCondBr(available_write_sites,
        activation_capacity_valid, decline);
    builder.SetInsertPoint(activation_capacity_valid);
    builder.CreateStore(pending_after_member_bound, live_pending_count);
    builder.CreateStore(events_after_member_bound, live_event_count);
    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        const auto belongs_to_activation = emit_equal(builder, activation_index,
            expected_i32(builder, site.member_index));
        const auto creates_site = builder.CreateAnd(contributes_to_prefix,
            belongs_to_activation);
        auto* const state_pointer = builder.CreateInBoundsGEP(i8, slot_states,
            expected_i64(builder, site.pending_slot));
        const auto old_state = builder.CreateLoad(i8, state_pointer,
            "activation.old.pending.slot.state");
        const auto new_state = builder.CreateSelect(creates_site,
            llvm::ConstantInt::get(i8, 1U), old_state);
        builder.CreateStore(new_state, state_pointer);
    }
    builder.CreateBr(validate_advance);

    builder.SetInsertPoint(validate_write);
    const auto write_index_wide = task_payload_index_wide(builder,
        validation_task_payload);
    const auto write_in_range = builder.CreateICmpULT(write_index_wide,
        builder.CreateZExt(pending_capacity, i64));
    const auto write_index = builder.CreateTrunc(write_index_wide, i32);
    auto* const validate_write_fields = llvm::BasicBlock::Create(
        context, "validate.write.fields", function);
    builder.CreateCondBr(write_in_range, validate_write_fields, decline);
    builder.SetInsertPoint(validate_write_fields);
    const auto write_flags = write_field(builder, writes_ptr, write_index,
        offsetof(RegionFrontierPendingWriteV1, flags), i32, "write.flags");
    const auto expected_member_index = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.member_index;
        }, UINT32_MAX);
    const auto expected_signal_slot = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.signal_slot;
        }, UINT32_MAX);
    const auto expected_source_instruction = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.source_instruction;
        }, UINT32_MAX);
    const auto expected_update_kind = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.update_kind;
        }, UINT32_MAX);
    const auto expected_event_kind = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.event_kind;
        }, UINT32_MAX);
    const auto expected_width = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.width;
        }, UINT32_MAX);
    const auto expected_word_count = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.word_count;
        }, UINT32_MAX);
    auto* write_ok = emit_equal(builder, write_flags,
        llvm::ConstantInt::get(i32,
            RegionFrontierPendingWriteFlagsV1::pending_active
                | RegionFrontierPendingWriteFlagsV1::pending_value_ready
                | RegionFrontierPendingWriteFlagsV1::pending_key_assigned
                | RegionFrontierPendingWriteFlagsV1::pending_internal_target));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        expected_event_kind,
        expected_i32(builder, static_cast<std::uint32_t>(
                                  RegionFrontierEventKindV1::internal_commit))));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV1, member_index), i32,
            "write.member.index"), expected_member_index));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV1, signal_slot), i32,
            "write.signal.slot"), expected_signal_slot));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV1, source_instruction), i32,
            "write.source.instruction"), expected_source_instruction));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV1, update_kind), i32,
            "write.update.kind"), expected_update_kind));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV1, width), i32,
            "write.width"), expected_width));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV1, word_count), i32,
            "write.word.count"), expected_word_count));
    auto* const slot_state_pointer = builder.CreateInBoundsGEP(i8,
        slot_states, builder.CreateZExt(write_index, i64));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        builder.CreateLoad(i8, slot_state_pointer, "write.slot.state"),
        llvm::ConstantInt::get(i8, 1U)));
    const auto write_key_pointer = constant_offset(builder,
        indexed_pointer(builder, writes_ptr, write_index,
            sizeof(RegionFrontierPendingWriteV1)),
        offsetof(RegionFrontierPendingWriteV1, commit_key));
    write_ok = emit_and(builder, write_ok,
        emit_slot_matches_key(builder, frame, write_key_pointer));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        load_at(builder, write_key_pointer,
            offsetof(RegionFrontierKeyV1, stable_order), i64, "write.stable"),
        task_stable));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        load_at(builder, write_key_pointer,
            offsetof(RegionFrontierKeyV1, sequence), i64, "write.sequence"),
        task_sequence));
    const auto signal_slot = expected_signal_slot;
    write_ok = emit_and(builder, write_ok,
        builder.CreateICmpULT(signal_slot, plane_count));
    builder.CreateCondBr(write_ok, validate_write_plane, decline);

    // Do not form or dereference a plane GEP until the slot range is proven.
    // This keeps malformed caller frames on the read-only decline path.
    builder.SetInsertPoint(validate_write_plane);
    const auto planes_array = builder.CreateBitCast(planes_ptr,
        llvm::PointerType::getUnqual(builder.getContext()));
    const auto plane_pointer = indexed_pointer(builder, planes_array,
        signal_slot, sizeof(RegionFrontierPlaneV1));
    const auto plane_flags = load_at(builder, plane_pointer,
        offsetof(RegionFrontierPlaneV1, flags), i32, "plane.flags");
    write_ok = emit_and(builder, write_ok,
        emit_flag_set(builder, plane_flags,
            RegionFrontierPlaneFlagsV1::certified_internal_single_owner));
    const auto metadata_index = load_at(builder, plane_pointer,
        offsetof(RegionFrontierPlaneV1, metadata_index), i32,
        "plane.metadata.index");
    write_ok = emit_and(builder, write_ok,
        builder.CreateICmpULT(metadata_index, metadata_count));
    const auto width = write_field(builder, writes_ptr, write_index,
        offsetof(RegionFrontierPendingWriteV1, width), i32, "write.width");
    const auto plane_width = load_at(builder, plane_pointer,
        offsetof(RegionFrontierPlaneV1, width), i32, "plane.width");
    write_ok = emit_and(builder, write_ok, emit_equal(builder, width, plane_width));
    const auto write_aval = write_field(builder, writes_ptr, write_index,
        offsetof(RegionFrontierPendingWriteV1, aval), frame_pointer,
        "write.aval");
    const auto write_bval = write_field(builder, writes_ptr, write_index,
        offsetof(RegionFrontierPendingWriteV1, bval), frame_pointer,
        "write.bval");
    write_ok = emit_and(builder, write_ok,
        builder.CreateIsNotNull(write_aval));
    write_ok = emit_and(builder, write_ok,
        builder.CreateIsNotNull(write_bval));
    builder.CreateCondBr(write_ok, validate_write_capacity, decline);

    builder.SetInsertPoint(validate_write_capacity);
    const auto pending_before_commit = builder.CreateLoad(i64,
        live_pending_count);
    const auto pending_nonzero = builder.CreateICmpNE(pending_before_commit,
        expected_i64(builder, 0U));
    const auto pending_commit_delta = builder.CreateSelect(contributes_to_prefix,
        expected_i64(builder, 1U), expected_i64(builder, 0U));
    const auto pending_after_commit_bound = builder.CreateSub(
        pending_before_commit, pending_commit_delta);
    const auto fanout_commit_delta = builder.CreateSelect(contributes_to_prefix,
        expected_i64(builder, max_commit_fanout_events), expected_i64(builder, 0U));
    const auto events_after_commit_bound = builder.CreateAdd(
        builder.CreateLoad(i64, live_event_count),
        fanout_commit_delta);
    const auto committed_after_prefix = builder.CreateAdd(
        builder.CreateLoad(i64, live_committed_count),
        builder.CreateZExt(contributes_to_prefix, i64));
    const auto commit_capacity_ok = builder.CreateAnd(
        builder.CreateOr(builder.CreateNot(contributes_to_prefix),
            pending_nonzero),
        builder.CreateAnd(
            builder.CreateICmpULE(pending_after_commit_bound,
                builder.CreateZExt(pending_capacity, i64)),
            builder.CreateAnd(
                builder.CreateICmpULE(events_after_commit_bound,
                    builder.CreateZExt(event_capacity, i64)),
                builder.CreateICmpULE(committed_after_prefix,
                    builder.CreateZExt(committed_capacity, i64)))));
    auto* const commit_capacity_valid = llvm::BasicBlock::Create(
        context, "commit.capacity.valid", function);
    builder.CreateCondBr(commit_capacity_ok,
        commit_capacity_valid, decline);
    builder.SetInsertPoint(commit_capacity_valid);
    builder.CreateStore(pending_after_commit_bound, live_pending_count);
    builder.CreateStore(events_after_commit_bound, live_event_count);
    builder.CreateStore(committed_after_prefix, live_committed_count);
    const auto slot_state_after_commit = builder.CreateSelect(
        contributes_to_prefix, llvm::ConstantInt::get(i8, 0U),
        builder.CreateLoad(i8, slot_state_pointer,
            "write.slot.state.retained"));
    builder.CreateStore(slot_state_after_commit, slot_state_pointer);
    builder.CreateBr(validate_advance);

    builder.SetInsertPoint(validate_boundary_ack);
    const auto boundary_index_wide = task_payload_index_wide(builder,
        validation_task_payload);
    const auto boundary_in_range = builder.CreateICmpULT(boundary_index_wide,
        builder.CreateZExt(pending_capacity, i64));
    const auto boundary_index = builder.CreateTrunc(boundary_index_wide, i32);
    auto* const validate_boundary_fields = llvm::BasicBlock::Create(
        context, "validate.boundary.fields", function);
    builder.CreateCondBr(boundary_in_range, validate_boundary_fields, decline);
    builder.SetInsertPoint(validate_boundary_fields);
    const auto boundary_flags = write_field(builder, writes_ptr, boundary_index,
        offsetof(RegionFrontierPendingWriteV1, flags), i32, "boundary.flags");
    const auto expected_boundary_member = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.member_index;
        }, UINT32_MAX);
    const auto expected_boundary_signal = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.signal_slot;
        }, UINT32_MAX);
    const auto expected_boundary_instruction = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.source_instruction;
        }, UINT32_MAX);
    const auto expected_boundary_update_kind = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.update_kind;
        }, UINT32_MAX);
    const auto expected_boundary_event = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.event_kind;
        }, UINT32_MAX);
    const auto expected_boundary_width = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.width;
        }, UINT32_MAX);
    const auto expected_boundary_words = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV1& site) {
            return site.word_count;
        }, UINT32_MAX);
    auto* boundary_ok = emit_all_flags_set(builder, boundary_flags,
        RegionFrontierPendingWriteFlagsV1::pending_active
            | RegionFrontierPendingWriteFlagsV1::pending_value_ready
            | RegionFrontierPendingWriteFlagsV1::pending_key_assigned
            | RegionFrontierPendingWriteFlagsV1::pending_boundary_target);
    boundary_ok = emit_and(builder, boundary_ok,
        emit_equal(builder, expected_boundary_event,
            expected_i32(builder, static_cast<std::uint32_t>(
                                      RegionFrontierEventKindV1::boundary_commit))));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV1, member_index), i32,
            "boundary.member.index"), expected_boundary_member));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV1, signal_slot), i32,
            "boundary.signal.slot"), expected_boundary_signal));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV1, source_instruction), i32,
            "boundary.source.instruction"), expected_boundary_instruction));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV1, update_kind), i32,
            "boundary.update.kind"), expected_boundary_update_kind));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV1, width), i32,
            "boundary.width"), expected_boundary_width));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV1, word_count), i32,
            "boundary.word.count"), expected_boundary_words));
    auto* const boundary_state_pointer = builder.CreateInBoundsGEP(i8,
        slot_states, builder.CreateZExt(boundary_index, i64));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        builder.CreateLoad(i8, boundary_state_pointer,
            "boundary.slot.state"), llvm::ConstantInt::get(i8, 1U)));
    constexpr auto boundary_known_flags
        = RegionFrontierPendingWriteFlagsV1::pending_active
        | RegionFrontierPendingWriteFlagsV1::pending_value_ready
        | RegionFrontierPendingWriteFlagsV1::pending_key_assigned
        | RegionFrontierPendingWriteFlagsV1::pending_internal_target
        | RegionFrontierPendingWriteFlagsV1::pending_boundary_target
        | RegionFrontierPendingWriteFlagsV1::pending_committed;
    boundary_ok = emit_and(builder, boundary_ok,
        builder.CreateICmpEQ(builder.CreateAnd(boundary_flags,
                                  expected_i32(builder,
                                      ~static_cast<std::uint32_t>(boundary_known_flags))),
            expected_i32(builder, 0U)));
    boundary_ok = emit_and(builder, boundary_ok,
        builder.CreateICmpEQ(builder.CreateAnd(boundary_flags,
                                  expected_i32(builder,
                                      RegionFrontierPendingWriteFlagsV1::pending_internal_target)),
            expected_i32(builder, 0U)));
    const auto boundary_key_pointer = constant_offset(builder,
        indexed_pointer(builder, writes_ptr, boundary_index,
            sizeof(RegionFrontierPendingWriteV1)),
        offsetof(RegionFrontierPendingWriteV1, commit_key));
    boundary_ok = emit_and(builder, boundary_ok,
        emit_slot_matches_key(builder, frame, boundary_key_pointer));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        load_at(builder, boundary_key_pointer,
            offsetof(RegionFrontierKeyV1, stable_order), i64,
            "boundary.stable"), task_stable));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        load_at(builder, boundary_key_pointer,
            offsetof(RegionFrontierKeyV1, sequence), i64,
            "boundary.sequence"), task_sequence));
    builder.CreateCondBr(boundary_ok, validate_boundary_capacity, decline);

    builder.SetInsertPoint(validate_boundary_capacity);
    const auto pending_before_boundary = builder.CreateLoad(i64,
        live_pending_count);
    const auto pending_nonzero_before_boundary = builder.CreateICmpNE(
        pending_before_boundary, expected_i64(builder, 0U));
    const auto pending_boundary_delta = builder.CreateSelect(contributes_to_prefix,
        expected_i64(builder, 1U), expected_i64(builder, 0U));
    const auto pending_after_boundary = builder.CreateSub(
        pending_before_boundary, pending_boundary_delta);
    auto* const boundary_capacity_valid = llvm::BasicBlock::Create(
        context, "boundary.capacity.valid", function);
    builder.CreateCondBr(builder.CreateOr(
            builder.CreateNot(contributes_to_prefix),
            pending_nonzero_before_boundary),
        boundary_capacity_valid, decline);
    builder.SetInsertPoint(boundary_capacity_valid);
    builder.CreateStore(pending_after_boundary, live_pending_count);
    auto* const boundary_consumed = builder.CreateSelect(contributes_to_prefix,
        llvm::ConstantInt::get(i8, 0U),
        builder.CreateLoad(i8, boundary_state_pointer,
            "boundary.slot.state.retained"));
    builder.CreateStore(boundary_consumed, boundary_state_pointer);
    builder.CreateBr(validate_advance);

    builder.SetInsertPoint(validate_advance);
    builder.CreateStore(task_stable, previous_stable_order);
    builder.CreateStore(task_sequence, previous_sequence);
    builder.CreateStore(llvm::ConstantInt::getTrue(context), has_previous_task);
    builder.CreateStore(builder.CreateAdd(scan_index, expected_i32(builder, 1U)),
        validate_index);
    builder.CreateBr(validate_loop);

    // Ordered event loop. All payloads and identities above are read-only
    // checked before this block can clear a ready bit or publish a plane.
    builder.SetInsertPoint(dispatch_loop);
    const auto stop_pointer = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1, stop_requested), "stop.requested.ptr");
    auto* const check_stop = llvm::BasicBlock::Create(
        context, "check.stop", function);
    builder.CreateBr(check_stop);
    builder.SetInsertPoint(check_stop);
    const auto current_cursor = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_task_cursor), i32,
        "current.task.cursor");
    auto* const test_stop_value = llvm::BasicBlock::Create(
        context, "test.stop.value", function);
    builder.CreateCondBr(builder.CreateIsNotNull(stop_pointer),
        test_stop_value, dispatch_task);
    builder.SetInsertPoint(test_stop_value);
    const auto stop_value = builder.CreateLoad(i32,
        builder.CreateBitCast(stop_pointer,
            llvm::PointerType::getUnqual(builder.getContext())),
        "stop.value");
    builder.CreateCondBr(builder.CreateICmpNE(stop_value, expected_i32(builder, 0U)),
        stopped, dispatch_task);

    builder.SetInsertPoint(dispatch_task);
    auto* const have_task = llvm::BasicBlock::Create(
        context, "have.task", function);
    builder.CreateCondBr(builder.CreateICmpULT(current_cursor, task_count),
        have_task, at_end);
    builder.SetInsertPoint(at_end);
    const auto staged_remaining = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, staged_event_count), i32,
        "staged.remaining");
    builder.CreateCondBr(builder.CreateICmpNE(staged_remaining,
            expected_i32(builder, 0U)), need_keys, quiescent);
    builder.SetInsertPoint(quiescent);
    return_status(builder, RegionFrontierStatusV1::quiescent);

    builder.SetInsertPoint(have_task);
    const auto task_payload = load_task_field(builder, tasks_ptr, current_cursor,
        offsetof(RegionFrontierSchedulerTaskV1, payload), "task.payload");
    const auto task_kind = task_payload_kind(builder, task_payload);
    const auto event_index = task_payload_index(builder, task_payload);
    const auto current_stable = load_task_field(builder, tasks_ptr, current_cursor,
        offsetof(RegionFrontierSchedulerTaskV1, stable_order), "current.stable");
    const auto current_sequence = load_task_field(builder, tasks_ptr, current_cursor,
        offsetof(RegionFrontierSchedulerTaskV1, sequence), "current.sequence");
    builder.CreateCondBr(same_slot_cut, cut_check, dispatch_ready);

    builder.SetInsertPoint(cut_check);
    const auto cut_stable = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, cut)
            + offsetof(RegionFrontierCutV1, next_key)
            + offsetof(RegionFrontierKeyV1, stable_order), i64, "cut.stable");
    const auto cut_sequence = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, cut)
            + offsetof(RegionFrontierCutV1, next_key)
            + offsetof(RegionFrontierKeyV1, sequence), i64, "cut.sequence");
    const auto before_cut = emit_key_before(builder, current_stable,
        current_sequence, cut_stable, cut_sequence);
    builder.CreateCondBr(before_cut, dispatch_ready, cut_yield);

    builder.SetInsertPoint(cut_yield);
    const auto events_before_cut = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, staged_event_count), i32,
        "events.before.cut");
    auto* const cut_status_block = llvm::BasicBlock::Create(
        context, "cut.status", function);
    builder.CreateCondBr(builder.CreateICmpNE(events_before_cut,
            expected_i32(builder, 0U)), need_keys, cut_status_block);
    builder.SetInsertPoint(cut_status_block);
    return_status(builder, RegionFrontierStatusV1::cut_before_key);

    builder.SetInsertPoint(dispatch_ready);
    auto* const event_switch = builder.CreateSwitch(task_kind,
        dispatch_invalid, 3U);
    event_switch->addCase(llvm::ConstantInt::get(i8,
            static_cast<std::uint8_t>(RegionFrontierEventKindV1::member_activation)),
        dispatch_activation);
    event_switch->addCase(llvm::ConstantInt::get(i8,
            static_cast<std::uint8_t>(RegionFrontierEventKindV1::internal_commit)),
        dispatch_internal);
    event_switch->addCase(llvm::ConstantInt::get(i8,
            static_cast<std::uint8_t>(RegionFrontierEventKindV1::boundary_commit)),
        dispatch_boundary);

    builder.SetInsertPoint(capacity_yield);
    const auto events_at_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, staged_event_count), i32,
        "events.at.capacity");
    auto* const capacity_status = llvm::BasicBlock::Create(
        context, "capacity.status", function);
    builder.CreateCondBr(builder.CreateICmpNE(events_at_capacity,
            expected_i32(builder, 0U)), need_keys, capacity_status);
    builder.SetInsertPoint(capacity_status);
    return_status(builder, RegionFrontierStatusV1::yield_before_task);

    builder.SetInsertPoint(dispatch_activation);
    const auto pending_now = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, pending_write_count), i32,
        "pending.now");
    const auto pending_room = builder.CreateSub(pending_capacity, pending_now);
    llvm::Value* runtime_member_write_bound = expected_i32(builder, 0U);
    llvm::Value* runtime_member_event_bound = expected_i32(builder, 0U);
    for (std::size_t index = member_count; index > 0U; --index) {
        const auto member_index = static_cast<std::uint32_t>(index - 1U);
        const auto matches_member = emit_equal(builder, event_index,
            expected_i32(builder, member_index));
        runtime_member_write_bound = builder.CreateSelect(matches_member,
            expected_i32(builder, max_member_write_counts[index - 1U]),
            runtime_member_write_bound);
        runtime_member_event_bound = builder.CreateSelect(matches_member,
            expected_i32(builder,
                max_member_staged_event_counts[index - 1U]),
            runtime_member_event_bound);
    }
    const auto pending_bound_ok = builder.CreateICmpUGE(pending_room,
        runtime_member_write_bound);
    const auto events_now = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, staged_event_count), i32,
        "events.now");
    const auto events_room = builder.CreateSub(event_capacity, events_now);
    const auto event_bound_ok = builder.CreateICmpUGE(events_room,
        runtime_member_event_bound);
    builder.CreateCondBr(builder.CreateAnd(pending_bound_ok, event_bound_ok),
        capacity_ok, capacity_yield);

    builder.SetInsertPoint(capacity_ok);
    const auto member_switch = builder.CreateSwitch(event_index,
        dispatch_invalid, static_cast<unsigned>(member_count));
    std::vector<llvm::BasicBlock*> member_blocks;
    member_blocks.reserve(member_count);
    for (std::size_t index = 0U; index < member_count; ++index) {
        auto* const block = llvm::BasicBlock::Create(context,
            "member.body." + std::to_string(index), function);
        member_blocks.push_back(block);
        member_switch->addCase(llvm::ConstantInt::get(i32, index), block);
    }
    for (std::size_t index = 0U; index < member_count; ++index) {
        builder.SetInsertPoint(member_blocks[index]);
        auto* const member_pointer = indexed_pointer(builder, members_ptr,
            event_index, sizeof(RegionFrontierMemberV1));
        const auto member_flags = member_field(builder, members_ptr, event_index,
            offsetof(RegionFrontierMemberV1, flags), i32, "member.flags");
        const auto has_pending_origin = emit_flag_set(builder, member_flags,
            RegionFrontierMemberFlagsV1::pending_activation);
        const auto consumed_queued_key_pointer = constant_offset(builder,
            member_pointer, offsetof(RegionFrontierMemberV1, queued_key));
        const auto pending_origin_pointer = constant_offset(builder,
            member_pointer,
            offsetof(RegionFrontierMemberV1, pending_activation_origin));
        const auto activation_origin_pointer = constant_offset(builder,
            member_pointer, offsetof(RegionFrontierMemberV1, activation_origin));
        constexpr std::size_t u64_key_fields[] {
            offsetof(RegionFrontierKeyV1, time),
            offsetof(RegionFrontierKeyV1, delta),
            offsetof(RegionFrontierKeyV1, systemverilog_round),
            offsetof(RegionFrontierKeyV1, stable_order),
            offsetof(RegionFrontierKeyV1, sequence),
        };
        for (const auto field : u64_key_fields) {
            const auto queued_value = load_at(builder,
                consumed_queued_key_pointer,
                field, i64, "queued.origin.field");
            const auto pending_value = load_at(builder, pending_origin_pointer,
                field, i64, "pending.origin.field");
            store_at(builder, activation_origin_pointer, field,
                builder.CreateSelect(has_pending_origin,
                    pending_value, queued_value, "activation.origin.field"));
        }
        constexpr std::size_t u32_key_fields[] {
            offsetof(RegionFrontierKeyV1, process_domain),
            offsetof(RegionFrontierKeyV1, phase),
        };
        for (const auto field : u32_key_fields) {
            const auto queued_value = load_at(builder,
                consumed_queued_key_pointer,
                field, i32, "queued.origin.field");
            const auto pending_value = load_at(builder, pending_origin_pointer,
                field, i32, "pending.origin.field");
            store_at(builder, activation_origin_pointer, field,
                builder.CreateSelect(has_pending_origin,
                    pending_value, queued_value, "activation.origin.field"));
        }
        store_at(builder, member_pointer,
            offsetof(RegionFrontierMemberV1, static_trigger_mask),
            expected_i64(builder, 0U));
        const auto word_index = builder.CreateUDiv(event_index,
            expected_i32(builder, 64U));
        const auto bit_index = builder.CreateURem(event_index,
            expected_i32(builder, 64U));
        const auto words = builder.CreateBitCast(ready_words_ptr,
            llvm::PointerType::getUnqual(builder.getContext()));
        const auto word_pointer = builder.CreateInBoundsGEP(i64, words,
            builder.CreateZExt(word_index, i64));
        const auto word = builder.CreateLoad(i64, word_pointer,
            "selected.ready.word");
        const auto mask = builder.CreateShl(expected_i64(builder, 1U),
            builder.CreateZExt(bit_index, i64));
        builder.CreateStore(builder.CreateAnd(word, builder.CreateNot(mask)),
            word_pointer);
        auto new_flags = builder.CreateAnd(member_flags,
            expected_i32(builder, ~static_cast<std::uint32_t>(
                RegionFrontierMemberFlagsV1::queued
                    | RegionFrontierMemberFlagsV1::queued_key_valid
                    | RegionFrontierMemberFlagsV1::pending_activation
                    | RegionFrontierMemberFlagsV1::waiting_on_static)));
        new_flags = builder.CreateOr(new_flags,
            expected_i32(builder, RegionFrontierMemberFlagsV1::executing));
        store_array_field(builder, members_ptr, event_index,
            sizeof(RegionFrontierMemberV1),
            offsetof(RegionFrontierMemberV1, flags), new_flags);
        store_frame(builder, frame,
            offsetof(RegionFrontierFrameV1, current_member), event_index);
        store_frame(builder, frame,
            offsetof(RegionFrontierFrameV1, saved_body_pc),
            expected_i32(builder, static_cast<std::uint32_t>(index)));
        emit_member(builder, index, frame);
        assert(builder.GetInsertBlock()->getTerminator() == nullptr);
        builder.CreateBr(member_done);
    }

    builder.SetInsertPoint(member_done);
    const auto completed_member = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, current_member), i32,
        "completed.member");
    const auto completed_flags = member_field(builder, members_ptr,
        completed_member, offsetof(RegionFrontierMemberV1, flags), i32,
        "completed.flags");
    const auto nonexecuting_flags = builder.CreateAnd(completed_flags,
        expected_i32(builder,
            ~static_cast<std::uint32_t>(RegionFrontierMemberFlagsV1::executing)));
    const auto waiting_flags = builder.CreateOr(nonexecuting_flags,
        expected_i32(builder, RegionFrontierMemberFlagsV1::waiting_on_static));
    store_array_field(builder, members_ptr, completed_member,
        sizeof(RegionFrontierMemberV1),
        offsetof(RegionFrontierMemberV1, flags), waiting_flags);
    store_frame(builder, frame, offsetof(RegionFrontierFrameV1, current_member),
        expected_i32(builder, UINT32_MAX));
    const auto profile_pointer = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV1,
            native_frontier_member_dispatches), "dispatch.counter.ptr");
    auto* const increment_profile = llvm::BasicBlock::Create(
        context, "increment.profile", function);
    auto* const no_profile = llvm::BasicBlock::Create(
        context, "no.profile", function);
    auto* const after_profile = llvm::BasicBlock::Create(
        context, "after.profile", function);
    builder.CreateCondBr(builder.CreateIsNotNull(profile_pointer),
        increment_profile, no_profile);
    builder.SetInsertPoint(increment_profile);
    auto* const profile_counter = builder.CreateBitCast(profile_pointer,
        llvm::PointerType::getUnqual(builder.getContext()));
    const auto profile_value = builder.CreateLoad(i64, profile_counter,
        "dispatch.counter");
    builder.CreateStore(builder.CreateAdd(profile_value,
        expected_i64(builder, 1U)), profile_counter);
    builder.CreateBr(after_profile);
    builder.SetInsertPoint(no_profile);
    builder.CreateBr(after_profile);
    builder.SetInsertPoint(after_profile);
    const auto next_member_cursor = builder.CreateAdd(current_cursor,
        expected_i32(builder, 1U));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_task_cursor),
        next_member_cursor);
    builder.CreateBr(dispatch_loop);

    builder.SetInsertPoint(dispatch_internal);
    const auto commit_events_now = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, staged_event_count), i32,
        "commit.events.now");
    const auto commit_event_room = builder.CreateSub(event_capacity,
        commit_events_now);
    builder.CreateCondBr(builder.CreateICmpUGE(commit_event_room,
            expected_i32(builder, max_commit_fanout_events)),
        internal_done, capacity_yield);
    builder.SetInsertPoint(internal_done);
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, current_pending_write), event_index);
    emit_internal_commit(builder, event_index, frame);
    assert(builder.GetInsertBlock()->getTerminator() == nullptr);
    const auto commit_flags = write_field(builder, writes_ptr, event_index,
        offsetof(RegionFrontierPendingWriteV1, flags), i32,
        "committed.write.flags");
    const auto inactive_flags = builder.CreateAnd(commit_flags,
        expected_i32(builder,
            ~static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV1::pending_active)));
    const auto retired_flags = builder.CreateOr(inactive_flags,
        expected_i32(builder,
            RegionFrontierPendingWriteFlagsV1::pending_committed));
    store_array_field(builder, writes_ptr, event_index,
        sizeof(RegionFrontierPendingWriteV1),
        offsetof(RegionFrontierPendingWriteV1, flags), retired_flags);
    const auto pending_after_commit = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, pending_write_count), i32,
        "pending.after.commit");
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, pending_write_count),
        builder.CreateSub(pending_after_commit, expected_i32(builder, 1U)));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, current_pending_write),
        expected_i32(builder, UINT32_MAX));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_task_cursor),
        builder.CreateAdd(current_cursor, expected_i32(builder, 1U)));
    builder.CreateBr(dispatch_loop);

    builder.SetInsertPoint(dispatch_boundary);
    const auto boundary_flags_now = write_field(builder, writes_ptr, event_index,
        offsetof(RegionFrontierPendingWriteV1, flags), i32,
        "boundary.flags.now");
    builder.CreateCondBr(emit_flag_set(builder, boundary_flags_now,
            RegionFrontierPendingWriteFlagsV1::pending_committed),
        boundary_ack, boundary_publication);
    builder.SetInsertPoint(boundary_publication);
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, current_pending_write), event_index);
    return_status(builder, RegionFrontierStatusV1::boundary_publication);

    builder.SetInsertPoint(boundary_ack);
    const auto boundary_retired_flags = builder.CreateAnd(boundary_flags_now,
        expected_i32(builder,
            ~static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV1::pending_active)));
    store_array_field(builder, writes_ptr, event_index,
        sizeof(RegionFrontierPendingWriteV1),
        offsetof(RegionFrontierPendingWriteV1, flags), boundary_retired_flags);
    const auto pending_after_boundary_dispatch = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, pending_write_count), i32,
        "pending.after.boundary");
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, pending_write_count),
        builder.CreateSub(pending_after_boundary_dispatch,
            expected_i32(builder, 1U)));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, current_pending_write),
        expected_i32(builder, UINT32_MAX));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV1, scheduler_task_cursor),
        builder.CreateAdd(current_cursor, expected_i32(builder, 1U)));
    builder.CreateBr(dispatch_loop);

    builder.SetInsertPoint(dispatch_invalid);
    return_status(builder, RegionFrontierStatusV1::decline_before_mutation);
    builder.SetInsertPoint(need_keys);
    return_status(builder, RegionFrontierStatusV1::need_scheduler_keys);
    builder.SetInsertPoint(stopped);
    return_status(builder, RegionFrontierStatusV1::stopped);
    builder.SetInsertPoint(decline);
    return_status(builder, RegionFrontierStatusV1::decline_before_mutation);

    // The C++ entry typedef is noexcept. Reject generated host/possibly
    // unwinding calls before marking the function nounwind in LLVM IR.
    for (auto& block : *function) {
        for (auto& instruction : block) {
            if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
                if (!call->doesNotThrow()) {
                    function->eraseFromParent();
                    return nullptr;
                }
            }
            if (llvm::isa<llvm::InvokeInst>(instruction)) {
                function->eraseFromParent();
                return nullptr;
            }
        }
    }
    function->addFnAttr(llvm::Attribute::NoUnwind);
    return function;
}

} // namespace fsim::runtime::simir::scratch
