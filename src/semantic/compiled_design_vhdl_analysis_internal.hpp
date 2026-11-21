// SPDX-License-Identifier: Apache-2.0
#pragma once

// Interfaces between the files of compile-time VHDL name analysis (gate BD
// build D3). Frozen for the D3 worker round: a change goes through the lead.
//
//   compiled_design_vhdl_analysis_regions.cpp  Regions (12.1, 12.4 placement,
//                                              13.2 library clauses)
//   compiled_design_vhdl_analysis_lookup.cpp   Lookup (12.3, 12.4, 8.3)
//   compiled_design_vhdl_analysis_walk.cpp     visit_names, selected_parts
//   compiled_design_vhdl_analysis.cpp          analyze_vhdl_names (driver,
//                                              storing, diagnostics)
//
// Every comparison of names is a comparison of interned symbols
// (support::SymbolId, owner 11-19): text is interned once, where a region's
// table is built or a selected name is split, and never compared.

#include "fsim/semantic/compiled_design.hpp"
#include "fsim/semantic/compiled_design_vhdl_analysis.hpp"
#include "fsim/semantic/vhdl_hir.hpp"
#include "fsim/support/symbol.hpp"
#include "fsim/support/vhdl_standard_declaration.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fsim::semantic::vhdl_names {

/// A point of the source text the visibility rules order: a declaration is
/// visible only after its end (12.3), a use clause makes names visible after
/// it (12.4). Points of different files, or of macro/generated text (a span
/// with an expansion), are unordered.
struct TextPoint {
    SourceFileId file;
    std::uint64_t offset { };
    bool ordered { };
};

/// True when `before` is ordered with `after` and lies at or before it (a
/// span end is exclusive: a declaration ending where a name starts precedes
/// it).
/// Unordered points never precede each other; a caller treats an unordered
/// declaration as already visible.
[[nodiscard]] bool precedes(const TextPoint& before, const TextPoint& after) noexcept;

/// One use clause (12.4) in force in a region, from a context clause, a
/// declarative part, or a context declaration a context reference names
/// (13.4); also the implicit `use STD.STANDARD.all` (13.2).
struct UseClause {
    enum class Kind : std::uint8_t {
        /// `use L.P.X;`: the declarations X of package P.
        member,
        /// `use L.P.all;`: every declaration of package P.
        all_of_package,
        /// `use L.all;`: every primary unit of library L.
        all_of_library,
        /// `use L.P;`: the simple name P of the primary unit.
        unit_name,
        /// `use STD.STANDARD.all;` (also the implicit clause of 13.2) where
        /// STANDARD is compiler-served (no STD.STANDARD unit in the design):
        /// its declarations are support::StandardDeclaration rows (16.3).
        standard_all,
        /// `use STD.STANDARD.X;` where STANDARD is compiler-served.
        standard_member,
        /// `use STD.STANDARD;` where STANDARD is compiler-served: the simple
        /// name STANDARD denotes the package.
        standard_name,
        /// `use STD.REFLECTION.all;`, `.X;` and `use STD.REFLECTION;` of
        /// the compiler-served STD.REFLECTION (2019 16.6): `served` names
        /// the package.
        served_all,
        served_member,
        served_name,
        /// A clause whose prefix is not a library logical name (`use P.all`
        /// after `use L.P;` or `use L.all;`, a package instance or a nested
        /// package, 4.9 and 8.3): `path` is resolved by visibility at the
        /// clause, in `scope`, when a name is looked up.
        selected,
    };
    Kind kind { Kind::member };
    /// The package (member, all_of_package) or primary unit (unit_name).
    UnitId unit;
    /// The library logical name (all_of_library; the unit's otherwise).
    support::SymbolId library;
    /// The member designator (member).
    support::SymbolId member;
    /// Names are use-visible from here (the end of the clause); the implicit
    /// clause is unordered (visible everywhere).
    TextPoint after;
    /// The clause's span, for FSIM-VHDL-NAME-002 (invalid when implicit).
    SourceSpanId source;
    /// selected: the parts of the clause's selected name (the last may be
    /// `all`) and the region the clause is written in.
    std::vector<support::SymbolId> path;
    ScopeId scope;
    /// served_*: the compiler-served package's simple name.
    support::SymbolId served;
};

/// A declarative region (12.1) as a symbol table.
struct Region {
    ScopeId scope;
    /// The region immediately enclosing this one (12.1): the parent scope;
    /// for the unit scope of a secondary unit (architecture, package body,
    /// configuration's block), the unit scope of its primary unit, whose
    /// region it extends (12.1 c, d).
    std::optional<ScopeId> enclosing;
    UnitId unit;
    /// The declarations declared immediately within this region, by the
    /// index of their designator's symbol, each list in declaration order.
    std::unordered_map<std::uint32_t, std::vector<DeclarationId>> declared;
    /// The operations a type declaration of this region implicitly declares
    /// (5.2.6, 5.3.2.4, 5.4.3, 5.5.2, 5.6, 5.7: relational and arithmetic
    /// operators, MINIMUM, MAXIMUM, TO_STRING, DEALLOCATE, FILE_OPEN, READ,
    /// ... in the edition of the region's unit), by the index of the
    /// operation designator's symbol: the type declarations declaring them.
    /// They are overloadable and have no vhdl::Declaration of their own.
    std::unordered_map<std::uint32_t, std::vector<DeclarationId>> implicit;
    /// Entities this region declares that have no vhdl::Declaration, by the
    /// index of their designator's symbol, with whether each is
    /// overloadable: a generate parameter (11.8), a statement label (implicitly declared,
    /// 11.1), a member of a package the compiler projects (VITAL, fixed,
    /// float, Synopsys: VhdlStandardPackageForm::projection).
    std::unordered_map<std::uint32_t, bool> undeclared;
    /// The label naming this region when its scope has none of its own (a
    /// labeled loop statement, 10.10), for an expanded name (8.3).
    support::SymbolId label;
    /// The use clauses written in this region, in source order: a unit's
    /// context clause belongs to its unit scope; a use clause of a
    /// declarative part belongs to the innermost scope whose span contains
    /// it (the parser lists every clause in Unit::context, with its span).
    std::vector<UseClause> uses;
    /// The library logical names library clauses of this region make
    /// visible (13.2), with the point each becomes visible from.
    std::vector<std::pair<support::SymbolId, TextPoint>> libraries;
};

/// One declaration of the compiler-served STD.STANDARD (16.3) a designator
/// names: a catalog row (type, subtype, function, attribute) or a literal
/// of an enumeration type / a unit of TIME, given by its type's row.
struct StandardEntry {
    enum class Kind : std::uint8_t { type, function, attribute, literal, unit };
    support::StandardDeclaration declaration { };
    Kind kind { Kind::type };
    /// 12.3: functions and enumeration literals are overloadable.
    [[nodiscard]] bool overloadable() const noexcept
    {
        return kind == Kind::function || kind == Kind::literal;
    }
};

/// Every declarative region of a design (regions.cpp). Built once, eagerly,
/// from vhdl_hir declarations, Model scopes and Unit::context; read-only
/// afterwards.
class Regions {
public:
    Regions(const CompiledDesign& design,
        std::span<const VhdlAvailablePrimary> available);
    ~Regions();
    Regions(const Regions&) = delete;
    Regions& operator=(const Regions&) = delete;

    /// The declarations (designator symbol, overloadable) of the package
    /// `name` of library `library` that the compiler serves for the unit
    /// `from` (STD.REFLECTION in VHDL-2019, when the design has no unit of
    /// it), or nullptr.
    [[nodiscard]] const std::vector<std::pair<support::SymbolId, bool>>*
    served_members(support::SymbolId library, support::SymbolId name,
        const vhdl::Unit& from) const;

    /// Whether `library` holds the primary unit `name` that the design has
    /// no HIR of (VhdlAvailablePrimary).
    [[nodiscard]] bool available_primary(
        support::SymbolId library, support::SymbolId name) const;

    /// Whether `unit` is a secondary unit whose primary unit the design has
    /// no HIR of (compiled elsewhere or in another edition): its names may
    /// denote the primary's declarations, so they are resolved when linked.
    [[nodiscard]] bool primary_missing(UnitId unit) const;

    /// The region of `scope`, or nullptr when the scope is not VHDL.
    [[nodiscard]] const Region* find(ScopeId scope) const noexcept;
    /// The design unit whose text `scope` belongs to, or nullptr.
    [[nodiscard]] const vhdl::Unit* unit_of(ScopeId scope) const noexcept;
    /// The library logical name of `unit` (WORK denotes it, 13.2).
    [[nodiscard]] support::SymbolId library_of(const vhdl::Unit& unit) const noexcept;
    /// The primary units of library `library` named `name` (13.1): entity,
    /// package, configuration, context, package instance declarations.
    [[nodiscard]] std::span<const UnitId> primary_units(
        support::SymbolId library, support::SymbolId name) const;
    /// The scope a primary unit's declarations are declared in.
    [[nodiscard]] std::optional<ScopeId> unit_scope(UnitId unit) const noexcept;
    /// The innermost scope of `unit` whose text contains `span` (a
    /// declarative item the HIR lists at the unit, such as a configuration
    /// specification of a block, 7.3.1); the unit scope when none does.
    [[nodiscard]] ScopeId innermost_scope(
        const vhdl::Unit& unit, SourceSpanId span) const;
    /// The child scope of `scope` (a component declaration, a block, a
    /// process of it) holding the text `span` begins in, if any.
    [[nodiscard]] std::optional<ScopeId> child_scope_holding(
        ScopeId scope, SourceSpanId span) const;
    /// Whether library logical name `library` is visible at `at` in
    /// `scope`: STD and WORK always (13.2), the predefined environment's
    /// implicit libraries, and every library clause of an enclosing region
    /// in force at `at`.
    [[nodiscard]] bool library_visible(
        support::SymbolId library, ScopeId scope, const TextPoint& at) const;
    /// The declarations of the compiler-served STD.STANDARD (16.3) named
    /// `designator` in the edition of `unit` (catalog rows available in its
    /// standard, plus STANDARD's enumeration literals and TIME's units).
    [[nodiscard]] std::span<const StandardEntry> standard_declarations(
        support::SymbolId designator, const vhdl::Unit& unit) const;
    /// Whether STD.STANDARD is compiler-served for `unit`'s design (the
    /// design holds no STD.STANDARD unit).
    [[nodiscard]] bool standard_served() const noexcept;
    /// The begin and end points of a span.
    [[nodiscard]] TextPoint begin(SourceSpanId span) const noexcept;
    [[nodiscard]] TextPoint end(SourceSpanId span) const noexcept;
    /// The design.
    [[nodiscard]] const CompiledDesign& design() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

enum class Visibility : std::uint8_t {
    found,
    not_found,
    /// 12.4: two potentially visible non-overloadable homographs (neither
    /// is directly visible); also a library unit name visible as two units.
    ambiguous,
};

struct Resolution {
    Visibility status { Visibility::not_found };
    /// `found` means candidates, units, standard or implicit is non-empty.
    /// found: the one non-overloadable declaration, or every visible
    /// overloadable declaration of the designator (12.3: overloadable
    /// homographs do not hide each other; an explicit declaration hides an
    /// implicit predefined operation it is a homograph of, 12.3 b).
    std::vector<DeclarationId> candidates;
    /// found: the primary units (13.1) the name denotes: `L.P` through a
    /// library prefix, or ones `use L.P;` / `use L.all;` make potentially
    /// visible; for 12.4 a unit is a non-overloadable entity. `found` means
    /// candidates or units is non-empty.
    std::vector<UnitId> units;
    /// found: declarations of the compiler-served STD.STANDARD the name
    /// denotes (no vhdl::Declaration exists for them).
    std::vector<StandardEntry> standard;
    /// found: the name denotes the compiler-served package STD.STANDARD
    /// itself (`STD.STANDARD` as a prefix, 8.3).
    bool standard_package { };
    /// found: the name denotes entities of Region::undeclared.
    bool undeclared { };
    /// ... of a compiler-projected package's region (BD2).
    bool undeclared_projected { };
    /// found: the name denotes a primary unit the design has no HIR of.
    bool available_unit { };
    /// found: the name denotes this compiler-served package of STD (when
    /// valid; Regions::served_members).
    support::SymbolId served_package;
    /// found: type declarations whose implicitly declared operations have
    /// the designator (Region::implicit); overloadable.
    std::vector<DeclarationId> implicit;
    /// ambiguous: the spans of the conflicting use clauses.
    std::vector<SourceSpanId> conflicting_uses;
};

/// What a prefix of an expanded or selected name denotes (8.3).
struct Denotation {
    enum class Kind : std::uint8_t {
        none,
        /// A library logical name: its primary units are the members.
        library,
        /// A primary unit (package, entity, ...): its declarations.
        unit,
        /// The compiler-served package STD.STANDARD: its declarations are
        /// Regions::standard_declarations.
        standard_package,
        /// A compiler-served package of STD other than STANDARD (`served`):
        /// its declarations are Regions::served_members.
        served_package,
        /// A named construct enclosing the name (process, block, generate,
        /// subprogram, loop label) or a package instance / generic package:
        /// the declarations of `scope`.
        region,
        /// An object, or a value of a record or protected type: what
        /// follows is an element or method selected by type, not by
        /// visibility (8.3, 8.4); the walk stops resolving there.
        object,
    };
    Kind kind { Kind::none };
    support::SymbolId library;
    UnitId unit;
    std::optional<ScopeId> scope;
    std::optional<DeclarationId> declaration;
    /// served_package: the package's simple name.
    support::SymbolId served;
};

/// Visibility over Regions (lookup.cpp).
class Lookup {
public:
    explicit Lookup(const Regions& regions);

    /// What `designator` denotes at `at` in `scope` by visibility:
    /// directly visible declarations of the enclosing regions, innermost
    /// first (12.3: a nonoverloadable declaration hides an outer homograph;
    /// a declaration is visible only after its end within its own region),
    /// then use-visible ones (12.4: potentially visible declarations a
    /// directly visible homograph does not hide; two non-overloadable
    /// potentially visible homographs make neither visible => ambiguous).
    [[nodiscard]] Resolution visible(support::SymbolId designator,
        ScopeId scope, const TextPoint& at) const;

    /// `designator` selected within what `prefix` denotes (8.3): a primary
    /// unit of a library, a declaration of a unit's or a region's
    /// declarative part. Not found when `prefix` is an object.
    /// `from` is the scope the name is written in (STANDARD's members are
    /// those of its unit's edition).
    [[nodiscard]] Resolution member(const Denotation& prefix,
        support::SymbolId designator, ScopeId from) const;

    /// What the first simple name of a selected name denotes at `at` in
    /// `scope`: a visible declaration (unit, region, object), else a
    /// visible library logical name (13.2), else none. `resolution` gets
    /// the declaration resolution when it is one.
    [[nodiscard]] Denotation denote(support::SymbolId designator,
        ScopeId scope, const TextPoint& at, Resolution& resolution) const;

    /// What a member resolution denotes (unit, region or object), for the
    /// next selection of a selected name.
    [[nodiscard]] Denotation denote(const Resolution& resolution) const;

private:
    /// What an interface package `declaration` (6.5.5, no region of its
    /// own) denotes: the region of its generic package template.
    [[nodiscard]] Denotation template_of(const vhdl::Declaration& declaration) const;

    const Regions& regions_;
};

/// How a visited name is resolved.
enum class NameRole : std::uint8_t {
    /// A simple or selected name in an expression, alias target, subtype
    /// indication, profile, mode view, attribute specification, group,
    /// disconnection specification: resolved by visibility at its site.
    ordinary,
    /// A use clause's selected name (`L.P.X`, `L.P.all`, `L.P`): resolved
    /// from its library, never against the region it is written in.
    use_clause,
    /// An entity/configuration/component name of an instance, binding
    /// indication, block or component configuration.
    unit_reference,
    /// The `f` of a formal part `f(s)` in a binding indication's port map
    /// (6.5.7.1): a function name visible at the binding indication when
    /// `f` is no port of the bound entity (a conversion), which the linker
    /// decides; resolved by visibility without an error when nothing is
    /// found (`f` is then the port, indexed).
    conversion_function,
    /// A name actual of a binding indication's generic map (7.3.2.2): a
    /// local generic of the component, which the linker binds, or a type
    /// mark visible at the binding indication (a generic type's actual,
    /// 6.5.7.2); only a type or subtype found by visibility is stored,
    /// without an error when nothing is found.
    binding_type_actual,
    /// The component name of a configuration specification (7.3.1): a
    /// component visible where the specification is written, which is the
    /// innermost region containing it (the HIR lists block specifications
    /// at the unit).
    specification_component,
};

/// One visibility-resolved name and the site it is resolved at (a Name
/// carries no scope of its own).
struct NameSite {
    vhdl::Name* name { };
    ScopeId scope;
    NameRole role { NameRole::ordinary };
    /// The instantiation whose target `name` is (unit_reference only).
    vhdl::Instance* instance { };
};

/// Every visibility-resolved Name of `unit` (walk.cpp): its expressions
/// (with operands and associations), statements, declarations and their
/// profiles and subtypes, type definitions, processes, instances, generate
/// regions, context, disconnection specifications and configurations.
/// Context-resolved names (see analyze_vhdl_names) are not visited, nor is
/// the suffix part of an expression that selects record elements or names
/// an attribute (the walk visits only the Name; the driver stops at an
/// object prefix).
void visit_names(vhdl::Hir& hir, const vhdl::Unit& unit,
    const std::function<void(const NameSite&)>& visit);

/// The simple names of a selected name's text (`a.b.c`, `P."+"`,
/// `\ext.id\.x`), each interned with support::vhdl_symbol; empty for a
/// malformed text. A simple name is one part.
[[nodiscard]] std::vector<support::SymbolId> selected_parts(const vhdl::Name& name);

} // namespace fsim::semantic::vhdl_names
