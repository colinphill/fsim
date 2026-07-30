// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void Lowerer::initialize_task_support() {
    task_frames_.clear();
    task_indices_.clear();
    pending_tasks_.clear();
    task_dependencies_.clear();
    task_suspending_.clear();
    active_task_.reset();
    task_return_jumps_.clear();
    task_call_stack_ = {};
    task_support_initialized_ = false;
    if (tasks_.empty()) {
        return;
    }
    if (tasks_.size()
        > std::numeric_limits<std::uint32_t>::max()) {
        report(
            "FSIM-ELAB-SVTASK-003",
            "too many visible tasks for the SimIR call stack",
            tasks_.front().span);
        return;
    }

    task_frames_.reserve(tasks_.size());
    for (const auto& task : tasks_) {
        const auto index = task_frames_.size();
        const auto [existing, inserted] =
            task_indices_.emplace(task.name, index);
        if (!inserted) {
            report(
                "FSIM-ELAB-SVTASK-004",
                "ambiguous visible task '" + task.name + "'",
                task.span);
            (void)existing;
            continue;
        }
        TaskFrame frame;
        frame.source = &task;
        task_frames_.push_back(std::move(frame));
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
                  const auto dependency =
                      task_indices_.find(statement.task_name);
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

    task_call_stack_.pointer =
        allocate_register(32, frontend::ValueDomain::Bit2);
    task_call_stack_.entries = next_register_;
    task_call_stack_.capacity =
        static_cast<std::uint32_t>(task_frames_.size());
    process_.operations.emplace_back(LoadConstant{
        task_call_stack_.pointer, unsigned_value(0, 32)});
    for (std::uint32_t index = 0;
         index < task_call_stack_.capacity; ++index) {
        const auto entry =
            allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant{entry, unsigned_value(0, 32)});
    }
    task_support_initialized_ = true;
}

void Lowerer::lower_task_call(const Statement& statement) {
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
    if (statement.task_arguments.size() != task.arguments.size()) {
        report(
            "FSIM-ELAB-SVTASK-005",
            "task '" + task.name + "' expects "
                + std::to_string(task.arguments.size())
                + " arguments but received "
                + std::to_string(statement.task_arguments.size()),
            statement.span);
        return;
    }

    if (!frame.allocated) {
        frame.arguments.reserve(task.arguments.size());
        frame.string_arguments.reserve(task.arguments.size());
        frame.container_arguments.reserve(task.arguments.size());
        frame.argument_is_string.reserve(task.arguments.size());
        frame.argument_is_container.reserve(task.arguments.size());
        for (const auto& argument : task.arguments) {
            if (argument.type.systemverilog_container) {
                const auto type =
                    container_type(argument.type, argument.span);
                if (!type) {
                    return;
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
                    "FSIM-ELAB-SVTASK-006",
                    "task argument '" + argument.name
                        + "' must have an executable width in [1, 64]",
                    argument.span);
                return;
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
         index < task.arguments.size(); ++index) {
        const auto& formal = task.arguments[index];
        if (frame.argument_is_container[index]) {
            if (formal.direction
                == frontend::PortDirection::Output) {
                process_.operations.emplace_back(
                    DeleteContainer{
                        frame.container_arguments[index],
                        std::nullopt});
                continue;
            }
            const auto actual =
                lower_container_expression(
                    statement.task_arguments[index]);
            if (!actual) {
                return;
            }
            process_.operations.emplace_back(
                CopyContainerRegister{
                    frame.container_arguments[index], *actual});
            continue;
        }
        if (frame.argument_is_string[index]) {
            if (formal.direction
                == frontend::PortDirection::Output) {
                process_.operations.emplace_back(
                    LoadStringConstant{
                        frame.string_arguments[index], {}});
                continue;
            }
            const auto actual =
                lower_string_expression(
                    statement.task_arguments[index]);
            if (!actual) {
                return;
            }
            process_.operations.emplace_back(
                CopyStringRegister{
                    frame.string_arguments[index], *actual});
            continue;
        }
        const auto width =
            static_cast<std::size_t>(*formal.type.width());
        if (formal.direction == frontend::PortDirection::Output) {
            process_.operations.emplace_back(LoadConstant{
                frame.arguments[index],
                default_packed_value(formal.type, width)});
            continue;
        }
        auto actual = lower_expression(
            statement.task_arguments[index], width, &formal.type);
        if (!actual) {
            return;
        }
        if (register_width(*actual) != width) {
            *actual = resize_register(
                *actual,
                width,
                is_signed_expression(
                    statement.task_arguments[index]));
        }
        process_.operations.emplace_back(
            CopyRegister{frame.arguments[index], *actual});
    }

    const auto call_site = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Call{
        frame.target.value_or(0),
        static_cast<InstructionIndex>(call_site + 1U),
        task_call_stack_});
    if (frame.target) {
        std::get<Call>(process_.operations[call_site]).target =
            *frame.target;
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
        const auto& formal = task.arguments[index];
        if (formal.direction == frontend::PortDirection::Input) {
            continue;
        }
        const auto temporary =
            "@task_copyout_" + std::to_string(task_index)
            + "_" + std::to_string(index);
        if (frame.argument_is_container[index]) {
            container_locals_.insert_or_assign(
                temporary, frame.container_arguments[index]);
            local_types_.insert_or_assign(
                temporary, &formal.type);
            Statement copy_out;
            copy_out.kind = StatementKind::Assignment;
            copy_out.assignment_kind = AssignmentKind::Blocking;
            copy_out.target = statement.task_arguments[index];
            copy_out.value = Expression{
                ExpressionKind::Identifier,
                temporary,
                {},
                statement.span};
            copy_out.span = statement.span;
            lower_assignment(copy_out);
            continue;
        }
        if (frame.argument_is_string[index]) {
            string_locals_.insert_or_assign(
                temporary, frame.string_arguments[index]);
            local_types_.insert_or_assign(
                temporary, &formal.type);
            Statement copy_out;
            copy_out.kind = StatementKind::Assignment;
            copy_out.assignment_kind = AssignmentKind::Blocking;
            copy_out.target = statement.task_arguments[index];
            copy_out.value = Expression{
                ExpressionKind::Identifier,
                temporary,
                {},
                statement.span};
            copy_out.span = statement.span;
            lower_assignment(copy_out);
            continue;
        }
        locals_.insert_or_assign(
            temporary, frame.arguments[index]);
        local_signed_.insert_or_assign(
            temporary, formal.type.is_signed);
        local_ranges_.insert_or_assign(
            temporary, formal.type.packed_range);
        local_integer_ranges_.insert_or_assign(
            temporary, formal.type.integer_range);
        local_members_.insert_or_assign(
            temporary, formal.type.packed_members);
        local_types_.insert_or_assign(temporary, &formal.type);

        Statement copy_out;
        copy_out.kind = StatementKind::Assignment;
        copy_out.assignment_kind = AssignmentKind::Blocking;
        copy_out.target = statement.task_arguments[index];
        copy_out.value = Expression{
            ExpressionKind::Identifier,
            temporary,
            {},
            statement.span};
        copy_out.span = statement.span;
        lower_assignment(copy_out);
    }
}

void Lowerer::lower_task_return(const Statement& statement) {
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
    process_.operations.emplace_back(Jump{});
}

void Lowerer::lower_task_body(const std::size_t task_index) {
    auto& frame = task_frames_[task_index];
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
    auto saved_integer_ranges =
        std::move(local_integer_ranges_);
    auto saved_members = std::move(local_members_);
    auto saved_types = std::move(local_types_);
    auto saved_scope = std::move(local_scope_);
    auto saved_loop_controls = std::move(loop_controls_);
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
    local_scope_ = {frame.source->name};
    loop_controls_.clear();
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
            const auto type =
                container_type(argument.type, argument.span);
            if (type) {
                process_.debug_container_locals.push_back(
                    DebugContainerLocal{
                        scoped_local_name(argument.name),
                        frame.container_arguments[index],
                        *type,
                        SourceLocation{
                            argument.span.source_name,
                            static_cast<std::uint32_t>(
                                argument.span.begin.line),
                            static_cast<std::uint32_t>(
                                argument.span.begin.column)}});
            }
            continue;
        }
        if (frame.argument_is_string[index]) {
            string_locals_.insert_or_assign(
                argument.name, frame.string_arguments[index]);
            local_types_.insert_or_assign(
                argument.name, &argument.type);
            process_.debug_string_locals.push_back(
                DebugStringLocal{
                    scoped_local_name(argument.name),
                    frame.string_arguments[index],
                    SourceLocation{
                        argument.span.source_name,
                        static_cast<std::uint32_t>(
                            argument.span.begin.line),
                        static_cast<std::uint32_t>(
                            argument.span.begin.column)}});
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
                + ":" + std::to_string(
                    argument.span.begin.column);
            debug_local_names_.emplace(debug_name);
        }
        process_.debug_locals.push_back(DebugLocal{
            std::move(debug_name),
            argument.type.spelling,
            frame.arguments[index],
            static_cast<std::size_t>(*argument.type.width()),
            SourceLocation{
                argument.span.source_name,
                static_cast<std::uint32_t>(
                    argument.span.begin.line),
                static_cast<std::uint32_t>(
                    argument.span.begin.column)},
            {},
            {},
            value_kind(argument.type.domain),
            argument.type.enumeration_literals});
        if (argument.type.integer_range) {
            const auto [lower, upper] =
                integer_bounds(argument.type.integer_range);
            process_.debug_locals.back().integer_lower = lower;
            process_.debug_locals.back().integer_upper = upper;
        }
    }
    initialize_variables(frame.source->variables);
    lower_statements(frame.source->statements);
    const auto epilogue = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto jump : task_return_jumps_) {
        process_.operations[jump] = Jump{epilogue};
    }
    process_.operations.emplace_back(Return{task_call_stack_});

    active_task_ = saved_active;
    task_return_jumps_ = std::move(saved_return_jumps);
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

void Lowerer::diagnose_task_cycles() {
    std::vector<std::uint8_t> state(task_frames_.size(), 0);
    const auto visit =
        [&](const auto& self, const std::size_t index) -> bool {
          if (state[index] == 1) {
              report(
                  "FSIM-ELAB-SVTASK-008",
                  "recursive task call graph involving '"
                      + task_frames_[index].source->name
                      + "' is not supported",
                  task_frames_[index].source->span);
              return true;
          }
          if (state[index] == 2) {
              return false;
          }
          state[index] = 1;
          for (const auto dependency :
               task_dependencies_[index]) {
              if (self(self, dependency)) {
                  state[index] = 2;
                  return true;
              }
          }
          state[index] = 2;
          return false;
        };
    for (std::size_t index = 0;
         index < task_frames_.size(); ++index) {
        if (state[index] == 0) {
            (void)visit(visit, index);
        }
    }
}

void Lowerer::lower_pending_tasks() {
    while (!pending_tasks_.empty()) {
        const auto task = pending_tasks_.front();
        pending_tasks_.pop_front();
        task_frames_[task].queued = false;
        lower_task_body(task);
    }
    diagnose_task_cycles();
}

} // namespace fsim::elaboration
