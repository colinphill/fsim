// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/Support/Alignment.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace fsim::compiler::llvm_detail {

EncodedDynamicPartWrite lower_dynamic_part_write(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    llvm::Type* i32,
    llvm::Type* i64,
    const std::vector<RegisterSlot>& registers,
    const runtime::simir::RegisterId source_register,
    const runtime::simir::DynamicPartIndex& selection,
    const runtime::simir::ValueKind signal_kind) {
  const auto source = coerce_value_kind(
      builder,
      load_register(builder, registers, source_register),
      signal_kind);
  const auto base = coerce_value_kind(
      builder,
      load_register(builder, registers, selection.base),
      runtime::simir::ValueKind::logic4);
  auto* integer = packed_integer_type(context, selection.width);
  auto* base_unknown = builder.CreateICmpNE(
      builder.CreateAnd(
          base.bval,
          constant_i64(
              context,
              std::numeric_limits<std::uint32_t>::max())),
      constant_i64(context, 0));
  auto* signed_base = builder.CreateSExt(
      builder.CreateTrunc(base.aval, i32), i64);
  const auto lower_bound = std::min(selection.left, selection.right);
  const auto upper_bound = std::max(selection.left, selection.right);
  auto* lower = llvm::ConstantInt::getSigned(i64, lower_bound);
  auto* upper = llvm::ConstantInt::getSigned(i64, upper_bound);
  const auto edge_distance =
      static_cast<std::int64_t>(selection.width - 1U);
  const auto right_delta = selection.increasing
      ? (selection.source_descending ? 0 : edge_distance)
      : (selection.source_descending ? -edge_distance : 0);
  auto* selected_right = builder.CreateAdd(
      signed_base,
      llvm::ConstantInt::getSigned(i64, right_delta));
  const bool target_offsets_increase = selection.left >= selection.right
      ? selection.source_descending
      : !selection.source_descending;
  const auto bound_distance = static_cast<std::uint64_t>(upper_bound)
      - static_cast<std::uint64_t>(lower_bound);
  const bool fast_interval_math_is_safe =
      lower_bound >= std::numeric_limits<std::int32_t>::min()
      && upper_bound <= std::numeric_limits<std::int32_t>::max()
      && bound_distance
          <= std::numeric_limits<std::uint32_t>::max()
              - selection.base_offset;
  if (selection.width > 0U
      && target_offsets_increase
      && fast_interval_math_is_safe) {
    const auto zero_i64 = constant_i64(context, 0U);
    const auto last_source_bit = constant_i64(
        context, static_cast<std::uint64_t>(selection.width - 1U));
    llvm::Value* first_candidate = nullptr;
    llvm::Value* last_candidate = nullptr;
    if (selection.source_descending) {
      first_candidate = builder.CreateSub(lower, selected_right);
      last_candidate = builder.CreateSub(upper, selected_right);
    } else {
      first_candidate = builder.CreateSub(selected_right, upper);
      last_candidate = builder.CreateSub(selected_right, lower);
    }
    auto* first_bit = builder.CreateSelect(
        builder.CreateICmpSGT(first_candidate, zero_i64),
        first_candidate,
        zero_i64);
    auto* last_bit = builder.CreateSelect(
        builder.CreateICmpSLT(last_candidate, last_source_bit),
        last_candidate,
        last_source_bit);
    auto* intersects_source = builder.CreateAnd(
        builder.CreateICmpSLE(first_candidate, last_source_bit),
        builder.CreateICmpSGE(last_candidate, zero_i64));
    auto* has_selected_bits = builder.CreateAnd(
        intersects_source,
        builder.CreateICmpSLE(first_bit, last_bit));
    auto* active = builder.CreateAnd(
        builder.CreateNot(base_unknown), has_selected_bits);
    auto* safe_first_bit = builder.CreateSelect(
        active, first_bit, zero_i64);
    auto* selected_width = builder.CreateSelect(
        active,
        builder.CreateAdd(
            builder.CreateSub(last_bit, first_bit),
            constant_i64(context, 1U)),
        zero_i64);
    auto* source_shift = builder.CreateZExtOrTrunc(
        safe_first_bit, integer);
    auto* full_width = llvm::ConstantInt::getSigned(
        i64, static_cast<std::int64_t>(selection.width));
    auto* full_mask = packed_mask(context, selection.width);
    auto* mask_shift = builder.CreateSelect(
        builder.CreateICmpEQ(selected_width, full_width),
        zero_i64,
        selected_width);
    auto* shifted_mask = builder.CreateShl(
        llvm::ConstantInt::get(integer, 1U),
        builder.CreateZExtOrTrunc(mask_shift, integer));
    auto* partial_mask = builder.CreateSub(
        shifted_mask, llvm::ConstantInt::get(integer, 1U));
    auto* selected_mask = builder.CreateSelect(
        builder.CreateICmpEQ(selected_width, full_width),
        full_mask,
        partial_mask);
    const std::array<llvm::Value*, 4> source_planes{
        source.aval,
        source.bval,
        source.logic9_plane2,
        source.logic9_plane3};
    std::array<llvm::Value*, 4> selected_planes{};
    for (std::size_t plane = 0; plane < selected_planes.size(); ++plane) {
      selected_planes[plane] = builder.CreateAnd(
          builder.CreateLShr(source_planes[plane], source_shift),
          selected_mask);
    }
    auto* selected_start = selection.source_descending
        ? builder.CreateAdd(selected_right, safe_first_bit)
        : builder.CreateSub(selected_right, safe_first_bit);
    auto* relative_offset = builder.CreateSelect(
        builder.CreateICmpSGE(
            selected_start,
            llvm::ConstantInt::getSigned(i64, selection.right)),
        builder.CreateSub(
            selected_start,
            llvm::ConstantInt::getSigned(i64, selection.right)),
        builder.CreateSub(
            llvm::ConstantInt::getSigned(i64, selection.right),
            selected_start));
    auto* first_offset = builder.CreateSelect(
        active,
        builder.CreateAdd(
            relative_offset,
            constant_i64(context, selection.base_offset)),
        zero_i64);
    return EncodedDynamicPartWrite{
        EncodedValue{
            selected_planes[0],
            selected_planes[1],
            selection.width,
            selected_planes[2],
            selected_planes[3],
            signal_kind},
        builder.CreateTrunc(first_offset, i32),
        builder.CreateTrunc(selected_width, i32)};
  }
  llvm::Value* selected_width = constant_i64(context, 0);
  llvm::Value* first_offset = constant_i64(context, 0);
  std::array<llvm::Value*, 4> selected_planes {
      llvm::ConstantInt::get(integer, 0),
      llvm::ConstantInt::get(integer, 0),
      llvm::ConstantInt::get(integer, 0),
      llvm::ConstantInt::get(integer, 0)
  };
  const std::array<llvm::Value*, 4> source_planes{
      source.aval,
      source.bval,
      source.logic9_plane2,
      source.logic9_plane3};
  for (std::uint32_t bit = 0; bit < selection.width; ++bit) {
    const auto delta = selection.source_descending
        ? static_cast<std::int64_t>(bit)
        : -static_cast<std::int64_t>(bit);
    auto* selected = builder.CreateAdd(
        selected_right,
        llvm::ConstantInt::getSigned(i64, delta));
    auto* valid = builder.CreateAnd(
        builder.CreateNot(base_unknown),
        builder.CreateAnd(
            builder.CreateICmpSGE(selected, lower),
            builder.CreateICmpSLE(selected, upper)));
    auto* right = llvm::ConstantInt::getSigned(i64, selection.right);
    auto* offset = builder.CreateSelect(
        builder.CreateICmpSGE(selected, right),
        builder.CreateSub(selected, right),
        builder.CreateSub(right, selected));
    offset = builder.CreateAdd(
        offset, constant_i64(context, selection.base_offset));
    first_offset = builder.CreateSelect(
        builder.CreateAnd(
            valid,
            builder.CreateICmpEQ(
                selected_width, constant_i64(context, 0))),
        offset,
        first_offset);
    for (std::size_t plane = 0; plane < selected_planes.size(); ++plane) {
        auto* source_bit = builder.CreateAnd(
            builder.CreateLShr(
                source_planes[plane],
                llvm::ConstantInt::get(integer, bit)),
            llvm::ConstantInt::get(integer, 1));
        auto* appended = builder.CreateOr(
            selected_planes[plane],
            builder.CreateShl(
                source_bit,
                builder.CreateZExtOrTrunc(
                    selected_width, integer)));
        selected_planes[plane] = builder.CreateSelect(
            valid, appended, selected_planes[plane]);
    }
    selected_width = builder.CreateAdd(
        selected_width, builder.CreateZExt(valid, i64));
  }
  return EncodedDynamicPartWrite{
      EncodedValue{
          selected_planes[0],
          selected_planes[1],
          selection.width,
          selected_planes[2],
          selected_planes[3],
          signal_kind},
      builder.CreateTrunc(first_offset, i32),
      builder.CreateTrunc(selected_width, i32)};
}

bool dynamic_part_offsets_increase(
    const std::int64_t left,
    const std::int64_t right,
    const bool source_descending) noexcept {
  return left >= right ? source_descending : !source_descending;
}

// The process validator checks nonzero widths, signed-32-bit declared bounds,
// and that base_offset plus the declared interval fits the source/target.
// These helpers rely on those limits when forming signed 64-bit coordinates.
std::optional<EncodedValue> lower_dynamic_part_select_interval(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    llvm::Type* i32,
    llvm::Type* i64,
    const std::vector<RegisterSlot>& registers,
    const runtime::simir::DynamicPartSelect& operation) {
  if (!dynamic_part_offsets_increase(
          operation.left,
          operation.right,
          operation.source_descending)) {
    return std::nullopt;
  }

  const auto& source_slot = registers[operation.source];
  const auto source_kind = source_slot.kind;
  const auto source = load_register(builder, registers, operation.source);
  const auto base = coerce_value_kind(
      builder,
      load_register(builder, registers, operation.base),
      runtime::simir::ValueKind::logic4);
  const bool effective_two_state = operation.two_state
      && source_kind != runtime::simir::ValueKind::logic9;
  auto* const integer = packed_integer_type(context, operation.width);

  auto* const base_unknown = builder.CreateICmpNE(
      builder.CreateAnd(
          base.bval,
          constant_i64(context, std::numeric_limits<std::uint32_t>::max())),
      constant_i64(context, 0));
  auto* const signed_base = builder.CreateSExt(
      builder.CreateTrunc(base.aval, i32), i64);
  const auto edge_distance =
      static_cast<std::int64_t>(operation.width - 1U);
  const auto right_delta = operation.increasing
      ? (operation.source_descending ? 0 : edge_distance)
      : (operation.source_descending ? -edge_distance : 0);
  auto* const selected_right = builder.CreateAdd(
      signed_base,
      llvm::ConstantInt::getSigned(i64, right_delta));
  auto* const lower = llvm::ConstantInt::getSigned(
      i64, std::min(operation.left, operation.right));
  auto* const upper = llvm::ConstantInt::getSigned(
      i64, std::max(operation.left, operation.right));
  auto* const zero_i64 = constant_i64(context, 0);
  auto* const last_index = constant_i64(context, operation.width - 1U);

  llvm::Value* first_index = nullptr;
  llvm::Value* last_index_in_range = nullptr;
  if (operation.source_descending) {
    first_index = builder.CreateSub(lower, selected_right);
    last_index_in_range = builder.CreateSub(upper, selected_right);
  } else {
    first_index = builder.CreateSub(selected_right, upper);
    last_index_in_range = builder.CreateSub(selected_right, lower);
  }
  auto* const clipped_first = builder.CreateSelect(
      builder.CreateICmpSLT(first_index, zero_i64),
      zero_i64,
      first_index);
  auto* const clipped_last = builder.CreateSelect(
      builder.CreateICmpSGT(last_index_in_range, last_index),
      last_index,
      last_index_in_range);
  auto* const interval_nonempty = builder.CreateICmpSLE(
      clipped_first, clipped_last);
  auto* const active = builder.CreateAnd(
      builder.CreateNot(base_unknown), interval_nonempty);
  auto* const safe_first = builder.CreateSelect(
      active, clipped_first, zero_i64);
  auto* const safe_last = builder.CreateSelect(
      active, clipped_last, zero_i64);
  auto* const valid_count = builder.CreateSelect(
      active,
      builder.CreateAdd(
          builder.CreateSub(safe_last, safe_first),
          constant_i64(context, 1)),
      zero_i64);

  auto* first_coordinate = operation.source_descending
      ? builder.CreateAdd(selected_right, safe_first)
      : builder.CreateSub(selected_right, safe_first);
  auto* const right = llvm::ConstantInt::getSigned(i64, operation.right);
  auto* const distance = builder.CreateSelect(
      builder.CreateICmpSGE(first_coordinate, right),
      builder.CreateSub(first_coordinate, right),
      builder.CreateSub(right, first_coordinate));
  auto* const source_offset = builder.CreateAdd(
      distance, constant_i64(context, operation.base_offset));
  auto* const safe_source_offset = builder.CreateSelect(
      active, source_offset, zero_i64);

  auto* const result_mask = packed_mask(context, operation.width);
  auto* const result_zero = llvm::ConstantInt::get(integer, 0);
  auto* const full_count = builder.CreateICmpEQ(
      valid_count,
      constant_i64(context, operation.width));
  auto* const count_for_shift = builder.CreateSelect(
      full_count,
      result_zero,
      builder.CreateZExtOrTrunc(valid_count, integer));
  auto* const one = llvm::ConstantInt::get(integer, 1);
  auto* const variable_low_mask = builder.CreateSub(
      builder.CreateShl(one, count_for_shift), one);
  auto* const low_mask = builder.CreateSelect(
      full_count, result_mask, variable_low_mask);
  auto* const result_offset = builder.CreateZExtOrTrunc(
      safe_first, integer);
  auto* const selected_mask = builder.CreateAnd(
      builder.CreateShl(low_mask, result_offset), result_mask);
  auto* const outside_mask = builder.CreateAnd(
      builder.CreateNot(selected_mask), result_mask);
  auto* const source_shift = builder.CreateZExtOrTrunc(
      safe_source_offset,
      packed_integer_type(context, source.width));

  const auto extract_interval = [&](llvm::Value* source_plane) {
    auto* extracted = builder.CreateZExtOrTrunc(
        builder.CreateLShr(source_plane, source_shift), integer);
    extracted = builder.CreateAnd(extracted, low_mask);
    extracted = builder.CreateShl(extracted, result_offset);
    return builder.CreateAnd(extracted, selected_mask);
  };
  const auto merge_invalid = [&](llvm::Value* extracted,
                                 llvm::Value* invalid) {
    return builder.CreateOr(
        builder.CreateAnd(invalid, outside_mask), extracted);
  };
  auto* const invalid_aval = effective_two_state
      ? result_zero : result_mask;
  auto* const invalid_bval =
      !effective_two_state
              && source_kind != runtime::simir::ValueKind::logic9
          ? result_mask : result_zero;
  return EncodedValue{
      merge_invalid(extract_interval(source.aval), invalid_aval),
      merge_invalid(extract_interval(source.bval), invalid_bval),
      operation.width,
      merge_invalid(extract_interval(source.logic9_plane2), result_zero),
      merge_invalid(extract_interval(source.logic9_plane3), result_zero),
      source_kind};
}

std::optional<EncodedValue> lower_dynamic_part_insert_interval(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    llvm::Type* i32,
    llvm::Type* i64,
    const std::vector<RegisterSlot>& registers,
    const runtime::simir::DynamicPartInsert& operation) {
  const auto& selection = operation.selection;
  if (!dynamic_part_offsets_increase(
          selection.left,
          selection.right,
          selection.source_descending)) {
    return std::nullopt;
  }

  const auto destination_kind = registers[operation.destination].kind;
  const auto target = coerce_value_kind(
      builder,
      load_register(builder, registers, operation.target),
      destination_kind);
  const auto write = lower_dynamic_part_write(
      builder,
      context,
      i32,
      i64,
      registers,
      operation.source,
      selection,
      destination_kind);
  auto* const integer = packed_integer_type(context, target.width);
  auto* const target_mask = packed_mask(context, target.width);
  auto* const zero = llvm::ConstantInt::get(integer, 0);
  auto* const one = llvm::ConstantInt::get(integer, 1);
  auto* const fills_target = builder.CreateICmpEQ(
      write.width,
      llvm::ConstantInt::get(i32, target.width));
  auto* const count_for_shift = builder.CreateSelect(
      fills_target,
      zero,
      builder.CreateZExtOrTrunc(write.width, integer));
  auto* const variable_low_mask = builder.CreateSub(
      builder.CreateShl(one, count_for_shift), one);
  auto* const low_mask = builder.CreateSelect(
      fills_target, target_mask, variable_low_mask);
  auto* const target_offset = builder.CreateZExtOrTrunc(
      write.offset, integer);
  auto* const write_mask = builder.CreateAnd(
      builder.CreateShl(low_mask, target_offset), target_mask);
  auto* const keep_mask = builder.CreateAnd(
      builder.CreateNot(write_mask), target_mask);
  const auto merge_plane = [&](llvm::Value* target_plane,
                               llvm::Value* source_plane) {
    auto* value = builder.CreateZExtOrTrunc(source_plane, integer);
    value = builder.CreateAnd(value, low_mask);
    value = builder.CreateShl(value, target_offset);
    return builder.CreateAnd(
        builder.CreateOr(
            builder.CreateAnd(target_plane, keep_mask),
            value),
        target_mask);
  };
  return EncodedValue{
      merge_plane(target.aval, write.value.aval),
      merge_plane(target.bval, write.value.bval),
      target.width,
      merge_plane(target.logic9_plane2, write.value.logic9_plane2),
      merge_plane(target.logic9_plane3, write.value.logic9_plane3),
      destination_kind};
}

EncodedValue load_wide_register_bit(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    const std::vector<RegisterSlot>& registers,
    const runtime::simir::RegisterId id,
    llvm::Value* const safe_offset) {
  const auto& slot = registers[id];
  auto* const i64 = llvm::Type::getInt64Ty(context);
  auto* const word_index = builder.CreateLShr(
      safe_offset, constant_i64(context, 6U));
  auto* const bit_index = builder.CreateAnd(
      safe_offset, constant_i64(context, 63U));
  const auto load_plane_bit = [&](llvm::Value* const base) {
    auto* const absolute_word = builder.CreateAdd(
        constant_i64(context, slot.word_offset), word_index);
    auto* const pointer = builder.CreateGEP(i64, base, absolute_word);
    auto* const word = builder.CreateLoad(i64, pointer);
    word->setAlignment(llvm::Align { 8 });
    return builder.CreateAnd(
        builder.CreateLShr(word, bit_index), constant_i64(context, 1U));
  };
  auto* const zero = constant_i64(context, 0U);
  auto* const aval = load_plane_bit(slot.aval_base);
  auto* const bval = load_plane_bit(slot.bval_base);
  if (slot.kind != runtime::simir::ValueKind::logic9) {
    return EncodedValue { aval, bval, 1U, zero, zero, slot.kind };
  }
  return EncodedValue {
      aval,
      bval,
      1U,
      load_plane_bit(slot.logic9_plane2_base),
      load_plane_bit(slot.logic9_plane3_base),
      slot.kind};
}

void store_wide_dynamic_insert_words(
    llvm::IRBuilder<>& builder,
    llvm::LLVMContext& context,
    const std::vector<RegisterSlot>& registers,
    const runtime::simir::RegisterId destination,
    const runtime::simir::RegisterId target,
    EncodedValue source,
    llvm::Value* const valid,
    llvm::Value* const safe_offset) {
  const auto& destination_slot = registers[destination];
  const auto& target_slot = registers[target];
  const auto width = target_slot.width;
  auto* const i64 = llvm::Type::getInt64Ty(context);
  auto* const zero = constant_i64(context, 0U);
  auto* const one = constant_i64(context, 1U);
  source = canonicalize_logic9_value(builder, source);
  const auto word_count = static_cast<std::size_t>(
      (static_cast<std::uint64_t>(width) + 63U) / 64U);
  auto* const selected_word = builder.CreateLShr(
      safe_offset, constant_i64(context, 6U));
  auto* const selected_bit = builder.CreateAnd(
      safe_offset, constant_i64(context, 63U));
  auto* const selected_mask = builder.CreateShl(one, selected_bit);

  const auto plane_base = [](const RegisterSlot& slot,
                             const std::size_t plane) -> llvm::Value* {
    switch (plane) {
    case 0U:
      return slot.aval_base;
    case 1U:
      return slot.bval_base;
    case 2U:
      return slot.logic9_plane2_base;
    case 3U:
      return slot.logic9_plane3_base;
    }
    return nullptr;
  };
  const auto load_word = [&](const RegisterSlot& slot,
                             const std::size_t plane,
                             const std::size_t word_index) {
    auto* const offset = builder.CreateAdd(
        constant_i64(context, slot.word_offset),
        constant_i64(context, word_index));
    auto* const pointer = builder.CreateGEP(
        i64, plane_base(slot, plane), offset);
    auto* const word = builder.CreateLoad(i64, pointer);
    word->setAlignment(llvm::Align { 8 });
    return word;
  };
  const auto store_word = [&](const RegisterSlot& slot,
                              const std::size_t plane,
                              const std::size_t word_index,
                              llvm::Value* const value) {
    auto* const offset = builder.CreateAdd(
        constant_i64(context, slot.word_offset),
        constant_i64(context, word_index));
    auto* const pointer = builder.CreateGEP(
        i64, plane_base(slot, plane), offset);
    auto* const store = builder.CreateStore(value, pointer);
    store->setAlignment(llvm::Align { 8 });
  };

  for (std::size_t word_index = 0U;
       word_index < word_count;
       ++word_index) {
    const auto bits_remaining = static_cast<std::uint64_t>(width)
        - static_cast<std::uint64_t>(word_index) * 64U;
    const auto active_bits = std::min<std::uint64_t>(bits_remaining, 64U);
    const auto word_mask = active_bits == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << active_bits) - 1U;
    auto* const mask = constant_i64(context, word_mask);
    std::array<llvm::Value*, 4> target_planes {
        load_word(target_slot, 0U, word_index),
        load_word(target_slot, 1U, word_index),
        zero,
        zero};
    if (target_slot.kind == runtime::simir::ValueKind::logic9) {
      target_planes[2U] = load_word(target_slot, 2U, word_index);
      target_planes[3U] = load_word(target_slot, 3U, word_index);
      auto* const invalid = builder.CreateAnd(
          target_planes[3U],
          builder.CreateOr(
              target_planes[2U],
              builder.CreateOr(target_planes[1U], target_planes[0U])));
      target_planes[0U] = builder.CreateOr(target_planes[0U], invalid);
      target_planes[1U] = builder.CreateAnd(
          target_planes[1U], builder.CreateNot(invalid));
      target_planes[2U] = builder.CreateAnd(
          target_planes[2U], builder.CreateNot(invalid));
      target_planes[3U] = builder.CreateAnd(
          target_planes[3U], builder.CreateNot(invalid));
    }
    for (auto*& plane : target_planes) {
      plane = builder.CreateAnd(plane, mask);
    }

    if (destination_slot.kind != target_slot.kind) {
      if (destination_slot.kind == runtime::simir::ValueKind::logic9) {
        target_planes = {
            target_planes[0U],
            builder.CreateAnd(
                builder.CreateNot(target_planes[1U]), mask),
            builder.CreateAnd(
                builder.CreateAnd(
                    builder.CreateNot(target_planes[0U]),
                    target_planes[1U]),
                mask),
            zero};
      } else {
        target_planes = {
            builder.CreateAnd(
                builder.CreateOr(
                    builder.CreateOr(
                        target_planes[0U], target_planes[3U]),
                    builder.CreateNot(builder.CreateOr(
                        target_planes[1U], target_planes[2U]))),
                mask),
            builder.CreateAnd(
                builder.CreateOr(
                    target_planes[3U],
                    builder.CreateNot(target_planes[1U])),
                mask),
            zero,
            zero};
      }
    }

    auto* const selected_here = builder.CreateAnd(
        valid,
        builder.CreateICmpEQ(
            selected_word, constant_i64(context, word_index)));
    auto* const write_mask = builder.CreateSelect(
        selected_here, selected_mask, zero);
    const std::array<llvm::Value*, 4> source_planes {
        source.aval,
        source.bval,
        source.logic9_plane2 != nullptr ? source.logic9_plane2 : zero,
        source.logic9_plane3 != nullptr ? source.logic9_plane3 : zero};
    const auto stored_plane_count
        = destination_slot.kind == runtime::simir::ValueKind::logic9
        ? 4U : 2U;
    for (std::size_t plane = 0U; plane < stored_plane_count; ++plane) {
      auto* const source_word = builder.CreateZExtOrTrunc(
          source_planes[plane], i64);
      auto* const inserted_bit = builder.CreateShl(
          builder.CreateAnd(source_word, one), selected_bit);
      auto* const updated = builder.CreateAnd(
          builder.CreateOr(
              builder.CreateAnd(
                  target_planes[plane], builder.CreateNot(write_mask)),
              builder.CreateSelect(selected_here, inserted_bit, zero)),
          mask);
      store_word(destination_slot, plane, word_index, updated);
    }
  }

  if (destination_slot.initialized_base != nullptr) {
    auto* const initialized_pointer = builder.CreateGEP(
        llvm::Type::getInt8Ty(context),
        destination_slot.initialized_base,
        constant_i64(context, destination_slot.index));
    builder.CreateStore(
        llvm::ConstantInt::get(llvm::Type::getInt8Ty(context), 1U),
        initialized_pointer);
  }
}

}  // namespace fsim::compiler::llvm_detail
