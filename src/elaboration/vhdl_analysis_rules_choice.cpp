// SPDX-License-Identifier: Apache-2.0
//
// Compile-time choice rules (gate BD D7, IEEE 1076-2019 10.9, 9.3.3.1, 9.2.8,
// 10.5, 10.6). Every rule decides from the unit as analyzed: a value is used
// only when its expression is locally static (9.4.2), and a selector subtype
// only when it is the subtype of a declared object whose bounds are locally
// static. A generic's value, an unconstrained interface object's bounds and
// anything that cannot be evaluated give no report.
#include "vhdl_analysis_rules.hpp"

#include "fsim/support/expression_marker.hpp"
#include "fsim/support/symbol.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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

namespace hir = semantic::vhdl;

struct Interval {
    std::int64_t lower { };
    std::int64_t upper { };
};

class ChoiceRules final {
public:
    ChoiceRules(const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture)
        : specialization_ { specialization }
        , entity_ { entity }
        , architecture_id_ { architecture.id }
    {
    }

    std::vector<VhdlHirTypeValidationIssue> run()
    {
        const auto& table = specialization_.design().vhdl_hir;
        for (const auto position : unit_positions(
                 &semantic::CompiledVhdlUnitItems::statements,
                 table.statements().size())) {
            const auto effective
                = specialization_.find_statement(table.statements()[position].id);
            if (!effective || effective->vhdl == nullptr) {
                continue;
            }
            const auto& statement = *effective->vhdl;
            if (statement.kind == hir::StatementKind::selection) {
                check_case(statement);
            } else if (statement.kind
                    == hir::StatementKind::variable_assignment
                || statement.kind == hir::StatementKind::signal_assignment) {
                check_array_assignment(statement);
            }
        }
        for (const auto position : unit_positions(
                 &semantic::CompiledVhdlUnitItems::expressions,
                 table.expressions().size())) {
            const auto effective = specialization_.find_expression(
                table.expressions()[position].id);
            if (!effective || effective->vhdl == nullptr) {
                continue;
            }
            const auto& expression = *effective->vhdl;
            if (expression.kind == hir::ExpressionKind::aggregate) {
                check_record_aggregate(expression);
            } else if (expression.predefined_operation
                    == hir::PredefinedOperation::power
                || expression.resolved_standard
                    == support::StandardDeclaration::op_pow_integer_integer
                || expression.resolved_standard
                    == support::StandardDeclaration::
                        op_pow_universal_integer_integer) {
                check_integer_power(expression);
            }
        }
        return std::move(issues_);
    }

private:
    const semantic::SpecializedHirUnit& specialization_;
    const semantic::vhdl::Unit& entity_;
    semantic::UnitId architecture_id_;
    std::vector<VhdlHirTypeValidationIssue> issues_;

    void report(std::string code, std::string message,
        const semantic::SourceSpanId source)
    {
        issues_.push_back({ std::move(code), std::move(message), source });
    }

    // The positions of the entity's and the architecture's items in the
    // design-wide HIR tables (the Validator's selection).
    [[nodiscard]] std::vector<std::uint32_t> unit_positions(
        const std::vector<std::uint32_t> semantic::CompiledVhdlUnitItems::*
            member,
        const std::size_t size) const
    {
        const auto& design = specialization_.design();
        const auto* entity_items = design.vhdl_unit_items(entity_.id);
        const auto* architecture_items
            = design.vhdl_unit_items(architecture_id_);
        std::vector<std::uint32_t> positions;
        if (entity_items == nullptr || architecture_items == nullptr) {
            positions.resize(size);
            std::iota(positions.begin(), positions.end(), 0U);
        } else if (entity_.id == architecture_id_) {
            positions = entity_items->*member;
        } else {
            std::ranges::merge(entity_items->*member,
                architecture_items->*member, std::back_inserter(positions));
        }
        return positions;
    }

    // ---- bounds (5.2.1, 9.4.2) -----------------------------------------

    // The position of a bound: its value when the written bound expression
    // is locally static, else the literal the front end stored (no
    // expression); nothing for anything else.
    [[nodiscard]] std::optional<std::int64_t> bound_position(
        const std::optional<std::int64_t> literal,
        const std::optional<semantic::ExpressionId> expression) const
    {
        if (expression) {
            if (!vhdl_locally_static_expression(specialization_, *expression)) {
                return std::nullopt;
            }
            return vhdl_value_position(specialization_, *expression);
        }
        return literal;
    }

    [[nodiscard]] std::optional<Interval> constraint_interval(
        const hir::RangeConstraint& constraint) const
    {
        if (constraint.null || constraint.direction_from_bounds) {
            return std::nullopt;
        }
        const auto left
            = bound_position(constraint.left, constraint.left_expression);
        const auto right
            = bound_position(constraint.right, constraint.right_expression);
        if (!left || !right) {
            return std::nullopt;
        }
        return Interval { std::min(*left, *right), std::max(*left, *right) };
    }

    // ---- 10.9 case statement ---------------------------------------------

    // The values every conforming implementation gives a predefined scalar
    // type (5.2.3.1, 16.3): INTEGER may be larger, so the range stated here
    // is the part that every INTEGER has.
    [[nodiscard]] static std::optional<Interval> standard_interval(
        const support::StandardDeclaration declaration)
    {
        using support::StandardDeclaration;
        constexpr std::int64_t integer_high = 2147483647;
        switch (declaration) {
        case StandardDeclaration::boolean:
        case StandardDeclaration::bit:
            return Interval { 0, 1 };
        case StandardDeclaration::character:
            return Interval { 0, 255 };
        case StandardDeclaration::severity_level:
            return Interval { 0, 3 };
        case StandardDeclaration::integer:
            return Interval { -integer_high, integer_high };
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] static std::optional<support::StandardDeclaration>
    provided_standard(const hir::ProvidedType provided)
    {
        using support::StandardDeclaration;
        switch (provided) {
        case hir::ProvidedType::boolean: return StandardDeclaration::boolean;
        case hir::ProvidedType::bit: return StandardDeclaration::bit;
        case hir::ProvidedType::character:
            return StandardDeclaration::character;
        case hir::ProvidedType::severity_level:
            return StandardDeclaration::severity_level;
        case hir::ProvidedType::integer:
        case hir::ProvidedType::natural:
        case hir::ProvidedType::positive:
            return StandardDeclaration::integer;
        default: return std::nullopt;
        }
    }

    // The values of the subtype `subtype` of a scalar object whose base type
    // is `base` (the expression's TypeIdentity), following subtype
    // declarations; nothing when a bound is not locally static, the subtype
    // is not scalar, or its base type is not `base`.
    [[nodiscard]] std::optional<Interval> subtype_interval(
        hir::SubtypeIndication subtype, const hir::TypeIdentity& base) const
    {
        for (std::size_t depth { }; depth < 64U; ++depth) {
            if (!subtype.constraints.empty()) {
                if (subtype.constraints.size() != 1U
                    || subtype.constraints.front().kind
                        == hir::RangeKind::array_index
                    || !base_matches(subtype, base)) {
                    return std::nullopt;
                }
                return constraint_interval(subtype.constraints.front());
            }
            if (const auto standard = provided_standard(subtype.provided_type)) {
                if (base.kind != hir::TypeIdentity::Kind::standard
                    || base.standard != *standard) {
                    return std::nullopt;
                }
                auto interval = standard_interval(*standard);
                if (interval
                    && subtype.provided_type == hir::ProvidedType::natural) {
                    interval->lower = 0;
                } else if (interval
                    && subtype.provided_type == hir::ProvidedType::positive) {
                    interval->lower = 1;
                }
                return interval;
            }
            if (!subtype.type_mark.target.valid()) {
                return std::nullopt;
            }
            const auto type = specialization_.find_type(
                subtype.type_mark.target);
            if (!type || type->vhdl == nullptr) {
                return std::nullopt;
            }
            const auto& definition = *type->vhdl;
            switch (definition.form) {
            case hir::TypeForm::subtype:
            case hir::TypeForm::alias:
                subtype = definition.base;
                continue;
            case hir::TypeForm::enumeration:
                if (base.kind != hir::TypeIdentity::Kind::declared
                    || base.declared != definition.id
                    || definition.enumeration_literals.empty()) {
                    return std::nullopt;
                }
                return Interval { 0,
                    static_cast<std::int64_t>(
                        definition.enumeration_literals.size()) - 1 };
            case hir::TypeForm::scalar:
                if (definition.floating_type || !definition.scalar_range
                    || base.kind != hir::TypeIdentity::Kind::declared
                    || base.declared != definition.id) {
                    return std::nullopt;
                }
                return constraint_interval(*definition.scalar_range);
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // Whether the type mark chain of `subtype` ends in the scalar type
    // `base`.
    [[nodiscard]] bool base_matches(hir::SubtypeIndication subtype,
        const hir::TypeIdentity& base) const
    {
        for (std::size_t depth { }; depth < 64U; ++depth) {
            if (const auto standard = provided_standard(subtype.provided_type)) {
                return base.kind == hir::TypeIdentity::Kind::standard
                    && base.standard == *standard;
            }
            if (!subtype.type_mark.target.valid()) {
                return false;
            }
            const auto type = specialization_.find_type(
                subtype.type_mark.target);
            if (!type || type->vhdl == nullptr) {
                return false;
            }
            const auto& definition = *type->vhdl;
            if (definition.form == hir::TypeForm::subtype
                || definition.form == hir::TypeForm::alias) {
                subtype = definition.base;
                continue;
            }
            return base.kind == hir::TypeIdentity::Kind::declared
                && base.declared == definition.id
                && (definition.form == hir::TypeForm::enumeration
                    || (definition.form == hir::TypeForm::scalar
                        && !definition.floating_type));
        }
        return false;
    }

    // The declaration an expression names as a whole object: a signal,
    // variable, constant or port (4.2, 6.4).
    [[nodiscard]] const hir::Declaration* object_declaration(
        const hir::Expression& expression) const
    {
        if (expression.kind != hir::ExpressionKind::name
            || expression.interpretation != hir::Interpretation::object
            || !expression.operands.empty() || !expression.referenced_name
            || !expression.referenced_name->selected
            || expression.referenced_name->selected_parts != 0U) {
            return nullptr;
        }
        const auto declaration = specialization_.find_declaration(
            *expression.referenced_name->selected);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            return nullptr;
        }
        switch (declaration->vhdl->form) {
        case hir::DeclarationForm::signal:
        case hir::DeclarationForm::variable:
        case hir::DeclarationForm::constant:
        case hir::DeclarationForm::port:
            return declaration->vhdl;
        default:
            return nullptr;
        }
    }

    // Whether `type` is an enumeration or integer type (a discrete type of
    // 5.2) whose values choice_interval can order.
    [[nodiscard]] bool discrete_scalar(const hir::TypeIdentity& type) const
    {
        if (type.kind == hir::TypeIdentity::Kind::standard) {
            return standard_interval(type.standard).has_value();
        }
        if (type.kind == hir::TypeIdentity::Kind::declared) {
            const auto found = specialization_.find_type(type.declared);
            return found && found->vhdl != nullptr
                && (found->vhdl->form == hir::TypeForm::enumeration
                    || (found->vhdl->form == hir::TypeForm::scalar
                        && !found->vhdl->floating_type));
        }
        return false;
    }

    [[nodiscard]] bool integer_class(const hir::TypeIdentity& type) const
    {
        if (type.kind == hir::TypeIdentity::Kind::standard) {
            return type.standard == support::StandardDeclaration::integer;
        }
        if (type.kind == hir::TypeIdentity::Kind::declared) {
            const auto found = specialization_.find_type(type.declared);
            // `type T is range L to R` is kept as a subtype of its
            // predefined base marked scalar_type (5.2.3.1).
            return found && found->vhdl != nullptr
                && (found->vhdl->scalar_type
                        || found->vhdl->form == hir::TypeForm::scalar)
                && !found->vhdl->floating_type;
        }
        return false;
    }

    // The values of one choice (10.9): nothing when it is not a locally
    // static value or range of the selector's type; those are other rules'
    // errors. A null range stands for no value (lower > upper).
    [[nodiscard]] std::optional<Interval> choice_interval(
        const semantic::ExpressionId choice,
        const hir::TypeIdentity& selector_type) const
    {
        const auto found = specialization_.find_expression(choice);
        if (!found || found->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto& expression = *found->vhdl;
        const auto position_of = [&](const semantic::ExpressionId id)
            -> std::optional<std::int64_t> {
            const auto operand = specialization_.find_expression(id);
            if (!operand || operand->vhdl == nullptr
                || !vhdl_locally_static_expression(specialization_, id)) {
                return std::nullopt;
            }
            const auto& type = operand->vhdl->type;
            if (!(type == selector_type
                    || (type.kind == hir::TypeIdentity::Kind::universal_integer
                        && integer_class(selector_type)))) {
                return std::nullopt;
            }
            return vhdl_value_position(specialization_, id);
        };
        const bool range = expression.operands.size() == 2U
            && ((expression.kind == hir::ExpressionKind::binary
                    && hir::written_range_direction(expression)
                        != hir::WrittenRangeDirection::none)
                || (expression.kind == hir::ExpressionKind::call
                    && support::vhdl_case_range(expression.marker)));
        if (range) {
            const auto left = position_of(expression.operands[0]);
            const auto right = position_of(expression.operands[1]);
            if (!left || !right) {
                return std::nullopt;
            }
            const bool descending = hir::written_range_direction(expression)
                    == hir::WrittenRangeDirection::downto
                || expression.marker
                    == support::ExpressionMarker::vhdl_case_range_downto;
            if (descending ? *left < *right : *left > *right) {
                return Interval { 1, 0 };
            }
            return Interval { std::min(*left, *right),
                std::max(*left, *right) };
        }
        if (expression.interpretation == hir::Interpretation::type_mark) {
            return std::nullopt;
        }
        const auto value = position_of(choice);
        if (!value) {
            return std::nullopt;
        }
        return Interval { *value, *value };
    }

    // 10.9: for an ordinary case statement whose expression names an object
    // of a locally static scalar subtype, each value of the subtype is
    // represented once and only once in the choices.
    void check_case(const hir::Statement& statement)
    {
        if (statement.matching_case || !statement.condition) {
            return;
        }
        const auto selector = specialization_.find_expression(
            *statement.condition);
        if (!selector || selector->vhdl == nullptr
            || !discrete_scalar(selector->vhdl->type)) {
            return;
        }
        const auto& selector_type = selector->vhdl->type;
        std::vector<std::pair<Interval, semantic::SourceSpanId>> intervals;
        bool has_others { };
        for (const auto& alternative : statement.alternatives) {
            if (alternative.is_default) {
                has_others = true;
                continue;
            }
            for (const auto choice : alternative.choices) {
                const auto interval = choice_interval(choice, selector_type);
                if (!interval) {
                    // Not a locally static value of the selector's type:
                    // 10.9 is decided by other rules.
                    return;
                }
                intervals.emplace_back(*interval,
                    specialization_.find_expression(choice)->vhdl->source);
            }
        }
        bool valid = true;
        std::vector<Interval> seen;
        for (const auto& [interval, source] : intervals) {
            if (interval.lower > interval.upper) {
                continue;
            }
            const auto overlap = std::ranges::find_if(
                seen, [&](const Interval& prior) {
                    return interval.lower <= prior.upper
                        && prior.lower <= interval.upper;
                });
            if (overlap != seen.end()) {
                const bool duplicate = overlap->lower == interval.lower
                    && overlap->upper == interval.upper;
                report(duplicate ? "FSIM-ELAB-VHDLCASE-003"
                                 : "FSIM-ELAB-VHDLCASE-004",
                    duplicate ? "a VHDL case statement repeats the same "
                                "discrete choice"
                              : "VHDL case choices or ranges overlap",
                    source);
                valid = false;
            } else {
                seen.push_back(interval);
            }
        }
        if (has_others || !valid) {
            return;
        }
        // Without OTHERS the choices cover every value of the selector's
        // subtype; that subtype is known only for the name of an object.
        const auto* object = object_declaration(*selector->vhdl);
        if (object == nullptr) {
            return;
        }
        const auto domain = subtype_interval(*object->subtype, selector_type);
        if (!domain) {
            return;
        }
        std::ranges::sort(seen, { }, &Interval::lower);
        auto expected = domain->lower;
        bool complete = domain->lower > domain->upper;
        for (const auto& interval : seen) {
            if (interval.upper < expected) {
                continue;
            }
            if (interval.lower > expected) {
                break;
            }
            if (interval.upper >= domain->upper) {
                complete = true;
                break;
            }
            expected = interval.upper + 1;
        }
        if (!complete) {
            report("FSIM-ELAB-VHDLCASE-005",
                "a VHDL case statement does not cover its complete selector "
                "subtype and requires an others alternative",
                statement.source);
        }
    }

    // ---- 10.5, 10.6 array assignment ---------------------------------------

    [[nodiscard]] std::optional<std::int64_t> constraint_length(
        const hir::RangeConstraint& constraint) const
    {
        if (constraint.null) {
            return std::int64_t { 0 };
        }
        const auto left
            = bound_position(constraint.left, constraint.left_expression);
        const auto right
            = bound_position(constraint.right, constraint.right_expression);
        if (!left || !right) {
            return std::nullopt;
        }
        const auto length = constraint.descending ? *left - *right + 1
                                                  : *right - *left + 1;
        return std::max<std::int64_t>(length, 0);
    }

    // The number of elements of the one-dimensional constrained array
    // subtype `subtype` of the array type `base` when every bound is locally
    // static (5.3.2.2); nothing otherwise.
    [[nodiscard]] std::optional<std::int64_t> array_length(
        hir::SubtypeIndication subtype, const hir::TypeIdentity& base) const
    {
        std::optional<std::int64_t> length;
        for (std::size_t depth { }; depth < 64U; ++depth) {
            if (!subtype.constraints.empty() && !length) {
                if (subtype.constraints.size() != 1U
                    || subtype.constraints.front().kind
                        != hir::RangeKind::array_index
                    || subtype.constraints.front().direction_from_bounds) {
                    return std::nullopt;
                }
                length = constraint_length(subtype.constraints.front());
                if (!length) {
                    return std::nullopt;
                }
            }
            if (!subtype.type_mark.target.valid()) {
                return std::nullopt;
            }
            const auto type = specialization_.find_type(
                subtype.type_mark.target);
            if (!type || type->vhdl == nullptr) {
                return std::nullopt;
            }
            const auto& definition = *type->vhdl;
            if (definition.form == hir::TypeForm::subtype
                || definition.form == hir::TypeForm::alias) {
                subtype = definition.base;
                continue;
            }
            if (definition.form != hir::TypeForm::array
                || definition.id != base.declared
                || definition.array_dimensions.size() != 1U) {
                return std::nullopt;
            }
            const auto& dimension = definition.array_dimensions.front();
            if (!length && dimension.constraint && !dimension.unconstrained
                && !dimension.constraint->direction_from_bounds) {
                length = constraint_length(*dimension.constraint);
            }
            return length;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::int64_t> object_array_length(
        const semantic::ExpressionId id, hir::TypeIdentity& type) const
    {
        const auto found = specialization_.find_expression(id);
        if (!found || found->vhdl == nullptr
            || found->vhdl->type.kind != hir::TypeIdentity::Kind::declared) {
            return std::nullopt;
        }
        const auto* object = object_declaration(*found->vhdl);
        if (object == nullptr) {
            return std::nullopt;
        }
        type = found->vhdl->type;
        return array_length(*object->subtype, type);
    }

    // 10.5.2.1, 10.6.2.1: the target and the value of an assignment between
    // two objects of one array type have the same number of elements; when
    // both subtypes are locally static the lengths are known at analysis.
    void check_array_assignment(const hir::Statement& statement)
    {
        if (!statement.target) {
            return;
        }
        std::optional<semantic::ExpressionId> value;
        if (statement.kind == hir::StatementKind::variable_assignment) {
            value = statement.value;
        } else if (statement.waveform.size() == 1U
            && !statement.waveform.front().disconnect
            && !statement.guarded_assignment && !statement.unaffected) {
            value = statement.waveform.front().value;
        }
        if (!value) {
            return;
        }
        hir::TypeIdentity target_type;
        hir::TypeIdentity value_type;
        const auto target_length
            = object_array_length(*statement.target, target_type);
        const auto value_length = object_array_length(*value, value_type);
        if (target_length && value_length && target_type == value_type
            && *target_length != *value_length) {
            report("FSIM-ELAB-VHARRAY-006",
                "VHDL array assignment requires compatible nominal type and "
                "bounds",
                statement.source);
        }
    }

    // ---- 9.3.3.1 record aggregate ----------------------------------------

    // The element simple names of a record aggregate association (9.3.3.1).
    [[nodiscard]] static std::vector<std::string> association_names(
        const hir::AggregateAssociation& association)
    {
        std::vector<std::string> names;
        auto spelling = std::string_view { association.choice_spelling };
        while (!spelling.empty()) {
            const auto separator = spelling.find('|');
            auto name = spelling.substr(0U, separator);
            while (!name.empty()
                && (name.front() == ' ' || name.front() == '\t')) {
                name.remove_prefix(1U);
            }
            while (!name.empty()
                && (name.back() == ' ' || name.back() == '\t')) {
                name.remove_suffix(1U);
            }
            if (!name.empty()) {
                names.emplace_back(name);
            }
            if (separator == std::string_view::npos) {
                break;
            }
            spelling.remove_prefix(separator + 1U);
        }
        return names;
    }

    // 9.3.3.1: each element simple name of a record aggregate is the name of
    // an element of the record type the aggregate has.
    void check_record_aggregate(const hir::Expression& aggregate)
    {
        if (aggregate.type.kind != hir::TypeIdentity::Kind::declared) {
            return;
        }
        const auto type = specialization_.find_type(aggregate.type.declared);
        if (!type || type->vhdl == nullptr
            || type->vhdl->form != hir::TypeForm::record) {
            return;
        }
        for (const auto& association : aggregate.associations) {
            if (association.positional()
                || association.choice_marker != support::ExpressionMarker::none
                || association.choice_spelling.empty()) {
                continue;
            }
            for (const auto& name : association_names(association)) {
                const auto symbol = support::vhdl_symbol(name);
                const bool found = std::ranges::any_of(
                    type->vhdl->record_elements,
                    [&](const hir::RecordElement& element) {
                        return support::vhdl_symbol(element.name) == symbol;
                    });
                if (!found) {
                    report("FSIM-ELAB-VHAGG-003",
                        "record aggregate type '" + type->vhdl->name
                            + "' has no element '" + name + "'",
                        association.source);
                }
            }
        }
    }

    // ---- 9.2.8 exponentiation ------------------------------------------------

    // 9.2.8: the exponent of an integer power is not negative; a locally
    // static negative exponent is known at analysis.
    void check_integer_power(const hir::Expression& power)
    {
        // 9.2.8: an integer type, universal_integer included.
        if (power.operands.size() != 2U
            || (!integer_class(power.type)
                && power.type.kind
                    != hir::TypeIdentity::Kind::universal_integer)
            || !vhdl_locally_static_expression(
                specialization_, power.operands[1])) {
            return;
        }
        const auto exponent
            = specialization_.evaluate_integral_expression(power.operands[1]);
        if (!exponent || *exponent >= 0) {
            return;
        }
        const auto operand = specialization_.find_expression(power.operands[1]);
        report("FSIM-ELAB-091",
            "bounded VHDL integer exponentiation requires a locally static "
            "nonnegative exponent",
            operand && operand->vhdl != nullptr ? operand->vhdl->source
                                                : power.source);
    }
};

} // namespace

std::vector<VhdlHirTypeValidationIssue> check_vhdl_choice_rules(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture)
{
    return ChoiceRules { specialization, entity, architecture }.run();
}

} // namespace fsim::elaboration
