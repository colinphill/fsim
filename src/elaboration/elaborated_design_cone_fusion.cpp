// SPDX-License-Identifier: Apache-2.0
//
// A2 combinational cone fusion planning (docs/simulation-performance-
// architecture.md §5.2). A pure acyclic network of SystemVerilog continuous
// assignments is rewritten into one process: member bodies run in
// topological order, internal nets become registers, and only the cone's
// sink publishes its original boundary writes under its original ProcessId.
// IEEE 1800 permits this schedule: active events may be processed in any
// order (§4.7) and there is no normative delta per net hop. Hidden internal
// nets are materialized by the runtime before any observation.
#include "fsim/elaboration/elaborator.hpp"
#include "elaborated_design_process_access.hpp"
#include "../runtime/simir_region_graph_bindings.hpp"
#include "../runtime/simir_region_graph_program_access.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

enum class ConeMemberKind : std::uint8_t {
    none,
    combinational,
    constant,
};

struct ConeWrite {
    SignalId signal { };
    std::uint32_t offset { };
    std::uint32_t width { };
};

struct ConeCandidate {
    ConeMemberKind kind { ConeMemberKind::none };
    std::vector<SignalId> reads;
    std::vector<ConeWrite> writes;
};

[[nodiscard]] bool pure_value_operation(const Operation& operation)
{
    return visit_operation([](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        return std::is_same_v<T, DebugPoint>
            || std::is_same_v<T, LoadConstant>
            || std::is_same_v<T, CopyRegister>
            || std::is_same_v<T, Shift>
            || std::is_same_v<T, Extract>
            || std::is_same_v<T, Concatenate>
            || std::is_same_v<T, ConditionalSelect>
            || std::is_same_v<T, UnaryNot>
            || std::is_same_v<T, Binary>
            || std::is_same_v<T, Reduction>
            || std::is_same_v<T, LogicalNot>
            || std::is_same_v<T, LogicalBinary>
            || std::is_same_v<T, ConvertToTwoState>
            || std::is_same_v<T, Insert>;
    }, operation);
}

// Offsets every register operand of an operation accepted by the classifier.
void offset_registers(Operation& operation, const RegisterId base)
{
    visit_operation([base](auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LoadConstant>
            || std::is_same_v<T, ReadSignal>) {
            value.destination += base;
        } else if constexpr (std::is_same_v<T, CopyRegister>
            || std::is_same_v<T, Extract>
            || std::is_same_v<T, UnaryNot>
            || std::is_same_v<T, Reduction>
            || std::is_same_v<T, LogicalNot>
            || std::is_same_v<T, ConvertToTwoState>) {
            value.destination += base;
            value.source += base;
        } else if constexpr (std::is_same_v<T, Shift>) {
            value.destination += base;
            value.value += base;
            value.amount += base;
        } else if constexpr (std::is_same_v<T, Concatenate>) {
            value.destination += base;
            for (auto& operand : value.operands) {
                operand += base;
            }
        } else if constexpr (std::is_same_v<T, ConditionalSelect>) {
            value.destination += base;
            value.condition += base;
            value.when_true += base;
            value.when_false += base;
        } else if constexpr (std::is_same_v<T, Binary>
            || std::is_same_v<T, LogicalBinary>) {
            value.destination += base;
            value.lhs += base;
            value.rhs += base;
        } else if constexpr (std::is_same_v<T, Insert>) {
            value.destination += base;
            value.target += base;
            value.source += base;
        } else if constexpr (std::is_same_v<T, WriteUpdate>
            || std::is_same_v<T, WriteUpdateSlice>
            || std::is_same_v<T, WriteBlocking>
            || std::is_same_v<T, WriteBlockingSlice>) {
            value.source += base;
        } else if constexpr (std::is_same_v<T, Branch>) {
            value.condition += base;
        }
    }, operation);
}

} // namespace

ConeFusionPlan ElaboratedDesign::plan_cone_fusion() const
{
    ConeFusionPlan plan;
    const auto disable = [&](const char* reason) {
        plan.disabled = true;
        plan.disabled_reason = reason;
        plan.cones.clear();
        plan.dormant.clear();
        return plan;
    };
    // Designs whose nets may be observed outside SimIR processes keep the
    // ordinary per-assign execution.
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
                // Dump selections name scopes, not signals; any of them may
                // include a hidden net.
                return disable("vcd_control");
            }
        }
    }
    std::vector<RegionSignalDescriptor> descriptors;
    descriptors.reserve(signal_count);
    for (const auto& signal : signals_) {
        if (signal.initial_value.width()
            > std::numeric_limits<std::uint32_t>::max()) {
            return disable("signal_width");
        }
        descriptors.push_back({
            static_cast<std::uint32_t>(signal.initial_value.width()),
            signal.resolution, signal.value_kind,
            signal.implicit_driver.has_value(), false,
            signal.event_variable, RegionObservation::none });
    }
    std::vector<std::uint8_t> access_complete(process_count, 1U);
    auto graph_bindings
        = runtime::simir::detail::build_region_graph_container_bindings(
        signals_, container_objects_, container_signal_aliases_,
        container_element_signal_aliases_,
        container_aggregate_signal_aliases_);
    const auto graph = region_graph_detail::RegionGraphProgramBuilder::build(
        views, descriptors, false, graph_bindings.containers,
        access_complete, graph_bindings.alias_families);
    const auto graph_signals = graph.signals();
    const auto graph_processes = graph.processes();
    if (std::ranges::any_of(graph_processes,
            &RegionProcessNode::dependencies_unknown)) {
        // Fail closed: an opaque access anywhere can read a hidden net.
        return disable("unknown_dependencies");
    }
    if (std::getenv("FSIM_PROFILE_CLOCK_NETS") != nullptr) {
        // Diagnostic: nets with many edge-sensitive readers and their writers.
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            const auto& node = graph_signals[signal];
            std::size_t edge_readers = 0U;
            for (const auto& reader : node.readers) {
                edge_readers += reader.edge != EdgeKind::any ? 1U : 0U;
            }
            if (edge_readers < 8U) {
                continue;
            }
            std::cerr << "fsim-profile: clock-net signal=" << signal
                      << " name=" << signals_[signal].name
                      << " edge_readers=" << edge_readers
                      << " readers=" << node.readers.size()
                      << " writers=" << node.writers.size();
            if (!node.writers.empty()) {
                std::cerr << " writer=" << views[node.writers.front().process].name();
            }
            std::cerr << '\n';
        }
    }
    std::vector<std::uint8_t> alias_proxy(signal_count, 0U);
    for (const auto& family : graph.signal_alias_families()) {
        if (family.proxy < signal_count) {
            alias_proxy[family.proxy] = 1U;
        }
    }

    // 1. Classify candidate member processes.
    std::vector<ConeCandidate> candidates(process_count);
    for (std::size_t index = 0U; index < process_count; ++index) {
        const auto& view = views[index];
        if (!view.valid()
            || view.scheduling_domain() != ProcessSchedulingDomain::systemverilog
            || view.observed() || view.reactive() || view.postponed()
            || view.final() || !view.initialize()
            || view.switch_source() || view.switch_target()
            || view.switch_control() || view.switch_bidirectional()
            || view.switch_resistive()
            || view.drive_strength() != DriveStrength { }
            || view.string_register_count() != 0U
            || view.container_register_count() != 0U
            || !view.debug_locals().empty()
            || !view.debug_string_locals().empty()
            || !view.debug_container_locals().empty()
            || !view.static_trigger_regions().empty()
            || !graph_processes[index].operation_write_ranges_exact
            || std::ranges::any_of(view.register_value_kinds(),
                [](const ValueKind kind) {
                    return kind != ValueKind::logic4;
                })) {
            continue;
        }
        const auto& operations = view.operations();
        const auto count = operations.size();
        if (count < 2U) {
            continue;
        }
        const bool combinational = !view.static_sensitivity().empty();
        std::size_t body_end = count;
        if (combinational) {
            if (count < 3U
                || !operation_holds<WaitSensitivity>(
                    operations.expanded(count - 2U))) {
                continue;
            }
            const auto tail = operations.expanded(count - 1U);
            const auto* jump = operation_get_if<Jump>(&tail);
            if (jump == nullptr || jump->target != 0U
                || std::ranges::any_of(view.static_sensitivity(),
                    [](const Sensitivity& sensitivity) {
                        return sensitivity.edge != EdgeKind::any;
                    })) {
                continue;
            }
            body_end = count - 2U;
        } else {
            const auto tail = operations.expanded(count - 1U);
            const auto* halt = operation_get_if<Halt>(&tail);
            if (halt == nullptr || halt->program_exit) {
                continue;
            }
            body_end = count - 1U;
        }
        ConeCandidate candidate;
        bool accepted = true;
        const auto record_write = [&](const SignalId signal,
                                      const std::uint32_t offset,
                                      const SignalUpdateDomain domain,
                                      const bool slice) {
            if (domain != SignalUpdateDomain::systemverilog_active
                || signal >= signal_count) {
                accepted = false;
                return;
            }
            const auto width = descriptors[signal].width;
            const auto& regions = view.driver_regions();
            const auto region = std::ranges::find_if(regions,
                [&](const Process::DriverRegion& candidate_region) {
                    if (candidate_region.signal != signal) {
                        return false;
                    }
                    if (!slice) {
                        return candidate_region.whole
                            || (candidate_region.offset == 0U
                                && candidate_region.width == width);
                    }
                    return !candidate_region.whole
                        && candidate_region.offset == offset;
                });
            if (region == regions.end()) {
                accepted = false;
                return;
            }
            candidate.writes.push_back({ signal,
                region->whole ? 0U : region->offset,
                region->whole ? width : region->width });
        };
        for (std::size_t op = 0U; op < body_end && accepted; ++op) {
            const auto operation = operations.expanded(op);
            if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
                if (!combinational || read->kind != SignalReadKind::current
                    || read->ticks != 1U || read->clock || read->gate
                    || read->signal >= signal_count) {
                    accepted = false;
                } else {
                    candidate.reads.push_back(read->signal);
                }
            } else if (const auto* update
                = operation_get_if<WriteUpdate>(&operation)) {
                record_write(update->signal, 0U, update->domain, false);
            } else if (const auto* update_slice
                = operation_get_if<WriteUpdateSlice>(&operation)) {
                record_write(update_slice->signal, update_slice->offset,
                    update_slice->domain, true);
            } else if (const auto* blocking
                = operation_get_if<WriteBlocking>(&operation)) {
                record_write(blocking->signal, 0U,
                    SignalUpdateDomain::systemverilog_active, false);
            } else if (const auto* blocking_slice
                = operation_get_if<WriteBlockingSlice>(&operation)) {
                record_write(blocking_slice->signal, blocking_slice->offset,
                    SignalUpdateDomain::systemverilog_active, true);
            } else if (const auto* jump = operation_get_if<Jump>(&operation)) {
                // Forward jumps stay inside the member body; body_end is the
                // member's tail and maps to the next member's first op.
                accepted = combinational && jump->target > op
                    && jump->target <= body_end;
            } else if (const auto* branch
                = operation_get_if<Branch>(&operation)) {
                accepted = combinational && branch->when_true > op
                    && branch->when_false > op
                    && branch->when_true <= body_end
                    && branch->when_false <= body_end;
            } else if (!pure_value_operation(operation)) {
                accepted = false;
            }
        }
        if (!accepted || candidate.writes.empty()) {
            continue;
        }
        if (!combinational
            && (candidate.writes.size() != 1U || !candidate.reads.empty())) {
            continue;
        }
        // Each write site must be distinct; overlapping writes from one
        // member would make the last write the sole register contribution.
        auto sorted_writes = candidate.writes;
        std::ranges::sort(sorted_writes, [](const auto& left, const auto& right) {
            return std::tie(left.signal, left.offset)
                < std::tie(right.signal, right.offset);
        });
        if (std::ranges::adjacent_find(sorted_writes,
                [](const auto& left, const auto& right) {
                    return left.signal == right.signal
                        && left.offset + left.width > right.offset;
                }) != sorted_writes.end()) {
            continue;
        }
        candidate.kind = combinational
            ? ConeMemberKind::combinational : ConeMemberKind::constant;
        ++plan.candidate_processes;
        candidates[index] = std::move(candidate);
    }

    // 2. Statically eligible internal nets: every access is a candidate
    // member, writers own pairwise-disjoint slices covering the net, and the
    // net carries no observation or modifier that needs published state.
    std::vector<std::uint8_t> internal(signal_count, 0U);
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        const auto& node = graph_signals[signal];
        const auto& info = signals_[signal];
        if (node.writers.empty() || node.readers.empty()
            || node.observations != RegionObservation::none
            || node.writers_unknown || node.dynamic_fork_writers
            || node.partial_projected_transactions
            || alias_proxy[signal] != 0U
            || (info.resolution != ResolutionKind::sv_wire
                && info.resolution != ResolutionKind::none)
            || info.value_kind != ValueKind::logic4
            || info.systemverilog_scalar != runtime::SystemVerilogScalarKind::None
            || info.event_variable || info.implicit_driver
            || info.charge_strength || descriptors[signal].width == 0U) {
            continue;
        }
        const auto width = descriptors[signal].width;
        bool eligible = true;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> intervals;
        for (const auto& writer : node.writers) {
            if (writer.process >= process_count
                || candidates[writer.process].kind == ConeMemberKind::none) {
                eligible = false;
                break;
            }
            const auto& writes = candidates[writer.process].writes;
            const auto offset = writer.width == 0U ? 0U : writer.offset;
            const auto extent = writer.width == 0U ? width : writer.width;
            if (std::ranges::none_of(writes, [&](const ConeWrite& write) {
                    return write.signal == signal && write.offset == offset
                        && write.width == extent;
                })) {
                eligible = false;
                break;
            }
            intervals.emplace_back(offset, extent);
        }
        if (!eligible) {
            continue;
        }
        // Multiple disjoint writers are an ordinary resolved wire; any other
        // multi-writer resolution keeps its published route.
        if (intervals.size() != 1U && info.resolution != ResolutionKind::sv_wire) {
            continue;
        }
        std::ranges::sort(intervals);
        std::uint32_t covered = 0U;
        for (const auto& [offset, extent] : intervals) {
            if (offset != covered) {
                eligible = false;
                break;
            }
            covered = offset + extent;
        }
        if (!eligible || covered != width) {
            continue;
        }
        for (const auto& reader : node.readers) {
            if (reader.process >= process_count
                || candidates[reader.process].kind
                    != ConeMemberKind::combinational
                || reader.edge != EdgeKind::any) {
                eligible = false;
                break;
            }
        }
        if (eligible) {
            internal[signal] = 1U;
        }
    }

    // 3. Fixpoint cone formation. A candidate whose writes are all internal
    // is absorbed into the unique cone that reads its outputs; a candidate
    // with a boundary write is a sink. Fan-out across cones, cycles, and
    // writers split across cones demote the net to a published boundary.
    std::vector<std::vector<ProcessId>> readers_by_signal(signal_count);
    std::vector<std::vector<ProcessId>> writers_by_signal(signal_count);
    for (SignalId signal = 0U; signal < signal_count; ++signal) {
        for (const auto& reader : graph_signals[signal].readers) {
            readers_by_signal[signal].push_back(reader.process);
        }
        for (const auto& writer : graph_signals[signal].writers) {
            writers_by_signal[signal].push_back(writer.process);
        }
        auto& readers = readers_by_signal[signal];
        std::ranges::sort(readers);
        readers.erase(std::ranges::unique(readers).begin(), readers.end());
        auto& writers = writers_by_signal[signal];
        std::ranges::sort(writers);
        writers.erase(std::ranges::unique(writers).begin(), writers.end());
    }
    for (auto& candidate : candidates) {
        std::ranges::sort(candidate.reads);
        candidate.reads.erase(std::ranges::unique(candidate.reads).begin(),
            candidate.reads.end());
    }
    constexpr auto no_cone = std::numeric_limits<ProcessId>::max();
    std::vector<ProcessId> cone(process_count, no_cone);
    std::vector<ProcessId> order;
    for (std::size_t iteration = 0U;; ++iteration) {
        if (iteration > 64U) {
            return disable("formation_limit");
        }
        std::vector<std::size_t> pending(process_count, 0U);
        std::vector<ProcessId> participants;
        for (std::size_t index = 0U; index < process_count; ++index) {
            if (candidates[index].kind == ConeMemberKind::none) {
                continue;
            }
            participants.push_back(static_cast<ProcessId>(index));
            for (const auto signal : candidates[index].reads) {
                if (internal[signal] != 0U) {
                    pending[index] += writers_by_signal[signal].size();
                }
            }
        }
        order.clear();
        std::vector<ProcessId> ready;
        for (const auto process : participants) {
            if (pending[process] == 0U) {
                ready.push_back(process);
            }
        }
        while (!ready.empty()) {
            const auto process = ready.back();
            ready.pop_back();
            order.push_back(process);
            for (const auto& write : candidates[process].writes) {
                if (internal[write.signal] == 0U) {
                    continue;
                }
                for (const auto reader : readers_by_signal[write.signal]) {
                    if (pending[reader] > 0U && --pending[reader] == 0U) {
                        ready.push_back(reader);
                    }
                }
            }
        }
        bool changed = false;
        if (order.size() != participants.size()) {
            for (const auto process : participants) {
                if (pending[process] == 0U) {
                    continue;
                }
                for (const auto signal : candidates[process].reads) {
                    if (internal[signal] != 0U) {
                        internal[signal] = 0U;
                        changed = true;
                    }
                }
            }
            if (changed) {
                continue;
            }
        }
        std::ranges::fill(cone, no_cone);
        for (auto position = order.rbegin(); position != order.rend();
             ++position) {
            const auto process = *position;
            const auto& candidate = candidates[process];
            const bool sink = std::ranges::any_of(candidate.writes,
                [&](const ConeWrite& write) {
                    return internal[write.signal] == 0U;
                });
            if (sink) {
                if (candidate.kind == ConeMemberKind::combinational) {
                    cone[process] = process;
                }
                continue;
            }
            ProcessId target = no_cone;
            bool conflict = false;
            for (const auto& write : candidate.writes) {
                for (const auto reader : readers_by_signal[write.signal]) {
                    const auto reader_cone = cone[reader];
                    if (reader_cone == no_cone
                        || (target != no_cone && reader_cone != target)) {
                        conflict = true;
                        break;
                    }
                    target = reader_cone;
                }
                if (conflict) {
                    break;
                }
            }
            if (conflict || target == no_cone) {
                for (const auto& write : candidate.writes) {
                    if (internal[write.signal] != 0U) {
                        internal[write.signal] = 0U;
                        changed = true;
                    }
                }
                continue;
            }
            cone[process] = target;
        }
        for (SignalId signal = 0U; signal < signal_count; ++signal) {
            if (internal[signal] == 0U) {
                continue;
            }
            const auto expected = readers_by_signal[signal].empty()
                ? no_cone : cone[readers_by_signal[signal].front()];
            const bool consistent = expected != no_cone
                && std::ranges::all_of(readers_by_signal[signal],
                    [&](const ProcessId process) {
                        return cone[process] == expected;
                    })
                && std::ranges::all_of(writers_by_signal[signal],
                    [&](const ProcessId process) {
                        return cone[process] == expected;
                    });
            if (!consistent) {
                internal[signal] = 0U;
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }

    // 4. Build one fused body per sink with at least one absorbed member.
    std::map<ProcessId, std::vector<ProcessId>> members_by_cone;
    for (const auto process : order) {
        if (cone[process] != no_cone) {
            members_by_cone[cone[process]].push_back(process);
        }
    }
    for (auto& [sink, members] : members_by_cone) {
        if (members.size() < 2U) {
            continue;
        }
        // `order` is topological, so members are already in dependency order.
        auto fused = views[sink].materialize();
        fused.static_sensitivity.clear();
        fused.debug_locals.clear();
        fused.expression_profiles = { };
        fused.register_value_kinds = { };
        fused.static_trigger_regions = { };
        std::vector<Operation> body;
        std::unordered_map<SignalId, RegisterId> net_register;
        RegisterId next_register = 0U;
        std::vector<RegisterId> base_by_member(members.size());
        bool overflow = false;
        for (std::size_t member = 0U; member < members.size(); ++member) {
            base_by_member[member] = next_register;
            const auto count = views[members[member]].register_count();
            if (count > std::numeric_limits<RegisterId>::max() - next_register) {
                overflow = true;
                break;
            }
            next_register += static_cast<RegisterId>(count);
        }
        if (overflow) {
            continue;
        }
        ConeFusionPlanCone planned;
        planned.sink = sink;
        for (const auto process : members) {
            for (const auto& write : candidates[process].writes) {
                if (internal[write.signal] == 0U
                    || net_register.contains(write.signal)) {
                    continue;
                }
                net_register.emplace(write.signal, next_register++);
                planned.internal_signals.push_back(write.signal);
                const bool whole_writer
                    = writers_by_signal[write.signal].size() == 1U
                    && write.offset == 0U
                    && write.width == descriptors[write.signal].width;
                if (!whole_writer) {
                    body.push_back(LoadConstant { net_register[write.signal],
                        runtime::PackedLogic4 { descriptors[write.signal].width,
                            runtime::Logic4::z } });
                }
            }
        }
        std::vector<Sensitivity> sensitivity;
        for (std::size_t member = 0U; member < members.size(); ++member) {
            const auto process = members[member];
            const auto& view = views[process];
            const auto base = base_by_member[member];
            for (const auto& entry : view.static_sensitivity()) {
                if (internal[entry.signal] == 0U
                    && std::ranges::find(sensitivity, entry)
                        == sensitivity.end()) {
                    sensitivity.push_back(entry);
                }
            }
            const auto& operations = view.operations();
            const auto body_end = candidates[process].kind
                    == ConeMemberKind::combinational
                ? operations.size() - 2U : operations.size() - 1U;
            // Every non-DebugPoint body op emits exactly one fused op, so a
            // prefix count maps original control-flow targets.
            std::vector<InstructionIndex> remap(body_end + 1U);
            {
                auto position = static_cast<InstructionIndex>(body.size());
                for (std::size_t op = 0U; op < body_end; ++op) {
                    remap[op] = position;
                    if (!operation_holds<DebugPoint>(operations[op])) {
                        ++position;
                    }
                }
                remap[body_end] = position;
            }
            for (std::size_t op = 0U; op < body_end; ++op) {
                auto operation = operations.expanded(op);
                if (operation_holds<DebugPoint>(operation)) {
                    continue;
                }
                if (auto* jump = operation_get_if<Jump>(&operation)) {
                    jump->target = remap[jump->target];
                    body.push_back(std::move(operation));
                    continue;
                }
                if (auto* branch = operation_get_if<Branch>(&operation)) {
                    branch->condition += base;
                    branch->when_true = remap[branch->when_true];
                    branch->when_false = remap[branch->when_false];
                    body.push_back(std::move(operation));
                    continue;
                }
                if (const auto* read = operation_get_if<ReadSignal>(&operation);
                    read != nullptr && internal[read->signal] != 0U) {
                    body.push_back(CopyRegister { read->destination + base,
                        net_register.at(read->signal) });
                    continue;
                }
                const auto whole_internal_write = [&](const SignalId signal,
                                                      const RegisterId source) {
                    body.push_back(CopyRegister { net_register.at(signal),
                        source + base });
                };
                const auto slice_internal_write = [&](const SignalId signal,
                                                      const RegisterId source,
                                                      const std::uint32_t offset) {
                    const auto net = net_register.at(signal);
                    const bool whole = writers_by_signal[signal].size() == 1U
                        && offset == 0U
                        && std::ranges::any_of(candidates[process].writes,
                            [&](const ConeWrite& item) {
                                return item.signal == signal
                                    && item.width == descriptors[signal].width;
                            });
                    if (whole) {
                        body.push_back(CopyRegister { net, source + base });
                    } else {
                        body.push_back(Insert { net, net, source + base, offset });
                    }
                };
                if (const auto* write = operation_get_if<WriteUpdate>(&operation);
                    write != nullptr && internal[write->signal] != 0U) {
                    whole_internal_write(write->signal, write->source);
                    continue;
                }
                if (const auto* write
                    = operation_get_if<WriteBlocking>(&operation);
                    write != nullptr && internal[write->signal] != 0U) {
                    whole_internal_write(write->signal, write->source);
                    continue;
                }
                if (const auto* write
                    = operation_get_if<WriteUpdateSlice>(&operation);
                    write != nullptr && internal[write->signal] != 0U) {
                    slice_internal_write(write->signal, write->source,
                        write->offset);
                    continue;
                }
                if (const auto* write
                    = operation_get_if<WriteBlockingSlice>(&operation);
                    write != nullptr && internal[write->signal] != 0U) {
                    slice_internal_write(write->signal, write->source,
                        write->offset);
                    continue;
                }
                offset_registers(operation, base);
                body.push_back(std::move(operation));
            }
        }
        if (sensitivity.empty()) {
            // A cone of constants has no boundary input; leave it unfused.
            continue;
        }
        body.push_back(WaitSensitivity { });
        body.push_back(Jump { 0U });
        for (const auto& entry : sensitivity) {
            if (std::ranges::find(planned.boundary_inputs, entry.signal)
                == planned.boundary_inputs.end()) {
                planned.boundary_inputs.push_back(entry.signal);
            }
        }
        fused.static_sensitivity = std::move(sensitivity);
        fused.register_count = next_register;
        fused.operations = OperationList { std::move(body) };
        planned.fused = std::move(fused);
        planned.members = members;
        plan.internal_nets += planned.internal_signals.size();
        for (const auto process : members) {
            if (process != sink) {
                plan.dormant.push_back(process);
            }
        }
        plan.cones.push_back(std::move(planned));
    }
    std::ranges::sort(plan.dormant);
    return plan;
}

} // namespace fsim::elaboration
