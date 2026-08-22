// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/code_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::string_view kCodeCoverageLineStateDiagnostic
    = "FSIM-COV-007";

// One executable statement point contributes to the physical source line on
// which the statement begins. The point and counter remain owned by the
// statement metric; derived lines never acquire counters of their own.
struct CodeCoverageStatementLineSite {
    CodeCoveragePointId point;
    CodeCoverageCounterId counter;
    std::size_t source_index { };
    std::uint64_t line { };
};

struct CodeCoverageLineState {
    std::size_t source_index { };
    std::uint64_t line { };
    std::uint64_t total { };
    std::uint64_t covered { };
    std::uint64_t uncovered { };
    std::uint64_t excluded { };
    CodeCoverageStatus status { CodeCoverageStatus::Uncovered };

    friend bool operator==(const CodeCoverageLineState&,
        const CodeCoverageLineState&)
        = default;
};

struct CodeCoverageLineStateLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_statement_points { 1U << 20U };
    std::size_t maximum_lines { 1U << 20U };
    std::uint64_t maximum_line_number { 1ULL << 31U };
};

enum class CodeCoverageLineStateError : std::uint8_t {
    None,
    ResourceLimit,
    ResultCountMismatch,
    InvalidPointIdentity,
    DuplicatePoint,
    InvalidLineNumber,
    PointResultMismatch,
    InvalidPointStatus,
};

struct CodeCoverageLineStateResult {
    std::vector<CodeCoverageLineState> lines;
    CodeCoverageMetricResult metric {
        .metric = CodeCoverageMetric::Line,
    };
    CodeCoverageLineStateError error { CodeCoverageLineStateError::None };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CodeCoverageLineStateError::None;
    }
};

[[nodiscard]] CodeCoverageLineStateResult derive_code_coverage_line_states(
    std::span<const CodeCoverageStatementLineSite> statement_sites,
    std::span<const CodeCoveragePointResult> statement_results,
    CodeCoverageLineStateLimits limits = { }) noexcept;

} // namespace fsim::runtime
