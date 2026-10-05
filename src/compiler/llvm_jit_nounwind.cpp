// SPDX-License-Identifier: Apache-2.0
#include "llvm_jit_internal.hpp"

#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Operator.h>

#include <vector>

namespace fsim::compiler::llvm_detail {
namespace {

[[nodiscard]] const llvm::Value* strip_pointer_bitcasts(
    const llvm::Value* value)
{
    while (const auto* const cast
        = llvm::dyn_cast<llvm::BitCastOperator>(value)) {
        value = cast->getOperand(0U);
    }
    return value;
}

[[nodiscard]] bool is_callback_value(
    const llvm::Value* value,
    const unsigned metadata_kind,
    llvm::SmallPtrSetImpl<const llvm::Value*>& active)
{
    if (value == nullptr || !active.insert(value).second) {
        return false;
    }

    bool result = false;
    if (const auto* const load = llvm::dyn_cast<llvm::LoadInst>(value)) {
        result = load->getMetadata(metadata_kind) != nullptr;
    } else if (const auto* const argument
        = llvm::dyn_cast<llvm::Argument>(value)) {
        const auto* const helper = argument->getParent();
        // CodeExtractor helpers stay local. Prove every use is a direct call
        // so the callback argument cannot escape to an unclassified caller.
        if (!helper->hasLocalLinkage()) {
            active.erase(value);
            return false;
        }
        bool has_direct_caller = false;
        result = true;
        for (const auto* const user : helper->users()) {
            const auto* const call = llvm::dyn_cast<llvm::CallBase>(user);
            if (call == nullptr || !llvm::isa<llvm::CallInst>(call)
                || strip_pointer_bitcasts(call->getCalledOperand())
                    != helper) {
                result = false;
                break;
            }
            has_direct_caller = true;
            const auto argument_index = argument->getArgNo();
            if (argument_index >= call->arg_size()
                || !is_callback_value(
                    call->getArgOperand(argument_index),
                    metadata_kind, active)) {
                result = false;
                break;
            }
        }
        result = result && has_direct_caller;
    } else if (const auto* const phi = llvm::dyn_cast<llvm::PHINode>(value)) {
        result = phi->getNumIncomingValues() != 0U;
        for (unsigned index = 0U;
             index < phi->getNumIncomingValues(); ++index) {
            if (!is_callback_value(
                    phi->getIncomingValue(index), metadata_kind, active)) {
                result = false;
                break;
            }
        }
    } else if (const auto* const select
        = llvm::dyn_cast<llvm::SelectInst>(value)) {
        result = is_callback_value(
                     select->getTrueValue(), metadata_kind, active)
            && is_callback_value(
                select->getFalseValue(), metadata_kind, active);
    } else if (const auto* const freeze
        = llvm::dyn_cast<llvm::FreezeInst>(value)) {
        result = is_callback_value(
            freeze->getOperand(0U), metadata_kind, active);
    } else if (const auto* const cast
        = llvm::dyn_cast<llvm::BitCastInst>(value)) {
        result = is_callback_value(
            cast->getOperand(0U), metadata_kind, active);
    }

    active.erase(value);
    return result;
}

[[nodiscard]] bool is_closed_direct_callee(
    const llvm::Function& callee, const llvm::Module& module)
{
    // Intrinsics are not implicitly safe: LLVM defines throwing intrinsics.
    return (!callee.isDeclaration() && callee.getParent() == &module)
        || callee.hasFnAttribute(llvm::Attribute::NoUnwind);
}

} // namespace

void apply_jit_module_no_unwind_contract(llvm::Module& module)
{
    const auto metadata_kind
        = module.getContext().getMDKindID(
            kJitNoUnwindCallbackMetadata);
    std::vector<llvm::CallBase*> closed_calls;
    for (auto& function : module) {
        if (function.isDeclaration()) {
            continue;
        }
        for (auto& block : function) {
            for (auto& instruction : block) {
                auto* const call = llvm::dyn_cast<llvm::CallBase>(&instruction);
                if (call == nullptr) {
                    if (llvm::isa<llvm::ResumeInst>(instruction)
                        || llvm::isa<llvm::LandingPadInst>(instruction)
                        || llvm::isa<llvm::CatchSwitchInst>(instruction)
                        || llvm::isa<llvm::CatchReturnInst>(instruction)
                        || llvm::isa<llvm::CatchPadInst>(instruction)
                        || llvm::isa<llvm::CleanupReturnInst>(instruction)
                        || llvm::isa<llvm::CleanupPadInst>(instruction)) {
                        throw LlvmJitError(
                            "generated process contains explicit exception "
                            "control flow");
                    }
                    continue;
                }
                if (!llvm::isa<llvm::CallInst>(call)) {
                    throw LlvmJitError(
                        "generated process contains a non-call control "
                        "transfer site");
                }
                const auto* const called_operand = call->getCalledOperand();
                const auto* const called_value
                    = strip_pointer_bitcasts(called_operand);
                if (const auto* const callee
                    = llvm::dyn_cast<llvm::Function>(called_value)) {
                    if (!is_closed_direct_callee(*callee, module)) {
                        throw LlvmJitError(
                            "generated process calls an unregistered "
                            "external function without the no-unwind "
                            "contract");
                    }
                } else {
                    llvm::SmallPtrSet<const llvm::Value*, 8U> active;
                    if (!is_callback_value(
                            called_operand, metadata_kind, active)) {
                        throw LlvmJitError(
                            "generated process contains an unclassified "
                            "indirect call");
                    }
                }
                closed_calls.push_back(call);
            }
        }
    }

    for (auto& function : module) {
        if (!function.isDeclaration()) {
            function.addFnAttr(llvm::Attribute::NoUnwind);
        }
    }
    for (auto* const call : closed_calls) {
        call->addFnAttr(llvm::Attribute::NoUnwind);
    }
}

void require_jit_module_no_unwind_contract(const llvm::Module& module)
{
    for (const auto& function : module) {
        if (function.isDeclaration()) {
            continue;
        }
        if (!function.hasFnAttribute(llvm::Attribute::NoUnwind)) {
            throw LlvmJitError(
                "generated JIT function lost its no-unwind attribute");
        }
        for (const auto& block : function) {
            for (const auto& instruction : block) {
                const auto* const call
                    = llvm::dyn_cast<llvm::CallBase>(&instruction);
                if (call != nullptr
                    && !call->hasFnAttr(llvm::Attribute::NoUnwind)) {
                    throw LlvmJitError(
                        "generated JIT callsite lost its no-unwind "
                        "attribute");
                }
            }
        }
    }
}

} // namespace fsim::compiler::llvm_detail
