// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/simir.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::runtime::simir {
namespace {

[[nodiscard]] std::string error_text(ProcessId process,
                                     InstructionIndex instruction,
                                     const std::string &message) {
  std::ostringstream result;
  result << "SimIR process " << process << ", instruction " << instruction
         << ": " << message;
  return result.str();
}

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;

[[nodiscard]] std::string format_output_value(
    const PackedLogic4& value,
    const OutputFormat format,
    const bool signed_decimal,
    const bool suppress_leading_zero) {
  const auto maybe_suppress_leading_zero =
      [suppress_leading_zero](std::string text) {
        if (!suppress_leading_zero || text.size() <= 1U) {
          return text;
        }
        const auto first = text.find_first_not_of('0');
        if (first == std::string::npos) {
          return std::string{"0"};
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
    constexpr std::string_view digits{"0123456789abcdef"};
    const auto digit_count = (value.width() + 3U) / 4U;
    std::string text(digit_count, '0');
    for (std::size_t digit = 0; digit < digit_count; ++digit) {
      const auto offset = digit * 4U;
      const auto bit_count =
          std::min<std::size_t>(4U, value.width() - offset);
      unsigned known_value{};
      bool all_x = true;
      bool all_z = true;
      bool has_unknown = false;
      for (std::size_t bit = 0; bit < bit_count; ++bit) {
        const auto state = value.get(offset + bit);
        all_x = all_x && state == Logic4::x;
        all_z = all_z && state == Logic4::z;
        has_unknown =
            has_unknown || state == Logic4::x || state == Logic4::z;
        if (state == Logic4::one) {
          known_value |= 1U << bit;
        }
      }
      const char character =
          all_x ? 'x'
          : all_z ? 'z'
          : has_unknown ? 'x'
                        : digits[known_value];
      text[digit_count - digit - 1U] = character;
    }
    return maybe_suppress_leading_zero(std::move(text));
  }
  case OutputFormat::octal: {
    constexpr std::string_view digits{"01234567"};
    const auto digit_count = (value.width() + 2U) / 3U;
    std::string text(digit_count, '0');
    for (std::size_t digit = 0; digit < digit_count; ++digit) {
      const auto offset = digit * 3U;
      const auto bit_count =
          std::min<std::size_t>(3U, value.width() - offset);
      unsigned known_value{};
      bool all_x = true;
      bool all_z = true;
      bool has_unknown = false;
      for (std::size_t bit = 0; bit < bit_count; ++bit) {
        const auto state = value.get(offset + bit);
        all_x = all_x && state == Logic4::x;
        all_z = all_z && state == Logic4::z;
        has_unknown =
            has_unknown || state == Logic4::x || state == Logic4::z;
        if (state == Logic4::one) {
          known_value |= 1U << bit;
        }
      }
      const char character =
          all_x ? 'x'
          : all_z ? 'z'
          : has_unknown ? 'x'
                        : digits[known_value];
      text[digit_count - digit - 1U] = character;
    }
    return maybe_suppress_leading_zero(std::move(text));
  }
  case OutputFormat::decimal: {
    for (std::size_t bit = 0; bit < value.width(); ++bit) {
      const auto state = value.get(bit);
      if (state == Logic4::x || state == Logic4::z) {
        return "x";
      }
    }
    auto magnitude = value;
    bool negative =
        signed_decimal
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
    std::string text{"0"};
    for (std::size_t bit = magnitude.width(); bit-- > 0;) {
      unsigned carry =
          magnitude.get(bit) == Logic4::one ? 1U : 0U;
      for (std::size_t digit = text.size(); digit-- > 0;) {
        const auto value_digit =
            static_cast<unsigned>(text[digit] - '0') * 2U
            + carry;
        text[digit] =
            static_cast<char>('0' + (value_digit % 10U));
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
    unsigned character{};
    const auto bit_count =
        std::min<std::size_t>(8U, value.width());
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
    std::string text;
    text.reserve(byte_count);
    bool leading_padding = true;
    for (std::size_t byte = byte_count; byte-- > 0;) {
      const auto offset = byte * 8U;
      const auto bit_count =
          std::min<std::size_t>(8U, value.width() - offset);
      unsigned character{};
      bool unknown = false;
      for (std::size_t bit = 0; bit < bit_count; ++bit) {
        const auto state = value.get(offset + bit);
        unknown =
            unknown || state == Logic4::x || state == Logic4::z;
        if (state == Logic4::one) {
          character |= 1U << bit;
        }
      }
      if (unknown) {
        text.push_back('x');
        leading_padding = false;
      } else if (character != 0U || !leading_padding) {
        text.push_back(static_cast<char>(character));
        leading_padding = false;
      }
    }
    return text;
  }
  }
  throw std::logic_error{"invalid formatted-output conversion"};
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
    const bool zero_pad) {
  auto formatted =
      format_output_value(
          value, format, signed_decimal, suppress_leading_zero);
  if (formatted.size() < minimum_width) {
    const auto padding =
        static_cast<std::size_t>(minimum_width) - formatted.size();
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

[[nodiscard]] std::string make_time_output(
    const std::string_view prefix,
    const std::string_view suffix,
    const SimulationTick tick,
    const std::uint32_t minimum_width,
    const bool left_justify,
    const bool zero_pad) {
  auto formatted = std::to_string(tick);
  if (formatted.size() < minimum_width) {
    const auto padding =
        static_cast<std::size_t>(minimum_width) - formatted.size();
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
                                Logic4 new_value) noexcept {
  if (old_value == new_value) {
    return false;
  }
  if (edge == EdgeKind::any) {
    return true;
  }
  if (edge == EdgeKind::posedge) {
    return (old_value == Logic4::zero &&
            (new_value == Logic4::one || new_value == Logic4::x ||
             new_value == Logic4::z)) ||
           ((old_value == Logic4::x || old_value == Logic4::z) &&
            new_value == Logic4::one);
  }
  return (old_value == Logic4::one &&
          (new_value == Logic4::zero || new_value == Logic4::x ||
           new_value == Logic4::z)) ||
         ((old_value == Logic4::x || old_value == Logic4::z) &&
          new_value == Logic4::zero);
}

[[nodiscard]] PackedLogic4 unary_not(const PackedLogic4 &source) {
  PackedLogic4 result(source.width(), Logic4::x);
  for (std::size_t index = 0; index < source.width(); ++index) {
    result.set(index, logic_not(source.get(index)));
  }
  return result;
}

[[nodiscard]] Logic4 truth_value(const PackedLogic4& source) {
  bool has_unknown = false;
  for (std::size_t index = 0; index < source.width(); ++index) {
    const auto value = source.get(index);
    if (value == Logic4::one) {
      return Logic4::one;
    }
    has_unknown =
        has_unknown || value == Logic4::x || value == Logic4::z;
  }
  return has_unknown ? Logic4::x : Logic4::zero;
}

[[nodiscard]] PackedLogic4 logical_not(const PackedLogic4& source) {
  return PackedLogic4(1, logic_not(truth_value(source)));
}

[[nodiscard]] PackedLogic4 logical_binary(
    const LogicalBinaryOperator operation,
    const PackedLogic4& lhs,
    const PackedLogic4& rhs) {
  const auto left = truth_value(lhs);
  const auto right = truth_value(rhs);
  return PackedLogic4(
      1,
      operation == LogicalBinaryOperator::logical_and
          ? logic_and(left, right)
          : logic_or(left, right));
}

[[nodiscard]] PackedLogic4 reduce_value(
    const ReductionOperator operation,
    const PackedLogic4& source) {
  if (operation == ReductionOperator::one_hot
      || operation
          == ReductionOperator::one_hot_or_zero) {
    std::size_t one_count = 0;
    for (std::size_t index = 0;
         index < source.width() && one_count < 2;
         ++index) {
      if (source.get(index) == Logic4::one) {
        ++one_count;
      }
    }
    const auto matched =
        operation == ReductionOperator::one_hot
            ? one_count == 1
            : one_count <= 1;
    return PackedLogic4(
        1, matched ? Logic4::one : Logic4::zero);
  }
  auto result =
      operation == ReductionOperator::bit_and
          ? Logic4::one
          : Logic4::zero;
  for (std::size_t index = 0; index < source.width(); ++index) {
    if (operation == ReductionOperator::bit_and) {
      result = logic_and(result, source.get(index));
    } else if (operation == ReductionOperator::bit_or) {
      result = logic_or(result, source.get(index));
    } else {
      result = logic_xor(result, source.get(index));
    }
  }
  return PackedLogic4(1, result);
}

[[nodiscard]] PackedLogic4 count_ones_value(
    const PackedLogic4& source) {
  std::uint32_t count = 0;
  for (std::size_t index = 0; index < source.width(); ++index) {
    if (source.get(index) == Logic4::one) {
      ++count;
    }
  }
  PackedLogic4 result(32, Logic4::zero);
  for (std::size_t bit = 0; bit < 32; ++bit) {
    result.set(
        bit,
        ((count >> bit) & 1U) != 0
            ? Logic4::one
            : Logic4::zero);
  }
  return result;
}

[[nodiscard]] PackedLogic4 count_bits_value(
    const PackedLogic4& source,
    const std::uint8_t state_mask) {
  std::uint32_t count = 0;
  for (std::size_t index = 0; index < source.width(); ++index) {
    const auto state =
        static_cast<std::uint8_t>(source.get(index));
    if ((state_mask & (std::uint8_t{1} << state)) != 0) {
      ++count;
    }
  }
  PackedLogic4 result(32, Logic4::zero);
  for (std::size_t bit = 0; bit < 32; ++bit) {
    result.set(
        bit,
        ((count >> bit) & 1U) != 0
            ? Logic4::one
            : Logic4::zero);
  }
  return result;
}

[[nodiscard]] PackedLogic4 shift_value(
    const ShiftOperator operation,
    const PackedLogic4& value,
    const PackedLogic4& amount_value) {
  const auto rotating =
      operation == ShiftOperator::rotate_left
      || operation == ShiftOperator::rotate_right;
  std::size_t amount = 0;
  std::size_t rotate_bit = 1U % value.width();
  for (std::size_t index = 0; index < amount_value.width(); ++index) {
    const auto bit = amount_value.get(index);
    if (bit == Logic4::x || bit == Logic4::z) {
      return PackedLogic4(value.width(), Logic4::x);
    }
    if (rotating) {
      if (bit == Logic4::one) {
        amount =
            amount >= value.width() - rotate_bit
                ? amount - (value.width() - rotate_bit)
                : amount + rotate_bit;
      }
      rotate_bit =
          rotate_bit >= value.width() - rotate_bit
              ? rotate_bit - (value.width() - rotate_bit)
              : rotate_bit + rotate_bit;
    } else if (bit == Logic4::one) {
      if (index >= std::numeric_limits<std::size_t>::digits
          || (std::size_t{1} << index) >= value.width()) {
        amount = value.width();
        break;
      }
      amount |= std::size_t{1} << index;
    }
  }
  const auto fill =
      operation == ShiftOperator::arithmetic_right
          ? value.get(value.width() - 1U)
          : operation == ShiftOperator::arithmetic_left
              ? value.get(0)
          : Logic4::zero;
  PackedLogic4 result(value.width(), fill);
  if (amount >= value.width()) {
    return result;
  }
  for (std::size_t index = 0; index < value.width(); ++index) {
    if (operation == ShiftOperator::rotate_left) {
      result.set(
          index,
          value.get((index + value.width() - amount) % value.width()));
    } else if (operation == ShiftOperator::rotate_right) {
      result.set(
          index,
          value.get((index + amount) % value.width()));
    } else if (
        operation == ShiftOperator::logical_left
        || operation == ShiftOperator::arithmetic_left) {
      if (index >= amount) {
        result.set(index, value.get(index - amount));
      }
    } else if (index + amount < value.width()) {
      result.set(index, value.get(index + amount));
    }
  }
  return result;
}

[[nodiscard]] PackedLogic4 extract_value(
    const PackedLogic4& source,
    const std::size_t offset,
    const std::size_t width) {
  if (width == 0 || offset > source.width()
      || width > source.width() - offset) {
    throw std::invalid_argument(
        "extract range is outside its source value");
  }
  PackedLogic4 result(width, Logic4::zero);
  for (std::size_t bit = 0; bit < width; ++bit) {
    result.set(bit, source.get(offset + bit));
  }
  return result;
}

[[nodiscard]] PackedLogic4 insert_value(
    PackedLogic4 target,
    const PackedLogic4& source,
    const std::size_t offset) {
  if (source.width() == 0 || offset > target.width()
      || source.width() > target.width() - offset) {
    throw std::invalid_argument(
        "insert range is outside its target value");
  }
  for (std::size_t bit = 0; bit < source.width(); ++bit) {
    target.set(offset + bit, source.get(bit));
  }
  return target;
}

[[nodiscard]] PackedLogic4 concatenate_values(
    const std::vector<PackedLogic4>& operands,
    const std::size_t expected_width) {
  if (operands.empty()) {
    throw std::invalid_argument(
        "concatenation requires at least one operand");
  }
  std::size_t width = 0;
  for (const auto& operand : operands) {
    if (operand.width()
        > std::numeric_limits<std::size_t>::max() - width) {
      throw std::invalid_argument("concatenation width overflows");
    }
    width += operand.width();
  }
  if (width == 0 || width != expected_width) {
    throw std::invalid_argument(
        "concatenation operand widths do not match its result width");
  }
  PackedLogic4 result(width, Logic4::zero);
  std::size_t offset = 0;
  for (auto operand = operands.rbegin();
       operand != operands.rend(); ++operand) {
    for (std::size_t bit = 0; bit < operand->width(); ++bit) {
      result.set(offset + bit, operand->get(bit));
    }
    offset += operand->width();
  }
  return result;
}

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

struct SignedDivision {
  PackedLogic4 quotient;
  PackedLogic4 remainder;
};

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
    throw std::invalid_argument("binary operands have different widths");
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
      break;
    }
    return PackedLogic4(
        1, result ? Logic4::one : Logic4::zero);
  }

  PackedLogic4 result(lhs.width(), Logic4::zero);
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
      result.set(index, logic_and(lhs.get(index), rhs.get(index)));
      break;
    case BinaryOperator::bit_or:
      result.set(index, logic_or(lhs.get(index), rhs.get(index)));
      break;
    case BinaryOperator::bit_xor:
      result.set(index, logic_xor(lhs.get(index), rhs.get(index)));
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

} // namespace

InterpreterError::InterpreterError(ProcessId process,
                                   InstructionIndex instruction,
                                   std::string message)
    : std::runtime_error(error_text(process, instruction, message)),
      process_(process), instruction_(instruction) {}

AssertionError::AssertionError(ProcessId process,
                               InstructionIndex instruction,
                               std::string message,
                               AssertionSeverity severity,
                               SourceLocation source,
                               const bool reported)
    : InterpreterError(
          process, instruction,
          message.empty() ? "assertion failed" : std::move(message)),
      severity_(severity), source_(std::move(source)),
      reported_(reported) {}

struct Interpreter::Impl {
  struct ExecutionContext;

  struct ProcessState {
    Process program;
    InstructionIndex pc{};
    std::vector<PackedLogic4> registers;
    std::unique_ptr<ProcessExecutor> executor;
    std::vector<Sensitivity> dynamic_sensitivity;
    std::vector<bool> dynamic_triggered;
    SourceLocation current_source;
    bool queued{};
    bool waiting_on_static{};
    bool waiting_on_signal{};
    bool dynamic_wait_all{};
    std::optional<InstructionIndex> wait_timeout_origin;
    std::optional<SimulationTick> wait_timeout_deadline;
    std::optional<RegisterId> wait_timeout_result;
    std::uint64_t wait_timeout_generation{};
    std::uint64_t random_state{};
    bool halted{};
  };

  struct Fanout {
    ProcessId process{};
    EdgeKind edge = EdgeKind::any;
  };

  struct PendingUpdate {
    SignalId signal{};
    std::optional<std::size_t> offset;
    PackedLogic4 value;
  };

  enum class PendingEventKind : std::uint8_t {
    none,
    delta,
    timed,
  };

  struct EventState {
    PendingEventKind kind{PendingEventKind::none};
    SimulationTick due{};
    std::uint64_t generation{};
  };

  explicit Impl(
      SchedulerOptions options,
      const std::uint64_t seed)
      : scheduler(options), root_seed(seed) {}

  Scheduler scheduler;
  std::uint64_t root_seed{1};
  std::vector<Signal> signals;
  std::vector<PackedLogic4> driven_values;
  std::vector<PackedLogic4> signal_last_values;
  std::vector<std::optional<PackedLogic4>> forced_values;
  std::vector<ProcessState> processes;
  std::vector<std::vector<Fanout>> static_fanout;
  std::vector<std::vector<Fanout>> dynamic_fanout;
  std::vector<EventState> event_states;
  std::vector<std::optional<std::pair<
      SimulationTick, std::uint64_t>>> signal_events;
  std::vector<std::optional<std::pair<
      SimulationTick, std::uint64_t>>> signal_transactions;
  std::vector<PendingUpdate> pending_updates;
  std::unordered_set<std::uint64_t> pending_channel_updates;
  SignalChangeHook signal_change_hook;
  ExecutionPointHook execution_point_hook;
  OutputHook output_hook;
  ReportHook report_hook;
  std::optional<MonitorInstall> monitor;
  ProcessId monitor_process{};
  bool monitor_enabled{true};
  std::uint64_t monitor_generation{};
  std::optional<std::pair<SimulationTick, std::uint64_t>>
      monitor_publication;
  bool update_commit_scheduled{};
  bool started{};
  bool stopped_by_design{};
  bool finals_ran{};

  [[nodiscard]] Signal &get_signal(SignalId id) {
    if (id >= signals.size()) {
      throw std::out_of_range("invalid SimIR signal ID");
    }
    return signals[id];
  }

  [[nodiscard]] const Signal &get_signal(SignalId id) const {
    if (id >= signals.size()) {
      throw std::out_of_range("invalid SimIR signal ID");
    }
    return signals[id];
  }

  [[nodiscard]] ProcessState &get_process(ProcessId id) {
    if (id >= processes.size()) {
      throw std::out_of_range("invalid SimIR process ID");
    }
    return processes[id];
  }

  [[nodiscard]] PackedLogic4 &get_register(ProcessState &process,
                                           RegisterId id) {
    if (id >= process.registers.size()) {
      throw InterpreterError(process.program.id, process.pc,
                             "invalid register ID");
    }
    return process.registers[id];
  }

  void remove_dynamic_wait(ProcessState &process) {
    if (!process.waiting_on_signal) {
      return;
    }
    for (const auto sensitivity : process.dynamic_sensitivity) {
      auto &fanout = dynamic_fanout[sensitivity.signal];
      fanout.erase(
          std::remove_if(
              fanout.begin(), fanout.end(),
              [&](const Fanout& entry) {
                return entry.process == process.program.id;
              }),
          fanout.end());
    }
    process.dynamic_sensitivity.clear();
    process.dynamic_triggered.clear();
    process.waiting_on_signal = false;
    process.dynamic_wait_all = false;
  }

  void write_process_register(
      ProcessState& process,
      const RegisterId destination,
      const PackedLogic4& value) {
    if (process.executor) {
      process.executor->write_register(
          destination, value);
      return;
    }
    get_register(process, destination) = value;
  }

  void clear_wait_timeout(ProcessState& process) {
    if (!process.wait_timeout_origin) {
      return;
    }
    if (process.wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
      fail(process, "wait timeout generation overflow");
    }
    ++process.wait_timeout_generation;
    process.wait_timeout_origin.reset();
    process.wait_timeout_deadline.reset();
    process.wait_timeout_result.reset();
  }

  void set_wait_timeout_result(
      ProcessState& process,
      const bool timed_out) {
    if (!process.wait_timeout_result) {
      return;
    }
    write_process_register(
        process,
        *process.wait_timeout_result,
        PackedLogic4::from_msb_string(
            timed_out ? "1" : "0"));
  }

  void begin_wait_timeout(
      ProcessState& process,
      const InstructionIndex origin,
      const SimulationTick delay,
      const std::optional<RegisterId> result) {
    clear_wait_timeout(process);
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
      process.pc = origin;
      fail(process, "simulation time overflow in WaitOn timeout");
    }
    if (process.wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
      process.pc = origin;
      fail(process, "wait timeout generation overflow");
    }
    const auto generation =
        ++process.wait_timeout_generation;
    const auto deadline = scheduler.now() + delay;
    process.wait_timeout_origin = origin;
    process.wait_timeout_deadline = deadline;
    process.wait_timeout_result = result;
    set_wait_timeout_result(process, false);

    auto callback =
        [this, id = process.program.id, origin, generation](
            Scheduler&) {
          auto& state = get_process(id);
          if (state.wait_timeout_generation != generation
              || state.wait_timeout_origin
                  != std::optional{origin}) {
            return;
          }
          set_wait_timeout_result(state, true);
          state.wait_timeout_origin.reset();
          state.wait_timeout_deadline.reset();
          state.wait_timeout_result.reset();
          queue_active_current(id);
        };
    if (delay == 0) {
      scheduler.schedule(
          SchedulerPhase::inactive,
          process.program.id,
          std::move(callback));
    } else {
      scheduler.schedule_at(
          deadline,
          SchedulerPhase::active,
          process.program.id,
          std::move(callback));
    }
  }

  void rearm_wait_timeout(
      ProcessState& process,
      const InstructionIndex instruction,
      const InstructionIndex origin,
      const std::optional<RegisterId> result) {
    if (process.wait_timeout_origin
            != std::optional{origin}
        || !process.wait_timeout_deadline) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn timeout rearm has no matching active deadline");
    }
    if (process.wait_timeout_result != result) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn timeout rearm result register mismatch");
    }
    if (*process.wait_timeout_deadline < scheduler.now()) {
      process.pc = instruction;
      fail(process, "WaitOn timeout deadline was missed");
    }
  }

  void mark_dynamic_event_resume(
      ProcessState& process) {
    const auto timed_out =
        process.wait_timeout_deadline
        && *process.wait_timeout_deadline <= scheduler.now();
    set_wait_timeout_result(process, timed_out);
  }

  [[nodiscard]] bool dynamic_wait_satisfied(
      ProcessState& process,
      const SignalId signal,
      const EdgeKind edge) {
    if (!process.dynamic_wait_all) {
      return true;
    }
    for (std::size_t index = 0;
         index < process.dynamic_sensitivity.size(); ++index) {
      const auto& sensitivity =
          process.dynamic_sensitivity[index];
      if (sensitivity.signal == signal
          && sensitivity.edge == edge) {
        process.dynamic_triggered[index] = true;
      }
    }
    return std::all_of(
        process.dynamic_triggered.begin(),
        process.dynamic_triggered.end(),
        [](const bool triggered) { return triggered; });
  }

  void handle_boundary(ProcessState& process,
                       InstructionIndex instruction,
                       InstructionIndex next_instruction);
  void handle_external_boundary(
      ProcessState& process,
      InstructionIndex instruction,
      InstructionIndex next_instruction,
      const ExternalSuspension& suspension);
  void request_channel_update(
      ProcessId process, std::uint64_t channel);
  void execute(ProcessId id);

  void queue_at(ProcessId id, SimulationTick time) {
    auto &process = get_process(id);
    if (process.halted || process.queued) {
      return;
    }
    process.queued = true;
    scheduler.schedule_at(
        time, SchedulerPhase::active, id,
        [this, id](Scheduler &) {
          auto &state = get_process(id);
          state.queued = false;
          state.waiting_on_static = false;
          remove_dynamic_wait(state);
          execute(id);
        });
  }

  void queue_next_delta(ProcessId id) {
    auto &process = get_process(id);
    if (process.halted || process.queued) {
      return;
    }
    process.queued = true;
    scheduler.schedule_next_delta(
        SchedulerPhase::active, id,
        [this, id](Scheduler &) {
          auto &state = get_process(id);
          state.queued = false;
          state.waiting_on_static = false;
          remove_dynamic_wait(state);
          execute(id);
        });
  }

  void queue_current(ProcessId id) {
    auto& process = get_process(id);
    if (process.halted || process.queued) {
      return;
    }
    process.queued = true;
    const auto phase =
        scheduler.current_phase().value_or(SchedulerPhase::active);
    scheduler.schedule(
        phase, id,
        [this, id](Scheduler&) {
          auto& state = get_process(id);
          state.queued = false;
          execute(id);
        });
  }

  void queue_active_current(ProcessId id) {
    auto& process = get_process(id);
    if (process.halted || process.queued) {
      return;
    }
    process.queued = true;
    scheduler.schedule(
        SchedulerPhase::active, id,
        [this, id](Scheduler&) {
          auto& state = get_process(id);
          state.queued = false;
          state.waiting_on_static = false;
          remove_dynamic_wait(state);
          execute(id);
        });
  }

  void trigger_event(const SignalId event) {
    (void)get_signal(event);
    for (const auto& sensitivity : static_fanout[event]) {
      auto& process = get_process(sensitivity.process);
      if (process.waiting_on_static) {
        queue_active_current(sensitivity.process);
      }
    }
    // Copy because queue_active_current removes dynamic registrations.
    const auto dynamic = dynamic_fanout[event];
    for (const auto& sensitivity : dynamic) {
      auto& process = get_process(sensitivity.process);
      if (dynamic_wait_satisfied(
              process, event, sensitivity.edge)) {
        mark_dynamic_event_resume(process);
        queue_active_current(sensitivity.process);
      }
    }
  }

  [[nodiscard]] std::uint64_t invalidate_event(
      const SignalId event) {
    (void)get_signal(event);
    auto& state = event_states[event];
    if (state.generation
        == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error{
          "event notification generation overflow"};
    }
    state.kind = PendingEventKind::none;
    state.due = 0;
    return ++state.generation;
  }

  void cancel_event(const SignalId event) {
    (void)invalidate_event(event);
  }

  void notify_event(
      const SignalId event,
      const SimulationTick delay,
      const EventNotificationKind kind,
      const StableOrder order) {
    (void)get_signal(event);
    auto& state = event_states[event];
    auto effective_kind = kind;

    if (kind == EventNotificationKind::delayed) {
      if (state.kind != PendingEventKind::none) {
        throw std::logic_error{
            "notify_delayed requires an event with no pending notification"};
      }
      effective_kind =
          delay == 0
              ? EventNotificationKind::delta
              : EventNotificationKind::timed;
    }

    if (effective_kind == EventNotificationKind::immediate) {
      if (delay != 0) {
        throw std::invalid_argument{
            "immediate event notification cannot have a delay"};
      }
      (void)invalidate_event(event);
      trigger_event(event);
      return;
    }

    if (effective_kind == EventNotificationKind::delta) {
      if (delay != 0) {
        throw std::invalid_argument{
            "delta event notification cannot have a delay"};
      }
      if (state.kind == PendingEventKind::delta) {
        return;
      }
      const auto generation = invalidate_event(event);
      state.kind = PendingEventKind::delta;
      state.due = scheduler.now();
      scheduler.schedule_next_delta(
          SchedulerPhase::active,
          order,
          [this, event, generation](Scheduler&) {
            auto& pending = event_states[event];
            if (pending.generation != generation
                || pending.kind != PendingEventKind::delta) {
              return;
            }
            pending.kind = PendingEventKind::none;
            pending.due = 0;
            trigger_event(event);
          });
      return;
    }

    if (effective_kind != EventNotificationKind::timed || delay == 0) {
      throw std::invalid_argument{
          "timed event notification requires a non-zero delay"};
    }
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
      throw std::overflow_error{
          "simulation time overflow while scheduling event notification"};
    }
    const auto due = scheduler.now() + delay;
    if (state.kind == PendingEventKind::delta
        || (state.kind == PendingEventKind::timed
            && state.due <= due)) {
      return;
    }
    const auto generation = invalidate_event(event);
    state.kind = PendingEventKind::timed;
    state.due = due;
    scheduler.schedule_at(
        due,
        SchedulerPhase::active,
        order,
        [this, event, generation, due](Scheduler&) {
          auto& pending = event_states[event];
          if (pending.generation != generation
              || pending.kind != PendingEventKind::timed
              || pending.due != due) {
            return;
          }
          pending.kind = PendingEventKind::none;
          pending.due = 0;
          trigger_event(event);
        });
  }

  void notify_execution_point(
      ProcessState& process,
      const InstructionIndex instruction,
      const ExecutionPointKind kind,
      const SourceLocation& source) {
    if (execution_point_hook) {
      execution_point_hook(
          scheduler,
          ExecutionPoint{
              process.program.id, instruction, kind, source});
    }
  }

  [[nodiscard]] bool monitor_watches(
      const SignalId signal) const {
    return monitor
        && std::ranges::any_of(
            monitor->values,
            [signal](const MonitorValue& value) {
              return value.kind == MonitorValueKind::signal
                  && value.signal == signal;
            });
  }

  [[nodiscard]] std::string render_monitor() const {
    if (!monitor) {
      return {};
    }
    std::string text;
    for (const auto& value : monitor->values) {
      if (value.kind == MonitorValueKind::time) {
        text += make_time_output(
            value.prefix,
            {},
            scheduler.now(),
            value.minimum_width,
            value.left_justify,
            value.zero_pad);
      } else {
        text += make_formatted_output(
            value.prefix,
            {},
            value.format,
            get_signal(value.signal).initial_value,
            value.signed_decimal,
            value.suppress_leading_zero,
            value.minimum_width,
            value.left_justify,
            value.zero_pad);
      }
    }
    text += monitor->trailing_text;
    return text;
  }

  void schedule_monitor_publication() {
    if (!monitor || !monitor_enabled) {
      return;
    }
    const auto publication =
        std::pair{scheduler.now(), scheduler.delta()};
    if (monitor_publication == publication) {
      return;
    }
    monitor_publication = publication;
    const auto generation = monitor_generation;
    const auto process = monitor_process;
    scheduler.schedule(
        SchedulerPhase::postponed,
        process,
        [this, generation, process](Scheduler& runtime) {
          if (generation != monitor_generation
              || !monitor_enabled || !monitor) {
            return;
          }
          monitor_publication.reset();
          if (output_hook) {
            output_hook(
                process,
                render_monitor(),
                monitor->newline,
                runtime.now(),
                runtime.delta());
          }
        });
  }

  void install_monitor(
      const ProcessId process,
      const MonitorInstall& registration) {
    if (monitor_generation
        == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error{"monitor generation overflow"};
    }
    ++monitor_generation;
    monitor = registration;
    monitor_process = process;
    monitor_enabled = true;
    monitor_publication.reset();
    schedule_monitor_publication();
  }

  void set_monitor_enabled(const bool enabled) {
    if (monitor_generation
        == std::numeric_limits<std::uint64_t>::max()) {
      throw std::overflow_error{"monitor generation overflow"};
    }
    ++monitor_generation;
    monitor_enabled = enabled;
    monitor_publication.reset();
    if (enabled) {
      schedule_monitor_publication();
    }
  }

  [[nodiscard]] static std::uint64_t initial_random_state(
      const std::uint64_t seed,
      const ProcessId process) noexcept {
    auto value =
        seed
        + UINT64_C(0x9e3779b97f4a7c15)
              * (static_cast<std::uint64_t>(process) + 1U);
    value = (value ^ (value >> 30U))
        * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U))
        * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
  }

  [[nodiscard]] static std::uint32_t next_random(
      ProcessState& process) noexcept {
    auto value =
        (process.random_state += UINT64_C(0x9e3779b97f4a7c15));
    value = (value ^ (value >> 30U))
        * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U))
        * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return static_cast<std::uint32_t>(value >> 32U);
  }

  [[nodiscard]] static std::optional<std::uint32_t>
  known_random_bound(const PackedLogic4& value) {
    if (value.empty()) {
      return std::nullopt;
    }
    std::uint32_t result{};
    const auto width = std::min<std::size_t>(32U, value.width());
    for (std::size_t bit = 0; bit < width; ++bit) {
      const auto state = value.get(bit);
      if (state == Logic4::x || state == Logic4::z) {
        return std::nullopt;
      }
      if (state == Logic4::one) {
        result |= UINT32_C(1) << bit;
      }
    }
    return result;
  }

  [[nodiscard]] PackedLogic4 random_value(
      const ProcessId process_id,
      const RandomKind kind,
      const std::optional<PackedLogic4>& maximum,
      const std::optional<PackedLogic4>& minimum) {
    auto& process = get_process(process_id);
    if (kind != RandomKind::urandom_range) {
      return PackedLogic4::from_aval_bval(
          32, next_random(process), 0);
    }
    if (!maximum) {
      throw std::logic_error{
          "$urandom_range operation has no maximum"};
    }
    const auto known_maximum = known_random_bound(*maximum);
    const auto known_minimum =
        minimum
            ? known_random_bound(*minimum)
            : std::optional<std::uint32_t>{0U};
    if (!known_maximum || !known_minimum) {
      return PackedLogic4(32, Logic4::x);
    }
    auto low = *known_minimum;
    auto high = *known_maximum;
    if (high < low) {
      std::swap(low, high);
    }
    const auto span =
        static_cast<std::uint64_t>(high)
        - static_cast<std::uint64_t>(low) + 1U;
    std::uint32_t sample{};
    if (span == (UINT64_C(1) << 32U)) {
      sample = next_random(process);
    } else {
      const auto full_range = UINT64_C(1) << 32U;
      const auto accepted = full_range - full_range % span;
      do {
        sample = next_random(process);
      } while (static_cast<std::uint64_t>(sample) >= accepted);
      sample = static_cast<std::uint32_t>(
          static_cast<std::uint64_t>(low)
          + static_cast<std::uint64_t>(sample) % span);
    }
    return PackedLogic4::from_aval_bval(32, sample, 0);
  }

  void publish(SignalId signal_id, PackedLogic4 value) {
    auto &signal = get_signal(signal_id);
    if (signal.initial_value.width() != value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    signal_transactions[signal_id] =
        std::pair{scheduler.now(), scheduler.delta() + 1};
    if (signal.initial_value == value) {
      return;
    }
    const auto old_value = signal.initial_value;
    signal_last_values[signal_id] = old_value;
    signal.initial_value = std::move(value);
    signal_events[signal_id] =
        std::pair{scheduler.now(), scheduler.delta() + 1};
    scheduler.note_signal_change(signal_id);
    if (signal_change_hook) {
      signal_change_hook(signal_id, signal.initial_value, scheduler.now());
    }
    if (monitor_watches(signal_id)) {
      schedule_monitor_publication();
    }

    for (const auto &sensitivity : static_fanout[signal_id]) {
      auto &process = get_process(sensitivity.process);
      if (!process.waiting_on_static) {
        continue;
      }
      if (sensitivity.edge != EdgeKind::any &&
          (old_value.width() != 1 ||
           !edge_matches(sensitivity.edge, old_value.get(0),
                         signal.initial_value.get(0)))) {
        continue;
      }
      queue_next_delta(sensitivity.process);
    }
    // Copy because queue_next_delta removes a process from every dynamic list.
    const auto dynamic = dynamic_fanout[signal_id];
    for (const auto sensitivity : dynamic) {
      if (sensitivity.edge != EdgeKind::any
          && (old_value.width() != 1
              || !edge_matches(
                  sensitivity.edge, old_value.get(0),
                  signal.initial_value.get(0)))) {
        continue;
      }
      auto& process = get_process(sensitivity.process);
      if (dynamic_wait_satisfied(
              process, signal_id, sensitivity.edge)) {
        mark_dynamic_event_resume(process);
        queue_next_delta(sensitivity.process);
      }
    }
  }

  void commit(SignalId signal_id, PackedLogic4 value) {
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    driven_values[signal_id] = value;
    if (!forced_values[signal_id].has_value()) {
      publish(signal_id, std::move(value));
    }
  }

  void commit_slice(
      const SignalId signal_id,
      PackedLogic4 value,
      const std::size_t offset) {
    (void)get_signal(signal_id);
    commit(
        signal_id,
        insert_value(
            driven_values[signal_id], value, offset));
  }

  void schedule_update_commit() {
    if (update_commit_scheduled) {
      return;
    }
    update_commit_scheduled = true;
    scheduler.schedule(
        SchedulerPhase::update,
        std::numeric_limits<StableOrder>::max(),
        [this](Scheduler&) {
          std::unordered_map<SignalId, PackedLogic4> coalesced;
          coalesced.reserve(pending_updates.size());
          for (auto& pending : pending_updates) {
            auto entry =
                coalesced
                    .try_emplace(
                        pending.signal,
                        driven_values[pending.signal])
                    .first;
            if (pending.offset) {
              entry->second = insert_value(
                  std::move(entry->second),
                  pending.value,
                  *pending.offset);
            } else {
              entry->second = std::move(pending.value);
            }
          }
          pending_updates.clear();
          update_commit_scheduled = false;
          std::vector<std::pair<SignalId, PackedLogic4>> updates;
          updates.reserve(coalesced.size());
          for (auto& [signal, value] : coalesced) {
            updates.emplace_back(signal, std::move(value));
          }
          std::sort(
              updates.begin(),
              updates.end(),
              [](const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
              });
          for (auto& [signal, value] : updates) {
            commit(signal, std::move(value));
          }
        });
  }

  void stage_update(SignalId signal_id, PackedLogic4 staged_value) {
    (void)get_signal(signal_id);
    if (driven_values[signal_id].width() != staged_value.width()) {
      throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    pending_updates.push_back(PendingUpdate{
        signal_id, std::nullopt, std::move(staged_value)});
    schedule_update_commit();
  }

  void stage_update_slice(
      const SignalId signal_id,
      PackedLogic4 value,
      const std::size_t offset) {
    (void)get_signal(signal_id);
    const auto target_width = driven_values[signal_id].width();
    if (value.width() == 0 || offset > target_width
        || value.width() > target_width - offset) {
      throw std::invalid_argument(
          "partial update range is outside its target signal");
    }
    pending_updates.push_back(PendingUpdate{
        signal_id, offset, std::move(value)});
    schedule_update_commit();
  }

  [[noreturn]] void fail(const ProcessState &process,
                         const std::string &message) const {
    throw InterpreterError(process.program.id, process.pc, message);
  }
};

struct Interpreter::Impl::ExecutionContext final
    : ProcessExecutionContext {
  Impl& owner;
  ProcessId process;

  ExecutionContext(Impl& owner_value, const ProcessId process_value)
      : owner(owner_value), process(process_value) {}

  [[nodiscard]] PackedLogic4
  read_signal(const SignalId signal) const override {
    return owner.get_signal(signal).initial_value;
  }

  [[nodiscard]] Logic4Word
  read_signal_word(const SignalId signal) const override {
    return owner.get_signal(signal).initial_value.low_word();
  }

  void write_blocking(
      const SignalId signal, PackedLogic4 value) override {
    owner.commit(signal, std::move(value));
  }

  void write_blocking_word(
      const SignalId signal,
      const Logic4Word value) override {
    owner.commit(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }

  void write_blocking_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset) override {
    owner.commit_slice(signal, std::move(value), offset);
  }

  void write_blocking_slice_word(
      const SignalId signal,
      const Logic4Word value,
      const std::uint32_t offset) override {
    owner.commit_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
  }

  void write_update(
      const SignalId signal, PackedLogic4 value) override {
    owner.stage_update(signal, std::move(value));
  }

  void write_update_word(
      const SignalId signal,
      const Logic4Word value) override {
    owner.stage_update(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval));
  }

  void write_update_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset) override {
    owner.stage_update_slice(
        signal, std::move(value), offset);
  }

  void write_update_slice_word(
      const SignalId signal,
      const Logic4Word value,
      const std::uint32_t offset) override {
    owner.stage_update_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset);
  }

  void write_after(
      const SignalId signal,
      PackedLogic4 value,
      const SimulationTick delay) override {
    owner.scheduler.schedule_after(
        delay,
        SchedulerPhase::update,
        process,
        [&owner = owner, signal, value = std::move(value)](
            Scheduler&) mutable {
          owner.stage_update(signal, std::move(value));
        });
  }

  void write_after_word(
      const SignalId signal,
      const Logic4Word value,
      const SimulationTick delay) override {
    auto packed = PackedLogic4::from_aval_bval(
        value.width, value.aval, value.bval);
    owner.scheduler.schedule_after(
        delay,
        SchedulerPhase::update,
        process,
        [&owner = owner, signal, value = std::move(packed)](
            Scheduler&) mutable {
          owner.stage_update(signal, std::move(value));
        });
  }

  void write_after_slice(
      const SignalId signal,
      PackedLogic4 value,
      const std::size_t offset,
      const SimulationTick delay) override {
    owner.scheduler.schedule_after(
        delay,
        SchedulerPhase::update,
        process,
        [&owner = owner,
         signal,
         value = std::move(value),
         offset](Scheduler&) mutable {
          owner.stage_update_slice(
              signal, std::move(value), offset);
        });
  }

  void write_after_slice_word(
      const SignalId signal,
      const Logic4Word value,
      const std::uint32_t offset,
      const SimulationTick delay) override {
    write_after_slice(
        signal,
        PackedLogic4::from_aval_bval(
            value.width, value.aval, value.bval),
        offset,
        delay);
  }

  void notify_event(
      const SignalId event,
      const SimulationTick delay,
      const EventNotificationKind kind) override {
    owner.notify_event(event, delay, kind, process);
  }

  void cancel_event(const SignalId event) override {
    owner.cancel_event(event);
  }

  [[nodiscard]] bool
  signal_event(const SignalId signal) const override {
    (void)owner.get_signal(signal);
    const auto& event = owner.signal_events[signal];
    return event
        && event->first == owner.scheduler.now()
        && event->second == owner.scheduler.delta();
  }

  [[nodiscard]] Logic4Word
  signal_last_value_word(const SignalId signal) const override {
    (void)owner.get_signal(signal);
    return owner.signal_last_values[signal].low_word();
  }

  [[nodiscard]] SimulationTick
  signal_last_event(const SignalId signal) const override {
    (void)owner.get_signal(signal);
    const auto& event = owner.signal_events[signal];
    return event
        ? owner.scheduler.now() - event->first
        : std::numeric_limits<SimulationTick>::max();
  }

  [[nodiscard]] bool
  signal_active(const SignalId signal) const override {
    (void)owner.get_signal(signal);
    const auto& transaction = owner.signal_transactions[signal];
    return transaction
        && transaction->first == owner.scheduler.now()
        && transaction->second == owner.scheduler.delta();
  }

  void request_channel_update(
      const std::uint64_t channel) override {
    owner.request_channel_update(process, channel);
  }

  void display(
      const std::string_view text,
      const bool newline) override {
    if (owner.output_hook) {
      owner.output_hook(
          process,
          text,
          newline,
          owner.scheduler.now(),
          owner.scheduler.delta());
    }
  }

  void postpone_display(
      const std::string_view text,
      const bool newline) override {
    owner.scheduler.schedule(
        SchedulerPhase::postponed,
        process,
        [&owner = owner,
         process = process,
         text = std::string{text},
         newline](Scheduler& scheduler) {
          if (owner.output_hook) {
            owner.output_hook(
                process,
                text,
                newline,
                scheduler.now(),
                scheduler.delta());
          }
        });
  }

  void display_formatted(
      const std::string_view prefix,
      const std::string_view suffix,
      const OutputFormat format,
      const PackedLogic4& value,
      const bool newline,
      const bool postponed,
      const bool signed_decimal,
      const bool suppress_leading_zero,
      const std::uint32_t minimum_width,
      const bool left_justify,
      const bool zero_pad) override {
    auto text =
        make_formatted_output(
            prefix,
            suffix,
            format,
            value,
            signed_decimal,
            suppress_leading_zero,
            minimum_width,
            left_justify,
            zero_pad);
    if (postponed) {
      owner.scheduler.schedule(
          SchedulerPhase::postponed,
          process,
          [&owner = owner,
           process = process,
           text = std::move(text),
           newline](Scheduler& scheduler) {
            if (owner.output_hook) {
              owner.output_hook(
                  process,
                  text,
                  newline,
                  scheduler.now(),
                  scheduler.delta());
            }
          });
    } else if (owner.output_hook) {
      owner.output_hook(
          process,
          text,
          newline,
          owner.scheduler.now(),
          owner.scheduler.delta());
    }
  }

  void display_time(
      const std::string_view prefix,
      const std::string_view suffix,
      const bool newline,
      const bool postponed,
      const std::uint32_t minimum_width,
      const bool left_justify,
      const bool zero_pad) override {
    auto text = make_time_output(
        prefix,
        suffix,
        owner.scheduler.now(),
        minimum_width,
        left_justify,
        zero_pad);
    if (postponed) {
      owner.scheduler.schedule(
          SchedulerPhase::postponed,
          process,
          [&owner = owner,
           process = process,
           text = std::move(text),
           newline](Scheduler& scheduler) {
            if (owner.output_hook) {
              owner.output_hook(
                  process,
                  text,
                  newline,
                  scheduler.now(),
                  scheduler.delta());
            }
          });
    } else if (owner.output_hook) {
      owner.output_hook(
          process,
          text,
          newline,
          owner.scheduler.now(),
          owner.scheduler.delta());
    }
  }

  void install_monitor(
      const MonitorInstall& registration) override {
    owner.install_monitor(process, registration);
  }

  void set_monitor_enabled(const bool enabled) override {
    owner.set_monitor_enabled(enabled);
  }

  [[nodiscard]] PackedLogic4 random_value(
      const RandomKind kind,
      const std::optional<PackedLogic4>& maximum,
      const std::optional<PackedLogic4>& minimum) override {
    return owner.random_value(
        process, kind, maximum, minimum);
  }

  void report(
      const std::string_view message,
      const AssertionSeverity severity,
      const SourceLocation& source) override {
    if (owner.report_hook) {
      owner.report_hook(
          process,
          message,
          severity,
          source,
          owner.scheduler.now(),
          owner.scheduler.delta());
    }
  }

  [[nodiscard]] bool
  execution_points_enabled() const noexcept override {
    return static_cast<bool>(owner.execution_point_hook);
  }
};

void Interpreter::Impl::request_channel_update(
    const ProcessId process_id,
    const std::uint64_t channel) {
  auto& process = get_process(process_id);
  if (!process.executor) {
    throw std::logic_error{
        "primitive-channel update requires an alternate executor"};
  }
  if (!pending_channel_updates.insert(channel).second) {
    return;
  }

  auto callback =
      [this, process_id, channel](Scheduler&) {
        auto& state = get_process(process_id);
        ExecutionContext context{*this, process_id};
        try {
          state.executor->update_channel(channel, context);
        } catch (...) {
          pending_channel_updates.erase(channel);
          throw;
        }
        pending_channel_updates.erase(channel);
      };
  const auto phase = scheduler.current_phase();
  if (phase && *phase >= SchedulerPhase::update) {
    scheduler.schedule_next_delta(
        SchedulerPhase::update, channel, std::move(callback));
  } else {
    scheduler.schedule(
        SchedulerPhase::update, channel, std::move(callback));
  }
}

void Interpreter::Impl::handle_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex next_instruction) {
  if (instruction >= process.program.operations.size()) {
    process.pc = instruction;
    fail(process, "executor returned an invalid boundary instruction");
  }
  if (instruction == std::numeric_limits<InstructionIndex>::max()
      || next_instruction != instruction + 1) {
    process.pc = instruction;
    fail(
        process,
        "executor returned a non-sequential boundary resume instruction");
  }

  const auto& operation = process.program.operations[instruction];
  process.pc = next_instruction;
  if (const auto* point = std::get_if<DebugPoint>(&operation)) {
    clear_wait_timeout(process);
    process.current_source = point->source;
    auto kind = ExecutionPointKind::statement;
    switch (point->kind) {
    case DebugPointKind::statement:
      kind = ExecutionPointKind::statement;
      break;
    case DebugPointKind::call:
      kind = ExecutionPointKind::call;
      break;
    case DebugPointKind::wait:
      kind = ExecutionPointKind::wait;
      break;
    case DebugPointKind::assertion:
      kind = ExecutionPointKind::assertion;
      break;
    case DebugPointKind::process_entry:
      kind = ExecutionPointKind::process_entry;
      break;
    }
    notify_execution_point(
        process, instruction, kind, process.current_source);
    if (scheduler.stop_requested()) {
      queue_current(process.program.id);
    }
    return;
  }
  if (const auto* wait = std::get_if<WaitFor>(&operation)) {
    clear_wait_timeout(process);
    if (wait->delay == 0) {
      process.queued = true;
      scheduler.schedule(
          SchedulerPhase::inactive,
          process.program.id,
          [this, id = process.program.id](Scheduler&) {
            auto& state = get_process(id);
            state.queued = false;
            execute(id);
          });
      notify_execution_point(
          process, instruction, ExecutionPointKind::process_suspend,
          process.current_source);
      return;
    }
    if (wait->delay
        > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
      process.pc = instruction;
      fail(process, "simulation time overflow in WaitFor");
    }
    queue_at(process.program.id, scheduler.now() + wait->delay);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (const auto* wait = std::get_if<WaitOn>(&operation)) {
    if (wait->signals.empty() && !wait->timeout) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn requires at least one signal or a timeout");
    }
    if (!wait->edges.empty()
        && wait->edges.size() != wait->signals.size()) {
      process.pc = instruction;
      fail(process, "WaitOn edge count must match its signal count");
    }
    if (!wait->timeout
        && (wait->timeout_result
            || wait->timeout_origin)) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn timeout metadata requires a timeout");
    }
    if (wait->timeout_origin
        && !wait->timeout_result) {
      process.pc = instruction;
      fail(
          process,
          "WaitOn timeout rearm requires a result register");
    }
    if (wait->timeout_origin) {
      if (*wait->timeout_origin >= instruction) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout origin must precede its rearm");
      }
      const auto* origin = std::get_if<WaitOn>(
          &process.program.operations[*wait->timeout_origin]);
      if (origin == nullptr
          || !origin->timeout
          || origin->timeout_origin
          || origin->timeout != wait->timeout
          || origin->timeout_result
              != wait->timeout_result
          || origin->signals != wait->signals
          || origin->edges != wait->edges) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout rearm does not match its origin");
      }
    }
    process.waiting_on_signal = true;
    process.dynamic_sensitivity.clear();
    process.dynamic_sensitivity.reserve(wait->signals.size());
    for (std::size_t index = 0; index < wait->signals.size(); ++index) {
      const auto signal = wait->signals[index];
      (void)get_signal(signal);
      const auto edge =
          wait->edges.empty() ? EdgeKind::any : wait->edges[index];
      switch (edge) {
      case EdgeKind::any:
        break;
      case EdgeKind::posedge:
      case EdgeKind::negedge:
        if (get_signal(signal).initial_value.width() != 1) {
          process.pc = instruction;
          fail(process, "WaitOn edge requires a scalar signal");
        }
        break;
      default:
        process.pc = instruction;
        fail(process, "WaitOn has an invalid edge kind");
      }
      process.dynamic_sensitivity.push_back({signal, edge});
    }
    std::sort(
        process.dynamic_sensitivity.begin(),
        process.dynamic_sensitivity.end(),
        [](const Sensitivity& lhs, const Sensitivity& rhs) {
          return lhs.signal < rhs.signal
              || (lhs.signal == rhs.signal
                  && lhs.edge < rhs.edge);
        });
    process.dynamic_sensitivity.erase(
        std::unique(
            process.dynamic_sensitivity.begin(),
            process.dynamic_sensitivity.end(),
            [](const Sensitivity& lhs, const Sensitivity& rhs) {
              return lhs.signal == rhs.signal
                  && lhs.edge == rhs.edge;
            }),
        process.dynamic_sensitivity.end());
    for (const auto sensitivity : process.dynamic_sensitivity) {
      dynamic_fanout[sensitivity.signal].push_back(
          {process.program.id, sensitivity.edge});
    }
    if (wait->timeout) {
      if (wait->timeout_origin) {
        rearm_wait_timeout(
            process,
            instruction,
            *wait->timeout_origin,
            wait->timeout_result);
      } else {
        begin_wait_timeout(
            process,
            instruction,
            *wait->timeout,
            wait->timeout_result);
      }
    } else {
      clear_wait_timeout(process);
    }
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<WaitSensitivity>(operation)) {
    clear_wait_timeout(process);
    if (process.program.static_sensitivity.empty()) {
      process.pc = instruction;
      fail(process, "WaitSensitivity requires a static sensitivity list");
    }
    process.waiting_on_static = true;
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<WaitForever>(operation)) {
    clear_wait_timeout(process);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<Yield>(operation)) {
    clear_wait_timeout(process);
    queue_next_delta(process.program.id);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<Pause>(operation)) {
    clear_wait_timeout(process);
    scheduler.request_stop();
    queue_current(process.program.id);
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<Stop>(operation)) {
    clear_wait_timeout(process);
    process.halted = true;
    stopped_by_design = true;
    scheduler.request_stop();
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }
  if (std::holds_alternative<Halt>(operation)) {
    clear_wait_timeout(process);
    process.halted = true;
    notify_execution_point(
        process, instruction, ExecutionPointKind::process_suspend,
        process.current_source);
    return;
  }

  process.pc = instruction;
  fail(
      process,
      "executor returned at an operation that is not a kernel boundary");
}

void Interpreter::Impl::handle_external_boundary(
    ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex next_instruction,
    const ExternalSuspension& suspension) {
  if (instruction >= process.program.operations.size()) {
    process.pc = instruction;
    fail(process, "executor returned an invalid dynamic boundary instruction");
  }
  if (instruction == std::numeric_limits<InstructionIndex>::max()
      || next_instruction != instruction + 1) {
    process.pc = instruction;
    fail(
        process,
        "executor returned a non-sequential dynamic boundary resume "
        "instruction");
  }
  process.pc = next_instruction;
  clear_wait_timeout(process);

  switch (suspension.kind) {
  case ExternalSuspendKind::simir_boundary:
    process.pc = instruction;
    fail(process, "missing dynamic suspension kind");
  case ExternalSuspendKind::wait_for:
    if (suspension.delay == 0) {
      queue_next_delta(process.program.id);
    } else {
      if (suspension.delay
          > std::numeric_limits<SimulationTick>::max() - scheduler.now()) {
        process.pc = instruction;
        fail(process, "simulation time overflow in dynamic wait");
      }
      queue_at(
          process.program.id, scheduler.now() + suspension.delay);
    }
    break;
  case ExternalSuspendKind::wait_on:
    if (suspension.sensitivity.empty()) {
      process.pc = instruction;
      fail(process, "dynamic wait requires at least one event");
    }
    process.waiting_on_signal = true;
    process.dynamic_sensitivity = suspension.sensitivity;
    std::sort(
        process.dynamic_sensitivity.begin(),
        process.dynamic_sensitivity.end(),
        [](const Sensitivity& lhs, const Sensitivity& rhs) {
          return lhs.signal < rhs.signal
              || (lhs.signal == rhs.signal && lhs.edge < rhs.edge);
        });
    process.dynamic_sensitivity.erase(
        std::unique(
            process.dynamic_sensitivity.begin(),
            process.dynamic_sensitivity.end()),
        process.dynamic_sensitivity.end());
    process.dynamic_wait_all = suspension.wait_all;
    process.dynamic_triggered.assign(
        process.dynamic_sensitivity.size(), false);
    for (const auto& sensitivity : process.dynamic_sensitivity) {
      (void)get_signal(sensitivity.signal);
      if (sensitivity.edge != EdgeKind::any) {
        process.pc = instruction;
        fail(process, "dynamic event wait must use any-change sensitivity");
      }
      dynamic_fanout[sensitivity.signal].push_back(
          {process.program.id, sensitivity.edge});
    }
    break;
  case ExternalSuspendKind::wait_sensitivity:
    if (process.program.static_sensitivity.empty()) {
      process.pc = instruction;
      fail(process, "dynamic static wait has no sensitivity list");
    }
    process.waiting_on_static = true;
    break;
  case ExternalSuspendKind::yield:
    queue_next_delta(process.program.id);
    break;
  case ExternalSuspendKind::halt:
    process.halted = true;
    break;
  }
  notify_execution_point(
      process, instruction, ExecutionPointKind::process_suspend,
      process.current_source);
}

void Interpreter::Impl::execute(ProcessId id) {
  auto &process = get_process(id);
  if (process.executor) {
    ExecutionContext context{*this, id};
    while (!process.halted) {
      const auto boundary = process.executor->resume(context, process.pc);
      if (boundary.external.kind
          == ExternalSuspendKind::simir_boundary) {
        handle_boundary(
            process, boundary.instruction, boundary.next_instruction);
      } else {
        handle_external_boundary(
            process,
            boundary.instruction,
            boundary.next_instruction,
            boundary.external);
      }
      if (!std::holds_alternative<DebugPoint>(
              process.program.operations[boundary.instruction])
          || scheduler.stop_requested()) {
        return;
      }
    }
    return;
  }

  while (!process.halted) {
    if (process.pc >= process.program.operations.size()) {
      fail(process, "program counter is outside the operation stream");
    }

    const auto instruction = process.pc;
    const auto &operation = process.program.operations[instruction];
    bool boundary = false;
    std::visit(
        Overloaded{
            [&](const LoadConstant &op) {
              get_register(process, op.destination) = op.value;
              ++process.pc;
            },
            [&](const ReadSignal &op) {
              get_register(process, op.destination) =
                  get_signal(op.signal).initial_value;
              ++process.pc;
            },
            [&](const SignalEvent& op) {
              (void)get_signal(op.signal);
              const auto& event = signal_events[op.signal];
              const auto active =
                  event
                  && event->first == scheduler.now()
                  && event->second == scheduler.delta();
              get_register(process, op.destination) =
                  PackedLogic4(
                      1, active ? Logic4::one : Logic4::zero);
              ++process.pc;
            },
            [&](const SignalLastValue& op) {
              (void)get_signal(op.signal);
              get_register(process, op.destination) =
                  signal_last_values[op.signal];
              ++process.pc;
            },
            [&](const SignalLastEvent& op) {
              (void)get_signal(op.signal);
              const auto& event = signal_events[op.signal];
              const auto elapsed =
                  event
                      ? scheduler.now() - event->first
                      : std::numeric_limits<SimulationTick>::max();
              get_register(process, op.destination) =
                  PackedLogic4::from_aval_bval(64, elapsed, 0);
              ++process.pc;
            },
            [&](const SignalActive& op) {
              (void)get_signal(op.signal);
              const auto& transaction = signal_transactions[op.signal];
              const auto active =
                  transaction
                  && transaction->first == scheduler.now()
                  && transaction->second == scheduler.delta();
              get_register(process, op.destination) =
                  PackedLogic4(
                      1, active ? Logic4::one : Logic4::zero);
              ++process.pc;
            },
            [&](const CopyRegister& op) {
              get_register(process, op.destination) =
                  get_register(process, op.source);
              ++process.pc;
            },
            [&](const UnaryNot &op) {
              get_register(process, op.destination) =
                  unary_not(get_register(process, op.source));
              ++process.pc;
            },
            [&](const LogicalNot& op) {
              get_register(process, op.destination) =
                  logical_not(get_register(process, op.source));
              ++process.pc;
            },
            [&](const LogicalBinary& op) {
              get_register(process, op.destination) =
                  logical_binary(
                      op.operation,
                      get_register(process, op.lhs),
                      get_register(process, op.rhs));
              ++process.pc;
            },
            [&](const Reduction& op) {
              get_register(process, op.destination) =
                  reduce_value(
                      op.operation,
                      get_register(process, op.source));
              ++process.pc;
            },
            [&](const CountOnes& op) {
              get_register(process, op.destination) =
                  count_ones_value(
                      get_register(process, op.source));
              ++process.pc;
            },
            [&](const CountBits& op) {
              get_register(process, op.destination) =
                  count_bits_value(
                      get_register(process, op.source),
                      op.state_mask);
              ++process.pc;
            },
            [&](const Shift& op) {
              get_register(process, op.destination) =
                  shift_value(
                      op.operation,
                      get_register(process, op.value),
                      get_register(process, op.amount));
              ++process.pc;
            },
            [&](const Extract& op) {
              try {
                get_register(process, op.destination) =
                    extract_value(
                        get_register(process, op.source),
                        op.offset,
                        op.width);
              } catch (const std::invalid_argument& error) {
                fail(process, error.what());
              }
              ++process.pc;
            },
            [&](const Insert& op) {
              try {
                get_register(process, op.destination) =
                    insert_value(
                        get_register(process, op.target),
                        get_register(process, op.source),
                        op.offset);
              } catch (const std::invalid_argument& error) {
                fail(process, error.what());
              }
              ++process.pc;
            },
            [&](const Concatenate& op) {
              std::vector<PackedLogic4> operands;
              operands.reserve(op.operands.size());
              for (const auto operand : op.operands) {
                operands.push_back(get_register(process, operand));
              }
              try {
                get_register(process, op.destination) =
                    concatenate_values(operands, op.width);
              } catch (const std::invalid_argument& error) {
                fail(process, error.what());
              }
              ++process.pc;
            },
            [&](const Binary &op) {
              try {
                get_register(process, op.destination) =
                    binary_value(op.operation, get_register(process, op.lhs),
                                 get_register(process, op.rhs));
              } catch (const std::invalid_argument &error) {
                fail(process, error.what());
              }
              ++process.pc;
            },
            [&](const ConditionalSelect& op) {
              try {
                get_register(process, op.destination) =
                    conditional_value(
                        get_register(process, op.condition),
                        get_register(process, op.when_true),
                        get_register(process, op.when_false));
              } catch (const std::invalid_argument& error) {
                fail(process, error.what());
              }
              ++process.pc;
            },
            [&](const WriteBlocking &op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              commit(op.signal, std::move(value));
            },
            [&](const WriteUpdate &op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              stage_update(op.signal, std::move(value));
            },
            [&](const WriteAfter &op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              scheduler.schedule_after(
                  op.delay, SchedulerPhase::update, process.program.id,
                  [this, signal = op.signal,
                   value = std::move(value)](Scheduler &) mutable {
                    stage_update(signal, std::move(value));
                  });
            },
            [&](const WriteBlockingSlice& op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              commit_slice(
                  op.signal, std::move(value), op.offset);
            },
            [&](const WriteUpdateSlice& op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              stage_update_slice(
                  op.signal, std::move(value), op.offset);
            },
            [&](const WriteAfterSlice& op) {
              auto value = get_register(process, op.source);
              ++process.pc;
              scheduler.schedule_after(
                  op.delay,
                  SchedulerPhase::update,
                  process.program.id,
                  [this,
                   signal = op.signal,
                   offset = op.offset,
                   value = std::move(value)](
                      Scheduler&) mutable {
                    stage_update_slice(
                        signal, std::move(value), offset);
                  });
            },
            [&](const WaitFor &op) {
              (void)op;
              boundary = true;
            },
            [&](const WaitOn &op) {
              (void)op;
              boundary = true;
            },
            [&](const WaitSensitivity &) {
              boundary = true;
            },
            [&](const WaitForever &) {
              boundary = true;
            },
            [&](const Yield &) {
              boundary = true;
            },
            [&](const Jump &op) {
              if (op.target >= process.program.operations.size()) {
                fail(process, "jump target is outside the operation stream");
              }
              process.pc = op.target;
            },
            [&](const Branch &op) {
              const auto &condition = get_register(process, op.condition);
              if (condition.width() != 1) {
                fail(process, "branch condition must be scalar");
              }
              const auto value = condition.get(0);
              InstructionIndex target{};
              if (value != Logic4::zero && value != Logic4::one) {
                if (op.unknown_policy
                    == UnknownBranchPolicy::when_false) {
                  target = op.when_false;
                } else {
                  fail(
                      process,
                      "branch condition is unknown or high impedance");
                }
              } else {
                target =
                    value == Logic4::one ? op.when_true : op.when_false;
              }
              if (target >= process.program.operations.size()) {
                fail(process, "branch target is outside the operation stream");
              }
              process.pc = target;
            },
            [&](const DebugPoint&) {
              boundary = true;
            },
            [&](const Assert &op) {
              const auto &condition = get_register(process, op.condition);
              if (condition.width() != 1 ||
                  condition.get(0) != Logic4::one) {
                const auto message =
                    op.message.empty()
                        ? std::string_view{"assertion failed"}
                        : std::string_view{op.message};
                if (op.severity != AssertionSeverity::failure
                    && report_hook) {
                  report_hook(
                      process.program.id,
                      message,
                      op.severity,
                      op.source,
                      scheduler.now(),
                      scheduler.delta());
                }
                if (op.severity == AssertionSeverity::failure) {
                  throw AssertionError(
                      process.program.id,
                      process.pc,
                      std::string{message},
                      op.severity,
                      op.source);
                }
              }
              ++process.pc;
            },
            [&](const Display& op) {
              if (op.postponed) {
                scheduler.schedule(
                    SchedulerPhase::postponed,
                    process.program.id,
                    [this,
                     process_id = process.program.id,
                     text = op.text,
                     newline = op.newline](Scheduler& runtime) {
                      if (output_hook) {
                        output_hook(
                            process_id,
                            text,
                            newline,
                            runtime.now(),
                            runtime.delta());
                      }
                    });
              } else if (output_hook) {
                output_hook(
                    process.program.id,
                    op.text,
                    op.newline,
                    scheduler.now(),
                    scheduler.delta());
              }
              ++process.pc;
            },
            [&](const FormatDisplay& op) {
              auto text = make_formatted_output(
                  op.prefix,
                  op.suffix,
                  op.format,
                  get_register(process, op.source),
                  op.signed_decimal,
                  op.suppress_leading_zero,
                  op.minimum_width,
                  op.left_justify,
                  op.zero_pad);
              if (op.postponed) {
                scheduler.schedule(
                    SchedulerPhase::postponed,
                    process.program.id,
                    [this,
                     process_id = process.program.id,
                     text = std::move(text),
                     newline = op.newline](Scheduler& runtime) {
                      if (output_hook) {
                        output_hook(
                            process_id,
                            text,
                            newline,
                            runtime.now(),
                            runtime.delta());
                      }
                    });
              } else if (output_hook) {
                output_hook(
                    process.program.id,
                    text,
                    op.newline,
                    scheduler.now(),
                    scheduler.delta());
              }
              ++process.pc;
            },
            [&](const TimeDisplay& op) {
              auto text = make_time_output(
                  op.prefix,
                  op.suffix,
                  scheduler.now(),
                  op.minimum_width,
                  op.left_justify,
                  op.zero_pad);
              if (op.postponed) {
                scheduler.schedule(
                    SchedulerPhase::postponed,
                    process.program.id,
                    [this,
                     process_id = process.program.id,
                     text = std::move(text),
                     newline = op.newline](Scheduler& runtime) {
                      if (output_hook) {
                        output_hook(
                            process_id,
                            text,
                            newline,
                            runtime.now(),
                            runtime.delta());
                      }
                    });
              } else if (output_hook) {
                output_hook(
                    process.program.id,
                    text,
                    op.newline,
                    scheduler.now(),
                    scheduler.delta());
              }
              ++process.pc;
            },
            [&](const MonitorInstall& op) {
              install_monitor(process.program.id, op);
              ++process.pc;
            },
            [&](const MonitorControl& op) {
              set_monitor_enabled(op.enabled);
              ++process.pc;
            },
            [&](const RandomValue& op) {
              const auto maximum =
                  op.maximum
                      ? std::optional<PackedLogic4>{
                            get_register(process, *op.maximum)}
                      : std::nullopt;
              const auto minimum =
                  op.minimum
                      ? std::optional<PackedLogic4>{
                            get_register(process, *op.minimum)}
                      : std::nullopt;
              get_register(process, op.destination) =
                  random_value(
                      process.program.id,
                      op.kind,
                      maximum,
                      minimum);
              ++process.pc;
            },
            [&](const Report& op) {
              if (report_hook) {
                report_hook(
                    process.program.id,
                    op.message,
                    op.severity,
                    op.source,
                    scheduler.now(),
                    scheduler.delta());
              }
              if (op.severity == AssertionSeverity::failure) {
                throw AssertionError(
                    process.program.id,
                    process.pc,
                    op.message.empty() ? "report failure" : op.message,
                    op.severity,
                    op.source,
                    true);
              }
              ++process.pc;
            },
            [&](const Pause &) {
              boundary = true;
            },
            [&](const Stop &) {
              boundary = true;
            },
            [&](const Halt &) {
              boundary = true;
            }},
        operation);

    if (boundary) {
      handle_boundary(process, instruction, instruction + 1);
      if (!std::holds_alternative<DebugPoint>(operation)
          || scheduler.stop_requested()) {
        return;
      }
    }
  }
}

Interpreter::Interpreter(
    SchedulerOptions options,
    const std::uint64_t seed)
    : impl_(std::make_unique<Impl>(options, seed)) {}
Interpreter::~Interpreter() = default;
Interpreter::Interpreter(Interpreter &&) noexcept = default;
Interpreter &Interpreter::operator=(Interpreter &&) noexcept = default;

SignalId Interpreter::add_signal(Signal signal) {
  if (impl_->started) {
    throw std::logic_error("cannot add a SimIR signal after start");
  }
  const auto id = static_cast<SignalId>(impl_->signals.size());
  if (static_cast<std::size_t>(id) != impl_->signals.size()) {
    throw std::length_error("too many SimIR signals");
  }
  impl_->driven_values.push_back(signal.initial_value);
  impl_->signal_last_values.push_back(signal.initial_value);
  impl_->forced_values.emplace_back();
  impl_->signals.push_back(std::move(signal));
  impl_->static_fanout.emplace_back();
  impl_->dynamic_fanout.emplace_back();
  impl_->event_states.emplace_back();
  impl_->signal_events.emplace_back();
  impl_->signal_transactions.emplace_back();
  return id;
}

ProcessId Interpreter::add_process(Process process) {
  if (impl_->started) {
    throw std::logic_error("cannot add a SimIR process after start");
  }
  const auto id = static_cast<ProcessId>(impl_->processes.size());
  if (static_cast<std::size_t>(id) != impl_->processes.size()) {
    throw std::length_error("too many SimIR processes");
  }
  if (process.id != id) {
    throw std::invalid_argument("SimIR process IDs must be dense and ordered");
  }
  if (process.final && process.initialize) {
    throw std::invalid_argument(
        "a SimIR final process cannot initialize at time zero");
  }
  for (const auto signal : process.static_sensitivity) {
    if (signal.signal >= impl_->signals.size()) {
      throw std::invalid_argument("process sensitivity references invalid signal");
    }
    if (signal.edge != EdgeKind::any &&
        impl_->signals[signal.signal].initial_value.width() != 1) {
      throw std::invalid_argument(
          "edge sensitivity currently requires a scalar signal");
    }
    impl_->static_fanout[signal.signal].push_back({id, signal.edge});
  }
  std::set<std::string> local_names;
  for (const auto& local : process.debug_locals) {
    if (local.name.empty() || local.width == 0
        || local.register_id >= process.register_count) {
      throw std::invalid_argument{"invalid SimIR debug-local metadata"};
    }
    if (!local_names.insert(local.name).second) {
      throw std::invalid_argument{"duplicate SimIR debug-local name"};
    }
  }

  Impl::ProcessState state;
  state.registers.assign(process.register_count, PackedLogic4{});
  state.random_state = Impl::initial_random_state(
      impl_->root_seed, id);
  state.waiting_on_static = !process.initialize;
  state.program = std::move(process);
  impl_->processes.push_back(std::move(state));
  return id;
}

void Interpreter::set_process_executor(
    const ProcessId process,
    std::unique_ptr<ProcessExecutor> executor) {
  if (impl_->started) {
    throw std::logic_error(
        "cannot install a SimIR process executor after start");
  }
  if (!executor) {
    throw std::invalid_argument("SimIR process executor cannot be null");
  }
  auto& state = impl_->get_process(process);
  if (state.executor) {
    throw std::logic_error(
        "a SimIR process executor is already installed");
  }
  state.executor = std::move(executor);
}

void Interpreter::start() {
  if (impl_->started) {
    return;
  }
  impl_->started = true;
  for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
    if (impl_->processes[id].program.initialize
        && !impl_->processes[id].program.final) {
      impl_->queue_at(id, impl_->scheduler.now());
    }
  }
}

RunResult Interpreter::run(std::optional<SimulationTick> until) {
  start();
  auto ordinary = impl_->scheduler.run(until);
  const bool design_stop =
      ordinary.status == RunStatus::stopped
      && impl_->stopped_by_design;
  if (impl_->finals_ran
      || (ordinary.status != RunStatus::completed
          && !design_stop)) {
    return ordinary;
  }

  impl_->finals_ran = true;
  if (design_stop) {
    impl_->scheduler.discard_pending();
    impl_->scheduler.clear_stop();
  }
  for (ProcessId id = 0; id < impl_->processes.size(); ++id) {
    if (impl_->processes[id].program.final) {
      impl_->queue_at(id, impl_->scheduler.now());
    }
  }
  if (!impl_->scheduler.has_pending()) {
    if (design_stop) {
      impl_->scheduler.request_stop();
    }
    return ordinary;
  }

  const auto final_result = impl_->scheduler.run();
  ordinary.time = final_result.time;
  ordinary.delta = final_result.delta;
  ordinary.callbacks_executed += final_result.callbacks_executed;
  if (design_stop) {
    ordinary.status = RunStatus::stopped;
    impl_->scheduler.request_stop();
  } else {
    ordinary.status = final_result.status;
  }
  return ordinary;
}

void Interpreter::deposit_signal(SignalId signal, PackedLogic4 value) {
  impl_->commit(signal, std::move(value));
}

void Interpreter::force_signal(SignalId signal, PackedLogic4 value) {
  if (impl_->get_signal(signal).initial_value.width() != value.width()) {
    throw std::invalid_argument("SimIR signal force width mismatch");
  }
  impl_->forced_values[signal] = value;
  impl_->publish(signal, std::move(value));
}

void Interpreter::release_signal(SignalId signal) {
  (void)impl_->get_signal(signal);
  if (!impl_->forced_values[signal].has_value()) {
    return;
  }
  impl_->forced_values[signal].reset();
  impl_->publish(signal, impl_->driven_values[signal]);
}

bool Interpreter::signal_is_forced(const SignalId signal) const {
  (void)impl_->get_signal(signal);
  return impl_->forced_values[signal].has_value();
}

void Interpreter::schedule_signal_at(SignalId signal, PackedLogic4 value,
                                     SimulationTick time, StableOrder order) {
  // Validate eagerly so a malformed drive does not fail much later.
  if (impl_->get_signal(signal).initial_value.width() != value.width()) {
    throw std::invalid_argument("SimIR signal assignment width mismatch");
  }
  impl_->scheduler.schedule_at(
      time, SchedulerPhase::update, order,
      [state = impl_.get(), signal, value = std::move(value)](
          Scheduler &) mutable {
        state->stage_update(signal, std::move(value));
      });
}

void Interpreter::schedule_signal_after(SignalId signal, PackedLogic4 value,
                                        SimulationTick delay,
                                        StableOrder order) {
  if (delay >
      std::numeric_limits<SimulationTick>::max() - impl_->scheduler.now()) {
    throw std::overflow_error("simulation time overflow scheduling signal");
  }
  schedule_signal_at(signal, std::move(value), impl_->scheduler.now() + delay,
                     order);
}

const PackedLogic4 &Interpreter::signal_value(SignalId signal) const {
  return impl_->get_signal(signal).initial_value;
}

PackedLogic4 Interpreter::read_debug_local(
    const ProcessId process,
    const std::size_t local_index) const {
  auto& state = impl_->get_process(process);
  if (local_index >= state.program.debug_locals.size()) {
    throw std::out_of_range{"invalid SimIR debug-local index"};
  }
  const auto& local = state.program.debug_locals[local_index];
  if (state.executor) {
    return state.executor->read_register(
        local.register_id, local.width);
  }
  const auto& value = state.registers.at(local.register_id);
  if (value.width() != local.width) {
    throw std::logic_error{"SimIR debug local has not been initialized"};
  }
  return value;
}

bool Interpreter::stopped_by_design() const noexcept {
  return impl_->stopped_by_design;
}

Scheduler &Interpreter::scheduler() noexcept { return impl_->scheduler; }
const Scheduler &Interpreter::scheduler() const noexcept {
  return impl_->scheduler;
}

void Interpreter::set_signal_change_hook(SignalChangeHook hook) {
  impl_->signal_change_hook = std::move(hook);
}

void Interpreter::set_execution_point_hook(ExecutionPointHook hook) {
  impl_->execution_point_hook = std::move(hook);
}

void Interpreter::set_output_hook(OutputHook hook) {
  impl_->output_hook = std::move(hook);
}

void Interpreter::set_report_hook(ReportHook hook) {
  impl_->report_hook = std::move(hook);
}

} // namespace fsim::runtime::simir
