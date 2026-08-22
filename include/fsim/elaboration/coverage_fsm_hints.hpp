// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/design.hpp"
#include "fsim/project/project.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageFsmHintsSchema
    = "fsim-coverage-fsm-hints-v3";
inline constexpr std::string_view kCoverageFsmHintsDiagnostic
    = "FSIM-COV-026";

enum class CoverageFsmHintOrigin : std::uint8_t {
    VhdlSource,
    Manifest,
};

// One independently validated contribution to a language-neutral FSM
// description. VHDL source contributions have an empty instance and apply to
// every elaborated instance of the unit. Manifest contributions select one
// exact stable hierarchy instance.
struct CoverageFsmHint {
    CoverageFsmHintOrigin origin { CoverageFsmHintOrigin::VhdlSource };
    std::string instance;
    std::string current_state;
    bool marks_current_state { };
    std::optional<std::string> next_state;
    std::optional<std::vector<std::string>> legal_states;

    friend bool operator==(const CoverageFsmHint&, const CoverageFsmHint&)
        = default;
};

struct CoverageFsmHintLimits {
    std::size_t maximum_attributes { 1U << 18U };
    std::size_t maximum_attribute_entity_names { 1U << 18U };
    std::size_t maximum_manifest_hints { 1U << 16U };
    std::size_t maximum_hints { 1U << 18U };
    std::size_t maximum_legal_states { 1U << 20U };
    std::size_t maximum_name_bytes { 1U << 16U };
    std::size_t maximum_value_bytes { 1U << 20U };
    std::size_t maximum_instance_bytes { 1U << 20U };
};

enum class CoverageFsmHintError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidVhdlStandard,
    InvalidAttributeDeclaration,
    InvalidAttributeSpecification,
    InvalidManifestHint,
};

struct CoverageFsmHintResult {
    std::optional<std::vector<CoverageFsmHint>> hints;
    CoverageFsmHintError error { CoverageFsmHintError::None };
    std::size_t input_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return hints.has_value() && error == CoverageFsmHintError::None;
    }
};

// Recognized VHDL source attributes are string-typed declarations named
// fsm_current_state, fsm_next_state, and fsm_legal_states. Specifications
// apply to exact signal/variable names. The current marker is "true"; next is
// one retained object name; legal states are a comma-separated list.
// Manifest entries use [[coverage.fsm]] with exact instance/current_state,
// optional next_state, and optional legal_states fields.
[[nodiscard]] CoverageFsmHintResult make_coverage_fsm_hints(
    const frontend::DesignUnit& unit,
    std::span<const project::CoverageFsmHintEntry> manifest_hints = { },
    CoverageFsmHintLimits limits = { }) noexcept;

} // namespace fsim::elaboration
