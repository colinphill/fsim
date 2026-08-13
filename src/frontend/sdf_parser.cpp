// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {

void parse_sdf_constructs(SdfFile& file,
    std::vector<Diagnostic>& diagnostics);
void adapt_sdf21(SdfFile& file, std::vector<Diagnostic>& diagnostics);
void adapt_sdf30(SdfFile& file, std::vector<Diagnostic>& diagnostics);

namespace {

    [[nodiscard]] bool ascii_iequals(const std::string_view left,
        const std::string_view right) noexcept
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto left_character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(left[index])));
            const auto right_character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(right[index])));
            if (left_character != right_character) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string ascii_lower(const std::string_view text)
    {
        std::string result { text };
        std::ranges::transform(result, result.begin(), [](const char character) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        });
        return result;
    }

    [[nodiscard]] std::string decode_string(const std::string_view spelling)
    {
        if (spelling.size() < 2U || spelling.front() != '"' || spelling.back() != '"') {
            return std::string { spelling };
        }
        std::string result;
        result.reserve(spelling.size() - 2U);
        bool escaped = false;
        for (std::size_t index = 1U; index + 1U < spelling.size(); ++index) {
            const auto character = spelling[index];
            if (!escaped && character == '\\') {
                escaped = true;
                continue;
            }
            if (escaped) {
                switch (character) {
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
                    result.push_back(character);
                    break;
                }
                escaped = false;
            } else {
                result.push_back(character);
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<SdfHeaderKind> header_kind(
        const std::string_view spelling) noexcept
    {
        static constexpr auto headers = std::to_array<std::pair<std::string_view,
            SdfHeaderKind>>({
            { "SDFVERSION", SdfHeaderKind::SdfVersion },
            { "DESIGN", SdfHeaderKind::Design },
            { "DATE", SdfHeaderKind::Date },
            { "VENDOR", SdfHeaderKind::Vendor },
            { "PROGRAM", SdfHeaderKind::Program },
            { "VERSION", SdfHeaderKind::ProgramVersion },
            { "DIVIDER", SdfHeaderKind::Divider },
            { "VOLTAGE", SdfHeaderKind::Voltage },
            { "PROCESS", SdfHeaderKind::Process },
            { "TEMPERATURE", SdfHeaderKind::Temperature },
            { "TIMESCALE", SdfHeaderKind::Timescale },
        });
        const auto result = std::ranges::find_if(headers, [&](const auto& header) {
            return ascii_iequals(spelling, header.first);
        });
        if (result == headers.end()) {
            return std::nullopt;
        }
        return result->second;
    }

    [[nodiscard]] constexpr std::size_t header_index(
        const SdfHeaderKind kind) noexcept
    {
        return static_cast<std::size_t>(kind);
    }

    class SdfParser {
    public:
        SdfParser(SdfLexResult lexed, std::string source_name)
        {
            result_.diagnostics = std::move(lexed.diagnostics);
            result_.resource_exhausted = lexed.resource_exhausted;
            result_.file.source_name = std::move(source_name);
            result_.file.tokens = std::move(lexed.tokens);
        }

        [[nodiscard]] SdfParseResult run()
        {
            if (result_.resource_exhausted) {
                return std::move(result_);
            }
            parse_file();
            return std::move(result_);
        }

    private:
        [[nodiscard]] bool at_end()
        {
            skip_comments();
            return current().kind == SdfTokenKind::EndOfFile;
        }

        void skip_comments()
        {
            while (index_ < result_.file.tokens.size() && result_.file.tokens[index_].kind == SdfTokenKind::Comment) {
                ++index_;
            }
        }

        [[nodiscard]] const SdfToken& current() const
        {
            return result_.file.tokens[index_ < result_.file.tokens.size()
                    ? index_
                    : result_.file.tokens.size() - 1U];
        }

        [[nodiscard]] const SdfToken* consume(const SdfTokenKind kind)
        {
            skip_comments();
            if (current().kind != kind) {
                return nullptr;
            }
            return &result_.file.tokens[index_++];
        }

        void diagnose(std::string code, std::string message,
            const SourceSpan& span)
        {
            result_.diagnostics.push_back(
                Diagnostic { DiagnosticSeverity::Error, std::move(code),
                    std::move(message), span, { } });
        }

        [[nodiscard]] const SdfToken* require_token(const SdfTokenKind kind,
            std::string message)
        {
            if (const auto* token = consume(kind)) {
                return token;
            }
            diagnose("FSIM-SDF-PARSE-001", std::move(message), current().span);
            return nullptr;
        }

        void parse_file()
        {
            const auto* root_left = require_token(
                SdfTokenKind::LeftParenthesis,
                "SDF source must begin with a DELAYFILE form");
            if (!root_left) {
                return;
            }
            const auto* root_keyword = consume(SdfTokenKind::Keyword);
            if (!root_keyword || !ascii_iequals(root_keyword->spelling, "DELAYFILE")) {
                diagnose("FSIM-SDF-PARSE-002",
                    "SDF root form must use the DELAYFILE keyword", current().span);
                return;
            }

            auto root_end = root_keyword->span.end;
            bool root_closed = false;
            while (!at_end()) {
                if (const auto* close = consume(SdfTokenKind::RightParenthesis)) {
                    root_end = close->span.end;
                    root_closed = true;
                    break;
                }
                if (current().kind != SdfTokenKind::LeftParenthesis) {
                    diagnose("FSIM-SDF-PARSE-002",
                        "expected an SDF header or CELL form", current().span);
                    ++index_;
                    continue;
                }
                parse_child_form();
            }
            if (!root_closed) {
                diagnose("FSIM-SDF-PARSE-001",
                    "SDF DELAYFILE form is missing its closing parenthesis",
                    current().span);
            }
            if (!result_.file.has_revision) {
                diagnose("FSIM-SDF-PARSE-007",
                    "SDF DELAYFILE requires one SDFVERSION header",
                    root_left->span);
            }

            result_.file.span = SourceSpan { result_.file.source_name,
                root_left->span.begin, root_end,
                result_.file.source_name, { } };
            skip_comments();
            if (current().kind != SdfTokenKind::EndOfFile) {
                diagnose("FSIM-SDF-PARSE-010",
                    "unexpected tokens follow the SDF DELAYFILE form",
                    current().span);
            }
        }

        void parse_child_form()
        {
            skip_comments();
            const auto first_token = index_;
            const auto* left = consume(SdfTokenKind::LeftParenthesis);
            skip_comments();
            const auto* keyword = consume(SdfTokenKind::Keyword);
            if (!keyword) {
                keyword = consume(SdfTokenKind::Identifier);
            }
            if (!left || !keyword) {
                diagnose("FSIM-SDF-PARSE-002",
                    "SDF child form requires a keyword", current().span);
                skip_form(first_token, left ? left->span.begin : current().span.begin,
                    { });
                return;
            }

            if (const auto kind = header_kind(keyword->spelling)) {
                if (body_started_) {
                    diagnose("FSIM-SDF-PARSE-008",
                        "SDF header appears after the first body form",
                        keyword->span);
                }
                parse_header(first_token, *left, *keyword, *kind);
                return;
            }
            if (ascii_iequals(keyword->spelling, "CELL")) {
                body_started_ = true;
                skip_form(first_token, left->span.begin, keyword->spelling);
                return;
            }

            diagnose("FSIM-SDF-PARSE-009",
                "unsupported SDF DELAYFILE child form '" + keyword->spelling + "'",
                keyword->span);
            body_started_ = true;
            skip_form(first_token, left->span.begin, keyword->spelling);
        }

        void skip_form(const std::size_t first_token, const SourceLocation begin,
            std::string keyword)
        {
            std::size_t depth = 1U;
            auto end = current().span.end;
            while (index_ < result_.file.tokens.size()) {
                const auto& token = result_.file.tokens[index_++];
                if (token.kind == SdfTokenKind::LeftParenthesis) {
                    ++depth;
                } else if (token.kind == SdfTokenKind::RightParenthesis) {
                    --depth;
                    end = token.span.end;
                    if (depth == 0U) {
                        break;
                    }
                } else if (token.kind == SdfTokenKind::EndOfFile) {
                    diagnose("FSIM-SDF-PARSE-001",
                        "SDF child form is missing its closing parenthesis",
                        token.span);
                    break;
                }
            }
            result_.file.body_forms.push_back(
                SdfRawForm { std::move(keyword), first_token, index_ - first_token,
                    SourceSpan { result_.file.source_name, begin, end,
                        result_.file.source_name, { } } });
        }

        void skip_nested_value()
        {
            std::size_t depth = 0U;
            while (index_ < result_.file.tokens.size()) {
                const auto& token = result_.file.tokens[index_++];
                if (token.kind == SdfTokenKind::LeftParenthesis) {
                    ++depth;
                } else if (token.kind == SdfTokenKind::RightParenthesis) {
                    if (depth == 0U) {
                        --index_;
                        return;
                    }
                    --depth;
                    if (depth == 0U) {
                        return;
                    }
                } else if (token.kind == SdfTokenKind::EndOfFile) {
                    return;
                }
            }
        }

        void parse_header(const std::size_t first_token, const SdfToken& left,
            const SdfToken& keyword, const SdfHeaderKind kind)
        {
            const auto rank = header_index(kind);
            if (rank < last_header_rank_ || (result_.file.headers.empty() && kind != SdfHeaderKind::SdfVersion)) {
                diagnose("FSIM-SDF-PARSE-006",
                    "SDF headers are not in canonical standard order",
                    keyword.span);
            }
            last_header_rank_ = std::max(last_header_rank_, rank);
            if (seen_headers_[rank]) {
                diagnose("FSIM-SDF-PARSE-004",
                    "duplicate SDF header '" + keyword.spelling + "'",
                    keyword.span);
            }
            seen_headers_[rank] = true;

            std::vector<const SdfToken*> values;
            const SdfToken* close = nullptr;
            while (!at_end()) {
                if ((close = consume(SdfTokenKind::RightParenthesis))) {
                    break;
                }
                if (current().kind == SdfTokenKind::LeftParenthesis) {
                    diagnose("FSIM-SDF-PARSE-003",
                        "SDF header values may not contain nested forms",
                        current().span);
                    skip_nested_value();
                    continue;
                }
                values.push_back(&result_.file.tokens[index_++]);
            }
            if (!close) {
                diagnose("FSIM-SDF-PARSE-001",
                    "SDF header is missing its closing parenthesis",
                    current().span);
            }

            SdfHeaderRecord record;
            record.kind = kind;
            record.keyword_spelling = keyword.spelling;
            record.value_spellings.reserve(values.size());
            for (const auto* value : values) {
                record.value_spellings.push_back(value->spelling);
            }
            record.canonical_value = validate_header_value(kind, values, keyword.span);
            const auto end = close ? close->span.end : current().span.end;
            record.span = SourceSpan { result_.file.source_name, left.span.begin, end,
                result_.file.source_name, { } };
            result_.file.headers.push_back(std::move(record));

            if (kind == SdfHeaderKind::SdfVersion && !result_.file.has_revision) {
                result_.file.has_revision = true;
                select_revision(result_.file.headers.back(), keyword.span);
            }
            (void)first_token;
        }

        [[nodiscard]] std::string validate_header_value(
            const SdfHeaderKind kind, const std::vector<const SdfToken*>& values,
            const SourceSpan& span)
        {
            switch (kind) {
            case SdfHeaderKind::SdfVersion:
            case SdfHeaderKind::Design:
            case SdfHeaderKind::Date:
            case SdfHeaderKind::Vendor:
            case SdfHeaderKind::Program:
            case SdfHeaderKind::ProgramVersion:
            case SdfHeaderKind::Process:
                return validate_string_header(kind, values, span);
            case SdfHeaderKind::Divider:
                return validate_divider(values, span);
            case SdfHeaderKind::Voltage:
            case SdfHeaderKind::Temperature:
                return validate_numeric_header(values, span);
            case SdfHeaderKind::Timescale:
                return validate_timescale(values, span);
            }
            return { };
        }

        [[nodiscard]] std::string validate_string_header(
            const SdfHeaderKind kind, const std::vector<const SdfToken*>& values,
            const SourceSpan& span)
        {
            if (values.size() != 1U || values.front()->kind != SdfTokenKind::String) {
                diagnose("FSIM-SDF-PARSE-003",
                    std::string { to_string(kind) } + " header requires exactly one quoted string",
                    span);
                return { };
            }
            return decode_string(values.front()->spelling);
        }

        [[nodiscard]] std::string validate_divider(
            const std::vector<const SdfToken*>& values, const SourceSpan& span)
        {
            if (values.size() != 1U || (values.front()->kind != SdfTokenKind::Slash && values.front()->kind != SdfTokenKind::Dot)) {
                diagnose("FSIM-SDF-PARSE-003",
                    "DIVIDER header requires '/' or '.'", span);
                return { };
            }
            return values.front()->spelling;
        }

        [[nodiscard]] std::string validate_numeric_header(
            const std::vector<const SdfToken*>& values, const SourceSpan& span)
        {
            if (values.size() == 1U && values.front()->kind == SdfTokenKind::Number) {
                return values.front()->spelling;
            }
            std::size_t colons = 0U;
            std::size_t numbers = 0U;
            std::string canonical;
            bool valid = !values.empty() && values.size() <= 5U;
            bool expect_number = true;
            for (const auto* value : values) {
                canonical += value->spelling;
                if (value->kind == SdfTokenKind::Colon) {
                    ++colons;
                    expect_number = true;
                } else if (value->kind == SdfTokenKind::Number && expect_number) {
                    ++numbers;
                    expect_number = false;
                } else {
                    valid = false;
                }
            }
            if (!valid || colons != 2U || numbers == 0U) {
                diagnose("FSIM-SDF-PARSE-003",
                    "numeric SDF header requires one number or one min:typ:max triple",
                    span);
                return { };
            }
            return canonical;
        }

        [[nodiscard]] std::string validate_timescale(
            const std::vector<const SdfToken*>& values, const SourceSpan& span)
        {
            static constexpr auto scales = std::to_array<std::string_view>({ "1", "10", "100" });
            static constexpr auto sdf21_scales = std::to_array<std::string_view>({ "1", "10", "100", "1.0", "10.0", "100.0" });
            static constexpr auto units = std::to_array<std::string_view>({ "s", "ms", "us", "ns", "ps", "fs" });
            static constexpr auto sdf21_units = std::to_array<std::string_view>({ "us", "ns", "ps" });
            const bool legacy = result_.file.has_revision
                && result_.file.revision != SdfRevision::Sdf40;
            if (values.size() != 2U || values.front()->kind != SdfTokenKind::Number || (values.back()->kind != SdfTokenKind::Identifier && values.back()->kind != SdfTokenKind::Keyword)) {
                diagnose(result_.file.revision == SdfRevision::Sdf21
                        ? "FSIM-SDF-21-006"
                        : legacy ? "FSIM-SDF-30-003"
                                 : "FSIM-SDF-PARSE-003",
                    "TIMESCALE header requires a scale and time unit", span);
                return { };
            }
            const auto unit = ascii_lower(values.back()->spelling);
            const bool valid_scale = legacy
                ? std::ranges::find(sdf21_scales, values.front()->spelling) != sdf21_scales.end()
                : std::ranges::find(scales, values.front()->spelling) != scales.end();
            const bool valid_unit = legacy
                ? std::ranges::find(sdf21_units, unit) != sdf21_units.end()
                : std::ranges::find(units, unit) != units.end();
            if (!valid_scale || !valid_unit) {
                diagnose(result_.file.revision == SdfRevision::Sdf21
                        ? "FSIM-SDF-21-006"
                        : legacy ? "FSIM-SDF-30-003"
                                 : "FSIM-SDF-PARSE-003",
                    "TIMESCALE must use 1, 10, or 100 and s, ms, us, ns, ps, or fs",
                    span);
                return { };
            }
            return values.front()->spelling + unit;
        }

        void select_revision(const SdfHeaderRecord& record, const SourceSpan& span)
        {
            const auto sdf21 = record.canonical_value.find("2.1");
            const auto sdf30 = record.canonical_value.find("3.0");
            const auto sdf40 = record.canonical_value.find("4.0");
            const auto first = std::min({ sdf21, sdf30, sdf40 });
            if (first != std::string::npos && first == sdf40) {
                result_.file.revision = SdfRevision::Sdf40;
                return;
            }
            if (first != std::string::npos && first == sdf21) {
                result_.file.revision = SdfRevision::Sdf21;
                return;
            }
            if (first != std::string::npos && first == sdf30) {
                result_.file.revision = SdfRevision::Sdf30;
                return;
            }
            diagnose("FSIM-SDF-PARSE-005",
                "selected SDF revision '" + record.canonical_value + "' is not enabled by the SDF 4.0 parser",
                span);
        }

        SdfParseResult result_;
        std::size_t index_ { };
        std::size_t last_header_rank_ { };
        std::array<bool, 11U> seen_headers_ { };
        bool body_started_ { };
    };

} // namespace

const SdfHeaderRecord* SdfFile::find_header(
    const SdfHeaderKind kind) const noexcept
{
    const auto result = std::ranges::find(headers, kind, &SdfHeaderRecord::kind);
    return result == headers.end() ? nullptr : &*result;
}

SdfParseResult parse_sdf(SourceText source, const SdfLexerLimits limits)
{
    auto source_name = source.name;
    auto result = SdfParser(lex_sdf(std::move(source), limits),
        std::move(source_name))
                      .run();
    parse_sdf_constructs(result.file, result.diagnostics);
    adapt_sdf21(result.file, result.diagnostics);
    adapt_sdf30(result.file, result.diagnostics);
    normalize_sdf(result.file, result.diagnostics);
    lower_sdf_ir(result.file, result.diagnostics);
    return result;
}

const char* to_string(const SdfRevision revision) noexcept
{
    switch (revision) {
    case SdfRevision::Sdf21:
        return "2.1";
    case SdfRevision::Sdf30:
        return "3.0";
    case SdfRevision::Sdf40:
        return "4.0";
    }
    return "4.0";
}

const char* to_string(const SdfHeaderKind kind) noexcept
{
    switch (kind) {
    case SdfHeaderKind::SdfVersion:
        return "SDFVERSION";
    case SdfHeaderKind::Design:
        return "DESIGN";
    case SdfHeaderKind::Date:
        return "DATE";
    case SdfHeaderKind::Vendor:
        return "VENDOR";
    case SdfHeaderKind::Program:
        return "PROGRAM";
    case SdfHeaderKind::ProgramVersion:
        return "VERSION";
    case SdfHeaderKind::Divider:
        return "DIVIDER";
    case SdfHeaderKind::Voltage:
        return "VOLTAGE";
    case SdfHeaderKind::Process:
        return "PROCESS";
    case SdfHeaderKind::Temperature:
        return "TEMPERATURE";
    case SdfHeaderKind::Timescale:
        return "TIMESCALE";
    }
    return "SDFVERSION";
}

} // namespace fsim::frontend
