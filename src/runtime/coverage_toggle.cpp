// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_toggle.hpp"

#include <limits>
#include <set>
#include <tuple>

namespace fsim::runtime {
namespace {

    using BitKey = std::tuple<std::uint64_t, std::uint64_t, std::size_t>;

    bool valid_saturation(const CoverageToggleOutcome& outcome) noexcept
    {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        return (!outcome.zero_to_one_overflow
                   || outcome.zero_to_one_hits == maximum)
            && (!outcome.one_to_zero_overflow
                || outcome.one_to_zero_hits == maximum)
            && (!outcome.unknown_transition_overflow
                || outcome.unknown_transition_observations == maximum)
            && (!outcome.high_impedance_transition_overflow
                || outcome.high_impedance_transition_observations == maximum);
    }

    bool valid_logic_value(const CoverageToggleLogicValue value) noexcept
    {
        switch (value) {
        case CoverageToggleLogicValue::Zero:
        case CoverageToggleLogicValue::One:
        case CoverageToggleLogicValue::Unknown:
        case CoverageToggleLogicValue::HighImpedance:
            return true;
        }
        return false;
    }

    bool binary_logic_value(const CoverageToggleLogicValue value) noexcept
    {
        return value == CoverageToggleLogicValue::Zero
            || value == CoverageToggleLogicValue::One;
    }

    void increment(std::uint64_t& value, bool& overflow,
        std::size_t& saturated_updates) noexcept
    {
        if (value == std::numeric_limits<std::uint64_t>::max()) {
            overflow = true;
            ++saturated_updates;
        } else {
            ++value;
        }
    }

} // namespace

CoverageToggleUpdateResult record_coverage_toggle_transitions(
    const std::span<CoverageToggleOutcome> outcomes,
    const std::span<const CoverageToggleTransition> transitions,
    const CoverageToggleLimits limits) noexcept
{
    CoverageToggleUpdateResult result;
    const auto reject = [&](const CoverageToggleError error,
                            const std::size_t index = 0U) {
        result.error = error;
        result.index = index;
        result.saturated_updates = 0U;
        result.unknown_observations = 0U;
        result.high_impedance_observations = 0U;
        return result;
    };
    try {
        if (outcomes.size() > limits.maximum_outcomes
            || transitions.size() > limits.maximum_transitions) {
            return reject(CoverageToggleError::ResourceLimit);
        }
        std::set<BitKey> identities;
        for (std::size_t index = 0U; index < outcomes.size(); ++index) {
            const auto& outcome = outcomes[index];
            if (!is_code_coverage_identity_valid(outcome.point)) {
                return reject(CoverageToggleError::InvalidPointIdentity, index);
            }
            if (!identities.emplace(outcome.point.high, outcome.point.low,
                               outcome.bit_index)
                    .second) {
                return reject(CoverageToggleError::DuplicateBitIdentity, index);
            }
            if (!valid_saturation(outcome)) {
                return reject(CoverageToggleError::InvalidSaturationState,
                    index);
            }
        }
        for (std::size_t index = 0U; index < transitions.size(); ++index) {
            const auto& transition = transitions[index];
            if (transition.outcome_index >= outcomes.size()) {
                return reject(
                    CoverageToggleError::TransitionOwnershipMismatch, index);
            }
            const auto& outcome = outcomes[transition.outcome_index];
            if (transition.point != outcome.point
                || transition.bit_index != outcome.bit_index) {
                return reject(
                    CoverageToggleError::TransitionOwnershipMismatch, index);
            }
        }
        for (const auto& transition : transitions) {
            if (transition.previous == transition.current) {
                continue;
            }
            auto& outcome = outcomes[transition.outcome_index];
            if (!transition.previous && transition.current) {
                increment(outcome.zero_to_one_hits,
                    outcome.zero_to_one_overflow, result.saturated_updates);
            } else {
                increment(outcome.one_to_zero_hits,
                    outcome.one_to_zero_overflow, result.saturated_updates);
            }
        }
        return result;
    } catch (...) {
        return reject(CoverageToggleError::ResourceLimit);
    }
}

CoverageToggleUpdateResult record_coverage_toggle_value_transitions(
    const std::span<CoverageToggleOutcome> outcomes,
    const std::span<const CoverageToggleValueTransition> transitions,
    const CoverageToggleLimits limits) noexcept
{
    CoverageToggleUpdateResult result;
    const auto reject = [&](const CoverageToggleError error,
                            const std::size_t index = 0U) {
        result.error = error;
        result.index = index;
        result.saturated_updates = 0U;
        result.unknown_observations = 0U;
        result.high_impedance_observations = 0U;
        return result;
    };
    try {
        if (outcomes.size() > limits.maximum_outcomes
            || transitions.size() > limits.maximum_transitions) {
            return reject(CoverageToggleError::ResourceLimit);
        }
        std::set<BitKey> identities;
        for (std::size_t index = 0U; index < outcomes.size(); ++index) {
            const auto& outcome = outcomes[index];
            if (!is_code_coverage_identity_valid(outcome.point)) {
                return reject(CoverageToggleError::InvalidPointIdentity, index);
            }
            if (!identities.emplace(outcome.point.high, outcome.point.low,
                               outcome.bit_index)
                    .second) {
                return reject(CoverageToggleError::DuplicateBitIdentity, index);
            }
            if (!valid_saturation(outcome)) {
                return reject(CoverageToggleError::InvalidSaturationState,
                    index);
            }
        }
        for (std::size_t index = 0U; index < transitions.size(); ++index) {
            const auto& transition = transitions[index];
            if (!valid_logic_value(transition.previous)
                || !valid_logic_value(transition.current)) {
                return reject(CoverageToggleError::InvalidLogicValue, index);
            }
            if (transition.outcome_index >= outcomes.size()) {
                return reject(
                    CoverageToggleError::TransitionOwnershipMismatch, index);
            }
            const auto& outcome = outcomes[transition.outcome_index];
            if (transition.point != outcome.point
                || transition.bit_index != outcome.bit_index) {
                return reject(
                    CoverageToggleError::TransitionOwnershipMismatch, index);
            }
        }
        for (const auto& transition : transitions) {
            if (transition.previous == transition.current) {
                continue;
            }
            auto& outcome = outcomes[transition.outcome_index];
            if (binary_logic_value(transition.previous)
                && binary_logic_value(transition.current)) {
                if (transition.previous == CoverageToggleLogicValue::Zero) {
                    increment(outcome.zero_to_one_hits,
                        outcome.zero_to_one_overflow,
                        result.saturated_updates);
                } else {
                    increment(outcome.one_to_zero_hits,
                        outcome.one_to_zero_overflow,
                        result.saturated_updates);
                }
                continue;
            }
            if (transition.previous == CoverageToggleLogicValue::Unknown
                || transition.current == CoverageToggleLogicValue::Unknown) {
                ++result.unknown_observations;
                increment(outcome.unknown_transition_observations,
                    outcome.unknown_transition_overflow,
                    result.saturated_updates);
            }
            if (transition.previous
                    == CoverageToggleLogicValue::HighImpedance
                || transition.current
                    == CoverageToggleLogicValue::HighImpedance) {
                ++result.high_impedance_observations;
                increment(outcome.high_impedance_transition_observations,
                    outcome.high_impedance_transition_overflow,
                    result.saturated_updates);
            }
        }
        return result;
    } catch (...) {
        return reject(CoverageToggleError::ResourceLimit);
    }
}

} // namespace fsim::runtime
