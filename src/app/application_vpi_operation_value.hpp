// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "sv_vpi_user.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace fsim::app::application_detail {

/// The value of an operand or of a result of an operation expression: a
/// vector with its width and sign (IEEE 1800-2023 11.6, 11.8) or a real.
struct VpiOperationValue {
    bool real_value { };
    double real { };
    runtime::PackedLogic4 bits;
    bool is_signed { };
};

namespace operation_value_detail {

using runtime::Logic4;
using runtime::PackedLogic4;

using Bits = std::vector<Logic4>; // least significant bit first

[[nodiscard]] inline Bits to_bits(const PackedLogic4& value)
{
    Bits bits;
    bits.reserve(value.width());
    for (std::size_t index = 0U; index < value.width(); ++index) {
        bits.push_back(value.get(index));
    }
    return bits;
}

[[nodiscard]] inline PackedLogic4 from_bits(const Bits& bits)
{
    PackedLogic4 result { bits.size(), Logic4::zero };
    for (std::size_t index = 0U; index < bits.size(); ++index) {
        result.set(index, bits[index]);
    }
    return result;
}

[[nodiscard]] inline bool known(const Logic4 bit) noexcept
{
    return bit == Logic4::zero || bit == Logic4::one;
}

[[nodiscard]] inline bool all_known(const Bits& bits) noexcept
{
    return std::ranges::all_of(bits, known);
}

// Extend (sign extension only when `sign`) or truncate to `width` bits.
[[nodiscard]] inline Bits extended(
    Bits bits, const std::size_t width, const bool sign)
{
    const auto fill = sign && !bits.empty() ? bits.back() : Logic4::zero;
    // An x or z sign bit extends as itself (11.8.2 and 11.6.1).
    bits.resize(width, fill);
    return bits;
}

[[nodiscard]] inline Bits filled(const std::size_t width, const Logic4 bit)
{
    return Bits(width, bit);
}

[[nodiscard]] inline Bits single(const Logic4 bit)
{
    return Bits(1U, bit);
}

// An unsigned integer of arbitrary width, least significant limb first.
using Limbs = std::vector<std::uint32_t>;

[[nodiscard]] inline Limbs to_limbs(const Bits& bits)
{
    Limbs limbs((bits.size() + 31U) / 32U + 1U, 0U);
    for (std::size_t index = 0U; index < bits.size(); ++index) {
        if (bits[index] == Logic4::one) {
            limbs[index / 32U] |= 1U << (index % 32U);
        }
    }
    return limbs;
}

[[nodiscard]] inline Bits from_limbs(const Limbs& limbs, const std::size_t width)
{
    Bits bits(width, Logic4::zero);
    for (std::size_t index = 0U; index < width; ++index) {
        const auto limb = index / 32U;
        if (limb < limbs.size() && ((limbs[limb] >> (index % 32U)) & 1U) != 0U) {
            bits[index] = Logic4::one;
        }
    }
    return bits;
}

inline void resize_limbs(Limbs& a, Limbs& b)
{
    const auto size = std::max(a.size(), b.size());
    a.resize(size, 0U);
    b.resize(size, 0U);
}

[[nodiscard]] inline int compare_limbs(Limbs a, Limbs b)
{
    resize_limbs(a, b);
    for (std::size_t index = a.size(); index-- > 0U;) {
        if (a[index] != b[index]) {
            return a[index] < b[index] ? -1 : 1;
        }
    }
    return 0;
}

[[nodiscard]] inline Limbs add_limbs(Limbs a, Limbs b)
{
    resize_limbs(a, b);
    a.push_back(0U);
    b.push_back(0U);
    std::uint64_t carry { };
    for (std::size_t index = 0U; index < a.size(); ++index) {
        const auto sum = static_cast<std::uint64_t>(a[index]) + b[index] + carry;
        a[index] = static_cast<std::uint32_t>(sum);
        carry = sum >> 32U;
    }
    return a;
}

// a - b modulo 2^(32 limbs); the caller masks to the result width.
[[nodiscard]] inline Limbs subtract_limbs(Limbs a, Limbs b)
{
    resize_limbs(a, b);
    std::uint64_t borrow { };
    for (std::size_t index = 0U; index < a.size(); ++index) {
        const auto difference = static_cast<std::uint64_t>(a[index])
            - b[index] - borrow;
        a[index] = static_cast<std::uint32_t>(difference);
        borrow = (difference >> 32U) & 1U;
    }
    return a;
}

[[nodiscard]] inline Limbs multiply_limbs(const Limbs& a, const Limbs& b)
{
    Limbs product(a.size() + b.size() + 1U, 0U);
    for (std::size_t i = 0U; i < a.size(); ++i) {
        std::uint64_t carry { };
        for (std::size_t j = 0U; j < b.size(); ++j) {
            const auto cell = static_cast<std::uint64_t>(a[i]) * b[j]
                + product[i + j] + carry;
            product[i + j] = static_cast<std::uint32_t>(cell);
            carry = cell >> 32U;
        }
        for (auto k = i + b.size(); carry != 0U && k < product.size(); ++k) {
            const auto cell = static_cast<std::uint64_t>(product[k]) + carry;
            product[k] = static_cast<std::uint32_t>(cell);
            carry = cell >> 32U;
        }
    }
    return product;
}

[[nodiscard]] inline bool zero_limbs(const Limbs& a)
{
    return std::ranges::all_of(a, [](const auto limb) { return limb == 0U; });
}

// Quotient and remainder by shift and subtract.
inline void divide_limbs(
    const Limbs& dividend, const Limbs& divisor, Limbs& quotient, Limbs& remainder)
{
    const auto size = std::max(dividend.size(), divisor.size());
    quotient.assign(size, 0U);
    remainder.assign(size, 0U);
    for (std::size_t bit = dividend.size() * 32U; bit-- > 0U;) {
        std::uint32_t carry = (dividend[bit / 32U] >> (bit % 32U)) & 1U;
        for (auto& limb : remainder) {
            const auto next = limb >> 31U;
            limb = (limb << 1U) | carry;
            carry = next;
        }
        if (compare_limbs(remainder, divisor) >= 0) {
            remainder = subtract_limbs(remainder, divisor);
            quotient[bit / 32U] |= 1U << (bit % 32U);
        }
    }
}

// Two's complement negation within `width` bits of a known vector.
[[nodiscard]] inline Bits negated(const Bits& bits)
{
    const auto width = bits.size();
    auto limbs = to_limbs(bits);
    for (auto& limb : limbs) {
        limb = ~limb;
    }
    limbs = add_limbs(limbs, Limbs { 1U });
    return from_limbs(limbs, width);
}

// The truth of a value (11.4.7): 1 when some bit is 1, 0 when all bits are 0,
// otherwise unknown.
enum class Truth : std::uint8_t { no, yes, unknown };

[[nodiscard]] inline Truth truth_of(const Bits& bits)
{
    bool unknown = false;
    for (const auto bit : bits) {
        if (bit == Logic4::one) {
            return Truth::yes;
        }
        unknown = unknown || !known(bit);
    }
    return unknown ? Truth::unknown : Truth::no;
}

[[nodiscard]] inline Logic4 logic_of(const Truth truth)
{
    return truth == Truth::yes ? Logic4::one
        : truth == Truth::no   ? Logic4::zero
                               : Logic4::x;
}

[[nodiscard]] inline Logic4 bit_and(const Logic4 a, const Logic4 b)
{
    if (a == Logic4::zero || b == Logic4::zero) {
        return Logic4::zero;
    }
    return known(a) && known(b) ? Logic4::one : Logic4::x;
}

[[nodiscard]] inline Logic4 bit_or(const Logic4 a, const Logic4 b)
{
    if (a == Logic4::one || b == Logic4::one) {
        return Logic4::one;
    }
    return known(a) && known(b) ? Logic4::zero : Logic4::x;
}

[[nodiscard]] inline Logic4 bit_xor(const Logic4 a, const Logic4 b)
{
    if (!known(a) || !known(b)) {
        return Logic4::x;
    }
    return a == b ? Logic4::zero : Logic4::one;
}

[[nodiscard]] inline Logic4 bit_not(const Logic4 a)
{
    return !known(a) ? Logic4::x
        : a == Logic4::one ? Logic4::zero
                           : Logic4::one;
}

[[nodiscard]] inline VpiOperationValue vector_result(
    const Bits& bits, const bool is_signed)
{
    VpiOperationValue result;
    result.bits = from_bits(bits);
    result.is_signed = is_signed;
    return result;
}

[[nodiscard]] inline VpiOperationValue real_result(const double value)
{
    VpiOperationValue result;
    result.real_value = true;
    result.real = value;
    return result;
}

[[nodiscard]] inline VpiOperationValue bit_result(const Logic4 bit)
{
    return vector_result(single(bit), false);
}

// The value of a vector operand as a real number (11.7).
[[nodiscard]] inline double as_real(const VpiOperationValue& value)
{
    if (value.real_value) {
        return value.real;
    }
    const auto bits = to_bits(value.bits);
    double result { };
    const bool negative = value.is_signed && !bits.empty()
        && bits.back() == Logic4::one;
    const auto magnitude = negative ? negated(bits) : bits;
    for (std::size_t index = magnitude.size(); index-- > 0U;) {
        result = result * 2.0 + (magnitude[index] == Logic4::one ? 1.0 : 0.0);
    }
    return negative ? -result : result;
}

// Signed or unsigned numeric comparison of known vectors extended to a common
// width; negative, zero or positive.
[[nodiscard]] inline int compare_numbers(
    const Bits& left, const Bits& right, const bool is_signed)
{
    const bool left_negative = is_signed && left.back() == Logic4::one;
    const bool right_negative = is_signed && right.back() == Logic4::one;
    if (left_negative != right_negative) {
        return left_negative ? -1 : 1;
    }
    return compare_limbs(to_limbs(left), to_limbs(right));
}

} // namespace operation_value_detail

/// The value of an operation expression (IEEE 1800-2023 11.4, 11.6, 11.8,
/// 37.78) from the values of its operands in source order: the expression is
/// self-determined, so its width is the widest operand of a context-determined
/// operator, and it is signed when all of those operands are. None for an
/// operator that has no value as an expression (an increment, a streaming
/// operator, a cast) or for operand values it cannot combine.
[[nodiscard]] inline std::optional<VpiOperationValue> evaluate_operation(
    const PLI_INT32 op_type, const std::span<const VpiOperationValue> operands)
{
    using namespace operation_value_detail;
    const auto count = operands.size();
    const bool any_real = std::ranges::any_of(
        operands, [](const auto& value) { return value.real_value; });
    const auto bits_of = [&](const std::size_t index) {
        return to_bits(operands[index].bits);
    };
    switch (op_type) {
    // Unary operators (11.4.1, 11.4.9).
    case vpiPlusOp:
        return count == 1U ? std::optional { operands[0] } : std::nullopt;
    case vpiMinusOp: {
        if (count != 1U) {
            return std::nullopt;
        }
        if (any_real) {
            return real_result(-operands[0].real);
        }
        auto bits = bits_of(0U);
        if (!all_known(bits)) {
            return vector_result(filled(bits.size(), Logic4::x),
                operands[0].is_signed);
        }
        return vector_result(negated(bits), operands[0].is_signed);
    }
    case vpiNotOp: {
        if (count != 1U) {
            return std::nullopt;
        }
        if (any_real) {
            return bit_result(operands[0].real == 0.0 ? Logic4::one
                                                       : Logic4::zero);
        }
        const auto truth = truth_of(bits_of(0U));
        return bit_result(truth == Truth::unknown ? Logic4::x
                : truth == Truth::yes             ? Logic4::zero
                                                  : Logic4::one);
    }
    case vpiBitNegOp: {
        if (count != 1U || any_real) {
            return std::nullopt;
        }
        auto bits = bits_of(0U);
        for (auto& bit : bits) {
            bit = bit_not(bit);
        }
        return vector_result(bits, operands[0].is_signed);
    }
    case vpiUnaryAndOp:
    case vpiUnaryNandOp:
    case vpiUnaryOrOp:
    case vpiUnaryNorOp:
    case vpiUnaryXorOp:
    case vpiUnaryXNorOp: {
        if (count != 1U || any_real) {
            return std::nullopt;
        }
        const auto bits = bits_of(0U);
        const bool is_and = op_type == vpiUnaryAndOp || op_type == vpiUnaryNandOp;
        const bool is_or = op_type == vpiUnaryOrOp || op_type == vpiUnaryNorOp;
        Logic4 accumulated = is_and ? Logic4::one : Logic4::zero;
        for (const auto bit : bits) {
            accumulated = is_and ? bit_and(accumulated, bit)
                : is_or          ? bit_or(accumulated, bit)
                                 : bit_xor(accumulated, bit);
        }
        const bool invert = op_type == vpiUnaryNandOp || op_type == vpiUnaryNorOp
            || op_type == vpiUnaryXNorOp;
        return bit_result(invert ? bit_not(accumulated) : accumulated);
    }
    // Concatenation: the first operand is the most significant (11.4.12).
    case vpiConcatOp: {
        if (count == 0U || any_real) {
            return std::nullopt;
        }
        Bits bits;
        for (std::size_t index = count; index-- > 0U;) {
            const auto part = bits_of(index);
            bits.insert(bits.end(), part.begin(), part.end());
        }
        return vector_result(bits, false);
    }
    // Replication `{n{a, b}}`: the first operand is the constant repetition
    // count, the others the concatenated items, the first most significant
    // (11.4.12.1). A count of zero or beyond 65,536 has no value here.
    case vpiMultiConcatOp: {
        if (count < 2U || any_real) {
            return std::nullopt;
        }
        const auto repeat_bits = bits_of(0U);
        const auto repeat = to_limbs(repeat_bits);
        if (!all_known(repeat_bits)
            || std::ranges::any_of(std::span { repeat }.subspan(1U),
                [](const std::uint32_t limb) { return limb != 0U; })
            || repeat[0] == 0U || repeat[0] > 65536U) {
            return std::nullopt;
        }
        Bits item;
        for (std::size_t index = count; index-- > 1U;) {
            const auto part = bits_of(index);
            item.insert(item.end(), part.begin(), part.end());
        }
        if (item.empty()) {
            return std::nullopt;
        }
        Bits bits;
        bits.reserve(item.size() * repeat[0]);
        for (std::uint32_t copy = 0U; copy < repeat[0]; ++copy) {
            bits.insert(bits.end(), item.begin(), item.end());
        }
        return vector_result(bits, false);
    }
    // Logical operators: operands are self-determined, the result one bit
    // (11.4.7).
    case vpiLogAndOp:
    case vpiLogOrOp:
    case vpiImplyOp: {
        if (count != 2U) {
            return std::nullopt;
        }
        const auto truth = [&](const std::size_t index) {
            return any_real
                ? (operands[index].real_value
                          ? (operands[index].real != 0.0 ? Truth::yes : Truth::no)
                          : truth_of(bits_of(index)))
                : truth_of(bits_of(index));
        };
        const auto a = truth(0U);
        const auto b = truth(1U);
        const auto yes_of = [](const Truth value) {
            return value == Truth::yes;
        };
        const auto no_of = [](const Truth value) { return value == Truth::no; };
        if (op_type == vpiLogAndOp) {
            return bit_result(no_of(a) || no_of(b) ? Logic4::zero
                    : yes_of(a) && yes_of(b)       ? Logic4::one
                                                   : Logic4::x);
        }
        if (op_type == vpiLogOrOp) {
            return bit_result(yes_of(a) || yes_of(b) ? Logic4::one
                    : no_of(a) && no_of(b)           ? Logic4::zero
                                                     : Logic4::x);
        }
        if (op_type == vpiImplyOp) {
            return bit_result(no_of(a) || yes_of(b) ? Logic4::one
                    : yes_of(a) && no_of(b)         ? Logic4::zero
                                                    : Logic4::x);
        }
        return std::nullopt;
    }
    // The condition is self-determined, the branches share one type (11.4.11).
    case vpiConditionOp: {
        if (count != 3U) {
            return std::nullopt;
        }
        const auto truth = operands[0].real_value
            ? (operands[0].real != 0.0 ? Truth::yes : Truth::no)
            : truth_of(bits_of(0U));
        if (operands[1].real_value || operands[2].real_value) {
            if (truth == Truth::unknown) {
                return std::nullopt;
            }
            const auto& chosen = operands[truth == Truth::yes ? 1U : 2U];
            return real_result(as_real(chosen));
        }
        const auto width = std::max(operands[1].bits.width(),
            operands[2].bits.width());
        const bool is_signed = operands[1].is_signed && operands[2].is_signed;
        auto yes = extended(bits_of(1U), width, is_signed);
        auto no = extended(bits_of(2U), width, is_signed);
        if (truth == Truth::yes) {
            return vector_result(yes, is_signed);
        }
        if (truth == Truth::no) {
            return vector_result(no, is_signed);
        }
        // An unknown condition combines the branches bit by bit.
        for (std::size_t index = 0U; index < width; ++index) {
            if (yes[index] != no[index] || !known(yes[index])) {
                yes[index] = Logic4::x;
            }
        }
        return vector_result(yes, is_signed);
    }
    default:
        break;
    }
    if (count != 2U) {
        return std::nullopt;
    }
    // Shifts: the left operand keeps its width and sign, the amount is
    // self-determined and unsigned (11.4.10, 11.8.1).
    if (op_type == vpiLShiftOp || op_type == vpiRShiftOp
        || op_type == vpiArithLShiftOp || op_type == vpiArithRShiftOp) {
        if (any_real) {
            return std::nullopt;
        }
        auto bits = bits_of(0U);
        const auto amount = bits_of(1U);
        const auto width = bits.size();
        if (!all_known(amount)) {
            return vector_result(filled(width, Logic4::x), operands[0].is_signed);
        }
        std::size_t distance { };
        bool beyond = false;
        for (std::size_t index = amount.size(); index-- > 0U;) {
            if (amount[index] == Logic4::one) {
                if (index >= 40U) {
                    beyond = true;
                } else {
                    distance |= std::size_t { 1 } << index;
                }
            }
        }
        if (beyond || distance > width) {
            distance = width;
        }
        const bool left = op_type == vpiLShiftOp || op_type == vpiArithLShiftOp;
        const auto fill = op_type == vpiArithRShiftOp && operands[0].is_signed
            ? bits.back()
            : Logic4::zero;
        Bits shifted(width, left ? Logic4::zero : fill);
        for (std::size_t index = 0U; index < width; ++index) {
            if (left && index >= distance) {
                shifted[index] = bits[index - distance];
            } else if (!left && index + distance < width) {
                shifted[index] = bits[index + distance];
            }
        }
        return vector_result(shifted, operands[0].is_signed);
    }
    if (op_type == vpiPowerOp && !any_real) {
        auto base = bits_of(0U);
        const auto exponent = bits_of(1U);
        const auto width = base.size();
        const bool is_signed = operands[0].is_signed;
        if (!all_known(base) || !all_known(exponent)) {
            return vector_result(filled(width, Logic4::x), is_signed);
        }
        const bool negative_exponent = operands[1].is_signed
            && exponent.back() == Logic4::one;
        const auto exponent_limbs = to_limbs(exponent);
        const bool base_zero = zero_limbs(to_limbs(base));
        const bool base_one = !base_zero
            && compare_limbs(to_limbs(base), Limbs { 1U }) == 0;
        const bool base_minus_one = is_signed && std::ranges::all_of(base,
            [](const Logic4 bit) { return bit == Logic4::one; });
        if (negative_exponent) {
            // 1 ** -n is 1, (-1) ** -n is +-1, 0 ** -n is x, otherwise 0
            // (11.4.3).
            if (base_zero) {
                return vector_result(filled(width, Logic4::x), is_signed);
            }
            if (base_one || (base_minus_one
                    && (exponent_limbs[0] & 1U) == 0U)) {
                Bits one(width, Logic4::zero);
                one[0] = Logic4::one;
                return vector_result(one, is_signed);
            }
            if (base_minus_one) {
                return vector_result(filled(width, Logic4::one), is_signed);
            }
            return vector_result(filled(width, Logic4::zero), is_signed);
        }
        // Square and multiply modulo 2^width.
        Limbs result { 1U };
        auto square = to_limbs(base);
        for (std::size_t index = 0U; index < exponent.size(); ++index) {
            if (exponent[index] == Logic4::one) {
                result = to_limbs(from_limbs(multiply_limbs(result, square), width));
            }
            if (index + 1U < exponent.size()) {
                square = to_limbs(from_limbs(multiply_limbs(square, square), width));
            }
        }
        return vector_result(from_limbs(result, width), is_signed);
    }
    if (any_real) {
        const double a = as_real(operands[0]);
        const double b = as_real(operands[1]);
        switch (op_type) {
        case vpiAddOp:
            return real_result(a + b);
        case vpiSubOp:
            return real_result(a - b);
        case vpiMultOp:
            return real_result(a * b);
        case vpiDivOp:
            return real_result(a / b);
        case vpiPowerOp:
            return real_result(std::pow(a, b));
        case vpiEqOp:
            return bit_result(a == b ? Logic4::one : Logic4::zero);
        case vpiNeqOp:
            return bit_result(a != b ? Logic4::one : Logic4::zero);
        case vpiLtOp:
            return bit_result(a < b ? Logic4::one : Logic4::zero);
        case vpiLeOp:
            return bit_result(a <= b ? Logic4::one : Logic4::zero);
        case vpiGtOp:
            return bit_result(a > b ? Logic4::one : Logic4::zero);
        case vpiGeOp:
            return bit_result(a >= b ? Logic4::one : Logic4::zero);
        default:
            return std::nullopt;
        }
    }
    // Context-determined operands share the wider width, and are signed only
    // when both are (11.6.1, 11.8.1).
    const auto width = std::max(operands[0].bits.width(), operands[1].bits.width());
    if (width == 0U) {
        return std::nullopt;
    }
    const bool is_signed = operands[0].is_signed && operands[1].is_signed;
    const auto left = extended(bits_of(0U), width, is_signed);
    const auto right = extended(bits_of(1U), width, is_signed);
    const bool known_operands = all_known(left) && all_known(right);
    switch (op_type) {
    case vpiAddOp:
    case vpiSubOp:
    case vpiMultOp:
    case vpiDivOp:
    case vpiModOp: {
        if (!known_operands) {
            return vector_result(filled(width, Logic4::x), is_signed);
        }
        const auto a = to_limbs(left);
        const auto b = to_limbs(right);
        if (op_type == vpiAddOp) {
            return vector_result(from_limbs(add_limbs(a, b), width), is_signed);
        }
        if (op_type == vpiSubOp) {
            return vector_result(from_limbs(subtract_limbs(a, b), width), is_signed);
        }
        if (op_type == vpiMultOp) {
            return vector_result(from_limbs(multiply_limbs(a, b), width), is_signed);
        }
        if (zero_limbs(b)) {
            return vector_result(filled(width, Logic4::x), is_signed);
        }
        // Signed division truncates toward zero; the remainder takes the
        // sign of the first operand (11.4.2).
        const bool left_negative = is_signed && left.back() == Logic4::one;
        const bool right_negative = is_signed && right.back() == Logic4::one;
        const auto magnitude_a = left_negative ? to_limbs(negated(left)) : a;
        const auto magnitude_b = right_negative ? to_limbs(negated(right)) : b;
        Limbs quotient;
        Limbs remainder;
        divide_limbs(magnitude_a, magnitude_b, quotient, remainder);
        if (op_type == vpiDivOp) {
            auto bits = from_limbs(quotient, width);
            return vector_result(
                left_negative != right_negative ? negated(bits) : bits, is_signed);
        }
        auto bits = from_limbs(remainder, width);
        return vector_result(left_negative ? negated(bits) : bits, is_signed);
    }
    case vpiBitAndOp:
    case vpiBitOrOp:
    case vpiBitXorOp:
    case vpiBitXNorOp: {
        Bits bits(width, Logic4::zero);
        for (std::size_t index = 0U; index < width; ++index) {
            auto bit = op_type == vpiBitAndOp ? bit_and(left[index], right[index])
                : op_type == vpiBitOrOp       ? bit_or(left[index], right[index])
                                              : bit_xor(left[index], right[index]);
            bits[index] = op_type == vpiBitXNorOp ? bit_not(bit) : bit;
        }
        return vector_result(bits, is_signed);
    }
    case vpiEqOp:
    case vpiNeqOp: {
        // A known mismatch decides; otherwise an x or z makes it unknown
        // (11.4.5).
        bool mismatch = false;
        bool unknown = false;
        for (std::size_t index = 0U; index < width; ++index) {
            if (known(left[index]) && known(right[index])) {
                mismatch = mismatch || left[index] != right[index];
            } else {
                unknown = true;
            }
        }
        const auto equal = mismatch ? Logic4::zero
            : unknown               ? Logic4::x
                                    : Logic4::one;
        return bit_result(op_type == vpiEqOp ? equal : bit_not(equal));
    }
    case vpiCaseEqOp:
    case vpiCaseNeqOp: {
        const bool same = left == right;
        return bit_result((op_type == vpiCaseEqOp) == same ? Logic4::one
                                                            : Logic4::zero);
    }
    // x and z bits of the right operand are wildcards (11.4.6).
    case vpiWildEqOp:
    case vpiWildNeqOp: {
        bool mismatch = false;
        bool unknown = false;
        for (std::size_t index = 0U; index < width; ++index) {
            if (!known(right[index])) {
                continue;
            }
            if (!known(left[index])) {
                unknown = true;
            } else {
                mismatch = mismatch || left[index] != right[index];
            }
        }
        const auto equal = mismatch ? Logic4::zero
            : unknown               ? Logic4::x
                                    : Logic4::one;
        return bit_result(op_type == vpiWildEqOp ? equal : bit_not(equal));
    }
    case vpiLtOp:
    case vpiLeOp:
    case vpiGtOp:
    case vpiGeOp: {
        if (!known_operands) {
            return bit_result(Logic4::x);
        }
        const auto order = compare_numbers(left, right, is_signed);
        const bool result = op_type == vpiLtOp ? order < 0
            : op_type == vpiLeOp               ? order <= 0
            : op_type == vpiGtOp               ? order > 0
                                               : order >= 0;
        return bit_result(result ? Logic4::one : Logic4::zero);
    }
    default:
        return std::nullopt;
    }
}

} // namespace fsim::app::application_detail
