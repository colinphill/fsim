// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/code_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::string_view kCoverageConditionEvaluationDiagnostic
    = "FSIM-COV-017";

enum class CoverageConditionLogicalOperator : std::uint8_t {
    And,
    Or,
    Nand,
    Nor,
    Xor,
    Xnor,
    Not,
};

enum class CoverageConditionOperand : std::uint8_t {
    Left,
    Right,
    Only,
};

struct CoverageConditionPathStep {
    CoverageConditionLogicalOperator logical_operator {
        CoverageConditionLogicalOperator::And
    };
    CoverageConditionOperand operand { CoverageConditionOperand::Left };

    friend bool operator==(const CoverageConditionPathStep&,
        const CoverageConditionPathStep&)
        = default;
};

enum class CoverageConditionTruth : std::uint8_t {
    False,
    True,
    Unknown,
};

struct CoverageConditionEvaluationAtom {
    CodeCoveragePointId point;
    std::size_t condition_index { };
    std::span<const CoverageConditionPathStep> evaluation_path;
};

struct CoverageConditionObservation {
    CodeCoveragePointId point;
    std::size_t condition_index { };
    CoverageConditionTruth truth { CoverageConditionTruth::Unknown };

    friend bool operator==(const CoverageConditionObservation&,
        const CoverageConditionObservation&)
        = default;
};

struct CoverageConditionSkippedAtom {
    CodeCoveragePointId point;
    std::size_t condition_index { };

    friend bool operator==(const CoverageConditionSkippedAtom&,
        const CoverageConditionSkippedAtom&)
        = default;
};

struct CoverageConditionEvaluationLimits {
    std::size_t maximum_atoms { 1U << 20U };
    std::size_t maximum_nodes { 1U << 22U };
    std::size_t maximum_path_steps { 1U << 22U };
    std::size_t maximum_nesting { 1U << 12U };
};

enum class CoverageConditionEvaluationError : std::uint8_t {
    None,
    EmptyDecision,
    ResourceLimit,
    InvalidPointIdentity,
    DuplicatePointIdentity,
    DuplicateConditionIndex,
    NonCanonicalConditionIndex,
    MalformedEvaluationPath,
    EvaluationFailure,
};

struct CoverageConditionEvaluationResult {
    std::vector<CoverageConditionObservation> observations;
    std::vector<CoverageConditionSkippedAtom> skipped;
    CoverageConditionTruth decision_truth { CoverageConditionTruth::Unknown };
    CoverageConditionEvaluationError error {
        CoverageConditionEvaluationError::None
    };
    std::size_t condition_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageConditionEvaluationError::None;
    }
};

using CoverageConditionAtomEvaluator = std::function<
    std::optional<CoverageConditionTruth>(
        const CoverageConditionEvaluationAtom&)>;

[[nodiscard]] CoverageConditionEvaluationResult
evaluate_coverage_condition(
    std::span<const CoverageConditionEvaluationAtom> atoms,
    const CoverageConditionAtomEvaluator& evaluate,
    CoverageConditionEvaluationLimits limits = { }) noexcept;

} // namespace fsim::runtime
