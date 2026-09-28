// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_internal.hpp"

#include <map>

namespace fsim::app::application_detail {

struct WorkspaceObjectSelection {
    std::filesystem::path path;
    std::vector<library::UnitIndexEntry> active_units;
    // Present only for managed catalog selections. Manual object spans retain
    // their caller-specified order.
    std::optional<std::size_t> catalog_rank { };
};

struct WorkspaceCompiledObject {
    std::string source;
    std::vector<semantic::UnitId> units;
    std::vector<std::string> classes;
    std::vector<semantic::CompiledUdpIdentity> primitives;
    std::vector<std::string> sources { };
    std::map<std::string, std::string> unit_sources { };
};

[[nodiscard]] std::optional<std::vector<WorkspaceCompiledObject>>
partition_workspace_objects(const CompilationWorkspace&, const project::Config&,
    diagnostic::Engine&);

[[nodiscard]] std::vector<std::pair<semantic::UnitId, library::UnitIndexEntry>>
compiled_object_unit_entries(const semantic::CompiledDesign&,
    std::string_view library);

[[nodiscard]] std::optional<CompilationWorkspace> load_workspace_objects(
    std::span<const WorkspaceObjectSelection> objects,
    diagnostic::Engine& diagnostics,
    bool validate_uvm_surface = true);

[[nodiscard]] std::optional<BuiltProject> build_workspace_objects(
    const project::Config& config,
    std::span<const WorkspaceObjectSelection> objects,
    std::span<const std::filesystem::path> systemc_plugins,
    diagnostic::Engine& diagnostics);

} // namespace fsim::app::application_detail
