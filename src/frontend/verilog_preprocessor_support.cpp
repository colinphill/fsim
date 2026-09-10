// SPDX-License-Identifier: Apache-2.0
#include "verilog_preprocessor_internal.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>
#include <system_error>

namespace fsim::frontend::preprocessor_detail {

    [[nodiscard]] StandardRevision default_standard_revision(
        const Language language)
    {
        return language == Language::Verilog2005
            ? StandardRevision::Verilog2005
            : StandardRevision::SystemVerilog2017;
    }

    [[nodiscard]] bool is_verilog_standard(const StandardRevision standard)
    {
        return standard == StandardRevision::Verilog1995
            || standard == StandardRevision::Verilog2001
            || standard == StandardRevision::Verilog2001NoConfig
            || standard == StandardRevision::Verilog2005;
    }

    [[nodiscard]] bool is_system_verilog_standard(
        const StandardRevision standard)
    {
        return standard == StandardRevision::SystemVerilog2005
            || standard == StandardRevision::SystemVerilog2009
            || standard == StandardRevision::SystemVerilog2012
            || standard == StandardRevision::SystemVerilog2017
            || standard == StandardRevision::SystemVerilog2023;
    }

    [[nodiscard]] unsigned standard_rank(const StandardRevision standard)
    {
        switch (standard) {
        case StandardRevision::Verilog1995:
            return 0;
        case StandardRevision::Verilog2001:
        case StandardRevision::Verilog2001NoConfig:
            return 1;
        case StandardRevision::Verilog2005:
            return 2;
        case StandardRevision::SystemVerilog2005:
            return 3;
        case StandardRevision::SystemVerilog2009:
            return 4;
        case StandardRevision::SystemVerilog2012:
            return 5;
        case StandardRevision::SystemVerilog2017:
            return 6;
        case StandardRevision::SystemVerilog2023:
            return 7;
        default:
            return 0;
        }
    }

    [[nodiscard]] std::optional<std::int32_t>
    systemverilog_coverage_constant(const std::string_view name) noexcept
    {
        if (name == "SV_COV_START")
            return 0;
        if (name == "SV_COV_STOP")
            return 1;
        if (name == "SV_COV_RESET")
            return 2;
        if (name == "SV_COV_CHECK")
            return 3;
        if (name == "SV_COV_MODULE")
            return 10;
        if (name == "SV_COV_HIER")
            return 11;
        if (name == "SV_COV_ASSERTION")
            return 20;
        if (name == "SV_COV_FSM_STATE")
            return 21;
        if (name == "SV_COV_STATEMENT")
            return 22;
        if (name == "SV_COV_TOGGLE")
            return 23;
        if (name == "SV_COV_OVERFLOW")
            return -2;
        if (name == "SV_COV_ERROR")
            return -1;
        if (name == "SV_COV_NOCOV")
            return 0;
        if (name == "SV_COV_OK")
            return 1;
        if (name == "SV_COV_PARTIAL")
            return 2;
        return std::nullopt;
    }

    // Preserve the published diagnostic identities after adopting true textual
    // include semantics. They must not be reused for a different failure class.
    [[maybe_unused]] constexpr std::string_view
        legacy_include_open_conditional_code = "FSIM-SV-PP-045";
    [[maybe_unused]] constexpr std::string_view
        legacy_include_cross_conditional_code = "FSIM-SV-PP-046";

    [[nodiscard]] bool is_line_continuation(const Token& token)
    {
        return !token.text.empty() && token.text.front() == '\\'
            && (token.text.find('\n') != std::string::npos
                || token.text.find('\r') != std::string::npos);
    }

    [[nodiscard]] bool same_tokens(
        const std::vector<Token>& left,
        const std::vector<Token>& right)
    {
        return left.size() == right.size()
            && std::equal(
                left.begin(), left.end(), right.begin(),
                [](const Token& lhs, const Token& rhs) {
                    return lhs.kind == rhs.kind && lhs.text == rhs.text;
                });
    }

    [[nodiscard]] bool same_macro(
        const Macro& left,
        const Macro& right)
    {
        if (left.parameters.has_value() != right.parameters.has_value()
            || !same_tokens(left.replacement, right.replacement)) {
            return false;
        }
        if (!left.parameters) {
            return true;
        }
        if (left.parameters->size() != right.parameters->size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.parameters->size(); ++index) {
            const auto& lhs = (*left.parameters)[index];
            const auto& rhs = (*right.parameters)[index];
            if (lhs.name != rhs.name
                || lhs.default_value.has_value()
                    != rhs.default_value.has_value()
                || (lhs.default_value
                    && !same_tokens(*lhs.default_value, *rhs.default_value))) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool identifier_spelling(const std::string_view spelling)
    {
        if (spelling.empty()) {
            return false;
        }
        const auto first = static_cast<unsigned char>(spelling.front());
        if (std::isalpha(first) == 0 && spelling.front() != '_'
            && spelling.front() != '$') {
            return false;
        }
        return std::all_of(
            spelling.begin() + 1,
            spelling.end(),
            [](const char character) {
                const auto value = static_cast<unsigned char>(character);
                return std::isalnum(value) != 0 || character == '_'
                    || character == '$';
            });
    }

    [[nodiscard]] std::string string_literal_spelling(
        const std::string_view value)
    {
        std::string result;
        result.reserve(value.size() + 2);
        result.push_back('"');
        for (const char character : value) {
            switch (character) {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result.push_back(character);
                break;
            }
        }
        result.push_back('"');
        return result;
    }

    [[nodiscard]] std::optional<std::string> decode_string_literal(
        const std::string_view spelling)
    {
        if (spelling.size() < 2 || spelling.front() != '"'
            || spelling.back() != '"') {
            return std::nullopt;
        }
        std::string result;
        result.reserve(spelling.size() - 2);
        for (std::size_t index = 1; index + 1 < spelling.size(); ++index) {
            const char character = spelling[index];
            if (character != '\\') {
                if (character == '\n' || character == '\r') {
                    return std::nullopt;
                }
                result.push_back(character);
                continue;
            }
            if (++index + 1 > spelling.size()) {
                return std::nullopt;
            }
            switch (spelling[index]) {
            case '\\':
                result.push_back('\\');
                break;
            case '"':
                result.push_back('"');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            default:
                return std::nullopt;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::size_t> decimal_number(
        const Token& token,
        const bool require_positive)
    {
        if (token.kind != TokenKind::Number || token.text.empty()) {
            return std::nullopt;
        }
        std::size_t result { };
        const auto parsed = std::from_chars(
            token.text.data(), token.text.data() + token.text.size(), result, 10);
        if (parsed.ec != std::errc { }
            || parsed.ptr != token.text.data() + token.text.size()
            || (require_positive && result == 0)) {
            return std::nullopt;
        }
        return result;
    }

    [[nodiscard]] SourceLocation remap_location(
        SourceLocation location,
        const SourceMapping& mapping)
    {
        if (location.line < mapping.physical_anchor_line) {
            return location;
        }
        const auto delta = location.line - mapping.physical_anchor_line;
        if (delta
            > std::numeric_limits<std::size_t>::max()
                - mapping.logical_anchor_line) {
            location.line = std::numeric_limits<std::size_t>::max();
        } else {
            location.line = mapping.logical_anchor_line + delta;
        }
        return location;
    }

    [[nodiscard]] SourceSpan remap_span(
        SourceSpan span,
        const SourceMapping& mapping)
    {
        if (physical_source(span) != mapping.physical_source) {
            return span;
        }
        if (span.physical_source_name.empty()) {
            span.physical_source_name = mapping.physical_source;
        }
        span.source_name = mapping.logical_source;
        span.begin = remap_location(span.begin, mapping);
        span.end = remap_location(span.end, mapping);
        return span;
    }

    [[nodiscard]] std::string location_text(const SourceSpan& span)
    {
        return span.source_name + ':' + std::to_string(span.begin.line)
            + ':' + std::to_string(span.begin.column);
    }

    [[nodiscard]] std::filesystem::path normalized_path(
        const std::filesystem::path& path)
    {
        std::error_code error;
        auto absolute = std::filesystem::absolute(path, error);
        if (error) {
            return path.lexically_normal();
        }
        auto canonical = std::filesystem::weakly_canonical(absolute, error);
        return error ? absolute.lexically_normal() : canonical;
    }


} // namespace fsim::frontend::preprocessor_detail
