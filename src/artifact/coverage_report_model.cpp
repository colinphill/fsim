// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_report_model.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <tuple>

namespace fsim::artifact {
namespace {

    using Identity = CoverageDatabaseIdentity;

    struct ExactKey {
        CoverageDatabaseNamespace name_space;
        CoverageDatabaseMetricFamily family;
        Identity source;
        Identity point;
        Identity instance;

        friend auto operator<=>(const ExactKey&, const ExactKey&) = default;
    };

    struct SourcePointKey {
        CoverageDatabaseNamespace name_space;
        CoverageDatabaseMetricFamily family;
        Identity source;
        Identity point;

        friend bool operator==(const SourcePointKey&, const SourcePointKey&)
            = default;
    };

    struct MetricKey {
        CoverageDatabaseNamespace name_space;
        CoverageDatabaseMetricFamily family;

        friend auto operator<=>(const MetricKey&, const MetricKey&) = default;
    };

    struct ExactPoint {
        std::uint64_t hits { };
        std::uint64_t excluded_hits { };
        bool hits_saturated { };
        bool excluded_hits_saturated { };
        bool has_metric { };
        bool excluded { };
        std::uint64_t source_line { };
    };

    bool empty_identity(const Identity identity) noexcept
    {
        return identity.high == 0U && identity.low == 0U;
    }

    void saturating_add(std::uint64_t& target, const std::uint64_t value,
        bool& saturated) noexcept
    {
        if (value > std::numeric_limits<std::uint64_t>::max() - target) {
            target = std::numeric_limits<std::uint64_t>::max();
            saturated = true;
        } else {
            target += value;
        }
    }

    void add_metric_point(CoverageReportMetric& metric,
        const CoverageReportPoint& point) noexcept
    {
        ++metric.total;
        switch (point.status) {
        case CoverageReportPointStatus::Covered:
            ++metric.covered;
            break;
        case CoverageReportPointStatus::Uncovered:
            ++metric.uncovered;
            break;
        case CoverageReportPointStatus::Excluded:
            ++metric.excluded;
            break;
        }
        saturating_add(metric.hits, point.hits, metric.hits_saturated);
        saturating_add(metric.excluded_hits, point.excluded_hits,
            metric.excluded_hits_saturated);
        metric.hits_saturated = metric.hits_saturated || point.hits_saturated;
        metric.excluded_hits_saturated
            = metric.excluded_hits_saturated || point.excluded_hits_saturated;
    }

    std::vector<CoverageReportMetric> summarize(
        const std::vector<CoverageReportPoint>& points)
    {
        std::map<MetricKey, CoverageReportMetric> metrics;
        for (const auto& point : points) {
            const MetricKey key { point.name_space, point.family };
            auto [entry, inserted] = metrics.try_emplace(key);
            if (inserted) {
                entry->second.name_space = point.name_space;
                entry->second.family = point.family;
            }
            add_metric_point(entry->second, point);
        }
        std::vector<CoverageReportMetric> result;
        result.reserve(metrics.size());
        for (auto& [key, metric] : metrics) {
            static_cast<void>(key);
            result.push_back(std::move(metric));
        }
        return result;
    }

    std::vector<CoverageReportMetric> finish_metrics(
        std::map<MetricKey, CoverageReportMetric> metrics)
    {
        std::vector<CoverageReportMetric> result;
        result.reserve(metrics.size());
        for (auto& [key, metric] : metrics) {
            static_cast<void>(key);
            result.push_back(std::move(metric));
        }
        return result;
    }

} // namespace

CoverageReportModelResult make_coverage_report_model(
    const CoverageDatabaseContents& contents,
    const CoverageReportModelLimits limits) noexcept
{
    const auto database = validate_coverage_database_contents(
        contents, limits.database);
    if (!database.ok()) {
        return { std::nullopt, CoverageReportModelError::InvalidDatabase,
            database.error, CoverageExclusionReportError::None,
            database.index };
    }
    auto exclusion_limits = limits.exclusions;
    exclusion_limits.database = limits.database;
    auto exclusion_report
        = make_coverage_exclusion_report(contents, exclusion_limits);
    if (!exclusion_report.ok()) {
        return { std::nullopt,
            CoverageReportModelError::InvalidExclusionReport,
            exclusion_report.database_error, exclusion_report.error,
            exclusion_report.index };
    }

    try {
        std::map<ExactKey, ExactPoint> exact;
        for (std::size_t index = 0U; index < contents.metrics.size(); ++index) {
            const auto& metric = contents.metrics[index];
            const ExactKey key { metric.name_space, metric.family,
                metric.source_identity, metric.bin_identity,
                metric.instance_identity };
            auto [entry, inserted] = exact.try_emplace(key);
            auto& point = entry->second;
            if (inserted && exact.size() > limits.maximum_exact_points) {
                return { std::nullopt, CoverageReportModelError::ResourceLimit,
                    CoverageDatabaseModelError::None,
                    CoverageExclusionReportError::None, index };
            }
            if (!inserted && point.source_line != metric.source_line) {
                return { std::nullopt,
                    CoverageReportModelError::InvalidDatabase,
                    CoverageDatabaseModelError::InvalidMetric,
                    CoverageExclusionReportError::None, index };
            }
            point.source_line = metric.source_line;
            point.has_metric = true;
            saturating_add(point.hits, metric.hits, point.hits_saturated);
            saturating_add(point.excluded_hits, metric.excluded_hits,
                point.excluded_hits_saturated);
            point.hits_saturated = point.hits_saturated || metric.overflow;
            point.excluded_hits_saturated
                = point.excluded_hits_saturated || metric.excluded_overflow;
        }
        for (std::size_t index = 0U; index < contents.exclusions.size();
            ++index) {
            const auto& exclusion = contents.exclusions[index];
            const ExactKey key { exclusion.name_space, exclusion.family,
                exclusion.source_identity, exclusion.point_identity,
                exclusion.instance_identity };
            auto [entry, inserted] = exact.try_emplace(key);
            auto& point = entry->second;
            if (inserted && exact.size() > limits.maximum_exact_points) {
                return { std::nullopt, CoverageReportModelError::ResourceLimit,
                    CoverageDatabaseModelError::None,
                    CoverageExclusionReportError::None, index };
            }
            if (!inserted && point.source_line != exclusion.source_line) {
                return { std::nullopt,
                    CoverageReportModelError::InvalidDatabase,
                    CoverageDatabaseModelError::InvalidExclusion,
                    CoverageExclusionReportError::None, index };
            }
            point.source_line = exclusion.source_line;
            point.excluded = true;
        }

        CoverageReportModel report;
        report.exclusions = std::move(*exclusion_report.report);
        report.sources.reserve(contents.sources.size());
        std::map<Identity, std::size_t> source_indices;
        for (std::size_t index = 0U; index < contents.sources.size(); ++index) {
            const auto& source = contents.sources[index];
            source_indices.emplace(source.identity, index);
            report.sources.push_back(
                { source.identity, source.logical_path, { }, { } });
        }

        std::map<Identity, CoverageInstanceReport> instance_reports;
        std::size_t source_point_count { };
        std::size_t total_instance_points { };
        auto exact_begin = exact.begin();
        while (exact_begin != exact.end()) {
            const SourcePointKey key { exact_begin->first.name_space,
                exact_begin->first.family, exact_begin->first.source,
                exact_begin->first.point };
            if (++source_point_count > limits.maximum_source_points) {
                return { std::nullopt,
                    CoverageReportModelError::ResourceLimit };
            }
            CoverageReportPoint point;
            point.name_space = key.name_space;
            point.family = key.family;
            point.point_identity = key.point;
            point.source_identity = key.source;
            point.source_line = exact_begin->second.source_line;
            const ExactPoint* source_occurrence { };
            std::size_t covered_occurrences { };
            std::size_t uncovered_occurrences { };
            auto exact_end = exact_begin;
            while (exact_end != exact.end()
                && SourcePointKey { exact_end->first.name_space,
                       exact_end->first.family, exact_end->first.source,
                       exact_end->first.point }
                    == key) {
                const auto& exact_key = exact_end->first;
                const auto& exact_point = exact_end->second;
                if (exact_point.source_line != point.source_line) {
                    return { std::nullopt,
                        CoverageReportModelError::InvalidDatabase,
                        CoverageDatabaseModelError::InvalidMetric };
                }
                if (empty_identity(exact_key.instance)) {
                    source_occurrence = &exact_point;
                }
                saturating_add(
                    point.hits, exact_point.hits, point.hits_saturated);
                saturating_add(point.excluded_hits,
                    exact_point.excluded_hits,
                    point.excluded_hits_saturated);
                point.hits_saturated
                    = point.hits_saturated || exact_point.hits_saturated;
                point.excluded_hits_saturated = point.excluded_hits_saturated
                    || exact_point.excluded_hits_saturated;
                if (!empty_identity(exact_key.instance)) {
                    if (total_instance_points
                        >= limits.maximum_instance_points) {
                        return { std::nullopt,
                            CoverageReportModelError::ResourceLimit };
                    }
                    ++total_instance_points;
                    auto [instance, inserted]
                        = instance_reports.try_emplace(exact_key.instance);
                    if (inserted) {
                        if (instance_reports.size()
                            > limits.maximum_instances) {
                            return { std::nullopt,
                                CoverageReportModelError::ResourceLimit };
                        }
                        instance->second.instance_identity = exact_key.instance;
                    }
                    instance->second.points.push_back({ exact_key.name_space,
                        exact_key.family, exact_key.point, exact_key.source,
                        exact_key.instance, exact_point.hits,
                        exact_point.excluded_hits, exact_point.hits_saturated,
                        exact_point.excluded_hits_saturated,
                        exact_point.excluded
                            ? CoverageReportPointStatus::Excluded
                            : (exact_point.hits != 0U
                                      ? CoverageReportPointStatus::Covered
                                      : CoverageReportPointStatus::Uncovered),
                        exact_point.source_line });
                }
                if (!empty_identity(exact_key.instance)
                    && !exact_point.excluded) {
                    if (exact_point.hits != 0U)
                        ++covered_occurrences;
                    else if (exact_point.has_metric)
                        ++uncovered_occurrences;
                }
                ++exact_end;
            }
            if (source_occurrence) {
                point.hits = source_occurrence->hits;
                point.excluded_hits = source_occurrence->excluded_hits;
                point.hits_saturated = source_occurrence->hits_saturated;
                point.excluded_hits_saturated
                    = source_occurrence->excluded_hits_saturated;
                point.status = source_occurrence->excluded
                    ? CoverageReportPointStatus::Excluded
                    : (source_occurrence->hits != 0U
                              ? CoverageReportPointStatus::Covered
                              : CoverageReportPointStatus::Uncovered);
            } else if (covered_occurrences != 0U) {
                point.status = CoverageReportPointStatus::Covered;
            } else if (uncovered_occurrences != 0U) {
                point.status = CoverageReportPointStatus::Uncovered;
            } else {
                point.status = CoverageReportPointStatus::Excluded;
            }
            const auto source = source_indices.find(key.source);
            if (source == source_indices.end()) {
                return { std::nullopt,
                    CoverageReportModelError::InvalidDatabase,
                    CoverageDatabaseModelError::InvalidSource };
            }
            report.sources[source->second].points.push_back(std::move(point));
            exact_begin = exact_end;
        }

        report.instances.reserve(instance_reports.size());
        for (auto& [identity, instance_report] : instance_reports) {
            static_cast<void>(identity);
            instance_report.metrics = summarize(instance_report.points);
            report.instances.push_back(std::move(instance_report));
        }

        std::map<MetricKey, CoverageReportMetric> combined_metrics;
        for (auto& source : report.sources) {
            source.metrics = summarize(source.points);
            for (const auto& point : source.points) {
                const MetricKey key { point.name_space, point.family };
                auto [entry, inserted] = combined_metrics.try_emplace(key);
                if (inserted) {
                    entry->second.name_space = point.name_space;
                    entry->second.family = point.family;
                }
                add_metric_point(entry->second, point);
            }
        }
        report.combined.metrics = finish_metrics(std::move(combined_metrics));
        return { std::move(report), CoverageReportModelError::None };
    } catch (const std::bad_alloc&) {
        return { std::nullopt, CoverageReportModelError::AllocationFailure };
    } catch (...) {
        return { std::nullopt, CoverageReportModelError::ResourceLimit };
    }
}

} // namespace fsim::artifact
