// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/coverage_exclusion_persistence.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace fsim;

artifact::CoverageDatabaseIdentity id(const std::uint64_t value)
{
    return { value, value + 100U };
}

elaboration::CoverageExternalExclusionPlan plan()
{
    const std::vector<project::CoverageExclusionEntry> entries {
        { .source = "rtl/*.sv", .hierarchy = std::nullopt, .object = std::nullopt, .metric = "statement", .reason = "generated source" },
        { .source = std::nullopt, .hierarchy = "top.*", .object = std::nullopt, .metric = "all", .reason = "vendor hierarchy" },
        { .source = "psl/*.psl", .hierarchy = std::nullopt, .object = std::nullopt, .metric = "psl_property", .reason = "imported property" },
    };
    auto result = elaboration::make_coverage_external_exclusion_plan(entries);
    assert(result.ok());
    return std::move(*result.plan);
}

elaboration::CoverageExclusionCandidate statement()
{
    return { artifact::CoverageDatabaseNamespace::Code,
        artifact::CoverageDatabaseMetricFamily::Statement,
        id(10U), id(1U), id(2U), "rtl/codec.sv", "top.u_codec", "",
        { "inline source control" }, 17U };
}

void test_source_and_external_reasons()
{
    auto candidate = statement();
    const auto result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &candidate, 1U });
    assert(result.ok());
    assert(result.records.size() == 3U);
    assert(result.records[0].scope
        == artifact::CoverageDatabaseMetricScope::Source);
    assert(result.records[0].reason == "inline source control");
    assert(result.records[0].source_line == 17U);
    assert(result.records[1].scope
        == artifact::CoverageDatabaseMetricScope::Instance);
    assert(result.records[1].reason == "generated source");
    assert(result.records[2].reason == "vendor hierarchy");

    auto reversed = plan();
    std::ranges::reverse(reversed.rules);
    const auto rejected = elaboration::make_coverage_exclusion_records(
        reversed, std::span { &candidate, 1U });
    assert(rejected.error
        == elaboration::CoverageExclusionPersistenceError::ExternalExclusion);
    assert(rejected.external_error
        == elaboration::CoverageExternalExclusionError::InvalidPlan);
    assert(rejected.records.empty());
}

void test_namespaces_and_empty_matches()
{
    auto psl = statement();
    psl.name_space = artifact::CoverageDatabaseNamespace::Psl;
    psl.family = artifact::CoverageDatabaseMetricFamily::PslProperty;
    psl.source_path = "psl/checks.psl";
    psl.hierarchy_path = "unit";
    psl.source_reasons.clear();
    auto functional = statement();
    functional.name_space
        = artifact::CoverageDatabaseNamespace::SystemVerilogFunctional;
    functional.family
        = artifact::CoverageDatabaseMetricFamily::SystemVerilogCoverpoint;
    functional.point_identity = id(11U);
    functional.source_path = "tb/codec_tb.sv";
    functional.hierarchy_path = "tb";
    functional.source_reasons.clear();
    const std::array candidates { psl, functional };
    const auto result = elaboration::make_coverage_exclusion_records(
        plan(), candidates);
    assert(result.ok());
    assert(result.records.size() == 1U);
    assert(result.records.front().name_space
        == artifact::CoverageDatabaseNamespace::Psl);
    assert(result.records.front().reason == "imported property");
}

void test_rejections_and_limits()
{
    auto candidate = statement();
    auto invalid = candidate;
    invalid.point_identity = { };
    auto result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &invalid, 1U });
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::InvalidCandidate);

    const std::array duplicates { candidate, candidate };
    result = elaboration::make_coverage_exclusion_records(plan(), duplicates);
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::DuplicateCandidate);

    invalid = candidate;
    invalid.source_reasons = { std::string { "bad\0reason", 10U } };
    result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &invalid, 1U });
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::InvalidReason);

    elaboration::CoverageExclusionPersistenceLimits limits;
    limits.maximum_candidates = 0U;
    result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &candidate, 1U }, limits);
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::ResourceLimit);
    limits = { };
    limits.maximum_source_reasons = 0U;
    result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &candidate, 1U }, limits);
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::ResourceLimit);
    limits = { };
    limits.maximum_records = 2U;
    result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &candidate, 1U }, limits);
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::ResourceLimit);
    limits = { };
    limits.maximum_reason_bytes = 4U;
    result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &candidate, 1U }, limits);
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::ResourceLimit);
    limits = { };
    limits.maximum_total_reason_bytes = 21U;
    result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &candidate, 1U }, limits);
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::ResourceLimit);
    limits = { };
    limits.maximum_source_line = 16U;
    result = elaboration::make_coverage_exclusion_records(
        plan(), std::span { &candidate, 1U }, limits);
    assert(result.error
        == elaboration::CoverageExclusionPersistenceError::InvalidCandidate);

    assert(elaboration::coverage_exclusion_persistence_error_name(
               elaboration::CoverageExclusionPersistenceError::ExternalExclusion)
        == "external-exclusion");
}

} // namespace

int main()
{
    test_source_and_external_reasons();
    test_namespaces_and_empty_matches();
    test_rejections_and_limits();
}
