// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_conditions.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

[[nodiscard]] elaboration::CoverageConditionResult
discover_coverage_conditions(
    std::span<const Statement> statements,
    CodeCoverageLanguage language,
    std::span<const elaboration::CoverageConditionSource> sources,
    elaboration::CoverageConditionLimits limits = { }) noexcept;

} // namespace fsim::frontend
