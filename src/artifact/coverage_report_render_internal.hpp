// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/artifact/coverage_report_render.hpp"

#include <array>
#include <charconv>
#include <system_error>
#include <utility>

namespace fsim::artifact::coverage_report_render_detail {

class Writer {
public:
    explicit Writer(const std::size_t limit) noexcept
        : limit_(limit)
    {
    }

    void append(const std::string_view value)
    {
        if (failed_ || output_.size() > limit_
            || value.size() > limit_ - output_.size()) {
            failed_ = true;
            return;
        }
        output_.append(value);
    }

    void character(const char value)
    {
        append(std::string_view { &value, 1U });
    }

    void number(const std::uint64_t value)
    {
        std::array<char, 20> bytes { };
        const auto [end, error]
            = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
        if (error != std::errc { }) {
            failed_ = true;
            return;
        }
        append(std::string_view {
            bytes.data(), static_cast<std::size_t>(end - bytes.data()) });
    }

    void identity(const CoverageDatabaseIdentity value)
    {
        static constexpr std::string_view digits = "0123456789abcdef";
        std::array<char, 32> bytes { };
        for (std::size_t index = 0U; index < 16U; ++index) {
            const auto shift = (15U - index) * 4U;
            bytes[index] = digits[(value.high >> shift) & 0xfU];
            bytes[index + 16U] = digits[(value.low >> shift) & 0xfU];
        }
        append(std::string_view { bytes.data(), bytes.size() });
    }

    [[nodiscard]] bool ok() const noexcept { return !failed_; }
    [[nodiscard]] std::string take() noexcept { return std::move(output_); }

private:
    std::size_t limit_;
    std::string output_;
    bool failed_ { };
};

inline std::string_view status_name(
    const CoverageReportPointStatus status) noexcept
{
    switch (status) {
    case CoverageReportPointStatus::Covered:
        return "covered";
    case CoverageReportPointStatus::Uncovered:
        return "uncovered";
    case CoverageReportPointStatus::Excluded:
        return "excluded";
    }
    return { };
}

inline std::string_view scope_name(
    const CoverageDatabaseMetricScope scope) noexcept
{
    switch (scope) {
    case CoverageDatabaseMetricScope::Source:
        return "source";
    case CoverageDatabaseMetricScope::Instance:
        return "instance";
    }
    return { };
}

inline void boolean(Writer& writer, const bool value)
{
    writer.append(value ? "true" : "false");
}

inline void escaped_json(Writer& writer, const std::string_view value)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    writer.character('"');
    for (const char raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
        case '"':
            writer.append("\\\"");
            break;
        case '\\':
            writer.append("\\\\");
            break;
        case '\b':
            writer.append("\\b");
            break;
        case '\f':
            writer.append("\\f");
            break;
        case '\n':
            writer.append("\\n");
            break;
        case '\r':
            writer.append("\\r");
            break;
        case '\t':
            writer.append("\\t");
            break;
        default:
            if (character < 0x20U) {
                writer.append("\\u00");
                writer.character(digits[(character >> 4U) & 0xfU]);
                writer.character(digits[character & 0xfU]);
            } else {
                writer.character(raw);
            }
        }
    }
    writer.character('"');
}

inline void escaped_text(Writer& writer, const std::string_view value)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    writer.character('"');
    for (const char raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        if (raw == '"' || raw == '\\') {
            writer.character('\\');
            writer.character(raw);
        } else if (character < 0x20U) {
            writer.append("\\u00");
            writer.character(digits[(character >> 4U) & 0xfU]);
            writer.character(digits[character & 0xfU]);
        } else {
            writer.character(raw);
        }
    }
    writer.character('"');
}

inline void escaped_html(Writer& writer, const std::string_view value)
{
    for (const char raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        switch (raw) {
        case '&':
            writer.append("&amp;");
            break;
        case '<':
            writer.append("&lt;");
            break;
        case '>':
            writer.append("&gt;");
            break;
        case '"':
            writer.append("&quot;");
            break;
        case '\'':
            writer.append("&#39;");
            break;
        default:
            if (character < 0x20U) {
                writer.append("&#");
                writer.number(character);
                writer.character(';');
            } else {
                writer.character(raw);
            }
        }
    }
}

void render_text(Writer& writer, const CoverageReportModel& report);
void render_json(Writer& writer, const CoverageReportModel& report);
void render_html(Writer& writer, const CoverageReportModel& report);

} // namespace fsim::artifact::coverage_report_render_detail
