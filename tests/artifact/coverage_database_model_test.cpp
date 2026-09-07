// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_model.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace {

fsim::artifact::CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value * 17U };
}

fsim::artifact::CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    fsim::artifact::CoverageDatabaseDigest result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(seed + index);
    }
    return result;
}

fsim::artifact::CoverageDatabaseContents example()
{
    using namespace fsim::artifact;
    CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(90U);
    contents.sources = {
        { id(2U), "rtl/second.sv", 22U, digest(2U) },
        { id(1U), "rtl/first.vhd", 11U, digest(1U) },
    };
    contents.runs = {
        { id(12U), "regression-2", "fsim 3.0.0-dev", 42U, 200U, 4U,
            CoverageDatabaseRunStatus::Stopped },
        { id(11U), "regression-1", "fsim 3.0.0-dev", 41U, 100U, 3U,
            CoverageDatabaseRunStatus::Complete },
    };
    contents.metrics = {
        { CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty,
            CoverageDatabaseMetricScope::Source, id(103U), id(1U), { },
            id(11U), 1U, 0U, false, false },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Toggle,
            CoverageDatabaseMetricScope::Instance, id(102U), id(2U),
            id(202U), id(12U), std::numeric_limits<std::uint64_t>::max(),
            0U, true, false },
        { CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCross,
            CoverageDatabaseMetricScope::Instance, id(101U), id(2U),
            id(201U), id(11U), 7U, 2U, false, false },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(100U), id(1U), { },
            id(11U), 3U, 0U, false, false, 11U },
    };
    contents.exclusions = {
        { CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslDirective,
            CoverageDatabaseMetricScope::Instance, id(302U), id(2U),
            id(202U), "disabled requirement" },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, id(301U), id(1U), { },
            "unreachable defensive arm", 12U },
    };
    return contents;
}

} // namespace

int main()
{
    using namespace fsim::artifact;

    static_assert(kCoverageDatabaseModel
        == "fsim-unified-coverage-database-v3");
    static_assert(kCoverageDatabaseModelDiagnostic == "FSIM-COV-032");
    assert(coverage_database_metric_family_name(
               CoverageDatabaseMetricFamily::Statement)
        == "statement");
    assert(coverage_database_metric_family_name(
               CoverageDatabaseMetricFamily::SystemVerilogCoverpoint)
        == "systemverilog-coverpoint");
    assert(coverage_database_metric_family_name(
               CoverageDatabaseMetricFamily::PslProperty)
        == "psl-property");
    assert(coverage_database_metric_family_name(
        static_cast<CoverageDatabaseMetricFamily>(0U))
            .empty());

    const auto made = make_coverage_database_contents(example());
    assert(made.ok());
    assert(make_coverage_database_contents(example()).contents
        == made.contents);
    assert(made.contents->sources.front().identity == id(1U));
    assert(made.contents->runs.front().identity == id(11U));
    assert(made.contents->metrics.front().name_space
        == CoverageDatabaseNamespace::Code);
    assert(made.contents->exclusions.front().name_space
        == CoverageDatabaseNamespace::Code);
    assert(validate_coverage_database_contents(*made.contents).ok());

    auto invalid = *made.contents;
    invalid.fingerprint.schema = 2U;
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidFingerprint);
    invalid = *made.contents;
    invalid.fingerprint.model = "old-model";
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidFingerprint);
    invalid = *made.contents;
    invalid.fingerprint.digest = { };
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidFingerprint);

    constexpr std::array invalid_paths { std::string_view { },
        std::string_view { "/absolute.sv" },
        std::string_view { "../escape.sv" },
        std::string_view { "./design.sv" },
        std::string_view { "rtl/../design.sv" },
        std::string_view { "rtl/./design.sv" },
        std::string_view { "rtl//design.sv" },
        std::string_view { "rtl/design.sv/" },
        std::string_view { "rtl\\design.sv" },
        std::string_view { "C:/rtl/design.sv" },
        std::string_view { "c:rtl/design.sv" },
        std::string_view { "rtl/design\0.sv", 13U } };
    for (const auto path : invalid_paths) {
        invalid = *made.contents;
        invalid.sources[0].logical_path.assign(path);
        assert(validate_coverage_database_contents(invalid).error
            == CoverageDatabaseModelError::InvalidSource);
    }
    invalid = *made.contents;
    invalid.sources.push_back(invalid.sources.front());
    assert(make_coverage_database_contents(std::move(invalid)).error
        == CoverageDatabaseModelError::DuplicateSource);
    invalid = *made.contents;
    std::swap(invalid.sources[0], invalid.sources[1]);
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::NonCanonicalOrder);

    invalid = *made.contents;
    invalid.runs[0].status = static_cast<CoverageDatabaseRunStatus>(0U);
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidRun);
    invalid = *made.contents;
    invalid.runs.push_back(invalid.runs.front());
    assert(make_coverage_database_contents(std::move(invalid)).error
        == CoverageDatabaseModelError::DuplicateRun);

    invalid = *made.contents;
    invalid.metrics[0].name_space
        = static_cast<CoverageDatabaseNamespace>(0U);
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidNamespace);
    invalid = *made.contents;
    invalid.metrics[0].family
        = CoverageDatabaseMetricFamily::SystemVerilogCross;
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidMetricFamily);
    invalid = *made.contents;
    invalid.metrics[0].scope = CoverageDatabaseMetricScope::Instance;
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidMetricScope);
    invalid = *made.contents;
    invalid.metrics[0].source_identity = id(999U);
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidMetric);
    invalid = *made.contents;
    invalid.metrics[0].overflow = true;
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidMetric);
    invalid = *made.contents;
    invalid.metrics[0].excluded_overflow = true;
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidMetric);
    invalid = *made.contents;
    invalid.metrics[0].source_line
        = CoverageDatabaseModelLimits { }.maximum_source_line + 1U;
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidMetric);
    invalid = *made.contents;
    auto conflicting_metric = invalid.metrics.front();
    conflicting_metric.run_identity = id(12U);
    conflicting_metric.source_line += 1U;
    invalid.metrics.push_back(conflicting_metric);
    assert(make_coverage_database_contents(std::move(invalid)).error
        == CoverageDatabaseModelError::InvalidMetric);
    invalid = *made.contents;
    invalid.metrics.push_back(invalid.metrics.front());
    assert(make_coverage_database_contents(std::move(invalid)).error
        == CoverageDatabaseModelError::DuplicateMetric);

    invalid = *made.contents;
    invalid.exclusions[0].reason.clear();
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidExclusion);
    invalid = *made.contents;
    invalid.exclusions[0].source_line
        = CoverageDatabaseModelLimits { }.maximum_source_line + 1U;
    assert(validate_coverage_database_contents(invalid).error
        == CoverageDatabaseModelError::InvalidExclusion);
    invalid = *made.contents;
    auto conflicting_exclusion = invalid.exclusions.front();
    conflicting_exclusion.reason = "second reason";
    conflicting_exclusion.source_line += 1U;
    invalid.exclusions.push_back(conflicting_exclusion);
    assert(make_coverage_database_contents(std::move(invalid)).error
        == CoverageDatabaseModelError::InvalidExclusion);
    invalid = *made.contents;
    invalid.exclusions.push_back(invalid.exclusions.front());
    assert(make_coverage_database_contents(std::move(invalid)).error
        == CoverageDatabaseModelError::DuplicateExclusion);

    auto limits = CoverageDatabaseModelLimits { };
    limits.maximum_sources = 1U;
    assert(make_coverage_database_contents(example(), limits).error
        == CoverageDatabaseModelError::ResourceLimit);
    limits = { };
    limits.maximum_runs = 1U;
    assert(make_coverage_database_contents(example(), limits).error
        == CoverageDatabaseModelError::ResourceLimit);
    limits = { };
    limits.maximum_metrics = 3U;
    assert(make_coverage_database_contents(example(), limits).error
        == CoverageDatabaseModelError::ResourceLimit);
    limits = { };
    limits.maximum_exclusions = 1U;
    assert(make_coverage_database_contents(example(), limits).error
        == CoverageDatabaseModelError::ResourceLimit);
    limits = { };
    limits.maximum_logical_path_bytes = 12U;
    assert(make_coverage_database_contents(example(), limits).error
        == CoverageDatabaseModelError::InvalidSource);
    limits = { };
    limits.maximum_reason_bytes = 10U;
    assert(make_coverage_database_contents(example(), limits).error
        == CoverageDatabaseModelError::InvalidExclusion);
    limits = { };
    limits.maximum_text_bytes = 1U;
    assert(make_coverage_database_contents(example(), limits).error
        == CoverageDatabaseModelError::ResourceLimit);
}
