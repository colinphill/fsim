// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_toggle.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
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
    return { 0x1790000000000007ULL,
        static_cast<std::uint64_t>(index + 1U) };
}

std::vector<fsim::runtime::CoverageToggleOutcome> outcomes()
{
    return { { point(0U), 0U }, { point(0U), 1U }, { point(1U), 0U } };
}

fsim::runtime::CoverageToggleTransition transition(const std::size_t owner,
    const bool previous, const bool current)
{
    const auto state = outcomes();
    return { state[owner].point, state[owner].bit_index, owner,
        previous, current };
}

fsim::runtime::CoverageToggleValueTransition value_transition(
    const std::size_t owner,
    const fsim::runtime::CoverageToggleLogicValue previous,
    const fsim::runtime::CoverageToggleLogicValue current)
{
    const auto state = outcomes();
    return { state[owner].point, state[owner].bit_index, owner,
        previous, current };
}

void test_direction_identity_and_recording()
{
    using namespace fsim::runtime;
    auto state = outcomes();
    const auto rise = coverage_toggle_bin_id(
        state[0], CoverageToggleDirection::ZeroToOne);
    const auto fall = coverage_toggle_bin_id(
        state[0], CoverageToggleDirection::OneToZero);
    require(rise != fall && rise.point == fall.point
            && rise.bit_index == fall.bit_index,
        "one selected bit must own distinct direction-qualified bin identities");

    const std::array values {
        transition(0U, false, true),
        transition(0U, true, true),
        transition(1U, true, false),
        transition(2U, false, false),
    };
    const auto result = record_coverage_toggle_transitions(state, values);
    require(result.ok() && state[0].zero_to_one_hits == 1U
            && state[0].one_to_zero_hits == 0U
            && state[1].zero_to_one_hits == 0U
            && state[1].one_to_zero_hits == 1U
            && state[2].zero_to_one_hits == 0U
            && state[2].one_to_zero_hits == 0U,
        "only actual binary transitions may increment their exact direction");
    require(coverage_toggle_status(state[0]) == CodeCoverageStatus::Partial
            && coverage_toggle_status(state[1]) == CodeCoverageStatus::Partial
            && coverage_toggle_status(state[2])
                == CodeCoverageStatus::Uncovered,
        "toggle status must score the two transition directions separately");

    const std::array reverse { transition(0U, true, false) };
    require(record_coverage_toggle_transitions(state, reverse).ok()
            && coverage_toggle_status(state[0])
                == CodeCoverageStatus::Covered,
        "observing the reverse direction must complete rather than alias a bin");
}

void test_repeated_transitions_and_saturation()
{
    using namespace fsim::runtime;
    auto state = outcomes();
    const std::array repeated {
        transition(0U, false, true),
        transition(0U, true, false),
        transition(0U, false, true),
    };
    require(record_coverage_toggle_transitions(state, repeated).ok()
            && state[0].zero_to_one_hits == 2U
            && state[0].one_to_zero_hits == 1U,
        "ordered transition batches may contain multiple events for one bit");

    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    state[0].zero_to_one_hits = maximum;
    state[0].one_to_zero_hits = maximum;
    const std::array overflow {
        transition(0U, false, true),
        transition(0U, true, false),
    };
    const auto saturated
        = record_coverage_toggle_transitions(state, overflow);
    require(saturated.ok() && saturated.saturated_updates == 2U
            && state[0].zero_to_one_hits == maximum
            && state[0].one_to_zero_hits == maximum
            && state[0].zero_to_one_overflow
            && state[0].one_to_zero_overflow,
        "both direction counters must saturate explicitly without wrapping");
}

void test_unknown_and_high_impedance_observations()
{
    using namespace fsim::runtime;
    using Value = CoverageToggleLogicValue;
    auto state = outcomes();
    const std::array diagnostic {
        value_transition(0U, Value::Zero, Value::Unknown),
        value_transition(0U, Value::Unknown, Value::One),
        value_transition(0U, Value::One, Value::HighImpedance),
        value_transition(0U, Value::HighImpedance, Value::Zero),
        value_transition(0U, Value::Unknown, Value::HighImpedance),
        value_transition(0U, Value::Unknown, Value::Unknown),
        value_transition(0U, Value::HighImpedance, Value::HighImpedance),
    };
    const auto observed
        = record_coverage_toggle_value_transitions(state, diagnostic);
    require(observed.ok() && observed.unknown_observations == 3U
            && observed.high_impedance_observations == 3U
            && state[0].unknown_transition_observations == 3U
            && state[0].high_impedance_transition_observations == 3U
            && state[0].zero_to_one_hits == 0U
            && state[0].one_to_zero_hits == 0U
            && coverage_toggle_status(state[0])
                == CodeCoverageStatus::Uncovered,
        "X/Z activity must remain diagnostic and never satisfy a binary toggle bin");

    const std::array binary {
        value_transition(0U, Value::Zero, Value::One),
        value_transition(0U, Value::One, Value::Zero),
    };
    require(record_coverage_toggle_value_transitions(state, binary).ok()
            && state[0].zero_to_one_hits == 1U
            && state[0].one_to_zero_hits == 1U
            && state[0].unknown_transition_observations == 3U
            && state[0].high_impedance_transition_observations == 3U
            && coverage_toggle_status(state[0])
                == CodeCoverageStatus::Covered,
        "the four-state path must still score exact binary-to-binary transitions");
}

void test_diagnostic_saturation_and_validation()
{
    using namespace fsim::runtime;
    using Error = CoverageToggleError;
    using Value = CoverageToggleLogicValue;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto state = outcomes();
    state[0].unknown_transition_observations = maximum;
    state[0].high_impedance_transition_observations = maximum;
    const std::array diagnostic {
        value_transition(0U, Value::Unknown, Value::HighImpedance)
    };
    const auto saturated
        = record_coverage_toggle_value_transitions(state, diagnostic);
    require(saturated.ok() && saturated.saturated_updates == 2U
            && saturated.unknown_observations == 1U
            && saturated.high_impedance_observations == 1U
            && state[0].unknown_transition_observations == maximum
            && state[0].high_impedance_transition_observations == maximum
            && state[0].unknown_transition_overflow
            && state[0].high_impedance_transition_overflow,
        "X and Z diagnostic counters must saturate independently without wrapping");

    state = outcomes();
    state[0].unknown_transition_overflow = true;
    const auto before = state;
    require(record_coverage_toggle_value_transitions(state, diagnostic).error
                == Error::InvalidSaturationState
            && state == before,
        "inconsistent diagnostic saturation must fail transactionally");
    state = outcomes();
    auto invalid = diagnostic;
    invalid[0].current = static_cast<Value>(0xffU);
    require(record_coverage_toggle_value_transitions(state, invalid).error
                == Error::InvalidLogicValue
            && state == outcomes(),
        "invalid four-state encodings must fail before observation mutation");
    auto wrong = diagnostic;
    wrong[0].outcome_index = state.size();
    require(record_coverage_toggle_value_transitions(state, wrong).error
                == Error::TransitionOwnershipMismatch
            && state == outcomes(),
        "four-state transitions must retain exact dense outcome ownership");
    CoverageToggleLimits limits;
    limits.maximum_transitions = 0U;
    require(record_coverage_toggle_value_transitions(
                state, diagnostic, limits)
                    .error
                == Error::ResourceLimit
            && state == outcomes(),
        "four-state transition ceilings must apply before mutation");
}

void test_transactional_validation_and_bounds()
{
    using namespace fsim::runtime;
    using Error = CoverageToggleError;
    const std::array valid { transition(0U, false, true) };
    const auto check = [&](std::vector<CoverageToggleOutcome> state,
                           const auto& transitions, const Error error,
                           const std::string_view message,
                           CoverageToggleLimits limits = { }) {
        const auto before = state;
        const auto result
            = record_coverage_toggle_transitions(state, transitions, limits);
        require(result.error == error && state == before, message);
    };

    auto state = outcomes();
    state[0].point = { };
    check(state, valid, Error::InvalidPointIdentity,
        "invalid point identity must fail before counter mutation");
    state = outcomes();
    state[1].bit_index = 0U;
    check(state, valid, Error::DuplicateBitIdentity,
        "duplicate point and bit ownership must be rejected");
    state = outcomes();
    state[0].zero_to_one_overflow = true;
    check(state, valid, Error::InvalidSaturationState,
        "overflow state must agree with a saturated direction counter");
    state = outcomes();
    auto wrong = valid;
    wrong[0].bit_index = 1U;
    check(state, wrong, Error::TransitionOwnershipMismatch,
        "transition point and bit must match their dense owner");
    wrong = valid;
    wrong[0].outcome_index = state.size();
    check(state, wrong, Error::TransitionOwnershipMismatch,
        "transition owner ordinals must remain in range");

    CoverageToggleLimits limits;
    limits.maximum_outcomes = 2U;
    check(state, valid, Error::ResourceLimit,
        "outcome ceiling must apply before mutation", limits);
    limits = { };
    limits.maximum_transitions = 0U;
    check(state, valid, Error::ResourceLimit,
        "transition ceiling must apply before mutation", limits);
}

} // namespace

int main()
{
    test_direction_identity_and_recording();
    test_repeated_transitions_and_saturation();
    test_unknown_and_high_impedance_observations();
    test_diagnostic_saturation_and_validation();
    test_transactional_validation_and_bounds();
    std::cout << "coverage toggle tests passed\n";
    return 0;
}
