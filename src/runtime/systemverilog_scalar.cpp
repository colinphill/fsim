// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <bit>
#include <cfenv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace fsim::runtime {
namespace {

    static_assert(std::numeric_limits<float>::is_iec559);
    static_assert(std::numeric_limits<double>::is_iec559);
    static_assert(std::numeric_limits<float>::radix == 2);
    static_assert(std::numeric_limits<double>::radix == 2);
    static_assert(std::numeric_limits<float>::round_style == std::round_to_nearest);
    static_assert(std::numeric_limits<double>::round_style == std::round_to_nearest);

    bool real_kind(const SystemVerilogScalarKind kind) noexcept
    {
        return kind == SystemVerilogScalarKind::ShortReal
            || kind == SystemVerilogScalarKind::Real
            || kind == SystemVerilogScalarKind::Realtime;
    }

    SystemVerilogScalarKind promoted_kind(
        const SystemVerilogScalarKind left,
        const SystemVerilogScalarKind right) noexcept
    {
        if (left == SystemVerilogScalarKind::Real
            || right == SystemVerilogScalarKind::Real)
            return SystemVerilogScalarKind::Real;
        if (left == SystemVerilogScalarKind::Realtime
            || right == SystemVerilogScalarKind::Realtime) {
            return SystemVerilogScalarKind::Realtime;
        }
        if (left == SystemVerilogScalarKind::ShortReal
            || right == SystemVerilogScalarKind::ShortReal) {
            return SystemVerilogScalarKind::ShortReal;
        }
        return left == SystemVerilogScalarKind::Time
                && right == SystemVerilogScalarKind::Time
            ? SystemVerilogScalarKind::Time
            : SystemVerilogScalarKind::None;
    }

    template <typename Number>
    SystemVerilogScalarResult real_arithmetic(
        const SystemVerilogScalarArithmetic operation,
        const Number left,
        const Number right,
        const SystemVerilogScalarKind kind) noexcept
    {
        if (operation == SystemVerilogScalarArithmetic::Divide && right == Number { }) {
            return { { }, SystemVerilogScalarError::DivideByZero };
        }
        Number value { };
        switch (operation) {
        case SystemVerilogScalarArithmetic::Add:
            value = left + right;
            break;
        case SystemVerilogScalarArithmetic::Subtract:
            value = left - right;
            break;
        case SystemVerilogScalarArithmetic::Multiply:
            value = left * right;
            break;
        case SystemVerilogScalarArithmetic::Divide:
            value = left / right;
            break;
        }
        if (!std::isfinite(value))
            return { { }, SystemVerilogScalarError::Nonfinite };
        return {
            kind == SystemVerilogScalarKind::ShortReal
                ? SystemVerilogScalarValue::shortreal(static_cast<float>(value))
                : kind == SystemVerilogScalarKind::Realtime
                ? SystemVerilogScalarValue::realtime(static_cast<double>(value))
                : SystemVerilogScalarValue::real(static_cast<double>(value)),
            SystemVerilogScalarError::None
        };
    }

    SystemVerilogScalarResult time_arithmetic(
        const SystemVerilogScalarArithmetic operation,
        const std::uint64_t left,
        const std::uint64_t right) noexcept
    {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t value { };
        switch (operation) {
        case SystemVerilogScalarArithmetic::Add:
            if (left > maximum - right)
                return { { }, SystemVerilogScalarError::Overflow };
            value = left + right;
            break;
        case SystemVerilogScalarArithmetic::Subtract:
            if (left < right)
                return { { }, SystemVerilogScalarError::Overflow };
            value = left - right;
            break;
        case SystemVerilogScalarArithmetic::Multiply:
            if (right != 0 && left > maximum / right) {
                return { { }, SystemVerilogScalarError::Overflow };
            }
            value = left * right;
            break;
        case SystemVerilogScalarArithmetic::Divide:
            if (right == 0)
                return { { }, SystemVerilogScalarError::DivideByZero };
            value = left / right;
            break;
        }
        return { SystemVerilogScalarValue::time(value), SystemVerilogScalarError::None };
    }

} // namespace

SystemVerilogScalarValue SystemVerilogScalarValue::shortreal(
    const float value) noexcept
{
    return { SystemVerilogScalarKind::ShortReal, std::bit_cast<std::uint32_t>(value) };
}

SystemVerilogScalarValue SystemVerilogScalarValue::real(
    const double value) noexcept
{
    return { SystemVerilogScalarKind::Real, std::bit_cast<std::uint64_t>(value) };
}

SystemVerilogScalarValue SystemVerilogScalarValue::realtime(
    const double value) noexcept
{
    return { SystemVerilogScalarKind::Realtime, std::bit_cast<std::uint64_t>(value) };
}

SystemVerilogScalarValue SystemVerilogScalarValue::time(
    const std::uint64_t ticks) noexcept
{
    return { SystemVerilogScalarKind::Time, ticks };
}

SystemVerilogScalarValue SystemVerilogScalarValue::chandle(
    const SystemVerilogChandle handle) noexcept
{
    return { SystemVerilogScalarKind::Chandle, handle };
}

SystemVerilogScalarValue SystemVerilogScalarValue::integral(
    const std::int64_t value) noexcept
{
    return { SystemVerilogScalarKind::None, std::bit_cast<std::uint64_t>(value) };
}

std::optional<float> SystemVerilogScalarValue::as_shortreal() const noexcept
{
    return kind == SystemVerilogScalarKind::ShortReal
        ? std::optional<float> {
              std::bit_cast<float>(static_cast<std::uint32_t>(bits))
          }
        : std::nullopt;
}

std::optional<double> SystemVerilogScalarValue::as_real() const noexcept
{
    if (kind == SystemVerilogScalarKind::ShortReal) {
        return static_cast<double>(
            std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
    }
    if (kind == SystemVerilogScalarKind::Real
        || kind == SystemVerilogScalarKind::Realtime) {
        return std::bit_cast<double>(bits);
    }
    if (kind == SystemVerilogScalarKind::None) {
        return static_cast<double>(std::bit_cast<std::int64_t>(bits));
    }
    return kind == SystemVerilogScalarKind::Time
        ? std::optional<double> { static_cast<double>(bits) }
        : std::nullopt;
}

std::optional<std::uint64_t> SystemVerilogScalarValue::as_time() const noexcept
{
    return kind == SystemVerilogScalarKind::Time
        ? std::optional<std::uint64_t> { bits }
        : std::nullopt;
}

std::optional<SystemVerilogChandle>
SystemVerilogScalarValue::as_chandle() const noexcept
{
    return kind == SystemVerilogScalarKind::Chandle
        ? std::optional<SystemVerilogChandle> { bits }
        : std::nullopt;
}

std::optional<std::int64_t>
SystemVerilogScalarValue::as_integral() const noexcept
{
    return kind == SystemVerilogScalarKind::None
        ? std::optional<std::int64_t> { std::bit_cast<std::int64_t>(bits) }
        : std::nullopt;
}

std::string SystemVerilogScalarValue::canonical() const
{
    std::ostringstream output;
    output << "svruntime-scalar-v1:k=" << static_cast<unsigned>(kind)
           << ":b=" << std::hex << std::setw(16) << std::setfill('0') << bits;
    return output.str();
}

SystemVerilogScalarResult systemverilog_scalar_arithmetic(
    const SystemVerilogScalarArithmetic operation,
    const SystemVerilogScalarValue& left,
    const SystemVerilogScalarValue& right) noexcept
{
    const auto kind = promoted_kind(left.kind, right.kind);
    if (kind == SystemVerilogScalarKind::None) {
        return { { }, SystemVerilogScalarError::InvalidKind };
    }
    if (kind == SystemVerilogScalarKind::Time) {
        return time_arithmetic(operation, left.bits, right.bits);
    }
    if (std::fegetround() != FE_TONEAREST) {
        return { { }, SystemVerilogScalarError::UnsupportedRoundingMode };
    }
    const auto lhs = left.as_real();
    const auto rhs = right.as_real();
    if (!lhs || !rhs || !std::isfinite(*lhs) || !std::isfinite(*rhs)) {
        return { { }, SystemVerilogScalarError::Nonfinite };
    }
    if (kind == SystemVerilogScalarKind::ShortReal) {
        return real_arithmetic(
            operation, static_cast<float>(*lhs), static_cast<float>(*rhs), kind);
    }
    return real_arithmetic(operation, *lhs, *rhs, kind);
}

namespace {

    bool scalar_numeric_kind(const SystemVerilogScalarKind kind) noexcept
    {
        return kind == SystemVerilogScalarKind::None
            || kind == SystemVerilogScalarKind::Time || real_kind(kind);
    }

    template <typename Number>
    bool comparison_value(
        const SystemVerilogScalarComparison operation,
        const Number left,
        const Number right) noexcept
    {
        switch (operation) {
        case SystemVerilogScalarComparison::Equal:
            return left == right;
        case SystemVerilogScalarComparison::NotEqual:
            return left != right;
        case SystemVerilogScalarComparison::Less:
            return left < right;
        case SystemVerilogScalarComparison::LessEqual:
            return left <= right;
        case SystemVerilogScalarComparison::Greater:
            return left > right;
        case SystemVerilogScalarComparison::GreaterEqual:
            return left >= right;
        }
        return false;
    }

    double rounded_value(
        const double value,
        const SystemVerilogScalarRounding rounding) noexcept
    {
        switch (rounding) {
        case SystemVerilogScalarRounding::NearestAwayFromZero:
            return std::round(value);
        case SystemVerilogScalarRounding::TowardZero:
            return std::trunc(value);
        case SystemVerilogScalarRounding::Floor:
            return std::floor(value);
        case SystemVerilogScalarRounding::Ceil:
            return std::ceil(value);
        }
        return value;
    }

    SystemVerilogScalarResult real_target(
        const double value,
        const SystemVerilogScalarKind target) noexcept
    {
        if (!std::isfinite(value))
            return { { }, SystemVerilogScalarError::Nonfinite };
        if (target == SystemVerilogScalarKind::ShortReal) {
            const auto narrowed = static_cast<float>(value);
            return std::isfinite(narrowed)
                ? SystemVerilogScalarResult {
                      SystemVerilogScalarValue::shortreal(narrowed),
                      SystemVerilogScalarError::None
                  }
                : SystemVerilogScalarResult { { }, SystemVerilogScalarError::Overflow };
        }
        if (target == SystemVerilogScalarKind::Real) {
            return { SystemVerilogScalarValue::real(value),
                SystemVerilogScalarError::None };
        }
        return { SystemVerilogScalarValue::realtime(value),
            SystemVerilogScalarError::None };
    }

} // namespace

SystemVerilogScalarPredicate systemverilog_scalar_compare(
    const SystemVerilogScalarComparison operation,
    const SystemVerilogScalarValue& left,
    const SystemVerilogScalarValue& right) noexcept
{
    if (left.kind == SystemVerilogScalarKind::Chandle
        || right.kind == SystemVerilogScalarKind::Chandle) {
        if (left.kind != SystemVerilogScalarKind::Chandle
            || right.kind != SystemVerilogScalarKind::Chandle
            || (operation != SystemVerilogScalarComparison::Equal
                && operation != SystemVerilogScalarComparison::NotEqual)) {
            return { false, SystemVerilogScalarError::InvalidKind };
        }
        const bool equal = left.bits == right.bits;
        return {
            operation == SystemVerilogScalarComparison::Equal ? equal : !equal,
            SystemVerilogScalarError::None
        };
    }
    if (!scalar_numeric_kind(left.kind) || !scalar_numeric_kind(right.kind)) {
        return { false, SystemVerilogScalarError::InvalidKind };
    }
    const bool integral_left = left.kind == SystemVerilogScalarKind::None
        || left.kind == SystemVerilogScalarKind::Time;
    const bool integral_right = right.kind == SystemVerilogScalarKind::None
        || right.kind == SystemVerilogScalarKind::Time;
    if (integral_left && integral_right) {
        if (left.kind == right.kind) {
            return left.kind == SystemVerilogScalarKind::Time
                ? SystemVerilogScalarPredicate {
                      comparison_value(operation, left.bits, right.bits), { }
                  }
                : SystemVerilogScalarPredicate { comparison_value(operation, *left.as_integral(), *right.as_integral()), { } };
        }
        const auto signed_value = left.kind == SystemVerilogScalarKind::None
            ? *left.as_integral()
            : *right.as_integral();
        const auto unsigned_value = left.kind == SystemVerilogScalarKind::Time
            ? left.bits
            : right.bits;
        bool signed_less { };
        bool equal { };
        if (signed_value < 0) {
            signed_less = true;
        } else {
            const auto converted = static_cast<std::uint64_t>(signed_value);
            signed_less = converted < unsigned_value;
            equal = converted == unsigned_value;
        }
        const bool left_is_signed = left.kind == SystemVerilogScalarKind::None;
        const bool less = left_is_signed ? signed_less : !signed_less && !equal;
        switch (operation) {
        case SystemVerilogScalarComparison::Equal:
            return { equal, { } };
        case SystemVerilogScalarComparison::NotEqual:
            return { !equal, { } };
        case SystemVerilogScalarComparison::Less:
            return { less, { } };
        case SystemVerilogScalarComparison::LessEqual:
            return { less || equal, { } };
        case SystemVerilogScalarComparison::Greater:
            return { !less && !equal, { } };
        case SystemVerilogScalarComparison::GreaterEqual:
            return { !less, { } };
        }
    }
    if ((integral_left || integral_right) && std::fegetround() != FE_TONEAREST) {
        return { false, SystemVerilogScalarError::UnsupportedRoundingMode };
    }
    return { comparison_value(operation, *left.as_real(), *right.as_real()), { } };
}

SystemVerilogScalarPredicate systemverilog_scalar_truth(
    const SystemVerilogScalarValue& value) noexcept
{
    if (!scalar_numeric_kind(value.kind)) {
        return { false, SystemVerilogScalarError::InvalidKind };
    }
    if (value.kind == SystemVerilogScalarKind::Time)
        return { value.bits != 0, { } };
    if (const auto integral = value.as_integral())
        return { *integral != 0, { } };
    const auto number = value.as_real();
    return number && std::isfinite(*number)
        ? SystemVerilogScalarPredicate { *number != 0.0, { } }
        : SystemVerilogScalarPredicate { false, SystemVerilogScalarError::Nonfinite };
}

SystemVerilogScalarResult convert_systemverilog_scalar(
    const SystemVerilogScalarValue& value,
    const SystemVerilogScalarKind target,
    const SystemVerilogScalarRounding rounding) noexcept
{
    if (!scalar_numeric_kind(value.kind)
        || (target != SystemVerilogScalarKind::None
            && target != SystemVerilogScalarKind::Time
            && !real_kind(target))) {
        return { { }, SystemVerilogScalarError::InvalidKind };
    }
    if (target == value.kind)
        return { value, { } };
    if (real_kind(target)) {
        if ((value.kind == SystemVerilogScalarKind::None
                || value.kind == SystemVerilogScalarKind::Time)
            && std::fegetround() != FE_TONEAREST) {
            return { { }, SystemVerilogScalarError::UnsupportedRoundingMode };
        }
        return real_target(*value.as_real(), target);
    }
    if (target == SystemVerilogScalarKind::None) {
        if (const auto integral = value.as_integral())
            return { value, { } };
        if (const auto ticks = value.as_time()) {
            if (*ticks > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                return { { }, SystemVerilogScalarError::Overflow };
            }
            return { SystemVerilogScalarValue::integral(
                         static_cast<std::int64_t>(*ticks)),
                { } };
        }
    } else {
        if (const auto ticks = value.as_time())
            return { value, { } };
        if (const auto integral = value.as_integral()) {
            return *integral < 0
                ? SystemVerilogScalarResult { { }, SystemVerilogScalarError::Overflow }
                : SystemVerilogScalarResult {
                      SystemVerilogScalarValue::time(
                          static_cast<std::uint64_t>(*integral)),
                      { }
                  };
        }
    }
    const auto number = value.as_real();
    if (!number || !std::isfinite(*number)) {
        return { { }, SystemVerilogScalarError::Nonfinite };
    }
    const auto rounded = rounded_value(*number, rounding);
    const auto extended = static_cast<long double>(rounded);
    if (target == SystemVerilogScalarKind::None) {
        constexpr long double lower = -9223372036854775808.0L;
        constexpr long double upper = 9223372036854775808.0L;
        if (extended < lower || extended >= upper) {
            return { { }, SystemVerilogScalarError::Overflow };
        }
        return { SystemVerilogScalarValue::integral(
                     static_cast<std::int64_t>(rounded)),
            { } };
    }
    constexpr long double upper = 18446744073709551616.0L;
    if (extended < 0.0L || extended >= upper) {
        return { { }, SystemVerilogScalarError::Overflow };
    }
    return { SystemVerilogScalarValue::time(
                 static_cast<std::uint64_t>(rounded)),
        { } };
}

SystemVerilogScalarResult systemverilog_scalar_from_packed(
    const PackedLogic4& value,
    const bool is_signed,
    const SystemVerilogScalarKind target) noexcept
{
    if (value.width() == 0 || value.width() > 64) {
        return { { }, SystemVerilogScalarError::Overflow };
    }
    const auto word = value.low_word();
    if (word.bval != 0)
        return { { }, SystemVerilogScalarError::UnknownValue };
    const auto mask = word.width == 64
        ? std::numeric_limits<std::uint64_t>::max()
        : (std::uint64_t { 1 } << word.width) - 1U;
    const auto bits = word.aval & mask;
    if (!is_signed) {
        if (target == SystemVerilogScalarKind::Time) {
            return { SystemVerilogScalarValue::time(bits), { } };
        }
        if (target == SystemVerilogScalarKind::None) {
            if (bits > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                return { { }, SystemVerilogScalarError::Overflow };
            }
            return { SystemVerilogScalarValue::integral(
                         static_cast<std::int64_t>(bits)),
                { } };
        }
        if (std::fegetround() != FE_TONEAREST) {
            return { { }, SystemVerilogScalarError::UnsupportedRoundingMode };
        }
        return real_target(static_cast<double>(bits), target);
    }
    auto extended = bits;
    if (word.width < 64 && ((bits >> (word.width - 1U)) & 1U) != 0) {
        extended |= ~mask;
    }
    return convert_systemverilog_scalar(
        SystemVerilogScalarValue::integral(
            std::bit_cast<std::int64_t>(extended)),
        target);
}

SystemVerilogPackedScalarResult systemverilog_scalar_to_packed(
    const SystemVerilogScalarValue& value,
    const std::size_t width,
    const bool is_signed,
    const SystemVerilogScalarRounding rounding)
{
    if (width == 0 || width > 64) {
        return { PackedLogic4 { }, SystemVerilogScalarError::Overflow };
    }
    const auto converted = convert_systemverilog_scalar(
        value, is_signed ? SystemVerilogScalarKind::None : SystemVerilogScalarKind::Time,
        rounding);
    if (!converted)
        return { PackedLogic4 { }, converted.error };
    std::uint64_t bits { };
    if (is_signed) {
        const auto number = *converted.value.as_integral();
        if (width < 64) {
            const auto minimum = -(std::int64_t { 1 } << (width - 1U));
            const auto maximum = (std::int64_t { 1 } << (width - 1U)) - 1;
            if (number < minimum || number > maximum) {
                return { PackedLogic4 { }, SystemVerilogScalarError::Overflow };
            }
        }
        bits = std::bit_cast<std::uint64_t>(number);
    } else {
        bits = *converted.value.as_time();
        if (width < 64 && bits >= (std::uint64_t { 1 } << width)) {
            return { PackedLogic4 { }, SystemVerilogScalarError::Overflow };
        }
    }
    return { PackedLogic4::from_aval_bval(width, bits, 0), { } };
}

SystemVerilogPackedScalarResult encode_systemverilog_scalar_payload(
    const SystemVerilogScalarValue& value)
{
    const auto width = value.kind == SystemVerilogScalarKind::ShortReal
        ? 32U
        : value.kind == SystemVerilogScalarKind::Real
            || value.kind == SystemVerilogScalarKind::Realtime
            || value.kind == SystemVerilogScalarKind::Time
            || value.kind == SystemVerilogScalarKind::Chandle
        ? 64U
        : 0U;
    return width == 0
        ? SystemVerilogPackedScalarResult {
              PackedLogic4 { }, SystemVerilogScalarError::InvalidKind
          }
        : SystemVerilogPackedScalarResult { PackedLogic4::from_aval_bval(width, value.bits, 0), { } };
}

SystemVerilogScalarResult decode_systemverilog_scalar_payload(
    const PackedLogic4& payload,
    const SystemVerilogScalarKind kind) noexcept
{
    const auto expected_width = kind == SystemVerilogScalarKind::ShortReal
        ? 32U
        : kind == SystemVerilogScalarKind::Real
            || kind == SystemVerilogScalarKind::Realtime
            || kind == SystemVerilogScalarKind::Time
            || kind == SystemVerilogScalarKind::Chandle
        ? 64U
        : 0U;
    if (expected_width == 0) {
        return { { }, SystemVerilogScalarError::InvalidKind };
    }
    if (payload.width() != expected_width) {
        return { { }, SystemVerilogScalarError::Overflow };
    }
    const auto word = payload.low_word();
    if (word.bval != 0) {
        return { { }, SystemVerilogScalarError::UnknownValue };
    }
    return { { kind, word.aval }, { } };
}

SystemVerilogPackedScalarResult systemverilog_scalar_binary_payload(
    const SystemVerilogScalarBinaryOperator operation,
    const PackedLogic4& left,
    const SystemVerilogScalarKind left_kind,
    const PackedLogic4& right,
    const SystemVerilogScalarKind right_kind,
    const SystemVerilogScalarKind result_kind) noexcept
{
    const auto lhs = decode_systemverilog_scalar_payload(left, left_kind);
    if (!lhs)
        return { PackedLogic4 { }, lhs.error };
    const auto rhs = decode_systemverilog_scalar_payload(right, right_kind);
    if (!rhs)
        return { PackedLogic4 { }, rhs.error };
    if (operation >= SystemVerilogScalarBinaryOperator::Equal) {
        const auto comparison = static_cast<SystemVerilogScalarComparison>(
            static_cast<std::uint8_t>(operation)
            - static_cast<std::uint8_t>(
                SystemVerilogScalarBinaryOperator::Equal));
        const auto result = systemverilog_scalar_compare(
            comparison, lhs.value, rhs.value);
        return result
            ? SystemVerilogPackedScalarResult {
                  PackedLogic4::from_aval_bval(1, result.value ? 1U : 0U, 0),
                  SystemVerilogScalarError::None
              }
            : SystemVerilogPackedScalarResult { PackedLogic4 { }, result.error };
    }
    const auto arithmetic = static_cast<SystemVerilogScalarArithmetic>(
        static_cast<std::uint8_t>(operation));
    auto result = systemverilog_scalar_arithmetic(
        arithmetic, lhs.value, rhs.value);
    if (!result)
        return { PackedLogic4 { }, result.error };
    if (result_kind != SystemVerilogScalarKind::None
        && result.value.kind != result_kind) {
        result = convert_systemverilog_scalar(result.value, result_kind);
        if (!result)
            return { PackedLogic4 { }, result.error };
    }
    return encode_systemverilog_scalar_payload(result.value);
}

namespace {

    struct MathNumber {
        double value { };
        SystemVerilogScalarError error { SystemVerilogScalarError::None };
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return error == SystemVerilogScalarError::None;
        }
    };

    MathNumber packed_math_number(
        const PackedLogic4& value, const bool is_signed) noexcept
    {
        if (value.width() == 0) {
            return { { }, SystemVerilogScalarError::Overflow };
        }
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            const auto state = value.get(bit);
            if (state != Logic4::zero && state != Logic4::one) {
                return { { }, SystemVerilogScalarError::UnknownValue };
            }
        }
        if (std::fegetround() != FE_TONEAREST) {
            return { { }, SystemVerilogScalarError::UnsupportedRoundingMode };
        }
        const bool negative = is_signed
            && value.get(value.width() - 1U) == Logic4::one;
        double result { };
        bool carry = negative;
        for (std::size_t bit = 0; bit < value.width(); ++bit) {
            bool selected = value.get(bit) == Logic4::one;
            if (negative) {
                selected = !selected;
                const bool sum = selected != carry;
                carry = selected && carry;
                selected = sum;
            }
            if (!selected) {
                continue;
            }
            if (bit >= static_cast<std::size_t>(
                    std::numeric_limits<double>::max_exponent)) {
                result = std::numeric_limits<double>::infinity();
                break;
            }
            result += std::ldexp(1.0, static_cast<int>(bit));
        }
        return { negative ? -result : result, { } };
    }

    MathNumber math_number(
        const PackedLogic4& value,
        const SystemVerilogScalarKind kind,
        const bool is_signed) noexcept
    {
        if (kind == SystemVerilogScalarKind::None) {
            return packed_math_number(value, is_signed);
        }
        if (!real_kind(kind) && kind != SystemVerilogScalarKind::Time) {
            return { { }, SystemVerilogScalarError::InvalidKind };
        }
        const auto scalar = decode_systemverilog_scalar_payload(value, kind);
        if (!scalar) {
            return { { }, scalar.error };
        }
        const auto number = scalar.value.as_real();
        return number
            ? MathNumber { *number, { } }
            : MathNumber { { }, SystemVerilogScalarError::InvalidKind };
    }

    bool binary_math_function(const SystemVerilogMathFunction function) noexcept
    {
        return function == SystemVerilogMathFunction::Pow
            || function == SystemVerilogMathFunction::Atan2
            || function == SystemVerilogMathFunction::Hypot;
    }

    SystemVerilogPackedScalarResult raw_payload(
        const PackedLogic4& value, const std::size_t width) noexcept
    {
        if (value.width() != width) {
            return { PackedLogic4 { }, SystemVerilogScalarError::Overflow };
        }
        const auto word = value.low_word();
        return word.bval == 0
            ? SystemVerilogPackedScalarResult {
                  PackedLogic4::from_aval_bval(width, word.aval, 0), { }
              }
            : SystemVerilogPackedScalarResult { PackedLogic4 { }, SystemVerilogScalarError::UnknownValue };
    }

} // namespace

SystemVerilogPackedScalarResult systemverilog_math_payload(
    const SystemVerilogMathFunction function,
    const PackedLogic4& first,
    const SystemVerilogScalarKind first_kind,
    const bool first_signed,
    const PackedLogic4* second,
    const SystemVerilogScalarKind second_kind,
    const bool second_signed) noexcept
{
    if (binary_math_function(function) != (second != nullptr)) {
        return { PackedLogic4 { }, SystemVerilogScalarError::InvalidKind };
    }
    switch (function) {
    case SystemVerilogMathFunction::Rtoi: {
        if (!real_kind(first_kind)) {
            return { PackedLogic4 { }, SystemVerilogScalarError::InvalidKind };
        }
        const auto scalar = decode_systemverilog_scalar_payload(first, first_kind);
        if (!scalar) {
            return { PackedLogic4 { }, scalar.error };
        }
        const auto integral = convert_systemverilog_scalar(
            scalar.value, SystemVerilogScalarKind::None,
            SystemVerilogScalarRounding::TowardZero);
        return integral
            ? systemverilog_scalar_to_packed(integral.value, 32U, true)
            : SystemVerilogPackedScalarResult { PackedLogic4 { }, integral.error };
    }
    case SystemVerilogMathFunction::Itor: {
        if (first_kind != SystemVerilogScalarKind::None) {
            return { PackedLogic4 { }, SystemVerilogScalarError::InvalidKind };
        }
        const auto number = packed_math_number(first, first_signed);
        return number
            ? encode_systemverilog_scalar_payload(
                  SystemVerilogScalarValue::real(number.value))
            : SystemVerilogPackedScalarResult { PackedLogic4 { }, number.error };
    }
    case SystemVerilogMathFunction::BitsToReal: {
        const auto raw = raw_payload(first, 64U);
        return raw
            ? encode_systemverilog_scalar_payload(
                  { SystemVerilogScalarKind::Real, raw.value.low_word().aval })
            : raw;
    }
    case SystemVerilogMathFunction::RealToBits: {
        if (first_kind != SystemVerilogScalarKind::Real
            && first_kind != SystemVerilogScalarKind::Realtime) {
            return { PackedLogic4 { }, SystemVerilogScalarError::InvalidKind };
        }
        const auto scalar = decode_systemverilog_scalar_payload(first, first_kind);
        return scalar
            ? SystemVerilogPackedScalarResult {
                  PackedLogic4::from_aval_bval(64U, scalar.value.bits, 0), { }
              }
            : SystemVerilogPackedScalarResult { PackedLogic4 { }, scalar.error };
    }
    case SystemVerilogMathFunction::BitsToShortReal: {
        const auto raw = raw_payload(first, 32U);
        return raw
            ? encode_systemverilog_scalar_payload(
                  { SystemVerilogScalarKind::ShortReal,
                      raw.value.low_word().aval })
            : raw;
    }
    case SystemVerilogMathFunction::ShortRealToBits: {
        if (first_kind != SystemVerilogScalarKind::ShortReal) {
            return { PackedLogic4 { }, SystemVerilogScalarError::InvalidKind };
        }
        const auto scalar = decode_systemverilog_scalar_payload(first, first_kind);
        return scalar
            ? SystemVerilogPackedScalarResult {
                  PackedLogic4::from_aval_bval(32U, scalar.value.bits, 0), { }
              }
            : SystemVerilogPackedScalarResult { PackedLogic4 { }, scalar.error };
    }
    default:
        break;
    }

    const auto lhs = math_number(first, first_kind, first_signed);
    if (!lhs) {
        return { PackedLogic4 { }, lhs.error };
    }
    MathNumber rhs;
    if (second != nullptr) {
        rhs = math_number(*second, second_kind, second_signed);
        if (!rhs) {
            return { PackedLogic4 { }, rhs.error };
        }
    }
    double value { };
    switch (function) {
    case SystemVerilogMathFunction::Ln:
        value = std::log(lhs.value);
        break;
    case SystemVerilogMathFunction::Log10:
        value = std::log10(lhs.value);
        break;
    case SystemVerilogMathFunction::Exp:
        value = std::exp(lhs.value);
        break;
    case SystemVerilogMathFunction::Sqrt:
        value = std::sqrt(lhs.value);
        break;
    case SystemVerilogMathFunction::Pow:
        value = std::pow(lhs.value, rhs.value);
        break;
    case SystemVerilogMathFunction::Floor:
        value = std::floor(lhs.value);
        break;
    case SystemVerilogMathFunction::Ceil:
        value = std::ceil(lhs.value);
        break;
    case SystemVerilogMathFunction::Sin:
        value = std::sin(lhs.value);
        break;
    case SystemVerilogMathFunction::Cos:
        value = std::cos(lhs.value);
        break;
    case SystemVerilogMathFunction::Tan:
        value = std::tan(lhs.value);
        break;
    case SystemVerilogMathFunction::Asin:
        value = std::asin(lhs.value);
        break;
    case SystemVerilogMathFunction::Acos:
        value = std::acos(lhs.value);
        break;
    case SystemVerilogMathFunction::Atan:
        value = std::atan(lhs.value);
        break;
    case SystemVerilogMathFunction::Atan2:
        value = std::atan2(lhs.value, rhs.value);
        break;
    case SystemVerilogMathFunction::Hypot:
        value = std::hypot(lhs.value, rhs.value);
        break;
    case SystemVerilogMathFunction::Sinh:
        value = std::sinh(lhs.value);
        break;
    case SystemVerilogMathFunction::Cosh:
        value = std::cosh(lhs.value);
        break;
    case SystemVerilogMathFunction::Tanh:
        value = std::tanh(lhs.value);
        break;
    case SystemVerilogMathFunction::Asinh:
        value = std::asinh(lhs.value);
        break;
    case SystemVerilogMathFunction::Acosh:
        value = std::acosh(lhs.value);
        break;
    case SystemVerilogMathFunction::Atanh:
        value = std::atanh(lhs.value);
        break;
    case SystemVerilogMathFunction::Rtoi:
    case SystemVerilogMathFunction::Itor:
    case SystemVerilogMathFunction::BitsToReal:
    case SystemVerilogMathFunction::RealToBits:
    case SystemVerilogMathFunction::BitsToShortReal:
    case SystemVerilogMathFunction::ShortRealToBits:
    case SystemVerilogMathFunction::Time:
    case SystemVerilogMathFunction::Stime:
    case SystemVerilogMathFunction::Realtime:
        return { PackedLogic4 { }, SystemVerilogScalarError::InvalidKind };
    }
    return encode_systemverilog_scalar_payload(
        SystemVerilogScalarValue::real(value));
}

SystemVerilogScalarClassification classify_systemverilog_scalar(
    const SystemVerilogScalarValue& value) noexcept
{
    if (!scalar_numeric_kind(value.kind)) {
        return { .error = SystemVerilogScalarError::InvalidKind };
    }
    if (value.kind == SystemVerilogScalarKind::Time) {
        return { true, value.bits == 0, false, value.bits != 0, false, false, false, { } };
    }
    if (const auto integral = value.as_integral()) {
        return { true, *integral == 0, *integral < 0, *integral != 0,
            false, false, false, { } };
    }
    if (const auto number = value.as_shortreal()) {
        const auto category = std::fpclassify(*number);
        return {
            std::isfinite(*number), category == FP_ZERO, std::signbit(*number),
            category == FP_NORMAL, category == FP_SUBNORMAL,
            category == FP_INFINITE, category == FP_NAN, { }
        };
    }
    const auto number = *value.as_real();
    const auto category = std::fpclassify(number);
    return {
        std::isfinite(number), category == FP_ZERO, std::signbit(number),
        category == FP_NORMAL, category == FP_SUBNORMAL,
        category == FP_INFINITE, category == FP_NAN, { }
    };
}

SystemVerilogScalarStorage::SystemVerilogScalarStorage(
    const SystemVerilogScalarStorageLimits limits)
    : limits_(limits)
{
}

SystemVerilogScalarError SystemVerilogScalarStorage::value_error(
    const SystemVerilogScalarValue value) const noexcept
{
    if (value.kind == SystemVerilogScalarKind::Time) {
        return SystemVerilogScalarError::None;
    }
    if (!real_kind(value.kind))
        return SystemVerilogScalarError::InvalidKind;
    const auto number = value.as_real();
    return number && std::isfinite(*number)
        ? SystemVerilogScalarError::None
        : SystemVerilogScalarError::Nonfinite;
}

SystemVerilogScalarMaterialization SystemVerilogScalarStorage::materialize(
    const SystemVerilogScalarValue value)
{
    if (const auto error = value_error(value);
        error != SystemVerilogScalarError::None)
        return { 0, error };
    if (values_.size() >= limits_.maximum_values
        || values_.size() >= limits_.maximum_bytes / canonical_bytes_per_value
        || values_.size() > std::numeric_limits<SystemVerilogScalarId>::max()) {
        return { 0, SystemVerilogScalarError::ResourceLimit };
    }
    const auto id = static_cast<SystemVerilogScalarId>(values_.size());
    values_.push_back(value);
    return { id, SystemVerilogScalarError::None };
}

std::optional<SystemVerilogScalarValue> SystemVerilogScalarStorage::load(
    const SystemVerilogScalarId id) const noexcept
{
    return id < values_.size()
        ? std::optional<SystemVerilogScalarValue> { values_[id] }
        : std::nullopt;
}

SystemVerilogScalarError SystemVerilogScalarStorage::store(
    const SystemVerilogScalarId id,
    const SystemVerilogScalarValue value) noexcept
{
    if (id >= values_.size())
        return SystemVerilogScalarError::InvalidId;
    if (const auto error = value_error(value);
        error != SystemVerilogScalarError::None)
        return error;
    values_[id] = value;
    return SystemVerilogScalarError::None;
}

SystemVerilogScalarMaterialization SystemVerilogScalarStorage::arithmetic(
    const SystemVerilogScalarArithmetic operation,
    const SystemVerilogScalarId left,
    const SystemVerilogScalarId right)
{
    if (left >= values_.size() || right >= values_.size()) {
        return { 0, SystemVerilogScalarError::InvalidId };
    }
    if (operations_ >= limits_.maximum_operations) {
        return { 0, SystemVerilogScalarError::ResourceLimit };
    }
    ++operations_;
    const auto result = systemverilog_scalar_arithmetic(
        operation, values_[left], values_[right]);
    if (!result)
        return { 0, result.error };
    return materialize(result.value);
}

std::size_t SystemVerilogScalarStorage::materialized_bytes() const noexcept
{
    return values_.size() * canonical_bytes_per_value;
}

} // namespace fsim::runtime
