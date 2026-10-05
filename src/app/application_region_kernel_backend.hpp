// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/runtime/simir_region_kernel_backend.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace fsim::app::application_detail {

[[nodiscard]] std::shared_ptr<runtime::simir::RegionKernelBackendProvider>
make_llvm_region_kernel_backend_provider(
    compiler::LlvmJitOptions options,
    std::string_view immutable_design_identity);

/// Inspect the private LLVM backend's most recent constant-variant decision
/// in application tests. Foreign backends have no such decision.
[[nodiscard]] std::optional<bool>
last_region_constant_variant_for_testing(
    const runtime::simir::RegionKernelBackend& backend) noexcept;

/// Observe the private LLVM frontier backend identity in focused tests. Body
/// reuse is reported separately from persistent object-cache statistics.
struct RegionFrontierBackendTestingSnapshot final {
    const void* shared_body_owner_token { };
    std::uint64_t shared_body_address { };
    runtime::simir::RegionFrontierStepEntryV2 wrapper_entry { };
    bool shared_body_registry_reused { };
    bool wrapper_registry_reused { };
    compiler::LlvmJitCacheStatistics body_object_cache_statistics { };
    compiler::LlvmJitCacheStatistics wrapper_object_cache_statistics { };
};

[[nodiscard]] std::optional<RegionFrontierBackendTestingSnapshot>
region_frontier_backend_testing_snapshot(
    const runtime::simir::RegionFrontierBackend& backend) noexcept;

} // namespace fsim::app::application_detail
