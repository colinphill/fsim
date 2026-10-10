// SPDX-License-Identifier: Apache-2.0
//
// Exact four-state operations on values of at most 64 bits, held as the
// aval/bval word pair used by PackedLogic4 (0=(0,0), 1=(1,0), x=(1,1),
// z=(0,1)). Every function reproduces the corresponding reference value
// function in simir_logic.cpp / simir_arithmetic.cpp bit for bit for logic4
// operands; runtime_kernel_word_ops_tests fuzzes each against the reference.
// Bits above the width are always zero in both planes.
#pragma once

#include "fsim/runtime/simir.hpp"

#include <bit>
#include <cstdint>
#include <limits>
#include <optional>

namespace fsim::runtime::simir::kernel_word {

struct Word {
    std::uint64_t a { };
    std::uint64_t b { };

    friend bool operator==(const Word&, const Word&) = default;
};

[[nodiscard]] constexpr std::uint64_t mask(const std::uint32_t width) noexcept
{
    return width >= 64U ? ~std::uint64_t { 0 }
                        : (std::uint64_t { 1 } << width) - 1U;
}

[[nodiscard]] constexpr bool known(const Word value) noexcept
{
    return value.b == 0U;
}

[[nodiscard]] constexpr Word all_x(const std::uint32_t width) noexcept
{
    return { mask(width), mask(width) };
}

/// Logic4 of one bit: 0, 1, x, z encoded as the reference enum values.
[[nodiscard]] constexpr Logic4 bit(const Word value,
    const std::uint32_t index) noexcept
{
    const auto a = (value.a >> index) & 1U;
    const auto b = (value.b >> index) & 1U;
    return b == 0U ? (a != 0U ? Logic4::one : Logic4::zero)
                   : (a != 0U ? Logic4::x : Logic4::z);
}

[[nodiscard]] constexpr Word from_logic4(const Logic4 value) noexcept
{
    switch (value) {
    case Logic4::zero:
        return { 0U, 0U };
    case Logic4::one:
        return { 1U, 0U };
    case Logic4::x:
        return { 1U, 1U };
    case Logic4::z:
        return { 0U, 1U };
    }
    return { 1U, 1U };
}

[[nodiscard]] constexpr Word bit_and(const Word l, const Word r,
    const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    const auto zero = (~l.a & ~l.b) | (~r.a & ~r.b);
    const auto one = (l.a & ~l.b) & (r.a & ~r.b);
    const auto x = ~(zero | one) & m;
    return { (one | x) & m, x };
}

[[nodiscard]] constexpr Word bit_or(const Word l, const Word r,
    const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    const auto one = (l.a & ~l.b) | (r.a & ~r.b);
    const auto zero = (~l.a & ~l.b) & (~r.a & ~r.b);
    const auto x = ~(zero | one) & m;
    return { (one | x) & m, x };
}

[[nodiscard]] constexpr Word bit_xor(const Word l, const Word r,
    const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    const auto unknown = (l.b | r.b) & m;
    return { ((l.a ^ r.a) | unknown) & m, unknown };
}

[[nodiscard]] constexpr Word unary_not(const Word value,
    const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    return { (~value.a | value.b) & m, value.b & m };
}

/// truth_value(): one when any bit is a known one, else x when any bit is
/// unknown, else zero.
[[nodiscard]] constexpr Logic4 truth(const Word value) noexcept
{
    if ((value.a & ~value.b) != 0U) {
        return Logic4::one;
    }
    return value.b != 0U ? Logic4::x : Logic4::zero;
}

[[nodiscard]] constexpr Word scalar(const Logic4 value) noexcept
{
    return from_logic4(value);
}

[[nodiscard]] constexpr Word logical_not(const Word value) noexcept
{
    return scalar(logic_not(truth(value)));
}

[[nodiscard]] constexpr Word logical_binary(
    const LogicalBinaryOperator operation, const Word l, const Word r) noexcept
{
    const auto left = truth(l);
    const auto right = truth(r);
    return scalar(operation == LogicalBinaryOperator::logical_and
            ? logic_and(left, right)
            : logic_or(left, right));
}

[[nodiscard]] constexpr Word reduce(const ReductionOperator operation,
    const Word value, const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    const auto ones = value.a & ~value.b & m;
    const auto zeros = ~value.a & ~value.b & m;
    const auto unknown = value.b & m;
    switch (operation) {
    case ReductionOperator::one_hot:
        return scalar(std::popcount(ones) == 1 ? Logic4::one : Logic4::zero);
    case ReductionOperator::one_hot_or_zero:
        return scalar(std::popcount(ones) <= 1 ? Logic4::one : Logic4::zero);
    case ReductionOperator::bit_and:
        if (zeros != 0U) {
            return scalar(Logic4::zero);
        }
        return scalar(unknown != 0U ? Logic4::x : Logic4::one);
    case ReductionOperator::bit_or:
        if (ones != 0U) {
            return scalar(Logic4::one);
        }
        return scalar(unknown != 0U ? Logic4::x : Logic4::zero);
    case ReductionOperator::bit_xor:
        if (unknown != 0U) {
            return scalar(Logic4::x);
        }
        return scalar((std::popcount(ones) & 1) != 0 ? Logic4::one
                                                     : Logic4::zero);
    }
    return scalar(Logic4::x);
}

[[nodiscard]] constexpr std::int64_t sign_extend(const std::uint64_t value,
    const std::uint32_t width) noexcept
{
    if (width >= 64U) {
        return static_cast<std::int64_t>(value);
    }
    const auto sign = std::uint64_t { 1 } << (width - 1U);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

[[nodiscard]] constexpr std::uint64_t power_known(std::uint64_t base,
    const std::uint64_t exponent, const std::uint32_t base_width,
    const std::uint32_t exponent_width, const bool signed_exponent) noexcept
{
    const auto m = mask(base_width);
    const bool negative = signed_exponent
        && ((exponent >> (exponent_width - 1U)) & 1U) != 0U;
    if (negative) {
        if (base == 0U) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        if (base == 1U) {
            return 1U;
        }
        if (base == m) {
            return (exponent & 1U) != 0U ? base : 1U;
        }
        return 0U;
    }
    std::uint64_t result = 1U & m;
    auto factor = base & m;
    for (std::uint32_t bit_index = 0U; bit_index < exponent_width;
         ++bit_index) {
        if (((exponent >> bit_index) & 1U) != 0U) {
            result = (result * factor) & m;
        }
        factor = (factor * factor) & m;
    }
    return result;
}

/// Logic9 code planes (plane k holds bit k of each element's ordinal Logic9
/// code) as a Logic4 word, when every element is 0, 1, X or Z.
[[nodiscard]] constexpr std::optional<Word> logic9_word(const std::uint64_t p0,
    const std::uint64_t p1, const std::uint64_t p2, const std::uint64_t p3,
    const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    const auto known = p1 & ~p2;             // 0 (0010) and 1 (0011)
    const auto unknown = ~p2 & ~p1 & p0;     // X (0001)
    const auto high_z = p2 & ~p1 & ~p0;      // Z (0100)
    if ((p3 & m) != 0U || ((known | unknown | high_z) & m) != m) {
        return std::nullopt;
    }
    return Word { p0 & m, (unknown | high_z) & m };
}

/// A Logic4 word with a 'U' mask: 'U' elements (code 0000) read as X in
/// the word. Every element must be 0, 1, X, Z or U.
struct UWord {
    Word value;
    std::uint64_t unknown { };
};

[[nodiscard]] constexpr std::optional<UWord> logic9_uword(const std::uint64_t p0,
    const std::uint64_t p1, const std::uint64_t p2, const std::uint64_t p3,
    const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    const auto unknown = ~(p0 | p1 | p2 | p3) & m;
    const auto word = logic9_word(p0 | unknown, p1, p2, p3, width);
    if (!word) {
        return std::nullopt;
    }
    return UWord { *word, unknown };
}

/// Logic9 code planes coerced to Logic4 (L and H read as 0 and 1, U, W and
/// - as X), as coerce_value_kind does.
[[nodiscard]] constexpr Word logic9_coerced(const std::uint64_t p0,
    const std::uint64_t p1, const std::uint64_t p2, const std::uint64_t p3,
    const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    const auto zero = ~p3 & p1 & ~p0;
    const auto one = ~p3 & p1 & p0;
    const auto high_z = ~p3 & p2 & ~p1 & ~p0;
    const auto unknown = ~(zero | one | high_z);
    return { (one | unknown) & m, (high_z | unknown) & m };
}

/// The Logic9 code planes of a Logic4 word (0, 1, X, Z map to their codes).
struct Logic9Planes {
    std::uint64_t p0 { };
    std::uint64_t p1 { };
    std::uint64_t p2 { };
    std::uint64_t p3 { };
};

[[nodiscard]] constexpr Logic9Planes logic9_planes(const Word value,
    const std::uint32_t width, const std::uint64_t unknown = 0U) noexcept
{
    // 'U' elements have code 0000.
    const auto m = mask(width) & ~unknown;
    return { value.a & m, ~value.b & m, ~value.a & value.b & m, 0U };
}

/// VHDL logic operations with 'U' masks (std_logic_1164 tables over
/// {0, 1, X, Z, U}; 'U' elements read as X in the words).
[[nodiscard]] constexpr std::uint64_t unknown_and(const Word l,
    const std::uint64_t lu, const Word r, const std::uint64_t ru) noexcept
{
    const auto zero_l = ~l.a & ~l.b;
    const auto zero_r = ~r.a & ~r.b;
    return (lu | ru) & ~zero_l & ~zero_r;
}

[[nodiscard]] constexpr std::uint64_t unknown_or(const Word l,
    const std::uint64_t lu, const Word r, const std::uint64_t ru) noexcept
{
    const auto one_l = l.a & ~l.b;
    const auto one_r = r.a & ~r.b;
    return (lu | ru) & ~one_l & ~one_r;
}

/// binary_value() for equal-width logic4 operands. The result width is one
/// for comparisons and the operand width otherwise.
[[nodiscard]] constexpr Word binary(const BinaryOperator operation,
    const Word l, const Word r, const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    const auto unknown = (l.b | r.b) & m;
    switch (operation) {
    case BinaryOperator::bit_and:
        return bit_and(l, r, width);
    case BinaryOperator::bit_or:
        return bit_or(l, r, width);
    case BinaryOperator::bit_xor:
        return bit_xor(l, r, width);
    case BinaryOperator::equal:
        // A known differing bit decides the relation (IEEE 1800-2017
        // 11.4.5).
        if (((l.a ^ r.a) & ~unknown & m) != 0U) {
            return scalar(Logic4::zero);
        }
        if (unknown != 0U) {
            return scalar(Logic4::x);
        }
        return scalar(Logic4::one);
    case BinaryOperator::case_equal:
        return scalar(l.a == r.a && l.b == r.b ? Logic4::one : Logic4::zero);
    case BinaryOperator::casez_equal: {
        const auto z = ((~l.a & l.b) | (~r.a & r.b)) & m;
        const auto differ = ((l.a ^ r.a) | (l.b ^ r.b)) & m & ~z;
        return scalar(differ == 0U ? Logic4::one : Logic4::zero);
    }
    case BinaryOperator::casex_equal: {
        const auto differ = ((l.a ^ r.a) | (l.b ^ r.b)) & m & ~unknown;
        return scalar(differ == 0U ? Logic4::one : Logic4::zero);
    }
    case BinaryOperator::wildcard_equal: {
        const auto care = ~r.b & m;
        if (((l.a ^ r.a) & care & ~l.b) != 0U) {
            return scalar(Logic4::zero);
        }
        if ((l.b & care) != 0U) {
            return scalar(Logic4::x);
        }
        return scalar(((l.a ^ r.a) & care) == 0U ? Logic4::one
                                                 : Logic4::zero);
    }
    case BinaryOperator::not_equal:
        if (((l.a ^ r.a) & ~unknown & m) != 0U) {
            return scalar(Logic4::one);
        }
        if (unknown != 0U) {
            return scalar(Logic4::x);
        }
        return scalar(Logic4::zero);
    case BinaryOperator::less_unsigned:
    case BinaryOperator::less_equal_unsigned:
    case BinaryOperator::greater_unsigned:
    case BinaryOperator::greater_equal_unsigned:
    case BinaryOperator::less_signed:
    case BinaryOperator::less_equal_signed:
    case BinaryOperator::greater_signed:
    case BinaryOperator::greater_equal_signed: {
        if (unknown != 0U) {
            return scalar(Logic4::x);
        }
        const bool is_signed = operation == BinaryOperator::less_signed
            || operation == BinaryOperator::less_equal_signed
            || operation == BinaryOperator::greater_signed
            || operation == BinaryOperator::greater_equal_signed;
        int comparison = 0;
        if (is_signed) {
            const auto left = sign_extend(l.a, width);
            const auto right = sign_extend(r.a, width);
            comparison = left < right ? -1 : left > right ? 1 : 0;
        } else {
            comparison = l.a < r.a ? -1 : l.a > r.a ? 1 : 0;
        }
        bool result = false;
        switch (operation) {
        case BinaryOperator::not_equal:
            result = comparison != 0;
            break;
        case BinaryOperator::less_unsigned:
        case BinaryOperator::less_signed:
            result = comparison < 0;
            break;
        case BinaryOperator::less_equal_unsigned:
        case BinaryOperator::less_equal_signed:
            result = comparison <= 0;
            break;
        case BinaryOperator::greater_unsigned:
        case BinaryOperator::greater_signed:
            result = comparison > 0;
            break;
        default:
            result = comparison >= 0;
            break;
        }
        return scalar(result ? Logic4::one : Logic4::zero);
    }
    case BinaryOperator::vhdl_match_equal:
        // Over {0, 1, X, Z} the VHDL matching relation is true exactly when
        // every element pair is a known equal bit.
        return scalar(((l.b | r.b | (l.a ^ r.a)) & m) != 0U ? Logic4::zero
                                                          : Logic4::one);
    default:
        break;
    }
    // Arithmetic: any unknown operand bit makes the whole result unknown.
    if (unknown != 0U) {
        return all_x(width);
    }
    const auto left = l.a;
    const auto right = r.a;
    switch (operation) {
    case BinaryOperator::add_unsigned:
    case BinaryOperator::add_signed:
        return { (left + right) & m, 0U };
    case BinaryOperator::subtract_unsigned:
    case BinaryOperator::subtract_signed:
        return { (left - right) & m, 0U };
    case BinaryOperator::multiply_unsigned:
    case BinaryOperator::multiply_signed:
        return { (left * right) & m, 0U };
    case BinaryOperator::power_unsigned:
    case BinaryOperator::power_signed: {
        const bool negative = operation == BinaryOperator::power_signed
            && ((right >> (width - 1U)) & 1U) != 0U;
        if (negative && left == 0U) {
            return all_x(width);
        }
        return { power_known(left, right, width, width,
                     operation == BinaryOperator::power_signed) & m, 0U };
    }
    case BinaryOperator::divide_unsigned:
        if (right == 0U) {
            return all_x(width);
        }
        return { (left / right) & m, 0U };
    case BinaryOperator::modulo_unsigned:
        if (right == 0U) {
            return all_x(width);
        }
        return { (left % right) & m, 0U };
    case BinaryOperator::divide_signed:
    case BinaryOperator::remainder_signed:
    case BinaryOperator::modulo_signed: {
        if (right == 0U) {
            return all_x(width);
        }
        const auto sign = std::uint64_t { 1 } << (width - 1U);
        const bool left_negative = (left & sign) != 0U;
        const bool right_negative = (right & sign) != 0U;
        const auto left_magnitude = left_negative ? (0U - left) & m : left;
        const auto right_magnitude = right_negative ? (0U - right) & m : right;
        auto quotient = (left_magnitude / right_magnitude) & m;
        auto remainder = (left_magnitude % right_magnitude) & m;
        if (left_negative != right_negative) {
            quotient = (0U - quotient) & m;
        }
        if (left_negative) {
            remainder = (0U - remainder) & m;
        }
        if (operation == BinaryOperator::divide_signed) {
            return { quotient, 0U };
        }
        if (operation == BinaryOperator::modulo_signed && remainder != 0U
            && left_negative != right_negative) {
            return { (remainder + right) & m, 0U };
        }
        return { remainder, 0U };
    }
    default:
        break;
    }
    return all_x(width);
}

/// shift_value() for a logic4 value of `width` bits and an amount of
/// `amount_width` bits.
[[nodiscard]] constexpr Word shift(ShiftOperator operation, const Word value,
    const std::uint32_t width, const Word amount,
    const std::uint32_t amount_width, const bool signed_amount) noexcept
{
    const auto m = mask(width);
    const auto amount_mask = mask(amount_width);
    if ((amount.b & amount_mask) != 0U) {
        return all_x(width);
    }
    auto magnitude = amount.a & amount_mask;
    if (signed_amount && ((magnitude >> (amount_width - 1U)) & 1U) != 0U) {
        switch (operation) {
        case ShiftOperator::logical_left:
            operation = ShiftOperator::logical_right;
            break;
        case ShiftOperator::logical_right:
            operation = ShiftOperator::logical_left;
            break;
        case ShiftOperator::arithmetic_left:
            operation = ShiftOperator::arithmetic_right;
            break;
        case ShiftOperator::arithmetic_right:
            operation = ShiftOperator::arithmetic_left;
            break;
        case ShiftOperator::rotate_left:
            operation = ShiftOperator::rotate_right;
            break;
        case ShiftOperator::rotate_right:
            operation = ShiftOperator::rotate_left;
            break;
        }
        magnitude = (0U - magnitude) & amount_mask;
    }
    const auto fill_bit = operation == ShiftOperator::arithmetic_right
        ? bit(value, width - 1U)
        : operation == ShiftOperator::arithmetic_left ? bit(value, 0U)
                                                      : Logic4::zero;
    const auto fill_word = from_logic4(fill_bit);
    const Word fill { fill_word.a != 0U ? m : 0U, fill_word.b != 0U ? m : 0U };
    if (operation == ShiftOperator::rotate_left
        || operation == ShiftOperator::rotate_right) {
        const auto amount_value = static_cast<std::uint32_t>(magnitude % width);
        if (amount_value == 0U) {
            return { value.a & m, value.b & m };
        }
        const auto rotate = [&](const std::uint64_t plane) {
            return operation == ShiftOperator::rotate_left
                ? ((plane << amount_value) | (plane >> (width - amount_value))) & m
                : ((plane >> amount_value) | (plane << (width - amount_value))) & m;
        };
        return { rotate(value.a), rotate(value.b) };
    }
    if (magnitude >= width) {
        return fill;
    }
    const auto amount_value = static_cast<std::uint32_t>(magnitude);
    if (amount_value == 0U) {
        return { value.a & m, value.b & m };
    }
    const auto keep = mask(width - amount_value);
    if (operation == ShiftOperator::logical_left
        || operation == ShiftOperator::arithmetic_left) {
        const auto low = mask(amount_value);
        return { ((value.a << amount_value) & m) | (fill.a & low),
            ((value.b << amount_value) & m) | (fill.b & low) };
    }
    const auto high = m & ~keep;
    return { ((value.a >> amount_value) & keep) | (fill.a & high),
        ((value.b >> amount_value) & keep) | (fill.b & high) };
}

[[nodiscard]] constexpr Word extract(const Word value,
    const std::uint32_t offset, const std::uint32_t width) noexcept
{
    const auto m = mask(width);
    if (offset >= 64U) {
        return { };
    }
    return { (value.a >> offset) & m, (value.b >> offset) & m };
}

[[nodiscard]] constexpr Word insert(const Word target, const Word source,
    const std::uint32_t offset, const std::uint32_t source_width) noexcept
{
    const auto field = mask(source_width) << offset;
    return { (target.a & ~field) | ((source.a << offset) & field),
        (target.b & ~field) | ((source.b << offset) & field) };
}

[[nodiscard]] constexpr Word conditional(const Word condition,
    const Word when_true, const Word when_false,
    const std::uint32_t width) noexcept
{
    const auto selected = bit(condition, 0U);
    if (selected == Logic4::one) {
        return when_true;
    }
    if (selected == Logic4::zero) {
        return when_false;
    }
    const auto m = mask(width);
    const auto same = ~((when_true.a ^ when_false.a) | (when_true.b ^ when_false.b))
        & m;
    return { (when_true.a & same) | (~same & m), (when_true.b & same) | (~same & m) };
}

[[nodiscard]] constexpr Word to_two_state(const Word value) noexcept
{
    return { value.a & ~value.b, 0U };
}

/// dynamic_index_offset(); nullopt where the reference throws.
[[nodiscard]] constexpr std::optional<std::uint32_t> dynamic_index(
    const Word index, const DynamicIndex& selection) noexcept
{
    if ((index.b & mask(32U)) != 0U) {
        return std::nullopt;
    }
    const auto raw = static_cast<std::uint32_t>(index.a);
    const auto signed_index = static_cast<std::int64_t>(
        static_cast<std::int32_t>(raw));
    const auto lower = selection.left < selection.right ? selection.left
                                                        : selection.right;
    const auto upper = selection.left < selection.right ? selection.right
                                                        : selection.left;
    if (signed_index < lower || signed_index > upper) {
        return std::nullopt;
    }
    const auto offset = signed_index >= selection.right
        ? static_cast<std::uint64_t>(signed_index - selection.right)
        : static_cast<std::uint64_t>(selection.right - signed_index);
    if (offset > std::numeric_limits<std::uint32_t>::max()
            - selection.base_offset) {
        return std::nullopt;
    }
    return selection.base_offset + static_cast<std::uint32_t>(offset);
}

/// dynamic_part_select_value() for a source of at most 64 bits.
[[nodiscard]] constexpr Word dynamic_part_select(const Word source,
    const std::uint32_t source_width, const Word base, const std::int64_t left,
    const std::int64_t right, const std::uint32_t base_offset,
    const std::uint32_t width, const bool increasing,
    const bool source_descending, const bool two_state) noexcept
{
    Word result = two_state ? Word { } : all_x(width);
    if ((base.b & mask(32U)) != 0U) {
        return result;
    }
    const auto signed_base = static_cast<std::int64_t>(
        static_cast<std::int32_t>(static_cast<std::uint32_t>(base.a)));
    const auto lower = left < right ? left : right;
    const auto upper = left < right ? right : left;
    const auto edge = static_cast<std::int64_t>(width - 1U);
    const auto selected_right = increasing
        ? signed_base + (source_descending ? 0 : edge)
        : signed_base - (source_descending ? edge : 0);
    for (std::uint32_t index = 0U; index < width; ++index) {
        const auto selected = source_descending
            ? selected_right + static_cast<std::int64_t>(index)
            : selected_right - static_cast<std::int64_t>(index);
        if (selected < lower || selected > upper) {
            continue;
        }
        const auto offset = selected >= right
            ? static_cast<std::uint64_t>(selected - right)
            : static_cast<std::uint64_t>(right - selected);
        if (offset > std::numeric_limits<std::uint32_t>::max() - base_offset
            || base_offset + offset >= source_width) {
            continue;
        }
        const auto from = base_offset + static_cast<std::uint32_t>(offset);
        const auto one = std::uint64_t { 1 } << index;
        result.a = (result.a & ~one) | (((source.a >> from) & 1U) << index);
        result.b = (result.b & ~one) | (((source.b >> from) & 1U) << index);
    }
    return result;
}

struct PartWrite {
    Word value;
    std::uint32_t offset { };
    std::uint32_t width { };
};

/// dynamic_part_write_value(): empty when nothing is written; `valid` false
/// where the reference throws.
struct PartWriteResult {
    std::optional<PartWrite> write;
    bool valid { true };
};

[[nodiscard]] constexpr PartWriteResult dynamic_part_write(const Word source,
    const Word base, const DynamicPartIndex& selection) noexcept
{
    if ((base.b & mask(32U)) != 0U) {
        return { };
    }
    const auto signed_base = static_cast<std::int64_t>(
        static_cast<std::int32_t>(static_cast<std::uint32_t>(base.a)));
    const auto lower = selection.left < selection.right ? selection.left
                                                        : selection.right;
    const auto upper = selection.left < selection.right ? selection.right
                                                        : selection.left;
    const auto edge = static_cast<std::int64_t>(selection.width - 1U);
    const auto selected_right = selection.increasing
        ? signed_base + (selection.source_descending ? 0 : edge)
        : signed_base - (selection.source_descending ? edge : 0);
    std::optional<std::uint32_t> first_source;
    std::uint64_t first_target { };
    std::uint32_t selected_width { };
    for (std::uint32_t index = 0U; index < selection.width; ++index) {
        const auto selected = selection.source_descending
            ? selected_right + static_cast<std::int64_t>(index)
            : selected_right - static_cast<std::int64_t>(index);
        if (selected < lower || selected > upper) {
            continue;
        }
        const auto offset = selected >= selection.right
            ? static_cast<std::uint64_t>(selected - selection.right)
            : static_cast<std::uint64_t>(selection.right - selected);
        if (offset > std::numeric_limits<std::uint32_t>::max()) {
            return { std::nullopt, false };
        }
        if (!first_source) {
            first_source = index;
            if (offset > std::numeric_limits<std::uint32_t>::max()
                    - selection.base_offset) {
                return { std::nullopt, false };
            }
            first_target = selection.base_offset + offset;
        } else if (index != *first_source + selected_width
            || selection.base_offset + offset != first_target + selected_width) {
            return { std::nullopt, false };
        }
        ++selected_width;
    }
    if (!first_source) {
        return { };
    }
    return { PartWrite { extract(source, *first_source, selected_width),
                 static_cast<std::uint32_t>(first_target), selected_width },
        true };
}

} // namespace fsim::runtime::simir::kernel_word
