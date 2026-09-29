// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/runtime/simir.hpp"

#include <memory>
#include <span>

namespace fsim::app {

[[nodiscard]] std::unique_ptr<runtime::simir::FusedStaticCohortExecutor>
make_fused_static_executor(
    compiler::LlvmJit& jit,
    compiler::JitProcessHandle handle,
    std::span<const runtime::simir::SignalId> actual_signals,
    std::span<const std::uint32_t> canonical_widths,
    std::span<const runtime::simir::SignalId> output_order,
    std::span<const runtime::simir::ValueKind> canonical_kinds = {},
    bool projected = false);

[[nodiscard]] std::unique_ptr<runtime::simir::FusedMaskedRegionExecutor>
make_fused_masked_region_executor(
    compiler::LlvmJit& jit,
    compiler::JitProcessHandle handle,
    std::span<const runtime::simir::SignalId> actual_signals,
    std::span<const std::uint32_t> canonical_widths,
    std::span<const runtime::simir::SignalId> output_order,
    std::span<const runtime::simir::ValueKind> canonical_kinds = {},
    bool projected = false);

} // namespace fsim::app
