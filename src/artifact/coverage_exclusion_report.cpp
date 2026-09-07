// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_exclusion_report.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <tuple>

namespace fsim::artifact {
namespace {

    auto point_key(const CoverageDatabaseExclusionRecord& record) noexcept
    {
        return std::tie(record.name_space, record.family, record.scope,
            record.source_identity, record.instance_identity,
            record.point_identity);
    }

} // namespace

CoverageExclusionReportResult make_coverage_exclusion_report(
    const CoverageDatabaseContents& contents,
    const CoverageExclusionReportLimits limits) noexcept
{
    const auto invalid = validate_coverage_database_contents(
        contents, limits.database);
    if (!invalid.ok()) {
        return { std::nullopt, CoverageExclusionReportError::InvalidDatabase,
            invalid.error, invalid.index };
    }
    if (contents.exclusions.size() > limits.maximum_reasons) {
        return { std::nullopt, CoverageExclusionReportError::ResourceLimit,
            CoverageDatabaseModelError::None, 0U };
    }
    try {
        CoverageExclusionReport report;
        report.points.reserve(std::min(
            contents.exclusions.size(), limits.maximum_points));
        std::size_t total_reason_bytes { };
        for (std::size_t index = 0U; index < contents.exclusions.size();
            ++index) {
            const auto& exclusion = contents.exclusions[index];
            if (exclusion.reason.size() > limits.maximum_reason_bytes) {
                return { std::nullopt,
                    CoverageExclusionReportError::ResourceLimit,
                    CoverageDatabaseModelError::None, index };
            }
            if (total_reason_bytes > limits.maximum_total_reason_bytes
                || exclusion.reason.size()
                    > limits.maximum_total_reason_bytes
                        - total_reason_bytes) {
                return { std::nullopt,
                    CoverageExclusionReportError::ResourceLimit,
                    CoverageDatabaseModelError::None, index };
            }
            total_reason_bytes += exclusion.reason.size();
            if (report.points.empty()
                || point_key(contents.exclusions[index - 1U])
                    != point_key(exclusion)) {
                if (report.points.size() >= limits.maximum_points) {
                    return { std::nullopt,
                        CoverageExclusionReportError::ResourceLimit,
                        CoverageDatabaseModelError::None, index };
                }
                report.points.push_back({ exclusion.name_space,
                    exclusion.family, exclusion.scope,
                    exclusion.point_identity, exclusion.source_identity,
                    exclusion.instance_identity, { },
                    exclusion.source_line });
            }
            report.points.back().reasons.push_back(exclusion.reason);
        }
        report.total_reasons = contents.exclusions.size();
        return { std::move(report), CoverageExclusionReportError::None,
            CoverageDatabaseModelError::None, 0U };
    } catch (const std::bad_alloc&) {
        return { std::nullopt,
            CoverageExclusionReportError::AllocationFailure,
            CoverageDatabaseModelError::None, 0U };
    }
}

} // namespace fsim::artifact
