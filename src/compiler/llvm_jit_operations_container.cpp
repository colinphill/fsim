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
    std::vector<RegisterSlot>& frame_registers_value,
    const std::span<const runtime::simir::RegisterId> instruction_uses_value,
    llvm::LLVMContext& context_value,
    llvm::Type* i32_value,
    llvm::Type* i64_value,
    llvm::Value* context_pointer_value,
    const std::uint32_t process_value,
    const std::uint32_t instruction_value,
    llvm::StructType* services_type,
    llvm::Value* services_argument,
    const std::span<const runtime::simir::ContainerType> container_types_value,
    llvm::Value* result_aval_value,
    llvm::Value* result_bval_value,
    llvm::Value* wide_callback_scratch_value,
    const std::uint32_t wide_callback_scratch_word_stride_value,
    const std::uint32_t fused_container_object_read_distance_value,
    const bool fused_container_object_read_single_use_value,
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)>
        runtime_error_if_value,
    std::function<void()> branch_to_next_value)
    : builder(builder_value)
    , registers(registers_value)
    , frame_registers(frame_registers_value)
    , instruction_uses(instruction_uses_value)
    , context(context_value)
    , i32(i32_value)
    , i64(i64_value)
    , context_pointer(context_pointer_value)
    , process(process_value)
    , instruction(instruction_value)
    , services_type_value(services_type)
    , services_argument_value(services_argument)
    , container_types(container_types_value)
    , result_aval(result_aval_value)
    , result_bval(result_bval_value)
    , wide_callback_scratch(wide_callback_scratch_value)
    , wide_callback_scratch_word_stride(
          wide_callback_scratch_word_stride_value)
    , fused_container_object_read_distance(
          fused_container_object_read_distance_value)
    , fused_container_object_read_single_use(
          fused_container_object_read_single_use_value)
    , runtime_error_if(std::move(runtime_error_if_value))
    , branch_to_next(std::move(branch_to_next_value))
{
    auto* pointer = llvm::PointerType::getUnqual(context);
    callback = load_jit_service_callback(
        builder,
        services_type,
        services_argument,
        JitServiceField::container_operation,
        "container.callback");
    callback_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i64, i64, i64, i64, pointer, pointer },
        false);
    read_word_callback = load_jit_service_callback(
        builder,
        services_type,
        services_argument,
        JitServiceField::container_read_word,
        "container.read-word.callback");
    write_word_callback = load_jit_service_callback(
        builder,
        services_type,
        services_argument,
        JitServiceField::container_write_word,
        "container.write-word.callback");
    read_word_callback_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i32, i32, i64, i64, pointer, pointer },
        false);
    write_word_callback_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i32, i32, i64, i64, i64, i64 },
        false);
    read_packed_callback_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i32, i32, i64, i64, pointer, pointer, i32 },
        false);
    write_packed_callback_type = llvm::FunctionType::get(
        i32,
        { pointer, i32, i32, i32, i32, i64, i64, pointer, pointer, i32 },
        false);
}

void ContainerOperationLowerer::invoke(
    const std::optional<runtime::simir::RegisterId> input0,
    const std::optional<runtime::simir::RegisterId> input1,
    const std::optional<runtime::simir::RegisterId> destination,
    const std::string_view label,
    const bool reload_wide_destination_from_frame)
{
    // Container and mutable-string callbacks own their non-packed state in the
    // executor. Publish exactly the live packed operands they may inspect;
    // fast JIT processes otherwise keep those values in transient storage.
    for (const auto register_id : instruction_uses) {
        const auto& source = registers[register_id];
        const auto& frame_destination = frame_registers[register_id];
        if (source.aval_base == frame_destination.aval_base
            && source.word_offset == frame_destination.word_offset) {
            continue;
        }
        store_register(
            builder,
            frame_registers,
            register_id,
            load_register(builder, registers, register_id));
    }
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
        if (reload_wide_destination_from_frame
            && registers[*destination].width > 64U) {
            store_register(
                builder,
                registers,
                *destination,
                load_register(builder, frame_registers, *destination));
        } else {
            store_register(
                builder,
                registers,
                *destination,
                { builder.CreateLoad(i64, result_aval),
                    builder.CreateLoad(i64, result_bval),
                    registers[*destination].width });
        }
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
void ContainerOperationLowerer::lower_bound_literal(
    const runtime::simir::LoadConstant& value)
{
    // A bound literal has no packed register inputs. The ordinary indexed
    // callback resolves the value from this executor's actual process.
    invoke(std::nullopt, std::nullopt, value.destination, "bound.literal");
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
        "container.reduce", true);
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
    const auto& type = container_types[value.source];
    const bool packed_fast_path = !value.string_index && !type.associative
        && (type.element_kind
                == runtime::simir::ContainerElementKind::Packed
            || type.element_kind
                == runtime::simir::ContainerElementKind::Scalar)
        && (registers[value.index].width == 32U
            || registers[value.index].width == 64U)
        && registers[value.index].kind
            == runtime::simir::ValueKind::logic4
        && registers[value.destination].kind
            == runtime::simir::ValueKind::logic4
        && registers[value.destination].width == type.element_width;
    if (packed_fast_path) {
        const auto index = load_register(builder, registers, value.index);
        const std::uint32_t flags = (value.linear_index ? 1U : 0U)
            | ((type.fixed || value.signed_index) ? 2U : 0U)
            | (fused_container_object_read_single_use ? 4U : 0U)
            | (fused_container_object_read_distance << 8U);
        const auto& destination = registers[value.destination];
        if (type.element_width > 64U) {
            const auto words = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(type.element_width) + 63U) / 64U);
            if (wide_callback_scratch == nullptr
                || wide_callback_scratch_word_stride < words) {
                throw LlvmJitUnsupportedError(
                    "wide container read has no callback scratch storage");
            }
            auto* aval = builder.CreateGEP(
                i64, destination.aval_base,
                constant_i64(context, destination.word_offset),
                "container.read-packed.aval");
            auto* bval = builder.CreateGEP(
                i64, destination.bval_base,
                constant_i64(context, destination.word_offset),
                "container.read-packed.bval");
            auto* scratch_aval = builder.CreateInBoundsGEP(
                i64,
                wide_callback_scratch,
                constant_i64(context, 0U),
                "container.read-packed.scratch.aval");
            auto* scratch_bval = builder.CreateInBoundsGEP(
                i64,
                wide_callback_scratch,
                constant_i64(
                    context, wide_callback_scratch_word_stride),
                "container.read-packed.scratch.bval");
            const auto byte_count
                = static_cast<std::uint64_t>(words) * sizeof(std::uint64_t);
            builder.CreateMemCpy(
                scratch_aval, llvm::Align(8), aval, llvm::Align(8), byte_count);
            builder.CreateMemCpy(
                scratch_bval, llvm::Align(8), bval, llvm::Align(8), byte_count);
            auto* status = builder.CreateCall(
                read_packed_callback_type,
                load_jit_service_callback(
                    builder,
                    services_type_value,
                    services_argument_value,
                    registers[value.index].width == 64U
                        ? JitServiceField::container_read_packed_index64
                        : JitServiceField::container_read_packed,
                    "container.read-packed.callback"),
                { context_pointer,
                    id(i32, process),
                    id(i32, instruction),
                    id(i32, value.source),
                    id(i32, flags),
                    index.aval,
                    index.bval,
                    scratch_aval,
                    scratch_bval,
                    id(i32, words) });
            // Preserve partial callback writes even when the callback reports
            // failure; the generated error edge used to leave those writes in
            // the destination register planes.
            builder.CreateMemCpy(
                aval, llvm::Align(8), scratch_aval, llvm::Align(8), byte_count);
            builder.CreateMemCpy(
                bval, llvm::Align(8), scratch_bval, llvm::Align(8), byte_count);
            runtime_error_if(
                builder.CreateICmpNE(status, id(i32, 0)),
                JitGeneratedRuntimeErrorReason::container_callback_failure,
                "container.read-packed");
            if (destination.initialized_base != nullptr) {
                builder.CreateStore(
                    llvm::ConstantInt::get(
                        llvm::Type::getInt8Ty(context), 1U),
                    builder.CreateGEP(
                        llvm::Type::getInt8Ty(context),
                        destination.initialized_base,
                        id(i32, destination.index)));
            }
            branch_to_next();
            return;
        }
        auto* zero = constant_i64(context, 0);
        builder.CreateStore(zero, result_aval);
        builder.CreateStore(zero, result_bval);
        llvm::Value* status { };
        if (registers[value.index].width == 64U) {
            auto* packed_index64_callback = load_jit_service_callback(
                builder,
                services_type_value,
                services_argument_value,
                JitServiceField::container_read_packed_index64,
                "container.read-packed-index64.callback");
            status = builder.CreateCall(
                read_packed_callback_type,
                packed_index64_callback,
                { context_pointer,
                    id(i32, process),
                    id(i32, instruction),
                    id(i32, value.source),
                    id(i32, flags),
                    index.aval,
                    index.bval,
                    result_aval,
                    result_bval,
                    id(i32, 1U) });
        } else {
            status = builder.CreateCall(
                read_word_callback_type,
                read_word_callback,
                { context_pointer,
                    id(i32, process),
                    id(i32, instruction),
                    id(i32, value.source),
                    id(i32, flags),
                    index.aval,
                    index.bval,
                    result_aval,
                    result_bval });
        }
        runtime_error_if(
            builder.CreateICmpNE(status, id(i32, 0)),
            JitGeneratedRuntimeErrorReason::container_callback_failure,
            "container.read-word");
        store_register(
            builder,
            registers,
            value.destination,
            { builder.CreateLoad(i64, result_aval),
                builder.CreateLoad(i64, result_bval),
                registers[value.destination].width });
        branch_to_next();
        return;
    }
    invoke(
        value.string_index ? std::nullopt : std::optional { value.index },
        std::nullopt, value.destination, "container.read", true);
}
void ContainerOperationLowerer::lower(
    const runtime::simir::ContainerWrite& value)
{
    const auto& type = container_types[value.target];
    const bool packed_fast_path = !value.string_index && !type.associative
        && (type.element_kind
                == runtime::simir::ContainerElementKind::Packed
            || type.element_kind
                == runtime::simir::ContainerElementKind::Scalar)
        && registers[value.index].width == 32U
        && registers[value.index].kind
            == runtime::simir::ValueKind::logic4
        && registers[value.source].kind
            == runtime::simir::ValueKind::logic4
        && registers[value.source].width == type.element_width;
    if (packed_fast_path) {
        const auto index = load_register(builder, registers, value.index);
        const std::uint32_t flags = (value.linear_index ? 1U : 0U)
            | ((type.fixed || value.signed_index) ? 2U : 0U);
        const auto& source_slot = registers[value.source];
        if (type.element_width > 64U) {
            const auto words = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(type.element_width) + 63U) / 64U);
            if (wide_callback_scratch == nullptr
                || wide_callback_scratch_word_stride < words) {
                throw LlvmJitUnsupportedError(
                    "wide container write has no callback scratch storage");
            }
            auto* aval = builder.CreateGEP(
                i64, source_slot.aval_base,
                constant_i64(context, source_slot.word_offset),
                "container.write-packed.aval");
            auto* bval = builder.CreateGEP(
                i64, source_slot.bval_base,
                constant_i64(context, source_slot.word_offset),
                "container.write-packed.bval");
            auto* scratch_aval = builder.CreateInBoundsGEP(
                i64,
                wide_callback_scratch,
                constant_i64(context, 0U),
                "container.write-packed.scratch.aval");
            auto* scratch_bval = builder.CreateInBoundsGEP(
                i64,
                wide_callback_scratch,
                constant_i64(
                    context, wide_callback_scratch_word_stride),
                "container.write-packed.scratch.bval");
            const auto byte_count
                = static_cast<std::uint64_t>(words) * sizeof(std::uint64_t);
            builder.CreateMemCpy(
                scratch_aval, llvm::Align(8), aval, llvm::Align(8), byte_count);
            builder.CreateMemCpy(
                scratch_bval, llvm::Align(8), bval, llvm::Align(8), byte_count);
            auto* status = builder.CreateCall(
                write_packed_callback_type,
                load_jit_service_callback(
                    builder,
                    services_type_value,
                    services_argument_value,
                    JitServiceField::container_write_packed,
                    "container.write-packed.callback"),
                { context_pointer,
                    id(i32, process),
                    id(i32, instruction),
                    id(i32, value.target),
                    id(i32, flags),
                    index.aval,
                    index.bval,
                    scratch_aval,
                    scratch_bval,
                    id(i32, words) });
            runtime_error_if(
                builder.CreateICmpNE(status, id(i32, 0)),
                JitGeneratedRuntimeErrorReason::container_callback_failure,
                "container.write-packed");
            branch_to_next();
            return;
        }
        const auto source = load_register(builder, registers, value.source);
        auto* status = builder.CreateCall(
            write_word_callback_type,
            write_word_callback,
            { context_pointer,
                id(i32, process),
                id(i32, instruction),
                id(i32, value.target),
                id(i32, flags),
                index.aval,
                index.bval,
                source.aval,
                source.bval });
        runtime_error_if(
            builder.CreateICmpNE(status, id(i32, 0)),
            JitGeneratedRuntimeErrorReason::container_callback_failure,
            "container.write-word");
        branch_to_next();
        return;
    }
    invoke(
        value.string_index ? std::nullopt : std::optional { value.index },
        value.source, std::nullopt, "container.write");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::WriteContainerObjectElement& value)
{
    invoke(
        value.index,
        value.source,
        std::nullopt,
        "container.object-element-write");
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
    const runtime::simir::AppendContainer& value)
{
    invoke(value.value, std::nullopt, std::nullopt,
        "container.append");
}
void ContainerOperationLowerer::lower(
    const runtime::simir::FormatContainerPattern&)
{
    invoke(std::nullopt, std::nullopt, std::nullopt,
        "container.format-pattern");
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
        "container.aggregate-read", true);
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
    if (registers[value.index].width > 64U) {
        for (const auto register_id : instruction_uses) {
            const auto& source = registers[register_id];
            const auto& frame_destination = frame_registers[register_id];
            if (source.aval_base == frame_destination.aval_base
                && source.word_offset == frame_destination.word_offset) {
                continue;
            }
            store_register(
                builder,
                frame_registers,
                register_id,
                load_register(builder, registers, register_id));
        }
        builder.CreateStore(zero, result_aval);
        builder.CreateStore(zero, result_bval);
        auto* status = builder.CreateCall(
            callback_type,
            callback,
            { context_pointer,
                id(i32, process),
                id(i32, instruction),
                builder.CreateTrunc(input.aval, i64),
                builder.CreateTrunc(input.bval, i64),
                constant_i64(context, 1),
                zero,
                result_aval,
                result_bval });
        runtime_error_if(
            builder.CreateICmpNE(status, id(i32, 0)),
            JitGeneratedRuntimeErrorReason::container_callback_failure,
            "container.traverse-wide");
        store_register(
            builder,
            registers,
            value.index,
            load_register(builder, frame_registers, value.index));
        store_register(
            builder,
            registers,
            value.destination,
            { builder.CreateLoad(i64, result_aval),
                builder.CreateLoad(i64, result_bval),
                registers[value.destination].width });
        branch_to_next();
        return;
    }
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

    builder.CreateStore(zero, result_aval);
    builder.CreateStore(zero, result_bval);
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
            result_aval,
            result_bval });
    runtime_error_if(
        builder.CreateICmpNE(status, id(i32, 0)),
        JitGeneratedRuntimeErrorReason::container_callback_failure,
        "container.traverse-status");
    store_register(
        builder,
        registers,
        value.destination,
        { builder.CreateLoad(i64, result_aval),
            builder.CreateLoad(i64, result_bval),
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
    invoke(
        std::nullopt, std::nullopt, value.destination,
        "container.pop", true);
}

} // namespace fsim::compiler::llvm_detail
