// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/code_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

inline constexpr std::string_view kCoverageFsmRuntimeSchema
    = "fsim-coverage-fsm-runtime-v3";
inline constexpr std::string_view kCoverageFsmRuntimeDiagnostic
    = "FSIM-COV-027";

struct CoverageFsmLegalTransitionDefinition {
    CodeCoveragePointId from_state;
    CodeCoveragePointId to_state;

    friend bool operator==(const CoverageFsmLegalTransitionDefinition&,
        const CoverageFsmLegalTransitionDefinition&) = default;
};

struct CoverageFsmMachineDefinition {
    CodeCoveragePointId current_state_object;
    std::vector<CodeCoveragePointId> states;
    std::vector<CoverageFsmLegalTransitionDefinition> legal_transitions;

    friend bool operator==(const CoverageFsmMachineDefinition&,
        const CoverageFsmMachineDefinition&) = default;
};

struct CoverageFsmDefinition {
    CodeCoveragePointId instance_identity;
    std::uint32_t specialization { };
    std::string instance;
    std::vector<CoverageFsmMachineDefinition> machines;
};

struct CoverageFsmVisitBin {
    CodeCoveragePointId id;
    CodeCoveragePointId current_state_object;
    CodeCoveragePointId state;
    std::uint64_t hits { };
    bool overflow { };

    friend bool operator==(const CoverageFsmVisitBin&,
        const CoverageFsmVisitBin&) = default;
};

struct CoverageFsmTransitionBin {
    CodeCoveragePointId id;
    CodeCoveragePointId current_state_object;
    CodeCoveragePointId from_state;
    CodeCoveragePointId to_state;
    std::uint64_t hits { };
    bool overflow { };

    friend bool operator==(const CoverageFsmTransitionBin&,
        const CoverageFsmTransitionBin&) = default;
};

struct CoverageFsmMachineRuntimeState {
    CodeCoveragePointId current_state_object;
    std::vector<CodeCoveragePointId> states;
    std::optional<CodeCoveragePointId> previous_state;
    std::uint64_t illegal_transition_observations { };
    bool illegal_transition_overflow { };

    friend bool operator==(const CoverageFsmMachineRuntimeState&,
        const CoverageFsmMachineRuntimeState&) = default;
};

struct CoverageFsmRuntimeModel {
    CodeCoveragePointId instance_identity;
    std::uint32_t specialization { };
    std::string instance;
    std::vector<CoverageFsmMachineRuntimeState> machines;
    std::vector<CoverageFsmVisitBin> state_visits;
    std::vector<CoverageFsmTransitionBin> legal_transitions;

    friend bool operator==(const CoverageFsmRuntimeModel&,
        const CoverageFsmRuntimeModel&) = default;
};

struct CoverageFsmObservation {
    CodeCoveragePointId current_state_object;
    CodeCoveragePointId state;
};

struct CoverageFsmRuntimeLimits {
    std::size_t maximum_machines { 1U << 20U };
    std::size_t maximum_states { 1U << 22U };
    std::size_t maximum_legal_transitions { 1U << 24U };
    std::size_t maximum_observations { 1U << 24U };
    std::size_t maximum_instance_bytes { 1U << 20U };
};

enum class CoverageFsmRuntimeError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidInstanceIdentity,
    InvalidInstance,
    InvalidCurrentStateObject,
    DuplicateCurrentStateObject,
    InvalidState,
    DuplicateState,
    InvalidLegalTransition,
    DuplicateLegalTransition,
    DuplicateBinIdentity,
    InvalidCanonicalOrder,
    InvalidSaturationState,
    InvalidPreviousState,
    ObservationOwnershipMismatch,
};

struct CoverageFsmBuildResult {
    std::optional<CoverageFsmRuntimeModel> model;
    CoverageFsmRuntimeError error { CoverageFsmRuntimeError::None };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return model.has_value() && error == CoverageFsmRuntimeError::None;
    }
};

struct CoverageFsmRecordResult {
    CoverageFsmRuntimeError error { CoverageFsmRuntimeError::None };
    std::size_t index { };
    std::size_t illegal_transitions { };
    std::size_t saturated_updates { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CoverageFsmRuntimeError::None;
    }
};

struct CoverageFsmMetricCoverage {
    std::uint64_t total { };
    std::uint64_t covered { };

    friend bool operator==(const CoverageFsmMetricCoverage&,
        const CoverageFsmMetricCoverage&) = default;
};

// Intentionally has no combined or synthetic FSM score. State visits and
// legal transitions are independent metric families.
struct CoverageFsmSummary {
    CoverageFsmMetricCoverage state_visits;
    CoverageFsmMetricCoverage legal_transitions;

    friend bool operator==(const CoverageFsmSummary&,
        const CoverageFsmSummary&) = default;
};

struct CoverageFsmSummaryResult {
    std::optional<CoverageFsmSummary> summary;
    CoverageFsmRuntimeError error { CoverageFsmRuntimeError::None };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return summary.has_value() && error == CoverageFsmRuntimeError::None;
    }
};

[[nodiscard]] CoverageFsmBuildResult make_coverage_fsm_runtime_model(
    const CoverageFsmDefinition& definition,
    CoverageFsmRuntimeLimits limits = { }) noexcept;

// Validates the complete model and observation batch before the first
// mutation. Every observation scores one state visit. After the first sample
// for a machine, a matching declared ordered pair scores one legal transition;
// an undeclared pair updates only the separate diagnostic count.
[[nodiscard]] CoverageFsmRecordResult record_coverage_fsm_observations(
    CoverageFsmRuntimeModel& model,
    std::span<const CoverageFsmObservation> observations,
    CoverageFsmRuntimeLimits limits = { }) noexcept;

[[nodiscard]] CoverageFsmSummaryResult summarize_coverage_fsm(
    const CoverageFsmRuntimeModel& model,
    CoverageFsmRuntimeLimits limits = { }) noexcept;

} // namespace fsim::runtime
