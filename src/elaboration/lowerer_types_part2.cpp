// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

[[nodiscard]] bool Lowerer::is_file_handle_expression(
    const Expression& expression) const
{
    if (expression.kind == ExpressionKind::IntegerLiteral) {
        return true;
    }
    const auto* type = expression.kind == ExpressionKind::Identifier
        ? object_type(expression.text)
        : systemverilog_expression_type(expression);
    const auto width = type == nullptr
        ? std::optional<std::size_t> { }
        : type->width();
    return type != nullptr && width && *width == 32U
        && type->systemverilog_scalar
        == frontend::SystemVerilogScalarKind::None
        && (type->domain == frontend::ValueDomain::Bit2
            || type->domain == frontend::ValueDomain::Logic4
            || type->domain == frontend::ValueDomain::Integer);
}

void Lowerer::collect_identifiers(
    const Expression& expression,
    std::set<std::string>& output) const
{
    const bool implicit_signal_attribute = language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call
        && (expression.text == "'transaction"
            || expression.text == "'delayed"
            || ((expression.text == "'stable"
                    || expression.text == "'quiet")
                && expression.operands.size() == 2));
    if (expression.kind == ExpressionKind::Identifier) {
        if (const auto selected = packed_member_reference(expression.text)) {
            output.insert(selected->base);
        } else {
            output.insert(expression.text);
        }
    } else if (
        language_ == frontend::Language::Vhdl2008
        && expression.kind == ExpressionKind::Call
        && !expression.operands.empty()) {
        if (const auto selected = packed_member_reference(expression.text)) {
            output.insert(selected->base);
        } else if (
            signals_.contains(expression.text)
            || locals_.contains(expression.text)) {
            output.insert(expression.text);
        }
    }
    if (!implicit_signal_attribute) {
        for (const auto& operand : expression.operands) {
            collect_identifiers(operand, output);
        }
    }
}

void Lowerer::collect_statement_identifiers(
    const std::span<const Statement> statements,
    std::set<std::string>& output) const
{
    for (const auto& statement : statements) {
        collect_identifiers(statement.vhdl_guard, output);
        switch (statement.kind) {
        case StatementKind::Assignment:
            for (const auto* target = &statement.target;
                (target->kind == ExpressionKind::Index
                    || target->kind == ExpressionKind::Slice)
                && !target->operands.empty();
                target = &target->operands.front()) {
                for (std::size_t index = 1;
                    index < target->operands.size(); ++index) {
                    collect_identifiers(
                        target->operands[index], output);
                }
            }
            if (statement.vhdl_waveform.empty()) {
                collect_identifiers(statement.value, output);
            } else {
                for (const auto& element :
                    statement.vhdl_waveform) {
                    collect_identifiers(element.value, output);
                }
            }
            break;
        case StatementKind::Force:
        case StatementKind::ProceduralAssign:
            collect_identifiers(statement.value, output);
            collect_identifiers(statement.target, output);
            break;
        case StatementKind::Release:
        case StatementKind::Deassign:
            collect_identifiers(statement.target, output);
            break;
        case StatementKind::If:
        case StatementKind::Assert:
        case StatementKind::WaitUntil:
            collect_identifiers(statement.condition, output);
            break;
        case StatementKind::Return:
            collect_identifiers(statement.value, output);
            break;
        case StatementKind::TaskCall:
            for (const auto& argument :
                statement.task_arguments) {
                collect_identifiers(argument, output);
            }
            break;
        case StatementKind::ContainerMethod:
            collect_identifiers(statement.value, output);
            break;
        case StatementKind::ProcedureCall:
            for (const auto& association :
                statement.procedure_arguments) {
                collect_identifiers(
                    association.value, output);
            }
            break;
        case StatementKind::Case:
            collect_identifiers(statement.condition, output);
            for (const auto& alternative :
                statement.case_alternatives) {
                for (const auto& choice : alternative.choices) {
                    collect_identifiers(choice, output);
                }
            }
            break;
        case StatementKind::Loop:
            if (statement.loop_runtime) {
                collect_identifiers(
                    statement.loop_initial, output);
                collect_identifiers(
                    statement.condition, output);
                collect_identifiers(
                    statement.loop_update_target, output);
            } else {
                collect_identifiers(
                    statement.loop_initial, output);
                collect_identifiers(
                    statement.loop_limit, output);
            }
            break;
        case StatementKind::Break:
        case StatementKind::Continue:
        case StatementKind::Delay:
        case StatementKind::WaitOn:
        case StatementKind::Display:
            if (statement.output_format) {
                collect_identifiers(statement.value, output);
            }
            for (const auto& value : statement.output_values) {
                collect_identifiers(value.value, output);
            }
            break;
        case StatementKind::FileClose:
        case StatementKind::FileFlush:
            collect_identifiers(
                statement.file_handle, output);
            break;
        case StatementKind::FileDisplay:
            collect_identifiers(
                statement.file_handle, output);
            if (statement.output_format) {
                collect_identifiers(statement.value, output);
            }
            for (const auto& value : statement.output_values) {
                collect_identifiers(value.value, output);
            }
            break;
        case StatementKind::MemoryLoad:
            collect_identifiers(statement.value, output);
            collect_identifiers(statement.target, output);
            for (const auto& argument :
                statement.task_arguments) {
                collect_identifiers(argument, output);
            }
            break;
        case StatementKind::MonitorControl:
            break;
        case StatementKind::WaitOrder:
        case StatementKind::EventTrigger:
        case StatementKind::Report:
        case StatementKind::Pause:
        case StatementKind::Finish:
        case StatementKind::Exit:
        case StatementKind::Fork:
        case StatementKind::WaitFork:
        case StatementKind::DisableFork:
        case StatementKind::Disable:
        case StatementKind::Block:
        case StatementKind::Null:
            break;
        }
        collect_statement_identifiers(
            statement.statements, output);
        collect_statement_identifiers(
            statement.else_statements, output);
        collect_statement_identifiers(
            statement.loop_updates, output);
        for (const auto& alternative :
            statement.case_alternatives) {
            collect_statement_identifiers(
                alternative.statements, output);
        }
    }
}

void Lowerer::collect_wildcard_identifiers(
    const std::span<const Statement> statements,
    std::set<std::string>& output) const
{
    std::deque<std::string> pending_functions;
    std::deque<std::string> pending_tasks;
    std::unordered_set<std::string> visited_functions;
    std::unordered_set<std::string> visited_tasks;

    const auto collect_expression_calls =
        [&](const auto& self, const Expression& expression) -> void {
        if (expression.kind == ExpressionKind::Call
            && function_indices_.contains(expression.text)) {
            pending_functions.push_back(expression.text);
        }
        for (const auto& operand : expression.operands) {
            self(self, operand);
        }
    };
    const auto collect_statement_calls =
        [&](const auto& self,
            const std::span<const Statement> body) -> void {
        for (const auto& statement : body) {
            collect_expression_calls(
                collect_expression_calls, statement.target);
            collect_expression_calls(
                collect_expression_calls, statement.value);
            collect_expression_calls(
                collect_expression_calls, statement.condition);
            collect_expression_calls(
                collect_expression_calls, statement.vhdl_guard);
            collect_expression_calls(
                collect_expression_calls, statement.loop_initial);
            collect_expression_calls(
                collect_expression_calls, statement.loop_limit);
            collect_expression_calls(
                collect_expression_calls,
                statement.loop_update_target);
            collect_expression_calls(
                collect_expression_calls, statement.file_handle);
            for (const auto& argument : statement.task_arguments) {
                collect_expression_calls(
                    collect_expression_calls, argument);
            }
            for (const auto& argument : statement.procedure_arguments) {
                collect_expression_calls(
                    collect_expression_calls, argument.value);
            }
            for (const auto& value : statement.output_values) {
                collect_expression_calls(
                    collect_expression_calls, value.value);
            }
            for (const auto& alternative : statement.case_alternatives) {
                for (const auto& choice : alternative.choices) {
                    collect_expression_calls(
                        collect_expression_calls, choice);
                }
                self(self, alternative.statements);
            }
            if (statement.kind == StatementKind::TaskCall
                && task_indices_.contains(statement.task_name)) {
                pending_tasks.push_back(statement.task_name);
            }
            self(self, statement.statements);
            self(self, statement.else_statements);
            self(self, statement.loop_updates);
        }
    };

    collect_statement_identifiers(statements, output);
    collect_statement_calls(collect_statement_calls, statements);
    while (!pending_functions.empty() || !pending_tasks.empty()) {
        if (!pending_functions.empty()) {
            auto name = std::move(pending_functions.front());
            pending_functions.pop_front();
            if (!visited_functions.insert(name).second) {
                continue;
            }
            const auto found = function_indices_.find(name);
            if (found == function_indices_.end()) {
                continue;
            }
            for (const auto index : found->second) {
                const auto& function = *function_frames_[index].source;
                std::set<std::string> dependencies;
                collect_statement_identifiers(
                    function.statements, dependencies);
                collect_statement_calls(
                    collect_statement_calls,
                    function.statements);
                for (const auto& argument : function.arguments) {
                    dependencies.erase(argument.name);
                    if (argument.default_value) {
                        collect_identifiers(
                            *argument.default_value,
                            dependencies);
                        collect_expression_calls(
                            collect_expression_calls,
                            *argument.default_value);
                    }
                }
                for (const auto& variable : function.variables) {
                    dependencies.erase(variable.name);
                    if (variable.initializer) {
                        collect_identifiers(
                            *variable.initializer,
                            dependencies);
                        collect_expression_calls(
                            collect_expression_calls,
                            *variable.initializer);
                    }
                }
                output.insert(
                    dependencies.begin(), dependencies.end());
            }
            continue;
        }

        auto name = std::move(pending_tasks.front());
        pending_tasks.pop_front();
        if (!visited_tasks.insert(name).second) {
            continue;
        }
        const auto found = task_indices_.find(name);
        if (found == task_indices_.end()) {
            continue;
        }
        const auto& task = *task_frames_[found->second].source;
        std::set<std::string> dependencies;
        collect_statement_identifiers(task.statements, dependencies);
        collect_statement_calls(
            collect_statement_calls, task.statements);
        for (const auto& argument : task.arguments) {
            dependencies.erase(argument.name);
            if (argument.default_value) {
                collect_identifiers(
                    *argument.default_value, dependencies);
                collect_expression_calls(
                    collect_expression_calls,
                    *argument.default_value);
            }
        }
        for (const auto& variable : task.variables) {
            dependencies.erase(variable.name);
            if (variable.initializer) {
                collect_identifiers(
                    *variable.initializer, dependencies);
                collect_expression_calls(
                    collect_expression_calls,
                    *variable.initializer);
            }
        }
        output.insert(dependencies.begin(), dependencies.end());
    }
}

RegisterId Lowerer::allocate_register(
    const std::size_t width,
    const frontend::ValueDomain domain)
{
    const auto id = next_register_++;
    register_widths_.push_back(width);
    register_domains_.push_back(domain);
    return id;
}

StringRegisterId Lowerer::allocate_string_register()
{
    return next_string_register_++;
}

[[nodiscard]] std::size_t Lowerer::register_width(const RegisterId id) const
{
    return register_widths_.at(static_cast<std::size_t>(id));
}

[[nodiscard]] frontend::ValueDomain Lowerer::register_domain(
    const RegisterId id) const
{
    return register_domains_.at(static_cast<std::size_t>(id));
}

bool Lowerer::validate_sv_nominal_assignment(
    const frontend::Type* target_type,
    const Expression& value)
{
    if (language_ != frontend::Language::SystemVerilog2017
        || target_type == nullptr) {
        return true;
    }
    if (target_type->systemverilog_virtual_interface) {
        if (value.kind == ExpressionKind::Call
            && value.text == "@sv-null") {
            return true;
        }
        const auto* source_type = systemverilog_expression_type(value);
        if (source_type != nullptr) {
            auto target_view = *target_type;
            auto source_view = *source_type;
            target_view.systemverilog_class_parameter_actuals.clear();
            source_view.systemverilog_class_parameter_actuals.clear();
            const bool compatible_view
                = frontend::systemverilog_virtual_interface_assignment_compatible(
                    target_view, source_view);
            if (compatible_view) {
                const auto target_identity
                    = systemverilog_interface_type_identities_.find(
                        target_type);
                const auto source_identity
                    = systemverilog_interface_type_identities_.find(
                        source_type);
                const auto literal_name
                    = systemverilog_interface_literal_name(value);
                const auto literal = literal_name
                    ? systemverilog_interface_literals_.find(
                          *literal_name)
                    : systemverilog_interface_literals_.end();
                if (target_identity
                        != systemverilog_interface_type_identities_.end()
                    && ((literal
                            != systemverilog_interface_literals_.end()
                            && target_identity->second
                                == literal->second.specialization_identity)
                        || (source_identity
                                != systemverilog_interface_type_identities_.end()
                            && target_identity->second
                                == source_identity->second))) {
                    return true;
                }
                if (target_identity
                        == systemverilog_interface_type_identities_.end()
                    && frontend::systemverilog_virtual_interface_assignment_compatible(
                        *target_type, *source_type)) {
                    return true;
                }
            }
        }
        report(
            "FSIM-ELAB-SVIFACE-012",
            "assignment to virtual interface '" + target_type->spelling
                + "' requires a same-type virtual interface whose selected "
                  "modport is not widened or rebound, or null",
            value.span);
        return false;
    }
    if (!is_systemverilog_nominal_packed_type(*target_type)) {
        return true;
    }
    const bool aggregate_target = target_type->packed_aggregate
        != frontend::PackedAggregateKind::None;
    if ((aggregate_target
            && value.kind == ExpressionKind::Aggregate)
        || (target_type->packed_aggregate
                == frontend::PackedAggregateKind::TaggedUnion
            && value.kind == ExpressionKind::Call
            && value.text.starts_with("@sv-tagged:"))) {
        return true;
    }
    const auto* source_type = systemverilog_expression_type(value);
    if (source_type != nullptr
        && is_systemverilog_nominal_packed_type(*source_type)
        && source_type->nominal_type
            == target_type->nominal_type) {
        return true;
    }
    const auto kind = aggregate_target
        ? std::string_view { "aggregate" }
        : std::string_view { "enumeration" };
    report(
        "FSIM-ELAB-SVTYPE-004",
        "assignment to " + std::string { kind } + " '"
            + target_type->spelling
            + "' requires the same nominal type, a matching explicit "
              "cast, or a contextual assignment pattern (expected '"
            + target_type->nominal_type + "', received '"
            + (source_type != nullptr
                    ? source_type->nominal_type
                    : value.nominal_type.empty()
                    ? std::string { "<none>" }
                    : value.nominal_type)
            + "')",
        value.span);
    return false;
}

[[nodiscard]] RegisterId Lowerer::resize_register(
    const RegisterId source,
    const std::size_t width,
    const bool sign_extend)
{
    const auto source_width = register_width(source);
    if (source_width == width) {
        return source;
    }
    const auto domain = register_domain(source);
    const auto destination = allocate_register(width, domain);
    if (width < source_width) {
        process_.operations.emplace_back(Extract {
            destination,
            source,
            0,
            static_cast<std::uint32_t>(width) });
        return destination;
    }
    const auto extension_width = width - source_width;
    RegisterId extension { };
    if (sign_extend) {
        extension = allocate_register(1, domain);
        process_.operations.emplace_back(Extract {
            extension,
            source,
            static_cast<std::uint32_t>(source_width - 1U),
            1 });
    } else {
        extension = allocate_register(
            extension_width,
            frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            extension,
            unsigned_value(0, extension_width) });
    }
    std::vector<RegisterId> operands;
    if (sign_extend) {
        operands.assign(extension_width, extension);
    } else {
        operands.push_back(extension);
    }
    operands.push_back(source);
    process_.operations.emplace_back(Concatenate {
        destination,
        std::move(operands),
        static_cast<std::uint32_t>(width) });
    return destination;
}

[[nodiscard]] RegisterId Lowerer::convert_to_two_state(
    const RegisterId source)
{
    if (is_two_state_domain(register_domain(source))) {
        return source;
    }
    const auto destination = allocate_register(
        register_width(source), frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(ConvertToTwoState {
        destination, source });
    return destination;
}

void Lowerer::initialize_variables(
    const std::vector<frontend::VariableDeclaration>& variables)
{
    struct Pending {
        const frontend::VariableDeclaration* declaration { };
        RegisterId register_id { };
        std::size_t width { };
    };
    std::vector<Pending> pending;
    pending.reserve(variables.size());
    std::unordered_set<std::string> declared_here;
    for (const auto& variable : variables) {
        if (variable.vhdl_file || variable.type.vhdl_file) {
            if (!declared_here.emplace(variable.name).second) {
                report(
                    "FSIM-ELAB-VHFILE-001",
                    "duplicate VHDL file object in the same scope '"
                        + variable.name + "'",
                    variable.span);
                continue;
            }
            if (!variable.type.vhdl_file) {
                report(
                    "FSIM-ELAB-VHFILE-002",
                    "VHDL file object '" + variable.name
                        + "' requires a visible file type",
                    variable.span);
                continue;
            }
            const auto handle = allocate_register(
                32, frontend::ValueDomain::Bit2);
            if (active_procedure_) {
                procedure_file_handles_.push_back(handle);
            }
            locals_.insert_or_assign(variable.name, handle);
            local_types_.insert_or_assign(
                variable.name, &variable.type);
            process_.debug_locals.push_back(DebugLocal {
                scoped_local_name(variable.name),
                variable.type.spelling,
                handle,
                32,
                SourceLocation {
                    variable.span.source_name.str(),
                    static_cast<std::uint32_t>(
                        variable.span.begin.line),
                    static_cast<std::uint32_t>(
                        variable.span.begin.column) },
                { },
                { },
                ValueKind::logic4,
                { },
                frontend::SystemVerilogScalarKind::None });
            process_.operations.emplace_back(
                LoadConstant { handle, unsigned_value(0, 32) });
            if (!variable.initializer) {
                continue;
            }
            if (!is_string_expression(*variable.initializer)) {
                report(
                    "FSIM-ELAB-VHFILE-003",
                    "VHDL file logical name must be a string expression",
                    variable.initializer->span);
                continue;
            }
            const auto path = lower_string_expression(
                *variable.initializer);
            const auto kind = variable.vhdl_file_open_kind
                ? variable.vhdl_file_open_kind->text
                : std::string { "read_mode" };
            const auto spelling = kind == "read_mode" ? "r"
                : kind == "write_mode"                ? "w"
                : kind == "append_mode"               ? "a"
                                                      : "";
            if (*spelling == '\0') {
                report(
                    "FSIM-ELAB-VHFILE-004",
                    "VHDL file open kind must be read_mode, "
                    "write_mode, or append_mode",
                    variable.vhdl_file_open_kind->span);
                continue;
            }
            const auto mode = allocate_string_register();
            process_.operations.emplace_back(
                LoadStringConstant { mode, spelling });
            if (path) {
                process_.operations.emplace_back(
                    FileOpen { handle, *path, mode, std::nullopt, true });
            }
            continue;
        }
        if (variable.type.systemverilog_container) {
            if (!declared_here.emplace(variable.name).second) {
                report(
                    "FSIM-ELAB-SVCONTAINER-001",
                    "duplicate container variable in the same scope '"
                        + variable.name + "'",
                    variable.span);
                continue;
            }
            const auto type = container_type(variable.type, variable.span);
            if (!type) {
                continue;
            }
            const auto register_id = allocate_container_register(*type);
            container_locals_.insert_or_assign(
                variable.name, register_id);
            local_types_.insert_or_assign(
                variable.name, &variable.type);
            process_.debug_container_locals.push_back(
                DebugContainerLocal {
                    scoped_local_name(variable.name),
                    register_id,
                    *type,
                    SourceLocation {
                        variable.span.source_name.str(),
                        static_cast<std::uint32_t>(
                            variable.span.begin.line),
                        static_cast<std::uint32_t>(
                            variable.span.begin.column) } });
            if (variable.initializer) {
                const auto value = variable.initializer->kind
                            == ExpressionKind::Aggregate
                        && variable.initializer->text
                            == "sv-pattern"
                    ? lower_container_pattern(
                          *variable.initializer,
                          variable.type,
                          *type)
                    : type->fixed
                    ? lower_static_container_assignment_value(
                          *variable.initializer, *type)
                    : lower_container_expression(
                          *variable.initializer);
                if (value) {
                    if (process_.container_register_types.at(*value)
                        != *type) {
                        report(
                            variable.initializer->kind
                                    == ExpressionKind::Call
                                ? "FSIM-ELAB-SVFUNC-008"
                                : "FSIM-ELAB-SVCONTAINER-010",
                            "container initializer requires an "
                            "exactly compatible kind and profile",
                            variable.initializer->span);
                    } else {
                        process_.operations.emplace_back(
                            CopyContainerRegister {
                                register_id, *value });
                    }
                }
            }
            continue;
        }
        if (variable.type.domain
            == frontend::ValueDomain::String) {
            if (!declared_here.emplace(variable.name).second) {
                report(
                    "FSIM-ELAB-SVSTRING-005",
                    "duplicate string variable in the same scope '"
                        + variable.name + "'",
                    variable.span);
                continue;
            }
            const auto register_id = allocate_string_register();
            string_locals_.insert_or_assign(
                variable.name, register_id);
            local_types_.insert_or_assign(
                variable.name, &variable.type);
            process_.debug_string_locals.push_back(
                DebugStringLocal {
                    scoped_local_name(variable.name),
                    register_id,
                    SourceLocation {
                        variable.span.source_name.str(),
                        static_cast<std::uint32_t>(
                            variable.span.begin.line),
                        static_cast<std::uint32_t>(
                            variable.span.begin.column) } });
            if (variable.initializer) {
                const auto value = lower_string_expression(
                    *variable.initializer);
                if (value) {
                    process_.operations.emplace_back(
                        CopyStringRegister {
                            register_id, *value });
                }
            } else {
                process_.operations.emplace_back(
                    LoadStringConstant { register_id, { } });
            }
            continue;
        }
        const auto width = variable.type.width();
        const bool null_vhdl_array = variable.type.vhdl_array
            && variable.type.vhdl_array->flat_width
            && *variable.type.vhdl_array->flat_width == 0;
        if (!width || (*width == 0 && !null_vhdl_array)) {
            report(
                "FSIM-ELAB-052",
                "local variable '" + variable.name
                    + "' has no executable packed width",
                variable.span);
            continue;
        }
        if (!declared_here.emplace(variable.name).second) {
            report(
                "FSIM-ELAB-053",
                "duplicate local variable in the same scope '"
                    + variable.name + "'",
                variable.span);
            continue;
        }
        const auto key = declaration_key(variable);
        const auto register_found = declaration_registers_.find(key);
        RegisterId register_id { };
        if (register_found == declaration_registers_.end()) {
            register_id = allocate_register(*width, variable.type.domain);
            declaration_registers_.emplace(key, register_id);
            auto debug_name = scoped_local_name(variable.name);
            if (!debug_local_names_.emplace(debug_name).second) {
                if (active_function_) {
                    debug_name += "@callable-"
                        + std::to_string(
                            function_frames_[*active_function_]
                                .invocation_identity);
                } else {
                    debug_name += "@"
                        + std::to_string(variable.span.begin.line)
                        + ":"
                        + std::to_string(variable.span.begin.column);
                }
                debug_local_names_.emplace(debug_name);
            }
            process_.debug_locals.push_back(DebugLocal {
                std::move(debug_name),
                variable.type.spelling,
                register_id,
                *width,
                SourceLocation {
                    variable.span.source_name.str(),
                    static_cast<std::uint32_t>(
                        variable.span.begin.line),
                    static_cast<std::uint32_t>(
                        variable.span.begin.column) },
                { },
                { },
                value_kind(variable.type.domain),
                { },
                variable.type.systemverilog_scalar });
            if (variable.type.integer_range) {
                const auto [lower, upper] = integer_bounds(variable.type.integer_range);
                process_.debug_locals.back().integer_lower = lower;
                process_.debug_locals.back().integer_upper = upper;
            }
            process_.debug_locals.back().enumeration_literals = variable.type.enumeration_literals;
        } else {
            register_id = register_found->second;
        }
        locals_.insert_or_assign(variable.name, register_id);
        local_signed_.insert_or_assign(
            variable.name, variable.type.is_signed);
        local_ranges_.insert_or_assign(
            variable.name, variable.type.packed_range);
        local_integer_ranges_.insert_or_assign(
            variable.name, variable.type.integer_range);
        local_members_.insert_or_assign(
            variable.name, variable.type.packed_members);
        local_types_.insert_or_assign(
            variable.name, &variable.type);
        pending.push_back(Pending { &variable, register_id, *width });
    }
    for (const auto& local : pending) {
        const auto& variable = *local.declaration;
        if (variable.initializer) {
            if (!validate_sv_nominal_assignment(
                    &variable.type, *variable.initializer)) {
                continue;
            }
            if (variable.type.domain
                    == frontend::ValueDomain::Integer
                && !is_integer_expression(
                    *variable.initializer)) {
                report(
                    "FSIM-ELAB-INTEGER-004",
                    "VHDL integer local initializer requires an "
                    "integer-family expression",
                    variable.span);
                continue;
            }
            if (variable.type.domain
                    == frontend::ValueDomain::Integer
                && !validate_static_integer_assignment(
                    *variable.initializer,
                    variable.type.integer_range,
                    variable.span)) {
                continue;
            }
            if (!variable.type.enumeration_literals.empty()
                && !validate_static_enumeration_assignment(
                    *variable.initializer,
                    variable.type,
                    variable.span)) {
                continue;
            }
            auto value = lower_expression(
                *variable.initializer,
                local.width,
                &variable.type);
            if (!value) {
                continue;
            }
            if (register_width(*value) != local.width
                && language_
                    != frontend::Language::Vhdl2008) {
                *value = resize_register(
                    *value,
                    local.width,
                    is_signed_expression(
                        *variable.initializer));
            }
            if (register_width(*value) != local.width) {
                report(
                    "FSIM-ELAB-054",
                    "local variable initializer width mismatch for '"
                        + variable.name + "'",
                    variable.span);
                continue;
            }
            if (is_two_state_domain(variable.type.domain)
                && !is_two_state_domain(
                    register_domain(*value))) {
                report(
                    "FSIM-ELAB-058",
                    "two-state local variable initializer for '"
                        + variable.name
                        + "' requires an explicit conversion",
                    variable.span);
                continue;
            }
            if (local.width == 0) {
                continue;
            }
            if (variable.type.domain
                == frontend::ValueDomain::Integer) {
                emit_integer_check(
                    *value, variable.type.integer_range);
            }
            if (!variable.type.enumeration_literals.empty()) {
                emit_enumeration_check(
                    *value, variable.type);
            }
            process_.operations.emplace_back(
                CopyRegister { local.register_id, *value });
            continue;
        }
        if (local.width == 0) {
            continue;
        }
        auto initial_value = default_packed_value(variable.type, local.width);
        process_.operations.emplace_back(
            LoadConstant {
                local.register_id,
                std::move(initial_value) });
    }
}

void Lowerer::report(std::string code, std::string message, frontend::SourceSpan span)
{
    diagnostics_.push_back({ std::move(code), std::move(message), std::move(span) });
}

} // namespace fsim::elaboration
