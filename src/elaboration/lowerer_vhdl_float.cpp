// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <bit>
#include <cmath>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

std::string_view simple_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return name.substr(
      separator == std::string_view::npos ? 0 : separator + 1);
}

bool float32_type(const frontend::Type* const type) {
  if (type == nullptr) {
    return false;
  }
  const auto name = simple_name(
      !type->named_type.empty() ? type->named_type : type->spelling);
  if (name != "float" && name != "unresolved_float"
      && name != "u_float" && name != "float32"
      && name != "unresolved_float32" && name != "u_float32") {
    return false;
  }
  return !type->packed_range
      || (type->packed_range->left == 8
          && type->packed_range->right == -23
          && type->packed_range->descending);
}

}  // namespace

Lowerer::ExpressionAttempt Lowerer::lower_vhdl_float_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* const expected_type) {
  if (language_ != frontend::Language::Vhdl2008
      || expression.kind != ExpressionKind::Call) {
    return ExpressionAttempt{};
  }
  const auto root_name = simple_name(expression.text);
  const auto float_result = root_name == "to_float"
      || root_name == "zerofp" || root_name == "neg_zerofp"
      || root_name == "nanfp" || root_name == "qnanfp"
      || root_name == "pos_inffp" || root_name == "neg_inffp"
      || root_name == "add" || root_name == "subtract"
      || root_name == "multiply" || root_name == "divide"
      || root_name == "sqrt" || root_name == "abs";
  const auto boolean_result = root_name == "finite"
      || root_name == "isnan" || root_name == "unordered"
      || root_name == "is_negative" || root_name == "eq"
      || root_name == "ne" || root_name == "lt" || root_name == "le"
      || root_name == "gt" || root_name == "ge";
  const auto packed_result = root_name == "to_slv"
      || root_name == "to_stdlogicvector"
      || root_name == "to_std_logic_vector";
  const auto integer_result = root_name == "to_integer";
  if (!float_result && !boolean_result && !packed_result
      && !integer_result) {
    return ExpressionAttempt{};
  }

  const auto valid_dimensions = [&](const Expression& candidate) {
    if (candidate.operands.size() != 2) {
      return false;
    }
    const auto exponent = static_integer_value(candidate.operands[0]);
    const auto fraction = static_integer_value(candidate.operands[1]);
    return exponent == 8 && fraction == 23;
  };
  std::function<std::optional<std::uint32_t>(const Expression&)> evaluate;
  evaluate = [&](const Expression& candidate)
      -> std::optional<std::uint32_t> {
    if (candidate.kind != ExpressionKind::Call) {
      return std::nullopt;
    }
    const auto name = simple_name(candidate.text);
    if (name == "zerofp" || name == "neg_zerofp"
        || name == "nanfp" || name == "qnanfp"
        || name == "pos_inffp" || name == "neg_inffp") {
      if (!valid_dimensions(candidate)) {
        return std::nullopt;
      }
      if (name == "neg_zerofp") {
        return UINT32_C(0x80000000);
      }
      if (name == "nanfp") {
        return UINT32_C(0x7f800001);
      }
      if (name == "qnanfp") {
        return UINT32_C(0x7fc00000);
      }
      if (name == "pos_inffp") {
        return UINT32_C(0x7f800000);
      }
      if (name == "neg_inffp") {
        return UINT32_C(0xff800000);
      }
      return UINT32_C(0);
    }
    if (name == "to_float") {
      if (candidate.operands.size() != 3
          || static_integer_value(candidate.operands[1]) != 8
          || static_integer_value(candidate.operands[2]) != 23) {
        return std::nullopt;
      }
      const auto integer = static_integer_value(candidate.operands[0]);
      return integer
          ? std::optional<std::uint32_t>{
                std::bit_cast<std::uint32_t>(static_cast<float>(*integer))}
          : std::nullopt;
    }
    const bool binary = name == "add" || name == "subtract"
        || name == "multiply" || name == "divide";
    if (binary) {
      if (candidate.operands.size() != 2) {
        return std::nullopt;
      }
      const auto lhs = evaluate(candidate.operands[0]);
      const auto rhs = evaluate(candidate.operands[1]);
      if (!lhs || !rhs) {
        return std::nullopt;
      }
      const auto left = std::bit_cast<float>(*lhs);
      const auto right = std::bit_cast<float>(*rhs);
      const auto result = name == "add" ? left + right
          : name == "subtract" ? left - right
          : name == "multiply" ? left * right
                                : left / right;
      return std::bit_cast<std::uint32_t>(result);
    }
    if ((name == "sqrt" || name == "abs")
        && candidate.operands.size() == 1) {
      const auto bits = evaluate(candidate.operands.front());
      if (!bits) {
        return std::nullopt;
      }
      const auto value = std::bit_cast<float>(*bits);
      return std::bit_cast<std::uint32_t>(
          name == "sqrt" ? std::sqrt(value) : std::fabs(value));
    }
    return std::nullopt;
  };

  if (float_result) {
    const auto bits = evaluate(expression);
    if (!bits) {
      report(
          root_name == "to_float" ? "FSIM-ELAB-VHFLT-001"
                                  : "FSIM-ELAB-VHFLT-003",
          "the bounded floating profile requires a locally static binary32 operation",
          expression.span);
      return std::nullopt;
    }
    if (expected_width != 32 || !float32_type(expected_type)) {
      report(
          "FSIM-ELAB-VHFLT-002",
          "the bounded floating profile requires float(8 downto -23) context for "
              + std::string{root_name} + " (received width "
              + std::to_string(expected_width) + ", type "
              + (expected_type == nullptr ? std::string{"<none>"}
                                          : expected_type->spelling)
              + ")",
          expression.span);
      return std::nullopt;
    }
    const auto result = allocate_register(
        32, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(LoadConstant{
        result, unsigned_value(*bits, 32).promoted_to_logic9()});
    return result;
  }

  const auto operand_bits = [&](const std::size_t index) {
    return index < expression.operands.size()
        ? evaluate(expression.operands[index])
        : std::optional<std::uint32_t>{};
  };
  if (packed_result) {
    const auto bits = operand_bits(0);
    if (!bits) {
      return ExpressionAttempt{};
    }
    if (expression.operands.size() != 1 || expected_width != 32) {
      report(
          "FSIM-ELAB-VHFLT-003",
          "binary32 vector conversion requires one locally static float operand",
          expression.span);
      return std::nullopt;
    }
    const auto result = allocate_register(
        32, frontend::ValueDomain::Logic9);
    process_.operations.emplace_back(LoadConstant{
        result, unsigned_value(*bits, 32).promoted_to_logic9()});
    return result;
  }
  if (integer_result) {
    const auto bits = operand_bits(0);
    if (expression.operands.size() != 1 || !bits) {
      return ExpressionAttempt{};
    }
    const auto value = std::bit_cast<float>(*bits);
    const auto integer_width = static_cast<std::size_t>(
        frontend::vhdl_predefined_integer_storage_width(vhdl_standard_));
    const auto rounded = std::nearbyint(static_cast<double>(value));
    const auto minimum = integer_width == 64U
        ? -9223372036854775808.0 : -2147483648.0;
    const auto exclusive_maximum = integer_width == 64U
        ? 9223372036854775808.0 : 2147483648.0;
    if (!std::isfinite(rounded) || rounded < minimum
        || rounded >= exclusive_maximum) {
      report(
          "FSIM-ELAB-VHFLT-004",
          "floating to_integer requires a finite result in the predefined "
          "integer range",
          expression.span);
      return std::nullopt;
    }
    const auto integer = static_cast<std::int64_t>(rounded);
    const auto result = allocate_register(
        integer_width, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant{
        result, integer_value(integer, integer_width)});
    return result;
  }

  const auto lhs = operand_bits(0);
  const auto rhs = expression.operands.size() > 1
      ? operand_bits(1) : lhs;
  const bool unary = root_name == "finite" || root_name == "isnan"
      || root_name == "is_negative";
  if (!lhs || !rhs || expression.operands.size() != (unary ? 1U : 2U)) {
    return ExpressionAttempt{};
  }
  const auto left = std::bit_cast<float>(*lhs);
  const auto right = std::bit_cast<float>(*rhs);
  const auto result_value = root_name == "finite" ? std::isfinite(left)
      : root_name == "isnan" ? std::isnan(left)
      : root_name == "is_negative" ? std::signbit(left)
      : root_name == "unordered" ? std::isunordered(left, right)
      : root_name == "eq" ? left == right
      : root_name == "ne" ? left != right
      : root_name == "lt" ? left < right
      : root_name == "le" ? left <= right
      : root_name == "gt" ? left > right
                           : left >= right;
  const auto result = allocate_register(
      1, frontend::ValueDomain::Boolean);
  process_.operations.emplace_back(LoadConstant{
      result, unsigned_value(result_value ? 1U : 0U, 1)});
  return result;
}

}  // namespace fsim::elaboration
