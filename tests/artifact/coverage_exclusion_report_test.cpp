// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_exclusion_report.hpp"

#include <algorithm>
#include <cassert>

namespace {

using namespace fsim::artifact;

CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value + 100U };
}

CoverageDatabaseDigest digest(const std::uint8_t value)
{
    CoverageDatabaseDigest result { };
    result.fill(static_cast<std::byte>(value));
    return result;
}

CoverageDatabaseContents contents()
{
    CoverageDatabaseContents result;
    result.fingerprint.digest = digest(7U);
    result.sources = { { id(1U), "rtl/a.sv", 10U, digest(8U) } };
    result.exclusions = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(10U), id(1U), { },
            "second reason" },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Instance, id(11U), id(1U), id(2U),
            "instance reason" },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(10U), id(1U), { },
            "first reason" },
    };
    auto made = make_coverage_database_contents(std::move(result));
    assert(made.ok());
    return std::move(*made.contents);
}

void test_complete_projection()
{
    const auto database = contents();
    const auto result = make_coverage_exclusion_report(database);
    assert(result.ok());
    assert(result.report->points.size() == 2U);
    assert(result.report->total_reasons == 3U);
    const auto& source = result.report->points.front();
    assert(source.scope == CoverageDatabaseMetricScope::Source);
    assert(source.reasons.size() == 2U);
    assert(source.reasons[0] == "first reason");
    assert(source.reasons[1] == "second reason");

    auto duplicate = database;
    duplicate.exclusions.push_back(duplicate.exclusions.front());
    assert(make_coverage_database_contents(std::move(duplicate)).error
        == CoverageDatabaseModelError::DuplicateExclusion);
}

void test_invalid_and_bounded_projection()
{
    const auto database = contents();
    auto invalid = database;
    std::ranges::reverse(invalid.exclusions);
    auto result = make_coverage_exclusion_report(invalid);
    assert(result.error == CoverageExclusionReportError::InvalidDatabase);
    assert(result.database_error == CoverageDatabaseModelError::NonCanonicalOrder);

    CoverageExclusionReportLimits limits;
    limits.maximum_points = 1U;
    result = make_coverage_exclusion_report(database, limits);
    assert(result.error == CoverageExclusionReportError::ResourceLimit);
    limits = { };
    limits.maximum_reasons = 2U;
    result = make_coverage_exclusion_report(database, limits);
    assert(result.error == CoverageExclusionReportError::ResourceLimit);
    limits = { };
    limits.maximum_reason_bytes = 5U;
    result = make_coverage_exclusion_report(database, limits);
    assert(result.error == CoverageExclusionReportError::ResourceLimit);
    limits = { };
    limits.maximum_total_reason_bytes = 20U;
    result = make_coverage_exclusion_report(database, limits);
    assert(result.error == CoverageExclusionReportError::ResourceLimit);
}

} // namespace

int main()
{
    test_complete_projection();
    test_invalid_and_bounded_projection();
}
