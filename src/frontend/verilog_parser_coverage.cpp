// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include "fsim/frontend/coverage_sampling.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <span>
#include <unordered_set>
#include <utility>

namespace fsim::frontend {

namespace {

    [[nodiscard]] bool unsupported_coverage_type(
        const std::span<const Token> tokens)
    {
        return std::ranges::any_of(tokens, [](const Token& token) {
            return token.text == "real" || token.text == "shortreal"
                || token.text == "realtime" || token.text == "string"
                || token.text == "chandle" || token.text == "event"
                || token.text == "void";
        });
    }

    [[nodiscard]] bool balanced_delimiters(
        const std::span<const Token> tokens)
    {
        int parentheses { };
        int brackets { };
        int braces { };
        for (const auto& token : tokens) {
            if (token.kind == TokenKind::LeftParen) {
                ++parentheses;
            } else if (token.kind == TokenKind::RightParen) {
                --parentheses;
            } else if (token.kind == TokenKind::LeftBracket) {
                ++brackets;
            } else if (token.kind == TokenKind::RightBracket) {
                --brackets;
            } else if (token.kind == TokenKind::LeftBrace) {
                ++braces;
            } else if (token.kind == TokenKind::RightBrace) {
                --braces;
            }
            if (parentheses < 0 || brackets < 0 || braces < 0) {
                return false;
            }
        }
        return parentheses == 0 && brackets == 0 && braces == 0;
    }

    [[nodiscard]] std::optional<std::size_t> matching_right_parenthesis(
        const std::span<const Token> tokens,
        const std::size_t left)
    {
        int depth { };
        for (std::size_t index = left; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftParen) {
                ++depth;
            } else if (tokens[index].kind == TokenKind::RightParen) {
                --depth;
                if (depth == 0) {
                    return index;
                }
            }
            if (depth < 0) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> matching_right_brace(
        const std::span<const Token> tokens,
        const std::size_t left)
    {
        int depth { };
        for (std::size_t index = left; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftBrace) {
                ++depth;
            } else if (tokens[index].kind == TokenKind::RightBrace) {
                --depth;
                if (depth == 0) {
                    return index;
                }
            }
            if (depth < 0) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> matching_right_bracket(
        const std::span<const Token> tokens,
        const std::size_t left)
    {
        int depth { };
        for (std::size_t index = left; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftBracket) {
                ++depth;
            } else if (tokens[index].kind == TokenKind::RightBracket) {
                --depth;
                if (depth == 0)
                    return index;
            }
            if (depth < 0)
                return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<std::vector<Token>> split_top_level(
        const std::span<const Token> tokens,
        const TokenKind separator)
    {
        std::vector<std::vector<Token>> items;
        std::size_t begin { };
        int parentheses { };
        int brackets { };
        int braces { };
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            const auto kind = tokens[index].kind;
            if (kind == TokenKind::LeftParen) {
                ++parentheses;
            } else if (kind == TokenKind::RightParen) {
                --parentheses;
            } else if (kind == TokenKind::LeftBracket) {
                ++brackets;
            } else if (kind == TokenKind::RightBracket) {
                --brackets;
            } else if (kind == TokenKind::LeftBrace) {
                ++braces;
            } else if (kind == TokenKind::RightBrace) {
                --braces;
            } else if (
                kind == separator && parentheses == 0 && brackets == 0
                && braces == 0) {
                items.emplace_back(
                    tokens.begin() + static_cast<std::ptrdiff_t>(begin),
                    tokens.begin() + static_cast<std::ptrdiff_t>(index));
                begin = index + 1U;
            }
        }
        items.emplace_back(
            tokens.begin() + static_cast<std::ptrdiff_t>(begin), tokens.end());
        return items;
    }

    [[nodiscard]] std::optional<std::size_t> find_top_level(
        const std::span<const Token> tokens,
        const TokenKind sought)
    {
        int parentheses { };
        int brackets { };
        int braces { };
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            const auto kind = tokens[index].kind;
            if (kind == TokenKind::LeftParen) {
                ++parentheses;
            } else if (kind == TokenKind::RightParen) {
                --parentheses;
            } else if (kind == TokenKind::LeftBracket) {
                ++brackets;
            } else if (kind == TokenKind::RightBracket) {
                --brackets;
            } else if (kind == TokenKind::LeftBrace) {
                ++braces;
            } else if (kind == TokenKind::RightBrace) {
                --braces;
            } else if (
                kind == sought && parentheses == 0 && brackets == 0 && braces == 0) {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> find_top_level_keyword(
        const std::span<const Token> tokens,
        const std::string_view keyword)
    {
        int parentheses { };
        int brackets { };
        int braces { };
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            const auto kind = tokens[index].kind;
            if (kind == TokenKind::LeftParen) {
                ++parentheses;
            } else if (kind == TokenKind::RightParen) {
                --parentheses;
            } else if (kind == TokenKind::LeftBracket) {
                ++brackets;
            } else if (kind == TokenKind::RightBracket) {
                --brackets;
            } else if (kind == TokenKind::LeftBrace) {
                ++braces;
            } else if (kind == TokenKind::RightBrace) {
                --braces;
            } else if (
                parentheses == 0 && brackets == 0 && braces == 0
                && tokens[index].text == keyword) {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool coverage_body_start(
        const std::span<const Token> tokens,
        const std::size_t left,
        const std::size_t right)
    {
        if (right == left + 1U) {
            return true;
        }
        const auto first = tokens[left + 1U].text;
        return first == "bins" || first == "wildcard" || first == "illegal_bins"
            || first == "ignore_bins" || first == "option"
            || first == "type_option";
    }

    [[nodiscard]] std::string joined_spelling(
        const std::span<const Token> tokens)
    {
        std::string result;
        for (const auto& token : tokens) {
            result += token.text;
        }
        return result;
    }

    struct CoverageLiteral {
        std::optional<std::int64_t> exact;
        std::uint64_t value { };
        std::uint64_t mask { };
        std::uint32_t width { };
        bool wildcard { };
        bool signed_value { };
        std::string value_bits;
        std::string unknown_bits;
        std::string mask_bits;
    };

    [[nodiscard]] std::string normalize_coverage_plane(
        std::string bits,
        const std::uint32_t width,
        const char extension)
    {
        if (bits.size() > width) {
            bits.erase(0U, bits.size() - width);
        } else if (bits.size() < width) {
            bits.insert(0U, width - bits.size(), extension);
        }
        return bits;
    }

    [[nodiscard]] std::uint64_t coverage_low_word(
        const std::string_view bits)
    {
        std::uint64_t result { };
        const auto begin = bits.size() > 64U ? bits.size() - 64U : 0U;
        for (std::size_t index = begin; index < bits.size(); ++index) {
            result = (result << 1U) | (bits[index] == '1' ? 1U : 0U);
        }
        return result;
    }

    void twos_complement_coverage_bits(std::string& bits)
    {
        bool carry { true };
        for (auto iterator = bits.rbegin(); iterator != bits.rend(); ++iterator) {
            const bool inverted = *iterator == '0';
            const bool result = inverted != carry;
            carry = inverted && carry;
            *iterator = result ? '1' : '0';
        }
    }

    [[nodiscard]] std::optional<std::string> decimal_coverage_bits(
        const std::string_view digits,
        const std::uint32_t width)
    {
        std::string bits(width, '0');
        for (const auto character : digits) {
            if (character < '0' || character > '9') {
                return std::nullopt;
            }
            unsigned carry = static_cast<unsigned>(character - '0');
            for (auto iterator = bits.rbegin(); iterator != bits.rend(); ++iterator) {
                const auto product = static_cast<unsigned>(*iterator - '0') * 10U
                    + carry;
                *iterator = (product & 1U) != 0U ? '1' : '0';
                carry = product >> 1U;
            }
        }
        return bits;
    }

    [[nodiscard]] std::optional<std::int64_t> coverage_scalar_exact(
        const std::string_view bits,
        const bool signed_value)
    {
        if (bits.empty() || bits.size() > 64U) {
            return std::nullopt;
        }
        auto value = coverage_low_word(bits);
        if (signed_value && bits.size() < 64U && bits.front() == '1') {
            value |= std::numeric_limits<std::uint64_t>::max() << bits.size();
        }
        return static_cast<std::int64_t>(value);
    }

    [[nodiscard]] std::optional<CoverageLiteral> coverage_literal(
        std::span<const Token> tokens)
    {
        bool negative { };
        if (tokens.size() == 2U && tokens.front().kind == TokenKind::Minus) {
            negative = true;
            tokens = tokens.subspan(1U);
        }
        if (tokens.size() != 1U || tokens.front().kind != TokenKind::Number) {
            return std::nullopt;
        }
        std::string spelling = tokens.front().text;
        spelling.erase(
            std::remove(spelling.begin(), spelling.end(), '_'), spelling.end());
        const auto apostrophe = spelling.find('\'');
        if (apostrophe == std::string::npos) {
            const auto magnitude
                = detail::OutputUnsignedInteger::parse(spelling, 10U);
            if (!magnitude
                || magnitude->bit_width()
                    >= std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            const auto width = static_cast<std::uint32_t>(std::max(
                std::size_t { 32U }, magnitude->bit_width() + 1U));
            auto bits = decimal_coverage_bits(spelling, width);
            if (!bits) {
                return std::nullopt;
            }
            if (negative) {
                twos_complement_coverage_bits(*bits);
            }
            CoverageLiteral result;
            result.exact = coverage_scalar_exact(*bits, true);
            result.value = coverage_low_word(*bits);
            result.mask = width >= 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (std::uint64_t { 1U } << width) - 1U;
            result.width = width;
            result.signed_value = true;
            result.value_bits = std::move(*bits);
            result.unknown_bits.assign(width, '0');
            result.mask_bits.assign(width, '1');
            return result;
        }

        std::uint32_t declared_width { };
        if (apostrophe != 0U) {
            const auto width_text = std::string_view { spelling }.substr(0U, apostrophe);
            const auto parsed = std::from_chars(
                width_text.data(), width_text.data() + width_text.size(),
                declared_width);
            if (parsed.ec != std::errc { }
                || parsed.ptr != width_text.data() + width_text.size()
                || declared_width == 0U) {
                return std::nullopt;
            }
        }
        auto suffix = std::string_view { spelling }.substr(apostrophe + 1U);
        bool signed_value { };
        if (!suffix.empty() && (suffix.front() == 's' || suffix.front() == 'S')) {
            signed_value = true;
            suffix.remove_prefix(1U);
        }
        if (suffix.size() < 2U) {
            return std::nullopt;
        }
        const auto base_character = suffix.front();
        suffix.remove_prefix(1U);

        if (base_character == 'd' || base_character == 'D') {
            const auto width = declared_width == 0U ? 64U : declared_width;
            if (suffix.size() == 1U
                && (suffix.front() == 'x' || suffix.front() == 'X'
                    || suffix.front() == 'z' || suffix.front() == 'Z'
                    || suffix.front() == '?')) {
                if (negative) {
                    return std::nullopt;
                }
                CoverageLiteral result;
                result.value_bits.assign(
                    width,
                    suffix.front() == 'x' || suffix.front() == 'X' ? '1' : '0');
                result.unknown_bits.assign(width, '1');
                result.mask_bits.assign(width, '0');
                result.value = coverage_low_word(result.value_bits);
                result.width = width;
                result.wildcard = true;
                result.signed_value = signed_value;
                return result;
            }
            auto bits = decimal_coverage_bits(suffix, width);
            if (!bits) {
                return std::nullopt;
            }
            auto exact = coverage_scalar_exact(*bits, signed_value);
            if (negative) {
                if (exact) {
                    *exact = -*exact;
                }
                twos_complement_coverage_bits(*bits);
            }
            CoverageLiteral result;
            result.exact = exact;
            result.value = coverage_low_word(*bits);
            result.mask = width >= 64U
                ? std::numeric_limits<std::uint64_t>::max()
                : (std::uint64_t { 1 } << width) - 1U;
            result.width = width;
            result.signed_value = signed_value;
            result.value_bits = std::move(*bits);
            result.unknown_bits.assign(width, '0');
            result.mask_bits.assign(width, '1');
            return result;
        }

        unsigned base { };
        std::uint32_t digit_width { };
        if (base_character == 'b' || base_character == 'B') {
            base = 2U;
            digit_width = 1U;
        } else if (base_character == 'o' || base_character == 'O') {
            base = 8U;
            digit_width = 3U;
        } else if (base_character == 'h' || base_character == 'H') {
            base = 16U;
            digit_width = 4U;
        } else {
            return std::nullopt;
        }
        if (suffix.size()
            > std::numeric_limits<std::uint32_t>::max() / digit_width) {
            return std::nullopt;
        }
        const auto natural_width = static_cast<std::uint32_t>(suffix.size())
            * digit_width;
        const auto width = declared_width == 0U ? natural_width : declared_width;
        if (width == 0U) {
            return std::nullopt;
        }

        std::string bits;
        std::string unknown_bits;
        std::string mask_bits;
        bits.reserve(natural_width);
        unknown_bits.reserve(natural_width);
        mask_bits.reserve(natural_width);
        bool wildcard { };
        for (const auto character : suffix) {
            if (character == '?' || character == 'x' || character == 'X'
                || character == 'z' || character == 'Z') {
                wildcard = true;
                bits.append(
                    digit_width,
                    character == 'x' || character == 'X' ? '1' : '0');
                unknown_bits.append(digit_width, '1');
                mask_bits.append(digit_width, '0');
                continue;
            }
            unsigned digit { };
            if (character >= '0' && character <= '9') {
                digit = static_cast<unsigned>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                digit = 10U + static_cast<unsigned>(character - 'a');
            } else if (character >= 'A' && character <= 'F') {
                digit = 10U + static_cast<unsigned>(character - 'A');
            } else {
                return std::nullopt;
            }
            if (digit >= base) {
                return std::nullopt;
            }
            for (auto bit = digit_width; bit > 0U; --bit) {
                bits.push_back((digit & (1U << (bit - 1U))) != 0U ? '1' : '0');
                unknown_bits.push_back('0');
                mask_bits.push_back('1');
            }
        }
        const bool unknown_most_significant
            = !unknown_bits.empty() && unknown_bits.front() == '1';
        const auto value_extension
            = (signed_value || unknown_most_significant) && !bits.empty()
            ? bits.front()
            : '0';
        const auto mask_extension = unknown_most_significant
            ? '0'
            : '1';
        const auto unknown_extension = unknown_most_significant ? '1' : '0';
        bits = normalize_coverage_plane(std::move(bits), width, value_extension);
        unknown_bits = normalize_coverage_plane(
            std::move(unknown_bits), width, unknown_extension);
        mask_bits = normalize_coverage_plane(
            std::move(mask_bits), width, mask_extension);
        if (negative) {
            if (wildcard) {
                return std::nullopt;
            }
            twos_complement_coverage_bits(bits);
        }
        auto exact = wildcard
            ? std::optional<std::int64_t> { }
            : coverage_scalar_exact(bits, signed_value);
        CoverageLiteral result;
        result.exact = exact;
        result.value = coverage_low_word(bits);
        result.mask = coverage_low_word(mask_bits);
        result.width = width;
        result.wildcard = wildcard;
        result.signed_value = signed_value;
        result.value_bits = std::move(bits);
        result.unknown_bits = std::move(unknown_bits);
        result.mask_bits = std::move(mask_bits);
        return result;
    }

    [[nodiscard]] std::optional<SystemVerilogCoverageBinValue> coverage_bin_value(
        const std::span<const Token> tokens,
        const bool wildcard_bin)
    {
        SystemVerilogCoverageBinValue result;
        result.tokens.assign(tokens.begin(), tokens.end());
        result.span = tokens.front().span;
        result.span.end = tokens.back().span.end;
        if (tokens.size() >= 5U
            && tokens.front().kind == TokenKind::LeftBracket
            && tokens.back().kind == TokenKind::RightBracket) {
            const auto interior = tokens.subspan(1U, tokens.size() - 2U);
            const auto colon = find_top_level(interior, TokenKind::Colon);
            if (!colon || *colon == 0U || *colon + 1U == interior.size()) {
                return std::nullopt;
            }
            const auto left = coverage_literal(interior.first(*colon));
            const auto right = coverage_literal(interior.subspan(*colon + 1U));
            if (!left || !right || left->wildcard || right->wildcard) {
                return std::nullopt;
            }
            result.range_left = left->exact;
            result.range_right = right->exact;
            result.range_left_bits = left->value_bits;
            result.range_right_bits = right->value_bits;
            result.range_left_signed = left->signed_value;
            result.range_right_signed = right->signed_value;
            result.width = std::max(left->width, right->width);
            return result;
        }
        const auto literal = coverage_literal(tokens);
        if (!literal) {
            return std::nullopt;
        }
        if (literal->wildcard && wildcard_bin) {
            result.wildcard = true;
            result.wildcard_value = literal->value;
            result.wildcard_mask = literal->mask;
            result.width = literal->width;
            result.wildcard_value_bits = literal->value_bits;
            result.wildcard_mask_bits = literal->mask_bits;
        } else {
            result.exact_value = literal->exact;
            result.width = literal->width;
            result.exact_bits = literal->value_bits;
            result.exact_unknown_bits = literal->unknown_bits;
            result.exact_signed = literal->signed_value;
        }
        return result;
    }

    [[nodiscard]] SourceSpan coverage_span(const std::span<const Token> tokens)
    {
        auto span = tokens.front().span;
        span.end = tokens.back().span.end;
        return span;
    }

    [[nodiscard]] std::optional<std::uint32_t> transition_bound(
        const std::span<const Token> tokens)
    {
        const auto value = coverage_literal(tokens);
        if (!value || value->wildcard || !value->exact || *value->exact < 0
            || static_cast<std::uint64_t>(*value->exact) > 65'536U) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(*value->exact);
    }

    [[nodiscard]] bool structure_transition_repetition(
        std::vector<Token>& tokens,
        SystemVerilogCoverageTransitionRepetition& repetition)
    {
        int parentheses { };
        int brackets { };
        int braces { };
        std::optional<std::size_t> repetition_left;
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            if (tokens[index].kind == TokenKind::LeftBracket
                && parentheses == 0 && brackets == 0 && braces == 0
                && index + 1U < tokens.size()
                && (tokens[index + 1U].kind == TokenKind::Star
                    || tokens[index + 1U].kind == TokenKind::Assign
                    || tokens[index + 1U].kind == TokenKind::ThinArrow)) {
                repetition_left = index;
            }
            if (tokens[index].kind == TokenKind::LeftParen)
                ++parentheses;
            if (tokens[index].kind == TokenKind::RightParen)
                --parentheses;
            if (tokens[index].kind == TokenKind::LeftBracket)
                ++brackets;
            if (tokens[index].kind == TokenKind::RightBracket)
                --brackets;
            if (tokens[index].kind == TokenKind::LeftBrace)
                ++braces;
            if (tokens[index].kind == TokenKind::RightBrace)
                --braces;
        }
        if (!repetition_left)
            return true;
        const auto right = matching_right_bracket(tokens, *repetition_left);
        if (!right || *right + 1U != tokens.size())
            return false;
        const auto marker = tokens[*repetition_left + 1U].kind;
        repetition.kind = marker == TokenKind::Star
            ? SystemVerilogCoverageTransitionRepetitionKind::Consecutive
            : marker == TokenKind::ThinArrow
            ? SystemVerilogCoverageTransitionRepetitionKind::Goto
            : SystemVerilogCoverageTransitionRepetitionKind::Nonconsecutive;
        repetition.span = coverage_span(
            std::span<const Token> { tokens }.subspan(
                *repetition_left, *right - *repetition_left + 1U));
        const auto count_tokens = std::span<const Token> { tokens }.subspan(
            *repetition_left + 2U, *right - *repetition_left - 2U);
        if (count_tokens.empty()) {
            if (repetition.kind
                != SystemVerilogCoverageTransitionRepetitionKind::Consecutive) {
                return false;
            }
            repetition.minimum = 0U;
            repetition.maximum.reset();
        } else {
            const auto colon = find_top_level(count_tokens, TokenKind::Colon);
            const auto minimum_tokens = colon
                ? count_tokens.first(*colon)
                : count_tokens;
            if (minimum_tokens.empty())
                return false;
            const auto minimum = transition_bound(minimum_tokens);
            if (!minimum)
                return false;
            repetition.minimum = *minimum;
            repetition.maximum = *minimum;
            if (colon) {
                const auto maximum_tokens = count_tokens.subspan(*colon + 1U);
                if (maximum_tokens.size() == 1U
                    && maximum_tokens.front().text == "$") {
                    repetition.maximum.reset();
                } else {
                    const auto maximum = transition_bound(maximum_tokens);
                    if (!maximum || *maximum < *minimum)
                        return false;
                    repetition.maximum = *maximum;
                }
            }
        }
        tokens.resize(*repetition_left);
        return !tokens.empty();
    }

    [[nodiscard]] std::optional<SystemVerilogCoverageTransitionStep>
    structure_transition_step(
        std::vector<Token> tokens,
        const bool wildcard_bin)
    {
        if (tokens.empty())
            return std::nullopt;
        SystemVerilogCoverageTransitionStep step;
        const auto original_span = coverage_span(tokens);
        if (!structure_transition_repetition(tokens, step.repetition)) {
            return std::nullopt;
        }
        const auto values = split_top_level(tokens, TokenKind::Comma);
        for (const auto& value_tokens : values) {
            if (value_tokens.empty())
                return std::nullopt;
            const auto value = coverage_bin_value(value_tokens, wildcard_bin);
            if (!value)
                return std::nullopt;
            step.values.push_back(*value);
        }
        step.span = original_span;
        return step;
    }

    [[nodiscard]] std::optional<SystemVerilogCoverageTransitionDelay>
    structure_transition_delay(
        const std::span<const Token> tokens,
        std::size_t& position)
    {
        const auto start = position;
        position += 2U;
        SystemVerilogCoverageTransitionDelay delay;
        if (position >= tokens.size())
            return std::nullopt;
        if (tokens[position].kind == TokenKind::LeftBracket) {
            const auto right = matching_right_bracket(tokens, position);
            if (!right || *right == position + 1U)
                return std::nullopt;
            const auto range = tokens.subspan(position + 1U, *right - position - 1U);
            const auto colon = find_top_level(range, TokenKind::Colon);
            if (!colon || *colon == 0U || *colon + 1U == range.size()) {
                return std::nullopt;
            }
            const auto minimum = transition_bound(range.first(*colon));
            const auto maximum = transition_bound(range.subspan(*colon + 1U));
            if (!minimum || !maximum || *maximum < *minimum)
                return std::nullopt;
            delay.minimum = *minimum;
            delay.maximum = *maximum;
            position = *right + 1U;
        } else {
            const auto bound = transition_bound(tokens.subspan(position, 1U));
            if (!bound)
                return std::nullopt;
            delay.minimum = *bound;
            delay.maximum = *bound;
            ++position;
        }
        delay.span = coverage_span(tokens.subspan(start, position - start));
        return delay;
    }

    [[nodiscard]] std::optional<SystemVerilogCoverageTransitionSequence>
    structure_transition_sequence(
        const std::span<const Token> tokens,
        const bool wildcard_bin)
    {
        if (tokens.size() < 5U || tokens.front().kind != TokenKind::LeftParen
            || tokens.back().kind != TokenKind::RightParen) {
            return std::nullopt;
        }
        const auto right = matching_right_parenthesis(tokens, 0U);
        if (!right || *right + 1U != tokens.size())
            return std::nullopt;
        const auto inner = tokens.subspan(1U, tokens.size() - 2U);
        SystemVerilogCoverageTransitionSequence sequence;
        std::size_t element_start { };
        int parentheses { };
        int brackets { };
        int braces { };
        for (std::size_t position = 0; position < inner.size();) {
            const bool arrow = inner[position].kind == TokenKind::Arrow
                && parentheses == 0 && brackets == 0 && braces == 0;
            const bool delay_marker = inner[position].kind == TokenKind::Hash
                && position + 1U < inner.size()
                && inner[position + 1U].kind == TokenKind::Hash
                && parentheses == 0 && brackets == 0 && braces == 0;
            if (arrow || delay_marker) {
                const auto step = structure_transition_step(
                    std::vector<Token> {
                        inner.begin() + static_cast<std::ptrdiff_t>(element_start),
                        inner.begin() + static_cast<std::ptrdiff_t>(position) },
                    wildcard_bin);
                if (!step)
                    return std::nullopt;
                sequence.steps.push_back(*step);
                if (arrow) {
                    SystemVerilogCoverageTransitionDelay delay;
                    delay.span = inner[position].span;
                    sequence.delays.push_back(std::move(delay));
                    ++position;
                } else {
                    const auto delay = structure_transition_delay(inner, position);
                    if (!delay)
                        return std::nullopt;
                    sequence.delays.push_back(*delay);
                }
                element_start = position;
                continue;
            }
            const auto kind = inner[position].kind;
            if (kind == TokenKind::LeftParen)
                ++parentheses;
            if (kind == TokenKind::RightParen)
                --parentheses;
            if (kind == TokenKind::LeftBracket)
                ++brackets;
            if (kind == TokenKind::RightBracket)
                --brackets;
            if (kind == TokenKind::LeftBrace)
                ++braces;
            if (kind == TokenKind::RightBrace)
                --braces;
            ++position;
        }
        const auto final_step = structure_transition_step(
            std::vector<Token> {
                inner.begin() + static_cast<std::ptrdiff_t>(element_start),
                inner.end() },
            wildcard_bin);
        if (!final_step)
            return std::nullopt;
        sequence.steps.push_back(*final_step);
        if (sequence.steps.size() < 2U
            || sequence.steps.size() != sequence.delays.size() + 1U) {
            return std::nullopt;
        }
        sequence.span = coverage_span(tokens);
        return sequence;
    }

    [[nodiscard]] std::optional<
        std::vector<SystemVerilogCoverageTransitionSequence>>
    structure_transition_sequences(
        const std::span<const Token> tokens,
        const bool wildcard_bin)
    {
        std::vector<SystemVerilogCoverageTransitionSequence> result;
        const auto sequences = split_top_level(tokens, TokenKind::Comma);
        for (const auto& sequence_tokens : sequences) {
            if (sequence_tokens.empty())
                return std::nullopt;
            const auto sequence = structure_transition_sequence(
                sequence_tokens, wildcard_bin);
            if (!sequence)
                return std::nullopt;
            result.push_back(*sequence);
        }
        return result;
    }

} // namespace

SystemVerilogCovergroupDeclaration
VerilogParser::parse_covergroup_declaration(
    const Token& start,
    const SystemVerilogCovergroupOwnerKind owner_kind)
{
    SystemVerilogCovergroupDeclaration declaration;
    declaration.owner_kind = owner_kind;

    if (language_ != Language::SystemVerilog2017) {
        error(
            start,
            "FSIM-SV-SEM-203",
            "a covergroup declaration requires SystemVerilog-2017");
    }

    if (!at(TokenKind::Identifier)) {
        error(
            current(),
            "FSIM-SV-PARSE-309",
            "expected a name after 'covergroup'");
    } else {
        const auto name = advance();
        declaration.name = name.text;
        declaration.name_token = name;
        declaration.name_span = name.span;
    }

    int parentheses { };
    int brackets { };
    int braces { };
    while (!at_end()
        && !keyword("endgroup")
        && !any_keyword(
            { "endmodule", "endinterface", "endprogram", "endpackage",
                "endclass" })) {
        if (at(TokenKind::Semicolon)
            && parentheses == 0
            && brackets == 0
            && braces == 0) {
            break;
        }
        const auto token = advance();
        declaration.header_tokens.push_back(token);
        if (token.kind == TokenKind::LeftParen) {
            ++parentheses;
        } else if (token.kind == TokenKind::RightParen) {
            --parentheses;
        } else if (token.kind == TokenKind::LeftBracket) {
            ++brackets;
        } else if (token.kind == TokenKind::RightBracket) {
            --brackets;
        } else if (token.kind == TokenKind::LeftBrace) {
            ++braces;
        } else if (token.kind == TokenKind::RightBrace) {
            --braces;
        }
    }
    if (!declaration.header_tokens.empty()) {
        declaration.header_span = span_from(
            declaration.header_tokens.front(),
            declaration.header_tokens.back());
    }
    expect(
        TokenKind::Semicolon,
        "';' after covergroup declaration header",
        "FSIM-SV-PARSE-310");
    structure_covergroup_header(declaration);

    while (!at_end()
        && !keyword("endgroup")
        && !any_keyword(
            { "endmodule", "endinterface", "endprogram", "endpackage",
                "endclass" })) {
        declaration.body_tokens.push_back(advance());
    }
    if (!declaration.body_tokens.empty()) {
        declaration.body_span = span_from(
            declaration.body_tokens.front(),
            declaration.body_tokens.back());
    }
    structure_covergroup_options(declaration);
    structure_covergroup_declarations(declaration);
    expect_keyword("endgroup", false, "FSIM-SV-PARSE-311");
    if (match(TokenKind::Colon)) {
        if (!at(TokenKind::Identifier)
            || any_keyword(
                { "endmodule", "endinterface", "endprogram", "endpackage",
                    "endclass" })) {
            error(
                current(),
                "FSIM-SV-PARSE-312",
                "expected a covergroup name after 'endgroup :'");
        } else {
            const auto end_name = advance();
            declaration.end_name = end_name.text;
            declaration.end_name_token = end_name;
            declaration.end_name_span = end_name.span;
            if (end_name.text != declaration.name) {
                error(
                    end_name,
                    "FSIM-SV-SEM-204",
                    "covergroup end name does not match '"
                        + declaration.name + "'");
            }
        }
    }
    declaration.span = span_from(start, previous());
    return declaration;
}

void VerilogParser::structure_covergroup_header(
    SystemVerilogCovergroupDeclaration& declaration)
{
    const std::span<const Token> header { declaration.header_tokens };
    if (header.empty()) {
        return;
    }
    std::size_t position { };
    if (header.front().kind == TokenKind::LeftParen) {
        const auto right = matching_right_parenthesis(header, 0U);
        if (!right) {
            error(
                header.front(),
                "FSIM-SV-PARSE-313",
                "a covergroup formal argument list has unbalanced delimiters");
            return;
        }
        structure_covergroup_formals(
            header.subspan(1U, *right - 1U),
            declaration.formals,
            "covergroup constructor");
        position = *right + 1U;
    }
    if (position == header.size()) {
        return;
    }

    const auto sampling_tokens = header.subspan(position);
    SystemVerilogCovergroupSampling sampling;
    sampling.tokens.assign(sampling_tokens.begin(), sampling_tokens.end());
    sampling.span = span_from(sampling_tokens.front(), sampling_tokens.back());
    if (sampling_tokens.size() >= 3U
        && sampling_tokens[0].text == "with"
        && sampling_tokens[1].text == "function"
        && sampling_tokens[2].text == "sample") {
        sampling.kind = SystemVerilogCovergroupSamplingKind::WithFunctionSample;
        if (sampling_tokens.size() < 5U
            || sampling_tokens[3].kind != TokenKind::LeftParen) {
            error(
                sampling_tokens.front(),
                "FSIM-SV-PARSE-315",
                "a 'with function sample' profile requires a formal list");
            return;
        }
        const auto right = matching_right_parenthesis(sampling_tokens, 3U);
        if (!right || *right + 1U != sampling_tokens.size()) {
            error(
                sampling_tokens.front(),
                "FSIM-SV-PARSE-315",
                "a 'with function sample' profile is unbalanced or has trailing tokens");
            return;
        }
        structure_covergroup_formals(
            sampling_tokens.subspan(4U, *right - 4U),
            sampling.formals,
            "covergroup sample");
    } else {
        sampling.kind = SystemVerilogCovergroupSamplingKind::Event;
        if (sampling_tokens.front().kind != TokenKind::At
            || !balanced_delimiters(sampling_tokens)) {
            error(
                sampling_tokens.front(),
                "FSIM-SV-PARSE-315",
                "a covergroup sampling event must start with '@' and be balanced");
            return;
        }
    }
    declaration.sampling = std::move(sampling);
}

void VerilogParser::structure_covergroup_formals(
    const std::span<const Token> tokens,
    std::vector<SystemVerilogCovergroupFormal>& formals,
    const std::string_view description)
{
    if (tokens.empty()) {
        return;
    }
    auto items = split_top_level(tokens, TokenKind::Comma);
    std::unordered_set<std::string> names;
    for (auto& item : items) {
        if (item.empty()) {
            error(
                tokens.front(),
                "FSIM-SV-PARSE-314",
                std::string { description } + " formal argument is empty");
            continue;
        }
        const auto assignment = find_top_level(item, TokenKind::Assign);
        const auto prefix_end = assignment.value_or(item.size());
        if (assignment && *assignment + 1U == item.size()) {
            error(
                item[*assignment],
                "FSIM-SV-PARSE-314",
                std::string { description } + " formal default is empty");
            continue;
        }
        const auto name_position = std::find_if(
            item.rbegin()
                + static_cast<std::ptrdiff_t>(item.size() - prefix_end),
            item.rend(),
            [](const Token& token) {
                return token.kind == TokenKind::Identifier;
            });
        if (name_position == item.rend()) {
            error(
                item.front(),
                "FSIM-SV-PARSE-314",
                std::string { description } + " formal argument has no name");
            continue;
        }
        const auto name_index = static_cast<std::size_t>(
            std::distance(item.begin(), name_position.base() - 1));
        std::size_t type_start { };
        SystemVerilogCovergroupFormal formal;
        if (item[type_start].text == "const"
            && type_start + 1U < name_index
            && item[type_start + 1U].text == "ref") {
            formal.const_ref = true;
            formal.direction = PortDirection::Ref;
            type_start += 2U;
        } else if (item[type_start].text == "input") {
            formal.direction = PortDirection::Input;
            ++type_start;
        } else if (item[type_start].text == "output") {
            formal.direction = PortDirection::Output;
            ++type_start;
        } else if (item[type_start].text == "inout") {
            formal.direction = PortDirection::Inout;
            ++type_start;
        } else if (item[type_start].text == "ref") {
            formal.direction = PortDirection::Ref;
            ++type_start;
        }
        if (type_start > name_index) {
            error(
                item.front(),
                "FSIM-SV-PARSE-314",
                std::string { description } + " formal argument has no declared name");
            continue;
        }
        formal.type_tokens.assign(
            item.begin() + static_cast<std::ptrdiff_t>(type_start),
            item.begin() + static_cast<std::ptrdiff_t>(name_index));
        if (unsupported_coverage_type(formal.type_tokens)) {
            error(
                formal.type_tokens.front(),
                "FSIM-SV-SEM-219",
                std::string { description }
                    + " formal type is outside the bounded integral coverage model");
            continue;
        }
        formal.name = item[name_index].text;
        formal.name_token = item[name_index];
        formal.name_span = item[name_index].span;
        if (assignment) {
            formal.default_tokens.assign(
                item.begin() + static_cast<std::ptrdiff_t>(*assignment + 1U),
                item.end());
        }
        formal.span = span_from(item.front(), item.back());
        if (!names.insert(formal.name).second) {
            error(
                item[name_index],
                "FSIM-SV-SEM-206",
                "duplicate " + std::string { description } + " formal argument '"
                    + formal.name + "'");
        } else {
            formals.push_back(std::move(formal));
        }
    }
}

void VerilogParser::structure_covergroup_options(
    SystemVerilogCovergroupDeclaration& declaration)
{
    const std::span<const Token> body { declaration.body_tokens };
    std::size_t begin { };
    int parentheses { };
    int brackets { };
    int braces { };
    for (std::size_t index = 0; index <= body.size(); ++index) {
        const bool at_end = index == body.size();
        const auto kind = at_end ? TokenKind::EndOfFile : body[index].kind;
        const bool terminator = !at_end && kind == TokenKind::Semicolon
            && parentheses == 0 && brackets == 0 && braces == 0;
        if (at_end || terminator) {
            const auto statement = body.subspan(begin, index - begin);
            if (!statement.empty()
                && (statement.front().text == "option"
                    || statement.front().text == "type_option")) {
                if (at_end) {
                    error(
                        statement.front(),
                        "FSIM-SV-PARSE-316",
                        "a covergroup option assignment requires a terminating semicolon");
                    break;
                }
                const auto assignment = find_top_level(statement, TokenKind::Assign);
                if (statement.size() < 4U
                    || statement[1].kind != TokenKind::Dot
                    || statement[2].kind != TokenKind::Identifier
                    || !assignment || *assignment != 3U
                    || *assignment + 1U == statement.size()) {
                    error(
                        statement.front(),
                        "FSIM-SV-PARSE-316",
                        "a covergroup option requires 'option.name = value'");
                } else {
                    SystemVerilogCovergroupOptionAssignment option;
                    option.scope = statement.front().text == "type_option"
                        ? SystemVerilogCovergroupOptionScope::Type
                        : SystemVerilogCovergroupOptionScope::Instance;
                    option.name = statement[2].text;
                    option.name_token = statement[2];
                    option.name_span = statement[2].span;
                    option.value_tokens.assign(
                        statement.begin()
                            + static_cast<std::ptrdiff_t>(*assignment + 1U),
                        statement.end());
                    option.span = span_from(statement.front(), body[index]);
                    declaration.option_assignments.push_back(std::move(option));
                }
            }
            begin = index + 1U;
        }
        if (at_end) {
            break;
        }
        if (kind == TokenKind::LeftParen) {
            ++parentheses;
        } else if (kind == TokenKind::RightParen) {
            --parentheses;
        } else if (kind == TokenKind::LeftBracket) {
            ++brackets;
        } else if (kind == TokenKind::RightBracket) {
            --brackets;
        } else if (kind == TokenKind::LeftBrace) {
            ++braces;
        } else if (kind == TokenKind::RightBrace) {
            --braces;
        }
    }
    for (const auto& option : declaration.option_assignments) {
        const auto literal = coverage_literal(option.value_tokens);
        if (!literal || literal->wildcard || !literal->exact)
            continue;
        const auto value = *literal->exact;
        const bool weight = option.name == "weight";
        const bool goal = option.name == "goal";
        const bool flag = option.name == "per_instance"
            || option.name == "merge_instances";
        const bool valid = weight
            ? value >= 0 && value <= 65'536
            : goal ? value >= 0 && value <= 100
            : flag ? value == 0 || value == 1
                   : true;
        if (!valid) {
            diagnostics_.push_back(Diagnostic {
                DiagnosticSeverity::Error,
                "FSIM-SV-SEM-218",
                "covergroup option '" + option.name
                    + "' is outside its bounded integer range",
                option.span,
                { } });
            continue;
        }
        if (weight) {
            auto& target = option.scope == SystemVerilogCovergroupOptionScope::Type
                ? declaration.effective_type_weight
                : declaration.effective_instance_weight;
            target = static_cast<std::uint32_t>(value);
        } else if (goal) {
            auto& target = option.scope == SystemVerilogCovergroupOptionScope::Type
                ? declaration.effective_type_goal
                : declaration.effective_instance_goal;
            target = static_cast<std::uint32_t>(value);
        } else if (option.name == "per_instance") {
            declaration.effective_per_instance = value != 0;
        } else if (option.name == "merge_instances") {
            declaration.effective_merge_instances = value != 0;
        }
    }
}

void VerilogParser::structure_covergroup_declarations(
    SystemVerilogCovergroupDeclaration& declaration)
{
    const std::span<const Token> body { declaration.body_tokens };
    std::unordered_set<std::string> names;
    std::size_t coverpoint_ordinal { };
    std::size_t cross_ordinal { };
    std::size_t position { };
    int outer_braces { };
    while (position < body.size()) {
        const auto& token = body[position];
        if (token.kind == TokenKind::LeftBrace) {
            ++outer_braces;
            ++position;
            continue;
        }
        if (token.kind == TokenKind::RightBrace) {
            --outer_braces;
            ++position;
            continue;
        }
        if (outer_braces != 0
            || token.kind != TokenKind::Identifier
            || (token.text != "coverpoint" && token.text != "cross")) {
            ++position;
            continue;
        }

        const bool coverpoint = token.text == "coverpoint";
        const auto keyword_position = position;
        std::size_t declaration_start = keyword_position;
        std::optional<Token> explicit_name_token;
        if (keyword_position >= 2U
            && body[keyword_position - 1U].kind == TokenKind::Colon
            && body[keyword_position - 2U].kind == TokenKind::Identifier) {
            declaration_start = keyword_position - 2U;
            explicit_name_token = body[declaration_start];
        }

        std::optional<std::size_t> body_left;
        std::optional<std::size_t> body_right;
        std::optional<std::size_t> terminator;
        int parentheses { };
        int brackets { };
        int braces { };
        for (std::size_t scan = keyword_position + 1U;
            scan < body.size(); ++scan) {
            const auto kind = body[scan].kind;
            if (kind == TokenKind::LeftParen) {
                ++parentheses;
            } else if (kind == TokenKind::RightParen) {
                --parentheses;
            } else if (kind == TokenKind::LeftBracket) {
                ++brackets;
            } else if (kind == TokenKind::RightBracket) {
                --brackets;
            } else if (
                kind == TokenKind::LeftBrace && parentheses == 0
                && brackets == 0 && braces == 0) {
                const auto right = matching_right_brace(body, scan);
                if (right && coverage_body_start(body, scan, *right)) {
                    body_left = scan;
                    body_right = *right;
                    terminator = *right;
                    if (*right + 1U < body.size()
                        && body[*right + 1U].kind == TokenKind::Semicolon) {
                        terminator = *right + 1U;
                    }
                    break;
                }
                ++braces;
            } else if (kind == TokenKind::LeftBrace) {
                ++braces;
            } else if (kind == TokenKind::RightBrace) {
                --braces;
            } else if (
                kind == TokenKind::Semicolon && parentheses == 0
                && brackets == 0 && braces == 0) {
                terminator = scan;
                break;
            }
        }
        if (!terminator) {
            error(
                token,
                "FSIM-SV-PARSE-317",
                "a coverpoint or cross declaration has no complete terminator");
            break;
        }

        const auto header_end = body_left.value_or(*terminator);
        const auto header = body.subspan(
            keyword_position + 1U, header_end - keyword_position - 1U);
        const auto iff = find_top_level_keyword(header, "iff");
        const auto expression_end = iff.value_or(header.size());
        SystemVerilogCoverageDeclaration item;
        item.kind = coverpoint
            ? SystemVerilogCoverageDeclarationKind::Coverpoint
            : SystemVerilogCoverageDeclarationKind::Cross;
        item.explicit_name = explicit_name_token.has_value();
        if (explicit_name_token) {
            item.name = explicit_name_token->text;
            item.name_token = explicit_name_token;
            item.name_span = explicit_name_token->span;
        } else if (coverpoint) {
            item.name = "$coverpoint$" + std::to_string(++coverpoint_ordinal);
        } else {
            item.name = "$cross$" + std::to_string(++cross_ordinal);
        }
        item.declaration_index = declaration.coverage_declarations.size();

        if (expression_end == 0U) {
            error(
                token,
                "FSIM-SV-PARSE-317",
                coverpoint
                    ? "a coverpoint declaration requires an expression"
                    : "a cross declaration requires operands");
            position = *terminator + 1U;
            continue;
        }
        const auto expression = header.first(expression_end);
        if (coverpoint) {
            item.expression_tokens.assign(expression.begin(), expression.end());
            item.expression_span = span_from(expression.front(), expression.back());
        } else {
            auto operands = split_top_level(expression, TokenKind::Comma);
            bool malformed { };
            for (auto& operand_tokens : operands) {
                if (operand_tokens.empty()) {
                    malformed = true;
                    continue;
                }
                SystemVerilogCoverageCrossOperand operand;
                operand.name = joined_spelling(operand_tokens);
                operand.tokens = std::move(operand_tokens);
                operand.span = span_from(
                    operand.tokens.front(), operand.tokens.back());
                item.cross_operands.push_back(std::move(operand));
            }
            if (malformed || item.cross_operands.size() < 2U) {
                error(
                    token,
                    "FSIM-SV-PARSE-317",
                    "a cross declaration requires at least two nonempty operands");
                position = *terminator + 1U;
                continue;
            }
        }

        if (iff) {
            if (*iff + 3U > header.size()
                || header[*iff + 1U].kind != TokenKind::LeftParen) {
                error(
                    header[*iff],
                    "FSIM-SV-PARSE-318",
                    "a coverage iff guard requires a parenthesized condition");
                position = *terminator + 1U;
                continue;
            }
            const auto right = matching_right_parenthesis(header, *iff + 1U);
            if (!right || *right + 1U != header.size()
                || *right == *iff + 2U) {
                error(
                    header[*iff],
                    "FSIM-SV-PARSE-318",
                    "a coverage iff guard is empty, unbalanced, or has trailing tokens");
                position = *terminator + 1U;
                continue;
            }
            item.iff_tokens.assign(
                header.begin() + static_cast<std::ptrdiff_t>(*iff + 2U),
                header.begin() + static_cast<std::ptrdiff_t>(*right));
            item.iff_span = span_from(header[*iff], header[*right]);
        }
        if (body_left && body_right) {
            item.body_tokens.assign(
                body.begin() + static_cast<std::ptrdiff_t>(*body_left + 1U),
                body.begin() + static_cast<std::ptrdiff_t>(*body_right));
            item.body_span = span_from(body[*body_left], body[*body_right]);
        }
        structure_coverage_options(item);
        if (coverpoint) {
            structure_coverpoint_bins(item);
        } else {
            structure_cross_bins(item);
        }
        item.span = span_from(body[declaration_start], body[*terminator]);
        if (!names.insert(item.name).second) {
            error(
                explicit_name_token.value_or(token),
                "FSIM-SV-SEM-207",
                "duplicate coverpoint or cross name '" + item.name + "'");
        } else {
            declaration.coverage_declarations.push_back(std::move(item));
        }
        position = *terminator + 1U;
    }
}

void VerilogParser::structure_coverpoint_bins(
    SystemVerilogCoverageDeclaration& declaration)
{
    constexpr std::size_t maximum_expanded_bins = 65'536U;
    const std::span<const Token> body { declaration.body_tokens };
    std::unordered_set<std::string> names;
    bool saw_bin_declaration { };
    std::size_t next_bin_index { };
    for (auto statement : split_top_level(body, TokenKind::Semicolon)) {
        if (statement.empty())
            continue;
        const bool wildcard_declaration = statement.front().text == "wildcard";
        const std::size_t keyword_index = wildcard_declaration ? 1U : 0U;
        if (keyword_index >= statement.size())
            continue;
        const auto keyword = statement[keyword_index].text;
        if (keyword != "bins" && keyword != "ignore_bins"
            && keyword != "illegal_bins") {
            continue;
        }
        saw_bin_declaration = true;
        const auto name_index = keyword_index + 1U;
        const auto assignment = find_top_level(statement, TokenKind::Assign);
        if (name_index >= statement.size()
            || statement[name_index].kind != TokenKind::Identifier
            || !assignment || *assignment <= name_index
            || *assignment + 1U == statement.size()) {
            error(
                statement.front(),
                "FSIM-SV-PARSE-319",
                "a coverpoint bin requires 'bins name = selection'");
            continue;
        }
        const auto source_name = statement[name_index].text;
        if (!names.insert(source_name).second) {
            error(
                statement[name_index],
                "FSIM-SV-SEM-212",
                "duplicate coverpoint bin name '" + source_name + "'");
            continue;
        }

        bool arrayed { };
        std::optional<std::size_t> declared_array_size;
        const auto declarator = std::span<const Token> { statement }.subspan(
            name_index + 1U, *assignment - name_index - 1U);
        if (!declarator.empty()) {
            if (declarator.front().kind != TokenKind::LeftBracket
                || declarator.back().kind != TokenKind::RightBracket) {
                error(
                    statement[name_index],
                    "FSIM-SV-PARSE-320",
                    "a bin array declarator must be empty or contain a positive size");
                continue;
            }
            arrayed = true;
            const auto extent = declarator.subspan(1U, declarator.size() - 2U);
            if (!extent.empty()) {
                const auto literal = coverage_literal(extent);
                if (!literal || literal->wildcard || !literal->exact
                    || *literal->exact <= 0
                    || static_cast<std::uint64_t>(*literal->exact)
                        > maximum_expanded_bins) {
                    error(
                        declarator.front(),
                        "FSIM-SV-SEM-213",
                        "a bin array size must be within 1..65536");
                    continue;
                }
                declared_array_size = static_cast<std::size_t>(*literal->exact);
            }
        }

        auto rhs = std::span<const Token> { statement }.subspan(
            *assignment + 1U);
        SystemVerilogCoverageBin bin;
        bin.kind = keyword == "ignore_bins"
            ? SystemVerilogCoverageBinKind::Ignore
            : keyword == "illegal_bins"
            ? SystemVerilogCoverageBinKind::Illegal
            : SystemVerilogCoverageBinKind::Regular;
        bin.name = source_name;
        bin.source_name = source_name;
        bin.name_token = statement[name_index];
        bin.declared_array_size = declared_array_size;
        bin.wildcard = wildcard_declaration;
        bin.weight = declaration.effective_weight;
        bin.goal = declaration.effective_goal;
        bin.at_least = declaration.effective_at_least;
        bin.span = span_from(statement.front(), statement.back());
        const auto iff = find_top_level_keyword(rhs, "iff");
        if (iff) {
            const auto left = *iff + 1U;
            if (*iff == 0U || left >= rhs.size()
                || rhs[left].kind != TokenKind::LeftParen) {
                error(
                    rhs[*iff],
                    "FSIM-SV-PARSE-322",
                    "a bin iff guard requires a nonempty parenthesized expression");
                continue;
            }
            const auto right = matching_right_parenthesis(rhs, left);
            if (!right || *right + 1U != rhs.size() || *right == left + 1U) {
                error(
                    rhs[*iff],
                    "FSIM-SV-PARSE-322",
                    "a bin iff guard requires a nonempty balanced expression");
                continue;
            }
            bin.iff_tokens.assign(
                rhs.begin() + static_cast<std::ptrdiff_t>(left + 1U),
                rhs.begin() + static_cast<std::ptrdiff_t>(*right));
            bin.iff_span = span_from(rhs[*iff], rhs[*right]);
            rhs = rhs.first(*iff);
        }
        if (const auto matches = find_top_level_keyword(rhs, "matches")) {
            error(
                rhs[*matches],
                "FSIM-SV-PARSE-320",
                "matches is only valid in a cross-bin selection expression");
            continue;
        }
        if (const auto with = find_top_level_keyword(rhs, "with")) {
            const auto left = *with + 1U;
            if (*with == 0U || left >= rhs.size()
                || rhs[left].kind != TokenKind::LeftParen) {
                error(
                    rhs[*with],
                    "FSIM-SV-PARSE-320",
                    "a coverpoint with clause requires a nonempty parenthesized expression");
                continue;
            }
            const auto right = matching_right_parenthesis(rhs, left);
            if (!right || *right + 1U != rhs.size() || *right == left + 1U) {
                error(
                    rhs[*with],
                    "FSIM-SV-PARSE-320",
                    "a coverpoint with clause requires one balanced expression");
                continue;
            }
            bin.with_tokens.assign(
                rhs.begin() + static_cast<std::ptrdiff_t>(left + 1U),
                rhs.begin() + static_cast<std::ptrdiff_t>(*right));
            bin.with_span = span_from(rhs[*with], rhs[*right]);
            rhs = rhs.first(*with);
        }
        if (rhs.size() == 1U && rhs.front().text == "default") {
            if (arrayed || wildcard_declaration) {
                error(
                    rhs.front(),
                    "FSIM-SV-PARSE-320",
                    "default bins cannot be wildcard or arrayed");
                continue;
            }
            bin.selection = SystemVerilogCoverageBinSelection::Default;
        } else if (rhs.size() == 1U
            && rhs.front().kind == TokenKind::Identifier
            && rhs.front().text == declaration.name
            && !bin.with_tokens.empty()) {
            // The coverpoint name denotes its complete value domain. The with
            // predicate is evaluated against the sampled candidate as `item`.
        } else if (
            rhs.size() == 2U && rhs[0].text == "default"
            && rhs[1].text == "sequence") {
            if (arrayed || wildcard_declaration) {
                error(
                    rhs.front(),
                    "FSIM-SV-PARSE-322",
                    "default sequence bins cannot be wildcard or arrayed");
                continue;
            }
            bin.selection = SystemVerilogCoverageBinSelection::DefaultSequence;
        } else if (rhs.front().kind == TokenKind::LeftParen) {
            const auto transitions = structure_transition_sequences(
                rhs, wildcard_declaration);
            if (!transitions) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-321",
                    "a transition bin requires balanced bounded sequences and repetitions");
                continue;
            }
            bin.transitions = *transitions;
        } else if (
            rhs.size() >= 3U && rhs.front().kind == TokenKind::LeftBrace
            && rhs.back().kind == TokenKind::RightBrace) {
            const auto right = matching_right_brace(rhs, 0U);
            if (!right || *right + 1U != rhs.size())
                continue;
            auto values = split_top_level(
                rhs.subspan(1U, rhs.size() - 2U), TokenKind::Comma);
            if (values.empty()
                || std::ranges::any_of(
                    values,
                    [](const std::vector<Token>& value) {
                        return value.empty();
                    })) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-319",
                    "an explicit coverpoint bin requires nonempty values");
                continue;
            }
            bool malformed { };
            for (auto& value_tokens : values) {
                const auto value = coverage_bin_value(
                    value_tokens, wildcard_declaration);
                if (!value) {
                    malformed = true;
                    break;
                }
                bin.values.push_back(*value);
            }
            if (malformed) {
                error(
                    statement.front(),
                    "FSIM-SV-PARSE-320",
                    "a bin value must be a bounded scalar, range, or wildcard literal");
                continue;
            }
        } else {
            error(
                statement.front(),
                "FSIM-SV-PARSE-320",
                "a bin selection must be default, a braced value set, or transitions");
            continue;
        }

        if (!arrayed) {
            bin.declaration_index = next_bin_index++;
            declaration.bins.push_back(std::move(bin));
            continue;
        }

        if (!bin.transitions.empty()) {
            const auto bin_count = declared_array_size.value_or(
                bin.transitions.size());
            if (bin_count == 0U || bin_count > maximum_expanded_bins
                || bin.transitions.size() > maximum_expanded_bins
                || bin_count > bin.transitions.size()) {
                error(
                    statement[name_index],
                    "FSIM-SV-SEM-214",
                    "transition array expansion exceeds 65536 sequences or creates empty bins");
                continue;
            }
            std::size_t sequence_offset { };
            for (std::size_t array_index = 0; array_index < bin_count;
                ++array_index) {
                const auto remaining_sequences = bin.transitions.size() - sequence_offset;
                const auto remaining_bins = bin_count - array_index;
                const auto sequence_count = (remaining_sequences + remaining_bins - 1U) / remaining_bins;
                auto expanded = bin;
                expanded.name = source_name + "[" + std::to_string(array_index) + "]";
                expanded.array_index = array_index;
                expanded.declaration_index = next_bin_index++;
                expanded.transitions.assign(
                    bin.transitions.begin()
                        + static_cast<std::ptrdiff_t>(sequence_offset),
                    bin.transitions.begin()
                        + static_cast<std::ptrdiff_t>(
                            sequence_offset + sequence_count));
                sequence_offset += sequence_count;
                declaration.bins.push_back(std::move(expanded));
            }
            continue;
        }

        std::vector<SystemVerilogCoverageBinValue> expanded_values;
        for (const auto& value : bin.values) {
            if (!value.range_left || !value.range_right) {
                expanded_values.push_back(value);
                continue;
            }
            const auto ascending = *value.range_left <= *value.range_right;
            auto current = *value.range_left;
            for (;;) {
                SystemVerilogCoverageBinValue expanded = value;
                expanded.exact_value = current;
                expanded.range_left.reset();
                expanded.range_right.reset();
                expanded_values.push_back(std::move(expanded));
                if (expanded_values.size() > maximum_expanded_bins)
                    break;
                if (current == *value.range_right)
                    break;
                current += ascending ? 1 : -1;
            }
            if (expanded_values.size() > maximum_expanded_bins)
                break;
        }
        if (!bin.with_tokens.empty()) {
            std::erase_if(
                expanded_values,
                [&](const SystemVerilogCoverageBinValue& value) {
                    if (!value.exact_value && value.exact_bits.empty())
                        return false;
                    const SystemVerilogCoverageSampleValue candidate {
                        value.exact_value.value_or(0),
                        0U,
                        value.width,
                        value.exact_bits,
                        value.exact_unknown_bits,
                        value.exact_signed
                    };
                    return !systemverilog_coverage_with_allows(
                        bin.with_tokens, candidate);
                });
        }
        const auto bin_count = declared_array_size.value_or(
            expanded_values.size());
        if (bin_count == 0U || bin_count > maximum_expanded_bins
            || expanded_values.size() > maximum_expanded_bins
            || bin_count > expanded_values.size()) {
            error(
                statement[name_index],
                "FSIM-SV-SEM-213",
                "bin array expansion exceeds 65536 bins/values or creates empty bins");
            continue;
        }
        std::size_t value_offset { };
        for (std::size_t array_index = 0; array_index < bin_count;
            ++array_index) {
            const auto remaining_values = expanded_values.size() - value_offset;
            const auto remaining_bins = bin_count - array_index;
            const auto value_count = (remaining_values + remaining_bins - 1U) / remaining_bins;
            auto expanded = bin;
            expanded.name = source_name + "[" + std::to_string(array_index) + "]";
            expanded.array_index = array_index;
            expanded.declaration_index = next_bin_index++;
            expanded.values.assign(
                expanded_values.begin()
                    + static_cast<std::ptrdiff_t>(value_offset),
                expanded_values.begin()
                    + static_cast<std::ptrdiff_t>(value_offset + value_count));
            value_offset += value_count;
            declaration.bins.push_back(std::move(expanded));
        }
    }
    if (!saw_bin_declaration) {
        SystemVerilogCoverageBin automatic;
        automatic.selection = SystemVerilogCoverageBinSelection::Automatic;
        automatic.name = "$auto";
        automatic.source_name = "$auto";
        automatic.weight = declaration.effective_weight;
        automatic.goal = declaration.effective_goal;
        automatic.at_least = declaration.effective_at_least;
        automatic.span = declaration.body_span.empty()
            ? declaration.expression_span
            : declaration.body_span;
        declaration.bins.push_back(std::move(automatic));
    }
}

void VerilogParser::structure_cross_bins(
    SystemVerilogCoverageDeclaration& declaration)
{
    const std::span<const Token> body { declaration.body_tokens };
    std::unordered_set<std::string> names;
    std::size_t next_bin_index { };
    for (auto statement : split_top_level(body, TokenKind::Semicolon)) {
        if (statement.empty())
            continue;
        const auto keyword = statement.front().text;
        if (keyword != "bins" && keyword != "ignore_bins"
            && keyword != "illegal_bins") {
            continue;
        }
        const auto assignment = find_top_level(statement, TokenKind::Assign);
        if (statement.size() < 4U
            || statement[1].kind != TokenKind::Identifier
            || !assignment || *assignment != 2U
            || *assignment + 1U == statement.size()) {
            error(
                statement.front(),
                "FSIM-SV-PARSE-323",
                "an explicit cross bin requires 'bins name = selection'");
            continue;
        }
        if (!names.insert(statement[1].text).second) {
            error(
                statement[1],
                "FSIM-SV-SEM-215",
                "duplicate explicit cross bin name '" + statement[1].text + "'");
            continue;
        }
        const auto selection = std::span<const Token> { statement }.subspan(
            *assignment + 1U);
        SystemVerilogCoverageBin bin;
        bin.kind = keyword == "ignore_bins"
            ? SystemVerilogCoverageBinKind::Ignore
            : keyword == "illegal_bins"
            ? SystemVerilogCoverageBinKind::Illegal
            : SystemVerilogCoverageBinKind::Regular;
        bin.name = statement[1].text;
        bin.source_name = bin.name;
        bin.name_token = statement[1];
        bin.declaration_index = next_bin_index++;
        bin.weight = declaration.effective_weight;
        bin.goal = declaration.effective_goal;
        bin.at_least = declaration.effective_at_least;
        bin.cross_selection_tokens.assign(
            statement.begin() + static_cast<std::ptrdiff_t>(*assignment + 1U),
            statement.end());
        bin.span = span_from(statement.front(), statement.back());
        declaration.bins.push_back(std::move(bin));
    }
}

void VerilogParser::structure_coverage_options(
    SystemVerilogCoverageDeclaration& declaration)
{
    const std::span<const Token> body { declaration.body_tokens };
    for (const auto& statement : split_top_level(body, TokenKind::Semicolon)) {
        if (statement.empty()
            || (statement.front().text != "option"
                && statement.front().text != "type_option")) {
            continue;
        }
        if (statement.size() < 5U
            || statement[1].kind != TokenKind::Dot
            || statement[2].kind != TokenKind::Identifier
            || statement[3].kind != TokenKind::Assign) {
            error(
                statement.front(),
                "FSIM-SV-SEM-217",
                "a coverage option requires 'option.name = bounded_integer'");
            continue;
        }
        const auto value_tokens = std::span<const Token> { statement }.subspan(4U);
        const auto literal = coverage_literal(value_tokens);
        const auto name = statement[2].text;
        const bool valid_name = name == "weight" || name == "goal"
            || name == "at_least";
        const bool valid_value = literal && !literal->wildcard && literal->exact
            && *literal->exact >= 0
            && (name != "at_least" || *literal->exact >= 1)
            && (name != "goal" || *literal->exact <= 100)
            && (name != "weight" || *literal->exact <= 65'536);
        if (!valid_name || !valid_value) {
            error(
                statement[2],
                "FSIM-SV-SEM-217",
                "coverage weight, goal, and at_least require bounded legal values");
            continue;
        }
        SystemVerilogCovergroupOptionAssignment option;
        option.scope = statement.front().text == "type_option"
            ? SystemVerilogCovergroupOptionScope::Type
            : SystemVerilogCovergroupOptionScope::Instance;
        option.name = name;
        option.name_token = statement[2];
        option.name_span = statement[2].span;
        option.value_tokens.assign(value_tokens.begin(), value_tokens.end());
        option.span = span_from(statement.front(), statement.back());
        declaration.option_assignments.push_back(std::move(option));
    }
    const auto apply_scope = [&](const SystemVerilogCovergroupOptionScope scope) {
        for (const auto& option : declaration.option_assignments) {
            if (option.scope != scope)
                continue;
            const auto literal = coverage_literal(option.value_tokens);
            if (!literal || !literal->exact)
                continue;
            if (option.name == "weight") {
                declaration.effective_weight = static_cast<std::uint32_t>(*literal->exact);
            } else if (option.name == "goal") {
                declaration.effective_goal = static_cast<std::uint32_t>(*literal->exact);
            } else {
                declaration.effective_at_least = static_cast<std::uint64_t>(*literal->exact);
            }
        }
    };
    apply_scope(SystemVerilogCovergroupOptionScope::Type);
    apply_scope(SystemVerilogCovergroupOptionScope::Instance);
}

void VerilogParser::add_covergroup_declaration(
    std::vector<SystemVerilogCovergroupDeclaration>& declarations,
    SystemVerilogCovergroupDeclaration declaration,
    const Token& start)
{
    if (declaration.name.empty()) {
        return;
    }
    const auto duplicate = std::ranges::any_of(
        declarations,
        [&](const SystemVerilogCovergroupDeclaration& existing) {
            return existing.name == declaration.name;
        });
    if (duplicate) {
        error(
            start,
            "FSIM-SV-SEM-205",
            "duplicate covergroup declaration '" + declaration.name + "'");
        return;
    }
    declarations.push_back(std::move(declaration));
}

} // namespace fsim::frontend
