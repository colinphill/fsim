// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/file_scanning.hpp"
#include "fsim/runtime/scope_randomize.hpp"
#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_string.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/path.hpp"
#include "simir_container_helpers.hpp"
#include "simir_execution_context.hpp"
#include "simir_execution_shared.hpp"
#include "simir_internal.hpp"
#include "simir_signal_attributes.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <stdexcept>
#include <string_view>

#include <boost/multiprecision/cpp_int.hpp>

namespace fsim::runtime::simir {

void Interpreter::Impl::execute_compact_constant(
    ConstantDriverStartupEntry& process)
{
    if (process.startup_write_bank != nullptr) {
        execute_compact_startup_write(process);
        return;
    }
    if (process.halted) {
        return;
    }
    if (native_process_count_profile_enabled) {
        process.track_interpreter_operations = true;
    }
    const auto program = processes.program_view(process.id);
    const auto& operations = program.operations();
    if (operations.size() != 4U && operations.size() != 5U) {
        throw std::logic_error {
            "compact constant process lost its validated body shape"
        };
    }
    const auto statement_offset = operations.size() == 5U ? 1U : 0U;
    const auto load_instruction = 1U + statement_offset;
    const auto write_instruction = 2U + statement_offset;
    const auto halt_instruction = 3U + statement_offset;
    const auto completed_instruction = 4U + statement_offset;
    std::uint64_t interpreted_operations = 0U;
    const auto invocation_started_at = process_profile_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point { };
    const auto count_operation = [&] {
        ++interpreted_operations;
        if (process_profile_enabled) {
            ++process.profile_interpreter_operations;
        }
    };
    const auto finish_invocation = [&] {
        if (process.track_interpreter_operations) {
            process.interpreter_operations += interpreted_operations;
        }
        if (process_profile_enabled) {
            ++process.profile_calls;
            process.profile_total_nanoseconds
                += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now()
                            - invocation_started_at)
                        .count());
        }
    };

    if (process.suspended) {
        process.suspended_wake = true;
        finish_invocation();
        return;
    }
    if (process.status != ProcessStatus::finished) {
        process.status = ProcessStatus::running;
    }
    if (process.pc == 0U) {
        count_operation();
        const auto marker_operation = operations.expanded(0U);
        const auto* marker = operation_get_if<DebugPoint>(&marker_operation);
        if (marker == nullptr) {
            throw std::logic_error {
                "compact constant process lost its entry DebugPoint"
            };
        }
        const auto& actual = operations.debug_point(0U, *marker);
        process.current_source = actual.source;
        process.current_scope = operations.debug_scope(actual.scope);
        process.pc = 1U;
        notify_execution_point(
            process.id, process.id, program, 0U,
            ExecutionPointKind::process_entry, process.current_source,
            process.current_scope);
        if (scheduler.stop_requested()) {
            queue_current(process.id);
            finish_invocation();
            return;
        }
    }
    if (statement_offset != 0U && process.pc == 1U) {
        count_operation();
        const auto marker_operation = operations.expanded(1U);
        const auto* marker = operation_get_if<DebugPoint>(&marker_operation);
        if (marker == nullptr || marker->kind != DebugPointKind::statement) {
            throw std::logic_error {
                "compact constant process lost its statement DebugPoint"
            };
        }
        const auto& actual = operations.debug_point(1U, *marker);
        process.current_source = actual.source;
        process.current_scope = operations.debug_scope(actual.scope);
        process.pc = load_instruction;
        notify_execution_point(
            process.id, process.id, program, 1U,
            ExecutionPointKind::statement, process.current_source,
            process.current_scope);
        if (scheduler.stop_requested()) {
            queue_current(process.id);
            finish_invocation();
            return;
        }
    }
    if (process.pc == load_instruction) {
        const auto load_operation = operations.expanded(load_instruction);
        const auto* load = operation_get_if<LoadConstant>(&load_operation);
        if (load == nullptr) {
            throw std::logic_error {
                "compact constant process lost its LoadConstant"
            };
        }
        count_operation();
        auto value = coerce_value_kind(
            load->value, signals[process.signal].value_kind);
        process.pc = write_instruction;
        count_operation();
        process.pc = halt_instruction;
        if (process.projected) {
            const auto write_operation = operations.expanded(write_instruction);
            bool projected_matches { };
            if (process.slice) {
                const auto* const projected_slice
                    = operation_get_if<WriteProjectedSlice>(
                        &write_operation);
                projected_matches = projected_slice != nullptr
                    && projected_slice->signal == process.signal
                    && projected_slice->source == load->destination
                    && projected_slice->offset == process.offset
                    && projected_slice->delay == process.projected_delay
                    && projected_slice->rejection
                        == process.projected_rejection
                    && projected_slice->mode == process.projected_mode;
            } else {
                const auto* const projected
                    = operation_get_if<WriteProjected>(&write_operation);
                projected_matches = projected != nullptr
                    && projected->signal == process.signal
                    && projected->source == load->destination
                    && projected->delay == process.projected_delay
                    && projected->rejection == process.projected_rejection
                    && projected->mode == process.projected_mode;
            }
            if (!projected_matches
                || process.update_domain != SignalUpdateDomain::generic) {
                throw std::logic_error {
                    "compact constant process lost its projected write"
                };
            }
            schedule_projected(
                process.id, process.signal, value,
                process.slice
                    ? std::optional<std::size_t> { process.offset }
                    : std::nullopt,
                process.projected_delay, process.projected_rejection,
                process.projected_mode);
        } else if (process.slice) {
            stage_update_slice(
                process.id, process.signal, std::move(value),
                process.offset,
                process.update_domain);
        } else {
            stage_update(
                process.id, process.signal, std::move(value),
                process.update_domain);
        }
    }
    if (process.pc == halt_instruction) {
        count_operation();
        process.pc = completed_instruction;
        const auto halt_operation = operations.expanded(halt_instruction);
        if (!operation_holds<Halt>(halt_operation)) {
            throw std::logic_error {
                "compact constant process lost its terminal Halt"
            };
        }
        notify_execution_point(
            process.id, process.id, program, halt_instruction,
            ExecutionPointKind::process_suspend, process.current_source,
            process.current_scope);
        process.halted = true;
        process.status = ProcessStatus::finished;
        process.suspended = false;
        process.suspended_wake = false;
        process.waiting_on_static = false;
        finish_invocation();
        return;
    }
    if (process.pc != completed_instruction) {
        throw std::logic_error {
            "compact constant process has an invalid program counter"
        };
    }
    finish_invocation();
}

void Interpreter::Impl::execute_compact_startup_write(
    ConstantDriverStartupEntry& process)
{
    if (process.halted) {
        return;
    }
    const auto& bank = *process.startup_write_bank;
    if (bank.signal != process.signal
        || bank.slice != process.slice
        || bank.offset != process.offset
        || bank.update_domain != process.update_domain
        || bank.operation_count
            != (bank.statement_point ? 5U : 4U)
                + static_cast<std::size_t>(bank.constant_copy.has_value())) {
        throw std::logic_error {
            "data-only startup write lost its registered shape"
        };
    }
    if (native_process_count_profile_enabled) {
        process.track_interpreter_operations = true;
    }
    const auto program = processes.program_view(process.id);
    const auto statement_offset = bank.statement_point ? 1U : 0U;
    const auto load_instruction = 1U + statement_offset;
    const auto copy_instruction = 2U + statement_offset;
    const InstructionIndex write_instruction = copy_instruction
        + static_cast<InstructionIndex>(bank.constant_copy.has_value());
    const auto halt_instruction = write_instruction + 1U;
    const auto completed_instruction = halt_instruction + 1U;
    std::uint64_t interpreted_operations = 0U;
    const auto invocation_started_at = process_profile_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point { };
    const auto count_operation = [&] {
        ++interpreted_operations;
        if (process_profile_enabled) {
            ++process.profile_interpreter_operations;
        }
    };
    const auto finish_invocation = [&] {
        if (process.track_interpreter_operations) {
            process.interpreter_operations += interpreted_operations;
        }
        if (process_profile_enabled) {
            ++process.profile_calls;
            process.profile_total_nanoseconds
                += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now()
                            - invocation_started_at)
                        .count());
        }
    };

    if (process.suspended) {
        process.suspended_wake = true;
        finish_invocation();
        return;
    }
    if (process.status != ProcessStatus::finished) {
        process.status = ProcessStatus::running;
    }
    if (process.pc == 0U) {
        count_operation();
        process.current_source = bank.entry_point.source;
        process.current_scope = bank.entry_point.scope;
        process.pc = 1U;
        notify_execution_point(
            process.id, process.id, program, 0U,
            ExecutionPointKind::process_entry, process.current_source,
            process.current_scope);
        if (scheduler.stop_requested()) {
            queue_current(process.id);
            finish_invocation();
            return;
        }
    }
    if (statement_offset != 0U && process.pc == 1U) {
        count_operation();
        const auto& statement_point = *bank.statement_point;
        process.current_source = statement_point.source;
        process.current_scope = statement_point.scope;
        process.pc = load_instruction;
        notify_execution_point(
            process.id, process.id, program, 1U,
            ExecutionPointKind::statement, process.current_source,
            process.current_scope);
        if (scheduler.stop_requested()) {
            queue_current(process.id);
            finish_invocation();
            return;
        }
    }
    if (process.pc == load_instruction) {
        count_operation();
        process.pc = bank.constant_copy
            ? copy_instruction : write_instruction;
    }
    if (bank.constant_copy && process.pc == copy_instruction) {
        count_operation();
        process.pc = write_instruction;
    }
    if (process.pc == write_instruction) {
        count_operation();
        auto value = coerce_value_kind(
            bank.value, signals[bank.signal].value_kind);
        process.pc = halt_instruction;
        if (bank.slice) {
            stage_update_slice(process.id, bank.signal, std::move(value),
                bank.offset, bank.update_domain);
        } else {
            stage_update(process.id, bank.signal, std::move(value),
                bank.update_domain);
        }
    }
    if (process.pc == halt_instruction) {
        count_operation();
        process.pc = completed_instruction;
        notify_execution_point(
            process.id, process.id, program, halt_instruction,
            ExecutionPointKind::process_suspend, process.current_source,
            process.current_scope);
        process.halted = true;
        process.status = ProcessStatus::finished;
        process.suspended = false;
        process.suspended_wake = false;
        process.waiting_on_static = false;
        finish_invocation();
        return;
    }
    if (process.pc != completed_instruction) {
        throw std::logic_error {
            "data-only startup write has an invalid program counter"
        };
    }
    finish_invocation();
}

void Interpreter::Impl::note_interpreted_execute(const ProcessId id)
{
    ++interpreted_executes;
    // FSIM_V4_STRICT=1: a v4 run must never interpret a process (the
    // kernel's members, hosted steps and process executors are native).
    static const bool strict = [] {
        const char* text = std::getenv("FSIM_V4_STRICT");
        return text != nullptr && std::string_view { text } == "1";
    }();
    if (strict && static_kernel) {
        throw std::runtime_error { "FSIM_V4_STRICT: process '"
            + std::string { get_process(id).program().name() }
            + "' ran on the reference interpreter in a v4 run" };
    }
    static const bool profile = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    if (profile) {
        ++interpreted_programs[id];
    }
}

void Interpreter::Impl::execute(ProcessId id)
{
    if (auto* const compact = processes.compact_constant(id)) {
        ++constant_driver_executes;
        execute_compact_constant(*compact);
        return;
    }
    auto& process = get_process(id);
    if (native_process_count_profile_enabled) {
        process.cold().track_interpreter_operations = true;
    }
    std::uint64_t interpreted_operations = 0U;
    struct ProcessProfileScope {
        ProcessState& process;
        bool enabled;
        std::chrono::steady_clock::time_point started;
        std::uint64_t& interpreted_operations;

        ~ProcessProfileScope()
        {
            if (process.cold().track_interpreter_operations) {
                process.cold().interpreter_operations += interpreted_operations;
            }
            if (!enabled) {
                return;
            }
            ++process.cold().profile_calls;
            process.cold().profile_total_nanoseconds += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count());
        }
    } profile_scope {
        process,
        process_profile_enabled,
        process_profile_enabled ? std::chrono::steady_clock::now()
                                : std::chrono::steady_clock::time_point { },
        interpreted_operations
    };
    if (process.suspended) {
        process.suspended_wake = true;
        return;
    }
    restore_callable_context(process);
    if (!process.executor && process.cold().deferred_executor
        && can_install_deferred_executor(process)
        && deferred_executor_ready(process)) {
        install_deferred_executor(process);
    }
    if (!process.halted) {
        process.status = ProcessStatus::running;
    }
    if (process.executor) {
        ExecutionContext context { *this, id };
        while (!process.halted) {
            if (native_phase_profile_enabled) {
                ++native_phase_profile_single_resumes;
            }
            if (native_process_count_profile_enabled) {
                if (id >= native_process_resume_counts.size()) {
                    const auto size = static_cast<std::size_t>(id) + 1U;
                    native_process_resume_counts.resize(size);
                    native_process_single_resume_counts.resize(size);
                    native_process_single_static_wait_counts.resize(size);
                    native_process_cohort_resume_counts.resize(size);
                    native_process_cohort_static_wait_counts.resize(size);
                    native_process_word_fanout_ready_counts.resize(size);
                }
                ++native_process_resume_counts[id];
            }
            const auto native_started = process_profile_enabled
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point { };
            const auto boundary = process.executor->resume(context, process.pc);
            if (native_process_count_profile_enabled) {
                ++native_process_single_resume_counts[id];
                ++native_process_single_boundary_counts[
                    external_suspension_profile_index(
                        boundary.external.kind)];
                if (is_static_wait_suspension(boundary.external.kind)) {
                    ++native_process_single_static_wait_counts[id];
                    const auto wave = std::pair {
                        scheduler.now(), scheduler.delta()
                    };
                    if (native_process_single_wave_identity != wave) {
                        native_process_single_wave_offsets.push_back(
                            native_process_single_wave_processes.size());
                        native_process_single_wave_identity = wave;
                    }
                    native_process_single_wave_processes.push_back(id);
                }
            }
            if (process_profile_enabled) {
                ++process.cold().profile_native_resumes;
                process.cold().profile_native_nanoseconds += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - native_started)
                    .count());
            }
            if (handle_executor_resume(process, boundary)) {
                continue;
            }
            return;
        }
        snapshot_callable_context(process);
        return;
    }

    if (!process.halted) {
        (void)ensure_process_frame(process);
    }

    // The process runs on this evaluator (its executor, such as the static
    // kernel's host stub, runs native code above).
    if (run_hosted_tier(process)) {
        return;
    }
    // A finished process has nothing left to interpret (a wake queued
    // before it halted reaches it after).
    if (process.halted) {
        return;
    }
    note_interpreted_execute(id);
    while (!process.halted) {
        if (process.pc >= process.program().operations().size()) {
            fail(process, "program counter is outside the operation stream");
        }

        const auto instruction = process.pc;
        if (process.cold().track_interpreter_operations) {
            ++interpreted_operations;
        }
        if (process_profile_enabled) {
            ++process.cold().profile_interpreter_operations;
        }
        const auto& operation
            = std::as_const(process.program().operations())[instruction];
        const bool boundary = fsim::runtime::simir::visit_operation(
            [&](const auto& op) {
                if (operation_budget) {
                    if (*operation_budget == 0U) {
                        fail(process,
                            "the evaluation exceeded its operation budget");
                    }
                    --*operation_budget;
                }
                const bool boundary
                    = execute_operation(process, instruction, op);
                if constexpr (requires { RegisterId { op.destination }; }) {
                    if (operation_budget && !boundary && process.frame
                        && op.destination
                            < process.frame->registers.size()) {
                        // A bounded evaluation (elaboration-time folding)
                        // also pays for the words an operation produces, so
                        // a loop over huge values exhausts it as surely as
                        // a long one. An insert into its own target updates
                        // the inserted part in place.
                        auto produced = op.destination;
                        if constexpr (requires {
                                          RegisterId { op.target };
                                          RegisterId { op.source };
                                      }) {
                            if (op.target == op.destination
                                && op.source
                                    < process.frame->registers.size()) {
                                produced = op.source;
                            }
                        }
                        auto work = static_cast<std::uint64_t>(
                            process.frame->registers[produced].width())
                            / 64U;
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(op)>,
                                          Binary>) {
                            // An operator over values wider than a word
                            // visits each bit, at about the cost of eight
                            // copied words.
                            const auto& registers = process.frame->registers;
                            const auto width = std::max(
                                op.lhs < registers.size()
                                    ? registers[op.lhs].width()
                                    : std::size_t { },
                                op.rhs < registers.size()
                                    ? registers[op.rhs].width()
                                    : std::size_t { });
                            if (width > 64U) {
                                work = std::max(work,
                                    static_cast<std::uint64_t>(width) * 8U);
                            }
                        }
                        *operation_budget
                            -= std::min(*operation_budget, work);
                    }
                }
                return boundary;
            },
            operation);
        if (boundary && !continue_after_boundary(process, operation, instruction)) {
            return;
        }
    }
}

bool Interpreter::Impl::continue_after_boundary(ProcessState& process,
    const Operation& operation, const InstructionIndex instruction)
{
    const bool debug_boundary = fsim::runtime::simir::operation_holds<DebugPoint>(operation);
    if (fsim::runtime::simir::operation_holds<Fork>(operation)
        && !process.executor && process.cold().deferred_executor
        && can_install_deferred_executor(process)
        && deferred_executor_ready(process)) {
        install_deferred_executor(process);
        queue_current(process.id);
        return false;
    }
    const bool immediate_process_boundary = is_immediate_process_boundary(operation);
    const bool synchronization_boundary = is_synchronization_boundary(operation);
    handle_boundary(process, instruction, instruction + 1);
    if ((immediate_process_boundary
            || (synchronization_boundary
                && process.status == ProcessStatus::running
                && !process.queued))
        && !scheduler.stop_requested()) {
        return true;
    }
    if (!debug_boundary || scheduler.stop_requested()) {
        snapshot_callable_context(process);
        return false;
    }
    return true;
}

template <typename Op>
bool Interpreter::Impl::execute_operation(ProcessState& process,
    const InstructionIndex instruction, const Op& op)
{
    const auto id = process.id;
    bool boundary = false;
    const auto selected_offset =
        [&](const DynamicIndex& selection) -> std::uint32_t {
        try {
            return dynamic_index_offset(
                get_register(process, selection.index),
                selection);
        } catch (const std::invalid_argument& error) {
            fail(process, error.what());
        }
    };
    // A SystemVerilog write through an unknown or out-of-range index
    // does nothing (IEEE 1800-2017 11.5.1); a VHDL index is checked.
    const auto written_offset = [&](const DynamicIndex& selection)
        -> std::optional<std::uint32_t> {
        try {
            return dynamic_index_offset(
                get_register(process, selection.index), selection);
        } catch (const std::invalid_argument& error) {
            if (selection.strict) {
                fail(process, error.what());
            }
            return std::nullopt;
        }
    };
    const auto known_string_index =
        [&](const RegisterId index_register,
            const bool signed_index,
            const std::size_t size) -> std::optional<std::size_t> {
        const auto& value = get_register(process, index_register);
        if (value.width() == 0 || value.width() > 64) {
            fail(
                process,
                "string index must be a nonempty value of at most 64 bits");
        }
        // An unknown, negative or out-of-range index selects no
        // character (IEEE 1800-2017 6.16).
        const auto word = value.low_word();
        if (word.bval != 0) {
            return std::nullopt;
        }
        if (signed_index && word.width != 0
            && word.width < 64
            && ((word.aval >> (word.width - 1U)) & 1U) != 0) {
            return std::nullopt;
        }
        if (signed_index && word.width == 64
            && (word.aval >> 63U) != 0) {
            return std::nullopt;
        }
        if (word.aval >= size) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(word.aval);
    };
    (void)id;
    (void)instruction;
    (void)op;
    (void)selected_offset;
    (void)written_offset;
    (void)known_string_index;
    [&] {
        using OperationType = Op;
        if constexpr (std::is_same_v<OperationType, LoadConstant>) {
            get_register(process, op.destination) = coerce_value_kind(
                op.value,
                register_value_kind(
                    process, op.destination));
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, ReadSignal>) {
            (void)get_signal(op.signal);
            if (op.kind == SignalReadKind::current) {
                get_register(process, op.destination) = coerce_value_kind(
                    logical_signal_value(op.signal),
                    register_value_kind(process, op.destination));
                ++process.pc;
                return;
            }
            execute_sampled_read(process, op);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, SignalEvent>
            || std::is_same_v<OperationType, SignalActive>) {
            bool value {};
            try {
                if constexpr (std::is_same_v<OperationType, SignalEvent>) {
                    value = signal_event_attribute(op.signal, op.selection);
                } else {
                    value = signal_active_attribute(op.signal, op.selection);
                }
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            get_register(process, op.destination)
                = PackedLogic4(1, value ? Logic4::one : Logic4::zero);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, SignalLastValue>) {
            try {
                get_register(process, op.destination) = coerce_value_kind(
                    signal_last_value_attribute(op.signal, op.selection),
                    register_value_kind(process, op.destination));
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, SignalLastEvent>
            || std::is_same_v<OperationType, SignalLastActive>) {
            SimulationTick elapsed {};
            try {
                if constexpr (std::is_same_v<OperationType, SignalLastEvent>) {
                    elapsed = signal_last_event_attribute(
                        op.signal, op.selection);
                } else {
                    elapsed = signal_last_active_attribute(
                        op.signal, op.selection);
                }
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            get_register(process, op.destination)
                = PackedLogic4::from_aval_bval(64, elapsed, 0);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, ReadSimulationTime>) {
            get_register(process, op.destination) = PackedLogic4::from_aval_bval(64, scheduler.now(), 0);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, VitalTimingCheck>) {
            ExecutionContext context { *this, id };
            PackedLogic4 result(1);
            result.fill(context.evaluate_vital_timing_check(
                instruction, op));
            get_register(process, op.destination) = std::move(result);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, VitalDelay>) {
            execute_vital_delay_operation(id, process, instruction, op);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, SignalDriving>) {
            get_register(process, op.destination)
                = signal_driving(process.id, op);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, SignalDrivingValue>) {
            auto value = signal_driving_value(process.id, op);
            if (!value) {
                fail(process,
                    "VHDL 'driving_value queried a signal without a driver");
            }
            get_register(process, op.destination) = coerce_value_kind(
                std::move(*value), register_value_kind(process, op.destination));
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, CopyRegister>) {
            get_register(process, op.destination) = coerce_value_kind(
                get_register(process, op.source),
                register_value_kind(
                    process, op.destination));
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, ConvertToTwoState>) {
            const auto& source = get_register(process, op.source);
            auto converted = PackedLogic4(
                source.width(), Logic4::zero);
            for (std::size_t bit = 0; bit < source.width(); ++bit) {
                if (to_logic4(source.get_logic9(bit))
                    == Logic4::one) {
                    converted.set(bit, Logic4::one);
                }
            }
            get_register(process, op.destination) = std::move(converted);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, ClassAllocate>) {
            ProcessContainerAccess access { *this, process };
            if (const auto* error = run_class_operation(access, op, process.id,
                    process.cold().design_process, process.program().name())) {
                fail(process, error);
            }
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, ClassPropertyRead>
            || std::is_same_v<OperationType, ClassPropertyWrite>) {
            ProcessContainerAccess access { *this, process };
            if (const auto* error = run_class_operation(access, op)) {
                fail(process, error);
            }
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, ClassMethodCall>) {
            ProcessContainerAccess access { *this, process };
            if (const auto* error = run_class_operation(access, op, process.id)) {
                fail(process, error);
            }
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, ClassStaticPropertyRead>
            || std::is_same_v<OperationType, ClassStaticPropertyWrite>) {
            ProcessContainerAccess access { *this, process };
            if (const auto* error = run_class_operation(access, op)) {
                fail(process, error);
            }
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, ClassStaticMethodCall>) {
            if (try_vhdl_disconnect_call(process.id, op.method_identity)) {
                ++process.pc;
                return;
            }
            ProcessContainerAccess access { *this, process };
            if (const auto* error = run_class_operation(access, op, process.id)) {
                fail(process, error);
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, LoadStringConstant>) {
            if (op.value.size() > maximum_string_bytes) {
                fail(process, "string literal exceeds the string length limit");
            }
            try {
                (void)systemverilog_string_length(op.value);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            get_string_register(process, op.destination) = op.value;
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, CopyStringRegister>) {
            get_string_register(process, op.destination) = get_string_register(process, op.source);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, ReadStringObject>) {
            get_string_register(process, op.destination) = string_object_storage(op.object);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, WriteStringObject>) {
            if (package_static_initialization_active) {
                initialize_package_static_string_object(
                    op.object,
                    get_string_register(process, op.source),
                    process.id);
                ++process.pc;
                return;
            }
            if (op.nonblocking) {
                // `s <= v` updates the string in the time slot's
                // NBA region (IEEE 1800-2017 10.4.2, 4.9.4).
                const auto origin = capture_signal_change_origin(
                    process.id, SignalUpdateDomain::systemverilog_nba);
                scheduler.schedule_systemverilog(origin.phase,
                    process.id,
                    [this, object = op.object,
                        value = get_string_register(
                            process, op.source)](Scheduler&) {
                        string_object_storage(object) = value;
                    });
                ++process.pc;
                return;
            }
            string_object_storage(op.object) = get_string_register(process, op.source);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, ConcatenateStrings>) {
            std::string result;
            for (const auto operand : op.operands) {
                const auto& value = get_string_register(process, operand);
                if (value.size()
                    > maximum_string_bytes - result.size()) {
                    fail(
                        process,
                        "string concatenation exceeds the string length limit");
                }
                result += value;
            }
            get_string_register(process, op.destination) = std::move(result);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, CompareStrings>) {
            bool equal { };
            try {
                equal = systemverilog_string_compare(
                            get_string_register(process, op.lhs),
                            get_string_register(process, op.rhs))
                    == 0;
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            get_register(process, op.destination) = PackedLogic4 {
                1,
                equal != op.not_equal
                    ? Logic4::one
                    : Logic4::zero
            };
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, StringLength>) {
            std::size_t size { };
            try {
                size = systemverilog_string_length(
                    get_string_register(process, op.source));
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            get_register(process, op.destination) = PackedLogic4::from_aval_bval(
                32, static_cast<std::uint32_t>(size), 0);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, StringIndex>) {
            auto& source = get_string_register(process, op.source);
            std::size_t size { };
            try {
                size = systemverilog_string_length(source);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            const auto index = known_string_index(
                op.index, op.signed_index, size);
            std::uint32_t byte { };
            try {
                // An index out of range reads 0 (6.16).
                byte = index
                    ? systemverilog_string_at(source, *index)
                    : 0U;
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            get_register(process, op.destination) = PackedLogic4::from_aval_bval(
                32, byte, 0);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, StringReplaceByte>) {
            auto& target = get_string_register(process, op.target);
            std::size_t size { };
            try {
                size = systemverilog_string_length(target);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            const auto index = known_string_index(
                op.index, op.signed_index, size);
            if (!index) {
                // A write out of range leaves the string unchanged
                // (6.16).
                ++process.pc;
                return;
            }
            const auto byte = get_register(process, op.source).low_word();
            if (byte.bval != 0) {
                fail(process, "string replacement byte contains X or Z");
            }
            try {
                systemverilog_string_replace(
                    target, *index,
                    static_cast<std::uint32_t>(byte.aval),
                    maximum_string_bytes);
            } catch (const std::exception& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, StringMethod>) {
            execute_string(process, op);
        } else if constexpr (std::is_same_v<OperationType, PlusArgSelect>) {
            const auto selected = select_plusarg(plusargs,
                get_string_register(process, op.query), op.format_query);
            if (op.selected) {
                get_string_register(process, *op.selected).assign(
                    selected.value_or(std::string_view { }));
            }
            get_register(process, op.destination) = PackedLogic4::from_aval_bval(
                32, selected ? 1U : 0U, 0);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, SystemCommand>) {
            if (!system_command_hook) {
                fail(process, "$system service is unavailable");
            }
            const auto command = op.command
                ? std::optional<std::string_view> {
                      get_string_register(process, *op.command)
                  }
                : std::nullopt;
            const auto status = system_command_hook(command);
            if (op.destination) {
                get_register(process, *op.destination)
                    = PackedLogic4::from_aval_bval(
                        32,
                        static_cast<std::uint32_t>(status),
                        0);
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, VcdControl>) {
            if (!vcd_control_hook) {
                fail(process, "VCD control service is unavailable");
            }
            VcdControlEvent event;
            event.kind = op.kind;
            if (op.filename) {
                event.filename = get_string_register(process, *op.filename);
            }
            if (op.value) {
                const auto converted
                    = get_register(process, *op.value).known_unsigned_value();
                if (!converted) {
                    fail(process, "VCD control value must be a known unsigned 64-bit integer");
                }
                event.value = *converted;
            }
            control_vcd(process.id, op, std::move(event));
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType,
                CoverageDatabaseControl>) {
            if (!coverage_database_control_hook) {
                fail(process,
                    "coverage database service is unavailable");
            }
            coverage_database_control_hook({ op.kind,
                get_string_register(process, op.filename) });
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, StochasticQueueOperation>) {
            execute_stochastic_queue(process, op);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, SvaOperation>) {
            execute_sva(process, op);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, PlaEvaluate>) {
            execute_pla(process, op);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, TimeFormatControl>) {
            set_time_format(process, op);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, UnaryNot>) {
            get_register(process, op.destination) = unary_not(get_register(process, op.source));
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, LogicalNot>) {
            get_register(process, op.destination) = logical_not(get_register(process, op.source));
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, LogicalBinary>) {
            get_register(process, op.destination) = logical_binary(
                op.operation,
                get_register(process, op.lhs),
                get_register(process, op.rhs));
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Reduction>) {
            get_register(process, op.destination) = reduce_value(
                op.operation,
                get_register(process, op.source));
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, CountOnes>) {
            get_register(process, op.destination) = count_ones_value(
                get_register(process, op.source));
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, CountBits>) {
            get_register(process, op.destination) = count_bits_value(
                get_register(process, op.source),
                op.state_mask);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Shift>) {
            get_register(process, op.destination) = shift_value(
                op.operation,
                get_register(process, op.value),
                get_register(process, op.amount),
                op.signed_amount);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Extract>) {
            try {
                get_register(process, op.destination) = extract_value(
                    get_register(process, op.source),
                    op.offset,
                    op.width);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, DynamicExtract>) {
            const auto& source = get_register(process, op.source);
            try {
                get_register(process, op.destination) = extract_value(
                    source,
                    dynamic_index_offset(
                        get_register(process, op.selection.index),
                        op.selection),
                    1);
            } catch (const std::invalid_argument& error) {
                if (op.selection.strict) {
                    fail(process, error.what());
                }
                auto invalid = PackedLogic4(1, Logic4::x);
                if (source.is_logic9()) {
                    invalid = invalid.promoted_to_logic9();
                }
                get_register(process, op.destination) =
                    std::move(invalid);
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, DynamicPartSelect>) {
            try {
                get_register(process, op.destination) = dynamic_part_select_value(
                    get_register(process, op.source),
                    get_register(process, op.base),
                    op.left,
                    op.right,
                    op.base_offset,
                    op.width,
                    op.increasing,
                    op.source_descending,
                    op.two_state);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Insert>) {
            try {
                // An element write to a register updates it in
                // place; a copy of the whole value would make a
                // loop over its elements quadratic.
                if (op.destination == op.target) {
                    get_register(process, op.target).insert_bits(
                        get_register(process, op.source), op.offset);
                } else {
                    get_register(process, op.destination)
                        = insert_value(
                            get_register(process, op.target),
                            get_register(process, op.source),
                            op.offset);
                }
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, DynamicInsert>) {
            try {
                const auto offset = dynamic_index_offset(
                    get_register(process, op.selection.index),
                    op.selection);
                if (op.destination == op.target) {
                    get_register(process, op.target).insert_bits(
                        get_register(process, op.source), offset);
                } else {
                    get_register(process, op.destination)
                        = insert_value(
                            get_register(process, op.target),
                            get_register(process, op.source),
                            offset);
                }
            } catch (const std::invalid_argument& error) {
                if (op.selection.strict) {
                    fail(process, error.what());
                }
                get_register(process, op.destination) =
                    get_register(process, op.target);
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, DynamicPartInsert>) {
            try {
                if (op.destination == op.target) {
                    const auto write = dynamic_part_write_value(
                        get_register(process, op.source),
                        get_register(process, op.selection.base),
                        op.selection);
                    if (write) {
                        get_register(process, op.target).insert_bits(
                            write->value, write->offset);
                    }
                } else {
                    get_register(process, op.destination)
                        = dynamic_part_insert_value(
                            get_register(process, op.target),
                            get_register(process, op.source),
                            get_register(
                                process, op.selection.base),
                            op.selection);
                }
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Concatenate>) {
            std::vector<PackedLogic4> operands;
            operands.reserve(op.operands.size());
            for (const auto operand : op.operands) {
                operands.push_back(get_register(process, operand));
            }
            try {
                get_register(process, op.destination) = concatenate_values(operands, op.width);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, SystemVerilogScalarBinary>) {
            const auto result = systemverilog_scalar_binary_payload(
                op.operation,
                get_register(process, op.lhs), op.lhs_kind,
                get_register(process, op.rhs), op.rhs_kind,
                op.result_kind);
            if (!result) {
                fail(
                    process,
                    "SystemVerilog scalar binary operation failed (error "
                        + std::to_string(static_cast<unsigned>(result.error))
                        + ", lhs kind "
                        + std::to_string(static_cast<unsigned>(op.lhs_kind))
                        + ", rhs kind "
                        + std::to_string(static_cast<unsigned>(op.rhs_kind))
                        + ")");
            }
            get_register(process, op.destination) = result.value;
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, SystemVerilogMath>) {
            if (op.function >= SystemVerilogMathFunction::Time) {
                const auto function
                    = op.function == SystemVerilogMathFunction::Time
                    ? SystemVerilogTimeFunction::Time
                    : op.function
                        == SystemVerilogMathFunction::Stime
                    ? SystemVerilogTimeFunction::Stime
                    : SystemVerilogTimeFunction::Realtime;
                const auto value = systemverilog_time_function(
                    function,
                    scheduler.now(),
                    { op.time_unit_femtoseconds,
                        op.time_precision_femtoseconds,
                        time_format.resolution_femtoseconds });
                if (!value) {
                    fail(
                        process,
                        "SystemVerilog time query failed (error "
                            + std::to_string(static_cast<unsigned>(
                                value.error))
                            + ")");
                }
                const auto encoded = op.function
                        == SystemVerilogMathFunction::Realtime
                    ? encode_systemverilog_scalar_payload(value.value)
                    : systemverilog_scalar_to_packed(
                          value.value,
                          op.function
                                  == SystemVerilogMathFunction::Stime
                              ? 32U
                              : 64U,
                          false);
                if (!encoded) {
                    fail(
                        process,
                        "SystemVerilog time query failed (error "
                            + std::to_string(static_cast<unsigned>(
                                encoded.error))
                            + ")");
                }
                get_register(process, op.destination) = encoded.value;
                ++process.pc;
                return;
            }
            const auto* second = op.second_width == 0
                ? nullptr
                : &get_register(process, op.second);
            const auto result = systemverilog_math_payload(
                op.function,
                get_register(process, op.first),
                op.first_kind,
                op.first_signed,
                second,
                op.second_kind,
                op.second_signed);
            if (!result) {
                fail(
                    process,
                    "SystemVerilog math function failed (error "
                        + std::to_string(
                            static_cast<unsigned>(result.error))
                        + ")");
            }
            get_register(process, op.destination) = result.value;
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Binary>) {
            try {
                const auto& lhs = get_register(process, op.lhs);
                const auto& rhs = get_register(process, op.rhs);
                auto result = binary_value(op.operation, lhs, rhs);
                get_register(process, op.destination)
                    = std::move(result);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, IntegerUnary>) {
            try {
                get_register(process, op.destination) = integer_unary_value(
                    op.operation,
                    get_register(process, op.source));
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, IntegerBinary>) {
            try {
                get_register(process, op.destination) = integer_binary_value(
                    op.operation,
                    get_register(process, op.lhs),
                    get_register(process, op.rhs));
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, IntegerCheck>) {
            try {
                check_integer_range(
                    get_register(process, op.source),
                    op.lower,
                    op.upper);
            } catch (const std::invalid_argument& error) {
                auto message = std::string { error.what() };
                if (message
                    == "VHDL integer operand contains an unknown or high-impedance value") {
                    const auto& value = get_register(
                        process, op.source);
                    auto bits = value.to_msb_string();
                    constexpr std::size_t maximum_bits { 128U };
                    if (bits.size() > maximum_bits) {
                        bits = bits.substr(
                            0U, maximum_bits) + "...";
                    }
                    message += " [process=" + process.program().name()
                        + ", register="
                        + std::to_string(op.source)
                        + ", width="
                        + std::to_string(value.width())
                        + ", value=" + bits + ", range="
                        + std::to_string(op.lower) + ".."
                        + std::to_string(op.upper) + "]";
                }
                fail(process, message);
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, ConditionalSelect>) {
            try {
                get_register(process, op.destination) = conditional_value(
                    get_register(process, op.condition),
                    get_register(process, op.when_true),
                    get_register(process, op.when_false));
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else {
            // Restart template dispatch depth before MSVC reaches its
            // nested-block compiler limit on the full operation set.
            [&] {
        if constexpr (std::is_same_v<OperationType, WriteBlocking>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            commit_driver(
                process.id,
                op.signal,
                std::move(value));
        } else if constexpr (std::is_same_v<OperationType, WriteUpdate>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            stage_update(
                process.id,
                op.signal,
                std::move(value),
                op.domain);
        } else if constexpr (std::is_same_v<OperationType, WriteAfter>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_after_update(process.id, op.signal, std::move(value),
                std::nullopt, op.domain, op.delay);
        } else if constexpr (std::is_same_v<OperationType, WriteDelayed>) {
            auto value = get_register(process, op.source);
            const auto delays = write_delayed_delays(op,
                [&](const RegisterId id) -> const PackedLogic4& {
                    return get_register(process, id);
                });
            ++process.pc;
            schedule_write_delayed(process.id, op, std::move(value), delays);
        } else if constexpr (std::is_same_v<OperationType, WriteInertial>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_inertial(
                process.id,
                op.signal,
                std::move(value),
                std::nullopt,
                op.delays,
                op.domain);
        } else if constexpr (std::is_same_v<OperationType, WriteProjected>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_projected(
                process.id,
                op.signal,
                value,
                std::nullopt,
                op.delay,
                op.rejection,
                op.mode);
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveform>) {
            std::vector<ProjectedWaveformValue> elements;
            elements.reserve(op.elements.size());
            for (const auto& element : op.elements) {
                elements.push_back(
                    { get_register(process, element.source),
                        projected_element_delay(process, element) });
            }
            const auto rejection = projected_rejection(
                op.rejection, elements);
            ++process.pc;
            schedule_projected_waveform(
                process.id,
                op.signal,
                elements,
                std::nullopt,
                rejection,
                op.mode);
        } else if constexpr (std::is_same_v<OperationType, WriteBlockingSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            commit_driver_slice(
                process.id,
                op.signal,
                std::move(value),
                op.offset);
        } else if constexpr (std::is_same_v<OperationType, WriteUpdateSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            stage_update_slice(
                process.id,
                op.signal,
                std::move(value),
                op.offset,
                op.domain);
        } else if constexpr (std::is_same_v<OperationType, WriteAfterSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_after_update(process.id, op.signal, std::move(value),
                op.offset, op.domain, op.delay);
        } else if constexpr (std::is_same_v<OperationType, WriteInertialSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_inertial(
                process.id,
                op.signal,
                std::move(value),
                op.offset,
                op.delays,
                op.domain);
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedSlice>) {
            auto value = get_register(process, op.source);
            ++process.pc;
            schedule_projected(
                process.id,
                op.signal,
                value,
                op.offset,
                op.delay,
                op.rejection,
                op.mode);
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformSlice>) {
            std::vector<ProjectedWaveformValue> elements;
            elements.reserve(op.elements.size());
            for (const auto& element : op.elements) {
                elements.push_back(
                    { get_register(process, element.source),
                        projected_element_delay(process, element) });
            }
            const auto rejection = projected_rejection(
                op.rejection, elements);
            ++process.pc;
            schedule_projected_waveform(
                process.id,
                op.signal,
                elements,
                op.offset,
                rejection,
                op.mode);
        } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = written_offset(op.selection);
            ++process.pc;
            if (offset) {
                commit_driver_slice(
                    process.id,
                    op.signal,
                    std::move(value),
                    *offset);
            }
        } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = written_offset(op.selection);
            ++process.pc;
            if (offset) {
                stage_update_slice(
                    process.id,
                    op.signal,
                    std::move(value),
                    *offset,
                    op.domain);
            }
        } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicSlice>) {
            auto value = get_register(process, op.source);
            // A SystemVerilog write outside the declared range has
            // no effect (IEEE 1800-2017 11.5.1).
            const auto written = written_offset(op.selection);
            ++process.pc;
            if (written) {
                schedule_after_update(process.id, op.signal,
                    std::move(value), *written, op.domain, op.delay);
            }
        } else if constexpr (std::is_same_v<OperationType, WriteBlockingDynamicPartSlice>) {
            try {
                const auto write = dynamic_part_write_value(
                    get_register(process, op.source),
                    get_register(process, op.selection.base),
                    op.selection);
                ++process.pc;
                if (write) {
                    commit_driver_slice(
                        process.id,
                        op.signal,
                        write->value,
                        write->offset);
                }
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
        } else if constexpr (std::is_same_v<OperationType, WriteUpdateDynamicPartSlice>) {
            try {
                const auto write = dynamic_part_write_value(
                    get_register(process, op.source),
                    get_register(process, op.selection.base),
                    op.selection);
                ++process.pc;
                if (write) {
                    stage_update_slice(
                        process.id,
                        op.signal,
                        write->value,
                        write->offset,
                        op.domain);
                }
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
        } else if constexpr (std::is_same_v<OperationType, WriteAfterDynamicPartSlice>) {
            try {
                auto write = dynamic_part_write_value(
                    get_register(process, op.source),
                    get_register(process, op.selection.base),
                    op.selection);
                ++process.pc;
                if (write) {
                    schedule_after_update(process.id, op.signal,
                        std::move(write->value), write->offset, op.domain,
                        op.delay);
                }
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
        } else if constexpr (std::is_same_v<OperationType, ForceSignalSlice>) {
            try {
                const auto offset = op.selection
                    ? selected_offset(*op.selection)
                    : op.offset;
                force_slice_from_process(process.id, op.signal,
                    get_register(process, op.source), offset,
                    op.driving_value, op.port_segment);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, ReleaseSignalSlice>) {
            try {
                const auto offset = op.selection
                    ? selected_offset(*op.selection)
                    : op.offset;
                release_slice_from_process(process.id, op.signal,
                    offset, op.width, op.driving_value,
                    op.port_segment);
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, WriteInertialDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            schedule_inertial(
                process.id,
                op.signal,
                std::move(value),
                offset,
                op.delays,
                op.domain);
        } else if constexpr (std::is_same_v<
                                 OperationType,
                                 WriteInertialDynamicPartSlice>) {
            try {
                auto write = dynamic_part_write_value(
                    get_register(process, op.source),
                    get_register(process, op.selection.base),
                    op.selection);
                ++process.pc;
                if (write) {
                    schedule_inertial(
                        process.id,
                        op.signal,
                        std::move(write->value),
                        write->offset,
                        op.delays,
                        op.domain);
                }
            } catch (const std::invalid_argument& error) {
                fail(process, error.what());
            }
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedDynamicSlice>) {
            auto value = get_register(process, op.source);
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            schedule_projected(
                process.id,
                op.signal,
                value,
                offset,
                op.delay,
                op.rejection,
                op.mode);
        } else if constexpr (std::is_same_v<OperationType, WriteProjectedWaveformDynamicSlice>) {
            std::vector<ProjectedWaveformValue> elements;
            elements.reserve(op.elements.size());
            for (const auto& element : op.elements) {
                elements.push_back(
                    { get_register(process, element.source),
                        projected_element_delay(process, element) });
            }
            const auto rejection = projected_rejection(
                op.rejection, elements);
            const auto offset = selected_offset(op.selection);
            ++process.pc;
            schedule_projected_waveform(
                process.id,
                op.signal,
                elements,
                offset,
                rejection,
                op.mode);
        } else if constexpr (std::is_same_v<OperationType, WaitFor>) {
            (void)op;
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, WaitRegion>) {
            (void)op;
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, WaitOn>) {
            (void)op;
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, WaitPla>) {
            (void)op;
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, WaitOrder>) {
            (void)op;
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, EventTriggered>) {
            (void)op;
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, EventAlias>) {
            (void)op;
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, WaitSensitivity>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, WaitForever>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, Yield>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, Fork>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, ForkEnd>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, WaitFork>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, DisableFork>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, DisableBlock>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, ProcessSelf>) {
            boundary = true;
        } else if constexpr (
            std::is_same_v<OperationType, ProcessStatusQuery>) {
            boundary = true;
        } else if constexpr (
            std::is_same_v<OperationType, ProcessCompleted>) {
            boundary = true;
        } else if constexpr (
            std::is_same_v<OperationType, ProcessAwait>) {
            boundary = true;
        } else if constexpr (
            std::is_same_v<OperationType, ProcessKill>
            || std::is_same_v<OperationType, ProcessSuspend>
            || std::is_same_v<OperationType, ProcessResume>
            || std::is_same_v<OperationType, ProcessGetRandState>
            || std::is_same_v<OperationType, ProcessSetRandState>
            || std::is_same_v<OperationType, ProcessSrandom>) {
            boundary = true;
        } else if constexpr (
            std::is_same_v<OperationType, MailboxCreate>
            || std::is_same_v<OperationType, MailboxPut>
            || std::is_same_v<OperationType, MailboxGet>
            || std::is_same_v<OperationType, MailboxNum>
            || std::is_same_v<OperationType, DynamicEvent>
            || std::is_same_v<OperationType, SemaphoreCreate>
            || std::is_same_v<OperationType, SemaphoreGet>
            || std::is_same_v<OperationType, SemaphorePut>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, Jump>) {
            if (op.target >= process.program().operations().size()) {
                fail(process, "jump target is outside the operation stream");
            }
            process.pc = op.target;
        } else if constexpr (std::is_same_v<OperationType, Call>) {
            if (op.stack.capacity == 0) {
                execute_dynamic_call(process, op);
                return;
            }
            const auto pointer = get_register(process, op.stack.pointer).low_word();
            if (pointer.bval != 0) {
                fail(process, "call-stack pointer is unknown");
            }
            if (pointer.aval >= op.stack.capacity) {
                fail(process, "call-stack capacity is exhausted");
            }
            if (op.target >= process.program().operations().size()
                || op.return_target
                    >= process.program().operations().size()) {
                fail(process, "call target is outside the operation stream");
            }
            get_register(
                process,
                static_cast<RegisterId>(
                    op.stack.entries + pointer.aval)) = PackedLogic4::from_aval_bval(32, op.return_target, 0);
            get_register(process, op.stack.pointer) = PackedLogic4::from_aval_bval(
                32, pointer.aval + 1U, 0);
            process.pc = op.target;
        } else if constexpr (std::is_same_v<OperationType, Return>) {
            if (op.stack.capacity == 0) {
                execute_dynamic_return(process, op);
                return;
            }
            const auto pointer = get_register(process, op.stack.pointer).low_word();
            if (pointer.bval != 0) {
                fail(process, "call-stack pointer is unknown");
            }
            if (pointer.aval == 0
                || pointer.aval > op.stack.capacity) {
                fail(process, "call-stack underflow");
            }
            const auto next_pointer = pointer.aval - 1U;
            const auto target = get_register(
                process,
                static_cast<RegisterId>(
                    op.stack.entries + next_pointer))
                                    .low_word();
            if (target.bval != 0
                || target.aval
                    >= process.program().operations().size()) {
                fail(process, "call-stack return target is invalid");
            }
            get_register(process, op.stack.pointer) = PackedLogic4::from_aval_bval(
                32, next_pointer, 0);
            process.pc = static_cast<InstructionIndex>(target.aval);
        } else if constexpr (
            std::is_same_v<OperationType, CallableFramePush>) {
            push_callable_frame(process, op);
        } else if constexpr (
            std::is_same_v<OperationType, CallableFramePop>) {
            pop_callable_frame(process, op);
        } else if constexpr (std::is_same_v<OperationType, Branch>) {
            const auto& condition = get_register(process, op.condition);
            if (condition.width() != 1) {
                fail(process, "branch condition must be scalar");
            }
            const auto value = condition.get(0);
            InstructionIndex target { };
            if (value != Logic4::zero && value != Logic4::one) {
                if (op.unknown_policy
                    == UnknownBranchPolicy::when_false) {
                    target = op.when_false;
                } else {
                    fail(
                        process,
                        "branch condition is unknown or high impedance");
                }
            } else {
                target = value == Logic4::one ? op.when_true : op.when_false;
            }
            if (target >= process.program().operations().size()) {
                fail(process, "branch target is outside the operation stream");
            }
            process.pc = target;
        } else if constexpr (std::is_same_v<OperationType, DebugPoint>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, Assert>) {
            const auto& condition = get_register(process, op.condition);
            if (condition.width() != 1 || condition.get(0) != Logic4::one) {
                const auto message = op.message.empty()
                    ? std::string_view { "assertion failed" }
                    : std::string_view { op.message };
                if (process.program().language_standard() == "2019") {
                    execute_vhdl_report(
                        process, process.pc, message, op.severity,
                        op.source, false);
                    ++process.pc;
                    return;
                }
                if (op.severity != AssertionSeverity::failure
                    && report_hook) {
                    report_hook(
                        process.id,
                        message,
                        op.severity,
                        op.source,
                        scheduler.now(),
                        scheduler.delta());
                }
                if (op.severity == AssertionSeverity::failure) {
                    throw AssertionError(
                        process.id,
                        process.pc,
                        std::string { message },
                        op.severity,
                        op.source);
                }
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Display>) {
            if (op.postponed) {
                auto publish = [this,
                                   process_id = process.id,
                                   text = op.text,
                                   newline = op.newline](
                                  Scheduler& runtime) {
                    if (output_hook) {
                        output_hook(
                            process_id,
                            text,
                            newline,
                            runtime.now(),
                            runtime.delta());
                    }
                };
                if (process.program().scheduling_domain()
                    == ProcessSchedulingDomain::systemverilog) {
                    scheduler.schedule_end_of_time_slot(
                        process.id, std::move(publish));
                } else {
                    scheduler.schedule(
                        SchedulerPhase::postponed,
                        process.id,
                        std::move(publish));
                }
            } else if (output_hook) {
                output_hook(
                    process.id,
                    op.text,
                    op.newline,
                    scheduler.now(),
                    scheduler.delta());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, FormatDisplay>) {
            // %t of a time value follows $timeformat (IEEE
            // 1800-2017 21.2.1.3, 20.4.3).
            const auto time_value = op.format == OutputFormat::time
                ? decode_systemverilog_scalar_payload(
                      get_register(process, op.source), op.scalar_kind)
                : decltype(decode_systemverilog_scalar_payload(
                      get_register(process, op.source),
                      op.scalar_kind)) { };
            auto tick = op.format == OutputFormat::time
                    && time_value
                ? time_value.value.as_time()
                : std::nullopt;
            // An integral %t argument is in its module's time unit.
            // A `time` value, like an integral one, counts in its
            // module's unit.
            const bool integral_time = op.format == OutputFormat::time
                && (op.scalar_kind == SystemVerilogScalarKind::None
                    || (op.scalar_kind != SystemVerilogScalarKind::Chandle
                        && op.time_unit_femtoseconds != 0U));
            if (integral_time) {
                tick = integral_time_ticks(
                    get_register(process, op.source),
                    op.signed_decimal, op.time_unit_femtoseconds,
                    time_format, op.scalar_kind);
            }
            auto text = tick
                ? make_time_output(op.prefix, op.suffix, *tick,
                      time_format,
                      op.minimum_width == 0
                          && !op.suppress_leading_zero,
                      op.minimum_width, op.left_justify, op.zero_pad)
                : make_formatted_output(
                op.prefix,
                op.suffix,
                integral_time ? OutputFormat::decimal : op.format,
                get_register(process, op.source),
                op.signed_decimal,
                op.suppress_leading_zero,
                op.minimum_width,
                op.left_justify,
                op.zero_pad,
                op.scalar_kind,
                op.precision);
            if (op.postponed) {
                auto publish = [this,
                                   process_id = process.id,
                                   text = std::move(text),
                                   newline = op.newline](
                                  Scheduler& runtime) {
                    if (output_hook) {
                        output_hook(
                            process_id,
                            text,
                            newline,
                            runtime.now(),
                            runtime.delta());
                    }
                };
                if (process.program().scheduling_domain()
                    == ProcessSchedulingDomain::systemverilog) {
                    scheduler.schedule_end_of_time_slot(
                        process.id, std::move(publish));
                } else {
                    scheduler.schedule(
                        SchedulerPhase::postponed,
                        process.id,
                        std::move(publish));
                }
            } else if (output_hook) {
                output_hook(
                    process.id,
                    text,
                    op.newline,
                    scheduler.now(),
                    scheduler.delta());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, StringDisplay>) {
            auto text = op.prefix
                + get_string_register(process, op.source)
                + op.suffix;
            if (op.postponed) {
                auto publish = [this,
                                   process_id = process.id,
                                   text = std::move(text),
                                   newline = op.newline](
                                  Scheduler& runtime) {
                    if (output_hook) {
                        output_hook(
                            process_id,
                            text,
                            newline,
                            runtime.now(),
                            runtime.delta());
                    }
                };
                if (process.program().scheduling_domain()
                    == ProcessSchedulingDomain::systemverilog) {
                    scheduler.schedule_end_of_time_slot(
                        process.id, std::move(publish));
                } else {
                    scheduler.schedule(
                        SchedulerPhase::postponed,
                        process.id,
                        std::move(publish));
                }
            } else if (output_hook) {
                output_hook(
                    process.id,
                    text,
                    op.newline,
                    scheduler.now(),
                    scheduler.delta());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, StringReport>) {
            const auto& encoded = get_register(process, op.severity);
            if (encoded.width() != 2
                || encoded.get(0) == Logic4::x
                || encoded.get(0) == Logic4::z
                || encoded.get(1) == Logic4::x
                || encoded.get(1) == Logic4::z) {
                fail(
                    process,
                    "VHDL severity expression produced an invalid value");
            }
            const auto ordinal = (encoded.get(0) == Logic4::one ? 1U : 0U)
                | (encoded.get(1) == Logic4::one ? 2U : 0U);
            const auto severity = static_cast<AssertionSeverity>(ordinal);
            const auto& message = get_string_register(process, op.message);
            if (process.program().language_standard() == "2019") {
                execute_vhdl_report(
                    process, process.pc, message, severity,
                    op.source, op.standalone);
            } else if (severity == AssertionSeverity::failure) {
                if (op.standalone && report_hook) {
                    report_hook(
                        process.id,
                        message,
                        severity,
                        op.source,
                        scheduler.now(),
                        scheduler.delta());
                }
                throw AssertionError(
                    process.id,
                    process.pc,
                    message.empty()
                        ? (op.standalone
                                  ? "report failure"
                                  : "assertion failed")
                        : message,
                    severity,
                    op.source,
                    op.standalone);
            } else if (report_hook) {
                report_hook(
                    process.id,
                    message,
                    severity,
                    op.source,
                    scheduler.now(),
                    scheduler.delta());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, TimeDisplay>) {
            auto text = make_time_output(
                op.prefix,
                op.suffix,
                scheduler.now(),
                time_format,
                op.use_timeformat_width,
                op.minimum_width,
                op.left_justify,
                op.zero_pad);
            if (op.postponed) {
                auto publish = [this,
                                   process_id = process.id,
                                   text = std::move(text),
                                   newline = op.newline](
                                  Scheduler& runtime) {
                    if (output_hook) {
                        output_hook(
                            process_id,
                            text,
                            newline,
                            runtime.now(),
                            runtime.delta());
                    }
                };
                if (process.program().scheduling_domain()
                    == ProcessSchedulingDomain::systemverilog) {
                    scheduler.schedule_end_of_time_slot(
                        process.id, std::move(publish));
                } else {
                    scheduler.schedule(
                        SchedulerPhase::postponed,
                        process.id,
                        std::move(publish));
                }
            } else if (output_hook) {
                output_hook(
                    process.id,
                    text,
                    op.newline,
                    scheduler.now(),
                    scheduler.delta());
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, MonitorInstall>) {
            install_monitor(process.id, op);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, MonitorControl>) {
            set_monitor_enabled(op.enabled);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, CoverageSample>) {
            if (!coverage_sample_hook) {
                fail(process, "coverage sampling service is unavailable");
            }
            std::vector<PackedLogic4> actuals;
            actuals.reserve(op.actuals.size());
            for (const auto actual : op.actuals) {
                actuals.push_back(get_register(process, actual));
            }
            coverage_sample_hook(
                op.instance_identity, actuals, op.signed_actuals,
                op.scalar_kinds,
                op.trigger);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, CoverageQuery>) {
            if (op.kind > CoverageQueryKind::covergroup_instance
                || (op.kind >= CoverageQueryKind::covergroup_type
                    && op.identity.empty())) {
                fail(process, "coverage query kind is invalid");
            }
            if (!coverage_query_hook) {
                fail(process, "coverage query service is unavailable");
            }
            auto value = coverage_query_hook(op.kind, op.identity);
            if (value.width() != 64U || value.is_logic9()) {
                fail(process, "coverage query service returned an invalid real payload");
            }
            get_register(process, op.destination) = std::move(value);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, VhdlPslApi>) {
            const bool query = op.kind == VhdlPslApiKind::assert_failed
                || op.kind == VhdlPslApiKind::is_covered
                || op.kind == VhdlPslApiKind::get_cover_assert
                || op.kind == VhdlPslApiKind::is_assert_covered;
            const bool set = op.kind == VhdlPslApiKind::set_cover_assert;
            const bool clear = op.kind == VhdlPslApiKind::clear_state;
            if ((!query && !set && !clear)
                || (query && (!op.destination || op.enable))
                || (set && (op.destination || !op.enable))
                || (clear && (op.destination || op.enable))) {
                fail(process, "VHDL PSL API operation is invalid");
            }
            if (!vhdl_psl_api_hook) {
                fail(process, "VHDL PSL API service is unavailable");
            }
            std::optional<bool> enable;
            if (op.enable) {
                const auto& value = get_register(process, *op.enable);
                if (value.width() != 1U || value.is_logic9()
                    || value.low_word().bval != 0U) {
                    fail(process,
                        "VHDL PSL API enable value is not BOOLEAN");
                }
                enable = (value.low_word().aval & 1U) != 0U;
            }
            const auto result = vhdl_psl_api_hook(op.kind, enable);
            if (op.destination) {
                get_register(process, *op.destination)
                    = PackedLogic4::from_aval_bval(
                        1U, result ? 1U : 0U, 0U);
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, VhdlAssertApi>) {
            execute_vhdl_assert_api(process, op);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, VhdlReflectionApi>) {
            execute_vhdl_reflection_api(process, op);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, CoverageControl>) {
            get_register(process, op.destination) = run_coverage_control(op,
                [&](const RegisterId id) -> const PackedLogic4& {
                    return get_register(process, id);
                },
                [&](const StringRegisterId id) -> const std::string& {
                    return get_string_register(process, id);
                });
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, CoverageAccess>) {
            get_register(process, op.destination) = run_coverage_access(op,
                [&](const RegisterId id) -> const PackedLogic4& {
                    return get_register(process, id);
                },
                [&](const StringRegisterId id) -> const std::string& {
                    return get_string_register(process, id);
                });
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, CodeCoverageHit>) {
            const auto counter
                = process.program().operations().code_coverage_counter(
                    process.pc, op.counter);
            const auto update = code_coverage_counters.record(counter);
            if (update == CodeCoverageCounterUpdate::Unavailable) {
                fail(process, "code coverage counter service is unavailable");
            }
            if (update == CodeCoverageCounterUpdate::OutOfRange) {
                fail(process, "code coverage counter is out of range");
            }
            if (update == CodeCoverageCounterUpdate::FirstOverflow
                && code_coverage_overflow_hook) {
                code_coverage_overflow_hook(counter);
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, RandomValue>) {
            const auto maximum = op.maximum
                ? std::optional<PackedLogic4> {
                      get_register(process, *op.maximum)
                  }
                : std::nullopt;
            const auto minimum = op.minimum
                ? std::optional<PackedLogic4> {
                      get_register(process, *op.minimum)
                  }
                : std::nullopt;
            get_register(process, op.destination) = random_value(
                process.id,
                op.kind,
                maximum,
                minimum);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, RandomDistribution>) {
            execute_random_distribution(process, op);
            ++process.pc;
        } else if constexpr (
            std::is_same_v<OperationType, VhdlEnvironmentTime>
            || std::is_same_v<OperationType,
                VhdlEnvironmentTimeToString>
            || std::is_same_v<OperationType,
                VhdlEnvironmentDirectory>
            || std::is_same_v<OperationType,
                VhdlEnvironmentGetenv>
            || std::is_same_v<OperationType,
                VhdlEnvironmentCallPath>
            || std::is_same_v<OperationType,
                VhdlEnvironmentGetCallPath>
            || std::is_same_v<OperationType, SignalStrengthText>
            || std::is_same_v<OperationType, RuntimeStringFormat>
            || std::is_same_v<OperationType, ClassPropertyContainerRead>
            || std::is_same_v<OperationType, ClassPropertyContainerWrite>) {
            boundary = true;
        } else if constexpr (
            std::is_same_v<OperationType, ScopeRandomize>) {
            execute_scope_randomize(process, instruction, op);
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Report>) {
            if (process.program().language_standard() == "2019") {
                execute_vhdl_report(
                    process, process.pc, op.message, op.severity,
                    op.source, true);
            } else if (report_hook) {
                report_hook(
                    process.id,
                    op.message,
                    op.severity,
                    op.source,
                    scheduler.now(),
                    scheduler.delta());
            }
            if (process.program().language_standard() != "2019"
                && op.severity == AssertionSeverity::failure) {
                throw AssertionError(
                    process.id,
                    process.pc,
                    op.message.empty() ? "report failure" : op.message,
                    op.severity,
                    op.source,
                    true);
            }
            ++process.pc;
        } else if constexpr (std::is_same_v<OperationType, Pause>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, Stop>) {
            boundary = true;
        } else if constexpr (std::is_same_v<OperationType, Halt>) {
            boundary = true;
        } else {
            if constexpr (requires {
                              execute_file(process, op);
                          }) {
                execute_file(process, op);
            } else if constexpr (requires {
                                     execute_container(process, op);
                                 }) {
                execute_container(process, op);
            } else {
                static_assert(
                    sizeof(op) == 0,
                    "unhandled SimIR operation");
            }
        }
            }();
        }
    }();
    return boundary;
}

template <typename Op>
std::uint32_t Interpreter::Impl::hosted_step(ProcessState& process,
    const InstructionIndex instruction)
{
    const auto& operation
        = std::as_const(process.program().operations())[instruction];
    const bool boundary
        = execute_operation(process, instruction, operation_get<Op>(operation));
    if (boundary && !continue_after_boundary(process, operation, instruction)) {
        return static_kernel_detail::operation_tier_stop;
    }
    return process.halted ? static_kernel_detail::operation_tier_stop : process.pc;
}

const std::array<Interpreter::Impl::HostedStep,
    static_kernel_detail::operation_kind_count>&
Interpreter::Impl::hosted_steps()
{
    using static_kernel_detail::OperationKindAt;
    static constexpr auto steps = []<std::size_t... K>(std::index_sequence<K...>) {
        return std::array<HostedStep, static_kernel_detail::operation_kind_count> {
            &Impl::hosted_step<OperationKindAt<K>>...
        };
    }(std::make_index_sequence<static_kernel_detail::operation_kind_count>());
    return steps;
}

SimulationTick Interpreter::Impl::projected_element_delay(
    ProcessState& process, const ProjectedWaveformElement& element)
{
    if (!element.delay_source) {
        return element.delay;
    }
    return projected_element_delay(
        element, get_register(process, *element.delay_source));
}

SimulationTick Interpreter::Impl::projected_element_delay(
    const ProjectedWaveformElement& element, const PackedLogic4& payload)
{
    if (!element.delay_source) {
        return element.delay;
    }
    WaitFor wait;
    wait.delay = element.delay;
    wait.source = element.delay_source;
    wait.source_width = element.delay_source_width;
    wait.source_signed = true;
    // An unknown or negative TIME is an error the bounded runtime treats as
    // an immediate transaction.
    if (payload.width() != element.delay_source_width
        || payload.low_word().bval != 0U
        || (element.delay_source_width != 0U
            && payload.get(element.delay_source_width - 1U) == Logic4::one)) {
        return SimulationTick { 0U };
    }
    return normalized_dynamic_wait_delay(wait, payload);
}

SimulationTick Interpreter::Impl::projected_rejection(
    const SimulationTick rejection,
    const std::vector<ProjectedWaveformValue>& elements)
{
    if (rejection != std::numeric_limits<SimulationTick>::max()) {
        return rejection;
    }
    return elements.empty() ? SimulationTick { 0U } : elements.front().delay;
}


const Interpreter::Impl::SignalSelectionHistory*
Interpreter::Impl::selected_attribute_history(const SignalId signal,
    const std::optional<SignalSelection>& selection,
    const char* attribute) const
{
    (void)get_signal(signal);
    if (!selection) {
        return nullptr;
    }
    const auto* selected = find_signal_selection_history(signal, *selection);
    if (selected == nullptr) {
        throw std::invalid_argument(std::string("selected SimIR ")
            + attribute + " range was not registered");
    }
    return selected;
}

bool Interpreter::Impl::signal_event_attribute(const SignalId signal,
    const std::optional<SignalSelection>& selection) const
{
    const auto* selected
        = selected_attribute_history(signal, selection, "event");
    return signal_attribute_detail::in_current_delta(*this,
        selected != nullptr ? selected->last_event : signal_events[signal]);
}

bool Interpreter::Impl::signal_active_attribute(const SignalId signal,
    const std::optional<SignalSelection>& selection) const
{
    const auto* selected
        = selected_attribute_history(signal, selection, "active");
    return signal_attribute_detail::in_current_delta(*this,
        selected != nullptr ? selected->last_transaction
                            : signal_transactions[signal]);
}

const PackedLogic4& Interpreter::Impl::signal_last_value_attribute(
    const SignalId signal, const std::optional<SignalSelection>& selection)
{
    const auto* selected
        = selected_attribute_history(signal, selection, "last-value");
    return selected != nullptr ? selected->last_value
                               : logical_signal_last_value(signal);
}

SimulationTick Interpreter::Impl::signal_last_event_attribute(
    const SignalId signal,
    const std::optional<SignalSelection>& selection) const
{
    const auto* selected
        = selected_attribute_history(signal, selection, "last-event");
    return signal_attribute_detail::elapsed_since(*this,
        selected != nullptr ? selected->last_event : signal_events[signal]);
}

SimulationTick Interpreter::Impl::signal_last_active_attribute(
    const SignalId signal, const std::optional<SignalSelection>& selection)
{
    const auto* selected = selected_attribute_history(
        signal, selection, "last-active");
    return selected != nullptr
        ? signal_attribute_detail::elapsed_since(
              *this, selected->last_transaction)
        : signal_attribute_detail::last_active(*this, signal);
}


void Interpreter::Impl::schedule_after_update(const ProcessId driver,
    const SignalId signal, PackedLogic4 value,
    const std::optional<std::size_t> offset,
    const SignalUpdateDomain domain, const SimulationTick delay)
{
    if (domain != SignalUpdateDomain::generic) {
        schedule_systemverilog_update(
            driver, signal, std::move(value), offset, domain, delay);
        return;
    }
    scheduler.schedule_after(delay, SchedulerPhase::update, driver,
        [this, driver, signal, offset, value = std::move(value)](
            Scheduler&) mutable {
            if (offset) {
                stage_update_slice(driver, signal, std::move(value), *offset);
            } else {
                stage_update(driver, signal, std::move(value));
            }
        });
}

TransitionDelays Interpreter::Impl::write_delayed_delays(
    const WriteDelayed& operation,
    const std::function<const PackedLogic4&(RegisterId)>& read)
{
    const auto evaluate = [&](const WaitFor& wait) {
        if (!wait.source) {
            return wait.delay;
        }
        const auto& payload = read(*wait.source);
        // An unknown integral delay counts as zero (9.4.1).
        return wait.source_kind == SystemVerilogScalarKind::None
                && payload.low_word().bval != 0U
            ? SimulationTick { 0U }
            : normalized_dynamic_wait_delay(wait, payload);
    };
    const auto rise = evaluate(operation.delay);
    const auto fall = operation.fall ? evaluate(*operation.fall) : rise;
    const auto turnoff = operation.turnoff ? evaluate(*operation.turnoff)
                                           : std::min(rise, fall);
    return TransitionDelays { rise, fall, turnoff };
}

void Interpreter::Impl::schedule_write_delayed(const ProcessId driver,
    const WriteDelayed& operation, PackedLogic4 value,
    const TransitionDelays& delays)
{
    if (operation.inertial) {
        schedule_inertial(driver, operation.signal, std::move(value),
            operation.offset, delays, operation.domain);
    } else {
        schedule_after_update(driver, operation.signal, std::move(value),
            operation.offset, operation.domain, delays.rise);
    }
}

} // namespace fsim::runtime::simir
