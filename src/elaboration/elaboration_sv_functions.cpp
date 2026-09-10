// SPDX-License-Identifier: Apache-2.0
#include "elaboration_sv_function_evaluator_internal.hpp"

#include "fsim/support/sha256.hpp"

#include <set>

namespace fsim::elaboration::elaboration_detail {
thread_local CallCache* active_fold_call_cache { };
thread_local std::vector<std::unique_ptr<CallCache>>
    active_elaboration_call_caches;
thread_local std::vector<Diagnostic>*
    active_constant_function_diagnostics { };
thread_local std::vector<frontend::Diagnostic>*
    active_constant_function_messages { };

void append_key_component(
    std::string& key,
    const std::string_view component)
{
    key += std::to_string(component.size());
    key += ':';
    key += component;
}

void append_expression_behavior(
    std::string& identity,
    const frontend::Expression& expression,
    std::set<std::string>& identifiers)
{
    append_key_component(identity,
        std::to_string(static_cast<unsigned>(expression.kind)));
    append_key_component(identity, expression.text);
    append_key_component(identity, expression.nominal_type);
    if (expression.kind == frontend::ExpressionKind::Identifier
        || expression.kind == frontend::ExpressionKind::Call) {
        identifiers.insert(expression.text);
    }
    for (const auto& operand : expression.operands) {
        append_expression_behavior(identity, operand, identifiers);
    }
    for (const auto& choices :
        expression.aggregate_choice_expressions) {
        for (const auto& choice : choices) {
            append_expression_behavior(identity, choice, identifiers);
        }
    }
}

void append_statement_behavior(
    std::string& identity,
    const frontend::Statement& statement,
    std::set<std::string>& identifiers)
{
    append_key_component(identity,
        std::to_string(static_cast<unsigned>(statement.kind)));
    append_key_component(identity, statement.label);
    append_key_component(identity, statement.task_name);
    append_key_component(identity, statement.procedure_name);
    append_key_component(identity, statement.output_text);
    append_key_component(identity, statement.output_prefix);
    append_key_component(identity, statement.output_suffix);
    append_key_component(identity, statement.output_trailing_text);
    append_key_component(identity,
        std::to_string(static_cast<unsigned>(
            statement.assertion_severity)));
    append_key_component(identity,
        std::to_string(statement.output_suppress_leading_zero));
    append_key_component(identity,
        std::to_string(statement.output_minimum_width));
    append_key_component(identity,
        std::to_string(statement.output_left_justify));
    append_key_component(identity,
        std::to_string(statement.output_zero_pad));
    append_key_component(identity, statement.loop_variable);
    append_key_component(identity,
        std::to_string(statement.loop_descending));
    append_key_component(identity,
        std::to_string(statement.loop_limit_exclusive));
    append_expression_behavior(identity, statement.target, identifiers);
    append_expression_behavior(identity, statement.value, identifiers);
    append_expression_behavior(identity, statement.condition, identifiers);
    append_expression_behavior(
        identity, statement.loop_initial, identifiers);
    append_expression_behavior(
        identity, statement.loop_limit, identifiers);
    for (const auto& argument : statement.task_arguments) {
        append_expression_behavior(identity, argument, identifiers);
    }
    for (const auto& output : statement.output_values) {
        append_expression_behavior(identity, output.value, identifiers);
        append_key_component(identity,
            std::to_string(static_cast<unsigned>(output.format)));
        append_key_component(identity, output.prefix);
        append_key_component(identity,
            std::to_string(output.suppress_leading_zero));
        append_key_component(identity,
            std::to_string(output.minimum_width));
        append_key_component(identity,
            std::to_string(output.left_justify));
        append_key_component(identity,
            std::to_string(output.zero_pad));
    }
    for (const auto& association : statement.procedure_arguments) {
        append_expression_behavior(
            identity, association.value, identifiers);
    }
    for (const auto& declaration : statement.declarations) {
        append_key_component(identity,
            vhdl_type_identity(declaration.type));
        if (declaration.initializer) {
            append_expression_behavior(
                identity, *declaration.initializer, identifiers);
        }
    }
    for (const auto& alternative : statement.case_alternatives) {
        for (const auto& choice : alternative.choices) {
            append_expression_behavior(identity, choice, identifiers);
        }
        for (const auto& nested : alternative.statements) {
            append_statement_behavior(identity, nested, identifiers);
        }
    }
    for (const auto& nested : statement.statements) {
        append_statement_behavior(identity, nested, identifiers);
    }
    for (const auto& nested : statement.else_statements) {
        append_statement_behavior(identity, nested, identifiers);
    }
}

CallableBehaviorIdentity callable_behavior_identity(
    const frontend::FunctionDeclaration& function)
{
    std::string identity;
    std::set<std::string> identifiers;
    append_key_component(identity, function.name);
    append_key_component(identity, function.specialization_identity);
    append_key_component(identity,
        vhdl_type_identity(function.return_type));
    for (const auto& argument : function.arguments) {
        append_key_component(identity, argument.name);
        append_key_component(identity,
            vhdl_type_identity(argument.type));
        if (argument.default_value) {
            append_expression_behavior(
                identity, *argument.default_value, identifiers);
        }
    }
    for (const auto& constant : function.constants) {
        append_key_component(identity, constant.name);
        append_key_component(identity,
            vhdl_type_identity(constant.type));
        append_expression_behavior(
            identity, constant.default_value, identifiers);
    }
    for (const auto& variable : function.variables) {
        append_key_component(identity, variable.name);
        append_key_component(identity,
            vhdl_type_identity(variable.type));
        if (variable.initializer) {
            append_expression_behavior(
                identity, *variable.initializer, identifiers);
        }
    }
    for (const auto& statement : function.statements) {
        append_statement_behavior(identity, statement, identifiers);
    }
    for (const auto& nested : function.functions) {
        const auto nested_identity = callable_behavior_identity(nested);
        append_key_component(identity, nested_identity.digest);
        identifiers.insert(
            nested_identity.identifiers.begin(),
            nested_identity.identifiers.end());
    }
    const auto digest = support::Sha256::hex(
        support::Sha256::digest(identity));
    return { digest, std::move(identifiers) };
}

class FoldCallCacheScope final {
public:
    explicit FoldCallCacheScope(CallCache& cache)
        : previous_ { active_fold_call_cache }
    {
        active_fold_call_cache = &cache;
    }

    FoldCallCacheScope(const FoldCallCacheScope&) = delete;
    FoldCallCacheScope& operator=(const FoldCallCacheScope&) = delete;

    ~FoldCallCacheScope()
    {
        active_fold_call_cache = previous_;
    }

private:
    CallCache* previous_ { };
};

[[nodiscard]] bool callable_name_matches(
    const std::string_view declared,
    const std::string_view referenced,
    const frontend::Language language)
{
    if (declared == referenced) {
        return true;
    }
    const auto leaf_name = [](const std::string_view name) {
        const auto separator = name.find_last_of(".:");
        return separator == std::string_view::npos
            ? name
            : name.substr(separator + 1U);
    };
    return language != frontend::Language::Vhdl2008
        && leaf_name(declared) == leaf_name(referenced)
        && (declared.find_first_of(".[") != std::string_view::npos
            || referenced.find_first_of(".[") != std::string_view::npos);
}

void fold_expression(
    Expression& expression,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback)
{
    for (auto& operand : expression.operands) {
        fold_expression(
            operand, functions, environment, fallback);
    }
    for (auto& association :
        expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            fold_expression(
                choice, functions, environment, fallback);
        }
    }
    if (expression.kind != ExpressionKind::Call
        || std::ranges::none_of(
            functions,
            [&](const auto& function) {
                return (function.language == frontend::Language::Vhdl2008
                               ? function.name == expression.text
                               : callable_name_matches(
                                     function.name,
                                     expression.text,
                                     function.language))
                    && !function.return_type.systemverilog_container;
            })) {
        return;
    }
    std::string error;
    ConstantFunctionEvaluator evaluator {
        functions, environment, fallback
    };
    if (const auto value = evaluator.evaluate(expression, error)) {
        expression = value->expression(expression.span);
    }
}

void fold_type(
    frontend::Type& type,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback)
{
    const auto fold_range = [&](auto& range) {
        if (range) {
            fold_expression(
                range->left, functions, environment, fallback);
            fold_expression(
                range->right, functions, environment, fallback);
        }
    };
    fold_range(type.packed_range_expression);
    fold_range(type.integer_range_expression);
    fold_range(type.integer_base_range_expression);
    fold_range(type.discrete_range_expression);
    fold_range(type.enumeration_range_expression);
    fold_range(type.enumeration_base_range_expression);
    for (auto& member : type.packed_members) {
        if (!member.nested_types.empty()) {
            fold_type(
                member.nested_types.front(),
                functions,
                environment,
                fallback);
        }
        fold_range(member.packed_range_expression);
    }
}

void fold_statements(
    std::vector<Statement>& statements,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback)
{
    for (auto& statement : statements) {
        fold_expression(
            statement.target, functions, environment, fallback);
        fold_expression(
            statement.value, functions, environment, fallback);
        fold_expression(
            statement.condition, functions, environment, fallback);
        for (auto& argument : statement.task_arguments) {
            fold_expression(
                argument, functions, environment, fallback);
        }
        for (auto& association :
            statement.procedure_arguments) {
            fold_expression(
                association.value,
                functions,
                environment,
                fallback);
        }
        fold_expression(
            statement.loop_initial, functions, environment, fallback);
        fold_expression(
            statement.loop_limit, functions, environment, fallback);
        for (auto& declaration : statement.declarations) {
            fold_type(
                declaration.type,
                functions,
                environment,
                fallback);
            if (declaration.initializer) {
                fold_expression(
                    *declaration.initializer,
                    functions,
                    environment,
                    fallback);
            }
        }
        for (auto& alternative :
            statement.case_alternatives) {
            for (auto& choice : alternative.choices) {
                fold_expression(
                    choice, functions, environment, fallback);
            }
            fold_statements(
                alternative.statements,
                functions,
                environment,
                fallback);
        }
        fold_statements(
            statement.statements,
            functions,
            environment,
            fallback);
        fold_statements(
            statement.else_statements,
            functions,
            environment,
            fallback);
    }
}

void fold_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback);

void fold_generate_body(
    frontend::GenerateBody& body,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback)
{
    std::optional<std::vector<frontend::FunctionDeclaration>>
        owned_visible_functions;
    const auto* visible_functions = &functions;
    std::set<std::string> local_function_names;
    for (const auto& function : body.functions) {
        if (function.language != frontend::Language::Vhdl2008) {
            local_function_names.insert(function.name);
        }
    }
    if (!local_function_names.empty()) {
        owned_visible_functions = functions;
        std::erase_if(
            *owned_visible_functions,
            [&](const auto& function) {
                return std::ranges::any_of(
                    local_function_names,
                    [&](const auto& local_name) {
                        return callable_name_matches(
                            function.name, local_name, function.language);
                    });
            });
        for (const auto& function : body.functions) {
            if (function.language != frontend::Language::Vhdl2008) {
                owned_visible_functions->push_back(function);
            }
        }
        visible_functions = &*owned_visible_functions;
    }
    for (auto& alias : body.type_aliases) {
        fold_type(alias.type, *visible_functions, environment, fallback);
    }
    for (auto& constant : body.constants) {
        fold_type(
            constant.type, *visible_functions, environment, fallback);
        fold_expression(
            constant.default_value,
            *visible_functions,
            environment,
            fallback);
    }
    for (auto& signal : body.signals) {
        fold_type(
            signal.type, *visible_functions, environment, fallback);
    }
    for (auto& function : body.functions) {
        fold_type(
            function.return_type,
            *visible_functions,
            environment,
            fallback);
        for (auto& argument : function.arguments) {
            fold_type(
                argument.type, *visible_functions, environment, fallback);
            if (argument.default_value) {
                fold_expression(
                    *argument.default_value,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        for (auto& variable : function.variables) {
            fold_type(
                variable.type, *visible_functions, environment, fallback);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        fold_statements(
            function.statements,
            *visible_functions,
            environment,
            fallback);
    }
    for (auto& task : body.tasks) {
        for (auto& argument : task.arguments) {
            fold_type(
                argument.type, *visible_functions, environment, fallback);
            if (argument.default_value) {
                fold_expression(
                    *argument.default_value,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        for (auto& variable : task.variables) {
            fold_type(
                variable.type, *visible_functions, environment, fallback);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        fold_statements(
            task.statements,
            *visible_functions,
            environment,
            fallback);
    }
    fold_statements(
        body.concurrent_statements,
        *visible_functions,
        environment,
        fallback);
    for (auto& process : body.processes) {
        for (auto& variable : process.variables) {
            fold_type(
                variable.type, *visible_functions, environment, fallback);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    *visible_functions,
                    environment,
                    fallback);
            }
        }
        fold_statements(
            process.statements,
            *visible_functions,
            environment,
            fallback);
    }
    for (auto& instance : body.instances) {
        for (auto& override : instance.parameter_overrides) {
            fold_expression(
                override.value,
                *visible_functions,
                environment,
                fallback);
        }
        for (auto& connection : instance.connections) {
            fold_expression(
                connection.value,
                *visible_functions,
                environment,
                fallback);
        }
    }
    for (auto& declaration : body.verilog_defparams) {
        for (auto& segment : declaration.path) {
            for (auto& index : segment.indices) {
                fold_expression(
                    index, *visible_functions, environment, fallback);
            }
        }
        fold_expression(
            declaration.value,
            *visible_functions,
            environment,
            fallback);
    }
    fold_generate_regions(
        body.generate_regions, *visible_functions, environment, fallback);
}

void fold_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback)
{
    for (auto& region : regions) {
        fold_expression(
            region.initial, functions, environment, fallback);
        fold_expression(
            region.condition, functions, environment, fallback);
        fold_expression(
            region.iteration, functions, environment, fallback);
        if (region.kind == frontend::GenerateKind::Conditional) {
            std::string error;
            ConstantFunctionEvaluator evaluator {
                functions, environment, fallback
            };
            if (const auto condition = evaluator.evaluate(
                    region.condition, error)) {
                if (const auto truth = condition->truth_value()) {
                    fold_generate_body(
                        *truth ? region.then_body : region.else_body,
                        functions,
                        environment,
                        fallback);
                    continue;
                }
            }
        }
        fold_generate_body(
            region.then_body, functions, environment, fallback);
        fold_generate_body(
            region.else_body, functions, environment, fallback);
        for (auto& alternative : region.alternatives) {
            for (auto& choice : alternative.choices) {
                fold_expression(
                    choice.left, functions, environment, fallback);
                if (choice.right) {
                    fold_expression(
                        *choice.right,
                        functions,
                        environment,
                        fallback);
                }
            }
            fold_generate_body(
                alternative.body,
                functions,
                environment,
                fallback);
        }
    }
}

ConstantFunctionMemoizationScope::ConstantFunctionMemoizationScope(
    std::vector<Diagnostic>& diagnostics,
    std::vector<frontend::Diagnostic>& messages)
    : previous_diagnostics_ { active_constant_function_diagnostics }
    , previous_messages_ { active_constant_function_messages }
{
    active_elaboration_call_caches.push_back(
        std::make_unique<CallCache>());
    active_constant_function_diagnostics = &diagnostics;
    active_constant_function_messages = &messages;
}

ConstantFunctionMemoizationScope::~ConstantFunctionMemoizationScope()
{
    active_elaboration_call_caches.pop_back();
    active_constant_function_diagnostics = previous_diagnostics_;
    active_constant_function_messages = previous_messages_;
}

std::optional<SystemVerilogConstantValue>
evaluate_systemverilog_constant_function_expression(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    const std::vector<frontend::FunctionDeclaration>& functions,
    std::string& error)
{
    ConstantFunctionEvaluator evaluator {
        functions, environment, fallback_environment
    };
    return evaluator.evaluate(expression, error);
}

bool evaluate_systemverilog_elaboration_report(
    const Statement& statement,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    const std::vector<frontend::FunctionDeclaration>& functions,
    std::string& error)
{
    ConstantFunctionEvaluator evaluator {
        functions, environment, fallback_environment
    };
    return evaluator.evaluate_elaboration_report(statement, error);
}

void fold_systemverilog_constant_functions(
    frontend::GenerateBody& body,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment)
{
    CallCache call_cache;
    const FoldCallCacheScope cache_scope { call_cache };
    fold_generate_body(
        body, functions, environment, fallback_environment);
}

void fold_systemverilog_constant_functions(
    DesignUnit& unit,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::vector<Diagnostic>&)
{
    CallCache call_cache;
    const FoldCallCacheScope cache_scope { call_cache };
    const auto& functions = unit.functions;
    for (auto& parameter : unit.parameters) {
        fold_type(
            parameter.type,
            functions,
            environment,
            fallback_environment);
        fold_expression(
            parameter.default_value,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& alias : unit.type_aliases) {
        fold_type(
            alias.type,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& port : unit.ports) {
        fold_type(
            port.type,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& signal : unit.signals) {
        fold_type(
            signal.type,
            functions,
            environment,
            fallback_environment);
    }
    // This pass supplies constant-required elaboration contexts only.  Do not
    // rewrite executable initializers, statements, or port expressions merely
    // because a pure call happens to have literal actuals: doing so removes
    // observable call safe points and can erase static callable side effects.
    for (auto& function : unit.functions) {
        fold_type(
            function.return_type,
            functions,
            environment,
            fallback_environment);
        for (auto& argument : function.arguments) {
            fold_type(
                argument.type,
                functions,
                environment,
                fallback_environment);
        }
        for (auto& variable : function.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
        }
    }
    for (auto& task : unit.tasks) {
        for (auto& argument : task.arguments) {
            fold_type(
                argument.type,
                functions,
                environment,
                fallback_environment);
        }
        for (auto& variable : task.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
        }
    }
    for (auto& procedure : unit.procedures) {
        for (auto& argument : procedure.arguments) {
            fold_type(
                argument.type,
                functions,
                environment,
                fallback_environment);
        }
        for (auto& variable : procedure.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
        }
    }
    for (auto& process : unit.processes) {
        for (auto& variable : process.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
        }
    }
    for (auto& instance : unit.instances) {
        for (auto& override : instance.parameter_overrides) {
            fold_expression(
                override.value,
                functions,
                environment,
                fallback_environment);
        }
    }
    for (auto& declaration : unit.verilog_defparams) {
        for (auto& segment : declaration.path) {
            for (auto& index : segment.indices) {
                fold_expression(
                    index,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_expression(
            declaration.value,
            functions,
            environment,
            fallback_environment);
    }
    fold_generate_regions(
        unit.generate_regions,
        functions,
        environment,
        fallback_environment);
}

} // namespace fsim::elaboration::elaboration_detail
