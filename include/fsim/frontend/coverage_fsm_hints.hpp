// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_fsm_hints.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

// Recognized VHDL source attributes are string-typed declarations named
// fsm_current_state, fsm_next_state, and fsm_legal_states. This syntax-facing
// discovery is confined to compilation; elaboration consumes retained HIR.
[[nodiscard]] elaboration::CoverageFsmHintResult make_coverage_fsm_hints(
    const DesignUnit& unit,
    std::span<const project::CoverageFsmHintEntry> manifest_hints = { },
    elaboration::CoverageFsmHintLimits limits = { }) noexcept;

} // namespace fsim::frontend
