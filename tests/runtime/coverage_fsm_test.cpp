// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_fsm.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using fsim::runtime::CodeCoveragePointId;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

CodeCoveragePointId point(const std::size_t index)
{
    return { 0x1790000000000011ULL,
        static_cast<std::uint64_t>(index + 1U) };
}

fsim::runtime::CoverageFsmDefinition definition(
    const CodeCoveragePointId instance = point(0U))
{
    using namespace fsim::runtime;
    CoverageFsmMachineDefinition machine;
    machine.current_state_object = point(1U);
    machine.states = { point(4U), point(2U), point(3U) };
    machine.legal_transitions = {
        { point(3U), point(2U) },
        { point(2U), point(3U) },
        { point(2U), point(2U) },
    };
    return { instance, 7U, "top.codec", { std::move(machine) } };
}

const fsim::runtime::CoverageFsmVisitBin& visit(
    const fsim::runtime::CoverageFsmRuntimeModel& model,
    const CodeCoveragePointId state)
{
    const auto found = std::ranges::find(
        model.state_visits, state,
        &fsim::runtime::CoverageFsmVisitBin::state);
    require(found != model.state_visits.end(), "expected state visit bin");
    return *found;
}

const fsim::runtime::CoverageFsmTransitionBin& transition(
    const fsim::runtime::CoverageFsmRuntimeModel& model,
    const CodeCoveragePointId from, const CodeCoveragePointId to)
{
    const auto found = std::ranges::find_if(
        model.legal_transitions, [&](const auto& bin) {
            return bin.from_state == from && bin.to_state == to;
        });
    require(found != model.legal_transitions.end(),
        "expected legal transition bin");
    return *found;
}

void test_stable_identity_and_canonical_order()
{
    using namespace fsim::runtime;
    auto first_input = definition();
    auto second_input = first_input;
    std::ranges::reverse(second_input.machines[0].states);
    std::ranges::reverse(second_input.machines[0].legal_transitions);
    const auto first = make_coverage_fsm_runtime_model(first_input);
    const auto second = make_coverage_fsm_runtime_model(second_input);
    require(first.ok() && second.ok() && *first.model == *second.model,
        "definition order must not change canonical FSM bins or identities");
    require(first.model->state_visits.size() == 3U
            && first.model->legal_transitions.size() == 3U,
        "state visits and declared legal transitions need separate bins");

    const auto other = make_coverage_fsm_runtime_model(definition(point(20U)));
    require(other.ok()
            && other.model->state_visits[0].id
                != first.model->state_visits[0].id
            && other.model->legal_transitions[0].id
                != first.model->legal_transitions[0].id,
        "hierarchical instance identity must qualify every FSM bin identity");
}

void test_visits_and_legal_transitions_score_separately()
{
    using namespace fsim::runtime;
    auto built = make_coverage_fsm_runtime_model(definition());
    require(built.ok(), "valid FSM definition must build");
    auto model = std::move(*built.model);
    const std::array observations {
        CoverageFsmObservation { point(1U), point(2U) },
        CoverageFsmObservation { point(1U), point(3U) },
        CoverageFsmObservation { point(1U), point(4U) },
        CoverageFsmObservation { point(1U), point(2U) },
    };
    const auto recorded = record_coverage_fsm_observations(model, observations);
    require(recorded.ok() && recorded.illegal_transitions == 2U
            && recorded.saturated_updates == 0U,
        "undeclared observed pairs must remain diagnostic only");
    require(visit(model, point(2U)).hits == 2U
            && visit(model, point(3U)).hits == 1U
            && visit(model, point(4U)).hits == 1U,
        "every observation must score exactly one state visit");
    require(transition(model, point(2U), point(3U)).hits == 1U
            && transition(model, point(3U), point(2U)).hits == 0U
            && transition(model, point(2U), point(2U)).hits == 0U,
        "only explicitly declared ordered transitions may score");
    require(model.machines[0].illegal_transition_observations == 2U,
        "machine diagnostics must retain undeclared-transition observations");

    const auto summary = summarize_coverage_fsm(model);
    require(summary.ok()
            && summary.summary->state_visits
                == CoverageFsmMetricCoverage { 3U, 3U }
            && summary.summary->legal_transitions
                == CoverageFsmMetricCoverage { 3U, 1U },
        "reports must keep visit and legal-transition totals independent");
}

void test_machine_histories_are_independent()
{
    using namespace fsim::runtime;
    auto input = definition();
    CoverageFsmMachineDefinition second;
    second.current_state_object = point(10U);
    second.states = { point(11U), point(12U) };
    second.legal_transitions = { { point(11U), point(12U) } };
    input.machines.push_back(std::move(second));
    auto built = make_coverage_fsm_runtime_model(input);
    require(built.ok(), "multiple independent FSMs must build");
    auto model = std::move(*built.model);
    const std::array observations {
        CoverageFsmObservation { point(1U), point(2U) },
        CoverageFsmObservation { point(10U), point(11U) },
        CoverageFsmObservation { point(1U), point(3U) },
        CoverageFsmObservation { point(10U), point(12U) },
    };
    const auto recorded = record_coverage_fsm_observations(model, observations);
    require(recorded.ok() && recorded.illegal_transitions == 0U
            && transition(model, point(2U), point(3U)).hits == 1U
            && transition(model, point(11U), point(12U)).hits == 1U,
        "interleaved machines must retain independent previous-state history");
}

void test_saturation_without_wrap()
{
    using namespace fsim::runtime;
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    auto built = make_coverage_fsm_runtime_model(definition());
    require(built.ok(), "saturation fixture must build");
    auto model = std::move(*built.model);
    auto& machine = model.machines[0];
    machine.previous_state = point(2U);
    machine.illegal_transition_observations = maximum;
    auto visit_found = std::ranges::find(model.state_visits, point(3U),
        &CoverageFsmVisitBin::state);
    require(visit_found != model.state_visits.end(),
        "saturation visit bin must exist");
    auto& visit_bin = *visit_found;
    visit_bin.hits = maximum;
    auto transition_found = std::ranges::find_if(
        model.legal_transitions, [](const auto& bin) {
            return bin.from_state == point(2U)
                && bin.to_state == point(3U);
        });
    require(transition_found != model.legal_transitions.end(),
        "saturation transition bin must exist");
    auto& transition_bin = *transition_found;
    transition_bin.hits = maximum;
    const std::array observations {
        CoverageFsmObservation { point(1U), point(3U) },
        CoverageFsmObservation { point(1U), point(4U) },
    };
    const auto saturated = record_coverage_fsm_observations(model, observations);
    require(saturated.ok() && saturated.illegal_transitions == 1U
            && saturated.saturated_updates == 3U
            && visit_bin.hits == maximum && visit_bin.overflow
            && transition_bin.hits == maximum && transition_bin.overflow
            && machine.illegal_transition_observations == maximum
            && machine.illegal_transition_overflow,
        "visit, transition, and diagnostic counters must saturate explicitly");
    const std::array repeat {
        CoverageFsmObservation { point(1U), point(3U) }
    };
    require(record_coverage_fsm_observations(model, repeat).saturated_updates
            == 0U,
        "already-reported saturation must not emit another overflow event");
}

void test_definition_rejection_and_resource_bounds()
{
    using namespace fsim::runtime;
    using Error = CoverageFsmRuntimeError;
    const auto rejects = [](CoverageFsmDefinition input, const Error error,
                             const std::string_view message,
                             CoverageFsmRuntimeLimits limits = { }) {
        const auto result = make_coverage_fsm_runtime_model(input, limits);
        require(!result.ok() && result.error == error, message);
    };
    auto input = definition();
    input.instance_identity = { };
    rejects(input, Error::InvalidInstanceIdentity,
        "invalid instance identity must be rejected");
    input = definition();
    input.instance.clear();
    rejects(input, Error::InvalidInstance,
        "empty instance path must be rejected");
    input = definition();
    input.machines.push_back(input.machines.front());
    rejects(input, Error::DuplicateCurrentStateObject,
        "current-state objects must be unique");
    input = definition();
    input.machines[0].states[0] = { };
    rejects(input, Error::InvalidState, "invalid states must be rejected");
    input = definition();
    input.machines[0].states[0] = input.machines[0].states[1];
    rejects(input, Error::DuplicateState, "duplicate states must be rejected");
    input = definition();
    input.machines[0].legal_transitions.push_back(
        input.machines[0].legal_transitions.front());
    rejects(input, Error::DuplicateLegalTransition,
        "duplicate legal transitions must be rejected");
    input = definition();
    input.machines[0].legal_transitions[0].to_state = point(99U);
    rejects(input, Error::InvalidLegalTransition,
        "legal transition endpoints must belong to the machine");

    CoverageFsmRuntimeLimits limits;
    limits.maximum_machines = 0U;
    rejects(definition(), Error::ResourceLimit,
        "machine ceiling must be enforced", limits);
    limits = { };
    limits.maximum_states = 2U;
    rejects(definition(), Error::ResourceLimit,
        "state ceiling must be enforced", limits);
    limits = { };
    limits.maximum_legal_transitions = 2U;
    rejects(definition(), Error::ResourceLimit,
        "transition ceiling must be enforced", limits);
    limits = { };
    limits.maximum_instance_bytes = 4U;
    rejects(definition(), Error::ResourceLimit,
        "instance path ceiling must be enforced", limits);
}

void test_model_and_batch_validation_are_transactional()
{
    using namespace fsim::runtime;
    using Error = CoverageFsmRuntimeError;
    const std::array valid {
        CoverageFsmObservation { point(1U), point(2U) }
    };
    const auto rejects_model = [&](auto mutate, const Error error,
                                   const std::string_view message) {
        auto built = make_coverage_fsm_runtime_model(definition());
        require(built.ok(), "transactional fixture must build");
        auto model = std::move(*built.model);
        mutate(model);
        const auto before = model;
        const auto result = record_coverage_fsm_observations(model, valid);
        require(result.error == error && model == before, message);
    };
    rejects_model([](auto& model) { model.state_visits[0].id = { }; },
        Error::DuplicateBinIdentity,
        "tampered bin identity must fail before mutation");
    rejects_model([](auto& model) { model.state_visits[0].overflow = true; },
        Error::InvalidSaturationState,
        "inconsistent visit saturation must fail before mutation");
    rejects_model([](auto& model) {
        model.machines[0].previous_state = point(99U);
    },
        Error::InvalidPreviousState, "previous state must remain owned by its machine");
    rejects_model([](auto& model) {
        std::ranges::reverse(model.state_visits);
    },
        Error::InvalidCanonicalOrder, "runtime bins must retain canonical order");

    auto built = make_coverage_fsm_runtime_model(definition());
    require(built.ok(), "observation fixture must build");
    auto model = std::move(*built.model);
    const auto before = model;
    const std::array observations {
        CoverageFsmObservation { point(1U), point(2U) },
        CoverageFsmObservation { point(1U), point(99U) },
    };
    require(record_coverage_fsm_observations(model, observations).error
                == Error::ObservationOwnershipMismatch
            && model == before,
        "the complete observation batch must validate before mutation");
    CoverageFsmRuntimeLimits limits;
    limits.maximum_observations = 1U;
    require(record_coverage_fsm_observations(model, observations, limits).error
                == Error::ResourceLimit
            && model == before,
        "observation ceiling must apply before mutation");
}

} // namespace

int main()
{
    test_stable_identity_and_canonical_order();
    test_visits_and_legal_transitions_score_separately();
    test_machine_histories_are_independent();
    test_saturation_without_wrap();
    test_definition_rejection_and_resource_bounds();
    test_model_and_batch_validation_are_transactional();
    std::cout << "coverage FSM runtime tests passed\n";
    return 0;
}
