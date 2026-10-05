// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

namespace {

constexpr std::size_t maximum_retained_samples { 4097U };

[[nodiscard]] std::size_t sampled_history_capacity(
    const ReadSignal& operation)
{
    if (operation.kind == SignalReadKind::past) {
        if (operation.ticks >= maximum_retained_samples - 1U) {
            return maximum_retained_samples;
        }
        return static_cast<std::size_t>(operation.ticks) + 1U;
    }
    if (operation.kind == SignalReadKind::rose
        || operation.kind == SignalReadKind::fell
        || operation.kind == SignalReadKind::stable
        || operation.kind == SignalReadKind::changed) {
        return 2U;
    }
    return 1U;
}

[[nodiscard]] bool sampled_history_kind(const SignalReadKind kind)
{
    return kind == SignalReadKind::sampled
        || kind == SignalReadKind::rose
        || kind == SignalReadKind::fell
        || kind == SignalReadKind::stable
        || kind == SignalReadKind::changed
        || kind == SignalReadKind::past;
}

} // namespace

void Interpreter::Impl::ensure_sampled_history_capacity(
    SampledHistoryState& history,
    const std::size_t capacity,
    const PackedLogic4& initial_value)
{
    if (history.values.size() >= capacity) {
        return;
    }
    std::vector<PackedLogic4> expanded(capacity, initial_value);
    if (history.value_count != 0U) {
        const auto old_capacity = history.values.size();
        for (std::size_t index = 0U;
             index < history.value_count;
             ++index) {
            expanded[index] = std::move(
                history.values[(history.first_value + index) % old_capacity]);
        }
    }
    history.values = std::move(expanded);
    history.first_value = 0U;
}

const PackedLogic4& Interpreter::Impl::sampled_history_value(
    const SampledHistoryState& history,
    const std::size_t index)
{
    if (index >= history.value_count || history.values.empty()) {
        throw std::logic_error { "sampled history index is out of range" };
    }
    return history.values[
        (history.first_value + index) % history.values.size()];
}

Interpreter::Impl::SampledHistorySlot
Interpreter::Impl::make_sampled_history_slot(
    const SimulationTick time,
    const ProcessSchedulingDomain process_domain,
    const SignalChangeOrigin origin,
    const std::uint64_t generic_delta)
{
    auto slot = SampledHistorySlot {
        time,
        process_domain,
        process_domain,
        SchedulerPhase::active,
        0U,
        0U
    };
    if (process_domain == ProcessSchedulingDomain::systemverilog) {
        slot.event_domain = ProcessSchedulingDomain::systemverilog;
        slot.event_phase = SchedulerPhase::active;
    } else {
        slot.event_domain = origin.process_domain;
        slot.event_phase = origin.phase;
        slot.generic_delta = generic_delta;
    }
    return slot;
}

bool Interpreter::Impl::same_sampled_history_slot(
    const SampledHistorySlot& previous,
    const SampledHistorySlot& current)
{
    if (current.process_domain == ProcessSchedulingDomain::systemverilog) {
        return previous.time == current.time
            && previous.process_domain == current.process_domain;
    }
    return previous.time == current.time
        && previous.process_domain == current.process_domain
        && previous.event_domain == current.event_domain
        && previous.generic_delta == current.generic_delta;
}

void Interpreter::Impl::append_sampled_history(
    SampledHistoryState& history,
    const SampledHistorySlot& slot,
    const PackedLogic4& value)
{
    if (history.values.empty()) {
        throw std::logic_error {
            "sampled history storage was not prepared before publication"
        };
    }
    const auto capacity = history.values.size();
    if (history.value_count < capacity) {
        const auto next = (history.first_value + history.value_count) % capacity;
        auto& destination = history.values[next];
        if (destination.width() != value.width()
            || destination.is_logic9() != value.is_logic9()) {
            throw std::logic_error {
                "sampled history changed value shape after preallocation"
            };
        }
        destination = value;
        ++history.value_count;
    } else {
        auto& destination = history.values[history.first_value];
        if (destination.width() != value.width()
            || destination.is_logic9() != value.is_logic9()) {
            throw std::logic_error {
                "sampled history changed value shape after preallocation"
            };
        }
        destination = value;
        history.first_value = (history.first_value + 1U) % capacity;
    }
    history.last_slot = slot;
}

void Interpreter::Impl::build_sampled_history_clock_index()
{
    sampled_histories.clear();
    sampled_history_keys_by_clock.clear();
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        if (processes.is_compact_constant(id)) {
            // Every compact constant shape is read-free; startup-write bank
            // rows therefore contribute no sampled clock history.
            continue;
        }
        const auto program = processes.program_view(id);
        if (program.scheduling_domain()
            != ProcessSchedulingDomain::systemverilog) {
            continue;
        }
        // Scan the full instruction array. Fork branches refer to PCs in this
        // same program, so their statically present reads are indexed too.
        for (std::size_t instruction = 0U;
             instruction < program.operations().size();
             ++instruction) {
            const auto expanded = program.operations().expanded(instruction);
            const auto* read = operation_get_if<ReadSignal>(&expanded);
            if (read == nullptr || !read->clock
                || !sampled_history_kind(read->kind)
                || read->signal >= signals.size()
                || *read->clock >= signals.size()
                || (read->gate && *read->gate >= signals.size())) {
                continue;
            }
            if (sampled_history_keys_by_clock.empty()) {
                sampled_history_keys_by_clock.resize(signals.size());
            }
            const SampledHistoryKey key {
                read->signal,
                read->clock,
                read->clock_edge,
                read->gate,
                program.scheduling_domain()
            };
            if (read->signal >= sampled_defaults.size()) {
                continue;
            }
            const auto [history, inserted]
                = sampled_histories.try_emplace(key);
            ensure_sampled_history_capacity(
                history->second,
                sampled_history_capacity(*read),
                sampled_defaults[read->signal]);
            if (inserted) {
                sampled_history_keys_by_clock[*read->clock].push_back(key);
            }
        }
    }
}

void Interpreter::Impl::capture_sampled_history_clock(
    const SignalId clock,
    const std::uint64_t generic_delta,
    const SignalChangeOrigin origin)
{
    if (clock >= sampled_history_keys_by_clock.size()
        || sampled_history_keys_by_clock[clock].empty()
        || clock >= signal_last_values.size()
        || clock >= signals.size()) {
        return;
    }
    const auto& previous_clock = logical_signal_last_value(clock);
    const auto& current_clock = logical_signal_value(clock);
    if (previous_clock.width() == 0U || current_clock.width() == 0U) {
        return;
    }
    if (previous_clock == current_clock) {
        return;
    }
    for (const auto& key : sampled_history_keys_by_clock[clock]) {
        const auto edge_matches_clock = key.edge == SampledClockEdge::any
            ? true
            : edge_matches(
                key.edge == SampledClockEdge::positive
                    ? EdgeKind::posedge
                    : EdgeKind::negedge,
                previous_clock.get(0U),
                current_clock.get(0U));
        if (!edge_matches_clock
            || key.signal >= sampled_values.size()
            || key.signal >= sampled_defaults.size()
            || (key.gate
                && (*key.gate >= sampled_values.size()
                    || sampled_values[*key.gate].width() == 0U))) {
            continue;
        }
        if (key.gate
            && sampled_values[*key.gate].get(0U) != Logic4::one) {
            continue;
        }
        auto& history = sampled_histories.at(key);
        const auto slot = make_sampled_history_slot(
            scheduler.now(), key.process_domain, origin, generic_delta);
        if (history.last_slot
            && same_sampled_history_slot(*history.last_slot, slot)) {
            continue;
        }
        append_sampled_history(
            history, slot, sampled_values[key.signal]);
    }
}

void Interpreter::Impl::execute_sampled_read(
    ProcessState& process,
    const ReadSignal& operation)
{
    (void)get_signal(operation.signal);
    if (operation.kind == SignalReadKind::current) {
        fail(process, "current signal read reached the sampled-read service");
    }
    const auto has_sampled_value = [this](const SignalId signal) {
        return signal < sampled_values.size()
            && signal < sampled_defaults.size()
            && (sampled_value_dependencies_unknown
                || (signal < sampled_value_dependency_mask.size()
                    && sampled_value_dependency_mask[signal] != 0U));
    };
    if (!has_sampled_value(operation.signal)) {
        fail(process, "sampled signal state is unavailable");
    }
    const auto& slot_value = sampled_values[operation.signal];
    if (operation.kind == SignalReadKind::future
        || operation.kind == SignalReadKind::rising
        || operation.kind == SignalReadKind::falling
        || operation.kind == SignalReadKind::steady
        || operation.kind == SignalReadKind::changing) {
        const auto& future = logical_signal_value(operation.signal);
        if (operation.kind == SignalReadKind::future) {
            write_process_register(process, operation.destination, future);
            return;
        }
        const auto equal = future == slot_value;
        const auto future_lsb = future.get(0U);
        const auto sampled_lsb = slot_value.get(0U);
        const auto value = operation.kind == SignalReadKind::rising
            ? future_lsb == Logic4::one && sampled_lsb != Logic4::one
            : operation.kind == SignalReadKind::falling
            ? future_lsb == Logic4::zero && sampled_lsb != Logic4::zero
            : operation.kind == SignalReadKind::steady
            ? equal
            : !equal;
        write_process_register(
            process,
            operation.destination,
            PackedLogic4(1U, value ? Logic4::one : Logic4::zero));
        return;
    }
    if (operation.kind == SignalReadKind::sampled && !operation.clock) {
        write_process_register(process, operation.destination, slot_value);
        return;
    }
    if (operation.ticks == 0U) {
        fail(process, "sampled history depth is zero");
    }
    if (operation.clock) {
        if (!has_sampled_value(*operation.clock)
            || *operation.clock >= signal_events.size()
            || *operation.clock >= signal_event_scheduling_stamps.size()) {
            fail(process, "sampled clock state is unavailable");
        }
        if (logical_signal_value(*operation.clock).width() == 0U
            || logical_signal_last_value(*operation.clock).width() == 0U) {
            fail(process, "sampled clock must have a nonzero width");
        }
    }
    if (operation.gate) {
        if (!has_sampled_value(*operation.gate)) {
            fail(process, "sampled gating state is unavailable");
        }
        if (sampled_values[*operation.gate].width() == 0U) {
            fail(process, "sampled gate must have a nonzero width");
        }
    }
    const SampledHistoryKey key {
        operation.signal,
        operation.clock,
        operation.clock_edge,
        operation.gate,
        process.program().scheduling_domain()
    };
    auto& history = sampled_histories[key];
    ensure_sampled_history_capacity(
        history,
        sampled_history_capacity(operation),
        sampled_defaults[operation.signal]);
    const auto process_domain = process.program().scheduling_domain();
    auto slot = make_sampled_history_slot(
        scheduler.now(), process_domain, SignalChangeOrigin { }, 0U);
    bool sample = !operation.clock;
    if (operation.clock) {
        const auto& event = signal_events[*operation.clock];
        const auto& stamp
            = signal_event_scheduling_stamps[*operation.clock];
        const bool systemverilog_history
            = process_domain == ProcessSchedulingDomain::systemverilog;
        const bool same_systemverilog_domain
            = systemverilog_history
            && stamp.origin.process_domain
                == ProcessSchedulingDomain::systemverilog;
        sample = event && event->first == scheduler.now()
            && (same_systemverilog_domain
                    || event->second == scheduler.delta());
        slot = make_sampled_history_slot(
            scheduler.now(),
            process_domain,
            stamp.origin,
            event ? event->second : 0U);
        if (sample && operation.clock_edge != SampledClockEdge::any) {
            const auto edge = operation.clock_edge == SampledClockEdge::positive
                ? EdgeKind::posedge
                : EdgeKind::negedge;
            sample = edge_matches(
                edge,
                logical_signal_last_value(*operation.clock).get(0U),
                logical_signal_value(*operation.clock).get(0U));
        }
    }
    if (sample && operation.gate) {
        sample = sampled_values[*operation.gate].get(0U) == Logic4::one;
    }
    const bool same_sampled_time_step = history.last_slot
        && same_sampled_history_slot(*history.last_slot, slot);
    if (sample && !same_sampled_time_step) {
        append_sampled_history(history, slot, slot_value);
    }
    const bool current_time_step_has_sample
        = sample || same_sampled_time_step;
    const auto& current = history.value_count == 0U
        ? sampled_defaults[operation.signal]
        : sampled_history_value(history, history.value_count - 1U);
    if (operation.kind == SignalReadKind::sampled) {
        write_process_register(process, operation.destination, current);
        return;
    }
    const auto& previous = history.value_count < 2U
        ? sampled_defaults[operation.signal]
        : sampled_history_value(history, history.value_count - 2U);
    PackedLogic4 result;
    if (operation.kind == SignalReadKind::past) {
        const auto current_sample_count
            = current_time_step_has_sample ? std::size_t { 1U } : std::size_t { };
        const auto prior_sample_count
            = history.value_count - current_sample_count;
        result = prior_sample_count >= operation.ticks
            ? sampled_history_value(
                history, prior_sample_count - operation.ticks)
            : sampled_defaults[operation.signal];
    } else {
        const auto equal = current == previous;
        const auto current_lsb = current.get(0U);
        const auto previous_lsb = previous.get(0U);
        const auto value = operation.kind == SignalReadKind::rose
            ? current_lsb == Logic4::one
                && previous_lsb != Logic4::one
            : operation.kind == SignalReadKind::fell
            ? current_lsb == Logic4::zero
                && previous_lsb != Logic4::zero
            : operation.kind == SignalReadKind::stable
            ? equal
            : !equal;
        result = PackedLogic4(1U, value ? Logic4::one : Logic4::zero);
    }
    write_process_register(process, operation.destination, result);
}

} // namespace fsim::runtime::simir
