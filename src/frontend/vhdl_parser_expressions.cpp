// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

#include <bit>
#include <new>

namespace fsim::frontend {

std::optional<Expression> VhdlParser::parse_vhdl_bit_string_literal()
{
    std::size_t specifier_offset = 0;
    std::optional<std::size_t> explicit_width;
    if (at(TokenKind::Number) && at(TokenKind::Identifier, 1) && at(TokenKind::StringLiteral, 2)) {
        specifier_offset = 1;
    } else if (!at(TokenKind::Identifier) || !at(TokenKind::StringLiteral, 1)) {
        return std::nullopt;
    }

    const auto specifier = detail::ascii_lower(current(specifier_offset).text);
    if (specifier != "b" && specifier != "o" && specifier != "x" && specifier != "d" && specifier != "ub" && specifier != "uo" && specifier != "ux" && specifier != "sb" && specifier != "so" && specifier != "sx" && specifier != "ud" && specifier != "sd") {
        return std::nullopt;
    }

    const auto start = current();
    bool malformed = specifier == "ud" || specifier == "sd";
    bool representation_failure = false;
    if (specifier_offset != 0) {
        const auto width = detail::decimal_u64(advance().text);
        if (!width || *width > std::numeric_limits<std::size_t>::max() || *width > std::string { }.max_size() - 2U) {
            malformed = true;
            representation_failure = true;
            explicit_width = 0;
        } else {
            explicit_width = static_cast<std::size_t>(*width);
        }
    }
    const auto specifier_token = advance();
    const auto digits_token = advance();
    auto digits = std::string_view { digits_token.text };
    if (digits.size() >= 2 && digits.front() == '"' && digits.back() == '"') {
        digits.remove_prefix(1);
        digits.remove_suffix(1);
    }
    if (specifier_offset != 0 && start.span.end.offset != specifier_token.span.begin.offset) {
        malformed = true;
    }
    if (specifier_token.span.end.offset != digits_token.span.begin.offset) {
        malformed = true;
    }
    std::string bits;
    try {
        std::size_t ring_start = 0;
        std::optional<char> discarded_character;
        bool discarded_uniform = true;
        const auto discard = [&](const char character) {
            if (!discarded_character) {
                discarded_character = character;
            } else if (*discarded_character != character) {
                discarded_uniform = false;
            }
        };
        const auto append_bit = [&](const char character) {
            if (!explicit_width) {
                bits.push_back(character);
                return;
            }
            if (*explicit_width == 0) {
                discard(character);
                return;
            }
            if (bits.size() < *explicit_width) {
                bits.push_back(character);
                return;
            }
            discard(bits[ring_start]);
            bits[ring_start] = character;
            ring_start = ring_start + 1U == *explicit_width ? 0U : ring_start + 1U;
        };
        const char base = specifier.back();
        const auto append_digit = [&](const char raw, const unsigned radix_width) {
            const auto character = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
            if (std::string_view { "UXZWLH-" }.find(character) != std::string_view::npos) {
                for (unsigned index = 0; index < radix_width; ++index) {
                    append_bit(character);
                }
                return;
            }
            const auto position = std::string_view { "0123456789ABCDEF" }.find(character);
            if (position == std::string_view::npos || position >= (std::size_t { 1 } << radix_width)) {
                malformed = true;
                return;
            }
            for (unsigned shift = radix_width; shift != 0; --shift) {
                append_bit(((position >> (shift - 1)) & 1U) != 0 ? '1' : '0');
            }
        };
        bool previous_underscore = false;
        std::size_t value_character_count = 0;
        const auto consume_underscore = [&](const char raw) {
            if (raw != '_') {
                previous_underscore = false;
                ++value_character_count;
                return false;
            }
            if (previous_underscore || value_character_count == 0) {
                malformed = true;
            }
            previous_underscore = true;
            return true;
        };
        if (base == 'b') {
            for (const char raw : digits) {
                if (consume_underscore(raw)) {
                    continue;
                }
                const auto character = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
                if (std::string_view { "01UXZWLH-" }.find(character) == std::string_view::npos) {
                    malformed = true;
                } else {
                    append_bit(character);
                }
            }
        } else if (base == 'o' || base == 'x') {
            const auto radix_width = base == 'o' ? 3U : 4U;
            for (const char raw : digits) {
                if (consume_underscore(raw)) {
                    continue;
                }
                append_digit(raw, radix_width);
            }
        } else {
            constexpr std::array<std::uint32_t, 10> powers_of_ten {
                1U, 10U, 100U, 1'000U, 10'000U,
                100'000U, 1'000'000U, 10'000'000U, 100'000'000U, 1'000'000'000U
            };
            std::vector<std::uint32_t> limbs { 0U };
            std::uint32_t chunk = 0;
            unsigned chunk_digits = 0;
            const auto flush_chunk = [&]() {
                if (chunk_digits == 0) {
                    return;
                }
                const auto multiplier = powers_of_ten[chunk_digits];
                std::uint64_t carry = chunk;
                for (auto& limb : limbs) {
                    const auto expanded = static_cast<std::uint64_t>(limb) * multiplier + carry;
                    limb = static_cast<std::uint32_t>(expanded);
                    carry = expanded >> 32U;
                }
                if (carry != 0) {
                    limbs.push_back(static_cast<std::uint32_t>(carry));
                }
                chunk = 0;
                chunk_digits = 0;
            };
            for (const char raw : digits) {
                if (consume_underscore(raw)) {
                    continue;
                }
                if (!std::isdigit(static_cast<unsigned char>(raw))) {
                    malformed = true;
                    continue;
                }
                chunk = chunk * 10U + static_cast<std::uint32_t>(raw - '0');
                ++chunk_digits;
                if (chunk_digits == 9) {
                    flush_chunk();
                }
            }
            flush_chunk();
            if (value_character_count == 0) {
                malformed = true;
            }
            while (limbs.size() > 1U && limbs.back() == 0U) {
                limbs.pop_back();
            }
            if (limbs.size() == 1U && limbs.front() == 0U) {
                append_bit('0');
            } else {
                const auto most_significant_bits = 32U - static_cast<unsigned>(std::countl_zero(limbs.back()));
                for (unsigned shift = most_significant_bits; shift != 0; --shift) {
                    append_bit(((limbs.back() >> (shift - 1U)) & 1U) != 0 ? '1' : '0');
                }
                for (auto limb = limbs.rbegin() + 1; limb != limbs.rend(); ++limb) {
                    for (unsigned shift = 32U; shift != 0; --shift) {
                        append_bit(((*limb >> (shift - 1U)) & 1U) != 0 ? '1' : '0');
                    }
                }
            }
        }
        if (previous_underscore) {
            malformed = true;
        }

        if (ring_start != 0) {
            std::rotate(bits.begin(),
                bits.begin() + static_cast<std::ptrdiff_t>(ring_start),
                bits.end());
        }
        if (discarded_character) {
            const char fill = specifier.front() == 's' && !bits.empty() ? bits.front() : '0';
            if (!discarded_uniform || *discarded_character != fill) {
                malformed = true;
            }
            if (specifier.front() == 's' && bits.empty()) {
                malformed = true;
            }
        }
        if (explicit_width && bits.size() < *explicit_width) {
            char extension = '0';
            if (specifier.front() == 's') {
                if (bits.empty()) {
                    malformed = true;
                } else {
                    extension = bits.front();
                }
            }
            bits.insert(bits.begin(), *explicit_width - bits.size(), extension);
        }
    } catch (const std::bad_alloc&) {
        std::string { }.swap(bits);
        malformed = true;
        representation_failure = true;
    } catch (const std::length_error&) {
        std::string { }.swap(bits);
        malformed = true;
        representation_failure = true;
    }
    if (malformed) {
        error(specifier_token, "FSIM-VHDL-PARSE-263",
            representation_failure
                ? "a VHDL bit-string literal cannot be materialized within the "
                  "host-addressable string representation"
                : "a VHDL bit-string literal requires adjacent B/O/X/D, "
                  "UB/UO/UX, or SB/SO/SX syntax, valid separated digits, and "
                  "lossless signed or unsigned adjustment");
    }
    return Expression { ExpressionKind::StringLiteral,
        "\"" + bits + "\"",
        { },
        cover(start.span, digits_token.span),
        { },
        { },
        { },
        bits };
}

Expression VhdlParser::parse_expression(
    int minimum_precedence, bool allow_conditional)
{
    Expression left = parse_unary();
    for (;;) {
        const auto operation = binary_operation();
        if (!operation || operation->precedence < minimum_precedence) {
            break;
        }
        const auto operator_token = advance();
        for (std::size_t index = 1; index < operation->token_count; ++index) {
            advance();
        }
        if (operation->name == "xnor" || operation->name == "sll" || operation->name == "srl" || operation->name == "sla" || operation->name == "sra" || operation->name == "rol" || operation->name == "ror") {
            require_vhdl_standard(
                operator_token, VhdlStandard::Vhdl1993,
                "the '" + operation->name + "' operator",
                "select VHDL-1993 or replace it with an explicitly declared "
                "VHDL-1987 subprogram");
        }
        if (operation->name == "?=" || operation->name == "?/=") {
            require_vhdl_standard(
                operator_token, VhdlStandard::Vhdl2008,
                "the '" + operation->name + "' matching operator",
                "select VHDL-2008 or use an ordinary equality operator");
        }
        if (operation->name == "**" && (at(TokenKind::Plus) || at(TokenKind::Minus))) {
            error(current(), "FSIM-VHDL-PARSE-114",
                "a signed VHDL exponent must be parenthesized");
        }
        Expression right = parse_expression(
            operation->precedence + 1, allow_conditional);
        const auto combined_span = cover(left.span, right.span);
        left = Expression { ExpressionKind::Binary,
            operation->name,
            { std::move(left), std::move(right) },
            combined_span };
        (void)operator_token;
        if (operation->name == "**" && at(TokenKind::Power)) {
            error(current(), "FSIM-VHDL-PARSE-114",
                "chained VHDL exponentiation requires parentheses");
        }
    }
    if (minimum_precedence == 0 && allow_conditional
        && match_keyword("when", true)) {
        require_vhdl_standard(
            previous(), VhdlStandard::Vhdl2008, "a conditional expression",
            "select VHDL-2008 or rewrite it as a conditional statement");
        const auto begin_span = left.span;
        auto condition = parse_expression();
        expect_keyword("else", true, "FSIM-VHDL-PARSE-115");
        auto when_false = parse_expression();
        const auto span = cover(begin_span, when_false.span);
        left = Expression {
            ExpressionKind::Call,
            "?:",
            { std::move(condition),
                std::move(left),
                std::move(when_false) },
            span
        };
    }
    return left;
}

std::optional<VhdlParser::BinaryOperation>
VhdlParser::binary_operation() const
{
    if (keyword("or", 0, true) || keyword("nor", 0, true) || keyword("xor", 0, true) || keyword("xnor", 0, true)) {
        return BinaryOperation { 1, detail::ascii_lower(current().text) };
    }
    if (keyword("and", 0, true) || keyword("nand", 0, true)) {
        return BinaryOperation { 2, detail::ascii_lower(current().text) };
    }
    if (at(TokenKind::Assign) || (at(TokenKind::NotEqual) && current().text == "/=") || at(TokenKind::Less) || at(TokenKind::LessEqual) || at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
        return BinaryOperation { 3, current().text };
    }
    if (at(TokenKind::Question) && (at(TokenKind::Assign, 1) || (at(TokenKind::NotEqual, 1) && current(1).text == "/="))) {
        return BinaryOperation { 3, at(TokenKind::Assign, 1) ? "?=" : "?/=", 2 };
    }
    if (keyword("sll", 0, true) || keyword("srl", 0, true) || keyword("sla", 0, true) || keyword("sra", 0, true) || keyword("rol", 0, true) || keyword("ror", 0, true)) {
        return BinaryOperation { 4, detail::ascii_lower(current().text) };
    }
    if (at(TokenKind::Ampersand) || at(TokenKind::Plus) || at(TokenKind::Minus)) {
        return BinaryOperation { 5, current().text };
    }
    if (at(TokenKind::Star) || at(TokenKind::Slash) || keyword("mod", 0, true) || keyword("rem", 0, true)) {
        return BinaryOperation { 6, detail::ascii_lower(current().text) };
    }
    if (at(TokenKind::Power)) {
        return BinaryOperation { 7, "**" };
    }
    return std::nullopt;
}

Expression VhdlParser::parse_unary()
{
    if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
        const auto operation = advance();
        Expression operand = parse_expression(6);
        return Expression { ExpressionKind::Unary,
            detail::ascii_lower(operation.text),
            { std::move(operand) },
            cover(operation.span, operand.span) };
    }
    if (keyword("not", 0, true) || keyword("abs", 0, true)) {
        const auto operation = advance();
        Expression operand = parse_unary();
        return Expression { ExpressionKind::Unary,
            detail::ascii_lower(operation.text),
            { std::move(operand) },
            cover(operation.span, operand.span) };
    }
    if (keyword("and", 0, true) || keyword("or", 0, true) || keyword("nand", 0, true) || keyword("nor", 0, true) || keyword("xor", 0, true) || keyword("xnor", 0, true)) {
        const auto operation = advance();
        require_vhdl_standard(
            operation, VhdlStandard::Vhdl2008,
            "the unary '" + detail::ascii_lower(operation.text) + "' reduction operator",
            "select VHDL-2008 or call an explicit reduction function");
        Expression operand = parse_unary();
        return Expression { ExpressionKind::Unary,
            detail::ascii_lower(operation.text),
            { std::move(operand) },
            cover(operation.span, operand.span) };
    }
    if (at(TokenKind::Question) && at(TokenKind::Question, 1)) {
        const auto operation = advance();
        advance();
        require_vhdl_standard(
            operation, VhdlStandard::Vhdl2008,
            "the unary '"
            "??"
            "' condition operator",
            "select VHDL-2008 or compare the operand explicitly");
        Expression operand = parse_unary();
        return Expression { ExpressionKind::Unary, "??", { std::move(operand) },
            cover(operation.span, operand.span) };
    }
    return parse_primary();
}

Expression VhdlParser::parse_primary()
{
    if (match_keyword("new", true)) {
        const auto allocator = previous();
        const auto subtype_start = current();
        auto allocated_type = parse_vhdl_type(true, true);
        Expression subtype { ExpressionKind::Identifier,
            "@vhdl-subtype:" + allocated_type.spelling,
            { },
            cover(subtype_start.span, previous().span) };
        const auto append_constraint =
            [&](const DiscreteRangeExpression& constraint) {
                subtype.operands.push_back(
                    Expression { ExpressionKind::Binary,
                        constraint.descending ? "downto" : "to",
                        { constraint.left, constraint.right },
                        constraint.span });
            };
        if (allocated_type.discrete_range_expression) {
            append_constraint(*allocated_type.discrete_range_expression);
        }
        for (const auto& constraint : allocated_type.vhdl_array_constraints) {
            append_constraint(constraint);
        }
        std::vector<Expression> operands { std::move(subtype) };
        std::string operation = "@vhdl-new";
        if (match(TokenKind::Apostrophe)) {
            operation = "@vhdl-new-qualified";
            operands.push_back(parse_primary());
        }
        return Expression { ExpressionKind::Call, std::move(operation),
            std::move(operands),
            cover(allocator.span, previous().span) };
    }
    if (match_keyword("null", true)) {
        const auto token = previous();
        return Expression { ExpressionKind::Call, "@vhdl-null", { }, token.span };
    }
    if (keyword("true", 0, true) || keyword("false", 0, true)) {
        const auto token = advance();
        return Expression {
            ExpressionKind::BooleanLiteral, vhdl_name(token.text), { }, token.span
        };
    }
    if (auto bit_string = parse_vhdl_bit_string_literal()) {
        return std::move(*bit_string);
    }
    if (at(TokenKind::Number)) {
        const auto token = advance();
        Expression literal {
            ExpressionKind::IntegerLiteral, token.text, { }, token.span
        };
        static constexpr std::array<std::string_view, 34>
            physical_literal_terminators {
                "after", "begin", "case", "downto", "else",
                "elsif", "end", "exit", "for", "generate",
                "if", "in", "inertial", "is", "loop",
                "next", "of", "on", "open", "others",
                "range", "reject", "report", "return", "select",
                "severity", "then", "to", "transport", "unaffected",
                "units", "until", "wait", "when"
            };
        if (at(TokenKind::Identifier) && !binary_operation() && std::ranges::find(physical_literal_terminators, vhdl_name(current().text)) == physical_literal_terminators.end()) {
            const auto unit = advance();
            return Expression { ExpressionKind::Call,
                "@vhdl-physical:" + vhdl_name(unit.text),
                { std::move(literal) },
                cover(token.span, unit.span) };
        }
        return literal;
    }
    if (at(TokenKind::CharacterLiteral)) {
        const auto token = advance();
        return Expression { ExpressionKind::LogicLiteral, token.text, { }, token.span };
    }
    if (at(TokenKind::StringLiteral)) {
        const auto token = advance();
        return Expression {
            ExpressionKind::StringLiteral, token.text, { }, token.span, { }, { }, { },
            string_literal_text(token)
        };
    }
    if (match(TokenKind::ShiftLeft)) {
        const auto start = previous();
        require_vhdl_standard(
            start, VhdlStandard::Vhdl2008, "an external name",
            "select VHDL-2008 or pass the object through an ordinary interface");
        if (!match_keyword("signal", true)) {
            error(current(), "FSIM-VHDL-PARSE-276",
                "bounded external names require the signal object class");
            if (at(TokenKind::Identifier)) {
                advance();
            }
        }

        (void)match(TokenKind::Dot);
        const auto first = expect_identifier("external signal path");
        std::string target = vhdl_name(first.text);
        while (match(TokenKind::Dot)) {
            const auto part = expect_identifier("external signal path element");
            target += '.';
            target += vhdl_name(part.text);
        }
        if (at(TokenKind::LeftParen) || at(TokenKind::Caret)) {
            error(current(), "FSIM-VHDL-PARSE-277",
                "bounded external signal names support local or absolute "
                "dot-separated paths");
        }
        expect(TokenKind::Colon, "':' before external-name subtype indication",
            "FSIM-VHDL-PARSE-278");
        const auto external_type = parse_vhdl_type(true, true);
        expect(TokenKind::ShiftRight, "'>>' after external signal name",
            "FSIM-VHDL-PARSE-279");
        Expression subtype {
            ExpressionKind::Identifier,
            "@vhdl-subtype:" + external_type.spelling,
            { },
            cover(first.span, previous().span)
        };
        subtype.call_result_width = external_type.width().value_or(0);
        subtype.call_result_domain = external_type.domain;
        subtype.call_result_signed = external_type.is_signed;
        subtype.nominal_type = external_type.nominal_type;
        return Expression {
            ExpressionKind::Call,
            "@vhdl-external",
            { Expression { ExpressionKind::Identifier, target, { }, first.span },
                std::move(subtype) },
            cover(start.span, previous().span)
        };
    }
    if (match_keyword("case", true)) {
        const auto start = previous();
        require_vhdl_standard(
            start, VhdlStandard::Vhdl2008, "a case expression",
            "select VHDL-2008 or rewrite it as a case statement");
        const auto selector = parse_expression(0, false);
        expect_keyword("is", true, "FSIM-VHDL-PARSE-270");

        struct CaseExpressionAlternative {
            std::vector<Expression> choices;
            Expression value;
        };
        std::vector<CaseExpressionAlternative> alternatives;
        std::optional<Expression> default_value;
        bool saw_true = false;
        bool saw_false = false;
        do {
            expect_keyword("when", true, "FSIM-VHDL-PARSE-271");
            std::vector<Expression> choices;
            bool others = false;
            do {
                if (match_keyword("others", true)) {
                    others = true;
                    choices.push_back(Expression { ExpressionKind::Identifier,
                        "others", { }, previous().span });
                } else {
                    auto choice = parse_expression(0, false);
                    if (match_keyword("to", true)
                        || match_keyword("downto", true)) {
                        const auto direction = previous();
                        auto right = parse_expression(0, false);
                        const auto span = cover(choice.span, right.span);
                        choice = Expression {
                            ExpressionKind::Binary,
                            detail::ascii_lower(direction.text),
                            { std::move(choice), std::move(right) },
                            span
                        };
                    }
                    if (choice.kind == ExpressionKind::BooleanLiteral) {
                        saw_true = saw_true || choice.text == "true";
                        saw_false = saw_false || choice.text == "false";
                    }
                    choices.push_back(std::move(choice));
                }
            } while (match(TokenKind::Pipe));
            if (others && choices.size() != 1) {
                error(previous(), "FSIM-VHDL-PARSE-272",
                    "others must be the only choice in a case-expression "
                    "alternative");
            }
            expect(TokenKind::Arrow, "'=>' after case-expression choices",
                "FSIM-VHDL-PARSE-273");
            auto value = parse_expression();
            if (others) {
                if (default_value) {
                    error(previous(), "FSIM-VHDL-PARSE-274",
                        "a case expression has more than one others alternative");
                }
                default_value = std::move(value);
            } else {
                alternatives.push_back(
                    CaseExpressionAlternative { std::move(choices), std::move(value) });
            }
            if (!match(TokenKind::Comma)) {
                break;
            }
            if (default_value) {
                error(previous(), "FSIM-VHDL-PARSE-274",
                    "the others case-expression alternative must be last");
            }
        } while (!at_end() && keyword("when", 0, true));

        if (!default_value) {
            if (!(saw_true && saw_false) || alternatives.empty()) {
                error(start, "FSIM-VHDL-PARSE-275",
                    "a bounded case expression requires a final others "
                    "alternative unless Boolean choices are exhaustive");
            }
            if (alternatives.empty()) {
                return Expression {
                    ExpressionKind::Invalid, "case", { }, start.span
                };
            }
            default_value = std::move(alternatives.back().value);
            alternatives.pop_back();
        }

        const auto choice_condition =
            [&](const Expression& choice) {
                if (choice.kind == ExpressionKind::Binary
                    && choice.operands.size() == 2
                    && (choice.text == "to"
                        || choice.text == "downto")) {
                    const bool ascending = choice.text == "to";
                    Expression lower {
                        ExpressionKind::Binary,
                        ascending ? ">=" : "<=",
                        { selector, choice.operands[0] },
                        cover(selector.span, choice.operands[0].span)
                    };
                    Expression upper {
                        ExpressionKind::Binary,
                        ascending ? "<=" : ">=",
                        { selector, choice.operands[1] },
                        cover(selector.span, choice.operands[1].span)
                    };
                    const auto span = cover(lower.span, upper.span);
                    return Expression {
                        ExpressionKind::Binary,
                        "and",
                        { std::move(lower), std::move(upper) },
                        span
                    };
                }
                return Expression {
                    ExpressionKind::Binary,
                    "=",
                    { selector, choice },
                    cover(selector.span, choice.span)
                };
            };

        auto result = std::move(*default_value);
        for (auto alternative = alternatives.rbegin();
            alternative != alternatives.rend(); ++alternative) {
            auto condition = choice_condition(alternative->choices.front());
            for (std::size_t choice = 1;
                choice < alternative->choices.size(); ++choice) {
                auto next = choice_condition(alternative->choices[choice]);
                const auto span = cover(condition.span, next.span);
                condition = Expression {
                    ExpressionKind::Binary,
                    "or",
                    { std::move(condition), std::move(next) },
                    span
                };
            }
            const auto span = cover(start.span, result.span);
            result = Expression {
                ExpressionKind::Call,
                "?:",
                { std::move(condition),
                    std::move(alternative->value),
                    std::move(result) },
                span
            };
        }
        return result;
    }
    if (at(TokenKind::Identifier)) {
        const auto name = advance();
        std::string canonical = vhdl_name(name.text);
        Expression selected { ExpressionKind::Identifier, canonical, { }, name.span };
        bool saw_dereference = false;
        while (match(TokenKind::Dot)) {
            const auto part = expect_identifier("selected name");
            const auto canonical_part = vhdl_name(part.text);
            if (canonical_part == "all") {
                selected.text = canonical;
                selected.span = cover(name.span, part.span);
                selected = Expression { ExpressionKind::Call,
                    "@vhdl-dereference",
                    { std::move(selected) },
                    cover(name.span, part.span) };
                saw_dereference = true;
            } else if (saw_dereference) {
                selected = Expression { ExpressionKind::Call,
                    "@vhdl-member:" + canonical_part,
                    { std::move(selected) },
                    cover(name.span, part.span) };
            } else {
                canonical += '.';
                canonical += canonical_part;
                selected.text = canonical;
                selected.span = cover(name.span, part.span);
            }
        }
        if (saw_dereference) {
            for (;;) {
                if (match(TokenKind::LeftParen)) {
                    auto index = parse_expression();
                    expect(TokenKind::RightParen, "')' after dereferenced index",
                        "FSIM-VHDL-PARSE-251");
                    selected = Expression { ExpressionKind::Index,
                        "index",
                        { std::move(selected), std::move(index) },
                        cover(name.span, previous().span) };
                    continue;
                }
                if (!match(TokenKind::Dot)) {
                    break;
                }
                const auto member = expect_identifier("selected dereferenced element");
                selected = Expression { ExpressionKind::Call,
                    "@vhdl-member:" + vhdl_name(member.text),
                    { std::move(selected) },
                    cover(name.span, member.span) };
            }
            return selected;
        }
        if (match(TokenKind::Apostrophe)) {
            if (at(TokenKind::LeftParen)) {
                auto value = parse_primary();
                return Expression { ExpressionKind::Call,
                    "@vhdl-qualified:" + canonical,
                    { std::move(value) },
                    cover(name.span, previous().span) };
            }
            const auto attribute = expect_identifier("attribute designator");
            const auto designator = vhdl_name(attribute.text);
            static constexpr std::array<std::string_view, 32> supported_attributes {
                "left", "right", "low", "high",
                "length", "ascending", "event", "last_value",
                "last_event", "last_active", "stable", "quiet",
                "active", "transaction", "delayed", "driving",
                "driving_value", "pos", "val", "succ",
                "pred", "leftof", "rightof", "range",
                "reverse_range", "image", "value", "simple_name",
                "instance_name", "path_name", "subtype", "element"
            };
            if (std::ranges::find(supported_attributes, designator) == supported_attributes.end()) {
                error(attribute, "FSIM-VHDL-SEM-030",
                    "unsupported bounded VHDL attribute '" + attribute.text + "'");
            }
            if (designator == "ascending" || designator == "reverse_range"
                || designator == "image" || designator == "value"
                || designator == "delayed" || designator == "quiet"
                || designator == "transaction" || designator == "active"
                || designator == "last_event" || designator == "last_active"
                || designator == "driving" || designator == "driving_value"
                || designator == "simple_name" || designator == "instance_name"
                || designator == "path_name") {
                require_vhdl_standard(
                    attribute, VhdlStandard::Vhdl1993,
                    "the predefined '" + designator + " attribute",
                    "select VHDL-1993 or later");
            }
            if (designator == "subtype" || designator == "element") {
                require_vhdl_standard(
                    attribute, VhdlStandard::Vhdl2008,
                    "the predefined '" + designator + " attribute",
                    "select VHDL-2008");
            }
            std::vector<Expression> operands {
                Expression { ExpressionKind::Identifier, canonical, { }, name.span }
            };
            if (match(TokenKind::LeftParen)) {
                operands.push_back(parse_expression());
                expect(TokenKind::RightParen, "')' after attribute argument",
                    "FSIM-VHDL-PARSE-120");
            }
            const bool requires_argument = designator == "pos" || designator == "val" || designator == "succ" || designator == "pred" || designator == "leftof" || designator == "rightof" || designator == "image"
                || designator == "value";
            if (requires_argument && operands.size() != 2) {
                error(attribute, "FSIM-VHDL-PARSE-142",
                    "enumeration attribute '" + attribute.text + "' requires one parenthesized argument");
            }
            return Expression { ExpressionKind::Call, "'" + designator,
                std::move(operands), cover(name.span, previous().span) };
        }
        if (match(TokenKind::LeftParen)) {
            const auto base = Expression { ExpressionKind::Identifier, canonical, { }, name.span };
            std::vector<Expression> arguments;
            std::vector<std::string> argument_names;
            bool saw_named = false;
            if (!at(TokenKind::RightParen)) {
                do {
                    std::string argument_name;
                    if (at(TokenKind::Identifier) && at(TokenKind::Arrow, 1)) {
                        saw_named = true;
                        argument_name = vhdl_name(advance().text);
                        advance();
                    } else if (saw_named) {
                        error(current(), "FSIM-VHDL-SEM-073",
                            "a positional function-call actual cannot follow a "
                            "named actual");
                    }
                    auto argument = parse_expression();
                    if (argument_name.empty() && (keyword("downto", 0, true) || keyword("to", 0, true))) {
                        const auto first_span = argument.span;
                        advance();
                        const auto direction = previous();
                        auto second = parse_expression();
                        if (arguments.empty() && at(TokenKind::RightParen)) {
                            expect(TokenKind::RightParen, "')' after slice",
                                "FSIM-VHDL-PARSE-033");
                            return Expression { ExpressionKind::Slice,
                                detail::ascii_lower(direction.text),
                                { base, std::move(argument), std::move(second) },
                                cover(name.span, previous().span) };
                        }
                        argument = Expression { ExpressionKind::Binary,
                            detail::ascii_lower(direction.text),
                            { std::move(argument), std::move(second) },
                            cover(first_span, previous().span) };
                    }
                    arguments.push_back(std::move(argument));
                    argument_names.push_back(std::move(argument_name));
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RightParen, "')' after arguments",
                "FSIM-VHDL-PARSE-033");
            Expression call { ExpressionKind::Call, std::move(canonical),
                std::move(arguments), cover(name.span, previous().span) };
            if (saw_named) {
                call.call_argument_names = std::move(argument_names);
            }
            for (;;) {
                if (match(TokenKind::LeftParen)) {
                    require_vhdl_standard(
                        previous(), VhdlStandard::Vhdl2008,
                        "indexing or slicing a function-call result",
                        "select VHDL-2008 or assign the function result to an "
                        "intermediate object");
                    auto first = parse_expression();
                    if (match_keyword("downto", true) || match_keyword("to", true)) {
                        const auto direction = previous();
                        auto second = parse_expression();
                        expect(TokenKind::RightParen, "')' after chained slice",
                            "FSIM-VHDL-PARSE-033");
                        call = Expression {
                            ExpressionKind::Slice,
                            detail::ascii_lower(direction.text),
                            { std::move(call), std::move(first), std::move(second) },
                            cover(name.span, previous().span)
                        };
                    } else {
                        expect(TokenKind::RightParen, "')' after chained index",
                            "FSIM-VHDL-PARSE-033");
                        call = Expression { ExpressionKind::Index,
                            "index",
                            { std::move(call), std::move(first) },
                            cover(name.span, previous().span) };
                    }
                    continue;
                }
                if (!match(TokenKind::Dot)) {
                    break;
                }
                require_vhdl_standard(
                    previous(), VhdlStandard::Vhdl2008,
                    "selecting an element of a function-call result",
                    "select VHDL-2008 or assign the function result to an "
                    "intermediate object");
                const auto member = expect_identifier("selected record element");
                call = Expression { ExpressionKind::Call,
                    "@vhdl-member:" + vhdl_name(member.text),
                    { std::move(call) },
                    cover(name.span, member.span) };
            }
            return call;
        }
        return Expression {
            ExpressionKind::Identifier, std::move(canonical), { }, name.span
        };
    }
    if (match(TokenKind::LeftParen)) {
        const auto open = previous();
        auto first = parse_expression();
        if (!at(TokenKind::Arrow) && !at(TokenKind::Comma) && !at(TokenKind::Pipe) && !keyword("to", 0, true) && !keyword("downto", 0, true)) {
            expect(TokenKind::RightParen, "')' after expression",
                "FSIM-VHDL-PARSE-034");
            first.span = span_from(open, previous());
            return first;
        }

        Expression aggregate;
        aggregate.kind = ExpressionKind::Aggregate;
        bool named_association = false;
        bool saw_others = false;
        const auto append_association = [&](Expression head) {
            const auto choice_with_optional_range = [&]() {
                const auto left_span = head.span;
                if (!match_keyword("to", true) && !match_keyword("downto", true)) {
                    return head;
                }
                const auto direction = previous();
                Expression right;
                if (at(TokenKind::Arrow) || at(TokenKind::Pipe) || at(TokenKind::Comma) || at(TokenKind::RightParen) || at_end()) {
                    error(current(), "FSIM-VHDL-PARSE-149",
                        "expected a right bound in aggregate range "
                        "choice");
                    right = Expression { ExpressionKind::Invalid, { }, { }, current().span };
                } else {
                    right = parse_expression();
                }
                return Expression { ExpressionKind::Binary,
                    detail::ascii_lower(direction.text),
                    { std::move(head), std::move(right) },
                    cover(left_span, previous().span) };
            };
            std::vector<Expression> choices;
            choices.push_back(choice_with_optional_range());
            while (match(TokenKind::Pipe)) {
                if (at(TokenKind::Arrow) || at(TokenKind::Comma) || at(TokenKind::RightParen) || at_end()) {
                    error(current(), "FSIM-VHDL-PARSE-150",
                        "expected an aggregate choice after '|'");
                    break;
                }
                head = parse_expression();
                choices.push_back(choice_with_optional_range());
            }
            if (match(TokenKind::Arrow)) {
                named_association = true;
                std::size_t others_count = 0;
                for (auto& choice_expression : choices) {
                    if (choice_expression.kind == ExpressionKind::BooleanLiteral || choice_expression.kind == ExpressionKind::LogicLiteral || choice_expression.kind == ExpressionKind::StringLiteral || choice_expression.kind == ExpressionKind::Aggregate) {
                        error(previous(), "FSIM-VHDL-PARSE-132",
                            "an aggregate choice must be a record element, "
                            "others, or a locally static integer expression or "
                            "range");
                    }
                    if (choice_expression.kind == ExpressionKind::Identifier) {
                        choice_expression.text = vhdl_name(choice_expression.text);
                        if (choice_expression.text == "others") {
                            ++others_count;
                        }
                    }
                }
                if (others_count != 0) {
                    if (choices.size() != 1) {
                        error(previous(), "FSIM-VHDL-SEM-041",
                            "others must be the only choice in its aggregate "
                            "association");
                    }
                    if (saw_others) {
                        error(previous(), "FSIM-VHDL-SEM-039",
                            "an aggregate has more than one others "
                            "association");
                    }
                    saw_others = true;
                }
                std::string choice = "@array";
                if (choices.size() == 1 && choices.front().kind == ExpressionKind::Identifier) {
                    choice = choices.front().text;
                }
                Expression value;
                if (at(TokenKind::Comma) || at(TokenKind::RightParen) || at_end()) {
                    error(current(), "FSIM-VHDL-PARSE-133",
                        "expected a value after aggregate =>");
                    value = Expression { ExpressionKind::Invalid, { }, { }, current().span };
                } else {
                    value = parse_expression();
                }
                aggregate.aggregate_choices.push_back(std::move(choice));
                aggregate.aggregate_choice_expressions.push_back(std::move(choices));
                aggregate.operands.push_back(std::move(value));
                return;
            }
            if (choices.size() != 1 || (choices.front().kind == ExpressionKind::Binary && (choices.front().text == "to" || choices.front().text == "downto"))) {
                error(current(), "FSIM-VHDL-PARSE-151",
                    "an aggregate choice list or range must be followed "
                    "by =>");
            }
            if (named_association) {
                error(current(), "FSIM-VHDL-SEM-038",
                    "a positional aggregate association cannot "
                    "follow a named association");
            }
            aggregate.aggregate_choices.emplace_back();
            aggregate.aggregate_choice_expressions.emplace_back();
            aggregate.operands.push_back(std::move(choices.front()));
        };
        append_association(std::move(first));
        while (match(TokenKind::Comma)) {
            if (saw_others) {
                error(previous(), "FSIM-VHDL-SEM-039",
                    "the others aggregate association must be last");
            }
            if (at(TokenKind::RightParen) || at_end()) {
                error(current(), "FSIM-VHDL-PARSE-133",
                    "expected an aggregate association after ','");
                break;
            }
            append_association(parse_expression());
        }
        expect(TokenKind::RightParen, "')' after expression",
            "FSIM-VHDL-PARSE-034");
        aggregate.span = span_from(open, previous());
        return aggregate;
    }

    const auto invalid = advance();
    error(invalid, "FSIM-VHDL-PARSE-035", "expected expression");
    return Expression { ExpressionKind::Invalid, invalid.text, { }, invalid.span };
}

void VhdlParser::infer_process_edge(Process& process)
{
    if (process.statements.size() != 1 || process.statements.front().kind != StatementKind::If || !process.statements.front().else_statements.empty()) {
        return;
    }
    const auto& condition = process.statements.front().condition;
    if (condition.kind != ExpressionKind::Call || condition.operands.size() != 1 || condition.operands.front().kind != ExpressionKind::Identifier) {
        return;
    }
    EdgeKind edge = EdgeKind::Any;
    if (condition.text == "rising_edge") {
        edge = EdgeKind::Positive;
    } else if (condition.text == "falling_edge") {
        edge = EdgeKind::Negative;
    } else {
        return;
    }
    const auto& signal = condition.operands.front().text;
    for (auto& sensitivity : process.sensitivities) {
        if (sensitivity.signal == signal) {
            sensitivity.edge = edge;
        }
    }
}

} // namespace fsim::frontend
