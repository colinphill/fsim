// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_line_state.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

fsim::runtime::CodeCoverageStatementLineSite site(
    const std::uint64_t id, const std::uint32_t counter,
    const std::size_t source, const std::uint64_t line)
{
    return {
        .point = { id, id * 17U },
        .counter = { counter },
        .source_index = source,
        .line = line,
    };
}

fsim::runtime::CodeCoveragePointResult point_result(
    const fsim::runtime::CodeCoverageStatementLineSite& owned,
    const fsim::runtime::CodeCoverageStatus status)
{
    return {
        .point = owned.point,
        .metric = fsim::runtime::CodeCoverageMetric::Statement,
        .counter = owned.counter,
        .hits = status == fsim::runtime::CodeCoverageStatus::Covered ? 1U
                                                                      : 0U,
        .status = status,
    };
}

struct Corpus {
    std::vector<fsim::runtime::CodeCoverageStatementLineSite> sites;
    std::vector<fsim::runtime::CodeCoveragePointResult> results;
};

Corpus corpus()
{
    using namespace fsim::runtime;
    Corpus value;
    const auto append = [&](const std::uint64_t id,
                            const std::uint32_t counter,
                            const std::size_t source,
                            const std::uint64_t line,
                            const CodeCoverageStatus status) {
        value.sites.push_back(site(id, counter, source, line));
        value.results.push_back(point_result(value.sites.back(), status));
    };
    append(5U, 4U, 1U, 5U, CodeCoverageStatus::Uncovered);
    append(1U, 0U, 0U, 10U, CodeCoverageStatus::Covered);
    append(6U, 5U, 1U, 6U, CodeCoverageStatus::Excluded);
    append(3U, 2U, 0U, 20U, CodeCoverageStatus::Covered);
    append(2U, 1U, 0U, 10U, CodeCoverageStatus::Uncovered);
    append(7U, 6U, 1U, 7U, CodeCoverageStatus::Covered);
    append(8U, 7U, 1U, 7U, CodeCoverageStatus::Excluded);
    append(9U, 8U, 1U, 8U, CodeCoverageStatus::Uncovered);
    append(10U, 9U, 1U, 8U, CodeCoverageStatus::Excluded);
    append(4U, 3U, 0U, 20U, CodeCoverageStatus::Covered);
    return value;
}

void require_error(const fsim::runtime::CodeCoverageLineStateError expected,
    const Corpus& input, const std::string_view message,
    const fsim::runtime::CodeCoverageLineStateLimits limits = { })
{
    const auto derived = fsim::runtime::derive_code_coverage_line_states(
        input.sites, input.results, limits);
    require(!derived.ok() && derived.error == expected
            && derived.lines.empty()
            && derived.metric.metric
                == fsim::runtime::CodeCoverageMetric::Line
            && derived.metric.total == 0U,
        message);
}

void test_line_states_and_metric()
{
    using namespace fsim::runtime;
    const auto input = corpus();
    const auto derived
        = derive_code_coverage_line_states(input.sites, input.results);
    require(derived.ok() && derived.lines.size() == 6U,
        "statement points must produce one deterministic state per owned line");
    const std::vector expected {
        CodeCoverageLineState { 0U, 10U, 2U, 1U, 1U, 0U,
            CodeCoverageStatus::Partial },
        CodeCoverageLineState { 0U, 20U, 2U, 2U, 0U, 0U,
            CodeCoverageStatus::Covered },
        CodeCoverageLineState { 1U, 5U, 1U, 0U, 1U, 0U,
            CodeCoverageStatus::Uncovered },
        CodeCoverageLineState { 1U, 6U, 1U, 0U, 0U, 1U,
            CodeCoverageStatus::Excluded },
        CodeCoverageLineState { 1U, 7U, 2U, 1U, 0U, 1U,
            CodeCoverageStatus::Covered },
        CodeCoverageLineState { 1U, 8U, 2U, 0U, 1U, 1U,
            CodeCoverageStatus::Uncovered },
    };
    require(derived.lines == expected,
        "covered, partial, uncovered, and excluded line scoring must ignore excluded points");
    require(derived.metric.metric == CodeCoverageMetric::Line
            && derived.metric.total == 6U
            && derived.metric.covered == 2U
            && derived.metric.partial == 1U
            && derived.metric.uncovered == 2U
            && derived.metric.excluded == 1U
            && derived.metric.status == CodeCoverageStatus::Partial,
        "derived line components must form the exact line metric without counters");

    auto reordered = input;
    std::ranges::reverse(reordered.sites);
    std::ranges::reverse(reordered.results);
    const auto repeated = derive_code_coverage_line_states(
        reordered.sites, reordered.results);
    require(repeated.ok() && repeated.lines == derived.lines
            && repeated.metric.total == derived.metric.total
            && repeated.metric.covered == derived.metric.covered
            && repeated.metric.partial == derived.metric.partial
            && repeated.metric.uncovered == derived.metric.uncovered
            && repeated.metric.excluded == derived.metric.excluded
            && repeated.metric.status == derived.metric.status,
        "line output must be independent of statement discovery order");

    const std::vector<CodeCoverageStatementLineSite> no_sites;
    const std::vector<CodeCoveragePointResult> no_results;
    const auto empty = derive_code_coverage_line_states(no_sites, no_results);
    require(empty.ok() && empty.lines.empty() && empty.metric.total == 0U
            && empty.metric.status == CodeCoverageStatus::Empty,
        "an empty executable inventory must not manufacture lines or counters");
}

void test_identity_and_alignment_rejections()
{
    using namespace fsim::runtime;
    auto input = corpus();
    input.results.pop_back();
    require_error(CodeCoverageLineStateError::ResultCountMismatch, input,
        "line derivation requires one aligned result per statement point");

    input = corpus();
    input.sites[0].point = { };
    require_error(CodeCoverageLineStateError::InvalidPointIdentity, input,
        "line sites must retain valid statement identities");
    input = corpus();
    input.sites[1] = input.sites[0];
    input.results[1] = input.results[0];
    require_error(CodeCoverageLineStateError::DuplicatePoint, input,
        "one executable statement point must not contribute twice");
    input = corpus();
    input.results[0].point.low ^= 1U;
    require_error(CodeCoverageLineStateError::PointResultMismatch, input,
        "line sites and statement results must retain the same point owner");
    input = corpus();
    ++input.results[0].counter.value;
    require_error(CodeCoverageLineStateError::PointResultMismatch, input,
        "line sites and statement results must retain the same counter owner");
    input = corpus();
    input.results[0].metric = CodeCoverageMetric::Branch;
    require_error(CodeCoverageLineStateError::PointResultMismatch, input,
        "branch results must not enter statement-derived line scoring");
}

void test_status_and_resource_rejections()
{
    using namespace fsim::runtime;
    auto input = corpus();
    input.sites[0].line = 0U;
    require_error(CodeCoverageLineStateError::InvalidLineNumber, input,
        "physical source lines are one based");

    input = corpus();
    input.results[0].status = CodeCoverageStatus::Partial;
    require_error(CodeCoverageLineStateError::InvalidPointStatus, input,
        "individual statement points cannot be partially covered");
    input = corpus();
    input.results[0].status = static_cast<CodeCoverageStatus>(255U);
    require_error(CodeCoverageLineStateError::InvalidPointStatus, input,
        "unknown statement states must be rejected");
    input = corpus();
    input.results[1].hits = 0U;
    require_error(CodeCoverageLineStateError::InvalidPointStatus, input,
        "covered statement points require at least one hit");
    input = corpus();
    input.results[0].hits = 1U;
    require_error(CodeCoverageLineStateError::InvalidPointStatus, input,
        "uncovered statement points cannot retain hits");
    input = corpus();
    input.results[2].hits = 1U;
    require_error(CodeCoverageLineStateError::InvalidPointStatus, input,
        "excluded statement points cannot retain scored hits");

    input = corpus();
    auto limits = CodeCoverageLineStateLimits { };
    limits.maximum_statement_points = input.sites.size() - 1U;
    require_error(CodeCoverageLineStateError::ResourceLimit, input,
        "statement ownership must be bounded before allocation", limits);
    limits = CodeCoverageLineStateLimits { };
    limits.maximum_sources = 1U;
    require_error(CodeCoverageLineStateError::ResourceLimit, input,
        "source indices must obey the configured inventory ceiling", limits);
    limits = CodeCoverageLineStateLimits { };
    limits.maximum_line_number = 7U;
    require_error(CodeCoverageLineStateError::ResourceLimit, input,
        "physical line numbers must obey the configured ceiling", limits);
    limits = CodeCoverageLineStateLimits { };
    limits.maximum_lines = 5U;
    require_error(CodeCoverageLineStateError::ResourceLimit, input,
        "unique derived lines must obey the configured ceiling", limits);
    require(kCodeCoverageLineStateDiagnostic == "FSIM-COV-007",
        "line-state diagnostic identity must remain stable");
}

} // namespace

int main()
{
    test_line_states_and_metric();
    test_identity_and_alignment_rejections();
    test_status_and_resource_rejections();
    return 0;
}
