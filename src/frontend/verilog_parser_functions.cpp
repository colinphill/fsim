// SPDX-License-Identifier: Apache-2.0
#include "verilog_parser_internal.hpp"

#include <algorithm>
#include <unordered_set>

namespace fsim::frontend {

FunctionDeclaration VerilogParser::parse_function(const Token& start) {
  FunctionDeclaration function;
  if (match_keyword("automatic")) {
    function.automatic = true;
  } else if (match_keyword("static")) {
    error(
        previous(),
        "FSIM-SV-UNSUPPORTED-033",
        "static function activation records are not implemented; declare "
        "the function automatic");
  } else {
    error(
        start,
        "FSIM-SV-UNSUPPORTED-033",
        "the current function subset requires an explicit automatic "
        "lifetime");
  }

  function.return_type = parse_parameter_type();
  const auto name = expect_identifier("function name");
  function.name = name.text;

  auto saved_names = std::move(current_procedural_names_);
  auto saved_arguments = std::move(current_function_arguments_);
  auto saved_function_name = std::move(current_function_name_);
  const bool saved_in_function = in_function_;
  current_procedural_names_.clear();
  current_function_arguments_.clear();
  current_function_name_ = function.name;
  in_function_ = true;
  current_procedural_names_.insert(function.name);

  if (match(TokenKind::LeftParen)) {
    Type inherited_type;
    bool have_inherited_type = false;
    while (!at_end() && !at(TokenKind::RightParen)) {
      PortDirection direction = PortDirection::Input;
      bool explicit_direction = false;
      if (is_direction_keyword()) {
        direction = parse_direction();
        explicit_direction = true;
      } else if (match_keyword("ref")) {
        explicit_direction = true;
        direction = PortDirection::Inout;
      }
      if (direction != PortDirection::Input) {
        error(
            previous(),
            "FSIM-SV-UNSUPPORTED-035",
            "function arguments currently support input direction only");
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
      if (match(TokenKind::Assign)) {
        error(
            previous(),
            "FSIM-SV-UNSUPPORTED-036",
            "default function arguments are not implemented");
        (void)parse_expression();
      }
      if (!match(TokenKind::Comma)) {
        break;
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
      const auto unsupported = advance();
      error(
          unsupported,
          "FSIM-SV-UNSUPPORTED-036",
          "classic function argument declarations are not implemented; "
          "use an ANSI argument list or a no-argument function");
      skip_to_semicolon();
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
            if (root->kind != ExpressionKind::Identifier
                || !locals.contains(root->text)) {
              error(
                  start,
                  "FSIM-SV-SEM-061",
                  "a function cannot assign a nonlocal object");
            } else if (
                std::ranges::any_of(
                    function.arguments,
                    [&](const FunctionArgument& argument) {
                      return argument.name == root->text;
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
  } else if (match_keyword("static")) {
    error(previous(), "FSIM-SV-UNSUPPORTED-037",
          "static task activation records are not implemented; declare "
          "the task automatic");
  } else {
    error(start, "FSIM-SV-UNSUPPORTED-037",
          "the current task subset requires an explicit automatic lifetime");
  }

  const auto name = expect_identifier("task name");
  task.name = name.text;

  auto saved_names = std::move(current_procedural_names_);
  const bool saved_in_task = in_task_;
  current_procedural_names_.clear();
  in_task_ = true;

  if (match(TokenKind::LeftParen)) {
    Type inherited_type;
    PortDirection inherited_direction{PortDirection::Input};
    bool have_inherited_formal = false;
    while (!at_end() && !at(TokenKind::RightParen)) {
      auto direction = inherited_direction;
      bool explicit_direction = false;
      if (is_direction_keyword()) {
        direction = parse_direction();
        inherited_direction = direction;
        explicit_direction = true;
      } else if (match_keyword("ref")) {
        direction = PortDirection::Inout;
        inherited_direction = direction;
        explicit_direction = true;
        error(previous(), "FSIM-SV-UNSUPPORTED-038",
              "ref task arguments are not implemented; use input, output, "
              "or inout");
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
      if (match(TokenKind::Assign)) {
        error(previous(), "FSIM-SV-UNSUPPORTED-040",
              "default task arguments are not implemented");
        (void)parse_expression();
      }
      if (!match(TokenKind::Comma)) {
        break;
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
      const auto unsupported = advance();
      error(unsupported, "FSIM-SV-UNSUPPORTED-040",
            "classic task argument declarations are not implemented; use "
            "an ANSI argument list or a no-argument task");
      skip_to_semicolon();
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
  }
  for (const auto& variable : task.variables) {
    if (!names.insert(variable.name).second) {
      error(start, "FSIM-SV-SEM-069",
            "duplicate or conflicting task local '" + variable.name + "'");
    }
  }

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
