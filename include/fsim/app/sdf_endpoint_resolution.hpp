// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_cell_resolution.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfEndpointRole {
    Input,
    Output,
    InterconnectSource,
    InterconnectDestination,
    Net,
    Device,
    TimingReference,
    TimingData,
    Condition,
};

enum class SdfEndpointObjectKind {
    HdlPort,
    HdlNet,
    SystemCPort,
    SystemCSignal,
};

struct SdfEndpointSelect {
    std::int64_t left { };
    std::int64_t right { };
    std::size_t width { 1U };

    friend bool operator==(const SdfEndpointSelect&,
        const SdfEndpointSelect&) = default;
};

struct SdfResolvedEndpoint {
    SdfEndpointRole role { SdfEndpointRole::Input };
    SdfEndpointObjectKind object_kind { SdfEndpointObjectKind::HdlNet };
    std::string instance_path;
    std::string object_path;
    runtime::simir::SignalId signal { };
    std::size_t object_width { };
    std::optional<SdfEndpointSelect> select;
    SdfScopeRootLanguage language { SdfScopeRootLanguage::SystemVerilog };
    frontend::PortDirection direction { frontend::PortDirection::Unknown };
    std::optional<elaboration::BoundaryConversionKind> conversion;
    std::optional<runtime::simir::SignalId> conversion_peer;
    std::string edge_identity;
    std::string condition_identity;

    friend bool operator==(const SdfResolvedEndpoint&,
        const SdfResolvedEndpoint&) = default;
};

struct SdfResolvedNodeEndpoints {
    std::uint64_t node_id { };
    std::uint64_t cell_id { };
    frontend::SdfConstructKind construct_kind {
        frontend::SdfConstructKind::Unknown
    };
    std::string target_instance_path;
    std::vector<SdfResolvedEndpoint> endpoints;
    std::optional<elaboration::VerilogSpecifyPathId> specify_path;
    std::optional<std::uint32_t> timing_check;
    std::string condition_identity;

    friend bool operator==(const SdfResolvedNodeEndpoints&,
        const SdfResolvedNodeEndpoints&) = default;
};

class SdfEndpointResolution final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfCellResolution>& cells() const
        noexcept;
    [[nodiscard]] std::span<const SdfResolvedNodeEndpoints> nodes() const
        noexcept;
    [[nodiscard]] std::span<const SdfResolvedNodeEndpoints> find_node(
        std::uint64_t node_id) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfEndpointResolution(std::shared_ptr<const SdfCellResolution> cells,
        std::vector<SdfResolvedNodeEndpoints> nodes,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfCellResolution> cells_;
    std::vector<SdfResolvedNodeEndpoints> nodes_;
    std::string semantic_identity_;
};

struct SdfEndpointResolutionLimits {
    std::size_t max_nodes { 1'000'000U };
    std::size_t max_candidates { 1'000'000U };
    std::size_t max_endpoints { 1'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
    std::size_t max_reported_candidates { 8U };
};

struct SdfEndpointResolutionResult {
    std::shared_ptr<const SdfEndpointResolution> resolution;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfEndpointResolutionResult resolve_sdf_endpoints(
    std::shared_ptr<const SdfCellResolution> cells,
    const elaboration::ElaboratedDesign& elaborated,
    SdfEndpointResolutionLimits limits = { });

} // namespace fsim::app
