// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace fsim::artifact {

inline constexpr std::uint32_t kCodeCoverageArtifactSchema = 3U;
inline constexpr std::string_view kCodeCoverageArtifactDiagnostic
    = "FSIM-COV-014";
inline constexpr std::string_view kCodeCoverageDisabledModel = "none";
inline constexpr std::string_view kCodeCoverageFoundationModel
    = "fsim-code-coverage-foundation-v3";

struct CodeCoverageArtifactIdentity {
    std::uint32_t schema { kCodeCoverageArtifactSchema };
    bool enabled { };
    std::string model { kCodeCoverageDisabledModel };
    std::string digest;

    friend bool operator==(
        const CodeCoverageArtifactIdentity&,
        const CodeCoverageArtifactIdentity&) = default;
};

enum class CodeCoverageArtifactIdentityError : std::uint8_t {
    None,
    SchemaMismatch,
    ModelMismatch,
    DigestMismatch,
    AllocationFailure,
};

struct CodeCoverageArtifactIdentityResult {
    CodeCoverageArtifactIdentity identity;
    CodeCoverageArtifactIdentityError error {
        CodeCoverageArtifactIdentityError::None
    };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == CodeCoverageArtifactIdentityError::None;
    }
};

[[nodiscard]] CodeCoverageArtifactIdentityResult
make_code_coverage_artifact_identity(bool enabled) noexcept;

[[nodiscard]] CodeCoverageArtifactIdentityError
validate_code_coverage_artifact_identity(
    const CodeCoverageArtifactIdentity& identity) noexcept;

} // namespace fsim::artifact
