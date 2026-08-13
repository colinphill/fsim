// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

namespace fsim::runtime::simir {

void Interpreter::Impl::execute_sampled_read(
    ProcessState& process,
    const ReadSignal& operation)
{
    (void)get_signal(operation.signal);
    if (operation.kind == SignalReadKind::current) {
        fail(process, "current signal read reached the sampled-read service");
    }
    if (operation.signal >= sampled_values.size()
        || operation.signal >= sampled_defaults.size()) {
        fail(process, "sampled signal state is unavailable");
    }
    const auto& slot_value = sampled_values[operation.signal];
    if (operation.kind == SignalReadKind::future
        || operation.kind == SignalReadKind::rising
        || operation.kind == SignalReadKind::falling
        || operation.kind == SignalReadKind::steady
        || operation.kind == SignalReadKind::changing) {
        const auto& future = get_signal(operation.signal).initial_value;
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
    const SampledHistoryKey key {
        operation.signal,
        operation.clock,
        operation.clock_edge,
        operation.gate
    };
    auto& history = sampled_histories[key];
    const auto slot = operation.clock
        ? std::pair { scheduler.now(), scheduler.delta() }
        : std::pair { scheduler.now(), std::uint64_t { 0 } };
    bool sample = !operation.clock;
    if (operation.clock) {
        if (*operation.clock >= sampled_values.size()
            || *operation.clock >= signal_events.size()) {
            fail(process, "sampled clock state is unavailable");
        }
        const auto& event = signal_events[*operation.clock];
        sample = event && event->first == scheduler.now()
            && event->second == scheduler.delta();
        if (sample && operation.clock_edge != SampledClockEdge::any) {
            const auto edge = operation.clock_edge == SampledClockEdge::positive
                ? EdgeKind::posedge
                : EdgeKind::negedge;
            sample = edge_matches(
                edge,
                signal_last_values[*operation.clock].get(0U),
                get_signal(*operation.clock).initial_value.get(0U));
        }
    }
    if (sample && operation.gate) {
        if (*operation.gate >= sampled_values.size()) {
            fail(process, "sampled gating state is unavailable");
        }
        sample = sampled_values[*operation.gate].get(0U) == Logic4::one;
    }
    if (sample && history.last_slot != slot) {
        history.values.push_back(slot_value);
        constexpr std::size_t maximum_retained_samples { 4097U };
        while (history.values.size() > maximum_retained_samples) {
            history.values.pop_front();
        }
        history.last_slot = slot;
    }
    const auto& current = history.values.empty()
        ? sampled_defaults[operation.signal]
        : history.values.back();
    if (operation.kind == SignalReadKind::sampled) {
        write_process_register(process, operation.destination, current);
        return;
    }
    const auto& previous = history.values.size() < 2U
        ? sampled_defaults[operation.signal]
        : history.values[history.values.size() - 2U];
    PackedLogic4 result;
    if (operation.kind == SignalReadKind::past) {
        result = history.values.size() > operation.ticks
            ? history.values[history.values.size() - operation.ticks - 1U]
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
