// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_workspace_internal.hpp"

namespace fsim::app::application_detail {

struct WorkspaceDesignSelection {
    std::vector<WorkspaceObjectSelection> objects;
    std::vector<std::filesystem::path> plugins;
    WorkspaceCatalogs catalogs;
};

// Tops have already been resolved to qualified catalog names. Validate the
// returned catalog dependencies before linking its object/plugin selections.
[[nodiscard]] std::optional<WorkspaceDesignSelection> select_workspace_design(
    const project::Config&, const workspace::Store&, const WorkspaceCatalogs&,
    diagnostic::Engine&);

} // namespace fsim::app::application_detail
