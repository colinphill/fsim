// SPDX-License-Identifier: Apache-2.0

#include "../../src/runtime/simir_internal.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct OwnedDriverDemotionTestAccess {
    static bool startup_banked(
        const Interpreter& interpreter, const ProcessId process)
    {
        return interpreter.impl_->processes.is_compact_constant(process);
    }

    static bool data_only_startup_write(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto* const compact
            = interpreter.impl_->processes.compact_constant(process);
        return compact != nullptr
            && compact->startup_write_bank != nullptr
            && compact->program_storage->instance_program.operations.empty();
    }

    static std::optional<CopyRegister> startup_constant_copy(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto* const compact
            = interpreter.impl_->processes.compact_constant(process);
        if (compact == nullptr || compact->startup_write_bank == nullptr) {
            return std::nullopt;
        }
        return compact->startup_write_bank->constant_copy;
    }

    static bool startup_write_body_materialized(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto* const compact
            = interpreter.impl_->processes.compact_constant(process);
        return compact != nullptr
            && compact->startup_write_bank != nullptr
            && static_cast<bool>(compact->startup_write_bank
                    ->cached_operations());
    }

    static bool startup_queued(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto* const compact
            = interpreter.impl_->processes.compact_constant(process);
        return compact != nullptr && compact->queued;
    }

    static bool projected_slice_descriptor_matches(
        const Interpreter& interpreter,
        const ProcessId process,
        const SignalId signal,
        const std::uint32_t offset,
        const std::size_t width,
        const ValueKind value_kind,
        const SimulationTick delay,
        const SimulationTick rejection,
        const ProjectedDelayMode mode)
    {
        const auto* const compact
            = interpreter.impl_->processes.compact_constant(process);
        if (compact == nullptr || compact->id != process
            || compact->signal != signal || !compact->slice
            || compact->offset != offset) {
            return false;
        }
        const auto program = interpreter.impl_->processes.program_view(process);
        const auto& regions = program.driver_regions();
        const auto register_kinds
            = process_layout_detail::ProcessLayoutAccess::view(
                program.register_value_kinds());
        auto load_operation = program.operations().expanded(1U);
        auto write_operation = program.operations().expanded(2U);
        const auto* const load = operation_get_if<LoadConstant>(&load_operation);
        const auto* const write
            = operation_get_if<WriteProjectedSlice>(&write_operation);
        return compact->signal == signal && compact->slice
            && compact->offset == offset && compact->projected
            && compact->update_domain == SignalUpdateDomain::generic
            && compact->projected_delay == delay
            && compact->projected_rejection == rejection
            && compact->projected_mode == mode
            && program.scheduling_domain() == ProcessSchedulingDomain::generic
            && program.register_count() == 1U
            && register_kinds.size() == 1U
            && register_kinds.front() == value_kind
            && load != nullptr && load->destination == 0U
            && load->value.width() == width
            && load->value.is_logic9() == (value_kind == ValueKind::logic9)
            && write != nullptr && write->signal == signal
            && write->source == 0U && write->offset == offset
            && write->delay == delay && write->rejection == rejection
            && write->mode == mode
            && regions.size() == 1U && regions.front().signal == signal
            && !regions.front().whole && regions.front().offset == offset
            && regions.front().width == width;
    }

    static bool process_frame_materialized(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto* const state
            = interpreter.impl_->processes.full_state_if_present(process);
        return state != nullptr && static_cast<bool>(state->frame);
    }

    static SignalId startup_signal(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto* const compact
            = interpreter.impl_->processes.compact_constant(process);
        return compact != nullptr ? compact->signal
                                  : std::numeric_limits<SignalId>::max();
    }

    static PackedLogic4 startup_value(
        const Interpreter& interpreter, const ProcessId process)
    {
        if (const auto* const compact
            = interpreter.impl_->processes.compact_constant(process);
            compact != nullptr && compact->startup_write_bank != nullptr) {
            return compact->startup_write_bank->value;
        }
        const auto program = interpreter.impl_->processes.program_view(process);
        auto operation = program.operations().expanded(1U);
        const auto* const load = operation_get_if<LoadConstant>(&operation);
        return load != nullptr ? load->value : PackedLogic4 { };
    }

    static PackedLogic4 current_signal_value(
        const Interpreter& interpreter, const SignalId signal)
    {
        return interpreter.impl_->logical_signal_value(signal);
    }

    static PackedLogic4 last_signal_value(
        const Interpreter& interpreter, const SignalId signal)
    {
        return interpreter.impl_->logical_signal_last_value(signal);
    }

    static PackedLogic4 stored_signal_value(
        const Interpreter& interpreter, const SignalId signal)
    {
        return interpreter.impl_->driven_values[signal];
    }

    static PackedLogic4 stored_driver_value(
        const Interpreter& interpreter, const ProcessId process,
        const SignalId signal)
    {
        const auto* const record
            = interpreter.impl_->driver_values.at(signal).find(process);
        if (record == nullptr) {
            throw std::logic_error { "expected original projected driver" };
        }
        return record->value;
    }

    static std::optional<std::pair<SimulationTick, std::uint64_t>>
    signal_transaction_stamp(
        const Interpreter& interpreter, const SignalId signal)
    {
        return interpreter.impl_->signal_transactions.at(signal);
    }

    static std::size_t compact_count(const Interpreter& interpreter)
    {
        return interpreter.impl_->processes.compact_constant_count();
    }

    static std::size_t full_state_count(const Interpreter& interpreter)
    {
        std::size_t result { };
        for (ProcessId id = 0U; id < interpreter.impl_->processes.size(); ++id) {
            if (interpreter.impl_->processes.full_state_if_present(id)
                != nullptr) {
                ++result;
            }
        }
        return result;
    }

    static const void* full_state_identity(
        const Interpreter& interpreter, const ProcessId process)
    {
        return interpreter.impl_->processes.full_state_if_present(process);
    }

    static std::pair<std::size_t, std::size_t> per_process_payload_bytes()
    {
        return {
            sizeof(Interpreter::Impl::ProcessState)
                + sizeof(Interpreter::Impl::ProcessColdState),
            sizeof(Interpreter::Impl::ConstantDriverStartupEntry)
                + sizeof(ProcessProgramStorage)
                + sizeof(ProcessStartupWriteBank),
        };
    }

    static std::size_t program_operation_count(
        const Interpreter& interpreter, const ProcessId process)
    {
        return interpreter.impl_->processes.operation_count(process);
    }

    static bool public_facade_materialized(
        const Interpreter& interpreter, const ProcessId process)
    {
        return interpreter.impl_->processes.public_facade_materialized(
            process);
    }

    static const void* program_storage_identity(
        const Interpreter& interpreter, const ProcessId process)
    {
        if (const auto* const compact
            = interpreter.impl_->processes.compact_constant(process)) {
            return compact->program_storage.get();
        }
        const auto* const full
            = interpreter.impl_->processes.full_state_if_present(process);
        return full == nullptr
            ? nullptr
            : std::addressof(full->cold().program_storage());
    }

    static const void* compact_descriptor_identity(
        const Interpreter& interpreter, const std::size_t compact_index)
    {
        const auto& descriptors
            = interpreter.impl_->processes.compact_constants_;
        return compact_index < descriptors.size()
            ? std::addressof(descriptors[compact_index]) : nullptr;
    }

    static bool compact_descriptor_is_tombstone(
        const Interpreter& interpreter, const std::size_t compact_index)
    {
        const auto& descriptors
            = interpreter.impl_->processes.compact_constants_;
        return compact_index < descriptors.size()
            && descriptors[compact_index].program_storage == nullptr;
    }

    static void seed_compact_profile_counters(
        Interpreter& interpreter, const ProcessId process)
    {
        auto* const compact
            = interpreter.impl_->processes.compact_constant(process);
        if (compact == nullptr) {
            throw std::logic_error { "expected compact profile test process" };
        }
        compact->profile_calls = 11U;
        compact->profile_interpreter_operations = 12U;
        compact->profile_updates = 13U;
        compact->profile_total_nanoseconds = 14U;
    }

    static bool profile_counters(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto* const full
            = interpreter.impl_->processes.full_state_if_present(process);
        return full != nullptr
            && full->cold().profile_calls == 11U
            && full->cold().profile_interpreter_operations == 12U
            && full->cold().profile_updates == 13U
            && full->cold().profile_total_nanoseconds == 14U;
    }

    static void promote_for_test(
        Interpreter& interpreter, const ProcessId process)
    {
        static_cast<void>(interpreter.impl_->get_process(process));
    }

    static bool process_bodies_shared(
        const Interpreter& interpreter,
        const ProcessId first,
        const ProcessId second)
    {
        const auto first_program
            = interpreter.impl_->processes.program_view(first);
        const auto second_program
            = interpreter.impl_->processes.program_view(second);
        return first_program.operations().shares_body_with(
            second_program.operations());
    }

    static std::optional<DebugPoint> process_entry_debug_point(
        const Interpreter& interpreter, const ProcessId process)
    {
        const auto program
            = interpreter.impl_->processes.program_view(process);
        auto operation = program.operations().expanded(0U);
        const auto* const point = operation_get_if<DebugPoint>(&operation);
        return point != nullptr
            ? std::optional<DebugPoint> { *point }
            : std::nullopt;
    }

    static std::vector<RegionConeConstantInput> region_constant_inputs(
        Interpreter& interpreter)
    {
        interpreter.impl_->build_native_signal_dependency_masks();
        auto snapshot = interpreter.impl_->build_region_runtime_snapshot(
            false, true, false);
        std::vector<RegionConeConstantInput> inputs;
        for (const auto& program : snapshot.programs_by_component) {
            if (!program) {
                continue;
            }
            inputs.insert(inputs.end(),
                program->activation_kernel.constant_inputs.begin(),
                program->activation_kernel.constant_inputs.end());
        }
        return inputs;
    }

    static void force_signal(
        Interpreter& interpreter, const SignalId signal, PackedLogic4 value)
    {
        interpreter.impl_->force_slice(signal, std::move(value), 0U);
    }

    static void release_signal(
        Interpreter& interpreter, const SignalId signal,
        const std::size_t width)
    {
        interpreter.impl_->release_slice(signal, 0U, width);
    }

    static void force_driver(
        Interpreter& interpreter, const ProcessId owner,
        const SignalId signal, PackedLogic4 value)
    {
        interpreter.impl_->force_driver_slice(
            owner, signal, std::move(value), 0U);
    }

    static void release_driver(
        Interpreter& interpreter, const ProcessId owner,
        const SignalId signal, const std::size_t width)
    {
        interpreter.impl_->release_driver_slice(owner, signal, 0U, width);
    }
};

} // namespace fsim::runtime::simir

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

Process constant_driver_process(
    const ProcessId id,
    const SignalId target,
    PackedLogic4 value,
    const bool eligible)
{
    Process process;
    process.id = id;
    process.name = "constant_driver_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    process.driver_regions = { { target, 0U, 0U, true } };
    process.operations.emplace_back(DebugPoint {
        DebugPointKind::process_entry, { }
    });
    process.operations.emplace_back(LoadConstant { 0U, std::move(value) });
    if (!eligible) {
        process.operations.emplace_back(CopyRegister { 0U, 0U });
    }
    process.operations.emplace_back(WriteUpdate {
        target, 0U, SignalUpdateDomain::systemverilog_active
    });
    process.operations.emplace_back(Halt { });
    return process;
}

Process two_register_constant_driver_process(
    const ProcessId id,
    const SignalId target,
    PackedLogic4 value,
    const bool include_coverage_hit = false)
{
    Process process;
    process.id = id;
    process.name = "two_register_constant_driver_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 2U;
    process.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4
    };
    process.driver_regions = { { target, 0U, 0U, true } };
    process.operations = {
        DebugPoint {
            DebugPointKind::process_entry,
            SourceLocation { "two_register_constant_copy.sv", 4U, 1U },
            "copy.entry"
        },
        DebugPoint {
            DebugPointKind::statement,
            SourceLocation { "two_register_constant_copy.sv", 5U, 3U },
            "copy.statement"
        },
        LoadConstant { 0U, std::move(value) },
        CopyRegister { 1U, 0U },
        WriteUpdate {
            target, 1U, SignalUpdateDomain::systemverilog_active
        },
    };
    if (include_coverage_hit) {
        process.operations.emplace_back(CodeCoverageHit {
            { 0xA5U, static_cast<std::uint64_t>(id) + 1U },
            CodeCoverageMetric::Statement,
            { 0U },
        });
    }
    process.operations.emplace_back(Halt { });
    return process;
}

void require_two_register_constant_copy_program(
    const Process& process,
    const ProcessId id,
    const SignalId target,
    const PackedLogic4& value,
    const bool has_coverage_hit)
{
    const ProcessProgramView program { process };
    const SourceLocation expected_entry_source {
        "two_register_constant_copy.sv", 4U, 1U
    };
    const SourceLocation expected_statement_source {
        "two_register_constant_copy.sv", 5U, 3U
    };
    require(program.id() == id
            && program.name()
                == "two_register_constant_driver_" + std::to_string(id)
            && program.scheduling_domain()
                == ProcessSchedulingDomain::systemverilog
            && program.register_count() == 2U,
        "the copied constant retains its original process and register metadata");
    const auto register_kinds
        = process_layout_detail::ProcessLayoutAccess::view(
            program.register_value_kinds());
    require(register_kinds.size() == 2U
            && register_kinds[0] == ValueKind::logic4
            && register_kinds[1] == ValueKind::logic4,
        "the copied constant retains both Logic4 registers");
    const auto& operations = program.operations();
    require(operations.size() == (has_coverage_hit ? 7U : 6U),
        "the copied constant retains its exact operation count");
    auto entry_operation = operations.expanded(0U);
    auto statement_operation = operations.expanded(1U);
    auto load_operation = operations.expanded(2U);
    auto copy_operation = operations.expanded(3U);
    auto write_operation = operations.expanded(4U);
    const auto* const entry
        = operation_get_if<DebugPoint>(&entry_operation);
    const auto* const statement
        = operation_get_if<DebugPoint>(&statement_operation);
    const auto* const load
        = operation_get_if<LoadConstant>(&load_operation);
    const auto* const copy
        = operation_get_if<CopyRegister>(&copy_operation);
    const auto* const write
        = operation_get_if<WriteUpdate>(&write_operation);
    const auto& regions = program.driver_regions();
    require(entry && entry->kind == DebugPointKind::process_entry
            && entry->source == expected_entry_source
            && entry->scope == "copy.entry"
            && statement && statement->kind == DebugPointKind::statement
            && statement->source == expected_statement_source
            && statement->scope == "copy.statement"
            && load && load->destination == 0U && load->value == value
            && copy && copy->destination == 1U && copy->source == 0U
            && write && write->signal == target && write->source == 1U
            && write->domain == SignalUpdateDomain::systemverilog_active
            && regions.size() == 1U
            && regions.front().signal == target
            && regions.front().whole && regions.front().offset == 0U
            && regions.front().width == 0U,
        "the copied constant retains its entry, statement, load, copy and write sequence");
    if (has_coverage_hit) {
        auto hit_operation = operations.expanded(5U);
        const auto* const hit = operation_get_if<CodeCoverageHit>(
            &hit_operation);
        const CodeCoveragePointId expected_point {
            0xA5U, static_cast<std::uint64_t>(id) + 1U
        };
        require(hit && hit->point == expected_point
                && hit->metric == CodeCoverageMetric::Statement
                && hit->counter == CodeCoverageCounterId { 0U },
            "the checked near miss retains its exact coverage operation");
    }
    auto halt_operation = operations.expanded(
        has_coverage_hit ? 6U : 5U);
    const auto* const halt = operation_get_if<Halt>(&halt_operation);
    require(halt && !halt->program_exit,
        "the copied constant retains its ordinary halt operation");
}

Process constant_driver_slice_process(
    const ProcessId id,
    const SignalId target,
    const std::uint32_t offset,
    PackedLogic4 value)
{
    Process process;
    process.id = id;
    process.name = "constant_slice_driver_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    process.driver_regions = { {
        target, offset, static_cast<std::uint32_t>(value.width()), false
    } };
    process.operations = {
        DebugPoint { DebugPointKind::process_entry, { } },
        LoadConstant { 0U, std::move(value) },
        WriteUpdateSlice {
            target, 0U, offset, SignalUpdateDomain::systemverilog_active
        },
        Halt { },
    };
    return process;
}

PackedLogic4 logic9_cycle(const std::size_t width, const std::size_t start = 0U)
{
    constexpr std::string_view states { "UX01ZWLH-" };
    std::string text;
    text.reserve(width);
    for (std::size_t index = 0U; index < width; ++index) {
        text.push_back(states[(index + start) % states.size()]);
    }
    return PackedLogic4::from_logic9_msb_string(text);
}

Process logic9_constant_driver_process(
    const ProcessId id,
    const SignalId target,
    PackedLogic4 value)
{
    Process process;
    process.id = id;
    process.name = "logic9_constant_driver_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic9 };
    process.driver_regions = { { target, 0U, 0U, true } };
    process.operations = {
        DebugPoint { DebugPointKind::process_entry, { } },
        LoadConstant { 0U, std::move(value) },
        WriteUpdate { target, 0U,
            SignalUpdateDomain::systemverilog_active },
        Halt { },
    };
    return process;
}

Process vhdl_projected_logic9_constant_driver_process(
    const ProcessId id,
    const SignalId target,
    PackedLogic4 value,
    const SimulationTick delay,
    const SimulationTick rejection,
    const ProjectedDelayMode mode)
{
    Process process;
    process.id = id;
    process.name = "vhdl_projected_constant_driver_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic9 };
    process.driver_regions = { { target, 0U, 0U, true } };
    process.operations = {
        DebugPoint { DebugPointKind::process_entry, { } },
        LoadConstant { 0U, std::move(value) },
        WriteProjected { target, 0U, delay, rejection, mode },
        Halt { },
    };
    return process;
}

Process vhdl_projected_slice_constant_driver_process(
    const ProcessId id,
    const SignalId target,
    const std::uint32_t offset,
    PackedLogic4 value,
    const SimulationTick delay,
    const SimulationTick rejection,
    const ProjectedDelayMode mode,
    const bool force_checked_route = false,
    const bool whole_driver_region = false)
{
    Process process;
    process.id = id;
    process.name = "vhdl_projected_slice_constant_" + std::to_string(id);
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.register_count = 1U;
    process.register_value_kinds = {
        value.is_logic9() ? ValueKind::logic9 : ValueKind::logic4
    };
    process.driver_regions = { whole_driver_region
            ? Process::DriverRegion { target, 0U, 0U, true }
            : Process::DriverRegion {
                target, offset,
                static_cast<std::uint32_t>(value.width()), false
            }
    };
    process.operations.emplace_back(DebugPoint {
        DebugPointKind::process_entry, { }
    });
    process.operations.emplace_back(LoadConstant { 0U, std::move(value) });
    if (force_checked_route) {
        process.operations.emplace_back(CopyRegister { 0U, 0U });
    }
    process.operations.emplace_back(WriteProjectedSlice {
        target, 0U, offset, delay, rejection, mode
    });
    process.operations.emplace_back(Halt { });
    return process;
}

struct ExecutionPointCapture {
    struct Entry {
        ProcessId process { };
        InstructionIndex instruction { };
        ExecutionPointKind kind { };
        SimulationTick time { };
    };

    std::array<Entry, 4U> entries { };
    std::size_t count { };
    bool overflow { };
    bool stop_at_entry { true };
    bool stop_at_statement { };

    void receive(Scheduler& scheduler, const ExecutionPoint& point)
    {
        if (point.process != 0U) {
            return;
        }
        if (count == entries.size()) {
            overflow = true;
            return;
        }
        entries[count++] = {
            point.process,
            point.instruction,
            point.kind,
            scheduler.now(),
        };
        const bool stop_at_entry_point
            = stop_at_entry
            && point.kind == ExecutionPointKind::process_entry;
        const bool stop_at_statement_point
            = stop_at_statement
            && point.kind == ExecutionPointKind::statement;
        if (stop_at_entry_point) {
            stop_at_entry = false;
            scheduler.request_stop();
        }
        if (stop_at_statement_point) {
            stop_at_statement = false;
            scheduler.request_stop();
        }
    }
};

struct SignalTraceCapture {
    struct Entry {
        SchedulerTraceKind kind { };
        std::optional<SchedulerPhase> phase;
        SimulationTick time { };
        bool systemverilog { };
        std::uint64_t delta { };
    };

    SignalId watched_signal { };
    std::array<Entry, 8U> entries { };
    std::size_t count { };
    bool overflow { };

    static void receive(
        void* const context, const SchedulerTraceRecord& record) noexcept
    {
        auto& capture = *static_cast<SignalTraceCapture*>(context);
        if ((record.kind != SchedulerTraceKind::signal_transaction
                && record.kind != SchedulerTraceKind::signal_change)
            || record.signal != capture.watched_signal) {
            return;
        }
        if (capture.count == capture.entries.size()) {
            capture.overflow = true;
            return;
        }
        capture.entries[capture.count++] = {
            record.kind,
            record.phase,
            record.time,
            record.systemverilog,
            record.delta,
        };
    }
};

struct StartupTransactionOrderCapture {
    std::array<SchedulerTraceRecord, 4U> entries { };
    std::size_t count { };
    bool overflow { };

    static void receive(
        void* const context, const SchedulerTraceRecord& record) noexcept
    {
        auto& capture
            = *static_cast<StartupTransactionOrderCapture*>(context);
        if (record.kind != SchedulerTraceKind::signal_transaction) {
            return;
        }
        if (capture.count == capture.entries.size()) {
            capture.overflow = true;
            return;
        }
        capture.entries[capture.count++] = record;
    }
};

void test_data_only_startup_write_equal_value_transaction()
{
    Interpreter interpreter;
    const auto value = PackedLogic4 { 8U, Logic4::z };
    const auto target = interpreter.add_signal(Signal {
        "bank_equal_value_target", value, ResolutionKind::sv_wire
    });
    static_cast<void>(interpreter.add_process(constant_driver_process(
        0U, target, value, true)));

    interpreter.start();
    require(interpreter.signal_value(target) == value
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target) == value,
        "the equal-value startup witness must begin with matching raw and visible roles");
    require(OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "startup planning retains only the constant write payload");
    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);

    const auto result = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(result.status == RunStatus::completed
            && interpreter.signal_value(target) == value
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target) == value,
        "an equal-value startup write preserves raw and current roles");
    require(trace.count == 1U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[0].time == 0U
            && trace.entries[0].systemverilog
            && trace.entries[0].phase == SchedulerPhase::active,
        "an equal-value startup write keeps its time-zero Active transaction without a value-change event");
    require(!OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "direct startup execution does not reconstruct its operation body");
}

void test_data_only_startup_write_preserves_transaction_order()
{
    Interpreter interpreter;
    const auto initial = PackedLogic4 { 8U, Logic4::z };
    const auto first_signal = interpreter.add_signal(Signal {
        "bank_order_first", initial, ResolutionKind::sv_wire
    });
    const auto second_signal = interpreter.add_signal(Signal {
        "bank_order_second", initial, ResolutionKind::sv_wire
    });
    const auto first_value = PackedLogic4::from_msb_string("10100101");
    const auto second_value = PackedLogic4::from_msb_string("01011010");
    static_cast<void>(interpreter.add_process(constant_driver_process(
        0U, first_signal, first_value, true)));
    static_cast<void>(interpreter.add_process(constant_driver_process(
        1U, second_signal, second_value, true)));

    interpreter.start();
    require(OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 1U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 1U),
        "ordered startup writers remain data-only until execution");
    StartupTransactionOrderCapture trace;
    interpreter.scheduler().set_trace_hook(
        &trace, &StartupTransactionOrderCapture::receive);

    const auto result = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(result.status == RunStatus::completed
            && interpreter.driver_value(0U, first_signal) == first_value
            && interpreter.driver_value(1U, second_signal) == second_value,
        "both original process identities publish their own startup value");
    require(trace.count == 2U && !trace.overflow
            && trace.entries[0].signal == first_signal
            && trace.entries[1].signal == second_signal
            && trace.entries[0].time == 0U
            && trace.entries[1].time == 0U
            && trace.entries[0].phase == SchedulerPhase::active
            && trace.entries[1].phase == SchedulerPhase::active
            && trace.entries[0].systemverilog
            && trace.entries[1].systemverilog,
        "data-only execution preserves the original time-zero Active transaction order");
    require(!OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 1U),
        "ordered startup execution does not reconstruct either operation body");
}

void test_data_only_startup_write_survives_pre_run_observation_and_force()
{
    Interpreter interpreter;
    const auto initial = PackedLogic4 { 8U, Logic4::z };
    const auto value = PackedLogic4::from_msb_string("10100101");
    const auto forced = PackedLogic4::from_msb_string("01011010");
    const auto target = interpreter.add_signal(Signal {
        "bank_pre_run_target", initial, ResolutionKind::sv_wire
    });
    static_cast<void>(interpreter.add_process(constant_driver_process(
        0U, target, value, true)));

    interpreter.start();
    require(OwnedDriverDemotionTestAccess::startup_queued(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "start queues the bodyless writer without executing it");
    require(interpreter.signal_value(target) == initial
            && OwnedDriverDemotionTestAccess::startup_queued(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "a public value observation before run leaves the queued writer bodyless");

    interpreter.force_signal(target, forced);
    require(interpreter.signal_value(target) == forced
            && OwnedDriverDemotionTestAccess::startup_queued(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "a pre-run force changes the resolved value without consuming the queued writer");
    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);

    const auto result = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(result.status == RunStatus::completed
            && interpreter.signal_value(target) == forced
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value
            && !OwnedDriverDemotionTestAccess::startup_queued(
                interpreter, 0U),
        "the retained startup write commits under force with its original driver");
    require(trace.count == 1U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[0].time == 0U
            && trace.entries[0].systemverilog
            && trace.entries[0].phase == SchedulerPhase::active,
        "the original startup write publishes once as a SystemVerilog Active transaction");
    require(!OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "pre-run observation, force, and execution do not retain reconstructed body storage");
    const auto& facade = interpreter.process_program(0U);
    const auto write_operation = facade.operations.expanded(2U);
    require(facade.operations.size() == 4U
            && operation_get_if<WriteUpdate>(&write_operation) != nullptr
            && OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U),
        "public process inspection lazily reconstructs the exact body while retaining bank data");

    interpreter.release_signal(target);
    require(interpreter.signal_value(target) == value,
        "releasing the pre-run force exposes the committed startup value");
}

void test_constant_driver_startup_bank()
{
    Interpreter interpreter;
    const auto target = interpreter.add_signal(Signal {
        "constant_target",
        PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire,
    });
    const auto observed = interpreter.add_signal(Signal {
        "observed_target", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire,
    });
    const auto value = PackedLogic4::from_msb_string("x0z1x01z");
    require(interpreter.add_process(constant_driver_process(
                0U, target, value, true)) == 0U,
        "eligible constant writer retains its original process identity");

    Process waiter;
    waiter.id = 1U;
    waiter.name = "constant_target_waiter";
    waiter.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    waiter.register_count = 1U;
    waiter.register_value_kinds = { ValueKind::logic4 };
    waiter.static_sensitivity = { { target, EdgeKind::any } };
    waiter.driver_regions = { { observed, 0U, 0U, true } };
    waiter.operations = {
        ReadSignal { 0U, target },
        WriteUpdate { observed, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(waiter)) == 1U,
        "change observer has a stable independent process identity");

    interpreter.start();
    require(OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::compact_count(interpreter) == 1U,
        "the exact constant whole write uses a bodyless compact process record");
    const auto [full_payload_bytes, compact_payload_bytes]
        = OwnedDriverDemotionTestAccess::per_process_payload_bytes();
    require(OwnedDriverDemotionTestAccess::full_state_count(interpreter) == 1U
            && full_payload_bytes > compact_payload_bytes,
        "compact admission avoids a full execution record and its cold sidecar");
    require(OwnedDriverDemotionTestAccess::program_operation_count(
                interpreter, 0U) == 4U
            && !OwnedDriverDemotionTestAccess::public_facade_materialized(
                interpreter, 0U),
        "operation metadata remains exact without materializing its public facade");
    require(!OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "startup preparation does not allocate its source register frame");
    bool rejected_debug_local { };
    try {
        static_cast<void>(interpreter.read_debug_local(0U, 0U));
    } catch (const std::out_of_range&) {
        rejected_debug_local = true;
    }
    require(rejected_debug_local
            && OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U),
        "a read-only miss on an absent debug local does not promote compact state");

    ExecutionPointCapture points;
    interpreter.set_execution_point_hook(
        [&](Scheduler& scheduler, const ExecutionPoint& point) {
            if (point.process == 0U
                && point.kind == ExecutionPointKind::process_suspend) {
                static_cast<void>(interpreter.process_program(point.process));
                require(OwnedDriverDemotionTestAccess::startup_banked(
                            interpreter, point.process),
                    "reentrant public program inspection keeps execution compact");
            }
            points.receive(scheduler, point);
        });
    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);

    const auto stopped = interpreter.run();
    require(stopped.status == RunStatus::stopped
            && interpreter.process_instruction(0U) == 1U
            && interpreter.signal_value(target)
                == PackedLogic4 { 8U, Logic4::z }
            && interpreter.driver_value(0U, target)
                == PackedLogic4 { 8U, Logic4::z }
            && trace.count == 0U
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "ProcessEntry stop preserves the ordinary resume point before write");

    interpreter.scheduler().clear_stop();
    const auto completed = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(completed.status == RunStatus::completed
            && interpreter.process_instruction(0U) == 4U
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "resume reaches the original Halt without materializing a frame");
    require(points.count == 2U && !points.overflow
            && points.entries[0].process == 0U
            && points.entries[0].instruction == 0U
            && points.entries[0].kind == ExecutionPointKind::process_entry
            && points.entries[0].time == 0U
            && points.entries[1].process == 0U
            && points.entries[1].instruction == 3U
            && points.entries[1].kind == ExecutionPointKind::process_suspend
            && points.entries[1].time == 0U,
        "compact execution preserves entry and suspension source boundaries");
    require(interpreter.signal_value(target) == value
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value
            && interpreter.signal_value(observed) == value,
        "constant publication retains raw driver, stored, current, and waiter values");
    require(trace.count == 2U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[1].kind == SchedulerTraceKind::signal_change
            && trace.entries[0].time == 0U && trace.entries[1].time == 0U
            && trace.entries[0].systemverilog
            && trace.entries[1].systemverilog
            && trace.entries[0].phase == SchedulerPhase::active
            && trace.entries[1].phase == SchedulerPhase::active,
        "startup value publishes as a time-zero SystemVerilog Active update");

    const auto forced = PackedLogic4::from_msb_string("10100101");
    interpreter.force_signal(target, forced);
    require(interpreter.signal_value(target) == forced
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value,
        "force overlays the compact writer without replacing its raw value");
    require(interpreter.run().status == RunStatus::completed
            && interpreter.signal_value(observed) == forced,
        "the ordinary sensitivity reader observes a later force update");
    interpreter.release_signal(target);
    require(interpreter.signal_value(target) == value
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value,
        "release exposes the original compact driver value");
    require(interpreter.run().status == RunStatus::completed
            && interpreter.signal_value(observed) == value,
        "the ordinary sensitivity reader observes release of the force");

    const auto* const first_public
        = std::addressof(interpreter.process_program(0U));
    const auto* const second_public
        = std::addressof(interpreter.process_program(0U));
    require(first_public == second_public
            && first_public->id == 0U
            && first_public->name == "constant_driver_0"
            && OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "late public inspection materializes one stable facade without promoting execution state");
}

void test_shared_constant_driver_startup_uses_instance_signal()
{
    Interpreter interpreter;
    const PackedLogic4 initial { 8U, Logic4::z };
    const auto first_signal = interpreter.add_signal(Signal {
        "shared_constant_first", initial, ResolutionKind::sv_wire
    });
    const auto sibling_signal = interpreter.add_signal(Signal {
        "shared_constant_sibling", initial, ResolutionKind::sv_wire
    });
    const std::array<Signal, 2U> sharing_signals {
        Signal { "shared_constant_first", initial, ResolutionKind::sv_wire },
        Signal { "shared_constant_sibling", initial, ResolutionKind::sv_wire },
    };
    const auto value = PackedLogic4::from_msb_string("x0z1x01z");
    auto representative
        = constant_driver_process(0U, first_signal, value, true);
    auto sibling
        = constant_driver_process(1U, sibling_signal, value, true);
    const SourceLocation sibling_entry_source { "shared.sv", 19U, 2U };
    auto entry_operation = sibling.operations.expanded(0U);
    auto* const entry_point
        = operation_get_if<DebugPoint>(&entry_operation);
    require(entry_point != nullptr,
        "the sibling process has its own entry point before sharing");
    entry_point->source = sibling_entry_source;
    sibling.operations.replace(0U, std::move(entry_operation));

    require(share_process_operations(
                representative, sibling, sharing_signals),
        "constant-driver processes can share an equal Halt and remap outputs");
    require(representative.operations.shares_body_with(sibling.operations),
        "the sibling constant driver shares the representative operation body");

    const auto set_program_exit = [](Process& process, const bool value) {
        auto operation = process.operations.expanded(3U);
        auto* const halt = operation_get_if<Halt>(&operation);
        require(halt != nullptr,
            "the sharing cases end in the original Halt operation");
        halt->program_exit = value;
        process.operations.replace(3U, std::move(operation));
    };

    auto exit_representative
        = constant_driver_process(2U, first_signal, value, true);
    auto exit_sibling
        = constant_driver_process(3U, sibling_signal, value, true);
    set_program_exit(exit_representative, true);
    set_program_exit(exit_sibling, true);
    require(share_process_operations(
                exit_representative, exit_sibling, sharing_signals)
            && exit_representative.operations.shares_body_with(
                exit_sibling.operations),
        "matching program-exit behavior remains shareable");

    auto mismatched_exit
        = constant_driver_process(4U, sibling_signal, value, true);
    set_program_exit(mismatched_exit, true);
    require(!share_process_operations(
                representative, mismatched_exit, sharing_signals),
        "different program-exit behavior keeps process bodies separate");

    require(interpreter.add_process(std::move(representative)) == 0U,
        "the representative preserves its registered process identity");
    require(interpreter.add_process(std::move(sibling)) == 1U,
        "the sibling preserves its registered process identity");
    interpreter.start();

    require(OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && OwnedDriverDemotionTestAccess::startup_banked(interpreter, 1U),
        "both instances pass compact-state recognition through the registered view");
    require(OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 1U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 1U),
        "each exact constant instance drops its operation body after registration");
    require(OwnedDriverDemotionTestAccess::full_state_count(interpreter) == 0U
            && OwnedDriverDemotionTestAccess::compact_count(interpreter) == 2U,
        "shared compact instances allocate no ProcessState records");
    require(OwnedDriverDemotionTestAccess::process_bodies_shared(
                interpreter, 0U, 1U),
        "the registered process views retain a shared operation body");
    require(OwnedDriverDemotionTestAccess::startup_signal(interpreter, 0U)
                == first_signal
            && OwnedDriverDemotionTestAccess::startup_signal(interpreter, 1U)
                == sibling_signal,
        "the startup entries keep each instance's distinct output signal id");
    require(OwnedDriverDemotionTestAccess::startup_value(interpreter, 0U)
                == value
            && OwnedDriverDemotionTestAccess::startup_value(interpreter, 1U)
                == value,
        "shared instances preserve the exact constant payload");
    const auto sibling_entry
        = OwnedDriverDemotionTestAccess::process_entry_debug_point(
            interpreter, 1U);
    require(sibling_entry
            && sibling_entry->kind == DebugPointKind::process_entry
            && sibling_entry->source == sibling_entry_source,
        "expanded instance metadata retains the sibling's process-entry point");

    require(interpreter.run().status == RunStatus::completed
            && interpreter.signal_value(first_signal) == value
            && interpreter.signal_value(sibling_signal) == value,
        "each banked instance publishes its constant on its own remapped signal");
}

void test_nonmatching_constant_process_uses_interpreter()
{
    Interpreter interpreter;
    const auto target = interpreter.add_signal(Signal {
        "near_miss_target", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire,
    });
    const auto value = PackedLogic4::from_msb_string("1xz0");
    static_cast<void>(interpreter.add_process(constant_driver_process(
        0U, target, value, false)));

    interpreter.start();
    require(!OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U),
        "an intervening register operation remains outside the exact bank shape");
    require(interpreter.run().status == RunStatus::completed
            && OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U)
            && interpreter.signal_value(target) == value
            && interpreter.driver_value(0U, target) == value,
        "near-match executes with the ordinary frame and original driver semantics");
}

void test_two_register_constant_copy_startup_bank_stop_resume()
{
    Interpreter interpreter;
    const auto initial = PackedLogic4 { 8U, Logic4::z };
    const auto target = interpreter.add_signal(Signal {
        "two_register_constant_target", initial, ResolutionKind::sv_wire
    });
    const auto value = PackedLogic4::from_msb_string("10100101");
    require(interpreter.add_process(
                two_register_constant_driver_process(0U, target, value))
            == 0U,
        "the two-register constant copy keeps its original process identity");

    interpreter.start();
    const auto banked_copy
        = OwnedDriverDemotionTestAccess::startup_constant_copy(
            interpreter, 0U);
    require(OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && banked_copy && banked_copy->destination == 1U
            && banked_copy->source == 0U
            && OwnedDriverDemotionTestAccess::program_operation_count(
                interpreter, 0U) == 6U
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "the exact known two-register copy enters the data-only startup bank");
    require_two_register_constant_copy_program(
        interpreter.process_program(0U), 0U, target, value, false);
    require(OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "public inspection reconstructs the exact copied source body without promoting it");

    ExecutionPointCapture points;
    points.stop_at_statement = true;
    interpreter.set_execution_point_hook(
        [&](Scheduler& scheduler, const ExecutionPoint& point) {
            points.receive(scheduler, point);
        });
    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);

    const auto entry_stop = interpreter.run();
    require(entry_stop.status == RunStatus::stopped
            && interpreter.process_instruction(0U) == 1U
            && interpreter.signal_value(target) == initial
            && interpreter.stored_signal_value(target) == initial
            && interpreter.driver_value(0U, target) == initial
            && trace.count == 0U
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "stopping at entry leaves the copied constant unpublished and resumable");
    require_two_register_constant_copy_program(
        interpreter.process_program(0U), 0U, target, value, false);

    interpreter.scheduler().clear_stop();
    const auto statement_stop = interpreter.run();
    require(statement_stop.status == RunStatus::stopped
            && interpreter.process_instruction(0U) == 2U
            && interpreter.signal_value(target) == initial
            && interpreter.stored_signal_value(target) == initial
            && interpreter.driver_value(0U, target) == initial
            && trace.count == 0U
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "resuming to the statement point still leaves the copied write pending");
    require_two_register_constant_copy_program(
        interpreter.process_program(0U), 0U, target, value, false);
    require(points.count == 2U && !points.overflow
            && points.entries[0].kind == ExecutionPointKind::process_entry
            && points.entries[0].instruction == 0U
            && points.entries[0].time == 0U
            && points.entries[1].kind == ExecutionPointKind::statement
            && points.entries[1].instruction == 1U
            && points.entries[1].time == 0U,
        "entry and statement stops retain their exact original program counters");

    interpreter.scheduler().clear_stop();
    const auto completed = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(completed.status == RunStatus::completed
            && interpreter.process_instruction(0U) == 6U
            && interpreter.signal_value(target) == value
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target) == initial
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "the final resume publishes once through the original compact process");
    require(points.count == 3U && !points.overflow
            && points.entries[2].kind == ExecutionPointKind::process_suspend
            && points.entries[2].instruction == 5U
            && points.entries[2].time == 0U
            && trace.count == 2U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[1].kind == SchedulerTraceKind::signal_change
            && trace.entries[0].time == 0U && trace.entries[1].time == 0U
            && trace.entries[0].systemverilog
            && trace.entries[1].systemverilog
            && trace.entries[0].phase == SchedulerPhase::active
            && trace.entries[1].phase == SchedulerPhase::active,
        "the resumed copy emits exactly one time-zero Active transaction and change");
    require_two_register_constant_copy_program(
        interpreter.process_program(0U), 0U, target, value, false);
}

void test_two_register_constant_copy_with_coverage_uses_interpreter()
{
    Interpreter interpreter;
    const auto initial = PackedLogic4 { 8U, Logic4::z };
    const auto target = interpreter.add_signal(Signal {
        "covered_two_register_constant_target", initial,
        ResolutionKind::sv_wire
    });
    const auto value = PackedLogic4::from_msb_string("01011010");
    interpreter.set_code_coverage_counters({ 0U });
    require(interpreter.add_process(two_register_constant_driver_process(
                0U, target, value, true)) == 0U,
        "the coverage-bearing copy keeps its original process identity");
    interpreter.start();
    require(!OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::program_operation_count(
                interpreter, 0U) == 7U,
        "an added coverage operation declines the exact compact shape");
    require_two_register_constant_copy_program(
        interpreter.process_program(0U), 0U, target, value, true);

    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);
    require(interpreter.run().status == RunStatus::completed,
        "the coverage-bearing copy completes through checked execution");
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    const auto counters = interpreter.code_coverage_counters();
    require(counters.size() == 1U && counters[0] == 1U
            && OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_banked(
                interpreter, 0U)
            && interpreter.signal_value(target) == value
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value
            && trace.count == 2U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[1].kind == SchedulerTraceKind::signal_change,
        "checked fallback executes its detailed coverage hit and publishes once");
}

void test_two_register_xz_constant_copy_uses_interpreter()
{
    Interpreter interpreter;
    const auto initial = PackedLogic4 { 6U, Logic4::z };
    const auto target = interpreter.add_signal(Signal {
        "xz_two_register_constant_target", initial,
        ResolutionKind::sv_wire
    });
    const auto value = PackedLogic4::from_msb_string("10xz01");
    require(interpreter.add_process(
                two_register_constant_driver_process(0U, target, value))
            == 0U,
        "the X/Z copy keeps its original process identity");
    interpreter.start();
    require(!OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::program_operation_count(
                interpreter, 0U) == 6U,
        "a two-register payload containing X/Z stays on checked execution");
    require_two_register_constant_copy_program(
        interpreter.process_program(0U), 0U, target, value, false);

    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);
    require(interpreter.run().status == RunStatus::completed,
        "the X/Z copy completes through checked execution");
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U)
            && interpreter.signal_value(target) == value
            && interpreter.stored_signal_value(target) == value
            && interpreter.driver_value(0U, target) == value
            && trace.count == 2U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[1].kind == SchedulerTraceKind::signal_change,
        "checked X/Z copy preserves four-state value and one publication");
}

void test_constant_driver_startup_bank_slice()
{
    Interpreter interpreter;
    const auto target = interpreter.add_signal(Signal {
        "constant_slice_target", PackedLogic4 { 8U, Logic4::z },
        ResolutionKind::sv_wire,
    });
    const auto value = PackedLogic4::from_msb_string("101");
    Process process;
    process.id = 0U;
    process.name = "constant_slice_driver";
    process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    process.driver_regions = { { target, 2U, 3U, false } };
    process.operations = {
        DebugPoint { DebugPointKind::process_entry, { } },
        LoadConstant { 0U, value },
        WriteUpdateSlice {
            target, 0U, 2U, SignalUpdateDomain::systemverilog_active
        },
        Halt { },
    };
    static_cast<void>(interpreter.add_process(std::move(process)));

    interpreter.start();
    require(OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "an exact constant slice enters the data-only startup bank without a frame");
    StartupTransactionOrderCapture trace;
    interpreter.scheduler().set_trace_hook(
        &trace, &StartupTransactionOrderCapture::receive);
    require(interpreter.run().status == RunStatus::completed,
        "constant slice startup finishes through the original process ID");
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    const auto expected = PackedLogic4::from_msb_string("ZZZ101ZZ");
    require(interpreter.signal_value(target) == expected
            && interpreter.stored_signal_value(target) == expected
            && interpreter.driver_value(0U, target) == expected
            && trace.count == 1U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[0].signal == target
            && trace.entries[0].time == 0U
            && trace.entries[0].phase == SchedulerPhase::active
            && trace.entries[0].systemverilog
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "slice bank preserves its original Active transaction, driver and untouched high-Z bits");
    require(!OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "compact slice execution does not reconstruct its operation body");
    const auto& public_process = interpreter.process_program(0U);
    auto write_operation = public_process.operations.expanded(2U);
    const auto* const write = operation_get_if<WriteUpdateSlice>(
        &write_operation);
    require(write != nullptr && write->signal == target
            && write->source == 0U && write->offset == 2U
            && write->domain == SignalUpdateDomain::systemverilog_active
            && OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U),
        "public inspection lazily reconstructs the exact original slice program");
}

void test_constant_driver_startup_bank_slice_equal_value_transaction()
{
    Interpreter interpreter;
    const auto expected = PackedLogic4 { 8U, Logic4::z };
    const auto target = interpreter.add_signal(Signal {
        "constant_equal_slice_target", expected, ResolutionKind::sv_wire,
    });
    static_cast<void>(interpreter.add_process(
        constant_driver_slice_process(0U, target, 2U,
            PackedLogic4 { 3U, Logic4::z })));

    interpreter.start();
    require(interpreter.signal_value(target) == expected
            && interpreter.stored_signal_value(target) == expected
            && interpreter.driver_value(0U, target) == expected
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target) == expected
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "the equal slice has matching Z-valued current, LAST, stored and driver roles before execution");
    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);

    const auto result = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(result.status == RunStatus::completed
            && interpreter.signal_value(target) == expected
            && interpreter.stored_signal_value(target) == expected
            && interpreter.driver_value(0U, target) == expected
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target) == expected,
        "an equal slice preserves current, LAST, stored and driver roles");
    require(trace.count == 1U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[0].time == 0U
            && trace.entries[0].systemverilog
            && trace.entries[0].phase == SchedulerPhase::active,
        "an equal slice still records its time-zero Active transaction without a value-change event");
    require(!OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U),
        "equal slice execution does not reconstruct its operation body");
}

void test_shared_constant_slice_startup_uses_instance_signal()
{
    Interpreter interpreter;
    const PackedLogic4 initial { 8U, Logic4::z };
    const auto first_signal = interpreter.add_signal(Signal {
        "shared_constant_slice_first", initial, ResolutionKind::sv_wire
    });
    const auto sibling_signal = interpreter.add_signal(Signal {
        "shared_constant_slice_sibling", initial, ResolutionKind::sv_wire
    });
    const std::array<Signal, 2U> sharing_signals {
        Signal { "shared_constant_slice_first", initial,
            ResolutionKind::sv_wire },
        Signal { "shared_constant_slice_sibling", initial,
            ResolutionKind::sv_wire },
    };
    const auto value = PackedLogic4::from_msb_string("101");
    auto representative
        = constant_driver_slice_process(0U, first_signal, 2U, value);
    auto sibling
        = constant_driver_slice_process(1U, sibling_signal, 2U, value);
    require(share_process_operations(
                representative, sibling, sharing_signals)
            && representative.operations.shares_body_with(sibling.operations),
        "constant slice writers share one operation body with per-instance signal bindings");
    require(interpreter.add_process(std::move(representative)) == 0U
            && interpreter.add_process(std::move(sibling)) == 1U,
        "shared slice writers retain their original process IDs");

    interpreter.start();
    require(OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 1U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 1U),
        "shared slice instances release their per-instance operation lists before execution");

    StartupTransactionOrderCapture trace;
    interpreter.scheduler().set_trace_hook(
        &trace, &StartupTransactionOrderCapture::receive);
    require(interpreter.run().status == RunStatus::completed,
        "both compact slice instances complete");
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    const auto expected = PackedLogic4::from_msb_string("ZZZ101ZZ");
    require(interpreter.signal_value(first_signal) == expected
            && interpreter.signal_value(sibling_signal) == expected
            && interpreter.driver_value(0U, first_signal) == expected
            && interpreter.driver_value(1U, sibling_signal) == expected
            && trace.count == 2U && !trace.overflow
            && trace.entries[0].signal == first_signal
            && trace.entries[1].signal == sibling_signal
            && trace.entries[0].time == 0U
            && trace.entries[1].time == 0U
            && trace.entries[0].systemverilog
            && trace.entries[1].systemverilog
            && trace.entries[0].phase == SchedulerPhase::active
            && trace.entries[1].phase == SchedulerPhase::active,
        "each instance publishes its own exact slice driver in process order");
    require(!OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 1U),
        "shared slice execution remains bodyless until process inspection");

    const auto& first_public = interpreter.process_program(0U);
    const auto& sibling_public = interpreter.process_program(1U);
    auto first_write_operation = first_public.operations.expanded(2U);
    auto sibling_write_operation = sibling_public.operations.expanded(2U);
    const auto* const first_write
        = operation_get_if<WriteUpdateSlice>(&first_write_operation);
    const auto* const sibling_write
        = operation_get_if<WriteUpdateSlice>(&sibling_write_operation);
    require(first_write != nullptr && sibling_write != nullptr
            && first_write->signal == first_signal
            && sibling_write->signal == sibling_signal
            && first_write->offset == 2U && sibling_write->offset == 2U
            && first_public.operations.shares_body_with(
                sibling_public.operations),
        "lazy public materialization preserves the shared body and each instance's signal remap");
}

void test_compact_constant_promotion_preserves_program_view()
{
    Interpreter interpreter;
    const auto target = interpreter.add_signal(Signal {
        "promoted_constant_target", PackedLogic4 { 5U, Logic4::z },
        ResolutionKind::sv_wire,
    });
    const auto value = PackedLogic4::from_msb_string("1x0z1");
    static_cast<void>(interpreter.add_process(constant_driver_process(
        0U, target, value, true)));
    const auto second_target = interpreter.add_signal(Signal {
        "promoted_constant_sibling_target", PackedLogic4 { 5U, Logic4::z },
        ResolutionKind::sv_wire,
    });
    const auto second_value = PackedLogic4::from_msb_string("001xz");
    static_cast<void>(interpreter.add_process(constant_driver_process(
        1U, second_target, second_value, true)));
    const auto third_target = interpreter.add_signal(Signal {
        "ordinary_state_sibling_target", PackedLogic4 { 5U, Logic4::z },
        ResolutionKind::sv_wire,
    });
    const auto third_value = PackedLogic4::from_msb_string("z10x1");
    static_cast<void>(interpreter.add_process(constant_driver_process(
        2U, third_target, third_value, false)));
    const auto view = InterpreterProgramAccess::view(interpreter, 0U);
    const auto* const facade
        = std::addressof(interpreter.process_program(0U));
    const auto* const storage
        = OwnedDriverDemotionTestAccess::program_storage_identity(
            interpreter, 0U);
    require(storage != nullptr
            && OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U),
        "the pre-promotion process begins in compact storage");
    const auto* const descriptor
        = OwnedDriverDemotionTestAccess::compact_descriptor_identity(
            interpreter, 0U);
    require(descriptor != nullptr
            && OwnedDriverDemotionTestAccess::compact_count(interpreter) == 2U,
        "both original processes reside in compact descriptors before promotion");

    interpreter.start();
    const auto* const stable_ordinary_state
        = OwnedDriverDemotionTestAccess::full_state_identity(interpreter, 2U);
    require(stable_ordinary_state != nullptr
            && OwnedDriverDemotionTestAccess::full_state_count(interpreter) == 1U,
        "start freezes the initial full-state slab before cohort and scheduler bindings");
    OwnedDriverDemotionTestAccess::promote_for_test(interpreter, 0U);
    require(!OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && OwnedDriverDemotionTestAccess::compact_count(interpreter) == 1U
            && OwnedDriverDemotionTestAccess::full_state_count(interpreter) == 2U
            && OwnedDriverDemotionTestAccess::full_state_identity(
                   interpreter, 2U) == stable_ordinary_state
            && std::addressof(interpreter.process_program(0U)) == facade
            && facade->id == 0U
            && facade->name == "constant_driver_0"
            && OwnedDriverDemotionTestAccess::program_storage_identity(
                   interpreter, 0U) == storage
            && OwnedDriverDemotionTestAccess::compact_descriptor_identity(
                   interpreter, 0U) == descriptor
            && OwnedDriverDemotionTestAccess::compact_descriptor_is_tombstone(
                   interpreter, 0U)
            && view.id() == 0U
            && view.operations().size() == 4U,
        "explicit full-state promotion preserves the borrowed program record address");
    OwnedDriverDemotionTestAccess::seed_compact_profile_counters(
        interpreter, 1U);
    OwnedDriverDemotionTestAccess::promote_for_test(interpreter, 1U);
    require(OwnedDriverDemotionTestAccess::profile_counters(
                interpreter, 1U)
            && OwnedDriverDemotionTestAccess::compact_count(interpreter) == 0U
            && OwnedDriverDemotionTestAccess::full_state_count(interpreter) == 3U
            && OwnedDriverDemotionTestAccess::full_state_identity(
                   interpreter, 2U) == stable_ordinary_state,
        "promotion transfers compact profiling state into the full sidecar");
    require(interpreter.run().status == RunStatus::completed
            && interpreter.signal_value(target) == value
            && interpreter.driver_value(0U, target) == value
            && interpreter.signal_value(second_target) == second_value
            && interpreter.driver_value(1U, second_target) == second_value
            && interpreter.signal_value(third_target) == third_value
            && interpreter.driver_value(2U, third_target) == third_value,
        "promoted and initial full records retain their publication semantics");
}

void test_constant_startup_fact_is_bound_to_region_input()
{
    Interpreter interpreter;
    const auto source = interpreter.add_signal(Signal {
        "region_constant_source", PackedLogic4 { 4U, Logic4::z },
        ResolutionKind::sv_wire,
    });
    const auto internal = interpreter.add_signal(Signal {
        "region_constant_internal", PackedLogic4 { 4U, Logic4::x },
        ResolutionKind::sv_wire,
    });
    const auto producer_output = interpreter.add_signal(Signal {
        "region_constant_producer_output", PackedLogic4 { 4U, Logic4::x },
        ResolutionKind::sv_wire,
    });
    const auto consumer_output = interpreter.add_signal(Signal {
        "region_constant_consumer_output", PackedLogic4 { 4U, Logic4::x },
        ResolutionKind::sv_wire,
    });

    Process producer;
    producer.id = 0U;
    producer.name = "region_constant_producer";
    producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    producer.register_count = 1U;
    producer.register_value_kinds = { ValueKind::logic4 };
    producer.static_sensitivity = { { source, EdgeKind::any } };
    producer.driver_regions = {
        { internal, 0U, 0U, true },
        { producer_output, 0U, 0U, true },
    };
    producer.operations = {
        ReadSignal { 0U, source },
        WriteUpdate { internal, 0U,
            SignalUpdateDomain::systemverilog_active },
        LoadConstant { 0U, PackedLogic4::from_msb_string("1010") },
        WriteUpdate { producer_output, 0U,
            SignalUpdateDomain::systemverilog_active },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(producer)) == 0U,
        "the first member reads the external startup signal");

    Process consumer;
    consumer.id = 1U;
    consumer.name = "region_constant_consumer";
    consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    consumer.register_count = 2U;
    consumer.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4,
    };
    consumer.static_sensitivity = { { internal, EdgeKind::any } };
    consumer.driver_regions = { { consumer_output, 0U, 0U, true } };
    consumer.operations = {
        ReadSignal { 0U, internal },
        UnaryNot { 1U, 0U },
        WriteUpdate { consumer_output, 1U,
            SignalUpdateDomain::systemverilog_active },
        LoadConstant { 1U, PackedLogic4::from_msb_string("0011") },
        WaitSensitivity { },
        Jump { 0U },
    };
    require(interpreter.add_process(std::move(consumer)) == 1U,
        "the second member closes a real internal signal component");
    const auto value = PackedLogic4::from_msb_string("1001");
    require(interpreter.add_process(constant_driver_process(
                2U, source, value, true)) == 2U,
        "the startup owner remains a distinct original process");

    const auto inputs
        = OwnedDriverDemotionTestAccess::region_constant_inputs(interpreter);
    require(OwnedDriverDemotionTestAccess::data_only_startup_write(
                interpreter, 2U)
            && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                interpreter, 2U)
            && inputs.size() == 1U
            && inputs.front().owner == 2U
            && inputs.front().signal == source
            && inputs.front().offset == 0U
            && inputs.front().width == 4U
            && inputs.front().value_kind == ValueKind::logic4
            && inputs.front().domain
                == SignalUpdateDomain::systemverilog_active
            && inputs.front().value == value,
        "the exact banked startup writer certifies the matching region boundary input");

    OwnedDriverDemotionTestAccess::force_signal(
        interpreter, source, PackedLogic4 { 4U, Logic4::one });
    require(OwnedDriverDemotionTestAccess::region_constant_inputs(interpreter)
                .empty(),
        "a currently forced boundary is excluded from startup specialization");
    OwnedDriverDemotionTestAccess::release_signal(interpreter, source, 4U);
    OwnedDriverDemotionTestAccess::force_driver(
        interpreter, 2U, source, PackedLogic4 { 4U, Logic4::zero });
    require(OwnedDriverDemotionTestAccess::region_constant_inputs(interpreter)
                .empty(),
        "a currently force-masked driver is excluded from specialization");
    OwnedDriverDemotionTestAccess::release_driver(
        interpreter, 2U, source, 4U);
    const auto released_inputs
        = OwnedDriverDemotionTestAccess::region_constant_inputs(interpreter);
    require(released_inputs.size() == 1U
            && released_inputs.front().value == value,
        "release restores the exact eligible startup fact for a fresh snapshot");
}

void test_logic9_constant_startup_and_region_fact()
{
    constexpr std::array widths {
        std::size_t { 9U }, std::size_t { 65U },
        std::size_t { 129U }, std::size_t { 256U },
        std::size_t { 1024U },
    };
    for (const auto width : widths) {
        Interpreter interpreter;
        const auto initial = PackedLogic4::from_logic9_msb_string(
            std::string(width, 'U'));
        const auto value = logic9_cycle(width);
        const auto source = interpreter.add_signal(Signal {
            "logic9_constant_source_" + std::to_string(width), initial,
            ResolutionKind::sv_wire, ValueKind::logic9,
        });
        const auto internal = interpreter.add_signal(Signal {
            "logic9_constant_internal_" + std::to_string(width), initial,
            ResolutionKind::sv_wire, ValueKind::logic9,
        });
        const auto output = interpreter.add_signal(Signal {
            "logic9_constant_output_" + std::to_string(width), initial,
            ResolutionKind::sv_wire, ValueKind::logic9,
        });

        Process producer;
        producer.id = 0U;
        producer.name = "logic9_constant_producer";
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.register_count = 1U;
        producer.register_value_kinds = { ValueKind::logic9 };
        producer.static_sensitivity = { { source, EdgeKind::any } };
        producer.driver_regions = { { internal, 0U, 0U, true } };
        producer.operations = {
            ReadSignal { 0U, source },
            WriteUpdate { internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { },
            Jump { 0U },
        };
        require(interpreter.add_process(std::move(producer)) == 0U,
            "Logic9 producer keeps its original process identity");

        Process consumer;
        consumer.id = 1U;
        consumer.name = "logic9_constant_consumer";
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.register_count = 1U;
        consumer.register_value_kinds = { ValueKind::logic9 };
        consumer.static_sensitivity = { { internal, EdgeKind::any } };
        consumer.driver_regions = { { output, 0U, 0U, true } };
        consumer.operations = {
            ReadSignal { 0U, internal },
            WriteUpdate { output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { },
            Jump { 0U },
        };
        require(interpreter.add_process(std::move(consumer)) == 1U,
            "Logic9 consumer remains an ordinary graph member");
        require(interpreter.add_process(logic9_constant_driver_process(
                    2U, source, value)) == 2U,
            "the Logic9 constant owner remains a distinct process");

        const auto candidate_inputs
            = OwnedDriverDemotionTestAccess::region_constant_inputs(
                interpreter);
        require(OwnedDriverDemotionTestAccess::startup_banked(interpreter, 2U)
                && OwnedDriverDemotionTestAccess::data_only_startup_write(
                    interpreter, 2U)
                && !OwnedDriverDemotionTestAccess::startup_write_body_materialized(
                    interpreter, 2U)
                && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                    interpreter, 2U)
                && candidate_inputs.size() == 1U
                && candidate_inputs.front().owner == 2U
                && candidate_inputs.front().signal == source
                && candidate_inputs.front().width == width
                && candidate_inputs.front().value_kind == ValueKind::logic9
                && candidate_inputs.front().value == value,
            "the graph snapshot preserves the whole exact Logic9 startup constant");

        SignalTraceCapture trace;
        trace.watched_signal = source;
        interpreter.scheduler().set_trace_hook(
            &trace, &SignalTraceCapture::receive);
        interpreter.start();
        require(interpreter.run().status == RunStatus::completed,
            "Logic9 constant, producer, and consumer finish startup publication");
        interpreter.scheduler().set_trace_hook(nullptr, nullptr);
        require(trace.count == 2U && !trace.overflow
                && trace.entries[0].kind
                    == SchedulerTraceKind::signal_transaction
                && trace.entries[1].kind == SchedulerTraceKind::signal_change
                && trace.entries[0].time == 0U && trace.entries[1].time == 0U
                && trace.entries[0].phase == SchedulerPhase::active
                && trace.entries[1].phase == SchedulerPhase::active
                && trace.entries[0].systemverilog
                && trace.entries[1].systemverilog,
            "the Logic9 owner publishes its time-zero Active transaction and change");
        require(OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, source) == value
                && OwnedDriverDemotionTestAccess::last_signal_value(
                    interpreter, source) == initial
                && OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, internal) == value
                && OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, output) == value
                && interpreter.driver_value(2U, source) == value
                && interpreter.driver_value(0U, internal) == value
                && interpreter.driver_value(1U, output) == value,
            "all nine Logic9 states survive owner, producer, and consumer writes");

        const auto forced = logic9_cycle(width, 4U);
        OwnedDriverDemotionTestAccess::force_signal(
            interpreter, source, forced);
        require(OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter).empty(),
            "a forced Logic9 boundary declines the startup specialization");
        OwnedDriverDemotionTestAccess::release_signal(
            interpreter, source, width);
        require(OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter) == candidate_inputs,
            "release restores the exact Logic9 startup fact for recertification");

        const auto forced_driver = logic9_cycle(width, 2U);
        OwnedDriverDemotionTestAccess::force_driver(
            interpreter, 2U, source, forced_driver);
        require(OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter).empty(),
            "a force-masked Logic9 driver declines specialization");
        OwnedDriverDemotionTestAccess::release_driver(
            interpreter, 2U, source, width);
        require(OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter) == candidate_inputs,
            "driver release makes a fresh whole Logic9 fact eligible again");

        const auto public_value = interpreter.signal_value(source);
        require(public_value == value
                && OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter).empty(),
            "late public value exposure demotes the rebuilt Logic9 candidate");
        interpreter.deposit_signal(source, forced);
        require(OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter).empty(),
            "a Logic9 deposit remains outside the guarded startup specialization");
    }
}

void test_vhdl_projected_logic9_constant_startup_and_region_fact()
{
    constexpr std::array widths {
        std::size_t { 9U }, std::size_t { 65U },
        std::size_t { 129U }, std::size_t { 256U },
        std::size_t { 1024U },
    };
    for (std::size_t width_index = 0U;
         width_index < widths.size(); ++width_index) {
        const auto width = widths[width_index];
        Interpreter interpreter;
        const auto initial = PackedLogic4::from_logic9_msb_string(
            std::string(width, 'U'));
        const auto value = logic9_cycle(width, width_index);
        const auto source = interpreter.add_signal(Signal {
            "vhdl_projected_constant_source_" + std::to_string(width),
            initial, ResolutionKind::std_logic, ValueKind::logic9,
        });
        const auto output = interpreter.add_signal(Signal {
            "vhdl_projected_constant_consumer_" + std::to_string(width),
            initial, ResolutionKind::std_logic, ValueKind::logic9,
        });
        const auto internal = interpreter.add_signal(Signal {
            "vhdl_projected_constant_internal_" + std::to_string(width),
            initial, ResolutionKind::sv_wire, ValueKind::logic9,
        });

        const auto delay = static_cast<SimulationTick>(
            3U + width_index);
        const auto mode = width_index == 1U
            ? ProjectedDelayMode::transport
            : ProjectedDelayMode::inertial;
        const auto rejection = mode == ProjectedDelayMode::transport
            ? delay + 2U : delay - 1U;
        require(interpreter.add_process(
                    vhdl_projected_logic9_constant_driver_process(
                        0U, source, value, delay, rejection, mode)) == 0U,
            "the VHDL projected owner retains its original process identity");

        Process producer;
        producer.id = 1U;
        producer.name = "vhdl_projected_constant_producer";
        producer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        producer.register_count = 1U;
        producer.register_value_kinds = { ValueKind::logic9 };
        producer.static_sensitivity = { { source, EdgeKind::any } };
        producer.driver_regions = { { internal, 0U, 0U, true } };
        producer.operations = {
            ReadSignal { 0U, source },
            WriteUpdate { internal, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { },
            Jump { 0U },
        };
        require(interpreter.add_process(std::move(producer)) == 1U,
            "the projected boundary feeds a distinct region producer");

        Process consumer;
        consumer.id = 2U;
        consumer.name = "vhdl_projected_constant_consumer";
        consumer.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        consumer.register_count = 1U;
        consumer.register_value_kinds = { ValueKind::logic9 };
        consumer.static_sensitivity = { { internal, EdgeKind::any } };
        consumer.driver_regions = { { output, 0U, 0U, true } };
        consumer.operations = {
            ReadSignal { 0U, internal },
            WriteUpdate { output, 0U,
                SignalUpdateDomain::systemverilog_active },
            WaitSensitivity { },
            Jump { 0U },
        };
        require(interpreter.add_process(std::move(consumer)) == 2U,
            "the internal wire connects the region to its std_logic output boundary");

        interpreter.start();
        require(OwnedDriverDemotionTestAccess::startup_banked(
                    interpreter, 0U)
                && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                    interpreter, 0U),
            "whole VHDL projected constants use the compact startup owner");
        const auto facts
            = OwnedDriverDemotionTestAccess::region_constant_inputs(
                interpreter);
        require(facts.size() == 1U
                && facts.front().owner == 0U
                && facts.front().signal == source
                && facts.front().offset == 0U
                && facts.front().width == width
                && facts.front().value_kind == ValueKind::logic9
                && facts.front().domain == SignalUpdateDomain::generic
                && facts.front().update_kind
                    == RegionUpdateKind::vhdl_projected
                && facts.front().projected_mode == mode
                && facts.front().projected_delay == delay
                && facts.front().projected_rejection == rejection
                && facts.front().value == value,
            "the guarded boundary fact retains exact projected timing, mode, owner, and Logic9 planes");
        require(OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, source) == initial
                && initial != facts.front().value,
            "the startup candidate does not replace current U before its transaction publishes");

        SignalTraceCapture trace;
        trace.watched_signal = source;
        interpreter.scheduler().set_trace_hook(
            &trace, &SignalTraceCapture::receive);
        const auto before_publication
            = interpreter.run(delay - 1U);
        require(before_publication.status == RunStatus::time_limit
                && before_publication.time == delay - 1U
                && OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, source) == initial
                && OwnedDriverDemotionTestAccess::last_signal_value(
                    interpreter, source) == initial
                && trace.count == 0U,
            "a delayed projected startup remains unpublished before the exact due time");

        const auto completed = interpreter.run();
        interpreter.scheduler().set_trace_hook(nullptr, nullptr);
        require(completed.status == RunStatus::completed
                && completed.time == delay
                && interpreter.process_instruction(0U) == 4U
                && OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, source) == value
                && OwnedDriverDemotionTestAccess::stored_signal_value(
                    interpreter, source) == value
                && OwnedDriverDemotionTestAccess::last_signal_value(
                    interpreter, source) == initial
                && OwnedDriverDemotionTestAccess::stored_driver_value(
                    interpreter, 0U, source) == value
                && OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, output) == value,
            "the original projected owner commits the all-nine-state value at its declared time");
        require(trace.count == 2U && !trace.overflow
                && trace.entries[0].kind
                    == SchedulerTraceKind::signal_transaction
                && trace.entries[1].kind
                    == SchedulerTraceKind::signal_change
                && trace.entries[0].time == delay
                && trace.entries[1].time == delay
                && !trace.entries[0].systemverilog
                && !trace.entries[1].systemverilog,
            "projected transaction and value-change events retain generic VHDL provenance");

        const auto forced = logic9_cycle(width, width_index + 1U);
        require(forced != value,
            "the intervention differs from the guarded projected constant");
        OwnedDriverDemotionTestAccess::force_signal(
            interpreter, source, forced);
        require(OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter).empty(),
            "a current force excludes a projected startup specialization");
        OwnedDriverDemotionTestAccess::release_signal(
            interpreter, source, width);
        require(OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter) == facts,
            "force release restores the exact projected fact for recertification");
        interpreter.deposit_signal(source, forced);
        require(OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, source) == forced
                && OwnedDriverDemotionTestAccess::stored_driver_value(
                    interpreter, 0U, source) == value
                && OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter) == facts,
            "a later deposit changes the current input without rewriting the guarded projected candidate");
        static_cast<void>(interpreter.signal_value(source));
        require(OwnedDriverDemotionTestAccess::region_constant_inputs(
                    interpreter).empty(),
            "late current-value observation remains a permanent admission guard");
    }
}

void test_vhdl_projected_equal_value_still_marks_transaction()
{
    constexpr std::size_t width { 9U };
    Interpreter interpreter;
    const auto initial = PackedLogic4::from_logic9_msb_string(
        std::string(width, 'U'));
    const auto signal = interpreter.add_signal(Signal {
        "vhdl_projected_equal_constant", initial,
        ResolutionKind::std_logic, ValueKind::logic9,
    });
    require(interpreter.add_process(
                vhdl_projected_logic9_constant_driver_process(
                    0U, signal, initial, 1U, 0U,
                    ProjectedDelayMode::inertial)) == 0U,
        "equal projected startup retains its original owner");

    SignalTraceCapture trace;
    trace.watched_signal = signal;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);
    interpreter.start();
    const auto completed = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(completed.status == RunStatus::completed
            && completed.time == 1U
            && OwnedDriverDemotionTestAccess::current_signal_value(
                interpreter, signal) == initial
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, signal) == initial
            && OwnedDriverDemotionTestAccess::stored_driver_value(
                interpreter, 0U, signal) == initial,
        "an equal VHDL projected constant preserves current, LAST, and owner values");
    require(trace.count == 1U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[0].time == 1U
            && !trace.entries[0].systemverilog,
        "an equal VHDL projected write still records its transaction without a change event");
}

struct ProjectedSliceRun {
    PackedLogic4 initial_current;
    PackedLogic4 initial_last;
    PackedLogic4 initial_stored;
    PackedLogic4 initial_driver;
    PackedLogic4 current;
    PackedLogic4 last;
    PackedLogic4 stored;
    PackedLogic4 driver;
    std::optional<std::pair<SimulationTick, std::uint64_t>>
        initial_transaction;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    std::array<SignalTraceCapture::Entry, 8U> trace_entries { };
    std::size_t trace_count { };
    bool trace_overflow { };
    bool frame_materialized { };
    SimulationTick completed_time { };
};

ProjectedSliceRun run_vhdl_projected_slice_constant(
    const ValueKind kind,
    PackedLogic4 value,
    const ProjectedDelayMode mode,
    const SimulationTick delay,
    const SimulationTick rejection,
    const bool force_checked_route,
    const bool whole_driver_region,
    const std::size_t target_width = 17U,
    const std::uint32_t offset = 3U)
{
    const auto initial = kind == ValueKind::logic9
        ? PackedLogic4::from_logic9_msb_string(
              std::string(target_width, 'Z'))
        : PackedLogic4 { target_width, Logic4::z };
    Interpreter interpreter;
    const auto target = interpreter.add_signal(Signal {
        "projected_slice_constant_" + std::to_string(
            static_cast<unsigned>(kind)),
        initial,
        kind == ValueKind::logic9
            ? ResolutionKind::std_logic
            : ResolutionKind::sv_wire,
        kind,
    });
    require(interpreter.add_process(
                vhdl_projected_slice_constant_driver_process(
                    0U, target, offset, value, delay, rejection,
                    mode, force_checked_route, whole_driver_region)) == 0U,
        "projected slice owner retains its original process identity");

    interpreter.start();
    const bool should_be_compact
        = !force_checked_route && !whole_driver_region;
    require(OwnedDriverDemotionTestAccess::startup_banked(
                interpreter, 0U) == should_be_compact
            && (should_be_compact
                ? OwnedDriverDemotionTestAccess::projected_slice_descriptor_matches(
                    interpreter, 0U, target, offset, value.width(), kind,
                    delay, rejection, mode)
                : !OwnedDriverDemotionTestAccess::process_frame_materialized(
                    interpreter, 0U)),
        "only the exact projected slice shape enters the compact startup bank");

    ProjectedSliceRun result;
    result.initial_current
        = OwnedDriverDemotionTestAccess::current_signal_value(
            interpreter, target);
    result.initial_last
        = OwnedDriverDemotionTestAccess::last_signal_value(
            interpreter, target);
    result.initial_stored
        = OwnedDriverDemotionTestAccess::stored_signal_value(
            interpreter, target);
    result.initial_driver
        = OwnedDriverDemotionTestAccess::stored_driver_value(
            interpreter, 0U, target);
    result.initial_transaction
        = OwnedDriverDemotionTestAccess::signal_transaction_stamp(
            interpreter, target);

    ExecutionPointCapture points;
    interpreter.set_execution_point_hook(
        [&](Scheduler& scheduler, const ExecutionPoint& point) {
            points.receive(scheduler, point);
        });
    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);
    const auto stopped = interpreter.run();
    require(stopped.status == RunStatus::stopped
            && points.count == 1U && !points.overflow
            && points.entries[0].kind == ExecutionPointKind::process_entry
            && points.entries[0].time == 0U
            && OwnedDriverDemotionTestAccess::current_signal_value(
                interpreter, target) == result.initial_current
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target) == result.initial_last
            && OwnedDriverDemotionTestAccess::stored_signal_value(
                interpreter, target) == result.initial_stored
            && OwnedDriverDemotionTestAccess::stored_driver_value(
                interpreter, 0U, target) == result.initial_driver
            && trace.count == 0U,
        "stopping at process entry leaves the projected slice unpublished");

    interpreter.scheduler().clear_stop();
    if (delay != 0U) {
        const auto pending = interpreter.run(delay - 1U);
        require(pending.status == RunStatus::time_limit
                && pending.time == delay - 1U
                && OwnedDriverDemotionTestAccess::current_signal_value(
                    interpreter, target) == result.initial_current
                && OwnedDriverDemotionTestAccess::last_signal_value(
                    interpreter, target) == result.initial_last
                && OwnedDriverDemotionTestAccess::stored_signal_value(
                    interpreter, target) == result.initial_stored
                && OwnedDriverDemotionTestAccess::stored_driver_value(
                    interpreter, 0U, target) == result.initial_driver
                && trace.count == 0U,
            "a delayed projected slice stays pending through the preceding tick");
    }
    const auto completed = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(completed.status == RunStatus::completed
            && completed.time == delay,
        "projected slice process resumes and publishes at its exact due tick");

    auto expected_current = result.initial_current;
    expected_current.insert_bits(value, offset);
    auto expected_stored = result.initial_stored;
    expected_stored.insert_bits(value, offset);
    auto expected_driver = result.initial_driver;
    expected_driver.insert_bits(value, offset);
    const bool changed = expected_current != result.initial_current;
    const auto final_transaction
        = OwnedDriverDemotionTestAccess::signal_transaction_stamp(
            interpreter, target);
    require(OwnedDriverDemotionTestAccess::current_signal_value(
                interpreter, target) == expected_current
            && OwnedDriverDemotionTestAccess::stored_signal_value(
                interpreter, target) == expected_stored
            && OwnedDriverDemotionTestAccess::stored_driver_value(
                interpreter, 0U, target) == expected_driver
            && final_transaction.has_value()
            && final_transaction->first == delay
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target)
                == (changed ? result.initial_current : result.initial_last),
        "slice publication preserves current, LAST, stored, and original raw driver values");
    const auto expected_events = changed ? 2U : 1U;
    require(trace.count == expected_events && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[0].time == delay
            && trace.entries[0].phase == SchedulerPhase::update
            && !trace.entries[0].systemverilog
            && final_transaction->second == trace.entries[0].delta + 1U
            && (changed
                ? trace.entries[1].kind == SchedulerTraceKind::signal_change
                    && trace.entries[1].time == delay
                    && trace.entries[1].phase == SchedulerPhase::update
                    && !trace.entries[1].systemverilog
                : true),
        "slice transaction and change events retain generic update-phase order");

    result.current = OwnedDriverDemotionTestAccess::current_signal_value(
        interpreter, target);
    result.last = OwnedDriverDemotionTestAccess::last_signal_value(
        interpreter, target);
    result.stored = OwnedDriverDemotionTestAccess::stored_signal_value(
        interpreter, target);
    result.driver = OwnedDriverDemotionTestAccess::stored_driver_value(
        interpreter, 0U, target);
    result.transaction = final_transaction;
    result.trace_entries = trace.entries;
    result.trace_count = trace.count;
    result.trace_overflow = trace.overflow;
    result.frame_materialized
        = OwnedDriverDemotionTestAccess::process_frame_materialized(
            interpreter, 0U);
    result.completed_time = completed.time;
    require(result.frame_materialized == !should_be_compact,
        "only a fallback slice process materializes an execution frame");
    return result;
}

bool same_projected_slice_run(
    const ProjectedSliceRun& left, const ProjectedSliceRun& right)
{
    if (left.initial_current != right.initial_current
        || left.initial_last != right.initial_last
        || left.initial_stored != right.initial_stored
        || left.initial_driver != right.initial_driver
        || left.initial_transaction != right.initial_transaction
        || left.current != right.current || left.last != right.last
        || left.stored != right.stored || left.driver != right.driver
        || left.transaction != right.transaction
        || left.trace_count != right.trace_count
        || left.trace_overflow != right.trace_overflow
        || left.completed_time != right.completed_time) {
        return false;
    }
    for (std::size_t index = 0U; index < left.trace_count; ++index) {
        const auto& left_entry = left.trace_entries[index];
        const auto& right_entry = right.trace_entries[index];
        if (left_entry.kind != right_entry.kind
            || left_entry.phase != right_entry.phase
            || left_entry.time != right_entry.time
            || left_entry.systemverilog != right_entry.systemverilog
            || left_entry.delta != right_entry.delta) {
            return false;
        }
    }
    return true;
}

void test_vhdl_projected_slice_constant_startup_and_fallback()
{
    const auto logic4_value = PackedLogic4::from_msb_string("1011");
    const auto logic4_compact = run_vhdl_projected_slice_constant(
        ValueKind::logic4, logic4_value,
        ProjectedDelayMode::inertial, 5U, 2U, false, false);
    const auto logic4_fallback = run_vhdl_projected_slice_constant(
        ValueKind::logic4, logic4_value,
        ProjectedDelayMode::inertial, 5U, 2U, true, false);
    require(same_projected_slice_run(logic4_compact, logic4_fallback),
        "Logic4 compact and ordinary inertial slice execution match exactly");

    const auto logic9_value = logic9_cycle(9U);
    const auto logic9_compact = run_vhdl_projected_slice_constant(
        ValueKind::logic9, logic9_value,
        ProjectedDelayMode::transport, 5U, 7U, false, false);
    const auto logic9_fallback = run_vhdl_projected_slice_constant(
        ValueKind::logic9, logic9_value,
        ProjectedDelayMode::transport, 5U, 7U, true, false);
    require(same_projected_slice_run(logic9_compact, logic9_fallback),
        "Logic9 transport slice retains all nine states and timing on fallback");

    const auto wide_logic9_value = logic9_cycle(65U, 4U);
    const auto wide_logic9_compact = run_vhdl_projected_slice_constant(
        ValueKind::logic9, wide_logic9_value,
        ProjectedDelayMode::inertial, 4U, 1U, false, false, 129U, 61U);
    const auto wide_logic9_fallback = run_vhdl_projected_slice_constant(
        ValueKind::logic9, wide_logic9_value,
        ProjectedDelayMode::inertial, 4U, 1U, true, false, 129U, 61U);
    require(same_projected_slice_run(
                wide_logic9_compact, wide_logic9_fallback),
        "a 65-bit Logic9 projected source crossing bit 63 matches checked execution");

    const auto logic4_equal = PackedLogic4 { 4U, Logic4::z };
    const auto equal_logic4_compact = run_vhdl_projected_slice_constant(
        ValueKind::logic4, logic4_equal,
        ProjectedDelayMode::inertial, 0U, 0U, false, false);
    const auto equal_logic4_fallback = run_vhdl_projected_slice_constant(
        ValueKind::logic4, logic4_equal,
        ProjectedDelayMode::inertial, 0U, 0U, true, false);
    require(same_projected_slice_run(
                equal_logic4_compact, equal_logic4_fallback),
        "equal zero-delay Logic4 slice still records the checked transaction");

    const auto logic9_equal
        = PackedLogic4::from_logic9_msb_string("ZZZZ");
    const auto equal_logic9_compact = run_vhdl_projected_slice_constant(
        ValueKind::logic9, logic9_equal,
        ProjectedDelayMode::inertial, 0U, 0U, false, false);
    const auto equal_logic9_fallback = run_vhdl_projected_slice_constant(
        ValueKind::logic9, logic9_equal,
        ProjectedDelayMode::inertial, 0U, 0U, true, false);
    require(same_projected_slice_run(
                equal_logic9_compact, equal_logic9_fallback),
        "equal zero-delay Logic9 slice preserves four-plane transaction semantics");

    const auto exact_geometry = run_vhdl_projected_slice_constant(
        ValueKind::logic4, logic4_value,
        ProjectedDelayMode::inertial, 5U, 2U, false, false);
    const auto broad_geometry = run_vhdl_projected_slice_constant(
        ValueKind::logic4, logic4_value,
        ProjectedDelayMode::inertial, 5U, 2U, false, true);
    require(same_projected_slice_run(exact_geometry, broad_geometry),
        "a conservative whole-driver region uses checked projected slice semantics");
}

void test_malformed_projected_slice_keeps_checked_error_timing()
{
    Interpreter interpreter;
    const auto target = interpreter.add_signal(Signal {
        "malformed_projected_slice_target",
        PackedLogic4 { 8U, Logic4::z }, ResolutionKind::sv_wire,
    });
    const auto value = PackedLogic4::from_msb_string("101");
    require(interpreter.add_process(
                vhdl_projected_slice_constant_driver_process(
                    0U, target, 2U, value, 1U, 2U,
                    ProjectedDelayMode::inertial)) == 0U,
        "malformed projected slice retains its source process identity");
    interpreter.start();
    const auto initial
        = OwnedDriverDemotionTestAccess::current_signal_value(
            interpreter, target);
    const auto initial_last
        = OwnedDriverDemotionTestAccess::last_signal_value(
            interpreter, target);
    const auto initial_stored
        = OwnedDriverDemotionTestAccess::stored_signal_value(
            interpreter, target);
    const auto initial_raw
        = OwnedDriverDemotionTestAccess::stored_driver_value(
            interpreter, 0U, target);
    const auto initial_transaction
        = OwnedDriverDemotionTestAccess::signal_transaction_stamp(
            interpreter, target);
    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);
    require(!OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "an inertial rejection interval beyond its first delay declines compaction");
    bool rejected { };
    try {
        static_cast<void>(interpreter.run());
    } catch (const std::invalid_argument& error) {
        rejected = std::string_view(error.what())
            == "projected-waveform rejection limit exceeds its first delay";
    }
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(rejected
            && OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U)
            && OwnedDriverDemotionTestAccess::current_signal_value(
                interpreter, target) == initial
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target) == initial_last
            && OwnedDriverDemotionTestAccess::stored_signal_value(
                interpreter, target) == initial_stored
            && OwnedDriverDemotionTestAccess::stored_driver_value(
                interpreter, 0U, target) == initial_raw
            && OwnedDriverDemotionTestAccess::signal_transaction_stamp(
                interpreter, target) == initial_transaction
            && trace.count == 0U,
        "the checked interpreter preserves the malformed waveform error before publication");
}

void test_multiwrite_projected_slice_remains_checked()
{
    constexpr std::uint32_t offset { 2U };
    constexpr SimulationTick delay { 5U };
    const auto first = PackedLogic4::from_msb_string("101");
    const auto second = PackedLogic4::from_msb_string("1X0");
    Interpreter interpreter;
    const auto target = interpreter.add_signal(Signal {
        "multiwrite_projected_slice_target",
        PackedLogic4 { 8U, Logic4::z }, ResolutionKind::sv_wire,
    });
    Process process;
    process.id = 0U;
    process.name = "multiwrite_projected_slice_owner";
    process.scheduling_domain = ProcessSchedulingDomain::generic;
    process.register_count = 1U;
    process.register_value_kinds = { ValueKind::logic4 };
    process.driver_regions = { { target, offset, 3U, false } };
    process.operations = {
        DebugPoint { DebugPointKind::process_entry, { } },
        LoadConstant { 0U, first },
        WriteProjectedSlice {
            target, 0U, offset, delay, delay,
            ProjectedDelayMode::inertial
        },
        LoadConstant { 0U, second },
        WriteProjectedSlice {
            target, 0U, offset, delay, delay,
            ProjectedDelayMode::inertial
        },
        Halt { },
    };
    require(interpreter.add_process(std::move(process)) == 0U,
        "multiwrite projected process retains its dense source identity");
    interpreter.start();
    require(!OwnedDriverDemotionTestAccess::startup_banked(interpreter, 0U)
            && !OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U),
        "multiwrite processes remain outside the exact one-write compact shape");
    const auto initial_current
        = OwnedDriverDemotionTestAccess::current_signal_value(
            interpreter, target);
    const auto initial_last
        = OwnedDriverDemotionTestAccess::last_signal_value(
            interpreter, target);
    auto expected = OwnedDriverDemotionTestAccess::stored_driver_value(
        interpreter, 0U, target);
    expected.insert_bits(second, offset);
    SignalTraceCapture trace;
    trace.watched_signal = target;
    interpreter.scheduler().set_trace_hook(
        &trace, &SignalTraceCapture::receive);
    const auto before_due = interpreter.run(delay - 1U);
    require(before_due.status == RunStatus::time_limit
            && before_due.time == delay - 1U
            && trace.count == 0U,
        "same-owner projected writes remain pending until their shared due tick");
    const auto completed = interpreter.run();
    interpreter.scheduler().set_trace_hook(nullptr, nullptr);
    require(completed.status == RunStatus::completed
            && completed.time == delay
            && OwnedDriverDemotionTestAccess::current_signal_value(
                interpreter, target) == expected
            && OwnedDriverDemotionTestAccess::stored_signal_value(
                interpreter, target) == expected
            && OwnedDriverDemotionTestAccess::stored_driver_value(
                interpreter, 0U, target) == expected
            && OwnedDriverDemotionTestAccess::last_signal_value(
                interpreter, target) == initial_current
            && initial_last == initial_current
            && OwnedDriverDemotionTestAccess::process_frame_materialized(
                interpreter, 0U)
            && trace.count == 2U && !trace.overflow
            && trace.entries[0].kind
                == SchedulerTraceKind::signal_transaction
            && trace.entries[1].kind == SchedulerTraceKind::signal_change
            && trace.entries[0].time == delay
            && trace.entries[1].time == delay,
        "checked inertial multiwrite keeps last assignment and one due publication");
}

} // namespace

int main()
{
    try {
        test_data_only_startup_write_equal_value_transaction();
        test_data_only_startup_write_preserves_transaction_order();
        test_data_only_startup_write_survives_pre_run_observation_and_force();
        test_constant_driver_startup_bank();
        test_shared_constant_driver_startup_uses_instance_signal();
        test_nonmatching_constant_process_uses_interpreter();
        test_two_register_constant_copy_startup_bank_stop_resume();
        test_two_register_constant_copy_with_coverage_uses_interpreter();
        test_two_register_xz_constant_copy_uses_interpreter();
        test_constant_driver_startup_bank_slice();
        test_constant_driver_startup_bank_slice_equal_value_transaction();
        test_shared_constant_slice_startup_uses_instance_signal();
        test_compact_constant_promotion_preserves_program_view();
        test_constant_startup_fact_is_bound_to_region_input();
        test_logic9_constant_startup_and_region_fact();
        test_vhdl_projected_logic9_constant_startup_and_region_fact();
        test_vhdl_projected_equal_value_still_marks_transaction();
        test_vhdl_projected_slice_constant_startup_and_fallback();
        test_malformed_projected_slice_keeps_checked_error_timing();
        test_multiwrite_projected_slice_remains_checked();
        std::cout << "constant driver startup bank tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "constant driver startup bank test failed: "
                  << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
