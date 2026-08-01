// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {

namespace {

void qualify_expression(
    frontend::Expression& expression,
    const std::string_view port,
    const std::unordered_set<std::string>& signals,
    const std::unordered_set<std::string>& callables,
    const std::unordered_set<std::string>& locals) {
  const auto qualify = [&](std::string& name, const auto& visible) {
    const auto separator = name.find('.');
    const auto base = name.substr(0, separator);
    if (!locals.contains(base) && visible.contains(base)) {
      name = std::string{port} + "." + name;
    }
  };
  if (expression.kind == frontend::ExpressionKind::Identifier) {
    qualify(expression.text, signals);
  } else if (expression.kind == frontend::ExpressionKind::Call) {
    qualify(expression.text, callables);
  }
  for (auto& operand : expression.operands) {
    qualify_expression(operand, port, signals, callables, locals);
  }
  for (auto& association : expression.aggregate_choice_expressions) {
    for (auto& choice : association) {
      qualify_expression(choice, port, signals, callables, locals);
    }
  }
}

void qualify_statements(
    std::vector<frontend::Statement>& statements,
    const std::string_view port,
    const std::unordered_set<std::string>& signals,
    const std::unordered_set<std::string>& callables,
    const std::unordered_set<std::string>& locals) {
  for (auto& statement : statements) {
    for (auto* expression : {
             &statement.target, &statement.value,
             &statement.condition, &statement.loop_initial,
             &statement.loop_limit, &statement.file_handle}) {
      qualify_expression(*expression, port, signals, callables, locals);
    }
    if (callables.contains(statement.task_name)
        && !locals.contains(statement.task_name)) {
      statement.task_name =
          std::string{port} + "." + statement.task_name;
    }
    for (auto& expression : statement.task_arguments) {
      qualify_expression(expression, port, signals, callables, locals);
    }
    for (auto& sensitivity : statement.sensitivities) {
      if (signals.contains(sensitivity.signal)
          && !locals.contains(sensitivity.signal)) {
        sensitivity.signal =
            std::string{port} + "." + sensitivity.signal;
      }
      qualify_expression(
          sensitivity.expression, port, signals, callables, locals);
    }
    for (auto& output : statement.output_values) {
      qualify_expression(output.value, port, signals, callables, locals);
    }
    for (auto& waveform : statement.vhdl_waveform) {
      qualify_expression(waveform.value, port, signals, callables, locals);
    }
    for (auto& declaration : statement.declarations) {
      if (declaration.initializer) {
        qualify_expression(
            *declaration.initializer,
            port, signals, callables, locals);
      }
    }
    qualify_statements(
        statement.statements, port, signals, callables, locals);
    qualify_statements(
        statement.else_statements, port, signals, callables, locals);
    for (auto& alternative : statement.case_alternatives) {
      for (auto& choice : alternative.choices) {
        qualify_expression(choice, port, signals, callables, locals);
      }
      qualify_statements(
          alternative.statements, port, signals, callables, locals);
    }
  }
}

template <typename Callable>
void qualify_callable(
    Callable& callable,
    const std::string_view port,
    const frontend::DesignUnit& interface_unit) {
  std::unordered_set<std::string> signals;
  for (const auto& signal : interface_unit.signals) {
    signals.insert(signal.name);
  }
  for (const auto& signal : interface_unit.ports) {
    signals.insert(signal.name);
  }
  std::unordered_set<std::string> callables;
  for (const auto& function : interface_unit.functions) {
    callables.insert(function.name);
  }
  for (const auto& task : interface_unit.tasks) {
    callables.insert(task.name);
  }
  std::unordered_set<std::string> locals;
  for (const auto& argument : callable.arguments) {
    locals.insert(argument.name);
  }
  for (const auto& variable : callable.variables) {
    locals.insert(variable.name);
  }
  for (auto& variable : callable.variables) {
    if (variable.initializer) {
      qualify_expression(
          *variable.initializer,
          port, signals, callables, locals);
    }
  }
  qualify_statements(
      callable.statements, port, signals, callables, locals);
  callable.name = std::string{port} + "." + callable.name;
}

}  // namespace

void HierarchyBuilder::qualify_interface_callable(
    frontend::FunctionDeclaration& callable,
    const std::string_view port,
    const frontend::DesignUnit& interface_unit) {
  qualify_callable(callable, port, interface_unit);
}

void HierarchyBuilder::qualify_interface_callable(
    frontend::TaskDeclaration& callable,
    const std::string_view port,
    const frontend::DesignUnit& interface_unit) {
  qualify_callable(callable, port, interface_unit);
}

}  // namespace fsim::elaboration
