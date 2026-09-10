// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/code_coverage.hpp"

#include <array>

namespace fsim::runtime {

CodeCoverageValidationResult validate_code_coverage(
    const CodeCoverageRun& run, const CodeCoverageResult& result,
    const CodeCoverageModelLimits limits) noexcept
{
    const auto reject = [](const CodeCoverageModelError error,
                            const std::size_t index = 0U) {
        return CodeCoverageValidationResult { error, index };
    };
    if (run.points.size() > limits.maximum_points
        || result.points.size() > limits.maximum_points
        || result.metrics.size() > limits.maximum_metric_results) {
        return reject(CodeCoverageModelError::ResourceLimit);
    }
    if (!is_code_coverage_identity_valid(run.id)) {
        return reject(CodeCoverageModelError::InvalidRunIdentity);
    }
    if (result.run != run.id) {
        return reject(CodeCoverageModelError::ResultRunMismatch);
    }
    if (result.points.size() != run.points.size()) {
        return reject(CodeCoverageModelError::ResultCountMismatch);
    }

    std::array<std::uint64_t, 2U> totals { };
    std::array<std::uint64_t, 2U> covered { };
    std::array<std::uint64_t, 2U> uncovered { };
    std::array<std::uint64_t, 2U> excluded { };
    for (std::size_t index = 0U; index < run.points.size(); ++index) {
        const auto& point = run.points[index];
        const auto& point_result = result.points[index];
        if (!is_code_coverage_identity_valid(point.id)) {
            return reject(CodeCoverageModelError::InvalidPointIdentity, index);
        }
        if (!is_code_coverage_point_metric(point.metric)) {
            return reject(CodeCoverageModelError::InvalidMetric, index);
        }
        if (point.counter.value != index) {
            return reject(
                CodeCoverageModelError::CounterOwnershipMismatch, index);
        }
        if (point_result.point != point.id
            || point_result.metric != point.metric
            || point_result.counter != point.counter) {
            return reject(CodeCoverageModelError::PointResultMismatch, index);
        }
        if (!is_code_coverage_status_valid(point_result.status)
            || point_result.status == CodeCoverageStatus::Empty
            || point_result.status == CodeCoverageStatus::Partial
            || (point_result.status == CodeCoverageStatus::Covered
                && point_result.hits == 0U)
            || (point_result.status != CodeCoverageStatus::Covered
                && point_result.hits != 0U)) {
            return reject(CodeCoverageModelError::InvalidPointStatus, index);
        }

        const auto metric_index
            = point.metric == CodeCoverageMetric::Statement ? 0U : 1U;
        ++totals[metric_index];
        if (point_result.status == CodeCoverageStatus::Covered) {
            ++covered[metric_index];
        } else if (point_result.status == CodeCoverageStatus::Excluded) {
            ++excluded[metric_index];
        } else {
            ++uncovered[metric_index];
        }
    }

    std::array<bool, 3U> metric_seen { };
    for (std::size_t index = 0U; index < result.metrics.size(); ++index) {
        const auto& metric = result.metrics[index];
        const auto name = code_coverage_metric_name(metric.metric);
        if (name.empty()) {
            return reject(CodeCoverageModelError::InvalidMetric, index);
        }
        const auto metric_index
            = static_cast<std::size_t>(metric.metric) - 1U;
        if (metric_seen[metric_index]) {
            return reject(CodeCoverageModelError::DuplicateMetricResult, index);
        }
        metric_seen[metric_index] = true;

        if (metric.covered > metric.total
            || metric.partial > metric.total - metric.covered
            || metric.uncovered
                > metric.total - metric.covered - metric.partial
            || metric.excluded
                != metric.total - metric.covered - metric.partial
                    - metric.uncovered) {
            return reject(CodeCoverageModelError::InvalidMetricCounts, index);
        }
        if (!is_code_coverage_status_valid(metric.status)
            || metric.status != code_coverage_metric_status(metric)) {
            return reject(CodeCoverageModelError::InvalidMetricStatus, index);
        }
        if (metric.metric != CodeCoverageMetric::Line) {
            const auto point_metric_index = metric_index;
            if (metric.total != totals[point_metric_index]
                || metric.covered != covered[point_metric_index]
                || metric.partial != 0U
                || metric.uncovered != uncovered[point_metric_index]
                || metric.excluded != excluded[point_metric_index]) {
                return reject(
                    CodeCoverageModelError::MetricResultMismatch, index);
            }
        }
    }

    for (std::size_t index = 0U; index < totals.size(); ++index) {
        if (totals[index] != 0U && !metric_seen[index]) {
            return reject(CodeCoverageModelError::MissingMetricResult, index);
        }
    }
    return { };
}

} // namespace fsim::runtime
