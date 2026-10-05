// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_graph.hpp"
#include "fsim/runtime/simir_region_frontier_v2.hpp"

#include <cstdint>
#include <vector>

namespace fsim::runtime::simir {

struct RegionFrontierCacheBindingResult final {
    RegionFrontierStatusV2 status { };
    std::vector<std::uint64_t> pending_aval;
    std::vector<std::uint64_t> pending_bval;
    std::uint64_t member_dispatches { };
};

} // namespace fsim::runtime::simir

namespace fsim::compiler::test {

using runtime::simir::RegionFrontierCacheBindingResult;

struct RegionFrontierTestValue final {
    std::vector<std::uint64_t> aval;
    std::vector<std::uint64_t> bval;
    std::vector<std::uint64_t> plane2 { };
    std::vector<std::uint64_t> plane3 { };
    runtime::simir::ValueKind value_kind { runtime::simir::ValueKind::logic4 };
};

struct RegionFrontierSignalInput final {
    runtime::simir::SignalId signal { };
    RegionFrontierTestValue value;
};

struct RegionFrontierStagingValues final {
    RegionFrontierTestValue external_input;
    std::vector<RegionFrontierSignalInput> external_inputs;
    RegionFrontierTestValue initial_internal;
    RegionFrontierTestValue expected_internal;
    RegionFrontierTestValue initial_boundary;
    RegionFrontierTestValue expected_boundary;
};

/// Exercise the real generated frontier entry with caller-owned fixture code
/// and its immutable descriptor layout. The caller must keep the code owner
/// alive until this function returns.
void run_region_frontier_staging_tests(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation,
    RegionFrontierStagingValues values = { });

/// Verify canonical-tail declines for every bound signal role and pending
/// value plane at a partial packed width. The helper first proves that an
/// unmodified rooted frame is accepted, then checks each malformed buffer on
/// an otherwise fresh frame.
void run_region_frontier_canonical_tail_guard_tests(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation,
    RegionFrontierStagingValues values = { });

/// Commit the first producer write and check that only a changed private
/// sensitivity range stages the selected reader activation.
void run_region_frontier_sensitivity_fanout_witness(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation,
    RegionFrontierStagingValues values,
    std::size_t expected_activation_events);

[[nodiscard]] RegionFrontierCacheBindingResult
run_cache_input_binding_witness(
    const runtime::simir::RegionConeActivationKernel& kernel,
    runtime::simir::RegionFrontierStepEntryV2 entry,
    const runtime::simir::RegionFrontierLayoutV2& layout,
    std::uint64_t runtime_generation,
    RegionFrontierTestValue external_input = { { 1U }, { 0U } },
    RegionFrontierTestValue initial_internal = { { 0U }, { 0U } });

} // namespace fsim::compiler::test
