// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_instance_identity.hpp"
#include "fsim/runtime/code_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageFsmValidationSchema
    = "fsim-coverage-fsm-validation-v3";
inline constexpr std::string_view kCoverageFsmAmbiguousDiagnostic
    = "FSIM-COV-028";
inline constexpr std::string_view kCoverageFsmIncompleteDiagnostic
    = "FSIM-COV-029";
inline constexpr std::string_view kCoverageFsmConflictingDiagnostic
    = "FSIM-COV-030";

enum class CoverageFsmDescriptionIssueKind : std::uint8_t {
    Ambiguous = 1U,
    Incomplete = 2U,
    Conflicting = 3U,
};

enum class CoverageFsmDescriptionSubject : std::uint8_t {
    CurrentState = 1U,
    NextState = 2U,
    LegalStates = 3U,
};

enum class CoverageFsmDescriptionOrigin : std::uint8_t {
    Enum = 1U,
    Case = 2U,
    Assignment = 3U,
    SystemVerilogPragma = 4U,
    VhdlSource = 5U,
    Manifest = 6U,
};

struct CoverageFsmDescriptionIssueCandidate {
    CoverageFsmDescriptionIssueKind kind {
        CoverageFsmDescriptionIssueKind::Ambiguous
    };
    CoverageFsmDescriptionSubject subject {
        CoverageFsmDescriptionSubject::CurrentState
    };
    CoverageInstanceIdentity instance_identity;
    std::string instance;
    std::string object;
    std::vector<CoverageFsmDescriptionOrigin> origins;
};

struct CoverageFsmDescriptionDiagnostic {
    runtime::CodeCoveragePointId id;
    CoverageFsmDescriptionIssueKind kind {
        CoverageFsmDescriptionIssueKind::Ambiguous
    };
    CoverageFsmDescriptionSubject subject {
        CoverageFsmDescriptionSubject::CurrentState
    };
    CoverageInstanceIdentity instance_identity;
    std::string instance;
    std::string object;
    std::vector<CoverageFsmDescriptionOrigin> origins;

    friend bool operator==(const CoverageFsmDescriptionDiagnostic&,
        const CoverageFsmDescriptionDiagnostic&)
        = default;
};

struct CoverageFsmDescriptionValidationLimits {
    std::size_t maximum_candidates { 1U << 20U };
    std::size_t maximum_diagnostics { 1U << 20U };
    std::size_t maximum_origins { 6U };
    std::size_t maximum_instance_bytes { 1U << 20U };
    std::size_t maximum_object_bytes { 1U << 16U };
};

enum class CoverageFsmDescriptionValidationError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidKind,
    InvalidSubject,
    InvalidInstanceIdentity,
    InvalidInstance,
    InvalidObject,
    InvalidOrigin,
    MissingOrigin,
    DuplicateDiagnosticIdentity,
};

struct CoverageFsmDescriptionValidationResult {
    std::optional<std::vector<CoverageFsmDescriptionDiagnostic>> diagnostics;
    CoverageFsmDescriptionValidationError error {
        CoverageFsmDescriptionValidationError::None
    };
    std::size_t candidate_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return diagnostics.has_value()
            && error == CoverageFsmDescriptionValidationError::None;
    }
};

[[nodiscard]] constexpr std::string_view
coverage_fsm_description_diagnostic_code(
    const CoverageFsmDescriptionIssueKind kind) noexcept
{
    switch (kind) {
    case CoverageFsmDescriptionIssueKind::Ambiguous:
        return kCoverageFsmAmbiguousDiagnostic;
    case CoverageFsmDescriptionIssueKind::Incomplete:
        return kCoverageFsmIncompleteDiagnostic;
    case CoverageFsmDescriptionIssueKind::Conflicting:
        return kCoverageFsmConflictingDiagnostic;
    }
    return { };
}

// Equivalent candidates are coalesced by instance, object, kind, and subject;
// their origins are unioned. The complete input is validated before any
// diagnostics are returned.
[[nodiscard]] CoverageFsmDescriptionValidationResult
make_coverage_fsm_description_diagnostics(
    std::span<const CoverageFsmDescriptionIssueCandidate> candidates,
    CoverageFsmDescriptionValidationLimits limits = { }) noexcept;

} // namespace fsim::elaboration
