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

FileOperationLowerer::FileOperationLowerer(
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
  for (unsigned index = 0; index < callbacks.size(); ++index) {
    callbacks[index] = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 56U + index),
        "file.callback." + std::to_string(index));
  }
  callback_types = {
      llvm::FunctionType::get(
          i32, {pointer, i32, i32, pointer}, false),
      llvm::FunctionType::get(
          i32, {pointer, i32, i32, i64, i64}, false),
      llvm::FunctionType::get(
          i32, {pointer, i32, i32, i64, i64}, false),
      llvm::FunctionType::get(
          i32, {pointer, i32, i32, i64, i64, pointer}, false),
      llvm::FunctionType::get(
          i32, {pointer, i32, i32, i64, i64, pointer}, false),
      llvm::FunctionType::get(
          i32, {pointer, i32, i32, i64, i64, pointer}, false),
  };
  generic_callback = builder.CreateLoad(
      pointer,
      builder.CreateStructGEP(runtime_type, runtime_argument, 62U),
      "file.generic.callback");
  generic_callback_type = llvm::FunctionType::get(
      i32,
      {pointer, i32, i32, i64, i64, i64, i64, pointer, pointer},
      false);
}

void FileOperationLowerer::check(
    llvm::Value* status, const std::string_view label) {
  runtime_error_if(
      builder.CreateICmpNE(status, id(i32, 0)),
      JitGeneratedRuntimeErrorReason::file_callback_failure,
      label);
}

void FileOperationLowerer::lower(
    const runtime::simir::FileOpen& operation) {
  auto* result = builder.CreateAlloca(i32, nullptr, "file.open.result");
  builder.CreateStore(id(i32, 0), result);
  auto* status = builder.CreateCall(
      callback_types[0],
      callbacks[0],
      {context_pointer, id(i32, process), id(i32, instruction),
       result});
  if (operation.status) {
    store_register(
        builder,
        registers,
        *operation.status,
        {builder.CreateZExt(status, i64),
         constant_i64(context, 0),
         2});
  } else {
    check(status, "file.open");
  }
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

void FileOperationLowerer::lower_handle_only(
    const runtime::simir::RegisterId handle,
    const std::size_t callback,
    const std::string_view label) {
  const auto value = load_register(builder, registers, handle);
  check(
      builder.CreateCall(
          callback_types[callback],
          callbacks[callback],
          {context_pointer, id(i32, process), id(i32, instruction),
           value.aval, value.bval}),
      label);
  branch_to_next();
}

void FileOperationLowerer::lower(
    const runtime::simir::FileClose& operation) {
  const auto value = load_register(
      builder, registers, operation.handle);
  check(
      builder.CreateCall(
          callback_types[1],
          callbacks[1],
          {context_pointer, id(i32, process), id(i32, instruction),
           value.aval, value.bval}),
      "file.close");
  if (operation.clear_handle) {
    store_register(
        builder,
        registers,
        operation.handle,
        {constant_i64(context, 0),
         constant_i64(context, 0),
         32});
  }
  branch_to_next();
}

void FileOperationLowerer::lower(
    const runtime::simir::FileWriteLiteral& operation) {
  lower_handle_only(operation.handle, 2, "file.write.literal");
}

void FileOperationLowerer::lower(
    const runtime::simir::FileWriteFormatted& operation) {
  lower_handle_only(operation.handle, 2, "file.write.formatted");
}

void FileOperationLowerer::lower(
    const runtime::simir::FileWriteString& operation) {
  lower_handle_only(operation.handle, 2, "file.write.string");
}

void FileOperationLowerer::lower_result(
    const runtime::simir::RegisterId destination,
    const runtime::simir::RegisterId handle,
    const std::size_t callback,
    const std::string_view label) {
  const auto value = load_register(builder, registers, handle);
  auto* result = builder.CreateAlloca(i32, nullptr, "file.result");
  check(
      builder.CreateCall(
          callback_types[callback],
          callbacks[callback],
          {context_pointer, id(i32, process), id(i32, instruction),
           value.aval, value.bval, result}),
      label);
  auto* loaded = builder.CreateLoad(i32, result);
  store_register(
      builder,
      registers,
      destination,
      {builder.CreateZExt(loaded, i64),
       constant_i64(context, 0),
       32});
  branch_to_next();
}

void FileOperationLowerer::lower(
    const runtime::simir::FileReadLine& operation) {
  if (operation.kind != runtime::simir::FileReadKind::line) {
    const auto handle = load_register(
        builder, registers, operation.handle);
    llvm::Value* source_aval = constant_i64(context, 0);
    llvm::Value* source_bval = constant_i64(context, 0);
    if (operation.kind == runtime::simir::FileReadKind::unget) {
      const auto source = load_register(
          builder, registers, operation.source);
      source_aval = source.aval;
      source_bval = source.bval;
    }
    auto* result_aval = builder.CreateAlloca(i64, nullptr, "file.char.aval");
    auto* result_bval = builder.CreateAlloca(i64, nullptr, "file.char.bval");
    check(
        builder.CreateCall(
            generic_callback_type, generic_callback,
            {context_pointer, id(i32, process), id(i32, instruction),
             handle.aval, handle.bval, source_aval, source_bval,
             result_aval, result_bval}),
        operation.kind == runtime::simir::FileReadKind::character
            ? "file.read.character" : "file.unread.character");
    store_register(
        builder, registers, operation.destination,
        {builder.CreateLoad(i64, result_aval),
         builder.CreateLoad(i64, result_bval), 32});
    branch_to_next();
    return;
  }
  lower_result(
      operation.destination, operation.handle, 3, "file.read.line");
}

void FileOperationLowerer::lower(
    const runtime::simir::FileEndOfFile& operation) {
  const auto value = load_register(
      builder, registers, operation.handle);
  auto* result = builder.CreateAlloca(i32, nullptr, "file.eof.result");
  check(
      builder.CreateCall(
          callback_types[4], callbacks[4],
          {context_pointer, id(i32, process), id(i32, instruction),
           value.aval, value.bval, result}),
      "file.end-of-file");
  store_register(
      builder,
      registers,
      operation.destination,
      {builder.CreateZExt(builder.CreateLoad(i32, result), i64),
       constant_i64(context, 0),
       operation.lookahead ? 1U : 32U});
  branch_to_next();
}

void FileOperationLowerer::lower(
    const runtime::simir::FileErrorStatus& operation) {
  lower_result(
      operation.destination, operation.handle, 5, "file.error");
}

void FileOperationLowerer::lower(
    const runtime::simir::FileScan& operation) {
  auto* zero = constant_i64(context, 0);
  const auto handle = operation.string_source
      ? EncodedValue{zero, zero, 32}
      : load_register(builder, registers, operation.handle);
  auto* result_aval = builder.CreateAlloca(i64, nullptr, "file.scan.aval");
  auto* result_bval = builder.CreateAlloca(i64, nullptr, "file.scan.bval");
  builder.CreateStore(zero, result_aval);
  builder.CreateStore(zero, result_bval);
  check(
      builder.CreateCall(
          generic_callback_type, generic_callback,
          {context_pointer, id(i32, process), id(i32, instruction),
           handle.aval, handle.bval, zero, zero, result_aval, result_bval}),
      operation.string_source ? "string.scan" : "file.scan");
  store_register(
      builder, registers, operation.destination,
      {builder.CreateLoad(i64, result_aval),
       builder.CreateLoad(i64, result_bval), 32});
  branch_to_next();
}

void FileOperationLowerer::lower(
    const runtime::simir::FileBinaryRead& operation) {
  const auto handle = load_register(builder, registers, operation.handle);
  auto* zero = constant_i64(context, 0);
  auto* result_aval = builder.CreateAlloca(i64, nullptr, "file.binary.aval");
  auto* result_bval = builder.CreateAlloca(i64, nullptr, "file.binary.bval");
  builder.CreateStore(zero, result_aval);
  builder.CreateStore(zero, result_bval);
  check(
      builder.CreateCall(
          generic_callback_type, generic_callback,
          {context_pointer, id(i32, process), id(i32, instruction),
           handle.aval, handle.bval, zero, zero, result_aval, result_bval}),
      "file.binary-read");
  store_register(
      builder, registers, operation.destination,
      {builder.CreateLoad(i64, result_aval),
       builder.CreateLoad(i64, result_bval), 32});
  branch_to_next();
}

void FileOperationLowerer::lower(
    const runtime::simir::FilePosition& operation) {
  auto* zero = constant_i64(context, 0);
  const auto offset = operation.kind
          == runtime::simir::FilePositionKind::seek
      ? load_register(builder, registers, operation.offset)
      : EncodedValue{zero, zero, 32};
  const auto origin = operation.kind
          == runtime::simir::FilePositionKind::seek
      ? load_register(builder, registers, operation.origin)
      : EncodedValue{zero, zero, 32};
  auto* result_aval = builder.CreateAlloca(i64, nullptr, "file.position.aval");
  auto* result_bval = builder.CreateAlloca(i64, nullptr, "file.position.bval");
  builder.CreateStore(zero, result_aval);
  builder.CreateStore(zero, result_bval);
  check(
      builder.CreateCall(
          generic_callback_type, generic_callback,
          {context_pointer, id(i32, process), id(i32, instruction),
           offset.aval, offset.bval, origin.aval, origin.bval,
           result_aval, result_bval}),
      "file.position");
  store_register(
      builder, registers, operation.destination,
      {builder.CreateLoad(i64, result_aval),
       builder.CreateLoad(i64, result_bval), 32});
  branch_to_next();
}

void FileOperationLowerer::lower(
    const runtime::simir::FileFlush&) {
  auto* zero = constant_i64(context, 0);
  auto* result_aval = builder.CreateAlloca(i64, nullptr, "file.flush.aval");
  auto* result_bval = builder.CreateAlloca(i64, nullptr, "file.flush.bval");
  builder.CreateStore(zero, result_aval);
  builder.CreateStore(zero, result_bval);
  check(
      builder.CreateCall(
          generic_callback_type, generic_callback,
          {context_pointer, id(i32, process), id(i32, instruction),
           zero, zero, zero, zero, result_aval, result_bval}),
      "file.flush");
  branch_to_next();
}

}  // namespace fsim::compiler::llvm_detail
