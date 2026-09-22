// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/vhdl_coverage_points.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

// Sequential process/subprogram bodies and concurrent statement collections
// share this compilation-only discovery path.
[[nodiscard]] bool is_executable_vhdl_statement_kind(
    StatementKind kind) noexcept;

[[nodiscard]] elaboration::VhdlCoveragePointResult
discover_vhdl_statement_points(
    std::span<const Statement> statements,
    Language language,
    VhdlStandard standard,
    std::span<const elaboration::VhdlCoverageSource> sources,
    elaboration::VhdlCoveragePointLimits limits = { }) noexcept;

} // namespace fsim::frontend
