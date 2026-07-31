// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

Lowerer::ExpressionAttempt Lowerer::lower_membership_expression(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Call
      || expression.text != "inside") {
    return ExpressionAttempt{};
  }
  if (language_ != frontend::Language::SystemVerilog2017) {
    report(
        "FSIM-ELAB-SVMEMBER-001",
        "the inside membership operator requires SystemVerilog",
        expression.span);
    return std::nullopt;
  }
  if (expression.operands.size() < 2) {
    report(
        "FSIM-ELAB-SVMEMBER-002",
        "an inside expression requires a left operand and a nonempty list",
        expression.span);
    return std::nullopt;
  }
  const auto& lhs_expression = expression.operands.front();
  if (is_container_expression(lhs_expression)
      || is_string_expression(lhs_expression)
      || lhs_expression.kind == ExpressionKind::Aggregate
      || lhs_expression.kind == ExpressionKind::Concatenation) {
    report(
        "FSIM-ELAB-SVMEMBER-003",
        "bounded inside membership requires scalar integral operands",
        lhs_expression.span);
    return std::nullopt;
  }
  const auto lhs_width = infer_width(lhs_expression);
  if (!lhs_width || *lhs_width == 0) {
    report(
        "FSIM-ELAB-SVMEMBER-003",
        "the inside left operand width is not statically inferable",
        lhs_expression.span);
    return std::nullopt;
  }
  const auto lhs = lower_expression(lhs_expression, *lhs_width);
  if (!lhs) {
    return std::nullopt;
  }
  const bool lhs_signed = is_signed_expression(lhs_expression);
  const auto result_domain = frontend::ValueDomain::Logic4;
  const auto result = allocate_register(1, result_domain);
  process_.operations.emplace_back(LoadConstant{
      result, PackedLogic4(1, Logic4::zero)});
  std::vector<InstructionIndex> matched_branches;

  const auto lower_compatible =
      [&](const Expression& operand)
          -> std::optional<RegisterId> {
        if (is_container_expression(operand)
            || is_string_expression(operand)
            || operand.kind == ExpressionKind::Aggregate
            || operand.kind == ExpressionKind::Concatenation
            || (operand.kind == ExpressionKind::Call
                && (operand.text == "inside"
                    || operand.text == "@inside-range"))) {
          report(
              "FSIM-ELAB-SVMEMBER-004",
              "inside members must be nonnested scalar integral values",
              operand.span);
          return std::nullopt;
        }
        const auto width = infer_width(operand);
        if (!width || *width != *lhs_width
            || is_signed_expression(operand) != lhs_signed) {
          report(
              "FSIM-ELAB-SVMEMBER-005",
              "inside members and range bounds must exactly match the "
              "left operand width and signedness",
              operand.span);
          return std::nullopt;
        }
        auto lowered = lower_expression(operand, *lhs_width);
        if (!lowered) {
          return std::nullopt;
        }
        return lowered;
      };

  for (std::size_t item_index = 1;
       item_index < expression.operands.size(); ++item_index) {
    const auto& item = expression.operands[item_index];
    std::optional<RegisterId> matched;
    if (item.kind == ExpressionKind::Call
        && item.text == "@inside-range") {
      if (item.operands.size() != 2) {
        report(
            "FSIM-ELAB-SVMEMBER-006",
            "an inside range requires exactly one low and high bound",
            item.span);
        return std::nullopt;
      }
      const auto low = lower_compatible(item.operands[0]);
      const auto high = lower_compatible(item.operands[1]);
      if (!low || !high) {
        return std::nullopt;
      }
      const auto valid = allocate_register(1, result_domain);
      const auto above_low = allocate_register(1, result_domain);
      const auto below_high = allocate_register(1, result_domain);
      const auto within_lower = allocate_register(1, result_domain);
      matched = allocate_register(1, result_domain);
      process_.operations.emplace_back(Binary{
          lhs_signed ? BinaryOperator::less_equal_signed
                     : BinaryOperator::less_equal_unsigned,
          valid, *low, *high});
      process_.operations.emplace_back(Binary{
          lhs_signed ? BinaryOperator::greater_equal_signed
                     : BinaryOperator::greater_equal_unsigned,
          above_low, *lhs, *low});
      process_.operations.emplace_back(Binary{
          lhs_signed ? BinaryOperator::less_equal_signed
                     : BinaryOperator::less_equal_unsigned,
          below_high, *lhs, *high});
      process_.operations.emplace_back(LogicalBinary{
          LogicalBinaryOperator::logical_and,
          within_lower, valid, above_low});
      process_.operations.emplace_back(LogicalBinary{
          LogicalBinaryOperator::logical_and,
          *matched, within_lower, below_high});
    } else {
      const auto value = lower_compatible(item);
      if (!value) {
        return std::nullopt;
      }
      matched = allocate_register(1, result_domain);
      process_.operations.emplace_back(Binary{
          BinaryOperator::wildcard_equal,
          *matched, *lhs, *value});
    }
    const auto accumulated = allocate_register(1, result_domain);
    process_.operations.emplace_back(LogicalBinary{
        LogicalBinaryOperator::logical_or,
        accumulated, result, *matched});
    process_.operations.emplace_back(CopyRegister{result, accumulated});
    if (item_index + 1 < expression.operands.size()) {
      const auto branch = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(Branch{
          result, 0, branch + 1,
          UnknownBranchPolicy::when_false});
      matched_branches.push_back(branch);
    }
  }
  const auto end = static_cast<InstructionIndex>(
      process_.operations.size());
  for (const auto branch : matched_branches) {
    process_.operations[branch] = Branch{
        result, end, branch + 1,
        UnknownBranchPolicy::when_false};
  }
  return result;
}

}  // namespace fsim::elaboration
