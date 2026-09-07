// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_database_merge.hpp"

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
    return { value, value * 31U };
}

CoverageDatabaseDigest digest(const std::uint8_t seed)
{
    CoverageDatabaseDigest result;
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index] = static_cast<std::byte>(seed + index);
    }
    return result;
}

CoverageDatabaseContents database(const std::uint64_t run_value)
{
    CoverageDatabaseContents contents;
    contents.fingerprint.digest = digest(50U);
    contents.sources = {
        { id(2U), "rtl/properties.psl", 200U, digest(2U) },
        { id(1U), "rtl/design.sv", 100U, digest(1U) },
    };
    contents.runs = { { id(run_value), "run-" + std::to_string(run_value),
        "fsim 3.0.0-dev", run_value, run_value * 10U, 2U,
        CoverageDatabaseRunStatus::Complete } };
    contents.metrics = {
        { CoverageDatabaseNamespace::Psl,
            CoverageDatabaseMetricFamily::PslProperty,
            CoverageDatabaseMetricScope::Instance, id(13U), id(2U), id(23U),
            id(run_value), std::numeric_limits<std::uint64_t>::max(), 0U, true,
            false },
        { CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCross,
            CoverageDatabaseMetricScope::Instance, id(12U), id(1U), id(22U),
            id(run_value), run_value, 2U, false, false },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Statement,
            CoverageDatabaseMetricScope::Source, id(11U), id(1U), { },
            id(run_value), 1U, 0U, false, false },
    };
    contents.exclusions = {
        { CoverageDatabaseNamespace::SystemVerilogFunctional,
            CoverageDatabaseMetricFamily::SystemVerilogCross,
            CoverageDatabaseMetricScope::Instance, id(12U), id(1U), id(22U),
            "excluded cross" },
        { CoverageDatabaseNamespace::Code,
            CoverageDatabaseMetricFamily::Branch,
            CoverageDatabaseMetricScope::Source, id(14U), id(1U), { },
            "unreachable" },
    };
    return contents;
}

} // namespace

int main()
{
    static_assert(kCoverageDatabaseMergeDiagnostic == "FSIM-COV-036");

    assert(merge_coverage_databases({ }).error
        == CoverageDatabaseMergeError::EmptyInput);

    const std::array inputs { database(101U), database(100U) };
    const auto merged = merge_coverage_databases(inputs);
    assert(merged.ok());
    assert(merged.contents->sources.size() == 2U);
    assert(merged.contents->exclusions.size() == 2U);
    assert(merged.contents->runs.size() == 2U);
    assert(merged.contents->runs.front().identity == id(100U));
    assert(merged.contents->metrics.size() == 6U);
    assert(merged.contents->metrics.front().name_space
        == CoverageDatabaseNamespace::Code);
    assert(merged.contents->metrics.back().overflow);

    const std::array reversed { inputs[1], inputs[0] };
    const auto deterministic = merge_coverage_databases(reversed);
    assert(deterministic.ok()
        && deterministic.contents == merged.contents);

    auto different = database(102U);
    different.fingerprint.digest = digest(51U);
    std::array pair { database(100U), different };
    auto rejected = merge_coverage_databases(pair);
    assert(rejected.error == CoverageDatabaseMergeError::FingerprintMismatch
        && rejected.input_index == 1U);

    different = database(102U);
    different.sources[0].logical_path = "rtl/different.psl";
    pair = { database(100U), different };
    rejected = merge_coverage_databases(pair);
    assert(rejected.error
        == CoverageDatabaseMergeError::SourceInventoryMismatch);

    different = database(102U);
    different.exclusions[0].reason = "different policy";
    pair = { database(100U), different };
    rejected = merge_coverage_databases(pair);
    assert(rejected.error
        == CoverageDatabaseMergeError::ExclusionInventoryMismatch);

    pair = { database(100U), database(100U) };
    rejected = merge_coverage_databases(pair);
    assert(rejected.error == CoverageDatabaseMergeError::DuplicateRun
        && rejected.input_index == 1U && rejected.record_index == 0U);

    different = database(102U);
    different.fingerprint.schema = 2U;
    pair = { database(100U), different };
    rejected = merge_coverage_databases(pair);
    assert(rejected.error == CoverageDatabaseMergeError::InvalidInput
        && rejected.model_error
            == CoverageDatabaseModelError::InvalidFingerprint);

    different = database(102U);
    different.metrics.front().run_identity = id(999U);
    pair = { database(100U), different };
    rejected = merge_coverage_databases(pair);
    assert(rejected.error == CoverageDatabaseMergeError::InvalidInput
        && rejected.model_error == CoverageDatabaseModelError::InvalidMetric);

    auto limits = CoverageDatabaseMergeLimits { };
    limits.model.maximum_runs = 1U;
    assert(merge_coverage_databases(inputs, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.model.maximum_metrics = 5U;
    assert(merge_coverage_databases(inputs, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.model.maximum_text_bytes = 10U;
    assert(merge_coverage_databases(inputs, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);
    limits = { };
    limits.maximum_inputs = 1U;
    assert(merge_coverage_databases(inputs, limits).error
        == CoverageDatabaseMergeError::ResourceLimit);

    assert(inputs[0].runs.size() == 1U && inputs[1].runs.size() == 1U);
}
