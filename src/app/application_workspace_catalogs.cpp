// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_internal.hpp"

#include "fsim/artifact/object.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <set>
#include <optional>
#include <unordered_map>
#include <tuple>
#include <vector>

namespace fsim::app::application_detail {
namespace {

    const workspace::ArtifactRecord* provider(const WorkspaceCatalogs& catalogs,
        const workspace::UnitDependency& dependency)
    {
        for (const auto& catalog : catalogs) {
            if (catalog.location.name != dependency.library) {
                continue;
            }
            for (const auto& artifact : catalog.artifacts) {
                if (std::ranges::any_of(artifact.units, [&](const auto& owned) {
                        return workspace::unit_identity(owned.unit)
                            == workspace::unit_identity(dependency.unit);
                    })) {
                    return &artifact;
                }
            }
        }
        return nullptr;
    }

    bool dependency_reference(const semantic::CompiledReference& reference)
    {
        using Kind = semantic::CompiledReferenceKind;
        return reference.kind == Kind::package || reference.kind == Kind::import
            || reference.kind == Kind::entity
            || reference.kind == Kind::architecture
            || reference.kind == Kind::configuration
            || reference.kind == Kind::context
            || reference.kind == Kind::class_declaration;
    }

    std::optional<semantic::CompiledUnitView> reference_target(
        const semantic::CompiledDesign& design,
        const semantic::CompiledReference& reference)
    {
        return reference.target ? design.find_unit(*reference.target) : std::nullopt;
    }

    // Classes by declaration identity, with their owning unit, built once
    // per dependency walk instead of scanned per reference and unit.
    struct ClassOwnerEntry {
        semantic::UnitId owner;
        const semantic::sv::ClassDeclaration* declaration { };
    };
    using ClassIdentityIndex
        = std::unordered_multimap<std::string, ClassOwnerEntry>;

    ClassIdentityIndex class_identity_index(
        const semantic::CompiledDesign& design)
    {
        ClassIdentityIndex index;
        index.reserve(design.systemverilog_hir.classes().size());
        for (const auto& declaration : design.systemverilog_hir.classes()) {
            const auto* owner = compiled_class_owner(design, declaration);
            if (owner == nullptr) {
                continue;
            }
            index.emplace(semantic::sv::class_declaration_identity(declaration),
                ClassOwnerEntry { owner->id, &declaration });
        }
        return index;
    }

    bool reference_matches(const semantic::CompiledDesign& design,
        const semantic::CompiledReference& reference,
        const library::UnitIndexEntry& unit,
        const ClassIdentityIndex& classes)
    {
        const auto target = reference_target(design, reference);
        if (!target || !target->identity) {
            return false;
        }
        const auto language = target->identity->language;
        const auto expected_language = language == semantic::Language::vhdl ? "vhdl"
            : language == semantic::Language::verilog ? "verilog" : "systemverilog";
        if (unit.language != expected_language) {
            return false;
        }
        using Kind = semantic::CompiledReferenceKind;
        switch (reference.kind) {
        case Kind::package:
        case Kind::import:
            if (unit.kind != "package" && unit.kind != "package_body") {
                return false;
            }
            break;
        case Kind::entity:
            if (unit.kind != "entity") {
                return false;
            }
            break;
        case Kind::architecture:
            if (unit.kind != "architecture") {
                return false;
            }
            break;
        case Kind::configuration:
            if (unit.kind != "configuration") {
                return false;
            }
            break;
        case Kind::context:
            if (unit.kind != "context") {
                return false;
            }
            break;
        case Kind::class_declaration:
            if (unit.kind != "class") {
                return false;
            }
            {
                const auto [first, last] = classes.equal_range(unit.name);
                return std::any_of(first, last, [&](const auto& entry) {
                    return entry.second.owner == target->identity->id
                        && (unit.name == reference.secondary_name
                            || entry.second.declaration->canonical_identity
                                == reference.secondary_name);
                });
            }
        default:
            return false;
        }
        if (unit.language == "vhdl") {
            if (target->vhdl == nullptr) {
                return false;
            }
            const auto expected_name_form
                = vhdl_identifier_form(target->vhdl->extended_name);
            if ((reference.kind == Kind::architecture
                    || reference.kind == Kind::configuration)
                && (unit.primary_name.empty()
                    || target->vhdl->primary_name.empty())) {
                return false;
            }
            const auto expected_primary_form = unit.primary_name.empty()
                ? library::VhdlIdentifierForm::unknown
                : target->vhdl->primary_name.empty()
                    ? expected_name_form
                    : vhdl_identifier_form(
                        target->vhdl->extended_primary_name);
            const auto expected_primary_name
                = target->vhdl->primary_name.empty()
                ? std::string_view { target->vhdl->name }
                : std::string_view { target->vhdl->primary_name };
            if (unit.name_form != expected_name_form
                || unit.primary_name_form != expected_primary_form) {
                return false;
            }
            if (!vhdl_identifier_matches(target->vhdl->name,
                    expected_name_form, unit.name, unit.name_form)) {
                return false;
            }
            if (!unit.primary_name.empty()
                && !vhdl_identifier_matches(unit.primary_name,
                    unit.primary_name_form, expected_primary_name,
                    expected_primary_form)) {
                return false;
            }
            return true;
        }
        return workspace_name_equal(
            target->identity->name, unit.name, unit.language);
    }

    struct TopName {
        std::string language;
        std::string library;
        std::string name;
        std::string architecture;
        fsim::library::VhdlIdentifierForm name_form {
            fsim::library::VhdlIdentifierForm::unknown
        };
        fsim::library::VhdlIdentifierForm architecture_form {
            fsim::library::VhdlIdentifierForm::unknown
        };
    };

    std::optional<TopName> parse_top(std::string_view target)
    {
        TopName result;
        if (const auto colon = target.find(':'); colon != std::string_view::npos) {
            result.language = target.substr(0, colon);
            target.remove_prefix(colon + 1);
            if (result.language == "sv") {
                result.language = "systemverilog";
            } else if (result.language != "verilog" && result.language != "vhdl"
                && result.language != "systemverilog" && result.language != "systemc") {
                return std::nullopt;
            }
        }
        if (result.language == "vhdl"
            || support::is_extended_vhdl_identifier(target)) {
            if (result.language.empty()) {
                result.language = "vhdl";
            }
            support::VhdlTargetSpelling spelling;
            if (!support::split_vhdl_target(target, spelling)) {
                return std::nullopt;
            }
            if (spelling.has_architecture) {
                result.architecture_form = vhdl_identifier_form(
                    support::is_extended_vhdl_identifier(
                        spelling.architecture));
                result.architecture
                    = support::decode_vhdl_identifier(spelling.architecture);
            }
            if (spelling.has_library) {
                result.library
                    = support::decode_vhdl_identifier(spelling.library);
            }
            result.name_form = vhdl_identifier_form(
                support::is_extended_vhdl_identifier(spelling.unit));
            result.name = support::decode_vhdl_identifier(spelling.unit);
            return result.name.empty()
                ? std::nullopt : std::optional { result };
        }
        if (const auto open = target.find('('); open != std::string_view::npos) {
            if (target.empty() || target.back() != ')' || open + 2 >= target.size()) {
                return std::nullopt;
            }
            result.architecture = target.substr(open + 1, target.size() - open - 2);
            target = target.substr(0, open);
        }
        if (const auto dot = target.rfind('.'); dot != std::string_view::npos) {
            result.library = target.substr(0, dot);
            target.remove_prefix(dot + 1);
        }
        result.name = target;
        result.name_form = library::VhdlIdentifierForm::basic;
        if (!result.architecture.empty()) {
            result.architecture_form = library::VhdlIdentifierForm::basic;
        }
        return result.name.empty() ? std::nullopt : std::optional { result };
    }

    bool is_top(const library::UnitIndexEntry& unit)
    {
        return unit.kind == "module" || unit.kind == "entity"
            || unit.kind == "configuration" || unit.kind == "program";
    }

} // namespace

bool workspace_error(diagnostic::Engine& diagnostics, std::string message)
{
    diagnostics.error("FSIM-WS-001", std::move(message));
    return false;
}

std::optional<WorkspaceCatalogs> workspace_catalogs(const workspace::Store& store,
    const workspace::LibraryCatalog* target, diagnostic::Engine& diagnostics)
{
    std::string error;
    auto locations = store.library_locations(error);
    if (!locations) {
        workspace_error(diagnostics, std::move(error));
        return std::nullopt;
    }
    WorkspaceCatalogs result;
    if (target) {
        result.push_back(*target);
    }
    for (const auto& location : *locations) {
        if (target && location.name == target->location.name) {
            continue;
        }
        auto catalog = store.read_library(location.name, error);
        if (!catalog) {
            workspace_error(diagnostics, std::move(error));
            return std::nullopt;
        }
        result.push_back(std::move(*catalog));
    }
    return result;
}

std::vector<WorkspaceObjectSelection> workspace_object_selections(
    const workspace::Store& store, const WorkspaceCatalogs& catalogs,
    std::string_view replacement_library, std::span<const std::string> replaced_sources,
    diagnostic::Engine& diagnostics)
{
    std::vector<WorkspaceObjectSelection> result;
    for (std::size_t rank = 0; rank < catalogs.size(); ++rank) {
        const auto& catalog = catalogs[rank];
        for (const auto& artifact : catalog.artifacts) {
            if (artifact.kind != workspace::ArtifactKind::Hdl) {
                continue;
            }
            WorkspaceObjectSelection selection;
            for (const auto& owned : artifact.units) {
                if (catalog.location.name == replacement_library
                    && std::ranges::find(replaced_sources, owned.source_key)
                        != replaced_sources.end()) {
                    continue;
                }
                selection.active_units.push_back(owned.unit);
            }
            if (selection.active_units.empty()) {
                continue;
            }
            std::string error;
            auto path = store.artifact_path(catalog.location, artifact, error);
            if (!path) {
                workspace_error(diagnostics, std::move(error));
                return { };
            }
            selection.path = std::move(*path);
            selection.catalog_rank = rank;
            const auto metadata = artifact::load_object_metadata(selection.path, diagnostics);
            if (!metadata || metadata->compilation_digest != artifact.fingerprint) {
                if (!diagnostics.has_error()) {
                    workspace_error(diagnostics, "compiled object fingerprint differs from the library catalog: " + catalog.location.name + "." + artifact.id);
                }
                return { };
            }
            result.push_back(std::move(selection));
        }
    }
    return result;
}

bool validate_workspace_dependencies(const WorkspaceCatalogs& catalogs,
    diagnostic::Engine& diagnostics)
{
    for (const auto& catalog : catalogs) {
        for (const auto& artifact : catalog.artifacts) {
            for (const auto& dependency : artifact.dependencies) {
                const auto* current = provider(catalogs, dependency);
                if (current && current->fingerprint == dependency.fingerprint) {
                    continue;
                }
                diagnostics.error("FSIM-WS-002",
                    "library '" + catalog.location.name + "' object '" + artifact.id
                        + "' must be recompiled: dependency '" + dependency.library
                        + "." + dependency.unit.name + "' has changed or was deleted");
                return false;
            }
        }
    }
    return true;
}

std::vector<workspace::UnitDependency> workspace_dependencies(
    const semantic::CompiledDesign& design,
    std::span<const semantic::UnitId> owners, const WorkspaceCatalogs& catalogs)
{
    std::vector<workspace::UnitDependency> result;
    std::set<std::tuple<std::string, std::string, std::string>> seen;
    std::vector<semantic::UnitId> pending_units(owners.begin(), owners.end());
    std::set<semantic::UnitId> visited_units;
    std::vector<const workspace::ArtifactRecord*> pending_artifacts;
    std::set<const workspace::ArtifactRecord*> visited_artifacts;
    const auto append = [&](const workspace::UnitDependency& dependency) {
        if (seen.emplace(dependency.library, workspace::unit_identity(dependency.unit),
                    dependency.fingerprint)
                .second) {
            result.push_back(dependency);
        }
    };
    std::unordered_map<std::uint32_t,
        std::vector<const semantic::CompiledReference*>>
        references_by_owner;
    for (const auto& reference : design.references()) {
        if (dependency_reference(reference)) {
            references_by_owner[reference.owner.value()].push_back(&reference);
        }
    }
    std::optional<ClassIdentityIndex> classes;
    std::set<std::tuple<semantic::CompiledReferenceKind, std::uint32_t,
        std::string>>
        scanned_targets;
    for (std::size_t index = 0; index < pending_units.size(); ++index) {
        const auto owner = pending_units[index];
        if (!visited_units.insert(owner).second) {
            continue;
        }
        const auto owned_references = references_by_owner.find(owner.value());
        if (owned_references == references_by_owner.end()) {
            continue;
        }
        for (const auto* reference_pointer : owned_references->second) {
            const auto& reference = *reference_pointer;
            const auto target = reference_target(design, reference);
            if (!target || !target->identity) {
                continue;
            }
            pending_units.push_back(target->identity->id);
            // References differing only in owner or source select the same
            // providers; scan the catalogs once per distinct target.
            if (!scanned_targets
                     .emplace(reference.kind, target->identity->id.value(),
                         reference.secondary_name)
                     .second) {
                continue;
            }
            const auto library = target->identity->library.empty()
                ? std::string_view { "work" }
                : std::string_view { target->identity->library };
            if (target->vhdl && target->vhdl->kind == semantic::vhdl::UnitKind::package) {
                // The body can supply folded constants and subprogram results.
                // Its own imports must remain part of the consumer's identity.
                for (const auto& unit : design.vhdl_units()) {
                    if (unit.kind == semantic::vhdl::UnitKind::package
                        && workspace_name_equal(unit.library, library, "vhdl")
                        && unit.extended_name
                            == target->vhdl->extended_name
                        && (unit.extended_name
                                ? unit.name == target->vhdl->name
                                : workspace_name_equal(
                                      unit.name, target->vhdl->name, "vhdl"))) {
                        pending_units.push_back(unit.id);
                    }
                }
            }
            for (const auto& catalog : catalogs) {
                if (catalog.location.name != library) {
                    continue;
                }
                for (const auto& artifact : catalog.artifacts) {
                    for (const auto& owned : artifact.units) {
                        if (!classes) {
                            classes = class_identity_index(design);
                        }
                        if (!reference_matches(
                                design, reference, owned.unit, *classes)) {
                            continue;
                        }
                        append({ catalog.location.name, owned.unit, artifact.fingerprint });
                        pending_artifacts.push_back(&artifact);
                    }
                }
            }
        }
    }
    for (std::size_t index = 0; index < pending_artifacts.size(); ++index) {
        const auto* artifact = pending_artifacts[index];
        if (!visited_artifacts.insert(artifact).second) {
            continue;
        }
        for (const auto& dependency : artifact->dependencies) {
            // A provider may already contain values folded from an older
            // dependency. Preserve that fingerprint instead of blessing the
            // current provider during a later consumer compilation.
            append(dependency);
            const auto* current = provider(catalogs, dependency);
            if (current && current->fingerprint == dependency.fingerprint) {
                pending_artifacts.push_back(current);
            }
        }
    }
    return result;
}

bool resolve_workspace_tops(project::Config& config,
    const WorkspaceCatalogs& catalogs, diagnostic::Engine& diagnostics)
{
    if (config.project.tops.empty() && !config.project.top.empty()) {
        config.project.tops.push_back({ config.project.top, { } });
    }
    if (config.project.tops.empty()) {
        return workspace_error(diagnostics, "elaboration requires at least one top-level name");
    }
    for (auto& top : config.project.tops) {
        const auto parsed = parse_top(top.target);
        if (!parsed) {
            return workspace_error(diagnostics, "invalid top-level name '" + top.target + "'");
        }
        std::vector<std::string> candidates;
        for (const auto& catalog : catalogs) {
            if (!parsed->library.empty() && parsed->library != catalog.location.name) {
                continue;
            }
            for (const auto& artifact : catalog.artifacts) {
                for (const auto& owned : artifact.units) {
                    const auto& unit = owned.unit;
                    if (!is_top(unit)
                        || (!parsed->language.empty()
                            && parsed->language != unit.language)
                        || (unit.language == "vhdl"
                                ? !vhdl_identifier_matches(unit.name,
                                      unit.name_form, parsed->name,
                                      parsed->name_form)
                                : !workspace_name_equal(
                                      parsed->name, unit.name, unit.language))
                        || (!parsed->architecture.empty()
                            && unit.language != "vhdl")) {
                        continue;
                    }
                    const auto language = unit.language == "systemverilog" ? "sv" : unit.language;
                    auto candidate = language + ":" + catalog.location.name
                        + "."
                        + (unit.language == "vhdl"
                                ? support::encode_vhdl_identifier(unit.name,
                                      unit.name_form
                                          == library::VhdlIdentifierForm::
                                              extended)
                                : unit.name);
                    if (!parsed->architecture.empty()) {
                        candidate += "("
                            + support::encode_vhdl_identifier(
                                parsed->architecture,
                                parsed->architecture_form
                                    == library::VhdlIdentifierForm::extended)
                            + ")";
                    }
                    candidates.push_back(std::move(candidate));
                }
            }
        }
        std::ranges::sort(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
        if (candidates.empty()) {
            return workspace_error(diagnostics, "top-level '" + top.target
                + "' was not found in the workspace libraries");
        }
        if (candidates.size() > 1) {
            std::string alternatives;
            for (const auto& candidate : candidates) {
                alternatives += (alternatives.empty() ? "" : ", ") + candidate;
            }
            return workspace_error(diagnostics, "top-level '" + top.target
                + "' is ambiguous; select one of: " + alternatives);
        }
        top.target = std::move(candidates.front());
    }
    config.project.top = config.project.tops.front().target;
    return true;
}

} // namespace fsim::app::application_detail
