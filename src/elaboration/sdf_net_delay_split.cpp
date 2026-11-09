// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/sdf_net_delay_split.hpp"

#include "lowerer_driver_regions.hpp"
#include "sdf_net_delay_dual_write.hpp"
#include "systemverilog_template_bindings.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>
#include <string_view>
#include <utility>

namespace fsim::elaboration {

namespace {

using dual_write::insert_after;
using dual_write::net_writes;
using dual_write::write_target;
using runtime::simir::Operation;
using runtime::simir::Process;
using runtime::simir::ProcessId;
using runtime::simir::NetDelayTable;
using runtime::simir::SignalId;

// The delays and pulse limits of a record, filled in by the module path
// rules (sdf_net_delay_table).
NetDelayTable net_delay_table(const SdfNetDelayInfo& record)
{
    return sdf_net_delay_table(record.delays, record.reject, record.error);
}

bool is_zero(const NetDelayTable& table)
{
    const auto zero = [](const runtime::SimulationTick tick) {
        return tick == 0U;
    };
    return std::ranges::all_of(table.delay, zero)
        && std::ranges::all_of(table.reject, zero)
        && std::ranges::all_of(table.error, zero);
}

template <class... Types>
bool holds_any(const Operation& operation)
{
    return ((runtime::simir::operation_get_if<Types>(&operation) != nullptr)
        || ...);
}

// Whether the process name `name` lies below the instance path `instance`.
bool below(const std::string_view name, const std::string_view instance)
{
    return name.size() > instance.size() && name.starts_with(instance)
        && name[instance.size()] == '.';
}

std::string_view leaf_of(const std::string_view name)
{
    const auto dot = name.rfind('.');
    return dot == std::string_view::npos ? name : name.substr(dot + 1U);
}

// A port connection process: the elaborator's adapter between a port formal
// and its actual (`port_actual_N` drives a formal from its actual,
// `port_result_N` an actual from its formal) and the bidirectional
// connection of an inout formal to a select or concatenation of its actual
// (`$inout_N`).
bool is_connection(const Process& process)
{
    const auto leaf = leaf_of(process.name);
    return leaf.starts_with("port_actual_") || leaf.starts_with("port_result_")
        || leaf.starts_with("$inout_");
}

// The signals a connection process takes its value from: what it reads (its
// sensitivity) and the terminals a switch conducts from.
std::vector<SignalId> connection_inputs(const Process& process)
{
    std::vector<SignalId> result;
    for (const auto& sensitivity : process.static_sensitivity) {
        result.push_back(sensitivity.signal);
    }
    if (process.switch_source) {
        result.push_back(*process.switch_source);
    }
    if (process.switch_bidirectional && process.switch_target) {
        result.push_back(*process.switch_target);
    }
    if (process.switch_control) {
        result.push_back(*process.switch_control);
    }
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

// Every signal a connection process names: its inputs and what it drives.
std::vector<SignalId> connection_signals(const Process& process)
{
    auto result = connection_inputs(process);
    for (const auto& region : process.driver_regions) {
        result.push_back(region.signal);
    }
    if (process.switch_target) {
        result.push_back(*process.switch_target);
    }
    if (process.switch_source) {
        result.push_back(*process.switch_source);
    }
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

// Whether a port connection process only passes bits of its inputs on: it
// reads signals, selects, concatenates and writes, with no computation, so
// that its output is made of the bits of its inputs and a copy of it that
// reads the shadows of the inputs makes the shadows of the output (a
// connection of the net, not a driver). A connection whose actual is an
// expression is a driver, an implicit continuous assignment (IEEE
// 1800-2023 23.3.3.2).
bool is_transparent(const Process& process)
{
    using namespace runtime::simir;
    if (!is_connection(process) || connection_inputs(process).empty()) {
        return false;
    }
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        const auto operation = process.operations.expanded(index);
        if (!holds_any<ReadSignal, LoadConstant, CopyRegister, Extract, Insert,
                Concatenate, WriteBlocking, WriteUpdate, WriteBlockingSlice,
                WriteUpdateSlice, WaitSensitivity, Jump, Halt, DebugPoint>(
                operation)) {
            return false;
        }
    }
    return true;
}

// Whether every operation of `process` can have its signals remapped.
bool can_remap(const Process& process)
{
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        auto operation = process.operations.expanded(index);
        if (!remap_operation_signals(
                operation, [](SignalId&) { return true; }, std::nullopt)) {
            return false;
        }
    }
    return true;
}

// Gives `process` a list of operations of its own, with the signals of its
// instance expanded.
void flatten(Process& process)
{
    if (process.operations.signal_remap().empty()) {
        return;
    }
    runtime::simir::OperationList flat;
    flat.reserve(process.operations.size());
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        flat.push_back(process.operations.expanded(index));
    }
    process.operations = std::move(flat);
}

// Gives the reads of `operation` the renames (a read, a wait, a monitor, the
// target of a force or a release, a strength display). A write keeps its
// target: the driver of the load instance drives the net itself, and its
// copy by a dual write. False when the operation names a renamed signal in a
// way the renaming does not cover.
bool rename_operation(Operation& operation,
    const std::map<SignalId, SignalId>& renames, bool& changed)
{
    using namespace runtime::simir;
    const auto rename = [&](SignalId& signal) {
        if (const auto found = renames.find(signal); found != renames.end()) {
            signal = found->second;
            changed = true;
        }
        return true;
    };
    const auto named = [&](const SignalId signal) {
        return renames.contains(signal);
    };
    if (write_target(operation) != nullptr) {
        return true;
    }
    if (auto* const value = operation_get_if<WaitOn>(&operation)) {
        for (auto& signal : value->signals) {
            rename(signal);
        }
        return true;
    }
    if (auto* const value = operation_get_if<ForceSignalSlice>(&operation)) {
        return rename(value->signal);
    }
    if (auto* const value = operation_get_if<ReleaseSignalSlice>(&operation)) {
        return rename(value->signal);
    }
    if (auto* const value = operation_get_if<SignalStrengthText>(&operation)) {
        return rename(value->signal);
    }
    if (auto* const value = operation_get_if<VitalTimingCheck>(&operation)) {
        rename(value->test_signal);
        if (value->reference_signal) {
            rename(*value->reference_signal);
        }
        if (value->trigger_signal) {
            rename(*value->trigger_signal);
        }
        return true;
    }
    if (operation_get_if<VitalDelay>(&operation) != nullptr) {
        // It drives its output, and reads through registers.
        return true;
    }
    if (auto* const value = operation_get_if<MonitorInstall>(&operation)) {
        for (auto& each : value->values) {
            if (each.kind == MonitorValueKind::signal) {
                rename(each.signal);
            }
        }
        return true;
    }
    if (auto* const value = operation_get_if<WaitPla>(&operation)) {
        for (auto& signal : value->signals) {
            rename(signal);
        }
        return true;
    }
    // Named events are no ports: the operations on them stay as they are.
    if (const auto* const value = operation_get_if<WaitOrder>(&operation)) {
        return std::ranges::none_of(value->events, named);
    }
    if (const auto* const value = operation_get_if<EventTriggered>(&operation)) {
        return !named(value->event);
    }
    if (const auto* const value = operation_get_if<EventAlias>(&operation)) {
        return !named(value->target) && !(value->has_source && named(value->source));
    }
    if (const auto* const value = operation_get_if<WriteContainerObject>(&operation)) {
        return !value->transaction_signal || !named(*value->transaction_signal);
    }
    if (const auto* const value
        = operation_get_if<WriteContainerObjectElement>(&operation)) {
        return !value->transaction_signal || !named(*value->transaction_signal);
    }
    bool operation_changed { };
    const bool accepted = remap_operation_signals(
        operation, rename, std::nullopt, &operation_changed);
    changed = changed || operation_changed;
    return accepted;
}

// Rewrites the reads of the signals in `renames` in `process`, and leaves its
// writes of them as they are. The empty string when that worked; else why
// the process cannot take it (`process` is then unspecified): it conducts
// the port through a switch, or names it in a way the rewriting does not
// cover.
std::string rename_reads(Process& process,
    const std::map<SignalId, SignalId>& renames, bool& changed)
{
    using namespace runtime::simir;
    flatten(process);
    const auto named = [&](const std::optional<SignalId>& signal) {
        return signal && renames.contains(*signal);
    };
    const char* const switch_reason
        = "a switch (tran, rtran, tranif, ...) of the load instance conducts "
          "between the port and another net, and IEEE 1364-2005 16.2.4 "
          "annotates interconnect delays only between module ports, never "
          "between primitive pins, and defines no delay across a switch";
    if (named(process.switch_target)
        || (process.switch_bidirectional && named(process.switch_source))) {
        // The inout port's own connection to a select or concatenation of
        // its actual is a connection of the net, not a switch of the design.
        if (leaf_of(process.name).starts_with("$inout_")
            && named(process.switch_target) && !named(process.switch_source)) {
            return { };
        }
        return switch_reason;
    }
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        auto operation = process.operations.expanded(index);
        bool operation_changed { };
        if (!rename_operation(operation, renames, operation_changed)) {
            return "a process of the load instance uses an operation on the "
                   "port that the net split does not cover ("
                + process.name + ")";
        }
        if (operation_changed) {
            process.operations.replace(index, std::move(operation));
            changed = true;
        }
    }
    const auto rename = [&](SignalId& signal) {
        if (const auto found = renames.find(signal); found != renames.end()) {
            signal = found->second;
        }
    };
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
    return { };
}

// Whether `process` still reads `net` (or waits on it, or has an operation
// that names signals in a way this check does not cover): its writes of the
// net are not reads.
bool reads_net(const Process& process, const SignalId net)
{
    using namespace runtime::simir;
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        auto operation = process.operations.expanded(index);
        if (write_target(operation) != nullptr) {
            continue;
        }
        if (const auto* const wait = operation_get_if<WaitOn>(&operation)) {
            if (std::ranges::find(wait->signals, net) != wait->signals.end()) {
                return true;
            }
            continue;
        }
        bool reads { };
        if (!remap_operation_signals(operation,
                [&](SignalId& signal) {
                    reads = reads || signal == net;
                    return true;
                },
                std::nullopt)
            || reads) {
            return true;
        }
    }
    return false;
}

// Gives the read of `net` that ends in `reg` the delayed `copy`. False when
// the process has no such read.
bool rename_marked_read(Process& process, const runtime::simir::RegisterId reg,
    const SignalId net, const SignalId copy)
{
    using namespace runtime::simir;
    for (std::size_t index = 0U; index < process.operations.size(); ++index) {
        auto operation = process.operations.expanded(index);
        auto* const read = operation_get_if<ReadSignal>(&operation);
        if (read == nullptr || read->destination != reg || read->signal != net) {
            continue;
        }
        read->signal = copy;
        process.operations.replace(index, std::move(operation));
        return true;
    }
    return false;
}

// A process that waits on its reads also waits on the delayed copy of a net
// it now reads through the copy: the copy replaces the net when nothing else
// of the process reads it, and joins it otherwise.
void follow_copy_sensitivity(
    Process& process, const SignalId net, const SignalId copy)
{
    using namespace runtime::simir;
    const bool keeps_net = reads_net(process, net);
    std::vector<Sensitivity> added;
    for (auto& sensitivity : process.static_sensitivity) {
        if (sensitivity.signal != net
            || sensitivity.edge != EdgeKind::any) {
            continue;
        }
        if (keeps_net) {
            added.push_back(sensitivity);
            added.back().signal = copy;
        } else {
            sensitivity.signal = copy;
        }
    }
    process.static_sensitivity.insert(process.static_sensitivity.end(),
        added.begin(), added.end());
    normalize_sensitivities(process.static_sensitivity);
    // The trigger masks index the old sensitivity order.
    process.static_trigger_regions.clear();
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

// The delay of one bit of a load since the records applied in order of
// occurrence (IEEE 1364-2005 16.3): a PORT record sets every source and the
// delay of a source that no INTERCONNECT names, an INTERCONNECT only its own
// source (16.2.4).
struct BitDelays {
    std::map<std::string, NetDelayTable> by_source;
    NetDelayTable port_delays;
    // The record that set each of those delays (the later of the records
    // that name the bit), npos for none.
    std::map<std::string, std::size_t> source_owner;
    std::size_t port_owner { std::string::npos };
};

// A record that ends at a load: the bits it names and its delays.
struct AppliedRecord {
    std::size_t record { };
    std::uint32_t first { };
    std::uint32_t count { };
    NetDelayTable delays;
};

// The drivers of a load's net that one source port stands for, with one drive
// strength (the strength of every driver reaches the load's copy as it is,
// IEEE 1800-2023 28.12): the processes of the port's instance that write the
// net (the nearest instance when sources nest), or, with no source, the
// net's other drivers, which take the PORT delays. The drivers of the load's
// own instance form the load's own group: their contribution reaches the
// load's copy with no delay (IEEE 1364-2005 16.2.4: the delay is the one
// from another port, and a driver inside the load is not routed through the
// interconnect into itself).
struct Group {
    std::string source;
    bool own { };
    runtime::simir::DriveStrength strength;
    std::vector<ProcessId> writers;
    std::vector<NetDelayTable> delays;
    // The signals of the load's family that the group's drivers reach, and
    // the port connection processes that carry them to the load, in order.
    std::set<SignalId> reach;
    std::vector<ProcessId> connections;
};

// The signals that make up a load's net across the port connection
// processes that pass its bits on (a port connected to a select of a vector,
// an inout port connected to a concatenation): the load's signal and the
// signals those processes read, recursively, with the processes themselves.
struct Family {
    std::vector<SignalId> signals;
    std::vector<ProcessId> connections;
};

struct Load {
    std::vector<std::size_t> records;
    // The first bit each record names, in `records` order.
    std::vector<std::uint32_t> record_offsets;
    std::vector<AppliedRecord> applied;
    std::string object;
    // Per bit of the port (a record of a bit or part select covers only its
    // bits), then the settled delay of each bit.
    std::vector<BitDelays> bits;
    // The record whose delays each bit ended with (the later of the records
    // that name it, IEEE 1364-2005 16.3); npos for a bit no record names.
    std::vector<std::size_t> owner;
    std::uint32_t width { };
    std::vector<NetDelayTable> delays;
    // The instance of each INTERCONNECT source port.
    std::map<std::string, std::string> sources;
    Family family;
    // The drivers of the family that are no connection, with the signals of
    // the family each writes.
    std::map<ProcessId, std::vector<SignalId>> real;
    // Set when the drivers of the net have different delays, strengths, or
    // the load drives the port itself: each group of drivers reaches the
    // load with its own delays and strength (IEEE 1364-2005 16.2.4).
    std::vector<Group> groups;
    // The strength a single copy of the net is driven with.
    runtime::simir::DriveStrength strength;
};

// Each bit's delay once all the records are applied, when every driver has
// the same: the delay of the first source, or the PORT delay.
void settle(Load& load)
{
    load.delays.clear();
    for (const auto& bit : load.bits) {
        load.delays.push_back(bit.by_source.empty()
                ? bit.port_delays : bit.by_source.begin()->second);
    }
}

// Each bit's delay from the drivers that `source` stands for (empty: the
// drivers no INTERCONNECT names, which take the PORT delay).
std::vector<NetDelayTable> group_delays(
    const Load& load, const std::string& source)
{
    std::vector<NetDelayTable> result;
    for (const auto& bit : load.bits) {
        const auto found = bit.by_source.find(source);
        result.push_back(
            found != bit.by_source.end() ? found->second : bit.port_delays);
    }
    return result;
}

// The delays of `increment` added to those of `base`, transition by
// transition, with the pulse limits (IEEE 1364-2005 16.3).
NetDelayTable added(const NetDelayTable& base, const NetDelayTable& increment)
{
    NetDelayTable result;
    for (std::size_t index = 0U; index < result.delay.size(); ++index) {
        result.delay[index] = base.delay[index] + increment.delay[index];
        result.reject[index] = base.reject[index] + increment.reject[index];
        result.error[index] = base.error[index] + increment.error[index];
    }
    return result;
}

// The record that owns each bit's delay from the drivers that `source` stands
// for (empty: the drivers no INTERCONNECT names), like group_delays.
std::vector<std::size_t> group_owners(
    const Load& load, const std::string& source)
{
    std::vector<std::size_t> result;
    for (const auto& bit : load.bits) {
        const auto found = bit.source_owner.find(source);
        result.push_back(
            found != bit.source_owner.end() ? found->second : bit.port_owner);
    }
    return result;
}

// Applies the records of `load` in order of occurrence. An INTERCONNECT
// whose source port drives nothing on the load's net (`driven` holds the
// sources that do) is annotated as the delay from all sources, like a PORT
// delay (IEEE 1364-2005 16.2.4: "If the source port is not found or if the
// source port and the load port are not actually on the same net, then a
// warning message is issued, but the delay to the load port is annotated
// anyway ... the delay is treated as if it were the delay from all source
// ports").
void apply_records(const ElaboratedDesignState& state, Load& load,
    const std::set<std::string>& driven, std::vector<std::string>& notes)
{
    load.bits.assign(load.width, BitDelays { });
    load.owner.assign(load.width, std::string::npos);
    for (const auto& applied : load.applied) {
        const auto& record = state.sdf_net_delays[applied.record];
        const bool from_all = record.kind == SdfNetDelayKind::port
            || !driven.contains(record.source.object);
        if (from_all && record.kind != SdfNetDelayKind::port
            && !is_zero(applied.delays)) {
            notes.push_back(record.identity + ": the source port "
                + record.source.object + " drives nothing on the net of "
                + record.destination.object);
        }
        for (auto bit = applied.first; bit < applied.first + applied.count;
            ++bit) {
            auto& entry = load.bits[bit];
            load.owner[bit] = applied.record;
            if (from_all) {
                entry.port_delays = record.increment
                    ? added(entry.port_delays, applied.delays)
                    : applied.delays;
                entry.port_owner = applied.record;
                for (auto& each : entry.by_source) {
                    each.second = record.increment
                        ? added(each.second, applied.delays) : applied.delays;
                    entry.source_owner[each.first] = applied.record;
                }
            } else {
                const auto current = entry.by_source.find(record.source.object);
                entry.by_source.insert_or_assign(record.source.object,
                    record.increment
                        ? added(current != entry.by_source.end()
                                ? current->second : entry.port_delays,
                            applied.delays)
                        : applied.delays);
                entry.source_owner.insert_or_assign(
                    record.source.object, applied.record);
            }
        }
    }
}

// The bits [offset, offset + width) a terminal names in a signal of `width`
// bits, by the signal's packed range (the right bound is offset 0).
std::optional<std::pair<std::uint32_t, std::uint32_t>> terminal_bits(
    const SdfNetDelayTerminal& terminal, const SignalInfo& info,
    const std::uint32_t width)
{
    if (terminal.select_width == 0U) {
        return std::pair { 0U, width };
    }
    const auto offset = [&](const std::int64_t index)
        -> std::optional<std::uint32_t> {
        if (!info.packed_range) {
            return index == 0 && width == 1U
                ? std::optional<std::uint32_t> { 0U } : std::nullopt;
        }
        const auto& range = *info.packed_range;
        if (index < std::min(range.left, range.right)
            || index > std::max(range.left, range.right)) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(range.descending
                ? index - range.right : range.right - index);
    };
    const auto left = offset(terminal.select_left);
    const auto right = offset(terminal.select_right);
    if (!left || !right) {
        return std::nullopt;
    }
    const auto low = std::min(*left, *right);
    if (terminal.select_width > width || low > width - terminal.select_width) {
        return std::nullopt;
    }
    return std::pair { low, terminal.select_width };
}

} // namespace

NetDelayTable sdf_net_delay_table(
    const std::span<const runtime::SimulationTick> delays,
    const std::span<const runtime::SimulationTick> reject,
    const std::span<const runtime::SimulationTick> error)
{
    NetDelayTable table;
    for (std::size_t index = 0U; index < table.delay.size(); ++index) {
        const auto delay = index < delays.size() ? delays[index]
                                                  : runtime::SimulationTick { };
        const auto low = std::min(delay,
            reject.size() == table.delay.size() ? reject[index] : delay);
        const auto high = std::clamp(
            error.size() == table.delay.size() ? error[index] : low, low,
            delay);
        table.delay[index] = delay;
        table.reject[index] = low;
        table.error[index] = high;
    }
    return table;
}

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
        const auto delays = net_delay_table(record);
        // An object with no instance in its path is a port of the design
        // itself: its load is the top module, so every process of the design
        // reads the delayed copy while what drives the port from outside
        // (the testbench, a VPI deposit or force) writes the net (IEEE
        // 1364-2005 16.2.4).
        if (terminal.signal >= state.signals.size()
            || terminal.signal >= state.signal_info.size()) {
            continue;
        }
        const auto width = static_cast<std::uint32_t>(
            state.signals[terminal.signal].initial_value.width());
        const auto bits
            = terminal_bits(terminal, state.signal_info[terminal.signal], width);
        if (!bits) {
            if (!is_zero(delays)) {
                result.skipped.push_back(record.identity
                    + ": the select lies outside the port's range");
            }
            continue;
        }
        LoadKey key { terminal.signal,
            dot == std::string::npos ? std::string { }
                                     : terminal.object.substr(0U, dot) };
        auto& load = loads[key];
        load.records.push_back(index);
        load.record_offsets.push_back(bits->first);
        load.object = terminal.object;
        load.width = width;
        load.applied.push_back(
            { index, bits->first, bits->second, delays });
        // A source is the driver behind one port; its selects name bits of
        // that one driver.
        const auto& source = record.source.object;
        if (record.kind != SdfNetDelayKind::port) {
            const auto source_dot = source.rfind('.');
            load.sources.emplace(source,
                source_dot == std::string::npos ? std::string { }
                                                : source.substr(0U, source_dot));
        }
    }
    if (loads.empty()) {
        return result;
    }
    // The drivers of each signal, by process.
    std::map<SignalId, std::vector<ProcessId>> writers_of;
    for (const auto& process : state.processes) {
        if (process.name.find(".$mipd_driver") != std::string::npos) {
            continue;
        }
        const auto add = [&](const SignalId signal) {
            auto& list = writers_of[signal];
            if (list.empty() || list.back() != process.id) {
                list.push_back(process.id);
            }
        };
        for (const auto& region : process.driver_regions) {
            add(region.signal);
        }
        for (const auto* const terminal :
            { &process.switch_source, &process.switch_target }) {
            if (terminal->has_value()) {
                add(**terminal);
            }
        }
    }

    // The drivers of each load's net, by the source each stands for (a
    // source group per drive strength, so that the strength of every driver
    // reaches the load as it is). When the groups have different delays or
    // strengths, or the load drives the port itself, each driver also writes
    // a copy of its group's contribution (a dual write: the net itself stays
    // exact for the other readers, forces and the VPI drivers), and each
    // group's copy reaches the load with the group's delays and strength.
    std::map<std::pair<ProcessId, SignalId>, std::vector<std::size_t>>
        write_positions;
    for (auto entry = loads.begin(); entry != loads.end();) {
        auto& load = entry->second;
        const auto& key = entry->first;

        // The family of the load's net: the signals the port connection
        // processes that only pass bits on carry to the load.
        load.family.signals.push_back(key.signal);
        std::set<SignalId> seen { key.signal };
        std::set<ProcessId> connection_set;
        for (std::size_t at = 0U; at < load.family.signals.size(); ++at) {
            const auto found = writers_of.find(load.family.signals[at]);
            if (found == writers_of.end()) {
                continue;
            }
            for (const auto writer : found->second) {
                const auto& process = state.processes[writer];
                if (!is_transparent(process)
                    || !connection_set.insert(writer).second) {
                    continue;
                }
                load.family.connections.push_back(writer);
                for (const auto signal : connection_signals(process)) {
                    if (seen.insert(signal).second) {
                        load.family.signals.push_back(signal);
                    }
                }
            }
        }
        for (const auto signal : load.family.signals) {
            if (const auto found = writers_of.find(signal);
                found != writers_of.end()) {
                for (const auto writer : found->second) {
                    if (!connection_set.contains(writer)) {
                        load.real[writer].push_back(signal);
                    }
                }
            }
        }

        // The source whose instance holds `path`, nearest first.
        const auto source_of = [&](std::string_view path) {
            for (auto dot = path.rfind('.'); dot != std::string_view::npos;
                dot = path.rfind('.')) {
                path = path.substr(0U, dot);
                const auto found = std::ranges::find_if(load.sources,
                    [&](const auto& each) { return each.second == path; });
                if (found != load.sources.end()) {
                    return found->first;
                }
            }
            return std::string { };
        };
        using GroupKey = std::tuple<bool, std::string, int, int>;
        std::map<GroupKey, Group> by_key;
        for (const auto& [writer, signals] : load.real) {
            const auto& process = state.processes[writer];
            // A connection that computes (a port connected to an expression)
            // is a driver outside the load, whatever instance holds it.
            const bool connection = is_connection(process);
            const bool own = !key.instance.empty() && !connection
                && below(process.name, key.instance);
            std::string source;
            if (!own) {
                source = source_of(process.name);
                // A port connection process of the parent drives the net
                // from the formal of the instance's port, which it reads.
                if (source.empty() && connection) {
                    for (const auto& sensitivity : process.static_sensitivity) {
                        if (sensitivity.signal < state.signals.size()) {
                            source = source_of(
                                state.signals[sensitivity.signal].name);
                            if (!source.empty()) {
                                break;
                            }
                        }
                    }
                }
            }
            auto& group = by_key[{ own, source,
                static_cast<int>(process.drive_strength.zero),
                static_cast<int>(process.drive_strength.one) }];
            group.own = own;
            group.source = source;
            group.strength = process.drive_strength;
            group.writers.push_back(writer);
        }
        std::vector<Group> groups;
        for (auto& [group_key, group] : by_key) {
            groups.push_back(std::move(group));
        }
        std::set<std::string> driven;
        for (const auto& group : groups) {
            if (!group.own && !group.source.empty()) {
                driven.insert(group.source);
            }
        }
        // The port of the design itself has no instance above it: whatever
        // drives it from outside (a VPI deposit or force) writes the net and
        // cannot be told from a process of the design that drives it, whose
        // contribution would have to reach the copy undelayed.
        if (key.instance.empty() && !load.real.empty()) {
            for (const auto record : load.records) {
                result.skipped.push_back(state.sdf_net_delays[record].identity
                    + ": a process of the design drives the port of the "
                      "design itself, and what drives it from outside (a VPI "
                      "deposit or force) writes the same net");
            }
            entry = loads.erase(entry);
            continue;
        }
        apply_records(state, load, driven, result.from_all_sources);
        for (auto& group : groups) {
            group.delays = group.own
                ? std::vector<NetDelayTable>(load.width)
                : group_delays(load, group.source);
        }
        const Group* outside { };
        bool own_present { };
        bool differing { };
        std::set<std::pair<int, int>> strengths;
        for (const auto& group : groups) {
            if (group.own) {
                own_present = true;
                continue;
            }
            if (outside == nullptr) {
                outside = &group;
            }
            differing = differing || group.delays != outside->delays;
            strengths.emplace(static_cast<int>(group.strength.zero),
                static_cast<int>(group.strength.one));
        }
        const bool grouped
            = own_present || differing || strengths.size() > 1U;
        if (!grouped) {
            if (outside != nullptr) {
                load.delays = outside->delays;
                load.strength = outside->strength;
            } else {
                settle(load);
            }
            ++entry;
            continue;
        }

        // The drivers of a group must write the net with operations the dual
        // write copies; the family's connections must be copyable; and each
        // group must reach the load.
        std::string failure;
        for (auto& group : groups) {
            for (const auto writer : group.writers) {
                const auto& process = state.processes[writer];
                for (const auto signal : load.real.at(writer)) {
                    auto& positions = write_positions[{ writer, signal }];
                    if (!failure.empty()
                        || net_writes(process, signal, positions, false, true)) {
                        continue;
                    }
                    const bool switched = process.switch_source
                        || process.switch_target || process.switch_control;
                    failure = switched
                        ? ": " + process.name
                            + " is a switch (tran, rtran, tranif, ...): IEEE "
                              "1364-2005 16.2.4 annotates interconnect delays "
                              "only between module ports, never between "
                              "primitive pins, and defines no delay across a "
                              "switch"
                        : ": " + process.name
                            + " writes the net with an operation the "
                              "per-source split does not cover";
                }
            }
        }
        for (const auto connection : load.family.connections) {
            if (failure.empty() && !can_remap(state.processes[connection])) {
                failure = ": the port connection process "
                    + state.processes[connection].name
                    + " uses an operation the per-source split does not cover";
            }
        }
        for (auto& group : groups) {
            for (const auto writer : group.writers) {
                for (const auto signal : load.real.at(writer)) {
                    group.reach.insert(signal);
                }
            }
            std::set<ProcessId> done;
            for (bool progress = true; progress;) {
                progress = false;
                for (const auto connection : load.family.connections) {
                    if (done.contains(connection)) {
                        continue;
                    }
                    const auto& process = state.processes[connection];
                    const auto inputs = connection_inputs(process);
                    if (!std::ranges::any_of(inputs, [&](const SignalId each) {
                            return group.reach.contains(each);
                        })) {
                        continue;
                    }
                    done.insert(connection);
                    group.connections.push_back(connection);
                    for (const auto signal : connection_signals(process)) {
                        group.reach.insert(signal);
                    }
                    progress = true;
                }
            }
            if (failure.empty() && !group.reach.contains(key.signal)) {
                failure = ": the drivers of "
                    + (group.source.empty() ? std::string { "the net" }
                                            : group.source)
                    + " reach the port through no port connection";
            }
        }
        if (failure.empty() && outside != nullptr
            && std::ranges::all_of(groups, [](const Group& group) {
                   return group.own
                       || std::ranges::all_of(group.delays, is_zero);
               })) {
            // Nothing outside the load is delayed.
            entry = loads.erase(entry);
            continue;
        }
        if (failure.empty() && outside == nullptr) {
            if (std::ranges::any_of(load.applied, [](const AppliedRecord& each) {
                    return !is_zero(each.delays);
                })) {
                failure = ": no process outside the load instance drives the "
                          "port, so no value reaches it through the "
                          "interconnect";
            } else {
                entry = loads.erase(entry);
                continue;
            }
        }
        if (!failure.empty()) {
            if (!failure.starts_with(':')) {
                failure.insert(0U, ": ");
            }
            for (const auto record : load.records) {
                result.skipped.push_back(
                    state.sdf_net_delays[record].identity + failure);
            }
            entry = loads.erase(entry);
            continue;
        }
        load.groups = std::move(groups);
        ++entry;
    }
    std::erase_if(loads, [](const auto& entry) {
        return entry.second.groups.empty()
            && std::ranges::all_of(entry.second.delays, is_zero);
    });
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
    // The reads through hierarchical names (`$display(top.u1.a)`), by
    // process. Such a read names the load port, wherever the process is:
    // when the name lies at or below a load instance, the read takes that
    // load's copy, and the process keeps its other reads of the net (IEEE
    // 1364-2005 16.2.4). The port of the design itself is the exception, as
    // for its VPI object: its own name is what the outside drives.
    std::map<ProcessId, std::vector<const HierarchicalSignalRead*>> marked;
    for (const auto& read : state.hierarchical_reads) {
        marked[read.process].push_back(&read);
    }
    const auto is_root = [&](const std::string& scope) {
        return scope.empty()
            || std::ranges::find(state.roots, scope) != state.roots.end();
    };
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
        std::map<LoadKey, std::string> rejected;
        for (std::size_t index = 0U; index < state.processes.size();
            ++index) {
            const auto& process = state.processes[index];
            // The delayed copies of an earlier split read the net itself.
            if (process.name.find(".$mipd_driver") != std::string::npos) {
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
            // The loads of the design's own ports are held by every process.
            if (const auto top = by_instance.find(std::string_view { });
                top != by_instance.end()) {
                for (const auto* key : top->second) {
                    if (renames.emplace(key->signal, copies.at(*key)).second) {
                        owners.push_back(key);
                    }
                }
            }
            // The reads of the process through the name of a load port: the
            // nearest load instance that the name lies below owns each.
            struct MarkedRead {
                const HierarchicalSignalRead* read;
                const LoadKey* owner;
            };
            std::vector<MarkedRead> marks;
            if (const auto found = marked.find(process.id);
                found != marked.end()) {
                for (const auto* read : found->second) {
                    const LoadKey* owner { };
                    for (const auto& [key, load] : loads) {
                        const bool below_load = key.instance.empty()
                            || (read->path.size() > key.instance.size()
                                && read->path.starts_with(key.instance)
                                && read->path[key.instance.size()] == '.');
                        if (key.signal != read->signal || !below_load
                            || (is_root(key.instance)
                                && read->path == load.object)
                            || (owner != nullptr
                                && key.instance.size()
                                    <= owner->instance.size())) {
                            continue;
                        }
                        owner = &key;
                    }
                    // A process of the load instance has the read renamed
                    // with its others.
                    if (owner != nullptr
                        && !(renames.contains(read->signal)
                            && renames.at(read->signal)
                                == copies.at(*owner))) {
                        marks.push_back({ read, owner });
                    }
                }
            }
            if (renames.empty() && marks.empty()) {
                continue;
            }
            auto work = process;
            flatten(work);
            std::vector<MarkedRead> applied;
            for (const auto& mark : marks) {
                if (mark.read->sensitivity) {
                    // An event control through the load port's name waits on
                    // the delayed copy, which is what the name denotes there.
                    bool waits { };
                    for (auto& sensitivity : work.static_sensitivity) {
                        if (sensitivity.signal == mark.read->signal) {
                            sensitivity.signal = copies.at(*mark.owner);
                            waits = true;
                        }
                    }
                    if (waits) {
                        normalize_sensitivities(work.static_sensitivity);
                        // The trigger masks index the old sensitivity order.
                        work.static_trigger_regions.clear();
                        applied.push_back(mark);
                    }
                    continue;
                }
                if (rename_marked_read(work, mark.read->reg, mark.read->signal,
                        copies.at(*mark.owner))) {
                    applied.push_back(mark);
                }
            }
            for (const auto& mark : applied) {
                if (mark.read->implicit && !mark.read->sensitivity) {
                    follow_copy_sensitivity(work, mark.read->signal,
                        copies.at(*mark.owner));
                }
            }
            bool renamed { };
            if (!renames.empty()) {
                if (auto reason = rename_reads(work, renames, renamed);
                    !reason.empty()) {
                    for (const auto* key : owners) {
                        rejected.try_emplace(*key, reason);
                    }
                    continue;
                }
            }
            if (renamed) {
                for (const auto* key : owners) {
                    ++readers[*key];
                }
            }
            for (const auto& mark : applied) {
                ++readers[*mark.owner];
            }
            if (renamed || !applied.empty()) {
                rewritten.emplace_back(index, std::move(work));
            }
        }
        bool dropped { };
        for (auto entry = loads.begin(); entry != loads.end();) {
            const auto refused = rejected.find(entry->first);
            if (refused == rejected.end() && readers.contains(entry->first)) {
                ++entry;
                continue;
            }
            for (const auto record : entry->second.records) {
                result.skipped.push_back(
                    state.sdf_net_delays[record].identity + ": "
                    + (refused != rejected.end()
                            ? refused->second
                            : std::string { "no process of the load instance "
                                            "reads the port" }));
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
        result.rewritten.push_back(static_cast<ProcessId>(index));
        state.processes[index] = std::move(process);
    }

    // The delayed copies: a signal for each load, and processes that write
    // the net's value to it after the transition delay, like a continuous
    // assignment with a delay. A per-source load keeps the net's implicit
    // driver and charge, since its copy resolves the groups' delayed values
    // as the net resolves its drivers.
    using namespace runtime::simir;
    const auto copy_signal = [&](const SignalId net, const SignalId id,
                                 std::string name, const bool resolves) {
        auto signal = state.signals[net];
        signal.name = name;
        if (!resolves) {
            signal.implicit_driver.reset();
            signal.charge_strength.reset();
            signal.charge_decay.reset();
        }
        auto info = state.signal_info[net];
        // The copy that the drivers of several groups write is a resolved
        // net, whatever the net it copies is (the own net of a port whose
        // actual is a select has no resolution of its own: one adapter
        // drives it).
        if (resolves && signal.resolution == ResolutionKind::none
            && signal.value_kind == ValueKind::logic4) {
            signal.resolution = ResolutionKind::sv_wire;
            signal.initial_value
                = runtime::PackedLogic4(signal.initial_value.width(), runtime::Logic4::z);
            if (info.systemverilog_net_type.empty()) {
                info.systemverilog_net_type = "wire";
            }
        }
        info.id = id;
        info.name = std::move(name);
        info.is_port = false;
        info.direction = frontend::PortDirection::Unknown;
        state.signals.push_back(std::move(signal));
        state.signal_info.push_back(std::move(info));
    };
    for (const auto& [key, load] : loads) {
        copy_signal(key.signal, copies.at(key), load.object + ".$mipd",
            !load.groups.empty());
    }
    // Each run of bits with equal delays has a driver of its own, like a
    // continuous assignment of that select (a select of the port delays only
    // its bits); an undelayed run is written at once. A source whose drivers
    // have several drive strengths has a driver for each strength in a run,
    // named after the first with ".<number>" appended, so that they follow
    // one another in the design's order.
    struct Feed {
        SignalId read { };
        DriveStrength strength;
    };
    using Runs = std::vector<std::pair<std::uint32_t, ProcessId>>;
    const auto add_drivers = [&](const Load& load, const SignalId id,
                                 const std::vector<Feed>& feeds,
                                 const std::vector<NetDelayTable>& delays,
                                 const std::string& tag,
                                 const std::vector<std::size_t>& owners) {
        const auto kind = state.signals[feeds.front().read].value_kind;
        Runs runs;
        for (std::uint32_t begin = 0U; begin < delays.size();) {
            auto end = begin + 1U;
            // A run also ends where the record that owns the bits changes,
            // so that a put on one record retimes only its own bits
            // (vpi_put_delays, IEEE 1800-2023 38.32).
            const auto owned = [&](const std::uint32_t bit) {
                return owners.empty() ? std::string::npos : owners[bit];
            };
            while (end < delays.size() && delays[end] == delays[begin]
                && owned(end) == owned(begin)) {
                ++end;
            }
            const bool whole = begin == 0U && end == delays.size();
            const auto& delay = delays[begin];
            for (std::size_t number = 0U; number < feeds.size(); ++number) {
                const auto read = feeds[number].read;
                Process process;
                process.id = static_cast<ProcessId>(state.processes.size());
                process.name = (whole ? load.object
                                      : load.object + '[' + std::to_string(begin)
                                        + "+:" + std::to_string(end - begin) + ']')
                    + ".$mipd_driver" + tag
                    + (number == 0U ? std::string { }
                                    : '.' + std::to_string(number));
                process.scheduling_domain
                    = ProcessSchedulingDomain::systemverilog;
                process.drive_strength = feeds[number].strength;
                process.static_sensitivity.push_back({ read, EdgeKind::any });
                process.operations.emplace_back(ReadSignal { 0U, read });
                process.register_value_kinds.push_back(kind);
                std::vector<std::size_t> widths { delays.size() };
                if (whole) {
                    if (is_zero(delay)) {
                        process.operations.emplace_back(WriteUpdate { id, 0U,
                            SignalUpdateDomain::systemverilog_active });
                    } else {
                        process.operations.emplace_back(WriteNetDelay { id, 0U,
                            std::nullopt, delay.values(),
                            SignalUpdateDomain::systemverilog_active });
                    }
                } else {
                    process.operations.emplace_back(
                        Extract { 1U, 0U, begin, end - begin });
                    widths.push_back(end - begin);
                    process.register_value_kinds.push_back(kind);
                    if (is_zero(delay)) {
                        process.operations.emplace_back(WriteUpdateSlice { id,
                            1U, begin, SignalUpdateDomain::systemverilog_active });
                    } else {
                        process.operations.emplace_back(WriteNetDelay { id, 1U,
                            begin, delay.values(),
                            SignalUpdateDomain::systemverilog_active });
                    }
                }
                process.operations.emplace_back(WaitSensitivity { });
                process.operations.emplace_back(Jump { 0U });
                process.register_count
                    = static_cast<std::uint32_t>(widths.size());
                process.driver_regions
                    = elaboration_detail::collect_driver_regions(process, widths);
                if (number == 0U) {
                    runs.emplace_back(begin, process.id);
                }
                state.processes.push_back(std::move(process));
            }
            begin = end;
        }
        return runs;
    };
    // A record's driver is the one of the run its first bit is in.
    const auto run_driver = [](const Runs& runs, const std::uint32_t offset) {
        return std::prev(std::ranges::upper_bound(runs, offset, { },
                             &std::pair<std::uint32_t, ProcessId>::first))
            ->second;
    };
    // The group copies, one for each set of drivers of a signal (loads of
    // one net with the same sources share them), the copies of the port
    // connection processes that carry a group's copies on, and the
    // operations each driver adds to write its copies.
    std::map<std::pair<SignalId, std::vector<ProcessId>>, SignalId>
        group_copies;
    std::map<std::pair<ProcessId, std::vector<ProcessId>>, ProcessId>
        connection_copies;
    std::set<std::pair<ProcessId, SignalId>> dual_done;
    std::map<ProcessId, std::vector<std::pair<std::size_t, Operation>>>
        dual_writes;
    for (auto& built : loads) {
        const auto& key = built.first;
        auto& load = built.second;
        const auto id = copies.at(key);
        if (load.groups.empty()) {
            const auto runs = add_drivers(
                load, id, { Feed { key.signal, load.strength } }, load.delays,
                { }, load.owner);
            for (std::size_t index = 0U; index < load.records.size();
                ++index) {
                auto& declared = state.sdf_net_delays[load.records[index]];
                declared.delayed = true;
                declared.load_signal = id;
                declared.driver = run_driver(runs, load.record_offsets[index]);
                // Every run whose bits this record owns: a put on the record
                // retimes each (a record that a later one overrides entirely
                // keeps `driver` alone).
                declared.run_drivers.clear();
                for (const auto& [begin, process] : runs) {
                    if (load.owner[begin] == load.records[index]) {
                        declared.run_drivers.push_back(process);
                    }
                }
            }
            ++result.split;
            continue;
        }
        const std::set<SignalId> family(
            load.family.signals.begin(), load.family.signals.end());
        // The copy of a family signal that one group's drivers write.
        std::vector<std::map<SignalId, SignalId>> shadows(load.groups.size());
        const auto shadow_of = [&](const std::size_t number,
                                   const SignalId signal) {
            auto& known = shadows[number];
            if (const auto found = known.find(signal); found != known.end()) {
                return found->second;
            }
            const auto [copy, added] = group_copies.try_emplace(
                { signal, load.groups[number].writers },
                static_cast<SignalId>(state.signals.size()));
            if (added) {
                copy_signal(signal, copy->second,
                    state.signals[signal].name + ".$mipd_source"
                        + std::to_string(group_copies.size() - 1U),
                    false);
            }
            known.emplace(signal, copy->second);
            return copy->second;
        };
        for (std::size_t number = 0U; number < load.groups.size(); ++number) {
            const auto& group = load.groups[number];
            for (const auto writer : group.writers) {
                for (const auto signal : load.real.at(writer)) {
                    const auto shadow = shadow_of(number, signal);
                    if (!dual_done.emplace(writer, shadow).second) {
                        continue;
                    }
                    auto& inserted = dual_writes[writer];
                    for (const auto position :
                        write_positions.at({ writer, signal })) {
                        auto operation
                            = state.processes[writer].operations.expanded(position);
                        *write_target(operation) = shadow;
                        inserted.emplace_back(position, std::move(operation));
                    }
                    const auto regions = state.processes[writer].driver_regions;
                    for (auto region : regions) {
                        if (region.signal == signal) {
                            region.signal = shadow;
                            state.processes[writer].driver_regions.push_back(
                                region);
                        }
                    }
                }
            }
            // A port connection process the group's drivers pass through
            // gets a copy that reads the group's copies and writes the
            // group's copies of what it drives.
            for (const auto connection : group.connections) {
                const auto [clone, added] = connection_copies.try_emplace(
                    { connection, group.writers }, ProcessId { });
                if (!added) {
                    continue;
                }
                Process process = state.processes[connection];
                flatten(process);
                std::map<SignalId, SignalId> map;
                for (const auto signal : connection_signals(process)) {
                    if (family.contains(signal)) {
                        map.emplace(signal, shadow_of(number, signal));
                    }
                }
                const auto remap = [&](SignalId& signal) {
                    if (const auto found = map.find(signal); found != map.end()) {
                        signal = found->second;
                    }
                    return true;
                };
                for (std::size_t index = 0U; index < process.operations.size();
                    ++index) {
                    auto operation = process.operations.expanded(index);
                    (void)remap_operation_signals(
                        operation, remap, std::nullopt);
                    process.operations.replace(index, std::move(operation));
                }
                for (auto& sensitivity : process.static_sensitivity) {
                    remap(sensitivity.signal);
                }
                normalize_sensitivities(process.static_sensitivity);
                process.static_trigger_regions.clear();
                for (auto& region : process.driver_regions) {
                    remap(region.signal);
                }
                for (auto* const terminal : { &process.switch_source,
                         &process.switch_target, &process.switch_control }) {
                    if (terminal->has_value()) {
                        remap(**terminal);
                    }
                }
                process.id = static_cast<ProcessId>(state.processes.size());
                process.name += ".$mipd_clone"
                    + std::to_string(connection_copies.size() - 1U);
                clone->second = process.id;
                state.processes.push_back(std::move(process));
                ++result.dual_written;
            }
        }
        // The groups of one source (and the load's own) share their runs of
        // bits: a driver for each strength follows the first of the run.
        std::vector<std::pair<std::pair<bool, std::string>,
            std::vector<std::size_t>>> buckets;
        for (std::size_t number = 0U; number < load.groups.size(); ++number) {
            const std::pair<bool, std::string> bucket_key {
                load.groups[number].own, load.groups[number].source };
            auto found = std::ranges::find(buckets, bucket_key,
                &decltype(buckets)::value_type::first);
            if (found == buckets.end()) {
                buckets.push_back({ bucket_key, { } });
                found = std::prev(buckets.end());
            }
            found->second.push_back(number);
        }
        std::map<std::pair<bool, std::string>, Runs> bucket_runs;
        // The record that owns each bit of a bucket's runs (npos for the
        // load's own undelayed group).
        std::map<std::pair<bool, std::string>, std::vector<std::size_t>>
            bucket_owners;
        for (std::size_t number = 0U; number < buckets.size(); ++number) {
            std::vector<Feed> feeds;
            for (const auto group : buckets[number].second) {
                feeds.push_back({ shadow_of(group, key.signal),
                    load.groups[group].strength });
            }
            const auto& lead = load.groups[buckets[number].second.front()];
            auto owners = lead.own ? std::vector<std::size_t> { }
                                   : group_owners(load, lead.source);
            bucket_runs.emplace(buckets[number].first,
                add_drivers(load, id, feeds, lead.delays,
                    '_' + std::to_string(number), owners));
            bucket_owners.emplace(buckets[number].first, std::move(owners));
        }
        std::set<std::string> driven;
        for (const auto& group : load.groups) {
            if (!group.own && !group.source.empty()) {
                driven.insert(group.source);
            }
        }
        // An INTERCONNECT record names the driver of its source's group, a
        // PORT record (and an INTERCONNECT whose source drives nothing here)
        // that of the other drivers when there are any.
        for (std::size_t index = 0U; index < load.records.size(); ++index) {
            auto& declared = state.sdf_net_delays[load.records[index]];
            const bool from_all = declared.kind == SdfNetDelayKind::port
                || !driven.contains(declared.source.object);
            auto found = bucket_runs.find({ false,
                from_all ? std::string { } : declared.source.object });
            if (found == bucket_runs.end() && from_all) {
                found = bucket_runs.begin();
            }
            if (found == bucket_runs.end() || found->first.first) {
                result.skipped.push_back(declared.identity
                    + ": no process outside the load instance drives the net");
                continue;
            }
            declared.delayed = true;
            declared.load_signal = id;
            declared.driver
                = run_driver(found->second, load.record_offsets[index]);
            // The runs of that group whose bits this record owns, so that a
            // put on the record retimes each of them and no other bit.
            declared.run_drivers.clear();
            const auto& owners = bucket_owners.at(found->first);
            for (const auto& [begin, process] : found->second) {
                if (!owners.empty() && owners[begin] == load.records[index]) {
                    declared.run_drivers.push_back(process);
                }
            }
        }
        ++result.split;
    }
    for (auto& [writer, inserted] : dual_writes) {
        std::ranges::stable_sort(inserted, { },
            &std::pair<std::size_t, Operation>::first);
        insert_after(state.processes[writer], inserted);
    }
    result.dual_written += dual_writes.size();
    state.signal_driver_inventory.reset();
    return result;
}

std::optional<
    std::vector<std::pair<runtime::simir::ProcessId, runtime::simir::NetDelayTable>>>
sdf_net_delay_retiming(
    const ElaboratedDesign& running, const ElaboratedDesign& annotated)
{
    using namespace runtime::simir;
    std::vector<std::pair<ProcessId, NetDelayTable>> result;
    const auto& old_records = running.sdf_net_delays();
    const auto& new_records = annotated.sdf_net_delays();
    if (old_records.empty() && new_records.empty()) {
        return result;
    }
    // A running design has handed its processes and signals to the
    // interpreter, so it is compared by its records: the delayed loads, each
    // with the copy signal and driver process the split gave it. The split
    // numbers them in a fixed order from one baseline, so the same triples
    // mean the same copies and drivers.
    using Triple = std::tuple<std::string, SignalId, ProcessId,
        std::vector<ProcessId>>;
    const auto delayed = [](const std::vector<SdfNetDelayInfo>& records) {
        std::vector<Triple> triples;
        for (const auto& record : records) {
            if (record.delayed) {
                triples.emplace_back(record.destination.object,
                    record.load_signal, record.driver, record.run_drivers);
            }
        }
        std::ranges::sort(triples);
        triples.erase(std::ranges::unique(triples).begin(), triples.end());
        return triples;
    };
    if (delayed(old_records) != delayed(new_records)) {
        return std::nullopt;
    }
    const auto is_zero_record = [](const SdfNetDelayInfo& record) {
        const auto zero = [](const runtime::SimulationTick tick) {
            return tick == 0U;
        };
        return std::ranges::all_of(record.delays, zero)
            && std::ranges::all_of(record.reject, zero)
            && std::ranges::all_of(record.error, zero);
    };
    const auto& processes = annotated.processes();
    const auto net_delay_of = [](const Process& process)
        -> std::optional<NetDelayTable> {
        for (std::size_t index = 0U; index < process.operations.size();
            ++index) {
            auto operation = process.operations.expanded(index);
            if (const auto* const write
                = operation_get_if<WriteNetDelay>(&operation)) {
                return NetDelayTable::from_values(write->table);
            }
        }
        return std::nullopt;
    };
    // A run of bits without a delay is written at once and has no table: it
    // cannot take one, and one that had a table cannot lose it. A running
    // record with no delay stands for such a run, one with a delay for a run
    // with a table.
    for (const auto& record : new_records) {
        if (!record.delayed || record.driver >= processes.size()) {
            continue;
        }
        const bool has_table = net_delay_of(processes[record.driver]).has_value();
        const auto old = std::ranges::find_if(old_records,
            [&](const SdfNetDelayInfo& each) {
                return each.delayed && each.driver == record.driver
                    && each.destination.object == record.destination.object;
            });
        if (old == old_records.end()
            || (!has_table && !is_zero_record(*old))
            || (has_table && is_zero_record(*old)
                && !is_zero_record(record))) {
            return std::nullopt;
        }
    }
    for (const auto& process : processes) {
        if (process.name.find(".$mipd_driver") == std::string::npos) {
            continue;
        }
        if (const auto table = net_delay_of(process)) {
            result.emplace_back(process.id, *table);
        }
    }
    return result;
}

std::optional<SdfNetDelayGraft> plan_sdf_net_delay_graft(
    const ElaboratedDesign& running, SdfNetDelayInfo record,
    const std::size_t process_count,
    const std::function<const runtime::simir::Process&(
        runtime::simir::ProcessId)>& running_program,
    std::string& reason)
{
    using namespace runtime::simir;
    auto state = running.state();
    if (state.processes.empty()) {
        state.processes.reserve(process_count);
        for (std::size_t index = 0U; index < process_count; ++index) {
            state.processes.push_back(
                running_program(static_cast<ProcessId>(index)));
        }
    }
    // The split sees the new record alone: the ports it split before stay as
    // they are, and the copies of an earlier split are not loads.
    auto earlier = std::move(state.sdf_net_delays);
    const auto first_signal = state.signals.size();
    const auto first_process = state.processes.size();
    record.kind = SdfNetDelayKind::port;
    state.sdf_net_delays.clear();
    state.sdf_net_delays.push_back(std::move(record));
    const auto split = split_sdf_net_delays(state);
    // A port whose net needs a dual write (its own driver, a driver of
    // another strength) changes processes that exist, which a running design
    // cannot take.
    if (split.dual_written != 0U) {
        reason = "the port's net needs per-source copies, which a running "
                 "design cannot take";
        return std::nullopt;
    }
    if (split.split == 0U || !state.sdf_net_delays.front().delayed
        || state.signals.size() <= first_signal
        || state.processes.size() <= first_process) {
        reason = split.skipped.empty()
            ? std::string { "the port has no delay to split" }
            : split.skipped.front();
        return std::nullopt;
    }
    SdfNetDelayGraft result;
    result.driver = state.sdf_net_delays.front().driver;
    result.graft.signals.assign(
        state.signals.begin() + static_cast<std::ptrdiff_t>(first_signal),
        state.signals.end());
    result.graft.processes.assign(
        state.processes.begin() + static_cast<std::ptrdiff_t>(first_process),
        state.processes.end());
    for (const auto index : split.rewritten) {
        if (index >= first_process) {
            continue;
        }
        result.graft.rewritten.emplace_back(index, state.processes[index]);
    }
    earlier.push_back(std::move(state.sdf_net_delays.front()));
    state.sdf_net_delays = std::move(earlier);
    auto grown = ElaboratedDesign::from_state(std::move(state));
    if (!grown) {
        reason = "the split design has invalid state";
        return std::nullopt;
    }
    result.grown = std::move(grown);
    return result;
}


std::optional<SdfNetDelayGraft> plan_tranif_control_graft(
    const ElaboratedDesign& running, const std::string_view switch_name,
    const NetDelayTable& table, const std::size_t process_count,
    const std::function<const runtime::simir::Process&(
        runtime::simir::ProcessId)>& running_program,
    std::string& reason)
{
    using namespace runtime::simir;
    auto state = running.state();
    if (state.processes.empty()) {
        state.processes.reserve(process_count);
        for (std::size_t index = 0U; index < process_count; ++index) {
            state.processes.push_back(
                running_program(static_cast<ProcessId>(index)));
        }
    }
    // The switch's two conducting processes (`<name>$left`, `<name>$right`)
    // read its control; the control is delayed for them alone (IEEE 1800-2023
    // 28.16: the turn-on and turn-off delays are the control's).
    const std::string left = std::string { switch_name } + "$left";
    const std::string right = std::string { switch_name } + "$right";
    std::vector<std::size_t> loads;
    std::optional<SignalId> control;
    for (std::size_t index = 0U; index < state.processes.size(); ++index) {
        const auto& process = state.processes[index];
        if (process.name != left && process.name != right) {
            continue;
        }
        if (!process.switch_control
            || (control && *control != *process.switch_control)) {
            reason = "the switch's control is not one signal";
            return std::nullopt;
        }
        control = process.switch_control;
        loads.push_back(index);
    }
    if (loads.empty() || !control || *control >= state.signals.size()) {
        reason = "the switch has no conducting process with a control";
        return std::nullopt;
    }
    const auto first_signal = state.signals.size();
    const auto first_process = state.processes.size();
    const auto copy_id = static_cast<SignalId>(first_signal);
    {
        auto signal = state.signals[*control];
        signal.name = std::string { switch_name } + ".$control";
        signal.implicit_driver.reset();
        signal.charge_strength.reset();
        signal.charge_decay.reset();
        auto info = state.signal_info[*control];
        info.id = copy_id;
        info.name = signal.name;
        info.is_port = false;
        info.direction = frontend::PortDirection::Unknown;
        state.signals.push_back(std::move(signal));
        state.signal_info.push_back(std::move(info));
    }
    const auto kind = state.signals[*control].value_kind;
    const auto width = state.signals[*control].initial_value.width();
    Process driver;
    driver.id = static_cast<ProcessId>(first_process);
    driver.name = std::string { switch_name } + "$control";
    driver.scheduling_domain = ProcessSchedulingDomain::systemverilog;
    driver.static_sensitivity.push_back({ *control, EdgeKind::any });
    driver.operations.emplace_back(ReadSignal { 0U, *control });
    driver.register_value_kinds.push_back(kind);
    driver.operations.emplace_back(WriteNetDelay { copy_id, 0U, std::nullopt,
        table.values(), SignalUpdateDomain::systemverilog_active });
    driver.operations.emplace_back(WaitSensitivity { });
    driver.operations.emplace_back(Jump { 0U });
    driver.register_count = 1U;
    driver.driver_regions = elaboration_detail::collect_driver_regions(
        driver, std::vector<std::size_t> { width });
    state.processes.push_back(std::move(driver));
    const std::map<SignalId, SignalId> renames { { *control, copy_id } };
    SdfNetDelayGraft result;
    for (const auto index : loads) {
        bool changed { };
        auto process = state.processes[index];
        if (const auto why = rename_reads(process, renames, changed);
            !why.empty() || !changed) {
            reason = why.empty() ? std::string { "the switch does not read its control" }
                                 : why;
            return std::nullopt;
        }
        state.processes[index] = process;
        result.graft.rewritten.emplace_back(
            static_cast<ProcessId>(index), std::move(process));
    }
    result.driver = static_cast<ProcessId>(first_process);
    result.graft.signals.assign(
        state.signals.begin() + static_cast<std::ptrdiff_t>(first_signal),
        state.signals.end());
    result.graft.processes.assign(
        state.processes.begin() + static_cast<std::ptrdiff_t>(first_process),
        state.processes.end());
    auto grown = ElaboratedDesign::from_state(std::move(state));
    if (!grown) {
        reason = "the split design has invalid state";
        return std::nullopt;
    }
    result.grown = std::move(grown);
    return result;
}


std::size_t apply_sdf_device_gate_delays(ElaboratedDesignState& state,
    const std::span<const SdfDeviceGateDelay> devices)
{
    using namespace runtime::simir;
    std::size_t rewritten { };
    for (const auto& device : devices) {
        const auto table = NetDelayTable::from_gate_delays(device.delays).values();
        for (auto& primitive : state.verilog_primitives) {
            if (primitive.instance != device.instance) {
                continue;
            }
            const bool drives = std::ranges::any_of(primitive.terminals,
                [&](const VerilogPrimitiveTerminalInfo& terminal) {
                    return terminal.direction
                            == frontend::VerilogPrimitiveTerminalDirection::Output
                        && terminal.signal == device.signal;
                });
            if (!drives) {
                continue;
            }
            for (std::size_t id = 0U; id < state.processes.size(); ++id) {
                auto& process = state.processes[id];
                // The labels the front end gives a gate's drivers: `g`,
                // `g$one`, `g$output<n>`.
                const std::string_view name { process.name };
                if (!name.starts_with(primitive.full_name)) {
                    continue;
                }
                const auto rest = name.substr(primitive.full_name.size());
                if (!(rest.empty() || rest == "$one" || rest.starts_with("$output"))) {
                    continue;
                }
                flatten(process);
                bool changed { };
                for (std::size_t index = 0U; index < process.operations.size(); ++index) {
                    auto operation = process.operations.expanded(index);
                    if (const auto* const update = operation_get_if<WriteUpdate>(&operation);
                        update != nullptr && update->signal == device.signal) {
                        // A gate with a delay writes inertially, like one
                        // declared with `#(rise, fall, turn-off)` (IEEE
                        // 1364-2005 7.14), so a later put retimes it.
                        operation = WriteInertial { update->signal, update->source,
                            device.delays, update->domain };
                    } else if (const auto* const slice
                        = operation_get_if<WriteUpdateSlice>(&operation);
                        slice != nullptr && slice->signal == device.signal) {
                        operation = WriteNetDelay { slice->signal, slice->source,
                            slice->offset, table, slice->domain };
                    } else if (auto* const inertial
                        = operation_get_if<WriteInertial>(&operation);
                        inertial != nullptr && inertial->signal == device.signal) {
                        inertial->delays = device.delays;
                    } else if (auto* const delayed
                        = operation_get_if<WriteNetDelay>(&operation);
                        delayed != nullptr && delayed->signal == device.signal) {
                        delayed->table = table;
                    } else {
                        continue;
                    }
                    process.operations.replace(index, std::move(operation));
                    changed = true;
                }
                if (changed) {
                    ++rewritten;
                    // The gate is delayed now, so its driver takes a delay
                    // slot like one declared with a delay
                    // (bind_primitive_delay_slots).
                    const auto driver = static_cast<ProcessId>(id);
                    if (std::ranges::find(primitive.drivers, driver)
                        == primitive.drivers.end()) {
                        primitive.drivers.push_back(driver);
                    }
                }
            }
        }
    }
    return rewritten;
}

} // namespace fsim::elaboration
