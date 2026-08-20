// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
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
  const auto destination_kind = registers[operation.destination].kind;
  const auto source = coerce_value_kind(
      builder,
      load_register(builder, registers, operation.source),
      destination_kind);
  const auto index = coerce_value_kind(
      builder,
      load_register(builder, registers, operation.selection.index),
      ValueKind::logic4);
  auto* unknown = builder.CreateICmpNE(
      builder.CreateAnd(
          index.bval,
          constant_i64(
              context,
              std::numeric_limits<std::uint32_t>::max())),
      constant_i64(context, 0));
  auto* selected = builder.CreateSExt(
      builder.CreateTrunc(index.aval, i32), i64);
  auto* lower = llvm::ConstantInt::getSigned(
      i64, std::min(operation.selection.left, operation.selection.right));
  auto* upper = llvm::ConstantInt::getSigned(
      i64, std::max(operation.selection.left, operation.selection.right));
  auto* valid = builder.CreateAnd(
      builder.CreateNot(unknown),
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
      offset, constant_i64(context, operation.selection.base_offset));
  auto* safe_offset = builder.CreateSelect(
      valid, offset, constant_i64(context, 0));
  auto* shift = builder.CreateZExtOrTrunc(
      safe_offset, packed_integer_type(context, source.width));
  const auto extract = [&](llvm::Value* value,
                           const bool unknown_one) {
      auto* extracted = builder.CreateZExtOrTrunc(
          builder.CreateAnd(
              builder.CreateLShr(value, shift),
              packed_constant(context, source.width, 1)),
          i64);
      return builder.CreateSelect(
          valid,
          extracted,
          constant_i64(context, unknown_one ? 1U : 0U));
  };
  store_register(
      builder,
      registers,
      operation.destination,
      EncodedValue {
          extract(source.aval, true),
          extract(
              source.bval,
              destination_kind == ValueKind::logic4),
          1,
          extract(source.logic9_plane2, false),
          extract(source.logic9_plane3, false),
          destination_kind });
  branch_to_next();
            
}

void ValueOperationLowerer::lower(
    const DynamicPartSelect& operation) {
  if (dynamic_part_signal_source) {
    const auto base = coerce_value_kind(
        builder,
        load_register(builder, registers, operation.base),
        ValueKind::logic4);
    runtime_error_if(
        builder.CreateICmpEQ(
            read_signal_dynamic_part_callback,
            llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(
                    read_signal_dynamic_part_callback->getType()))),
        JitGeneratedRuntimeErrorReason::signal_callback_failure,
        "dynamic.part.signal.callback");
    const auto flags = (operation.increasing ? UINT32_C(1) : UINT32_C(0))
        | (operation.source_descending ? UINT32_C(2) : UINT32_C(0))
        | (operation.two_state ? UINT32_C(4) : UINT32_C(0));
    auto* status = builder.CreateCall(
        read_signal_dynamic_part_type,
        read_signal_dynamic_part_callback,
        { context_pointer,
            llvm::ConstantInt::get(i32, *dynamic_part_signal_source),
            llvm::ConstantInt::get(
                i32, registers[operation.source].width),
            base.aval,
            base.bval,
            llvm::ConstantInt::getSigned(i64, operation.left),
            llvm::ConstantInt::getSigned(i64, operation.right),
            llvm::ConstantInt::get(i32, operation.base_offset),
            llvm::ConstantInt::get(i32, operation.width),
            llvm::ConstantInt::get(i32, flags),
            logic9_word_slot });
    runtime_error_if(
        builder.CreateICmpNE(status, llvm::ConstantInt::get(i32, 0)),
        JitGeneratedRuntimeErrorReason::signal_callback_failure,
        "dynamic.part.signal.read");
    const auto load_plane = [&](const std::uint32_t plane) {
      return builder.CreateLoad(
          i64,
          builder.CreateInBoundsGEP(
              i64,
              logic9_word_slot,
              llvm::ConstantInt::get(i32, plane)));
    };
    store_register(
        builder,
        registers,
        operation.destination,
        EncodedValue {
            load_plane(0),
            load_plane(1),
            operation.width,
            load_plane(2),
            load_plane(3),
            registers[operation.source].kind });
    branch_to_next();
    return;
  }
  if (constant_part_select_source != nullptr && operation.width <= 64U
      && constant_part_select_source->width()
          == registers[operation.source].width
      && constant_part_select_source->is_logic9()
          == (registers[operation.source].kind == ValueKind::logic9)) {
    const auto lower_bound = std::min(operation.left, operation.right);
    const auto upper_bound = std::max(operation.left, operation.right);
    const auto edge = static_cast<std::int64_t>(operation.width - 1U);
    const auto table_min = operation.increasing
        ? lower_bound - edge : lower_bound;
    const auto table_max = operation.increasing
        ? upper_bound : upper_bound + edge;
    const auto entry_count = static_cast<std::uint64_t>(
        table_max - table_min + 1);
    constexpr std::uint64_t maximum_constant_select_entries = 65536U;
    if (entry_count <= maximum_constant_select_entries) {
      const auto source_kind = registers[operation.source].kind;
      const auto width_mask = operation.width == 64U
          ? std::numeric_limits<std::uint64_t>::max()
          : (UINT64_C(1) << operation.width) - 1U;
      const std::array invalid_planes {
          operation.two_state ? UINT64_C(0) : width_mask,
          !operation.two_state && source_kind != ValueKind::logic9
              ? width_mask : UINT64_C(0),
          UINT64_C(0),
          UINT64_C(0)
      };
      std::array<std::vector<std::uint64_t>, 4> tables;
      for (std::size_t plane = 0; plane < tables.size(); ++plane) {
        tables[plane].assign(
            static_cast<std::size_t>(entry_count),
            invalid_planes[plane]);
      }
      const auto source_bit = [&](const std::size_t plane,
                                  const std::uint64_t offset) {
        if (plane >= 2U && source_kind != ValueKind::logic9) {
          return false;
        }
        const auto words = source_kind == ValueKind::logic9
            ? constant_part_select_source->logic9_plane_words(plane)
            : (plane == 0U
                  ? constant_part_select_source->aval_words()
                  : constant_part_select_source->bval_words());
        if (offset / 64U >= words.size()) {
          return false;
        }
        return ((words[offset / 64U] >> (offset % 64U)) & 1U) != 0U;
      };
      const auto right_delta = operation.increasing
          ? (operation.source_descending ? 0 : edge)
          : (operation.source_descending ? -edge : 0);
      for (std::int64_t base_value = table_min;
           base_value <= table_max; ++base_value) {
        const auto entry = static_cast<std::size_t>(
            base_value - table_min);
        for (std::uint32_t bit = 0; bit < operation.width; ++bit) {
          const auto delta = operation.source_descending
              ? static_cast<std::int64_t>(bit)
              : -static_cast<std::int64_t>(bit);
          const auto selected = base_value + right_delta + delta;
          if (selected < lower_bound || selected > upper_bound) {
            continue;
          }
          const auto distance = selected >= operation.right
              ? selected - operation.right
              : operation.right - selected;
          const auto offset = static_cast<std::uint64_t>(distance)
              + operation.base_offset;
          const auto result_mask = UINT64_C(1) << bit;
          for (std::size_t plane = 0; plane < tables.size(); ++plane) {
            tables[plane][entry] &= ~result_mask;
            if (source_bit(plane, offset)) {
              tables[plane][entry] |= result_mask;
            }
          }
        }
      }

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
      auto* valid = builder.CreateAnd(
          builder.CreateNot(base_unknown),
          builder.CreateAnd(
              builder.CreateICmpSGE(
                  signed_base,
                  llvm::ConstantInt::getSigned(i64, table_min)),
              builder.CreateICmpSLE(
                  signed_base,
                  llvm::ConstantInt::getSigned(i64, table_max))));
      auto* table_index = builder.CreateSelect(
          valid,
          builder.CreateSub(
              signed_base,
              llvm::ConstantInt::getSigned(i64, table_min)),
          constant_i64(context, 0));
      auto* element_type = packed_integer_type(context, operation.width);
      auto* array_type = llvm::ArrayType::get(element_type, entry_count);
      auto* module = builder.GetInsertBlock()->getModule();
      std::array<llvm::Value*, 4> selected_planes { };
      for (std::size_t plane = 0; plane < tables.size(); ++plane) {
        std::vector<llvm::Constant*> entries;
        entries.reserve(tables[plane].size());
        for (const auto value : tables[plane]) {
          entries.push_back(llvm::ConstantInt::get(element_type, value));
        }
        auto* global = new llvm::GlobalVariable(
            *module,
            array_type,
            true,
            llvm::GlobalValue::PrivateLinkage,
            llvm::ConstantArray::get(array_type, entries),
            "fsim.constant.part.select");
        global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        auto* address = builder.CreateInBoundsGEP(
            array_type,
            global,
            { constant_i64(context, 0), table_index });
        selected_planes[plane] = builder.CreateSelect(
            valid,
            builder.CreateLoad(element_type, address),
            llvm::ConstantInt::get(element_type, invalid_planes[plane]));
      }
      store_register(
          builder,
          registers,
          operation.destination,
          EncodedValue {
              selected_planes[0],
              selected_planes[1],
              operation.width,
              selected_planes[2],
              selected_planes[3],
              source_kind });
      branch_to_next();
      return;
    }
  }
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

  llvm::Value* aval = packed_constant(context, operation.width, 0);
  llvm::Value* bval = packed_constant(context, operation.width, 0);
  llvm::Value* plane2 = packed_constant(context, operation.width, 0);
  llvm::Value* plane3 = packed_constant(context, operation.width, 0);
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
        auto* packed_offset = builder.CreateZExtOrTrunc(
            safe_offset, packed_integer_type(context, source.width));
        auto* extracted = builder.CreateAnd(
            builder.CreateLShr(plane, packed_offset),
            packed_constant(context, source.width, 1));
        return builder.CreateZExtOrTrunc(builder.CreateSelect(
                                             valid,
                                             extracted,
                                             packed_constant(
                                                 context,
                                                 source.width,
                                                 !operation.two_state && unknown_one ? 1U : 0U)),
            packed_integer_type(context, operation.width));
    };
    const auto append = [&](llvm::Value* result, llvm::Value* value) {
        return builder.CreateOr(
            result,
            builder.CreateShl(
                value, packed_constant(context, operation.width, bit)));
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
              auto* target_mask = packed_mask(context, target.width);
              const auto widen = [&](llvm::Value* value) {
                  return builder.CreateZExtOrTrunc(
                      value, packed_integer_type(context, target.width));
              };
              auto* shifted_mask = builder.CreateShl(
                  packed_low_mask(
                      context, target.width, source.width),
                  packed_constant(
                      context, target.width, operation.offset));
              auto* keep_mask = builder.CreateAnd(
                  builder.CreateNot(shifted_mask),
                  target_mask);
              auto* shift = packed_constant(context, target.width, operation.offset);
              auto* aval = builder.CreateOr(
                  builder.CreateAnd(target.aval, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(
                          widen(source.aval),
                          packed_low_mask(
                              context, target.width, source.width)),
                      shift));
              auto* bval = builder.CreateOr(
                  builder.CreateAnd(target.bval, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(
                          widen(source.bval),
                          packed_low_mask(
                              context, target.width, source.width)),
                      shift));
              auto* plane2 = builder.CreateOr(
                  builder.CreateAnd(
                      target.logic9_plane2, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(
                          widen(source.logic9_plane2),
                          packed_low_mask(
                              context, target.width, source.width)),
                      shift));
              auto* plane3 = builder.CreateOr(
                  builder.CreateAnd(
                      target.logic9_plane3, keep_mask),
                  builder.CreateShl(
                      builder.CreateAnd(
                          widen(source.logic9_plane3),
                          packed_low_mask(
                              context, target.width, source.width)),
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
              const auto index = coerce_value_kind(
                  builder,
                  load_register(
                      builder, registers, operation.selection.index),
                  ValueKind::logic4);
              auto* unknown = builder.CreateICmpNE(
                  builder.CreateAnd(
                      index.bval,
                      constant_i64(
                          context,
                          std::numeric_limits<std::uint32_t>::max())),
                  constant_i64(context, 0));
              auto* selected = builder.CreateSExt(
                  builder.CreateTrunc(index.aval, i32), i64);
              auto* lower = llvm::ConstantInt::getSigned(
                  i64,
                  std::min(
                      operation.selection.left,
                      operation.selection.right));
              auto* upper = llvm::ConstantInt::getSigned(
                  i64,
                  std::max(
                      operation.selection.left,
                      operation.selection.right));
              auto* valid = builder.CreateAnd(
                  builder.CreateNot(unknown),
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
              auto* shift = builder.CreateZExtOrTrunc(
                  safe_offset,
                  packed_integer_type(context, target.width));
              auto* shifted_mask = builder.CreateSelect(
                  valid,
                  builder.CreateShl(
                      packed_constant(context, target.width, 1), shift),
                  packed_constant(context, target.width, 0));
              auto* keep_mask = builder.CreateAnd(
                  builder.CreateNot(shifted_mask),
                  packed_mask(context, target.width));
              const auto insert_plane =
                  [&](llvm::Value* target_plane,
                      llvm::Value* source_plane) {
                      auto* inserted = builder.CreateShl(
                          builder.CreateAnd(
                              builder.CreateZExtOrTrunc(
                                  source_plane,
                                  packed_integer_type(
                                      context, target.width)),
                              packed_constant(
                                  context, target.width, 1)),
                          shift);
                      return builder.CreateOr(
                          builder.CreateAnd(
                              target_plane, keep_mask),
                          builder.CreateSelect(
                              valid,
                              inserted,
                              packed_constant(context, target.width, 0)));
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
  const auto target_mask = packed_mask(context, target.width);
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
            packed_constant(context, target.width, 1),
            builder.CreateZExtOrTrunc(
                safe_offset,
                packed_integer_type(context, target.width))),
        packed_constant(context, target.width, 0));
    auto* keep_mask = builder.CreateAnd(
        builder.CreateNot(selected_mask), target_mask);
    const auto insert_plane =
        [&](llvm::Value* target_plane,
            llvm::Value* source_plane) {
            auto* source_bit = builder.CreateAnd(
                builder.CreateLShr(
                    source_plane,
                    packed_constant(context, source.width, bit)),
                packed_constant(context, source.width, 1));
            auto* shifted = builder.CreateShl(
                builder.CreateZExtOrTrunc(
                    source_bit,
                    packed_integer_type(context, target.width)),
                builder.CreateZExtOrTrunc(
                    safe_offset,
                    packed_integer_type(context, target.width)));
            return builder.CreateOr(
                builder.CreateAnd(target_plane, keep_mask),
                builder.CreateSelect(
                    valid,
                    shifted,
                    packed_constant(context, target.width, 0)));
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
    llvm::Value* aval = packed_constant(
        context, operation.width, 0);
    llvm::Value* bval = packed_constant(
        context, operation.width, 0);
    llvm::Value* plane2 = packed_constant(
        context, operation.width, 0);
    llvm::Value* plane3 = packed_constant(
        context, operation.width, 0);
    const auto destination_kind = registers[operation.destination].kind;
    std::uint32_t offset = 0;
    for (auto operand = operation.operands.rbegin();
        operand != operation.operands.rend(); ++operand) {
        const auto source = coerce_value_kind(
            builder,
            load_register(builder, registers, *operand),
            destination_kind);
        auto* source_mask = packed_mask(context, source.width);
        auto* source_aval = builder.CreateAnd(source.aval, source_mask);
        auto* source_bval = builder.CreateAnd(source.bval, source_mask);
        auto* source_plane2 = builder.CreateAnd(
            source.logic9_plane2, source_mask);
        auto* source_plane3 = builder.CreateAnd(
            source.logic9_plane3, source_mask);
        const auto widen = [&](llvm::Value* value) {
            return builder.CreateZExtOrTrunc(
                value,
                packed_integer_type(context, operation.width));
        };
        source_aval = widen(source_aval);
        source_bval = widen(source_bval);
        source_plane2 = widen(source_plane2);
        source_plane3 = widen(source_plane3);
        if (offset != 0) {
            auto* shift = packed_constant(
                context, operation.width, offset);
            source_aval = builder.CreateShl(source_aval, shift);
            source_bval = builder.CreateShl(source_bval, shift);
            source_plane2 = builder.CreateShl(source_plane2, shift);
            source_plane3 = builder.CreateShl(source_plane3, shift);
        }
        aval = builder.CreateOr(aval, source_aval);
        bval = builder.CreateOr(bval, source_bval);
        plane2 = builder.CreateOr(plane2, source_plane2);
        plane3 = builder.CreateOr(plane3, source_plane3);
        offset += source.width;
    }
    auto* mask = packed_mask(context, operation.width);
    store_register(
        builder, registers, operation.destination,
        EncodedValue {
            builder.CreateAnd(aval, mask),
            builder.CreateAnd(bval, mask),
            operation.width,
            builder.CreateAnd(plane2, mask),
            builder.CreateAnd(plane3, mask),
            destination_kind });
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
                  value = lower_logic9_binary(
                      builder, lhs, rhs, operation.operation);
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
              auto* mask = packed_mask(context, when_true.width);
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
              auto* merged_bval = builder.CreateOr(
                  builder.CreateAnd(when_true.bval, same),
                  destination_kind == ValueKind::logic9
                      ? packed_constant(context, when_true.width, 0)
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
