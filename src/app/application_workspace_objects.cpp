// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_objects.hpp"

#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <set>

namespace fsim::app::application_detail {
namespace {

constexpr std::string_view workspace_object_diagnostic = "FSIM-WORKSPACE-004";

struct OrderedSelection {
    const WorkspaceObjectSelection* selection { };
    std::string key;
};

std::optional<std::vector<const WorkspaceObjectSelection*>>
ordered_workspace_objects(const std::span<const WorkspaceObjectSelection> objects,
    diagnostic::Engine& diagnostics)
{
    std::vector<const WorkspaceObjectSelection*> ordered;
    ordered.reserve(objects.size());
    for (std::size_t begin = 0; begin < objects.size();) {
        const auto rank = objects[begin].catalog_rank;
        std::size_t end = begin + 1;
        if (rank) {
            while (end < objects.size() && objects[end].catalog_rank == rank) {
                ++end;
            }
        }
        if (!rank) {
            ordered.push_back(&objects[begin]);
            begin = end;
            continue;
        }

        std::vector<OrderedSelection> group;
        group.reserve(end - begin);
        for (std::size_t index = begin; index < end; ++index) {
            const auto& selection = objects[index];
            if (selection.active_units.empty()) {
                continue;
            }
            auto metadata = artifact::load_object_metadata(
                selection.path, diagnostics);
            if (!metadata) {
                return std::nullopt;
            }
            auto bytes = artifact::serialize_object_metadata(*metadata);
            compiler::CacheKeyBuilder key;
            key.add("managed-selection-order", "catalog-content-v1");
            key.add("library", metadata->library);
            key.add("language", metadata->language);
            key.add("standard", metadata->standard);
            key.add("profile", metadata->compatibility_profile);
            key.add("compilation", metadata->compilation_digest);
            key.add("compiled-hir", metadata->compiled_hir_checksum);
            key.add_bytes("metadata", std::as_bytes(std::span { bytes }));
            key.add("active-count", std::to_string(selection.active_units.size()));
            for (const auto& unit : selection.active_units) {
                key.add("unit-language", unit.language);
                key.add("unit-kind", unit.kind);
                key.add("unit-name", unit.name);
                key.add("unit-primary", unit.primary_name);
                key.add("unit-architecture", unit.architecture);
                key.add("unit-artifact", support::path_to_utf8(unit.artifact));
                key.add("unit-checksum", unit.checksum);
                key.add("unit-standard", unit.standard);
                key.add("unit-profile", unit.compatibility_profile);
                key.add("unit-name-form",
                    std::to_string(static_cast<std::uint8_t>(unit.name_form)));
                key.add("unit-primary-name-form",
                    std::to_string(static_cast<std::uint8_t>(unit.primary_name_form)));
            }
            group.push_back({ &selection, key.finish() });
        }
        std::ranges::sort(group, { }, &OrderedSelection::key);
        for (std::size_t index = 1; index < group.size(); ++index) {
            if (group[index - 1].key != group[index].key) {
                continue;
            }
            const auto left = artifact::load_object_metadata(
                group[index - 1].selection->path, diagnostics);
            const auto right = artifact::load_object_metadata(
                group[index].selection->path, diagnostics);
            if (!left || !right) {
                return std::nullopt;
            }
            if (artifact::serialize_object_metadata(*left)
                    != artifact::serialize_object_metadata(*right)
                || group[index - 1].selection->active_units
                    != group[index].selection->active_units) {
                diagnostics.error(std::string { workspace_object_diagnostic },
                    "managed objects have a colliding content order identity");
                return std::nullopt;
            }
        }
        for (const auto& item : group) {
            ordered.push_back(item.selection);
        }
        begin = end;
    }
    return ordered;
}

std::string_view normalized_library(const std::string_view library)
{
    return library.empty() ? std::string_view { "work" } : library;
}

bool unit_index_identity_compatible(
    const library::UnitIndexEntry& indexed,
    const library::UnitIndexEntry& candidate)
{
    const auto form_matches = [](const library::VhdlIdentifierForm left,
                                 const library::VhdlIdentifierForm right) {
        return left == library::VhdlIdentifierForm::unknown
            || right == library::VhdlIdentifierForm::unknown || left == right;
    };
    return indexed.language == candidate.language
        && indexed.kind == candidate.kind && indexed.name == candidate.name
        && indexed.primary_name == candidate.primary_name
        && indexed.architecture == candidate.architecture
        && indexed.artifact == candidate.artifact
        && indexed.checksum == candidate.checksum
        && indexed.standard == candidate.standard
        && indexed.compatibility_profile == candidate.compatibility_profile
        && form_matches(indexed.name_form, candidate.name_form)
        && form_matches(indexed.primary_name_form, candidate.primary_name_form);
}

std::vector<semantic::UnitId> indexed_unit_ids(
    const semantic::CompiledDesign& design, const std::string_view library)
{
    // Match the language ordering of compiled_unit_metadata_entries. Synthetic
    // compilation units are owned by their selected global class declarations.
    std::vector<semantic::UnitId> result;
    for (const auto& unit : design.systemverilog_hir.units()) {
        if (normalized_library(unit.library) == library
            && unit.kind != semantic::sv::UnitKind::compilation_unit) {
            result.push_back(unit.id);
        }
    }
    for (const auto& unit : design.vhdl_hir.units()) {
        if (normalized_library(unit.library) == library) {
            result.push_back(unit.id);
        }
    }
    return result;
}

struct ObjectSelection {
    std::vector<semantic::UnitId> units;
    std::vector<std::string> classes;
    std::vector<semantic::CompiledUdpIdentity> udps;
};

std::optional<ObjectSelection> resolve_selection(
    const WorkspaceObjectSelection& selection,
    artifact::ObjectMetadata& metadata,
    const semantic::CompiledDesign& design,
    std::set<std::string>& known_units,
    diagnostic::Engine& diagnostics)
{
    const auto entries = compiled_unit_metadata_entries(design, metadata.library);
    const auto ids = indexed_unit_ids(design, metadata.library);
    if (entries.size() != ids.size()) {
        diagnostics.error(std::string { workspace_object_diagnostic },
            "cannot match managed object metadata to its compiled units");
        return std::nullopt;
    }
    ObjectSelection result;
    for (const auto& unit : design.systemverilog_hir.units()) {
        if (unit.kind == semantic::sv::UnitKind::compilation_unit
            && normalized_library(unit.library) == metadata.library) {
            result.units.push_back(unit.id);
        }
    }
    for (const auto& entry : selection.active_units) {
        auto indexed = metadata.units.end();
        std::size_t indexed_count = 0U;
        for (auto candidate = metadata.units.begin();
             candidate != metadata.units.end(); ++candidate) {
            if (unit_index_identity_compatible(entry, *candidate)) {
                indexed = candidate;
                ++indexed_count;
            }
        }
        if (indexed_count != 1U) {
            diagnostics.error(std::string { workspace_object_diagnostic },
                indexed_count == 0U
                    ? "library catalog selects an absent or changed object '"
                        + metadata.library + '.' + entry.name + "' in "
                            + support::path_to_utf8(selection.path)
                    : "legacy library catalog unit identity is ambiguous for '"
                        + metadata.library + '.' + entry.name + "' in "
                            + support::path_to_utf8(selection.path));
            return std::nullopt;
        }
        auto resolved_entry = *indexed;
        std::optional<std::size_t> compiled_unit_index;
        if (resolved_entry.kind != "class"
            && resolved_entry.kind != "primitive") {
            for (std::size_t index = 0; index < entries.size(); ++index) {
                if (!unit_index_identity_compatible(resolved_entry, entries[index])) {
                    continue;
                }
                if (compiled_unit_index) {
                    diagnostics.error(std::string { workspace_object_diagnostic },
                        "legacy object identity is ambiguous without VHDL identifier forms: '"
                            + metadata.library + '.' + entry.name + "'");
                    return std::nullopt;
                }
                compiled_unit_index = index;
            }
            if (!compiled_unit_index) {
                diagnostics.error(std::string { workspace_object_diagnostic },
                    "library object has no compiled identity: '"
                        + metadata.library + '.' + entry.name + "'");
                return std::nullopt;
            }
            // Old object and catalog schemas omit the identifier form. The HIR
            // unit index is checksummed with the object and supplies it when the
            // normalized identity has exactly one possible match.
            indexed->name_form = entries[*compiled_unit_index].name_form;
            indexed->primary_name_form
                = entries[*compiled_unit_index].primary_name_form;
            resolved_entry = *indexed;
        }
        const auto key = compiled_unit_metadata_key(resolved_entry, metadata.library);
        if (!known_units.insert(key).second) {
            diagnostics.error(std::string { workspace_object_diagnostic },
                "library catalog selects duplicate object '" + key + "'");
            return std::nullopt;
        }
        if (resolved_entry.kind == "class") {
            result.classes.push_back(resolved_entry.name);
        } else if (resolved_entry.kind == "primitive") {
            result.udps.push_back({ metadata.library, resolved_entry.name });
        } else {
            if (!compiled_unit_index) {
                diagnostics.error(std::string { workspace_object_diagnostic },
                    "library object has no compiled identity: '"
                        + metadata.library + '.' + entry.name + "'");
                return std::nullopt;
            }
            result.units.push_back(ids[*compiled_unit_index]);
        }
    }
    return result;
}

struct CompilerEnvironment {
    std::optional<std::string> standard;
    std::vector<library::VhdlPackageDependency> dependencies;
    std::optional<std::string> bytes;
    // The object whose compiler package set covers every other object's:
    // it alone supplies the shared packages.
    std::optional<std::filesystem::path> provider;
    std::vector<library::VhdlPackageDependency> provided;
};

std::optional<std::vector<std::string>> select_compiler_environment(
    const std::filesystem::path& metadata_path,
    const artifact::ObjectMetadata& metadata,
    const semantic::CompiledDesign& design,
    CompilerEnvironment& environment,
    diagnostic::Engine& diagnostics)
{
    if (metadata.language == "vhdl") {
        if (environment.standard && *environment.standard != metadata.standard) {
            diagnostics.error("FSIM-ART-VHDEP-001",
                "workspace objects select incompatible VHDL standards; "
                "recompile them with one VHDL standard");
            return std::nullopt;
        }
        environment.standard = metadata.standard;
    }
    std::vector<std::string> libraries;
    for (const auto& dependency : metadata.vhdl_package_dependencies) {
        const auto library = dependency.package.substr(
            0, dependency.package.find('.'));
        if (library != metadata.library
            && std::ranges::find(libraries, library) == libraries.end()) {
            libraries.push_back(library);
        }
    }
    if (metadata.vhdl_package_dependencies.empty()) {
        return libraries;
    }
    // An object whose packages are a subset of the provider's uses the
    // provider's copies (identical revision and source digest).
    if (environment.provider && *environment.provider != metadata_path) {
        const bool covered = std::ranges::all_of(metadata.vhdl_package_dependencies,
            [&](const library::VhdlPackageDependency& dependency) {
                return std::ranges::find(environment.provided, dependency)
                    != environment.provided.end();
            });
        if (covered) {
            return std::vector<std::string> { };
        }
    }
    auto projected = semantic::extract_compiled_libraries(design, libraries);
    if (!projected.ok()) {
        diagnostics.error(std::string { workspace_object_diagnostic },
            "cannot project managed compiler package environment: "
                + projected.error);
        return std::nullopt;
    }
    auto bytes = serialize_compiled_hir_bundle(*projected.design, diagnostics);
    if (!bytes) {
        return std::nullopt;
    }
    if (!environment.bytes) {
        environment.dependencies = metadata.vhdl_package_dependencies;
        environment.bytes = std::move(bytes);
        return libraries;
    }
    if (environment.dependencies != metadata.vhdl_package_dependencies
        || *environment.bytes != *bytes) {
        diagnostics.error("FSIM-ART-VHDEP-001",
            "workspace objects contain incompatible compiler-supplied VHDL "
            "package environments; recompile them with this fsim build");
        return std::nullopt;
    }
    return std::vector<std::string> { };
}

bool merge_trace_archive(CompilationWorkspace& result,
    const CompilationWorkspace& input, diagnostic::Engine& diagnostics)
{
    if (!input.trace_archive) {
        return true;
    }
    if (result.trace_archive
        && (!trace_archive_profiles_compatible(
                *result.trace_archive, *input.trace_archive)
            || (result.trace_archive->lifecycle == TraceLifecycle::Disabled)
                != (input.trace_archive->lifecycle == TraceLifecycle::Disabled))) {
        diagnostics.error("FSIM-TRACE-ARCHIVE-003",
            "workspace objects contain incompatible trace formats, profiles or lifecycle classes");
        return false;
    }
    if (!result.trace_archive) {
        result.trace_archive = input.trace_archive;
        return true;
    }
    const auto current = encode_trace_archive(
        *result.trace_archive, TraceArchiveKind::Object);
    const auto candidate = encode_trace_archive(
        *input.trace_archive, TraceArchiveKind::Object);
    if (!current.ok() || !candidate.ok()) {
        diagnostics.error("FSIM-TRACE-ARCHIVE-003",
            "workspace object trace archive cannot be compared");
        return false;
    }
    const auto byte_less = [](const std::byte left, const std::byte right) {
        return std::to_integer<unsigned>(left) < std::to_integer<unsigned>(right);
    };
    if (std::ranges::lexicographical_compare(candidate.archive,
            current.archive, byte_less)) {
        result.trace_archive = input.trace_archive;
    }
    return true;
}

bool append_selected_sources(CompilationWorkspace& result,
    CompilationWorkspace& input, const semantic::CompiledDesign& selected,
    std::map<std::string, std::string>& known_sources,
    diagnostic::Engine& diagnostics)
{
    std::set<std::string> selected_paths;
    for (const auto& source : selected.semantics.source_files()) {
        selected_paths.insert(source_path_key(
            support::path_from_utf8(source.physical_name)));
    }
    for (auto& source : input.hdl_sources) {
        const auto key = source_path_key(source.path);
        if (!selected_paths.contains(key)) {
            continue;
        }
        const auto [found, inserted] = known_sources.emplace(
            key, source.content_digest);
        if (!inserted && found->second != source.content_digest) {
            diagnostics.error(std::string { workspace_object_diagnostic },
                "managed object source identity collides with different content: "
                    + support::path_to_utf8(source.path));
            return false;
        }
        if (inserted) {
            result.hdl_sources.push_back(std::move(source));
        }
    }
    return true;
}

bool validate_uvm_environment(const CompilationWorkspace& checked,
    diagnostic::Engine& diagnostics)
{
    const auto release = checked.systemverilog_uvm_provenance.release;
    if (release == project::SystemVerilogUvmRelease::none) {
        return true;
    }
    const auto has_class = [&](const std::string_view suffix) {
        return std::ranges::any_of(
            checked.compiled_systemverilog_class_specializations,
            [&](const auto& specialization) {
                return specialization.declaration_identity.ends_with(suffix);
            });
    };
    const auto compatibility = project::systemverilog_uvm_compatibility(release);
    if (!has_class("::uvm_object")
        || has_class("::uvm_policy") != compatibility.ieee_policy_classes) {
        diagnostics.error("FSIM-UVM-VERSION-002",
            "governed UVM workspace release does not match its linked "
            "compiled-HIR uvm_pkg API surface");
        return false;
    }
    return true;
}

} // namespace

std::vector<std::pair<semantic::UnitId, library::UnitIndexEntry>>
compiled_object_unit_entries(const semantic::CompiledDesign& design,
    const std::string_view expected_library)
{
    auto entries = compiled_unit_metadata_entries(design, expected_library);
    const auto ids = indexed_unit_ids(design, expected_library);
    std::vector<std::pair<semantic::UnitId, library::UnitIndexEntry>> result;
    if (entries.size() != ids.size()) {
        return result;
    }
    result.reserve(ids.size());
    for (std::size_t index = 0; index < ids.size(); ++index) {
        result.emplace_back(ids[index], std::move(entries[index]));
    }
    return result;
}

std::optional<library::UnitIndexEntry> hydrate_legacy_vhdl_identifier_forms(
    const WorkspaceObjectSelection& selection,
    const library::UnitIndexEntry& indexed,
    diagnostic::Engine& diagnostics)
{
    if (indexed.language != "vhdl"
        || (indexed.name_form != library::VhdlIdentifierForm::unknown
            && (indexed.primary_name.empty()
                || indexed.primary_name_form
                    != library::VhdlIdentifierForm::unknown))) {
        return indexed;
    }

    const auto metadata = artifact::load_object_metadata(
        selection.path, diagnostics);
    if (!metadata) {
        return std::nullopt;
    }
    const WorkspaceObjectSelection single {
        selection.path, { indexed }, selection.catalog_rank
    };
    const auto design = load_workspace_objects(
        std::array { single }, diagnostics, false);
    if (!design) {
        return std::nullopt;
    }

    const auto entries = compiled_object_unit_entries(*design,
        normalized_library(metadata->library));
    const library::UnitIndexEntry* resolved = nullptr;
    std::size_t match_count = 0U;
    for (const auto& [unit_id, candidate] : entries) {
        (void)unit_id;
        if (!unit_index_identity_compatible(indexed, candidate)) {
            continue;
        }
        resolved = &candidate;
        ++match_count;
    }
    if (match_count != 1U || resolved == nullptr) {
        diagnostics.error(std::string { workspace_object_diagnostic },
            match_count == 0U
                ? "checksummed compiled HIR has no matching legacy VHDL unit"
                : "checksummed compiled HIR has ambiguous legacy VHDL unit identity");
        return std::nullopt;
    }

    const auto name_form = resolved->name_form;
    const auto primary_name_form = resolved->primary_name_form;
    if ((indexed.name_form == library::VhdlIdentifierForm::unknown
            && name_form == library::VhdlIdentifierForm::unknown)
        || (indexed.primary_name_form
                == library::VhdlIdentifierForm::unknown
            && !indexed.primary_name.empty()
            && primary_name_form == library::VhdlIdentifierForm::unknown)
        || (indexed.name_form != library::VhdlIdentifierForm::unknown
            && indexed.name_form != name_form)
        || (indexed.primary_name_form
                != library::VhdlIdentifierForm::unknown
            && indexed.primary_name_form != primary_name_form)) {
        diagnostics.error(std::string { workspace_object_diagnostic },
            "checksummed compiled HIR does not provide a unique compatible VHDL identifier form");
        return std::nullopt;
    }

    auto result = indexed;
    result.name_form = name_form;
    result.primary_name_form = primary_name_form;
    return result;
}

std::optional<CompilationWorkspace> load_workspace_objects(
    const std::span<const WorkspaceObjectSelection> objects,
    diagnostic::Engine& diagnostics,
    const bool validate_uvm_surface)
{
    const auto ordered = ordered_workspace_objects(objects, diagnostics);
    if (!ordered) {
        return std::nullopt;
    }
    CompilationWorkspace result;
    std::vector<semantic::CompiledDesign> bundles;
    std::set<std::string> known_units;
    std::map<std::string, std::string> known_sources;
    CompilerEnvironment environment;
    // The provider has the largest compiler package set; every other set
    // must be contained in it, or the objects use one set each as before.
    {
        std::vector<std::pair<std::filesystem::path,
            std::vector<library::VhdlPackageDependency>>> sets;
        for (const auto* entry : *ordered) {
            if (entry->active_units.empty()) {
                continue;
            }
            diagnostic::Engine quiet;
            const auto metadata = artifact::load_object_metadata(entry->path, quiet);
            if (!metadata) {
                sets.clear();
                break;
            }
            if (!metadata->vhdl_package_dependencies.empty()) {
                sets.emplace_back(entry->path, metadata->vhdl_package_dependencies);
            }
        }
        const auto largest = std::ranges::max_element(sets, { },
            [](const auto& item) { return item.second.size(); });
        if (largest != sets.end()
            && std::ranges::all_of(sets, [&](const auto& item) {
                   return std::ranges::all_of(item.second, [&](const auto& dependency) {
                       return std::ranges::find(largest->second, dependency)
                           != largest->second.end();
                   });
               })) {
            environment.provider = largest->first;
            environment.provided = largest->second;
        }
    }
    compiler::CacheKeyBuilder uvm_identity;
    uvm_identity.add("uvm-provenance-schema", "fsim-uvm-object-v1");
    for (const auto* entry : *ordered) {
        const auto& selection = *entry;
        if (selection.active_units.empty()) {
            continue;
        }
        const std::array paths { selection.path };
        auto input = load_object_workspace(paths, diagnostics, false);
        if (!input) {
            return std::nullopt;
        }
        auto metadata = artifact::load_object_metadata(
            selection.path, diagnostics);
        if (!metadata) {
            return std::nullopt;
        }
        const auto selected = resolve_selection(
            selection, *metadata, *input, known_units, diagnostics);
        if (!selected) {
            return std::nullopt;
        }
        const auto libraries = select_compiler_environment(
            selection.path, *metadata, *input, environment, diagnostics);
        if (!libraries) {
            return std::nullopt;
        }
        auto projected = semantic::extract_compiled_objects(*input,
            selected->units, selected->classes, selected->udps, *libraries);
        if (!projected.ok()) {
            diagnostics.error(std::string { workspace_object_diagnostic },
                "cannot project managed object selection: " + projected.error);
            return std::nullopt;
        }
        if (!merge_trace_archive(result, *input, diagnostics)
            || !append_selected_sources(result, *input, *projected.design,
                known_sources, diagnostics)) {
            return std::nullopt;
        }
        const auto release = input->systemverilog_uvm_provenance.release;
        if (release != project::SystemVerilogUvmRelease::none) {
            auto& selected_release = result.systemverilog_uvm_provenance.release;
            if (selected_release != project::SystemVerilogUvmRelease::none
                && selected_release != release) {
                diagnostics.error("FSIM-UVM-VERSION-001",
                    "workspace objects select incompatible governed UVM releases");
                return std::nullopt;
            }
            selected_release = release;
        }
        uvm_identity.add("object-release", metadata->uvm_release);
        uvm_identity.add("object-compilation", metadata->compilation_digest);
        auto provenance = std::move(input->objects.front());
        provenance.unit_checksums.assign(selection.active_units.size(),
            metadata->compiled_hir_checksum);
        result.objects.push_back(std::move(provenance));
        bundles.push_back(std::move(*projected.design));
    }
    const auto linked = install_linked_compiled_design(result, std::move(bundles));
    if (!linked.ok()) {
        diagnostics.error(linked.diagnostic_code.empty()
                ? std::string { workspace_object_diagnostic }
                : linked.diagnostic_code,
            "cannot link managed objects: " + linked.error);
        return std::nullopt;
    }
    if (!result.semantics.valid()) {
        diagnostics.error(std::string { workspace_object_diagnostic },
            "managed objects produced an invalid semantic model");
        return std::nullopt;
    }
    install_compiled_class_specializations(result);
    if (validate_uvm_surface && !validate_uvm_environment(result, diagnostics)) {
        return std::nullopt;
    }
    result.source_count = result.hdl_sources.size();
    if (result.systemverilog_uvm_provenance.release
        != project::SystemVerilogUvmRelease::none) {
        result.systemverilog_uvm_provenance.source_identity = uvm_identity.finish();
    }
    return result;
}

} // namespace fsim::app::application_detail
