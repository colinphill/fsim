// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<RegisterId> Lowerer::lower_condition(
    const Expression& expression,
    std::string diagnostic_code,
    const std::string_view construct) {
  if (language_ == frontend::Language::Vhdl2008
      && expression.kind == ExpressionKind::Binary
      && expression.operands.size() == 2
      && (expression.text == "and"
          || expression.text == "nand"
          || expression.text == "or"
          || expression.text == "nor")) {
    Expression call{
        ExpressionKind::Call,
        expression.text,
        expression.operands,
        expression.span};
    const auto found = function_indices_.find(expression.text);
    const bool user_operator = found != function_indices_.end()
        && std::ranges::any_of(
            found->second,
            [&](const std::size_t index) {
              return vhdl_function_profile_matches(
                  call, *function_frames_[index].source, nullptr);
            });
    if (!user_operator) {
      const auto left = lower_condition(
          expression.operands[0], diagnostic_code, construct);
      if (!left) {
        return std::nullopt;
      }
      const auto first_branch = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(Branch{
          *left, 0, 0, UnknownBranchPolicy::error});

      const auto right_start = static_cast<InstructionIndex>(
          process_.operations.size());
      const auto right = lower_condition(
          expression.operands[1], diagnostic_code, construct);
      if (!right) {
        return std::nullopt;
      }
      const auto second_branch = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(Branch{
          *right, 0, 0, UnknownBranchPolicy::error});

      const auto result = allocate_register(
          1, frontend::ValueDomain::Boolean);
      const bool invert = expression.text == "nand"
          || expression.text == "nor";
      const auto true_start = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(LoadConstant{
          result, unsigned_value(invert ? 0U : 1U, 1U)});
      const auto finished_jump = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(Jump{0});
      const auto false_start = static_cast<InstructionIndex>(
          process_.operations.size());
      process_.operations.emplace_back(LoadConstant{
          result, unsigned_value(invert ? 1U : 0U, 1U)});
      const auto end = static_cast<InstructionIndex>(
          process_.operations.size());

      process_.operations[second_branch] = Branch{
          *right,
          true_start,
          false_start,
          UnknownBranchPolicy::error};
      const bool and_family = expression.text == "and"
          || expression.text == "nand";
      process_.operations[first_branch] = Branch{
          *left,
          and_family ? right_start : true_start,
          and_family ? false_start : right_start,
          UnknownBranchPolicy::error};
      process_.operations[finished_jump] = Jump{end};
      return result;
    }
  }
  const auto expression_width =
      infer_width(expression).value_or(std::size_t{1});
  const auto source = lower_expression(expression, expression_width);
  if (!source) {
    return std::nullopt;
  }
  if (language_ == frontend::Language::Vhdl2008) {
    if (register_width(*source) != 1
        || register_domain(*source) != frontend::ValueDomain::Boolean) {
      report(
          std::move(diagnostic_code),
          "a VHDL " + std::string{construct}
              + " condition must have type boolean",
          expression.span);
      return std::nullopt;
    }
    return source;
  }

  // Applying logical negation twice retains X/Z truth while normalizing the
  // complete SystemVerilog expression to a scalar branch condition.
  const auto truth_domain =
      is_two_state_domain(register_domain(*source))
          ? frontend::ValueDomain::Bit2
          : frontend::ValueDomain::Logic4;
  const auto inverted = allocate_register(1, truth_domain);
  process_.operations.emplace_back(LogicalNot{inverted, *source});
  const auto normalized = allocate_register(1, truth_domain);
  process_.operations.emplace_back(LogicalNot{normalized, inverted});
  return normalized;
}

}  // namespace fsim::elaboration
