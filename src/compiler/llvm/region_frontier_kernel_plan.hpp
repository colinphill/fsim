// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "region_frontier_codegen_v2.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace fsim::compiler {

/// Optional diagnostic details for the first sensitivity rejected by the
/// planner. This is observational only and is not part of the plan identity.
struct RegionFrontierRejectedSensitivity {
    bool present { };
    std::uint32_t member_process { };
    std::size_t member_index { };
    std::size_t sensitivity_index { };
    std::uint32_t signal { };
    std::uint32_t edge { };
    std::uint32_t offset { };
    std::uint32_t width { };
    bool matching_input_present { };
    std::uint32_t matching_input_width { };
    std::uint32_t matching_input_kind { };
    bool matching_input_internal { };
};

class RegionFrontierKernelPlan final {
public:
    /// Optional diagnostic sink for the source line that rejects a plan. It
    /// is never part of plan/cache identity.
    [[nodiscard]] static std::optional<RegionFrontierKernelPlan> try_create(
        const runtime::simir::RegionConeActivationKernel& kernel,
        std::uint32_t* rejection_line = nullptr,
        RegionFrontierRejectedSensitivity* rejected_sensitivity = nullptr);

    ~RegionFrontierKernelPlan();
    RegionFrontierKernelPlan(RegionFrontierKernelPlan&&) noexcept;
    RegionFrontierKernelPlan& operator=(RegionFrontierKernelPlan&&) noexcept;
    RegionFrontierKernelPlan(const RegionFrontierKernelPlan&) = delete;
    RegionFrontierKernelPlan& operator=(const RegionFrontierKernelPlan&) = delete;

    [[nodiscard]] const runtime::simir::RegionFrontierLayoutV2&
    layout() const noexcept;

    [[nodiscard]] std::span<const
        runtime::simir::scratch::RegionFrontierFanoutRangeSpan>
    fanout_range_spans() const noexcept;

    [[nodiscard]] std::span<const
        runtime::simir::scratch::RegionFrontierFanoutSensitivityRange>
    fanout_sensitivity_ranges() const noexcept;

    [[nodiscard]] std::string_view cache_identity() const noexcept;

    /// Certified identity for the emitted shared body. Unlike the physical
    /// cache identity, this excludes only plan-bound generations and IDs after
    /// verifying that all remaining emitted structure is identical.
    [[nodiscard]] std::optional<std::string_view>
    shared_body_identity() const noexcept;

    /// Census-only structural digest with ProcessId and SignalId references
    /// replaced by plan-local ordinals. It is not a cache key or proof that
    /// generated code can be shared; physical cache identity remains exact.
    /// Ordinals preserve member and signal-slot order, so renames that reorder
    /// those source tables may conservatively produce a different digest.
    [[nodiscard]] std::optional<std::string_view>
    structural_census_identity() const noexcept;

    [[nodiscard]] llvm::Function* emit_step(
        llvm::Module& module, std::string_view symbol,
        const runtime::simir::scratch::EmitCertifiedInternalCommitV2&
            emit_internal_commit) const;

    [[nodiscard]] llvm::Function* emit_shared_body(
        llvm::Module& module, std::string_view symbol,
        const runtime::simir::scratch::EmitCertifiedInternalCommitV2&
            emit_internal_commit) const;

private:
    struct Impl;
    explicit RegionFrontierKernelPlan(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

} // namespace fsim::compiler
