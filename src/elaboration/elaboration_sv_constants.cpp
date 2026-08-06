// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <bit>
#include <sstream>

namespace fsim::elaboration::elaboration_detail {
namespace {

using Value = SystemVerilogConstantValue;

inline constexpr std::uint32_t maximum_constant_width =
    16U * 1024U * 1024U;
inline constexpr std::uint64_t maximum_constant_work_units =
    64U * 1024U * 1024U;

[[nodiscard]] std::size_t checked_constant_width(
    const std::uint64_t width) {
    if (width == 0 || width > maximum_constant_width) {
        throw std::length_error{
            "SystemVerilog constant exceeds the 16,777,216-bit resource "
            "limit"};
    }
    return static_cast<std::size_t>(width);
}

[[nodiscard]] std::uint64_t width_mask(const std::uint32_t width) noexcept {
    return width >= 64
        ? std::numeric_limits<std::uint64_t>::max()
        : (std::uint64_t{1} << width) - 1U;
}

void normalize(Value& value) noexcept {
    value.width = std::max(value.width, std::uint32_t{1});
    if (value.width > 64U || value.packed.is_logic9()) {
        value.refresh_low_word_mirrors();
        return;
    }
    const auto mask = width_mask(value.width);
    value.bits &= mask;
    value.unknown_bits &= mask;
    value.high_impedance_bits &= value.unknown_bits;
    value.bits &= ~value.unknown_bits;
    const auto aval =
        value.bits | (value.unknown_bits & ~value.high_impedance_bits);
    value.packed = PackedLogic4::from_aval_bval(
        value.width, aval, value.unknown_bits);
}

void append_packed(Value& destination, const Value& operand) {
    const bool logic9 =
        destination.packed.is_logic9() || operand.packed.is_logic9();
    auto shifted = PackedLogic4{destination.width, Logic4::zero};
    if (logic9) {
        shifted = shifted.promoted_to_logic9();
    }
    const auto retained = destination.width > operand.width
        ? destination.width - operand.width : 0U;
    for (std::uint32_t bit = 0; bit < retained; ++bit) {
        if (logic9) {
            shifted.set_logic9(
                bit + operand.width,
                destination.packed.get_logic9(bit));
        } else {
            shifted.set(
                bit + operand.width, destination.packed.get(bit));
        }
    }
    const auto copied = std::min(operand.width, destination.width);
    for (std::uint32_t bit = 0; bit < copied; ++bit) {
        if (logic9) {
            shifted.set_logic9(bit, operand.packed.get_logic9(bit));
        } else {
            shifted.set(bit, operand.packed.get(bit));
        }
    }
    destination.packed = std::move(shifted);
    destination.refresh_low_word_mirrors();
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
    return Value{
        PackedLogic4{width, Logic4::x},
        is_signed,
        false,
        frontend::ValueDomain::Logic4,
        {},
        source};
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

struct ParsedDecimal {
    PackedLogic4 packed;
    std::uint32_t width{};
};

[[nodiscard]] std::optional<ParsedDecimal> parse_decimal_packed(
    std::string_view digits,
    const std::optional<std::uint32_t> explicit_width,
    const bool reserve_sign,
    std::string& error) {
    while (digits.size() > 1U && digits.front() == '0') {
        digits.remove_prefix(1U);
    }
    if (digits.empty()
        || !std::ranges::all_of(
            digits, [](const char digit) { return digit >= '0' && digit <= '9'; })) {
        error = "SystemVerilog decimal literal contains an invalid digit";
        return std::nullopt;
    }
    if (!explicit_width && digits.size() > maximum_constant_width) {
        error =
            "SystemVerilog decimal literal exceeds the 16,777,216-bit "
            "resource limit";
        return std::nullopt;
    }
    const auto estimated_bits = explicit_width
        ? static_cast<std::uint64_t>(*explicit_width)
        : std::max<std::uint64_t>(
              1U,
              (static_cast<std::uint64_t>(digits.size()) * 3322U
                  + 999U) / 1000U);
    if (estimated_bits > maximum_constant_width) {
        error =
            "SystemVerilog decimal literal exceeds the 16,777,216-bit "
            "resource limit";
        return std::nullopt;
    }
    const auto word_capacity = static_cast<std::size_t>(
        (estimated_bits + 63U) / 64U);
    if (word_capacity != 0U
        && digits.size() > maximum_constant_work_units / word_capacity) {
        error =
            "SystemVerilog decimal literal exceeds the constant-evaluation "
            "work limit";
        return std::nullopt;
    }
    std::vector<std::uint64_t> words(
        explicit_width ? word_capacity : std::size_t{1}, 0U);
    std::size_t used_words = 1U;
    for (const char character : digits) {
        std::uint64_t carry = static_cast<unsigned>(character - '0');
        const auto processed_words = explicit_width
            ? words.size() : used_words;
        for (std::size_t index = 0; index < processed_words; ++index) {
            const auto word = words[index];
            const auto low_product =
                (word & 0xffffffffU) * 10U + carry;
            const auto high_product =
                (word >> 32U) * 10U + (low_product >> 32U);
            words[index] =
                (high_product << 32U)
                | (low_product & 0xffffffffU);
            carry = high_product >> 32U;
        }
        if (!explicit_width && carry != 0U) {
            if (used_words == word_capacity) {
                error =
                    "SystemVerilog decimal literal exceeds the "
                    "16,777,216-bit resource limit";
                return std::nullopt;
            }
            words.push_back(carry);
            ++used_words;
        }
    }
    if (explicit_width && (*explicit_width % 64U) != 0U) {
        words.back() &= width_mask(*explicit_width % 64U);
    }
    while (used_words > 1U && words[used_words - 1U] == 0U) {
        --used_words;
    }
    const auto magnitude_width = words[used_words - 1U] == 0U
        ? 1U
        : static_cast<std::uint32_t>(
            (used_words - 1U) * 64U
            + 64U - std::countl_zero(words[used_words - 1U]));
    const auto selected_width = explicit_width.value_or(
        std::max(
            std::uint32_t{32},
            static_cast<std::uint32_t>(
                magnitude_width + (reserve_sign ? 1U : 0U))));
    if (selected_width > maximum_constant_width) {
        error =
            "SystemVerilog decimal literal exceeds the 16,777,216-bit "
            "resource limit";
        return std::nullopt;
    }
    auto packed = PackedLogic4{selected_width, Logic4::zero};
    for (std::uint32_t bit = 0; bit < selected_width; ++bit) {
        const auto word = static_cast<std::size_t>(bit / 64U);
        if (word < words.size()
            && ((words[word] >> (bit % 64U)) & 1U) != 0U) {
            packed.set(bit, Logic4::one);
        }
    }
    return ParsedDecimal{std::move(packed), selected_width};
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
        if (!explicit_width || *explicit_width == 0
            || *explicit_width > maximum_constant_width) {
            error =
                "SystemVerilog constant width exceeds the 16,777,216-bit "
                "resource limit";
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
        const auto parsed = parse_decimal_packed(
            digits,
            explicit_width
                ? std::optional<std::uint32_t>{
                      static_cast<std::uint32_t>(*explicit_width)}
                : std::nullopt,
            explicitly_signed,
            error);
        if (!parsed) {
            return std::nullopt;
        }
        return Value{
            std::move(parsed->packed),
            explicitly_signed,
            !explicit_width.has_value(),
            frontend::ValueDomain::Logic4,
            {},
            expression.span};
    }

    if (digits.size()
        > maximum_constant_width / static_cast<std::size_t>(digit_width)) {
        error =
            "SystemVerilog based literal exceeds the 16,777,216-bit "
            "resource limit";
        return std::nullopt;
    }
    const auto raw_width =
        static_cast<std::uint64_t>(digits.size()) * digit_width;
    const auto selected_width = explicit_width.value_or(
        std::max<std::uint64_t>(32U, raw_width));
    if (selected_width == 0
        || selected_width > maximum_constant_width) {
        error =
            "SystemVerilog based literal exceeds the 16,777,216-bit "
            "resource limit";
        return std::nullopt;
    }
    Value result{
        PackedLogic4{
            static_cast<std::size_t>(selected_width), Logic4::zero},
        explicitly_signed,
        !explicit_width.has_value(),
        frontend::ValueDomain::Logic4,
        {},
        expression.span};
    std::uint32_t output_bit = 0;
    for (auto digit = digits.rbegin();
         digit != digits.rend() && output_bit < selected_width;
         ++digit) {
        const auto folded = static_cast<char>(
            std::tolower(static_cast<unsigned char>(*digit)));
        if (folded == 'x' || folded == 'z' || folded == '?') {
            for (std::uint32_t bit = 0;
                 bit < digit_width && output_bit < selected_width;
                 ++bit, ++output_bit) {
                result.packed.set(
                    output_bit,
                    folded == 'z' || folded == '?'
                        ? Logic4::z : Logic4::x);
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
             bit < digit_width && output_bit < selected_width;
             ++bit, ++output_bit) {
            if (((value >> bit) & 1U) != 0) {
                result.packed.set(output_bit, Logic4::one);
            }
        }
    }
    result.refresh_low_word_mirrors();
    return result;
}

[[nodiscard]] Value resized(
    Value value,
    const std::uint32_t width) {
    const bool width_changed = value.width != width;
    const bool logic9 = value.packed.is_logic9();
    auto resized_value = PackedLogic4{width, Logic4::zero};
    if (logic9) {
        resized_value = resized_value.promoted_to_logic9();
    }
    const auto copied = std::min(value.width, width);
    for (std::uint32_t bit = 0; bit < copied; ++bit) {
        if (logic9) {
            resized_value.set_logic9(bit, value.packed.get_logic9(bit));
        } else {
            resized_value.set(bit, value.packed.get(bit));
        }
    }
    if (width > value.width && value.is_signed) {
        if (logic9) {
            const auto sign = value.packed.get_logic9(value.width - 1U);
            for (auto bit = value.width; bit < width; ++bit) {
                resized_value.set_logic9(bit, sign);
            }
        } else {
            const auto sign = value.packed.get(value.width - 1U);
            for (auto bit = value.width; bit < width; ++bit) {
                resized_value.set(bit, sign);
            }
        }
    }
    value.packed = std::move(resized_value);
    value.width = width;
    if (width_changed) {
        value.packed_range = frontend::PackedRange{
            static_cast<std::int64_t>(width - 1U), 0, true};
    }
    value.refresh_low_word_mirrors();
    return value;
}

[[nodiscard]] Value common_operand(
    Value value,
    const std::uint32_t width,
    const bool common_signed) {
    value = resized(std::move(value), width);
    value.is_signed = common_signed;
    return value;
}

[[nodiscard]] std::optional<std::uint64_t> nonnegative_count(
    const Value& value,
    std::string& error);

[[nodiscard]] bool packed_one(
    const PackedLogic4& value,
    const std::uint32_t bit) noexcept {
    return runtime::to_logic4(value.get_logic9(bit)) == Logic4::one;
}

[[nodiscard]] bool packed_is_zero(
    const PackedLogic4& value,
    const std::uint32_t width) noexcept {
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        if (packed_one(value, bit)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool packed_is_one(
    const PackedLogic4& value,
    const std::uint32_t width) noexcept {
    if (!packed_one(value, 0)) {
        return false;
    }
    for (std::uint32_t bit = 1; bit < width; ++bit) {
        if (packed_one(value, bit)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool packed_is_all_ones(
    const PackedLogic4& value,
    const std::uint32_t width) noexcept {
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        if (!packed_one(value, bit)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool packed_is_signed_minimum(
    const PackedLogic4& value,
    const std::uint32_t width) noexcept {
    if (!packed_one(value, width - 1U)) {
        return false;
    }
    for (std::uint32_t bit = 0; bit + 1U < width; ++bit) {
        if (packed_one(value, bit)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] PackedLogic4 packed_add(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const std::uint32_t width) {
    auto result = PackedLogic4{width, Logic4::zero};
    bool carry = false;
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        const bool lhs = packed_one(left, bit);
        const bool rhs = packed_one(right, bit);
        result.set(bit, lhs ^ rhs ^ carry ? Logic4::one : Logic4::zero);
        carry = (lhs && rhs) || (lhs && carry) || (rhs && carry);
    }
    return result;
}

[[nodiscard]] PackedLogic4 packed_negate(
    const PackedLogic4& value,
    const std::uint32_t width) {
    auto inverted = PackedLogic4{width, Logic4::zero};
    auto one = PackedLogic4{width, Logic4::zero};
    one.set(0, Logic4::one);
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        inverted.set(
            bit, packed_one(value, bit) ? Logic4::zero : Logic4::one);
    }
    return packed_add(inverted, one, width);
}

[[nodiscard]] int packed_compare_unsigned(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const std::uint32_t width) noexcept {
    for (std::uint32_t offset = 0; offset < width; ++offset) {
        const auto bit = width - offset - 1U;
        const bool lhs = packed_one(left, bit);
        const bool rhs = packed_one(right, bit);
        if (lhs != rhs) {
            return lhs ? 1 : -1;
        }
    }
    return 0;
}

[[nodiscard]] PackedLogic4 packed_subtract(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const std::uint32_t width) {
    return packed_add(left, packed_negate(right, width), width);
}

[[nodiscard]] PackedLogic4 packed_multiply(
    const PackedLogic4& left,
    const PackedLogic4& right,
    const std::uint32_t width) {
    auto result = PackedLogic4{width, Logic4::zero};
    for (std::uint32_t rhs_bit = 0; rhs_bit < width; ++rhs_bit) {
        if (!packed_one(right, rhs_bit)) {
            continue;
        }
        bool carry = false;
        for (std::uint32_t lhs_bit = 0;
             lhs_bit + rhs_bit < width;
             ++lhs_bit) {
            const auto result_bit = lhs_bit + rhs_bit;
            const bool accumulated = packed_one(result, result_bit);
            const bool operand = packed_one(left, lhs_bit);
            result.set(
                result_bit,
                accumulated ^ operand ^ carry
                    ? Logic4::one : Logic4::zero);
            carry =
                (accumulated && operand)
                || (accumulated && carry)
                || (operand && carry);
        }
    }
    return result;
}

struct PackedDivision {
    PackedLogic4 quotient;
    PackedLogic4 remainder;
};

[[nodiscard]] std::optional<PackedDivision> packed_divide_unsigned(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor,
    const std::uint32_t width) {
    auto quotient = PackedLogic4{width, Logic4::zero};
    auto remainder = PackedLogic4{width, Logic4::zero};
    bool divisor_zero = true;
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        divisor_zero = divisor_zero && !packed_one(divisor, bit);
    }
    if (divisor_zero) {
        return std::nullopt;
    }
    for (std::uint32_t offset = 0; offset < width; ++offset) {
        const auto source_bit = width - offset - 1U;
        for (std::uint32_t bit = width - 1U; bit > 0U; --bit) {
            remainder.set(
                bit,
                packed_one(remainder, bit - 1U)
                    ? Logic4::one : Logic4::zero);
        }
        remainder.set(
            0,
            packed_one(dividend, source_bit)
                ? Logic4::one : Logic4::zero);
        if (packed_compare_unsigned(remainder, divisor, width) >= 0) {
            remainder = packed_subtract(remainder, divisor, width);
            quotient.set(source_bit, Logic4::one);
        }
    }
    return PackedDivision{
        std::move(quotient), std::move(remainder)};
}

[[nodiscard]] std::optional<Value> evaluate_wide_arithmetic(
    const Value& left,
    const Value& right,
    const std::string_view operation,
    const frontend::SourceSpan& source,
    std::string& error) {
    const auto width = std::max(left.width, right.width);
    const bool signed_result = left.is_signed && right.is_signed;
    const auto lhs = common_operand(left, width, signed_result);
    const auto rhs = common_operand(right, width, signed_result);
    if (!lhs.known() || !rhs.known()) {
        return make_unknown(width, signed_result, source);
    }
    const auto result_domain =
        lhs.domain == rhs.domain
            ? lhs.domain : frontend::ValueDomain::Logic4;
    const auto make_result = [&](PackedLogic4 packed) {
        return Value{
            std::move(packed),
            signed_result,
            false,
            result_domain,
            {},
            source};
    };
    if (operation == "+") {
        return make_result(packed_add(lhs.packed, rhs.packed, width));
    }
    if (operation == "-") {
        return make_result(
            packed_subtract(lhs.packed, rhs.packed, width));
    }
    if (operation == "**" && signed_result
        && packed_one(rhs.packed, width - 1U)) {
        if (packed_is_zero(lhs.packed, width)) {
            error = "constant zero to a negative power is undefined";
            return std::nullopt;
        }
        auto result = PackedLogic4{width, Logic4::zero};
        if (packed_is_one(lhs.packed, width)) {
            result.set(0, Logic4::one);
        } else if (packed_is_all_ones(lhs.packed, width)) {
            const auto state = packed_one(rhs.packed, 0)
                ? Logic4::one : Logic4::zero;
            for (std::uint32_t bit = 0; bit < width; ++bit) {
                result.set(bit, state);
            }
        }
        return make_result(std::move(result));
    }
    if (width > maximum_constant_work_units / width) {
        error =
            "arbitrary-width multiplicative constant evaluation exceeds "
            "the work limit";
        return std::nullopt;
    }
    const auto multiplication_work =
        static_cast<std::uint64_t>(width) * width;
    if (operation == "*") {
        return make_result(
            packed_multiply(lhs.packed, rhs.packed, width));
    }
    if (operation == "**") {
        const auto exponent = nonnegative_count(rhs, error);
        if (!exponent) {
            return std::nullopt;
        }
        auto factor = lhs.packed;
        auto accumulated = PackedLogic4{width, Logic4::zero};
        accumulated.set(0, Logic4::one);
        auto remaining = *exponent;
        std::uint64_t work = 0;
        while (remaining != 0U) {
            if ((remaining & 1U) != 0U) {
                if (work > maximum_constant_work_units - multiplication_work) {
                    error =
                        "arbitrary-width power constant evaluation exceeds "
                        "the work limit";
                    return std::nullopt;
                }
                work += multiplication_work;
                accumulated = packed_multiply(accumulated, factor, width);
            }
            remaining >>= 1U;
            if (remaining != 0U) {
                if (work > maximum_constant_work_units - multiplication_work) {
                    error =
                        "arbitrary-width power constant evaluation exceeds "
                        "the work limit";
                    return std::nullopt;
                }
                work += multiplication_work;
                factor = packed_multiply(factor, factor, width);
            }
        }
        return make_result(std::move(accumulated));
    }
    auto dividend = lhs.packed;
    auto divisor = rhs.packed;
    const bool negative_left =
        signed_result && packed_one(dividend, width - 1U);
    const bool negative_right =
        signed_result && packed_one(divisor, width - 1U);
    if (negative_left && negative_right
        && packed_is_signed_minimum(dividend, width)
        && packed_is_all_ones(divisor, width)) {
        error =
            "constant division overflows the signed destination width";
        return std::nullopt;
    }
    if (negative_left) {
        dividend = packed_negate(dividend, width);
    }
    if (negative_right) {
        divisor = packed_negate(divisor, width);
    }
    auto divided = packed_divide_unsigned(dividend, divisor, width);
    if (!divided) {
        error = "division by zero in SystemVerilog constant expression";
        return std::nullopt;
    }
    if (operation == "/") {
        if (negative_left != negative_right) {
            divided->quotient = packed_negate(divided->quotient, width);
        }
        return make_result(std::move(divided->quotient));
    }
    if (negative_left) {
        divided->remainder = packed_negate(divided->remainder, width);
    }
    return make_result(std::move(divided->remainder));
}

enum class Truth { False, True, Unknown };

[[nodiscard]] Truth truth(const Value& value) noexcept {
    const auto value_truth = value.truth_value();
    return !value_truth
        ? Truth::Unknown
        : *value_truth ? Truth::True : Truth::False;
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
    if (value.is_signed || value.width > 64U) {
        const auto converted = value.integer_value();
        if (!converted || *converted < 0) {
            error =
                "constant count must be nonnegative and fit the bounded "
                "signed 64-bit resource range";
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
    auto packed = PackedLogic4{width, Logic4::zero};
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        const auto lhs_state = runtime::to_logic4(lhs.packed.get_logic9(bit));
        const auto rhs_state = runtime::to_logic4(rhs.packed.get_logic9(bit));
        const bool lhs_known =
            lhs_state == Logic4::zero || lhs_state == Logic4::one;
        const bool rhs_known =
            rhs_state == Logic4::zero || rhs_state == Logic4::one;
        const bool lhs_one = lhs_state == Logic4::one;
        const bool rhs_one = rhs_state == Logic4::one;
        auto state = Logic4::x;
        if (operation == "&") {
            state = (!lhs_one && lhs_known) || (!rhs_one && rhs_known)
                ? Logic4::zero
                : lhs_one && rhs_one ? Logic4::one : Logic4::x;
        } else if (operation == "|") {
            state = lhs_one || rhs_one
                ? Logic4::one
                : lhs_known && rhs_known ? Logic4::zero : Logic4::x;
        } else {
            if (lhs_known && rhs_known) {
                bool one = lhs_one != rhs_one;
                if (operation == "~^" || operation == "^~") {
                    one = !one;
                }
                state = one ? Logic4::one : Logic4::zero;
            }
        }
        packed.set(bit, state);
    }
    const auto domain = lhs.domain == rhs.domain
        ? lhs.domain : frontend::ValueDomain::Logic4;
    return Value{
        std::move(packed), is_signed, false, domain, {}, source};
}

[[nodiscard]] int compare_known(
    const Value& left,
    const Value& right) noexcept {
    const auto width = std::max(left.width, right.width);
    const bool signed_comparison = left.is_signed && right.is_signed;
    const auto lhs = common_operand(left, width, signed_comparison);
    const auto rhs = common_operand(right, width, signed_comparison);
    if (signed_comparison) {
        const bool lhs_negative = packed_one(lhs.packed, width - 1U);
        const bool rhs_negative = packed_one(rhs.packed, width - 1U);
        if (lhs_negative != rhs_negative) {
            return lhs_negative ? -1 : 1;
        }
    }
    return packed_compare_unsigned(lhs.packed, rhs.packed, width);
}

[[nodiscard]] Truth wildcard_equal(
    const Value& left,
    const Value& right) noexcept {
    const auto width = std::max(left.width, right.width);
    const bool common_signed = left.is_signed && right.is_signed;
    const auto lhs = common_operand(left, width, common_signed);
    const auto rhs = common_operand(right, width, common_signed);
    bool unknown = false;
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        const auto expected = runtime::to_logic4(rhs.packed.get_logic9(bit));
        if (expected == Logic4::x || expected == Logic4::z) {
            continue;
        }
        const auto actual = runtime::to_logic4(lhs.packed.get_logic9(bit));
        if (actual == Logic4::x || actual == Logic4::z) {
            unknown = true;
        } else if (actual != expected) {
            return Truth::False;
        }
    }
    return unknown ? Truth::Unknown : Truth::True;
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
    if (expression.text == "==?" || expression.text == "!=?") {
        auto result = wildcard_equal(*left, *right);
        if (expression.text == "!=?" && result != Truth::Unknown) {
            result = result == Truth::True ? Truth::False : Truth::True;
        }
        return logical_result(result, expression.span);
    }
    if (expression.text == "==" || expression.text == "!="
        || expression.text == "=" || expression.text == "/="
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
        bool equal = lhs.packed == rhs.packed;
        if (expression.text == "!=" || expression.text == "!=="
            || expression.text == "/=") {
            equal = !equal;
        }
        return logical_result(
            equal ? Truth::True : Truth::False, expression.span);
    }
    const bool arithmetic_operation =
        expression.text == "+" || expression.text == "-"
        || expression.text == "*" || expression.text == "/"
        || expression.text == "%" || expression.text == "**";
    if (arithmetic_operation
        && (left->width > 64U || right->width > 64U)) {
        return evaluate_wide_arithmetic(
            *left, *right, expression.text, expression.span, error);
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
        if (!right->known()) {
            return make_unknown(
                left->width, left->is_signed, expression.span);
        }
        if (right->is_signed
            && packed_one(right->packed, right->width - 1U)) {
            error = "constant shift count must be nonnegative";
            return std::nullopt;
        }
        std::uint64_t count = 0;
        for (std::uint32_t bit = 0; bit < right->width; ++bit) {
            if (!packed_one(right->packed, bit)) {
                continue;
            }
            if (bit >= 32U) {
                count = left->width;
                break;
            }
            count += std::uint64_t{1} << bit;
            if (count >= left->width) {
                count = left->width;
                break;
            }
        }
        Value result = *left;
        result.source = expression.span;
        const bool arithmetic_right =
            expression.text == ">>>" && result.is_signed;
        auto shifted = PackedLogic4{result.width, Logic4::zero};
        if (result.packed.is_logic9()) {
            shifted = shifted.promoted_to_logic9();
        }
        const auto set_shifted = [&](const std::uint32_t bit,
                                     const runtime::Logic9 state) {
            if (result.packed.is_logic9()) {
                shifted.set_logic9(bit, state);
            } else {
                shifted.set(bit, runtime::to_logic4(state));
            }
        };
        const auto sign = result.packed.get_logic9(result.width - 1U);
        if (count < result.width) {
            const auto amount = static_cast<std::uint32_t>(count);
            if (expression.text == "<<" || expression.text == "<<<") {
                for (auto bit = amount; bit < result.width; ++bit) {
                    set_shifted(
                        bit, result.packed.get_logic9(bit - amount));
                }
            } else {
                for (std::uint32_t bit = 0;
                     bit + amount < result.width;
                     ++bit) {
                    set_shifted(
                        bit, result.packed.get_logic9(bit + amount));
                }
            }
        }
        if (arithmetic_right) {
            const auto first_extension = count >= result.width
                ? 0U : result.width - static_cast<std::uint32_t>(count);
            for (auto bit = first_extension; bit < result.width; ++bit) {
                set_shifted(bit, sign);
            }
        }
        result.packed = std::move(shifted);
        result.refresh_low_word_mirrors();
        return result;
    }

    if (left->width > 64U || right->width > 64U) {
        error =
            "unsupported arbitrary-width SystemVerilog constant operator '"
            + expression.text + "'";
        return std::nullopt;
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
        auto digits = cleaned_digits(expression.text);
        auto parsed = parse_decimal_packed(
            digits, std::nullopt, true, error);
        if (!parsed) {
            return std::nullopt;
        }
        return Value{
            std::move(parsed->packed),
            true,
            true,
            frontend::ValueDomain::Logic4,
            expression.nominal_type,
            expression.span};
    }
    if (expression.kind == ExpressionKind::LogicLiteral) {
        auto result = parse_based_literal(expression, error);
        if (result) {
            result->nominal_type = expression.nominal_type;
        }
        return result;
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
            auto result = make_known(
                static_cast<std::uint64_t>(fallback->second),
                64,
                true,
                false,
                expression.span);
            result.nominal_type = expression.nominal_type;
            return result;
        }
        error =
            "unknown or forward parameter reference '"
            + expression.text + "'";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Index
        && expression.operands.size() == 2U) {
        const auto base = evaluate_impl(
            expression.operands[0], environment, fallback_environment, error);
        const auto index = base
            ? evaluate_impl(
                  expression.operands[1], environment,
                  fallback_environment, error)
            : std::nullopt;
        if (!base || !index) {
            return std::nullopt;
        }
        auto packed = PackedLogic4{1, Logic4::x};
        if (base->packed.is_logic9()) {
            packed = packed.promoted_to_logic9();
        }
        const auto position = index->integer_value();
        if (position && *position >= 0
            && static_cast<std::uint64_t>(*position) < base->width) {
            if (base->packed.is_logic9()) {
                packed.set_logic9(
                    0, base->packed.get_logic9(
                           static_cast<std::uint32_t>(*position)));
            } else {
                packed.set(
                    0, base->packed.get(
                           static_cast<std::uint32_t>(*position)));
            }
        }
        return Value{
            std::move(packed), false, false, base->domain, {},
            expression.span};
    }
    if (expression.kind == ExpressionKind::Slice
        && expression.operands.size() == 3U) {
        const auto base = evaluate_impl(
            expression.operands[0], environment, fallback_environment, error);
        const auto first = base
            ? evaluate_impl(
                  expression.operands[1], environment,
                  fallback_environment, error)
            : std::nullopt;
        const auto second = first
            ? evaluate_impl(
                  expression.operands[2], environment,
                  fallback_environment, error)
            : std::nullopt;
        if (!base || !first || !second) {
            return std::nullopt;
        }
        const auto first_index = first->integer_value();
        const auto second_index = second->integer_value();
        if (!first_index || !second_index) {
            error = "constant part-select bounds must be known integers";
            return std::nullopt;
        }
        std::uint64_t width = 0;
        if (expression.text == "+:" || expression.text == "-:") {
            if (*second_index <= 0) {
                error = "constant indexed part-select width must be positive";
                return std::nullopt;
            }
            width = static_cast<std::uint64_t>(*second_index);
        } else {
            const auto distance = *first_index >= *second_index
                ? static_cast<std::uint64_t>(*first_index)
                    - static_cast<std::uint64_t>(*second_index)
                : static_cast<std::uint64_t>(*second_index)
                    - static_cast<std::uint64_t>(*first_index);
            if (distance == std::numeric_limits<std::uint64_t>::max()) {
                error = "constant part-select width exceeds the resource limit";
                return std::nullopt;
            }
            width = distance + 1U;
        }
        if (width == 0U || width > maximum_constant_width) {
            error = "constant part-select width exceeds the resource limit";
            return std::nullopt;
        }
        const auto result_width = static_cast<std::uint32_t>(width);
        auto packed = PackedLogic4{result_width, Logic4::x};
        if (base->packed.is_logic9()) {
            packed = packed.promoted_to_logic9();
        }
        const auto source_at = [&](const std::uint32_t bit)
            -> std::optional<std::uint32_t> {
            bool add = true;
            auto anchor = *second_index;
            auto offset = bit;
            if (expression.text == "+:") {
                anchor = *first_index;
            } else if (expression.text == "-:") {
                anchor = *first_index;
                offset = result_width - bit - 1U;
                add = false;
            } else if (*first_index < *second_index) {
                add = false;
            }
            if (anchor < 0) {
                return std::nullopt;
            }
            const auto unsigned_anchor = static_cast<std::uint64_t>(anchor);
            if ((!add && unsigned_anchor < offset)
                || (add && unsigned_anchor
                        > std::numeric_limits<std::uint64_t>::max() - offset)) {
                return std::nullopt;
            }
            const auto source = add
                ? unsigned_anchor + offset : unsigned_anchor - offset;
            return source < base->width
                ? std::optional<std::uint32_t>{
                      static_cast<std::uint32_t>(source)}
                : std::nullopt;
        };
        for (std::uint32_t bit = 0; bit < result_width; ++bit) {
            const auto source = source_at(bit);
            if (!source) {
                continue;
            }
            if (base->packed.is_logic9()) {
                packed.set_logic9(bit, base->packed.get_logic9(*source));
            } else {
                packed.set(bit, base->packed.get(*source));
            }
        }
        return Value{
            std::move(packed), false, false, base->domain, {},
            expression.span};
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
        if (expression.text == "-") {
            if (!operand->known()) {
                return make_unknown(
                    operand->width,
                    operand->is_signed,
                    expression.span);
            }
            operand->packed =
                packed_negate(operand->packed, operand->width);
            operand->refresh_low_word_mirrors();
            return operand;
        }
        if (expression.text == "~") {
            for (std::uint32_t bit = 0; bit < operand->width; ++bit) {
                const auto state = runtime::to_logic4(
                    operand->packed.get_logic9(bit));
                operand->packed.set(
                    bit,
                    state == Logic4::zero ? Logic4::one
                    : state == Logic4::one ? Logic4::zero
                                           : Logic4::x);
            }
            operand->refresh_low_word_mirrors();
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
                    const auto state = runtime::to_logic4(
                        operand->packed.get_logic9(bit));
                    if (state == Logic4::x || state == Logic4::z) {
                        if (reduced == Truth::True) {
                            reduced = Truth::Unknown;
                        }
                    } else if (state == Logic4::zero) {
                        reduced = Truth::False;
                        break;
                    }
                }
            } else if (expression.text == "|" || expression.text == "~|") {
                reduced = truth(*operand);
            } else if (!operand->known()) {
                reduced = Truth::Unknown;
            } else {
                bool odd = false;
                for (std::uint32_t bit = 0; bit < operand->width; ++bit) {
                    odd = odd != packed_one(operand->packed, bit);
                }
                reduced = odd ? Truth::True : Truth::False;
            }
            if (expression.text == "~&" || expression.text == "~|"
                || expression.text == "~^" || expression.text == "^~") {
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
        && expression.text.starts_with("@sv-cast:")) {
        if (expression.operands.size() != 1U) {
            error = "SystemVerilog type casts require exactly one argument";
            return std::nullopt;
        }
        const auto type_name = std::string_view{expression.text}.substr(
            std::string_view{"@sv-cast:"}.size());
        std::uint32_t width = 0;
        bool is_signed = false;
        bool two_state = false;
        auto domain = frontend::ValueDomain::Unknown;
        if (type_name == "bit") {
            width = 1;
            two_state = true;
            domain = frontend::ValueDomain::Bit2;
        } else if (type_name == "logic" || type_name == "reg") {
            width = 1;
            domain = frontend::ValueDomain::Logic4;
        } else if (type_name == "byte") {
            width = 8;
            is_signed = true;
            two_state = true;
            domain = frontend::ValueDomain::Integer;
        } else if (type_name == "shortint") {
            width = 16;
            is_signed = true;
            two_state = true;
            domain = frontend::ValueDomain::Integer;
        } else if (type_name == "int") {
            width = 32;
            is_signed = true;
            two_state = true;
            domain = frontend::ValueDomain::Integer;
        } else if (type_name == "longint") {
            width = 64;
            is_signed = true;
            two_state = true;
            domain = frontend::ValueDomain::Integer;
        } else if (type_name == "integer") {
            width = 32;
            is_signed = true;
            domain = frontend::ValueDomain::Logic4;
        } else if (expression.call_result_width != 0U
                   && expression.call_result_width
                       <= maximum_constant_width
                   && expression.call_result_domain
                       != frontend::ValueDomain::Unknown) {
            width = static_cast<std::uint32_t>(
                expression.call_result_width);
            is_signed = expression.call_result_signed;
            domain = expression.call_result_domain;
            two_state = is_two_state_domain(domain);
        } else {
            error =
                "constant casts to named or nonintegral type '"
                + std::string{type_name}
                + "' require resolved type layout";
            return std::nullopt;
        }
        auto result = evaluate_impl(
            expression.operands.front(), environment,
            fallback_environment, error);
        if (!result) {
            return std::nullopt;
        }
        result = resized(std::move(*result), width);
        if (two_state && !result->known()) {
            error =
                "X or Z bits would be lost converting a constant into "
                "two-state cast type '" + std::string{type_name} + "'";
            return std::nullopt;
        }
        if (domain == frontend::ValueDomain::Logic4
            && result->packed.is_logic9()) {
            result->packed = runtime::collapse_to_logic4(result->packed);
            result->refresh_low_word_mirrors();
        } else if (domain == frontend::ValueDomain::Logic9
                   && !result->packed.is_logic9()) {
            result->packed = result->packed.promoted_to_logic9();
            result->refresh_low_word_mirrors();
        }
        result->is_signed = is_signed;
        result->unsized = false;
        result->domain = domain;
        result->nominal_type = expression.nominal_type.empty()
            ? std::string{type_name}
            : expression.nominal_type;
        result->source = expression.span;
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
        if (!slice_size || *slice_size == 0U
            || *slice_size > maximum_constant_width) {
            if (slice_size) {
                error =
                    "streaming concatenation slice size exceeds the "
                    "constant-width resource limit";
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
            if (operand->width > maximum_constant_width - width) {
                error =
                    "streaming concatenation result exceeds the constant-"
                    "width resource limit";
                return std::nullopt;
            }
            width += operand->width;
            operands.push_back(std::move(*operand));
        }
        if (width == 0U) {
            error = "streaming concatenation requires a nonempty operand";
            return std::nullopt;
        }
        if (operands.size() > maximum_constant_work_units / width) {
            error =
                "streaming concatenation exceeds the constant-evaluation "
                "work limit";
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

        auto packed = PackedLogic4{width, Logic4::zero};
        if (ordinary.packed.is_logic9()) {
            packed = packed.promoted_to_logic9();
        }
        for (std::uint64_t offset = 0; offset < width;) {
            const auto chunk = std::min(*slice_size, width - offset);
            const auto destination = width - offset - chunk;
            for (std::uint64_t bit = 0; bit < chunk; ++bit) {
                if (ordinary.packed.is_logic9()) {
                    packed.set_logic9(
                        destination + bit,
                        ordinary.packed.get_logic9(offset + bit));
                } else {
                    packed.set(
                        destination + bit,
                        ordinary.packed.get(offset + bit));
                }
            }
            offset += chunk;
        }
        return Value{
            std::move(packed), false, false, ordinary.domain, {},
            expression.span};
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
    const bool packed_query =
        expression.kind == ExpressionKind::Call
        && (expression.text == "$bits"
            || expression.text == "$left"
            || expression.text == "$right"
            || expression.text == "$low"
            || expression.text == "$high"
            || expression.text == "$size"
            || expression.text == "$increment"
            || expression.text == "$dimensions"
            || expression.text == "$unpacked_dimensions");
    if (packed_query) {
        const bool accepts_dimension =
            expression.text == "$left"
            || expression.text == "$right"
            || expression.text == "$low"
            || expression.text == "$high"
            || expression.text == "$size"
            || expression.text == "$increment";
        if (expression.operands.empty()
            || expression.operands.size() > (accepts_dimension ? 2U : 1U)) {
            error = expression.text
                + " requires one packed argument"
                + (accepts_dimension
                       ? " and at most one dimension argument" : "");
            return std::nullopt;
        }
        const auto operand = evaluate_impl(
            expression.operands.front(), environment,
            fallback_environment, error);
        if (!operand) {
            return std::nullopt;
        }
        if (expression.operands.size() == 2U) {
            const auto dimension = evaluate_impl(
                expression.operands[1], environment,
                fallback_environment, error);
            if (!dimension) {
                return std::nullopt;
            }
            const auto selected = nonnegative_count(*dimension, error);
            if (!selected || *selected != 1U) {
                if (selected) {
                    error = expression.text
                        + " supports only packed dimension 1";
                }
                return std::nullopt;
            }
        }
        const auto range = operand->packed_range.value_or(
            frontend::PackedRange{
                static_cast<std::int64_t>(operand->width - 1U),
                0,
                true});
        std::int64_t result = 0;
        if (expression.text == "$bits" || expression.text == "$size") {
            result = static_cast<std::int64_t>(operand->width);
        } else if (expression.text == "$left") {
            result = range.left;
        } else if (expression.text == "$right") {
            result = range.right;
        } else if (expression.text == "$low") {
            result = std::min(range.left, range.right);
        } else if (expression.text == "$high") {
            result = std::max(range.left, range.right);
        } else if (expression.text == "$increment") {
            result = range.left >= range.right ? 1 : -1;
        } else if (expression.text == "$dimensions") {
            result = 1;
        } else if (expression.text == "$unpacked_dimensions") {
            result = 0;
        }
        return make_known(
            static_cast<std::uint32_t>(result), 32, true, false,
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
        if (operand->is_signed
            && packed_one(operand->packed, operand->width - 1U)) {
            error = "$clog2 requires a nonnegative integral argument";
            return std::nullopt;
        }
        std::uint32_t highest_one = 0;
        bool found_one = false;
        bool lower_one = false;
        for (auto bit = operand->width; bit-- > 0U;) {
            if (!packed_one(operand->packed, bit)) {
                continue;
            }
            if (!found_one) {
                highest_one = bit;
                found_one = true;
            } else {
                lower_one = true;
                break;
            }
        }
        const auto result =
            !found_one || highest_one == 0U
                ? 0U
                : highest_one + (lower_one ? 1U : 0U);
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
        auto packed = PackedLogic4{width, Logic4::x};
        const bool logic9 =
            lhs.packed.is_logic9() || rhs.packed.is_logic9();
        if (logic9) {
            packed = packed.promoted_to_logic9();
        }
        for (std::uint32_t bit = 0; bit < width; ++bit) {
            const auto lhs_state = lhs.packed.get_logic9(bit);
            const auto rhs_state = rhs.packed.get_logic9(bit);
            if (logic9) {
                packed.set_logic9(
                    bit,
                    lhs_state == rhs_state
                        ? lhs_state : runtime::Logic9::x);
            } else if (lhs_state == rhs_state) {
                packed.set(bit, runtime::to_logic4(lhs_state));
            }
        }
        const auto domain = lhs.domain == rhs.domain
            ? lhs.domain : frontend::ValueDomain::Logic4;
        return Value{
            std::move(packed), common_signed, false, domain, {},
            expression.span};
    }
    const bool scalar_default_pattern =
        expression.kind == ExpressionKind::Aggregate
        && expression.text == "sv-pattern"
        && expression.operands.size() == 1U
        && expression.aggregate_choices.size() == 1U
        && expression.aggregate_choices.front() == "default";
    if (scalar_default_pattern) {
        const auto value = evaluate_impl(
            expression.operands.front(), environment,
            fallback_environment, error);
        if (!value) {
            return std::nullopt;
        }
        auto packed = PackedLogic4{1, Logic4::x};
        if (value->packed.is_logic9()) {
            packed = packed.promoted_to_logic9();
            packed.set_logic9(0, value->packed.get_logic9(0));
        } else {
            packed.set(0, value->packed.get(0));
        }
        return Value{
            std::move(packed), true, true, value->domain, {},
            expression.span};
    }
    const bool positional_pattern =
        expression.kind == ExpressionKind::Aggregate
        && expression.text == "sv-pattern"
        && std::ranges::all_of(
            expression.aggregate_choices,
            [](const auto& choice) { return choice.empty(); });
    if (expression.kind == ExpressionKind::Aggregate
        && expression.text == "sv-pattern" && !positional_pattern
        && !scalar_default_pattern) {
        error =
            "keyed/default assignment patterns require aggregate type "
            "layout and are deferred to aggregate constant evaluation";
        return std::nullopt;
    }
    if (expression.kind == ExpressionKind::Concatenation
        || expression.kind == ExpressionKind::Replication
        || positional_pattern) {
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
            if (operand->width > maximum_constant_width - element_width) {
                error =
                    "concatenation element width exceeds the "
                    "16,777,216-bit resource limit";
                return std::nullopt;
            }
            element_width += operand->width;
            operands.push_back(std::move(*operand));
        }
        if (element_width == 0
            || repetitions > maximum_constant_width / element_width) {
            error =
                "concatenation result exceeds the 16,777,216-bit "
                "resource limit";
            return std::nullopt;
        }
        const auto result_width = element_width * repetitions;
        if (repetitions
                > maximum_constant_work_units / operands.size()
            || result_width
                > maximum_constant_work_units
                    / (repetitions * operands.size())) {
            error =
                "concatenation or replication exceeds the constant-"
                "evaluation work limit";
            return std::nullopt;
        }
        Value result{
            0,
            0,
            0,
            static_cast<std::uint32_t>(result_width),
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

SystemVerilogConstantValue::SystemVerilogConstantValue(
    const std::uint64_t initial_bits,
    const std::uint64_t initial_unknown_bits,
    const std::uint64_t initial_high_impedance_bits,
    const std::uint32_t initial_width,
    const bool initial_signed,
    const bool initial_unsized,
    frontend::SourceSpan initial_source)
    : packed{checked_constant_width(initial_width), Logic4::zero},
      bits{initial_bits},
      unknown_bits{initial_unknown_bits},
      high_impedance_bits{initial_high_impedance_bits},
      width{initial_width},
      is_signed{initial_signed},
      unsized{initial_unsized},
      source{std::move(initial_source)},
      packed_range{frontend::PackedRange{
          static_cast<std::int64_t>(initial_width - 1U), 0, true}} {
    const auto copied = std::min(width, std::uint32_t{64});
    for (std::uint32_t bit = 0; bit < copied; ++bit) {
        const auto bit_mask = std::uint64_t{1} << bit;
        packed.set(
            bit,
            (unknown_bits & bit_mask) != 0
                ? ((high_impedance_bits & bit_mask) != 0
                       ? Logic4::z : Logic4::x)
                : (bits & bit_mask) != 0
                      ? Logic4::one : Logic4::zero);
    }
    refresh_low_word_mirrors();
}

SystemVerilogConstantValue::SystemVerilogConstantValue(
    PackedLogic4 initial_packed,
    const bool initial_signed,
    const bool initial_unsized,
    const frontend::ValueDomain initial_domain,
    std::string initial_nominal_type,
    frontend::SourceSpan initial_source)
    : packed{std::move(initial_packed)},
      width{static_cast<std::uint32_t>(packed.width())},
      is_signed{initial_signed},
      unsized{initial_unsized},
      domain{initial_domain},
      nominal_type{std::move(initial_nominal_type)},
      source{std::move(initial_source)},
      packed_range{frontend::PackedRange{
          static_cast<std::int64_t>(packed.width() - 1U), 0, true}} {
    (void)checked_constant_width(packed.width());
    refresh_low_word_mirrors();
}

void SystemVerilogConstantValue::refresh_low_word_mirrors() noexcept {
    bits = 0;
    unknown_bits = 0;
    high_impedance_bits = 0;
    const auto copied = std::min(width, std::uint32_t{64});
    for (std::uint32_t bit = 0; bit < copied; ++bit) {
        const auto bit_mask = std::uint64_t{1} << bit;
        const auto state = packed.is_logic9()
            ? runtime::to_logic4(packed.get_logic9(bit))
            : packed.get(bit);
        if (state == Logic4::one) {
            bits |= bit_mask;
        } else if (state == Logic4::x || state == Logic4::z) {
            unknown_bits |= bit_mask;
            if (state == Logic4::z) {
                high_impedance_bits |= bit_mask;
            }
        }
    }
}

std::uint64_t SystemVerilogConstantValue::mask() const noexcept {
    return width_mask(width);
}

bool SystemVerilogConstantValue::known() const noexcept {
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        if (runtime::to_logic4(packed.get_logic9(bit)) == Logic4::x
            || runtime::to_logic4(packed.get_logic9(bit)) == Logic4::z) {
            return false;
        }
    }
    return true;
}

std::optional<bool>
SystemVerilogConstantValue::truth_value() const noexcept {
    bool unknown = false;
    for (std::uint32_t bit = 0; bit < width; ++bit) {
        const auto state = runtime::to_logic4(packed.get_logic9(bit));
        if (state == Logic4::one) {
            return true;
        }
        if (state == Logic4::x || state == Logic4::z) {
            unknown = true;
        }
    }
    return unknown ? std::nullopt : std::optional<bool>{false};
}

std::optional<std::int64_t>
SystemVerilogConstantValue::integer_value() const noexcept {
    if (!known()) {
        return std::nullopt;
    }
    const auto value = bits & mask();
    if (width > 64U) {
        const auto sign_state = runtime::to_logic4(
            packed.get_logic9(width - 1U));
        const bool negative = is_signed && sign_state == Logic4::one;
        for (std::uint32_t bit = 64U; bit < width; ++bit) {
            const auto state = runtime::to_logic4(packed.get_logic9(bit));
            if (state != (negative ? Logic4::one : Logic4::zero)) {
                return std::nullopt;
            }
        }
        if (!negative) {
            if (value > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max())) {
                return std::nullopt;
            }
            return static_cast<std::int64_t>(value);
        }
        if ((value & (std::uint64_t{1} << 63U)) == 0U) {
            return std::nullopt;
        }
        const auto magnitude = (~value) + 1U;
        if (magnitude == (std::uint64_t{1} << 63U)) {
            return std::numeric_limits<std::int64_t>::min();
        }
        return -static_cast<std::int64_t>(magnitude);
    }
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

bool is_systemverilog_nominal_packed_type(
    const frontend::Type& type) noexcept {
    return !type.nominal_type.empty()
        && (type.packed_aggregate
                != frontend::PackedAggregateKind::None
            || !type.enumeration_literals.empty());
}

std::optional<SystemVerilogConstantValue>
convert_systemverilog_parameter_value(
    const SystemVerilogConstantValue& value,
    const frontend::Type& type,
    std::string& error) {
    auto result = value;
    if (is_systemverilog_nominal_packed_type(type)
        && result.nominal_type != type.nominal_type) {
        error = "nominal packed parameter type '" + type.spelling
            + "' requires the same nominal value, a matching explicit "
              "cast, or a contextual assignment pattern (expected '"
            + type.nominal_type + "', received '"
            + (result.nominal_type.empty()
                ? std::string{"<none>"} : result.nominal_type)
            + "')";
        return std::nullopt;
    }
    if (!(type.spelling == "implicit" && type.named_type.empty())) {
        const auto width = type.width();
        if (!width || *width == 0
            || *width > maximum_constant_width) {
            error =
                "declared SystemVerilog parameter type does not have a "
                "width within the 16,777,216-bit resource limit";
            return std::nullopt;
        }
        result = resized(result, static_cast<std::uint32_t>(*width));
        result.is_signed = type.is_signed;
        result.unsized = false;
        result.packed_range = type.packed_range.value_or(
            frontend::PackedRange{
                static_cast<std::int64_t>(*width - 1U), 0, true});
    }
    const bool two_state =
        type.domain == frontend::ValueDomain::Bit2
        || type.spelling == "bit" || type.spelling == "byte"
        || type.spelling == "shortint" || type.spelling == "int"
        || type.spelling == "longint";
    if (two_state && !result.known()) {
        error =
            "X or Z bits would be lost converting a constant into "
            "two-state parameter type '" + type.spelling + "'";
        return std::nullopt;
    }
    if (type.domain != frontend::ValueDomain::Unknown) {
        result.domain = type.domain;
    }
    result.nominal_type = !type.nominal_type.empty()
        ? type.nominal_type
        : !type.named_type.empty() ? type.named_type : type.spelling;
    if (result.domain == frontend::ValueDomain::Logic9
        && !result.packed.is_logic9()) {
        result.packed = result.packed.promoted_to_logic9();
        result.refresh_low_word_mirrors();
    }
    normalize(result);
    return result;
}


} // namespace fsim::elaboration::elaboration_detail
