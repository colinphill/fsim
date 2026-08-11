// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void Lowerer::collect_class_tasks(
    const std::vector<Statement>& statements)
{
    for (const auto& statement : statements) {
        if (statement.kind == StatementKind::TaskCall
            && (statement.task_name.starts_with("@sv-task:")
                || statement.task_name.starts_with("@sv-static-task:"))
            && std::ranges::none_of(
                class_tasks_, [&](const auto& existing) {
                    return existing.name == statement.task_name;
                })) {
            frontend::TaskDeclaration task;
            task.name = statement.task_name;
            task.automatic = true;
            task.lifetime_explicit = false;
            task.span = statement.span;
            const auto has_receiver = statement.task_name.starts_with("@sv-task:");
            if (has_receiver && !statement.task_arguments.empty()) {
                frontend::Type receiver_type;
                receiver_type.systemverilog_class_declaration = statement.task_arguments.front().nominal_type;
                receiver_type.systemverilog_class_name = statement.task_arguments.front().nominal_type;
                task.arguments.emplace_back(
                    "this",
                    std::move(receiver_type),
                    frontend::PortDirection::Input,
                    statement.task_arguments.front().span);
            }
            for (const auto& argument : statement.class_method_arguments) {
                task.arguments.emplace_back(
                    argument.name,
                    argument.type,
                    argument.direction,
                    argument.span,
                    argument.reference,
                    argument.default_value);
            }
            task.variables = statement.declarations;
            task.statements = statement.statements;
            class_tasks_.push_back(std::move(task));
        }
        collect_class_tasks(statement.statements);
        collect_class_tasks(statement.else_statements);
        for (const auto& alternative : statement.case_alternatives) {
            collect_class_tasks(alternative.statements);
        }
    }
}

void Lowerer::initialize_task_support()
{
    task_frames_.clear();
    task_indices_.clear();
    pending_tasks_.clear();
    task_dependencies_.clear();
    task_suspending_.clear();
    active_task_.reset();
    task_return_jumps_.clear();
    task_call_stack_ = { };
    task_support_initialized_ = false;
    const auto task_count = tasks_.size() + class_tasks_.size();
    if (task_count == 0) {
        return;
    }
    if (task_count
        > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVTASK-003",
            "too many visible tasks for the SimIR call stack",
            tasks_.empty() ? class_tasks_.front().span : tasks_.front().span);
        return;
    }

    task_frames_.reserve(task_count);
    const auto register_task = [&](const frontend::TaskDeclaration& task) {
        const auto index = task_frames_.size();
        const auto [existing, inserted] = task_indices_.emplace(task.name, index);
        if (!inserted) {
            report(
                "FSIM-ELAB-SVTASK-004",
                "ambiguous visible task '" + task.name + "'",
                task.span);
            (void)existing;
            return;
        }
        TaskFrame frame;
        frame.source = &task;
        frame.invocation_identity = next_callable_invocation_identity_++;
        if (!task.automatic) {
            frame.static_variables = allocate_static_callable_variables(
                task.variables,
                task.statements,
                task.name);
        }
        task_frames_.push_back(std::move(frame));
    };
    for (const auto& task : tasks_) {
        register_task(task);
    }
    for (const auto& task : class_tasks_) {
        register_task(task);
    }
    task_dependencies_.resize(task_frames_.size());
    task_suspending_.assign(task_frames_.size(), false);
    const auto statement_may_suspend =
        [&](const auto& self,
            const std::vector<Statement>& statements) -> bool {
        for (const auto& statement : statements) {
            if (statement.kind == StatementKind::Delay
                || statement.kind == StatementKind::WaitOn
                || statement.kind == StatementKind::WaitUntil) {
                return true;
            }
            if (statement.kind == StatementKind::TaskCall) {
                const auto dependency = task_indices_.find(statement.task_name);
                if (dependency != task_indices_.end()
                    && task_suspending_[dependency->second]) {
                    return true;
                }
            }
            if (self(self, statement.statements)
                || self(self, statement.else_statements)) {
                return true;
            }
            for (const auto& alternative :
                statement.case_alternatives) {
                if (self(self, alternative.statements)) {
                    return true;
                }
            }
        }
        return false;
    };
    bool changed = false;
    do {
        changed = false;
        for (std::size_t index = 0;
            index < task_frames_.size(); ++index) {
            if (!task_suspending_[index]
                && statement_may_suspend(
                    statement_may_suspend,
                    task_frames_[index].source->statements)) {
                task_suspending_[index] = true;
                changed = true;
            }
        }
    } while (changed);
    if (task_frames_.empty()) {
        return;
    }

    task_support_initialized_ = true;
}

void Lowerer::lower_task_call(const Statement& statement)
{
    if (statement.assertion_control
        != frontend::SystemVerilogAssertionControlKind::None) {
        auto marker = std::string { "\x1f"
                                    "fsim.assertion-control|" }
            + statement.task_name.substr(1U);
        if (statement.assertion_control
                == frontend::SystemVerilogAssertionControlKind::Control
            && !statement.task_arguments.empty()) {
            marker += "|" + statement.task_arguments.front().text;
        }
        process_.operations.emplace_back(
            Display { std::move(marker), false, false });
        return;
    }
    if (lower_string_format_task(statement)) {
        return;
    }
    constexpr std::string_view class_container_push_prefix {
        "@sv-container-push-back:"
    };
    if (statement.task_name.starts_with(class_container_push_prefix)) {
        if (statement.task_arguments.size() != 2U) {
            report(
                "FSIM-ELAB-SVCLASS-015",
                "class handle queue push_back requires one value",
                statement.span);
            return;
        }
        const auto receiver = lower_expression(
            statement.task_arguments[0], 64);
        const auto value = lower_expression(
            statement.task_arguments[1], 64);
        if (!receiver || !value)
            return;
        const auto destination = allocate_register(
            64, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ClassMethodCall {
            destination,
            *receiver,
            "@container-push-back:"
                + statement.task_name.substr(
                    class_container_push_prefix.size()),
            { *value },
            { "" },
            { static_cast<std::uint8_t>(
                frontend::PortDirection::Input) },
            64,
            false });
        return;
    }
    const auto native_uvm_task = [](const std::string_view identity) {
        return identity.starts_with("@uvm-")
            || identity.find("::uvm_") != std::string_view::npos;
    };
    constexpr std::string_view class_static_task_prefix { "@sv-static-task:" };
    if (statement.task_name.starts_with(class_static_task_prefix)
        && native_uvm_task(statement.task_name.substr(
            class_static_task_prefix.size()))) {
        std::vector<RegisterId> actuals;
        std::vector<std::uint8_t> actual_kinds;
        actuals.reserve(statement.task_arguments.size());
        actual_kinds.reserve(statement.task_arguments.size());
        for (const auto& operand : statement.task_arguments) {
            if (is_string_expression(operand)) {
                const auto actual = lower_string_expression(operand);
                if (!actual)
                    return;
                actuals.push_back(*actual);
                actual_kinds.push_back(1U);
                continue;
            }
            const auto width = infer_width(operand).value_or(64U);
            const auto actual = lower_expression(operand, width);
            if (!actual)
                return;
            actuals.push_back(*actual);
            actual_kinds.push_back(0U);
        }
        auto names = statement.task_argument_names;
        if (names.size() != actuals.size()) {
            names.assign(actuals.size(), std::string { });
        }
        std::vector<std::uint8_t> directions;
        directions.reserve(actuals.size());
        for (std::size_t index = 0; index < actuals.size(); ++index) {
            const auto direction = index < statement.class_method_arguments.size()
                ? statement.class_method_arguments[index].direction
                : frontend::PortDirection::Input;
            directions.push_back(static_cast<std::uint8_t>(direction));
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ClassStaticMethodCall {
            destination,
            statement.task_name.substr(class_static_task_prefix.size()),
            std::move(actuals),
            std::move(names),
            std::move(directions),
            1,
            std::move(actual_kinds) });
        return;
    }
    constexpr std::string_view class_task_prefix { "@sv-task:" };
    if (statement.task_name.starts_with(class_task_prefix)
        && native_uvm_task(statement.task_name.substr(
            class_task_prefix.size()))) {
        const auto identity = std::string_view { statement.task_name }.substr(
            class_task_prefix.size());
        if (statement.task_arguments.empty()) {
            report(
                "FSIM-ELAB-SVCLASS-006",
                "class task requires a class receiver",
                statement.span);
            return;
        }
        const auto receiver = lower_expression(
            statement.task_arguments.front(), 64);
        if (!receiver)
            return;
        std::vector<RegisterId> actuals;
        std::vector<std::uint8_t> actual_kinds;
        actuals.reserve(statement.task_arguments.size() - 1U);
        actual_kinds.reserve(statement.task_arguments.size() - 1U);
        for (std::size_t index = 1U;
            index < statement.task_arguments.size(); ++index) {
            const auto& operand = statement.task_arguments[index];
            if (is_string_expression(operand)) {
                const auto actual = lower_string_expression(operand);
                if (!actual)
                    return;
                actuals.push_back(*actual);
                actual_kinds.push_back(1U);
                continue;
            }
            const auto width = infer_width(operand).value_or(64U);
            const auto actual = lower_expression(operand, width);
            if (!actual)
                return;
            actuals.push_back(*actual);
            actual_kinds.push_back(0U);
        }
        auto names = statement.task_argument_names;
        if (names.size() == statement.task_arguments.size()) {
            names.erase(names.begin());
        }
        if (names.size() != actuals.size()) {
            names.assign(actuals.size(), std::string { });
        }
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ClassMethodCall {
            destination,
            *receiver,
            std::string { identity },
            std::move(actuals),
            std::move(names),
            std::vector<std::uint8_t>(
                statement.task_arguments.size() - 1U,
                static_cast<std::uint8_t>(
                    frontend::PortDirection::Input)),
            1,
            true,
            std::move(actual_kinds) });
        return;
    }
    if (!task_support_initialized_) {
        report(
            "FSIM-ELAB-SVTASK-001",
            "unknown task '" + statement.task_name + "'",
            statement.span);
        return;
    }
    const auto found = task_indices_.find(statement.task_name);
    if (found == task_indices_.end()) {
        report(
            "FSIM-ELAB-SVTASK-001",
            "unknown task '" + statement.task_name + "'",
            statement.span);
        return;
    }
    const auto task_index = found->second;
    auto& frame = task_frames_[task_index];
    const auto& task = *frame.source;
    if (task_suspending_[task_index]
        && (process_kind_ == frontend::ProcessKind::Final
            || process_kind_
                == frontend::ProcessKind::SystemVerilogAlwaysComb
            || process_kind_
                == frontend::ProcessKind::SystemVerilogAlwaysLatch)) {
        report(
            "FSIM-ELAB-SVTASK-010",
            "suspending task '" + task.name
                + "' cannot be called from final, always_comb, "
                  "or always_latch",
            statement.span);
        return;
    }
    const bool has_defaults = std::ranges::any_of(
        task.arguments,
        [](const frontend::TaskArgument& argument) {
            return argument.default_value.has_value();
        });
    if (statement.task_argument_names.empty()
        && !has_defaults
        && statement.task_arguments.size() != task.arguments.size()) {
        report(
            "FSIM-ELAB-SVTASK-005",
            "task '" + task.name + "' expects "
                + std::to_string(task.arguments.size())
                + " arguments but received "
                + std::to_string(statement.task_arguments.size()),
            statement.span);
        return;
    }
    const auto actuals = bind_task_actuals(statement, task);
    if (!actuals) {
        return;
    }
    for (std::size_t index = 0;
        index < task.arguments.size(); ++index) {
        if (!task.arguments[index].reference) {
            continue;
        }
        const auto& actual = *(*actuals)[index];
        const auto writable = [&](const auto& self,
                                  const Expression& candidate) -> bool {
            if (candidate.kind == ExpressionKind::Identifier) {
                return locals_.contains(candidate.text)
                    || string_locals_.contains(candidate.text)
                    || container_locals_.contains(candidate.text)
                    || signals_.contains(candidate.text)
                    || packed_member_reference(candidate.text).has_value();
            }
            return ((candidate.kind == ExpressionKind::Index
                        && candidate.operands.size() == 2)
                       || (candidate.kind == ExpressionKind::Slice
                           && candidate.operands.size() == 3))
                && self(self, candidate.operands.front());
        };
        if (!task.automatic || !writable(writable, actual)) {
            report(
                "FSIM-ELAB-SVTASK-013",
                "ref task arguments require an automatic task and a "
                "writable variable actual",
                actual.span);
            return;
        }
    }

    if (!frame.allocated) {
        frame.arguments.reserve(task.arguments.size());
        frame.string_arguments.reserve(task.arguments.size());
        frame.container_arguments.reserve(task.arguments.size());
        frame.container_output_defaults.reserve(
            task.arguments.size());
        frame.argument_is_string.reserve(task.arguments.size());
        frame.argument_is_container.reserve(task.arguments.size());
        for (const auto& argument : task.arguments) {
            if (argument.type.systemverilog_container) {
                const auto type = container_type(argument.type, argument.span);
                if (!type) {
                    return;
                }
                frame.arguments.push_back({ });
                frame.string_arguments.push_back({ });
                frame.container_arguments.push_back(
                    allocate_container_register(*type));
                frame.container_output_defaults.push_back(
                    argument.direction
                                == frontend::PortDirection::Output
                            && type->fixed
                        ? std::optional<ContainerRegisterId> {
                              allocate_container_register(*type) }
                        : std::nullopt);
                frame.argument_is_string.push_back(false);
                frame.argument_is_container.push_back(true);
                continue;
            }
            if (argument.type.domain
                == frontend::ValueDomain::String) {
                frame.arguments.push_back({ });
                frame.string_arguments.push_back(
                    allocate_string_register());
                frame.container_arguments.push_back({ });
                frame.container_output_defaults.push_back(
                    std::nullopt);
                frame.argument_is_string.push_back(true);
                frame.argument_is_container.push_back(false);
                continue;
            }
            const auto width = argument.type.width();
            if (!width || *width == 0) {
                report(
                    "FSIM-ELAB-SVTASK-006",
                    "task argument '" + argument.name
                        + "' must have a positive executable width",
                    argument.span);
                return;
            }
            frame.arguments.push_back(allocate_register(
                static_cast<std::size_t>(*width),
                argument.type.domain));
            frame.string_arguments.push_back({ });
            frame.container_arguments.push_back({ });
            frame.container_output_defaults.push_back(
                std::nullopt);
            frame.argument_is_string.push_back(false);
            frame.argument_is_container.push_back(false);
        }
        if (task.automatic) {
            for (std::size_t index = 0;
                index < task.arguments.size(); ++index) {
                if (frame.argument_is_container[index]) {
                    frame.invocation_containers.push_back(
                        frame.container_arguments[index]);
                } else if (frame.argument_is_string[index]) {
                    frame.invocation_strings.push_back(
                        frame.string_arguments[index]);
                } else {
                    frame.invocation_packed.push_back(frame.arguments[index]);
                }
            }
        }
        frame.allocated = true;
    }

    std::vector<Expression> copy_out_targets(task.arguments.size());
    for (std::size_t index = 0;
        index < task.arguments.size(); ++index) {
        if (task.arguments[index].direction
            == frontend::PortDirection::Input) {
            continue;
        }
        auto target = capture_callable_copy_out_target(
            *(*actuals)[index],
            "@task_target_" + std::to_string(task_index)
                + "_" + std::to_string(index)
                + "_" + std::to_string(process_.operations.size()));
        if (!target) {
            return;
        }
        copy_out_targets[index] = std::move(*target);
    }

    std::vector<RegisterId> packed_actuals(task.arguments.size());
    std::vector<StringRegisterId> string_actuals(task.arguments.size());
    std::vector<ContainerRegisterId> container_actuals(
        task.arguments.size());
    for (std::size_t index = 0;
        index < task.arguments.size(); ++index) {
        const auto& formal = task.arguments[index];
        if (formal.direction != frontend::PortDirection::Output
            && !validate_sv_nominal_assignment(
                &formal.type, *(*actuals)[index])) {
            return;
        }
        if (frame.argument_is_container[index]) {
            const auto formal_type = container_type(formal.type, formal.span);
            if (!formal_type) {
                return;
            }
            container_actuals[index] = allocate_container_register(*formal_type);
            if (formal.direction
                == frontend::PortDirection::Output) {
                continue;
            }
            const auto actual = formal_type->fixed
                ? lower_static_container_assignment_value(
                      *(*actuals)[index],
                      *formal_type)
                : lower_container_expression(
                      *(*actuals)[index]);
            if (!actual) {
                return;
            }
            if (process_.container_register_types.at(*actual)
                != *formal_type) {
                report(
                    "FSIM-ELAB-SVTASK-011",
                    "task container input/inout arguments require an "
                    "exactly compatible kind and profile",
                    (*actuals)[index]->span);
                return;
            }
            process_.operations.emplace_back(CopyContainerRegister {
                container_actuals[index], *actual });
            continue;
        }
        if (frame.argument_is_string[index]) {
            string_actuals[index] = allocate_string_register();
            if (formal.direction
                == frontend::PortDirection::Output) {
                continue;
            }
            const auto actual = lower_string_expression(
                *(*actuals)[index]);
            if (!actual) {
                return;
            }
            process_.operations.emplace_back(CopyStringRegister {
                string_actuals[index], *actual });
            continue;
        }
        const auto width = static_cast<std::size_t>(*formal.type.width());
        packed_actuals[index] = allocate_register(width, formal.type.domain);
        if (formal.direction == frontend::PortDirection::Output) {
            continue;
        }
        auto actual = lower_expression(
            *(*actuals)[index], width, &formal.type);
        if (!actual) {
            return;
        }
        if (register_width(*actual) != width) {
            *actual = resize_register(
                *actual,
                width,
                is_signed_expression(
                    *(*actuals)[index]));
        }
        process_.operations.emplace_back(
            CopyRegister { packed_actuals[index], *actual });
    }

    if (task.automatic) {
        const auto push_site = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(CallableFramePush {
            frame.invocation_identity,
            frame.invocation_packed,
            frame.invocation_strings,
            frame.invocation_containers });
        if (!frame.invocation_layout_finalized) {
            frame.invocation_push_sites.push_back(push_site);
        }
    }

    for (std::size_t index = 0;
        index < task.arguments.size(); ++index) {
        const auto& formal = task.arguments[index];
        if (frame.argument_is_container[index]) {
            if (formal.direction == frontend::PortDirection::Output) {
                if (const auto reset = frame.container_output_defaults[index]) {
                    process_.operations.emplace_back(CopyContainerRegister {
                        frame.container_arguments[index], *reset });
                } else {
                    process_.operations.emplace_back(DeleteContainer {
                        frame.container_arguments[index], std::nullopt });
                }
            } else {
                process_.operations.emplace_back(CopyContainerRegister {
                    frame.container_arguments[index], container_actuals[index] });
            }
        } else if (frame.argument_is_string[index]) {
            if (formal.direction == frontend::PortDirection::Output) {
                process_.operations.emplace_back(LoadStringConstant {
                    frame.string_arguments[index], { } });
            } else {
                process_.operations.emplace_back(CopyStringRegister {
                    frame.string_arguments[index], string_actuals[index] });
            }
        } else if (formal.direction == frontend::PortDirection::Output) {
            const auto width = static_cast<std::size_t>(*formal.type.width());
            process_.operations.emplace_back(LoadConstant {
                frame.arguments[index],
                default_packed_value(formal.type, width) });
        } else {
            process_.operations.emplace_back(CopyRegister {
                frame.arguments[index], packed_actuals[index] });
        }
    }

    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call {
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        task_call_stack_ });
    if (frame.target) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target = *frame.target;
    } else {
        frame.call_sites.push_back(call_site);
    }
    if (!frame.queued && !frame.lowered) {
        frame.queued = true;
        pending_tasks_.push_back(task_index);
    }
    if (active_task_) {
        task_dependencies_[*active_task_].insert(task_index);
    }

    for (std::size_t index = 0;
        index < task.arguments.size(); ++index) {
        if (task.arguments[index].direction
            == frontend::PortDirection::Input) {
            continue;
        }
        if (frame.argument_is_container[index]) {
            process_.operations.emplace_back(CopyContainerRegister {
                container_actuals[index], frame.container_arguments[index] });
        } else if (frame.argument_is_string[index]) {
            process_.operations.emplace_back(CopyStringRegister {
                string_actuals[index], frame.string_arguments[index] });
        } else {
            process_.operations.emplace_back(CopyRegister {
                packed_actuals[index], frame.arguments[index] });
        }
    }
    if (task.automatic) {
        std::vector<RegisterId> preserve_packed;
        std::vector<StringRegisterId> preserve_strings;
        std::vector<ContainerRegisterId> preserve_containers;
        for (std::size_t index = 0;
            index < task.arguments.size(); ++index) {
            if (task.arguments[index].direction
                == frontend::PortDirection::Input) {
                continue;
            }
            if (frame.argument_is_container[index]) {
                preserve_containers.push_back(container_actuals[index]);
            } else if (frame.argument_is_string[index]) {
                preserve_strings.push_back(string_actuals[index]);
            } else {
                preserve_packed.push_back(packed_actuals[index]);
            }
        }
        process_.operations.emplace_back(CallableFramePop {
            frame.invocation_identity,
            std::move(preserve_packed),
            std::move(preserve_strings),
            std::move(preserve_containers) });
    }
    for (std::size_t index = 0;
        index < task.arguments.size(); ++index) {
        const auto& formal = task.arguments[index];
        if (formal.direction == frontend::PortDirection::Input) {
            continue;
        }
        lower_callable_copy_out(
            copy_out_targets[index],
            formal.type,
            packed_actuals[index],
            string_actuals[index],
            container_actuals[index],
            frame.argument_is_string[index],
            frame.argument_is_container[index],
            "@task_copyout_" + std::to_string(task_index)
                + "_" + std::to_string(index));
    }
}

void Lowerer::lower_task_return(const Statement& statement)
{
    if (!active_task_) {
        report(
            "FSIM-ELAB-SVTASK-007",
            "task return statement is outside an executable task",
            statement.span);
        return;
    }
    task_return_jumps_.push_back(
        static_cast<InstructionIndex>(
            process_.operations.size()));
    process_.operations.emplace_back(Jump { });
}

void Lowerer::lower_task_body(const std::size_t task_index)
{
    auto& frame = task_frames_[task_index];
    if (frame.lowered || !frame.allocated) {
        return;
    }
    frame.target = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto call_site : frame.call_sites) {
        fsim::runtime::simir::operation_get<Call>(process_.operations[call_site]).target = *frame.target;
    }
    frame.call_sites.clear();
    frame.lowered = true;

    auto saved_locals = std::move(locals_);
    auto saved_string_locals = std::move(string_locals_);
    auto saved_container_locals = std::move(container_locals_);
    auto saved_signed = std::move(local_signed_);
    auto saved_ranges = std::move(local_ranges_);
    auto saved_integer_ranges = std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    auto saved_loop_controls = std::move(loop_controls_);
    auto saved_block_controls = std::move(block_controls_);
    auto saved_named_block_controls = std::move(named_block_controls_);
    auto saved_named_fork_controls = std::move(named_fork_controls_);
    auto saved_return_jumps = std::move(task_return_jumps_);
    const auto saved_active = active_task_;
    locals_.clear();
    string_locals_.clear();
    container_locals_.clear();
    local_signed_.clear();
    local_ranges_.clear();
    local_integer_ranges_.clear();
    local_members_.clear();
    local_types_.clear();
    local_scope_ = { frame.source->name };
    loop_controls_.clear();
    block_controls_.clear();
    named_block_controls_.clear();
    named_fork_controls_.clear();
    task_return_jumps_.clear();
    active_task_ = task_index;

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
    for (std::size_t index = 0;
        index < frame.source->arguments.size(); ++index) {
        const auto& argument = frame.source->arguments[index];
        if (frame.argument_is_container[index]) {
            container_locals_.insert_or_assign(
                argument.name,
                frame.container_arguments[index]);
            local_types_.insert_or_assign(
                argument.name, &argument.type);
            const auto type = container_type(argument.type, argument.span);
            if (type) {
                process_.debug_container_locals.push_back(
                    DebugContainerLocal {
                        scoped_local_name(argument.name),
                        frame.container_arguments[index],
                        *type,
                        SourceLocation {
                            argument.span.source_name,
                            static_cast<std::uint32_t>(
                                argument.span.begin.line),
                            static_cast<std::uint32_t>(
                                argument.span.begin.column) } });
            }
            continue;
        }
        if (frame.argument_is_string[index]) {
            string_locals_.insert_or_assign(
                argument.name, frame.string_arguments[index]);
            local_types_.insert_or_assign(
                argument.name, &argument.type);
            process_.debug_string_locals.push_back(
                DebugStringLocal {
                    scoped_local_name(argument.name),
                    frame.string_arguments[index],
                    SourceLocation {
                        argument.span.source_name,
                        static_cast<std::uint32_t>(
                            argument.span.begin.line),
                        static_cast<std::uint32_t>(
                            argument.span.begin.column) } });
            continue;
        }
        bind(
            argument.name,
            argument.type,
            frame.arguments[index]);
        auto debug_name = scoped_local_name(argument.name);
        if (!debug_local_names_.emplace(debug_name).second) {
            debug_name += "@"
                + std::to_string(argument.span.begin.line)
                + ":" + std::to_string(argument.span.begin.column);
            debug_local_names_.emplace(debug_name);
        }
        process_.debug_locals.push_back(DebugLocal {
            std::move(debug_name),
            argument.type.spelling,
            frame.arguments[index],
            static_cast<std::size_t>(*argument.type.width()),
            SourceLocation {
                argument.span.source_name,
                static_cast<std::uint32_t>(
                    argument.span.begin.line),
                static_cast<std::uint32_t>(
                    argument.span.begin.column) },
            { },
            { },
            value_kind(argument.type.domain),
            argument.type.enumeration_literals,
            argument.type.systemverilog_scalar });
        if (argument.type.integer_range) {
            const auto [lower, upper] = integer_bounds(argument.type.integer_range);
            process_.debug_locals.back().integer_lower = lower;
            process_.debug_locals.back().integer_upper = upper;
        }
    }
    if (frame.source->automatic) {
        const auto packed_begin = next_register_;
        const auto string_begin = next_string_register_;
        const auto container_begin = next_container_register_;
        initialize_variables(frame.source->variables);
        lower_statements(frame.source->statements);
        for (auto id = packed_begin; id < next_register_; ++id) {
            frame.invocation_packed.push_back(id);
        }
        for (auto id = string_begin; id < next_string_register_; ++id) {
            frame.invocation_strings.push_back(id);
        }
        for (auto id = container_begin;
            id < next_container_register_; ++id) {
            frame.invocation_containers.push_back(id);
        }
    } else {
        bind_static_callable_variables(
            frame.source->variables, frame.static_variables);
        lower_statements(frame.source->statements);
    }
    const auto epilogue = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto jump : task_return_jumps_) {
        process_.operations[jump] = Jump { epilogue };
    }
    process_.operations.emplace_back(Return { task_call_stack_ });
    for (const auto push_site : frame.invocation_push_sites) {
        process_.operations[push_site] = CallableFramePush {
            frame.invocation_identity,
            frame.invocation_packed,
            frame.invocation_strings,
            frame.invocation_containers
        };
    }
    frame.invocation_push_sites.clear();
    frame.invocation_layout_finalized = true;

    active_task_ = saved_active;
    task_return_jumps_ = std::move(saved_return_jumps);
    loop_controls_ = std::move(saved_loop_controls);
    block_controls_ = std::move(saved_block_controls);
    named_block_controls_ = std::move(saved_named_block_controls);
    named_fork_controls_ = std::move(saved_named_fork_controls);
    local_scope_ = std::move(saved_scope);
    local_types_ = std::move(saved_types);
    local_members_ = std::move(saved_members);
    local_integer_ranges_ = std::move(saved_integer_ranges);
    local_ranges_ = std::move(saved_ranges);
    local_signed_ = std::move(saved_signed);
    locals_ = std::move(saved_locals);
    string_locals_ = std::move(saved_string_locals);
    container_locals_ = std::move(saved_container_locals);
}

void Lowerer::lower_pending_tasks()
{
    while (!pending_tasks_.empty()) {
        const auto task = pending_tasks_.front();
        pending_tasks_.pop_front();
        task_frames_[task].queued = false;
        lower_task_body(task);
    }
}

} // namespace fsim::elaboration
