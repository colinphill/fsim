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
  return sensitivities;
}

SystemVerilogClockingSkew VerilogParser::parse_clocking_skew() {
  const auto start = current();
  SystemVerilogClockingSkew skew;
  if (match_keyword("posedge")) {
    skew.edge = EdgeKind::Positive;
  } else if (match_keyword("negedge")) {
    skew.edge = EdgeKind::Negative;
  }
  if (match(TokenKind::Hash)) {
    const auto hash = previous();
    if (at(TokenKind::Number)
        && current().text == "1"
        && at(TokenKind::Identifier, 1)
        && current(1).text == "step") {
      advance();
      advance();
      skew.one_step = true;
    } else {
      skew.delay = parse_verilog_delay(hash);
    }
  }
  skew.span = span_from(start, previous());
  return skew;
}

void VerilogParser::parse_default_clocking(
    DesignUnit& unit,
    const Token& start) {
  const auto name =
      expect_identifier("default clocking block name");
  expect(
      TokenKind::Semicolon,
      "';' after a default clocking declaration",
      "FSIM-SV-PARSE-288");
  if (unit.systemverilog_default_clocking_block) {
    error(
        start,
        "FSIM-SV-SEM-185",
        "a design unit can declare only one default clocking block");
    return;
  }
  if (!std::ranges::any_of(
          unit.systemverilog_clocking_blocks,
          [&](const SystemVerilogClockingBlock& block) {
            return block.name == name.text;
          })) {
    error(
        name,
        "FSIM-SV-SEM-186",
        "default clocking block '" + name.text
            + "' has not been declared");
    return;
  }
  unit.systemverilog_default_clocking_block = name.text;
  unit.systemverilog_default_clocking_span =
      span_from(start, previous());
}

void VerilogParser::parse_clocking_block(
    DesignUnit& unit,
    const Token& start) {
  const auto name = expect_identifier("clocking block name");
  SystemVerilogClockingBlock block;
  block.name = name.text;
  expect(
      TokenKind::At,
      "'@' before a clocking event",
      "FSIM-SV-PARSE-283");
  block.event = parse_sensitivity();
  expect(
      TokenKind::Semicolon,
      "';' after a clocking event",
      "FSIM-SV-PARSE-284");

  while (!at_end() && !keyword("endclocking")) {
    if (match_keyword("default")) {
      const auto default_start = previous();
      bool parsed_skew = false;
      while (keyword("input") || keyword("output")) {
        const auto direction = parse_direction();
        parsed_skew = true;
        if (!(keyword("posedge") || keyword("negedge")
              || at(TokenKind::Hash))) {
          error(
              current(),
              "FSIM-SV-SEM-181",
              "a default clocking direction requires an edge or delay skew");
          break;
        }
        auto skew = parse_clocking_skew();
        auto& destination =
            direction == PortDirection::Input
            ? block.default_input_skew
            : block.default_output_skew;
        if (destination) {
          error(
              default_start,
              "FSIM-SV-SEM-182",
              "a clocking block repeats its default "
                  + std::string{
                      direction == PortDirection::Input
                      ? "input" : "output"}
                  + " skew");
        } else {
          destination = std::move(skew);
        }
      }
      if (!parsed_skew) {
        error(
            default_start,
            "FSIM-SV-SEM-181",
            "a default clocking declaration requires an input "
            "or output skew");
      }
      expect(
          TokenKind::Semicolon,
          "';' after default clocking skews",
          "FSIM-SV-PARSE-287");
      continue;
    }
    if (!is_direction_keyword()) {
      const auto invalid = advance();
      error(
          invalid,
          "FSIM-SV-SEM-176",
          "a clocking signal declaration requires an input, output, "
          "or inout direction");
      skip_to_semicolon();
      continue;
    }
    const auto direction = parse_direction();
    std::optional<SystemVerilogClockingSkew> skew;
    if (keyword("posedge") || keyword("negedge")
        || at(TokenKind::Hash)) {
      skew = parse_clocking_skew();
      if (direction == PortDirection::Inout) {
        error(
            previous(),
            "FSIM-SV-SEM-183",
            "an inout clocking signal cannot specify a skew");
      }
    }
    do {
      const auto signal = expect_identifier("clocking signal name");
      SystemVerilogClockingSignal declaration;
      declaration.name = signal.text;
      declaration.direction = direction;
      declaration.skew = skew;
      if (match(TokenKind::Assign)) {
        declaration.expression = parse_expression();
      }
      declaration.span = span_from(signal, previous());

      const bool duplicate = std::ranges::any_of(
          block.signals,
          [&](const SystemVerilogClockingSignal& existing) {
            return existing.name == declaration.name;
          });
      if (duplicate) {
        error(
            signal,
            "FSIM-SV-SEM-177",
            "duplicate clocking signal declaration '"
                + signal.text + "'");
      } else {
        const bool signal_declared =
            declaration.expression.has_value()
            || std::ranges::any_of(
                unit.signals,
                [&](const SignalDeclaration& candidate) {
                  return candidate.name == signal.text;
                })
            || std::ranges::any_of(
                unit.ports,
                [&](const SignalDeclaration& candidate) {
                  return candidate.name == signal.text;
                });
        if (!signal_declared) {
          error(
              signal,
              "FSIM-SV-SEM-178",
              "clocking signal '" + signal.text
                  + "' is not declared by its design unit");
        } else {
          block.signals.push_back(std::move(declaration));
        }
      }
    } while (match(TokenKind::Comma));
    expect(
        TokenKind::Semicolon,
        "';' after a clocking signal declaration",
        "FSIM-SV-PARSE-285");
  }

  expect_keyword(
      "endclocking",
      false,
      "FSIM-SV-PARSE-286");
  if (match(TokenKind::Colon)) {
    const auto end_name =
        expect_identifier("clocking block name after endclocking");
    if (end_name.text != block.name) {
      error(
          end_name,
          "FSIM-SV-SEM-179",
          "clocking block end name does not match '"
              + block.name + "'");
    }
  }
  block.span = span_from(start, previous());
  if (std::ranges::any_of(
          unit.systemverilog_clocking_blocks,
          [&](const SystemVerilogClockingBlock& existing) {
            return existing.name == block.name;
          })) {
    error(
        name,
        "FSIM-SV-SEM-180",
        "duplicate clocking block declaration '"
            + block.name + "'");
  } else {
    unit.systemverilog_clocking_blocks.push_back(
        std::move(block));
  }
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
