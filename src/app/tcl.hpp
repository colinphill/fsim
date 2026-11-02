// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/cli/driver.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/configuration.hpp"
#include "fsim/project/project.hpp"

#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::app {

int handle_tcl(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::istream& input,
    std::ostream& output,
    std::ostream& error);

// The configuration scripts a command in `workspace` runs, in order: the
// per-user script, then the workspace's `fsim.tcl`. Only existing files.
[[nodiscard]] std::vector<std::pair<project::ConfigLayer, std::filesystem::path>>
configuration_scripts(const std::filesystem::path& workspace);

// Runs the configuration scripts of the current workspace in a Tcl
// interpreter that has only the configuration commands. A build without
// Tcl fails when a script exists rather than ignoring it.
bool load_configuration_scripts(const cli::Invocation& invocation,
    project::Configuration& configuration, diagnostic::Engine& diagnostics);

// The same for the scripts of `workspace`, for a host that is not the fsim
// executable (an empty `program` lets Tcl find the running one).
bool load_configuration_scripts(const std::filesystem::path& workspace,
    const std::filesystem::path& program,
    project::Configuration& configuration, diagnostic::Engine& diagnostics);

namespace tcl_detail {

/// Explain a failed Tcl `cd` on Windows when the target is an existing
/// directory too long to be the working directory without long path support.
/// Returns the note naming the LongPathsEnabled registry fix, or nothing when
/// the limit does not explain the failure. Always nothing on other platforms.
[[nodiscard]] std::optional<std::string> windows_long_path_cd_note(
    std::string_view requested_directory, bool long_paths_enabled);

}  // namespace tcl_detail

}  // namespace fsim::app
