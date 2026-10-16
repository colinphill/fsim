// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_workspace.hpp"
#include "application_workspace_objects.hpp"
#include "application_workspace_store.hpp"

#include <span>

namespace fsim::app::application_detail {

using WorkspaceCatalogs = std::vector<workspace::LibraryCatalog>;

bool workspace_error(diagnostic::Engine&, std::string);
[[nodiscard]] std::optional<WorkspaceCatalogs> workspace_catalogs(
    const workspace::Store&, const workspace::LibraryCatalog* target,
    diagnostic::Engine&);
[[nodiscard]] std::vector<WorkspaceObjectSelection> workspace_object_selections(
    const workspace::Store&, const WorkspaceCatalogs&,
    std::string_view replacement_library, std::span<const std::string> replaced_sources,
    diagnostic::Engine&);
bool validate_workspace_dependencies(const WorkspaceCatalogs&, diagnostic::Engine&);
bool hydrate_workspace_artifact_vhdl_identifier_forms(
    const workspace::Store&, const workspace::LibraryLocation&,
    workspace::ArtifactRecord&, diagnostic::Engine&);
[[nodiscard]] std::optional<std::vector<workspace::UnitDependency>>
workspace_dependencies(const workspace::Store&, const semantic::CompiledDesign&,
    std::span<const semantic::UnitId>, const WorkspaceCatalogs&,
    diagnostic::Engine&);
bool resolve_workspace_tops(project::Config&, const workspace::Store&,
    WorkspaceCatalogs&, diagnostic::Engine&);

// Shared phase implementation preserves the AOT receipt path after managed
// object selection and transactional snapshot publication.
int elaborate_built_workspace(const cli::Invocation&, const project::Config&,
    std::optional<BuiltProject>, diagnostic::Engine&, std::ostream&);

} // namespace fsim::app::application_detail
