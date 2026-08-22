// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_coverage.hpp"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>

#include <limits>

namespace fsim::compiler::llvm_detail {

void lower_code_coverage_hit(CodeCoverageLowering lowering)
{
    auto& builder = lowering.builder;
    auto* function = builder.GetInsertBlock()->getParent();
    auto* const pointer_type = llvm::cast<llvm::PointerType>(
        lowering.hit_counters->getType());
    const auto nonnull = [&](llvm::Value* value) {
        return builder.CreateICmpNE(
            value, llvm::ConstantPointerNull::get(pointer_type));
    };

    const auto slot = llvm::ConstantInt::get(
        lowering.i32, lowering.hit_slot);
    const auto map_available = builder.CreateAnd(
        nonnull(lowering.hit_counters),
        builder.CreateICmpULT(slot, lowering.hit_count));
    lowering.runtime_error_if(
        builder.CreateNot(map_available),
        JitGeneratedRuntimeErrorReason::coverage_callback_failure,
        "code.coverage.map");

    auto* counter = builder.CreateLoad(
        lowering.i32,
        builder.CreateInBoundsGEP(
            lowering.i32, lowering.hit_counters, slot),
        "code.coverage.counter");
    auto* direct = llvm::BasicBlock::Create(
        lowering.context, "code.coverage.direct", function);
    auto* increment = llvm::BasicBlock::Create(
        lowering.context, "code.coverage.increment", function);
    auto* checked = llvm::BasicBlock::Create(
        lowering.context, "code.coverage.checked", function);
    auto* done = llvm::BasicBlock::Create(
        lowering.context, "code.coverage.done", function);
    builder.CreateCondBr(
        builder.CreateAnd(
            nonnull(lowering.counter_values),
            builder.CreateICmpULT(counter, lowering.counter_count)),
        direct,
        checked);

    builder.SetInsertPoint(direct);
    auto* address = builder.CreateInBoundsGEP(
        lowering.i64, lowering.counter_values, counter);
    auto* value = builder.CreateLoad(
        lowering.i64, address, "code.coverage.value");
    builder.CreateCondBr(
        builder.CreateICmpNE(
            value,
            llvm::ConstantInt::get(
                lowering.i64,
                std::numeric_limits<std::uint64_t>::max())),
        increment,
        checked);

    builder.SetInsertPoint(increment);
    builder.CreateStore(
        builder.CreateAdd(
            value, llvm::ConstantInt::get(lowering.i64, 1U)),
        address);
    builder.CreateBr(done);

    builder.SetInsertPoint(checked);
    lowering.runtime_error_if(
        builder.CreateICmpEQ(
            lowering.record_callback,
            llvm::ConstantPointerNull::get(pointer_type)),
        JitGeneratedRuntimeErrorReason::coverage_callback_failure,
        "code.coverage.callback");
    auto* status = builder.CreateCall(
        lowering.record_callback_type,
        lowering.record_callback,
        { lowering.context_pointer,
            llvm::ConstantInt::get(lowering.i32, lowering.process),
            llvm::ConstantInt::get(lowering.i32, lowering.instruction),
            counter });
    lowering.runtime_error_if(
        builder.CreateICmpNE(
            status, llvm::ConstantInt::get(lowering.i32, 0U)),
        JitGeneratedRuntimeErrorReason::coverage_callback_failure,
        "code.coverage.record");
    builder.CreateBr(done);

    builder.SetInsertPoint(done);
    lowering.branch_to_next();
}

} // namespace fsim::compiler::llvm_detail
