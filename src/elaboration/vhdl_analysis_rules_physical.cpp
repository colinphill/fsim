// SPDX-License-Identifier: Apache-2.0
//
// Compile-time analysis rules for physical types and for names that must
// denote a particular kind of entity (IEEE 1076-2019 5.2.4.1, 8.3, 4.7, 4.8).
// Each rule decides from the unit as analyzed and never from a generic's
// value, an actual or an unconstrained interface object's bounds; an
// undecidable case reports nothing, and elaboration keeps its own checks.
#include "vhdl_analysis_rules.hpp"

#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/support/expression_marker.hpp"
#include "fsim/support/symbol.hpp"
#include "fsim/support/vhdl_standard_declaration.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fsim::elaboration {

namespace {

namespace vh = semantic::vhdl;
using Issue = VhdlHirTypeValidationIssue;

/// Positions in the design's VHDL HIR of the items of the entity and the
/// architecture, in HIR order; every position when the lookup indexes are
/// stale (the callers still filter by scope).
struct UnitItems {
    std::vector<std::uint32_t> declarations;
    std::vector<std::uint32_t> types;
    std::vector<std::uint32_t> expressions;
    std::vector<std::uint32_t> statements;
    std::vector<std::uint32_t> processes;
    std::vector<std::uint32_t> instances;
};

UnitItems unit_items(const semantic::CompiledDesign& design,
    const vh::Unit& entity, const vh::Unit& architecture)
{
    const auto& hir = design.vhdl_hir;
    const auto* entity_items = design.vhdl_unit_items(entity.id);
    const auto* architecture_items = design.vhdl_unit_items(architecture.id);
    UnitItems items;
    const auto select = [&](std::vector<std::uint32_t> UnitItems::*result,
                            const std::vector<std::uint32_t>
                                semantic::CompiledVhdlUnitItems::*member,
                            const std::size_t size) {
        auto& positions = items.*result;
        if (entity_items == nullptr || architecture_items == nullptr) {
            positions.resize(size);
            std::iota(positions.begin(), positions.end(), 0U);
        } else if (entity.id == architecture.id) {
            positions = entity_items->*member;
        } else {
            std::ranges::merge(entity_items->*member,
                architecture_items->*member, std::back_inserter(positions));
        }
    };
    using Stored = semantic::CompiledVhdlUnitItems;
    select(&UnitItems::declarations, &Stored::declarations,
        hir.declarations().size());
    select(&UnitItems::types, &Stored::types, hir.types().size());
    select(&UnitItems::expressions, &Stored::expressions,
        hir.expressions().size());
    select(&UnitItems::statements, &Stored::statements,
        hir.statements().size());
    select(&UnitItems::processes, &Stored::processes,
        hir.processes().size());
    select(&UnitItems::instances, &Stored::instances,
        hir.instances().size());
    return items;
}

/// Whether a scope belongs to the entity or the architecture.
bool owned_by_unit(const semantic::CompiledDesign& design,
    const semantic::ScopeId scope, const vh::Unit& entity,
    const vh::Unit& architecture)
{
    const auto* found = design.find_scope(scope);
    return found != nullptr
        && (found->unit == entity.id || found->unit == architecture.id);
}

// ---- physical types (5.2.4.1) --------------------------------------------

/// Whether a range bound of a physical type is of a real type (5.2.4.1:
/// each bound shall be a locally static expression of some integer type). A
/// real literal, or an expression the typing gave a real type, is decided
/// without evaluation.
bool real_bound(const semantic::SpecializedHirUnit& specialization,
    const std::optional<semantic::ExpressionId>& id)
{
    if (!id) {
        return false;
    }
    const auto bound = specialization.find_expression(*id);
    if (!bound || bound->vhdl == nullptr) {
        return false;
    }
    const auto& type = bound->vhdl->type;
    return bound->vhdl->kind == vh::ExpressionKind::real_literal
        || type.kind == vh::TypeIdentity::Kind::universal_real
        || (type.kind == vh::TypeIdentity::Kind::standard
            && type.standard == support::StandardDeclaration::real);
}

/// The units of one physical type declaration. At most one issue is reported
/// per type, as elaboration does.
void check_physical_type(const semantic::SpecializedHirUnit& specialization,
    const vh::TypeDefinition& type, std::vector<Issue>& issues)
{
    // A missing range or primary unit is reported by the parser and
    // elaboration (FSIM-ELAB-VHPHYSICAL-001), not decided here.
    if (!type.scalar_range || type.physical_units.empty()) {
        return;
    }
    if (real_bound(specialization, type.scalar_range->left_expression)
        || real_bound(specialization, type.scalar_range->right_expression)) {
        issues.push_back({ "FSIM-ELAB-VHPHYSICAL-011",
            "the range of a VHDL physical type must have integer bounds",
            type.scalar_range->source });
        return;
    }
    // The units declared so far, with their value in primary units when the
    // literals that define them are all locally static integers.
    struct Earlier {
        support::SymbolId symbol;
        std::optional<std::int64_t> scale;
    };
    std::vector<Earlier> earlier;
    for (std::size_t index { }; index < type.physical_units.size(); ++index) {
        const auto& unit = type.physical_units[index];
        const auto symbol = support::vhdl_symbol(unit.name);
        if (index == 0U) {
            // primary_unit_declaration ::= identifier (5.2.4.1).
            if (unit.scale) {
                issues.push_back({ "FSIM-ELAB-VHPHYSICAL-003",
                    "the primary physical unit cannot have a "
                    "secondary-unit scale",
                    unit.source });
                return;
            }
            earlier.push_back({ symbol, 1 });
            continue;
        }
        const auto invalid = [&] {
            issues.push_back({ "FSIM-ELAB-VHPHYSICAL-004",
                "secondary unit '" + unit.name
                    + "' has an invalid, forward, or overflowing scale",
                unit.source });
        };
        std::optional<std::int64_t> scale;
        const auto literal_view = unit.scale
            ? specialization.find_expression(*unit.scale)
            : std::nullopt;
        // Only `[abstract_literal] unit_name` is decided; another form of
        // the right-hand side leaves this unit's value unknown.
        if (literal_view && literal_view->vhdl != nullptr
            && literal_view->vhdl->marker
                == support::ExpressionMarker::vhdl_physical
            && literal_view->vhdl->operands.size() == 1U) {
            const auto& literal = *literal_view->vhdl;
            const auto prior = std::ranges::find(earlier,
                support::vhdl_symbol(literal.marker_argument),
                &Earlier::symbol);
            // A secondary unit is defined in terms of a unit of this type
            // declared before it (5.2.4.1): any other unit name denotes a
            // unit of another type, a later unit, or nothing.
            if (prior == earlier.end()) {
                invalid();
                return;
            }
            const auto multiplier_view
                = specialization.find_expression(literal.operands.front());
            if (multiplier_view && multiplier_view->vhdl != nullptr) {
                // The abstract literal shall be an integer literal (5.2.4.1).
                if (multiplier_view->vhdl->kind
                    == vh::ExpressionKind::real_literal) {
                    invalid();
                    return;
                }
                const auto multiplier = specialization
                    .evaluate_integral_expression(literal.operands.front());
                if (multiplier) {
                    if (*multiplier <= 0
                        || (prior->scale
                            && *prior->scale
                                > std::numeric_limits<std::int64_t>::max()
                                    / *multiplier)) {
                        invalid();
                        return;
                    }
                    if (prior->scale) {
                        scale = *prior->scale * *multiplier;
                    }
                }
            }
        }
        earlier.push_back({ symbol, scale });
    }
}

void check_physical_types(const semantic::SpecializedHirUnit& specialization,
    const vh::Unit& entity, const vh::Unit& architecture,
    const UnitItems& items, std::vector<Issue>& issues)
{
    const auto& design = specialization.design();
    for (const auto position : items.types) {
        const auto& stored = design.vhdl_hir.types()[position];
        const auto effective = specialization.find_type(stored.id);
        if (!effective || effective->vhdl == nullptr
            || effective->vhdl->form != vh::TypeForm::physical) {
            continue;
        }
        const auto declaration
            = specialization.find_declaration(effective->vhdl->declaration);
        if (!declaration || declaration->vhdl == nullptr
            || !owned_by_unit(
                design, declaration->vhdl->scope, entity, architecture)) {
            continue;
        }
        check_physical_type(specialization, *effective->vhdl, issues);
    }
}

// ---- selected names (8.3) -------------------------------------------------

/// The scopes of the named constructs an expanded name's prefix may denote:
/// processes, block, generate and loop statements, and subprograms (8.3).
std::vector<semantic::ScopeId> construct_scopes(
    const semantic::CompiledDesign& design, const UnitItems& items,
    const vh::Unit& entity, const vh::Unit& architecture)
{
    std::vector<semantic::ScopeId> result;
    const auto& hir = design.vhdl_hir;
    // Block and generate statements (8.3): each region's scope, and the
    // scope of each of its alternatives.
    const auto add_regions = [&](const auto& self,
                                 const std::vector<vh::GenerateRegion>& regions)
        -> void {
        for (const auto& region : regions) {
            result.push_back(region.scope);
            for (const auto& alternative : region.alternatives) {
                if (alternative.scope.valid()) {
                    result.push_back(alternative.scope);
                }
            }
            self(self, region.nested);
        }
    };
    add_regions(add_regions, architecture.generates);
    if (entity.id != architecture.id) {
        add_regions(add_regions, entity.generates);
    }
    for (const auto position : items.processes) {
        result.push_back(hir.processes()[position].scope);
    }
    for (const auto position : items.statements) {
        const auto& statement = hir.statements()[position];
        if (statement.nested_scope) {
            result.push_back(*statement.nested_scope);
        }
    }
    for (const auto position : items.declarations) {
        const auto& declaration = hir.declarations()[position];
        if (declaration.callable && declaration.nested_scope) {
            result.push_back(*declaration.nested_scope);
        }
    }
    return result;
}

/// `F.e` where F is a function (8.3: a selected name denoting a record
/// element needs a record result with that element, and the prefix of an
/// expanded name shall not be a function call). The parser reads `F.e` as a
/// selection from F's result; the prefix's stored name gives F.
void check_function_result_selection(
    const semantic::SpecializedHirUnit& specialization,
    const vh::Expression& member, std::vector<Issue>& issues)
{
    if (member.kind != vh::ExpressionKind::call
        || member.marker != support::ExpressionMarker::vhdl_member
        || member.operands.size() != 1U) {
        return;
    }
    const auto prefix_view
        = specialization.find_expression(member.operands.front());
    if (!prefix_view || prefix_view->vhdl == nullptr) {
        return;
    }
    const auto& prefix = *prefix_view->vhdl;
    if (prefix.kind != vh::ExpressionKind::name || !prefix.referenced_name
        || !prefix.referenced_name->analyzed
        || prefix.referenced_name->overloads.size() != 1U
        || prefix.referenced_name->selected_parts != 0U) {
        return;
    }
    const auto function = specialization.find_declaration(
        prefix.referenced_name->overloads.front());
    if (!function || function->vhdl == nullptr || !function->vhdl->callable
        || !function->vhdl->callable->function
        || !function->vhdl->callable->return_type) {
        return;
    }
    // Within the function itself `F.e` is an expanded name of a declaration
    // immediately within F (8.3), not decided here.
    if (function->vhdl->nested_scope
        && semantic::scope_within(specialization.design().semantics,
            member.scope, *function->vhdl->nested_scope)) {
        return;
    }
    const auto& result = *function->vhdl->callable->return_type;
    bool selects_element { };
    if ((result.written_type_mark
            && result.written_type_mark->standard
                != support::StandardDeclaration::none)
        || result.type_mark.builtin_declaration) {
        // A STANDARD type is never a record, an access or a protected type.
        selects_element = false;
    } else {
        const auto* base = result.type_mark.target.valid()
            ? semantic::vhdl_base_type_definition(
                specialization, result.type_mark.target)
            : nullptr;
        if (base == nullptr) {
            return;
        }
        switch (base->form) {
        case vh::TypeForm::record: {
            const auto element = support::vhdl_symbol(member.marker_argument);
            selects_element = std::ranges::any_of(base->record_elements,
                [&](const auto& candidate) {
                    return support::vhdl_symbol(candidate.name) == element;
                });
            break;
        }
        case vh::TypeForm::scalar:
        case vh::TypeForm::enumeration:
        case vh::TypeForm::array:
        case vh::TypeForm::physical:
            selects_element = false;
            break;
        default:
            return; // access, protected, file: selected through the value
        }
    }
    if (!selects_element) {
        issues.push_back({ "FSIM-ELAB-VHNAME-014",
            "expanded name '" + prefix.text + "." + member.marker_argument
                + "' does not denote a declaration of the construct named "
                  "by its prefix",
            member.source });
    }
}

/// An expanded name whose prefix denotes a process, block, generate, loop or
/// subprogram is only allowed within that construct (8.3).
void check_construct_expanded_name(
    const semantic::SpecializedHirUnit& specialization,
    const UnitItems& items, const std::vector<semantic::ScopeId>& constructs,
    const std::unordered_set<std::uint32_t>& excluded,
    const vh::Expression& expression, std::vector<Issue>& issues)
{
    if (expression.kind != vh::ExpressionKind::name
        || expression.marker != support::ExpressionMarker::none
        || excluded.contains(expression.id.value())) {
        return;
    }
    const auto& design = specialization.design();
    const auto& model = design.semantics;
    const auto spelling = expression.referenced_name
            && !expression.referenced_name->canonical.empty()
        ? std::string_view { expression.referenced_name->canonical }
        : std::string_view { expression.text };
    const auto parts = support::split_vhdl_selected_name(spelling);
    if (parts.size() != 2U) {
        return;
    }
    const auto report = [&] {
        issues.push_back({ "FSIM-ELAB-VHNAME-014",
            "expanded name '" + std::string { spelling }
                + "' does not denote a declaration of the construct named "
                  "by its prefix",
            expression.source });
    };
    const auto& name = expression.referenced_name;
    const auto prefix = support::vhdl_symbol(parts.front());
    // A block or generate label is the declaration of the statement
    // (DeclarationForm::generated): when the stored result is that label,
    // the prefix denotes the construct whose scope the label names.
    bool label_declared { };
    semantic::ScopeId label_declaration_scope;
    if (name && name->selected) {
        const auto label = specialization.find_declaration(*name->selected);
        if (label && label->vhdl != nullptr
            && label->vhdl->form == vh::DeclarationForm::generated
            && support::vhdl_symbol(label->vhdl->name) == prefix) {
            label_declared = true;
            label_declaration_scope = label->vhdl->scope;
        }
    }
    if (!label_declared && name && name->analyzed && name->selected
        && name->selected_parts == 0U) {
        // The suffix was selected: a declaration immediately within a
        // construct is named only within that construct.
        const auto declaration
            = specialization.find_declaration(*name->selected);
        if (declaration && declaration->vhdl != nullptr
            && std::ranges::find(constructs, declaration->vhdl->scope)
                != constructs.end()
            && !semantic::scope_within(
                model, expression.scope, declaration->vhdl->scope)) {
            report();
        }
        return;
    }
    if (!label_declared && name
        && (name->selected || !name->overloads.empty()
            || name->standard != support::StandardDeclaration::none)) {
        return;
    }
    // No stored result: the prefix may be the label of a construct of the
    // unit. It denotes that construct when the label is declared in a region
    // enclosing the name and no declaration of the unit in a region enclosing
    // the name hides it (12.3).
    const semantic::Scope* label_scope { };
    for (const auto id : constructs) {
        const auto* scope = design.find_scope(id);
        if (scope == nullptr || scope->role != semantic::ScopeRole::named
            || !scope->parent || scope->name.empty()
            || support::vhdl_symbol(scope->name) != prefix
            || (label_declared && *scope->parent != label_declaration_scope)) {
            continue;
        }
        if (label_scope != nullptr && label_scope->id != scope->id) {
            return; // ambiguous
        }
        label_scope = scope;
    }
    if (label_scope == nullptr
        || !semantic::scope_within(
            model, expression.scope, *label_scope->parent)
        || semantic::scope_within(model, expression.scope, label_scope->id)) {
        return;
    }
    const auto& hir = design.vhdl_hir;
    // The label's own declaration (form generated) is no other declaration
    // that hides it.
    const bool hidden = !label_declared && std::ranges::any_of(
        items.declarations, [&](const std::uint32_t position) {
            const auto& declaration = hir.declarations()[position];
            return declaration.form != vh::DeclarationForm::generated
                && support::vhdl_symbol(declaration.name) == prefix
                && semantic::scope_within(
                    model, expression.scope, declaration.scope);
        });
    if (!hidden) {
        report();
    }
}

void check_selected_names(const semantic::SpecializedHirUnit& specialization,
    const vh::Unit& entity, const vh::Unit& architecture,
    const UnitItems& items, std::vector<Issue>& issues)
{
    const auto& design = specialization.design();
    const auto& hir = design.vhdl_hir;
    // The actuals of generic maps are not analysis names (generics are
    // globally static, 9.4.3), and the path of an external name is a
    // hierarchy path, not an expanded name (8.7).
    std::unordered_set<std::uint32_t> excluded;
    for (const auto position : items.instances) {
        for (const auto& association : hir.instances()[position].generic_map) {
            if (association.expression) {
                excluded.insert(association.expression->value());
            }
        }
    }
    for (const auto position : items.expressions) {
        const auto& expression = hir.expressions()[position];
        if (expression.kind == vh::ExpressionKind::call
            && expression.marker == support::ExpressionMarker::vhdl_external
            && !expression.operands.empty()) {
            excluded.insert(expression.operands.front().value());
        }
    }
    const auto constructs
        = construct_scopes(design, items, entity, architecture);
    for (const auto position : items.expressions) {
        const auto& expression = hir.expressions()[position];
        if (!owned_by_unit(design, expression.scope, entity, architecture)) {
            continue;
        }
        check_function_result_selection(specialization, expression, issues);
        check_construct_expanded_name(
            specialization, items, constructs, excluded, expression, issues);
    }
}

} // namespace

std::vector<VhdlHirTypeValidationIssue> check_vhdl_physical_name_rules(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture)
{
    std::vector<Issue> issues;
    const auto items
        = unit_items(specialization.design(), entity, architecture);
    check_physical_types(specialization, entity, architecture, items, issues);
    check_selected_names(specialization, entity, architecture, items, issues);
    return issues;
}

} // namespace fsim::elaboration
