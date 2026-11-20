// SPDX-License-Identifier: Apache-2.0
#include "compiled_design_vhdl_analysis_internal.hpp"
#include "fsim/project/project.hpp"
#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/vhdl_standard_declaration.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

// Visibility over the declarative regions (IEEE 1076-2019 12.3, 12.4, 8.3).
// Names are compared as interned symbols; a declaration's profile is compared
// by type identity (the root type of each parameter and of the result), never
// by spelling.
namespace fsim::semantic::vhdl_names {

namespace {

constexpr std::size_t max_region_depth = 4096U;

/// Whether the point `before` lies before `at` for visibility: unordered
/// points (other files, generated text) are taken as already in force.
bool in_force(const TextPoint& before, const TextPoint& at) noexcept
{
    return !before.ordered || !at.ordered || !(before.file == at.file)
        || precedes(before, at);
}

/// 12.3: subprograms (functions, procedures, generic subprogram instances,
/// subprogram aliases) and enumeration literals are overloadable.
bool overloadable(const vhdl::Declaration& declaration) noexcept
{
    return declaration.callable.has_value()
        || declaration.alias_signature.has_value()
        || declaration.form == vhdl::DeclarationForm::enumeration_literal;
}

/// The parameter and result type profile of an overloadable declaration
/// (12.3, 4.5.1), by root type identity. An enumeration literal behaves as a
/// parameterless function returning its type (3.5.1).
struct Profile {
    bool function { };
    std::vector<std::uint32_t> parameters;
    std::uint32_t result { };

    bool operator==(const Profile&) const = default;
};

std::optional<std::uint32_t> root_type_of(
    const CompiledDesign& design, const vhdl::SubtypeIndication& subtype)
{
    const auto target = subtype.type_mark.target;
    if (!target.valid()) {
        return std::nullopt;
    }
    const auto root = vhdl_root_type(design, target);
    if (!root.valid()) {
        return std::nullopt;
    }
    return root.value();
}

/// nullopt when a type of the profile cannot be determined: the declaration
/// is then not a homograph of any other overloadable (no spelling fallback).
std::optional<Profile> profile_of(
    const CompiledDesign& design, const vhdl::Declaration& declaration)
{
    Profile profile;
    if (declaration.form == vhdl::DeclarationForm::enumeration_literal) {
        if (!declaration.subtype) {
            return std::nullopt;
        }
        const auto type = root_type_of(design, *declaration.subtype);
        if (!type) {
            return std::nullopt;
        }
        profile.function = true;
        profile.result = *type;
        return profile;
    }
    if (declaration.alias_signature.has_value()) {
        const auto& signature = *declaration.alias_signature;
        for (const auto& parameter : signature.parameters) {
            const auto type = root_type_of(design, parameter);
            if (!type) {
                return std::nullopt;
            }
            profile.parameters.push_back(*type);
        }
        if (signature.result) {
            const auto type = root_type_of(design, *signature.result);
            if (!type) {
                return std::nullopt;
            }
            profile.function = true;
            profile.result = *type;
        }
        return profile;
    }
    if (!declaration.callable) {
        return std::nullopt;
    }
    const auto& callable = *declaration.callable;
    profile.function = callable.function;
    for (const auto formal_id : callable.formals) {
        const auto formal = design.find_declaration(formal_id);
        if (!formal || formal->vhdl == nullptr || !formal->vhdl->subtype) {
            return std::nullopt;
        }
        const auto type = root_type_of(design, *formal->vhdl->subtype);
        if (!type) {
            return std::nullopt;
        }
        profile.parameters.push_back(*type);
    }
    if (callable.function) {
        if (!callable.return_type) {
            return std::nullopt;
        }
        const auto type = root_type_of(design, *callable.return_type);
        if (!type) {
            return std::nullopt;
        }
        profile.result = *type;
    }
    return profile;
}

/// 12.3: two declarations with the same designator are homographs when at
/// least one is not overloadable, or both are and have the same profile.
/// `a` and `b` carry the same designator (the caller's).
bool homographs(const CompiledDesign& design, const vhdl::Declaration& a,
    const vhdl::Declaration& b)
{
    if (!overloadable(a) || !overloadable(b)) {
        return true;
    }
    const auto left = profile_of(design, a);
    const auto right = profile_of(design, b);
    return left && right && *left == *right;
}

/// The same entity: a declaration and its completion (a subprogram
/// specification and its body, a deferred constant and its full
/// declaration, an incomplete type and its full declaration).
bool same_entity(const vhdl::Declaration& a, const vhdl::Declaration& b) noexcept
{
    return a.id == b.id || (a.completion && *a.completion == b.id)
        || (b.completion && *b.completion == a.id);
}

struct Gathered {
    bool non_overloadable { };
    std::vector<const vhdl::Declaration*> declarations;
};

/// The declarations of `designator` declared immediately in `region` that
/// `visible` accepts. A non-overloadable one is alone (the first: a later
/// one is its completion); otherwise every overloadable one, a completion
/// of an earlier one omitted.
template <typename Accept>
Gathered gather(const CompiledDesign& design, const Region& region,
    const support::SymbolId designator, const Accept& accept)
{
    Gathered result;
    const auto found = region.declared.find(designator.index());
    if (found == region.declared.end()) {
        return result;
    }
    for (const auto id : found->second) {
        const auto view = design.find_declaration(id);
        if (!view || view->vhdl == nullptr || !accept(*view->vhdl)) {
            continue;
        }
        const auto& declaration = *view->vhdl;
        if (!overloadable(declaration)) {
            result.non_overloadable = true;
            result.declarations.assign(1U, &declaration);
            return result;
        }
        const bool completion = std::ranges::any_of(result.declarations,
            [&](const vhdl::Declaration* earlier) {
                return same_entity(*earlier, declaration);
            });
        if (!completion) {
            result.declarations.push_back(&declaration);
        }
    }
    return result;
}

/// Whether region `outer` is `inner` or encloses it (12.1).
bool encloses(const Regions& regions, const ScopeId outer, ScopeId inner)
{
    for (std::size_t steps = 0; steps < max_region_depth; ++steps) {
        if (inner == outer) {
            return true;
        }
        const auto* const region = regions.find(inner);
        if (region == nullptr || !region->enclosing) {
            return false;
        }
        inner = *region->enclosing;
    }
    return false;
}

/// A potentially visible entity (12.4): a declaration or a primary unit,
/// with the clause that makes it so.
struct Potential {
    const vhdl::Declaration* declaration { };
    UnitId unit;
    SourceSpanId clause;
    /// A declaration of the compiler-served STD.STANDARD (declaration null,
    /// unit invalid).
    std::optional<StandardEntry> standard;
    /// `declaration` is a type declaration; the entity is an operation it
    /// implicitly declares (overloadable).
    bool implicit { };
    /// An entity of Region::undeclared of the region `region` (its value:
    /// whether overloadable).
    std::optional<bool> undeclared;
    std::optional<ScopeId> region;
    /// The compiler-served package STD.STANDARD itself (`use STD.STANDARD;`).
    bool standard_package { };
    /// Another compiler-served package of STD itself (`use STD.REFLECTION;`).
    support::SymbolId served_package;

    [[nodiscard]] bool overloadable_entity() const noexcept
    {
        if (implicit) {
            return true;
        }
        if (undeclared) {
            return *undeclared;
        }
        if (standard) {
            return standard->overloadable();
        }
        return declaration != nullptr && overloadable(*declaration);
    }

    [[nodiscard]] bool same(const Potential& other) const noexcept
    {
        if (implicit || other.implicit) {
            return implicit && other.implicit && declaration == other.declaration;
        }
        if (undeclared || other.undeclared) {
            return undeclared.has_value() && other.undeclared.has_value()
                && region == other.region;
        }
        if (standard_package || other.standard_package) {
            return standard_package && other.standard_package;
        }
        if (served_package.valid() || other.served_package.valid()) {
            return served_package == other.served_package;
        }
        if (standard || other.standard) {
            return standard && other.standard
                && standard->declaration == other.standard->declaration
                && standard->kind == other.standard->kind;
        }
        return declaration != nullptr
            ? other.declaration == declaration
            : other.declaration == nullptr && other.unit == unit;
    }
};

/// Where `declaration` takes effect within its own region (12.3): the end
/// of the declaration, or of the incomplete type declaration it completes
/// (5.4.2).
TextPoint declared_from(const Regions& regions, const vhdl::Declaration& declaration)
{
    return regions.end(declaration.incomplete_source.value_or(declaration.source));
}

/// The type declarations of `region` implicitly declaring `designator`
/// that `accept` takes.
template <typename Accept>
std::vector<const vhdl::Declaration*> implicit_in(const CompiledDesign& design,
    const Region& region, const support::SymbolId designator,
    const Accept& accept)
{
    std::vector<const vhdl::Declaration*> result;
    const auto found = region.implicit.find(designator.index());
    if (found == region.implicit.end()) {
        return result;
    }
    for (const auto id : found->second) {
        const auto view = design.find_declaration(id);
        if (view && view->vhdl != nullptr && accept(*view->vhdl)) {
            result.push_back(view->vhdl);
        }
    }
    return result;
}

/// What the prefix of a `selected` use clause (its path without the last
/// part) denotes at the clause (12.4, 8.3); defined after Lookup.
Denotation selected_prefix(const Regions& regions, const UseClause& clause);

/// The nesting of `selected` clause expansions on this thread: a clause's
/// prefix is looked up by visibility, which reads the clause again.
thread_local unsigned selected_depth = 0U;
constexpr unsigned max_selected_depth = 4U;

/// The entities of `designator` declared in `region` (a package's or a
/// package instance's), as potentially visible through `source` (12.4).
void expand_region(const CompiledDesign& design, const Region& region,
    const support::SymbolId designator, const SourceSpanId source,
    std::vector<Potential>& out)
{
    const auto gathered = gather(design, region, designator,
        [](const vhdl::Declaration&) { return true; });
    for (const auto* declaration : gathered.declarations) {
        Potential entry;
        entry.declaration = declaration;
        entry.clause = source;
        out.push_back(entry);
    }
    if (gathered.non_overloadable) {
        return;
    }
    for (const auto* type : implicit_in(design, region, designator,
             [](const vhdl::Declaration&) { return true; })) {
        Potential entry;
        entry.declaration = type;
        entry.clause = source;
        entry.implicit = true;
        out.push_back(entry);
    }
    if (const auto found = region.undeclared.find(designator.index());
        found != region.undeclared.end()) {
        Potential entry;
        entry.clause = source;
        entry.undeclared = found->second;
        entry.region = region.scope;
        out.push_back(entry);
    }
}

/// 12.4 (1076-2008 and later): `use P.T` where T denotes a type or subtype
/// also makes potentially visible the enumeration literals and units of
/// the base type and the implicit declarations of predefined operations for
/// the type (those of `designator` here).
void expand_type_member(const Regions& regions, const UseClause& clause,
    const support::SymbolId designator, const vhdl::Unit* from,
    std::vector<Potential>& out)
{
    if (from == nullptr) {
        return;
    }
    const auto standard = project::parse_vhdl_standard(from->standard);
    if (!standard || standard_year(*standard) < 2008U) {
        return;
    }
    const auto& design = regions.design();
    const auto scope = regions.unit_scope(clause.unit);
    const auto* const region
        = scope ? regions.find(*scope) : static_cast<const Region*>(nullptr);
    if (region == nullptr) {
        return;
    }
    const auto accept = [](const vhdl::Declaration&) { return true; };
    std::vector<TypeId> types;
    for (const auto* named : gather(design, *region, clause.member, accept).declarations) {
        if ((named->form != vhdl::DeclarationForm::type
                && named->form != vhdl::DeclarationForm::subtype)
            || !named->declared_type) {
            continue;
        }
        types.push_back(*named->declared_type);
        // A subtype's base type (5.1): its literals and units.
        if (const auto* definition = design.vhdl_hir.find_type(*named->declared_type);
            definition != nullptr && definition->form == vhdl::TypeForm::subtype
            && definition->base.type_mark.target.valid()) {
            types.push_back(definition->base.type_mark.target);
        }
    }
    if (types.empty()) {
        return;
    }
    const auto named_type = [&](const TypeId id) {
        return std::ranges::find(types, id) != types.end();
    };
    for (const auto* type : implicit_in(design, *region, designator, accept)) {
        if (type->declared_type && named_type(*type->declared_type)) {
            Potential entry;
            entry.declaration = type;
            entry.clause = clause.source;
            entry.implicit = true;
            out.push_back(entry);
        }
    }
    for (const auto* declaration : gather(design, *region, designator, accept).declarations) {
        bool of_type = declaration->form == vhdl::DeclarationForm::enumeration_literal
            && declaration->subtype && named_type(declaration->subtype->type_mark.target);
        for (const auto id : types) {
            const auto* definition = design.vhdl_hir.find_type(id);
            if (definition == nullptr) {
                continue;
            }
            for (const auto& unit : definition->physical_units) {
                of_type = of_type || unit.declaration == declaration->id;
            }
        }
        if (of_type) {
            Potential entry;
            entry.declaration = declaration;
            entry.clause = clause.source;
            out.push_back(entry);
        }
    }
}

void expand_use(const Regions& regions, const UseClause& clause,
    const support::SymbolId designator, const vhdl::Unit* from,
    std::vector<Potential>& out)
{
    const auto& design = regions.design();
    const auto unit_entry = [&](const UnitId unit) {
        Potential entry;
        entry.unit = unit;
        entry.clause = clause.source;
        out.push_back(entry);
    };
    // The declarations of a compiler-served package of STD (16.6).
    const auto served_entries = [&](const support::SymbolId package) {
        const auto* const members = from == nullptr ? nullptr
            : regions.served_members(support::vhdl_symbol("std"), package, *from);
        if (members == nullptr) {
            return;
        }
        for (const auto& [symbol, overloadable] : *members) {
            if (symbol == designator) {
                Potential entry;
                entry.clause = clause.source;
                entry.undeclared = overloadable;
                out.push_back(entry);
            }
        }
    };
    const auto standard_entries = [&]() {
        if (from == nullptr) {
            return;
        }
        for (const auto& found : regions.standard_declarations(designator, *from)) {
            Potential entry;
            entry.clause = clause.source;
            entry.standard = found;
            out.push_back(entry);
        }
    };
    switch (clause.kind) {
    case UseClause::Kind::member:
    case UseClause::Kind::all_of_package: {
        if (clause.kind == UseClause::Kind::member
            && !(clause.member == designator)) {
            expand_type_member(regions, clause, designator, from, out);
            return;
        }
        const auto scope = regions.unit_scope(clause.unit);
        const auto* const region
            = scope ? regions.find(*scope) : static_cast<const Region*>(nullptr);
        if (region != nullptr) {
            expand_region(design, *region, designator, clause.source, out);
        }
        return;
    }
    case UseClause::Kind::all_of_library: {
        const auto units = regions.primary_units(clause.library, designator);
        for (const auto unit : units) {
            unit_entry(unit);
        }
        if (units.empty() && regions.standard_served()
            && clause.library == support::vhdl_symbol("std")
            && designator == support::vhdl_symbol("standard")) {
            // `use STD.all;` makes the compiler-served STANDARD visible.
            Potential entry;
            entry.clause = clause.source;
            entry.standard_package = true;
            out.push_back(entry);
        }
        return;
    }
    case UseClause::Kind::unit_name:
        for (const auto unit : regions.primary_units(clause.library, designator)) {
            if (unit == clause.unit) {
                unit_entry(unit);
            }
        }
        return;
    case UseClause::Kind::standard_all:
    case UseClause::Kind::standard_member:
        if (clause.kind == UseClause::Kind::standard_member
            && !(clause.member == designator)) {
            return;
        }
        standard_entries();
        return;
    case UseClause::Kind::standard_name:
        if (designator == support::vhdl_symbol("standard")) {
            Potential entry;
            entry.clause = clause.source;
            entry.standard_package = true;
            out.push_back(entry);
        }
        return;
    case UseClause::Kind::served_all:
    case UseClause::Kind::served_member:
        if (clause.kind == UseClause::Kind::served_member
            && !(clause.member == designator)) {
            return;
        }
        served_entries(clause.served);
        return;
    case UseClause::Kind::served_name:
        if (designator == clause.served) {
            Potential entry;
            entry.clause = clause.source;
            entry.served_package = clause.served;
            out.push_back(entry);
        }
        return;
    case UseClause::Kind::selected: {
        if (clause.path.size() < 2U || selected_depth >= max_selected_depth) {
            return;
        }
        const auto last = clause.path.back();
        const bool all = last == support::vhdl_symbol("all");
        if (!all && !(last == designator)) {
            return;
        }
        ++selected_depth;
        struct Leave {
            ~Leave() { --selected_depth; }
        } leave;
        const auto prefix = selected_prefix(regions, clause);
        switch (prefix.kind) {
        case Denotation::Kind::unit:
        case Denotation::Kind::region: {
            const auto scope = prefix.kind == Denotation::Kind::unit
                ? regions.unit_scope(prefix.unit)
                : prefix.scope;
            const auto* const region = scope ? regions.find(*scope)
                                             : static_cast<const Region*>(nullptr);
            if (region != nullptr) {
                expand_region(design, *region, designator, clause.source, out);
            }
            return;
        }
        case Denotation::Kind::standard_package:
            standard_entries();
            return;
        case Denotation::Kind::served_package:
            served_entries(prefix.served);
            return;
        case Denotation::Kind::library:
            if (all) {
                for (const auto unit :
                    regions.primary_units(prefix.library, designator)) {
                    unit_entry(unit);
                }
            }
            return;
        case Denotation::Kind::object:
        case Denotation::Kind::none:
            return;
        }
        return;
    }
    }
}

/// directly and potentially visible entities of `designator` (12.3, 12.4).
/// `resolution.candidates` and `resolution.units` are filled.
void collect(const Regions& regions, const support::SymbolId designator,
    const ScopeId scope, const TextPoint& at, Resolution& resolution)
{
    const auto& design = regions.design();
    std::vector<const vhdl::Declaration*> direct;
    std::vector<const vhdl::Declaration*> direct_implicit;
    bool direct_undeclared = false;
    std::vector<const Region*> chain;
    bool hidden_by_non_overloadable = false;

    // 12.3: directly visible declarations, innermost region first.
    ScopeId current = scope;
    for (std::size_t steps = 0; steps < max_region_depth; ++steps) {
        const auto* const region = regions.find(current);
        if (region == nullptr) {
            break;
        }
        chain.push_back(region);
        // 12.3: within its own region a declaration is visible only after
        // its end; in an enclosing region the whole region is read. A loop
        // parameter's region declares it before the loop's statements
        // (10.10), wherever its HIR span ends.
        const auto* const named = design.find_scope(current);
        const bool own = steps == 0U
            && !(named != nullptr && named->role == ScopeRole::loop_parameters);
        const auto gathered = gather(design, *region, designator,
            [&](const vhdl::Declaration& declaration) {
                return !own || in_force(declared_from(regions, declaration), at);
            });
        if (gathered.non_overloadable) {
            // A non-overloadable declaration hides every outer homograph;
            // an inner overloadable one hides it (it is their homograph).
            if (direct.empty()) {
                direct.push_back(gathered.declarations.front());
            }
            hidden_by_non_overloadable = true;
            break;
        }
        for (const auto* type : implicit_in(design, *region, designator,
                 [&](const vhdl::Declaration& declaration) {
                     return !own
                         || in_force(declared_from(regions, declaration), at);
                 })) {
            direct_implicit.push_back(type);
        }
        if (const auto found = region->undeclared.find(designator.index());
            found != region->undeclared.end()) {
            if (!found->second) {
                // A non-overloadable entity without a declaration (a
                // generate parameter, GUARD, a label): like a declaration.
                if (direct.empty() && direct_implicit.empty()
                    && !direct_undeclared) {
                    direct_undeclared = true;
                }
                hidden_by_non_overloadable = true;
                for (const auto* declaration : gathered.declarations) {
                    direct.push_back(declaration);
                }
                break;
            }
            direct_undeclared = true;
        }
        const auto inner_count = direct.size();
        for (const auto* declaration : gathered.declarations) {
            const bool hidden = std::any_of(direct.begin(),
                direct.begin() + static_cast<std::ptrdiff_t>(inner_count),
                [&](const vhdl::Declaration* inner) {
                    return homographs(design, *inner, *declaration);
                });
            if (!hidden) {
                direct.push_back(declaration);
            }
        }
        if (!region->enclosing) {
            break;
        }
        current = *region->enclosing;
    }

    // 12.4: potentially visible declarations; a directly visible homograph
    // hides them, so a non-overloadable direct one hides all.
    std::vector<Potential> potential;
    if (!hidden_by_non_overloadable) {
        std::vector<Potential> hits;
        const auto* const from = regions.unit_of(scope);
        for (const auto* region : chain) {
            for (const auto& clause : region->uses) {
                if (in_force(clause.after, at)) {
                    expand_use(regions, clause, designator, from, hits);
                }
            }
        }
        const bool direct_any = !direct.empty() || !direct_implicit.empty()
            || direct_undeclared;
        for (const auto& hit : hits) {
            // A directly visible declaration hides a potentially visible
            // homograph (12.4). An overloadable entity without a
            // vhdl::Declaration (a STANDARD or implicit operation, a member
            // of a projected package) is hidden only by a homograph of the
            // same profile, which 12.5 decides.
            const bool hidden = hit.declaration != nullptr && !hit.implicit
                ? std::any_of(direct.begin(), direct.end(),
                      [&](const vhdl::Declaration* inner) {
                          return homographs(design, *inner, *hit.declaration);
                      })
                    || ((direct_implicit.size() > 0U || direct_undeclared)
                        && !overloadable(*hit.declaration))
                : direct_any && !hit.overloadable_entity();
            const bool duplicate = std::ranges::any_of(
                potential, [&](const Potential& earlier) {
                    return earlier.same(hit);
                });
            if (!hidden && !duplicate) {
                potential.push_back(hit);
            }
        }
        // 12.4: with a non-overloadable potentially visible homograph beside
        // another potentially visible entity, neither is made visible.
        const bool any_non_overloadable = std::ranges::any_of(
            potential, [](const Potential& entry) {
                return !entry.overloadable_entity();
            });
        if (any_non_overloadable && potential.size() > 1U) {
            resolution.status = Visibility::ambiguous;
            for (const auto& hit : hits) {
                if (hit.clause.valid()
                    && std::ranges::find(resolution.conflicting_uses, hit.clause)
                        == resolution.conflicting_uses.end()) {
                    resolution.conflicting_uses.push_back(hit.clause);
                }
            }
            return;
        }
    }

    for (const auto* declaration : direct) {
        resolution.candidates.push_back(declaration->id);
    }
    for (const auto* type : direct_implicit) {
        resolution.implicit.push_back(type->id);
    }
    resolution.undeclared = direct_undeclared;
    for (const auto& entry : potential) {
        if (entry.undeclared) {
            resolution.undeclared = true;
        } else if (entry.standard_package) {
            resolution.standard_package = true;
        } else if (entry.served_package.valid()) {
            resolution.served_package = entry.served_package;
        } else if (entry.implicit) {
            resolution.implicit.push_back(entry.declaration->id);
        } else if (entry.standard) {
            resolution.standard.push_back(*entry.standard);
        } else if (entry.declaration != nullptr) {
            resolution.candidates.push_back(entry.declaration->id);
        } else {
            resolution.units.push_back(entry.unit);
        }
    }
    resolution.status = resolution.candidates.empty() && resolution.units.empty()
            && resolution.standard.empty() && resolution.implicit.empty()
            && !resolution.undeclared && !resolution.standard_package
            && !resolution.served_package.valid()
        ? Visibility::not_found
        : Visibility::found;
}

/// What the found declarations denote as a prefix (8.3). `from` is the scope
/// the name is written in, when known: a subprogram (or other construct)
/// whose region encloses it is a region prefix.
Denotation classify(const Regions& regions,
    const std::vector<DeclarationId>& candidates,
    const std::optional<ScopeId> from)
{
    Denotation result;
    const auto& design = regions.design();
    std::optional<ScopeId> package_scope;
    for (const auto id : candidates) {
        const auto view = design.find_declaration(id);
        if (!view || view->vhdl == nullptr) {
            continue;
        }
        const auto& declaration = *view->vhdl;
        const vhdl::Declaration* completion = nullptr;
        if (declaration.completion) {
            const auto later = design.find_declaration(*declaration.completion);
            completion = later ? later->vhdl : nullptr;
        }
        for (const auto* owner : { &declaration, completion }) {
            if (owner == nullptr || !owner->nested_scope) {
                continue;
            }
            if (from && encloses(regions, *owner->nested_scope, *from)) {
                result.kind = Denotation::Kind::region;
                result.scope = owner->nested_scope;
                result.declaration = declaration.id;
                return result;
            }
            if (declaration.form == vhdl::DeclarationForm::package_instance
                || declaration.form == vhdl::DeclarationForm::generic_package) {
                package_scope = owner->nested_scope;
            }
        }
    }
    if (package_scope && candidates.size() == 1U) {
        // A package instance or generic package: its declarations.
        result.kind = Denotation::Kind::region;
        result.scope = package_scope;
        result.declaration = candidates.front();
        return result;
    }
    // An object, a function call's result, or any other declaration: what
    // follows is selected by type, not visibility (8.3, 8.4).
    result.kind = Denotation::Kind::object;
    if (candidates.size() == 1U) {
        result.declaration = candidates.front();
    }
    return result;
}

Denotation of_unit(const UnitId unit)
{
    Denotation result;
    result.kind = Denotation::Kind::unit;
    result.unit = unit;
    return result;
}

} // namespace

Lookup::Lookup(const Regions& regions)
    : regions_ { regions }
{
}

Resolution Lookup::visible(const support::SymbolId designator,
    const ScopeId scope, const TextPoint& at) const
{
    Resolution resolution;
    collect(regions_, designator, scope, at, resolution);
    return resolution;
}

Resolution Lookup::member(const Denotation& prefix,
    const support::SymbolId designator, const ScopeId from) const
{
    Resolution resolution;
    switch (prefix.kind) {
    case Denotation::Kind::library: {
        // 8.3: a primary unit of the library (13.1); WORK denotes the
        // working library of the unit the name is written in (13.2).
        auto library = prefix.library;
        if (library == support::vhdl_symbol("work")) {
            if (const auto* const unit = regions_.unit_of(from)) {
                library = regions_.library_of(*unit);
            }
        }
        const auto units = regions_.primary_units(library, designator);
        if (units.empty() && regions_.standard_served()
            && library == support::vhdl_symbol("std")
            && designator == support::vhdl_symbol("standard")) {
            // The compiler-served package STD.STANDARD (16.3).
            resolution.standard_package = true;
            resolution.status = Visibility::found;
            return resolution;
        }
        if (const auto* const unit = regions_.unit_of(from); units.empty()
            && unit != nullptr
            && regions_.served_members(library, designator, *unit) != nullptr) {
            // A compiler-served package of STD (STD.REFLECTION, 16.6).
            resolution.served_package = designator;
            resolution.status = Visibility::found;
            return resolution;
        }
        if (units.empty() && regions_.available_primary(library, designator)) {
            resolution.available_unit = true;
            resolution.status = Visibility::found;
            return resolution;
        }
        resolution.units.assign(units.begin(), units.end());
        resolution.status = units.empty() ? Visibility::not_found
            : units.size() == 1U          ? Visibility::found
                                          : Visibility::ambiguous;
        return resolution;
    }
    case Denotation::Kind::unit:
    case Denotation::Kind::region: {
        // 8.3: a declaration of the unit's or region's declarative part.
        std::optional<ScopeId> scope = prefix.scope;
        if (prefix.kind == Denotation::Kind::unit) {
            scope = regions_.unit_scope(prefix.unit);
        }
        const auto* const region
            = scope ? regions_.find(*scope) : static_cast<const Region*>(nullptr);
        if (region == nullptr) {
            return resolution;
        }
        const auto gathered = gather(regions_.design(), *region, designator,
            [](const vhdl::Declaration&) { return true; });
        for (const auto* declaration : gathered.declarations) {
            resolution.candidates.push_back(declaration->id);
        }
        if (!gathered.non_overloadable) {
            for (const auto* type : implicit_in(regions_.design(), *region,
                     designator, [](const vhdl::Declaration&) { return true; })) {
                resolution.implicit.push_back(type->id);
            }
            resolution.undeclared = region->undeclared.contains(designator.index());
        }
        resolution.status = resolution.candidates.empty()
                && resolution.implicit.empty() && !resolution.undeclared
            ? Visibility::not_found
            : Visibility::found;
        return resolution;
    }
    case Denotation::Kind::standard_package: {
        const auto* const unit = regions_.unit_of(from);
        if (unit == nullptr) {
            return resolution;
        }
        for (const auto& entry : regions_.standard_declarations(designator, *unit)) {
            resolution.standard.push_back(entry);
        }
        resolution.status = resolution.standard.empty() ? Visibility::not_found
                                                        : Visibility::found;
        return resolution;
    }
    case Denotation::Kind::served_package: {
        const auto* const unit = regions_.unit_of(from);
        const auto* const members = unit == nullptr ? nullptr
            : regions_.served_members(prefix.library, prefix.served, *unit);
        if (members != nullptr
            && std::ranges::any_of(*members, [&](const auto& member) {
                   return member.first == designator;
               })) {
            resolution.undeclared = true;
            resolution.status = Visibility::found;
        }
        return resolution;
    }
    case Denotation::Kind::object:
    case Denotation::Kind::none:
        break;
    }
    return resolution;
}

Denotation Lookup::denote(const support::SymbolId designator,
    const ScopeId scope, const TextPoint& at, Resolution& resolution) const
{
    resolution = Resolution { };
    collect(regions_, designator, scope, at, resolution);
    if (resolution.status == Visibility::ambiguous) {
        return { };
    }
    // A visible declaration.
    if (!resolution.candidates.empty()) {
        if (resolution.candidates.size() == 1U) {
            const auto view
                = regions_.design().find_declaration(resolution.candidates.front());
            if (view && view->vhdl != nullptr && view->vhdl->package
                && !view->vhdl->nested_scope) {
                return template_of(*view->vhdl);
            }
        }
        return classify(regions_, resolution.candidates, scope);
    }
    // An enclosing named construct (process, block, generate, loop label,
    // entity, architecture): 8.3 allows its simple name as a prefix inside
    // it. The scope's name is interned here, on this rare path only.
    ScopeId current = scope;
    for (std::size_t steps = 0; steps < max_region_depth; ++steps) {
        const auto* const region = regions_.find(current);
        if (region == nullptr) {
            break;
        }
        const auto* const named = regions_.design().find_scope(current);
        if ((named != nullptr && named->role == ScopeRole::named
                && support::vhdl_symbol(named->name) == designator)
            || region->label == designator) {
            Denotation result;
            result.kind = Denotation::Kind::region;
            result.scope = current;
            return result;
        }
        if (!region->enclosing) {
            break;
        }
        current = *region->enclosing;
    }
    // A primary unit made visible by a use clause (12.4).
    if (!resolution.units.empty()) {
        if (resolution.units.size() > 1U) {
            resolution.status = Visibility::ambiguous;
            return { };
        }
        return of_unit(resolution.units.front());
    }
    // The compiler-served STD.STANDARD made visible by `use STD.STANDARD;`.
    if (resolution.standard_package) {
        Denotation result;
        result.kind = Denotation::Kind::standard_package;
        result.library = support::vhdl_symbol("std");
        return result;
    }
    if (resolution.served_package.valid()) {
        Denotation result;
        result.kind = Denotation::Kind::served_package;
        result.library = support::vhdl_symbol("std");
        result.served = resolution.served_package;
        return result;
    }
    // A STANDARD declaration (type, function, literal), an implicitly
    // declared operation or an entity without a declaration: selected by
    // type.
    if (!resolution.standard.empty() || !resolution.implicit.empty()
        || resolution.undeclared) {
        Denotation result;
        result.kind = Denotation::Kind::object;
        return result;
    }
    // A visible library logical name (13.2).
    if (regions_.library_visible(designator, scope, at)) {
        Denotation result;
        result.kind = Denotation::Kind::library;
        result.library = designator;
        return result;
    }
    return { };
}

Denotation Lookup::denote(const Resolution& resolution) const
{
    if (resolution.status != Visibility::found) {
        return { };
    }
    if (resolution.candidates.size() == 1U) {
        const auto view = regions_.design().find_declaration(
            resolution.candidates.front());
        if (view && view->vhdl != nullptr && view->vhdl->package
            && !view->vhdl->nested_scope) {
            return template_of(*view->vhdl);
        }
    }
    if (!resolution.candidates.empty()) {
        return classify(regions_, resolution.candidates, std::nullopt);
    }
    if (resolution.available_unit) {
        // A unit without HIR: it declares nothing visible here.
        return of_unit(UnitId { });
    }
    if (resolution.units.size() == 1U) {
        return of_unit(resolution.units.front());
    }
    if (resolution.standard_package) {
        Denotation result;
        result.kind = Denotation::Kind::standard_package;
        result.library = support::vhdl_symbol("std");
        return result;
    }
    if (resolution.served_package.valid()) {
        Denotation result;
        result.kind = Denotation::Kind::served_package;
        result.library = support::vhdl_symbol("std");
        result.served = resolution.served_package;
        return result;
    }
    if (!resolution.standard.empty() || !resolution.implicit.empty()
        || resolution.undeclared) {
        // A STANDARD type, function or literal, an implicitly declared
        // operation or an entity without a declaration: what follows is
        // selected by type (8.3).
        Denotation result;
        result.kind = Denotation::Kind::object;
        return result;
    }
    return { };
}

Denotation Lookup::template_of(const vhdl::Declaration& declaration) const
{
    // An interface package's members are those of the instance of its
    // template that the actual denotes (6.5.5): by visibility, those the
    // template declares.
    thread_local unsigned depth = 0U;
    if (depth >= max_selected_depth) {
        return { };
    }
    ++depth;
    struct Leave {
        ~Leave() { --depth; }
    } leave;
    const auto parts = support::split_vhdl_selected_name(
        declaration.package->template_name.text());
    if (parts.empty()) {
        return { };
    }
    const auto at = regions_.end(declaration.source);
    Resolution resolution;
    auto denotation = denote(support::vhdl_symbol(parts.front()),
        declaration.scope, at, resolution);
    for (std::size_t index = 1U; index < parts.size(); ++index) {
        if (denotation.kind == Denotation::Kind::none
            || denotation.kind == Denotation::Kind::object) {
            return { };
        }
        resolution = member(
            denotation, support::vhdl_symbol(parts[index]), declaration.scope);
        denotation = denote(resolution);
    }
    return denotation;
}

namespace {

Denotation selected_prefix(const Regions& regions, const UseClause& clause)
{
    const Lookup lookup { regions };
    Resolution resolution;
    auto denotation = lookup.denote(
        clause.path.front(), clause.scope, clause.after, resolution);
    for (std::size_t index = 1U; index + 1U < clause.path.size(); ++index) {
        if (denotation.kind == Denotation::Kind::none
            || denotation.kind == Denotation::Kind::object) {
            return { };
        }
        resolution = lookup.member(denotation, clause.path[index], clause.scope);
        if (resolution.status != Visibility::found) {
            return { };
        }
        denotation = lookup.denote(resolution);
    }
    return denotation;
}

} // namespace

} // namespace fsim::semantic::vhdl_names
