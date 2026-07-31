// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>

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
      i64, std::min(selection.left, selection.right));
  auto* upper = llvm::ConstantInt::getSigned(
      i64, std::max(selection.left, selection.right));
  const auto edge_distance =
      static_cast<std::int64_t>(selection.width - 1U);
  const auto right_delta = selection.increasing
      ? (selection.source_descending ? 0 : edge_distance)
      : (selection.source_descending ? -edge_distance : 0);
  auto* selected_right = builder.CreateAdd(
      signed_base,
      llvm::ConstantInt::getSigned(i64, right_delta));
  llvm::Value* selected_width = constant_i64(context, 0);
  llvm::Value* first_offset = constant_i64(context, 0);
  std::array<llvm::Value*, 4> selected_planes{
      constant_i64(context, 0),
      constant_i64(context, 0),
      constant_i64(context, 0),
      constant_i64(context, 0)};
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
              source_planes[plane], constant_i64(context, bit)),
          constant_i64(context, 1));
      auto* appended = builder.CreateOr(
          selected_planes[plane],
          builder.CreateShl(source_bit, selected_width));
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

}  // namespace fsim::compiler::llvm_detail
