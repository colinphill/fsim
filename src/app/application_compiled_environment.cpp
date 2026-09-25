// SPDX-License-Identifier: Apache-2.0
#include "application_compiled_environment.hpp"

#include "application_internal.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <set>
#include <tuple>

namespace fsim::app::application_detail {
namespace {

std::string_view library_name(const std::string_view library)
{
    return library.empty() ? std::string_view { "work" } : library;
}

bool same_unit_identity(
    const semantic::Unit& left, const semantic::Unit& right)
{
    return left.language == right.language && left.kind == right.kind
        && library_name(left.library) == library_name(right.library)
        && left.name == right.name
        && left.secondary_name == right.secondary_name;
}

bool shared_compiler_package(const semantic::vhdl::Unit& left,
    const semantic::vhdl::Unit& right)
{
    return !left.standard_package_revision.empty()
        && !right.standard_package_revision.empty()
        && left.kind == right.kind && left.library == right.library
        && left.name == right.name
        && left.primary_name == right.primary_name;
}

bool external_unit(
    const semantic::CompiledDesign& design, const semantic::UnitId id)
{
    const auto unit = std::ranges::find(
        design.systemverilog_hir.units(), id, &semantic::sv::Unit::id);
    return unit != design.systemverilog_hir.units().end() && unit->external;
}

std::filesystem::path unit_source_owner(const semantic::CompiledDesign& design,
    const semantic::SourceSpanId source_span,
    const std::span<const ParsedSourceUnitOwner> owners,
    const std::filesystem::path& base_directory)
{
    if (!source_span.valid()
        || source_span.value() >= design.semantics.source_spans().size()) {
        return { };
    }
    const auto& span = design.semantics.source_spans()[source_span.value()];
    if (!span.file.valid()
        || span.file.value() >= design.semantics.source_files().size()) {
        return { };
    }
    const auto& file = design.semantics.source_files()[span.file.value()];
    const auto path = support::path_from_utf8(file.physical_name);
    const auto owner = std::ranges::find_if(owners, [&](const auto& candidate) {
        return candidate.offset == span.begin.offset
            && same_source_path(support::path_from_utf8(candidate.physical_name), path);
    });
    const auto source = owner == owners.end() ? path : owner->source;
    return (source.is_absolute() ? source : base_directory / source)
        .lexically_normal();
}

} // namespace

bool compiled_package_has_member(const semantic::CompiledDesign& design,
    const std::string_view library, const std::string_view package,
    const std::string_view member)
{
    const semantic::CompiledDesignResolver resolver { design, { } };
    const auto contains = [&](const semantic::sv::Unit& provider) {
        return !resolver.resolve_systemverilog_package_member(
            provider.library, provider.name, member).candidates.empty();
    };
    if (const auto provider = resolver.find_systemverilog_package(
            library_name(library), package)) {
        return provider->systemverilog && contains(*provider->systemverilog);
    }
    if (package.find("::") != std::string_view::npos) {
        return false;
    }
    // Provider ambiguity is diagnosed during analysis. Avoid manufacturing a
    // local net that would hide the imported name before that check can run.
    return std::ranges::any_of(design.systemverilog_units(), [&](const auto& provider) {
        return provider.kind == semantic::sv::UnitKind::package
            && provider.name == package && contains(provider);
    });
}

std::vector<CompiledSourceUnitIdentity> compiled_source_unit_identities(
    CompilationWorkspace& checked, const project::Config& config,
    const std::span<const ParsedSourceUnitOwner> owners)
{
    std::set<std::string> libraries;
    for (const auto& source_set : config.source_sets) {
        if (source_set.language != project::Language::systemc)
            libraries.emplace(library_name(source_set.library));
    }
    std::vector<CompiledSourceUnitIdentity> result;
    for (const auto& unit : checked.semantics.units()) {
        if (libraries.contains(std::string { library_name(unit.library) })) {
            result.push_back({ unit, external_unit(checked, unit.id),
                unit_source_owner(checked, unit.source, owners,
                    config.base_directory) });
        }
    }
    checked.source_class_identities.clear();
    checked.source_class_paths.clear();
    for (const auto& declaration : checked.systemverilog_hir.classes()) {
        const auto* owner = compiled_class_owner(checked, declaration);
        if (owner == nullptr
            || !libraries.contains(std::string { library_name(owner->library) })) {
            continue;
        }
        checked.source_class_identities.push_back(
            semantic::sv::class_declaration_identity(declaration));
        checked.source_class_paths.push_back(unit_source_owner(checked,
            declaration.source, owners, config.base_directory));
    }
    checked.source_udp_identities.clear();
    checked.source_udp_paths.clear();
    for (const auto& declaration : checked.systemverilog_hir.udps()) {
        const auto library = std::string { library_name(declaration.library) };
        if (!libraries.contains(library))
            continue;
        checked.source_udp_identities.push_back({ library, declaration.name });
        checked.source_udp_paths.push_back(unit_source_owner(checked,
            declaration.source, owners, config.base_directory));
    }
    return result;
}

bool install_compiled_environment(CompilationWorkspace& checked,
    const semantic::CompiledDesign& imported, diagnostic::Engine& diagnostics)
{
    std::vector<semantic::UnitId> selected;
    selected.reserve(imported.semantics.units().size());
    for (const auto& unit : imported.semantics.units())
        selected.push_back(unit.id);

    // Each object carries its compiler-owned VHDL package environment. The
    // request's copy supplies shared packages, while imported packages that
    // this request did not need remain available to its existing consumers.
    for (const auto& unit : imported.vhdl_hir.units()) {
        if (unit.standard_package_revision.empty())
            continue;
        const auto local = std::ranges::find_if(checked.vhdl_hir.units(),
            [&](const auto& candidate) {
                return shared_compiler_package(unit, candidate);
            });
        if (local == checked.vhdl_hir.units().end())
            continue;
        if (unit.standard != local->standard
            || unit.compatibility_profile != local->compatibility_profile
            || unit.standard_package_revision != local->standard_package_revision
            || unit.predefined_environment.identity
                != local->predefined_environment.identity) {
            diagnostics.error("FSIM-ART-VHDEP-001",
                "compiled library selects a different compiler-supplied VHDL "
                "package environment for '" + unit.library + "." + unit.name
                    + "'; recompile the library with this source standard "
                      "and fsim build");
            return false;
        }
        std::erase(selected, unit.id);
    }

    std::vector<std::string> classes;
    for (const auto& declaration : imported.systemverilog_hir.classes())
        classes.push_back(semantic::sv::class_declaration_identity(declaration));
    std::vector<semantic::CompiledUdpIdentity> udps;
    for (const auto& declaration : imported.systemverilog_hir.udps()) {
        udps.push_back({ std::string { library_name(declaration.library) },
            declaration.name });
    }
    auto environment = semantic::extract_compiled_objects(
        imported, selected, classes, udps);
    if (!environment.ok()) {
        diagnostics.error("FSIM-SEM-0003",
            "cannot project compiled library environment: " + environment.error);
        return false;
    }
    std::vector<semantic::CompiledDesign> inputs;
    inputs.push_back(std::move(*environment.design));
    inputs.push_back(std::move(static_cast<semantic::CompiledDesign&>(checked)));
    auto linked = install_linked_compiled_design(checked, std::move(inputs));
    if (!linked.ok()) {
        diagnostics.error(linked.diagnostic_code.empty()
                ? "FSIM-SEM-0003" : linked.diagnostic_code,
            "cannot link compiled library environment: " + linked.error);
        return false;
    }
    return true;
}

bool identify_compiled_source_units(CompilationWorkspace& checked,
    const std::span<const CompiledSourceUnitIdentity> identities,
    diagnostic::Engine& diagnostics)
{
    checked.source_units.clear();
    checked.source_units.reserve(identities.size());
    checked.source_unit_paths.clear();
    checked.source_unit_paths.reserve(identities.size());
    for (const auto& identity : identities) {
        const auto found = std::ranges::find_if(checked.semantics.units(),
            [&](const auto& unit) {
                return same_unit_identity(unit, identity.unit)
                    && external_unit(checked, unit.id) == identity.external;
            });
        if (found == checked.semantics.units().end()) {
            diagnostics.error("FSIM-SEM-0003",
                "source definition disappeared while linking compiled libraries: '"
                    + std::string { library_name(identity.unit.library) } + "."
                    + identity.unit.name + "'");
            return false;
        }
        checked.source_units.push_back(found->id);
        checked.source_unit_paths.push_back(identity.source);
    }
    return true;
}

void remember_compiled_references(const semantic::CompiledDesign& design,
    std::vector<CompiledReferenceIdentity>& identities)
{
    for (const auto& reference : design.references()) {
        if (!reference.owner.valid() || !reference.target
            || !reference.target->valid()
            || reference.owner.value() >= design.semantics.units().size()
            || reference.target->value() >= design.semantics.units().size()) {
            continue;
        }
        identities.push_back({ design.semantics.units()[reference.owner.value()],
            design.semantics.units()[reference.target->value()], reference });
    }
}

void restore_compiled_references(semantic::CompiledDesign& design,
    const std::span<const CompiledReferenceIdentity> identities)
{
    const auto key = [](const semantic::CompiledReference& reference) {
        return std::tuple { reference.owner, reference.kind, reference.library,
            reference.name, reference.secondary_name };
    };
    std::set<decltype(key(semantic::CompiledReference { }))> present;
    for (const auto& reference : design.references())
        present.insert(key(reference));
    const auto find = [&](const semantic::Unit& identity) {
        return std::ranges::find_if(design.semantics.units(),
            [&](const auto& unit) { return same_unit_identity(unit, identity); });
    };
    for (const auto& identity : identities) {
        const auto owner = find(identity.owner);
        const auto target = find(identity.target);
        if (owner == design.semantics.units().end()
            || target == design.semantics.units().end()) {
            continue;
        }
        auto reference = identity.reference;
        reference.owner = owner->id;
        reference.target = target->id;
        // The original expression may have been folded away and its source ID
        // relocated. Its owning unit supplies stable valid provenance.
        reference.source = owner->source;
        if (present.insert(key(reference)).second)
            design.mutable_references().push_back(std::move(reference));
    }
    std::ranges::stable_sort(design.mutable_references(),
        [&](const auto& left, const auto& right) { return key(left) < key(right); });
}

} // namespace fsim::app::application_detail
