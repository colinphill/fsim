// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <bit>
#include <iomanip>
#include <sstream>

namespace fsim::elaboration::elaboration_detail {
namespace {

using Value = SystemVerilogConstantValue;

[[nodiscard]] std::uint64_t width_mask(const std::uint32_t width) noexcept {
    return width >= 64
        ? std::numeric_limits<std::uint64_t>::max()
        : (std::uint64_t{1} << width) - 1U;
}

void normalize(Value& value) noexcept {
    value.width = std::clamp(value.width, std::uint32_t{1}, std::uint32_t{64});
    const auto mask = width_mask(value.width);
    value.bits &= mask;
    value.unknown_bits &= mask;
    value.high_impedance_bits &= value.unknown_bits;
    value.bits &= ~value.unknown_bits;
}

void append_packed(Value& destination, const Value& operand) noexcept {
    if (operand.width >= 64U) {
        destination.bits = operand.bits & operand.mask();
        destination.unknown_bits =
            operand.unknown_bits & operand.mask();
        destination.high_impedance_bits =
            operand.high_impedance_bits & operand.mask();
        return;
    }
    destination.bits =
        (destination.bits << operand.width)
        | (operand.bits & operand.mask());
    destination.unknown_bits =
        (destination.unknown_bits << operand.width)
        | (operand.unknown_bits & operand.mask());
    destination.high_impedance_bits =
        (destination.high_impedance_bits << operand.width)
        | (operand.high_impedance_bits & operand.mask());
}

[[nodiscard]] Value make_known(
    const std::uint64_t bits,
    const std::uint32_t width,
    const bool is_signed,
    const bool unsized,
    const frontend::SourceSpan& source) {
    Value result{bits, 0, 0, width, is_signed, unsized, source};
    normalize(result);
    return result;
}

[[nodiscard]] Value make_unknown(
    const std::uint32_t width,
    const bool is_signed,
    const frontend::SourceSpan& source) {
    Value result{
        0,
        width_mask(width),
        0,
        width,
        is_signed,
        false,
        source};
    normalize(result);
    return result;
}

[[nodiscard]] std::string cleaned_digits(const std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char character : text) {
        if (character != '_') {
            result.push_back(character);
        }
    }
    return result;
}

[[nodiscard]] std::optional<std::uint64_t> parse_unsigned(
    const std::string_view text,
    const int base) {
    const auto cleaned = cleaned_digits(text);
    if (cleaned.empty()) {
        return std::nullopt;
    }
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(
        cleaned.data(), cleaned.data() + cleaned.size(), result, base);
    if (parsed.ec != std::errc{}
        || parsed.ptr != cleaned.data() + cleaned.size()) {
        return std::nullopt;
    }
    return result;
}

[[nodiscard]] std::uint32_t minimum_unsigned_width(
    const std::uint64_t value) noexcept {
    return value == 0
        ? 1U
        : static_cast<std::uint32_t>(64U - std::countl_zero(value));
}

[[nodiscard]] std::optional<Value> parse_based_literal(
    const Expression& expression,
    std::string& error) {
    const auto quote = expression.text.find('\'');
    if (quote == std::string::npos) {
        return std::nullopt;
    }
    const auto width_text =
        std::string_view{expression.text}.substr(0, quote);
    auto suffix =
        std::string_view{expression.text}.substr(quote + 1U);
    bool explicitly_signed = false;
    if (!suffix.empty()
        && (suffix.front() == 's' || suffix.front() == 'S')) {
        explicitly_signed = true;
        suffix.remove_prefix(1);
    }
    if (width_text.empty() && suffix.size() == 1U) {
        const auto digit = static_cast<char>(
            std::tolower(static_cast<unsigned char>(suffix.front())));
        if (digit == '0' || digit == '1'
            || digit == 'x' || digit == 'z' || digit == '?') {
            Value result{
                digit == '1' ? 1U : 0U,
                digit == 'x' || digit == 'z' || digit == '?' ? 1U : 0U,
                digit == 'z' || digit == '?' ? 1U : 0U,
                1,
                false,
                true,
                expression.span};
            normalize(result);
            return result;
        }
    }
    if (suffix.size() < 2U) {
        error = "SystemVerilog based literal is missing a base or digits";
        return std::nullopt;
    }
    const auto base_character = static_cast<char>(
        std::tolower(static_cast<unsigned char>(suffix.front())));
    suffix.remove_prefix(1);
    std::uint32_t digit_width = 0;
    int numeric_base = 0;
    switch (base_character) {
    case 'b':
        digit_width = 1;
        numeric_base = 2;
        break;
    case 'o':
        digit_width = 3;
        numeric_base = 8;
        break;
    case 'd':
        numeric_base = 10;
        break;
    case 'h':
        digit_width = 4;
        numeric_base = 16;
        break;
    default:
        error = "SystemVerilog based literal uses an unsupported base";
        return std::nullopt;
    }

    std::optional<std::uint64_t> explicit_width;
    if (!width_text.empty()) {
        explicit_width = parse_unsigned(width_text, 10);
        if (!explicit_width || *explicit_width == 0 || *explicit_width > 64) {
            error = "SystemVerilog constant width must be from 1 through 64";
            return std::nullopt;
        }
    }
    const auto digits = cleaned_digits(suffix);
    if (digits.empty()) {
        error = "SystemVerilog based literal has no digits";
        return std::nullopt;
    }

    if (numeric_base == 10) {
        if (std::ranges::any_of(
                digits,
                [](const char character) {
                    const auto folded = static_cast<char>(
                        std::tolower(
                            static_cast<unsigned char>(character)));
                    return folded == 'x' || folded == 'z'
                        || folded == '?';
                })) {
            const auto width = explicit_width
                ? static_cast<std::uint32_t>(*explicit_width)
                : 32U;
            return make_unknown(
                width, explicitly_signed, expression.span);
        }
        const auto parsed = parse_unsigned(digits, 10);
        if (!parsed) {
            error = "SystemVerilog decimal based literal is not representable";
            return std::nullopt;
        }
        const auto width = explicit_width
            ? static_cast<std::uint32_t>(*explicit_width)
            : std::max(32U, minimum_unsigned_width(*parsed));
        return make_known(
            *parsed,
            width,
            explicitly_signed,
            !explicit_width.has_value(),
            expression.span);
    }

    const auto raw_width =
        static_cast<std::uint64_t>(digits.size()) * digit_width;
    const auto selected_width = explicit_width.value_or(
        std::max<std::uint64_t>(32U, raw_width));
    if (selected_width == 0 || selected_width > 64 || raw_width > 64) {
        error = "SystemVerilog based literal exceeds the bounded 64-bit width";
        return std::nullopt;
    }
    Value result{
        0,
        0,
        0,
        static_cast<std::uint32_t>(selected_width),
        explicitly_signed,
        !explicit_width.has_value(),
        expression.span};
    std::uint32_t output_bit = 0;
    for (auto digit = digits.rbegin();
         digit != digits.rend() && output_bit < 64;
         ++digit) {
        const auto folded = static_cast<char>(
            std::tolower(static_cast<unsigned char>(*digit)));
        if (folded == 'x' || folded == 'z' || folded == '?') {
            for (std::uint32_t bit = 0;
                 bit < digit_width && output_bit < 64;
                 ++bit, ++output_bit) {
                result.unknown_bits |= std::uint64_t{1} << output_bit;
                if (folded == 'z' || folded == '?') {
                    result.high_impedance_bits |=
                        std::uint64_t{1} << output_bit;
                }
            }
            continue;
        }
        unsigned value = 0;
        if (folded >= '0' && folded <= '9') {
            value = static_cast<unsigned>(folded - '0');
        } else if (folded >= 'a' && folded <= 'f') {
            value = 10U + static_cast<unsigned>(folded - 'a');
        } else {
            error = "SystemVerilog based literal contains an invalid digit";
            return std::nullopt;
        }
        if (value >= static_cast<unsigned>(numeric_base)) {
            error = "SystemVerilog based literal digit is outside its base";
            return std::nullopt;
        }
        for (std::uint32_t bit = 0;
             bit < digit_width && output_bit < 64;
             ++bit, ++output_bit) {
            if (((value >> bit) & 1U) != 0) {
                result.bits |= std::uint64_t{1} << output_bit;
            }
        }
    }
    normalize(result);
    return result;
}

[[nodiscard]] Value resized(
    Value value,
    const std::uint32_t width) noexcept {
    normalize(value);
    if (width > value.width && value.is_signed) {
        const auto sign = std::uint64_t{1} << (value.width - 1U);
        const auto extension = width_mask(width) & ~width_mask(value.width);
        if ((value.unknown_bits & sign) != 0) {
            value.unknown_bits |= extension;
            if ((value.high_impedance_bits & sign) != 0) {
                value.high_impedance_bits |= extension;
            }
        } else if ((value.bits & sign) != 0) {
            value.bits |= extension;
        }
    }
    value.width = width;
    normalize(value);
    return value;
}

[[nodiscard]] Value common_operand(
    Value value,
    const std::uint32_t width,
    const bool common_signed) noexcept {
    value = resized(std::move(value), width);
    value.is_signed = common_signed;
    return value;
}

enum class Truth { False, True, Unknown };

[[nodiscard]] Truth truth(const Value& value) noexcept {
    const auto known_one = value.bits & ~value.unknown_bits & value.mask();
    if (known_one != 0) {
        return Truth::True;
    }
    return value.known() ? Truth::False : Truth::Unknown;
}

[[nodiscard]] Value logical_result(
    const Truth value,
    const frontend::SourceSpan& source) {
    if (value == Truth::Unknown) {
        return make_unknown(1, false, source);
    }
    return make_known(
        value == Truth::True ? 1U : 0U,
        1,
        false,
        false,
        source);
}

[[nodiscard]] std::optional<std::uint64_t> nonnegative_count(
    const Value& value,
    std::string& error) {
    if (!value.known()) {
        error = "constant count contains X or Z";
        return std::nullopt;
    }
    if (value.is_signed) {
        const auto converted = value.integer_value();
        if (!converted || *converted < 0) {
            error = "constant count must be nonnegative";
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(*converted);
    }
    return value.bits & value.mask();
}

[[nodiscard]] Value bitwise(
    const Value& left,
    const Value& right,
    const std::string_view operation,
    const frontend::SourceSpan& source) {
    const auto width = std::max(left.width, right.width);
    const bool is_signed = left.is_signed && right.is_signed;
    const auto lhs = common_operand(left, width, is_signed);
    const auto rhs = common_operand(right, width, is_signed);
    Value result{0, 0, 0, width, is_signed, false, source};
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        const auto mask = std::uint64_t{1} << bit;
        const bool lhs_unknown = (lhs.unknown_bits & mask) != 0;
        const bool rhs_unknown = (rhs.unknown_bits & mask) != 0;
        const bool lhs_one = (lhs.bits & mask) != 0;
        const bool rhs_one = (rhs.bits & mask) != 0;
        bool unknown = false;
        bool one = false;
        if (operation == "&") {
            unknown =
                (lhs_unknown && (rhs_unknown || rhs_one))
                || (rhs_unknown && lhs_one);
            one = !unknown && lhs_one && rhs_one;
        } else if (operation == "|") {
            unknown =
                (lhs_unknown && (rhs_unknown || !rhs_one))
                || (rhs_unknown && !lhs_one);
            one = !unknown && (lhs_one || rhs_one);
        } else {
            unknown = lhs_unknown || rhs_unknown;
            one = !unknown && (lhs_one != rhs_one);
            if (operation == "~^" || operation == "^~") {
                one = !unknown && !one;
            }
        }
        if (unknown) {
            result.unknown_bits |= mask;
        } else if (one) {
            result.bits |= mask;
        }
    }
    normalize(result);
    return result;
}

[[nodiscard]] int compare_known(
    const Value& left,
    const Value& right) noexcept {
    const auto width = std::max(left.width, right.width);
    const bool signed_comparison = left.is_signed && right.is_signed;
    const auto lhs = common_operand(left, width, signed_comparison);
    const auto rhs = common_operand(right, width, signed_comparison);
    if (signed_comparison) {
        const auto lhs_value = lhs.integer_value().value_or(0);
        const auto rhs_value = rhs.integer_value().value_or(0);
        return lhs_value < rhs_value ? -1 : lhs_value > rhs_value ? 1 : 0;
    }
    const auto lhs_value = lhs.bits & lhs.mask();
    const auto rhs_value = rhs.bits & rhs.mask();
    return lhs_value < rhs_value ? -1 : lhs_value > rhs_value ? 1 : 0;
}

[[nodiscard]] Truth wildcard_equal(
    const Value& left,
    const Value& right) noexcept {
    const auto width = std::max(left.width, right.width);
    const bool common_signed = left.is_signed && right.is_signed;
    const auto lhs = common_operand(left, width, common_signed);
    const auto rhs = common_operand(right, width, common_signed);
    const auto compared = rhs.mask() & ~rhs.unknown_bits;
    if ((lhs.unknown_bits & compared) != 0) {
        return Truth::Unknown;
    }
    return ((lhs.bits ^ rhs.bits) & compared) == 0
        ? Truth::True : Truth::False;
}

[[nodiscard]] Truth logical_and(
    const Truth lhs,
    const Truth rhs) noexcept {
    if (lhs == Truth::False || rhs == Truth::False) {
        return Truth::False;
    }
    return lhs == Truth::True && rhs == Truth::True
        ? Truth::True : Truth::Unknown;
}

[[nodiscard]] Truth relational(
    const Value& lhs,
    const Value& rhs,
    const bool less_equal) noexcept {
    if (!lhs.known() || !rhs.known()) {
        return Truth::Unknown;
    }
    const auto comparison = compare_known(lhs, rhs);
    return (less_equal ? comparison <= 0 : comparison >= 0)
        ? Truth::True : Truth::False;
}

[[nodiscard]] std::optional<Value> evaluate_impl(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error);

[[nodiscard]] std::optional<Value> evaluate_binary(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error) {
    auto left = evaluate_impl(
        expression.operands[0], environment, fallback_environment, error);
    if (!left) {
        return std::nullopt;
    }
    if (expression.text == "&&" && truth(*left) == Truth::False) {
        return logical_result(Truth::False, expression.span);
    }
    if (expression.text == "||" && truth(*left) == Truth::True) {
        return logical_result(Truth::True, expression.span);
    }
    auto right = evaluate_impl(
        expression.operands[1], environment, fallback_environment, error);
    if (!right) {
        return std::nullopt;
    }
    if (expression.text == "&&" || expression.text == "||") {
        const auto lhs = truth(*left);
        const auto rhs = truth(*right);
        const auto result =
            expression.text == "&&"
                ? (lhs == Truth::False || rhs == Truth::False
                       ? Truth::False
                       : lhs == Truth::True && rhs == Truth::True
                           ? Truth::True
                           : Truth::Unknown)
                : (lhs == Truth::True || rhs == Truth::True
                       ? Truth::True
                       : lhs == Truth::False && rhs == Truth::False
                           ? Truth::False
                           : Truth::Unknown);
        return logical_result(result, expression.span);
    }
    if (expression.text == "&" || expression.text == "|"
        || expression.text == "^" || expression.text == "~^"
        || expression.text == "^~") {
        return bitwise(*left, *right, expression.text, expression.span);
    }
    if (expression.text == "==" || expression.text == "!="
        || expression.text == "===" || expression.text == "!==") {
        const bool case_equality =
            expression.text == "===" || expression.text == "!==";
        if (!case_equality && (!left->known() || !right->known())) {
            return logical_result(Truth::Unknown, expression.span);
        }
        const auto width = std::max(left->width, right->width);
        const bool common_signed = left->is_signed && right->is_signed;
        const auto lhs = common_operand(*left, width, common_signed);
        const auto rhs = common_operand(*right, width, common_signed);
        bool equal =
            lhs.bits == rhs.bits
            && (!case_equality
                || (lhs.unknown_bits == rhs.unknown_bits
                    && lhs.high_impedance_bits
                        == rhs.high_impedance_bits));
        if (expression.text == "!=" || expression.text == "!==") {
            equal = !equal;
        }
        return logical_result(
            equal ? Truth::True : Truth::False, expression.span);
    }
    if (expression.text == "<" || expression.text == "<="
        || expression.text == ">" || expression.text == ">=") {
        if (!left->known() || !right->known()) {
            return logical_result(Truth::Unknown, expression.span);
        }
        const auto comparison = compare_known(*left, *right);
        const bool result =
            expression.text == "<" ? comparison < 0
            : expression.text == "<=" ? comparison <= 0
            : expression.text == ">" ? comparison > 0
                                     : comparison >= 0;
        return logical_result(
            result ? Truth::True : Truth::False, expression.span);
    }
    if (expression.text == "<<" || expression.text == "<<<"
        || expression.text == ">>" || expression.text == ">>>") {
        const auto count = nonnegative_count(*right, error);
        if (!count) {
            return std::nullopt;
        }
        Value result = *left;
        result.source = expression.span;
        if (*count >= result.width) {
            const bool arithmetic =
                expression.text == ">>>" && result.is_signed;
            const auto sign = std::uint64_t{1} << (result.width - 1U);
            if (arithmetic && (result.unknown_bits & sign) != 0) {
                result.bits = 0;
                result.unknown_bits = result.mask();
                result.high_impedance_bits = 0;
            } else if (arithmetic && (result.bits & sign) != 0) {
                result.bits = result.mask();
                result.unknown_bits = 0;
                result.high_impedance_bits = 0;
            } else {
                result.bits = 0;
                result.unknown_bits = 0;
                result.high_impedance_bits = 0;
            }
            return result;
        }
        const auto amount = static_cast<unsigned>(*count);
        if (expression.text == "<<" || expression.text == "<<<") {
            result.bits <<= amount;
            result.unknown_bits <<= amount;
            result.high_impedance_bits <<= amount;
        } else {
            const bool arithmetic =
                expression.text == ">>>" && result.is_signed;
            const auto sign = std::uint64_t{1} << (result.width - 1U);
            result.bits >>= amount;
            result.unknown_bits >>= amount;
            result.high_impedance_bits >>= amount;
            if (arithmetic && amount != 0) {
                const auto extension =
                    result.mask()
                    & ~width_mask(result.width - amount);
                if ((left->unknown_bits & sign) != 0) {
                    result.unknown_bits |= extension;
                } else if ((left->bits & sign) != 0) {
                    result.bits |= extension;
                }
            }
        }
        normalize(result);
        return result;
    }

    const auto width = std::max(left->width, right->width);
    const bool common_signed = left->is_signed && right->is_signed;
    const auto lhs = common_operand(*left, width, common_signed);
    const auto rhs = common_operand(*right, width, common_signed);
    if (!lhs.known() || !rhs.known()) {
        return make_unknown(width, common_signed, expression.span);
    }
    Value result =
        make_known(0, width, common_signed, false, expression.span);
    if (expression.text == "+") {
        result.bits = lhs.bits + rhs.bits;
    } else if (expression.text == "-") {
        result.bits = lhs.bits - rhs.bits;
    } else if (expression.text == "*") {
        result.bits = lhs.bits * rhs.bits;
    } else if (expression.text == "**") {
        const auto exponent = rhs.integer_value();
        if (!exponent) {
            error =
                "constant exponent is outside the bounded signed 64-bit "
                "range";
            return std::nullopt;
        }
        if (*exponent < 0) {
            const auto base = lhs.integer_value();
            if (!base || *base == 0) {
                error = "constant zero to a negative power is undefined";
                return std::nullopt;
            }
            result.bits =
                *base == 1 ? 1U
                : *base == -1
                    ? ((*exponent & 1) != 0 ? result.mask() : 1U)
                    : 0U;
        } else {
            std::uint64_t product = 1;
            auto factor = lhs.bits & lhs.mask();
            auto remaining = static_cast<std::uint64_t>(*exponent);
            while (remaining != 0) {
                if ((remaining & 1U) != 0) {
                    product *= factor;
                }
                remaining >>= 1U;
                if (remaining != 0) {
                    factor *= factor;
                }
            }
            result.bits = product;
        }
    } else if (expression.text == "/" || expression.text == "%") {
        if ((rhs.bits & rhs.mask()) == 0) {
            error = "constant division by zero";
            return std::nullopt;
        }
        if (common_signed) {
            const auto lhs_value = lhs.integer_value().value_or(0);
            const auto rhs_value = rhs.integer_value().value_or(0);
            if (lhs_value == std::numeric_limits<std::int64_t>::min()
                && rhs_value == -1) {
                error = "constant division overflows signed 64-bit range";
                return std::nullopt;
            }
            result.bits = static_cast<std::uint64_t>(
                expression.text == "/"
                    ? lhs_value / rhs_value
                    : lhs_value % rhs_value);
        } else {
            result.bits =
                expression.text == "/"
                    ? lhs.bits / rhs.bits
                    : lhs.bits % rhs.bits;
        }
    } else {
        error =
            "unsupported SystemVerilog binary constant operator '"
            + expression.text + "'";
        return std::nullopt;
    }
    normalize(result);
    return result;
}

[[nodiscard]] std::optional<Value> evaluate_impl(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error) {
    if (expression.kind == ExpressionKind::BooleanLiteral) {
        if (expression.text == "true" || expression.text == "false") {
            return make_known(
                expression.text == "true" ? 1U : 0U,
                1,
                false,
                false,
                expression.span);
        }
        error = "Boolean literal is malformed";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        const auto parsed = parse_unsigned(expression.text, 10);
        if (!parsed
            || *parsed > static_cast<std::uint64_t>(
                              std::numeric_limits<std::int64_t>::max())) {
            error =
                "unsized decimal literal requires more than the bounded "
                "signed 64-bit representation";
            return std::nullopt;
        }
        return make_known(
            *parsed,
            std::max(32U, minimum_unsigned_width(*parsed) + 1U),
            true,
            true,
            expression.span);
    }
    if (expression.kind == ExpressionKind::LogicLiteral) {
        return parse_based_literal(expression, error);
    }
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto found = environment.find(expression.text);
            found != environment.end()) {
            auto result = found->second;
            result.source = expression.span;
            return result;
        }
        if (const auto fallback =
                fallback_environment.find(expression.text);
            fallback != fallback_environment.end()) {
            return make_known(
                static_cast<std::uint64_t>(fallback->second),
                64,
                true,
                false,
                expression.span);
        }
        error =
            "unknown or forward parameter reference '"
            + expression.text + "'";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1U) {
        auto operand = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!operand) {
            return std::nullopt;
        }
        operand->source = expression.span;
        if (expression.text == "+") {
            return operand;
        }
        if (expression.text == "!") {
            const auto value = truth(*operand);
            return logical_result(
                value == Truth::True ? Truth::False
                : value == Truth::False ? Truth::True
                                        : Truth::Unknown,
                expression.span);
        }
        if (expression.text == "~") {
            operand->bits = ~operand->bits;
            normalize(*operand);
            return operand;
        }
        if (expression.text == "-") {
            if (!operand->known()) {
                return make_unknown(
                    operand->width,
                    operand->is_signed,
                    expression.span);
            }
            operand->bits = (~operand->bits) + 1U;
            normalize(*operand);
            return operand;
        }
        if (expression.text == "&" || expression.text == "|"
            || expression.text == "^" || expression.text == "~&"
            || expression.text == "~|" || expression.text == "~^"
            || expression.text == "^~") {
            Truth reduced = Truth::False;
            if (expression.text == "&" || expression.text == "~&") {
                reduced = Truth::True;
                for (std::uint32_t bit = 0; bit < operand->width; ++bit) {
                    const auto mask = std::uint64_t{1} << bit;
                    if ((operand->unknown_bits & mask) != 0) {
                        if (reduced == Truth::True) {
                            reduced = Truth::Unknown;
                        }
                    } else if ((operand->bits & mask) == 0) {
                        reduced = Truth::False;
                        break;
                    }
                }
            } else if (expression.text == "|" || expression.text == "~|") {
                reduced = truth(*operand);
            } else if (!operand->known()) {
                reduced = Truth::Unknown;
            } else {
                reduced =
                    (std::popcount(operand->bits & operand->mask()) & 1U) != 0
                        ? Truth::True
                        : Truth::False;
            }
            if (expression.text.starts_with('~')) {
                reduced =
                    reduced == Truth::True ? Truth::False
                    : reduced == Truth::False ? Truth::True
                                              : Truth::Unknown;
            }
            return logical_result(reduced, expression.span);
        }
        error =
            "unsupported SystemVerilog unary constant operator '"
            + expression.text + "'";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2U) {
        return evaluate_binary(
            expression, environment, fallback_environment, error);
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "$signed"
            || expression.text == "$unsigned")) {
        if (expression.operands.size() != 1U) {
            error = expression.text + " requires exactly one argument";
            return std::nullopt;
        }
        auto result = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (result) {
            result->is_signed = expression.text == "$signed";
            result->source = expression.span;
        }
        return result;
    }
    if (expression.kind == ExpressionKind::Call
        && (expression.text == "@stream-left"
            || expression.text == "@stream-right")) {
        if (expression.operands.size() < 2U) {
            error =
                "streaming concatenation requires a slice size and at "
                "least one operand";
            return std::nullopt;
        }
        const auto slice = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!slice) {
            return std::nullopt;
        }
        const auto slice_size = nonnegative_count(*slice, error);
        if (!slice_size || *slice_size == 0U || *slice_size > 64U) {
            if (slice_size) {
                error =
                    "streaming concatenation slice size must be from 1 "
                    "through 64";
            }
            return std::nullopt;
        }

        std::vector<Value> operands;
        std::uint64_t width = 0;
        for (std::size_t index = 1;
             index < expression.operands.size(); ++index) {
            auto operand = evaluate_impl(
                expression.operands[index],
                environment,
                fallback_environment,
                error);
            if (!operand) {
                return std::nullopt;
            }
            if (operand->width > 64U - width) {
                error =
                    "streaming concatenation result exceeds the bounded "
                    "64-bit width";
                return std::nullopt;
            }
            width += operand->width;
            operands.push_back(std::move(*operand));
        }
        if (width == 0U) {
            error = "streaming concatenation requires a nonempty operand";
            return std::nullopt;
        }

        Value ordinary{
            0,
            0,
            0,
            static_cast<std::uint32_t>(width),
            false,
            false,
            expression.span};
        for (const auto& operand : operands) {
            append_packed(ordinary, operand);
        }
        normalize(ordinary);
        if (expression.text == "@stream-right"
            || *slice_size >= width) {
            return ordinary;
        }

        Value result{
            0,
            0,
            0,
            static_cast<std::uint32_t>(width),
            false,
            false,
            expression.span};
        for (std::uint64_t offset = 0; offset < width;) {
            const auto chunk = std::min(*slice_size, width - offset);
            const auto mask = width_mask(
                static_cast<std::uint32_t>(chunk));
            result.bits =
                (result.bits << chunk)
                | ((ordinary.bits >> offset) & mask);
            result.unknown_bits =
                (result.unknown_bits << chunk)
                | ((ordinary.unknown_bits >> offset) & mask);
            result.high_impedance_bits =
                (result.high_impedance_bits << chunk)
                | ((ordinary.high_impedance_bits >> offset) & mask);
            offset += chunk;
        }
        normalize(result);
        return result;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "inside") {
        if (expression.operands.size() < 2U) {
            error = "inside requires a left operand and a nonempty list";
            return std::nullopt;
        }
        const auto left = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!left) {
            return std::nullopt;
        }
        Truth accumulated = Truth::False;
        for (std::size_t index = 1;
             index < expression.operands.size(); ++index) {
            const auto& item = expression.operands[index];
            Truth matched = Truth::False;
            if (item.kind == ExpressionKind::Call
                && item.text == "@inside-range") {
                if (item.operands.size() != 2U) {
                    error = "inside range requires a low and high bound";
                    return std::nullopt;
                }
                const auto low = evaluate_impl(
                    item.operands[0], environment,
                    fallback_environment, error);
                const auto high = evaluate_impl(
                    item.operands[1], environment,
                    fallback_environment, error);
                if (!low || !high) {
                    return std::nullopt;
                }
                if (low->width != left->width
                    || high->width != left->width
                    || low->is_signed != left->is_signed
                    || high->is_signed != left->is_signed) {
                    error =
                        "inside range bounds must exactly match the left "
                        "operand width and signedness";
                    return std::nullopt;
                }
                matched = logical_and(
                    relational(*low, *high, true),
                    logical_and(
                        relational(*left, *low, false),
                        relational(*left, *high, true)));
            } else {
                const auto value = evaluate_impl(
                    item, environment, fallback_environment, error);
                if (!value) {
                    return std::nullopt;
                }
                if (value->width != left->width
                    || value->is_signed != left->is_signed) {
                    error =
                        "inside members must exactly match the left operand "
                        "width and signedness";
                    return std::nullopt;
                }
                matched = wildcard_equal(*left, *value);
            }
            if (matched == Truth::True) {
                return logical_result(Truth::True, expression.span);
            }
            if (matched == Truth::Unknown) {
                accumulated = Truth::Unknown;
            }
        }
        return logical_result(accumulated, expression.span);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$isunknown") {
        if (expression.operands.size() != 1U) {
            error = "$isunknown requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!operand) {
            return std::nullopt;
        }
        return make_known(
            operand->known() ? 0U : 1U,
            1,
            false,
            false,
            expression.span);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "$clog2") {
        if (expression.operands.size() != 1U) {
            error = "$clog2 requires exactly one argument";
            return std::nullopt;
        }
        const auto operand = evaluate_impl(
            expression.operands.front(),
            environment,
            fallback_environment,
            error);
        if (!operand || !operand->known()) {
            if (operand) {
                error = "$clog2 argument contains X or Z";
            }
            return std::nullopt;
        }
        if (operand->is_signed) {
            const auto signed_value = operand->integer_value();
            if (!signed_value || *signed_value < 0) {
                error =
                    "$clog2 requires a nonnegative integral argument in "
                    "the current executable slice";
                return std::nullopt;
            }
        }
        const auto magnitude = operand->bits & operand->mask();
        const auto result =
            magnitude <= 1U
                ? 0U
                : static_cast<std::uint64_t>(
                      64U - std::countl_zero(magnitude - 1U));
        return make_known(
            result, 32, true, false, expression.span);
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "?:"
        && expression.operands.size() == 3U) {
        const auto condition = evaluate_impl(
            expression.operands[0],
            environment,
            fallback_environment,
            error);
        if (!condition) {
            return std::nullopt;
        }
        auto when_true = evaluate_impl(
            expression.operands[1],
            environment,
            fallback_environment,
            error);
        if (!when_true) {
            return std::nullopt;
        }
        auto when_false = evaluate_impl(
            expression.operands[2],
            environment,
            fallback_environment,
            error);
        if (!when_false) {
            return std::nullopt;
        }
        const auto width = std::max(when_true->width, when_false->width);
        const bool common_signed =
            when_true->is_signed && when_false->is_signed;
        auto lhs = common_operand(*when_true, width, common_signed);
        auto rhs = common_operand(*when_false, width, common_signed);
        if (truth(*condition) == Truth::True) {
            lhs.source = expression.span;
            return lhs;
        }
        if (truth(*condition) == Truth::False) {
            rhs.source = expression.span;
            return rhs;
        }
        Value result{
            lhs.bits & rhs.bits,
            lhs.unknown_bits | rhs.unknown_bits,
            lhs.high_impedance_bits & rhs.high_impedance_bits,
            width,
            common_signed,
            false,
            expression.span};
        result.unknown_bits |=
            (lhs.bits ^ rhs.bits)
            | (lhs.unknown_bits ^ rhs.unknown_bits)
            | (lhs.high_impedance_bits ^ rhs.high_impedance_bits);
        normalize(result);
        return result;
    }
    if (expression.kind == ExpressionKind::Concatenation
        || expression.kind == ExpressionKind::Replication) {
        std::size_t first = 0;
        std::uint64_t repetitions = 1;
        if (expression.kind == ExpressionKind::Replication) {
            if (expression.operands.size() < 2U) {
                error = "replication requires a count and at least one operand";
                return std::nullopt;
            }
            const auto count = evaluate_impl(
                expression.operands.front(),
                environment,
                fallback_environment,
                error);
            if (!count) {
                return std::nullopt;
            }
            const auto converted = nonnegative_count(*count, error);
            if (!converted || *converted == 0) {
                if (converted) {
                    error = "replication count must be positive";
                }
                return std::nullopt;
            }
            repetitions = *converted;
            first = 1;
        }
        if (expression.operands.size() == first) {
            error = "concatenation requires at least one operand";
            return std::nullopt;
        }
        std::vector<Value> operands;
        std::uint64_t element_width = 0;
        for (std::size_t index = first;
             index < expression.operands.size();
             ++index) {
            auto operand = evaluate_impl(
                expression.operands[index],
                environment,
                fallback_environment,
                error);
            if (!operand) {
                return std::nullopt;
            }
            element_width += operand->width;
            operands.push_back(std::move(*operand));
        }
        if (element_width == 0 || repetitions > 64U / element_width) {
            error = "concatenation result exceeds the bounded 64-bit width";
            return std::nullopt;
        }
        Value result{
            0,
            0,
            0,
            static_cast<std::uint32_t>(element_width * repetitions),
            false,
            false,
            expression.span};
        for (std::uint64_t repetition = 0;
             repetition < repetitions;
             ++repetition) {
            for (const auto& operand : operands) {
                append_packed(result, operand);
            }
        }
        normalize(result);
        return result;
    }
    error =
        "expression form is not a supported SystemVerilog integral "
        "constant expression";
    return std::nullopt;
}

} // namespace

std::uint64_t SystemVerilogConstantValue::mask() const noexcept {
    return width_mask(width);
}

bool SystemVerilogConstantValue::known() const noexcept {
    return (unknown_bits & mask()) == 0;
}

std::optional<bool>
SystemVerilogConstantValue::truth_value() const noexcept {
    if ((bits & ~unknown_bits & mask()) != 0) {
        return true;
    }
    return known() ? std::optional<bool>{false} : std::nullopt;
}

std::optional<std::int64_t>
SystemVerilogConstantValue::integer_value() const noexcept {
    if (!known()) {
        return std::nullopt;
    }
    const auto value = bits & mask();
    if (!is_signed) {
        if (value > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(value);
    }
    const auto sign = std::uint64_t{1} << (width - 1U);
    if ((value & sign) == 0) {
        return static_cast<std::int64_t>(value);
    }
    const auto extended = value | ~mask();
    const auto magnitude = (~extended) + 1U;
    if (magnitude == (std::uint64_t{1} << 63U)) {
        return std::numeric_limits<std::int64_t>::min();
    }
    return -static_cast<std::int64_t>(magnitude);
}

std::string SystemVerilogConstantValue::display() const {
    if (known()) {
        if (is_signed) {
            return std::to_string(integer_value().value_or(0));
        }
        return std::to_string(bits & mask());
    }
    return expression(source).text;
}

std::string SystemVerilogConstantValue::canonical() const {
    std::ostringstream stream;
    stream << "svconst-v1:w=" << width
           << ":s=" << (is_signed ? 1 : 0)
           << ":u=" << (unsized ? 1 : 0)
           << ":b=" << std::hex << std::setw(16) << std::setfill('0')
           << (bits & mask())
           << ":x=" << std::setw(16) << (unknown_bits & mask())
           << ":z=" << std::setw(16) << (high_impedance_bits & mask());
    return stream.str();
}

Expression SystemVerilogConstantValue::expression(
    const frontend::SourceSpan& use_span) const {
    if (unsized && known() && is_signed) {
        if (const auto value = integer_value()) {
            return constant_expression(
                *value,
                use_span,
                frontend::ValueDomain::Integer,
                frontend::Language::SystemVerilog2017);
        }
    }
    std::string digits;
    digits.reserve(width);
    for (std::uint32_t offset = 0; offset < width; ++offset) {
        const auto bit = width - offset - 1U;
        const auto bit_mask = std::uint64_t{1} << bit;
        if ((unknown_bits & bit_mask) != 0) {
            digits.push_back(
                (high_impedance_bits & bit_mask) != 0 ? 'z' : 'x');
        } else {
            digits.push_back((bits & bit_mask) != 0 ? '1' : '0');
        }
    }
    return {
        ExpressionKind::LogicLiteral,
        std::to_string(width)
            + (is_signed ? "'sb" : "'b")
            + digits,
        {},
        use_span};
}

std::optional<SystemVerilogConstantValue>
evaluate_systemverilog_constant_expression(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::string& error) {
    error.clear();
    auto result = evaluate_impl(
        expression, environment, fallback_environment, error);
    if (result) {
        normalize(*result);
    }
    return result;
}

std::optional<SystemVerilogConstantValue>
convert_systemverilog_parameter_value(
    const SystemVerilogConstantValue& value,
    const frontend::Type& type,
    std::string& error) {
    auto result = value;
    if (!(type.spelling == "implicit" && type.named_type.empty())) {
        const auto width = type.width();
        if (!width || *width == 0 || *width > 64) {
            error =
                "declared SystemVerilog parameter type does not have a "
                "bounded width from 1 through 64";
            return std::nullopt;
        }
        result = resized(result, static_cast<std::uint32_t>(*width));
        result.is_signed = type.is_signed;
        result.unsized = false;
    }
    const bool two_state =
        type.spelling == "bit" || type.spelling == "byte"
        || type.spelling == "shortint" || type.spelling == "int"
        || type.spelling == "longint";
    if (two_state && !result.known()) {
        error =
            "X or Z bits would be lost converting a constant into "
            "two-state parameter type '" + type.spelling + "'";
        return std::nullopt;
    }
    normalize(result);
    return result;
}

namespace {

void substitute_sv_type(
    frontend::Type& type,
    const SystemVerilogConstantEnvironment& environment) {
    const auto substitute_range =
        [&](auto& range) {
          if (range) {
              substitute_systemverilog_parameters(
                  range->left, environment);
              substitute_systemverilog_parameters(
                  range->right, environment);
          }
        };
    substitute_range(type.packed_range_expression);
    substitute_range(type.enumeration_range_expression);
    substitute_range(type.enumeration_base_range_expression);
    substitute_range(type.integer_range_expression);
    substitute_range(type.integer_base_range_expression);
    substitute_range(type.discrete_range_expression);
    if (type.systemverilog_container) {
        if (type.systemverilog_container->queue_maximum) {
            substitute_systemverilog_parameters(
                *type.systemverilog_container->queue_maximum,
                environment);
        }
        if (auto& ranges =
                type.systemverilog_container
                    ->static_range_expressions;
            !ranges.empty()) {
            substitute_systemverilog_parameters(
                ranges.front().left, environment);
            substitute_systemverilog_parameters(
                ranges.front().right, environment);
        }
        if (type.systemverilog_container
                ->associative_index_type) {
            substitute_sv_type(
                *type.systemverilog_container
                     ->associative_index_type,
                environment);
        }
    }
    for (auto& member : type.packed_members) {
        substitute_range(member.packed_range_expression);
    }
}

void substitute_sv_variable(
    frontend::VariableDeclaration& declaration,
    const SystemVerilogConstantEnvironment& environment) {
    substitute_sv_type(declaration.type, environment);
    if (declaration.initializer) {
        substitute_systemverilog_parameters(
            *declaration.initializer, environment);
    }
}

void substitute_sv_statements(
    std::vector<Statement>& statements,
    const SystemVerilogConstantEnvironment& environment);

void substitute_sv_function(
    frontend::FunctionDeclaration& function,
    const SystemVerilogConstantEnvironment& environment) {
    substitute_sv_type(function.return_type, environment);
    for (auto& argument : function.arguments) {
        substitute_sv_type(argument.type, environment);
        if (argument.default_value) {
            substitute_systemverilog_parameters(
                *argument.default_value, environment);
        }
    }
    for (auto& variable : function.variables) {
        substitute_sv_variable(variable, environment);
    }
    substitute_sv_statements(function.statements, environment);
}

void substitute_sv_task(
    frontend::TaskDeclaration& task,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& argument : task.arguments) {
        substitute_sv_type(argument.type, environment);
        if (argument.default_value) {
            substitute_systemverilog_parameters(
                *argument.default_value, environment);
        }
    }
    for (auto& variable : task.variables) {
        substitute_sv_variable(variable, environment);
    }
    substitute_sv_statements(task.statements, environment);
}

void substitute_sv_statements(
    std::vector<Statement>& statements,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& statement : statements) {
        substitute_systemverilog_parameters(
            statement.target, environment);
        substitute_systemverilog_parameters(
            statement.value, environment);
        substitute_systemverilog_parameters(
            statement.condition, environment);
        for (auto& argument : statement.task_arguments) {
            substitute_systemverilog_parameters(
                argument, environment);
        }
        for (auto& association :
             statement.procedure_arguments) {
            substitute_systemverilog_parameters(
                association.value, environment);
        }
        substitute_systemverilog_parameters(
            statement.loop_initial, environment);
        substitute_systemverilog_parameters(
            statement.loop_limit, environment);
        for (auto& element : statement.vhdl_waveform) {
            substitute_systemverilog_parameters(
                element.value, environment);
        }
        for (auto& output : statement.output_values) {
            substitute_systemverilog_parameters(
                output.value, environment);
        }
        for (auto& declaration : statement.declarations) {
            substitute_sv_variable(declaration, environment);
        }
        for (auto& alternative : statement.case_alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_systemverilog_parameters(
                    choice, environment);
            }
            substitute_sv_statements(
                alternative.statements, environment);
        }
        auto body_environment = environment;
        if (statement.kind == StatementKind::Loop) {
            body_environment.erase(statement.loop_variable);
        }
        substitute_sv_statements(
            statement.statements, body_environment);
        substitute_sv_statements(
            statement.else_statements, environment);
    }
}

void substitute_sv_instances(
    std::vector<frontend::Instance>& instances,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& instance : instances) {
        for (auto& override : instance.parameter_overrides) {
            substitute_systemverilog_parameters(
                override.value, environment);
        }
        for (auto& connection : instance.connections) {
            substitute_systemverilog_parameters(
                connection.value, environment);
        }
    }
}

void substitute_sv_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogConstantEnvironment& environment);

void substitute_sv_generate_body(
    frontend::GenerateBody& body,
    const SystemVerilogConstantEnvironment& environment) {
    auto body_environment = environment;
    for (auto& constant : body.constants) {
        substitute_sv_type(constant.type, body_environment);
        substitute_systemverilog_parameters(
            constant.default_value, body_environment);
    }
    for (auto& signal : body.signals) {
        substitute_sv_type(signal.type, body_environment);
    }
    for (auto& function : body.functions) {
        substitute_sv_function(function, body_environment);
    }
    for (auto& task : body.tasks) {
        substitute_sv_task(task, body_environment);
    }
    substitute_sv_statements(
        body.concurrent_statements, body_environment);
    for (auto& process : body.processes) {
        for (auto& variable : process.variables) {
            substitute_sv_variable(variable, body_environment);
        }
        substitute_sv_statements(
            process.statements, body_environment);
    }
    substitute_sv_instances(body.instances, body_environment);
    substitute_sv_generate_regions(
        body.generate_regions, body_environment);
}

void substitute_sv_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& region : regions) {
        substitute_systemverilog_parameters(
            region.initial, environment);
        auto body_environment = environment;
        if (region.kind == frontend::GenerateKind::Iterative) {
            body_environment.erase(region.variable);
        }
        substitute_systemverilog_parameters(
            region.condition, body_environment);
        substitute_systemverilog_parameters(
            region.iteration, body_environment);
        substitute_sv_generate_body(
            region.then_body, body_environment);
        substitute_sv_generate_body(
            region.else_body, body_environment);
        for (auto& alternative : region.alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_systemverilog_parameters(
                    choice.left, body_environment);
                if (choice.right) {
                    substitute_systemverilog_parameters(
                        *choice.right, body_environment);
                }
            }
            substitute_sv_generate_body(
                alternative.body, body_environment);
        }
    }
}

} // namespace

void substitute_systemverilog_parameters(
    Expression& expression,
    const SystemVerilogConstantEnvironment& environment) {
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto found = environment.find(expression.text);
            found != environment.end()) {
            expression = found->second.expression(expression.span);
            return;
        }
    }
    for (auto& association :
         expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            substitute_systemverilog_parameters(choice, environment);
        }
    }
    for (auto& operand : expression.operands) {
        substitute_systemverilog_parameters(operand, environment);
    }
}

void substitute_systemverilog_parameters(
    DesignUnit& unit,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& parameter : unit.parameters) {
        substitute_sv_type(parameter.type, environment);
        substitute_systemverilog_parameters(
            parameter.default_value, environment);
    }
    for (auto& alias : unit.type_aliases) {
        substitute_sv_type(alias.type, environment);
        for (auto& literal : alias.enum_literals) {
            substitute_systemverilog_parameters(
                literal.value, environment);
        }
    }
    for (auto& port : unit.ports) {
        substitute_sv_type(port.type, environment);
    }
    for (auto& signal : unit.signals) {
        substitute_sv_type(signal.type, environment);
    }
    for (auto& variable : unit.variables) {
        substitute_sv_variable(variable, environment);
    }
    for (auto& function : unit.functions) {
        substitute_sv_function(function, environment);
    }
    for (auto& task : unit.tasks) {
        substitute_sv_task(task, environment);
    }
    substitute_sv_statements(
        unit.concurrent_statements, environment);
    for (auto& process : unit.processes) {
        for (auto& variable : process.variables) {
            substitute_sv_variable(variable, environment);
        }
        substitute_sv_statements(
            process.statements, environment);
    }
    substitute_sv_instances(unit.instances, environment);
    substitute_sv_generate_regions(
        unit.generate_regions, environment);
}

void substitute_systemverilog_parameters(
    frontend::GenerateBody& body,
    const SystemVerilogConstantEnvironment& environment) {
    substitute_sv_generate_body(body, environment);
}

} // namespace fsim::elaboration::elaboration_detail
