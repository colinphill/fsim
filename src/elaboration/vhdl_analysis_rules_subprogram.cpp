// SPDX-License-Identifier: Apache-2.0
#include "vhdl_analysis_rules.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fsim::elaboration {
namespace {

// The position of a locally static scalar value in its type (5.2.1): the
// typed position of a character literal or STD_ULOGIC-valued attribute,
// else the integral fold (an enumeration literal's position, an integer).
[[nodiscard]] std::optional<std::int64_t> vhdl_value_position(
    const semantic::SpecializedHirUnit& unit, const semantic::ExpressionId id)
{
    if (const auto typed = unit.evaluate_vhdl_position(id)) {
        return typed;
    }
    return unit.evaluate_integral_expression(id);
}

namespace vh = semantic::vhdl;
using Issue = VhdlHirTypeValidationIssue;
using UnitItems = semantic::CompiledVhdlUnitItems;

/// What a leaf expression of an actual or a sensitivity name is, decided
/// from the stored analysis only.
enum class LeafKind : std::uint8_t {
    unknown,
    object,
    literal,
    operation,
    call_result,
};

/// An object name with its selections peeled off (8.3, 8.4, 8.5).
struct Selection {
    const vh::Expression* leaf { };
    /// Index and slice operands applied to the leaf (8.4, 8.5).
    std::vector<semantic::ExpressionId> indices;
    /// A dereference or an unreadable prefix: nothing is decided.
    bool opaque { };
};

class SubprogramRules final {
public:
    SubprogramRules(const semantic::SpecializedHirUnit& specialization,
        const vh::Unit& entity, const vh::Unit& architecture)
        : spec_ { specialization }
        , entity_ { entity }
        , architecture_ { architecture }
    {
    }

    std::vector<Issue> run()
    {
        const auto& hir = spec_.design().vhdl_hir;
        const auto declarations = positions(
            &UnitItems::declarations, hir.declarations().size());
        const auto statements = positions(
            &UnitItems::statements, hir.statements().size());
        const auto expressions = positions(
            &UnitItems::expressions, hir.expressions().size());
        for (const auto position : declarations) {
            const auto& declaration = hir.declarations()[position];
            if (declaration.callable && declaration.nested_scope
                && selected_scope(declaration.scope)) {
                callable_scopes_.insert(declaration.nested_scope->value());
            }
        }
        for (const auto position : declarations) {
            check_duplicate_object(hir.declarations()[position]);
        }
        for (const auto position : statements) {
            const auto& statement = hir.statements()[position];
            if (!selected_scope(statement.scope)) {
                continue;
            }
            if (statement.kind == vh::StatementKind::wait_statement) {
                check_wait(statement);
            } else if (statement.kind == vh::StatementKind::procedure_call) {
                check_signal_formals(statement);
            }
        }
        for (const auto position : expressions) {
            const auto& expression = hir.expressions()[position];
            if (expression.kind == vh::ExpressionKind::slice
                && selected_scope(expression.scope)) {
                check_slice(expression);
            }
        }
        return std::move(issues_);
    }

private:
    const semantic::SpecializedHirUnit& spec_;
    const vh::Unit& entity_;
    const vh::Unit& architecture_;
    std::vector<Issue> issues_;
    std::unordered_set<std::uint32_t> callable_scopes_;
    std::unordered_map<std::uint32_t, std::unordered_set<std::string>> declared_;

    // The positions of the entity's and architecture's items, in HIR order;
    // every position when the lookup indexes are stale (callers still
    // check selected_scope).
    [[nodiscard]] std::vector<std::uint32_t> positions(
        std::vector<std::uint32_t> UnitItems::*member,
        const std::size_t size) const
    {
        const auto& design = spec_.design();
        const auto* entity = design.vhdl_unit_items(entity_.id);
        const auto* architecture = design.vhdl_unit_items(architecture_.id);
        std::vector<std::uint32_t> result;
        if (entity == nullptr || architecture == nullptr) {
            result.resize(size);
            std::iota(result.begin(), result.end(), 0U);
        } else if (entity_.id == architecture_.id) {
            result = entity->*member;
        } else {
            std::ranges::merge(entity->*member, architecture->*member,
                std::back_inserter(result));
        }
        return result;
    }

    [[nodiscard]] bool selected_scope(const semantic::ScopeId scope) const
    {
        const auto& scopes = spec_.design().semantics.scopes();
        return scope.valid() && scope.value() < scopes.size()
            && (scopes[scope.value()].unit == entity_.id
                || scopes[scope.value()].unit == architecture_.id);
    }

    [[nodiscard]] bool inside_callable(semantic::ScopeId scope) const
    {
        const auto& scopes = spec_.design().semantics.scopes();
        for (std::size_t depth { }; depth < 4096U; ++depth) {
            if (!scope.valid() || scope.value() >= scopes.size()) {
                return false;
            }
            if (callable_scopes_.contains(scope.value())) {
                return true;
            }
            const auto& parent = scopes[scope.value()].parent;
            if (!parent) {
                return false;
            }
            scope = *parent;
        }
        return false;
    }

    void report(std::string code, std::string message,
        const semantic::SourceSpanId source)
    {
        issues_.push_back(Issue { std::move(code), std::move(message), source });
    }

    [[nodiscard]] static std::optional<vh::ObjectClass> object_class(
        const vh::Declaration& declaration)
    {
        using Form = vh::DeclarationForm;
        switch (declaration.form) {
        case Form::signal: return vh::ObjectClass::signal;
        case Form::variable: return vh::ObjectClass::variable;
        case Form::constant:
        case Form::generic_constant:
        case Form::generate_parameter: return vh::ObjectClass::constant;
        case Form::file: return vh::ObjectClass::file;
        case Form::port: return declaration.object_class;
        default: return std::nullopt;
        }
    }

    // The declaration the stored analysis gave a name or an indexed object
    // name; null when it is not exactly one.
    [[nodiscard]] const vh::Declaration* denoted(
        const vh::Expression& expression) const
    {
        if ((expression.kind != vh::ExpressionKind::name
                && expression.kind != vh::ExpressionKind::call)
            || !expression.referenced_name) {
            return nullptr;
        }
        const auto& name = *expression.referenced_name;
        if (!name.analyzed || name.exemption != vh::AnalysisExemption::none
            || !name.selected || name.overloads.size() > 1U) {
            return nullptr;
        }
        const auto found = spec_.find_declaration(*name.selected);
        return found ? found->vhdl : nullptr;
    }

    // A `name(args)` call node whose name denotes an object is an indexed
    // or sliced name of it (8.4, 8.5), not a call.
    [[nodiscard]] const vh::Declaration* indexed_object(
        const vh::Expression& expression) const
    {
        if (expression.kind != vh::ExpressionKind::call
            || expression.marker != support::ExpressionMarker::none
            || !expression.referenced_name
            || expression.referenced_name->selected_parts != 0U) {
            return nullptr;
        }
        const auto* declaration = denoted(expression);
        return declaration != nullptr && object_class(*declaration)
            ? declaration
            : nullptr;
    }

    [[nodiscard]] Selection peel(const vh::Expression& start) const
    {
        Selection result;
        const vh::Expression* current = &start;
        for (std::size_t depth { }; depth < 256U; ++depth) {
            if (current->marker == support::ExpressionMarker::vhdl_dereference) {
                result.opaque = true;
                return result;
            }
            if (indexed_object(*current) != nullptr) {
                result.indices.insert(result.indices.end(),
                    current->operands.begin(), current->operands.end());
                result.leaf = current;
                return result;
            }
            const bool member = current->kind == vh::ExpressionKind::call
                && current->marker == support::ExpressionMarker::vhdl_member;
            const bool selects = current->kind == vh::ExpressionKind::index
                || current->kind == vh::ExpressionKind::slice || member;
            if (!selects) {
                result.leaf = current;
                return result;
            }
            if (current->operands.empty()) {
                result.opaque = true;
                return result;
            }
            if (!member) {
                result.indices.insert(result.indices.end(),
                    current->operands.begin() + 1, current->operands.end());
            }
            const auto next = spec_.find_expression(current->operands.front());
            if (!next || next->vhdl == nullptr) {
                result.opaque = true;
                return result;
            }
            current = next->vhdl;
        }
        result.opaque = true;
        return result;
    }

    [[nodiscard]] LeafKind classify(const vh::Expression& e) const
    {
        using Kind = vh::ExpressionKind;
        switch (e.kind) {
        case Kind::integer_literal:
        case Kind::real_literal:
        case Kind::boolean_literal:
        case Kind::logic_literal:
        case Kind::string_literal: return LeafKind::literal;
        case Kind::unary:
        case Kind::binary: return LeafKind::operation;
        case Kind::name:
        case Kind::call: break;
        default: return LeafKind::unknown;
        }
        if (e.predefined_attribute
            || e.interpretation == vh::Interpretation::attribute
            || e.marker != support::ExpressionMarker::none) {
            return LeafKind::unknown;
        }
        if (const auto* declaration = denoted(e)) {
            using Form = vh::DeclarationForm;
            if (object_class(*declaration)) {
                return LeafKind::object;
            }
            if (e.kind == Kind::name
                && declaration->form == Form::enumeration_literal) {
                return LeafKind::literal;
            }
            if (declaration->callable && declaration->callable->function) {
                return LeafKind::call_result;
            }
            if (e.kind == Kind::call
                && (declaration->form == Form::type
                    || declaration->form == Form::subtype)) {
                return LeafKind::call_result;
            }
            return LeafKind::unknown;
        }
        // A type conversion to a predefined type (9.3.6): the name denotes
        // a STANDARD type, which has no declaration of its own.
        if (e.kind == Kind::call && e.referenced_name
            && e.referenced_name->analyzed
            && e.referenced_name->standard
                != support::StandardDeclaration::none) {
            return LeafKind::call_result;
        }
        if (e.kind == Kind::call
            && (e.interpretation == vh::Interpretation::conversion
                || e.interpretation == vh::Interpretation::function_call)) {
            return LeafKind::call_result;
        }
        return LeafKind::unknown;
    }

    // Whether an index expression of a static name reads an object whose
    // value changes at run time (8.1, 9.4: a static expression has none).
    [[nodiscard]] bool reads_dynamic_object(
        const semantic::ExpressionId id, const std::size_t depth = 0U) const
    {
        if (depth > 64U) {
            return false;
        }
        const auto found = spec_.find_expression(id);
        if (!found || found->vhdl == nullptr) {
            return false;
        }
        const auto& e = *found->vhdl;
        if (e.predefined_attribute
            || e.interpretation == vh::Interpretation::attribute
            || e.marker != support::ExpressionMarker::none) {
            return false;
        }
        if (e.kind == vh::ExpressionKind::name
            || e.kind == vh::ExpressionKind::call) {
            if (const auto* declaration = denoted(e)) {
                if (const auto cls = object_class(*declaration)) {
                    if (*cls == vh::ObjectClass::variable
                        || *cls == vh::ObjectClass::signal
                        || *cls == vh::ObjectClass::file) {
                        return true;
                    }
                }
            }
        } else if (e.kind != vh::ExpressionKind::unary
            && e.kind != vh::ExpressionKind::binary
            && e.kind != vh::ExpressionKind::index
            && e.kind != vh::ExpressionKind::slice) {
            return false;
        }
        return std::ranges::any_of(
            e.operands, [&](const semantic::ExpressionId operand) {
                return reads_dynamic_object(operand, depth + 1U);
            });
    }

    // 4.2.2.3: the actual associated with a signal formal is a static
    // signal name. True when the actual positively is not one.
    [[nodiscard]] bool not_static_signal_name(
        const semantic::ExpressionId id) const
    {
        const auto found = spec_.find_expression(id);
        if (!found || found->vhdl == nullptr) {
            return false;
        }
        const auto selection = peel(*found->vhdl);
        if (selection.opaque || selection.leaf == nullptr) {
            return false;
        }
        switch (classify(*selection.leaf)) {
        case LeafKind::literal:
        case LeafKind::operation:
        case LeafKind::call_result: return true;
        case LeafKind::object: break;
        case LeafKind::unknown: return false;
        }
        const auto* declaration = denoted(*selection.leaf);
        const auto cls = declaration != nullptr ? object_class(*declaration)
                                                : std::nullopt;
        if (!cls) {
            return false;
        }
        if (*cls != vh::ObjectClass::signal) {
            return true;
        }
        return std::ranges::any_of(selection.indices,
            [&](const semantic::ExpressionId index) {
                return reads_dynamic_object(index);
            });
    }

    // IEEE 1076-2019 4.2.2.1, 4.2.2.3 (1076-1993 2.1.1.2): a signal formal
    // is associated with a static signal name; no conversion appears.
    // (The class-variable rule, FSIM-ELAB-VHPROC-018, is the Validator's.)
    void check_signal_formals(const vh::Statement& statement)
    {
        if (!statement.resolved_procedure
            || statement.resolved_formals.size()
                != statement.procedure_arguments.size()) {
            return;
        }
        const auto callee
            = spec_.find_declaration(*statement.resolved_procedure);
        if (!callee || callee->vhdl == nullptr || !callee->vhdl->callable) {
            return;
        }
        const auto& formals = callee->vhdl->callable->formals;
        std::vector<std::size_t> argument_of(formals.size(), formals.size());
        std::vector<std::size_t> uses(formals.size());
        for (std::size_t index { }; index < statement.resolved_formals.size();
             ++index) {
            const auto position = statement.resolved_formals[index];
            if (position == vh::no_formal || position >= formals.size()) {
                continue;
            }
            ++uses[position];
            argument_of[position] = index;
        }
        for (std::size_t position { }; position < formals.size(); ++position) {
            if (uses[position] != 1U) {
                continue;
            }
            const auto formal = spec_.find_declaration(formals[position]);
            if (!formal || formal->vhdl == nullptr
                || formal->vhdl->object_class != vh::ObjectClass::signal) {
                continue;
            }
            const auto& actual
                = statement.procedure_arguments[argument_of[position]];
            if (formal->vhdl->initializer
                && actual.actual == *formal->vhdl->initializer) {
                continue;
            }
            if (!not_static_signal_name(actual.actual)) {
                continue;
            }
            const auto written = spec_.find_expression(actual.actual);
            report("FSIM-ELAB-VHPROC-022",
                "signal-class procedure formal '" + formal->vhdl->name
                    + "' requires a whole signal name actual",
                written && written->vhdl != nullptr ? written->vhdl->source
                                                    : statement.source);
            return;
        }
    }

    // IEEE 1076-2019 10.2: each name of a wait statement's sensitivity list
    // is a static signal name.
    void check_wait(const vh::Statement& statement)
    {
        for (const auto& entry : statement.sensitivities) {
            if (!entry.expression) {
                continue;
            }
            const auto found = spec_.find_expression(*entry.expression);
            if (!found || found->vhdl == nullptr) {
                continue;
            }
            const auto selection = peel(*found->vhdl);
            if (selection.opaque || selection.leaf == nullptr
                || classify(*selection.leaf) != LeafKind::object) {
                continue;
            }
            const auto* declaration = denoted(*selection.leaf);
            const auto cls = declaration != nullptr ? object_class(*declaration)
                                                    : std::nullopt;
            if (!cls || *cls == vh::ObjectClass::signal) {
                continue;
            }
            report("FSIM-ELAB-059",
                "unknown wait signal '"
                    + (entry.signal.empty() ? selection.leaf->text
                                            : entry.signal)
                    + "'",
                entry.source);
        }
    }

    // IEEE 1076-2019 12.3: no two homographs in one declarative region;
    // objects are not overloadable.
    void check_duplicate_object(const vh::Declaration& declaration)
    {
        using Form = vh::DeclarationForm;
        if ((declaration.form != Form::variable
                && declaration.form != Form::constant
                && declaration.form != Form::file)
            || declaration.deferred || !selected_scope(declaration.scope)) {
            return;
        }
        const auto& scopes = spec_.design().semantics.scopes();
        if (scopes[declaration.scope.value()].role
            == semantic::ScopeRole::loop_parameters) {
            return;
        }
        auto& names = declared_[declaration.scope.value()];
        if (!names.insert(declaration.name).second) {
            report("FSIM-ELAB-053",
                "duplicate local variable in the same scope '"
                    + declaration.name + "'",
                declaration.source);
        }
    }

    // The one index constraint a one-dimensional array subtype declares,
    // through its subtype chain (5.3.2); null when it is not constrained
    // by declaration.
    [[nodiscard]] const vh::RangeConstraint* index_constraint(
        const vh::SubtypeIndication& indication,
        const std::size_t depth = 0U) const
    {
        if (depth > 64U) {
            return nullptr;
        }
        if (!indication.constraints.empty()) {
            return indication.constraints.size() == 1U
                    && indication.element_constraints.empty()
                ? &indication.constraints.front()
                : nullptr;
        }
        if (!indication.type_mark.target.valid()) {
            return nullptr;
        }
        const auto found = spec_.find_type(indication.type_mark.target);
        if (!found || found->vhdl == nullptr) {
            return nullptr;
        }
        const auto& definition = *found->vhdl;
        if (definition.form == vh::TypeForm::subtype
            || definition.form == vh::TypeForm::alias) {
            return index_constraint(definition.base, depth + 1U);
        }
        if (definition.form == vh::TypeForm::array
            && definition.array_dimensions.size() == 1U
            && !definition.array_dimensions.front().unconstrained
            && definition.array_dimensions.front().constraint) {
            return &*definition.array_dimensions.front().constraint;
        }
        return nullptr;
    }

    // A bound known from the unit alone: a locally static expression (9.4.2)
    // or a literal value; never a generic's value.
    [[nodiscard]] std::optional<std::int64_t> static_bound(
        const std::optional<std::int64_t> value,
        const std::optional<semantic::ExpressionId> expression) const
    {
        if (expression) {
            if (!vhdl_locally_static_expression(spec_, *expression)) {
                return std::nullopt;
            }
            return vhdl_value_position(spec_, *expression);
        }
        return value;
    }

    // IEEE 1076-2019 8.5: the direction of a slice's discrete range is that
    // of the prefix's index range, and its bounds belong to it unless the
    // slice is null. Decided only for a prefix whose subtype is constrained
    // by declaration with locally static bounds; a slice in a subprogram
    // body is evaluated per call, so execution decides it.
    void check_slice(const vh::Expression& slice)
    {
        if (slice.operands.size() != 3U
            || vh::written_range_direction(slice) == vh::WrittenRangeDirection::none
            || inside_callable(slice.scope)) {
            return;
        }
        const auto prefix = spec_.find_expression(slice.operands.front());
        if (!prefix || prefix->vhdl == nullptr
            || prefix->vhdl->kind != vh::ExpressionKind::name
            || !prefix->vhdl->referenced_name
            || prefix->vhdl->referenced_name->selected_parts != 0U) {
            return;
        }
        const auto* declaration = denoted(*prefix->vhdl);
        if (declaration == nullptr || declaration->deferred) {
            return;
        }
        const vh::SubtypeIndication* indication { };
        if (declaration->callable && declaration->callable->function) {
            if (declaration->callable->return_type) {
                indication = &*declaration->callable->return_type;
            }
        } else if (object_class(*declaration) && declaration->subtype) {
            indication = &*declaration->subtype;
        }
        if (indication == nullptr) {
            return;
        }
        const auto* base = semantic::vhdl_base_type_definition(
            spec_, indication->type_mark.target);
        if (base == nullptr || base->form != vh::TypeForm::array
            || base->array_dimensions.size() != 1U) {
            return;
        }
        const auto* range = index_constraint(*indication);
        if (range == nullptr || range->null || range->direction_from_bounds) {
            return;
        }
        const bool descending = vh::written_range_direction(slice)
            == vh::WrittenRangeDirection::downto;
        const auto message = std::string {
            "a VHDL slice requires constant in-range bounds with a direction "
            "compatible with the selected array"
        };
        if (descending != range->descending) {
            report("FSIM-ELAB-068", message, slice.source);
            return;
        }
        if (!vhdl_locally_static_expression(spec_, slice.operands[1])
            || !vhdl_locally_static_expression(spec_, slice.operands[2])) {
            return;
        }
        const auto left = vhdl_value_position(spec_, slice.operands[1]);
        const auto right = vhdl_value_position(spec_, slice.operands[2]);
        const auto first = static_bound(range->left, range->left_expression);
        const auto last = static_bound(range->right, range->right_expression);
        if (!left || !right || !first || !last) {
            return;
        }
        const bool null_slice = descending ? *left < *right : *left > *right;
        if (null_slice) {
            return;
        }
        const auto low = range->descending ? *last : *first;
        const auto high = range->descending ? *first : *last;
        if (*left < low || *left > high || *right < low || *right > high) {
            report("FSIM-ELAB-068", message, slice.source);
        }
    }
};

} // namespace

std::vector<VhdlHirTypeValidationIssue> check_vhdl_subprogram_rules(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture)
{
    return SubprogramRules { specialization, entity, architecture }.run();
}

} // namespace fsim::elaboration
