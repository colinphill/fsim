// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void Lowerer::lower_qualified_case(const Statement& statement) {
  std::string qualifier_name;
  bool diagnose_multiple = false;
  bool diagnose_no_match = false;
  switch (statement.case_qualifier) {
  case frontend::CaseQualifier::Unique:
    qualifier_name = "unique";
    diagnose_multiple = true;
    diagnose_no_match = true;
    break;
  case frontend::CaseQualifier::Unique0:
    qualifier_name = "unique0";
    diagnose_multiple = true;
    break;
  case frontend::CaseQualifier::Priority:
    qualifier_name = "priority";
    diagnose_no_match = true;
    break;
  case frontend::CaseQualifier::None:
  default:
    report(
        "FSIM-ELAB-SVCASEQUAL-001",
        "case statement has an invalid qualifier",
        statement.span);
    return;
  }

  BinaryOperator match_operation = BinaryOperator::case_equal;
  bool inside_matching = false;
  switch (statement.case_match_kind) {
  case frontend::CaseMatchKind::Exact:
    break;
  case frontend::CaseMatchKind::WildcardZ:
    match_operation = BinaryOperator::casez_equal;
    break;
  case frontend::CaseMatchKind::WildcardXZ:
    match_operation = BinaryOperator::casex_equal;
    break;
  case frontend::CaseMatchKind::Inside:
    inside_matching = true;
    match_operation = BinaryOperator::wildcard_equal;
    break;
  default:
    report(
        "FSIM-ELAB-081",
        "case statement has an invalid matching mode",
        statement.span);
    return;
  }
  if (language_ != frontend::Language::SystemVerilog2017) {
    report(
        "FSIM-ELAB-SVCASEQUAL-002",
        "case qualifiers require SystemVerilog",
        statement.span);
    return;
  }
  if (inside_matching
      && (is_container_expression(statement.condition)
          || is_string_expression(statement.condition)
          || statement.condition.kind == ExpressionKind::Aggregate
          || statement.condition.kind == ExpressionKind::Concatenation)) {
    report(
        "FSIM-ELAB-SVCASEINSIDE-002",
        "bounded case inside requires a scalar integral selector",
        statement.condition.span);
    return;
  }
  const auto inferred_selector_width = infer_width(statement.condition);
  if (inside_matching
      && (!inferred_selector_width || *inferred_selector_width == 0)) {
    report(
        "FSIM-ELAB-SVCASEINSIDE-002",
        "the case inside selector width is not statically inferable",
        statement.condition.span);
    return;
  }
  const auto selector_width = inferred_selector_width.value_or(
      std::size_t{1});
  const bool selector_signed = is_signed_expression(statement.condition);
  const auto* selector_type =
      statement.condition.kind == ExpressionKind::Identifier
          ? object_type(statement.condition.text)
          : nullptr;
  const auto selector = lower_expression(
      statement.condition,
      selector_width,
      selector_type != nullptr
              && !selector_type->enumeration_literals.empty()
          ? selector_type
          : nullptr);
  if (!selector) {
    return;
  }

  const auto one = allocate_register(1, frontend::ValueDomain::Logic4);
  process_.operations.emplace_back(
      LoadConstant{one, PackedLogic4(1, Logic4::one)});
  const auto make_false = [&]() {
    const auto value = allocate_register(1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(
        LoadConstant{value, PackedLogic4(1, Logic4::zero)});
    return value;
  };
  const auto lower_inside_operand =
      [&](const Expression& operand) -> std::optional<RegisterId> {
    if (is_container_expression(operand)
        || is_string_expression(operand)
        || operand.kind == ExpressionKind::Aggregate
        || operand.kind == ExpressionKind::Concatenation
        || (operand.kind == ExpressionKind::Call
            && (operand.text == "inside"
                || operand.text == "@inside-range"))) {
      report(
          "FSIM-ELAB-SVCASEINSIDE-003",
          "case inside choices must be nonnested scalar integral values",
          operand.span);
      return std::nullopt;
    }
    const auto width = infer_width(operand);
    if (!width || *width != selector_width
        || is_signed_expression(operand) != selector_signed) {
      report(
          "FSIM-ELAB-SVCASEINSIDE-004",
          "case inside choices and range bounds must exactly match the "
          "selector width and signedness",
          operand.span);
      return std::nullopt;
    }
    return lower_expression(operand, selector_width);
  };
  const auto lower_choice =
      [&](const Expression& choice) -> std::optional<RegisterId> {
    if (inside_matching && choice.kind == ExpressionKind::Call
        && choice.text == "@inside-range") {
      if (choice.operands.size() != 2) {
        report(
            "FSIM-ELAB-SVCASEINSIDE-006",
            "a case inside range requires exactly one low and high bound",
            choice.span);
        return std::nullopt;
      }
      const auto low = lower_inside_operand(choice.operands[0]);
      const auto high = lower_inside_operand(choice.operands[1]);
      if (!low || !high) {
        return std::nullopt;
      }
      const auto domain = frontend::ValueDomain::Logic4;
      const auto valid = allocate_register(1, domain);
      const auto above_low = allocate_register(1, domain);
      const auto below_high = allocate_register(1, domain);
      const auto within_lower = allocate_register(1, domain);
      const auto matched = allocate_register(1, domain);
      process_.operations.emplace_back(Binary{
          selector_signed ? BinaryOperator::less_equal_signed
                          : BinaryOperator::less_equal_unsigned,
          valid, *low, *high});
      process_.operations.emplace_back(Binary{
          selector_signed ? BinaryOperator::greater_equal_signed
                          : BinaryOperator::greater_equal_unsigned,
          above_low, *selector, *low});
      process_.operations.emplace_back(Binary{
          selector_signed ? BinaryOperator::less_equal_signed
                          : BinaryOperator::less_equal_unsigned,
          below_high, *selector, *high});
      process_.operations.emplace_back(LogicalBinary{
          LogicalBinaryOperator::logical_and,
          within_lower, valid, above_low});
      process_.operations.emplace_back(LogicalBinary{
          LogicalBinaryOperator::logical_and,
          matched, within_lower, below_high});
      return matched;
    }
    const auto choice_register = inside_matching
        ? lower_inside_operand(choice)
        : lower_expression(
              choice,
              register_width(*selector),
              selector_type != nullptr
                      && !selector_type->enumeration_literals.empty()
                  ? selector_type
                  : nullptr);
    if (!choice_register) {
      return std::nullopt;
    }
    if (!inside_matching
        && register_width(*choice_register)
            != register_width(*selector)) {
      report(
          "FSIM-ELAB-063",
          "case item width "
              + std::to_string(register_width(*choice_register))
              + " does not match selector width "
              + std::to_string(register_width(*selector)),
          choice.span);
      return std::nullopt;
    }
    const auto matched = allocate_register(
        1,
        inside_matching ? frontend::ValueDomain::Logic4
                        : frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary{
        match_operation, matched, *selector, *choice_register});
    return matched;
  };

  struct QualifiedAlternative {
    const frontend::CaseAlternative* source{};
    RegisterId matched{};
  };
  std::vector<QualifiedAlternative> alternatives;
  const frontend::CaseAlternative* default_alternative = nullptr;
  auto any_match = make_false();
  auto multiple_match = make_false();
  for (const auto& alternative : statement.case_alternatives) {
    if (alternative.is_default) {
      default_alternative = &alternative;
      continue;
    }
    if (inside_matching && alternative.choices.empty()) {
      report(
          "FSIM-ELAB-SVCASEINSIDE-005",
          "a case inside alternative requires at least one choice",
          alternative.span);
    }
    auto alternative_match = make_false();
    for (const auto& choice : alternative.choices) {
      const auto raw_match = lower_choice(choice);
      if (!raw_match) {
        continue;
      }
      const auto definite =
          allocate_register(1, frontend::ValueDomain::Bit2);
      process_.operations.emplace_back(Binary{
          BinaryOperator::case_equal, definite, *raw_match, one});
      const auto merged =
          allocate_register(1, frontend::ValueDomain::Bit2);
      process_.operations.emplace_back(Binary{
          BinaryOperator::bit_or,
          merged, alternative_match, definite});
      alternative_match = merged;
    }
    alternatives.push_back(
        QualifiedAlternative{&alternative, alternative_match});
    const auto overlap =
        allocate_register(1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary{
        BinaryOperator::bit_and,
        overlap, any_match, alternative_match});
    const auto next_multiple =
        allocate_register(1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary{
        BinaryOperator::bit_or,
        next_multiple, multiple_match, overlap});
    multiple_match = next_multiple;
    const auto next_any =
        allocate_register(1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary{
        BinaryOperator::bit_or,
        next_any, any_match, alternative_match});
    any_match = next_any;
  }

  const auto source = SourceLocation{
      statement.span.source_name,
      static_cast<std::uint32_t>(statement.span.begin.line),
      static_cast<std::uint32_t>(statement.span.begin.column)};
  const auto warn_if = [&](const RegisterId condition,
                           const std::string& message) {
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        condition, 0, 0, UnknownBranchPolicy::when_false});
    const auto warning = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Report{
        message, AssertionSeverity::warning, source});
    const auto next = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[branch] = Branch{
        condition, warning, next, UnknownBranchPolicy::when_false};
  };
  if (diagnose_multiple) {
    warn_if(
        multiple_match,
        qualifier_name + " case has multiple matching items");
  }
  if (diagnose_no_match && default_alternative == nullptr) {
    const auto no_match =
        allocate_register(1, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(LogicalNot{no_match, any_match});
    warn_if(
        no_match,
        qualifier_name + " case has no matching item");
  }

  std::vector<InstructionIndex> exit_jumps;
  for (const auto& alternative : alternatives) {
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        alternative.matched, 0, 0,
        UnknownBranchPolicy::when_false});
    const auto body = static_cast<InstructionIndex>(
        process_.operations.size());
    lower_statements(alternative.source->statements);
    exit_jumps.push_back(static_cast<InstructionIndex>(
        process_.operations.size()));
    process_.operations.emplace_back(Jump{0});
    const auto next = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[branch] = Branch{
        alternative.matched, body, next,
        UnknownBranchPolicy::when_false};
  }
  if (default_alternative != nullptr) {
    lower_statements(default_alternative->statements);
  }
  const auto end = static_cast<InstructionIndex>(
      process_.operations.size());
  for (const auto jump : exit_jumps) {
    process_.operations[jump] = Jump{end};
  }
}

}  // namespace fsim::elaboration
