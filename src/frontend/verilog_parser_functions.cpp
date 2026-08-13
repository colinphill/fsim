// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace fsim::frontend {

FunctionDeclaration VerilogParser::parse_function(
    const Token& start,
    const bool prototype,
    const bool default_automatic) {
  FunctionDeclaration function;
  function.language = language_;
  if (match_keyword("automatic")) {
    function.automatic = true;
    function.lifetime_explicit = true;
  } else if (match_keyword("static")) {
    function.lifetime_explicit = true;
  } else {
    function.automatic = default_automatic;
    function.lifetime_explicit = false;
  }

  // A user-defined return type is followed by the function name and then
  // '('. The general declaration lookahead deliberately treats that shape
  // as a possible instance, but inside a function header it is unambiguous.
  const bool constructor = keyword("new");
  bool qualified_constructor = false;
  std::size_t qualified_constructor_offset = 0;
  while (at(TokenKind::Identifier, qualified_constructor_offset)
         && at(TokenKind::Scope, qualified_constructor_offset + 1U)) {
    if (keyword("new", qualified_constructor_offset + 2U)) {
      qualified_constructor = true;
      break;
    }
    if (!at(TokenKind::Identifier, qualified_constructor_offset + 2U)) {
      break;
    }
    qualified_constructor_offset += 2U;
  }
  const bool void_result = keyword("void");
  const bool builtin_return_type =
      keyword("string") || keyword("byte")
      || keyword("shortint") || keyword("longint")
      || keyword("shortreal") || keyword("real")
      || keyword("realtime") || keyword("chandle")
      || keyword("process")
      || keyword("time") || keyword("integer") || keyword("int")
      || keyword("logic") || keyword("reg") || keyword("bit")
      || keyword("signed") || keyword("unsigned")
      || at(TokenKind::LeftBracket);
  if (constructor) {
    const auto name = advance();
    function.name = name.text;
    function.return_type.spelling = "constructor";
  } else if (qualified_constructor) {
    function.return_type.spelling = "constructor";
    function.name = expect_identifier("constructor owner").text;
    while (match(TokenKind::Scope)) {
      function.name += "::";
      if (keyword("new")) {
        function.name += advance().text;
        break;
      }
      function.name +=
          expect_identifier("selected constructor owner").text;
    }
  } else {
    if (void_result) {
      const auto result = advance();
      function.return_type.spelling = result.text;
      function.return_type.named_type_span = result.span;
    } else {
      function.return_type =
          !builtin_return_type && at(TokenKind::Identifier)
              && at(TokenKind::Identifier, 1)
              ? parse_named_type()
              : parse_parameter_type();
    }
    const auto name = expect_identifier("function name");
    function.name = name.text;
    while (match(TokenKind::Scope)) {
      function.name += "::";
      function.name += expect_identifier("selected function name").text;
    }
  }
  (void)parse_optional_container_dimension(
      function.return_type);

  auto saved_names = std::move(current_procedural_names_);
  auto saved_types = std::move(current_procedural_types_);
  auto saved_arguments = std::move(current_function_arguments_);
  auto saved_function_name = std::move(current_function_name_);
  const bool saved_in_function = in_function_;
  const bool saved_function_returns_void = current_function_returns_void_;
  current_procedural_names_.clear();
  current_procedural_types_.clear();
  current_function_arguments_.clear();
  current_function_name_ = function.name;
  in_function_ = true;
  current_function_returns_void_ = void_result;
  current_procedural_names_.insert(function.name);

  std::vector<std::string> classic_header_arguments;
  if (match(TokenKind::LeftParen)) {
    const bool classic_header =
        at(TokenKind::Identifier)
        && (at(TokenKind::Comma, 1)
            || at(TokenKind::RightParen, 1));
    if (classic_header) {
      std::unordered_set<std::string> names;
      do {
        const auto argument_name =
            expect_identifier("classic function argument name");
        if (!names.insert(argument_name.text).second
            || argument_name.text == function.name) {
          error(
              argument_name,
              "FSIM-SV-SEM-058",
              "duplicate or conflicting function argument '"
                  + argument_name.text + "'");
        }
        classic_header_arguments.push_back(argument_name.text);
      } while (match(TokenKind::Comma));
    } else {
    Type inherited_type;
    bool have_inherited_type = false;
    while (!at_end() && !at(TokenKind::RightParen)) {
      PortDirection direction = PortDirection::Input;
      bool explicit_direction = false;
      bool reference = false;
      (void)match_keyword("const");
      if (is_direction_keyword()) {
        direction = parse_direction();
        explicit_direction = true;
      } else if (match_keyword("ref")) {
        explicit_direction = true;
        direction = PortDirection::Inout;
        reference = true;
      }

      const bool explicit_type =
          keyword("string") || keyword("byte")
          || keyword("shortint") || keyword("longint")
          || keyword("time") || keyword("shortreal")
          || keyword("real") || keyword("realtime")
          || keyword("chandle") || keyword("process")
          || keyword("integer")
          || keyword("int") || keyword("logic")
          || keyword("reg") || keyword("bit")
          || keyword("signed") || keyword("unsigned")
          || at(TokenKind::LeftBracket)
          || is_named_type_reference_start();
      Type type;
      if (explicit_direction || explicit_type
          || !have_inherited_type) {
        type = parse_parameter_type();
        inherited_type = type;
        have_inherited_type = true;
      } else {
        type = inherited_type;
      }
      const auto argument_name =
          expect_identifier("function argument name");
      if (!current_function_arguments_.insert(
              argument_name.text).second
          || argument_name.text == function.name) {
        error(
            argument_name,
            "FSIM-SV-SEM-058",
            "duplicate or conflicting function argument '"
                + argument_name.text + "'");
      }
      current_procedural_names_.insert(argument_name.text);
      (void)parse_optional_container_dimension(type);
      current_procedural_types_.insert_or_assign(argument_name.text, type);
      function.arguments.push_back(FunctionArgument{
          argument_name.text,
          std::move(type),
          direction,
          argument_name.span});
      function.arguments.back().reference = reference;
      if (match(TokenKind::Assign)) {
        function.arguments.back().default_value = parse_expression();
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    }
    expect(
        TokenKind::RightParen,
        "')' after function arguments",
        "FSIM-SV-PARSE-140");
  }
  expect(
      TokenKind::Semicolon,
      "';' after function header",
      "FSIM-SV-PARSE-141");

  if (prototype) {
    function.defined = false;
    function.span = span_from(start, previous());
    current_procedural_names_ = std::move(saved_names);
    current_procedural_types_ = std::move(saved_types);
    current_function_arguments_ = std::move(saved_arguments);
    current_function_name_ = std::move(saved_function_name);
    in_function_ = saved_in_function;
    current_function_returns_void_ = saved_function_returns_void;
    return function;
  }

  Statement body;
  body.kind = StatementKind::Block;
  DesignUnit local_declarations;
  while (!at_end() && !keyword("endfunction")) {
    const auto before = position();
    if (match_keyword("typedef")) {
      const auto alias_count = local_declarations.type_aliases.size();
      parse_typedef(local_declarations, previous());
      if (local_declarations.type_aliases.size() > alias_count) {
        const auto& alias = local_declarations.type_aliases.back();
        current_procedural_types_.insert_or_assign(alias.name, alias.type);
      }
    } else if (is_direction_keyword() || keyword("ref")) {
      const auto declaration_start = advance();
      const bool reference = declaration_start.text == "ref";
      const auto direction = reference
          ? PortDirection::Inout
          : declaration_start.text == "output"
              ? PortDirection::Output
          : declaration_start.text == "inout"
              ? PortDirection::Inout
              : PortDirection::Input;
      Type type;
      if (at(TokenKind::Identifier)
          && (at(TokenKind::Comma, 1)
              || at(TokenKind::Semicolon, 1)
              || at(TokenKind::Assign, 1))) {
        type = Type{ValueDomain::Integer, "implicit", std::nullopt, true};
      } else {
        type = parse_parameter_type();
      }
      do {
        const auto argument_name =
            expect_identifier("classic function argument name");
        if (!current_function_arguments_.insert(
                argument_name.text).second
            || argument_name.text == function.name) {
          error(
              argument_name,
              "FSIM-SV-SEM-058",
              "duplicate or conflicting function argument '"
                  + argument_name.text + "'");
        }
        current_procedural_names_.insert(argument_name.text);
        auto argument_type = type;
        (void)parse_optional_container_dimension(argument_type);
        current_procedural_types_.insert_or_assign(
            argument_name.text, argument_type);
        function.arguments.push_back(FunctionArgument{
            argument_name.text,
            std::move(argument_type),
            direction,
            argument_name.span});
        function.arguments.back().reference = reference;
        if (match(TokenKind::Assign)) {
          function.arguments.back().default_value = parse_expression();
        }
      } while (match(TokenKind::Comma));
      expect(
          TokenKind::Semicolon,
          "';' after classic function argument declaration",
          "FSIM-SV-PARSE-197");
    } else if (is_declaration_start()) {
      parse_procedural_declaration(body);
    } else if (auto statement = parse_statement()) {
      body.statements.push_back(std::move(*statement));
    }
    if (position() == before) {
      advance();
    }
  }
  expect_keyword(
      "endfunction", false, "FSIM-SV-PARSE-142");
  if (match(TokenKind::Colon)) {
    const auto end_name = (constructor || qualified_constructor)
            && keyword("new")
        ? advance()
        : expect_identifier("function name after endfunction");
    const auto separator = function.name.rfind("::");
    const auto expected_name = separator == std::string::npos
        ? std::string_view{function.name}
        : std::string_view{function.name}.substr(separator + 2U);
    if (end_name.text != expected_name) {
      error(
          end_name,
          "FSIM-SV-SEM-059",
          "function end name does not match '"
              + function.name + "'");
    }
  }

  function.type_aliases = std::move(local_declarations.type_aliases);
  function.variables = std::move(body.declarations);
  function.statements = std::move(body.statements);
  if (!classic_header_arguments.empty()) {
    std::vector<FunctionArgument> ordered;
    ordered.reserve(classic_header_arguments.size());
    for (const auto& argument_name : classic_header_arguments) {
      const auto found = std::ranges::find(
          function.arguments,
          argument_name,
          &FunctionArgument::name);
      if (found == function.arguments.end()) {
        error(
            start,
            "FSIM-SV-SEM-058",
            "classic function argument '" + argument_name
                + "' has no body declaration");
      } else {
        ordered.push_back(std::move(*found));
      }
    }
    if (ordered.size() != function.arguments.size()) {
      error(
          start,
          "FSIM-SV-SEM-058",
          "classic function body declares an argument absent from its "
          "header list");
    }
    function.arguments = std::move(ordered);
  }
  function.span = span_from(start, previous());
  if (!constructor && !qualified_constructor
      && function.return_type.spelling != "void") {
    validate_function_body(function, start);
  }

  current_procedural_names_ = std::move(saved_names);
  current_procedural_types_ = std::move(saved_types);
  current_function_arguments_ = std::move(saved_arguments);
  current_function_name_ = std::move(saved_function_name);
  in_function_ = saved_in_function;
  current_function_returns_void_ = saved_function_returns_void;
  return function;
}

void VerilogParser::validate_function_body(
    const FunctionDeclaration& function,
    const Token& start) {
  const auto result_separator = function.name.rfind("::");
  const auto result_name = result_separator == std::string::npos
      ? function.name
      : function.name.substr(result_separator + 2U);
  std::unordered_set<std::string> locals;
  locals.insert(result_name);
  for (const auto& argument : function.arguments) {
    locals.insert(argument.name);
    if (argument.default_value
        && (argument.direction != PortDirection::Input
            || argument.reference)) {
      error(
          start,
          "FSIM-SV-SEM-095",
          "default function arguments require input value formals");
    }
    if (argument.reference && !function.automatic) {
      error(
          start,
          "FSIM-SV-SEM-096",
          "ref function arguments require automatic lifetime");
    }
  }

  const auto collect_declarations =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          for (const auto& declaration : statement.declarations) {
            locals.insert(declaration.name);
          }
          self(self, statement.statements);
          self(self, statement.else_statements);
          for (const auto& alternative :
               statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  for (const auto& variable : function.variables) {
    if (!locals.insert(variable.name).second) {
      error(
          start,
          "FSIM-SV-SEM-060",
          "duplicate or conflicting function local '"
              + variable.name + "'");
    }
  }
  collect_declarations(
      collect_declarations, function.statements);
  const auto inspect =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          if (statement.kind == StatementKind::Assignment) {
            const Expression* root = &statement.target;
            while ((root->kind == ExpressionKind::Index
                    || root->kind == ExpressionKind::Slice)
                   && !root->operands.empty()) {
              root = &root->operands.front();
            }
            if (root->kind != ExpressionKind::Identifier) {
              error(
                  start,
                  "FSIM-SV-SEM-061",
                  "a function assignment target must have an identifier "
                  "root");
            }
            if (statement.assignment_kind
                    != AssignmentKind::Blocking
                || statement.procedural_assignment_control
                    != ProceduralAssignmentControl::None) {
              error(
                  start,
                  "FSIM-SV-SEM-063",
                  "function assignments must be blocking and time-free");
            }
          }
          const bool forbidden = statement.kind == StatementKind::Delay
              || statement.kind == StatementKind::WaitOn
              || statement.kind == StatementKind::WaitUntil
              || statement.kind == StatementKind::WaitOrder
              || (statement.kind == StatementKind::EventTrigger
                  && (statement.delay
                      || statement.assignment_kind
                          == AssignmentKind::NonBlocking))
              || (statement.kind == StatementKind::Fork
                  && statement.fork_join_kind != ForkJoinKind::None)
              || statement.kind == StatementKind::WaitFork
              || statement.kind == StatementKind::DisableFork
              || statement.kind == StatementKind::Pause
              || statement.kind == StatementKind::Finish;
          if (forbidden) {
            error(
                start,
                "FSIM-SV-SEM-064",
                "functions cannot contain timing controls, event or task "
                "statements");
          }
          self(self, statement.statements);
          self(self, statement.else_statements);
          for (const auto& alternative :
               statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  inspect(inspect, function.statements);
}

TaskDeclaration VerilogParser::parse_task(
    const Token& start,
    const bool prototype,
    const bool default_automatic) {
  TaskDeclaration task;
  if (match_keyword("automatic")) {
    task.automatic = true;
    task.lifetime_explicit = true;
  } else if (match_keyword("static")) {
    task.lifetime_explicit = true;
  } else {
    task.automatic = default_automatic;
    task.lifetime_explicit = false;
  }

  const auto name = expect_identifier("task name");
  task.name = name.text;
  while (match(TokenKind::Scope)) {
    task.name += "::";
    task.name += expect_identifier("selected task name").text;
  }

  auto saved_names = std::move(current_procedural_names_);
  auto saved_types = std::move(current_procedural_types_);
  const bool saved_in_task = in_task_;
  current_procedural_names_.clear();
  current_procedural_types_.clear();
  in_task_ = true;

  std::vector<std::string> classic_header_arguments;
  if (match(TokenKind::LeftParen)) {
    const bool classic_header =
        at(TokenKind::Identifier)
        && (at(TokenKind::Comma, 1)
            || at(TokenKind::RightParen, 1));
    if (classic_header) {
      std::unordered_set<std::string> names;
      do {
        const auto argument_name =
            expect_identifier("classic task argument name");
        if (!names.insert(argument_name.text).second) {
          error(
              argument_name,
              "FSIM-SV-SEM-067",
              "duplicate task argument '" + argument_name.text + "'");
        }
        classic_header_arguments.push_back(argument_name.text);
      } while (match(TokenKind::Comma));
    } else {
    Type inherited_type;
    PortDirection inherited_direction{PortDirection::Input};
    bool have_inherited_formal = false;
    while (!at_end() && !at(TokenKind::RightParen)) {
      auto direction = inherited_direction;
      bool explicit_direction = false;
      bool reference = false;
      (void)match_keyword("const");
      if (is_direction_keyword()) {
        direction = parse_direction();
        inherited_direction = direction;
        explicit_direction = true;
      } else if (match_keyword("ref")) {
        direction = PortDirection::Inout;
        inherited_direction = direction;
        explicit_direction = true;
        reference = true;
      } else if (!have_inherited_formal) {
        inherited_direction = PortDirection::Input;
        direction = PortDirection::Input;
      }

      const bool explicit_type =
          keyword("string") || keyword("byte") || keyword("shortint") ||
          keyword("longint") || keyword("time") || keyword("shortreal") ||
          keyword("real") || keyword("realtime") || keyword("chandle") || keyword("process") || keyword("integer") ||
          keyword("int") || keyword("logic") || keyword("reg") ||
          keyword("bit") || keyword("signed") || keyword("unsigned") ||
          at(TokenKind::LeftBracket) || is_named_type_reference_start();
      Type type;
      if (explicit_direction || explicit_type || !have_inherited_formal) {
        type = parse_parameter_type();
        inherited_type = type;
      } else {
        type = inherited_type;
      }
      have_inherited_formal = true;
      const auto argument_name = expect_identifier("task argument name");
      if (!current_procedural_names_.insert(argument_name.text).second) {
        error(argument_name, "FSIM-SV-SEM-067",
              "duplicate task argument '" + argument_name.text + "'");
      }
      (void)parse_optional_container_dimension(type);
      current_procedural_types_.insert_or_assign(argument_name.text, type);
      task.arguments.push_back(TaskArgument{argument_name.text, std::move(type),
                                            direction, argument_name.span});
      task.arguments.back().reference = reference;
      if (match(TokenKind::Assign)) {
        task.arguments.back().default_value = parse_expression();
      }
      if (!match(TokenKind::Comma)) {
        break;
      }
    }
    }
    expect(TokenKind::RightParen, "')' after task arguments",
           "FSIM-SV-PARSE-143");
  }
  expect(TokenKind::Semicolon, "';' after task header", "FSIM-SV-PARSE-144");

  if (prototype) {
    task.span = span_from(start, previous());
    current_procedural_names_ = std::move(saved_names);
    current_procedural_types_ = std::move(saved_types);
    in_task_ = saved_in_task;
    return task;
  }

  Statement body;
  body.kind = StatementKind::Block;
  DesignUnit local_declarations;
  while (!at_end() && !keyword("endtask")) {
    const auto before = position();
    if (match_keyword("typedef")) {
      const auto alias_count = local_declarations.type_aliases.size();
      parse_typedef(local_declarations, previous());
      if (local_declarations.type_aliases.size() > alias_count) {
        const auto& alias = local_declarations.type_aliases.back();
        current_procedural_types_.insert_or_assign(alias.name, alias.type);
      }
    } else if (is_direction_keyword() || keyword("ref")) {
      const auto declaration_start = advance();
      const bool reference = declaration_start.text == "ref";
      const auto direction = reference
          ? PortDirection::Inout
          : declaration_start.text == "output"
              ? PortDirection::Output
          : declaration_start.text == "inout"
              ? PortDirection::Inout
              : PortDirection::Input;
      Type type;
      if (at(TokenKind::Identifier)
          && (at(TokenKind::Comma, 1)
              || at(TokenKind::Semicolon, 1)
              || at(TokenKind::Assign, 1))) {
        type = Type{ValueDomain::Integer, "implicit", std::nullopt, true};
      } else {
        type = parse_parameter_type();
      }
      do {
        const auto argument_name =
            expect_identifier("classic task argument name");
        if (!current_procedural_names_.insert(
                argument_name.text).second) {
          error(
              argument_name,
              "FSIM-SV-SEM-067",
              "duplicate task argument '" + argument_name.text + "'");
        }
        auto argument_type = type;
        (void)parse_optional_container_dimension(argument_type);
        current_procedural_types_.insert_or_assign(
            argument_name.text, argument_type);
        task.arguments.push_back(TaskArgument{
            argument_name.text,
            std::move(argument_type),
            direction,
            argument_name.span});
        task.arguments.back().reference = reference;
        if (match(TokenKind::Assign)) {
          task.arguments.back().default_value = parse_expression();
        }
      } while (match(TokenKind::Comma));
      expect(
          TokenKind::Semicolon,
          "';' after classic task argument declaration",
          "FSIM-SV-PARSE-198");
    } else if (is_declaration_start()) {
      parse_procedural_declaration(body);
    } else if (auto statement = parse_statement()) {
      body.statements.push_back(std::move(*statement));
    }
    if (position() == before) {
      advance();
    }
  }
  expect_keyword("endtask", false, "FSIM-SV-PARSE-145");
  if (match(TokenKind::Colon)) {
    const auto end_name = expect_identifier("task name after endtask");
    const auto separator = task.name.rfind("::");
    const auto expected_name = separator == std::string::npos
        ? std::string_view{task.name}
        : std::string_view{task.name}.substr(separator + 2U);
    if (end_name.text != expected_name) {
      error(end_name, "FSIM-SV-SEM-068",
            "task end name does not match '" + task.name + "'");
    }
  }

  task.type_aliases = std::move(local_declarations.type_aliases);
  task.variables = std::move(body.declarations);
  task.statements = std::move(body.statements);
  if (!classic_header_arguments.empty()) {
    std::vector<TaskArgument> ordered;
    ordered.reserve(classic_header_arguments.size());
    for (const auto& argument_name : classic_header_arguments) {
      const auto found = std::ranges::find(
          task.arguments,
          argument_name,
          &TaskArgument::name);
      if (found == task.arguments.end()) {
        error(
            start,
            "FSIM-SV-SEM-067",
            "classic task argument '" + argument_name
                + "' has no body declaration");
      } else {
        ordered.push_back(std::move(*found));
      }
    }
    if (ordered.size() != task.arguments.size()) {
      error(
          start,
          "FSIM-SV-SEM-067",
          "classic task body declares an argument absent from its header "
          "list");
    }
    task.arguments = std::move(ordered);
  }
  task.span = span_from(start, previous());
  validate_task_body(task, start);

  current_procedural_names_ = std::move(saved_names);
  current_procedural_types_ = std::move(saved_types);
  in_task_ = saved_in_task;
  return task;
}

void VerilogParser::validate_task_body(
    const TaskDeclaration& task,
    const Token& start) {
  std::unordered_set<std::string> names;
  for (const auto& argument : task.arguments) {
    names.insert(argument.name);
    if (argument.default_value
        && (argument.direction != PortDirection::Input
            || argument.reference)) {
      error(
          start,
          "FSIM-SV-SEM-097",
          "default task arguments require input value formals");
    }
    if (argument.reference && !task.automatic) {
      error(
          start,
          "FSIM-SV-SEM-098",
          "ref task arguments require automatic lifetime");
    }
  }
  for (const auto& variable : task.variables) {
    if (!names.insert(variable.name).second) {
      error(start, "FSIM-SV-SEM-069",
            "duplicate or conflicting task local '" + variable.name + "'");
    }
  }
}

}  // namespace fsim::frontend
