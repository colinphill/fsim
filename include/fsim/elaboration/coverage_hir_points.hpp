// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/frontend/coverage_source_control.hpp"
#include "fsim/semantic/compiled_design.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fsim::elaboration {

struct SpecializationInfo;

struct CoverageHirSource {
    CoverageInventorySource inventory;
    std::vector<frontend::CoverageSourceExclusion> exclusions;
};

struct CoverageHirContext {
    std::span<const CoverageHirSource> sources;
};

enum class CoverageHirPointError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidProcess,
    InvalidStatement,
    InvalidSource,
    InvalidSpan,
    MissingBranchArm,
    DuplicatePoint,
};

struct CoverageHirPointResult {
    CoverageInstanceInventoryDraft draft;
    CoverageHirPointError error { CoverageHirPointError::None };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageHirPointError::None;
    }
};

// Discover source-backed executable statements in selected SystemVerilog
// module processes. The inventory includes both conditional outcomes even
// when specialization later removes one path. Callable and synthetic bodies
// have separate ownership and are outside this module-process scope.
[[nodiscard]] CoverageHirPointResult discover_hir_module_coverage_points(
    const semantic::CompiledDesign& compiled,
    const SpecializationInfo& specialization,
    std::span<const CoverageHirSource> sources,
    std::size_t maximum_statements = 1U << 20U) noexcept;

} // namespace fsim::elaboration
