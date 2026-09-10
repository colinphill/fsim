// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_report_render.hpp"

namespace fsim::artifact::coverage_report_render_detail {

class Writer {
public:
    explicit Writer(std::size_t limit) noexcept;

    void append(std::string_view value);
    void character(char value);
    void number(std::uint64_t value);
    void identity(CoverageDatabaseIdentity value);

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] std::string take() noexcept;

private:
    std::size_t limit_;
    std::string output_;
    bool failed_ { };
};

[[nodiscard]] std::string_view status_name(
    CoverageReportPointStatus status) noexcept;
[[nodiscard]] std::string_view scope_name(
    CoverageDatabaseMetricScope scope) noexcept;
void boolean(Writer& writer, bool value);
void escaped_json(Writer& writer, std::string_view value);
void escaped_text(Writer& writer, std::string_view value);
void escaped_html(Writer& writer, std::string_view value);

void render_text(Writer& writer, const CoverageReportModel& report);
void render_json(Writer& writer, const CoverageReportModel& report);
void render_html(Writer& writer, const CoverageReportModel& report);

} // namespace fsim::artifact::coverage_report_render_detail
