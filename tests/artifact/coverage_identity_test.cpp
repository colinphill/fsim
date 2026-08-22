// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_identity.hpp"

#include <cassert>
#include <string_view>

int main()
{
    using namespace fsim::artifact;
    static_assert(kCodeCoverageArtifactSchema == 3U);
    static_assert(kCodeCoverageArtifactDiagnostic == "FSIM-COV-014");

    const auto disabled = make_code_coverage_artifact_identity(false);
    const auto enabled = make_code_coverage_artifact_identity(true);
    assert(disabled.ok() && enabled.ok());
    assert(!disabled.identity.enabled
        && disabled.identity.model == kCodeCoverageDisabledModel);
    assert(enabled.identity.enabled
        && enabled.identity.model == kCodeCoverageBroadMetricsModel);
    assert(disabled.identity.digest.size() == 64U);
    assert(enabled.identity.digest.size() == 64U);
    assert(disabled.identity.digest != enabled.identity.digest);
    assert(validate_code_coverage_artifact_identity(disabled.identity)
        == CodeCoverageArtifactIdentityError::None);
    assert(validate_code_coverage_artifact_identity(enabled.identity)
        == CodeCoverageArtifactIdentityError::None);
    assert(make_code_coverage_artifact_identity(true).identity == enabled.identity);

    auto invalid = enabled.identity;
    invalid.schema = 2U;
    assert(validate_code_coverage_artifact_identity(invalid)
        == CodeCoverageArtifactIdentityError::SchemaMismatch);
    invalid = enabled.identity;
    invalid.model = std::string { kCodeCoverageFoundationModel };
    assert(validate_code_coverage_artifact_identity(invalid)
        == CodeCoverageArtifactIdentityError::ModelMismatch);
    invalid = enabled.identity;
    invalid.model = std::string { kCodeCoverageDisabledModel };
    assert(validate_code_coverage_artifact_identity(invalid)
        == CodeCoverageArtifactIdentityError::ModelMismatch);
    invalid = enabled.identity;
    invalid.digest.front() = invalid.digest.front() == '0' ? '1' : '0';
    assert(validate_code_coverage_artifact_identity(invalid)
        == CodeCoverageArtifactIdentityError::DigestMismatch);
}
