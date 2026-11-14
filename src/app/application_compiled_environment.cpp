// SPDX-License-Identifier: Apache-2.0
#include "application_compiled_environment.hpp"

#include "application_internal.hpp"
#include "fsim/semantic/compiled_design_linker.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

namespace fsim::app::application_detail {
namespace {

std::string_view library_name(const std::string_view library)
{
    return library.empty() ? std::string_view { "work" } : library;
}

bool same_unit_identity(
    const semantic::Unit& left, const semantic::Unit& right,
    const VhdlUnitIdentifierForms& left_vhdl_forms,
    const VhdlUnitIdentifierForms& right_vhdl_forms)
{
    const bool common_identity_matches
        = left.language == right.language && left.kind == right.kind
        && library_name(left.library) == library_name(right.library)
        && left.name == right.name
        && left.secondary_name == right.secondary_name;
    if (!common_identity_matches) {
        return false;
    }
    return left.language != semantic::Language::vhdl
        || (left_vhdl_forms.available && right_vhdl_forms.available
            && left_vhdl_forms.name_extended
                == right_vhdl_forms.name_extended
            && left_vhdl_forms.primary_name_extended
                == right_vhdl_forms.primary_name_extended);
}

VhdlUnitIdentifierForms vhdl_unit_identifier_forms(
    const semantic::CompiledDesign& design, const semantic::UnitId id)
{
    const auto unit = std::ranges::find(
        design.vhdl_hir.units(), id, &semantic::vhdl::Unit::id);
    if (unit == design.vhdl_hir.units().end()) {
        return { };
    }
    return { true, unit->extended_name, unit->extended_primary_name };
}

bool shared_compiler_package(const semantic::vhdl::Unit& left,
    const semantic::vhdl::Unit& right)
{
    return !left.standard_package_revision.empty()
        && !right.standard_package_revision.empty()
        && left.kind == right.kind && left.library == right.library
        && left.name == right.name
        && left.primary_name == right.primary_name
        && left.extended_name == right.extended_name
        && left.extended_primary_name == right.extended_primary_name;
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

frontend::SystemVerilogPackageMember compiled_package_member(
    const semantic::CompiledDesign& design, const std::string_view library,
    const std::string_view package, const std::string_view member)
{
    using Member = frontend::SystemVerilogPackageMember;
    const semantic::CompiledDesignResolver resolver { design, { } };
    const auto kind = [&](const semantic::sv::Unit& provider) {
        if (!resolver.resolve_systemverilog_package_class(
                provider.library, provider.name, member).candidates.empty()) {
            return Member::class_type;
        }
        auto result = Member::none;
        for (const auto id : resolver.resolve_systemverilog_package_member(
                 provider.library, provider.name, member).candidates) {
            const auto declaration = design.find_declaration(id);
            if (!declaration || declaration->systemverilog == nullptr) {
                continue;
            }
            const auto& record = *declaration->systemverilog;
            if (record.form == semantic::sv::DeclarationForm::type_parameter) {
                return Member::type;
            }
            if (record.form
                != semantic::sv::DeclarationForm::typedef_declaration) {
                result = Member::value;
                continue;
            }
            const auto* type = record.declared_type
                ? design.systemverilog_hir.find_type(*record.declared_type)
                : nullptr;
            return type != nullptr
                    && (type->base.container_form
                        || !type->base.unpacked_dimensions.empty())
                ? Member::unpacked_type
                : Member::type;
        }
        if (result == Member::none
            && !resolver.resolve_systemverilog_package_let(
                provider.library, provider.name, member).candidates.empty()) {
            result = Member::value;
        }
        return result;
    };
    if (const auto provider = resolver.find_systemverilog_package(
            library_name(library), package)) {
        return provider->systemverilog ? kind(*provider->systemverilog)
                                       : Member::none;
    }
    if (package.find("::") != std::string_view::npos) {
        return Member::none;
    }
    // Provider ambiguity is diagnosed during analysis. Avoid manufacturing a
    // local net that would hide the imported name before that check can run.
    for (const auto& provider : design.systemverilog_units()) {
        if (provider.kind == semantic::sv::UnitKind::package
            && provider.name == package) {
            if (const auto found = kind(provider); found != Member::none) {
                return found;
            }
        }
    }
    return Member::none;
}

std::optional<frontend::SystemVerilogPackageAssertions>
compiled_package_assertions(const semantic::CompiledDesign& design,
    const std::string_view library, const std::string_view package,
    std::vector<semantic::SourceFile>& files)
{
    const auto record_file = [&](const semantic::SourceSpanId id) {
        const auto& spans = design.semantics.source_spans();
        const auto& sources = design.semantics.source_files();
        if (!id.valid() || id.value() >= spans.size()) {
            return;
        }
        const auto file = spans[id.value()].file;
        if (!file.valid() || file.value() >= sources.size()
            || std::ranges::any_of(files, [&](const semantic::SourceFile& known) {
                   return known.physical_name
                       == sources[file.value()].physical_name;
               })) {
            return;
        }
        files.push_back(sources[file.value()]);
    };
    const auto tokens = [&](const semantic::sv::Unit& provider) {
        frontend::SystemVerilogPackageAssertions result;
        for (const auto& declaration : provider.assertion_declarations) {
            if (declaration.kind
                == semantic::sv::AssertionDeclarationKind::checker) {
                continue;
            }
            const bool sequence = declaration.kind
                == semantic::sv::AssertionDeclarationKind::sequence;
            const auto name_span
                = frontend_source_span(design.semantics, declaration.name_source);
            const auto word = [&](const std::string_view text,
                                  const frontend::TokenKind kind
                                  = frontend::TokenKind::Identifier) {
                frontend::Token token;
                token.kind = kind;
                token.text = std::string { text };
                token.span = name_span;
                return token;
            };
            record_file(declaration.name_source);
            for (const auto& token : declaration.header_tokens) {
                record_file(token.source);
            }
            for (const auto& token : declaration.body_tokens) {
                record_file(token.source);
            }
            std::vector<frontend::Token> source;
            source.reserve(declaration.header_tokens.size()
                + declaration.body_tokens.size() + 4U);
            source.push_back(word(sequence ? "sequence" : "property"));
            source.push_back(word(declaration.name));
            for (const auto& token : declaration.header_tokens) {
                source.push_back(frontend_source_token(design.semantics, token));
            }
            source.push_back(word(";", frontend::TokenKind::Semicolon));
            for (const auto& token : declaration.body_tokens) {
                source.push_back(frontend_source_token(design.semantics, token));
            }
            source.push_back(word(sequence ? "endsequence" : "endproperty"));
            result.sources.push_back(std::move(source));
        }
        return result;
    };
    const semantic::CompiledDesignResolver resolver { design, { } };
    if (const auto provider = resolver.find_systemverilog_package(
            library_name(library), package)) {
        if (provider->systemverilog == nullptr) {
            return std::nullopt;
        }
        return tokens(*provider->systemverilog);
    }
    if (package.find("::") != std::string_view::npos) {
        return std::nullopt;
    }
    // Provider ambiguity is diagnosed during analysis, as for the members.
    for (const auto& provider : design.systemverilog_units()) {
        if (provider.kind == semantic::sv::UnitKind::package
            && provider.name == package) {
            return tokens(provider);
        }
    }
    return std::nullopt;
}

frontend::SourceSpan frontend_source_span(
    const semantic::Model& semantics, const semantic::SourceSpanId id)
{
    frontend::SourceSpan output;
    if (!id.valid() || id.value() >= semantics.source_spans().size()) {
        return output;
    }
    const auto& input = semantics.source_spans()[id.value()];
    if (!input.file.valid()
        || input.file.value() >= semantics.source_files().size()) {
        return output;
    }
    const auto& file = semantics.source_files()[input.file.value()];
    output.source_name
        = input.logical_name.empty() ? file.physical_name : input.logical_name;
    output.physical_source_name = file.physical_name;
    output.begin = { static_cast<std::size_t>(input.begin.offset),
        static_cast<std::size_t>(input.begin.line),
        static_cast<std::size_t>(input.begin.column) };
    output.end = { static_cast<std::size_t>(input.end.offset),
        static_cast<std::size_t>(input.end.line),
        static_cast<std::size_t>(input.end.column) };
    std::vector<std::string> expansion_stack;
    auto expansion = input.expansion;
    while (expansion && expansion->valid()
        && expansion->value() < semantics.expansions().size()) {
        const auto& record = semantics.expansions()[expansion->value()];
        expansion_stack.push_back(record.description);
        expansion = record.parent;
    }
    for (auto iterator = expansion_stack.rbegin();
         iterator != expansion_stack.rend(); ++iterator) {
        output.expansion_stack.push_back(*iterator);
    }
    return output;
}

frontend::Token frontend_source_token(
    const semantic::Model& semantics, const semantic::sv::SourceToken& input)
{
    auto kind = frontend::TokenKind::EndOfFile;
    if (input.kind
        <= static_cast<std::uint16_t>(frontend::TokenKind::EndOfFile)) {
        kind = static_cast<frontend::TokenKind>(input.kind);
    }
    auto source_span = frontend_source_span(semantics, input.source);
    auto expansion_stack = source_span.expansion_stack;
    auto output = frontend::Token { kind, input.text, std::move(source_span),
        std::move(expansion_stack) };
    output.generated_text
        = static_cast<frontend::GeneratedTextKind>(input.generated_text);
    return output;
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
                    config.base_directory),
                vhdl_unit_identifier_forms(checked, unit.id) });
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
        // Units of standards in one package family mix: the package copies
        // differ only by the standard stamp, and the design keeps one set
        // (the newest standard's) when it is elaborated. The predefined
        // environment identity names the standard, so it is compared only
        // between copies of one standard.
        const bool same_standard = unit.standard == local->standard;
        if (!same_standard
            && vhdl_package_family(unit.standard)
                != vhdl_package_family(local->standard)) {
            diagnostics.error("FSIM-ART-VHDEP-001",
                vhdl_mixed_family_message(unit.standard, local->standard));
            return false;
        }
        if (unit.compatibility_profile != local->compatibility_profile
            || unit.standard_package_revision != local->standard_package_revision
            || (same_standard
                && unit.predefined_environment.identity
                    != local->predefined_environment.identity)) {
            diagnostics.error("FSIM-ART-VHDEP-001",
                "compiled library selects a different compiler-supplied VHDL "
                "package environment for '" + unit.library + "." + unit.name
                    + "'; recompile the library with this source standard "
                      "and fsim build");
            return false;
        }
        std::erase(selected, unit.id);
    }

    // Re-analysis replaces a library unit (IEEE 1076-2008 13.5): imported
    // VHDL units this compilation defines again are left out, together with
    // the secondary units of replaced primary units, which are obsolete.
    const auto secondary_unit = [](const semantic::vhdl::Unit& unit) {
        return unit.kind == semantic::vhdl::UnitKind::architecture
            || (unit.kind == semantic::vhdl::UnitKind::package
                && !unit.primary_name.empty());
    };
    const auto library_key = [](const std::string_view library) {
        std::string key { library_name(library) };
        std::ranges::transform(key, key.begin(), [](const char character) {
            return static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        });
        return key;
    };
    using VhdlUnitIdentity = std::tuple<semantic::vhdl::UnitKind,
        std::string, std::string, std::string, bool, bool>;
    std::set<VhdlUnitIdentity> redefined;
    std::set<std::tuple<std::string, std::string, bool>> replaced_primaries;
    for (const auto& unit : checked.vhdl_hir.units()) {
        if (!unit.standard_package_revision.empty())
            continue;
        const auto library = library_key(unit.library);
        redefined.emplace(unit.kind, library, unit.name, unit.primary_name,
            unit.extended_name, unit.extended_primary_name);
        if (!secondary_unit(unit))
            replaced_primaries.emplace(library, unit.name, unit.extended_name);
    }
    for (const auto& unit : imported.vhdl_hir.units()) {
        if (!unit.standard_package_revision.empty())
            continue;
        const auto library = library_key(unit.library);
        if (redefined.contains(
                { unit.kind, library, unit.name, unit.primary_name,
                    unit.extended_name, unit.extended_primary_name })
            || (secondary_unit(unit)
                && replaced_primaries.contains(
                    { library, unit.primary_name,
                        unit.extended_primary_name }))) {
            std::erase(selected, unit.id);
        }
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
                return same_unit_identity(unit, identity.unit,
                    vhdl_unit_identifier_forms(checked, unit.id),
                    identity.vhdl_forms)
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
        identities.push_back({
            design.semantics.units()[reference.owner.value()],
            design.semantics.units()[reference.target->value()], reference,
            vhdl_unit_identifier_forms(design, reference.owner),
            vhdl_unit_identifier_forms(design, *reference.target) });
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
    const auto find = [&](const semantic::Unit& identity,
                          const VhdlUnitIdentifierForms& identity_forms) {
        return std::ranges::find_if(design.semantics.units(),
            [&](const auto& unit) {
                return same_unit_identity(unit, identity,
                    vhdl_unit_identifier_forms(design, unit.id),
                    identity_forms);
            });
    };
    for (const auto& identity : identities) {
        const auto owner = find(identity.owner, identity.owner_vhdl_forms);
        const auto target = find(identity.target, identity.target_vhdl_forms);
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
