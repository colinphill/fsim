// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_condition_outcomes.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace {

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

fsim::runtime::CodeCoveragePointId point(const std::size_t index)
{
    return { 0x1790000000000005ULL,
        static_cast<std::uint64_t>(index + 1U) };
}

std::vector<fsim::runtime::CoverageConditionOutcome> outcomes(
    const std::size_t count)
{
    std::vector<fsim::runtime::CoverageConditionOutcome> result;
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        result.push_back({ point(index) });
    }
    return result;
}

fsim::runtime::CoverageConditionObservation observation(
    const std::size_t index,
    const fsim::runtime::CoverageConditionTruth truth)
{
    return { point(index), index, truth };
}

void test_binary_scoring_and_auxiliary_unknowns()
{
    using namespace fsim::runtime;
    auto state = outcomes(4U);
    const std::array first {
        observation(0U, CoverageConditionTruth::True),
        observation(1U, CoverageConditionTruth::False),
        observation(2U, CoverageConditionTruth::Unknown),
    };
    const auto recorded = record_coverage_condition_outcomes(state, first);
    require(recorded.ok() && recorded.saturated_updates == 0U,
        "valid atomic observations must record without saturation");
    require(state[0].true_hits == 1U && state[0].false_hits == 0U
            && state[0].unknown_observations == 0U,
        "true truth must increment only the true scored bin");
    require(state[1].true_hits == 0U && state[1].false_hits == 1U
            && state[1].unknown_observations == 0U,
        "false truth must increment only the false scored bin");
    require(state[2].true_hits == 0U && state[2].false_hits == 0U
            && state[2].unknown_observations == 1U,
        "unknown truth must increment only its auxiliary observation");
    require(state[3] == CoverageConditionOutcome { point(3U) },
        "a short-circuited atom with no observation must remain unchanged");
    require(coverage_condition_outcome_status(state[0])
                == CodeCoverageStatus::Partial
            && coverage_condition_outcome_status(state[1])
                == CodeCoverageStatus::Partial
            && coverage_condition_outcome_status(state[2])
                == CodeCoverageStatus::Uncovered
            && coverage_condition_outcome_status(state[3])
                == CodeCoverageStatus::Uncovered,
        "unknown observations must not satisfy either binary score bin");

    const std::array second {
        observation(0U, CoverageConditionTruth::False),
        observation(1U, CoverageConditionTruth::True),
        observation(2U, CoverageConditionTruth::True),
    };
    require(record_coverage_condition_outcomes(state, second).ok(),
        "a later evaluation must update the same dense ownership table");
    require(coverage_condition_outcome_status(state[0])
                == CodeCoverageStatus::Covered
            && coverage_condition_outcome_status(state[1])
                == CodeCoverageStatus::Covered
            && coverage_condition_outcome_status(state[2])
                == CodeCoverageStatus::Partial,
        "condition status must derive from separate true and false bins");
}

void test_saturation_is_explicit_and_nonwrapping()
{
    using namespace fsim::runtime;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto state = outcomes(3U);
    state[0].true_hits = maximum;
    state[1].false_hits = maximum;
    state[2].unknown_observations = maximum;
    const std::array values {
        observation(0U, CoverageConditionTruth::True),
        observation(1U, CoverageConditionTruth::False),
        observation(2U, CoverageConditionTruth::Unknown),
    };
    const auto result = record_coverage_condition_outcomes(state, values);
    require(result.ok() && result.saturated_updates == 3U,
        "every saturated update must be reported explicitly");
    require(state[0].true_hits == maximum && state[0].true_overflow
            && state[1].false_hits == maximum && state[1].false_overflow
            && state[2].unknown_observations == maximum
            && state[2].unknown_overflow,
        "scored and auxiliary counters must saturate without wrapping");
    require(coverage_condition_outcome_status(state[2])
            == CodeCoverageStatus::Uncovered,
        "even saturated unknown activity must remain outside binary scoring");
}

void test_transactional_rejections()
{
    using namespace fsim::runtime;
    using Error = CoverageConditionOutcomeError;
    const std::array valid {
        observation(0U, CoverageConditionTruth::True),
    };

    auto require_unchanged = [&](std::vector<CoverageConditionOutcome> state,
                                 const auto& observations,
                                 const Error error,
                                 const std::string_view message) {
        const auto before = state;
        const auto result
            = record_coverage_condition_outcomes(state, observations);
        require(!result.ok() && result.error == error && state == before,
            message);
    };

    auto state = outcomes(2U);
    state[0].point = { };
    require_unchanged(state, valid, Error::InvalidPointIdentity,
        "invalid table identities must fail before mutation");
    state = outcomes(2U);
    state[1].point = state[0].point;
    require_unchanged(state, valid, Error::DuplicatePointIdentity,
        "duplicate table identities must fail before mutation");
    state = outcomes(2U);
    state[0].true_overflow = true;
    require_unchanged(state, valid, Error::InvalidSaturationState,
        "overflow flags without saturated counters must be rejected");

    state = outcomes(2U);
    auto bad_truth = valid;
    bad_truth[0].truth = static_cast<CoverageConditionTruth>(255U);
    require_unchanged(state, bad_truth, Error::InvalidObservationTruth,
        "invalid truth encodings must be rejected transactionally");
    auto wrong_point = valid;
    wrong_point[0].point = point(1U);
    require_unchanged(state, wrong_point, Error::ObservationOwnershipMismatch,
        "an ordinal cannot observe another point owner");
    auto wrong_index = valid;
    wrong_index[0].condition_index = 2U;
    require_unchanged(state, wrong_index, Error::ObservationOwnershipMismatch,
        "out-of-range condition ordinals must be rejected");
    const std::array duplicate {
        observation(0U, CoverageConditionTruth::True),
        observation(0U, CoverageConditionTruth::False),
    };
    require_unchanged(state, duplicate, Error::DuplicateObservation,
        "one evaluation cannot publish two outcomes for one atom");
}

void test_resource_bounds_precede_mutation()
{
    using namespace fsim::runtime;
    auto state = outcomes(2U);
    const auto before = state;
    const std::array values {
        observation(0U, CoverageConditionTruth::True),
        observation(1U, CoverageConditionTruth::False),
    };
    CoverageConditionOutcomeLimits limits;
    limits.maximum_outcomes = 1U;
    auto result = record_coverage_condition_outcomes(state, values, limits);
    require(result.error == CoverageConditionOutcomeError::ResourceLimit
            && state == before,
        "outcome-table ceilings must apply before mutation");
    limits = { };
    limits.maximum_observations = 1U;
    result = record_coverage_condition_outcomes(state, values, limits);
    require(result.error == CoverageConditionOutcomeError::ResourceLimit
            && state == before,
        "observation-batch ceilings must apply before mutation");

    std::vector<CoverageConditionOutcome> empty;
    require(record_coverage_condition_outcomes(empty, { }).ok(),
        "an empty initialized design with no observations must be a no-op");
}

} // namespace

int main()
{
    test_binary_scoring_and_auxiliary_unknowns();
    test_saturation_is_explicit_and_nonwrapping();
    test_transactional_rejections();
    test_resource_bounds_precede_mutation();
    std::cout << "coverage condition outcome tests passed\n";
    return 0;
}
