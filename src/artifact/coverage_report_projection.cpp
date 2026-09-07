// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_report_projection.hpp"

#include "coverage_report_render_internal.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <vector>

namespace fsim::artifact {
namespace {

    using coverage_report_render_detail::Writer;

    struct ProjectedBranch {
        std::uint64_t hits { };
    };

    struct ProjectedLine {
        std::uint64_t hits { };
        bool hits_saturated { };
        bool has_line_metric { };
        bool has_statement_metric { };
        bool covered { };
        std::vector<ProjectedBranch> branches;
    };

    struct SourceProjection {
        std::map<std::uint64_t, ProjectedLine> lines;
        std::size_t valid_lines { };
        std::size_t covered_lines { };
        std::size_t valid_branches { };
        std::size_t covered_branches { };
    };

    struct ProjectionBudget {
        std::size_t lines { };
        std::size_t branches { };
    };

    struct ProjectionFailure {
        CoverageReportProjectionError error {
            CoverageReportProjectionError::None
        };
        std::size_t index { };
    };

    bool projected_line_family(const CoverageReportPoint& point) noexcept
    {
        return point.name_space == CoverageDatabaseNamespace::Code
            && (point.family == CoverageDatabaseMetricFamily::Statement
                || point.family == CoverageDatabaseMetricFamily::Line);
    }

    bool projected_branch_family(const CoverageReportPoint& point) noexcept
    {
        return point.name_space == CoverageDatabaseNamespace::Code
            && point.family == CoverageDatabaseMetricFamily::Branch;
    }

    void saturating_add(std::uint64_t& target, const std::uint64_t value,
        bool& saturated) noexcept
    {
        if (value > std::numeric_limits<std::uint64_t>::max() - target) {
            target = std::numeric_limits<std::uint64_t>::max();
            saturated = true;
        } else {
            target += value;
        }
    }

    ProjectionFailure make_source_projection(const CoverageSourceReport& source,
        const CoverageReportProjectionLimits& limits, ProjectionBudget& budget,
        SourceProjection& result)
    {
        for (std::size_t index = 0U; index < source.points.size(); ++index) {
            const auto& point = source.points[index];
            if ((!projected_line_family(point)
                    && !projected_branch_family(point))
                || point.status == CoverageReportPointStatus::Excluded) {
                continue;
            }
            if (point.source_line == 0U) {
                return { CoverageReportProjectionError::MissingSourceLine,
                    index };
            }
            auto [line, inserted]
                = result.lines.try_emplace(point.source_line);
            if (inserted && ++budget.lines > limits.maximum_lines) {
                return { CoverageReportProjectionError::ResourceLimit,
                    index };
            }
            if (projected_branch_family(point)) {
                if (++budget.branches > limits.maximum_branches) {
                    return { CoverageReportProjectionError::ResourceLimit,
                        index };
                }
                line->second.branches.push_back({ point.hits });
                continue;
            }
            const auto explicit_line
                = point.family == CoverageDatabaseMetricFamily::Line;
            line->second.has_statement_metric
                = line->second.has_statement_metric || !explicit_line;
            if (explicit_line && !line->second.has_line_metric) {
                line->second.hits = 0U;
                line->second.hits_saturated = false;
                line->second.covered = false;
                line->second.has_line_metric = true;
            }
            if (!line->second.has_line_metric || explicit_line) {
                saturating_add(line->second.hits, point.hits,
                    line->second.hits_saturated);
                line->second.hits_saturated
                    = line->second.hits_saturated || point.hits_saturated;
                line->second.covered = line->second.covered
                    || point.status == CoverageReportPointStatus::Covered;
            }
        }
        for (const auto& [number, line] : result.lines) {
            static_cast<void>(number);
            if (line.has_line_metric || line.has_statement_metric) {
                ++result.valid_lines;
                if (line.covered)
                    ++result.covered_lines;
            }
            result.valid_branches += line.branches.size();
            result.covered_branches += static_cast<std::size_t>(
                std::ranges::count_if(line.branches,
                    [](const auto& branch) { return branch.hits != 0U; }));
        }
        return { };
    }

    bool valid_lcov_path(const std::string_view path) noexcept
    {
        return std::ranges::none_of(path, [](const char raw) {
            return static_cast<unsigned char>(raw) < 0x20U;
        });
    }

    bool valid_xml_path(const std::string_view path) noexcept
    {
        return std::ranges::none_of(path, [](const char raw) {
            const auto value = static_cast<unsigned char>(raw);
            return value < 0x20U && value != 0x09U && value != 0x0aU
                && value != 0x0dU;
        });
    }

    void rate(Writer& writer, const std::size_t covered,
        const std::size_t total)
    {
        if (total == 0U) {
            writer.append("1.000000");
            return;
        }
        writer.number(covered / total);
        writer.character('.');
        auto remainder = covered % total;
        for (std::size_t digit = 0U; digit < 6U; ++digit) {
            remainder *= 10U;
            writer.character(static_cast<char>('0' + remainder / total));
            remainder %= total;
        }
    }

    std::size_t branch_percent(
        const std::size_t covered, const std::size_t total) noexcept
    {
        return total == 0U ? 100U : (covered * 100U) / total;
    }

    ProjectionFailure render_lcov(Writer& writer,
        const CoverageReportModel& report,
        const CoverageReportProjectionLimits& limits)
    {
        ProjectionBudget budget;
        writer.append("TN:fsim\n");
        for (std::size_t source_index = 0U;
            source_index < report.sources.size(); ++source_index) {
            const auto& source = report.sources[source_index];
            if (!valid_lcov_path(source.logical_path)) {
                return { CoverageReportProjectionError::InvalidSourcePath,
                    source_index };
            }
            SourceProjection projection;
            const auto failure
                = make_source_projection(source, limits, budget, projection);
            if (failure.error != CoverageReportProjectionError::None)
                return { failure.error, source_index };
            writer.append("SF:");
            writer.append(source.logical_path);
            writer.character('\n');
            for (const auto& [number, line] : projection.lines) {
                const auto is_line
                    = line.has_line_metric || line.has_statement_metric;
                if (is_line) {
                    writer.append("DA:");
                    writer.number(number);
                    writer.character(',');
                    writer.number(line.hits);
                    writer.character('\n');
                }
                for (std::size_t branch_index = 0U;
                    branch_index < line.branches.size(); ++branch_index) {
                    const auto& branch = line.branches[branch_index];
                    writer.append("BRDA:");
                    writer.number(number);
                    writer.append(",0,");
                    writer.number(branch_index);
                    writer.character(',');
                    writer.number(branch.hits);
                    writer.character('\n');
                }
            }
            writer.append("LF:");
            writer.number(projection.valid_lines);
            writer.append("\nLH:");
            writer.number(projection.covered_lines);
            writer.append("\nBRF:");
            writer.number(projection.valid_branches);
            writer.append("\nBRH:");
            writer.number(projection.covered_branches);
            writer.append("\nend_of_record\n");
        }
        return { };
    }

    void cobertura_line(Writer& writer, const std::uint64_t number,
        const ProjectedLine& line, const bool is_line)
    {
        if (!is_line && line.branches.empty())
            return;
        writer.append("<line number=\"");
        writer.number(number);
        writer.append("\" hits=\"");
        writer.number(is_line ? line.hits : 0U);
        writer.append("\" branch=\"");
        writer.append(line.branches.empty() ? "false" : "true");
        writer.character('"');
        if (line.branches.empty()) {
            writer.append("/>\n");
            return;
        }
        const auto covered = static_cast<std::size_t>(std::ranges::count_if(
            line.branches,
            [](const auto& branch) { return branch.hits != 0U; }));
        writer.append(" condition-coverage=\"");
        writer.number(branch_percent(covered, line.branches.size()));
        writer.append("% (");
        writer.number(covered);
        writer.character('/');
        writer.number(line.branches.size());
        writer.append(")\"><conditions><condition number=\"0\" type=\"jump\" "
                      "coverage=\"");
        writer.number(branch_percent(covered, line.branches.size()));
        writer.append("%\"/></conditions></line>\n");
    }

    ProjectionFailure render_cobertura(Writer& writer,
        const CoverageReportModel& report,
        const CoverageReportProjectionLimits& limits)
    {
        SourceProjection totals;
        ProjectionBudget first_budget;
        for (std::size_t index = 0U; index < report.sources.size(); ++index) {
            if (!valid_xml_path(report.sources[index].logical_path)) {
                return { CoverageReportProjectionError::InvalidSourcePath,
                    index };
            }
            SourceProjection projection;
            const auto failure = make_source_projection(
                report.sources[index], limits, first_budget, projection);
            if (failure.error != CoverageReportProjectionError::None)
                return { failure.error, index };
            totals.valid_lines += projection.valid_lines;
            totals.covered_lines += projection.covered_lines;
            totals.valid_branches += projection.valid_branches;
            totals.covered_branches += projection.covered_branches;
        }
        writer.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<coverage line-rate=\"");
        rate(writer, totals.covered_lines, totals.valid_lines);
        writer.append("\" branch-rate=\"");
        rate(writer, totals.covered_branches, totals.valid_branches);
        writer.append("\" lines-covered=\"");
        writer.number(totals.covered_lines);
        writer.append("\" lines-valid=\"");
        writer.number(totals.valid_lines);
        writer.append("\" branches-covered=\"");
        writer.number(totals.covered_branches);
        writer.append("\" branches-valid=\"");
        writer.number(totals.valid_branches);
        writer.append("\" complexity=\"0\" version=\"fsim-3\" timestamp=\"0\">"
                      "<sources><source>.</source></sources><packages>"
                      "<package name=\"fsim\" line-rate=\"");
        rate(writer, totals.covered_lines, totals.valid_lines);
        writer.append("\" branch-rate=\"");
        rate(writer, totals.covered_branches, totals.valid_branches);
        writer.append("\" complexity=\"0\"><classes>\n");

        ProjectionBudget second_budget;
        for (std::size_t index = 0U; index < report.sources.size(); ++index) {
            const auto& source = report.sources[index];
            SourceProjection projection;
            const auto failure = make_source_projection(
                source, limits, second_budget, projection);
            if (failure.error != CoverageReportProjectionError::None)
                return { failure.error, index };
            writer.append("<class name=\"");
            coverage_report_render_detail::escaped_html(
                writer, source.logical_path);
            writer.append("\" filename=\"");
            coverage_report_render_detail::escaped_html(
                writer, source.logical_path);
            writer.append("\" line-rate=\"");
            rate(writer, projection.covered_lines, projection.valid_lines);
            writer.append("\" branch-rate=\"");
            rate(writer, projection.covered_branches,
                projection.valid_branches);
            writer.append("\" complexity=\"0\"><methods/><lines>\n");
            for (const auto& [number, line] : projection.lines) {
                const auto is_line
                    = line.has_line_metric || line.has_statement_metric;
                cobertura_line(writer, number, line, is_line);
            }
            writer.append("</lines></class>\n");
        }
        writer.append("</classes></package></packages></coverage>\n");
        return { };
    }

} // namespace

CoverageReportProjectionResult project_coverage_report(
    const CoverageDatabaseContents& contents,
    const CoverageReportProjectionFormat format,
    const CoverageReportProjectionLimits limits) noexcept
{
    if (format != CoverageReportProjectionFormat::Lcov
        && format != CoverageReportProjectionFormat::Cobertura) {
        return { std::nullopt,
            CoverageReportProjectionError::InvalidFormat };
    }
    const auto made = make_coverage_report_model(contents, limits.model);
    if (!made.ok()) {
        return { std::nullopt,
            CoverageReportProjectionError::InvalidReportModel, made.error,
            made.database_error, made.exclusion_error, made.index };
    }
    try {
        Writer writer(limits.maximum_output_bytes);
        const auto failure = format == CoverageReportProjectionFormat::Lcov
            ? render_lcov(writer, *made.report, limits)
            : render_cobertura(writer, *made.report, limits);
        if (failure.error != CoverageReportProjectionError::None) {
            return { std::nullopt, failure.error,
                CoverageReportModelError::None,
                CoverageDatabaseModelError::None,
                CoverageExclusionReportError::None, failure.index };
        }
        if (!writer.ok()) {
            return { std::nullopt,
                CoverageReportProjectionError::ResourceLimit };
        }
        return { writer.take(), CoverageReportProjectionError::None };
    } catch (const std::bad_alloc&) {
        return { std::nullopt,
            CoverageReportProjectionError::AllocationFailure };
    } catch (...) {
        return { std::nullopt,
            CoverageReportProjectionError::ResourceLimit };
    }
}

} // namespace fsim::artifact
