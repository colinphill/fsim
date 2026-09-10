// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "llvm_jit_internal.hpp"

#include <llvm/ADT/STLFunctionalExtras.h>

namespace fsim::compiler::llvm_detail {

struct OperationValidationContext {
    const runtime::simir::Process& process;
    ValidatedProcess& result;
    std::span<const std::uint32_t> signal_widths;
    std::span<const runtime::simir::ValueKind> signal_value_kinds;
    llvm::function_ref<std::uint32_t(std::uint32_t, std::size_t)>
        exact_signal_width;
    llvm::function_ref<std::uint32_t(std::uint32_t, std::size_t)>
        referenced_signal_width;
    llvm::function_ref<std::uint32_t(std::uint32_t, std::size_t)> signal_width;
    llvm::function_ref<void(runtime::simir::RegisterId, std::size_t)> record_use;
    llvm::function_ref<void(runtime::simir::RegisterId, std::size_t, std::size_t)>
        constrain_width;
    llvm::function_ref<void(runtime::simir::RegisterId, std::size_t)>
        record_definition;
    llvm::function_ref<void(
        runtime::simir::RegisterId,
        runtime::simir::RegisterId,
        std::size_t)> unify_registers;
    llvm::function_ref<void(
        runtime::simir::StringRegisterId,
        std::size_t,
        std::string_view)> validate_string_register;
    llvm::function_ref<void(
        runtime::simir::ContainerRegisterId,
        std::size_t,
        std::string_view)> validate_container_register;
};

void validate_prefix_operation(
    const OperationValidationContext& context,
    const runtime::simir::Operation& operation,
    std::size_t index);

} // namespace fsim::compiler::llvm_detail
