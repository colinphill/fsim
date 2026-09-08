// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

[[nodiscard]] bool has_unknown(const PackedLogic4& value) {
  for (std::size_t index = 0; index < value.width(); ++index) {
    const auto bit = value.get(index);
    if (bit == Logic4::x || bit == Logic4::z) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool is_zero(const PackedLogic4& value) {
  for (std::size_t index = 0; index < value.width(); ++index) {
    if (value.get(index) == Logic4::one) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_one(const PackedLogic4& value) {
  if (value.get(0) != Logic4::one) {
    return false;
  }
  for (std::size_t index = 1; index < value.width(); ++index) {
    if (value.get(index) != Logic4::zero) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool is_all_ones(const PackedLogic4& value) {
  for (std::size_t index = 0; index < value.width(); ++index) {
    if (value.get(index) != Logic4::one) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] int compare_known_unsigned(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs) {
  for (std::size_t index = lhs.width(); index-- > 0;) {
    if (lhs.get(index) == rhs.get(index)) {
      continue;
    }
    return lhs.get(index) == Logic4::one ? 1 : -1;
  }
  return 0;
}

[[nodiscard]] PackedLogic4 subtract_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs) {
  PackedLogic4 result(lhs.width(), Logic4::zero);
  bool borrow = false;
  for (std::size_t index = 0; index < lhs.width(); ++index) {
    const bool left = lhs.get(index) == Logic4::one;
    const bool right = rhs.get(index) == Logic4::one;
    result.set(
        index,
        (left != right) != borrow ? Logic4::one : Logic4::zero);
    borrow = (!left && (right || borrow)) || (right && borrow);
  }
  return result;
}

[[nodiscard]] PackedLogic4 add_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs) {
  PackedLogic4 result(lhs.width(), Logic4::zero);
  bool carry = false;
  for (std::size_t index = 0; index < lhs.width(); ++index) {
    const bool left = lhs.get(index) == Logic4::one;
    const bool right = rhs.get(index) == Logic4::one;
    result.set(
        index,
        (left != right) != carry ? Logic4::one : Logic4::zero);
    carry = (left && right) || (carry && (left || right));
  }
  return result;
}

[[nodiscard]] PackedLogic4 negate_known(
    const PackedLogic4& value) {
  return subtract_known(
      PackedLogic4(value.width(), Logic4::zero), value);
}

[[nodiscard]] PackedLogic4 multiply_known(
    const PackedLogic4& lhs,
    const PackedLogic4& rhs) {
  PackedLogic4 result(lhs.width(), Logic4::zero);
  for (std::size_t rhs_bit = 0; rhs_bit < rhs.width(); ++rhs_bit) {
    if (rhs.get(rhs_bit) != Logic4::one) {
      continue;
    }
    bool carry = false;
    for (std::size_t bit = rhs_bit; bit < result.width(); ++bit) {
      const bool accumulated = result.get(bit) == Logic4::one;
      const bool multiplicand =
          lhs.get(bit - rhs_bit) == Logic4::one;
      result.set(
          bit,
          (accumulated != multiplicand) != carry
              ? Logic4::one
              : Logic4::zero);
      carry =
          (accumulated && multiplicand)
          || (carry && (accumulated || multiplicand));
    }
  }
  return result;
}

[[nodiscard]] PackedLogic4 power_known(
    const PackedLogic4& base,
    const PackedLogic4& exponent,
    const bool signed_exponent) {
  const bool negative =
      signed_exponent
      && exponent.get(exponent.width() - 1) == Logic4::one;
  if (negative) {
    if (is_zero(base)) {
      return PackedLogic4(base.width(), Logic4::x);
    }
    if (is_one(base)) {
      auto result = PackedLogic4(base.width(), Logic4::zero);
      result.set(0, Logic4::one);
      return result;
    }
    if (is_all_ones(base)) {
      if (exponent.get(0) == Logic4::one) {
        return base;
      }
      auto result = PackedLogic4(base.width(), Logic4::zero);
      result.set(0, Logic4::one);
      return result;
    }
    return PackedLogic4(base.width(), Logic4::zero);
  }

  auto result = PackedLogic4(base.width(), Logic4::zero);
  result.set(0, Logic4::one);
  auto factor = base;
  for (std::size_t bit = 0; bit < exponent.width(); ++bit) {
    if (exponent.get(bit) == Logic4::one) {
      result = multiply_known(result, factor);
    }
    if (bit + 1 < exponent.width()) {
      factor = multiply_known(factor, factor);
    }
  }
  return result;
}

[[nodiscard]] PackedLogic4 divide_known(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor,
    const bool return_remainder) {
  PackedLogic4 quotient(dividend.width(), Logic4::zero);
  PackedLogic4 remainder(dividend.width(), Logic4::zero);
  for (std::size_t dividend_bit = dividend.width();
       dividend_bit-- > 0;) {
    for (std::size_t bit = remainder.width(); bit-- > 1;) {
      remainder.set(bit, remainder.get(bit - 1));
    }
    remainder.set(0, dividend.get(dividend_bit));
    if (compare_known_unsigned(remainder, divisor) >= 0) {
      remainder = subtract_known(remainder, divisor);
      quotient.set(dividend_bit, Logic4::one);
    }
  }
  return return_remainder ? remainder : quotient;
}



[[nodiscard]] SignedDivision divide_known_signed(
    const PackedLogic4& dividend,
    const PackedLogic4& divisor) {
  const bool dividend_negative =
      dividend.get(dividend.width() - 1) == Logic4::one;
  const bool divisor_negative =
      divisor.get(divisor.width() - 1) == Logic4::one;
  const auto dividend_magnitude =
      dividend_negative ? negate_known(dividend) : dividend;
  const auto divisor_magnitude =
      divisor_negative ? negate_known(divisor) : divisor;
  auto quotient =
      divide_known(dividend_magnitude, divisor_magnitude, false);
  auto remainder =
      divide_known(dividend_magnitude, divisor_magnitude, true);
  if (dividend_negative != divisor_negative) {
    quotient = negate_known(quotient);
  }
  if (dividend_negative) {
    remainder = negate_known(remainder);
  }
  return {std::move(quotient), std::move(remainder)};
}

[[nodiscard]] PackedLogic4 binary_value(BinaryOperator operation,
                                        const PackedLogic4 &lhs,
                                        const PackedLogic4 &rhs) {
  if (lhs.width() != rhs.width()) {
    throw std::invalid_argument(
        "binary operands have different widths (left="
        + std::to_string(lhs.width()) + ", right="
        + std::to_string(rhs.width()) + ")");
  }
  if (lhs.empty()) {
    throw std::invalid_argument("binary operands must not be empty");
  }
  if (operation == BinaryOperator::equal) {
    bool unknown = false;
    bool mismatch = false;
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      const auto left = lhs.get(index);
      const auto right = rhs.get(index);
      const bool left_known = left == Logic4::zero || left == Logic4::one;
      const bool right_known = right == Logic4::zero || right == Logic4::one;
      if (!left_known || !right_known) {
        unknown = true;
      } else if (left != right) {
        mismatch = true;
      }
    }
    return PackedLogic4(
        1,
        unknown ? Logic4::x
                : mismatch ? Logic4::zero : Logic4::one);
  }
  if (operation == BinaryOperator::case_equal
      || operation == BinaryOperator::casez_equal
      || operation == BinaryOperator::casex_equal) {
    auto result = PackedLogic4(1, Logic4::one);
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      if (operation == BinaryOperator::case_equal
          && (lhs.is_logic9() || rhs.is_logic9())) {
        if (lhs.get_logic9(index)
            != rhs.get_logic9(index)) {
          result.set(0, Logic4::zero);
          break;
        }
        continue;
      }
      const auto left = lhs.get(index);
      const auto right = rhs.get(index);
      const auto wildcard =
          operation == BinaryOperator::casez_equal
              ? left == Logic4::z || right == Logic4::z
              : operation == BinaryOperator::casex_equal
                  && (left == Logic4::x || left == Logic4::z
                      || right == Logic4::x || right == Logic4::z);
      if (!wildcard && left != right) {
        result.set(0, Logic4::zero);
        break;
      }
    }
    return result;
  }
  if (operation == BinaryOperator::wildcard_equal) {
    bool unknown = false;
    bool mismatch = false;
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      const auto left = lhs.get(index);
      const auto right = rhs.get(index);
      if (right == Logic4::x || right == Logic4::z) {
        continue;
      }
      if (left == Logic4::x || left == Logic4::z) {
        unknown = true;
      } else if (left != right) {
        mismatch = true;
      }
    }
    return PackedLogic4(
        1,
        unknown ? Logic4::x
                : mismatch ? Logic4::zero : Logic4::one);
  }
  if (operation == BinaryOperator::vhdl_match_equal) {
    const auto match_class = [](const Logic9 value) {
      if (value == Logic9::dont_care) {
        return -1;
      }
      if (value == Logic9::zero || value == Logic9::l) {
        return 0;
      }
      if (value == Logic9::one || value == Logic9::h) {
        return 1;
      }
      return 2;
    };
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      const auto left = match_class(lhs.get_logic9(index));
      const auto right = match_class(rhs.get_logic9(index));
      if (left != -1 && right != -1
          && (left == 2 || right == 2 || left != right)) {
        return PackedLogic4(1, Logic4::zero);
      }
    }
    return PackedLogic4(1, Logic4::one);
  }
  if (operation == BinaryOperator::not_equal
      || operation == BinaryOperator::less_unsigned
      || operation == BinaryOperator::less_equal_unsigned
      || operation == BinaryOperator::greater_unsigned
      || operation == BinaryOperator::greater_equal_unsigned
      || operation == BinaryOperator::less_signed
      || operation == BinaryOperator::less_equal_signed
      || operation == BinaryOperator::greater_signed
      || operation == BinaryOperator::greater_equal_signed) {
    for (std::size_t index = 0; index < lhs.width(); ++index) {
      const auto left = lhs.get(index);
      const auto right = rhs.get(index);
      if (left == Logic4::x || left == Logic4::z
          || right == Logic4::x || right == Logic4::z) {
        return PackedLogic4(1, Logic4::x);
      }
    }
    const bool signed_comparison =
        operation == BinaryOperator::less_signed
        || operation == BinaryOperator::less_equal_signed
        || operation == BinaryOperator::greater_signed
        || operation == BinaryOperator::greater_equal_signed;
    const bool lhs_negative =
        lhs.get(lhs.width() - 1) == Logic4::one;
    const bool rhs_negative =
        rhs.get(rhs.width() - 1) == Logic4::one;
    auto comparison = 0;
    if (signed_comparison && lhs_negative != rhs_negative) {
      comparison = lhs_negative ? -1 : 1;
    } else {
      comparison = compare_known_unsigned(lhs, rhs);
    }
    const bool less = comparison < 0;
    const bool greater = comparison > 0;
    bool result = false;
    switch (operation) {
    case BinaryOperator::not_equal:
      result = less || greater;
      break;
    case BinaryOperator::less_unsigned:
      result = less;
      break;
    case BinaryOperator::less_equal_unsigned:
      result = less || !greater;
      break;
    case BinaryOperator::greater_unsigned:
      result = greater;
      break;
    case BinaryOperator::greater_equal_unsigned:
      result = greater || !less;
      break;
    case BinaryOperator::less_signed:
      result = less;
      break;
    case BinaryOperator::less_equal_signed:
      result = less || !greater;
      break;
    case BinaryOperator::greater_signed:
      result = greater;
      break;
    case BinaryOperator::greater_equal_signed:
      result = greater || !less;
      break;
    case BinaryOperator::bit_and:
    case BinaryOperator::bit_or:
    case BinaryOperator::bit_xor:
    case BinaryOperator::add_unsigned:
    case BinaryOperator::subtract_unsigned:
    case BinaryOperator::multiply_unsigned:
    case BinaryOperator::power_unsigned:
    case BinaryOperator::divide_unsigned:
    case BinaryOperator::modulo_unsigned:
    case BinaryOperator::add_signed:
    case BinaryOperator::subtract_signed:
    case BinaryOperator::multiply_signed:
    case BinaryOperator::power_signed:
    case BinaryOperator::divide_signed:
    case BinaryOperator::remainder_signed:
    case BinaryOperator::modulo_signed:
    case BinaryOperator::equal:
    case BinaryOperator::case_equal:
    case BinaryOperator::casez_equal:
    case BinaryOperator::casex_equal:
    case BinaryOperator::wildcard_equal:
    case BinaryOperator::vhdl_match_equal:
      break;
    }
    return PackedLogic4(
        1, result ? Logic4::one : Logic4::zero);
  }

  PackedLogic4 result(lhs.width(), Logic4::zero);
  const auto exact_logic9 =
      lhs.is_logic9() || rhs.is_logic9();
  if (exact_logic9) {
    result.fill(Logic9::u);
  }
  const bool arithmetic =
      operation == BinaryOperator::add_unsigned
      || operation == BinaryOperator::subtract_unsigned
      || operation == BinaryOperator::multiply_unsigned
      || operation == BinaryOperator::power_unsigned
      || operation == BinaryOperator::divide_unsigned
      || operation == BinaryOperator::modulo_unsigned
      || operation == BinaryOperator::add_signed
      || operation == BinaryOperator::subtract_signed
      || operation == BinaryOperator::multiply_signed
      || operation == BinaryOperator::power_signed
      || operation == BinaryOperator::divide_signed
      || operation == BinaryOperator::remainder_signed
      || operation == BinaryOperator::modulo_signed;
  if (arithmetic && (has_unknown(lhs) || has_unknown(rhs))) {
    return PackedLogic4(lhs.width(), Logic4::x);
  }
  if (operation == BinaryOperator::add_unsigned
      || operation == BinaryOperator::add_signed) {
    return add_known(lhs, rhs);
  }
  if (operation == BinaryOperator::subtract_unsigned
      || operation == BinaryOperator::subtract_signed) {
    return subtract_known(lhs, rhs);
  }
  if (operation == BinaryOperator::multiply_unsigned
      || operation == BinaryOperator::multiply_signed) {
    return multiply_known(lhs, rhs);
  }
  if (operation == BinaryOperator::power_unsigned
      || operation == BinaryOperator::power_signed) {
    return power_known(
        lhs,
        rhs,
        operation == BinaryOperator::power_signed);
  }
  if (operation == BinaryOperator::divide_unsigned
      || operation == BinaryOperator::modulo_unsigned) {
    if (is_zero(rhs)) {
      return PackedLogic4(lhs.width(), Logic4::x);
    }
    return divide_known(
        lhs, rhs, operation == BinaryOperator::modulo_unsigned);
  }
  if (operation == BinaryOperator::divide_signed
      || operation == BinaryOperator::remainder_signed
      || operation == BinaryOperator::modulo_signed) {
    if (is_zero(rhs)) {
      return PackedLogic4(lhs.width(), Logic4::x);
    }
    auto divided = divide_known_signed(lhs, rhs);
    if (operation == BinaryOperator::divide_signed) {
      return divided.quotient;
    }
    if (operation == BinaryOperator::modulo_signed
        && !is_zero(divided.remainder)
        && (lhs.get(lhs.width() - 1)
            != rhs.get(rhs.width() - 1))) {
      return add_known(divided.remainder, rhs);
    }
    return divided.remainder;
  }

  for (std::size_t index = 0; index < lhs.width(); ++index) {
    switch (operation) {
    case BinaryOperator::bit_and:
      if (exact_logic9) {
        result.set_logic9(
            index,
            logic_and(
                lhs.get_logic9(index),
                rhs.get_logic9(index)));
      } else {
        result.set(
            index, logic_and(lhs.get(index), rhs.get(index)));
      }
      break;
    case BinaryOperator::bit_or:
      if (exact_logic9) {
        result.set_logic9(
            index,
            logic_or(
                lhs.get_logic9(index),
                rhs.get_logic9(index)));
      } else {
        result.set(
            index, logic_or(lhs.get(index), rhs.get(index)));
      }
      break;
    case BinaryOperator::bit_xor:
      if (exact_logic9) {
        result.set_logic9(
            index,
            logic_xor(
                lhs.get_logic9(index),
                rhs.get_logic9(index)));
      } else {
        result.set(
            index, logic_xor(lhs.get(index), rhs.get(index)));
      }
      break;
    case BinaryOperator::add_unsigned:
    case BinaryOperator::subtract_unsigned:
    case BinaryOperator::multiply_unsigned:
    case BinaryOperator::power_unsigned:
    case BinaryOperator::divide_unsigned:
    case BinaryOperator::modulo_unsigned:
    case BinaryOperator::add_signed:
    case BinaryOperator::subtract_signed:
    case BinaryOperator::multiply_signed:
    case BinaryOperator::power_signed:
    case BinaryOperator::divide_signed:
    case BinaryOperator::remainder_signed:
    case BinaryOperator::modulo_signed:
    case BinaryOperator::equal:
    case BinaryOperator::case_equal:
    case BinaryOperator::casez_equal:
    case BinaryOperator::casex_equal:
    case BinaryOperator::wildcard_equal:
    case BinaryOperator::vhdl_match_equal:
    case BinaryOperator::not_equal:
    case BinaryOperator::less_unsigned:
    case BinaryOperator::less_equal_unsigned:
    case BinaryOperator::greater_unsigned:
    case BinaryOperator::greater_equal_unsigned:
    case BinaryOperator::less_signed:
    case BinaryOperator::less_equal_signed:
    case BinaryOperator::greater_signed:
    case BinaryOperator::greater_equal_signed:
      break;
    }
  }
  return result;
}

[[nodiscard]] std::int64_t checked_integer_operand(
    const PackedLogic4& value) {
  if ((value.width() != 32 && value.width() != 64)
      || has_unknown(value)) {
    throw std::invalid_argument(
        "VHDL integer operand contains an unknown or "
        "high-impedance value");
  }
  const auto converted = value.known_signed_value();
  if (!converted) {
    throw std::invalid_argument(
        "VHDL integer operand contains an unknown or "
        "high-impedance value");
  }
  return *converted;
}

[[nodiscard]] std::int64_t integer_minimum(const std::size_t width) {
  return width == 64 ? std::numeric_limits<std::int64_t>::min()
                     : std::numeric_limits<std::int32_t>::min();
}

[[nodiscard]] std::int64_t integer_maximum(const std::size_t width) {
  return width == 64 ? std::numeric_limits<std::int64_t>::max()
                     : std::numeric_limits<std::int32_t>::max();
}

[[nodiscard]] bool checked_add_integer(
    const std::int64_t lhs,
    const std::int64_t rhs,
    const std::int64_t minimum,
    const std::int64_t maximum,
    std::int64_t& result) {
  if ((rhs > 0 && lhs > maximum - rhs)
      || (rhs < 0 && lhs < minimum - rhs)) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

[[nodiscard]] bool checked_subtract_integer(
    const std::int64_t lhs,
    const std::int64_t rhs,
    const std::int64_t minimum,
    const std::int64_t maximum,
    std::int64_t& result) {
  if ((rhs < 0 && lhs > maximum + rhs)
      || (rhs > 0 && lhs < minimum + rhs)) {
    return false;
  }
  result = lhs - rhs;
  return true;
}

[[nodiscard]] bool checked_multiply_integer(
    const std::int64_t lhs,
    const std::int64_t rhs,
    const std::int64_t minimum,
    const std::int64_t maximum,
    std::int64_t& result) {
  if (lhs == 0 || rhs == 0) {
    result = 0;
    return true;
  }
  if ((lhs == -1 && rhs == minimum)
      || (rhs == -1 && lhs == minimum)) {
    return false;
  }
  const bool overflow = lhs > 0
      ? (rhs > 0 ? lhs > maximum / rhs : rhs < minimum / lhs)
      : (rhs > 0 ? lhs < minimum / rhs : lhs < maximum / rhs);
  if (overflow) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

[[nodiscard]] PackedLogic4 packed_integer(
    const std::int64_t value,
    const std::size_t width) {
  if ((width != 32 && width != 64)
      || value < integer_minimum(width)
      || value > integer_maximum(width)) {
    throw std::invalid_argument("VHDL integer arithmetic overflow");
  }
  return PackedLogic4::from_aval_bval(
      width, static_cast<std::uint64_t>(value), 0);
}

[[nodiscard]] PackedLogic4 integer_unary_value(
    const IntegerUnaryOperator operation,
    const PackedLogic4& source) {
  const auto value =
      checked_integer_operand(source);
  if (value == integer_minimum(source.width())) {
    throw std::invalid_argument("VHDL integer arithmetic overflow");
  }
  switch (operation) {
  case IntegerUnaryOperator::negate:
    return packed_integer(-value, source.width());
  case IntegerUnaryOperator::absolute:
    return packed_integer(value < 0 ? -value : value, source.width());
  }
  throw std::invalid_argument("unknown VHDL integer unary operation");
}

[[nodiscard]] PackedLogic4 integer_binary_value(
    const IntegerBinaryOperator operation,
    const PackedLogic4& lhs_value,
    const PackedLogic4& rhs_value) {
  if (lhs_value.width() != rhs_value.width()) {
    throw std::invalid_argument("VHDL integer operand widths differ");
  }
  const auto width = lhs_value.width();
  const auto minimum = integer_minimum(width);
  const auto maximum = integer_maximum(width);
  const auto lhs = checked_integer_operand(lhs_value);
  const auto rhs = checked_integer_operand(rhs_value);
  std::int64_t result = 0;
  switch (operation) {
  case IntegerBinaryOperator::add:
    if (!checked_add_integer(lhs, rhs, minimum, maximum, result)) {
      throw std::invalid_argument("VHDL integer arithmetic overflow");
    }
    return packed_integer(result, width);
  case IntegerBinaryOperator::subtract:
    if (!checked_subtract_integer(lhs, rhs, minimum, maximum, result)) {
      throw std::invalid_argument("VHDL integer arithmetic overflow");
    }
    return packed_integer(result, width);
  case IntegerBinaryOperator::multiply:
    if (!checked_multiply_integer(lhs, rhs, minimum, maximum, result)) {
      throw std::invalid_argument("VHDL integer arithmetic overflow");
    }
    return packed_integer(result, width);
  case IntegerBinaryOperator::power: {
    if (rhs < 0) {
      throw std::invalid_argument(
          "VHDL integer exponent must be nonnegative");
    }
    result = 1;
    auto factor = lhs;
    auto exponent = static_cast<std::uint64_t>(rhs);
    while (exponent != 0) {
      if ((exponent & 1U) != 0) {
        if (!checked_multiply_integer(
                result, factor, minimum, maximum, result)) {
          throw std::invalid_argument("VHDL integer arithmetic overflow");
        }
      }
      exponent >>= 1U;
      if (exponent != 0) {
        if (!checked_multiply_integer(
                factor, factor, minimum, maximum, factor)) {
          throw std::invalid_argument("VHDL integer arithmetic overflow");
        }
      }
    }
    return packed_integer(result, width);
  }
  case IntegerBinaryOperator::divide:
  case IntegerBinaryOperator::remainder:
  case IntegerBinaryOperator::modulo:
    if (rhs == 0) {
      throw std::invalid_argument("VHDL integer division by zero");
    }
    if (lhs == minimum && rhs == -1) {
      throw std::invalid_argument("VHDL integer arithmetic overflow");
    }
    if (operation == IntegerBinaryOperator::divide) {
      return packed_integer(lhs / rhs, width);
    }
    {
      result = lhs % rhs;
      if (operation == IntegerBinaryOperator::modulo
          && result != 0 && ((result < 0) != (rhs < 0))) {
        result += rhs;
      }
      return packed_integer(result, width);
    }
  }
  throw std::invalid_argument("unknown VHDL integer binary operation");
}

void check_integer_range(
    const PackedLogic4& source,
    const std::int64_t lower,
    const std::int64_t upper) {
  const auto value = checked_integer_operand(source);
  if (value < lower || value > upper) {
    throw std::invalid_argument(
        "VHDL integer subtype range check failed");
  }
}

[[nodiscard]] PackedLogic4 conditional_value(
    const PackedLogic4& condition,
    const PackedLogic4& when_true,
    const PackedLogic4& when_false) {
  if (condition.width() != 1) {
    throw std::invalid_argument(
        "conditional-select condition is not scalar");
  }
  if (when_true.width() != when_false.width()) {
    throw std::invalid_argument(
        "conditional-select values have different widths");
  }
  if (condition.get(0) == Logic4::one) {
    return when_true;
  }
  if (condition.get(0) == Logic4::zero) {
    return when_false;
  }
  PackedLogic4 result(when_true.width(), Logic4::x);
  for (std::size_t index = 0; index < result.width(); ++index) {
    if (when_true.get(index) == when_false.get(index)) {
      result.set(index, when_true.get(index));
    }
  }
  return result;
}



} // namespace fsim::runtime::simir
