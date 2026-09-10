// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::string_view kCodeCoverageModelDiagnostic = "FSIM-COV-001";

enum class CodeCoverageMetric : std::uint8_t {
    Statement = 1U,
    Branch = 2U,
    Line = 3U,
};

enum class CodeCoverageStatus : std::uint8_t {
    Empty,
    Uncovered,
    Partial,
    Covered,
    Excluded,
};

struct CodeCoveragePointId {
    std::uint64_t high { };
    std::uint64_t low { };

    friend constexpr bool operator==(
        const CodeCoveragePointId&, const CodeCoveragePointId&)
        = default;
};

struct CodeCoverageRunId {
    std::uint64_t high { };
    std::uint64_t low { };

    friend constexpr bool operator==(
        const CodeCoverageRunId&, const CodeCoverageRunId&)
        = default;
};

struct CodeCoverageCounterId {
    std::uint32_t value { };

    friend constexpr bool operator==(
        const CodeCoverageCounterId&, const CodeCoverageCounterId&)
        = default;
};

struct CodeCoveragePoint {
    CodeCoveragePointId id;
    CodeCoverageMetric metric { CodeCoverageMetric::Statement };
    CodeCoverageCounterId counter;
};

struct CodeCoverageRun {
    CodeCoverageRunId id;
    std::vector<CodeCoveragePoint> points;
};

struct CodeCoveragePointResult {
    CodeCoveragePointId point;
    CodeCoverageMetric metric { CodeCoverageMetric::Statement };
    CodeCoverageCounterId counter;
    std::uint64_t hits { };
    CodeCoverageStatus status { CodeCoverageStatus::Uncovered };
};

struct CodeCoverageMetricResult {
    CodeCoverageMetric metric { CodeCoverageMetric::Statement };
    std::uint64_t total { };
    std::uint64_t covered { };
    std::uint64_t partial { };
    std::uint64_t uncovered { };
    std::uint64_t excluded { };
    CodeCoverageStatus status { CodeCoverageStatus::Empty };
};

struct CodeCoverageResult {
    CodeCoverageRunId run;
    std::vector<CodeCoveragePointResult> points;
    std::vector<CodeCoverageMetricResult> metrics;
};

struct CodeCoverageModelLimits {
    std::size_t maximum_points { 1U << 20U };
    std::size_t maximum_metric_results { 3U };
};

enum class CodeCoverageModelError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidRunIdentity,
    ResultRunMismatch,
    ResultCountMismatch,
    InvalidPointIdentity,
    InvalidMetric,
    CounterOwnershipMismatch,
    PointResultMismatch,
    InvalidPointStatus,
    InvalidMetricCounts,
    InvalidMetricStatus,
    DuplicateMetricResult,
    MissingMetricResult,
    MetricResultMismatch,
};

struct CodeCoverageValidationResult {
    CodeCoverageModelError error { CodeCoverageModelError::None };
    std::size_t index { };

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return error == CodeCoverageModelError::None;
    }
};

[[nodiscard]] constexpr bool is_code_coverage_identity_valid(
    const CodeCoveragePointId id) noexcept
{
    return id.high != 0U || id.low != 0U;
}

[[nodiscard]] constexpr bool is_code_coverage_identity_valid(
    const CodeCoverageRunId id) noexcept
{
    return id.high != 0U || id.low != 0U;
}

[[nodiscard]] constexpr std::string_view code_coverage_metric_name(
    const CodeCoverageMetric metric) noexcept
{
    switch (metric) {
    case CodeCoverageMetric::Statement:
        return "statement";
    case CodeCoverageMetric::Branch:
        return "branch";
    case CodeCoverageMetric::Line:
        return "line";
    }
    return { };
}

[[nodiscard]] constexpr bool is_code_coverage_point_metric(
    const CodeCoverageMetric metric) noexcept
{
    return metric == CodeCoverageMetric::Statement
        || metric == CodeCoverageMetric::Branch;
}

[[nodiscard]] constexpr bool is_code_coverage_status_valid(
    const CodeCoverageStatus status) noexcept
{
    switch (status) {
    case CodeCoverageStatus::Empty:
    case CodeCoverageStatus::Uncovered:
    case CodeCoverageStatus::Partial:
    case CodeCoverageStatus::Covered:
    case CodeCoverageStatus::Excluded:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr CodeCoverageStatus code_coverage_metric_status(
    const CodeCoverageMetricResult& result) noexcept
{
    if (result.total == 0U) {
        return CodeCoverageStatus::Empty;
    }
    if (result.excluded == result.total) {
        return CodeCoverageStatus::Excluded;
    }
    const auto scored = result.total - result.excluded;
    if (result.covered == scored) {
        return CodeCoverageStatus::Covered;
    }
    if (result.covered == 0U && result.partial == 0U) {
        return CodeCoverageStatus::Uncovered;
    }
    return CodeCoverageStatus::Partial;
}

[[nodiscard]] CodeCoverageValidationResult validate_code_coverage(
    const CodeCoverageRun& run,
    const CodeCoverageResult& result,
    CodeCoverageModelLimits limits = { }) noexcept;

} // namespace fsim::runtime
