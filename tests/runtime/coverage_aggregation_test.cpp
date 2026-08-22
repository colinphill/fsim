// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_aggregation.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace {

using fsim::runtime::CodeCoverageAggregationError;
using fsim::runtime::CodeCoverageAggregationLimits;
using fsim::runtime::CodeCoverageCounterId;
using fsim::runtime::CodeCoverageMetric;
using fsim::runtime::CodeCoverageMetricResult;
using fsim::runtime::CodeCoveragePoint;
using fsim::runtime::CodeCoveragePointId;
using fsim::runtime::CodeCoveragePointOwnership;
using fsim::runtime::CodeCoveragePointResult;
using fsim::runtime::CodeCoverageResult;
using fsim::runtime::CodeCoverageRun;
using fsim::runtime::CodeCoverageStatus;

struct Corpus {
    CodeCoverageRun run;
    CodeCoverageResult result;
    std::array<CodeCoveragePointOwnership, 5U> ownership;
};

Corpus corpus()
{
    constexpr CodeCoveragePointId a { 1U, 10U };
    constexpr CodeCoveragePointId b { 1U, 20U };
    constexpr CodeCoveragePointId c { 2U, 10U };
    Corpus value;
    value.run.id = { 8U, 9U };
    value.run.points = {
        CodeCoveragePoint { a, CodeCoverageMetric::Statement, { 0U } },
        CodeCoveragePoint { b, CodeCoverageMetric::Branch, { 1U } },
        CodeCoveragePoint { a, CodeCoverageMetric::Statement, { 2U } },
        CodeCoveragePoint { b, CodeCoverageMetric::Branch, { 3U } },
        CodeCoveragePoint { c, CodeCoverageMetric::Statement, { 4U } },
    };
    value.result.run = value.run.id;
    value.result.points = {
        CodeCoveragePointResult { a, CodeCoverageMetric::Statement,
            { 0U }, 2U, CodeCoverageStatus::Covered },
        CodeCoveragePointResult { b, CodeCoverageMetric::Branch,
            { 1U }, 0U, CodeCoverageStatus::Uncovered },
        CodeCoveragePointResult { a, CodeCoverageMetric::Statement,
            { 2U }, 3U, CodeCoverageStatus::Covered },
        CodeCoveragePointResult { b, CodeCoverageMetric::Branch,
            { 3U }, 1U, CodeCoverageStatus::Covered },
        CodeCoveragePointResult { c, CodeCoverageMetric::Statement,
            { 4U }, 0U, CodeCoverageStatus::Excluded },
    };
    value.result.metrics = {
        CodeCoverageMetricResult { CodeCoverageMetric::Statement, 3U, 2U,
            0U, 0U, 1U, CodeCoverageStatus::Covered },
        CodeCoverageMetricResult { CodeCoverageMetric::Branch, 2U, 1U, 0U,
            1U, 0U, CodeCoverageStatus::Partial },
    };
    value.ownership = {
        CodeCoveragePointOwnership { CodeCoverageCounterId { 0U }, 0U, 0U },
        CodeCoveragePointOwnership { CodeCoverageCounterId { 1U }, 0U, 0U },
        CodeCoveragePointOwnership { CodeCoverageCounterId { 2U }, 1U, 0U },
        CodeCoveragePointOwnership { CodeCoverageCounterId { 3U }, 1U, 0U },
        CodeCoveragePointOwnership { CodeCoverageCounterId { 4U }, 1U, 1U },
    };
    return value;
}

void expect_error(const Corpus& value,
    const CodeCoverageAggregationError error,
    const CodeCoverageAggregationLimits limits = { })
{
    const auto aggregated = fsim::runtime::make_code_coverage_aggregation(
        value.run, value.result, value.ownership, 2U, 2U, limits);
    assert(!aggregated.ok());
    assert(!aggregated.aggregation);
    assert(aggregated.error == error);
}

void test_union_and_instance_retention()
{
    const auto value = corpus();
    const auto aggregated = fsim::runtime::make_code_coverage_aggregation(
        value.run, value.result, value.ownership, 2U, 2U);
    assert(aggregated.ok());
    const auto& result = *aggregated.aggregation;
    assert(result.run == value.run.id);
    assert(result.instances.size() == 2U);
    assert(result.sources.size() == 2U);

    assert(result.instances[0].points.size() == 2U);
    assert(result.instances[1].points.size() == 3U);
    assert(result.instances[0].points[1].status
        == CodeCoverageStatus::Uncovered);
    assert(result.instances[1].points[1].status
        == CodeCoverageStatus::Covered);
    assert(result.instances[0].points[0].counter.value == 0U);
    assert(result.instances[1].points[0].counter.value == 2U);
    assert(result.instances[1].points[2].status
        == CodeCoverageStatus::Excluded);

    assert(result.sources[0].points.size() == 2U);
    const auto& source_a = result.sources[0].points[0];
    const auto& source_b = result.sources[0].points[1];
    assert(source_a.point == CodeCoveragePointId(1U, 10U));
    assert(source_a.hits == 5U);
    assert(source_a.occurrences == 2U);
    assert(source_a.covered_occurrences == 2U);
    assert(source_a.status == CodeCoverageStatus::Covered);
    assert(source_b.point == CodeCoveragePointId(1U, 20U));
    assert(source_b.occurrences == 2U);
    assert(source_b.covered_occurrences == 1U);
    assert(source_b.uncovered_occurrences == 1U);
    assert(source_b.status == CodeCoverageStatus::Covered);
    assert(result.sources[0].metrics.size() == 2U);
    assert(result.sources[0].metrics[0].total == 1U);
    assert(result.sources[0].metrics[0].covered == 1U);
    assert(result.sources[0].metrics[1].total == 1U);
    assert(result.sources[0].metrics[1].covered == 1U);
    assert(result.sources[1].points.size() == 1U);
    assert(result.sources[1].points[0].status
        == CodeCoverageStatus::Excluded);
    assert(result.sources[1].metrics[0].status
        == CodeCoverageStatus::Excluded);
}

void test_saturation_and_empty_owners()
{
    auto value = corpus();
    value.result.points[0].hits = UINT64_MAX;
    const auto saturated = fsim::runtime::make_code_coverage_aggregation(
        value.run, value.result, value.ownership, 2U, 2U);
    assert(saturated.ok());
    assert(saturated.aggregation->sources[0].points[0].hits == UINT64_MAX);
    assert(saturated.aggregation->sources[0].points[0].hits_saturated);

    CodeCoverageRun empty_run;
    empty_run.id = { 4U, 5U };
    CodeCoverageResult empty_result;
    empty_result.run = empty_run.id;
    const std::array<CodeCoveragePointOwnership, 0U> no_ownership;
    const auto empty = fsim::runtime::make_code_coverage_aggregation(
        empty_run, empty_result, no_ownership, 2U, 3U);
    assert(empty.ok());
    assert(empty.aggregation->instances.size() == 2U);
    assert(empty.aggregation->sources.size() == 3U);
    for (const auto& instance : empty.aggregation->instances) {
        assert(instance.points.empty() && instance.metrics.empty());
    }
    for (const auto& source : empty.aggregation->sources) {
        assert(source.points.empty() && source.metrics.empty());
    }
}

void test_rejections()
{
    auto value = corpus();
    value.result.points[0].hits = 0U;
    expect_error(value, CodeCoverageAggregationError::InvalidCoverageResult);

    value = corpus();
    value.ownership[1].counter.value = 4U;
    expect_error(value, CodeCoverageAggregationError::NonCanonicalOwnership);
    value = corpus();
    value.ownership[1].instance = 2U;
    expect_error(value, CodeCoverageAggregationError::UnknownInstance);
    value = corpus();
    value.ownership[1].source = 2U;
    expect_error(value, CodeCoverageAggregationError::UnknownSource);
    value = corpus();
    value.ownership[2].instance = 0U;
    expect_error(value, CodeCoverageAggregationError::DuplicateInstancePoint);
    value = corpus();
    value.ownership[2].source = 1U;
    expect_error(value, CodeCoverageAggregationError::PointSourceConflict);

    value = corpus();
    value.run.points[2].metric = CodeCoverageMetric::Branch;
    value.result.points[2].metric = CodeCoverageMetric::Branch;
    value.result.metrics = {
        CodeCoverageMetricResult { CodeCoverageMetric::Statement, 2U, 1U,
            0U, 0U, 1U, CodeCoverageStatus::Covered },
        CodeCoverageMetricResult { CodeCoverageMetric::Branch, 3U, 2U, 0U,
            1U, 0U, CodeCoverageStatus::Partial },
    };
    expect_error(value, CodeCoverageAggregationError::PointMetricConflict);

    auto limits = CodeCoverageAggregationLimits { };
    limits.maximum_instances = 1U;
    expect_error(corpus(), CodeCoverageAggregationError::ResourceLimit, limits);
    limits = { };
    limits.maximum_sources = 1U;
    expect_error(corpus(), CodeCoverageAggregationError::ResourceLimit, limits);
    limits = { };
    limits.maximum_points = 4U;
    expect_error(corpus(), CodeCoverageAggregationError::ResourceLimit, limits);

    value = corpus();
    const auto short_ownership = std::span { value.ownership }.first(4U);
    const auto mismatched = fsim::runtime::make_code_coverage_aggregation(
        value.run, value.result, short_ownership, 2U, 2U);
    assert(!mismatched.ok());
    assert(mismatched.error
        == CodeCoverageAggregationError::OwnershipCountMismatch);
}

} // namespace

int main()
{
    static_assert(fsim::runtime::kCodeCoverageAggregationDiagnostic
        == "FSIM-COV-013");
    test_union_and_instance_retention();
    test_saturation_and_empty_owners();
    test_rejections();
}
