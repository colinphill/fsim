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

[[nodiscard]] constexpr char ascii_lower(const char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A')) : value;
}

[[nodiscard]] std::size_t checked_constant_width(
    const std::uint64_t width) {
    if (width == 0 || width > maximum_constant_width) {
        throw std::length_error{
            "SystemVerilog constant exceeds the 16,777,216-bit resource "
            "limit (requested " + std::to_string(width) + " bits)"};
    }
    return static_cast<std::size_t>(width);
}

[[nodiscard]] PackedLogic4 packed_from_low_word(
    const std::uint64_t bits,
    const std::uint64_t unknown_bits,
    const std::uint64_t high_impedance_bits,
    const std::uint32_t width) {
    auto packed = PackedLogic4{
        checked_constant_width(width), Logic4::zero};
    const auto low_width = std::min(width, std::uint32_t{64});
    packed.insert_word(
        runtime::Logic4Word{
            low_width,
            (bits & ~unknown_bits)
                | (unknown_bits & ~high_impedance_bits),
            unknown_bits},
        0);
    return packed;
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

void convert_to_two_state(Value& value)
{
    auto converted = PackedLogic4(value.width, Logic4::zero);
    for (std::uint32_t bit = 0; bit < value.width; ++bit) {
        if (runtime::to_logic4(value.packed.get_logic9(bit))
            == Logic4::one) {
            converted.set(bit, Logic4::one);
        }
    }
    value.packed = std::move(converted);
    value.refresh_low_word_mirrors();
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
            + 64U - static_cast<std::size_t>(
                        std::countl_zero(words[used_words - 1U])));
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
        const auto digit = ascii_lower(suffix.front());
        if (digit == '0' || digit == '1'
            || digit == 'x' || digit == 'z') {
            Value result {
                digit == '1' ? 1U : 0U,
                digit == 'x' || digit == 'z' ? 1U : 0U,
                digit == 'z' ? 1U : 0U,
                1,
                false,
                true,
                expression.span
            };
            normalize(result);
            return result;
        }
    }
    if (suffix.size() < 2U) {
        error = "SystemVerilog based literal is missing a base or digits";
        return std::nullopt;
    }
    const auto base_character = ascii_lower(suffix.front());
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
        const auto decimal_state = [](const char character) {
            return ascii_lower(character);
        };
        const bool has_unknown_digit = std::ranges::any_of(
            digits,
            [&](const char character) {
                const auto folded = decimal_state(character);
                return folded == 'x' || folded == 'z'
                    || folded == '?';
            });
        if (has_unknown_digit) {
            if (digits.size() != 1U) {
                error = "a decimal SystemVerilog based literal may use only one "
                        "x, z, or ? digit";
                return std::nullopt;
            }
            const auto width = explicit_width
                ? static_cast<std::uint32_t>(*explicit_width)
                : 32U;
            const auto fill = decimal_state(digits.front()) == 'x'
                ? Logic4::x
                : Logic4::z;
            return Value {
                PackedLogic4 { width, fill },
                explicitly_signed,
                !explicit_width.has_value(),
                frontend::ValueDomain::Logic4,
                { },
                expression.span
            };
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
        const auto folded = ascii_lower(*digit);
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
    if (!width_changed) {
        return value;
    }
    const bool logic9 = value.packed.is_logic9();
    auto resized_value = PackedLogic4{width, Logic4::zero};
    if (logic9) {
        resized_value = resized_value.promoted_to_logic9();
    }
    if (width > value.width && value.is_signed) {
        if (logic9) {
            const auto sign = value.packed.get_logic9(value.width - 1U);
            resized_value.fill(sign);
        } else {
            const auto sign = value.packed.get(value.width - 1U);
            resized_value.fill(sign);
        }
    }
    const auto copied = std::min(value.width, width);
    resized_value.insert_bits(value.packed.extract_bits(0, copied), 0);
    value.packed = std::move(resized_value);
    value.width = width;
    value.packed_range = frontend::PackedRange{
        static_cast<std::int64_t>(width - 1U), 0, true};
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

struct ConstantProfile {
    std::uint32_t width { };
    bool is_signed { };
    frontend::ValueDomain domain { frontend::ValueDomain::Unknown };
};

[[nodiscard]] std::optional<ConstantProfile> constant_profile(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment);

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
    if (left->unbounded || right->unbounded) {
        error = "symbolic unbounded '$' is only valid as a parameter value "
                "or an argument to $isunbounded";
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

#include "elaboration_sv_constant_evaluator.tpp"

[[nodiscard]] std::optional<ConstantProfile> constant_profile(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment)
{
    std::string ignored_error;
    if (const auto value = evaluate_impl(
            expression, environment, fallback_environment, ignored_error)) {
        return ConstantProfile {
            value->width, value->is_signed, value->domain
        };
    }
    if (expression.call_result_width != 0U
        && expression.call_result_width <= maximum_constant_width) {
        return ConstantProfile {
            static_cast<std::uint32_t>(expression.call_result_width),
            expression.call_result_signed,
            expression.call_result_domain
        };
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.operands.size() == 1U) {
        const auto operand = constant_profile(
            expression.operands.front(), environment, fallback_environment);
        if (!operand) {
            return std::nullopt;
        }
        if (expression.text == "!" || expression.text == "&"
            || expression.text == "|" || expression.text == "^"
            || expression.text == "~&" || expression.text == "~|"
            || expression.text == "~^" || expression.text == "^~") {
            return ConstantProfile {
                1U, false, frontend::ValueDomain::Logic4
            };
        }
        return operand;
    }
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2U) {
        const auto left = constant_profile(
            expression.operands[0], environment, fallback_environment);
        const auto right = constant_profile(
            expression.operands[1], environment, fallback_environment);
        if (!left || !right) {
            return std::nullopt;
        }
        if (expression.text == "==" || expression.text == "!="
            || expression.text == "===" || expression.text == "!=="
            || expression.text == "==?" || expression.text == "!=?"
            || expression.text == "<" || expression.text == "<="
            || expression.text == ">" || expression.text == ">="
            || expression.text == "&&" || expression.text == "||") {
            return ConstantProfile {
                1U, false, frontend::ValueDomain::Logic4
            };
        }
        if (expression.text == "<<" || expression.text == "<<<"
            || expression.text == ">>" || expression.text == ">>>") {
            return left;
        }
        return ConstantProfile {
            std::max(left->width, right->width),
            left->is_signed && right->is_signed,
            left->domain == right->domain
                ? left->domain
                : frontend::ValueDomain::Logic4
        };
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "?:"
        && expression.operands.size() == 3U) {
        const auto when_true = constant_profile(
            expression.operands[1], environment, fallback_environment);
        const auto when_false = constant_profile(
            expression.operands[2], environment, fallback_environment);
        if (!when_true || !when_false) {
            return std::nullopt;
        }
        return ConstantProfile {
            std::max(when_true->width, when_false->width),
            when_true->is_signed && when_false->is_signed,
            when_true->domain == when_false->domain
                ? when_true->domain
                : frontend::ValueDomain::Logic4
        };
    }
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
    : packed{packed_from_low_word(
          initial_bits,
          initial_unknown_bits,
          initial_high_impedance_bits,
          initial_width)},
      bits{initial_bits},
      unknown_bits{initial_unknown_bits},
      high_impedance_bits{initial_high_impedance_bits},
      width{initial_width},
      is_signed{initial_signed},
      unsized{initial_unsized},
      source{std::move(initial_source)},
      packed_range{frontend::PackedRange{
          static_cast<std::int64_t>(initial_width - 1U), 0, true}} {
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
    const auto mask = width_mask(width);
    if (!packed.is_logic9()) {
        const auto aval = packed.aval_words().front() & mask;
        const auto bval = packed.bval_words().front() & mask;
        bits = aval & ~bval;
        unknown_bits = bval;
        high_impedance_bits = bval & ~aval;
        return;
    }

    const auto plane0 = packed.logic9_plane_words(0).front();
    const auto plane1 = packed.logic9_plane_words(1).front();
    const auto plane2 = packed.logic9_plane_words(2).front();
    const auto plane3 = packed.logic9_plane_words(3).front();
    bits = plane0 & plane1 & ~plane3 & mask;
    unknown_bits = (~plane1 | plane3) & mask;
    high_impedance_bits = plane2
        & ~(plane0 | plane1 | plane3) & mask;
}

std::uint64_t SystemVerilogConstantValue::mask() const noexcept {
    return width_mask(width);
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
    if (result.unbounded) {
        if (type.spelling == "implicit" && type.named_type.empty()) {
            return result;
        }
        error = "symbolic unbounded '$' requires an implicit parameter type";
        return std::nullopt;
    }
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
    if (two_state) {
        convert_to_two_state(result);
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
