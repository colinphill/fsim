// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <iomanip>
#include <sstream>

namespace fsim::elaboration::elaboration_detail {
namespace {

std::string source_spelling(const std::string_view bytes) {
    std::string result{"\""};
    for (const unsigned char byte : bytes) {
        switch (byte) {
        case '\n':
            result += "\\n";
            break;
        case '\t':
            result += "\\t";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '"':
            result += "\\\"";
            break;
        default:
            if (byte >= 0x20U && byte <= 0x7eU) {
                result.push_back(static_cast<char>(byte));
            } else {
                result.push_back('\\');
                result.push_back(
                    static_cast<char>('0' + ((byte >> 6U) & 7U)));
                result.push_back(
                    static_cast<char>('0' + ((byte >> 3U) & 7U)));
                result.push_back(
                    static_cast<char>('0' + (byte & 7U)));
            }
            break;
        }
    }
    result.push_back('"');
    return result;
}

bool string_literal_value(
    const Expression& expression,
    std::string& value) {
    if (expression.kind != ExpressionKind::StringLiteral) {
        return false;
    }
    if (expression.decoded_string) {
        value = *expression.decoded_string;
        return true;
    }
    return false;
}

void substitute_expression(
    Expression& expression,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment) {
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto found = environment.find(expression.text);
            found != environment.end()) {
            expression = found->second.expression(expression.span);
            return;
        }
    }
    for (auto& operand : expression.operands) {
        substitute_expression(
            operand, environment, integer_environment);
    }
    for (auto& association :
         expression.aggregate_choice_expressions) {
        for (auto& choice : association) {
            substitute_expression(
                choice, environment, integer_environment);
        }
    }
    if (expression.kind == ExpressionKind::Binary
        && (expression.text == "==" || expression.text == "!=")
        && expression.operands.size() == 2) {
        std::string left;
        std::string right;
        if (string_literal_value(expression.operands[0], left)
            && string_literal_value(expression.operands[1], right)) {
            const bool equal = left == right;
            expression = constant_expression(
                equal == (expression.text == "==") ? 1 : 0,
                expression.span,
                frontend::ValueDomain::Integer,
                frontend::Language::SystemVerilog2017);
            return;
        }
    }
    std::string error;
    if (const auto value =
            evaluate_systemverilog_string_expression(
                expression,
                environment,
                integer_environment,
                error)) {
        expression = value->expression(expression.span);
    }
}

void substitute_type(
    frontend::Type& type,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment) {
    const auto substitute_range =
        [&](auto& range) {
          if (range) {
              substitute_expression(
                  range->left, environment, integer_environment);
              substitute_expression(
                  range->right, environment, integer_environment);
          }
        };
    substitute_range(type.packed_range_expression);
    substitute_range(type.enumeration_range_expression);
    substitute_range(type.enumeration_base_range_expression);
    substitute_range(type.integer_range_expression);
    substitute_range(type.integer_base_range_expression);
    substitute_range(type.discrete_range_expression);
    for (auto& member : type.packed_members) {
        substitute_range(member.packed_range_expression);
    }
}

void substitute_statements(
    std::vector<Statement>& statements,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics);

void substitute_variable(
    frontend::VariableDeclaration& variable,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment) {
    substitute_type(
        variable.type, environment, integer_environment);
    if (variable.initializer) {
        substitute_expression(
            *variable.initializer,
            environment,
            integer_environment);
    }
}

void collapse_output(Statement& statement) {
    std::string bytes;
    if (statement.output_format
        && *statement.output_format == frontend::OutputFormat::String
        && string_literal_value(statement.value, bytes)) {
        statement.output_text =
            statement.output_prefix + bytes + statement.output_suffix;
        statement.output_format.reset();
        statement.value = {};
        return;
    }
    if (statement.output_values.size() != 1
        || !string_literal_value(
            statement.output_values.front().value, bytes)) {
        return;
    }
    const auto& output = statement.output_values.front();
    if (output.format != frontend::OutputFormat::Decimal
        && output.format != frontend::OutputFormat::String) {
        return;
    }
    statement.output_text =
        output.prefix + bytes + statement.output_trailing_text;
    statement.output_values.clear();
    statement.output_trailing_text.clear();
}

void substitute_statements(
    std::vector<Statement>& statements,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& statement : statements) {
        substitute_expression(
            statement.target, environment, integer_environment);
        substitute_expression(
            statement.value, environment, integer_environment);
        substitute_expression(
            statement.condition, environment, integer_environment);
        for (auto& argument : statement.task_arguments) {
            substitute_expression(
                argument, environment, integer_environment);
        }
        for (auto& association :
             statement.procedure_arguments) {
            substitute_expression(
                association.value,
                environment,
                integer_environment);
        }
        substitute_expression(
            statement.loop_initial, environment, integer_environment);
        substitute_expression(
            statement.loop_limit, environment, integer_environment);
        for (auto& element : statement.vhdl_waveform) {
            substitute_expression(
                element.value, environment, integer_environment);
        }
        for (auto& output : statement.output_values) {
            substitute_expression(
                output.value, environment, integer_environment);
        }
        if (statement.kind == StatementKind::Display) {
            collapse_output(statement);
        } else if (statement.kind == StatementKind::Report
                   && statement.value.valid()) {
            std::string bytes;
            if (string_literal_value(statement.value, bytes)) {
                statement.output_text = std::move(bytes);
                statement.value = {};
            } else {
                diagnostics.push_back({
                    "FSIM-ELAB-SVSTRING-003",
                    "report message is not a constant string expression",
                    statement.value.span});
            }
        }
        for (auto& declaration : statement.declarations) {
            substitute_variable(
                declaration, environment, integer_environment);
        }
        for (auto& alternative : statement.case_alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_expression(
                    choice, environment, integer_environment);
            }
            substitute_statements(
                alternative.statements,
                environment,
                integer_environment,
                diagnostics);
        }
        substitute_statements(
            statement.statements,
            environment,
            integer_environment,
            diagnostics);
        substitute_statements(
            statement.else_statements,
            environment,
            integer_environment,
            diagnostics);
    }
}

void substitute_body(
    frontend::GenerateBody& body,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics);

void substitute_regions(
    std::vector<frontend::GenerateRegion>& regions,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& region : regions) {
        substitute_expression(
            region.initial, environment, integer_environment);
        substitute_expression(
            region.condition, environment, integer_environment);
        substitute_expression(
            region.iteration, environment, integer_environment);
        substitute_body(
            region.then_body,
            environment,
            integer_environment,
            diagnostics);
        substitute_body(
            region.else_body,
            environment,
            integer_environment,
            diagnostics);
        for (auto& alternative : region.alternatives) {
            for (auto& choice : alternative.choices) {
                substitute_expression(
                    choice.left, environment, integer_environment);
                if (choice.right) {
                    substitute_expression(
                        *choice.right,
                        environment,
                        integer_environment);
                }
            }
            substitute_body(
                alternative.body,
                environment,
                integer_environment,
                diagnostics);
        }
    }
}

void substitute_body(
    frontend::GenerateBody& body,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& constant : body.constants) {
        substitute_type(
            constant.type, environment, integer_environment);
        substitute_expression(
            constant.default_value,
            environment,
            integer_environment);
    }
    for (auto& signal : body.signals) {
        substitute_type(
            signal.type, environment, integer_environment);
    }
    substitute_statements(
        body.concurrent_statements,
        environment,
        integer_environment,
        diagnostics);
    for (auto& process : body.processes) {
        for (auto& variable : process.variables) {
            substitute_variable(
                variable, environment, integer_environment);
        }
        substitute_statements(
            process.statements,
            environment,
            integer_environment,
            diagnostics);
    }
    for (auto& instance : body.instances) {
        for (auto& override : instance.parameter_overrides) {
            substitute_expression(
                override.value, environment, integer_environment);
        }
        for (auto& connection : instance.connections) {
            substitute_expression(
                connection.value, environment, integer_environment);
        }
    }
    substitute_regions(
        body.generate_regions,
        environment,
        integer_environment,
        diagnostics);
}

} // namespace

std::string SystemVerilogStringValue::display() const {
    return source_spelling(bytes);
}

std::string SystemVerilogStringValue::canonical() const {
    std::ostringstream output;
    output << "svstring-v1;bytes=" << bytes.size() << ";hex=";
    output << std::hex << std::setfill('0');
    for (const unsigned char byte : bytes) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

Expression SystemVerilogStringValue::expression(
    const frontend::SourceSpan& use_span) const {
    Expression result{
        ExpressionKind::StringLiteral,
        source_spelling(bytes),
        {},
        use_span};
    result.decoded_string = bytes;
    return result;
}

std::optional<SystemVerilogStringValue>
evaluate_systemverilog_string_expression(
    const Expression& expression,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::string& error) {
    if (expression.kind == ExpressionKind::StringLiteral) {
        if (!expression.decoded_string) {
            error =
                "string literal has no retained decoded byte value";
            return std::nullopt;
        }
        return SystemVerilogStringValue{
            *expression.decoded_string, expression.span};
    }
    if (expression.kind == ExpressionKind::Identifier) {
        const auto found = environment.find(expression.text);
        if (found == environment.end()) {
            error = "unknown string constant '" + expression.text + "'";
            return std::nullopt;
        }
        return found->second;
    }
    if (expression.kind == ExpressionKind::Concatenation) {
        if (expression.operands.empty()) {
            error = "string concatenation requires at least one operand";
            return std::nullopt;
        }
        SystemVerilogStringValue result;
        result.source = expression.span;
        for (const auto& operand : expression.operands) {
            const auto value =
                evaluate_systemverilog_string_expression(
                    operand, environment, integer_environment, error);
            if (!value) {
                error = "string concatenation operand: " + error;
                return std::nullopt;
            }
            result.bytes += value->bytes;
        }
        return result;
    }
    if (expression.kind == ExpressionKind::Call
        && expression.text == "?:"
        && expression.operands.size() == 3) {
        std::string condition_error;
        const auto condition =
            evaluate_systemverilog_constant_expression(
                expression.operands[0],
                {},
                integer_environment,
                condition_error);
        if (!condition) {
            error = "string conditional condition: " + condition_error;
            return std::nullopt;
        }
        const auto truth = condition->truth_value();
        if (!truth) {
            error = "string conditional condition contains X or Z";
            return std::nullopt;
        }
        return evaluate_systemverilog_string_expression(
            expression.operands[*truth ? 1U : 2U],
            environment,
            integer_environment,
            error);
    }
    error = "expression is not a supported constant string";
    return std::nullopt;
}

void substitute_systemverilog_strings(
    Expression& expression,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment) {
    substitute_expression(
        expression, environment, integer_environment);
}

void substitute_systemverilog_strings(
    DesignUnit& unit,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics) {
    for (auto& parameter : unit.parameters) {
        substitute_type(
            parameter.type, environment, integer_environment);
        substitute_expression(
            parameter.default_value,
            environment,
            integer_environment);
    }
    for (auto& alias : unit.type_aliases) {
        substitute_type(
            alias.type, environment, integer_environment);
        for (auto& literal : alias.enum_literals) {
            substitute_expression(
                literal.value, environment, integer_environment);
        }
    }
    for (auto& port : unit.ports) {
        substitute_type(port.type, environment, integer_environment);
    }
    for (auto& signal : unit.signals) {
        substitute_type(signal.type, environment, integer_environment);
    }
    substitute_statements(
        unit.concurrent_statements,
        environment,
        integer_environment,
        diagnostics);
    for (auto& process : unit.processes) {
        for (auto& variable : process.variables) {
            substitute_variable(
                variable, environment, integer_environment);
        }
        substitute_statements(
            process.statements,
            environment,
            integer_environment,
            diagnostics);
    }
    for (auto& instance : unit.instances) {
        for (auto& override : instance.parameter_overrides) {
            substitute_expression(
                override.value, environment, integer_environment);
        }
        for (auto& connection : instance.connections) {
            substitute_expression(
                connection.value, environment, integer_environment);
        }
    }
    substitute_regions(
        unit.generate_regions,
        environment,
        integer_environment,
        diagnostics);
}

void substitute_systemverilog_strings(
    frontend::GenerateBody& body,
    const SystemVerilogStringEnvironment& environment,
    const ConstantEnvironment& integer_environment,
    std::vector<Diagnostic>& diagnostics) {
    substitute_body(
        body, environment, integer_environment, diagnostics);
}

} // namespace fsim::elaboration::elaboration_detail
