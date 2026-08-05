// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Module.h>

namespace fsim::compiler::llvm_detail {

namespace {

[[nodiscard]] llvm::Constant* id(
    llvm::Type* type, const std::uint32_t value) {
  return llvm::ConstantInt::get(type, value);
}

}  // namespace

StringOperationLowerer::StringOperationLowerer(
    llvm::Module& module_value,
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
    : module(module_value),
      builder(builder_value),
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
  for (unsigned index = 0; index < callbacks.size(); ++index) {
    callbacks[index] = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 46U + index),
        "string.callback." + std::to_string(index));
  }
  callback_types = {
      llvm::FunctionType::get(
          i32, {pointer, i32, pointer, i64}, false),
      llvm::FunctionType::get(i32, {pointer, i32, i32}, false),
      llvm::FunctionType::get(i32, {pointer, i32, i32}, false),
      llvm::FunctionType::get(i32, {pointer, i32, i32}, false),
      llvm::FunctionType::get(
          i32, {pointer, i32, i32, i32, pointer, i32}, false),
      llvm::FunctionType::get(
          i32, {pointer, i32, i32, i32, pointer}, false),
      llvm::FunctionType::get(i32, {pointer, i32, pointer}, false),
      llvm::FunctionType::get(
          i32,
          {pointer, i32, i32, i32, i64, i64, i32, pointer},
          false),
      llvm::FunctionType::get(
          i32,
          {pointer, i32, i32, i32, i64, i64, i32, i64, i64},
          false),
      llvm::FunctionType::get(
          i32,
          {pointer, i32, i32, pointer, i64, pointer, i64, i32, i32},
          false),
  };
}

void StringOperationLowerer::check(
    llvm::Value* status, const std::string_view label) {
  runtime_error_if(
      builder.CreateICmpNE(status, id(i32, 0)),
      JitGeneratedRuntimeErrorReason::string_callback_failure,
      label);
}

void StringOperationLowerer::lower(
    const runtime::simir::LoadStringConstant& operation) {
  auto* bytes = builder.CreateGlobalString(
      operation.value, "string.literal", 0, &module);
  check(
      builder.CreateCall(
          callback_types[0],
          callbacks[0],
          {context_pointer,
           id(i32, operation.destination),
           bytes,
           constant_i64(context, operation.value.size())}),
      "string.load");
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::CopyStringRegister& operation) {
  check(
      builder.CreateCall(
          callback_types[1],
          callbacks[1],
          {context_pointer,
           id(i32, operation.destination),
           id(i32, operation.source)}),
      "string.copy");
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::ReadStringObject& operation) {
  check(
      builder.CreateCall(
          callback_types[2],
          callbacks[2],
          {context_pointer,
           id(i32, operation.destination),
           id(i32, operation.object)}),
      "string.object.read");
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::WriteStringObject& operation) {
  check(
      builder.CreateCall(
          callback_types[3],
          callbacks[3],
          {context_pointer,
           id(i32, operation.object),
           id(i32, operation.source)}),
      "string.object.write");
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::ConcatenateStrings& operation) {
  llvm::Value* operands = llvm::ConstantPointerNull::get(
      llvm::PointerType::getUnqual(context));
  if (!operation.operands.empty()) {
    std::vector<std::uint32_t> values{
        operation.operands.begin(), operation.operands.end()};
    auto* initializer = llvm::ConstantDataArray::get(context, values);
    auto* global = new llvm::GlobalVariable(
        module,
        initializer->getType(),
        true,
        llvm::GlobalValue::PrivateLinkage,
        initializer,
        "string.operands");
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    operands = global;
  }
  check(
      builder.CreateCall(
          callback_types[4],
          callbacks[4],
          {context_pointer,
           id(i32, process),
           id(i32, instruction),
           id(i32, operation.destination),
           operands,
           id(i32, static_cast<std::uint32_t>(
                       operation.operands.size()))}),
      "string.concatenate");
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::CompareStrings& operation) {
  auto* result = builder.CreateAlloca(i32, nullptr, "string.compare.result");
  check(
      builder.CreateCall(
          callback_types[5],
          callbacks[5],
          {context_pointer,
           id(i32, operation.lhs),
           id(i32, operation.rhs),
           id(i32, operation.not_equal ? 1U : 0U),
           result}),
      "string.compare");
  auto* value = builder.CreateLoad(i32, result);
  store_register(
      builder,
      registers,
      operation.destination,
      {builder.CreateZExt(value, i64),
       constant_i64(context, 0),
       1});
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::StringLength& operation) {
  auto* result = builder.CreateAlloca(i32, nullptr, "string.length.result");
  check(
      builder.CreateCall(
          callback_types[6],
          callbacks[6],
          {context_pointer, id(i32, operation.source), result}),
      "string.length");
  auto* value = builder.CreateLoad(i32, result);
  store_register(
      builder,
      registers,
      operation.destination,
      {builder.CreateZExt(value, i64),
       constant_i64(context, 0),
       32});
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::StringIndex& operation) {
  const auto index = load_register(builder, registers, operation.index);
  auto* result = builder.CreateAlloca(i32, nullptr, "string.index.result");
  check(
      builder.CreateCall(
          callback_types[7],
          callbacks[7],
          {context_pointer,
           id(i32, process),
           id(i32, instruction),
           id(i32, operation.source),
           index.aval,
           index.bval,
           id(i32, operation.signed_index ? 1U : 0U),
           result}),
      "string.index");
  auto* value = builder.CreateLoad(i32, result);
  store_register(
      builder,
      registers,
      operation.destination,
      {builder.CreateZExt(value, i64),
       constant_i64(context, 0),
       32});
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::StringReplaceCodePoint& operation) {
  const auto index = load_register(builder, registers, operation.index);
  const auto source = load_register(builder, registers, operation.source);
  check(
      builder.CreateCall(
          callback_types[8],
          callbacks[8],
          {context_pointer,
           id(i32, process),
           id(i32, instruction),
           id(i32, operation.target),
           index.aval,
           index.bval,
           id(i32, operation.signed_index ? 1U : 0U),
           source.aval,
           source.bval}),
      "string.replace");
  branch_to_next();
}

void StringOperationLowerer::lower(
    const runtime::simir::StringDisplay& operation) {
  auto* prefix = builder.CreateGlobalString(
      operation.prefix, "string.output.prefix", 0, &module);
  auto* suffix = builder.CreateGlobalString(
      operation.suffix, "string.output.suffix", 0, &module);
  check(
      builder.CreateCall(
          callback_types[9],
          callbacks[9],
          {context_pointer,
           id(i32, process),
           id(i32, operation.source),
           prefix,
           constant_i64(context, operation.prefix.size()),
           suffix,
           constant_i64(context, operation.suffix.size()),
           id(i32, operation.newline ? 1U : 0U),
           id(i32, operation.postponed ? 1U : 0U)}),
      "string.output");
  branch_to_next();
}

}  // namespace fsim::compiler::llvm_detail
