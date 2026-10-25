// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace fsim::runtime::simir::signal_attribute_detail {

/// The time since a (tick, delta) stamp, or TIME'HIGH when it never
/// happened (IEEE 1076-2008 16.2.4).
template <typename Owner, typename Stamp>
[[nodiscard]] SimulationTick elapsed_since(
    const Owner& owner, const Stamp& stamp) {
    return stamp ? owner.scheduler.now() - stamp->first
                 : static_cast<SimulationTick>(
                       std::numeric_limits<std::int64_t>::max());
}

/// Whether a (tick, delta) stamp is in the current simulation cycle.
template <typename Owner, typename Stamp>
[[nodiscard]] bool in_current_delta(const Owner& owner, const Stamp& stamp) {
    return stamp && stamp->first == owner.scheduler.now()
        && stamp->second == owner.scheduler.delta();
}

template <typename Owner>
[[nodiscard]] SimulationTick last_active(
    Owner& owner, const SignalId signal) {
    if (signal >= owner.signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    return elapsed_since(owner, owner.signal_transactions[signal]);
}

template <typename Owner>
[[nodiscard]] bool driving(
    Owner& owner,
    const ProcessId process,
    const SignalId signal,
    const std::optional<SignalSelection> selection = std::nullopt) {
    if (signal >= owner.signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    // A driver disconnected by a null transaction is not driving (IEEE
    // 1076-2008 16.2.4, 14.7.2).
    if constexpr (requires { owner.vhdl_disconnected_drivers; }) {
        if (owner.vhdl_disconnected_drivers.contains({ signal, process })) {
            return false;
        }
    }
    const auto regions = owner.processes.program_view(process).driver_regions();
    // A composite signal is driving only when every scalar subelement is
    // (16.2.4).
    const auto begin = selection
        ? static_cast<std::size_t>(selection->offset)
        : std::size_t { };
    const auto end = selection
        ? begin + selection->width
        : static_cast<std::size_t>(
              owner.signals[signal].initial_value.width());
    auto covered_until = begin;
    while (covered_until < end) {
        auto next = covered_until;
        for (const auto& region : regions) {
            if (region.signal != signal) {
                continue;
            }
            if (region.whole) {
                return true;
            }
            const auto region_begin = static_cast<std::size_t>(region.offset);
            const auto region_end = region_begin + region.width;
            if (region_begin <= covered_until && region_end > next) {
                next = std::min(region_end, end);
            }
        }
        if (next == covered_until) {
            return false;
        }
        covered_until = next;
    }
    return true;
}

template <typename Owner>
[[nodiscard]] PackedLogic4 driving_value(
    Owner& owner,
    const ProcessId process,
    const SignalId signal,
    const std::optional<SignalSelection> selection = std::nullopt) {
    if (!driving(owner, process, signal, selection)) {
        throw std::logic_error {
            "process has no driver for signal driving-value query"
        };
    }
    auto value = owner.current_driver_value(process, signal);
    if (selection) {
        value = value.extract_bits(selection->offset, selection->width);
    }
    return value;
}

} // namespace fsim::runtime::simir::signal_attribute_detail
