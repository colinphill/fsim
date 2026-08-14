// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_path_timing.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfAppliedPulseTiming {
    elaboration::VerilogSpecifyPathId path_id { };
    frontend::SdfConstructKind pulse_construct_kind {
        frontend::SdfConstructKind::Unknown
    };
    bool global_annotation { };
    std::vector<runtime::SimulationTick> source_reject_delays;
    std::vector<runtime::SimulationTick> source_error_delays;
    std::vector<runtime::SimulationTick> source_retain_delays;
    std::vector<runtime::SimulationTick> effective_reject_delays;
    std::vector<runtime::SimulationTick> effective_error_delays;
    std::vector<runtime::SimulationTick> effective_retain_delays;
    std::vector<SdfSelectedPercentage> selected_percentages;
    elaboration::VerilogSpecifyPathInfo effective_path;
    std::vector<frontend::SourceSpan> annotation_sources;
    std::vector<std::string> annotation_identities;
    std::string canonical_identity;

    friend bool operator==(const SdfAppliedPulseTiming&,
        const SdfAppliedPulseTiming&) = delete;
};

class SdfPulseTimingApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationPlan>& plan() const
        noexcept;
    [[nodiscard]] std::span<const SdfAppliedPulseTiming> paths() const noexcept;
    [[nodiscard]] const SdfAppliedPulseTiming* find_path(
        elaboration::VerilogSpecifyPathId id) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfPulseTimingApplication(std::shared_ptr<const SdfAnnotationPlan> plan,
        std::vector<SdfAppliedPulseTiming> paths,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfAnnotationPlan> plan_;
    std::vector<SdfAppliedPulseTiming> paths_;
    std::string semantic_identity_;
};

struct SdfPulseTimingLimits {
    std::size_t max_paths { 1'000'000U };
    std::size_t max_values_per_path { 12U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfPulseTimingResult {
    std::shared_ptr<const SdfPulseTimingApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfPulseTimingResult apply_sdf_pulse_timing(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    SdfPulseTimingLimits limits = { });

} // namespace fsim::app
