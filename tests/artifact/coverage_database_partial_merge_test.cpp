// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_partial_merge.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using namespace fsim::artifact;

CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value * 37U };
}

CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    CoverageDatabaseDigest result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(seed + index);
    }
    return result;
}

CoverageDatabaseMetricRecord metric(const CoverageDatabaseNamespace name_space,
    const CoverageDatabaseMetricFamily family,
    const CoverageDatabaseMetricScope scope, const std::uint64_t bin,
    const std::uint64_t source, const std::uint64_t instance,
    const std::uint64_t run, const std::uint64_t hits,
    const std::uint64_t source_line = 1U)
{
    return { name_space, family, scope, id(bin), id(source),
        instance == 0U ? CoverageDatabaseIdentity { } : id(instance), id(run),
        hits, 0U, hits == std::numeric_limits<std::uint64_t>::max(), false,
        source_line };
}

CoverageDatabaseContents target()
{
    CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(60U);
    contents.sources = {
        { id(1U), "rtl/unchanged.sv", 100U, digest(1U) },
        { id(2U), "rtl/changed.psl", 200U, digest(2U) },
    };
    contents.runs = { { id(100U), "target", "fsim 3.0.0-dev", 1U, 10U, 1U,
        CoverageDatabaseRunStatus::Complete } };
    contents.metrics = {
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, 11U, 1U, 0U, 100U, 1U),
        metric(CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCross,
            CoverageDatabaseMetricScope::Instance, 12U, 1U, 22U, 100U, 2U),
        metric(CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty,
            CoverageDatabaseMetricScope::Instance, 13U, 1U, 23U, 100U, 3U),
        metric(CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslDirective,
            CoverageDatabaseMetricScope::Source, 14U, 2U, 0U, 100U, 4U),
    };
    contents.exclusions = { { CoverageDatabaseNamespace::Code,
        CoverageDatabaseMetricFamily::Branch,
        CoverageDatabaseMetricScope::Source, id(15U), id(1U), { },
        "target policy" } };
    return contents;
}

CoverageDatabaseContents historical(const std::uint64_t retained_run)
{
    auto contents = target();
    contents.fingerprint.digest = digest(61U);
    contents.sources[1].content_digest = digest(3U);
    contents.sources.push_back(
        { id(3U), "rtl/removed.sv", 300U, digest(4U) });
    contents.runs = {
        { id(retained_run), "historical-retained", "fsim 3.0.0-dev", 2U,
            20U, 2U, CoverageDatabaseRunStatus::Complete },
        { id(retained_run + 1U), "historical-omitted", "fsim 3.0.0-dev", 3U,
            30U, 3U, CoverageDatabaseRunStatus::Stopped },
    };
    contents.metrics = {
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, 11U, 1U, 0U, retained_run,
            std::numeric_limits<std::uint64_t>::max()),
        metric(CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCross,
            CoverageDatabaseMetricScope::Instance, 12U, 1U, 22U,
            retained_run, 20U),
        metric(CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty,
            CoverageDatabaseMetricScope::Instance, 13U, 1U, 23U,
            retained_run, 30U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, 99U, 1U, 0U,
            retained_run + 1U, 40U),
        metric(CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCross,
            CoverageDatabaseMetricScope::Instance, 12U, 1U, 24U,
            retained_run + 1U, 45U),
        metric(CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslDirective,
            CoverageDatabaseMetricScope::Source, 14U, 2U, 0U,
            retained_run + 1U, 50U),
        metric(CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, 16U, 3U, 0U,
            retained_run + 1U, 60U),
    };
    contents.exclusions = {
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, id(15U), id(1U), { },
            "historical policy" },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(16U), id(3U), { },
            "removed point" },
    };
    return contents;
}

} // namespace

int main()
{
    static_assert(kCoverageDatabasePartialMergeDiagnostic == "FSIM-COV-037");

    const auto canonical_target = make_coverage_database_contents(target());
    assert(canonical_target.ok());
    const auto no_history = merge_coverage_databases_partially(target(), { });
    assert(no_history.ok()
        && no_history.contents == canonical_target.contents);

    const std::array history { historical(101U) };
    const auto merged = merge_coverage_databases_partially(target(), history);
    assert(merged.ok());
    assert(merged.contents->fingerprint == canonical_target.contents->fingerprint);
    assert(merged.contents->sources == canonical_target.contents->sources);
    assert(merged.contents->exclusions == canonical_target.contents->exclusions);
    assert(merged.contents->runs.size() == 2U);
    assert(merged.contents->metrics.size() == 7U);
    assert(std::ranges::any_of(
        merged.contents->metrics, &CoverageDatabaseMetricRecord::overflow));
    assert(merged.statistics.unchanged_source_matches == 1U);
    assert(merged.statistics.retained_runs == 1U);
    assert(merged.statistics.omitted_runs == 1U);
    assert(merged.statistics.retained_metrics == 3U);
    assert(merged.statistics.omitted_metrics == 4U);
    assert(merged.statistics.omitted_exclusions == 2U);

    auto relocated = historical(101U);
    relocated.metrics.front().source_line += 1U;
    const std::array relocated_history { relocated };
    const auto relocated_merge
        = merge_coverage_databases_partially(target(), relocated_history);
    assert(relocated_merge.ok()
        && relocated_merge.statistics.retained_metrics == 2U
        && relocated_merge.statistics.omitted_metrics == 5U);

    const std::array forward { historical(101U), historical(103U) };
    const std::array reverse { forward[1], forward[0] };
    const auto first = merge_coverage_databases_partially(target(), forward);
    const auto second = merge_coverage_databases_partially(target(), reverse);
    assert(first.ok() && second.ok() && first.contents == second.contents);

    auto duplicate = historical(100U);
    std::array one { duplicate };
    auto rejected = merge_coverage_databases_partially(target(), one);
    assert(rejected.error == CoverageDatabaseMergeError::DuplicateRun
        && rejected.input_index == 1U);

    duplicate.metrics.erase(duplicate.metrics.begin(),
        duplicate.metrics.begin() + 3);
    one = { duplicate };
    const auto omitted_duplicate
        = merge_coverage_databases_partially(target(), one);
    assert(omitted_duplicate.ok()
        && omitted_duplicate.statistics.retained_runs == 0U);

    auto invalid = historical(101U);
    invalid.fingerprint.schema = 2U;
    one = { invalid };
    rejected = merge_coverage_databases_partially(target(), one);
    assert(rejected.error == CoverageDatabaseMergeError::InvalidInput
        && rejected.model_error
            == CoverageDatabaseModelError::InvalidFingerprint);

    auto limits = CoverageDatabasePartialMergeLimits { };
    limits.merge.maximum_inputs = 1U;
    assert(merge_coverage_databases_partially(target(), history, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.merge.model.maximum_runs = 1U;
    assert(merge_coverage_databases_partially(target(), history, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.merge.model.maximum_metrics = 6U;
    assert(merge_coverage_databases_partially(target(), history, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.maximum_historical_sources = 2U;
    assert(merge_coverage_databases_partially(target(), history, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.maximum_historical_runs = 1U;
    assert(merge_coverage_databases_partially(target(), history, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.maximum_historical_metrics = 6U;
    assert(merge_coverage_databases_partially(target(), history, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.maximum_historical_exclusions = 1U;
    assert(merge_coverage_databases_partially(target(), history, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);

    assert(history[0].runs.size() == 2U && target().runs.size() == 1U);
}
