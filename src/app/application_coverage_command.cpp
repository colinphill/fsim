// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"

#include "fsim/artifact/coverage_database_codec.hpp"
#include "fsim/artifact/coverage_database_merge.hpp"
#include "fsim/artifact/coverage_database_partial_merge.hpp"
#include "fsim/artifact/coverage_report_model.hpp"
#include "fsim/artifact/coverage_report_projection.hpp"
#include "fsim/artifact/coverage_report_render.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fsim::app::application_detail {
namespace {

    constexpr std::string_view kDiagnostic = "FSIM-COV-047";
    constexpr int kSuccess = 0;
    constexpr int kFailure = 1;
    constexpr int kThresholdFailure = 4;

    enum class ThresholdResult {
        Passed,
        Failed,
        Invalid,
    };

    void fail(diagnostic::Engine& diagnostics, std::string message)
    {
        diagnostics.error(std::string { kDiagnostic }, std::move(message));
    }

    std::optional<artifact::CoverageDatabaseMetricFamily> metric_family(
        const std::string_view spelling) noexcept
    {
        using Family = artifact::CoverageDatabaseMetricFamily;
        static constexpr std::array families { Family::Statement,
            Family::Branch, Family::Line, Family::Condition,
            Family::Expression, Family::Toggle, Family::FsmState,
            Family::FsmTransition, Family::SystemVerilogCoverpoint,
            Family::SystemVerilogCross, Family::PslDirective,
            Family::PslProperty };
        const auto found = std::ranges::find_if(families, [&](const auto family) {
            return artifact::coverage_database_metric_family_name(family)
                == spelling;
        });
        return found == families.end() ? std::nullopt
                                       : std::optional { *found };
    }

    std::uint64_t minimum_covered(
        const std::uint64_t total, const std::uint32_t percent) noexcept
    {
        const auto whole = total / 100U;
        const auto remainder = total % 100U;
        return whole * percent
            + (remainder * percent + 99U) / 100U;
    }

    ThresholdResult evaluate_thresholds(
        const artifact::CoverageDatabaseContents& contents,
        const std::span<const cli::CoverageThreshold> thresholds,
        diagnostic::Engine& diagnostics)
    {
        if (thresholds.empty())
            return ThresholdResult::Passed;
        const auto made = artifact::make_coverage_report_model(contents);
        if (!made.ok()) {
            fail(diagnostics,
                "cannot evaluate coverage thresholds from the invalid report model");
            return ThresholdResult::Invalid;
        }
        bool passed = true;
        for (const auto& threshold : thresholds) {
            const auto family = metric_family(threshold.metric);
            if (!family.has_value()) {
                fail(diagnostics,
                    "unknown coverage threshold metric '" + threshold.metric
                        + "'");
                return ThresholdResult::Invalid;
            }
            const auto summary = std::ranges::find(
                made.report->combined.metrics, *family,
                &artifact::CoverageReportMetric::family);
            const auto scored = summary == made.report->combined.metrics.end()
                ? 0U
                : summary->total - summary->excluded;
            const auto covered = summary == made.report->combined.metrics.end()
                ? 0U
                : summary->covered;
            const auto required
                = minimum_covered(scored, threshold.percent);
            if ((scored == 0U && threshold.percent != 0U)
                || covered < required) {
                fail(diagnostics,
                    "coverage threshold '" + threshold.metric + "="
                        + std::to_string(threshold.percent)
                        + "' failed: " + std::to_string(covered) + "/"
                        + std::to_string(scored) + " scored points covered");
                passed = false;
            }
        }
        return passed ? ThresholdResult::Passed : ThresholdResult::Failed;
    }

    bool write_text_atomically(const std::filesystem::path& path,
        const std::string_view contents)
    {
        if (path.empty()
            || contents.size()
                > static_cast<std::size_t>(
                    std::numeric_limits<std::streamsize>::max())) {
            return false;
        }
        std::error_code error;
        if (!path.parent_path().empty()) {
            std::filesystem::create_directories(path.parent_path(), error);
            if (error)
                return false;
        }
        auto temporary = path;
        temporary += ".fsim-tmp";
        auto backup = path;
        backup += ".fsim-old";
        const auto destination_exists = std::filesystem::exists(path, error);
        if (error)
            return false;
        const auto backup_exists = std::filesystem::exists(backup, error);
        if (error)
            return false;
        if (!destination_exists && backup_exists) {
            std::filesystem::rename(backup, path, error);
            if (error)
                return false;
        } else if (backup_exists) {
            std::filesystem::remove(backup, error);
            if (error)
                return false;
        }
        std::filesystem::remove(temporary, error);
        if (error)
            return false;
        {
            std::ofstream output(
                temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                return false;
            output.write(contents.data(),
                static_cast<std::streamsize>(contents.size()));
            if (!output) {
                output.close();
                std::filesystem::remove(temporary, error);
                return false;
            }
        }
        const auto exists = std::filesystem::exists(path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return false;
        }
        if (exists) {
            std::filesystem::rename(path, backup, error);
            if (error) {
                std::filesystem::remove(temporary, error);
                return false;
            }
        }
        std::filesystem::rename(temporary, path, error);
        if (error) {
            if (exists) {
                std::error_code restore_error;
                std::filesystem::rename(backup, path, restore_error);
            }
            std::filesystem::remove(temporary, error);
            return false;
        }
        if (exists) {
            std::filesystem::remove(backup, error);
            if (error)
                return false;
        }
        return true;
    }

    std::optional<std::string> render_report(
        const artifact::CoverageDatabaseContents& contents,
        const std::string_view format)
    {
        if (format == "lcov" || format == "cobertura") {
            auto projected = artifact::project_coverage_report(contents,
                format == "lcov"
                    ? artifact::CoverageReportProjectionFormat::Lcov
                    : artifact::CoverageReportProjectionFormat::Cobertura);
            return projected.ok() ? std::move(projected.output) : std::nullopt;
        }
        const auto rendered = artifact::render_coverage_report(contents,
            format == "html"       ? artifact::CoverageReportFormat::Html
                : format == "json" ? artifact::CoverageReportFormat::Json
                                   : artifact::CoverageReportFormat::Text);
        return rendered.ok() ? std::move(rendered.output) : std::nullopt;
    }

    std::optional<std::vector<artifact::CoverageDatabaseContents>>
    read_inputs(const std::span<const std::filesystem::path> paths,
        diagnostic::Engine& diagnostics)
    {
        std::vector<artifact::CoverageDatabaseContents> result;
        result.reserve(paths.size());
        for (const auto& path : paths) {
            auto read = artifact::read_coverage_database(path);
            if (!read.ok()) {
                fail(diagnostics, "cannot read coverage database '" + support::path_to_utf8(path) + "'");
                return std::nullopt;
            }
            result.push_back(std::move(*read.contents));
        }
        return result;
    }

} // namespace

int handle_coverage_merge(const cli::Invocation& invocation,
    const project::Config&, diagnostic::Engine& diagnostics,
    std::ostream& output, std::ostream&)
{
    auto inputs = read_inputs(
        std::span<const std::filesystem::path> { invocation.files },
        diagnostics);
    if (!inputs.has_value())
        return kFailure;
    std::optional<artifact::CoverageDatabaseContents> merged;
    if (invocation.coverage_partial_merge) {
        auto result = artifact::merge_coverage_databases_partially(
            inputs->front(),
            std::span<const artifact::CoverageDatabaseContents> { *inputs }
                .subspan(1U));
        if (result.ok())
            merged = std::move(result.contents);
    } else {
        auto result = artifact::merge_coverage_databases(*inputs);
        if (result.ok())
            merged = std::move(result.contents);
    }
    if (!merged.has_value()) {
        fail(diagnostics,
            "coverage database merge failed without publishing partial output");
        return kFailure;
    }
    if (!artifact::write_coverage_database_atomically(
            *invocation.artifact_output, std::move(*merged))
            .ok()) {
        fail(diagnostics, "cannot atomically write merged coverage database '" + support::path_to_utf8(*invocation.artifact_output) + "'");
        return kFailure;
    }
    output << "coverage merge wrote " << inputs->size() << " input(s) to "
           << support::path_to_utf8(*invocation.artifact_output) << '\n';
    return output ? kSuccess : kFailure;
}

int handle_coverage_report(const cli::Invocation& invocation,
    const project::Config&, diagnostic::Engine& diagnostics,
    std::ostream& output, std::ostream&)
{
    const auto read = artifact::read_coverage_database(invocation.files.front());
    if (!read.ok()) {
        fail(diagnostics, "cannot read coverage database '" + support::path_to_utf8(invocation.files.front()) + "'");
        return kFailure;
    }
    const auto format
        = invocation.coverage_report_format.value_or("text");
    auto report = render_report(*read.contents, format);
    if (!report.has_value()) {
        fail(diagnostics,
            "coverage report construction failed without publishing partial output");
        return kFailure;
    }
    if (invocation.artifact_output.has_value()) {
        if (!write_text_atomically(*invocation.artifact_output, *report)) {
            fail(diagnostics, "cannot atomically write coverage report '" + support::path_to_utf8(*invocation.artifact_output) + "'");
            return kFailure;
        }
    } else {
        output << *report;
        if (!output) {
            fail(diagnostics, "cannot write coverage report to standard output");
            return kFailure;
        }
    }
    switch (evaluate_thresholds(
        *read.contents, invocation.coverage_thresholds, diagnostics)) {
    case ThresholdResult::Passed:
        return kSuccess;
    case ThresholdResult::Failed:
        return kThresholdFailure;
    case ThresholdResult::Invalid:
        return kFailure;
    }
    return kFailure;
}

} // namespace fsim::app::application_detail
