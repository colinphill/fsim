// SPDX-License-Identifier: Apache-2.0
// Scratch-only LLVM emitter for a scheduler-authenticated SV event prefix.
#include "region_frontier_codegen_v2.hpp"

#include "logic9_word_lowering.hpp"
#include "region_frontier_initial_slot_validation.hpp"

#include <llvm/ADT/Twine.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
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

static constexpr std::size_t pointer_array_element_offset(
    const std::size_t array_offset, const std::uint32_t plane_index)
{
    return array_offset + sizeof(std::uint64_t*) * plane_index;
}

static llvm::Value* value_role_pointer(llvm::IRBuilder<>& builder,
    llvm::Value* plane, const std::size_t role_offset,
    const std::uint32_t value_plane, const llvm::Twine& name)
{
    return load_at(builder, plane,
        pointer_array_element_offset(role_offset, value_plane),
        llvm::PointerType::getUnqual(builder.getContext()), name);
}

static llvm::Value* value_role_pointer(llvm::IRBuilder<>& builder,
    llvm::Value* plane, llvm::Value* role_offset,
    const std::uint32_t value_plane, const llvm::Twine& name)
{
    auto* const role_base = byte_pointer(builder, plane, role_offset);
    return load_at(builder, role_base,
        pointer_array_element_offset(0U, value_plane),
        llvm::PointerType::getUnqual(builder.getContext()), name);
}

static llvm::Value* pending_value_pointer(llvm::IRBuilder<>& builder,
    llvm::Value* write, const std::uint32_t value_plane,
    const llvm::Twine& name)
{
    return load_at(builder, write,
        pointer_array_element_offset(
            offsetof(RegionFrontierPendingWriteV2, value_planes),
            value_plane), llvm::PointerType::getUnqual(builder.getContext()),
        name);
}

static llvm::Value* emit_equal(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right);

static llvm::Value* emit_and(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right);

static llvm::MDNode* create_unroll_disabled_loop_id(
    llvm::LLVMContext& context);

static llvm::Value* value_buffers_are_canonical(llvm::IRBuilder<>& builder,
    const std::array<llvm::Value*, 4U>& planes, llvm::Value* width,
    llvm::Value* word_count, llvm::Value* kind, llvm::Value* plane_count)
{
    auto& context = builder.getContext();
    auto* const i1 = llvm::Type::getInt1Ty(context);
    auto* const i32 = i32_type(builder);
    auto* const i64 = i64_type(builder);
    auto* const function = builder.GetInsertBlock()->getParent();
    const auto logic4_kind = llvm::ConstantInt::get(i32,
        static_cast<std::uint32_t>(RegionFrontierValueKindV2::logic4));
    const auto logic9_kind = llvm::ConstantInt::get(i32,
        static_cast<std::uint32_t>(RegionFrontierValueKindV2::logic9));
    const auto is_logic4 = builder.CreateICmpEQ(kind, logic4_kind);
    const auto is_logic9 = builder.CreateICmpEQ(kind, logic9_kind);
    const auto kind_is_known = builder.CreateOr(is_logic4, is_logic9);
    const auto required_plane_count = builder.CreateSelect(is_logic4,
        llvm::ConstantInt::get(i32, kRegionFrontierLogic4PlaneCountV2),
        llvm::ConstantInt::get(i32, kRegionFrontierLogic9PlaneCountV2));
    const auto width_is_nonzero = builder.CreateICmpNE(width,
        llvm::ConstantInt::get(i32, 0U));
    const auto width64 = builder.CreateZExt(width, i64);
    const auto expected_word_count = builder.CreateUDiv(
        builder.CreateAdd(width64, llvm::ConstantInt::get(i64, 63U)),
        llvm::ConstantInt::get(i64, 64U));
    const auto word_count_matches = builder.CreateICmpEQ(
        builder.CreateZExt(word_count, i64), expected_word_count);
    const auto plane_count_matches = builder.CreateICmpEQ(plane_count,
        required_plane_count);
    auto* const shape_is_valid = builder.CreateAnd(kind_is_known,
        builder.CreateAnd(width_is_nonzero,
            builder.CreateAnd(word_count_matches, plane_count_matches)));

    auto* const kind_dispatch = llvm::BasicBlock::Create(context,
        "canonical.kind.dispatch", function);
    auto* const logic4_check = llvm::BasicBlock::Create(context,
        "canonical.logic4.check", function);
    auto* const logic4_tail = llvm::BasicBlock::Create(context,
        "canonical.logic4.tail", function);
    auto* const logic4_aligned = llvm::BasicBlock::Create(context,
        "canonical.logic4.aligned", function);
    auto* const logic9_check = llvm::BasicBlock::Create(context,
        "canonical.logic9.check", function);
    auto* const logic9_word_header = llvm::BasicBlock::Create(context,
        "canonical.logic9.word.header", function);
    auto* const logic9_word_body = llvm::BasicBlock::Create(context,
        "canonical.logic9.word.body", function);
    auto* const logic9_word_advance = llvm::BasicBlock::Create(context,
        "canonical.logic9.word.advance", function);
    auto* const logic9_word_done = llvm::BasicBlock::Create(context,
        "canonical.logic9.word.done", function);
    auto* const invalid_shape = llvm::BasicBlock::Create(context,
        "canonical.invalid.shape", function);
    auto* const result_join = llvm::BasicBlock::Create(context,
        "canonical.result", function);
    builder.CreateCondBr(shape_is_valid, kind_dispatch, invalid_shape);

    builder.SetInsertPoint(kind_dispatch);
    builder.CreateCondBr(is_logic4, logic4_check, logic9_check);

    builder.SetInsertPoint(logic4_check);
    const auto logic4_remainder = builder.CreateURem(width,
        llvm::ConstantInt::get(i32, 64U));
    builder.CreateCondBr(builder.CreateICmpEQ(logic4_remainder,
                               llvm::ConstantInt::get(i32, 0U)),
        logic4_aligned, logic4_tail);

    builder.SetInsertPoint(logic4_aligned);
    builder.CreateBr(result_join);

    builder.SetInsertPoint(logic4_tail);
    const auto logic4_shift = builder.CreateZExt(logic4_remainder, i64);
    const auto logic4_low_mask = builder.CreateSub(
        builder.CreateShl(llvm::ConstantInt::get(i64, 1U), logic4_shift),
        llvm::ConstantInt::get(i64, 1U));
    const auto logic4_high_mask = builder.CreateNot(logic4_low_mask);
    const auto logic4_last_word_index = builder.CreateSub(word_count,
        llvm::ConstantInt::get(i32, 1U));
    llvm::Value* logic4_tail_valid = llvm::ConstantInt::getTrue(context);
    for (std::uint32_t plane = 0U;
         plane < kRegionFrontierLogic4PlaneCountV2; ++plane) {
        const auto last_word = load_array_field(builder, planes[plane],
            logic4_last_word_index, sizeof(std::uint64_t), 0U, i64,
            "canonical.logic4.tail.word");
        const auto outside_width = builder.CreateAnd(last_word,
            logic4_high_mask);
        logic4_tail_valid = emit_and(builder, logic4_tail_valid,
            builder.CreateICmpEQ(outside_width,
                llvm::ConstantInt::get(i64, 0U)));
    }
    builder.CreateBr(result_join);

    builder.SetInsertPoint(logic9_check);
    const auto logic9_remainder = builder.CreateURem(width,
        llvm::ConstantInt::get(i32, 64U));
    const auto logic9_is_word_aligned = builder.CreateICmpEQ(
        logic9_remainder, llvm::ConstantInt::get(i32, 0U));
    const auto logic9_safe_remainder = builder.CreateSelect(
        logic9_is_word_aligned, llvm::ConstantInt::get(i32, 1U),
        logic9_remainder);
    const auto logic9_shift = builder.CreateZExt(logic9_safe_remainder, i64);
    const auto logic9_low_mask = builder.CreateSub(
        builder.CreateShl(llvm::ConstantInt::get(i64, 1U), logic9_shift),
        llvm::ConstantInt::get(i64, 1U));
    const auto computed_logic9_high_mask = builder.CreateNot(
        logic9_low_mask);
    const auto logic9_high_mask = builder.CreateSelect(
        logic9_is_word_aligned, llvm::ConstantInt::get(i64, 0U),
        computed_logic9_high_mask);
    const auto logic9_last_word_index = builder.CreateSub(word_count,
        llvm::ConstantInt::get(i32, 1U));
    builder.CreateBr(logic9_word_header);

    builder.SetInsertPoint(logic9_word_header);
    auto* const logic9_word_index = builder.CreatePHI(i32, 2U,
        "canonical.logic9.word.index");
    auto* const logic9_words_valid = builder.CreatePHI(i1, 2U,
        "canonical.logic9.words.valid");
    logic9_word_index->addIncoming(llvm::ConstantInt::get(i32, 0U),
        logic9_check);
    logic9_words_valid->addIncoming(llvm::ConstantInt::getTrue(context),
        logic9_check);
    builder.CreateCondBr(builder.CreateICmpULT(logic9_word_index,
                                     word_count),
        logic9_word_body, logic9_word_done);

    builder.SetInsertPoint(logic9_word_body);
    std::array<llvm::Value*, kRegionFrontierLogic9PlaneCountV2> logic9_words { };
    for (std::uint32_t plane = 0U;
         plane < kRegionFrontierLogic9PlaneCountV2; ++plane) {
        logic9_words[plane] = load_array_field(builder, planes[plane],
            logic9_word_index, sizeof(std::uint64_t), 0U, i64,
            "canonical.logic9.word");
    }
    const auto lower_planes = builder.CreateOr(logic9_words[0U],
        builder.CreateOr(logic9_words[1U], logic9_words[2U]));
    const auto reserved_bits = builder.CreateAnd(logic9_words[3U],
        lower_planes);
    const auto reserved_bits_absent = builder.CreateICmpEQ(reserved_bits,
        llvm::ConstantInt::get(i64, 0U));
    auto* const all_planes = builder.CreateOr(lower_planes, logic9_words[3U]);
    const auto outside_width = builder.CreateAnd(all_planes,
        logic9_high_mask);
    const auto last_word = builder.CreateICmpEQ(logic9_word_index,
        logic9_last_word_index);
    const auto tail_bits_absent = builder.CreateOr(builder.CreateNot(last_word),
        builder.CreateICmpEQ(outside_width,
            llvm::ConstantInt::get(i64, 0U)));
    const auto logic9_word_valid = builder.CreateAnd(reserved_bits_absent,
        tail_bits_absent);
    const auto next_logic9_words_valid = builder.CreateAnd(
        logic9_words_valid, logic9_word_valid);
    builder.CreateBr(logic9_word_advance);

    builder.SetInsertPoint(logic9_word_advance);
    const auto next_logic9_word_index = builder.CreateAdd(logic9_word_index,
        llvm::ConstantInt::get(i32, 1U));
    builder.CreateBr(logic9_word_header);
    logic9_word_index->addIncoming(next_logic9_word_index,
        logic9_word_advance);
    logic9_words_valid->addIncoming(next_logic9_words_valid,
        logic9_word_advance);
    auto* const logic9_word_latch = logic9_word_advance->getTerminator();
    logic9_word_latch->setMetadata(llvm::LLVMContext::MD_loop,
        create_unroll_disabled_loop_id(context));

    builder.SetInsertPoint(logic9_word_done);
    builder.CreateBr(result_join);

    builder.SetInsertPoint(invalid_shape);
    builder.CreateBr(result_join);

    builder.SetInsertPoint(result_join);
    auto* const result = builder.CreatePHI(i1, 4U,
        "canonical.value.valid");
    result->addIncoming(llvm::ConstantInt::getTrue(context), logic4_aligned);
    result->addIncoming(logic4_tail_valid, logic4_tail);
    result->addIncoming(logic9_words_valid, logic9_word_done);
    result->addIncoming(llvm::ConstantInt::getFalse(context), invalid_shape);
    return result;
}

static llvm::Value* load_task_field(llvm::IRBuilder<>& builder,
    llvm::Value* tasks, llvm::Value* task_index,
    const std::size_t field_offset, const llvm::Twine& name)
{
    return load_array_field(builder, tasks, task_index,
        sizeof(RegionFrontierSchedulerTaskV2), field_offset,
        i64_type(builder), name);
}

static llvm::Value* member_field(llvm::IRBuilder<>& builder,
    llvm::Value* members, llvm::Value* member_index,
    const std::size_t field_offset, llvm::Type* type,
    const llvm::Twine& name)
{
    return load_array_field(builder, members, member_index,
        sizeof(RegionFrontierMemberV2), field_offset, type, name);
}

static llvm::Value* write_field(llvm::IRBuilder<>& builder,
    llvm::Value* writes, llvm::Value* write_index,
    const std::size_t field_offset, llvm::Type* type,
    const llvm::Twine& name)
{
    return load_array_field(builder, writes, write_index,
        sizeof(RegionFrontierPendingWriteV2), field_offset, type, name);
}

static llvm::Value* emit_equal(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right);

static llvm::Value* emit_and(llvm::IRBuilder<>& builder,
    llvm::Value* left, llvm::Value* right);

static llvm::Value* emit_all_flags_set(llvm::IRBuilder<>& builder,
    llvm::Value* flags, const std::uint32_t mask);

template<typename Field>
static llvm::Value* select_write_site_field(llvm::IRBuilder<>& builder,
    llvm::Value* pending_slot, const RegionFrontierLayoutV2& layout,
    Field&& field, const std::uint32_t invalid_value)
{
    auto* const i32 = i32_type(builder);
    auto* const invalid = llvm::ConstantInt::get(i32, invalid_value);
    const auto site_count = static_cast<std::size_t>(layout.write_site_count);
    if (site_count == 0U) {
        return invalid;
    }

    std::vector<llvm::Constant*> field_values;
    if (site_count > std::numeric_limits<std::size_t>::max()
            / sizeof(llvm::Constant*)
        || site_count > field_values.max_size()) {
        throw std::invalid_argument {
            "region frontier write-site field table is too large"
        };
    }
    field_values.reserve(site_count);
    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        field_values.push_back(llvm::ConstantInt::get(i32,
            static_cast<std::uint32_t>(field(layout.write_sites[site_index]))));
    }

    auto* const table_type = llvm::ArrayType::get(i32, site_count);
    auto* const table_value = llvm::ConstantArray::get(table_type,
        field_values);
    auto* const function = builder.GetInsertBlock()->getParent();
    auto* const table = new llvm::GlobalVariable(*function->getParent(),
        table_type, true, llvm::GlobalValue::PrivateLinkage, table_value,
        "region.frontier.write.site.field");

    auto* const site_in_range = builder.CreateICmpULT(pending_slot,
        llvm::ConstantInt::get(i32, layout.write_site_count),
        "write.site.field.index.in.range");
    // Clamp before the inbounds table load; the final select preserves the
    // caller's invalid value for every out-of-range pending slot.
    auto* const safe_site_index = builder.CreateSelect(site_in_range,
        pending_slot, llvm::ConstantInt::get(i32, 0U),
        "write.site.field.clamped.index");
    const std::array<llvm::Value*, 2U> table_indices {
        llvm::ConstantInt::get(i64_type(builder), 0U),
        builder.CreateZExt(safe_site_index, i64_type(builder)),
    };
    auto* const field_pointer = builder.CreateInBoundsGEP(table_type, table,
        table_indices, "write.site.field.table.address");
    auto* const selected_field = builder.CreateLoad(i32, field_pointer,
        "write.site.field.table.value");
    return builder.CreateSelect(site_in_range, selected_field, invalid,
        "write.site.field.result");
}

static llvm::Value* select_member_table_field(llvm::IRBuilder<>& builder,
    llvm::Value* member_index, const std::span<const std::uint32_t> values,
    const std::uint32_t invalid_value)
{
    auto* const i32 = i32_type(builder);
    if (values.empty()) {
        return llvm::ConstantInt::get(i32, invalid_value);
    }
    std::vector<llvm::Constant*> fields;
    fields.reserve(values.size());
    for (const auto value : values) {
        fields.push_back(llvm::ConstantInt::get(i32, value));
    }
    auto* const table_type = llvm::ArrayType::get(i32, values.size());
    auto* const table = new llvm::GlobalVariable(
        *builder.GetInsertBlock()->getModule(), table_type, true,
        llvm::GlobalValue::PrivateLinkage,
        llvm::ConstantArray::get(table_type, fields),
        "region.frontier.member.field");
    const auto in_range = builder.CreateICmpULT(member_index,
        llvm::ConstantInt::get(i32, values.size()),
        "member.field.index.in.range");
    const auto safe_index = builder.CreateSelect(in_range, member_index,
        llvm::ConstantInt::get(i32, 0U), "member.field.clamped.index");
    const std::array<llvm::Value*, 2U> indices {
        llvm::ConstantInt::get(i64_type(builder), 0U),
        builder.CreateZExt(safe_index, i64_type(builder)),
    };
    auto* const address = builder.CreateInBoundsGEP(table_type, table,
        indices, "member.field.address");
    const auto selected = builder.CreateLoad(i32, address,
        "member.field.value");
    return builder.CreateSelect(in_range, selected,
        llvm::ConstantInt::get(i32, invalid_value), "member.field.result");
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

static llvm::MDNode* create_unroll_disabled_loop_id(
    llvm::LLVMContext& context)
{
    auto* const disable_unroll = llvm::MDNode::get(context,
        llvm::MDString::get(context, "llvm.loop.unroll.disable"));
    auto* const loop_id = llvm::MDNode::getDistinct(context,
        { static_cast<llvm::Metadata*>(nullptr), disable_unroll });
    loop_id->replaceOperandWith(0U, loop_id);
    return loop_id;
}

llvm::StructType* region_frontier_physical_binding_type_v2(
    llvm::LLVMContext& context,
    const std::size_t member_count,
    const std::size_t signal_slot_count)
{
    constexpr auto max_count = static_cast<std::size_t>(
        std::numeric_limits<std::uint32_t>::max());
    if (member_count == 0U || signal_slot_count == 0U
        || member_count > max_count || signal_slot_count > max_count) {
        throw std::invalid_argument {
            "invalid region frontier physical binding shape"
        };
    }
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    return llvm::StructType::get(context, {
        i64,
        i64,
        llvm::ArrayType::get(i32, member_count),
        llvm::ArrayType::get(i32, signal_slot_count),
        llvm::ArrayType::get(i32, signal_slot_count),
    });
}

namespace {

static llvm::Value* load_physical_binding_scalar_v2(
    llvm::IRBuilder<>& builder,
    llvm::StructType* binding_type,
    llvm::Value* physical_binding,
    const unsigned field,
    llvm::Type* value_type,
    const llvm::Twine& name)
{
    if (binding_type == nullptr || physical_binding == nullptr
        || field >= binding_type->getNumElements()
        || binding_type->getElementType(field) != value_type) {
        throw std::invalid_argument {
            "invalid region frontier physical binding field"
        };
    }
    auto* const field_pointer = builder.CreateStructGEP(binding_type,
        physical_binding, field, name + ".field");
    return builder.CreateLoad(value_type, field_pointer, name);
}

static llvm::Value* load_physical_binding_array_element_v2(
    llvm::IRBuilder<>& builder,
    llvm::StructType* binding_type,
    llvm::Value* physical_binding,
    const unsigned field,
    llvm::Value* index,
    const llvm::Twine& name)
{
    if (binding_type == nullptr || physical_binding == nullptr || index == nullptr
        || field >= binding_type->getNumElements()
        || !index->getType()->isIntegerTy(32U)) {
        throw std::invalid_argument {
            "invalid region frontier physical binding array"
        };
    }
    auto* const array_type = llvm::dyn_cast<llvm::ArrayType>(
        binding_type->getElementType(field));
    if (array_type == nullptr
        || array_type->getNumElements() == 0U
        || !array_type->getElementType()->isIntegerTy(32U)) {
        throw std::invalid_argument {
            "invalid region frontier physical binding element type"
        };
    }
    auto* const array_pointer = builder.CreateStructGEP(binding_type,
        physical_binding, field, name + ".array");
    const std::array<llvm::Value*, 2U> indices {
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(builder.getContext()),
            0U),
        index,
    };
    auto* const element_pointer = builder.CreateInBoundsGEP(array_type,
        array_pointer, indices, name + ".address");
    return builder.CreateLoad(llvm::Type::getInt32Ty(builder.getContext()),
        element_pointer, name);
}

} // namespace

llvm::Value* load_region_frontier_member_process_id_v2(
    llvm::IRBuilder<>& builder,
    llvm::StructType* binding_type,
    llvm::Value* physical_binding,
    llvm::Value* member_index)
{
    return load_physical_binding_array_element_v2(builder, binding_type,
        physical_binding, 2U, member_index, "binding.member.process");
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
        offsetof(RegionFrontierKeyV2, time), i64_type(builder), "key.time");
    const auto delta = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV2, delta), i64_type(builder), "key.delta");
    const auto round = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV2, systemverilog_round), i64_type(builder),
        "key.sv.round");
    const auto domain = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV2, process_domain), i32_type(builder),
        "key.process.domain");
    const auto phase = load_at(builder, key_base,
        offsetof(RegionFrontierKeyV2, phase), i32_type(builder), "key.phase");
    const auto slot_offset = offsetof(RegionFrontierFrameV2, slot);
    auto* matches = emit_equal(builder, time,
        load_frame(builder, frame, slot_offset + offsetof(RegionFrontierSlotV2, time),
            i64_type(builder), "slot.time"));
    matches = emit_and(builder, matches, emit_equal(builder, delta,
        load_frame(builder, frame, slot_offset + offsetof(RegionFrontierSlotV2, delta),
            i64_type(builder), "slot.delta")));
    const auto slot_round = load_frame(builder, frame,
        slot_offset + offsetof(RegionFrontierSlotV2, systemverilog_round),
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
            slot_offset + offsetof(RegionFrontierSlotV2, process_domain),
            i32_type(builder), "slot.process.domain")));
    return emit_and(builder, matches, emit_equal(builder, phase,
        load_frame(builder, frame,
            slot_offset + offsetof(RegionFrontierSlotV2, phase),
            i32_type(builder), "slot.phase")));
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
    const RegionFrontierStatusV2 status)
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
                kRegionFrontierPayloadKindShiftV2)), i8_type(builder));
}

static llvm::Value* task_payload_index(llvm::IRBuilder<>& builder,
    llvm::Value* payload)
{
    const auto masked = builder.CreateAnd(payload,
        llvm::ConstantInt::get(i64_type(builder),
            kRegionFrontierPayloadIndexMaskV2));
    return builder.CreateTrunc(masked, i32_type(builder));
}

static llvm::Value* task_payload_index_wide(llvm::IRBuilder<>& builder,
    llvm::Value* payload)
{
    return builder.CreateAnd(payload,
        llvm::ConstantInt::get(i64_type(builder),
            kRegionFrontierPayloadIndexMaskV2));
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
static llvm::Function* emit_region_frontier_loop_impl_v2(llvm::Module& module,
    const std::string& symbol,
    const RegionFrontierLayoutV2& layout,
    const EmitBoundCertifiedMemberBodyV2& emit_member,
    const EmitCertifiedInternalCommitV2& emit_internal_commit,
    const bool shared_body)
{
    const auto member_count = static_cast<std::size_t>(layout.member_count);
    const bool generic_mode = layout.execution_mode
        == RegionFrontierExecutionModeV2::generic_deferred_update;
    if (!region_frontier_layout_header_valid_v2(layout)
        || !region_frontier_execution_mode_valid_v2(layout.execution_mode)
        || layout.member_count == 0U
        || layout.members == nullptr
        || layout.max_member_write_counts == nullptr
        || layout.max_member_staged_event_counts == nullptr
        || (layout.signal_slot_count != 0U && layout.signals == nullptr)
        || (layout.write_site_count != 0U && layout.write_sites == nullptr)) {
        throw std::invalid_argument { "invalid region frontier V2 layout" };
    }
    for (std::uint32_t signal_slot = 0U;
         signal_slot < layout.signal_slot_count; ++signal_slot) {
        const auto& signal = layout.signals[signal_slot];
        if (!region_frontier_value_shape_valid_v2(signal.value_kind,
                signal.width, signal.word_count, signal.plane_count)
            || (generic_mode
                && signal.flags
                    != RegionFrontierPlaneFlagsV2::read_only_boundary_port)) {
            throw std::invalid_argument {
                "invalid region frontier V2 signal value shape"
            };
        }
    }
    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        if (site.signal_slot >= layout.signal_slot_count) {
            throw std::invalid_argument {
                "invalid region frontier V2 write site shape"
            };
        }
        const auto& signal = layout.signals[site.signal_slot];
        const auto role_flags = signal.flags
            & (RegionFrontierPlaneFlagsV2::certified_internal_single_owner
                | RegionFrontierPlaneFlagsV2::read_only_boundary_port);
        const bool internal_site
            = role_flags
            == RegionFrontierPlaneFlagsV2::certified_internal_single_owner;
        const bool boundary_site
            = role_flags
            == RegionFrontierPlaneFlagsV2::read_only_boundary_port;
        const bool site_extent_valid
            = !generic_mode && !internal_site && boundary_site
            ? site.width <= signal.width
            : site.width == signal.width
                && site.word_count == signal.word_count;
        if ((!internal_site && !boundary_site)
            || site.value_kind != signal.value_kind
            || !site_extent_valid
            || site.pending_slot >= layout.pending_write_capacity
            || !region_frontier_value_shape_valid_v2(site.value_kind,
                site.width, site.word_count, site.plane_count)
            || site.plane_count != signal.plane_count
            || (generic_mode
                && (site.event_kind != static_cast<std::uint32_t>(
                        RegionFrontierEventKindV2::generic_deferred_update)
                    || site.update_kind != static_cast<std::uint32_t>(
                        RegionUpdateKind::generic)
                    || (site.value_kind
                            != RegionFrontierValueKindV2::logic4
                        && site.value_kind
                            != RegionFrontierValueKindV2::logic9)))
            || (!generic_mode
                && site.event_kind == static_cast<std::uint32_t>(
                    RegionFrontierEventKindV2::generic_deferred_update))) {
            throw std::invalid_argument {
                "invalid region frontier V2 write site shape"
            };
        }
    }
    if (generic_mode
        && (layout.reserved0 != 0U
            || layout.reserved_capacity != 0U
            || layout.max_commit_fanout_events != 0U)) {
        throw std::invalid_argument {
            "invalid generic region frontier V2 layout contract"
        };
    }
    // Keep the standalone loop entry from emitting against a malformed
    // certificate even when a caller supplied a custom commit callback.
    validate_region_frontier_internal_commit_layout_v2(layout);
    const auto max_member_write_counts = std::span<const std::uint32_t> {
        layout.max_member_write_counts, member_count };
    const auto max_member_staged_event_counts
        = std::span<const std::uint32_t> {
            layout.max_member_staged_event_counts, member_count };
    const auto max_commit_fanout_events = layout.max_commit_fanout_events;
    // Only immutable certified recipe coordinates drive selected-member loops.
    // Public frame member descriptors remain mutable and are validated normally.
    std::vector<std::uint32_t> member_first_write_sites;
    std::vector<std::uint32_t> member_write_site_counts;
    bool contiguous_member_sites { true };
    for (std::size_t member = 0U; member < member_count; ++member) {
        const auto first = layout.members[member].first_write_site;
        const auto count = layout.members[member].write_site_count;
        if (first > layout.write_site_count
            || count > layout.write_site_count - first) {
            contiguous_member_sites = false;
            break;
        }
        for (std::uint32_t site = first; site < first + count; ++site) {
            if (layout.write_sites[site].member_index != member) {
                contiguous_member_sites = false;
                break;
            }
        }
        member_first_write_sites.push_back(first);
        member_write_site_counts.push_back(count);
    }
    if (contiguous_member_sites) {
        // Every member-owned write site must occur in the selected span.
        for (std::uint32_t site = 0U; site < layout.write_site_count; ++site) {
            const auto member = layout.write_sites[site].member_index;
            if (member >= member_count
                || site < member_first_write_sites[member]
                || site - member_first_write_sites[member]
                    >= member_write_site_counts[member]) {
                contiguous_member_sites = false;
                break;
            }
        }
    }

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
    auto* const i1 = llvm::Type::getInt1Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const frame_pointer = llvm::PointerType::getUnqual(context);
    auto* const binding_type = shared_body
        ? region_frontier_physical_binding_type_v2(context,
            layout.member_count, layout.signal_slot_count)
        : nullptr;
    std::vector<llvm::Type*> function_parameters { frame_pointer };
    if (shared_body) {
        function_parameters.push_back(frame_pointer);
        function_parameters.push_back(i1);
        function_parameters.push_back(i1);
        function_parameters.push_back(i1);
    }
    auto* const function_type = llvm::FunctionType::get(i32,
        function_parameters, false);
    if (module.getNamedValue(symbol) != nullptr) {
        throw std::invalid_argument {
            "duplicate region frontier loop symbol"
        };
    }
    auto* const function = llvm::Function::Create(function_type,
        llvm::GlobalValue::ExternalLinkage, symbol, module);
    auto* const frame = function->getArg(0U);
    frame->setName("instance_frame");
    auto* const physical_binding = shared_body
        ? function->getArg(1U) : nullptr;
    auto* const alias_prevalidated = shared_body
        ? function->getArg(2U) : nullptr;
    auto* const value_contents_prevalidated = shared_body
        ? function->getArg(3U) : nullptr;
    auto* const descriptor_shapes_prevalidated = shared_body
        ? function->getArg(4U) : nullptr;
    if (physical_binding != nullptr) {
        physical_binding->setName("physical_binding");
    }
    if (alias_prevalidated != nullptr) {
        alias_prevalidated->setName("alias_prevalidated");
    }
    if (value_contents_prevalidated != nullptr) {
        value_contents_prevalidated->setName("value_contents_prevalidated");
    }
    if (descriptor_shapes_prevalidated != nullptr) {
        descriptor_shapes_prevalidated->setName(
            "descriptor_shapes_prevalidated");
    }

    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto* const prefix_validation = llvm::BasicBlock::Create(
        context, "abi.prefix.validation", function);
    auto* const contract_validation = llvm::BasicBlock::Create(
        context, "abi.contract.validation", function);
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
    auto* const member_preamble = llvm::BasicBlock::Create(
        context, "member.preamble", function);
    auto* const member_dispatch = llvm::BasicBlock::Create(
        context, "member.dispatch", function);
    auto* const impossible_member_dispatch = llvm::BasicBlock::Create(
        context, "member.dispatch.impossible", function);
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
    builder.CreateCondBr(builder.CreateIsNotNull(frame), prefix_validation,
        decline);

    // Only the first two 32-bit fields are readable until the exact V2 size
    // has passed. This rejects V1 and truncated/future frames before reading
    // the V2 contract tag or any frame pointer.
    builder.SetInsertPoint(prefix_validation);
    const auto prefix_abi_version = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, abi_version), i32,
        "abi.prefix.version");
    const auto prefix_struct_size = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, struct_size), i32,
        "abi.prefix.size");
    auto* prefix_valid = emit_equal(builder, prefix_abi_version,
        llvm::ConstantInt::get(i32, kRegionFrontierAbiVersionV2));
    prefix_valid = emit_and(builder, prefix_valid, emit_equal(builder,
        prefix_struct_size,
        llvm::ConstantInt::get(i32, sizeof(RegionFrontierFrameV2))));
    builder.CreateCondBr(prefix_valid, contract_validation, decline);

    builder.SetInsertPoint(contract_validation);
    const auto prefix_contract = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, value_plane_contract), i32,
        "abi.value.plane.contract");
    builder.CreateCondBr(emit_equal(builder, prefix_contract,
            llvm::ConstantInt::get(i32,
                kRegionFrontierValuePlaneContractV2)), preflight, decline);

    builder.SetInsertPoint(preflight);
    const auto abi_version = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, abi_version), i32, "abi.version");
    const auto struct_size = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, struct_size), i32, "abi.size");
    const auto value_plane_contract = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, value_plane_contract), i32,
        "value.plane.contract");
    const auto generic_update_ack_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, generic_update_ack_count), i32,
        "generic.update.ack.count");
    const auto runtime_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, runtime_generation), i64,
        "runtime.generation");
    const auto bound_runtime_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, bound_runtime_generation), i64,
        "bound.runtime.generation");
    const auto certificate_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, certificate_generation), i64,
        "certificate.generation");
    const auto component_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, component_generation), i64,
        "component.generation");
    const auto scheduler_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, scheduler_frontier_generation), i64,
        "scheduler.frontier.generation");
    const auto member_count_value = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, member_count), i32, "member.count");
    const auto task_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, scheduler_task_count), i32,
        "scheduler.task.count");
    const auto task_cursor = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, scheduler_task_cursor), i32,
        "scheduler.task.cursor");
    const auto task_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, scheduler_task_capacity), i32,
        "scheduler.task.capacity");
    const auto readiness_words = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, readiness_word_count), i32,
        "readiness.words");
    const auto pending_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_write_capacity), i32,
        "pending.write.capacity");
    const auto pending_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_write_count), i32,
        "pending.write.count");
    const auto event_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_event_capacity), i32,
        "staged.event.capacity");
    const auto event_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_event_count), i32,
        "staged.event.count");
    const auto signal_slot_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, signal_slot_count), i32,
        "signal.slot.count");
    const auto metadata_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, metadata_count), i32,
        "metadata.count");
    const auto fanout_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, fanout_edge_count), i32,
        "fanout.count");
    const auto committed_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, committed_signal_capacity), i32,
        "committed.signal.capacity");
    const auto committed_count = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, committed_signal_count), i32,
        "committed.signal.count");
    const auto cut_kind = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, cut)
            + offsetof(RegionFrontierCutV2, kind), i8, "cut.kind");
    const auto cut_generation = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, cut)
            + offsetof(RegionFrontierCutV2, scheduler_frontier_generation),
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
    const auto expected_certificate_generation = shared_body
        ? load_physical_binding_scalar_v2(builder, binding_type,
            physical_binding, 0U, i64, "binding.certificate.generation")
        : expected_i64(builder, layout.certificate_generation);
    generations_match = emit_and(builder, generations_match,
        emit_equal(builder, certificate_generation,
            expected_certificate_generation));
    const auto expected_component_generation = shared_body
        ? load_physical_binding_scalar_v2(builder, binding_type,
            physical_binding, 1U, i64, "binding.component.generation")
        : expected_i64(builder, layout.component_generation);
    generations_match = emit_and(builder, generations_match,
        emit_equal(builder, component_generation,
            expected_component_generation));
    generations_match = emit_and(builder, generations_match,
        emit_equal(builder, scheduler_generation, cut_generation));

    const auto same_slot_cut = emit_equal(builder, cut_kind,
        llvm::ConstantInt::get(i8, static_cast<std::uint8_t>(
            RegionFrontierCutKindV2::same_slot_key)));
    const auto closed_prefix_cut = emit_equal(builder, cut_kind,
        llvm::ConstantInt::get(i8, static_cast<std::uint8_t>(
            RegionFrontierCutKindV2::closed_prefix)));
    const auto cut_known = builder.CreateOr(same_slot_cut, closed_prefix_cut);
    llvm::Value* cut_slot_matches = llvm::ConstantInt::getTrue(context);
    auto* const cut_key_pointer = constant_offset(builder, frame,
        offsetof(RegionFrontierFrameV2, cut)
            + offsetof(RegionFrontierCutV2, next_key));
    cut_slot_matches = emit_slot_matches_key(builder, frame, cut_key_pointer);

    const auto expected_member_count = expected_i32(builder,
        static_cast<std::uint32_t>(member_count));
    const auto expected_words = expected_i32(builder,
        static_cast<std::uint32_t>((member_count + 63U) / 64U));
    auto* static_ok = emit_equal(builder, abi_version,
        expected_i32(builder, kRegionFrontierAbiVersionV2));
    static_ok = emit_and(builder, static_ok, emit_equal(builder, struct_size,
        expected_i32(builder, sizeof(RegionFrontierFrameV2))));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        value_plane_contract,
        expected_i32(builder, kRegionFrontierValuePlaneContractV2)));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        generic_update_ack_count, expected_i32(builder, 0U)));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        member_count_value, expected_member_count));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        signal_slot_count, expected_i32(builder, layout.signal_slot_count)));
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
            offsetof(RegionFrontierFrameV2, slot)
                + offsetof(RegionFrontierSlotV2, process_domain), i32,
            "slot.domain"),
        expected_i32(builder, generic_mode
            ? static_cast<std::uint32_t>(ProcessSchedulingDomain::generic)
            : static_cast<std::uint32_t>(
                ProcessSchedulingDomain::systemverilog))));
    static_ok = emit_and(builder, static_ok, emit_equal(builder,
        load_frame(builder, frame,
            offsetof(RegionFrontierFrameV2, slot)
                + offsetof(RegionFrontierSlotV2, phase), i32,
            "slot.phase"),
        expected_i32(builder, static_cast<std::uint32_t>(
                                  SchedulerPhase::active))));
    if (generic_mode) {
        static_ok = emit_and(builder, static_ok, emit_equal(builder,
            load_frame(builder, frame,
                offsetof(RegionFrontierFrameV2, slot)
                    + offsetof(RegionFrontierSlotV2, systemverilog_round),
                i64, "generic.slot.round"), expected_i64(builder, 0U)));
        static_ok = emit_and(builder, static_ok, emit_equal(builder,
            pending_count, expected_i32(builder, 0U)));
        static_ok = emit_and(builder, static_ok, emit_equal(builder,
            event_count, expected_i32(builder, 0U)));
        static_ok = emit_and(builder, static_ok, emit_equal(builder,
            committed_count, expected_i32(builder, 0U)));
    }
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
        offsetof(RegionFrontierFrameV2, ready_words), "ready.words.ptr");
    auto* const members_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, members), "members.ptr");
    auto* const tasks_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, scheduler_tasks), "tasks.ptr");
    auto* const writes_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_writes), "writes.ptr");
    auto* const staged_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_events), "staged.events.ptr");
    auto* const committed_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, committed_signals),
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
        offsetof(RegionFrontierFrameV2, planes), "planes.ptr");
    const auto metadata_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, metadata), "metadata.ptr");
    const auto fanout_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, fanout_edges), "fanout.ptr");
    const auto port_planes_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, port_planes), "port.planes.ptr");
    const auto dispatch_counter_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, native_frontier_member_dispatches),
        "dispatch.counter.ptr");
    const auto stop_requested_ptr = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2, stop_requested), "stop.requested.ptr");
    auto* state_arrays_ok = builder.CreateAnd(
        builder.CreateOr(emit_equal(builder, signal_slot_count,
                           expected_i32(builder, 0U)),
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
        sizeof(RegionFrontierFrameV2), alignof(RegionFrontierFrameV2));
    append_frame_range(ready_words_ptr, readiness_words,
        sizeof(std::uint64_t), alignof(std::uint64_t));
    append_frame_range(members_ptr, member_count_value,
        sizeof(RegionFrontierMemberV2), alignof(RegionFrontierMemberV2));
    append_frame_range(tasks_ptr, task_count,
        sizeof(RegionFrontierSchedulerTaskV2),
        alignof(RegionFrontierSchedulerTaskV2));
    append_frame_range(planes_ptr, signal_slot_count,
        sizeof(RegionFrontierPlaneV2), alignof(RegionFrontierPlaneV2));
    append_frame_range(metadata_ptr, metadata_count,
        sizeof(RegionFrontierSignalMetadataV2),
        alignof(RegionFrontierSignalMetadataV2));
    append_frame_range(fanout_ptr, fanout_count,
        sizeof(RegionFrontierFanoutEdgeV2),
        alignof(RegionFrontierFanoutEdgeV2));
    append_frame_range(port_planes_ptr, signal_slot_count, sizeof(void*),
        alignof(void*));
    append_frame_range(writes_ptr, pending_capacity,
        sizeof(RegionFrontierPendingWriteV2),
        alignof(RegionFrontierPendingWriteV2));
    append_frame_range(staged_ptr, event_capacity,
        sizeof(RegionFrontierStagedEventV2),
        alignof(RegionFrontierStagedEventV2));
    append_frame_range(committed_ptr, committed_capacity,
        sizeof(RegionFrontierCommittedSignalV2),
        alignof(RegionFrontierCommittedSignalV2));
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
    return_status(builder, RegionFrontierStatusV2::stale_generation);

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

    struct ValidationRangeDescriptor {
        std::uint32_t storage_kind { };
        std::uint32_t slot { };
        std::uint32_t pointer_offset { };
        std::uint64_t bytes { };
        std::uint64_t alias_tag { };
    };
    constexpr auto signal_plane_storage = std::uint32_t { 0U };
    constexpr auto pending_write_storage = std::uint32_t { 1U };
    std::vector<ValidationRangeDescriptor> validation_ranges;
    llvm::Value* plane_map_ok = llvm::ConstantInt::getTrue(context);
    auto* const i8_pointer = llvm::PointerType::getUnqual(context);
    const std::array<std::size_t, 4U> role_array_offsets {
        offsetof(RegionFrontierPlaneV2, current_planes),
        offsetof(RegionFrontierPlaneV2, previous_planes),
        offsetof(RegionFrontierPlaneV2, stored_planes),
        offsetof(RegionFrontierPlaneV2, owner_planes),
    };
    for (std::uint32_t member_index = 0U;
         member_index < layout.member_count; ++member_index) {
        auto* const member = indexed_pointer(builder, members_ptr,
            expected_i32(builder, member_index), sizeof(RegionFrontierMemberV2));
        const auto expected_member_process_id = shared_body
            ? load_region_frontier_member_process_id_v2(builder, binding_type,
                physical_binding, expected_i32(builder, member_index))
            : expected_i32(builder,
                layout.members[member_index].process_id);
        plane_map_ok = emit_and(builder, plane_map_ok, emit_equal(builder,
            load_at(builder, member,
                offsetof(RegionFrontierMemberV2, process_id), i32,
                "mapped.member.process"), expected_member_process_id));
    }
    for (std::uint32_t edge_index = 0U;
         edge_index < layout.fanout_edge_count; ++edge_index) {
        const auto& expected_edge = layout.fanout_edges[edge_index];
        auto* const edge = indexed_pointer(builder, fanout_ptr,
            expected_i32(builder, edge_index),
            sizeof(RegionFrontierFanoutEdgeV2));
        plane_map_ok = emit_and(builder, plane_map_ok, emit_equal(builder,
            load_at(builder, edge,
                offsetof(RegionFrontierFanoutEdgeV2, signal_slot), i32,
                "mapped.fanout.signal"),
            expected_i32(builder, expected_edge.signal_slot)));
        plane_map_ok = emit_and(builder, plane_map_ok, emit_equal(builder,
            load_at(builder, edge,
                offsetof(RegionFrontierFanoutEdgeV2, member_index), i32,
                "mapped.fanout.member"),
            expected_i32(builder, expected_edge.member_index)));
        const auto trigger_mask = load_at(builder, edge,
            offsetof(RegionFrontierFanoutEdgeV2, trigger_mask), i64,
            "mapped.fanout.trigger.mask");
        const auto trigger_mask_matches_mode = generic_mode
            ? builder.CreateICmpEQ(trigger_mask, expected_i64(builder, 0U))
            : builder.CreateICmpNE(trigger_mask, expected_i64(builder, 0U));
        plane_map_ok = emit_and(builder, plane_map_ok,
            trigger_mask_matches_mode);
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
    const auto append_mutable_buffer = [&](const std::uint32_t signal_slot,
                                           const std::uint32_t value_plane,
                                           const std::uint32_t role_index,
                                           const std::uint64_t bytes) {
        const auto pointer_offset = pointer_array_element_offset(
            role_array_offsets[role_index], value_plane);
        const auto alias_key = static_cast<std::uint64_t>(signal_slot)
                * 4U
            + value_plane + 1U;
        const auto alias_tag = role_index == 2U || role_index == 3U
            ? alias_key * 2U + (role_index == 3U ? 1U : 0U) : 0U;
        validation_ranges.push_back({ signal_plane_storage,
            signal_slot, static_cast<std::uint32_t>(pointer_offset), bytes,
            alias_tag });
    };
    const auto append_boundary_buffer = [&](const std::uint32_t signal_slot,
                                            const std::uint32_t value_plane,
                                            const std::uint64_t bytes) {
        const auto pointer_offset = pointer_array_element_offset(
            offsetof(RegionFrontierPlaneV2, boundary_planes), value_plane);
        validation_ranges.push_back({ signal_plane_storage,
            signal_slot, static_cast<std::uint32_t>(pointer_offset), bytes,
            0U });
    };
    struct SignalPlaneShape {
        std::uint32_t signal_id { };
        std::uint32_t owner_process_id { };
        std::uint32_t value_kind { };
        std::uint32_t width { };
        std::uint32_t word_count { };
        std::uint32_t plane_count { };
        std::uint32_t flags { };
        std::uint32_t metadata_index { };
    };
    constexpr auto signal_plane_shape_bytes = sizeof(SignalPlaneShape);
    const auto signal_plane_shape_count
        = static_cast<std::size_t>(layout.signal_slot_count);
    std::vector<SignalPlaneShape> signal_plane_shapes;
    if (signal_plane_shape_count
        > std::numeric_limits<std::size_t>::max()
            / signal_plane_shape_bytes
        || signal_plane_shape_count > signal_plane_shapes.max_size()) {
        throw std::invalid_argument {
            "region frontier signal-plane shape table is too large"
        };
    }
    signal_plane_shapes.reserve(signal_plane_shape_count);
    for (std::uint32_t signal_slot = 0U;
         signal_slot < layout.signal_slot_count; ++signal_slot) {
        const auto& expected_plane = layout.signals[signal_slot];
        signal_plane_shapes.push_back({
            shared_body ? 0U : expected_plane.signal_id,
            shared_body ? 0U : expected_plane.owner_process_id,
            static_cast<std::uint32_t>(expected_plane.value_kind),
            expected_plane.width,
            expected_plane.word_count,
            expected_plane.plane_count,
            expected_plane.flags,
            expected_plane.metadata_index,
        });

        const bool internal
            = (expected_plane.flags
                & RegionFrontierPlaneFlagsV2::certified_internal_single_owner)
            != 0U;
        const bool boundary
            = (expected_plane.flags
                & RegionFrontierPlaneFlagsV2::read_only_boundary_port)
            != 0U;
        const auto byte_count = static_cast<std::uint64_t>(
            expected_plane.word_count) * sizeof(std::uint64_t);
        for (std::uint32_t value_plane = 0U; value_plane < 4U; ++value_plane) {
            const bool present = value_plane < expected_plane.plane_count;
            if (internal && present) {
                for (std::uint32_t role = 0U;
                     role < role_array_offsets.size(); ++role) {
                    append_mutable_buffer(signal_slot, value_plane, role,
                        byte_count);
                }
            }
            if (boundary && present) {
                append_boundary_buffer(signal_slot, value_plane, byte_count);
            }
        }
    }

    llvm::StructType* signal_shape_record_type { };
    llvm::ArrayType* signal_shape_array_type { };
    llvm::GlobalVariable* expected_signal_shapes { };
    llvm::BasicBlock* descriptor_shape_skip { };
    const auto descriptor_shape_base_map_ok = plane_map_ok;
    if (shared_body
        && (signal_plane_shape_count != 0U
            || layout.write_site_count != 0U)) {
        auto* const descriptor_shape_validation = llvm::BasicBlock::Create(
            context, "validate.descriptor.shapes", function);
        descriptor_shape_skip = llvm::BasicBlock::Create(context,
            "descriptor.shapes.prevalidated", function);
        builder.CreateCondBr(descriptor_shapes_prevalidated,
            descriptor_shape_skip, descriptor_shape_validation);
        builder.SetInsertPoint(descriptor_shape_validation);
    }
    if (signal_plane_shape_count != 0U) {
        const std::array<llvm::Type*, 8U> shape_field_types {
            i32, i32, i32, i32, i32, i32, i32, i32,
        };
        signal_shape_record_type = llvm::StructType::get(context,
            shape_field_types, false);
        signal_shape_array_type = llvm::ArrayType::get(
            signal_shape_record_type, signal_plane_shape_count);
        std::vector<llvm::Constant*> shape_constants;
        if (signal_plane_shape_count > shape_constants.max_size()) {
            throw std::invalid_argument {
                "region frontier signal-plane shape constants are too large"
            };
        }
        shape_constants.reserve(signal_plane_shape_count);
        for (const auto& shape : signal_plane_shapes) {
            shape_constants.push_back(llvm::ConstantStruct::get(
                signal_shape_record_type, {
                    llvm::ConstantInt::get(i32, shape.signal_id),
                    llvm::ConstantInt::get(i32, shape.owner_process_id),
                    llvm::ConstantInt::get(i32, shape.value_kind),
                    llvm::ConstantInt::get(i32, shape.width),
                    llvm::ConstantInt::get(i32, shape.word_count),
                    llvm::ConstantInt::get(i32, shape.plane_count),
                    llvm::ConstantInt::get(i32, shape.flags),
                    llvm::ConstantInt::get(i32, shape.metadata_index),
                }));
        }
        expected_signal_shapes = new llvm::GlobalVariable(module,
            signal_shape_array_type, true, llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantArray::get(signal_shape_array_type, shape_constants),
            function->getName().str() + ".signal.plane.shapes");

        auto* const signal_shape_header = llvm::BasicBlock::Create(context,
            "validate.signal.shape.header", function);
        auto* const signal_shape_body = llvm::BasicBlock::Create(context,
            "validate.signal.shape.body", function);
        auto* const signal_shape_advance = llvm::BasicBlock::Create(context,
            "validate.signal.shape.advance", function);
        auto* const signal_shape_done = llvm::BasicBlock::Create(context,
            "validate.signal.shape.done", function);
        auto* const signal_shape_preheader = builder.GetInsertBlock();
        builder.CreateBr(signal_shape_header);

        builder.SetInsertPoint(signal_shape_header);
        auto* const signal_shape_index = builder.CreatePHI(i64, 2U,
            "signal.shape.index");
        auto* const accumulated_plane_map_ok = builder.CreatePHI(
            llvm::Type::getInt1Ty(context), 2U,
            "signal.shape.matches");
        signal_shape_index->addIncoming(expected_i64(builder, 0U),
            signal_shape_preheader);
        accumulated_plane_map_ok->addIncoming(plane_map_ok,
            signal_shape_preheader);
        builder.CreateCondBr(builder.CreateICmpULT(signal_shape_index,
                expected_i64(builder, signal_plane_shape_count)),
            signal_shape_body, signal_shape_done);

        builder.SetInsertPoint(signal_shape_body);
        const std::array<llvm::Value*, 2U> shape_indices {
            expected_i64(builder, 0U), signal_shape_index,
        };
        auto* const expected_shape = builder.CreateInBoundsGEP(
            signal_shape_array_type, expected_signal_shapes, shape_indices,
            "expected.signal.plane.shape");
        const auto load_signal_shape_field = [&](const unsigned field,
                                                  const llvm::Twine& name) {
            auto* const field_pointer = builder.CreateStructGEP(
                signal_shape_record_type, expected_shape, field,
                name + ".field");
            return builder.CreateLoad(i32, field_pointer, name);
        };
        const auto expected_signal_slot = builder.CreateTrunc(
            signal_shape_index, i32, "signal.shape.slot");
        const auto expected_signal_id = shared_body
            ? load_physical_binding_array_element_v2(builder, binding_type,
                physical_binding, 3U, expected_signal_slot,
                "binding.signal.id")
            : load_signal_shape_field(0U, "expected.signal.id");
        const auto expected_owner_process_id = shared_body
            ? load_physical_binding_array_element_v2(builder, binding_type,
                physical_binding, 4U, expected_signal_slot,
                "binding.owner.process")
            : load_signal_shape_field(1U, "expected.owner.process");
        const auto expected_value_kind = load_signal_shape_field(2U,
            "expected.value.kind");
        const auto expected_width = load_signal_shape_field(3U,
            "expected.width");
        const auto expected_word_count = load_signal_shape_field(4U,
            "expected.word.count");
        const auto expected_plane_count = load_signal_shape_field(5U,
            "expected.plane.count");
        const auto expected_flags = load_signal_shape_field(6U,
            "expected.flags");
        const auto expected_metadata_index = load_signal_shape_field(7U,
            "expected.metadata.index");
        auto* const plane = indexed_pointer(builder, planes_ptr,
            expected_signal_slot, sizeof(RegionFrontierPlaneV2));
        auto* matches = emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV2, signal_id), i32,
                "mapped.signal.id"),
            expected_signal_id);
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV2, owner_process_id), i32,
                "mapped.owner.process"),
            expected_owner_process_id));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV2, value_kind), i32,
                "mapped.value.kind"), expected_value_kind));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV2, width), i32,
                "mapped.width"), expected_width));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV2, word_count), i32,
                "mapped.word.count"), expected_word_count));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV2, plane_count), i32,
                "mapped.plane.count"), expected_plane_count));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV2, flags), i32,
                "mapped.flags"), expected_flags));
        matches = emit_and(builder, matches, emit_equal(builder,
            load_at(builder, plane,
                offsetof(RegionFrontierPlaneV2, metadata_index), i32,
                "mapped.metadata.index"), expected_metadata_index));
        const auto port_plane = load_array_field(builder, port_planes_ptr,
            expected_signal_slot, sizeof(void*), 0U, i8_pointer,
            "mapped.port.plane");
        matches = emit_and(builder, matches,
            builder.CreateICmpEQ(port_plane, plane));

        const auto internal_mask = expected_i32(builder,
            RegionFrontierPlaneFlagsV2::certified_internal_single_owner);
        const auto boundary_mask = expected_i32(builder,
            RegionFrontierPlaneFlagsV2::read_only_boundary_port);
        const auto is_internal = builder.CreateICmpNE(
            builder.CreateAnd(expected_flags, internal_mask),
            expected_i32(builder, 0U));
        const auto is_boundary = builder.CreateICmpNE(
            builder.CreateAnd(expected_flags, boundary_mask),
            expected_i32(builder, 0U));
        for (std::uint32_t value_plane = 0U; value_plane < 4U; ++value_plane) {
            const auto plane_is_present = builder.CreateICmpULT(
                expected_i32(builder, value_plane), expected_plane_count);
            const auto mutable_plane_is_present = builder.CreateAnd(
                is_internal, plane_is_present);
            for (std::uint32_t role = 0U;
                 role < role_array_offsets.size(); ++role) {
                const auto pointer = value_role_pointer(builder, plane,
                    role_array_offsets[role], value_plane,
                    "mapped.mutable.value.plane");
                const auto present_pointer_valid = add_nonnull_aligned(pointer);
                const auto absent_pointer_valid = builder.CreateIsNull(pointer);
                const auto pointer_shape_matches = builder.CreateSelect(
                    mutable_plane_is_present, present_pointer_valid,
                    absent_pointer_valid, "mapped.mutable.pointer.matches");
                matches = emit_and(builder, matches, pointer_shape_matches);
            }
            const auto boundary_pointer = load_at(builder, plane,
                pointer_array_element_offset(
                    offsetof(RegionFrontierPlaneV2, boundary_planes),
                    value_plane), i8_pointer,
                "mapped.boundary.value.plane");
            const auto boundary_plane_is_present = builder.CreateAnd(
                is_boundary, plane_is_present);
            const auto present_boundary_valid
                = add_nonnull_aligned(boundary_pointer);
            const auto absent_boundary_valid
                = builder.CreateIsNull(boundary_pointer);
            const auto boundary_shape_matches = builder.CreateSelect(
                boundary_plane_is_present, present_boundary_valid,
                absent_boundary_valid, "mapped.boundary.pointer.matches");
            matches = emit_and(builder, matches, boundary_shape_matches);
        }

        const auto next_plane_map_ok = emit_and(builder,
            accumulated_plane_map_ok, matches);
        builder.CreateBr(signal_shape_advance);

        builder.SetInsertPoint(signal_shape_advance);
        const auto next_signal_shape_index = builder.CreateAdd(
            signal_shape_index, expected_i64(builder, 1U),
            "signal.shape.next.index");
        builder.CreateBr(signal_shape_header);
        signal_shape_index->addIncoming(next_signal_shape_index,
            signal_shape_advance);
        accumulated_plane_map_ok->addIncoming(next_plane_map_ok,
            signal_shape_advance);
        auto* const signal_shape_latch
            = signal_shape_advance->getTerminator();
        signal_shape_latch->setMetadata(llvm::LLVMContext::MD_loop,
            create_unroll_disabled_loop_id(context));

        builder.SetInsertPoint(signal_shape_done);
        plane_map_ok = accumulated_plane_map_ok;
    }

    const auto signal_range_count = validation_ranges.size();

    // The pending-write records are mutable frame input, but their expected
    // shapes are immutable layout data. Keep those expectations in a private
    // module constant and validate the frame records with a compact runtime
    // loop instead of unrolling one copy of the checks per write site.
    using PendingWriteShape
        = fsim::compiler::llvm_detail::ExpectedInitialPendingSiteV1;
    constexpr auto pending_write_shape_bytes
        = sizeof(PendingWriteShape);
    const auto pending_write_shape_count
        = static_cast<std::size_t>(layout.write_site_count);
    std::vector<PendingWriteShape> pending_write_shapes;
    if (pending_write_shape_count
        > std::numeric_limits<std::size_t>::max()
            / pending_write_shape_bytes
        || pending_write_shape_count > pending_write_shapes.max_size()) {
        throw std::invalid_argument {
            "region frontier pending-write shape table is too large"
        };
    }
    pending_write_shapes.reserve(pending_write_shape_count);
    llvm::StructType* expected_write_shape_type { };
    llvm::ArrayType* expected_write_shape_array_type { };
    llvm::GlobalVariable* expected_write_shapes { };
    for (std::uint32_t site_index = 0U;
         site_index < layout.write_site_count; ++site_index) {
        const auto& site = layout.write_sites[site_index];
        pending_write_shapes.push_back({
            site.pending_slot,
            static_cast<std::uint32_t>(site.value_kind), site.width,
            site.word_count, site.plane_count, site.member_index,
            site.signal_slot, site.source_instruction, site.update_kind,
            site.event_kind,
        });

        const auto byte_count = static_cast<std::uint64_t>(site.word_count)
            * sizeof(std::uint64_t);
        for (std::uint32_t value_plane = 0U;
             value_plane < site.plane_count; ++value_plane) {
            const auto pointer_offset = pointer_array_element_offset(
                offsetof(RegionFrontierPendingWriteV2, value_planes),
                value_plane);
            validation_ranges.push_back({ pending_write_storage,
                site.pending_slot, static_cast<std::uint32_t>(pointer_offset),
                byte_count, 0U });
        }
    }

    if (layout.write_site_count != 0U) {
        const std::array<llvm::Type*, 10U> shape_field_types {
            i32,
            i32, i32, i32, i32, i32, i32, i32, i32, i32,
        };
        expected_write_shape_type = llvm::StructType::get(context,
            shape_field_types, false);
        expected_write_shape_array_type = llvm::ArrayType::get(
            expected_write_shape_type, pending_write_shape_count);
        std::vector<llvm::Constant*> shape_constants;
        if (pending_write_shape_count > shape_constants.max_size()) {
            throw std::invalid_argument {
                "region frontier pending-write shape constants are too large"
            };
        }
        shape_constants.reserve(pending_write_shape_count);
        for (const auto& shape : pending_write_shapes) {
            shape_constants.push_back(llvm::ConstantStruct::get(
                expected_write_shape_type, {
                    llvm::ConstantInt::get(i32, shape.pending_slot),
                    llvm::ConstantInt::get(i32, shape.value_kind),
                    llvm::ConstantInt::get(i32, shape.width),
                    llvm::ConstantInt::get(i32, shape.word_count),
                    llvm::ConstantInt::get(i32, shape.plane_count),
                    llvm::ConstantInt::get(i32, shape.member_index),
                    llvm::ConstantInt::get(i32, shape.signal_slot),
                    llvm::ConstantInt::get(i32, shape.source_instruction),
                    llvm::ConstantInt::get(i32, shape.update_kind),
                    llvm::ConstantInt::get(i32, shape.event_kind),
                }));
        }
        expected_write_shapes = new llvm::GlobalVariable(module,
            expected_write_shape_array_type, true,
            llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantArray::get(expected_write_shape_array_type,
                shape_constants),
            function->getName().str() + ".pending.write.shapes");

        auto* const write_shape_header = llvm::BasicBlock::Create(context,
            "validate.pending.write.shape.header", function);
        auto* const write_shape_body = llvm::BasicBlock::Create(context,
            "validate.pending.write.shape.body", function);
        auto* const write_shape_advance = llvm::BasicBlock::Create(context,
            "validate.pending.write.shape.advance", function);
        auto* const write_shape_done = llvm::BasicBlock::Create(context,
            "validate.pending.write.shape.done", function);
        auto* const write_shape_preheader = builder.GetInsertBlock();
        builder.CreateBr(write_shape_header);

        builder.SetInsertPoint(write_shape_header);
        auto* const write_shape_index = builder.CreatePHI(i64, 2U,
            "pending.write.shape.index");
        auto* const accumulated_shape_match = builder.CreatePHI(
            llvm::Type::getInt1Ty(context), 2U,
            "pending.write.shape.matches");
        write_shape_index->addIncoming(expected_i64(builder, 0U),
            write_shape_preheader);
        accumulated_shape_match->addIncoming(plane_map_ok,
            write_shape_preheader);
        const auto has_write_shape = builder.CreateICmpULT(write_shape_index,
            expected_i64(builder, pending_write_shape_count));
        builder.CreateCondBr(has_write_shape, write_shape_body,
            write_shape_done);

        builder.SetInsertPoint(write_shape_body);
        const std::array<llvm::Value*, 2U> shape_indices {
            expected_i64(builder, 0U), write_shape_index,
        };
        auto* const expected_shape = builder.CreateInBoundsGEP(
            expected_write_shape_array_type, expected_write_shapes,
            shape_indices,
            "expected.pending.write.shape");
        const auto load_expected_shape_field = [&](const unsigned field,
                                                   const llvm::Twine& name) {
            auto* const field_pointer = builder.CreateStructGEP(
                expected_write_shape_type, expected_shape, field,
                name + ".field");
            return builder.CreateLoad(i32, field_pointer, name);
        };
        const auto expected_pending_slot = load_expected_shape_field(0U,
            "expected.pending.slot");
        const auto expected_value_kind = load_expected_shape_field(1U,
            "expected.pending.value.kind");
        const auto expected_width = load_expected_shape_field(2U,
            "expected.pending.width");
        const auto expected_word_count = load_expected_shape_field(3U,
            "expected.pending.word.count");
        const auto expected_plane_count = load_expected_shape_field(4U,
            "expected.pending.plane.count");
        auto* const write = indexed_pointer(builder, writes_ptr,
            expected_pending_slot,
            sizeof(RegionFrontierPendingWriteV2));
        auto* pending_shape_matches = emit_equal(builder,
            load_at(builder, write,
                offsetof(RegionFrontierPendingWriteV2, value_kind), i32,
                "mapped.pending.value.kind"), expected_value_kind);
        pending_shape_matches = emit_and(builder, pending_shape_matches,
            emit_equal(builder, load_at(builder, write,
                    offsetof(RegionFrontierPendingWriteV2, width), i32,
                    "mapped.pending.width"), expected_width));
        pending_shape_matches = emit_and(builder, pending_shape_matches,
            emit_equal(builder, load_at(builder, write,
                    offsetof(RegionFrontierPendingWriteV2, word_count), i32,
                    "mapped.pending.word.count"), expected_word_count));
        pending_shape_matches = emit_and(builder, pending_shape_matches,
            emit_equal(builder, load_at(builder, write,
                    offsetof(RegionFrontierPendingWriteV2, plane_count), i32,
                    "mapped.pending.plane.count"), expected_plane_count));

        for (std::uint32_t value_plane = 0U;
             value_plane < 4U; ++value_plane) {
            const auto pointer = pending_value_pointer(builder, write,
                value_plane, "mapped.pending.value.plane");
            const auto plane_is_present = builder.CreateICmpULT(
                expected_i32(builder, value_plane), expected_plane_count);
            const auto present_plane_valid = add_nonnull_aligned(pointer);
            const auto absent_plane_valid = builder.CreateIsNull(pointer);
            const auto pointer_shape_matches = builder.CreateSelect(
                plane_is_present, present_plane_valid, absent_plane_valid,
                "mapped.pending.value.plane.matches");
            pending_shape_matches = emit_and(builder, pending_shape_matches,
                pointer_shape_matches);
        }

        const auto next_shape_match = emit_and(builder,
            accumulated_shape_match, pending_shape_matches);
        builder.CreateBr(write_shape_advance);

        builder.SetInsertPoint(write_shape_advance);
        const auto next_write_shape_index = builder.CreateAdd(
            write_shape_index, expected_i64(builder, 1U),
            "pending.write.shape.next.index");
        builder.CreateBr(write_shape_header);
        write_shape_index->addIncoming(next_write_shape_index,
            write_shape_advance);
        accumulated_shape_match->addIncoming(next_shape_match,
            write_shape_advance);

        builder.SetInsertPoint(write_shape_done);
        plane_map_ok = accumulated_shape_match;
    }

    if (descriptor_shape_skip != nullptr) {
        auto* const descriptor_shape_checked_exit = builder.GetInsertBlock();
        const auto descriptor_shape_checked_map_ok = plane_map_ok;
        auto* const descriptor_shape_merge = llvm::BasicBlock::Create(
            context, "descriptor.shape.validation.done", function);
        builder.CreateBr(descriptor_shape_merge);
        builder.SetInsertPoint(descriptor_shape_skip);
        builder.CreateBr(descriptor_shape_merge);
        builder.SetInsertPoint(descriptor_shape_merge);
        auto* const merged_plane_map_ok = builder.CreatePHI(i1, 2U,
            "plane.map.matches");
        merged_plane_map_ok->addIncoming(descriptor_shape_checked_map_ok,
            descriptor_shape_checked_exit);
        merged_plane_map_ok->addIncoming(descriptor_shape_base_map_ok,
            descriptor_shape_skip);
        plane_map_ok = merged_plane_map_ok;
    }

    auto* const validate_plane_tails = llvm::BasicBlock::Create(
        context, "validate.plane.tails", function);
    if (shared_body) {
        auto* const validate_range_geometry = llvm::BasicBlock::Create(
            context, "validate.range.geometry", function);
        auto* const plane_map_valid = llvm::BasicBlock::Create(context,
            "plane.map.valid", function);
        builder.CreateCondBr(plane_map_ok, plane_map_valid, decline);
        builder.SetInsertPoint(plane_map_valid);
        // The existing alias receipt skips only range geometry here. The
        // explicit descriptor-shape receipt has already bypassed only the
        // signal and pending shape loops; all entries still pass the live
        // member and fanout map checks above.
        builder.CreateCondBr(alias_prevalidated, validate_plane_tails,
            validate_range_geometry);
        builder.SetInsertPoint(validate_range_geometry);
    }
    if (validation_ranges.empty()) {
        if (shared_body) {
            builder.CreateBr(validate_plane_tails);
        } else {
            builder.CreateCondBr(plane_map_ok, validate_plane_tails, decline);
        }
    } else {
        auto* const range_descriptor_type = llvm::StructType::get(context,
            { i32, i32, i32, i64, i64 });
        auto* const range_descriptor_array_type = llvm::ArrayType::get(
            range_descriptor_type, validation_ranges.size());
        std::vector<llvm::Constant*> range_descriptor_constants;
        range_descriptor_constants.reserve(validation_ranges.size());
        for (const auto& descriptor : validation_ranges) {
            range_descriptor_constants.push_back(llvm::ConstantStruct::get(
                range_descriptor_type, {
                    llvm::ConstantInt::get(i32, descriptor.storage_kind),
                    llvm::ConstantInt::get(i32, descriptor.slot),
                    llvm::ConstantInt::get(i32, descriptor.pointer_offset),
                    llvm::ConstantInt::get(i64, descriptor.bytes),
                    llvm::ConstantInt::get(i64, descriptor.alias_tag),
                }));
        }
        // Keep the descriptor map as linear immutable module data. The
        // validation loop uses only fixed scalar allocas, never a graph-sized
        // runtime stack buffer.
        auto* const range_descriptor_global = new llvm::GlobalVariable(
            module, range_descriptor_array_type, true,
            llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantArray::get(range_descriptor_array_type,
                range_descriptor_constants),
            function->getName().str() + ".validation.ranges");

        const auto total_range_count = validation_ranges.size();
        const auto pending_range_count
            = total_range_count - signal_range_count;
        auto* const full_range_count = expected_i64(builder,
            total_range_count);
        auto* const signal_only_range_count = expected_i64(builder,
            signal_range_count);
        auto* const effective_range_count = builder.CreateAlloca(i64, nullptr,
            "range.effective.count");
        auto* const outer_index = builder.CreateAlloca(i64, nullptr,
            "range.outer.index");
        auto* const inner_index = builder.CreateAlloca(i64, nullptr,
            "range.inner.index");
        auto* const range_outer_header = llvm::BasicBlock::Create(
            context, "range.outer.header", function);
        auto* const range_outer_body = llvm::BasicBlock::Create(
            context, "range.outer.body", function);
        auto* const range_inner_initialize = llvm::BasicBlock::Create(
            context, "range.inner.initialize", function);
        auto* const range_inner_header = llvm::BasicBlock::Create(
            context, "range.inner.header", function);
        auto* const range_inner_body = llvm::BasicBlock::Create(
            context, "range.inner.body", function);
        auto* const range_inner_advance = llvm::BasicBlock::Create(
            context, "range.inner.advance", function);
        auto* const range_outer_advance = llvm::BasicBlock::Create(
            context, "range.outer.advance", function);

        struct RuntimeRange {
            llvm::Value* pointer { };
            llvm::Value* bytes { };
            llvm::Value* alias_tag { };
        };
        const auto load_runtime_range = [&](llvm::Value* index,
                                            const llvm::Twine& name) {
            const std::array<llvm::Value*, 2U> indices {
                expected_i64(builder, 0U), index,
            };
            auto* const descriptor = builder.CreateInBoundsGEP(
                range_descriptor_array_type, range_descriptor_global,
                indices, name + ".descriptor");
            const auto load_descriptor_field = [&](const unsigned field,
                                                   llvm::Type* type,
                                                   const llvm::Twine& suffix) {
                auto* const field_pointer = builder.CreateStructGEP(
                    range_descriptor_type, descriptor, field,
                    name + suffix + ".field");
                return builder.CreateLoad(type, field_pointer,
                    name + suffix);
            };
            auto* const storage_kind = load_descriptor_field(0U, i32,
                ".storage");
            auto* const slot = load_descriptor_field(1U, i32, ".slot");
            auto* const pointer_offset = load_descriptor_field(2U, i32,
                ".pointer.offset");
            auto* const bytes = load_descriptor_field(3U, i64, ".bytes");
            auto* const alias_tag = load_descriptor_field(4U, i64,
                ".alias.tag");
            auto* const signal_range = llvm::BasicBlock::Create(context,
                name + ".signal", function);
            auto* const pending_range = llvm::BasicBlock::Create(context,
                name + ".pending", function);
            auto* const loaded_range = llvm::BasicBlock::Create(context,
                name + ".loaded", function);
            builder.CreateCondBr(emit_equal(builder, storage_kind,
                                      expected_i32(builder,
                                          pending_write_storage)),
                pending_range, signal_range);

            builder.SetInsertPoint(signal_range);
            auto* const signal_record = indexed_pointer(builder, planes_ptr,
                slot, sizeof(RegionFrontierPlaneV2));
            auto* const signal_pointer_address = byte_pointer(builder,
                signal_record, builder.CreateZExt(pointer_offset, i64));
            auto* const signal_pointer = builder.CreateLoad(i8_pointer,
                signal_pointer_address, name + ".signal.pointer");
            builder.CreateBr(loaded_range);

            builder.SetInsertPoint(pending_range);
            auto* const pending_record = indexed_pointer(builder, writes_ptr,
                slot, sizeof(RegionFrontierPendingWriteV2));
            auto* const pending_pointer_address = byte_pointer(builder,
                pending_record, builder.CreateZExt(pointer_offset, i64));
            auto* const pending_pointer = builder.CreateLoad(i8_pointer,
                pending_pointer_address, name + ".pending.pointer");
            builder.CreateBr(loaded_range);

            builder.SetInsertPoint(loaded_range);
            auto* const pointer = builder.CreatePHI(i8_pointer, 2U,
                name + ".pointer");
            pointer->addIncoming(signal_pointer, signal_range);
            pointer->addIncoming(pending_pointer, pending_range);
            return RuntimeRange { pointer, bytes, alias_tag };
        };

        // Pending descriptors are a suffix and carry no alias exception.
        // Ordered nonempty ranges prove pending-pending separation; the
        // envelope proves their separation from signals and frame ranges. The
        // signal prefix still uses the exact alias checks below. Any failed
        // envelope check runs the full table because padding may overlap it.
        auto* const full_range_initialize = llvm::BasicBlock::Create(context,
            "range.full.initialize", function);
        llvm::BasicBlock* compact_range_initialize { };
        if (pending_range_count > 1U) {
            compact_range_initialize = llvm::BasicBlock::Create(context,
                "range.compact.initialize", function);
        }

        if (pending_range_count > 1U) {
            auto* const pending_order_start = llvm::BasicBlock::Create(
                context, "pending.range.order.start", function);
            auto* const pending_order_header = llvm::BasicBlock::Create(
                context, "pending.range.order.header", function);
            auto* const pending_order_body = llvm::BasicBlock::Create(
                context, "pending.range.order.body", function);
            auto* const pending_order_advance = llvm::BasicBlock::Create(
                context, "pending.range.order.advance", function);
            auto* const pending_bound_frame_checks
                = llvm::BasicBlock::Create(context,
                    "pending.range.bound.frames", function);
            auto* const pending_bound_signal_header
                = llvm::BasicBlock::Create(context,
                    "pending.range.bound.signal.header", function);
            auto* const pending_bound_signal_start
                = llvm::BasicBlock::Create(context,
                    "pending.range.bound.signal.start", function);
            auto* const pending_bound_signal_body
                = llvm::BasicBlock::Create(context,
                    "pending.range.bound.signal.body", function);
            auto* const pending_bound_signal_advance
                = llvm::BasicBlock::Create(context,
                    "pending.range.bound.signal.advance", function);

            auto* const pending_order_index = builder.CreateAlloca(i64,
                nullptr, "pending.range.order.index");
            auto* const previous_pending_end = builder.CreateAlloca(i64,
                nullptr, "pending.range.previous.end");
            auto* const pending_bound_signal_index = builder.CreateAlloca(i64,
                nullptr, "pending.range.bound.signal.index");
            auto* const first_pending_index = expected_i64(builder,
                signal_range_count);

            builder.CreateCondBr(plane_map_ok, pending_order_start, decline);

            builder.SetInsertPoint(pending_order_start);
            const auto first_pending_range = load_runtime_range(
                first_pending_index, "pending.range.first");
            const auto pending_base_address = builder.CreatePtrToInt(
                first_pending_range.pointer, i64,
                "pending.range.base.address");
            builder.CreateStore(expected_i64(builder, 0U),
                pending_order_index);
            builder.CreateStore(pending_base_address, previous_pending_end);
            builder.CreateBr(pending_order_header);

            builder.SetInsertPoint(pending_order_header);
            auto* const pending_order_value = builder.CreateLoad(i64,
                pending_order_index, "pending.range.order.value");
            builder.CreateCondBr(builder.CreateICmpULT(pending_order_value,
                                         expected_i64(builder,
                                             pending_range_count)),
                pending_order_body, pending_bound_frame_checks);

            builder.SetInsertPoint(pending_order_body);
            const auto descriptor_index = builder.CreateAdd(
                first_pending_index, pending_order_value,
                "pending.range.descriptor.index");
            const auto pending_range = load_runtime_range(descriptor_index,
                "pending.range.current");
            const auto pending_begin = builder.CreatePtrToInt(
                pending_range.pointer, i64, "pending.range.begin");
            const auto pending_end = builder.CreateAdd(pending_begin,
                pending_range.bytes, "pending.range.end");
            auto* const previous_end = builder.CreateLoad(i64,
                previous_pending_end, "pending.range.previous.end.value");
            const auto positive_range = builder.CreateICmpNE(
                pending_range.bytes, expected_i64(builder, 0U));
            const auto nonwrapping_range = builder.CreateICmpUGE(
                pending_end, pending_begin);
            const auto ordered_range = builder.CreateICmpUGE(
                pending_begin, previous_end);
            const auto ordered_nonoverlapping = emit_and(builder,
                positive_range, emit_and(builder, nonwrapping_range,
                    ordered_range));
            builder.CreateCondBr(ordered_nonoverlapping,
                pending_order_advance, full_range_initialize);

            builder.SetInsertPoint(pending_order_advance);
            builder.CreateStore(pending_end, previous_pending_end);
            builder.CreateStore(builder.CreateAdd(pending_order_value,
                                     expected_i64(builder, 1U)),
                pending_order_index);
            auto* const pending_order_latch = builder.CreateBr(
                pending_order_header);
            pending_order_latch->setMetadata(llvm::LLVMContext::MD_loop,
                create_unroll_disabled_loop_id(context));

            builder.SetInsertPoint(pending_bound_frame_checks);
            const auto pending_bound_end = builder.CreateLoad(i64,
                previous_pending_end, "pending.range.bound.end");
            const auto pending_bound_bytes = builder.CreateSub(
                pending_bound_end, pending_base_address,
                "pending.range.bound.bytes");
            llvm::Value* bound_disjoint_from_frames
                = builder.CreateICmpUGT(pending_bound_bytes,
                    expected_i64(builder, 0U));
            for (std::size_t frame_index = 0U;
                 frame_index < frame_ranges.size(); ++frame_index) {
                const auto& frame_range = frame_ranges[frame_index];
                bound_disjoint_from_frames = emit_and(builder,
                    bound_disjoint_from_frames,
                    pointer_ranges_disjoint(builder,
                        first_pending_range.pointer, pending_bound_bytes,
                        frame_range.pointer, frame_range.bytes));
            }
            builder.CreateCondBr(bound_disjoint_from_frames,
                pending_bound_signal_start, full_range_initialize);

            builder.SetInsertPoint(pending_bound_signal_start);
            builder.CreateStore(expected_i64(builder, 0U),
                pending_bound_signal_index);
            builder.CreateBr(pending_bound_signal_header);

            builder.SetInsertPoint(pending_bound_signal_header);
            const auto signal_index = builder.CreateLoad(i64,
                pending_bound_signal_index,
                "pending.range.bound.signal.index.value");
            builder.CreateCondBr(builder.CreateICmpULT(signal_index,
                                            signal_only_range_count),
                pending_bound_signal_body, compact_range_initialize);

            builder.SetInsertPoint(pending_bound_signal_body);
            const auto signal_range = load_runtime_range(
                signal_index,
                "pending.range.bound.signal");
            const auto bound_disjoint_from_signal
                = pointer_ranges_disjoint(builder,
                    first_pending_range.pointer, pending_bound_bytes,
                    signal_range.pointer, signal_range.bytes);
            builder.CreateCondBr(bound_disjoint_from_signal,
                pending_bound_signal_advance, full_range_initialize);

            builder.SetInsertPoint(pending_bound_signal_advance);
            const auto previous_signal_index = builder.CreateLoad(i64,
                pending_bound_signal_index,
                "pending.range.bound.signal.previous.index");
            const auto next_signal_index = builder.CreateAdd(
                previous_signal_index,
                expected_i64(builder, 1U), "pending.range.bound.signal.next");
            builder.CreateStore(next_signal_index,
                pending_bound_signal_index);
            auto* const signal_bound_latch = builder.CreateBr(
                pending_bound_signal_header);
            signal_bound_latch->setMetadata(llvm::LLVMContext::MD_loop,
                create_unroll_disabled_loop_id(context));
        } else {
            builder.CreateCondBr(plane_map_ok, full_range_initialize, decline);
        }

        builder.SetInsertPoint(full_range_initialize);
        builder.CreateStore(full_range_count, effective_range_count);
        builder.CreateStore(expected_i64(builder, 0U), outer_index);
        builder.CreateBr(range_outer_header);

        if (compact_range_initialize != nullptr) {
            builder.SetInsertPoint(compact_range_initialize);
            builder.CreateStore(signal_only_range_count,
                effective_range_count);
            builder.CreateStore(expected_i64(builder, 0U), outer_index);
            builder.CreateBr(range_outer_header);
        }

        builder.SetInsertPoint(range_outer_header);
        auto* const outer_value = builder.CreateLoad(i64, outer_index,
            "range.outer.value");
        auto* const active_range_count = builder.CreateLoad(i64,
            effective_range_count, "range.active.count");
        builder.CreateCondBr(builder.CreateICmpULT(outer_value,
                                         active_range_count),
            range_outer_body, validate_plane_tails);

        builder.SetInsertPoint(range_outer_body);
        const auto first_range = load_runtime_range(outer_value,
            "range.first");
        for (std::size_t frame_index = 0U;
             frame_index < frame_ranges.size(); ++frame_index) {
            auto* const next_frame_check
                = frame_index + 1U == frame_ranges.size()
                ? range_inner_initialize
                : llvm::BasicBlock::Create(context,
                    "range.frame.next", function);
            const auto& frame_range = frame_ranges[frame_index];
            const auto disjoint = pointer_ranges_disjoint(builder,
                first_range.pointer, first_range.bytes, frame_range.pointer,
                frame_range.bytes);
            builder.CreateCondBr(disjoint, next_frame_check, decline);
            if (frame_index + 1U != frame_ranges.size()) {
                builder.SetInsertPoint(next_frame_check);
            }
        }
        if (frame_ranges.empty()) {
            builder.CreateBr(range_inner_initialize);
        }

        builder.SetInsertPoint(range_inner_initialize);
        builder.CreateStore(builder.CreateAdd(outer_value,
                                 expected_i64(builder, 1U)),
            inner_index);
        builder.CreateBr(range_inner_header);

        builder.SetInsertPoint(range_inner_header);
        auto* const inner_value = builder.CreateLoad(i64, inner_index,
            "range.inner.value");
        auto* const inner_range_count = builder.CreateLoad(i64,
            effective_range_count, "range.inner.count");
        builder.CreateCondBr(builder.CreateICmpULT(inner_value,
                                         inner_range_count),
            range_inner_body, range_outer_advance);

        builder.SetInsertPoint(range_inner_body);
        const auto second_range = load_runtime_range(inner_value,
            "range.second");
        const auto disjoint = pointer_ranges_disjoint(builder,
            first_range.pointer, first_range.bytes, second_range.pointer,
            second_range.bytes);
        const auto first_alias_key = builder.CreateLShr(
            first_range.alias_tag, expected_i64(builder, 1U));
        const auto second_alias_key = builder.CreateLShr(
            second_range.alias_tag, expected_i64(builder, 1U));
        const auto same_nonzero_alias_key = builder.CreateAnd(
            builder.CreateICmpNE(first_alias_key,
                expected_i64(builder, 0U)),
            builder.CreateICmpEQ(first_alias_key, second_alias_key));
        const auto opposite_alias_roles = builder.CreateICmpNE(
            builder.CreateAnd(first_range.alias_tag,
                expected_i64(builder, 1U)),
            builder.CreateAnd(second_range.alias_tag,
                expected_i64(builder, 1U)));
        const auto stored_owner_alias = builder.CreateAnd(
            same_nonzero_alias_key, opposite_alias_roles);
        const auto same_pointer = builder.CreateICmpEQ(first_range.pointer,
            second_range.pointer);
        const auto same_pointer_alias = builder.CreateAnd(stored_owner_alias,
            same_pointer);
        const auto pair_is_valid = builder.CreateOr(disjoint,
            same_pointer_alias);
        builder.CreateCondBr(pair_is_valid, range_inner_advance, decline);

        builder.SetInsertPoint(range_inner_advance);
        builder.CreateStore(builder.CreateAdd(inner_value,
                                 expected_i64(builder, 1U)),
            inner_index);
        auto* const inner_latch = builder.CreateBr(range_inner_header);
        inner_latch->setMetadata(llvm::LLVMContext::MD_loop,
            create_unroll_disabled_loop_id(context));

        builder.SetInsertPoint(range_outer_advance);
        builder.CreateStore(builder.CreateAdd(outer_value,
                                 expected_i64(builder, 1U)),
            outer_index);
        auto* const outer_latch = builder.CreateBr(range_outer_header);
        outer_latch->setMetadata(llvm::LLVMContext::MD_loop,
            create_unroll_disabled_loop_id(context));
    }
    auto* const initialize_slots = llvm::BasicBlock::Create(
        context, "initialize.pending.slots", function);
    auto* const validate_loop = llvm::BasicBlock::Create(
        context, "validate.loop", function);
    auto* const validate_value_contents = shared_body
        ? llvm::BasicBlock::Create(context, "validate.value.contents", function)
        : validate_plane_tails;
    builder.SetInsertPoint(validate_plane_tails);
    if (shared_body) {
        // This private receipt skips the repeated value-buffer canonicality
        // helper. The descriptor-shape receipt is separate; frame, alias,
        // task, key, and dynamic initial-slot checks still run on every entry.
        builder.CreateCondBr(value_contents_prevalidated, initialize_slots,
            validate_value_contents);
        builder.SetInsertPoint(validate_value_contents);
    }
    auto* const value_contents_preheader = builder.GetInsertBlock();
    llvm::Value* tail_words_ok = llvm::ConstantInt::getTrue(context);
    if (signal_plane_shape_count != 0U) {
        auto* const signal_tail_header = llvm::BasicBlock::Create(context,
            "tail.signal.header", function);
        auto* const signal_tail_body = llvm::BasicBlock::Create(context,
            "tail.signal.body", function);
        auto* const signal_tail_internal_header = llvm::BasicBlock::Create(
            context, "tail.signal.internal.role.header", function);
        auto* const signal_tail_internal_body = llvm::BasicBlock::Create(
            context, "tail.signal.internal.role.body", function);
        auto* const signal_tail_internal_advance = llvm::BasicBlock::Create(
            context, "tail.signal.internal.role.advance", function);
        auto* const signal_tail_internal_done = llvm::BasicBlock::Create(
            context, "tail.signal.internal.role.done", function);
        auto* const signal_tail_boundary = llvm::BasicBlock::Create(context,
            "tail.signal.boundary", function);
        auto* const signal_tail_check_done = llvm::BasicBlock::Create(context,
            "tail.signal.check.done", function);
        auto* const signal_tail_advance = llvm::BasicBlock::Create(context,
            "tail.signal.advance", function);
        auto* const signal_tail_done = llvm::BasicBlock::Create(context,
            "tail.signal.done", function);
        builder.CreateBr(signal_tail_header);

        builder.SetInsertPoint(signal_tail_header);
        auto* const signal_index = builder.CreatePHI(i32, 2U,
            "tail.signal.index");
        auto* const accumulated_signal_valid = builder.CreatePHI(
            llvm::Type::getInt1Ty(context), 2U,
            "tail.signal.valid");
        signal_index->addIncoming(expected_i32(builder, 0U),
            value_contents_preheader);
        accumulated_signal_valid->addIncoming(tail_words_ok,
            value_contents_preheader);
        builder.CreateCondBr(builder.CreateICmpULT(signal_index,
                expected_i32(builder, static_cast<std::uint32_t>(
                    signal_plane_shape_count))),
            signal_tail_body, signal_tail_done);

        builder.SetInsertPoint(signal_tail_body);
        const std::array<llvm::Value*, 2U> signal_shape_indices {
            expected_i64(builder, 0U),
            builder.CreateZExt(signal_index, i64,
                "tail.signal.shape.index"),
        };
        auto* const expected_signal_shape = builder.CreateInBoundsGEP(
            signal_shape_array_type, expected_signal_shapes,
            signal_shape_indices, "tail.expected.signal.shape");
        const auto load_signal_tail_shape = [&](const unsigned field,
                                                 const llvm::Twine& name) {
            auto* const field_pointer = builder.CreateStructGEP(
                signal_shape_record_type, expected_signal_shape, field,
                name + ".field");
            return builder.CreateLoad(i32, field_pointer, name);
        };
        const auto signal_value_kind = load_signal_tail_shape(2U,
            "tail.signal.value.kind");
        const auto signal_width = load_signal_tail_shape(3U,
            "tail.signal.width");
        const auto signal_word_count = load_signal_tail_shape(4U,
            "tail.signal.word.count");
        const auto signal_plane_count = load_signal_tail_shape(5U,
            "tail.signal.plane.count");
        const auto signal_flags = load_signal_tail_shape(6U,
            "tail.signal.flags");
        auto* const signal_plane = indexed_pointer(builder, planes_ptr,
            signal_index, sizeof(RegionFrontierPlaneV2));
        const auto is_internal_signal = builder.CreateICmpNE(
            builder.CreateAnd(signal_flags, expected_i32(builder,
                RegionFrontierPlaneFlagsV2::certified_internal_single_owner)),
            expected_i32(builder, 0U));
        builder.CreateCondBr(is_internal_signal,
            signal_tail_internal_header, signal_tail_boundary);

        builder.SetInsertPoint(signal_tail_internal_header);
        auto* const role_index = builder.CreatePHI(i32, 2U,
            "tail.signal.role.index");
        auto* const accumulated_role_valid = builder.CreatePHI(
            llvm::Type::getInt1Ty(context), 2U,
            "tail.signal.role.valid");
        role_index->addIncoming(expected_i32(builder, 0U), signal_tail_body);
        accumulated_role_valid->addIncoming(accumulated_signal_valid,
            signal_tail_body);
        builder.CreateCondBr(builder.CreateICmpULT(role_index,
                expected_i32(builder, static_cast<std::uint32_t>(
                    role_array_offsets.size()))),
            signal_tail_internal_body, signal_tail_internal_done);

        builder.SetInsertPoint(signal_tail_internal_body);
        llvm::Value* role_offset = expected_i64(builder,
            role_array_offsets[0U]);
        for (std::uint32_t role = 1U;
             role < role_array_offsets.size(); ++role) {
            role_offset = builder.CreateSelect(
                builder.CreateICmpEQ(role_index, expected_i32(builder, role)),
                expected_i64(builder, role_array_offsets[role]), role_offset,
                "tail.signal.role.offset");
        }
        std::array<llvm::Value*, 4U> internal_value_planes { };
        for (std::uint32_t value_plane = 0U;
             value_plane < internal_value_planes.size(); ++value_plane) {
            internal_value_planes[value_plane] = value_role_pointer(builder,
                signal_plane, role_offset, value_plane,
                "tail.mutable.value.plane");
        }
        const auto internal_role_valid = value_buffers_are_canonical(builder,
            internal_value_planes, signal_width, signal_word_count,
            signal_value_kind, signal_plane_count);
        builder.CreateBr(signal_tail_internal_advance);

        builder.SetInsertPoint(signal_tail_internal_advance);
        const auto next_role_index = builder.CreateAdd(role_index,
            expected_i32(builder, 1U), "tail.signal.role.next.index");
        const auto next_role_valid = emit_and(builder,
            accumulated_role_valid, internal_role_valid);
        auto* const role_latch = builder.CreateBr(signal_tail_internal_header);
        role_latch->setMetadata(llvm::LLVMContext::MD_loop,
            create_unroll_disabled_loop_id(context));
        role_index->addIncoming(next_role_index,
            signal_tail_internal_advance);
        accumulated_role_valid->addIncoming(next_role_valid,
            signal_tail_internal_advance);

        builder.SetInsertPoint(signal_tail_boundary);
        std::array<llvm::Value*, 4U> boundary_value_planes { };
        for (std::uint32_t value_plane = 0U;
             value_plane < boundary_value_planes.size(); ++value_plane) {
            boundary_value_planes[value_plane] = load_at(builder,
                signal_plane, pointer_array_element_offset(
                    offsetof(RegionFrontierPlaneV2, boundary_planes),
                    value_plane), i8_pointer,
                "tail.boundary.value.plane");
        }
        const auto boundary_valid = value_buffers_are_canonical(builder,
            boundary_value_planes, signal_width, signal_word_count,
            signal_value_kind, signal_plane_count);
        const auto accumulated_boundary_valid = emit_and(builder,
            accumulated_signal_valid, boundary_valid);
        auto* const boundary_check_predecessor = builder.GetInsertBlock();
        builder.CreateBr(signal_tail_check_done);

        builder.SetInsertPoint(signal_tail_internal_done);
        builder.CreateBr(signal_tail_check_done);

        builder.SetInsertPoint(signal_tail_check_done);
        auto* const signal_check_valid = builder.CreatePHI(
            llvm::Type::getInt1Ty(context), 2U,
            "tail.signal.check.valid");
        signal_check_valid->addIncoming(accumulated_role_valid,
            signal_tail_internal_done);
        signal_check_valid->addIncoming(accumulated_boundary_valid,
            boundary_check_predecessor);
        builder.CreateBr(signal_tail_advance);

        builder.SetInsertPoint(signal_tail_advance);
        const auto next_signal_index = builder.CreateAdd(signal_index,
            expected_i32(builder, 1U), "tail.signal.next.index");
        builder.CreateBr(signal_tail_header);
        signal_index->addIncoming(next_signal_index, signal_tail_advance);
        accumulated_signal_valid->addIncoming(signal_check_valid,
            signal_tail_advance);
        auto* const signal_latch = signal_tail_advance->getTerminator();
        signal_latch->setMetadata(llvm::LLVMContext::MD_loop,
            create_unroll_disabled_loop_id(context));

        builder.SetInsertPoint(signal_tail_done);
        tail_words_ok = accumulated_signal_valid;
    }

    if (pending_write_shape_count != 0U) {
        auto* const pending_tail_header = llvm::BasicBlock::Create(context,
            "tail.pending.header", function);
        auto* const pending_tail_body = llvm::BasicBlock::Create(context,
            "tail.pending.body", function);
        auto* const pending_tail_advance = llvm::BasicBlock::Create(context,
            "tail.pending.advance", function);
        auto* const pending_tail_done = llvm::BasicBlock::Create(context,
            "tail.pending.done", function);
        auto* const pending_tail_preheader = builder.GetInsertBlock();
        builder.CreateBr(pending_tail_header);

        builder.SetInsertPoint(pending_tail_header);
        auto* const pending_index = builder.CreatePHI(i32, 2U,
            "tail.pending.index");
        auto* const accumulated_pending_valid = builder.CreatePHI(
            llvm::Type::getInt1Ty(context), 2U,
            "tail.pending.valid");
        pending_index->addIncoming(expected_i32(builder, 0U),
            pending_tail_preheader);
        accumulated_pending_valid->addIncoming(tail_words_ok,
            pending_tail_preheader);
        builder.CreateCondBr(builder.CreateICmpULT(pending_index,
                expected_i32(builder, static_cast<std::uint32_t>(
                    pending_write_shape_count))),
            pending_tail_body, pending_tail_done);

        builder.SetInsertPoint(pending_tail_body);
        const std::array<llvm::Value*, 2U> pending_shape_indices {
            expected_i64(builder, 0U),
            builder.CreateZExt(pending_index, i64,
                "tail.pending.shape.index"),
        };
        auto* const expected_write_shape = builder.CreateInBoundsGEP(
            expected_write_shape_array_type, expected_write_shapes,
            pending_shape_indices, "tail.expected.pending.shape");
        const auto load_pending_tail_shape = [&](const unsigned field,
                                                  const llvm::Twine& name) {
            auto* const field_pointer = builder.CreateStructGEP(
                expected_write_shape_type, expected_write_shape, field,
                name + ".field");
            return builder.CreateLoad(i32, field_pointer, name);
        };
        const auto expected_pending_slot = load_pending_tail_shape(0U,
            "tail.pending.slot");
        const auto pending_value_kind = load_pending_tail_shape(1U,
            "tail.pending.value.kind");
        const auto pending_width = load_pending_tail_shape(2U,
            "tail.pending.width");
        const auto pending_word_count = load_pending_tail_shape(3U,
            "tail.pending.word.count");
        const auto pending_plane_count = load_pending_tail_shape(4U,
            "tail.pending.plane.count");
        auto* const write = indexed_pointer(builder, writes_ptr,
            expected_pending_slot, sizeof(RegionFrontierPendingWriteV2));
        std::array<llvm::Value*, 4U> pending_value_planes { };
        for (std::uint32_t value_plane = 0U;
             value_plane < pending_value_planes.size(); ++value_plane) {
            pending_value_planes[value_plane] = pending_value_pointer(builder,
                write, value_plane, "tail.pending.value.plane");
        }
        const auto pending_valid = value_buffers_are_canonical(builder,
            pending_value_planes, pending_width, pending_word_count,
            pending_value_kind, pending_plane_count);
        builder.CreateBr(pending_tail_advance);

        builder.SetInsertPoint(pending_tail_advance);
        const auto next_pending_index = builder.CreateAdd(pending_index,
            expected_i32(builder, 1U), "tail.pending.next.index");
        const auto next_pending_valid = emit_and(builder,
            accumulated_pending_valid, pending_valid);
        auto* const pending_latch = builder.CreateBr(pending_tail_header);
        pending_latch->setMetadata(llvm::LLVMContext::MD_loop,
            create_unroll_disabled_loop_id(context));
        pending_index->addIncoming(next_pending_index,
            pending_tail_advance);
        accumulated_pending_valid->addIncoming(next_pending_valid,
            pending_tail_advance);

        builder.SetInsertPoint(pending_tail_done);
        tail_words_ok = accumulated_pending_valid;
    }
    builder.CreateCondBr(tail_words_ok, initialize_slots, decline);
    builder.SetInsertPoint(initialize_slots);
    auto* const initial_slot_done = llvm::BasicBlock::Create(context,
        "initialize.pending.slots.done", function);
    if (layout.write_site_count != 0U) {
        auto* const helper_pointer = llvm::PointerType::getUnqual(context);
        auto* const helper_type = llvm::FunctionType::get(i32,
            { helper_pointer, helper_pointer, i32, helper_pointer,
                helper_pointer }, false);
        auto helper = module.getOrInsertFunction(
            fsim::compiler::llvm_detail::kInitialSlotValidationHelperSymbol,
            helper_type);
        llvm::cast<llvm::Function>(helper.getCallee())->addFnAttr(
            llvm::Attribute::NoUnwind);
        auto* const expected_shapes_pointer = builder.CreateBitCast(
            expected_write_shapes, helper_pointer);
        auto* const helper_valid = builder.CreateCall(helper,
            { frame, expected_shapes_pointer,
                expected_i32(builder, layout.write_site_count),
                slot_states, live_pending_count },
            "initial.slot.validation.valid");
        helper_valid->setDoesNotThrow();
        builder.CreateCondBr(emit_equal(builder, helper_valid,
                expected_i32(builder, 1U)), initial_slot_done, decline);
    } else {
        builder.CreateBr(initial_slot_done);
    }
    builder.SetInsertPoint(initial_slot_done);
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
        scan_index, offsetof(RegionFrontierSchedulerTaskV2, payload),
        "validation.payload");
    const auto validation_kind = task_payload_kind(builder,
        validation_task_payload);
    const auto task_stable = load_task_field(builder, tasks_ptr, scan_index,
        offsetof(RegionFrontierSchedulerTaskV2, stable_order), "task.stable");
    const auto task_sequence = load_task_field(builder, tasks_ptr, scan_index,
        offsetof(RegionFrontierSchedulerTaskV2, sequence), "task.sequence");
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
            offsetof(RegionFrontierFrameV2, cut)
                + offsetof(RegionFrontierCutV2, next_key)
                + offsetof(RegionFrontierKeyV2, stable_order), i64,
            "preflight.cut.stable"),
        load_frame(builder, frame,
            offsetof(RegionFrontierFrameV2, cut)
                + offsetof(RegionFrontierCutV2, next_key)
                + offsetof(RegionFrontierKeyV2, sequence), i64,
            "preflight.cut.sequence"));
    const auto contributes_to_prefix = builder.CreateOr(
        builder.CreateNot(same_slot_cut), task_before_cut);
    auto* const validation_switch = builder.CreateSwitch(validation_kind,
        decline, generic_mode ? 1U : 3U);
    validation_switch->addCase(llvm::ConstantInt::get(i8,
            static_cast<std::uint8_t>(RegionFrontierEventKindV2::member_activation)),
        validate_activation);
    if (!generic_mode) {
        validation_switch->addCase(llvm::ConstantInt::get(i8,
                static_cast<std::uint8_t>(RegionFrontierEventKindV2::internal_commit)),
            validate_write);
        validation_switch->addCase(llvm::ConstantInt::get(i8,
                static_cast<std::uint8_t>(RegionFrontierEventKindV2::boundary_commit)),
            validate_boundary_ack);
    }

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
        activation_index, offsetof(RegionFrontierMemberV2, flags), i32,
        "activation.flags");
    auto* activation_ok = emit_all_flags_set(builder, activation_flags,
        RegionFrontierMemberFlagsV2::queued
            | RegionFrontierMemberFlagsV2::queued_key_valid);
    activation_ok = emit_and(builder, activation_ok,
        builder.CreateNot(emit_flag_set(builder, activation_flags,
            RegionFrontierMemberFlagsV2::executing)));
    auto* const queued_key_pointer = constant_offset(builder,
        indexed_pointer(builder, members_ptr, activation_index,
            sizeof(RegionFrontierMemberV2)),
        offsetof(RegionFrontierMemberV2, queued_key));
    activation_ok = emit_and(builder, activation_ok,
        emit_slot_matches_key(builder, frame, queued_key_pointer));
    const auto queued_stable = load_at(builder, queued_key_pointer,
        offsetof(RegionFrontierKeyV2, stable_order), i64, "queued.stable");
    const auto queued_sequence = load_at(builder, queued_key_pointer,
        offsetof(RegionFrontierKeyV2, sequence), i64, "queued.sequence");
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
    llvm::Value* member_write_bound = select_member_table_field(builder,
        activation_index, max_member_write_counts, 0U);
    llvm::Value* member_event_bound = select_member_table_field(builder,
        activation_index, max_member_staged_event_counts, 0U);
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
    const auto emit_selected_member_site_loop = [&](
        llvm::BasicBlock* const done, const bool mark_slots) {
        if (layout.write_site_count == 0U) {
            builder.CreateBr(done);
            builder.SetInsertPoint(done);
            return;
        }
        auto* const loop_entry = builder.GetInsertBlock();
        const auto first_site = select_member_table_field(builder,
            activation_index, member_first_write_sites, 0U);
        const auto site_count = select_member_table_field(builder,
            activation_index, member_write_site_counts, 0U);
        auto* const header = llvm::BasicBlock::Create(context,
            mark_slots ? "activation.site.mark.header"
                       : "activation.site.check.header", function);
        auto* const body = llvm::BasicBlock::Create(context,
            "activation.selected.site", function);
        auto* const advance = llvm::BasicBlock::Create(context,
            "activation.selected.site.next", function);
        builder.CreateBr(header);
        builder.SetInsertPoint(header);
        auto* const cursor = builder.CreatePHI(i32, 2U,
            "activation.member.site.cursor");
        cursor->addIncoming(expected_i32(builder, 0U), loop_entry);
        builder.CreateCondBr(builder.CreateICmpULT(cursor, site_count),
            body, done);
        builder.SetInsertPoint(body);
        const auto site_index = builder.CreateAdd(first_site, cursor);
        const std::array<llvm::Value*, 2U> indices {
            expected_i64(builder, 0U), builder.CreateZExt(site_index, i64),
        };
        auto* const site = builder.CreateInBoundsGEP(
            expected_write_shape_array_type, expected_write_shapes, indices,
            "activation.expected.site");
        auto* const pending_slot_address = builder.CreateStructGEP(
            expected_write_shape_type, site, 0U);
        const auto pending_slot = builder.CreateLoad(i32, pending_slot_address,
            "activation.selected.pending.slot");
        auto* const state_address = builder.CreateInBoundsGEP(i8, slot_states,
            builder.CreateZExt(pending_slot, i64));
        const auto state = builder.CreateLoad(i8, state_address,
            "activation.selected.slot.state");
        if (mark_slots) {
            const auto new_state = builder.CreateSelect(contributes_to_prefix,
                llvm::ConstantInt::get(i8, 1U), state);
            builder.CreateStore(new_state, state_address);
            builder.CreateBr(advance);
        } else {
            const auto free = emit_equal(builder, state,
                llvm::ConstantInt::get(i8, 0U));
            builder.CreateCondBr(builder.CreateOr(
                    builder.CreateNot(contributes_to_prefix), free),
                advance, decline);
        }
        builder.SetInsertPoint(advance);
        const auto next = builder.CreateAdd(cursor, expected_i32(builder, 1U));
        auto* const latch = builder.CreateBr(header);
        latch->setMetadata(llvm::LLVMContext::MD_loop,
            create_unroll_disabled_loop_id(context));
        cursor->addIncoming(next, advance);
        builder.SetInsertPoint(done);
    };
    builder.SetInsertPoint(activation_site_capacity);
    if (contiguous_member_sites) {
        // Complete the read-only pass before any slot is marked occupied.
        emit_selected_member_site_loop(activation_capacity_valid, false);
    } else {
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
    }
    builder.SetInsertPoint(activation_capacity_valid);
    builder.CreateStore(pending_after_member_bound, live_pending_count);
    builder.CreateStore(events_after_member_bound, live_event_count);
    if (contiguous_member_sites) {
        auto* const marked = llvm::BasicBlock::Create(context,
            "activation.member.sites.marked", function);
        emit_selected_member_site_loop(marked, true);
    } else {
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
        offsetof(RegionFrontierPendingWriteV2, flags), i32, "write.flags");
    const auto expected_member_index = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.member_index;
        }, UINT32_MAX);
    const auto expected_signal_slot = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.signal_slot;
        }, UINT32_MAX);
    const auto expected_source_instruction = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.source_instruction;
        }, UINT32_MAX);
    const auto expected_update_kind = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.update_kind;
        }, UINT32_MAX);
    const auto expected_event_kind = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.event_kind;
        }, UINT32_MAX);
    const auto expected_width = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.width;
        }, UINT32_MAX);
    const auto expected_word_count = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.word_count;
        }, UINT32_MAX);
    const auto expected_value_kind = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return static_cast<std::uint32_t>(site.value_kind);
        }, UINT32_MAX);
    const auto expected_plane_count = select_write_site_field(builder,
        write_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.plane_count;
        }, UINT32_MAX);
    auto* write_ok = emit_equal(builder, write_flags,
        llvm::ConstantInt::get(i32,
            RegionFrontierPendingWriteFlagsV2::pending_active
                | RegionFrontierPendingWriteFlagsV2::pending_value_ready
                | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
                | RegionFrontierPendingWriteFlagsV2::pending_internal_target));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        expected_event_kind,
        expected_i32(builder, static_cast<std::uint32_t>(
                                  RegionFrontierEventKindV2::internal_commit))));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV2, member_index), i32,
            "write.member.index"), expected_member_index));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV2, signal_slot), i32,
            "write.signal.slot"), expected_signal_slot));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV2, source_instruction), i32,
            "write.source.instruction"), expected_source_instruction));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV2, update_kind), i32,
            "write.update.kind"), expected_update_kind));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV2, value_kind), i32,
            "write.value.kind"), expected_value_kind));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV2, width), i32,
            "write.width"), expected_width));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV2, word_count), i32,
            "write.word.count"), expected_word_count));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        write_field(builder, writes_ptr, write_index,
            offsetof(RegionFrontierPendingWriteV2, plane_count), i32,
            "write.plane.count"), expected_plane_count));
    auto* const slot_state_pointer = builder.CreateInBoundsGEP(i8,
        slot_states, builder.CreateZExt(write_index, i64));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        builder.CreateLoad(i8, slot_state_pointer, "write.slot.state"),
        llvm::ConstantInt::get(i8, 1U)));
    const auto write_key_pointer = constant_offset(builder,
        indexed_pointer(builder, writes_ptr, write_index,
            sizeof(RegionFrontierPendingWriteV2)),
        offsetof(RegionFrontierPendingWriteV2, commit_key));
    write_ok = emit_and(builder, write_ok,
        emit_slot_matches_key(builder, frame, write_key_pointer));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        load_at(builder, write_key_pointer,
            offsetof(RegionFrontierKeyV2, stable_order), i64, "write.stable"),
        task_stable));
    write_ok = emit_and(builder, write_ok, emit_equal(builder,
        load_at(builder, write_key_pointer,
            offsetof(RegionFrontierKeyV2, sequence), i64, "write.sequence"),
        task_sequence));
    const auto signal_slot = expected_signal_slot;
    write_ok = emit_and(builder, write_ok,
        builder.CreateICmpULT(signal_slot, signal_slot_count));
    builder.CreateCondBr(write_ok, validate_write_plane, decline);

    // Do not form or dereference a plane GEP until the slot range is proven.
    // This keeps malformed caller frames on the read-only decline path.
    builder.SetInsertPoint(validate_write_plane);
    const auto planes_array = builder.CreateBitCast(planes_ptr,
        llvm::PointerType::getUnqual(builder.getContext()));
    const auto plane_pointer = indexed_pointer(builder, planes_array,
        signal_slot, sizeof(RegionFrontierPlaneV2));
    const auto plane_flags = load_at(builder, plane_pointer,
        offsetof(RegionFrontierPlaneV2, flags), i32, "plane.flags");
    write_ok = emit_and(builder, write_ok,
        emit_flag_set(builder, plane_flags,
            RegionFrontierPlaneFlagsV2::certified_internal_single_owner));
    const auto metadata_index = load_at(builder, plane_pointer,
        offsetof(RegionFrontierPlaneV2, metadata_index), i32,
        "plane.metadata.index");
    write_ok = emit_and(builder, write_ok,
        builder.CreateICmpULT(metadata_index, metadata_count));
    const auto width = write_field(builder, writes_ptr, write_index,
        offsetof(RegionFrontierPendingWriteV2, width), i32, "write.width");
    const auto plane_width = load_at(builder, plane_pointer,
        offsetof(RegionFrontierPlaneV2, width), i32, "plane.width");
    write_ok = emit_and(builder, write_ok, emit_equal(builder, width, plane_width));
    const auto plane_kind = load_at(builder, plane_pointer,
        offsetof(RegionFrontierPlaneV2, value_kind), i32, "plane.value.kind");
    write_ok = emit_and(builder, write_ok,
        emit_equal(builder, plane_kind, expected_value_kind));
    const auto plane_planes = load_at(builder, plane_pointer,
        offsetof(RegionFrontierPlaneV2, plane_count), i32,
        "plane.value.plane.count");
    write_ok = emit_and(builder, write_ok,
        emit_equal(builder, plane_planes, expected_plane_count));
    for (std::uint32_t value_plane = 0U; value_plane < 4U; ++value_plane) {
        const auto write_value_plane = pending_value_pointer(builder,
            indexed_pointer(builder, writes_ptr, write_index,
                sizeof(RegionFrontierPendingWriteV2)),
            value_plane, "write.value.plane");
        const auto plane_is_present = builder.CreateICmpULT(
            expected_i32(builder, value_plane), expected_plane_count);
        const auto pointer_shape_ok = builder.CreateSelect(plane_is_present,
            builder.CreateIsNotNull(write_value_plane),
            builder.CreateIsNull(write_value_plane));
        write_ok = emit_and(builder, write_ok, pointer_shape_ok);
    }
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
        offsetof(RegionFrontierPendingWriteV2, flags), i32, "boundary.flags");
    const auto expected_boundary_member = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.member_index;
        }, UINT32_MAX);
    const auto expected_boundary_signal = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.signal_slot;
        }, UINT32_MAX);
    const auto expected_boundary_instruction = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.source_instruction;
        }, UINT32_MAX);
    const auto expected_boundary_update_kind = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.update_kind;
        }, UINT32_MAX);
    const auto expected_boundary_event = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.event_kind;
        }, UINT32_MAX);
    const auto expected_boundary_width = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.width;
        }, UINT32_MAX);
    const auto expected_boundary_words = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.word_count;
        }, UINT32_MAX);
    const auto expected_boundary_value_kind = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return static_cast<std::uint32_t>(site.value_kind);
        }, UINT32_MAX);
    const auto expected_boundary_plane_count = select_write_site_field(builder,
        boundary_index, layout,
        [](const RegionFrontierWriteSiteV2& site) {
            return site.plane_count;
        }, UINT32_MAX);
    auto* boundary_ok = emit_all_flags_set(builder, boundary_flags,
        RegionFrontierPendingWriteFlagsV2::pending_active
            | RegionFrontierPendingWriteFlagsV2::pending_value_ready
            | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
            | RegionFrontierPendingWriteFlagsV2::pending_boundary_target);
    boundary_ok = emit_and(builder, boundary_ok,
        emit_equal(builder, expected_boundary_event,
            expected_i32(builder, static_cast<std::uint32_t>(
                                      RegionFrontierEventKindV2::boundary_commit))));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV2, member_index), i32,
            "boundary.member.index"), expected_boundary_member));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV2, signal_slot), i32,
            "boundary.signal.slot"), expected_boundary_signal));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV2, source_instruction), i32,
            "boundary.source.instruction"), expected_boundary_instruction));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV2, update_kind), i32,
            "boundary.update.kind"), expected_boundary_update_kind));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV2, value_kind), i32,
            "boundary.value.kind"), expected_boundary_value_kind));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV2, width), i32,
            "boundary.width"), expected_boundary_width));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV2, word_count), i32,
            "boundary.word.count"), expected_boundary_words));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        write_field(builder, writes_ptr, boundary_index,
            offsetof(RegionFrontierPendingWriteV2, plane_count), i32,
            "boundary.plane.count"), expected_boundary_plane_count));
    auto* const boundary_state_pointer = builder.CreateInBoundsGEP(i8,
        slot_states, builder.CreateZExt(boundary_index, i64));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        builder.CreateLoad(i8, boundary_state_pointer,
            "boundary.slot.state"), llvm::ConstantInt::get(i8, 1U)));
    constexpr auto boundary_known_flags
        = RegionFrontierPendingWriteFlagsV2::pending_active
        | RegionFrontierPendingWriteFlagsV2::pending_value_ready
        | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
        | RegionFrontierPendingWriteFlagsV2::pending_internal_target
        | RegionFrontierPendingWriteFlagsV2::pending_boundary_target
        | RegionFrontierPendingWriteFlagsV2::pending_committed;
    boundary_ok = emit_and(builder, boundary_ok,
        builder.CreateICmpEQ(builder.CreateAnd(boundary_flags,
                                  expected_i32(builder,
                                      ~static_cast<std::uint32_t>(boundary_known_flags))),
            expected_i32(builder, 0U)));
    boundary_ok = emit_and(builder, boundary_ok,
        builder.CreateICmpEQ(builder.CreateAnd(boundary_flags,
                                  expected_i32(builder,
                                      RegionFrontierPendingWriteFlagsV2::pending_internal_target)),
            expected_i32(builder, 0U)));
    const auto boundary_key_pointer = constant_offset(builder,
        indexed_pointer(builder, writes_ptr, boundary_index,
            sizeof(RegionFrontierPendingWriteV2)),
        offsetof(RegionFrontierPendingWriteV2, commit_key));
    boundary_ok = emit_and(builder, boundary_ok,
        emit_slot_matches_key(builder, frame, boundary_key_pointer));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        load_at(builder, boundary_key_pointer,
            offsetof(RegionFrontierKeyV2, stable_order), i64,
            "boundary.stable"), task_stable));
    boundary_ok = emit_and(builder, boundary_ok, emit_equal(builder,
        load_at(builder, boundary_key_pointer,
            offsetof(RegionFrontierKeyV2, sequence), i64,
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
        offsetof(RegionFrontierFrameV2, stop_requested), "stop.requested.ptr");
    auto* const check_stop = llvm::BasicBlock::Create(
        context, "check.stop", function);
    builder.CreateBr(check_stop);
    builder.SetInsertPoint(check_stop);
    const auto current_cursor = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, scheduler_task_cursor), i32,
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
        offsetof(RegionFrontierFrameV2, staged_event_count), i32,
        "staged.remaining");
    if (generic_mode) {
        auto* const generic_batch_ready = llvm::BasicBlock::Create(
            context, "generic.update.batch.ready", function);
        builder.CreateCondBr(builder.CreateICmpNE(staged_remaining,
                expected_i32(builder, 0U)), generic_batch_ready, quiescent);
        builder.SetInsertPoint(generic_batch_ready);
        return_status(builder,
            RegionFrontierStatusV2::generic_update_batch_ready);
    } else {
        builder.CreateCondBr(builder.CreateICmpNE(staged_remaining,
                expected_i32(builder, 0U)), need_keys, quiescent);
    }
    builder.SetInsertPoint(quiescent);
    return_status(builder, RegionFrontierStatusV2::quiescent);

    builder.SetInsertPoint(have_task);
    const auto task_payload = load_task_field(builder, tasks_ptr, current_cursor,
        offsetof(RegionFrontierSchedulerTaskV2, payload), "task.payload");
    const auto task_kind = task_payload_kind(builder, task_payload);
    const auto event_index = task_payload_index(builder, task_payload);
    const auto current_stable = load_task_field(builder, tasks_ptr, current_cursor,
        offsetof(RegionFrontierSchedulerTaskV2, stable_order), "current.stable");
    const auto current_sequence = load_task_field(builder, tasks_ptr, current_cursor,
        offsetof(RegionFrontierSchedulerTaskV2, sequence), "current.sequence");
    builder.CreateCondBr(same_slot_cut, cut_check, dispatch_ready);

    builder.SetInsertPoint(cut_check);
    const auto cut_stable = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, cut)
            + offsetof(RegionFrontierCutV2, next_key)
            + offsetof(RegionFrontierKeyV2, stable_order), i64, "cut.stable");
    const auto cut_sequence = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, cut)
            + offsetof(RegionFrontierCutV2, next_key)
            + offsetof(RegionFrontierKeyV2, sequence), i64, "cut.sequence");
    const auto before_cut = emit_key_before(builder, current_stable,
        current_sequence, cut_stable, cut_sequence);
    builder.CreateCondBr(before_cut, dispatch_ready, cut_yield);

    builder.SetInsertPoint(cut_yield);
    const auto events_before_cut = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_event_count), i32,
        "events.before.cut");
    auto* const cut_status_block = llvm::BasicBlock::Create(
        context, "cut.status", function);
    llvm::BasicBlock* const generic_cut_batch_ready = generic_mode
        ? llvm::BasicBlock::Create(context,
              "generic.cut.batch.ready", function)
        : nullptr;
    builder.CreateCondBr(builder.CreateICmpNE(events_before_cut,
            expected_i32(builder, 0U)),
        generic_mode ? generic_cut_batch_ready : need_keys,
        cut_status_block);
    builder.SetInsertPoint(cut_status_block);
    return_status(builder, RegionFrontierStatusV2::cut_before_key);
    if (generic_mode) {
        builder.SetInsertPoint(generic_cut_batch_ready);
        return_status(builder,
            RegionFrontierStatusV2::generic_update_batch_ready);
    }

    builder.SetInsertPoint(dispatch_ready);
    auto* const event_switch = builder.CreateSwitch(task_kind,
        dispatch_invalid, generic_mode ? 1U : 3U);
    event_switch->addCase(llvm::ConstantInt::get(i8,
            static_cast<std::uint8_t>(RegionFrontierEventKindV2::member_activation)),
        dispatch_activation);
    if (!generic_mode) {
        event_switch->addCase(llvm::ConstantInt::get(i8,
                static_cast<std::uint8_t>(RegionFrontierEventKindV2::internal_commit)),
            dispatch_internal);
        event_switch->addCase(llvm::ConstantInt::get(i8,
                static_cast<std::uint8_t>(RegionFrontierEventKindV2::boundary_commit)),
            dispatch_boundary);
    }

    builder.SetInsertPoint(capacity_yield);
    const auto events_at_capacity = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_event_count), i32,
        "events.at.capacity");
    auto* const capacity_status = llvm::BasicBlock::Create(
        context, "capacity.status", function);
    llvm::BasicBlock* const generic_capacity_batch_ready = generic_mode
        ? llvm::BasicBlock::Create(context,
              "generic.capacity.batch.ready", function)
        : nullptr;
    builder.CreateCondBr(builder.CreateICmpNE(events_at_capacity,
            expected_i32(builder, 0U)),
        generic_mode ? generic_capacity_batch_ready : need_keys,
        capacity_status);
    builder.SetInsertPoint(capacity_status);
    return_status(builder, RegionFrontierStatusV2::yield_before_task);
    if (generic_mode) {
        builder.SetInsertPoint(generic_capacity_batch_ready);
        return_status(builder,
            RegionFrontierStatusV2::generic_update_batch_ready);
    }

    builder.SetInsertPoint(dispatch_activation);
    const auto pending_now = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_write_count), i32,
        "pending.now");
    const auto pending_room = builder.CreateSub(pending_capacity, pending_now);
    const auto runtime_member_write_bound = select_member_table_field(builder,
        event_index, max_member_write_counts, 0U);
    const auto runtime_member_event_bound = select_member_table_field(builder,
        event_index, max_member_staged_event_counts, 0U);
    const auto pending_bound_ok = builder.CreateICmpUGE(pending_room,
        runtime_member_write_bound);
    const auto events_now = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_event_count), i32,
        "events.now");
    const auto events_room = builder.CreateSub(event_capacity, events_now);
    const auto event_bound_ok = builder.CreateICmpUGE(events_room,
        runtime_member_event_bound);
    builder.CreateCondBr(builder.CreateAnd(pending_bound_ok, event_bound_ok),
        capacity_ok, capacity_yield);

    builder.SetInsertPoint(capacity_ok);
    const auto member_index_valid = builder.CreateICmpULT(event_index,
        expected_i32(builder, static_cast<std::uint32_t>(member_count)),
        "member.index.valid");
    builder.CreateCondBr(member_index_valid, member_preamble,
        dispatch_invalid);

    builder.SetInsertPoint(member_preamble);
    auto* const member_pointer = indexed_pointer(builder, members_ptr,
        event_index, sizeof(RegionFrontierMemberV2));
    const auto member_flags = member_field(builder, members_ptr, event_index,
        offsetof(RegionFrontierMemberV2, flags), i32, "member.flags");
    const auto has_pending_origin = emit_flag_set(builder, member_flags,
        RegionFrontierMemberFlagsV2::pending_activation);
    const auto consumed_queued_key_pointer = constant_offset(builder,
        member_pointer, offsetof(RegionFrontierMemberV2, queued_key));
    const auto pending_origin_pointer = constant_offset(builder,
        member_pointer,
        offsetof(RegionFrontierMemberV2, pending_activation_origin));
    const auto activation_origin_pointer = constant_offset(builder,
        member_pointer, offsetof(RegionFrontierMemberV2, activation_origin));
    constexpr std::size_t u64_key_fields[] {
        offsetof(RegionFrontierKeyV2, time),
        offsetof(RegionFrontierKeyV2, delta),
        offsetof(RegionFrontierKeyV2, systemverilog_round),
        offsetof(RegionFrontierKeyV2, stable_order),
        offsetof(RegionFrontierKeyV2, sequence),
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
        offsetof(RegionFrontierKeyV2, process_domain),
        offsetof(RegionFrontierKeyV2, phase),
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
        offsetof(RegionFrontierMemberV2, static_trigger_mask),
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
            RegionFrontierMemberFlagsV2::queued
                | RegionFrontierMemberFlagsV2::queued_key_valid
                | RegionFrontierMemberFlagsV2::pending_activation
                | RegionFrontierMemberFlagsV2::waiting_on_static)));
    new_flags = builder.CreateOr(new_flags,
        expected_i32(builder, RegionFrontierMemberFlagsV2::executing));
    store_array_field(builder, members_ptr, event_index,
        sizeof(RegionFrontierMemberV2),
        offsetof(RegionFrontierMemberV2, flags), new_flags);
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, current_member), event_index);
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, saved_body_pc), event_index);
    builder.CreateBr(member_dispatch);

    builder.SetInsertPoint(member_dispatch);
    // The range guard dominates this switch and the cases cover
    // [0, member_count), so this default cannot run after the preamble stores.
    const auto member_switch = builder.CreateSwitch(event_index,
        impossible_member_dispatch, static_cast<unsigned>(member_count));
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
        emit_member(builder, index, frame, physical_binding);
        assert(builder.GetInsertBlock()->getTerminator() == nullptr);
        builder.CreateBr(member_done);
    }

    builder.SetInsertPoint(impossible_member_dispatch);
    builder.CreateUnreachable();

    builder.SetInsertPoint(member_done);
    const auto completed_member = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, current_member), i32,
        "completed.member");
    const auto completed_flags = member_field(builder, members_ptr,
        completed_member, offsetof(RegionFrontierMemberV2, flags), i32,
        "completed.flags");
    const auto nonexecuting_flags = builder.CreateAnd(completed_flags,
        expected_i32(builder,
            ~static_cast<std::uint32_t>(RegionFrontierMemberFlagsV2::executing)));
    const auto waiting_flags = builder.CreateOr(nonexecuting_flags,
        expected_i32(builder, RegionFrontierMemberFlagsV2::waiting_on_static));
    store_array_field(builder, members_ptr, completed_member,
        sizeof(RegionFrontierMemberV2),
        offsetof(RegionFrontierMemberV2, flags), waiting_flags);
    store_frame(builder, frame, offsetof(RegionFrontierFrameV2, current_member),
        expected_i32(builder, UINT32_MAX));
    const auto profile_pointer = load_frame_pointer(builder, frame,
        offsetof(RegionFrontierFrameV2,
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
        offsetof(RegionFrontierFrameV2, scheduler_task_cursor),
        next_member_cursor);
    builder.CreateBr(dispatch_loop);

    builder.SetInsertPoint(dispatch_internal);
    const auto commit_events_now = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, staged_event_count), i32,
        "commit.events.now");
    const auto commit_event_room = builder.CreateSub(event_capacity,
        commit_events_now);
    builder.CreateCondBr(builder.CreateICmpUGE(commit_event_room,
            expected_i32(builder, max_commit_fanout_events)),
        internal_done, capacity_yield);
    builder.SetInsertPoint(internal_done);
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, current_pending_write), event_index);
    emit_internal_commit(builder, event_index, frame);
    assert(builder.GetInsertBlock()->getTerminator() == nullptr);
    const auto commit_flags = write_field(builder, writes_ptr, event_index,
        offsetof(RegionFrontierPendingWriteV2, flags), i32,
        "committed.write.flags");
    const auto inactive_flags = builder.CreateAnd(commit_flags,
        expected_i32(builder,
            ~static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_active)));
    const auto retired_flags = builder.CreateOr(inactive_flags,
        expected_i32(builder,
            RegionFrontierPendingWriteFlagsV2::pending_committed));
    store_array_field(builder, writes_ptr, event_index,
        sizeof(RegionFrontierPendingWriteV2),
        offsetof(RegionFrontierPendingWriteV2, flags), retired_flags);
    const auto pending_after_commit = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_write_count), i32,
        "pending.after.commit");
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_write_count),
        builder.CreateSub(pending_after_commit, expected_i32(builder, 1U)));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, current_pending_write),
        expected_i32(builder, UINT32_MAX));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, scheduler_task_cursor),
        builder.CreateAdd(current_cursor, expected_i32(builder, 1U)));
    builder.CreateBr(dispatch_loop);

    builder.SetInsertPoint(dispatch_boundary);
    const auto boundary_flags_now = write_field(builder, writes_ptr, event_index,
        offsetof(RegionFrontierPendingWriteV2, flags), i32,
        "boundary.flags.now");
    builder.CreateCondBr(emit_flag_set(builder, boundary_flags_now,
            RegionFrontierPendingWriteFlagsV2::pending_committed),
        boundary_ack, boundary_publication);
    builder.SetInsertPoint(boundary_publication);
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, current_pending_write), event_index);
    return_status(builder, RegionFrontierStatusV2::boundary_publication);

    builder.SetInsertPoint(boundary_ack);
    const auto boundary_retired_flags = builder.CreateAnd(boundary_flags_now,
        expected_i32(builder,
            ~static_cast<std::uint32_t>(RegionFrontierPendingWriteFlagsV2::pending_active)));
    store_array_field(builder, writes_ptr, event_index,
        sizeof(RegionFrontierPendingWriteV2),
        offsetof(RegionFrontierPendingWriteV2, flags), boundary_retired_flags);
    const auto pending_after_boundary_dispatch = load_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_write_count), i32,
        "pending.after.boundary");
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, pending_write_count),
        builder.CreateSub(pending_after_boundary_dispatch,
            expected_i32(builder, 1U)));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, current_pending_write),
        expected_i32(builder, UINT32_MAX));
    store_frame(builder, frame,
        offsetof(RegionFrontierFrameV2, scheduler_task_cursor),
        builder.CreateAdd(current_cursor, expected_i32(builder, 1U)));
    builder.CreateBr(dispatch_loop);

    builder.SetInsertPoint(dispatch_invalid);
    return_status(builder, RegionFrontierStatusV2::decline_before_mutation);
    builder.SetInsertPoint(need_keys);
    return_status(builder, RegionFrontierStatusV2::need_scheduler_keys);
    builder.SetInsertPoint(stopped);
    if (generic_mode) {
        // A stop can arrive between two member bodies. Generic writes from the
        // already-consumed prefix must still reach the ordinary Update queue;
        // report them for host staging before the scheduler propagates stop.
        const auto stopped_event_count = load_frame(builder, frame,
            offsetof(RegionFrontierFrameV2, staged_event_count), i32,
            "stopped.staged.event.count");
        auto* const generic_stopped_ready = llvm::BasicBlock::Create(
            context, "generic.stopped.batch.ready", function);
        auto* const generic_stopped_empty = llvm::BasicBlock::Create(
            context, "generic.stopped.empty", function);
        builder.CreateCondBr(builder.CreateICmpNE(stopped_event_count,
                expected_i32(builder, 0U)),
            generic_stopped_ready, generic_stopped_empty);
        builder.SetInsertPoint(generic_stopped_ready);
        return_status(builder,
            RegionFrontierStatusV2::generic_update_batch_ready);
        builder.SetInsertPoint(generic_stopped_empty);
        return_status(builder, RegionFrontierStatusV2::stopped);
    } else {
        return_status(builder, RegionFrontierStatusV2::stopped);
    }
    builder.SetInsertPoint(decline);
    return_status(builder, RegionFrontierStatusV2::decline_before_mutation);

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

llvm::Function* emit_region_frontier_loop_v2(llvm::Module& module,
    const std::string& symbol,
    const RegionFrontierLayoutV2& layout,
    const EmitCertifiedMemberBodyV2& emit_member,
    const EmitCertifiedInternalCommitV2& emit_internal_commit)
{
    if (!emit_member) {
        throw std::invalid_argument {
            "missing region frontier member emitter"
        };
    }
    const EmitBoundCertifiedMemberBodyV2 bound_member
        = [&emit_member](llvm::IRBuilder<>& builder,
              const std::size_t member_index, llvm::Value* frame,
              llvm::Value*) {
              emit_member(builder, member_index, frame);
          };
    return emit_region_frontier_loop_impl_v2(module, symbol, layout,
        bound_member, emit_internal_commit, false);
}

llvm::Function* emit_region_frontier_shared_body_v2(llvm::Module& module,
    const std::string& body_symbol,
    const RegionFrontierLayoutV2& structural_layout,
    const EmitBoundCertifiedMemberBodyV2& emit_member,
    const EmitCertifiedInternalCommitV2& emit_internal_commit)
{
    return emit_region_frontier_loop_impl_v2(module, body_symbol,
        structural_layout, emit_member, emit_internal_commit, true);
}

llvm::Function* emit_region_frontier_entry_thunk_v2(llvm::Module& module,
    const std::string& wrapper_symbol,
    const std::string& body_symbol,
    const RegionFrontierLayoutV2& exact_layout,
    const bool alias_prevalidated,
    const bool value_contents_prevalidated,
    const bool descriptor_shapes_prevalidated)
{
    if (wrapper_symbol.empty() || body_symbol.empty()
        || wrapper_symbol == body_symbol
        || !region_frontier_layout_header_valid_v2(exact_layout)
        || !region_frontier_execution_mode_valid_v2(
            exact_layout.execution_mode)
        || exact_layout.member_count == 0U || exact_layout.members == nullptr
        || exact_layout.signal_slot_count == 0U
        || exact_layout.signals == nullptr
        || ((alias_prevalidated || value_contents_prevalidated
                || descriptor_shapes_prevalidated)
            && exact_layout.execution_mode
                != RegionFrontierExecutionModeV2::systemverilog_active)) {
        throw std::invalid_argument {
            "invalid region frontier exact wrapper layout"
        };
    }
    if (descriptor_shapes_prevalidated
        && (!alias_prevalidated || !value_contents_prevalidated)) {
        throw std::invalid_argument {
            "descriptor-shape receipt requires alias and canonical receipts"
        };
    }
    validate_region_frontier_internal_commit_layout_v2(exact_layout);
    if (module.getNamedValue(wrapper_symbol) != nullptr
        || module.getNamedValue(wrapper_symbol + ".physical.binding") != nullptr) {
        throw std::invalid_argument {
            "duplicate region frontier exact wrapper symbol"
        };
    }

    auto& context = module.getContext();
    auto* const i1 = llvm::Type::getInt1Ty(context);
    auto* const i32 = llvm::Type::getInt32Ty(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const pointer = llvm::PointerType::getUnqual(context);
    auto* const binding_type = region_frontier_physical_binding_type_v2(
        context, exact_layout.member_count, exact_layout.signal_slot_count);
    auto* const member_id_array_type = llvm::cast<llvm::ArrayType>(
        binding_type->getElementType(2U));
    auto* const signal_id_array_type = llvm::cast<llvm::ArrayType>(
        binding_type->getElementType(3U));
    auto* const owner_id_array_type = llvm::cast<llvm::ArrayType>(
        binding_type->getElementType(4U));

    std::vector<llvm::Constant*> member_process_ids;
    member_process_ids.reserve(exact_layout.member_count);
    for (std::uint32_t member = 0U;
         member < exact_layout.member_count; ++member) {
        member_process_ids.push_back(llvm::ConstantInt::get(i32,
            exact_layout.members[member].process_id));
    }
    std::vector<llvm::Constant*> signal_ids;
    std::vector<llvm::Constant*> owner_process_ids;
    signal_ids.reserve(exact_layout.signal_slot_count);
    owner_process_ids.reserve(exact_layout.signal_slot_count);
    for (std::uint32_t slot = 0U;
         slot < exact_layout.signal_slot_count; ++slot) {
        signal_ids.push_back(llvm::ConstantInt::get(i32,
            exact_layout.signals[slot].signal_id));
        owner_process_ids.push_back(llvm::ConstantInt::get(i32,
            exact_layout.signals[slot].owner_process_id));
    }
    const std::array<llvm::Constant*, 5U> binding_fields {
        llvm::ConstantInt::get(i64, exact_layout.certificate_generation),
        llvm::ConstantInt::get(i64, exact_layout.component_generation),
        llvm::ConstantArray::get(member_id_array_type, member_process_ids),
        llvm::ConstantArray::get(signal_id_array_type, signal_ids),
        llvm::ConstantArray::get(owner_id_array_type, owner_process_ids),
    };
    auto* const binding_constant = llvm::ConstantStruct::get(binding_type,
        binding_fields);
    const auto binding_name = wrapper_symbol + ".physical.binding";
    auto* const binding_global = new llvm::GlobalVariable(module, binding_type,
        true, llvm::GlobalValue::PrivateLinkage, binding_constant, binding_name);

    auto* const body_type = llvm::FunctionType::get(i32,
        { pointer, pointer, i1, i1, i1 }, false);
    auto* const named_body = module.getNamedValue(body_symbol);
    auto* body = llvm::dyn_cast_or_null<llvm::Function>(named_body);
    if (named_body != nullptr && body == nullptr) {
        binding_global->eraseFromParent();
        throw std::invalid_argument {
            "region frontier shared body symbol is not a function"
        };
    }
    if (body == nullptr) {
        body = llvm::Function::Create(body_type,
            llvm::GlobalValue::ExternalLinkage, body_symbol, module);
        body->addFnAttr(llvm::Attribute::NoUnwind);
    } else if (body->getFunctionType() != body_type
        || body->getCallingConv() != llvm::CallingConv::C
        || !body->hasFnAttribute(llvm::Attribute::NoUnwind)) {
        binding_global->eraseFromParent();
        throw std::invalid_argument {
            "region frontier shared body has an incompatible signature"
        };
    }

    auto* const wrapper_type = llvm::FunctionType::get(i32, { pointer }, false);
    auto* const wrapper = llvm::Function::Create(wrapper_type,
        llvm::GlobalValue::ExternalLinkage, wrapper_symbol, module);
    auto* const frame = wrapper->getArg(0U);
    frame->setName("instance_frame");
    auto* const entry = llvm::BasicBlock::Create(context, "entry", wrapper);
    llvm::IRBuilder<> builder { entry };
    auto* const status = builder.CreateCall(body_type, body,
        { frame, binding_global,
            llvm::ConstantInt::get(i1, alias_prevalidated ? 1U : 0U),
            llvm::ConstantInt::get(i1,
                value_contents_prevalidated ? 1U : 0U),
            llvm::ConstantInt::get(i1,
                descriptor_shapes_prevalidated ? 1U : 0U) },
        "shared.status");
    status->setDoesNotThrow();
    builder.CreateRet(status);
    wrapper->addFnAttr(llvm::Attribute::NoUnwind);
    return wrapper;
}

} // namespace fsim::runtime::simir::scratch
