// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit_region_frontier.hpp"

#include <cstdint>

namespace fsim::compiler::llvm_detail {

/// Source-private observations for compiler tests. These values expose actual
/// ORC body ownership and per-materialization cache accounting without adding
/// fields to the installed executor API.
struct RegionFrontierTestAccess final {
    [[nodiscard]] static std::uint64_t shared_body_address(
        const LlvmRegionFrontierExecutor& executor) noexcept;

    [[nodiscard]] static const void* shared_body_owner_token(
        const LlvmRegionFrontierExecutor& executor) noexcept;

    [[nodiscard]] static bool body_registry_reused(
        const LlvmRegionFrontierExecutor& executor) noexcept;

    [[nodiscard]] static bool wrapper_registry_reused(
        const LlvmRegionFrontierExecutor& executor) noexcept;

    [[nodiscard]] static LlvmJitCacheStatistics body_cache_statistics(
        const LlvmRegionFrontierExecutor& executor) noexcept;

    [[nodiscard]] static LlvmJitCacheStatistics wrapper_cache_statistics(
        const LlvmRegionFrontierExecutor& executor) noexcept;
};

} // namespace fsim::compiler::llvm_detail
