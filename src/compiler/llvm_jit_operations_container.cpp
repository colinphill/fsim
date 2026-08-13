// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/Constants.h>

namespace fsim::compiler::llvm_detail {

namespace {
    [[nodiscard]] llvm::Constant* id(
        llvm::Type* type, const std::uint32_t value)
    {
        return llvm::ConstantInt::get(type, value);
    }
} // namespace

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
        std::string_view)>
        runtime_error_if_value,
    std::function<void()> branch_to_next_value)
    : builder(builder_value)
    , registers(registers_value)
    , context(context_value)
    , i32(i32_value)
    , i64(i64_value)
    , context_pointer(context_pointer_value)
    , process(process_value)
    , instruction(instruction_value)
    , runtime_error_if(std::move(runtime_error_if_value))
    , branch_to_next(std::move(branch_to_next_value))
{
    auto* pointer = llvm::PointerType::getUnqual(context);
    callback = builder.CreateLoad(
        pointer,
        builder.CreateStructGEP(
            runtime_type, runtime_argument, 62U),
        "container.callback");
    callback_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i64, i64, i64, i64, pointer, pointer },
        false);
}

void ContainerOperationLowerer::invoke(
    const std::optional<runtime::simir::RegisterId> input0,
    const std::optional<runtime::simir::RegisterId> input1,
    const std::optional<runtime::simir::RegisterId> destination,
    const std::string_view label)
{
    auto* zero = constant_i64(context, 0);
    const auto first = input0
        ? load_register(builder, registers, *input0)
        : EncodedValue { zero, zero, 1 };
    const auto second = input1
        ? load_register(builder, registers, *input1)
        : EncodedValue { zero, zero, 1 };
    const auto callback_word = [&](llvm::Value* value) {
        const auto* type = llvm::cast<llvm::IntegerType>(value->getType());
        return type->getBitWidth() > 64
            ? builder.CreateTrunc(value, i64)
            : value;
    };
    auto* result_aval = builder.CreateAlloca(i64, nullptr, "container.result.aval");
    auto* result_bval = builder.CreateAlloca(i64, nullptr, "container.result.bval");
    builder.CreateStore(zero, result_aval);
    builder.CreateStore(zero, result_bval);
    auto* status = builder.CreateCall(
        callback_type,
        callback,
        { context_pointer,
            id(i32, process),
            id(i32, instruction),
            callback_word(first.aval),
            callback_word(first.bval),
            callback_word(second.aval),
            callback_word(second.bval),
            result_aval,
            result_bval });
    runtime_error_if(
        builder.CreateICmpNE(status, id(i32, 0)),
        JitGeneratedRuntimeErrorReason::container_callback_failure,
        label);
    if (destination) {
        store_register(
            builder,
            registers,
            *destination,
            { builder.CreateLoad(i64, result_aval),
                builder.CreateLoad(i64, result_bval),
                registers[*destination].width });
    }
    branch_to_next();
}

void ContainerOperationLowerer::lower(
    const runtime::simir::ResizeContainer& value)
{
    invoke(value.size, std::nullopt, std::nullopt, "container.resize");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::SystemVerilogScalarBinary& value)
{
    invoke(value.lhs, value.rhs, value.destination, "scalar.binary");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::SystemVerilogMath& value)
{
    invoke(
        std::nullopt, std::nullopt, value.destination,
        "systemverilog.math");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::CopyContainerRegister&)
{
    invoke(std::nullopt, std::nullopt, std::nullopt, "container.copy");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ConditionalContainerSelect& value)
{
    invoke(
        value.condition, std::nullopt, std::nullopt,
        "container.conditional");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::CompareContainers& value)
{
    invoke(
        std::nullopt, std::nullopt, value.destination,
        "container.compare");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ReadContainerObject&)
{
    invoke(std::nullopt, std::nullopt, std::nullopt, "container.read-object");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::WriteContainerObject&)
{
    invoke(std::nullopt, std::nullopt, std::nullopt, "container.write-object");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerSize& value)
{
    invoke(std::nullopt, std::nullopt, value.destination, "container.size");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerReduction& value)
{
    invoke(
        std::nullopt, std::nullopt, value.destination,
        "container.reduce");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::OrderContainer&)
{
    invoke(
        std::nullopt, std::nullopt, std::nullopt,
        "container.order");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::LocateContainer&)
{
    invoke(
        std::nullopt, std::nullopt, std::nullopt,
        "container.locate");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerRead& value)
{
    invoke(
        value.string_index ? std::nullopt : std::optional { value.index },
        std::nullopt,
        registers[value.destination].width > 64
            ? std::nullopt
            : std::optional { value.destination },
        "container.read");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerWrite& value)
{
    invoke(
        value.string_index ? std::nullopt : std::optional { value.index },
        value.source, std::nullopt, "container.write");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerStringRead& value)
{
    invoke(
        value.string_index ? std::nullopt : std::optional { value.index },
        std::nullopt, std::nullopt,
        "container.string-read");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerStringWrite& value)
{
    invoke(
        value.string_index ? std::nullopt : std::optional { value.index },
        std::nullopt, std::nullopt,
        "container.string-write");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerElementRead& value)
{
    invoke(
        value.index, std::nullopt, std::nullopt,
        "container.element-read");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerElementWrite& value)
{
    invoke(
        value.index, std::nullopt, std::nullopt,
        "container.element-write");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerAggregateRead& value)
{
    invoke(
        value.index, std::nullopt, value.destination,
        "container.aggregate-read");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerAggregateWrite& value)
{
    invoke(
        value.index, value.source, std::nullopt,
        "container.aggregate-write");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::CopyContainerAggregateElement& value)
{
    invoke(
        value.target_index, value.source_index, std::nullopt,
        "container.aggregate-copy");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::StringMethod& value)
{
    using runtime::simir::StringMethodOperator;
    const auto first = value.operation == StringMethodOperator::getc
            || value.operation == StringMethodOperator::putc
            || value.operation == StringMethodOperator::substr
            || (value.operation >= StringMethodOperator::itoa
                && value.operation <= StringMethodOperator::realtoa)
        ? std::optional { value.first }
        : std::nullopt;
    const auto second = value.operation == StringMethodOperator::putc
            || value.operation == StringMethodOperator::substr
        ? std::optional { value.second }
        : std::nullopt;
    const auto destination = value.operation == StringMethodOperator::getc
            || value.operation == StringMethodOperator::atoreal
            || value.operation == StringMethodOperator::compare
            || value.operation == StringMethodOperator::icompare
            || (value.operation >= StringMethodOperator::atoi
                && value.operation <= StringMethodOperator::atobin)
        ? std::optional { value.destination }
        : std::nullopt;
    invoke(first, second, destination, "string.method");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::DeleteContainer& value)
{
    invoke(
        value.string_index ? std::nullopt : value.index,
        std::nullopt, std::nullopt, "container.delete");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerExists& value)
{
    invoke(
        value.string_index ? std::nullopt : std::optional { value.index },
        std::nullopt, value.destination, "container.exists");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::TraverseContainer& value)
{
    if (value.string_index) {
        invoke(
            std::nullopt, std::nullopt, value.destination,
            "container.traverse-string");
        return;
    }
    const auto input = load_register(builder, registers, value.index);
    auto* zero = constant_i64(context, 0);
    auto* result_aval = builder.CreateAlloca(i64, nullptr, "container.key.aval");
    auto* result_bval = builder.CreateAlloca(i64, nullptr, "container.key.bval");
    builder.CreateStore(zero, result_aval);
    builder.CreateStore(zero, result_bval);
    auto* key_status = builder.CreateCall(
        callback_type,
        callback,
        { context_pointer,
            id(i32, process),
            id(i32, instruction),
            input.aval,
            input.bval,
            zero,
            zero,
            result_aval,
            result_bval });
    runtime_error_if(
        builder.CreateICmpNE(key_status, id(i32, 0)),
        JitGeneratedRuntimeErrorReason::container_callback_failure,
        "container.traverse-key");
    store_register(
        builder,
        registers,
        value.index,
        { builder.CreateLoad(i64, result_aval),
            builder.CreateLoad(i64, result_bval),
            registers[value.index].width });

    auto* status_aval = builder.CreateAlloca(i64, nullptr, "container.status.aval");
    auto* status_bval = builder.CreateAlloca(i64, nullptr, "container.status.bval");
    builder.CreateStore(zero, status_aval);
    builder.CreateStore(zero, status_bval);
    auto* status = builder.CreateCall(
        callback_type,
        callback,
        { context_pointer,
            id(i32, process),
            id(i32, instruction),
            input.aval,
            input.bval,
            constant_i64(context, 1),
            zero,
            status_aval,
            status_bval });
    runtime_error_if(
        builder.CreateICmpNE(status, id(i32, 0)),
        JitGeneratedRuntimeErrorReason::container_callback_failure,
        "container.traverse-status");
    store_register(
        builder,
        registers,
        value.destination,
        { builder.CreateLoad(i64, status_aval),
            builder.CreateLoad(i64, status_bval),
            registers[value.destination].width });
    branch_to_next();
}
void ContainerOperationLowerer::lower(
    const runtime::simir::LoadMemory& value)
{
    invoke(
        value.start, value.finish, std::nullopt,
        value.write
            ? value.hexadecimal ? "container.writememh"
                                : "container.writememb"
            : value.hexadecimal ? "container.readmemh"
                                : "container.readmemb");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::VitalMemoryDeclare& value)
{
    invoke(
        std::nullopt, std::nullopt, value.destination,
        "vital.memory-declare");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::PushContainer& value)
{
    invoke(
        value.source, value.index, std::nullopt,
        value.index ? "container.insert" : "container.push");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::PopContainer& value)
{
    invoke(std::nullopt, std::nullopt, value.destination, "container.pop");
}

} // namespace fsim::compiler::llvm_detail
