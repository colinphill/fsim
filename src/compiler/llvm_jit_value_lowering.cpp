// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/ADT/APInt.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/ErrorHandling.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>

namespace fsim::compiler::llvm_detail {

using runtime::Logic9;
using runtime::simir::BinaryOperator;
using runtime::simir::RegisterId;
using runtime::simir::ShiftOperator;
using runtime::simir::ValueKind;

[[nodiscard]] llvm::IntegerType* packed_integer_type(
    llvm::LLVMContext& context,
    const std::uint32_t width)
{
    return llvm::IntegerType::get(context, std::max(width, 64U));
}

[[nodiscard]] llvm::ConstantInt* packed_constant(
    llvm::LLVMContext& context,
    const std::uint32_t width,
    const std::uint64_t value)
{
    return llvm::ConstantInt::get(packed_integer_type(context, width), value);
}

[[nodiscard]] llvm::ConstantInt* packed_mask(
    llvm::LLVMContext& context,
    const std::uint32_t width)
{
    return packed_low_mask(context, width, width);
}

[[nodiscard]] llvm::Value* lower_power(
    llvm::IRBuilder<>& builder,
    llvm::Value* base,
    llvm::Value* exponent,
    llvm::ConstantInt* mask,
    llvm::ConstantInt* zero,
    llvm::ConstantInt* one,
    const std::uint32_t width)
{
    constexpr std::uint32_t runtime_loop_min_width = 128U;
    if (width < runtime_loop_min_width) {
        llvm::Value* powered = one;
        llvm::Value* factor = base;
        for (std::uint32_t bit = 0; bit < width; ++bit) {
            auto* selected = builder.CreateICmpNE(
                builder.CreateAnd(
                    builder.CreateLShr(
                        exponent,
                        packed_constant(builder.getContext(), width, bit)),
                    one),
                zero);
            powered = builder.CreateSelect(
                selected,
                builder.CreateAnd(
                    builder.CreateMul(powered, factor), mask),
                powered);
            if (bit + 1U < width) {
                factor = builder.CreateAnd(
                    builder.CreateMul(factor, factor), mask);
            }
        }
        return powered;
    }

    auto* const preheader = builder.GetInsertBlock();
    auto* const function = preheader->getParent();
    auto& context = builder.getContext();
    auto* const header = llvm::BasicBlock::Create(
        context, "power.loop.header", function);
    auto* const body = llvm::BasicBlock::Create(
        context, "power.loop.body", function);
    auto* const exit = llvm::BasicBlock::Create(
        context, "power.loop.exit", function);
    builder.CreateBr(header);

    builder.SetInsertPoint(header);
    auto* const remaining = builder.CreatePHI(
        exponent->getType(), 2U, "power.loop.remaining");
    remaining->addIncoming(exponent, preheader);
    auto* const powered = builder.CreatePHI(
        base->getType(), 2U, "power.loop.result");
    powered->addIncoming(one, preheader);
    auto* const factor = builder.CreatePHI(
        base->getType(), 2U, "power.loop.factor");
    factor->addIncoming(base, preheader);
    builder.CreateCondBr(
        builder.CreateICmpNE(remaining, zero), body, exit);

    builder.SetInsertPoint(body);
    auto* const selected = builder.CreateICmpNE(
        builder.CreateAnd(remaining, one), zero);
    auto* const multiplied = builder.CreateAnd(
        builder.CreateMul(powered, factor), mask);
    auto* const next_powered = builder.CreateSelect(
        selected, multiplied, powered);
    auto* const next_factor = builder.CreateAnd(
        builder.CreateMul(factor, factor), mask);
    auto* const next_remaining = builder.CreateLShr(remaining, one);
    builder.CreateBr(header);
    remaining->addIncoming(next_remaining, body);
    powered->addIncoming(next_powered, body);
    factor->addIncoming(next_factor, body);

    builder.SetInsertPoint(exit);
    return powered;
}

[[nodiscard]] llvm::ConstantInt* packed_low_mask(
    llvm::LLVMContext& context,
    const std::uint32_t storage_width,
    const std::uint32_t active_width)
{
    assert(active_width <= std::max(storage_width, 64U));
    const auto integer_width = std::max(storage_width, 64U);
    return llvm::ConstantInt::get(
        context, llvm::APInt::getLowBitsSet(integer_width, active_width));
}

[[nodiscard]] EncodedValue canonicalize_logic9_value(
    llvm::IRBuilder<>& builder,
    EncodedValue value)
{
    if (value.kind != ValueKind::logic9) {
        return value;
    }

    auto& context = builder.getContext();
    auto* const mask = packed_mask(context, value.width);
    auto* const zero = packed_constant(context, value.width, 0U);
    auto* plane0 = builder.CreateAnd(value.aval, mask);
    auto* plane1 = builder.CreateAnd(value.bval, mask);
    auto* plane2 = value.logic9_plane2 == nullptr
        ? zero
        : builder.CreateAnd(value.logic9_plane2, mask);
    auto* plane3 = value.logic9_plane3 == nullptr
        ? zero
        : builder.CreateAnd(value.logic9_plane3, mask);

    // Ordinals 9..15 are reserved and read as X by the runtime. Their raw
    // plane representation must not leak into generated Logic9 operations.
    auto* const reserved = builder.CreateAnd(
        plane3, builder.CreateOr(plane0, builder.CreateOr(plane1, plane2)));
    return {
        builder.CreateAnd(builder.CreateOr(plane0, reserved), mask),
        builder.CreateAnd(plane1, builder.CreateNot(reserved)),
        value.width,
        builder.CreateAnd(plane2, builder.CreateNot(reserved)),
        builder.CreateAnd(plane3, builder.CreateNot(reserved)),
        ValueKind::logic9
    };
}

[[nodiscard]] ShiftOperator reverse_shift(
    const ShiftOperator operation) noexcept {
  switch (operation) {
  case ShiftOperator::logical_left:
    return ShiftOperator::logical_right;
  case ShiftOperator::logical_right:
    return ShiftOperator::logical_left;
  case ShiftOperator::arithmetic_left:
    return ShiftOperator::arithmetic_right;
  case ShiftOperator::arithmetic_right:
    return ShiftOperator::arithmetic_left;
  case ShiftOperator::rotate_left:
    return ShiftOperator::rotate_right;
  case ShiftOperator::rotate_right:
    return ShiftOperator::rotate_left;
  }
  return operation;
}

[[nodiscard]] static llvm::Value* register_slot_pointer(
    llvm::IRBuilder<>& builder, llvm::Type* element_type,
    llvm::Value* base, const std::uint64_t offset, const char* name)
{
    if (offset == 0U) {
        return base;
    }
    return builder.CreateGEP(element_type, base,
        constant_i64(builder.getContext(), offset), name);
}

[[nodiscard]] EncodedValue
load_register(llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    const RegisterId id)
{
    const auto& slot = registers[id];
    auto& context = builder.getContext();
    auto* i64 = llvm::Type::getInt64Ty(context);
    auto* aval_pointer = register_slot_pointer(
        builder, i64, slot.aval_base, slot.word_offset,
        "register.aval.pointer");
    auto* bval_pointer = register_slot_pointer(
        builder, i64, slot.bval_base, slot.word_offset,
        "register.bval.pointer");
    auto* integer = packed_integer_type(context, slot.width);
    auto* storage_integer = packed_integer_type(
        context, ((slot.width + 63U) / 64U) * 64U);
    llvm::Value* zero = llvm::ConstantInt::get(integer, 0);
    const auto forwarded_plane = [&](const std::size_t plane,
                                     llvm::Value* pointer,
                                     const char* name) -> llvm::Value* {
        if (slot.constant_planes != nullptr
            && slot.constant_planes->published[plane] != nullptr) {
            ++slot.constant_planes->forwarded_loads;
            return slot.constant_planes->published[plane];
        }
        // Slots and stores use whole words. Loading the same storage type
        // keeps local slots promotable and discards padding only afterwards.
        auto* loaded = builder.CreateLoad(storage_integer, pointer, name);
        loaded->setAlignment(llvm::Align { 8 });
        return builder.CreateTruncOrBitCast(loaded, integer);
    };
    auto* aval = forwarded_plane(0, aval_pointer, "register.aval");
    auto* bval = slot.known_logic4
        ? llvm::ConstantInt::get(integer, 0)
        : forwarded_plane(1, bval_pointer, "register.bval");
    llvm::Value* logic9_plane2 = zero;
    llvm::Value* logic9_plane3 = zero;
    if (slot.kind == ValueKind::logic9) {
        auto* plane2_pointer = register_slot_pointer(
            builder, i64, slot.logic9_plane2_base, slot.word_offset,
            "register.logic9.plane2.pointer");
        auto* plane3_pointer = register_slot_pointer(
            builder, i64, slot.logic9_plane3_base, slot.word_offset,
            "register.logic9.plane3.pointer");
        auto* plane2 = forwarded_plane(
            2, plane2_pointer, "register.logic9.plane2");
        auto* plane3 = forwarded_plane(
            3, plane3_pointer, "register.logic9.plane3");
        logic9_plane2 = plane2;
        logic9_plane3 = plane3;
    }
    return canonicalize_logic9_value(
        builder,
        EncodedValue {
            aval,
            bval,
            slot.width,
            logic9_plane2,
            logic9_plane3,
            slot.kind,
        });
}

[[nodiscard]] EncodedValue coerce_value_kind(
    llvm::IRBuilder<>& builder,
    EncodedValue value,
    const ValueKind destination_kind) {
  if (value.kind == destination_kind) {
    return value;
  }
  auto* zero = packed_constant(builder.getContext(), value.width, 0);
  if (value.logic9_plane2 == nullptr) {
    value.logic9_plane2 = zero;
  }
  if (value.logic9_plane3 == nullptr) {
    value.logic9_plane3 = zero;
  }
  auto* mask = packed_mask(builder.getContext(), value.width);
  if (destination_kind == ValueKind::logic9) {
    auto* plane0 = builder.CreateAnd(value.aval, mask);
    auto* plane1 = builder.CreateAnd(
        builder.CreateNot(value.bval), mask);
    auto* plane2 = builder.CreateAnd(
        builder.CreateAnd(
            builder.CreateNot(value.aval), value.bval),
        mask);
    return {
        plane0,
        plane1,
        value.width,
        plane2,
        zero,
        ValueKind::logic9};
  }

  // With the ordinal Logic9 encoding, p1 identifies 0/1/L/H, while p3
  // identifies '-' and every reserved code. Reserved codes still map to X;
  // these expressions do not assume they are unreachable.
  auto* aval = builder.CreateAnd(
      builder.CreateOr(
          builder.CreateOr(value.aval, value.logic9_plane3),
          builder.CreateNot(builder.CreateOr(
              value.bval, value.logic9_plane2))),
      mask);
  auto* bval = builder.CreateAnd(
      builder.CreateOr(value.logic9_plane3, builder.CreateNot(value.bval)),
      mask);
  return { aval, bval, value.width, zero, zero, ValueKind::logic4 };

}

[[nodiscard]] llvm::Value* logic9_state_mask(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value,
    const std::uint8_t state)
{
    llvm::Value* selected = packed_mask(builder.getContext(), value.width);
    const std::array planes {
        value.aval,
        value.bval,
        value.logic9_plane2,
        value.logic9_plane3
    };
    for (std::size_t plane = 0; plane < planes.size(); ++plane) {
        selected = builder.CreateAnd(
            selected,
            ((state >> plane) & 1U) != 0
                ? planes[plane]
                : builder.CreateNot(planes[plane]));
    }
    return selected;
}

[[nodiscard]] EncodedValue lower_logic9_not(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value)
{
    auto* mask = packed_mask(builder.getContext(), value.width);
    auto* zero = packed_constant(builder.getContext(), value.width, 0);
    // 0/1/L/H have p1 set and p3 clear. U is the all-zero encoding;
    // every other non-known state (including reserved codes) produces X.
    auto* known = builder.CreateAnd(
        value.bval, builder.CreateNot(value.logic9_plane3));
    auto* non_u = builder.CreateOr(
        builder.CreateOr(value.aval, value.bval),
        builder.CreateOr(value.logic9_plane2, value.logic9_plane3));
    auto* plane0 = builder.CreateAnd(
        builder.CreateAnd(non_u,
            builder.CreateNot(builder.CreateAnd(known, value.aval))),
        mask);
    auto* plane1 = builder.CreateAnd(known, mask);
    return { plane0, plane1, value.width, zero, zero, ValueKind::logic9 };
}

[[nodiscard]] EncodedValue lower_logic9_binary(
    llvm::IRBuilder<>& builder,
    const EncodedValue& lhs,
    const EncodedValue& rhs,
    const BinaryOperator operation)
{
    assert(lhs.width == rhs.width);
    assert(operation == BinaryOperator::bit_and
        || operation == BinaryOperator::bit_or
        || operation == BinaryOperator::bit_xor);

    struct LogicClasses {
        llvm::Value* u;
        llvm::Value* zero;
        llvm::Value* one;
    };

    auto* mask = packed_mask(builder.getContext(), lhs.width);
    auto* zero = packed_constant(builder.getContext(), lhs.width, 0);
    const auto classes = [&](const EncodedValue& value) {
        auto* not_plane3 = builder.CreateNot(value.logic9_plane3);
        auto* strong_or_weak_zero = builder.CreateAnd(
            builder.CreateAnd(value.bval, builder.CreateNot(value.aval)),
            not_plane3);
        auto* strong_or_weak_one = builder.CreateAnd(
            builder.CreateAnd(value.bval, value.aval), not_plane3);
        auto* any_plane = builder.CreateOr(
            builder.CreateOr(value.aval, value.bval),
            builder.CreateOr(
                value.logic9_plane2, value.logic9_plane3));
        return LogicClasses {
            builder.CreateAnd(builder.CreateNot(any_plane), mask),
            builder.CreateAnd(strong_or_weak_zero, mask),
            builder.CreateAnd(strong_or_weak_one, mask)
        };
    };
    const auto left = classes(lhs);
    const auto right = classes(rhs);

    llvm::Value* result_zero = nullptr;
    llvm::Value* result_one = nullptr;
    llvm::Value* result_u = nullptr;
    if (operation == BinaryOperator::bit_and) {
        result_zero = builder.CreateOr(left.zero, right.zero);
        result_one = builder.CreateAnd(left.one, right.one);
        result_u = builder.CreateAnd(
            builder.CreateNot(result_zero),
            builder.CreateOr(left.u, right.u));
    } else if (operation == BinaryOperator::bit_or) {
        result_zero = builder.CreateAnd(left.zero, right.zero);
        result_one = builder.CreateOr(left.one, right.one);
        result_u = builder.CreateAnd(
            builder.CreateNot(result_one),
            builder.CreateOr(left.u, right.u));
    } else {
        result_zero = builder.CreateOr(
            builder.CreateAnd(left.zero, right.zero),
            builder.CreateAnd(left.one, right.one));
        result_one = builder.CreateOr(
            builder.CreateAnd(left.zero, right.one),
            builder.CreateAnd(left.one, right.zero));
        result_u = builder.CreateOr(left.u, right.u);
    }

    return {
        builder.CreateAnd(builder.CreateNot(
            builder.CreateOr(result_zero, result_u)), mask),
        builder.CreateAnd(builder.CreateOr(result_zero, result_one), mask),
        lhs.width,
        zero,
        zero,
        ValueKind::logic9
    };
}

void store_register(llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    const RegisterId id, EncodedValue value)
{
    const auto& slot = registers[id];
    auto& context = builder.getContext();
    auto* i64 = llvm::Type::getInt64Ty(context);
    auto* aval_pointer = register_slot_pointer(
        builder, i64, slot.aval_base, slot.word_offset,
        "register.aval.pointer");
    auto* bval_pointer = register_slot_pointer(
        builder, i64, slot.bval_base, slot.word_offset,
        "register.bval.pointer");
    value = coerce_value_kind(builder, value, slot.kind);
    value = canonicalize_logic9_value(builder, value);
    if (slot.constant_planes != nullptr
        && slot.constant_planes->active) {
        auto& forwarding = *slot.constant_planes;
        ++forwarding.stores;
        const auto literal_i64 = [](llvm::Value* plane) {
            auto* literal = llvm::dyn_cast_or_null<llvm::ConstantInt>(plane);
            return literal != nullptr
                    && literal->getType()->isIntegerTy(64)
                ? literal
                : nullptr;
        };
        forwarding.pending = {
            literal_i64(value.aval),
            literal_i64(value.bval),
            slot.kind == ValueKind::logic9
                ? literal_i64(value.logic9_plane2)
                : nullptr,
            slot.kind == ValueKind::logic9
                ? literal_i64(value.logic9_plane3)
                : nullptr,
        };
    }
    if (slot.width > 64U && slot.width % 64U != 0U) {
        auto* const storage_type = packed_integer_type(
            context, ((slot.width + 63U) / 64U) * 64U);
        value.aval = builder.CreateZExt(value.aval, storage_type);
        value.bval = builder.CreateZExt(value.bval, storage_type);
        if (slot.kind == ValueKind::logic9) {
            value.logic9_plane2 = builder.CreateZExt(
                value.logic9_plane2, storage_type);
            value.logic9_plane3 = builder.CreateZExt(
                value.logic9_plane3, storage_type);
        }
    }
    auto* aval = builder.CreateStore(value.aval, aval_pointer);
    auto* bval = builder.CreateStore(value.bval, bval_pointer);
    aval->setAlignment(llvm::Align { 8 });
    bval->setAlignment(llvm::Align { 8 });
    const auto mark_safe_frame_store = [&](llvm::StoreInst* const store) {
        store->setMetadata(
            context.getMDKindID(kTieredSafeFrameStoreMetadata),
            llvm::MDNode::get(context, llvm::ArrayRef<llvm::Metadata*> { }));
    };
    mark_safe_frame_store(aval);
    mark_safe_frame_store(bval);
    if (slot.kind == ValueKind::logic9) {
        auto* plane2_pointer = register_slot_pointer(
            builder, i64, slot.logic9_plane2_base, slot.word_offset,
            "register.logic9.plane2.pointer");
        auto* plane3_pointer = register_slot_pointer(
            builder, i64, slot.logic9_plane3_base, slot.word_offset,
            "register.logic9.plane3.pointer");
        auto* logic9_plane2 = builder.CreateStore(
            value.logic9_plane2, plane2_pointer);
        auto* logic9_plane3 = builder.CreateStore(
            value.logic9_plane3, plane3_pointer);
        logic9_plane2->setAlignment(llvm::Align { 8 });
        logic9_plane3->setAlignment(llvm::Align { 8 });
        mark_safe_frame_store(logic9_plane2);
        mark_safe_frame_store(logic9_plane3);
    }
    if (slot.initialized_base != nullptr) {
        auto* initialized_pointer = register_slot_pointer(
            builder, llvm::Type::getInt8Ty(context),
            slot.initialized_base, slot.index,
            "register.initialized.pointer");
        auto* const initialized_store = builder.CreateStore(
            llvm::ConstantInt::get(
                llvm::Type::getInt8Ty(context), 1),
            initialized_pointer);
        mark_safe_frame_store(initialized_store);
    }
}

[[nodiscard]] std::uint64_t width_mask(const std::uint32_t width) noexcept
{
    return width == 64 ? ~std::uint64_t { 0 }
                       : (std::uint64_t { 1 } << width) - 1U;
}

[[nodiscard]] llvm::ConstantInt* constant_i64(llvm::LLVMContext& context,
    const std::uint64_t value)
{
    return llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), value);
}

[[nodiscard]] EncodedBit bit_at(llvm::IRBuilder<>& builder,
    const EncodedValue value,
    const std::uint32_t bit)
{
    auto& context = builder.getContext();
    auto* shift = packed_constant(context, value.width, bit);
    auto* aval = builder.CreateTrunc(builder.CreateLShr(value.aval, shift),
        llvm::Type::getInt1Ty(context));
    auto* bval = builder.CreateTrunc(builder.CreateLShr(value.bval, shift),
        llvm::Type::getInt1Ty(context));
    return { aval, bval };
}

[[nodiscard]] EncodedBit truth_bit(
    llvm::IRBuilder<>& builder,
    const EncodedValue value) {
  auto& context = builder.getContext();
  auto* mask = packed_mask(context, value.width);
  auto* known_ones = builder.CreateAnd(
      builder.CreateAnd(value.aval, mask),
      builder.CreateNot(value.bval));
  auto* has_one = builder.CreateICmpNE(
      known_ones, packed_constant(context, value.width, 0));
  auto* has_unknown = builder.CreateICmpNE(
      builder.CreateAnd(value.bval, mask),
      packed_constant(context, value.width, 0));
  auto* unknown =
      builder.CreateAnd(builder.CreateNot(has_one), has_unknown);
  return {builder.CreateOr(has_one, unknown), unknown};
}

[[nodiscard]] EncodedBit bit_xor(llvm::IRBuilder<> &builder,
                                 const EncodedBit lhs,
                                 const EncodedBit rhs) {
  auto *known = builder.CreateAnd(builder.CreateNot(lhs.bval),
                                  builder.CreateNot(rhs.bval));
  auto *unknown = builder.CreateNot(known);
  auto *known_value =
      builder.CreateAnd(builder.CreateXor(lhs.aval, rhs.aval), known);
  return {builder.CreateOr(known_value, unknown), unknown};
}

[[nodiscard]] EncodedBit bit_and(llvm::IRBuilder<> &builder,
                                 const EncodedBit lhs,
                                 const EncodedBit rhs) {
  auto *lhs_known = builder.CreateNot(lhs.bval);
  auto *rhs_known = builder.CreateNot(rhs.bval);
  auto *lhs_zero =
      builder.CreateAnd(builder.CreateNot(lhs.aval), lhs_known);
  auto *rhs_zero =
      builder.CreateAnd(builder.CreateNot(rhs.aval), rhs_known);
  auto *known_zero = builder.CreateOr(lhs_zero, rhs_zero);
  auto *lhs_one = builder.CreateAnd(lhs.aval, lhs_known);
  auto *rhs_one = builder.CreateAnd(rhs.aval, rhs_known);
  auto *known_one = builder.CreateAnd(lhs_one, rhs_one);
  auto *unknown =
      builder.CreateNot(builder.CreateOr(known_zero, known_one));
  return {builder.CreateOr(known_one, unknown), unknown};
}

[[nodiscard]] EncodedBit bit_or(llvm::IRBuilder<> &builder,
                                const EncodedBit lhs,
                                const EncodedBit rhs) {
  auto *lhs_known = builder.CreateNot(lhs.bval);
  auto *rhs_known = builder.CreateNot(rhs.bval);
  auto *lhs_one = builder.CreateAnd(lhs.aval, lhs_known);
  auto *rhs_one = builder.CreateAnd(rhs.aval, rhs_known);
  auto *known_one = builder.CreateOr(lhs_one, rhs_one);
  auto *lhs_zero =
      builder.CreateAnd(builder.CreateNot(lhs.aval), lhs_known);
  auto *rhs_zero =
      builder.CreateAnd(builder.CreateNot(rhs.aval), rhs_known);
  auto *known_zero = builder.CreateAnd(lhs_zero, rhs_zero);
  auto *unknown =
      builder.CreateNot(builder.CreateOr(known_zero, known_one));
  return {builder.CreateOr(known_one, unknown), unknown};
}

[[nodiscard]] EncodedValue lower_binary(llvm::IRBuilder<>& builder,
    const BinaryOperator operation,
    const EncodedValue lhs,
    const EncodedValue rhs)
{
    auto& context = builder.getContext();
    auto* mask = packed_mask(context, lhs.width);
    auto* zero = packed_constant(context, lhs.width, 0);
    auto* one = packed_constant(context, lhs.width, 1);
    switch (operation) {
    case BinaryOperator::bit_and: {
        auto* const lhs_aval = builder.CreateAnd(lhs.aval, mask);
        auto* const lhs_bval = builder.CreateAnd(lhs.bval, mask);
        auto* const rhs_aval = builder.CreateAnd(rhs.aval, mask);
        auto* const rhs_bval = builder.CreateAnd(rhs.bval, mask);
        auto* const possible_lhs
            = builder.CreateOr(lhs_aval, lhs_bval);
        auto* const possible_rhs
            = builder.CreateOr(rhs_aval, rhs_bval);
        auto* const aval = builder.CreateAnd(possible_lhs, possible_rhs);
        auto* const bval = builder.CreateAnd(
            aval, builder.CreateOr(lhs_bval, rhs_bval));
        return { aval, bval, lhs.width };
    }
    case BinaryOperator::bit_or: {
        auto* const lhs_aval = builder.CreateAnd(lhs.aval, mask);
        auto* const lhs_bval = builder.CreateAnd(lhs.bval, mask);
        auto* const rhs_aval = builder.CreateAnd(rhs.aval, mask);
        auto* const rhs_bval = builder.CreateAnd(rhs.bval, mask);
        auto* const aval = builder.CreateOr(
            builder.CreateOr(lhs_aval, lhs_bval),
            builder.CreateOr(rhs_aval, rhs_bval));
        auto* const lhs_one = builder.CreateAnd(
            lhs_aval, builder.CreateNot(lhs_bval));
        auto* const rhs_one = builder.CreateAnd(
            rhs_aval, builder.CreateNot(rhs_bval));
        auto* const known_one = builder.CreateOr(lhs_one, rhs_one);
        auto* const bval = builder.CreateAnd(
            aval, builder.CreateNot(known_one));
        return { aval, bval, lhs.width };
    }
    case BinaryOperator::bit_xor: {
        auto* const lhs_aval = builder.CreateAnd(lhs.aval, mask);
        auto* const lhs_bval = builder.CreateAnd(lhs.bval, mask);
        auto* const rhs_aval = builder.CreateAnd(rhs.aval, mask);
        auto* const rhs_bval = builder.CreateAnd(rhs.bval, mask);
        auto* const unknown = builder.CreateAnd(
            builder.CreateOr(lhs_bval, rhs_bval), mask);
        auto* const aval = builder.CreateOr(
            builder.CreateXor(lhs_aval, rhs_aval), unknown);
        return { aval, unknown, lhs.width };
    }
    case BinaryOperator::add_unsigned:
    case BinaryOperator::add_signed: {
        auto* unknown = builder.CreateICmpNE(
            builder.CreateAnd(
                builder.CreateOr(lhs.bval, rhs.bval), mask),
            zero);
        auto* known_result = builder.CreateAdd(
            builder.CreateAnd(lhs.aval, mask),
            builder.CreateAnd(rhs.aval, mask));
        return {
            builder.CreateSelect(
                unknown, mask, builder.CreateAnd(known_result, mask)),
            builder.CreateSelect(
                unknown, mask, zero),
            lhs.width
        };
    }
    case BinaryOperator::subtract_unsigned:
    case BinaryOperator::multiply_unsigned:
    case BinaryOperator::power_unsigned:
    case BinaryOperator::divide_unsigned:
    case BinaryOperator::modulo_unsigned:
    case BinaryOperator::subtract_signed:
    case BinaryOperator::multiply_signed:
    case BinaryOperator::power_signed:
    case BinaryOperator::divide_signed:
    case BinaryOperator::remainder_signed:
    case BinaryOperator::modulo_signed: {
        auto* unknown = builder.CreateICmpNE(
            builder.CreateAnd(
                builder.CreateOr(lhs.bval, rhs.bval), mask),
            zero);
        auto* left = builder.CreateAnd(lhs.aval, mask);
        auto* right = builder.CreateAnd(rhs.aval, mask);
        auto* invalid = unknown;
        const bool division = operation == BinaryOperator::divide_unsigned
            || operation == BinaryOperator::modulo_unsigned
            || operation == BinaryOperator::divide_signed
            || operation == BinaryOperator::remainder_signed
            || operation == BinaryOperator::modulo_signed;
        if (division) {
            invalid = builder.CreateOr(
                invalid,
                builder.CreateICmpEQ(
                    right, zero));
            right = builder.CreateSelect(
                invalid, one, right);
        }
        llvm::Value* known_result = nullptr;
        if (operation == BinaryOperator::subtract_unsigned
            || operation == BinaryOperator::subtract_signed) {
            known_result = builder.CreateSub(left, right);
        } else if (
            operation == BinaryOperator::multiply_unsigned
            || operation == BinaryOperator::multiply_signed) {
            known_result = builder.CreateMul(left, right);
        } else if (
            operation == BinaryOperator::power_unsigned
            || operation == BinaryOperator::power_signed) {
            auto* powered = lower_power(
                builder, left, right, mask, zero, one, lhs.width);
            if (operation == BinaryOperator::power_signed) {
                auto* sign_bit = builder.CreateShl(
                    one, packed_constant(context, lhs.width, lhs.width - 1U));
                auto* negative = builder.CreateICmpNE(
                    builder.CreateAnd(right, sign_bit), zero);
                auto* base_zero = builder.CreateICmpEQ(
                    left, zero);
                invalid = builder.CreateOr(
                    invalid, builder.CreateAnd(negative, base_zero));
                auto* base_one = builder.CreateICmpEQ(
                    left, one);
                auto* base_minus_one = builder.CreateICmpEQ(left, mask);
                auto* odd = builder.CreateICmpNE(
                    builder.CreateAnd(
                        right, one),
                    zero);
                auto* minus_one_result = builder.CreateSelect(
                    odd, mask, one);
                auto* negative_result = builder.CreateSelect(
                    base_one,
                    one,
                    builder.CreateSelect(
                        base_minus_one,
                        minus_one_result,
                        zero));
                powered = builder.CreateSelect(
                    negative, negative_result, powered);
            }
            known_result = powered;
        } else if (
            operation == BinaryOperator::divide_unsigned) {
            known_result = builder.CreateUDiv(left, right);
        } else if (
            operation == BinaryOperator::modulo_unsigned) {
            known_result = builder.CreateURem(left, right);
        } else {
            const auto sign_extend =
                [&](llvm::Value* value) -> llvm::Value* {
                if (lhs.width >= 64) {
                    return value;
                }
                auto* narrow_type = llvm::IntegerType::get(context, lhs.width);
                return builder.CreateSExt(
                    builder.CreateTrunc(value, narrow_type),
                    llvm::Type::getInt64Ty(context));
            };
            auto* signed_left = sign_extend(left);
            auto* signed_right = sign_extend(right);
            auto* sign_bit = builder.CreateShl(
                one, packed_constant(context, lhs.width, lhs.width - 1U));
            auto* overflow = builder.CreateAnd(
                builder.CreateICmpEQ(
                    left, sign_bit),
                builder.CreateICmpEQ(
                    right, mask));
            auto* safe_right = builder.CreateSelect(
                builder.CreateOr(invalid, overflow),
                one,
                signed_right);
            if (operation == BinaryOperator::divide_signed) {
                auto* divided = builder.CreateSDiv(signed_left, safe_right);
                known_result = builder.CreateSelect(
                    overflow, signed_left, divided);
            } else {
                auto* remainder = builder.CreateSRem(signed_left, safe_right);
                if (operation == BinaryOperator::modulo_signed) {
                    auto* nonzero = builder.CreateICmpNE(
                        remainder, zero);
                    auto* signs_differ = builder.CreateICmpNE(
                        builder.CreateICmpSLT(
                            signed_left, zero),
                        builder.CreateICmpSLT(
                            signed_right, zero));
                    remainder = builder.CreateSelect(
                        builder.CreateAnd(nonzero, signs_differ),
                        builder.CreateAdd(remainder, signed_right),
                        remainder);
                }
                known_result = remainder;
            }
        }
        return {
            builder.CreateSelect(
                invalid, mask, builder.CreateAnd(known_result, mask)),
            builder.CreateSelect(
                invalid, mask, zero),
            lhs.width
        };
    }
    case BinaryOperator::equal:
    case BinaryOperator::not_equal: {
        // A known differing bit decides the relation; unknown bits make it
        // ambiguous only otherwise (IEEE 1800-2017 11.4.5).
        auto* unknown_bits = builder.CreateAnd(builder.CreateOr(lhs.bval, rhs.bval), mask);
        auto* unknown = builder.CreateICmpNE(unknown_bits,
            zero);
        auto* differ_bits = builder.CreateAnd(
            builder.CreateAnd(
                builder.CreateXor(lhs.aval, rhs.aval),
                builder.CreateNot(unknown_bits)),
            mask);
        auto* differ = builder.CreateICmpNE(differ_bits, zero);
        auto* ambiguous = builder.CreateAnd(unknown, builder.CreateNot(differ));
        auto* set = operation == BinaryOperator::equal
            ? builder.CreateNot(differ)
            : builder.CreateOr(differ, ambiguous);
        auto* aval = builder.CreateZExt(set, llvm::Type::getInt64Ty(context));
        auto* bval = builder.CreateZExt(ambiguous, llvm::Type::getInt64Ty(context));
        return { aval, bval, 1 };
    }
    case BinaryOperator::case_equal: {
        auto* aval_equal = builder.CreateICmpEQ(
            builder.CreateAnd(lhs.aval, mask),
            builder.CreateAnd(rhs.aval, mask));
        auto* bval_equal = builder.CreateICmpEQ(
            builder.CreateAnd(lhs.bval, mask),
            builder.CreateAnd(rhs.bval, mask));
        auto* equal = builder.CreateAnd(aval_equal, bval_equal);
        return {
            builder.CreateZExt(equal, llvm::Type::getInt64Ty(context)),
            constant_i64(context, 0),
            1
        };
    }
    case BinaryOperator::casez_equal:
    case BinaryOperator::casex_equal: {
        auto* wildcard = operation == BinaryOperator::casez_equal
            ? builder.CreateOr(
                  builder.CreateAnd(
                      builder.CreateNot(lhs.aval), lhs.bval),
                  builder.CreateAnd(
                      builder.CreateNot(rhs.aval), rhs.bval))
            : builder.CreateOr(lhs.bval, rhs.bval);
        auto* mismatch = builder.CreateAnd(
            builder.CreateOr(
                builder.CreateXor(lhs.aval, rhs.aval),
                builder.CreateXor(lhs.bval, rhs.bval)),
            builder.CreateAnd(builder.CreateNot(wildcard), mask));
        auto* equal = builder.CreateICmpEQ(
            mismatch, zero);
        return {
            builder.CreateZExt(equal, llvm::Type::getInt64Ty(context)),
            constant_i64(context, 0),
            1
        };
    }
    case BinaryOperator::wildcard_equal: {
        auto* compared_mask = builder.CreateAnd(builder.CreateNot(rhs.bval), mask);
        auto* unknown_bits = builder.CreateAnd(lhs.bval, compared_mask);
        auto* unknown = builder.CreateICmpNE(
            unknown_bits, zero);
        auto* mismatch_bits = builder.CreateAnd(
            builder.CreateXor(lhs.aval, rhs.aval), compared_mask);
        auto* equal = builder.CreateICmpEQ(
            mismatch_bits, zero);
        // A known differing bit decides the relation (IEEE 1800-2017
        // 11.4.6).
        auto* known_differ = builder.CreateICmpNE(
            builder.CreateAnd(mismatch_bits, builder.CreateNot(lhs.bval)),
            zero);
        auto* ambiguous = builder.CreateAnd(
            unknown, builder.CreateNot(known_differ));
        return {
            builder.CreateZExt(
                builder.CreateAnd(builder.CreateOr(unknown, equal),
                    builder.CreateNot(known_differ)),
                llvm::Type::getInt64Ty(context)),
            builder.CreateZExt(
                ambiguous, llvm::Type::getInt64Ty(context)),
            1
        };
    }
    case BinaryOperator::vhdl_match_equal: {
        if (lhs.kind == ValueKind::logic9 || rhs.kind == ValueKind::logic9) {
            const auto left_dash = logic9_state_mask(
                builder, lhs, static_cast<std::uint8_t>(Logic9::dont_care));
            const auto right_dash = logic9_state_mask(
                builder, rhs, static_cast<std::uint8_t>(Logic9::dont_care));
            const auto left_zero = builder.CreateOr(
                logic9_state_mask(
                    builder, lhs, static_cast<std::uint8_t>(Logic9::zero)),
                logic9_state_mask(
                    builder, lhs, static_cast<std::uint8_t>(Logic9::l)));
            const auto right_zero = builder.CreateOr(
                logic9_state_mask(
                    builder, rhs, static_cast<std::uint8_t>(Logic9::zero)),
                logic9_state_mask(
                    builder, rhs, static_cast<std::uint8_t>(Logic9::l)));
            const auto left_one = builder.CreateOr(
                logic9_state_mask(
                    builder, lhs, static_cast<std::uint8_t>(Logic9::one)),
                logic9_state_mask(
                    builder, lhs, static_cast<std::uint8_t>(Logic9::h)));
            const auto right_one = builder.CreateOr(
                logic9_state_mask(
                    builder, rhs, static_cast<std::uint8_t>(Logic9::one)),
                logic9_state_mask(
                    builder, rhs, static_cast<std::uint8_t>(Logic9::h)));
            auto* matched = builder.CreateOr(
                builder.CreateOr(left_dash, right_dash),
                builder.CreateOr(
                    builder.CreateAnd(left_zero, right_zero),
                    builder.CreateAnd(left_one, right_one)));
            auto* equal = builder.CreateICmpEQ(
                builder.CreateAnd(builder.CreateNot(matched), mask),
                zero);
            return {
                builder.CreateZExt(equal, llvm::Type::getInt64Ty(context)),
                constant_i64(context, 0),
                1
            };
        }
        auto* mismatch = builder.CreateAnd(
            builder.CreateOr(
                builder.CreateXor(lhs.aval, rhs.aval),
                builder.CreateOr(lhs.bval, rhs.bval)),
            mask);
        auto* equal = builder.CreateICmpEQ(
            mismatch, zero);
        return {
            builder.CreateZExt(equal, llvm::Type::getInt64Ty(context)),
            constant_i64(context, 0),
            1
        };
    }
    case BinaryOperator::less_unsigned:
    case BinaryOperator::less_equal_unsigned:
    case BinaryOperator::greater_unsigned:
    case BinaryOperator::greater_equal_unsigned:
    case BinaryOperator::less_signed:
    case BinaryOperator::less_equal_signed:
    case BinaryOperator::greater_signed:
    case BinaryOperator::greater_equal_signed: {
        auto* unknown_bits = builder.CreateAnd(
            builder.CreateOr(lhs.bval, rhs.bval), mask);
        auto* unknown = builder.CreateICmpNE(
            unknown_bits, zero);
        auto* left = builder.CreateAnd(lhs.aval, mask);
        auto* right = builder.CreateAnd(rhs.aval, mask);
        const bool signed_comparison = operation == BinaryOperator::less_signed
            || operation == BinaryOperator::less_equal_signed
            || operation == BinaryOperator::greater_signed
            || operation == BinaryOperator::greater_equal_signed;
        if (signed_comparison && lhs.width < 64) {
            auto* narrow_type = llvm::IntegerType::get(context, lhs.width);
            left = builder.CreateSExt(
                builder.CreateTrunc(left, narrow_type),
                llvm::Type::getInt64Ty(context));
            right = builder.CreateSExt(
                builder.CreateTrunc(right, narrow_type),
                llvm::Type::getInt64Ty(context));
        }
        llvm::CmpInst::Predicate predicate = llvm::CmpInst::ICMP_NE;
        switch (operation) {
        case BinaryOperator::not_equal:
            predicate = llvm::CmpInst::ICMP_NE;
            break;
        case BinaryOperator::less_unsigned:
            predicate = llvm::CmpInst::ICMP_ULT;
            break;
        case BinaryOperator::less_equal_unsigned:
            predicate = llvm::CmpInst::ICMP_ULE;
            break;
        case BinaryOperator::greater_unsigned:
            predicate = llvm::CmpInst::ICMP_UGT;
            break;
        case BinaryOperator::greater_equal_unsigned:
            predicate = llvm::CmpInst::ICMP_UGE;
            break;
        case BinaryOperator::less_signed:
            predicate = llvm::CmpInst::ICMP_SLT;
            break;
        case BinaryOperator::less_equal_signed:
            predicate = llvm::CmpInst::ICMP_SLE;
            break;
        case BinaryOperator::greater_signed:
            predicate = llvm::CmpInst::ICMP_SGT;
            break;
        case BinaryOperator::greater_equal_signed:
            predicate = llvm::CmpInst::ICMP_SGE;
            break;
        case BinaryOperator::bit_and:
        case BinaryOperator::bit_or:
        case BinaryOperator::bit_xor:
        case BinaryOperator::add_unsigned:
        case BinaryOperator::subtract_unsigned:
        case BinaryOperator::multiply_unsigned:
        case BinaryOperator::power_unsigned:
        case BinaryOperator::divide_unsigned:
        case BinaryOperator::modulo_unsigned:
        case BinaryOperator::add_signed:
        case BinaryOperator::subtract_signed:
        case BinaryOperator::multiply_signed:
        case BinaryOperator::power_signed:
        case BinaryOperator::divide_signed:
        case BinaryOperator::remainder_signed:
        case BinaryOperator::modulo_signed:
        case BinaryOperator::equal:
        case BinaryOperator::case_equal:
        case BinaryOperator::casez_equal:
        case BinaryOperator::casex_equal:
        case BinaryOperator::wildcard_equal:
        case BinaryOperator::vhdl_match_equal:
            llvm_unreachable("not a comparison operator");
        }
        auto* compared = builder.CreateICmp(predicate, left, right);
        return {
            builder.CreateZExt(
                builder.CreateOr(unknown, compared),
                llvm::Type::getInt64Ty(context)),
            builder.CreateZExt(
                unknown, llvm::Type::getInt64Ty(context)),
            1
        };
    }
    }
    llvm_unreachable("all BinaryOperator values are handled");
}

}  // namespace fsim::compiler::llvm_detail
