// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <bit>

namespace fsim::elaboration::elaboration_detail {
namespace {

std::optional<std::uint64_t> physical_unit_femtoseconds(
    const std::string_view unit) {
  if (unit == "fs") return 1;
  if (unit == "ps") return 1'000;
  if (unit == "ns") return 1'000'000;
  if (unit == "us") return 1'000'000'000;
  if (unit == "ms") return 1'000'000'000'000;
  if (unit == "s") return 1'000'000'000'000'000;
  return std::nullopt;
}

std::uint64_t time_scale(const std::string_view spelling) {
  if (spelling.empty()) return 1;
  std::uint64_t magnitude{};
  const auto parsed = std::from_chars(
      spelling.data(), spelling.data() + spelling.size(), magnitude);
  if (parsed.ec != std::errc{} || parsed.ptr == spelling.data()) return 1;
  const auto factor = physical_unit_femtoseconds(
      std::string_view{parsed.ptr,
                       static_cast<std::size_t>(
                           spelling.data() + spelling.size() - parsed.ptr)});
  if (!factor || magnitude > std::numeric_limits<std::uint64_t>::max() / *factor) {
    return 1;
  }
  return magnitude * *factor;
}

}  // namespace

frontend::SystemVerilogScalarEvaluationContext
systemverilog_scalar_evaluation_context(const DesignUnit& unit) {
  return {
      time_scale(unit.time_unit),
      time_scale(unit.time_precision)};
}

void substitute_systemverilog_scalars(
    Expression& expression,
    const SystemVerilogScalarConstantEnvironment& environment) {
  if (expression.kind == ExpressionKind::Identifier) {
    if (const auto found = environment.find(expression.text);
        found != environment.end()) {
      expression = found->second.expression(expression.span);
      return;
    }
  }
  for (auto& choices : expression.aggregate_choice_expressions) {
    for (auto& choice : choices) {
      substitute_systemverilog_scalars(choice, environment);
    }
  }
  for (auto& operand : expression.operands) {
    substitute_systemverilog_scalars(operand, environment);
  }
}

namespace {

void substitute_instance_actuals(
    std::vector<frontend::Instance>& instances,
    const SystemVerilogScalarConstantEnvironment& environment) {
  for (auto& instance : instances) {
    for (auto& override : instance.parameter_overrides) {
      substitute_systemverilog_scalars(override.value, environment);
    }
  }
}

void substitute_generate_parameter_sites(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogScalarConstantEnvironment& environment);

void substitute_generate_body_parameter_sites(
    frontend::GenerateBody& body,
    const SystemVerilogScalarConstantEnvironment& environment) {
  for (auto& constant : body.constants) {
    substitute_systemverilog_scalars(constant.default_value, environment);
  }
  substitute_instance_actuals(body.instances, environment);
  for (auto& declaration : body.verilog_defparams) {
      for (auto& segment : declaration.path) {
          for (auto& index : segment.indices) {
              substitute_systemverilog_scalars(index, environment);
          }
      }
      substitute_systemverilog_scalars(declaration.value, environment);
  }
  substitute_generate_parameter_sites(body.generate_regions, environment);
}

void substitute_generate_parameter_sites(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogScalarConstantEnvironment& environment) {
  for (auto& region : regions) {
    substitute_systemverilog_scalars(region.initial, environment);
    substitute_systemverilog_scalars(region.condition, environment);
    substitute_systemverilog_scalars(region.iteration, environment);
    substitute_generate_body_parameter_sites(region.then_body, environment);
    substitute_generate_body_parameter_sites(region.else_body, environment);
    for (auto& alternative : region.alternatives) {
      for (auto& choice : alternative.choices) {
        substitute_systemverilog_scalars(choice.left, environment);
        if (choice.right) {
          substitute_systemverilog_scalars(*choice.right, environment);
        }
      }
      substitute_generate_body_parameter_sites(alternative.body, environment);
    }
  }
}

void substitute_variable_parameter_sites(
    frontend::VariableDeclaration& variable,
    const SystemVerilogScalarConstantEnvironment& environment) {
  if (variable.initializer) {
    substitute_systemverilog_scalars(*variable.initializer, environment);
  }
}

void substitute_statement_parameter_sites(
    std::vector<Statement>& statements,
    const SystemVerilogScalarConstantEnvironment& environment) {
  for (auto& statement : statements) {
    substitute_systemverilog_scalars(statement.target, environment);
    substitute_systemverilog_scalars(statement.value, environment);
    substitute_systemverilog_scalars(statement.condition, environment);
    substitute_systemverilog_scalars(statement.loop_initial, environment);
    substitute_systemverilog_scalars(statement.loop_limit, environment);
    substitute_systemverilog_scalars(
        statement.loop_update_target, environment);
    for (auto& argument : statement.task_arguments) {
      substitute_systemverilog_scalars(argument, environment);
    }
    for (auto& argument : statement.procedure_arguments) {
      substitute_systemverilog_scalars(argument.value, environment);
    }
    for (auto& output : statement.output_values) {
      substitute_systemverilog_scalars(output.value, environment);
    }
    for (auto& element : statement.vhdl_waveform) {
      substitute_systemverilog_scalars(element.value, environment);
    }
    for (auto& variable : statement.declarations) {
      substitute_variable_parameter_sites(variable, environment);
    }
    for (auto& alternative : statement.case_alternatives) {
      for (auto& choice : alternative.choices) {
        substitute_systemverilog_scalars(choice, environment);
      }
      substitute_statement_parameter_sites(
          alternative.statements, environment);
    }
    substitute_statement_parameter_sites(statement.statements, environment);
    substitute_statement_parameter_sites(
        statement.else_statements, environment);
  }
}

template <typename Callable>
void substitute_callable_parameter_sites(
    Callable& callable,
    const SystemVerilogScalarConstantEnvironment& environment) {
  for (auto& argument : callable.arguments) {
    if (argument.default_value) {
      substitute_systemverilog_scalars(*argument.default_value, environment);
    }
  }
  for (auto& variable : callable.variables) {
    substitute_variable_parameter_sites(variable, environment);
  }
  substitute_statement_parameter_sites(callable.statements, environment);
}

}  // namespace

void substitute_systemverilog_scalar_parameter_sites(
    DesignUnit& unit,
    const SystemVerilogScalarConstantEnvironment& environment) {
  for (auto& parameter : unit.parameters) {
    substitute_systemverilog_scalars(parameter.default_value, environment);
  }
  for (auto& variable : unit.variables) {
    substitute_variable_parameter_sites(variable, environment);
  }
  substitute_statement_parameter_sites(
      unit.concurrent_statements, environment);
  for (auto& process : unit.processes) {
    for (auto& variable : process.variables) {
      substitute_variable_parameter_sites(variable, environment);
    }
    substitute_statement_parameter_sites(process.statements, environment);
  }
  for (auto& function : unit.functions) {
    substitute_callable_parameter_sites(function, environment);
  }
  for (auto& task : unit.tasks) {
    substitute_callable_parameter_sites(task, environment);
  }
  substitute_instance_actuals(unit.instances, environment);
  for (auto& declaration : unit.verilog_defparams) {
      for (auto& segment : declaration.path) {
          for (auto& index : segment.indices) {
              substitute_systemverilog_scalars(index, environment);
          }
      }
      substitute_systemverilog_scalars(declaration.value, environment);
  }
  for (auto& instance : unit.instances) {
    for (auto& connection : instance.connections) {
      substitute_systemverilog_scalars(connection.value, environment);
    }
  }
  substitute_generate_parameter_sites(unit.generate_regions, environment);
}

namespace {

bool contains_systemverilog_scalar(const frontend::Expression& expression) {
  if (expression.systemverilog_scalar_kind
          != frontend::SystemVerilogScalarKind::None
      || expression.systemverilog_decimal_literal
      || expression.text == "@sv-null") {
    return true;
  }
  return std::ranges::any_of(
      expression.operands, contains_systemverilog_scalar);
}

bool callable_type_matches(
    const frontend::Type& left,
    const frontend::Type& right) {
  const bool scalar =
      left.systemverilog_scalar != frontend::SystemVerilogScalarKind::None
      || right.systemverilog_scalar != frontend::SystemVerilogScalarKind::None;
  if (scalar) return left.systemverilog_scalar == right.systemverilog_scalar;
  return left.domain == right.domain
      && left.is_signed == right.is_signed
      && left.spelling == right.spelling
      && left.named_type == right.named_type
      && left.width() == right.width();
}

template <typename Argument>
bool argument_profiles_match(
    const std::vector<Argument>& left,
    const std::vector<Argument>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].direction != right[index].direction
        || left[index].reference != right[index].reference
        || !callable_type_matches(left[index].type, right[index].type)) {
      return false;
    }
  }
  return true;
}

template <typename Callable>
void prepare_callable(
    Callable& callable,
    const SystemVerilogScalarConstantEnvironment& environment) {
  frontend::SystemVerilogScalarTypeEnvironment types;
  for (const auto& [name, value] : environment) types.emplace(name, value.kind);
  for (const auto& argument : callable.arguments) {
    types.insert_or_assign(argument.name, argument.type.systemverilog_scalar);
  }
  for (auto& argument : callable.arguments) {
    if (!argument.default_value) continue;
    substitute_systemverilog_scalars(*argument.default_value, environment);
    std::string error;
    (void)frontend::propagate_systemverilog_scalar_types(
        *argument.default_value, types, error);
  }
}

}  // namespace

bool systemverilog_function_profile_matches(
    const frontend::FunctionDeclaration& left,
    const frontend::FunctionDeclaration& right) {
  return left.name == right.name
      && callable_type_matches(left.return_type, right.return_type)
      && argument_profiles_match(left.arguments, right.arguments);
}

bool systemverilog_task_profile_matches(
    const frontend::TaskDeclaration& left,
    const frontend::TaskDeclaration& right) {
  return left.name == right.name
      && argument_profiles_match(left.arguments, right.arguments);
}

void prepare_systemverilog_scalar_callable_profiles(
    DesignUnit& unit,
    const SystemVerilogScalarConstantEnvironment& environment) {
  frontend::SystemVerilogScalarTypeEnvironment types;
  for (const auto& [name, value] : environment) types.emplace(name, value.kind);
  for (auto& port : unit.ports) {
    if (!port.default_value) continue;
    substitute_systemverilog_scalars(*port.default_value, environment);
    std::string error;
    (void)frontend::propagate_systemverilog_scalar_types(
        *port.default_value, types, error);
  }
  for (auto& function : unit.functions) prepare_callable(function, environment);
  for (auto& task : unit.tasks) prepare_callable(task, environment);
}

std::optional<SystemVerilogScalarConstant>
evaluate_systemverilog_scalar_parameter(
    const Expression& expression,
    const frontend::Type& type,
    const SystemVerilogScalarConstantEnvironment& environment,
    const SystemVerilogConstantEnvironment& integral_environment,
    const ConstantEnvironment& fallback_environment,
    const frontend::SystemVerilogScalarEvaluationContext& context,
    bool& applicable,
    std::string& error) {
  auto candidate = expression;
  substitute_systemverilog_scalars(candidate, environment);
  frontend::SystemVerilogScalarTypeEnvironment types;
  for (const auto& [name, value] : environment) types.emplace(name, value.kind);
  (void)frontend::propagate_systemverilog_scalar_types(candidate, types, error);
  applicable = type.systemverilog_scalar
          != frontend::SystemVerilogScalarKind::None
      || contains_systemverilog_scalar(candidate);
  if (!applicable) {
    error.clear();
    return std::nullopt;
  }
  auto value = frontend::evaluate_systemverilog_scalar_constant(
      candidate, environment, context, error);
  if (!value && type.systemverilog_scalar
                    != frontend::SystemVerilogScalarKind::None) {
    auto integral = evaluate_systemverilog_constant_expression(
        candidate, integral_environment, fallback_environment, error);
    if (integral && integral->known()) {
      if (const auto number = integral->integer_value()) {
        value = SystemVerilogScalarConstant{
            frontend::SystemVerilogScalarKind::None,
            std::bit_cast<std::uint64_t>(*number)};
      }
    }
  }
  if (!value) return std::nullopt;
  auto target = type.systemverilog_scalar;
  const bool implicit = type.spelling == "implicit" && type.named_type.empty();
  if (implicit && target == frontend::SystemVerilogScalarKind::None) {
    target = value->kind;
  }
  return frontend::convert_systemverilog_scalar_constant(
      *value, target, error);
}

}  // namespace fsim::elaboration::elaboration_detail
