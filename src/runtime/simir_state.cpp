// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <utility>

namespace fsim::runtime::simir {

[[nodiscard]] std::size_t Interpreter::Impl::InertialDriverKeyHash::operator()(
    const InertialDriverKey& key) const noexcept
{
    auto result = static_cast<std::size_t>(key.process);
    result ^= static_cast<std::size_t>(key.signal)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.offset)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.width)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.process_domain)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.phase)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    return result;
}

[[nodiscard]] std::size_t Interpreter::Impl::ProjectedDriverKeyHash::operator()(
    const ProjectedDriverKey& key) const noexcept
{
    auto result = static_cast<std::size_t>(key.process);
    result ^= static_cast<std::size_t>(key.signal)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    result ^= static_cast<std::size_t>(key.offset)
        + UINT64_C(0x9e3779b97f4a7c15)
        + (result << 6U) + (result >> 2U);
    return result;
}

void Interpreter::Impl::discard_scheduler_work() noexcept
{
    clear_systemverilog_update_pool();
    cohort_snapshots.discard();
    active_cohort_ready.clear();
    for (auto& cohort : static_sensitivity_cohorts) {
        cohort.pending = { };
        cohort.ready.clear();
    }
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        if (auto* compact = processes.compact_constant(id)) {
            compact->queued = false;
        } else {
            processes[id].queued = false;
        }
    }
}

[[nodiscard]] SignalHot& Interpreter::Impl::get_signal(SignalId id)
{
    if (id >= signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    materialize_direct_signal(id);
    return signals[id];
}

[[nodiscard]] const SignalHot& Interpreter::Impl::get_signal(
    SignalId id) const
{
    if (id >= signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    const_cast<Impl*>(this)->materialize_direct_signal(id);
    return signals[id];
}

[[nodiscard]] SignalCold& Interpreter::Impl::get_signal_cold(SignalId id)
{
    if (id >= signal_cold.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    return signal_cold[id];
}

[[nodiscard]] const SignalCold& Interpreter::Impl::get_signal_cold(
    SignalId id) const
{
    if (id >= signal_cold.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    return signal_cold[id];
}

DriverRecord* Interpreter::Impl::direct_single_driver_record(
    const SignalId signal_id) noexcept
{
    if (signal_id >= direct_single_driver_routes.size()
        || signal_id >= driver_values.size()
        || has_container_signal_alias(signal_id)) {
        return nullptr;
    }
    const auto& route = direct_single_driver_routes[signal_id];
    return route.active
        ? driver_values[signal_id].find(route.process)
        : nullptr;
}

const DriverRecord* Interpreter::Impl::direct_single_driver_record(
    const SignalId signal_id) const noexcept
{
    if (signal_id >= direct_single_driver_routes.size()
        || signal_id >= driver_values.size()
        || has_container_signal_alias(signal_id)) {
        return nullptr;
    }
    const auto& route = direct_single_driver_routes[signal_id];
    return route.active
        ? driver_values[signal_id].find(route.process)
        : nullptr;
}

bool Interpreter::Impl::is_aggregate_signal_proxy(
    const SignalId signal_id) const noexcept
{
    return signal_id < signal_container_aggregate_aliases.size()
        && signal_container_aggregate_aliases[signal_id].has_value();
}

bool Interpreter::Impl::has_container_signal_alias(
    const SignalId signal_id) const noexcept
{
    if (is_aggregate_signal_proxy(signal_id)
        || (signal_id < signal_container_element_aliases.size()
            && signal_container_element_aliases[signal_id])) {
        return true;
    }
    return signal_id < signal_container_aliases.size()
        && !signal_container_aliases[signal_id].empty();
}

void Interpreter::Impl::revoke_stable_writer_shadow_for_container_alias(
    const SignalId signal_id)
{
    if (signal_id >= stable_single_writer_processes.size()
        || stable_single_writer_processes[signal_id]
            == std::numeric_limits<ProcessId>::max()) {
        return;
    }
    if (signal_writer_revision
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error {
            "SimIR signal-writer topology revision overflow"
        };
    }
    stable_single_writer_processes[signal_id]
        = std::numeric_limits<ProcessId>::max();
    ++signal_writer_revision;
}

void Interpreter::Impl::materialize_direct_signal(const SignalId id)
{
    if (id >= direct_signal_materialization_pending.size()
        || direct_signal_materialization_pending[id] == 0U) {
        return;
    }
    const auto width = signals[id].initial_value.width();
    if (signals[id].value_kind == ValueKind::logic9) {
        const auto current = Logic9Word {
            width,
            { direct_signal_logic9_plane0[id],
                direct_signal_logic9_plane1[id],
                direct_signal_logic9_plane2[id],
                direct_signal_logic9_plane3[id] }
        };
        const auto previous = Logic9Word {
            width,
            { direct_signal_last_logic9_plane0[id],
                direct_signal_last_logic9_plane1[id],
                direct_signal_last_logic9_plane2[id],
                direct_signal_last_logic9_plane3[id] }
        };
        signals[id].initial_value.assign_logic9_word(current);
        signal_last_values[id].assign_logic9_word(previous);
        driven_values[id].assign_logic9_word(current);
        if (auto* record = direct_single_driver_record(id)) {
            record->value.assign_logic9_word(current);
        }
        direct_signal_materialization_pending[id] = 0U;
        return;
    }
    const auto current = Logic4Word {
        width, direct_signal_aval[id], direct_signal_bval[id]
    };
    const auto previous = Logic4Word {
        width, direct_signal_last_aval[id], direct_signal_last_bval[id]
    };
    signals[id].initial_value.assign_word(current);
    signal_last_values[id].assign_word(previous);
    driven_values[id].assign_word(current);
    if (auto* record = direct_single_driver_record(id)) {
        record->value.assign_word(current);
    }
    direct_signal_materialization_pending[id] = 0U;
}

[[nodiscard]] Interpreter::Impl::ProcessState&
Interpreter::Impl::get_process(ProcessId id)
{
    if (id >= processes.size()) {
        throw std::out_of_range("invalid SimIR process ID");
    }
    return processes.promote_state(id);
}

void Interpreter::Impl::ProcessTable::reserve_initial(
    const std::size_t capacity)
{
    if (!slots_.empty() || frozen_) {
        throw std::logic_error {
            "cannot reserve process table after registration"
        };
    }
    slots_.reserve(capacity);
    stable_full_.reserve_initial(capacity);
}

void Interpreter::Impl::ProcessTable::push_back(ProcessState&& process)
{
    if (process.id != slots_.size()) {
        throw std::invalid_argument {
            "SimIR process table IDs must remain dense and ordered"
        };
    }
    const auto& program_template
        = process.cold().program_storage().program_template;
    const auto scheduling_domain = program_template != nullptr
        ? program_template->scheduling_domain
        : ProcessSchedulingDomain::generic;
    const auto full_index = stable_full_.size();
    if (slots_.size() == slots_.capacity()) {
        const auto next_capacity = std::max<std::size_t>(
            4U, slots_.capacity() * 2U);
        slots_.reserve(next_capacity);
    }
    stable_full_.push_back(std::move(process));
    slots_.push_back(
        Slot { SlotKind::full, scheduling_domain, full_index });
}

void Interpreter::Impl::ProcessTable::push_compact(
    ConstantDriverStartupEntry&& process)
{
    if (frozen_ || process.id != slots_.size()) {
        throw std::logic_error {
            "compact constant processes are admitted before start"
        };
    }
    const auto scheduling_domain
        = process.program_storage->program_template->scheduling_domain;
    if (slots_.size() == slots_.capacity()) {
        const auto next_capacity = std::max<std::size_t>(
            4U, slots_.capacity() * 2U);
        slots_.reserve(next_capacity);
    }
    const auto compact_index = compact_constants_.size();
    compact_constants_.push_back(std::move(process));
    try {
        slots_.push_back(Slot {
            SlotKind::compact_constant, scheduling_domain, compact_index });
    } catch (...) {
        compact_constants_.pop_back();
        throw;
    }
    ++active_compact_constants_;
}

void Interpreter::Impl::ProcessTable::freeze_initial_storage()
{
    frozen_ = true;
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::full_state(const std::size_t index)
{
    return stable_full_[index];
}

const Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::full_state(const std::size_t index) const
{
    return stable_full_[index];
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::promote(const std::size_t id)
{
    if (id >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    auto& slot = slots_[id];
    if (slot.kind == SlotKind::full) {
        return full_state(slot.index);
    }

    const auto compact_index = slot.index;
    const auto scheduling_domain = slot.scheduling_domain;
    auto& compact = compact_constants_[compact_index];
    std::optional<OperationList> restored_startup_body;
    if (compact.startup_write_bank != nullptr) {
        // Reconstruct before appending or publishing full state. Allocation
        // failure leaves the compact descriptor and any borrowed views live.
        restored_startup_body.emplace(
            compact.startup_write_bank->operations());
    }
    ProcessState promoted;
    promoted.id = compact.id;
    promoted.generation = compact.generation;
    promoted.pc = compact.pc;
    promoted.execution_phase = compact.execution_phase;
    promoted.status = compact.status;
    promoted.static_trigger_mask = compact.static_trigger_mask;
    promoted.queued = compact.queued;
    promoted.waiting_on_static = compact.waiting_on_static;
    promoted.waiting_on_signal = compact.waiting_on_signal;
    promoted.halted = compact.halted;
    promoted.suspended = compact.suspended;
    promoted.suspended_wake = compact.suspended_wake;
    promoted.killed = compact.killed;
    promoted.suspended_status = compact.suspended_status;
    promoted.cold().design_process = compact.id;
    promoted.cold().random_state = compact.random_state;
    promoted.cold().current_source = compact.current_source;
    promoted.cold().current_scope = compact.current_scope;
    promoted.cold().track_interpreter_operations
        = compact.track_interpreter_operations;
    promoted.cold().interpreter_operations
        = compact.interpreter_operations;
    promoted.cold().profile_calls = compact.profile_calls;
    promoted.cold().profile_interpreter_operations
        = compact.profile_interpreter_operations;
    promoted.cold().profile_updates = compact.profile_updates;
    promoted.cold().profile_total_nanoseconds
        = compact.profile_total_nanoseconds;
    const auto full_index = stable_full_.size();
    stable_full_.push_back(std::move(promoted));
    if (restored_startup_body) {
        compact.program_storage->instance_program.operations
            = std::move(*restored_startup_body);
    }
    full_state(full_index).cold().retained_program_storage
        = std::move(compact.program_storage);

    slot = Slot { SlotKind::full, scheduling_domain, full_index };
    --active_compact_constants_;
    return full_state(full_index);
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::operator[](const std::size_t index)
{
    if (index >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    if (slots_[index].kind != SlotKind::full) {
        throw std::logic_error {
            "compact process access requires explicit promotion"
        };
    }
    return full_state(slots_[index].index);
}

const Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::operator[](const std::size_t index) const
{
    if (index >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    if (slots_[index].kind != SlotKind::full) {
        throw std::logic_error {
            "compact constant process has no full ProcessState"
        };
    }
    return full_state(slots_[index].index);
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::at(const std::size_t index)
{
    return (*this)[index];
}

const Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::at(const std::size_t index) const
{
    return (*this)[index];
}

Interpreter::Impl::ProcessState&
Interpreter::Impl::ProcessTable::promote_state(const std::size_t index)
{
    return promote(index);
}

Interpreter::Impl::ProcessState*
Interpreter::Impl::ProcessTable::full_state_if_present(
    const ProcessId id) noexcept
{
    if (id >= slots_.size() || slots_[id].kind != SlotKind::full) {
        return nullptr;
    }
    return std::addressof(full_state(slots_[id].index));
}

const Interpreter::Impl::ProcessState*
Interpreter::Impl::ProcessTable::full_state_if_present(
    const ProcessId id) const noexcept
{
    if (id >= slots_.size() || slots_[id].kind != SlotKind::full) {
        return nullptr;
    }
    return std::addressof(full_state(slots_[id].index));
}

ProcessProgramView Interpreter::Impl::ProcessTable::program_view(
    const ProcessId id) const
{
    if (id >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    const auto& slot = slots_[id];
    if (slot.kind == SlotKind::full) {
        return full_state(slot.index).program();
    }
    const auto& compact = compact_constants_[slot.index];
    const auto& storage = *compact.program_storage;
    return ProcessProgramView {
        *storage.program_template, storage.instance_program,
        compact.startup_write_bank.get()
    };
}

const Process& Interpreter::Impl::ProcessTable::public_program(
    const ProcessId id) const
{
    if (id >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    if (slots_[id].kind == SlotKind::full) {
        return full_state(slots_[id].index).cold().public_program();
    }
    auto& storage = *compact_constants_[slots_[id].index].program_storage;
    if (!storage.public_program_facade) {
        storage.public_program_facade
            = std::make_unique<Process>(program_view(id).materialize());
    }
    return *storage.public_program_facade;
}

std::size_t Interpreter::Impl::ProcessTable::operation_count(
    const ProcessId id) const
{
    if (id >= slots_.size()) {
        throw std::out_of_range { "invalid SimIR process ID" };
    }
    if (const auto* const compact = compact_constant(id)) {
        if (compact->startup_write_bank != nullptr) {
            return compact->startup_write_bank->operation_count;
        }
        return compact->program_storage->instance_program.operations.size();
    }
    return program_view(id).operations().size();
}

Interpreter::Impl::ConstantDriverStartupEntry*
Interpreter::Impl::ProcessTable::compact_constant(
    const ProcessId id) noexcept
{
    if (id >= slots_.size()
        || slots_[id].kind != SlotKind::compact_constant) {
        return nullptr;
    }
    return std::addressof(compact_constants_[slots_[id].index]);
}

const Interpreter::Impl::ConstantDriverStartupEntry*
Interpreter::Impl::ProcessTable::compact_constant(
    const ProcessId id) const noexcept
{
    if (id >= slots_.size()
        || slots_[id].kind != SlotKind::compact_constant) {
        return nullptr;
    }
    return std::addressof(compact_constants_[slots_[id].index]);
}

bool Interpreter::Impl::ProcessTable::is_compact_constant(
    const ProcessId id) const noexcept
{
    return compact_constant(id) != nullptr;
}

std::size_t Interpreter::Impl::ProcessTable::compact_constant_count() const noexcept
{
    return active_compact_constants_;
}

bool Interpreter::Impl::ProcessTable::public_facade_materialized(
    const ProcessId id) const noexcept
{
    if (id >= slots_.size()) {
        return false;
    }
    const auto& slot = slots_[id];
    if (slot.kind == SlotKind::full) {
        return full_state(slot.index).cold()
            .program_storage().public_program_facade != nullptr;
    }
    return compact_constants_[slot.index].program_storage
        ->public_program_facade != nullptr;
}

void Interpreter::Impl::ProcessTable::add_profile_updates(
    const ProcessId id, const std::size_t count)
{
    if (auto* compact = compact_constant(id)) {
        compact->profile_updates += count;
        return;
    }
    full_state_if_present(id)->cold().profile_updates += count;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_updates(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->profile_updates;
    }
    return full_state_if_present(id)->cold().profile_updates;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_total_nanoseconds(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->profile_total_nanoseconds;
    }
    return full_state_if_present(id)->cold().profile_total_nanoseconds;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_calls(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->profile_calls;
    }
    return full_state_if_present(id)->cold().profile_calls;
}

std::uint64_t
Interpreter::Impl::ProcessTable::profile_interpreter_operations(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->profile_interpreter_operations;
    }
    return full_state_if_present(id)->cold().profile_interpreter_operations;
}

std::uint64_t Interpreter::Impl::ProcessTable::interpreter_operations(
    const ProcessId id) const
{
    if (const auto* compact = compact_constant(id)) {
        return compact->interpreter_operations;
    }
    return full_state_if_present(id)->cold().interpreter_operations;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_native_resumes(
    const ProcessId id) const
{
    return compact_constant(id) != nullptr
        ? 0U
        : full_state_if_present(id)->cold().profile_native_resumes;
}

std::uint64_t Interpreter::Impl::ProcessTable::profile_native_nanoseconds(
    const ProcessId id) const
{
    return compact_constant(id) != nullptr
        ? 0U
        : full_state_if_present(id)->cold().profile_native_nanoseconds;
}

bool Interpreter::Impl::ProcessTable::has_executor(
    const ProcessId id) const noexcept
{
    const auto* state = full_state_if_present(id);
    return state != nullptr && static_cast<bool>(state->executor);
}

[[nodiscard]] Interpreter::Impl::ProcessFrame&
Interpreter::Impl::ensure_process_frame(ProcessState& process)
{
    if (process.frame) {
        return *process.frame;
    }

    auto frame = std::make_shared<ProcessFrame>();
    frame->registers.assign(
        process.program().register_count(), PackedLogic4 { });
    frame->string_registers.assign(
        process.program().string_register_count(), { });
    frame->container_registers.reserve(
        process.program().container_register_count());
    const auto container_register_types
        = process_layout_detail::ProcessLayoutAccess::view(
            process.program().container_register_types());
    for (const auto& type : container_register_types) {
        frame->container_registers.push_back(
            default_container_register(type));
    }
    process.frame = std::move(frame);
    return *process.frame;
}

[[nodiscard]] bool Interpreter::Impl::can_install_deferred_executor(
    const ProcessState& process) noexcept
{
    return (!process.frame
               || (process.frame.use_count() == 1
                   && process.frame->vital_memories.empty()))
        && process.cold().dynamic_call_stack.empty()
        && process.cold().callable_frames.empty();
}

[[nodiscard]] PackedLogic4& Interpreter::Impl::get_register(ProcessState& process,
    RegisterId id)
{
    auto& frame = ensure_process_frame(process);
    if (id >= frame.registers.size()) {
        throw InterpreterError(process.id, process.pc,
            "invalid register ID");
    }
    return frame.registers[id];
}

[[nodiscard]] std::string& Interpreter::Impl::get_string_register(
    ProcessState& process,
    const StringRegisterId id)
{
    auto& frame = ensure_process_frame(process);
    if (id >= frame.string_registers.size()) {
        throw InterpreterError(
            process.id, process.pc, "invalid string register ID");
    }
    return frame.string_registers[id];
}

[[nodiscard]] StringObject& Interpreter::Impl::get_string_object(
    const StringObjectId id)
{
    if (id >= string_objects.size()) {
        throw std::out_of_range { "invalid SimIR string object ID" };
    }
    return string_objects[id];
}

[[nodiscard]] const StringObject& Interpreter::Impl::get_string_object(
    const StringObjectId id) const
{
    if (id >= string_objects.size()) {
        throw std::out_of_range { "invalid SimIR string object ID" };
    }
    return string_objects[id];
}

[[nodiscard]] std::string& Interpreter::Impl::string_object_storage(
    const StringObjectId id)
{
    return const_cast<std::string&>(
        std::as_const(*this).string_object_storage(id));
}

[[nodiscard]] const std::string& Interpreter::Impl::string_object_storage(
    const StringObjectId id) const
{
    const auto& object = get_string_object(id);
    if (!object.element_alias) {
        return object.initial_value;
    }
    const auto& alias = *object.element_alias;
    if (alias.object >= container_objects.size()
        || alias.ordinal >= container_objects[alias.object]
                                .initial_value.string_elements.size()) {
        throw std::out_of_range { "invalid SimIR string element alias" };
    }
    return container_objects[alias.object]
        .initial_value.string_elements[alias.ordinal];
}

[[nodiscard]] ContainerValue&
Interpreter::Impl::get_container_register(
    ProcessState& process,
    const ContainerRegisterId id)
{
    if (!process.frame || id >= process.frame->container_registers.size()) {
        throw InterpreterError(
            process.id, process.pc,
            "invalid container register ID");
    }
    auto& storage = process.frame->container_registers[id];
    if (!storage) {
        throw InterpreterError(
            process.id, process.pc,
            "uninitialized container register storage");
    }
    if (storage.use_count() != 1) {
        storage = std::make_shared<ContainerValue>(*storage);
    }
    return *storage;
}

[[nodiscard]] const ContainerValue&
Interpreter::Impl::read_container_register(
    const ProcessState& process,
    const ContainerRegisterId id) const
{
    if (!process.frame || id >= process.frame->container_registers.size()
        || !process.frame->container_registers[id]) {
        throw InterpreterError(
            process.id, process.pc,
            "invalid container register ID");
    }
    return *process.frame->container_registers[id];
}

[[nodiscard]] Interpreter::Impl::SharedContainerValue
Interpreter::Impl::container_register_storage(
    const ProcessState& process,
    const ContainerRegisterId id) const
{
    (void)read_container_register(process, id);
    return process.frame->container_registers[id];
}

void Interpreter::Impl::set_container_register_storage(
    ProcessState& process,
    const ContainerRegisterId id,
    SharedContainerValue value)
{
    if (!value || !process.frame
        || id >= process.frame->container_registers.size()) {
        throw InterpreterError(
            process.id, process.pc,
            "invalid container register storage");
    }
    process.frame->container_registers[id] = std::move(value);
}

[[nodiscard]] Interpreter::Impl::SharedContainerValue
Interpreter::Impl::default_container_register(const ContainerType& type)
{
    const auto found = std::ranges::find_if(
        default_container_values,
        [&](const SharedContainerValue& value) {
            return value && value->type == type;
        });
    if (found != default_container_values.end()) {
        return *found;
    }
    auto value = std::make_shared<ContainerValue>(
        default_container_value(type));
    default_container_values.push_back(value);
    return value;
}

[[nodiscard]] ContainerObject& Interpreter::Impl::get_container_object(
    const ContainerObjectId id)
{
    if (id >= container_objects.size()) {
        throw std::out_of_range { "invalid SimIR container object ID" };
    }
    return container_objects[id];
}

[[nodiscard]] const ContainerObject&
Interpreter::Impl::get_container_object(
    const ContainerObjectId id) const
{
    if (id >= container_objects.size()) {
        throw std::out_of_range { "invalid SimIR container object ID" };
    }
    return container_objects[id];
}

[[nodiscard]] ValueKind Interpreter::Impl::register_value_kind(
    const ProcessState& process,
    const RegisterId id)
{
    return process_layout_detail::ProcessLayoutAccess::copy_at_or(
        process.program().register_value_kinds(), id, ValueKind::logic4);
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::coerce_value_kind(
    PackedLogic4 value,
    const ValueKind kind)
{
    if (kind == ValueKind::logic9) {
        return value.is_logic9()
            ? value
            : value.promoted_to_logic9();
    }
    return value.is_logic9()
        ? collapse_to_logic4(value)
        : value;
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::normalize_signal_value(
    const SignalId signal,
    PackedLogic4 value) const
{
    auto normalized = coerce_value_kind(
        std::move(value),
        get_signal(signal).value_kind);
    const auto kind = get_signal(signal).systemverilog_scalar;
    // `time` is a 4-state integral type (IEEE 1800-2017 6.11), so its
    // payload may carry unknown bits.
    if (kind == SystemVerilogScalarKind::Time && has_unknown(normalized)) {
        return normalized;
    }
    if (kind != SystemVerilogScalarKind::None) {
        const auto decoded = decode_systemverilog_scalar_payload(
            normalized, kind);
        if (!decoded) {
            throw std::invalid_argument {
                "SimIR scalar signal has an invalid encoded payload"
            };
        }
        const auto classification = classify_systemverilog_scalar(decoded.value);
        // A real may hold an infinity or NaN (IEEE 754).
        if (kind != SystemVerilogScalarKind::Chandle && !classification) {
            throw std::invalid_argument {
                "SimIR scalar signal requires a finite value"
            };
        }
    }
    return normalized;
}

void Interpreter::Impl::remove_dynamic_wait_nonempty(ProcessState& process)
{
    auto& cold = process.cold();
    auto* const dynamic_wait = cold.dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr) {
        process.waiting_on_signal = false;
        return;
    }
    const auto old_wait_generation = dynamic_wait->dynamic_wait_generation;
    if (dynamic_wait->dynamic_wait_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "dynamic wait generation overflow");
    }
    ++dynamic_wait->dynamic_wait_generation;
    ensure_dynamic_fanout_counts();
    for (std::size_t sensitivity_index = 0;
        sensitivity_index < dynamic_wait->dynamic_sensitivity.size();
        ++sensitivity_index) {
        const auto signal
            = dynamic_wait->dynamic_sensitivity[sensitivity_index].signal;
        if (signal >= dynamic_fanout.size()
            || sensitivity_index
                >= dynamic_wait->dynamic_fanout_positions.size()) {
            continue;
        }
        auto& fanout = dynamic_fanout[signal];
        const auto position
            = dynamic_wait->dynamic_fanout_positions[sensitivity_index];
        if (position >= fanout.size()) {
            continue;
        }
        auto& registration = fanout[position];
        if (!registration.active
            || registration.process != process.id
            || registration.process_generation != process.generation
            || registration.wait_generation != old_wait_generation
            || registration.sensitivity_index != sensitivity_index) {
            continue;
        }
        registration.active = false;
        auto& active = dynamic_fanout_active_counts[signal];
        if (active != 0U) {
            --active;
        }
        ++dynamic_fanout_tombstone_counts[signal];
        compact_dynamic_fanout(signal);
    }
    dynamic_wait->dynamic_sensitivity.clear();
    dynamic_wait->dynamic_triggered.clear();
    dynamic_wait->dynamic_fanout_positions.clear();
    dynamic_wait->dynamic_wait_all = false;
    dynamic_wait->wait_order_events.clear();
    dynamic_wait->wait_order_index = 0;
    dynamic_wait->wait_order_result.reset();
    if (dynamic_wait->waiting_on_container) {
        auto& fanout
            = container_dynamic_fanout[*dynamic_wait->waiting_on_container];
        std::erase(fanout, process.id);
        dynamic_wait->waiting_on_container.reset();
    }
    process.waiting_on_signal = false;
}

void Interpreter::Impl::ensure_dynamic_fanout_counts()
{
    if (dynamic_fanout_active_counts.size() < dynamic_fanout.size()) {
        dynamic_fanout_active_counts.resize(dynamic_fanout.size());
    }
    if (dynamic_fanout_tombstone_counts.size() < dynamic_fanout.size()) {
        dynamic_fanout_tombstone_counts.resize(dynamic_fanout.size());
    }
}

void Interpreter::Impl::register_dynamic_wait_fanout(ProcessState& process)
{
    auto& cold = process.cold();
    auto* const dynamic_wait = cold.dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr) {
        throw std::logic_error {
            "dynamic wait registration has no process-side wait state"
        };
    }
    dynamic_wait->dynamic_fanout_positions.clear();
    dynamic_wait->dynamic_fanout_positions.reserve(
        dynamic_wait->dynamic_sensitivity.size());
    for (const auto& sensitivity : dynamic_wait->dynamic_sensitivity) {
        if (sensitivity.signal >= dynamic_fanout.size()) {
            fail(process, "dynamic wait references an invalid signal");
        }
    }
    if (dynamic_wait->dynamic_wait_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "dynamic wait generation overflow");
    }
    ++dynamic_wait->dynamic_wait_generation;
    ensure_dynamic_fanout_counts();
    std::size_t registered = 0;
    try {
        for (std::size_t index = 0;
            index < dynamic_wait->dynamic_sensitivity.size(); ++index) {
            const auto& sensitivity = dynamic_wait->dynamic_sensitivity[index];
            compact_dynamic_fanout(sensitivity.signal);
            auto& active = dynamic_fanout_active_counts[sensitivity.signal];
            if (active == std::numeric_limits<std::size_t>::max()) {
                fail(process, "dynamic wait registration count overflow");
            }
            auto& fanout = dynamic_fanout[sensitivity.signal];
            const auto position = fanout.size();
            fanout.push_back({
                process.id,
                process.generation,
                dynamic_wait->dynamic_wait_generation,
                sensitivity.edge,
                index,
                true
            });
            dynamic_wait->dynamic_fanout_positions.push_back(position);
            ++active;
            ++registered;
        }
    } catch (...) {
        for (std::size_t index = 0; index < registered; ++index) {
            const auto signal
                = dynamic_wait->dynamic_sensitivity[index].signal;
            const auto position
                = dynamic_wait->dynamic_fanout_positions[index];
            auto& fanout = dynamic_fanout[signal];
            if (position < fanout.size() && fanout[position].active
                && fanout[position].process == process.id
                && fanout[position].wait_generation
                    == dynamic_wait->dynamic_wait_generation) {
                fanout[position].active = false;
                --dynamic_fanout_active_counts[signal];
                ++dynamic_fanout_tombstone_counts[signal];
            }
        }
        dynamic_wait->dynamic_fanout_positions.clear();
        throw;
    }
}

[[nodiscard]] bool Interpreter::Impl::dynamic_wait_registration_is_current(
    const ProcessState& process,
    const DynamicWaitRegistration& registration,
    const SignalId signal) const noexcept
{
    if (!registration.active
        || registration.process != process.id
        || registration.process_generation != process.generation
        || registration.wait_generation == 0U
        || process.halted || !process.waiting_on_signal) {
        return false;
    }
    const auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr
        || dynamic_wait->dynamic_wait_generation != registration.wait_generation
        || registration.sensitivity_index
            >= dynamic_wait->dynamic_sensitivity.size()) {
        return false;
    }
    const auto& sensitivity = dynamic_wait->dynamic_sensitivity[
        registration.sensitivity_index];
    return sensitivity.signal == signal
        && sensitivity.edge == registration.edge;
}

void Interpreter::Impl::compact_dynamic_fanout(const SignalId signal)
{
    if (signal >= dynamic_fanout.size()) {
        return;
    }
    ensure_dynamic_fanout_counts();
    auto& fanout = dynamic_fanout[signal];
    const auto active = dynamic_fanout_active_counts[signal];
    const auto tombstones = dynamic_fanout_tombstone_counts[signal];
    if (tombstones == 0U
        || (active != 0U
            && (tombstones < 64U || tombstones < active))) {
        return;
    }
    std::erase_if(fanout, [&](const DynamicWaitRegistration& registration) {
        return registration.process >= processes.size()
            || !dynamic_wait_registration_is_current(
                processes[registration.process], registration, signal);
    });
    for (std::size_t index = 0; index < fanout.size(); ++index) {
        const auto& registration = fanout[index];
        if (registration.process >= processes.size()) {
            continue;
        }
        auto& process = processes[registration.process];
        auto* const dynamic_wait
            = process.cold().dynamic_wait_state_if_present();
        if (dynamic_wait != nullptr
            && registration.sensitivity_index
                < dynamic_wait->dynamic_fanout_positions.size()
            && dynamic_wait_registration_is_current(
                process, registration, signal)) {
            dynamic_wait->dynamic_fanout_positions[
                registration.sensitivity_index] = index;
        }
    }
    dynamic_fanout_active_counts[signal] = fanout.size();
    dynamic_fanout_tombstone_counts[signal] = 0;
}

[[nodiscard]] bool Interpreter::Impl::has_dynamic_waits(
    const SignalId signal) const noexcept
{
    return signal < dynamic_fanout_active_counts.size()
        && dynamic_fanout_active_counts[signal] != 0U;
}

template <typename Id, typename Value>
bool Interpreter::Impl::update_callable_register_context(
    ProcessState& process, const Id destination,
    std::vector<Id> ProcessState::CallableFrameState::*const ids,
    std::vector<Value> ProcessState::CallableFrameState::*const values,
    const Value& value)
{
    const auto update_context = [&](
        ProcessState::CallableFrameState& context) {
        const auto& context_ids = context.*ids;
        const auto found = std::ranges::find(context_ids, destination);
        if (found == context_ids.end()) {
            return false;
        }
        (context.*values)[static_cast<std::size_t>(
            std::distance(context_ids.begin(), found))] = value;
        return true;
    };
    const auto update_escaping_contexts = [&] {
        for (auto context
            = process.cold().escaping_callable_contexts.rbegin();
            context != process.cold().escaping_callable_contexts.rend();
            ++context) {
            if (!*context) {
                fail(process, "escaping automatic callable context is null");
            }
            if (update_context(**context)) {
                return true;
            }
        }
        return false;
    };
    if (process.cold().callable_registers_suspended) {
        return (process.cold().suspended_callable_context
                   && update_context(
                       *process.cold().suspended_callable_context))
            || update_escaping_contexts();
    }
    const auto& frames = process.cold().callable_frames;
    for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame) {
        const auto& frame_ids = (*frame).*ids;
        if (std::ranges::find(frame_ids, destination) != frame_ids.end()) {
            if (frame->escaping_context) {
                (void)update_context(*frame->escaping_context);
            }
            return false;
        }
    }
    (void)update_escaping_contexts();
    return false;
}

void Interpreter::Impl::write_process_register(
    Interpreter::Impl::ProcessState& process,
    const RegisterId destination,
    const PackedLogic4& value)
{
    const auto converted = coerce_value_kind(
        value, register_value_kind(process, destination));
    if (update_callable_register_context(process, destination,
            &ProcessState::CallableFrameState::packed_ids,
            &ProcessState::CallableFrameState::packed, converted)) {
        return;
    }
    if (process.executor) {
        process.executor->write_register(
            destination, converted);
        return;
    }
    get_register(process, destination) = converted;
}

void Interpreter::Impl::write_process_string_register(
    Interpreter::Impl::ProcessState& process,
    const StringRegisterId destination,
    const std::string_view value)
{
    if (value.size() > maximum_string_bytes) {
        fail(process, "string mailbox value exceeds the byte limit");
    }
    (void)systemverilog_string_length(value);
    const std::string copied { value };
    if (update_callable_register_context(process, destination,
            &ProcessState::CallableFrameState::string_ids,
            &ProcessState::CallableFrameState::strings, copied)) {
        return;
    }
    if (process.executor) {
        process.executor->write_string_register(destination, copied);
        return;
    }
    get_string_register(process, destination) = copied;
}

void Interpreter::Impl::clear_wait_timeout_nonempty(ProcessState& process)
{
    auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr) {
        process.wait_timeout_origin.reset();
        return;
    }
    if (dynamic_wait->wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "wait timeout generation overflow");
    }
    ++dynamic_wait->wait_timeout_generation;
    process.wait_timeout_origin.reset();
    dynamic_wait->wait_timeout_deadline.reset();
    dynamic_wait->wait_timeout_result.reset();
}

void Interpreter::Impl::set_wait_timeout_result(
    Interpreter::Impl::ProcessState& process,
    const bool timed_out)
{
    auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr || !dynamic_wait->wait_timeout_result) {
        return;
    }
    write_process_register(
        process,
        *dynamic_wait->wait_timeout_result,
        PackedLogic4::from_msb_string(
            timed_out ? "1" : "0"));
}

void Interpreter::Impl::begin_wait_timeout(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex origin,
    const SimulationTick delay,
    const std::optional<RegisterId> result)
{
    clear_wait_timeout(process);
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
        process.pc = origin;
        fail(process, "simulation time overflow in WaitOn timeout");
    }
    auto& dynamic_wait = process.cold().ensure_dynamic_wait_state();
    if (dynamic_wait.wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        process.pc = origin;
        fail(process, "wait timeout generation overflow");
    }
    const auto generation = ++dynamic_wait.wait_timeout_generation;
    const auto deadline = scheduler.now() + delay;
    process.wait_timeout_origin = origin;
    dynamic_wait.wait_timeout_deadline = deadline;
    dynamic_wait.wait_timeout_result = result;
    set_wait_timeout_result(process, false);

    struct WaitTimeoutTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
        InstructionIndex origin { };
        std::uint64_t generation { };
        bool after_updates { };
    };
    // IEEE 1076-2008 14.7.5.1: a VHDL process whose timeout expires resumes
    // after the signal updates of that time, as queue_timeout does.
    const bool after_updates
        = process.program().scheduling_domain()
            != ProcessSchedulingDomain::systemverilog
        && !process.program().postponed() && delay != 0;
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            WaitTimeoutTask,
            +[](Scheduler&, const WaitTimeoutTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                auto* const timeout
                    = state.cold().dynamic_wait_state_if_present();
                if (timeout == nullptr
                    || timeout->wait_timeout_generation
                        != scheduled.generation
                    || state.wait_timeout_origin
                        != std::optional { scheduled.origin }) {
                    return;
                }
                scheduled.owner->set_wait_timeout_result(state, true);
                state.wait_timeout_origin.reset();
                timeout->wait_timeout_deadline.reset();
                timeout->wait_timeout_result.reset();
                if (scheduled.after_updates) {
                    scheduled.owner->queue_next_delta(scheduled.process);
                } else {
                    scheduled.owner->queue_active_current(
                        scheduled.process);
                }
            }>(WaitTimeoutTask {
                this, process.id, origin, generation, after_updates });
    const bool systemverilog_process
        = process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog;
    if (delay == 0) {
        if (systemverilog_process) {
            scheduler.schedule_internal_systemverilog(
                SchedulerPhase::inactive, process.id, task);
        } else {
            scheduler.schedule_internal(
                SchedulerPhase::inactive, process.id, task);
        }
    } else if (systemverilog_process) {
        scheduler.schedule_internal_systemverilog_at(
            deadline, SchedulerPhase::active, process.id, task);
    } else {
        scheduler.schedule_internal_at(deadline,
            after_updates ? SchedulerPhase::update : SchedulerPhase::active,
            process.id, task);
    }
}

void Interpreter::Impl::rearm_wait_timeout(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex origin,
    const std::optional<RegisterId> result)
{
    const auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (process.wait_timeout_origin
            != std::optional { origin }
        || dynamic_wait == nullptr
        || !dynamic_wait->wait_timeout_deadline) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout rearm has no matching active deadline");
    }
    if (dynamic_wait->wait_timeout_result != result) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout rearm result register mismatch");
    }
    if (*dynamic_wait->wait_timeout_deadline < scheduler.now()) {
        process.pc = instruction;
        fail(process, "WaitOn timeout deadline was missed");
    }
}

void Interpreter::Impl::mark_dynamic_event_resume(
    Interpreter::Impl::ProcessState& process)
{
    const auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    const auto timed_out = dynamic_wait != nullptr
        && dynamic_wait->wait_timeout_deadline
        && *dynamic_wait->wait_timeout_deadline <= scheduler.now();
    set_wait_timeout_result(process, timed_out);
}

[[nodiscard]] bool Interpreter::Impl::dynamic_wait_satisfied(
    Interpreter::Impl::ProcessState& process,
    const SignalId signal,
    const DynamicWaitRegistration& registration)
{
    if (!dynamic_wait_registration_is_current(
            process, registration, signal)) {
        return false;
    }
    auto* const dynamic_wait
        = process.cold().dynamic_wait_state_if_present();
    if (dynamic_wait == nullptr) {
        return false;
    }
    if (dynamic_wait->wait_order_result) {
        if (dynamic_wait->wait_order_index
            >= dynamic_wait->wait_order_events.size()) {
            throw std::logic_error {
                "wait_order state has no expected event"
            };
        }
        if (signal
            == dynamic_wait->wait_order_events[
                dynamic_wait->wait_order_index]) {
            ++dynamic_wait->wait_order_index;
            if (dynamic_wait->wait_order_index
                != dynamic_wait->wait_order_events.size()) {
                return false;
            }
            write_process_register(
                process,
                *dynamic_wait->wait_order_result,
                PackedLogic4::from_aval_bval(1, 1, 0));
            return true;
        }
        write_process_register(
            process,
            *dynamic_wait->wait_order_result,
            PackedLogic4::from_aval_bval(1, 0, 0));
        return true;
    }
    if (!dynamic_wait->dynamic_wait_all) {
        return true;
    }
    if (registration.sensitivity_index
        >= dynamic_wait->dynamic_triggered.size()) {
        throw std::logic_error {
            "dynamic wait-all state has no matching sensitivity"
        };
    }
    dynamic_wait->dynamic_triggered[registration.sensitivity_index] = true;
    return std::all_of(
        dynamic_wait->dynamic_triggered.begin(),
        dynamic_wait->dynamic_triggered.end(),
        [](const bool triggered) { return triggered; });
}

void Interpreter::Impl::register_static_sensitivity_cohort(
    const ProcessId id)
{
    const auto process = processes.program_view(id);
    static_sensitivity_cohort_by_process.resize(
        processes.size(), std::numeric_limits<std::size_t>::max());
    if (process.static_sensitivity().empty()) {
        return;
    }

    auto sensitivity = process.static_sensitivity();
    normalize_sensitivities(sensitivity);

    std::string key;
    key += process.postponed() ? 'p'
        : process.reactive() ? 'r'
        : process.observed() ? 'o'
                           : 'a';
    key += process.scheduling_domain()
            == ProcessSchedulingDomain::systemverilog
        ? 's'
        : 'g';
    for (const auto& entry : sensitivity) {
        key += std::to_string(static_cast<std::size_t>(entry.signal));
        key += ':';
        key += std::to_string(static_cast<std::underlying_type_t<EdgeKind>>(
            entry.edge));
        key += ':';
        key += std::to_string(entry.offset);
        key += ':';
        key += std::to_string(entry.width);
        key += ';';
    }
    const auto [found, inserted]
        = static_sensitivity_cohort_by_key.try_emplace(
            std::move(key), static_sensitivity_cohorts.size());
    if (inserted) {
        static_sensitivity_cohorts.emplace_back();
    }
    const auto cohort = found->second;
    static_sensitivity_cohorts[cohort].members.push_back(id);
    static_sensitivity_cohort_by_process[id] = cohort;
}

void Interpreter::Impl::rebuild_static_fanout()
{
    if (!static_fanout_dirty) {
        return;
    }

    constexpr auto category_count = std::size_t { 4U };
    using CategoryCounts = std::array<std::size_t, category_count>;
    std::vector<std::size_t> counts(signals.size(), 0U);
    std::vector<CategoryCounts> category_counts(
        signals.size(), CategoryCounts { });
    // Inert static kernel members have no static sensitivity.
    const auto inert = [&](const std::size_t process) {
        return static_kernel && process < fusion_dormant_process.size()
            && fusion_dormant_process[process] == 1U;
    };
    for (std::size_t process_index = 0;
         process_index < processes.size(); ++process_index) {
        if (inert(process_index)) {
            continue;
        }
        for (const auto& sensitivity :
             processes.program_view(
                 static_cast<ProcessId>(process_index)).static_sensitivity()) {
            if (sensitivity.signal >= counts.size()) {
                throw std::logic_error {
                    "static sensitivity references a missing signal"
                };
            }
            const auto category = static_cast<std::size_t>(sensitivity.edge);
            if (category >= category_count) {
                throw std::logic_error {
                    "static sensitivity has an invalid edge kind"
                };
            }
            auto& count = counts[sensitivity.signal];
            if (count == std::numeric_limits<std::size_t>::max()) {
                throw std::length_error {
                    "static sensitivity fanout is too large"
                };
            }
            ++count;
            auto& category_count_for_signal
                = category_counts[sensitivity.signal][category];
            if (category_count_for_signal
                == std::numeric_limits<std::size_t>::max()) {
                throw std::length_error {
                    "static sensitivity fanout is too large"
                };
            }
            ++category_count_for_signal;
        }
    }

    std::vector<std::size_t> offsets(signals.size() + 1U, 0U);
    for (std::size_t signal = 0; signal < counts.size(); ++signal) {
        const auto previous = offsets[signal];
        if (counts[signal]
            > std::numeric_limits<std::size_t>::max() - previous) {
            throw std::length_error {
                "static sensitivity fanout is too large"
            };
        }
        offsets[signal + 1U] = previous + counts[signal];
    }

    std::vector<std::array<FanoutSpan, category_count>> category_spans(
        signals.size());
    std::size_t category_entry_count { };
    for (std::size_t signal = 0; signal < category_counts.size(); ++signal) {
        for (std::size_t category = 0; category < category_count;
             ++category) {
            auto& span = category_spans[signal][category];
            span.begin = category_entry_count;
            span.count = category_counts[signal][category];
            if (span.count
                > std::numeric_limits<std::size_t>::max()
                    - category_entry_count) {
                throw std::length_error {
                    "static sensitivity fanout is too large"
                };
            }
            category_entry_count += span.count;
        }
    }

    std::vector<Fanout> entries(offsets.back());
    std::vector<std::size_t> next(offsets.begin(), offsets.end() - 1);
    std::vector<std::size_t> category_entries(category_entry_count);
    std::vector<CategoryCounts> category_next(signals.size());
    for (std::size_t signal = 0; signal < category_spans.size(); ++signal) {
        for (std::size_t category = 0; category < category_count;
             ++category) {
            category_next[signal][category]
                = category_spans[signal][category].begin;
        }
    }
    for (std::size_t process_index = 0;
         process_index < processes.size(); ++process_index) {
        if (inert(process_index)) {
            continue;
        }
        const auto process = processes.program_view(
            static_cast<ProcessId>(process_index));
        const auto id = static_cast<ProcessId>(process_index);
        for (std::size_t sensitivity_index = 0;
             sensitivity_index < process.static_sensitivity().size();
             ++sensitivity_index) {
            const auto& sensitivity
                = process.static_sensitivity()[sensitivity_index];
            const auto trigger_mask
                = sensitivity_index < 63U
                    && !process.static_trigger_regions().empty()
                ? UINT64_C(1) << sensitivity_index
                : Process::full_static_trigger_mask;
            const auto category = static_cast<std::size_t>(sensitivity.edge);
            const auto entry_index = next[sensitivity.signal]++;
            entries[entry_index] = {
                id, sensitivity.edge, trigger_mask,
                sensitivity.offset, sensitivity.width
            };
            category_entries[
                category_next[sensitivity.signal][category]++] = entry_index;
        }
    }

    static_fanout_entries.swap(entries);
    static_fanout_offsets.swap(offsets);
    static_fanout_category_entries.swap(category_entries);
    static_fanout_category_spans.swap(category_spans);
    static_fanout_dirty = false;
}

std::span<const Interpreter::Impl::Fanout>
Interpreter::Impl::static_fanout_for(const SignalId signal) const noexcept
{
    const auto index = static_cast<std::size_t>(signal);
    if (static_fanout_offsets.empty()
        || index >= static_fanout_offsets.size() - 1U) {
        return { };
    }
    const auto begin = static_fanout_offsets[index];
    const auto count = static_fanout_offsets[index + 1U] - begin;
    return std::span<const Fanout> { static_fanout_entries }
        .subspan(begin, count);
}

std::span<const std::size_t>
Interpreter::Impl::static_fanout_indices_for(
    const SignalId signal,
    const EdgeKind edge) const noexcept
{
    const auto index = static_cast<std::size_t>(signal);
    const auto category = static_cast<std::size_t>(edge);
    if (index >= static_fanout_category_spans.size()
        || category >= static_fanout_category_spans[index].size()) {
        return { };
    }
    const auto span = static_fanout_category_spans[index][category];
    return std::span<const std::size_t> { static_fanout_category_entries }
        .subspan(span.begin, span.count);
}

Interpreter::Impl::StaticTransitionMatches
Interpreter::Impl::decode_static_transition(
    const Logic4 old_value,
    const Logic4 new_value) noexcept
{
    return {
        edge_matches(EdgeKind::posedge, old_value, new_value),
        edge_matches(EdgeKind::negedge, old_value, new_value),
    };
}

void Interpreter::Impl::notify_static_value_change(
    const SignalId signal_id,
    const StaticTransitionMatches transition,
    const bool count_native_word_profile,
    const SignalChangeOrigin origin)
{
    const bool group_static_fanout = fanout_cohort_grouping_enabled;
    const auto visit = next_static_fanout_visit();
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    // Grouped fanout compares the packed previous/current values below. A
    // deferred direct publication can change a boundary signal, so
    // materialize it before inspecting the packed values.
    materialize_direct_signal(signal_id);
    const auto visit_category = [&](const EdgeKind edge) {
        for (const auto entry_index :
             static_fanout_indices_for(signal_id, edge)) {
            const auto& sensitivity = static_fanout_entries[entry_index];
            if (sensitivity.width != 0U) {
                // Native word publication may leave packed storage deferred.
                // Materialize current and previous values before inspecting a
                // retained aggregate range; no stale packed copy is authoritative.
                materialize_direct_signal(signal_id);
                if (!sensitivity_range_changed(signal_last_values[signal_id],
                        signals[signal_id].initial_value,
                        sensitivity.offset, sensitivity.width)) {
                    continue;
                }
            }
            auto& triggered_process = get_process(sensitivity.process);
            triggered_process.static_trigger_mask
                |= sensitivity.static_trigger_mask;

            if (native_process_count_profile_enabled) {
                if (count_native_word_profile) {
                    ++native_process_word_fanout_matches;
                }
                if (triggered_process.waiting_on_static) {
                    if (count_native_word_profile) {
                        ++native_process_word_fanout_ready;
                        if (sensitivity.process
                            >= native_process_word_fanout_ready_counts.size()) {
                            native_process_word_fanout_ready_counts.resize(
                                static_cast<std::size_t>(sensitivity.process)
                                + 1U);
                            native_process_static_trigger_counts.resize(
                                static_cast<std::size_t>(sensitivity.process)
                                + 1U);
                        }
                    } else if (sensitivity.process
                        >= native_process_static_trigger_counts.size()) {
                        native_process_static_trigger_counts.resize(
                            static_cast<std::size_t>(sensitivity.process)
                            + 1U);
                    }
                    ++native_process_static_trigger_counts[
                        sensitivity.process][signal_id];
                }
            }

            const auto cohort
                = sensitivity.process < static_sensitivity_cohort_by_process.size()
                ? static_sensitivity_cohort_by_process[sensitivity.process]
                : no_cohort;
            if (group_static_fanout && cohort != no_cohort
                && static_sensitivity_cohorts[cohort].members.size() >= 2U) {
                auto& grouped = static_sensitivity_cohorts[cohort];
                if (grouped.fanout_visit != visit) {
                    grouped.fanout_visit = visit;
                    if (origin.process_domain
                            == ProcessSchedulingDomain::generic
                        && triggered_process.program().scheduling_domain()
                            == ProcessSchedulingDomain::generic) {
                        queue_static_cohort_next_delta(cohort);
                    } else {
                        for (const auto member : grouped.members) {
                            auto& candidate = get_process(member);
                            if (candidate.waiting_on_static) {
                                queue_static_next_delta(member, origin);
                            }
                        }
                    }
                }
                continue;
            }

            if (triggered_process.waiting_on_static) {
                queue_static_next_delta(sensitivity.process, origin);
            }
        }
    };

    visit_category(EdgeKind::any);
    if (transition.posedge) {
        visit_category(EdgeKind::posedge);
    }
    if (transition.negedge) {
        visit_category(EdgeKind::negedge);
    }
}

void Interpreter::Impl::queue_at(
    ProcessId id, SimulationTick time, const bool initial)
{
    // Within a time step processes run in stable order. An always_comb or
    // always_latch starts after every other time-zero procedure has started
    // (IEEE 1800-2017 9.2.2.2.2), so its first run orders after them all.
    constexpr StableOrder after_procedures_order { StableOrder { 1U } << 40U };
    const StableOrder order = initial
            && processes.program_view(id).startup_phase()
                == ProcessStartupPhase::after_procedures
        ? after_procedures_order + id
        : StableOrder { id };
    if (static_kernel_hosts(id)) {
        const auto program = processes.program_view(id);
        if (initial && program.postponed()
            && program.scheduling_domain()
                != ProcessSchedulingDomain::systemverilog) {
            // A postponed process makes its initialization run with the
            // others (IEEE 1076-2019 14.7.5.2): in the Active work of the
            // first round, not in the postponed region.
            static_kernel_enqueue_hosted(id, StaticKernelHostedWake::current);
            return;
        }
        static_kernel_enqueue_hosted(
            id, StaticKernelHostedWake::at, time, order);
        return;
    }
    if (auto* const compact = processes.compact_constant(id)) {
        if (compact->halted || compact->queued) {
            return;
        }
        compact->queued = true;
        const auto phase = compact->execution_phase;
        struct ProcessQueueTask {
            Interpreter::Impl* owner { };
            ProcessId process { };
        };
        const auto task =
            fsim::runtime::detail::make_scheduler_task_descriptor<
                ProcessQueueTask,
                +[](Scheduler&, const ProcessQueueTask& scheduled) {
                    if (auto* state = scheduled.owner->processes
                            .compact_constant(scheduled.process)) {
                        state->queued = false;
                        state->waiting_on_static = false;
                    } else {
                        auto& full_state = scheduled.owner->get_process(
                            scheduled.process);
                        full_state.queued = false;
                        full_state.waiting_on_static = false;
                        scheduled.owner->remove_dynamic_wait(full_state);
                    }
                    scheduled.owner->execute(scheduled.process);
                }>(ProcessQueueTask { this, id });
        if (processes.program_view(id).scheduling_domain()
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_internal_systemverilog_at(
                time, phase, order, task);
        } else {
            // A postponed process makes its initialization run with the
            // others (IEEE 1076-2008 14.7.5.2).
            scheduler.schedule_internal_at(time,
                initial && phase == SchedulerPhase::postponed
                    ? SchedulerPhase::active
                    : phase,
                order, task);
        }
        return;
    }
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    process.timer_wake = time > scheduler.now();
    const auto phase = process.execution_phase;
    struct ProcessQueueTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
        std::uint32_t epoch { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessQueueTask,
            +[](Scheduler&, const ProcessQueueTask& scheduled) {
                if (auto* compact = scheduled.owner->processes
                        .compact_constant(scheduled.process)) {
                    compact->queued = false;
                    compact->waiting_on_static = false;
                } else {
                    auto& state = scheduled.owner->get_process(
                        scheduled.process);
                    // A disable abandoned this timer (IEEE 1800-2017 9.6.2).
                    if (state.timer_epoch != scheduled.epoch) {
                        return;
                    }
                    state.queued = false;
                    state.timer_wake = false;
                    state.waiting_on_static = false;
                    scheduled.owner->remove_dynamic_wait(state);
                }
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id, process.timer_epoch });
    if (process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_internal_systemverilog_at(
            time, phase, order, task);
    } else {
        scheduler.schedule_internal_at(time,
            initial && phase == SchedulerPhase::postponed
                ? SchedulerPhase::active
                : phase,
            order, task);
    }
}

void Interpreter::Impl::queue_timeout(
    ProcessState& process, const SimulationTick time)
{
    // IEEE 1076-2008 14.7.5.3: in a simulation cycle every active signal is
    // updated before the processes that resume in it run, including those
    // whose timeout expires. Queue the resumption from the update phase so
    // it joins the processes the updates wake.
    if (process.program().scheduling_domain()
            == ProcessSchedulingDomain::systemverilog
        || process.program().postponed() || process.halted
        || process.queued || time <= scheduler.now()) {
        queue_at(process.id, time);
        return;
    }
    process.queued = true;
    process.timer_wake = true;
    scheduler.schedule_after(time - scheduler.now(), SchedulerPhase::update,
        process.id,
        [this, id = process.id, epoch = process.timer_epoch](Scheduler&) {
            auto& state = get_process(id);
            if (state.timer_epoch != epoch) {
                return;
            }
            state.queued = false;
            state.timer_wake = false;
            queue_next_delta(id);
        });
}

void Interpreter::Impl::queue_next_delta(
    const ProcessId id,
    const SignalChangeOrigin origin)
{
    if (static_kernel_hosts(id)) {
        // The host scheduler runs a SystemVerilog process's same-domain
        // next-delta wake in the next round of the Active region and any
        // other wake, including every wake of a VHDL (generic domain)
        // process, in the next delta (IEEE 1076-2019 14.7.5.3).
        static_kernel_enqueue_hosted(id,
            origin.process_domain == ProcessSchedulingDomain::systemverilog
                    && processes.scheduling_domain(id)
                        == ProcessSchedulingDomain::systemverilog
                ? StaticKernelHostedWake::active
                : StaticKernelHostedWake::next_delta);
        return;
    }
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    process.timer_wake = false;
    const auto phase = process.execution_phase;
    struct ProcessQueueTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessQueueTask,
            +[](Scheduler&, const ProcessQueueTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                state.queued = false;
                state.waiting_on_static = false;
                scheduled.owner->remove_dynamic_wait(state);
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id });
    if (processes.scheduling_domain(id)
        == ProcessSchedulingDomain::systemverilog) {
        if (origin.process_domain
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_internal_systemverilog_next_delta(
                phase, id, task);
        } else {
            scheduler.schedule_next_delta(
                SchedulerPhase::active, id,
                [this, id, phase](Scheduler& runtime) {
                    auto& state = get_process(id);
                    state.queued = true;
                    runtime.schedule_systemverilog(
                        phase, id,
                        [this, id](Scheduler&) {
                            auto& state = get_process(id);
                            state.queued = false;
                            state.waiting_on_static = false;
                            remove_dynamic_wait(state);
                            execute(id);
                        });
                });
        }
        return;
    }
    scheduler.schedule_internal_next_delta(phase, id, task);
}

void Interpreter::Impl::queue_zero_delay_resume(
    ProcessState& process)
{
    // A SystemVerilog #0 resumes in the Inactive region; a VHDL
    // `wait for 0 ns` (generic domain) resumes in the next delta cycle and
    // takes the queue_next_delta path below (IEEE 1076-2019 10.2).
    if (static_kernel_hosts(process.id)
        && process.program().scheduling_domain()
            == ProcessSchedulingDomain::systemverilog
        && !process.program().postponed()) {
        static_kernel_enqueue_hosted(
            process.id, StaticKernelHostedWake::inactive);
        return;
    }
    if (process.halted || process.queued) {
        return;
    }
    if (process.program().postponed()
        || process.program().scheduling_domain()
            != ProcessSchedulingDomain::systemverilog) {
        queue_next_delta(process.id);
        return;
    }

    process.queued = true;
    struct ProcessResumeTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessResumeTask,
            +[](Scheduler&, const ProcessResumeTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                state.queued = false;
                state.waiting_on_static = false;
                scheduled.owner->remove_dynamic_wait(state);
                scheduled.owner->execute(scheduled.process);
            }>(ProcessResumeTask { this, process.id });
    scheduler.schedule_internal_systemverilog(
        SchedulerPhase::inactive, process.id, task);
}

void Interpreter::Impl::queue_static_next_delta(
    const ProcessId id,
    const SignalChangeOrigin origin)
{
    if (static_kernel_hosts(id)) {
        queue_next_delta(id, origin);
        return;
    }
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    const auto scheduling_domain
        = processes.scheduling_origin(id).process_domain;
    if (scheduling_domain != ProcessSchedulingDomain::generic
        || origin.process_domain
            != ProcessSchedulingDomain::generic) {
        queue_next_delta(id, origin);
        return;
    }
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    const auto cohort = id < static_sensitivity_cohort_by_process.size()
        ? static_sensitivity_cohort_by_process[id]
        : no_cohort;
    if (cohort == no_cohort
        || static_sensitivity_cohorts[cohort].members.size() < 2U) {
        queue_next_delta(id, origin);
        return;
    }
    queue_static_cohort_next_delta(cohort);
}

void Interpreter::Impl::queue_static_cohort_next_delta(
    const std::size_t cohort_id)
{
    auto& cohort = static_sensitivity_cohorts.at(cohort_id);
    // The kernel schedules a hosted process, so a cohort task must not run
    // it. The cohort fanout stands for one wake of every member waiting on
    // the signal, so a hosted member is woken here on its own, as a
    // generic-domain next-delta wake (the callers only group generic wakes of
    // generic processes; IEEE 1076-2019 14.7.5.3). This is independent of a
    // task already pending for the other members.
    for (const auto member : cohort.members) {
        if (!static_kernel_hosts(member)) {
            continue;
        }
        const auto& candidate = get_process(member);
        if (!candidate.halted && !candidate.queued
            && candidate.waiting_on_static) {
            queue_next_delta(member, SignalChangeOrigin { });
        }
    }
    if (cohort.pending) {
        return;
    }

    cohort.ready.clear();
    cohort.ready.reserve(cohort.members.size());
    for (const auto member : cohort.members) {
        auto& candidate = get_process(member);
        if (candidate.halted || candidate.queued
            || !candidate.waiting_on_static || static_kernel_hosts(member)) {
            continue;
        }
        cohort.ready.push_back(member);
    }
    if (cohort.ready.empty()) {
        return;
    }
    const auto& process = get_process(cohort.ready.front());
    const auto phase = process.execution_phase;
    const auto order = cohort.ready.front();
    const auto snapshot = cohort_snapshots.acquire(cohort.ready);
    const auto execute_one = [this, cohort_id](Scheduler&) {
        execute_queued_static_cohort(cohort_id);
    };
    try {
        if (static_phase_batches_enabled) {
            static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
            scheduler.schedule_next_delta_batchable(
                phase, order, *this,
                static_cast<std::uint64_t>(cohort_id), execute_one);
        } else {
            scheduler.schedule_next_delta(phase, order, execute_one);
        }
    } catch (...) {
        cohort_snapshots.release(snapshot);
        cohort.ready.clear();
        throw;
    }
    for (const auto member : cohort.ready) {
        get_process(member).queued = true;
    }
    cohort.pending = snapshot;
    cohort.ready.clear();
}

void Interpreter::Impl::execute_queued_static_cohort(
    const std::size_t cohort_id)
{
    auto& cohort = static_sensitivity_cohorts.at(cohort_id);
    const auto snapshot = std::exchange(
        cohort.pending, CohortSnapshotPool::Token { });
    if (!cohort_snapshots.consume(snapshot, [this](
            const std::span<const ProcessId> ready) {
            execute_static_cohort(ready);
        })) {
        throw std::logic_error {
            "scheduler batch references an empty static cohort"
        };
    }
}

std::uint64_t Interpreter::Impl::next_static_fanout_visit()
{
    ++static_fanout_visit_generation;
    if (static_fanout_visit_generation != 0U) {
        return static_fanout_visit_generation;
    }
    static_fanout_visit_generation = 1U;
    for (auto& cohort : static_sensitivity_cohorts) {
        cohort.fanout_visit = 0U;
    }
    return static_fanout_visit_generation;
}

void Interpreter::Impl::queue_current(ProcessId id)
{
    if (static_kernel_hosts(id)) {
        static_kernel_enqueue_hosted(id, StaticKernelHostedWake::current);
        return;
    }
    if (auto* const compact = processes.compact_constant(id)) {
        if (compact->halted || compact->queued) {
            return;
        }
        compact->queued = true;
        const auto phase
            = scheduler.current_phase().value_or(SchedulerPhase::active);
        struct ProcessQueueTask {
            Interpreter::Impl* owner { };
            ProcessId process { };
        };
        const auto task =
            fsim::runtime::detail::make_scheduler_task_descriptor<
                ProcessQueueTask,
                +[](Scheduler&, const ProcessQueueTask& scheduled) {
                    if (auto* state = scheduled.owner->processes
                            .compact_constant(scheduled.process)) {
                        state->queued = false;
                    } else {
                        scheduled.owner->get_process(
                            scheduled.process).queued = false;
                    }
                    scheduled.owner->execute(scheduled.process);
                }>(ProcessQueueTask { this, id });
        if (processes.program_view(id).scheduling_domain()
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_internal_systemverilog(phase, id, task);
        } else {
            scheduler.schedule_internal(phase, id, task);
        }
        return;
    }
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    const auto phase = scheduler.current_phase().value_or(SchedulerPhase::active);
    struct ProcessQueueTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessQueueTask,
            +[](Scheduler&, const ProcessQueueTask& scheduled) {
                if (auto* compact = scheduled.owner->processes
                        .compact_constant(scheduled.process)) {
                    compact->queued = false;
                } else {
                    scheduled.owner->get_process(
                        scheduled.process).queued = false;
                }
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id });
    if (process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_internal_systemverilog(phase, id, task);
    } else {
        scheduler.schedule_internal(phase, id, task);
    }
}

void Interpreter::Impl::queue_active_current(ProcessId id)
{
    if (static_kernel_hosts(id)) {
        static_kernel_enqueue_hosted(id, StaticKernelHostedWake::active);
        return;
    }
    queue_active_current_at(id, id);
}

void Interpreter::Impl::queue_active_current_at(
    const ProcessId id, const StableOrder order)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
    process.timer_wake = false;
    const auto phase = process.execution_phase;
    struct ProcessQueueTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            ProcessQueueTask,
            +[](Scheduler&, const ProcessQueueTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                state.queued = false;
                state.waiting_on_static = false;
                scheduled.owner->remove_dynamic_wait(state);
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id });
    if (process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_internal_systemverilog(phase, order, task);
    } else {
        scheduler.schedule_internal(phase, order, task);
    }
}

void Interpreter::Impl::queue_static_active_current(const ProcessId id)
{
    if (static_kernel_hosts(id)) {
        static_kernel_enqueue_hosted(id, StaticKernelHostedWake::active);
        return;
    }
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    if (process.program().scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        queue_active_current(id);
        return;
    }
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    const auto cohort = id < static_sensitivity_cohort_by_process.size()
        ? static_sensitivity_cohort_by_process[id]
        : no_cohort;
    if (cohort == no_cohort
        || static_sensitivity_cohorts[cohort].members.size() < 2U) {
        queue_active_current(id);
        return;
    }

    active_cohort_ready.clear();
    active_cohort_ready.reserve(
        static_sensitivity_cohorts[cohort].members.size());
    for (const auto member : static_sensitivity_cohorts[cohort].members) {
        auto& candidate = get_process(member);
        if (candidate.halted || candidate.queued
            || !candidate.waiting_on_static) {
            continue;
        }
        if (static_kernel_hosts(member)) {
            // The kernel runs a hosted member, not the cohort task.
            static_kernel_enqueue_hosted(
                member, StaticKernelHostedWake::active);
            continue;
        }
        active_cohort_ready.push_back(member);
    }
    if (active_cohort_ready.empty()) {
        return;
    }
    const auto phase = process.execution_phase;
    const auto order = active_cohort_ready.front();
    const auto snapshot = cohort_snapshots.acquire(active_cohort_ready);
    try {
        scheduler.schedule(phase, order, [this, snapshot](Scheduler&) {
            if (!cohort_snapshots.consume(snapshot, [this](
                    const std::span<const ProcessId> ready) {
                    execute_static_cohort(ready);
                })) {
                throw std::logic_error {
                    "scheduler references an empty static cohort"
                };
            }
        });
    } catch (...) {
        cohort_snapshots.release(snapshot);
        active_cohort_ready.clear();
        throw;
    }
    for (const auto member : active_cohort_ready) {
        get_process(member).queued = true;
    }
    active_cohort_ready.clear();
}

void Interpreter::Impl::set_event_identity(
    const SignalId signal,
    const std::optional<SignalId> identity)
{
    if (!get_signal(signal).event_variable) {
        throw std::logic_error {
            "named-event identity can only be assigned to event variables"
        };
    }
    if (identity && !get_signal(*identity).event_variable) {
        throw std::logic_error {
            "named-event identity must reference an event variable"
        };
    }
    auto& current_identity = event_identities.at(signal);
    const auto previous = current_identity;
    if (previous == identity) {
        return;
    }
    if (previous) {
        const auto& previous_members = event_identity_members.at(*previous);
        const auto member = std::lower_bound(
            previous_members.begin(), previous_members.end(), signal);
        if (member == previous_members.end() || *member != signal) {
            throw std::logic_error {
                "named-event identity index is missing its current member"
            };
        }
    }
    if (identity) {
        auto& next_members = event_identity_members.at(*identity);
        const auto member = std::lower_bound(
            next_members.begin(), next_members.end(), signal);
        if (member == next_members.end() || *member != signal) {
            next_members.insert(member, signal);
        }
    }
    if (previous) {
        auto& previous_members = event_identity_members.at(*previous);
        const auto member = std::lower_bound(
            previous_members.begin(), previous_members.end(), signal);
        previous_members.erase(member);
    }
    current_identity = identity;
}

bool Interpreter::Impl::event_triggered_now(const SignalId event) const
{
    const auto& signal = get_signal(event);
    const auto identity = signal.event_variable
        ? event_identities.at(event)
        : std::optional<SignalId> { event };
    const auto& stamp = identity
        ? signal_events[*identity]
        : std::optional<std::pair<SimulationTick, std::uint64_t>> { };
    return stamp && stamp->first == scheduler.now();
}

void Interpreter::Impl::trigger_event(
    const SignalId event,
    const SignalChangeOrigin origin)
{
    const auto& source = get_signal(event);
    if (event_trigger_hook) {
        event_trigger_hook(event, scheduler.now());
    }
    const auto update_member = [&](const SignalId member) {
        record_signal_event(member, scheduler.delta(), origin);
        note_event_trigger(member);
        for (const auto& sensitivity : static_fanout_for(member)) {
            auto& process = get_process(sensitivity.process);
            process.static_trigger_mask |= sensitivity.static_trigger_mask;
            if (process.waiting_on_static) {
                if (process.program().scheduling_domain()
                    == origin.process_domain) {
                    queue_static_active_current(sensitivity.process);
                } else {
                    queue_static_next_delta(sensitivity.process, origin);
                }
            }
        }
    };
    if (source.event_variable) {
        for (const auto member : event_identity_members.at(event)) {
            update_member(member);
        }
    } else {
        update_member(event);
    }
    // Copy because queue_active_current removes dynamic registrations.
    const auto dynamic = dynamic_fanout[event];
    for (const auto& registration : dynamic) {
        if (registration.process >= processes.size()) {
            continue;
        }
        auto& process = get_process(registration.process);
        if (dynamic_wait_satisfied(
                process, event, registration)) {
            mark_dynamic_event_resume(process);
            if (process.program().scheduling_domain()
                == origin.process_domain) {
                queue_active_current(registration.process);
            } else {
                queue_next_delta(registration.process, origin);
            }
        }
    }
}

void Interpreter::Impl::stamp_signal_event(
    const SignalId signal,
    const std::uint64_t generic_delta,
    const SignalChangeOrigin origin)
{
    update_selected_signal_value_history(signal, generic_delta);
    signal_events.at(signal)
        = std::pair { scheduler.now(), generic_delta };
    const auto systemverilog_round
        = origin.process_domain == ProcessSchedulingDomain::systemverilog
        ? scheduler.systemverilog_round()
        : std::uint64_t { };
    signal_event_scheduling_stamps.at(signal)
        = SignalEventSchedulingStamp { origin, systemverilog_round };
}

void Interpreter::Impl::record_signal_event(
    const SignalId signal,
    const std::uint64_t generic_delta,
    const SignalChangeOrigin origin)
{
    stamp_signal_event(signal, generic_delta, origin);
    capture_sampled_history_clock(signal, generic_delta, origin);
}

void Interpreter::Impl::register_signal_selection(
    const SignalId signal,
    const SignalSelection selection)
{
    if (signal >= signals.size() || signal >= signal_selection_histories.size()
        || selection.width == 0U
        || selection.offset > signals[signal].initial_value.width()
        || selection.width
            > signals[signal].initial_value.width() - selection.offset) {
        throw std::invalid_argument {
            "selected SimIR signal query has an invalid range"
        };
    }
    auto& histories = signal_selection_histories[signal];
    if (std::ranges::any_of(histories, [&](const auto& history) {
            return history.selection == selection;
        })) {
        return;
    }
    const auto current = logical_signal_value(signal).extract_bits(
        selection.offset, selection.width);
    histories.push_back(SignalSelectionHistory {
        .selection = selection,
        .last_value = current,
        .last_event = std::nullopt,
        .last_transaction = std::nullopt,
        .transaction_pending = false,
    });
}

const Interpreter::Impl::SignalSelectionHistory*
Interpreter::Impl::find_signal_selection_history(
    const SignalId signal,
    const SignalSelection selection) const noexcept
{
    if (signal >= signal_selection_histories.size()) {
        return nullptr;
    }
    const auto& histories = signal_selection_histories[signal];
    const auto found = std::ranges::find(
        histories, selection, &SignalSelectionHistory::selection);
    return found == histories.end() ? nullptr : &*found;
}

void Interpreter::Impl::stage_selected_signal_transaction(
    const SignalId signal,
    const std::size_t offset,
    const std::size_t width)
{
    if (signal >= signal_selection_histories.size()
        || signal_selection_histories[signal].empty()) {
        return;
    }
    if (width == 0U || offset > signals[signal].initial_value.width()
        || width > signals[signal].initial_value.width() - offset) {
        throw std::invalid_argument {
            "selected SimIR transaction has an invalid range"
        };
    }
    if (signal_selection_transaction_region_pending[signal] == 0U) {
        signal_selection_transaction_region_pending[signal] = 1U;
        if (signal_selection_transaction_pending_listed[signal] == 0U) {
            signal_selection_pending_transaction_signals.push_back(signal);
            signal_selection_transaction_pending_listed[signal] = 1U;
        }
    }
    const auto end = offset + width;
    for (auto& history : signal_selection_histories[signal]) {
        const auto selected_end = static_cast<std::size_t>(
            history.selection.offset + history.selection.width);
        history.transaction_pending = history.transaction_pending
            || (offset < selected_end && history.selection.offset < end);
    }
}

void Interpreter::Impl::stage_selected_signal_transaction_mask(
    const SignalId signal,
    const std::span<const std::uint64_t> mask_words)
{
    if (signal >= signal_selection_histories.size()
        || signal_selection_histories[signal].empty()) {
        return;
    }
    if (signal_selection_transaction_region_pending[signal] == 0U) {
        signal_selection_transaction_region_pending[signal] = 1U;
        if (signal_selection_transaction_pending_listed[signal] == 0U) {
            signal_selection_pending_transaction_signals.push_back(signal);
            signal_selection_transaction_pending_listed[signal] = 1U;
        }
    }
    for (auto& history : signal_selection_histories[signal]) {
        const auto end = static_cast<std::size_t>(
            history.selection.offset + history.selection.width);
        for (auto bit = static_cast<std::size_t>(history.selection.offset);
             bit < end; ++bit) {
            const auto word = bit / 64U;
            if (word < mask_words.size()
                && ((mask_words[word] >> (bit % 64U)) & UINT64_C(1)) != 0U) {
                history.transaction_pending = true;
                break;
            }
        }
    }
}

void Interpreter::Impl::note_selected_signal_transaction(
    const SignalId signal)
{
    if (signal >= signal_selection_histories.size()
        || signal_selection_histories[signal].empty()) {
        return;
    }
    auto& histories = signal_selection_histories[signal];
    if (signal_selection_transaction_region_pending[signal] == 0U) {
        for (auto& history : histories) {
            history.transaction_pending = true;
        }
    }
    const auto stamp = std::pair { scheduler.now(), scheduler.delta() + 1U };
    for (auto& history : histories) {
        if (history.transaction_pending) {
            history.last_transaction = stamp;
            history.transaction_pending = false;
        }
    }
    signal_selection_transaction_region_pending[signal] = 0U;
    if (signal_selection_transaction_pending_listed[signal] != 0U) {
        std::erase(signal_selection_pending_transaction_signals, signal);
        signal_selection_transaction_pending_listed[signal] = 0U;
    }
}

void Interpreter::Impl::clear_pending_selected_signal_transactions() noexcept
{
    for (const auto signal : signal_selection_pending_transaction_signals) {
        if (signal >= signal_selection_histories.size()) {
            continue;
        }
        for (auto& history : signal_selection_histories[signal]) {
            history.transaction_pending = false;
        }
        signal_selection_transaction_region_pending[signal] = 0U;
        signal_selection_transaction_pending_listed[signal] = 0U;
    }
    signal_selection_pending_transaction_signals.clear();
}

void Interpreter::Impl::update_selected_signal_value_history(
    const SignalId signal,
    const std::uint64_t generic_delta)
{
    if (signal >= signal_selection_histories.size()
        || signal_selection_histories[signal].empty()) {
        return;
    }
    const auto& previous = logical_signal_last_value(signal);
    const auto& current = logical_signal_value(signal);
    const auto stamp = std::pair { scheduler.now(), generic_delta };
    for (auto& history : signal_selection_histories[signal]) {
        const auto before = previous.extract_bits(
            history.selection.offset, history.selection.width);
        const auto after = current.extract_bits(
            history.selection.offset, history.selection.width);
        // An event on S updates the last value of each subelement that S's
        // update assigns, changed or not (IEEE 1076-2008 16.2.4).
        const bool updated = history.selection.composite_element
            && (history.transaction_pending
                || history.last_transaction == std::optional { stamp });
        if (before != after || updated) {
            history.last_value = before;
        }
        if (before != after) {
            history.last_event = stamp;
        }
    }
}

[[nodiscard]] std::uint64_t Interpreter::Impl::invalidate_event(
    const SignalId event)
{
    (void)get_signal(event);
    auto& state = event_states[event];
    if (state.generation
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error {
            "event notification generation overflow"
        };
    }
    state.kind = PendingEventKind::none;
    state.due = 0;
    state.origin = { };
    return ++state.generation;
}

void Interpreter::Impl::cancel_event(const SignalId event)
{
    const auto& signal = get_signal(event);
    const auto identity = signal.event_variable
        ? event_identities.at(event)
        : std::optional<SignalId> { event };
    if (identity) {
        (void)invalidate_event(*identity);
    }
}

void Interpreter::Impl::notify_event(
    const SignalId event,
    const SimulationTick delay,
    const EventNotificationKind kind,
    const StableOrder order,
    const SignalChangeOrigin origin)
{
    const auto& signal = get_signal(event);
    const auto identity = signal.event_variable
        ? event_identities.at(event)
        : std::optional<SignalId> { event };
    if (!identity) {
        return;
    }
    const auto notified_event = *identity;
    auto& state = event_states[notified_event];
    auto effective_kind = kind;

    if (kind == EventNotificationKind::delayed) {
        if (state.kind != PendingEventKind::none) {
            throw std::logic_error {
                "notify_delayed requires an event with no pending notification"
            };
        }
        effective_kind = delay == 0
            ? EventNotificationKind::delta
            : EventNotificationKind::timed;
    }

    if (effective_kind == EventNotificationKind::immediate) {
        if (delay != 0) {
            throw std::invalid_argument {
                "immediate event notification cannot have a delay"
            };
        }
        (void)invalidate_event(notified_event);
        trigger_event(notified_event, origin);
        return;
    }

    if (effective_kind == EventNotificationKind::delta) {
        if (delay != 0) {
            throw std::invalid_argument {
                "delta event notification cannot have a delay"
            };
        }
        if (state.kind == PendingEventKind::delta) {
            return;
        }
        const auto generation = invalidate_event(notified_event);
        state.kind = PendingEventKind::delta;
        state.due = scheduler.now();
        state.origin = origin;
        struct PendingEventTask {
            Interpreter::Impl* owner { };
            SignalId event { };
            std::uint64_t generation { };
        };
        const auto task =
            fsim::runtime::detail::make_scheduler_task_descriptor<
                PendingEventTask,
                +[](Scheduler&, const PendingEventTask& scheduled) {
                    auto& pending
                        = scheduled.owner->event_states[scheduled.event];
                    if (pending.generation != scheduled.generation
                        || pending.kind
                            != PendingEventKind::delta) {
                        return;
                    }
                    pending.kind = PendingEventKind::none;
                    pending.due = 0;
                    const auto origin = pending.origin;
                    pending.origin = { };
                    scheduled.owner->trigger_event(
                        scheduled.event, origin);
                }>(PendingEventTask {
                    this, notified_event, generation });
        if (origin.process_domain
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_internal_systemverilog_next_delta(
                origin.phase, order, task);
        } else {
            scheduler.schedule_internal_next_delta(
                SchedulerPhase::active, order, task);
        }
        return;
    }

    if (effective_kind != EventNotificationKind::timed || delay == 0) {
        throw std::invalid_argument {
            "timed event notification requires a non-zero delay"
        };
    }
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
        throw std::overflow_error {
            "simulation time overflow while scheduling event notification"
        };
    }
    const auto due = scheduler.now() + delay;
    if (state.kind == PendingEventKind::delta
        || (state.kind == PendingEventKind::timed
            && state.due <= due)) {
        return;
    }
    const auto generation = invalidate_event(notified_event);
    state.kind = PendingEventKind::timed;
    state.due = due;
    state.origin = origin;
    struct PendingEventTask {
        Interpreter::Impl* owner { };
        SignalId event { };
        std::uint64_t generation { };
        SimulationTick due { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            PendingEventTask,
            +[](Scheduler&, const PendingEventTask& scheduled) {
                auto& pending = scheduled.owner->event_states[
                    scheduled.event];
                if (pending.generation != scheduled.generation
                    || pending.kind != PendingEventKind::timed
                    || pending.due != scheduled.due) {
                    return;
                }
                pending.kind = PendingEventKind::none;
                pending.due = 0;
                const auto origin = pending.origin;
                pending.origin = { };
                scheduled.owner->trigger_event(
                    scheduled.event, origin);
            }>(PendingEventTask {
                this, notified_event, generation, due });
    if (origin.process_domain
        == ProcessSchedulingDomain::systemverilog) {
        scheduler.schedule_internal_systemverilog_at(
            due, origin.phase, order, task);
    } else {
        scheduler.schedule_internal_at(
            due, SchedulerPhase::active, order, task);
    }
}

void Interpreter::Impl::notify_execution_point(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex instruction,
    const ExecutionPointKind kind,
    const SourceLocation& source,
    const std::string_view scope)
{
    // The static kernel's host process runs a stub program that is not part
    // of the design: it raises no execution points. (Its members do, through
    // the ProcessId overload.)
    if (static_kernel
        && ((process.id < fusion_dormant_process.size()
                && fusion_dormant_process[process.id] == 2U)
            || static_kernel_host(process.id))) {
        return;
    }
    notify_execution_point(
        process.id, process.cold().design_process, process.program(),
        instruction, kind, source,
        scope.empty() ? std::string_view { process.cold().current_scope }
                      : scope);
}

void Interpreter::Impl::notify_execution_point(
    const ProcessId process,
    const ProcessId design_process,
    const ProcessProgramView program,
    const InstructionIndex instruction,
    const ExecutionPointKind kind,
    const SourceLocation& source,
    const std::string_view scope)
{
    if (execution_point_hook) {
        execution_point_hook(
            scheduler,
            ExecutionPoint {
                process, design_process, instruction, kind, source,
                std::string { scope }, program.language_standard(),
                program.compatibility_profile() });
    }
}

[[nodiscard]] bool Interpreter::Impl::monitor_watches(
    const SignalId signal) const
{
    if (!monitor) {
        return false;
    }
    if (monitor_signal_watches_unknown
        || monitor_signal_watch_mask.size() != signals.size()
        || signal >= monitor_signal_watch_mask.size()) {
        return true;
    }
    return monitor_signal_watch_mask[signal] != 0U;
}

[[nodiscard]] std::string Interpreter::Impl::render_monitor(
    const MonitorInstall& registration) const
{
    std::string text;
    for (const auto& value : registration.values) {
        if (value.kind == MonitorValueKind::time) {
            text += make_time_output(
                value.prefix,
                { },
                scheduler.now(),
                time_format,
                value.use_timeformat_width,
                value.minimum_width,
                value.left_justify,
                value.zero_pad);
        } else if (value.kind == MonitorValueKind::simulation_time) {
            const auto now = systemverilog_time_function(
                value.time_function,
                scheduler.now(),
                { value.time_unit_femtoseconds,
                    value.time_precision_femtoseconds,
                    time_format.resolution_femtoseconds });
            const auto encoded = !now
                ? SystemVerilogPackedScalarResult {
                      PackedLogic4 { }, now.error }
                : value.time_function == SystemVerilogTimeFunction::Realtime
                ? encode_systemverilog_scalar_payload(now.value)
                : systemverilog_scalar_to_packed(now.value,
                      value.time_function == SystemVerilogTimeFunction::Stime
                          ? 32U
                          : 64U,
                      false);
            // %t shows the time function's value, in this module's unit,
            // under $timeformat (IEEE 1800-2017 21.2.1.3).
            const auto tick = value.format == OutputFormat::time && encoded
                ? integral_time_ticks(encoded.value, false,
                      value.time_unit_femtoseconds == 0U
                          ? std::numeric_limits<std::uint64_t>::max()
                          : value.time_unit_femtoseconds,
                      time_format,
                      value.time_function == SystemVerilogTimeFunction::Realtime
                          ? SystemVerilogScalarKind::Realtime
                          : SystemVerilogScalarKind::None)
                : std::nullopt;
            if (tick) {
                text += make_time_output(value.prefix, { }, *tick,
                    time_format, value.use_timeformat_width,
                    value.minimum_width, value.left_justify,
                    value.zero_pad);
                continue;
            }
            text += make_formatted_output(
                value.prefix,
                { },
                value.format,
                encoded ? encoded.value
                        : PackedLogic4 { 64U, Logic4::x },
                value.signed_decimal,
                value.suppress_leading_zero,
                value.minimum_width,
                value.left_justify,
                value.zero_pad,
                value.scalar_kind);
        } else if (value.strength) {
            text += value.prefix + signal_strength_text(value.signal);
        } else if (value.format == OutputFormat::time
            && value.time_unit_femtoseconds != 0U) {
            // A value shown with %t counts in its module's time unit (IEEE
            // 1800-2017 21.2.1.3).
            const auto& current = get_signal(value.signal).initial_value;
            const auto tick = integral_time_ticks(current,
                value.signed_decimal, value.time_unit_femtoseconds,
                time_format, value.scalar_kind);
            text += tick
                ? make_time_output(value.prefix, { }, *tick, time_format,
                      value.use_timeformat_width, value.minimum_width,
                      value.left_justify, value.zero_pad)
                : make_formatted_output(value.prefix, { },
                      OutputFormat::decimal, current, value.signed_decimal,
                      value.suppress_leading_zero, value.minimum_width,
                      value.left_justify, value.zero_pad);
        } else {
            text += make_formatted_output(
                value.prefix,
                { },
                value.format,
                get_signal(value.signal).initial_value,
                value.signed_decimal,
                value.suppress_leading_zero,
                value.minimum_width,
                value.left_justify,
                value.zero_pad,
                value.scalar_kind);
        }
    }
    text += registration.trailing_text;
    return text;
}

void Interpreter::Impl::schedule_monitor_publication()
{
    if (!monitor || !monitor_enabled) {
        return;
    }
    const auto generation = monitor_generation;
    const auto process = monitor_process;
    if (processes.program_view(process).scheduling_domain()
        == ProcessSchedulingDomain::systemverilog) {
        const auto publication = scheduler.now();
        if (monitor_systemverilog_publication == publication) {
            return;
        }
        monitor_systemverilog_publication = publication;
        scheduler.schedule_end_of_time_slot(
            process,
            [this, generation, process](Scheduler& runtime) {
                if (generation != monitor_generation
                    || !monitor_enabled || !monitor) {
                    return;
                }
                monitor_systemverilog_publication.reset();
                if (monitor_file_handle) {
                    write_file(
                        process,
                        *monitor_file_handle,
                        render_monitor(*monitor),
                        monitor->newline);
                } else if (output_hook) {
                    output_hook(
                        process,
                        render_monitor(*monitor),
                        monitor->newline,
                        runtime.now(),
                        runtime.delta());
                }
            });
        return;
    }
    const auto publication = std::pair { scheduler.now(), scheduler.delta() };
    if (monitor_publication == publication) {
        return;
    }
    monitor_publication = publication;
    scheduler.schedule(
        SchedulerPhase::postponed,
        process,
        [this, generation, process](Scheduler& runtime) {
            if (generation != monitor_generation
                || !monitor_enabled || !monitor) {
                return;
            }
            monitor_publication.reset();
            if (monitor_file_handle) {
                write_file(
                    process,
                    *monitor_file_handle,
                    render_monitor(*monitor),
                    monitor->newline);
            } else if (output_hook) {
                output_hook(
                    process,
                    render_monitor(*monitor),
                    monitor->newline,
                    runtime.now(),
                    runtime.delta());
            }
        });
}

void Interpreter::Impl::install_monitor(
    const ProcessId process,
    const MonitorInstall& registration)
{
    install_monitor(process, registration, registration.file_handle
            ? std::optional<FileHandle> { known_file_handle(
                  get_process(process), *registration.file_handle) }
            : std::nullopt);
}

void Interpreter::Impl::install_monitor(const ProcessId process,
    const MonitorInstall& registration,
    const std::optional<FileHandle> file_handle)
{
    if (registration.one_shot) {
        auto publish = [this, process, registration, file_handle](
                           Scheduler& runtime) {
                if (file_handle) {
                    write_file(
                        process,
                        *file_handle,
                        render_monitor(registration),
                        registration.newline);
                } else if (output_hook) {
                    output_hook(
                        process,
                        render_monitor(registration),
                        registration.newline,
                        runtime.now(),
                        runtime.delta());
                }
            };
        if (processes.program_view(process).scheduling_domain()
            == ProcessSchedulingDomain::systemverilog) {
            scheduler.schedule_end_of_time_slot(process, std::move(publish));
        } else {
            scheduler.schedule(
                SchedulerPhase::postponed, process, std::move(publish));
        }
        return;
    }
    if (monitor_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "monitor generation overflow" };
    }
    ++monitor_generation;
    monitor = registration;
    monitor_signal_watches_unknown
        = monitor_signal_watch_mask.size() != signals.size();
    std::ranges::fill(monitor_signal_watch_mask, 0U);
    for (const auto& value : registration.values) {
        if (value.kind != MonitorValueKind::signal) {
            continue;
        }
        if (value.signal >= monitor_signal_watch_mask.size()) {
            monitor_signal_watches_unknown = true;
            continue;
        }
        monitor_signal_watch_mask[value.signal] = 1U;
    }
    monitor_process = process;
    monitor_file_handle = file_handle;
    monitor_enabled = true;
    monitor_publication.reset();
    monitor_systemverilog_publication.reset();
    schedule_monitor_publication();
}

void Interpreter::Impl::set_monitor_enabled(const bool enabled)
{
    if (monitor_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error { "monitor generation overflow" };
    }
    ++monitor_generation;
    monitor_enabled = enabled;
    monitor_publication.reset();
    monitor_systemverilog_publication.reset();
    if (enabled) {
        schedule_monitor_publication();
    }
}

void Interpreter::Impl::set_time_format(
    ProcessState& process,
    const TimeFormatControl& operation)
{
    try {
        apply_time_format(get_register(process, operation.units),
            get_register(process, operation.precision),
            get_register(process, operation.minimum_width),
            get_string_register(process, operation.suffix));
    } catch (const std::invalid_argument& error) {
        fail(process, error.what());
    }
}

void Interpreter::Impl::apply_time_format(const PackedLogic4& units_value,
    const PackedLogic4& precision_value,
    const PackedLogic4& minimum_width_value, const std::string& suffix)
{
    const auto decode = [&](const PackedLogic4& value, const char* name) {
        const auto word = value.low_word();
        if (value.width() != 32 || word.bval != 0) {
            throw std::invalid_argument(std::string { "$timeformat " } + name
                + " must be a known 32-bit value");
        }
        return static_cast<std::uint32_t>(word.aval);
    };
    const auto units = static_cast<std::int32_t>(
        decode(units_value, "units"));
    const auto precision = decode(precision_value, "precision");
    const auto minimum_width = decode(minimum_width_value, "minimum width");
    if (units < -15 || units > 0
        || precision > maximum_string_bytes
        || minimum_width > maximum_string_bytes
        || suffix.size() > maximum_string_bytes) {
        throw std::invalid_argument(
            "$timeformat arguments exceed their supported IEEE profile");
    }
    time_format.units = units;
    time_format.precision = precision;
    time_format.suffix = suffix;
    time_format.minimum_width = minimum_width;
}

[[nodiscard]] std::uint64_t Interpreter::Impl::initial_random_state(
    const std::uint64_t seed,
    const ProcessId process) noexcept
{
    auto value = seed
        + UINT64_C(0x9e3779b97f4a7c15)
            * (static_cast<std::uint64_t>(process) + 1U);
    value = (value ^ (value >> 30U))
        * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U))
        * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31U);
}

[[nodiscard]] std::uint32_t Interpreter::Impl::next_random(
    Interpreter::Impl::ProcessState& process) noexcept
{
    return next_random(process.cold().random_state);
}

[[nodiscard]] std::uint32_t Interpreter::Impl::next_random(
    std::uint64_t& state) noexcept
{
    auto value = (state += UINT64_C(0x9e3779b97f4a7c15));
    value = (value ^ (value >> 30U))
        * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27U))
        * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31U;
    return static_cast<std::uint32_t>(value >> 32U);
}

[[nodiscard]] std::optional<std::uint32_t>
Interpreter::Impl::known_random_bound(const PackedLogic4& value)
{
    if (value.empty()) {
        return std::nullopt;
    }
    std::uint32_t result { };
    const auto width = std::min<std::size_t>(32U, value.width());
    for (std::size_t bit = 0; bit < width; ++bit) {
        const auto state = value.get(bit);
        if (state == Logic4::x || state == Logic4::z) {
            return std::nullopt;
        }
        if (state == Logic4::one) {
            result |= UINT32_C(1) << bit;
        }
    }
    return result;
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::random_value(
    const ProcessId process_id,
    const RandomKind kind,
    const std::optional<PackedLogic4>& maximum,
    const std::optional<PackedLogic4>& minimum)
{
    return random_value(get_process(process_id).cold().random_state, kind,
        maximum, minimum);
}

[[nodiscard]] PackedLogic4 Interpreter::Impl::random_value(
    std::uint64_t& state,
    const RandomKind kind,
    const std::optional<PackedLogic4>& maximum,
    const std::optional<PackedLogic4>& minimum)
{
    if (kind == RandomKind::random) {
        const auto result = evaluate_random_distribution(
            RandomDistributionKind::uniform, system_random_seed,
            std::numeric_limits<std::int32_t>::min(),
            std::numeric_limits<std::int32_t>::max());
        system_random_seed = result.seed;
        return PackedLogic4::from_aval_bval(
            32, std::bit_cast<std::uint32_t>(result.value), 0);
    }
    if (kind != RandomKind::urandom_range) {
        return PackedLogic4::from_aval_bval(
            32, next_random(state), 0);
    }
    if (!maximum) {
        throw std::logic_error {
            "$urandom_range operation has no maximum"
        };
    }
    const auto known_maximum = known_random_bound(*maximum);
    const auto known_minimum = minimum
        ? known_random_bound(*minimum)
        : std::optional<std::uint32_t> { 0U };
    if (!known_maximum || !known_minimum) {
        return PackedLogic4(32, Logic4::x);
    }
    auto low = *known_minimum;
    auto high = *known_maximum;
    if (high < low) {
        std::swap(low, high);
    }
    const auto span = static_cast<std::uint64_t>(high)
        - static_cast<std::uint64_t>(low) + 1U;
    std::uint32_t sample { };
    if (span == (UINT64_C(1) << 32U)) {
        sample = next_random(state);
    } else {
        const auto full_range = UINT64_C(1) << 32U;
        const auto accepted = full_range - full_range % span;
        do {
            sample = next_random(state);
        } while (static_cast<std::uint64_t>(sample) >= accepted);
        sample = static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(low)
            + static_cast<std::uint64_t>(sample) % span);
    }
    return PackedLogic4::from_aval_bval(32, sample, 0);
}

void Interpreter::Impl::publish(
    SignalId signal_id, PackedLogic4 value,
    const bool notify_fanout)
{
    auto& signal = get_signal(signal_id);
    if (signal.initial_value.width() != value.width()) {
        throw std::invalid_argument(
            "SimIR signal assignment width mismatch (signal "
            + std::to_string(signal_id) + " has width "
            + std::to_string(signal.initial_value.width())
            + ", value has width " + std::to_string(value.width()) + ")");
    }
    value = normalize_signal_value(signal_id, std::move(value));
    publish_normalized(signal_id, std::move(value), notify_fanout);
}

void Interpreter::Impl::publish_normalized(
    const SignalId signal_id,
    PackedLogic4 value,
    const bool notify_fanout,
    const SignalChangeOrigin origin)
{
    const std::array<SignalId, 1U> changed_signals { signal_id };
    auto reference_refresh
        = prepare_container_value_reference_refresh(changed_signals);
    ActiveContainerReferenceRefresh active_refresh;
    begin_container_value_reference_refresh(
        active_refresh, changed_signals, reference_refresh);
    const auto finish_refresh = [this](
                                    ActiveContainerReferenceRefresh* frame) noexcept {
        end_container_value_reference_refresh(*frame);
    };
    const std::unique_ptr<ActiveContainerReferenceRefresh,
        decltype(finish_refresh)> refresh_scope(
        &active_refresh, finish_refresh);
    note_signal_transaction(signal_id, notify_fanout, origin);
    auto& signal = signals[signal_id];
    if (signal.initial_value == value) {
        synchronize_container_value_references(changed_signals, reference_refresh);
        return;
    }
    signal_last_values[signal_id] = std::move(signal.initial_value);
    signal.initial_value = std::move(value);
    refresh_direct_signal_planes(signal_id);
    synchronize_container_value_references(changed_signals, reference_refresh);
    publish_value_change(signal_id, notify_fanout, origin);
}

void Interpreter::Impl::publish_normalized_word(
    const SignalId signal_id,
    const Logic4Word value,
    const bool notify_fanout,
    const SignalChangeOrigin origin)
{
    const std::array<SignalId, 1U> changed_signals { signal_id };
    auto reference_refresh
        = prepare_container_value_reference_refresh(changed_signals);
    ActiveContainerReferenceRefresh active_refresh;
    begin_container_value_reference_refresh(
        active_refresh, changed_signals, reference_refresh);
    const auto finish_refresh = [this](
                                    ActiveContainerReferenceRefresh* frame) noexcept {
        end_container_value_reference_refresh(*frame);
    };
    const std::unique_ptr<ActiveContainerReferenceRefresh,
        decltype(finish_refresh)> refresh_scope(
        &active_refresh, finish_refresh);
    note_signal_transaction(signal_id, notify_fanout, origin);
    auto& signal = signals[signal_id];
    const auto current = signal.initial_value.unchecked_low_word();
    if (current == value) {
        synchronize_container_value_references(changed_signals, reference_refresh);
        return;
    }
    if (native_process_count_profile_enabled) {
        ++native_process_word_changes;
    }
    signal_last_values[signal_id].assign_word(current);
    signal.initial_value.assign_word(value);
    const auto encoded = signal.initial_value.unchecked_low_word();
    direct_signal_aval[signal_id] = encoded.aval;
    direct_signal_bval[signal_id] = encoded.bval;
    synchronize_container_value_references(changed_signals, reference_refresh);
    publish_value_change(signal_id, notify_fanout, origin);
}

bool Interpreter::Impl::can_publish_native_word(
    const SignalId signal_id,
    const ProcessId process) noexcept
{
    if (native_signal_observation_required_hook) {
        if (native_signal_observation_required_hook(signal_id)) {
            if (native_phase_profile_enabled) {
                ++native_phase_profile_rejected_observer;
            }
            return false;
        }
    } else if (signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook
        || container_element_change_hook) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_observer;
        }
        return false;
    }
    return can_publish_native_word_prevalidated(signal_id, process);
}

bool Interpreter::Impl::native_word_publication_phase_eligible() noexcept
{
    if (native_signal_observation_any_hook) {
        return !native_signal_observation_any_hook();
    }
    if (native_signal_observation_required_hook) {
        return false;
    }
    return !signal_change_hook && !stored_signal_change_hook
        && !driver_change_hook && !scalar_signal_change_hook
        && !container_object_change_hook
        && !container_element_change_hook;
}

bool Interpreter::Impl::native_signal_has_runtime_dependency(
    const SignalId signal_id) const noexcept
{
    if (signal_id >= signals.size()
        || signals[signal_id].public_value_reference_exposed
        || signal_id >= signal_container_element_aliases.size()
        || signal_id >= signal_container_aggregate_aliases.size()
        || native_signal_dependencies_unknown
        || native_signal_dependency_mask.size() != signals.size()) {
        return true;
    }
    // Element aliases publish logical-container changes and may feed dynamic
    // array readers through the ContainerObject interface. Aggregate proxies
    // are callback-backed views of those leaves. Native word and fused
    // publication routes do not prove either bridge, so keep both on the
    // ordinary checked publication path.
    if (signal_container_element_aliases[signal_id]
        || signal_container_aggregate_aliases[signal_id]) {
        return true;
    }
    if (native_signal_dependencies_unknown
        || native_signal_dependency_mask.size() != signals.size()) {
        return true;
    }
    if (requires_sampled_values) {
        if (sampled_value_dependencies_unknown
            || sampled_value_dependency_mask.size() != signals.size()) {
            return true;
        }
        if (sampled_value_dependency_mask[signal_id] != 0U) {
            return true;
        }
    }
    if (has_bidirectional_switches) {
        if (signal_id >= switch_endpoint_adjacency.size()
            || signal_id >= switch_control_adjacency.size()) {
            return true;
        }
        if (!switch_endpoint_adjacency[signal_id].empty()
            || !switch_control_adjacency[signal_id].empty()) {
            return true;
        }
    }
    if (monitor_watches(signal_id)) {
        return true;
    }
    return native_signal_dependency_mask[signal_id] != 0U;
}

void Interpreter::Impl::build_native_signal_dependency_masks() noexcept
{
    native_signal_dependencies_unknown = false;
    native_signal_dependency_mask.clear();
    module_path_destination_mask.clear();
    try {
        native_signal_dependency_mask.assign(signals.size(), 0U);
        module_path_destination_mask.assign(signals.size(), 0U);
    } catch (...) {
        native_signal_dependency_mask.clear();
        module_path_destination_mask.clear();
        native_signal_dependencies_unknown = true;
        return;
    }

    const auto mark_signal = [&](const SignalId signal) {
        if (signal >= native_signal_dependency_mask.size()) {
            native_signal_dependencies_unknown = true;
            return;
        }
        native_signal_dependency_mask[signal] = 1U;
    };
    const auto mark_terminal = [&](const ModulePathTerminal& terminal) {
        mark_signal(terminal.signal);
    };
    const auto mark_expression = [&](const ModulePathExpression& expression) {
        if (expression.empty()) {
            return;
        }
        if (expression.root >= expression.nodes.size()) {
            native_signal_dependencies_unknown = true;
            return;
        }
        for (const auto& node : expression.nodes) {
            switch (node.operation) {
            case ModulePathExpressionOperator::constant:
            case ModulePathExpressionOperator::bit_not:
            case ModulePathExpressionOperator::logical_not:
            case ModulePathExpressionOperator::reduction:
            case ModulePathExpressionOperator::binary:
            case ModulePathExpressionOperator::logical_binary:
            case ModulePathExpressionOperator::shift:
            case ModulePathExpressionOperator::conditional:
            case ModulePathExpressionOperator::concatenate:
                break;
            case ModulePathExpressionOperator::terminal:
                mark_terminal(node.terminal);
                break;
            default:
                native_signal_dependencies_unknown = true;
                break;
            }
        }
    };
    const auto mark_event = [&](const ModuleTimingEvent& event) {
        mark_terminal(event.terminal);
        mark_expression(event.condition);
    };

    // Native publication routes lack range-aware readiness proofs. Keep
    // ranged static sensitivities on checked publication.
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        // Inert static kernel members have no static sensitivity.
        if (static_kernel && id < fusion_dormant_process.size()
            && fusion_dormant_process[id] == 1U) {
            continue;
        }
        const auto program = processes.program_view(id);
        for (const auto& sensitivity : program.static_sensitivity()) {
            if (sensitivity.width != 0U)
                mark_signal(sensitivity.signal);
        }
    }

    for (const auto& path : module_paths) {
        for (const auto& source : path.sources) {
            mark_terminal(source);
        }
        for (const auto& destination : path.destinations) {
            mark_terminal(destination);
            if (destination.signal >= module_path_destination_mask.size()) {
                native_signal_dependencies_unknown = true;
            } else {
                module_path_destination_mask[destination.signal] = 1U;
            }
        }
        mark_expression(path.condition);
        mark_expression(path.data_source);
    }

    if (module_timing_check_states.size() != module_timing_checks.size()) {
        native_signal_dependencies_unknown = true;
    }
    for (const auto& check : module_timing_checks) {
        mark_event(check.reference);
        if (check.data) {
            mark_event(*check.data);
        }
        if (check.notifier) {
            mark_signal(*check.notifier);
        }
        if (check.delayed_reference) {
            mark_terminal(*check.delayed_reference);
        }
        if (check.delayed_data) {
            mark_terminal(*check.delayed_data);
        }
        mark_expression(check.timestamp_condition);
        mark_expression(check.timecheck_condition);
    }
}

void Interpreter::Impl::build_native_signal_publication_shape_certificate()
    noexcept
{
    native_signal_publication_shape_certificate.clear();
    if (native_signal_dependencies_unknown
        || native_signal_dependency_mask.size() != signals.size()
        || (requires_sampled_values
            && (sampled_value_dependencies_unknown
                || sampled_value_dependency_mask.size() != signals.size()))
        || signal_container_aliases.size() != signals.size()
        || signal_container_element_aliases.size() != signals.size()
        || signal_container_aggregate_aliases.size() != signals.size()
        || (has_bidirectional_switches
            && (switch_endpoint_adjacency.size() != signals.size()
                || switch_control_adjacency.size() != signals.size()))) {
        return;
    }

    try {
        std::vector<std::uint8_t> prepared(signals.size(), 0U);
        for (std::size_t index = 0U; index < signals.size(); ++index) {
            if (native_signal_dependency_mask[index] != 0U
                || (requires_sampled_values
                    && sampled_value_dependency_mask[index] != 0U)
                || !signal_container_aliases[index].empty()
                || signal_container_element_aliases[index]
                || signal_container_aggregate_aliases[index]) {
                continue;
            }
            if (has_bidirectional_switches
                && (!switch_endpoint_adjacency[index].empty()
                    || !switch_control_adjacency[index].empty())) {
                continue;
            }
            prepared[index] = 1U;
        }
        native_signal_publication_shape_certificate.swap(prepared);
    } catch (...) {
        // The certificate only permits an optimization. Allocation or
        // topology-shape failures leave every signal on checked publication.
        native_signal_publication_shape_certificate.clear();
    }
}

bool Interpreter::Impl::can_publish_native_word_prevalidated(
    const SignalId signal_id,
    const ProcessId process) noexcept
{
    const bool shape_certified
        = signal_id < signals.size()
        && signal_id < native_signal_publication_shape_certificate.size()
        && native_signal_publication_shape_certificate[signal_id] != 0U;
    if (native_phase_profile_enabled) {
        if (shape_certified) {
            ++native_phase_profile_shape_certificate_hits;
        } else {
            ++native_phase_profile_shape_certificate_misses;
        }
    }
    if (!shape_certified
        || signal_id >= direct_single_driver_routes.size()
        || signal_id >= dynamic_fanout.size()
        || signal_id >= signal_container_aliases.size()
        || signals[signal_id].public_value_reference_exposed
        || monitor_watches(signal_id)
        || has_dynamic_waits(signal_id)) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_structure;
        }
        return false;
    }
    const auto& signal = signals[signal_id];
    const auto& route = direct_single_driver_routes[signal_id];
    if (!route.active || route.process != process
        || direct_single_driver_record(signal_id) == nullptr) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_route;
        }
        return false;
    }
    if (signal.resolution != ResolutionKind::sv_wire
        && signal.value_kind == ValueKind::logic4
        && signal.systemverilog_scalar == SystemVerilogScalarKind::None) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_semantics;
        }
        return false;
    }
    if (signal.resolution != ResolutionKind::sv_wire
        || signal.value_kind != ValueKind::logic4
        || signal.systemverilog_scalar != SystemVerilogScalarKind::None
        || signal.event_variable || signal.has_implicit_driver
        || signal.has_charge_strength || external_driver_values[signal_id]
        || forced_values[signal_id]
        || forced_driver_values[signal_id]
        || signal.initial_value.width() == 0U
        || signal.initial_value.width() > 64U) {
        if (native_phase_profile_enabled) {
            ++native_phase_profile_rejected_runtime;
        }
        return false;
    }
    return true;
}

bool Interpreter::Impl::can_publish_native_logic9_word_prevalidated(
    const SignalId signal_id,
    const ProcessId process) noexcept
{
    if (signal_id >= signals.size()
        || signal_id >= direct_single_driver_routes.size()
        || signal_id >= dynamic_fanout.size()
        || signal_id >= signal_container_aliases.size()
        || native_signal_has_runtime_dependency(signal_id)
        || has_dynamic_waits(signal_id)
        || !signal_container_aliases[signal_id].empty()) {
        return false;
    }
    const auto& signal = signals[signal_id];
    const auto& route = direct_single_driver_routes[signal_id];
    return route.active && route.process == process
        && direct_single_driver_record(signal_id) != nullptr
        && signal.resolution == ResolutionKind::std_logic
        && signal.value_kind == ValueKind::logic9
        && signal.systemverilog_scalar == SystemVerilogScalarKind::None
        && !signal.event_variable && !signal.has_implicit_driver
        && !signal.has_charge_strength
        && !external_driver_values[signal_id]
        && !forced_values[signal_id]
        && !forced_driver_values[signal_id]
        && signal.initial_value.width() != 0U
        && signal.initial_value.width() <= 64U;
}

bool Interpreter::Impl::can_publish_blocking_word(
    const SignalId signal_id) noexcept
{
    if (package_static_initialization_active
        || signal_id >= signals.size()
        || signal_id >= dynamic_fanout.size()
        || signal_id >= signal_container_aliases.size()
        || native_signal_has_runtime_dependency(signal_id)
        || has_dynamic_waits(signal_id)
        || !signal_container_aliases[signal_id].empty()) {
        return false;
    }
    if (native_signal_observation_required_hook) {
        if (native_signal_observation_required_hook(signal_id)) {
            return false;
        }
    } else if (signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook
        || container_element_change_hook) {
        return false;
    }
    const auto& signal = signals[signal_id];
    return signal.resolution == ResolutionKind::none
        && signal.value_kind == ValueKind::logic4
        && signal.systemverilog_scalar == SystemVerilogScalarKind::None
        && !signal.event_variable && !signal.has_implicit_driver
        && !signal.has_charge_strength
        && !external_driver_values[signal_id]
        && !forced_values[signal_id]
        && !forced_driver_values[signal_id]
        && signal.initial_value.width() != 0U
        && signal.initial_value.width() <= 64U;
}

void Interpreter::Impl::publish_native_word(
    const SignalId signal_id,
    Logic4Word value,
    const SignalChangeOrigin origin)
{
    if (package_static_initialization_active) {
        throw std::logic_error {
            "native signal publication is disabled during package startup"
        };
    }

    const auto wide_offset = direct_wide_signal_offsets[signal_id];

    const bool has_static_fanout = !static_fanout_for(signal_id).empty();
    note_signal_transaction(signal_id, has_static_fanout, origin);
    const auto width = signals[signal_id].initial_value.width();
    const auto mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << width) - UINT64_C(1);
    value.aval &= mask;
    value.bval &= mask;
    const auto current = Logic4Word {
        width, direct_signal_aval[signal_id], direct_signal_bval[signal_id]
    };
    if (current == value) {
        return;
    }

    callback_observation_complete = false;
    direct_signal_last_aval[signal_id] = current.aval;
    direct_signal_last_bval[signal_id] = current.bval;
    direct_signal_aval[signal_id] = value.aval;
    direct_signal_bval[signal_id] = value.bval;
    direct_wide_signal_aval[wide_offset] = value.aval;
    direct_wide_signal_bval[wide_offset] = value.bval;

    direct_signal_materialization_pending[signal_id] = 1U;

    ++signal_value_revisions[signal_id];
    note_signal_revision(signal_id);
    note_aggregate_leaf_current_change(signal_id);
    if (signal_value_revisions[signal_id] == 0U) {
        signal_value_revisions[signal_id] = 1U;
        std::ranges::fill(
            container_materialized_revisions, std::nullopt);
    }
    record_signal_event(
        signal_id, scheduler.delta() + 1U, origin);
    scheduler.note_signal_change(signal_id);

    if (!has_static_fanout) {
        return;
    }
    const auto decode_low = [](const Logic4Word word) {
        const bool aval = (word.aval & UINT64_C(1)) != 0U;
        const bool bval = (word.bval & UINT64_C(1)) != 0U;
        return bval ? (aval ? Logic4::x : Logic4::z)
                    : (aval ? Logic4::one : Logic4::zero);
    };
    auto transition = StaticTransitionMatches { };
    const bool has_edge_fanout
        = !static_fanout_indices_for(
               signal_id, EdgeKind::posedge).empty()
        || !static_fanout_indices_for(
               signal_id, EdgeKind::negedge).empty();
    if (width != 0U && has_edge_fanout) {
        transition = decode_static_transition(
            decode_low(current), decode_low(value));
    }
    notify_static_value_change(signal_id, transition, true, origin);
}

void Interpreter::Impl::publish_native_logic9_word(
    const SignalId signal_id,
    Logic9Word value,
    const SignalChangeOrigin origin)
{
    const auto width = signals[signal_id].initial_value.width();
    const auto mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << width) - UINT64_C(1);
    value.width = width;
    for (auto& plane : value.planes) {
        plane &= mask;
    }

    const bool has_static_fanout = !static_fanout_for(signal_id).empty();
    note_signal_transaction(signal_id, has_static_fanout, origin);

    // Preserve the checked runtime value even if a raw native plane contains
    // a reserved code.
    const auto current = Logic9Word {
        width,
        { direct_signal_logic9_plane0[signal_id],
            direct_signal_logic9_plane1[signal_id],
            direct_signal_logic9_plane2[signal_id],
            direct_signal_logic9_plane3[signal_id] }
    };
    if ((((current.planes[0] ^ value.planes[0])
              | (current.planes[1] ^ value.planes[1])
              | (current.planes[2] ^ value.planes[2])
              | (current.planes[3] ^ value.planes[3]))
            & mask)
        == 0U) {
        return;
    }
    callback_observation_complete = false;
    direct_signal_last_logic9_plane0[signal_id] = current.planes[0];
    direct_signal_last_logic9_plane1[signal_id] = current.planes[1];
    direct_signal_last_logic9_plane2[signal_id] = current.planes[2];
    direct_signal_last_logic9_plane3[signal_id] = current.planes[3];
    direct_signal_logic9_plane0[signal_id] = value.planes[0];
    direct_signal_logic9_plane1[signal_id] = value.planes[1];
    direct_signal_logic9_plane2[signal_id] = value.planes[2];
    direct_signal_logic9_plane3[signal_id] = value.planes[3];

    direct_signal_materialization_pending[signal_id] = 1U;

    ++signal_value_revisions[signal_id];
    note_signal_revision(signal_id);
    note_aggregate_leaf_current_change(signal_id);
    if (signal_value_revisions[signal_id] == 0U) {
        signal_value_revisions[signal_id] = 1U;
        std::ranges::fill(
            container_materialized_revisions, std::nullopt);
    }
    record_signal_event(
        signal_id, scheduler.delta() + 1U, origin);
    scheduler.note_signal_change(signal_id);

    if (!has_static_fanout) {
        return;
    }
    const auto decode_low = [](const Logic9Word& word) {
        std::uint8_t encoded { };
        for (std::size_t plane = 0; plane < word.planes.size(); ++plane) {
            encoded |= static_cast<std::uint8_t>(
                (word.planes[plane] & UINT64_C(1)) << plane);
        }
        const auto logic9
            = encoded <= static_cast<std::uint8_t>(Logic9::dont_care)
            ? static_cast<Logic9>(encoded)
            : Logic9::x;
        return to_logic4(logic9);
    };
    auto transition = StaticTransitionMatches { };
    const bool has_edge_fanout
        = !static_fanout_indices_for(
               signal_id, EdgeKind::posedge).empty()
        || !static_fanout_indices_for(
               signal_id, EdgeKind::negedge).empty();
    if (width != 0U && has_edge_fanout) {
        transition = decode_static_transition(
            decode_low(current), decode_low(value));
    }
    notify_static_value_change(signal_id, transition, false, origin);
}

void Interpreter::Impl::note_signal_transaction(
    const SignalId signal_id,
    const bool notify_fanout,
    const SignalChangeOrigin origin)
{
    scheduler.note_signal_transaction(signal_id);
    // A port conversion of an active source is itself active in the same
    // cycle, even without an event (IEEE 1076-2008 14.7.3.3).
    if (!vhdl_conversion_dependents.empty()) {
        if (const auto found = vhdl_conversion_dependents.find(signal_id);
            found != vhdl_conversion_dependents.end()) {
            for (const auto target : found->second) {
                queue_vhdl_conversion(target);
            }
        }
    }
    const bool group_static_fanout = fanout_cohort_grouping_enabled;
    signal_transactions[signal_id] = std::pair { scheduler.now(), scheduler.delta() + 1 };
    note_selected_signal_transaction(signal_id);
    if (signal_id < signal_container_element_aliases.size()
        && signal_container_element_aliases[signal_id]) {
        const auto object
            = signal_container_element_aliases[signal_id]->first;
        if (object < container_aggregate_signal_aliases.size()
            && container_aggregate_signal_aliases[object]) {
            const auto proxy
                = container_aggregate_signal_aliases[object]->signal;
            auto& batch = aggregate_signal_batches[proxy];
            if (batch.depth != 0U) {
                batch.transaction_changed = true;
                batch.notify_fanout = batch.notify_fanout || notify_fanout;
                batch.origin = origin;
            } else {
                note_signal_transaction(proxy, notify_fanout, origin);
            }
        }
    }
    const auto transaction_fanout
        = static_fanout_indices_for(signal_id, EdgeKind::transaction);
    if (notify_fanout && !transaction_fanout.empty()) {
        const auto visit = next_static_fanout_visit();
        const auto no_cohort = std::numeric_limits<std::size_t>::max();
        for (const auto entry_index : transaction_fanout) {
            const auto& sensitivity = static_fanout_entries[entry_index];
            auto& triggered_process = get_process(sensitivity.process);
            triggered_process.static_trigger_mask
                |= sensitivity.static_trigger_mask;
            const auto cohort
                = sensitivity.process
                        < static_sensitivity_cohort_by_process.size()
                ? static_sensitivity_cohort_by_process[
                      sensitivity.process]
                : no_cohort;
            if (group_static_fanout && cohort != no_cohort
                && static_sensitivity_cohorts[cohort].members.size()
                    >= 2U) {
                auto& grouped = static_sensitivity_cohorts[cohort];
                if (grouped.fanout_visit != visit) {
                    grouped.fanout_visit = visit;
                    if (origin.process_domain
                            == ProcessSchedulingDomain::generic
                        && triggered_process.program().scheduling_domain()
                            == ProcessSchedulingDomain::generic) {
                        queue_static_cohort_next_delta(cohort);
                    } else {
                        for (const auto member : grouped.members) {
                            auto& candidate = get_process(member);
                            if (candidate.waiting_on_static) {
                                queue_static_next_delta(member, origin);
                            }
                        }
                    }
                }
                continue;
            }
            auto& process = get_process(sensitivity.process);
            if (process.waiting_on_static) {
                queue_static_next_delta(sensitivity.process, origin);
            }
        }
    }
}

void Interpreter::Impl::publish_aggregate_leaf_value_change(
    const SignalId signal_id,
    const bool notify_fanout,
    const SignalChangeOrigin origin)
{
    if (signal_id >= signal_container_element_aliases.size()
        || !signal_container_element_aliases[signal_id]) {
        return;
    }
    const auto object
        = signal_container_element_aliases[signal_id]->first;
    if (object >= container_aggregate_signal_aliases.size()
        || !container_aggregate_signal_aliases[object]) {
        return;
    }
    const auto proxy = container_aggregate_signal_aliases[object]->signal;
    auto& batch = aggregate_signal_batches[proxy];
    if (batch.depth != 0U) {
        batch.changed = true;
        batch.notify_fanout = batch.notify_fanout || notify_fanout;
        batch.origin = origin;
        return;
    }
    publish_aggregate_signal_value_change(proxy, notify_fanout, origin);
}

void Interpreter::Impl::publish_aggregate_signal_value_change(
    const SignalId proxy,
    const bool notify_fanout,
    const SignalChangeOrigin origin,
    std::exception_ptr* observer_error,
    const bool event_prepared)
{
    // Whole-family writes can preinstall the stamp and history before traces.
    if (!event_prepared) {
        record_signal_event(proxy, scheduler.delta() + 1U, origin);
    }
    scheduler.note_signal_change(proxy);

    if (observer_error == nullptr) {
        const auto& previous = aggregate_signal_last_value(proxy);
        const auto& current = aggregate_signal_current_value(proxy);
        evaluate_module_timing_checks(proxy, previous, current);
        if (signal_change_hook) {
            signal_change_hook(proxy, current, scheduler.now());
        }
        if (monitor_watches(proxy)) {
            schedule_monitor_publication();
        }
        if (!notify_fanout) {
            return;
        }
        const auto static_entries = static_fanout_for(proxy);
        const auto dynamic_entries = dynamic_fanout.at(proxy);
        if (static_entries.empty() && dynamic_entries.empty()) {
            return;
        }
        signals[proxy].initial_value = current;
        signal_last_values[proxy] = previous;
        refresh_direct_signal_planes(proxy);
        notify_static_value_change(proxy, { }, false, origin);
        for (const auto& registration : dynamic_entries) {
            if (registration.edge != EdgeKind::any) {
                continue;
            }
            if (registration.process >= processes.size()) {
                continue;
            }
            auto& process = get_process(registration.process);
            if (dynamic_wait_satisfied(process, proxy, registration)) {
                mark_dynamic_event_resume(process);
                queue_next_delta(registration.process, origin);
            }
        }
        return;
    }

    const auto& previous = aggregate_signal_last_value(proxy);
    const auto& current = aggregate_signal_current_value(proxy);
    std::optional<PackedLogic4> observer_value;
    if (signal_change_hook) {
        observer_value.emplace(current);
    }
    evaluate_module_timing_checks(proxy, previous, current);
    if (monitor_watches(proxy)) {
        schedule_monitor_publication();
    }
    if (notify_fanout) {
        const auto static_entries = static_fanout_for(proxy);
        const auto dynamic_entries = dynamic_fanout.at(proxy);
        if (!static_entries.empty() || !dynamic_entries.empty()) {
            signals[proxy].initial_value = current;
            signal_last_values[proxy] = previous;
            refresh_direct_signal_planes(proxy);
            notify_static_value_change(proxy, { }, false, origin);
            for (const auto& registration : dynamic_entries) {
                if (registration.edge != EdgeKind::any) {
                    continue;
                }
                if (registration.process >= processes.size()) {
                    continue;
                }
                auto& process = get_process(registration.process);
                if (dynamic_wait_satisfied(process, proxy, registration)) {
                    mark_dynamic_event_resume(process);
                    queue_next_delta(registration.process, origin);
                }
            }
        }
    }

    if (observer_value) {
        try {
            signal_change_hook(
                proxy, *observer_value, scheduler.now());
        } catch (...) {
            if (!*observer_error) {
                *observer_error = std::current_exception();
            }
        }
    }
}

void Interpreter::Impl::publish_value_change(
    const SignalId signal_id,
    const bool notify_fanout,
    const SignalChangeOrigin origin,
    const bool state_prepared)
{
    auto& signal = signals[signal_id];
    const auto& old_value = signal_last_values[signal_id];
    // A port conversion reading this signal updates its target in the same
    // simulation cycle (IEEE 1076-2008 14.7.3.3).
    if (!vhdl_conversion_dependents.empty()) {
        if (const auto found = vhdl_conversion_dependents.find(signal_id);
            found != vhdl_conversion_dependents.end()) {
            for (const auto target : found->second) {
                queue_vhdl_conversion(target);
            }
        }
    }
    if (!state_prepared) {
        ++signal_value_revisions[signal_id];
        note_signal_revision(signal_id);
        note_aggregate_leaf_current_change(signal_id);
        if (signal_value_revisions[signal_id] == 0U) {
            signal_value_revisions[signal_id] = 1U;
            std::ranges::fill(
                container_materialized_revisions, std::nullopt);
        }
        record_signal_event(
            signal_id, scheduler.delta() + 1U, origin);
    }
    scheduler.note_signal_change(signal_id);
    if (static_kernel_import_enabled) {
        // An opaque host process wrote a kernel-owned signal.
        import_static_kernel_write(signal_id, origin);
    }
    evaluate_module_timing_checks(signal_id, old_value, signal.initial_value);
    auto* alias_observer = container_alias_leaf_observer(signal_id);
    if (signal_change_hook && alias_observer) {
        alias_observer->current_changed = true;
        const auto object
            = signal_container_element_aliases[signal_id]->first;
        container_alias_write_batches[object].frames.back().changed = true;
    } else if (signal_change_hook) {
        signal_change_hook(signal_id, signal.initial_value, scheduler.now());
    }
    if (scalar_signal_change_hook
        && signal.systemverilog_scalar != SystemVerilogScalarKind::None) {
        if (alias_observer) {
            alias_observer->scalar_changed = true;
        } else {
            const auto scalar = decode_systemverilog_scalar_payload(
                signal.initial_value, signal.systemverilog_scalar);
            if (!scalar) {
                throw std::logic_error {
                    "SimIR scalar signal published an invalid payload"
                };
            }
            scalar_signal_change_hook(
                signal_id, scalar.value, scheduler.now());
        }
    }
    if (monitor_watches(signal_id)) {
        schedule_monitor_publication();
    }

    publish_aggregate_leaf_value_change(signal_id, notify_fanout, origin);

    if (notify_fanout) {
        auto transition = StaticTransitionMatches { };
        bool transition_decoded { };
        const bool has_static_edge_fanout
            = !static_fanout_indices_for(
                   signal_id, EdgeKind::posedge).empty()
            || !static_fanout_indices_for(
                   signal_id, EdgeKind::negedge).empty();
        if (old_value.width() != 0U
            && signal.initial_value.width() != 0U
            && has_static_edge_fanout) {
            transition = decode_static_transition(
                edge_value(old_value), edge_value(signal.initial_value));
            transition_decoded = true;
        }
        notify_static_value_change(signal_id, transition, false, origin);

        // Copy because queue_next_delta removes a process from every dynamic list.
        const auto dynamic = dynamic_fanout[signal_id];
        for (const auto& registration : dynamic) {
            if (registration.edge != EdgeKind::any) {
                if (old_value.width() == 0U
                    || signal.initial_value.width() == 0U) {
                    continue;
                }
                if (!transition_decoded) {
                    transition = decode_static_transition(
                        edge_value(old_value), edge_value(signal.initial_value));
                    transition_decoded = true;
                }
                if (!transition.matches(registration.edge)) {
                    continue;
                }
            }
            if (registration.process >= processes.size()) {
                continue;
            }
            auto& process = get_process(registration.process);
            if (dynamic_wait_satisfied(
                    process, signal_id, registration)) {
                mark_dynamic_event_resume(process);
                queue_next_delta(registration.process, origin);
            }
        }
    }
}

void Interpreter::Impl::refresh_direct_signal_planes(
    const SignalId signal_id)
{
    const auto& value = signals[signal_id].initial_value;
    if (value.is_logic9()) {
        if (value.width() != 0U && value.width() <= 64U) {
            const auto word = value.logic9_low_word();
            direct_signal_logic9_plane0[signal_id] = word.planes[0];
            direct_signal_logic9_plane1[signal_id] = word.planes[1];
            direct_signal_logic9_plane2[signal_id] = word.planes[2];
            direct_signal_logic9_plane3[signal_id] = word.planes[3];
        }
        const auto offset = direct_wide_signal_offsets[signal_id];
        const auto plane0 = value.logic9_plane_words(0U);
        const auto plane1 = value.logic9_plane_words(1U);
        const auto plane2 = value.logic9_plane_words(2U);
        const auto plane3 = value.logic9_plane_words(3U);
        std::ranges::copy(
            plane0, direct_wide_signal_aval.begin() + offset);
        std::ranges::copy(
            plane1, direct_wide_signal_bval.begin() + offset);
        std::ranges::copy(
            plane2, direct_wide_signal_logic9_plane2.begin() + offset);
        std::ranges::copy(
            plane3, direct_wide_signal_logic9_plane3.begin() + offset);
        return;
    }
    if (value.width() <= 64U) {
        const auto word = value.unchecked_low_word();
        direct_signal_aval[signal_id] = word.aval;
        direct_signal_bval[signal_id] = word.bval;
    }
    const auto offset = direct_wide_signal_offsets[signal_id];
    const auto aval = value.aval_words();
    const auto bval = value.bval_words();
    std::ranges::copy(
        aval, direct_wide_signal_aval.begin() + offset);
    std::ranges::copy(
        bval, direct_wide_signal_bval.begin() + offset);
}

} // namespace fsim::runtime::simir
