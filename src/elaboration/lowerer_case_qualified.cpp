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
  bool pattern_matching = false;
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
  case frontend::CaseMatchKind::Matches:
    pattern_matching = true;
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
        pattern_matching
            ? "FSIM-ELAB-SVMATCH-001"
            : "FSIM-ELAB-SVCASEQUAL-002",
        pattern_matching
            ? "case matches pattern matching requires SystemVerilog"
            : "case qualifiers require SystemVerilog",
        statement.span);
    return;
  }
  if ((inside_matching || pattern_matching)
      && (is_container_expression(statement.condition)
          || is_string_expression(statement.condition)
          || statement.condition.kind == ExpressionKind::Aggregate)) {
      report(
          pattern_matching
              ? "FSIM-ELAB-SVMATCH-002"
              : "FSIM-ELAB-SVCASEINSIDE-002",
          pattern_matching
              ? "bounded case matches requires a scalar integral selector"
              : "bounded case inside requires a scalar integral selector",
          statement.condition.span);
      return;
  }
  const auto inferred_selector_width = infer_width(statement.condition);
  if ((inside_matching || pattern_matching)
      && (!inferred_selector_width || *inferred_selector_width == 0)) {
    report(
        pattern_matching
            ? "FSIM-ELAB-SVMATCH-002"
            : "FSIM-ELAB-SVCASEINSIDE-002",
        pattern_matching
            ? "the case matches selector width is not statically inferable"
            : "the case inside selector width is not statically inferable",
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
  const InsideIntegralOperand selector_operand {
      *selector, register_width(*selector), selector_signed
  };

  const auto one = allocate_register(1, frontend::ValueDomain::Logic4);
  process_.operations.emplace_back(
      LoadConstant{one, PackedLogic4(1, Logic4::one)});
  const auto make_false = [&]() {
    const auto value = allocate_register(1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(
        LoadConstant{value, PackedLogic4(1, Logic4::zero)});
    return value;
  };
  const auto bind_patterns = [&](const auto& bindings) {
      for (const auto& binding : bindings) {
          locals_.insert_or_assign(binding.name, binding.value);
          local_signed_.insert_or_assign(
              binding.name, binding.signed_value);
          local_ranges_.insert_or_assign(
              binding.name, binding.packed_range);
          local_integer_ranges_.insert_or_assign(
              binding.name, binding.integer_range);
          local_members_.insert_or_assign(
              binding.name, binding.members);
          if (binding.type != nullptr) {
              local_types_.insert_or_assign(binding.name, binding.type);
          } else {
              local_types_.erase(binding.name);
          }
      }
  };
  const auto lower_choice =
      [&](const Expression& choice,
          std::vector<CasePatternBinding>& bindings)
      -> std::optional<RegisterId> {
      const Expression* match_choice = &choice;
      const Expression* match_guard = nullptr;
      if (pattern_matching && choice.kind == ExpressionKind::Call
          && choice.text == "@match-guard") {
          if (choice.operands.size() != 2U) {
              report(
                  "FSIM-ELAB-SVMATCH-005",
                  "guarded case matches HIR requires one pattern and one guard",
                  choice.span);
              return std::nullopt;
          }
          match_choice = &choice.operands[0];
          match_guard = &choice.operands[1];
      }
      const auto apply_guard =
          [&](const RegisterId raw_match) -> std::optional<RegisterId> {
          if (match_guard == nullptr) {
              return raw_match;
          }
          const auto guarded = make_false();
          const auto match_branch = static_cast<InstructionIndex>(
              process_.operations.size());
          process_.operations.emplace_back(Branch {
              raw_match, 0, 0, UnknownBranchPolicy::when_false });
          const auto guard_start = static_cast<InstructionIndex>(
              process_.operations.size());
          const auto guard = lower_condition(
              *match_guard,
              "FSIM-ELAB-SVMATCH-006",
              "case matches guard");
          if (!guard) {
              return std::nullopt;
          }
          const auto definite = allocate_register(
              1, frontend::ValueDomain::Bit2);
          process_.operations.emplace_back(Binary {
              BinaryOperator::case_equal, definite, *guard, one });
          process_.operations.emplace_back(
              CopyRegister { guarded, definite });
          const auto guard_end = static_cast<InstructionIndex>(
              process_.operations.size());
          process_.operations[match_branch] = Branch {
              raw_match,
              guard_start,
              guard_end,
              UnknownBranchPolicy::when_false
          };
          return guarded;
      };
      if (pattern_matching) {
          auto pattern = lower_case_match_pattern(
              *match_choice,
              *selector,
              selector_width,
              selector_signed,
              selector_type);
          if (!pattern) {
              return std::nullopt;
          }
          bindings = std::move(pattern->bindings);
          bind_patterns(bindings);
          return apply_guard(pattern->condition);
      }
      if (inside_matching && match_choice->kind == ExpressionKind::Call
          && match_choice->text == "@inside-range") {
          if (match_choice->operands.size() != 2) {
              report(
                  "FSIM-ELAB-SVCASEINSIDE-006",
                  "a case inside range requires exactly one low and high bound",
                  match_choice->span);
              return std::nullopt;
          }
          const auto low = lower_inside_integral_operand(
              match_choice->operands[0],
              "FSIM-ELAB-SVCASEINSIDE-003",
              "case inside range bounds must be integral expressions");
          const auto high = lower_inside_integral_operand(
              match_choice->operands[1],
              "FSIM-ELAB-SVCASEINSIDE-003",
              "case inside range bounds must be integral expressions");
          if (!low || !high) {
              return std::nullopt;
          }
          const auto valid_operands = size_integral_comparison(*low, *high);
          const auto low_operands = size_integral_comparison(selector_operand, *low);
          const auto high_operands = size_integral_comparison(selector_operand, *high);
          const auto domain = frontend::ValueDomain::Logic4;
          const auto valid = allocate_register(1, domain);
          const auto above_low = allocate_register(1, domain);
          const auto below_high = allocate_register(1, domain);
          const auto within_lower = allocate_register(1, domain);
          const auto matched = allocate_register(1, domain);
          process_.operations.emplace_back(Binary {
              valid_operands.signed_value
                  ? BinaryOperator::less_equal_signed
                  : BinaryOperator::less_equal_unsigned,
              valid, valid_operands.lhs, valid_operands.rhs });
          process_.operations.emplace_back(Binary {
              low_operands.signed_value
                  ? BinaryOperator::greater_equal_signed
                  : BinaryOperator::greater_equal_unsigned,
              above_low, low_operands.lhs, low_operands.rhs });
          process_.operations.emplace_back(Binary {
              high_operands.signed_value
                  ? BinaryOperator::less_equal_signed
                  : BinaryOperator::less_equal_unsigned,
              below_high, high_operands.lhs, high_operands.rhs });
          process_.operations.emplace_back(LogicalBinary {
              LogicalBinaryOperator::logical_and,
              within_lower, valid, above_low });
          process_.operations.emplace_back(LogicalBinary {
              LogicalBinaryOperator::logical_and,
              matched, within_lower, below_high });
          return matched;
      }
      std::optional<RegisterId> choice_register;
      auto comparison_selector = *selector;
      if (inside_matching) {
          const auto operand = lower_inside_integral_operand(
              *match_choice,
              "FSIM-ELAB-SVCASEINSIDE-003",
              "case inside choices must be integral expressions");
          if (operand) {
              const auto comparison = size_integral_comparison(selector_operand, *operand);
              comparison_selector = comparison.lhs;
              choice_register = comparison.rhs;
          }
      } else {
          choice_register = lower_expression(
              *match_choice,
              register_width(*selector),
              selector_type != nullptr
                      && !selector_type->enumeration_literals.empty()
                  ? selector_type
                  : nullptr);
      }
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
              match_choice->span);
          return std::nullopt;
      }
      const auto matched = allocate_register(
          1,
          inside_matching ? frontend::ValueDomain::Logic4
                          : frontend::ValueDomain::Bit2);
      process_.operations.emplace_back(Binary {
          match_operation, matched, comparison_selector, *choice_register });
      return matched;
  };

  struct QualifiedAlternative {
    const frontend::CaseAlternative* source{};
    RegisterId matched{};
    std::vector<CasePatternBinding> bindings;
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
    if (pattern_matching && alternative.choices.size() != 1) {
      report(
          "FSIM-ELAB-SVMATCH-005",
          "a case matches item requires exactly one pattern",
          alternative.span);
      continue;
    }
    auto alternative_match = make_false();
    std::vector<CasePatternBinding> alternative_bindings;
    for (const auto& choice : alternative.choices) {
        auto outer_locals = locals_;
        auto outer_signed = local_signed_;
        auto outer_ranges = local_ranges_;
        auto outer_integer_ranges = local_integer_ranges_;
        auto outer_members = local_members_;
        auto outer_types = local_types_;
        std::vector<CasePatternBinding> choice_bindings;
        const auto raw_match = lower_choice(choice, choice_bindings);
        locals_ = std::move(outer_locals);
        local_signed_ = std::move(outer_signed);
        local_ranges_ = std::move(outer_ranges);
        local_integer_ranges_ = std::move(outer_integer_ranges);
        local_members_ = std::move(outer_members);
        local_types_ = std::move(outer_types);
        if (!raw_match) {
            continue;
        }
        alternative_bindings = std::move(choice_bindings);
        const auto definite = allocate_register(1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::case_equal, definite, *raw_match, one });
        const auto merged = allocate_register(1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary {
            BinaryOperator::bit_or,
            merged, alternative_match, definite });
        alternative_match = merged;
    }
    alternatives.push_back(
        QualifiedAlternative {
            &alternative,
            alternative_match,
            std::move(alternative_bindings) });
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
      statement.span.source_name.str(),
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
    auto outer_locals = locals_;
    auto outer_signed = local_signed_;
    auto outer_ranges = local_ranges_;
    auto outer_integer_ranges = local_integer_ranges_;
    auto outer_members = local_members_;
    auto outer_types = local_types_;
    bind_patterns(alternative.bindings);
    lower_statements(alternative.source->statements);
    locals_ = std::move(outer_locals);
    local_signed_ = std::move(outer_signed);
    local_ranges_ = std::move(outer_ranges);
    local_integer_ranges_ = std::move(outer_integer_ranges);
    local_members_ = std::move(outer_members);
    local_types_ = std::move(outer_types);
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
