// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_conditions.hpp"

#include <span>
#include <string_view>

namespace fsim::elaboration {

inline constexpr std::string_view kVerilogCoverageConditionDiagnostic
    = "FSIM-COV-015";

using VerilogCoverageConditionSource = CoverageConditionSource;
using VerilogCoverageConditionPoint = CoverageConditionPoint;
using VerilogCoverageConditionLimits = CoverageConditionLimits;
using VerilogCoverageConditionError = CoverageConditionError;
using VerilogCoverageConditionResult = CoverageConditionResult;

[[nodiscard]] VerilogCoverageConditionResult
discover_verilog_coverage_conditions(
    std::span<const frontend::Statement> statements,
    frontend::Language language,
    std::span<const VerilogCoverageConditionSource> sources,
    VerilogCoverageConditionLimits limits = { }) noexcept;

} // namespace fsim::elaboration
