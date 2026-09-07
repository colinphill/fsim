// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_report_model.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

namespace {

using namespace fsim::artifact;

CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value * 17U };
}

CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    CoverageDatabaseDigest result { };
    for (std::size_t index = 0U; index < result.size(); ++index)
        result[index] = static_cast<std::byte>(seed + index);
    return result;
}

CoverageDatabaseMetricRecord metric(CoverageDatabaseNamespace name_space,
    CoverageDatabaseMetricFamily family, CoverageDatabaseMetricScope scope,
    const std::uint64_t point, const std::uint64_t source,
    const std::uint64_t instance, const std::uint64_t run,
    const std::uint64_t hits, const bool overflow = false,
    const std::uint64_t source_line = 1U)
{
    return { name_space, family, scope, id(point), id(source),
        instance == 0U ? CoverageDatabaseIdentity { } : id(instance), id(run),
        hits, 0U, overflow, false, source_line };
}

CoverageDatabaseContents database()
{
    CoverageDatabaseContents result;
    result.fingerprint.digest = digest(1U);
    result.sources = {
        { id(1U), "rtl/a.sv", 100U, digest(2U) },
        { id(2U), "rtl/b.sv", 200U, digest(3U) },
        { id(3U), "rtl/empty.sv", 0U, digest(4U) },
    };
    result.runs = {
        { id(101U), "first", "fsim", 1U, 10U, 0U,
            CoverageDatabaseRunStatus::Complete },
        { id(102U), "second", "fsim", 2U, 20U, 0U,
            CoverageDatabaseRunStatus::Complete },
    };
    result.metrics = {
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Instance, 10U, 1U, 201U, 101U,
            0U, false, 10U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Instance, 10U, 1U, 202U, 101U,
            3U, false, 10U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Condition,
            CoverageDatabaseMetricScope::Source, 11U, 1U, 0U, 101U,
            std::numeric_limits<std::uint64_t>::max(), true, 11U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Condition,
            CoverageDatabaseMetricScope::Source, 11U, 1U, 0U, 102U, 1U,
            false, 11U),
        metric(CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCoverpoint,
            CoverageDatabaseMetricScope::Instance, 12U, 1U, 201U, 101U,
            0U, false, 12U),
    };
    result.exclusions = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Instance, id(10U), id(1U), id(201U),
            "instance waiver", 10U },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, id(20U), id(2U), { },
            "generated branch", 20U },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, id(20U), id(2U), { },
            "verification waiver", 20U },
        { CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty,
            CoverageDatabaseMetricScope::Instance, id(21U), id(2U), id(202U),
            "property waiver", 21U },
    };
    auto made = make_coverage_database_contents(std::move(result));
    assert(made.ok());
    return std::move(*made.contents);
}

const CoverageSourceReport& source(
    const CoverageReportModel& report, const std::uint64_t value)
{
    const auto found = std::ranges::find(
        report.sources, id(value), &CoverageSourceReport::source_identity);
    assert(found != report.sources.end());
    return *found;
}

const CoverageInstanceReport& instance(
    const CoverageReportModel& report, const std::uint64_t value)
{
    const auto found = std::ranges::find(report.instances, id(value),
        &CoverageInstanceReport::instance_identity);
    assert(found != report.instances.end());
    return *found;
}

const CoverageReportPoint& point(const std::vector<CoverageReportPoint>& points,
    const CoverageDatabaseNamespace name_space,
    const CoverageDatabaseMetricFamily family)
{
    const auto found = std::ranges::find_if(points, [&](const auto& candidate) {
        return candidate.name_space == name_space && candidate.family == family;
    });
    assert(found != points.end());
    return *found;
}

void test_three_views_and_no_grand_score()
{
    const auto input = database();
    const auto made = make_coverage_report_model(input);
    assert(made.ok());
    const auto repeated = make_coverage_report_model(input);
    assert(repeated.ok() && repeated.report == made.report);
    const auto& report = *made.report;
    assert(report.sources.size() == 3U);
    assert(source(report, 3U).points.empty()
        && source(report, 3U).metrics.empty());

    const auto& first = source(report, 1U);
    assert(first.logical_path == "rtl/a.sv" && first.points.size() == 3U);
    const auto& statement = point(first.points,
        CoverageDatabaseNamespace::Code,
        CoverageDatabaseMetricFamily::Statement);
    assert(statement.status == CoverageReportPointStatus::Covered
        && statement.hits == 3U && statement.source_line == 10U);
    const auto& condition = point(first.points,
        CoverageDatabaseNamespace::Code,
        CoverageDatabaseMetricFamily::Condition);
    assert(condition.status == CoverageReportPointStatus::Covered
        && condition.hits == std::numeric_limits<std::uint64_t>::max()
        && condition.hits_saturated);
    assert(point(first.points,
               CoverageDatabaseNamespace::SystemVerilogFunctional,
               CoverageDatabaseMetricFamily::SystemVerilogCoverpoint)
               .status
        == CoverageReportPointStatus::Uncovered);

    const auto& second = source(report, 2U);
    assert(point(second.points, CoverageDatabaseNamespace::Code,
               CoverageDatabaseMetricFamily::Branch)
               .status
        == CoverageReportPointStatus::Excluded);
    assert(point(second.points, CoverageDatabaseNamespace::Psl,
               CoverageDatabaseMetricFamily::PslProperty)
               .status
        == CoverageReportPointStatus::Excluded);

    const auto& first_instance = instance(report, 201U);
    assert(point(first_instance.points, CoverageDatabaseNamespace::Code,
               CoverageDatabaseMetricFamily::Statement)
               .status
        == CoverageReportPointStatus::Excluded);
    const auto& second_instance = instance(report, 202U);
    assert(point(second_instance.points, CoverageDatabaseNamespace::Code,
               CoverageDatabaseMetricFamily::Statement)
               .status
        == CoverageReportPointStatus::Covered);

    assert(report.combined.metrics.size() == 5U);
    assert(std::ranges::all_of(report.combined.metrics,
        [](const auto& metric) {
            return metric.total
                == metric.covered + metric.uncovered + metric.excluded;
        }));
    assert(report.exclusions.points.size() == 3U
        && report.exclusions.total_reasons == 4U);
}

void test_invalid_and_resource_failures()
{
    auto invalid = database();
    std::ranges::reverse(invalid.metrics);
    auto result = make_coverage_report_model(invalid);
    assert(result.error == CoverageReportModelError::InvalidDatabase);

    invalid = database();
    const auto statement_exclusion = std::ranges::find(invalid.exclusions,
        CoverageDatabaseMetricFamily::Statement,
        &CoverageDatabaseExclusionRecord::family);
    assert(statement_exclusion != invalid.exclusions.end());
    statement_exclusion->source_line += 1U;
    result = make_coverage_report_model(invalid);
    assert(result.error == CoverageReportModelError::InvalidDatabase);

    const auto input = database();
    CoverageReportModelLimits limits;
    limits.maximum_exact_points = 1U;
    result = make_coverage_report_model(input, limits);
    assert(result.error == CoverageReportModelError::ResourceLimit);
    limits = { };
    limits.maximum_source_points = 1U;
    result = make_coverage_report_model(input, limits);
    assert(result.error == CoverageReportModelError::ResourceLimit);
    limits = { };
    limits.maximum_instance_points = 1U;
    result = make_coverage_report_model(input, limits);
    assert(result.error == CoverageReportModelError::ResourceLimit);
    limits = { };
    limits.maximum_instances = 1U;
    result = make_coverage_report_model(input, limits);
    assert(result.error == CoverageReportModelError::ResourceLimit);
    limits = { };
    limits.exclusions.maximum_reasons = 1U;
    result = make_coverage_report_model(input, limits);
    assert(result.error == CoverageReportModelError::InvalidExclusionReport);
    assert(result.exclusion_error == CoverageExclusionReportError::ResourceLimit);
}

} // namespace

int main()
{
    test_three_views_and_no_grand_score();
    test_invalid_and_resource_failures();
}
