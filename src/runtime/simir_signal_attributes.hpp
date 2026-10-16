// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace fsim::runtime::simir::signal_attribute_detail {

template <typename Owner>
[[nodiscard]] SimulationTick last_active(
    Owner& owner, const SignalId signal) {
    if (signal >= owner.signals.size()) {
        throw std::out_of_range("invalid SimIR signal ID");
    }
    const auto& transaction = owner.signal_transactions[signal];
    return transaction
        ? owner.scheduler.now() - transaction->first
        // Never active: TIME'HIGH (IEEE 1076-2008 16.2.4).
        : static_cast<SimulationTick>(
              std::numeric_limits<std::int64_t>::max());
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
    const auto regions = owner.processes.program_view(process).driver_regions();
    if (!selection) {
        return std::ranges::any_of(regions, [signal](const auto& region) {
            return region.signal == signal;
        });
    }
    const auto begin = static_cast<std::size_t>(selection->offset);
    const auto end = begin + selection->width;
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
    owner.require_region_forwarding_role_journal_flushed_for_signal(signal);
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
