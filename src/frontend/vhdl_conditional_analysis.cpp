// SPDX-License-Identifier: Apache-2.0
#include "vhdl_conditional_analysis_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::frontend {
namespace {

    constexpr std::size_t max_conditional_depth = 128U;

    [[nodiscard]] char ascii_lower(const char value) noexcept
    {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(value)));
    }

    [[nodiscard]] std::string lower_copy(const std::string_view value)
    {
        std::string result { value };
        std::ranges::transform(result, result.begin(), ascii_lower);
        return result;
    }

    [[nodiscard]] std::string_view trim(const std::string_view value) noexcept
    {
        std::size_t begin = 0U;
        while (begin != value.size()
            && std::isspace(static_cast<unsigned char>(value[begin]))) {
            ++begin;
        }
        std::size_t end = value.size();
        while (end != begin
            && std::isspace(static_cast<unsigned char>(value[end - 1U]))) {
            --end;
        }
        return value.substr(begin, end - begin);
    }

    enum class ExpressionTokenKind {
        Identifier,
        String,
        Equal,
        NotEqual,
        Less,
        LessEqual,
        Greater,
        GreaterEqual,
        LeftParen,
        RightParen,
        End,
        Invalid,
    };

    struct ExpressionToken {
        ExpressionTokenKind kind { ExpressionTokenKind::Invalid };
        std::string text;
    };

    class ConditionExpression {
    public:
        explicit ConditionExpression(const std::string_view text)
            : text_(text)
        {
            advance();
        }

        [[nodiscard]] std::optional<bool> evaluate()
        {
            const auto value = parse_or();
            if (!value || current_.kind != ExpressionTokenKind::End) {
                return std::nullopt;
            }
            return value;
        }

    private:
        [[nodiscard]] static bool identifier_start(const char value) noexcept
        {
            return std::isalpha(static_cast<unsigned char>(value)) != 0;
        }

        [[nodiscard]] static bool identifier_continue(const char value) noexcept
        {
            return std::isalnum(static_cast<unsigned char>(value)) != 0
                || value == '_';
        }

        [[nodiscard]] ExpressionToken next()
        {
            while (position_ != text_.size()
                && std::isspace(
                       static_cast<unsigned char>(text_[position_]))
                    != 0) {
                ++position_;
            }
            if (position_ == text_.size()) {
                return { ExpressionTokenKind::End, { } };
            }
            const char value = text_[position_++];
            if (identifier_start(value)) {
                const auto begin = position_ - 1U;
                while (position_ != text_.size()
                    && identifier_continue(text_[position_])) {
                    ++position_;
                }
                return { ExpressionTokenKind::Identifier,
                    lower_copy(text_.substr(begin, position_ - begin)) };
            }
            if (value == '"') {
                std::string result;
                while (position_ != text_.size()) {
                    const char character = text_[position_++];
                    if (character != '"') {
                        result.push_back(character);
                        continue;
                    }
                    if (position_ != text_.size() && text_[position_] == '"') {
                        ++position_;
                        result.push_back('"');
                        continue;
                    }
                    return { ExpressionTokenKind::String, std::move(result) };
                }
                return { ExpressionTokenKind::Invalid, { } };
            }
            switch (value) {
            case '=':
                return { ExpressionTokenKind::Equal, { } };
            case '/':
                if (position_ != text_.size() && text_[position_] == '=') {
                    ++position_;
                    return { ExpressionTokenKind::NotEqual, { } };
                }
                break;
            case '<':
                if (position_ != text_.size() && text_[position_] == '=') {
                    ++position_;
                    return { ExpressionTokenKind::LessEqual, { } };
                }
                return { ExpressionTokenKind::Less, { } };
            case '>':
                if (position_ != text_.size() && text_[position_] == '=') {
                    ++position_;
                    return { ExpressionTokenKind::GreaterEqual, { } };
                }
                return { ExpressionTokenKind::Greater, { } };
            case '(':
                return { ExpressionTokenKind::LeftParen, { } };
            case ')':
                return { ExpressionTokenKind::RightParen, { } };
            default:
                break;
            }
            return { ExpressionTokenKind::Invalid, { } };
        }

        void advance() { current_ = next(); }

        [[nodiscard]] bool at_word(const std::string_view word) const noexcept
        {
            return current_.kind == ExpressionTokenKind::Identifier
                && current_.text == word;
        }

        [[nodiscard]] std::optional<bool> parse_or()
        {
            auto result = parse_xor();
            while (result && at_word("or")) {
                advance();
                const auto right = parse_xor();
                if (!right) {
                    return std::nullopt;
                }
                *result = *result || *right;
            }
            return result;
        }

        [[nodiscard]] std::optional<bool> parse_xor()
        {
            auto result = parse_and();
            while (result && (at_word("xor") || at_word("xnor"))) {
                const bool invert = at_word("xnor");
                advance();
                const auto right = parse_and();
                if (!right) {
                    return std::nullopt;
                }
                *result = (*result != *right) != invert;
            }
            return result;
        }

        [[nodiscard]] std::optional<bool> parse_and()
        {
            auto result = parse_unary();
            while (result && at_word("and")) {
                advance();
                const auto right = parse_unary();
                if (!right) {
                    return std::nullopt;
                }
                *result = *result && *right;
            }
            return result;
        }

        [[nodiscard]] std::optional<bool> parse_unary()
        {
            if (at_word("not")) {
                advance();
                const auto operand = parse_unary();
                if (!operand) {
                    return std::nullopt;
                }
                return !*operand;
            }
            if (current_.kind == ExpressionTokenKind::LeftParen) {
                advance();
                const auto result = parse_or();
                if (!result || current_.kind != ExpressionTokenKind::RightParen) {
                    return std::nullopt;
                }
                advance();
                return result;
            }
            return parse_comparison();
        }

        [[nodiscard]] static std::string identifier_value(
            const std::string_view identifier)
        {
            constexpr std::array values {
                std::pair { "vhdl_version", "2019" },
                std::pair { "tool_type", "SIMULATION" },
                std::pair { "tool_vendor", "FSIM PROJECT" },
                std::pair { "tool_name", "FSIM" },
                std::pair { "tool_edition", "COMMUNITY" },
                std::pair { "tool_version", "3.0" },
            };
            const auto found = std::ranges::find(values, identifier, &decltype(values)::value_type::first);
            return found == values.end() ? std::string { } : std::string { found->second };
        }

        [[nodiscard]] std::optional<std::string> parse_value()
        {
            if (current_.kind == ExpressionTokenKind::String) {
                auto value = std::move(current_.text);
                advance();
                return value;
            }
            if (current_.kind == ExpressionTokenKind::Identifier) {
                auto value = identifier_value(current_.text);
                advance();
                return value;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<bool> parse_comparison()
        {
            const auto left = parse_value();
            if (!left) {
                return std::nullopt;
            }
            const auto operation = current_.kind;
            switch (operation) {
            case ExpressionTokenKind::Equal:
            case ExpressionTokenKind::NotEqual:
            case ExpressionTokenKind::Less:
            case ExpressionTokenKind::LessEqual:
            case ExpressionTokenKind::Greater:
            case ExpressionTokenKind::GreaterEqual:
                advance();
                break;
            default:
                return std::nullopt;
            }
            const auto right = parse_value();
            if (!right) {
                return std::nullopt;
            }
            switch (operation) {
            case ExpressionTokenKind::Equal:
                return *left == *right;
            case ExpressionTokenKind::NotEqual:
                return *left != *right;
            case ExpressionTokenKind::Less:
                return *left < *right;
            case ExpressionTokenKind::LessEqual:
                return *left <= *right;
            case ExpressionTokenKind::Greater:
                return *left > *right;
            case ExpressionTokenKind::GreaterEqual:
                return *left >= *right;
            default:
                return std::nullopt;
            }
        }

        std::string_view text_;
        std::size_t position_ { };
        ExpressionToken current_;
    };

    struct ConditionalFrame {
        bool parent_active { };
        bool branch_taken { };
        bool active { };
        bool else_seen { };
        SourceSpan opener;
    };

    struct ProtectionFrame {
        bool encrypted { };
        bool diagnosed { };
        SourceSpan opener;
    };

    class ConditionalAnalysis {
    public:
        ConditionalAnalysis(SourceText source, const VhdlStandard standard)
            : source_(std::move(source))
            , output_(source_)
            , standard_(standard)
        {
        }

        [[nodiscard]] VhdlConditionalAnalysisResult run()
        {
            std::size_t line_begin = 0U;
            std::size_t line = 1U;
            while (line_begin < source_.text.size()) {
                const auto newline = source_.text.find_first_of("\r\n", line_begin);
                const auto line_end = newline == std::string::npos
                    ? source_.text.size()
                    : newline;
                process_line(line_begin, line_end, line);
                if (newline == std::string::npos) {
                    line_begin = source_.text.size();
                } else if (source_.text[newline] == '\r'
                    && newline + 1U < source_.text.size()
                    && source_.text[newline + 1U] == '\n') {
                    line_begin = newline + 2U;
                } else {
                    line_begin = newline + 1U;
                }
                ++line;
            }
            for (const auto& frame : stack_) {
                diagnose("FSIM-VHDL-CA-003",
                    "unterminated conditional analysis directive; add `end if",
                    frame.opener);
            }
            if (protection_ && protection_->diagnosed) {
                diagnose("FSIM-VHDL-PROTECT-004",
                    protection_->encrypted
                        ? "unterminated protected decryption envelope; add `protect end_protected"
                        : "unterminated protected source envelope; add `protect end",
                    protection_->opener);
            }
            return { std::move(output_), std::move(diagnostics_) };
        }

    private:
        [[nodiscard]] bool active() const noexcept
        {
            return stack_.empty() || stack_.back().active;
        }

        [[nodiscard]] SourceSpan line_span(
            const std::size_t line_begin, const std::size_t line_end,
            const std::size_t line, const std::size_t directive_begin) const
        {
            return SourceSpan { source_.name,
                SourceLocation { directive_begin, line, directive_begin - line_begin + 1U },
                SourceLocation { line_end, line, line_end - line_begin + 1U },
                source_.name, { } };
        }

        void diagnose(std::string code, std::string message, SourceSpan span)
        {
            diagnostics_.push_back(Diagnostic { DiagnosticSeverity::Error,
                std::move(code), std::move(message), std::move(span), { } });
        }

        void mask(const std::size_t begin, const std::size_t end)
        {
            std::fill(output_.text.begin() + static_cast<std::ptrdiff_t>(begin),
                output_.text.begin() + static_cast<std::ptrdiff_t>(end), ' ');
        }

        [[nodiscard]] static std::pair<std::string_view, std::string_view>
        split_word(const std::string_view value) noexcept
        {
            std::size_t end = 0U;
            while (end != value.size()
                && std::isalpha(static_cast<unsigned char>(value[end])) != 0) {
                ++end;
            }
            return { value.substr(0U, end), trim(value.substr(end)) };
        }

        [[nodiscard]] static std::pair<std::string_view, std::string_view>
        split_protection_word(const std::string_view value) noexcept
        {
            std::size_t end = 0U;
            while (end != value.size()
                && (std::isalpha(static_cast<unsigned char>(value[end])) != 0
                    || value[end] == '_')) {
                ++end;
            }
            return { value.substr(0U, end), trim(value.substr(end)) };
        }

        [[nodiscard]] static std::optional<std::string>
        directive_string(std::string_view value)
        {
            value = trim(value);
            if (value.size() < 2U || value.front() != '"') {
                return std::nullopt;
            }
            std::string result;
            for (std::size_t index = 1U; index < value.size(); ++index) {
                if (value[index] != '"') {
                    result.push_back(value[index]);
                    continue;
                }
                if (index + 1U < value.size() && value[index + 1U] == '"') {
                    result.push_back('"');
                    ++index;
                    continue;
                }
                return trim(value.substr(index + 1U)).empty()
                    ? std::optional<std::string> { std::move(result) }
                    : std::nullopt;
            }
            return std::nullopt;
        }

        [[nodiscard]] static std::string_view strip_line_comment(
            const std::string_view value) noexcept
        {
            bool in_string = false;
            for (std::size_t index = 0U; index + 1U < value.size(); ++index) {
                if (value[index] == '"') {
                    if (in_string && value[index + 1U] == '"') {
                        ++index;
                    } else {
                        in_string = !in_string;
                    }
                    continue;
                }
                if (!in_string && value[index] == '-' && value[index + 1U] == '-') {
                    return value.substr(0U, index);
                }
            }
            return value;
        }

        void update_block_comment_depth(
            const std::size_t line_begin, const std::size_t line_end) noexcept
        {
            bool in_string = false;
            for (auto index = line_begin; index < line_end; ++index) {
                if (block_comment_depth_ == 0U && source_.text[index] == '"') {
                    if (in_string && index + 1U < line_end
                        && source_.text[index + 1U] == '"') {
                        ++index;
                    } else {
                        in_string = !in_string;
                    }
                    continue;
                }
                if (in_string) {
                    continue;
                }
                if (block_comment_depth_ == 0U && index + 1U < line_end
                    && source_.text[index] == '-' && source_.text[index + 1U] == '-') {
                    return;
                }
                if (index + 1U < line_end && source_.text[index] == '/'
                    && source_.text[index + 1U] == '*') {
                    ++block_comment_depth_;
                    ++index;
                    continue;
                }
                if (block_comment_depth_ != 0U && index + 1U < line_end
                    && source_.text[index] == '*' && source_.text[index + 1U] == '/') {
                    --block_comment_depth_;
                    ++index;
                }
            }
        }

        void process_protection(
            const std::string_view remainder, const SourceSpan& span)
        {
            const auto [raw_keyword, arguments]
                = split_protection_word(remainder);
            const auto keyword = lower_copy(raw_keyword);
            const bool reporting = active();
            const bool supported = standard_ >= VhdlStandard::Vhdl2008;
            const bool control_reporting = reporting && supported;
            if (!supported && reporting) {
                diagnose("FSIM-VHDL-PROTECT-001",
                    "protect tool directives require VHDL-2008 or later",
                    span);
            }
            if (keyword == "begin" || keyword == "begin_protected") {
                if (!arguments.empty() && control_reporting) {
                    diagnose("FSIM-VHDL-PROTECT-002",
                        "a protected envelope opening directive takes no operands",
                        span);
                }
                if (protection_) {
                    if (control_reporting) {
                        diagnose("FSIM-VHDL-PROTECT-002",
                            "protected source envelopes cannot be nested", span);
                    }
                    return;
                }
                protection_ = ProtectionFrame {
                    keyword == "begin_protected", control_reporting, span
                };
                if (protection_->encrypted && control_reporting) {
                    diagnose("FSIM-VHDL-PROTECT-003",
                        "the protected decryption envelope requires an unavailable key provider",
                        span);
                }
                return;
            }
            if (keyword == "end" || keyword == "end_protected") {
                const bool encrypted_end = keyword == "end_protected";
                if (!arguments.empty() && control_reporting) {
                    diagnose("FSIM-VHDL-PROTECT-002",
                        "a protected envelope closing directive takes no operands",
                        span);
                }
                if (!protection_ || protection_->encrypted != encrypted_end) {
                    if (control_reporting) {
                        diagnose("FSIM-VHDL-PROTECT-002",
                            "protected envelope closing directive has no matching opening directive",
                            span);
                    }
                    return;
                }
                protection_.reset();
                return;
            }
            // The remaining protect pragma expressions are metadata for an
            // encrypting or decrypting tool. They do not alter plain VHDL
            // semantics and encrypted metadata is never exposed downstream.
        }

        [[nodiscard]] std::optional<bool> condition(
            std::string_view text, const SourceSpan& span)
        {
            text = trim(text);
            const auto [last, before_last] = [&]() {
                const auto position = text.find_last_not_of(" \t");
                if (position == std::string_view::npos) {
                    return std::pair { std::string_view { }, std::string_view { } };
                }
                const auto word_begin = text.find_last_of(" \t", position);
                return std::pair { text.substr(
                                       word_begin == std::string_view::npos ? 0U
                                                                            : word_begin + 1U,
                                       position - (word_begin == std::string_view::npos ? 0U : word_begin + 1U)
                                           + 1U),
                    trim(text.substr(0U,
                        word_begin == std::string_view::npos ? 0U : word_begin)) };
            }();
            if (lower_copy(last) != "then" || before_last.empty()) {
                diagnose("FSIM-VHDL-CA-002",
                    "a conditional analysis `if or `elsif directive requires a condition followed by then",
                    span);
                return std::nullopt;
            }
            const auto result = ConditionExpression(before_last).evaluate();
            if (!result) {
                diagnose("FSIM-VHDL-CA-002",
                    "invalid conditional analysis expression", span);
            }
            return result;
        }

        void process_line(const std::size_t line_begin, const std::size_t line_end,
            const std::size_t line)
        {
            auto first = line_begin;
            while (first != line_end
                && (source_.text[first] == ' ' || source_.text[first] == '\t')) {
                ++first;
            }
            if (protection_ && protection_->encrypted) {
                if (first != line_end && source_.text[first] == '`') {
                    const auto protected_text = trim(strip_line_comment(
                        std::string_view { source_.text }.substr(
                            first + 1U, line_end - first - 1U)));
                    const auto [directive_word, directive_remainder]
                        = split_word(protected_text);
                    if (lower_copy(directive_word) == "protect") {
                        const auto [keyword, arguments]
                            = split_protection_word(directive_remainder);
                        const auto control = lower_copy(keyword);
                        if (control == "begin" || control == "begin_protected"
                            || control == "end"
                            || control == "end_protected") {
                            const auto span = line_span(
                                line_begin, line_end, line, first);
                            process_protection(directive_remainder, span);
                            mask(line_begin, line_end);
                            return;
                        }
                        (void)arguments;
                    }
                }
                mask(line_begin, line_end);
                return;
            }
            if (first == line_end || source_.text[first] != '`'
                || block_comment_depth_ != 0U) {
                update_block_comment_depth(line_begin, line_end);
                if (!active()) {
                    mask(line_begin, line_end);
                }
                return;
            }

            const auto span = line_span(line_begin, line_end, line, first);
            const auto [word, remainder] = split_word(
                trim(strip_line_comment(std::string_view { source_.text }.substr(
                    first + 1U, line_end - first - 1U))));
            const auto directive = lower_copy(word);
            if (directive != "if" && directive != "elsif" && directive != "else"
                && directive != "end" && directive != "warning"
                && directive != "error" && directive != "protect") {
                if (!active()) {
                    mask(line_begin, line_end);
                }
                return;
            }
            mask(line_begin, line_end);

            if (directive == "protect") {
                process_protection(remainder, span);
                return;
            }

            if (standard_ != VhdlStandard::Vhdl2019) {
                diagnose("FSIM-VHDL-CA-001",
                    "conditional analysis directives require VHDL-2019; select that revision or remove the directive",
                    span);
            }

            if (directive == "warning" || directive == "error") {
                const auto message = directive_string(remainder);
                if (!message) {
                    diagnose("FSIM-VHDL-CA-002",
                        "a conditional analysis warning or error directive requires exactly one string literal",
                        span);
                    return;
                }
                if (standard_ != VhdlStandard::Vhdl2019 || !active()) {
                    return;
                }
                diagnostics_.push_back(Diagnostic {
                    directive == "warning" ? DiagnosticSeverity::Warning
                                           : DiagnosticSeverity::Error,
                    directive == "warning" ? "FSIM-VHDL-CA-005"
                                           : "FSIM-VHDL-CA-006",
                    *message, span, { } });
                return;
            }

            if (overflow_depth_ != 0U) {
                if (directive == "if") {
                    (void)condition(remainder, span);
                    ++overflow_depth_;
                } else if (directive == "elsif") {
                    (void)condition(remainder, span);
                } else if (directive == "end") {
                    const auto [ending_word, ending_remainder] = split_word(remainder);
                    if ((!ending_word.empty() && lower_copy(ending_word) != "if")
                        || !ending_remainder.empty()) {
                        diagnose("FSIM-VHDL-CA-002",
                            "a conditional analysis closing directive must be `end or `end if",
                            span);
                    } else {
                        --overflow_depth_;
                    }
                } else if (!remainder.empty()) {
                    diagnose("FSIM-VHDL-CA-002",
                        "a conditional analysis `else directive takes no operands", span);
                }
                return;
            }

            if (directive == "if") {
                const auto selected = condition(remainder, span);
                if (stack_.size() == max_conditional_depth) {
                    diagnose("FSIM-VHDL-CA-004",
                        "conditional analysis nesting exceeds 128 levels", span);
                    overflow_depth_ = 1U;
                    return;
                }
                const bool parent = active();
                const bool branch = selected.value_or(false);
                stack_.push_back(ConditionalFrame {
                    parent, branch, parent && branch, false, span });
                return;
            }
            if (stack_.empty()) {
                diagnose("FSIM-VHDL-CA-003",
                    "conditional analysis branch directive has no matching `if", span);
                return;
            }
            auto& frame = stack_.back();
            if (directive == "elsif") {
                const auto selected = condition(remainder, span);
                if (frame.else_seen) {
                    diagnose("FSIM-VHDL-CA-003",
                        "`elsif cannot follow `else in a conditional analysis group", span);
                    frame.active = false;
                    return;
                }
                const bool branch = selected.value_or(false);
                frame.active = frame.parent_active && !frame.branch_taken && branch;
                frame.branch_taken = frame.branch_taken || branch;
                return;
            }
            if (directive == "else") {
                if (!remainder.empty()) {
                    diagnose("FSIM-VHDL-CA-002",
                        "a conditional analysis `else directive takes no operands", span);
                }
                if (frame.else_seen) {
                    diagnose("FSIM-VHDL-CA-003",
                        "duplicate `else in a conditional analysis group", span);
                    frame.active = false;
                    return;
                }
                frame.else_seen = true;
                frame.active = frame.parent_active && !frame.branch_taken;
                frame.branch_taken = true;
                return;
            }

            const auto [ending_word, ending_remainder] = split_word(remainder);
            if ((!ending_word.empty() && lower_copy(ending_word) != "if")
                || !ending_remainder.empty()) {
                diagnose("FSIM-VHDL-CA-002",
                    "a conditional analysis closing directive must be `end or `end if", span);
                return;
            }
            stack_.pop_back();
        }

        SourceText source_;
        SourceText output_;
        VhdlStandard standard_;
        std::size_t block_comment_depth_ { };
        std::size_t overflow_depth_ { };
        std::vector<ConditionalFrame> stack_;
        std::optional<ProtectionFrame> protection_;
        std::vector<Diagnostic> diagnostics_;
    };

} // namespace

VhdlConditionalAnalysisResult analyze_vhdl_conditionals(
    SourceText source, const VhdlStandard standard)
{
    return ConditionalAnalysis(std::move(source), standard).run();
}

} // namespace fsim::frontend
