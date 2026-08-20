// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

namespace {

    std::optional<std::string> expand_based_digits(
        const std::string_view digits,
        const char base)
    {
        const auto bits_per_digit = base == 'b' ? 1U : base == 'o' ? 3U
            : base == 'h'                                          ? 4U
                                                                   : 0U;
        if (bits_per_digit == 0) {
            return std::nullopt;
        }
        std::string expanded;
        expanded.reserve(digits.size() * bits_per_digit);
        for (const char raw : digits) {
            if (raw == '_') {
                continue;
            }
            const auto digit = static_cast<char>(
                std::tolower(static_cast<unsigned char>(raw)));
            if (digit == 'x' || digit == 'z' || digit == '?') {
                expanded.append(
                    bits_per_digit,
                    digit == '?' ? 'z' : digit);
                continue;
            }
            const auto value = digit >= '0' && digit <= '9'
                ? static_cast<unsigned>(digit - '0')
                : digit >= 'a' && digit <= 'f'
                ? static_cast<unsigned>(digit - 'a' + 10)
                : 16U;
            const auto radix = base == 'b' ? 2U : base == 'o' ? 8U
                                                              : 16U;
            if (value >= radix) {
                return std::nullopt;
            }
            for (auto bit = bits_per_digit; bit != 0; --bit) {
                expanded.push_back(
                    ((value >> (bit - 1U)) & 1U) != 0U ? '1' : '0');
            }
        }
        return expanded.empty() ? std::nullopt
                                : std::optional { std::move(expanded) };
    }

    std::optional<std::string> expand_decimal_digits(
        const std::string_view digits,
        const std::size_t width)
    {
        std::string decimal;
        decimal.reserve(digits.size());
        for (const char raw : digits) {
            if (raw != '_') {
                decimal.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(raw))));
            }
        }
        if (decimal.empty()) {
            return std::nullopt;
        }
        if (decimal == "x" || decimal == "z" || decimal == "?") {
            return std::string(width, decimal == "?" ? 'z' : decimal.front());
        }
        if (!std::ranges::all_of(decimal, [](const char digit) {
                return digit >= '0' && digit <= '9';
            })) {
            return std::nullopt;
        }
        const auto first_nonzero = decimal.find_first_not_of('0');
        decimal = first_nonzero == std::string::npos
            ? "0"
            : decimal.substr(first_nonzero);
        std::string expanded(width, '0');
        for (std::size_t bit = 0; bit < width && decimal != "0"; ++bit) {
            unsigned carry = 0;
            for (char& digit : decimal) {
                const auto value = carry * 10U
                    + static_cast<unsigned>(digit - '0');
                digit = static_cast<char>('0' + value / 2U);
                carry = value % 2U;
            }
            expanded[width - 1U - bit] = carry != 0U ? '1' : '0';
            const auto next_nonzero = decimal.find_first_not_of('0');
            decimal = next_nonzero == std::string::npos
                ? "0"
                : decimal.substr(next_nonzero);
        }
        return expanded;
    }

    std::optional<LoweredLiteral> sized_based_value(
        const std::string_view digits,
        const char base,
        const std::size_t width)
    {
        auto expanded = base == 'd'
            ? expand_decimal_digits(digits, width)
            : expand_based_digits(digits, base);
        if (!expanded) {
            return std::nullopt;
        }
        const auto fill = expanded->front() == 'x'
            ? 'x'
            : expanded->front() == 'z' ? 'z'
                                       : '0';
        if (expanded->size() < width) {
            expanded->insert(expanded->begin(), width - expanded->size(), fill);
        } else if (expanded->size() > width) {
            expanded->erase(0, expanded->size() - width);
        }
        try {
            auto value = PackedLogic4::from_msb_string(*expanded);
            const auto four_state = std::ranges::any_of(
                *expanded,
                [](const char bit) { return bit != '0' && bit != '1'; });
            return LoweredLiteral {
                std::move(value),
                four_state ? frontend::ValueDomain::Logic4
                           : frontend::ValueDomain::Bit2
            };
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }

} // namespace

std::optional<LoweredLiteral> literal_value(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Language language) {
    auto text = expression.text;
    if (expression.kind == ExpressionKind::BooleanLiteral
        && language == frontend::Language::Vhdl2008
        && (text == "true" || text == "false")) {
        return LoweredLiteral{
            PackedLogic4(
                1,
                text == "true" ? Logic4::one : Logic4::zero),
            frontend::ValueDomain::Boolean};
    }
    if (expression.kind == ExpressionKind::LogicLiteral && text.size() == 3
        && text.front() == '\'' && text.back() == '\'') {
        if (language == frontend::Language::Vhdl2008) {
            const auto parsed = runtime::parse_logic9(text[1]);
            if (!parsed) {
                return std::nullopt;
            }
            const auto domain =
                *parsed == runtime::Logic9::zero
                        || *parsed == runtime::Logic9::one
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic9;
            return LoweredLiteral{
                domain == frontend::ValueDomain::Logic9
                    ? PackedLogic4::from_logic9_msb_string(
                          std::string_view{text}.substr(1, 1))
                    : PackedLogic4(
                          1, runtime::to_logic4(*parsed)),
                domain};
        }
        const auto parsed = runtime::parse_logic4(text[1]);
        if (!parsed) {
            return std::nullopt;
        }
        const auto domain =
            *parsed == Logic4::zero || *parsed == Logic4::one
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
        return LoweredLiteral{PackedLogic4(1, *parsed), domain};
    }
    if (expression.kind == ExpressionKind::StringLiteral && text.size() >= 2
        && text.front() == '"' && text.back() == '"') {
        text = text.substr(1, text.size() - 2);
        try {
            if (language == frontend::Language::Vhdl2008) {
                const auto nine_state =
                    runtime::PackedLogic9::from_msb_string(text);
                auto domain = frontend::ValueDomain::Bit2;
                for (std::size_t bit = 0; bit < nine_state.width(); ++bit) {
                    const auto value = nine_state.get(bit);
                    if (value != runtime::Logic9::zero
                        && value != runtime::Logic9::one) {
                        domain = frontend::ValueDomain::Logic9;
                        break;
                    }
                }
                return LoweredLiteral{
                    domain == frontend::ValueDomain::Logic9
                        ? PackedLogic4::from_logic9_msb_string(text)
                        : runtime::collapse_to_logic4(nine_state),
                    domain};
            }
            if (!expression.decoded_string) {
                return std::nullopt;
            }
            auto value = PackedLogic4(
                expected_width, Logic4::zero);
            const auto& bytes = *expression.decoded_string;
            const auto byte_count = std::min(
                bytes.size(), (expected_width + 7U) / 8U);
            for (std::size_t byte_index = 0;
                 byte_index < byte_count;
                 ++byte_index) {
                const auto byte = static_cast<unsigned char>(
                    bytes[bytes.size() - 1U - byte_index]);
                for (std::size_t bit = 0; bit < 8U; ++bit) {
                    const auto destination = byte_index * 8U + bit;
                    if (destination >= expected_width) {
                        break;
                    }
                    value.set(
                        destination,
                        (byte & (1U << bit)) != 0U
                            ? Logic4::one
                            : Logic4::zero);
                }
            }
            return LoweredLiteral{
                std::move(value), frontend::ValueDomain::Bit2};
        } catch (const std::invalid_argument&) {
            return std::nullopt;
        }
    }

    const auto quote = text.find('\'');
    if (quote != std::string::npos) {
        const auto width_text = std::string_view{text}.substr(0, quote);
        std::size_t width = expected_width;
        if (!width_text.empty()) {
            const auto parsed_width = unsigned_decimal(width_text);
            if (!parsed_width || *parsed_width == 0
                || *parsed_width > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            width = static_cast<std::size_t>(*parsed_width);
        }
        auto digits = std::string_view{text}.substr(quote + 1);
        if (!digits.empty()
            && (digits.front() == 's' || digits.front() == 'S')) {
            digits.remove_prefix(1);
        }
        if (digits.empty()) {
            return std::nullopt;
        }
        if (language == frontend::Language::SystemVerilog2017
            && width_text.empty() && digits.size() == 1U) {
            const auto fill = static_cast<char>(
                std::tolower(
                    static_cast<unsigned char>(digits.front())));
            const auto value =
                fill == '0' ? Logic4::zero
                : fill == '1' ? Logic4::one
                : fill == 'x' ? Logic4::x
                : fill == 'z' || fill == '?' ? Logic4::z
                                                 : Logic4::zero;
            if (fill != '0' && fill != '1' && fill != 'x'
                && fill != 'z' && fill != '?') {
                return std::nullopt;
            }
            return LoweredLiteral{
                PackedLogic4(expected_width, value),
                fill == '0' || fill == '1'
                    ? frontend::ValueDomain::Bit2
                    : frontend::ValueDomain::Logic4};
        }
        const auto base = static_cast<char>(
            std::tolower(static_cast<unsigned char>(digits.front())));
        digits.remove_prefix(1);
        if (base == 'b' || base == 'o' || base == 'd' || base == 'h') {
            return sized_based_value(digits, base, width);
        }
        return std::nullopt;
    }

    const auto value = unsigned_decimal(text);
    return value
        ? std::optional{LoweredLiteral{
              unsigned_value(*value, expected_width),
              frontend::ValueDomain::Bit2}}
        : std::nullopt;
}

}  // namespace fsim::elaboration::elaboration_detail
