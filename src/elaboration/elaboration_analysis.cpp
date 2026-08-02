// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {

namespace {

void substitute_delay_parameters(
    frontend::Delay& delay,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
  if (delay.expression) {
    substitute_parameters(
        *delay.expression, environment, domains, language);
  }
  const auto substitute_alternative =
      [&](frontend::DelayAlternative& alternative) {
        if (alternative.expression) {
          substitute_parameters(
              *alternative.expression,
              environment,
              domains,
              language);
        }
      };
  if (delay.minimum) {
    substitute_alternative(*delay.minimum);
  }
  if (delay.typical) {
    substitute_alternative(*delay.typical);
  }
  if (delay.maximum) {
    substitute_alternative(*delay.maximum);
  }
  if (delay.expression) {
    std::string error;
    const auto value = evaluate_systemverilog_constant_expression(
        *delay.expression, {}, environment, error);
    const auto negative = value && value->known()
        && value->is_signed && value->width != 0
        && ((value->bits >> (value->width - 1U)) & 1U) != 0;
    if (!value || !value->known() || negative) {
      diagnostics.push_back({
          "FSIM-ELAB-SVDELAY-001",
          "SystemVerilog delay expression must be a known "
          "nonnegative locally constant integral value"
              + (error.empty() ? std::string{} : ": " + error),
          delay.expression->span});
      delay.magnitude = 0;
    } else {
      const auto magnitude = value->bits & value->mask();
      if (magnitude != 0
          && delay.magnitude
              > std::numeric_limits<std::uint64_t>::max()
                  / magnitude) {
        diagnostics.push_back({
            "FSIM-ELAB-SVDELAY-002",
            "SystemVerilog delay expression overflows the 64-bit "
            "simulation time range after time-unit normalization",
            delay.expression->span});
        delay.magnitude = 0;
      } else {
        delay.magnitude *= magnitude;
      }
    }
    delay.expression.reset();
  }
  for (auto& additional : delay.additional_values) {
    substitute_delay_parameters(
        additional, environment, domains, diagnostics, language);
  }
}

}  // namespace

std::string generated_scope(
    const std::string_view parent_scope,
    const std::string_view local_scope) {
    return parent_scope.empty()
        ? std::string{local_scope}
        : std::string{parent_scope} + "." + std::string{local_scope};
}

void substitute_parameters(
    frontend::VariableDeclaration& declaration,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
    substitute_parameters(
        declaration.type,
        environment,
        domains,
        diagnostics,
        language);
    if (declaration.initializer) {
        substitute_parameters(
            *declaration.initializer,
            environment,
            domains,
            language);
    }
}

void substitute_parameters(
    frontend::SignalDeclaration& declaration,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
    substitute_parameters(
        declaration.type,
        environment,
        domains,
        diagnostics,
        language);
    if (declaration.net_delay) {
        substitute_delay_parameters(
            *declaration.net_delay,
            environment,
            domains,
            diagnostics,
            language);
    }
}

void substitute_parameters(
    std::vector<Statement>& statements,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    std::vector<Diagnostic>& diagnostics,
    const frontend::Language language) {
    for (auto& statement : statements) {
        if (statement.delay) {
            substitute_delay_parameters(
                *statement.delay,
                environment,
                domains,
                diagnostics,
                language);
        }
        substitute_parameters(
            statement.target, environment, domains, language);
        substitute_parameters(
            statement.value, environment, domains, language);
        for (auto& element : statement.vhdl_waveform) {
            substitute_parameters(
                element.value, environment, domains, language);
        }
        substitute_parameters(
            statement.condition, environment, domains, language);
        for (auto& argument : statement.task_arguments) {
            substitute_parameters(
                argument, environment, domains, language);
        }
        for (auto& sensitivity : statement.sensitivities) {
            substitute_parameters(
                sensitivity.expression,
                environment,
                domains,
                language);
        }
        for (auto& association :
             statement.procedure_arguments) {
            substitute_parameters(
                association.value,
                environment,
                domains,
                language);
        }
        substitute_parameters(
            statement.loop_initial, environment, domains, language);
        substitute_parameters(
            statement.loop_limit, environment, domains, language);
        for (auto& declaration : statement.declarations) {
            substitute_parameters(
                declaration,
                environment,
                domains,
                diagnostics,
                language);
        }
        for (auto& alternative : statement.case_alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_parameters(
                    choice, environment, domains, language);
            }
            substitute_parameters(
                alternative.statements,
                environment,
                domains,
                diagnostics,
                language);
        }
        auto statement_environment = environment;
        auto statement_domains = domains;
        if (statement.kind == StatementKind::Loop) {
            statement_environment.erase(statement.loop_variable);
            statement_domains.erase(statement.loop_variable);
        }
        substitute_parameters(
            statement.statements,
            statement_environment,
            statement_domains,
            diagnostics,
            language);
        substitute_parameters(
            statement.else_statements,
            environment,
            domains,
            diagnostics,
            language);
    }
}
void substitute_parameters(
    std::vector<frontend::Instance>& instances,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language) {
    for (auto& instance : instances) {
        for (auto& override : instance.parameter_overrides) {
            substitute_parameters(
                override.value, environment, domains, language);
        }
        for (auto& connection : instance.connections) {
            substitute_parameters(
                connection.value, environment, domains, language);
        }
    }
}
void collect_qualified_identifiers(
    const Expression& expression,
    QualifiedIdentifierMap& identifiers) {
    if ((expression.kind == ExpressionKind::Identifier
         || expression.kind == ExpressionKind::Call)
        && (expression.text.find('.') != std::string::npos
            || expression.text.find("::")
                != std::string::npos)) {
        identifiers.try_emplace(
            expression.text, expression.span);
    }
    for (const auto& association :
         expression.aggregate_choice_expressions) {
        for (const auto& choice : association) {
            collect_qualified_identifiers(
                choice, identifiers);
        }
    }
    for (const auto& operand : expression.operands) {
        collect_qualified_identifiers(operand, identifiers);
    }
}
void collect_qualified_identifiers(
    const frontend::Type& type,
    QualifiedIdentifierMap& identifiers) {
    if (!type.named_type.empty()
        && type.named_type.find("::") != std::string::npos) {
        identifiers.try_emplace(
            type.named_type, type.named_type_span);
    }
    for (const auto& member : type.packed_members) {
        if (!member.nested_types.empty()) {
            collect_qualified_identifiers(
                member.nested_types.front(), identifiers);
        }
        if (!member.packed_range_expression) {
            continue;
        }
        collect_qualified_identifiers(
            member.packed_range_expression->left,
            identifiers);
        collect_qualified_identifiers(
            member.packed_range_expression->right,
            identifiers);
    }
    if (type.integer_range_expression) {
        collect_qualified_identifiers(
            type.integer_range_expression->left, identifiers);
        collect_qualified_identifiers(
            type.integer_range_expression->right, identifiers);
    }
    if (type.integer_base_range_expression) {
        collect_qualified_identifiers(
            type.integer_base_range_expression->left,
            identifiers);
        collect_qualified_identifiers(
            type.integer_base_range_expression->right,
            identifiers);
    }
    const auto collect_discrete_range =
        [&](const std::optional<
                frontend::DiscreteRangeExpression>& range) {
          if (!range) {
              return;
          }
          collect_qualified_identifiers(
              range->left, identifiers);
          collect_qualified_identifiers(
              range->right, identifiers);
        };
    collect_discrete_range(type.discrete_range_expression);
    collect_discrete_range(type.enumeration_range_expression);
    collect_discrete_range(
        type.enumeration_base_range_expression);
    if (type.systemverilog_container) {
        if (type.systemverilog_container->queue_maximum) {
            collect_qualified_identifiers(
                *type.systemverilog_container->queue_maximum,
                identifiers);
        }
        for (const auto& range :
             type.systemverilog_container
                 ->static_range_expressions) {
            collect_qualified_identifiers(
                range.left, identifiers);
            collect_qualified_identifiers(
                range.right, identifiers);
        }
        if (type.systemverilog_container
                ->associative_index_type) {
            collect_qualified_identifiers(
                *type.systemverilog_container
                     ->associative_index_type,
                identifiers);
        }
    }
    if (!type.packed_range_expression) {
        return;
    }
    collect_qualified_identifiers(
        type.packed_range_expression->left, identifiers);
    collect_qualified_identifiers(
        type.packed_range_expression->right, identifiers);
}
void collect_qualified_identifiers(
    const std::vector<Statement>& statements,
    QualifiedIdentifierMap& identifiers) {
    for (const auto& statement : statements) {
        collect_qualified_identifiers(
            statement.target, identifiers);
        collect_qualified_identifiers(
            statement.value, identifiers);
        for (const auto& element : statement.vhdl_waveform) {
            collect_qualified_identifiers(
                element.value, identifiers);
        }
        collect_qualified_identifiers(
            statement.condition, identifiers);
        if (statement.kind == StatementKind::TaskCall
            && statement.task_name.find("::")
                != std::string::npos) {
            identifiers.try_emplace(
                statement.task_name, statement.span);
        }
        if (statement.kind == StatementKind::ProcedureCall
            && statement.procedure_name.find('.')
                != std::string::npos) {
            identifiers.try_emplace(
                statement.procedure_name, statement.span);
        }
        for (const auto& argument : statement.task_arguments) {
            collect_qualified_identifiers(
                argument, identifiers);
        }
        for (const auto& sensitivity : statement.sensitivities) {
            collect_qualified_identifiers(
                sensitivity.expression, identifiers);
        }
        for (const auto& association :
             statement.procedure_arguments) {
            collect_qualified_identifiers(
                association.value, identifiers);
        }
        for (const auto& declaration : statement.declarations) {
            collect_qualified_identifiers(
                declaration.type, identifiers);
            if (declaration.initializer) {
                collect_qualified_identifiers(
                    *declaration.initializer, identifiers);
            }
        }
        for (const auto& alternative :
             statement.case_alternatives) {
            for (const auto& choice : alternative.choices) {
                collect_qualified_identifiers(
                    choice, identifiers);
            }
            collect_qualified_identifiers(
                alternative.statements, identifiers);
        }
        collect_qualified_identifiers(
            statement.statements, identifiers);
        collect_qualified_identifiers(
            statement.else_statements, identifiers);
    }
}
void collect_qualified_identifiers(
    const frontend::GenerateBody& body,
    QualifiedIdentifierMap& identifiers) {
    std::unordered_set<std::string> prior_identifiers;
    prior_identifiers.reserve(identifiers.size());
    for (const auto& [name, span] : identifiers) {
        (void)span;
        prior_identifiers.insert(name);
    }
    for (const auto& alias : body.type_aliases) {
        collect_qualified_identifiers(alias.type, identifiers);
    }
    for (const auto& constant : body.constants) {
        collect_qualified_identifiers(
            constant.type, identifiers);
        collect_qualified_identifiers(
            constant.default_value, identifiers);
    }
    for (const auto& signal : body.signals) {
        collect_qualified_identifiers(signal.type, identifiers);
    }
    for (const auto& function : body.functions) {
        collect_vhdl_local_qualified_identifiers(
            function, identifiers);
    }
    for (const auto& task : body.tasks) {
        for (const auto& argument : task.arguments) {
            collect_qualified_identifiers(argument.type, identifiers);
            if (argument.default_value) {
                collect_qualified_identifiers(
                    *argument.default_value, identifiers);
            }
        }
        for (const auto& variable : task.variables) {
            collect_qualified_identifiers(variable.type, identifiers);
            if (variable.initializer) {
                collect_qualified_identifiers(
                    *variable.initializer, identifiers);
            }
        }
        collect_qualified_identifiers(task.statements, identifiers);
    }
    const auto collect_generic_parameters =
        [&](const auto& parameters) {
          for (const auto& parameter : parameters) {
            collect_qualified_identifiers(
                parameter.type, identifiers);
            collect_qualified_identifiers(
                parameter.default_value, identifiers);
            if (parameter.default_type) {
                collect_qualified_identifiers(
                    *parameter.default_type, identifiers);
            }
            if (parameter.function_profile) {
                collect_qualified_identifiers(
                    parameter.function_profile->return_type,
                    identifiers);
                for (const auto& argument :
                     parameter.function_profile->arguments) {
                    collect_qualified_identifiers(
                        argument.type, identifiers);
                }
            }
            if (parameter.procedure_profile) {
                for (const auto& argument :
                     parameter.procedure_profile->arguments) {
                    collect_qualified_identifiers(
                        argument.type, identifiers);
                }
            }
          }
        };
    for (const auto& generic :
         body.generic_function_templates) {
        collect_generic_parameters(generic.generic_parameters);
        collect_vhdl_local_qualified_identifiers(
            generic.function, identifiers);
    }
    for (const auto& generic :
         body.generic_procedure_templates) {
        collect_generic_parameters(generic.generic_parameters);
        collect_vhdl_local_qualified_identifiers(
            generic.procedure, identifiers);
    }
    const auto collect_generic_maps = [&](const auto& instances) {
      for (const auto& instance : instances) {
        for (const auto& actual : instance.generic_map) {
          collect_qualified_identifiers(actual.value, identifiers);
          if (actual.type_value) {
            collect_qualified_identifiers(
                *actual.type_value, identifiers);
          }
        }
      }
    };
    collect_generic_maps(body.generic_function_instances);
    collect_generic_maps(body.generic_procedure_instances);
    for (const auto& component :
         body.vhdl_component_declarations) {
        for (const auto& generic : component.generics) {
            collect_qualified_identifiers(
                generic.type, identifiers);
            collect_qualified_identifiers(
                generic.default_value, identifiers);
        }
        for (const auto& port : component.ports) {
            collect_qualified_identifiers(
                port.type, identifiers);
            if (port.default_value) {
                collect_qualified_identifiers(
                    *port.default_value, identifiers);
            }
        }
    }
    for (const auto& package : body.package_instances) {
        for (const auto& actual : package.generic_map) {
            collect_qualified_identifiers(
                actual.value, identifiers);
            if (actual.type_value) {
                collect_qualified_identifiers(
                    *actual.type_value, identifiers);
            }
        }
    }
    collect_qualified_identifiers(
        body.concurrent_statements, identifiers);
    for (const auto& process : body.processes) {
        collect_vhdl_local_qualified_identifiers(
            process, identifiers);
    }
    for (const auto& procedure : body.procedures) {
        collect_vhdl_local_qualified_identifiers(
            procedure, identifiers);
    }
    for (const auto& instance : body.instances) {
        for (const auto& override :
             instance.parameter_overrides) {
            collect_qualified_identifiers(
                override.value, identifiers);
        }
        for (const auto& connection : instance.connections) {
            collect_qualified_identifiers(
                connection.value, identifiers);
        }
    }
    collect_qualified_identifiers(
        body.generate_regions, identifiers);
    for (const auto& package : body.package_instances) {
        const auto prefix = package.name + ".";
        std::erase_if(
            identifiers,
            [&](const auto& identifier) {
              return !prior_identifiers.contains(identifier.first)
                  && identifier.first.starts_with(prefix);
            });
    }
}
void collect_qualified_identifiers(
    const std::vector<frontend::GenerateRegion>& generates,
    QualifiedIdentifierMap& identifiers) {
    for (const auto& generate : generates) {
        for (const auto& generic : generate.block_generics) {
            collect_qualified_identifiers(generic.type, identifiers);
            collect_qualified_identifiers(
                generic.default_value, identifiers);
        }
        for (const auto& actual : generate.block_generic_map) {
            collect_qualified_identifiers(actual.value, identifiers);
            if (actual.type_value) {
                collect_qualified_identifiers(
                    *actual.type_value, identifiers);
            }
        }
        for (const auto& port : generate.block_ports) {
            collect_qualified_identifiers(port.type, identifiers);
            if (port.default_value) {
                collect_qualified_identifiers(
                    *port.default_value, identifiers);
            }
        }
        for (const auto& actual : generate.block_port_map) {
            collect_qualified_identifiers(actual.value, identifiers);
        }
        collect_qualified_identifiers(
            generate.initial, identifiers);
        collect_qualified_identifiers(
            generate.condition, identifiers);
        collect_qualified_identifiers(
            generate.iteration, identifiers);
        const bool deferred_vhdl_block =
            generate.kind == frontend::GenerateKind::StaticBlock
            && std::ranges::any_of(
                generate.block_generics,
                [](const auto& generic) {
                  return generic.kind
                      != frontend::ParameterKind::Value;
                });
        if (deferred_vhdl_block) {
            continue;
        }
        collect_qualified_identifiers(
            generate.then_body, identifiers);
        collect_qualified_identifiers(
            generate.else_body, identifiers);
        for (const auto& alternative :
             generate.alternatives) {
            for (const auto& choice : alternative.choices) {
                collect_qualified_identifiers(
                    choice.left, identifiers);
                if (choice.right) {
                    collect_qualified_identifiers(
                        *choice.right, identifiers);
                }
            }
            collect_qualified_identifiers(
                alternative.body, identifiers);
        }
    }
}
QualifiedIdentifierMap qualified_identifiers(
    const DesignUnit& unit) {
    QualifiedIdentifierMap result;
    for (const auto& alias : unit.type_aliases) {
        collect_qualified_identifiers(alias.type, result);
    }
    for (const auto& parameter : unit.parameters) {
        collect_qualified_identifiers(parameter.type, result);
        collect_qualified_identifiers(
            parameter.default_value, result);
    }
    for (const auto& port : unit.ports) {
        collect_qualified_identifiers(port.type, result);
    }
    for (const auto& signal : unit.signals) {
        collect_qualified_identifiers(signal.type, result);
    }
    for (const auto& component :
         unit.vhdl_component_declarations) {
        for (const auto& generic : component.generics) {
            collect_qualified_identifiers(
                generic.type, result);
            collect_qualified_identifiers(
                generic.default_value, result);
        }
        for (const auto& port : component.ports) {
            collect_qualified_identifiers(
                port.type, result);
            if (port.default_value) {
                collect_qualified_identifiers(
                    *port.default_value, result);
            }
        }
    }
    for (const auto& function : unit.functions) {
        collect_vhdl_local_qualified_identifiers(
            function, result);
    }
    for (const auto& task : unit.tasks) {
        for (const auto& argument : task.arguments) {
            collect_qualified_identifiers(argument.type, result);
            if (argument.default_value) {
                collect_qualified_identifiers(
                    *argument.default_value, result);
            }
        }
        for (const auto& variable : task.variables) {
            collect_qualified_identifiers(
                variable.type, result);
            if (variable.initializer) {
                collect_qualified_identifiers(
                    *variable.initializer, result);
            }
        }
        collect_qualified_identifiers(
            task.statements, result);
    }
    for (const auto& procedure : unit.procedures) {
        collect_vhdl_local_qualified_identifiers(
            procedure, result);
    }
    for (const auto& generic : unit.generic_function_templates) {
        for (const auto& parameter :
             generic.generic_parameters) {
            collect_qualified_identifiers(
                parameter.type, result);
            collect_qualified_identifiers(
                parameter.default_value, result);
        }
        collect_vhdl_local_qualified_identifiers(
            generic.function, result);
    }
    for (const auto& generic :
         unit.generic_procedure_templates) {
        for (const auto& parameter :
             generic.generic_parameters) {
            collect_qualified_identifiers(
                parameter.type, result);
            collect_qualified_identifiers(
                parameter.default_value, result);
        }
        collect_vhdl_local_qualified_identifiers(
            generic.procedure, result);
    }
    const auto collect_generic_map =
        [&](const auto& instances) {
          for (const auto& instance : instances) {
            for (const auto& actual : instance.generic_map) {
              collect_qualified_identifiers(
                  actual.value, result);
              if (actual.type_value) {
                collect_qualified_identifiers(
                    *actual.type_value, result);
              }
            }
          }
        };
    collect_generic_map(unit.generic_function_instances);
    collect_generic_map(unit.generic_procedure_instances);
    collect_qualified_identifiers(
        unit.concurrent_statements, result);
    for (const auto& process : unit.processes) {
        collect_vhdl_local_qualified_identifiers(
            process, result);
    }
    for (const auto& instance : unit.instances) {
        for (const auto& override :
             instance.parameter_overrides) {
            collect_qualified_identifiers(
                override.value, result);
        }
        for (const auto& connection : instance.connections) {
            collect_qualified_identifiers(
                connection.value, result);
        }
    }
    collect_qualified_identifiers(unit.generate_regions, result);
    return result;
}
void qualify_generated_statement(
    Statement& statement,
    const GeneratedNameEnvironment& names) {
    auto body_names = names;
    for (auto& declaration : statement.declarations) {
        if (declaration.initializer) {
            qualify_generated_expression(
                *declaration.initializer, body_names);
        }
        body_names.erase(declaration.name);
    }
    qualify_generated_expression(statement.target, body_names);
    qualify_generated_expression(statement.value, body_names);
    for (auto& element : statement.vhdl_waveform) {
        qualify_generated_expression(element.value, body_names);
    }
    qualify_generated_expression(statement.condition, body_names);
    for (auto& argument : statement.task_arguments) {
        qualify_generated_expression(argument, body_names);
    }
    if (statement.kind == StatementKind::TaskCall) {
        if (const auto found = body_names.find(statement.task_name);
            found != body_names.end()) {
            statement.task_name = found->second;
        }
    }
    for (auto& association : statement.procedure_arguments) {
        qualify_generated_expression(
            association.value, body_names);
    }
    if (statement.kind == StatementKind::ProcedureCall) {
        if (const auto found =
                body_names.find(statement.procedure_name);
            found != body_names.end()) {
            statement.procedure_name = found->second;
        }
    }
    for (auto& sensitivity : statement.sensitivities) {
        qualify_generated_expression(
            sensitivity.expression, body_names);
        if (const auto found = body_names.find(sensitivity.signal);
            found != body_names.end()) {
            sensitivity.signal = found->second;
        }
    }
    for (auto& alternative : statement.case_alternatives) {
        for (auto& choice : alternative.choices) {
            qualify_generated_expression(choice, body_names);
        }
        qualify_generated_statements(
            alternative.statements, body_names);
    }
    qualify_generated_statements(statement.statements, body_names);
    qualify_generated_statements(
        statement.else_statements, body_names);
}
void qualify_generated_statements(
    std::vector<Statement>& statements,
    const GeneratedNameEnvironment& names) {
    for (auto& statement : statements) {
        qualify_generated_statement(statement, names);
    }
}
void qualify_generated_process(
    frontend::Process& process,
    const GeneratedNameEnvironment& names,
    const std::string_view scope) {
    process.name = process.name.empty()
        ? std::string{scope}
        : generated_scope(scope, process.name);
    auto process_names = names;
    for (auto& alias : process.signal_aliases) {
        qualify_generated_type(alias.type, process_names);
        if (const auto found = process_names.find(alias.actual);
            found != process_names.end()) {
            alias.actual = found->second;
        }
    }
    for (auto& variable : process.variables) {
        qualify_generated_type(variable.type, process_names);
        if (variable.initializer) {
            qualify_generated_expression(
                *variable.initializer, process_names);
        }
        process_names.erase(variable.name);
    }
    for (auto& sensitivity : process.sensitivities) {
        qualify_generated_expression(
            sensitivity.expression, process_names);
        if (const auto found = process_names.find(sensitivity.signal);
            found != process_names.end()) {
            sensitivity.signal = found->second;
        }
    }
    qualify_generated_statements(process.statements, process_names);
}
void qualify_generated_function(
    frontend::FunctionDeclaration& function,
    const GeneratedNameEnvironment& names,
    const std::string_view scope) {
    const auto local_name = function.name;
    function.name = generated_scope(scope, local_name);
    auto function_names = names;
    for (auto& alias : function.signal_aliases) {
        qualify_generated_type(alias.type, function_names);
        if (const auto found = function_names.find(alias.actual);
            found != function_names.end()) {
            alias.actual = found->second;
        }
    }
    qualify_generated_type(function.return_type, function_names);
    for (auto& argument : function.arguments) {
        qualify_generated_type(argument.type, function_names);
        if (argument.default_value) {
            qualify_generated_expression(
                *argument.default_value, function_names);
        }
        function_names.erase(argument.name);
    }
    for (auto& variable : function.variables) {
        qualify_generated_type(variable.type, function_names);
        if (variable.initializer) {
            qualify_generated_expression(
                *variable.initializer, function_names);
        }
        function_names.erase(variable.name);
    }
    qualify_generated_statements(function.statements, function_names);
}
void qualify_generated_task(
    frontend::TaskDeclaration& task,
    const GeneratedNameEnvironment& names,
    const std::string_view scope) {
    const auto local_name = task.name;
    task.name = generated_scope(scope, local_name);
    auto task_names = names;
    for (auto& argument : task.arguments) {
        qualify_generated_type(argument.type, task_names);
        if (argument.default_value) {
            qualify_generated_expression(
                *argument.default_value, task_names);
        }
        task_names.erase(argument.name);
    }
    for (auto& variable : task.variables) {
        qualify_generated_type(variable.type, task_names);
        if (variable.initializer) {
            qualify_generated_expression(
                *variable.initializer, task_names);
        }
        task_names.erase(variable.name);
    }
    qualify_generated_statements(task.statements, task_names);
}
void qualify_generated_procedure(
    frontend::ProcedureDeclaration& procedure,
    const GeneratedNameEnvironment& names,
    const std::string_view scope) {
    const auto local_name = procedure.name;
    procedure.name = generated_scope(scope, local_name);
    auto procedure_names = names;
    procedure_names[local_name] = procedure.name;
    for (auto& alias : procedure.signal_aliases) {
        qualify_generated_type(alias.type, procedure_names);
        if (const auto found = procedure_names.find(alias.actual);
            found != procedure_names.end()) {
            alias.actual = found->second;
        }
    }
    for (auto& argument : procedure.arguments) {
        qualify_generated_type(argument.type, names);
        if (argument.default_value) {
            qualify_generated_expression(
                *argument.default_value, procedure_names);
        }
        procedure_names.erase(argument.name);
    }
    for (auto& variable : procedure.variables) {
        qualify_generated_type(variable.type, names);
        if (variable.initializer) {
            qualify_generated_expression(
                *variable.initializer, procedure_names);
        }
        procedure_names.erase(variable.name);
    }
    qualify_generated_statements(
        procedure.statements, procedure_names);
}
void qualify_generated_instance(
    frontend::Instance& instance,
    const GeneratedNameEnvironment& names,
    const std::string_view scope) {
    instance.name = generated_scope(scope, instance.name);
    for (auto& override : instance.parameter_overrides) {
        qualify_generated_expression(override.value, names);
    }
    for (auto& connection : instance.connections) {
        qualify_generated_expression(connection.value, names);
    }
}
void evaluate_generated_constants(
    frontend::GenerateBody& body,
    ConstantEnvironment& environment,
    ConstantDomainEnvironment& domains,
    const frontend::Language language,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& constant : body.constants) {
        substitute_parameters(
            constant.type,
            environment,
            domains,
            diagnostics,
            language);
        substitute_parameters(
            constant.default_value,
            environment,
            domains,
            language);
        std::string error;
        auto value = evaluate_constant_expression(
            constant.default_value, environment, error);
        if (!value) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-011",
                "cannot evaluate generated "
                    + std::string{
                        language == frontend::Language::Vhdl2008
                            ? "constant"
                            : "parameter"}
                    + " '" + constant.name + "': " + error,
                constant.span});
            value = 0;
        }
        if (language != frontend::Language::Vhdl2008) {
            *value = normalize_systemverilog_parameter_value(
                *value, constant.type);
        }
        const bool exceeds_word =
            constant.type.packed_range
            && constant.type.packed_range->width() > 64;
        if (exceeds_word) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-012",
                "generated "
                    + std::string{
                        language == frontend::Language::Vhdl2008
                            ? "constant"
                            : "parameter"}
                    + " '" + constant.name
                    + "' exceeds the bounded 64-bit integral width",
                constant.span});
        } else if (language == frontend::Language::Vhdl2008) {
            const auto& spelling = constant.type.spelling;
            const bool violates_natural =
                spelling == "natural" && *value < 0;
            const bool violates_positive =
                spelling == "positive" && *value <= 0;
            const bool violates_boolean =
                constant.type.domain == frontend::ValueDomain::Boolean
                && *value != 0 && *value != 1;
            const bool violates_bit =
                constant.type.domain == frontend::ValueDomain::Bit2
                && *value != 0 && *value != 1;
            if (violates_natural || violates_positive
                || violates_boolean || violates_bit) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-012",
                    "generated constant '" + constant.name
                        + "' value is outside subtype '"
                        + constant.type.spelling + "'",
                    constant.span});
            }
        }
        environment[constant.name] = *value;
        domains[constant.name] = ConstantTypeInfo{
            constant.type.domain,
            language == frontend::Language::Vhdl2008
                && !constant.type.enumeration_literals.empty(),
            constant.type.nominal_type};
    }
}
void append_generated_body(
    frontend::GenerateBody body,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    const std::string_view scope,
    const GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics,
    const VhdlBlockInterfacePreparer* block_preparer) {
    auto body_environment = environment;
    auto body_domains = domains;
    evaluate_generated_constants(
        body,
        body_environment,
        body_domains,
        language,
        diagnostics);
    body.constants.clear();
    substitute_parameters(
        body,
        body_environment,
        body_domains,
        language,
        diagnostics);
    auto body_names = visible_names;
    for (const auto& alias : body.type_aliases) {
        body_names[alias.name] = generated_scope(scope, alias.name);
    }
    for (const auto& package : body.package_instances) {
        body_names[package.name] =
            generated_scope(scope, package.name);
    }
    std::unordered_map<std::string, std::string>
        scoped_vhdl_type_identities;
    if (language == frontend::Language::Vhdl2008) {
        const auto scope_suffix = "@" + std::string{scope};
        for (const auto& alias : body.type_aliases) {
            if (alias.declaration_kind
                    != frontend::TypeDeclarationKind::VhdlSubtype
                && !alias.type.nominal_type.empty()) {
                scoped_vhdl_type_identities.emplace(
                    alias.type.nominal_type,
                    alias.type.nominal_type + scope_suffix);
            }
            if (!alias.type.vhdl_type_declaration.empty()) {
                scoped_vhdl_type_identities.emplace(
                    alias.type.vhdl_type_declaration,
                    alias.type.vhdl_type_declaration + scope_suffix);
            }
        }
        const auto append_local_type_identities =
            [&](const auto& owner) {
              for (const auto& alias : owner.type_aliases) {
                  if (alias.declaration_kind
                          != frontend::TypeDeclarationKind::VhdlSubtype
                      && !alias.type.nominal_type.empty()) {
                      scoped_vhdl_type_identities.emplace(
                          alias.type.nominal_type,
                          alias.type.nominal_type + scope_suffix);
                  }
                  if (!alias.type.vhdl_type_declaration.empty()) {
                      scoped_vhdl_type_identities.emplace(
                          alias.type.vhdl_type_declaration,
                          alias.type.vhdl_type_declaration
                              + scope_suffix);
                  }
              }
            };
        for (const auto& function : body.functions) {
            append_local_type_identities(function);
        }
        for (const auto& procedure : body.procedures) {
            append_local_type_identities(procedure);
        }
        for (const auto& process : body.processes) {
            append_local_type_identities(process);
        }
    }
    const auto scope_vhdl_type =
        [&](auto&& self, frontend::Type& type) -> void {
          if (const auto found = scoped_vhdl_type_identities.find(
                  type.nominal_type);
              found != scoped_vhdl_type_identities.end()) {
              type.nominal_type = found->second;
          }
          if (const auto found = scoped_vhdl_type_identities.find(
                  type.vhdl_type_declaration);
              found != scoped_vhdl_type_identities.end()) {
              type.vhdl_type_declaration = found->second;
          }
          for (auto& member : type.packed_members) {
              for (auto& nested : member.nested_types) {
                  self(self, nested);
              }
          }
          if (type.systemverilog_container
              && type.systemverilog_container
                     ->associative_index_type) {
              self(
                  self,
                  *type.systemverilog_container
                       ->associative_index_type);
          }
        };
    for (auto& alias : body.type_aliases) {
        scope_vhdl_type(scope_vhdl_type, alias.type);
    }
    for (auto& signal : body.signals) {
        scope_vhdl_type(scope_vhdl_type, signal.type);
    }
    for (auto& alias : body.signal_aliases) {
        scope_vhdl_type(scope_vhdl_type, alias.type);
    }
    for (auto& function : body.functions) {
        scope_vhdl_type(scope_vhdl_type, function.return_type);
        for (auto& argument : function.arguments) {
            scope_vhdl_type(scope_vhdl_type, argument.type);
        }
        for (auto& variable : function.variables) {
            scope_vhdl_type(scope_vhdl_type, variable.type);
        }
        for (auto& constant : function.constants) {
            scope_vhdl_type(scope_vhdl_type, constant.type);
        }
        for (auto& alias : function.type_aliases) {
            scope_vhdl_type(scope_vhdl_type, alias.type);
        }
        for (auto& alias : function.signal_aliases) {
            scope_vhdl_type(scope_vhdl_type, alias.type);
        }
    }
    for (auto& procedure : body.procedures) {
        for (auto& argument : procedure.arguments) {
            scope_vhdl_type(scope_vhdl_type, argument.type);
        }
        for (auto& variable : procedure.variables) {
            scope_vhdl_type(scope_vhdl_type, variable.type);
        }
        for (auto& constant : procedure.constants) {
            scope_vhdl_type(scope_vhdl_type, constant.type);
        }
        for (auto& alias : procedure.type_aliases) {
            scope_vhdl_type(scope_vhdl_type, alias.type);
        }
        for (auto& alias : procedure.signal_aliases) {
            scope_vhdl_type(scope_vhdl_type, alias.type);
        }
    }
    for (auto& process : body.processes) {
        for (auto& constant : process.constants) {
            scope_vhdl_type(scope_vhdl_type, constant.type);
        }
        for (auto& alias : process.type_aliases) {
            scope_vhdl_type(scope_vhdl_type, alias.type);
        }
        for (auto& alias : process.signal_aliases) {
            scope_vhdl_type(scope_vhdl_type, alias.type);
        }
        for (auto& variable : process.variables) {
            scope_vhdl_type(scope_vhdl_type, variable.type);
        }
    }
    for (auto& generic : body.generic_function_templates) {
        scope_vhdl_type(
            scope_vhdl_type, generic.function.return_type);
        for (auto& argument : generic.function.arguments) {
            scope_vhdl_type(scope_vhdl_type, argument.type);
        }
        for (auto& variable : generic.function.variables) {
            scope_vhdl_type(scope_vhdl_type, variable.type);
        }
    }
    for (auto& generic : body.generic_procedure_templates) {
        for (auto& argument : generic.procedure.arguments) {
            scope_vhdl_type(scope_vhdl_type, argument.type);
        }
        for (auto& variable : generic.procedure.variables) {
            scope_vhdl_type(scope_vhdl_type, variable.type);
        }
    }
    const auto conforming_type =
        [](const frontend::Type& left,
           const frontend::Type& right) {
          return left.spelling == right.spelling
              && left.named_type == right.named_type
              && left.domain == right.domain
              && left.is_signed == right.is_signed;
        };
    std::vector<bool> merged_function_declarations(
        body.functions.size());
    for (std::size_t declaration = 0;
         declaration < body.functions.size(); ++declaration) {
        const auto& candidate = body.functions[declaration];
        if (candidate.defined) {
            continue;
        }
        for (std::size_t body_index = declaration + 1;
             body_index < body.functions.size(); ++body_index) {
            auto& defined = body.functions[body_index];
            if (!defined.defined
                || candidate.name != defined.name
                || candidate.pure != defined.pure
                || candidate.arguments.size()
                    != defined.arguments.size()
                || !conforming_type(
                    candidate.return_type, defined.return_type)) {
                continue;
            }
            bool conforming = true;
            for (std::size_t argument = 0;
                 argument < candidate.arguments.size(); ++argument) {
                if (candidate.arguments[argument].direction
                        != defined.arguments[argument].direction
                    || !conforming_type(
                        candidate.arguments[argument].type,
                        defined.arguments[argument].type)) {
                    conforming = false;
                    break;
                }
            }
            if (!conforming) {
                continue;
            }
            for (std::size_t argument = 0;
                 argument < candidate.arguments.size(); ++argument) {
                if (!defined.arguments[argument].default_value
                    && candidate.arguments[argument].default_value) {
                    defined.arguments[argument].default_value =
                        candidate.arguments[argument].default_value;
                }
            }
            merged_function_declarations[declaration] = true;
            break;
        }
    }
    for (std::size_t index = body.functions.size();
         index != 0; --index) {
        if (merged_function_declarations[index - 1]) {
            body.functions.erase(
                body.functions.begin()
                + static_cast<std::ptrdiff_t>(index - 1));
        }
    }
    std::vector<bool> merged_procedure_declarations(
        body.procedures.size());
    for (std::size_t declaration = 0;
         declaration < body.procedures.size(); ++declaration) {
        const auto& candidate = body.procedures[declaration];
        if (candidate.defined) {
            continue;
        }
        for (std::size_t body_index = declaration + 1;
             body_index < body.procedures.size(); ++body_index) {
            auto& defined = body.procedures[body_index];
            if (!defined.defined
                || candidate.name != defined.name
                || candidate.arguments.size()
                    != defined.arguments.size()) {
                continue;
            }
            bool conforming = true;
            for (std::size_t argument = 0;
                 argument < candidate.arguments.size(); ++argument) {
                const auto& left = candidate.arguments[argument];
                const auto& right = defined.arguments[argument];
                if (left.direction != right.direction
                    || left.object_class != right.object_class
                    || !conforming_type(left.type, right.type)) {
                    conforming = false;
                    break;
                }
            }
            if (!conforming) {
                continue;
            }
            for (std::size_t argument = 0;
                 argument < candidate.arguments.size(); ++argument) {
                if (!defined.arguments[argument].default_value
                    && candidate.arguments[argument].default_value) {
                    defined.arguments[argument].default_value =
                        candidate.arguments[argument].default_value;
                }
            }
            merged_procedure_declarations[declaration] = true;
            break;
        }
    }
    for (std::size_t index = body.procedures.size();
         index != 0; --index) {
        if (merged_procedure_declarations[index - 1]) {
            body.procedures.erase(
                body.procedures.begin()
                + static_cast<std::ptrdiff_t>(index - 1));
        }
    }
    for (auto& alias : body.type_aliases) {
        const auto local_name = alias.name;
        qualify_generated_type(alias.type, body_names);
        alias.name = body_names.at(local_name);
        unit.type_aliases.push_back(std::move(alias));
    }
    for (auto& declaration :
         body.vhdl_component_declarations) {
        declaration.scope_path = scope;
        declaration.declaration_order =
            unit.vhdl_component_declarations.size();
        declaration.owner_library = unit.library;
        declaration.owner_name = unit.name;
        unit.vhdl_component_declarations.push_back(
            std::move(declaration));
    }
    for (auto& signal : body.signals) {
        const auto local_name = signal.name;
        qualify_generated_type(signal.type, body_names);
        signal.name = generated_scope(scope, local_name);
        body_names[local_name] = signal.name;
        unit.signals.push_back(std::move(signal));
    }
    for (auto& alias : body.signal_aliases) {
        const auto local_name = alias.name;
        qualify_generated_type(alias.type, body_names);
        if (const auto found = body_names.find(alias.actual);
            found != body_names.end()) {
            alias.actual = found->second;
        }
        alias.name = generated_scope(scope, local_name);
        body_names[local_name] = alias.name;
        unit.signal_aliases.push_back(std::move(alias));
    }
    if (language == frontend::Language::Vhdl2008) {
        struct CallableDeclaration {
            std::size_t offset{};
            bool function{};
            std::size_t index{};
        };
        std::vector<CallableDeclaration> declarations;
        for (std::size_t index = 0;
             index < body.functions.size(); ++index) {
            declarations.push_back({
                body.functions[index].span.begin.offset,
                true,
                index});
        }
        for (std::size_t index = 0;
             index < body.procedures.size(); ++index) {
            declarations.push_back({
                body.procedures[index].span.begin.offset,
                false,
                index});
        }
        std::ranges::stable_sort(
            declarations, {}, &CallableDeclaration::offset);
        auto callable_names = body_names;
        for (const auto& declaration : declarations) {
            if (declaration.function) {
                auto& function = body.functions[declaration.index];
                callable_names[function.name] =
                    generated_scope(scope, function.name);
                qualify_generated_function(
                    function, callable_names, scope);
            } else {
                auto& procedure = body.procedures[declaration.index];
                callable_names[procedure.name] =
                    generated_scope(scope, procedure.name);
                qualify_generated_procedure(
                    procedure, callable_names, scope);
            }
        }
        body_names = std::move(callable_names);
    } else {
        for (const auto& function : body.functions) {
            body_names[function.name] =
                generated_scope(scope, function.name);
        }
        for (const auto& task : body.tasks) {
            body_names[task.name] =
                generated_scope(scope, task.name);
        }
        for (auto& function : body.functions) {
            qualify_generated_function(
                function, body_names, scope);
        }
        for (auto& task : body.tasks) {
            qualify_generated_task(task, body_names, scope);
        }
    }
    for (auto& generic : body.generic_function_templates) {
        const auto local_name = generic.function.name;
        body_names[local_name] =
            generated_scope(scope, local_name);
        qualify_generated_function(
            generic.function, body_names, scope);
    }
    for (auto& generic : body.generic_procedure_templates) {
        const auto local_name = generic.procedure.name;
        body_names[local_name] =
            generated_scope(scope, local_name);
        qualify_generated_procedure(
            generic.procedure, body_names, scope);
    }
    const auto qualify_generic_instances =
        [&](auto& instances) {
          for (auto& instance : instances) {
              if (const auto found = body_names.find(
                      instance.template_name);
                  found != body_names.end()) {
                  instance.template_name = found->second;
              }
              const auto local_name = instance.name;
              instance.name = generated_scope(scope, local_name);
              body_names[local_name] = instance.name;
              for (auto& actual : instance.generic_map) {
                  qualify_generated_expression(
                      actual.value, body_names);
                  if (actual.type_value) {
                      qualify_generated_type(
                          *actual.type_value, body_names);
                  }
              }
          }
        };
    qualify_generic_instances(body.generic_function_instances);
    qualify_generic_instances(body.generic_procedure_instances);
    for (auto& instance : body.package_instances) {
        const auto local_name = instance.name;
        instance.name = body_names.at(local_name);
        for (auto& actual : instance.generic_map) {
            qualify_generated_expression(
                actual.value, body_names);
            if (actual.type_value) {
                qualify_generated_type(
                    *actual.type_value, body_names);
            }
        }
        unit.package_instances.push_back(std::move(instance));
    }
    for (auto& function : body.functions) {
        unit.functions.push_back(std::move(function));
    }
    for (auto& task : body.tasks) {
        unit.tasks.push_back(std::move(task));
    }
    for (auto& procedure : body.procedures) {
        unit.procedures.push_back(std::move(procedure));
    }
    for (auto& generic : body.generic_function_templates) {
        unit.generic_function_templates.push_back(
            std::move(generic));
    }
    for (auto& generic : body.generic_procedure_templates) {
        unit.generic_procedure_templates.push_back(
            std::move(generic));
    }
    for (auto& instance : body.generic_function_instances) {
        unit.generic_function_instances.push_back(
            std::move(instance));
    }
    for (auto& instance : body.generic_procedure_instances) {
        unit.generic_procedure_instances.push_back(
            std::move(instance));
    }
    for (auto& statement : body.concurrent_statements) {
        if (statement.label == "@vhdl-block-input-driver") {
            qualify_generated_expression(statement.target, body_names);
            statement.label.clear();
        } else {
            qualify_generated_statement(statement, body_names);
        }
        unit.concurrent_statements.push_back(std::move(statement));
    }
    for (auto& process : body.processes) {
        qualify_generated_process(process, body_names, scope);
        unit.processes.push_back(std::move(process));
    }
    for (auto& instance : body.instances) {
        qualify_generated_instance(instance, body_names, scope);
        unit.instances.push_back(std::move(instance));
    }
    expand_generate_regions(
        body.generate_regions,
        body_environment,
        body_domains,
        language,
        scope,
        body_names,
        unit,
        diagnostics,
        block_preparer);
}

std::optional<frontend::ValueDomain> vhdl_guard_domain(
    const frontend::Expression& expression,
    const ConstantDomainEnvironment& domains,
    const GeneratedNameEnvironment& visible_names,
    const DesignUnit& unit) {
    using frontend::ExpressionKind;
    using frontend::ValueDomain;
    if (expression.kind == ExpressionKind::BooleanLiteral) {
        return ValueDomain::Boolean;
    }
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        return ValueDomain::Integer;
    }
    if (expression.kind == ExpressionKind::StringLiteral) {
        return ValueDomain::String;
    }
    if (expression.kind == ExpressionKind::LogicLiteral) {
        return ValueDomain::Logic9;
    }
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto constant = domains.find(expression.text);
            constant != domains.end()) {
            return constant->second.domain;
        }
        const auto mapped = visible_names.find(expression.text);
        const auto name = mapped == visible_names.end()
            ? std::string_view{expression.text}
            : std::string_view{mapped->second};
        const auto find_domain = [&](const auto& declarations)
            -> std::optional<ValueDomain> {
          const auto found = std::ranges::find(
              declarations, name, &frontend::SignalDeclaration::name);
          return found == declarations.end()
              ? std::nullopt
              : std::optional{found->type.domain};
        };
        if (const auto domain = find_domain(unit.ports)) return domain;
        return find_domain(unit.signals);
    }
    if (expression.kind == ExpressionKind::Unary
        && expression.text == "not" && expression.operands.size() == 1) {
        return vhdl_guard_domain(
            expression.operands.front(), domains, visible_names, unit);
    }
    if (expression.kind == ExpressionKind::Binary) {
        constexpr std::array<std::string_view, 6> comparisons{
            "=", "/=", "<", "<=", ">", ">="};
        if (std::ranges::find(comparisons, expression.text)
            != comparisons.end()) {
            return ValueDomain::Boolean;
        }
        constexpr std::array<std::string_view, 6> logical{
            "and", "or", "nand", "nor", "xor", "xnor"};
        if (std::ranges::find(logical, expression.text) != logical.end()
            && !expression.operands.empty()) {
            return vhdl_guard_domain(
                expression.operands.front(), domains, visible_names, unit);
        }
    }
    return std::nullopt;
}

void expand_generate_regions(
    std::vector<frontend::GenerateRegion>& generates,
    const ConstantEnvironment& environment,
    const ConstantDomainEnvironment& domains,
    const frontend::Language language,
    const std::string_view parent_scope,
    const GeneratedNameEnvironment& visible_names,
    DesignUnit& unit,
    std::vector<Diagnostic>& diagnostics,
    const VhdlBlockInterfacePreparer* block_preparer) {
    for (auto& generate : generates) {
        if (generate.kind == frontend::GenerateKind::StaticBlock) {
            auto body = generate.then_body;
            const auto scope = generated_scope(
                parent_scope, generate.then_scope);
            auto body_visible_names = visible_names;
            if (language == frontend::Language::Vhdl2008
                && block_preparer != nullptr
                && !(*block_preparer)(
                    generate,
                    body,
                    environment,
                    domains,
                    scope,
                    body_visible_names,
                    unit,
                    diagnostics)) {
                continue;
            }
            if (language == frontend::Language::Vhdl2008
                && (!generate.block_generics.empty()
                    || !generate.block_generic_map.empty()
                    || !generate.block_ports.empty()
                    || !generate.block_port_map.empty())
                && !prepare_vhdl_block_interface(
                    generate,
                    body,
                    body_visible_names,
                    diagnostics)) {
                continue;
            }
            if (language == frontend::Language::Vhdl2008
                && generate.condition.valid()) {
                const auto domain = vhdl_guard_domain(
                    generate.condition, domains, visible_names, unit);
                if (domain && *domain != frontend::ValueDomain::Boolean) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-013",
                        "VHDL block guard expression must have Boolean "
                        "type",
                        generate.condition.span});
                    continue;
                }
                auto guard_expression = generate.condition;
                qualify_generated_expression(
                    guard_expression, visible_names);
                frontend::SignalDeclaration guard;
                guard.name = "guard";
                guard.type.spelling = "boolean";
                guard.type.domain = frontend::ValueDomain::Boolean;
                guard.span = generate.condition.span;
                body.signals.insert(
                    body.signals.begin(), std::move(guard));
                frontend::Statement driver;
                driver.kind = frontend::StatementKind::Assignment;
                driver.assignment_kind =
                    frontend::AssignmentKind::Continuous;
                driver.target = frontend::Expression{
                    frontend::ExpressionKind::Identifier,
                    "guard", {}, generate.condition.span};
                driver.value = std::move(guard_expression);
                driver.vhdl_delay_mechanism =
                    frontend::VhdlDelayMechanism::ImplicitInertial;
                driver.span = generate.condition.span;
                body.concurrent_statements.insert(
                    body.concurrent_statements.begin(),
                    std::move(driver));
            }
            append_generated_body(
                std::move(body),
                environment,
                domains,
                language,
                generated_scope(
                    parent_scope, generate.then_scope),
                body_visible_names,
                unit,
                diagnostics,
                block_preparer);
            continue;
        }
        if (generate.kind == frontend::GenerateKind::Selection) {
            std::string error;
            const auto selector = evaluate_constant_expression(
                generate.condition, environment, error);
            if (!selector) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-008",
                    "cannot evaluate selection-generate expression: "
                        + error,
                    generate.condition.span});
                continue;
            }
            const frontend::GenerateAlternative* selected = nullptr;
            const frontend::GenerateAlternative* default_alternative =
                nullptr;
            bool invalid = false;
            struct ChoiceInterval {
                std::int64_t lower;
                std::int64_t upper;
            };
            std::vector<ChoiceInterval> choice_intervals;
            for (const auto& alternative : generate.alternatives) {
                if (alternative.is_default) {
                    if (default_alternative != nullptr) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-010",
                            "selection generate has more than one "
                            "default alternative",
                            alternative.span});
                        invalid = true;
                    } else {
                        default_alternative = &alternative;
                    }
                    continue;
                }
                bool alternative_matches = false;
                for (const auto& choice : alternative.choices) {
                    error.clear();
                    const auto left = evaluate_constant_expression(
                        choice.left, environment, error);
                    if (!left) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-009",
                            "cannot evaluate selection-generate choice: "
                                + error,
                            choice.span});
                        invalid = true;
                        continue;
                    }
                    auto right = left;
                    if (choice.right) {
                        error.clear();
                        right = evaluate_constant_expression(
                            *choice.right, environment, error);
                        if (!right) {
                            diagnostics.push_back({
                                "FSIM-ELAB-GEN-009",
                                "cannot evaluate selection-generate range "
                                "bound: " + error,
                                choice.span});
                            invalid = true;
                            continue;
                        }
                    }
                    const bool empty_range =
                        choice.right
                        && (choice.descending
                                ? *left < *right
                                : *left > *right);
                    if (empty_range) {
                        continue;
                    }
                    const ChoiceInterval interval{
                        std::min(*left, *right),
                        std::max(*left, *right)};
                    const bool overlaps =
                        std::any_of(
                            choice_intervals.begin(),
                            choice_intervals.end(),
                            [&](const ChoiceInterval& existing) {
                                return interval.lower
                                           <= existing.upper
                                    && existing.lower
                                           <= interval.upper;
                            });
                    if (overlaps) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-010",
                            "selection generate has overlapping "
                            "constant choices or ranges",
                            choice.span});
                        invalid = true;
                    } else {
                        choice_intervals.push_back(interval);
                    }
                    alternative_matches =
                        alternative_matches
                        || (interval.lower <= *selector
                            && *selector <= interval.upper);
                }
                if (alternative_matches) {
                    if (selected != nullptr) {
                        diagnostics.push_back({
                            "FSIM-ELAB-GEN-010",
                            "selection generate has overlapping matching "
                            "alternatives",
                            alternative.span});
                        invalid = true;
                    } else {
                        selected = &alternative;
                    }
                }
            }
            if (invalid) {
                continue;
            }
            if (selected == nullptr) {
                selected = default_alternative;
            }
            if (selected != nullptr) {
                append_generated_body(
                    selected->body,
                    environment,
                    domains,
                    language,
                    generated_scope(
                        parent_scope, selected->scope),
                    visible_names,
                    unit,
                    diagnostics,
                    block_preparer);
            }
            continue;
        }
        if (generate.kind == frontend::GenerateKind::Iterative) {
            if (environment.contains(generate.variable)) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-007",
                    "nested loop-generate variable '"
                        + generate.variable
                        + "' shadows an enclosing constant; this "
                          "bounded slice requires a distinct name",
                    generate.span});
                continue;
            }
            std::string error;
            const auto initial = evaluate_constant_expression(
                generate.initial, environment, error);
            if (!initial) {
                diagnostics.push_back({
                    "FSIM-ELAB-GEN-002",
                    "cannot evaluate loop-generate initial value: "
                        + error,
                    generate.initial.span});
                continue;
            }
            auto iteration_environment = environment;
            auto iteration_domains = domains;
            iteration_domains[generate.variable] =
                frontend::ValueDomain::Integer;
            std::int64_t value = *initial;
            constexpr std::size_t maximum_iterations = 1'000'000;
            std::size_t count = 0;
            while (true) {
                iteration_environment[generate.variable] = value;
                error.clear();
                const auto condition = evaluate_constant_expression(
                    generate.condition,
                    iteration_environment,
                    error);
                if (!condition) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-003",
                        "cannot evaluate loop-generate condition: "
                            + error,
                        generate.condition.span});
                    break;
                }
                if (*condition == 0) {
                    break;
                }
                if (count++ == maximum_iterations) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-004",
                        "loop generate exceeds the bounded "
                        "1,000,000-iteration elaboration limit",
                        generate.span});
                    break;
                }
                auto body = generate.then_body;
                const auto indexed_scope =
                    generate.then_scope + "["
                    + std::to_string(value) + "]";
                append_generated_body(
                    std::move(body),
                    iteration_environment,
                    iteration_domains,
                    language,
                    generated_scope(parent_scope, indexed_scope),
                    visible_names,
                    unit,
                    diagnostics,
                    block_preparer);
                error.clear();
                const auto next = evaluate_constant_expression(
                    generate.iteration,
                    iteration_environment,
                    error);
                if (!next) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-005",
                        "cannot evaluate loop-generate iteration: "
                            + error,
                        generate.iteration.span});
                    break;
                }
                if (*next == value) {
                    diagnostics.push_back({
                        "FSIM-ELAB-GEN-006",
                        "loop-generate iteration does not advance",
                        generate.iteration.span});
                    break;
                }
                value = *next;
            }
            continue;
        }
        std::string error;
        const auto condition = evaluate_constant_expression(
            generate.condition, environment, error);
        if (!condition) {
            diagnostics.push_back({
                "FSIM-ELAB-GEN-001",
                "cannot evaluate conditional generate expression: "
                    + error,
                generate.condition.span});
            continue;
        }
        const bool selected_then = *condition != 0;
        const auto& selected_body =
            selected_then
            ? generate.then_body
            : generate.else_body;
        const auto& local_scope =
            selected_then
            ? generate.then_scope
            : generate.else_scope;
        append_generated_body(
            selected_body,
            environment,
            domains,
            language,
            generated_scope(parent_scope, local_scope),
            visible_names,
            unit,
            diagnostics,
            block_preparer);
    }
}

} // namespace fsim::elaboration::elaboration_detail
