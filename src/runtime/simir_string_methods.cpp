// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include "simir_internal.hpp"

#include <algorithm>
#include <array>
#include <charconv>

namespace fsim::runtime::simir {
namespace {

    [[nodiscard]] std::uint32_t fold_ascii(
        const std::uint32_t value,
        const bool case_insensitive) noexcept
    {
        return case_insensitive && value >= 'A' && value <= 'Z'
            ? value + ('a' - 'A')
            : value;
    }

} // namespace

std::uint32_t string_getc(
    const std::string_view value,
    const std::optional<std::int32_t> index)
{
    if (!index || *index < 0)
        return 0;
    try {
        return systemverilog_string_at(value, static_cast<std::size_t>(*index));
    } catch (const std::out_of_range&) {
        return 0;
    }
}

std::int32_t string_compare(
    const std::string_view lhs,
    const std::string_view rhs,
    const bool case_insensitive)
{
    if (!case_insensitive)
        return systemverilog_string_compare(lhs, rhs);
    const auto count = std::min(lhs.size(), rhs.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto left = fold_ascii(
            static_cast<unsigned char>(lhs[index]), true);
        const auto right = fold_ascii(
            static_cast<unsigned char>(rhs[index]), true);
        if (left != right) {
            return left < right ? -1 : 1;
        }
    }
    return lhs.size() == rhs.size()
        ? 0
        : lhs.size() < rhs.size() ? -1 : 1;
}

std::string string_change_case(
    const std::string_view value,
    const bool uppercase)
{
    std::string result { value };
    (void)systemverilog_string_length(result);
    for (auto& character : result) {
        const auto byte = static_cast<unsigned char>(character);
        if (uppercase && byte >= 'a' && byte <= 'z') {
            character = static_cast<char>(byte - ('a' - 'A'));
        } else if (!uppercase && byte >= 'A' && byte <= 'Z') {
            character = static_cast<char>(byte + ('a' - 'A'));
        }
    }
    return result;
}

std::string string_substr(
    const std::string_view value,
    const std::optional<std::int32_t> first,
    const std::optional<std::int32_t> last)
{
    if (!first || !last || *first < 0 || *last < *first
        || static_cast<std::size_t>(*last)
            >= systemverilog_string_length(value)) {
        return { };
    }
    return systemverilog_string_slice(
        value,
        static_cast<std::size_t>(*first),
        static_cast<std::size_t>(*last));
}

void string_putc(
    std::string& value,
    const std::optional<std::int32_t> index,
    const std::optional<std::uint32_t> character)
{
    if (!index || !character || *index < 0)
        return;
    if (static_cast<std::size_t>(*index)
        >= systemverilog_string_length(value))
        return;
    systemverilog_string_replace(
        value, static_cast<std::size_t>(*index), *character,
        maximum_string_bytes);
}

std::int32_t string_to_integer(
    const std::string_view value,
    const unsigned radix)
{
    std::uint32_t result { };
    bool negative { };
    bool started { };
    for (const auto stored : value) {
        const auto character = static_cast<unsigned char>(stored);
        if (character == '_') {
            continue;
        }
        if (!started && radix == 10
            && (character == '+' || character == '-')) {
            negative = character == '-';
            started = true;
            continue;
        }
        unsigned digit { };
        if (character >= '0' && character <= '9') {
            digit = static_cast<unsigned>(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = static_cast<unsigned>(character - 'a') + 10U;
        } else if (character >= 'A' && character <= 'F') {
            digit = static_cast<unsigned>(character - 'A') + 10U;
        } else {
            break;
        }
        if (digit >= radix) {
            break;
        }
        started = true;
        result = result * radix + digit;
    }
    return static_cast<std::int32_t>(negative ? 0U - result : result);
}

std::string string_from_integer(
    const std::int32_t value,
    const unsigned radix)
{
    std::array<char, 35> buffer { };
    const auto converted = radix == 10
        ? std::to_chars(buffer.data(), buffer.data() + buffer.size(), value)
        : std::to_chars(
              buffer.data(), buffer.data() + buffer.size(),
              static_cast<std::uint32_t>(value), static_cast<int>(radix));
    return converted.ec == std::errc { }
        ? std::string { buffer.data(), converted.ptr }
        : std::string { };
}

std::uint32_t checked_vhdl_textio_field_width(
    const PackedLogic4& value)
{
    if (value.width() == 0U || value.is_logic9()
        || std::ranges::any_of(
            value.bval_words(), [](const auto word) { return word != 0U; })) {
        throw std::invalid_argument {
            "VHDL TextIO field contains unknown bits"
        };
    }
    const auto aval = value.aval_words();
    if (std::ranges::any_of(
            aval.subspan(1U), [](const auto word) { return word != 0U; })
        || aval.front() > maximum_string_bytes) {
        throw std::length_error {
            "VHDL TextIO field exceeds the 4096-byte limit"
        };
    }
    return static_cast<std::uint32_t>(aval.front());
}

// An integer literal with optional leading and trailing whitespace, sign,
// base (`16#FF#`), underscores and exponent (IEEE 1076-2008 15.5).
[[nodiscard]] std::int64_t vhdl_integer_value(const std::string_view text)
{
    const auto fail = [&]() -> std::int64_t {
        throw std::invalid_argument {
            "VHDL 'VALUE: \"" + std::string { text }
            + "\" is not an integer literal"
        };
    };
    const auto space = [](const char character) {
        return character == ' ' || character == '\t'
            || character == '\xa0';
    };
    std::size_t begin { };
    std::size_t end { text.size() };
    while (begin < end && space(text[begin]))
        ++begin;
    while (end > begin && space(text[end - 1U]))
        --end;
    auto literal = text.substr(begin, end - begin);
    bool negative { };
    if (!literal.empty() && (literal.front() == '-' || literal.front() == '+')) {
        negative = literal.front() == '-';
        literal.remove_prefix(1U);
    }
    const auto digit_value = [](const char character) -> int {
        if (character >= '0' && character <= '9')
            return character - '0';
        if (character >= 'a' && character <= 'f')
            return character - 'a' + 10;
        if (character >= 'A' && character <= 'F')
            return character - 'A' + 10;
        return -1;
    };
    // Digits with single underscores between them.
    const auto digits = [&](std::string_view part, const int radix,
                            unsigned __int128& value) {
        if (part.empty() || part.front() == '_' || part.back() == '_')
            return false;
        bool underscore { };
        for (const auto character : part) {
            if (character == '_') {
                if (underscore)
                    return false;
                underscore = true;
                continue;
            }
            underscore = false;
            const auto digit = digit_value(character);
            if (digit < 0 || digit >= radix)
                return false;
            value = value * static_cast<unsigned>(radix)
                + static_cast<unsigned>(digit);
            if (value > (static_cast<unsigned __int128>(1) << 64))
                return false;
        }
        return true;
    };
    unsigned __int128 value { };
    auto exponent_part = std::string_view { };
    const auto hash = literal.find_first_of("#:");
    if (hash != std::string_view::npos) {
        unsigned __int128 radix { };
        const auto close = literal.find(literal[hash], hash + 1U);
        if (!digits(literal.substr(0U, hash), 10, radix) || radix < 2U
            || radix > 16U || close == std::string_view::npos
            || !digits(literal.substr(hash + 1U, close - hash - 1U),
                static_cast<int>(radix), value)) {
            return fail();
        }
        exponent_part = literal.substr(close + 1U);
    } else {
        const auto exponent = literal.find_first_of("eE");
        if (!digits(literal.substr(0U, exponent), 10, value))
            return fail();
        if (exponent != std::string_view::npos)
            exponent_part = literal.substr(exponent);
    }
    if (!exponent_part.empty()) {
        if (exponent_part.front() != 'e' && exponent_part.front() != 'E')
            return fail();
        exponent_part.remove_prefix(1U);
        if (!exponent_part.empty() && exponent_part.front() == '+')
            exponent_part.remove_prefix(1U);
        unsigned __int128 exponent { };
        if (!digits(exponent_part, 10, exponent) || exponent > 20U)
            return fail();
        for (unsigned __int128 index { }; index < exponent; ++index) {
            value *= 10U;
            if (value > (static_cast<unsigned __int128>(1) << 64))
                return fail();
        }
    }
    const auto limit = static_cast<unsigned __int128>(
                           std::numeric_limits<std::int64_t>::max())
        + (negative ? 1U : 0U);
    if (value > limit)
        return fail();
    return negative ? static_cast<std::int64_t>(-static_cast<__int128>(value))
                    : static_cast<std::int64_t>(value);
}

[[nodiscard]] std::uint64_t string_to_real_bits(
    const std::string_view value)
{
    std::string compact;
    compact.reserve(value.size());
    for (const auto stored : value) {
        const auto byte = static_cast<unsigned char>(stored);
        if (byte == '_')
            continue;
        if (byte > 0x7fU) {
            return SystemVerilogScalarValue::real(0.0).bits;
        }
        compact.push_back(static_cast<char>(byte));
    }
    const auto parsed = scan_systemverilog_scalar(
        compact, SystemVerilogScalarKind::Real);
    return parsed ? parsed.value.bits
                  : SystemVerilogScalarValue::real(0.0).bits;
}

[[nodiscard]] std::string string_from_real_bits(
    const std::uint64_t bits)
{
    const auto formatted = format_systemverilog_scalar(
        { SystemVerilogScalarKind::Real, bits });
    if (!formatted) {
        throw std::invalid_argument { "realtoa could not format the real value" };
    }
    return formatted.text;
}

StringMethodResult execute_string_method(
    const StringMethodOperator operation,
    std::string& source,
    const std::string_view argument,
    const std::optional<std::int32_t> first,
    const std::optional<std::int32_t> second,
    const std::optional<std::uint64_t> first_bits)
{
    if (operation == StringMethodOperator::getc) {
        return { { string_getc(source, first) }, std::nullopt, std::nullopt };
    }
    if (operation == StringMethodOperator::putc) {
        string_putc(
            source, first,
            second ? std::optional<std::uint32_t> {
                         static_cast<std::uint32_t>(*second) }
                   : std::nullopt);
        return { };
    }
    if (operation == StringMethodOperator::toupper
        || operation == StringMethodOperator::tolower) {
        return {
            std::nullopt,
            std::nullopt,
            string_change_case(
                source, operation == StringMethodOperator::toupper)
        };
    }
    if (operation == StringMethodOperator::compare
        || operation == StringMethodOperator::icompare) {
        return { { static_cast<std::uint32_t>(string_compare(
                     source, argument,
                     operation == StringMethodOperator::icompare)) },
            std::nullopt,
            std::nullopt };
    }
    if (operation == StringMethodOperator::substr) {
        return {
            std::nullopt, std::nullopt,
            string_substr(source, first, second)
        };
    }
    if (operation >= StringMethodOperator::atoi
        && operation <= StringMethodOperator::atobin) {
        const auto radix = operation == StringMethodOperator::atoi
            ? 10U
            : operation == StringMethodOperator::atohex
            ? 16U
            : operation == StringMethodOperator::atooct ? 8U
                                                        : 2U;
        return { { static_cast<std::uint32_t>(
                     string_to_integer(source, radix)) },
            std::nullopt, std::nullopt };
    }
    if (operation == StringMethodOperator::atoreal) {
        return { std::nullopt, string_to_real_bits(source), std::nullopt };
    }
    if (operation == StringMethodOperator::vhdl_enumeration_value) {
        const auto space = [](const char character) {
            return character == ' ' || character == '\t'
                || character == '\xa0';
        };
        std::string_view text { source };
        while (!text.empty() && space(text.front()))
            text.remove_prefix(1U);
        while (!text.empty() && space(text.back()))
            text.remove_suffix(1U);
        const auto equal = [&](const std::string_view literal) {
            // Identifiers compare without case; character literals exactly.
            if (literal.size() != text.size())
                return false;
            if (!literal.empty() && literal.front() == '\'')
                return literal == text;
            return std::ranges::equal(literal, text,
                [](const char left, const char right) {
                    return std::tolower(static_cast<unsigned char>(left))
                        == std::tolower(static_cast<unsigned char>(right));
                });
        };
        std::uint64_t position { };
        std::size_t start { };
        while (start <= argument.size()) {
            const auto end = argument.find('\n', start);
            const auto literal = argument.substr(start,
                end == std::string_view::npos ? std::string_view::npos
                                              : end - start);
            if (equal(literal))
                return { std::nullopt, position, std::nullopt };
            if (end == std::string_view::npos)
                break;
            start = end + 1U;
            ++position;
        }
        throw std::invalid_argument {
            "VHDL 'VALUE: \"" + source
            + "\" is not a literal of the enumeration type"
        };
    }
    if (operation == StringMethodOperator::vhdl_integer_value) {
        return { std::nullopt,
            static_cast<std::uint64_t>(vhdl_integer_value(source)),
            std::nullopt };
    }
    if (operation == StringMethodOperator::realtoa) {
        if (!first_bits) {
            throw std::invalid_argument { "realtoa input contains X or Z" };
        }
        source = string_from_real_bits(*first_bits);
        return { };
    }
    const auto radix = operation == StringMethodOperator::itoa
        ? 10U
        : operation == StringMethodOperator::hextoa
        ? 16U
        : operation == StringMethodOperator::octtoa ? 8U
                                                    : 2U;
    source = string_from_integer(first.value_or(0), radix);
    return { };
}

namespace {

std::string vhdl_time_text(const PackedLogic4& value,
    const std::uint32_t unit_index,
    const std::uint64_t resolution_femtoseconds)
{
    constexpr std::array<std::pair<std::string_view, std::uint64_t>, 8U>
        units { {
            { "fs", 1U },
            { "ps", 1'000U },
            { "ns", 1'000'000U },
            { "us", 1'000'000'000U },
            { "ms", 1'000'000'000'000U },
            { "sec", 1'000'000'000'000'000U },
            { "min", 60'000'000'000'000'000U },
            { "hr", 3'600'000'000'000'000'000U },
        } };
    const auto word = value.low_word();
    if (word.bval != 0) {
        throw std::runtime_error { "VHDL TIME value contains X or Z" };
    }
    const auto ticks = static_cast<std::int64_t>(word.aval);
    const bool negative = ticks < 0;
    const auto magnitude = static_cast<unsigned __int128>(
        negative ? -static_cast<__int128>(ticks)
                 : static_cast<__int128>(ticks))
        * std::max<std::uint64_t>(resolution_femtoseconds, 1U);
    const auto& unit = unit_index < units.size() ? units[unit_index]
                                                 : units.front();
    auto whole = magnitude / unit.second;
    auto remainder = static_cast<std::uint64_t>(magnitude % unit.second);
    // TIME'LOW and TIME'HIGH in femtoseconds exceed 64 bits at coarser
    // resolutions.
    std::string digits;
    do {
        digits.insert(digits.begin(), static_cast<char>('0' + whole % 10U));
        whole /= 10U;
    } while (whole != 0U);
    std::string text = negative ? "-" : "";
    text += digits;
    if (remainder != 0U) {
        // A value that is not a multiple of the unit is written with the
        // fraction it needs (IEEE 1076-2008 16.4).
        std::string fraction;
        for (auto scale = unit.second / 10U; scale != 0U && remainder != 0U;
             scale /= 10U) {
            fraction += static_cast<char>('0' + remainder / scale);
            remainder %= scale;
        }
        // The fraction is written like a REAL image, to 15 significant
        // digits.
        auto number = digits + fraction;
        const auto first = number.find_first_not_of('0');
        constexpr std::size_t significant = 15U;
        if (first != std::string::npos && number.size() - first > significant) {
            const auto keep = first + significant;
            bool carry = number[keep] >= '5';
            number.resize(keep);
            for (auto index = keep; carry && index-- > 0U;) {
                if (number[index] == '9') {
                    number[index] = '0';
                } else {
                    ++number[index];
                    carry = false;
                }
            }
            if (carry) {
                number.insert(number.begin(), '1');
                digits.insert(digits.begin(), '0');
            }
            number.resize(std::max(number.size(), digits.size()), '0');
        }
        auto whole_text = number.substr(0U, digits.size());
        auto fraction_text = number.substr(digits.size());
        while (!fraction_text.empty() && fraction_text.back() == '0') {
            fraction_text.pop_back();
        }
        text = negative ? "-" : "";
        text += whole_text;
        if (!fraction_text.empty()) {
            text += '.';
            text += fraction_text;
        }
    }
    text += ' ';
    text += unit.first;
    return text;
}

} // namespace

void execute_string_format(
    const StringMethod& operation,
    std::string& destination,
    const PackedLogic4& packed_value,
    const std::string_view string_value,
    const std::uint64_t tick,
    const SystemVerilogTimeFormat& time_format)
{
    std::string formatted;
    if (operation.operation == StringMethodOperator::format_string) {
        formatted = string_value;
        if (formatted.size() < operation.minimum_width) {
            const auto padding = static_cast<std::size_t>(operation.minimum_width)
                - formatted.size();
            if (operation.left_justify) {
                formatted.append(padding, ' ');
            } else {
                formatted.insert(0U, padding, ' ');
            }
        }
    } else if (operation.operation
        == StringMethodOperator::format_vhdl_time) {
        formatted = vhdl_time_text(packed_value, operation.precision,
            time_format.resolution_femtoseconds);
    } else if (operation.operation == StringMethodOperator::format_time) {
        // An integral value is in its module's time unit (IEEE 1800-2017
        // 21.2.1.3); without a value the current time is shown.
        const auto value_tick = operation.time_unit_femtoseconds != 0U
            ? integral_time_ticks(packed_value, operation.time_signed,
                  operation.time_unit_femtoseconds, time_format)
            : std::optional<SimulationTick> { tick };
        formatted = value_tick
            ? make_time_output(
                  { }, { }, *value_tick, time_format,
                  operation.use_timeformat_width,
                  operation.minimum_width,
                  operation.left_justify, operation.zero_pad)
            : make_formatted_output(
                  { }, { }, OutputFormat::decimal, packed_value,
                  operation.time_signed, operation.suppress_leading_zero,
                  operation.minimum_width, operation.left_justify,
                  operation.zero_pad);
    } else {
        formatted = make_formatted_output(
            { }, { }, operation.format, packed_value,
            operation.signed_decimal,
            operation.suppress_leading_zero,
            operation.minimum_width,
            operation.left_justify,
            operation.zero_pad,
            operation.scalar_kind,
            operation.precision);
    }
    if (formatted.size() > maximum_string_bytes - destination.size()) {
        throw std::length_error {
            "formatted string exceeds 4096-byte limit"
        };
    }
    destination += formatted;
}

void Interpreter::Impl::execute_string(
    ProcessState& process,
    const StringMethod& operation)
{
    auto& source = get_string_register(process, operation.source);
    const bool dynamic_string_field = operation.operation
        == StringMethodOperator::format_string_dynamic_field;
    const bool dynamic_packed_field = operation.operation
        == StringMethodOperator::format_packed_dynamic_field;
    const bool string_format = operation.operation
            == StringMethodOperator::format_string
        || dynamic_string_field;
    const bool packed_format = operation.operation
            == StringMethodOperator::format_packed
        || dynamic_packed_field;
    const bool format = packed_format || string_format
        || operation.operation == StringMethodOperator::format_time
        || operation.operation == StringMethodOperator::format_vhdl_time;
    if (format) {
        auto normalized = operation;
        if (dynamic_string_field || dynamic_packed_field) {
            const auto width_register = dynamic_string_field
                ? operation.first : operation.second;
            const auto width = get_register(process, width_register);
            normalized.minimum_width
                = checked_vhdl_textio_field_width(width);
            normalized.operation = dynamic_string_field
                ? StringMethodOperator::format_string
                : StringMethodOperator::format_packed;
        }
        const auto packed = packed_format
                || operation.operation
                    == StringMethodOperator::format_vhdl_time
                || (operation.operation == StringMethodOperator::format_time
                    && operation.time_unit_femtoseconds != 0U)
            ? get_register(process, operation.first)
            : PackedLogic4 { 1, Logic4::zero };
        execute_string_format(
            normalized, source, packed,
            string_format
                ? std::string_view { get_string_register(
                      process, operation.argument) }
                : std::string_view { },
            scheduler.now(), time_format);
        ++process.pc;
        return;
    }
    const auto signed32 = [&](const RegisterId register_id)
        -> std::optional<std::int32_t> {
        const auto word = get_register(process, register_id).low_word();
        return word.bval == 0
            ? std::optional<std::int32_t> { static_cast<std::int32_t>(
                  static_cast<std::uint32_t>(word.aval)) }
            : std::nullopt;
    };
    const bool compare = operation.operation == StringMethodOperator::compare
        || operation.operation == StringMethodOperator::icompare
        || operation.operation
            == StringMethodOperator::vhdl_enumeration_value;
    const bool has_first = operation.operation == StringMethodOperator::getc
        || operation.operation == StringMethodOperator::putc
        || operation.operation == StringMethodOperator::substr
        || (operation.operation >= StringMethodOperator::itoa
            && operation.operation <= StringMethodOperator::realtoa);
    const bool has_second = operation.operation == StringMethodOperator::putc
        || operation.operation == StringMethodOperator::substr;
    const auto first_word = has_first
        ? std::optional { get_register(process, operation.first).low_word() }
        : std::nullopt;
    const auto result = execute_string_method(
        operation.operation, source,
        compare
            ? std::string_view { get_string_register(process, operation.argument) }
            : std::string_view { },
        has_first ? signed32(operation.first) : std::nullopt,
        has_second ? signed32(operation.second) : std::nullopt,
        operation.operation == StringMethodOperator::realtoa
                && first_word && first_word->bval == 0
            ? std::optional<std::uint64_t> { first_word->aval }
            : std::nullopt);
    if (result.integer) {
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(
            32,
            *result.integer, 0);
    }
    if (result.scalar_bits) {
        get_register(process, operation.destination) = PackedLogic4::from_aval_bval(64, *result.scalar_bits, 0);
    }
    if (result.string) {
        get_string_register(process, operation.string_destination) = *result.string;
    }
    ++process.pc;
}

} // namespace fsim::runtime::simir
