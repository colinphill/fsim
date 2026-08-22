// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/code_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace fsim::runtime {

inline constexpr std::string_view kCoverageToggleDiagnostic = "FSIM-COV-020";

enum class CoverageToggleDirection : std::uint8_t {
    ZeroToOne = 1U,
    OneToZero = 2U,
};

struct CoverageToggleBinId {
    CodeCoveragePointId point;
    std::size_t bit_index { };
    CoverageToggleDirection direction { CoverageToggleDirection::ZeroToOne };

    friend bool operator==(const CoverageToggleBinId&,
        const CoverageToggleBinId&)
        = default;
};

struct CoverageToggleOutcome {
    CodeCoveragePointId point;
    std::size_t bit_index { };
    std::uint64_t zero_to_one_hits { };
    std::uint64_t one_to_zero_hits { };
    bool zero_to_one_overflow { };
    bool one_to_zero_overflow { };
    std::uint64_t unknown_transition_observations { };
    std::uint64_t high_impedance_transition_observations { };
    bool unknown_transition_overflow { };
    bool high_impedance_transition_overflow { };

    friend bool operator==(const CoverageToggleOutcome&,
        const CoverageToggleOutcome&)
        = default;
};

enum class CoverageToggleLogicValue : std::uint8_t {
    Zero = 0U,
    One = 1U,
    Unknown = 2U,
    HighImpedance = 3U,
};

struct CoverageToggleValueTransition {
    CodeCoveragePointId point;
    std::size_t bit_index { };
    std::size_t outcome_index { };
    CoverageToggleLogicValue previous { CoverageToggleLogicValue::Zero };
    CoverageToggleLogicValue current { CoverageToggleLogicValue::Zero };
};

struct CoverageToggleTransition {
    CodeCoveragePointId point;
    std::size_t bit_index { };
    std::size_t outcome_index { };
    bool previous { };
    bool current { };
};

struct CoverageToggleLimits {
    std::size_t maximum_outcomes { 1U << 22U };
    std::size_t maximum_transitions { 1U << 22U };
};

enum class CoverageToggleError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidPointIdentity,
    DuplicateBitIdentity,
    InvalidSaturationState,
    InvalidLogicValue,
    TransitionOwnershipMismatch,
};

struct CoverageToggleUpdateResult {
    CoverageToggleError error { CoverageToggleError::None };
    std::size_t index { };
    std::size_t saturated_updates { };
    std::size_t unknown_observations { };
    std::size_t high_impedance_observations { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageToggleError::None;
    }
};

[[nodiscard]] constexpr CoverageToggleBinId coverage_toggle_bin_id(
    const CoverageToggleOutcome& outcome,
    const CoverageToggleDirection direction) noexcept
{
    return { outcome.point, outcome.bit_index, direction };
}

[[nodiscard]] constexpr CodeCoverageStatus coverage_toggle_status(
    const CoverageToggleOutcome& outcome) noexcept
{
    const auto covered = static_cast<unsigned>(outcome.zero_to_one_hits != 0U)
        + static_cast<unsigned>(outcome.one_to_zero_hits != 0U);
    return covered == 0U ? CodeCoverageStatus::Uncovered
        : covered == 1U  ? CodeCoverageStatus::Partial
                         : CodeCoverageStatus::Covered;
}

[[nodiscard]] CoverageToggleUpdateResult record_coverage_toggle_transitions(
    std::span<CoverageToggleOutcome> outcomes,
    std::span<const CoverageToggleTransition> transitions,
    CoverageToggleLimits limits = { }) noexcept;

// X and Z participation is retained as diagnostic activity only. A value
// transition can satisfy a scored binary bin only when both endpoints are
// exactly Zero or One.
[[nodiscard]] CoverageToggleUpdateResult
record_coverage_toggle_value_transitions(
    std::span<CoverageToggleOutcome> outcomes,
    std::span<const CoverageToggleValueTransition> transitions,
    CoverageToggleLimits limits = { }) noexcept;

} // namespace fsim::runtime
