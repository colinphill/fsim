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
      : std::numeric_limits<SimulationTick>::max();
}

template <typename Owner>
[[nodiscard]] bool driving(
    Owner& owner,
    const ProcessId process,
    const SignalId signal) {
  if (signal >= owner.signals.size()) {
    throw std::out_of_range("invalid SimIR signal ID");
  }
  const auto regions = owner.processes.program_view(process).driver_regions();
  return std::ranges::any_of(regions, [signal](const auto& region) {
    return region.signal == signal;
  });
}

template <typename Owner>
[[nodiscard]] PackedLogic4 driving_value(
    Owner& owner,
    const ProcessId process,
    const SignalId signal) {
  owner.require_region_forwarding_role_journal_flushed_for_signal(signal);
  if (!driving(owner, process, signal)) {
    throw std::logic_error{
        "process has no driver for signal driving-value query"};
  }
  return owner.current_driver_value(process, signal);
}

}  // namespace fsim::runtime::simir::signal_attribute_detail
