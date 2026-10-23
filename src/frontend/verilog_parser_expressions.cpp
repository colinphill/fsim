// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"
#include "fsim/frontend/systemverilog_standard_package.hpp"

#include <algorithm>
#include <charconv>
#include <limits>

namespace fsim::frontend {

[[nodiscard]] std::optional<StandardRevision>
verilog_system_service_standard(std::string_view name);

// The spelling of a type actual in a `C#(...)` selection, with a class
// specialization's own actuals (`R#(P#(T))`), so distinct selections keep
// distinct spellings.
std::string class_actual_type_spelling(const Type& type)
{
    // A virtual interface type keeps its keyword (`C#(virtual I.mp)`).
    if (type.systemverilog_virtual_interface) {
        return "virtual " + type.systemverilog_interface_type
            + (type.systemverilog_interface_modport.empty()
                    ? std::string { }
                    : "." + type.systemverilog_interface_modport);
    }
    if (type.named_type.empty()) {
        return type.spelling;
    }
    if (type.systemverilog_class_parameter_actuals.empty()
        || type.named_type.find("::") != std::string::npos) {
        return type.named_type;
    }
    auto spelling = type.named_type + "#(";
    for (std::size_t index = 0;
        index < type.systemverilog_class_parameter_actuals.size(); ++index) {
        const auto& actual = type.systemverilog_class_parameter_actuals[index];
        if (index != 0U) {
            spelling += ",";
        }
        if (actual.name) {
            spelling += "." + *actual.name + "(";
        }
        spelling += actual.type_actual
            ? class_actual_type_spelling(*actual.type_actual)
            : actual.value.text;
        if (actual.name) {
            spelling += ")";
        }
    }
    return spelling + ")";
}

DelayAlternative VerilogParser::parse_verilog_delay_alternative(
    const bool parenthesized)
{
    // `#1 ->e;` triggers an event after the delay; `->` is not an
    // implication inside a delay value.
    ++constraint_parse_depth_;
    struct DelayDepth {
        std::size_t& depth;
        ~DelayDepth() { --depth; }
    } delay_depth { constraint_parse_depth_ };
    DelayAlternative alternative;
    const auto start = current();
    if (at(TokenKind::Colon) || at(TokenKind::Comma)
        || at(TokenKind::RightParen) || at(TokenKind::Semicolon)
        || at(TokenKind::EndOfFile)) {
        error(
            current(),
            "FSIM-SV-PARSE-023",
            "expected delay magnitude expression");
        alternative.span = current().span;
        return alternative;
    }
    if (at(TokenKind::Number) && current().text == "1"
        && at(TokenKind::Identifier, 1) && current(1).text == "step") {
        // `#1step` is one unit of the global time precision (IEEE
        // 1800-2017 14.4), one project tick.
        advance();
        advance();
        alternative.magnitude = 1;
        alternative.span = span_from(start, previous());
        return alternative;
    }
    // A time literal's unit follows its number without space (IEEE
    // 1800-2017 5.8); `#1 s.x = v` delays an assignment to `s.x`.
    const bool explicit_time_literal = at(TokenKind::Number)
        && at(TokenKind::Identifier, 1)
        && (!current(1).expansion_stack.empty()
            || current().span.end.offset == current(1).span.begin.offset)
        && time_unit_femtoseconds(current(1).text).has_value();
    // An unparenthesized delay is a single number (IEEE 1800-2017 A.2.2.3),
    // so in `#1 'h5` or `#100'b0` the based literal is the next expression,
    // and in `#69 - 1` the delay is 69.
    if (!parenthesized && !explicit_time_literal && at(TokenKind::Number)) {
        auto& token = tokens_[index_];
        const auto quote = token.text.find('\'');
        if (quote != std::string::npos && quote != 0U
            && std::ranges::all_of(token.text.substr(0U, quote),
                [](const char character) {
                    return (character >= '0' && character <= '9')
                        || character == '_';
                })) {
            auto rest = token;
            rest.text = token.text.substr(quote);
            token.text.resize(quote);
            tokens_.insert(
                tokens_.begin() + static_cast<std::ptrdiff_t>(index_) + 1,
                std::move(rest));
        }
    }
    const bool lone_number = !parenthesized && !explicit_time_literal
        && at(TokenKind::Number)
        && current().text.find('\'') == std::string::npos;
    // An unparenthesized identifier delay is only the name (`#d (y, a)` on
    // a gate is not a call).
    const bool lone_identifier = !parenthesized && !explicit_time_literal
        && at(TokenKind::Identifier) && at(TokenKind::LeftParen, 1);
    auto expression = explicit_time_literal
        ? Expression { }
        : lone_number
        ? Expression { ExpressionKind::IntegerLiteral, advance().text, { },
              previous().span }
        : lone_identifier
        ? Expression { ExpressionKind::Identifier, advance().text, { },
              previous().span }
        : parse_expression();
    const bool plain_decimal_literal = expression.kind == ExpressionKind::IntegerLiteral
        && decimal_ratio(expression.text).has_value();
    const auto magnitude = explicit_time_literal
        ? advance()
        : Token {
              TokenKind::Number,
              expression.text,
              expression.span,
              { }
          };
    if (explicit_time_literal || plain_decimal_literal) {
        const auto parsed = decimal_ratio(magnitude.text);
        alternative.magnitude = parsed->numerator;
        alternative.divisor = parsed->denominator;
        if (at(TokenKind::Identifier)
            && (!current().expansion_stack.empty()
                || previous().span.end.offset == current().span.begin.offset)
            && time_unit_femtoseconds(current().text)) {
            const auto explicit_unit = advance();
            alternative.unit = explicit_unit.text;
            if (language_ != Language::SystemVerilog2017) {
                error(
                    explicit_unit,
                    "FSIM-SV-SEM-051",
                    "an explicitly unit-suffixed delay literal requires "
                    "SystemVerilog-2017");
            }
        } else if (!module_time_unit_.empty()) {
            if (alternative.magnitude
                > std::numeric_limits<std::uint64_t>::max()
                    / module_time_unit_magnitude_) {
                error(
                    magnitude,
                    "FSIM-SV-SEM-010",
                    "delay magnitude overflows after applying "
                    "SystemVerilog timeunit");
            } else {
                alternative.magnitude *= module_time_unit_magnitude_;
                alternative.unit = module_time_unit_;
            }
        }
        // Without a timescale, a fractional delay counts ticks of the
        // project resolution, the implementation's default time unit
        // (IEEE 1800-2017 3.14.2.3), and rounds to it.
    } else if (
        (expression.kind == ExpressionKind::IntegerLiteral
            || expression.kind == ExpressionKind::LogicLiteral)
        && (expression.text.find('\'') == std::string::npos
            || !parenthesized)) {
        const bool nondecimal = expression.text.find('\'') != std::string::npos;
        error(
            start,
            nondecimal ? "FSIM-SV-SEM-002" : "FSIM-SV-SEM-049",
            nondecimal
                ? "delay magnitude must be a decimal literal"
                : "delay magnitude must be a representable nonnegative "
                  "decimal literal");
    } else {
        // A parenthesized delay is any constant or run-time expression,
        // including a based literal (`#(32'hf0)`, A.2.2.3).
        alternative.expression = std::move(expression);
        if (!module_time_unit_.empty()) {
            alternative.magnitude = module_time_unit_magnitude_;
            alternative.unit = module_time_unit_;
        } else {
            alternative.magnitude = 1;
        }
    }
    alternative.span = span_from(start, previous());
    return alternative;
}

void VerilogParser::set_selected_delay(
    Delay& delay,
    const DelayAlternative& alternative)
{
    delay.magnitude = alternative.magnitude;
    delay.divisor = alternative.divisor;
    delay.unit = alternative.unit;
    delay.expression = alternative.expression;
}

Delay VerilogParser::parse_verilog_delay_value(const bool parenthesized)
{
    Delay delay;
    auto first = parse_verilog_delay_alternative(parenthesized);
    set_selected_delay(delay, first);
    if (match(TokenKind::Colon)) {
        if (!parenthesized) {
            error(
                previous(),
                "FSIM-SV-SEM-052",
                "a min:typ:max delay triple must be parenthesized");
        }
        auto typical = parse_verilog_delay_alternative();
        expect(
            TokenKind::Colon,
            "second ':' in min:typ:max delay",
            "FSIM-SV-PARSE-134");
        auto maximum = parse_verilog_delay_alternative();
        delay.minimum = std::move(first);
        delay.typical = std::move(typical);
        delay.maximum = std::move(maximum);
        set_selected_delay(delay, *delay.typical);
    }
    delay.span = cover(first.span, previous().span);
    return delay;
}

Delay VerilogParser::parse_verilog_delay(
    const Token& start,
    const std::size_t maximum_values)
{
    const bool parenthesized = match(TokenKind::LeftParen);
    auto delay = parse_verilog_delay_value(parenthesized);
    if (parenthesized) {
        while (match(TokenKind::Comma)) {
            const auto comma = previous();
            if (at(TokenKind::RightParen)) {
                error(
                    comma,
                    "FSIM-SV-PARSE-135",
                    "expected a delay value after ','");
                break;
            }
            const auto value_start = current();
            delay.additional_values.push_back(
                parse_verilog_delay_value(true));
            if (delay.additional_values.size() + 1 > maximum_values) {
                error(
                    value_start,
                    "FSIM-SV-SEM-053",
                    "this delay control accepts at most "
                        + std::to_string(maximum_values)
                        + " transition delay value"
                        + (maximum_values == 1 ? "" : "s"));
            }
        }
        expect(TokenKind::RightParen, "')' after delay",
            "FSIM-SV-PARSE-024");
    }
    delay.span = span_from(start, previous());
    return delay;
}

Expression VerilogParser::parse_lvalue()
{
    if (match(TokenKind::LeftBrace)) {
        const auto start = previous();
        std::vector<Expression> operands;
        do {
            operands.push_back(parse_lvalue());
        } while (match(TokenKind::Comma));
        expect(
            TokenKind::RightBrace,
            "'}' after concatenated assignment target",
            "FSIM-SV-PARSE-345");
        return Expression {
            ExpressionKind::Concatenation,
            "concat",
            std::move(operands),
            cover(start.span, previous().span)
        };
    }
    const auto has_receiver_call = [&] {
        std::size_t parenthesis_depth { };
        std::size_t square_depth { };
        for (auto token_index = position();
            token_index < tokens_.size();
            ++token_index) {
            const auto kind = tokens_[token_index].kind;
            if (kind == TokenKind::EndOfFile
                || (kind == TokenKind::Semicolon
                    && parenthesis_depth == 0U && square_depth == 0U)) {
                return false;
            }
            if (parenthesis_depth == 0U && square_depth == 0U) {
                if (kind == TokenKind::LeftParen) {
                    return true;
                }
                if (kind == TokenKind::LessEqual
                    || is_blocking_assignment_operator(kind)) {
                    return false;
                }
            }
            switch (kind) {
            case TokenKind::LeftParen:
                ++parenthesis_depth;
                break;
            case TokenKind::RightParen:
                if (parenthesis_depth != 0U) {
                    --parenthesis_depth;
                }
                break;
            case TokenKind::LeftBracket:
                ++square_depth;
                break;
            case TokenKind::RightBracket:
                if (square_depth != 0U) {
                    --square_depth;
                }
                break;
            default:
                break;
            }
        }
        return false;
    };
    if (at(TokenKind::Identifier) && has_receiver_call()) {
        const auto before = position();
        const auto source_order = systemverilog_source_order();
        const auto implicit_reference_count = implicit_net_references_.size();
        auto selected = parse_primary();
        const Expression* lvalue_base = &selected;
        while ((lvalue_base->kind == ExpressionKind::Index
                   || lvalue_base->kind == ExpressionKind::Slice)
            && !lvalue_base->operands.empty()) {
            lvalue_base = &lvalue_base->operands.front();
        }
        if (lvalue_base->kind == ExpressionKind::Call
            && lvalue_base->text.starts_with("@sv-select:")) {
            // A function or method returning a class handle can be the
            // receiver of a writable member selection. Keep its member and
            // index expressions in the same tree used by ordinary lvalues.
            selected.systemverilog_source_order = source_order;
            return selected;
        }
        // `C#(0)::b[i]` names a static property of a class
        // specialization (IEEE 1800-2017 8.25.1).
        if (lvalue_base->kind == ExpressionKind::Identifier
            && lvalue_base->systemverilog_class_selection) {
            selected.systemverilog_source_order = source_order;
            return selected;
        }
        rewind(before);
        implicit_net_references_.resize(implicit_reference_count);
    }
    const auto source_order = systemverilog_source_order();
    const auto name = keyword("this") || keyword("super")
        ? advance()
        : expect_identifier("assignment target");
    Expression expression { ExpressionKind::Identifier, name.text, { },
        name.span };
    expression.systemverilog_source_order = source_order;
    // `g[0].D` names a member of a generate block array element, not the
    // simple name `g` (IEEE 1800-2017 27.6).
    bool member_after_select { };
    for (;;) {
        if (at(TokenKind::Dot) || at(TokenKind::Scope)) {
            member_after_select = member_after_select
                || expression.kind != ExpressionKind::Identifier;
            const auto separator = advance();
            const auto selected = expect_identifier("selected name");
            expression.text += separator.kind == TokenKind::Scope ? "::" : ".";
            expression.text += selected.text;
            expression.span = cover(expression.span, selected.span);
        } else if (match(TokenKind::LeftBracket)) {
            Expression first = parse_expression();
            if (at(TokenKind::PlusColon)
                || at(TokenKind::MinusColon)) {
                const auto direction = advance();
                (void)require_standard(
                    "an indexed part-select",
                    StandardRevision::Verilog2001,
                    direction,
                    "FSIM-SV-PARSE-347");
                Expression width = parse_expression();
                expect(TokenKind::RightBracket,
                    "']' after indexed part-select",
                    "FSIM-SV-PARSE-025");
                expression = Expression { ExpressionKind::Slice, direction.text,
                    { std::move(expression), std::move(first),
                        std::move(width) },
                    cover(expression.span, previous().span) };
            } else if (match(TokenKind::Colon)) {
                Expression second = parse_expression();
                expect(TokenKind::RightBracket, "']' after part-select",
                    "FSIM-SV-PARSE-025");
                expression = Expression { ExpressionKind::Slice, ":",
                    { std::move(expression), std::move(first),
                        std::move(second) },
                    cover(expression.span, previous().span) };
            } else {
                expect(TokenKind::RightBracket, "']' after index",
                    "FSIM-SV-PARSE-026");
                expression = Expression { ExpressionKind::Index, "index",
                    { std::move(expression), std::move(first) },
                    cover(expression.span, previous().span) };
            }
        } else {
            break;
        }
    }
    const Expression* root = &expression;
    while ((root->kind == ExpressionKind::Index
               || root->kind == ExpressionKind::Slice)
        && !root->operands.empty()) {
        root = &root->operands.front();
    }
    if (root->kind == ExpressionKind::Identifier
        && root->text == name.text && !member_after_select) {
        note_implicit_net_reference(
            name, expression.kind == ExpressionKind::Call);
    }
    return expression;
}

std::optional<VerilogParser::BinaryOperation> VerilogParser::binary_operation() const
{
    if (in_verilog_attribute_ && at(TokenKind::Star)
        && at(TokenKind::RightParen, 1)) {
        return std::nullopt;
    }
    if (at(TokenKind::OrOr)) {
        return BinaryOperation { 1, "||" };
    }
    if (at(TokenKind::AndAnd)) {
        return BinaryOperation { 2, "&&" };
    }
    if (at(TokenKind::Pipe)) {
        return BinaryOperation { 3, "|" };
    }
    if (at(TokenKind::Caret) || at(TokenKind::TildeCaret)
        || at(TokenKind::CaretTilde)) {
        return BinaryOperation { 4, current().text };
    }
    if (at(TokenKind::Ampersand)) {
        return BinaryOperation { 5, "&" };
    }
    if (at(TokenKind::EqualEqual) || at(TokenKind::CaseEqual)
        || at(TokenKind::WildcardEqual)
        || at(TokenKind::CaseNotEqual)
        || at(TokenKind::WildcardNotEqual)
        || (at(TokenKind::NotEqual) && current().text == "!=")) {
        return BinaryOperation { 6, current().text };
    }
    // `<` followed by `->` is the equivalence operator `<->`.
    if (at(TokenKind::Less) && at(TokenKind::ThinArrow, 1)) {
        return std::nullopt;
    }
    if (at(TokenKind::Less) || at(TokenKind::LessEqual) || at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
        return BinaryOperation { 7, current().text };
    }
    if (at(TokenKind::ShiftLeft)
        || at(TokenKind::ShiftRight)
        || at(TokenKind::ArithmeticShiftLeft)
        || at(TokenKind::ArithmeticShiftRight)) {
        return BinaryOperation { 8, current().text };
    }
    if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
        return BinaryOperation { 9, current().text };
    }
    if (at(TokenKind::Star) || at(TokenKind::Slash) || at(TokenKind::Percent)) {
        return BinaryOperation { 10, current().text };
    }
    if (at(TokenKind::Power)) {
        return BinaryOperation { 11, "**" };
    }
    return std::nullopt;
}

Expression VerilogParser::parse_expression(int minimum_precedence)
{
    Expression left = parse_unary();
    for (;;) {
        constexpr int membership_precedence = 6;
        if (at(TokenKind::Identifier)
            && current().text == "inside"
            && membership_precedence >= minimum_precedence) {
            const auto inside = advance();
            (void)require_standard(
                "the inside membership operator",
                StandardRevision::SystemVerilog2005,
                inside,
                "FSIM-SV-PARSE-347");
            if (language_ != Language::SystemVerilog2017) {
                error(
                    inside,
                    "FSIM-SV-PARSE-175",
                    "the inside membership operator requires SystemVerilog");
            }
            expect(
                TokenKind::LeftBrace,
                "'{' after inside",
                "FSIM-SV-PARSE-176");
            std::vector<Expression> operands;
            operands.push_back(std::move(left));
            if (at(TokenKind::RightBrace)) {
                error(
                    current(),
                    "FSIM-SV-PARSE-177",
                    "an inside membership list must not be empty");
            } else {
                for (;;) {
                    if (match(TokenKind::LeftBracket)) {
                        const auto range_start = previous();
                        auto low = parse_expression();
                        std::string range_kind { "@inside-range" };
                        if (at(TokenKind::AbsoluteTolerance)
                            || at(TokenKind::RelativeTolerance)) {
                            const auto tolerance = advance();
                            (void)require_standard(
                                "inside tolerance ranges",
                                StandardRevision::SystemVerilog2023,
                                tolerance,
                                "FSIM-SV-PARSE-347");
                            range_kind = tolerance.kind
                                    == TokenKind::AbsoluteTolerance
                                ? "@inside-absolute-tolerance"
                                : "@inside-relative-tolerance";
                        } else {
                            expect(
                                TokenKind::Colon,
                                "':' or a tolerance operator in inside range",
                                "FSIM-SV-PARSE-178");
                        }
                        auto high = parse_expression();
                        expect(
                            TokenKind::RightBracket,
                            "']' after inside range",
                            "FSIM-SV-PARSE-179");
                        operands.push_back(Expression {
                            ExpressionKind::Call,
                            std::move(range_kind),
                            { std::move(low), std::move(high) },
                            cover(range_start.span, previous().span) });
                    } else {
                        operands.push_back(parse_expression());
                    }
                    if (!match(TokenKind::Comma)) {
                        break;
                    }
                }
            }
            expect(
                TokenKind::RightBrace,
                "'}' after inside membership list",
                "FSIM-SV-PARSE-176");
            const auto combined = cover(
                operands.empty() ? inside.span : operands.front().span,
                previous().span);
            left = Expression {
                ExpressionKind::Call,
                "inside",
                std::move(operands),
                combined
            };
            continue;
        }
        const auto operation = binary_operation();
        if (!operation || operation->precedence < minimum_precedence) {
            break;
        }
        if (at(TokenKind::Power)
            || at(TokenKind::ArithmeticShiftLeft)
            || at(TokenKind::ArithmeticShiftRight)) {
            (void)require_standard(
                "operator '" + current().text + "'",
                StandardRevision::Verilog2001,
                current(),
                "FSIM-SV-PARSE-347");
        }
        if (at(TokenKind::WildcardEqual)
            || at(TokenKind::WildcardNotEqual)) {
            (void)require_standard(
                "a wildcard equality operator",
                StandardRevision::SystemVerilog2005,
                current(),
                "FSIM-SV-PARSE-347");
        }
        if (language_ != Language::SystemVerilog2017
            && (at(TokenKind::WildcardEqual)
                || at(TokenKind::WildcardNotEqual))) {
            error(
                current(),
                "FSIM-VERILOG-SEM-004",
                "wildcard equality operators require SystemVerilog");
        }
        advance();
        // An attribute instance may follow a binary operator (IEEE
        // 1800-2017 5.12).
        if (verilog_attribute_instance_start()) {
            parse_verilog_attribute_instances();
        }
        Expression right = parse_expression(operation->precedence + 1);
        const auto combined = cover(left.span, right.span);
        left = Expression { ExpressionKind::Binary, operation->name,
            { std::move(left), std::move(right) }, combined };
    }
    if (minimum_precedence == 0 && match(TokenKind::Question)) {
        if (verilog_attribute_instance_start()) {
            parse_verilog_attribute_instances();
        }
        Expression when_true = parse_expression();
        expect(TokenKind::Colon, "':' in conditional expression",
            "FSIM-SV-PARSE-027");
        Expression when_false = parse_expression();
        const auto combined = cover(left.span, when_false.span);
        left = Expression { ExpressionKind::Call, "?:",
            { std::move(left), std::move(when_true),
                std::move(when_false) },
            combined };
    }
    // Logical equivalence `a <-> b` (IEEE 1800-2017 11.4.7) is true when
    // both operands have the same truth value: !(!a ^ !b).
    if (minimum_precedence == 0 && language_ == Language::SystemVerilog2017
        && at(TokenKind::Less) && at(TokenKind::ThinArrow, 1)) {
        advance();
        advance();
        Expression right = parse_expression();
        const auto combined = cover(left.span, right.span);
        Expression left_truth { ExpressionKind::Unary, "!",
            { std::move(left) }, combined };
        Expression right_truth { ExpressionKind::Unary, "!",
            { std::move(right) }, combined };
        Expression different { ExpressionKind::Binary, "^",
            { std::move(left_truth), std::move(right_truth) }, combined };
        left = Expression { ExpressionKind::Unary, "!",
            { std::move(different) }, combined };
    }
    // Logical implication `a -> b` (IEEE 1800-2017 11.4.7) is !a || b; it
    // binds loosest and associates to the right.
    if (minimum_precedence == 0 && language_ == Language::SystemVerilog2017
        && constraint_parse_depth_ == 0 && at(TokenKind::ThinArrow)
        && !at(TokenKind::LeftBrace, 1)) {
        advance();
        Expression right = parse_expression();
        const auto combined = cover(left.span, right.span);
        Expression left_false { ExpressionKind::Unary, "!",
            { std::move(left) }, combined };
        left = Expression { ExpressionKind::Binary, "||",
            { std::move(left_false), std::move(right) }, combined };
    }
    return left;
}

Expression VerilogParser::parse_unary()
{
    if (match(TokenKind::PlusPlus)
        || match(TokenKind::MinusMinus)) {
        const auto operation = previous();
        (void)require_standard(
            "an increment or decrement expression",
            StandardRevision::SystemVerilog2005,
            operation,
            "FSIM-SV-PARSE-347");
        if (language_ != Language::SystemVerilog2017) {
            error(
                operation,
                "FSIM-VERILOG-SEM-010",
                "increment and decrement expressions require SystemVerilog");
        }
        auto operand = parse_unary();
        const auto combined = cover(operation.span, operand.span);
        return Expression {
            ExpressionKind::Update,
            operation.kind == TokenKind::PlusPlus ? "pre++" : "pre--",
            { std::move(operand) },
            combined
        };
    }
    if (at(TokenKind::Plus) || at(TokenKind::Minus) || at(TokenKind::Bang) || at(TokenKind::Tilde) || at(TokenKind::Ampersand) || at(TokenKind::Pipe) || at(TokenKind::Caret) || at(TokenKind::TildeAmpersand) || at(TokenKind::TildePipe) || at(TokenKind::TildeCaret) || at(TokenKind::CaretTilde)) {
        const auto operation = advance();
        if (verilog_attribute_instance_start()) {
            parse_verilog_attribute_instances();
        }
        // A unary operator applies to a primary (IEEE 1800-2017 A.8.3), so
        // `~ |a` is not the reduction `~|a`.
        if (operation.kind == TokenKind::Tilde
            && (at(TokenKind::Pipe) || at(TokenKind::Ampersand)
                || at(TokenKind::Caret))) {
            error(current(), "FSIM-SV-PARSE-392",
                "'~' followed by a separate reduction operator is not an "
                "expression; use '~" + current().text + "'");
        }
        Expression operand = parse_unary();
        return Expression { ExpressionKind::Unary, operation.text,
            { std::move(operand) },
            cover(operation.span, operand.span) };
    }
    return parse_primary();
}

Expression VerilogParser::parse_primary()
{
    const auto source_order = systemverilog_source_order();
    auto result = parse_primary_impl();
    result.systemverilog_source_order = source_order;
    return result;
}

Expression VerilogParser::parse_primary_impl()
{
    if (keyword("tagged")) {
        const auto tagged = advance();
        (void)require_standard(
            "a tagged-union expression",
            StandardRevision::SystemVerilog2005,
            tagged,
            "FSIM-SV-PARSE-347");
        const auto member = expect_identifier("tagged-union member name");
        if (at(TokenKind::Semicolon)
            || at(TokenKind::Comma)
            || at(TokenKind::RightParen)
            || at(TokenKind::RightBrace)
            || at(TokenKind::EndOfFile)) {
            // `tagged Invalid` selects a void member, which has no value
            // (IEEE 1800-2017 11.9).
            return Expression {
                ExpressionKind::Call,
                "@sv-tagged:" + member.text,
                { Expression { ExpressionKind::LogicLiteral, "1'b0", { }, member.span } },
                cover(tagged.span, member.span)
            };
        }
        auto value = parse_unary();
        const auto span = cover(tagged.span, value.span);
        return Expression {
            ExpressionKind::Call,
            "@sv-tagged:" + member.text,
            { std::move(value) },
            span
        };
    }
    if (match(TokenKind::Apostrophe)) {
        const auto apostrophe = previous();
        (void)require_standard(
            "an assignment pattern",
            StandardRevision::SystemVerilog2005,
            apostrophe,
            "FSIM-SV-PARSE-347");
        if (language_ != Language::SystemVerilog2017) {
            error(
                apostrophe,
                "FSIM-SV-SEM-084",
                "assignment patterns require SystemVerilog-2017");
        }
        expect(
            TokenKind::LeftBrace,
            "'{' after assignment-pattern apostrophe",
            "FSIM-SV-PARSE-163");
        Expression pattern {
            ExpressionKind::Aggregate,
            "sv-pattern",
            { },
            apostrophe.span
        };
        const auto parse_pattern_value = [&]() {
            if (at(TokenKind::Comma)
                || at(TokenKind::RightBrace)
                || at(TokenKind::EndOfFile)) {
                error(
                    current(),
                    "FSIM-SV-PARSE-174",
                    "expected a value after an assignment-pattern association");
                return Expression {
                    ExpressionKind::Invalid,
                    current().text,
                    { },
                    current().span
                };
            }
            return parse_expression();
        };
        while (!at(TokenKind::RightBrace)
            && !at(TokenKind::EndOfFile)) {
            if (keyword("default")) {
                const auto default_token = advance();
                Expression default_choice {
                    ExpressionKind::DefaultChoice,
                    default_token.text,
                    { },
                    default_token.span
                };
                if (!match(TokenKind::Colon)) {
                    error(
                        current(),
                        "FSIM-SV-PARSE-173",
                        "expected ':' after an assignment-pattern default choice");
                }
                pattern.aggregate_choices.push_back("default");
                pattern.aggregate_choice_expressions.push_back(
                    { std::move(default_choice) });
                pattern.operands.push_back(parse_pattern_value());
            } else {
                const auto references_before
                    = implicit_net_references_.size();
                auto first = parse_expression();
                // A replication pattern `'{n{a, b}}` (IEEE 1800-2017
                // 10.10.1) repeats its items n times.
                std::uint64_t count { };
                const bool literal_count
                    = first.kind == ExpressionKind::IntegerLiteral
                    && std::from_chars(first.text.data(),
                           first.text.data() + first.text.size(), count)
                               .ec
                        == std::errc { }
                    && count > 0U && count <= 65536U;
                // A constant count other than a literal (`'{N{x}}`) fills
                // every element with one item, as its N copies do.
                const bool constant_count = first.kind
                        == ExpressionKind::Identifier
                    || first.kind == ExpressionKind::Binary
                    || first.kind == ExpressionKind::Call;
                if (pattern.operands.empty() && at(TokenKind::LeftBrace)
                    && (literal_count || constant_count)) {
                    advance();
                    std::vector<Expression> items;
                    do {
                        items.push_back(parse_expression());
                    } while (match(TokenKind::Comma));
                    expect(
                        TokenKind::RightBrace,
                        "'}' after replicated assignment-pattern items",
                        "FSIM-SV-PARSE-164");
                    if (!literal_count) {
                        if (items.size() != 1U) {
                            error(
                                previous(),
                                "FSIM-SV-PARSE-164",
                                "a replication of several assignment-pattern "
                                "items requires a literal count");
                            break;
                        }
                        pattern.aggregate_choices.push_back("default");
                        pattern.aggregate_choice_expressions.push_back(
                            { Expression { ExpressionKind::DefaultChoice,
                                "default", { }, first.span } });
                        pattern.operands.push_back(std::move(items.front()));
                        break;
                    }
                    for (std::uint64_t copy = 0; copy < count; ++copy) {
                        for (const auto& item : items) {
                            pattern.aggregate_choices.emplace_back();
                            pattern.aggregate_choice_expressions.emplace_back();
                            pattern.operands.push_back(item);
                        }
                    }
                    break;
                }
                if (match(TokenKind::Colon)) {
                    // A member key (`'{a: 1}`) names a member or type, not a
                    // net.
                    if (first.kind == ExpressionKind::Identifier) {
                        for (auto index = implicit_net_references_.size();
                            index > references_before; --index) {
                            if (implicit_net_references_[index - 1U].name
                                == first.text) {
                                implicit_net_references_.erase(
                                    implicit_net_references_.begin()
                                    + static_cast<std::ptrdiff_t>(index - 1U));
                            }
                        }
                    }
                    pattern.aggregate_choices.push_back("@key");
                    pattern.aggregate_choice_expressions.push_back(
                        { std::move(first) });
                    pattern.operands.push_back(parse_pattern_value());
                } else {
                    pattern.aggregate_choices.emplace_back();
                    pattern.aggregate_choice_expressions.emplace_back();
                    pattern.operands.push_back(std::move(first));
                }
            }
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        expect(
            TokenKind::RightBrace,
            "'}' after assignment pattern",
            "FSIM-SV-PARSE-164");
        pattern.span = span_from(apostrophe, previous());
        return pattern;
    }
    if (at(TokenKind::Number)) {
        auto token = advance();
        if (token.text.find('\'') == std::string::npos
            && at(TokenKind::Number)
            && current().text.starts_with("'")) {
            const auto based = advance();
            token.text += based.text;
            token.span = cover(token.span, based.span);
        }
        const bool joined_sized_cast = token.text.ends_with("'")
            && at(TokenKind::LeftParen);
        if (joined_sized_cast
            || (token.text.find('\'') == std::string::npos
                && match(TokenKind::Apostrophe))) {
            if (joined_sized_cast) {
                token.text.pop_back();
            }
            (void)require_standard(
                "a sized casting expression",
                StandardRevision::SystemVerilog2005,
                token,
                "FSIM-SV-PARSE-347");
            expect(
                TokenKind::LeftParen,
                "'(' after SystemVerilog casting size",
                "FSIM-SV-PARSE-219");
            auto operand = parse_expression();
            expect(
                TokenKind::RightParen,
                "')' after SystemVerilog cast expression",
                "FSIM-SV-PARSE-220");
            auto cast = Expression {
                ExpressionKind::Call,
                "@sv-cast:" + token.text,
                { std::move(operand) },
                span_from(token, previous())
            };
            const auto width = detail::decimal_i64(token.text);
            if (!width || *width <= 0) {
                error(
                    token,
                    "FSIM-ELAB-SVCAST-003",
                    "a SystemVerilog casting size must be a positive "
                    "decimal constant");
            } else {
                cast.call_result_width =
                    static_cast<std::uint64_t>(*width);
                cast.call_result_domain = ValueDomain::Logic4;
            }
            return parse_postfix(std::move(cast));
        }
        const bool decimal_form = token.text.find('.') != std::string::npos
            || token.text.find_first_of("eE") != std::string::npos;
        std::optional<Token> time_unit;
        if (at(TokenKind::Identifier)
            && (!current().expansion_stack.empty()
                || token.span.end.offset == current().span.begin.offset)
            && time_unit_femtoseconds(current().text)) {
            time_unit = advance();
        } else if (decimal_form && at(TokenKind::Identifier)
            && token.span.end.offset == current().span.begin.offset) {
            const auto invalid_unit = advance();
            error(
                invalid_unit,
                "FSIM-SV-PARSE-282",
                "unrecognized SystemVerilog time-literal unit '"
                    + invalid_unit.text + "'");
        }
        const auto kind = token.text.find('\'') == std::string::npos
            ? ExpressionKind::IntegerLiteral
            : ExpressionKind::LogicLiteral;
        // The size of a sized literal is a nonzero decimal (IEEE 1800-2017
        // 5.7.1).
        if (const auto quote = token.text.find('\''); quote != std::string::npos
            && quote != 0U) {
            std::string size;
            for (const auto character : token.text.substr(0U, quote)) {
                if (character != '_' && character != ' ' && character != '\t') {
                    size.push_back(character);
                }
            }
            if (const auto value = detail::decimal_i64(size); value && *value == 0) {
                error(token, "FSIM-SV-SEM-409",
                    "the size of a sized literal must be nonzero");
            }
        }
        auto expression = Expression {
            kind,
            token.text + (time_unit ? time_unit->text : std::string { }),
            { },
            time_unit ? cover(token.span, time_unit->span) : token.span
        };
        if (kind == ExpressionKind::IntegerLiteral
            && (decimal_form || time_unit)) {
            expression.systemverilog_decimal_literal = parse_systemverilog_decimal_literal(token, time_unit);
            if (expression.systemverilog_decimal_literal) {
                expression.systemverilog_scalar_kind = time_unit
                    ? SystemVerilogScalarKind::Realtime
                    : SystemVerilogScalarKind::Real;
            }
        }
        return expression;
    }
    if (at(TokenKind::StringLiteral)) {
        const auto token = advance();
        auto expression = Expression {
            ExpressionKind::StringLiteral, token.text, { }, token.span
        };
        expression.decoded_string = decoded_string_literal_text(token);
        expression.generated_text = token.generated_text;
        return parse_postfix(std::move(expression));
    }
    if (at(TokenKind::Identifier)
        || (keyword("void") && at(TokenKind::Apostrophe, 1))) {
        const auto name = advance();
        std::string canonical = name.text;
        // `C#(args)` names a class specialization, also inside a scope
        // chain (`P::C#(args)::member`, IEEE 1800-2017 8.25.1).
        // The parsed actuals of the last `#(...)` in the chain.
        std::shared_ptr<const Type> selection;
        const auto append_specialization = [&] {
            if (!match(TokenKind::Hash)) {
                return;
            }
            Instance actual_owner;
            parse_parameter_overrides(actual_owner, previous());
            auto selected = std::make_shared<Type>();
            selected->named_type = canonical;
            canonical += "#(";
            for (std::size_t index = 0;
                index < actual_owner.parameter_overrides.size(); ++index) {
                if (index != 0)
                    canonical += ",";
                auto& actual = actual_owner.parameter_overrides[index];
                if (actual.name) {
                    canonical += "." + *actual.name + "(";
                }
                if (actual.type_value) {
                    canonical += class_actual_type_spelling(
                        *actual.type_value);
                } else {
                    canonical += actual.value.text;
                }
                if (actual.name)
                    canonical += ")";
                SystemVerilogClassTypeActual retained;
                retained.name = std::move(actual.name);
                retained.value = std::move(actual.value);
                if (actual.type_value) {
                    retained.type_actual = std::make_shared<Type>(
                        std::move(*actual.type_value));
                }
                retained.span = std::move(actual.span);
                selected->systemverilog_class_parameter_actuals.push_back(
                    std::move(retained));
            }
            canonical += ")";
            selected->spelling = canonical;
            selection = std::move(selected);
        };
        append_specialization();
        while (match(TokenKind::Scope)) {
            canonical += "::";
            const bool standard_member = canonical == "std::"
                && at(TokenKind::Identifier)
                && find_systemverilog_standard_package_declaration(
                       standard_revision_, current().text) != nullptr;
            // `C::new` names a typed constructor (IEEE 1800-2017 8.8).
            if (keyword("new")) {
                const auto constructor = advance();
                Expression typed {
                    ExpressionKind::Call,
                    "@sv-new-typed:"
                        + canonical.substr(0, canonical.size() - 2U),
                    { },
                    cover(name.span, constructor.span)
                };
                if (match(TokenKind::LeftParen)) {
                    bool named { };
                    while (!at_end() && !at(TokenKind::RightParen)) {
                        // `.name(value)` binds a named argument (13.5.4).
                        if (match(TokenKind::Dot)) {
                            const auto formal = expect_identifier(
                                "named constructor argument");
                            expect(TokenKind::LeftParen,
                                "'(' after named constructor argument",
                                "FSIM-SV-PARSE-199");
                            typed.call_argument_names.resize(
                                typed.operands.size());
                            typed.call_argument_names.push_back(formal.text);
                            typed.operands.push_back(
                                at(TokenKind::RightParen) ? Expression { }
                                                          : parse_expression());
                            expect(TokenKind::RightParen,
                                "')' after named constructor argument",
                                "FSIM-SV-PARSE-391");
                            named = true;
                        } else {
                            typed.operands.push_back(parse_expression());
                        }
                        if (!match(TokenKind::Comma)) {
                            break;
                        }
                    }
                    if (named) {
                        typed.call_argument_names.resize(
                            typed.operands.size());
                    }
                    expect(TokenKind::RightParen,
                        "')' after typed constructor arguments",
                        "FSIM-SV-PARSE-391");
                    typed.span = cover(typed.span, previous().span);
                }
                typed.systemverilog_class_selection = selection;
                return typed;
            }
            canonical += (standard_member
                ? advance()
                : expect_identifier("package-scoped name")).text;
            if (at(TokenKind::Hash) && at(TokenKind::LeftParen, 1)) {
                append_specialization();
            }
        }
        // `$unit::name` names a declaration of this file's compilation-unit
        // package (IEEE 1800-2017 3.12.1, 26.3).
        if (canonical.starts_with("$unit::") && compilation_unit_package_) {
            (void)require_standard(
                "predefined scope '$unit'",
                StandardRevision::SystemVerilog2005,
                name,
                "FSIM-SV-PARSE-349");
            canonical = compilation_unit_package_->name
                + canonical.substr(std::string_view { "$unit" }.size());
        }
        if (canonical == "$unit" || canonical.starts_with("$unit::")
            || canonical == "$root" || canonical.starts_with("$root::")
            || canonical.starts_with("std::")
            || canonical.starts_with("process::")
            || canonical.starts_with("local::")) {
            (void)require_standard(
                "predefined scope '" + canonical + "'",
                StandardRevision::SystemVerilog2005,
                name,
                "FSIM-SV-PARSE-349");
        }
        if (match(TokenKind::Apostrophe)) {
            (void)require_standard(
                "a type cast",
                StandardRevision::SystemVerilog2005,
                name,
                "FSIM-SV-PARSE-347");
            if (language_ != Language::SystemVerilog2017) {
                error(
                    name,
                    "FSIM-SV-SEM-125",
                    "type casts require SystemVerilog-2017");
            }
            if (at(TokenKind::LeftBrace)) {
                // A typed assignment pattern `T'{...}` (IEEE 1800-2017
                // 10.9): the pattern takes the named type.
                --index_;
                auto pattern = parse_primary();
                pattern.nominal_type = canonical;
                return parse_postfix(std::move(pattern));
            }
            expect(
                TokenKind::LeftParen,
                "'(' after SystemVerilog cast type",
                "FSIM-SV-PARSE-219");
            auto operand = parse_expression();
            expect(
                TokenKind::RightParen,
                "')' after SystemVerilog cast expression",
                "FSIM-SV-PARSE-220");
            return parse_postfix(Expression {
                ExpressionKind::Call,
                "@sv-cast:" + canonical,
                { std::move(operand) },
                span_from(name, previous()) });
        }
        Expression expression {
            ExpressionKind::Identifier,
            canonical,
            { },
            cover(name.span, previous().span)
        };
        expression.systemverilog_class_selection = std::move(selection);
        if (const auto required = verilog_system_service_standard(canonical)) {
            note_system_service_standard(canonical, *required, name);
        }
        // `$timeunit` and `$timeprecision` of the current design element,
        // as a power of ten (IEEE 1800-2017 20.4.2). An optional argument
        // naming the element itself is accepted.
        if ((canonical == "$timeunit" || canonical == "$timeprecision")
            && (!at(TokenKind::LeftParen)
                || (at(TokenKind::RightParen, 1)
                    || (at(TokenKind::Identifier, 1)
                        && at(TokenKind::RightParen, 2))))) {
            const auto exponent_of = [](std::string_view spelling)
                -> std::optional<int> {
                int exponent { };
                while (!spelling.empty() && spelling.front() == '1') {
                    spelling.remove_prefix(1U);
                    while (!spelling.empty() && spelling.front() == '0') {
                        ++exponent;
                        spelling.remove_prefix(1U);
                    }
                }
                static constexpr std::array<std::pair<std::string_view, int>, 6>
                    units { { { "s", 0 }, { "ms", -3 }, { "us", -6 },
                        { "ns", -9 }, { "ps", -12 }, { "fs", -15 } } };
                for (const auto& [unit, scale] : units) {
                    if (spelling == unit) {
                        return exponent + scale;
                    }
                }
                return std::nullopt;
            };
            const auto magnitude_digits = [](std::uint64_t magnitude) {
                std::string digits { "1" };
                while (magnitude >= 10U) {
                    digits += '0';
                    magnitude /= 10U;
                }
                return digits;
            };
            const auto exponent = canonical == "$timeunit"
                ? (module_time_unit_.empty()
                        ? std::nullopt
                        : exponent_of(magnitude_digits(
                                          module_time_unit_magnitude_)
                              + module_time_unit_))
                : exponent_of(module_time_precision_);
            if (exponent) {
                if (match(TokenKind::LeftParen)) {
                    (void)match(TokenKind::Identifier);
                    expect(TokenKind::RightParen,
                        "')' after time scope", "FSIM-SV-PARSE-033");
                }
                Expression value { ExpressionKind::IntegerLiteral,
                    std::to_string(*exponent < 0 ? -*exponent : *exponent),
                    {}, name.span };
                if (*exponent >= 0) {
                    return value;
                }
                return Expression { ExpressionKind::Unary, "-",
                    { std::move(value) }, name.span };
            }
        }
        if (canonical == "new" && at(TokenKind::LeftBracket)) {
            (void)require_standard(
                "the predefined new operator",
                StandardRevision::SystemVerilog2005,
                name,
                "FSIM-SV-PARSE-349");
        }
        // An attribute instance may precede a call's arguments (IEEE
        // 1800-2017 5.12): `f (* attribute *) (a, b)`.
        if (verilog_attribute_instance_start()) {
            parse_verilog_attribute_instances();
        }
        if (match(TokenKind::LeftParen)) {
            std::vector<Expression> arguments;
            std::vector<std::string> argument_names;
            std::optional<std::uint64_t> data_type_bits;
            std::shared_ptr<const Type> type_operand;
            const auto sampled_value_call = canonical == "$sampled"
                || canonical == "$rose" || canonical == "$fell"
                || canonical == "$stable" || canonical == "$changed"
                || canonical == "$past";
            if (!at(TokenKind::RightParen)) {
                do {
                    if (at(TokenKind::Comma)
                        || at(TokenKind::RightParen)) {
                        argument_names.emplace_back();
                        arguments.emplace_back();
                    } else if (match(TokenKind::Dot)) {
                        const auto formal = expect_identifier("named function argument");
                        expect(
                            TokenKind::LeftParen,
                            "'(' after named function argument",
                            "FSIM-SV-PARSE-199");
                        argument_names.push_back(formal.text);
                        if (at(TokenKind::RightParen)) {
                            arguments.emplace_back();
                        } else {
                            arguments.push_back(parse_expression());
                        }
                        expect(
                            TokenKind::RightParen,
                            "')' after named function argument",
                            "FSIM-SV-PARSE-200");
                    } else if (sampled_value_call && match(TokenKind::At)) {
                        const auto event_start = previous();
                        const auto sensitivities = parse_sensitivity();
                        Expression event {
                            ExpressionKind::Call,
                            "@sv-clocking-event",
                            { },
                            event_start.span
                        };
                        if (sensitivities.size() == 1U
                            && !sensitivities.front().signal.empty()) {
                            event.operands.emplace_back(
                                ExpressionKind::Identifier,
                                sensitivities.front().signal,
                                std::vector<Expression> { },
                                sensitivities.front().span);
                            event.call_result_width = static_cast<std::uint64_t>(
                                sensitivities.front().edge);
                            event.span = cover(
                                event_start.span, sensitivities.front().span);
                        } else {
                            error(
                                event_start,
                                "FSIM-SV-SEM-075",
                                "a sampled-value clocking event currently requires one direct signal");
                        }
                        argument_names.emplace_back();
                        arguments.push_back(std::move(event));
                    } else if (canonical == "type" && arguments.empty()
                        && language_ == Language::SystemVerilog2017
                        && type_operator_data_type_start()) {
                        // `type(data_type)` (IEEE 1800-2017 6.23).
                        type_operand = std::make_shared<const Type>(
                            parse_parameter_type());
                    } else if (canonical == "$bits" && arguments.empty()
                        && (keyword("logic") || keyword("reg")
                            || keyword("bit") || keyword("byte")
                            || keyword("shortint") || keyword("int")
                            || keyword("longint") || keyword("integer")
                            || keyword("time"))) {
                        // $bits of a built-in integral data type is that
                        // type's width (IEEE 1800-2017 20.6.2).
                        const auto type_start = current();
                        const auto data_type = parse_parameter_type();
                        data_type_bits = data_type.width().value_or(1U);
                        argument_names.emplace_back();
                        arguments.push_back(Expression {
                            ExpressionKind::IntegerLiteral,
                            std::to_string(*data_type_bits),
                            { },
                            cover(type_start.span, previous().span) });
                    } else if (canonical == "$bits" && arguments.empty()
                        && (keyword("struct") || keyword("union"))) {
                        // The keyword makes this a data_type argument rather
                        // than an expression (IEEE 1800-2017 20.6.2). Reuse
                        // the ordinary aggregate type parser so its member
                        // grammar and packed width calculation stay aligned.
                        const auto type_start = current();
                        const auto data_type = parse_parameter_type();
                        // An aggregate with no concrete packed range must not
                        // inherit Type::width()'s scalar fallback width of one.
                        const auto width = data_type.packed_range
                            ? data_type.width()
                            : std::optional<std::uint64_t> { };
                        if (!width) {
                            error(type_start,
                                "FSIM-SV-UNSUPPORTED-050",
                                "$bits of this aggregate data type requires a statically known width");
                        }
                        data_type_bits = width.value_or(0U);
                        argument_names.emplace_back();
                        arguments.push_back(Expression {
                            ExpressionKind::IntegerLiteral,
                            std::to_string(*data_type_bits),
                            { },
                            cover(type_start.span, previous().span) });
                    } else {
                        argument_names.emplace_back();
                        arguments.push_back(parse_expression());
                    }
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RightParen, "')' after arguments",
                "FSIM-SV-PARSE-028");
            if (canonical == "$sampled" && arguments.size() == 2U
                && arguments.back().kind != ExpressionKind::Invalid) {
                diagnose_systemverilog_2023_annex(
                    SystemVerilogAnnexConstruct::sampled_clock_argument,
                    name);
            }
            auto class_selection
                = std::move(expression.systemverilog_class_selection);
            expression = Expression { ExpressionKind::Call, canonical,
                std::move(arguments),
                cover(name.span, previous().span) };
            expression.call_argument_names = std::move(argument_names);
            expression.systemverilog_class_selection
                = std::move(class_selection);
            if (canonical == "new") {
                if (require_standard(
                        "the predefined new operator",
                        StandardRevision::SystemVerilog2005,
                        name,
                        "FSIM-SV-PARSE-349")) {
                    expression.text = "@sv-new";
                }
            } else if (canonical == "$cast") {
                expression.text = "@sv-dollar-cast";
            } else if (canonical == "type"
                && language_ == Language::SystemVerilog2017) {
                expression.text = "@sv-type";
                expression.systemverilog_type_operand = type_operand;
                if (expression.operands.size() != (type_operand ? 0U : 1U)) {
                    error(
                        name,
                        "FSIM-SV-SEM-241",
                        "the type operator requires exactly one expression");
                }
            }
            const auto require_file_call =
                [&](const std::size_t arity,
                    const std::string_view description) {
                    if (expression.operands.size() != arity) {
                        error(
                            name,
                            "FSIM-SV-SEM-075",
                            canonical + " requires " + std::string { description });
                    }
                };
            if (canonical == "$fopen") {
                if (expression.operands.size() < 1U
                    || expression.operands.size() > 2U) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        "$fopen requires a filename and optional text mode");
                } else if (expression.operands.size() == 2U) {
                    (void)require_standard(
                        "the two-argument $fopen signature",
                        StandardRevision::Verilog2001, name,
                        "FSIM-SV-PARSE-350");
                }
            } else if (canonical == "$fgets") {
                require_file_call(2, "a string target and file handle");
            } else if (canonical == "$fgetc") {
                require_file_call(1, "one file handle");
            } else if (canonical == "$ungetc") {
                require_file_call(2, "a character and file handle");
            } else if (canonical == "$feof") {
                require_file_call(1, "one file handle");
            } else if (canonical == "$ferror") {
                require_file_call(2, "a file handle and string target");
            } else if (canonical == "$fscanf" || canonical == "$sscanf") {
                if (expression.operands.size() < 2U) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        canonical + " requires a source and literal format");
                }
            } else if (canonical == "$fread") {
                if (expression.operands.size() < 2U
                    || expression.operands.size() > 4U) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        "$fread requires a target, handle, and optional start/count");
                }
            } else if (canonical == "$fseek") {
                require_file_call(3, "a handle, offset, and origin");
            } else if (canonical == "$ftell") {
                require_file_call(1, "one file handle");
            } else if (canonical == "$rewind") {
                require_file_call(1, "one file handle");
            } else if (canonical == "$test$plusargs") {
                require_file_call(1, "one string expression");
            } else if (canonical == "$value$plusargs") {
                require_file_call(
                    2, "a format string and writable target");
            } else if (canonical == "$random" || canonical == "$urandom") {
                if (expression.operands.size() > 1U) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        canonical + " accepts at most one inout seed");
                }
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = canonical == "$random";
            } else if (canonical == "$urandom_range") {
                if (expression.operands.empty()
                    || expression.operands.size() > 2U) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        "$urandom_range requires a maximum and optional minimum");
                }
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = false;
            } else if (canonical == "$sformatf") {
                if (expression.operands.empty()) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        "$sformatf requires a format expression");
                }
                expression.call_result_domain = ValueDomain::String;
            } else if (canonical == "$onehot"
                || canonical == "$onehot0"
                || canonical == "$isunknown") {
                require_file_call(1, "one packed expression");
                expression.call_result_width = 1;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = false;
            } else if (canonical == "$countbits") {
                if (expression.operands.size() < 2U) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        "$countbits requires an expression and at least one control bit");
                }
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = true;
            } else if (canonical == "$signed" || canonical == "$unsigned") {
                require_file_call(1, "one packed expression");
                expression.call_result_signed = canonical == "$signed";
                // IEEE 1800-2017 11.7: the argument is integral.
                if (expression.operands.size() == 1U
                    && expression.operands.front().kind == ExpressionKind::IntegerLiteral
                    && expression.operands.front().systemverilog_scalar_kind
                        == SystemVerilogScalarKind::Real) {
                    error(name, "FSIM-SV-SEM-415",
                        canonical + " requires an integral argument, not a real value");
                }
            } else if (contains_word(
                           { "$bits", "$clog2", "$countones", "$dimensions",
                               "$unpacked_dimensions" },
                           canonical)) {
                require_file_call(1, "one expression or data type");
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = true;
                if (canonical == "$bits" && data_type_bits) {
                    expression = Expression { ExpressionKind::IntegerLiteral,
                        std::to_string(*data_type_bits), { },
                        expression.span };
                }
            } else if (contains_word(
                           { "$high", "$increment", "$left", "$low", "$right",
                               "$size" },
                           canonical)) {
                if (expression.operands.empty()
                    || expression.operands.size() > 2U) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        canonical
                            + " requires an array and optional dimension");
                }
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = true;
            } else if (canonical == "$system") {
                if (expression.operands.size() > 1U) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        "$system accepts zero or one command string");
                }
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = true;
            } else if (canonical == "$coverage_control") {
                if (expression.operands.size() != 4U) {
                    error(
                        name,
                        "FSIM-COV-038",
                        "$coverage_control requires a control command, coverage type, scope, and module or instance selector");
                }
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = true;
            } else if (canonical == "$coverage_get"
                || canonical == "$coverage_get_max"
                || canonical == "$coverage_merge"
                || canonical == "$coverage_save") {
                const auto file_call = canonical == "$coverage_merge"
                    || canonical == "$coverage_save";
                if (expression.operands.size() != (file_call ? 2U : 3U)) {
                    error(name, "FSIM-COV-039",
                        canonical + (file_call
                            ? " requires a coverage type and filename"
                            : " requires a coverage type, scope, and module or instance selector"));
                }
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = true;
            } else if (canonical == "$q_full") {
                require_file_call(2, "a queue ID and writable status");
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = true;
            } else if (canonical == "$__fsim_sva_tick"
                || canonical == "$__fsim_sva_outcome") {
                // The concurrent-property monitor of a generated assertion
                // process.
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = true;
            } else if (canonical == "$typename") {
                require_file_call(1, "one expression or data type");
                expression.call_result_domain = ValueDomain::String;
            } else if (canonical == "$isunbounded") {
                require_file_call(1, "one parameter or constant expression");
                expression.call_result_width = 1;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = false;
            } else if (canonical == "$sampled"
                || canonical == "$rose"
                || canonical == "$fell"
                || canonical == "$stable"
                || canonical == "$changed"
                || canonical == "$past"
                || canonical == "$past_gclk"
                || canonical == "$rose_gclk"
                || canonical == "$fell_gclk"
                || canonical == "$stable_gclk"
                || canonical == "$changed_gclk"
                || canonical == "$future_gclk"
                || canonical == "$rising_gclk"
                || canonical == "$falling_gclk"
                || canonical == "$steady_gclk"
                || canonical == "$changing_gclk") {
                const auto global = canonical.ends_with("_gclk");
                if (expression.operands.empty()
                    || expression.operands.size()
                        > (global ? 1U : canonical == "$past" ? 4U
                                                              : 2U)) {
                    error(
                        name,
                        "FSIM-SV-SEM-075",
                        canonical
                            + " requires one expression"
                            + (global
                                    ? ""
                                    : canonical == "$past"
                                    ? ", optional constant tick count, gating expression, and clocking event"
                                    : " and an optional clocking event"));
                }
                if (canonical != "$sampled" && canonical != "$past"
                    && canonical != "$past_gclk"
                    && canonical != "$future_gclk") {
                    expression.call_result_width = 1;
                    expression.call_result_domain = ValueDomain::Bit2;
                    expression.call_result_signed = false;
                }
            } else if (canonical == "$get_coverage"
                || canonical == "$get_inst_coverage") {
                require_file_call(0, "no arguments");
                expression.call_result_width = 64;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = false;
                expression.systemverilog_scalar_kind
                    = SystemVerilogScalarKind::Real;
            } else if (canonical == "$dist_uniform"
                || canonical == "$dist_normal"
                || canonical == "$dist_erlang") {
                require_file_call(3, "an inout seed and two integer arguments");
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = true;
            } else if (canonical == "$dist_exponential"
                || canonical == "$dist_poisson"
                || canonical == "$dist_chi_square"
                || canonical == "$dist_t") {
                require_file_call(2, "an inout seed and one integer argument");
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = true;
            } else if (canonical == "$simtime") {
                // The current time in simulation-precision units, an
                // extension that ivtest uses.
                require_file_call(0, "no arguments");
                expression.call_result_width = 64;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = false;
            } else if (canonical == "$time"
                || canonical == "$stime"
                || canonical == "$realtime") {
                require_file_call(0, "no arguments");
                expression.call_result_width
                    = canonical == "$stime" ? 32 : 64;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = false;
                expression.systemverilog_scalar_kind
                    = canonical == "$realtime"
                    ? SystemVerilogScalarKind::Realtime
                    : canonical == "$time"
                    ? SystemVerilogScalarKind::Time
                    : SystemVerilogScalarKind::None;
            } else if (canonical == "$pow"
                || canonical == "$atan2"
                || canonical == "$hypot") {
                require_file_call(2, "two numeric expressions");
                expression.call_result_width = 64;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.systemverilog_scalar_kind
                    = SystemVerilogScalarKind::Real;
            } else if (canonical == "$rtoi"
                || canonical == "$itor"
                || canonical == "$bitstoreal"
                || canonical == "$realtobits"
                || canonical == "$bitstoshortreal"
                || canonical == "$shortrealtobits"
                || canonical == "$ln"
                || canonical == "$log10"
                || canonical == "$exp"
                || canonical == "$sqrt"
                || canonical == "$floor"
                || canonical == "$ceil"
                || canonical == "$sin"
                || canonical == "$cos"
                || canonical == "$tan"
                || canonical == "$asin"
                || canonical == "$acos"
                || canonical == "$atan"
                || canonical == "$sinh"
                || canonical == "$cosh"
                || canonical == "$tanh"
                || canonical == "$asinh"
                || canonical == "$acosh"
                || canonical == "$atanh") {
                require_file_call(1, "one numeric expression");
                const bool result32 = canonical == "$rtoi"
                    || canonical == "$bitstoshortreal"
                    || canonical == "$shortrealtobits";
                expression.call_result_width = result32 ? 32 : 64;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = canonical == "$rtoi";
                if (canonical == "$itor"
                    || canonical == "$bitstoreal"
                    || (canonical != "$rtoi"
                        && canonical != "$realtobits"
                        && canonical != "$bitstoshortreal"
                        && canonical != "$shortrealtobits")) {
                    expression.systemverilog_scalar_kind
                        = SystemVerilogScalarKind::Real;
                } else if (canonical == "$bitstoshortreal") {
                    expression.systemverilog_scalar_kind
                        = SystemVerilogScalarKind::ShortReal;
                }
            }
            if (contains_word(
                    { "$fopen", "$fgets", "$fgetc", "$ungetc", "$feof",
                        "$ferror", "$fscanf", "$sscanf", "$fread", "$fseek",
                        "$ftell", "$rewind", "$test$plusargs",
                        "$value$plusargs" },
                    canonical)) {
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = true;
            }
            // A constant function call as a casting size
            // (`$bits(v)'(x)`, IEEE 1800-2017 6.24.1).
            if (language_ == Language::SystemVerilog2017
                && at(TokenKind::Apostrophe) && at(TokenKind::LeftParen, 1)) {
                (void)advance();
                (void)advance();
                auto operand = parse_expression();
                expect(
                    TokenKind::RightParen,
                    "')' after SystemVerilog cast expression",
                    "FSIM-SV-PARSE-220");
                const auto span = cover(expression.span, previous().span);
                if (expression.text == "@sv-type") {
                    // `type(...)'(x)` casts to the type the operator
                    // denotes (IEEE 1800-2017 6.23, 6.24.1).
                    auto cast_type = expression.systemverilog_type_operand;
                    if (!cast_type && expression.operands.size() == 1U) {
                        Type type_of;
                        type_of.spelling = "@type-of";
                        type_of.named_type_span = expression.span;
                        type_of.systemverilog_type_of
                            = std::make_shared<const Expression>(
                                std::move(expression.operands.front()));
                        cast_type = std::make_shared<const Type>(
                            std::move(type_of));
                    }
                    Expression cast { ExpressionKind::Call, "@sv-cast:@type",
                        { std::move(operand) }, span };
                    cast.systemverilog_type_operand = std::move(cast_type);
                    return parse_postfix(std::move(cast));
                }
                return parse_postfix(Expression {
                    ExpressionKind::Call,
                    "@sv-size-cast:expression",
                    { std::move(expression), std::move(operand) },
                    span });
            }
            return parse_postfix(std::move(expression));
        }
        if (canonical == "null") {
            if (!require_standard(
                    "the predefined null value",
                    StandardRevision::SystemVerilog2005,
                    name,
                    "FSIM-SV-PARSE-349")) {
                return parse_postfix(std::move(expression));
            }
            return Expression {
                ExpressionKind::Call, "@sv-null", { }, name.span
            };
        }
        if (canonical == "new" && !at(TokenKind::LeftBracket)) {
            if (!require_standard(
                    "the predefined new operator",
                    StandardRevision::SystemVerilog2005,
                    name,
                    "FSIM-SV-PARSE-349")) {
                return parse_postfix(std::move(expression));
            }
            // `new handle` makes a shallow copy (IEEE 1800-2017 8.12).
            if (language_ == Language::SystemVerilog2017
                && at(TokenKind::Identifier)
                && (keyword("this")
                    || !keyword_reserved(keyword_set_, current().text))) {
                auto source = parse_unary();
                const auto span = cover(name.span, source.span);
                return Expression {
                    ExpressionKind::Call, "@sv-shallow-copy",
                    { std::move(source) }, span
                };
            }
            return Expression {
                ExpressionKind::Call, "@sv-new", { }, name.span
            };
        }
        if (canonical == "$urandom" || canonical == "$random"
            || canonical == "$time" || canonical == "$stime"
            || canonical == "$realtime" || canonical == "$simtime") {
            expression.kind = ExpressionKind::Call;
            if (canonical == "$simtime") {
                expression.call_result_width = 64;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = false;
            } else if (canonical == "$random" || canonical == "$urandom") {
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Integer;
                expression.call_result_signed = canonical == "$random";
            } else if (canonical == "$time" || canonical == "$stime"
                || canonical == "$realtime") {
                expression.call_result_width
                    = canonical == "$stime" ? 32 : 64;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = false;
                expression.systemverilog_scalar_kind
                    = canonical == "$realtime"
                    ? SystemVerilogScalarKind::Realtime
                    : canonical == "$time"
                    ? SystemVerilogScalarKind::Time
                    : SystemVerilogScalarKind::None;
            }
            return parse_postfix(std::move(expression));
        }
        expression = parse_postfix(std::move(expression));
        const Expression* root = &expression;
        while ((root->kind == ExpressionKind::Index
                   || root->kind == ExpressionKind::Slice)
            && !root->operands.empty()) {
            root = &root->operands.front();
        }
        if (root->kind == ExpressionKind::Identifier
            && root->text == name.text) {
            note_implicit_net_reference(
                name, expression.kind == ExpressionKind::Call);
        }
        return expression;
    }
    if (match(TokenKind::LeftParen)) {
        const auto open = previous();
        const auto inner_begin = index_;
        // Inside parentheses a constraint's `->` is the logical implication
        // operator of an expression (IEEE 1800-2017 11.4.7, 18.5.6).
        const auto constraint_depth = constraint_parse_depth_;
        constraint_parse_depth_ = 0U;
        Expression expression = parse_expression();
        constraint_parse_depth_ = constraint_depth;
        const auto assignment_operator
            = is_blocking_assignment_operator(current().kind);
        if (assignment_operator && statement_hoist_depth_ != 0U
            && language_ == Language::SystemVerilog2017
            && constraint_parse_depth_ == 0U) {
            // An assignment within an expression (IEEE 1800-2017 11.3.6)
            // runs before the enclosing statement; its value is the
            // target's new value.
            index_ = inner_begin;
            std::vector<Token> tokens;
            std::size_t depth = 1U;
            while (!at_end()) {
                if (at(TokenKind::LeftParen)) {
                    ++depth;
                } else if (at(TokenKind::RightParen) && --depth == 0U) {
                    break;
                }
                tokens.push_back(advance());
            }
            Token terminator;
            terminator.kind = TokenKind::Semicolon;
            terminator.text = ";";
            terminator.span = current().span;
            tokens.push_back(std::move(terminator));
            expect(TokenKind::RightParen, "')' after expression",
                "FSIM-SV-PARSE-029");
            const auto close = previous();
            auto assignment = parse_generated_statement(
                std::move(tokens), span_from(open, close));
            if (assignment) {
                const Statement* last = &*assignment;
                while (last->kind == StatementKind::Block
                    && !last->statements.empty()) {
                    last = &last->statements.back();
                }
                expression = last->target;
                hoisted_statements_.push_back(std::move(*assignment));
            }
            expression.span = span_from(open, close);
            return parse_postfix(std::move(expression));
        }
        // A min:typ:max expression selects its typical value
        // (IEEE 1800-2017 11.11).
        if (match(TokenKind::Colon)) {
            expression = parse_expression();
            expect(TokenKind::Colon, "':' in min:typ:max expression",
                "FSIM-SV-PARSE-029");
            (void)parse_expression();
        }
        expect(TokenKind::RightParen, "')' after expression",
            "FSIM-SV-PARSE-029");
        expression.span = span_from(open, previous());
        if (match(TokenKind::Apostrophe)) {
            const auto apostrophe = previous();
            (void)require_standard(
                "a sized casting expression",
                StandardRevision::SystemVerilog2005,
                apostrophe,
                "FSIM-SV-PARSE-347");
            expect(
                TokenKind::LeftParen,
                "'(' after SystemVerilog casting size",
                "FSIM-SV-PARSE-219");
            auto operand = parse_expression();
            expect(
                TokenKind::RightParen,
                "')' after SystemVerilog cast expression",
                "FSIM-SV-PARSE-220");
            return parse_postfix(Expression {
                ExpressionKind::Call,
                "@sv-size-cast:expression",
                { std::move(expression), std::move(operand) },
                span_from(open, previous()) });
        }
        return parse_postfix(std::move(expression));
    }
    if (match(TokenKind::LeftBrace)) {
        const auto open = previous();
        if (at(TokenKind::ShiftLeft)
            || at(TokenKind::ShiftRight)) {
            const auto direction = advance();
            (void)require_standard(
                "a streaming concatenation",
                StandardRevision::SystemVerilog2005,
                direction,
                "FSIM-SV-PARSE-347");
            if (language_ != Language::SystemVerilog2017) {
                error(
                    direction,
                    "FSIM-SV-SEM-100",
                    "streaming concatenation requires SystemVerilog-2017");
            }
            Expression slice_size {
                ExpressionKind::IntegerLiteral,
                "1",
                { },
                direction.span
            };
            if (!at(TokenKind::LeftBrace)
                && (keyword("bit") || keyword("logic") || keyword("reg"))) {
                // A one-bit type, or a vector of one with literal packed
                // dimensions (`bit [1:0]`), as the slice size: its width
                // (IEEE 1800-2017 11.4.14).
                const auto type = advance();
                std::uint64_t width { 1U };
                bool literal_dimensions { true };
                while (at(TokenKind::LeftBracket)
                    && at(TokenKind::Number, 1U) && at(TokenKind::Colon, 2U)
                    && at(TokenKind::Number, 3U)
                    && at(TokenKind::RightBracket, 4U)) {
                    advance();
                    const auto left = advance();
                    advance();
                    const auto right = advance();
                    advance();
                    std::uint64_t msb { };
                    std::uint64_t lsb { };
                    const auto parsed_left = std::from_chars(left.text.data(),
                        left.text.data() + left.text.size(), msb);
                    const auto parsed_right = std::from_chars(
                        right.text.data(),
                        right.text.data() + right.text.size(), lsb);
                    if (parsed_left.ec != std::errc { }
                        || parsed_left.ptr != left.text.data() + left.text.size()
                        || parsed_right.ec != std::errc { }
                        || parsed_right.ptr
                            != right.text.data() + right.text.size()) {
                        literal_dimensions = false;
                        break;
                    }
                    width *= (msb > lsb ? msb - lsb : lsb - msb) + 1U;
                }
                if (!literal_dimensions) {
                    error(type, "FSIM-SV-PARSE-191",
                        "a streaming slice type needs decimal literal packed "
                        "dimensions");
                }
                slice_size = Expression {
                    ExpressionKind::IntegerLiteral, std::to_string(width),
                    { }, type.span };
            } else if (!at(TokenKind::LeftBrace)) {
                if (keyword("byte") || keyword("shortint")
                    || keyword("int") || keyword("longint")
                    || keyword("integer") || keyword("time")) {
                    const auto type = advance();
                    // A simple integral type supplies its bit width, not
                    // a value to resolve as an identifier (IEEE 1800-2017
                    // 11.4.14.1).
                    const auto width = type.text == "byte" ? "8"
                        : type.text == "shortint" ? "16"
                        : type.text == "longint" || type.text == "time" ? "64"
                        : "32";
                    slice_size = Expression {
                        ExpressionKind::IntegerLiteral,
                        width,
                        { },
                        type.span
                    };
                } else if (at(TokenKind::Identifier)
                    && at(TokenKind::LeftBrace, 1U)
                    && packed_typedef_types_.contains(current().text)) {
                    // A user type as the slice size gives its width
                    // (IEEE 1800-2017 11.4.14).
                    const auto type = advance();
                    slice_size = Expression {
                        ExpressionKind::Call, "$bits",
                        { Expression { ExpressionKind::Identifier,
                            type.text, { }, type.span } },
                        type.span };
                } else {
                    slice_size = parse_expression();
                }
            }
            expect(
                TokenKind::LeftBrace,
                "'{' before streaming operands",
                "FSIM-SV-PARSE-191");
            std::vector<Expression> operands;
            operands.push_back(std::move(slice_size));
            if (at(TokenKind::RightBrace)) {
                error(
                    current(),
                    "FSIM-SV-PARSE-192",
                    "streaming concatenation requires at least one operand");
            } else {
                do {
                    auto operand = parse_expression();
                    if (keyword("with") && at(TokenKind::LeftBracket, 1U)) {
                        // `array with [range]` streams only the items of
                        // the range (IEEE 1800-2017 11.4.14.4): operands
                        // are the array and the range's one or two bounds.
                        const auto with = advance();
                        advance();
                        std::vector<Expression> parts;
                        parts.push_back(std::move(operand));
                        parts.push_back(parse_expression());
                        std::string text = "@stream-with";
                        if (match(TokenKind::Colon)) {
                            text = "@stream-with-range";
                            parts.push_back(parse_expression());
                        } else if (match(TokenKind::PlusColon)) {
                            text = "@stream-with-plus";
                            parts.push_back(parse_expression());
                        } else if (match(TokenKind::MinusColon)) {
                            text = "@stream-with-minus";
                            parts.push_back(parse_expression());
                        }
                        expect(TokenKind::RightBracket,
                            "']' after a streaming with range",
                            "FSIM-SV-PARSE-193");
                        operand = Expression { ExpressionKind::Call,
                            std::move(text), std::move(parts),
                            span_from(with, previous()) };
                    }
                    operands.push_back(std::move(operand));
                } while (match(TokenKind::Comma));
            }
            expect(
                TokenKind::RightBrace,
                "'}' after streaming operands",
                "FSIM-SV-PARSE-193");
            expect(
                TokenKind::RightBrace,
                "'}' after streaming concatenation",
                "FSIM-SV-PARSE-194");
            return Expression {
                ExpressionKind::Call,
                direction.kind == TokenKind::ShiftLeft
                    ? "@stream-left"
                    : "@stream-right",
                std::move(operands),
                span_from(open, previous())
            };
        }
        std::vector<Expression> elements;
        if (!at(TokenKind::RightBrace)) {
            Expression first = parse_expression();
            if (match(TokenKind::LeftBrace)) {
                elements.push_back(std::move(first));
                if (at(TokenKind::RightBrace)) {
                    error(
                        current(),
                        "FSIM-SV-PARSE-090",
                        "replication concatenations require at least one operand");
                } else {
                    do {
                        elements.push_back(parse_expression());
                    } while (match(TokenKind::Comma));
                }
                expect(TokenKind::RightBrace,
                    "'}' after replication operands",
                    "FSIM-SV-PARSE-030");
                expect(TokenKind::RightBrace,
                    "'}' after replication concatenation",
                    "FSIM-SV-PARSE-030");
                return Expression {
                    ExpressionKind::Replication,
                    "replicate",
                    std::move(elements),
                    span_from(open, previous())
                };
            }
            elements.push_back(std::move(first));
            while (match(TokenKind::Comma)) {
                elements.push_back(parse_expression());
            }
        }
        expect(TokenKind::RightBrace, "'}' after concatenation",
            "FSIM-SV-PARSE-030");
        // IEEE 1364-2005 5.1.14: an unsized constant number is not allowed
        // in a concatenation. (A SystemVerilog unpacked array concatenation
        // takes unsized items.)
        if (keyword_set_rank(keyword_set_for_standard_revision(standard_revision_))
            < keyword_set_rank(keyword_set_for_standard_revision(
                StandardRevision::SystemVerilog2005))) {
            for (const auto& element : elements) {
                if ((element.kind == ExpressionKind::IntegerLiteral
                        || element.kind == ExpressionKind::LogicLiteral)
                    && (element.text.find('\'') == std::string::npos
                        || element.text.starts_with('\''))) {
                    error(open, "FSIM-SV-SEM-413",
                        "an unsized constant number cannot be an operand of a "
                        "Verilog concatenation");
                    break;
                }
            }
        }
        Expression concatenation { ExpressionKind::Concatenation, "concat",
            std::move(elements), span_from(open, previous()) };
        // A bit- or part-select of a concatenation (IEEE 1800-2017
        // 11.4.12).
        if (language_ == Language::SystemVerilog2017
            && at(TokenKind::LeftBracket)) {
            return parse_postfix(std::move(concatenation));
        }
        return concatenation;
    }
    const auto invalid = advance();
    error(invalid, "FSIM-SV-PARSE-031", "expected expression");
    return Expression { ExpressionKind::Invalid, invalid.text, { },
        invalid.span };
}

} // namespace fsim::frontend
