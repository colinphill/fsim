// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_line_state.hpp"

#include <algorithm>
#include <tuple>
#include <vector>

namespace fsim::runtime {
namespace {

struct LineContribution {
    CodeCoveragePointId point;
    std::size_t source_index { };
    std::uint64_t line { };
    CodeCoverageStatus status { CodeCoverageStatus::Uncovered };
};

constexpr auto point_key(const LineContribution& contribution) noexcept
{
    return std::tie(contribution.point.high, contribution.point.low);
}

constexpr auto line_key(const LineContribution& contribution) noexcept
{
    return std::tie(contribution.source_index, contribution.line,
        contribution.point.high, contribution.point.low);
}

constexpr CodeCoverageStatus derive_line_status(
    const CodeCoverageLineState& line) noexcept
{
    if (line.excluded == line.total) {
        return CodeCoverageStatus::Excluded;
    }
    const auto scored = line.total - line.excluded;
    if (line.covered == scored) {
        return CodeCoverageStatus::Covered;
    }
    if (line.covered == 0U) {
        return CodeCoverageStatus::Uncovered;
    }
    return CodeCoverageStatus::Partial;
}

} // namespace

CodeCoverageLineStateResult derive_code_coverage_line_states(
    const std::span<const CodeCoverageStatementLineSite> statement_sites,
    const std::span<const CodeCoveragePointResult> statement_results,
    const CodeCoverageLineStateLimits limits) noexcept
{
    CodeCoverageLineStateResult result;
    const auto reject = [&](const CodeCoverageLineStateError error,
                            const std::size_t index = 0U) {
        result.lines.clear();
        result.metric = CodeCoverageMetricResult {
            .metric = CodeCoverageMetric::Line,
        };
        result.error = error;
        result.index = index;
        return result;
    };

    try {
        if (statement_sites.size() > limits.maximum_statement_points
            || statement_results.size() > limits.maximum_statement_points) {
            return reject(CodeCoverageLineStateError::ResourceLimit);
        }
        if (statement_sites.size() != statement_results.size()) {
            return reject(
                CodeCoverageLineStateError::ResultCountMismatch);
        }

        std::vector<LineContribution> contributions;
        contributions.reserve(statement_sites.size());
        for (std::size_t index = 0U; index < statement_sites.size(); ++index) {
            const auto& site = statement_sites[index];
            const auto& point_result = statement_results[index];
            if (!is_code_coverage_identity_valid(site.point)) {
                return reject(
                    CodeCoverageLineStateError::InvalidPointIdentity, index);
            }
            if (site.source_index >= limits.maximum_sources
                || site.line > limits.maximum_line_number) {
                return reject(
                    CodeCoverageLineStateError::ResourceLimit, index);
            }
            if (site.line == 0U) {
                return reject(
                    CodeCoverageLineStateError::InvalidLineNumber, index);
            }
            if (point_result.point != site.point
                || point_result.counter != site.counter
                || point_result.metric != CodeCoverageMetric::Statement) {
                return reject(
                    CodeCoverageLineStateError::PointResultMismatch, index);
            }
            if (!is_code_coverage_status_valid(point_result.status)
                || point_result.status == CodeCoverageStatus::Empty
                || point_result.status == CodeCoverageStatus::Partial
                || (point_result.status == CodeCoverageStatus::Covered
                    && point_result.hits == 0U)
                || (point_result.status != CodeCoverageStatus::Covered
                    && point_result.hits != 0U)) {
                return reject(
                    CodeCoverageLineStateError::InvalidPointStatus, index);
            }
            contributions.push_back(LineContribution {
                site.point,
                site.source_index,
                site.line,
                point_result.status,
            });
        }

        std::ranges::sort(contributions, { }, point_key);
        for (std::size_t index = 1U; index < contributions.size(); ++index) {
            if (contributions[index - 1U].point
                == contributions[index].point) {
                return reject(
                    CodeCoverageLineStateError::DuplicatePoint, index);
            }
        }
        std::ranges::sort(contributions, { }, line_key);

        result.lines.reserve(
            std::min(contributions.size(), limits.maximum_lines));
        for (const auto& contribution : contributions) {
            if (result.lines.empty()
                || result.lines.back().source_index
                        != contribution.source_index
                || result.lines.back().line != contribution.line) {
                if (result.lines.size() >= limits.maximum_lines) {
                    return reject(CodeCoverageLineStateError::ResourceLimit);
                }
                result.lines.push_back(CodeCoverageLineState {
                    .source_index = contribution.source_index,
                    .line = contribution.line,
                });
            }
            auto& line = result.lines.back();
            ++line.total;
            if (contribution.status == CodeCoverageStatus::Covered) {
                ++line.covered;
            } else if (contribution.status == CodeCoverageStatus::Excluded) {
                ++line.excluded;
            } else {
                ++line.uncovered;
            }
        }

        result.metric.total = result.lines.size();
        for (auto& line : result.lines) {
            line.status = derive_line_status(line);
            if (line.status == CodeCoverageStatus::Covered) {
                ++result.metric.covered;
            } else if (line.status == CodeCoverageStatus::Partial) {
                ++result.metric.partial;
            } else if (line.status == CodeCoverageStatus::Excluded) {
                ++result.metric.excluded;
            } else {
                ++result.metric.uncovered;
            }
        }
        result.metric.status = code_coverage_metric_status(result.metric);
        return result;
    } catch (...) {
        return reject(CodeCoverageLineStateError::ResourceLimit);
    }
}

} // namespace fsim::runtime
