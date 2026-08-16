// SPDX-License-Identifier: Apache-2.0

#include "fsim/systemc/scv_artifact.hpp"

#include "fsim/systemc/scv.hpp"

#include <string>

namespace fsim::systemc {

bool validate_scv_artifact_compatibility(
    const std::string_view candidate,
    const std::string_view artifact_kind,
    diagnostic::Engine& diagnostics)
{
    if (candidate.size() > scv_artifact_identity_limit) {
        diagnostics.error(
            "FSIM-SCV-A001",
            std::string { artifact_kind }
                + " SCV compatibility identity exceeds the 4096-byte limit");
        return false;
    }
    const std::string identity { candidate };
    if (fsim_scv_accepts_compatibility_identity(identity.c_str())) {
        return true;
    }
    diagnostics.error(
        "FSIM-SCV-A001",
        std::string { fsim_scv_compatibility_diagnostic(identity.c_str()) }
            + " in " + std::string { artifact_kind });
    return false;
}

} // namespace fsim::systemc
