// SPDX-License-Identifier: Apache-2.0
#include "application_design_artifact_codec_validation.hpp"

#include <filesystem>

namespace fsim::app::codec_detail {

constexpr std::string_view kCode = "FSIM-ART-0013";

bool portable_semantics(
    const semantic::ModelRecords& records,
    diagnostic::Engine& diagnostics)
{
    for (const auto& file : records.source_files) {
        if (std::filesystem::path(file.physical_name).is_absolute()) {
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
