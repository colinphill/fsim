// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/cli/driver.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace fsim::app {

int handle_tcl(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::istream& input,
    std::ostream& output,
    std::ostream& error);

namespace tcl_detail {

/// Explain a failed Tcl `cd` on Windows when the target is an existing
/// directory too long to be the working directory without long path support.
/// Returns the note naming the LongPathsEnabled registry fix, or nothing when
/// the limit does not explain the failure. Always nothing on other platforms.
[[nodiscard]] std::optional<std::string> windows_long_path_cd_note(
    std::string_view requested_directory, bool long_paths_enabled);

}  // namespace tcl_detail

}  // namespace fsim::app
