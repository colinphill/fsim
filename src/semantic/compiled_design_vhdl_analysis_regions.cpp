// SPDX-License-Identifier: Apache-2.0
#include "compiled_design_vhdl_analysis_internal.hpp"

#include "fsim/project/project.hpp"
#include "fsim/semantic/vhdl_standard_catalog.hpp"
#include "fsim/semantic/vhdl_standard_declaration.hpp"
#include "fsim/support/vhdl_identifier.hpp"
#include "fsim/support/vhdl_reflection_declarations.hpp"
#include "fsim/support/vhdl_standard_enumeration.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Declarative regions of a VHDL design as symbol tables (gate BD build D3,
// IEEE 1076-2019 12.1, 12.3, 12.4, 13.2, 13.4). Built once, eagerly, from the
// HIR declarations, the Model scopes and every unit's context items; all
// keys are interned symbols (owner 11-19), text is interned where a table is
// built or a selected name is split and never compared afterwards.
//
// Symbol of a declaration (the key of Region::declared). A reference's
// symbol is Name::symbol = vhdl_symbol(Name::text()), the canonical text:
// basic identifiers lower-cased, extended identifiers exactly as written
// with their backslashes, character literals ('a') and operator symbols
// as the bare operator text (`+`, `and`; a quoted `"AND"` of a use clause
// is stripped of its quotes and folded here). Declaration::name is DECODED
// (decode_vhdl_identifier: an extended identifier's body without
// backslashes, case kept; a basic identifier lower-cased; a character
// literal with its quotes; an operator without quotes), so
//  - a declaration whose Declaration::spelling is an extended identifier
//    (\...\) is keyed by vhdl_symbol(spelling) = the symbol of a reference;
//  - every other declaration is keyed by vhdl_symbol(name), which folds
//    case and leaves character literals and operators as they are.
// A declaration of an extended identifier whose spelling the parser did not
// record (Declaration::spelling is filled by a token heuristic,
// record_vhdl_spellings) is keyed by its decoded name and cannot be told
// from a basic identifier: see the report.

namespace fsim::semantic::vhdl_names {

bool precedes(const TextPoint& before, const TextPoint& after) noexcept
{
    return before.ordered && after.ordered && before.file == after.file
        && before.offset <= after.offset;
}

namespace {

[[nodiscard]] std::uint64_t key_of(
    const support::SymbolId library, const support::SymbolId name) noexcept
{
    return (static_cast<std::uint64_t>(library.index()) << 32U)
        | static_cast<std::uint64_t>(name.index());
}

// The symbol of a unit's (possibly extended) simple name.
[[nodiscard]] support::SymbolId unit_symbol(
    const std::string_view name, const bool extended)
{
    return support::vhdl_symbol(
        support::encode_vhdl_identifier(name, extended));
}

// The symbol of the designator that ends a selected name: an operator symbol
// is written in quotes in a use clause and declared without them.
[[nodiscard]] support::SymbolId designator_symbol(const std::string_view part)
{
    if (part.size() >= 2U && part.front() == '"' && part.back() == '"') {
        return support::vhdl_symbol(part.substr(1U, part.size() - 2U));
    }
    return support::vhdl_symbol(part);
}

// The designators of the operations a type declaration implicitly declares
// in the edition `year` (IEEE 1076-2019 5.2.6, 5.3.2.4, 5.4.3, 5.5.2, 9.2;
// 1076-1987/1993/2008 likewise): only the designators, the profiles are
// 12.5's business.
[[nodiscard]] std::vector<std::string_view> implicit_designators(
    const vhdl::TypeForm form, const std::uint16_t year,
    const bool one_dimensional)
{
    std::vector<std::string_view> result;
    const auto add = [&](std::initializer_list<std::string_view> names) {
        result.insert(result.end(), names.begin(), names.end());
    };
    switch (form) {
    case vhdl::TypeForm::file:
        add({ "read", "write", "endfile" });
        if (year >= 1993U) {
            add({ "file_open", "file_close" });
        }
        if (year >= 2008U) {
            add({ "flush" });
        }
        if (year >= 2019U) {
            add({ "file_rewind", "file_seek", "file_truncate", "file_state",
                "file_mode", "file_position", "file_size", "file_canseek" });
        }
        return result;
    case vhdl::TypeForm::access:
        add({ "=", "/=", "deallocate" });
        return result;
    case vhdl::TypeForm::protected_type:
    case vhdl::TypeForm::protected_body:
    case vhdl::TypeForm::subtype:
    case vhdl::TypeForm::alias:
    case vhdl::TypeForm::unresolved:
        return result;
    case vhdl::TypeForm::record:
        add({ "=", "/=" });
        return result;
    case vhdl::TypeForm::scalar:
        add({ "=", "/=", "<", "<=", ">", ">=", "+", "-", "*", "/", "abs",
            "**", "mod", "rem" });
        break;
    case vhdl::TypeForm::physical:
        add({ "=", "/=", "<", "<=", ">", ">=", "+", "-", "*", "/", "abs" });
        if (year >= 2008U) {
            add({ "mod", "rem" });
        }
        break;
    case vhdl::TypeForm::enumeration:
        add({ "=", "/=", "<", "<=", ">", ">=" });
        if (year >= 2008U) {
            // 9.2.3: matching relational operators (of BIT and STD_ULOGIC;
            // the profiles decide which types).
            add({ "?=", "?/=", "?<", "?<=", "?>", "?>=" });
        }
        break;
    case vhdl::TypeForm::array:
        add({ "=", "/=" });
        if (one_dimensional) {
            add({ "<", "<=", ">", ">=", "&" });
            // 9.2.2, 9.2.4: logical and shift operators of one-dimensional
            // arrays of BIT or BOOLEAN; 9.2.3 matching of BIT and STD_ULOGIC
            // arrays (the profiles decide which element types).
            add({ "and", "or", "nand", "nor", "xor", "not" });
            if (year >= 1993U) {
                add({ "xnor", "sll", "srl", "sla", "sra", "rol", "ror" });
            }
            if (year >= 2008U) {
                add({ "?=", "?/=" });
            }
        }
        break;
    }
    if (year >= 2008U) {
        add({ "minimum", "maximum" });
        if (form != vhdl::TypeForm::array || one_dimensional) {
            add({ "to_string" });
        }
    }
    return result;
}

[[nodiscard]] support::SymbolId declaration_symbol(const vhdl::Declaration& d)
{
    if (d.name.empty()) {
        return { };
    }
    if (!d.spelling.empty()
        && support::is_extended_vhdl_identifier(d.spelling)) {
        return support::vhdl_symbol(d.spelling);
    }
    return support::vhdl_symbol(d.name);
}

// 12.3: a declaration is a named declaration of its region unless it is an
// attribute specification (7.2: it declares nothing, it associates a value).
// A `generated` declaration is the label of a generate statement (11.8: the
// label implicitly declares the generate statement in the enclosing region),
// so it is named.
[[nodiscard]] bool declares_name(const vhdl::Declaration& d) noexcept
{
    return d.form != vhdl::DeclarationForm::attribute_specification;
}

[[nodiscard]] support::SymbolId library_symbol_of(const vhdl::Unit& unit)
{
    if (!unit.library.empty()) {
        return support::vhdl_symbol(unit.library);
    }
    if (!unit.predefined_environment.working_library.empty()) {
        return support::vhdl_symbol(unit.predefined_environment.working_library);
    }
    return support::vhdl_symbol("work");
}

// A primary unit of 13.1: entity, package declaration, configuration,
// context declaration. A package body is a package unit with a primary name.
[[nodiscard]] bool is_primary(const vhdl::Unit& unit) noexcept
{
    switch (unit.kind) {
    case vhdl::UnitKind::entity:
    case vhdl::UnitKind::configuration:
    case vhdl::UnitKind::context:
        return true;
    case vhdl::UnitKind::package:
        return unit.primary_name.empty();
    case vhdl::UnitKind::architecture:
    case vhdl::UnitKind::psl_verification_unit:
        return false;
    }
    return false;
}

} // namespace

struct Regions::State {
    struct ScopePosition {
        ScopeId scope;
        TextPoint begin;
        TextPoint end;
    };
    /// By vhdl::Unit::id value: the unit's scopes (other than its own) with
    /// the text they span.
    std::unordered_map<std::uint32_t, std::vector<ScopePosition>> scope_positions;
    const CompiledDesign* design { };
    const std::vector<SourceSpan>* spans { };
    /// By scope id value; scope invalid for a scope that is not VHDL.
    std::vector<Region> regions;
    /// By vhdl::Unit::id value.
    std::vector<const vhdl::Unit*> units;
    std::vector<support::SymbolId> libraries;
    std::vector<std::vector<support::SymbolId>> implicit_libraries;
    std::unordered_map<std::uint64_t, std::vector<UnitId>> primaries;
    support::SymbolId std_library;
    support::SymbolId work_library;
    support::SymbolId all_symbol;
    support::SymbolId standard_symbol;
    /// The library logical names of the design's units (and STD, WORK).
    std::unordered_set<std::uint32_t> known_libraries;
    /// key_of(library, name) of every VhdlAvailablePrimary.
    std::unordered_set<std::uint64_t> available;
    /// Secondary units whose primary unit the design has no HIR of.
    std::unordered_set<std::uint32_t> missing_primary;
    /// STD.REFLECTION's declarations (2019 16.6) and whether the design
    /// holds a unit of it (then it is an ordinary package).
    support::SymbolId reflection_symbol { support::vhdl_symbol("reflection") };
    std::vector<std::pair<support::SymbolId, bool>> reflection_members;
    bool reflection_unit { };
    /// Whether the design holds a STD.STANDARD package unit; when not, the
    /// compiler serves it (16.3) and `standard_tables` are its declarations.
    bool standard_unit { };
    using StandardTable
        = std::unordered_map<std::uint32_t, std::vector<StandardEntry>>;
    /// By edition year, built on first use.
    mutable std::unordered_map<std::uint16_t, StandardTable> standard_tables;

    /// STANDARD's declarations in the edition `year` (16.3): the catalog
    /// rows the edition has, the literals of its enumeration types and the
    /// units of TIME.
    [[nodiscard]] const StandardTable& standard_table(
        const std::uint16_t year) const
    {
        if (const auto found = standard_tables.find(year);
            found != standard_tables.end()) {
            return found->second;
        }
        StandardTable table;
        std::unordered_map<std::string, support::StandardDeclaration> types;
        const auto rows = vhdl_standard_catalog();
        for (std::size_t row { }; row < rows.size(); ++row) {
            const auto declaration = static_cast<support::StandardDeclaration>(
                static_cast<std::uint16_t>(row + 1U));
            if (!support::available(declaration, year)) {
                continue;
            }
            StandardEntry entry;
            entry.declaration = declaration;
            switch (rows[row].kind) {
            case 't':
                entry.kind = StandardEntry::Kind::type;
                break;
            case 'f':
            case 'i':
                entry.kind = StandardEntry::Kind::function;
                break;
            case 'a':
                entry.kind = StandardEntry::Kind::attribute;
                break;
            default:
                continue; // 'r': a range record type has no designator
            }
            std::string name { rows[row].name };
            std::ranges::transform(name, name.begin(), [](const char c) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            });
            if (entry.kind == StandardEntry::Kind::type) {
                types.emplace(name, declaration);
            }
            table[designator_symbol(name).index()].push_back(entry);
        }
        const auto add = [&](const std::string_view type,
                             const std::string_view literal,
                             const StandardEntry::Kind kind) {
            const auto found = types.find(std::string { type });
            if (found == types.end()) {
                return; // the edition has no such type
            }
            table[support::vhdl_symbol(literal).index()].push_back(
                { found->second, kind });
        };
        // The identifier literals of BOOLEAN, CHARACTER, SEVERITY_LEVEL,
        // FILE_OPEN_KIND, FILE_OPEN_STATUS, RANGE_DIRECTION and
        // FILE_ORIGIN_KIND (character literals are not names).
        add("boolean", "false", StandardEntry::Kind::literal);
        add("boolean", "true", StandardEntry::Kind::literal);
        for (const auto control :
            support::vhdl_standard_enumeration_detail::character_controls) {
            add("character", control, StandardEntry::Kind::literal);
        }
        add("character", "del", StandardEntry::Kind::literal);
        if (year >= 1993U) {
            for (unsigned position = 128U; position < 160U; ++position) {
                add("character", "c" + std::to_string(position),
                    StandardEntry::Kind::literal);
            }
        }
        for (const auto& type : support::vhdl_standard_enumerations()) {
            for (const auto literal : type.literals) {
                add(type.name, literal, StandardEntry::Kind::literal);
            }
        }
        for (const auto literal : { "file_origin_begin",
                 "file_origin_current", "file_origin_end" }) {
            add("file_origin_kind", literal, StandardEntry::Kind::literal);
        }
        for (const auto unit : { "fs", "ps", "ns", "us", "ms", "sec", "min",
                 "hr" }) {
            add("time", unit, StandardEntry::Kind::unit);
        }
        return standard_tables.emplace(year, std::move(table)).first->second;
    }

    [[nodiscard]] const vhdl::Unit* unit_by_id(const UnitId id) const noexcept
    {
        return id.valid() && id.value() < units.size() ? units[id.value()]
                                                        : nullptr;
    }

    [[nodiscard]] TextPoint point(
        const SourceSpanId span, const bool at_end) const noexcept
    {
        if (!span.valid() || span.value() >= spans->size()) {
            return { };
        }
        const auto& record = (*spans)[span.value()];
        if (record.expansion || !record.file.valid()) {
            return { };
        }
        return { record.file,
            at_end ? record.end.offset : record.begin.offset, true };
    }

    // The primary units of (library, name) accepted by `accept`; when several
    // remain (one design holds the units of several standards' libraries)
    // those analyzed under `standard` are preferred.
    template <typename Accept>
    [[nodiscard]] std::vector<const vhdl::Unit*> pick(
        const support::SymbolId library, const support::SymbolId name,
        const std::string& standard, Accept accept) const
    {
        std::vector<const vhdl::Unit*> result;
        const auto found = primaries.find(key_of(library, name));
        if (found == primaries.end()) {
            return result;
        }
        for (const auto id : found->second) {
            const auto* unit = unit_by_id(id);
            if (unit != nullptr && accept(*unit)) {
                result.push_back(unit);
            }
        }
        if (result.size() > 1U) {
            std::vector<const vhdl::Unit*> same;
            for (const auto* unit : result) {
                if (unit->standard == standard) {
                    same.push_back(unit);
                }
            }
            if (!same.empty()) {
                result = std::move(same);
            }
        }
        return result;
    }

    [[nodiscard]] support::SymbolId library_of(const vhdl::Unit& unit) const
    {
        if (unit.id.valid() && unit.id.value() < units.size()
            && units[unit.id.value()] == &unit) {
            return libraries[unit.id.value()];
        }
        return library_symbol_of(unit);
    }

    // 13.2: WORK denotes the library of the unit the clause belongs to.
    [[nodiscard]] support::SymbolId resolve_library(
        const support::SymbolId name, const support::SymbolId owner) const
    {
        return name == work_library ? owner : name;
    }

    [[nodiscard]] Region* region_of(const ScopeId scope) noexcept
    {
        if (!scope.valid() || scope.value() >= regions.size()
            || !regions[scope.value()].scope.valid()) {
            return nullptr;
        }
        return &regions[scope.value()];
    }

    /// 12.4 and 13.2: `use L.P.X`, `L.P.all`, `L.all`, `L.P`.
    void add_use(Region& target, const vhdl::Name& name,
        const vhdl::Unit& owner, const support::SymbolId owner_library,
        const TextPoint& after, const SourceSpanId source) const
    {
        const auto parts = support::split_vhdl_selected_name(name.text());
        if (parts.size() < 2U) {
            return;
        }
        UseClause clause;
        clause.after = after;
        clause.source = source;
        const auto first = support::vhdl_symbol(parts[0]);
        if (parts.size() > 3U || !known_libraries.contains(first.index())) {
            // The prefix is a package (made visible by an earlier clause),
            // a package instance or a nested package (4.9, 8.3): resolved
            // by visibility at the clause when names are looked up.
            clause.kind = UseClause::Kind::selected;
            for (const auto& part : parts) {
                clause.path.push_back(designator_symbol(part));
            }
            clause.scope = target.scope;
            target.uses.push_back(clause);
            return;
        }
        const auto library = resolve_library(first, owner_library);
        if (parts.size() == 2U) {
            const auto second = designator_symbol(parts[1]);
            if (second == all_symbol) {
                clause.kind = UseClause::Kind::all_of_library;
                clause.library = library;
                target.uses.push_back(clause);
                return;
            }
            if (!reflection_unit && library == std_library
                && second == reflection_symbol) {
                // `use STD.REFLECTION;` of the compiler-served package.
                clause.kind = UseClause::Kind::served_name;
                clause.library = std_library;
                clause.served = reflection_symbol;
                target.uses.push_back(clause);
                return;
            }
            if (!standard_unit && library == std_library
                && second == standard_symbol) {
                // `use STD.STANDARD;` of the compiler-served package.
                clause.kind = UseClause::Kind::standard_name;
                clause.library = std_library;
                target.uses.push_back(clause);
                return;
            }
            clause.kind = UseClause::Kind::unit_name;
            for (const auto* unit : pick(library, second, owner.standard,
                     [](const vhdl::Unit&) { return true; })) {
                clause.unit = unit->id;
                clause.library = library_of(*unit);
                target.uses.push_back(clause);
            }
            return;
        }
        const auto third = designator_symbol(parts[2]);
        const bool all = third == all_symbol;
        if (!standard_unit && library == std_library
            && designator_symbol(parts[1]) == standard_symbol) {
            // `use STD.STANDARD.X;` / `.all` of the compiler-served
            // package (16.3).
            clause.kind = all ? UseClause::Kind::standard_all
                              : UseClause::Kind::standard_member;
            clause.library = std_library;
            if (!all) {
                clause.member = third;
            }
            target.uses.push_back(clause);
            return;
        }
        if (!reflection_unit && library == std_library
            && designator_symbol(parts[1]) == reflection_symbol) {
            // `use STD.REFLECTION.X;` / `.all` (2019 16.6).
            clause.kind = all ? UseClause::Kind::served_all
                              : UseClause::Kind::served_member;
            clause.library = std_library;
            clause.served = reflection_symbol;
            if (!all) {
                clause.member = third;
            }
            target.uses.push_back(clause);
            return;
        }
        clause.kind = all ? UseClause::Kind::all_of_package
                          : UseClause::Kind::member;
        if (!all) {
            clause.member = third;
        }
        for (const auto* unit : pick(library, designator_symbol(parts[1]),
                 owner.standard, [](const vhdl::Unit& candidate) {
                     return candidate.kind == vhdl::UnitKind::package;
                 })) {
            clause.unit = unit->id;
            clause.library = library_of(*unit);
            target.uses.push_back(clause);
        }
    }

    void add_item(Region& target, const vhdl::ContextItem& item,
        const vhdl::Unit& owner, const support::SymbolId owner_library,
        const TextPoint& after, const SourceSpanId source,
        std::vector<UnitId>& expanding) const
    {
        switch (item.kind) {
        case vhdl::ContextKind::library_clause:
            for (const auto& name : item.selected_names) {
                const auto parts
                    = support::split_vhdl_selected_name(name.text());
                if (parts.size() == 1U) {
                    target.libraries.emplace_back(
                        support::vhdl_symbol(parts[0]), after);
                }
            }
            break;
        case vhdl::ContextKind::use_clause:
            for (const auto& name : item.selected_names) {
                add_use(target, name, owner, owner_library, after, source);
            }
            break;
        case vhdl::ContextKind::context_reference:
            // 13.4: `context L.C;` stands for the items of context
            // declaration C, as if written here. A context that is already
            // being expanded is skipped (cycle).
            for (const auto& name : item.selected_names) {
                const auto parts
                    = support::split_vhdl_selected_name(name.text());
                if (parts.size() != 2U) {
                    continue;
                }
                const auto library = resolve_library(
                    support::vhdl_symbol(parts[0]), owner_library);
                for (const auto* context : pick(library,
                         designator_symbol(parts[1]), owner.standard,
                         [](const vhdl::Unit& candidate) {
                             return candidate.kind == vhdl::UnitKind::context;
                         })) {
                    if (std::ranges::find(expanding, context->id)
                        != expanding.end()) {
                        continue;
                    }
                    expanding.push_back(context->id);
                    const auto context_library = library_of(*context);
                    for (const auto& inner : context->context) {
                        add_item(target, inner, *context, context_library,
                            after, source, expanding);
                    }
                    expanding.pop_back();
                }
            }
            break;
        }
    }

    struct ItemPosition {
        std::uint32_t index { };
        TextPoint begin;
        TextPoint end;
    };

    // 12.1, 12.4: a clause of a declarative part belongs to the innermost
    // region whose text contains it; Unit::context lists every clause of
    // the unit with its span.
    void place_context(const vhdl::Unit& unit,
        const std::vector<std::vector<std::uint32_t>>& unit_scopes,
        const std::vector<Scope>& scopes)
    {
        auto* unit_region = region_of(unit.scope);
        if (unit_region == nullptr) {
            return;
        }
        const auto owner_library = library_of(unit);
        std::vector<UnitId> expanding;
        std::vector<ItemPosition> positioned;
        for (std::size_t index { }; index < unit.context.size(); ++index) {
            const auto& item = unit.context[index];
            const auto begin = point(item.source, false);
            if (!begin.ordered) {
                add_item(*unit_region, item, unit, owner_library,
                    point(item.source, true), item.source, expanding);
                continue;
            }
            positioned.push_back({ static_cast<std::uint32_t>(index), begin,
                point(item.source, true) });
        }
        std::vector<ScopePosition> candidates;
        if (unit.id.value() < unit_scopes.size()) {
            for (const auto scope_index : unit_scopes[unit.id.value()]) {
                const auto& scope = scopes[scope_index];
                if (scope.id == unit.scope) {
                    continue;
                }
                const auto begin = point(scope.source, false);
                if (begin.ordered) {
                    candidates.push_back(
                        { scope.id, begin, point(scope.source, true) });
                }
            }
        }
        scope_positions[unit.id.value()] = candidates;
        if (positioned.empty()) {
            return;
        }
        std::ranges::sort(positioned,
            [](const ItemPosition& left, const ItemPosition& right) {
                if (left.begin.file != right.begin.file) {
                    return left.begin.file < right.begin.file;
                }
                if (left.begin.offset != right.begin.offset) {
                    return left.begin.offset < right.begin.offset;
                }
                return left.index < right.index;
            });
        // Outer scopes first; of two spans that are equal the later
        // created scope is the deeper one.
        std::ranges::sort(candidates,
            [](const ScopePosition& left, const ScopePosition& right) {
                if (left.begin.file != right.begin.file) {
                    return left.begin.file < right.begin.file;
                }
                if (left.begin.offset != right.begin.offset) {
                    return left.begin.offset < right.begin.offset;
                }
                if (left.end.offset != right.end.offset) {
                    return left.end.offset > right.end.offset;
                }
                return left.scope < right.scope;
            });
        const auto contains = [](const ScopePosition& outer,
                                  const TextPoint& begin,
                                  const TextPoint& end) {
            return outer.begin.file == begin.file
                && outer.begin.offset <= begin.offset
                && end.offset <= outer.end.offset;
        };
        std::vector<const ScopePosition*> stack;
        std::size_t next { };
        for (const auto& placed : positioned) {
            while (next < candidates.size()
                && (candidates[next].begin.file < placed.begin.file
                    || (candidates[next].begin.file == placed.begin.file
                        && candidates[next].begin.offset
                            <= placed.begin.offset))) {
                const auto& candidate = candidates[next++];
                while (!stack.empty()
                    && !contains(*stack.back(), candidate.begin,
                        candidate.end)) {
                    stack.pop_back();
                }
                stack.push_back(&candidate);
            }
            while (!stack.empty()
                && !contains(*stack.back(), placed.begin, placed.end)) {
                stack.pop_back();
            }
            auto* target = stack.empty() ? unit_region
                                         : region_of(stack.back()->scope);
            if (target == nullptr) {
                target = unit_region;
            }
            const auto& item = unit.context[placed.index];
            add_item(*target, item, unit, owner_library, placed.end,
                item.source, expanding);
        }
    }

    explicit State(const CompiledDesign& source_design)
        : design { &source_design }
        , spans { &source_design.semantics.source_spans() }
        , std_library { support::vhdl_symbol("std") }
        , work_library { support::vhdl_symbol("work") }
        , all_symbol { support::vhdl_symbol("all") }
        , standard_symbol { support::vhdl_symbol("standard") }
    {
        known_libraries.insert(std_library.index());
        known_libraries.insert(work_library.index());
        const auto& hir_units = source_design.vhdl_hir.units();
        const auto& scopes = source_design.semantics.scopes();

        std::size_t unit_count { };
        for (const auto& unit : hir_units) {
            if (unit.id.valid()) {
                unit_count = std::max<std::size_t>(
                    unit_count, static_cast<std::size_t>(unit.id.value()) + 1U);
            }
        }
        units.assign(unit_count, nullptr);
        libraries.assign(unit_count, support::SymbolId { });
        implicit_libraries.resize(unit_count);
        for (const auto& unit : hir_units) {
            if (!unit.id.valid()) {
                continue;
            }
            units[unit.id.value()] = &unit;
            libraries[unit.id.value()] = library_symbol_of(unit);
            known_libraries.insert(libraries[unit.id.value()].index());
            // 13.2: the implicit library names of the predefined
            // environment (STD and WORK by default).
            for (const auto& library :
                unit.predefined_environment.implicit_libraries) {
                implicit_libraries[unit.id.value()].push_back(
                    support::vhdl_symbol(library));
            }
            if (unit.kind == vhdl::UnitKind::package && is_primary(unit)
                && libraries[unit.id.value()] == std_library
                && unit_symbol(unit.name, unit.extended_name)
                    == standard_symbol) {
                standard_unit = true;
            }
            if (unit.kind == vhdl::UnitKind::package && is_primary(unit)
                && libraries[unit.id.value()] == std_library
                && unit_symbol(unit.name, unit.extended_name)
                    == reflection_symbol) {
                reflection_unit = true;
            }
            if (is_primary(unit)) {
                primaries[key_of(libraries[unit.id.value()],
                              unit_symbol(unit.name, unit.extended_name))]
                    .push_back(unit.id);
            }
        }

        // A SystemVerilog module, interface or program of a library is a
        // design entity a VHDL instance or binding may name (mixed
        // contract, IEEE 1076-2019 13.1 / UG900): a primary unit of its
        // library for expanded names; it has no VHDL region.
        for (const auto& unit : source_design.systemverilog_hir.units()) {
            if (!unit.id.valid() || unit.name.empty()
                || (unit.kind != sv::UnitKind::module
                    && unit.kind != sv::UnitKind::interface
                    && unit.kind != sv::UnitKind::program)) {
                continue;
            }
            const auto library = support::vhdl_symbol(
                unit.library.empty() ? std::string_view { "work" }
                                     : std::string_view { unit.library });
            known_libraries.insert(library.index());
            primaries[key_of(library, support::vhdl_symbol(unit.name))]
                .push_back(unit.id);
        }

        // One region per VHDL scope; the scopes of every unit, for placing
        // its context items.
        std::size_t scope_count { };
        for (const auto& scope : scopes) {
            scope_count = std::max<std::size_t>(
                scope_count, static_cast<std::size_t>(scope.id.value()) + 1U);
        }
        regions.resize(scope_count);
        std::vector<std::vector<std::uint32_t>> unit_scopes(unit_count);
        for (std::size_t index { }; index < scopes.size(); ++index) {
            const auto& scope = scopes[index];
            if (unit_by_id(scope.unit) == nullptr) {
                continue;
            }
            auto& region = regions[scope.id.value()];
            region.scope = scope.id;
            region.enclosing = scope.parent;
            region.unit = scope.unit;
            unit_scopes[scope.unit.value()].push_back(
                static_cast<std::uint32_t>(index));
        }

        // 12.1 c, d: an architecture or package body extends the declarative
        // region of its primary unit, which belongs to the same library.
        for (const auto& unit : hir_units) {
            const bool architecture = unit.kind == vhdl::UnitKind::architecture;
            const bool body = unit.kind == vhdl::UnitKind::package
                && !unit.primary_name.empty();
            auto* region = (architecture || body) ? region_of(unit.scope)
                                                  : nullptr;
            if (region == nullptr) {
                continue;
            }
            const auto wanted = architecture ? vhdl::UnitKind::entity
                                             : vhdl::UnitKind::package;
            const auto primary = pick(libraries[unit.id.value()],
                unit_symbol(unit.primary_name, unit.extended_primary_name),
                unit.standard, [&](const vhdl::Unit& candidate) {
                    return candidate.kind == wanted;
                });
            if (!primary.empty()) {
                region->enclosing = primary.front()->scope;
            } else {
                missing_primary.insert(unit.id.value());
            }
        }

        // 12.3: the declarations immediately within each region, in
        // declaration order.
        for (const auto& declaration : source_design.vhdl_hir.declarations()) {
            if (!declares_name(declaration)) {
                continue;
            }
            auto* region = region_of(declaration.scope);
            const auto symbol = declaration_symbol(declaration);
            if (region != nullptr && symbol.valid()) {
                region->declared[symbol.index()].push_back(declaration.id);
            }
        }

        // 5.x: the operations each type declaration implicitly declares in
        // its region (overloadable; an explicit homograph hides one, 12.3).
        for (const auto& declaration : source_design.vhdl_hir.declarations()) {
            // `type T is range ...` is held as a subtype declaration of its
            // predefined base marked scalar_type (a type declaration all the
            // same, 5.2.3.1, 5.2.5.1).
            if ((declaration.form != vhdl::DeclarationForm::type
                    && declaration.form != vhdl::DeclarationForm::subtype)
                || !declaration.declared_type) {
                continue;
            }
            auto* region = region_of(declaration.scope);
            const auto* const type
                = source_design.vhdl_hir.find_type(*declaration.declared_type);
            const auto* const unit
                = region == nullptr ? nullptr : unit_by_id(region->unit);
            if (region == nullptr || type == nullptr || unit == nullptr
                || (declaration.form == vhdl::DeclarationForm::subtype
                    && !type->scalar_type)) {
                continue;
            }
            const auto standard = project::parse_vhdl_standard(unit->standard);
            if (!standard) {
                continue;
            }
            // `type T is range ...` declares a distinct integer or floating
            // type (5.2.3.1, 5.2.5.1), kept as a subtype of its predefined
            // base (TypeDefinition::scalar_type): it has the scalar
            // operations.
            const auto form = type->scalar_type ? vhdl::TypeForm::scalar : type->form;
            for (const auto designator : implicit_designators(form,
                     standard_year(*standard),
                     type->array_dimensions.size() == 1U)) {
                region->implicit[support::vhdl_symbol(designator).index()]
                    .push_back(declaration.id);
            }
        }

        // Entities without a vhdl::Declaration (Region::undeclared).
        const auto undeclare = [&](const ScopeId scope,
                                   const support::SymbolId symbol,
                                   const bool overloadable) {
            auto* region = region_of(scope);
            if (region != nullptr && symbol.valid()
                && !region->declared.contains(symbol.index())) {
                region->undeclared.emplace(symbol.index(), overloadable);
            }
        };
        // (A generate parameter, 11.8, and GUARD, 11.2, are declarations
        // the HIR builder makes.)
        for (const auto& unit : hir_units) {
            // The members of a package the compiler projects.
            if (unit.standard_package_form
                == support::VhdlStandardPackageForm::projection) {
                for (const auto& member : unit.standard_package_declarations) {
                    undeclare(unit.scope, support::vhdl_symbol(member), true);
                }
            }
        }
        // 11.1: a statement label is implicitly declared in the enclosing
        // region (a named process, block or generate scope whose name no
        // declaration of the parent declares); a labeled loop names its
        // region (10.10).
        for (const auto& scope : scopes) {
            const auto* const owner = unit_by_id(scope.unit);
            if (scope.role == ScopeRole::named && scope.parent
                && owner != nullptr && owner->scope != scope.id
                && !scope.name.empty()) {
                undeclare(*scope.parent, support::vhdl_symbol(scope.name), false);
            }
        }
        // A labeled statement's or instance's label (11.1, 10.1): an entity
        // of the region the statement is in.
        for (const auto& statement : source_design.vhdl_hir.statements()) {
            if (!statement.label.empty()) {
                undeclare(statement.scope, support::vhdl_symbol(statement.label),
                    false);
            }
        }
        for (const auto& instance : source_design.vhdl_hir.instances()) {
            if (!instance.name.empty()) {
                undeclare(instance.scope, support::vhdl_symbol(instance.name),
                    false);
            }
        }
        for (const auto& statement : source_design.vhdl_hir.statements()) {
            if (statement.nested_scope && !statement.loop_label.empty()) {
                const auto symbol = support::vhdl_symbol(statement.loop_label);
                if (auto* region = region_of(*statement.nested_scope)) {
                    region->label = symbol;
                    if (region->enclosing) {
                        undeclare(*region->enclosing, symbol, false);
                    }
                }
            }
        }

        // 13.2: the implicit use clauses of every unit's region, before its
        // written ones; then 12.4: the written clauses.
        for (const auto& unit : hir_units) {
            auto* region = region_of(unit.scope);
            if (region == nullptr) {
                continue;
            }
            for (const auto& entry :
                unit.predefined_environment.implicit_packages) {
                const auto parts = support::split_vhdl_selected_name(entry);
                if (parts.size() != 3U
                    || designator_symbol(parts[2]) != all_symbol) {
                    continue;
                }
                UseClause clause;
                clause.kind = UseClause::Kind::all_of_package;
                for (const auto* package : pick(
                         resolve_library(support::vhdl_symbol(parts[0]),
                             libraries[unit.id.value()]),
                         designator_symbol(parts[1]), unit.standard,
                         [](const vhdl::Unit& candidate) {
                             return candidate.kind == vhdl::UnitKind::package;
                         })) {
                    clause.unit = package->id;
                    clause.library = library_of(*package);
                    region->uses.push_back(clause);
                }
            }
            // 13.2: every design unit but STD.STANDARD itself is preceded
            // by `use STD.STANDARD.all;`, whatever its predefined
            // environment lists (a unit of the standard libraries' own
            // build lists none).
            const bool is_standard = library_of(unit) == std_library
                && designator_symbol(unit.name) == standard_symbol
                && unit.kind == vhdl::UnitKind::package;
            const bool has_standard = std::ranges::any_of(region->uses,
                [&](const UseClause& clause) {
                    const auto* used = unit_by_id(clause.unit);
                    return clause.kind == UseClause::Kind::all_of_package
                        && used != nullptr && library_of(*used) == std_library
                        && designator_symbol(used->name) == standard_symbol;
                });
            if (!is_standard && !has_standard) {
                if (!standard_unit) {
                    // STANDARD is compiler-served (16.3).
                    UseClause clause;
                    clause.kind = UseClause::Kind::standard_all;
                    clause.library = std_library;
                    region->uses.insert(region->uses.begin(), clause);
                }
                for (const auto* package : pick(
                         resolve_library(std_library,
                             libraries[unit.id.value()]),
                         standard_symbol, unit.standard,
                         [](const vhdl::Unit& candidate) {
                             return candidate.kind == vhdl::UnitKind::package;
                         })) {
                    UseClause clause;
                    clause.kind = UseClause::Kind::all_of_package;
                    clause.unit = package->id;
                    clause.library = std_library;
                    region->uses.push_back(clause);
                }
            }
            place_context(unit, unit_scopes, scopes);
        }
    }
};

Regions::Regions(const CompiledDesign& design,
    const std::span<const VhdlAvailablePrimary> available)
    : state_ { std::make_unique<State>(design) }
{
    for (const auto& unit : available) {
        state_->known_libraries.insert(unit.library.index());
        state_->available.insert(key_of(unit.library, unit.name));
    }
}

const std::vector<std::pair<support::SymbolId, bool>>* Regions::served_members(
    const support::SymbolId library, const support::SymbolId name,
    const vhdl::Unit& from) const
{
    const auto standard = project::parse_vhdl_standard(from.standard);
    if (state_->reflection_unit || !(library == state_->std_library)
        || !(name == state_->reflection_symbol) || !standard
        || *standard != project::VhdlStandard::vhdl_2019) {
        return nullptr;
    }
    if (state_->reflection_members.empty()) {
        for (const auto& entry : support::vhdl_reflection_declarations) {
            state_->reflection_members.emplace_back(
                support::vhdl_symbol(entry.designator), entry.overloadable);
        }
    }
    return &state_->reflection_members;
}

bool Regions::primary_missing(const UnitId unit) const
{
    return state_->missing_primary.contains(unit.value());
}

bool Regions::available_primary(
    const support::SymbolId library, const support::SymbolId name) const
{
    return state_->available.contains(key_of(library, name));
}

Regions::~Regions() = default;

const Region* Regions::find(const ScopeId scope) const noexcept
{
    if (!scope.valid() || scope.value() >= state_->regions.size()
        || !state_->regions[scope.value()].scope.valid()) {
        return nullptr;
    }
    return &state_->regions[scope.value()];
}

const vhdl::Unit* Regions::unit_of(const ScopeId scope) const noexcept
{
    const auto* region = find(scope);
    return region == nullptr ? nullptr : state_->unit_by_id(region->unit);
}

support::SymbolId Regions::library_of(const vhdl::Unit& unit) const noexcept
{
    return state_->library_of(unit);
}

std::span<const UnitId> Regions::primary_units(
    const support::SymbolId library, const support::SymbolId name) const
{
    const auto found = state_->primaries.find(key_of(library, name));
    if (found == state_->primaries.end()) {
        return { };
    }
    return found->second;
}

ScopeId Regions::innermost_scope(
    const vhdl::Unit& unit, const SourceSpanId span) const
{
    const auto begin = this->begin(span);
    const auto end = this->end(span);
    ScopeId result = unit.scope;
    const auto found = state_->scope_positions.find(unit.id.value());
    if (found == state_->scope_positions.end() || !begin.ordered) {
        return result;
    }
    const State::ScopePosition* best = nullptr;
    for (const auto& candidate : found->second) {
        if (candidate.begin.file != begin.file
            || candidate.begin.offset > begin.offset
            || end.offset > candidate.end.offset) {
            continue;
        }
        if (best == nullptr || candidate.begin.offset > best->begin.offset
            || (candidate.begin.offset == best->begin.offset
                && candidate.end.offset <= best->end.offset)) {
            best = &candidate;
        }
    }
    return best != nullptr ? best->scope : result;
}

std::optional<ScopeId> Regions::unit_scope(const UnitId unit) const noexcept
{
    const auto* record = state_->unit_by_id(unit);
    if (record == nullptr || !record->scope.valid()) {
        return std::nullopt;
    }
    return record->scope;
}

bool Regions::library_visible(const support::SymbolId library,
    const ScopeId scope, const TextPoint& at) const
{
    // 13.2: STD and WORK are implicitly declared.
    if (library == state_->std_library || library == state_->work_library) {
        return true;
    }
    if (const auto* unit = unit_of(scope);
        unit != nullptr && unit->id.value() < state_->implicit_libraries.size()) {
        const auto& implicit = state_->implicit_libraries[unit->id.value()];
        if (std::ranges::find(implicit, library) != implicit.end()) {
            return true;
        }
    }
    // A library clause of an enclosing region makes the name visible from
    // the end of the clause (12.1: the chain runs through a secondary
    // unit into its primary unit, whose clauses apply to it, 13.4).
    for (const auto* region = find(scope); region != nullptr;
        region = region->enclosing ? find(*region->enclosing) : nullptr) {
        for (const auto& [name, after] : region->libraries) {
            if (name != library) {
                continue;
            }
            const bool comparable
                = after.ordered && at.ordered && after.file == at.file;
            if (!comparable || after.offset <= at.offset) {
                return true;
            }
        }
    }
    return false;
}

std::span<const StandardEntry> Regions::standard_declarations(
    const support::SymbolId designator, const vhdl::Unit& unit) const
{
    if (state_->standard_unit || !designator.valid()) {
        return { };
    }
    const auto standard = project::parse_vhdl_standard(unit.standard);
    if (!standard) {
        return { };
    }
    const auto& table = state_->standard_table(standard_year(*standard));
    const auto found = table.find(designator.index());
    if (found == table.end()) {
        return { };
    }
    return found->second;
}

bool Regions::standard_served() const noexcept
{
    return !state_->standard_unit;
}

TextPoint Regions::begin(const SourceSpanId span) const noexcept
{
    return state_->point(span, false);
}

TextPoint Regions::end(const SourceSpanId span) const noexcept
{
    return state_->point(span, true);
}

const CompiledDesign& Regions::design() const noexcept
{
    return *state_->design;
}

} // namespace fsim::semantic::vhdl_names
