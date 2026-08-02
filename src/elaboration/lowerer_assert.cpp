// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void Lowerer::lower_assert(const Statement& statement) {
  const auto condition = lower_condition(
      statement.condition, "FSIM-ELAB-051", "assertion");
  if (!condition) {
    return;
  }
  if (language_ == frontend::Language::SystemVerilog2017) {
    const auto branch_index = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        *condition, 0, 0, UnknownBranchPolicy::when_false});
    const auto pass_start = static_cast<InstructionIndex>(
        process_.operations.size());
    lower_statements(statement.statements);
    const auto jump_index = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Jump{0});
    const auto failure_start = static_cast<InstructionIndex>(
        process_.operations.size());
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
  AssertionSeverity severity = AssertionSeverity::error;
  switch (statement.assertion_severity) {
  case frontend::AssertionSeverity::Note:
    severity = AssertionSeverity::note;
    break;
  case frontend::AssertionSeverity::Warning:
    severity = AssertionSeverity::warning;
    break;
  case frontend::AssertionSeverity::Error:
    severity = AssertionSeverity::error;
    break;
  case frontend::AssertionSeverity::Failure:
    severity = AssertionSeverity::failure;
    break;
  }
  process_.operations.emplace_back(Assert{
      *condition,
      statement.assertion_message,
      severity,
      SourceLocation{
          statement.span.source_name,
          static_cast<std::uint32_t>(statement.span.begin.line),
          static_cast<std::uint32_t>(statement.span.begin.column)}});
}

}  // namespace fsim::elaboration
