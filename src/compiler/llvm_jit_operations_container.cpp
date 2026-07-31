// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>

namespace fsim::compiler::llvm_detail {

namespace {
[[nodiscard]] llvm::Constant* id(
    llvm::Type* type, const std::uint32_t value) {
  return llvm::ConstantInt::get(type, value);
}
}  // namespace

ContainerOperationLowerer::ContainerOperationLowerer(
    llvm::IRBuilder<>& builder_value,
    std::vector<RegisterSlot>& registers_value,
    llvm::LLVMContext& context_value,
    llvm::Type* i32_value,
    llvm::Type* i64_value,
    llvm::Value* context_pointer_value,
    const std::uint32_t process_value,
    const std::uint32_t instruction_value,
    llvm::StructType* runtime_type,
    llvm::Value* runtime_argument,
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)> runtime_error_if_value,
    std::function<void()> branch_to_next_value)
    : builder(builder_value),
      registers(registers_value),
      context(context_value),
      i32(i32_value),
      i64(i64_value),
      context_pointer(context_pointer_value),
      process(process_value),
      instruction(instruction_value),
      runtime_error_if(std::move(runtime_error_if_value)),
      branch_to_next(std::move(branch_to_next_value)) {
  auto* pointer = llvm::PointerType::getUnqual(context);
  callback = builder.CreateLoad(
      pointer,
      builder.CreateStructGEP(
          runtime_type, runtime_argument, 62U),
      "container.callback");
  callback_type = llvm::FunctionType::get(
      i32,
      {pointer, i32, i32, i64, i64, i64, i64, pointer, pointer},
      false);
}

void ContainerOperationLowerer::invoke(
    const std::optional<runtime::simir::RegisterId> input0,
    const std::optional<runtime::simir::RegisterId> input1,
    const std::optional<runtime::simir::RegisterId> destination,
    const std::string_view label) {
  auto* zero = constant_i64(context, 0);
  const auto first = input0
      ? load_register(builder, registers, *input0)
      : EncodedValue{zero, zero, 1};
  const auto second = input1
      ? load_register(builder, registers, *input1)
      : EncodedValue{zero, zero, 1};
  auto* result_aval =
      builder.CreateAlloca(i64, nullptr, "container.result.aval");
  auto* result_bval =
      builder.CreateAlloca(i64, nullptr, "container.result.bval");
  builder.CreateStore(zero, result_aval);
  builder.CreateStore(zero, result_bval);
  auto* status = builder.CreateCall(
      callback_type,
      callback,
      {context_pointer,
       id(i32, process),
       id(i32, instruction),
       first.aval,
       first.bval,
       second.aval,
       second.bval,
       result_aval,
       result_bval});
  runtime_error_if(
      builder.CreateICmpNE(status, id(i32, 0)),
      JitGeneratedRuntimeErrorReason::container_callback_failure,
      label);
  if (destination) {
    store_register(
        builder,
        registers,
        *destination,
        {builder.CreateLoad(i64, result_aval),
         builder.CreateLoad(i64, result_bval),
         registers[*destination].width});
  }
  branch_to_next();
}

void ContainerOperationLowerer::lower(
    const runtime::simir::ResizeContainer& value) {
  invoke(value.size, std::nullopt, std::nullopt, "container.resize");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::CopyContainerRegister&) {
  invoke(std::nullopt, std::nullopt, std::nullopt, "container.copy");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ConditionalContainerSelect& value) {
  invoke(
      value.condition, std::nullopt, std::nullopt,
      "container.conditional");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ReadContainerObject&) {
  invoke(std::nullopt, std::nullopt, std::nullopt, "container.read-object");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::WriteContainerObject&) {
  invoke(std::nullopt, std::nullopt, std::nullopt, "container.write-object");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerSize& value) {
  invoke(std::nullopt, std::nullopt, value.destination, "container.size");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerReduction& value) {
  invoke(
      std::nullopt, std::nullopt, value.destination,
      "container.reduce");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::OrderContainer&) {
  invoke(
      std::nullopt, std::nullopt, std::nullopt,
      "container.order");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::LocateContainer&) {
  invoke(
      std::nullopt, std::nullopt, std::nullopt,
      "container.locate");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerRead& value) {
  invoke(value.index, std::nullopt, value.destination, "container.read");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerWrite& value) {
  invoke(value.index, value.source, std::nullopt, "container.write");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::DeleteContainer& value) {
  invoke(value.index, std::nullopt, std::nullopt, "container.delete");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerExists& value) {
  invoke(value.index, std::nullopt, value.destination, "container.exists");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::TraverseContainer& value) {
  const auto input =
      load_register(builder, registers, value.index);
  auto* zero = constant_i64(context, 0);
  auto* result_aval =
      builder.CreateAlloca(i64, nullptr, "container.key.aval");
  auto* result_bval =
      builder.CreateAlloca(i64, nullptr, "container.key.bval");
  builder.CreateStore(zero, result_aval);
  builder.CreateStore(zero, result_bval);
  auto* key_status = builder.CreateCall(
      callback_type,
      callback,
      {context_pointer,
       id(i32, process),
       id(i32, instruction),
       input.aval,
       input.bval,
       zero,
       zero,
       result_aval,
       result_bval});
  runtime_error_if(
      builder.CreateICmpNE(key_status, id(i32, 0)),
      JitGeneratedRuntimeErrorReason::container_callback_failure,
      "container.traverse-key");
  store_register(
      builder,
      registers,
      value.index,
      {builder.CreateLoad(i64, result_aval),
       builder.CreateLoad(i64, result_bval),
       registers[value.index].width});

  auto* status_aval =
      builder.CreateAlloca(i64, nullptr, "container.status.aval");
  auto* status_bval =
      builder.CreateAlloca(i64, nullptr, "container.status.bval");
  builder.CreateStore(zero, status_aval);
  builder.CreateStore(zero, status_bval);
  auto* status = builder.CreateCall(
      callback_type,
      callback,
      {context_pointer,
       id(i32, process),
       id(i32, instruction),
       input.aval,
       input.bval,
       constant_i64(context, 1),
       zero,
       status_aval,
       status_bval});
  runtime_error_if(
      builder.CreateICmpNE(status, id(i32, 0)),
      JitGeneratedRuntimeErrorReason::container_callback_failure,
      "container.traverse-status");
  store_register(
      builder,
      registers,
      value.destination,
      {builder.CreateLoad(i64, status_aval),
       builder.CreateLoad(i64, status_bval),
       registers[value.destination].width});
  branch_to_next();
}
void ContainerOperationLowerer::lower(
    const runtime::simir::LoadMemory& value) {
  invoke(
      value.start, value.finish, std::nullopt,
      value.hexadecimal ? "container.readmemh"
                        : "container.readmemb");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::PushContainer& value) {
  invoke(value.source, std::nullopt, std::nullopt, "container.push");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::PopContainer& value) {
  invoke(std::nullopt, std::nullopt, value.destination, "container.pop");
}

}  // namespace fsim::compiler::llvm_detail
