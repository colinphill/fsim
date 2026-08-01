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
    statement.fork_join_kind = ForkJoinKind::Any;
    if (language_ != Language::SystemVerilog2017) {
      error(
          previous(), "FSIM-SV-SEM-108",
          "join_any requires SystemVerilog");
    }
  } else if (current().text == "join_none") {
    advance();
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
  statement.span = span_from(start, previous());
  return statement;
}

}  // namespace fsim::frontend
