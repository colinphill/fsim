// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_annotation_scope.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfResolvedInstanceKind {
    Hdl,
    SystemC,
};

struct SdfResolvedInstance {
    SdfResolvedInstanceKind kind { SdfResolvedInstanceKind::Hdl };
    std::uint64_t declaration_id { };
    std::string root_alias;
    std::string instance_path;
    std::string unit_identity;
    std::string cell_type;
    SdfScopeRootLanguage language { SdfScopeRootLanguage::SystemVerilog };
    SdfHierarchyCasePolicy case_policy { SdfHierarchyCasePolicy::Sensitive };
    bool physical_primitive { };

    friend bool operator==(const SdfResolvedInstance&,
        const SdfResolvedInstance&) = default;
};

struct SdfResolvedCell {
    std::uint64_t cell_id { };
    std::string source_identity;
    frontend::SdfInstanceSelectorKind selector {
        frontend::SdfInstanceSelectorKind::Empty
    };
    std::vector<SdfResolvedInstance> targets;

    friend bool operator==(const SdfResolvedCell&,
        const SdfResolvedCell&) = default;
};

class SdfCellResolution final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationScope>& scope() const
        noexcept;
    [[nodiscard]] std::span<const SdfResolvedCell> cells() const noexcept;
    [[nodiscard]] const SdfResolvedCell* find_cell(
        std::uint64_t cell_id) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfCellResolution(std::shared_ptr<const SdfAnnotationScope> scope,
        std::vector<SdfResolvedCell> cells, std::string semantic_identity);

private:
    std::shared_ptr<const SdfAnnotationScope> scope_;
    std::vector<SdfResolvedCell> cells_;
    std::string semantic_identity_;
};

struct SdfCellResolutionLimits {
    std::size_t max_candidates { 1'000'000U };
    std::size_t max_matches { 1'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
    std::size_t max_reported_candidates { 8U };
};

struct SdfCellResolutionResult {
    std::shared_ptr<const SdfCellResolution> resolution;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfCellResolutionResult resolve_sdf_cells(
    std::shared_ptr<const SdfAnnotationScope> scope,
    const elaboration::ElaboratedDesign& elaborated,
    SdfCellResolutionLimits limits = { });

} // namespace fsim::app
