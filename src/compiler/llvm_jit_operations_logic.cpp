// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Support/ErrorHandling.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace fsim::compiler::llvm_detail {

using runtime::Logic9;
using runtime::simir::BinaryOperator;
using runtime::simir::ConvertToTwoState;
using runtime::simir::CopyRegister;
using runtime::simir::CountBits;
using runtime::simir::CountOnes;
using runtime::simir::Extract;
using runtime::simir::IntegerBinaryOperator;
using runtime::simir::IntegerUnaryOperator;
using runtime::simir::LogicalBinary;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::LogicalNot;
using runtime::simir::Reduction;
using runtime::simir::ReductionOperator;
using runtime::simir::Shift;
using runtime::simir::ShiftOperator;
using runtime::simir::UnaryNot;
using runtime::simir::ValueKind;

namespace {

[[nodiscard]] llvm::Value* count_set_bits_in_words(
    llvm::IRBuilder<>& builder,
    llvm::Value* value,
    const std::uint32_t width)
{
    auto& context = builder.getContext();
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const zero = llvm::ConstantInt::get(i64, 0U);
    llvm::Value* count = zero;
    auto* const popcount = llvm::Intrinsic::getOrInsertDeclaration(
        builder.GetInsertBlock()->getModule(),
        llvm::Intrinsic::ctpop,
        i64,
        { i64 });

    for (std::uint64_t offset = 0U; offset < width; offset += 64U) {
        const auto active_width = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(64U, width - offset));
        auto* shifted = value;
        if (offset != 0U) {
            shifted = builder.CreateLShr(
                value, packed_constant(
                           context, width,
                           static_cast<std::uint64_t>(offset)));
        }
        auto* word = builder.CreateZExtOrTrunc(shifted, i64);
        if (active_width != 64U) {
            word = builder.CreateAnd(
                word, packed_low_mask(context, 64U, active_width));
        }
        count = builder.CreateAdd(
            count, builder.CreateCall(popcount, { word }));
    }
    return count;
}

[[nodiscard]] llvm::Value* logic4_exact_one_bits(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value)
{
    auto* const mask = packed_mask(builder.getContext(), value.width);
    return builder.CreateAnd(
        builder.CreateAnd(value.aval, builder.CreateNot(value.bval)), mask);
}

} // namespace

void ValueOperationLowerer::lower(
    const CopyRegister& operation) {
              store_register(
                  builder, registers, operation.destination,
                  load_register(
                      builder, registers, operation.source));
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const ConvertToTwoState& operation)
{
    auto value = coerce_value_kind(
        builder,
        load_register(builder, registers, operation.source),
        ValueKind::logic4);
    auto* mask = packed_mask(builder.getContext(), value.width);
    auto* zero = packed_constant(builder.getContext(), value.width, 0);
    value.aval = builder.CreateAnd(
        value.aval, builder.CreateNot(value.bval));
    value.aval = builder.CreateAnd(value.aval, mask);
    value.bval = zero;
    value.logic9_plane2 = zero;
    value.logic9_plane3 = zero;
    store_register(
        builder, registers, operation.destination, value);
    branch_to_next();
}

void ValueOperationLowerer::lower(
    const UnaryNot& operation) {
              if (registers[operation.destination].known_logic4) {
                const auto source = load_register(
                    builder, registers, operation.source);
                auto* const mask = packed_mask(context, source.width);
                auto* const aval = builder.CreateAnd(
                    builder.CreateNot(source.aval), mask);
                auto* const zero = packed_constant(
                    context, source.width, 0U);
                store_register(
                    builder, registers, operation.destination,
                    EncodedValue {
                        aval, zero, source.width, zero, zero,
                        ValueKind::logic4 });
                branch_to_next();
                return;
              }
              if (try_lower_wide_bitwise_not(
                      builder,
                      registers,
                      operation.destination,
                      operation.source)) {
                branch_to_next();
                return;
              }
              const auto source =
                  load_register(builder, registers, operation.source);
              if (source.kind == ValueKind::logic9) {
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    lower_logic9_not(builder, source));
                branch_to_next();
                return;
              }
              auto* mask = packed_mask(context, source.width);
              auto *aval = builder.CreateAnd(
                  builder.CreateOr(builder.CreateNot(source.aval),
                                   source.bval),
                  mask);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{aval, source.bval, source.width});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const LogicalNot& operation) {
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              auto* mask = packed_mask(context, source.width);
              auto *known_ones = builder.CreateAnd(
                  builder.CreateAnd(source.aval, mask),
                  builder.CreateNot(source.bval));
              auto* has_one = builder.CreateICmpNE(
                  known_ones,
                  packed_constant(context, source.width, 0));
              auto* has_unknown = builder.CreateICmpNE(
                  builder.CreateAnd(source.bval, mask),
                  packed_constant(context, source.width, 0));
              auto *not_true = builder.CreateNot(has_one);
              auto *unknown =
                  builder.CreateAnd(not_true, has_unknown);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(
                          not_true,
                          llvm::Type::getInt64Ty(context)),
                      builder.CreateZExt(
                          unknown,
                          llvm::Type::getInt64Ty(context)),
                      1});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const LogicalBinary& operation) {
              const auto left = truth_bit(
                  builder,
                  coerce_value_kind(
                      builder,
                      load_register(
                          builder, registers, operation.lhs),
                      ValueKind::logic4));
              const auto right = truth_bit(
                  builder,
                  coerce_value_kind(
                      builder,
                      load_register(
                          builder, registers, operation.rhs),
                      ValueKind::logic4));
              const auto result =
                  operation.operation
                          == LogicalBinaryOperator::logical_and
                      ? bit_and(builder, left, right)
                      : bit_or(builder, left, right);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateZExt(
                          result.aval,
                          llvm::Type::getInt64Ty(context)),
                      builder.CreateZExt(
                          result.bval,
                          llvm::Type::getInt64Ty(context)),
                      1});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const Reduction& operation) {
              if (registers[operation.destination].known_logic4) {
                const auto source = load_register(
                    builder, registers, operation.source);
                auto* const mask = packed_mask(context, source.width);
                auto* const value = builder.CreateAnd(source.aval, mask);
                llvm::Value* reduced = nullptr;
                if (operation.operation == ReductionOperator::bit_and) {
                  reduced = builder.CreateICmpEQ(value, mask);
                } else if (
                    operation.operation == ReductionOperator::bit_or) {
                  reduced = builder.CreateICmpNE(
                      value, packed_constant(context, source.width, 0U));
                } else if (
                    operation.operation == ReductionOperator::bit_xor) {
                  auto* const one_count = count_set_bits_in_words(
                      builder, logic4_exact_one_bits(builder, source),
                      source.width);
                  auto* const parity = builder.CreateAnd(
                      one_count, llvm::ConstantInt::get(i64, 1U));
                  reduced = builder.CreateICmpNE(
                      parity, packed_constant(context, 1U, 0U));
                } else {
                  throw LlvmJitError(
                      "known Logic4 region reduction is unsupported");
                }
                auto* const zero = packed_constant(context, 1U, 0U);
                store_register(
                    builder, registers, operation.destination,
                    EncodedValue {
                        builder.CreateZExt(reduced, i64), zero, 1U,
                        zero, zero, ValueKind::logic4 });
                branch_to_next();
                return;
              }
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              const auto extend_boolean = [&](llvm::Value* value) {
                return builder.CreateZExt(value, i64);
              };
              const auto count_exact_ones = [&] {
                return count_set_bits_in_words(
                    builder, logic4_exact_one_bits(builder, source),
                    source.width);
              };
              if (operation.operation
                      == ReductionOperator::one_hot
                  || operation.operation
                      == ReductionOperator::one_hot_or_zero) {
                auto* const count = count_exact_ones();
                auto* const one = llvm::ConstantInt::get(i64, 1U);
                auto* matched =
                    operation.operation
                            == ReductionOperator::one_hot
                        ? builder.CreateICmpEQ(count, one)
                        : builder.CreateICmpULE(count, one);
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    EncodedValue{
                        builder.CreateZExt(
                            matched,
                            llvm::Type::getInt64Ty(context)),
                        llvm::ConstantInt::get(
                            llvm::Type::getInt64Ty(context), 0),
                        1});
                branch_to_next();
                return;
              }
              auto* const mask = packed_mask(context, source.width);
              auto* const zero = packed_constant(context, source.width, 0U);
              auto* const aval = builder.CreateAnd(source.aval, mask);
              auto* const bval = builder.CreateAnd(source.bval, mask);
              auto* result_aval = static_cast<llvm::Value*>(nullptr);
              auto* result_bval = static_cast<llvm::Value*>(nullptr);
              if (operation.operation == ReductionOperator::bit_and) {
                auto* const known_zero = builder.CreateAnd(
                    builder.CreateNot(aval), builder.CreateNot(bval));
                auto* const has_known_zero = builder.CreateICmpNE(
                    builder.CreateAnd(known_zero, mask), zero);
                auto* const has_unknown = builder.CreateICmpNE(bval, zero);
                auto* const result_one = builder.CreateNot(has_known_zero);
                result_aval = extend_boolean(result_one);
                result_bval = extend_boolean(
                    builder.CreateAnd(result_one, has_unknown));
              } else if (
                  operation.operation == ReductionOperator::bit_or) {
                auto* const known_one = builder.CreateAnd(
                    aval, builder.CreateNot(bval));
                auto* const has_known_one = builder.CreateICmpNE(
                    known_one, zero);
                auto* const has_unknown = builder.CreateICmpNE(bval, zero);
                result_aval = extend_boolean(
                    builder.CreateOr(has_known_one, has_unknown));
                result_bval = extend_boolean(
                    builder.CreateAnd(
                        has_unknown, builder.CreateNot(has_known_one)));
              } else {
                auto* const has_unknown = builder.CreateICmpNE(bval, zero);
                auto* const parity = builder.CreateAnd(
                    count_exact_ones(), llvm::ConstantInt::get(i64, 1U));
                result_aval = builder.CreateOr(
                    parity, extend_boolean(has_unknown));
                result_bval = extend_boolean(has_unknown);
              }
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue {
                      result_aval,
                      result_bval,
                      1});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const CountOnes& operation) {
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              auto* const count = count_set_bits_in_words(
                  builder, logic4_exact_one_bits(builder, source),
                  source.width);
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      count,
                      llvm::ConstantInt::get(
                          llvm::Type::getInt64Ty(context), 0),
                      32});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const CountBits& operation) {
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              auto* const mask = packed_mask(context, source.width);
              auto* const aval = builder.CreateAnd(source.aval, mask);
              auto* const bval = builder.CreateAnd(source.bval, mask);
              llvm::Value* selected =
                  packed_constant(context, source.width, 0U);
              if ((operation.state_mask & 0x1U) != 0U) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(
                          builder.CreateNot(builder.CreateOr(aval, bval)),
                          mask));
              }
              if ((operation.state_mask & 0x2U) != 0U) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(aval, builder.CreateNot(bval)));
              }
              if ((operation.state_mask & 0x4U) != 0U) {
                  selected = builder.CreateOr(
                      selected, builder.CreateAnd(aval, bval));
              }
              if ((operation.state_mask & 0x8U) != 0U) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(
                          builder.CreateNot(aval), bval));
              }
              auto* const count = count_set_bits_in_words(
                  builder, builder.CreateAnd(selected, mask),
                  source.width);
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      count,
                      llvm::ConstantInt::get(
                          llvm::Type::getInt64Ty(context), 0),
                      32});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const Shift& operation) {
              const auto value =
                  load_register(
                      builder, registers, operation.value);
              const auto amount = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.amount),
                  ValueKind::logic4);
              auto* value_mask = packed_mask(context, value.width);
              auto* amount_mask = packed_mask(context, amount.width);
              auto* amount_zero = packed_constant(
                  context, amount.width, 0);
              auto* amount_one = packed_constant(
                  context, amount.width, 1);
              auto* amount_unknown = builder.CreateICmpNE(
                  builder.CreateAnd(amount.bval, amount_mask),
                  amount_zero);
              auto* raw_amount_bits =
                  builder.CreateAnd(amount.aval, amount_mask);
              llvm::Value* amount_negative =
                  llvm::ConstantInt::getFalse(context);
              llvm::Value* amount_bits = raw_amount_bits;
              if (operation.signed_amount) {
                  auto* sign_mask = builder.CreateShl(
                      amount_one,
                      packed_constant(
                          context, amount.width, amount.width - 1U));
                  amount_negative = builder.CreateICmpNE(
                      builder.CreateAnd(
                          raw_amount_bits, sign_mask),
                      amount_zero);
                  auto* magnitude = builder.CreateAnd(
                      builder.CreateSub(
                          amount_zero,
                          raw_amount_bits),
                      amount_mask);
                  amount_bits = builder.CreateSelect(
                      amount_negative,
                      magnitude,
                      raw_amount_bits);
              }
              auto* amount_too_large = builder.CreateICmpUGE(
                  amount_bits,
                  packed_constant(context, amount.width, value.width));
              const auto rotating =
                  operation.operation == ShiftOperator::rotate_left
                  || operation.operation == ShiftOperator::rotate_right;
              auto* safe_amount = rotating
                  ? builder.CreateURem(
                        amount_bits,
                        packed_constant(
                            context, amount.width, value.width))
                  : builder.CreateSelect(
                        amount_too_large,
                        amount_zero,
                        amount_bits);
              auto* value_amount = builder.CreateZExtOrTrunc(
                  safe_amount, packed_integer_type(context, value.width));
              llvm::Value* inverse_amount = nullptr;
              if (rotating) {
                  auto* zero_value = packed_constant(
                      context, value.width, 0);
                  auto* width_value = packed_constant(
                      context, value.width, value.width);
                  inverse_amount = builder.CreateSelect(
                      builder.CreateICmpEQ(value_amount, zero_value),
                      zero_value,
                      builder.CreateSub(width_value, value_amount));
              }
              const auto shift_component =
                  [&](llvm::Value* component,
                      const ShiftOperator selected_operation,
                      const bool zero_plane)
                      -> llvm::Value* {
                    if (selected_operation
                            == ShiftOperator::rotate_left
                        || selected_operation
                            == ShiftOperator::rotate_right) {
                        auto* left_amount = selected_operation
                                == ShiftOperator::rotate_left
                            ? value_amount
                            : inverse_amount;
                        auto* right_amount = selected_operation
                                == ShiftOperator::rotate_left
                            ? inverse_amount
                            : value_amount;
                        return builder.CreateOr(
                            builder.CreateShl(component, left_amount),
                            builder.CreateLShr(component, right_amount));
                    }
                    if (selected_operation
                            == ShiftOperator::logical_left
                        || selected_operation
                            == ShiftOperator::arithmetic_left) {
                        auto* shifted = builder.CreateShl(
                            component, value_amount);
                        if (selected_operation
                            == ShiftOperator::arithmetic_left) {
                            auto* fill_mask = builder.CreateSub(
                                builder.CreateShl(
                                    packed_constant(context, value.width, 1),
                                    value_amount),
                                packed_constant(context, value.width, 1));
                            auto* rightmost = builder.CreateAnd(
                                component,
                                packed_constant(context, value.width, 1));
                            auto* fill = builder.CreateSelect(
                                builder.CreateICmpNE(
                                    rightmost,
                                    packed_constant(
                                        context, value.width, 0)),
                                fill_mask,
                                packed_constant(context, value.width, 0));
                            return builder.CreateOr(shifted, fill);
                        }
                      if (zero_plane) {
                          auto* fill_mask = builder.CreateSub(
                              builder.CreateShl(
                                  packed_constant(context, value.width, 1),
                                  value_amount),
                              packed_constant(context, value.width, 1));
                          return builder.CreateOr(
                              shifted, fill_mask);
                      }
                      return shifted;
                    }
                    if (selected_operation
                        == ShiftOperator::logical_right) {
                        auto* shifted = builder.CreateLShr(
                            component, value_amount);
                        if (zero_plane) {
                            auto* fill_mask = builder.CreateXor(
                                value_mask,
                                builder.CreateLShr(
                                    value_mask, value_amount));
                            return builder.CreateOr(
                                shifted, fill_mask);
                        }
                      return shifted;
                    }
                    const auto extension_shift = std::max(value.width, 64U) - value.width;
                    auto* sign_extended = component;
                    if (extension_shift != 0) {
                        sign_extended = builder.CreateAShr(
                            builder.CreateShl(
                                component,
                                packed_constant(
                                    context, value.width, extension_shift)),
                            packed_constant(
                                context, value.width, extension_shift));
                    }
                    return builder.CreateAShr(
                        sign_extended, value_amount);
                  };
              const auto selected_shift_component =
                  [&](llvm::Value* component,
                      const bool zero_plane) -> llvm::Value* {
                    auto* positive = shift_component(
                        component, operation.operation, zero_plane);
                    if (!operation.signed_amount) {
                      return positive;
                    }
                    auto* negative = shift_component(
                        component,
                        reverse_shift(operation.operation),
                        zero_plane);
                    return builder.CreateSelect(
                        amount_negative, negative, positive);
                  };
              auto* shifted_aval =
                  selected_shift_component(value.aval, false);
              auto* shifted_bval =
                  selected_shift_component(
                      value.bval,
                      value.kind == ValueKind::logic9);
              auto* shifted_plane2 =
                  selected_shift_component(
                      value.logic9_plane2, false);
              auto* shifted_plane3 =
                  selected_shift_component(
                      value.logic9_plane3, false);
              const auto oversized_component =
                  [&](llvm::Value* component,
                      const ShiftOperator selected_operation,
                      const bool zero_plane)
                      -> llvm::Value* {
                    if (selected_operation
                        == ShiftOperator::arithmetic_right) {
                      const auto sign_offset =
                          value.width - 1U;
                      auto* sign = builder.CreateAnd(
                          builder.CreateLShr(
                              component,
                              packed_constant(
                                  context, value.width, sign_offset)),
                          packed_constant(context, value.width, 1));
                      return builder.CreateSelect(
                          builder.CreateICmpNE(
                              sign,
                              packed_constant(context, value.width, 0)),
                          value_mask,
                          packed_constant(context, value.width, 0));
                    }
                    if (selected_operation
                        == ShiftOperator::arithmetic_left) {
                        auto* rightmost = builder.CreateAnd(
                            component,
                            packed_constant(context, value.width, 1));
                        return builder.CreateSelect(
                            builder.CreateICmpNE(
                                rightmost,
                                packed_constant(context, value.width, 0)),
                            value_mask,
                            packed_constant(context, value.width, 0));
                    }
                    return zero_plane
                        ? value_mask
                        : packed_constant(context, value.width, 0);
                  };
              const auto selected_oversized_component =
                  [&](llvm::Value* component,
                      const bool zero_plane) -> llvm::Value* {
                    auto* positive = oversized_component(
                        component, operation.operation, zero_plane);
                    if (!operation.signed_amount) {
                      return positive;
                    }
                    auto* negative = oversized_component(
                        component,
                        reverse_shift(operation.operation),
                        zero_plane);
                    return builder.CreateSelect(
                        amount_negative, negative, positive);
                  };
              auto* oversized_aval =
                  selected_oversized_component(value.aval, false);
              auto* oversized_bval =
                  selected_oversized_component(
                      value.bval,
                      value.kind == ValueKind::logic9);
              auto* oversized_plane2 =
                  selected_oversized_component(
                      value.logic9_plane2, false);
              auto* oversized_plane3 =
                  selected_oversized_component(
                      value.logic9_plane3, false);
              auto* known_aval = builder.CreateSelect(
                  rotating
                      ? llvm::ConstantInt::getFalse(context)
                      : amount_too_large,
                  oversized_aval,
                  builder.CreateAnd(shifted_aval, value_mask));
              auto* known_bval = builder.CreateSelect(
                  rotating
                      ? llvm::ConstantInt::getFalse(context)
                      : amount_too_large,
                  oversized_bval,
                  builder.CreateAnd(shifted_bval, value_mask));
              auto* known_plane2 = builder.CreateSelect(
                  rotating
                      ? llvm::ConstantInt::getFalse(context)
                      : amount_too_large,
                  oversized_plane2,
                  builder.CreateAnd(shifted_plane2, value_mask));
              auto* known_plane3 = builder.CreateSelect(
                  rotating
                      ? llvm::ConstantInt::getFalse(context)
                      : amount_too_large,
                  oversized_plane3,
                  builder.CreateAnd(shifted_plane3, value_mask));
              if (value.kind == ValueKind::logic9) {
                  store_register(
                      builder,
                      registers,
                      operation.destination,
                      EncodedValue {
                          builder.CreateSelect(
                              amount_unknown,
                              value_mask,
                              known_aval),
                          builder.CreateSelect(
                              amount_unknown,
                              packed_constant(context, value.width, 0),
                              known_bval),
                          value.width,
                          builder.CreateSelect(
                              amount_unknown,
                              packed_constant(context, value.width, 0),
                              known_plane2),
                          builder.CreateSelect(
                              amount_unknown,
                              packed_constant(context, value.width, 0),
                              known_plane3),
                          ValueKind::logic9 });
                  branch_to_next();
                  return;
              }
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateSelect(
                          amount_unknown, value_mask, known_aval),
                      builder.CreateSelect(
                          amount_unknown, value_mask, known_bval),
                      value.width});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const Extract& operation) {
              const auto source =
                  load_register(
                      builder, registers, operation.source);
              auto* shift = packed_constant(
                  context, source.width, operation.offset);
              auto* mask = packed_low_mask(
                  context, source.width, operation.width);
              const auto extract = [&](llvm::Value* value) {
                  return builder.CreateZExtOrTrunc(
                      builder.CreateAnd(
                          builder.CreateLShr(value, shift), mask),
                      packed_integer_type(context, operation.width));
              };
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue {
                      extract(source.aval),
                      extract(source.bval),
                      operation.width,
                      extract(source.logic9_plane2),
                      extract(source.logic9_plane3),
                      source.kind });
              branch_to_next();
            
}


}  // namespace fsim::compiler::llvm_detail
