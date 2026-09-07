// SPDX-License-Identifier: Apache-2.0
#include "coverage_report_render_internal.hpp"

#include <span>

namespace fsim::artifact::coverage_report_render_detail {
namespace {

    void metric_row(Writer& writer, const CoverageReportMetric& value)
    {
        writer.append("<tr><td>");
        escaped_html(writer, coverage_database_namespace_name(value.name_space));
        writer.append("</td><td>");
        escaped_html(writer, coverage_database_metric_family_name(value.family));
        writer.append("</td><td>");
        writer.number(value.total);
        writer.append("</td><td>");
        writer.number(value.covered);
        writer.append("</td><td>");
        writer.number(value.uncovered);
        writer.append("</td><td>");
        writer.number(value.excluded);
        writer.append("</td><td>");
        writer.number(value.hits);
        writer.append("</td><td>");
        writer.number(value.excluded_hits);
        writer.append("</td><td>");
        boolean(writer, value.hits_saturated);
        writer.append("</td><td>");
        boolean(writer, value.excluded_hits_saturated);
        writer.append("</td></tr>\n");
    }

    void metrics(Writer& writer,
        const std::span<const CoverageReportMetric> values)
    {
        writer.append("<table><thead><tr><th>namespace</th><th>family</th>"
                      "<th>total</th><th>covered</th><th>uncovered</th>"
                      "<th>excluded</th><th>hits</th><th>excluded hits</th>"
                      "<th>hits saturated</th><th>excluded hits saturated</th>"
                      "</tr></thead><tbody>\n");
        for (const auto& value : values)
            metric_row(writer, value);
        writer.append("</tbody></table>\n");
    }

    void points(Writer& writer,
        const std::span<const CoverageReportPoint> values)
    {
        writer.append("<table><thead><tr><th>namespace</th><th>family</th>"
                      "<th>point</th><th>source</th><th>instance</th>"
                      "<th>line</th>"
                      "<th>hits</th><th>excluded hits</th><th>hits saturated</th>"
                      "<th>excluded hits saturated</th><th>status</th>"
                      "</tr></thead><tbody>\n");
        for (const auto& value : values) {
            writer.append("<tr><td>");
            escaped_html(
                writer, coverage_database_namespace_name(value.name_space));
            writer.append("</td><td>");
            escaped_html(
                writer, coverage_database_metric_family_name(value.family));
            writer.append("</td><td>");
            writer.identity(value.point_identity);
            writer.append("</td><td>");
            writer.identity(value.source_identity);
            writer.append("</td><td>");
            writer.identity(value.instance_identity);
            writer.append("</td><td>");
            writer.number(value.source_line);
            writer.append("</td><td>");
            writer.number(value.hits);
            writer.append("</td><td>");
            writer.number(value.excluded_hits);
            writer.append("</td><td>");
            boolean(writer, value.hits_saturated);
            writer.append("</td><td>");
            boolean(writer, value.excluded_hits_saturated);
            writer.append("</td><td>");
            escaped_html(writer, status_name(value.status));
            writer.append("</td></tr>\n");
        }
        writer.append("</tbody></table>\n");
    }

} // namespace

void render_html(Writer& writer, const CoverageReportModel& report)
{
    writer.append("<!doctype html>\n<html lang=\"en\"><head>"
                  "<meta charset=\"utf-8\"><title>fsim coverage report</title>"
                  "</head><body>\n<h1>fsim coverage report</h1>\n"
                  "<h2>Combined</h2>\n");
    metrics(writer, report.combined.metrics);
    writer.append("<h2>Sources</h2>\n");
    for (const auto& source : report.sources) {
        writer.append("<section><h3>");
        escaped_html(writer, source.logical_path);
        writer.append("</h3><p>source identity: <code>");
        writer.identity(source.source_identity);
        writer.append("</code></p>\n");
        metrics(writer, source.metrics);
        points(writer, source.points);
        writer.append("</section>\n");
    }
    writer.append("<h2>Instances</h2>\n");
    for (const auto& instance : report.instances) {
        writer.append("<section><h3><code>");
        writer.identity(instance.instance_identity);
        writer.append("</code></h3>\n");
        metrics(writer, instance.metrics);
        points(writer, instance.points);
        writer.append("</section>\n");
    }
    writer.append("<h2>Exclusions</h2><p>total reasons: ");
    writer.number(report.exclusions.total_reasons);
    writer.append("</p><table><thead><tr><th>namespace</th><th>family</th>"
                  "<th>scope</th><th>point</th><th>source</th><th>instance</th>"
                  "<th>line</th>"
                  "<th>reasons</th></tr></thead><tbody>\n");
    for (const auto& value : report.exclusions.points) {
        writer.append("<tr><td>");
        escaped_html(writer, coverage_database_namespace_name(value.name_space));
        writer.append("</td><td>");
        escaped_html(writer, coverage_database_metric_family_name(value.family));
        writer.append("</td><td>");
        escaped_html(writer, scope_name(value.scope));
        writer.append("</td><td>");
        writer.identity(value.point_identity);
        writer.append("</td><td>");
        writer.identity(value.source_identity);
        writer.append("</td><td>");
        writer.identity(value.instance_identity);
        writer.append("</td><td>");
        writer.number(value.source_line);
        writer.append("</td><td><ul>");
        for (const auto& reason : value.reasons) {
            writer.append("<li>");
            escaped_html(writer, reason);
            writer.append("</li>");
        }
        writer.append("</ul></td></tr>\n");
    }
    writer.append("</tbody></table>\n</body></html>\n");
}

} // namespace fsim::artifact::coverage_report_render_detail
