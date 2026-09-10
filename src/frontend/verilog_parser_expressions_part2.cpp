// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <limits>

namespace fsim::frontend {

[[nodiscard]] std::optional<StandardRevision>
verilog_system_service_standard(std::string_view name);

std::optional<SystemVerilogDecimalLiteral>
VerilogParser::parse_systemverilog_decimal_literal(
    const Token& number,
    const std::optional<Token>& unit)
{
    std::string compact;
    compact.reserve(number.text.size());
    for (const char character : number.text) {
        if (character != '_') {
            compact.push_back(character);
        }
    }

    const auto exponent_position = compact.find_first_of("eE");
    if (exponent_position != std::string::npos
        && compact.find_first_of("eE", exponent_position + 1)
            != std::string::npos) {
        error(
            number,
            "FSIM-SV-PARSE-281",
            "malformed SystemVerilog real/time literal '" + number.text + "'");
        return std::nullopt;
    }
    const auto mantissa = std::string_view { compact }.substr(0, exponent_position);
    auto exponent_text = exponent_position == std::string::npos
        ? std::string_view { }
        : std::string_view { compact }.substr(exponent_position + 1);
    bool negative_exponent = false;
    if (!exponent_text.empty()
        && (exponent_text.front() == '+'
            || exponent_text.front() == '-')) {
        negative_exponent = exponent_text.front() == '-';
        exponent_text.remove_prefix(1);
    }
    std::int64_t explicit_exponent { };
    if (exponent_position != std::string::npos) {
        const auto parsed = decimal_i64(exponent_text, negative_exponent);
        if (!parsed) {
            error(
                number,
                "FSIM-SV-PARSE-281",
                "malformed or excessive SystemVerilog real/time exponent in '"
                    + number.text + "'");
            return std::nullopt;
        }
        explicit_exponent = *parsed;
    }

    std::string digits;
    digits.reserve(mantissa.size());
    bool saw_decimal = false;
    std::size_t fractional_digits { };
    for (const char character : mantissa) {
        if (character == '.') {
            if (saw_decimal || digits.empty()) {
                error(
                    number,
                    "FSIM-SV-PARSE-281",
                    "malformed SystemVerilog real/time mantissa '"
                        + number.text + "'");
                return std::nullopt;
            }
            saw_decimal = true;
            continue;
        }
        if (character < '0' || character > '9') {
            error(
                number,
                "FSIM-SV-PARSE-281",
                "malformed SystemVerilog real/time literal '" + number.text + "'");
            return std::nullopt;
        }
        digits.push_back(character);
        if (saw_decimal) {
            ++fractional_digits;
        }
    }
    if (digits.empty() || (saw_decimal && fractional_digits == 0)) {
        error(
            number,
            "FSIM-SV-PARSE-281",
            "malformed SystemVerilog real/time mantissa '" + number.text + "'");
        return std::nullopt;
    }
    if (fractional_digits
        > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())) {
        error(
            number,
            "FSIM-SV-PARSE-281",
            "SystemVerilog real/time literal exceeds the source representation");
        return std::nullopt;
    }
    const auto fractional = static_cast<std::int64_t>(fractional_digits);
    if (explicit_exponent
        < std::numeric_limits<std::int64_t>::min() + fractional) {
        error(
            number,
            "FSIM-SV-PARSE-281",
            "SystemVerilog real/time exponent exceeds the source representation");
        return std::nullopt;
    }
    auto canonical_exponent = explicit_exponent - fractional;
    const auto first_nonzero = digits.find_first_not_of('0');
    if (first_nonzero == std::string::npos) {
        digits = "0";
        canonical_exponent = 0;
    } else {
        digits.erase(0, first_nonzero);
        while (digits.size() > 1 && digits.back() == '0') {
            if (canonical_exponent == std::numeric_limits<std::int64_t>::max()) {
                error(
                    number,
                    "FSIM-SV-PARSE-281",
                    "SystemVerilog real/time exponent exceeds the source representation");
                return std::nullopt;
            }
            digits.pop_back();
            ++canonical_exponent;
        }
    }
    return SystemVerilogDecimalLiteral {
        unit ? SystemVerilogDecimalLiteralKind::Time
             : SystemVerilogDecimalLiteralKind::Real,
        std::move(digits),
        canonical_exponent,
        unit ? unit->text : std::string { }
    };
}

Expression VerilogParser::parse_postfix(Expression expression)
{
    for (;;) {
        if (expression.kind == ExpressionKind::Call
            && (expression.text == "randomize"
                || expression.text == ".randomize"
                || expression.text.ends_with("::randomize"))
            && current().text == "with") {
            advance();
            if (!match(TokenKind::LeftBrace)) {
                error(
                    current(),
                    "FSIM-SV-PARSE-334",
                    "expected '{' after randomize with");
            } else {
                auto constraints = parse_constraint_block_expressions(
                    "'}' after randomize with constraint block",
                    "FSIM-SV-PARSE-335");
                expression.aggregate_choices.push_back(
                    "@sv-inline-constraint");
                expression.aggregate_choice_expressions.push_back(
                    std::move(constraints));
                expression.span = cover(expression.span, previous().span);
            }
            continue;
        }
        if (match(TokenKind::Dot)) {
            auto member = current();
            if (keyword("new")) {
                member = advance();
            } else if (member.kind == TokenKind::Identifier
                && (member.text == "and"
                    || member.text == "or"
                    || member.text == "xor"
                    || member.text == "unique")) {
                advance();
            } else {
                member = expect_identifier("member name");
            }
            const bool parenthesized_call = match(TokenKind::LeftParen);
            const bool implicit_container_with = !parenthesized_call && current().text == "with"
                && (member.text == "sum"
                    || member.text == "product"
                    || member.text == "and"
                    || member.text == "or"
                    || member.text == "xor"
                    || member.text == "sort"
                    || member.text == "rsort"
                    || member.text == "min"
                    || member.text == "max"
                    || member.text == "unique"
                    || member.text == "unique_index"
                    || member.text == "find"
                    || member.text == "find_index"
                    || member.text == "find_first"
                    || member.text == "find_first_index"
                    || member.text == "find_last"
                    || member.text == "find_last_index");
            if (parenthesized_call || implicit_container_with) {
                const auto receiver_span = expression.span;
                const auto receiver_type = [&]() -> const Type* {
                    if (expression.kind != ExpressionKind::Identifier)
                        return nullptr;
                    const auto found = current_procedural_types_.find(expression.text);
                    return found == current_procedural_types_.end()
                        ? nullptr
                        : &found->second;
                }();
                const bool known_container_receiver = receiver_type != nullptr
                    && receiver_type->systemverilog_container.has_value();
                const bool known_string_receiver = receiver_type != nullptr
                    && receiver_type->domain == ValueDomain::String;
                const bool known_class_receiver = receiver_type != nullptr
                    && !known_container_receiver
                    && (!receiver_type->named_type.empty()
                        || !receiver_type
                            ->systemverilog_class_declaration.empty());
                if (known_container_receiver || known_string_receiver) {
                    (void)require_standard(
                        "standard method '" + member.text + "'",
                        StandardRevision::SystemVerilog2005,
                        member,
                        "FSIM-SV-PARSE-349");
                }
                const bool predicate_locator_candidate = member.text == "find"
                    || member.text == "find_index"
                    || member.text == "find_first"
                    || member.text == "find_first_index"
                    || member.text == "find_last"
                    || member.text == "find_last_index";
                const auto implicit_reference_count = implicit_net_references_.size();
                std::vector<Expression> operands;
                std::vector<std::string> argument_names(1);
                operands.push_back(std::move(expression));
                if (parenthesized_call && !at(TokenKind::RightParen)) {
                    do {
                        if (at(TokenKind::Comma)
                            || at(TokenKind::RightParen)) {
                            argument_names.emplace_back();
                            operands.emplace_back();
                        } else if (match(TokenKind::Dot)) {
                            const auto formal = expect_identifier("named method argument");
                            expect(
                                TokenKind::LeftParen,
                                "'(' after named method argument",
                                "FSIM-SV-PARSE-224");
                            argument_names.push_back(formal.text);
                            if (at(TokenKind::RightParen)) {
                                operands.emplace_back();
                            } else {
                                operands.push_back(parse_expression());
                            }
                            expect(
                                TokenKind::RightParen,
                                "')' after named method argument",
                                "FSIM-SV-PARSE-225");
                        } else {
                            argument_names.emplace_back();
                            operands.push_back(parse_expression());
                        }
                    } while (match(TokenKind::Comma));
                }
                if (parenthesized_call) {
                    expect(
                        TokenKind::RightParen,
                        "')' after method arguments",
                        "FSIM-SV-PARSE-028");
                }
                const bool predicate_locator_method = predicate_locator_candidate
                    && !known_class_receiver
                    && (known_container_receiver
                        || current().text == "with");
                const auto argument_count = operands.size() - 1U;
                const bool reduction_method = member.text == "sum"
                    || member.text == "product"
                    || member.text == "and"
                    || member.text == "or"
                    || member.text == "xor";
                const bool ordering_method = member.text == "reverse"
                    || member.text == "sort"
                    || member.text == "rsort";
                const bool ordering_key_method = member.text == "sort"
                    || member.text == "rsort";
                const bool unsupported_shuffle = member.text == "shuffle";
                const bool locator_method = member.text == "min"
                    || member.text == "max"
                    || member.text == "unique"
                    || member.text == "unique_index";
                const bool valid_iterator_argument = (predicate_locator_method
                                                         || reduction_method
                                                         || ordering_key_method
                                                         || locator_method)
                    && argument_count == 1
                    && operands[1].kind == ExpressionKind::Identifier
                    && operands[1].text.find('.') == std::string::npos;
                if (valid_iterator_argument) {
                    implicit_net_references_.resize(
                        implicit_reference_count);
                    container_iterator_names_.insert(
                        operands[1].text);
                }
                const auto expected_arguments = member.text == "insert"
                    ? std::optional<std::size_t> { 2 }
                    : member.text == "push_front"
                        || member.text == "push_back"
                        || member.text == "exists"
                        || member.text == "first"
                        || member.text == "last"
                        || member.text == "next"
                        || member.text == "prev"
                        || member.text == "getc"
                        || member.text == "compare"
                        || member.text == "icompare"
                        || member.text == "itoa"
                        || member.text == "hextoa"
                        || member.text == "octtoa"
                        || member.text == "bintoa"
                        || member.text == "realtoa"
                    ? std::optional<std::size_t> { 1 }
                    : member.text == "size"
                        || member.text == "pop_front"
                        || member.text == "pop_back"
                        || member.text == "reverse"
                        || member.text == "len"
                        || member.text == "toupper"
                        || member.text == "tolower"
                        || member.text == "atoi"
                        || member.text == "atohex"
                        || member.text == "atooct"
                        || member.text == "atobin"
                        || member.text == "atoreal"
                        || unsupported_shuffle
                    ? std::optional<std::size_t> { 0 }
                    : member.text == "substr"
                        || member.text == "putc"
                    ? std::optional<std::size_t> { 2 }
                    : member.text == "delete"
                    ? (argument_count <= 1
                              ? std::optional<std::size_t> { argument_count }
                              : std::optional<std::size_t> { 1 })
                    : std::nullopt;
                if (expected_arguments
                    && argument_count != *expected_arguments
                    && !known_class_receiver) {
                    const bool string_method = member.text == "getc" || member.text == "compare"
                        || member.text == "icompare" || member.text == "len"
                        || member.text == "toupper" || member.text == "tolower"
                        || member.text == "substr" || member.text == "putc"
                        || member.text == "atoi" || member.text == "atohex"
                        || member.text == "atooct" || member.text == "atobin"
                        || member.text == "atoreal"
                        || member.text == "itoa" || member.text == "hextoa"
                        || member.text == "octtoa" || member.text == "bintoa"
                        || member.text == "realtoa";
                    error(
                        member,
                        string_method ? "FSIM-SV-SEM-127" : "FSIM-SV-SEM-081",
                        std::string { string_method ? "string" : "container" }
                            + " method '" + member.text + "' requires "
                            + std::to_string(*expected_arguments)
                            + " argument(s)");
                }
                if (predicate_locator_method
                    && (argument_count > 1
                        || (argument_count == 1
                            && operands[1].kind
                                != ExpressionKind::Identifier)
                        || (argument_count == 1
                            && operands[1].text.find('.')
                                != std::string::npos))) {
                    error(
                        member,
                        "FSIM-SV-SEM-090",
                        "predicate container locator method '" + member.text
                            + "' accepts at most one iterator identifier");
                }
                if (reduction_method
                    && (argument_count > 1
                        || (argument_count == 1
                            && operands[1].kind
                                != ExpressionKind::Identifier)
                        || (argument_count == 1
                            && operands[1].text.find('.')
                                != std::string::npos))) {
                    error(
                        member,
                        "FSIM-SV-SEM-094",
                        "container reduction method '" + member.text
                            + "' accepts at most one transformation iterator "
                              "identifier");
                }
                if (ordering_key_method
                    && (argument_count > 1
                        || (argument_count == 1
                            && operands[1].kind
                                != ExpressionKind::Identifier)
                        || (argument_count == 1
                            && operands[1].text.find('.')
                                != std::string::npos))) {
                    error(
                        member,
                        "FSIM-SV-SEM-092",
                        "container ordering method '" + member.text
                            + "' accepts at most one iterator identifier");
                }
                if (locator_method
                    && (argument_count > 1
                        || (argument_count == 1
                            && operands[1].kind
                                != ExpressionKind::Identifier)
                        || (argument_count == 1
                            && operands[1].text.find('.')
                                != std::string::npos))) {
                    error(
                        member,
                        "FSIM-SV-SEM-093",
                        "container locator method '" + member.text
                            + "' accepts at most one transformation iterator "
                              "identifier");
                }
                if (reduction_method
                    && language_ != Language::SystemVerilog2017) {
                    error(
                        member,
                        "FSIM-SV-SEM-085",
                        "container reduction methods require SystemVerilog 2017");
                }
                if (reduction_method) {
                    const auto iterator_scope_name = valid_iterator_argument
                        ? operands[1].text
                        : std::string { "item" };
                    if (current().text != "with") {
                        if (argument_count != 0) {
                            error(
                                current(),
                                "FSIM-SV-SEM-094",
                                "a named container reduction transformation iterator "
                                "requires a with-clause");
                        }
                    } else {
                        container_iterator_names_.insert(iterator_scope_name);
                        const bool iterator_scope_inserted = current_procedural_names_.insert(
                                                                                          iterator_scope_name)
                                                                 .second;
                        advance();
                        expect(
                            TokenKind::LeftParen,
                            "'(' after container reduction with",
                            "FSIM-SV-PARSE-167");
                        if (at(TokenKind::RightParen)) {
                            error(
                                current(),
                                "FSIM-SV-SEM-091",
                                "a container reduction with-clause requires a "
                                "transformation expression");
                        } else {
                            operands.push_back(parse_expression());
                        }
                        if (iterator_scope_inserted) {
                            current_procedural_names_.erase(
                                iterator_scope_name);
                        }
                        expect(
                            TokenKind::RightParen,
                            "')' after container reduction transformation",
                            "FSIM-SV-PARSE-168");
                    }
                }
                if ((ordering_method || unsupported_shuffle)
                    && language_ != Language::SystemVerilog2017) {
                    error(
                        member,
                        "FSIM-SV-SEM-086",
                        "container ordering methods require SystemVerilog 2017");
                }
                if ((member.text == "reverse" || unsupported_shuffle)
                    && current().text == "with") {
                    error(
                        current(),
                        "FSIM-SV-UNSUPPORTED-041",
                        "reverse and shuffle ordering with-clauses are not "
                        "supported");
                }
                if (ordering_key_method) {
                    const auto iterator_scope_name = valid_iterator_argument
                        ? operands[1].text
                        : std::string { "item" };
                    if (current().text != "with") {
                        if (argument_count != 0) {
                            error(
                                current(),
                                "FSIM-SV-SEM-092",
                                "a named container ordering iterator requires a "
                                "with-clause");
                        }
                    } else {
                        container_iterator_names_.insert(iterator_scope_name);
                        const bool iterator_scope_inserted = current_procedural_names_.insert(
                                                                                          iterator_scope_name)
                                                                 .second;
                        advance();
                        expect(
                            TokenKind::LeftParen,
                            "'(' after container ordering with",
                            "FSIM-SV-PARSE-169");
                        if (at(TokenKind::RightParen)) {
                            error(
                                current(),
                                "FSIM-SV-SEM-092",
                                "a container ordering with-clause requires a key "
                                "expression");
                        } else {
                            operands.push_back(parse_expression());
                        }
                        if (iterator_scope_inserted) {
                            current_procedural_names_.erase(
                                iterator_scope_name);
                        }
                        expect(
                            TokenKind::RightParen,
                            "')' after container ordering key expression",
                            "FSIM-SV-PARSE-170");
                    }
                }
                if (locator_method
                    && language_ != Language::SystemVerilog2017) {
                    error(
                        member,
                        "FSIM-SV-SEM-087",
                        "container locator methods require SystemVerilog 2017");
                }
                if (locator_method) {
                    const auto iterator_scope_name = valid_iterator_argument
                        ? operands[1].text
                        : std::string { "item" };
                    if (current().text != "with") {
                        if (argument_count != 0) {
                            error(
                                current(),
                                "FSIM-SV-SEM-093",
                                "a named container locator transformation iterator "
                                "requires a with-clause");
                        }
                    } else {
                        container_iterator_names_.insert(iterator_scope_name);
                        const bool iterator_scope_inserted = current_procedural_names_.insert(
                                                                                          iterator_scope_name)
                                                                 .second;
                        advance();
                        expect(
                            TokenKind::LeftParen,
                            "'(' after container locator transformation with",
                            "FSIM-SV-PARSE-171");
                        if (at(TokenKind::RightParen)) {
                            error(
                                current(),
                                "FSIM-SV-SEM-093",
                                "a container locator with-clause requires a "
                                "transformation expression");
                        } else {
                            operands.push_back(parse_expression());
                        }
                        if (iterator_scope_inserted) {
                            current_procedural_names_.erase(
                                iterator_scope_name);
                        }
                        expect(
                            TokenKind::RightParen,
                            "')' after container locator transformation",
                            "FSIM-SV-PARSE-172");
                    }
                }
                if (predicate_locator_method
                    && language_ != Language::SystemVerilog2017) {
                    error(
                        member,
                        "FSIM-SV-SEM-088",
                        "predicate container locator methods require "
                        "SystemVerilog 2017");
                }
                if (predicate_locator_method) {
                    const auto iterator_scope_name = valid_iterator_argument
                        ? operands[1].text
                        : std::string { "item" };
                    container_iterator_names_.insert(iterator_scope_name);
                    const bool iterator_scope_inserted = current_procedural_names_.insert(
                                                                                      iterator_scope_name)
                                                             .second;
                    if (current().text != "with") {
                        error(
                            current(),
                            "FSIM-SV-SEM-089",
                            "predicate container locator method '" + member.text
                                + "' requires a with-clause");
                    } else {
                        advance();
                        expect(
                            TokenKind::LeftParen,
                            "'(' after container locator with",
                            "FSIM-SV-PARSE-165");
                        if (at(TokenKind::RightParen)) {
                            error(
                                current(),
                                "FSIM-SV-SEM-089",
                                "a container locator with-clause requires a predicate");
                        } else {
                            operands.push_back(parse_expression());
                        }
                        expect(
                            TokenKind::RightParen,
                            "')' after container locator predicate",
                            "FSIM-SV-PARSE-166");
                    }
                    if (iterator_scope_inserted) {
                        current_procedural_names_.erase(
                            iterator_scope_name);
                    }
                }
                expression = Expression {
                    ExpressionKind::Call,
                    "." + member.text,
                    std::move(operands),
                    cover(receiver_span, previous().span)
                };
                expression.call_argument_names = std::move(argument_names);
                if (member.text == "atoreal") {
                    expression.call_result_width = 64;
                    expression.call_result_domain = ValueDomain::Bit2;
                    expression.systemverilog_scalar_kind = SystemVerilogScalarKind::Real;
                }
                continue;
            }
            const auto selected_span = cover(expression.span, member.span);
            if (expression.kind == ExpressionKind::Identifier) {
                expression.text += '.';
                expression.text += member.text;
                expression.span = selected_span;
            } else {
                expression = Expression {
                    ExpressionKind::Call,
                    "@sv-select:" + member.text,
                    { std::move(expression) },
                    selected_span
                };
            }
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
                    "FSIM-SV-PARSE-032");
                expression = Expression { ExpressionKind::Slice, direction.text,
                    { std::move(expression), std::move(first),
                        std::move(width) },
                    cover(expression.span, previous().span) };
            } else if (match(TokenKind::Colon)) {
                Expression second = parse_expression();
                expect(TokenKind::RightBracket, "']' after part-select",
                    "FSIM-SV-PARSE-032");
                expression = Expression { ExpressionKind::Slice, ":",
                    { std::move(expression), std::move(first),
                        std::move(second) },
                    cover(expression.span, previous().span) };
            } else {
                expect(TokenKind::RightBracket, "']' after index",
                    "FSIM-SV-PARSE-033");
                expression = Expression { ExpressionKind::Index, "index",
                    { std::move(expression), std::move(first) },
                    cover(expression.span, previous().span) };
            }
        } else if (
            at(TokenKind::LeftParen)
            && expression.kind == ExpressionKind::Index
            && expression.operands.size() == 2U
            && expression.operands[0].kind == ExpressionKind::Identifier
            && expression.operands[0].text == "new") {
            const auto expression_span = expression.span;
            std::vector<Expression> operands;
            operands.push_back(std::move(expression.operands[1]));
            advance();
            if (!at(TokenKind::RightParen)) {
                do {
                    operands.push_back(parse_expression());
                } while (match(TokenKind::Comma));
            }
            expect(
                TokenKind::RightParen,
                "')' after dynamic-array initialization",
                "FSIM-SV-PARSE-223");
            if (operands.size() != 2U) {
                error(
                    previous(), "FSIM-SV-SEM-128",
                    "new[size](initializer) requires exactly one initializer");
            }
            expression = Expression {
                ExpressionKind::Call, "@new-array", std::move(operands),
                cover(expression_span, previous().span)
            };
        } else if (match(TokenKind::PlusPlus)
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
            const auto combined = cover(expression.span, operation.span);
            expression = Expression {
                ExpressionKind::Update,
                operation.kind == TokenKind::PlusPlus ? "post++" : "post--",
                { std::move(expression) },
                combined
            };
            break;
        } else {
            break;
        }
    }
    return expression;
}

} // namespace fsim::frontend
