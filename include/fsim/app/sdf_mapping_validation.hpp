// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_endpoint_resolution.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfAnnotationTargetSummary {
    std::uint64_t cell_id { };
    std::string target_instance_path;
    SdfScopeRootLanguage language { SdfScopeRootLanguage::SystemVerilog };
    std::size_t annotation_count { };
    std::size_t endpoint_count { };
    std::vector<runtime::simir::SignalId> signals;

    friend bool operator==(const SdfAnnotationTargetSummary&,
        const SdfAnnotationTargetSummary&) = default;
};

class SdfAnnotationSummary final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfEndpointResolution>&
    endpoint_resolution() const noexcept;
    [[nodiscard]] std::span<const SdfAnnotationTargetSummary> targets() const
        noexcept;
    [[nodiscard]] std::size_t annotation_count() const noexcept;
    [[nodiscard]] std::size_t endpoint_count() const noexcept;
    [[nodiscard]] std::size_t delay_annotation_count() const noexcept;
    [[nodiscard]] std::size_t timing_check_annotation_count() const noexcept;
    [[nodiscard]] std::size_t timing_environment_annotation_count() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfAnnotationSummary(
        std::shared_ptr<const SdfEndpointResolution> endpoint_resolution,
        std::vector<SdfAnnotationTargetSummary> targets,
        std::size_t annotation_count, std::size_t endpoint_count,
        std::size_t delay_annotation_count,
        std::size_t timing_check_annotation_count,
        std::size_t timing_environment_annotation_count,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfEndpointResolution> endpoint_resolution_;
    std::vector<SdfAnnotationTargetSummary> targets_;
    std::size_t annotation_count_ { };
    std::size_t endpoint_count_ { };
    std::size_t delay_annotation_count_ { };
    std::size_t timing_check_annotation_count_ { };
    std::size_t timing_environment_annotation_count_ { };
    std::string semantic_identity_;
};

struct SdfMappingValidationLimits {
    std::size_t max_mappings { 1'000'000U };
    std::size_t max_endpoints { 1'000'000U };
    std::size_t max_targets { 1'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfMappingValidationResult {
    std::shared_ptr<const SdfAnnotationSummary> summary;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfMappingValidationResult validate_sdf_mapping(
    std::shared_ptr<const SdfEndpointResolution> endpoint_resolution,
    const elaboration::ElaboratedDesign& elaborated,
    SdfMappingValidationLimits limits = { });

} // namespace fsim::app
