// SPDX-License-Identifier: Apache-2.0
// The executor retains the JIT that owns the generated entry.
#pragma once

#include "fsim/compiler/llvm_jit.hpp"
#include "fsim/runtime/simir_region_activation.hpp"
#include "fsim/runtime/simir_region_frontier_v2.hpp"

#include <memory>
#include <string_view>

namespace fsim::compiler {

namespace llvm_detail {
struct RegionFrontierTestAccess;
struct RegionFrontierPrivateAccess;
}

/// Compiled typed event-prefix entry plus immutable Logic4/Logic9 frame layout.
/// Member bodies are lowered from RegionConeActivationKernel programs; the
/// runtime owns one mutable RegionFrontierFrameV2 per certified instance.
class LlvmRegionFrontierExecutor final {
public:
    /// Build and own the typed plan without emitting or materializing LLVM
    /// code. A null result is a conservative planner/option decline.
    [[nodiscard]] static std::unique_ptr<LlvmPreparedRegionFrontier>
    prepare(
        const runtime::simir::RegionConeActivationKernel& kernel,
        LlvmJitOptions options = {},
        std::string_view immutable_design_identity = {});

    /// Consume one prepared plan and perform the existing JIT/cache pipeline.
    /// A moved-from or already-consumed plan returns null.
    [[nodiscard]] static std::unique_ptr<LlvmRegionFrontierExecutor>
    materialize(LlvmPreparedRegionFrontier&& prepared);

    [[nodiscard]] static std::unique_ptr<LlvmRegionFrontierExecutor>
    try_create(
        const runtime::simir::RegionConeActivationKernel& kernel,
        LlvmJitOptions options = {},
        std::string_view immutable_design_identity = {});

    ~LlvmRegionFrontierExecutor();
    LlvmRegionFrontierExecutor(LlvmRegionFrontierExecutor&&) noexcept;
    LlvmRegionFrontierExecutor& operator=(LlvmRegionFrontierExecutor&&) noexcept;
    LlvmRegionFrontierExecutor(const LlvmRegionFrontierExecutor&) = delete;
    LlvmRegionFrontierExecutor& operator=(
        const LlvmRegionFrontierExecutor&) = delete;

    [[nodiscard]] runtime::simir::RegionFrontierStepEntryV2
    step_entry() const noexcept;

    /// Descriptor arrays remain valid for this executor's lifetime. Per-instance
    /// frame and signal-plane storage belongs to the runtime adapter.
    [[nodiscard]] const runtime::simir::RegionFrontierLayoutV2&
    layout() const noexcept;

    /// Cache activity caused by this materialization only. A layer reused
    /// from an in-memory native owner reports zero; registry reuse is exposed
    /// separately through source-private test access and is not a disk hit.
    [[nodiscard]] LlvmJitCacheStatistics cache_statistics() const noexcept;

private:
    friend struct llvm_detail::RegionFrontierTestAccess;
    friend struct llvm_detail::RegionFrontierPrivateAccess;

    struct Impl;
    explicit LlvmRegionFrontierExecutor(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::compiler
