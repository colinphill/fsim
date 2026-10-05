// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_frontier_v2.hpp"
#include "fsim/runtime/simir_region_graph.hpp"

#include <cstdint>

namespace fsim::compiler::test {

/// Exercise the production-generated entry against valid and adversarial
/// caller-owned frames. The caller must keep the code owner alive until return.
void run_region_frontier_adversarial_tests(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation);

/// Compare checked and private alias-prevalidated thunks after validating
/// identical geometry, and preserve all non-geometric guards on both entries.
void run_region_frontier_alias_prevalidated_entry_tests(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 checked_entry,
    runtime::simir::RegionFrontierStepEntryV2 trusted_entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation);

/// Exercise the private physical binding checks through an exact public-entry
/// thunk and verify Generic versus SystemVerilog stable-order provenance.
void run_region_frontier_shared_binding_guard_tests(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation);

/// Exercise construction-time symbol, signature, and no-unwind guards for an
/// exact-plan Status(Frame*) entry thunk.
void run_region_frontier_entry_thunk_guard_tests(
    const runtime::simir::RegionFrontierLayoutV2& layout);

/// Check that a Generic entry retains the scheduler-provided stable order
/// after the physical ProcessIds have been remapped.
void run_region_frontier_generic_stable_order_test(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation);

/// Keep the write-site initialization checks active at both O0 and O2.
void run_region_frontier_write_site_shape_guard_tests(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation);

/// Prove the emitted V2 entry reads only the eight-byte ABI prefix before
/// rejecting a V1-sized or truncated caller buffer.
void run_region_frontier_prefix_guard_tests(
    runtime::simir::RegionFrontierStepEntryV2 entry);

} // namespace fsim::compiler::test
