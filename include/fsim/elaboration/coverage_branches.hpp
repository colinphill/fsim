// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_point_identity.hpp"
#include "fsim/frontend/design.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageBranchDiagnostic = "FSIM-COV-006";

enum class CoverageDecisionKind : std::uint8_t {
    If,
    Case,
    Loop,
};

struct CoverageBranchSource {
    std::string source_name;
    frontend::CodeCoverageSourceIdentity identity;
};

struct CoverageBranchPoint {
    runtime::CodeCoveragePointId id;
    frontend::CodeCoverageLanguage language {
        frontend::CodeCoverageLanguage::Verilog
    };
    CoverageDecisionKind decision { CoverageDecisionKind::If };
    frontend::CodeCoverageConstructKind arm {
        frontend::CodeCoverageConstructKind::BranchTrueArm
    };
    frontend::CodeCoverageSourceSpan span;
    std::size_t source_index { };
    std::size_t arm_index { };

    friend bool operator==(const CoverageBranchPoint&,
        const CoverageBranchPoint&)
        = default;
};

struct CoverageBranchLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_statements { 1U << 20U };
    std::size_t maximum_arms { 1U << 20U };
    std::size_t maximum_nesting { 1U << 12U };
};

enum class CoverageBranchError {
    None,
    InvalidLanguage,
    ResourceLimit,
    EmptySourceName,
    DuplicateSourceName,
    InvalidSourceIdentity,
    UnknownArmSource,
    InvalidArmSpan,
    MissingExplicitArm,
    DuplicatePoint,
};

struct CoverageBranchResult {
    std::vector<CoverageBranchPoint> points;
    CoverageBranchError error { CoverageBranchError::None };
    std::size_t statement_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageBranchError::None;
    }
};

[[nodiscard]] CoverageBranchResult discover_coverage_branch_points(
    std::span<const frontend::Statement> statements,
    frontend::CodeCoverageLanguage language,
    std::span<const CoverageBranchSource> sources,
    CoverageBranchLimits limits = { }) noexcept;

} // namespace fsim::elaboration
