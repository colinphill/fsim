// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_inventory.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fsim::elaboration {

enum class CoveragePointExclusionKind : std::uint8_t {
    Declaration,
    StaticallyRemoved,
};

struct CoveragePointExclusion {
    std::size_t source_index { };
    frontend::CodeCoverageSourceSpan span;
    CoveragePointExclusionKind kind {
        CoveragePointExclusionKind::Declaration
    };

    friend bool operator==(
        const CoveragePointExclusion&, const CoveragePointExclusion&)
        = default;
};

struct CoveragePointExclusionLimits {
    std::size_t maximum_points { 1U << 20U };
    std::size_t maximum_exclusions { 1U << 20U };
};

enum class CoveragePointExclusionError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidSource,
    InvalidSourceIdentity,
    InvalidPointIdentity,
    InvalidMetric,
    InvalidPointSpan,
    InvalidLine,
    DuplicatePoint,
    InvalidExclusionKind,
    InvalidExclusionSpan,
    DuplicateExclusion,
};

struct CoveragePointExclusionResult {
    std::vector<CoverageInventoryPointDraft> points;
    CoveragePointExclusionError error {
        CoveragePointExclusionError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoveragePointExclusionError::None;
    }
};

// Apply already-resolved elaboration exclusions without regenerating point
// identities. A point is removed only when its complete source span belongs to
// an excluded declaration or statically removed construct; enclosing and
// neighboring executable points remain byte-for-byte unchanged.
[[nodiscard]] CoveragePointExclusionResult exclude_coverage_points(
    std::span<const CoverageInventorySource> sources,
    std::span<const CoverageInventoryPointDraft> points,
    std::span<const CoveragePointExclusion> exclusions,
    CoveragePointExclusionLimits limits = { }) noexcept;

} // namespace fsim::elaboration
