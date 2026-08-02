// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/Support/ErrorHandling.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace fsim::compiler::llvm_detail {

using runtime::Logic9;
using runtime::simir::DynamicExtract;
using runtime::simir::DynamicPartSelect;
using runtime::simir::Insert;
using runtime::simir::DynamicInsert;
using runtime::simir::DynamicPartInsert;
using runtime::simir::Concatenate;
using runtime::simir::Binary;
using runtime::simir::IntegerUnary;
using runtime::simir::IntegerBinary;
using runtime::simir::IntegerCheck;
using runtime::simir::ConditionalSelect;
using runtime::simir::BinaryOperator;
using runtime::simir::IntegerBinaryOperator;
using runtime::simir::IntegerUnaryOperator;
using runtime::simir::LogicalBinaryOperator;
using runtime::simir::ReductionOperator;
using runtime::simir::ShiftOperator;
using runtime::simir::ValueKind;

void ValueOperationLowerer::lower(
    const DynamicExtract& operation) {
              const auto source = load_register(
                  builder, registers, operation.source);
              auto* shift = dynamic_offset(operation.selection);
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      builder.CreateAnd(
                          builder.CreateLShr(source.aval, shift),
                          constant_i64(context, 1)),
                      builder.CreateAnd(
                          builder.CreateLShr(source.bval, shift),
                          constant_i64(context, 1)),
                      1,
                      builder.CreateAnd(
                          builder.CreateLShr(
                              source.logic9_plane2, shift),
                          constant_i64(context, 1)),
                      builder.CreateAnd(
                          builder.CreateLShr(
                              source.logic9_plane3, shift),
                          constant_i64(context, 1)),
                      source.kind});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const DynamicPartSelect& operation) {
  const auto source = load_register(
      builder, registers, operation.source);
  const auto base = coerce_value_kind(
      builder,
      load_register(builder, registers, operation.base),
      ValueKind::logic4);
  auto* base_unknown = builder.CreateICmpNE(
      builder.CreateAnd(
          base.bval,
          constant_i64(
              context,
              std::numeric_limits<std::uint32_t>::max())),
      constant_i64(context, 0));
  auto* signed_base = builder.CreateSExt(
      builder.CreateTrunc(base.aval, i32), i64);
  auto* lower = llvm::ConstantInt::getSigned(
      i64, std::min(operation.left, operation.right));
  auto* upper = llvm::ConstantInt::getSigned(
      i64, std::max(operation.left, operation.right));

  llvm::Value* aval = constant_i64(context, 0);
  llvm::Value* bval = constant_i64(context, 0);
  llvm::Value* plane2 = constant_i64(context, 0);
  llvm::Value* plane3 = constant_i64(context, 0);
  const auto edge_distance =
      static_cast<std::int64_t>(operation.width - 1U);
  const auto right_delta = operation.increasing
      ? (operation.source_descending ? 0 : edge_distance)
      : (operation.source_descending ? -edge_distance : 0);
  auto* selected_right = builder.CreateAdd(
      signed_base,
      llvm::ConstantInt::getSigned(i64, right_delta));
  for (std::uint32_t bit = 0; bit < operation.width; ++bit) {
    const auto delta = operation.source_descending
        ? static_cast<std::int64_t>(bit)
        : -static_cast<std::int64_t>(bit);
    auto* selected = builder.CreateAdd(
        selected_right,
        llvm::ConstantInt::getSigned(i64, delta));
    auto* in_range = builder.CreateAnd(
        builder.CreateICmpSGE(selected, lower),
        builder.CreateICmpSLE(selected, upper));
    auto* valid = builder.CreateAnd(
        builder.CreateNot(base_unknown), in_range);
    auto* right = llvm::ConstantInt::getSigned(
        i64, operation.right);
    auto* offset = builder.CreateSelect(
        builder.CreateICmpSGE(selected, right),
        builder.CreateSub(selected, right),
        builder.CreateSub(right, selected));
    offset = builder.CreateAdd(
        offset, constant_i64(context, operation.base_offset));
    auto* safe_offset = builder.CreateSelect(
        valid, offset, constant_i64(context, 0));
    const auto select_bit = [&](llvm::Value* plane,
                                const bool unknown_one) {
      auto* extracted = builder.CreateAnd(
          builder.CreateLShr(plane, safe_offset),
          constant_i64(context, 1));
      return builder.CreateSelect(
          valid,
          extracted,
          constant_i64(
              context,
              !operation.two_state && unknown_one ? 1U : 0U));
    };
    const auto append = [&](llvm::Value* result, llvm::Value* value) {
      return builder.CreateOr(
          result,
          builder.CreateShl(
              value, constant_i64(context, bit)));
    };
    aval = append(aval, select_bit(source.aval, true));
    bval = append(
        bval,
        select_bit(
            source.bval,
            source.kind != ValueKind::logic9));
    plane2 = append(
        plane2, select_bit(source.logic9_plane2, false));
    plane3 = append(
        plane3, select_bit(source.logic9_plane3, false));
  }
  store_register(
      builder,
      registers,
      operation.destination,
      EncodedValue{
          aval,
          bval,
          operation.width,
          plane2,
          plane3,
          source.kind});
  branch_to_next();
}

void ValueOperationLowerer::lower(
    const Insert& operation) {
              const auto destination_kind =
                  registers[operation.destination].kind;
              const auto target = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.target),
                  destination_kind);
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  destination_kind);
              auto* source_mask =
                  constant_i64(context, width_mask(source.width));
              auto* shifted_mask =
                  builder.CreateShl(
                      source_mask,
                      constant_i64(context, operation.offset));
              auto* keep_mask =
                  builder.CreateAnd(
                      builder.CreateNot(shifted_mask),
                      constant_i64(
                          context, width_mask(target.width)));
              auto* shift =
                  constant_i64(context, operation.offset);
              auto* aval = builder.CreateOr(
                  builder.CreateAnd(target.aval, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(source.aval, source_mask),
                      shift));
              auto* bval = builder.CreateOr(
                  builder.CreateAnd(target.bval, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(source.bval, source_mask),
                      shift));
              auto* plane2 = builder.CreateOr(
                  builder.CreateAnd(
                      target.logic9_plane2, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(
                          source.logic9_plane2, source_mask),
                      shift));
              auto* plane3 = builder.CreateOr(
                  builder.CreateAnd(
                      target.logic9_plane3, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(
                          source.logic9_plane3, source_mask),
                      shift));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      aval,
                      bval,
                      target.width,
                      plane2,
                      plane3,
                      destination_kind});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const DynamicInsert& operation) {
              const auto destination_kind =
                  registers[operation.destination].kind;
              const auto target = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.target),
                  destination_kind);
              const auto source = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.source),
                  destination_kind);
              auto* shift = dynamic_offset(operation.selection);
              auto* shifted_mask = builder.CreateShl(
                  constant_i64(context, 1), shift);
              auto* keep_mask = builder.CreateAnd(
                  builder.CreateNot(shifted_mask),
                  constant_i64(
                      context, width_mask(target.width)));
              const auto insert_plane =
                  [&](llvm::Value* target_plane,
                      llvm::Value* source_plane) {
                    return builder.CreateOr(
                        builder.CreateAnd(
                            target_plane, keep_mask),
                        builder.CreateShl(
                            builder.CreateAnd(
                                source_plane,
                                constant_i64(context, 1)),
                            shift));
                  };
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      insert_plane(target.aval, source.aval),
                      insert_plane(target.bval, source.bval),
                      target.width,
                      insert_plane(
                          target.logic9_plane2,
                          source.logic9_plane2),
                      insert_plane(
                          target.logic9_plane3,
                          source.logic9_plane3),
                      destination_kind});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const DynamicPartInsert& operation) {
  const auto destination_kind =
      registers[operation.destination].kind;
  const auto target = coerce_value_kind(
      builder,
      load_register(builder, registers, operation.target),
      destination_kind);
  const auto source = coerce_value_kind(
      builder,
      load_register(builder, registers, operation.source),
      destination_kind);
  const auto base = coerce_value_kind(
      builder,
      load_register(
          builder, registers, operation.selection.base),
      ValueKind::logic4);
  auto* base_unknown = builder.CreateICmpNE(
      builder.CreateAnd(
          base.bval,
          constant_i64(
              context,
              std::numeric_limits<std::uint32_t>::max())),
      constant_i64(context, 0));
  auto* signed_base = builder.CreateSExt(
      builder.CreateTrunc(base.aval, i32), i64);
  auto* lower = llvm::ConstantInt::getSigned(
      i64,
      std::min(operation.selection.left, operation.selection.right));
  auto* upper = llvm::ConstantInt::getSigned(
      i64,
      std::max(operation.selection.left, operation.selection.right));
  const auto edge_distance = static_cast<std::int64_t>(
      operation.selection.width - 1U);
  const auto right_delta = operation.selection.increasing
      ? (operation.selection.source_descending ? 0 : edge_distance)
      : (operation.selection.source_descending ? -edge_distance : 0);
  auto* selected_right = builder.CreateAdd(
      signed_base,
      llvm::ConstantInt::getSigned(i64, right_delta));

  auto* aval = target.aval;
  auto* bval = target.bval;
  auto* plane2 = target.logic9_plane2;
  auto* plane3 = target.logic9_plane3;
  const auto target_mask =
      constant_i64(context, width_mask(target.width));
  for (std::uint32_t bit = 0;
       bit < operation.selection.width;
       ++bit) {
    const auto delta = operation.selection.source_descending
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
    auto* right = llvm::ConstantInt::getSigned(
        i64, operation.selection.right);
    auto* offset = builder.CreateSelect(
        builder.CreateICmpSGE(selected, right),
        builder.CreateSub(selected, right),
        builder.CreateSub(right, selected));
    offset = builder.CreateAdd(
        offset,
        constant_i64(context, operation.selection.base_offset));
    auto* safe_offset = builder.CreateSelect(
        valid, offset, constant_i64(context, 0));
    auto* selected_mask = builder.CreateSelect(
        valid,
        builder.CreateShl(
            constant_i64(context, 1), safe_offset),
        constant_i64(context, 0));
    auto* keep_mask = builder.CreateAnd(
        builder.CreateNot(selected_mask), target_mask);
    const auto insert_plane =
        [&](llvm::Value* target_plane,
            llvm::Value* source_plane) {
          auto* source_bit = builder.CreateAnd(
              builder.CreateLShr(
                  source_plane,
                  constant_i64(context, bit)),
              constant_i64(context, 1));
          auto* shifted = builder.CreateShl(
              source_bit, safe_offset);
          return builder.CreateOr(
              builder.CreateAnd(target_plane, keep_mask),
              builder.CreateSelect(
                  valid, shifted, constant_i64(context, 0)));
        };
    aval = insert_plane(aval, source.aval);
    bval = insert_plane(bval, source.bval);
    plane2 = insert_plane(plane2, source.logic9_plane2);
    plane3 = insert_plane(plane3, source.logic9_plane3);
  }
  store_register(
      builder,
      registers,
      operation.destination,
      EncodedValue{
          aval,
          bval,
          target.width,
          plane2,
          plane3,
          destination_kind});
  branch_to_next();
}

void ValueOperationLowerer::lower(
    const Concatenate& operation) {
              llvm::Value* aval = constant_i64(context, 0);
              llvm::Value* bval = constant_i64(context, 0);
              llvm::Value* plane2 = constant_i64(context, 0);
              llvm::Value* plane3 = constant_i64(context, 0);
              const auto destination_kind =
                  registers[operation.destination].kind;
              std::uint32_t offset = 0;
              for (auto operand = operation.operands.rbegin();
                   operand != operation.operands.rend(); ++operand) {
                const auto source = coerce_value_kind(
                    builder,
                    load_register(builder, registers, *operand),
                    destination_kind);
                auto* source_mask =
                    constant_i64(context, width_mask(source.width));
                auto* source_aval =
                    builder.CreateAnd(source.aval, source_mask);
                auto* source_bval =
                    builder.CreateAnd(source.bval, source_mask);
                auto* source_plane2 = builder.CreateAnd(
                    source.logic9_plane2, source_mask);
                auto* source_plane3 = builder.CreateAnd(
                    source.logic9_plane3, source_mask);
                if (offset != 0) {
                  auto* shift = constant_i64(context, offset);
                  source_aval =
                      builder.CreateShl(source_aval, shift);
                  source_bval =
                      builder.CreateShl(source_bval, shift);
                  source_plane2 =
                      builder.CreateShl(source_plane2, shift);
                  source_plane3 =
                      builder.CreateShl(source_plane3, shift);
                }
                aval = builder.CreateOr(aval, source_aval);
                bval = builder.CreateOr(bval, source_bval);
                plane2 = builder.CreateOr(plane2, source_plane2);
                plane3 = builder.CreateOr(plane3, source_plane3);
                offset += source.width;
              }
              auto* mask =
                  constant_i64(context, width_mask(operation.width));
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateAnd(aval, mask),
                      builder.CreateAnd(bval, mask),
                      operation.width,
                      builder.CreateAnd(plane2, mask),
                      builder.CreateAnd(plane3, mask),
                      destination_kind});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const Binary& operation) {
              auto lhs =
                  load_register(builder, registers, operation.lhs);
              auto rhs =
                  load_register(builder, registers, operation.rhs);
              EncodedValue value{};
              if (lhs.kind == ValueKind::logic9
                  || rhs.kind == ValueKind::logic9) {
                lhs = coerce_value_kind(
                    builder, lhs, ValueKind::logic9);
                rhs = coerce_value_kind(
                    builder, rhs, ValueKind::logic9);
                if (operation.operation
                        == BinaryOperator::bit_and
                    || operation.operation
                        == BinaryOperator::bit_or
                    || operation.operation
                        == BinaryOperator::bit_xor) {
                  const auto make_table =
                      [&](const BinaryOperator selected) {
                        std::array<
                            std::array<Logic9, 9>, 9> table{};
                        for (std::size_t left = 0;
                             left < table.size();
                             ++left) {
                          for (std::size_t right = 0;
                               right < table[left].size();
                               ++right) {
                            const auto left_state =
                                static_cast<Logic9>(left);
                            const auto right_state =
                                static_cast<Logic9>(right);
                            table[left][right] =
                                selected
                                        == BinaryOperator::bit_and
                                    ? runtime::logic_and(
                                          left_state,
                                          right_state)
                                    : selected
                                              == BinaryOperator::bit_or
                                          ? runtime::logic_or(
                                                left_state,
                                                right_state)
                                          : runtime::logic_xor(
                                                left_state,
                                                right_state);
                          }
                        }
                        return table;
                      };
                  value = map_logic9_binary(
                      builder,
                      lhs,
                      rhs,
                      make_table(operation.operation));
                } else if (
                    operation.operation
                    == BinaryOperator::vhdl_match_equal) {
                  value = lower_binary(
                      builder, operation.operation, lhs, rhs);
                } else if (
                    operation.operation
                    == BinaryOperator::case_equal) {
                  auto* mask = constant_i64(
                      context, width_mask(lhs.width));
                  auto* mismatch = builder.CreateAnd(
                      builder.CreateOr(
                          builder.CreateOr(
                              builder.CreateXor(
                                  lhs.aval, rhs.aval),
                              builder.CreateXor(
                                  lhs.bval, rhs.bval)),
                          builder.CreateOr(
                              builder.CreateXor(
                                  lhs.logic9_plane2,
                                  rhs.logic9_plane2),
                              builder.CreateXor(
                                  lhs.logic9_plane3,
                                  rhs.logic9_plane3))),
                      mask);
                  auto* equal = builder.CreateICmpEQ(
                      mismatch, constant_i64(context, 0));
                  value = {
                      builder.CreateZExt(equal, i64),
                      constant_i64(context, 0),
                      1};
                } else {
                  value = lower_binary(
                      builder,
                      operation.operation,
                      coerce_value_kind(
                          builder, lhs, ValueKind::logic4),
                      coerce_value_kind(
                          builder, rhs, ValueKind::logic4));
                }
              } else {
                value = lower_binary(
                    builder, operation.operation, lhs, rhs);
              }
              store_register(
                  builder, registers, operation.destination, value);
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const IntegerUnary& operation) {
              const auto source =
                  load_register(
                      builder, registers, operation.source);
              runtime_error_if(
                  builder.CreateICmpNE(
                      builder.CreateAnd(
                          source.bval,
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0)),
                  JitGeneratedRuntimeErrorReason::
                      integer_operand_unknown,
                  "integer.unary.unknown");
              auto* signed_source = builder.CreateSExt(
                  builder.CreateTrunc(source.aval, i32),
                  llvm::Type::getInt64Ty(context));
              auto* minimum = llvm::ConstantInt::getSigned(
                  llvm::Type::getInt64Ty(context),
                  std::numeric_limits<std::int32_t>::min());
              runtime_error_if(
                  builder.CreateICmpEQ(signed_source, minimum),
                  JitGeneratedRuntimeErrorReason::integer_overflow,
                  "integer.unary.overflow");
              llvm::Value* result = nullptr;
              if (operation.operation
                  == IntegerUnaryOperator::negate) {
                result = builder.CreateNeg(signed_source);
              } else {
                result = builder.CreateSelect(
                    builder.CreateICmpSLT(
                        signed_source,
                        constant_i64(context, 0)),
                    builder.CreateNeg(signed_source),
                    signed_source);
              }
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      builder.CreateAnd(
                          result,
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0),
                      32});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const IntegerBinary& operation) {
              const auto lhs = load_register(
                  builder, registers, operation.lhs);
              const auto rhs = load_register(
                  builder, registers, operation.rhs);
              runtime_error_if(
                  builder.CreateICmpNE(
                      builder.CreateAnd(
                          builder.CreateOr(lhs.bval, rhs.bval),
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0)),
                  JitGeneratedRuntimeErrorReason::
                      integer_operand_unknown,
                  "integer.binary.unknown");
              auto* left = builder.CreateSExt(
                  builder.CreateTrunc(lhs.aval, i32), i64);
              auto* right = builder.CreateSExt(
                  builder.CreateTrunc(rhs.aval, i32), i64);
              auto* minimum = llvm::ConstantInt::getSigned(
                  i64, std::numeric_limits<std::int32_t>::min());
              auto* maximum = llvm::ConstantInt::getSigned(
                  i64, std::numeric_limits<std::int32_t>::max());
              const auto overflow_if =
                  [&](llvm::Value* value,
                      const std::string_view label) {
                    runtime_error_if(
                        builder.CreateOr(
                            builder.CreateICmpSLT(value, minimum),
                            builder.CreateICmpSGT(value, maximum)),
                        JitGeneratedRuntimeErrorReason::
                            integer_overflow,
                        label);
                  };
              llvm::Value* result = nullptr;
              switch (operation.operation) {
              case IntegerBinaryOperator::add:
                result = builder.CreateAdd(left, right);
                overflow_if(result, "integer.add.overflow");
                break;
              case IntegerBinaryOperator::subtract:
                result = builder.CreateSub(left, right);
                overflow_if(result, "integer.subtract.overflow");
                break;
              case IntegerBinaryOperator::multiply:
                result = builder.CreateMul(left, right);
                overflow_if(result, "integer.multiply.overflow");
                break;
              case IntegerBinaryOperator::power: {
                runtime_error_if(
                    builder.CreateICmpSLT(
                        right, constant_i64(context, 0)),
                    JitGeneratedRuntimeErrorReason::
                        integer_negative_exponent,
                    "integer.power.exponent");
                llvm::Value* powered = constant_i64(context, 1);
                llvm::Value* factor = left;
                for (std::uint32_t bit = 0; bit < 31; ++bit) {
                  auto* selected = builder.CreateICmpNE(
                      builder.CreateAnd(
                          builder.CreateLShr(
                              right, constant_i64(context, bit)),
                          constant_i64(context, 1)),
                      constant_i64(context, 0));
                  auto* product =
                      builder.CreateMul(powered, factor);
                  runtime_error_if(
                      builder.CreateAnd(
                          selected,
                          builder.CreateOr(
                              builder.CreateICmpSLT(
                                  product, minimum),
                              builder.CreateICmpSGT(
                                  product, maximum))),
                      JitGeneratedRuntimeErrorReason::
                          integer_overflow,
                      "integer.power.product");
                  powered = builder.CreateSelect(
                      selected, product, powered);
                  if (bit + 1U < 31U) {
                    auto* remaining = builder.CreateLShr(
                        right, constant_i64(context, bit + 1U));
                    auto* needed = builder.CreateICmpNE(
                        remaining, constant_i64(context, 0));
                    auto* squared =
                        builder.CreateMul(factor, factor);
                    runtime_error_if(
                        builder.CreateAnd(
                            needed,
                            builder.CreateOr(
                                builder.CreateICmpSLT(
                                    squared, minimum),
                                builder.CreateICmpSGT(
                                    squared, maximum))),
                        JitGeneratedRuntimeErrorReason::
                            integer_overflow,
                        "integer.power.factor");
                    factor = builder.CreateSelect(
                        needed, squared, factor);
                  }
                }
                result = powered;
                break;
              }
              case IntegerBinaryOperator::divide:
              case IntegerBinaryOperator::remainder:
              case IntegerBinaryOperator::modulo: {
                runtime_error_if(
                    builder.CreateICmpEQ(
                        right, constant_i64(context, 0)),
                    JitGeneratedRuntimeErrorReason::
                        integer_division_by_zero,
                    "integer.division.zero");
                runtime_error_if(
                    builder.CreateAnd(
                        builder.CreateICmpEQ(left, minimum),
                        builder.CreateICmpEQ(
                            right,
                            llvm::ConstantInt::getSigned(i64, -1))),
                    JitGeneratedRuntimeErrorReason::
                        integer_overflow,
                    "integer.division.overflow");
                if (operation.operation
                    == IntegerBinaryOperator::divide) {
                  result = builder.CreateSDiv(left, right);
                } else {
                  result = builder.CreateSRem(left, right);
                  if (operation.operation
                      == IntegerBinaryOperator::modulo) {
                    auto* nonzero = builder.CreateICmpNE(
                        result, constant_i64(context, 0));
                    auto* signs_differ = builder.CreateICmpNE(
                        builder.CreateICmpSLT(
                            result, constant_i64(context, 0)),
                        builder.CreateICmpSLT(
                            right, constant_i64(context, 0)));
                    result = builder.CreateSelect(
                        builder.CreateAnd(nonzero, signs_differ),
                        builder.CreateAdd(result, right),
                        result);
                  }
                }
                break;
              }
              }
              store_register(
                  builder,
                  registers,
                  operation.destination,
                  EncodedValue{
                      builder.CreateAnd(
                          result,
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0),
                      32});
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const IntegerCheck& operation) {
              const auto source = load_register(
                  builder, registers, operation.source);
              runtime_error_if(
                  builder.CreateICmpNE(
                      builder.CreateAnd(
                          source.bval,
                          constant_i64(
                              context,
                              std::numeric_limits<std::uint32_t>::max())),
                      constant_i64(context, 0)),
                  JitGeneratedRuntimeErrorReason::
                      integer_operand_unknown,
                  "integer.check.unknown");
              auto* value = builder.CreateSExt(
                  builder.CreateTrunc(
                      source.aval,
                      llvm::Type::getInt32Ty(context)),
                  llvm::Type::getInt64Ty(context));
              auto* lower = llvm::ConstantInt::getSigned(
                  llvm::Type::getInt64Ty(context),
                  operation.lower);
              auto* upper = llvm::ConstantInt::getSigned(
                  llvm::Type::getInt64Ty(context),
                  operation.upper);
              runtime_error_if(
                  builder.CreateOr(
                      builder.CreateICmpSLT(value, lower),
                      builder.CreateICmpSGT(value, upper)),
                  JitGeneratedRuntimeErrorReason::
                      integer_subtype_range,
                  "integer.check.range");
              branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const ConditionalSelect& operation) {
              const auto condition = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.condition),
                  ValueKind::logic4);
              const auto destination_kind =
                  registers[operation.destination].kind;
              const auto when_true = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.when_true),
                  destination_kind);
              const auto when_false = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.when_false),
                  destination_kind);
              auto *mask =
                  constant_i64(context, width_mask(when_true.width));
              auto *different = builder.CreateAnd(
                  builder.CreateOr(
                      builder.CreateOr(
                          builder.CreateXor(
                              when_true.aval, when_false.aval),
                          builder.CreateXor(
                              when_true.bval,
                              when_false.bval)),
                      builder.CreateOr(
                          builder.CreateXor(
                              when_true.logic9_plane2,
                              when_false.logic9_plane2),
                          builder.CreateXor(
                              when_true.logic9_plane3,
                              when_false.logic9_plane3))),
                  mask);
              auto *same = builder.CreateXor(different, mask);
              auto *merged_aval = builder.CreateOr(
                  builder.CreateAnd(when_true.aval, same),
                  different);
              auto *merged_bval = builder.CreateOr(
                  builder.CreateAnd(when_true.bval, same),
                  destination_kind == ValueKind::logic9
                      ? constant_i64(context, 0)
                      : different);
              auto* merged_plane2 = builder.CreateAnd(
                  when_true.logic9_plane2, same);
              auto* merged_plane3 = builder.CreateAnd(
                  when_true.logic9_plane3, same);
              auto *unknown = builder.CreateICmpNE(
                  builder.CreateAnd(
                      condition.bval, constant_i64(context, 1)),
                  constant_i64(context, 0));
              auto *select_true = builder.CreateICmpNE(
                  builder.CreateAnd(
                      condition.aval, constant_i64(context, 1)),
                  constant_i64(context, 0));
              auto *known_aval = builder.CreateSelect(
                  select_true, when_true.aval, when_false.aval);
              auto *known_bval = builder.CreateSelect(
                  select_true, when_true.bval, when_false.bval);
              auto* known_plane2 = builder.CreateSelect(
                  select_true,
                  when_true.logic9_plane2,
                  when_false.logic9_plane2);
              auto* known_plane3 = builder.CreateSelect(
                  select_true,
                  when_true.logic9_plane3,
                  when_false.logic9_plane3);
              store_register(
                  builder, registers, operation.destination,
                  EncodedValue{
                      builder.CreateSelect(
                          unknown, merged_aval, known_aval),
                      builder.CreateSelect(
                          unknown, merged_bval, known_bval),
                      when_true.width,
                      builder.CreateSelect(
                          unknown, merged_plane2, known_plane2),
                      builder.CreateSelect(
                          unknown, merged_plane3, known_plane3),
                      destination_kind});
              branch_to_next();
            
}


}  // namespace fsim::compiler::llvm_detail
