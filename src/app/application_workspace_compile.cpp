// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_workspace_internal.hpp"

#include "fsim/artifact/object.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <set>

namespace fsim::app::application_detail {
namespace {

    using CompiledObject = WorkspaceCompiledObject;

    bool compatible_compile_unit(const library::UnitIndexEntry& unit,
        const project::SourceSet& sources)
    {
        if (sources.language == project::Language::vhdl) {
            return unit.language == "vhdl" && unit.standard == sources.standard;
        }
        if (sources.language == project::Language::verilog
            || sources.language == project::Language::system_verilog) {
            return unit.language == "verilog" || unit.language == "systemverilog";
        }
        return false;
    }

    WorkspaceCatalogs compile_environment_catalogs(WorkspaceCatalogs catalogs,
        const project::SourceSet& sources, std::span<const std::string> replaced_sources,
        std::vector<CompiledUnitAvailability>& available_units)
    {
        for (auto& catalog : catalogs) {
            for (auto& artifact : catalog.artifacts) {
                if (artifact.kind != workspace::ArtifactKind::Hdl) {
                    continue;
                }
                std::erase_if(artifact.units, [&](const auto& owned) {
                    if (catalog.location.name == sources.library
                        && std::ranges::find(replaced_sources, owned.source_key)
                            != replaced_sources.end()) {
                        return true;
                    }
                    if (compatible_compile_unit(owned.unit, sources)) {
                        return false;
                    }
                    if (owned.unit.language == "vhdl") {
                        available_units.push_back({ catalog.location.name, owned.unit });
                    }
                    return true;
                });
            }
            std::erase_if(catalog.artifacts, [](const auto& artifact) {
                return artifact.kind == workspace::ArtifactKind::Hdl && artifact.units.empty();
            });
        }
        return catalogs;
    }

    std::optional<std::string> source_key(const std::filesystem::path& path,
        const project::Config& config, diagnostic::Engine& diagnostics)
    {
        std::string error;
        auto result = workspace::source_identity(
            path.is_absolute() ? path : config.base_directory / path, error);
        if (!result) {
            workspace_error(diagnostics, std::move(error));
        }
        return result;
    }

    bool has_shared_compilation_state(const semantic::CompiledDesign& design,
        const semantic::UnitId container)
    {
        const auto scopes = design.semantics.scopes();
        for (const auto& declaration : design.systemverilog_hir.declarations()) {
            if (!declaration.scope.valid()
                || declaration.scope.value() >= scopes.size()
                || scopes[declaration.scope.value()].unit != container
                || (declaration.form != semantic::sv::DeclarationForm::variable
                    && declaration.form != semantic::sv::DeclarationForm::net)
                || declaration.direction != semantic::sv::Direction::unknown) {
                continue;
            }
            bool shared = declaration.lifetime != semantic::sv::Lifetime::automatic;
            auto scope = std::optional<semantic::ScopeId> { declaration.scope };
            bool class_owned { false };
            for (std::size_t depth = 0; scope && depth < scopes.size(); ++depth) {
                if (!scope->valid() || scope->value() >= scopes.size()) {
                    break;
                }
                if (std::ranges::any_of(design.systemverilog_hir.classes(),
                        [&](const auto& type) { return type.scope == *scope; })) {
                    class_owned = true;
                    break;
                }
                scope = scopes[scope->value()].parent;
            }
            if (class_owned) {
                continue;
            }
            scope = declaration.scope;
            for (std::size_t depth = 0; scope && depth < scopes.size(); ++depth) {
                if (!scope->valid() || scope->value() >= scopes.size()) {
                    break;
                }
                const auto callable = std::ranges::find_if(
                    design.systemverilog_hir.declarations(), [&](const auto& candidate) {
                        return candidate.callable && candidate.nested_scope == scope;
                    });
                if (callable != design.systemverilog_hir.declarations().end()) {
                    // The return slot and formal arguments are invocation state.
                    // Retained static locals can carry state across invocations.
                    if (declaration.name == callable->name
                        || std::ranges::find(callable->callable->formals, declaration.id)
                            != callable->callable->formals.end()
                        || (callable->callable->lifetime == semantic::sv::Lifetime::automatic
                            && declaration.lifetime != semantic::sv::Lifetime::static_lifetime)) {
                        shared = false;
                    }
                    break;
                }
                scope = scopes[scope->value()].parent;
            }
            if (shared) {
                return true;
            }
        }
        return false;
    }

    void append_source(CompiledObject& group, const std::string& source)
    {
        if (std::ranges::find(group.sources, source) == group.sources.end()) {
            group.sources.push_back(source);
        }
    }

    void merge_shared_compilation_objects(std::vector<CompiledObject>& groups)
    {
        CompiledObject combined;
        for (auto& group : groups) {
            if (combined.source.empty()) {
                combined.source = group.source;
            }
            for (const auto id : group.units) {
                if (std::ranges::find(combined.units, id) == combined.units.end()) {
                    combined.units.push_back(id);
                }
            }
            for (auto& identity : group.classes) {
                combined.classes.push_back(std::move(identity));
            }
            for (auto& identity : group.primitives) {
                combined.primitives.push_back(std::move(identity));
            }
            for (const auto& source : group.sources) {
                append_source(combined, source);
            }
            combined.unit_sources.insert(group.unit_sources.begin(), group.unit_sources.end());
        }
        groups.clear();
        groups.push_back(std::move(combined));
    }

    std::optional<std::vector<CompiledObject>> partition_objects(
        const CompilationWorkspace& checked, const project::Config& config,
        diagnostic::Engine& diagnostics)
    {
        std::vector<CompiledObject> result;
        std::vector<std::pair<semantic::UnitId, std::string>> containers;
        const auto& target_library = config.source_sets.front().library;
        const auto unit_entries = compiled_object_unit_entries(checked, target_library);
        for (std::size_t index = 0; index < checked.source_units.size(); ++index) {
            const auto id = checked.source_units[index];
            auto source = source_key(checked.source_unit_paths[index], config, diagnostics);
            if (!source) {
                return std::nullopt;
            }
            const auto& unit = checked.semantics.units()[id.value()];
            if (unit.kind == semantic::UnitKind::systemverilog_compilation_unit) {
                containers.emplace_back(id, std::move(*source));
                continue;
            }
            const auto entry = std::ranges::find_if(unit_entries,
                [&](const auto& candidate) { return candidate.first == id; });
            if (entry == unit_entries.end()) {
                workspace_error(diagnostics, "compiled unit has no catalog identity: " + unit.name);
                return std::nullopt;
            }
            result.push_back({ *source, { id }, { }, { } });
            auto& group = result.back();
            append_source(group, *source);
            group.unit_sources.emplace(workspace::unit_identity(entry->second), *source);
        }
        for (std::size_t index = 0; index < checked.source_class_identities.size(); ++index) {
            const auto& identity = checked.source_class_identities[index];
            const auto* declaration = find_compiled_class(checked,
                config.source_sets.front().library, identity);
            const auto* owner = declaration ? compiled_class_owner(checked, *declaration) : nullptr;
            auto source = source_key(checked.source_class_paths[index], config, diagnostics);
            if (!source || !owner) {
                if (!diagnostics.has_error()) {
                    workspace_error(diagnostics, "compiled class has no owning unit: " + identity);
                }
                return std::nullopt;
            }
            auto group = std::ranges::find_if(result, [&](const auto& candidate) {
                if (owner->kind == semantic::sv::UnitKind::compilation_unit) {
                    return std::ranges::find(candidate.classes, declaration->enclosing_identity)
                        != candidate.classes.end();
                }
                return std::ranges::find(candidate.units, owner->id) != candidate.units.end();
            });
            if (group == result.end()) {
                result.push_back({ *source, { owner->id }, { }, { } });
                group = std::prev(result.end());
            }
            const auto entry = compiled_class_metadata_entry(checked, *declaration, target_library);
            if (!entry) {
                workspace_error(diagnostics, "compiled class has no catalog identity: " + identity);
                return std::nullopt;
            }
            group->classes.push_back(identity);
            append_source(*group, *source);
            group->unit_sources.emplace(workspace::unit_identity(*entry), *source);
        }
        for (std::size_t index = 0; index < checked.source_udp_identities.size(); ++index) {
            auto source = source_key(checked.source_udp_paths[index], config, diagnostics);
            if (!source) {
                return std::nullopt;
            }
            result.push_back({ *source, { }, { },
                { checked.source_udp_identities[index] } });
            const auto& identity = checked.source_udp_identities[index];
            const auto* declaration = find_compiled_udp(checked, identity.library, identity.name);
            if (!declaration) {
                workspace_error(diagnostics, "compiled primitive has no catalog identity: " + identity.name);
                return std::nullopt;
            }
            const library::UnitIndexEntry entry {
                declaration->language == semantic::Language::verilog ? "verilog" : "systemverilog",
                "primitive", identity.name, { }, { }, { }, { }, { }, { }
            };
            auto& group = result.back();
            append_source(group, *source);
            group.unit_sources.emplace(workspace::unit_identity(entry), *source);
        }
        for (auto& group : result) {
            for (const auto& [id, source] : containers) {
                if ((source == group.source
                        || config.source_sets.front().compilation_unit == "source-set")
                    && std::ranges::find(group.units, id) == group.units.end()) {
                    group.units.push_back(id);
                    append_source(group, source);
                }
            }
        }
        if (result.size() > 1U && std::ranges::any_of(containers, [&](const auto& container) {
                return has_shared_compilation_state(checked, container.first);
            })) {
            // Shared compilation-unit state must retain one owning declaration
            // set even when several named objects refer to it. The catalog still
            // records each object's source owner for transactional replacement.
            merge_shared_compilation_objects(result);
            for (const auto& [id, source] : containers) {
                if (std::ranges::find(result.front().units, id) == result.front().units.end()) {
                    result.front().units.push_back(id);
                }
                append_source(result.front(), source);
            }
        }
        return result;
    }

    void replace_catalog_sources(workspace::LibraryCatalog& catalog,
        std::span<const std::string> sources,
        const std::vector<workspace::ArtifactRecord>& records)
    {
        for (auto& artifact : catalog.artifacts) {
            if (artifact.kind != workspace::ArtifactKind::Hdl) {
                continue;
            }
            std::erase_if(artifact.units, [&](const auto& unit) {
                return std::ranges::find(sources, unit.source_key) != sources.end();
            });
        }
        std::erase_if(catalog.artifacts, [](const auto& artifact) {
            return artifact.kind == workspace::ArtifactKind::Hdl && artifact.units.empty();
        });
        catalog.artifacts.insert(catalog.artifacts.end(), records.begin(), records.end());
    }

    std::vector<std::string> supporting_libraries(const CheckedProject& checked)
    {
        std::vector<std::string> result;
        for (const auto& dependency : vhdl_package_dependencies(checked)) {
            const auto library = dependency.package.substr(0, dependency.package.find('.'));
            if (std::ranges::find(result, library) == result.end()) {
                result.push_back(library);
            }
        }
        return result;
    }

} // namespace

std::optional<std::vector<WorkspaceCompiledObject>> partition_workspace_objects(
    const CompilationWorkspace& checked, const project::Config& config,
    diagnostic::Engine& diagnostics)
{
    return partition_objects(checked, config, diagnostics);
}

int handle_workspace_compile(const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream&)
{
    if (config.source_sets.size() != 1) {
        workspace_error(diagnostics, "compile requires one source language per invocation");
        return 1;
    }
    const auto& sources = config.source_sets.front();
    workspace::Store store(config.base_directory);
    std::string error;
    auto transaction = store.begin_library(sources.library, error);
    if (!transaction) {
        workspace_error(diagnostics, std::move(error));
        return 1;
    }
    std::vector<std::string> replaced_sources;
    for (const auto& source : sources.files) {
        auto key = source_key(source, config, diagnostics);
        if (!key) {
            return 1;
        }
        replaced_sources.push_back(std::move(*key));
        if (invocation.verbosity != cli::Verbosity::quiet) {
            output << "compiling " << project::to_string(sources.language) << " source "
                   << support::path_to_utf8(source) << " into library '"
                   << sources.library << "'\n";
        }
    }
    auto catalogs = workspace_catalogs(store, &transaction->catalog(), diagnostics);
    if (!catalogs) {
        return 1;
    }
    for (const auto& artifact : transaction->catalog().artifacts) {
        if (artifact.kind != workspace::ArtifactKind::Hdl || artifact.sources.size() <= 1) {
            continue;
        }
        const auto supplied = [&](const std::string& source) {
            return std::ranges::find(replaced_sources, source) != replaced_sources.end();
        };
        if (std::ranges::any_of(artifact.sources, supplied)
            && !std::ranges::all_of(artifact.sources, supplied)) {
            workspace_error(diagnostics, "object '" + artifact.id
                + "' shares compilation-unit state; recompile all of its source files together");
            return 1;
        }
    }
    std::vector<CompiledUnitAvailability> available_units;
    const auto imported_catalogs = compile_environment_catalogs(*catalogs, sources,
        replaced_sources, available_units);
    auto objects = workspace_object_selections(store, imported_catalogs, sources.library,
        replaced_sources, diagnostics);
    if (diagnostics.has_error()) {
        return 1;
    }
    auto imported = load_workspace_objects(objects, diagnostics, false);
    if (!imported) {
        return 1;
    }
    if (invocation.verbosity == cli::Verbosity::verbose) {
        output << "  compiled environment: " << objects.size() << " object(s), "
               << catalogs->size() << " library/libraries\n"
               << "  standard: " << sources.standard << '\n';
    }
    auto checked_workspace = check_project_for_object(config, diagnostics, &*imported,
        available_units);
    if (!checked_workspace) {
        return 1;
    }
    auto groups = partition_workspace_objects(*checked_workspace, config, diagnostics);
    if (!groups) {
        return 1;
    }
    auto checked = release_compiled_project(std::move(*checked_workspace));
    checked_workspace.reset();
    const auto supporting = supporting_libraries(checked);
    std::vector<workspace::ArtifactRecord> records;
    records.reserve(groups->size());
    for (const auto& group : *groups) {
        auto projection = semantic::extract_compiled_objects(checked, group.units,
            group.classes, group.primitives, supporting);
        if (!projection.ok()) {
            workspace_error(diagnostics, "cannot select compiled object: " + projection.error);
            return 1;
        }
        auto record = transaction->allocate_artifact(workspace::ArtifactKind::Hdl, error);
        if (!record) {
            workspace_error(diagnostics, std::move(error));
            return 1;
        }
        const auto path = transaction->artifact_path(*record);
        if (!publish_workspace_object(config, checked, std::move(*projection.design),
                path, diagnostics)) {
            return 1;
        }
        const auto metadata = artifact::load_object_metadata(path, diagnostics);
        if (!metadata) {
            return 1;
        }
        record->sources = group.sources;
        record->fingerprint = metadata->compilation_digest;
        for (const auto& unit : metadata->units) {
            const auto owner = group.unit_sources.find(workspace::unit_identity(unit));
            if (owner == group.unit_sources.end()) {
                workspace_error(diagnostics, "compiled object lost source ownership for '"
                    + unit.name + "'");
                return 1;
            }
            record->units.push_back({ unit, owner->second });
            if (invocation.verbosity == cli::Verbosity::verbose) {
                output << "  " << unit.kind << ' ' << sources.library << '.' << unit.name
                       << " -> " << record->id << '\n';
            }
        }
        records.push_back(std::move(*record));
    }
    auto& target = catalogs->front();
    replace_catalog_sources(target, replaced_sources, records);
    for (std::size_t index = 0; index < records.size(); ++index) {
        records[index].dependencies = workspace_dependencies(checked, (*groups)[index].units,
            *catalogs);
        std::erase_if(records[index].dependencies, [&](const auto& dependency) {
            return dependency.library == sources.library
                && std::ranges::any_of(records[index].units, [&](const auto& unit) {
                    return workspace::unit_identity(unit.unit)
                        == workspace::unit_identity(dependency.unit);
                });
        });
    }
    const auto count = records.size();
    if (!transaction->commit(std::move(records), replaced_sources, error)) {
        workspace_error(diagnostics, std::move(error));
        return 1;
    }
    if (invocation.verbosity != cli::Verbosity::quiet) {
        output << "compiled " << count << " object(s) into library '" << sources.library << "'\n";
    }
    return 0;
}

} // namespace fsim::app::application_detail
