// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_lowering_internal.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>

#include <cstddef>

namespace fsim::compiler::llvm_detail {

void lower_masked_member_gates(llvm::Function& function,
    llvm::StructType* runtime_type,
    const std::span<llvm::BasicBlock* const> instruction_blocks,
    const std::span<const FusedMaskedMemberGate> gates)
{
    if (gates.empty()) {
        return;
    }
    auto& context = function.getContext();
    auto* i8 = llvm::Type::getInt8Ty(context);
    auto* i32 = llvm::Type::getInt32Ty(context);
    auto* i64 = llvm::Type::getInt64Ty(context);
    auto* pointer = llvm::PointerType::getUnqual(context);
    auto* original_entry = &function.getEntryBlock();
    auto* check_size = llvm::BasicBlock::Create(context,
        "masked.check.size", &function, original_entry);
    auto* check_mask = llvm::BasicBlock::Create(context,
        "masked.check.words", &function, original_entry);
    auto* invalid = llvm::BasicBlock::Create(context,
        "masked.invalid", &function, original_entry);
    llvm::IRBuilder<> builder(check_size);
    auto* runtime = function.getArg(0U);
    const auto offset_pointer = [&](llvm::Value* object, const std::size_t offset) {
        return builder.CreateInBoundsGEP(i8, object,
            llvm::ConstantInt::get(i64, offset));
    };
    auto* size = builder.CreateLoad(
        i32,
        runtime_instance_field_address(
            builder,
            runtime_type,
            runtime,
            JitRuntimeInstanceField::struct_size));
    builder.CreateCondBr(
        builder.CreateICmpUGE(
            size,
            llvm::ConstantInt::get(
                i32, sizeof(fsim_jit_runtime_instance_v2))),
        check_mask,
        invalid);
    builder.SetInsertPoint(check_mask);
    auto* words = builder.CreateLoad(
        pointer,
        runtime_instance_field_address(
            builder,
            runtime_type,
            runtime,
            JitRuntimeInstanceField::fused_activation_words),
        "activation.words");
    auto* count = builder.CreateLoad(
        i32,
        runtime_instance_field_address(
            builder,
            runtime_type,
            runtime,
            JitRuntimeInstanceField::fused_activation_word_count));
    auto* pc = builder.CreateLoad(
        i32,
        offset_pointer(
            function.getArg(1U),
            offsetof(fsim_jit_frame_v2, program_counter)));
    auto* valid_pc = builder.CreateOr(
        builder.CreateICmpEQ(pc, llvm::ConstantInt::get(i32, 0U)),
        builder.CreateICmpEQ(pc,
            llvm::ConstantInt::get(i32, instruction_blocks.size() - 1U)));
    auto* valid_words = builder.CreateAnd(
        builder.CreateICmpNE(words, llvm::ConstantPointerNull::get(pointer)),
        builder.CreateICmpUGE(count,
            llvm::ConstantInt::get(i32, (gates.size() + 63U) / 64U)));
    builder.CreateCondBr(builder.CreateAnd(valid_pc, valid_words), original_entry, invalid);
    builder.SetInsertPoint(invalid);
    constexpr auto error_reason
        = JitGeneratedRuntimeErrorReason::fused_activation_invalid;
    const auto error_value = static_cast<std::uint64_t>(error_reason);
    builder.CreateStore(llvm::ConstantInt::get(i32, error_value),
        offset_pointer(function.getArg(1U),
            offsetof(fsim_jit_frame_v2, program_counter)));
    builder.CreateStore(llvm::ConstantInt::get(
            i32, FSIM_JIT_FRAME_STATE_RUNTIME_ERROR_V2),
        offset_pointer(function.getArg(1U), offsetof(fsim_jit_frame_v2, state)));
    builder.CreateStore(llvm::ConstantInt::get(
            i32, FSIM_JIT_INVALID_INSTRUCTION_V2),
        offset_pointer(function.getArg(1U),
            offsetof(fsim_jit_frame_v2, last_instruction)));
    builder.CreateStore(llvm::ConstantInt::get(
            i32, FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2),
        offset_pointer(function.getArg(2U),
            offsetof(fsim_jit_resume_result_v2, status)));
    builder.CreateStore(llvm::ConstantInt::get(
            i32, FSIM_JIT_INVALID_INSTRUCTION_V2),
        offset_pointer(function.getArg(2U),
            offsetof(fsim_jit_resume_result_v2, instruction)));
    builder.CreateStore(llvm::ConstantInt::get(i64, error_value),
        offset_pointer(function.getArg(2U),
            offsetof(fsim_jit_resume_result_v2, delay)));
    builder.CreateRet(llvm::ConstantInt::get(
        i32, FSIM_JIT_RESUME_STATUS_RUNTIME_ERROR_V2));

    // The preflight becomes LLVM's entry block. Keep fixed scratch allocas
    // there so normal SROA/mem2reg can still promote the register storage.
    for (auto it = original_entry->begin(); it != original_entry->end();) {
        auto* allocation = llvm::dyn_cast<llvm::AllocaInst>(&*it++);
        if (allocation && llvm::isa<llvm::ConstantInt>(allocation->getArraySize())) {
            allocation->moveBefore(check_size->getTerminator()->getIterator());
        }
    }

    // Every member has a separate register namespace and a forward-only CFG.
    // Incoming edges still enter its original block, now an activation gate.
    for (const auto& gate : gates) {
        if (gate.begin_instruction >= gate.end_instruction
            || gate.end_instruction >= instruction_blocks.size()
            || instruction_blocks[gate.begin_instruction] == nullptr
            || instruction_blocks[gate.end_instruction] == nullptr) {
            throw LlvmJitError("masked member has no complete native body");
        }
        auto* entry = instruction_blocks[gate.begin_instruction];
        auto* body = entry->splitBasicBlock(entry->getFirstNonPHIIt(), "masked.body");
        entry->getTerminator()->eraseFromParent();
        builder.SetInsertPoint(entry);
        auto* word = builder.CreateLoad(i64,
            builder.CreateInBoundsGEP(i64, words,
                llvm::ConstantInt::get(i32, gate.activation_bit / 64U)));
        auto* active = builder.CreateICmpNE(builder.CreateAnd(word,
            llvm::ConstantInt::get(i64, UINT64_C(1) << (gate.activation_bit % 64U))),
            llvm::ConstantInt::get(i64, 0U));
        builder.CreateCondBr(active, body, instruction_blocks[gate.end_instruction]);
    }
}

} // namespace fsim::compiler::llvm_detail
