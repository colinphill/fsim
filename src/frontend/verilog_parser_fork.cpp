// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

Statement VerilogParser::parse_fork_statement(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::Fork;
  if (match(TokenKind::Colon)) {
    statement.label = expect_identifier("fork block name").text;
  }

  while (!at_end() && current().text != "join"
         && current().text != "join_any"
         && current().text != "join_none") {
    const auto before = position();
    if (is_declaration_start()) {
      parse_procedural_declaration(statement);
    } else if (auto branch = parse_statement()) {
      statement.statements.push_back(std::move(*branch));
    }
    if (position() == before) {
      advance();
    }
  }

  if (current().text == "join") {
    advance();
    statement.fork_join_kind = ForkJoinKind::All;
  } else if (current().text == "join_any") {
    advance();
    (void)require_standard(
        "join_any",
        StandardRevision::SystemVerilog2005,
        previous(),
        "FSIM-SV-PARSE-347");
    statement.fork_join_kind = ForkJoinKind::Any;
    if (language_ != Language::SystemVerilog2017) {
      error(
          previous(), "FSIM-SV-SEM-108",
          "join_any requires SystemVerilog");
    }
  } else if (current().text == "join_none") {
    advance();
    (void)require_standard(
        "join_none",
        StandardRevision::SystemVerilog2005,
        previous(),
        "FSIM-SV-PARSE-347");
    statement.fork_join_kind = ForkJoinKind::None;
    if (language_ != Language::SystemVerilog2017) {
      error(
          previous(), "FSIM-SV-SEM-108",
          "join_none requires SystemVerilog");
    }
  } else {
    error(
        current(), "FSIM-SV-PARSE-206",
        "expected join, join_any, or join_none after fork branches");
  }

  if (match(TokenKind::Colon)) {
    const auto closing_label = expect_identifier("fork block name");
    if (statement.label.empty()) {
      error(
          closing_label, "FSIM-SV-SEM-109",
          "a join label requires a matching named fork");
    } else if (closing_label.text != statement.label) {
      error(
          closing_label, "FSIM-SV-SEM-109",
          "join label '" + closing_label.text
              + "' does not match fork label '" + statement.label + "'");
    }
  }
  const auto contains_return = [](const auto& self,
                                  const Statement& candidate) -> bool {
    if (candidate.kind == StatementKind::Return) {
      return true;
    }
    const auto any_return = [&](const std::vector<Statement>& statements) {
      return std::ranges::any_of(
          statements,
          [&](const Statement& nested) { return self(self, nested); });
    };
    if (any_return(candidate.statements)
        || any_return(candidate.else_statements)) {
      return true;
    }
    return std::ranges::any_of(
        candidate.case_alternatives,
        [&](const CaseAlternative& alternative) {
          return any_return(alternative.statements);
        });
  };
  if ((in_function_ || in_task_)
      && std::ranges::any_of(
          statement.statements,
          [&](const Statement& branch) {
            return contains_return(contains_return, branch);
          })) {
    error(
        start,
        "FSIM-SV-SEM-247",
        "a return statement cannot be nested in fork...join");
  }
  statement.span = span_from(start, previous());
  return statement;
}

}  // namespace fsim::frontend
