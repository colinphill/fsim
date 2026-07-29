// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

GenerateRegion VhdlParser::parse_vhdl_conditional_generate(
  const Token& label,
  const Token& start) {
  GenerateRegion result;
  result.then_scope = vhdl_name(label.text);
  result.else_scope = result.then_scope;
  result.condition = parse_expression();
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-056");
  parse_vhdl_generate_declarations(result.then_body);
  (void)match_keyword("begin", true);
  parse_vhdl_generate_branch(result.then_body);
  if (match_keyword("else", true)) {
    expect_keyword("generate", true, "FSIM-VHDL-PARSE-057");
    parse_vhdl_generate_declarations(result.else_body);
    (void)match_keyword("begin", true);
    parse_vhdl_generate_branch(result.else_body);
  }
  expect_keyword("end", true, "FSIM-VHDL-PARSE-058");
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-059");
  if (at(TokenKind::Identifier)) {
    const auto end_label = advance();
    if (vhdl_name(end_label.text) != result.then_scope) {
      error(
          end_label,
          "FSIM-VHDL-PARSE-060",
          "generate end label does not match '"
              + result.then_scope + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after conditional generate",
      "FSIM-VHDL-PARSE-061");
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VhdlParser::parse_vhdl_iterative_generate(
  const Token& label,
  const Token& start) {
  GenerateRegion result;
  result.kind = GenerateKind::Iterative;
  result.then_scope = vhdl_name(label.text);
  const auto variable = expect_identifier("generate loop variable");
  result.variable = vhdl_name(variable.text);
  expect_keyword("in", true, "FSIM-VHDL-PARSE-062");
  result.initial = parse_expression();
  bool descending = false;
  if (match_keyword("to", true)) {
    descending = false;
  } else if (match_keyword("downto", true)) {
    descending = true;
  } else {
    error(
        current(),
        "FSIM-VHDL-PARSE-063",
        "expected 'to' or 'downto' in generate iteration range");
  }
  auto limit = parse_expression();
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-064");
  const auto expression_span = span_from(variable, previous());
  Expression loop_variable{
      ExpressionKind::Identifier,
      result.variable,
      {},
      variable.span};
  result.condition = {
      ExpressionKind::Binary,
      descending ? ">=" : "<=",
      {loop_variable, std::move(limit)},
      expression_span};
  result.iteration = {
      ExpressionKind::Binary,
      descending ? "-" : "+",
      {
          std::move(loop_variable),
          Expression{
              ExpressionKind::IntegerLiteral,
              "1",
              {},
              expression_span}},
      expression_span};
  parse_vhdl_generate_declarations(result.then_body);
  (void)match_keyword("begin", true);
  parse_vhdl_generate_branch(result.then_body);
  expect_keyword("end", true, "FSIM-VHDL-PARSE-065");
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-066");
  if (at(TokenKind::Identifier)) {
    const auto end_label = advance();
    if (vhdl_name(end_label.text) != result.then_scope) {
      error(
          end_label,
          "FSIM-VHDL-PARSE-067",
          "generate end label does not match '"
              + result.then_scope + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after iterative generate",
      "FSIM-VHDL-PARSE-068");
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VhdlParser::parse_vhdl_selection_generate(
  const Token& label,
  const Token& start) {
  GenerateRegion result;
  result.kind = GenerateKind::Selection;
  result.condition = parse_expression();
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-069");
  bool saw_default = false;
  while (!at_end() && !keyword("end", 0, true)) {
    GenerateAlternative alternative;
    const auto alternative_start = current();
    if (saw_default) {
      error(
          current(),
          "FSIM-VHDL-SEM-018",
          "an others case-generate alternative must be last");
    }
    if (!(at(TokenKind::Identifier)
          && at(TokenKind::Colon, 1)
          && keyword("when", 2, true))) {
      error(
          current(),
          "FSIM-VHDL-PARSE-070",
          "a case-generate alternative must have a stable label");
    }
    const auto alternative_label =
        expect_identifier("case-generate alternative label");
    alternative.scope = vhdl_name(alternative_label.text);
    expect(
        TokenKind::Colon,
        "':' after case-generate alternative label",
        "FSIM-VHDL-PARSE-071");
    expect_keyword("when", true, "FSIM-VHDL-PARSE-071");
    if (match_keyword("others", true)) {
      alternative.is_default = true;
      if (saw_default) {
        error(
            previous(),
            "FSIM-VHDL-SEM-017",
            "case generate contains more than one others "
            "alternative");
      }
      saw_default = true;
    } else {
      do {
        GenerateChoice choice;
        choice.left = parse_expression();
        choice.span = choice.left.span;
        if (match_keyword("to", true)
            || match_keyword("downto", true)) {
          choice.descending =
              vhdl_name(previous().text) == "downto";
          choice.right = parse_expression();
          choice.span =
              cover(choice.left.span, choice.right->span);
        }
        alternative.choices.push_back(
            std::move(choice));
      } while (match(TokenKind::Pipe));
      if (alternative.choices.empty()) {
        error(
            current(),
            "FSIM-VHDL-PARSE-072",
            "case-generate alternative requires a choice");
      }
    }
    expect(
        TokenKind::Arrow,
        "'=>' after case-generate choices",
        "FSIM-VHDL-PARSE-073");
    parse_vhdl_generate_declarations(alternative.body);
    (void)match_keyword("begin", true);
    parse_vhdl_generate_branch(
        alternative.body,
        true);
    alternative.span =
        span_from(alternative_start, previous());
    result.alternatives.push_back(std::move(alternative));
  }
  expect_keyword("end", true, "FSIM-VHDL-PARSE-074");
  expect_keyword("generate", true, "FSIM-VHDL-PARSE-075");
  if (at(TokenKind::Identifier)) {
    const auto end_label = advance();
    if (vhdl_name(end_label.text) != vhdl_name(label.text)) {
      error(
          end_label,
          "FSIM-VHDL-PARSE-076",
          "generate end label does not match '"
              + vhdl_name(label.text) + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after case generate",
      "FSIM-VHDL-PARSE-077");
  result.span = span_from(start, previous());
  return result;
}

GenerateRegion VhdlParser::parse_vhdl_static_block(
  const Token& label,
  const Token& start) {
  GenerateRegion result;
  result.kind = GenerateKind::StaticBlock;
  result.then_scope = vhdl_name(label.text);
  if (at(TokenKind::LeftParen)) {
    const auto guard = current();
    skip_balanced(
        TokenKind::LeftParen, TokenKind::RightParen);
    error(
        guard,
        "FSIM-VHDL-UNSUPPORTED-021",
        "guarded block statements are not executable yet");
  }
  (void)match_keyword("is", true);
  parse_vhdl_generate_declarations(result.then_body);
  expect_keyword("begin", true, "FSIM-VHDL-PARSE-078");
  parse_vhdl_generate_branch(result.then_body);
  expect_keyword("end", true, "FSIM-VHDL-PARSE-079");
  expect_keyword("block", true, "FSIM-VHDL-PARSE-080");
  if (at(TokenKind::Identifier)) {
    const auto end_label = advance();
    if (vhdl_name(end_label.text) != result.then_scope) {
      error(
          end_label,
          "FSIM-VHDL-PARSE-081",
          "block end label does not match '"
              + result.then_scope + "'");
    }
  }
  expect(
      TokenKind::Semicolon,
      "';' after block statement",
      "FSIM-VHDL-PARSE-082");
  result.span = span_from(start, previous());
  return result;
}

void VhdlParser::parse_vhdl_generate_declarations(GenerateBody& body) {
  for (;;) {
    if (match_keyword("signal", true)) {
      parse_signal_declaration(
          body.signals, &body.constants);
      continue;
    }
    if (match_keyword("constant", true)) {
      parse_vhdl_generate_constant(
          body, previous());
      continue;
    }
    break;
  }
}

void VhdlParser::parse_vhdl_generate_constant(
  GenerateBody& body, const Token& start) {
  std::vector<Token> names;
  names.push_back(expect_identifier("constant name"));
  while (match(TokenKind::Comma)) {
    names.push_back(expect_identifier("constant name"));
  }
  expect(
      TokenKind::Colon,
      "':' after constant names",
      "FSIM-VHDL-PARSE-083");
  const auto type = parse_vhdl_type(true);
  Expression value;
  if (match(TokenKind::ColonEqual)) {
    value = parse_expression();
  } else {
    error(
        current(),
        "FSIM-VHDL-PARSE-084",
        "a generated constant requires a default expression");
  }
  expect(
      TokenKind::Semicolon,
      "';' after constant declaration",
      "FSIM-VHDL-PARSE-085");
  for (const auto& name : names) {
    const auto canonical = vhdl_name(name.text);
    const bool duplicate =
        std::any_of(
            body.constants.begin(),
            body.constants.end(),
            [&](const ParameterDeclaration& constant) {
              return constant.name == canonical;
            })
        || std::any_of(
            body.signals.begin(),
            body.signals.end(),
            [&](const SignalDeclaration& signal) {
              return signal.name == canonical;
            });
    if (duplicate) {
      error(
          name,
          "FSIM-VHDL-SEM-019",
          "duplicate or conflicting generated constant declaration '"
              + canonical + "'");
      continue;
    }
    body.constants.push_back(ParameterDeclaration{
        canonical,
        type,
        value,
        true,
        span_from(start, previous())});
  }
}

void VhdlParser::parse_vhdl_generate_branch(
    GenerateBody& body,
    const bool stop_at_case_alternative) {
  while (!at_end() && !keyword("else", 0, true)
         && !keyword("end", 0, true)
         && !(stop_at_case_alternative
              && at(TokenKind::Identifier)
              && at(TokenKind::Colon, 1)
              && keyword("when", 2, true))) {
    std::optional<Token> label;
    if (at(TokenKind::Identifier)
        && at(TokenKind::Colon, 1)) {
      label = advance();
      advance();
    }
    if (keyword("process", 0, true)) {
      body.processes.push_back(parse_process(
          label ? vhdl_name(label->text) : std::string{}));
      continue;
    }
    if (match_keyword("assert", true)) {
      auto statement = parse_vhdl_assertion(previous());
      if (label) {
        statement.label = vhdl_name(label->text);
      }
      body.concurrent_statements.push_back(std::move(statement));
      continue;
    }
    if (label && match_keyword("if", true)) {
      body.generate_regions.push_back(
          parse_vhdl_conditional_generate(
              *label, previous()));
      continue;
    }
    if (label && match_keyword("for", true)) {
      body.generate_regions.push_back(
          parse_vhdl_iterative_generate(
              *label, previous()));
      continue;
    }
    if (label && match_keyword("case", true)) {
      body.generate_regions.push_back(
          parse_vhdl_selection_generate(
              *label, previous()));
      continue;
    }
    if (label && match_keyword("block", true)) {
      body.generate_regions.push_back(
          parse_vhdl_static_block(
              *label, previous()));
      continue;
    }
    if (label && (
        keyword("entity", 0, true)
        || (at(TokenKind::Identifier)
            && (keyword("port", 1, true)
                || keyword("generic", 1, true))))) {
      body.instances.push_back(parse_vhdl_instance(*label));
      continue;
    }
    const auto before = position();
    auto assignment = parse_assignment(true);
    if (assignment) {
      body.concurrent_statements.push_back(
          std::move(*assignment));
      continue;
    }
    rewind(before);
    const auto unsupported = advance();
    error(
        unsupported,
        "FSIM-VHDL-UNSUPPORTED-020",
        "unsupported concurrent item in generate branch");
    skip_to_semicolon();
  }
}

Instance VhdlParser::parse_vhdl_instance(const Token& label) {
  Instance instance;
  instance.name = vhdl_name(label.text);

  if (match_keyword("entity", true)) {
    const auto first = expect_identifier("entity name");
    instance.unit_name = vhdl_name(first.text);
    if (match(TokenKind::Dot)) {
      const auto unit = expect_identifier("entity name after library");
      instance.unit_name += '.';
      instance.unit_name += vhdl_name(unit.text);
    }
    if (match(TokenKind::LeftParen)) {
      const auto architecture =
          expect_identifier("architecture name in entity aspect");
      instance.unit_name += '(';
      instance.unit_name += vhdl_name(architecture.text);
      instance.unit_name += ')';
      expect(TokenKind::RightParen, "')' after architecture name",
             "FSIM-VHDL-PARSE-036");
    }
  } else {
    const auto component = expect_identifier("component name");
    instance.unit_name = vhdl_name(component.text);
  }

  if (match_keyword("generic", true)) {
    parse_vhdl_generic_map(instance, previous());
  }

  if (!match_keyword("port", true)) {
    error(current(), "FSIM-VHDL-PARSE-039",
          "expected 'port map' in VHDL instance");
    skip_to_semicolon();
    instance.span = span_from(label, previous());
    return instance;
  }
  expect_keyword("map", true, "FSIM-VHDL-PARSE-040");
  if (!match(TokenKind::LeftParen)) {
    error(current(), "FSIM-VHDL-PARSE-041",
          "expected '(' after port map");
    skip_to_semicolon();
    instance.span = span_from(label, previous());
    return instance;
  }

  while (!at_end() && !at(TokenKind::RightParen)) {
    instance.connections.push_back(parse_vhdl_port_connection());
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(TokenKind::RightParen, "')' after port map",
         "FSIM-VHDL-PARSE-042");
  expect(TokenKind::Semicolon, "';' after VHDL instance",
         "FSIM-VHDL-PARSE-043");
  instance.span = span_from(label, previous());
  return instance;
}

void VhdlParser::parse_vhdl_generic_map(
  Instance& instance,
  const Token& start) {
  expect_keyword("map", true, "FSIM-VHDL-PARSE-037");
  expect(
      TokenKind::LeftParen,
      "'(' after generic map",
      "FSIM-VHDL-PARSE-038");
  bool saw_named = false;
  while (!at_end() && !at(TokenKind::RightParen)) {
    const auto association_start = current();
    ParameterOverride actual;
    if (at(TokenKind::Identifier)
        && at(TokenKind::Arrow, 1)) {
      saw_named = true;
      const auto name = advance();
      advance();
      actual.name = vhdl_name(name.text);
      if (std::any_of(
              instance.parameter_overrides.begin(),
              instance.parameter_overrides.end(),
              [&](const ParameterOverride& existing) {
                return existing.name == actual.name;
              })) {
        error(
            name,
            "FSIM-VHDL-SEM-015",
            "duplicate named generic actual '"
                + *actual.name + "'");
      }
    } else if (saw_named) {
      error(
          current(),
          "FSIM-VHDL-SEM-016",
          "a positional generic actual cannot follow a named actual");
    }
    if (match_keyword("open", true)) {
      actual.value = Expression{
          ExpressionKind::Invalid,
          "open",
          {},
          previous().span};
      error(
          previous(),
          "FSIM-VHDL-UNSUPPORTED-019",
          "open generic actuals are not implemented in this frontend "
          "slice");
    } else {
      actual.value = parse_expression();
    }
    actual.span =
        cover(association_start.span, previous().span);
    instance.parameter_overrides.push_back(std::move(actual));
    if (!match(TokenKind::Comma)) {
      break;
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after generic map",
      "FSIM-VHDL-PARSE-055");
  (void)start;
}

PortConnection VhdlParser::parse_vhdl_port_connection() {
  const auto start = current();
  PortConnection connection;
  if (at(TokenKind::Identifier) && at(TokenKind::Arrow, 1)) {
    connection.port = vhdl_name(advance().text);
    advance();
  }

  bool simple_identifier = false;
  if (at(TokenKind::Identifier) && !keyword("open", 0, true)) {
    const auto actual = advance();
    connection.value =
        Expression{ExpressionKind::Identifier, vhdl_name(actual.text), {},
                   actual.span};
    simple_identifier =
        at(TokenKind::Comma) || at(TokenKind::RightParen);
  }

  if (!simple_identifier) {
    const auto unsupported = current();
    error(unsupported, "FSIM-VHDL-UNSUPPORTED-010",
          "port-map actuals must be simple identifiers in the "
          "vertical-slice frontend");
    skip_vhdl_connection_actual();
    const auto end = previous();
    connection.value.kind = ExpressionKind::Invalid;
    if (connection.value.text.empty()) {
      connection.value.text = unsupported.text;
      connection.value.span = unsupported.span;
    } else {
      connection.value.span = cover(connection.value.span, end.span);
    }
  }
  connection.span = cover(start.span, previous().span);
  return connection;
}

void VhdlParser::skip_vhdl_connection_actual() {
  std::size_t parenthesis_depth = 0;
  std::size_t bracket_depth = 0;
  std::size_t brace_depth = 0;
  while (!at_end()) {
    if (at(TokenKind::Comma) && parenthesis_depth == 0 &&
        bracket_depth == 0 && brace_depth == 0) {
      return;
    }
    if (at(TokenKind::RightParen) && parenthesis_depth == 0 &&
        bracket_depth == 0 && brace_depth == 0) {
      return;
    }
    if (match(TokenKind::LeftParen)) {
      ++parenthesis_depth;
    } else if (at(TokenKind::RightParen)) {
      advance();
      if (parenthesis_depth != 0) {
        --parenthesis_depth;
      }
    } else if (match(TokenKind::LeftBracket)) {
      ++bracket_depth;
    } else if (at(TokenKind::RightBracket)) {
      advance();
      if (bracket_depth != 0) {
        --bracket_depth;
      }
    } else if (match(TokenKind::LeftBrace)) {
      ++brace_depth;
    } else if (at(TokenKind::RightBrace)) {
      advance();
      if (brace_depth != 0) {
        --brace_depth;
      }
    } else {
      advance();
    }
  }
}

Process VhdlParser::parse_process(std::string label) {
  const auto start =
      expect_keyword("process", true, "FSIM-VHDL-PARSE-019");
  Process process;
  process.kind = ProcessKind::VhdlProcess;
  process.name = std::move(label);

  if (match(TokenKind::LeftParen)) {
    while (!at_end() && !at(TokenKind::RightParen)) {
      const auto signal = expect_identifier("sensitivity name");
      process.sensitivities.push_back(
          Sensitivity{EdgeKind::Any, vhdl_name(signal.text), signal.span});
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    expect(TokenKind::RightParen, "')' after sensitivity list",
           "FSIM-VHDL-PARSE-020");
  }
  match_keyword("is", true);
  while (!at_end() && !keyword("begin", 0, true)) {
    if (!match_keyword("variable", true)) {
      const auto declaration = current();
      error(
          declaration,
          "FSIM-VHDL-UNSUPPORTED-007",
          "only process variable declarations are implemented in this "
          "declarative slice");
      while (!at_end() && !keyword("begin", 0, true)
             && !at(TokenKind::Semicolon)) {
        advance();
      }
      match(TokenKind::Semicolon);
      continue;
    }
    std::vector<Token> names;
    names.push_back(expect_identifier("variable name"));
    while (match(TokenKind::Comma)) {
      names.push_back(expect_identifier("variable name"));
    }
    expect(
        TokenKind::Colon,
        "':' after variable names",
        "FSIM-VHDL-PARSE-047");
    const auto type = parse_vhdl_type(true, true);
    std::optional<Expression> initializer;
    if (match(TokenKind::ColonEqual)) {
      initializer = parse_expression();
    }
    expect(
        TokenKind::Semicolon,
        "';' after variable declaration",
        "FSIM-VHDL-PARSE-048");
    for (const auto& name : names) {
      process.variables.push_back(VariableDeclaration{
          vhdl_name(name.text),
          type,
          initializer,
          span_from(name, previous())});
    }
  }
  expect_keyword("begin", true, "FSIM-VHDL-PARSE-021");
  sequential_loop_labels_seen_.clear();
  process.statements = parse_statement_list({"end"});
  sequential_loop_labels_seen_.clear();
  const auto contains_explicit_wait =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> bool {
        for (const auto& statement : statements) {
          if (statement.kind == StatementKind::Delay
              || statement.kind == StatementKind::WaitOn
              || statement.kind == StatementKind::WaitUntil
              || self(self, statement.statements)
              || self(self, statement.else_statements)) {
            return true;
          }
        }
        return false;
      };
  if (!process.sensitivities.empty()
      && contains_explicit_wait(
          contains_explicit_wait, process.statements)) {
    error(
        start,
        "FSIM-VHDL-SEM-012",
        "a process sensitivity list cannot be combined with an explicit "
        "wait statement");
  }
  for (const auto& statement : process.statements) {
    if (contains_explicit_wait(
            contains_explicit_wait, statement.statements)
        || contains_explicit_wait(
            contains_explicit_wait, statement.else_statements)) {
      error(
          start,
          "FSIM-VHDL-UNSUPPORTED-017",
          "wait statements nested in conditional control flow require "
          "suspension-path analysis not implemented in this frontend "
          "slice");
      break;
    }
  }
  expect_keyword("end", true, "FSIM-VHDL-PARSE-022");
  match_keyword("process", true);
  if (at(TokenKind::Identifier)) {
    advance();
  }
  expect(TokenKind::Semicolon, "';' after process",
         "FSIM-VHDL-PARSE-023");
  process.span = span_from(start, previous());
  infer_process_edge(process);
  return process;
}

}  // namespace fsim::frontend
