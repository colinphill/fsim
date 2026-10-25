// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_driver_inventory.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

namespace fsim::runtime::simir {

/// Build the structural owner and bit-mask inventory once for a graph. This
/// is the elaboration-time proof that artifacts serialize and strict loading
/// compares with a freshly built copy. Partial projected ranges are retained
/// as candidates that still require a certificate wherever the inventory is
/// bound.
[[nodiscard]] SignalDriverInventory build_signal_driver_inventory(
    const RegionGraph& graph);

} // namespace fsim::runtime::simir
