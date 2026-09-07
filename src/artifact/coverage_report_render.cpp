// SPDX-License-Identifier: Apache-2.0
#include "coverage_report_render_internal.hpp"

#include <new>

namespace fsim::artifact {

CoverageReportRenderResult render_coverage_report(
    const CoverageDatabaseContents& contents, const CoverageReportFormat format,
    const CoverageReportRenderLimits limits) noexcept
{
    if (format != CoverageReportFormat::Text
        && format != CoverageReportFormat::Html
        && format != CoverageReportFormat::Json) {
        return { std::nullopt,
            CoverageReportRenderError::InvalidReportFormat };
    }
    const auto made = make_coverage_report_model(contents, limits.model);
    if (!made.ok()) {
        return { std::nullopt, CoverageReportRenderError::InvalidReportModel,
            made.error, made.database_error, made.exclusion_error, made.index };
    }
    try {
        coverage_report_render_detail::Writer writer(
            limits.maximum_output_bytes);
        switch (format) {
        case CoverageReportFormat::Text:
            coverage_report_render_detail::render_text(writer, *made.report);
            break;
        case CoverageReportFormat::Html:
            coverage_report_render_detail::render_html(writer, *made.report);
            break;
        case CoverageReportFormat::Json:
            coverage_report_render_detail::render_json(writer, *made.report);
            break;
        default:
            return { std::nullopt,
                CoverageReportRenderError::InvalidReportFormat };
        }
        if (!writer.ok()) {
            return { std::nullopt, CoverageReportRenderError::ResourceLimit };
        }
        return { writer.take(), CoverageReportRenderError::None };
    } catch (const std::bad_alloc&) {
        return { std::nullopt,
            CoverageReportRenderError::AllocationFailure };
    } catch (...) {
        return { std::nullopt, CoverageReportRenderError::ResourceLimit };
    }
}

} // namespace fsim::artifact
