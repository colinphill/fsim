// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_fsm_hints.hpp"
#include "fsim/elaboration/coverage_fsm_validation.hpp"
#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/elaboration/verilog_coverage_points.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageFsmInferenceSchema
    = "fsim-coverage-fsm-inference-v3";
inline constexpr std::string_view kCoverageFsmInferenceDiagnostic
    = "FSIM-COV-025";

struct CoverageFsmState {
    runtime::CodeCoveragePointId id;
    std::string name;
    std::size_t ordinal { };

    friend bool operator==(const CoverageFsmState&, const CoverageFsmState&)
        = default;
};

struct CoverageFsmCurrentStateObject {
    runtime::CodeCoveragePointId source_point;
    runtime::CodeCoveragePointId point;
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    frontend::CodeCoverageLanguage language {
        frontend::CodeCoverageLanguage::SystemVerilog
    };
    std::string hierarchy_path;
    bool enum_evidence { };
    bool case_evidence { };
    bool pragma_evidence { };
    bool vhdl_source_hint_evidence { };
    bool manifest_hint_evidence { };
    std::size_t source_index { };
    frontend::CodeCoverageSourceSpan span;
    std::uint64_t line { };
    std::vector<CoverageFsmState> states;

    friend bool operator==(const CoverageFsmCurrentStateObject&,
        const CoverageFsmCurrentStateObject&)
        = default;
};

struct CoverageFsmNextStateObject {
    runtime::CodeCoveragePointId source_point;
    runtime::CodeCoveragePointId point;
    runtime::CodeCoveragePointId current_state_object;
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    frontend::CodeCoverageLanguage language {
        frontend::CodeCoverageLanguage::SystemVerilog
    };
    std::string hierarchy_path;
    std::size_t source_index { };
    frontend::CodeCoverageSourceSpan span;
    std::uint64_t line { };
    bool assignment_evidence { };
    bool pragma_evidence { };
    bool vhdl_source_hint_evidence { };
    bool manifest_hint_evidence { };

    friend bool operator==(const CoverageFsmNextStateObject&,
        const CoverageFsmNextStateObject&)
        = default;
};

struct CoverageFsmLegalStateSet {
    runtime::CodeCoveragePointId point;
    runtime::CodeCoveragePointId current_state_object;
    bool enum_evidence { };
    bool case_evidence { };
    bool pragma_evidence { };
    bool vhdl_source_hint_evidence { };
    bool manifest_hint_evidence { };
    std::vector<runtime::CodeCoveragePointId> states;

    friend bool operator==(const CoverageFsmLegalStateSet&,
        const CoverageFsmLegalStateSet&)
        = default;
};

struct CoverageFsmInference {
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    std::string instance;
    std::vector<CoverageFsmCurrentStateObject> current_state_objects;
    std::vector<CoverageFsmNextStateObject> next_state_objects;
    std::vector<CoverageFsmLegalStateSet> legal_state_sets;
    std::vector<CoverageFsmDescriptionDiagnostic> description_diagnostics;

    friend bool operator==(
        const CoverageFsmInference&, const CoverageFsmInference&)
        = default;
};

struct CoverageFsmInferenceLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_objects { 1U << 20U };
    std::size_t maximum_cases { 1U << 20U };
    std::size_t maximum_case_choices { 1U << 22U };
    std::size_t maximum_assignments { 1U << 22U };
    std::size_t maximum_pragmas { 1U << 16U };
    std::size_t maximum_pragma_specifications { 1U << 18U };
    std::size_t maximum_pragma_value_bytes { 1U << 20U };
    std::size_t maximum_hints { 1U << 18U };
    std::size_t maximum_hint_legal_states { 1U << 20U };
    std::size_t maximum_hint_name_bytes { 1U << 16U };
    std::size_t maximum_hint_instance_bytes { 1U << 20U };
    std::size_t maximum_states { 1U << 22U };
    std::size_t maximum_next_state_objects { 1U << 20U };
    std::size_t maximum_legal_state_sets { 1U << 20U };
    std::size_t maximum_state_name_bytes { 1U << 16U };
    std::size_t maximum_hierarchy_bytes { 1U << 20U };
    std::size_t maximum_statement_depth { 1U << 12U };
    std::uint64_t maximum_line_number { 1ULL << 31U };
    CoverageInstanceIdentityLimits instance_identity;
    CoverageFsmDescriptionValidationLimits description_validation;
};

enum class CoverageFsmInferenceError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidLanguage,
    InvalidStandard,
    InvalidUnitKind,
    InstanceOwnerMismatch,
    InvalidInstanceIdentity,
    EmptySourceName,
    DuplicateSourceName,
    DuplicateSourceIdentity,
    InvalidSourceIdentity,
    EmptyObjectName,
    InvalidObjectName,
    UnknownObjectSource,
    ObjectSourceOwnershipMismatch,
    InvalidObjectSpan,
    InvalidObjectLine,
    InvalidStateName,
    DuplicateObjectPath,
    DuplicateObjectIdentity,
    DuplicateStateIdentity,
    DuplicateNextStatePath,
    DuplicateNextStateIdentity,
    DuplicateLegalStateSetIdentity,
    InvalidSystemVerilogPragma,
    InvalidFsmHint,
    InvalidDescriptionDiagnostic,
};

struct CoverageFsmInferenceResult {
    std::optional<CoverageFsmInference> inference;
    CoverageFsmInferenceError error { CoverageFsmInferenceError::None };
    std::size_t object_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return inference.has_value()
            && error == CoverageFsmInferenceError::None;
    }
};

// `ports` is the already resolved specialized entity interface for VHDL and
// normally unit.ports for Verilog/SystemVerilog. Next-state objects require a
// direct compatible retained-object assignment, an exact SystemVerilog FSM
// description pragma, or a validated VHDL/manifest hint; legal sets reference
// only already inferred states. Ambiguous, incomplete, and conflicting
// descriptions retain stable detailed diagnostics without displacing
// independently valid fallback inference.
[[nodiscard]] CoverageFsmInferenceResult make_coverage_fsm_inference(
    const frontend::DesignUnit& unit,
    std::span<const frontend::SignalDeclaration> ports,
    const CoverageInventoryOwner& owner,
    std::span<const VerilogCoverageSource> sources,
    std::span<const CoverageFsmHint> hints,
    CoverageFsmInferenceLimits limits = { }) noexcept;

[[nodiscard]] inline CoverageFsmInferenceResult make_coverage_fsm_inference(
    const frontend::DesignUnit& unit,
    const std::span<const frontend::SignalDeclaration> ports,
    const CoverageInventoryOwner& owner,
    const std::span<const VerilogCoverageSource> sources,
    const CoverageFsmInferenceLimits limits = { }) noexcept
{
    return make_coverage_fsm_inference(
        unit, ports, owner, sources, { }, limits);
}

} // namespace fsim::elaboration
