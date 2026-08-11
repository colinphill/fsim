// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/ErrorHandling.h>

#include <array>
#include <cstdint>
#include <limits>

namespace fsim::compiler::llvm_detail {

using runtime::Logic9;
using runtime::simir::CopyRegister;
using runtime::simir::UnaryNot;
using runtime::simir::LogicalNot;
using runtime::simir::LogicalBinary;
using runtime::simir::Reduction;
using runtime::simir::CountOnes;
using runtime::simir::CountBits;
using runtime::simir::Shift;
using runtime::simir::Extract;
using runtime::simir::BinaryOperator;
using runtime::simir::IntegerBinaryOperator;
using runtime::simir::IntegerUnaryOperator;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::ReductionOperator;
using runtime::simir::ShiftOperator;
using runtime::simir::ValueKind;

void ValueOperationLowerer::lower(
    const CopyRegister& operation) {
              store_register(
                  builder, registers, operation.destination,
                  load_register(
                      builder, registers, operation.source));
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const UnaryNot& operation) {
              const auto source =
                  load_register(builder, registers, operation.source);
              if (source.kind == ValueKind::logic9) {
                constexpr auto table = [] {
                  std::array<Logic9, 9> values{};
                  for (std::size_t state = 0;
                       state < values.size();
                       ++state) {
                    values[state] = runtime::logic_not(
                        static_cast<Logic9>(state));
                  }
                  return values;
                }();
                store_register(
                    builder,
                    registers,
                    operation.destination,
                    map_logic9_unary(builder, source, table));
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
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              if (operation.operation
                      == ReductionOperator::one_hot
                  || operation.operation
                      == ReductionOperator::one_hot_or_zero) {
                llvm::Value* seen_one =
                    llvm::ConstantInt::getFalse(context);
                llvm::Value* multiple_ones =
                    llvm::ConstantInt::getFalse(context);
                for (std::uint32_t bit = 0;
                     bit < source.width;
                     ++bit) {
                  const auto value =
                      bit_at(builder, source, bit);
                  auto* exact_one = builder.CreateAnd(
                      value.aval,
                      builder.CreateNot(value.bval));
                  multiple_ones = builder.CreateOr(
                      multiple_ones,
                      builder.CreateAnd(seen_one, exact_one));
                  seen_one =
                      builder.CreateOr(seen_one, exact_one);
                }
                auto* matched =
                    operation.operation
                            == ReductionOperator::one_hot
                        ? builder.CreateAnd(
                              seen_one,
                              builder.CreateNot(multiple_ones))
                        : builder.CreateNot(multiple_ones);
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
              EncodedBit result{
                  operation.operation == ReductionOperator::bit_and
                      ? llvm::ConstantInt::getTrue(context)
                      : llvm::ConstantInt::getFalse(context),
                  llvm::ConstantInt::getFalse(context)};
              for (std::uint32_t bit = 0; bit < source.width; ++bit) {
                const auto value = bit_at(builder, source, bit);
                if (operation.operation
                    == ReductionOperator::bit_and) {
                  result = bit_and(builder, result, value);
                } else if (
                    operation.operation
                    == ReductionOperator::bit_or) {
                  result = bit_or(builder, result, value);
                } else {
                  result = bit_xor(builder, result, value);
                }
              }
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
    const CountOnes& operation) {
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  ValueKind::logic4);
              llvm::Value* count = llvm::ConstantInt::get(
                  llvm::Type::getInt64Ty(context), 0);
              for (std::uint32_t bit = 0;
                   bit < source.width;
                   ++bit) {
                const auto value =
                    bit_at(builder, source, bit);
                auto* exact_one = builder.CreateAnd(
                    value.aval,
                    builder.CreateNot(value.bval));
                count = builder.CreateAdd(
                    count,
                    builder.CreateZExt(
                        exact_one,
                        llvm::Type::getInt64Ty(context)));
              }
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
              llvm::Value* count = llvm::ConstantInt::get(
                  llvm::Type::getInt64Ty(context), 0);
              for (std::uint32_t bit = 0;
                   bit < source.width;
                   ++bit) {
                const auto value =
                    bit_at(builder, source, bit);
                auto* not_aval = builder.CreateNot(value.aval);
                auto* not_bval = builder.CreateNot(value.bval);
                llvm::Value* selected =
                    llvm::ConstantInt::getFalse(context);
                if ((operation.state_mask & 0x1U) != 0) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(not_aval, not_bval));
                }
                if ((operation.state_mask & 0x2U) != 0) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(value.aval, not_bval));
                }
                if ((operation.state_mask & 0x4U) != 0) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(value.aval, value.bval));
                }
                if ((operation.state_mask & 0x8U) != 0) {
                  selected = builder.CreateOr(
                      selected,
                      builder.CreateAnd(not_aval, value.bval));
                }
                count = builder.CreateAdd(
                    count,
                    builder.CreateZExt(
                        selected,
                        llvm::Type::getInt64Ty(context)));
              }
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
              const auto shift_component =
                  [&](llvm::Value* component,
                      const ShiftOperator selected_operation,
                      const bool zero_plane)
                      -> llvm::Value* {
                    if (selected_operation
                            == ShiftOperator::rotate_left
                        || selected_operation
                            == ShiftOperator::rotate_right) {
                        auto* inverse_amount = builder.CreateURem(
                            builder.CreateSub(
                                packed_constant(
                                    context, value.width, value.width),
                                value_amount),
                            packed_constant(
                                context, value.width, value.width));
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
