// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_builder_internal.hpp"

namespace fsim::elaboration {

void HierarchyBuilder::add_systemverilog_alias_connections(
    const SystemVerilogAliasPlan& plan,
    const std::string_view path,
    const SignalMap& signals,
    SpecializationInfo& specialization)
{
    std::size_t ordinal { };
    for (const auto& connection : plan.connections) {
        const auto left = signals.find(connection.left);
        const auto right = signals.find(connection.right);
        if (left == signals.end() || right == signals.end()) {
            report(
                "FSIM-ELAB-SVALIAS-002",
                "cannot bind a SystemVerilog alias connection to its "
                "packed nets",
                connection.source);
            continue;
        }
        if (left->second == right->second
            && connection.left_offset == connection.right_offset) {
            continue;
        }
        const auto process_index = design_.processes_.size();
        const auto process_id = static_cast<ProcessId>(process_index);
        if (static_cast<std::size_t>(process_id) != process_index) {
            throw std::length_error { "too many SimIR processes" };
        }
        Process process;
        process.id = process_id;
        process.name = std::string { path } + ".$alias_"
            + std::to_string(ordinal++);
        process.switch_source = left->second;
        process.switch_target = right->second;
        process.switch_source_offset = connection.left_offset;
        process.switch_target_offset = connection.right_offset;
        process.switch_width = connection.width;
        process.switch_bidirectional = true;
        process.initialize = false;
        process.operations.emplace_back(Halt { });
        specialization.processes.push_back(process.id);
        design_.processes_.push_back(std::move(process));
    }
}

} // namespace fsim::elaboration
