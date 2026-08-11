// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/ADT/APInt.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
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

[[nodiscard]] EncodedValue
load_register(llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    const RegisterId id)
{
    const auto& slot = registers[id];
    auto* integer = packed_integer_type(builder.getContext(), slot.width);
    llvm::Value* zero = llvm::ConstantInt::get(integer, 0);
    auto* aval = builder.CreateLoad(integer, slot.aval, "register.aval");
    auto* bval = builder.CreateLoad(integer, slot.bval, "register.bval");
    aval->setAlignment(llvm::Align { 8 });
    bval->setAlignment(llvm::Align { 8 });
    llvm::Value* logic9_plane2 = zero;
    llvm::Value* logic9_plane3 = zero;
    if (slot.kind == ValueKind::logic9) {
        auto* plane2 = builder.CreateLoad(
            integer, slot.logic9_plane2, "register.logic9.plane2");
        auto* plane3 = builder.CreateLoad(
            integer, slot.logic9_plane3, "register.logic9.plane3");
        plane2->setAlignment(llvm::Align { 8 });
        plane3->setAlignment(llvm::Align { 8 });
        logic9_plane2 = plane2;
        logic9_plane3 = plane3;
    }
    return {
        aval,
        bval,
        slot.width,
        logic9_plane2,
        logic9_plane3,
        slot.kind,
    };
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

  const auto state_mask =
      [&](const std::uint8_t state) -> llvm::Value* {
        llvm::Value* selected = mask;
        const std::array planes{
            value.aval,
            value.bval,
            value.logic9_plane2,
            value.logic9_plane3};
        for (std::size_t plane = 0; plane < planes.size(); ++plane) {
          const auto bit = ((state >> plane) & 1U) != 0;
          selected = builder.CreateAnd(
              selected,
              bit ? planes[plane]
                  : builder.CreateNot(planes[plane]));
        }
        return builder.CreateAnd(selected, mask);
      };
  const auto zero_state = builder.CreateOr(
      state_mask(static_cast<std::uint8_t>(Logic9::zero)),
      state_mask(static_cast<std::uint8_t>(Logic9::l)));
  const auto one_state = builder.CreateOr(
      state_mask(static_cast<std::uint8_t>(Logic9::one)),
      state_mask(static_cast<std::uint8_t>(Logic9::h)));
  const auto z_state =
      state_mask(static_cast<std::uint8_t>(Logic9::z));
  auto* known = builder.CreateOr(zero_state, one_state);
  auto* x_state = builder.CreateAnd(
      builder.CreateNot(builder.CreateOr(known, z_state)), mask);
  return {
      builder.CreateOr(one_state, x_state),
      builder.CreateOr(z_state, x_state),
      value.width,
      zero,
      zero,
      ValueKind::logic4
  };
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

[[nodiscard]] EncodedValue map_logic9_unary(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value,
    const std::array<Logic9, 9>& table)
{
    auto* zero = packed_constant(builder.getContext(), value.width, 0);
    std::array<llvm::Value*, 4> result {
        zero, zero, zero, zero
    };
    for (std::uint8_t state = 0; state < table.size(); ++state) {
        auto* selected = logic9_state_mask(builder, value, state);
        const auto encoded = static_cast<std::uint8_t>(table[state]);
        for (std::size_t plane = 0; plane < result.size(); ++plane) {
            if (((encoded >> plane) & 1U) != 0) {
                result[plane] = builder.CreateOr(result[plane], selected);
            }
        }
    }
    return {
        result[0],
        result[1],
        value.width,
        result[2],
        result[3],
        ValueKind::logic9
    };
}

[[nodiscard]] EncodedValue map_logic9_binary(
    llvm::IRBuilder<>& builder,
    const EncodedValue& lhs,
    const EncodedValue& rhs,
    const std::array<std::array<Logic9, 9>, 9>& table)
{
    auto* zero = packed_constant(builder.getContext(), lhs.width, 0);
    std::array<llvm::Value*, 4> result {
        zero, zero, zero, zero
    };
    std::array<llvm::Value*, 9> left_masks { };
    std::array<llvm::Value*, 9> right_masks { };
    for (std::uint8_t state = 0; state < 9; ++state) {
        left_masks[state] = logic9_state_mask(builder, lhs, state);
        right_masks[state] = logic9_state_mask(builder, rhs, state);
    }
    for (std::uint8_t left = 0; left < 9; ++left) {
        for (std::uint8_t right = 0; right < 9; ++right) {
            auto* selected = builder.CreateAnd(
                left_masks[left], right_masks[right]);
            const auto encoded = static_cast<std::uint8_t>(table[left][right]);
            for (std::size_t plane = 0; plane < result.size(); ++plane) {
                if (((encoded >> plane) & 1U) != 0) {
                    result[plane] = builder.CreateOr(result[plane], selected);
                }
            }
        }
    }
    return {
        result[0],
        result[1],
        lhs.width,
        result[2],
        result[3],
        ValueKind::logic9
    };
}

void store_register(llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    const RegisterId id, EncodedValue value)
{
    const auto& slot = registers[id];
    value = coerce_value_kind(builder, value, slot.kind);
    auto* aval = builder.CreateStore(value.aval, slot.aval);
    auto* bval = builder.CreateStore(value.bval, slot.bval);
    aval->setAlignment(llvm::Align { 8 });
    bval->setAlignment(llvm::Align { 8 });
    if (slot.kind == ValueKind::logic9) {
        auto* logic9_plane2 = builder.CreateStore(
            value.logic9_plane2, slot.logic9_plane2);
        auto* logic9_plane3 = builder.CreateStore(
            value.logic9_plane3, slot.logic9_plane3);
        logic9_plane2->setAlignment(llvm::Align { 8 });
        logic9_plane3->setAlignment(llvm::Align { 8 });
    }
    builder.CreateStore(
        llvm::ConstantInt::get(
            llvm::Type::getInt8Ty(builder.getContext()), 1),
        slot.initialized);
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
        auto* lhs_zero = builder.CreateAnd(builder.CreateNot(lhs.aval),
            builder.CreateNot(lhs.bval));
        auto* rhs_zero = builder.CreateAnd(builder.CreateNot(rhs.aval),
            builder.CreateNot(rhs.bval));
        auto* known_zero = builder.CreateOr(lhs_zero, rhs_zero);
        auto* lhs_one = builder.CreateAnd(lhs.aval, builder.CreateNot(lhs.bval));
        auto* rhs_one = builder.CreateAnd(rhs.aval, builder.CreateNot(rhs.bval));
        auto* known_one = builder.CreateAnd(lhs_one, rhs_one);
        auto* unknown = builder.CreateAnd(builder.CreateNot(
                                              builder.CreateOr(known_zero, known_one)),
            mask);
        return { builder.CreateAnd(builder.CreateOr(known_one, unknown), mask),
            unknown, lhs.width };
    }
    case BinaryOperator::bit_or: {
        auto* lhs_one = builder.CreateAnd(lhs.aval, builder.CreateNot(lhs.bval));
        auto* rhs_one = builder.CreateAnd(rhs.aval, builder.CreateNot(rhs.bval));
        auto* known_one = builder.CreateOr(lhs_one, rhs_one);
        auto* lhs_zero = builder.CreateAnd(builder.CreateNot(lhs.aval),
            builder.CreateNot(lhs.bval));
        auto* rhs_zero = builder.CreateAnd(builder.CreateNot(rhs.aval),
            builder.CreateNot(rhs.bval));
        auto* known_zero = builder.CreateAnd(lhs_zero, rhs_zero);
        auto* unknown = builder.CreateAnd(builder.CreateNot(
                                              builder.CreateOr(known_zero, known_one)),
            mask);
        return { builder.CreateAnd(builder.CreateOr(known_one, unknown), mask),
            unknown, lhs.width };
    }
    case BinaryOperator::bit_xor: {
        auto* known = builder.CreateAnd(builder.CreateNot(
                                            builder.CreateOr(lhs.bval, rhs.bval)),
            mask);
        auto* unknown = builder.CreateAnd(builder.CreateNot(known), mask);
        auto* known_value = builder.CreateAnd(builder.CreateXor(lhs.aval, rhs.aval), known);
        return { builder.CreateOr(known_value, unknown), unknown, lhs.width };
    }
    case BinaryOperator::add_unsigned:
    case BinaryOperator::add_signed: {
        auto* unknown = builder.CreateICmpNE(
            builder.CreateAnd(
                builder.CreateOr(lhs.bval, rhs.bval), mask),
            zero);
        llvm::Value* result_aval = zero;
        EncodedBit carry { llvm::ConstantInt::getFalse(context),
            llvm::ConstantInt::getFalse(context) };
        for (std::uint32_t bit = 0; bit < lhs.width; ++bit) {
            const auto left = bit_at(builder, lhs, bit);
            const auto right = bit_at(builder, rhs, bit);
            const auto partial = bit_xor(builder, left, right);
            const auto sum = bit_xor(builder, partial, carry);
            const auto carry_generate = bit_and(builder, left, right);
            const auto carry_propagate = bit_and(builder, carry, bit_or(builder, left, right));
            carry = bit_or(builder, carry_generate, carry_propagate);

            auto* shift = packed_constant(context, lhs.width, bit);
            auto* aval = builder.CreateShl(
                builder.CreateZExt(
                    sum.aval, packed_integer_type(context, lhs.width)),
                shift);
            result_aval = builder.CreateOr(result_aval, aval);
        }
        return {
            builder.CreateSelect(
                unknown, mask, builder.CreateAnd(result_aval, mask)),
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
            llvm::Value* powered = one;
            llvm::Value* factor = left;
            for (std::uint32_t bit = 0; bit < lhs.width; ++bit) {
                auto* selected = builder.CreateICmpNE(
                    builder.CreateAnd(
                        builder.CreateLShr(
                            right, packed_constant(context, lhs.width, bit)),
                        one),
                    zero);
                powered = builder.CreateSelect(
                    selected,
                    builder.CreateAnd(
                        builder.CreateMul(powered, factor), mask),
                    powered);
                if (bit + 1U < lhs.width) {
                    factor = builder.CreateAnd(
                        builder.CreateMul(factor, factor), mask);
                }
            }
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
    case BinaryOperator::equal: {
        auto* unknown_bits = builder.CreateAnd(builder.CreateOr(lhs.bval, rhs.bval), mask);
        auto* unknown = builder.CreateICmpNE(unknown_bits,
            zero);
        auto* equal = builder.CreateICmpEQ(
            builder.CreateAnd(lhs.aval, mask),
            builder.CreateAnd(rhs.aval, mask));
        auto* aval = builder.CreateZExt(
            builder.CreateOr(unknown, equal), llvm::Type::getInt64Ty(context));
        auto* bval = builder.CreateZExt(unknown, llvm::Type::getInt64Ty(context));
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
        return {
            builder.CreateZExt(
                builder.CreateOr(unknown, equal),
                llvm::Type::getInt64Ty(context)),
            builder.CreateZExt(
                unknown, llvm::Type::getInt64Ty(context)),
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
    case BinaryOperator::not_equal:
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
