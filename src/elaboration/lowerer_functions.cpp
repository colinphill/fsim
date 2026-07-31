// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

const frontend::FunctionDeclaration* Lowerer::visible_function(
    const std::string_view name) const {
    const auto found = function_indices_.find(std::string{name});
    return found == function_indices_.end()
        ? nullptr
        : function_frames_[found->second].source;
}

void Lowerer::initialize_function_support() {
    function_frames_.clear();
    function_indices_.clear();
    pending_functions_.clear();
    function_dependencies_.clear();
    active_function_.reset();
    function_return_jumps_.clear();
    function_call_stack_ = {};
    function_support_initialized_ = false;
    if (functions_.empty()) {
        return;
    }
    if (functions_.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVFUNC-001",
            "too many visible functions for the SimIR call stack",
            functions_.front().span);
        return;
    }

    function_frames_.reserve(functions_.size());
    for (const auto& function : functions_) {
        const auto index = function_frames_.size();
        const auto [existing, inserted] =
            function_indices_.emplace(function.name, index);
        if (!inserted) {
            report(
                "FSIM-ELAB-SVFUNC-002",
                "ambiguous visible function '" + function.name + "'",
                function.span);
            (void)existing;
            continue;
        }
        FunctionFrame frame;
        frame.source = &function;
        if (!function.automatic) {
            frame.static_variables =
                allocate_static_callable_variables(
                    function.variables,
                    "FSIM-ELAB-SVFUNC-013",
                    "function");
        }
        function_frames_.push_back(std::move(frame));
    }
    function_dependencies_.resize(function_frames_.size());
    if (function_frames_.empty()) {
        return;
    }

    function_call_stack_.pointer =
        allocate_register(32, frontend::ValueDomain::Bit2);
    function_call_stack_.entries = next_register_;
    function_call_stack_.capacity =
        static_cast<std::uint32_t>(function_frames_.size());
    process_.operations.emplace_back(LoadConstant{
        function_call_stack_.pointer, unsigned_value(0, 32)});
    for (std::uint32_t index = 0;
         index < function_call_stack_.capacity; ++index) {
        const auto entry =
            allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant{entry, unsigned_value(0, 32)});
    }
    function_support_initialized_ = true;
}

Lowerer::ExpressionAttempt Lowerer::lower_user_function_expression(
    const Expression& expression,
    const std::size_t expected_width,
    const frontend::Type*) {
    if (expression.kind != ExpressionKind::Call
        || !function_support_initialized_) {
        return ExpressionAttempt{};
    }
    const auto found = function_indices_.find(expression.text);
    if (found == function_indices_.end()) {
        return ExpressionAttempt{};
    }
    const auto function_index = found->second;
    auto& frame = function_frames_[function_index];
    const auto& function = *frame.source;
    if (function.return_type.domain
            == frontend::ValueDomain::String
        || function.return_type.systemverilog_container) {
        return ExpressionAttempt{};
    }
    const bool has_defaults = std::ranges::any_of(
        function.arguments,
        [](const frontend::FunctionArgument& argument) {
          return argument.default_value.has_value();
        });
    if (expression.call_argument_names.empty()
        && !has_defaults
        && expression.operands.size() != function.arguments.size()) {
        report(
            "FSIM-ELAB-SVFUNC-003",
            "function '" + function.name + "' expects "
                + std::to_string(function.arguments.size())
                + " arguments but received "
                + std::to_string(expression.operands.size()),
            expression.span);
        return std::nullopt;
    }
    const auto actuals = bind_function_actuals(expression, function);
    if (!actuals
        || !validate_function_reference_actuals(function, *actuals)) {
        return std::nullopt;
    }

    if (!frame.allocated) {
        const auto return_width = function.return_type.width();
        if (!return_width || *return_width == 0
            || *return_width > 64) {
            report(
                "FSIM-ELAB-SVFUNC-004",
                "function '" + function.name
                    + "' return type must have an executable width in "
                      "[1, 64]",
                function.span);
            return std::nullopt;
        }
        frame.result = allocate_register(
            static_cast<std::size_t>(*return_width),
            function.return_type.domain);
        frame.arguments.reserve(function.arguments.size());
        frame.string_arguments.reserve(function.arguments.size());
        frame.container_arguments.reserve(function.arguments.size());
        frame.argument_is_string.reserve(function.arguments.size());
        frame.argument_is_container.reserve(function.arguments.size());
        for (const auto& argument : function.arguments) {
            if (argument.type.systemverilog_container) {
                const auto type =
                    container_type(argument.type, argument.span);
                if (!type) {
                    return std::nullopt;
                }
                frame.arguments.push_back({});
                frame.string_arguments.push_back({});
                frame.container_arguments.push_back(
                    allocate_container_register(*type));
                frame.argument_is_string.push_back(false);
                frame.argument_is_container.push_back(true);
                continue;
            }
            if (argument.type.domain
                == frontend::ValueDomain::String) {
                frame.arguments.push_back({});
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.container_arguments.push_back({});
                frame.argument_is_string.push_back(true);
                frame.argument_is_container.push_back(false);
                continue;
            }
            const auto width = argument.type.width();
            if (!width || *width == 0 || *width > 64) {
                report(
                    "FSIM-ELAB-SVFUNC-004",
                    "function argument '" + argument.name
                        + "' must have an executable width in [1, 64]",
                    argument.span);
                return std::nullopt;
            }
            frame.arguments.push_back(allocate_register(
                static_cast<std::size_t>(*width),
                argument.type.domain));
            frame.string_arguments.push_back({});
            frame.container_arguments.push_back({});
            frame.argument_is_string.push_back(false);
            frame.argument_is_container.push_back(false);
        }
        frame.allocated = true;
    }

    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Output) {
            if (frame.argument_is_container[index]
                || frame.argument_is_string[index]) {
                report(
                    "FSIM-ELAB-SVFUNC-011",
                    "function output/inout/ref formals require a bounded "
                    "packed integral type",
                    formal.span);
                return std::nullopt;
            }
            const auto width = static_cast<std::size_t>(
                *formal.type.width());
            process_.operations.emplace_back(LoadConstant{
                frame.arguments[index],
                default_packed_value(formal.type, width)});
            continue;
        }
        if (frame.argument_is_container[index]) {
            const auto formal_type =
                container_type(formal.type, formal.span);
            if (!formal_type) {
                return std::nullopt;
            }
            const auto actual =
                formal_type->fixed
                    ? lower_static_container_assignment_value(
                          *(*actuals)[index],
                          *formal_type)
                    : lower_container_expression(
                          *(*actuals)[index]);
            if (!actual) {
                return std::nullopt;
            }
            if (process_.container_register_types.at(*actual)
                != *formal_type) {
                report(
                    "FSIM-ELAB-SVFUNC-009",
                    "function container arguments require an exactly "
                    "compatible kind and profile",
                    (*actuals)[index]->span);
                return std::nullopt;
            }
            process_.operations.emplace_back(
                CopyContainerRegister{
                    frame.container_arguments[index], *actual});
            continue;
        }
        if (frame.argument_is_string[index]) {
            const auto actual =
                lower_string_expression(
                    *(*actuals)[index]);
            if (!actual) {
                return std::nullopt;
            }
            process_.operations.emplace_back(
                CopyStringRegister{
                    frame.string_arguments[index], *actual});
            continue;
        }
        const auto width = static_cast<std::size_t>(
            *formal.type.width());
        auto actual = lower_expression(
            *(*actuals)[index], width, &formal.type);
        if (!actual) {
            return std::nullopt;
        }
        if (register_width(*actual) != width) {
            *actual = resize_register(
                *actual,
                width,
                is_signed_expression(*(*actuals)[index]));
        }
        process_.operations.emplace_back(
            CopyRegister{frame.arguments[index], *actual});
    }
    const auto return_width =
        static_cast<std::size_t>(*function.return_type.width());
    process_.operations.emplace_back(LoadConstant{
        frame.result,
        default_packed_value(function.return_type, return_width)});
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call{
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        function_call_stack_});
    if (frame.target) {
        std::get<Call>(process_.operations[call_site]).target =
            *frame.target;
    } else {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_functions_.push_back(function_index);
    }
    if (active_function_) {
        function_dependencies_[*active_function_].insert(
            function_index);
    }
    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Input) {
            continue;
        }
        lower_callable_copy_out(
            *(*actuals)[index],
            formal.type,
            frame.arguments[index],
            frame.string_arguments[index],
            frame.container_arguments[index],
            frame.argument_is_string[index],
            frame.argument_is_container[index],
            "@function_copyout_" + std::to_string(function_index)
                + "_" + std::to_string(index));
    }

    const auto destination = allocate_register(
        return_width, function.return_type.domain);
    process_.operations.emplace_back(
        CopyRegister{destination, frame.result});
    if (expected_width != 0 && expected_width != return_width) {
        return resize_register(
            destination,
            expected_width,
            function.return_type.is_signed);
    }
    return destination;
}

std::optional<ContainerRegisterId>
Lowerer::lower_user_container_function_expression(
    const Expression& expression) {
    if (expression.kind != ExpressionKind::Call
        || !function_support_initialized_) {
        return std::nullopt;
    }
    const auto found = function_indices_.find(expression.text);
    if (found == function_indices_.end()) {
        return std::nullopt;
    }
    const auto function_index = found->second;
    auto& frame = function_frames_[function_index];
    const auto& function = *frame.source;
    if (!function.return_type.systemverilog_container) {
        return std::nullopt;
    }
    const bool has_defaults = std::ranges::any_of(
        function.arguments,
        [](const frontend::FunctionArgument& argument) {
          return argument.default_value.has_value();
        });
    if (expression.call_argument_names.empty()
        && !has_defaults
        && expression.operands.size() != function.arguments.size()) {
        report(
            "FSIM-ELAB-SVFUNC-003",
            "function '" + function.name + "' expects "
                + std::to_string(function.arguments.size())
                + " arguments but received "
                + std::to_string(expression.operands.size()),
            expression.span);
        return std::nullopt;
    }
    const auto actuals = bind_function_actuals(expression, function);
    if (!actuals
        || !validate_function_reference_actuals(function, *actuals)) {
        return std::nullopt;
    }

    const auto result_type =
        container_type(function.return_type, function.span);
    if (!result_type) {
        return std::nullopt;
    }
    if (!frame.allocated) {
        frame.result_is_container = true;
        frame.container_result =
            allocate_container_register(*result_type);
        frame.container_result_default =
            allocate_container_register(*result_type);
        frame.arguments.reserve(function.arguments.size());
        frame.string_arguments.reserve(function.arguments.size());
        frame.container_arguments.reserve(function.arguments.size());
        frame.argument_is_string.reserve(function.arguments.size());
        frame.argument_is_container.reserve(function.arguments.size());
        for (const auto& argument : function.arguments) {
            if (argument.type.systemverilog_container) {
                const auto type =
                    container_type(argument.type, argument.span);
                if (!type) {
                    return std::nullopt;
                }
                frame.arguments.push_back({});
                frame.string_arguments.push_back({});
                frame.container_arguments.push_back(
                    allocate_container_register(*type));
                frame.argument_is_string.push_back(false);
                frame.argument_is_container.push_back(true);
                continue;
            }
            if (argument.type.domain
                == frontend::ValueDomain::String) {
                frame.arguments.push_back({});
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.container_arguments.push_back({});
                frame.argument_is_string.push_back(true);
                frame.argument_is_container.push_back(false);
                continue;
            }
            const auto width = argument.type.width();
            if (!width || *width == 0 || *width > 64) {
                report(
                    "FSIM-ELAB-SVFUNC-004",
                    "function argument '" + argument.name
                        + "' must have an executable width in [1, 64]",
                    argument.span);
                return std::nullopt;
            }
            frame.arguments.push_back(allocate_register(
                static_cast<std::size_t>(*width),
                argument.type.domain));
            frame.string_arguments.push_back({});
            frame.container_arguments.push_back({});
            frame.argument_is_string.push_back(false);
            frame.argument_is_container.push_back(false);
        }
        frame.allocated = true;
    }

    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Output) {
            if (frame.argument_is_container[index]
                || frame.argument_is_string[index]) {
                report(
                    "FSIM-ELAB-SVFUNC-011",
                    "function output/inout/ref formals require a bounded "
                    "packed integral type",
                    formal.span);
                return std::nullopt;
            }
            const auto width = static_cast<std::size_t>(
                *formal.type.width());
            process_.operations.emplace_back(LoadConstant{
                frame.arguments[index],
                default_packed_value(formal.type, width)});
            continue;
        }
        if (frame.argument_is_container[index]) {
            const auto formal_type =
                container_type(formal.type, formal.span);
            if (!formal_type) {
                return std::nullopt;
            }
            const auto actual =
                formal_type->fixed
                    ? lower_static_container_assignment_value(
                          *(*actuals)[index],
                          *formal_type)
                    : lower_container_expression(
                          *(*actuals)[index]);
            if (!actual) {
                return std::nullopt;
            }
            if (process_.container_register_types.at(*actual)
                != *formal_type) {
                report(
                    "FSIM-ELAB-SVFUNC-009",
                    "function container arguments require an exactly "
                    "compatible kind and profile",
                    (*actuals)[index]->span);
                return std::nullopt;
            }
            process_.operations.emplace_back(
                CopyContainerRegister{
                    frame.container_arguments[index], *actual});
            continue;
        }
        if (frame.argument_is_string[index]) {
            const auto actual =
                lower_string_expression(*(*actuals)[index]);
            if (!actual) {
                return std::nullopt;
            }
            process_.operations.emplace_back(
                CopyStringRegister{
                    frame.string_arguments[index], *actual});
            continue;
        }
        const auto width = static_cast<std::size_t>(
            *formal.type.width());
        auto actual = lower_expression(
            *(*actuals)[index], width, &formal.type);
        if (!actual) {
            return std::nullopt;
        }
        if (register_width(*actual) != width) {
            *actual = resize_register(
                *actual,
                width,
                is_signed_expression(*(*actuals)[index]));
        }
        process_.operations.emplace_back(
            CopyRegister{frame.arguments[index], *actual});
    }

    process_.operations.emplace_back(
        CopyContainerRegister{
            frame.container_result,
            frame.container_result_default});
    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call{
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        function_call_stack_});
    if (frame.target) {
        std::get<Call>(process_.operations[call_site]).target =
            *frame.target;
    } else {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_functions_.push_back(function_index);
    }
    if (active_function_) {
        function_dependencies_[*active_function_].insert(
            function_index);
    }
    for (std::size_t index = 0;
         index < function.arguments.size(); ++index) {
        const auto& formal = function.arguments[index];
        if (formal.direction == frontend::PortDirection::Input) {
            continue;
        }
        lower_callable_copy_out(
            *(*actuals)[index],
            formal.type,
            frame.arguments[index],
            frame.string_arguments[index],
            frame.container_arguments[index],
            frame.argument_is_string[index],
            frame.argument_is_container[index],
            "@function_copyout_" + std::to_string(function_index)
                + "_" + std::to_string(index));
    }

    const auto destination =
        allocate_container_register(*result_type);
    process_.operations.emplace_back(
        CopyContainerRegister{
            destination, frame.container_result});
    return destination;
}

void Lowerer::lower_function_return(const Statement& statement) {
    if (!active_function_) {
        report(
            "FSIM-ELAB-SVFUNC-005",
            "return statement is outside an executable function",
            statement.span);
        return;
    }
    auto& frame = function_frames_[*active_function_];
    const auto& function = *frame.source;
    if (frame.result_is_container) {
        if (!statement.value.valid()) {
            report(
                "FSIM-ELAB-SVFUNC-005",
                "static-array function return requires a value",
                statement.span);
        } else {
            const auto result_type = container_type(
                function.return_type, function.span);
            if (result_type) {
                const auto value =
                    result_type->fixed
                        ? lower_static_container_assignment_value(
                              statement.value, *result_type)
                        : lower_container_expression(
                              statement.value);
                if (value) {
                    if (!result_type->fixed
                        && process_.container_register_types.at(*value)
                            != *result_type) {
                        report(
                            "FSIM-ELAB-SVFUNC-008",
                            "function return container kind and profile "
                            "must exactly match the declared result",
                            statement.value.span);
                    } else {
                        process_.operations.emplace_back(
                            CopyContainerRegister{
                                frame.container_result, *value});
                    }
                }
            }
        }
        function_return_jumps_.push_back(
            static_cast<InstructionIndex>(
                process_.operations.size()));
        process_.operations.emplace_back(Jump{});
        return;
    }
    if (frame.result_is_string) {
        if (!statement.value.valid()) {
            report(
                "FSIM-ELAB-SVFUNC-005",
                "string function return requires a value",
                statement.span);
        } else if (const auto value =
                       lower_string_expression(statement.value)) {
            process_.operations.emplace_back(
                CopyStringRegister{
                    frame.string_result, *value});
        }
        function_return_jumps_.push_back(
            static_cast<InstructionIndex>(
                process_.operations.size()));
        process_.operations.emplace_back(Jump{});
        return;
    }
    const auto width =
        static_cast<std::size_t>(*function.return_type.width());
    if (!statement.value.valid()) {
        report(
            "FSIM-ELAB-SVFUNC-005",
            "function return requires a value",
            statement.span);
    } else {
        auto value = lower_expression(
            statement.value, width, &function.return_type);
        if (value) {
            if (register_width(*value) != width) {
                *value = resize_register(
                    *value,
                    width,
                    is_signed_expression(statement.value));
            }
            process_.operations.emplace_back(
                CopyRegister{frame.result, *value});
        }
    }
    function_return_jumps_.push_back(
        static_cast<InstructionIndex>(
            process_.operations.size()));
    process_.operations.emplace_back(Jump{});
}

void Lowerer::lower_function_body(const std::size_t function_index) {
    auto& frame = function_frames_[function_index];
    if (frame.lowered || !frame.allocated) {
        return;
    }
    frame.target = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto call_site : frame.call_sites) {
        std::get<Call>(process_.operations[call_site]).target =
            *frame.target;
    }
    frame.call_sites.clear();
    frame.lowered = true;

    auto saved_locals = std::move(locals_);
    auto saved_string_locals = std::move(string_locals_);
    auto saved_container_locals =
        std::move(container_locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges = std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    auto saved_loop_controls = std::move(loop_controls_);
    auto saved_return_jumps = std::move(function_return_jumps_);
    const auto saved_active = active_function_;
    locals_.clear();
    string_locals_.clear();
    container_locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = {frame.source->name};
    loop_controls_.clear();
    function_return_jumps_.clear();
    active_function_ = function_index;

    const auto bind =
        [&](const std::string& name,
            const frontend::Type& type,
            const RegisterId register_id) {
          locals_.insert_or_assign(name, register_id);
          local_signed_.insert_or_assign(name, type.is_signed);
          local_ranges_.insert_or_assign(name, type.packed_range);
          local_integer_ranges_.insert_or_assign(
              name, type.integer_range);
          local_members_.insert_or_assign(
              name, type.packed_members);
          local_types_.insert_or_assign(name, &type);
        };
    if (frame.result_is_container) {
        container_locals_.insert_or_assign(
            frame.source->name, frame.container_result);
        local_types_.insert_or_assign(
            frame.source->name, &frame.source->return_type);
        const auto separator = frame.source->name.rfind("::");
        const auto alias =
            separator == std::string::npos
                ? frame.source->name
                : frame.source->name.substr(separator + 2);
        process_.debug_container_locals.push_back(
            DebugContainerLocal{
                scoped_local_name(alias),
                frame.container_result,
                process_.container_register_types.at(
                    frame.container_result),
                SourceLocation{
                    frame.source->span.source_name,
                    static_cast<std::uint32_t>(
                        frame.source->span.begin.line),
                    static_cast<std::uint32_t>(
                        frame.source->span.begin.column)}});
    } else if (frame.result_is_string) {
        string_locals_.insert_or_assign(
            frame.source->name, frame.string_result);
        local_types_.insert_or_assign(
            frame.source->name, &frame.source->return_type);
    } else {
        bind(
            frame.source->name,
            frame.source->return_type,
            frame.result);
    }
    if (const auto separator =
            frame.source->name.rfind("::");
        separator != std::string::npos) {
        if (frame.result_is_container) {
            const auto alias =
                frame.source->name.substr(separator + 2);
            container_locals_.insert_or_assign(
                alias, frame.container_result);
            local_types_.insert_or_assign(
                alias, &frame.source->return_type);
        } else if (frame.result_is_string) {
            string_locals_.insert_or_assign(
                frame.source->name.substr(separator + 2),
                frame.string_result);
        } else {
            bind(
                frame.source->name.substr(separator + 2),
                frame.source->return_type,
                frame.result);
        }
    }
    for (std::size_t index = 0;
         index < frame.source->arguments.size(); ++index) {
        if (frame.argument_is_container[index]) {
            container_locals_.insert_or_assign(
                frame.source->arguments[index].name,
                frame.container_arguments[index]);
            local_types_.insert_or_assign(
                frame.source->arguments[index].name,
                &frame.source->arguments[index].type);
        } else if (frame.argument_is_string[index]) {
            string_locals_.insert_or_assign(
                frame.source->arguments[index].name,
                frame.string_arguments[index]);
            local_types_.insert_or_assign(
                frame.source->arguments[index].name,
                &frame.source->arguments[index].type);
        } else {
            bind(
                frame.source->arguments[index].name,
                frame.source->arguments[index].type,
                frame.arguments[index]);
        }
    }
    if (frame.source->automatic) {
        initialize_variables(frame.source->variables);
    } else {
        bind_static_callable_variables(
            frame.source->variables, frame.static_variables);
    }
    lower_statements(frame.source->statements);
    const auto epilogue = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto jump : function_return_jumps_) {
        process_.operations[jump] = Jump{epilogue};
    }
    process_.operations.emplace_back(
        Return{function_call_stack_});

    active_function_ = saved_active;
    function_return_jumps_ = std::move(saved_return_jumps);
    loop_controls_ = std::move(saved_loop_controls);
    local_scope_ = std::move(saved_scope);
    local_types_ = std::move(saved_types);
    local_members_ = std::move(saved_members);
    local_integer_ranges_ =
        std::move(saved_integer_ranges);
    local_ranges_ = std::move(saved_ranges);
    local_signed_ = std::move(saved_signed);
    locals_ = std::move(saved_locals);
    string_locals_ = std::move(saved_string_locals);
    container_locals_ =
        std::move(saved_container_locals);
}

void Lowerer::diagnose_function_cycles() {
    std::vector<std::uint8_t> state(
        function_frames_.size(), 0);
    const auto visit =
        [&](const auto& self, const std::size_t index) -> bool {
          if (state[index] == 1) {
              report(
                  "FSIM-ELAB-SVFUNC-006",
                  "recursive function call graph involving '"
                      + function_frames_[index].source->name
                      + "' is not supported",
                  function_frames_[index].source->span);
              return true;
          }
          if (state[index] == 2) {
              return false;
          }
          state[index] = 1;
          for (const auto dependency :
               function_dependencies_[index]) {
              if (self(self, dependency)) {
                  state[index] = 2;
                  return true;
              }
          }
          state[index] = 2;
          return false;
        };
    for (std::size_t index = 0;
         index < function_frames_.size(); ++index) {
        if (state[index] == 0) {
            (void)visit(visit, index);
        }
    }
}

void Lowerer::lower_pending_functions() {
    while (!pending_functions_.empty()) {
        const auto function = pending_functions_.front();
        pending_functions_.pop_front();
        function_frames_[function].queued = false;
        lower_function_body(function);
    }
    diagnose_function_cycles();
}

} // namespace fsim::elaboration
