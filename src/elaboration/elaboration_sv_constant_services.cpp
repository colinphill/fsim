// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <cctype>
#include <sstream>

namespace fsim::elaboration::elaboration_detail {

std::string SystemVerilogConstantValue::display() const {
    if (known()) {
        if (const auto integer = integer_value()) {
            return std::to_string(*integer);
        }
        if (width <= 64U && !is_signed) {
            return std::to_string(bits & mask());
        }
    }
    return expression(source).text;
}

std::string SystemVerilogConstantValue::canonical() const {
    std::ostringstream stream;
    stream << "svconst-v2:w=" << width
           << ":s=" << (is_signed ? 1 : 0)
           << ":u=" << (unsized ? 1 : 0)
           << ":d=" << static_cast<unsigned>(domain)
           << ":n=" << nominal_type.size() << ':' << nominal_type
           << ":v=" << packed.to_msb_string();
    return stream.str();
}

Expression SystemVerilogConstantValue::expression(
    const frontend::SourceSpan& use_span) const {
    if (unsized && known() && is_signed) {
        if (const auto value = integer_value()) {
            return constant_expression(
                *value,
                use_span,
                frontend::ValueDomain::Integer,
                frontend::Language::SystemVerilog2017,
                false,
                nominal_type);
        }
    }
    auto digits = packed.to_msb_string();
    for (auto& digit : digits) {
        digit = static_cast<char>(
            std::tolower(static_cast<unsigned char>(digit)));
    }
    return {
        ExpressionKind::LogicLiteral,
        std::to_string(width)
            + (is_signed ? "'sb" : "'b")
            + digits,
        {},
        use_span,
        {},
        {},
        nominal_type};
}


namespace {

void substitute_sv_delay_parameters(
    frontend::Delay& delay,
    const SystemVerilogConstantEnvironment& environment) {
    if (delay.expression) {
        substitute_systemverilog_parameters(
            *delay.expression, environment);
    }
    const auto substitute_alternative =
        [&](frontend::DelayAlternative& alternative) {
            if (alternative.expression) {
                substitute_systemverilog_parameters(
                    *alternative.expression, environment);
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
    for (auto& additional : delay.additional_values) {
        substitute_sv_delay_parameters(additional, environment);
    }
}

void substitute_sv_type(
    frontend::Type& type,
    const SystemVerilogConstantEnvironment& environment) {
    const auto substitute_bound =
        [&](Expression& expression) {
          if (expression.kind == ExpressionKind::Identifier) {
              if (const auto found = environment.find(expression.text);
                  found != environment.end()) {
                  if (const auto integer = found->second.integer_value()) {
                      expression = constant_expression(
                          *integer,
                          expression.span,
                          frontend::ValueDomain::Integer,
                          frontend::Language::SystemVerilog2017);
                      return;
                  }
              }
          }
          substitute_systemverilog_parameters(expression, environment);
        };
    const auto substitute_range =
        [&](auto& range) {
          if (range) {
              substitute_bound(range->left);
              substitute_bound(range->right);
          }
        };
    substitute_range(type.packed_range_expression);
    substitute_range(type.enumeration_range_expression);
    substitute_range(type.enumeration_base_range_expression);
    substitute_range(type.integer_range_expression);
    substitute_range(type.integer_base_range_expression);
    substitute_range(type.discrete_range_expression);
    for (auto& value : type.systemverilog_enumeration_values) {
        substitute_systemverilog_parameters(value, environment);
    }
    if (type.systemverilog_container) {
        if (type.systemverilog_container->queue_maximum) {
            substitute_systemverilog_parameters(
                *type.systemverilog_container->queue_maximum,
                environment);
        }
        for (auto& range :
             type.systemverilog_container
                 ->static_range_expressions) {
            substitute_systemverilog_parameters(
                range.left, environment);
            substitute_systemverilog_parameters(
                range.right, environment);
        }
        if (type.systemverilog_container
                ->associative_index_type) {
            substitute_sv_type(
                *type.systemverilog_container
                     ->associative_index_type,
                environment);
        }
    }
    for (auto& member : type.packed_members) {
        if (member.initializer) {
            substitute_systemverilog_parameters(
                *member.initializer, environment);
        }
        if (!member.nested_types.empty()) {
            substitute_sv_type(
                member.nested_types.front(), environment);
        }
        substitute_range(member.packed_range_expression);
    }
}

void substitute_sv_variable(
    frontend::VariableDeclaration& declaration,
    const SystemVerilogConstantEnvironment& environment) {
    substitute_sv_type(declaration.type, environment);
    if (declaration.initializer) {
        substitute_systemverilog_parameters(
            *declaration.initializer, environment);
    }
}

void substitute_sv_statements(
    std::vector<Statement>& statements,
    const SystemVerilogConstantEnvironment& environment);

void substitute_sv_function(
    frontend::FunctionDeclaration& function,
    const SystemVerilogConstantEnvironment& environment) {
    substitute_sv_type(function.return_type, environment);
    for (auto& argument : function.arguments) {
        substitute_sv_type(argument.type, environment);
        if (argument.default_value) {
            substitute_systemverilog_parameters(
                *argument.default_value, environment);
        }
    }
    for (auto& variable : function.variables) {
        substitute_sv_variable(variable, environment);
    }
    substitute_sv_statements(function.statements, environment);
}

void substitute_sv_task(
    frontend::TaskDeclaration& task,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& argument : task.arguments) {
        substitute_sv_type(argument.type, environment);
        if (argument.default_value) {
            substitute_systemverilog_parameters(
                *argument.default_value, environment);
        }
    }
    for (auto& variable : task.variables) {
        substitute_sv_variable(variable, environment);
    }
    substitute_sv_statements(task.statements, environment);
}

void substitute_sv_statements(
    std::vector<Statement>& statements,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& statement : statements) {
        if (statement.delay) {
            substitute_sv_delay_parameters(
                *statement.delay, environment);
        }
        substitute_systemverilog_parameters(
            statement.target, environment);
        substitute_systemverilog_parameters(
            statement.value, environment);
        substitute_systemverilog_parameters(
            statement.condition, environment);
        for (auto& argument : statement.task_arguments) {
            substitute_systemverilog_parameters(
                argument, environment);
        }
        for (auto& association :
             statement.procedure_arguments) {
            substitute_systemverilog_parameters(
                association.value, environment);
        }
        substitute_systemverilog_parameters(
            statement.loop_initial, environment);
        substitute_systemverilog_parameters(
            statement.loop_limit, environment);
        substitute_systemverilog_parameters(
            statement.loop_update_target, environment);
        for (auto& element : statement.vhdl_waveform) {
            substitute_systemverilog_parameters(
                element.value, environment);
        }
        for (auto& output : statement.output_values) {
            substitute_systemverilog_parameters(
                output.value, environment);
        }
        for (auto& declaration : statement.declarations) {
            substitute_sv_variable(declaration, environment);
        }
        for (auto& alternative : statement.case_alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_systemverilog_parameters(
                    choice, environment);
            }
            substitute_sv_statements(
                alternative.statements, environment);
        }
        auto body_environment = environment;
        if (statement.kind == StatementKind::Loop) {
            body_environment.erase(statement.loop_variable);
        }
        substitute_sv_statements(
            statement.statements, body_environment);
        substitute_sv_statements(
            statement.else_statements, environment);
    }
}

void substitute_sv_instances(
    std::vector<frontend::Instance>& instances,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& instance : instances) {
        for (auto& override : instance.parameter_overrides) {
            substitute_systemverilog_parameters(
                override.value, environment);
        }
        for (auto& connection : instance.connections) {
            substitute_systemverilog_parameters(
                connection.value, environment);
        }
    }
}

void substitute_sv_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogConstantEnvironment& environment);

void substitute_sv_generate_body(
    frontend::GenerateBody& body,
    const SystemVerilogConstantEnvironment& environment) {
    auto body_environment = environment;
    for (auto& alias : body.type_aliases) {
        substitute_sv_type(alias.type, body_environment);
    }
    for (auto& constant : body.constants) {
        substitute_sv_type(constant.type, body_environment);
        substitute_systemverilog_parameters(
            constant.default_value, body_environment);
    }
    for (auto& signal : body.signals) {
        substitute_sv_type(signal.type, body_environment);
        if (signal.net_delay) {
            substitute_sv_delay_parameters(
                *signal.net_delay, body_environment);
        }
        if (signal.charge_decay) {
            substitute_sv_delay_parameters(
                *signal.charge_decay, body_environment);
        }
    }
    for (auto& function : body.functions) {
        substitute_sv_function(function, body_environment);
    }
    for (auto& task : body.tasks) {
        substitute_sv_task(task, body_environment);
    }
    substitute_sv_statements(
        body.concurrent_statements, body_environment);
    for (auto& process : body.processes) {
        for (auto& variable : process.variables) {
            substitute_sv_variable(variable, body_environment);
        }
        substitute_sv_statements(
            process.statements, body_environment);
    }
    substitute_sv_instances(body.instances, body_environment);
    for (auto& declaration : body.verilog_defparams) {
        for (auto& segment : declaration.path) {
            for (auto& index : segment.indices) {
                substitute_systemverilog_parameters(
                    index, body_environment);
            }
        }
        substitute_systemverilog_parameters(
            declaration.value, body_environment);
    }
    substitute_sv_generate_regions(
        body.generate_regions, body_environment);
}

void substitute_sv_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& region : regions) {
        substitute_systemverilog_parameters(
            region.initial, environment);
        auto body_environment = environment;
        if (region.kind == frontend::GenerateKind::Iterative) {
            body_environment.erase(region.variable);
        }
        substitute_systemverilog_parameters(
            region.condition, body_environment);
        substitute_systemverilog_parameters(
            region.iteration, body_environment);
        substitute_sv_generate_body(
            region.then_body, body_environment);
        substitute_sv_generate_body(
            region.else_body, body_environment);
        for (auto& alternative : region.alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_systemverilog_parameters(
                    choice.left, body_environment);
                if (choice.right) {
                    substitute_systemverilog_parameters(
                        *choice.right, body_environment);
                }
            }
            substitute_sv_generate_body(
                alternative.body, body_environment);
        }
    }
}

} // namespace

void substitute_systemverilog_parameters(
    Expression& expression,
    const SystemVerilogConstantEnvironment& environment) {
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto found = environment.find(expression.text);
            found != environment.end()) {
            expression = found->second.expression(expression.span);
            return;
        }
    }
    for (auto& association :
         expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            substitute_systemverilog_parameters(choice, environment);
        }
    }
    for (auto& operand : expression.operands) {
        substitute_systemverilog_parameters(operand, environment);
    }
}

void substitute_systemverilog_parameters(
    DesignUnit& unit,
    const SystemVerilogConstantEnvironment& environment) {
    for (auto& parameter : unit.parameters) {
        substitute_sv_type(parameter.type, environment);
        substitute_systemverilog_parameters(
            parameter.default_value, environment);
    }
    for (auto& alias : unit.type_aliases) {
        substitute_sv_type(alias.type, environment);
        for (auto& literal : alias.enum_literals) {
            substitute_systemverilog_parameters(
                literal.value, environment);
        }
    }
    for (auto& port : unit.ports) {
        substitute_sv_type(port.type, environment);
        if (port.net_delay) {
            substitute_sv_delay_parameters(
                *port.net_delay, environment);
        }
        if (port.charge_decay) {
            substitute_sv_delay_parameters(
                *port.charge_decay, environment);
        }
    }
    for (auto& signal : unit.signals) {
        substitute_sv_type(signal.type, environment);
        if (signal.net_delay) {
            substitute_sv_delay_parameters(
                *signal.net_delay, environment);
        }
        if (signal.charge_decay) {
            substitute_sv_delay_parameters(
                *signal.charge_decay, environment);
        }
    }
    for (auto& variable : unit.variables) {
        substitute_sv_variable(variable, environment);
    }
    for (auto& function : unit.functions) {
        substitute_sv_function(function, environment);
    }
    for (auto& task : unit.tasks) {
        substitute_sv_task(task, environment);
    }
    substitute_sv_statements(
        unit.concurrent_statements, environment);
    for (auto& process : unit.processes) {
        for (auto& variable : process.variables) {
            substitute_sv_variable(variable, environment);
        }
        substitute_sv_statements(
            process.statements, environment);
    }
    substitute_sv_instances(unit.instances, environment);
    for (auto& declaration : unit.verilog_defparams) {
        for (auto& segment : declaration.path) {
            for (auto& index : segment.indices) {
                substitute_systemverilog_parameters(index, environment);
            }
        }
        substitute_systemverilog_parameters(
            declaration.value, environment);
    }
    substitute_sv_generate_regions(
        unit.generate_regions, environment);
    for (auto& block : unit.verilog_specify_blocks) {
        for (auto& declaration : block.specparams) {
            substitute_systemverilog_parameters(
                declaration.value, environment);
            if (declaration.minimum) {
                substitute_systemverilog_parameters(
                    *declaration.minimum, environment);
            }
            if (declaration.typical) {
                substitute_systemverilog_parameters(
                    *declaration.typical, environment);
            }
            if (declaration.maximum) {
                substitute_systemverilog_parameters(
                    *declaration.maximum, environment);
            }
            if (declaration.path_pulse_error_limit) {
                substitute_systemverilog_parameters(
                    *declaration.path_pulse_error_limit, environment);
            }
        }
        for (auto& path : block.module_paths) {
            for (auto& source : path.sources) {
                substitute_systemverilog_parameters(source, environment);
            }
            for (auto& destination : path.destinations) {
                substitute_systemverilog_parameters(
                    destination, environment);
            }
            substitute_systemverilog_parameters(
                path.destination_data_source, environment);
            substitute_systemverilog_parameters(
                path.condition, environment);
            for (auto& delay : path.delays) {
                substitute_sv_delay_parameters(delay, environment);
            }
        }
        for (auto& pulse : block.pulse_declarations) {
            for (auto& terminal : pulse.terminals) {
                substitute_systemverilog_parameters(
                    terminal, environment);
            }
        }
        for (auto& check : block.timing_checks) {
            const auto substitute_event = [&](auto& event) {
                substitute_systemverilog_parameters(
                    event.expression, environment);
                substitute_systemverilog_parameters(
                    event.condition, environment);
            };
            substitute_event(check.reference_event);
            substitute_event(check.data_event);
            for (auto& limit : check.limits) {
                substitute_systemverilog_parameters(limit, environment);
            }
            for (auto& limit : check.normalized_limits) {
                substitute_sv_delay_parameters(limit, environment);
            }
            substitute_systemverilog_parameters(
                check.threshold, environment);
            if (check.normalized_threshold) {
                substitute_sv_delay_parameters(
                    *check.normalized_threshold, environment);
            }
            substitute_systemverilog_parameters(
                check.notifier, environment);
            substitute_systemverilog_parameters(
                check.timestamp_condition, environment);
            substitute_systemverilog_parameters(
                check.timecheck_condition, environment);
            substitute_systemverilog_parameters(
                check.delayed_reference, environment);
            substitute_systemverilog_parameters(
                check.delayed_data, environment);
            substitute_systemverilog_parameters(
                check.event_based_flag, environment);
            substitute_systemverilog_parameters(
                check.remain_active_flag, environment);
        }
    }
}

void substitute_systemverilog_parameters(
    frontend::GenerateBody& body,
    const SystemVerilogConstantEnvironment& environment) {
    substitute_sv_generate_body(body, environment);
}

} // namespace fsim::elaboration::elaboration_detail
