// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

std::vector<Sensitivity> VerilogParser::parse_sensitivity() {
  std::vector<Sensitivity> sensitivities;
  if (match(TokenKind::Star)) {
    sensitivities.push_back(
        Sensitivity{EdgeKind::Any, "*", previous().span, {}});
    return sensitivities;
  }
  if (at(TokenKind::Identifier) && !at(TokenKind::LeftParen, 1)) {
    const auto signal = advance();
    std::string signal_name = signal.text;
    while (match(TokenKind::Dot)) {
      signal_name += '.';
      signal_name +=
          expect_identifier("selected sensitivity signal").text;
    }
    if (signal_name.find('.') == std::string::npos) {
      note_implicit_net_reference(signal);
    }
    sensitivities.push_back(
        Sensitivity{
            EdgeKind::Any, std::move(signal_name), signal.span, {}});
    return sensitivities;
  }
  expect(TokenKind::LeftParen, "'(' after '@'", "FSIM-SV-PARSE-014");
  if (match(TokenKind::Star)) {
    sensitivities.push_back(
        Sensitivity{EdgeKind::Any, "*", previous().span, {}});
    expect(TokenKind::RightParen, "')' after '@*'",
           "FSIM-SV-PARSE-015");
    return sensitivities;
  }
  while (!at_end() && !at(TokenKind::RightParen)) {
    EdgeKind edge = EdgeKind::Any;
    if (match_keyword("posedge")) {
      edge = EdgeKind::Positive;
    } else if (match_keyword("negedge")) {
      edge = EdgeKind::Negative;
    }
    auto expression = parse_expression();
    Sensitivity sensitivity;
    sensitivity.edge = edge;
    sensitivity.span = expression.span;
    if (expression.kind == ExpressionKind::Identifier) {
      sensitivity.signal = expression.text;
      if (sensitivity.signal.find('.') == std::string::npos) {
        note_implicit_net_reference(Token{
            TokenKind::Identifier,
            sensitivity.signal,
            sensitivity.span,
            {}});
      }
    } else {
      sensitivity.expression = std::move(expression);
      if (edge != EdgeKind::Any) {
        error(
            previous(), "FSIM-SV-SEM-104",
            "edge-qualified event expressions must be direct scalar "
            "signals in this bounded event-control slice");
      }
    }
    sensitivities.push_back(std::move(sensitivity));
    if (match(TokenKind::Comma) || match_keyword("or")) {
      continue;
    }
    break;
  }
  expect(TokenKind::RightParen, "')' after sensitivity list",
         "FSIM-SV-PARSE-016");
  if (sensitivities.empty()) {
    error(
        previous(), "FSIM-SV-PARSE-136",
        "an event control requires at least one event expression");
  }
  if (sensitivities.size() != 1
      && std::ranges::any_of(
          sensitivities,
          [](const Sensitivity& sensitivity) {
            return sensitivity.expression.valid();
          })) {
    error(
        previous(), "FSIM-SV-SEM-105",
        "a general packed event expression cannot be mixed with other "
        "event-list items in this bounded slice");
  }
  return sensitivities;
}

bool VerilogParser::cycle_paths_are_safe(
    const std::vector<Statement>& statements) const {
  constexpr std::uint8_t fallthrough = 1;
  constexpr std::uint8_t safe_terminal = 2;
  constexpr std::uint8_t unsafe_cycle = 4;
  const auto analyze_paths =
      [&](const auto& self,
          const std::vector<Statement>& children) -> std::uint8_t {
    std::uint8_t result = fallthrough;
    for (const auto& child : children) {
      if ((result & fallthrough) == 0) {
        break;
      }
      std::uint8_t next = fallthrough;
      if (child.kind == StatementKind::Delay
          || child.kind == StatementKind::WaitOn
          || child.kind == StatementKind::Pause
          || child.kind == StatementKind::Finish
          || child.kind == StatementKind::Return
          || child.kind == StatementKind::Break
          || (child.kind == StatementKind::Assignment
              && child.procedural_assignment_control
                  != ProceduralAssignmentControl::None)) {
        next = safe_terminal;
      } else if (child.kind == StatementKind::Continue) {
        next = unsafe_cycle;
      } else if (child.kind == StatementKind::Block) {
        next = self(self, child.statements);
      } else if (child.kind == StatementKind::If) {
        next = self(self, child.statements)
            | (child.else_statements.empty()
                   ? fallthrough
                   : self(self, child.else_statements));
      } else if (child.kind == StatementKind::Case) {
        next = std::ranges::any_of(
                   child.case_alternatives,
                   [](const CaseAlternative& alternative) {
                     return alternative.is_default;
                   })
            ? std::uint8_t{0}
            : fallthrough;
        for (const auto& alternative : child.case_alternatives) {
          next |= self(self, alternative.statements);
        }
      }
      result = (result & ~fallthrough) | next;
    }
    return result;
  };
  const auto paths = analyze_paths(analyze_paths, statements);
  return (paths & (fallthrough | unsafe_cycle)) == 0;
}

Statement VerilogParser::parse_forever_statement(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::Loop;
  statement.loop_runtime = true;
  statement.condition = Expression{
      ExpressionKind::LogicLiteral, "1'b1", {}, start.span};
  parse_procedural_loop_body(start, statement);
  if (!cycle_paths_are_safe(statement.statements)) {
    error(
        start, "FSIM-SV-SEM-030",
        "every reachable forever-loop path must suspend, break, return, "
        "or terminate the simulation before its backedge");
  }
  statement.span = span_from(start, previous());
  return statement;
}

}  // namespace fsim::frontend
