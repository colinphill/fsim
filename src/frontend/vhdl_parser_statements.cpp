// SPDX-License-Identifier: Apache-2.0
#include "vhdl_parser_internal.hpp"

namespace fsim::frontend {

std::vector<Statement> VhdlParser::parse_statement_list(
  std::initializer_list<std::string_view> terminators) {
  std::vector<Statement> statements;
  while (!at_end()) {
    bool stop = false;
    for (const auto terminator : terminators) {
      stop = stop || keyword(terminator, 0, true);
    }
    if (stop) {
      break;
    }
    std::optional<Token> opening_label;
    if (at(TokenKind::Identifier)
        && at(TokenKind::Colon, 1)) {
      opening_label = advance();
      advance();
    }
    const auto before = position();
    if (auto statement =
            parse_sequential_statement(opening_label)) {
      if (opening_label) {
        statement->label = vhdl_name(opening_label->text);
        statement->span = cover(opening_label->span, statement->span);
      }
      statements.push_back(std::move(*statement));
    }
    if (position() == before) {
      advance();
    }
  }
  return statements;
}

std::optional<Statement> VhdlParser::parse_sequential_statement(
    const std::optional<Token>& opening_label) {
  if (match_keyword("with", true)) {
    return parse_vhdl_selected_assignment(previous(), false);
  }
  if (match_keyword("wait", true)) {
    const auto start = previous();
    Statement statement;
    bool has_sensitivity_clause = false;
    bool has_condition_clause = false;
    if (match_keyword("on", true)) {
      has_sensitivity_clause = true;
      do {
        const auto signal = expect_identifier("wait sensitivity name");
        statement.sensitivities.push_back(Sensitivity{
            EdgeKind::Any, vhdl_name(signal.text), signal.span, {}});
      } while (match(TokenKind::Comma));
    }
    if (match_keyword("until", true)) {
      has_condition_clause = true;
      statement.condition = parse_expression();
    }
    if (match_keyword("for", true)) {
      statement.delay = parse_vhdl_delay(previous());
    }
    if (has_condition_clause) {
      statement.kind = StatementKind::WaitUntil;
    } else if (has_sensitivity_clause) {
      statement.kind = StatementKind::WaitOn;
    } else if (statement.delay) {
      statement.kind = StatementKind::Delay;
    } else {
      // The default sensitivity set is empty, so both a bare wait and
      // `wait until true` suspend permanently.
      statement.kind = StatementKind::WaitUntil;
      statement.condition = Expression{
          ExpressionKind::BooleanLiteral,
          "true",
          {},
          start.span};
    }
    expect(
        TokenKind::Semicolon,
        "';' after wait statement",
        "FSIM-VHDL-PARSE-049");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("return", true)) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::Return;
    if (!in_vhdl_function_ && !in_vhdl_procedure_) {
      error(
          start,
          "FSIM-VHDL-SEM-046",
          "a VHDL return statement is only supported in a subprogram body");
    }
    if (at(TokenKind::Semicolon)) {
      if (in_vhdl_function_) {
        error(
            current(),
            "FSIM-VHDL-PARSE-164",
            "a VHDL function return statement requires an expression");
      }
    } else {
      statement.value = parse_expression();
      if (in_vhdl_procedure_) {
        error(
            start,
            "FSIM-VHDL-SEM-054",
            "a VHDL procedure return statement cannot return a value");
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after return statement",
        "FSIM-VHDL-PARSE-165");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("assert", true)) {
    return parse_vhdl_assertion(previous());
  }
  if (match_keyword("report", true)) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::Report;
    statement.assertion_severity = AssertionSeverity::Note;
    const auto message = expect(
        TokenKind::StringLiteral,
        "literal string after report",
        "FSIM-VHDL-PARSE-121");
    statement.output_text = string_literal_text(message);
    if (match_keyword("severity", true)) {
      const auto severity =
          expect_identifier("report severity");
      if (detail::iequals(severity.text, "note")) {
        statement.assertion_severity = AssertionSeverity::Note;
      } else if (detail::iequals(severity.text, "warning")) {
        statement.assertion_severity = AssertionSeverity::Warning;
      } else if (detail::iequals(severity.text, "error")) {
        statement.assertion_severity = AssertionSeverity::Error;
      } else if (detail::iequals(severity.text, "failure")) {
        statement.assertion_severity = AssertionSeverity::Failure;
      } else {
        error(
            severity,
            "FSIM-VHDL-SEM-011",
            "report severity must be note, warning, error, or "
            "failure");
      }
    }
    expect(
        TokenKind::Semicolon,
        "';' after report statement",
        "FSIM-VHDL-PARSE-122");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("if", true)) {
    const auto start = previous();
    auto statement = parse_if_branch(start);
    expect_keyword("end", true, "FSIM-VHDL-PARSE-024");
    expect_keyword("if", true, "FSIM-VHDL-PARSE-025");
    parse_statement_end_label(
        opening_label
            ? vhdl_name(opening_label->text)
            : std::string_view{},
        "if");
    expect(TokenKind::Semicolon, "';' after if statement",
           "FSIM-VHDL-PARSE-026");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("case", true)) {
    const auto start = previous();
    Statement statement;
    statement.kind = StatementKind::Case;
    const bool matching = match(TokenKind::Question);
    if (matching) {
      statement.case_match_kind = CaseMatchKind::VhdlMatching;
    }
    statement.condition = parse_expression();
    expect_keyword("is", true, "FSIM-VHDL-PARSE-094");
    bool saw_others = false;
    while (!at_end() && !keyword("end", 0, true)) {
      expect_keyword("when", true, "FSIM-VHDL-PARSE-095");
      if (saw_others) {
        error(
            previous(),
            "FSIM-VHDL-SEM-022",
            "the others alternative must be last in a VHDL case "
            "statement");
      }
      CaseAlternative alternative;
      const auto alternative_start = previous();
      if (match_keyword("others", true)) {
        alternative.is_default = true;
        if (saw_others) {
          error(
              previous(),
              "FSIM-VHDL-SEM-021",
              "a VHDL case statement contains more than one others "
              "alternative");
        }
        saw_others = true;
      } else {
        do {
          alternative.choices.push_back(parse_vhdl_case_choice());
        } while (match(TokenKind::Pipe));
      }
      expect(
          TokenKind::Arrow,
          "'=>' after VHDL case choices",
          "FSIM-VHDL-PARSE-096");
      alternative.statements =
          parse_statement_list({"when", "end"});
      alternative.span =
          span_from(alternative_start, previous());
      statement.case_alternatives.push_back(
          std::move(alternative));
    }
    expect_keyword("end", true, "FSIM-VHDL-PARSE-097");
    expect_keyword("case", true, "FSIM-VHDL-PARSE-098");
    const bool matching_end = match(TokenKind::Question);
    if (matching != matching_end) {
      error(
          previous(),
          "FSIM-VHDL-SEM-086",
          "a VHDL matching case must use '?' after both opening and ending "
          "case keywords");
    }
    parse_statement_end_label(
        opening_label
            ? vhdl_name(opening_label->text)
            : std::string_view{},
        "case");
    expect(
        TokenKind::Semicolon,
        "';' after VHDL case statement",
        "FSIM-VHDL-PARSE-099");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("for", true)) {
    const auto start = opening_label.value_or(previous());
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_label =
        opening_label
            ? vhdl_name(opening_label->text)
            : std::string{};
    const auto variable =
        expect_identifier("for-loop parameter");
    statement.loop_variable = vhdl_name(variable.text);
    expect_keyword("in", true, "FSIM-VHDL-PARSE-100");
    statement.loop_initial = parse_expression();
    const bool attribute_range =
        statement.loop_initial.kind
            == ExpressionKind::Call
        && (statement.loop_initial.text == "'range"
            || statement.loop_initial.text
                == "'reverse_range");
    if (attribute_range) {
      statement.loop_limit = Expression{
          ExpressionKind::Invalid,
          {},
          {},
          statement.loop_initial.span};
    } else if (match_keyword("to", true)) {
      statement.loop_descending = false;
      statement.loop_limit = parse_expression();
    } else if (match_keyword("downto", true)) {
      statement.loop_descending = true;
      statement.loop_limit = parse_expression();
    } else {
      error(
          current(),
          "FSIM-VHDL-PARSE-101",
          "expected 'to' or 'downto' in sequential for-loop range");
      statement.loop_limit = parse_expression();
    }
    expect_keyword("loop", true, "FSIM-VHDL-PARSE-102");
    validate_opening_loop_label(
        statement.loop_label,
        opening_label.value_or(start));
    ++sequential_loop_depth_;
    sequential_loop_labels_.push_back(
        statement.loop_label);
    statement.statements = parse_statement_list({"end"});
    sequential_loop_labels_.pop_back();
    --sequential_loop_depth_;
    expect_keyword("end", true, "FSIM-VHDL-PARSE-103");
    expect_keyword("loop", true, "FSIM-VHDL-PARSE-104");
    parse_loop_end_label(statement.loop_label);
    expect(
        TokenKind::Semicolon,
        "';' after sequential for loop",
        "FSIM-VHDL-PARSE-105");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("while", true)) {
    const auto start = opening_label.value_or(previous());
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_label =
        opening_label
            ? vhdl_name(opening_label->text)
            : std::string{};
    statement.loop_runtime = true;
    statement.condition = parse_expression();
    expect_keyword("loop", true, "FSIM-VHDL-PARSE-106");
    validate_opening_loop_label(
        statement.loop_label,
        opening_label.value_or(start));
    ++sequential_loop_depth_;
    sequential_loop_labels_.push_back(
        statement.loop_label);
    statement.statements = parse_statement_list({"end"});
    sequential_loop_labels_.pop_back();
    --sequential_loop_depth_;
    expect_keyword("end", true, "FSIM-VHDL-PARSE-107");
    expect_keyword("loop", true, "FSIM-VHDL-PARSE-108");
    parse_loop_end_label(statement.loop_label);
    expect(
        TokenKind::Semicolon,
        "';' after sequential while loop",
        "FSIM-VHDL-PARSE-109");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (match_keyword("loop", true)) {
    const auto start = opening_label.value_or(previous());
    Statement statement;
    statement.kind = StatementKind::Loop;
    statement.loop_label =
        opening_label
            ? vhdl_name(opening_label->text)
            : std::string{};
    statement.loop_runtime = true;
    statement.condition = Expression{
        ExpressionKind::BooleanLiteral,
        "true",
        {},
        start.span};
    validate_opening_loop_label(
        statement.loop_label,
        opening_label.value_or(start));
    ++sequential_loop_depth_;
    sequential_loop_labels_.push_back(
        statement.loop_label);
    statement.statements = parse_statement_list({"end"});
    sequential_loop_labels_.pop_back();
    --sequential_loop_depth_;
    expect_keyword("end", true, "FSIM-VHDL-PARSE-111");
    expect_keyword("loop", true, "FSIM-VHDL-PARSE-112");
    parse_loop_end_label(statement.loop_label);
    expect(
        TokenKind::Semicolon,
        "';' after unconditional sequential loop",
        "FSIM-VHDL-PARSE-113");
    statement.span = span_from(start, previous());
    return statement;
  }
  if (keyword("exit", 0, true) || keyword("next", 0, true)) {
    const auto start = advance();
    const auto is_exit = detail::iequals(start.text, "exit");
    if (sequential_loop_depth_ == 0) {
      error(
          start,
          "FSIM-VHDL-SEM-023",
          std::string{"a VHDL "}
              + (is_exit ? "exit" : "next")
              + " statement must be nested in a sequential loop");
    }
    Statement control;
    control.kind =
        is_exit ? StatementKind::Break : StatementKind::Continue;
    control.span = start.span;
    if (at(TokenKind::Identifier)
        && !keyword("when", 0, true)) {
      const auto label = advance();
      control.loop_control_label =
          vhdl_name(label.text);
      if (sequential_loop_depth_ != 0
          && std::ranges::find(
                 sequential_loop_labels_,
                 control.loop_control_label)
              == sequential_loop_labels_.end()) {
        error(
            label,
            "FSIM-VHDL-SEM-024",
            "target loop label '"
                + control.loop_control_label
                + "' is not visible at this exit or next statement");
      }
    }
    if (match_keyword("when", true)) {
      Statement conditional;
      conditional.kind = StatementKind::If;
      conditional.condition = parse_expression();
      control.span = span_from(start, previous());
      conditional.statements.push_back(std::move(control));
      expect(
          TokenKind::Semicolon,
          "';' after VHDL exit or next statement",
          "FSIM-VHDL-PARSE-110");
      conditional.span = span_from(start, previous());
      return conditional;
    }
    expect(
        TokenKind::Semicolon,
        "';' after VHDL exit or next statement",
        "FSIM-VHDL-PARSE-110");
    control.span = span_from(start, previous());
    return control;
  }
  if (match_keyword("null", true)) {
    const auto start = previous();
    expect(TokenKind::Semicolon, "';' after null",
           "FSIM-VHDL-PARSE-027");
    Statement statement;
    statement.kind = StatementKind::Null;
    statement.span = span_from(start, previous());
    return statement;
  }
  const auto procedure_call_ahead =
      [&]() {
        if (!at(TokenKind::Identifier)) {
          return false;
        }
        std::size_t lookahead = 1;
        while (at(TokenKind::Dot, lookahead)
               && at(TokenKind::Identifier, lookahead + 1)) {
          lookahead += 2;
        }
        if (at(TokenKind::Semicolon, lookahead)) {
          return true;
        }
        if (!at(TokenKind::LeftParen, lookahead)) {
          return false;
        }
        std::size_t depth = 0;
        do {
          if (at(TokenKind::LeftParen, lookahead)) {
            ++depth;
          } else if (at(TokenKind::RightParen, lookahead)) {
            --depth;
          } else if (at(TokenKind::EndOfFile, lookahead)) {
            return false;
          }
          ++lookahead;
        } while (depth != 0);
        return at(TokenKind::Semicolon, lookahead);
      };
  if (procedure_call_ahead()) {
    return parse_vhdl_procedure_call();
  }
  if (auto assignment = parse_assignment(false)) {
    return assignment;
  }
  const auto unsupported = advance();
  error(unsupported, "FSIM-VHDL-UNSUPPORTED-008",
        "unsupported sequential statement starting with '" +
            unsupported.text + "'");
  skip_to_semicolon();
  return std::nullopt;
}

std::optional<Statement>
VhdlParser::parse_vhdl_procedure_call() {
  if (!at(TokenKind::Identifier)) {
    return std::nullopt;
  }
  const auto start_position = position();
  const auto start = advance();
  std::string name = vhdl_name(start.text);
  while (match(TokenKind::Dot)) {
    name += '.';
    name += vhdl_name(
        expect_identifier("selected procedure name").text);
  }

  Statement statement;
  statement.kind = StatementKind::ProcedureCall;
  statement.procedure_name = std::move(name);
  if (match(TokenKind::LeftParen)) {
    bool saw_named = false;
    if (!at(TokenKind::RightParen)) {
      do {
        const auto association_start = current();
        SubprogramAssociation association;
        if (at(TokenKind::Identifier)
            && at(TokenKind::Arrow, 1)) {
          saw_named = true;
          association.formal =
              vhdl_name(advance().text);
          advance();
          association.value = parse_expression();
        } else {
          if (saw_named) {
            error(
                current(),
                "FSIM-VHDL-SEM-055",
                "a positional procedure actual cannot follow a named "
                "actual");
          }
          association.value = parse_expression();
        }
        association.span =
            span_from(association_start, previous());
        statement.procedure_arguments.push_back(
            std::move(association));
      } while (match(TokenKind::Comma));
    }
    expect(
        TokenKind::RightParen,
        "')' after procedure call arguments",
        "FSIM-VHDL-PARSE-179");
  } else if (!at(TokenKind::Semicolon)) {
    rewind(start_position);
    return std::nullopt;
  }
  expect(
      TokenKind::Semicolon,
      "';' after procedure call",
      "FSIM-VHDL-PARSE-180");
  statement.span = span_from(start, previous());
  return statement;
}

Statement VhdlParser::parse_vhdl_assertion(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::Assert;
  statement.condition = parse_expression();
  if (match_keyword("report", true)) {
    const auto message = expect(
        TokenKind::StringLiteral, "string literal after report",
        "FSIM-VHDL-PARSE-045");
    statement.assertion_message = string_literal_text(message);
  }
  if (match_keyword("severity", true)) {
    const auto severity = expect_identifier("assertion severity");
    const auto canonical = detail::ascii_lower(severity.text);
    if (canonical == "note") {
      statement.assertion_severity = AssertionSeverity::Note;
    } else if (canonical == "warning") {
      statement.assertion_severity = AssertionSeverity::Warning;
    } else if (canonical == "error") {
      statement.assertion_severity = AssertionSeverity::Error;
    } else if (canonical == "failure") {
      statement.assertion_severity = AssertionSeverity::Failure;
    } else {
      error(
          severity, "FSIM-VHDL-SEM-011",
          "assertion severity must be note, warning, error, or failure");
    }
  }
  expect(TokenKind::Semicolon, "';' after assertion",
         "FSIM-VHDL-PARSE-046");
  statement.span = span_from(start, previous());
  return statement;
}

void VhdlParser::validate_opening_loop_label(
  const std::string_view label,
  const Token& token) {
  if (label.empty()) {
    return;
  }
  if (std::ranges::find(
          sequential_loop_labels_seen_, label)
      != sequential_loop_labels_seen_.end()) {
    error(
        token,
        "FSIM-VHDL-SEM-026",
        "sequential loop label '"
            + std::string{label}
            + "' duplicates another label in this process");
    return;
  }
  sequential_loop_labels_seen_.emplace_back(label);
}

void VhdlParser::parse_loop_end_label(
  const std::string_view opening_label) {
  if (!at(TokenKind::Identifier)) {
    return;
  }
  const auto end_label = advance();
  const auto canonical =
      vhdl_name(end_label.text);
  if (opening_label.empty()) {
    error(
        end_label,
        "FSIM-VHDL-SEM-025",
        "an end-loop label requires a matching opening loop label");
  } else if (canonical != opening_label) {
    error(
        end_label,
        "FSIM-VHDL-SEM-025",
        "end-loop label '" + canonical
            + "' does not match opening label '"
            + std::string{opening_label} + "'");
  }
}

void VhdlParser::parse_statement_end_label(
    const std::string_view opening_label,
    const std::string_view statement_kind) {
  if (!at(TokenKind::Identifier)) {
    return;
  }
  const auto end_label = advance();
  const auto canonical = vhdl_name(end_label.text);
  if (opening_label.empty()) {
    error(
        end_label,
        "FSIM-VHDL-SEM-084",
        "an end-" + std::string{statement_kind}
            + " label requires a matching opening label");
  } else if (canonical != opening_label) {
    error(
        end_label,
        "FSIM-VHDL-SEM-084",
        "end-" + std::string{statement_kind} + " label '"
            + canonical + "' does not match opening label '"
            + std::string{opening_label} + "'");
  }
}

Statement VhdlParser::parse_if_branch(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::If;
  statement.condition = parse_expression();
  expect_keyword("then", true, "FSIM-VHDL-PARSE-028");
  statement.statements = parse_statement_list({"elsif", "else", "end"});
  if (match_keyword("elsif", true)) {
    const auto elsif = previous();
    statement.else_statements.push_back(parse_if_branch(elsif));
  } else if (match_keyword("else", true)) {
    statement.else_statements = parse_statement_list({"end"});
  }
  statement.span = span_from(start, previous());
  return statement;
}

std::optional<Statement> VhdlParser::parse_assignment(bool concurrent) {
  const auto start_position = position();
  if (!at(TokenKind::Identifier)) {
    return std::nullopt;
  }
  const auto start = current();
  Expression target = parse_lvalue();
  AssignmentKind kind = AssignmentKind::VhdlSignal;
  if (match(TokenKind::LessEqual)) {
    kind = concurrent ? AssignmentKind::Continuous
                      : AssignmentKind::VhdlSignal;
  } else if (!concurrent && match(TokenKind::ColonEqual)) {
    kind = AssignmentKind::Blocking;
  } else {
    rewind(start_position);
    return std::nullopt;
  }

  Statement statement;
  statement.kind = StatementKind::Assignment;
  statement.assignment_kind = kind;
  statement.target = std::move(target);
  if (kind != AssignmentKind::Blocking) {
    if (match_keyword("guarded", true)) {
      statement.vhdl_guarded_assignment = true;
      if (!concurrent) {
        error(
            previous(),
            "FSIM-VHDL-SEM-087",
            "guarded is permitted only on a concurrent signal assignment");
      }
    }
    parse_vhdl_delay_mechanism(statement);
    statement = parse_conditional_signal_assignment(std::move(statement));
  } else {
    statement.value = parse_conditional_assignment_value();
    diagnose_misplaced_vhdl_delay_mechanism();
    if (match_keyword("after", true)) {
      statement.delay = parse_vhdl_delay(previous());
    }
  }
  expect(TokenKind::Semicolon, "';' after assignment",
         "FSIM-VHDL-PARSE-029");
  statement.span = span_from(start, previous());
  return statement;
}

Statement VhdlParser::parse_vhdl_selected_assignment(
    const Token& start,
    const bool concurrent) {
  Statement statement;
  statement.kind = StatementKind::Case;
  statement.condition = parse_expression();
  expect_keyword("select", true, "FSIM-VHDL-PARSE-116");
  if (match(TokenKind::Question)) {
    statement.case_match_kind = CaseMatchKind::VhdlMatching;
  }
  const auto target = parse_lvalue();
  AssignmentKind assignment_kind{};
  if (match(TokenKind::LessEqual)) {
    assignment_kind = concurrent ? AssignmentKind::Continuous
                                 : AssignmentKind::VhdlSignal;
  } else if (!concurrent && match(TokenKind::ColonEqual)) {
    assignment_kind = AssignmentKind::Blocking;
  } else {
    error(
        current(),
        "FSIM-VHDL-PARSE-117",
        concurrent
            ? "expected '<=' in selected signal assignment"
            : "expected '<=' or ':=' in sequential selected assignment");
  }
  Statement common_assignment;
  if (assignment_kind != AssignmentKind::Blocking) {
    if (match_keyword("guarded", true)) {
      common_assignment.vhdl_guarded_assignment = true;
      if (!concurrent) {
        error(
            previous(),
            "FSIM-VHDL-SEM-087",
            "guarded is permitted only on a concurrent signal assignment");
      }
    }
    common_assignment.vhdl_delay_mechanism =
        VhdlDelayMechanism::ImplicitInertial;
    parse_vhdl_delay_mechanism(common_assignment);
  }

  bool saw_others = false;
  do {
    CaseAlternative alternative;
    const auto alternative_start = current();
    Statement assignment;
    assignment.kind = StatementKind::Assignment;
    assignment.assignment_kind = assignment_kind;
    assignment.target = target;
    assignment.vhdl_delay_mechanism =
        common_assignment.vhdl_delay_mechanism;
    assignment.vhdl_rejection_limit =
        common_assignment.vhdl_rejection_limit;
    assignment.vhdl_guarded_assignment =
        common_assignment.vhdl_guarded_assignment;
    if (assignment_kind == AssignmentKind::Blocking) {
      assignment.value = parse_expression();
      diagnose_misplaced_vhdl_delay_mechanism();
    } else {
      parse_vhdl_waveform(assignment);
    }
    expect_keyword("when", true, "FSIM-VHDL-PARSE-118");
    if (match_keyword("others", true)) {
      alternative.is_default = true;
      if (saw_others) {
        error(
            previous(),
            "FSIM-VHDL-SEM-027",
            "a selected assignment contains more than one others "
            "alternative");
      }
      saw_others = true;
    } else {
      if (saw_others) {
        error(
            current(),
            "FSIM-VHDL-SEM-028",
            "the others alternative must be last in a selected "
            "assignment");
      }
      do {
        alternative.choices.push_back(parse_vhdl_case_choice());
      } while (match(TokenKind::Pipe));
    }
    assignment.span =
        cover(alternative_start.span, previous().span);
    alternative.statements.push_back(std::move(assignment));
    alternative.span =
        cover(alternative_start.span, previous().span);
    statement.case_alternatives.push_back(
        std::move(alternative));
  } while (match(TokenKind::Comma));
  if (!saw_others) {
    error(
        current(),
        "FSIM-VHDL-SEM-029",
        "the bounded selected-assignment form requires a final others "
        "alternative");
  }
  expect(
      TokenKind::Semicolon,
      "';' after selected signal assignment",
      "FSIM-VHDL-PARSE-119");
  statement.span = span_from(start, previous());
  statement.vhdl_guarded_assignment =
      common_assignment.vhdl_guarded_assignment;
  return statement;
}

Expression VhdlParser::parse_vhdl_case_choice() {
  auto left = parse_expression();
  if (!match_keyword("to", true)
      && !match_keyword("downto", true)) {
    return left;
  }
  const auto direction = vhdl_name(previous().text);
  auto right = parse_expression();
  const auto span = cover(left.span, right.span);
  return Expression{
      ExpressionKind::Call,
      direction == "downto"
          ? "@vhdl-case-range-downto"
          : "@vhdl-case-range-to",
      {std::move(left), std::move(right)},
      span};
}

Statement VhdlParser::parse_conditional_signal_assignment(Statement assignment) {
  const auto assignment_start = assignment.target.span;
  parse_vhdl_waveform(assignment);
  if (!match_keyword("when", true)) {
    assignment.span = cover(assignment_start, previous().span);
    return assignment;
  }

  Statement conditional;
  conditional.kind = StatementKind::If;
  conditional.vhdl_conditional_assignment = true;
  conditional.vhdl_guarded_assignment =
      assignment.vhdl_guarded_assignment;
  conditional.condition = parse_expression();
  expect_keyword("else", true, "FSIM-VHDL-PARSE-115");
  conditional.statements.push_back(std::move(assignment));

  Statement alternate;
  alternate.kind = StatementKind::Assignment;
  alternate.assignment_kind =
      conditional.statements.front().assignment_kind;
  alternate.target = conditional.statements.front().target;
  alternate.vhdl_delay_mechanism =
      conditional.statements.front().vhdl_delay_mechanism;
  alternate.vhdl_rejection_limit =
      conditional.statements.front().vhdl_rejection_limit;
  alternate.vhdl_guarded_assignment =
      conditional.statements.front().vhdl_guarded_assignment;
  conditional.else_statements.push_back(
      parse_conditional_signal_assignment(std::move(alternate)));
  conditional.span = cover(assignment_start, previous().span);
  return conditional;
}

void VhdlParser::parse_vhdl_waveform(Statement& statement) {
  if (match_keyword("unaffected", true)) {
    statement.vhdl_unaffected = true;
    if (at(TokenKind::Comma)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-125",
          "'unaffected' must be the complete waveform alternative");
      while (!at(TokenKind::Semicolon)
             && !at(TokenKind::EndOfFile)) {
        advance();
      }
    }
    return;
  }

  do {
    const auto start = current();
    VhdlWaveformElement element;
    if (match_keyword("null", true)) {
      element.disconnect = true;
      if (!statement.vhdl_guarded_assignment) {
        error(
            previous(),
            "FSIM-VHDL-UNSUPPORTED-025",
            "null waveform elements require a guarded concurrent "
            "signal assignment");
      }
    } else if (
        at(TokenKind::Semicolon) || keyword("when", 0, true)
        || keyword("else", 0, true)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-126",
          "expected a value or 'unaffected' in a VHDL waveform");
      element.value = Expression{
          ExpressionKind::IntegerLiteral, "0", {}, current().span};
    } else {
      element.value = parse_expression();
    }
    diagnose_misplaced_vhdl_delay_mechanism();
    if (match_keyword("after", true)) {
      element.delay = parse_vhdl_delay(previous());
    }
    element.span = cover(start.span, previous().span);
    statement.vhdl_waveform.push_back(std::move(element));
  } while (match(TokenKind::Comma));

  if (statement.vhdl_waveform.empty()) {
    error(
        current(),
        "FSIM-VHDL-PARSE-126",
        "expected at least one element in a VHDL waveform");
    return;
  }
  statement.value = statement.vhdl_waveform.front().value;
  statement.delay = statement.vhdl_waveform.front().delay;
}

void VhdlParser::parse_vhdl_delay_mechanism(Statement& statement) {
  statement.vhdl_delay_mechanism =
      VhdlDelayMechanism::ImplicitInertial;
  if (match_keyword("transport", true)) {
    statement.vhdl_delay_mechanism =
        VhdlDelayMechanism::Transport;
    return;
  }
  if (match_keyword("inertial", true)) {
    statement.vhdl_delay_mechanism =
        VhdlDelayMechanism::Inertial;
    return;
  }
  if (!match_keyword("reject", true)) {
    return;
  }

  const auto reject = previous();
  statement.vhdl_delay_mechanism =
      VhdlDelayMechanism::Inertial;
  if (match(TokenKind::Minus)) {
    error(
        previous(),
        "FSIM-VHDL-SEM-031",
        "a VHDL rejection limit must be nonnegative");
  }
  statement.vhdl_rejection_limit =
      parse_vhdl_delay(reject);
  if (!match_keyword("inertial", true)) {
    error(
        current(),
        "FSIM-VHDL-PARSE-123",
        "expected 'inertial' after a VHDL reject time");
    if (keyword("transport", 0, true)) {
      advance();
    }
  }
}

void VhdlParser::diagnose_misplaced_vhdl_delay_mechanism() {
  if (!keyword("transport", 0, true)
      && !keyword("inertial", 0, true)
      && !keyword("reject", 0, true)) {
    return;
  }
  error(
      current(),
      "FSIM-VHDL-PARSE-124",
      "a VHDL delay mechanism must immediately follow '<='");
  Statement ignored;
  parse_vhdl_delay_mechanism(ignored);
}

Expression VhdlParser::parse_conditional_assignment_value() {
  auto when_true = parse_expression();
  if (!match_keyword("when", true)) {
    return when_true;
  }
  const auto begin_span = when_true.span;
  auto condition = parse_expression();
  expect_keyword("else", true, "FSIM-VHDL-PARSE-115");
  auto when_false = parse_conditional_assignment_value();
  const auto span = cover(begin_span, when_false.span);
  return Expression{
      ExpressionKind::Call,
      "?:",
      {
          std::move(condition),
          std::move(when_true),
          std::move(when_false)},
      span};
}

Delay VhdlParser::parse_vhdl_delay(const Token& start) {
  Delay delay;
  const auto magnitude =
      expect(TokenKind::Number, "delay magnitude", "FSIM-VHDL-PARSE-030");
  if (const auto parsed = decimal_u64(magnitude.text)) {
    delay.magnitude = *parsed;
  } else {
    error(magnitude, "FSIM-VHDL-SEM-004",
          "delay magnitude must be an integer literal");
  }
  const auto unit = expect_identifier("physical time unit");
  delay.unit = detail::ascii_lower(unit.text);
  delay.span = span_from(start, unit);
  return delay;
}

Expression VhdlParser::parse_lvalue() {
  const auto name = expect_identifier("assignment target");
  Expression expression{ExpressionKind::Identifier, vhdl_name(name.text),
                        {}, name.span};
  while (match(TokenKind::Dot)) {
    const auto member =
        expect_identifier("selected record element");
    const auto canonical_member = vhdl_name(member.text);
    if (canonical_member == "all") {
      expression = Expression{
          ExpressionKind::Call,
          "@vhdl-dereference",
          {std::move(expression)},
          cover(name.span, member.span)};
    } else if (expression.kind == ExpressionKind::Identifier) {
      expression.text += '.';
      expression.text += canonical_member;
      expression.span = cover(expression.span, member.span);
    } else {
      expression = Expression{
          ExpressionKind::Call,
          "@vhdl-member:" + canonical_member,
          {std::move(expression)},
          cover(name.span, member.span)};
    }
  }
  for (;;) {
    if (match(TokenKind::LeftParen)) {
      bool saw_slice = false;
      do {
        Expression first = parse_expression();
        if (match_keyword("downto", true) || match_keyword("to", true)) {
          saw_slice = true;
          const auto direction = previous();
          Expression second = parse_expression();
          expression = Expression{
              ExpressionKind::Slice, detail::ascii_lower(direction.text),
              {std::move(expression), std::move(first), std::move(second)},
              cover(expression.span, second.span)};
        } else {
          expression = Expression{
              ExpressionKind::Index, "index",
              {std::move(expression), std::move(first)},
              cover(expression.span, first.span)};
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')' after indices",
             saw_slice ? "FSIM-VHDL-PARSE-031"
                       : "FSIM-VHDL-PARSE-032");
      expression.span = cover(expression.span, previous().span);
      continue;
    }
    if (!match(TokenKind::Dot)) {
      break;
    }
    const auto member = expect_identifier("selected record element");
    if (vhdl_name(member.text) == "all") {
      expression = Expression{
          ExpressionKind::Call,
          "@vhdl-dereference",
          {std::move(expression)},
          cover(name.span, member.span)};
    } else {
      expression = Expression{
          ExpressionKind::Call,
          "@vhdl-member:" + vhdl_name(member.text),
          {std::move(expression)},
          cover(name.span, member.span)};
    }
  }
  return expression;
}

}  // namespace fsim::frontend
