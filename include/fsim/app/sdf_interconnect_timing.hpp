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

struct SdfAppliedInterconnectTiming {
    SdfTimingTargetKind target_kind { SdfTimingTargetKind::Interconnect };
    SdfDelayApplicationMode mode { SdfDelayApplicationMode::None };
    std::string target_instance_path;
    std::string target_identity;
    std::vector<SdfResolvedEndpoint> endpoints;
    std::vector<runtime::simir::ProcessId> driver_processes;
    std::vector<runtime::SimulationTick> transition_delays;
    frontend::SourceSpan annotation_source;
    std::string annotation_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfAppliedInterconnectTiming&,
        const SdfAppliedInterconnectTiming&) = default;
};

class SdfInterconnectTimingApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationPlan>& plan() const
        noexcept;
    [[nodiscard]] std::span<const SdfAppliedInterconnectTiming> timings() const
        noexcept;
    [[nodiscard]] const SdfAppliedInterconnectTiming* find_target(
        std::string_view identity) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfInterconnectTimingApplication(
        std::shared_ptr<const SdfAnnotationPlan> plan,
        std::vector<SdfAppliedInterconnectTiming> timings,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfAnnotationPlan> plan_;
    std::vector<SdfAppliedInterconnectTiming> timings_;
    std::string semantic_identity_;
};

struct SdfInterconnectTimingLimits {
    std::size_t max_targets { 1'000'000U };
    std::size_t max_endpoints_per_target { 64U };
    std::size_t max_drivers_per_target { 1'000'000U };
    std::size_t max_values_per_target { 12U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfInterconnectTimingResult {
    std::shared_ptr<const SdfInterconnectTimingApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfInterconnectTimingResult apply_sdf_interconnect_timing(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    SdfInterconnectTimingLimits limits = { });

} // namespace fsim::app
