// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/coverage_fsm.hpp"

#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::runtime {
namespace {

    using Error = CoverageFsmRuntimeError;
    using IdKey = std::pair<std::uint64_t, std::uint64_t>;
    using VisitKey = std::tuple<std::uint64_t, std::uint64_t,
        std::uint64_t, std::uint64_t>;
    using TransitionKey = std::tuple<std::uint64_t, std::uint64_t,
        std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t>;

    IdKey key(const CodeCoveragePointId id) noexcept
    {
        return { id.high, id.low };
    }

    VisitKey visit_key(
        const CodeCoveragePointId object, const CodeCoveragePointId state) noexcept
    {
        return { object.high, object.low, state.high, state.low };
    }

    TransitionKey transition_key(const CodeCoveragePointId object,
        const CodeCoveragePointId from, const CodeCoveragePointId to) noexcept
    {
        return { object.high, object.low, from.high, from.low, to.high, to.low };
    }

    void update_u64(support::Sha256& hash, const std::uint64_t value) noexcept
    {
        std::array<std::byte, 8U> bytes { };
        for (std::size_t index = 0U; index < bytes.size(); ++index) {
            const auto shift = static_cast<unsigned>(
                (bytes.size() - index - 1U) * 8U);
            bytes[index] = static_cast<std::byte>((value >> shift) & 0xffU);
        }
        hash.update(bytes);
    }

    void update_string(support::Sha256& hash, const std::string_view value) noexcept
    {
        update_u64(hash, value.size());
        hash.update(value);
    }

    std::uint64_t digest_word(
        const support::Sha256::Digest& digest, const std::size_t first) noexcept
    {
        std::uint64_t value { };
        for (std::size_t index = first; index < first + 8U; ++index) {
            value = (value << 8U) | digest[index];
        }
        return value;
    }

    CodeCoveragePointId bin_identity(const CodeCoveragePointId instance,
        const CodeCoveragePointId object, const CodeCoveragePointId first,
        const std::optional<CodeCoveragePointId> second,
        const std::string_view role) noexcept
    {
        support::Sha256 hash;
        update_string(hash, kCoverageFsmRuntimeSchema);
        update_string(hash, role);
        update_u64(hash, instance.high);
        update_u64(hash, instance.low);
        update_u64(hash, object.high);
        update_u64(hash, object.low);
        update_u64(hash, first.high);
        update_u64(hash, first.low);
        if (second) {
            update_u64(hash, second->high);
            update_u64(hash, second->low);
        }
        const auto digest = hash.finish();
        return { digest_word(digest, 0U), digest_word(digest, 8U) };
    }

    bool valid_saturation(const std::uint64_t hits, const bool overflow) noexcept
    {
        return !overflow || hits == std::numeric_limits<std::uint64_t>::max();
    }

    void increment(std::uint64_t& hits, bool& overflow,
        std::size_t& saturated_updates) noexcept
    {
        if (hits == std::numeric_limits<std::uint64_t>::max()) {
            if (!overflow) {
                overflow = true;
                ++saturated_updates;
            }
            return;
        }
        ++hits;
    }

    struct ValidationResult {
        Error error { Error::None };
        std::size_t index { };
    };

    ValidationResult validate_model(const CoverageFsmRuntimeModel& model,
        const CoverageFsmRuntimeLimits limits)
    {
        if (model.machines.size() > limits.maximum_machines
            || model.state_visits.size() > limits.maximum_states
            || model.legal_transitions.size()
                > limits.maximum_legal_transitions) {
            return { Error::ResourceLimit, 0U };
        }
        if (!is_code_coverage_identity_valid(model.instance_identity)) {
            return { Error::InvalidInstanceIdentity, 0U };
        }
        if (model.instance.empty() || model.instance.find('\0') != std::string::npos) {
            return { Error::InvalidInstance, 0U };
        }
        if (model.instance.size() > limits.maximum_instance_bytes) {
            return { Error::ResourceLimit, 0U };
        }

        std::set<IdKey> objects;
        std::set<IdKey> states;
        std::map<IdKey, std::set<IdKey>> states_by_object;
        IdKey previous_object { };
        bool first_object = true;
        std::size_t state_count = 0U;
        for (std::size_t index = 0U; index < model.machines.size(); ++index) {
            const auto& machine = model.machines[index];
            const auto object = key(machine.current_state_object);
            if (!is_code_coverage_identity_valid(machine.current_state_object)) {
                return { Error::InvalidCurrentStateObject, index };
            }
            if (!objects.emplace(object).second) {
                return { Error::DuplicateCurrentStateObject, index };
            }
            if (!first_object && object <= previous_object) {
                return { Error::InvalidCanonicalOrder, index };
            }
            first_object = false;
            previous_object = object;
            if (machine.states.size() < 2U
                || machine.states.size() > limits.maximum_states - state_count) {
                return { Error::ResourceLimit, index };
            }
            state_count += machine.states.size();
            IdKey previous_state { };
            bool first_state = true;
            auto& owned_states = states_by_object[object];
            for (const auto state : machine.states) {
                const auto state_key = key(state);
                if (!is_code_coverage_identity_valid(state)) {
                    return { Error::InvalidState, index };
                }
                if (!states.emplace(state_key).second
                    || !owned_states.emplace(state_key).second) {
                    return { Error::DuplicateState, index };
                }
                if (!first_state && state_key <= previous_state) {
                    return { Error::InvalidCanonicalOrder, index };
                }
                first_state = false;
                previous_state = state_key;
            }
            if (machine.previous_state
                && !owned_states.contains(key(*machine.previous_state))) {
                return { Error::InvalidPreviousState, index };
            }
            if (!valid_saturation(machine.illegal_transition_observations,
                    machine.illegal_transition_overflow)) {
                return { Error::InvalidSaturationState, index };
            }
        }
        if (state_count != model.state_visits.size()) {
            return { Error::InvalidState, state_count };
        }

        std::set<VisitKey> visits;
        std::set<IdKey> bin_ids;
        VisitKey previous_visit { };
        bool first_visit = true;
        for (std::size_t index = 0U; index < model.state_visits.size(); ++index) {
            const auto& visit = model.state_visits[index];
            const auto visit_owner = visit_key(
                visit.current_state_object, visit.state);
            const auto machine = states_by_object.find(
                key(visit.current_state_object));
            if (machine == states_by_object.end()
                || !machine->second.contains(key(visit.state))) {
                return { Error::InvalidState, index };
            }
            if (!visits.emplace(visit_owner).second
                || (!first_visit && visit_owner <= previous_visit)) {
                return { Error::InvalidCanonicalOrder, index };
            }
            first_visit = false;
            previous_visit = visit_owner;
            const auto expected = bin_identity(model.instance_identity,
                visit.current_state_object, visit.state, std::nullopt,
                "state-visit");
            if (visit.id != expected || !is_code_coverage_identity_valid(visit.id)
                || !bin_ids.emplace(key(visit.id)).second) {
                return { Error::DuplicateBinIdentity, index };
            }
            if (!valid_saturation(visit.hits, visit.overflow)) {
                return { Error::InvalidSaturationState, index };
            }
        }

        std::set<TransitionKey> transitions;
        TransitionKey previous_transition { };
        bool first_transition = true;
        for (std::size_t index = 0U;
            index < model.legal_transitions.size(); ++index) {
            const auto& transition = model.legal_transitions[index];
            const auto owner = transition_key(transition.current_state_object,
                transition.from_state, transition.to_state);
            const auto machine = states_by_object.find(
                key(transition.current_state_object));
            if (machine == states_by_object.end()
                || !machine->second.contains(key(transition.from_state))
                || !machine->second.contains(key(transition.to_state))) {
                return { Error::InvalidLegalTransition, index };
            }
            if (!transitions.emplace(owner).second
                || (!first_transition && owner <= previous_transition)) {
                return { Error::InvalidCanonicalOrder, index };
            }
            first_transition = false;
            previous_transition = owner;
            const auto expected = bin_identity(model.instance_identity,
                transition.current_state_object, transition.from_state,
                transition.to_state, "legal-transition");
            if (transition.id != expected
                || !is_code_coverage_identity_valid(transition.id)
                || !bin_ids.emplace(key(transition.id)).second) {
                return { Error::DuplicateBinIdentity, index };
            }
            if (!valid_saturation(transition.hits, transition.overflow)) {
                return { Error::InvalidSaturationState, index };
            }
        }
        return { };
    }

} // namespace

CoverageFsmBuildResult make_coverage_fsm_runtime_model(
    const CoverageFsmDefinition& definition,
    const CoverageFsmRuntimeLimits limits) noexcept
{
    CoverageFsmBuildResult result;
    const auto reject = [&](const Error error, const std::size_t index = 0U) {
        result.model.reset();
        result.error = error;
        result.index = index;
        return result;
    };
    try {
        if (definition.machines.size() > limits.maximum_machines) {
            return reject(Error::ResourceLimit);
        }
        if (!is_code_coverage_identity_valid(definition.instance_identity)) {
            return reject(Error::InvalidInstanceIdentity);
        }
        if (definition.instance.empty()
            || definition.instance.find('\0') != std::string::npos) {
            return reject(Error::InvalidInstance);
        }
        if (definition.instance.size() > limits.maximum_instance_bytes) {
            return reject(Error::ResourceLimit);
        }

        CoverageFsmRuntimeModel model;
        model.instance_identity = definition.instance_identity;
        model.specialization = definition.specialization;
        model.instance = definition.instance;
        std::set<IdKey> objects;
        std::set<IdKey> states;
        std::set<IdKey> bins;
        std::size_t total_states = 0U;
        std::size_t total_transitions = 0U;
        for (std::size_t index = 0U;
            index < definition.machines.size(); ++index) {
            const auto& input = definition.machines[index];
            if (!is_code_coverage_identity_valid(input.current_state_object)) {
                return reject(Error::InvalidCurrentStateObject, index);
            }
            if (!objects.emplace(key(input.current_state_object)).second) {
                return reject(Error::DuplicateCurrentStateObject, index);
            }
            if (input.states.size() < 2U
                || input.states.size() > limits.maximum_states - total_states
                || input.legal_transitions.size()
                    > limits.maximum_legal_transitions - total_transitions) {
                return reject(Error::ResourceLimit, index);
            }
            total_states += input.states.size();
            total_transitions += input.legal_transitions.size();
            CoverageFsmMachineRuntimeState machine;
            machine.current_state_object = input.current_state_object;
            std::set<IdKey> owned_states;
            for (const auto state : input.states) {
                if (!is_code_coverage_identity_valid(state)) {
                    return reject(Error::InvalidState, index);
                }
                if (!states.emplace(key(state)).second
                    || !owned_states.emplace(key(state)).second) {
                    return reject(Error::DuplicateState, index);
                }
                machine.states.push_back(state);
                CoverageFsmVisitBin visit;
                visit.current_state_object = input.current_state_object;
                visit.state = state;
                visit.id = bin_identity(definition.instance_identity,
                    input.current_state_object, state, std::nullopt,
                    "state-visit");
                if (!is_code_coverage_identity_valid(visit.id)
                    || !bins.emplace(key(visit.id)).second) {
                    return reject(Error::DuplicateBinIdentity, index);
                }
                model.state_visits.push_back(std::move(visit));
            }
            std::ranges::sort(machine.states, { }, [](const auto state) {
                return key(state);
            });
            std::set<std::pair<IdKey, IdKey>> legal;
            for (const auto& transition : input.legal_transitions) {
                if (!owned_states.contains(key(transition.from_state))
                    || !owned_states.contains(key(transition.to_state))) {
                    return reject(Error::InvalidLegalTransition, index);
                }
                if (!legal.emplace(
                              key(transition.from_state), key(transition.to_state))
                        .second) {
                    return reject(Error::DuplicateLegalTransition, index);
                }
                CoverageFsmTransitionBin bin;
                bin.current_state_object = input.current_state_object;
                bin.from_state = transition.from_state;
                bin.to_state = transition.to_state;
                bin.id = bin_identity(definition.instance_identity,
                    input.current_state_object, transition.from_state,
                    transition.to_state, "legal-transition");
                if (!is_code_coverage_identity_valid(bin.id)
                    || !bins.emplace(key(bin.id)).second) {
                    return reject(Error::DuplicateBinIdentity, index);
                }
                model.legal_transitions.push_back(std::move(bin));
            }
            model.machines.push_back(std::move(machine));
        }
        std::ranges::sort(model.machines, { }, [](const auto& machine) {
            return key(machine.current_state_object);
        });
        std::ranges::sort(model.state_visits, { }, [](const auto& visit) {
            return visit_key(visit.current_state_object, visit.state);
        });
        std::ranges::sort(model.legal_transitions, { }, [](const auto& bin) {
            return transition_key(
                bin.current_state_object, bin.from_state, bin.to_state);
        });
        const auto validation = validate_model(model, limits);
        if (validation.error != Error::None) {
            return reject(validation.error, validation.index);
        }
        result.model = std::move(model);
        result.error = Error::None;
        result.index = 0U;
        return result;
    } catch (const std::bad_alloc&) {
        return reject(Error::ResourceLimit);
    } catch (const std::length_error&) {
        return reject(Error::ResourceLimit);
    }
}

CoverageFsmRecordResult record_coverage_fsm_observations(
    CoverageFsmRuntimeModel& model,
    const std::span<const CoverageFsmObservation> observations,
    const CoverageFsmRuntimeLimits limits) noexcept
{
    CoverageFsmRecordResult result;
    const auto reject = [&](const Error error, const std::size_t index = 0U) {
        result.error = error;
        result.index = index;
        result.illegal_transitions = 0U;
        result.saturated_updates = 0U;
        return result;
    };
    try {
        if (observations.size() > limits.maximum_observations) {
            return reject(Error::ResourceLimit);
        }
        const auto validation = validate_model(model, limits);
        if (validation.error != Error::None) {
            return reject(validation.error, validation.index);
        }

        std::map<IdKey, std::size_t> machines;
        std::map<VisitKey, std::size_t> visits;
        std::map<TransitionKey, std::size_t> transitions;
        for (std::size_t index = 0U; index < model.machines.size(); ++index) {
            machines.emplace(key(model.machines[index].current_state_object), index);
        }
        for (std::size_t index = 0U; index < model.state_visits.size(); ++index) {
            const auto& visit = model.state_visits[index];
            visits.emplace(visit_key(visit.current_state_object, visit.state), index);
        }
        for (std::size_t index = 0U;
            index < model.legal_transitions.size(); ++index) {
            const auto& transition = model.legal_transitions[index];
            transitions.emplace(transition_key(transition.current_state_object,
                                    transition.from_state, transition.to_state),
                index);
        }
        for (std::size_t index = 0U; index < observations.size(); ++index) {
            const auto& observation = observations[index];
            if (!is_code_coverage_identity_valid(
                    observation.current_state_object)
                || !is_code_coverage_identity_valid(observation.state)
                || !machines.contains(key(observation.current_state_object))
                || !visits.contains(visit_key(
                    observation.current_state_object, observation.state))) {
                return reject(Error::ObservationOwnershipMismatch, index);
            }
        }

        for (const auto& observation : observations) {
            auto& visit = model.state_visits[visits.at(visit_key(
                observation.current_state_object, observation.state))];
            increment(visit.hits, visit.overflow, result.saturated_updates);
            auto& machine = model.machines[machines.at(key(observation.current_state_object))];
            if (machine.previous_state) {
                const auto transition = transitions.find(transition_key(
                    observation.current_state_object,
                    *machine.previous_state, observation.state));
                if (transition == transitions.end()) {
                    ++result.illegal_transitions;
                    increment(machine.illegal_transition_observations,
                        machine.illegal_transition_overflow,
                        result.saturated_updates);
                } else {
                    auto& bin = model.legal_transitions[transition->second];
                    increment(bin.hits, bin.overflow,
                        result.saturated_updates);
                }
            }
            machine.previous_state = observation.state;
        }
        return result;
    } catch (...) {
        return reject(Error::ResourceLimit);
    }
}

CoverageFsmSummaryResult summarize_coverage_fsm(
    const CoverageFsmRuntimeModel& model,
    const CoverageFsmRuntimeLimits limits) noexcept
{
    CoverageFsmSummaryResult result;
    try {
        const auto validation = validate_model(model, limits);
        if (validation.error != Error::None) {
            result.error = validation.error;
            result.index = validation.index;
            return result;
        }
        CoverageFsmSummary summary;
        summary.state_visits.total = model.state_visits.size();
        summary.legal_transitions.total = model.legal_transitions.size();
        summary.state_visits.covered = static_cast<std::uint64_t>(
            std::ranges::count_if(model.state_visits,
                [](const auto& bin) { return bin.hits != 0U; }));
        summary.legal_transitions.covered = static_cast<std::uint64_t>(
            std::ranges::count_if(model.legal_transitions,
                [](const auto& bin) { return bin.hits != 0U; }));
        result.summary = summary;
        return result;
    } catch (...) {
        result.summary.reset();
        result.error = Error::ResourceLimit;
        result.index = 0U;
        return result;
    }
}

} // namespace fsim::runtime
