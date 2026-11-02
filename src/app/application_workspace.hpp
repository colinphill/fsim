// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/cli/driver.hpp"

#include <string_view>

namespace fsim::app {

[[nodiscard]] std::optional<BuiltProject> load_workspace_snapshot(
    const cli::Invocation&, const project::Config&, diagnostic::Engine&);

// The metadata of a workspace snapshot: what it was elaborated with.
[[nodiscard]] std::optional<artifact::DesignMetadata> read_snapshot_metadata(
    const project::Config&, std::string_view snapshot, diagnostic::Engine&);

// Warns (FSIM-TCL-CFG-003) when the configuration's elaboration settings
// differ from those `snapshot` was elaborated with. Null: no scripts ran.
void warn_changed_snapshot_settings(const project::Configuration*,
    std::string_view snapshot, const artifact::DesignMetadata&,
    diagnostic::Engine&);

namespace application_detail {

int handle_workspace_compile(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_workspace_elaborate(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_workspace_simulate(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_workspace_simulate_stdio(const cli::Invocation&,
    const project::Config&, diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_workspace_library(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_workspace_snapshot_info(const cli::Invocation&,
    const project::Config&, diagnostic::Engine&, std::ostream&, std::ostream&);

// Publish a single selected compiled object using the checked source receipts.
// With `roots`, the object records only those checked sources (and their
// includes): the compilation unit its design units were parsed in.
bool publish_workspace_object(const project::Config&, const CheckedProject&,
    semantic::CompiledDesign, const std::filesystem::path&,
    diagnostic::Engine&,
    const std::vector<std::filesystem::path>* roots = nullptr);

} // namespace application_detail
} // namespace fsim::app
