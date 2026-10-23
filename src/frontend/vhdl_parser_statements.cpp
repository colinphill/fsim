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

void VhdlParser::parse_sequential_block_declarations(
    Statement& statement) {
  std::vector<ParameterDeclaration> constants;
  std::vector<TypeAliasDeclaration> type_aliases;
  std::vector<SignalAliasDeclaration> signal_aliases;
  std::vector<VariableDeclaration> variables;
  std::vector<PackageInstantiation> package_instances;
  std::vector<FunctionDeclaration> functions;
  std::vector<ProcedureDeclaration> procedures;
  std::vector<VhdlAttributeDeclaration> attributes;
  std::vector<VhdlGroupDeclaration> groups;
  while (!at_end() && !keyword("begin", 0, true)) {
    if (match_keyword("file", true)) {
      parse_vhdl_file_declaration(variables, previous());
      continue;
    }
    if (parse_vhdl_local_nonobject_declaration(
            constants,
            type_aliases,
            signal_aliases,
            variables,
            package_instances,
            functions,
            procedures,
            attributes,
            groups)) {
      continue;
    }
    if (!match_keyword("variable", true)) {
      const auto declaration = current();
      error(
          declaration,
          "FSIM-VHDL-UNSUPPORTED-056",
          "unsupported sequential block declarative item");
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
        "FSIM-VHDL-PARSE-289");
    const auto type = parse_vhdl_type(true, true);
    std::optional<Expression> initializer;
    if (match(TokenKind::ColonEqual)) {
      initializer = parse_expression();
    }
    expect(
        TokenKind::Semicolon,
        "';' after variable declaration",
        "FSIM-VHDL-PARSE-290");
    for (const auto& name : names) {
      variables.push_back(VariableDeclaration{
          vhdl_name(name.text),
          type,
          initializer,
          span_from(name, previous())});
      note_vhdl_access_object(vhdl_name(name.text), type);
    }
  }
  validate_vhdl_local_declaration_names(
      constants,
      type_aliases,
      signal_aliases,
      variables,
      package_instances,
      functions,
      procedures);
  statement.constants = std::move(constants);
  statement.type_aliases = std::move(type_aliases);
  statement.signal_aliases = std::move(signal_aliases);
  statement.package_instances = std::move(package_instances);
  statement.functions = std::move(functions);
  statement.procedures = std::move(procedures);
  statement.declarations = std::move(variables);
  statement.vhdl_attributes = std::move(attributes);
  statement.vhdl_groups = std::move(groups);
}

std::optional<Statement> VhdlParser::parse_sequential_statement(
    const std::optional<Token>& opening_label) {
  if (match_keyword("block", true)) {
    const auto start = previous();
    (void)require_vhdl_standard(
        start,
        VhdlStandard::Vhdl2019,
        "sequential block statements",
        "move the declarations to the enclosing process or subprogram");
    Statement statement;
    statement.kind = StatementKind::Block;
    const auto outer_named_types = vhdl_named_types_;
    const auto outer_named_type_kinds = vhdl_named_type_kinds_;
    match_keyword("is", true);
    parse_sequential_block_declarations(statement);
    expect_keyword("begin", true, "FSIM-VHDL-PARSE-291");
    statement.statements = parse_statement_list({"end"});
    expect_keyword("end", true, "FSIM-VHDL-PARSE-292");
    match_keyword("block", true);
    parse_statement_end_label(
        opening_label
            ? vhdl_name(opening_label->text)
            : std::string_view{},
        "sequential block");
    expect(
        TokenKind::Semicolon,
        "';' after sequential block statement",
        "FSIM-VHDL-PARSE-293");
    statement.span = span_from(start, previous());
    vhdl_named_types_ = outer_named_types;
    vhdl_named_type_kinds_ = outer_named_type_kinds;
    return statement;
  }
  if (match_keyword("with", true)) {
      (void)require_vhdl_standard(
          previous(),
          VhdlStandard::Vhdl2008,
          "sequential selected assignments",
          "replace the assignment with a sequential case statement");
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
        auto expression = parse_expression();
        const bool identifier =
            expression.kind == ExpressionKind::Identifier;
        // A name keeps its expression too, so a record field (`s.x`) or a
        // signal formal resolves where the design-signal name does not
        // (IEEE 1076-2008 10.2).
        auto name = identifier ? vhdl_name(expression.text) : std::string{};
        statement.sensitivities.push_back(Sensitivity{
            EdgeKind::Any,
            std::move(name),
            expression.span,
            std::move(expression)});
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
    (void)require_vhdl_standard(
        start,
        VhdlStandard::Vhdl1993,
        "report statements",
        "use an assertion statement with an equivalent report clause in VHDL-87");
    Statement statement;
    statement.kind = StatementKind::Report;
    statement.assertion_severity = AssertionSeverity::Note;
    if (keyword("severity", 0, true)
        || at(TokenKind::Semicolon)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-261",
          "a VHDL report statement requires a report expression");
    } else {
      statement.vhdl_report_expression = parse_expression();
      if (statement.vhdl_report_expression.kind
          == ExpressionKind::StringLiteral) {
        statement.output_text = string_literal_text(Token{
            TokenKind::StringLiteral,
            statement.vhdl_report_expression.text,
            statement.vhdl_report_expression.span,
            {}});
      }
    }
    parse_vhdl_severity_clause(statement);
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
        (void)require_vhdl_standard(
            previous(),
            VhdlStandard::Vhdl2008,
            "matching case statements",
            "use an ordinary case statement with exact choices");
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
    if (statement.loop_initial.kind == ExpressionKind::Identifier
        && keyword("range", 0, true)) {
      // `T range L to R`: the constraint supplies the iteration bounds.
      advance();
      statement.loop_initial = parse_expression();
    } else if (statement.loop_initial.kind == ExpressionKind::Identifier
               && keyword("loop", 0, true)) {
      // A discrete type mark iterates its whole range (IEEE 1076-2008
      // 10.10). Preserve the inherited direction with the range profile.
      const auto mark = statement.loop_initial;
      statement.loop_initial = Expression{
          ExpressionKind::Call, "'range", {mark}, mark.span};
    }
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
        // A VHDL-2019 call may carry a generic map before its actuals.
        if (vhdl_standard_ >= VhdlStandard::Vhdl2019
            && keyword("generic", lookahead, true)
            && keyword("map", lookahead + 1U, true)
            && at(TokenKind::LeftParen, lookahead + 2U)) {
          const auto map_closed = skip_vhdl_parentheses(lookahead + 2U);
          if (!map_closed) {
            return false;
          }
          lookahead = *map_closed;
        }
        if (at(TokenKind::Semicolon, lookahead)) {
          return true;
        }
        if (!at(TokenKind::LeftParen, lookahead)) {
          return false;
        }
        const auto closed = skip_vhdl_parentheses(lookahead);
        if (!closed) {
          return false;
        }
        lookahead = *closed;
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

bool VhdlParser::vhdl_individual_formal_ahead() const {
  if (!at(TokenKind::Identifier)) {
    return false;
  }
  std::size_t lookahead = 1;
  bool selected = false;
  for (;;) {
    if (at(TokenKind::Dot, lookahead)
        && at(TokenKind::Identifier, lookahead + 1)) {
      lookahead += 2;
      selected = true;
      continue;
    }
    if (!at(TokenKind::LeftParen, lookahead)) {
      break;
    }
    const auto closed = skip_vhdl_parentheses(lookahead);
    if (!closed) {
      return false;
    }
    lookahead = *closed;
    selected = true;
  }
  return selected && at(TokenKind::Arrow, lookahead);
}

std::string VhdlParser::parse_vhdl_individual_part(VhdlIndividualPart &part) {
  auto base = vhdl_name(advance().text);
  for (;;) {
    if (match(TokenKind::Dot)) {
      const auto element = expect_identifier("record element name");
      part.path.push_back(Expression { ExpressionKind::Identifier,
          vhdl_name(element.text), { }, element.span });
      continue;
    }
    if (!match(TokenKind::LeftParen)) {
      break;
    }
    do {
      auto selector = parse_expression();
      // A slice of the formal (`p(3 downto 0)`) is a range choice.
      if (keyword("downto", 0, true) || keyword("to", 0, true)) {
        const auto direction = advance();
        auto right = parse_expression();
        const auto span = cover(selector.span, right.span);
        selector = Expression { ExpressionKind::Binary,
            detail::ascii_lower(direction.text),
            { std::move(selector), std::move(right) }, span };
      }
      part.path.push_back(std::move(selector));
    } while (match(TokenKind::Comma));
    expect(TokenKind::RightParen, "')' after formal subelement",
        "FSIM-VHDL-PARSE-179");
  }
  expect(TokenKind::Arrow, "'=>' after formal subelement",
      "FSIM-VHDL-PARSE-179");
  part.value = parse_expression();
  return base;
}

Expression VhdlParser::build_vhdl_individual_aggregate(
    const std::vector<VhdlIndividualPart> &all_parts,
    const SourceSpan &aggregate_span) const {
  const auto choice_key = [](const auto& self, const Expression& choice)
      -> std::string {
    auto key = std::to_string(static_cast<int>(choice.kind)) + ":"
        + detail::ascii_lower(choice.text) + "(";
    for (const auto& operand : choice.operands) {
      key += self(self, operand) + ",";
    }
    return key + ")";
  };
  // Parts sharing a selector at `depth` form one nested association.
  const auto build = [&](const auto& self,
                         const std::vector<const VhdlIndividualPart*>& parts,
                         const std::size_t depth,
                         const SourceSpan& span) -> Expression {
    Expression aggregate { ExpressionKind::Aggregate, "", { }, span };
    std::vector<std::string> keys;
    std::vector<std::vector<const VhdlIndividualPart*>> groups;
    for (const auto* part : parts) {
      if (depth >= part->path.size()) {
        continue;
      }
      const auto key = choice_key(choice_key, part->path[depth]);
      const auto found = std::ranges::find(keys, key);
      if (found == keys.end()) {
        keys.push_back(key);
        groups.push_back({ part });
      } else {
        groups[static_cast<std::size_t>(
                   std::distance(keys.begin(), found))]
            .push_back(part);
      }
    }
    for (const auto& group : groups) {
      auto choice = group.front()->path[depth];
      auto value = group.size() == 1U
              && group.front()->path.size() == depth + 1U
          ? group.front()->value
          : self(self, group, depth + 1U, span);
      if (choice.kind == ExpressionKind::Identifier) {
        choice.text = vhdl_name(choice.text);
      }
      aggregate.aggregate_choices.push_back(
          choice.kind == ExpressionKind::Identifier ? choice.text
                                                    : "@array");
      aggregate.aggregate_choice_expressions.push_back(
          std::vector<Expression> { std::move(choice) });
      aggregate.operands.push_back(std::move(value));
    }
    return aggregate;
  };

  std::vector<const VhdlIndividualPart*> pointers;
  for (const auto& part : all_parts) {
    pointers.push_back(&part);
  }
  return build(build, pointers, 0U, aggregate_span);
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

  // `p generic map (...) (args);` calls an implicit instance of the generic
  // procedure `p` (IEEE 1076-2019 9.3.4).
  name = parse_vhdl_call_generic_map(start, std::move(name), true);

  Statement statement;
  statement.kind = StatementKind::ProcedureCall;
  statement.procedure_name = std::move(name);
  if (const auto api = vhdl_simulator_api(statement.procedure_name);
      api != VhdlSimulatorApi::none) {
    require_vhdl_standard(
        start, vhdl_simulator_api_standard(api),
        "the STD.ENV simulator, data, and time interface",
        "select VHDL-2019 or remove the STD.ENV call");
  }
  if (match(TokenKind::LeftParen)) {
    bool saw_named = false;
    // Individually associated subelements of one formal become one named
    // aggregate actual for that formal (IEEE 1076-2008 6.5.7.1).
    std::vector<std::pair<std::string, std::vector<VhdlIndividualPart>>>
        individual;
    if (!at(TokenKind::RightParen)) {
      do {
        const auto association_start = current();
        SubprogramAssociation association;
        if (vhdl_individual_formal_ahead()) {
          saw_named = true;
          VhdlIndividualPart part;
          const auto base = parse_vhdl_individual_part(part);
          auto group = std::ranges::find(individual, base,
              &std::pair<std::string, std::vector<VhdlIndividualPart>>::first);
          if (group == individual.end()) {
            individual.emplace_back(base, std::vector<VhdlIndividualPart> { });
            group = std::prev(individual.end());
            // The aggregate is filled in once every part is known.
            association.formal = base;
            association.value = Expression { ExpressionKind::Aggregate,
                "", { }, association_start.span };
            association.span = span_from(association_start, previous());
            statement.procedure_arguments.push_back(std::move(association));
          }
          group->second.push_back(std::move(part));
          continue;
        }
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
    for (auto& [base, parts] : individual) {
      auto target = std::ranges::find_if(statement.procedure_arguments,
          [&](const SubprogramAssociation& candidate) {
            return candidate.formal == base
                && candidate.value.kind == ExpressionKind::Aggregate
                && candidate.value.operands.empty();
          });
      if (target != statement.procedure_arguments.end()) {
        target->value = build_vhdl_individual_aggregate(
            parts, target->value.span);
      }
    }
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

// An optional `severity expression` clause of a report or assertion; a
// literal severity level also sets the statement's static severity.
void VhdlParser::parse_vhdl_severity_clause(Statement& statement) {
  if (!match_keyword("severity", true)) {
    return;
  }
  if (at(TokenKind::Semicolon)) {
    error(
        current(),
        "FSIM-VHDL-PARSE-262",
        "a VHDL severity clause requires an expression");
  } else {
    statement.vhdl_severity_expression = parse_expression();
  }
  const auto& severity = statement.vhdl_severity_expression;
  if (severity.kind != ExpressionKind::Identifier) {
    return;
  }
  if (severity.text == "note") {
    statement.assertion_severity = AssertionSeverity::Note;
  } else if (severity.text == "warning") {
    statement.assertion_severity = AssertionSeverity::Warning;
  } else if (severity.text == "error") {
    statement.assertion_severity = AssertionSeverity::Error;
  } else if (severity.text == "failure") {
    statement.assertion_severity = AssertionSeverity::Failure;
  }
}

Statement VhdlParser::parse_vhdl_assertion(const Token& start) {
  Statement statement;
  statement.kind = StatementKind::Assert;
  statement.condition = parse_expression();
  if (match_keyword("report", true)) {
    if (keyword("severity", 0, true)
        || at(TokenKind::Semicolon)) {
      error(
          current(),
          "FSIM-VHDL-PARSE-261",
          "a VHDL assertion report clause requires an expression");
    } else {
      statement.vhdl_report_expression = parse_expression();
      if (statement.vhdl_report_expression.kind
          == ExpressionKind::StringLiteral) {
        statement.assertion_message = string_literal_text(Token{
            TokenKind::StringLiteral,
            statement.vhdl_report_expression.text,
            statement.vhdl_report_expression.span,
            {}});
      }
    }
  }
  parse_vhdl_severity_clause(statement);
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

// An aggregate assignment target, possibly nested: `((a, b), c) := v;`
// (IEEE 1076-2008 10.5.2.1, 10.6.2.1).  Its leaf names go to `leaves`.
Expression VhdlParser::parse_vhdl_aggregate_target(
    std::vector<Expression>& leaves) {
  Expression result;
  const auto open = advance();
  result.kind = ExpressionKind::Aggregate;
  result.span = open.span;
  const auto named_association_ahead = [&]() {
    std::size_t lookahead { };
    std::size_t depth { };
    for (;;) {
      if (at(TokenKind::EndOfFile, lookahead)) {
        return false;
      }
      if (at(TokenKind::LeftParen, lookahead)) {
        ++depth;
      } else if (at(TokenKind::RightParen, lookahead)) {
        if (depth == 0U) {
          return false;
        }
        --depth;
      } else if (depth == 0U
          && at(TokenKind::Arrow, lookahead)) {
        return true;
      } else if (depth == 0U
          && (at(TokenKind::Comma, lookahead)
              || at(TokenKind::Pipe, lookahead))) {
        return false;
      }
      ++lookahead;
    }
  };
  bool saw_named_association { };
  if (!at(TokenKind::RightParen)) {
    do {
      std::vector<Expression> choices;
      std::string choice_spelling;
      if (named_association_ahead()) {
        saw_named_association = true;
        do {
          auto choice = parse_expression();
          if (match_keyword("to", true)
              || match_keyword("downto", true)) {
            const auto direction = previous();
            auto right = parse_expression();
            choice = Expression { ExpressionKind::Binary,
                detail::ascii_lower(direction.text),
                { std::move(choice), std::move(right) },
                span_from(direction, previous()) };
          }
          choices.push_back(std::move(choice));
        } while (match(TokenKind::Pipe));
        expect(TokenKind::Arrow,
            "'=>' after aggregate target choice",
            "FSIM-VHDL-PARSE-029");
        if (choices.size() == 1U
            && choices.front().kind == ExpressionKind::Identifier) {
          choice_spelling = vhdl_name(choices.front().text);
        } else {
          choice_spelling = "@array";
        }
      } else if (saw_named_association) {
        error(current(), "FSIM-VHDL-SEM-038",
            "a positional aggregate target association cannot follow "
            "a named association");
      }
      // A nested aggregate target, `((a, b), c) := v;`.
      const auto nested_after = at(TokenKind::LeftParen)
          ? skip_vhdl_parentheses(0U)
          : std::nullopt;
      auto element_target = nested_after
          ? parse_vhdl_aggregate_target(leaves)
          : parse_lvalue();
      if (!nested_after) {
        leaves.push_back(element_target);
      }
      result.aggregate_choices.push_back(
          std::move(choice_spelling));
      result.aggregate_choice_expressions.push_back(
          std::move(choices));
      result.operands.push_back(std::move(element_target));
    } while (match(TokenKind::Comma));
  }
  expect(TokenKind::RightParen, "')' after aggregate target",
      "FSIM-VHDL-PARSE-029");
  result.span = span_from(open, previous());
  return result;
}

std::optional<Statement> VhdlParser::parse_assignment(bool concurrent) {
  struct ConcurrentScope {
    bool& slot;
    bool saved;
    ~ConcurrentScope() { slot = saved; }
  } concurrent_scope { parsing_concurrent_assignment_,
                       parsing_concurrent_assignment_ };
  parsing_concurrent_assignment_ = concurrent;
  const auto start_position = position();
  const auto assignment_start = current();
  // An aggregate assignment target, `(a, b) := v;` or `(a, b) <= v;`
  // (IEEE 1076-2008 10.5.2.1, 10.6.2.1, 11.6).
  std::vector<Expression> aggregate_targets;
  Expression aggregate_target;
  if (at(TokenKind::LeftParen)) {
    const auto after_target = skip_vhdl_parentheses(0U);
    if (after_target && !at(TokenKind::RightParen, 1U)
        && (at(TokenKind::LessEqual, *after_target)
            || at(TokenKind::ColonEqual, *after_target))) {
      aggregate_target = parse_vhdl_aggregate_target(aggregate_targets);
    }
  }
  // An external name denotes the object it names, so it can be a target
  // (IEEE 1076-2008 8.7, 10.5.2.1).
  // An alias of an external name denotes that object too (6.6.2).
  const bool external_target = aggregate_targets.empty()
      && (at(TokenKind::ShiftLeft)
          || (at(TokenKind::Identifier)
              && vhdl_external_aliases_.contains(vhdl_name(current().text))
              && !at(TokenKind::Dot, 1U) && !at(TokenKind::Apostrophe, 1U)
              && !at(TokenKind::LeftParen, 1U)));
  if (aggregate_targets.empty() && !at(TokenKind::Identifier)
      && !external_target) {
    return std::nullopt;
  }
  const auto start = assignment_start;
  Expression target = external_target ? parse_primary()
      : aggregate_targets.empty()
      ? parse_lvalue()
      : std::move(aggregate_target);
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
  if (kind != AssignmentKind::Blocking
      && (keyword("force", 0, true)
          || keyword("release", 0, true))) {
    const auto operation = advance();
    (void)require_vhdl_standard(
        operation,
        VhdlStandard::Vhdl2008,
        "force and release assignments",
        "drive the signal through an explicit testbench source");
    const bool force = vhdl_name(operation.text) == "force";
    statement.kind = force ? StatementKind::Force
                           : StatementKind::Release;
    if (concurrent) {
      error(operation, "FSIM-VHDL-SEM-102",
            "VHDL force and release assignments are sequential statements");
    }
    if (match_keyword("in", true)) {
      // The default and explicit in modes both force the effective value.
    } else if (match_keyword("out", true)) {
      statement.vhdl_force_driving_value = true;
    }
    if (force) {
      if (at(TokenKind::Semicolon)) {
        error(current(), "FSIM-VHDL-PARSE-280",
              "a VHDL force assignment requires a forcing expression");
      } else {
        statement.value = parse_expression(0, false);
      }
      // A conditional force assignment (`t <= force a when c else b;`,
      // IEEE 1076-2008 10.5.3) forces the first value whose condition
      // holds; it is the equivalent if statement of forces.
      if (keyword("when", 0, true)) {
        std::vector<std::pair<Expression, std::optional<Expression>>> arms;
        auto value = std::move(statement.value);
        for (;;) {
          if (!match_keyword("when", true)) {
            arms.emplace_back(std::move(value), std::nullopt);
            break;
          }
          auto condition = parse_expression();
          arms.emplace_back(std::move(value), std::move(condition));
          if (!match_keyword("else", true)) {
            break;
          }
          value = parse_expression(0, false);
        }
        expect(TokenKind::Semicolon,
               "';' after VHDL force or release assignment",
               "FSIM-VHDL-PARSE-282");
        const auto span = span_from(start, previous());
        std::optional<Statement> tail;
        for (auto arm = arms.rbegin(); arm != arms.rend(); ++arm) {
          auto forced = statement;
          forced.value = std::move(arm->first);
          forced.span = span;
          if (!arm->second) {
            tail = std::move(forced);
            continue;
          }
          Statement conditional;
          conditional.kind = StatementKind::If;
          conditional.condition = std::move(*arm->second);
          conditional.statements.push_back(std::move(forced));
          if (tail) {
            conditional.else_statements.push_back(std::move(*tail));
          }
          conditional.span = span;
          tail = std::move(conditional);
        }
        return std::move(*tail);
      }
    } else if (!at(TokenKind::Semicolon)) {
      error(current(), "FSIM-VHDL-PARSE-281",
            "a VHDL release assignment has no expression");
      (void)parse_expression();
    }
    expect(TokenKind::Semicolon, "';' after VHDL force or release assignment",
           "FSIM-VHDL-PARSE-282");
    statement.span = span_from(start, previous());
    return statement;
  }
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
    // `unaffected` as an alternative of a conditional variable assignment
    // leaves the variable's value as it is (IEEE 1076-2019 10.6.3): it
    // reads back the target.
    {
      std::vector<Expression*> pending{&statement.value};
      while (!pending.empty()) {
        auto* expression = pending.back();
        pending.pop_back();
        if (expression->kind != ExpressionKind::Call
            || expression->text != "?:") {
          continue;
        }
        for (std::size_t index = 1U; index < expression->operands.size();
             ++index) {
          auto& alternative = expression->operands[index];
          if (alternative.kind == ExpressionKind::Identifier
              && vhdl_name(alternative.text) == "unaffected") {
            const auto span = alternative.span;
            alternative = statement.target;
            alternative.span = span;
          } else {
            pending.push_back(&alternative);
          }
        }
      }
    }
    diagnose_misplaced_vhdl_delay_mechanism();
    if (match_keyword("after", true)) {
      statement.delay = parse_vhdl_delay(previous());
    }
  }
  expect(TokenKind::Semicolon, "';' after assignment",
         "FSIM-VHDL-PARSE-029");
  statement.span = span_from(start, previous());
  // A guarded target is assigned only by a guarded concurrent assignment,
  // and an aggregate target names only guarded or only unguarded signals
  // (IEEE 1076-2008 11.6).
  if (concurrent && kind == AssignmentKind::Continuous) {
    const auto guarded = [&](const Expression& name) {
      return name.kind == ExpressionKind::Identifier
          && vhdl_guarded_signal_names_.contains(
              detail::ascii_lower(name.text));
    };
    const auto& names = aggregate_targets.empty()
        ? std::vector<Expression>{statement.target}
        : aggregate_targets;
    const auto guarded_count = static_cast<std::size_t>(
        std::ranges::count_if(names, guarded));
    const bool all_names = std::ranges::all_of(names,
        [](const Expression& name) {
          return name.kind == ExpressionKind::Identifier;
        });
    if (all_names && guarded_count != 0U && guarded_count != names.size()) {
      error(start, "FSIM-VHDL-SEM-129",
          "an aggregate signal assignment target names both guarded and "
          "unguarded signals");
      return statement;
    }
    if (guarded_count != 0U && !statement.vhdl_guarded_assignment) {
      error(start, "FSIM-VHDL-SEM-129",
          "a guarded signal is the target of an unguarded concurrent "
          "signal assignment");
      return statement;
    }
  }
  if (!aggregate_targets.empty()) {
    // An aggregate target may not name the same object more than once
    // (IEEE 1076-2008 10.5.2.1, 10.6.2.1).
    const auto same_target = [&](const auto& self,
                                 const Expression& left,
                                 const Expression& right) -> bool {
      return left.kind == right.kind && left.text == right.text
          && left.operands.size() == right.operands.size()
          && std::ranges::equal(left.operands, right.operands,
              [&](const Expression& left_operand,
                  const Expression& right_operand) {
                return self(self, left_operand, right_operand);
              });
    };
    for (std::size_t first { }; first < aggregate_targets.size(); ++first) {
      for (auto second = first + 1U; second < aggregate_targets.size();
           ++second) {
        const auto& left = aggregate_targets[first];
        const auto& right = aggregate_targets[second];
        if (same_target(same_target, left, right)) {
          error(start, "FSIM-VHDL-SEM-120",
              "an aggregate assignment target names the same object more "
              "than once");
          return statement;
        }
      }
    }
    // Keep the target aggregate intact. Expanding this into one assignment
    // per association would evaluate a function-call RHS once per element
    // and lose record source ordering; the lowerer fans out one packed RHS.
  }
  return statement;
}

std::optional<Statement> VhdlParser::split_vhdl_aggregate_assignment(
    const Statement& statement,
    const std::vector<Expression>& targets) const {
  const auto count = targets.size();
  // Element `k` of the value, counted from the left (IEEE 1076-2008
  // 9.3.3.2: positional elements match the index range from the left).
  const auto element = [&](const Expression& value, const std::size_t k)
      -> std::optional<Expression> {
    // The value's type must be known without the target's (IEEE 1076-2008
    // 10.5.2.1): an aggregate or string value is qualified.
    const Expression* source = &value;
    if (source->kind == ExpressionKind::Aggregate
        || source->kind == ExpressionKind::StringLiteral) {
      return std::nullopt;
    }
    if (source->kind == ExpressionKind::Call
        && source->text.starts_with("@vhdl-qualified:")
        && source->operands.size() == 1U) {
      source = &source->operands.front();
    }
    if (source->kind == ExpressionKind::Aggregate
        && source->operands.size() == count
        && std::ranges::all_of(source->aggregate_choices,
            [](const std::string& choice) { return choice.empty(); })) {
      return source->operands[k];
    }
    if (source->kind == ExpressionKind::StringLiteral
        && source->text.size() == count + 2U
        && source->text.front() == '"') {
      return Expression { ExpressionKind::LogicLiteral,
          std::string { '\'', source->text[k + 1U], '\'' }, { },
          source->span };
    }
    if (value.kind == ExpressionKind::Identifier) {
      // v(v'left + k * ((v'right - v'left) / abs(v'right - v'left))).
      const auto& span = value.span;
      const auto attribute = [&](const std::string_view name) {
        return Expression { ExpressionKind::Call, std::string { name },
            { value }, span };
      };
      auto distance = Expression { ExpressionKind::Binary, "-",
          { attribute("'right"), attribute("'left") }, span };
      auto magnitude = Expression { ExpressionKind::Unary, "abs",
          { distance }, span };
      auto step = Expression { ExpressionKind::Binary, "/",
          { std::move(distance), std::move(magnitude) }, span };
      auto offset = Expression { ExpressionKind::Binary, "*",
          { Expression { ExpressionKind::IntegerLiteral,
                std::to_string(k), { }, span },
            std::move(step) },
          span };
      auto index = Expression { ExpressionKind::Binary, "+",
          { attribute("'left"), std::move(offset) }, span };
      return Expression { ExpressionKind::Call, value.text,
          { std::move(index) }, span };
    }
    return std::nullopt;
  };
  if (statement.kind != StatementKind::Assignment
      || !statement.value.valid()) {
    return std::nullopt;
  }
  Statement block;
  block.kind = StatementKind::Block;
  block.span = statement.span;
  for (std::size_t k = 0U; k < count; ++k) {
    Statement part = statement;
    part.target = targets[k];
    auto value = element(statement.value, k);
    if (!value) {
      return std::nullopt;
    }
    part.value = std::move(*value);
    for (auto& wave : part.vhdl_waveform) {
      if (!wave.value.valid()) {
        continue;
      }
      auto wave_value = element(wave.value, k);
      if (!wave_value) {
        return std::nullopt;
      }
      wave.value = std::move(*wave_value);
    }
    block.statements.push_back(std::move(part));
  }
  return block;
}

Statement VhdlParser::parse_vhdl_selected_assignment(
    const Token& start,
    const bool concurrent) {
  Statement statement;
  statement.kind = StatementKind::Case;
  statement.condition = parse_expression();
  expect_keyword("select", true, "FSIM-VHDL-PARSE-116");
  if (match(TokenKind::Question)) {
      (void)require_vhdl_standard(
          previous(),
          VhdlStandard::Vhdl2008,
          "matching selected assignments",
          "use an ordinary selected assignment with exact choices");
      statement.case_match_kind = CaseMatchKind::VhdlMatching;
  }
  std::vector<Expression> aggregate_leaves;
  const auto target = at(TokenKind::LeftParen)
      ? parse_vhdl_aggregate_target(aggregate_leaves)
      : parse_lvalue();
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
      assignment.value = parse_expression(0, false);
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
  // Selected assignments have the coverage rules of their equivalent case
  // statement (IEEE 1076-2008 10.5.4). Coverage is checked after resolving the
  // selector subtype; exhaustive explicit choices do not require others.
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

  if (assignment.assignment_kind != AssignmentKind::Continuous) {
      (void)require_vhdl_standard(
          previous(),
          VhdlStandard::Vhdl2008,
          "sequential conditional assignments",
          "replace the assignment with a sequential if statement");
  }

  Statement conditional;
  conditional.kind = StatementKind::If;
  conditional.vhdl_conditional_assignment = true;
  conditional.vhdl_guarded_assignment =
      assignment.vhdl_guarded_assignment;
  conditional.condition = parse_expression();
  conditional.statements.push_back(std::move(assignment));
  if (!match_keyword("else", true)) {
    // The final `else` is optional (IEEE 1076-2008 11.6, 10.5.3); when no
    // condition holds, the target is not assigned.
    conditional.span = cover(assignment_start, previous().span);
    return conditional;
  }

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
      if (statement.assignment_kind != AssignmentKind::Continuous) {
          (void)require_vhdl_standard(
              previous(),
              VhdlStandard::Vhdl2008,
              "unaffected in sequential signal assignments",
              "omit the assignment on the unaffected control path");
      }
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
    // A null transaction must target a guarded signal (IEEE 1076-2008
    // 10.5.2.2): a sequential one is checked on lowering; a concurrent one
    // must be a guarded assignment.
    if (match_keyword("null", true)) {
      element.disconnect = true;
      if (!statement.vhdl_guarded_assignment
          && parsing_concurrent_assignment_) {
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
      element.value = parse_expression(0, false);
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
    auto when_true = parse_expression(0, false);
    if (!match_keyword("when", true)) {
        return when_true;
    }
    (void)require_vhdl_standard(
        previous(),
        VhdlStandard::Vhdl2008,
        "sequential conditional assignments",
        "replace the assignment with a sequential if statement");
    const auto begin_span = when_true.span;
    auto condition = parse_expression();
    expect_keyword("else", true, "FSIM-VHDL-PARSE-115");
    auto when_false = parse_conditional_assignment_value();
    const auto span = cover(begin_span, when_false.span);
    return Expression {
        ExpressionKind::Call,
        "?:",
        { std::move(condition),
            std::move(when_true),
            std::move(when_false) },
        span
    };
}

Delay VhdlParser::parse_vhdl_delay(const Token& start) {
  Delay delay;
  auto expression = parse_expression(0, false);
  delay.span = cover(start.span, expression.span);
  constexpr std::string_view physical_prefix{"@vhdl-physical:"};
  if (expression.kind == ExpressionKind::Call
      && expression.text.starts_with(physical_prefix)
      && expression.operands.size() == 1
      && expression.operands.front().kind
          == ExpressionKind::IntegerLiteral) {
    if (const auto magnitude =
            decimal_u64(expression.operands.front().text)) {
      delay.magnitude = *magnitude;
      delay.unit = expression.text.substr(physical_prefix.size());
      return delay;
    }
  }
  delay.expression = std::move(expression);
  return delay;
}

Expression VhdlParser::parse_lvalue() {
  const auto name = expect_identifier("assignment target");
  Expression expression{ExpressionKind::Identifier, vhdl_name(name.text),
                        {}, name.span};
  // An indexed or selected name rooted at an access object implicitly
  // dereferences it (IEEE 1076-2008 8.3), matching parse_primary(). Keep an
  // explicit `.all` suffix on the existing path below.
  if (vhdl_access_object_names_.contains(expression.text)
      && (at(TokenKind::LeftParen)
          || (at(TokenKind::Dot) && at(TokenKind::Identifier, 1)
              && vhdl_name(current(1).text) != "all"))) {
    expression = Expression{ExpressionKind::Call,
                            "@vhdl-dereference",
                            {std::move(expression)},
                            name.span};
  }
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
      std::vector<Expression> indices;
      do {
        Expression first = parse_expression();
        if (match_keyword("downto", true) || match_keyword("to", true)) {
          saw_slice = true;
          // A legal slice has one range, rather than a list of selectors.
          // Preserve the parser's existing recovery for mixed forms by
          // flushing any preceding selectors before building the slice.
          for (auto& index : indices) {
            const auto index_span = cover(expression.span, index.span);
            expression = Expression{
                ExpressionKind::Index, "index",
                {std::move(expression), std::move(index)}, index_span};
          }
          indices.clear();
          const auto direction = previous();
          Expression second = parse_expression();
          const auto slice_span = cover(expression.span, second.span);
          expression = Expression{
              ExpressionKind::Slice, detail::ascii_lower(direction.text),
              {std::move(expression), std::move(first), std::move(second)},
              slice_span};
        } else if (saw_slice) {
          const auto index_span = cover(expression.span, first.span);
          expression = Expression{
              ExpressionKind::Index, "index",
              {std::move(expression), std::move(first)}, index_span};
        } else {
          indices.push_back(std::move(first));
        }
      } while (match(TokenKind::Comma));
      expect(TokenKind::RightParen, "')' after indices",
             saw_slice ? "FSIM-VHDL-PARSE-031"
                       : "FSIM-VHDL-PARSE-032");
      if (!indices.empty()) {
        const auto group_span = cover(expression.span, previous().span);
        std::vector<Expression> operands;
        operands.reserve(indices.size() + 1U);
        operands.push_back(std::move(expression));
        for (auto& index : indices) {
          operands.push_back(std::move(index));
        }
        expression = Expression{
            ExpressionKind::Index, "index", std::move(operands), group_span};
      }
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
