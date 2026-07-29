// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

#include <array>
#include <cstdint>

namespace fsim::compiler::llvm_detail {

using runtime::simir::LoadConstant;
using runtime::simir::WriteProjectedWaveform;
using runtime::simir::WriteProjected;
using runtime::simir::WriteInertial;
using runtime::simir::ReadSignal;
using runtime::simir::SignalEvent;
using runtime::simir::SignalLastValue;
using runtime::simir::SignalLastEvent;
using runtime::simir::SignalActive;
using runtime::simir::ValueKind;

void SignalOperationLowerer::lower(
    const LoadConstant& operation) {
              EncodedValue value{};
              if (operation.value.is_logic9()) {
                const auto word = operation.value.logic9_low_word();
                value = {
                    constant_i64(context, word.planes[0]),
                    constant_i64(context, word.planes[1]),
                    static_cast<std::uint32_t>(word.width),
                    constant_i64(context, word.planes[2]),
                    constant_i64(context, word.planes[3]),
                    ValueKind::logic9};
              } else {
                const auto word = operation.value.low_word();
                value = {
                    constant_i64(context, word.aval),
                    constant_i64(context, word.bval),
                    static_cast<std::uint32_t>(word.width)};
              }
              store_register(
                  builder, registers, operation.destination,
                  value);
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
                builder.CreateCall(
                    read_logic9_type,
                    read_logic9_callback,
                    {
                        context_pointer,
                        llvm::ConstantInt::get(
                            i32, operation.signal),
                        logic9_word_slot});
                auto value =
                    load_logic9_word(logic9_word_slot, width);
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
              builder.CreateStore(constant_i64(context, 0), read_bval_slot);
              auto *aval = builder.CreateCall(
                  read_type, read_callback,
                  {context_pointer, llvm::ConstantInt::get(i32, operation.signal),
                   read_bval_slot},
                  "aval");
              auto *bval =
                  builder.CreateLoad(i64, read_bval_slot, "bval.value");
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


}  // namespace fsim::compiler::llvm_detail
