// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/coverage_database_psl.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

namespace {

using namespace fsim;

artifact::CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value * 23U };
}

artifact::CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    artifact::CoverageDatabaseDigest result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(seed + index);
    }
    return result;
}

artifact::CoverageDatabaseContents database()
{
    artifact::CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(70U);
    contents.sources = {
        { id(1U), "rtl/checks.vhd", 100U, digest(1U) },
        { id(2U), "rtl/properties.psl", 200U, digest(2U) },
    };
    contents.runs = { { id(3U), "psl-run", "fsim 3.0.0-dev", 5U, 70U,
        4U, artifact::CoverageDatabaseRunStatus::Complete } };
    return contents;
}

std::vector<app::ConcurrentAssertionCoverage> coverage()
{
    app::ConcurrentAssertionCoverage assertion;
    assertion.name = "work:rtl:stable";
    assertion.process = "top.encoder";
    assertion.kind = app::ConcurrentAssertionCoverageKind::assertion;
    assertion.slot = 2U;
    assertion.attempts = 10U;
    assertion.passes = 7U;
    assertion.failures = 1U;
    assertion.vacuous = 1U;
    assertion.aborted = 1U;
    assertion.instance_identity = "top.encoder";
    assertion.source_span = 11U;

    app::ConcurrentAssertionCoverage cover;
    cover.name = "work:rtl:accepted";
    cover.process = "top.decoder";
    cover.kind = app::ConcurrentAssertionCoverageKind::cover;
    cover.slot = 4U;
    cover.attempts = 3U;
    cover.passes = 2U;
    cover.failures = 1U;
    cover.instance_identity = "top.decoder";
    cover.source_span = 12U;

    auto assumption = assertion;
    assumption.name = "work:rtl:environment";
    assumption.kind = app::ConcurrentAssertionCoverageKind::assumption;
    assumption.slot = 5U;
    assumption.attempts = 0U;
    assumption.passes = 0U;
    assumption.failures = 0U;
    assumption.vacuous = 0U;
    assumption.aborted = 0U;
    auto restriction = cover;
    restriction.name = "work:rtl:legal_sequence";
    restriction.kind = app::ConcurrentAssertionCoverageKind::restriction;
    restriction.slot = 6U;
    restriction.attempts = 0U;
    restriction.passes = 0U;
    restriction.failures = 0U;
    return { cover, assumption, assertion, restriction };
}

std::array<app::PslCoverageDatabaseSource, 2> sources()
{
    return { app::PslCoverageDatabaseSource { 12U, id(2U) },
        app::PslCoverageDatabaseSource { 11U, id(1U) } };
}

} // namespace

int main()
{
    using namespace fsim;
    static_assert(app::kPslCoverageDatabaseDiagnostic == "FSIM-COV-034");
    const auto bindings = sources();
    const auto made = app::project_psl_coverage_namespace(
        database(), coverage(), bindings, id(3U));
    assert(made.ok());
    auto reversed = coverage();
    std::ranges::reverse(reversed);
    assert(app::project_psl_coverage_namespace(
               database(), reversed, bindings, id(3U))
               .contents
        == made.contents);
    assert(made.contents->metrics.size() == 20U
        && made.contents->exclusions.empty());
    assert(std::ranges::count(made.contents->metrics,
               artifact::CoverageDatabaseMetricFamily::PslDirective,
               &artifact::CoverageDatabaseMetricRecord::family)
        == 4);
    assert(std::ranges::count(made.contents->metrics,
               artifact::CoverageDatabaseMetricFamily::PslProperty,
               &artifact::CoverageDatabaseMetricRecord::family)
        == 16);
    assert(std::ranges::all_of(made.contents->metrics, [](const auto& metric) {
        return metric.name_space == artifact::CoverageDatabaseNamespace::Psl
            && metric.scope
            == artifact::CoverageDatabaseMetricScope::Instance
            && metric.run_identity == id(3U) && metric.excluded_hits == 0U
            && !metric.excluded_overflow;
    }));
    const auto attempt_ten = std::ranges::find_if(
        made.contents->metrics, [](const auto& metric) {
            return metric.family
                == artifact::CoverageDatabaseMetricFamily::PslDirective
                && metric.hits == 10U;
        });
    const auto attempt_three = std::ranges::find_if(
        made.contents->metrics, [](const auto& metric) {
            return metric.family
                == artifact::CoverageDatabaseMetricFamily::PslDirective
                && metric.hits == 3U;
        });
    assert(attempt_ten != made.contents->metrics.end()
        && attempt_three != made.contents->metrics.end()
        && attempt_ten->instance_identity != attempt_three->instance_identity
        && attempt_ten->source_identity != attempt_three->source_identity);

    auto maximum = coverage();
    maximum.resize(1U);
    maximum[0].attempts = std::numeric_limits<std::uint64_t>::max();
    maximum[0].passes = std::numeric_limits<std::uint64_t>::max();
    maximum[0].failures = 0U;
    const auto saturated = app::project_psl_coverage_namespace(
        database(), maximum, bindings, id(3U));
    assert(saturated.ok()
        && std::ranges::count_if(
               saturated.contents->metrics,
               [](const auto& metric) { return metric.overflow; })
            == 2);

    auto result = app::project_psl_coverage_namespace(
        *made.contents, coverage(), bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::NamespaceNotEmpty);
    result = app::project_psl_coverage_namespace(
        database(), coverage(), bindings, id(99U));
    assert(result.error == app::PslCoverageDatabaseError::InvalidRun);
    auto invalid_database = database();
    invalid_database.fingerprint.schema = 2U;
    result = app::project_psl_coverage_namespace(
        std::move(invalid_database), coverage(), bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::InvalidDatabaseModel
        && result.model_error
            == artifact::CoverageDatabaseModelError::InvalidFingerprint);

    auto invalid_bindings = bindings;
    invalid_bindings[0].source_identity = id(99U);
    result = app::project_psl_coverage_namespace(
        database(), coverage(), invalid_bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::InvalidSourceBinding);
    invalid_bindings = bindings;
    invalid_bindings[1].source = invalid_bindings[0].source;
    result = app::project_psl_coverage_namespace(
        database(), coverage(), invalid_bindings, id(3U));
    assert(result.error
        == app::PslCoverageDatabaseError::DuplicateSourceBinding);

    auto invalid = coverage();
    invalid[0].name.clear();
    result = app::project_psl_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::InvalidDirective);
    invalid = coverage();
    invalid.push_back(invalid.front());
    result = app::project_psl_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::DuplicateDirective);
    invalid = coverage();
    invalid[0].kind
        = static_cast<app::ConcurrentAssertionCoverageKind>(255U);
    result = app::project_psl_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::InvalidDirective);
    invalid = coverage();
    invalid[0].attempts += 1U;
    result = app::project_psl_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::InvalidDirective);
    invalid = coverage();
    invalid[0].attempts = std::numeric_limits<std::uint64_t>::max();
    invalid[0].passes = std::numeric_limits<std::uint64_t>::max();
    invalid[0].failures = 1U;
    result = app::project_psl_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::InvalidDirective);
    invalid = coverage();
    invalid[0].source_span = 99U;
    result = app::project_psl_coverage_namespace(
        database(), invalid, bindings, id(3U));
    assert(result.error == app::PslCoverageDatabaseError::UnknownSource);

    auto limits = app::PslCoverageDatabaseLimits { };
    limits.maximum_source_bindings = 1U;
    result = app::project_psl_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error == app::PslCoverageDatabaseError::ResourceLimit);
    limits = { };
    limits.maximum_directives = 3U;
    result = app::project_psl_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error == app::PslCoverageDatabaseError::ResourceLimit);
    limits = { };
    limits.maximum_identity_bytes = 3U;
    result = app::project_psl_coverage_namespace(
        database(), coverage(), bindings, id(3U), limits);
    assert(result.error == app::PslCoverageDatabaseError::InvalidDirective);
}
