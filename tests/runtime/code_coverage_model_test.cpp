// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/code_coverage.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

fsim::runtime::CodeCoverageRun make_run()
{
    using namespace fsim::runtime;
    return {
        .id = { 0x1234U, 0x5678U },
        .points = {
            { { 1U, 11U }, CodeCoverageMetric::Statement, { 0U } },
            { { 2U, 22U }, CodeCoverageMetric::Statement, { 1U } },
            { { 3U, 33U }, CodeCoverageMetric::Branch, { 2U } },
        },
    };
}

fsim::runtime::CodeCoverageResult make_result()
{
    using namespace fsim::runtime;
    return {
        .run = { 0x1234U, 0x5678U },
        .points = {
            { { 1U, 11U }, CodeCoverageMetric::Statement, { 0U }, 4U,
                CodeCoverageStatus::Covered },
            { { 2U, 22U }, CodeCoverageMetric::Statement, { 1U }, 0U,
                CodeCoverageStatus::Uncovered },
            { { 3U, 33U }, CodeCoverageMetric::Branch, { 2U }, 0U,
                CodeCoverageStatus::Excluded },
        },
        .metrics = {
            { CodeCoverageMetric::Statement, 2U, 1U, 0U, 1U, 0U, CodeCoverageStatus::Partial },
            { CodeCoverageMetric::Branch, 1U, 0U, 0U, 0U, 1U, CodeCoverageStatus::Excluded },
            { CodeCoverageMetric::Line, 3U, 1U, 1U, 1U, 0U, CodeCoverageStatus::Partial },
        },
    };
}

void require_error(const fsim::runtime::CodeCoverageModelError expected,
    const fsim::runtime::CodeCoverageRun& run,
    const fsim::runtime::CodeCoverageResult& result,
    const std::string_view message,
    const fsim::runtime::CodeCoverageModelLimits limits = { })
{
    const auto validation = fsim::runtime::validate_code_coverage(
        run, result, limits);
    require(validation.error == expected, message);
}

void test_typed_identity_and_names()
{
    using namespace fsim::runtime;
    static_assert(!std::is_same_v<CodeCoveragePointId, CodeCoverageRunId>);
    static_assert(!std::is_same_v<CodeCoveragePointId, CodeCoverageCounterId>);
    require(kCodeCoverageModelDiagnostic == "FSIM-COV-001",
        "coverage model diagnostic identity must remain stable");
    require(code_coverage_metric_name(CodeCoverageMetric::Statement)
                == "statement"
            && code_coverage_metric_name(CodeCoverageMetric::Branch) == "branch"
            && code_coverage_metric_name(CodeCoverageMetric::Line) == "line",
        "metric identities must be language neutral and stable");
    require(code_coverage_metric_name(
                static_cast<CodeCoverageMetric>(255U))
                .empty(),
        "unknown metric identities must not acquire a spelling");
}

void test_valid_model()
{
    using namespace fsim::runtime;
    const auto run = make_run();
    const auto result = make_result();
    require(validate_code_coverage(run, result).ok(),
        "a bounded aligned run and result must validate");

    const CodeCoverageRun empty_run {
        .id = { 1U, 1U },
        .points = { },
    };
    const CodeCoverageResult empty_result {
        .run = empty_run.id,
        .points = { },
        .metrics = { },
    };
    require(validate_code_coverage(empty_run, empty_result).ok(),
        "an identified empty run must validate without synthetic metrics");
}

void test_identity_and_resource_rejections()
{
    using namespace fsim::runtime;
    auto run = make_run();
    auto result = make_result();

    run.id = { };
    require_error(CodeCoverageModelError::InvalidRunIdentity, run, result,
        "an empty run identity must be rejected");
    run = make_run();
    result.run.low ^= 1U;
    require_error(CodeCoverageModelError::ResultRunMismatch, run, result,
        "a result from another run must be rejected");
    result = make_result();
    result.points.pop_back();
    require_error(CodeCoverageModelError::ResultCountMismatch, run, result,
        "point results must retain complete run ownership");
    result = make_result();
    require_error(CodeCoverageModelError::ResourceLimit, run, result,
        "the point budget must bound run ownership", { 2U, 3U });
    require_error(CodeCoverageModelError::ResourceLimit, run, result,
        "the metric-result budget must be bounded", { 3U, 2U });
}

void test_point_rejections()
{
    using namespace fsim::runtime;
    auto run = make_run();
    auto result = make_result();

    run.points[0].id = { };
    require_error(CodeCoverageModelError::InvalidPointIdentity, run, result,
        "an empty point identity must be rejected");
    run = make_run();
    run.points[0].metric = CodeCoverageMetric::Line;
    require_error(CodeCoverageModelError::InvalidMetric, run, result,
        "derived line metrics must not own execution counters");
    run = make_run();
    run.points[1].counter.value = 0U;
    require_error(CodeCoverageModelError::CounterOwnershipMismatch, run,
        result, "counter ownership must be dense and unique within a run");
    run = make_run();
    result.points[0].point.low ^= 1U;
    require_error(CodeCoverageModelError::PointResultMismatch, run, result,
        "point results must retain their typed point identity");
    result = make_result();
    result.points[0].hits = 0U;
    require_error(CodeCoverageModelError::InvalidPointStatus, run, result,
        "covered points must have at least one hit");
    result = make_result();
    result.points[1].status = CodeCoverageStatus::Partial;
    require_error(CodeCoverageModelError::InvalidPointStatus, run, result,
        "individual execution points cannot be partially covered");
    result = make_result();
    result.points[2].hits = 1U;
    require_error(CodeCoverageModelError::InvalidPointStatus, run, result,
        "excluded points cannot retain scored hits");
}

void test_metric_rejections()
{
    using namespace fsim::runtime;
    const auto run = make_run();
    auto result = make_result();

    result.metrics[0].total = 1U;
    require_error(CodeCoverageModelError::InvalidMetricCounts, run, result,
        "metric components must form an exact bounded total");
    result = make_result();
    result.metrics[0].status = CodeCoverageStatus::Covered;
    require_error(CodeCoverageModelError::InvalidMetricStatus, run, result,
        "metric status must be derived from its component counts");
    result = make_result();
    result.metrics[1] = result.metrics[0];
    require_error(CodeCoverageModelError::DuplicateMetricResult, run, result,
        "a run must not publish duplicate metric results");
    result = make_result();
    result.metrics.erase(result.metrics.begin());
    require_error(CodeCoverageModelError::MissingMetricResult, run, result,
        "every owned point metric must have one metric result");
    result = make_result();
    result.metrics[0].covered = 2U;
    result.metrics[0].uncovered = 0U;
    result.metrics[0].status = CodeCoverageStatus::Covered;
    require_error(CodeCoverageModelError::MetricResultMismatch, run, result,
        "point and metric results must agree exactly");
}

} // namespace

int main()
{
    test_typed_identity_and_names();
    test_valid_model();
    test_identity_and_resource_rejections();
    test_point_rejections();
    test_metric_rejections();
    return 0;
}
