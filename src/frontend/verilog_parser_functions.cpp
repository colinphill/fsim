// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace fsim::frontend {

FunctionDeclaration VerilogParser::parse_function(const Token& start) {
  FunctionDeclaration function;
  if (match_keyword("automatic")) {
    function.automatic = true;
    function.lifetime_explicit = true;
  } else if (match_keyword("static")) {
    function.lifetime_explicit = true;
  } else {
    function.lifetime_explicit = false;
  }

  function.return_type = parse_parameter_type();
  const auto name = expect_identifier("function name");
  function.name = name.text;
  (void)parse_optional_container_dimension(
      function.return_type);

  auto saved_names = std::move(current_procedural_names_);
  auto saved_arguments = std::move(current_function_arguments_);
  auto saved_function_name = std::move(current_function_name_);
  const bool saved_in_function = in_function_;
  current_procedural_names_.clear();
  current_function_arguments_.clear();
  current_function_name_ = function.name;
  in_function_ = true;
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
          || keyword("time") || keyword("integer")
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

  Statement body;
  body.kind = StatementKind::Block;
  while (!at_end() && !keyword("endfunction")) {
    const auto before = position();
    if (is_direction_keyword() || keyword("ref")) {
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
    const auto end_name =
        expect_identifier("function name after endfunction");
    if (end_name.text != function.name) {
      error(
          end_name,
          "FSIM-SV-SEM-059",
          "function end name does not match '"
              + function.name + "'");
    }
  }

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
  validate_function_body(function, start);

  current_procedural_names_ = std::move(saved_names);
  current_function_arguments_ = std::move(saved_arguments);
  current_function_name_ = std::move(saved_function_name);
  in_function_ = saved_in_function;
  return function;
}

void VerilogParser::validate_function_body(
    const FunctionDeclaration& function,
    const Token& start) {
  std::unordered_set<std::string> locals;
  locals.insert(function.name);
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
    if (argument.direction != PortDirection::Input
        && (argument.type.domain == ValueDomain::String
            || argument.type.systemverilog_container)) {
      error(
          start,
          "FSIM-SV-UNSUPPORTED-035",
          "function output, inout, and ref formals currently require a "
          "packed integral type");
    }
  }

  bool unsupported_static_block_local = false;
  const auto collect_declarations =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          unsupported_static_block_local =
              unsupported_static_block_local
              || (!function.automatic
                  && !statement.declarations.empty());
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
  if (unsupported_static_block_local) {
    error(
        start,
        "FSIM-SV-SEM-099",
        "static or implicit-lifetime functions require declarations in "
        "the function body scope");
  }

  bool assigns_result = false;
  const auto inspect =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          if (statement.kind == StatementKind::Return) {
            assigns_result = assigns_result
                || statement.value.valid();
          }
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
            } else if (
                std::ranges::any_of(
                    function.arguments,
                    [&](const FunctionArgument& argument) {
                      return argument.name == root->text
                          && argument.direction
                              == PortDirection::Input
                          && !argument.reference;
                    })) {
              error(
                  start,
                  "FSIM-SV-SEM-062",
                  "a function cannot assign an input argument");
            }
            assigns_result =
                assigns_result || root->text == function.name;
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
          const bool forbidden =
              statement.kind == StatementKind::Delay
              || statement.kind == StatementKind::WaitOn
              || statement.kind == StatementKind::WaitUntil
              || statement.kind == StatementKind::EventTrigger
              || statement.kind == StatementKind::TaskCall
              || statement.kind == StatementKind::Display
              || statement.kind == StatementKind::MonitorControl
              || statement.kind == StatementKind::Report
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
  if (!assigns_result) {
    error(
        start,
        "FSIM-SV-SEM-065",
        "function '" + function.name
            + "' has no result assignment or return statement");
  }
}

TaskDeclaration VerilogParser::parse_task(const Token& start) {
  TaskDeclaration task;
  if (match_keyword("automatic")) {
    task.automatic = true;
    task.lifetime_explicit = true;
  } else if (match_keyword("static")) {
    task.lifetime_explicit = true;
  } else {
    task.lifetime_explicit = false;
  }

  const auto name = expect_identifier("task name");
  task.name = name.text;

  auto saved_names = std::move(current_procedural_names_);
  const bool saved_in_task = in_task_;
  current_procedural_names_.clear();
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
          keyword("longint") || keyword("time") || keyword("integer") ||
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

  Statement body;
  body.kind = StatementKind::Block;
  while (!at_end() && !keyword("endtask")) {
    const auto before = position();
    if (is_direction_keyword() || keyword("ref")) {
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
    if (end_name.text != task.name) {
      error(end_name, "FSIM-SV-SEM-068",
            "task end name does not match '" + task.name + "'");
    }
  }

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

  const auto reject_nested_static_declarations =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
        for (const auto& statement : statements) {
          if (!task.automatic && !statement.declarations.empty()) {
            error(
                start,
                "FSIM-SV-SEM-099",
                "static or implicit-lifetime tasks require declarations "
                "in the task body scope");
          }
          self(self, statement.statements);
          self(self, statement.else_statements);
          for (const auto& alternative :
               statement.case_alternatives) {
            self(self, alternative.statements);
          }
        }
      };
  reject_nested_static_declarations(
      reject_nested_static_declarations, task.statements);

  const auto inspect =
      [&](const auto& self,
          const std::vector<Statement>& statements) -> void {
    for (const auto& statement : statements) {
      const bool forbidden =
          statement.kind == StatementKind::Pause ||
          statement.kind == StatementKind::Finish ||
          (statement.kind == StatementKind::Assignment &&
           (statement.assignment_kind != AssignmentKind::Blocking ||
            statement.procedural_assignment_control !=
                ProceduralAssignmentControl::None));
      if (forbidden) {
        error(start, "FSIM-SV-SEM-070",
              "bounded tasks reject nonblocking or intra-assignment "
              "controls, $stop, and $finish");
      }
      self(self, statement.statements);
      self(self, statement.else_statements);
      for (const auto& alternative :
           statement.case_alternatives) {
        self(self, alternative.statements);
      }
    }
  };
  inspect(inspect, task.statements);
}

}  // namespace fsim::frontend
