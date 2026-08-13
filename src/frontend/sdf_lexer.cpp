// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/sdf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::frontend {
namespace {

    constexpr auto kSdfKeywords = std::to_array<std::string_view>({
        "ABSOLUTE",
        "ARRIVAL",
        "BIDIRECTSKEW",
        "CCOND",
        "CELL",
        "CELLTYPE",
        "CORRELATION",
        "COND",
        "CONDELSE",
        "DATE",
        "DELAY",
        "DELAYFILE",
        "DESIGN",
        "DEVICE",
        "DEPARTURE",
        "DIFF",
        "DIVIDER",
        "EXCEPTION",
        "HOLD",
        "GLOBALPATHPULSE",
        "INCREMENT",
        "INSTANCE",
        "INTERCONNECT",
        "IOPATH",
        "LABEL",
        "MIPD",
        "NAME",
        "NETDELAY",
        "NEGATIVE",
        "NEGEDGE",
        "NOCHANGE",
        "PATHCONSTRAINT",
        "PATHPULSE",
        "PATHPULSEPERCENT",
        "PERIOD",
        "PERIODCONSTRAINT",
        "PORT",
        "POSEDGE",
        "PROCESS",
        "PROGRAM",
        "PROGRAM_VERSION",
        "RECOVERY",
        "RECREM",
        "REMOVAL",
        "RETAIN",
        "SDFVERSION",
        "SCOND",
        "SETUP",
        "SETUPHOLD",
        "SKEW",
        "SKEWCONSTRAINT",
        "SLACK",
        "SUM",
        "TEMPERATURE",
        "TIMESCALE",
        "TIMINGCHECK",
        "TIMINGENV",
        "VENDOR",
        "VERSION",
        "VOLTAGE",
        "WAVEFORM",
        "WIDTH",
    });

    [[nodiscard]] bool ascii_space(const char character) noexcept
    {
        switch (character) {
        case ' ':
        case '\t':
        case '\n':
        case '\r':
        case '\f':
        case '\v':
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool ascii_digit(const char character) noexcept
    {
        return character >= '0' && character <= '9';
    }

    [[nodiscard]] bool printable_ascii(const char character) noexcept
    {
        const auto byte = static_cast<unsigned char>(character);
        return byte >= 0x20U && byte <= 0x7eU;
    }

    [[nodiscard]] bool ascii_iequals(const std::string_view left,
        const std::string_view right) noexcept
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto left_character = static_cast<char>(
                std::toupper(static_cast<unsigned char>(left[index])));
            if (left_character != right[index]) {
                return false;
            }
        }
        return true;
    }

    class SdfLexer {
    public:
        SdfLexer(SourceText source, SdfLexerLimits limits)
            : source_(std::move(source))
            , limits_(limits)
        {
        }

        [[nodiscard]] SdfLexResult run()
        {
            if (source_.text.size() > limits_.max_source_bytes) {
                const auto begin = current_location();
                diagnose("FSIM-SDF-LEX-007",
                    "SDF source exceeds the configured byte limit", begin, begin);
                result_.resource_exhausted = true;
                stopped_ = true;
            }

            while (!stopped_ && !at_end()) {
                skip_whitespace();
                if (at_end()) {
                    break;
                }
                lex_one();
            }

            if (!stopped_ && parenthesis_depth_ != 0U) {
                const auto location = current_location();
                diagnose("FSIM-SDF-LEX-010",
                    "SDF source ends inside parenthesis nesting", location,
                    location);
            }
            const auto location = current_location();
            result_.tokens.push_back(SdfToken { SdfTokenKind::EndOfFile, { },
                span(location, location) });
            return std::move(result_);
        }

    private:
        [[nodiscard]] bool at_end() const noexcept
        {
            return index_ >= source_.text.size();
        }

        [[nodiscard]] char peek(const std::size_t lookahead = 0U) const noexcept
        {
            const auto position = index_ + lookahead;
            return position < source_.text.size() ? source_.text[position] : '\0';
        }

        [[nodiscard]] SourceLocation current_location() const noexcept
        {
            return SourceLocation { index_, line_, column_ };
        }

        [[nodiscard]] SourceSpan span(const SourceLocation begin,
            const SourceLocation end) const
        {
            return SourceSpan { source_.name, begin, end, source_.name, { } };
        }

        char advance()
        {
            const char character = source_.text[index_++];
            if (character == '\r') {
                if (!at_end() && peek() == '\n') {
                    ++index_;
                }
                ++line_;
                column_ = 1U;
            } else if (character == '\n') {
                ++line_;
                column_ = 1U;
            } else {
                ++column_;
            }
            return character;
        }

        void skip_whitespace()
        {
            while (!at_end() && ascii_space(peek())) {
                advance();
            }
        }

        void diagnose(std::string code, std::string message,
            const SourceLocation begin, const SourceLocation end)
        {
            result_.diagnostics.push_back(
                Diagnostic { DiagnosticSeverity::Error, std::move(code),
                    std::move(message), span(begin, end), { } });
        }

        [[nodiscard]] bool token_limit_exceeded(const SourceLocation begin,
            const bool numeric = false)
        {
            const auto length = index_ - begin.offset;
            const auto limit = numeric ? limits_.max_numeric_bytes
                                       : limits_.max_token_bytes;
            if (length <= limit) {
                return false;
            }
            diagnose(numeric ? "FSIM-SDF-LEX-006" : "FSIM-SDF-LEX-011",
                numeric ? "SDF numeric token exceeds the configured byte limit"
                        : "SDF token exceeds the configured byte limit",
                begin, current_location());
            result_.resource_exhausted = true;
            stopped_ = true;
            return true;
        }

        void emit(const SdfTokenKind kind, const SourceLocation begin)
        {
            const auto end = current_location();
            if (end.offset - begin.offset > limits_.max_token_bytes) {
                diagnose("FSIM-SDF-LEX-011",
                    "SDF token exceeds the configured byte limit", begin, end);
                result_.resource_exhausted = true;
                stopped_ = true;
                return;
            }
            if (result_.tokens.size() >= limits_.max_tokens) {
                diagnose("FSIM-SDF-LEX-008",
                    "SDF token count exceeds the configured limit", begin, end);
                result_.resource_exhausted = true;
                stopped_ = true;
                return;
            }
            result_.tokens.push_back(SdfToken {
                kind, source_.text.substr(begin.offset, end.offset - begin.offset),
                span(begin, end) });
        }

        void lex_left_parenthesis()
        {
            const auto begin = current_location();
            advance();
            emit(SdfTokenKind::LeftParenthesis, begin);
            if (stopped_) {
                return;
            }
            ++parenthesis_depth_;
            if (parenthesis_depth_ > limits_.max_parenthesis_depth) {
                diagnose("FSIM-SDF-LEX-009",
                    "SDF parenthesis nesting exceeds the configured limit", begin,
                    current_location());
                result_.resource_exhausted = true;
                stopped_ = true;
            }
        }

        void lex_right_parenthesis()
        {
            const auto begin = current_location();
            advance();
            emit(SdfTokenKind::RightParenthesis, begin);
            if (stopped_) {
                return;
            }
            if (parenthesis_depth_ == 0U) {
                diagnose("FSIM-SDF-LEX-010", "unmatched SDF closing parenthesis", begin,
                    current_location());
                return;
            }
            --parenthesis_depth_;
        }

        void lex_line_comment()
        {
            const auto begin = current_location();
            advance();
            advance();
            while (!at_end() && peek() != '\n' && peek() != '\r') {
                advance();
                if (token_limit_exceeded(begin)) {
                    return;
                }
            }
            emit(SdfTokenKind::Comment, begin);
        }

        void lex_block_comment()
        {
            const auto begin = current_location();
            advance();
            advance();
            while (!at_end()) {
                if (peek() == '*' && peek(1U) == '/') {
                    advance();
                    advance();
                    emit(SdfTokenKind::Comment, begin);
                    return;
                }
                advance();
                if (token_limit_exceeded(begin)) {
                    return;
                }
            }
            diagnose("FSIM-SDF-LEX-002", "unterminated SDF block comment", begin,
                current_location());
            emit(SdfTokenKind::Comment, begin);
        }

        void lex_string()
        {
            const auto begin = current_location();
            advance();
            bool escaped = false;
            while (!at_end()) {
                const auto character = peek();
                if (!escaped && character == '"') {
                    advance();
                    emit(SdfTokenKind::String, begin);
                    return;
                }
                if (!escaped && character == '\\') {
                    escaped = true;
                    advance();
                } else {
                    escaped = false;
                    advance();
                }
                if (token_limit_exceeded(begin)) {
                    return;
                }
            }
            diagnose("FSIM-SDF-LEX-003", "unterminated SDF quoted string", begin,
                current_location());
            emit(SdfTokenKind::String, begin);
        }

        void lex_number()
        {
            const auto begin = current_location();
            if (peek() == '+' || peek() == '-') {
                advance();
            }
            while (ascii_digit(peek())) {
                advance();
                if (token_limit_exceeded(begin, true)) {
                    return;
                }
            }
            if (peek() == '.') {
                advance();
                while (ascii_digit(peek())) {
                    advance();
                    if (token_limit_exceeded(begin, true)) {
                        return;
                    }
                }
            }
            if (peek() == 'e' || peek() == 'E') {
                const auto exponent = current_location();
                advance();
                if (peek() == '+' || peek() == '-') {
                    advance();
                }
                if (!ascii_digit(peek())) {
                    diagnose("FSIM-SDF-LEX-005",
                        "SDF exponent requires at least one decimal digit", exponent,
                        current_location());
                }
                while (ascii_digit(peek())) {
                    advance();
                    if (token_limit_exceeded(begin, true)) {
                        return;
                    }
                }
            }
            emit(SdfTokenKind::Number, begin);
        }

        [[nodiscard]] bool identifier_terminator(const char character) const noexcept
        {
            return character == '\0' || ascii_space(character)
                || !printable_ascii(character) || character == '('
                || character == ')' || character == ':' || character == '"';
        }

        void lex_identifier()
        {
            const auto begin = current_location();
            bool escaped = false;
            while (!at_end() && !identifier_terminator(peek())) {
                if (peek() == '/' && (peek(1U) == '/' || peek(1U) == '*')) {
                    break;
                }
                if (peek() == '\\') {
                    escaped = true;
                    advance();
                    if (at_end() || identifier_terminator(peek())) {
                        diagnose("FSIM-SDF-LEX-004",
                            "SDF identifier ends with an incomplete escape", begin,
                            current_location());
                        break;
                    }
                }
                advance();
                if (token_limit_exceeded(begin)) {
                    return;
                }
            }
            const auto spelling = std::string_view { source_.text }.substr(
                begin.offset, index_ - begin.offset);
            auto kind = escaped ? SdfTokenKind::EscapedIdentifier
                                : SdfTokenKind::Identifier;
            if (!escaped && is_sdf_keyword(spelling)) {
                kind = SdfTokenKind::Keyword;
            }
            emit(kind, begin);
        }

        void emit_single(const SdfTokenKind kind)
        {
            const auto begin = current_location();
            advance();
            emit(kind, begin);
        }

        void lex_slash()
        {
            if (peek(1U) == '/') {
                lex_line_comment();
            } else if (peek(1U) == '*') {
                lex_block_comment();
            } else {
                emit_single(SdfTokenKind::Slash);
            }
        }

        void lex_dot()
        {
            if (ascii_digit(peek(1U))) {
                lex_number();
            } else {
                emit_single(SdfTokenKind::Dot);
            }
        }

        void lex_sign(const SdfTokenKind kind)
        {
            if (ascii_digit(peek(1U))
                || (peek(1U) == '.' && ascii_digit(peek(2U)))) {
                lex_number();
            } else {
                emit_single(kind);
            }
        }

        void lex_one()
        {
            const auto character = peek();
            if (!printable_ascii(character)) {
                const auto begin = current_location();
                advance();
                diagnose("FSIM-SDF-LEX-001", "invalid byte in SDF source", begin,
                    current_location());
                return;
            }

            switch (character) {
            case '(':
                lex_left_parenthesis();
                return;
            case ')':
                lex_right_parenthesis();
                return;
            case ':':
                emit_single(SdfTokenKind::Colon);
                return;
            case '*':
                emit_single(SdfTokenKind::Star);
                return;
            case '"':
                lex_string();
                return;
            case '/':
                lex_slash();
                return;
            case '.':
                lex_dot();
                return;
            case '+':
                lex_sign(SdfTokenKind::Plus);
                return;
            case '-':
                lex_sign(SdfTokenKind::Minus);
                return;
            default:
                if (ascii_digit(character)) {
                    lex_number();
                } else {
                    lex_identifier();
                }
                return;
            }
        }

        SourceText source_;
        SdfLexerLimits limits_;
        std::size_t index_ { };
        std::size_t line_ { 1U };
        std::size_t column_ { 1U };
        std::size_t parenthesis_depth_ { };
        bool stopped_ { };
        SdfLexResult result_;
    };

} // namespace

SdfLexResult lex_sdf(SourceText source, const SdfLexerLimits limits)
{
    return SdfLexer(std::move(source), limits).run();
}

bool is_sdf_keyword(const std::string_view spelling) noexcept
{
    return std::ranges::any_of(kSdfKeywords, [&](const std::string_view keyword) {
        return ascii_iequals(spelling, keyword);
    });
}

const char* to_string(const SdfTokenKind kind) noexcept
{
    switch (kind) {
    case SdfTokenKind::LeftParenthesis:
        return "left-parenthesis";
    case SdfTokenKind::RightParenthesis:
        return "right-parenthesis";
    case SdfTokenKind::Colon:
        return "colon";
    case SdfTokenKind::Plus:
        return "plus";
    case SdfTokenKind::Minus:
        return "minus";
    case SdfTokenKind::Star:
        return "star";
    case SdfTokenKind::Slash:
        return "slash";
    case SdfTokenKind::Dot:
        return "dot";
    case SdfTokenKind::Keyword:
        return "keyword";
    case SdfTokenKind::Identifier:
        return "identifier";
    case SdfTokenKind::EscapedIdentifier:
        return "escaped-identifier";
    case SdfTokenKind::String:
        return "string";
    case SdfTokenKind::Number:
        return "number";
    case SdfTokenKind::Comment:
        return "comment";
    case SdfTokenKind::EndOfFile:
        return "end-of-file";
    }
    return "end-of-file";
}

} // namespace fsim::frontend
