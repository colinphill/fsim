// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_report_render.hpp"

#include <cassert>
#include <limits>
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

CoverageDatabaseContents database()
{
    CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(1U);
    contents.sources = {
        { id(1U), "rtl/<a&\"'>\n.sv", 42U, digest(2U) },
        { id(2U), "rtl/empty.sv", 0U, digest(3U) },
    };
    contents.runs = {
        { id(10U), "run", "fsim", 7U, 100U, 2U,
            CoverageDatabaseRunStatus::Complete },
    };
    contents.metrics = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Instance, id(100U), id(1U),
            id(200U), id(10U), std::numeric_limits<std::uint64_t>::max(),
            std::numeric_limits<std::uint64_t>::max(), true, true, 7U },
        { CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCoverpoint,
            CoverageDatabaseMetricScope::Source, id(101U), id(1U), { },
            id(10U), 0U, 0U, false, false, 8U },
        { CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty,
            CoverageDatabaseMetricScope::Source, id(103U), id(1U), { },
            id(10U), 2U, 0U, false, false, 9U },
    };
    contents.exclusions = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, id(102U), id(1U), { },
            "why & < \"quote\" \\ tab\t", 10U },
    };
    auto made = make_coverage_database_contents(std::move(contents));
    assert(made.ok());
    return std::move(*made.contents);
}

std::size_t occurrences(
    const std::string_view text, const std::string_view needle)
{
    std::size_t result { };
    for (std::size_t offset = 0U;
        (offset = text.find(needle, offset)) != std::string_view::npos;
        offset += needle.size()) {
        ++result;
    }
    return result;
}

void test_deterministic_text()
{
    const auto input = database();
    const auto first = render_coverage_report(input, CoverageReportFormat::Text);
    const auto second = render_coverage_report(input, CoverageReportFormat::Text);
    assert(first.ok() && second.ok() && first.output == second.output);
    assert(first.output->starts_with("fsim coverage report v3\ncombined\n"));
    assert(first.output->find("path=\"rtl/<a&\\\"'>\\u000a.sv\"")
        != std::string::npos);
    assert(first.output->find(
               "hits=18446744073709551615 excluded_hits=18446744073709551615 "
               "hits_saturated=true")
        != std::string::npos);
    assert(first.output->find("status=covered") != std::string::npos);
    assert(first.output->find("status=uncovered") != std::string::npos);
    assert(first.output->find("status=excluded") != std::string::npos);
    assert(first.output->find("namespace=psl family=psl-property")
        != std::string::npos);
    assert(first.output->find(" line=7 hits=") != std::string::npos);
    assert(first.output->find("exclusions total_reasons=1")
        != std::string::npos);
    assert(first.output->find("reason=\"why & < \\\"quote\\\" \\\\ tab\\u0009\"")
        != std::string::npos);
}

void test_deterministic_safe_html()
{
    const auto input = database();
    const auto first = render_coverage_report(input, CoverageReportFormat::Html);
    const auto second = render_coverage_report(input, CoverageReportFormat::Html);
    assert(first.ok() && second.ok() && first.output == second.output);
    assert(first.output->starts_with("<!doctype html>\n<html lang=\"en\">"));
    assert(first.output->ends_with("</body></html>\n"));
    assert(first.output->find("rtl/&lt;a&amp;&quot;&#39;&gt;&#10;.sv")
        != std::string::npos);
    assert(first.output->find(
               "why &amp; &lt; &quot;quote&quot; \\ tab&#9;")
        != std::string::npos);
    assert(first.output->find("<script") == std::string::npos);
    assert(occurrences(*first.output, "<h2>") == 4U);
}

void test_full_fidelity_json()
{
    const auto input = database();
    const auto first = render_coverage_report(input, CoverageReportFormat::Json);
    const auto second = render_coverage_report(input, CoverageReportFormat::Json);
    assert(first.ok() && second.ok() && first.output == second.output);
    assert(first.output->starts_with(
        "{\"schema\":\"fsim-coverage-report-v3\",\"sources\":["));
    assert(first.output->ends_with("}}\n"));
    assert(first.output->find("\"logical_path\":\"rtl/<a&\\\"'>\\n.sv\"")
        != std::string::npos);
    assert(first.output->find(
               "\"hits\":18446744073709551615,"
               "\"excluded_hits\":18446744073709551615,")
        != std::string::npos);
    assert(first.output->find("\"source_line\":7,\"hits\":")
        != std::string::npos);
    assert(first.output->find("\"hits_saturated\":true")
        != std::string::npos);
    assert(first.output->find("\"excluded_hits_saturated\":true")
        != std::string::npos);
    assert(first.output->find("\"status\":\"covered\"")
        != std::string::npos);
    assert(first.output->find("\"status\":\"uncovered\"")
        != std::string::npos);
    assert(first.output->find("\"status\":\"excluded\"")
        != std::string::npos);
    assert(first.output->find(
               "\"namespace\":\"psl\",\"family\":\"psl-property\"")
        != std::string::npos);
    assert(first.output->find(
               "\"reasons\":[\"why & < \\\"quote\\\" \\\\ tab\\t\"]")
        != std::string::npos);
    assert(occurrences(*first.output, "\"source_identity\"") >= 4U);
    assert(occurrences(*first.output, "\"instance_identity\"") >= 3U);
    assert(first.output->find("overall") == std::string::npos);
    assert(first.output->find("grand_score") == std::string::npos);
    assert(first.output->find('\n') == first.output->size() - 1U);
}

void test_transactional_failures()
{
    const auto input = database();
    CoverageReportRenderLimits limits;
    limits.maximum_output_bytes = 1U;
    auto result
        = render_coverage_report(input, CoverageReportFormat::Json, limits);
    assert(!result.output.has_value()
        && result.error == CoverageReportRenderError::ResourceLimit);

    limits = { };
    limits.model.maximum_source_points = 1U;
    result = render_coverage_report(input, CoverageReportFormat::Text, limits);
    assert(!result.output.has_value()
        && result.error == CoverageReportRenderError::InvalidReportModel
        && result.model_error == CoverageReportModelError::ResourceLimit);

    auto invalid = input;
    invalid.metrics.insert(invalid.metrics.begin() + 1,
        invalid.metrics.front());
    result = render_coverage_report(invalid, CoverageReportFormat::Html);
    assert(!result.output.has_value()
        && result.error == CoverageReportRenderError::InvalidReportModel
        && result.model_error == CoverageReportModelError::InvalidDatabase
        && result.database_error
            == CoverageDatabaseModelError::DuplicateMetric);

    result = render_coverage_report(
        input, static_cast<CoverageReportFormat>(255U));
    assert(!result.output.has_value()
        && result.error == CoverageReportRenderError::InvalidReportFormat);
}

} // namespace

int main()
{
    test_deterministic_text();
    test_deterministic_safe_html();
    test_full_fidelity_json();
    test_transactional_failures();
}
