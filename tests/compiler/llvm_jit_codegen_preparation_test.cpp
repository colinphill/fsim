// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_codegen_preparation.hpp"
#include "llvm_jit_test_support.hpp"

#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/IntrinsicInst.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Error.h>
#include <llvm/TargetParser/Triple.h>

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace fsim::tests::compiler {
namespace {

using fsim::compiler::llvm_detail::prepare_fast_isel_module;
using NativeOperation = std::uint64_t (*)(
    std::uint64_t, std::uint64_t, std::uint64_t);
using NativeUnaryOperation = std::uint64_t (*)(std::uint64_t);

constexpr std::array intrinsic_ids {
    llvm::Intrinsic::ctpop, llvm::Intrinsic::fshl, llvm::Intrinsic::fshr,
    llvm::Intrinsic::abs, llvm::Intrinsic::smin, llvm::Intrinsic::smax,
    llvm::Intrinsic::umin, llvm::Intrinsic::umax,
};

std::uint64_t width_mask(const unsigned width)
{
    return std::numeric_limits<std::uint64_t>::max() >> (64U - width);
}

std::uint64_t expected_value(const llvm::Intrinsic::ID id,
    const unsigned width, std::uint64_t left, std::uint64_t right,
    const std::uint64_t shift)
{
    const auto mask = width_mask(width);
    left &= mask;
    right &= mask;
    const auto amount = (shift & mask) % width;
    const auto sign = std::uint64_t { 1U } << (width - 1U);
    const bool signed_less = (left ^ sign) < (right ^ sign);
    switch (id) {
    case llvm::Intrinsic::ctpop:
        return static_cast<std::uint64_t>(std::popcount(left));
    case llvm::Intrinsic::fshl:
    case llvm::Intrinsic::fshr: {
        // Independently extract each bit from the conceptual 2*width
        // concatenation; do not repeat the generated two-shift formula.
        std::uint64_t result { };
        for (unsigned bit { }; bit < width; ++bit) {
            const auto source = id == llvm::Intrinsic::fshl
                ? width + bit - amount : bit + amount;
            const auto source_bit = source >= width
                ? (left >> (source - width)) & 1U
                : (right >> source) & 1U;
            result |= source_bit << bit;
        }
        return result;
    }
    case llvm::Intrinsic::abs:
        return (left & sign) == 0U ? left : (0U - left) & mask;
    case llvm::Intrinsic::smin:
        return signed_less ? left : right;
    case llvm::Intrinsic::smax:
        return signed_less ? right : left;
    case llvm::Intrinsic::umin:
        return left < right ? left : right;
    case llvm::Intrinsic::umax:
        return left < right ? right : left;
    default:
        assert(false);
        return 0U;
    }
}

std::string add_operation(llvm::Module& module,
    const llvm::Intrinsic::ID id, const unsigned width)
{
    auto& context = module.getContext();
    llvm::IRBuilder<> builder { context };
    auto* const word = builder.getInt64Ty();
    auto* const type = builder.getIntNTy(width);
    const auto name = "prepare_" + std::to_string(id)
        + "_" + std::to_string(width);
    auto* function = llvm::Function::Create(
        llvm::FunctionType::get(word, { word, word, word }, false),
        llvm::Function::ExternalLinkage, name, module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
    const auto operand = [&](const unsigned index) {
        return builder.CreateTruncOrBitCast(function->getArg(index), type);
    };
    std::vector<llvm::Value*> arguments { operand(0U) };
    if (id == llvm::Intrinsic::abs) {
        arguments.push_back(builder.getFalse());
    } else if (id != llvm::Intrinsic::ctpop) {
        arguments.push_back(operand(1U));
        if (id == llvm::Intrinsic::fshl || id == llvm::Intrinsic::fshr) {
            arguments.push_back(operand(2U));
        }
    }
    auto* const declaration = llvm::Intrinsic::getOrInsertDeclaration(
        &module, id, { type });
    builder.CreateRet(builder.CreateZExtOrBitCast(
        builder.CreateCall(declaration, arguments), word));
    return name;
}

void check_native_expansion()
{
    auto jit = llvm::cantFail(llvm::orc::LLJITBuilder().create());
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("codegen-preparation", *context);
    module->setDataLayout(jit->getDataLayout());
    module->setTargetTriple(jit->getTargetTriple());
    constexpr std::array widths { 1U, 2U, 8U, 15U, 32U, 64U };
    for (const auto width : widths) {
        for (const auto id : intrinsic_ids) {
            add_operation(*module, id, width);
        }
    }
    assert(!llvm::verifyModule(*module));
    assert(prepare_fast_isel_module(*module));
    assert(!llvm::verifyModule(*module));
    for (const auto& function : *module) {
        for (const auto& block : function) {
            for (const auto& instruction : block) {
                assert(!llvm::isa<llvm::IntrinsicInst>(instruction));
            }
        }
    }
    llvm::cantFail(jit->addIRModule(llvm::orc::ThreadSafeModule {
        std::move(module), std::move(context) }));
    for (const auto width : widths) {
        const auto mask = width_mask(width);
        const std::array patterns { std::uint64_t { }, mask,
            mask >> 1U, std::uint64_t { 1U } << (width - 1U),
            std::uint64_t { 0xaaaaaaaaaaaaaaaaULL },
            std::uint64_t { 0x5555555555555555ULL } };
        for (const auto id : intrinsic_ids) {
            const auto name = "prepare_" + std::to_string(id)
                + "_" + std::to_string(width);
            const auto operation = llvm::cantFail(jit->lookup(name))
                .toPtr<NativeOperation>();
            for (const auto left : patterns) {
                for (const auto right : patterns) {
                    for (std::uint64_t shift { }; shift < 2U * width + 1U; ++shift) {
                        assert(operation(left, right, shift)
                            == expected_value(id, width, left, right, shift));
                    }
                    assert(operation(left, right, mask)
                        == expected_value(id, width, left, right, mask));
                }
            }
            // Exhaustive byte inputs cover signed extrema, all populations,
            // and every pair for comparisons and two-source funnel shifts.
            if (width == 8U) {
                for (std::uint64_t left { }; left <= 255U; ++left) {
                    for (std::uint64_t right { }; right <= 255U; ++right) {
                        const auto shift = left + right;
                        assert(operation(left, right, shift)
                            == expected_value(id, width, left, right, shift));
                    }
                }
            }
        }
    }
}

void check_control_flow_preparation()
{
    llvm::LLVMContext context;
    llvm::Module module { "codegen-cfg", context };
    module.setTargetTriple(llvm::Triple { "x86_64-pc-windows-msvc" });
    llvm::IRBuilder<> builder { context };
    auto* const type = builder.getInt64Ty();
    auto* const function = llvm::Function::Create(
        llvm::FunctionType::get(type, { type }, false),
        llvm::Function::ExternalLinkage, "cfg", module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    auto* const first = llvm::BasicBlock::Create(context, "first", function);
    auto* const other = llvm::BasicBlock::Create(context, "other", function);
    auto* const merge = llvm::BasicBlock::Create(context, "merge", function);
    builder.SetInsertPoint(entry);
    auto* const slot = builder.CreateAlloca(type);
    llvm::Value* value = function->getArg(0U);
    for (unsigned index { }; index < 40U; ++index) {
        value = builder.CreateXor(value, builder.getInt64(index));
    }
    auto* const selection = builder.CreateSwitch(value, other, 1U);
    selection->addCase(builder.getInt64(0U), first);
    builder.SetInsertPoint(first);
    auto* const incoming = builder.CreatePHI(type, 1U);
    incoming->addIncoming(value, entry);
    builder.CreateBr(merge);
    builder.SetInsertPoint(other);
    builder.CreateBr(merge);
    builder.SetInsertPoint(merge);
    auto* const result = builder.CreatePHI(type, 2U);
    result->addIncoming(incoming, first);
    result->addIncoming(builder.getInt64(2U), other);
    builder.CreateRet(result);
    assert(prepare_fast_isel_module(module));
    assert(!llvm::verifyModule(module));
    assert(slot->getParent() == entry);
    assert(slot->isStaticAlloca());
    assert(entry->size() == 2U);
    assert(selection->getParent()->size() == 1U);
    assert(result->getIncomingBlock(0U) == first);
    assert(result->getIncomingBlock(1U) == other);
    assert(incoming->getIncomingBlock(0U) == selection->getParent());
    assert(!prepare_fast_isel_module(module));
}

std::uint64_t xor_sequence(std::uint64_t value, const unsigned count)
{
    for (unsigned index { }; index < count; ++index) {
        value ^= 0x9e3779b97f4a7c15ULL
            + static_cast<std::uint64_t>(index);
    }
    return value;
}

void assert_prepared_blocks_are_bounded(const llvm::Function& function)
{
    constexpr std::size_t maximum_block_instructions { 1024U };
    for (const auto& block : function) {
        assert(block.size() <= maximum_block_instructions);
    }
}

void check_oversized_block_execution_and_control_flow()
{
    constexpr unsigned operation_count { 2500U };
    constexpr unsigned musttail_operation_count { 1300U };
    auto jit = llvm::cantFail(llvm::orc::LLJITBuilder().create());
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>(
        "codegen-oversized-blocks", *context);
    module->setDataLayout(jit->getDataLayout());
    module->setTargetTriple(jit->getTargetTriple());
    llvm::IRBuilder<> builder { *context };
    auto* const word = builder.getInt64Ty();
    auto* const function = llvm::Function::Create(
        llvm::FunctionType::get(word, { word }, false),
        llvm::Function::ExternalLinkage, "large_cfg", *module);
    auto* const entry = llvm::BasicBlock::Create(*context, "entry", function);
    auto* const check_one
        = llvm::BasicBlock::Create(*context, "check_one", function);
    auto* const from_zero
        = llvm::BasicBlock::Create(*context, "from_zero", function);
    auto* const from_one
        = llvm::BasicBlock::Create(*context, "from_one", function);
    auto* const alternate
        = llvm::BasicBlock::Create(*context, "alternate", function);
    auto* const work = llvm::BasicBlock::Create(*context, "work", function);
    auto* const merge = llvm::BasicBlock::Create(*context, "merge", function);
    builder.SetInsertPoint(entry);
    auto* const zero = builder.CreateICmpEQ(
        function->getArg(0U), builder.getInt64(0U));
    builder.CreateCondBr(zero, from_zero, check_one);
    builder.SetInsertPoint(check_one);
    auto* const one = builder.CreateICmpEQ(
        function->getArg(0U), builder.getInt64(1U));
    builder.CreateCondBr(one, from_one, alternate);
    builder.SetInsertPoint(from_zero);
    builder.CreateBr(work);
    builder.SetInsertPoint(from_one);
    auto* const one_input = builder.CreateAdd(
        function->getArg(0U), builder.getInt64(1U));
    builder.CreateBr(work);
    builder.SetInsertPoint(alternate);
    builder.CreateBr(merge);
    builder.SetInsertPoint(work);
    auto* const input = builder.CreatePHI(word, 2U);
    input->addIncoming(function->getArg(0U), from_zero);
    input->addIncoming(one_input, from_one);
    llvm::Value* value = input;
    for (unsigned index { }; index < operation_count; ++index) {
        value = builder.CreateXor(value, builder.getInt64(
            0x9e3779b97f4a7c15ULL + static_cast<std::uint64_t>(index)));
    }
    builder.CreateBr(merge);
    builder.SetInsertPoint(merge);
    auto* const result = builder.CreatePHI(word, 2U);
    result->addIncoming(value, work);
    result->addIncoming(builder.getInt64(0U), alternate);
    builder.CreateRet(result);

    auto* const callee = llvm::Function::Create(
        llvm::FunctionType::get(word, { word }, false),
        llvm::Function::ExternalLinkage, "musttail_target", *module);
    auto* const callee_entry = llvm::BasicBlock::Create(
        *context, "entry", callee);
    builder.SetInsertPoint(callee_entry);
    builder.CreateRet(callee->getArg(0U));

    auto* const tail_function = llvm::Function::Create(
        llvm::FunctionType::get(word, { word }, false),
        llvm::Function::ExternalLinkage, "large_musttail", *module);
    auto* const tail_entry = llvm::BasicBlock::Create(
        *context, "entry", tail_function);
    builder.SetInsertPoint(tail_entry);
    llvm::Value* tail_value = tail_function->getArg(0U);
    for (unsigned index { }; index < musttail_operation_count; ++index) {
        tail_value = builder.CreateXor(tail_value,
            builder.getInt64(0x9e3779b97f4a7c15ULL
                + static_cast<std::uint64_t>(index)));
    }
    auto* const musttail = builder.CreateCall(callee, { tail_value });
    musttail->setTailCallKind(llvm::CallInst::TCK_MustTail);
    auto* const tail_return = builder.CreateRet(musttail);

    assert(!llvm::verifyModule(*module));
    assert(prepare_fast_isel_module(*module));
    assert(!llvm::verifyModule(*module));
    assert_prepared_blocks_are_bounded(*function);
    assert(input->getParent() == work);
    assert(result->getIncomingBlock(0U) != work);
    auto* const result_predecessor = result->getIncomingBlock(0U);
    auto* const result_branch = llvm::cast<llvm::BranchInst>(
        result_predecessor->getTerminator());
    assert(result_branch->getSuccessor(0U) == merge);
    assert(musttail->getParent()->size() > 1024U);
    assert(musttail->getParent() == tail_return->getParent());
    assert(musttail->getFunctionType() == tail_function->getFunctionType());
    assert(musttail->getNextNode() == tail_return);
    assert(!prepare_fast_isel_module(*module));

    llvm::cantFail(jit->addIRModule(llvm::orc::ThreadSafeModule {
        std::move(module), std::move(context) }));
    const auto compiled = llvm::cantFail(jit->lookup("large_cfg"))
                              .toPtr<NativeUnaryOperation>();
    for (const auto input_value : std::array<std::uint64_t, 4U> {
             0U, 1U, 2U, 0x123456789abcdef0ULL }) {
        const auto expected = input_value == 0U
            ? xor_sequence(input_value, operation_count)
            : input_value == 1U
                ? xor_sequence(input_value + 1U, operation_count) : 0U;
        assert(compiled(input_value) == expected);
    }
}

void check_nonprefix_static_alloca()
{
    llvm::LLVMContext context;
    llvm::Module module { "codegen-nonprefix-alloca", context };
    module.setTargetTriple(llvm::Triple { "x86_64-pc-windows-msvc" });
    llvm::IRBuilder<> builder { context };
    auto* const word = builder.getInt64Ty();
    auto* const function = llvm::Function::Create(
        llvm::FunctionType::get(word, { word }, false),
        llvm::Function::ExternalLinkage, "late_static_alloca", module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", function);
    builder.SetInsertPoint(entry);
    auto* value = builder.CreateAdd(function->getArg(0U), builder.getInt64(1U));
    auto* const slot = builder.CreateAlloca(word);
    for (unsigned index { }; index < 1500U; ++index) {
        value = builder.CreateXor(value,
            builder.getInt64(0x9e3779b97f4a7c15ULL
                + static_cast<std::uint64_t>(index)));
    }
    builder.CreateRet(value);
    assert(slot->isStaticAlloca());
    assert(!llvm::verifyModule(module));
    assert(!prepare_fast_isel_module(module));
    assert(!llvm::verifyModule(module));
    assert(slot->getParent() == entry);
    assert(slot->isStaticAlloca());
    assert(entry->size() > 1024U);
}

void check_poison_and_unsupported_types()
{
    llvm::LLVMContext context;
    llvm::Module module { "codegen-poison-and-fallback", context };
    llvm::IRBuilder<> builder { context };
    auto* const byte = builder.getInt8Ty();
    auto* const poison_function = llvm::Function::Create(
        llvm::FunctionType::get(byte, false),
        llvm::Function::ExternalLinkage, "poison", module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(
        context, "entry", poison_function));
    auto* const absolute = llvm::Intrinsic::getOrInsertDeclaration(
        &module, llvm::Intrinsic::abs, { byte });
    auto* const result = builder.CreateCall(absolute,
        { builder.getInt8(128U), builder.getTrue() });
    auto* const returned = builder.CreateRet(result);
    std::vector<llvm::Type*> unsupported_types {
        builder.getIntNTy(65U),
        llvm::FixedVectorType::get(builder.getInt64Ty(), 2U),
    };
    std::vector<llvm::CallInst*> retained;
    for (auto* type : unsupported_types) {
        auto* const function = llvm::Function::Create(
            llvm::FunctionType::get(type, { type }, false),
            llvm::Function::ExternalLinkage, "unsupported", module);
        builder.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
        auto* const declaration = llvm::Intrinsic::getOrInsertDeclaration(
            &module, llvm::Intrinsic::ctpop, { type });
        auto* const call = builder.CreateCall(declaration, { function->getArg(0U) });
        retained.push_back(call);
        builder.CreateRet(call);
    }
    assert(prepare_fast_isel_module(module));
    assert(!llvm::verifyModule(module));
    assert(llvm::isa<llvm::PoisonValue>(returned->getReturnValue()));
    for (auto* call : retained) {
        assert(call->getParent() != nullptr);
        assert(call->getIntrinsicID() == llvm::Intrinsic::ctpop);
    }
    assert(!prepare_fast_isel_module(module));
}

} // namespace

void test_fast_isel_codegen_preparation()
{
    check_native_expansion();
    check_control_flow_preparation();
    check_oversized_block_execution_and_control_flow();
    check_nonprefix_static_alloca();
    check_poison_and_unsupported_types();
}

} // namespace fsim::tests::compiler
