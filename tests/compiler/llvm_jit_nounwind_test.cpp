// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"
#include "llvm_jit_test_support.hpp"

#include <llvm/ADT/ArrayRef.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>

#include <cassert>

namespace fsim::tests::compiler {
namespace {

using namespace fsim::compiler::llvm_detail;

[[nodiscard]] llvm::Function* make_callback_helper(
    llvm::Module& module,
    const llvm::GlobalValue::LinkageTypes linkage,
    const bool convert_pointer_through_integer = false)
{
    auto& context = module.getContext();
    auto* const void_type = llvm::Type::getVoidTy(context);
    auto* const pointer_type = llvm::PointerType::getUnqual(context);
    auto* const helper_type = llvm::FunctionType::get(
        void_type, { pointer_type }, false);
    auto* const helper = llvm::Function::Create(
        helper_type, linkage, "callback_helper", module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", helper);
    llvm::IRBuilder<> builder(entry);
    llvm::Value* callback = helper->getArg(0U);
    if (convert_pointer_through_integer) {
        auto* const integer_type = llvm::Type::getInt64Ty(context);
        callback = builder.CreateIntToPtr(
            builder.CreatePtrToInt(callback, integer_type), pointer_type);
    }
    auto* const callback_type = llvm::FunctionType::get(void_type, false);
    builder.CreateCall(callback_type, callback, { });
    builder.CreateRetVoid();
    return helper;
}

[[nodiscard]] llvm::Function* make_caller(
    llvm::Module& module,
    llvm::Function* helper,
    const bool mark_callback)
{
    auto& context = module.getContext();
    auto* const void_type = llvm::Type::getVoidTy(context);
    auto* const pointer_type = llvm::PointerType::getUnqual(context);
    auto* const caller_type = llvm::FunctionType::get(
        void_type, { pointer_type }, false);
    auto* const caller = llvm::Function::Create(
        caller_type, llvm::GlobalValue::ExternalLinkage,
        "kernel", module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", caller);
    llvm::IRBuilder<> builder(entry);
    auto* const callback = builder.CreateLoad(
        pointer_type, caller->getArg(0U), "callback");
    if (mark_callback) {
        const auto kind = context.getMDKindID(
            kJitNoUnwindCallbackMetadata);
        callback->setMetadata(
            kind,
            llvm::MDNode::get(
                context, llvm::ArrayRef<llvm::Metadata*> { }));
    }
    auto* const helper_call = builder.CreateCall(
        helper, { callback });
    static_cast<void>(helper_call);
    builder.CreateRetVoid();
    return caller;
}

void expect_rejected(llvm::Module& module)
{
    bool rejected = false;
    try {
        apply_jit_module_no_unwind_contract(module);
    } catch (const fsim::compiler::LlvmJitError&) {
        rejected = true;
    }
    assert(rejected);
}

void test_outlined_internal_callback_helper()
{
    llvm::LLVMContext context;
    llvm::Module module("outlined_internal_callback", context);
    auto* const helper = make_callback_helper(
        module, llvm::GlobalValue::InternalLinkage);
    auto* const caller = make_caller(module, helper, true);

    apply_jit_module_no_unwind_contract(module);
    require_jit_module_no_unwind_contract(module);
    assert(helper->hasFnAttribute(llvm::Attribute::NoUnwind));
    assert(caller->hasFnAttribute(llvm::Attribute::NoUnwind));

    auto* const helper_call = llvm::cast<llvm::CallInst>(
        &*helper->getEntryBlock().begin());
    assert(helper_call->hasFnAttr(llvm::Attribute::NoUnwind));
    helper_call->removeFnAttr(llvm::Attribute::NoUnwind);
    bool invariant_rejected = false;
    try {
        require_jit_module_no_unwind_contract(module);
    } catch (const fsim::compiler::LlvmJitError&) {
        invariant_rejected = true;
    }
    assert(invariant_rejected);
}

void test_external_callback_helper_rejected()
{
    llvm::LLVMContext context;
    llvm::Module module("external_callback_helper", context);
    auto* const helper = make_callback_helper(
        module, llvm::GlobalValue::ExternalLinkage);
    static_cast<void>(make_caller(module, helper, true));
    expect_rejected(module);
}

void test_address_escaping_callback_helper_rejected()
{
    llvm::LLVMContext context;
    llvm::Module module("escaping_callback_helper", context);
    auto* const helper = make_callback_helper(
        module, llvm::GlobalValue::InternalLinkage);
    auto* const caller = make_caller(module, helper, true);
    auto* const pointer_type = llvm::PointerType::getUnqual(context);
    static_cast<void>(new llvm::GlobalVariable(
        module,
        pointer_type,
        true,
        llvm::GlobalValue::InternalLinkage,
        helper,
        "escaped_helper"));
    static_cast<void>(caller);
    expect_rejected(module);
}

void test_address_space_cast_helper_use_rejected()
{
    llvm::LLVMContext context;
    llvm::Module module("address_space_cast_helper", context);
    auto* const helper = make_callback_helper(
        module, llvm::GlobalValue::InternalLinkage);
    auto* const caller = make_caller(module, helper, true);
    auto* const address_space_one = llvm::PointerType::get(context, 1U);
    llvm::IRBuilder<> builder(
        caller->getEntryBlock().getTerminator());
    auto* const cast_helper = builder.CreateAddrSpaceCast(
        helper, address_space_one);
    builder.CreateCall(
        helper->getFunctionType(), cast_helper, { caller->getArg(0U) });
    expect_rejected(module);
}

void test_non_pointer_preserving_callback_cast_rejected()
{
    llvm::LLVMContext context;
    llvm::Module module("integer_cast_callback", context);
    auto* const helper = make_callback_helper(
        module,
        llvm::GlobalValue::InternalLinkage,
        true);
    static_cast<void>(make_caller(module, helper, true));
    expect_rejected(module);
}

void test_unproven_address_space_callback_cast_rejected()
{
    llvm::LLVMContext context;
    llvm::Module module("address_space_cast_callback", context);
    auto* const void_type = llvm::Type::getVoidTy(context);
    auto* const source_pointer = llvm::PointerType::getUnqual(context);
    auto* const destination_pointer = llvm::PointerType::get(context, 1U);
    auto* const caller_type = llvm::FunctionType::get(
        void_type, { source_pointer }, false);
    auto* const caller = llvm::Function::Create(
        caller_type,
        llvm::GlobalValue::ExternalLinkage,
        "kernel",
        module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", caller);
    llvm::IRBuilder<> builder(entry);
    auto* const callback = builder.CreateLoad(
        source_pointer, caller->getArg(0U), "callback");
    const auto kind = context.getMDKindID(kJitNoUnwindCallbackMetadata);
    callback->setMetadata(
        kind,
        llvm::MDNode::get(
            context, llvm::ArrayRef<llvm::Metadata*> { }));
    auto* const cast_callback = builder.CreateAddrSpaceCast(
        callback, destination_pointer);
    auto* const callback_type = llvm::FunctionType::get(void_type, false);
    builder.CreateCall(callback_type, cast_callback, { });
    builder.CreateRetVoid();
    expect_rejected(module);
}

void test_unmarked_callback_rejected()
{
    llvm::LLVMContext context;
    llvm::Module module("unmarked_callback", context);
    auto* const void_type = llvm::Type::getVoidTy(context);
    auto* const pointer_type = llvm::PointerType::getUnqual(context);
    auto* const callback_type = llvm::FunctionType::get(void_type, false);
    auto* const caller_type = llvm::FunctionType::get(
        void_type, { pointer_type }, false);
    auto* const caller = llvm::Function::Create(
        caller_type,
        llvm::GlobalValue::ExternalLinkage,
        "kernel",
        module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", caller);
    llvm::IRBuilder<> builder(entry);
    auto* const callback = builder.CreateLoad(
        pointer_type, caller->getArg(0U), "unmarked_callback");
    builder.CreateCall(callback_type, callback, { });
    builder.CreateRetVoid();
    expect_rejected(module);
}

void test_unregistered_external_direct_call_rejected()
{
    llvm::LLVMContext context;
    llvm::Module module("unregistered_external_call", context);
    auto* const void_type = llvm::Type::getVoidTy(context);
    auto* const function_type = llvm::FunctionType::get(void_type, false);
    auto* const external = llvm::Function::Create(
        function_type,
        llvm::GlobalValue::ExternalLinkage,
        "unregistered_external",
        module);
    auto* const caller = llvm::Function::Create(
        function_type,
        llvm::GlobalValue::ExternalLinkage,
        "kernel",
        module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", caller);
    llvm::IRBuilder<> builder(entry);
    builder.CreateCall(external);
    builder.CreateRetVoid();
    expect_rejected(module);
}

void test_registered_external_direct_call()
{
    llvm::LLVMContext context;
    llvm::Module module("registered_external_call", context);
    auto* const void_type = llvm::Type::getVoidTy(context);
    auto* const function_type = llvm::FunctionType::get(void_type, false);
    auto* const external = llvm::Function::Create(
        function_type,
        llvm::GlobalValue::ExternalLinkage,
        "registered_external",
        module);
    external->addFnAttr(llvm::Attribute::NoUnwind);
    auto* const caller = llvm::Function::Create(
        function_type,
        llvm::GlobalValue::ExternalLinkage,
        "kernel",
        module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", caller);
    llvm::IRBuilder<> builder(entry);
    auto* const call = builder.CreateCall(external);
    builder.CreateRetVoid();

    apply_jit_module_no_unwind_contract(module);
    require_jit_module_no_unwind_contract(module);
    assert(call->hasFnAttr(llvm::Attribute::NoUnwind));
}

void test_throwing_intrinsic_rejected()
{
    llvm::LLVMContext context;
    llvm::Module module("throwing_intrinsic", context);
    auto* const void_type = llvm::Type::getVoidTy(context);
    auto* const caller_type = llvm::FunctionType::get(void_type, false);
    auto* const caller = llvm::Function::Create(
        caller_type,
        llvm::GlobalValue::ExternalLinkage,
        "kernel",
        module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", caller);
    llvm::IRBuilder<> builder(entry);
    auto* const patchpoint = llvm::Intrinsic::getOrInsertDeclaration(
        &module, llvm::Intrinsic::experimental_patchpoint_void);
    builder.CreateCall(
        patchpoint,
        { llvm::ConstantInt::get(llvm::Type::getInt64Ty(context), 1U),
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0U),
            llvm::ConstantPointerNull::get(
                llvm::PointerType::getUnqual(context)),
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), 0U) });
    builder.CreateRetVoid();
    expect_rejected(module);
}

void test_explicitly_nounwind_intrinsic_accepted()
{
    llvm::LLVMContext context;
    llvm::Module module("nounwind_intrinsic", context);
    auto* const void_type = llvm::Type::getVoidTy(context);
    auto* const i64 = llvm::Type::getInt64Ty(context);
    auto* const caller_type = llvm::FunctionType::get(void_type, false);
    auto* const caller = llvm::Function::Create(
        caller_type,
        llvm::GlobalValue::ExternalLinkage,
        "kernel",
        module);
    auto* const entry = llvm::BasicBlock::Create(context, "entry", caller);
    llvm::IRBuilder<> builder(entry);
    auto* const count_bits = llvm::Intrinsic::getOrInsertDeclaration(
        &module,
        llvm::Intrinsic::ctpop,
        llvm::ArrayRef<llvm::Type*> { i64 });
    assert(count_bits->hasFnAttribute(llvm::Attribute::NoUnwind));
    auto* const call = builder.CreateCall(
        count_bits, { llvm::ConstantInt::get(i64, 0x5aU) });
    static_cast<void>(call);
    builder.CreateRetVoid();

    apply_jit_module_no_unwind_contract(module);
    require_jit_module_no_unwind_contract(module);
}

} // namespace

void test_jit_nounwind_contract()
{
    test_outlined_internal_callback_helper();
    test_external_callback_helper_rejected();
    test_address_escaping_callback_helper_rejected();
    test_address_space_cast_helper_use_rejected();
    test_non_pointer_preserving_callback_cast_rejected();
    test_unproven_address_space_callback_cast_rejected();
    test_unmarked_callback_rejected();
    test_unregistered_external_direct_call_rejected();
    test_registered_external_direct_call();
    test_throwing_intrinsic_rejected();
    test_explicitly_nounwind_intrinsic_accepted();
}

} // namespace fsim::tests::compiler
