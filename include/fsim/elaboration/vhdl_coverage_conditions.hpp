// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_conditions.hpp"

#include <string_view>

namespace fsim::elaboration {

inline constexpr std::string_view kVhdlCoverageConditionDiagnostic
    = "FSIM-COV-016";

using VhdlCoverageConditionSource = CoverageConditionSource;
using VhdlCoverageConditionPoint = CoverageConditionPoint;
using VhdlCoverageConditionLimits = CoverageConditionLimits;
using VhdlCoverageConditionError = CoverageConditionError;
using VhdlCoverageConditionResult = CoverageConditionResult;

} // namespace fsim::elaboration
