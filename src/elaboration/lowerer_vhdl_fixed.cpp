// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

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
  if (distance >= 64) {
    report(
        "FSIM-ELAB-VHFIX-002",
        "fixed-point execution supports result widths from 1 through 64",
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
    if (!integer || *right > 0 || *right < -63) {
      report(
          "FSIM-ELAB-VHFIX-003",
          std::string{name}
              + " requires a locally static integer and a binary point from 0 through -63",
          expression.span);
      return std::nullopt;
    }
    const bool signed_result = name == "to_sfixed";
    const auto scale = static_cast<unsigned>(-*right);
    std::uint64_t bits = 0;
    if (!signed_result) {
      const auto maximum = result_width == 64
          ? std::numeric_limits<std::uint64_t>::max()
          : (std::uint64_t{1} << result_width) - 1U;
      const auto maximum_integer = maximum >> scale;
      if (*integer > 0
          && static_cast<std::uint64_t>(*integer) > maximum_integer) {
        bits = maximum;
      } else if (*integer > 0) {
        bits = static_cast<std::uint64_t>(*integer) << scale;
      }
    } else {
      const auto sign_index = result_width - 1U;
      const auto minimum_bits = std::uint64_t{1} << sign_index;
      const auto maximum = minimum_bits - 1U;
      if (*integer > 0
          && (scale > sign_index
              || static_cast<std::uint64_t>(*integer)
                  > (maximum >> scale))) {
        bits = maximum;
      } else if (*integer < 0) {
        const auto magnitude = static_cast<std::uint64_t>(
            -(*integer + 1)) + 1U;
        const auto maximum_magnitude = scale > sign_index
            ? std::uint64_t{0}
            : std::uint64_t{1} << (sign_index - scale);
        bits = magnitude > maximum_magnitude
            ? minimum_bits
            : static_cast<std::uint64_t>(*integer) << scale;
      } else {
        bits = static_cast<std::uint64_t>(*integer) << scale;
      }
    }
    if (result_width < 64) {
      bits &= (std::uint64_t{1} << result_width) - 1U;
    }
    auto value = unsigned_value(bits, result_width).promoted_to_logic9();
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
  if (source_width_value == 0 || source_width_value > 64) {
    report(
        "FSIM-ELAB-VHFIX-002",
        "fixed-point resize source width must be from 1 through 64",
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
    const auto amount = static_cast<std::size_t>(*right - source_right);
    if (signed_source || source_width == 64 || amount >= source_width) {
      report(
          "FSIM-ELAB-VHFIX-003",
          "rounded resize supports bounded unsigned fractional narrowing",
          expression.span);
      return std::nullopt;
    }
    const auto work_width = source_width + 1U;
    source = resize_register(*source, work_width, false);
    const auto half = allocate_register(
        work_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(LoadConstant{
        half,
        unsigned_value(
            std::uint64_t{1} << (amount - 1U), work_width)
            .promoted_to_logic9()});
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
    const auto amount = static_cast<std::size_t>(source_right - *right);
    if (amount >= 64
        || source_width > 64 - amount) {
      report(
          "FSIM-ELAB-VHFIX-003",
          "fixed-point resize scale expansion exceeds 64 bits",
          expression.span);
      return std::nullopt;
    }
    const auto work_width = std::max(result_width, source_width + amount);
    source = resize_register(*source, work_width, signed_source);
    const auto count = allocate_register(32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        count, unsigned_value(amount, 32)});
    const auto shifted = allocate_register(
        work_width, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(Shift{
        ShiftOperator::logical_left, shifted, *source, count, false});
    source = shifted;
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
