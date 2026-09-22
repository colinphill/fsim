// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec_validation.hpp"
#include "fsim/support/path.hpp"

#include <filesystem>

namespace fsim::app::codec_detail {

constexpr std::string_view kCode = "FSIM-ART-0013";

bool portable_semantics(
    const semantic::ModelRecords& records,
    diagnostic::Engine& diagnostics)
{
    for (const auto& file : records.source_files) {
        if (support::path_is_portably_absolute(
                support::path_from_utf8(file.physical_name))) {
            diagnostics.error(
                std::string { kCode },
                "semantic state contains a producer-absolute source path: "
                    + file.physical_name);
            return false;
        }
    }
    return true;
}

} // namespace fsim::app::codec_detail
