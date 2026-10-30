// SPDX-License-Identifier: Apache-2.0
//
// Clock domains of the engine v4 static kernel (Phase 7 P1). A domain is a
// clock signal and an edge: its flops are the sequential members that edge
// triggers, and its cone the combinational members downstream of them. A
// clock is primary (written by a testbench thread or the host), derived
// (written by a flop: a divider) or combinational (a gated clock). The
// analysis reports what a cycle-based schedule could cover
// (FSIM_PROFILE_KERNEL_DOMAINS); the cycle code builds on it.
//
// A VHDL flop with asynchronous controls (`process (clk, rst)` with
// `if rst = '1' ... elsif rising_edge(clk)`) runs as a thread: its edge
// entry is its clock. A derived clock's domain runs after its parent, the
// domain of the flop that writes it. A gated clock is a combinational
// function of a source clock and enables. The report screens out gates that
// can pulse at the edge (the source reaching the gate at several delta
// depths, an enable that is a flop on the same edge); what remains is only
// a candidate. Triggering the gated domain from the source edge is exact
// only if no enable changes at any delta of the edge's time step, including
// after the edge, which this report does not prove (Phase 7 plan, gated
// clocks).
#include "simir_static_kernel_compiled_internal.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fsim::runtime::simir {

using namespace static_kernel_detail;

namespace {

enum class ClockKind : std::uint8_t {
    host,
    testbench,
    derived,
    combinational,
    mixed,
};

// The signal a write operation targets.
std::optional<SignalId> written_signal(const Operation& operation)
{
    return visit_operation([](const auto& op) -> std::optional<SignalId> {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, WriteBlocking>
            || std::is_same_v<T, WriteBlockingSlice>
            || std::is_same_v<T, WriteBlockingDynamicSlice>
            || std::is_same_v<T, WriteBlockingDynamicPartSlice>
            || std::is_same_v<T, WriteUpdate>
            || std::is_same_v<T, WriteUpdateSlice>
            || std::is_same_v<T, WriteUpdateDynamicSlice>
            || std::is_same_v<T, WriteUpdateDynamicPartSlice>
            || std::is_same_v<T, WriteAfter>
            || std::is_same_v<T, WriteAfterSlice>
            || std::is_same_v<T, WriteAfterDynamicSlice>
            || std::is_same_v<T, WriteAfterDynamicPartSlice>
            || std::is_same_v<T, WriteInertial>
            || std::is_same_v<T, WriteInertialSlice>
            || std::is_same_v<T, WriteInertialDynamicSlice>
            || std::is_same_v<T, WriteInertialDynamicPartSlice>
            || std::is_same_v<T, WriteProjected>
            || std::is_same_v<T, WriteProjectedSlice>
            || std::is_same_v<T, WriteProjectedDynamicSlice>
            || std::is_same_v<T, WriteProjectedWaveform>
            || std::is_same_v<T, WriteProjectedWaveformSlice>
            || std::is_same_v<T, WriteProjectedWaveformDynamicSlice>
            || std::is_same_v<T, WriteDelayed>) {
            return op.signal;
        } else {
            return std::nullopt;
        }
    }, operation);
}

// A domain's edge: a VHDL RISING_EDGE (FALLING_EDGE) sensitivity shares the
// clock's posedge (negedge) domain; the two differ only on metavalue
// transitions, which the report does not model.
constexpr EdgeKind domain_edge(const EdgeKind edge)
{
    return edge == EdgeKind::rising ? EdgeKind::posedge
        : edge == EdgeKind::falling ? EdgeKind::negedge
        : edge;
}

// The register an operation the shape matchers below follow defines.
std::optional<RegisterId> defined_register(const Operation& operation)
{
    return visit_operation([](const auto& op) -> std::optional<RegisterId> {
        using T = std::decay_t<decltype(op)>;
        if constexpr (std::is_same_v<T, ReadSignal> || std::is_same_v<T, LoadConstant>
            || std::is_same_v<T, Binary> || std::is_same_v<T, LogicalBinary>
            || std::is_same_v<T, Extract> || std::is_same_v<T, ConvertToTwoState>
            || std::is_same_v<T, CopyRegister>) {
            return op.destination;
        } else {
            return std::nullopt;
        }
    }, operation);
}

// The level a '0', '1', 'L' or 'H' constant denotes: true for high.
std::optional<bool> constant_level(const PackedLogic4& value)
{
    if (value == PackedLogic4::from_logic9_msb_string("1")
        || value == PackedLogic4::from_logic9_msb_string("H")
        || value == PackedLogic4(1U, Logic4::one)) {
        return true;
    }
    if (value == PackedLogic4::from_logic9_msb_string("0")
        || value == PackedLogic4::from_logic9_msb_string("L")
        || value == PackedLogic4(1U, Logic4::zero)) {
        return false;
    }
    return std::nullopt;
}

// The operations one activation runs. A VHDL member with a prologue keeps
// its first run after the wait (body_begin past body_end); its activation
// is then the operations before the wait.
template <typename Member>
std::pair<std::size_t, std::size_t> activation_range(const Member& member)
{
    const auto end = std::min<std::size_t>(member.body_end, member.operations().size());
    return { member.body_begin <= member.body_end ? member.body_begin : 0U, end };
}

// The clock of a VHDL process sensitive to levels only (`process (clk,
// rst)`): the one sensitivity signal its body tests with 'EVENT, which
// RISING_EDGE and FALLING_EDGE lower to (IEEE 1076-2008 16.7). The edge is
// the level the body compares the clock's current value with ('1' or 'H'
// for a rising edge); every such comparison must agree. This is a shape
// match on the lowered body, for the report only: cycle code needs the
// branch structure proven (Phase 7 plan, P1 report shapes).
template <typename Member>
std::optional<std::vector<Sensitivity>> body_edge(
    const Member& member, const std::vector<Sensitivity>& sensitivity)
{
    const auto& operations = member.operations();
    const auto [begin, end] = activation_range(member);
    std::optional<SignalId> clock;
    std::unordered_map<RegisterId, std::size_t> definition;
    for (auto index = begin; index < end; ++index) {
        if (const auto* event = operation_get_if<SignalEvent>(&operations[index])) {
            if ((clock && *clock != event->signal)
                || std::ranges::none_of(sensitivity, [&](const Sensitivity& entry) {
                       return entry.signal == event->signal;
                   })) {
                return std::nullopt;
            }
            clock = event->signal;
        }
        if (const auto destination = defined_register(operations[index])) {
            definition[*destination] = index;
        }
    }
    if (!clock) {
        return std::nullopt;
    }
    // A register through bit selects and copies: the clock's current value,
    // or a constant's level.
    const auto source = [&](RegisterId reg) -> const Operation* {
        for (std::size_t steps = 0U; steps < 8U; ++steps) {
            const auto found = definition.find(reg);
            if (found == definition.end()) {
                return nullptr;
            }
            const auto& operation = operations[found->second];
            if (const auto* extract = operation_get_if<Extract>(&operation)) {
                reg = extract->source;
            } else if (const auto* copy = operation_get_if<CopyRegister>(&operation)) {
                reg = copy->source;
            } else {
                return &operation;
            }
        }
        return nullptr;
    };
    const auto clock_value = [&](const Operation* operation) {
        const auto* read = operation != nullptr ? operation_get_if<ReadSignal>(operation) : nullptr;
        return read != nullptr && read->signal == *clock && read->kind == SignalReadKind::current;
    };
    std::optional<bool> high;
    for (auto index = begin; index < end; ++index) {
        const auto* binary = operation_get_if<Binary>(&operations[index]);
        if (binary == nullptr
            || (binary->operation != BinaryOperator::case_equal
                && binary->operation != BinaryOperator::equal)) {
            continue;
        }
        const auto* lhs = source(binary->lhs);
        const auto* rhs = source(binary->rhs);
        const auto* level_operand = clock_value(lhs) ? rhs : clock_value(rhs) ? lhs : nullptr;
        const auto* constant = level_operand != nullptr
            ? operation_get_if<LoadConstant>(level_operand)
            : nullptr;
        if (constant == nullptr) {
            continue;
        }
        const auto level = constant_level(constant->value);
        if (!level || (high && *high != *level)) {
            return std::nullopt;
        }
        high = *level;
    }
    if (!high) {
        return std::nullopt;
    }
    auto edges = sensitivity;
    for (auto& entry : edges) {
        if (entry.signal == *clock) {
            entry.edge = *high ? EdgeKind::posedge : EdgeKind::negedge;
        }
    }
    return edges;
}

// A level-sensitive latch on a clock (`if clk = '0' then q <= d; end
// if;`): one branch on a comparison of one signal with a '0' or '1'
// constant, and every signal write in its true arm. Returns the signal and
// whether the latch is transparent while it is high. Report only.
template <typename Member>
std::optional<std::pair<SignalId, bool>> latch_level(const Member& member)
{
    const auto& operations = member.operations();
    const auto [begin, end] = activation_range(member);
    std::optional<std::size_t> branch_at;
    std::unordered_map<RegisterId, std::size_t> definition;
    for (auto index = begin; index < end; ++index) {
        const auto& operation = operations[index];
        if (operation_get_if<Branch>(&operation) != nullptr) {
            if (branch_at) {
                return std::nullopt;
            }
            branch_at = index;
            continue;
        }
        if (branch_at) {
            continue;
        }
        if (const auto destination = defined_register(operation)) {
            definition[*destination] = index;
        }
    }
    if (!branch_at) {
        return std::nullopt;
    }
    const auto& branch = *operation_get_if<Branch>(&operations[*branch_at]);
    if (branch.when_true <= *branch_at || branch.when_false <= branch.when_true) {
        return std::nullopt;
    }
    // Writes only in the true arm.
    for (auto index = begin; index < end; ++index) {
        if (written_signal(operations[index])
            && (index < branch.when_true || index >= branch.when_false)) {
            return std::nullopt;
        }
    }
    // The condition: one signal against '0' or '1' constants.
    std::optional<SignalId> signal;
    std::optional<bool> high;
    std::vector<RegisterId> pending { branch.condition };
    for (std::size_t steps = 0U; !pending.empty() && steps < 16U; ++steps) {
        const auto reg = pending.back();
        pending.pop_back();
        const auto found = definition.find(reg);
        if (found == definition.end()) {
            return std::nullopt;
        }
        const auto& operation = operations[found->second];
        if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
            if ((signal && *signal != read->signal) || read->kind != SignalReadKind::current) {
                return std::nullopt;
            }
            signal = read->signal;
        } else if (const auto* constant = operation_get_if<LoadConstant>(&operation)) {
            const auto level = constant_level(constant->value);
            if (!level || (high && *high != *level)) {
                return std::nullopt;
            }
            high = *level;
        } else if (const auto* binary = operation_get_if<Binary>(&operation)) {
            if (binary->operation != BinaryOperator::case_equal
                && binary->operation != BinaryOperator::equal) {
                return std::nullopt;
            }
            pending.push_back(binary->lhs);
            pending.push_back(binary->rhs);
        } else if (const auto* logical = operation_get_if<LogicalBinary>(&operation)) {
            if (logical->operation != LogicalBinaryOperator::logical_or) {
                return std::nullopt;
            }
            pending.push_back(logical->lhs);
            pending.push_back(logical->rhs);
        } else if (const auto* extract = operation_get_if<Extract>(&operation)) {
            pending.push_back(extract->source);
        } else if (const auto* convert = operation_get_if<ConvertToTwoState>(&operation)) {
            pending.push_back(convert->source);
        } else if (const auto* copy = operation_get_if<CopyRegister>(&operation)) {
            pending.push_back(copy->source);
        }
    }
    if (!pending.empty() || !signal || !high) {
        return std::nullopt;
    }
    return std::pair { *signal, *high };
}

constexpr const char* clock_kind_name(const ClockKind kind)
{
    switch (kind) {
    case ClockKind::host:
        return "host";
    case ClockKind::testbench:
        return "testbench";
    case ClockKind::derived:
        return "derived";
    case ClockKind::combinational:
        return "combinational";
    case ClockKind::mixed:
        return "mixed";
    }
    return "?";
}

} // namespace

void Interpreter::Impl::StaticKernel::report_domains()
{
    const auto member_count = static_cast<std::uint32_t>(members_.size());
    const auto slot_count = static_cast<std::uint32_t>(slots_.size());
    // Nodes: slots, then host inputs.
    const auto node_count = slot_count + static_cast<std::uint32_t>(inputs_.size());
    std::vector<std::vector<std::uint32_t>> reads(member_count);
    std::vector<std::vector<std::uint32_t>> writes(member_count);
    std::vector<std::vector<std::uint32_t>> writers(node_count);
    const auto nodes_of = [&](const SignalId signal, auto&& visit) {
        if (signal >= slot_of_signal_.size()) {
            return;
        }
        if (slot_of_signal_[signal] != no_slot) {
            visit(slot_of_signal_[signal]);
        } else if (family_of_signal_[signal] != no_slot) {
            for (const auto& leaf : families_[family_of_signal_[signal]].leaves) {
                visit(leaf.slot);
            }
        } else if (signal < input_of_signal_.size()
            && input_of_signal_[signal] != no_slot) {
            visit(slot_count + input_of_signal_[signal]);
        }
    };
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        for (const auto& operation : members_[index].operations()) {
            if (const auto* read = operation_get_if<ReadSignal>(&operation)) {
                nodes_of(read->signal,
                    [&](const std::uint32_t node) { reads[index].push_back(node); });
                continue;
            }
            const auto written = written_signal(operation);
            if (written) {
                nodes_of(*written,
                    [&](const std::uint32_t node) { writes[index].push_back(node); });
            }
        }
    }
    // The scheduling graph adds what the operations do not name directly
    // (template replay, families): a slot's writer processes, and its readers
    // (whole-slot notify targets, a partition standing for all its members,
    // and ranged readers).
    std::unordered_map<ProcessId, std::uint32_t> member_of_process;
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        member_of_process.emplace(members_[index].process, index);
    }
    for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
        for (const auto& region : slots_[slot].writers) {
            if (const auto found = member_of_process.find(region.process);
                found != member_of_process.end()) {
                writes[found->second].push_back(slot);
            }
        }
        for (const auto& reader : slots_[slot].readers) {
            if (reader.member < member_count) {
                reads[reader.member].push_back(slot);
            }
        }
        if (slot < slot_notify_.size()) {
            const auto notify = slot_notify_[slot];
            for (auto at = notify.begin; at < notify.end; ++at) {
                const auto id = notify_targets_[at].id;
                if ((id & partition_tag) == 0U) {
                    if (id < member_count) {
                        reads[id].push_back(slot);
                    }
                    continue;
                }
                for (const auto member : partitions_[id & ~partition_tag].members) {
                    reads[member].push_back(slot);
                }
            }
        }
    }
    for (std::uint32_t input = 0U; input < inputs_.size(); ++input) {
        for (const auto& reader : inputs_[input].readers) {
            if (reader.member < member_count) {
                reads[reader.member].push_back(slot_count + input);
            }
        }
    }
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        for (auto* list : { &reads[index], &writes[index] }) {
            std::ranges::sort(*list);
            list->erase(std::ranges::unique(*list).begin(), list->end());
        }
        for (const auto node : writes[index]) {
            writers[node].push_back(index);
        }
    }
    // Edge triggers of each member, and how many members each clock node
    // triggers.
    struct Trigger {
        std::uint32_t node { };
        EdgeKind edge { };
    };
    std::vector<std::vector<Trigger>> triggers(member_count);
    std::vector<std::uint32_t> edge_readers(node_count, 0U);
    const auto add_edges = [&](const std::uint32_t node, const std::vector<EdgeReader>& edges) {
        for (const auto& reader : edges) {
            // A sequential dispatch group stands for all its members.
            if ((reader.member & dispatch_group_tag) != 0U) {
                for (const auto member :
                    dispatch_groups_[reader.member & ~dispatch_group_tag].members) {
                    triggers[member].push_back({ node, domain_edge(reader.edge) });
                    ++edge_readers[node];
                }
            } else if (reader.member < member_count) {
                triggers[reader.member].push_back({ node, domain_edge(reader.edge) });
                ++edge_readers[node];
            }
        }
    };
    for (std::uint32_t slot = 0U; slot < slot_count; ++slot) {
        add_edges(slot, slots_[slot].edges);
    }
    for (std::uint32_t input = 0U; input < inputs_.size(); ++input) {
        add_edges(slot_count + input, inputs_[input].edges);
    }
    // Flops: sequential members, and VHDL threads whose sensitivity is edge
    // entries plus level entries (asynchronous controls).
    std::vector<std::uint8_t> flop(member_count, 0U);
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        const auto& member = members_[index];
        if (member.kind == StaticKernelMemberKind::sequential) {
            flop[index] = triggers[index].empty() ? 0U : 1U;
            continue;
        }
        const auto& sensitivity = member.wait_sensitivity;
        const auto is_edge = [](const Sensitivity& entry) {
            return domain_edge(entry.edge) == EdgeKind::posedge
                || domain_edge(entry.edge) == EdgeKind::negedge;
        };
        if (member.kind != StaticKernelMemberKind::behavioral || !member.vhdl
            || sensitivity.empty()
            || !std::ranges::all_of(sensitivity, [&](const Sensitivity& entry) {
                   return is_edge(entry) || entry.edge == EdgeKind::any;
               })) {
            continue;
        }
        auto edges = std::ranges::any_of(sensitivity, is_edge)
            ? std::optional { sensitivity }
            : body_edge(member, sensitivity);
        if (!edges) {
            continue;
        }
        for (const auto& entry : *edges) {
            if (is_edge(entry)) {
                nodes_of(entry.signal, [&](const std::uint32_t node) {
                    triggers[index].push_back({ node, domain_edge(entry.edge) });
                    ++edge_readers[node];
                });
            }
        }
        flop[index] = triggers[index].empty() ? 0U : 2U;
    }
    const auto node_signal = [&](const std::uint32_t node) {
        return node < slot_count ? slots_[node].signal : inputs_[node - slot_count].signal;
    };
    const auto node_name = [&](const std::uint32_t node) -> std::string {
        return impl_.get_signal_cold(node_signal(node)).name;
    };
    const auto kind_of = [&](const std::uint32_t node) {
        std::optional<ClockKind> kind;
        for (const auto writer : writers[node]) {
            const auto member_kind = members_[writer].kind;
            const auto next = flop[writer] != 0U
                ? ClockKind::derived
                : member_kind == StaticKernelMemberKind::combinational
                ? ClockKind::combinational
                : ClockKind::testbench;
            kind = kind && *kind != next ? ClockKind::mixed : next;
        }
        return kind.value_or(ClockKind::host);
    };
    // Each flop's clock: its one trigger, or among several (asynchronous
    // set and reset) the node that triggers the most members.
    std::vector<std::uint32_t> clock_of(member_count, no_slot);
    std::vector<EdgeKind> edge_of(member_count, EdgeKind::posedge);
    std::uint32_t async_flops = 0U;
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        if (flop[index] == 0U) {
            continue;
        }
        const auto best = std::ranges::max_element(triggers[index], { },
            [&](const Trigger& trigger) { return edge_readers[trigger.node]; });
        clock_of[index] = best->node;
        edge_of[index] = best->edge;
        async_flops += triggers[index].size() > 1U || flop[index] == 2U ? 1U : 0U;
    }
    // Domains: (clock node, edge).
    std::map<std::pair<std::uint32_t, int>, std::uint32_t> domain_of_key;
    struct Domain {
        std::uint32_t clock { };
        EdgeKind edge { };
        std::uint32_t flops { };
        std::uint64_t flop_code { };
        std::uint32_t cone { };
        std::uint64_t cone_code { };
    };
    std::vector<Domain> domains;
    std::vector<std::uint32_t> member_domain(member_count, no_slot);
    const auto code_size = [&](const std::uint32_t index) -> std::uint64_t {
        return members_[index].compiled ? members_[index].compiled->code.size()
                                                   : members_[index].operations().size();
    };
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        if (clock_of[index] == no_slot) {
            continue;
        }
        const auto key = std::pair { clock_of[index], static_cast<int>(edge_of[index]) };
        const auto [found, inserted] = domain_of_key.emplace(key,
            static_cast<std::uint32_t>(domains.size()));
        if (inserted) {
            domains.push_back({ clock_of[index], edge_of[index] });
        }
        member_domain[index] = found->second;
        ++domains[found->second].flops;
        domains[found->second].flop_code += code_size(index);
    }
    // Which domains feed each combinational member (through flop outputs
    // and other combinational members), and whether a testbench thread or
    // the host writes something it reads. Iterated to a fixed point over
    // the (acyclic, settled) combinational graph.
    std::vector<std::vector<std::uint32_t>> fed(member_count);
    std::vector<std::uint8_t> testbench_input(member_count, 0U);
    std::vector<std::vector<std::uint32_t>> node_domains(node_count);
    std::vector<std::uint8_t> node_testbench(node_count, 0U);
    for (std::uint32_t node = 0U; node < node_count; ++node) {
        for (const auto writer : writers[node]) {
            const auto kind = members_[writer].kind;
            if (flop[writer] != 0U && member_domain[writer] != no_slot) {
                node_domains[node].push_back(member_domain[writer]);
            } else if (kind != StaticKernelMemberKind::combinational
                && kind != StaticKernelMemberKind::sequential) {
                node_testbench[node] = 1U;
            }
        }
        if (node >= slot_count && writers[node].empty()) {
            // A host input no kernel member writes: the host drives it.
            node_testbench[node] = inputs_[node - slot_count].unwritten ? 0U : 1U;
        }
    }
    for (bool changed = true; changed;) {
        changed = false;
        for (std::uint32_t index = 0U; index < member_count; ++index) {
            if (members_[index].kind != StaticKernelMemberKind::combinational) {
                continue;
            }
            auto next = fed[index];
            auto testbench = testbench_input[index];
            for (const auto node : reads[index]) {
                next.insert(next.end(), node_domains[node].begin(),
                    node_domains[node].end());
                testbench |= node_testbench[node];
            }
            std::ranges::sort(next);
            next.erase(std::ranges::unique(next).begin(), next.end());
            if (next != fed[index] || testbench != testbench_input[index]) {
                fed[index] = std::move(next);
                testbench_input[index] = testbench;
                changed = true;
                for (const auto node : writes[index]) {
                    auto& domains_here = node_domains[node];
                    domains_here.insert(domains_here.end(), fed[index].begin(),
                        fed[index].end());
                    std::ranges::sort(domains_here);
                    domains_here.erase(std::ranges::unique(domains_here).begin(),
                        domains_here.end());
                    node_testbench[node] |= testbench;
                }
            }
        }
    }
    const auto edge_name = [](const EdgeKind edge) {
        return edge == EdgeKind::posedge ? "pos" : edge == EdgeKind::negedge ? "neg" : "other";
    };
    // Each domain's admission status for the cycle schedule: "admitted", a
    // "pending_" proof the cycle code still needs, or a "refused_" reason.
    std::vector<std::string> domain_status(domains.size(), "admitted");
    for (std::uint32_t domain_index = 0U; domain_index < domains.size(); ++domain_index) {
        const auto& domain = domains[domain_index];
        const auto clock_kind = kind_of(domain.clock);
        if (clock_kind == ClockKind::mixed) {
            domain_status[domain_index] = "refused_mixed_clock";
            continue;
        }
        if (clock_kind == ClockKind::derived) {
            // Parents: the domains of the flops that write the clock.
            std::vector<std::uint32_t> parents;
            for (const auto writer : writers[domain.clock]) {
                if (member_domain[writer] != no_slot) {
                    parents.push_back(member_domain[writer]);
                }
            }
            std::ranges::sort(parents);
            parents.erase(std::ranges::unique(parents).begin(), parents.end());
            std::cerr << "fsim-kernel: domain-derived clock=" << node_name(domain.clock)
                      << " edge=" << edge_name(domain.edge) << " parent=";
            for (std::size_t at = 0U; at < parents.size(); ++at) {
                const auto& parent = domains[parents[at]];
                std::cerr << (at == 0U ? "" : ",") << node_name(parent.clock) << '/'
                          << edge_name(parent.edge);
            }
            std::cerr << '\n';
            continue;
        }
        if (clock_kind != ClockKind::combinational) {
            continue;
        }
        // The gate cone: back from the clock through combinational writers
        // to its leaves, with the delta depths at which each leaf reaches
        // the clock.
        std::map<std::uint32_t, std::vector<std::uint32_t>> leaf_depths;
        // Enables written by a latch on a clock: the latch's clock and
        // whether it is transparent while that clock is high.
        std::map<std::uint32_t, std::pair<SignalId, bool>> latched;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> stack { { domain.clock, 0U } };
        std::map<std::pair<std::uint32_t, std::uint32_t>, bool> visited;
        bool bounded = true;
        while (!stack.empty()) {
            const auto [node, depth] = stack.back();
            stack.pop_back();
            if (visited.contains({ node, depth })) {
                continue;
            }
            visited[{ node, depth }] = true;
            const bool combinational = !writers[node].empty()
                && std::ranges::all_of(writers[node], [&](const std::uint32_t writer) {
                       return members_[writer].kind == StaticKernelMemberKind::combinational;
                   });
            if (combinational && node != domain.clock && writers[node].size() == 1U
                && members_[writers[node].front()].vhdl) {
                if (const auto latch = latch_level(members_[writers[node].front()])) {
                    latched[node] = *latch;
                    leaf_depths[node].push_back(depth);
                    continue;
                }
            }
            if (!combinational) {
                leaf_depths[node].push_back(depth);
                continue;
            }
            if (depth >= 64U || visited.size() > 4096U) {
                bounded = false;
                break;
            }
            for (const auto writer : writers[node]) {
                for (const auto read : reads[writer]) {
                    stack.push_back({ read, depth + 1U });
                }
            }
        }
        // The source clock: the leaf edge readers watch (a clock of its own
        // domains or testbench edges); the other leaves are enables.
        std::vector<std::uint32_t> sources;
        for (const auto& [leaf, depths] : leaf_depths) {
            if (edge_readers[leaf] != 0U) {
                sources.push_back(leaf);
            }
        }
        // A buffered clock: the cone's one leaf is the source (or, without
        // edge readers of its own, is written only by testbench threads or
        // the host), with no enables and one delta depth, so each clock edge
        // follows one leaf change (a converted or renamed clock).
        const bool buffered = bounded && leaf_depths.size() == 1U
            && leaf_depths.begin()->second.size() == 1U
            && (sources.size() == 1U
                || (sources.empty()
                    && std::ranges::all_of(writers[leaf_depths.begin()->first],
                        [&](const std::uint32_t writer) {
                            return flop[writer] == 0U
                                && members_[writer].kind
                                != StaticKernelMemberKind::combinational;
                        })));
        if (buffered && sources.empty()) {
            sources.push_back(leaf_depths.begin()->first);
        }
        const char* gate = nullptr;
        std::size_t source_depths = 0U;
        if (!bounded) {
            gate = "refuse_unbounded";
        } else if (sources.size() != 1U) {
            gate = sources.empty() ? "refuse_no_source" : "refuse_multiple_sources";
        } else {
            auto depths = leaf_depths[sources.front()];
            std::ranges::sort(depths);
            depths.erase(std::ranges::unique(depths).begin(), depths.end());
            source_depths = depths.size();
            if (depths.size() != 1U) {
                gate = "refuse_reconvergent_source";
            }
        }
        std::size_t enables = 0U;
        for (const auto& [leaf, depths] : leaf_depths) {
            if (sources.size() == 1U && leaf == sources.front()) {
                continue;
            }
            ++enables;
            // A latch transparent while the source is at its pre-edge
            // level holds the enable steady across the edge (a clock-gating
            // cell); one transparent at the post-edge level can pass a
            // change straight through.
            if (const auto found = latched.find(leaf); found != latched.end()
                && sources.size() == 1U
                && found->second.first == node_signal(sources.front())) {
                const bool post_edge_high = domain.edge == EdgeKind::posedge;
                if (gate == nullptr && found->second.second == post_edge_high) {
                    gate = "refuse_same_phase_latch";
                }
                continue;
            }
            for (const auto writer : writers[leaf]) {
                // A flop on the source's edge updates the enable in the
                // edge's own step: the gate can pulse.
                if (gate == nullptr && flop[writer] != 0U && sources.size() == 1U
                    && clock_of[writer] == sources.front()
                    && edge_of[writer] == domain.edge) {
                    gate = "refuse_same_edge_enable";
                }
            }
        }
        std::cerr << "fsim-kernel: domain-gate clock=" << node_name(domain.clock)
                  << " edge=" << edge_name(domain.edge) << " source="
                  << (sources.size() == 1U ? node_name(sources.front()) : std::string { "-" })
                  << " source_depths=" << source_depths << " enables=" << enables
                  << " gate=" << (gate != nullptr ? gate : buffered ? "buffer" : "candidate")
                  << '\n';
        // A candidate gate still needs the no-glitch certificate (Phase 7
        // plan, P1 gated clocks).
        domain_status[domain_index] = gate != nullptr
            ? std::string { "refused_gate_" } + (gate + std::string_view { "refuse_" }.size())
            : buffered ? "admitted" : "pending_gate_certificate";
    }
    // Members by class.
    struct Class {
        std::uint32_t members { };
        std::uint64_t code { };
        std::uint32_t example { };
    };
    std::map<std::string, Class> classes;
    const auto count_class = [&](const std::string& name, const std::uint32_t index) {
        auto& entry = classes[name];
        if (entry.members++ == 0U) {
            entry.example = index;
        }
        entry.code += code_size(index);
    };
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        const auto& member = members_[index];
        if (flop[index] == 2U) {
            count_class(member_domain[index] != no_slot ? "flop_async_thread"
                                                        : "sequential_untriggered",
                index);
            continue;
        }
        switch (member.kind) {
        case StaticKernelMemberKind::sequential:
            count_class(member_domain[index] != no_slot ? "flop" : "sequential_untriggered",
                index);
            break;
        case StaticKernelMemberKind::combinational: {
            const auto& from = fed[index];
            const bool testbench = testbench_input[index] != 0U;
            if (from.size() == 1U) {
                ++domains[from.front()].cone;
                domains[from.front()].cone_code += code_size(index);
                count_class(testbench ? "cone_one_domain_testbench_input" : "cone_one_domain",
                    index);
            } else if (from.empty()) {
                count_class(testbench ? "comb_testbench_only" : "comb_constant", index);
            } else {
                count_class(testbench ? "cone_multi_domain_testbench_input"
                                      : "cone_multi_domain",
                    index);
            }
            break;
        }
        default:
            count_class(std::string { "other_kind_" }
                    + std::to_string(static_cast<int>(member.kind))
                    + (member.vhdl ? "_vhdl" : "_sv"),
                index);
            break;
        }
    }
    // Members by admission status. A flop takes its domain's status; a VHDL
    // thread recognized as a flop by the report's shape match still needs
    // the branch proof (Phase 7 plan, P3 prerequisite). A cone takes the
    // first status among its domains that is not "admitted"; constant
    // combinational members settle at time zero.
    profile_domain_status_names_.clear();
    profile_domain_status_.assign(member_count, 0U);
    std::map<std::string, std::uint32_t> status_index;
    const auto set_status = [&](const std::uint32_t index, const std::string& name) {
        const auto [found, inserted] = status_index.emplace(name,
            static_cast<std::uint32_t>(profile_domain_status_names_.size()));
        if (inserted) {
            profile_domain_status_names_.push_back(name);
        }
        profile_domain_status_[index] = found->second;
    };
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        const auto& member = members_[index];
        if (flop[index] != 0U) {
            const auto& status = member_domain[index] == no_slot
                ? std::string { "refused_untriggered_sequential" }
                : domain_status[member_domain[index]];
            set_status(index,
                flop[index] == 2U && status == "admitted" ? "pending_vhdl_flop_proof" : status);
            continue;
        }
        switch (member.kind) {
        case StaticKernelMemberKind::sequential:
            set_status(index, "refused_untriggered_sequential");
            break;
        case StaticKernelMemberKind::combinational: {
            if (fed[index].empty()) {
                set_status(index,
                    testbench_input[index] != 0U ? "refused_testbench_only" : "admitted");
                break;
            }
            const auto blocked = std::ranges::find_if(fed[index],
                [&](const std::uint32_t domain) { return domain_status[domain] != "admitted"; });
            set_status(index, blocked == fed[index].end() ? "admitted" : domain_status[*blocked]);
            break;
        }
        case StaticKernelMemberKind::once:
            set_status(index, "refused_once");
            break;
        default:
            set_status(index, member.vhdl ? "refused_thread_vhdl" : "refused_thread_sv");
            break;
        }
    }
    std::map<std::string, Class> admission;
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        auto& entry = admission[profile_domain_status_names_[profile_domain_status_[index]]];
        ++entry.members;
        entry.code += code_size(index);
    }
    for (const auto& [name, entry] : admission) {
        std::cerr << "fsim-kernel: domain-admission " << name << " members=" << entry.members
                  << " code=" << entry.code << '\n';
    }
    std::cerr << "fsim-kernel: domains count=" << domains.size()
              << " members=" << member_count << " async_flops=" << async_flops
              << '\n';
    // Member names are kept under FSIM_PROFILE_KERNEL.
    for (const auto& [name, entry] : classes) {
        std::cerr << "fsim-kernel: domain-class " << name << " members=" << entry.members
                  << " code=" << entry.code;
        if (entry.example < profile_names_.size()) {
            std::cerr << " example=" << profile_names_[entry.example];
        }
        std::cerr << '\n';
    }
    std::vector<std::uint32_t> order(domains.size());
    std::iota(order.begin(), order.end(), 0U);
    std::ranges::sort(order, std::greater<> { }, [&](const std::uint32_t domain) {
        return domains[domain].flop_code + domains[domain].cone_code;
    });
    for (std::size_t rank = 0U; rank < order.size() && rank < 24U; ++rank) {
        const auto& domain = domains[order[rank]];
        std::cerr << "fsim-kernel: domain clock=" << node_name(domain.clock)
                  << " edge=" << (domain.edge == EdgeKind::posedge ? "pos"
                                     : domain.edge == EdgeKind::negedge ? "neg"
                                     : "other")
                  << " kind=" << clock_kind_name(kind_of(domain.clock))
                  << " flops=" << domain.flops << " flop_code=" << domain.flop_code
                  << " cone=" << domain.cone << " cone_code=" << domain.cone_code << '\n';
    }
}

void Interpreter::Impl::StaticKernel::report_domain_runs() const
{
    // Runs by admission status: member runs, and partition runs counted once
    // per member of the partition.
    std::vector<std::uint64_t> runs(profile_domain_status_names_.size(), 0U);
    for (std::uint32_t index = 0U; index < profile_member_runs_.size(); ++index) {
        runs[profile_domain_status_[index]] += profile_member_runs_[index];
    }
    for (std::uint32_t partition = 0U; partition < profile_partition_counts_.size();
         ++partition) {
        for (const auto member : partitions_[partition].members) {
            runs[profile_domain_status_[member]] += profile_partition_counts_[partition];
        }
    }
    std::cerr << "fsim-kernel: domain-runs";
    for (std::size_t status = 0U; status < runs.size(); ++status) {
        std::cerr << ' ' << profile_domain_status_names_[status] << '=' << runs[status];
    }
    std::cerr << '\n';
}

} // namespace fsim::runtime::simir
