// SPDX-License-Identifier: Apache-2.0
#include "coverage_report_render_internal.hpp"

namespace fsim::artifact::coverage_report_render_detail {
namespace {

    void metric(Writer& writer, const CoverageReportMetric& value,
        const std::string_view prefix)
    {
        writer.append(prefix);
        writer.append("metric namespace=");
        writer.append(coverage_database_namespace_name(value.name_space));
        writer.append(" family=");
        writer.append(coverage_database_metric_family_name(value.family));
        writer.append(" total=");
        writer.number(value.total);
        writer.append(" covered=");
        writer.number(value.covered);
        writer.append(" uncovered=");
        writer.number(value.uncovered);
        writer.append(" excluded=");
        writer.number(value.excluded);
        writer.append(" hits=");
        writer.number(value.hits);
        writer.append(" excluded_hits=");
        writer.number(value.excluded_hits);
        writer.append(" hits_saturated=");
        boolean(writer, value.hits_saturated);
        writer.append(" excluded_hits_saturated=");
        boolean(writer, value.excluded_hits_saturated);
        writer.character('\n');
    }

    void point(Writer& writer, const CoverageReportPoint& value,
        const std::string_view prefix)
    {
        writer.append(prefix);
        writer.append("point namespace=");
        writer.append(coverage_database_namespace_name(value.name_space));
        writer.append(" family=");
        writer.append(coverage_database_metric_family_name(value.family));
        writer.append(" point=");
        writer.identity(value.point_identity);
        writer.append(" source=");
        writer.identity(value.source_identity);
        writer.append(" instance=");
        writer.identity(value.instance_identity);
        writer.append(" line=");
        writer.number(value.source_line);
        writer.append(" hits=");
        writer.number(value.hits);
        writer.append(" excluded_hits=");
        writer.number(value.excluded_hits);
        writer.append(" hits_saturated=");
        boolean(writer, value.hits_saturated);
        writer.append(" excluded_hits_saturated=");
        boolean(writer, value.excluded_hits_saturated);
        writer.append(" status=");
        writer.append(status_name(value.status));
        writer.character('\n');
    }

} // namespace

void render_text(Writer& writer, const CoverageReportModel& report)
{
    writer.append("fsim coverage report v3\ncombined\n");
    for (const auto& value : report.combined.metrics)
        metric(writer, value, "  ");
    writer.append("sources\n");
    for (const auto& source : report.sources) {
        writer.append("  source identity=");
        writer.identity(source.source_identity);
        writer.append(" path=");
        escaped_text(writer, source.logical_path);
        writer.character('\n');
        for (const auto& value : source.metrics)
            metric(writer, value, "    ");
        for (const auto& value : source.points)
            point(writer, value, "    ");
    }
    writer.append("instances\n");
    for (const auto& instance : report.instances) {
        writer.append("  instance identity=");
        writer.identity(instance.instance_identity);
        writer.character('\n');
        for (const auto& value : instance.metrics)
            metric(writer, value, "    ");
        for (const auto& value : instance.points)
            point(writer, value, "    ");
    }
    writer.append("exclusions total_reasons=");
    writer.number(report.exclusions.total_reasons);
    writer.character('\n');
    for (const auto& value : report.exclusions.points) {
        writer.append("  exclusion namespace=");
        writer.append(coverage_database_namespace_name(value.name_space));
        writer.append(" family=");
        writer.append(coverage_database_metric_family_name(value.family));
        writer.append(" scope=");
        writer.append(scope_name(value.scope));
        writer.append(" point=");
        writer.identity(value.point_identity);
        writer.append(" source=");
        writer.identity(value.source_identity);
        writer.append(" instance=");
        writer.identity(value.instance_identity);
        writer.append(" line=");
        writer.number(value.source_line);
        writer.character('\n');
        for (const auto& reason : value.reasons) {
            writer.append("    reason=");
            escaped_text(writer, reason);
            writer.character('\n');
        }
    }
}

} // namespace fsim::artifact::coverage_report_render_detail
