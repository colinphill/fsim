// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_point_identity.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/runtime/coverage_condition_evaluation.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::elaboration {

enum class CoverageConditionDecisionKind : std::uint8_t {
    If,
    Loop,
    ImmediateAssertion,
    WaitUntil,
};

using CoverageConditionLogicalOperator
    = runtime::CoverageConditionLogicalOperator;
using CoverageConditionOperand = runtime::CoverageConditionOperand;
using CoverageConditionPathStep = runtime::CoverageConditionPathStep;

struct CoverageConditionSource {
    std::string source_name;
    frontend::CodeCoverageSourceIdentity identity;
};

struct CoverageConditionPoint {
    runtime::CodeCoveragePointId id;
    frontend::CodeCoverageLanguage language {
        frontend::CodeCoverageLanguage::Verilog
    };
    CoverageConditionDecisionKind decision {
        CoverageConditionDecisionKind::If
    };
    frontend::ExpressionKind expression_kind {
        frontend::ExpressionKind::Invalid
    };
    frontend::CodeCoverageSourceSpan span;
    std::size_t source_index { };
    std::size_t decision_index { };
    std::size_t condition_index { };
    std::vector<CoverageConditionPathStep> evaluation_path;

    friend bool operator==(const CoverageConditionPoint&,
        const CoverageConditionPoint&)
        = default;
};

struct CoverageConditionLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_statements { 1U << 20U };
    std::size_t maximum_conditions { 1U << 20U };
    std::size_t maximum_expression_nodes { 1U << 22U };
    std::size_t maximum_path_steps { 1U << 22U };
    std::size_t maximum_statement_nesting { 1U << 12U };
    std::size_t maximum_expression_nesting { 1U << 12U };
};

enum class CoverageConditionError : std::uint8_t {
    None,
    InvalidLanguage,
    InvalidStandard,
    ResourceLimit,
    EmptySourceName,
    DuplicateSourceName,
    InvalidSourceIdentity,
    MissingDecisionCondition,
    MalformedLogicalExpression,
    UnknownConditionSource,
    InvalidConditionSpan,
    DuplicatePoint,
};

struct CoverageConditionResult {
    std::vector<CoverageConditionPoint> points;
    CoverageConditionError error { CoverageConditionError::None };
    std::size_t statement_index { };
    std::size_t expression_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageConditionError::None;
    }
};

[[nodiscard]] CoverageConditionResult discover_coverage_conditions(
    std::span<const frontend::Statement> statements,
    frontend::CodeCoverageLanguage language,
    std::span<const CoverageConditionSource> sources,
    CoverageConditionLimits limits = { }) noexcept;

} // namespace fsim::elaboration
