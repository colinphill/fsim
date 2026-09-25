// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"

namespace fsim::app {

[[nodiscard]] std::optional<BuiltProject> load_workspace_snapshot(
    const cli::Invocation&, const project::Config&, diagnostic::Engine&);

namespace application_detail {

int handle_workspace_compile(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_workspace_elaborate(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_workspace_simulate(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_workspace_library(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);

// Publish a single selected compiled object using the checked source receipts.
bool publish_workspace_object(const project::Config&, const CheckedProject&,
    semantic::CompiledDesign, const std::filesystem::path&,
    diagnostic::Engine&);

} // namespace application_detail
} // namespace fsim::app
