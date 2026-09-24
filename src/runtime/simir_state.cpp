// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <cstdlib>

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
    cohort_snapshots.discard();
    active_cohort_ready.clear();
    native_region_ready_scratch.clear();
    for (auto& cohort : static_sensitivity_cohorts) {
        cohort.pending = { };
        cohort.ready.clear();
    }
    for (auto& process : processes) {
        process.queued = false;
    }
    for (auto& region : native_static_regions) {
        std::ranges::fill(region.active, UINT8_C(0));
        region.ready_offsets.clear();
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
        || signal_id >= driver_values.size()) {
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
        || signal_id >= driver_values.size()) {
        return nullptr;
    }
    const auto& route = direct_single_driver_routes[signal_id];
    return route.active
        ? driver_values[signal_id].find(route.process)
        : nullptr;
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
    return processes[id];
}

[[nodiscard]] Interpreter::Impl::ProcessFrame&
Interpreter::Impl::ensure_process_frame(ProcessState& process)
{
    if (process.frame) {
        return *process.frame;
    }

    auto frame = std::make_shared<ProcessFrame>();
    frame->registers.assign(
        process.program().register_count, PackedLogic4 { });
    frame->string_registers.assign(
        process.program().string_register_count, { });
    frame->container_registers.reserve(
        process.program().container_register_count);
    for (const auto& type : process.program().container_register_types) {
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
    if (process.program().register_value_kinds.empty()) {
        return ValueKind::logic4;
    }
    return process.program().register_value_kinds.at(id);
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
    if (kind != SystemVerilogScalarKind::None) {
        const auto decoded = decode_systemverilog_scalar_payload(
            normalized, kind);
        if (!decoded) {
            throw std::invalid_argument {
                "SimIR scalar signal has an invalid encoded payload"
            };
        }
        const auto classification = classify_systemverilog_scalar(decoded.value);
        if (kind != SystemVerilogScalarKind::Chandle
            && (!classification || !classification.finite)) {
            throw std::invalid_argument {
                "SimIR scalar signal requires a finite value"
            };
        }
    }
    return normalized;
}

void Interpreter::Impl::remove_dynamic_wait(ProcessState& process)
{
    auto& cold = process.cold();
    if (!process.waiting_on_signal
        && !cold.waiting_on_container
        && cold.dynamic_sensitivity.empty()) {
        return;
    }

    const auto old_wait_generation = cold.dynamic_wait_generation;
    if (cold.dynamic_wait_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "dynamic wait generation overflow");
    }
    ++cold.dynamic_wait_generation;
    process.waiting_on_signal = false;
    ensure_dynamic_fanout_counts();
    for (std::size_t sensitivity_index = 0;
        sensitivity_index < cold.dynamic_sensitivity.size();
        ++sensitivity_index) {
        const auto signal = cold.dynamic_sensitivity[sensitivity_index].signal;
        if (signal >= dynamic_fanout.size()
            || sensitivity_index >= cold.dynamic_fanout_positions.size()) {
            continue;
        }
        auto& fanout = dynamic_fanout[signal];
        const auto position = cold.dynamic_fanout_positions[sensitivity_index];
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
    cold.dynamic_sensitivity.clear();
    cold.dynamic_triggered.clear();
    cold.dynamic_fanout_positions.clear();
    cold.dynamic_wait_all = false;
    cold.wait_order_events.clear();
    cold.wait_order_index = 0;
    cold.wait_order_result.reset();
    if (cold.waiting_on_container) {
        auto& fanout = container_dynamic_fanout[*cold.waiting_on_container];
        std::erase(fanout, process.id);
        cold.waiting_on_container.reset();
    }
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
    cold.dynamic_fanout_positions.clear();
    cold.dynamic_fanout_positions.reserve(cold.dynamic_sensitivity.size());
    for (const auto& sensitivity : cold.dynamic_sensitivity) {
        if (sensitivity.signal >= dynamic_fanout.size()) {
            fail(process, "dynamic wait references an invalid signal");
        }
    }
    if (cold.dynamic_wait_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "dynamic wait generation overflow");
    }
    ++cold.dynamic_wait_generation;
    ensure_dynamic_fanout_counts();
    std::size_t registered = 0;
    try {
        for (std::size_t index = 0;
            index < cold.dynamic_sensitivity.size(); ++index) {
            const auto& sensitivity = cold.dynamic_sensitivity[index];
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
                cold.dynamic_wait_generation,
                sensitivity.edge,
                index,
                true
            });
            cold.dynamic_fanout_positions.push_back(position);
            ++active;
            ++registered;
        }
    } catch (...) {
        for (std::size_t index = 0; index < registered; ++index) {
            const auto signal = cold.dynamic_sensitivity[index].signal;
            const auto position = cold.dynamic_fanout_positions[index];
            auto& fanout = dynamic_fanout[signal];
            if (position < fanout.size() && fanout[position].active
                && fanout[position].process == process.id
                && fanout[position].wait_generation
                    == cold.dynamic_wait_generation) {
                fanout[position].active = false;
                --dynamic_fanout_active_counts[signal];
                ++dynamic_fanout_tombstone_counts[signal];
            }
        }
        cold.dynamic_fanout_positions.clear();
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
    const auto& cold = process.cold();
    if (cold.dynamic_wait_generation != registration.wait_generation
        || registration.sensitivity_index >= cold.dynamic_sensitivity.size()) {
        return false;
    }
    const auto& sensitivity = cold.dynamic_sensitivity[
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
        auto& cold = process.cold();
        if (registration.sensitivity_index
                < cold.dynamic_fanout_positions.size()
            && dynamic_wait_registration_is_current(
                process, registration, signal)) {
            cold.dynamic_fanout_positions[
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

void Interpreter::Impl::write_process_register(
    Interpreter::Impl::ProcessState& process,
    const RegisterId destination,
    const PackedLogic4& value)
{
    const auto converted = coerce_value_kind(
        value, register_value_kind(process, destination));
    if (process.cold().suspended_callable_context) {
        auto& context = *process.cold().suspended_callable_context;
        const auto found = std::ranges::lower_bound(
            context.packed_ids, destination);
        if (found != context.packed_ids.end()
            && *found == destination) {
            const auto index = static_cast<std::size_t>(
                std::distance(context.packed_ids.begin(), found));
            context.packed[index] = converted;
            return;
        }
    }
    if (process.executor) {
        process.executor->write_register(
            destination, converted);
        return;
    }
    get_register(process, destination) = converted;
}

void Interpreter::Impl::clear_wait_timeout(ProcessState& process)
{
    auto& cold = process.cold();
    if (!cold.wait_timeout_origin) {
        return;
    }
    if (cold.wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        fail(process, "wait timeout generation overflow");
    }
    ++cold.wait_timeout_generation;
    cold.wait_timeout_origin.reset();
    cold.wait_timeout_deadline.reset();
    cold.wait_timeout_result.reset();
}

void Interpreter::Impl::set_wait_timeout_result(
    Interpreter::Impl::ProcessState& process,
    const bool timed_out)
{
    auto& cold = process.cold();
    if (!cold.wait_timeout_result) {
        return;
    }
    write_process_register(
        process,
        *cold.wait_timeout_result,
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
    auto& cold = process.cold();
    if (delay
        > std::numeric_limits<SimulationTick>::max()
            - scheduler.now()) {
        process.pc = origin;
        fail(process, "simulation time overflow in WaitOn timeout");
    }
    if (cold.wait_timeout_generation
        == std::numeric_limits<std::uint64_t>::max()) {
        process.pc = origin;
        fail(process, "wait timeout generation overflow");
    }
    const auto generation = ++cold.wait_timeout_generation;
    const auto deadline = scheduler.now() + delay;
    cold.wait_timeout_origin = origin;
    cold.wait_timeout_deadline = deadline;
    cold.wait_timeout_result = result;
    set_wait_timeout_result(process, false);

    struct WaitTimeoutTask {
        Interpreter::Impl* owner { };
        ProcessId process { };
        InstructionIndex origin { };
        std::uint64_t generation { };
    };
    const auto task =
        fsim::runtime::detail::make_scheduler_task_descriptor<
            WaitTimeoutTask,
            +[](Scheduler&, const WaitTimeoutTask& scheduled) {
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                auto& timeout = state.cold();
                if (timeout.wait_timeout_generation
                        != scheduled.generation
                    || timeout.wait_timeout_origin
                        != std::optional { scheduled.origin }) {
                    return;
                }
                scheduled.owner->set_wait_timeout_result(state, true);
                timeout.wait_timeout_origin.reset();
                timeout.wait_timeout_deadline.reset();
                timeout.wait_timeout_result.reset();
                scheduled.owner->queue_active_current(
                    scheduled.process);
            }>(WaitTimeoutTask {
                this, process.id, origin, generation });
    if (delay == 0) {
        scheduler.schedule_internal(
            SchedulerPhase::inactive,
            process.id,
            task);
    } else {
        scheduler.schedule_internal_at(
            deadline,
            SchedulerPhase::active,
            process.id,
            task);
    }
}

void Interpreter::Impl::rearm_wait_timeout(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex instruction,
    const InstructionIndex origin,
    const std::optional<RegisterId> result)
{
    const auto& cold = process.cold();
    if (cold.wait_timeout_origin
            != std::optional { origin }
        || !cold.wait_timeout_deadline) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout rearm has no matching active deadline");
    }
    if (cold.wait_timeout_result != result) {
        process.pc = instruction;
        fail(
            process,
            "WaitOn timeout rearm result register mismatch");
    }
    if (*cold.wait_timeout_deadline < scheduler.now()) {
        process.pc = instruction;
        fail(process, "WaitOn timeout deadline was missed");
    }
}

void Interpreter::Impl::mark_dynamic_event_resume(
    Interpreter::Impl::ProcessState& process)
{
    const auto& cold = process.cold();
    const auto timed_out = cold.wait_timeout_deadline
        && *cold.wait_timeout_deadline <= scheduler.now();
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
    auto& cold = process.cold();
    if (cold.wait_order_result) {
        if (cold.wait_order_index >= cold.wait_order_events.size()) {
            throw std::logic_error {
                "wait_order state has no expected event"
            };
        }
        if (signal
            == cold.wait_order_events[cold.wait_order_index]) {
            ++cold.wait_order_index;
            if (cold.wait_order_index != cold.wait_order_events.size()) {
                return false;
            }
            write_process_register(
                process,
                *cold.wait_order_result,
                PackedLogic4::from_aval_bval(1, 1, 0));
            return true;
        }
        write_process_register(
            process,
            *cold.wait_order_result,
            PackedLogic4::from_aval_bval(1, 0, 0));
        return true;
    }
    if (!cold.dynamic_wait_all) {
        return true;
    }
    if (registration.sensitivity_index >= cold.dynamic_triggered.size()) {
        throw std::logic_error {
            "dynamic wait-all state has no matching sensitivity"
        };
    }
    cold.dynamic_triggered[registration.sensitivity_index] = true;
    return std::all_of(
        cold.dynamic_triggered.begin(),
        cold.dynamic_triggered.end(),
        [](const bool triggered) { return triggered; });
}

void Interpreter::Impl::register_static_sensitivity_cohort(
    const ProcessId id)
{
    const auto& process = get_process(id).program();
    static_sensitivity_cohort_by_process.resize(
        processes.size(), std::numeric_limits<std::size_t>::max());
    if (process.static_sensitivity.empty()) {
        return;
    }

    auto sensitivity = process.static_sensitivity;
    std::ranges::sort(
        sensitivity,
        { },
        [](const Sensitivity& entry) {
            return std::pair {
                entry.signal,
                static_cast<std::underlying_type_t<EdgeKind>>(entry.edge)
            };
        });
    sensitivity.erase(
        std::ranges::unique(
            sensitivity,
            { },
            [](const Sensitivity& entry) {
                return std::pair {
                    entry.signal,
                    static_cast<std::underlying_type_t<EdgeKind>>(entry.edge)
                };
            })
            .begin(),
        sensitivity.end());

    std::string key;
    key += process.postponed ? 'p'
        : process.reactive ? 'r'
        : process.observed ? 'o'
                           : 'a';
    for (const auto& entry : sensitivity) {
        key += std::to_string(static_cast<std::size_t>(entry.signal));
        key += ':';
        key += std::to_string(static_cast<std::underlying_type_t<EdgeKind>>(
            entry.edge));
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
    for (std::size_t process_index = 0;
         process_index < processes.size(); ++process_index) {
        for (const auto& sensitivity :
             processes[process_index].program().static_sensitivity) {
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
        const auto& process = processes[process_index].program();
        const auto id = static_cast<ProcessId>(process_index);
        for (std::size_t sensitivity_index = 0;
             sensitivity_index < process.static_sensitivity.size();
             ++sensitivity_index) {
            const auto& sensitivity
                = process.static_sensitivity[sensitivity_index];
            const auto trigger_mask
                = sensitivity_index < 63U
                    && !process.static_trigger_regions.empty()
                ? UINT64_C(1) << sensitivity_index
                : Process::full_static_trigger_mask;
            const auto category = static_cast<std::size_t>(sensitivity.edge);
            const auto entry_index = next[sensitivity.signal]++;
            entries[entry_index] = {
                id, sensitivity.edge, trigger_mask
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
    const bool count_native_word_profile)
{
    const bool group_static_fanout = fanout_cohort_grouping_enabled;
    const auto visit = next_static_fanout_visit();
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    const auto visit_category = [&](const EdgeKind edge) {
        for (const auto entry_index :
             static_fanout_indices_for(signal_id, edge)) {
            const auto& sensitivity = static_fanout_entries[entry_index];
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
                    queue_static_cohort_next_delta(cohort);
                }
                continue;
            }

            if (triggered_process.waiting_on_static) {
                queue_static_next_delta(sensitivity.process);
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

void Interpreter::Impl::queue_at(ProcessId id, SimulationTick time)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
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
    scheduler.schedule_internal_at(time, phase, id, task);
}

void Interpreter::Impl::queue_next_delta(ProcessId id)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
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
    scheduler.schedule_internal_next_delta(phase, id, task);
}

void Interpreter::Impl::queue_static_next_delta(const ProcessId id)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    const auto no_cohort = std::numeric_limits<std::size_t>::max();
    const auto cohort = id < static_sensitivity_cohort_by_process.size()
        ? static_sensitivity_cohort_by_process[id]
        : no_cohort;
    if (cohort == no_cohort
        || static_sensitivity_cohorts[cohort].members.size() < 2U) {
        const auto no_region = std::numeric_limits<std::size_t>::max();
        const auto region = id < native_static_region_by_process.size()
            ? native_static_region_by_process[id]
            : no_region;
        if (region != no_region) {
            process.queued = true;
            const auto offset = native_static_region_offset_by_process[id];
            native_static_regions[region].active[offset] = 1U;
            const auto fallback = [this, id, region, offset](Scheduler&) {
                native_static_regions[region].active[offset] = 0U;
                auto& state = get_process(id);
                state.queued = false;
                state.waiting_on_static = false;
                remove_dynamic_wait(state);
                execute(id);
            };
            scheduler.schedule_next_delta_batchable(
                SchedulerPhase::active, id, *this,
                native_static_region_payload
                    | static_cast<std::uint64_t>(id),
                fallback);
            return;
        }
        queue_next_delta(id);
        return;
    }

    queue_static_cohort_next_delta(cohort);
}

void Interpreter::Impl::queue_static_cohort_next_delta(
    const std::size_t cohort_id)
{
    auto& cohort = static_sensitivity_cohorts.at(cohort_id);
    if (cohort.pending) {
        return;
    }

    cohort.ready.clear();
    cohort.ready.reserve(cohort.members.size());
    for (const auto member : cohort.members) {
        auto& candidate = get_process(member);
        if (candidate.halted || candidate.queued
            || !candidate.waiting_on_static) {
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
                auto& state = scheduled.owner->get_process(
                    scheduled.process);
                state.queued = false;
                scheduled.owner->execute(scheduled.process);
            }>(ProcessQueueTask { this, id });
    scheduler.schedule_internal(phase, id, task);
}

void Interpreter::Impl::queue_active_current(ProcessId id)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
        return;
    }
    process.queued = true;
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
    scheduler.schedule_internal(phase, id, task);
}

void Interpreter::Impl::queue_static_active_current(const ProcessId id)
{
    auto& process = get_process(id);
    if (process.halted || process.queued) {
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

void Interpreter::Impl::trigger_event(const SignalId event)
{
    const auto& source = get_signal(event);
    if (event_trigger_hook) {
        event_trigger_hook(event, scheduler.now());
    }
    const auto update_member = [&](const SignalId member) {
        signal_events[member] = std::pair {
            scheduler.now(), scheduler.delta()
        };
        for (const auto& sensitivity : static_fanout_for(member)) {
            auto& process = get_process(sensitivity.process);
            process.static_trigger_mask |= sensitivity.static_trigger_mask;
            if (process.waiting_on_static) {
                queue_static_active_current(sensitivity.process);
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
            queue_active_current(registration.process);
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
    const StableOrder order)
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
        trigger_event(notified_event);
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
                    auto& pending
                        = scheduled.owner->event_states[scheduled.event];
                    if (pending.generation != scheduled.generation
                        || pending.kind
                            != PendingEventKind::delta) {
                        return;
                    }
                    pending.kind = PendingEventKind::none;
                    pending.due = 0;
                    scheduled.owner->trigger_event(scheduled.event);
                }>(PendingEventTask {
                    this, notified_event, generation, state.due });
        scheduler.schedule_internal_next_delta(
            SchedulerPhase::active,
            order,
            task);
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
                scheduled.owner->trigger_event(scheduled.event);
            }>(PendingEventTask {
                this, notified_event, generation, due });
    scheduler.schedule_internal_at(
        due,
        SchedulerPhase::active,
        order,
        task);
}

void Interpreter::Impl::notify_execution_point(
    Interpreter::Impl::ProcessState& process,
    const InstructionIndex instruction,
    const ExecutionPointKind kind,
    const SourceLocation& source,
    const std::string_view scope)
{
    if (execution_point_hook) {
        const auto effective_scope = scope.empty()
            ? std::string_view { process.cold().current_scope }
            : scope;
        execution_point_hook(
            scheduler,
            ExecutionPoint {
                process.id, process.design_process,
                instruction, kind, source, std::string { effective_scope },
                process.program().language_standard,
                process.program().compatibility_profile });
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
    const auto publication = std::pair { scheduler.now(), scheduler.delta() };
    if (monitor_publication == publication) {
        return;
    }
    monitor_publication = publication;
    const auto generation = monitor_generation;
    const auto process = monitor_process;
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
    const auto file_handle = registration.file_handle
        ? std::optional<FileHandle> { known_file_handle(
              get_process(process), *registration.file_handle) }
        : std::nullopt;
    if (registration.one_shot) {
        scheduler.schedule(
            SchedulerPhase::postponed,
            process,
            [this, process, registration, file_handle](Scheduler& runtime) {
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
            });
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
    if (enabled) {
        schedule_monitor_publication();
    }
}

void Interpreter::Impl::set_time_format(
    ProcessState& process,
    const TimeFormatControl& operation)
{
    const auto decode = [&](const RegisterId id, const char* name) {
        const auto& value = get_register(process, id);
        const auto word = value.low_word();
        if (value.width() != 32 || word.bval != 0) {
            fail(
                process,
                std::string { "$timeformat " } + name
                    + " must be a known 32-bit value");
        }
        return static_cast<std::uint32_t>(word.aval);
    };
    const auto units = static_cast<std::int32_t>(
        decode(operation.units, "units"));
    const auto precision = decode(operation.precision, "precision");
    const auto minimum_width = decode(
        operation.minimum_width, "minimum width");
    const auto& suffix = get_string_register(process, operation.suffix);
    if (units < -15 || units > 0
        || precision > maximum_string_bytes
        || minimum_width > maximum_string_bytes
        || suffix.size() > maximum_string_bytes) {
        fail(
            process,
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
    auto value = (process.cold().random_state += UINT64_C(0x9e3779b97f4a7c15));
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
    auto& process = get_process(process_id);
    if (kind != RandomKind::urandom_range) {
        return PackedLogic4::from_aval_bval(
            32, next_random(process), 0);
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
        sample = next_random(process);
    } else {
        const auto full_range = UINT64_C(1) << 32U;
        const auto accepted = full_range - full_range % span;
        do {
            sample = next_random(process);
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
        throw std::invalid_argument("SimIR signal assignment width mismatch");
    }
    value = normalize_signal_value(signal_id, std::move(value));
    publish_normalized(signal_id, std::move(value), notify_fanout);
}

void Interpreter::Impl::publish_normalized(
    const SignalId signal_id,
    PackedLogic4 value,
    const bool notify_fanout)
{
    note_signal_transaction(signal_id, notify_fanout);
    auto& signal = signals[signal_id];
    if (signal.initial_value == value) {
        return;
    }
    signal_last_values[signal_id] = std::move(signal.initial_value);
    signal.initial_value = std::move(value);
    refresh_direct_signal_planes(signal_id);
    publish_value_change(signal_id, notify_fanout);
}

void Interpreter::Impl::publish_normalized_word(
    const SignalId signal_id,
    const Logic4Word value,
    const bool notify_fanout)
{
    note_signal_transaction(signal_id, notify_fanout);
    auto& signal = signals[signal_id];
    const auto current = signal.initial_value.unchecked_low_word();
    if (current == value) {
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
    publish_value_change(signal_id, notify_fanout);
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
        || container_object_change_hook) {
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
        && !container_object_change_hook;
}

bool Interpreter::Impl::native_signal_has_runtime_dependency(
    const SignalId signal_id) const noexcept
{
    if (signal_id >= signals.size()
        || native_signal_dependencies_unknown
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

bool Interpreter::Impl::can_publish_native_word_prevalidated(
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

bool Interpreter::Impl::can_publish_native_logic9_word(
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
    if (native_signal_observation_required_hook) {
        if (native_signal_observation_required_hook(signal_id)) {
            return false;
        }
    } else if (signal_change_hook || stored_signal_change_hook
        || driver_change_hook || scalar_signal_change_hook
        || container_object_change_hook) {
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
    if (signal_id >= signals.size()
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
        || container_object_change_hook) {
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
    Logic4Word value)
{
    const bool has_static_fanout = !static_fanout_for(signal_id).empty();
    note_signal_transaction(signal_id, has_static_fanout);
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

    direct_signal_last_aval[signal_id] = current.aval;
    direct_signal_last_bval[signal_id] = current.bval;
    direct_signal_aval[signal_id] = value.aval;
    direct_signal_bval[signal_id] = value.bval;
    const auto wide_offset = direct_wide_signal_offsets[signal_id];
    direct_wide_signal_aval[wide_offset] = value.aval;
    direct_wide_signal_bval[wide_offset] = value.bval;
    direct_signal_materialization_pending[signal_id] = 1U;

    ++signal_value_revisions[signal_id];
    if (signal_value_revisions[signal_id] == 0U) {
        signal_value_revisions[signal_id] = 1U;
        std::ranges::fill(
            container_materialized_revisions, std::nullopt);
    }
    signal_events[signal_id]
        = std::pair { scheduler.now(), scheduler.delta() + 1U };
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
    const bool has_scalar_edge_fanout
        = !static_fanout_indices_for(
               signal_id, EdgeKind::posedge).empty()
        || !static_fanout_indices_for(
               signal_id, EdgeKind::negedge).empty();
    if (width == 1U && has_scalar_edge_fanout) {
        transition = decode_static_transition(
            decode_low(current), decode_low(value));
    }
    notify_static_value_change(signal_id, transition, true);
}

void Interpreter::Impl::publish_native_logic9_word(
    const SignalId signal_id,
    Logic9Word value)
{
    const bool has_static_fanout = !static_fanout_for(signal_id).empty();
    note_signal_transaction(signal_id, has_static_fanout);
    const auto width = signals[signal_id].initial_value.width();
    const auto mask = width == 64U
        ? std::numeric_limits<std::uint64_t>::max()
        : (UINT64_C(1) << width) - UINT64_C(1);
    value.width = width;
    for (auto& plane : value.planes) {
        plane &= mask;
    }
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
    if (signal_value_revisions[signal_id] == 0U) {
        signal_value_revisions[signal_id] = 1U;
        std::ranges::fill(
            container_materialized_revisions, std::nullopt);
    }
    signal_events[signal_id]
        = std::pair { scheduler.now(), scheduler.delta() + 1U };
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
    const bool has_scalar_edge_fanout
        = !static_fanout_indices_for(
               signal_id, EdgeKind::posedge).empty()
        || !static_fanout_indices_for(
               signal_id, EdgeKind::negedge).empty();
    if (width == 1U && has_scalar_edge_fanout) {
        transition = decode_static_transition(
            decode_low(current), decode_low(value));
    }
    notify_static_value_change(signal_id, transition);
}

void Interpreter::Impl::note_signal_transaction(
    const SignalId signal_id,
    const bool notify_fanout)
{
    const bool group_static_fanout = fanout_cohort_grouping_enabled;
    signal_transactions[signal_id] = std::pair { scheduler.now(), scheduler.delta() + 1 };
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
                    queue_static_cohort_next_delta(cohort);
                }
                continue;
            }
            auto& process = get_process(sensitivity.process);
            if (process.waiting_on_static) {
                queue_static_next_delta(sensitivity.process);
            }
        }
    }
}

void Interpreter::Impl::publish_value_change(
    const SignalId signal_id,
    const bool notify_fanout)
{
    auto& signal = signals[signal_id];
    const auto& old_value = signal_last_values[signal_id];
    ++signal_value_revisions[signal_id];
    if (signal_value_revisions[signal_id] == 0U) {
        signal_value_revisions[signal_id] = 1U;
        std::ranges::fill(
            container_materialized_revisions, std::nullopt);
    }
    signal_events[signal_id] = std::pair { scheduler.now(), scheduler.delta() + 1 };
    scheduler.note_signal_change(signal_id);
    evaluate_module_timing_checks(signal_id, old_value, signal.initial_value);
    if (signal_change_hook) {
        signal_change_hook(signal_id, signal.initial_value, scheduler.now());
    }
    if (scalar_signal_change_hook
        && signal.systemverilog_scalar != SystemVerilogScalarKind::None) {
        const auto scalar = decode_systemverilog_scalar_payload(
            signal.initial_value, signal.systemverilog_scalar);
        if (!scalar) {
            throw std::logic_error {
                "SimIR scalar signal published an invalid payload"
            };
        }
        scalar_signal_change_hook(signal_id, scalar.value, scheduler.now());
    }
    if (monitor_watches(signal_id)) {
        schedule_monitor_publication();
    }

    if (notify_fanout) {
        auto transition = StaticTransitionMatches { };
        bool transition_decoded { };
        const bool has_static_edge_fanout
            = !static_fanout_indices_for(
                   signal_id, EdgeKind::posedge).empty()
            || !static_fanout_indices_for(
                   signal_id, EdgeKind::negedge).empty();
        if (old_value.width() == 1U && signal.initial_value.width() == 1U
            && has_static_edge_fanout) {
            transition = decode_static_transition(
                old_value.get(0), signal.initial_value.get(0));
            transition_decoded = true;
        }
        notify_static_value_change(signal_id, transition);

        // Copy because queue_next_delta removes a process from every dynamic list.
        const auto dynamic = dynamic_fanout[signal_id];
        for (const auto& registration : dynamic) {
            if (registration.edge != EdgeKind::any) {
                if (old_value.width() != 1U
                    || signal.initial_value.width() != 1U) {
                    continue;
                }
                if (!transition_decoded) {
                    transition = decode_static_transition(
                        old_value.get(0), signal.initial_value.get(0));
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
                queue_next_delta(registration.process);
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
