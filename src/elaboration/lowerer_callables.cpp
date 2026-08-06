// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

#include <unordered_set>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

std::optional<std::vector<const Expression*>>
Lowerer::bind_function_actuals(
    const Expression& expression,
    const frontend::FunctionDeclaration& function) {
    std::vector<const Expression*> bound(function.arguments.size());
    const bool have_names = !expression.call_argument_names.empty();
    if (have_names
        && expression.call_argument_names.size()
            != expression.operands.size()) {
        report(
            "FSIM-ELAB-SVFUNC-010",
            "function call association metadata is inconsistent",
            expression.span);
        return std::nullopt;
    }
    bool named_seen = false;
    std::size_t positional = 0;
    for (std::size_t index = 0;
         index < expression.operands.size(); ++index) {
        const auto& name = have_names
            ? expression.call_argument_names[index]
            : std::string{};
        std::size_t formal_index = 0;
        if (name.empty()) {
            if (named_seen || positional >= bound.size()) {
                report(
                    "FSIM-ELAB-SVFUNC-010",
                    named_seen
                        ? "positional function argument follows a named argument"
                        : "too many positional function arguments",
                    expression.span);
                return std::nullopt;
            }
            formal_index = positional++;
        } else {
            named_seen = true;
            const auto found = std::ranges::find(
                function.arguments,
                name,
                &frontend::FunctionArgument::name);
            if (found == function.arguments.end()) {
                report(
                    "FSIM-ELAB-SVFUNC-010",
                    "unknown named function argument '" + name + "'",
                    expression.span);
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(function.arguments.begin(), found));
        }
        if (bound[formal_index] != nullptr) {
            report(
                "FSIM-ELAB-SVFUNC-010",
                "function argument '"
                    + function.arguments[formal_index].name
                    + "' is associated more than once",
                expression.span);
            return std::nullopt;
        }
        if (expression.operands[index].valid()) {
            bound[formal_index] = &expression.operands[index];
        }
    }
    for (std::size_t index = 0; index < bound.size(); ++index) {
        if (bound[index] == nullptr
            && function.arguments[index].default_value) {
            bound[index] = &*function.arguments[index].default_value;
        }
        if (bound[index] == nullptr) {
            report(
                "FSIM-ELAB-SVFUNC-010",
                "function argument '" + function.arguments[index].name
                    + "' has no actual or default value",
                expression.span);
            return std::nullopt;
        }
    }
    return bound;
}

bool Lowerer::validate_function_reference_actuals(
    const frontend::FunctionDeclaration& function,
    const std::vector<const Expression*>& actuals) {
    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
        if (!function.arguments[index].reference) {
            continue;
        }
        const auto& actual = *actuals[index];
        const bool direct_local =
            actual.kind == ExpressionKind::Identifier
            && (locals_.contains(actual.text)
                || string_locals_.contains(actual.text)
                || container_locals_.contains(actual.text));
        if (!function.automatic || !direct_local) {
            report(
                "FSIM-ELAB-SVFUNC-012",
                "ref function arguments require an automatic function and "
                "a direct caller-local variable actual",
                actual.span);
            return false;
        }
    }
    return true;
}

std::optional<std::vector<const Expression*>>
Lowerer::bind_task_actuals(
    const Statement& statement,
    const frontend::TaskDeclaration& task) {
    std::vector<const Expression*> bound(task.arguments.size());
    const bool have_names = !statement.task_argument_names.empty();
    if (have_names
        && statement.task_argument_names.size()
            != statement.task_arguments.size()) {
        report(
            "FSIM-ELAB-SVTASK-012",
            "task call association metadata is inconsistent",
            statement.span);
        return std::nullopt;
    }
    bool named_seen = false;
    std::size_t positional = 0;
    for (std::size_t index = 0;
         index < statement.task_arguments.size(); ++index) {
        const auto& name = have_names
            ? statement.task_argument_names[index]
            : std::string{};
        std::size_t formal_index = 0;
        if (name.empty()) {
            if (named_seen || positional >= bound.size()) {
                report(
                    "FSIM-ELAB-SVTASK-012",
                    named_seen
                        ? "positional task argument follows a named argument"
                        : "too many positional task arguments",
                    statement.span);
                return std::nullopt;
            }
            formal_index = positional++;
        } else {
            named_seen = true;
            const auto found = std::ranges::find(
                task.arguments,
                name,
                &frontend::TaskArgument::name);
            if (found == task.arguments.end()) {
                report(
                    "FSIM-ELAB-SVTASK-012",
                    "unknown named task argument '" + name + "'",
                    statement.span);
                return std::nullopt;
            }
            formal_index = static_cast<std::size_t>(
                std::distance(task.arguments.begin(), found));
        }
        if (bound[formal_index] != nullptr) {
            report(
                "FSIM-ELAB-SVTASK-012",
                "task argument '" + task.arguments[formal_index].name
                    + "' is associated more than once",
                statement.span);
            return std::nullopt;
        }
        if (statement.task_arguments[index].valid()) {
            bound[formal_index] = &statement.task_arguments[index];
        }
    }
    for (std::size_t index = 0; index < bound.size(); ++index) {
        if (bound[index] == nullptr
            && task.arguments[index].default_value) {
            bound[index] = &*task.arguments[index].default_value;
        }
        if (bound[index] == nullptr) {
            report(
                "FSIM-ELAB-SVTASK-012",
                "task argument '" + task.arguments[index].name
                    + "' has no actual or default value",
                statement.span);
            return std::nullopt;
        }
    }
    return bound;
}

void Lowerer::lower_callable_copy_out(
    const Expression& target,
    const frontend::Type& type,
    const RegisterId value,
    const StringRegisterId string_value,
    const ContainerRegisterId container_value,
    const bool is_string,
    const bool is_container,
    std::string temporary) {
    if (is_container) {
        container_locals_.insert_or_assign(temporary, container_value);
    } else if (is_string) {
        string_locals_.insert_or_assign(temporary, string_value);
    } else {
        locals_.insert_or_assign(temporary, value);
        local_signed_.insert_or_assign(temporary, type.is_signed);
        local_ranges_.insert_or_assign(temporary, type.packed_range);
        local_integer_ranges_.insert_or_assign(
            temporary, type.integer_range);
        local_members_.insert_or_assign(temporary, type.packed_members);
    }
    local_types_.insert_or_assign(temporary, &type);
    Statement copy_out;
    copy_out.kind = StatementKind::Assignment;
    copy_out.assignment_kind = AssignmentKind::Blocking;
    copy_out.target = target;
    copy_out.value = Expression{
        ExpressionKind::Identifier,
        std::move(temporary),
        {},
        target.span};
    copy_out.span = target.span;
    lower_assignment(copy_out);
}

std::vector<Lowerer::CallableVariableRegister>
Lowerer::allocate_static_callable_variables(
    const std::vector<frontend::VariableDeclaration>& variables,
    const std::string_view diagnostic_code,
    const std::string_view callable_kind) {
    std::vector<CallableVariableRegister> registers;
    registers.reserve(variables.size());
    for (const auto& variable : variables) {
        CallableVariableRegister storage;
        if (variable.type.domain == frontend::ValueDomain::String) {
            storage.is_string = true;
            storage.string = allocate_string_register();
            const auto initial = variable.initializer
                ? lower_string_expression(*variable.initializer)
                : std::optional<StringRegisterId>{};
            if (initial) {
                process_.operations.emplace_back(
                    CopyStringRegister{storage.string, *initial});
            } else {
                process_.operations.emplace_back(
                    LoadStringConstant{storage.string, {}});
            }
            registers.push_back(storage);
            continue;
        }
        const auto width = variable.type.width();
        if (variable.type.systemverilog_container
            || !width || *width == 0) {
            report(
                std::string{diagnostic_code},
                "static or implicit-lifetime "
                    + std::string{callable_kind}
                    + " locals require a bounded packed or string type",
                variable.span);
            storage.packed = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant{
                storage.packed, unsigned_value(0, 1)});
            registers.push_back(storage);
            continue;
        }
        storage.packed = allocate_register(
            static_cast<std::size_t>(*width),
            variable.type.domain);
        process_.operations.emplace_back(LoadConstant{
            storage.packed,
            default_packed_value(variable.type, *width)});
        if (variable.initializer) {
            if (!validate_sv_nominal_assignment(
                    &variable.type, *variable.initializer)) {
                registers.push_back(storage);
                continue;
            }
            auto value = lower_expression(
                *variable.initializer,
                static_cast<std::size_t>(*width),
                &variable.type);
            if (value) {
                if (register_width(*value) != *width) {
                    *value = resize_register(
                        *value,
                        static_cast<std::size_t>(*width),
                        is_signed_expression(*variable.initializer));
                }
                process_.operations.emplace_back(
                    CopyRegister{storage.packed, *value});
            }
        }
        registers.push_back(storage);
    }
    return registers;
}

void Lowerer::bind_static_callable_variables(
    const std::vector<frontend::VariableDeclaration>& variables,
    const std::vector<CallableVariableRegister>& registers) {
    for (std::size_t index = 0; index < variables.size(); ++index) {
        const auto& variable = variables[index];
        const auto& storage = registers[index];
        if (storage.is_string) {
            string_locals_.insert_or_assign(variable.name, storage.string);
            local_types_.insert_or_assign(variable.name, &variable.type);
            process_.debug_string_locals.push_back(DebugStringLocal{
                scoped_local_name(variable.name), storage.string,
                SourceLocation{
                    variable.span.source_name,
                    static_cast<std::uint32_t>(variable.span.begin.line),
                    static_cast<std::uint32_t>(variable.span.begin.column)}});
            continue;
        }
        const auto register_id = storage.packed;
        locals_.insert_or_assign(variable.name, register_id);
        local_signed_.insert_or_assign(
            variable.name, variable.type.is_signed);
        local_ranges_.insert_or_assign(
            variable.name, variable.type.packed_range);
        local_integer_ranges_.insert_or_assign(
            variable.name, variable.type.integer_range);
        local_members_.insert_or_assign(
            variable.name, variable.type.packed_members);
        local_types_.insert_or_assign(variable.name, &variable.type);
        auto debug_name = scoped_local_name(variable.name);
        if (!debug_local_names_.emplace(debug_name).second) {
            debug_name += "@" + std::to_string(variable.span.begin.line)
                + ":" + std::to_string(variable.span.begin.column);
            debug_local_names_.emplace(debug_name);
        }
        process_.debug_locals.push_back(DebugLocal{
            std::move(debug_name),
            variable.type.spelling,
            register_id,
            static_cast<std::size_t>(variable.type.width().value_or(1)),
            SourceLocation{
                variable.span.source_name,
                static_cast<std::uint32_t>(variable.span.begin.line),
                static_cast<std::uint32_t>(variable.span.begin.column)},
            {},
            {},
            value_kind(variable.type.domain),
            variable.type.enumeration_literals,
            variable.type.systemverilog_scalar});
        if (variable.type.integer_range) {
            const auto [lower, upper] =
                integer_bounds(variable.type.integer_range);
            process_.debug_locals.back().integer_lower = lower;
            process_.debug_locals.back().integer_upper = upper;
        }
    }
}

}  // namespace fsim::elaboration
