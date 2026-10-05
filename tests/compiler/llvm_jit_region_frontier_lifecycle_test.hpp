// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_frontier_v2.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

#include <cstdint>

namespace fsim::compiler::test {

/// Exercise activation lifecycle transitions in the production-generated entry.
void run_region_frontier_lifecycle_tests(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation);

} // namespace fsim::compiler::test
