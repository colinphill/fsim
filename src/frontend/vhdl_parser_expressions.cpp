// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

#include <bit>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <new>

namespace fsim::frontend {

void VhdlParser::note_vhdl_access_object(
    const std::string& name, const Type& type) {
  vhdl_object_classes_.insert_or_assign(name, "variable");
  auto mark = std::string_view { type.spelling };
  if (const auto dot = mark.find_last_of('.'); dot != std::string_view::npos) {
    mark.remove_prefix(dot + 1U);
  }
  if (vhdl_access_type_names_.contains(vhdl_name(std::string { mark }))) {
    vhdl_access_object_names_.insert(name);
  } else {
    vhdl_access_object_names_.erase(name);
  }
  // An object declared with a unit's name hides that unit within its
  // declarative region (IEEE 1076-2008 12.3).
  if (vhdl_physical_unit_names_.contains(name)) {
    vhdl_unit_hiding_object_names_.insert(name);
  }
}
namespace {

// The suffix `.all` of any prefix denotes the designated object (IEEE
// 1076-2008 8.3); any other suffix selects a record element.
std::string vhdl_selection_suffix(const std::string_view spelling)
{
    auto name = vhdl_name(spelling);
    return name == "all" ? std::string { "@vhdl-dereference" }
                         : "@vhdl-member:" + name;
}

// A based real literal (IEEE 1076-2008 15.5.3): base # digits . digits #
// with an exponent of the base, as a decimal spelling.
std::optional<std::string> vhdl_based_real_decimal(const std::string_view spelling)
{
    {
        const auto first = spelling.find('#');
        if (first == std::string_view::npos) {
            return std::nullopt;
        }
        const auto second = spelling.find('#', first + 1U);
        if (second == std::string_view::npos) {
            return std::nullopt;
        }
        unsigned base { };
        for (const auto character : spelling.substr(0U, first)) {
            if (character == '_') {
                continue;
            }
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            base = base * 10U + static_cast<unsigned>(character - '0');
        }
        if (base < 2U || base > 16U) {
            return std::nullopt;
        }
        long double value { };
        long double scale { 1.0L };
        bool fraction { };
        bool digits { };
        for (const auto character : spelling.substr(first + 1U, second - first - 1U)) {
            if (character == '_') {
                continue;
            }
            if (character == '.') {
                if (fraction) {
                    return std::nullopt;
                }
                fraction = true;
                continue;
            }
            const auto lowered = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
            const unsigned digit = lowered >= '0' && lowered <= '9'
                ? static_cast<unsigned>(lowered - '0')
                : lowered >= 'a' && lowered <= 'f'
                ? static_cast<unsigned>(lowered - 'a') + 10U
                : 16U;
            if (digit >= base) {
                return std::nullopt;
            }
            digits = true;
            if (fraction) {
                scale /= static_cast<long double>(base);
                value += static_cast<long double>(digit) * scale;
            } else {
                value = value * static_cast<long double>(base)
                    + static_cast<long double>(digit);
            }
        }
        if (!digits || !fraction) {
            return std::nullopt;
        }
        auto rest = spelling.substr(second + 1U);
        if (!rest.empty()) {
            if (rest.front() != 'e' && rest.front() != 'E') {
                return std::nullopt;
            }
            rest.remove_prefix(1U);
            if (!rest.empty() && rest.front() == '+') {
                rest.remove_prefix(1U);
            }
            long long exponent { };
            const auto parsed = std::from_chars(rest.data(), rest.data() + rest.size(), exponent);
            if (rest.empty() || parsed.ec != std::errc { }
                || parsed.ptr != rest.data() + rest.size()) {
                return std::nullopt;
            }
            value *= std::pow(static_cast<long double>(base),
                static_cast<long double>(exponent));
        }
        std::array<char, 64> buffer { };
        const auto written = std::snprintf(buffer.data(), buffer.size(), "%.17Le", value);
        if (written <= 0 || static_cast<std::size_t>(written) >= buffer.size()) {
            return std::nullopt;
        }
        return std::string { buffer.data(), static_cast<std::size_t>(written) };
    }
}

std::optional<SystemVerilogDecimalLiteral> vhdl_real_payload(
    const std::string_view spelling)
{
    if (spelling.find('#') != std::string_view::npos) {
        const auto decimal = vhdl_based_real_decimal(spelling);
        return decimal ? vhdl_real_payload(*decimal) : std::nullopt;
    }
    std::string compact;
    compact.reserve(spelling.size());
    for (const auto character : spelling) {
        if (character != '_') {
            compact.push_back(character);
        }
    }
    const auto exponent_position = compact.find_first_of("eE");
    if (exponent_position != std::string::npos
        && compact.find_first_of("eE", exponent_position + 1U)
            != std::string::npos) {
        return std::nullopt;
    }
    const auto mantissa = std::string_view{compact}.substr(
        0, exponent_position);
    const auto point = mantissa.find('.');
    if (point == std::string_view::npos
        || point == 0U || point + 1U == mantissa.size()
        || mantissa.find('.', point + 1U) != std::string_view::npos) {
        return std::nullopt;
    }
    std::int64_t exponent{};
    if (exponent_position != std::string::npos) {
        auto text = std::string_view{compact}.substr(
            exponent_position + 1U);
        if (!text.empty() && text.front() == '+') {
            text.remove_prefix(1U);
        }
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), exponent);
        if (text.empty() || parsed.ec != std::errc{}
            || parsed.ptr != text.data() + text.size()) {
            return std::nullopt;
        }
    }
    const auto fractional_digits = mantissa.size() - point - 1U;
    if (fractional_digits
        > static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max())
        || exponent < std::numeric_limits<std::int64_t>::min()
                + static_cast<std::int64_t>(fractional_digits)) {
        return std::nullopt;
    }
    exponent -= static_cast<std::int64_t>(fractional_digits);
    std::string digits;
    digits.reserve(mantissa.size() - 1U);
    digits.append(mantissa.substr(0, point));
    digits.append(mantissa.substr(point + 1U));
    const auto first = digits.find_first_not_of('0');
    if (first == std::string::npos) {
        return SystemVerilogDecimalLiteral{
            SystemVerilogDecimalLiteralKind::Real, "0", 0, {}};
    }
    digits.erase(0, first);
    while (digits.size() > 1U && digits.back() == '0') {
        if (exponent == std::numeric_limits<std::int64_t>::max()) {
            return std::nullopt;
        }
        digits.pop_back();
        ++exponent;
    }
    return SystemVerilogDecimalLiteral{
        SystemVerilogDecimalLiteralKind::Real,
        std::move(digits), exponent, {}};
}

} // namespace

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
    // Before VHDL-2008 a bit string literal has at least one digit
    // (IEEE 1076-1993 13.7).
    if (digits.empty() && vhdl_standard_ < VhdlStandard::Vhdl2008) {
        error(digits_token, "FSIM-VHDL-PARSE-298",
            "a VHDL bit string literal requires at least one digit before "
            "VHDL-2008");
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
                // Any other graphic character stands for itself in each
                // position (IEEE 1076-2008 15.8, `UO"2C"` is "010CCC").
                if (!std::isgraph(static_cast<unsigned char>(raw))) {
                    malformed = true;
                    return;
                }
                for (unsigned index = 0; index < radix_width; ++index) {
                    append_bit(raw);
                }
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
            while (limbs.size() > 1U && limbs.back() == 0U) {
                limbs.pop_back();
            }
            // An empty literal (`D""`) has no bits (IEEE 1076-2008 15.8).
            if (limbs.size() == 1U && limbs.front() == 0U) {
                if (value_character_count != 0) {
                    append_bit('0');
                }
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

// The selectors that may follow a function call or an attribute name,
// which are names themselves (IEEE 1076-2008 8.1): an index or slice, a
// record element, and a value attribute.
Expression VhdlParser::parse_vhdl_name_suffixes(
    Expression prefix, const SourceSpan& start)
{
    for (;;) {
        if (match(TokenKind::LeftParen)) {
            prefix = parse_index_or_slice_suffix(std::move(prefix), start,
                "')' after chained slice", "')' after chained index",
                "FSIM-VHDL-PARSE-033");
            continue;
        }
        if (at(TokenKind::Dot) && at(TokenKind::Identifier, 1)) {
            advance();
            const auto member = advance();
            prefix = Expression { ExpressionKind::Call,
                vhdl_selection_suffix(member.text),
                { std::move(prefix) }, cover(start, member.span) };
            continue;
        }
        if (at(TokenKind::Apostrophe) && at(TokenKind::Identifier, 1)) {
            advance();
            const auto attribute = advance();
            const auto designator = detail::ascii_lower(attribute.text);
            std::vector<Expression> operands { std::move(prefix) };
            const bool dimension_argument = designator == "left"
                || designator == "right" || designator == "high"
                || designator == "low" || designator == "length"
                || designator == "range" || designator == "reverse_range"
                || designator == "ascending";
            if (dimension_argument && match(TokenKind::LeftParen)) {
                operands.push_back(parse_expression());
                expect(TokenKind::RightParen, "')' after attribute argument",
                    "FSIM-VHDL-PARSE-120");
            }
            prefix = Expression { ExpressionKind::Call, "'" + designator,
                std::move(operands), cover(start, previous().span) };
            continue;
        }
        return prefix;
    }
}

Expression VhdlParser::parse_index_or_slice_suffix(Expression prefix,
    const SourceSpan& start, const std::string_view slice_close,
    const std::string_view index_close, const std::string_view code,
    const bool index_list)
{
    auto first = parse_expression();
    if (match_keyword("downto", true) || match_keyword("to", true)) {
        const auto direction = previous();
        auto last = parse_expression();
        expect(TokenKind::RightParen, slice_close,
            std::string { code });
        return Expression { ExpressionKind::Slice,
            detail::ascii_lower(direction.text),
            { std::move(prefix), std::move(first), std::move(last) },
            cover(start, previous().span) };
    }
    std::vector<Expression> operands;
    operands.push_back(std::move(prefix));
    operands.push_back(std::move(first));
    while (index_list && match(TokenKind::Comma)) {
        operands.push_back(parse_expression());
    }
    expect(TokenKind::RightParen, index_close,
        std::string { code });
    return Expression { ExpressionKind::Index, "index",
        std::move(operands), cover(start, previous().span) };
}

Expression VhdlParser::parse_expression(
    int minimum_precedence, bool allow_conditional)
{
    Expression left = parse_unary();
    // IEEE 1076-2008 9.1: a relation has at most one relational operator and
    // a shift expression one shift operator; a sequence of logical operators
    // repeats one AND, OR, XOR or XNOR, and NAND or NOR takes two relations.
    std::string previous_logical;
    bool previous_relational { };
    bool previous_shift { };
    for (;;) {
        const auto operation = binary_operation();
        if (!operation || operation->precedence < minimum_precedence) {
            break;
        }
        if (operation->precedence == 1) {
            if (!previous_logical.empty()
                && (operation->name != previous_logical
                    || operation->name == "nand" || operation->name == "nor")) {
                error(current(), "FSIM-VHDL-PARSE-301",
                    "VHDL logical operators of different kinds, or a repeated NAND "
                    "or NOR, require parentheses");
            }
            previous_logical = operation->name;
        } else if (operation->precedence == 3) {
            if (previous_relational) {
                error(current(), "FSIM-VHDL-PARSE-301",
                    "a VHDL relation has at most one relational operator");
            }
            previous_relational = true;
        } else if (operation->precedence == 4) {
            if (previous_shift) {
                error(current(), "FSIM-VHDL-PARSE-301",
                    "a VHDL shift expression has at most one shift operator");
            }
            previous_shift = true;
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
        if (operation->name.starts_with('?')) {
            require_vhdl_standard(
                operator_token, VhdlStandard::Vhdl2008,
                "the '" + operation->name + "' matching operator",
                "select VHDL-2008 or use an ordinary equality operator");
        }
        if (operation->name == "**" && (at(TokenKind::Plus) || at(TokenKind::Minus))) {
            error(current(), "FSIM-VHDL-PARSE-114",
                "a signed VHDL exponent must be parenthesized");
        }
        // A sign begins only a simple expression (IEEE 1076-2008 9.1), so
        // the right operand of an adding or multiplying operator is unsigned.
        if ((operation->precedence == 5 || operation->precedence == 6)
            && (at(TokenKind::Plus) || at(TokenKind::Minus))) {
            error(current(), "FSIM-VHDL-PARSE-297",
                "a signed operand of a VHDL adding or multiplying operator "
                "must be parenthesized");
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
            previous(), VhdlStandard::Vhdl2019, "a conditional expression",
            "select VHDL-2019 or rewrite it as a conditional statement");
        const auto begin_span = left.span;
        auto condition = parse_expression();
        expect_keyword("else", true, "FSIM-VHDL-PARSE-115");
        auto when_false = parse_expression();
        const auto span = cover(begin_span, when_false.span);
        left = Expression {
            ExpressionKind::Conditional,
            "when",
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
    // VHDL gives every logical operator one precedence; an expression does
    // not mix them without parentheses (IEEE 1076-2008 9.1).
    if (keyword("and", 0, true) || keyword("nand", 0, true)) {
        return BinaryOperation { 1, detail::ascii_lower(current().text) };
    }
    if (at(TokenKind::Assign) || (at(TokenKind::NotEqual) && current().text == "/=") || at(TokenKind::Less) || at(TokenKind::LessEqual) || at(TokenKind::Greater) || at(TokenKind::GreaterEqual)) {
        return BinaryOperation { 3, current().text };
    }
    if (at(TokenKind::Question) && (at(TokenKind::Assign, 1) || (at(TokenKind::NotEqual, 1) && current(1).text == "/="))) {
        return BinaryOperation { 3, at(TokenKind::Assign, 1) ? "?=" : "?/=", 2 };
    }
    // Matching ordering operators (IEEE 1076-2008 9.2.3).
    if (at(TokenKind::Question) && (at(TokenKind::Less, 1) || at(TokenKind::LessEqual, 1) || at(TokenKind::Greater, 1) || at(TokenKind::GreaterEqual, 1))) {
        return BinaryOperation { 3, "?" + current(1).text, 2 };
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
        if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
            error(current(), "FSIM-VHDL-PARSE-297",
                "a signed operand of a VHDL sign must be parenthesized");
        }
        Expression operand = parse_expression(6);
        return Expression { ExpressionKind::Unary,
            detail::ascii_lower(operation.text),
            { std::move(operand) },
            cover(operation.span, operand.span) };
    }
    if (keyword("not", 0, true) || keyword("abs", 0, true)) {
        const auto operation = advance();
        if (at(TokenKind::Plus) || at(TokenKind::Minus)) {
            error(current(), "FSIM-VHDL-PARSE-297",
                "a signed operand of a VHDL '"
                    + detail::ascii_lower(operation.text)
                    + "' operator must be parenthesized");
        }
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
    auto primary = parse_primary();
    // An attribute name's prefix may be any name: an indexed or selected
    // name, a dereference, or another attribute (IEEE 1076-2008 8.6).
    // A qualified expression or an aggregate is not a name, so it cannot be
    // an attribute prefix (8.6).
    while (at(TokenKind::Apostrophe) && at(TokenKind::Identifier, 1)
        && ((primary.kind == ExpressionKind::Call
                && !primary.text.starts_with("@vhdl-qualified:"))
            || primary.kind == ExpressionKind::Index
            || primary.kind == ExpressionKind::Slice)) {
        advance();
        const auto attribute = advance();
        std::vector<Expression> operands;
        operands.push_back(std::move(primary));
        if (match(TokenKind::LeftParen)) {
            operands.push_back(parse_expression());
            expect(TokenKind::RightParen, "')' after attribute argument",
                "FSIM-VHDL-PARSE-120");
        }
        const auto span = cover(operands.front().span, previous().span);
        primary = Expression { ExpressionKind::Call,
            "'" + vhdl_name(attribute.text), std::move(operands), span };
    }
    return primary;
}

Expression VhdlParser::parse_primary()
{
    // A function call whose name is an operator symbol, optionally selected
    // ("and"(a, b), STD.STANDARD."<"(a, b)), is the operator applied to its
    // actuals (IEEE 1076-2008 4.5.2, 9.3.4): overload resolution is that of
    // the operator.
    {
        std::size_t lookahead { };
        while (at(TokenKind::Identifier, lookahead)
            && at(TokenKind::Dot, lookahead + 1U)) {
            lookahead += 2U;
        }
        const auto symbol = at(TokenKind::StringLiteral, lookahead)
                && at(TokenKind::LeftParen, lookahead + 1U)
            ? detail::ascii_lower(
                  string_literal_text(current(lookahead)))
            : std::string { };
        static constexpr std::array<std::string_view, 27> binary_symbols {
            "and", "or", "nand", "nor", "xor", "xnor", "=", "/=", "<",
            "<=", ">", ">=", "sll", "srl", "sla", "sra", "rol", "ror", "+",
            "-", "&", "*", "/", "mod", "rem", "**", "?="
        };
        const bool binary_symbol
            = std::ranges::find(binary_symbols, symbol)
            != binary_symbols.end();
        const bool unary_symbol = symbol == "not" || symbol == "abs";
        if (binary_symbol || unary_symbol) {
            const auto start = current();
            std::optional<std::string> selected_operator_prefix;
            if (lookahead != 0U) {
                std::string prefix;
                for (std::size_t index { }; index < lookahead; index += 2U) {
                    if (!prefix.empty()) {
                        prefix.push_back('.');
                    }
                    prefix += current(index).text;
                }
                selected_operator_prefix = std::move(prefix);
            }
            for (std::size_t index { }; index <= lookahead; ++index) {
                advance();
            }
            expect(TokenKind::LeftParen, "'(' after operator symbol",
                "FSIM-VHDL-PARSE-034");
            std::vector<Expression> actuals;
            do {
                if (at(TokenKind::Identifier) && at(TokenKind::Arrow, 1)) {
                    advance();
                    advance();
                }
                actuals.push_back(parse_expression());
            } while (match(TokenKind::Comma));
            expect(TokenKind::RightParen,
                "')' after operator symbol actuals", "FSIM-VHDL-PARSE-034");
            const auto span = cover(start.span, previous().span);
            if (actuals.size() == 2U && binary_symbol) {
                auto expression = Expression { ExpressionKind::Binary, symbol,
                    std::move(actuals), span };
                expression.selected_operator_prefix
                    = std::move(selected_operator_prefix);
                expression.vhdl_operator_symbol_call = true;
                return parse_vhdl_name_suffixes(
                    std::move(expression), start.span);
            }
            if (actuals.size() == 1U
                && (unary_symbol || symbol == "+" || symbol == "-"
                    || symbol == "and" || symbol == "or"
                    || symbol == "nand" || symbol == "nor"
                    || symbol == "xor" || symbol == "xnor")) {
                auto expression = Expression { ExpressionKind::Unary, symbol,
                    std::move(actuals), span };
                expression.selected_operator_prefix
                    = std::move(selected_operator_prefix);
                expression.vhdl_operator_symbol_call = true;
                return parse_vhdl_name_suffixes(
                    std::move(expression), start.span);
            }
            error(start, "FSIM-VHDL-PARSE-300",
                "an operator symbol function call requires the operator's "
                "number of actuals");
            return Expression { ExpressionKind::Unary, symbol,
                std::move(actuals), span };
        }
    }
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
        // The base of a based literal is 2 to 16 (IEEE 1076-2008 15.5.3).
        if (const auto hash = token.text.find('#'); hash != std::string::npos) {
            std::string base;
            for (const auto character : token.text.substr(0U, hash)) {
                if (character != '_') {
                    base.push_back(character);
                }
            }
            const auto value = detail::decimal_u64(base);
            if (!value || *value < 2U || *value > 16U) {
                error(token, "FSIM-VHDL-PARSE-299",
                    "the base of a VHDL based literal must be 2 to 16");
            }
        }
        Expression literal {
            ExpressionKind::IntegerLiteral, token.text, { }, token.span
        };
        if (token.text.find('.') != std::string::npos) {
            literal.systemverilog_decimal_literal =
                vhdl_real_payload(token.text);
            if (!literal.systemverilog_decimal_literal) {
                error(token, "FSIM-VHDL-PARSE-296",
                    "malformed or unsupported VHDL real literal '"
                        + token.text + "'");
            } else {
                literal.systemverilog_scalar_kind =
                    SystemVerilogScalarKind::Real;
                if (token.text.find('#') != std::string::npos) {
                    literal.text = vhdl_based_real_decimal(token.text).value_or(literal.text);
                }
            }
        }
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
        // An expanded name whose prefix is an operator symbol names a
        // declaration of that operator function (`"+".d`, IEEE 1076-2008
        // 8.3).
        if (at(TokenKind::Dot) && at(TokenKind::Identifier, 1)) {
            advance();
            const auto part = advance();
            // Operator functions are declared under their symbol without
            // quotes.
            auto text = string_literal_text(token);
            std::ranges::transform(text, text.begin(),
                [](const unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
            return Expression { ExpressionKind::Identifier,
                text + "." + vhdl_name(part.text), { },
                cover(token.span, part.span) };
        }
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
        // A signal or constant of another design entity or block (IEEE
        // 1076-2008 8.7); an elaborated constant is read through its static
        // value.
        if (!match_keyword("signal", true)
            && !match_keyword("constant", true)) {
            error(current(), "FSIM-VHDL-PARSE-276",
                "bounded external names require the signal or constant "
                "object class");
            if (at(TokenKind::Identifier)) {
                advance();
            }
        }

        // An absolute pathname starts with `.`, a relative one may go up
        // with `^.`; a generate statement label takes its literal index
        // (IEEE 1076-2008 8.7).
        std::string target;
        if (match(TokenKind::Dot)) {
            target = ".";
        }
        while (at(TokenKind::Caret) && at(TokenKind::Dot, 1U)) {
            advance();
            advance();
            target += "^.";
        }
        const auto first = expect_identifier("external signal path");
        // A generate index that is not a literal (`g(N)`) is a globally
        // static expression; the path keeps a `(#k)` placeholder for the
        // k-th index expression, evaluated at elaboration.
        std::vector<Expression> path_indices;
        const auto path_element = [&](const Token& part) {
            target += vhdl_name(part.text);
            if (at(TokenKind::LeftParen) && at(TokenKind::Number, 1U)
                && at(TokenKind::RightParen, 2U)) {
                advance();
                target += "(" + advance().text + ")";
                advance();
            } else if (at(TokenKind::LeftParen)) {
                advance();
                target += "(#" + std::to_string(path_indices.size()) + ")";
                path_indices.push_back(parse_expression(0, false));
                expect(TokenKind::RightParen,
                    "')' after external path generate index",
                    "FSIM-VHDL-PARSE-277");
            }
        };
        path_element(first);
        while (match(TokenKind::Dot)) {
            const auto part = expect_identifier("external signal path element");
            target += '.';
            path_element(part);
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
        Expression external {
            ExpressionKind::Call,
            "@vhdl-external",
            { Expression { ExpressionKind::Identifier, target,
                  std::move(path_indices), first.span },
                std::move(subtype) },
            cover(start.span, previous().span)
        };
        // An external name is a name; an element or record element of the
        // object it denotes is selected by suffix (IEEE 1076-2008 8.3, 8.7).
        if ((at(TokenKind::Dot) && at(TokenKind::Identifier, 1))
            || at(TokenKind::LeftParen)) {
            return parse_vhdl_name_suffixes(std::move(external), start.span);
        }
        return external;
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
        const auto name_index = index_;
        const auto name = advance();
        std::string canonical = vhdl_name(name.text);
        // `f generic map (...) (args)` calls an implicit instance of the
        // generic function `f` (IEEE 1076-2019 9.3.4).
        canonical = parse_vhdl_call_generic_map(
            name, std::move(canonical), false);
        // An attribute of an external-name alias other than a name
        // attribute applies to the object it denotes (IEEE 1076-2008 6.6.2).
        const bool object_attribute = at(TokenKind::Apostrophe)
            && at(TokenKind::Identifier, 1)
            && vhdl_name(current(1).text) != "simple_name"
            && vhdl_name(current(1).text) != "path_name"
            && vhdl_name(current(1).text) != "instance_name"
            && vhdl_name(current(1).text) != "stable"
            && vhdl_name(current(1).text) != "quiet"
            && vhdl_name(current(1).text) != "delayed";
        if (const auto external = vhdl_external_aliases_.find(canonical);
            external != vhdl_external_aliases_.end()
            && (!at(TokenKind::Apostrophe) || object_attribute)
            && (!at(TokenKind::Dot) || at(TokenKind::Identifier, 1))) {
            auto expression = external->second;
            expression.span = name.span;
            // An element or record element of the object an external-name
            // alias denotes is selected by suffix (IEEE 1076-2008 6.6.2,
            // 8.3, 8.7).
            if (at(TokenKind::Dot) || at(TokenKind::LeftParen)
                || object_attribute) {
                return parse_vhdl_name_suffixes(
                    std::move(expression), name.span);
            }
            return expression;
        }
        std::string selected_spelling { name.text };
        // Keep the source spelling of a selected name only when an extended
        // identifier makes it differ from the canonical text.
        const auto attach_selected_spelling
            = [&selected_spelling](Expression& expression) {
                  if (selected_spelling.find('\\') != std::string::npos) {
                      expression.vhdl_selected_name_spelling
                          = selected_spelling;
                  }
              };
        Expression selected { ExpressionKind::Identifier, canonical, { }, name.span };
        attach_selected_spelling(selected);
        // The result of a parameterless function call is the prefix of a
        // selected record element (IEEE 1076-2008 8.3).
        if (vhdl_parameterless_function_names_.contains(canonical)
            && !vhdl_object_classes_.contains(canonical)
            && std::ranges::find(vhdl_enclosing_function_names_, canonical)
                == vhdl_enclosing_function_names_.end()
            && at(TokenKind::Dot) && at(TokenKind::Identifier, 1)
            && vhdl_name(current(1).text) != "all") {
            return parse_vhdl_name_suffixes(std::move(selected), name.span);
        }
        bool saw_dereference = false;
        bool method_result = false;
        // An index or element selected through an access object reaches
        // the designated object (IEEE 1076-2008 8.3).
        // So does an array attribute of an access object (8.3, 16.2.3).
        const auto array_attribute = [&] {
            if (!at(TokenKind::Apostrophe) || !at(TokenKind::Identifier, 1)) {
                return false;
            }
            const auto attribute = vhdl_name(current(1).text);
            return attribute == "length" || attribute == "low"
                || attribute == "high" || attribute == "left"
                || attribute == "right" || attribute == "range"
                || attribute == "reverse_range" || attribute == "ascending";
        };
        if (vhdl_access_object_names_.contains(canonical)
            && (at(TokenKind::LeftParen) || array_attribute()
                || (at(TokenKind::Dot) && at(TokenKind::Identifier, 1)
                    && vhdl_name(current(1).text) != "all"))) {
            selected = Expression { ExpressionKind::Call,
                "@vhdl-dereference",
                { std::move(selected) },
                name.span };
            saw_dereference = true;
        }
        while (match(TokenKind::Dot)) {
            // A selected name's suffix can be a character literal, which is
            // case sensitive (IEEE 1076-2019 8.3).
            const bool character_part = at(TokenKind::CharacterLiteral);
            const auto part = character_part
                ? advance()
                : expect_identifier("selected name");
            const auto canonical_part = character_part
                ? part.text
                : vhdl_name(part.text);
            // A parenthesized range after `.all` is a slice, which stays an
            // explicit dereference.
            const auto parenthesized_range = [&] {
                int depth { };
                for (std::size_t offset { };; ++offset) {
                    const auto& token = current(offset);
                    if (token.kind == TokenKind::EndOfFile) {
                        return false;
                    }
                    if (token.kind == TokenKind::LeftParen) {
                        ++depth;
                    } else if (token.kind == TokenKind::RightParen) {
                        if (--depth == 0) {
                            return false;
                        }
                    } else if (depth == 1
                        && token.kind == TokenKind::Identifier
                        && (vhdl_name(token.text) == "to"
                            || vhdl_name(token.text) == "downto"
                            || vhdl_name(token.text) == "range")) {
                        return true;
                    }
                }
            };
            if (canonical_part == "all" && !saw_dereference
                && ((at(TokenKind::Apostrophe)
                        && at(TokenKind::Identifier, 1)
                        && vhdl_name(current(1).text) == "length")
                    || (at(TokenKind::LeftParen)
                        && !parenthesized_range()))) {
                // `l.all'length` and `l.all(i)` are the implicit
                // dereferences `l'length` and `l(i)` (IEEE 1076-2008 8.3).
                selected.text = canonical;
                selected.span = cover(name.span, part.span);
            } else if (canonical_part == "all") {
                // Only the name before the first dereference is spelled
                // out; `pp.all.all` dereferences the dereference.
                if (!saw_dereference) {
                    selected.text = canonical;
                }
                selected.span = cover(name.span, part.span);
                selected = Expression { ExpressionKind::Call,
                    "@vhdl-dereference",
                    { std::move(selected) },
                    cover(name.span, part.span) };
                saw_dereference = true;
            } else if (saw_dereference || method_result) {
                selected = Expression { ExpressionKind::Call,
                    "@vhdl-member:" + canonical_part,
                    { std::move(selected) },
                    cover(name.span, part.span) };
            } else {
                selected_spelling += '.';
                selected_spelling += part.text;
                canonical += '.';
                canonical += canonical_part;
                selected.text = canonical;
                selected.span = cover(name.span, part.span);
                attach_selected_spelling(selected);
                // The result of a parameterless method call on a protected
                // object prefixes a selected record element (8.3).
                if (const auto object = vhdl_object_classes_.find(
                        canonical.substr(0, canonical.find('.')));
                    canonical.find('.') == canonical.rfind('.')
                    && object != vhdl_object_classes_.end()
                    && object->second == "variable"
                    && vhdl_parameterless_function_names_.contains(
                        canonical_part)
                    && at(TokenKind::Dot) && at(TokenKind::Identifier, 1)
                    && vhdl_name(current(1).text) != "all") {
                    method_result = true;
                }
            }
        }
        if (const auto api = vhdl_simulator_api(selected.text);
            api != VhdlSimulatorApi::none) {
            require_vhdl_standard(
                name, vhdl_simulator_api_standard(api),
                "the STD.ENV simulator, data, and time interface",
                "select VHDL-2019 or remove the STD.ENV reference");
        }
        if (saw_dereference || method_result) {
            for (;;) {
                if (match(TokenKind::LeftParen)) {
                    selected = parse_index_or_slice_suffix(
                        std::move(selected), name.span,
                        "')' after dereferenced slice",
                        "')' after dereferenced index",
                        "FSIM-VHDL-PARSE-251");
                    continue;
                }
                if (!match(TokenKind::Dot)) {
                    break;
                }
                const auto member = expect_identifier("selected dereferenced element");
                selected = Expression { ExpressionKind::Call,
                    vhdl_selection_suffix(member.text),
                    { std::move(selected) },
                    cover(name.span, member.span) };
            }
            return selected;
        }
        // A signature after the prefix of an attribute name selects one of
        // several overloaded subprograms or enumeration literals (IEEE
        // 1076-2008 8.6); the attribute specification is found by the name.
        if (at(TokenKind::LeftBracket)) {
            std::size_t depth { };
            std::size_t offset { };
            for (; index_ + offset < tokens_.size(); ++offset) {
                const auto kind = current(offset).kind;
                if (kind == TokenKind::LeftBracket) {
                    ++depth;
                } else if (kind == TokenKind::RightBracket && --depth == 0U) {
                    break;
                } else if (kind == TokenKind::Semicolon) {
                    break;
                }
            }
            if (depth == 0U && at(TokenKind::Apostrophe, offset + 1U)
                && at(TokenKind::Identifier, offset + 2U)) {
                for (std::size_t skipped { }; skipped <= offset; ++skipped) {
                    advance();
                }
            }
        }
        if (match(TokenKind::Apostrophe)) {
            if (at(TokenKind::LeftParen)) {
                auto value = parse_primary();
                auto qualified = Expression { ExpressionKind::Call,
                    "@vhdl-qualified:" + canonical,
                    { std::move(value) },
                    cover(name.span, previous().span) };
                attach_selected_spelling(qualified);
                return qualified;
            }
            auto attribute = expect_identifier("attribute designator");
            auto designator = vhdl_name(attribute.text);
            // T'BASE denotes the base type of T and is allowed only as the
            // prefix of another attribute (IEEE 1076-2008 16.2.2).
            if (designator == "base") {
                const auto separator = canonical.find_last_of('.');
                const auto base = vhdl_base_type_mark(
                    separator == std::string::npos
                        ? canonical
                        : canonical.substr(separator + 1U));
                if (base && match(TokenKind::Apostrophe)
                    && at(TokenKind::Identifier)) {
                    canonical = *base;
                    selected_spelling = *base;
                    attribute = expect_identifier("attribute designator");
                    designator = vhdl_name(attribute.text);
                }
            }
            // `R'VALUE` of a range attribute R is R's range record: an
            // aggregate of R's bounds and direction (IEEE 1076-2019 16.2.6).
            if ((designator == "range" || designator == "reverse_range")
                && vhdl_standard_ >= VhdlStandard::Vhdl2019) {
                std::size_t offset { };
                if (at(TokenKind::LeftParen)) {
                    std::size_t depth { };
                    for (; index_ + offset < tokens_.size(); ++offset) {
                        if (at(TokenKind::LeftParen, offset)) {
                            ++depth;
                        } else if (at(TokenKind::RightParen, offset)
                            && --depth == 0U) {
                            ++offset;
                            break;
                        }
                    }
                }
                if (at(TokenKind::Apostrophe, offset)
                    && keyword("value", offset + 1U, true)
                    && !at(TokenKind::LeftParen, offset + 2U)) {
                    const auto make = [&](const TokenKind kind,
                                          std::string text) {
                        Token token;
                        token.kind = kind;
                        token.text = std::move(text);
                        token.span = attribute.span;
                        return token;
                    };
                    const auto prefix_end = index_ - 2U;
                    const std::vector<Token> prefix(
                        tokens_.begin()
                            + static_cast<std::ptrdiff_t>(name_index),
                        tokens_.begin()
                            + static_cast<std::ptrdiff_t>(prefix_end));
                    const std::vector<Token> dimension(
                        tokens_.begin() + static_cast<std::ptrdiff_t>(index_),
                        tokens_.begin()
                            + static_cast<std::ptrdiff_t>(index_ + offset));
                    const bool reverse = designator == "reverse_range";
                    std::vector<Token> replacement;
                    const auto attribute_of = [&](const std::string& which) {
                        replacement.insert(
                            replacement.end(), prefix.begin(), prefix.end());
                        replacement.push_back(make(TokenKind::Apostrophe, "'"));
                        replacement.push_back(make(TokenKind::Identifier, which));
                        replacement.insert(replacement.end(),
                            dimension.begin(), dimension.end());
                    };
                    replacement.push_back(make(TokenKind::LeftParen, "("));
                    replacement.push_back(make(TokenKind::Identifier, "left"));
                    replacement.push_back(make(TokenKind::Arrow, "=>"));
                    attribute_of(reverse ? "right" : "left");
                    replacement.push_back(make(TokenKind::Comma, ","));
                    replacement.push_back(make(TokenKind::Identifier, "right"));
                    replacement.push_back(make(TokenKind::Arrow, "=>"));
                    attribute_of(reverse ? "left" : "right");
                    replacement.push_back(make(TokenKind::Comma, ","));
                    replacement.push_back(
                        make(TokenKind::Identifier, "direction"));
                    replacement.push_back(make(TokenKind::Arrow, "=>"));
                    replacement.push_back(
                        make(TokenKind::Identifier, "range_direction"));
                    replacement.push_back(make(TokenKind::Apostrophe, "'"));
                    replacement.push_back(make(TokenKind::Identifier, "val"));
                    replacement.push_back(make(TokenKind::LeftParen, "("));
                    replacement.push_back(make(TokenKind::Identifier, "boolean"));
                    replacement.push_back(make(TokenKind::Apostrophe, "'"));
                    replacement.push_back(make(TokenKind::Identifier, "pos"));
                    replacement.push_back(make(TokenKind::LeftParen, "("));
                    if (!reverse) {
                        replacement.push_back(make(TokenKind::Identifier, "not"));
                    }
                    attribute_of("ascending");
                    replacement.push_back(make(TokenKind::RightParen, ")"));
                    replacement.push_back(make(TokenKind::RightParen, ")"));
                    replacement.push_back(make(TokenKind::RightParen, ")"));
                    const auto first = tokens_.begin()
                        + static_cast<std::ptrdiff_t>(name_index);
                    tokens_.erase(first,
                        tokens_.begin()
                            + static_cast<std::ptrdiff_t>(index_ + offset + 2U));
                    tokens_.insert(tokens_.begin()
                            + static_cast<std::ptrdiff_t>(name_index),
                        replacement.begin(), replacement.end());
                    index_ = name_index;
                    return parse_primary();
                }
            }
            static constexpr std::array<std::string_view, 36> supported_attributes {
                "left", "right", "low", "high",
                "length", "ascending", "event", "last_value",
                "last_event", "last_active", "stable", "quiet",
                "active", "transaction", "delayed", "driving",
                "driving_value", "pos", "val", "succ",
                "pred", "leftof", "rightof", "range",
                "reverse_range", "image", "value", "simple_name",
                "instance_name", "path_name", "subtype", "element",
                "index", "designated_subtype", "reflect", "converse"
            };
            if (std::ranges::find(supported_attributes, designator) == supported_attributes.end()) {
                // A user-defined attribute names the value of its
                // specification, a static expression (IEEE 1076-2008 6.7,
                // 7.2).
                if (const auto values = vhdl_attribute_values_.find(designator);
                    values != vhdl_attribute_values_.end()) {
                    const auto separator = canonical.find_last_of('.');
                    const auto entity = separator == std::string::npos
                        ? canonical
                        : canonical.substr(separator + 1U);
                    auto found = values->second.find(entity);
                    // An attribute of an object applies to its aliases.
                    auto specified_entity = entity;
                    for (auto alias = vhdl_object_alias_targets_.find(entity);
                         found == values->second.end()
                         && alias != vhdl_object_alias_targets_.end()
                         && alias->second != alias->first;
                         alias = vhdl_object_alias_targets_.find(alias->second)) {
                        found = values->second.find(alias->second);
                        if (found != values->second.end()) {
                            specified_entity = alias->second;
                        }
                    }
                    if (const auto object_class
                        = vhdl_object_classes_.find(entity);
                        found == values->second.end()
                        && object_class != vhdl_object_classes_.end()) {
                        found = values->second.find(
                            "others:" + object_class->second);
                        if (found == values->second.end()) {
                            found = values->second.find(
                                "all:" + object_class->second);
                        }
                    }
                    if (found == values->second.end()) {
                        found = values->second.find("others");
                    }
                    if (found == values->second.end()) {
                        found = values->second.find("all");
                    }
                    if (found != values->second.end()) {
                        auto value = found->second;
                        value.span = cover(name.span, attribute.span);
                        // Keep the specification's prefix and designator so
                        // the HIR can recover the declared attribute subtype
                        // after replacing this occurrence with its static
                        // value (IEEE 1076-2008 6.7).
                        // An alias's attribute names the aliased object's
                        // specification.
                        value.nominal_type = "@fsim-vhdl-user-attribute:"
                            + (separator == std::string::npos
                                    ? specified_entity
                                    : canonical.substr(0, separator + 1U)
                                        + specified_entity)
                            + ":" + designator;
                        for (;;) {
                            if (match(TokenKind::Dot)) {
                                const auto member = expect_identifier("selected attribute element");
                                value = Expression { ExpressionKind::Call,
                                    vhdl_selection_suffix(member.text),
                                    { std::move(value) },
                                    cover(name.span, member.span) };
                                continue;
                            }
                            // An indexed name or slice of the attribute's
                            // value (`PT'AT1(1)`, `PT'AT1(2 to 4)`).
                            if (!match(TokenKind::LeftParen)) {
                                break;
                            }
                            const auto& specified
                                = value.text.starts_with("@vhdl-qualified:")
                                    && value.operands.size() == 1U
                                ? value.operands.front()
                                : value;
                            if (specified.kind == ExpressionKind::IntegerLiteral
                                || specified.kind == ExpressionKind::BooleanLiteral) {
                                error(previous(), "FSIM-VHDL-SEM-126",
                                    "the value of attribute '" + attribute.text
                                        + "' is not an array and cannot be indexed");
                            }
                            value = parse_index_or_slice_suffix(
                                std::move(value), name.span, "')' after slice",
                                "')' after index", "FSIM-VHDL-PARSE-033");
                        }
                        // The value is itself a prefix (`x'a'right`).
                        return parse_vhdl_name_suffixes(
                            std::move(value), name.span);
                    }
                }
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
            if (designator == "index" || designator == "designated_subtype"
                || designator == "reflect" || designator == "converse") {
                require_vhdl_standard(
                    attribute, VhdlStandard::Vhdl2019,
                    "the predefined '" + designator + " attribute",
                    "select VHDL-2019");
            }
            if (designator == "index" || designator == "designated_subtype"
                || designator == "converse") {
                error(attribute, "FSIM-VHDL-SEM-113",
                    "the predefined '" + designator
                        + " attribute does not denote a value expression");
            }
            auto attribute_prefix = Expression {
                ExpressionKind::Identifier, canonical, { }, name.span };
            attach_selected_spelling(attribute_prefix);
            std::vector<Expression> operands {
                std::move(attribute_prefix)
            };
            // A signal attribute without a parameter denotes a value, which
            // a following parenthesis indexes or slices (IEEE 1076-2008
            // 16.2.4, 8.1).
            const bool parameterless_signal_attribute
                = designator == "event" || designator == "active"
                || designator == "last_event" || designator == "last_active"
                || designator == "last_value" || designator == "driving"
                || designator == "driving_value"
                || designator == "transaction";
            if (!parameterless_signal_attribute
                && match(TokenKind::LeftParen)) {
                operands.push_back(parse_expression());
                expect(TokenKind::RightParen, "')' after attribute argument",
                    "FSIM-VHDL-PARSE-120");
            }
            const bool object_shorthand =
                vhdl_standard_ >= VhdlStandard::Vhdl2019
                && (designator == "pos" || designator == "succ"
                    || designator == "pred" || designator == "leftof"
                    || designator == "rightof" || designator == "image");
            const bool possible_object_shorthand =
                operands.size() == 1U && designator != "val"
                && (designator == "pos" || designator == "succ"
                    || designator == "pred" || designator == "leftof"
                    || designator == "rightof" || designator == "image");
            if (possible_object_shorthand
                && vhdl_standard_ < VhdlStandard::Vhdl2019) {
                require_vhdl_standard(
                    attribute, VhdlStandard::Vhdl2019,
                    "object-prefix shorthand for the predefined '"
                        + designator + " attribute",
                    "select VHDL-2019 or supply the required parenthesized "
                    "argument to a type-mark attribute");
            }
            const bool requires_argument =
                (designator == "pos" || designator == "val"
                    || designator == "succ" || designator == "pred"
                    || designator == "leftof" || designator == "rightof"
                    || designator == "image" || designator == "value")
                && !object_shorthand;
            if (requires_argument && operands.size() != 2) {
                error(attribute, "FSIM-VHDL-PARSE-142",
                    "enumeration attribute '" + attribute.text + "' requires one parenthesized argument");
            }
            if (designator == "reflect" && operands.size() != 1) {
                error(attribute, "FSIM-VHDL-SEM-113",
                    "the predefined 'reflect attribute does not accept an "
                    "argument");
            }
            return parse_vhdl_name_suffixes(
                Expression { ExpressionKind::Call, "'" + designator,
                    std::move(operands), cover(name.span, previous().span) },
                name.span);
        }
        if (match(TokenKind::LeftParen)) {
            if (at(TokenKind::RightParen)) {
                // An actual parameter part or index list is never empty;
                // a call without actuals has no parentheses (IEEE 1076-2008
                // 9.3.4).
                error(current(), "FSIM-VHDL-PARSE-303",
                    "VHDL parentheses after a name cannot be empty");
            }
            auto base = Expression {
                ExpressionKind::Identifier, canonical, { }, name.span };
            attach_selected_spelling(base);
            std::vector<Expression> arguments;
            std::vector<std::string> argument_names;
            bool saw_named = false;
            // Individually associated subelements of one formal become one
            // named aggregate actual (IEEE 1076-2008 6.5.7.1).
            std::vector<std::pair<std::string,
                std::vector<VhdlIndividualPart>>> individual;
            if (!at(TokenKind::RightParen)) {
                do {
                    std::string argument_name;
                    if (vhdl_individual_formal_ahead()) {
                        saw_named = true;
                        const auto part_span = current().span;
                        VhdlIndividualPart part;
                        auto formal = parse_vhdl_individual_part(part);
                        auto group = std::ranges::find(individual, formal,
                            &std::pair<std::string,
                                std::vector<VhdlIndividualPart>>::first);
                        if (group == individual.end()) {
                            individual.emplace_back(formal,
                                std::vector<VhdlIndividualPart> { });
                            group = std::prev(individual.end());
                            arguments.push_back(Expression {
                                ExpressionKind::Aggregate, "", { },
                                part_span });
                            argument_names.push_back(std::move(formal));
                        }
                        group->second.push_back(std::move(part));
                        continue;
                    }
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
                            auto slice = Expression { ExpressionKind::Slice,
                                detail::ascii_lower(direction.text),
                                { base, std::move(argument), std::move(second) },
                                cover(name.span, previous().span) };
                            // A slice is a name and may itself be sliced or
                            // indexed (`v(1 to 10)(2 to 9)`).
                            while (match(TokenKind::LeftParen)) {
                                slice = parse_index_or_slice_suffix(
                                    std::move(slice), name.span,
                                    "')' after chained slice",
                                    "')' after chained index",
                                    "FSIM-VHDL-PARSE-033", false);
                            }
                            return slice;
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
            for (const auto& [formal, parts] : individual) {
                for (std::size_t index { }; index < arguments.size(); ++index) {
                    if (argument_names[index] == formal
                        && arguments[index].kind == ExpressionKind::Aggregate
                        && arguments[index].operands.empty()) {
                        arguments[index] = build_vhdl_individual_aggregate(
                            parts, arguments[index].span);
                        break;
                    }
                }
            }
            Expression call { ExpressionKind::Call, std::move(canonical),
                std::move(arguments), cover(name.span, previous().span) };
            attach_selected_spelling(call);
            if (saw_named) {
                call.call_argument_names = std::move(argument_names);
            }
            for (;;) {
                if (match(TokenKind::LeftParen)) {
                    // A function call is a prefix of an indexed name or a
                    // slice in every revision (IEEE 1076-1993 6.1). All
                    // selectors of one parenthesized suffix form one Index
                    // node; a nested array applies another suffix.
                    call = parse_index_or_slice_suffix(std::move(call),
                        name.span, "')' after chained slice",
                        "')' after chained index", "FSIM-VHDL-PARSE-033");
                    continue;
                }
                if (!match(TokenKind::Dot)) {
                    break;
                }
                // A function call is the prefix of a selected name in
                // every revision (IEEE 1076-1993 6.3).
                const auto member = expect_identifier("selected record element");
                call = Expression { ExpressionKind::Call,
                    vhdl_selection_suffix(member.text),
                    { std::move(call) },
                    cover(name.span, member.span) };
            }
            return call;
        }
        if (vhdl_physical_unit_names_.contains(canonical)
            && !vhdl_unit_hiding_object_names_.contains(canonical)) {
            return Expression { ExpressionKind::Call,
                "@vhdl-physical:" + canonical,
                { Expression { ExpressionKind::IntegerLiteral, "1", { },
                    name.span } },
                name.span };
        }
        auto expression = Expression {
            ExpressionKind::Identifier, std::move(canonical), { }, name.span
        };
        expression.vhdl_selected_name_spelling
            = std::move(selected.vhdl_selected_name_spelling);
        return expression;
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
                    // Boolean and character literals choose elements of
                    // arrays indexed by BOOLEAN or a character type.
                    if (choice_expression.kind == ExpressionKind::StringLiteral || choice_expression.kind == ExpressionKind::Aggregate) {
                        error(previous(), "FSIM-VHDL-PARSE-132",
                            "an aggregate choice must be a record element, "
                            "others, or a locally static discrete expression or "
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
