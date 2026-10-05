// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_codegen_preparation.hpp"

#include <llvm/TargetParser/Triple.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace fsim::compiler::llvm_detail {
namespace {

llvm::Value* expand_population_count(
    llvm::IRBuilder<>& builder, llvm::Value* value)
{
    auto* const result_type = value->getType();
    auto* const word_type = builder.getInt64Ty();
    value = builder.CreateZExtOrTrunc(value, word_type);
    const auto mask = [&](const std::uint64_t bits) {
        return llvm::ConstantInt::get(word_type, bits);
    };
    value = builder.CreateSub(value, builder.CreateAnd(
        builder.CreateLShr(value, 1U), mask(0x5555555555555555ULL)));
    value = builder.CreateAdd(
        builder.CreateAnd(value, mask(0x3333333333333333ULL)),
        builder.CreateAnd(builder.CreateLShr(value, 2U),
            mask(0x3333333333333333ULL)));
    value = builder.CreateAnd(
        builder.CreateAdd(value, builder.CreateLShr(value, 4U)),
        mask(0x0f0f0f0f0f0f0f0fULL));
    value = builder.CreateMul(value, mask(0x0101010101010101ULL));
    return builder.CreateZExtOrTrunc(
        builder.CreateLShr(value, 56U), result_type);
}

llvm::Value* expand_funnel_shift(
    llvm::IRBuilder<>& builder, llvm::IntrinsicInst& intrinsic)
{
    auto* const type = intrinsic.getType();
    const auto width = type->getIntegerBitWidth();
    auto* const left = intrinsic.getArgOperand(0U);
    auto* const right = intrinsic.getArgOperand(1U);
    const bool shift_left = intrinsic.getIntrinsicID() == llvm::Intrinsic::fshl;
    if (width == 1U) {
        return shift_left ? left : right;
    }
    auto* const size = llvm::ConstantInt::get(type, width);
    auto* const zero = llvm::ConstantInt::get(type, 0U);
    auto* const amount = builder.CreateURem(
        intrinsic.getArgOperand(2U), size);
    // Both shifts stay in range even when amount is zero. The select then
    // discards the opposite source, matching funnel-shift modulo semantics.
    auto* const complement = builder.CreateURem(
        builder.CreateSub(size, amount), size);
    auto* const combined = builder.CreateOr(
        builder.CreateShl(left, shift_left ? amount : complement),
        builder.CreateLShr(right, shift_left ? complement : amount));
    return builder.CreateSelect(builder.CreateICmpEQ(amount, zero),
        shift_left ? left : right, combined);
}

llvm::Value* expand_intrinsic(llvm::IntrinsicInst& intrinsic)
{
    auto* const type = intrinsic.getType();
    if (!type->isIntegerTy() || type->getIntegerBitWidth() > 64U
        || intrinsic.arg_empty()) {
        return nullptr;
    }
    llvm::IRBuilder<> builder { &intrinsic };
    builder.SetCurrentDebugLocation(intrinsic.getDebugLoc());
    auto* const left = intrinsic.getArgOperand(0U);
    switch (intrinsic.getIntrinsicID()) {
    case llvm::Intrinsic::ctpop:
        return expand_population_count(builder, left);
    case llvm::Intrinsic::fshl:
    case llvm::Intrinsic::fshr:
        return expand_funnel_shift(builder, intrinsic);
    case llvm::Intrinsic::abs: {
        auto* const zero = llvm::ConstantInt::get(type, 0U);
        const bool minimum_is_poison = llvm::cast<llvm::ConstantInt>(
            intrinsic.getArgOperand(1U))->isOne();
        auto* const minimum = llvm::ConstantInt::get(
            type, llvm::APInt::getSignedMinValue(type->getIntegerBitWidth()));
        if (minimum_is_poison) {
            if (const auto* constant = llvm::dyn_cast<llvm::ConstantInt>(left);
                constant && constant == minimum) {
                return llvm::PoisonValue::get(type);
            }
        }
        auto* const negative = builder.CreateSub(zero, left);
        auto* const absolute = builder.CreateSelect(
            builder.CreateICmpSLT(left, zero), negative, left);
        if (!minimum_is_poison) {
            return absolute;
        }
        return builder.CreateSelect(
            builder.CreateICmpEQ(left, minimum),
            llvm::PoisonValue::get(type), absolute);
    }
    case llvm::Intrinsic::smin:
    case llvm::Intrinsic::smax:
    case llvm::Intrinsic::umin:
    case llvm::Intrinsic::umax: {
        auto* const right = intrinsic.getArgOperand(1U);
        const auto id = intrinsic.getIntrinsicID();
        const bool signed_comparison = id == llvm::Intrinsic::smin
            || id == llvm::Intrinsic::smax;
        const bool minimum = id == llvm::Intrinsic::smin
            || id == llvm::Intrinsic::umin;
        auto* const less = signed_comparison
            ? builder.CreateICmpSLT(left, right)
            : builder.CreateICmpULT(left, right);
        return builder.CreateSelect(less,
            minimum ? left : right, minimum ? right : left);
    }
    default:
        return nullptr;
    }
}

constexpr std::size_t maximum_prepared_block_instructions { 1024U };

bool has_nonprefix_static_alloca(llvm::BasicBlock& block,
    llvm::BasicBlock::iterator first_body_instruction)
{
    for (auto instruction = first_body_instruction;
         instruction != block.end(); ++instruction) {
        if (const auto* const alloca
            = llvm::dyn_cast<llvm::AllocaInst>(&*instruction);
            alloca != nullptr && alloca->isStaticAlloca()) {
            return true;
        }
    }
    return false;
}

bool contains_musttail_call(const llvm::BasicBlock& block)
{
    for (const auto& instruction : block) {
        if (const auto* const call = llvm::dyn_cast<llvm::CallBase>(&instruction);
            call != nullptr && call->isMustTailCall()) {
            return true;
        }
    }
    return false;
}

void collect_oversized_block_cuts(llvm::Function& function,
    llvm::BasicBlock& block, std::vector<llvm::Instruction*>& cuts)
{
    if (function.hasPersonalityFn() || block.isEHPad()
        || contains_musttail_call(block)
        || !llvm::isa<llvm::BranchInst, llvm::ReturnInst>(block.getTerminator())) {
        return;
    }

    auto instruction = block.begin();
    std::size_t protected_prefix_count { };
    while (instruction != block.end()
        && llvm::isa<llvm::PHINode>(*instruction)) {
        ++instruction;
        ++protected_prefix_count;
    }
    if (&block == &function.getEntryBlock()) {
        while (instruction != block.end()
            && llvm::isa<llvm::AllocaInst>(*instruction)) {
            ++instruction;
            ++protected_prefix_count;
        }
        // A later static alloca is still a static entry allocation. Do not
        // move it to a generated body block.
        if (has_nonprefix_static_alloca(block, instruction)) {
            return;
        }
    }
    if (protected_prefix_count >= maximum_prepared_block_instructions) {
        return;
    }

    // Every split adds an unconditional branch to its prefix. Count that
    // branch against the chunk limit and leave the original terminator in
    // the final chunk.
    auto chunk_instruction_count = protected_prefix_count;
    for (; instruction != block.end(); ++instruction) {
        if (instruction->isTerminator()) {
            break;
        }
        if (chunk_instruction_count
            == maximum_prepared_block_instructions - 1U) {
            cuts.push_back(&*instruction);
            chunk_instruction_count = 0U;
        }
        ++chunk_instruction_count;
    }
}

bool split_oversized_ordinary_blocks(llvm::Module& module)
{
    std::vector<llvm::Instruction*> split_points;
    for (auto& function : module) {
        if (function.isDeclaration()) {
            continue;
        }
        for (auto& block : function) {
            collect_oversized_block_cuts(function, block, split_points);
        }
    }

    // splitBasicBlock splices the suffix and changes the moved instructions'
    // parent. Splitting the saved points from right to left keeps each move to
    // one bounded chunk and leaves earlier saved points in their original
    // prefix.
    for (auto split = split_points.rbegin(); split != split_points.rend(); ++split) {
        (*split)->getParent()->splitBasicBlock(
            *split, "fsim.codegen.chunk");
    }
    return !split_points.empty();
}

} // namespace

bool prepare_fast_isel_module(llvm::Module& module)
{
    std::vector<llvm::IntrinsicInst*> intrinsics;
    std::vector<llvm::SwitchInst*> large_switches;
    for (auto& function : module) {
        for (auto& block : function) {
            for (auto& instruction : block) {
                if (auto* intrinsic
                    = llvm::dyn_cast<llvm::IntrinsicInst>(&instruction)) {
                    intrinsics.push_back(intrinsic);
                }
            }
            // Isolate only substantial prefixes. A switch may force fallback
            // selection, but its unrelated preceding instructions need not.
            if (block.size() >= 32U) {
                if (auto* selection = llvm::dyn_cast<llvm::SwitchInst>(
                        block.getTerminator())) {
                    large_switches.push_back(selection);
                }
            }
        }
    }
    bool changed { };
    for (auto* intrinsic : intrinsics) {
        if (auto* replacement = expand_intrinsic(*intrinsic)) {
            intrinsic->replaceAllUsesWith(replacement);
            intrinsic->eraseFromParent();
            changed = true;
        }
    }
    for (auto* selection : large_switches) {
        selection->getParent()->splitBasicBlock(
            selection->getIterator(), "fsim.switch.dispatch");
        changed = true;
    }
    const auto& triple = module.getTargetTriple();
    if (triple.isOSWindows() && triple.getArch() == llvm::Triple::x86_64) {
        for (auto& function : module) {
            if (function.isDeclaration()) {
                continue;
            }
            auto& entry = function.getEntryBlock();
            auto body = entry.begin();
            while (body != entry.end()
                && llvm::isa<llvm::AllocaInst>(*body)) {
                ++body;
            }
            if (!has_nonprefix_static_alloca(entry, body)
                && body != entry.end() && !body->isTerminator()) {
                // Keep static allocas in the entry block. Argument lowering
                // can fall back there without taking the body with it.
                entry.splitBasicBlock(body, "fsim.argument.body");
                changed = true;
            }
        }
    }
    if (split_oversized_ordinary_blocks(module)) {
        changed = true;
    }
    return changed;
}

} // namespace fsim::compiler::llvm_detail
