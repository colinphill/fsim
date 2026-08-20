// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>

#include <cstdint>
#include <string>

namespace fsim::compiler::llvm_detail {

using runtime::simir::Branch;
using runtime::simir::Call;
using runtime::simir::CallableFramePop;
using runtime::simir::CallableFramePush;
using runtime::simir::Jump;
using runtime::simir::Return;
using runtime::simir::UnknownBranchPolicy;

void ControlFlowOperationLowerer::lower(const Jump& operation)
{
    builder.CreateBr(instruction_blocks[operation.target]);
}

void ControlFlowOperationLowerer::lower(const Call& operation)
{
    if (native_call) {
        if (ssa_callable_return != nullptr) {
            builder.CreateStore(
                llvm::ConstantInt::get(
                    llvm::cast<llvm::IntegerType>(i32),
                    operation.return_target),
                ssa_callable_return);
            builder.CreateBr(instruction_blocks[operation.target]);
            return;
        }
        auto* depth_pointer
            = builder.CreateStructGEP(frame_type, frame_argument, 13);
        auto* depth = builder.CreateLoad(i32, depth_pointer, "call.depth");
        runtime_error_if(
            builder.CreateICmpUGE(
                depth,
                llvm::ConstantInt::get(
                    llvm::cast<llvm::IntegerType>(i32),
                    FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V1)),
            JitGeneratedRuntimeErrorReason::call_stack_overflow,
            "native.call.stack.overflow");
        auto* stack = builder.CreateStructGEP(
            frame_type, frame_argument, 15);
        auto* target_pointer = builder.CreateInBoundsGEP(
            llvm::ArrayType::get(
                i32, FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V1),
            stack,
            { llvm::ConstantInt::get(
                  llvm::cast<llvm::IntegerType>(i32), 0),
                depth });
        builder.CreateStore(
            llvm::ConstantInt::get(
                llvm::cast<llvm::IntegerType>(i32),
                operation.return_target),
            target_pointer);
        builder.CreateStore(
            builder.CreateAdd(
                depth,
                llvm::ConstantInt::get(
                    llvm::cast<llvm::IntegerType>(i32), 1)),
            depth_pointer);
        builder.CreateBr(instruction_blocks[operation.target]);
        return;
    }
    if (operation.stack.capacity == 0) {
        return_result(
            FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
            instruction,
            0,
            FSIM_JIT_FRAME_STATE_READY,
            instruction);
        return;
    }
    const auto pointer_value = load_register(builder, registers, operation.stack.pointer);
    runtime_error_if(
        builder.CreateICmpNE(
            pointer_value.bval, constant_i64(context, 0)),
        JitGeneratedRuntimeErrorReason::call_stack_unknown,
        "call.stack.unknown");
    runtime_error_if(
        builder.CreateICmpUGE(
            pointer_value.aval,
            constant_i64(context, operation.stack.capacity)),
        JitGeneratedRuntimeErrorReason::call_stack_overflow,
        "call.stack.overflow");
    auto* entry_index = builder.CreateAdd(
        pointer_value.aval,
        constant_i64(context, operation.stack.entries));
    builder.CreateStore(
        constant_i64(context, operation.return_target),
        builder.CreateGEP(i64, register_aval, entry_index));
    builder.CreateStore(
        constant_i64(context, 0),
        builder.CreateGEP(i64, register_bval, entry_index));
    builder.CreateStore(
        llvm::ConstantInt::get(i8, 1),
        builder.CreateGEP(i8, register_initialized, entry_index));
    store_register(
        builder,
        registers,
        operation.stack.pointer,
        EncodedValue {
            builder.CreateAdd(
                pointer_value.aval, constant_i64(context, 1)),
            constant_i64(context, 0),
            32 });
    builder.CreateBr(instruction_blocks[operation.target]);
}

void ControlFlowOperationLowerer::lower(const Return& operation)
{
    if (native_return) {
        if (ssa_callable_return != nullptr) {
            auto* target = builder.CreateLoad(
                i32,
                ssa_callable_return,
                "native.return.target");
            auto* dispatch_return = builder.CreateSwitch(
                target,
                invalid_pc,
                static_cast<unsigned>(native_return_targets.size()));
            for (const auto return_target : native_return_targets) {
                dispatch_return->addCase(
                    llvm::ConstantInt::get(
                        llvm::cast<llvm::IntegerType>(i32),
                        return_target),
                    instruction_blocks[return_target]);
            }
            return;
        }
        auto* depth_pointer
            = builder.CreateStructGEP(frame_type, frame_argument, 13);
        auto* depth = builder.CreateLoad(i32, depth_pointer, "return.depth");
        runtime_error_if(
            builder.CreateICmpEQ(
                depth,
                llvm::ConstantInt::get(
                    llvm::cast<llvm::IntegerType>(i32), 0)),
            JitGeneratedRuntimeErrorReason::call_stack_underflow,
            "native.return.stack.underflow");
        auto* next_depth = builder.CreateSub(
            depth,
            llvm::ConstantInt::get(
                llvm::cast<llvm::IntegerType>(i32), 1));
        auto* stack = builder.CreateStructGEP(
            frame_type, frame_argument, 15);
        auto* target_pointer = builder.CreateInBoundsGEP(
            llvm::ArrayType::get(
                i32, FSIM_JIT_NATIVE_CALL_STACK_CAPACITY_V1),
            stack,
            { llvm::ConstantInt::get(
                  llvm::cast<llvm::IntegerType>(i32), 0),
                next_depth });
        auto* target = builder.CreateLoad(
            i32, target_pointer, "native.return.target");
        builder.CreateStore(next_depth, depth_pointer);
        auto* dispatch_return = builder.CreateSwitch(
            target,
            invalid_pc,
            static_cast<unsigned>(native_return_targets.size()));
        for (const auto return_target : native_return_targets) {
            dispatch_return->addCase(
                llvm::ConstantInt::get(
                    llvm::cast<llvm::IntegerType>(i32),
                    return_target),
                instruction_blocks[return_target]);
        }
        return;
    }
    if (operation.stack.capacity == 0) {
        return_result(
            FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
            instruction,
            0,
            FSIM_JIT_FRAME_STATE_READY,
            instruction);
        return;
    }
    const auto pointer_value = load_register(builder, registers, operation.stack.pointer);
    runtime_error_if(
        builder.CreateICmpNE(
            pointer_value.bval, constant_i64(context, 0)),
        JitGeneratedRuntimeErrorReason::call_stack_unknown,
        "return.stack.unknown");
    runtime_error_if(
        builder.CreateOr(
            builder.CreateICmpEQ(
                pointer_value.aval, constant_i64(context, 0)),
            builder.CreateICmpUGT(
                pointer_value.aval,
                constant_i64(context, operation.stack.capacity))),
        JitGeneratedRuntimeErrorReason::call_stack_underflow,
        "return.stack.underflow");
    auto* next_pointer = builder.CreateSub(
        pointer_value.aval, constant_i64(context, 1));
    auto* entry_index = builder.CreateAdd(
        next_pointer,
        constant_i64(context, operation.stack.entries));
    auto* target_aval = builder.CreateLoad(
        i64,
        builder.CreateGEP(i64, register_aval, entry_index),
        "return.target.aval");
    auto* target_bval = builder.CreateLoad(
        i64,
        builder.CreateGEP(i64, register_bval, entry_index),
        "return.target.bval");
    runtime_error_if(
        builder.CreateOr(
            builder.CreateICmpNE(
                target_bval, constant_i64(context, 0)),
            builder.CreateICmpUGE(
                target_aval,
                constant_i64(context, instruction_blocks.size()))),
        JitGeneratedRuntimeErrorReason::call_stack_target,
        "return.stack.target");
    store_register(
        builder,
        registers,
        operation.stack.pointer,
        EncodedValue {
            next_pointer, constant_i64(context, 0), 32 });
    auto* dispatch_return = builder.CreateSwitch(
        builder.CreateTrunc(target_aval, i32),
        invalid_pc,
        static_cast<unsigned>(static_return_targets.size()));
    for (const auto target : static_return_targets) {
        dispatch_return->addCase(
            llvm::ConstantInt::get(
                llvm::cast<llvm::IntegerType>(i32),
                target),
            instruction_blocks[target]);
    }
}

void ControlFlowOperationLowerer::lower(const CallableFramePush&)
{
    if (native_frame) {
        builder.CreateBr(instruction_blocks[index + 1U]);
        return;
    }
    return_result(
        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
        instruction,
        0,
        FSIM_JIT_FRAME_STATE_READY,
        instruction);
}

void ControlFlowOperationLowerer::lower(const CallableFramePop&)
{
    if (native_frame) {
        builder.CreateBr(instruction_blocks[index + 1U]);
        return;
    }
    return_result(
        FSIM_JIT_RESUME_STATUS_SIMIR_BOUNDARY,
        instruction,
        0,
        FSIM_JIT_FRAME_STATE_READY,
        instruction);
}

void ControlFlowOperationLowerer::lower(const Branch& operation)
{
    const auto condition = load_register(builder, registers, operation.condition);
    auto* known = builder.CreateICmpEQ(
        condition.bval, constant_i64(context, 0));
    auto* one = builder.CreateICmpEQ(
        condition.aval, constant_i64(context, 1));
    if (operation.unknown_policy
        == UnknownBranchPolicy::when_false) {
        builder.CreateCondBr(
            builder.CreateAnd(known, one),
            instruction_blocks[operation.when_true],
            instruction_blocks[operation.when_false]);
        return;
    }

    auto* known_block = llvm::BasicBlock::Create(
        context, "branch.known." + std::to_string(index), function);
    auto* unknown_block = llvm::BasicBlock::Create(
        context, "branch.unknown." + std::to_string(index), function);
    builder.CreateCondBr(known, known_block, unknown_block);

    builder.SetInsertPoint(known_block);
    builder.CreateCondBr(
        one,
        instruction_blocks[operation.when_true],
        instruction_blocks[operation.when_false]);

    builder.SetInsertPoint(unknown_block);
    constexpr auto reason = JitGeneratedRuntimeErrorReason::unknown_branch_condition;
    return_result(
        FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR,
        instruction,
        static_cast<std::uint64_t>(reason),
        FSIM_JIT_FRAME_STATE_RUNTIME_ERROR,
        static_cast<std::uint32_t>(reason));
}

} // namespace fsim::compiler::llvm_detail
