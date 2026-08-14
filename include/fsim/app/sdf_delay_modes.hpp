// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_target_plan.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfTransitionClass : std::size_t {
    ZeroToOne,
    OneToZero,
    ZeroToZ,
    ZToOne,
    OneToZ,
    ZToZero,
    ZeroToX,
    XToOne,
    OneToX,
    XToZero,
    XToZ,
    ZToX,
};

using SdfTransitionDelayProfile = std::array<runtime::SimulationTick, 12U>;

[[nodiscard]] std::optional<SdfTransitionDelayProfile>
expand_sdf_transition_delays(std::span<const std::uint64_t> values) noexcept;

struct SdfAppliedDelayModeStep {
    std::size_t plan_index { };
    std::uint64_t node_id { };
    SdfTimingTargetKind target_kind { SdfTimingTargetKind::SpecifyPath };
    SdfDelayApplicationMode mode { SdfDelayApplicationMode::None };
    std::string target_identity;
    std::vector<runtime::SimulationTick> source_values;
    SdfTransitionDelayProfile before_profile { };
    SdfTransitionDelayProfile effective_profile { };
    frontend::SourceSpan annotation_source;
    std::string annotation_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfAppliedDelayModeStep&,
        const SdfAppliedDelayModeStep&) = default;
};

class SdfDelayModeApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] std::span<const std::shared_ptr<const SdfAnnotationPlan>>
    plans() const noexcept;
    [[nodiscard]] std::span<const SdfAppliedDelayModeStep> steps() const
        noexcept;
    [[nodiscard]] const SdfAppliedDelayModeStep* find_final(
        SdfTimingTargetKind kind, std::string_view identity) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfDelayModeApplication(
        std::vector<std::shared_ptr<const SdfAnnotationPlan>> plans,
        std::vector<SdfAppliedDelayModeStep> steps,
        std::string semantic_identity);

private:
    std::vector<std::shared_ptr<const SdfAnnotationPlan>> plans_;
    std::vector<SdfAppliedDelayModeStep> steps_;
    std::string semantic_identity_;
};

struct SdfDelayModeLimits {
    std::size_t max_plans { 1'000'000U };
    std::size_t max_steps { 1'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfDelayModeResult {
    std::shared_ptr<const SdfDelayModeApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfDelayModeResult apply_sdf_delay_modes(
    std::span<const std::shared_ptr<const SdfAnnotationPlan>> ordered_plans,
    SdfDelayModeLimits limits = { });

} // namespace fsim::app
