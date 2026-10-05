// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_graph.hpp"

#include <cstdint>

namespace fsim::runtime::simir::detail {

enum class RegionDriverClassificationMode : std::uint8_t {
    runtime_capabilities,
    persisted_structure,
};

/// Classify writer ranges with the runtime external-driver gate optionally
/// excluded from the immutable structural inventory.
[[nodiscard]] RegionDriverClass classify_region_driver_class(
    const RegionSignalNode& signal,
    RegionDriverClassificationMode mode);

} // namespace fsim::runtime::simir::detail
