// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llvm_jit_internal.hpp"

#include <llvm/IR/IRBuilder.h>

#include <array>
#include <cstdint>
#include <vector>

namespace fsim::compiler::llvm_detail {

struct EncodedValue {
  llvm::Value* aval{};
  llvm::Value* bval{};
  std::uint32_t width{};
  llvm::Value* logic9_plane2{};
  llvm::Value* logic9_plane3{};
  runtime::simir::ValueKind kind{
      runtime::simir::ValueKind::logic4};
};

struct RegisterSlot {
  llvm::Value* aval{};
  llvm::Value* bval{};
  llvm::Value* initialized{};
  std::uint32_t width{};
  llvm::Value* logic9_plane2{};
  llvm::Value* logic9_plane3{};
  runtime::simir::ValueKind kind{
      runtime::simir::ValueKind::logic4};
};

struct EncodedBit {
  llvm::Value* aval{};
  llvm::Value* bval{};
};

[[nodiscard]] runtime::simir::ShiftOperator reverse_shift(
    runtime::simir::ShiftOperator operation) noexcept;

[[nodiscard]] EncodedValue load_register(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId id);

[[nodiscard]] EncodedValue coerce_value_kind(
    llvm::IRBuilder<>& builder,
    EncodedValue value,
    runtime::simir::ValueKind destination_kind);

[[nodiscard]] llvm::Value* logic9_state_mask(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value,
    std::uint8_t state);

[[nodiscard]] EncodedValue map_logic9_unary(
    llvm::IRBuilder<>& builder,
    const EncodedValue& value,
    const std::array<runtime::Logic9, 9>& table);

[[nodiscard]] EncodedValue map_logic9_binary(
    llvm::IRBuilder<>& builder,
    const EncodedValue& lhs,
    const EncodedValue& rhs,
    const std::array<std::array<runtime::Logic9, 9>, 9>& table);

void store_register(
    llvm::IRBuilder<>& builder,
    const std::vector<RegisterSlot>& registers,
    runtime::simir::RegisterId id,
    EncodedValue value);

[[nodiscard]] std::uint64_t width_mask(std::uint32_t width) noexcept;

[[nodiscard]] llvm::ConstantInt* constant_i64(
    llvm::LLVMContext& context,
    std::uint64_t value);

[[nodiscard]] EncodedBit bit_at(
    llvm::IRBuilder<>& builder,
    EncodedValue value,
    std::uint32_t index);

[[nodiscard]] EncodedBit truth_bit(
    llvm::IRBuilder<>& builder,
    EncodedValue value);

[[nodiscard]] EncodedBit bit_xor(
    llvm::IRBuilder<>& builder,
    EncodedBit lhs,
    EncodedBit rhs);

[[nodiscard]] EncodedBit bit_and(
    llvm::IRBuilder<>& builder,
    EncodedBit lhs,
    EncodedBit rhs);

[[nodiscard]] EncodedBit bit_or(
    llvm::IRBuilder<>& builder,
    EncodedBit lhs,
    EncodedBit rhs);

[[nodiscard]] EncodedValue lower_binary(
    llvm::IRBuilder<>& builder,
    runtime::simir::BinaryOperator operation,
    EncodedValue lhs,
    EncodedValue rhs);

}  // namespace fsim::compiler::llvm_detail
