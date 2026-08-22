// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/coverage_condition_evaluation.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace fsim::runtime {

inline constexpr std::string_view kCoverageConditionOutcomesDiagnostic
    = "FSIM-COV-018";

struct CoverageConditionOutcome {
    CodeCoveragePointId point;
    std::uint64_t true_hits { };
    std::uint64_t false_hits { };
    std::uint64_t unknown_observations { };
    bool true_overflow { };
    bool false_overflow { };
    bool unknown_overflow { };

    friend bool operator==(const CoverageConditionOutcome&,
        const CoverageConditionOutcome&)
        = default;
};

struct CoverageConditionOutcomeLimits {
    std::size_t maximum_outcomes { 1U << 20U };
    std::size_t maximum_observations { 1U << 20U };
};

enum class CoverageConditionOutcomeError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidPointIdentity,
    DuplicatePointIdentity,
    InvalidSaturationState,
    InvalidObservationTruth,
    ObservationOwnershipMismatch,
    DuplicateObservation,
};

struct CoverageConditionOutcomeUpdateResult {
    CoverageConditionOutcomeError error {
        CoverageConditionOutcomeError::None
    };
    std::size_t index { };
    std::size_t saturated_updates { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageConditionOutcomeError::None;
    }
};

[[nodiscard]] constexpr CodeCoverageStatus
coverage_condition_outcome_status(
    const CoverageConditionOutcome& outcome) noexcept
{
    const auto covered = static_cast<unsigned>(outcome.true_hits != 0U)
        + static_cast<unsigned>(outcome.false_hits != 0U);
    return covered == 0U ? CodeCoverageStatus::Uncovered
        : covered == 1U  ? CodeCoverageStatus::Partial
                         : CodeCoverageStatus::Covered;
}

[[nodiscard]] CoverageConditionOutcomeUpdateResult
record_coverage_condition_outcomes(
    std::span<CoverageConditionOutcome> outcomes,
    std::span<const CoverageConditionObservation> observations,
    CoverageConditionOutcomeLimits limits = { }) noexcept;

} // namespace fsim::runtime
