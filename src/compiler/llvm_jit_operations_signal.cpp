// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/ADT/APInt.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <vector>

namespace fsim::compiler::llvm_detail {

using runtime::simir::LoadConstant;
using runtime::simir::WriteBlocking;
using runtime::simir::WriteUpdate;
using runtime::simir::WriteAfter;
using runtime::simir::WriteBlockingSlice;
using runtime::simir::WriteUpdateSlice;
using runtime::simir::WriteAfterSlice;
using runtime::simir::ForceSignalSlice;
using runtime::simir::ReleaseSignalSlice;
using runtime::simir::WriteProjectedWaveform;
using runtime::simir::WriteProjected;
using runtime::simir::WriteInertial;
using runtime::simir::ReadSignal;
using runtime::simir::SignalEvent;
using runtime::simir::SignalLastValue;
using runtime::simir::SignalLastEvent;
using runtime::simir::ReadSimulationTime;
using runtime::simir::VitalTimingCheck;
using runtime::simir::VitalDelay;
using runtime::simir::SignalActive;
using runtime::simir::SignalLastActive;
using runtime::simir::SignalDriving;
using runtime::simir::SignalDrivingValue;
using runtime::simir::ValueKind;

llvm::StructType* create_jit_runtime_type(llvm::LLVMContext& context) {
  auto* i32 = llvm::Type::getInt32Ty(context);
  auto* i64 = llvm::Type::getInt64Ty(context);
  auto* pointer = llvm::PointerType::getUnqual(context);
  return llvm::StructType::create(
      context,
      { i32, i32, pointer, pointer, pointer, pointer, pointer, pointer,
          i32, i32, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer, pointer, pointer,
          i32, i32,
          pointer, pointer, pointer, i32, i32, i32,
          pointer, pointer, pointer, i32, i32,
          pointer, i32, i32, i64, pointer, pointer, pointer,
          pointer, pointer, pointer, pointer,
          pointer, pointer, i32, i32, pointer },
      "fsim_jit_runtime_v1");
}

void SignalOperationLowerer::mark_direct_update_active(
    llvm::Value* slot,
    const std::uint32_t slot_index,
    llvm::Value* enabled)
{
  auto* active_address = builder.CreateStructGEP(
      direct_update_slot_type, slot, 5);
  auto* const enabled_i32 = enabled == nullptr
      ? llvm::ConstantInt::get(i32, 1U)
      : builder.CreateZExt(enabled, i32);
  auto* const enabled_i64 = builder.CreateZExt(enabled_i32, i64);
  if (require_direct_update_slots) {
    auto* word_address = builder.CreateInBoundsGEP(
        i64,
        direct_update_active_words,
        llvm::ConstantInt::get(i32, slot_index / 64U));
    builder.CreateStore(
        builder.CreateOr(
            builder.CreateLoad(i64, word_address),
            builder.CreateMul(
                enabled_i64,
                constant_i64(
                    context, UINT64_C(1) << (slot_index % 64U)))),
        word_address);
    builder.CreateStore(
        builder.CreateOr(
            builder.CreateLoad(i32, active_address), enabled_i32),
        active_address);
    return;
  }

  auto* mark_bitmap = llvm::BasicBlock::Create(
      context, "update.activity.bitmap", builder.GetInsertBlock()->getParent());
  auto* mark_done = llvm::BasicBlock::Create(
      context, "update.activity.done", builder.GetInsertBlock()->getParent());
  builder.CreateCondBr(
      builder.CreateICmpNE(
          direct_update_active_words,
          llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(
              direct_update_active_words->getType()))),
      mark_bitmap,
      mark_done);
  builder.SetInsertPoint(mark_bitmap);
  auto* word_address = builder.CreateInBoundsGEP(
      i64,
      direct_update_active_words,
      llvm::ConstantInt::get(i32, slot_index / 64U));
  builder.CreateStore(
      builder.CreateOr(
          builder.CreateLoad(i64, word_address),
          builder.CreateMul(
              enabled_i64,
              constant_i64(
                  context, UINT64_C(1) << (slot_index % 64U)))),
      word_address);
  builder.CreateBr(mark_done);
  builder.SetInsertPoint(mark_done);
  builder.CreateStore(
      builder.CreateOr(
          builder.CreateLoad(i32, active_address), enabled_i32),
      active_address);
}

bool SignalOperationLowerer::begin_direct_update(
    const runtime::simir::SignalId signal,
    const std::uint32_t offset,
    const EncodedValue source)
{
  if (signal < signal_widths.size() && signal_widths[signal] > 64U) {
    return begin_direct_wide_update(signal, offset, source);
  }
  if (direct_update_slots == nullptr
      || direct_update_slot_type == nullptr
      || (source.kind != ValueKind::logic4
          && source.kind != ValueKind::logic9)
      || source.width == 0U
      || source.width > 64U) {
    return false;
  }
  const auto found = std::ranges::find(direct_update_signals, signal);
  if (found == direct_update_signals.end()) {
    return false;
  }
  const auto slot_index = static_cast<std::uint32_t>(
      std::distance(direct_update_signals.begin(), found));
  llvm::BasicBlock* callback = nullptr;
  if (!require_direct_update_slots) {
    auto* direct = llvm::BasicBlock::Create(
        context, "update.direct", builder.GetInsertBlock()->getParent());
    callback = llvm::BasicBlock::Create(
        context, "update.callback", builder.GetInsertBlock()->getParent());
    builder.CreateCondBr(
        builder.CreateICmpNE(
            direct_update_slots,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(direct_update_slots->getType()))),
        direct,
        callback);
    builder.SetInsertPoint(direct);
  }
  auto* slot = builder.CreateInBoundsGEP(
      direct_update_slot_type,
      direct_update_slots,
      llvm::ConstantInt::get(i32, slot_index),
      "update.slot");
  const auto source_mask = width_mask(source.width);
  const auto shifted_mask = offset == 0U
      ? source_mask
      : source_mask << offset;
  auto* mask = constant_i64(context, shifted_mask);
  auto* shifted_aval = offset == 0U
      ? source.aval
      : builder.CreateShl(source.aval, constant_i64(context, offset));
  auto* shifted_bval = offset == 0U
      ? source.bval
      : builder.CreateShl(source.bval, constant_i64(context, offset));
  auto* const aval_address = builder.CreateStructGEP(
      direct_update_slot_type, slot, 0);
  auto* const bval_address = builder.CreateStructGEP(
      direct_update_slot_type, slot, 1);
  auto* const previous_aval = builder.CreateLoad(i64, aval_address);
  auto* const previous_bval = builder.CreateLoad(i64, bval_address);
  llvm::Value* enabled = llvm::ConstantInt::getTrue(context);
  llvm::Value* effective_mask = mask;
  if (source.kind == ValueKind::logic4) {
    auto* const changed = builder.CreateICmpNE(
        builder.CreateAnd(
            builder.CreateOr(
                builder.CreateXor(previous_aval, shifted_aval),
                builder.CreateXor(previous_bval, shifted_bval)),
            mask),
        constant_i64(context, 0U));
    auto* const reserved_address = builder.CreateStructGEP(
        direct_update_slot_type, slot, 6);
    auto* const shadow_valid = builder.CreateICmpNE(
        builder.CreateAnd(
            builder.CreateLoad(i32, reserved_address),
            llvm::ConstantInt::get(i32, 1U)),
        llvm::ConstantInt::get(i32, 0U));
    enabled = builder.CreateOr(builder.CreateNot(shadow_valid), changed);
    effective_mask = builder.CreateSelect(
        enabled, mask, constant_i64(context, 0U));
  }
  const auto merge_plane = [&](const unsigned member, llvm::Value* shifted) {
    auto* address = builder.CreateStructGEP(
        direct_update_slot_type, slot, member);
    auto* previous = builder.CreateLoad(i64, address);
    auto* merged = builder.CreateOr(
        builder.CreateAnd(previous, builder.CreateNot(effective_mask)),
        builder.CreateAnd(shifted, effective_mask));
    builder.CreateStore(merged, address);
  };
  merge_plane(0, shifted_aval);
  merge_plane(1, shifted_bval);
  if (source.kind == ValueKind::logic9) {
    auto* shifted_plane2 = offset == 0U
        ? source.logic9_plane2
        : builder.CreateShl(
              source.logic9_plane2, constant_i64(context, offset));
    auto* shifted_plane3 = offset == 0U
        ? source.logic9_plane3
        : builder.CreateShl(
              source.logic9_plane3, constant_i64(context, offset));
    merge_plane(2, shifted_plane2);
    merge_plane(3, shifted_plane3);
  }
  auto* mask_address = builder.CreateStructGEP(
      direct_update_slot_type, slot, 4);
  builder.CreateStore(
      builder.CreateOr(
          builder.CreateLoad(i64, mask_address), effective_mask),
      mask_address);
  if (source.kind == ValueKind::logic4) {
    mark_direct_update_active(slot, slot_index, enabled);
  }
  branch_to_next();
  if (callback != nullptr) {
    builder.SetInsertPoint(callback);
  }
  return true;
}

bool SignalOperationLowerer::begin_direct_update(
    const runtime::simir::SignalId signal,
    llvm::Value* offset,
    llvm::Value* width,
    const EncodedValue source)
{
  if (signal < signal_widths.size() && signal_widths[signal] > 64U) {
    return begin_direct_wide_update(signal, offset, width, source);
  }
  if (direct_update_slots == nullptr
      || direct_update_slot_type == nullptr
      || (source.kind != ValueKind::logic4
          && source.kind != ValueKind::logic9)
      || source.width == 0U
      || source.width > 64U) {
    return false;
  }
  const auto found = std::ranges::find(direct_update_signals, signal);
  if (found == direct_update_signals.end()) {
    return false;
  }
  const auto slot_index = static_cast<std::uint32_t>(
      std::distance(direct_update_signals.begin(), found));
  llvm::BasicBlock* callback = nullptr;
  if (!require_direct_update_slots) {
    auto* direct = llvm::BasicBlock::Create(
        context,
        "update.dynamic.direct",
        builder.GetInsertBlock()->getParent());
    callback = llvm::BasicBlock::Create(
        context,
        "update.dynamic.callback",
        builder.GetInsertBlock()->getParent());
    builder.CreateCondBr(
        builder.CreateICmpNE(
            direct_update_slots,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(direct_update_slots->getType()))),
        direct,
        callback);
    builder.SetInsertPoint(direct);
  }
  auto* slot = builder.CreateInBoundsGEP(
      direct_update_slot_type,
      direct_update_slots,
      llvm::ConstantInt::get(i32, slot_index),
      "update.dynamic.slot");
  auto* offset64 = builder.CreateZExt(offset, i64);
  auto* width64 = builder.CreateZExt(width, i64);
  auto* source_mask = builder.CreateLShr(
      constant_i64(context, std::numeric_limits<std::uint64_t>::max()),
      builder.CreateSub(constant_i64(context, 64U), width64));
  auto* mask = builder.CreateShl(source_mask, offset64);
  auto* shifted_aval = builder.CreateShl(source.aval, offset64);
  auto* shifted_bval = builder.CreateShl(source.bval, offset64);
  const auto merge_plane = [&](const unsigned member, llvm::Value* shifted) {
    auto* address = builder.CreateStructGEP(
        direct_update_slot_type, slot, member);
    auto* previous = builder.CreateLoad(i64, address);
    auto* merged = builder.CreateOr(
        builder.CreateAnd(previous, builder.CreateNot(mask)),
        builder.CreateAnd(shifted, mask));
    builder.CreateStore(merged, address);
  };
  merge_plane(0, shifted_aval);
  merge_plane(1, shifted_bval);
  if (source.kind == ValueKind::logic9) {
    merge_plane(2, builder.CreateShl(source.logic9_plane2, offset64));
    merge_plane(3, builder.CreateShl(source.logic9_plane3, offset64));
  }
  auto* mask_address = builder.CreateStructGEP(
      direct_update_slot_type, slot, 4);
  builder.CreateStore(
      builder.CreateOr(builder.CreateLoad(i64, mask_address), mask),
      mask_address);
  if (source.kind == ValueKind::logic4) {
    mark_direct_update_active(slot, slot_index);
  }
  branch_to_next();
  if (callback != nullptr) {
    builder.SetInsertPoint(callback);
  }
  return true;
}

bool SignalOperationLowerer::begin_direct_wide_update(
    const runtime::simir::SignalId signal,
    const std::uint32_t offset,
    const EncodedValue source)
{
  if (direct_update_slots == nullptr
      || direct_update_slot_type == nullptr
      || source.kind != ValueKind::logic4
      || source.width == 0U
      || signal >= signal_widths.size()
      || signal_widths[signal] <= 64U
      || offset > signal_widths[signal]
      || source.width > signal_widths[signal] - offset) {
    return false;
  }
  const auto found = std::ranges::find(direct_update_signals, signal);
  if (found == direct_update_signals.end()) {
    return false;
  }
  const auto slot_index = static_cast<std::uint32_t>(
      std::distance(direct_update_signals.begin(), found));
  llvm::BasicBlock* callback = nullptr;
  if (!require_direct_update_slots) {
    auto* direct = llvm::BasicBlock::Create(
        context, "update.wide.direct", builder.GetInsertBlock()->getParent());
    callback = llvm::BasicBlock::Create(
        context, "update.wide.callback", builder.GetInsertBlock()->getParent());
    builder.CreateCondBr(
        builder.CreateICmpNE(
            direct_update_slots,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(direct_update_slots->getType()))),
        direct,
        callback);
    builder.SetInsertPoint(direct);
  }
  auto* slot = builder.CreateInBoundsGEP(
      direct_update_slot_type,
      direct_update_slots,
      llvm::ConstantInt::get(i32, slot_index),
      "update.wide.slot");
  auto* pointer = llvm::PointerType::getUnqual(context);
  auto* aval_words = builder.CreateLoad(
      pointer, builder.CreateStructGEP(direct_update_slot_type, slot, 7));
  auto* bval_words = builder.CreateLoad(
      pointer, builder.CreateStructGEP(direct_update_slot_type, slot, 8));
  auto* mask_words = builder.CreateLoad(
      pointer, builder.CreateStructGEP(direct_update_slot_type, slot, 9));
  const auto source_word = [&](llvm::Value* plane,
                               const std::uint32_t source_offset) {
    auto* value = plane;
    if (source_offset != 0U) {
      value = builder.CreateLShr(
          value,
          llvm::ConstantInt::get(value->getType(), source_offset));
    }
    const auto plane_width = llvm::cast<llvm::IntegerType>(
        value->getType())->getBitWidth();
    if (plane_width > 64U) {
      return builder.CreateTrunc(value, i64);
    }
    if (plane_width < 64U) {
      return builder.CreateZExt(value, i64);
    }
    return value;
  };
  const auto merge_word = [&](llvm::Value* words,
                              const std::uint32_t word,
                              llvm::Value* value,
                              const std::uint64_t mask) {
    auto* address = builder.CreateInBoundsGEP(
        i64, words, llvm::ConstantInt::get(i32, word));
    auto* previous = builder.CreateLoad(i64, address);
    auto* encoded_mask = constant_i64(context, mask);
    builder.CreateStore(
        builder.CreateOr(
            builder.CreateAnd(previous, builder.CreateNot(encoded_mask)),
            builder.CreateAnd(value, encoded_mask)),
        address);
  };
  const auto first_word = offset / 64U;
  const auto last_word = (offset + source.width - 1U) / 64U;
  for (auto word = first_word; word <= last_word; ++word) {
    const auto word_start = word * 64U;
    const auto overlap_start = std::max(offset, word_start);
    const auto overlap_end = std::min(
        offset + source.width, word_start + 64U);
    const auto source_offset = overlap_start - offset;
    const auto target_offset = overlap_start - word_start;
    const auto overlap_width = overlap_end - overlap_start;
    const auto low_mask = overlap_width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << overlap_width) - 1U;
    const auto mask = low_mask << target_offset;
    auto* aval = source_word(source.aval, source_offset);
    auto* bval = source_word(source.bval, source_offset);
    if (target_offset != 0U) {
      aval = builder.CreateShl(aval, constant_i64(context, target_offset));
      bval = builder.CreateShl(bval, constant_i64(context, target_offset));
    }
    merge_word(aval_words, word, aval, mask);
    merge_word(bval_words, word, bval, mask);
    auto* mask_address = builder.CreateInBoundsGEP(
        i64, mask_words, llvm::ConstantInt::get(i32, word));
    builder.CreateStore(
        builder.CreateOr(
            builder.CreateLoad(i64, mask_address),
            constant_i64(context, mask)),
        mask_address);
  }
  mark_direct_update_active(slot, slot_index);
  branch_to_next();
  if (callback != nullptr) {
    builder.SetInsertPoint(callback);
  }
  return true;
}

bool SignalOperationLowerer::begin_direct_wide_update(
    const runtime::simir::SignalId signal,
    llvm::Value* offset,
    llvm::Value* width,
    const EncodedValue source)
{
  if (direct_update_slots == nullptr
      || direct_update_slot_type == nullptr
      || source.kind != ValueKind::logic4
      || source.width == 0U
      || source.width > 64U
      || signal >= signal_widths.size()
      || signal_widths[signal] <= 64U) {
    return false;
  }
  const auto found = std::ranges::find(direct_update_signals, signal);
  if (found == direct_update_signals.end()) {
    return false;
  }
  const auto slot_index = static_cast<std::uint32_t>(
      std::distance(direct_update_signals.begin(), found));
  llvm::BasicBlock* callback = nullptr;
  if (!require_direct_update_slots) {
    auto* direct = llvm::BasicBlock::Create(
        context,
        "update.wide.dynamic.direct",
        builder.GetInsertBlock()->getParent());
    callback = llvm::BasicBlock::Create(
        context,
        "update.wide.dynamic.callback",
        builder.GetInsertBlock()->getParent());
    builder.CreateCondBr(
        builder.CreateICmpNE(
            direct_update_slots,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(direct_update_slots->getType()))),
        direct,
        callback);
    builder.SetInsertPoint(direct);
  }
  auto* slot = builder.CreateInBoundsGEP(
      direct_update_slot_type,
      direct_update_slots,
      llvm::ConstantInt::get(i32, slot_index),
      "update.wide.dynamic.slot");
  auto* pointer = llvm::PointerType::getUnqual(context);
  auto* aval_words = builder.CreateLoad(
      pointer, builder.CreateStructGEP(direct_update_slot_type, slot, 7));
  auto* bval_words = builder.CreateLoad(
      pointer, builder.CreateStructGEP(direct_update_slot_type, slot, 8));
  auto* mask_words = builder.CreateLoad(
      pointer, builder.CreateStructGEP(direct_update_slot_type, slot, 9));
  auto* word = builder.CreateLShr(
      offset, llvm::ConstantInt::get(i32, 6));
  auto* bit = builder.CreateAnd(offset, llvm::ConstantInt::get(i32, 63));
  auto* capacity = builder.CreateSub(
      llvm::ConstantInt::get(i32, 64), bit);
  auto* first_width = builder.CreateSelect(
      builder.CreateICmpULT(width, capacity), width, capacity);
  auto* first_width64 = builder.CreateZExt(first_width, i64);
  auto* bit64 = builder.CreateZExt(bit, i64);
  auto* first_low_mask = builder.CreateLShr(
      constant_i64(context, std::numeric_limits<std::uint64_t>::max()),
      builder.CreateSub(constant_i64(context, 64U), first_width64));
  auto* first_mask = builder.CreateShl(first_low_mask, bit64);
  const auto merge_word = [&](llvm::Value* words,
                              llvm::Value* word_index,
                              llvm::Value* value,
                              llvm::Value* mask) {
    auto* address = builder.CreateInBoundsGEP(i64, words, word_index);
    auto* previous = builder.CreateLoad(i64, address);
    builder.CreateStore(
        builder.CreateOr(
            builder.CreateAnd(previous, builder.CreateNot(mask)),
            builder.CreateAnd(value, mask)),
        address);
  };
  merge_word(
      aval_words, word, builder.CreateShl(source.aval, bit64), first_mask);
  merge_word(
      bval_words, word, builder.CreateShl(source.bval, bit64), first_mask);
  auto* first_mask_address = builder.CreateInBoundsGEP(i64, mask_words, word);
  builder.CreateStore(
      builder.CreateOr(
          builder.CreateLoad(i64, first_mask_address), first_mask),
      first_mask_address);
  mark_direct_update_active(slot, slot_index);

  auto* cross = llvm::BasicBlock::Create(
      context,
      "update.wide.dynamic.cross",
      builder.GetInsertBlock()->getParent());
  auto* done = llvm::BasicBlock::Create(
      context,
      "update.wide.dynamic.done",
      builder.GetInsertBlock()->getParent());
  builder.CreateCondBr(builder.CreateICmpUGT(width, first_width), cross, done);
  builder.SetInsertPoint(cross);
  auto* remaining_width = builder.CreateSub(width, first_width);
  auto* remaining_width64 = builder.CreateZExt(remaining_width, i64);
  auto* remaining_mask = builder.CreateLShr(
      constant_i64(context, std::numeric_limits<std::uint64_t>::max()),
      builder.CreateSub(constant_i64(context, 64U), remaining_width64));
  auto* next_word = builder.CreateAdd(word, llvm::ConstantInt::get(i32, 1));
  merge_word(
      aval_words,
      next_word,
      builder.CreateLShr(source.aval, first_width64),
      remaining_mask);
  merge_word(
      bval_words,
      next_word,
      builder.CreateLShr(source.bval, first_width64),
      remaining_mask);
  auto* remaining_mask_address = builder.CreateInBoundsGEP(
      i64, mask_words, next_word);
  builder.CreateStore(
      builder.CreateOr(
          builder.CreateLoad(i64, remaining_mask_address), remaining_mask),
      remaining_mask_address);
  builder.CreateBr(done);
  builder.SetInsertPoint(done);
  branch_to_next();
  if (callback != nullptr) {
    builder.SetInsertPoint(callback);
  }
  return true;
}

void SignalOperationLowerer::lower(
    const LoadConstant& operation) {
              EncodedValue value{};
              if (operation.value.is_logic9()) {
                const auto width = static_cast<std::uint32_t>(
                    operation.value.width());
                if (width <= 64) {
                  const auto word = operation.value.logic9_low_word();
                  value = {
                      constant_i64(context, word.planes[0]),
                      constant_i64(context, word.planes[1]),
                      width,
                      constant_i64(context, word.planes[2]),
                      constant_i64(context, word.planes[3]),
                      ValueKind::logic9};
                } else {
                  std::array<std::vector<std::uint64_t>, 4> planes;
                  for (auto& plane : planes) {
                    plane.resize((width + 63U) / 64U);
                  }
                  for (std::uint32_t bit = 0; bit < width; ++bit) {
                    const auto encoded = static_cast<std::uint8_t>(
                        operation.value.get_logic9(bit));
                    for (std::size_t plane = 0; plane < planes.size(); ++plane) {
                      if (((encoded >> plane) & 1U) != 0U) {
                        planes[plane][bit / 64U]
                            |= std::uint64_t { 1 } << (bit % 64U);
                      }
                    }
                  }
                  const auto constant_plane = [&](const std::size_t plane) {
                    return llvm::ConstantInt::get(
                        context, llvm::APInt(width, planes[plane]));
                  };
                  value = {
                      constant_plane(0),
                      constant_plane(1),
                      width,
                      constant_plane(2),
                      constant_plane(3),
                      ValueKind::logic9};
                }
              } else {
                  const auto width = static_cast<std::uint32_t>(
                      operation.value.width());
                  if (width <= 64) {
                      const auto word = operation.value.low_word();
                      value = {
                          constant_i64(context, word.aval),
                          constant_i64(context, word.bval),
                          width
                      };
                  } else {
                      value = {
                          llvm::ConstantInt::get(
                              context,
                              llvm::APInt(
                                  width, operation.value.aval_words())),
                          llvm::ConstantInt::get(
                              context,
                              llvm::APInt(
                                  width, operation.value.bval_words())),
                          width
                      };
                  }
              }
              store_register(
                  builder, registers, operation.destination,
                  value);
              branch_to_next();
            
}

void SignalOperationLowerer::lower(const WriteBlocking& operation) {
  const auto signal_kind = signal_value_kinds.empty()
      ? ValueKind::logic4 : signal_value_kinds[operation.signal];
  const auto source = coerce_value_kind(
      builder, load_register(builder, registers, operation.source),
      signal_kind);
  if (signal_kind == ValueKind::logic9) {
    store_logic9_word(logic9_word_slot, source);
    builder.CreateCall(
        write_logic9_type, write_logic9_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         logic9_word_slot});
  } else {
    builder.CreateCall(
        write_type, write_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         source.aval, source.bval});
  }
  branch_to_next();
}

void SignalOperationLowerer::lower(const WriteUpdate& operation) {
  const auto signal_kind = signal_value_kinds.empty()
      ? ValueKind::logic4 : signal_value_kinds[operation.signal];
  const auto source = coerce_value_kind(
      builder, load_register(builder, registers, operation.source),
      signal_kind);
  if (begin_direct_update(operation.signal, 0U, source)
      && require_direct_update_slots) {
    return;
  }
  if (signal_kind == ValueKind::logic9) {
    store_logic9_word(logic9_word_slot, source);
    builder.CreateCall(
        write_logic9_type, write_update_logic9_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         logic9_word_slot});
  } else {
    builder.CreateCall(
        write_type, write_update_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         source.aval, source.bval});
  }
  branch_to_next();
}

void SignalOperationLowerer::lower(const WriteAfter& operation) {
  const auto signal_kind = signal_value_kinds.empty()
      ? ValueKind::logic4 : signal_value_kinds[operation.signal];
  const auto source = coerce_value_kind(
      builder, load_register(builder, registers, operation.source),
      signal_kind);
  if (signal_kind == ValueKind::logic9) {
    store_logic9_word(logic9_word_slot, source);
    builder.CreateCall(
        write_after_logic9_type, write_after_logic9_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         logic9_word_slot, constant_i64(context, operation.delay)});
  } else {
    builder.CreateCall(
        write_after_type, write_after_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         source.aval, source.bval, constant_i64(context, operation.delay)});
  }
  branch_to_next();
}

void SignalOperationLowerer::lower(const WriteBlockingSlice& operation) {
  const auto signal_kind = signal_value_kinds.empty()
      ? ValueKind::logic4 : signal_value_kinds[operation.signal];
  const auto source = coerce_value_kind(
      builder, load_register(builder, registers, operation.source),
      signal_kind);
  if (signal_kind == ValueKind::logic9) {
    store_logic9_word(logic9_word_slot, source);
    builder.CreateCall(
        write_slice_logic9_type, write_blocking_slice_logic9_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         llvm::ConstantInt::get(i32, operation.offset),
         llvm::ConstantInt::get(i32, source.width), logic9_word_slot});
  } else {
    builder.CreateCall(
        write_slice_type, write_blocking_slice_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         llvm::ConstantInt::get(i32, operation.offset),
         llvm::ConstantInt::get(i32, source.width), source.aval, source.bval});
  }
  branch_to_next();
}

void SignalOperationLowerer::lower(const WriteUpdateSlice& operation) {
  const auto signal_kind = signal_value_kinds.empty()
      ? ValueKind::logic4 : signal_value_kinds[operation.signal];
  const auto source = coerce_value_kind(
      builder, load_register(builder, registers, operation.source),
      signal_kind);
  if (begin_direct_update(operation.signal, operation.offset, source)
      && require_direct_update_slots) {
    return;
  }
  if (signal_kind == ValueKind::logic9) {
    store_logic9_word(logic9_word_slot, source);
    builder.CreateCall(
        write_slice_logic9_type, write_update_slice_logic9_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         llvm::ConstantInt::get(i32, operation.offset),
         llvm::ConstantInt::get(i32, source.width), logic9_word_slot});
  } else {
    builder.CreateCall(
        write_slice_type, write_update_slice_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         llvm::ConstantInt::get(i32, operation.offset),
         llvm::ConstantInt::get(i32, source.width), source.aval, source.bval});
  }
  branch_to_next();
}

void SignalOperationLowerer::lower(const WriteAfterSlice& operation) {
  const auto signal_kind = signal_value_kinds.empty()
      ? ValueKind::logic4 : signal_value_kinds[operation.signal];
  const auto source = coerce_value_kind(
      builder, load_register(builder, registers, operation.source),
      signal_kind);
  if (signal_kind == ValueKind::logic9) {
    store_logic9_word(logic9_word_slot, source);
    builder.CreateCall(
        write_after_slice_logic9_type, write_after_slice_logic9_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         llvm::ConstantInt::get(i32, operation.offset),
         llvm::ConstantInt::get(i32, source.width), logic9_word_slot,
         constant_i64(context, operation.delay)});
  } else {
    builder.CreateCall(
        write_after_slice_type, write_after_slice_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         llvm::ConstantInt::get(i32, operation.offset),
         llvm::ConstantInt::get(i32, source.width), source.aval, source.bval,
         constant_i64(context, operation.delay)});
  }
  branch_to_next();
}

void SignalOperationLowerer::lower(const ForceSignalSlice& operation) {
  const auto signal_kind = signal_value_kinds.empty()
      ? ValueKind::logic4 : signal_value_kinds[operation.signal];
  const auto source = coerce_value_kind(
      builder, load_register(builder, registers, operation.source),
      signal_kind);
  auto* offset = operation.selection
      ? builder.CreateTrunc(dynamic_offset(*operation.selection), i32)
      : llvm::ConstantInt::get(i32, operation.offset);
  if (signal_kind == ValueKind::logic9) {
    store_logic9_word(logic9_word_slot, source);
    builder.CreateCall(
        write_slice_logic9_type,
        operation.driving_value
            ? force_driver_signal_slice_logic9_callback
            : force_signal_slice_logic9_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         offset,
         llvm::ConstantInt::get(i32, source.width), logic9_word_slot});
  } else {
    builder.CreateCall(
        write_slice_type,
        operation.driving_value
            ? force_driver_signal_slice_callback
            : force_signal_slice_callback,
        {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
         offset,
         llvm::ConstantInt::get(i32, source.width), source.aval, source.bval});
  }
  branch_to_next();
}

void SignalOperationLowerer::lower(const ReleaseSignalSlice& operation) {
  auto* offset = operation.selection
      ? builder.CreateTrunc(dynamic_offset(*operation.selection), i32)
      : llvm::ConstantInt::get(i32, operation.offset);
  builder.CreateCall(
      release_slice_type,
      operation.driving_value
          ? release_driver_signal_slice_callback
          : release_signal_slice_callback,
      {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
       offset,
       llvm::ConstantInt::get(i32, operation.width)});
  branch_to_next();
}

void SignalOperationLowerer::lower(
    const WriteProjectedWaveform& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              if (signal_kind == ValueKind::logic9) {
                auto* array_type = llvm::ArrayType::get(
                    logic9_projected_element_type,
                    operation.elements.size());
                auto* storage = builder.CreateAlloca(
                    array_type,
                    nullptr,
                    "projected.logic9.waveform");
                for (std::size_t element_index = 0;
                     element_index < operation.elements.size();
                     ++element_index) {
                  const auto& element =
                      operation.elements[element_index];
                  auto source = coerce_value_kind(
                      builder,
                      load_register(
                          builder,
                          registers,
                          element.source),
                      ValueKind::logic9);
                  auto* slot = builder.CreateInBoundsGEP(
                      array_type,
                      storage,
                      {
                          llvm::ConstantInt::get(i32, 0),
                          llvm::ConstantInt::get(
                              i32,
                              static_cast<std::uint32_t>(
                                  element_index))});
                  store_logic9_word(
                      builder.CreateStructGEP(
                          logic9_projected_element_type,
                          slot,
                          0),
                      source);
                  builder.CreateStore(
                      constant_i64(context, element.delay),
                      builder.CreateStructGEP(
                          logic9_projected_element_type,
                          slot,
                          1));
                }
                const auto first = load_register(
                    builder,
                    registers,
                    operation.elements.front().source);
                builder.CreateCall(
                    write_projected_waveform_logic9_type,
                    write_projected_waveform_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        llvm::ConstantInt::get(i32, first.width),
                        storage,
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                operation.elements.size())),
                        constant_i64(
                            context, operation.rejection),
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                operation.mode))});
                branch_to_next();
                return;
              }
              auto* array_type = llvm::ArrayType::get(
                  projected_element_type, operation.elements.size());
              auto* storage = builder.CreateAlloca(
                  array_type, nullptr, "projected.waveform");
              for (std::size_t element_index = 0;
                   element_index < operation.elements.size();
                   ++element_index) {
                const auto& element =
                    operation.elements[element_index];
                const auto source =
                    load_register(builder, registers, element.source);
                auto* slot = builder.CreateInBoundsGEP(
                    array_type,
                    storage,
                    {llvm::ConstantInt::get(i32, 0),
                     llvm::ConstantInt::get(
                         i32,
                         static_cast<std::uint32_t>(element_index))});
                builder.CreateStore(
                    source.aval,
                    builder.CreateStructGEP(
                        projected_element_type, slot, 0));
                builder.CreateStore(
                    source.bval,
                    builder.CreateStructGEP(
                        projected_element_type, slot, 1));
                builder.CreateStore(
                    constant_i64(context, element.delay),
                    builder.CreateStructGEP(
                        projected_element_type, slot, 2));
              }
              const auto first = load_register(
                  builder, registers, operation.elements.front().source);
              builder.CreateCall(
                  write_projected_waveform_type,
                  write_projected_waveform_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
                      llvm::ConstantInt::get(i32, first.width),
                      storage,
                      llvm::ConstantInt::get(
                          i32,
                          static_cast<std::uint32_t>(
                              operation.elements.size())),
                      constant_i64(context, operation.rejection),
                      llvm::ConstantInt::get(
                          i32,
                          static_cast<std::uint32_t>(
                              operation.mode))});
              branch_to_next();
            
}

void SignalOperationLowerer::lower(
    const WriteProjected& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9
                  && operation.delay == 0U
                  && operation.rejection == 0U
                  && operation.mode
                      == runtime::simir::ProjectedDelayMode::inertial
                  && begin_direct_update(operation.signal, 0U, source)
                  && require_direct_update_slots) {
                return;
              }
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_projected_logic9_type,
                    write_projected_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot,
                        constant_i64(
                            context, operation.delay),
                        constant_i64(
                            context, operation.rejection),
                        llvm::ConstantInt::get(
                            i32,
                            static_cast<std::uint32_t>(
                                operation.mode))});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_projected_type,
                  write_projected_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
                      source.aval,
                      source.bval,
                      constant_i64(context, operation.delay),
                      constant_i64(context, operation.rejection),
                      llvm::ConstantInt::get(
                          i32,
                          static_cast<std::uint32_t>(
                              operation.mode))});
              branch_to_next();
            
}

void SignalOperationLowerer::lower(
    const WriteInertial& operation) {
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  signal_kind);
              if (signal_kind == ValueKind::logic9) {
                store_logic9_word(logic9_word_slot, source);
                builder.CreateCall(
                    write_inertial_logic9_type,
                    write_inertial_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot,
                        constant_i64(
                            context, operation.delays.rise),
                        constant_i64(
                            context, operation.delays.fall),
                        constant_i64(
                            context,
                            operation.delays.turnoff)});
                branch_to_next();
                return;
              }
              builder.CreateCall(
                  write_inertial_type,
                  write_inertial_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
                      source.aval,
                      source.bval,
                      constant_i64(context, operation.delays.rise),
                      constant_i64(context, operation.delays.fall),
                      constant_i64(
                          context, operation.delays.turnoff)});
              branch_to_next();
            
}

void SignalOperationLowerer::lower(
    const ReadSignal& operation) {
              const auto width = signal_widths[operation.signal];
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              if (signal_kind == ValueKind::logic9) {
                EncodedValue value;
                const auto direct = std::ranges::find(
                    direct_read_signals, operation.signal);
                if (direct != direct_read_signals.end()
                    && direct_signal_logic9_plane0 != nullptr
                    && direct_signal_logic9_plane1 != nullptr
                    && direct_signal_logic9_plane2 != nullptr
                    && direct_signal_logic9_plane3 != nullptr
                    && direct_read_signal_map != nullptr
                    && direct_read_signal_count != nullptr
                    && direct_signal_count != nullptr) {
                  auto* function = builder.GetInsertBlock()->getParent();
                  auto* map_block = llvm::BasicBlock::Create(
                      context, "read.logic9.direct.map", function);
                  auto* direct_block = llvm::BasicBlock::Create(
                      context, "read.logic9.direct", function);
                  auto* callback_block = llvm::BasicBlock::Create(
                      context, "read.logic9.callback", function);
                  auto* merge_block = llvm::BasicBlock::Create(
                      context, "read.logic9.merge", function);
                  const auto nonnull = [&](llvm::Value* pointer_value) {
                    return builder.CreateICmpNE(
                        pointer_value,
                        llvm::ConstantPointerNull::get(
                            llvm::cast<llvm::PointerType>(
                                pointer_value->getType())));
                  };
                  const auto slot = static_cast<std::uint32_t>(
                      std::distance(direct_read_signals.begin(), direct));
                  auto* pointers_available = builder.CreateAnd(
                      builder.CreateAnd(
                          nonnull(direct_signal_logic9_plane0),
                          nonnull(direct_signal_logic9_plane1)),
                      builder.CreateAnd(
                          nonnull(direct_signal_logic9_plane2),
                          nonnull(direct_signal_logic9_plane3)));
                  pointers_available = builder.CreateAnd(
                      pointers_available, nonnull(direct_read_signal_map));
                  builder.CreateCondBr(
                      builder.CreateAnd(
                          pointers_available,
                          builder.CreateICmpULT(
                              llvm::ConstantInt::get(i32, slot),
                              direct_read_signal_count)),
                      map_block,
                      callback_block);

                  builder.SetInsertPoint(map_block);
                  auto* actual = builder.CreateLoad(
                      i32,
                      builder.CreateInBoundsGEP(
                          i32,
                          direct_read_signal_map,
                          llvm::ConstantInt::get(i32, slot)),
                      "read.logic9.actual");
                  builder.CreateCondBr(
                      builder.CreateICmpULT(actual, direct_signal_count),
                      direct_block,
                      callback_block);

                  builder.SetInsertPoint(direct_block);
                  const auto load_plane = [&](llvm::Value* planes,
                                               const char* name) {
                    return builder.CreateLoad(
                        i64,
                        builder.CreateInBoundsGEP(i64, planes, actual),
                        name);
                  };
                  std::array<llvm::Value*, 4> direct_planes {
                      load_plane(
                          direct_signal_logic9_plane0,
                          "read.logic9.direct.plane0"),
                      load_plane(
                          direct_signal_logic9_plane1,
                          "read.logic9.direct.plane1"),
                      load_plane(
                          direct_signal_logic9_plane2,
                          "read.logic9.direct.plane2"),
                      load_plane(
                          direct_signal_logic9_plane3,
                          "read.logic9.direct.plane3")
                  };
                  builder.CreateBr(merge_block);
                  auto* direct_end = builder.GetInsertBlock();

                  builder.SetInsertPoint(callback_block);
                  builder.CreateCall(
                      read_logic9_type,
                      read_logic9_callback,
                      { context_pointer,
                          llvm::ConstantInt::get(i32, operation.signal),
                          logic9_word_slot });
                  const auto callback_value = load_logic9_word(
                      logic9_word_slot, width);
                  std::array<llvm::Value*, 4> callback_planes {
                      callback_value.aval,
                      callback_value.bval,
                      callback_value.logic9_plane2,
                      callback_value.logic9_plane3
                  };
                  builder.CreateBr(merge_block);
                  auto* callback_end = builder.GetInsertBlock();

                  builder.SetInsertPoint(merge_block);
                  std::array<llvm::Value*, 4> merged_planes { };
                  for (std::size_t plane = 0; plane < 4U; ++plane) {
                    auto* merged = builder.CreatePHI(
                        i64, 2, "read.logic9.plane");
                    merged->addIncoming(direct_planes[plane], direct_end);
                    merged->addIncoming(callback_planes[plane], callback_end);
                    merged_planes[plane] = merged;
                  }
                  value = EncodedValue {
                      merged_planes[0],
                      merged_planes[1],
                      width,
                      merged_planes[2],
                      merged_planes[3],
                      ValueKind::logic9
                  };
                } else {
                  builder.CreateCall(
                      read_logic9_type,
                      read_logic9_callback,
                      { context_pointer,
                          llvm::ConstantInt::get(i32, operation.signal),
                          logic9_word_slot });
                  value = load_logic9_word(logic9_word_slot, width);
                }
                auto* mask =
                    constant_i64(context, width_mask(width));
                value.aval = builder.CreateAnd(value.aval, mask);
                value.bval = builder.CreateAnd(value.bval, mask);
                value.logic9_plane2 =
                    builder.CreateAnd(value.logic9_plane2, mask);
                value.logic9_plane3 =
                    builder.CreateAnd(value.logic9_plane3, mask);
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    value);
                branch_to_next();
                return;
              }
              llvm::Value* aval = nullptr;
              llvm::Value* bval = nullptr;
              const auto direct = std::ranges::find(
                  direct_read_signals, operation.signal);
              if (direct != direct_read_signals.end()
                  && direct_signal_aval != nullptr
                  && direct_signal_bval != nullptr
                  && direct_read_signal_map != nullptr
                  && direct_read_signal_count != nullptr
                  && direct_signal_count != nullptr) {
                auto* function = builder.GetInsertBlock()->getParent();
                auto* direct_block = llvm::BasicBlock::Create(
                    context, "read.direct", function);
                auto* callback_block = llvm::BasicBlock::Create(
                    context, "read.callback", function);
                auto* merge_block = llvm::BasicBlock::Create(
                    context, "read.merge", function);
                const auto nonnull = [&](llvm::Value* value) {
                  return builder.CreateICmpNE(
                      value,
                      llvm::ConstantPointerNull::get(
                          llvm::cast<llvm::PointerType>(value->getType())));
                };
                const auto slot = static_cast<std::uint32_t>(
                    std::distance(direct_read_signals.begin(), direct));
                builder.CreateCondBr(
                    builder.CreateAnd(
                        builder.CreateAnd(
                            builder.CreateAnd(
                                nonnull(direct_signal_aval),
                                nonnull(direct_signal_bval)),
                            nonnull(direct_read_signal_map)),
                        builder.CreateICmpULT(
                            llvm::ConstantInt::get(i32, slot),
                            direct_read_signal_count)),
                    direct_block,
                    callback_block);

                builder.SetInsertPoint(direct_block);
                auto* actual = builder.CreateLoad(
                    i32,
                    builder.CreateInBoundsGEP(
                        i32,
                        direct_read_signal_map,
                        llvm::ConstantInt::get(i32, slot)),
                    "read.actual");
                auto* direct_value_block = llvm::BasicBlock::Create(
                    context, "read.direct.value", function);
                builder.CreateCondBr(
                    builder.CreateICmpULT(actual, direct_signal_count),
                    direct_value_block,
                    callback_block);
                builder.SetInsertPoint(direct_value_block);
                auto* direct_aval = builder.CreateLoad(
                    i64,
                    builder.CreateInBoundsGEP(
                        i64, direct_signal_aval, actual),
                    "read.direct.aval");
                auto* direct_bval = builder.CreateLoad(
                    i64,
                    builder.CreateInBoundsGEP(
                        i64, direct_signal_bval, actual),
                    "read.direct.bval");
                builder.CreateBr(merge_block);
                auto* direct_end = builder.GetInsertBlock();

                builder.SetInsertPoint(callback_block);
                builder.CreateStore(
                    constant_i64(context, 0), read_bval_slot);
                auto* callback_aval = builder.CreateCall(
                    read_type,
                    read_callback,
                    { context_pointer,
                        llvm::ConstantInt::get(i32, operation.signal),
                        read_bval_slot },
                    "read.callback.aval");
                auto* callback_bval = builder.CreateLoad(
                    i64, read_bval_slot, "read.callback.bval");
                builder.CreateBr(merge_block);
                auto* callback_end = builder.GetInsertBlock();

                builder.SetInsertPoint(merge_block);
                auto* aval_phi = builder.CreatePHI(i64, 2, "aval");
                aval_phi->addIncoming(direct_aval, direct_end);
                aval_phi->addIncoming(callback_aval, callback_end);
                auto* bval_phi = builder.CreatePHI(i64, 2, "bval.value");
                bval_phi->addIncoming(direct_bval, direct_end);
                bval_phi->addIncoming(callback_bval, callback_end);
                aval = aval_phi;
                bval = bval_phi;
              } else {
                builder.CreateStore(
                    constant_i64(context, 0), read_bval_slot);
                aval = builder.CreateCall(
                    read_type,
                    read_callback,
                    { context_pointer,
                        llvm::ConstantInt::get(i32, operation.signal),
                        read_bval_slot },
                    "aval");
                bval = builder.CreateLoad(
                    i64, read_bval_slot, "bval.value");
              }
              auto *mask = constant_i64(context, width_mask(width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      width});
              branch_to_next();
            
}

void SignalOperationLowerer::lower(
    const SignalEvent& operation) {
              auto* active = builder.CreateCall(
                  signal_event_type,
                  signal_event_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(
                          i32, operation.signal)},
                  "signal.event");
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(active, i64),
                      constant_i64(context, 0),
                      1});
              branch_to_next();
            
}

void SignalOperationLowerer::lower(
    const SignalLastValue& operation) {
              const auto width = signal_widths[operation.signal];
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              if (signal_kind == ValueKind::logic9) {
                builder.CreateCall(
                    read_logic9_type,
                    signal_last_value_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot});
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    load_logic9_word(logic9_word_slot, width));
                branch_to_next();
                return;
              }
              builder.CreateStore(
                  constant_i64(context, 0), read_bval_slot);
              auto* aval = builder.CreateCall(
                  signal_last_value_type,
                  signal_last_value_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
                      read_bval_slot},
                  "signal.last_value.aval");
              auto* bval = builder.CreateLoad(
                  i64, read_bval_slot, "signal.last_value.bval");
              auto* mask = constant_i64(context, width_mask(width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      width});
              branch_to_next();
            
}

void SignalOperationLowerer::lower(
    const SignalLastEvent& operation) {
              auto* elapsed = builder.CreateCall(
                  signal_last_event_type,
                  signal_last_event_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal)},
                  "signal.last_event");
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      elapsed,
                      constant_i64(context, 0),
                      64});
              branch_to_next();
            
}

void SignalOperationLowerer::lower(
    const ReadSimulationTime& operation) {
  auto* now = builder.CreateCall(
      read_simulation_time_type,
      read_simulation_time_callback,
      {context_pointer},
      "simulation.time");
  store_register(
      builder, registers, operation.destination,
      EncodedValue{now, constant_i64(context, 0), 64});
  branch_to_next();
}

void SignalOperationLowerer::lower(
    const VitalTimingCheck& operation) {
  auto* ordinal = builder.CreateCall(
      vital_timing_check_type,
      vital_timing_check_callback,
      {context_pointer,
       llvm::ConstantInt::get(i32, process_id),
       llvm::ConstantInt::get(i32, instruction)},
      "vital.timing");
  const auto plane = [&](const unsigned shift) {
    return builder.CreateZExt(
        builder.CreateAnd(
            builder.CreateLShr(
                ordinal, llvm::ConstantInt::get(i32, shift)),
            llvm::ConstantInt::get(i32, 1)),
        llvm::Type::getInt64Ty(context));
  };
  store_register(
      builder, registers, operation.destination,
      EncodedValue{
          plane(0), plane(1), 1, plane(2), plane(3), ValueKind::logic9});
  branch_to_next();
}

void SignalOperationLowerer::lower(const VitalDelay&) {
  builder.CreateCall(
      vital_delay_type,
      vital_delay_callback,
      {context_pointer,
       llvm::ConstantInt::get(i32, process_id),
       llvm::ConstantInt::get(i32, instruction)});
  branch_to_next();
}

void SignalOperationLowerer::lower(
    const SignalActive& operation) {
              auto* active = builder.CreateCall(
                  signal_active_type,
                  signal_active_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal)},
                  "signal.active");
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(active, i64),
                      constant_i64(context, 0),
                      1});
              branch_to_next();
            
}

void SignalOperationLowerer::lower(
    const SignalLastActive& operation) {
              auto* elapsed = builder.CreateCall(
                  signal_last_active_type,
                  signal_last_active_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal)},
                  "signal.last_active");
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      elapsed,
                      constant_i64(context, 0),
                      64});
              branch_to_next();
}

void SignalOperationLowerer::lower(
    const SignalDriving& operation) {
              auto* driving = builder.CreateCall(
                  signal_driving_type,
                  signal_driving_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal)},
                  "signal.driving");
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(driving, i64),
                      constant_i64(context, 0),
                      1});
              branch_to_next();
}

void SignalOperationLowerer::lower(
    const SignalDrivingValue& operation) {
              const auto width = signal_widths[operation.signal];
              const auto signal_kind =
                  signal_value_kinds.empty()
                      ? ValueKind::logic4
                      : signal_value_kinds[operation.signal];
              if (signal_kind == ValueKind::logic9) {
                builder.CreateCall(
                    read_logic9_type,
                    signal_driving_value_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot});
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    load_logic9_word(logic9_word_slot, width));
                branch_to_next();
                return;
              }
              builder.CreateStore(
                  constant_i64(context, 0), read_bval_slot);
              auto* aval = builder.CreateCall(
                  signal_driving_value_type,
                  signal_driving_value_callback,
                  {
                      context_pointer,
                      llvm::ConstantInt::get(i32, operation.signal),
                      read_bval_slot},
                  "signal.driving_value.aval");
              auto* bval = builder.CreateLoad(
                  i64, read_bval_slot, "signal.driving_value.bval");
              auto* mask = constant_i64(context, width_mask(width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      width});
              branch_to_next();
}


}  // namespace fsim::compiler::llvm_detail
