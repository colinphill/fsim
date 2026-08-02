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

}  // namespace

Lowerer::ExpressionAttempt
Lowerer::lower_vhdl_numeric_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* const expected_type) {
  if (language_ != frontend::Language::Vhdl2008
      || expression.kind != ExpressionKind::Call) {
    return ExpressionAttempt{};
  }
  const auto name = simple_name(expression.text);
  const bool conversion = name == "to_integer"
      || name == "to_signed" || name == "to_unsigned"
      || name == "resize";
  const bool shift = name == "shift_left" || name == "shift_right"
      || name == "rotate_left" || name == "rotate_right";
  if (!conversion && !shift) {
    return ExpressionAttempt{};
  }

  if (shift) {
    if (expression.operands.size() != 2) {
      report(
          "FSIM-ELAB-VHNUM-001",
          "a numeric shift or rotate requires a vector and an integer count",
          expression.span);
      return std::nullopt;
    }
    auto operation = name == "shift_left" ? "sll"
        : name == "rotate_left" ? "rol"
        : name == "rotate_right" ? "ror"
        : is_signed_expression(expression.operands.front()) ? "sra"
                                                            : "srl";
    return lower_expression(
        Expression{
            ExpressionKind::Binary,
            operation,
            expression.operands,
            expression.span},
        expected_width,
        expected_type);
  }

  if (name == "to_integer") {
    if (expression.operands.size() != 1) {
      report(
          "FSIM-ELAB-VHNUM-001",
          "to_integer requires exactly one signed or unsigned vector",
          expression.span);
      return std::nullopt;
    }
    const auto width = infer_width(expression.operands.front());
    const bool signed_operand =
        is_signed_expression(expression.operands.front());
    if (!width || *width == 0 || *width > (signed_operand ? 32U : 31U)) {
      report(
          "FSIM-ELAB-VHNUM-003",
          "to_integer supports signed widths 1..32 and unsigned widths 1..31",
          expression.operands.front().span);
      return std::nullopt;
    }
    const auto source = lower_expression(
        expression.operands.front(), *width);
    if (!source) {
      return std::nullopt;
    }
    const auto resized = resize_register(*source, 32, signed_operand);
    const auto result = allocate_register(
        32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(CopyRegister{result, resized});
    process_.operations.emplace_back(IntegerCheck{
        result,
        std::numeric_limits<std::int32_t>::min(),
        std::numeric_limits<std::int32_t>::max()});
    return result;
  }

  if (expression.operands.size() != 2) {
    report(
        "FSIM-ELAB-VHNUM-001",
        std::string{name}
            + " requires a value and a locally static result size",
        expression.span);
    return std::nullopt;
  }
  const auto requested = static_integer_value(expression.operands[1]);
  if (!requested || *requested < 1 || *requested > 64) {
    report(
        "FSIM-ELAB-VHNUM-002",
        std::string{name}
            + " requires a locally static result size from 1 through 64",
        expression.operands[1].span);
    return std::nullopt;
  }
  const auto result_width = static_cast<std::size_t>(*requested);
  if (expected_width != result_width) {
    report(
        "FSIM-ELAB-VHNUM-004",
        std::string{name} + " result width "
            + std::to_string(result_width)
            + " does not match its contextual width "
            + std::to_string(expected_width),
        expression.span);
    return std::nullopt;
  }

  if (name == "resize") {
    const auto source_width = infer_width(expression.operands.front());
    if (!source_width || *source_width == 0 || *source_width > 64) {
      report(
          "FSIM-ELAB-VHNUM-001",
          "resize requires a bounded signed or unsigned vector",
          expression.operands.front().span);
      return std::nullopt;
    }
    const auto source = lower_expression(
        expression.operands.front(), *source_width);
    if (!source) {
      return std::nullopt;
    }
    auto result = resize_register(
        *source,
        result_width,
        is_signed_expression(expression.operands.front()));
    if (expected_type != nullptr
        && register_domain(result) != expected_type->domain) {
      const auto converted = allocate_register(
          result_width, expected_type->domain);
      process_.operations.emplace_back(CopyRegister{converted, result});
      result = converted;
    }
    return result;
  }

  const auto& integer = expression.operands.front();
  if (!is_integer_expression(integer)) {
    report(
        "FSIM-ELAB-VHNUM-001",
        std::string{name} + " requires an integer value operand",
        integer.span);
    return std::nullopt;
  }
  const auto source = lower_expression(integer, 32);
  if (!source) {
    return std::nullopt;
  }
  if (name == "to_unsigned") {
    process_.operations.emplace_back(IntegerCheck{
        *source, 0, std::numeric_limits<std::int32_t>::max()});
  }
  const auto resized = resize_register(
      *source, result_width, name == "to_signed");
  const auto* visible_result_type = visible_type_mark(
      name == "to_signed" ? "signed" : "unsigned");
  const auto domain = expected_type != nullptr
          && (expected_type->domain == frontend::ValueDomain::Bit2
              || expected_type->domain == frontend::ValueDomain::Logic9)
      ? expected_type->domain
      : visible_result_type != nullptr
              && (visible_result_type->domain == frontend::ValueDomain::Bit2
                  || visible_result_type->domain
                      == frontend::ValueDomain::Logic9)
          ? visible_result_type->domain
      : frontend::ValueDomain::Logic9;
  const auto result = allocate_register(result_width, domain);
  process_.operations.emplace_back(CopyRegister{result, resized});
  return result;
}

}  // namespace fsim::elaboration
