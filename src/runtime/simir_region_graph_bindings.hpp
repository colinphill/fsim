// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_graph.hpp"
#include "simir_signal_storage.hpp"

namespace fsim::runtime::simir::detail {

struct RegionGraphContainerBindings {
    std::vector<RegionContainerDescriptor> containers;
    std::vector<RegionSignalAliasFamilyDescriptor> alias_families;
    std::vector<std::uint8_t> complete_alias_member;
};

/// Build neutral, immutable container and complete alias-family descriptors
/// from the registered SimIR object/alias rows. Runtime observations and
/// public signal mutation state do not enter this proof.
[[nodiscard]] RegionGraphContainerBindings
build_region_graph_container_bindings(
    std::span<const Signal> signals,
    std::span<const ContainerObject> objects,
    std::span<const ContainerSignalAlias> direct_aliases,
    std::span<const ContainerElementSignalAlias> element_aliases,
    std::span<const ContainerAggregateSignalAlias> aggregate_aliases);

[[nodiscard]] RegionGraphContainerBindings
build_region_graph_container_bindings(
    std::span<const SignalHot> signals,
    std::span<const ContainerObject> objects,
    std::span<const ContainerSignalAlias> direct_aliases,
    std::span<const ContainerElementSignalAlias> element_aliases,
    std::span<const ContainerAggregateSignalAlias> aggregate_aliases);

} // namespace fsim::runtime::simir::detail
