// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_points.hpp"

#include <algorithm>
#include <iterator>
#include <new>
#include <set>
#include <stdexcept>
#include <tuple>

namespace fsim::elaboration {
namespace {

    using Error = CoveragePointExclusionError;

    struct Interval {
        std::size_t source_index { };
        std::uint64_t begin { };
        std::uint64_t end { };
    };

    bool valid_kind(const CoveragePointExclusionKind kind) noexcept
    {
        switch (kind) {
        case CoveragePointExclusionKind::Declaration:
        case CoveragePointExclusionKind::StaticallyRemoved:
            return true;
        }
        return false;
    }

} // namespace

CoveragePointExclusionResult exclude_coverage_points(
    const std::span<const CoverageInventorySource> sources,
    const std::span<const CoverageInventoryPointDraft> points,
    const std::span<const CoveragePointExclusion> exclusions,
    const CoveragePointExclusionLimits limits) noexcept
{
    CoveragePointExclusionResult result;
    const auto reject = [&](const Error error, const std::size_t index = 0U) {
        result.points.clear();
        result.error = error;
        result.index = index;
        return result;
    };
    if (points.size() > limits.maximum_points
        || exclusions.size() > limits.maximum_exclusions) {
        return reject(Error::ResourceLimit);
    }

    try {
        for (std::size_t index = 0U; index < sources.size(); ++index) {
            if (sources[index].source_name.empty()) {
                return reject(Error::InvalidSource, index);
            }
            if (!frontend::is_code_coverage_source_identity_valid(
                    sources[index].identity)) {
                return reject(Error::InvalidSourceIdentity, index);
            }
        }

        std::vector<Interval> intervals;
        intervals.reserve(exclusions.size());
        std::set<std::tuple<std::size_t, std::uint64_t, std::uint64_t>>
            exact_exclusions;
        for (std::size_t index = 0U; index < exclusions.size(); ++index) {
            const auto& exclusion = exclusions[index];
            if (exclusion.source_index >= sources.size()) {
                return reject(Error::InvalidSource, index);
            }
            if (!valid_kind(exclusion.kind)) {
                return reject(Error::InvalidExclusionKind, index);
            }
            if (exclusion.span.begin_offset >= exclusion.span.end_offset
                || exclusion.span.end_offset
                    > sources[exclusion.source_index].identity.content_bytes) {
                return reject(Error::InvalidExclusionSpan, index);
            }
            const auto key = std::tuple { exclusion.source_index,
                exclusion.span.begin_offset, exclusion.span.end_offset };
            if (!exact_exclusions.insert(key).second) {
                return reject(Error::DuplicateExclusion, index);
            }
            intervals.push_back({ exclusion.source_index,
                exclusion.span.begin_offset, exclusion.span.end_offset });
        }
        std::ranges::sort(intervals, { }, [](const Interval& interval) {
            return std::tuple {
                interval.source_index, interval.begin, interval.end
            };
        });
        std::vector<Interval> merged;
        merged.reserve(intervals.size());
        for (const auto& interval : intervals) {
            if (!merged.empty()
                && merged.back().source_index == interval.source_index
                && interval.begin <= merged.back().end) {
                merged.back().end = std::max(merged.back().end, interval.end);
            } else {
                merged.push_back(interval);
            }
        }

        result.points.reserve(points.size());
        std::set<std::pair<std::uint64_t, std::uint64_t>> point_ids;
        for (std::size_t index = 0U; index < points.size(); ++index) {
            const auto& point = points[index];
            if (!runtime::is_code_coverage_identity_valid(point.id)) {
                return reject(Error::InvalidPointIdentity, index);
            }
            if (!runtime::is_code_coverage_point_metric(point.metric)) {
                return reject(Error::InvalidMetric, index);
            }
            if (point.source_index >= sources.size()) {
                return reject(Error::InvalidSource, index);
            }
            if (point.span.begin_offset >= point.span.end_offset
                || point.span.end_offset
                    > sources[point.source_index].identity.content_bytes) {
                return reject(Error::InvalidPointSpan, index);
            }
            if (point.line == 0U) {
                return reject(Error::InvalidLine, index);
            }
            if (!point_ids.emplace(point.id.high, point.id.low).second) {
                return reject(Error::DuplicatePoint, index);
            }

            const auto candidate = std::ranges::upper_bound(
                merged,
                std::pair { point.source_index, point.span.begin_offset },
                { },
                [](const Interval& interval) {
                    return std::pair { interval.source_index, interval.begin };
                });
            bool excluded = false;
            if (candidate != merged.begin()) {
                const auto& interval = *std::prev(candidate);
                excluded = interval.source_index == point.source_index
                    && interval.begin <= point.span.begin_offset
                    && interval.end >= point.span.end_offset;
            }
            if (!excluded) {
                result.points.push_back(point);
            }
        }
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit);
    }
}

} // namespace fsim::elaboration
