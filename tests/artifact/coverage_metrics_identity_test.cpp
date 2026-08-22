// SPDX-License-Identifier: Apache-2.0
#include "fsim/artifact/coverage_identity.hpp"

#include <cassert>
#include <string>

int main()
{
    using namespace fsim::artifact;

    const auto current = make_code_coverage_artifact_identity(true);
    assert(current.ok());
    assert(current.identity.schema == kCodeCoverageArtifactSchema);
    assert(current.identity.enabled);
    assert(current.identity.model == kCodeCoverageBroadMetricsModel);
    assert(current.identity.digest.size() == 64U);
    assert(make_code_coverage_artifact_identity(true).identity
        == current.identity);

    auto foundation = current.identity;
    foundation.model = std::string { kCodeCoverageFoundationModel };
    assert(validate_code_coverage_artifact_identity(foundation)
        == CodeCoverageArtifactIdentityError::ModelMismatch);

    const auto disabled = make_code_coverage_artifact_identity(false);
    assert(disabled.ok() && !disabled.identity.enabled);
    assert(disabled.identity.model == kCodeCoverageDisabledModel);
    assert(disabled.identity.digest != current.identity.digest);
}
