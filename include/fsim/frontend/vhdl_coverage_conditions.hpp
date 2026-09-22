// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/vhdl_coverage_conditions.hpp"
#include "fsim/frontend/coverage_conditions.hpp"

#include <span>

namespace fsim::frontend {

[[nodiscard]] elaboration::VhdlCoverageConditionResult
discover_vhdl_coverage_conditions(
    std::span<const Statement> statements,
    Language language,
    VhdlStandard standard,
    std::span<const elaboration::VhdlCoverageConditionSource> sources,
    elaboration::VhdlCoverageConditionLimits limits = { }) noexcept;

} // namespace fsim::frontend
