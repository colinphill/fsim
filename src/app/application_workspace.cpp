// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_workspace_internal.hpp"
#include "application_workspace_selection.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/support/path.hpp"

#include <sstream>

namespace fsim::app {
using namespace application_detail;

std::optional<BuiltProject> load_workspace_snapshot(const cli::Invocation& invocation,
    const project::Config& config, diagnostic::Engine& diagnostics)
{
    workspace::Store store(config.base_directory);
    std::string error;
    const auto snapshot = store.read_snapshot(invocation.snapshot, error);
    if (!snapshot) {
        workspace_error(diagnostics, "cannot load snapshot '" + invocation.snapshot + "': " + error);
        return std::nullopt;
    }
    const auto metadata = artifact::load_design_metadata(snapshot->path, diagnostics);
    if (!metadata) {
        return std::nullopt;
    }
    if (invocation.delay_mode && metadata->delay_mode != project::to_string(*invocation.delay_mode)) {
        workspace_error(diagnostics, "requested delay mode differs from snapshot '"
            + invocation.snapshot + "'; elaborate a new snapshot with that delay mode");
        return std::nullopt;
    }
    auto built = load_design_artifact(snapshot->path, diagnostics);
    if (!built) {
        return std::nullopt;
    }
    built->optimization = config.build.optimization;
    built->cache_path = config.build.cache_path;
    if (invocation.file_root) {
        built->file_root = *invocation.file_root;
    }
    if (invocation.random_seed) {
        built->seed = entropy_seed();
        built->entropy_seed = true;
    } else if (invocation.seed) {
        built->seed = *invocation.seed;
        built->entropy_seed = false;
    }
    if (invocation.compiled_processes == cli::CompiledProcessPolicy::all) {
        built->compiled_process_selection = BuiltProject::CompiledProcessSelection::all;
    } else if (invocation.compiled_processes == cli::CompiledProcessPolicy::selected) {
        built->compiled_process_selection = BuiltProject::CompiledProcessSelection::selected;
    }
    return built;
}

namespace application_detail {

int handle_workspace_elaborate(const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream&)
{
    workspace::Store store(config.base_directory);
    auto catalogs = workspace_catalogs(store, nullptr, diagnostics);
    if (!catalogs) {
        return 1;
    }
    auto selected_config = config;
    if (!resolve_workspace_tops(selected_config, *catalogs, diagnostics)) {
        return 1;
    }
    auto selection = select_workspace_design(selected_config, store, *catalogs, diagnostics);
    if (!selection || !validate_workspace_dependencies(selection->catalogs, diagnostics)) {
        return 1;
    }
    std::string error;
    auto transaction = store.begin_snapshot(invocation.snapshot, error);
    if (!transaction) {
        workspace_error(diagnostics, std::move(error));
        return 1;
    }
    if (invocation.verbosity != cli::Verbosity::quiet) {
        output << "elaborating";
        for (const auto& top : selected_config.project.tops) {
            output << ' ' << top.target;
        }
        output << " into snapshot '" << invocation.snapshot << "'\n";
    }
    if (invocation.verbosity == cli::Verbosity::verbose) {
        output << "  loading " << selection->objects.size() << " HDL object(s) and "
               << selection->plugins.size() << " SystemC plugin(s)\n";
    }
    auto built = build_workspace_objects(selected_config, selection->objects, selection->plugins, diagnostics);
    if (!built) {
        return 1;
    }
    auto phase = invocation;
    phase.artifact_output = transaction->artifact_path();
    phase.cache_directory = selected_config.build.cache_path;
    std::ostringstream details;
    const auto status = elaborate_built_workspace(phase, selected_config, std::move(built),
        diagnostics, invocation.verbosity == cli::Verbosity::verbose ? output : details);
    if (status != 0) {
        return status;
    }
    if (!transaction->commit(error)) {
        workspace_error(diagnostics, std::move(error));
        return 1;
    }
    if (invocation.verbosity != cli::Verbosity::quiet) {
        output << "snapshot '" << invocation.snapshot << "' is ready\n";
    }
    return 0;
}

int handle_workspace_simulate(const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream& error_output)
{
    workspace::Store store(config.base_directory);
    std::string error;
    const auto snapshot = store.read_snapshot(invocation.snapshot, error);
    if (!snapshot) {
        workspace_error(diagnostics, "cannot load snapshot '" + invocation.snapshot + "': " + error);
        return 1;
    }
    auto phase = invocation;
    phase.design = snapshot->path;
    phase.cache_directory = config.build.cache_path;
    return handle_simulate(phase, config, diagnostics, output, error_output);
}

int handle_workspace_library(const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream&)
{
    workspace::Store store(config.base_directory);
    std::string error;
    const auto name = invocation.library_name.value_or("work");
    switch (invocation.command) {
    case cli::Command::library_map:
        if (!invocation.library_mapping_path
            || !store.map_library(name, *invocation.library_mapping_path, error)) {
            workspace_error(diagnostics, std::move(error));
            return 1;
        }
        break;
    case cli::Command::library_unmap:
        if (!store.unmap_library(name, error)) {
            workspace_error(diagnostics, std::move(error));
            return 1;
        }
        break;
    case cli::Command::library_delete:
        if (!store.delete_library(name, error)) {
            workspace_error(diagnostics, std::move(error));
            return 1;
        }
        break;
    case cli::Command::library_delete_object:
        if (!invocation.library_object_id
            || !store.delete_artifact(name, *invocation.library_object_id, error)) {
            workspace_error(diagnostics, std::move(error));
            return 1;
        }
        break;
    case cli::Command::library_list: {
        const auto libraries = store.library_locations(error);
        if (!libraries) {
            workspace_error(diagnostics, std::move(error));
            return 1;
        }
        for (const auto& library : *libraries) {
            output << library.name << '\t' << support::path_to_utf8(library.directory)
                   << (library.mapped ? "\tmapped" : "") << '\n';
        }
        return 0;
    }
    case cli::Command::library_objects: {
        const auto catalog = store.read_library(name, error);
        if (!catalog) {
            workspace_error(diagnostics, std::move(error));
            return 1;
        }
        for (const auto& artifact : catalog->artifacts) {
            const auto kind = artifact.kind == workspace::ArtifactKind::Hdl ? "HDL"
                : artifact.kind == workspace::ArtifactKind::SystemCObject ? "SystemC object"
                                                                        : "SystemC plugin";
            output << artifact.id << '\t' << kind;
            for (const auto& owned : artifact.units) {
                output << '\t' << owned.unit.language << ':' << name << '.' << owned.unit.name;
                if (!owned.unit.primary_name.empty()) {
                    output << " (" << owned.unit.kind << " of " << owned.unit.primary_name << ')';
                }
            }
            output << '\n';
            if (invocation.verbosity == cli::Verbosity::verbose) {
                for (const auto& source : artifact.sources) {
                    output << "  source: " << source << '\n';
                }
            }
        }
        return 0;
    }
    default:
        workspace_error(diagnostics, "invalid library operation");
        return 1;
    }
    if (invocation.verbosity != cli::Verbosity::quiet) {
        output << "updated library '" << name << "'\n";
    }
    return 0;
}

} // namespace application_detail
} // namespace fsim::app
