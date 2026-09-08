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
    if (register_width(*source) == 1
        && register_domain(*source) == frontend::ValueDomain::Boolean) {
      return source;
    }
    if (vhdl_standard_ == frontend::VhdlStandard::Vhdl2019
        && register_width(*source) == 1
        && (register_domain(*source) == frontend::ValueDomain::Bit2
            || register_domain(*source) == frontend::ValueDomain::Logic4
            || register_domain(*source) == frontend::ValueDomain::Logic9)) {
      const auto result = allocate_register(
          1, frontend::ValueDomain::Boolean);
      const auto one = allocate_register(1, register_domain(*source));
      process_.operations.emplace_back(LoadConstant{
          one, PackedLogic4(1, Logic4::one)});
      process_.operations.emplace_back(Binary{
          register_domain(*source) == frontend::ValueDomain::Logic9
              ? BinaryOperator::vhdl_match_equal
              : BinaryOperator::case_equal,
          result,
          *source,
          one});
      return result;
    }
    report(
        std::move(diagnostic_code),
        "a VHDL " + std::string{construct}
            + " condition must have type boolean",
        expression.span);
    return std::nullopt;
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

std::optional<RegisterId> Lowerer::lower_vhdl_conditional_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type* expected_type) {
  const bool first_class =
      expression.kind == ExpressionKind::Conditional;
  const bool assignment_form =
      expression.kind == ExpressionKind::Call
      && expression.text == "?:";
  if (language_ != frontend::Language::Vhdl2008
      || (!first_class && !assignment_form)
      || expression.operands.size() != 3U) {
    report(
        "FSIM-ELAB-VHCOND-001",
        "a VHDL conditional expression requires one condition and "
        "exactly two candidate results",
        expression.span);
    return std::nullopt;
  }
  if (first_class
      && vhdl_standard_ != frontend::VhdlStandard::Vhdl2019) {
    report(
        "FSIM-ELAB-VHCOND-001",
        "a first-class conditional expression requires VHDL-2019",
        expression.span);
    return std::nullopt;
  }

  const auto& condition_expression = expression.operands[0];
  const auto& when_true_expression = expression.operands[1];
  const auto& when_false_expression = expression.operands[2];
  auto when_true_type = vhdl_expression_type(when_true_expression);
  auto when_false_type = vhdl_expression_type(when_false_expression);
  const auto same_base_type = [](const frontend::Type& left,
                                 const frontend::Type& right) {
    if (left.domain != right.domain
        || left.is_signed != right.is_signed) {
      return false;
    }
    if (!left.nominal_type.empty() || !right.nominal_type.empty()) {
      return !left.nominal_type.empty()
          && left.nominal_type == right.nominal_type;
    }
    if (left.vhdl_array || right.vhdl_array
        || !left.packed_members.empty()
        || !right.packed_members.empty()) {
      return left.spelling == right.spelling;
    }
    return true;
  };

  const frontend::Type* context = expected_type;
  std::optional<frontend::Type> inferred_context;
  if (context == nullptr) {
    if (when_true_type && when_false_type) {
      if (!same_base_type(*when_true_type, *when_false_type)) {
        report(
            "FSIM-ELAB-VHCOND-002",
            "VHDL conditional-expression candidate results must "
            "have one common base type",
            expression.span);
        return std::nullopt;
      }
      inferred_context = *when_true_type;
    } else if (when_true_type) {
      inferred_context = *when_true_type;
    } else if (when_false_type) {
      inferred_context = *when_false_type;
    }
    context = inferred_context ? &*inferred_context : nullptr;
  }
  if (context == nullptr
      || !vhdl_expression_matches_type(when_true_expression, *context)
      || !vhdl_expression_matches_type(when_false_expression, *context)
      || (when_true_type && when_false_type
          && !same_base_type(*when_true_type, *when_false_type))) {
    report(
        "FSIM-ELAB-VHCOND-002",
        "VHDL conditional-expression candidate results must resolve "
        "to the surrounding common base type",
        expression.span);
    return std::nullopt;
  }

  const auto context_width = context->width();
  const auto true_width = infer_width(when_true_expression);
  const auto false_width = infer_width(when_false_expression);
  const auto value_width = expected_width != 0U
      ? expected_width
      : context_width.value_or(
          true_width.value_or(false_width.value_or(0U)));
  if (value_width == 0U) {
    report(
        "FSIM-ELAB-VHCOND-002",
        "the contextual width of a VHDL conditional expression "
        "cannot be determined",
        expression.span);
    return std::nullopt;
  }

  const auto condition = lower_condition(
      condition_expression,
      first_class ? "FSIM-ELAB-VHCOND-003" : "FSIM-ELAB-092",
      first_class ? "conditional-expression" : "conditional-assignment");
  if (!condition) {
    return std::nullopt;
  }
  const auto result_domain = context->domain
          != frontend::ValueDomain::Unknown
      ? context->domain
      : frontend::ValueDomain::Logic4;
  const auto destination = allocate_register(value_width, result_domain);
  const auto branch_index = static_cast<InstructionIndex>(
      process_.operations.size());
  process_.operations.emplace_back(Branch{
      *condition, 0, 0, UnknownBranchPolicy::error});

  const auto true_start = static_cast<InstructionIndex>(
      process_.operations.size());
  const auto when_true = lower_expression(
      when_true_expression, value_width, context);
  if (!when_true || register_width(*when_true) != value_width) {
    report(
        "FSIM-ELAB-VHCOND-002",
        "the selected VHDL conditional-expression result does not "
        "match its contextual width",
        when_true_expression.span);
    return std::nullopt;
  }
  process_.operations.emplace_back(CopyRegister{destination, *when_true});
  const auto true_exit = static_cast<InstructionIndex>(
      process_.operations.size());
  process_.operations.emplace_back(Jump{0});

  const auto false_start = static_cast<InstructionIndex>(
      process_.operations.size());
  const auto when_false = lower_expression(
      when_false_expression, value_width, context);
  if (!when_false || register_width(*when_false) != value_width) {
    report(
        "FSIM-ELAB-VHCOND-002",
        "the final VHDL conditional-expression result does not "
        "match its contextual width",
        when_false_expression.span);
    return std::nullopt;
  }
  process_.operations.emplace_back(CopyRegister{destination, *when_false});
  const auto end = static_cast<InstructionIndex>(
      process_.operations.size());
  process_.operations[branch_index] = Branch{
      *condition, true_start, false_start, UnknownBranchPolicy::error};
  process_.operations[true_exit] = Jump{end};
  return destination;
}

}  // namespace fsim::elaboration
