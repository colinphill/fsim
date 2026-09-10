// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"
#include "fsim/frontend/systemverilog_standard_package.hpp"

#include <algorithm>
#include <limits>

namespace fsim::frontend {

[[nodiscard]] std::optional<StandardRevision>
verilog_system_service_standard(std::string_view name);

DelayAlternative VerilogParser::parse_verilog_delay_alternative()
{
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
    const bool explicit_time_literal = at(TokenKind::Number)
        && at(TokenKind::Identifier, 1)
        && time_unit_femtoseconds(current(1).text).has_value();
    auto expression = explicit_time_literal
        ? Expression { }
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
        } else if (alternative.divisor != 1) {
            error(
                magnitude,
                "FSIM-SV-SEM-050",
                "a fractional delay requires an explicit time unit or an "
                "active `timescale/timeunit");
        }
    } else if (
        expression.kind == ExpressionKind::IntegerLiteral
        || expression.kind == ExpressionKind::LogicLiteral) {
        const bool nondecimal = expression.text.find('\'') != std::string::npos;
        error(
            start,
            nondecimal ? "FSIM-SV-SEM-002" : "FSIM-SV-SEM-049",
            nondecimal
                ? "delay magnitude must be a decimal literal"
                : "delay magnitude must be a representable nonnegative "
                  "decimal literal");
    } else {
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
    auto first = parse_verilog_delay_alternative();
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
    const auto name = keyword("this") || keyword("super")
        ? advance()
        : expect_identifier("assignment target");
    Expression expression { ExpressionKind::Identifier, name.text, { },
        name.span };
    for (;;) {
        if (at(TokenKind::Dot) || at(TokenKind::Scope)) {
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
        && root->text == name.text) {
        note_implicit_net_reference(name);
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
        Expression right = parse_expression(operation->precedence + 1);
        const auto combined = cover(left.span, right.span);
        left = Expression { ExpressionKind::Binary, operation->name,
            { std::move(left), std::move(right) }, combined };
    }
    if (minimum_precedence == 0 && match(TokenKind::Question)) {
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
        Expression operand = parse_unary();
        return Expression { ExpressionKind::Unary, operation.text,
            { std::move(operand) },
            cover(operation.span, operand.span) };
    }
    return parse_primary();
}

Expression VerilogParser::parse_primary()
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
            error(
                current(),
                "FSIM-SV-PARSE-031",
                "tagged-union construction requires a member value");
            return Expression {
                ExpressionKind::Call,
                "@sv-tagged:" + member.text,
                { },
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
                auto first = parse_expression();
                if (match(TokenKind::Colon)) {
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
                    "FSIM-SV-SEM-188",
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
        return parse_postfix(std::move(expression));
    }
    if (at(TokenKind::Identifier)
        || (keyword("void") && at(TokenKind::Apostrophe, 1))) {
        const auto name = advance();
        std::string canonical = name.text;
        if (match(TokenKind::Hash)) {
            Instance actual_owner;
            parse_parameter_overrides(actual_owner, previous());
            canonical += "#(";
            for (std::size_t index = 0;
                index < actual_owner.parameter_overrides.size(); ++index) {
                if (index != 0)
                    canonical += ",";
                const auto& actual = actual_owner.parameter_overrides[index];
                if (actual.name) {
                    canonical += "." + *actual.name + "(";
                }
                if (actual.type_value) {
                    canonical += actual.type_value->named_type.empty()
                        ? actual.type_value->spelling
                        : actual.type_value->named_type;
                } else {
                    canonical += actual.value.text;
                }
                if (actual.name)
                    canonical += ")";
            }
            canonical += ")";
        }
        while (match(TokenKind::Scope)) {
            canonical += "::";
            const bool standard_member = canonical == "std::"
                && at(TokenKind::Identifier)
                && find_systemverilog_standard_package_declaration(
                       standard_revision_, current().text) != nullptr;
            canonical += (standard_member
                ? advance()
                : expect_identifier("package-scoped name")).text;
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
        if (const auto required = verilog_system_service_standard(canonical)) {
            (void)require_standard(
                "the system service '" + canonical + "'", *required, name,
                "FSIM-SV-PARSE-350");
        }
        if (canonical == "new" && at(TokenKind::LeftBracket)) {
            (void)require_standard(
                "the predefined new operator",
                StandardRevision::SystemVerilog2005,
                name,
                "FSIM-SV-PARSE-349");
        }
        if (match(TokenKind::LeftParen)) {
            std::vector<Expression> arguments;
            std::vector<std::string> argument_names;
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
            expression = Expression { ExpressionKind::Call, canonical,
                std::move(arguments),
                cover(name.span, previous().span) };
            expression.call_argument_names = std::move(argument_names);
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
                if (expression.operands.size() != 1U) {
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
            } else if (contains_word(
                           { "$bits", "$clog2", "$countones", "$dimensions",
                               "$unpacked_dimensions" },
                           canonical)) {
                require_file_call(1, "one expression or data type");
                expression.call_result_width = 32;
                expression.call_result_domain = ValueDomain::Bit2;
                expression.call_result_signed = true;
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
            return Expression {
                ExpressionKind::Call, "@sv-new", { }, name.span
            };
        }
        if (canonical == "$urandom" || canonical == "$random"
            || canonical == "$time" || canonical == "$stime"
            || canonical == "$realtime") {
            expression.kind = ExpressionKind::Call;
            if (canonical == "$random" || canonical == "$urandom") {
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
            note_implicit_net_reference(name);
        }
        return expression;
    }
    if (match(TokenKind::LeftParen)) {
        const auto open = previous();
        Expression expression = parse_expression();
        expect(TokenKind::RightParen, "')' after expression",
            "FSIM-SV-PARSE-029");
        expression.span = span_from(open, previous());
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
            if (!at(TokenKind::LeftBrace)) {
                if (keyword("byte") || keyword("shortint")
                    || keyword("int") || keyword("longint")
                    || keyword("integer") || keyword("time")) {
                    const auto type = advance();
                    slice_size = Expression {
                        ExpressionKind::Identifier,
                        type.text,
                        { },
                        type.span
                    };
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
                    operands.push_back(parse_expression());
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
        return Expression { ExpressionKind::Concatenation, "concat",
            std::move(elements), span_from(open, previous()) };
    }
    const auto invalid = advance();
    error(invalid, "FSIM-SV-PARSE-031", "expected expression");
    return Expression { ExpressionKind::Invalid, invalid.text, { },
        invalid.span };
}

} // namespace fsim::frontend
