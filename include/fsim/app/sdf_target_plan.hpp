// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_mapping_validation.hpp"
#include "fsim/app/sdf_value_policy.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfTimingTargetKind {
    SpecifyPath,
    Interconnect,
    Port,
    Mipd,
    Device,
    TimingCheck,
    Pulse,
    TimingEnvironment,
};

enum class SdfDelayApplicationMode {
    None,
    Absolute,
    Increment,
};

struct SdfPlannedAnnotation {
    std::uint64_t node_id { };
    std::uint64_t cell_id { };
    frontend::SdfConstructKind construct_kind {
        frontend::SdfConstructKind::Unknown
    };
    SdfTimingTargetKind target_kind { SdfTimingTargetKind::SpecifyPath };
    SdfDelayApplicationMode delay_mode { SdfDelayApplicationMode::None };
    std::string target_instance_path;
    std::string target_identity;
    std::string condition_identity;
    std::vector<std::string> edge_identities;
    std::vector<SdfResolvedEndpoint> endpoints;
    std::vector<runtime::simir::SignalId> endpoint_signals;
    std::vector<std::uint64_t> before_ticks;
    std::vector<std::uint64_t> after_ticks;
    std::vector<std::int64_t> before_check_ticks;
    std::vector<std::int64_t> after_check_ticks;
    std::vector<std::uint64_t> retain_ticks;
    std::vector<SdfSelectedPercentage> after_percentages;
    frontend::SourceSpan source;
    std::string source_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfPlannedAnnotation&,
        const SdfPlannedAnnotation&) = default;
};

class SdfAnnotationPlan final {
public:
    static constexpr std::uint32_t schema_version = 4U;

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationSummary>& summary()
        const noexcept;
    [[nodiscard]] const SdfValuePolicy& value_policy() const noexcept;
    [[nodiscard]] std::span<const SdfPlannedAnnotation> annotations() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfAnnotationPlan(std::shared_ptr<const SdfAnnotationSummary> summary,
        SdfValuePolicy value_policy,
        std::vector<SdfPlannedAnnotation> annotations,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfAnnotationSummary> summary_;
    SdfValuePolicy value_policy_;
    std::vector<SdfPlannedAnnotation> annotations_;
    std::string semantic_identity_;
};

struct SdfTargetPlanLimits {
    std::size_t max_annotations { 1'000'000U };
    std::size_t max_values_per_annotation { 12U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfTargetPlanResult {
    std::shared_ptr<const SdfAnnotationPlan> plan;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfTargetPlanResult build_sdf_annotation_plan(
    std::shared_ptr<const SdfAnnotationSummary> summary,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfValuePolicy& value_policy,
    SdfTargetPlanLimits limits = { });

} // namespace fsim::app
