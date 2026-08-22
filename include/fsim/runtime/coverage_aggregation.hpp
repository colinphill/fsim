// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/code_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::string_view kCodeCoverageAggregationDiagnostic
    = "FSIM-COV-013";

struct CodeCoveragePointOwnership {
    CodeCoverageCounterId counter;
    std::uint32_t instance { };
    std::size_t source { };
};

struct CodeCoverageInstanceAggregate {
    std::uint32_t instance { };
    std::vector<CodeCoveragePointResult> points;
    std::vector<CodeCoverageMetricResult> metrics;
};

struct CodeCoverageSourcePointAggregate {
    CodeCoveragePointId point;
    CodeCoverageMetric metric { CodeCoverageMetric::Statement };
    std::uint64_t hits { };
    CodeCoverageStatus status { CodeCoverageStatus::Uncovered };
    std::uint64_t occurrences { };
    std::uint64_t covered_occurrences { };
    std::uint64_t uncovered_occurrences { };
    std::uint64_t excluded_occurrences { };
    bool hits_saturated { };
};

struct CodeCoverageSourceAggregate {
    std::size_t source { };
    std::vector<CodeCoverageSourcePointAggregate> points;
    std::vector<CodeCoverageMetricResult> metrics;
};

struct CodeCoverageAggregation {
    CodeCoverageRunId run;
    std::vector<CodeCoverageInstanceAggregate> instances;
    std::vector<CodeCoverageSourceAggregate> sources;
};

struct CodeCoverageAggregationLimits {
    std::size_t maximum_instances { 1U << 20U };
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_points { 1U << 20U };
    CodeCoverageModelLimits model;
};

enum class CodeCoverageAggregationError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidCoverageResult,
    OwnershipCountMismatch,
    NonCanonicalOwnership,
    UnknownInstance,
    UnknownSource,
    DuplicateInstancePoint,
    PointSourceConflict,
    PointMetricConflict,
};

struct CodeCoverageAggregationResult {
    std::optional<CodeCoverageAggregation> aggregation;
    CodeCoverageAggregationError error {
        CodeCoverageAggregationError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return aggregation.has_value()
            && error == CodeCoverageAggregationError::None;
    }
};

[[nodiscard]] CodeCoverageAggregationResult make_code_coverage_aggregation(
    const CodeCoverageRun& run, const CodeCoverageResult& result,
    std::span<const CodeCoveragePointOwnership> ownership,
    std::size_t instance_count, std::size_t source_count,
    CodeCoverageAggregationLimits limits = { }) noexcept;

} // namespace fsim::runtime
