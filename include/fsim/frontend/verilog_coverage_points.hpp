// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/verilog_coverage_points.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

// A source statement is executable unless it is only a lexical block
// container or an explicit null statement.
[[nodiscard]] bool is_executable_verilog_statement_kind(
    StatementKind kind) noexcept;

[[nodiscard]] elaboration::VerilogCoveragePointResult
discover_verilog_statement_points(
    std::span<const Statement> statements,
    Language language,
    std::span<const elaboration::VerilogCoverageSource> sources,
    elaboration::VerilogCoveragePointLimits limits = { }) noexcept;

} // namespace fsim::frontend
