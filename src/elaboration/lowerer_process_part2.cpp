// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
#include "lowerer_driver_regions.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    [[nodiscard]] runtime::simir::OutputFormat runtime_output_format(
        const frontend::OutputFormat source)
    {
        switch (source) {
        case frontend::OutputFormat::Binary:
            return runtime::simir::OutputFormat::binary;
        case frontend::OutputFormat::Hexadecimal:
            return runtime::simir::OutputFormat::hexadecimal;
        case frontend::OutputFormat::Octal:
            return runtime::simir::OutputFormat::octal;
        case frontend::OutputFormat::Decimal:
            return runtime::simir::OutputFormat::decimal;
        case frontend::OutputFormat::Character:
            return runtime::simir::OutputFormat::character;
        case frontend::OutputFormat::String:
            return runtime::simir::OutputFormat::string;
        case frontend::OutputFormat::RealScientific:
            return runtime::simir::OutputFormat::real_scientific;
        case frontend::OutputFormat::RealFixed:
            return runtime::simir::OutputFormat::real_fixed;
        case frontend::OutputFormat::RealGeneral:
            return runtime::simir::OutputFormat::real_general;
        case frontend::OutputFormat::Time:
            return runtime::simir::OutputFormat::time;
        case frontend::OutputFormat::Unformatted2:
            return runtime::simir::OutputFormat::unformatted2;
        case frontend::OutputFormat::Unformatted4:
            return runtime::simir::OutputFormat::unformatted4;
        case frontend::OutputFormat::Hierarchy:
            break;
        }
        throw std::logic_error { "invalid frontend output format" };
    }

} // namespace


void Lowerer::lower_case_alternative(
    const frontend::CaseAlternative& alternative)
{
    const bool scoped = language_ == frontend::Language::Vhdl2008;
    if (scoped) {
        local_scope_.push_back(
            "$when_"
            + std::to_string(alternative.span.begin.line) + "_"
            + std::to_string(alternative.span.begin.column) + "_"
            + std::to_string(alternative.span.begin.offset));
    }
    lower_statements(alternative.statements);
    if (scoped) {
        local_scope_.pop_back();
    }
}

void Lowerer::lower_block(const Statement& statement)
{
    auto outer_locals = locals_;
    auto outer_string_locals = string_locals_;
    auto outer_container_locals = container_locals_;
    auto outer_signed = local_signed_;
    auto outer_ranges = local_ranges_;
    auto outer_integer_ranges = local_integer_ranges_;
    auto outer_members = local_members_;
    auto outer_types = local_types_;
    const auto declaration_scope = local_scope_;
    const auto block_begin = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto function_returns_begin = function_return_jumps_.size();
    const auto procedure_returns_begin = procedure_return_jumps_.size();
    local_scope_.push_back(block_scope_name(statement));
    const bool named = !statement.label.empty();
    std::optional<std::size_t> named_control_index;
    if (named) {
        block_controls_.push_back({ statement.label, { }, std::nullopt });
        named_control_index = named_block_controls_.size();
        named_block_controls_.push_back(
            { statement.label, declaration_scope, block_begin, std::nullopt, { } });
    }
    const bool automatic_scope = !(active_function_
                                     && !function_frames_[*active_function_].source->automatic)
        && !(active_task_
            && !task_frames_[*active_task_].source->automatic);
    if (active_function_
        && !function_frames_[*active_function_].source->automatic) {
        bind_static_callable_variables(
            statement.declarations,
            function_frames_[*active_function_].static_variables);
    } else if (
        active_task_
        && !task_frames_[*active_task_].source->automatic) {
        bind_static_callable_variables(
            statement.declarations,
            task_frames_[*active_task_].static_variables);
    } else {
        initialize_variables(statement.declarations);
    }
    lower_statements(statement.statements);
    if (named) {
        auto control = std::move(block_controls_.back());
        block_controls_.pop_back();
        const auto epilogue = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump : control.disable_jumps) {
            process_.operations[jump] = Jump { epilogue };
        }
        auto& named_control = named_block_controls_.at(*named_control_index);
        named_control.end = epilogue;
        for (const auto operation : named_control.disable_operations) {
            process_.operations[operation] = DisableBlock {
                named_control.begin, epilogue
            };
        }
    }
    const auto emit_cleanup = [&] {
        for (const auto& variable : statement.declarations) {
            if (variable.vhdl_file || variable.type.vhdl_file) {
                const auto file = locals_.find(variable.name);
                if (file != locals_.end()) {
                    process_.operations.emplace_back(
                        FileClose { file->second, true, true });
                }
            }
        }
        if (automatic_scope) {
            emit_vhdl_access_scope_cleanup(statement.declarations);
        }
    };
    emit_cleanup();

    const auto route_returns_through_cleanup =
        [&](std::vector<InstructionIndex>& returns,
            const std::size_t first) {
            if (language_ != frontend::Language::Vhdl2008
                || first >= returns.size()) {
                return;
            }
            const auto normal_exit = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { });
            const auto return_cleanup = static_cast<InstructionIndex>(
                process_.operations.size());
            emit_cleanup();
            const auto continuation = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(Jump { });
            for (auto index = first; index < returns.size(); ++index) {
                process_.operations[returns[index]] = Jump {
                    return_cleanup
                };
            }
            returns.erase(
                returns.begin()
                    + static_cast<std::ptrdiff_t>(first),
                returns.end());
            returns.push_back(continuation);
            process_.operations[normal_exit] = Jump {
                static_cast<InstructionIndex>(
                    process_.operations.size())
            };
        };
    if (active_function_) {
        route_returns_through_cleanup(
            function_return_jumps_, function_returns_begin);
    } else if (active_procedure_) {
        route_returns_through_cleanup(
            procedure_return_jumps_, procedure_returns_begin);
    }
    local_scope_.pop_back();
    locals_ = std::move(outer_locals);
    string_locals_ = std::move(outer_string_locals);
    container_locals_ = std::move(outer_container_locals);
    local_signed_ = std::move(outer_signed);
    local_ranges_ = std::move(outer_ranges);
    local_integer_ranges_ = std::move(outer_integer_ranges);
    local_members_ = std::move(outer_members);
    local_types_ = std::move(outer_types);
}

void Lowerer::lower_event_trigger(const Statement& statement)
{
    if (statement.target.kind != ExpressionKind::Identifier) {
        report(
            "FSIM-ELAB-099",
            "named-event trigger requires a simple event name",
            statement.span);
        return;
    }
    const auto found = signals_.find(statement.target.text);
    if (found == signals_.end()) {
        report(
            "FSIM-ELAB-100",
            "unknown named event '" + statement.target.text + "'",
            statement.target.span);
        return;
    }
    const auto& info = design_.signal_info_.at(found->second);
    if (info.type_name != "event") {
        report(
            "FSIM-ELAB-101",
            "event trigger target '" + statement.target.text
                + "' is not declared as an event",
            statement.target.span);
        return;
    }
    const auto current = allocate_register(1, frontend::ValueDomain::Logic4);
    const auto toggled = allocate_register(1, frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(
        ReadSignal { current, found->second });
    process_.operations.emplace_back(
        UnaryNot { toggled, current });
    if (statement.assignment_kind
        == AssignmentKind::NonBlocking) {
        if (statement.delay) {
            process_.operations.emplace_back(
                WriteAfter {
                    found->second,
                    toggled,
                    statement.delay->magnitude });
        } else {
            process_.operations.emplace_back(
                WriteUpdate { found->second, toggled });
        }
    } else {
        process_.operations.emplace_back(
            WriteBlocking { found->second, toggled });
    }
}

const Statement* Lowerer::recognized_vhdl_edge_guard(
    const frontend::Process& source)
{
    if (source.statements.empty()) {
        return nullptr;
    }
    const auto& statement = source.statements.front();
    if (statement.kind != StatementKind::If
        || statement.condition.kind != ExpressionKind::Call
        || statement.condition.operands.size() != 1
        || statement.condition.operands.front().kind
            != ExpressionKind::Identifier) {
        return nullptr;
    }
    const auto& callee = statement.condition.text;
    const auto expected_edge = callee == "rising_edge"
        ? frontend::EdgeKind::Positive
        : callee == "falling_edge"
        ? frontend::EdgeKind::Negative
        : frontend::EdgeKind::Any;
    if (expected_edge == frontend::EdgeKind::Any) {
        return nullptr;
    }
    const auto& signal = statement.condition.operands.front().text;
    const auto matching = std::find_if(
        source.sensitivities.begin(),
        source.sensitivities.end(),
        [&](const frontend::Sensitivity& sensitivity) {
            return sensitivity.signal == signal
                && sensitivity.edge == expected_edge;
        });
    return matching == source.sensitivities.end() ? nullptr : &statement;
}

void Lowerer::lower_statements(const std::vector<Statement>& statements)
{
    for (const auto& statement : statements) {
        lower_statement(statement);
    }
}

void Lowerer::lower_statement(const Statement& statement)
{
    constexpr std::string_view concurrent_assertion_action_marker {
        "\x1f"
        "fsim.concurrent-assertion-action-region|"
    };
    if (statement.kind == StatementKind::Display
        && statement.output_text.starts_with(
            concurrent_assertion_action_marker)) {
        process_.operations.emplace_back(
            WaitRegion { runtime::SchedulerPhase::reactive });
        sample_concurrent_assertion_reads_ = false;
        return;
    }
    const auto statement_scope = vhdl_statement_scope_name(statement);
    if (!statement_scope.empty()) {
        local_scope_.push_back(statement_scope);
    }
    if (statement.kind != StatementKind::Block
        && !(statement.kind == StatementKind::Loop
            && statement.loop_runtime)) {
        auto kind = DebugPointKind::statement;
        if (statement.kind == StatementKind::Assert) {
            kind = DebugPointKind::assertion;
        } else if (
            statement.kind == StatementKind::Display
            || statement.kind == StatementKind::FileClose
            || statement.kind == StatementKind::FileFlush
            || statement.kind == StatementKind::FileDisplay
            || statement.kind == StatementKind::MemoryLoad
            || statement.kind == StatementKind::Report
            || statement.kind == StatementKind::TaskCall
            || statement.kind == StatementKind::ProcedureCall) {
            kind = DebugPointKind::call;
        } else if (
            statement.kind == StatementKind::Delay
            || statement.kind == StatementKind::WaitOn
            || statement.kind == StatementKind::WaitUntil
            || statement.kind == StatementKind::WaitOrder) {
            kind = DebugPointKind::wait;
        }
        emit_debug_point(kind, statement.span);
    }
    switch (statement.kind) {
    case StatementKind::Assignment:
        lower_assignment(statement);
        break;
    case StatementKind::Force:
    case StatementKind::Release:
        lower_force_release(statement);
        break;
    case StatementKind::ProceduralAssign:
    case StatementKind::Deassign:
        lower_procedural_continuous_assignment(statement);
        break;
    case StatementKind::ContainerMethod:
        lower_container_method(statement);
        break;
    case StatementKind::If:
        lower_if(statement);
        break;
    case StatementKind::Case:
        lower_case(statement);
        break;
    case StatementKind::Loop:
        lower_loop(statement);
        break;
    case StatementKind::Break:
        lower_loop_control(statement, true);
        break;
    case StatementKind::Continue:
        lower_loop_control(statement, false);
        break;
    case StatementKind::Return:
        if (active_task_) {
            lower_task_return(statement);
        } else if (active_procedure_) {
            lower_procedure_return(statement);
        } else {
            lower_function_return(statement);
        }
        break;
    case StatementKind::TaskCall:
        if (!lower_synchronization_method_statement(statement)
            && !lower_process_method_statement(statement)) {
            lower_task_call(statement);
        }
        break;
    case StatementKind::ProcedureCall:
        lower_procedure_call(statement);
        break;
    case StatementKind::Assert:
        lower_assert(statement);
        break;
    case StatementKind::Delay:
        if (!statement.delay) {
            report("FSIM-ELAB-030", "delay statement has no delay", statement.span);
            break;
        }
        if (lower_delay_wait(*statement.delay, statement.span)) {
            lower_statements(statement.statements);
        }
        break;
    case StatementKind::WaitOn: {
        if (emit_event_control_wait(statement)
            && statement.clocking_cycle_delay
            && !systemverilog_program_owner_) {
            // A clocking-cycle wait synchronizes module code with the
            // clocking block's observed input sample. Program processes are
            // already resumed in the reactive region; other processes need
            // an explicit region handoff before consuming clocking inputs or
            // scheduling clocking outputs.
            process_.operations.emplace_back(
                WaitRegion { runtime::SchedulerPhase::reactive });
        }
        lower_statements(statement.statements);
        break;
    }
    case StatementKind::WaitUntil:
        lower_wait_until(statement);
        break;
    case StatementKind::WaitOrder:
        lower_wait_order(statement);
        break;
    case StatementKind::EventTrigger:
        lower_event_trigger(statement);
        break;
    case StatementKind::MonitorControl:
        process_.operations.emplace_back(
            MonitorControl { statement.monitor_enabled });
        break;
    case StatementKind::FileClose: {
        if (!is_file_handle_expression(statement.file_handle)) {
            report(
                "FSIM-ELAB-SVFILE-001",
                "$fclose handle must be a 32-bit integer expression",
                statement.file_handle.span);
            break;
        }
        auto handle = lower_expression(statement.file_handle, 32);
        if (!handle) {
            break;
        }
        if (register_width(*handle) != 32) {
            *handle = resize_register(
                *handle,
                32,
                is_signed_expression(statement.file_handle));
        }
        process_.operations.emplace_back(FileClose { *handle });
        break;
    }
    case StatementKind::FileFlush: {
        if (statement.file_handle.kind == ExpressionKind::Invalid) {
            process_.operations.emplace_back(FileFlush { 0, true });
            break;
        }
        if (!is_file_handle_expression(statement.file_handle)) {
            report(
                "FSIM-ELAB-SVFILE-016",
                "$fflush handle must be a 32-bit integer expression",
                statement.file_handle.span);
            break;
        }
        auto handle = lower_expression(statement.file_handle, 32);
        if (!handle)
            break;
        if (register_width(*handle) != 32) {
            *handle = resize_register(
                *handle, 32,
                is_signed_expression(statement.file_handle));
        }
        process_.operations.emplace_back(FileFlush { *handle, false });
        break;
    }
    case StatementKind::FileDisplay: {
        if (!is_file_handle_expression(statement.file_handle)) {
            report(
                "FSIM-ELAB-SVFILE-001",
                "file output handle must be a 32-bit integer "
                "expression",
                statement.file_handle.span);
            break;
        }
        auto handle = lower_expression(statement.file_handle, 32);
        if (!handle) {
            break;
        }
        if (register_width(*handle) != 32) {
            *handle = resize_register(
                *handle,
                32,
                is_signed_expression(statement.file_handle));
        }
        const auto scalar_kind_of =
            [&](const Expression& value) {
                if (value.text == "$time") {
                    return frontend::SystemVerilogScalarKind::Time;
                }
                if (value.text == "$realtime") {
                    return frontend::SystemVerilogScalarKind::Realtime;
                }
                const auto* output_type = value.kind
                        == frontend::ExpressionKind::Identifier
                    ? object_type(value.text)
                    : nullptr;
                return output_type != nullptr
                    ? output_type->systemverilog_scalar
                    : value.systemverilog_scalar_kind;
            };
        const auto scalar_format_matches =
            [&](const Expression& value,
                const frontend::OutputFormat output_format) {
                const auto scalar_kind = scalar_kind_of(value);
                const bool real_kind = scalar_kind
                        == frontend::SystemVerilogScalarKind::ShortReal
                    || scalar_kind
                        == frontend::SystemVerilogScalarKind::Real
                    || scalar_kind
                        == frontend::SystemVerilogScalarKind::Realtime;
                const bool real_format = output_format
                        == frontend::OutputFormat::RealScientific
                    || output_format
                        == frontend::OutputFormat::RealFixed
                    || output_format
                        == frontend::OutputFormat::RealGeneral;
                return real_kind ? real_format
                    : scalar_kind
                        == frontend::SystemVerilogScalarKind::Time
                    ? output_format
                            == frontend::OutputFormat::Time
                        || output_format
                            == frontend::OutputFormat::Decimal
                    : scalar_kind
                        == frontend::SystemVerilogScalarKind::Chandle
                    ? output_format
                        == frontend::OutputFormat::Hexadecimal
                    : !real_format
                        && output_format
                            != frontend::OutputFormat::Time;
            };
        if (statement.output_postponed) {
            MonitorInstall monitor;
            monitor.file_handle = *handle;
            monitor.newline = statement.output_newline;
            monitor.one_shot = !statement.output_monitor;
            const auto task_name = statement.output_monitor
                ? "$fmonitor"
                : "$fstrobe";
            if (statement.output_values.empty()
                && !statement.output_format) {
                monitor.trailing_text = statement.output_text;
                process_.operations.emplace_back(std::move(monitor));
                break;
            }
            std::string pending_prefix;
            const auto append_value
                = [&](const frontend::OutputValue& output) {
                      auto prefix = std::move(pending_prefix)
                          + output.prefix;
                      if (output.format
                          == frontend::OutputFormat::Hierarchy) {
                          pending_prefix = std::move(prefix) + hierarchy_;
                          return;
                      }
                      MonitorValue value;
                      value.prefix = std::move(prefix);
                      value.minimum_width = output.minimum_width;
                      value.left_justify = output.left_justify;
                      value.zero_pad = output.zero_pad;
                      value.suppress_leading_zero
                          = output.suppress_leading_zero;
                      if (output.format
                          == frontend::OutputFormat::Time) {
                          value.kind = MonitorValueKind::time;
                          value.use_timeformat_width
                              = output.minimum_width == 0
                              && !output.suppress_leading_zero;
                      } else {
                          if (!scalar_format_matches(
                                  output.value, output.format)) {
                              report(
                                  "FSIM-ELAB-SVFILE-007",
                                  std::string { task_name }
                                      + " conversion is incompatible with the value type",
                                  output.value.span);
                              return;
                          }
                          if (output.value.kind
                              != frontend::ExpressionKind::Identifier) {
                              report(
                                  "FSIM-ELAB-SVFILE-007",
                                  std::string { task_name }
                                      + " requires direct packed-signal value expressions",
                                  output.value.span);
                              return;
                          }
                          const auto signal = signals_.find(
                              output.value.text);
                          if (signal == signals_.end()) {
                              report(
                                  "FSIM-ELAB-020",
                                  "unknown file-strobe signal '"
                                      + output.value.text + "'",
                                  output.value.span);
                              return;
                          }
                          value.kind = MonitorValueKind::signal;
                          value.signal = signal->second;
                          value.format = runtime_output_format(
                              output.format);
                          value.scalar_kind = scalar_kind_of(
                              output.value);
                          value.signed_decimal
                              = value.format
                                  == runtime::simir::OutputFormat::decimal
                              && is_signed_expression(output.value);
                      }
                      monitor.values.push_back(std::move(value));
                  };
            if (!statement.output_values.empty()) {
                for (const auto& output : statement.output_values) {
                    append_value(output);
                }
                monitor.trailing_text = std::move(pending_prefix)
                    + statement.output_trailing_text;
            } else {
                append_value(
                    frontend::OutputValue {
                        statement.value,
                        *statement.output_format,
                        statement.output_prefix,
                        statement.output_suppress_leading_zero,
                        statement.output_minimum_width,
                        statement.output_left_justify,
                        statement.output_zero_pad });
                monitor.trailing_text = std::move(pending_prefix)
                    + statement.output_suffix;
            }
            if (!monitor.values.empty()
                || !monitor.trailing_text.empty()) {
                process_.operations.emplace_back(std::move(monitor));
            }
            break;
        }
        const auto lower_output =
            [&](const frontend::OutputValue& output,
                const std::string& suffix,
                const bool newline) {
                if (output.format
                    == frontend::OutputFormat::Hierarchy) {
                    process_.operations.emplace_back(
                        FileWriteLiteral {
                            *handle,
                            output.prefix + hierarchy_ + suffix,
                            newline });
                    return;
                }
                if ((output.format
                            == frontend::OutputFormat::String
                        || output.format
                            == frontend::OutputFormat::Decimal)
                    && is_string_expression(output.value)) {
                    const auto source = lower_string_expression(output.value);
                    if (source) {
                        process_.operations.emplace_back(
                            FileWriteString {
                                *handle,
                                *source,
                                output.prefix,
                                suffix,
                                newline });
                    }
                    return;
                }
                if (!scalar_format_matches(
                        output.value, output.format)) {
                    report(
                        "FSIM-ELAB-SVFILE-007",
                        "formatted file conversion is incompatible with the value type",
                        output.value.span);
                    return;
                }
                const auto width = infer_width(output.value).value_or(std::size_t { 32 });
                const auto source = lower_expression(output.value, width);
                if (!source) {
                    report(
                        "FSIM-ELAB-SVFILE-007",
                        "formatted file output value cannot be lowered",
                        output.value.span);
                    return;
                }
                const auto format = runtime_output_format(output.format);
                process_.operations.emplace_back(
                    FileWriteFormatted {
                        *handle,
                        *source,
                        static_cast<std::uint32_t>(width),
                        format,
                        output.prefix,
                        suffix,
                        newline,
                        format
                                == runtime::simir::OutputFormat::decimal
                            && is_signed_expression(output.value),
                        output.suppress_leading_zero,
                        output.minimum_width,
                        output.left_justify,
                        output.zero_pad,
                        scalar_kind_of(output.value) });
            };
        if (!statement.output_values.empty()) {
            for (std::size_t index = 0;
                index < statement.output_values.size();
                ++index) {
                const bool last = index + 1 == statement.output_values.size();
                lower_output(
                    statement.output_values[index],
                    last ? statement.output_trailing_text
                         : std::string { },
                    last && statement.output_newline);
            }
            break;
        }
        if (!statement.output_format) {
            process_.operations.emplace_back(
                FileWriteLiteral {
                    *handle,
                    statement.output_text,
                    statement.output_newline });
            break;
        }
        lower_output(
            frontend::OutputValue {
                statement.value,
                *statement.output_format,
                statement.output_prefix,
                statement.output_suppress_leading_zero,
                statement.output_minimum_width,
                statement.output_left_justify,
                statement.output_zero_pad },
            statement.output_suffix,
            statement.output_newline);
        break;
    }
    case StatementKind::MemoryLoad: {
        const auto path = lower_string_expression(statement.value);
        if (!path) {
            report(
                "FSIM-ELAB-SVMEMORY-002",
                "memory-file name must be a string expression",
                statement.value.span);
            break;
        }
        if (statement.target.kind
            != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVMEMORY-003",
                "memory-file operand must be a direct static-array "
                "object",
                statement.target.span);
            break;
        }
        if (!statement.memory_write
            && read_only_container_objects_.contains(
                statement.target.text)) {
            report(
                "FSIM-ELAB-SVPORT-009",
                "an input container port is read-only within its "
                "module",
                statement.target.span);
            break;
        }
        const auto target = lower_container_expression(
            statement.target);
        const auto* source_type = object_type(statement.target.text);
        const auto runtime_type = source_type
            ? container_type(
                  *source_type,
                  statement.target.span)
            : std::nullopt;
        if (!target || !runtime_type) {
            break;
        }
        if (!runtime_type->fixed) {
            report(
                "FSIM-ELAB-SVMEMORY-003",
                "$readmem*/$writemem* requires a bounded "
                "static unpacked array",
                statement.target.span);
            break;
        }
        const auto lower_bound =
            [&](const Expression* expression)
            -> std::optional<RegisterId> {
            if (!expression) {
                return std::nullopt;
            }
            auto result = lower_expression(*expression, 32);
            if (!result) {
                return std::nullopt;
            }
            if (register_width(*result) != 32) {
                *result = resize_register(
                    *result, 32,
                    is_signed_expression(*expression));
            }
            return result;
        };
        const auto* start_expression = statement.task_arguments.empty()
            ? nullptr
            : &statement.task_arguments[0];
        const auto* finish_expression = statement.task_arguments.size() < 2
            ? nullptr
            : &statement.task_arguments[1];
        const auto start = lower_bound(start_expression);
        const auto finish = lower_bound(finish_expression);
        if ((start_expression && !start)
            || (finish_expression && !finish)) {
            report(
                "FSIM-ELAB-SVMEMORY-004",
                "memory-file start and finish must be integral "
                "expressions",
                statement.span);
            break;
        }
        process_.operations.emplace_back(
            LoadMemory {
                *target, *path, start, finish,
                statement.memory_hex,
                statement.memory_write });
        if (!statement.memory_write) {
            if (const auto object = container_objects_.find(
                    statement.target.text);
                object != container_objects_.end()) {
                process_.operations.emplace_back(
                    WriteContainerObject {
                        object->second, *target, std::nullopt });
            }
        }
        break;
    }
    case StatementKind::Display: {
        const auto scalar_kind_of = [&](const Expression& value) {
            if (value.text == "$time") {
                return frontend::SystemVerilogScalarKind::Time;
            }
            if (value.text == "$realtime") {
                return frontend::SystemVerilogScalarKind::Realtime;
            }
            const auto* type = value.kind == ExpressionKind::Identifier
                ? object_type(value.text)
                : nullptr;
            return type != nullptr
                ? type->systemverilog_scalar
                : value.systemverilog_scalar_kind;
        };
        const auto scalar_format_matches = [&](
                                               const Expression& value,
                                               const frontend::OutputFormat format) {
            const auto scalar = scalar_kind_of(value);
            const bool real_scalar = scalar == frontend::SystemVerilogScalarKind::ShortReal
                || scalar == frontend::SystemVerilogScalarKind::Real
                || scalar == frontend::SystemVerilogScalarKind::Realtime;
            const bool real_format = format == frontend::OutputFormat::RealScientific
                || format == frontend::OutputFormat::RealFixed
                || format == frontend::OutputFormat::RealGeneral;
            return real_scalar ? real_format
                : scalar == frontend::SystemVerilogScalarKind::Time
                ? format == frontend::OutputFormat::Decimal
                    || format == frontend::OutputFormat::Time
                : scalar == frontend::SystemVerilogScalarKind::Chandle
                ? format == frontend::OutputFormat::Hexadecimal
                : !real_format;
        };
        if (statement.output_monitor
            || statement.output_postponed) {
            if (statement.output_values.empty()
                && !statement.output_format) {
                process_.operations.emplace_back(
                    MonitorInstall {
                        { },
                        statement.output_text,
                        statement.output_newline,
                        statement.output_postponed
                            && !statement.output_monitor,
                        std::nullopt });
                break;
            }
            MonitorInstall monitor;
            monitor.newline = statement.output_newline;
            monitor.one_shot = statement.output_postponed
                && !statement.output_monitor;
            std::string pending_prefix;
            const auto append_value =
                [&](const frontend::OutputValue& output) {
                    auto prefix = std::move(pending_prefix) + output.prefix;
                    if (output.format
                        == frontend::OutputFormat::Hierarchy) {
                        pending_prefix = std::move(prefix) + hierarchy_;
                        return;
                    }
                    MonitorValue value;
                    value.prefix = std::move(prefix);
                    value.minimum_width = output.minimum_width;
                    value.left_justify = output.left_justify;
                    value.zero_pad = output.zero_pad;
                    value.suppress_leading_zero = output.suppress_leading_zero;
                    if (output.format
                        == frontend::OutputFormat::Time) {
                        value.kind = MonitorValueKind::time;
                        value.use_timeformat_width = output.minimum_width == 0
                            && !output.suppress_leading_zero;
                    } else {
                        if (!scalar_format_matches(
                                output.value, output.format)) {
                            report(
                                "FSIM-ELAB-102",
                                "formatted output conversion is incompatible with the value type",
                                output.value.span);
                            return;
                        }
                        if (output.value.kind
                            != frontend::ExpressionKind::Identifier) {
                            report(
                                monitor.one_shot
                                    ? "FSIM-ELAB-108"
                                    : "FSIM-ELAB-103",
                                monitor.one_shot
                                    ? "$strobe currently requires direct "
                                      "packed-signal value expressions"
                                    : "$monitor currently requires direct "
                                      "packed-signal value expressions",
                                output.value.span);
                            return;
                        }
                        const auto signal = signals_.find(output.value.text);
                        if (signal == signals_.end()) {
                            report(
                                "FSIM-ELAB-020",
                                "unknown monitor signal '"
                                    + output.value.text + "'",
                                output.value.span);
                            return;
                        }
                        value.kind = MonitorValueKind::signal;
                        value.signal = signal->second;
                        value.format = runtime_output_format(output.format);
                        value.scalar_kind = scalar_kind_of(output.value);
                        value.signed_decimal = value.format
                                == runtime::simir::OutputFormat::
                                    decimal
                            && is_signed_expression(output.value);
                    }
                    monitor.values.push_back(std::move(value));
                };
            if (!statement.output_values.empty()) {
                for (const auto& output :
                    statement.output_values) {
                    append_value(output);
                }
                monitor.trailing_text = std::move(pending_prefix)
                    + statement.output_trailing_text;
            } else {
                append_value(
                    frontend::OutputValue {
                        statement.value,
                        *statement.output_format,
                        statement.output_prefix,
                        statement.output_suppress_leading_zero,
                        statement.output_minimum_width,
                        statement.output_left_justify,
                        statement.output_zero_pad });
                monitor.trailing_text = std::move(pending_prefix)
                    + statement.output_suffix;
            }
            if (!monitor.values.empty()) {
                process_.operations.emplace_back(
                    std::move(monitor));
            }
            break;
        }
        if (!statement.output_values.empty()) {
            for (std::size_t index = 0;
                index < statement.output_values.size();
                ++index) {
                const auto& output = statement.output_values[index];
                const bool last = index + 1 == statement.output_values.size();
                if (output.format
                    == frontend::OutputFormat::Hierarchy) {
                    process_.operations.emplace_back(
                        Display {
                            output.prefix + hierarchy_
                                + (last
                                        ? statement.output_trailing_text
                                        : std::string { }),
                            last && statement.output_newline,
                            statement.output_postponed });
                    continue;
                }
                if (output.format == frontend::OutputFormat::Time
                    && (!output.value.valid()
                        || output.value.text == "$time")) {
                    process_.operations.emplace_back(
                        TimeDisplay {
                            output.prefix,
                            last
                                ? statement.output_trailing_text
                                : std::string { },
                            last && statement.output_newline,
                            statement.output_postponed,
                            output.minimum_width,
                            output.left_justify,
                            output.zero_pad,
                            output.minimum_width == 0
                                && !output.suppress_leading_zero });
                    continue;
                }
                if (output.format
                        == frontend::OutputFormat::String
                    && is_string_expression(output.value)) {
                    const auto source = lower_string_expression(output.value);
                    if (!source) {
                        continue;
                    }
                    process_.operations.emplace_back(
                        StringDisplay {
                            *source,
                            output.prefix,
                            last
                                ? statement.output_trailing_text
                                : std::string { },
                            last && statement.output_newline,
                            statement.output_postponed });
                    continue;
                }
                if (!scalar_format_matches(
                        output.value, output.format)) {
                    report(
                        "FSIM-ELAB-102",
                        "formatted output conversion is incompatible with the value type",
                        output.value.span);
                    continue;
                }
                const auto width = infer_width(output.value)
                                       .value_or(std::size_t { 32 });
                const auto source = lower_expression(output.value, width);
                if (!source) {
                    report(
                        "FSIM-ELAB-102",
                        "formatted output value cannot be lowered",
                        output.value.span);
                    continue;
                }
                const auto format = runtime_output_format(output.format);
                process_.operations.emplace_back(
                    FormatDisplay {
                        *source,
                        format,
                        output.prefix,
                        last
                            ? statement.output_trailing_text
                            : std::string { },
                        last && statement.output_newline,
                        statement.output_postponed,
                        format
                                == runtime::simir::OutputFormat::decimal
                            && is_signed_expression(output.value),
                        output.suppress_leading_zero,
                        output.minimum_width,
                        output.left_justify,
                        output.zero_pad,
                        scalar_kind_of(output.value) });
            }
            break;
        }
        if (statement.output_format) {
            if (*statement.output_format
                    == frontend::OutputFormat::String
                && is_string_expression(statement.value)) {
                const auto source = lower_string_expression(statement.value);
                if (source) {
                    process_.operations.emplace_back(
                        StringDisplay {
                            *source,
                            statement.output_prefix,
                            statement.output_suffix,
                            statement.output_newline,
                            statement.output_postponed });
                }
                break;
            }
            if (!scalar_format_matches(
                    statement.value, *statement.output_format)) {
                report(
                    "FSIM-ELAB-102",
                    "formatted output conversion is incompatible with the value type",
                    statement.value.span);
                break;
            }
            const auto width = infer_width(statement.value)
                                   .value_or(std::size_t { 32 });
            const auto source = lower_expression(statement.value, width);
            if (!source) {
                report(
                    "FSIM-ELAB-102",
                    "formatted output value cannot be lowered",
                    statement.span);
                break;
            }
            const auto format = runtime_output_format(*statement.output_format);
            process_.operations.emplace_back(
                FormatDisplay {
                    *source,
                    format,
                    statement.output_prefix,
                    statement.output_suffix,
                    statement.output_newline,
                    statement.output_postponed,
                    format == runtime::simir::OutputFormat::decimal
                        && is_signed_expression(statement.value),
                    statement.output_suppress_leading_zero,
                    statement.output_minimum_width,
                    statement.output_left_justify,
                    statement.output_zero_pad,
                    scalar_kind_of(statement.value) });
        } else {
            process_.operations.emplace_back(
                Display {
                    statement.output_text,
                    statement.output_newline,
                    statement.output_postponed });
        }
        break;
    }
    case StatementKind::Report: {
        lower_assert(statement);
        break;
    }
    case StatementKind::Pause:
        process_.operations.emplace_back(Pause { });
        break;
    case StatementKind::Finish:
        process_.operations.emplace_back(Stop { });
        break;
    case StatementKind::Exit:
        if (!systemverilog_program_owner_) {
            report(
                "FSIM-ELAB-SVEXIT-001",
                "$exit is legal only in a SystemVerilog program",
                statement.span);
        }
        process_.operations.emplace_back(Halt { true });
        break;
    case StatementKind::Fork:
        lower_fork(statement);
        break;
    case StatementKind::WaitFork:
        process_.operations.emplace_back(WaitFork { });
        break;
    case StatementKind::DisableFork:
        process_.operations.emplace_back(DisableFork { });
        break;
    case StatementKind::Disable: {
        const auto found = std::find_if(
            block_controls_.rbegin(), block_controls_.rend(),
            [&](const BlockControlContext& control) {
                return control.label == statement.task_name;
            });
        if (found != block_controls_.rend() && found->fork_site) {
            process_.operations.emplace_back(
                DisableFork { found->fork_site });
            break;
        }
        if (found != block_controls_.rend()) {
            found->disable_jumps.push_back(
                static_cast<InstructionIndex>(
                    process_.operations.size()));
            process_.operations.emplace_back(Jump { });
            break;
        }
        const auto named_fork = std::find_if(
            named_fork_controls_.rbegin(), named_fork_controls_.rend(),
            [&](const NamedForkControl& control) {
                return control.label == statement.task_name
                    && control.scope.size() <= local_scope_.size()
                    && std::equal(
                        control.scope.begin(), control.scope.end(),
                        local_scope_.begin());
            });
        if (named_fork != named_fork_controls_.rend()) {
            process_.operations.emplace_back(
                DisableFork { named_fork->site });
            break;
        }
        const auto named_block = std::find_if(
            named_block_controls_.rbegin(), named_block_controls_.rend(),
            [&](const NamedBlockControl& control) {
                return control.label == statement.task_name
                    && control.scope.size() <= local_scope_.size()
                    && std::equal(
                        control.scope.begin(), control.scope.end(),
                        local_scope_.begin());
            });
        if (named_block != named_block_controls_.rend()) {
            const auto operation = static_cast<InstructionIndex>(
                process_.operations.size());
            process_.operations.emplace_back(DisableBlock {
                named_block->begin,
                named_block->end.value_or(0) });
            if (!named_block->end) {
                named_block->disable_operations.push_back(operation);
            }
            break;
        }
        report(
            "FSIM-ELAB-SVDISABLE-001",
            "disable target '" + statement.task_name
                + "' is not visible as a named block",
            statement.span);
        break;
    }
    case StatementKind::Block:
        lower_block(statement);
        break;
    case StatementKind::Null:
        break;
    }
    if (!statement_scope.empty()) {
        local_scope_.pop_back();
    }
}

void Lowerer::emit_debug_point(
    const DebugPointKind kind,
    const frontend::SourceSpan& span)
{
    process_.operations.emplace_back(DebugPoint {
        kind,
        SourceLocation {
            span.source_name.str(),
            static_cast<std::uint32_t>(span.begin.line),
            static_cast<std::uint32_t>(span.begin.column) },
        debug_scope_name() });
}

void Lowerer::lower_wait_until(const Statement& statement)
{
    if (language_ != frontend::Language::Vhdl2008) {
        lower_immediate_condition_wait(statement);
        return;
    }

    const auto dependency_start = implicit_signal_dependencies_.size();
    const auto condition_operations_start = process_.operations.size();
    const auto condition = lower_condition(
        statement.condition,
        "FSIM-ELAB-079",
        "wait-until");
    if (!condition) {
        return;
    }
    std::vector<runtime::simir::Operation> condition_operations(
        std::make_move_iterator(
            process_.operations.begin()
            + static_cast<std::ptrdiff_t>(
                condition_operations_start)),
        std::make_move_iterator(
            process_.operations.end()));
    process_.operations.resize(condition_operations_start);

    std::vector<SignalId> waited_signals;
    std::vector<runtime::simir::EdgeKind> waited_edges;
    if (!statement.sensitivities.empty()) {
        auto resolved = resolve_wait_sensitivities(statement);
        waited_signals = std::move(resolved.first);
        waited_edges = std::move(resolved.second);
    } else {
        std::set<std::string> dependencies;
        collect_identifiers(
            statement.condition, dependencies);
        for (const auto& dependency : dependencies) {
            if (locals_.contains(dependency)) {
                continue;
            }
            if (const auto found = signals_.find(dependency);
                found != signals_.end()) {
                waited_signals.push_back(found->second);
            }
        }
        for (auto index = dependency_start;
            index < implicit_signal_dependencies_.size();
            ++index) {
            waited_signals.push_back(
                implicit_signal_dependencies_[index]);
        }
        std::ranges::sort(waited_signals);
        waited_signals.erase(
            std::unique(
                waited_signals.begin(),
                waited_signals.end()),
            waited_signals.end());
    }

    if (waited_signals.empty() && !statement.delay) {
        process_.operations.emplace_back(WaitForever { });
        lower_statements(statement.statements);
        return;
    }

    std::optional<RegisterId> timed_out;
    if (statement.delay) {
        timed_out = allocate_register(
            1, frontend::ValueDomain::Boolean);
    }

    const auto initial_wait = static_cast<InstructionIndex>(
        process_.operations.size());
    WaitOn first_wait {
        waited_signals, waited_edges
    };
    if (statement.delay) {
        first_wait.timeout = statement.delay->magnitude;
        first_wait.timeout_result = timed_out;
    }
    process_.operations.emplace_back(
        std::move(first_wait));

    std::optional<InstructionIndex> timeout_branch;
    if (timed_out) {
        timeout_branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(
            Branch {
                *timed_out,
                0,
                0,
                UnknownBranchPolicy::error });
    }

    const auto condition_start = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.insert(
        process_.operations.end(),
        std::make_move_iterator(
            condition_operations.begin()),
        std::make_move_iterator(
            condition_operations.end()));
    const auto condition_branch = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto unknown_policy = language_ == frontend::Language::Vhdl2008
        ? UnknownBranchPolicy::error
        : UnknownBranchPolicy::when_false;
    process_.operations.emplace_back(
        Branch { *condition, 0, 0, unknown_policy });

    const auto rewait_start = static_cast<InstructionIndex>(
        process_.operations.size());
    WaitOn rewait {
        std::move(waited_signals),
        std::move(waited_edges)
    };
    if (statement.delay) {
        rewait.timeout = statement.delay->magnitude;
        rewait.timeout_result = timed_out;
        rewait.timeout_origin = initial_wait;
    }
    process_.operations.emplace_back(std::move(rewait));
    process_.operations.emplace_back(
        Jump {
            timeout_branch.value_or(
                condition_start) });

    const auto satisfied_start = static_cast<InstructionIndex>(
        process_.operations.size());
    lower_statements(statement.statements);
    process_.operations[condition_branch] = Branch {
        *condition,
        satisfied_start,
        rewait_start,
        unknown_policy
    };
    if (timeout_branch) {
        process_.operations[*timeout_branch] = Branch {
            *timed_out,
            satisfied_start,
            condition_start,
            UnknownBranchPolicy::error
        };
    }
}

void Lowerer::lower_immediate_condition_wait(
    const Statement& statement)
{
    const auto condition_start = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto condition = lower_condition(
        statement.condition,
        "FSIM-ELAB-079",
        "wait");
    if (!condition) {
        return;
    }
    const auto branch_index = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(
        Branch {
            *condition,
            0,
            0,
            UnknownBranchPolicy::when_false });

    const auto wait_start = static_cast<InstructionIndex>(
        process_.operations.size());
    std::set<std::string> dependencies;
    collect_identifiers(
        statement.condition, dependencies);
    std::vector<SignalId> waited_signals;
    for (const auto& dependency : dependencies) {
        if (locals_.contains(dependency)) {
            continue;
        }
        if (const auto found = signals_.find(dependency);
            found != signals_.end()) {
            waited_signals.push_back(found->second);
        }
    }
    std::ranges::sort(waited_signals);
    waited_signals.erase(
        std::unique(
            waited_signals.begin(),
            waited_signals.end()),
        waited_signals.end());
    if (waited_signals.empty()) {
        process_.operations.emplace_back(WaitForever { });
    } else {
        process_.operations.emplace_back(
            WaitOn { std::move(waited_signals) });
        process_.operations.emplace_back(
            Jump { condition_start });
    }

    const auto satisfied_start = static_cast<InstructionIndex>(
        process_.operations.size());
    lower_statements(statement.statements);
    process_.operations[branch_index] = Branch {
        *condition,
        satisfied_start,
        wait_start,
        UnknownBranchPolicy::when_false
    };
}

} // namespace fsim::elaboration
