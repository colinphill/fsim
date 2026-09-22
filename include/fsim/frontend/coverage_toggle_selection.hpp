// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_toggle_selection.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

// Compile-time selection over the parser-owned unit. The result describes
// only default exclusions and is safe to retain after the syntax workspace is
// destroyed.
[[nodiscard]] elaboration::CoverageToggleSelectionResult
make_default_coverage_toggle_selection(
    const DesignUnit& unit,
    std::span<const SignalDeclaration> ports,
    const elaboration::CoverageInventoryOwner& owner,
    std::span<const elaboration::VerilogCoverageSource> sources,
    elaboration::CoverageToggleSelectionLimits limits = { }) noexcept;

} // namespace fsim::frontend
