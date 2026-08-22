// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_aggregation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::runtime {
namespace {

    using Error = CodeCoverageAggregationError;

    struct PointKey {
        std::uint64_t high { };
        std::uint64_t low { };

        friend bool operator<(const PointKey& left, const PointKey& right) noexcept
        {
            return std::tie(left.high, left.low)
                < std::tie(right.high, right.low);
        }
    };

    struct PointDefinition {
        std::size_t source { };
        CodeCoverageMetric metric { CodeCoverageMetric::Statement };
    };

    CodeCoverageAggregationResult reject(
        const Error error, const std::size_t index = 0U) noexcept
    {
        return { std::nullopt, error, index };
    }

    void add_status(CodeCoverageMetricResult& metric,
        const CodeCoverageStatus status) noexcept
    {
        ++metric.total;
        switch (status) {
        case CodeCoverageStatus::Covered:
            ++metric.covered;
            break;
        case CodeCoverageStatus::Partial:
            ++metric.partial;
            break;
        case CodeCoverageStatus::Excluded:
            ++metric.excluded;
            break;
        case CodeCoverageStatus::Uncovered:
        case CodeCoverageStatus::Empty:
            ++metric.uncovered;
            break;
        }
    }

    template <typename Point, typename Status>
    std::vector<CodeCoverageMetricResult> metrics_from(
        const std::vector<Point>& points, Status status)
    {
        std::array<CodeCoverageMetricResult, 2U> metrics {
            CodeCoverageMetricResult { CodeCoverageMetric::Statement },
            CodeCoverageMetricResult { CodeCoverageMetric::Branch },
        };
        for (const auto& point : points) {
            const auto index = point.metric == CodeCoverageMetric::Statement
                ? 0U
                : 1U;
            add_status(metrics[index], status(point));
        }
        std::vector<CodeCoverageMetricResult> result;
        result.reserve(metrics.size());
        for (auto& metric : metrics) {
            if (metric.total == 0U) {
                continue;
            }
            metric.status = code_coverage_metric_status(metric);
            result.push_back(metric);
        }
        return result;
    }

    void add_source_occurrence(CodeCoverageSourcePointAggregate& aggregate,
        const CodeCoveragePointResult& point) noexcept
    {
        ++aggregate.occurrences;
        if (point.status == CodeCoverageStatus::Covered) {
            ++aggregate.covered_occurrences;
        } else if (point.status == CodeCoverageStatus::Excluded) {
            ++aggregate.excluded_occurrences;
        } else {
            ++aggregate.uncovered_occurrences;
        }
        if (point.hits > std::numeric_limits<std::uint64_t>::max() - aggregate.hits) {
            aggregate.hits = std::numeric_limits<std::uint64_t>::max();
            aggregate.hits_saturated = true;
        } else {
            aggregate.hits += point.hits;
        }
        aggregate.status = aggregate.covered_occurrences != 0U
            ? CodeCoverageStatus::Covered
            : (aggregate.uncovered_occurrences != 0U
                      ? CodeCoverageStatus::Uncovered
                      : CodeCoverageStatus::Excluded);
    }

} // namespace

CodeCoverageAggregationResult make_code_coverage_aggregation(
    const CodeCoverageRun& run, const CodeCoverageResult& result,
    const std::span<const CodeCoveragePointOwnership> ownership,
    const std::size_t instance_count, const std::size_t source_count,
    const CodeCoverageAggregationLimits limits) noexcept
{
    if (instance_count > limits.maximum_instances
        || source_count > limits.maximum_sources
        || result.points.size() > limits.maximum_points) {
        return reject(Error::ResourceLimit);
    }
    auto model_limits = limits.model;
    model_limits.maximum_points
        = std::min(model_limits.maximum_points, limits.maximum_points);
    if (!validate_code_coverage(run, result, model_limits).ok()) {
        return reject(Error::InvalidCoverageResult);
    }
    if (ownership.size() != result.points.size()) {
        return reject(Error::OwnershipCountMismatch);
    }

    try {
        CodeCoverageAggregation aggregation;
        aggregation.run = run.id;
        aggregation.instances.resize(instance_count);
        for (std::size_t index = 0U; index < instance_count; ++index) {
            aggregation.instances[index].instance
                = static_cast<std::uint32_t>(index);
        }
        aggregation.sources.resize(source_count);
        for (std::size_t index = 0U; index < source_count; ++index) {
            aggregation.sources[index].source = index;
        }

        std::map<PointKey, PointDefinition> point_definitions;
        std::map<std::pair<std::uint32_t, PointKey>, std::size_t>
            instance_points;
        std::vector<std::map<PointKey, std::size_t>> source_points(
            source_count);
        for (std::size_t index = 0U; index < ownership.size(); ++index) {
            const auto& owner = ownership[index];
            const auto& point = result.points[index];
            if (owner.counter.value != index) {
                return reject(Error::NonCanonicalOwnership, index);
            }
            if (owner.instance >= instance_count) {
                return reject(Error::UnknownInstance, index);
            }
            if (owner.source >= source_count) {
                return reject(Error::UnknownSource, index);
            }
            const PointKey key { point.point.high, point.point.low };
            const auto [definition, inserted] = point_definitions.emplace(
                key, PointDefinition { owner.source, point.metric });
            if (!inserted && definition->second.source != owner.source) {
                return reject(Error::PointSourceConflict, index);
            }
            if (!inserted && definition->second.metric != point.metric) {
                return reject(Error::PointMetricConflict, index);
            }
            if (!instance_points.emplace(
                                    std::pair { owner.instance, key }, index)
                    .second) {
                return reject(Error::DuplicateInstancePoint, index);
            }

            aggregation.instances[owner.instance].points.push_back(point);
            auto& source = aggregation.sources[owner.source];
            const auto [source_point, source_inserted]
                = source_points[owner.source].emplace(key, source.points.size());
            if (source_inserted) {
                source.points.push_back(CodeCoverageSourcePointAggregate {
                    point.point, point.metric });
            }
            add_source_occurrence(
                source.points[source_point->second], point);
        }

        for (auto& instance : aggregation.instances) {
            instance.metrics = metrics_from(instance.points,
                [](const CodeCoveragePointResult& point) {
                    return point.status;
                });
        }
        for (auto& source : aggregation.sources) {
            std::ranges::sort(source.points,
                [](const CodeCoverageSourcePointAggregate& left,
                    const CodeCoverageSourcePointAggregate& right) {
                    return std::tie(left.point.high, left.point.low, left.metric)
                        < std::tie(
                            right.point.high, right.point.low, right.metric);
                });
            source.metrics = metrics_from(source.points,
                [](const CodeCoverageSourcePointAggregate& point) {
                    return point.status;
                });
        }
        return { std::move(aggregation), Error::None, 0U };
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit);
    }
}

} // namespace fsim::runtime
