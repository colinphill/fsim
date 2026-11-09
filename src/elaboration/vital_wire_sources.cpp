// SPDX-License-Identifier: Apache-2.0
#include "fsim/elaboration/vital_wire_sources.hpp"

#include "sdf_net_delay_dual_write.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string_view>
#include <utility>

namespace fsim::elaboration {

namespace {

using runtime::simir::Operation;
using runtime::simir::Process;
using runtime::simir::ProcessId;
using runtime::simir::SignalId;
using runtime::simir::VitalDelay;
using runtime::SimulationTick;

std::string lower(const std::string_view text)
{
    std::string result { text };
    for (auto& character : result) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

// The leaf of a hierarchical name.
std::string_view leaf_of(const std::string_view name)
{
    return name.substr(name.rfind('.') == std::string_view::npos
            ? 0U
            : name.rfind('.') + 1U);
}

bool is_adapter(const std::string_view name)
{
    const auto leaf = leaf_of(name);
    return leaf.starts_with("port_result_") || leaf.starts_with("port_actual_");
}

struct Site {
    std::size_t process { };
    std::size_t index { };
    std::size_t element { };
};

} // namespace

VitalWireSourcesResult apply_vital_wire_sources(ElaboratedDesignState& state,
    const std::span<const VitalWireLocations> locations)
{
    using namespace runtime::simir;
    VitalWireSourcesResult result;
    for (const auto& location : locations) {
        const auto fail = [&](const std::string& why) {
            result.failures.push_back("the multiple source mode of the generic '"
                + location.generic + "' of instance '" + location.instance
                + "' (IEEE 1076.4-2000 5.2.7.1.5) cannot be set up: " + why);
        };
        // The VitalWireDelay calls that name the generic: their wire delayed
        // signal is a signal of the instance itself. A scalar generic has one
        // call; a vector generic has one call for each element, an expansion
        // of the generate over the bus whose delay actual is `tipd_A(i)`.
        const auto instance = lower(location.instance);
        std::vector<Site> sites;
        for (std::size_t index = 0U; index < state.processes.size(); ++index) {
            const auto& process = state.processes[index];
            if (!lower(process.name).starts_with(instance + '.')) {
                continue;
            }
            for (std::size_t position = 0U; position < process.operations.size();
                ++position) {
                auto operation = process.operations.expanded(position);
                const auto* const delay = operation_get_if<VitalDelay>(&operation);
                if (delay == nullptr || delay->kind != VitalDelayKind::wire
                    || delay->wire->wire_generic != location.generic
                    || delay->output >= state.signal_info.size()
                    || delay->wire->wire_index.has_value() != location.vector_form) {
                    continue;
                }
                const auto name = lower(state.signal_info[delay->output].name);
                if (name.starts_with(instance + '.')
                    && name.find('.', instance.size() + 1U)
                        == std::string::npos) {
                    const auto element = location.vector_form
                        ? static_cast<std::int64_t>(
                              *delay->wire->wire_index < location.left
                                  ? location.left - *delay->wire->wire_index
                                  : *delay->wire->wire_index - location.left)
                        : std::int64_t { };
                    sites.push_back({ index, position,
                        static_cast<std::size_t>(element) });
                }
            }
        }
        // Only the elements with more than one source need their call; the
        // others keep the plain behaviour.
        std::vector<Site> calls;
        bool calls_found = true;
        for (std::size_t element = 0U; element < location.elements.size();
            ++element) {
            if (location.elements[element].sources.empty()) {
                continue;
            }
            const auto count = std::ranges::count(
                sites, element, &Site::element);
            if (count != 1) {
                fail((location.vector_form
                         ? "element " + std::to_string(element) + ": "
                         : std::string { })
                    + (count == 0
                            ? "no VitalWireDelay call of the instance uses "
                              "the generic"
                            : "more than one VitalWireDelay call of the "
                              "instance uses the generic"));
                calls_found = false;
                continue;
            }
            calls.push_back(*std::ranges::find(sites, element, &Site::element));
        }
        if (!calls_found || calls.empty()) {
            continue;
        }
        // The actual of the InSig port of each call; all calls read one bus.
        std::optional<SignalId> input;
        bool consistent = true;
        std::size_t slot_count { };
        for (const auto& site : calls) {
            auto call = state.processes[site.process].operations.expanded(
                site.index);
            const auto* const delay = operation_get_if<VitalDelay>(&call);
            const auto& process = state.processes[site.process];
            // A call on one element extracts it from the register that reads
            // the bus.
            auto wanted = delay->source;
            if (delay->wire->input_offset) {
                for (std::size_t position = 0U; position < site.index;
                    ++position) {
                    auto operation = process.operations.expanded(position);
                    if (const auto* const extract
                        = operation_get_if<Extract>(&operation);
                        extract != nullptr
                        && extract->destination == delay->source) {
                        wanted = extract->source;
                    }
                }
            }
            std::optional<SignalId> read_signal;
            for (std::size_t position = 0U; position < site.index; ++position) {
                auto operation = process.operations.expanded(position);
                if (const auto* const read = operation_get_if<ReadSignal>(&operation);
                    read != nullptr && read->destination == wanted) {
                    read_signal = read->signal;
                }
            }
            if (!read_signal || *read_signal >= state.signals.size()
                || (input && *input != *read_signal)) {
                fail("the call does not read the actual of its InSig port");
                consistent = false;
                break;
            }
            input = read_signal;
            slot_count = delay->shape == VitalDelayShape::single
                ? 1U : delay->shape == VitalDelayShape::delay01 ? 2U : 6U;
            const auto& element = location.elements[site.element];
            if (element.slots.size() != slot_count
                || std::ranges::any_of(element.sources,
                    [&](const VitalWireSourceLocation& each) {
                        return each.slots.size() != slot_count;
                    })) {
                fail("the generic does not hold the delays of the call's delay "
                     "type");
                consistent = false;
                break;
            }
        }
        if (!consistent) {
            continue;
        }

        // The writers of a signal: the processes that drive it.
        const auto writers_of = [&](const SignalId signal) {
            std::vector<ProcessId> writers;
            for (const auto& process : state.processes) {
                bool writes = std::ranges::any_of(process.driver_regions,
                    [&](const Process::DriverRegion& region) {
                        return region.signal == signal;
                    });
                for (const auto* const terminal :
                    { &process.switch_source, &process.switch_target }) {
                    writes = writes || (terminal->has_value() && **terminal == signal);
                }
                if (writes) {
                    writers.push_back(process.id);
                }
            }
            return writers;
        };
        // The actual of InSig is the net its port connection copies into the
        // instance; its sources are the drivers of that net.
        auto net = *input;
        auto writers = writers_of(net);
        // Port actuals can forward in a ring: stop on a net seen before.
        std::vector<SignalId> visited_nets { net };
        while (!writers.empty()) {
            std::optional<SignalId> origin;
            bool forwarded = true;
            for (const auto writer : writers) {
                const auto& process = state.processes[writer];
                if (!lower(leaf_of(process.name)).starts_with("port_actual_")
                    || process.static_sensitivity.size() != 1U
                    || process.static_sensitivity.front().signal == net
                    || process.static_sensitivity.front().signal
                        >= state.signal_info.size()
                    || state.signal_info[process.static_sensitivity.front().signal]
                            .width
                        != state.signal_info[net].width
                    || (origin
                        && *origin != process.static_sensitivity.front().signal)) {
                    forwarded = false;
                    break;
                }
                origin = process.static_sensitivity.front().signal;
            }
            if (!forwarded || !origin || *origin >= state.signals.size()
                || std::ranges::find(visited_nets, *origin)
                    != visited_nets.end()) {
                break;
            }
            net = *origin;
            visited_nets.push_back(net);
            writers = writers_of(net);
        }
        if (writers.empty()) {
            fail("the actual of the InSig port has no driver");
            continue;
        }

        // The source each writer stands for: the SDF source whose instance
        // holds it, nearest first; a port connection process is placed by
        // the signals it reads. No source: the writer takes the generic's
        // value.
        std::vector<std::string> source_instances;
        bool distinct = true;
        for (const auto& site : calls) {
            std::vector<std::string> named;
            for (const auto& source : location.elements[site.element].sources) {
                const auto source_instance = lower(source.source_instance);
                if (std::ranges::find(named, source_instance) != named.end()) {
                    fail("the sources '" + source.source_instance + "/"
                        + source.source_port + "' and another port of the same "
                          "instance reach one element; their drivers cannot be "
                          "told apart");
                    distinct = false;
                }
                named.push_back(source_instance);
                if (std::ranges::find(source_instances, source_instance)
                    == source_instances.end()) {
                    source_instances.push_back(source_instance);
                }
            }
        }
        if (!distinct) {
            continue;
        }
        const auto source_of = [&](std::string_view path)
            -> std::optional<std::size_t> {
            std::string text { path };
            for (auto dot = text.rfind('.'); dot != std::string::npos;
                dot = text.rfind('.')) {
                text.resize(dot);
                const auto found = std::ranges::find(source_instances, text);
                if (found != source_instances.end()) {
                    return static_cast<std::size_t>(
                        std::distance(source_instances.begin(), found));
                }
            }
            return std::nullopt;
        };
        constexpr auto unnamed = std::numeric_limits<std::size_t>::max();
        std::map<std::size_t, std::vector<ProcessId>> groups;
        for (const auto writer : writers) {
            const auto& process = state.processes[writer];
            std::optional<std::size_t> source;
            if (is_adapter(process.name)) {
                for (const auto& sensitivity : process.static_sensitivity) {
                    if (sensitivity.signal < state.signals.size()
                        && (source = source_of(
                                lower(state.signals[sensitivity.signal].name)))) {
                        break;
                    }
                }
            }
            if (!source) {
                source = source_of(lower(process.name));
            }
            groups[source.value_or(unnamed)].push_back(writer);
        }
        bool complete = true;
        for (std::size_t index = 0U; index < source_instances.size(); ++index) {
            if (!groups.contains(index)) {
                fail("the source '" + source_instances[index]
                    + "' drives no part of the net");
                complete = false;
            }
        }
        if (!complete) {
            continue;
        }
        std::map<ProcessId, std::vector<std::size_t>> positions;
        for (const auto& [source, group] : groups) {
            for (const auto writer : group) {
                if (!dual_write::net_writes(
                        state.processes[writer], net, positions[writer], true)) {
                    fail("the driver '" + state.processes[writer].name
                        + "' reads the net or writes it with an operation the "
                          "per-source shadow does not cover");
                    complete = false;
                }
            }
        }
        if (std::ranges::any_of(groups, [&](const auto& group) {
                return std::ranges::any_of(calls, [&](const Site& call) {
                    return std::ranges::find(group.second,
                               static_cast<ProcessId>(call.process))
                        != group.second.end();
                });
            })) {
            fail("the process of the VitalWireDelay call also drives the net");
            complete = false;
        }
        if (!complete) {
            continue;
        }

        // The shadows and the dual writes.
        std::vector<std::pair<std::size_t, SignalId>> shadows;
        std::size_t number { };
        for (const auto& [source, group] : groups) {
            const auto shadow = static_cast<SignalId>(state.signals.size());
            auto signal = state.signals[net];
            signal.name = state.signals[net].name + ".$vital_source"
                + std::to_string(number++);
            signal.implicit_driver.reset();
            signal.charge_strength.reset();
            signal.charge_decay.reset();
            auto info = state.signal_info[net];
            info.id = shadow;
            info.name = signal.name;
            info.is_port = false;
            info.direction = frontend::PortDirection::Unknown;
            state.signals.push_back(std::move(signal));
            state.signal_info.push_back(std::move(info));
            for (const auto writer : group) {
                auto& process = state.processes[writer];
                std::vector<std::pair<std::size_t, Operation>> inserted;
                for (const auto position : positions.at(writer)) {
                    auto operation = process.operations.expanded(position);
                    if (auto* const vital = operation_get_if<VitalDelay>(&operation)) {
                        vital->wire.edit().mirrors.push_back(shadow);
                        process.operations.replace(position, std::move(operation));
                        continue;
                    }
                    *dual_write::write_target(operation) = shadow;
                    inserted.emplace_back(position, std::move(operation));
                }
                const auto regions = process.driver_regions;
                for (auto region : regions) {
                    if (region.signal == net) {
                        region.signal = shadow;
                        process.driver_regions.push_back(region);
                    }
                }
                if (!inserted.empty()) {
                    dual_write::insert_after(process, inserted);
                }
            }
            shadows.emplace_back(source, shadow);
        }
        // Each call selects among the sources of its element: a source that
        // an entry names for the element has that location, any other
        // source (including the drivers no entry names) the element's value.
        for (const auto& site : calls) {
            const auto& element = location.elements[site.element];
            std::vector<VitalWireSource> sources;
            for (const auto& [source, shadow] : shadows) {
                const std::vector<std::int64_t>* slots = &element.slots;
                if (source != unnamed) {
                    const auto& wanted = source_instances[source];
                    const auto named = std::ranges::find_if(element.sources,
                        [&](const VitalWireSourceLocation& each) {
                            return lower(each.source_instance) == wanted;
                        });
                    if (named != element.sources.end()) {
                        slots = &named->slots;
                    }
                }
                VitalWireSource wire_source;
                wire_source.shadow = shadow;
                for (std::size_t index = 0U; index < slots->size(); ++index) {
                    wire_source.delays[index] = (*slots)[index] < 0
                        ? SimulationTick { }
                        : static_cast<SimulationTick>((*slots)[index]);
                }
                sources.push_back(wire_source);
            }
            auto call = state.processes[site.process].operations.expanded(
                site.index);
            operation_get_if<VitalDelay>(&call)->wire.edit().wire_sources
                = std::move(sources);
            state.processes[site.process].operations.replace(
                site.index, std::move(call));
        }
    }
    state.signal_driver_inventory.reset();
    return result;
}

} // namespace fsim::elaboration
