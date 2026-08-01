// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

namespace fsim::frontend {

Process VerilogParser::parse_initial() {
  const auto start =
      expect_keyword("initial", false, "FSIM-SV-PARSE-013");
  current_procedural_names_.clear();
  Process process;
  process.kind = ProcessKind::Initial;
  auto body = parse_statement();
  if (body) {
    if (body->kind == StatementKind::Block
        && body->label.empty()) {
      process.variables = std::move(body->declarations);
      process.statements = std::move(body->statements);
    } else {
      process.statements.push_back(std::move(*body));
    }
  }
  process.span = span_from(start, previous());
  current_procedural_names_.clear();
  return process;
}

Process VerilogParser::parse_final() {
  const auto start =
      keyword("final")
          || (language_ == Language::Verilog2005
              && at(TokenKind::Identifier)
              && current().text == "final")
      ? advance()
      : expect_keyword("final", false, "FSIM-SV-PARSE-111");
  current_procedural_names_.clear();
  Process process;
  process.kind = ProcessKind::Final;
  if (language_ == Language::Verilog2005) {
    error(
        start,
        "FSIM-VERILOG-SEM-006",
        "final procedures require SystemVerilog");
  }
  auto body = parse_statement();
  if (body) {
    if (body->kind == StatementKind::Block
        && body->label.empty()) {
      process.variables = std::move(body->declarations);
      process.statements = std::move(body->statements);
    } else {
      process.statements.push_back(std::move(*body));
    }
  }

  const auto inspect =
      [&](const auto& self,
          const std::vector<Statement>& statements,
          bool& has_suspension,
          bool& has_nonblocking) -> void {
    for (const auto& statement : statements) {
      has_suspension =
          has_suspension
          || statement.kind == StatementKind::Delay
          || statement.kind == StatementKind::WaitOn
          || statement.kind == StatementKind::WaitUntil
          || statement.kind == StatementKind::Pause
          || statement.kind == StatementKind::Finish
          || (statement.kind == StatementKind::Assignment
              && statement.procedural_assignment_control
                  != ProceduralAssignmentControl::None);
      has_nonblocking =
          has_nonblocking
          || (statement.kind == StatementKind::Assignment
              && statement.assignment_kind
                  == AssignmentKind::NonBlocking);
      self(
          self, statement.statements,
          has_suspension, has_nonblocking);
      self(
          self, statement.else_statements,
          has_suspension, has_nonblocking);
      for (const auto& alternative :
           statement.case_alternatives) {
        self(
            self, alternative.statements,
            has_suspension, has_nonblocking);
      }
    }
  };
  bool has_suspension = false;
  bool has_nonblocking = false;
  inspect(
      inspect, process.statements,
      has_suspension, has_nonblocking);
  if (has_suspension) {
    error(
        start,
        "FSIM-SV-SEM-032",
        "a final procedure cannot contain a timing control, wait, "
        "or $finish");
  }
  if (has_nonblocking) {
    error(
        start,
        "FSIM-SV-SEM-033",
        "a final procedure cannot contain a nonblocking assignment");
  }
  process.span = span_from(start, previous());
  current_procedural_names_.clear();
  return process;
}

void VerilogParser::skip_case_statement() {
  std::size_t depth = 1;
  while (!at_end() && depth != 0) {
    if (keyword("case") || keyword("casez") || keyword("casex")) {
      ++depth;
      advance();
    } else if (match_keyword("endcase")) {
      --depth;
    } else {
      advance();
    }
  }
}

Statement VerilogParser::parse_case_statement(
  const Token& start,
  const CaseMatchKind match_kind,
  const CaseQualifier qualifier) {
  Statement statement;
  statement.kind = StatementKind::Case;
  statement.case_match_kind = match_kind;
  statement.case_qualifier = qualifier;
  expect(TokenKind::LeftParen, "'(' after case", "FSIM-SV-PARSE-046");
  statement.condition = parse_expression();
  expect(TokenKind::RightParen, "')' after case expression",
         "FSIM-SV-PARSE-047");
  if (at(TokenKind::Identifier) && current().text == "inside") {
    advance();
    if (language_ != Language::SystemVerilog2017) {
      error(previous(), "FSIM-SV-PARSE-180",
            "case inside matching requires SystemVerilog");
    }
    if (match_kind != CaseMatchKind::Exact) {
      error(previous(), "FSIM-SV-PARSE-184",
            "case inside cannot be combined with casez or casex");
    }
    statement.case_match_kind = CaseMatchKind::Inside;
  } else if (at(TokenKind::Identifier)
             && current().text == "matches") {
    advance();
    if (language_ != Language::SystemVerilog2017) {
      error(previous(), "FSIM-SV-PARSE-187",
            "case matches pattern matching requires SystemVerilog");
    }
    if (match_kind != CaseMatchKind::Exact) {
      error(previous(), "FSIM-SV-PARSE-188",
            "bounded case matches cannot be combined with casez or casex");
    }
    statement.case_match_kind = CaseMatchKind::Matches;
  }

  bool saw_default = false;
  const auto skip_pattern_tail = [&]() {
    std::size_t parentheses = 0;
    std::size_t brackets = 0;
    std::size_t braces = 0;
    while (!at_end() && !keyword("endcase")) {
      if (at(TokenKind::Colon) && parentheses == 0
          && brackets == 0 && braces == 0) {
        return;
      }
      if (at(TokenKind::LeftParen)) {
        ++parentheses;
      } else if (at(TokenKind::RightParen) && parentheses != 0) {
        --parentheses;
      } else if (at(TokenKind::LeftBracket)) {
        ++brackets;
      } else if (at(TokenKind::RightBracket) && brackets != 0) {
        --brackets;
      } else if (at(TokenKind::LeftBrace)) {
        ++braces;
      } else if (at(TokenKind::RightBrace) && braces != 0) {
        --braces;
      }
      advance();
    }
  };
  while (!at_end() && !keyword("endcase")) {
    const auto item_start = current();
    CaseAlternative alternative;
    if (match_keyword("default")) {
      alternative.is_default = true;
      if (saw_default) {
        error(item_start, "FSIM-SV-SEM-014",
              "a case statement may contain only one default item");
      }
      saw_default = true;
    } else {
      const auto parse_choice = [&]() {
        if (statement.case_match_kind == CaseMatchKind::Matches) {
          if (match(TokenKind::Dot)) {
            const auto pattern_start = previous();
            if (match(TokenKind::Star)) {
              alternative.choices.push_back(Expression{
                  ExpressionKind::Call,
                  "@match-wildcard",
                  {},
                  cover(pattern_start.span, previous().span)});
              return;
            }
            if (at(TokenKind::Identifier)) {
              const auto variable = advance();
              error(pattern_start, "FSIM-SV-UNSUPPORTED-042",
                    "case matches variable-binding patterns are not yet "
                    "implemented");
              alternative.choices.push_back(Expression{
                  ExpressionKind::Call,
                  "@match-unsupported",
                  {},
                  cover(pattern_start.span, variable.span)});
              return;
            }
            error(pattern_start, "FSIM-SV-PARSE-190",
                  "expected '*' or a pattern variable after '.'");
            alternative.choices.push_back(Expression{
                ExpressionKind::Call,
                "@match-unsupported",
                {},
                pattern_start.span});
            return;
          }
          if (keyword("tagged") || at(TokenKind::Apostrophe)) {
            const auto unsupported = advance();
            error(unsupported, "FSIM-SV-UNSUPPORTED-042",
                  "tagged and structured case matches patterns are not yet "
                  "implemented");
            skip_pattern_tail();
            alternative.choices.push_back(Expression{
                ExpressionKind::Call,
                "@match-unsupported",
                {},
                unsupported.span});
            return;
          }
        }
        if (statement.case_match_kind == CaseMatchKind::Inside
            && match(TokenKind::LeftBracket)) {
          const auto range_start = previous();
          auto low = parse_expression();
          expect(TokenKind::Colon, "':' in case inside range",
                 "FSIM-SV-PARSE-181");
          auto high = parse_expression();
          expect(TokenKind::RightBracket, "']' after case inside range",
                 "FSIM-SV-PARSE-182");
          alternative.choices.push_back(Expression{
              ExpressionKind::Call,
              "@inside-range",
              {std::move(low), std::move(high)},
              cover(range_start.span, previous().span)});
          return;
        }
        alternative.choices.push_back(parse_expression());
      };
      if (statement.case_match_kind == CaseMatchKind::Inside
          && at(TokenKind::Colon)) {
        error(current(), "FSIM-SV-PARSE-183",
              "a case inside item requires at least one choice");
      } else {
        parse_choice();
      }
      if (statement.case_match_kind == CaseMatchKind::Matches
          && match(TokenKind::AndAndAnd)) {
        error(previous(), "FSIM-SV-UNSUPPORTED-043",
              "guarded case matches patterns are not yet implemented");
        skip_pattern_tail();
      }
      if (statement.case_match_kind == CaseMatchKind::Matches
          && match(TokenKind::Comma)) {
        error(previous(), "FSIM-SV-PARSE-189",
              "a case matches item accepts exactly one pattern");
        skip_pattern_tail();
      }
      while (match(TokenKind::Comma)) {
        if (statement.case_match_kind == CaseMatchKind::Inside
            && at(TokenKind::Colon)) {
          error(current(), "FSIM-SV-PARSE-183",
                "a case inside item cannot end with an empty choice");
          break;
        }
        parse_choice();
      }
    }
    expect(TokenKind::Colon, "':' after case item",
           "FSIM-SV-PARSE-048");
    if (auto body = parse_statement()) {
      alternative.statements.push_back(std::move(*body));
    }
    alternative.span = cover(item_start.span, previous().span);
    statement.case_alternatives.push_back(std::move(alternative));
  }
  expect_keyword("endcase", false, "FSIM-SV-PARSE-049");
  statement.span = span_from(start, previous());
  return statement;
}

void VerilogParser::parse_procedural_loop_body(
  const Token& start, Statement& statement) {
  (void)start;
  ++current_loop_depth_;
  auto body = parse_statement();
  --current_loop_depth_;
  if (!body) {
    return;
  }
  if (body->kind == StatementKind::Block
      && body->label.empty()
      && body->declarations.empty()) {
    statement.statements = std::move(body->statements);
    return;
  }
  statement.statements.push_back(std::move(*body));
}

Statement VerilogParser::parse_procedural_for_statement(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::Loop;
  expect(
      TokenKind::LeftParen,
      "'(' after procedural for",
      "FSIM-SV-PARSE-095");
  const bool inline_variable =
      match_keyword("int") || match_keyword("integer");
  const auto variable =
      expect_identifier("procedural loop variable");
  statement.loop_variable = variable.text;
  statement.loop_variable_declared = inline_variable;
  statement.target = Expression{
      ExpressionKind::Identifier,
      variable.text,
      {},
      variable.span};
  expect(
      TokenKind::Assign,
      "'=' after procedural loop variable",
      "FSIM-SV-PARSE-096");
  statement.loop_initial = parse_expression();
  ++current_loop_names_[statement.loop_variable];
  expect(
      TokenKind::Semicolon,
      "';' after procedural loop initializer",
      "FSIM-SV-PARSE-097");

  auto condition = parse_expression();
  statement.condition = condition;
  expect(
      TokenKind::Semicolon,
      "';' after procedural loop condition",
      "FSIM-SV-PARSE-098");
  const bool canonical_condition =
      condition.kind == ExpressionKind::Binary
      && condition.operands.size() == 2
      && condition.operands.front().kind
          == ExpressionKind::Identifier
      && condition.operands.front().text
          == statement.loop_variable
      && (condition.text == "<" || condition.text == "<="
          || condition.text == ">" || condition.text == ">=");
  if (!canonical_condition) {
    error(
        variable,
        "FSIM-SV-SEM-027",
        "bounded procedural for-loop condition must compare its loop "
        "variable against an integral bound");
    statement.loop_runtime = true;
  } else {
    statement.loop_descending =
        condition.text == ">" || condition.text == ">=";
    statement.loop_limit_exclusive =
        condition.text == "<" || condition.text == ">";
    statement.loop_limit = condition.operands[1];
  }
  if (!inline_variable) {
    statement.loop_runtime = true;
  }

  std::optional<Token> prefix_update;
  if (match(TokenKind::PlusPlus)
      || match(TokenKind::MinusMinus)) {
    prefix_update = previous();
  }
  const auto iteration_variable =
      expect_identifier("procedural loop iteration variable");
  if (iteration_variable.text != statement.loop_variable) {
    error(
        iteration_variable,
        "FSIM-SV-SEM-028",
        "procedural loop iteration must update loop variable '"
            + statement.loop_variable + "'");
  }
  std::string update_operation;
  std::optional<Expression> update_operand;
  if (prefix_update) {
    update_operation =
        prefix_update->kind == TokenKind::PlusPlus ? "+" : "-";
    update_operand = Expression{
        ExpressionKind::IntegerLiteral,
        "1",
        {},
        prefix_update->span};
  } else if (
      match(TokenKind::PlusPlus)
      || match(TokenKind::MinusMinus)) {
    update_operation =
        previous().kind == TokenKind::PlusPlus ? "+" : "-";
    update_operand = Expression{
        ExpressionKind::IntegerLiteral,
        "1",
        {},
        previous().span};
  } else if (
      match(TokenKind::PlusAssign)
      || match(TokenKind::MinusAssign)) {
    update_operation =
        previous().kind == TokenKind::PlusAssign ? "+" : "-";
    update_operand = parse_expression();
  } else if (match(TokenKind::Assign)) {
    statement.value = parse_expression();
  }
  if (update_operand) {
    statement.value = Expression{
        ExpressionKind::Binary,
        update_operation,
        {statement.target, std::move(*update_operand)},
        cover(iteration_variable.span, previous().span)};
  }
  if (!statement.value.valid()) {
    error(
        iteration_variable,
        "FSIM-SV-SEM-103",
        "procedural loop update is not an assignment or increment of '"
            + statement.loop_variable + "'");
    statement.loop_runtime = true;
  }

  const auto update_amount =
      statement.value.kind == ExpressionKind::Binary
          && statement.value.operands.size() == 2
          && (statement.value.text == "+"
              || statement.value.text == "-")
      ? simple_integer_constant(statement.value.operands[1])
      : std::nullopt;
  const bool supported_update =
      update_amount && *update_amount > 0
      && (statement.loop_descending
              ? statement.value.text == "-"
              : statement.value.text == "+");
  if (!supported_update) {
    error(
        iteration_variable,
        "FSIM-SV-SEM-029",
        "bounded procedural for-loop iteration must advance by a "
        "positive constant toward its comparison bound");
  }
  if (!canonical_condition || !supported_update
      || !inline_variable || *update_amount != 1) {
    statement.loop_runtime = true;
  }
  expect(
      TokenKind::RightParen,
      "')' after procedural loop header",
      "FSIM-SV-PARSE-099");
  parse_procedural_loop_body(start, statement);
  const auto loop_name =
      current_loop_names_.find(statement.loop_variable);
  if (loop_name != current_loop_names_.end()
      && --loop_name->second == 0) {
    current_loop_names_.erase(loop_name);
  }
  statement.span = span_from(start, previous());
  return statement;
}

Statement VerilogParser::parse_repeat_statement(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::Loop;
  statement.loop_repeat = true;
  statement.loop_limit_exclusive = true;
  statement.loop_initial = Expression{
      ExpressionKind::IntegerLiteral,
      "0",
      {},
      start.span};
  expect(
      TokenKind::LeftParen,
      "'(' after repeat",
      "FSIM-SV-PARSE-100");
  statement.loop_limit = parse_expression();
  expect(
      TokenKind::RightParen,
      "')' after repeat count",
      "FSIM-SV-PARSE-101");
  parse_procedural_loop_body(start, statement);
  statement.span = span_from(start, previous());
  return statement;
}

Statement VerilogParser::parse_while_statement(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::Loop;
  statement.loop_runtime = true;
  expect(
      TokenKind::LeftParen,
      "'(' after while",
      "FSIM-SV-PARSE-102");
  statement.condition = parse_expression();
  expect(
      TokenKind::RightParen,
      "')' after while condition",
      "FSIM-SV-PARSE-103");
  parse_procedural_loop_body(start, statement);
  statement.span = span_from(start, previous());
  return statement;
}

Statement VerilogParser::parse_do_while_statement(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::Loop;
  statement.loop_runtime = true;
  statement.loop_post_test = true;
  parse_procedural_loop_body(start, statement);
  expect_keyword(
      "while", false, "FSIM-SV-PARSE-105");
  expect(
      TokenKind::LeftParen,
      "'(' after do-while body",
      "FSIM-SV-PARSE-106");
  statement.condition = parse_expression();
  expect(
      TokenKind::RightParen,
      "')' after do-while condition",
      "FSIM-SV-PARSE-107");
  expect(
      TokenKind::Semicolon,
      "';' after do-while statement",
      "FSIM-SV-PARSE-108");
  statement.span = span_from(start, previous());
  return statement;
}

void VerilogParser::parse_fatal_arguments(Statement& statement) {
  statement.output_text = "$fatal";
  statement.assertion_severity = AssertionSeverity::Failure;
  if (!match(TokenKind::LeftParen)) {
    return;
  }
  if (!at(TokenKind::RightParen)) {
    if (at(TokenKind::StringLiteral)) {
      statement.output_text =
          decoded_string_literal_text(advance());
    } else {
      // Accept and ignore the standard numeric finish control while
      // retaining one bounded constant-string display message.
      (void)parse_expression();
      if (match(TokenKind::Comma)) {
        if (at(TokenKind::StringLiteral)) {
          statement.output_text =
              decoded_string_literal_text(advance());
        } else {
          statement.value = parse_expression();
        }
      }
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after $fatal arguments",
      "FSIM-SV-PARSE-115");
}

void VerilogParser::parse_nonfatal_report_arguments(
  Statement& statement,
  const Token& task) {
  statement.output_text = task.text;
  if (!match(TokenKind::LeftParen)) {
    return;
  }
  if (!at(TokenKind::RightParen)) {
    if (at(TokenKind::StringLiteral)) {
      statement.output_text =
          decoded_string_literal_text(advance());
    } else {
      statement.value = parse_expression();
    }
    while (match(TokenKind::Comma)) {
      error(
          previous(),
          "FSIM-SV-SEM-043",
          task.text
              + " currently accepts at most one literal message");
      (void)parse_expression();
    }
  }
  expect(
      TokenKind::RightParen,
      "')' after " + task.text + " arguments",
      "FSIM-SV-PARSE-129");
}

std::optional<Statement> VerilogParser::parse_statement() {
  if (match(TokenKind::ThinArrow)) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::EventTrigger;
    if (match(TokenKind::Greater)) {
      statement.assignment_kind = AssignmentKind::NonBlocking;
      if (language_ == Language::Verilog2005) {
        error(
            previous(),
            "FSIM-VERILOG-SEM-008",
            "nonblocking named-event trigger '->>' requires "
            "SystemVerilog");
      }
    }
    if (match(TokenKind::Hash)) {
      const auto delay_start = previous();
      statement.delay = parse_verilog_delay(delay_start);
      if (statement.assignment_kind
          != AssignmentKind::NonBlocking) {
        error(
            delay_start,
            "FSIM-SV-SEM-036",
            "a delayed named-event trigger requires nonblocking "
            "'->>' syntax");
      }
    }
    const auto event = expect_identifier("named event after '->'");
    statement.target = Expression{
        ExpressionKind::Identifier,
        event.text,
        {},
        event.span};
    expect(
        TokenKind::Semicolon,
        "';' after named-event trigger",
        "FSIM-SV-PARSE-118");
    statement.span = span_from(start, previous());
    return statement;
  }
  const auto is_case_qualifier = [&]() {
    return at(TokenKind::Identifier)
        && (current().text == "unique"
            || current().text == "unique0"
            || current().text == "priority");
  };
  if (is_case_qualifier()) {
    const auto qualifier_start = advance();
    CaseQualifier qualifier = CaseQualifier::Unique;
    if (qualifier_start.text == "unique0") {
      qualifier = CaseQualifier::Unique0;
    } else if (qualifier_start.text == "priority") {
      qualifier = CaseQualifier::Priority;
    }
    if (language_ != Language::SystemVerilog2017) {
      error(qualifier_start, "FSIM-SV-PARSE-185",
            "case qualifiers require SystemVerilog");
    }
    while (is_case_qualifier()) {
      error(current(), "FSIM-SV-PARSE-186",
            "a case statement accepts only one qualifier");
      advance();
    }
    if (match_keyword("case")) {
      return parse_case_statement(
          qualifier_start, CaseMatchKind::Exact, qualifier);
    }
    if (match_keyword("casez")) {
      return parse_case_statement(
          qualifier_start, CaseMatchKind::WildcardZ, qualifier);
    }
    if (match_keyword("casex")) {
      return parse_case_statement(
          qualifier_start, CaseMatchKind::WildcardXZ, qualifier);
    }
    error(qualifier_start, "FSIM-SV-UNSUPPORTED-017",
          "bounded unique and priority qualifiers require a case statement");
    return parse_statement();
  }
  if (match_keyword("case")) {
    return parse_case_statement(previous(), CaseMatchKind::Exact);
  }
  if (match_keyword("casez")) {
    return parse_case_statement(previous(), CaseMatchKind::WildcardZ);
  }
  if (match_keyword("casex")) {
    return parse_case_statement(previous(), CaseMatchKind::WildcardXZ);
  }
  if (language_ == Language::SystemVerilog2017
      && match_keyword("for")) {
    return parse_procedural_for_statement(previous());
  }
  if (match_keyword("repeat")) {
    return parse_repeat_statement(previous());
  }
  if (match_keyword("while")) {
    return parse_while_statement(previous());
  }
  if (language_ == Language::SystemVerilog2017
      && match_keyword("do")) {
    return parse_do_while_statement(previous());
  }
  if (match_keyword("forever")) {
    return parse_forever_statement(previous());
  }
  if (language_ == Language::SystemVerilog2017
      && (keyword("break") || keyword("continue"))) {
    const auto start = advance();
    const auto is_break = start.text == "break";
    if (current_loop_depth_ == 0) {
      error(
          start,
          "FSIM-SV-SEM-031",
          std::string{"a SystemVerilog "}
              + (is_break ? "break" : "continue")
              + " statement must be nested in a procedural loop");
    }
    expect(
        TokenKind::Semicolon,
        "';' after SystemVerilog break or continue statement",
        "FSIM-SV-PARSE-104");
    Statement statement;
    statement.kind =
        is_break ? StatementKind::Break : StatementKind::Continue;
    statement.span = span_from(start, previous());
    return statement;
  }
  if (language_ == Language::SystemVerilog2017
      && match_keyword("return")) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::Return;
    if (!in_function_ && !in_task_) {
      error(start,
          "FSIM-SV-SEM-056",
          "a return statement must be nested in a function or task");
    }
    if (in_task_ && !at(TokenKind::Semicolon)) {
      error(start, "FSIM-SV-SEM-071",
            "a task return statement cannot return a value");
      statement.value = parse_expression();
    } else if (!at(TokenKind::Semicolon)) {
      statement.value = parse_expression();
    } else if (in_function_) {
      error(start,
          "FSIM-SV-SEM-057",
          "a non-void function return requires a value");
    }
    expect(
        TokenKind::Semicolon,
        "';' after return statement",
        "FSIM-SV-PARSE-139");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("wait")) {
    const auto start = previous();
    Statement statement;
    if (match_keyword("fork")) {
      statement.kind = StatementKind::WaitFork;
      if (language_ != Language::SystemVerilog2017) {
        error(
            start, "FSIM-SV-SEM-107",
            "wait fork requires SystemVerilog");
      }
      expect(
          TokenKind::Semicolon,
          "';' after wait fork",
          "FSIM-SV-PARSE-207");
      statement.span = span_from(start, previous());
      return statement;
    }
    statement.kind = StatementKind::WaitUntil;
    expect(
        TokenKind::LeftParen,
        "'(' after wait",
        "FSIM-SV-PARSE-109");
    statement.condition = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after wait condition",
        "FSIM-SV-PARSE-110");
    if (auto controlled = parse_statement()) {
      statement.statements.push_back(
          std::move(*controlled));
    }
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("fork")) {
    return parse_fork_statement(previous());
  }
  if (match_keyword("disable")) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::DisableFork;
    if (!match_keyword("fork")) {
      error(
          current(), "FSIM-SV-PARSE-208",
          "bounded process control requires 'disable fork'");
    }
    if (language_ != Language::SystemVerilog2017) {
      error(
          start, "FSIM-SV-SEM-107",
          "disable fork requires SystemVerilog");
    }
    expect(
        TokenKind::Semicolon,
        "';' after disable fork",
        "FSIM-SV-PARSE-209");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (language_ == Language::SystemVerilog2017
      && match_keyword("assert")) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::Assert;
    expect(TokenKind::LeftParen, "'(' after assert",
           "FSIM-SV-PARSE-137");
    statement.condition = parse_expression();
    expect(TokenKind::RightParen, "')' after assertion condition",
           "FSIM-SV-PARSE-138");
    if (match_keyword("else")) {
      statement.assertion_has_failure_action = true;
      if (auto action = parse_statement()) {
        statement.else_statements.push_back(
            std::move(*action));
      } else {
        error(
            current(),
            "FSIM-SV-PARSE-044",
            "expected an immediate-assertion failure action");
      }
    } else {
      statement.assertion_has_pass_action = true;
      if (auto action = parse_statement()) {
        statement.statements.push_back(std::move(*action));
      } else {
        error(
            current(),
            "FSIM-SV-PARSE-044",
            "expected an immediate-assertion pass action");
      }
      if (match_keyword("else")) {
        statement.assertion_has_failure_action = true;
        if (auto action = parse_statement()) {
          statement.else_statements.push_back(
              std::move(*action));
        } else {
          error(
              current(),
              "FSIM-SV-PARSE-044",
              "expected an immediate-assertion failure action");
        }
      }
    }
    statement.span = span_from(start, previous());
    return statement;
  }
  if (keyword("$fatal") || keyword("$error")
      || keyword("$warning") || keyword("$info")) {
    const auto start = advance();
    Statement statement;
    statement.kind = StatementKind::Report;
    if (language_ == Language::Verilog2005) {
      error(
          start,
          start.text == "$fatal"
              ? "FSIM-VERILOG-SEM-007"
              : "FSIM-VERILOG-SEM-009",
          start.text + " requires SystemVerilog");
    }
    if (start.text == "$fatal") {
      parse_fatal_arguments(statement);
    } else {
      if (start.text == "$info") {
        statement.assertion_severity = AssertionSeverity::Note;
      } else if (start.text == "$warning") {
        statement.assertion_severity =
            AssertionSeverity::Warning;
      } else {
        statement.assertion_severity = AssertionSeverity::Error;
      }
      parse_nonfatal_report_arguments(statement, start);
    }
    expect(
        TokenKind::Semicolon,
        "';' after " + start.text,
        start.text == "$fatal"
            ? "FSIM-SV-PARSE-116"
            : "FSIM-SV-PARSE-130");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("begin")) {
    const auto start = previous();
    std::string opening_label;
    if (match(TokenKind::Colon)) {
      opening_label = expect_identifier("block name").text;
    }
    Statement block;
    block.kind = StatementKind::Block;
    block.label = opening_label;
    while (!at_end() && !keyword("end")) {
      const auto before = position();
      if (is_declaration_start()) {
        parse_procedural_declaration(block);
      } else if (auto child = parse_statement()) {
        block.statements.push_back(std::move(*child));
      }
      if (position() == before) {
        advance();
      }
    }
    expect_keyword("end", false, "FSIM-SV-PARSE-017");
    if (match(TokenKind::Colon)) {
      const auto closing_label = expect_identifier("block name");
      if (opening_label.empty()) {
        error(
            closing_label,
            "FSIM-SV-SEM-034",
            "an end block label requires a matching opening label");
      } else if (closing_label.text != opening_label) {
        error(
            closing_label,
            "FSIM-SV-SEM-034",
            "end block label '" + closing_label.text
                + "' does not match opening label '"
                + opening_label + "'");
      }
    }
    block.span = span_from(start, previous());
    return block;
  }

  if (match_keyword("if")) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::If;
    expect(TokenKind::LeftParen, "'(' after if", "FSIM-SV-PARSE-018");
    statement.condition = parse_expression();
    expect(TokenKind::RightParen, "')' after if condition",
           "FSIM-SV-PARSE-019");
    if (auto true_branch = parse_statement()) {
      if (true_branch->kind == StatementKind::Block
          && true_branch->label.empty()
          && true_branch->declarations.empty()) {
        statement.statements = std::move(true_branch->statements);
      } else {
        statement.statements.push_back(std::move(*true_branch));
      }
    }
    if (match_keyword("else")) {
      if (auto false_branch = parse_statement()) {
        if (false_branch->kind == StatementKind::Block
            && false_branch->label.empty()
            && false_branch->declarations.empty()) {
          statement.else_statements =
              std::move(false_branch->statements);
        } else {
          statement.else_statements.push_back(
              std::move(*false_branch));
        }
      }
    }
    statement.span = span_from(start, previous());
    return statement;
  }

  if (match(TokenKind::At)) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::WaitOn;
    statement.sensitivities = parse_sensitivity();
    if (!match(TokenKind::Semicolon)) {
      if (auto controlled = parse_statement()) {
        statement.statements.push_back(std::move(*controlled));
      }
    }
    statement.span = span_from(start, previous());
    return statement;
  }

  if (match(TokenKind::Hash)) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::Delay;
    statement.delay = parse_verilog_delay(start);
    if (match(TokenKind::Semicolon)) {
      statement.span = span_from(start, previous());
      return statement;
    }
    if (auto delayed = parse_statement()) {
      statement.statements.push_back(std::move(*delayed));
    }
    statement.span = span_from(start, previous());
    return statement;
  }

  if (keyword("$readmemb") || keyword("$readmemh")
      || keyword("$writememb") || keyword("$writememh")) {
    const bool hexadecimal =
        keyword("$readmemh") || keyword("$writememh");
    const bool write =
        keyword("$writememb") || keyword("$writememh");
    const auto start = advance();
    Statement statement;
    statement.kind = StatementKind::MemoryLoad;
    statement.memory_hex = hexadecimal;
    statement.memory_write = write;
    if (language_ != Language::SystemVerilog2017) {
      error(
          start,
          "FSIM-SV-SEM-083",
          start.text + " requires SystemVerilog-2017");
    }
    expect(
        TokenKind::LeftParen,
        "'(' after " + start.text,
        "FSIM-SV-PARSE-159");
    statement.value = parse_expression();
    expect(
        TokenKind::Comma,
        "',' after memory-file name",
        "FSIM-SV-PARSE-160");
    statement.target = parse_expression();
    if (match(TokenKind::Comma)) {
      statement.task_arguments.push_back(parse_expression());
      if (match(TokenKind::Comma)) {
        statement.task_arguments.push_back(parse_expression());
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after read-memory arguments",
        "FSIM-SV-PARSE-161");
    expect(
        TokenKind::Semicolon,
        "';' after read-memory task",
        "FSIM-SV-PARSE-162");
    statement.span = span_from(start, previous());
    return statement;
  }

  if (keyword("$fflush")) {
    const auto start = advance();
    Statement statement;
    statement.kind = StatementKind::FileFlush;
    if (language_ != Language::SystemVerilog2017) {
      error(
          start,
          "FSIM-SV-SEM-074",
          "$fflush requires SystemVerilog-2017");
    }
    if (match(TokenKind::LeftParen)) {
      if (!at(TokenKind::RightParen)) {
        statement.file_handle = parse_expression();
        while (match(TokenKind::Comma)) {
          error(
              previous(),
              "FSIM-SV-SEM-075",
              "$fflush accepts at most one file handle");
          (void)parse_expression();
        }
      }
      expect(
          TokenKind::RightParen,
          "')' after $fflush arguments",
          "FSIM-SV-PARSE-201");
    }
    expect(
        TokenKind::Semicolon,
        "';' after $fflush",
        "FSIM-SV-PARSE-202");
    statement.span = span_from(start, previous());
    return statement;
  }

  if (keyword("$fclose")) {
    const auto start = advance();
    Statement statement;
    statement.kind = StatementKind::FileClose;
    if (language_ != Language::SystemVerilog2017) {
      error(
          start,
          "FSIM-SV-SEM-074",
          "$fclose requires SystemVerilog-2017");
    }
    expect(
        TokenKind::LeftParen,
        "'(' after $fclose",
        "FSIM-SV-PARSE-148");
    statement.file_handle = parse_expression();
    expect(
        TokenKind::RightParen,
        "')' after $fclose handle",
        "FSIM-SV-PARSE-149");
    expect(
        TokenKind::Semicolon,
        "';' after $fclose",
        "FSIM-SV-PARSE-150");
    statement.span = span_from(start, previous());
    return statement;
  }

  if (keyword("$fdisplay") || keyword("$fwrite")) {
    const bool newline = keyword("$fdisplay");
    const auto start = advance();
    const std::string task_name = start.text;
    Statement statement;
    statement.kind = StatementKind::FileDisplay;
    statement.output_newline = newline;
    if (language_ != Language::SystemVerilog2017) {
      error(
          start,
          "FSIM-SV-SEM-074",
          task_name + " requires SystemVerilog-2017");
    }
    expect(
        TokenKind::LeftParen,
        "'(' after " + task_name,
        "FSIM-SV-PARSE-151");
    statement.file_handle = parse_expression();
    expect(
        TokenKind::Comma,
        "',' after " + task_name + " file handle",
        "FSIM-SV-PARSE-152");
    if (!at(TokenKind::StringLiteral)) {
      error(
          current(),
          "FSIM-SV-SEM-076",
          task_name
              + " requires a literal format and at most one value");
      (void)parse_expression();
    } else {
      const auto format_token = advance();
      auto parsed_format = parse_output_format(
          decoded_string_literal_text(format_token));
      std::vector<Expression> values;
      while (match(TokenKind::Comma)) {
        values.push_back(parse_expression());
      }
      const auto consumes_value =
          [](const OutputFormat format) {
            return format != OutputFormat::Hierarchy
                && format != OutputFormat::Time;
          };
      const auto required_values =
          static_cast<std::size_t>(std::ranges::count_if(
              parsed_format.conversions,
              [&consumes_value](const auto& conversion) {
                return consumes_value(conversion.format);
              }));
      const bool unsupported_conversion =
          std::ranges::any_of(
              parsed_format.conversions,
              [](const auto& conversion) {
                return conversion.format == OutputFormat::Hierarchy
                    || conversion.format == OutputFormat::Time;
              });
      if (!parsed_format.valid || unsupported_conversion
          || parsed_format.conversions.size() > 1
          || values.size() > 1
          || values.size() != required_values) {
        error(
            format_token,
            "FSIM-SV-SEM-076",
            task_name
                + " supports a literal or one %b/%h/%o/%d/%c/%s "
                  "conversion with exactly one value");
      } else if (parsed_format.conversions.empty()) {
        statement.output_text =
            std::move(parsed_format.trailing_text);
      } else {
        auto& conversion = parsed_format.conversions.front();
        statement.output_format = conversion.format;
        statement.output_prefix = std::move(conversion.prefix);
        statement.output_suffix =
            std::move(parsed_format.trailing_text);
        statement.output_suppress_leading_zero =
            conversion.suppress_leading_zero;
        statement.output_minimum_width =
            conversion.minimum_width;
        statement.output_left_justify =
            conversion.left_justify;
        statement.output_zero_pad = conversion.zero_pad;
        if (!values.empty()) {
          statement.value = std::move(values.front());
        }
      }
    }
    expect(
        TokenKind::RightParen,
        "')' after " + task_name + " arguments",
        "FSIM-SV-PARSE-153");
    expect(
        TokenKind::Semicolon,
        "';' after " + task_name,
        "FSIM-SV-PARSE-154");
    statement.span = span_from(start, previous());
    return statement;
  }

  if (keyword("$monitoron") || keyword("$monitoroff")) {
    const auto start = advance();
    const bool enabled = start.text == "$monitoron";
    if (match(TokenKind::LeftParen)) {
      expect(
          TokenKind::RightParen,
          "')' after " + start.text,
          "FSIM-SV-PARSE-127");
    }
    expect(
        TokenKind::Semicolon,
        "';' after " + start.text,
        "FSIM-SV-PARSE-128");
    Statement statement;
    statement.kind = StatementKind::MonitorControl;
    statement.monitor_enabled = enabled;
    statement.span = span_from(start, previous());
    return statement;
  }

  if (keyword("$display") || keyword("$write")
      || keyword("$strobe") || keyword("$monitor")) {
    const bool monitor = keyword("$monitor");
    const bool postponed = keyword("$strobe") || monitor;
    const bool newline = !keyword("$write");
    const std::string_view task_name =
        monitor ? "$monitor"
        : postponed ? "$strobe"
        : newline ? "$display"
                  : "$write";
    const std::string semantic_code =
        monitor ? "FSIM-SV-SEM-041"
        : postponed ? "FSIM-SV-SEM-039"
        : newline ? "FSIM-SV-SEM-037"
                  : "FSIM-SV-SEM-038";
    const std::string close_code =
        monitor ? "FSIM-SV-PARSE-125"
        : postponed ? "FSIM-SV-PARSE-123"
        : newline ? "FSIM-SV-PARSE-119"
                  : "FSIM-SV-PARSE-121";
    const std::string semicolon_code =
        monitor ? "FSIM-SV-PARSE-126"
        : postponed ? "FSIM-SV-PARSE-124"
        : newline ? "FSIM-SV-PARSE-120"
                  : "FSIM-SV-PARSE-122";
    const auto start = advance();
    Statement statement;
    statement.kind = StatementKind::Display;
    statement.output_newline = newline;
    statement.output_postponed = postponed;
    statement.output_monitor = monitor;
    if (match(TokenKind::LeftParen)) {
      if (!at(TokenKind::RightParen)) {
        if (at(TokenKind::StringLiteral)) {
          const auto format_token = advance();
          auto parsed_format = parse_output_format(
              decoded_string_literal_text(format_token));
          std::vector<Expression> values;
          while (match(TokenKind::Comma)) {
            values.push_back(parse_expression());
          }
          const auto consumes_value =
              [](const OutputFormat format) {
                return format != OutputFormat::Hierarchy
                    && format != OutputFormat::Time;
              };
          const auto required_values =
              static_cast<std::size_t>(std::ranges::count_if(
                  parsed_format.conversions,
                  [&consumes_value](const auto& conversion) {
                    return consumes_value(conversion.format);
                  }));
          if (!parsed_format.valid) {
            error(
                format_token,
                "FSIM-SV-SEM-042",
                "the current formatted-output slice supports "
                "%b, %h/%x, %o, %d, %c, %s, %m, or %t "
                "conversion, field width, left/zero padding, "
                "and %%");
          } else if (values.size() < required_values) {
            error(
                format_token,
                semantic_code,
                std::string{task_name}
                    + " format conversions require matching value "
                      "arguments");
          } else if (values.empty()
                     && parsed_format.conversions.empty()) {
              statement.output_text =
                  std::move(parsed_format.trailing_text);
          } else if (parsed_format.conversions.size() == 1
                     && values.size() == 1
                     && consumes_value(
                         parsed_format.conversions.front().format)) {
            auto& conversion = parsed_format.conversions.front();
            statement.output_format = conversion.format;
            statement.output_prefix = std::move(conversion.prefix);
            statement.output_suffix =
                std::move(parsed_format.trailing_text);
            statement.output_suppress_leading_zero =
                conversion.suppress_leading_zero;
            statement.output_minimum_width =
                conversion.minimum_width;
            statement.output_left_justify =
                conversion.left_justify;
            statement.output_zero_pad = conversion.zero_pad;
            statement.value = std::move(values.front());
          } else {
            statement.output_values.reserve(
                parsed_format.conversions.size()
                + values.size() - required_values);
            std::size_t value_index{};
            for (std::size_t index = 0;
                 index < parsed_format.conversions.size();
                 ++index) {
              auto& conversion = parsed_format.conversions[index];
              Expression value;
              if (consumes_value(conversion.format)) {
                value = std::move(values[value_index++]);
              }
              statement.output_values.push_back(
                  OutputValue{
                      std::move(value),
                      conversion.format,
                      std::move(conversion.prefix),
                      conversion.suppress_leading_zero,
                      conversion.minimum_width,
                      conversion.left_justify,
                      conversion.zero_pad});
            }
            for (std::size_t index = value_index;
                 index < values.size(); ++index) {
              statement.output_values.push_back(
                  OutputValue{
                      std::move(values[index]),
                      OutputFormat::Decimal,
                      index == value_index
                          ? std::move(parsed_format.trailing_text)
                          : std::string{}});
            }
            if (value_index == values.size()) {
              statement.output_trailing_text =
                  std::move(parsed_format.trailing_text);
            }
          }
        } else {
          std::vector<Expression> values;
          do {
            values.push_back(parse_expression());
          } while (match(TokenKind::Comma));
          if (values.size() == 1
                     && (values.front().kind
                             == ExpressionKind::IntegerLiteral
                         || values.front().kind
                             == ExpressionKind::LogicLiteral)) {
            const auto value =
                constant_output_number(values.front().text);
            if (!value) {
              error(
                  start,
                  semantic_code,
                  "the current " + std::string{task_name}
                      + " slice requires a known numeric literal");
            } else {
              statement.output_text = *value;
            }
          } else {
            statement.output_values.reserve(values.size());
            for (auto& value : values) {
              statement.output_values.push_back(
                  OutputValue{
                      std::move(value),
                      OutputFormat::Decimal,
                      std::string{}});
            }
          }
        }
      }
      expect(
          TokenKind::RightParen,
          "')' after " + std::string{task_name} + " arguments",
          close_code);
    }
    expect(
        TokenKind::Semicolon,
        "';' after " + std::string{task_name},
        semicolon_code);
    statement.span = span_from(start, previous());
    return statement;
  }

  if (keyword("$finish")) {
    const auto start = advance();
    if (match(TokenKind::LeftParen)) {
      if (!at(TokenKind::RightParen)) {
        (void)parse_expression();
      }
      expect(TokenKind::RightParen, "')' after $finish",
             "FSIM-SV-PARSE-020");
    }
    expect(TokenKind::Semicolon, "';' after $finish",
           "FSIM-SV-PARSE-021");
    Statement statement;
    statement.kind = StatementKind::Finish;
    statement.span = span_from(start, previous());
    return statement;
  }
  if (keyword("$stop")) {
    const auto start = advance();
    if (match(TokenKind::LeftParen)) {
      if (!at(TokenKind::RightParen)) {
        (void)parse_expression();
      }
      expect(
          TokenKind::RightParen,
          "')' after $stop",
          "FSIM-SV-PARSE-112");
    }
    expect(
        TokenKind::Semicolon,
        "';' after $stop",
        "FSIM-SV-PARSE-113");
    Statement statement;
    statement.kind = StatementKind::Pause;
    statement.span = span_from(start, previous());
    return statement;
  }

  if (match(TokenKind::Semicolon)) {
    Statement statement;
    statement.kind = StatementKind::Null;
    statement.span = previous().span;
    return statement;
  }

  if (keyword("force") || keyword("release")) {
    const auto start = advance();
    const bool force = start.text == "force";
    if (language_ != Language::SystemVerilog2017) {
      error(
          start,
          "FSIM-VERILOG-SEM-011",
          "procedural force/release requires SystemVerilog");
    }
    Statement statement;
    statement.kind = force
        ? StatementKind::Force
        : StatementKind::Release;
    statement.target = parse_lvalue();
    if (force) {
      expect(
          TokenKind::Assign,
          "'=' in procedural force statement",
          "FSIM-SV-PARSE-195");
      statement.value = parse_expression();
    }
    expect(
        TokenKind::Semicolon,
        "';' after procedural force/release",
        "FSIM-SV-PARSE-196");
    statement.span = span_from(start, previous());
    return statement;
  }

  bool container_method_statement = false;
  if (at(TokenKind::Identifier)) {
    std::size_t square_depth = 0;
    for (std::size_t lookahead = 1;
         lookahead < 4096;
         ++lookahead) {
      const auto kind = current(lookahead).kind;
      if (kind == TokenKind::EndOfFile
          || (kind == TokenKind::Semicolon
              && square_depth == 0)
          || ((kind == TokenKind::Assign
               || kind == TokenKind::LessEqual
               || kind == TokenKind::PlusAssign
               || kind == TokenKind::MinusAssign
               || kind == TokenKind::StarAssign
               || kind == TokenKind::SlashAssign
               || kind == TokenKind::PercentAssign
               || kind == TokenKind::AmpersandAssign
               || kind == TokenKind::PipeAssign
               || kind == TokenKind::CaretAssign
               || kind == TokenKind::ShiftLeftAssign
               || kind == TokenKind::ShiftRightAssign
               || kind == TokenKind::ArithmeticShiftLeftAssign
               || kind == TokenKind::ArithmeticShiftRightAssign)
              && square_depth == 0)
          || (kind == TokenKind::LeftParen
              && square_depth == 0)) {
        break;
      }
      if (kind == TokenKind::LeftBracket) {
        ++square_depth;
      } else if (
          kind == TokenKind::RightBracket
          && square_depth != 0) {
        --square_depth;
      } else if (
          square_depth == 0
          && kind == TokenKind::Dot
          && current(lookahead + 1U).kind
              == TokenKind::Identifier
          && current(lookahead + 2U).kind
              == TokenKind::LeftParen
          && contains_word(
              {"delete", "insert", "push_front", "push_back",
               "pop_front", "pop_back", "exists",
               "first", "last", "next", "prev",
               "sum", "product", "and", "or", "xor",
               "reverse", "sort", "rsort", "shuffle",
               "min", "max", "unique", "unique_index",
               "find", "find_index", "find_first",
               "find_first_index", "find_last",
               "find_last_index", "putc", "itoa",
               "hextoa", "octtoa", "bintoa"},
              current(lookahead + 1U).text)) {
        container_method_statement = true;
        break;
      }
    }
  }
  if (container_method_statement) {
    const auto start = current();
    Statement statement;
    statement.kind = StatementKind::ContainerMethod;
    statement.value = parse_expression();
    expect(
        TokenKind::Semicolon,
        "';' after container method call",
        "FSIM-SV-PARSE-156");
    statement.span = span_from(start, previous());
    return statement;
  }

  if (at(TokenKind::Identifier)) {
    std::size_t lookahead = 1;
    while (at(TokenKind::Scope, lookahead) &&
           at(TokenKind::Identifier, lookahead + 1)) {
      lookahead += 2;
    }
    if (at(TokenKind::Dot, lookahead)
        && at(TokenKind::Identifier, lookahead + 1)) {
      lookahead += 2;
    }
    if (at(TokenKind::LeftParen, lookahead) ||
        at(TokenKind::Semicolon, lookahead)) {
      const auto start = advance();
      std::string name = start.text;
      while (match(TokenKind::Scope)) {
        name += "::";
        name += expect_identifier("package-scoped task name").text;
      }
      if (match(TokenKind::Dot)) {
        name += ".";
        name += expect_identifier("interface task name").text;
      }
      Statement statement;
      statement.kind = StatementKind::TaskCall;
      statement.task_name = std::move(name);
      if (match(TokenKind::LeftParen)) {
        if (!at(TokenKind::RightParen)) {
          do {
            if (match(TokenKind::Dot)) {
              const auto formal =
                  expect_identifier("named task argument");
              expect(
                  TokenKind::LeftParen,
                  "'(' after named task argument",
                  "FSIM-SV-PARSE-201");
              statement.task_argument_names.push_back(formal.text);
              if (at(TokenKind::RightParen)) {
                statement.task_arguments.emplace_back();
              } else {
                statement.task_arguments.push_back(parse_expression());
              }
              expect(
                  TokenKind::RightParen,
                  "')' after named task argument",
                  "FSIM-SV-PARSE-202");
            } else {
              statement.task_argument_names.emplace_back();
              statement.task_arguments.push_back(parse_expression());
            }
          } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RightParen, "')' after task call arguments",
               "FSIM-SV-PARSE-146");
      }
      expect(TokenKind::Semicolon, "';' after task call", "FSIM-SV-PARSE-147");
      statement.span = span_from(start, previous());
      return statement;
    }
  }

  if (at(TokenKind::Identifier)
      || at(TokenKind::PlusPlus)
      || at(TokenKind::MinusMinus)) {
    const auto before = position();
    const auto start = current();
    std::optional<Token> prefix_update;
    if (match(TokenKind::PlusPlus)
        || match(TokenKind::MinusMinus)) {
      prefix_update = previous();
    }
    Expression target = parse_lvalue();
    AssignmentKind assignment_kind{AssignmentKind::Blocking};
    std::optional<std::string> update_operation;
    bool unit_update = prefix_update.has_value();
    ProceduralUpdateKind update_kind{
        ProceduralUpdateKind::None};
    if (prefix_update) {
      update_operation =
          prefix_update->kind == TokenKind::PlusPlus ? "+" : "-";
      update_kind = ProceduralUpdateKind::Prefix;
    } else if (match(TokenKind::PlusPlus)
               || match(TokenKind::MinusMinus)) {
      update_operation =
          previous().kind == TokenKind::PlusPlus ? "+" : "-";
      unit_update = true;
      update_kind = ProceduralUpdateKind::Postfix;
    } else if (match(TokenKind::LessEqual)) {
      assignment_kind = AssignmentKind::NonBlocking;
    } else if (match(TokenKind::Assign)) {
      assignment_kind = AssignmentKind::Blocking;
    } else {
      const auto compound_operation =
          [](const TokenKind kind)
              -> std::optional<std::string_view> {
            switch (kind) {
            case TokenKind::PlusAssign:
              return "+";
            case TokenKind::MinusAssign:
              return "-";
            case TokenKind::StarAssign:
              return "*";
            case TokenKind::SlashAssign:
              return "/";
            case TokenKind::PercentAssign:
              return "%";
            case TokenKind::AmpersandAssign:
              return "&";
            case TokenKind::PipeAssign:
              return "|";
            case TokenKind::CaretAssign:
              return "^";
            case TokenKind::ShiftLeftAssign:
              return "<<";
            case TokenKind::ShiftRightAssign:
              return ">>";
            case TokenKind::ArithmeticShiftLeftAssign:
              return "<<<";
            case TokenKind::ArithmeticShiftRightAssign:
              return ">>>";
            default:
              return std::nullopt;
            }
          }(current().kind);
      if (compound_operation) {
        update_operation = *compound_operation;
        update_kind = ProceduralUpdateKind::Compound;
        advance();
      } else {
        rewind(before);
        const auto unsupported = advance();
        error(unsupported, "FSIM-SV-UNSUPPORTED-008",
              "unsupported procedural statement starting with '" +
                  unsupported.text + "'");
        skip_to_semicolon();
        return std::nullopt;
      }
    }
    if (update_operation
        && language_ == Language::Verilog2005) {
      error(
          start,
          "FSIM-VERILOG-SEM-005",
          "compound assignments and standalone increment/decrement "
          "require SystemVerilog");
    }

    std::optional<Delay> delay;
    std::vector<Sensitivity> assignment_sensitivities;
    Expression assignment_repeat_count;
    bool assignment_repeat = false;
    ProceduralAssignmentControl assignment_control{
        ProceduralAssignmentControl::None};
    if (!unit_update) {
      if (match_keyword("repeat")) {
        assignment_repeat = true;
        expect(
            TokenKind::LeftParen,
            "'(' after repeated assignment control",
            "FSIM-SV-PARSE-203");
        assignment_repeat_count = parse_expression();
        expect(
            TokenKind::RightParen,
            "')' after repeated assignment count",
            "FSIM-SV-PARSE-204");
        expect(
            TokenKind::At,
            "'@' after repeated assignment count",
            "FSIM-SV-PARSE-205");
        assignment_control =
            ProceduralAssignmentControl::Event;
        assignment_sensitivities = parse_sensitivity();
      } else if (match(TokenKind::Hash)) {
        assignment_control =
            ProceduralAssignmentControl::Delay;
        delay = parse_verilog_delay(previous());
      } else if (match(TokenKind::At)) {
        assignment_control =
            ProceduralAssignmentControl::Event;
        assignment_sensitivities = parse_sensitivity();
      }
      while (at(TokenKind::Hash) || at(TokenKind::At)) {
        const auto duplicate = advance();
        error(
            duplicate,
            "FSIM-SV-SEM-054",
            "a procedural assignment accepts only one delay or event "
            "control");
        if (duplicate.kind == TokenKind::Hash) {
          (void)parse_verilog_delay(duplicate);
        } else {
          (void)parse_sensitivity();
        }
      }
    }
    Expression value;
    bool recovered_through_semicolon = false;
    if (unit_update) {
      value = Expression{
          ExpressionKind::Binary,
          *update_operation,
          {
              target,
              Expression{
                  ExpressionKind::IntegerLiteral,
                  "1",
                  {},
                  previous().span}},
          cover(target.span, previous().span)};
    } else {
      value = parse_expression();
      if (update_operation) {
        const auto value_span = value.span;
        value = Expression{
            ExpressionKind::Binary,
            *update_operation,
            {target, std::move(value)},
            cover(target.span, value_span)};
      }
    }
    if (!recovered_through_semicolon) {
      expect(TokenKind::Semicolon, "';' after assignment",
             "FSIM-SV-PARSE-022");
    }
    Statement statement;
    statement.kind = StatementKind::Assignment;
    statement.assignment_kind = assignment_kind;
    statement.target = std::move(target);
    statement.value = std::move(value);
    statement.delay = std::move(delay);
    statement.sensitivities =
        std::move(assignment_sensitivities);
    statement.procedural_assignment_control =
        assignment_control;
    statement.procedural_assignment_repeat =
        assignment_repeat;
    statement.loop_limit =
        std::move(assignment_repeat_count);
    statement.procedural_update_kind = update_kind;
    statement.procedural_update_operator =
        update_operation.value_or(std::string{});
    statement.span = span_from(start, previous());
    return statement;
  }

  const auto unexpected = advance();
  error(unexpected, "FSIM-SV-UNSUPPORTED-009",
        "unsupported statement starting with '" + unexpected.text + "'");
  skip_to_semicolon();
  return std::nullopt;
}

std::optional<VerilogParser::DecimalRatio> VerilogParser::decimal_ratio(
  const std::string_view spelling) {
  std::string compact;
  compact.reserve(spelling.size());
  for (const char character : spelling) {
    if (character != '_') {
      compact.push_back(character);
    }
  }
  const auto exponent_position = compact.find_first_of("eE");
  if (exponent_position != std::string::npos
      && compact.find_first_of("eE", exponent_position + 1)
          != std::string::npos) {
    return std::nullopt;
  }
  const auto mantissa =
      std::string_view{compact}.substr(0, exponent_position);
  std::int64_t exponent{};
  if (exponent_position != std::string::npos) {
    auto exponent_text =
        std::string_view{compact}.substr(exponent_position + 1);
    bool negative = false;
    if (!exponent_text.empty()
        && (exponent_text.front() == '+'
            || exponent_text.front() == '-')) {
      negative = exponent_text.front() == '-';
      exponent_text.remove_prefix(1);
    }
    const auto parsed_exponent =
        decimal_i64(exponent_text, negative);
    if (!parsed_exponent) {
      return std::nullopt;
    }
    exponent = *parsed_exponent;
  }

  std::string digits;
  digits.reserve(mantissa.size());
  bool saw_decimal = false;
  std::size_t fractional_digits{};
  for (const char character : mantissa) {
    if (character == '.') {
      if (saw_decimal) {
        return std::nullopt;
      }
      saw_decimal = true;
      continue;
    }
    if (character < '0' || character > '9') {
      return std::nullopt;
    }
    digits.push_back(character);
    if (saw_decimal) {
      ++fractional_digits;
    }
  }
  if (digits.empty()) {
    return std::nullopt;
  }
  auto numerator = decimal_u64(digits);
  if (!numerator) {
    return std::nullopt;
  }
  if (*numerator == 0) {
    return DecimalRatio{};
  }
  if (fractional_digits
      > static_cast<std::size_t>(
          std::numeric_limits<std::int64_t>::max())) {
    return std::nullopt;
  }
  auto scale =
      static_cast<std::int64_t>(fractional_digits);
  if ((exponent > 0
       && scale < std::numeric_limits<std::int64_t>::min() + exponent)
      || (exponent < 0
          && scale > std::numeric_limits<std::int64_t>::max() + exponent)) {
    return std::nullopt;
  }
  scale -= exponent;
  while (scale > 0 && *numerator % 10 == 0) {
    *numerator /= 10;
    --scale;
  }
  std::uint64_t denominator = 1;
  while (scale > 0) {
    if (denominator
        > std::numeric_limits<std::uint64_t>::max() / 10) {
      return std::nullopt;
    }
    denominator *= 10;
    --scale;
  }
  while (scale < 0) {
    if (*numerator
        > std::numeric_limits<std::uint64_t>::max() / 10) {
      return std::nullopt;
    }
    *numerator *= 10;
    ++scale;
  }
  const auto divisor = std::gcd(*numerator, denominator);
  return DecimalRatio{
      *numerator / divisor,
      denominator / divisor};
}

}  // namespace fsim::frontend
