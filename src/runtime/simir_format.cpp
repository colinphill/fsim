// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include "fsim/frontend/output_format.hpp"
#include "fsim/runtime/file_scanning.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <bit>
#include <cmath>

namespace fsim::runtime::simir {

[[nodiscard]] std::string error_text(ProcessId process,
    InstructionIndex instruction,
    const std::string& message)
{
    std::ostringstream result;
    result << "SimIR process " << process << ", instruction " << instruction
           << ": " << message;
    return result.str();
}

[[nodiscard]] std::optional<SignalId> output_signal(
    const Operation& operation)
{
    return fsim::runtime::simir::visit_operation(
        [&](const auto& value) -> std::optional<SignalId> {
            using OperationType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteDelayed>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<
                                     OperationType,
                                     WriteInertialDynamicPartSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
                return std::optional { value.signal };
            } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
                return std::optional { value.signal };
            } else {
                return { };
            }
        },
        operation);
}

[[nodiscard]] std::string format_output_value(
    const PackedLogic4& value,
    const OutputFormat format,
    const bool signed_decimal,
    const bool suppress_leading_zero)
{
    const auto append_native_word = [](
                                        std::string& text,
                                        const std::uint32_t word) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            const auto shift = std::endian::native == std::endian::little
                ? byte * 8U
                : (3U - byte) * 8U;
            text.push_back(static_cast<char>((word >> shift) & 0xffU));
        }
    };
    const auto maybe_suppress_leading_zero =
        [suppress_leading_zero](std::string text) {
            if (!suppress_leading_zero || text.size() <= 1U) {
                return text;
            }
            const auto first = text.find_first_not_of('0');
            if (first == std::string::npos) {
                return std::string { "0" };
            }
            text.erase(0, first);
            return text;
        };
    switch (format) {
    case OutputFormat::binary: {
        auto text = value.to_msb_string();
        std::transform(
            text.begin(),
            text.end(),
            text.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        return maybe_suppress_leading_zero(std::move(text));
    }
    case OutputFormat::hexadecimal: {
        constexpr std::string_view digits { "0123456789abcdef" };
        const auto digit_count = (value.width() + 3U) / 4U;
        std::string text(digit_count, '0');
        for (std::size_t digit = 0; digit < digit_count; ++digit) {
            const auto offset = digit * 4U;
            const auto bit_count = std::min<std::size_t>(4U, value.width() - offset);
            unsigned known_value { };
            bool all_x = true;
            bool all_z = true;
            bool has_unknown = false;
            bool has_high_impedance = false;
            for (std::size_t bit = 0; bit < bit_count; ++bit) {
                const auto state = value.get(offset + bit);
                all_x = all_x && state == Logic4::x;
                all_z = all_z && state == Logic4::z;
                has_unknown = has_unknown || state == Logic4::x;
                has_high_impedance = has_high_impedance || state == Logic4::z;
                if (state == Logic4::one) {
                    known_value |= 1U << bit;
                }
            }
            // A digit with some x bits is X, else with some z bits Z
            // (IEEE 1800-2017 21.2.1.3).
            const char character = all_x ? 'x'
                : all_z                  ? 'z'
                : has_unknown            ? 'X'
                : has_high_impedance     ? 'Z'
                                         : digits[known_value];
            text[digit_count - digit - 1U] = character;
        }
        return maybe_suppress_leading_zero(std::move(text));
    }
    case OutputFormat::octal: {
        constexpr std::string_view digits { "01234567" };
        const auto digit_count = (value.width() + 2U) / 3U;
        std::string text(digit_count, '0');
        for (std::size_t digit = 0; digit < digit_count; ++digit) {
            const auto offset = digit * 3U;
            const auto bit_count = std::min<std::size_t>(3U, value.width() - offset);
            unsigned known_value { };
            bool all_x = true;
            bool all_z = true;
            bool has_unknown = false;
            bool has_high_impedance = false;
            for (std::size_t bit = 0; bit < bit_count; ++bit) {
                const auto state = value.get(offset + bit);
                all_x = all_x && state == Logic4::x;
                all_z = all_z && state == Logic4::z;
                has_unknown = has_unknown || state == Logic4::x;
                has_high_impedance = has_high_impedance || state == Logic4::z;
                if (state == Logic4::one) {
                    known_value |= 1U << bit;
                }
            }
            // A digit with some x bits is X, else with some z bits Z
            // (IEEE 1800-2017 21.2.1.3).
            const char character = all_x ? 'x'
                : all_z                  ? 'z'
                : has_unknown            ? 'X'
                : has_high_impedance     ? 'Z'
                                         : digits[known_value];
            text[digit_count - digit - 1U] = character;
        }
        return maybe_suppress_leading_zero(std::move(text));
    }
    case OutputFormat::decimal: {
        // Unknown bits print as x when all bits are x, z when all are z,
        // and otherwise X if any bit is x, else Z (IEEE 1800-2017 21.2.1.4).
        std::size_t unknown { };
        std::size_t high_impedance { };
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            const auto state = value.get(bit);
            if (state == Logic4::x) {
                ++unknown;
            } else if (state == Logic4::z) {
                ++high_impedance;
            }
        }
        if (unknown != 0U || high_impedance != 0U) {
            if (unknown == value.width()) {
                return "x";
            }
            if (high_impedance == value.width()) {
                return "z";
            }
            return unknown != 0U ? "X" : "Z";
        }
        auto magnitude = value;
        bool negative = signed_decimal
            && value.get(value.width() - 1U) == Logic4::one;
        if (negative) {
            bool carry = true;
            for (std::size_t bit = 0; bit < magnitude.width(); ++bit) {
                const bool inverted = value.get(bit) == Logic4::zero;
                const bool result = inverted != carry;
                carry = inverted && carry;
                magnitude.set(
                    bit, result ? Logic4::one : Logic4::zero);
            }
        }
        std::string text { "0" };
        for (std::size_t bit = magnitude.width(); bit-- > 0;) {
            unsigned carry = magnitude.get(bit) == Logic4::one ? 1U : 0U;
            for (std::size_t digit = text.size(); digit-- > 0;) {
                const auto value_digit = static_cast<unsigned>(text[digit] - '0') * 2U
                    + carry;
                text[digit] = static_cast<char>('0' + (value_digit % 10U));
                carry = value_digit / 10U;
            }
            if (carry != 0) {
                text.insert(text.begin(), static_cast<char>('0' + carry));
            }
        }
        if (negative && text != "0") {
            text.insert(text.begin(), '-');
        }
        return text;
    }
    case OutputFormat::character: {
        unsigned character { };
        const auto bit_count = std::min<std::size_t>(8U, value.width());
        for (std::size_t bit = 0; bit < bit_count; ++bit) {
            const auto state = value.get(bit);
            if (state == Logic4::x || state == Logic4::z) {
                return "x";
            }
            if (state == Logic4::one) {
                character |= 1U << bit;
            }
        }
        return std::string(1, static_cast<char>(character));
    }
    case OutputFormat::string: {
        const auto byte_count = (value.width() + 7U) / 8U;
        // Each 8 bits is one character, and a zero byte (such as the
        // padding of a wider variable holding a string) is shown as a
        // space, as other simulators do (IEEE 1800-2017 21.2.1.7); `%0s`
        // drops the leading padding.
        std::string text;
        text.reserve(byte_count);
        bool leading_padding = suppress_leading_zero;
        for (std::size_t byte = byte_count; byte-- > 0;) {
            const auto offset = byte * 8U;
            const auto bit_count = std::min<std::size_t>(8U, value.width() - offset);
            unsigned character { };
            bool unknown = false;
            for (std::size_t bit = 0; bit < bit_count; ++bit) {
                const auto state = value.get(offset + bit);
                unknown = unknown || state == Logic4::x || state == Logic4::z;
                if (state == Logic4::one) {
                    character |= 1U << bit;
                }
            }
            if (unknown) {
                text.push_back('x');
                leading_padding = false;
            } else if (character != 0U || !leading_padding) {
                text.push_back(character == 0U ? ' ' : static_cast<char>(character));
                leading_padding = false;
            }
        }
        return text;
    }
    case OutputFormat::unformatted2: {
        const auto word_count = (value.width() + 31U) / 32U;
        std::string text;
        text.reserve(word_count * 4U);
        for (std::size_t word = 0; word < word_count; ++word) {
            std::uint32_t payload { };
            for (std::size_t bit = 0;
                bit < 32U && word * 32U + bit < value.width();
                ++bit) {
                if (value.get(word * 32U + bit) == Logic4::one)
                    payload |= UINT32_C(1) << bit;
            }
            append_native_word(text, payload);
        }
        return text;
    }
    case OutputFormat::unformatted4: {
        const auto word_count = (value.width() + 31U) / 32U;
        std::string text;
        text.reserve(word_count * 8U);
        for (std::size_t word = 0; word < word_count; ++word) {
            std::uint32_t aval { };
            std::uint32_t bval { };
            for (std::size_t bit = 0;
                bit < 32U && word * 32U + bit < value.width();
                ++bit) {
                const auto state = value.get(word * 32U + bit);
                if (state == Logic4::one || state == Logic4::x)
                    aval |= UINT32_C(1) << bit;
                if (state == Logic4::x || state == Logic4::z)
                    bval |= UINT32_C(1) << bit;
            }
            append_native_word(text, aval);
            append_native_word(text, bval);
        }
        return text;
    }
    case OutputFormat::real_scientific:
    case OutputFormat::real_fixed:
    case OutputFormat::real_general:
    case OutputFormat::time:
        throw std::logic_error {
            "scalar formatted-output conversion has no scalar metadata"
        };
    }
    throw std::logic_error { "invalid formatted-output conversion" };
}

[[nodiscard]] std::string make_formatted_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const OutputFormat format,
    const PackedLogic4& value,
    const bool signed_decimal,
    const bool suppress_leading_zero,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad,
    const SystemVerilogScalarKind scalar_kind,
    const std::uint32_t precision)
{
    const bool scalar_text = scalar_kind != SystemVerilogScalarKind::None
        && scalar_kind != SystemVerilogScalarKind::Chandle
        && (format == OutputFormat::real_scientific
            || format == OutputFormat::real_fixed
            || format == OutputFormat::real_general
            || format == OutputFormat::time
            || format == OutputFormat::decimal);
    if (scalar_text) {
        const auto decoded = decode_systemverilog_scalar_payload(value, scalar_kind);
        if (!decoded)
            throw std::runtime_error { "invalid scalar formatted payload" };
        SystemVerilogScalarFormatOptions options;
        options.format = format == OutputFormat::real_scientific
            ? SystemVerilogScalarTextFormat::Scientific
            : format == OutputFormat::real_fixed
            ? SystemVerilogScalarTextFormat::Fixed
            : format == OutputFormat::time
            ? SystemVerilogScalarTextFormat::Time
            : scalar_kind == SystemVerilogScalarKind::Time
            ? SystemVerilogScalarTextFormat::Decimal
            : SystemVerilogScalarTextFormat::General;
        options.minimum_width = minimum_width;
        options.left_justify = left_justify;
        options.padding = zero_pad ? '0' : ' ';
        // %e, %f and %g follow C's printf: six digits unless the format
        // gives a precision; a plain real display is %g (21.2.1.3).
        if (format != OutputFormat::time) {
            options.precision
                = precision == std::numeric_limits<std::uint32_t>::max()
                ? 6U
                : precision;
        }
        const auto formatted = format_systemverilog_scalar(decoded.value, options);
        if (!formatted)
            throw std::runtime_error { "scalar formatting failed" };
        return std::string { prefix } + formatted.text + std::string { suffix };
    }
    auto formatted = format_output_value(
        value, format, signed_decimal, suppress_leading_zero);
    if (formatted.size() < minimum_width) {
        const auto padding = static_cast<std::size_t>(minimum_width) - formatted.size();
        if (left_justify) {
            formatted.append(padding, ' ');
        } else if (zero_pad && !formatted.empty()
            && formatted.front() == '-') {
            formatted.insert(1U, padding, '0');
        } else {
            formatted.insert(
                0U, padding, zero_pad ? '0' : ' ');
        }
    }
    std::string result;
    result.reserve(prefix.size() + formatted.size() + suffix.size());
    result.append(prefix);
    result.append(formatted);
    result.append(suffix);
    return result;
}

[[nodiscard]] std::optional<SimulationTick> integral_time_ticks(
    const PackedLogic4& value,
    const bool signed_value,
    const std::uint64_t unit_femtoseconds,
    const SystemVerilogTimeFormat& time_format,
    const SystemVerilogScalarKind scalar_kind)
{
    const auto resolution = time_format.resolution_femtoseconds;
    if (unit_femtoseconds == 0U || resolution == 0U || value.width() == 0U
        || value.width() > 64U) {
        return std::nullopt;
    }
    // The maximum unit names the project resolution itself.
    const auto unit = unit_femtoseconds
            == std::numeric_limits<std::uint64_t>::max()
        ? resolution
        : unit_femtoseconds;
    // A real time rounds to the nearest tick.
    if (scalar_kind == SystemVerilogScalarKind::Real
        || scalar_kind == SystemVerilogScalarKind::Realtime
        || scalar_kind == SystemVerilogScalarKind::ShortReal) {
        const auto decoded = decode_systemverilog_scalar_payload(
            value, scalar_kind);
        const auto number = decoded ? decoded.value.as_real() : std::nullopt;
        if (!number || !std::isfinite(*number) || *number < 0.0) {
            return std::nullopt;
        }
        const auto ticks = std::round(static_cast<long double>(*number)
            * static_cast<long double>(unit)
            / static_cast<long double>(resolution));
        if (ticks >= 18446744073709551616.0L) {
            return std::nullopt;
        }
        return static_cast<SimulationTick>(ticks);
    }
    const auto word = value.low_word();
    if (word.bval != 0U) {
        return std::nullopt;
    }
    if (signed_value && ((word.aval >> (value.width() - 1U)) & 1U) != 0U) {
        return std::nullopt;
    }
    const auto product = static_cast<unsigned __int128>(word.aval) * unit;
    const auto ticks = (product + resolution / 2U) / resolution;
    if (ticks > std::numeric_limits<SimulationTick>::max()) {
        return std::nullopt;
    }
    return static_cast<SimulationTick>(ticks);
}

[[nodiscard]] std::string make_time_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const SimulationTick tick,
    const SystemVerilogTimeFormat& time_format,
    const bool use_timeformat_width,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad)
{
    if (time_format.units < -15 || time_format.units > 0
        || time_format.precision > maximum_string_bytes
        || time_format.resolution_femtoseconds == 0) {
        throw std::invalid_argument { "invalid SystemVerilog time format" };
    }
    using boost::multiprecision::cpp_int;
    const auto power_of_ten = [](const std::uint32_t exponent) {
        auto result = cpp_int { 1 };
        for (std::uint32_t index = 0; index < exponent; ++index) {
            result *= 10;
        }
        return result;
    };
    const auto unit_femtoseconds = power_of_ten(
        static_cast<std::uint32_t>(time_format.units + 15));
    const auto precision_scale = power_of_ten(time_format.precision);
    const cpp_int scaled = cpp_int { tick }
        * time_format.resolution_femtoseconds * precision_scale;
    cpp_int rounded = scaled / unit_femtoseconds;
    const cpp_int remainder = scaled % unit_femtoseconds;
    if (remainder * 2 >= unit_femtoseconds) {
        ++rounded;
    }
    auto formatted = rounded.convert_to<std::string>();
    if (time_format.precision != 0) {
        const auto precision = static_cast<std::size_t>(time_format.precision);
        if (formatted.size() <= precision) {
            formatted.insert(0, precision + 1U - formatted.size(), '0');
        }
        formatted.insert(formatted.size() - precision, 1U, '.');
    }
    formatted += time_format.suffix;
    const auto selected_width = use_timeformat_width
        ? time_format.minimum_width
        : minimum_width;
    if (formatted.size() < selected_width) {
        const auto padding = static_cast<std::size_t>(selected_width) - formatted.size();
        if (left_justify) {
            formatted.append(padding, ' ');
        } else {
            formatted.insert(0U, padding, zero_pad ? '0' : ' ');
        }
    }
    std::string result;
    result.reserve(prefix.size() + formatted.size() + suffix.size());
    result.append(prefix);
    result.append(formatted);
    result.append(suffix);
    return result;
}

[[nodiscard]] bool edge_matches(EdgeKind edge, Logic4 old_value,
    Logic4 new_value) noexcept
{
    if (old_value == new_value) {
        return false;
    }
    if (edge == EdgeKind::any) {
        return true;
    }
    if (edge == EdgeKind::posedge) {
        return (old_value == Logic4::zero && (new_value == Logic4::one || new_value == Logic4::x || new_value == Logic4::z)) || ((old_value == Logic4::x || old_value == Logic4::z) && new_value == Logic4::one);
    }
    return (old_value == Logic4::one && (new_value == Logic4::zero || new_value == Logic4::x || new_value == Logic4::z)) || ((old_value == Logic4::x || old_value == Logic4::z) && new_value == Logic4::zero);
}

// Edge controls on packed expressions are detected from the LSB only.
Logic4 edge_value(const PackedLogic4& value) noexcept
{
    if (value.width() == 0U) {
        return Logic4::x;
    }
    return value.is_logic9()
        ? to_logic4(value.get_logic9(0U))
        : value.get(0U);
}

namespace {

[[nodiscard]] std::string pattern_packed_element(
    const PackedLogic4& value, const bool signed_value)
{
    if (value.is_logic9()) {
        return format_output_value(
            value, OutputFormat::binary, false, true);
    }
    bool known = true;
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
        const auto state = value.get(bit);
        if (state != Logic4::zero && state != Logic4::one) {
            known = false;
            break;
        }
    }
    if (known) {
        return format_output_value(
            value, OutputFormat::decimal, signed_value, true);
    }
    return std::to_string(value.width()) + "'b"
        + format_output_value(value, OutputFormat::binary, false, false);
}

[[nodiscard]] std::string pattern_quoted(const std::string& text)
{
    std::string result = "\"";
    for (const char character : text) {
        if (character == '"' || character == '\\') {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    result.push_back('"');
    return result;
}

void append_assignment_pattern(std::string& text, const ContainerValue& value);

void append_pattern_element(
    std::string& text, const ContainerValue& value, const std::size_t index)
{
    switch (value.type.element_kind) {
    case ContainerElementKind::Packed:
        text += pattern_packed_element(
            value.elements.at(index), value.type.signed_elements);
        return;
    case ContainerElementKind::Scalar:
        text += make_formatted_output({ }, { }, OutputFormat::real_general,
            value.elements.at(index), false, false, 0U, false, false,
            value.type.scalar_kind);
        return;
    case ContainerElementKind::String:
        text += pattern_quoted(value.string_elements.at(index));
        return;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        append_assignment_pattern(text, value.nested_elements.at(index));
        return;
    }
}

[[nodiscard]] std::size_t pattern_element_count(const ContainerValue& value)
{
    switch (value.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        return value.elements.size();
    case ContainerElementKind::String:
        return value.string_elements.size();
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        return value.nested_elements.size();
    }
    return 0U;
}

void append_assignment_pattern(std::string& text, const ContainerValue& value)
{
    if (value.type.aggregate_value) {
        // A structure prints its members by name; a union prints only its
        // first member.
        text += "'{";
        const auto count = value.type.union_aggregate
            ? std::min<std::size_t>(value.nested_elements.size(), 1U)
            : value.nested_elements.size();
        for (std::size_t member = 0; member < count; ++member) {
            if (member != 0U) {
                text.push_back(',');
            }
            if (member < value.type.member_names.size()
                && !value.type.member_names[member].empty()) {
                text += value.type.member_names[member];
                text.push_back(':');
            }
            const auto& field = value.nested_elements[member];
            // A singular member is a fixed one-element container.
            if (!field.type.aggregate_value && field.type.fixed
                && field.type.dimensions.size() <= 1U
                && pattern_element_count(field) == 1U
                && field.type.index_left == field.type.index_right
                && field.type.element_kind != ContainerElementKind::Container) {
                append_pattern_element(text, field, 0U);
            } else {
                append_assignment_pattern(text, field);
            }
        }
        text.push_back('}');
        return;
    }
    // A multidimensional fixed array nests one pattern per dimension.
    if (value.type.fixed && value.type.dimensions.size() > 1U) {
        std::vector<std::size_t> extents;
        for (const auto& [left, right] : value.type.dimensions) {
            extents.push_back(static_cast<std::size_t>(left >= right
                ? static_cast<std::int64_t>(left) - right
                : static_cast<std::int64_t>(right) - left) + 1U);
        }
        std::size_t next = 0;
        const auto nested = [&](const auto& self, const std::size_t depth)
            -> void {
            text += "'{";
            for (std::size_t index = 0; index < extents[depth]; ++index) {
                if (index != 0U) {
                    text.push_back(',');
                }
                if (depth + 1U < extents.size()) {
                    self(self, depth + 1U);
                } else if (next < pattern_element_count(value)) {
                    append_pattern_element(text, value, next++);
                }
            }
            text.push_back('}');
        };
        nested(nested, 0U);
        return;
    }
    text += "'{";
    const auto count = pattern_element_count(value);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0U) {
            text.push_back(',');
        }
        if (value.type.associative) {
            if (value.type.string_indices) {
                text += pattern_quoted(value.string_keys.at(index));
            } else {
                text += pattern_packed_element(
                    value.keys.at(index), value.type.signed_indices);
            }
            text.push_back(':');
        }
        append_pattern_element(text, value, index);
    }
    text.push_back('}');
}

} // namespace

std::string format_container_assignment_pattern(const ContainerValue& value)
{
    std::string text;
    append_assignment_pattern(text, value);
    return text;
}

OutputFormat runtime_output_format(const frontend::OutputFormat format)
{
    using Source = frontend::OutputFormat;
    switch (format) {
    case Source::Binary: return OutputFormat::binary;
    case Source::Hexadecimal: return OutputFormat::hexadecimal;
    case Source::Octal: return OutputFormat::octal;
    case Source::Decimal: return OutputFormat::decimal;
    case Source::Character: return OutputFormat::character;
    case Source::String: return OutputFormat::string;
    case Source::RealScientific: return OutputFormat::real_scientific;
    case Source::RealFixed: return OutputFormat::real_fixed;
    case Source::RealGeneral: return OutputFormat::real_general;
    case Source::Unformatted2: return OutputFormat::unformatted2;
    case Source::Unformatted4: return OutputFormat::unformatted4;
    case Source::Pattern:
    case Source::Strength: return OutputFormat::decimal;
    case Source::Hierarchy:
    case Source::Library:
    case Source::Time: break;
    }
    throw std::logic_error { "invalid runtime string format" };
}

std::uint32_t automatic_decimal_width(
    const std::size_t width, const bool signed_value) noexcept
{
    // The digits of 2^w - 1 (unsigned) or of 2^(w-1) plus a sign; neither
    // power of two is a power of ten, so the digit count is exact.
    const auto magnitude_bits = signed_value ? width - 1U : width;
    const auto digits = static_cast<std::uint32_t>(
        std::floor(static_cast<long double>(magnitude_bits)
            * std::log10(2.0L))) + 1U;
    return digits + (signed_value ? 1U : 0U);
}

std::string format_runtime_string(const RuntimeStringFormat& operation,
    const std::string_view format,
    const std::function<PackedLogic4(const RuntimeFormatArgument&)>& packed,
    const std::function<std::string(const RuntimeFormatArgument&)>& text,
    const SimulationTick tick, const SystemVerilogTimeFormat& time_format)
{
    using Format = frontend::OutputFormat;
    const auto parsed = frontend::parse_output_format(format);
    if (!parsed.valid) {
        throw std::invalid_argument { "run-time format string is malformed" };
    }
    const auto& arguments = operation.arguments;
    std::string result;
    const auto append = [&](const std::string_view part) {
        if (part.size() > maximum_string_bytes - result.size()) {
            throw std::length_error {
                "formatted string exceeds 4096-byte limit"
            };
        }
        result += part;
    };
    const auto method_for = [](const frontend::ParsedOutputConversion& conversion) {
        StringMethod method;
        method.minimum_width = conversion.minimum_width;
        method.precision = conversion.precision;
        method.left_justify = conversion.left_justify;
        method.zero_pad = conversion.zero_pad;
        method.suppress_leading_zero = conversion.suppress_leading_zero;
        return method;
    };
    const auto consumes_value = [](const frontend::ParsedOutputConversion& conversion) {
        return conversion.format != Format::Hierarchy
            && conversion.format != Format::Library
            && conversion.format != Format::Time;
    };
    // The value conversions as the literal-format lowering performs them
    // (IEEE 1800-2017 21.2.1): %p shows a string quoted and other singular
    // values as %0d or %g, %e/%f/%g convert an integral value to real, and
    // %d rounds a real.
    const auto append_value = [&](frontend::ParsedOutputConversion conversion,
                                  const RuntimeFormatArgument& argument) {
        append(conversion.prefix);
        if (conversion.format == Format::Pattern) {
            if (argument.string) {
                append("\"");
                append(text(argument));
                append("\"");
                return;
            }
            conversion.suppress_leading_zero = conversion.minimum_width == 0U
                || conversion.suppress_leading_zero;
            conversion.format = frontend::is_real_scalar_kind(argument.scalar_kind)
                ? Format::RealGeneral
                : Format::Decimal;
        }
        if (conversion.format == Format::Strength) {
            throw std::invalid_argument {
                "a run-time format cannot show strengths (%v)"
            };
        }
        auto method = method_for(conversion);
        PackedLogic4 value { 1, Logic4::zero };
        auto scalar = SystemVerilogScalarKind::None;
        bool signed_value { };
        if (argument.string) {
            const auto characters = text(argument);
            if (conversion.format == Format::String) {
                method.operation = StringMethodOperator::format_string;
                execute_string_format(method, result,
                    PackedLogic4 { 1, Logic4::zero }, characters, tick,
                    time_format);
                return;
            }
            value = pack_file_text(characters, static_cast<std::uint32_t>(
                std::max<std::size_t>(characters.size(), 1U) * 8U));
        } else {
            value = packed(argument);
            scalar = argument.scalar_kind;
            signed_value = argument.is_signed;
        }
        const bool real_conversion = conversion.format == Format::RealScientific
            || conversion.format == Format::RealFixed
            || conversion.format == Format::RealGeneral;
        if (real_conversion && scalar == SystemVerilogScalarKind::None) {
            PackedLogic4 known(value.width(), Logic4::zero);
            for (std::size_t bit = 0; bit < value.width(); ++bit) {
                if (to_logic4(value.get_logic9(bit)) == Logic4::one) {
                    known.set(bit, Logic4::one);
                }
            }
            auto real = systemverilog_math_payload(
                SystemVerilogMathFunction::Itor, known,
                SystemVerilogScalarKind::None, signed_value, nullptr,
                SystemVerilogScalarKind::None, false);
            if (!real) {
                throw std::invalid_argument {
                    "a run-time %e/%f/%g value cannot convert to real"
                };
            }
            value = std::move(real.value);
            scalar = SystemVerilogScalarKind::Real;
        }
        const bool real_decimal = conversion.format == Format::Decimal
            && frontend::is_real_scalar_kind(scalar);
        if (real_decimal) {
            auto integral = systemverilog_scalar_binary_payload(
                SystemVerilogScalarBinaryOperator::Convert, value, scalar,
                value, scalar, SystemVerilogScalarKind::None);
            if (!integral) {
                throw std::invalid_argument {
                    "a run-time %d real value cannot convert to an integer"
                };
            }
            value = std::move(integral.value);
            scalar = SystemVerilogScalarKind::None;
        }
        method.operation = StringMethodOperator::format_packed;
        method.format = runtime_output_format(conversion.format);
        method.scalar_kind = scalar;
        method.signed_decimal = method.format == OutputFormat::decimal
            && (real_decimal || signed_value);
        if (!real_decimal && method.format == OutputFormat::decimal
            && !conversion.suppress_leading_zero && !conversion.left_justify
            && conversion.minimum_width == 0U
            && (scalar == SystemVerilogScalarKind::None
                || scalar == SystemVerilogScalarKind::Time)) {
            method.minimum_width
                = automatic_decimal_width(value.width(), signed_value);
        }
        execute_string_format(method, result, value, { }, tick, time_format);
    };

    std::size_t next { };
    // %t takes an argument when the arguments outnumber the other
    // conversions; otherwise it shows the current time (IEEE 1800-2017
    // 21.2.1.3).
    const auto other_values = static_cast<std::size_t>(
        std::ranges::count_if(parsed.conversions, consumes_value));
    auto time_values = arguments.size() > other_values
        ? arguments.size() - other_values
        : std::size_t { };
    for (const auto& conversion : parsed.conversions) {
        if (conversion.format == Format::Hierarchy) {
            append(conversion.prefix);
            append(operation.hierarchy);
            continue;
        }
        if (conversion.format == Format::Library) {
            append(conversion.prefix);
            append(operation.library_cell);
            continue;
        }
        if (conversion.format == Format::Time) {
            append(conversion.prefix);
            const RuntimeFormatArgument* argument = nullptr;
            if (time_values != 0U && next < arguments.size()) {
                argument = &arguments[next++];
                --time_values;
            }
            auto method = method_for(conversion);
            method.operation = StringMethodOperator::format_time;
            method.use_timeformat_width = conversion.minimum_width == 0U
                && !conversion.suppress_leading_zero;
            PackedLogic4 value { 1, Logic4::zero };
            // An integral value other than $time is in the module's time
            // unit.
            if (argument != nullptr && !argument->current_time
                && !argument->string && argument->width != 0U
                && argument->width <= 64U
                && (argument->scalar_kind == SystemVerilogScalarKind::None
                    || argument->scalar_kind == SystemVerilogScalarKind::Time)) {
                value = packed(*argument);
                method.time_unit_femtoseconds = operation.time_unit_femtoseconds;
                method.time_signed = argument->is_signed;
            }
            execute_string_format(method, result, value, { }, tick, time_format);
            continue;
        }
        if (next >= arguments.size()) {
            throw std::invalid_argument {
                "run-time format names more values than it is given"
            };
        }
        append_value(conversion, arguments[next++]);
    }
    append(parsed.trailing_text);
    // Values past the format show as $display shows them: a string's
    // characters and another value's decimal form.
    while (next < arguments.size()) {
        const auto& argument = arguments[next++];
        frontend::ParsedOutputConversion conversion;
        conversion.format = argument.string ? Format::String : Format::Decimal;
        append_value(conversion, argument);
    }
    return result;
}

} // namespace fsim::runtime::simir
