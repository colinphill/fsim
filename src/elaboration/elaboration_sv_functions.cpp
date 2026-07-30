// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration::elaboration_detail {
namespace {

using Value = SystemVerilogConstantValue;

enum class Flow {
    normal,
    returned,
    broken,
    continued,
    failed,
};

class ConstantFunctionEvaluator final {
public:
    ConstantFunctionEvaluator(
        const std::vector<frontend::FunctionDeclaration>& functions,
        const SystemVerilogConstantEnvironment& globals,
        const ConstantEnvironment& fallback)
        : functions_(functions),
          globals_(globals),
          fallback_(fallback) {}

    std::optional<Value> evaluate(
        const Expression& expression,
        std::string& error) {
        return evaluate_expression(expression, globals_, error);
    }

private:
    const frontend::FunctionDeclaration* find_function(
        const std::string_view name) const {
        const auto found = std::ranges::find(
            functions_, name,
            &frontend::FunctionDeclaration::name);
        return found == functions_.end() ? nullptr : &*found;
    }

    std::optional<Value> evaluate_expression(
        const Expression& expression,
        const SystemVerilogConstantEnvironment& environment,
        std::string& error) {
        if (expression.kind == ExpressionKind::Call) {
            if (const auto* function =
                    find_function(expression.text)) {
                return evaluate_call(
                    *function, expression, environment, error);
            }
        }
        auto folded = expression;
        for (auto& operand : folded.operands) {
            const auto value =
                evaluate_expression(operand, environment, error);
            if (!value) {
                return std::nullopt;
            }
            operand = value->expression(operand.span);
        }
        for (auto& association :
             folded.aggregate_choice_expressions) {
            for (auto& choice : association) {
                const auto value =
                    evaluate_expression(
                        choice, environment, error);
                if (!value) {
                    return std::nullopt;
                }
                choice = value->expression(choice.span);
            }
        }
        return evaluate_systemverilog_constant_expression(
            folded, environment, fallback_, error);
    }

    std::optional<Value> converted(
        const Expression& expression,
        const frontend::Type& type,
        const SystemVerilogConstantEnvironment& environment,
        std::string& error) {
        const auto value =
            evaluate_expression(expression, environment, error);
        if (!value) {
            return std::nullopt;
        }
        return convert_systemverilog_parameter_value(
            *value, type, error);
    }

    static std::string result_alias(
        const std::string_view name) {
        const auto separator = name.rfind("::");
        return separator == std::string_view::npos
            ? std::string{name}
            : std::string{name.substr(separator + 2)};
    }

    std::optional<Value> evaluate_call(
        const frontend::FunctionDeclaration& function,
        const Expression& call,
        const SystemVerilogConstantEnvironment& caller,
        std::string& error) {
        if (call.operands.size() != function.arguments.size()) {
            error =
                "constant function '" + function.name + "' expects "
                + std::to_string(function.arguments.size())
                + " arguments";
            return std::nullopt;
        }
        if (std::ranges::find(call_stack_, &function)
            != call_stack_.end()) {
            error =
                "recursive constant function call involving '"
                + function.name + "'";
            return std::nullopt;
        }
        call_stack_.push_back(&function);
        struct Pop {
            std::vector<const frontend::FunctionDeclaration*>& stack;
            ~Pop() { stack.pop_back(); }
        } pop{call_stack_};

        auto environment = globals_;
        for (std::size_t index = 0;
             index < function.arguments.size(); ++index) {
            auto value = converted(
                call.operands[index],
                function.arguments[index].type,
                caller,
                error);
            if (!value) {
                error =
                    "constant function argument '"
                    + function.arguments[index].name + "': "
                    + error;
                return std::nullopt;
            }
            environment.insert_or_assign(
                function.arguments[index].name,
                std::move(*value));
        }

        std::unordered_map<std::string, const frontend::Type*> types;
        types.emplace(function.name, &function.return_type);
        types.emplace(
            result_alias(function.name), &function.return_type);
        for (const auto& argument : function.arguments) {
            types.emplace(argument.name, &argument.type);
        }
        const auto collect_declarations =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> void {
              for (const auto& statement : statements) {
                  for (const auto& declaration :
                       statement.declarations) {
                      types.insert_or_assign(
                          declaration.name, &declaration.type);
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
            types.insert_or_assign(
                variable.name, &variable.type);
        }
        collect_declarations(
            collect_declarations, function.statements);

        const auto initialize =
            [&](const frontend::VariableDeclaration& variable)
                -> bool {
              Value value{
                  0,
                  0,
                  0,
                  static_cast<std::uint32_t>(
                      variable.type.width().value_or(32)),
                  variable.type.is_signed,
                  false,
                  variable.span};
              if (variable.initializer) {
                  const auto initial = converted(
                      *variable.initializer,
                      variable.type,
                      environment,
                      error);
                  if (!initial) {
                      return false;
                  }
                  value = *initial;
              }
              environment.insert_or_assign(
                  variable.name, std::move(value));
              return true;
            };
        for (const auto& variable : function.variables) {
            if (!initialize(variable)) {
                return std::nullopt;
            }
        }
        const auto initialize_nested =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> bool {
              for (const auto& statement : statements) {
                  for (const auto& declaration :
                       statement.declarations) {
                      if (!initialize(declaration)) {
                          return false;
                      }
                  }
                  if (!self(self, statement.statements)
                      || !self(self, statement.else_statements)) {
                      return false;
                  }
                  for (const auto& alternative :
                       statement.case_alternatives) {
                      if (!self(self, alternative.statements)) {
                          return false;
                      }
                  }
              }
              return true;
            };
        if (!initialize_nested(
                initialize_nested, function.statements)) {
            return std::nullopt;
        }

        std::optional<Value> result;
        const auto flow = execute_statements(
            function.statements,
            environment,
            types,
            result,
            error);
        if (flow == Flow::failed) {
            return std::nullopt;
        }
        if (!result) {
            const auto named = environment.find(function.name);
            const auto alias =
                environment.find(result_alias(function.name));
            if (named != environment.end()) {
                result = named->second;
            } else if (alias != environment.end()) {
                result = alias->second;
            }
        }
        if (!result) {
            error =
                "constant function '" + function.name
                + "' did not assign a result";
            return std::nullopt;
        }
        return convert_systemverilog_parameter_value(
            *result, function.return_type, error);
    }

    Flow execute_statements(
        const std::vector<Statement>& statements,
        SystemVerilogConstantEnvironment& environment,
        const std::unordered_map<
            std::string, const frontend::Type*>& types,
        std::optional<Value>& result,
        std::string& error) {
        for (const auto& statement : statements) {
            const auto flow = execute_statement(
                statement, environment, types, result, error);
            if (flow != Flow::normal) {
                return flow;
            }
        }
        return Flow::normal;
    }

    Flow execute_statement(
        const Statement& statement,
        SystemVerilogConstantEnvironment& environment,
        const std::unordered_map<
            std::string, const frontend::Type*>& types,
        std::optional<Value>& result,
        std::string& error) {
        if (statement.kind == StatementKind::Assignment) {
            if (statement.target.kind
                != ExpressionKind::Identifier) {
                error =
                    "constant functions currently require whole-variable "
                    "assignment targets";
                return Flow::failed;
            }
            const auto type = types.find(statement.target.text);
            if (type == types.end()) {
                error =
                    "constant function assignment target '"
                    + statement.target.text + "' is not local";
                return Flow::failed;
            }
            const auto value = converted(
                statement.value, *type->second, environment, error);
            if (!value) {
                return Flow::failed;
            }
            environment.insert_or_assign(
                statement.target.text, *value);
            if (statement.target.text
                    == call_stack_.back()->name
                || statement.target.text
                    == result_alias(call_stack_.back()->name)) {
                result = *value;
            }
            return Flow::normal;
        }
        if (statement.kind == StatementKind::Return) {
            const auto value = converted(
                statement.value,
                call_stack_.back()->return_type,
                environment,
                error);
            if (!value) {
                return Flow::failed;
            }
            result = *value;
            return Flow::returned;
        }
        if (statement.kind == StatementKind::Block) {
            return execute_statements(
                statement.statements,
                environment,
                types,
                result,
                error);
        }
        if (statement.kind == StatementKind::If) {
            const auto condition = evaluate_expression(
                statement.condition, environment, error);
            if (!condition) {
                return Flow::failed;
            }
            const auto truth = condition->truth_value();
            if (!truth) {
                error =
                    "constant function condition contains X or Z";
                return Flow::failed;
            }
            return execute_statements(
                *truth
                    ? statement.statements
                    : statement.else_statements,
                environment,
                types,
                result,
                error);
        }
        if (statement.kind == StatementKind::Case) {
            const auto selector = evaluate_expression(
                statement.condition, environment, error);
            if (!selector) {
                return Flow::failed;
            }
            const frontend::CaseAlternative* selected = nullptr;
            const frontend::CaseAlternative* fallback = nullptr;
            for (const auto& alternative :
                 statement.case_alternatives) {
                if (alternative.is_default) {
                    fallback = &alternative;
                    continue;
                }
                for (const auto& choice : alternative.choices) {
                    const auto value = evaluate_expression(
                        choice, environment, error);
                    if (!value) {
                        return Flow::failed;
                    }
                    if (value->width == selector->width
                        && value->bits == selector->bits
                        && value->unknown_bits
                            == selector->unknown_bits
                        && value->high_impedance_bits
                            == selector->high_impedance_bits) {
                        selected = &alternative;
                        break;
                    }
                }
                if (selected != nullptr) {
                    break;
                }
            }
            selected = selected != nullptr ? selected : fallback;
            return selected == nullptr
                ? Flow::normal
                : execute_statements(
                      selected->statements,
                      environment,
                      types,
                      result,
                      error);
        }
        if (statement.kind == StatementKind::Loop) {
            constexpr std::size_t maximum_iterations = 1'000'000;
            std::size_t iteration = 0;
            if (statement.loop_runtime) {
                for (;;) {
                    if (!statement.loop_post_test) {
                        const auto condition = evaluate_expression(
                            statement.condition,
                            environment,
                            error);
                        if (!condition) {
                            return Flow::failed;
                        }
                        const auto truth = condition->truth_value();
                        if (!truth) {
                            error =
                                "constant function loop condition "
                                "contains X or Z";
                            return Flow::failed;
                        }
                        if (!*truth) {
                            return Flow::normal;
                        }
                    }
                    if (iteration++ == maximum_iterations) {
                        error =
                            "constant function loop exceeds 1,000,000 "
                            "iterations";
                        return Flow::failed;
                    }
                    const auto flow = execute_statements(
                        statement.statements,
                        environment,
                        types,
                        result,
                        error);
                    if (flow == Flow::returned
                        || flow == Flow::failed) {
                        return flow;
                    }
                    if (flow == Flow::broken) {
                        return Flow::normal;
                    }
                    if (statement.loop_post_test) {
                        const auto condition = evaluate_expression(
                            statement.condition,
                            environment,
                            error);
                        if (!condition) {
                            return Flow::failed;
                        }
                        const auto truth = condition->truth_value();
                        if (!truth || !*truth) {
                            return truth
                                ? Flow::normal
                                : Flow::failed;
                        }
                    }
                }
            }
            const auto initial = evaluate_expression(
                statement.loop_initial, environment, error);
            const auto limit = initial
                ? evaluate_expression(
                      statement.loop_limit, environment, error)
                : std::nullopt;
            if (!initial || !limit
                || !initial->integer_value()
                || !limit->integer_value()) {
                if (error.empty()) {
                    error =
                        "constant function loop bounds must be known "
                        "integers";
                }
                return Flow::failed;
            }
            auto value = *initial->integer_value();
            const auto final = *limit->integer_value();
            const auto in_range = [&]() {
                return statement.loop_descending
                    ? (statement.loop_limit_exclusive
                           ? value > final : value >= final)
                    : (statement.loop_limit_exclusive
                           ? value < final : value <= final);
            };
            while (in_range()) {
                if (iteration++ == maximum_iterations) {
                    error =
                        "constant function loop exceeds 1,000,000 "
                        "iterations";
                    return Flow::failed;
                }
                if (!statement.loop_variable.empty()) {
                    environment.insert_or_assign(
                        statement.loop_variable,
                        Value{
                            static_cast<std::uint64_t>(value),
                            0,
                            0,
                            32,
                            true,
                            false,
                            statement.span});
                }
                const auto flow = execute_statements(
                    statement.statements,
                    environment,
                    types,
                    result,
                    error);
                if (flow == Flow::returned
                    || flow == Flow::failed) {
                    return flow;
                }
                if (flow == Flow::broken) {
                    return Flow::normal;
                }
                value += statement.loop_descending ? -1 : 1;
            }
            return Flow::normal;
        }
        if (statement.kind == StatementKind::Break) {
            return Flow::broken;
        }
        if (statement.kind == StatementKind::Continue) {
            return Flow::continued;
        }
        if (statement.kind == StatementKind::Null) {
            return Flow::normal;
        }
        error =
            "statement is not permitted in a constant function";
        return Flow::failed;
    }

    const std::vector<frontend::FunctionDeclaration>& functions_;
    const SystemVerilogConstantEnvironment& globals_;
    const ConstantEnvironment& fallback_;
    std::vector<const frontend::FunctionDeclaration*> call_stack_;
};

void fold_expression(
    Expression& expression,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
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
                return function.name == expression.text;
            })) {
        return;
    }
    std::string error;
    ConstantFunctionEvaluator evaluator{
        functions, environment, fallback};
    if (const auto value = evaluator.evaluate(expression, error)) {
        expression = value->expression(expression.span);
    }
}

void fold_type(
    frontend::Type& type,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
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
        fold_range(member.packed_range_expression);
    }
}

void fold_statements(
    std::vector<Statement>& statements,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
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
    const ConstantEnvironment& fallback) {
    for (auto& constant : body.constants) {
        fold_type(
            constant.type, functions, environment, fallback);
        fold_expression(
            constant.default_value,
            functions,
            environment,
            fallback);
    }
    for (auto& signal : body.signals) {
        fold_type(
            signal.type, functions, environment, fallback);
    }
    fold_statements(
        body.concurrent_statements,
        functions,
        environment,
        fallback);
    for (auto& process : body.processes) {
        for (auto& variable : process.variables) {
            fold_type(
                variable.type, functions, environment, fallback);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback);
            }
        }
        fold_statements(
            process.statements,
            functions,
            environment,
            fallback);
    }
    for (auto& instance : body.instances) {
        for (auto& override : instance.parameter_overrides) {
            fold_expression(
                override.value, functions, environment, fallback);
        }
        for (auto& connection : instance.connections) {
            fold_expression(
                connection.value, functions, environment, fallback);
        }
    }
    fold_generate_regions(
        body.generate_regions, functions, environment, fallback);
}

void fold_generate_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const std::vector<frontend::FunctionDeclaration>& functions,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback) {
    for (auto& region : regions) {
        fold_expression(
            region.initial, functions, environment, fallback);
        fold_expression(
            region.condition, functions, environment, fallback);
        fold_expression(
            region.iteration, functions, environment, fallback);
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

} // namespace

std::optional<SystemVerilogConstantValue>
evaluate_systemverilog_constant_function_expression(
    const Expression& expression,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    const std::vector<frontend::FunctionDeclaration>& functions,
    std::string& error) {
    ConstantFunctionEvaluator evaluator{
        functions, environment, fallback_environment};
    return evaluator.evaluate(expression, error);
}

void fold_systemverilog_constant_functions(
    DesignUnit& unit,
    const SystemVerilogConstantEnvironment& environment,
    const ConstantEnvironment& fallback_environment,
    std::vector<Diagnostic>&) {
    const auto functions = unit.functions;
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
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_statements(
            function.statements,
            functions,
            environment,
            fallback_environment);
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
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_statements(
            task.statements,
            functions,
            environment,
            fallback_environment);
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
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_statements(
            procedure.statements,
            functions,
            environment,
            fallback_environment);
    }
    fold_statements(
        unit.concurrent_statements,
        functions,
        environment,
        fallback_environment);
    for (auto& process : unit.processes) {
        for (auto& variable : process.variables) {
            fold_type(
                variable.type,
                functions,
                environment,
                fallback_environment);
            if (variable.initializer) {
                fold_expression(
                    *variable.initializer,
                    functions,
                    environment,
                    fallback_environment);
            }
        }
        fold_statements(
            process.statements,
            functions,
            environment,
            fallback_environment);
    }
    for (auto& instance : unit.instances) {
        for (auto& override : instance.parameter_overrides) {
            fold_expression(
                override.value,
                functions,
                environment,
                fallback_environment);
        }
        for (auto& connection : instance.connections) {
            fold_expression(
                connection.value,
                functions,
                environment,
                fallback_environment);
        }
    }
    fold_generate_regions(
        unit.generate_regions,
        functions,
        environment,
        fallback_environment);
}

} // namespace fsim::elaboration::elaboration_detail
