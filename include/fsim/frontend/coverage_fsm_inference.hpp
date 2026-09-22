// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_fsm_inference.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

// `ports` is the already resolved compile-local entity interface for VHDL and
// normally unit.ports for Verilog/SystemVerilog. Next-state objects require a
// direct compatible retained-object assignment, an exact SystemVerilog FSM
// description pragma, or a validated VHDL/manifest hint; legal sets reference
// only already inferred states. Elaboration consumes the retained results,
// never this syntax forest.
[[nodiscard]] elaboration::CoverageFsmInferenceResult
make_coverage_fsm_inference(
    const DesignUnit& unit,
    std::span<const SignalDeclaration> ports,
    const elaboration::CoverageInventoryOwner& owner,
    std::span<const elaboration::VerilogCoverageSource> sources,
    std::span<const elaboration::CoverageFsmHint> hints,
    elaboration::CoverageFsmInferenceLimits limits = { }) noexcept;

[[nodiscard]] inline elaboration::CoverageFsmInferenceResult
make_coverage_fsm_inference(
    const DesignUnit& unit,
    const std::span<const SignalDeclaration> ports,
    const elaboration::CoverageInventoryOwner& owner,
    const std::span<const elaboration::VerilogCoverageSource> sources,
    const elaboration::CoverageFsmInferenceLimits limits = { }) noexcept
{
    return make_coverage_fsm_inference(
        unit, ports, owner, sources, { }, limits);
}

} // namespace fsim::frontend
