// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_target_plan.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfAppliedPathTiming {
    elaboration::VerilogSpecifyPathId path_id { };
    SdfDelayApplicationMode mode { SdfDelayApplicationMode::None };
    std::vector<runtime::SimulationTick> source_delays;
    std::vector<runtime::SimulationTick> effective_delays;
    elaboration::VerilogSpecifyPathInfo effective_path;
    frontend::SourceSpan annotation_source;
    std::string annotation_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfAppliedPathTiming&,
        const SdfAppliedPathTiming&) = delete;
};

class SdfPathTimingApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationPlan>& plan() const
        noexcept;
    [[nodiscard]] std::span<const SdfAppliedPathTiming> paths() const noexcept;
    [[nodiscard]] const SdfAppliedPathTiming* find_path(
        elaboration::VerilogSpecifyPathId id) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfPathTimingApplication(std::shared_ptr<const SdfAnnotationPlan> plan,
        std::vector<SdfAppliedPathTiming> paths,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfAnnotationPlan> plan_;
    std::vector<SdfAppliedPathTiming> paths_;
    std::string semantic_identity_;
};

struct SdfPathTimingLimits {
    std::size_t max_paths { 1'000'000U };
    std::size_t max_values_per_path { 12U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfPathTimingResult {
    std::shared_ptr<const SdfPathTimingApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfPathTimingResult apply_sdf_path_timing(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    SdfPathTimingLimits limits = { });

} // namespace fsim::app
