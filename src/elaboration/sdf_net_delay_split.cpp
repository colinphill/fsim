// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/sdf_net_delay_split.hpp"

#include "lowerer_driver_regions.hpp"
#include "systemverilog_template_bindings.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include <string_view>
#include <utility>

namespace fsim::elaboration {

namespace {

using runtime::simir::Operation;
using runtime::simir::Process;
using runtime::simir::ProcessId;
using runtime::simir::SignalId;
using runtime::simir::TransitionDelays;

// The rise, fall and turn-off delay of twelve transition delays in the
// order of a module path (IEEE 1800-2023 30.5.1 Table 30-2); the turn-off
// delay is the smaller of the 0->z and 1->z delays (30.5.2).
TransitionDelays transition_delays(
    const std::vector<runtime::SimulationTick>& table)
{
    return { table[0], table[1], std::min(table[2], table[4]) };
}

bool is_zero(const TransitionDelays& delays)
{
    return delays.rise == 0U && delays.fall == 0U && delays.turnoff == 0U;
}

template <class... Types>
bool holds_any(const Operation& operation)
{
    return ((runtime::simir::operation_get_if<Types>(&operation) != nullptr)
        || ...);
}

// Whether `operation` can stay as it is in a process that does not write a
// renamed signal: a delayed write names only its target.
bool names_only_targets(const Operation& operation)
{
    using namespace runtime::simir;
    return holds_any<WriteAfter, WriteInertial, WriteAfterSlice,
        WriteInertialSlice, WriteAfterDynamicSlice, WriteInertialDynamicSlice,
        WriteAfterDynamicPartSlice, WriteInertialDynamicPartSlice,
        WriteDelayed>(operation);
}

// Rewrites the reads of the signals in `renames` in `process`. False when the
// process writes one of them (but a port adapter, which only drives the port
// formal and reads the actual) or has an operation that names signals in a
// way the rewriting does not cover; `process` is then unspecified.
bool rename_reads(Process& process,
    const std::map<SignalId, SignalId>& renames, bool& changed)
{
    using namespace runtime::simir;
    if (!process.operations.signal_remap().empty()) {
        return false;
    }
    const bool writes_renamed = std::ranges::any_of(process.driver_regions,
        [&](const Process::DriverRegion& region) {
            return renames.contains(region.signal);
        });
    if (writes_renamed || (process.switch_target
            && renames.contains(*process.switch_target))) {
        return process.name.find(".port_actual_") != std::string::npos
            && !process.switch_target;
    }
    const auto rename = [&](SignalId& signal) {
        if (const auto found = renames.find(signal); found != renames.end()) {
            signal = found->second;
        }
        return true;
    };
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        auto operation = process.operations.expanded(index);
        if (auto* wait = operation_get_if<WaitOn>(&operation)) {
            bool waits { };
            for (auto& signal : wait->signals) {
                if (renames.contains(signal)) {
                    rename(signal);
                    waits = true;
                }
            }
            if (waits) {
                process.operations.replace(index, std::move(operation));
                changed = true;
            }
            continue;
        }
        auto trial = operation;
        bool operation_changed { };
        if (remap_operation_signals(
                trial, rename, std::nullopt, &operation_changed)) {
            if (operation_changed) {
                process.operations.replace(index, std::move(trial));
                changed = true;
            }
            continue;
        }
        if (!names_only_targets(operation)) {
            return false;
        }
    }
    bool sensitivity_changed { };
    for (auto& sensitivity : process.static_sensitivity) {
        const auto before = sensitivity.signal;
        rename(sensitivity.signal);
        sensitivity_changed = sensitivity_changed
            || sensitivity.signal != before;
    }
    if (sensitivity_changed) {
        normalize_sensitivities(process.static_sensitivity);
        // The trigger masks index the old sensitivity order.
        process.static_trigger_regions.clear();
        changed = true;
    }
    for (auto* const terminal :
        { &process.switch_source, &process.switch_control }) {
        if (terminal->has_value()) {
            const auto before = **terminal;
            rename(**terminal);
            changed = changed || **terminal != before;
        }
    }
    return true;
}

struct LoadKey {
    SignalId signal { };
    std::string instance;
    friend bool operator<(const LoadKey& left, const LoadKey& right)
    {
        return std::tie(left.signal, left.instance)
            < std::tie(right.signal, right.instance);
    }
};

struct Load {
    std::vector<std::size_t> records;
    TransitionDelays delays;
    std::string object;
    // The delay each source has had since the records applied in order of
    // occurrence (IEEE 1364-2005 16.3): a PORT record sets every source and
    // the delay of a source that no INTERCONNECT names, an INTERCONNECT only
    // its own source (16.2.4).
    std::map<std::string, TransitionDelays> by_source;
    TransitionDelays port_delays;
    bool differing { };
};

// The load's delay once all its records are applied: the delay every source
// has, or `differing` when the sources have different delays.
void settle(Load& load)
{
    if (load.by_source.empty()) {
        load.delays = load.port_delays;
        return;
    }
    load.delays = load.by_source.begin()->second;
    for (const auto& [source, delays] : load.by_source) {
        load.differing = load.differing || !(delays == load.delays);
    }
}

} // namespace

SdfNetDelaySplitResult split_sdf_net_delays(ElaboratedDesignState& state)
{
    SdfNetDelaySplitResult result;
    // The loads: the PORT delays and the INTERCONNECT delays that end at a
    // port, in the instance behind the port. The records are in order of
    // occurrence and apply in that order (IEEE 1364-2005 16.3): a PORT delay
    // after an INTERCONNECT replaces the delay of every source, an
    // INTERCONNECT after a PORT changes only the delay from its own source
    // (16.2.4 gives each source and load pair its own delay).
    std::map<LoadKey, Load> loads;
    for (std::size_t index = 0U; index < state.sdf_net_delays.size(); ++index) {
        auto& record = state.sdf_net_delays[index];
        record.delayed = false;
        const auto& terminal = record.destination;
        const auto dot = terminal.object.rfind('.');
        if (record.delays.size() != 12U) {
            continue;
        }
        const auto delays = transition_delays(record.delays);
        if (dot == std::string::npos) {
            if (!is_zero(delays)) {
                result.skipped.push_back(record.identity
                    + ": the load is a top-level port, which no instance "
                      "reads");
            }
            continue;
        }
        if (terminal.select_width != 0U) {
            if (!is_zero(delays)) {
                result.skipped.push_back(record.identity
                    + ": the load is a bit or part select of a port");
            }
            continue;
        }
        if (terminal.signal >= state.signals.size()) {
            continue;
        }
        LoadKey key { terminal.signal, terminal.object.substr(0U, dot) };
        auto& load = loads[key];
        load.records.push_back(index);
        load.object = terminal.object;
        if (record.kind == SdfNetDelayKind::port) {
            load.port_delays = delays;
            for (auto& entry : load.by_source) {
                entry.second = delays;
            }
        } else {
            load.by_source.insert_or_assign(record.source.object
                    + '/' + std::to_string(record.source.select_width),
                delays);
        }
    }
    for (auto entry = loads.begin(); entry != loads.end();) {
        settle(entry->second);
        if (entry->second.differing) {
            for (const auto record : entry->second.records) {
                result.skipped.push_back(state.sdf_net_delays[record].identity
                    + ": the sources of the load have different delays, and "
                      "the net split delays the net as a whole");
            }
            entry = loads.erase(entry);
        } else {
            ++entry;
        }
    }
    std::erase_if(loads,
        [](const auto& entry) { return is_zero(entry.second.delays); });
    if (loads.empty()) {
        return result;
    }

    // Each process takes the renames of the loads whose instance holds it
    // (nearest instance first, so the nearest wins when nested instances name
    // one net). A load whose instance has a process the rewriting cannot
    // cover keeps its net, as does one nothing reads; the pass then repeats
    // without it, so that no process is renamed for a dropped load.
    std::vector<std::pair<std::size_t, Process>> rewritten;
    std::map<LoadKey, SignalId> copies;
    for (;;) {
        std::map<std::string_view, std::vector<const LoadKey*>> by_instance;
        copies.clear();
        for (const auto& [key, load] : loads) {
            by_instance[key.instance].push_back(&key);
            copies.emplace(key,
                static_cast<SignalId>(state.signals.size() + copies.size()));
        }
        rewritten.clear();
        std::map<LoadKey, std::size_t> readers;
        std::map<LoadKey, bool> rejected;
        for (std::size_t index = 0U; index < state.processes.size();
            ++index) {
            const auto& process = state.processes[index];
            // The delayed copies of an earlier split read the net itself.
            if (process.name.ends_with(".$mipd_driver")) {
                continue;
            }
            std::map<SignalId, SignalId> renames;
            std::vector<const LoadKey*> owners;
            // The instances that hold the process, nearest first.
            std::string_view name = process.name;
            for (auto dot = name.rfind('.'); dot != std::string_view::npos;
                dot = name.rfind('.')) {
                name = name.substr(0U, dot);
                const auto found = by_instance.find(name);
                if (found == by_instance.end()) {
                    continue;
                }
                for (const auto* key : found->second) {
                    if (renames.emplace(key->signal, copies.at(*key)).second) {
                        owners.push_back(key);
                    }
                }
            }
            if (renames.empty()) {
                continue;
            }
            auto work = process;
            bool changed { };
            if (!rename_reads(work, renames, changed)) {
                for (const auto* key : owners) {
                    rejected[*key] = true;
                }
                continue;
            }
            if (changed) {
                for (const auto* key : owners) {
                    ++readers[*key];
                }
                rewritten.emplace_back(index, std::move(work));
            }
        }
        bool dropped { };
        for (auto entry = loads.begin(); entry != loads.end();) {
            const bool unsupported = rejected.contains(entry->first);
            if (!unsupported && readers.contains(entry->first)) {
                ++entry;
                continue;
            }
            for (const auto record : entry->second.records) {
                result.skipped.push_back(
                    state.sdf_net_delays[record].identity
                    + (unsupported
                            ? ": a process of the load instance writes the "
                              "port or uses an operation the net split does "
                              "not cover"
                            : ": no process of the load instance reads the "
                              "port"));
            }
            entry = loads.erase(entry);
            dropped = true;
        }
        if (loads.empty()) {
            return result;
        }
        if (!dropped) {
            break;
        }
    }
    for (auto& [index, process] : rewritten) {
        state.processes[index] = std::move(process);
    }

    // The delayed copies: a signal for each load, and a process that writes
    // the net's value to it after the transition delay, like a continuous
    // assignment with a delay.
    using namespace runtime::simir;
    for (auto& [key, load] : loads) {
        const auto id = copies.at(key);
        auto signal = state.signals[key.signal];
        signal.name = load.object + ".$mipd";
        signal.implicit_driver.reset();
        signal.charge_strength.reset();
        signal.charge_decay.reset();
        auto info = state.signal_info[key.signal];
        info.id = id;
        info.name = signal.name;
        info.is_port = false;
        info.direction = frontend::PortDirection::Unknown;
        state.signals.push_back(std::move(signal));
        state.signal_info.push_back(std::move(info));
        const auto net_signal = key.signal;
        const auto width = state.signals[net_signal].initial_value.width();
        Process process;
        process.id = static_cast<ProcessId>(state.processes.size());
        process.name = load.object + ".$mipd_driver";
        process.scheduling_domain = ProcessSchedulingDomain::systemverilog;
        process.static_sensitivity.push_back({ net_signal, EdgeKind::any });
        process.operations.emplace_back(ReadSignal { 0U, net_signal });
        process.operations.emplace_back(WriteInertial { id, 0U, load.delays,
            SignalUpdateDomain::systemverilog_active });
        process.operations.emplace_back(WaitSensitivity { });
        process.operations.emplace_back(Jump { 0U });
        process.register_count = 1U;
        process.register_value_kinds.push_back(
            state.signals[net_signal].value_kind);
        const std::array<std::size_t, 1U> widths { width };
        process.driver_regions
            = elaboration_detail::collect_driver_regions(process, widths);
        for (const auto record : load.records) {
            auto& declared = state.sdf_net_delays[record];
            declared.delayed = true;
            declared.load_signal = id;
            declared.driver = process.id;
        }
        state.processes.push_back(std::move(process));
        ++result.split;
    }
    state.signal_driver_inventory.reset();
    return result;
}

} // namespace fsim::elaboration
