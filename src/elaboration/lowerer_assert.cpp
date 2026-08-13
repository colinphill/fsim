// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

std::optional<AssertionSeverity> predefined_severity(
    const Expression& expression) {
  if (expression.kind != ExpressionKind::Identifier) {
    return std::nullopt;
  }
  if (expression.text == "note") return AssertionSeverity::note;
  if (expression.text == "warning") return AssertionSeverity::warning;
  if (expression.text == "error") return AssertionSeverity::error;
  if (expression.text == "failure") return AssertionSeverity::failure;
  return std::nullopt;
}

std::uint64_t severity_ordinal(const AssertionSeverity severity) {
  return static_cast<std::uint64_t>(severity);
}

}  // namespace

void Lowerer::lower_assert(const Statement& statement) {
  if (statement.kind == StatementKind::Assert
      && language_ == frontend::Language::SystemVerilog2017) {
    const bool concurrent = statement.assertion_message.starts_with(
        "concurrent assertion '");
    const auto condition = lower_condition(
        statement.condition, "FSIM-ELAB-051", "assertion");
    if (!condition) {
      return;
    }
    const auto branch_index = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        *condition, 0, 0, UnknownBranchPolicy::when_false});
    const auto pass_start = static_cast<InstructionIndex>(
        process_.operations.size());
    if (concurrent) {
      process_.operations.emplace_back(
          WaitRegion{runtime::SchedulerPhase::reactive});
    }
    lower_statements(statement.statements);
    const auto jump_index = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Jump{0});
    const auto failure_start = static_cast<InstructionIndex>(
        process_.operations.size());
    if (concurrent) {
      process_.operations.emplace_back(
          WaitRegion{runtime::SchedulerPhase::reactive});
    }
    if (statement.assertion_has_failure_action) {
      lower_statements(statement.else_statements);
    } else {
      process_.operations.emplace_back(runtime::simir::Report{
          statement.assertion_message.empty()
              ? "assertion failed"
              : statement.assertion_message,
          AssertionSeverity::error,
          SourceLocation{
              statement.span.source_name,
              static_cast<std::uint32_t>(statement.span.begin.line),
              static_cast<std::uint32_t>(statement.span.begin.column)}});
    }
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[branch_index] = Branch{
        *condition,
        pass_start,
        failure_start,
        UnknownBranchPolicy::when_false};
    process_.operations[jump_index] = Jump{end};
    return;
  }

  const bool standalone = statement.kind == StatementKind::Report;
  auto default_severity = standalone
      ? AssertionSeverity::note
      : AssertionSeverity::error;
  if (standalone
      && language_ == frontend::Language::SystemVerilog2017) {
    default_severity = static_cast<AssertionSeverity>(
        statement.assertion_severity);
  }
  const auto static_severity =
      statement.vhdl_severity_expression.valid()
          ? predefined_severity(statement.vhdl_severity_expression)
          : std::optional<AssertionSeverity>{default_severity};
  const bool static_message =
      !statement.vhdl_report_expression.valid()
      || statement.vhdl_report_expression.kind
          == ExpressionKind::StringLiteral;

  std::optional<RegisterId> condition;
  if (!standalone) {
    condition = lower_condition(
        statement.condition, "FSIM-ELAB-051", "assertion");
    if (!condition) {
      return;
    }
    if (static_message && static_severity) {
      process_.operations.emplace_back(Assert{
          *condition,
          statement.assertion_message,
          *static_severity,
          SourceLocation{
              statement.span.source_name,
              static_cast<std::uint32_t>(statement.span.begin.line),
              static_cast<std::uint32_t>(statement.span.begin.column)}});
      return;
    }
  } else if (static_message && static_severity) {
    process_.operations.emplace_back(runtime::simir::Report{
        statement.output_text,
        *static_severity,
        SourceLocation{
            statement.span.source_name,
            static_cast<std::uint32_t>(statement.span.begin.line),
            static_cast<std::uint32_t>(statement.span.begin.column)}});
    return;
  }

  std::optional<InstructionIndex> pass_branch;
  if (condition) {
    pass_branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        *condition, 0, *pass_branch + 1,
        UnknownBranchPolicy::when_false});
  }

  std::optional<StringRegisterId> message;
  if (statement.vhdl_report_expression.valid()) {
    if (!is_string_expression(statement.vhdl_report_expression)) {
      report(
          "FSIM-ELAB-VHREPORT-001",
          "a VHDL report expression must have string type",
          statement.vhdl_report_expression.span);
    } else {
      message = lower_string_expression(
          statement.vhdl_report_expression);
    }
  } else {
    message = allocate_string_register();
    process_.operations.emplace_back(LoadStringConstant{
        *message, standalone ? std::string{} : "assertion failed"});
  }

  std::optional<RegisterId> severity_register;
  if (static_severity) {
    severity_register = allocate_register(
        2, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(LoadConstant{
        *severity_register,
        unsigned_value(severity_ordinal(*static_severity), 2)});
  } else {
    frontend::Type severity_type;
    severity_type.spelling = "severity_level";
    severity_type.domain = frontend::ValueDomain::Bit2;
    severity_type.packed_range = frontend::PackedRange{1, 0, true};
    severity_type.enumeration_literals = {
        "note", "warning", "error", "failure"};
    if (vhdl_expression_matches_type(
            statement.vhdl_severity_expression, severity_type)) {
      severity_register = lower_expression(
          statement.vhdl_severity_expression, 2, &severity_type);
    }
    if (!severity_register) {
      report(
          "FSIM-ELAB-VHREPORT-002",
          "a VHDL severity expression must have severity_level type",
          statement.vhdl_severity_expression.span);
    }
  }

  if (message && severity_register) {
    process_.operations.emplace_back(StringReport{
        *message,
        *severity_register,
        SourceLocation{
            statement.span.source_name,
            static_cast<std::uint32_t>(statement.span.begin.line),
            static_cast<std::uint32_t>(statement.span.begin.column)},
        standalone});
  }
  if (pass_branch) {
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[*pass_branch] = Branch{
        *condition, end, *pass_branch + 1,
        UnknownBranchPolicy::when_false};
  }
}

}  // namespace fsim::elaboration
