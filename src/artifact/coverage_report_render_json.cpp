// SPDX-License-Identifier: Apache-2.0
#include "coverage_report_render_internal.hpp"

#include <span>

namespace fsim::artifact::coverage_report_render_detail {
namespace {

    void metric(Writer& writer, const CoverageReportMetric& value)
    {
        writer.append("{\"namespace\":");
        escaped_json(writer, coverage_database_namespace_name(value.name_space));
        writer.append(",\"family\":");
        escaped_json(writer, coverage_database_metric_family_name(value.family));
        writer.append(",\"total\":");
        writer.number(value.total);
        writer.append(",\"covered\":");
        writer.number(value.covered);
        writer.append(",\"uncovered\":");
        writer.number(value.uncovered);
        writer.append(",\"excluded\":");
        writer.number(value.excluded);
        writer.append(",\"hits\":");
        writer.number(value.hits);
        writer.append(",\"excluded_hits\":");
        writer.number(value.excluded_hits);
        writer.append(",\"hits_saturated\":");
        boolean(writer, value.hits_saturated);
        writer.append(",\"excluded_hits_saturated\":");
        boolean(writer, value.excluded_hits_saturated);
        writer.character('}');
    }

    void point(Writer& writer, const CoverageReportPoint& value)
    {
        writer.append("{\"namespace\":");
        escaped_json(writer, coverage_database_namespace_name(value.name_space));
        writer.append(",\"family\":");
        escaped_json(writer, coverage_database_metric_family_name(value.family));
        writer.append(",\"point_identity\":\"");
        writer.identity(value.point_identity);
        writer.append("\",\"source_identity\":\"");
        writer.identity(value.source_identity);
        writer.append("\",\"instance_identity\":\"");
        writer.identity(value.instance_identity);
        writer.append("\",\"source_line\":");
        writer.number(value.source_line);
        writer.append(",\"hits\":");
        writer.number(value.hits);
        writer.append(",\"excluded_hits\":");
        writer.number(value.excluded_hits);
        writer.append(",\"hits_saturated\":");
        boolean(writer, value.hits_saturated);
        writer.append(",\"excluded_hits_saturated\":");
        boolean(writer, value.excluded_hits_saturated);
        writer.append(",\"status\":");
        escaped_json(writer, status_name(value.status));
        writer.character('}');
    }

    template <typename Value, typename Function>
    void array(Writer& writer, const std::span<const Value> values,
        Function function)
    {
        writer.character('[');
        for (std::size_t index = 0U; index < values.size(); ++index) {
            if (index != 0U)
                writer.character(',');
            function(writer, values[index]);
        }
        writer.character(']');
    }

} // namespace

void render_json(Writer& writer, const CoverageReportModel& report)
{
    writer.append("{\"schema\":");
    escaped_json(writer, kCoverageReportJsonSchema);
    writer.append(",\"sources\":[");
    for (std::size_t index = 0U; index < report.sources.size(); ++index) {
        if (index != 0U)
            writer.character(',');
        const auto& source = report.sources[index];
        writer.append("{\"source_identity\":\"");
        writer.identity(source.source_identity);
        writer.append("\",\"logical_path\":");
        escaped_json(writer, source.logical_path);
        writer.append(",\"metrics\":");
        array(writer, std::span { source.metrics }, metric);
        writer.append(",\"points\":");
        array(writer, std::span { source.points }, point);
        writer.character('}');
    }
    writer.append("],\"instances\":[");
    for (std::size_t index = 0U; index < report.instances.size(); ++index) {
        if (index != 0U)
            writer.character(',');
        const auto& instance = report.instances[index];
        writer.append("{\"instance_identity\":\"");
        writer.identity(instance.instance_identity);
        writer.append("\",\"metrics\":");
        array(writer, std::span { instance.metrics }, metric);
        writer.append(",\"points\":");
        array(writer, std::span { instance.points }, point);
        writer.character('}');
    }
    writer.append("],\"combined\":{\"metrics\":");
    array(writer, std::span { report.combined.metrics }, metric);
    writer.append("},\"exclusions\":{\"total_reasons\":");
    writer.number(report.exclusions.total_reasons);
    writer.append(",\"points\":[");
    for (std::size_t index = 0U; index < report.exclusions.points.size();
        ++index) {
        if (index != 0U)
            writer.character(',');
        const auto& value = report.exclusions.points[index];
        writer.append("{\"namespace\":");
        escaped_json(writer, coverage_database_namespace_name(value.name_space));
        writer.append(",\"family\":");
        escaped_json(writer, coverage_database_metric_family_name(value.family));
        writer.append(",\"scope\":");
        escaped_json(writer, scope_name(value.scope));
        writer.append(",\"point_identity\":\"");
        writer.identity(value.point_identity);
        writer.append("\",\"source_identity\":\"");
        writer.identity(value.source_identity);
        writer.append("\",\"instance_identity\":\"");
        writer.identity(value.instance_identity);
        writer.append("\",\"source_line\":");
        writer.number(value.source_line);
        writer.append(",\"reasons\":[");
        for (std::size_t reason = 0U; reason < value.reasons.size(); ++reason) {
            if (reason != 0U)
                writer.character(',');
            escaped_json(writer, value.reasons[reason]);
        }
        writer.append("]}");
    }
    writer.append("]}}\n");
}

} // namespace fsim::artifact::coverage_report_render_detail
