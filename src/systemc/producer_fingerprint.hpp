// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace fsim::systemc {

// Canonical producer ABI identity shared by incremental compilation and
// embedded-design admission. Per-invocation compilation inputs are excluded.
[[nodiscard]] std::optional<std::string> plugin_producer_fingerprint(
    const project::SystemCSection& settings,
    const std::filesystem::path& working_directory,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
