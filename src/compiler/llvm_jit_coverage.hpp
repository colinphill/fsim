// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit.hpp"

#include <llvm/IR/IRBuilder.h>

#include <cstdint>
#include <functional>
#include <string_view>

namespace fsim::compiler::llvm_detail {

inline constexpr std::string_view kLlvmCodeCoverageDiagnostic
    = "FSIM-COV-011";

struct CodeCoverageLowering {
    llvm::IRBuilder<>& builder;
    llvm::LLVMContext& context;
    llvm::Type* i32;
    llvm::Type* i64;
    llvm::Value* context_pointer;
    llvm::Value* hit_counters;
    llvm::Value* counter_values;
    llvm::Value* hit_count;
    llvm::Value* counter_count;
    llvm::Value* record_callback;
    llvm::FunctionType* record_callback_type;
    std::uint32_t process;
    std::uint32_t instruction;
    std::uint32_t hit_slot;
    std::function<void(
        llvm::Value*,
        JitGeneratedRuntimeErrorReason,
        std::string_view)>
        runtime_error_if;
    std::function<void()> branch_to_next;
};

void lower_code_coverage_hit(CodeCoverageLowering lowering);

} // namespace fsim::compiler::llvm_detail
