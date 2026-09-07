// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_report_projection.hpp"

#include <cassert>
#include <string_view>

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
    CoverageDatabaseMetricFamily family, std::uint64_t point,
    std::uint64_t hits, std::uint64_t line)
{
    return { name_space, family, CoverageDatabaseMetricScope::Source,
        id(point), id(1U), { }, id(100U), hits, 0U, false, false, line };
}

CoverageDatabaseContents database()
{
    CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(1U);
    contents.sources = {
        { id(1U), "rtl/a&<.sv", 100U, digest(2U) },
        { id(2U), "rtl/empty.sv", 0U, digest(3U) },
    };
    contents.runs = {
        { id(100U), "run", "fsim", 1U, 20U, 0U,
            CoverageDatabaseRunStatus::Complete },
    };
    contents.metrics = {
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement, 10U, 2U, 10U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement, 11U, 0U, 11U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch, 20U, 1U, 10U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch, 21U, 0U, 10U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Line, 12U, 5U, 10U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Condition, 30U, 7U, 13U),
        metric(CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCoverpoint, 40U, 8U,
            14U),
        metric(CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty, 50U, 9U, 15U),
    };
    contents.exclusions = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(60U), id(1U), { },
            "excluded statement", 12U },
    };
    auto made = make_coverage_database_contents(std::move(contents));
    assert(made.ok());
    return std::move(*made.contents);
}

void test_lcov_projection()
{
    const auto input = database();
    const auto first = project_coverage_report(
        input, CoverageReportProjectionFormat::Lcov);
    const auto second = project_coverage_report(
        input, CoverageReportProjectionFormat::Lcov);
    assert(first.ok() && second.ok() && first.output == second.output);
    const std::string_view expected
        = "TN:fsim\n"
          "SF:rtl/a&<.sv\n"
          "DA:10,5\n"
          "BRDA:10,0,0,1\n"
          "BRDA:10,0,1,0\n"
          "DA:11,0\n"
          "LF:2\nLH:1\nBRF:2\nBRH:1\nend_of_record\n"
          "SF:rtl/empty.sv\n"
          "LF:0\nLH:0\nBRF:0\nBRH:0\nend_of_record\n";
    assert(*first.output == expected);
    assert(first.output->find("DA:12") == std::string::npos);
    assert(first.output->find("DA:13") == std::string::npos);
}

void test_cobertura_projection()
{
    const auto input = database();
    const auto first = project_coverage_report(
        input, CoverageReportProjectionFormat::Cobertura);
    const auto second = project_coverage_report(
        input, CoverageReportProjectionFormat::Cobertura);
    assert(first.ok() && second.ok() && first.output == second.output);
    assert(first.output->starts_with(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<coverage "));
    assert(first.output->ends_with(
        "</classes></package></packages></coverage>\n"));
    assert(first.output->find(
               "line-rate=\"0.500000\" branch-rate=\"0.500000\" "
               "lines-covered=\"1\" lines-valid=\"2\" "
               "branches-covered=\"1\" branches-valid=\"2\"")
        != std::string::npos);
    assert(first.output->find(
               "name=\"rtl/a&amp;&lt;.sv\" filename=\"rtl/a&amp;&lt;.sv\"")
        != std::string::npos);
    assert(first.output->find(
               "<line number=\"10\" hits=\"5\" branch=\"true\" "
               "condition-coverage=\"50% (1/2)\"")
        != std::string::npos);
    assert(first.output->find(
               "<line number=\"11\" hits=\"0\" branch=\"false\"/>")
        != std::string::npos);
    assert(first.output->find("number=\"12\"") == std::string::npos);
    assert(first.output->find("number=\"13\"") == std::string::npos);
}

void test_rejections_and_limits()
{
    const auto input = database();
    auto missing = input;
    missing.metrics.front().source_line = 0U;
    auto result = project_coverage_report(
        missing, CoverageReportProjectionFormat::Lcov);
    assert(!result.output.has_value()
        && result.error == CoverageReportProjectionError::MissingSourceLine);

    auto unsafe = input;
    unsafe.sources.front().logical_path = "rtl/a\n.sv";
    result = project_coverage_report(
        unsafe, CoverageReportProjectionFormat::Lcov);
    assert(!result.output.has_value()
        && result.error == CoverageReportProjectionError::InvalidSourcePath);
    result = project_coverage_report(
        unsafe, CoverageReportProjectionFormat::Cobertura);
    assert(result.ok() && result.output->find("rtl/a&#10;.sv") != std::string::npos);

    CoverageReportProjectionLimits limits;
    limits.maximum_lines = 1U;
    result = project_coverage_report(
        input, CoverageReportProjectionFormat::Lcov, limits);
    assert(!result.output.has_value()
        && result.error == CoverageReportProjectionError::ResourceLimit);
    limits = { };
    limits.maximum_branches = 1U;
    result = project_coverage_report(
        input, CoverageReportProjectionFormat::Cobertura, limits);
    assert(!result.output.has_value()
        && result.error == CoverageReportProjectionError::ResourceLimit);
    limits = { };
    limits.maximum_output_bytes = 1U;
    result = project_coverage_report(
        input, CoverageReportProjectionFormat::Lcov, limits);
    assert(!result.output.has_value()
        && result.error == CoverageReportProjectionError::ResourceLimit);

    auto invalid = input;
    invalid.metrics.insert(invalid.metrics.begin() + 1,
        invalid.metrics.front());
    result = project_coverage_report(
        invalid, CoverageReportProjectionFormat::Cobertura);
    assert(!result.output.has_value()
        && result.error == CoverageReportProjectionError::InvalidReportModel
        && result.database_error
            == CoverageDatabaseModelError::DuplicateMetric);

    result = project_coverage_report(input,
        static_cast<CoverageReportProjectionFormat>(255U));
    assert(!result.output.has_value()
        && result.error == CoverageReportProjectionError::InvalidFormat);
}

} // namespace

int main()
{
    test_lcov_projection();
    test_cobertura_projection();
    test_rejections_and_limits();
}
