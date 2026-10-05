// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_graph.hpp"
#include "simir_process_program.hpp"

#include <cstdint>
#include <span>

namespace fsim::runtime::simir::region_graph_detail {

/// Source-private bridge for graph construction from immutable process rows.
class RegionGraphProgramBuilder {
public:
    [[nodiscard]] static RegionGraph build(
        std::span<const ProcessProgramView> processes,
        std::span<const RegionSignalDescriptor> signals,
        bool collect_opaque_operation_counts,
        std::span<const RegionContainerDescriptor> containers,
        std::span<const std::uint8_t> process_access_complete,
        std::span<const RegionSignalAliasFamilyDescriptor> signal_alias_families);
};

} // namespace fsim::runtime::simir::region_graph_detail
