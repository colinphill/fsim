// SPDX-License-Identifier: Apache-2.0
//
// Loading of the precompiled VHDL standard libraries STD and IEEE (gate BD
// build D2): the managed libraries of an edition, built at fsim build time
// (src/tools/fsim_standard_library_builder.cpp), read as any mapped library
// is, but only the objects a design needs.
#include "application_internal.hpp"
#include "application_standard_library_artifact.hpp"
#include "application_workspace_internal.hpp"
#include "application_workspace_objects.hpp"
#include "application_workspace_store.hpp"

#include "fsim/artifact/object.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <set>
#include <utility>

namespace fsim::app::application_detail {
namespace {

// "library.unit" of a catalog unit's primary unit: a package body is owned
// under its package (IEEE 1076-2019 13.1: library units are identified by
// their logical library and name).
std::string primary_key(
    const std::string_view library, const library::UnitIndexEntry& unit)
{
    return support::ascii_lower(std::string { library }) + "."
        + support::ascii_lower(
            unit.primary_name.empty() ? unit.name : unit.primary_name);
}

} // namespace

std::optional<semantic::CompiledDesign> load_precompiled_standard_libraries(
    const frontend::VhdlStandard standard,
    const std::span<const std::string> packages,
    diagnostic::Engine& diagnostics)
{
    const auto root = checked_standard_library_root(standard, diagnostics);
    if (!root) {
        return std::nullopt;
    }
    const workspace::Store store(*root, workspace::StandardLibraryStore { });
    const auto catalogs = workspace_catalogs(store, nullptr, diagnostics);
    if (!catalogs) {
        return std::nullopt;
    }

    // The requested packages' objects and, through the catalogs' recorded
    // dependencies, those they depend on.
    std::set<std::string> wanted { packages.begin(), packages.end() };
    std::set<std::pair<std::size_t, std::size_t>> selected;
    for (bool grew = true; grew;) {
        grew = false;
        for (std::size_t rank = 0; rank < catalogs->size(); ++rank) {
            const auto& catalog = (*catalogs)[rank];
            for (std::size_t index = 0; index < catalog.artifacts.size(); ++index) {
                const auto& artifact = catalog.artifacts[index];
                if (artifact.kind != workspace::ArtifactKind::Hdl
                    || selected.contains({ rank, index })) {
                    continue;
                }
                const bool owns_wanted = std::ranges::any_of(
                    artifact.units, [&](const workspace::OwnedUnit& owned) {
                        return wanted.contains(
                            primary_key(catalog.location.name, owned.unit));
                    });
                if (!owns_wanted) {
                    continue;
                }
                selected.insert({ rank, index });
                grew = true;
                for (const auto& dependency : artifact.dependencies) {
                    wanted.insert(primary_key(dependency.library, dependency.unit));
                }
            }
        }
    }

    std::vector<WorkspaceObjectSelection> selections;
    for (const auto& [rank, index] : selected) {
        const auto& catalog = (*catalogs)[rank];
        const auto& artifact = catalog.artifacts[index];
        WorkspaceObjectSelection selection;
        for (const auto& owned : artifact.units) {
            selection.active_units.push_back(owned.unit);
        }
        std::string error;
        auto path = store.artifact_path(catalog.location, artifact, error);
        if (!path) {
            workspace_error(diagnostics, std::move(error));
            return std::nullopt;
        }
        selection.path = std::move(*path);
        selection.catalog_rank = rank;
        const auto metadata = artifact::load_object_metadata(selection.path, diagnostics);
        if (!metadata || metadata->compilation_digest != artifact.fingerprint) {
            if (!diagnostics.has_error()) {
                workspace_error(diagnostics,
                    "precompiled standard library object fingerprint differs from "
                    "its catalog: " + catalog.location.name + "." + artifact.id);
            }
            return std::nullopt;
        }
        selections.push_back(std::move(selection));
    }
    // The objects are fsim's own, checksummed, and their fingerprints match
    // the catalogs: the load trusts their structure as workspace compile
    // does (the load may run on a check thread, where that trust is unset).
    const semantic::TrustedCompiledHirLoad trusted_objects;
    auto loaded = load_workspace_objects(selections, diagnostics, false, *root);
    if (!loaded) {
        return std::nullopt;
    }
    semantic::CompiledDesign design {
        std::move(static_cast<semantic::CompiledDesign&>(*loaded)) };
    // The objects name their sources relative to the edition's library
    // root ("src/ieee/numeric_std.vhdl"). A design names them in the
    // reserved form "fsim-standard/<library>/<file>" the served standard
    // sources use, so they never coincide with a project file's name.
    std::vector<library::SourceNameMapping> mappings;
    std::set<std::string> mapped;
    for (const auto& dependency : design.dependencies()) {
        const auto& name = dependency.logical_name;
        if (!name.starts_with("src/") || !mapped.insert(name).second) {
            continue;
        }
        mappings.push_back(library::SourceNameMapping {
            name, "fsim-standard/" + name.substr(4U) });
    }
    if (!relocate_compiled_design_sources(design, mappings, diagnostics)) {
        return std::nullopt;
    }
    return design;
}

} // namespace fsim::app::application_detail
