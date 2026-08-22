// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_condition_outcomes.hpp"

#include <limits>
#include <set>
#include <utility>
#include <vector>

namespace fsim::runtime {
namespace {

    using PointKey = std::pair<std::uint64_t, std::uint64_t>;

    bool valid_truth(const CoverageConditionTruth truth) noexcept
    {
        switch (truth) {
        case CoverageConditionTruth::False:
        case CoverageConditionTruth::True:
        case CoverageConditionTruth::Unknown:
            return true;
        }
        return false;
    }

    bool valid_saturation_state(const CoverageConditionOutcome& outcome) noexcept
    {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        return (!outcome.true_overflow || outcome.true_hits == maximum)
            && (!outcome.false_overflow || outcome.false_hits == maximum)
            && (!outcome.unknown_overflow
                || outcome.unknown_observations == maximum);
    }

    void increment(std::uint64_t& value, bool& overflow,
        std::size_t& saturated_updates) noexcept
    {
        if (value == std::numeric_limits<std::uint64_t>::max()) {
            overflow = true;
            ++saturated_updates;
            return;
        }
        ++value;
    }

} // namespace

CoverageConditionOutcomeUpdateResult record_coverage_condition_outcomes(
    const std::span<CoverageConditionOutcome> outcomes,
    const std::span<const CoverageConditionObservation> observations,
    const CoverageConditionOutcomeLimits limits) noexcept
{
    CoverageConditionOutcomeUpdateResult result;
    const auto reject = [&](const CoverageConditionOutcomeError error,
                            const std::size_t index = 0U) {
        result.error = error;
        result.index = index;
        result.saturated_updates = 0U;
        return result;
    };

    try {
        if (outcomes.size() > limits.maximum_outcomes
            || observations.size() > limits.maximum_observations) {
            return reject(CoverageConditionOutcomeError::ResourceLimit);
        }
        std::set<PointKey> point_ids;
        for (std::size_t index = 0U; index < outcomes.size(); ++index) {
            const auto& outcome = outcomes[index];
            if (!is_code_coverage_identity_valid(outcome.point)) {
                return reject(
                    CoverageConditionOutcomeError::InvalidPointIdentity,
                    index);
            }
            if (!point_ids.emplace(
                              outcome.point.high, outcome.point.low)
                    .second) {
                return reject(
                    CoverageConditionOutcomeError::DuplicatePointIdentity,
                    index);
            }
            if (!valid_saturation_state(outcome)) {
                return reject(
                    CoverageConditionOutcomeError::InvalidSaturationState,
                    index);
            }
        }

        std::vector<bool> observed(outcomes.size(), false);
        for (std::size_t index = 0U; index < observations.size(); ++index) {
            const auto& observation = observations[index];
            if (!valid_truth(observation.truth)) {
                return reject(
                    CoverageConditionOutcomeError::InvalidObservationTruth,
                    index);
            }
            if (observation.condition_index >= outcomes.size()
                || outcomes[observation.condition_index].point
                    != observation.point) {
                return reject(
                    CoverageConditionOutcomeError::ObservationOwnershipMismatch,
                    index);
            }
            if (observed[observation.condition_index]) {
                return reject(
                    CoverageConditionOutcomeError::DuplicateObservation,
                    index);
            }
            observed[observation.condition_index] = true;
        }

        for (const auto& observation : observations) {
            auto& outcome = outcomes[observation.condition_index];
            switch (observation.truth) {
            case CoverageConditionTruth::True:
                increment(outcome.true_hits, outcome.true_overflow,
                    result.saturated_updates);
                break;
            case CoverageConditionTruth::False:
                increment(outcome.false_hits, outcome.false_overflow,
                    result.saturated_updates);
                break;
            case CoverageConditionTruth::Unknown:
                increment(outcome.unknown_observations,
                    outcome.unknown_overflow, result.saturated_updates);
                break;
            }
        }
        return result;
    } catch (...) {
        return reject(CoverageConditionOutcomeError::ResourceLimit);
    }
}

} // namespace fsim::runtime
