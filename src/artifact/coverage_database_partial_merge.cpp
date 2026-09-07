// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_partial_merge.hpp"

#include <algorithm>
#include <compare>
#include <new>
#include <set>
#include <utility>

namespace fsim::artifact {
namespace {

    struct PointKey {
        CoverageDatabaseNamespace name_space;
        CoverageDatabaseMetricFamily family;
        CoverageDatabaseMetricScope scope;
        CoverageDatabaseIdentity source_identity;
        CoverageDatabaseIdentity instance_identity;
        CoverageDatabaseIdentity bin_identity;
        std::uint64_t source_line { };

        friend auto operator<=>(const PointKey&, const PointKey&) = default;
    };

    PointKey point_key(const CoverageDatabaseMetricRecord& metric) noexcept
    {
        return { metric.name_space, metric.family, metric.scope,
            metric.source_identity, metric.instance_identity,
            metric.bin_identity, metric.source_line };
    }

    bool has_target_point(const CoverageDatabaseContents& merged,
        const std::size_t target_metric_count,
        const CoverageDatabaseMetricRecord& metric)
    {
        const auto target_metrics
            = std::span { merged.metrics }.first(target_metric_count);
        return std::ranges::binary_search(target_metrics, point_key(metric),
            { }, [](const auto& item) { return point_key(item); });
    }

    bool add_with_ceiling(std::size_t& value, const std::size_t amount,
        const std::size_t ceiling) noexcept
    {
        if (amount > ceiling || value > ceiling - amount) {
            return false;
        }
        value += amount;
        return true;
    }

    CoverageDatabasePartialMergeResult model_failure(
        const CoverageDatabaseModelResult& model,
        const std::size_t input_index) noexcept
    {
        auto error = CoverageDatabaseMergeError::InvalidInput;
        if (model.error == CoverageDatabaseModelError::ResourceLimit
            || model.error == CoverageDatabaseModelError::ArithmeticOverflow) {
            error = CoverageDatabaseMergeError::ResourceLimit;
        } else if (model.error
            == CoverageDatabaseModelError::AllocationFailure) {
            error = CoverageDatabaseMergeError::AllocationFailure;
        }
        return { { }, { }, error, model.error, input_index, model.index };
    }

} // namespace

CoverageDatabasePartialMergeResult merge_coverage_databases_partially(
    const CoverageDatabaseContents& target,
    const std::span<const CoverageDatabaseContents> history,
    const CoverageDatabasePartialMergeLimits& limits) noexcept
{
    if (limits.merge.maximum_inputs == 0U
        || history.size() > limits.merge.maximum_inputs - 1U) {
        return { { }, { }, CoverageDatabaseMergeError::ResourceLimit,
            CoverageDatabaseModelError::ResourceLimit };
    }

    std::size_t historical_sources { };
    std::size_t historical_runs { };
    std::size_t historical_metrics { };
    std::size_t historical_exclusions { };
    for (const auto& candidate : history) {
        if (!add_with_ceiling(historical_sources, candidate.sources.size(),
                limits.maximum_historical_sources)
            || !add_with_ceiling(historical_runs, candidate.runs.size(),
                limits.maximum_historical_runs)
            || !add_with_ceiling(historical_metrics, candidate.metrics.size(),
                limits.maximum_historical_metrics)
            || !add_with_ceiling(historical_exclusions,
                candidate.exclusions.size(),
                limits.maximum_historical_exclusions)) {
            return { { }, { }, CoverageDatabaseMergeError::ResourceLimit,
                CoverageDatabaseModelError::ResourceLimit };
        }
    }

    try {
        auto target_model
            = make_coverage_database_contents(target, limits.merge.model);
        if (!target_model.ok()) {
            return model_failure(target_model, 0U);
        }

        CoverageDatabaseContents merged = std::move(*target_model.contents);
        std::set<CoverageDatabaseIdentity> run_identities;
        for (const auto& run : merged.runs) {
            run_identities.insert(run.identity);
        }
        const auto target_metric_count = merged.metrics.size();

        CoverageDatabasePartialMergeStatistics statistics;
        for (std::size_t history_index = 0; history_index < history.size();
            ++history_index) {
            const auto input_index = history_index + 1U;
            auto candidate_model = make_coverage_database_contents(
                history[history_index], limits.merge.model);
            if (!candidate_model.ok()) {
                return model_failure(candidate_model, input_index);
            }
            auto& candidate = *candidate_model.contents;

            std::vector<CoverageDatabaseIdentity> unchanged_sources;
            unchanged_sources.reserve(candidate.sources.size());
            for (const auto& source : candidate.sources) {
                const auto target_source = std::ranges::lower_bound(
                    merged.sources, source.identity, { },
                    &CoverageDatabaseSourceRecord::identity);
                if (target_source != merged.sources.end()
                    && *target_source == source) {
                    unchanged_sources.push_back(source.identity);
                    ++statistics.unchanged_source_matches;
                }
            }

            std::set<CoverageDatabaseIdentity> retained_run_identities;
            for (const auto& metric : candidate.metrics) {
                if (!std::ranges::binary_search(
                        unchanged_sources, metric.source_identity)
                    || !has_target_point(
                        merged, target_metric_count, metric)) {
                    ++statistics.omitted_metrics;
                    continue;
                }
                if (merged.metrics.size()
                    >= limits.merge.model.maximum_metrics) {
                    return { { }, { },
                        CoverageDatabaseMergeError::ResourceLimit,
                        CoverageDatabaseModelError::ResourceLimit, input_index,
                        statistics.retained_metrics };
                }
                merged.metrics.push_back(metric);
                retained_run_identities.insert(metric.run_identity);
                ++statistics.retained_metrics;
            }

            for (std::size_t run_index = 0; run_index < candidate.runs.size();
                ++run_index) {
                const auto& run = candidate.runs[run_index];
                if (!retained_run_identities.contains(run.identity)) {
                    ++statistics.omitted_runs;
                    continue;
                }
                if (!run_identities.insert(run.identity).second) {
                    return { { }, { }, CoverageDatabaseMergeError::DuplicateRun,
                        CoverageDatabaseModelError::DuplicateRun, input_index,
                        run_index };
                }
                if (merged.runs.size() >= limits.merge.model.maximum_runs) {
                    return { { }, { },
                        CoverageDatabaseMergeError::ResourceLimit,
                        CoverageDatabaseModelError::ResourceLimit, input_index,
                        run_index };
                }
                merged.runs.push_back(run);
                ++statistics.retained_runs;
            }
            statistics.omitted_exclusions += candidate.exclusions.size();
        }

        auto final_model = make_coverage_database_contents(
            std::move(merged), limits.merge.model);
        if (!final_model.ok()) {
            auto failure = model_failure(final_model, 0U);
            failure.statistics = statistics;
            return failure;
        }
        return { std::move(final_model.contents), statistics,
            CoverageDatabaseMergeError::None,
            CoverageDatabaseModelError::None, 0U, 0U };
    } catch (const std::bad_alloc&) {
        return { { }, { }, CoverageDatabaseMergeError::AllocationFailure };
    }
}

} // namespace fsim::artifact
