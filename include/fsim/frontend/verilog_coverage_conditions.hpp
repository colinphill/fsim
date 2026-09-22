// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/verilog_coverage_conditions.hpp"
#include "fsim/frontend/coverage_conditions.hpp"

#include <span>

namespace fsim::frontend {

[[nodiscard]] elaboration::VerilogCoverageConditionResult
discover_verilog_coverage_conditions(
    std::span<const Statement> statements,
    Language language,
    std::span<const elaboration::VerilogCoverageConditionSource> sources,
    elaboration::VerilogCoverageConditionLimits limits = { }) noexcept;

} // namespace fsim::frontend
