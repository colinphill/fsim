// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_branches.hpp"
#include "fsim/frontend/design.hpp"

#include <span>

namespace fsim::frontend {

// Compile-local syntax discovery. Compiled and specialized designs use the
// HIR coverage records retained by the compilation pipeline instead.
[[nodiscard]] elaboration::CoverageBranchResult
discover_coverage_branch_points(
    std::span<const Statement> statements,
    CodeCoverageLanguage language,
    std::span<const elaboration::CoverageBranchSource> sources,
    elaboration::CoverageBranchLimits limits = { }) noexcept;

} // namespace fsim::frontend
