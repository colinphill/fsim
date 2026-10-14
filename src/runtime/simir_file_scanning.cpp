// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/file_scanning.hpp"

#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <limits>
#include <tuple>

namespace fsim::runtime::simir {
namespace {

    constexpr std::string_view vhdl_textio_read_marker
        = "\x1f" "fsim-vhdl-textio-read";
    constexpr std::string_view vhdl_textio_read_2008_marker
        = "\x1f" "fsim-vhdl-textio-read-2008";
    constexpr std::string_view vhdl_textio_radix_read_2008_marker
        = "\x1f" "fsim-vhdl-textio-radix-read-2008";
    constexpr std::string_view vhdl_textio_sread_marker
        = "\x1f" "fsim-vhdl-textio-sread";

    [[nodiscard]] bool vhdl_textio_read(
        const FileScan& operation) noexcept
    {
        return operation.string_source && operation.consume_string_source
            && operation.conversions.size() == 1U
            && (operation.conversions.front().prefix
                    == vhdl_textio_read_marker
                || operation.conversions.front().prefix
                    == vhdl_textio_read_2008_marker
                || operation.conversions.front().prefix
                    == vhdl_textio_radix_read_2008_marker
                || operation.conversions.front().prefix
                    == vhdl_textio_sread_marker);
    }

    [[nodiscard]] bool vhdl_textio_read_2008(
        const FileScan& operation) noexcept
    {
        return operation.string_source && operation.consume_string_source
            && operation.conversions.size() == 1U
            && (operation.conversions.front().prefix
                    == vhdl_textio_read_2008_marker
                || operation.conversions.front().prefix
                    == vhdl_textio_radix_read_2008_marker);
    }

    [[nodiscard]] bool vhdl_textio_sread(
        const FileScan& operation) noexcept
    {
        return operation.string_source && operation.consume_string_source
            && operation.conversions.size() == 1U
            && operation.conversions.front().prefix
                == vhdl_textio_sread_marker;
    }

    [[nodiscard]] bool space(const std::int32_t character) noexcept
    {
        return character >= 0
            && std::isspace(static_cast<unsigned char>(character)) != 0;
    }

    [[nodiscard]] bool vhdl_textio_space(
        const std::int32_t character) noexcept
    {
        return character == ' ' || character == '\t' || character == 0xa0;
    }

    class Scanner {
    public:
        Scanner(
            const std::function<std::int32_t()>& read_value,
            const std::function<void(std::int32_t)>& unread_value)
            : read_(read_value)
            , unread_(unread_value)
        {
        }

        [[nodiscard]] std::int32_t read()
        {
            const auto value = read_();
            if (value >= 0)
                ++consumed_;
            return value;
        }
        void unread(const std::int32_t character)
        {
            if (character >= 0) {
                unread_(character);
                if (consumed_ > 0)
                    --consumed_;
            }
        }
        [[nodiscard]] bool skip_space(const bool vhdl_textio = false)
        {
            auto character = read();
            while (vhdl_textio ? vhdl_textio_space(character)
                               : space(character))
                character = read();
            unread(character);
            return character >= 0;
        }

        enum class Match : std::uint8_t { matched,
            mismatch,
            eof };
        [[nodiscard]] Match match(const std::string_view literal)
        {
            for (std::size_t index = 0; index < literal.size(); ++index) {
                const auto expected = static_cast<unsigned char>(literal[index]);
                if (space(expected)) {
                    while (index + 1U < literal.size()
                        && space(static_cast<unsigned char>(literal[index + 1U]))) {
                        ++index;
                    }
                    (void)skip_space();
                    continue;
                }
                const auto character = read();
                if (character < 0)
                    return Match::eof;
                if (character != expected) {
                    unread(character);
                    return Match::mismatch;
                }
            }
            return Match::matched;
        }
        [[nodiscard]] std::size_t consumed() const noexcept
        {
            return consumed_;
        }

    private:
        const std::function<std::int32_t()>& read_;
        const std::function<void(std::int32_t)>& unread_;
        std::size_t consumed_ { };
    };

    void negate_known(PackedLogic4& value)
    {
        bool carry = true;
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            const bool inverted = value.get(bit) == Logic4::zero;
            value.set(bit, inverted != carry ? Logic4::one : Logic4::zero);
            carry = inverted && carry;
        }
    }

    [[nodiscard]] std::optional<InputScanValue> finish_packed_scan(
        PackedLogic4 value,
        const bool negative,
        const InputScanTarget& target)
    {
        bool unknown = false;
        bool nonzero = false;
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            const auto state = value.get(bit);
            unknown = unknown || state == Logic4::x || state == Logic4::z;
            nonzero = nonzero || state == Logic4::one;
            if (target.two_state && (state == Logic4::x || state == Logic4::z)) {
                value.set(bit, Logic4::zero);
            }
        }
        if (target.scalar_kind == SystemVerilogScalarKind::Chandle
            && (unknown || nonzero)) {
            return std::nullopt;
        }
        if (negative) {
            if (unknown && !target.two_state) {
                value.fill(Logic4::x);
            } else {
                negate_known(value);
            }
        }
        return InputScanValue { std::move(value), { }, false };
    }

    [[nodiscard]] std::optional<InputScanValue> packed_digits(
        std::string token,
        const InputScanFormat format,
        const InputScanTarget& target)
    {
        if (target.scalar_kind != SystemVerilogScalarKind::None
            && target.scalar_kind != SystemVerilogScalarKind::Chandle) {
            token.erase(std::remove(token.begin(), token.end(), '_'), token.end());
            const auto scalar = scan_systemverilog_scalar(
                token, target.scalar_kind);
            if (!scalar)
                return std::nullopt;
            auto packed = encode_systemverilog_scalar_payload(scalar.value);
            if (!packed)
                return std::nullopt;
            return InputScanValue { std::move(packed.value), { }, false };
        }
        if (format == InputScanFormat::boolean_value) {
            std::ranges::transform(token, token.begin(), [](const char character) {
                return static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            });
            if (token != "true" && token != "false")
                return std::nullopt;
            PackedLogic4 value(target.width, Logic4::zero);
            if (token == "true")
                value.set(0, Logic4::one);
            return InputScanValue { std::move(value), { }, false };
        }
        bool negative = false;
        if (!token.empty() && (token.front() == '+' || token.front() == '-')) {
            negative = token.front() == '-';
            token.erase(token.begin());
        }
        std::string digits;
        digits.reserve(token.size());
        for (const auto character : token) {
            if (character != '_')
                digits.push_back(character);
        }
        if (digits.empty())
            return std::nullopt;

        if (format == InputScanFormat::decimal
            || format == InputScanFormat::unsigned_decimal) {
            bool unknown = false;
            bool high_impedance = true;
            PackedLogic4 value(target.width, Logic4::zero);
            for (const auto character : digits) {
                if (character >= '0' && character <= '9') {
                    if (unknown)
                        return std::nullopt;
                    auto carry = static_cast<unsigned>(character - '0');
                    for (std::size_t bit = 0; bit < value.width(); ++bit) {
                        const auto expanded = (value.get(bit) == Logic4::one ? 10U : 0U) + carry;
                        value.set(bit, (expanded & 1U) != 0U ? Logic4::one : Logic4::zero);
                        carry = expanded >> 1U;
                    }
                } else if (character == 'x' || character == 'X'
                    || character == '?' || character == 'z'
                    || character == 'Z') {
                    unknown = true;
                    high_impedance = high_impedance
                        && (character == 'z' || character == 'Z');
                } else {
                    return std::nullopt;
                }
            }
            if (unknown) {
                value.fill(high_impedance ? Logic4::z : Logic4::x);
            }
            return finish_packed_scan(
                std::move(value), negative, target);
        }

        unsigned bits { };
        if (format == InputScanFormat::binary)
            bits = 1;
        else if (format == InputScanFormat::octal)
            bits = 3;
        else
            bits = 4;
        if (format == InputScanFormat::hexadecimal && digits.size() > 2U
            && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
            digits.erase(0, 2);
        }
        if (format == InputScanFormat::binary && digits.size() > 2U
            && digits[0] == '0' && (digits[1] == 'b' || digits[1] == 'B')) {
            digits.erase(0, 2);
        }
        if (format == InputScanFormat::octal && digits.size() > 2U
            && digits[0] == '0' && (digits[1] == 'o' || digits[1] == 'O')) {
            digits.erase(0, 2);
        }
        if (digits.empty())
            return std::nullopt;
        const auto digit_mask = (1U << bits) - 1U;
        PackedLogic4 value(target.width, Logic4::zero);
        std::size_t offset = 0;
        for (auto iterator = digits.rbegin(); iterator != digits.rend(); ++iterator) {
            const auto character = *iterator;
            unsigned digit { };
            std::optional<Logic4> unknown_state;
            if (character >= '0' && character <= '9') {
                digit = static_cast<unsigned>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                digit = 10U + static_cast<unsigned>(character - 'a');
            } else if (character >= 'A' && character <= 'F') {
                digit = 10U + static_cast<unsigned>(character - 'A');
            } else if (character == 'x' || character == 'X'
                || character == '?' || character == 'z'
                || character == 'Z') {
                unknown_state = character == 'z' || character == 'Z'
                    ? Logic4::z
                    : Logic4::x;
            } else {
                return std::nullopt;
            }
            if (!unknown_state && digit > digit_mask)
                return std::nullopt;
            for (unsigned bit = 0; bit < bits
                && offset + bit < value.width();
                ++bit) {
                value.set(
                    offset + bit,
                    unknown_state.value_or(
                        (digit & (1U << bit)) != 0U
                            ? Logic4::one
                            : Logic4::zero));
            }
            offset += bits;
        }
        return finish_packed_scan(
            std::move(value), negative, target);
    }

    [[nodiscard]] std::optional<InputScanValue> packed_vhdl_radix_digits(
        std::string_view token,
        const InputScanFormat format,
        const InputScanTarget& target)
    {
        const auto bits_per_digit = format == InputScanFormat::octal
            ? 3U
            : format == InputScanFormat::hexadecimal ? 4U : 0U;
        if (bits_per_digit == 0U || target.width == 0U) {
            return std::nullopt;
        }
        const auto expected_digits
            = (static_cast<std::size_t>(target.width)
                  + bits_per_digit - 1U)
            / bits_per_digit;
        std::string digits;
        digits.reserve(token.size());
        for (const auto character : token) {
            if (character != '_') {
                digits.push_back(character);
            }
        }
        if (digits.size() != expected_digits) {
            return std::nullopt;
        }

        PackedLogic4 value(target.width, Logic4::zero);
        std::size_t bit_offset { };
        for (auto iterator = digits.rbegin(); iterator != digits.rend();
            ++iterator) {
            const auto character = *iterator;
            unsigned digit { };
            if (character >= '0' && character <= '9') {
                digit = static_cast<unsigned>(character - '0');
            } else if (format == InputScanFormat::hexadecimal
                && character >= 'a' && character <= 'f') {
                digit = 10U + static_cast<unsigned>(character - 'a');
            } else if (format == InputScanFormat::hexadecimal
                && character >= 'A' && character <= 'F') {
                digit = 10U + static_cast<unsigned>(character - 'A');
            } else {
                return std::nullopt;
            }
            const auto maximum_digit = (1U << bits_per_digit) - 1U;
            if (digit > maximum_digit) {
                return std::nullopt;
            }
            for (unsigned bit = 0; bit < bits_per_digit; ++bit) {
                const auto set = (digit & (1U << bit)) != 0U;
                if (bit_offset + bit >= value.width()) {
                    if (set) {
                        return std::nullopt;
                    }
                    continue;
                }
                value.set(bit_offset + bit,
                    set ? Logic4::one : Logic4::zero);
            }
            bit_offset += bits_per_digit;
        }
        return InputScanValue { std::move(value), { }, false };
    }

    [[nodiscard]] InputScanValue packed_text(
        const std::string_view text, const InputScanTarget& target)
    {
        return { pack_file_text(text, target.width), { }, false };
    }

    [[nodiscard]] std::uint32_t native_word(
        const std::string_view bytes, const std::size_t offset)
    {
        std::uint32_t result { };
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            const auto shift = std::endian::native == std::endian::little
                ? byte * 8U
                : (3U - byte) * 8U;
            result |= static_cast<std::uint32_t>(
                          static_cast<unsigned char>(bytes[offset + byte]))
                << shift;
        }
        return result;
    }

    [[nodiscard]] std::optional<InputScanValue> packed_unformatted(
        const std::string_view bytes,
        const InputScanFormat format,
        const InputScanTarget& target)
    {
        PackedLogic4 value(target.width, Logic4::zero);
        if (format == InputScanFormat::unformatted2) {
            const auto expected = (static_cast<std::size_t>(target.width) + 7U)
                / 8U;
            if (bytes.size() != expected)
                return std::nullopt;
            for (std::size_t byte = 0; byte < bytes.size(); ++byte) {
                const auto logical_byte = std::endian::native
                        == std::endian::little
                    ? byte
                    : bytes.size() - byte - 1U;
                const auto payload = static_cast<unsigned char>(bytes[byte]);
                for (std::size_t bit = 0;
                    bit < 8U && logical_byte * 8U + bit < value.width();
                    ++bit) {
                    value.set(
                        logical_byte * 8U + bit,
                        (payload & (1U << bit)) != 0U
                            ? Logic4::one
                            : Logic4::zero);
                }
            }
        } else {
            const auto words = (static_cast<std::size_t>(target.width) + 31U)
                / 32U;
            if (bytes.size() != words * 8U)
                return std::nullopt;
            for (std::size_t word = 0; word < words; ++word) {
                const auto aval = native_word(bytes, word * 8U);
                const auto bval = native_word(bytes, word * 8U + 4U);
                for (std::size_t bit = 0;
                    bit < 32U && word * 32U + bit < value.width();
                    ++bit) {
                    const bool a = (aval & (UINT32_C(1) << bit)) != 0U;
                    const bool b = (bval & (UINT32_C(1) << bit)) != 0U;
                    value.set(
                        word * 32U + bit,
                        !b ? (a ? Logic4::one : Logic4::zero)
                           : (a ? Logic4::x : Logic4::z));
                }
            }
        }
        return finish_packed_scan(std::move(value), false, target);
    }

    [[nodiscard]] bool numeric_character(
        const std::int32_t character,
        const InputScanFormat format,
        const bool first,
        const bool vhdl_textio) noexcept
    {
        if (character < 0)
            return false;
        if (format == InputScanFormat::boolean_value) {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z');
        }
        if (format == InputScanFormat::real) {
            return (character >= '0' && character <= '9') || character == '.'
                || character == 'e' || character == 'E'
                || character == '+' || character == '-' || character == '_';
        }
        if (first && (character == '+' || character == '-'))
            return true;
        if (vhdl_textio
            && (format == InputScanFormat::decimal
                || format == InputScanFormat::unsigned_decimal)) {
            return (character >= '0' && character <= '9')
                || character == '_';
        }
        if (character == '_' || character == '?' || character == 'x'
            || character == 'X' || character == 'z' || character == 'Z')
            return true;
        if (character >= '0' && character <= '9') {
            if (format == InputScanFormat::binary)
                return character <= '1';
            if (format == InputScanFormat::octal)
                return character <= '7';
            return true;
        }
        return format == InputScanFormat::hexadecimal
            && ((character >= 'a' && character <= 'f')
                || (character >= 'A' && character <= 'F'));
    }

    [[nodiscard]] bool valid_vhdl_numeric_separators(
        const std::string_view text) noexcept
    {
        for (std::size_t index = 0U; index < text.size(); ++index) {
            if (text[index] == '_'
                && (index == 0U || index + 1U == text.size()
                    || text[index - 1U] < '0' || text[index - 1U] > '9'
                    || text[index + 1U] < '0' || text[index + 1U] > '9')) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool valid_vhdl_real_literal(
        const std::string_view text) noexcept
    {
        std::size_t index { };
        if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
            ++index;
        }
        const auto consume_integer = [&]() {
            if (index >= text.size()
                || text[index] < '0' || text[index] > '9') {
                return false;
            }
            ++index;
            while (index < text.size()) {
                if (text[index] >= '0' && text[index] <= '9') {
                    ++index;
                    continue;
                }
                if (text[index] != '_'
                    || index + 1U >= text.size()
                    || text[index + 1U] < '0'
                    || text[index + 1U] > '9') {
                    break;
                }
                index += 2U;
            }
            return true;
        };
        if (!consume_integer() || index >= text.size()
            || text[index] != '.') {
            return false;
        }
        ++index;
        if (!consume_integer()) {
            return false;
        }
        if (index < text.size()
            && (text[index] == 'e' || text[index] == 'E')) {
            ++index;
            if (index < text.size()
                && (text[index] == '+' || text[index] == '-')) {
                ++index;
            }
            if (!consume_integer()) {
                return false;
            }
        }
        return index == text.size();
    }

    [[nodiscard]] std::tuple<std::string, bool, bool> read_conversion_text(
        Scanner& scanner,
        const InputScanConversion& conversion,
        const bool vhdl_textio,
        const bool vhdl_textio_2008,
        const bool vhdl_textio_radix_read_2008,
        const bool vhdl_textio_sread)
    {
        std::string text;
        bool eof = false;
        const bool unformatted = conversion.format
                == InputScanFormat::unformatted2
            || conversion.format == InputScanFormat::unformatted4;
        const auto default_limit = unformatted
            ? conversion.format == InputScanFormat::unformatted2
                ? (static_cast<std::size_t>(conversion.target.width) + 7U) / 8U
                : ((static_cast<std::size_t>(conversion.target.width) + 31U)
                      / 32U)
                    * 8U
            : conversion.format == InputScanFormat::character
            ? 1U
            : static_cast<std::uint32_t>(maximum_string_bytes);
        // The executor resolves this sentinel from the destination
        // STRING's retained constraint. A null STRING completes without
        // consuming any LINE characters.
        if (vhdl_textio
            && conversion.format == InputScanFormat::string
            && conversion.maximum_characters == 0U
            && !vhdl_textio_sread) {
            return { std::move(text), true, false };
        }
        const auto limit = conversion.maximum_characters == 0
            ? default_limit
            : conversion.maximum_characters;
        if (vhdl_textio_sread
            && conversion.format == InputScanFormat::string) {
            (void)scanner.skip_space(true);
            while (text.size() < conversion.maximum_characters) {
                const auto character = scanner.read();
                if (character < 0) {
                    eof = true;
                    break;
                }
                if (vhdl_textio_space(character)) {
                    scanner.unread(character);
                    break;
                }
                text.push_back(static_cast<char>(character));
            }
            return { std::move(text), true, eof };
        }
        if (vhdl_textio
            && conversion.format == InputScanFormat::string) {
            while (text.size() < limit) {
                const auto character = scanner.read();
                if (character < 0) {
                    eof = true;
                    break;
                }
                text.push_back(static_cast<char>(character));
            }
            const bool complete = text.size() == limit;
            return { std::move(text), complete, eof };
        }
        if (vhdl_textio
            && conversion.format == InputScanFormat::binary) {
            (void)scanner.skip_space(true);
            std::size_t bit_count { };
            while (bit_count < conversion.target.width
                && text.size() < maximum_string_bytes) {
                const auto character = scanner.read();
                if (character < 0) {
                    eof = true;
                    break;
                }
                if (character == '_' && vhdl_textio_2008) {
                    if (bit_count == 0U
                        || text.size() + 1U >= maximum_string_bytes) {
                        scanner.unread(character);
                        break;
                    }
                    const auto next = scanner.read();
                    if (next != '0' && next != '1') {
                        scanner.unread(next);
                        scanner.unread(character);
                        break;
                    }
                    text.push_back('_');
                    text.push_back(static_cast<char>(next));
                    ++bit_count;
                    continue;
                }
                if (character != '0' && character != '1') {
                    scanner.unread(character);
                    break;
                }
                text.push_back(static_cast<char>(character));
                ++bit_count;
            }
            const bool complete = bit_count == conversion.target.width;
            return { std::move(text), complete, eof };
        }
        if (vhdl_textio_radix_read_2008
            && (conversion.format == InputScanFormat::octal
                || conversion.format == InputScanFormat::hexadecimal)) {
            (void)scanner.skip_space(true);
            const auto bits_per_digit = conversion.format
                    == InputScanFormat::octal
                ? 3U
                : 4U;
            const auto expected_digits
                = (static_cast<std::size_t>(conversion.target.width)
                      + bits_per_digit - 1U)
                / bits_per_digit;
            std::size_t digit_count { };
            bool previous_underline { };
            while (digit_count < expected_digits
                && text.size() < maximum_string_bytes) {
                const auto character = scanner.read();
                if (character < 0) {
                    eof = true;
                    break;
                }
                const bool digit = conversion.format == InputScanFormat::octal
                    ? character >= '0' && character <= '7'
                    : (character >= '0' && character <= '9')
                        || (character >= 'a' && character <= 'f')
                        || (character >= 'A' && character <= 'F');
                if (digit) {
                    text.push_back(static_cast<char>(character));
                    ++digit_count;
                    previous_underline = false;
                } else if (character == '_' && digit_count != 0U
                    && !previous_underline) {
                    text.push_back('_');
                    previous_underline = true;
                } else {
                    scanner.unread(character);
                    break;
                }
            }
            return { std::move(text),
                digit_count == expected_digits, eof };
        }
        if (vhdl_textio
            && conversion.format == InputScanFormat::boolean_value) {
            (void)scanner.skip_space(true);
            auto character = scanner.read();
            if (character < 0) {
                return { std::move(text), false, true };
            }
            const auto first = static_cast<char>(std::tolower(
                static_cast<unsigned char>(character)));
            if (first != 't' && first != 'f') {
                scanner.unread(character);
                return { std::move(text), false, false };
            }
            text.push_back(static_cast<char>(character));
            for (;;) {
                character = scanner.read();
                if (character < 0) {
                    eof = true;
                    break;
                }
                const bool identifier_character
                    = (character >= 'a' && character <= 'z')
                    || (character >= 'A' && character <= 'Z')
                    || (character >= '0' && character <= '9')
                    || character == '_';
                if (!identifier_character) {
                    scanner.unread(character);
                    break;
                }
                text.push_back(static_cast<char>(character));
            }
            std::ranges::transform(text, text.begin(), [](const char value) {
                return static_cast<char>(std::tolower(
                    static_cast<unsigned char>(value)));
            });
            const bool valid = text == "true" || text == "false";
            return { std::move(text), valid, eof };
        }
        if (conversion.format != InputScanFormat::character && !unformatted
            && !(vhdl_textio
                && conversion.format == InputScanFormat::string)) {
            (void)scanner.skip_space(vhdl_textio);
        }
        while (text.size() < limit) {
            const auto character = scanner.read();
            if (character < 0) {
                eof = true;
                break;
            }
            const bool numeric_vhdl_textio = vhdl_textio
                && (conversion.format == InputScanFormat::decimal
                    || conversion.format
                        == InputScanFormat::unsigned_decimal
                    || conversion.format == InputScanFormat::real);
            if (numeric_vhdl_textio && character == '_') {
                const auto previous_is_digit = !text.empty()
                    && text.back() >= '0' && text.back() <= '9';
                if (!previous_is_digit || text.size() + 1U >= limit) {
                    scanner.unread(character);
                    break;
                }
                const auto next = scanner.read();
                if (next < '0' || next > '9') {
                    scanner.unread(next);
                    scanner.unread(character);
                    break;
                }
                text.push_back('_');
                text.push_back(static_cast<char>(next));
                continue;
            }
            const bool accepted = unformatted
                || conversion.format == InputScanFormat::character
                || (conversion.format == InputScanFormat::string
                        ? !space(character)
                        : numeric_character(character, conversion.format,
                              text.empty(), vhdl_textio));
            if (!accepted) {
                scanner.unread(character);
                break;
            }
            text.push_back(static_cast<char>(character));
        }
        const bool complete_character =
            (conversion.format != InputScanFormat::character && !unformatted)
            || text.size() == limit;
        return { std::move(text), complete_character, eof };
    }

} // namespace

PackedLogic4 pack_file_text(
    const std::string_view text, const std::uint32_t width)
{
    PackedLogic4 value(width, Logic4::zero);
    std::size_t offset = 0;
    for (auto iterator = text.rbegin(); iterator != text.rend(); ++iterator) {
        const auto character = static_cast<unsigned char>(*iterator);
        for (unsigned bit = 0; bit < 8U && offset + bit < value.width(); ++bit) {
            value.set(
                offset + bit,
                (character & (1U << bit)) != 0U ? Logic4::one : Logic4::zero);
        }
        offset += 8U;
    }
    return value;
}

InputScanResult scan_formatted_input(
    const FileScan& operation,
    const std::function<std::int32_t()>& read,
    const std::function<void(std::int32_t)>& unread)
{
    Scanner scanner { read, unread };
    const bool vhdl_textio_line = vhdl_textio_read(operation);
    const bool vhdl_textio_2008 = vhdl_textio_read_2008(operation);
    const bool vhdl_textio_radix_read_2008
        = operation.string_source && operation.consume_string_source
        && operation.conversions.size() == 1U
        && operation.conversions.front().prefix
            == vhdl_textio_radix_read_2008_marker;
    const bool is_vhdl_textio_sread = vhdl_textio_sread(operation);
    InputScanResult result;
    result.values.resize(operation.conversions.size());
    for (std::size_t index = 0; index < operation.conversions.size(); ++index) {
        const auto& conversion = operation.conversions[index];
        const auto prefix = vhdl_textio_line
            ? Scanner::Match::matched
            : scanner.match(conversion.prefix);
        if (prefix != Scanner::Match::matched) {
            if (prefix == Scanner::Match::eof && result.assignments == 0)
                result.assignments = -1;
            result.consumed = scanner.consumed();
            return result;
        }
        auto [text, complete, eof] = read_conversion_text(
            scanner, conversion, vhdl_textio_line, vhdl_textio_2008,
            vhdl_textio_radix_read_2008, is_vhdl_textio_sread);
        const bool empty_string_read = vhdl_textio_line
            && conversion.format == InputScanFormat::string
            && conversion.maximum_characters == 0U && complete;
        if ((!is_vhdl_textio_sread && text.empty()
                && !empty_string_read)
            || !complete) {
            if (eof && result.assignments == 0)
                result.assignments = -1;
            result.consumed = scanner.consumed();
            return result;
        }
        if (conversion.suppress)
            continue;
        std::optional<InputScanValue> value;
        const auto sread_length = text.size();
        const bool unformatted = conversion.format
                == InputScanFormat::unformatted2
            || conversion.format == InputScanFormat::unformatted4;
        const bool text_format = conversion.format == InputScanFormat::character
            || conversion.format == InputScanFormat::string;
        if (unformatted) {
            value = packed_unformatted(
                text, conversion.format, conversion.target);
        } else if (text_format) {
            const bool string_target = conversion.target.kind
                    == InputScanTargetKind::string_register
                || conversion.target.kind == InputScanTargetKind::string_object;
            if (is_vhdl_textio_sread && string_target
                && text.size() < conversion.maximum_characters) {
                // Elements after STRLEN are unspecified by TextIO SREAD;
                // spaces keep the fixed-length STRING representation intact.
                text.resize(conversion.maximum_characters, ' ');
            }
            value = string_target
                ? InputScanValue { PackedLogic4 { 1, Logic4::zero }, std::move(text), true }
                : packed_text(text, conversion.target);
        } else {
            const bool valid_textio_number
                = !vhdl_textio_line
                || vhdl_textio_radix_read_2008
                || (conversion.format == InputScanFormat::real
                        ? valid_vhdl_real_literal(text)
                        : ((conversion.format != InputScanFormat::decimal
                                && conversion.format
                                    != InputScanFormat::unsigned_decimal)
                            || valid_vhdl_numeric_separators(text)));
            value = valid_textio_number
                ? (vhdl_textio_radix_read_2008
                        ? packed_vhdl_radix_digits(
                              text, conversion.format, conversion.target)
                        : packed_digits(std::move(text), conversion.format,
                              conversion.target))
                : std::nullopt;
        }
        if (!value) {
            result.consumed = scanner.consumed();
            return result;
        }
        result.values[index] = std::move(value);
        if (is_vhdl_textio_sread) {
            result.assignments = static_cast<std::int32_t>(sread_length);
        } else {
            ++result.assignments;
        }
    }
    (void)scanner.match(operation.trailing_text);
    result.consumed = scanner.consumed();
    return result;
}

InputScanResult scan_formatted_string(
    const FileScan& operation, const std::string_view input)
{
    std::size_t offset { };
    const std::function<std::int32_t()> read = [&]() {
        return offset < input.size()
            ? static_cast<std::int32_t>(static_cast<unsigned char>(input[offset++]))
            : -1;
    };
    const std::function<void(std::int32_t)> unread = [&](const std::int32_t value) {
        if (value >= 0 && offset > 0)
            --offset;
    };
    return scan_formatted_input(operation, read, unread);
}

} // namespace fsim::runtime::simir
