// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view simple_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return name.substr(
      separator == std::string_view::npos ? 0 : separator + 1);
}

bool fixed_type(const frontend::Type* const type) {
  if (type == nullptr) {
    return false;
  }
  const auto name = simple_name(
      !type->named_type.empty() ? type->named_type : type->spelling);
  return name == "ufixed" || name == "sfixed"
      || name == "unresolved_ufixed" || name == "unresolved_sfixed";
}

bool signed_fixed_type(const frontend::Type& type) {
  const auto name = simple_name(
      !type.named_type.empty() ? type.named_type : type.spelling);
  return type.is_signed || name == "sfixed" || name == "unresolved_sfixed";
}

PackedLogic4 fixed_integer_value(
    const std::int64_t integer,
    const std::size_t width,
    const std::size_t scale,
    const bool signed_result) {
  PackedLogic4 result(width, Logic4::zero);
  const auto magnitude = integer < 0
      ? static_cast<std::uint64_t>(-(integer + 1)) + 1U
      : static_cast<std::uint64_t>(integer);
  const auto sign_index = width - 1U;
  bool overflow = false;
  if (!signed_result) {
    const auto value_bits = width > scale ? width - scale : 0U;
    overflow = integer > 0 && value_bits < 64U
        && magnitude >= (std::uint64_t{1} << value_bits);
    if (overflow) {
      for (std::size_t bit = 0; bit < width; ++bit) {
        result.set(bit, Logic4::one);
      }
      return result.promoted_to_logic9();
    }
    if (integer <= 0) {
      return result.promoted_to_logic9();
    }
  } else if (integer > 0) {
    const auto value_bits = sign_index > scale ? sign_index - scale : 0U;
    overflow = value_bits < 63U
        && magnitude >= (std::uint64_t{1} << value_bits);
    if (overflow) {
      for (std::size_t bit = 0; bit < sign_index; ++bit) {
        result.set(bit, Logic4::one);
      }
      return result.promoted_to_logic9();
    }
  } else if (integer < 0) {
    const auto value_bits = sign_index >= scale ? sign_index - scale : 0U;
    overflow = sign_index < scale
        || (value_bits < 64U
            && magnitude > (std::uint64_t{1} << value_bits));
    if (overflow) {
      result.set(sign_index, Logic4::one);
      return result.promoted_to_logic9();
    }
  }

  const auto encoded = static_cast<std::uint64_t>(integer);
  for (std::size_t bit = scale; bit < width; ++bit) {
    const auto source_bit = bit - scale;
    const bool one = source_bit < 64U
        ? ((encoded >> source_bit) & 1U) != 0
        : integer < 0;
    if (one) {
      result.set(bit, Logic4::one);
    }
  }
  return result.promoted_to_logic9();
}

PackedLogic4 one_hot_value(
    const std::size_t bit,
    const std::size_t width) {
  PackedLogic4 result(width, Logic4::zero);
  result.set(bit, Logic4::one);
  return result.promoted_to_logic9();
}

}  // namespace

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_fixed_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* const expected_type) {
  if (language_ != frontend::Language::Vhdl2008
      || expression.kind != ExpressionKind::Call) {
    return ExpressionAttempt{};
  }
  const auto name = simple_name(expression.text);
  const bool conversion = name == "to_ufixed" || name == "to_sfixed";
  const auto source_type = expression.operands.empty()
      ? std::optional<frontend::Type>{}
      : vhdl_expression_type(expression.operands.front());
  const bool fixed_resize = name == "resize"
      && (fixed_type(expected_type)
          || (source_type && fixed_type(&*source_type)));
  if (!conversion && !fixed_resize) {
    return ExpressionAttempt{};
  }
  if (expression.operands.size() != 3) {
    report(
        "FSIM-ELAB-VHFIX-001",
        std::string{name}
            + " requires a value and locally static left and right bounds",
        expression.span);
    return std::nullopt;
  }
  const auto left = static_integer_value(expression.operands[1]);
  const auto right = static_integer_value(expression.operands[2]);
  if (!left || !right || *left < *right) {
    report(
        "FSIM-ELAB-VHFIX-002",
        std::string{name}
            + " requires a locally static descending fixed-point range",
        expression.span);
    return std::nullopt;
  }
  const auto distance = index_distance(*left, *right);
  if (distance >= std::numeric_limits<std::uint32_t>::max()) {
    report(
        "FSIM-ELAB-VHFIX-002",
        "fixed-point result width is not representable by SimIR",
        expression.span);
    return std::nullopt;
  }
  const auto result_width = static_cast<std::size_t>(distance + 1U);
  if (expected_width != result_width
      || (expected_type != nullptr
          && expected_type->packed_range
          && (expected_type->packed_range->left != *left
              || expected_type->packed_range->right != *right
              || !expected_type->packed_range->descending))) {
    report(
        "FSIM-ELAB-VHFIX-004",
        std::string{name}
            + " bounds do not match the contextual fixed-point range",
        expression.span);
    return std::nullopt;
  }

  if (conversion) {
    if (!is_integer_expression(expression.operands.front())) {
      report(
          "FSIM-ELAB-VHFIX-001",
          std::string{name} + " requires an integer value in this v1 slice",
          expression.operands.front().span);
      return std::nullopt;
    }
    const auto integer = static_integer_value(expression.operands.front());
    if (!integer || *right > 0) {
      report(
          "FSIM-ELAB-VHFIX-003",
          std::string{name}
              + " requires a locally static integer and a nonpositive "
                "binary-point index",
          expression.span);
      return std::nullopt;
    }
    const bool signed_result = name == "to_sfixed";
    const auto scale_value = index_distance(0, *right);
    if (scale_value > std::numeric_limits<std::size_t>::max()) {
      report(
          "FSIM-ELAB-VHFIX-003",
          "fixed-point binary-point distance exceeds host address space",
          expression.span);
      return std::nullopt;
    }
    auto value = fixed_integer_value(
        *integer, result_width,
        static_cast<std::size_t>(scale_value), signed_result);
    const auto result = allocate_register(
        result_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(LoadConstant{result, std::move(value)});
    return result;
  }

  if (!source_type || !fixed_type(&*source_type)
      || !source_type->packed_range
      || !source_type->packed_range->descending) {
    report(
        "FSIM-ELAB-VHFIX-001",
        "fixed-point resize requires a constrained ufixed or sfixed operand",
        expression.operands.front().span);
    return std::nullopt;
  }
  const auto source_width_value = source_type->packed_range->width();
  if (source_width_value == 0
      || source_width_value > std::numeric_limits<std::uint32_t>::max()) {
    report(
        "FSIM-ELAB-VHFIX-002",
        "fixed-point resize source width is not representable by SimIR",
        expression.operands.front().span);
    return std::nullopt;
  }
  const auto source_width = static_cast<std::size_t>(source_width_value);
  auto source = lower_expression(
      expression.operands.front(), source_width, &*source_type);
  if (!source) {
    return std::nullopt;
  }
  const bool signed_source = signed_fixed_type(*source_type);
  const auto source_right = source_type->packed_range->right;
  if (source_right < *right) {
    const auto amount_value = index_distance(*right, source_right);
    if (amount_value > std::numeric_limits<std::size_t>::max()) {
      report(
          "FSIM-ELAB-VHFIX-003",
          "fixed-point resize distance exceeds host address space",
          expression.span);
      return std::nullopt;
    }
    const auto amount = static_cast<std::size_t>(amount_value);
    if (signed_source) {
      report(
          "FSIM-ELAB-VHFIX-003",
          "rounded resize does not yet support signed fractional narrowing",
          expression.span);
      return std::nullopt;
    }
    if (amount > source_width) {
      const auto zero = allocate_register(
          result_width, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(LoadConstant{
          zero, PackedLogic4(result_width, Logic4::zero)
                    .promoted_to_logic9()});
      return zero;
    }
    if (source_width == std::numeric_limits<std::uint32_t>::max()) {
      report(
          "FSIM-ELAB-VHFIX-003",
          "rounded resize requires an intermediate width representable by SimIR",
          expression.span);
      return std::nullopt;
    }
    const auto work_width = source_width + 1U;
    source = resize_register(*source, work_width, false);
    const auto half = allocate_register(
        work_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(LoadConstant{
        half, one_hot_value(amount - 1U, work_width)});
    const auto rounded = allocate_register(
        work_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Binary{
        BinaryOperator::add_unsigned, rounded, *source, half});
    const auto count = allocate_register(32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        count, unsigned_value(amount, 32)});
    const auto shifted = allocate_register(
        work_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Shift{
        ShiftOperator::logical_right, shifted, rounded, count, false});
    source = shifted;
  } else if (source_right > *right) {
    const auto amount_value = index_distance(source_right, *right);
    if (amount_value > std::numeric_limits<std::size_t>::max()) {
      report(
          "FSIM-ELAB-VHFIX-003",
          "fixed-point resize distance exceeds host address space",
          expression.span);
      return std::nullopt;
    }
    const auto amount = static_cast<std::size_t>(amount_value);
    const auto work_width = result_width;
    source = resize_register(*source, work_width, signed_source);
    if (amount >= work_width) {
      const auto zero = allocate_register(
          work_width, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(LoadConstant{
          zero, PackedLogic4(work_width, Logic4::zero)
                    .promoted_to_logic9()});
      source = zero;
    } else {
      const auto count = allocate_register(
          32, frontend::ValueDomain::Bit2);
      process_.operations.emplace_back(LoadConstant{
          count, unsigned_value(amount, 32)});
      const auto shifted = allocate_register(
          work_width, frontend::ValueDomain::Logic9);
      process_.operations.emplace_back(Shift{
          ShiftOperator::logical_left, shifted, *source, count, false});
      source = shifted;
    }
  }
  auto resized = resize_register(*source, result_width, signed_source);
  if (register_domain(resized) != frontend::ValueDomain::Logic9) {
    const auto promoted = allocate_register(
        result_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(CopyRegister{promoted, resized});
    resized = promoted;
  }
  return resized;
}

}  // namespace fsim::elaboration
