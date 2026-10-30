// SPDX-License-Identifier: Apache-2.0
//
// Clock domains of the engine v4 static kernel (Phase 7 P1). A domain is a
// clock signal and an edge: its flops are the sequential members that edge
// triggers, and its cone the combinational members downstream of them. A
// clock is primary (written by a testbench thread or the host), derived
// (written by a flop: a divider) or combinational (a gated clock). The
// analysis reports what a cycle-based schedule could cover
// (FSIM_PROFILE_KERNEL_DOMAINS); the cycle code builds on it.
#include "simir_static_kernel_compiled_internal.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <string>
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

void Interpreter::Impl::StaticKernel::report_domains() const
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
            const auto written = visit_operation([](const auto& op)
                -> std::optional<SignalId> {
                using T = std::decay_t<decltype(op)>;
                if constexpr (std::is_same_v<T, WriteBlocking>
                    || std::is_same_v<T, WriteBlockingSlice>
                    || std::is_same_v<T, WriteBlockingDynamicSlice>
                    || std::is_same_v<T, WriteBlockingDynamicPartSlice>
                    || std::is_same_v<T, WriteUpdate>
                    || std::is_same_v<T, WriteUpdateSlice>
                    || std::is_same_v<T, WriteUpdateDynamicSlice>
                    || std::is_same_v<T, WriteUpdateDynamicPartSlice>) {
                    return op.signal;
                } else {
                    return std::nullopt;
                }
            }, operation);
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
                    triggers[member].push_back({ node, reader.edge });
                    ++edge_readers[node];
                }
            } else if (reader.member < member_count) {
                triggers[reader.member].push_back({ node, reader.edge });
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
    const auto node_name = [&](const std::uint32_t node) -> std::string {
        const auto signal = node < slot_count ? slots_[node].signal
                                              : inputs_[node - slot_count].signal;
        return impl_.get_signal_cold(signal).name;
    };
    const auto kind_of = [&](const std::uint32_t node) {
        std::optional<ClockKind> kind;
        for (const auto writer : writers[node]) {
            const auto member_kind = members_[writer].kind;
            const auto next = member_kind == StaticKernelMemberKind::sequential
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
        if (members_[index].kind != StaticKernelMemberKind::sequential
            || triggers[index].empty()) {
            continue;
        }
        const auto best = std::ranges::max_element(triggers[index], { },
            [&](const Trigger& trigger) { return edge_readers[trigger.node]; });
        clock_of[index] = best->node;
        edge_of[index] = best->edge;
        async_flops += triggers[index].size() > 1U ? 1U : 0U;
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
            if (kind == StaticKernelMemberKind::sequential
                && member_domain[writer] != no_slot) {
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
    // Members by class.
    std::map<std::string, std::pair<std::uint32_t, std::uint64_t>> classes;
    const auto count_class = [&](const std::string& name, const std::uint32_t index) {
        auto& entry = classes[name];
        ++entry.first;
        entry.second += code_size(index);
    };
    for (std::uint32_t index = 0U; index < member_count; ++index) {
        const auto& member = members_[index];
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
                    + std::to_string(static_cast<int>(member.kind)),
                index);
            break;
        }
    }
    std::cerr << "fsim-kernel: domains count=" << domains.size()
              << " members=" << member_count << " async_flops=" << async_flops
              << '\n';
    for (const auto& [name, entry] : classes) {
        std::cerr << "fsim-kernel: domain-class " << name << " members=" << entry.first
                  << " code=" << entry.second << '\n';
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

} // namespace fsim::runtime::simir
