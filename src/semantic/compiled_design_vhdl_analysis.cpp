// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_vhdl_analysis.hpp"

#include "compiled_design_vhdl_analysis_internal.hpp"
#include "compiled_design_vhdl_typing_internal.hpp"
#include "fsim/project/project.hpp"
#include "fsim/semantic/vhdl_native_intrinsics.hpp"
#include "fsim/semantic/vhdl_standard_declaration.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fsim::semantic {

namespace {

using namespace vhdl_names;

diagnostic::SourceSpan to_span(const Model& model, const SourceSpanId id)
{
    diagnostic::SourceSpan span;
    if (const auto* const found = find_source_span(model, id); found != nullptr) {
        span.path = found->logical_name;
        span.begin = { found->begin.line, found->begin.column, found->begin.offset };
        span.end = { found->end.line, found->end.column, found->end.offset };
    }
    return span;
}

bool overloadable(const vhdl::Declaration& declaration)
{
    switch (declaration.form) {
    case vhdl::DeclarationForm::function:
    case vhdl::DeclarationForm::procedure:
    case vhdl::DeclarationForm::generic_function:
    case vhdl::DeclarationForm::generic_procedure:
    case vhdl::DeclarationForm::generic_function_template:
    case vhdl::DeclarationForm::generic_procedure_template:
    case vhdl::DeclarationForm::generic_function_instance:
    case vhdl::DeclarationForm::generic_procedure_instance:
    case vhdl::DeclarationForm::enumeration_literal:
        return true;
    case vhdl::DeclarationForm::alias:
        return static_cast<bool>(declaration.alias_signature);
    default:
        return false;
    }
}

class Analysis {
public:
    Analysis(CompiledDesign& design, diagnostic::Engine& diagnostics,
        const std::span<const VhdlAvailablePrimary> available,
        const std::span<const support::SymbolId> bound_labels)
        : bound_labels_(bound_labels.begin(), bound_labels.end())
        , design_(design)
        , diagnostics_(diagnostics)
        , regions_(design, available)
        , lookup_(regions_)
        , all_(support::vhdl_symbol("all"))
    {
    }

    [[nodiscard]] bool run(std::span<const UnitId> units)
    {
        const std::set<std::uint32_t> wanted = [&] {
            std::set<std::uint32_t> ids;
            for (const auto id : units) {
                ids.insert(id.value());
            }
            return ids;
        }();
        std::vector<UnitId> analyzed;
        for (const auto& unit : design_.vhdl_hir.units()) {
            if (unit.id.valid() && wanted.contains(unit.id.value())
                && unit.package_instance_expansion) {
                // A package instance's expansion is re-analyzed with its
                // actuals in gate BD2 (V4; owner 11-20: exempt R1): its names
                // carry that recorded reason.
                visit_names(design_.vhdl_hir, unit, [](const NameSite& site) {
                    if (site.name != nullptr) {
                        site.name->exemption = vhdl::AnalysisExemption::package_instance;
                    }
                });
                continue;
            }
            if (!unit.id.valid() || !wanted.contains(unit.id.value())
                || regions_.primary_missing(unit.id)) {
                continue;
            }
            visit_names(design_.vhdl_hir, unit,
                [&](const NameSite& site) { resolve(site); });
            analyzed.push_back(unit.id);
        }
        if (failed_) {
            return false;
        }
        // A package the compiler projects (VITAL, fixed, float, Synopsys:
        // VhdlStandardPackageForm::projection) is analyzed from its real
        // source in gate BD2 (owner 11-20: exempt R2): its names the pass
        // did not resolve carry that recorded reason.
        for (const auto& unit : design_.vhdl_hir.units()) {
            if (unit.standard_package_form == support::VhdlStandardPackageForm::projection) {
                visit_names(design_.vhdl_hir, unit, [](const NameSite& site) {
                    if (site.name != nullptr && !site.name->analyzed) {
                        site.name->exemption = vhdl::AnalysisExemption::projected_package;
                    }
                });
            }
        }
        complete_conversion_signals();
        complete_view_interfaces(analyzed);
        // Expression typing (9.3, 12.5) on the resolved names.
        return type_vhdl_expressions(design_, regions_, lookup_, analyzed, diagnostics_);
    }

private:
    void error(const char* code, std::string message, const SourceSpanId source,
        const std::vector<SourceSpanId>& notes = {})
    {
        if (quiet_) {
            return;
        }
        failed_ = true;
        diagnostic::Diagnostic report;
        report.severity = diagnostic::Severity::error;
        report.code = code;
        report.message = std::move(message);
        report.span = to_span(design_.semantics, source);
        for (const auto note : notes) {
            report.notes.push_back(
                { "conflicting use clause", to_span(design_.semantics, note) });
        }
        diagnostics_.report(std::move(report));
    }

    void store(vhdl::Name& name, const Resolution& resolution) const
    {
        if (resolution.status != Visibility::found) {
            return;
        }
        // The stored fields hold the whole result only for declarations,
        // STANDARD entries and implicit operations; units, undeclared or
        // served entities are read by the elaboration from the name.
        name.analyzed = resolution.units.empty() && !resolution.standard_package
            && !resolution.undeclared && !resolution.available_unit
            && !resolution.served_package.valid();
        if (resolution.undeclared_projected) {
            name.exemption = vhdl::AnalysisExemption::projected_package;
        } else if (!name.analyzed) {
            name.exemption = vhdl::AnalysisExemption::no_declaration;
        }
        // One STANDARD type, subtype or attribute (16.3): no declaration
        // exists for it; the typing (D4) reads its row.
        if (resolution.candidates.empty() && resolution.standard.size() == 1U
            && (resolution.standard.front().kind == StandardEntry::Kind::type
                || resolution.standard.front().kind
                    == StandardEntry::Kind::attribute)) {
            name.standard = resolution.standard.front().declaration;
            return;
        }
        if (resolution.candidates.empty()) {
            return;
        }
        const auto* const first
            = design_.vhdl_hir.find_declaration(resolution.candidates.front());
        const bool over = first != nullptr && overloadable(*first);
        name.overloads = resolution.candidates;
        if (!over || resolution.candidates.size() == 1) {
            name.selected = resolution.candidates.front();
        } else {
            name.selected.reset();
        }
        if (name.selected && first != nullptr
            && first->form == vhdl::DeclarationForm::generic_type
            && name.exemption == vhdl::AnalysisExemption::none) {
            name.exemption = vhdl::AnalysisExemption::generic_type;
        }
    }

    // The native standard package (D2's designator index) a use clause in
    // force at `site` names (a package whose declarations carry native
    // intrinsics) that declares `designator` in some edition but not in
    // that of the unit `site` is in; null when none does.
    [[nodiscard]] const VhdlNativeIntrinsic* package_in_other_edition(
        const NameSite& site, const std::string& designator) const
    {
        const auto* const unit = regions_.unit_of(site.scope);
        if (unit == nullptr) {
            return nullptr;
        }
        const auto standard = project::parse_vhdl_standard(unit->standard);
        if (!standard) {
            return nullptr;
        }
        auto folded = designator;
        std::ranges::transform(folded, folded.begin(), [](const char c) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        });
        const auto own = 1U << static_cast<unsigned>(*standard);
        // The native package of the used unit: that of any declaration of
        // its region with an intrinsic.
        const auto native_package = [&](const UnitId used)
            -> const VhdlNativeIntrinsic* {
            const auto scope = regions_.unit_scope(used);
            const auto* const region
                = scope ? regions_.find(*scope) : nullptr;
            if (region == nullptr) {
                return nullptr;
            }
            for (const auto& [symbol, ids] : region->declared) {
                for (const auto id : ids) {
                    const auto view = regions_.design().find_declaration(id);
                    if (view && view->vhdl != nullptr
                        && view->vhdl->native_intrinsic != 0U) {
                        return vhdl_native_intrinsic(
                            view->vhdl->native_intrinsic);
                    }
                }
            }
            return nullptr;
        };
        std::optional<ScopeId> scope = site.scope;
        for (std::size_t depth { }; scope && depth < 256U; ++depth) {
            const auto* const region = regions_.find(*scope);
            if (region == nullptr) {
                break;
            }
            for (const auto& clause : region->uses) {
                if ((clause.kind != UseClause::Kind::member
                        && clause.kind != UseClause::Kind::all_of_package)
                    || !clause.unit.valid()) {
                    continue;
                }
                const auto* const native = native_package(clause.unit);
                if (native == nullptr) {
                    continue;
                }
                const auto editions = vhdl_native_designator_editions(
                    native->package_id, folded);
                if (editions != 0U && (editions & own) == 0U) {
                    return native;
                }
            }
            scope = region->enclosing;
        }
        return nullptr;
    }

    // Reports for a non-found resolution; true when it was found.
    bool check(const Resolution& resolution, const vhdl::Name& name,
        const std::string& missing)
    {
        switch (resolution.status) {
        case Visibility::found:
            return true;
        case Visibility::not_found:
            error("FSIM-VHDL-NAME-001",
                "no visible declaration of '" + missing + "'", name.source);
            return false;
        case Visibility::ambiguous:
            error("FSIM-VHDL-NAME-002",
                "'" + missing
                    + "' is ambiguous: potentially visible through more than one use clause",
                name.source, resolution.conflicting_uses);
            return false;
        }
        return false;
    }

    // The spelling of part `index` of a selected name, for messages only.
    static std::string part_text(const vhdl::Name& name, const std::size_t index)
    {
        std::size_t begin = 0;
        for (std::size_t i = 0; i < index; ++i) {
            const auto dot = name.spelling.find('.', begin);
            if (dot == std::string::npos) {
                return name.spelling;
            }
            begin = dot + 1;
        }
        const auto dot = name.spelling.find('.', begin);
        return name.spelling.substr(
            begin, dot == std::string::npos ? std::string::npos : dot - begin);
    }

    void resolve(const NameSite& site)
    {
        if (site.name == nullptr) {
            return;
        }
        vhdl::Name& name = *site.name;
        if (site.instance != nullptr && !site.instance->component
            && !site.instance->configuration
            && std::ranges::find(bound_labels_,
                   support::vhdl_symbol(site.instance->name))
                != bound_labels_.end()) {
            // A project binding names this instance's target (resolved and
            // checked at elaboration, validate_bindings).
            return;
        }
        auto parts = selected_parts(name);
        // A direct instantiation's `L.E(A)` (11.7.1): the architecture is
        // the entity's, named by context; E (or L.E) is resolved here.
        if (support::VhdlTargetSpelling target;
            site.role == NameRole::unit_reference
            && support::split_vhdl_target(name.text(), target)
            && target.has_architecture) {
            auto unit = name;
            const auto text = name.text();
            unit.canonical = std::string { text.substr(
                0U, static_cast<std::size_t>(
                        target.unit.data() + target.unit.size() - text.data())) };
            unit.spelling = unit.canonical;
            parts = selected_parts(unit);
        }
        if (parts.empty()) {
            return;
        }
        const TextPoint at = regions_.begin(name.source);
        if (site.role == NameRole::use_clause) {
            resolve_use(site, name, parts, at);
        } else if (site.role == NameRole::specification_component) {
            NameSite at_specification = site;
            if (const auto* unit = design_.vhdl_scope_unit(site.scope); unit != nullptr) {
                at_specification.scope = regions_.innermost_scope(*unit, name.source);
            }
            resolve_ordinary(at_specification, name, parts, at);
        } else if (site.role == NameRole::conversion_function) {
            // Only a conversion found by visibility is stored: function
            // names, or one type mark (a type conversion, 6.5.7.1, 9.3.6).
            const bool was_quiet = quiet_;
            quiet_ = true;
            auto copy = name;
            resolve_ordinary(site, copy, parts, at);
            quiet_ = was_quiet;
            const bool functions = copy.analyzed
                && (copy.selected || !copy.overloads.empty())
                && std::ranges::all_of(copy.overloads, [&](const DeclarationId id) {
                       const auto view = design_.find_declaration(id);
                       return view && view->vhdl != nullptr
                           && view->vhdl->form == vhdl::DeclarationForm::function;
                   })
                && (!copy.selected || [&] {
                       const auto view = design_.find_declaration(*copy.selected);
                       using Form = vhdl::DeclarationForm;
                       return view && view->vhdl != nullptr
                           && (view->vhdl->form == Form::function
                               || (copy.overloads.empty()
                                   && (view->vhdl->form == Form::type
                                       || view->vhdl->form == Form::subtype)));
                   }());
            if (functions) {
                name = std::move(copy);
            }
        } else if (site.role == NameRole::binding_type_actual) {
            const bool was_quiet = quiet_;
            quiet_ = true;
            auto copy = name;
            resolve_ordinary(site, copy, parts, at);
            quiet_ = was_quiet;
            using Form = vhdl::DeclarationForm;
            const bool type_mark = copy.analyzed && copy.overloads.empty()
                && (copy.selected
                        ? [&] {
                              const auto view = design_.find_declaration(*copy.selected);
                              return view && view->vhdl != nullptr
                                  && (view->vhdl->form == Form::type
                                      || view->vhdl->form == Form::subtype);
                          }()
                        : copy.standard != support::StandardDeclaration::none
                            && provided_type_of(copy.standard).has_value());
            if (type_mark) {
                name = std::move(copy);
            }
        } else {
            target_ = std::nullopt;
            resolve_ordinary(site, name, parts, at);
            if (site.instance != nullptr && site.role == NameRole::unit_reference
                && target_ && !site.instance->configuration) {
                resolve_formals(*site.instance, *target_);
            }
        }
    }

    // The formals of an instance's named associations (6.5.7): each formal
    // designator is an interface declaration of the instantiated entity or
    // component, found by its symbol among that interface list (an operator
    // symbol of a generic function too). Individual associations of one
    // formal (`p(1) => a, p(2) => b`, `p.x => c`) are one association of
    // `p` whose element choices the parser grouped (6.5.7.1); an instance's
    // formal conversions `f(s) => d` are parsed as `s` associated with an
    // implicit signal.
    void resolve_formals(vhdl::Instance& instance, const Resolution& target)
    {
        using Form = vhdl::DeclarationForm;
        const vhdl::ComponentProfile* component = nullptr;
        const Region* entity = nullptr;
        if (target.units.size() == 1U) {
            if (const auto scope = regions_.unit_scope(target.units.front())) {
                entity = regions_.find(*scope);
            }
        } else if (target.candidates.size() == 1U) {
            if (const auto view = design_.find_declaration(target.candidates.front());
                view && view->vhdl != nullptr && view->vhdl->form == Form::component
                && view->vhdl->component) {
                component = &*view->vhdl->component;
            }
        }
        if (component == nullptr && entity == nullptr) {
            return;
        }
        const auto interface = [](const vhdl::Declaration& declaration, const bool ports) {
            return ports ? declaration.form == Form::port
                         : declaration.form == Form::generic_constant
                    || declaration.form == Form::generic_type
                    || declaration.form == Form::generic_function
                    || declaration.form == Form::generic_procedure
                    || declaration.form == Form::generic_package;
        };
        const auto bind = [&](std::vector<vhdl::Association>& map, const bool ports) {
            for (auto& association : map) {
                if (!association.formal || association.formal->analyzed
                    || association.formal_conversion) {
                    continue;
                }
                const auto parts = selected_parts(*association.formal);
                if (parts.empty()) {
                    continue;
                }
                std::optional<DeclarationId> found;
                const auto consider = [&](const DeclarationId id) {
                    const auto view = design_.find_declaration(id);
                    if (!found && view && view->vhdl != nullptr
                        && interface(*view->vhdl, ports)
                        && support::vhdl_symbol(view->vhdl->name) == parts.front()) {
                        found = id;
                    }
                };
                if (component != nullptr) {
                    for (const auto id : ports ? component->ports : component->generics) {
                        consider(id);
                    }
                } else if (const auto declared = entity->declared.find(parts.front().index());
                    declared != entity->declared.end()) {
                    for (const auto id : declared->second) {
                        consider(id);
                    }
                }
                if (!found) {
                    continue;
                }
                // The rest of a selected formal (`outer.a`) is an element
                // selected by type (8.3).
                association.formal->selected = *found;
                association.formal->overloads.clear();
                association.formal->analyzed = true;
                if (ports && association.expression) {
                    conversion_formals_.emplace_back(*association.expression, *found);
                }
            }
        };
        bind(instance.generic_map, false);
        bind(instance.port_map, true);
    }

    // The implicit signal of a formal part f(s) (6.5.7.1) has its formal's
    // subtype: its written type mark is the formal declaration's, as
    // analyzed in the formal's unit (run after every unit's names).
    // A record mode-view interface without a subtype indication of its own
    // takes the view's record subtype (6.5.2), whose type mark is resolved
    // where the view is declared: the resolved mark is the interface's too.
    void complete_view_interfaces(const std::vector<UnitId>& units)
    {
        auto& declarations = design_.vhdl_hir.mutable_declarations();
        for (const auto unit_id : units) {
            const auto unit = design_.find_unit(unit_id);
            if (!unit || unit->vhdl == nullptr) {
                continue;
            }
            for (const auto id : unit->vhdl->declarations) {
                const auto interface = design_.find_declaration(id);
                if (!interface || interface->vhdl == nullptr
                    || !interface->vhdl->interface_view || !interface->vhdl->subtype
                    || interface->vhdl->subtype->written_type_mark) {
                    continue;
                }
                const auto& profile = *interface->vhdl->interface_view;
                if (profile.explicit_subtype
                    || profile.form != vhdl::ModeViewElementForm::record_view
                    || !profile.view.selected) {
                    continue;
                }
                const auto view = design_.find_declaration(*profile.view.selected);
                if (!view || view->vhdl == nullptr || !view->vhdl->mode_view
                    || !view->vhdl->mode_view->record_subtype.written_type_mark
                    || !view->vhdl->mode_view->record_subtype.written_type_mark->analyzed) {
                    continue;
                }
                const auto position = static_cast<std::size_t>(
                    interface->vhdl - design_.vhdl_hir.declarations().data());
                declarations.at(position).subtype->written_type_mark
                    = view->vhdl->mode_view->record_subtype.written_type_mark;
            }
        }
    }

    void complete_conversion_signals()
    {
        auto& declarations = design_.vhdl_hir.mutable_declarations();
        for (const auto& [actual, formal] : conversion_formals_) {
            const auto expression = design_.find_expression(actual);
            if (!expression || expression->vhdl == nullptr
                || expression->vhdl->kind != vhdl::ExpressionKind::name
                || !expression->vhdl->referenced_name
                || !expression->vhdl->referenced_name->selected) {
                continue;
            }
            const auto signal = design_.find_declaration(
                *expression->vhdl->referenced_name->selected);
            const auto port = design_.find_declaration(formal);
            if (!signal || signal->vhdl == nullptr || !signal->vhdl->formal_conversion_signal
                || !signal->vhdl->subtype || !port || port->vhdl == nullptr
                || !port->vhdl->subtype || !port->vhdl->subtype->written_type_mark) {
                continue;
            }
            const auto position = static_cast<std::size_t>(
                signal->vhdl - design_.vhdl_hir.declarations().data());
            declarations.at(position).subtype->written_type_mark
                = port->vhdl->subtype->written_type_mark;
        }
        conversion_formals_.clear();
    }

    void resolve_ordinary(const NameSite& site, vhdl::Name& name,
        const std::vector<support::SymbolId>& parts, const TextPoint& at)
    {
        Resolution resolution;
        Denotation denotation
            = lookup_.denote(parts[0], site.scope, at, resolution);
        if (denotation.kind == Denotation::Kind::none
            && resolution.status != Visibility::found) {
            if (resolution.status == Visibility::not_found && parts.size() == 1U) {
                // Kept code (owner 11-19): a name a used standard package
                // declares only in other editions (16.4; e.g. SREAD and
                // TO_HSTRING are VHDL-2008).
                if (const auto* const package
                    = package_in_other_edition(site, part_text(name, 0));
                    package != nullptr) {
                    error("FSIM-ELAB-VHSTD-001",
                        "'" + part_text(name, 0)
                            + "' names no visible declaration: "
                            + std::string { package->library } + '.'
                            + std::string { package->package }
                            + " declares it only in another VHDL edition",
                        name.source);
                    return;
                }
            }
            (void)check(resolution, name, part_text(name, 0));
            return;
        }
        // A selection through a package instance or generic package (4.9,
        // 6.5.5) reaches declarations BD2 (V4) analyzes.
        bool through_instance = false;
        std::size_t denoted_parts = parts.size();
        for (std::size_t i = 1; i < parts.size(); ++i) {
            if (denotation.kind == Denotation::Kind::object
                || denotation.kind == Denotation::Kind::none) {
                denoted_parts = i;
                break; // 8.3, 8.4: the rest is selected by type
            }
            const auto instance_form = [&](const DeclarationId id) {
                const auto view = design_.find_declaration(id);
                return view && view->vhdl != nullptr
                    && (view->vhdl->form == vhdl::DeclarationForm::package_instance
                        || view->vhdl->form == vhdl::DeclarationForm::generic_package);
            };
            if (denotation.kind == Denotation::Kind::region && denotation.declaration
                && instance_form(*denotation.declaration)) {
                through_instance = true;
            }
            // An interface package denotes its template's region (6.5.5); the
            // prefix itself is the generic package.
            if (resolution.candidates.size() == 1U
                && instance_form(resolution.candidates.front())) {
                through_instance = true;
            }
            resolution = lookup_.member(denotation, parts[i], site.scope);
            if (resolution.status == Visibility::not_found) {
                error("FSIM-VHDL-NAME-003",
                    "'" + prefix_text(name, i) + "' has no member '"
                        + part_text(name, i) + "'",
                    name.source);
                return;
            }
            if (!check(resolution, name, part_text(name, i))) {
                return;
            }
            denotation = lookup_.denote(resolution);
        }
        store(name, resolution);
        target_ = resolution;
        if (denoted_parts < parts.size()) {
            name.selected_parts = static_cast<std::uint8_t>(
                std::min<std::size_t>(denoted_parts, 255U));
        }
        if (through_instance) {
            name.exemption = vhdl::AnalysisExemption::package_instance;
        }
        // The simple name of an enclosing named construct (an entity,
        // architecture, process, block, generate or loop label, 8.3): no
        // HIR declaration; the elaboration reads it from the name.
        if (parts.size() == 1U && denotation.kind == Denotation::Kind::region
            && resolution.status != Visibility::found) {
            name.exemption = vhdl::AnalysisExemption::no_declaration;
        }
    }

    static std::string prefix_text(const vhdl::Name& name, const std::size_t count)
    {
        std::string text;
        for (std::size_t i = 0; i < count; ++i) {
            if (i != 0) {
                text += '.';
            }
            text += part_text(name, i);
        }
        return text;
    }

    void resolve_use(const NameSite& site, vhdl::Name& name,
        const std::vector<support::SymbolId>& parts, const TextPoint& at)
    {
        Denotation denotation;
        Resolution resolution;
        if (regions_.library_visible(parts[0], site.scope, at)) {
            denotation.kind = Denotation::Kind::library;
            denotation.library = parts[0];
        } else {
            // A prefix that is not a library logical name: a package made
            // visible by an earlier use clause, a package instance or an
            // enclosing package (12.4, 8.3).
            denotation = lookup_.denote(parts[0], site.scope, at, resolution);
            if (denotation.kind == Denotation::Kind::none
                || denotation.kind == Denotation::Kind::object) {
                if (resolution.status == Visibility::found) {
                    resolution.status = Visibility::not_found;
                }
                (void)check(resolution, name, part_text(name, 0));
                return;
            }
        }
        for (std::size_t i = 1; i < parts.size(); ++i) {
            if (i + 1 == parts.size() && parts[i] == all_) {
                return; // `all`: the clause's target, nothing more to resolve
            }
            resolution = lookup_.member(denotation, parts[i], site.scope);
            if (resolution.status == Visibility::not_found) {
                error("FSIM-VHDL-NAME-003",
                    "'" + prefix_text(name, i) + "' has no member '"
                        + part_text(name, i) + "'",
                    name.source);
                return;
            }
            if (!check(resolution, name, part_text(name, i))) {
                return;
            }
            denotation = lookup_.denote(resolution);
        }
        // A clause naming a unit (`use L.P;`) denotes no declaration a Name
        // can hold; only a member of a package or region is stored.
        if (denotation.kind != Denotation::Kind::unit
            && denotation.kind != Denotation::Kind::library) {
            store(name, resolution);
        }
    }

    std::vector<support::SymbolId> bound_labels_;
    // The final resolution of the last ordinary name (an instance target).
    std::optional<Resolution> target_;
    // Set while a name whose absence is no error is resolved.
    bool quiet_ { };
    // (port map actual, formal port) of resolved instance formals.
    std::vector<std::pair<ExpressionId, DeclarationId>> conversion_formals_;
    CompiledDesign& design_;
    diagnostic::Engine& diagnostics_;
    Regions regions_;
    Lookup lookup_;
    support::SymbolId all_;
    bool failed_ { };
};

} // namespace

bool analyze_vhdl_names(CompiledDesign& design, const std::span<const UnitId> units,
    diagnostic::Engine& diagnostics,
    const std::span<const VhdlAvailablePrimary> available,
    const std::span<const support::SymbolId> bound_labels)
{
    Analysis analysis(design, diagnostics, available, bound_labels);
    return analysis.run(units);
}

} // namespace fsim::semantic
