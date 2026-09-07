// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_source_control.hpp"
#include "fsim/project/project.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageExternalExclusionDiagnostic
    = "FSIM-COV-042";
inline constexpr std::string_view kCoverageExternalExclusionSchema
    = "fsim-coverage-external-exclusion-v1";

struct CoverageExternalExclusionRule {
    std::optional<std::string> source_pattern;
    std::optional<std::string> hierarchy_pattern;
    std::optional<std::string> object_pattern;
    frontend::CoverageSourceMetric metric {
        frontend::CoverageSourceMetric::All
    };
    std::string reason;
    std::size_t manifest_index { };
    std::string identity;

    friend bool operator==(const CoverageExternalExclusionRule&,
        const CoverageExternalExclusionRule&)
        = default;
};

struct CoverageExternalExclusionPlan {
    std::vector<CoverageExternalExclusionRule> rules;
    std::string identity;

    friend bool operator==(const CoverageExternalExclusionPlan&,
        const CoverageExternalExclusionPlan&)
        = default;
};

struct CoverageExternalExclusionLimits {
    std::size_t maximum_rules { 1U << 16U };
    std::size_t maximum_pattern_bytes { 1U << 12U };
    std::size_t maximum_reason_bytes { 1U << 12U };
    std::size_t maximum_total_bytes { 1U << 24U };
    std::size_t maximum_targets { 1U << 20U };
    std::size_t maximum_target_bytes { 1U << 14U };
    std::size_t maximum_match_operations { 1U << 28U };
};

enum class CoverageExternalExclusionError : std::uint8_t {
    None,
    ResourceLimit,
    MissingMetric,
    UnknownMetric,
    MissingReason,
    EmptySelector,
    InvalidSourcePattern,
    InvalidHierarchyPattern,
    InvalidObjectPattern,
    DuplicateRule,
    ConflictingReason,
    InvalidPlan,
    InvalidTarget,
};

struct CoverageExternalExclusionPlanResult {
    std::optional<CoverageExternalExclusionPlan> plan;
    CoverageExternalExclusionError error {
        CoverageExternalExclusionError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return plan.has_value()
            && error == CoverageExternalExclusionError::None;
    }
};

struct CoverageExternalExclusionTarget {
    std::string_view source_path;
    std::string_view hierarchy_path;
    std::string_view object_path;
    frontend::CoverageSourceMetric metric {
        frontend::CoverageSourceMetric::Statement
    };
};

struct CoverageExternalExclusionTargetMatch {
    // Canonical rule indices for the corresponding input target. Each rule
    // retains its declaration index and reason for Change 14.
    std::vector<std::size_t> rule_indices;
};

struct CoverageExternalExclusionMatchResult {
    // One entry per input target, including targets that match no rule.
    std::vector<CoverageExternalExclusionTargetMatch> targets;
    CoverageExternalExclusionError error {
        CoverageExternalExclusionError::None
    };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageExternalExclusionError::None;
    }
};

[[nodiscard]] CoverageExternalExclusionPlanResult
make_coverage_external_exclusion_plan(
    std::span<const project::CoverageExclusionEntry> entries,
    CoverageExternalExclusionLimits limits = { }) noexcept;

[[nodiscard]] std::string_view coverage_external_exclusion_error_name(
    CoverageExternalExclusionError error) noexcept;

[[nodiscard]] CoverageExternalExclusionMatchResult
match_coverage_external_exclusions(
    const CoverageExternalExclusionPlan& plan,
    std::span<const CoverageExternalExclusionTarget> targets,
    CoverageExternalExclusionLimits limits = { }) noexcept;

} // namespace fsim::elaboration
