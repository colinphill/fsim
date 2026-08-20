// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

[[nodiscard]] PackedLogic4 unary_not(const PackedLogic4 &source) {
  PackedLogic4 result(source.width(), Logic4::x);
  if (source.is_logic9()) {
    result.fill(Logic9::u);
  }
  for (std::size_t index = 0; index < source.width(); ++index) {
    if (source.is_logic9()) {
      result.set_logic9(
          index, logic_not(source.get_logic9(index)));
    } else {
      result.set(index, logic_not(source.get(index)));
    }
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

[[nodiscard]] constexpr ShiftOperator reverse_shift(
    const ShiftOperator operation) noexcept {
  switch (operation) {
  case ShiftOperator::logical_left:
    return ShiftOperator::logical_right;
  case ShiftOperator::logical_right:
    return ShiftOperator::logical_left;
  case ShiftOperator::arithmetic_left:
    return ShiftOperator::arithmetic_right;
  case ShiftOperator::arithmetic_right:
    return ShiftOperator::arithmetic_left;
  case ShiftOperator::rotate_left:
    return ShiftOperator::rotate_right;
  case ShiftOperator::rotate_right:
    return ShiftOperator::rotate_left;
  }
  return operation;
}

[[nodiscard]] PackedLogic4 shift_value(
    ShiftOperator operation,
    const PackedLogic4& value,
    const PackedLogic4& amount_value,
    const bool signed_amount) {
  for (std::size_t index = 0; index < amount_value.width(); ++index) {
    const auto bit = amount_value.get(index);
    if (bit == Logic4::x || bit == Logic4::z) {
      return PackedLogic4(value.width(), Logic4::x);
    }
  }

  auto magnitude = amount_value;
  if (signed_amount
      && amount_value.get(amount_value.width() - 1U)
          == Logic4::one) {
    operation = reverse_shift(operation);
    magnitude =
        PackedLogic4(amount_value.width(), Logic4::zero);
    bool carry = true;
    for (std::size_t index = 0;
         index < amount_value.width(); ++index) {
      const bool inverted =
          amount_value.get(index) == Logic4::zero;
      magnitude.set(
          index,
          inverted != carry ? Logic4::one : Logic4::zero);
      carry = inverted && carry;
    }
  }

  const auto rotating =
      operation == ShiftOperator::rotate_left
      || operation == ShiftOperator::rotate_right;
  std::size_t amount = 0;
  std::size_t rotate_bit = 1U % value.width();
  for (std::size_t index = 0; index < magnitude.width(); ++index) {
    const auto bit = magnitude.get(index);
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
  if (value.is_logic9()) {
    const auto logic9_fill =
        operation == ShiftOperator::arithmetic_right
            ? value.get_logic9(value.width() - 1U)
            : operation == ShiftOperator::arithmetic_left
                ? value.get_logic9(0)
                : Logic9::zero;
    result.fill(logic9_fill);
  }
  if (amount >= value.width()) {
    return result;
  }
  for (std::size_t index = 0; index < value.width(); ++index) {
    if (operation == ShiftOperator::rotate_left) {
      const auto source_index =
          (index + value.width() - amount) % value.width();
      if (value.is_logic9()) {
        result.set_logic9(
            index, value.get_logic9(source_index));
      } else {
        result.set(index, value.get(source_index));
      }
    } else if (operation == ShiftOperator::rotate_right) {
      const auto source_index =
          (index + amount) % value.width();
      if (value.is_logic9()) {
        result.set_logic9(
            index, value.get_logic9(source_index));
      } else {
        result.set(index, value.get(source_index));
      }
    } else if (
        operation == ShiftOperator::logical_left
        || operation == ShiftOperator::arithmetic_left) {
      if (index >= amount) {
        if (value.is_logic9()) {
          result.set_logic9(
              index, value.get_logic9(index - amount));
        } else {
          result.set(index, value.get(index - amount));
        }
      }
    } else if (index + amount < value.width()) {
      if (value.is_logic9()) {
        result.set_logic9(
            index, value.get_logic9(index + amount));
      } else {
        result.set(index, value.get(index + amount));
      }
    }
  }
  return result;
}

[[nodiscard]] PackedLogic4 extract_value(
    const PackedLogic4& source,
    const std::size_t offset,
    const std::size_t width) {
  return source.extract_bits(offset, width);
}

[[nodiscard]] PackedLogic4 insert_value(
    PackedLogic4 target,
    const PackedLogic4& source,
    const std::size_t offset) {
  target.insert_bits(source, offset);
  return target;
}

[[nodiscard]] std::uint32_t dynamic_index_offset(
    const PackedLogic4& index,
    const DynamicIndex& selection) {
  if (index.width() != 32) {
    throw std::invalid_argument(
        "dynamic packed index must use the signed 32-bit representation");
  }
  std::uint32_t raw{};
  for (std::size_t bit = 0; bit < 32; ++bit) {
    const auto value = index.get(bit);
    if (value == Logic4::x || value == Logic4::z) {
      throw std::invalid_argument(
          "dynamic packed index contains an unknown or high-impedance "
          "value");
    }
    if (value == Logic4::one) {
      raw |= UINT32_C(1) << bit;
    }
  }
  const auto signed_index =
      raw <= static_cast<std::uint32_t>(
                 std::numeric_limits<std::int32_t>::max())
          ? static_cast<std::int64_t>(raw)
          : static_cast<std::int64_t>(raw)
                - (INT64_C(1) << 32);
  const auto lower =
      std::min(selection.left, selection.right);
  const auto upper =
      std::max(selection.left, selection.right);
  if (signed_index < lower || signed_index > upper) {
    throw std::invalid_argument(
        "dynamic packed index is outside the declared range");
  }
  const auto offset =
      signed_index >= selection.right
          ? static_cast<std::uint64_t>(
                signed_index - selection.right)
          : static_cast<std::uint64_t>(
                selection.right - signed_index);
  if (offset
      > std::numeric_limits<std::uint32_t>::max()
            - selection.base_offset) {
    throw std::invalid_argument(
        "dynamic packed index offset is not representable");
  }
  return selection.base_offset
      + static_cast<std::uint32_t>(offset);
}

[[nodiscard]] PackedLogic4 dynamic_part_select_value(
    const PackedLogic4& source,
    const PackedLogic4& base,
    const std::int64_t left,
    const std::int64_t right,
    const std::uint32_t base_offset,
    const std::uint32_t width,
    const bool increasing,
    const bool source_descending,
    const bool two_state) {
    if (base.width() != 32 || width == 0) {
        throw std::invalid_argument(
            "dynamic part-select requires a signed 32-bit base and a "
            "nonzero fixed width");
    }
  auto result = PackedLogic4{
      width, two_state ? Logic4::zero : Logic4::x};
  if (source.is_logic9()) {
    result.fill(Logic9::x);
  }

  std::uint32_t raw{};
  for (std::size_t bit = 0; bit < 32; ++bit) {
    const auto value = base.get(bit);
    if (value == Logic4::x || value == Logic4::z) {
      return result;
    }
    if (value == Logic4::one) {
      raw |= UINT32_C(1) << bit;
    }
  }
  const auto signed_base =
      raw <= static_cast<std::uint32_t>(
                 std::numeric_limits<std::int32_t>::max())
          ? static_cast<std::int64_t>(raw)
          : static_cast<std::int64_t>(raw)
                - (INT64_C(1) << 32);
  const auto lower = std::min(left, right);
  const auto upper = std::max(left, right);
  const auto edge_distance =
      static_cast<std::int64_t>(width - 1U);
  const auto selected_right =
      increasing
          ? signed_base
                + (source_descending ? 0 : edge_distance)
          : signed_base
                - (source_descending ? edge_distance : 0);
  for (std::uint32_t bit = 0; bit < width; ++bit) {
    const auto selected =
        source_descending
            ? selected_right + static_cast<std::int64_t>(bit)
            : selected_right - static_cast<std::int64_t>(bit);
    if (selected < lower || selected > upper) {
      continue;
    }
    const auto offset = selected >= right
        ? static_cast<std::uint64_t>(selected - right)
        : static_cast<std::uint64_t>(right - selected);
    if (offset > std::numeric_limits<std::uint32_t>::max()
                     - base_offset
        || base_offset + offset >= source.width()) {
      continue;
    }
    const auto source_offset = base_offset
        + static_cast<std::uint32_t>(offset);
    if (source.is_logic9()) {
      result.set_logic9(bit, source.get_logic9(source_offset));
    } else {
      result.set(bit, source.get(source_offset));
    }
  }
  return result;
}

[[nodiscard]] std::optional<DynamicPartWrite>
dynamic_part_write_value(
    const PackedLogic4& source,
    const PackedLogic4& base,
    const DynamicPartIndex& selection) {
  if (base.width() != 32
      || selection.width == 0
      || source.width() != selection.width) {
      throw std::invalid_argument(
          "dynamic part-select write requires a signed 32-bit base and a "
          "matching nonzero fixed width");
  }
  std::uint32_t raw{};
  for (std::size_t bit = 0; bit < 32; ++bit) {
    const auto value = base.get(bit);
    if (value == Logic4::x || value == Logic4::z) {
      return std::nullopt;
    }
    if (value == Logic4::one) {
      raw |= UINT32_C(1) << bit;
    }
  }
  const auto signed_base =
      raw <= static_cast<std::uint32_t>(
                 std::numeric_limits<std::int32_t>::max())
          ? static_cast<std::int64_t>(raw)
          : static_cast<std::int64_t>(raw)
                - (INT64_C(1) << 32);
  const auto lower = std::min(selection.left, selection.right);
  const auto upper = std::max(selection.left, selection.right);
  const auto edge_distance =
      static_cast<std::int64_t>(selection.width - 1U);
  const auto selected_right = selection.increasing
      ? signed_base
            + (selection.source_descending ? 0 : edge_distance)
      : signed_base
            - (selection.source_descending ? edge_distance : 0);
  std::optional<std::uint32_t> first_source_bit;
  std::optional<std::uint32_t> first_target_offset;
  std::uint32_t selected_width{};
  for (std::uint32_t bit = 0; bit < selection.width; ++bit) {
    const auto selected = selection.source_descending
        ? selected_right + static_cast<std::int64_t>(bit)
        : selected_right - static_cast<std::int64_t>(bit);
    if (selected < lower || selected > upper) {
      continue;
    }
    const auto offset = selected >= selection.right
        ? static_cast<std::uint64_t>(selected - selection.right)
        : static_cast<std::uint64_t>(selection.right - selected);
    if (offset > std::numeric_limits<std::uint32_t>::max()) {
      throw std::invalid_argument(
          "dynamic part-select write offset is not representable");
    }
    if (!first_source_bit) {
      first_source_bit = bit;
      if (offset
          > std::numeric_limits<std::uint32_t>::max()
                - selection.base_offset) {
        throw std::invalid_argument(
            "dynamic part-select write offset is not representable");
      }
      first_target_offset = selection.base_offset
          + static_cast<std::uint32_t>(offset);
    } else if (
        bit != *first_source_bit + selected_width
        || selection.base_offset + offset
            != *first_target_offset + selected_width) {
      throw std::invalid_argument(
          "dynamic part-select write intersection is not contiguous");
    }
    ++selected_width;
  }
  if (!first_source_bit) {
    return std::nullopt;
  }
  return DynamicPartWrite{
      extract_value(source, *first_source_bit, selected_width),
      *first_target_offset};
}

[[nodiscard]] PackedLogic4 dynamic_part_insert_value(
    PackedLogic4 target,
    const PackedLogic4& source,
    const PackedLogic4& base,
    const DynamicPartIndex& selection) {
  const auto write =
      dynamic_part_write_value(source, base, selection);
  if (!write) {
    return target;
  }
  return insert_value(
      std::move(target), write->value, write->offset);
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
  if (std::ranges::any_of(
          operands,
          [](const PackedLogic4& operand) {
            return operand.is_logic9();
          })) {
    result.fill(Logic9::u);
  }
  std::size_t offset = 0;
  for (auto operand = operands.rbegin();
       operand != operands.rend(); ++operand) {
    for (std::size_t bit = 0; bit < operand->width(); ++bit) {
      if (result.is_logic9()) {
        result.set_logic9(
            offset + bit, operand->get_logic9(bit));
      } else {
        result.set(offset + bit, operand->get(bit));
      }
    }
    offset += operand->width();
  }
  return result;
}



} // namespace fsim::runtime::simir
