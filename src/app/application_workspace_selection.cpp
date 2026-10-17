// SPDX-License-Identifier: Apache-2.0
#include "application_workspace_selection.hpp"

#include "fsim/artifact/object.hpp"
#include "fsim/systemc/incremental.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <tuple>
#include <vector>

namespace fsim::app::application_detail {
namespace {

    std::string normalized_library(std::string_view library)
    {
        return std::string { library.empty() ? std::string_view { "work" } : library };
    }

    struct Target {
        std::string language;
        std::string library;
        std::string name;
        std::string architecture;
        library::VhdlIdentifierForm name_form {
            library::VhdlIdentifierForm::unknown
        };
        library::VhdlIdentifierForm architecture_form {
            library::VhdlIdentifierForm::unknown
        };
    };

    Target parse_target(std::string_view text)
    {
        Target result;
        const auto colon = text.find(':');
        if (colon != std::string_view::npos
            && (colon + 1 == text.size() || text[colon + 1] != ':')) {
            const auto language = text.substr(0, colon);
            if (language == "sv" || language == "systemverilog" || language == "verilog"
                || language == "vhdl" || language == "systemc") {
                result.language = language == "sv" ? "systemverilog" : language;
                text.remove_prefix(colon + 1);
            }
        }
        support::VhdlTargetSpelling spelling;
        if (support::split_vhdl_target(text, spelling)) {
            if (spelling.has_architecture) {
                result.architecture
                    = support::decode_vhdl_identifier(spelling.architecture);
                result.architecture_form = vhdl_identifier_form(
                    support::is_extended_vhdl_identifier(
                        spelling.architecture));
            }
            if (spelling.has_library) {
                result.library
                    = support::decode_vhdl_identifier(spelling.library);
            }
            text = spelling.unit;
        } else if (const auto dot = text.find('.');
            dot != std::string_view::npos && dot < text.find('\\')) {
            // A Verilog escaped identifier (`\$I178`) has no closing
            // backslash; only a library prefix before it is split off.
            result.library
                = support::decode_vhdl_identifier(text.substr(0U, dot));
            text.remove_prefix(dot + 1U);
        }
        const bool name_extended = support::is_extended_vhdl_identifier(text);
        result.name = result.language == "vhdl" || name_extended
            ? support::decode_vhdl_identifier(text) : std::string { text };
        result.name_form = vhdl_identifier_form(name_extended);
        return result;
    }

    bool hierarchy_unit(const library::UnitIndexEntry& unit)
    {
        return unit.kind == "module" || unit.kind == "entity" || unit.kind == "interface"
            || unit.kind == "program" || unit.kind == "configuration";
    }

    struct RecordLocation {
        std::size_t library;
        std::size_t artifact;
    };

    class Selector {
    public:
        Selector(const project::Config& config, const workspace::Store& store,
            const WorkspaceCatalogs& catalogs, diagnostic::Engine& diagnostics)
            : config_(config)
            , store_(store)
            , catalogs_(catalogs)
            , diagnostics_(diagnostics)
        {
            for (std::size_t library = 0; library < catalogs.size(); ++library) {
                for (std::size_t artifact = 0; artifact < catalogs[library].artifacts.size(); ++artifact) {
                    records_.push_back({ library, artifact });
                }
            }
            selected_.resize(records_.size());
        }

        std::optional<WorkspaceDesignSelection> run()
        {
            for (const auto& top : config_.project.tops) {
                root_libraries_.insert(parse_target(top.target).library);
            }
            for (const auto& top : config_.project.tops) {
                if (!select_target(parse_target(top.target), { })) {
                    workspace_error(diagnostics_, "selected top-level was not found: " + top.target);
                    return std::nullopt;
                }
            }
            for (const auto& binding : config_.bindings) {
                if (!binding.target) {
                    continue;
                }
                for (const auto& library : root_libraries_) {
                    select_target(parse_target(*binding.target), library);
                }
            }
            for (std::size_t index = 0; index < pending_.size(); ++index) {
                const auto record = pending_[index];
                select_dependencies(record);
                if (stale_dependency_) {
                    continue;
                }
                if (!scan_record(record)) {
                    return std::nullopt;
                }
            }
            return finish();
        }

    private:
        const workspace::ArtifactRecord& artifact(std::size_t index) const
        {
            const auto position = records_[index];
            return catalogs_[position.library].artifacts[position.artifact];
        }

        const workspace::LibraryLocation& location(std::size_t index) const
        {
            return catalogs_[records_[index].library].location;
        }

        void select_record(std::size_t index)
        {
            if (selected_[index]) {
                return;
            }
            selected_[index] = true;
            pending_.push_back(index);
            const auto& library = location(index).name;
            if (involved_libraries_.insert(library).second) {
                for (std::size_t candidate = 0; candidate < records_.size(); ++candidate) {
                    if (location(candidate).name == library
                        && std::ranges::any_of(artifact(candidate).units, [](const auto& owned) {
                            return owned.unit.kind == "bind";
                        })) {
                        select_record(candidate);
                    }
                }
            }
        }

        bool in_scope(std::string_view candidate, const Target& target,
            std::string_view owner_library) const
        {
            if (!target.library.empty()) {
                return workspace_name_equal(
                    candidate, target.library, target.language);
            }
            if (workspace_name_equal(
                    candidate, owner_library, target.language)) {
                return true;
            }
            return std::ranges::any_of(
                config_.elaboration.search_libraries, [&](const auto& library) {
                    return workspace_name_equal(
                        candidate, library, target.language);
                });
        }

        bool select_target(const Target& target, std::string_view owner_library,
            std::string_view required_kind = { }, bool include_architectures = true)
        {
            bool found { false };
            for (std::size_t index = 0; index < records_.size(); ++index) {
                if (!in_scope(location(index).name, target, owner_library)) {
                    continue;
                }
                for (const auto& owned : artifact(index).units) {
                    const auto& unit = owned.unit;
                    if ((!target.language.empty() && target.language != unit.language)
                        || (!required_kind.empty() ? unit.kind != required_kind : !hierarchy_unit(unit))
                        || (unit.language == "vhdl"
                            ? !vhdl_identifier_matches(unit.name,
                                  unit.name_form, target.name, target.name_form)
                            : !workspace_name_equal(
                                  unit.name, target.name, unit.language))) {
                        continue;
                    }
                    found = true;
                    select_record(index);
                    if (include_architectures && unit.language == "vhdl" && unit.kind == "entity") {
                        select_architectures(location(index).name, unit.name,
                            target.architecture, unit.name_form,
                            target.architecture_form);
                    }
                }
            }
            return found;
        }

        void select_architectures(std::string_view library,
            std::string_view entity, std::string_view architecture,
            const library::VhdlIdentifierForm entity_form,
            const library::VhdlIdentifierForm architecture_form)
        {
            for (std::size_t index = 0; index < records_.size(); ++index) {
                if (!workspace_name_equal(
                        location(index).name, library, "vhdl")) {
                    continue;
                }
                for (const auto& owned : artifact(index).units) {
                    const auto& unit = owned.unit;
                    if (unit.language == "vhdl" && unit.kind == "architecture"
                        && entity_form != library::VhdlIdentifierForm::unknown
                        && unit.primary_name_form == entity_form
                        && vhdl_identifier_matches(unit.primary_name,
                            unit.primary_name_form, entity, entity_form)
                        && (architecture.empty()
                            || (architecture_form
                                    != library::VhdlIdentifierForm::unknown
                                && vhdl_identifier_matches(unit.name,
                                    unit.name_form, architecture,
                                    architecture_form)))) {
                        select_record(index);
                    }
                }
            }
        }

        void select_package_body(std::string_view expected_library,
            const library::UnitIndexEntry& specification)
        {
            if (specification.language != "vhdl" || specification.kind != "package"
                || !specification.primary_name.empty()
                || specification.name_form
                    == library::VhdlIdentifierForm::unknown) {
                return;
            }
            for (std::size_t index = 0; index < records_.size(); ++index) {
                if (!workspace_name_equal(
                        location(index).name, expected_library, "vhdl")) {
                    continue;
                }
                for (const auto& owned : artifact(index).units) {
                    const auto& unit = owned.unit;
                    if (unit.language == "vhdl" && (unit.kind == "package" || unit.kind == "package_body")
                        && unit.name_form == specification.name_form
                        && unit.primary_name_form == specification.name_form
                        && vhdl_identifier_matches(unit.name, unit.name_form,
                            specification.name, specification.name_form)
                        && (unit.kind == "package_body" || !unit.primary_name.empty())) {
                        select_record(index);
                    }
                }
            }
        }

        void select_dependencies(std::size_t index)
        {
            for (const auto& dependency : artifact(index).dependencies) {
                bool found { false };
                for (std::size_t candidate = 0; candidate < records_.size(); ++candidate) {
                    if (location(candidate).name != dependency.library) {
                        continue;
                    }
                    if (std::ranges::none_of(artifact(candidate).units, [&](const auto& owned) {
                            return workspace::unit_identity(owned.unit) == workspace::unit_identity(dependency.unit);
                        })) {
                        continue;
                    }
                    found = true;
                    select_record(candidate);
                    if (artifact(candidate).fingerprint != dependency.fingerprint) {
                        stale_dependency_ = true;
                    }
                }
                if (!found) {
                    stale_dependency_ = true;
                }
            }
        }

        void select_hierarchy_scope(std::string_view library)
        {
            for (std::size_t index = 0; index < records_.size(); ++index) {
                if (location(index).name == library
                    && std::ranges::any_of(artifact(index).units, [](const auto& owned) {
                        return hierarchy_unit(owned.unit);
                    })) {
                    select_record(index);
                }
            }
        }

        void select_configuration(const semantic::sv::Unit& unit)
        {
            if (!unit.configuration) {
                return;
            }
            // Liblists apply to instance paths discovered during elaboration.
            // Preserve all potential hierarchy providers in those named scopes.
            for (const auto& library : unit.configuration->default_liblist) {
                select_hierarchy_scope(library);
            }
            for (const auto& rule : unit.configuration->rules) {
                for (const auto& library : rule.liblist) {
                    select_hierarchy_scope(library);
                }
            }
        }

        std::string owner_library(const semantic::CompiledDesign& design, semantic::ScopeId scope) const
        {
            if (!scope.valid() || scope.value() >= design.semantics.scopes().size()) {
                return "work";
            }
            const auto owner = design.find_unit(design.semantics.scopes()[scope.value()].unit);
            return owner ? normalized_library(owner->identity->library) : "work";
        }

        bool instance_reference(const semantic::CompiledDesign& design,
            const semantic::CompiledReference& reference) const
        {
            const auto matches = [&](const auto& instance) {
                return instance.source == reference.source && instance.scope.valid()
                    && instance.scope.value() < design.semantics.scopes().size()
                    && design.semantics.scopes()[instance.scope.value()].unit == reference.owner;
            };
            return std::ranges::any_of(design.systemverilog_hir.instances(), matches)
                || std::ranges::any_of(design.vhdl_hir.instances(), matches);
        }

        void select_instances(const semantic::CompiledDesign& design)
        {
            for (const auto& instance : design.systemverilog_hir.instances()) {
                select_target(parse_target(instance.target.spelling), owner_library(design, instance.scope),
                    instance.udp ? std::string_view { "primitive" } : std::string_view { });
            }
            for (const auto& instance : design.vhdl_hir.instances()) {
                const auto library = owner_library(design, instance.scope);
                auto target = parse_target(instance.target.spelling);
                target.language = "vhdl";
                const auto owner = instance.scope.valid()
                    && instance.scope.value() < design.semantics.scopes().size()
                    ? design.semantics.scopes()[instance.scope.value()].unit
                    : semantic::UnitId { };
                const auto unit_reference = std::ranges::find_if(
                    design.references(), [&](const auto& reference) {
                        return reference.owner == owner
                            && reference.source == instance.source
                            && (reference.kind
                                    == semantic::CompiledReferenceKind::entity
                                || reference.kind
                                    == semantic::CompiledReferenceKind::configuration)
                            && reference.target.has_value();
                    });
                if (!instance.component
                    && unit_reference != design.references().end()) {
                    const auto target_unit
                        = design.find_unit(*unit_reference->target);
                    if (target_unit && target_unit->vhdl
                        && target_unit->identity) {
                        target.name = target_unit->vhdl->name;
                        target.name_form = target_unit->vhdl->extended_name
                            ? library::VhdlIdentifierForm::extended
                            : library::VhdlIdentifierForm::basic;
                        if (target.library.empty()
                            || workspace_name_equal(
                                target.library, "work", "vhdl")) {
                            target.library = target_unit->identity->library;
                        }
                        const auto architecture_reference
                            = std::ranges::find_if(
                                design.references(), [&](const auto& reference) {
                                    if (reference.owner != owner
                                        || reference.source != instance.source
                                        || reference.kind
                                            != semantic::CompiledReferenceKind::architecture
                                        || !reference.target) {
                                        return false;
                                    }
                                    const auto architecture
                                        = design.find_unit(*reference.target);
                                    return architecture && architecture->vhdl
                                        && architecture->vhdl->kind
                                            == semantic::vhdl::UnitKind::architecture
                                        && architecture->vhdl->primary_name
                                            == target_unit->vhdl->name
                                        && architecture->vhdl->extended_primary_name
                                            == target_unit->vhdl->extended_name;
                                });
                        if (architecture_reference != design.references().end()) {
                            const auto architecture = design.find_unit(
                                *architecture_reference->target);
                            if (architecture && architecture->vhdl) {
                                target.architecture = architecture->vhdl->name;
                                target.architecture_form
                                    = architecture->vhdl->extended_name
                                    ? library::VhdlIdentifierForm::extended
                                    : library::VhdlIdentifierForm::basic;
                            }
                        }
                    }
                }
                if (instance.component) {
                    // A selected component name may be package.member, not
                    // library.entity. Its default-bound entity is searched
                    // from the owning architecture's library.
                    target.library = library;
                } else {
                    if (workspace_name_equal(target.library, "work", "vhdl")) {
                        target.library = library;
                    }
                }
                select_target(target, library, instance.configuration ? "configuration"
                    : instance.component ? "" : "entity");
            }
        }

        void select_reference(const semantic::CompiledDesign& design,
            const semantic::CompiledReference& reference)
        {
            using Kind = semantic::CompiledReferenceKind;
            const auto owner = design.find_unit(reference.owner);
            const auto library = owner ? normalized_library(owner->identity->library) : "work";
            const auto language = owner && owner->vhdl ? std::string_view { "vhdl" }
                : std::string_view { "systemverilog" };
            if (owner && owner->vhdl && !owner->vhdl->standard_package_revision.empty()) {
                return;
            }
            const auto target_unit = reference.target ? design.find_unit(*reference.target) : std::nullopt;
            if (target_unit && target_unit->vhdl
                && !target_unit->vhdl->standard_package_revision.empty()) {
                return;
            }
            if ((reference.kind == Kind::module || reference.kind == Kind::entity
                    || reference.kind == Kind::architecture || reference.kind == Kind::configuration)
                && instance_reference(design, reference)) {
                return;
            }
            if (reference.kind == Kind::package || reference.kind == Kind::import
                || reference.kind == Kind::context || reference.kind == Kind::class_declaration) {
                for (std::size_t index = 0; index < records_.size(); ++index) {
                    if (!workspace_name_equal(location(index).name,
                            reference.library, language)) {
                        continue;
                    }
                    for (const auto& owned : artifact(index).units) {
                        const auto& unit = owned.unit;
                        if (unit.language != language) {
                            continue;
                        }
                        const bool class_matches
                            = reference.kind == Kind::class_declaration
                            && unit.kind == "class"
                            && (unit.name == reference.secondary_name
                                || (reference.secondary_name.empty()
                                    && unit.name.ends_with("::" + reference.name)));
                        const bool unit_kind_matches
                            = reference.kind == Kind::context
                            ? unit.kind == "context"
                            : unit.kind == "package" && unit.primary_name.empty();
                        const bool unit_matches_form = unit.language == "vhdl"
                            ? target_unit && target_unit->vhdl
                                && vhdl_identifier_matches(unit.name,
                                    unit.name_form, target_unit->vhdl->name,
                                    vhdl_identifier_form(
                                        target_unit->vhdl->extended_name))
                            : workspace_name_equal(
                                  unit.name, reference.name, unit.language);
                        const bool matches = reference.kind == Kind::class_declaration
                            ? class_matches
                            : unit_kind_matches && unit_matches_form;
                        if (matches) {
                            select_record(index);
                            select_package_body(location(index).name, unit);
                        }
                    }
                }
                return;
            }
            Target target { { }, reference.library, reference.name, { } };
            if (reference.kind == Kind::architecture) {
                if (target_unit && target_unit->vhdl
                    && target_unit->vhdl->kind
                        == semantic::vhdl::UnitKind::architecture) {
                    const auto& architecture = *target_unit->vhdl;
                    select_architectures(
                        reference.library, architecture.primary_name,
                        architecture.name,
                        architecture.extended_primary_name
                            ? library::VhdlIdentifierForm::extended
                            : library::VhdlIdentifierForm::basic,
                        architecture.extended_name
                            ? library::VhdlIdentifierForm::extended
                            : library::VhdlIdentifierForm::basic);
                }
            } else if (reference.kind == Kind::entity) {
                const bool architecture_owner = owner && owner->vhdl
                    && owner->vhdl->kind == semantic::vhdl::UnitKind::architecture
                    && target_unit && target_unit->vhdl
                    && owner->vhdl->primary_name == target_unit->vhdl->name
                    && owner->vhdl->extended_primary_name
                        == target_unit->vhdl->extended_name
                    && reference.source == owner->vhdl->source;
                target.language = "vhdl";
                if (target_unit && target_unit->vhdl) {
                    target.name = target_unit->vhdl->name;
                    target.name_form = target_unit->vhdl->extended_name
                        ? library::VhdlIdentifierForm::extended
                        : library::VhdlIdentifierForm::basic;
                }
                const auto paired = std::ranges::find_if(design.references(), [&](const auto& candidate) {
                    if (candidate.kind != Kind::architecture
                        || candidate.owner != reference.owner
                        || candidate.library != reference.library
                        || candidate.name != reference.name
                        || candidate.source != reference.source) {
                        return false;
                    }
                    const auto paired_unit = candidate.target
                        ? design.find_unit(*candidate.target) : std::nullopt;
                    if (!target_unit || !target_unit->vhdl
                        || !paired_unit || !paired_unit->vhdl) {
                        return false;
                    }
                    return paired_unit->vhdl->primary_name
                            == target_unit->vhdl->name
                        && paired_unit->vhdl->extended_primary_name
                            == target_unit->vhdl->extended_name;
                });
                if (paired != design.references().end()) {
                    target.architecture = paired->secondary_name;
                    const auto paired_unit = paired->target
                        ? design.find_unit(*paired->target) : std::nullopt;
                    if (paired_unit && paired_unit->vhdl) {
                        target.architecture = paired_unit->vhdl->name;
                        target.architecture_form
                            = paired_unit->vhdl->extended_name
                            ? library::VhdlIdentifierForm::extended
                            : library::VhdlIdentifierForm::basic;
                    }
                }
                select_target(target, library, "entity", !architecture_owner);
            } else if (reference.kind == Kind::configuration) {
                if (target_unit && target_unit->vhdl) {
                    target.language = "vhdl";
                    target.name = target_unit->vhdl->name;
                    target.name_form = target_unit->vhdl->extended_name
                        ? library::VhdlIdentifierForm::extended
                        : library::VhdlIdentifierForm::basic;
                }
                select_target(target, library, "configuration");
            } else if (reference.kind == Kind::module || reference.kind == Kind::bind) {
                select_target(target, library);
            }
        }

        bool scan_record(std::size_t index)
        {
            const auto& record = artifact(index);
            std::string error;
            const auto path = store_.artifact_path(location(index), record, error);
            if (!path) {
                return workspace_error(diagnostics_, std::move(error));
            }
            if (record.kind == workspace::ArtifactKind::SystemCPlugin) {
                const auto metadata = systemc::load_incremental_plugin_metadata(*path, diagnostics_);
                if (!metadata) {
                    return false;
                }
                if (metadata->link_digest != record.fingerprint
                    || metadata->logical_library != location(index).name) {
                    return workspace_error(diagnostics_, "SystemC plugin differs from its library catalog");
                }
                for (const auto& owned : record.units) {
                    if (owned.unit.language != "systemc" || owned.unit.kind != "module"
                        || std::ranges::none_of(metadata->factories, [&](const auto& factory) {
                            return factory.name == owned.unit.name;
                        })) {
                        return workspace_error(diagnostics_, "SystemC factory differs from its library catalog");
                    }
                }
                std::set<std::string> scopes = root_libraries_;
                scopes.insert(location(index).name);
                scopes.insert(config_.elaboration.search_libraries.begin(), config_.elaboration.search_libraries.end());
                for (const auto& library : scopes) {
                    select_hierarchy_scope(library);
                }
                return true;
            }
            if (record.kind != workspace::ArtifactKind::Hdl) {
                return true;
            }
            const auto metadata = artifact::load_object_metadata(*path, diagnostics_);
            if (!metadata) {
                return false;
            }
            if (metadata->compilation_digest != record.fingerprint) {
                return workspace_error(diagnostics_, "compiled object fingerprint differs from its library catalog");
            }
            if (metadata->uvm_release != "none") {
                select_target({ "systemverilog", { }, "uvm_pkg", { } }, location(index).name, "package");
            }
            WorkspaceObjectSelection selected { *path, { } };
            for (const auto& owned : record.units) {
                selected.active_units.push_back(owned.unit);
                select_package_body(location(index).name, owned.unit);
            }
            const auto design = load_workspace_objects(std::array { selected }, diagnostics_, false);
            if (!design) {
                return false;
            }
            select_instances(*design);
            for (const auto& reference : design->references()) {
                select_reference(*design, reference);
            }
            for (const auto& unit : design->systemverilog_units()) {
                select_configuration(unit);
            }
            for (const auto& unit : design->vhdl_units()) {
                if (unit.kind == semantic::vhdl::UnitKind::configuration && !unit.primary_name.empty()) {
                    Target target { "vhdl", normalized_library(unit.library), unit.primary_name, { } };
                    target.name_form = unit.extended_primary_name
                        ? library::VhdlIdentifierForm::extended
                        : library::VhdlIdentifierForm::basic;
                    if (unit.configuration) {
                        const auto spelling = unit.configuration->block.text();
                        target.architecture
                            = support::decode_vhdl_identifier(spelling);
                        target.architecture_form = vhdl_identifier_form(
                            support::is_extended_vhdl_identifier(spelling));
                    }
                    select_target(target, unit.library, "entity");
                }
            }
            return true;
        }

        std::optional<WorkspaceDesignSelection> finish()
        {
            WorkspaceDesignSelection result;
            for (std::size_t library = 0; library < catalogs_.size(); ++library) {
                workspace::LibraryCatalog catalog { catalogs_[library].location, { } };
                for (std::size_t index = 0; index < records_.size(); ++index) {
                    if (!selected_[index] || records_[index].library != library) {
                        continue;
                    }
                    const auto& record = artifact(index);
                    catalog.artifacts.push_back(record);
                    std::string error;
                    const auto path = store_.artifact_path(location(index), record, error);
                    if (!path) {
                        workspace_error(diagnostics_, std::move(error));
                        return std::nullopt;
                    }
                    if (record.kind == workspace::ArtifactKind::Hdl) {
                        WorkspaceObjectSelection object { *path, { } };
                        object.catalog_rank = library;
                        for (const auto& owned : record.units) {
                            object.active_units.push_back(owned.unit);
                        }
                        result.objects.push_back(std::move(object));
                    } else if (record.kind == workspace::ArtifactKind::SystemCPlugin) {
                        result.plugins.push_back(*path);
                    }
                }
                if (!catalog.artifacts.empty()) {
                    result.catalogs.push_back(std::move(catalog));
                }
            }
            return result;
        }

        const project::Config& config_;
        const workspace::Store& store_;
        const WorkspaceCatalogs& catalogs_;
        diagnostic::Engine& diagnostics_;
        std::vector<RecordLocation> records_;
        std::vector<bool> selected_;
        std::vector<std::size_t> pending_;
        std::set<std::string> root_libraries_;
        std::set<std::string> involved_libraries_;
        bool stale_dependency_ { false };
    };

} // namespace

std::optional<WorkspaceDesignSelection> select_workspace_design(
    const project::Config& config, const workspace::Store& store,
    const WorkspaceCatalogs& catalogs, diagnostic::Engine& diagnostics)
{
    return Selector(config, store, catalogs, diagnostics).run();
}

} // namespace fsim::app::application_detail
