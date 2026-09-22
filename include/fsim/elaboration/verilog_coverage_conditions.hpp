// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_conditions.hpp"

#include <string_view>

namespace fsim::elaboration {

inline constexpr std::string_view kVerilogCoverageConditionDiagnostic
    = "FSIM-COV-015";

using VerilogCoverageConditionSource = CoverageConditionSource;
using VerilogCoverageConditionPoint = CoverageConditionPoint;
using VerilogCoverageConditionLimits = CoverageConditionLimits;
using VerilogCoverageConditionError = CoverageConditionError;
using VerilogCoverageConditionResult = CoverageConditionResult;

} // namespace fsim::elaboration
