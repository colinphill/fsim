// SPDX-License-Identifier: Apache-2.0
#include "simir_internal.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>

namespace fsim::runtime::simir {
namespace {

bool initial_only_writer(const Process& process)
{
    if (process.final || process.postponed || process.reactive
        || process.observed || !process.static_sensitivity.empty()
        || process.operations.empty()
        || !operation_holds<Halt>(process.operations.expanded(
            process.operations.size() - 1U))) {
        return false;
    }
    for (std::size_t index = 0U;
         index + 1U < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        if (!operation_holds<DebugPoint>(operation)
            && !operation_holds<LoadConstant>(operation)
            && !operation_holds<CopyRegister>(operation)
            && !operation_holds<WriteUpdate>(operation)
            && !operation_holds<WriteUpdateSlice>(operation)) {
            return false;
        }
    }
    return true;
}

} // namespace

void Interpreter::Impl::build_private_signal_bridges()
{
    const auto none = std::numeric_limits<std::size_t>::max();
    private_signal_bridges.clear();
    private_signal_bridges.resize(signals.size());
    fused_masked_counts.private_candidates = 0U;
    if (process_profile_enabled || execution_point_hook
        || driver_change_hook || signal_change_hook
        || stored_signal_change_hook || scalar_signal_change_hook
        || container_object_change_hook
        || (native_signal_observation_any_hook
            && native_signal_observation_any_hook())) {
        return;
    }

    // Build the complete elaborated read/write inventory once. Testing every
    // candidate against every process would turn the private graph into a
    // design-size-squared startup pass on large replicated designs.
    std::vector<std::vector<ProcessId>> readers(signals.size());
    std::vector<std::vector<ProcessId>> writers(signals.size());
    std::vector<std::uint8_t> observed(signals.size(), 0U);
    std::vector<std::uint8_t> initial_only(processes.size(), 0U);
    const auto observe = [&](const SignalId signal) {
        if (signal < observed.size()) {
            observed[signal] = 1U;
        }
    };
    for (ProcessId id = 0U; id < processes.size(); ++id) {
        const auto& program = processes[id].program();
        initial_only[id] = initial_only_writer(program);
        for (const auto& sensitivity : program.static_sensitivity) {
            if (sensitivity.edge != EdgeKind::any) {
                observe(sensitivity.signal);
            }
        }
        for (const auto& driver : program.driver_regions) {
            if (driver.signal < writers.size()) {
                writers[driver.signal].push_back(id);
            }
        }
        for (std::size_t index = 0U;
             index < program.operations.size(); ++index) {
            const auto operation = program.operations.expanded(index);
            visit_operation([&](const auto& value) {
                using Type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Type, ReadSignal>) {
                    if (value.signal < readers.size()) {
                        if (value.kind == SignalReadKind::current
                            && value.ticks == 1U
                            && !value.clock && !value.gate) {
                            readers[value.signal].push_back(id);
                        } else {
                            observe(value.signal);
                        }
                    }
                    if (value.clock) {
                        observe(*value.clock);
                    }
                    if (value.gate) {
                        observe(*value.gate);
                    }
                } else if constexpr (std::is_same_v<Type, WriteUpdate>
                    || std::is_same_v<Type, WriteUpdateSlice>
                    || std::is_same_v<Type, WriteProjected>) {
                    if (value.signal < writers.size()) {
                        writers[value.signal].push_back(id);
                    }
                } else if constexpr (std::is_same_v<Type, WaitOn>
                    || std::is_same_v<Type, WaitPla>) {
                    for (const auto signal : value.signals) {
                        observe(signal);
                    }
                } else if constexpr (std::is_same_v<Type, WaitOrder>) {
                    for (const auto signal : value.events) {
                        observe(signal);
                    }
                } else if constexpr (std::is_same_v<Type, MonitorInstall>) {
                    for (const auto& item : value.values) {
                        if (item.kind == MonitorValueKind::signal) {
                            observe(item.signal);
                        }
                    }
                } else if constexpr (std::is_same_v<Type, EventAlias>) {
                    observe(value.target);
                    if (value.has_source) {
                        observe(value.source);
                    }
                } else if constexpr (std::is_same_v<Type, EventTriggered>) {
                    observe(value.event);
                } else if constexpr (requires { value.signal; }) {
                    observe(value.signal);
                }
            }, operation);
        }
    }
    for (auto& ids : readers) {
        std::ranges::sort(ids);
        ids.erase(std::ranges::unique(ids).begin(), ids.end());
    }
    for (auto& ids : writers) {
        std::ranges::sort(ids);
        ids.erase(std::ranges::unique(ids).begin(), ids.end());
    }

    for (std::size_t cohort = 0U; cohort < fused_static_cohorts.size();
         ++cohort) {
        const auto& producer = fused_static_cohorts[cohort];
        if (!producer.certified) {
            continue;
        }
        for (const auto signal : producer.candidate.private_outputs) {
            if (signal >= signals.size()
                || private_signal_bridges[signal].certified
                || observed[signal]
                || signal_transaction_observed[signal]
                || native_signal_has_runtime_dependency(signal)
                || has_dynamic_waits(signal)
                || !signal_container_aliases[signal].empty()
                || forced_values[signal] || forced_driver_values[signal]
                || external_driver_values[signal]
                || monitor_watches(signal)
                || (native_signal_observation_required_hook
                    && native_signal_observation_required_hook(signal))) {
                continue;
            }

            auto bridge = PrivateSignalBridge { };
            bridge.producer_cohort = cohort;
            bool valid = true;
            for (const auto& fanout : static_fanout_for(signal)) {
                if (fanout.edge != EdgeKind::any
                    || fanout.process >= fused_masked_region_by_process.size()) {
                    valid = false;
                    break;
                }
                const auto region_id
                    = fused_masked_region_by_process[fanout.process];
                if (region_id == none
                    || region_id >= fused_masked_regions.size()
                    || !fused_masked_regions[region_id].certified
                    || std::ranges::find(
                        fused_masked_regions[region_id].candidate.outputs,
                        signal) != fused_masked_regions[region_id]
                            .candidate.outputs.end()) {
                    valid = false;
                    break;
                }
                bridge.consumers.push_back(fanout);
                if (std::ranges::find(
                        bridge.consumer_regions, region_id)
                    == bridge.consumer_regions.end()) {
                    bridge.consumer_regions.push_back(region_id);
                }
            }
            if (!valid || bridge.consumers.empty()) {
                continue;
            }

            // Every current body reader must be in the precomputed frontier.
            // Every writer outside the producer must terminate after its
            // initial constant write, leaving its owned bits in committed.
            for (const auto id : readers[signal]) {
                if (std::ranges::find(
                        bridge.consumers, id, &Fanout::process)
                    == bridge.consumers.end()) {
                    valid = false;
                    break;
                }
            }
            for (const auto id : writers[signal]) {
                if (std::ranges::find(
                        producer.candidate.members, id)
                        == producer.candidate.members.end()
                    && !initial_only[id]) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                continue;
            }
            bridge.certified = true;
            private_signal_bridges[signal] = std::move(bridge);
            ++fused_masked_counts.private_candidates;
        }
    }
}

bool Interpreter::Impl::private_signal_bridge_active(
    const SignalId signal) const
{
    if (signal >= private_signal_bridges.size()) {
        return false;
    }
    const auto& bridge = private_signal_bridges[signal];
    if (!bridge.certified || bridge.producer_cohort
            >= fused_static_cohorts.size()) {
        return false;
    }
    const auto& producer = fused_static_cohorts[bridge.producer_cohort];
    if (!producer.certified || !producer.executor
        || signal_transaction_observed[signal]
        || native_signal_has_runtime_dependency(signal)
        || has_dynamic_waits(signal)
        || !signal_container_aliases[signal].empty()
        || monitor_watches(signal)
        || forced_values[signal] || forced_driver_values[signal]
        || external_driver_values[signal]
        || (native_signal_observation_any_hook
            && native_signal_observation_any_hook())
        || (native_signal_observation_required_hook
            && native_signal_observation_required_hook(signal))) {
        return false;
    }
    for (const auto region_id : bridge.consumer_regions) {
        if (region_id >= fused_masked_regions.size()
            || !fused_masked_regions[region_id].certified
            || !fused_masked_regions[region_id].executor) {
            return false;
        }
    }
    return true;
}

void Interpreter::Impl::notify_private_signal_bridge(
    const SignalId signal)
{
    const auto& bridge = private_signal_bridges[signal];
    if (fused_masked_counters_enabled) {
        ++fused_masked_counts.private_local_commits;
        fused_masked_counts.private_fanout_entries_avoided
            += bridge.consumers.size();
    }
    for (const auto& fanout : bridge.consumers) {
        auto& process = get_process(fanout.process);
        process.static_trigger_mask |= fanout.static_trigger_mask;
        if (!process.waiting_on_static || process.halted
            || process.queued) {
            continue;
        }
        queue_static_next_delta(fanout.process);
        if (fused_masked_counters_enabled) {
            ++fused_masked_counts.private_masked_notifications;
        }
    }
}

void Interpreter::Impl::commit_private_owned_signal(
    const SignalId signal, PackedLogic4 value)
{
    if (!private_signal_bridge_active(signal)
        || !owned_driver_active(signal)
        || forced_values[signal] || forced_driver_values[signal]
        || external_driver_values[signal]) {
        commit_resolved(signal, std::move(value));
        return;
    }
    value = normalize_signal_value(signal, std::move(value));
    const bool stored_changed = driven_values[signal] != value;
    driven_values[signal] = value;
    publish_normalized(signal, std::move(value));
    if (stored_changed) {
        publish_container_signal_aliases(signal);
    }
    mark_switch_network_dirty(signal);
    refresh_switch_network();
    if (fused_masked_counters_enabled) {
        ++fused_masked_counts.private_owned_direct_commits;
    }
}

} // namespace fsim::runtime::simir
