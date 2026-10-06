// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 static kernel planning (docs/simulation-engine-v4-plan.md).
// Members are SystemVerilog processes with a static shape:
//
// - combinational: one any-change static wait and a loop jump;
// - sequential: one posedge/negedge static wait at the top and a loop jump;
// - once: no sensitivity, a terminating halt.
//
// Their bodies may use only the operations the kernel evaluates. A signal is
// kernel-owned when every writer is a member and nothing outside the members
// can observe it except through ordinary reads, which make it a boundary
// output. Memories are kernel-owned when every accessor is a member.
#include "fsim/elaboration/elaborator.hpp"
#include "elaborated_design_process_access.hpp"
#include "../runtime/simir_region_graph_bindings.hpp"
#include "../runtime/simir_region_graph_program_access.hpp"
#include "../runtime/simir_static_kernel.hpp"

#include <algorithm>
#include <cstdlib>
#include <cxxabi.h>
#include <typeinfo>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

void collect_container_objects(
    const Operation& operation, std::vector<ContainerObjectId>& objects)
{
    visit_operation([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, ReadContainerObject>
            || std::is_same_v<T, WriteContainerObject>
            || std::is_same_v<T, WriteContainerObjectElement>) {
            objects.push_back(value.object);
        } else if constexpr (std::is_same_v<T, PlaEvaluate>
            || std::is_same_v<T, WaitPla>) {
            objects.push_back(value.memory);
        }
    }, operation);
}

[[nodiscard]] std::optional<InstructionIndex> branch_target_outside(
    const Operation& operation, const std::size_t count)
{
    return visit_operation([&](const auto& value)
        -> std::optional<InstructionIndex> {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Jump>) {
            return value.target >= count ? std::optional { value.target }
                                         : std::nullopt;
        } else if constexpr (std::is_same_v<T, Branch>) {
            if (value.when_true >= count) {
                return value.when_true;
            }
            return value.when_false >= count ? std::optional { value.when_false }
                                             : std::nullopt;
        } else {
            return std::nullopt;
        }
    }, operation);
}

struct Candidate {
    ProcessId process { };
    std::uint8_t kind { };
    bool run_at_start { };
    bool vhdl { };
    std::uint32_t body_begin { };
    std::uint32_t body_end { };
    std::uint32_t exit_alt { std::numeric_limits<std::uint32_t>::max() };
    std::vector<Sensitivity> sensitivity;
};

} // namespace

StaticKernelPlan ElaboratedDesign::plan_static_kernel() const
{
    StaticKernelPlan plan;
    const auto disable = [&](const char* reason) {
        StaticKernelPlan disabled;
        disabled.disabled = true;
        disabled.disabled_reason = reason;
        return disabled;
    };
    if (code_coverage_inventory_) {
        return disable("coverage");
    }
    if (!verilog_specify_paths_.empty() || !verilog_timing_checks_.empty()) {
        return disable("specify");
    }
    if (!systemc_processes_.empty() || !systemc_objects_.empty()) {
        return disable("systemc");
    }

    const auto process_count = this->process_count();
    const auto signal_count = signals_.size();
    std::vector<ProcessProgramView> views;
    views.reserve(process_count);
    for (std::size_t index = 0U; index < process_count; ++index) {
        views.push_back(detail::ElaboratedDesignProcessAccess::process_view(
            *this, index));
    }
    for (const auto& view : views) {
        const auto& operations = view.operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            if (operation_holds<VcdControl>(operations[op])) {
                return disable("vcd_control");
            }
        }
    }
    if (std::ranges::any_of(signals_, [](const auto& signal) {
            return signal.initial_value.width()
                > std::numeric_limits<std::uint32_t>::max();
        })) {
        return disable("signal_width");
    }
    const auto graph_owner
        = detail::ElaboratedDesignProcessAccess::region_graph(*this);
    const auto& graph = *graph_owner;
    const auto graph_signals = graph.signals();
    const auto graph_processes = graph.processes();
    if (std::ranges::any_of(graph_processes,
            &RegionProcessNode::dependencies_unknown)) {
        // Fail closed: an opaque access could read kernel-owned state.
        return disable("unknown_dependencies");
    }
    std::vector<std::uint8_t> alias_signal(signal_count, 0U);
    for (const auto& family : graph.signal_alias_families()) {
        if (family.proxy < signal_count) {
            alias_signal[family.proxy] = 1U;
        }
    }
    for (const auto& alias : container_signal_aliases_) {
        if (alias.signal < signal_count) {
            alias_signal[alias.signal] = 1U;
        }
    }
    for (const auto& alias : container_element_signal_aliases_) {
        if (alias.signal < signal_count) {
            alias_signal[alias.signal] = 1U;
        }
    }
    for (const auto& alias : container_aggregate_signal_aliases_) {
        if (alias.signal < signal_count) {
            alias_signal[alias.signal] = 1U;
        }
    }

    // 1. Classify member candidates.
    std::vector<std::optional<Candidate>> candidates(process_count);
    std::vector<std::vector<ContainerObjectId>> container_uses(process_count);
    for (std::size_t index = 0U; index < process_count; ++index) {
        const auto& view = views[index];
        if (!view.valid()) {
            continue;
        }
        const auto& operations = view.operations();
        const auto count = operations.size();
        for (std::size_t op = 0U; op < count; ++op) {
            collect_container_objects(
                operations.expanded(op), container_uses[index]);
        }
        std::ranges::sort(container_uses[index]);
        container_uses[index].erase(std::unique(container_uses[index].begin(),
            container_uses[index].end()), container_uses[index].end());
        const bool vhdl = view.scheduling_domain()
            != ProcessSchedulingDomain::systemverilog;
        if (view.observed() || view.reactive() || view.postponed()
            || view.final() || !view.initialize()
            || view.switch_source() || view.switch_target()
            || view.switch_control() || view.switch_bidirectional()
            || view.switch_resistive()
            || view.drive_strength() != DriveStrength { }
            || view.string_register_count() != 0U
            || (vhdl && view.container_register_count() != 0U)
            || (vhdl && view.language_standard() == "2019")
            || (!vhdl && std::ranges::any_of(view.register_value_kinds(),
                [](const ValueKind kind) { return kind != ValueKind::logic4; }))
            || count < 2U) {
            continue;
        }
        Candidate member;
        member.process = static_cast<ProcessId>(index);
        member.vhdl = vhdl;
        const auto& sensitivity = view.static_sensitivity();
        std::optional<std::size_t> wait;
        bool shape_ok = true;
        for (std::size_t op = 0U; op < count; ++op) {
            if (operation_holds<WaitSensitivity>(operations.expanded(op))) {
                if (wait) {
                    shape_ok = false;
                }
                wait = op;
            }
        }
        if (!shape_ok) {
            continue;
        }
        const auto loop_jump = [&](const std::size_t op) {
            if (op >= count) {
                return false;
            }
            const auto operation = operations.expanded(op);
            const auto* jump = operation_get_if<Jump>(&operation);
            return jump != nullptr && jump->target == 0U;
        };
        // Structural operations excluded from the supported-operation check.
        std::size_t structural_a = count;
        std::size_t structural_b = count;
        if (vhdl) {
            // A VHDL process with one static wait W runs from operation 0 to
            // W at initialization; each activation resumes at W + 1 and runs
            // until it reaches W again. A process without sensitivity runs
            // once to its halt.
            if (sensitivity.empty()) {
                std::optional<std::size_t> halt;
                for (std::size_t op = 0U; op < count && shape_ok; ++op) {
                    const auto operation = operations.expanded(op);
                    if (const auto* value = operation_get_if<Halt>(&operation)) {
                        shape_ok = !halt && !value->program_exit;
                        halt = op;
                    }
                }
                if (wait || !halt || !shape_ok) {
                    continue;
                }
                member.kind = 2U;
                member.body_begin = 0U;
                member.body_end = static_cast<std::uint32_t>(*halt);
                structural_a = *halt;
            } else {
                const bool any = std::ranges::all_of(sensitivity,
                    [](const Sensitivity& entry) {
                        return entry.edge == EdgeKind::any;
                    });
                const bool edges = std::ranges::all_of(sensitivity,
                    [](const Sensitivity& entry) {
                        return entry.edge == EdgeKind::posedge
                            || entry.edge == EdgeKind::negedge;
                    });
                if (!wait || *wait + 1U >= count || !(any || edges)
                    || std::ranges::any_of(sensitivity,
                        [&](const Sensitivity& entry) {
                            return entry.signal >= signal_count;
                        })) {
                    continue;
                }
                member.kind = any ? 0U : 1U;
                member.run_at_start = *wait != 0U;
                member.body_begin = static_cast<std::uint32_t>(*wait + 1U);
                member.body_end = static_cast<std::uint32_t>(*wait);
                member.sensitivity = sensitivity;
                structural_a = *wait;
            }
        } else if (sensitivity.empty()) {
            const auto tail = operations.expanded(count - 1U);
            const auto* halt = operation_get_if<Halt>(&tail);
            if (wait || halt == nullptr || halt->program_exit) {
                continue;
            }
            member.kind = 2U;
            member.body_begin = 0U;
            member.body_end = static_cast<std::uint32_t>(count - 1U);
            structural_a = count - 1U;
        } else {
            if (!wait) {
                continue;
            }
            const bool any = std::ranges::all_of(sensitivity,
                [](const Sensitivity& entry) {
                    return entry.edge == EdgeKind::any;
                });
            const bool edges = std::ranges::all_of(sensitivity,
                [](const Sensitivity& entry) {
                    return entry.edge == EdgeKind::posedge
                        || entry.edge == EdgeKind::negedge;
                });
            // SystemVerilog bodies end at the loop jump. VHDL subprogram
            // bodies may follow it and are reached only through Call.
            if (*wait != 0U && any && loop_jump(*wait + 1U)
                && (vhdl || *wait == count - 2U)) {
                member.kind = 0U;
                member.run_at_start = true;
                member.body_begin = 0U;
                member.body_end = static_cast<std::uint32_t>(*wait);
                member.exit_alt = static_cast<std::uint32_t>(*wait + 1U);
                structural_a = *wait;
                structural_b = *wait + 1U;
            } else if (*wait == 0U && (any || edges)) {
                std::size_t jump = 1U;
                while (jump < count && !loop_jump(jump)) {
                    ++jump;
                }
                if (jump == count || (!vhdl && jump != count - 1U)) {
                    continue;
                }
                member.kind = any ? 0U : 1U;
                member.body_begin = 1U;
                member.body_end = static_cast<std::uint32_t>(jump);
                member.exit_alt = 0U;
                structural_a = 0U;
                structural_b = jump;
            } else {
                continue;
            }
            if (std::ranges::any_of(sensitivity, [&](const Sensitivity& entry) {
                    return entry.signal >= signal_count;
                })) {
                continue;
            }
            member.sensitivity = sensitivity;
        }
        bool supported = true;
        for (std::size_t op = 0U; op < count && supported; ++op) {
            if (op == structural_a || op == structural_b) {
                continue;
            }
            const auto operation = operations.expanded(op);
            supported = (vhdl ? static_kernel_vhdl_operation_supported(operation)
                              : static_kernel_operation_supported(operation))
                && !branch_target_outside(operation, count);
            if (supported) {
                visit_operation([&](const auto& value) {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (requires { value.signal; }) {
                        if constexpr (std::is_same_v<
                                          std::decay_t<decltype(value.signal)>,
                                          SignalId>) {
                            supported = value.signal < signal_count;
                        }
                    }
                    if constexpr (std::is_same_v<T, Call>) {
                        supported = value.target < count
                            && value.return_target < count;
                    }
                }, operation);
            }
        }
        if (!supported) {
            continue;
        }
        candidates[index] = std::move(member);
    }
    // A kernel runs in one language mode: VHDL members follow exact delta
    // semantics, SystemVerilog members the levelized Active-region schedule.
    std::size_t vhdl_candidates = 0U;
    std::size_t sv_candidates = 0U;
    for (const auto& candidate : candidates) {
        if (candidate) {
            ++(candidate->vhdl ? vhdl_candidates : sv_candidates);
        }
    }
    const bool vhdl_mode = vhdl_candidates > sv_candidates;
    for (auto& candidate : candidates) {
        if (candidate && candidate->vhdl != vhdl_mode) {
            candidate.reset();
        }
    }

    // 2. Alias structure: per-element arrays (leaves plus an optional
    // aggregate proxy) and memories stored as one packed signal are owned
    // as units together with their container object.
    const auto object_count = container_objects_.size();
    std::vector<std::vector<SignalId>> element_signals(object_count);
    std::vector<std::uint8_t> element_complete(object_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        element_signals[object].assign(
            container_objects_[object].initial_value.elements.size(),
            std::numeric_limits<SignalId>::max());
    }
    for (const auto& alias : container_element_signal_aliases_) {
        if (alias.object < object_count
            && alias.ordinal < element_signals[alias.object].size()) {
            element_signals[alias.object][alias.ordinal] = alias.signal;
        }
    }
    for (std::size_t object = 0U; object < object_count; ++object) {
        element_complete[object] = !element_signals[object].empty()
            && std::ranges::none_of(element_signals[object],
                [](const SignalId signal) {
                    return signal == std::numeric_limits<SignalId>::max();
                })
            ? 1U : 0U;
    }
    std::vector<std::optional<SignalId>> packed_alias(object_count);
    std::vector<std::uint8_t> has_element_alias(object_count, 0U);
    for (const auto& alias : container_signal_aliases_) {
        if (alias.object < object_count) {
            packed_alias[alias.object] = alias.signal;
        }
    }
    for (const auto& alias : container_element_signal_aliases_) {
        if (alias.object < object_count) {
            has_element_alias[alias.object] = 1U;
        }
    }
    std::vector<const RegionSignalAliasFamilyDescriptor*> family_of_object(
        object_count, nullptr);
    std::vector<std::uint8_t> proxy_signal(signal_count, 0U);
    for (const auto& family : graph.signal_alias_families()) {
        if (family.object < object_count) {
            family_of_object[family.object] = &family;
        }
        if (family.proxy < signal_count) {
            proxy_signal[family.proxy] = 1U;
        }
    }
    std::vector<std::uint8_t> container_shape_ok(object_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        const auto& container = container_objects_[object];
        const auto& type = container.initial_value.type;
        container_shape_ok[object] = type.fixed && !type.associative
            && !container.slice_alias
            && (type.element_kind == ContainerElementKind::Packed
                || type.element_kind == ContainerElementKind::Scalar)
            && !(packed_alias[object] && has_element_alias[object])
            ? 1U : 0U;
    }
    std::vector<std::vector<ProcessId>> accessors(object_count);
    for (std::size_t index = 0U; index < process_count; ++index) {
        for (const auto object : container_uses[index]) {
            if (object < object_count) {
                accessors[object].push_back(static_cast<ProcessId>(index));
            }
        }
    }

    // Signals a non-member touches through anything but a current-value
    // read: writes, forces, history and event queries, sampled reads. None
    // of these may target kernel-owned state.
    std::vector<std::uint8_t> host_touched(signal_count, 0U);
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (candidates[index]) {
            continue;
        }
        const auto& operations = views[index].operations();
        for (std::size_t op = 0U; op < operations.size(); ++op) {
            visit_operation([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, ReadSignal>) {
                    if (value.kind != SignalReadKind::current
                        || value.ticks != 1U || value.clock || value.gate) {
                        if (value.signal < signal_count) {
                            host_touched[value.signal] = 1U;
                        }
                    }
                } else if constexpr (requires { value.signal; }) {
                    if constexpr (std::is_same_v<
                                      std::decay_t<decltype(value.signal)>,
                                      SignalId>) {
                        if (value.signal < signal_count) {
                            host_touched[value.signal] = 1U;
                        }
                    }
                }
            }, operations.expanded(op));
        }
    }
    const auto basic_ok = [&](const SignalId signal) {
        const auto& node = graph_signals[signal];
        const auto& info = signals_[signal];
        // Members make only zero-delay inertial projected writes, which
        // never leave pending transactions.
        return !node.dynamic_fork_writers
            && (vhdl_mode || !node.partial_projected_transactions)
            && host_touched[signal] == 0U
            && (info.value_kind == ValueKind::logic4 || vhdl_mode)
            && info.systemverilog_scalar == runtime::SystemVerilogScalarKind::None
            && !info.event_variable && !info.implicit_driver
            && !info.charge_strength
            && signals_[signal].initial_value.width() != 0U
            && (info.resolution == ResolutionKind::none
                || info.resolution == ResolutionKind::sv_wire
                || (vhdl_mode && info.resolution == ResolutionKind::std_logic));
    };
    const auto writers_ok = [&](const SignalId signal) {
        const auto& node = graph_signals[signal];
        if (std::ranges::any_of(node.writers, [&](const auto& writer) {
                return writer.process >= process_count
                    || !candidates[writer.process];
            })) {
            return false;
        }
        const auto resolution = signals_[signal].resolution;
        const auto width = static_cast<std::uint32_t>(
            signals_[signal].initial_value.width());
        if (vhdl_mode && (resolution == ResolutionKind::sv_wire
                || resolution == ResolutionKind::std_logic)) {
            // Each element must have exactly one driving process, so the
            // resolution function is the identity; a resolved std_logic
            // signal must also have a driver for every element.
            std::map<ProcessId, std::vector<std::pair<std::uint32_t,
                std::uint32_t>>> per_process;
            for (const auto& writer : node.writers) {
                if (writer.width == 0U) {
                    per_process[writer.process].emplace_back(0U, width);
                } else {
                    per_process[writer.process].emplace_back(
                        writer.offset, writer.offset + writer.width);
                }
            }
            for (auto& [process, ranges] : per_process) {
                std::ranges::sort(ranges);
                // Merge this process's own ranges.
                std::vector<std::pair<std::uint32_t, std::uint32_t>> merged;
                for (const auto& range : ranges) {
                    if (!merged.empty() && range.first <= merged.back().second) {
                        merged.back().second
                            = std::max(merged.back().second, range.second);
                    } else {
                        merged.push_back(range);
                    }
                }
                ranges = std::move(merged);
            }
            std::vector<std::pair<std::uint32_t, std::uint32_t>> regions;
            for (const auto& [process, ranges] : per_process) {
                regions.insert(regions.end(), ranges.begin(), ranges.end());
            }
            std::ranges::sort(regions);
            std::uint64_t covered = 0U;
            for (std::size_t index = 0U; index < regions.size(); ++index) {
                if (index != 0U
                    && regions[index - 1U].second > regions[index].first) {
                    return false;
                }
                covered += regions[index].second - regions[index].first;
            }
            return resolution != ResolutionKind::std_logic || covered == width;
        }
        if (resolution == ResolutionKind::sv_wire) {
            // A resolved net may be kernel-owned only when its drivers cover
            // pairwise-disjoint bits, so no resolution is needed.
            std::vector<ProcessId> writers;
            for (const auto& writer : node.writers) {
                writers.push_back(writer.process);
            }
            std::ranges::sort(writers);
            writers.erase(std::unique(writers.begin(), writers.end()),
                writers.end());
            if (writers.size() > 1U) {
                std::vector<std::pair<std::uint32_t, std::uint32_t>> regions;
                for (const auto writer : writers) {
                    for (const auto& region : views[writer].driver_regions()) {
                        if (region.signal == signal) {
                            regions.emplace_back(
                                region.whole ? 0U : region.offset,
                                region.whole ? width : region.width);
                        }
                    }
                }
                std::ranges::sort(regions);
                for (std::size_t index = 1U; index < regions.size(); ++index) {
                    if (regions[index - 1U].first + regions[index - 1U].second
                        > regions[index].first) {
                        return false;
                    }
                }
            }
        }
        return true;
    };
    const auto signal_ok = [&](const SignalId signal) {
        return signal < signal_count && basic_ok(signal) && writers_ok(signal);
    };
    const auto edge_sensitive_member = [&](const SignalId signal) {
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (candidates[index]
                && std::ranges::any_of(candidates[index]->sensitivity,
                    [&](const Sensitivity& entry) {
                        return entry.signal == signal
                            && entry.edge != EdgeKind::any;
                    })) {
                return true;
            }
        }
        return false;
    };

    std::vector<std::uint8_t> owned(signal_count, 0U);
    std::vector<std::uint8_t> owned_container(object_count, 0U);
    std::vector<std::uint8_t> owned_family(object_count, 0U);
    std::vector<std::uint8_t> alias_role(signal_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        for (const auto signal : element_signals[object]) {
            if (signal < signal_count) {
                alias_role[signal] = 1U;
            }
        }
        if (packed_alias[object] && *packed_alias[object] < signal_count) {
            alias_role[*packed_alias[object]] = 1U;
        }
    }
    for (std::size_t signal = 0U; signal < signal_count; ++signal) {
        if (proxy_signal[signal] != 0U) {
            alias_role[signal] = 1U;
        }
    }
    for (const auto& alias : container_aggregate_signal_aliases_) {
        if (alias.signal < signal_count) {
            alias_role[alias.signal] = 1U;
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        std::ranges::fill(owned, 0U);
        std::ranges::fill(owned_container, 0U);
        std::ranges::fill(owned_family, 0U);
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            if (alias_role[signal] == 0U
                && !graph_signals[signal].writers.empty()
                && signal_ok(signal)) {
                owned[signal] = 1U;
            }
        }
        for (std::size_t object = 0U; object < object_count; ++object) {
            if (container_shape_ok[object] == 0U || vhdl_mode
                || std::ranges::any_of(accessors[object],
                    [&](const ProcessId process) {
                        return !candidates[process];
                    })) {
                continue;
            }
            if (packed_alias[object]) {
                const auto signal = *packed_alias[object];
                if (signal_ok(signal)) {
                    owned[signal] = 1U;
                    owned_container[object] = 1U;
                }
                continue;
            }
            if (has_element_alias[object] == 0U) {
                owned_container[object] = 1U;
                continue;
            }
            if (element_complete[object] == 0U) {
                continue;
            }
            const auto* family = family_of_object[object];
            if (family != nullptr
                && (!family->complete || family->proxy >= signal_count
                    || !signal_ok(family->proxy)
                    || edge_sensitive_member(family->proxy))) {
                continue;
            }
            if (!std::ranges::all_of(element_signals[object], signal_ok)) {
                continue;
            }
            for (const auto signal : element_signals[object]) {
                owned[signal] = 1U;
            }
            owned_container[object] = 1U;
            owned_family[object] = family != nullptr ? 1U : 0U;
        }
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (candidates[index]
                && std::ranges::any_of(container_uses[index],
                    [&](const ContainerObjectId object) {
                        return object >= object_count
                            || owned_container[object] == 0U;
                    })) {
                candidates[index].reset();
                changed = true;
            }
        }
    }

    const bool diagnose = std::getenv("FSIM_PROFILE_KERNEL_PLAN") != nullptr;
    if (diagnose) {
        std::map<std::string, std::size_t> reasons;
        std::map<std::string, std::string> examples;
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            if (graph_signals[signal].writers.empty() || owned[signal] != 0U
                || proxy_signal[signal] != 0U) {
                continue;
            }
            const auto& node = graph_signals[signal];
            std::string reason = alias_role[signal] != 0U ? "alias:" : "";
            if (!basic_ok(signal)) {
                reason += host_touched[signal] != 0U ? "host_touched" : "basic";
            } else if (std::ranges::any_of(node.writers, [&](const auto& writer) {
                           return !candidates[writer.process];
                       })) {
                reason += "host_writer";
            } else if (!writers_ok(signal)) {
                reason += "undeclared_or_resolved";
            } else {
                reason += "container_unit";
            }
            ++reasons[reason];
            examples.emplace(reason, signals_[signal].name);
        }
        for (const auto& [reason, count] : reasons) {
            std::cerr << "fsim-kernel-plan: unowned reason=" << reason
                      << " count=" << count << " example=" << examples[reason]
                      << '\n';
        }
        std::map<std::string, std::size_t> rejected;
        std::map<std::string, std::string> rejected_examples;
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (candidates[index] || !views[index].valid()) {
                continue;
            }
            const auto& operations = views[index].operations();
            std::string reason = "shape";
            std::set<std::string> unsupported;
            for (std::size_t op = 0U; op < operations.size(); ++op) {
                const auto operation = operations.expanded(op);
                const bool vhdl_process = views[index].scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog;
                if (!(vhdl_process
                            ? static_kernel_vhdl_operation_supported(operation)
                            : static_kernel_operation_supported(operation))
                    && !operation_holds<WaitSensitivity>(operation)
                    && !operation_holds<Halt>(operation)) {
                    visit_operation([&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        int status = 0;
                        char* name = abi::__cxa_demangle(
                            typeid(T).name(), nullptr, nullptr, &status);
                        std::string text = name != nullptr ? name : "?";
                        std::free(name);
                        if (const auto colon = text.rfind("::");
                            colon != std::string::npos) {
                            text = text.substr(colon + 2U);
                        }
                        unsupported.insert(text);
                    }, operation);
                }
            }
            if (!unsupported.empty()) {
                reason = "ops:";
                for (const auto& name : unsupported) {
                    reason += name + ",";
                }
            }
            if (reason == "shape" && !container_uses[index].empty()) {
                reason = "container";
            }
            ++rejected[reason];
            rejected_examples.emplace(reason, views[index].name());
        }
        if (const char* dump = std::getenv("FSIM_PROFILE_KERNEL_PLAN_DUMP")) {
            std::size_t dumped = 0U;
            for (std::size_t index = 0U; index < process_count && dumped < 12U;
                 ++index) {
                if (!views[index].valid()
                    || views[index].name().find(dump) == std::string::npos) {
                    continue;
                }
                ++dumped;
                const auto& operations = views[index].operations();
                std::cerr << "fsim-kernel-plan-dump: " << views[index].name()
                          << " candidate=" << (candidates[index] ? 1 : 0)
                          << " domain="
                          << static_cast<int>(views[index].scheduling_domain())
                          << " sens=";
                for (const auto& entry : views[index].static_sensitivity()) {
                    std::cerr << entry.signal << '/'
                              << static_cast<int>(entry.edge) << ',';
                }
                std::cerr << " kinds=";
                for (const auto kind : views[index].register_value_kinds()) {
                    std::cerr << static_cast<int>(kind);
                }
                std::cerr << '\n';
                for (std::size_t op = 0U; op < operations.size(); ++op) {
                    visit_operation([&](const auto& value) {
                        using T = std::decay_t<decltype(value)>;
                        int status = 0;
                        char* name = abi::__cxa_demangle(
                            typeid(T).name(), nullptr, nullptr, &status);
                        std::string text = name != nullptr ? name : "?";
                        std::free(name);
                        if (const auto colon = text.rfind("::");
                            colon != std::string::npos) {
                            text = text.substr(colon + 2U);
                        }
                        std::cerr << "  " << op << ' ' << text;
                        if constexpr (requires { value.destination; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.destination)>>) {
                                std::cerr << " d=" << value.destination;
                            }
                        }
                        if constexpr (requires { value.signal; }) {
                            if constexpr (std::is_same_v<std::decay_t<
                                              decltype(value.signal)>, SignalId>) {
                                std::cerr << " sig=" << value.signal << '('
                                          << signals_[value.signal].name << ')';
                            }
                        }
                        if constexpr (requires { value.source; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.source)>>) {
                                std::cerr << " src=" << value.source;
                            }
                        }
                        if constexpr (requires { value.left; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.left)>>) {
                                std::cerr << " l=" << value.left;
                            }
                        }
                        if constexpr (requires { value.right; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.right)>>) {
                                std::cerr << " r=" << value.right;
                            }
                        }
                        if constexpr (requires { value.condition; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.condition)>>) {
                                std::cerr << " c=" << value.condition;
                            }
                        }
                        if constexpr (requires { value.target; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.target)>>) {
                                std::cerr << " t=" << value.target;
                            }
                        }
                        if constexpr (requires { value.offset; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.offset)>>) {
                                std::cerr << " off=" << value.offset;
                            }
                        }
                        if constexpr (requires { value.width; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.width)>>) {
                                std::cerr << " w=" << value.width;
                            }
                        }
                        if constexpr (requires { value.operation; }) {
                            std::cerr << " op=" << static_cast<int>(value.operation);
                        }
                        if constexpr (requires { value.lhs; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.lhs)>>) {
                                std::cerr << " lhs=" << value.lhs
                                          << " rhs=" << value.rhs;
                            }
                        }
                        if constexpr (requires { value.delay; }) {
                            if constexpr (std::is_integral_v<std::decay_t<
                                              decltype(value.delay)>>) {
                                std::cerr << " delay=" << value.delay;
                            }
                        }
                        std::cerr << '\n';
                    }, operations.expanded(op));
                }
            }
        }
        for (const auto& [reason, count] : rejected) {
            std::cerr << "fsim-kernel-plan: rejected reason=" << reason
                      << " count=" << count
                      << " example=" << rejected_examples[reason] << '\n';
        }
    }

    if (vhdl_mode) {
        // Delta exactness holds only inside a closed VHDL kernel: every VHDL
        // process is a member, and every signal one member writes and
        // another reads is kernel-owned (a host round trip would add deltas).
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (views[index].valid() && !candidates[index]
                && views[index].scheduling_domain()
                    != ProcessSchedulingDomain::systemverilog) {
                if (diagnose) {
                    std::cerr << "fsim-kernel-plan: vhdl_partial process="
                              << views[index].name() << '\n';
                }
                return disable("vhdl_partial");
            }
        }
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            if (owned[signal] != 0U) {
                continue;
            }
            const auto& node = graph_signals[signal];
            const auto member_access = [&](const auto& accesses) {
                return std::ranges::any_of(accesses, [&](const auto& access) {
                    return access.process < process_count
                        && candidates[access.process];
                });
            };
            if (member_access(node.writers) && member_access(node.readers)) {
                if (diagnose) {
                    const auto& info = signals_[signal];
                    std::cerr << "fsim-kernel-plan: vhdl_unowned signal="
                              << info.name << " basic=" << basic_ok(signal)
                              << " writers_ok=" << writers_ok(signal)
                              << " host_touched=" << int { host_touched[signal] }
                              << " fork=" << node.dynamic_fork_writers
                              << " partial=" << node.partial_projected_transactions
                              << " kind=" << static_cast<int>(info.value_kind)
                              << " resolution=" << static_cast<int>(info.resolution)
                              << " implicit=" << info.implicit_driver.has_value()
                              << " width=" << signals_[signal].initial_value.width()
                              << " alias=" << int { alias_role[signal] }
                              << " writers=";
                    for (const auto& writer : node.writers) {
                        std::cerr << views[writer.process].name() << ',';
                    }
                    std::cerr << '\n';
                }
                return disable("vhdl_unowned");
            }
        }
    }

    // 3. Members, specification, host and boundaries.
    auto spec = std::make_shared<StaticKernelRuntimeSpec>();
    std::vector<std::uint8_t> member(process_count, 0U);
    for (std::size_t index = 0U; index < process_count; ++index) {
        if (!candidates[index]) {
            continue;
        }
        member[index] = 1U;
        auto& candidate = *candidates[index];
        StaticKernelMemberSpec runtime_member;
        runtime_member.process = candidate.process;
        runtime_member.kind = static_cast<StaticKernelMemberKind>(candidate.kind);
        runtime_member.run_at_start = candidate.run_at_start;
        runtime_member.body_begin = candidate.body_begin;
        runtime_member.body_end = candidate.body_end;
        runtime_member.exit_alt = candidate.exit_alt;
        runtime_member.sensitivity = std::move(candidate.sensitivity);
        spec->members.push_back(std::move(runtime_member));
    }
    if (spec->members.empty()) {
        return disable("no_members");
    }
    spec->vhdl = vhdl_mode;
    std::vector<std::uint8_t> virtual_owned(signal_count, 0U);
    for (std::size_t object = 0U; object < object_count; ++object) {
        if (owned_container[object] == 0U) {
            continue;
        }
        StaticKernelContainerSpec container;
        container.object = static_cast<ContainerObjectId>(object);
        if (packed_alias[object]) {
            container.storage = StaticKernelContainerStorage::packed_signal;
            container.packed_signal = *packed_alias[object];
        } else if (has_element_alias[object] != 0U) {
            container.storage = StaticKernelContainerStorage::element_signals;
            container.element_signals = element_signals[object];
        }
        if (!accessors[object].empty()
            || container.storage != StaticKernelContainerStorage::elements) {
            spec->containers.push_back(std::move(container));
        }
        if (owned_family[object] != 0U) {
            const auto& family = *family_of_object[object];
            StaticKernelAliasFamily runtime_family;
            runtime_family.proxy = family.proxy;
            runtime_family.width = family.width;
            for (const auto& leaf : family.leaves) {
                runtime_family.leaves.push_back(
                    { leaf.signal, leaf.offset, leaf.width });
            }
            virtual_owned[family.proxy] = 1U;
            spec->families.push_back(std::move(runtime_family));
        }
    }
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        if (owned[signal] == 0U) {
            continue;
        }
        spec->owned_signals.push_back(signal);
        if (vhdl_mode) {
            const auto width = static_cast<std::uint32_t>(
                signals_[signal].initial_value.width());
            std::map<ProcessId, std::vector<std::pair<std::uint32_t,
                std::uint32_t>>> per_process;
            for (const auto& writer : graph_signals[signal].writers) {
                per_process[writer.process].emplace_back(
                    writer.width == 0U ? 0U : writer.offset,
                    writer.width == 0U ? width : writer.offset + writer.width);
            }
            for (auto& [process, ranges] : per_process) {
                std::ranges::sort(ranges);
                std::pair<std::uint32_t, std::uint32_t> current = ranges.front();
                const auto emit = [&] {
                    spec->writer_regions.push_back({ signal, process,
                        current.first, current.second - current.first });
                };
                for (std::size_t index = 1U; index < ranges.size(); ++index) {
                    if (ranges[index].first <= current.second) {
                        current.second
                            = std::max(current.second, ranges[index].second);
                    } else {
                        emit();
                        current = ranges[index];
                    }
                }
                emit();
            }
        }
        if (std::ranges::any_of(graph_signals[signal].readers,
                [&](const auto& reader) {
                    return reader.process >= process_count
                        || member[reader.process] == 0U;
                })) {
            spec->boundary_outputs.push_back(signal);
        }
    }
    std::vector<std::uint8_t> input(signal_count, 0U);
    std::vector<Sensitivity> host_sensitivity;
    for (const auto& entry : spec->members) {
        for (const auto& sensitivity : entry.sensitivity) {
            if (owned[sensitivity.signal] == 0U
                && virtual_owned[sensitivity.signal] == 0U
                && input[sensitivity.signal] == 0U) {
                input[sensitivity.signal] = 1U;
                ++plan.boundary_inputs;
                host_sensitivity.push_back({ sensitivity.signal, EdgeKind::any });
            }
        }
    }
    plan.host = spec->members.front().process;
    auto stub = views[plan.host].materialize();
    stub.static_sensitivity = std::move(host_sensitivity);
    stub.debug_locals.clear();
    stub.debug_string_locals.clear();
    stub.debug_container_locals.clear();
    stub.expression_profiles = { };
    stub.register_value_kinds = { };
    stub.static_trigger_regions = { };
    stub.container_register_types = { };
    stub.register_count = 0U;
    stub.string_register_count = 0U;
    stub.container_register_count = 0U;
    stub.initialize = true;
    std::vector<Operation> body;
    body.push_back(WaitSensitivity { });
    body.push_back(Jump { 0U });
    body.push_back(Yield { });
    body.push_back(Jump { 0U });
    stub.operations = OperationList { std::move(body) };
    plan.host_program = std::move(stub);
    plan.members = spec->members.size();
    plan.owned_signals = spec->owned_signals.size();
    plan.boundary_outputs = spec->boundary_outputs.size();
    plan.owned_containers = spec->containers.size();
    plan.spec = std::move(spec);
    return plan;
}

} // namespace fsim::elaboration
