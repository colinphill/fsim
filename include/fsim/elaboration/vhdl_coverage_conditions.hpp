// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_conditions.hpp"

#include <span>
#include <string_view>

namespace fsim::elaboration {

inline constexpr std::string_view kVhdlCoverageConditionDiagnostic
    = "FSIM-COV-016";

using VhdlCoverageConditionSource = CoverageConditionSource;
using VhdlCoverageConditionPoint = CoverageConditionPoint;
using VhdlCoverageConditionLimits = CoverageConditionLimits;
using VhdlCoverageConditionError = CoverageConditionError;
using VhdlCoverageConditionResult = CoverageConditionResult;

[[nodiscard]] VhdlCoverageConditionResult discover_vhdl_coverage_conditions(
    std::span<const frontend::Statement> statements,
    frontend::Language language,
    frontend::VhdlStandard standard,
    std::span<const VhdlCoverageConditionSource> sources,
    VhdlCoverageConditionLimits limits = { }) noexcept;

} // namespace fsim::elaboration
