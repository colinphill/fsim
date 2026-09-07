// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

inline constexpr std::string_view kCoverageSourceControlDiagnostic
    = "FSIM-COV-041";

// Values deliberately follow the unified database metric-family identities.
// All is source-control-only and never becomes a stored metric family.
enum class CoverageSourceMetric : std::uint8_t {
    All = 0U,
    Statement = 1U,
    Branch = 2U,
    Line = 3U,
    Condition = 4U,
    Expression = 5U,
    Toggle = 6U,
    FsmState = 7U,
    FsmTransition = 8U,
    SystemVerilogCoverpoint = 9U,
    SystemVerilogCross = 10U,
    PslDirective = 11U,
    PslProperty = 12U,
};

enum class CoverageSourceControlAction : std::uint8_t {
    Off,
    On,
};

struct CoverageSourceDirective {
    CoverageSourceControlAction action { CoverageSourceControlAction::Off };
    CoverageSourceMetric metric { CoverageSourceMetric::All };
    std::string reason;
    std::size_t offset { };
    std::uint64_t line { };

    friend bool operator==(const CoverageSourceDirective&,
        const CoverageSourceDirective&)
        = default;
};

struct CoverageSourceExclusion {
    CoverageSourceMetric metric { CoverageSourceMetric::All };
    std::size_t begin_offset { };
    std::size_t end_offset { };
    std::string reason;
    std::uint64_t off_line { };
    // Zero denotes an off region that intentionally extends to end-of-file.
    std::uint64_t on_line { };

    friend bool operator==(const CoverageSourceExclusion&,
        const CoverageSourceExclusion&)
        = default;
};

struct CoverageSourceControlLimits {
    std::size_t maximum_source_bytes { 1U << 30U };
    std::size_t maximum_line_bytes { 1U << 20U };
    std::size_t maximum_directives { 1U << 16U };
    std::size_t maximum_reason_bytes { 1U << 12U };
    std::size_t maximum_total_reason_bytes { 1U << 20U };
};

enum class CoverageSourceControlError : std::uint8_t {
    None,
    InvalidLanguage,
    ResourceLimit,
    MalformedDirective,
    UnknownMetric,
    MissingReason,
    UnexpectedReason,
    DuplicateOff,
    UnmatchedOn,
    ConflictingAllMetric,
};

struct CoverageSourceControlResult {
    std::vector<CoverageSourceDirective> directives;
    std::vector<CoverageSourceExclusion> exclusions;
    CoverageSourceControlError error { CoverageSourceControlError::None };
    std::size_t error_offset { };
    std::uint64_t error_line { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageSourceControlError::None;
    }

    friend bool operator==(const CoverageSourceControlResult&,
        const CoverageSourceControlResult&)
        = default;
};

// Recognized line-comment forms are:
//   // fsim coverage off metric=statement reason="generated glue"
//   // fsim coverage on metric=statement
// and the corresponding VHDL `--` spelling. Off regions may extend to EOF.
[[nodiscard]] CoverageSourceControlResult parse_coverage_source_controls(
    std::string_view source,
    Language language,
    CoverageSourceControlLimits limits = { }) noexcept;

[[nodiscard]] const CoverageSourceExclusion*
coverage_source_exclusion_at(
    std::span<const CoverageSourceExclusion> exclusions,
    CoverageSourceMetric metric,
    std::size_t source_offset) noexcept;

[[nodiscard]] std::string_view coverage_source_metric_name(
    CoverageSourceMetric metric) noexcept;

[[nodiscard]] std::optional<CoverageSourceMetric>
coverage_source_metric_from_name(std::string_view name) noexcept;

} // namespace fsim::frontend
