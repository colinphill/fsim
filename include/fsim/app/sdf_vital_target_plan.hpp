// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_mapping_validation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfVitalPortBinding {
    SdfEndpointRole role { SdfEndpointRole::Input };
    SdfEndpointObjectKind object_kind { SdfEndpointObjectKind::HdlPort };
    std::string object_path;
    runtime::simir::SignalId signal { };
    std::size_t width { };
    std::optional<SdfEndpointSelect> select;
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    frontend::ValueDomain value_domain { frontend::ValueDomain::Unknown };
    runtime::simir::ResolutionKind resolution {
        runtime::simir::ResolutionKind::none
    };
    std::string type_name;
    std::string edge_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfVitalPortBinding&,
        const SdfVitalPortBinding&) = default;
};

struct SdfVitalGenericBinding {
    std::string name;
    std::string value;
    std::string semantic_identity;

    friend bool operator==(const SdfVitalGenericBinding&,
        const SdfVitalGenericBinding&) = default;
};

struct SdfVitalPlannedTarget {
    std::uint64_t node_id { };
    std::uint64_t cell_id { };
    frontend::SdfConstructKind construct_kind {
        frontend::SdfConstructKind::Unknown
    };
    std::string instance_path;
    std::string unit_identity;
    std::string specialization_unit;
    std::string library;
    std::string condition_identity;
    frontend::Language language { frontend::Language::Vhdl2008 };
    std::vector<SdfVitalPortBinding> ports;
    std::vector<SdfVitalGenericBinding> generics;
    frontend::SourceSpan source;
    std::string source_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfVitalPlannedTarget&,
        const SdfVitalPlannedTarget&) = default;
};

class SdfVitalTargetPlan final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalTargetPlan(std::shared_ptr<const SdfAnnotationSummary> summary,
        std::vector<SdfVitalPlannedTarget> targets,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationSummary>& summary()
        const noexcept;
    [[nodiscard]] std::span<const SdfVitalPlannedTarget> targets() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfAnnotationSummary> summary_;
    std::vector<SdfVitalPlannedTarget> targets_;
    std::string semantic_identity_;
};

struct SdfVitalTargetPlanLimits {
    std::size_t max_targets { 1'000'000U };
    std::size_t max_ports_per_target { 4096U };
    std::size_t max_generics_per_target { 4096U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfVitalTargetPlanResult {
    std::shared_ptr<const SdfVitalTargetPlan> plan;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalTargetPlanResult build_sdf_vital_target_plan(
    std::shared_ptr<const SdfAnnotationSummary> summary,
    const elaboration::ElaboratedDesign& elaborated,
    SdfVitalTargetPlanLimits limits = { });

} // namespace fsim::app
