// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/elaboration/verilog_coverage_points.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageToggleSelectionSchema
    = "fsim-coverage-toggle-selection-v3";
inline constexpr std::string_view kCoverageToggleSelectionDiagnostic
    = "FSIM-COV-023";

enum class CoverageToggleExclusionReason : std::uint8_t {
    AutomaticLocal = 1U,
    ProceduralLocal = 2U,
    Memory = 3U,
    Array = 4U,
};

enum class CoverageToggleExcludedContainerKind : std::uint8_t {
    None,
    StaticArray,
    DynamicArray,
    Queue,
    AssociativeArray,
    VhdlArray,
};

struct CoverageToggleExcludedDimension {
    std::int64_t left { };
    std::int64_t right { };
    bool concrete { };

    friend bool operator==(const CoverageToggleExcludedDimension&,
        const CoverageToggleExcludedDimension&)
        = default;
};

struct CoverageToggleExcludedShape {
    CoverageToggleExcludedContainerKind kind {
        CoverageToggleExcludedContainerKind::None
    };
    std::vector<CoverageToggleExcludedDimension> dimensions;
    std::size_t element_width { };
    bool string_index { };

    friend bool operator==(const CoverageToggleExcludedShape&,
        const CoverageToggleExcludedShape&)
        = default;
};

struct CoverageToggleExclusion {
    runtime::CodeCoveragePointId source_point;
    runtime::CodeCoveragePointId id;
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    frontend::CodeCoverageLanguage language {
        frontend::CodeCoverageLanguage::SystemVerilog
    };
    std::string hierarchy_path;
    std::vector<CoverageToggleExclusionReason> reasons;
    CoverageToggleExcludedShape shape;
    std::size_t source_index { };
    frontend::CodeCoverageSourceSpan span;
    std::uint64_t line { };

    friend bool operator==(
        const CoverageToggleExclusion&, const CoverageToggleExclusion&)
        = default;
};

struct CoverageToggleSelection {
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    std::string instance;
    std::vector<CoverageToggleExclusion> exclusions;

    friend bool operator==(
        const CoverageToggleSelection&, const CoverageToggleSelection&)
        = default;
};

struct CoverageToggleSelectionLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_declarations { 1U << 20U };
    std::size_t maximum_exclusions { 1U << 20U };
    std::size_t maximum_reasons { 1U << 21U };
    std::size_t maximum_name_bytes { 1U << 16U };
    std::size_t maximum_hierarchy_bytes { 1U << 20U };
    std::size_t maximum_scope_depth { 64U };
    std::uint64_t maximum_line_number { 1ULL << 31U };
    CoverageInstanceIdentityLimits instance_identity;
};

enum class CoverageToggleSelectionError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidLanguage,
    InvalidUnitKind,
    InstanceOwnerMismatch,
    InvalidInstanceIdentity,
    EmptySourceName,
    DuplicateSourceName,
    DuplicateSourceIdentity,
    InvalidSourceIdentity,
    EmptyObjectName,
    InvalidObjectName,
    UnknownObjectSource,
    ObjectSourceOwnershipMismatch,
    InvalidObjectSpan,
    InvalidObjectLine,
    DuplicateObjectPath,
    DuplicateExclusionIdentity,
};

struct CoverageToggleSelectionResult {
    std::optional<CoverageToggleSelection> selection;
    CoverageToggleSelectionError error {
        CoverageToggleSelectionError::None
    };
    std::size_t declaration_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return selection.has_value()
            && error == CoverageToggleSelectionError::None;
    }
};

[[nodiscard]] constexpr std::string_view
coverage_toggle_exclusion_reason_name(
    const CoverageToggleExclusionReason reason) noexcept
{
    switch (reason) {
    case CoverageToggleExclusionReason::AutomaticLocal:
        return "automatic-local";
    case CoverageToggleExclusionReason::ProceduralLocal:
        return "procedural-local";
    case CoverageToggleExclusionReason::Memory:
        return "memory";
    case CoverageToggleExclusionReason::Array:
        return "array";
    }
    return "unknown";
}

} // namespace fsim::elaboration
