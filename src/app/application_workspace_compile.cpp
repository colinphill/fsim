// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_standard_library_artifact_internal.hpp"
#include "application_workspace_internal.hpp"

#include "fsim/artifact/object.hpp"
#include "fsim/elaboration/dpi_c_type.hpp"
#include "fsim/elaboration/elaborator.hpp"
#include "fsim/frontend/preprocessor.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_vhdl_analysis_stamp.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "application_workspace.hpp"
#include "application_workspace_objects.hpp"
#include "application_workspace_store.hpp"
#include "fsim/app/built_project.hpp"
#include "fsim/cli/driver.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/frontend/diagnostic.hpp"
#include "fsim/frontend/token.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/project/project.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/model.hpp"
#include "fsim/semantic/systemverilog_hir.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iterator>
#include <map>
#include <set>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <ios>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
        // An object built on a unit that changed since, or that this compile
        // replaces, was compiled against another version of it (a package's
        // sequences copied from its source, IEEE 1800-2023 26.3): it is no
        // environment of the current one, and elaboration refuses it until
        // it is recompiled (FSIM-WS-002). A unit several objects own (copies
        // of one header's unit) is current while one owner it was compiled
        // against keeps it.
        const auto stale = [&](const workspace::ArtifactRecord& artifact) {
            return std::ranges::any_of(artifact.dependencies,
                [&](const workspace::UnitDependency& dependency) {
                    const auto identity = workspace::unit_identity(dependency.unit);
                    for (const auto& catalog : catalogs) {
                        if (catalog.location.name != dependency.library) {
                            continue;
                        }
                        for (const auto& provider : catalog.artifacts) {
                            if (provider.fingerprint != dependency.fingerprint) {
                                continue;
                            }
                            if (std::ranges::any_of(provider.units,
                                    [&](const auto& owned) {
                                        return workspace::unit_identity(owned.unit)
                                            == identity
                                            && (catalog.location.name != sources.library
                                                || std::ranges::find(replaced_sources,
                                                       owned.source_key)
                                                    == replaced_sources.end());
                                    })) {
                                return false;
                            }
                        }
                    }
                    return true;
                });
        };
        // Judged on the whole catalogs, before any unit is left out.
        std::set<std::pair<std::string, std::string>> outdated_artifacts;
        for (const auto& catalog : catalogs) {
            for (const auto& artifact : catalog.artifacts) {
                if (artifact.kind == workspace::ArtifactKind::Hdl && stale(artifact)) {
                    outdated_artifacts.emplace(catalog.location.name, artifact.id);
                }
            }
        }
        for (auto& catalog : catalogs) {
            for (auto& artifact : catalog.artifacts) {
                if (artifact.kind != workspace::ArtifactKind::Hdl) {
                    continue;
                }
                const bool outdated
                    = outdated_artifacts.contains({ catalog.location.name, artifact.id });
                std::erase_if(artifact.units, [&](const auto& owned) {
                    if (catalog.location.name == sources.library
                        && std::ranges::find(replaced_sources, owned.source_key)
                            != replaced_sources.end()) {
                        return true;
                    }
                    if (compatible_compile_unit(owned.unit, sources)) {
                        return outdated;
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
        // A DPI export is one C-linkage definition (IEEE 1800-2017 35.5.4):
        // its compilation unit stays in a single object.
        if (std::ranges::any_of(design.systemverilog_hir.dpi_declarations(),
                [&](const auto& dpi) {
                    return dpi.direction
                            == semantic::sv::DpiDirection::export_declaration
                        && dpi.owner_scope.valid()
                        && dpi.owner_scope.value() < scopes.size()
                        && scopes[dpi.owner_scope.value()].unit == container;
                })) {
            return true;
        }
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
        // A source-set compilation unit's $unit declarations belong to
        // every object of that unit; a root compiled as a unit of its own
        // has a compilation-unit identity of its own.
        const auto unit_identity = [&](const semantic::UnitId id) -> std::string_view {
            const auto found = std::ranges::find(
                checked.systemverilog_hir.units(), id, &semantic::sv::Unit::id);
            return found == checked.systemverilog_hir.units().end()
                ? std::string_view { }
                : std::string_view { found->compilation_unit_identity };
        };
        for (auto& group : result) {
            for (const auto& [id, source] : containers) {
                const auto same_unit = [&] {
                    const auto identity = unit_identity(id);
                    return !identity.empty()
                        && std::ranges::any_of(group.units, [&](const semantic::UnitId unit) {
                               return unit_identity(unit) == identity;
                           });
                };
                if ((source == group.source
                        || (config.source_sets.front().compilation_unit == "source-set"
                            && same_unit()))
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
        // Re-analysis replaces a VHDL library unit even when another source
        // file defined it (IEEE 1076-2008 13.5); the secondary units of a
        // replaced primary unit are obsolete.
        const auto secondary = [](const library::UnitIndexEntry& unit) {
            return unit.kind == "architecture" || unit.kind == "package_body"
                || (unit.kind == "package" && !unit.primary_name.empty());
        };
        std::set<std::string> replaced_units;
        std::set<std::string> replaced_primaries;
        const auto primary_identity = [](const library::UnitIndexEntry& unit) {
            auto primary = unit;
            primary.kind = "primary";
            // A configuration names its entity in primary_name but is itself a
            // primary unit.
            const bool own_name = unit.primary_name.empty()
                || (unit.kind != "architecture" && unit.kind != "package_body"
                    && unit.kind != "package");
            primary.name = own_name ? unit.name : unit.primary_name;
            primary.primary_name.clear();
            primary.architecture.clear();
            primary.name_form = own_name
                ? unit.name_form : unit.primary_name_form;
            primary.primary_name_form = library::VhdlIdentifierForm::unknown;
            return workspace::unit_identity(primary);
        };
        for (const auto& record : records) {
            for (const auto& owned : record.units) {
                if (owned.unit.language != "vhdl") {
                    continue;
                }
                replaced_units.insert(workspace::unit_identity(owned.unit));
                if (!secondary(owned.unit)) {
                    replaced_primaries.insert(primary_identity(owned.unit));
                }
            }
        }
        for (auto& artifact : catalog.artifacts) {
            if (artifact.kind != workspace::ArtifactKind::Hdl) {
                continue;
            }
            std::erase_if(artifact.units, [&](const auto& unit) {
                return std::ranges::find(sources, unit.source_key) != sources.end()
                    || (unit.unit.language == "vhdl"
                        && (replaced_units.contains(workspace::unit_identity(unit.unit))
                            || (secondary(unit.unit)
                                && replaced_primaries.contains(primary_identity(unit.unit)))));
            });
        }
        std::erase_if(catalog.artifacts, [](const auto& artifact) {
            return artifact.kind == workspace::ArtifactKind::Hdl && artifact.units.empty();
        });
        catalog.artifacts.insert(catalog.artifacts.end(), records.begin(), records.end());
    }

    // The analysis rules of each newly analyzed architecture (IEEE 1076-2019
    // 9, 10, 11) on the unit as analyzed: nothing is elaborated, and a rule
    // that needs a generic value or an actual is left to elaboration.
    // Diagnostics outside the analyzed files are not reported here.
    bool check_vhdl_analysis_rules(const CheckedProject& checked,
        std::span<const CompiledObject> groups, const project::SourceSet& sources,
        diagnostic::Engine& diagnostics)
    {
        std::set<std::uint64_t> analyzed;
        for (const auto& group : groups) {
            for (const auto& unit : group.units) {
                analyzed.insert(unit.value());
            }
        }
        std::set<std::string> files;
        for (const auto& file : sources.files) {
            files.insert(support::path_to_utf8(file.filename()));
        }
        bool clean = true;
        for (const auto& unit : checked.vhdl_hir.units()) {
            if (unit.kind != semantic::vhdl::UnitKind::architecture
                || !analyzed.contains(unit.id.value())) {
                continue;
            }
            for (const auto& input :
                elaboration::validate_vhdl_architecture_analysis(checked, unit.id)) {
                const auto file = support::path_to_utf8(
                    std::filesystem::path { input.span.source_name.str() }.filename());
                if (!files.contains(file)) {
                    continue;
                }
                diagnostics.error(input.code, input.message, span(input.span));
                clean = false;
            }
        }
        return clean;
    }

    std::vector<std::string> supporting_libraries(const CheckedProject& checked)
    {
        std::vector<std::string> result;
        for (const auto& dependency : vhdl_package_dependencies(checked)) {
            // A package of the precompiled standard libraries is a mapped
            // library's unit: an object carries it only as the context its
            // own units need, as for any other library.
            if (is_standard_library_package_revision(dependency.revision)) {
                continue;
            }
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

static int workspace_compile(const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output,
    const std::optional<frontend::VhdlStandard> bootstrap_standard,
    const bool uvm_library_build = false)
{
    // Workspace objects and libraries are checksummed compiled HIR; the new
    // object's state is still checked when it is serialized.
    const semantic::TrustedCompiledHirLoad trusted_objects;
    // Environment objects built on a unit this compile analyzes again are
    // obsolete and leave the library with this transaction (IEEE 1076-2019
    // 13.5); the compile's own links resolve them again meanwhile.
    const semantic::VhdlObsoleteUnitsLeftUnstamped obsolete_environment_units;
    if (config.source_sets.size() != 1) {
        workspace_error(diagnostics, "compile requires one source language per invocation");
        return 1;
    }
    const auto& sources = config.source_sets.front();
    // `--preprocess-only` stops after the Verilog preprocessor (IEEE
    // 1800-2017 22), reporting its diagnostics without parsing.
    if (invocation.preprocess_only
        && (sources.language == project::Language::verilog
            || sources.language == project::Language::system_verilog)) {
        frontend::PreprocessorOptions options;
        options.include_directories = sources.include_directories;
        if (const auto uvm_library = uvm_library_request(config);
            uvm_library && !uvm_library->root.empty()) {
            options.include_directories.push_back(
                uvm_library_include_directory(uvm_library->root));
        }
        options.defines = sources.defines;
        options.default_timescale = sources.default_timescale;
        const auto result = frontend::preprocess_verilog_compilation_unit(
            sources.files,
            sources.language == project::Language::verilog
                ? frontend::Language::Verilog2005
                : frontend::Language::SystemVerilog2017,
            options);
        for (const auto& diagnostic : result.lexed.diagnostics) {
            if (diagnostic.severity == frontend::DiagnosticSeverity::Error) {
                diagnostics.error(diagnostic.code, diagnostic.message);
            }
        }
        return result.ok() ? 0 : 1;
    }
    const auto store = bootstrap_standard || uvm_library_build
        ? workspace::Store(config.base_directory, workspace::StandardLibraryStore { })
        : workspace::Store(config.base_directory);
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
    // An object written by an older fsim cannot be decoded. It is left out
    // of the environment (its units too) and is rebuilt when its own sources
    // are recompiled. Outdated objects are looked for only after a failed
    // load, so a current workspace reads no payload twice.
    // The precompiled UVM library the source set's release maps (owner
    // D2-Q9): its catalog joins the workspace's, so objects compiled against
    // it are current; its objects join the environment only when a source
    // names one of its packages or an environment object depends on it.
    auto uvm_library = bootstrap_standard || uvm_library_build
        ? std::nullopt : uvm_library_request(config);
    bool load_uvm = false;
    if (uvm_library && !uvm_library->root.empty()
        && std::ranges::none_of(*catalogs, [](const workspace::LibraryCatalog& catalog) {
               return catalog.location.name == uvm_library_name;
           })) {
        std::string uvm_error;
        auto catalog = uvm_library_catalog(uvm_library->root, uvm_error);
        if (!catalog) {
            workspace_error(diagnostics, std::move(uvm_error));
            return 1;
        }
        load_uvm = std::ranges::any_of(*catalogs, [](const workspace::LibraryCatalog& other) {
            return std::ranges::any_of(other.artifacts, [](const workspace::ArtifactRecord& artifact) {
                return std::ranges::any_of(artifact.dependencies,
                    [](const workspace::UnitDependency& dependency) {
                        return dependency.library == uvm_library_name;
                    });
            });
        });
        catalogs->push_back(std::move(*catalog));
    } else {
        append_dependent_uvm_library_catalog(*catalogs);
        load_uvm = !uvm_library;
    }
    std::vector<OutdatedObject> outdated;
    std::vector<CompiledUnitAvailability> available_units;
    std::vector<WorkspaceObjectSelection> objects;
    std::optional<CompilationWorkspace> imported;
    const auto report_skipped = [&] {
        for (const auto& object : outdated) {
            report_outdated_object(diagnostics, object, true);
        }
    };
    std::optional<CompilationWorkspace> checked_workspace;
    for (;;) {
    outdated.clear();
    imported.reset();
    for (;;) {
        diagnostic::Engine attempt;
        available_units.clear();
        auto imported_catalogs = compile_environment_catalogs(*catalogs, sources,
            replaced_sources, available_units);
        if (!load_uvm) {
            std::erase_if(imported_catalogs, [](const workspace::LibraryCatalog& catalog) {
                return catalog.location.name == uvm_library_name;
            });
        }
        objects = workspace_object_selections(store, imported_catalogs, sources.library,
            replaced_sources, attempt);
        if (!attempt.has_error()) {
            imported = load_workspace_objects(
                objects, attempt, false, config.base_directory);
        }
        if (imported && !attempt.has_error()) {
            for (const auto& diagnostic : attempt.diagnostics()) {
                diagnostics.report(diagnostic);
            }
            break;
        }
        imported.reset();
        auto found = find_outdated_objects(store, imported_catalogs);
        if (found.empty()) {
            for (const auto& diagnostic : attempt.diagnostics()) {
                diagnostics.report(diagnostic);
            }
            break;
        }
        for (auto& object : found) {
            for (auto& catalog : *catalogs) {
                if (catalog.location.name == object.library) {
                    std::erase_if(catalog.artifacts, [&](const auto& artifact) {
                        return artifact.id == object.id;
                    });
                }
            }
            if (invocation.verbosity == cli::Verbosity::verbose) {
                output << "  skipping outdated object " << object.path.filename().string()
                       << " (" << object.reason << ")\n";
            }
            outdated.push_back(std::move(object));
        }
    }
    if (!imported) {
        report_skipped();
        return 1;
    }
    if (invocation.verbosity == cli::Verbosity::verbose) {
        output << "  compiled environment: " << objects.size() << " object(s), "
               << catalogs->size() << " library/libraries\n"
               << "  standard: " << sources.standard << '\n';
    }
    if (uvm_library) {
        uvm_library->loaded = load_uvm;
        uvm_library->needed = false;
    }
    if (uvm_library && !load_uvm) {
        // A pass that ends because a source names the library reports
        // nothing; any other result's diagnostics are the compile's.
        diagnostic::Engine pass;
        checked_workspace = check_project_for_object(config, pass, &*imported,
            available_units, bootstrap_standard, &*uvm_library);
        if (uvm_library->needed) {
            load_uvm = true;
            continue;
        }
        for (const auto& diagnostic : pass.diagnostics()) {
            diagnostics.report(diagnostic);
        }
    } else {
        checked_workspace = check_project_for_object(config, diagnostics, &*imported,
            available_units, bootstrap_standard,
            uvm_library ? &*uvm_library : nullptr);
    }
    break;
    }
    if (!checked_workspace) {
        // A unit only a skipped object provides is reported as missing.
        report_skipped();
        return 1;
    }
    auto groups = partition_workspace_objects(*checked_workspace, config, diagnostics);
    if (!groups) {
        return 1;
    }
    auto checked = release_compiled_project(std::move(*checked_workspace));
    checked_workspace.reset();
    if (invocation.dpi_header) {
        auto header = support::native_fs::open_ofstream(
            *invocation.dpi_header, std::ios::binary);
        header << elaboration::systemverilog_dpi_header(
            checked.systemverilog_hir);
        if (!header) {
            workspace_error(diagnostics, "cannot write DPI header '"
                    + fsim::support::path_to_utf8(*invocation.dpi_header) + "'");
            return 1;
        }
    }
    const auto supporting = supporting_libraries(checked);
    std::vector<semantic::CompiledObjectSelection> selections;
    selections.reserve(groups->size());
    for (const auto& group : *groups) {
        selections.push_back({ group.units, group.classes, group.primitives });
    }
    // The source keys of each checked root and its includes, which own
    // the units of the groups.
    std::vector<std::vector<std::string>> source_keys;
    source_keys.reserve(checked.hdl_sources.size());
    for (const auto& source : checked.hdl_sources) {
        auto& keys = source_keys.emplace_back();
        const auto add_key = [&](const std::filesystem::path& path) {
            auto key = source_key(path, config, diagnostics);
            if (key) {
                keys.push_back(std::move(*key));
            }
            return key.has_value();
        };
        if (!add_key(source.path)) {
            return 1;
        }
        for (const auto& dependency : source.dependencies) {
            if (!add_key(dependency.path)) {
                return 1;
            }
        }
    }
    std::vector<workspace::ArtifactRecord> records;
    records.reserve(groups->size());
    // Each object is published before the next is projected, so only one
    // projection is held beside the checked design.
    const auto published = semantic::extract_compiled_object_set(checked, selections, supporting,
        [&](const std::size_t group_index, semantic::CompiledLinkResult projection) {
        const auto& group = (*groups)[group_index];
        if (!projection.ok()) {
            workspace_error(diagnostics, "cannot select compiled object: " + projection.error);
            return false;
        }
        auto record = transaction->allocate_artifact(workspace::ArtifactKind::Hdl, error);
        if (!record) {
            workspace_error(diagnostics, std::move(error));
            return false;
        }
        const auto path = transaction->artifact_path(*record);
        // The object records the roots of the compilation units its units
        // were parsed in, so a file compiled with independent files records
        // what it records when compiled alone.
        std::set<std::string> units;
        std::vector<std::filesystem::path> roots;
        for (std::size_t index = 0; index < checked.hdl_sources.size(); ++index) {
            if (std::ranges::any_of(source_keys[index], [&](const auto& key) {
                    return std::ranges::find(group.sources, key) != group.sources.end();
                })) {
                units.insert(checked.hdl_sources[index].compilation_unit_digest);
            }
        }
        for (const auto& source : checked.hdl_sources) {
            if (units.contains(source.compilation_unit_digest)) {
                roots.push_back(source.path);
            }
        }
        const auto digests = compiled_unit_content_digests(
            *projection.design, sources.library);
        if (!publish_workspace_object(config, checked, std::move(*projection.design),
                path, diagnostics, &roots)) {
            return false;
        }
        const auto metadata = artifact::load_object_metadata(path, diagnostics);
        if (!metadata) {
            return false;
        }
        record->sources = group.sources;
        record->fingerprint = metadata->compilation_digest;
        for (const auto& unit : metadata->units) {
            const auto owner = group.unit_sources.find(workspace::unit_identity(unit));
            if (owner == group.unit_sources.end()) {
                workspace_error(diagnostics, "compiled object lost source ownership for '"
                    + unit.name + "'");
                return false;
            }
            const auto digest = digests.find(
                compiled_unit_metadata_key(unit, sources.library));
            record->units.push_back({ unit, owner->second,
                digest == digests.end() ? 0U : digest->second });
        }
        records.push_back(std::move(*record));
        return true;
    });
    if (!published) {
        return 1;
    }
    if (sources.language == project::Language::vhdl
        && !check_vhdl_analysis_rules(checked, *groups, sources, diagnostics)) {
        return 1;
    }
    auto& target = catalogs->front();
    replace_catalog_sources(target, replaced_sources, records);
    for (std::size_t index = 0; index < records.size(); ++index) {
        records[index].dependencies = workspace_dependencies(checked,
            (*groups)[index].units, *catalogs);
        std::erase_if(records[index].dependencies, [&](const auto& dependency) {
            return dependency.library == sources.library
                && std::ranges::any_of(records[index].units, [&](const auto& unit) {
                       return workspace::unit_identity(unit.unit)
                           == workspace::unit_identity(dependency.unit);
                   });
        });
    }
    const auto count = records.size();
    std::vector<library::UnitIndexEntry> published_units;
    for (const auto& record : records) {
        for (const auto& owned : record.units)
            published_units.push_back(owned.unit);
    }
    if (!transaction->commit(std::move(records), replaced_sources, error)) {
        workspace_error(diagnostics, std::move(error));
        return 1;
    }
    if (invocation.verbosity == cli::Verbosity::verbose) {
        // Artifacts are named when the transaction commits.
        for (const auto& unit : published_units) {
            const auto identity = workspace::unit_identity(unit);
            for (const auto& artifact : transaction->catalog().artifacts) {
                if (std::ranges::any_of(artifact.units, [&](const auto& owned) {
                        return workspace::unit_identity(owned.unit) == identity;
                    })) {
                    output << "  " << unit.kind << ' ' << sources.library << '.' << unit.name
                           << " -> " << support::path_to_utf8(artifact.path) << '\n';
                    break;
                }
            }
        }
    }
    if (invocation.verbosity != cli::Verbosity::quiet) {
        output << "compiled " << count << " object(s) into library '" << sources.library << "'\n";
    }
    return 0;
}

int handle_workspace_compile(const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream&)
{
    return workspace_compile(invocation, config, diagnostics, output, std::nullopt);
}

int compile_standard_library(const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream&,
    const frontend::VhdlStandard standard)
{
    return workspace_compile(invocation, config, diagnostics, output, standard);
}

int compile_uvm_library(const cli::Invocation& invocation, const project::Config& config,
    diagnostic::Engine& diagnostics, std::ostream& output, std::ostream&)
{
    return workspace_compile(invocation, config, diagnostics, output, std::nullopt, true);
}

} // namespace fsim::app::application_detail
