// SPDX-License-Identifier: Apache-2.0
#include "vhdl_hir_type_validation.hpp"
#include "fsim/support/vhdl_identifier.hpp"

#include "elaborator_internal.hpp"

#include "fsim/elaboration/elaborator.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iterator>
#include <set>
#include <tuple>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace fsim::elaboration {
namespace {

using elaboration_detail::equal_ignoring_ascii_case;
using elaboration_detail::lowercase_ascii;
using elaboration_detail::lowercase_ascii_copy;
using elaboration_detail::simple_name_of;
using semantic::vhdl::RangeConstraint;
using semantic::vhdl::RangeKind;
using semantic::vhdl::SubtypeIndication;
using semantic::vhdl::TypeDefinition;
using semantic::vhdl::TypeForm;

// The number of values from `low` through `high` (low <= high), or nothing
// when it does not fit in 64 bits.
[[nodiscard]] constexpr std::optional<std::uint64_t> inclusive_extent(
    const std::int64_t low, const std::int64_t high) noexcept
{
    const auto distance = static_cast<std::uint64_t>(high)
        - static_cast<std::uint64_t>(low);
    return distance == std::numeric_limits<std::uint64_t>::max()
        ? std::nullopt
        : std::optional<std::uint64_t> { distance + 1U };
}

enum class ProfileKind : std::uint8_t {
    unknown,
    scalar,
    integer,
    enumeration,
    array,
    record,
    string,
};

struct ArrayDimensionProfile {
    std::optional<std::int64_t> minimum;
    std::optional<std::int64_t> maximum;
    bool constrained { };
    bool descending { };
    bool null { };
    std::optional<std::int64_t> left { };
    std::optional<std::int64_t> right { };
    bool bounds_known { };
    bool direction_known { };
    bool null_known { };
};

struct TypeProfile {
    ProfileKind kind { ProfileKind::unknown };
    std::optional<std::int64_t> minimum;
    std::optional<std::int64_t> maximum;
    std::vector<ArrayDimensionProfile> dimensions;
    bool indefinite_element { };
};

class Validator final {
public:
    Validator(
        const semantic::SpecializedHirUnit& specialization,
        const semantic::vhdl::Unit& entity,
        const semantic::vhdl::Unit& architecture)
        : specialization_ { specialization }
        , entity_ { entity }
        , architecture_ { architecture }
    {
    }

    // An analysis-only validator: the expression rules it answers read no
    // entity or architecture state.
    explicit Validator(const semantic::SpecializedHirUnit& specialization)
        : Validator { specialization, detached_unit(), detached_unit() }
    {
    }

    [[nodiscard]] bool locally_static(const semantic::ExpressionId id) const
    {
        std::unordered_set<std::uint32_t> active_expressions;
        std::unordered_set<std::uint32_t> active_declarations;
        return locally_static_expression(
            id, active_expressions, active_declarations);
    }

    // Check only the VHNAME name rules, skipping the type
    // checks that need generic values.
    void set_names_only() noexcept
    {
        names_only_ = true;
    }

    [[nodiscard]] std::vector<VhdlHirTypeValidationIssue> run()
    {
        // STD.ENV, IEEE.MATH_REAL/MATH_COMPLEX and the VHDL-2019 reflection
        // API are compiler-supplied without retained declarations.
        for (const auto* unit : { &entity_, &architecture_ }) {
            if (unit->standard == "2019") {
                name_check_ = false;
            }
            for (const auto& item : unit->context) {
                for (const auto& name : item.selected_names) {
                    for (const auto* spelling : { &name.spelling, &name.canonical }) {
                        std::string lowered { *spelling };
                        lowercase_ascii(lowered);
                        if (lowered.find("std.env") != std::string::npos
                            || lowered.find("math_real") != std::string::npos
                            || lowered.find("math_complex") != std::string::npos) {
                            name_check_ = false;
                        }
                        // MATH_REAL overloads "mod" and "**" for REAL.
                        if (lowered.find("math_real") != std::string::npos
                            || lowered.find("math_complex") != std::string::npos) {
                            operator_check_ = false;
                        }
                    }
                }
            }
        }
        // The VITAL packages' declarations are compiler-supplied and not
        // retained, so names in a design using VITAL cannot be checked.
        for (const auto& unit : specialization_.design().vhdl_hir.units()) {
            std::string lowered { unit.name };
            lowercase_ascii(lowered);
            if (lowered.starts_with("vital_") && equal_ignoring_ascii_case(unit.library, "ieee")) {
                name_check_ = false;
                break;
            }
        }
        collect_unit_types(entity_);
        collect_unit_types(architecture_);
        if (names_only_) {
            // The name rules are all checked while walking the operations.
            validate_operations();
            return std::move(issues_);
        }
        validate_unit_objects(entity_);
        validate_unit_objects(architecture_);
        validate_attribute_specifications();
        validate_actual_types();
        for (const auto id : relevant_types_) {
            const auto type = specialization_.find_type(
                semantic::TypeId::from_index(id));
            if (type && type->vhdl != nullptr) {
                validate_type_definition(*type->vhdl);
            }
        }
        validate_operations();
        validate_instance_boundaries();
        return std::move(issues_);
    }

private:
    const TypeDefinition* type_definition(const semantic::TypeId id) const
    {
        const auto type = specialization_.find_type(id);
        return type && type->vhdl != nullptr ? type->vhdl : nullptr;
    }

    semantic::ScopeId type_declaration_scope(
        const TypeDefinition& definition) const
    {
        if (!definition.declaration.valid()) {
            return {};
        }
        const auto declaration = specialization_.find_declaration(
            definition.declaration);
        return declaration && declaration->vhdl != nullptr
                && declaration->vhdl->scope.valid()
            ? declaration->vhdl->scope
            : semantic::ScopeId { };
    }

    std::optional<semantic::TypeId> named_type(
        const std::string_view spelling,
        const semantic::ScopeId use_scope = { }) const
    {
        const auto package_member_type = [&](
            const std::string_view selected_spelling,
            const semantic::ScopeId scope,
            const std::optional<semantic::UnitId> expected_package)
            -> std::optional<semantic::TypeId> {
            if (!scope.valid()) {
                return std::nullopt;
            }
            semantic::vhdl::Name selected_name;
            selected_name.spelling = std::string { selected_spelling };
            const semantic::CompiledDeclarationPredicate type_member
                = [](const semantic::CompiledDeclarationView& candidate) {
                if (candidate.vhdl == nullptr
                    || !candidate.vhdl->declared_type) {
                    return false;
                }
                using Form = semantic::vhdl::DeclarationForm;
                return candidate.vhdl->form == Form::type
                    || candidate.vhdl->form == Form::subtype
                    || candidate.vhdl->form == Form::generic_type;
            };
            const auto member = semantic::CompiledDesignResolver {
                specialization_ }
                                    .resolve_vhdl_package_members(
                                        selected_name, scope, type_member)
                                    .unique();
            if (!member
                || (expected_package
                    && member->template_unit != *expected_package)) {
                return std::nullopt;
            }
            const auto package = specialization_.design().find_unit(
                member->template_unit);
            const auto declaration = specialization_.find_declaration(
                member->member);
            if (!package || package->vhdl == nullptr
                || package->vhdl->kind
                    != semantic::vhdl::UnitKind::package
                || !declaration || declaration->vhdl == nullptr
                || declaration->vhdl->scope != package->vhdl->scope
                || !declaration->vhdl->declared_type) {
                return std::nullopt;
            }
            return declaration->vhdl->declared_type;
        };

        if (use_scope.valid()) {
            if (const auto resolved
                = semantic::CompiledDesignResolver { specialization_ }
                      .resolve_vhdl_named_type(spelling, use_scope)) {
                return resolved;
            }
            // Selected package types are resolved from the package's direct
            // declaration set. The package-member resolver also accounts for
            // package instances and visibility from this use scope.
            if (const auto package_type = package_member_type(
                    spelling, use_scope, std::nullopt)) {
                return package_type;
            }
        }
        const auto name = simple_name_of(spelling);
        const auto last_colon = spelling.rfind(':');
        if (last_colon != std::string_view::npos && last_colon != 0U) {
            const auto offset_colon = spelling.rfind(':', last_colon - 1U);
            if (offset_colon != std::string_view::npos
                && offset_colon + 1U < last_colon) {
                const auto offset_spelling = spelling.substr(
                    offset_colon + 1U,
                    last_colon - offset_colon - 1U);
                std::uint64_t offset { };
                const auto parsed = std::from_chars(
                    offset_spelling.data(),
                    offset_spelling.data() + offset_spelling.size(),
                    offset);
                if (parsed.ec == std::errc { }
                    && parsed.ptr
                        == offset_spelling.data()
                            + offset_spelling.size()) {
                    const auto source_name = spelling.substr(
                        0U, offset_colon);
                    std::optional<semantic::TypeId> exact;
                    const auto& spans
                        = specialization_.design().semantics.source_spans();
                    for (const auto& stored :
                        specialization_.design().vhdl_hir.types()) {
                        const auto type = specialization_.find_type(
                            stored.id);
                        if (!type || type->vhdl == nullptr
                            || !equal_ignoring_ascii_case(type->vhdl->name, name)
                            || !type->vhdl->source.valid()
                            || type->vhdl->source.value() >= spans.size()) {
                            continue;
                        }
                        const auto& source
                            = spans[type->vhdl->source.value()];
                        if (source.logical_name != source_name
                            || source.begin.offset != offset) {
                            continue;
                        }
                        if (exact && *exact != type->vhdl->id) {
                            return std::nullopt;
                        }
                        exact = type->vhdl->id;
                    }
                    if (exact) {
                        return exact;
                    }
                }
            }
        }
        if (use_scope.valid()) {
            return std::nullopt;
        }
        const auto first_separator = spelling.find('.');
        if (first_separator != std::string_view::npos) {
            const auto second_separator
                = spelling.find('.', first_separator + 1U);
            std::string_view library;
            std::string_view package_name;
            std::string_view type_name;
            std::string standard_spelling;
            auto selected_spelling = spelling;
            if (second_separator == std::string_view::npos) {
                if (!equal_ignoring_ascii_case(
                        spelling.substr(0U, first_separator), "standard")) {
                    return std::nullopt;
                }
                // Preserve the traditional STANDARD.<type> spelling while
                // anchoring it to the predefined STD.STANDARD package.
                library = "std";
                package_name = "standard";
                type_name = spelling.substr(first_separator + 1U);
                standard_spelling = "std.standard." + std::string { type_name };
                selected_spelling = standard_spelling;
            } else {
                // A scope-free selected name has no lexical environment.
                // Resolve only an exact library/package member, using the
                // central package resolver and requiring the package's direct
                // declaration scope; never select a nested declaration from
                // a package body by its simple identifier alone.
                if (spelling.find('.', second_separator + 1U)
                    != std::string_view::npos) {
                    return std::nullopt;
                }
                library = spelling.substr(0U, first_separator);
                package_name = spelling.substr(
                    first_separator + 1U,
                    second_separator - first_separator - 1U);
                type_name = spelling.substr(second_separator + 1U);
            }
            if (library.empty() || package_name.empty() || type_name.empty()) {
                return std::nullopt;
            }
            std::optional<semantic::TypeId> selected;
            for (const auto& package :
                specialization_.design().vhdl_units()) {
                if (package.kind != semantic::vhdl::UnitKind::package
                    || !package.primary_name.empty()
                    || !equal_ignoring_ascii_case(
                        package.library, library)
                    || !equal_ignoring_ascii_case(
                        package.name, package_name)) {
                    continue;
                }
                const auto resolved = package_member_type(
                    selected_spelling, package.scope, package.id);
                if (!resolved) {
                    continue;
                }
                if (selected && *selected != *resolved) {
                    return std::nullopt;
                }
                selected = *resolved;
            }
            return selected;
        }
        std::optional<semantic::TypeId> selected;
        for (const auto& stored :
            specialization_.design().vhdl_hir.types()) {
            const auto type = specialization_.find_type(stored.id);
            if (!type || type->vhdl == nullptr
                || !equal_ignoring_ascii_case(type->vhdl->name, name)) {
                continue;
            }
            if (selected && *selected != type->vhdl->id) {
                return std::nullopt;
            }
            selected = type->vhdl->id;
        }
        return selected;
    }

    std::optional<semantic::TypeId> subtype_type(
        const SubtypeIndication& subtype) const
    {
        if (subtype.type_mark.target.valid()) {
            return subtype.type_mark.target;
        }
        return named_type(subtype.type_mark.spelling);
    }

    // The subtype of record element `element` of an object of subtype
    // `owner`, refined by the element constraints that `owner` and the
    // named subtypes it denotes impose (IEEE 1076-2008 5.3.3.1).
    SubtypeIndication record_member_subtype(
        const SubtypeIndication& owner,
        const semantic::vhdl::RecordElement& element) const
    {
        return constrained_element(owner, element.name, element.subtype);
    }

    // The element subtype `element` of `owner` at `step` ("" for an array
    // element), refined by the element constraints of `owner` and of the
    // named subtypes it denotes.
    SubtypeIndication constrained_element(
        const SubtypeIndication& owner,
        const std::string_view step,
        const SubtypeIndication& element) const
    {
        auto collected = owner;
        auto target = owner.type_mark.target;
        std::unordered_set<std::uint32_t> visited;
        while (target.valid() && visited.insert(target.value()).second) {
            const auto* definition = type_definition(target);
            if (definition == nullptr
                || (definition->form != TypeForm::subtype
                    && definition->form != TypeForm::alias)) {
                break;
            }
            collected.element_constraints.insert(
                collected.element_constraints.begin(),
                definition->base.element_constraints.begin(),
                definition->base.element_constraints.end());
            target = definition->base.type_mark.target;
        }
        return specialized_subtype(semantic::vhdl::constrained_element_subtype(
            collected, step, element));
    }

    SubtypeIndication specialized_subtype(SubtypeIndication subtype) const
    {
        return semantic::CompiledDesignResolver { specialization_ }
            .effective_vhdl_subtype(subtype, specialization_.scope())
            .value_or(std::move(subtype));
    }

    bool contextual_result_declaration(
        const semantic::DeclarationId id) const
    {
        return std::ranges::any_of(
            specialization_.design().vhdl_hir.declarations(),
            [&](const auto& stored) {
                const auto declaration
                    = specialization_.find_declaration(stored.id);
                return declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->callable
                    && declaration->vhdl->callable->return_identifier
                    && *declaration->vhdl->callable->return_identifier
                        == id;
            });
    }

    bool subprogram_formal(const semantic::DeclarationId id) const
    {
        return std::ranges::any_of(
            specialization_.design().vhdl_hir.declarations(),
            [&](const auto& stored) {
                const auto declaration
                    = specialization_.find_declaration(stored.id);
                return declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->callable
                    && std::ranges::find(
                           declaration->vhdl->callable->formals, id)
                        != declaration->vhdl->callable->formals.end();
            });
    }

    bool contextual_result_prefix(
        const semantic::ExpressionId prefix_id,
        const SubtypeIndication& subtype) const
    {
        const auto prefix = specialization_.find_expression(prefix_id);
        if (prefix && prefix->vhdl != nullptr
            && prefix->vhdl->referenced_name
            && prefix->vhdl->referenced_name->selected
            && contextual_result_declaration(
                *prefix->vhdl->referenced_name->selected)) {
            return true;
        }
        // A direct call returning an unconstrained array is only a candidate
        // for contextual range deferral here. The lowerer must still prove
        // the call's concrete result shape before emitting the attribute.
        if (prefix && prefix->vhdl != nullptr
            && prefix->vhdl->kind
                == semantic::vhdl::ExpressionKind::call
            && prefix->vhdl->referenced_name
            && prefix->vhdl->referenced_name->selected) {
            const auto function = specialization_.find_declaration(
                *prefix->vhdl->referenced_name->selected);
            if (function && function->vhdl != nullptr
                && function->vhdl->form
                    == semantic::vhdl::DeclarationForm::function
                && function->vhdl->callable
                && function->vhdl->callable->return_type) {
                const auto return_subtype = specialized_subtype(
                    *function->vhdl->callable->return_type);
                const auto actual_subtype = specialized_subtype(subtype);
                const auto return_type = subtype_type(return_subtype);
                const auto actual_type = subtype_type(actual_subtype);
                const auto return_root = return_type
                    ? array_root(*return_type) : std::nullopt;
                const auto actual_root = actual_type
                    ? array_root(*actual_type) : std::nullopt;
                if (return_root && actual_root
                    && *return_root == *actual_root) {
                    const auto return_profile = profile(return_subtype);
                    const auto actual_profile = profile(actual_subtype);
                    const auto is_unconstrained_rank_one_array
                        = [](const TypeProfile& candidate) {
                              return candidate.kind == ProfileKind::array
                                  && candidate.dimensions.size() == 1U
                                  && !candidate.dimensions.front().constrained;
                          };
                    if (is_unconstrained_rank_one_array(return_profile)
                        && is_unconstrained_rank_one_array(actual_profile)) {
                        return true;
                    }
                }
            }
        }
        const auto type = subtype_type(specialized_subtype(subtype));
        const auto* definition = type ? type_definition(*type) : nullptr;
        return definition != nullptr
            && contextual_result_declaration(definition->declaration);
    }

    std::optional<std::int64_t> static_integer(
        const semantic::ExpressionId id,
        std::unordered_set<std::uint32_t>& active) const
    {
        if (const auto folded
            = specialization_.evaluate_integral_expression(id)) {
            return folded;
        }
        if (!active.insert(id.value()).second) {
            return std::nullopt;
        }
        const auto guard = [&] {
            active.erase(id.value());
        };
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            guard();
            return std::nullopt;
        }
        const auto& value = *expression->vhdl;
        if (value.kind == semantic::vhdl::ExpressionKind::name) {
            const semantic::CompiledDeclarationPredicate constant
                = [](const semantic::CompiledDeclarationView& candidate) {
                      if (candidate.vhdl == nullptr) {
                          return false;
                      }
                      using Form = semantic::vhdl::DeclarationForm;
                      return candidate.vhdl->form
                              == Form::generic_constant
                          || candidate.vhdl->form == Form::constant
                          || candidate.vhdl->form
                              == Form::enumeration_literal;
                  };
            const semantic::CompiledDesignResolver resolver {
                specialization_ };
            auto declaration = resolver
                                   .resolve_expression_name(
                                       id, constant, false)
                                   .unique();
            if (!declaration && value.referenced_name) {
                const auto package = resolver
                                         .resolve_vhdl_package_members(
                                             *value.referenced_name,
                                             value.scope, constant)
                                         .unique();
                if (package) {
                    declaration = package->member;
                }
            }
            const auto record = declaration
                ? specialization_.find_declaration(*declaration)
                : std::nullopt;
            if (record && record->vhdl != nullptr
                && record->vhdl->initializer) {
                const auto result = static_integer(
                    *record->vhdl->initializer, active);
                guard();
                return result;
            }
        }
        if (value.kind == semantic::vhdl::ExpressionKind::integer_literal) {
            std::string spelling;
            spelling.reserve(value.text.size());
            std::ranges::copy_if(value.text,
                std::back_inserter(spelling),
                [](const char character) { return character != '_'; });
            std::int64_t result { };
            const auto parsed = std::from_chars(
                spelling.data(), spelling.data() + spelling.size(), result);
            guard();
            return parsed.ec == std::errc { }
                    && parsed.ptr == spelling.data() + spelling.size()
                ? std::optional { result }
                : std::nullopt;
        }
        if (value.kind == semantic::vhdl::ExpressionKind::unary
            && value.operands.size() == 1U) {
            const auto operand = static_integer(value.operands.front(), active);
            guard();
            if (!operand || (value.text == "-"
                    && *operand == std::numeric_limits<std::int64_t>::min())) {
                return std::nullopt;
            }
            if (value.text == "-") {
                return -*operand;
            }
            if (value.text == "+") {
                return operand;
            }
            return std::nullopt;
        }
        if (value.kind == semantic::vhdl::ExpressionKind::binary
            && value.operands.size() == 2U
            && (value.text == "+" || value.text == "-")) {
            const auto left = static_integer(value.operands[0], active);
            const auto right = static_integer(value.operands[1], active);
            guard();
            if (!left || !right) {
                return std::nullopt;
            }
            if (value.text == "+") {
                if ((*right > 0
                        && *left > std::numeric_limits<std::int64_t>::max()
                                - *right)
                    || (*right < 0
                        && *left < std::numeric_limits<std::int64_t>::min()
                                - *right)) {
                    return std::nullopt;
                }
                return *left + *right;
            }
            if ((*right < 0
                    && *left > std::numeric_limits<std::int64_t>::max()
                            + *right)
                || (*right > 0
                    && *left < std::numeric_limits<std::int64_t>::min()
                            + *right)) {
                return std::nullopt;
            }
            return *left - *right;
        }
        const auto attribute = value.kind
                == semantic::vhdl::ExpressionKind::call
            && !value.operands.empty()
            && (semantic::vhdl::is_scalar_bound_attribute(value.text)
                || value.text == "'length"
                || value.text == "'ascending");
        if (!attribute) {
            guard();
            return std::nullopt;
        }
        auto prefix = expression_subtype(value.operands.front());
        if (!prefix) {
            const auto prefix_expression = specialization_.find_expression(
                value.operands.front());
            std::optional<semantic::TypeId> type;
            if (prefix_expression
                && prefix_expression->vhdl != nullptr) {
                const semantic::CompiledDeclarationPredicate type_declaration
                    = semantic::declares_vhdl_type;
                const semantic::CompiledDesignResolver resolver {
                    specialization_
                };
                const auto declaration
                    = resolver.resolve_expression_name(
                                  value.operands.front(),
                                  type_declaration, true)
                          .unique();
                const auto view = declaration
                    ? specialization_.find_declaration(*declaration)
                    : std::nullopt;
                type = view && view->vhdl != nullptr
                        && view->vhdl->declared_type
                    ? view->vhdl->declared_type
                    : named_type(prefix_expression->vhdl->text,
                          prefix_expression->vhdl->scope);
            }
            const auto* definition = type ? type_definition(*type) : nullptr;
            if (definition != nullptr) {
                prefix = definition->base;
                prefix->type_mark.target = definition->id;
            }
        }
        if (!prefix) {
            guard();
            return std::nullopt;
        }
        const auto prefix_profile = profile(*prefix);
        std::int64_t dimension { 1 };
        if (value.operands.size() == 2U) {
            const auto selected = static_integer(value.operands[1], active);
            if (!selected) {
                guard();
                return std::nullopt;
            }
            dimension = *selected;
        }
        if (prefix_profile.kind != ProfileKind::array || dimension <= 0
            || static_cast<std::uint64_t>(dimension)
                > prefix_profile.dimensions.size()) {
            guard();
            return std::nullopt;
        }
        const auto& range = prefix_profile.dimensions[
            static_cast<std::size_t>(dimension - 1)];
        const auto prefix_expression = specialization_.find_expression(
            value.operands.front());
        const bool prefix_is_object
            = prefix_expression && prefix_expression->vhdl != nullptr
            && name_object_subtype(*prefix_expression->vhdl).has_value();
        if (prefix_is_object && !range.constrained) {
            guard();
            return std::nullopt;
        }
        guard();
        if (value.text == "'left") {
            if (range.left) {
                return range.left;
            }
            if (range.constrained && !range.bounds_known) {
                return std::nullopt;
            }
            if (!range.minimum || !range.maximum) {
                return std::nullopt;
            }
            if (!range.direction_known) {
                return std::nullopt;
            }
            return range.descending ? range.maximum : range.minimum;
        }
        if (value.text == "'right") {
            if (range.right) {
                return range.right;
            }
            if (range.constrained && !range.bounds_known) {
                return std::nullopt;
            }
            if (!range.minimum || !range.maximum) {
                return std::nullopt;
            }
            if (!range.direction_known) {
                return std::nullopt;
            }
            return range.descending ? range.minimum : range.maximum;
        }
        if (value.text == "'low") {
            if (range.constrained && !range.bounds_known) {
                return std::nullopt;
            }
            return range.minimum;
        }
        if (value.text == "'high") {
            if (range.constrained && !range.bounds_known) {
                return std::nullopt;
            }
            return range.maximum;
        }
        if (value.text == "'ascending") {
            if (!range.direction_known) {
                return std::nullopt;
            }
            return range.descending ? 0 : 1;
        }
        if (range.null_known && range.null) {
            return 0;
        }
        if (range.bounds_known && range.left && range.right
            && !range.null_known && *range.left != *range.right) {
            return std::nullopt;
        }
        if (range.left && range.right && *range.left == *range.right) {
            return 1;
        }
        if (range.constrained && !range.null_known) {
            return std::nullopt;
        }
        if (!range.minimum || !range.maximum) {
            return std::nullopt;
        }
        const auto distance = static_cast<std::uint64_t>(*range.maximum)
            - static_cast<std::uint64_t>(*range.minimum);
        return distance < static_cast<std::uint64_t>(
                              std::numeric_limits<std::int64_t>::max())
            ? std::optional { static_cast<std::int64_t>(distance + 1U) }
            : std::nullopt;
    }

    std::optional<std::int64_t> bound(
        const std::optional<std::int64_t> value,
        const std::optional<semantic::ExpressionId> expression) const
    {
        if (value || !expression) {
            return value;
        }
        std::unordered_set<std::uint32_t> active;
        return static_integer(*expression, active);
    }

    std::optional<std::int64_t> array_dimension_bound(
        const TypeDefinition& array,
        const std::size_t dimension_index,
        const std::optional<std::int64_t> value,
        const std::optional<semantic::ExpressionId> expression) const
    {
        if (!expression) {
            return value;
        }
        if (const auto index_subtype = array_dimension_index_subtype(
                array, dimension_index)) {
            if (subtype_operand_type(*index_subtype, 0U).kind
                == OperandClass::enumeration) {
                // A digit character can also parse as an integer, but an
                // enumerated index uses its declared literal ordinal.
                if (const auto ordinal = enumeration_index_value(
                        *index_subtype, *expression)) {
                    return ordinal;
                }
                const auto attribute = specialization_.find_expression(
                    *expression);
                if (attribute && attribute->vhdl != nullptr
                    && (semantic::vhdl::is_scalar_bound_attribute(
                        attribute->vhdl->text))
                    && attribute->vhdl->kind
                        == semantic::vhdl::ExpressionKind::call
                    && attribute->vhdl->operands.size() == 1U) {
                    const auto prefix = expression_subtype(
                        attribute->vhdl->operands.front());
                    const auto prefix_type = prefix
                        ? subtype_type(*prefix)
                        : std::nullopt;
                    const auto prefix_root = prefix_type
                        ? enumeration_root_type(*prefix_type)
                        : std::nullopt;
                    const auto index_type = subtype_type(*index_subtype);
                    const auto index_root = index_type
                        ? enumeration_root_type(*index_type)
                        : std::nullopt;
                    if (prefix_root && index_root
                        && *prefix_root == *index_root
                        && locally_static(*expression)) {
                        return bound(std::nullopt, expression);
                    }
                }
                return std::nullopt;
            }
        } else if (const auto literal_order
            = targetless_std_ulogic_index_literal_order(
                array, dimension_index)) {
            if (const auto ordinal = targetless_std_ulogic_index_literal(
                    *literal_order, *expression)) {
                return ordinal;
            }
            const auto attribute = specialization_.find_expression(
                *expression);
            if (attribute && attribute->vhdl != nullptr
                && (semantic::vhdl::is_scalar_bound_attribute(
                    attribute->vhdl->text))
                && attribute->vhdl->kind == semantic::vhdl::ExpressionKind::call
                && attribute->vhdl->operands.size() == 1U) {
                const auto prefix = specialization_.find_expression(
                    attribute->vhdl->operands.front());
                if (prefix && prefix->vhdl != nullptr
                    && prefix->vhdl->kind
                        == semantic::vhdl::ExpressionKind::name
                    && (!prefix->vhdl->referenced_name
                        || (!prefix->vhdl->referenced_name->selected
                            && prefix->vhdl->referenced_name->overloads
                                   .empty()))
                    && equal_ignoring_ascii_case(
                        prefix->vhdl->text, "std_ulogic")
                    && prefix->vhdl->scope
                        == type_declaration_scope(array)) {
                    return bound(std::nullopt, expression);
                }
            }
            return std::nullopt;
        }
        return bound(value, expression);
    }

    // An unconstrained index of the ascending range `minimum to maximum`.
    static ArrayDimensionProfile ascending_index_profile(
        const std::int64_t minimum, const std::int64_t maximum)
    {
        auto result = ArrayDimensionProfile { minimum, maximum, false, false };
        result.left = minimum;
        result.right = maximum;
        result.bounds_known = true;
        result.direction_known = true;
        result.null_known = true;
        return result;
    }

    static ArrayDimensionProfile index_profile(
        const std::string_view spelling)
    {
        constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
        const auto name = simple_name_of(spelling);
        if (equal_ignoring_ascii_case(name, "natural")) {
            return ascending_index_profile(0, maximum);
        }
        if (equal_ignoring_ascii_case(name, "positive")) {
            return ascending_index_profile(1, maximum);
        }
        if (equal_ignoring_ascii_case(name, "integer")) {
            return ascending_index_profile(
                std::numeric_limits<std::int64_t>::min(), maximum);
        }
        if (equal_ignoring_ascii_case(name, "boolean")
            || equal_ignoring_ascii_case(name, "bit")) {
            return ascending_index_profile(0, 1);
        }
        if (equal_ignoring_ascii_case(name, "character")) {
            return ascending_index_profile(0, 255);
        }
        return { };
    }

    ArrayDimensionProfile index_profile(
        const semantic::vhdl::Name& index_subtype) const
    {
        auto result = index_profile(index_subtype.spelling);
        const auto declaration = index_subtype.selected
            ? specialization_.find_declaration(*index_subtype.selected)
            : std::nullopt;
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->declared_type) {
            return result;
        }
        auto type_id = *declaration->vhdl->declared_type;
        std::unordered_set<std::uint32_t> visited;
        while (type_id.valid() && visited.insert(type_id.value()).second) {
            const auto* definition = type_definition(type_id);
            if (definition == nullptr) {
                break;
            }
            if (!definition->enumeration_literals.empty()) {
                const auto last
                    = definition->enumeration_literals.size() - 1U;
                if (last > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    break;
                }
                return ascending_index_profile(
                    0, static_cast<std::int64_t>(last));
            }
            if (definition->scalar_range) {
                const auto& range = *definition->scalar_range;
                SubtypeIndication index_type;
                index_type.type_mark.target = type_id;
                index_type.type_mark.spelling = definition->name;
                const auto index_operand_type
                    = subtype_operand_type(index_type, 0U);
                const auto range_bound = [&](
                                             const std::optional<std::int64_t> value,
                                             const std::optional<semantic::ExpressionId> expression) {
                    if (index_operand_type.kind == OperandClass::enumeration
                        && expression) {
                        return enumeration_index_value(
                            index_type, *expression);
                    }
                    return bound(value, expression);
                };
                const auto left = range_bound(
                    range.left, range.left_expression);
                const auto right = range_bound(
                    range.right, range.right_expression);
                if (left && right) {
                    result.minimum = std::min(*left, *right);
                    result.maximum = std::max(*left, *right);
                }
                result.left = left;
                result.right = right;
                result.bounds_known = left && right;
                result.direction_known = !range.direction_from_bounds;
                result.descending = range.descending;
                if (!range.direction_from_bounds && left && right) {
                    result.null = range.null
                        || (range.descending
                            ? *left < *right
                            : *left > *right);
                    result.null_known = true;
                } else if (range.direction_from_bounds && left && right
                    && *left == *right) {
                    result.null = false;
                    result.null_known = true;
                } else {
                    result.null = false;
                    result.null_known = false;
                }
                return result;
            }
            if (definition->form != TypeForm::subtype
                && definition->form != TypeForm::alias) {
                break;
            }
            type_id = definition->base.type_mark.target;
        }
        return result;
    }

    std::optional<semantic::TypeId> enumeration_root_type(
        semantic::TypeId type) const
    {
        const auto* base
            = semantic::vhdl_base_type_definition(specialization_, type);
        return base != nullptr && base->form == TypeForm::enumeration
            ? std::optional { base->id }
            : std::nullopt;
    }

    std::optional<std::int64_t> enumeration_index_value(
        const SubtypeIndication& expected_subtype,
        const semantic::ExpressionId expression_id,
        const std::size_t depth = 0U) const
    {
        if (depth > 64U) {
            return std::nullopt;
        }
        const auto expected_type = subtype_type(expected_subtype);
        const auto expected_root = expected_type
            ? enumeration_root_type(*expected_type)
            : std::nullopt;
        const auto* expected_definition = expected_root
            ? type_definition(*expected_root)
            : nullptr;
        const auto expression = specialization_.find_expression(
            expression_id);
        if (expected_root == std::nullopt
            || expected_definition == nullptr
            || expected_definition->form != TypeForm::enumeration
            || expression == std::nullopt
            || expression->vhdl == nullptr) {
            return std::nullopt;
        }

        const auto& value = *expression->vhdl;
        using Kind = semantic::vhdl::ExpressionKind;
        constexpr std::string_view qualified_prefix {
            semantic::vhdl::qualified_expression_prefix };
        if (value.kind == Kind::call
            && value.text.starts_with(qualified_prefix)
            && value.operands.size() == 1U) {
            const auto qualified_subtype = expression_subtype(expression_id);
            const auto qualified_type = qualified_subtype
                ? subtype_type(*qualified_subtype)
                : std::nullopt;
            const auto qualified_root = qualified_type
                ? enumeration_root_type(*qualified_type)
                : std::nullopt;
            if (!qualified_root || *qualified_root != *expected_root) {
                return std::nullopt;
            }
            if (!locally_static(expression_id)) {
                return std::nullopt;
            }
            return enumeration_index_value(
                expected_subtype, value.operands.front(), depth + 1U);
        }
        const auto direct_literal
            = value.kind == Kind::logic_literal
            || (value.kind == Kind::name
                && value.text.starts_with('\''));
        const auto selected = value.referenced_name
            ? value.referenced_name->selected
            : std::nullopt;
        if (value.kind == Kind::call && value.operands.size() == 1U
            && (value.text == "'low" || value.text == "'high"
                || value.text == "'left" || value.text == "'right")) {
            const auto prefix = specialization_.find_expression(
                value.operands.front());
            if (prefix && prefix->vhdl != nullptr
                && prefix->vhdl->kind == Kind::name) {
                const auto& prefix_name = prefix->vhdl->referenced_name;
                std::optional<semantic::TypeId> prefix_type;
                if (prefix_name && prefix_name->selected) {
                    const auto declaration = specialization_.find_declaration(
                        *prefix_name->selected);
                    if (declaration && declaration->vhdl != nullptr) {
                        using Form = semantic::vhdl::DeclarationForm;
                        const auto form = declaration->vhdl->form;
                        if (form == Form::type || form == Form::subtype
                            || form == Form::generic_type
                            || (form == Form::alias
                                && declaration->vhdl->declared_type)) {
                            prefix_type = declaration->vhdl->declared_type;
                        }
                    }
                } else {
                    const auto spelling = prefix_name
                        ? (prefix_name->text())
                        : std::string_view { prefix->vhdl->text };
                    prefix_type
                        = semantic::CompiledDesignResolver { specialization_ }
                              .resolve_vhdl_named_type(
                                  spelling,
                                  prefix->vhdl->scope.valid()
                                      ? std::optional { prefix->vhdl->scope }
                                      : std::nullopt);
                }
                if (prefix_type && *prefix_type == *expected_root
                    && !expected_definition->enumeration_literals.empty()) {
                    const bool low_endpoint
                        = value.text == "'low" || value.text == "'left";
                    return low_endpoint
                        ? expected_definition->enumeration_literals.front().ordinal
                        : expected_definition->enumeration_literals.back().ordinal;
                }
            }
        }
        const auto expected_operand_type
            = subtype_operand_type(expected_subtype, 0U);

        if (value.kind == Kind::name && !direct_literal && !selected) {
            return std::nullopt;
        }
        if (selected) {
            const auto declaration = specialization_.find_declaration(
                *selected);
            if (!declaration || declaration->vhdl == nullptr) {
                return std::nullopt;
            }
            using Form = semantic::vhdl::DeclarationForm;
            if (declaration->vhdl->form == Form::enumeration_literal) {
                const auto literal = std::ranges::find(
                    expected_definition->enumeration_literals, *selected,
                    &semantic::vhdl::EnumerationLiteral::declaration);
                if (literal
                    == expected_definition->enumeration_literals.end()) {
                    return std::nullopt;
                }
            } else if (value.kind == Kind::name
                && (declaration->vhdl->form == Form::constant
                    || declaration->vhdl->form == Form::generic_constant)) {
                const auto actual_operand_type = operand_type(
                    expression_id);
                if (actual_operand_type.kind != OperandClass::enumeration
                    || expected_operand_type.kind
                        != OperandClass::enumeration
                    || actual_operand_type.root.empty()
                    || expected_operand_type.root.empty()
                    || actual_operand_type.root
                        != expected_operand_type.root) {
                    return std::nullopt;
                }
                if (!locally_static(expression_id)) {
                    return std::nullopt;
                }
                // Keep the selected enum identity through the initializer;
                // generic integral folding reads a literal such as '1' as 1.
                return declaration->vhdl->initializer
                    ? enumeration_index_value(
                          expected_subtype,
                          *declaration->vhdl->initializer, depth + 1U)
                    : std::nullopt;
            } else if (!direct_literal
                || declaration->vhdl->form
                    != Form::enumeration_literal) {
                return std::nullopt;
            }
        } else if (!direct_literal) {
            return std::nullopt;
        }

        return semantic::CompiledDesignResolver { specialization_ }
            .vhdl_enumeration_literal_ordinal(
                *expected_root, expression_id);
    }

    std::optional<SubtypeIndication> array_dimension_index_subtype(
        const TypeDefinition& array,
        const std::size_t dimension_index) const
    {
        if (array.form != TypeForm::array
            || dimension_index >= array.array_dimensions.size()) {
            return std::nullopt;
        }
        const auto& index_name
            = array.array_dimensions[dimension_index].index_subtype;
        std::optional<semantic::TypeId> type;
        if (index_name.selected) {
            const auto declaration = specialization_.find_declaration(
                *index_name.selected);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->declared_type) {
                return std::nullopt;
            }
            using Form = semantic::vhdl::DeclarationForm;
            const auto form = declaration->vhdl->form;
            if (form != Form::type && form != Form::subtype
                && form != Form::generic_type) {
                return std::nullopt;
            }
            type = declaration->vhdl->declared_type;
        } else if (!index_name.spelling.empty()) {
            const auto scope = type_declaration_scope(array);
            const auto spelling = index_name.text();
            type = semantic::CompiledDesignResolver { specialization_ }
                       .resolve_vhdl_named_type(
                           spelling,
                           scope.valid()
                               ? std::optional { scope }
                               : std::nullopt);
        }
        if (!type) {
            return std::nullopt;
        }
        SubtypeIndication result;
        result.type_mark.target = *type;
        result.type_mark.spelling = index_name.spelling;
        return result;
    }

    bool implicit_standard_character_index(
        const TypeDefinition& array,
        const std::size_t dimension_index) const
    {
        if (array.form != TypeForm::array
            || dimension_index >= array.array_dimensions.size()) {
            return false;
        }
        const auto& index_name
            = array.array_dimensions[dimension_index].index_subtype;
        if (index_name.selected || !index_name.overloads.empty()
            || !equal_ignoring_ascii_case(
                index_name.spelling, "character")
            || (!index_name.canonical.empty()
                && !equal_ignoring_ascii_case(
                    index_name.canonical, "character"))) {
            return false;
        }

        const auto scope = type_declaration_scope(array);
        const auto& scopes = specialization_.design().semantics.scopes();
        if (!scope.valid() || scope.value() >= scopes.size()
            || scopes[scope.value()].id != scope) {
            return false;
        }
        const semantic::CompiledDesignResolver resolver { specialization_ };
        if (resolver.resolve_vhdl_named_type(
                index_name.spelling, scope)) {
            return false;
        }
        const auto is_type_declaration = [](
            const semantic::CompiledDeclarationView& candidate) {
            if (candidate.vhdl == nullptr) {
                return false;
            }
            using Form = semantic::vhdl::DeclarationForm;
            const auto form = candidate.vhdl->form;
            return form == Form::type || form == Form::subtype
                || form == Form::generic_type;
        };
        const auto visible_type = resolver.resolve_vhdl(
            index_name, scope, is_type_declaration);
        if (visible_type.status
                != semantic::CompiledResolutionStatus::not_found
            || !visible_type.candidates.empty()) {
            return false;
        }
        const auto visible_name = resolver.resolve_vhdl(
            index_name, scope,
            [](const semantic::CompiledDeclarationView&) { return true; });
        return visible_name.status
                == semantic::CompiledResolutionStatus::not_found
            && visible_name.candidates.empty();
    }

    std::optional<std::string_view> targetless_std_ulogic_index_literal_order(
        const TypeDefinition& array,
        const std::size_t dimension_index) const
    {
        if (array.form != TypeForm::array
            || dimension_index >= array.array_dimensions.size()) {
            return std::nullopt;
        }
        const auto& index_name
            = array.array_dimensions[dimension_index].index_subtype;
        const auto spelling = index_name.text();
        if (index_name.selected
            || !equal_ignoring_ascii_case(
                index_name.spelling, "std_ulogic")
            || !equal_ignoring_ascii_case(spelling, "std_ulogic")) {
            return std::nullopt;
        }

        const auto scope = type_declaration_scope(array);
        if (!scope.valid()) {
            return std::nullopt;
        }
        const semantic::CompiledDesignResolver resolver { specialization_ };
        if (!resolver.vhdl_builtin_type_visible(spelling, scope)
            || !resolver.vhdl_builtin_package_member_imported(
                "ieee", "std_logic_1164", "std_ulogic", scope)
            || resolver.resolve_vhdl_named_type(spelling, scope)) {
            return std::nullopt;
        }

        semantic::vhdl::Name type_name;
        type_name.spelling = index_name.spelling;
        type_name.canonical = std::string { spelling };
        const auto visible_type = resolver.resolve_vhdl(
            type_name, scope,
            [](const semantic::CompiledDeclarationView& candidate) {
                if (candidate.vhdl == nullptr) {
                    return false;
                }
                using Form = semantic::vhdl::DeclarationForm;
                const auto form = candidate.vhdl->form;
                return form == Form::type || form == Form::subtype
                    || form == Form::generic_type;
            });
        if (visible_type.status
                != semantic::CompiledResolutionStatus::not_found
            || !visible_type.candidates.empty()) {
            return std::nullopt;
        }

        // This is the IEEE std_logic_1164 enumeration order, also used by
        // the HIR lowerer.
        return std::string_view { "UX01ZWLH-" };
    }

    std::optional<unsigned char> targetless_untyped_character_literal(
        const semantic::ExpressionId expression_id) const
    {
        const auto expression = specialization_.find_expression(
            expression_id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto& value = *expression->vhdl;
        using Kind = semantic::vhdl::ExpressionKind;
        const bool direct_character_literal
            = value.kind == Kind::logic_literal
            || (value.kind == Kind::name
                && value.text.size() == 3U
                && value.text.front() == '\''
                && value.text.back() == '\'');
        if (!direct_character_literal
            || value.text.size() != 3U
            || value.text.front() != '\''
            || value.text.back() != '\''
            || !value.nominal_type.empty()
            || (value.referenced_name
                && (value.referenced_name->selected
                    || !value.referenced_name->overloads.empty()))) {
            return std::nullopt;
        }
        return static_cast<unsigned char>(value.text[1]);
    }

    std::optional<std::int64_t> targetless_std_ulogic_index_literal(
        const std::string_view literal_order,
        const semantic::ExpressionId expression_id) const
    {
        const auto character
            = targetless_untyped_character_literal(expression_id);
        if (!character) {
            return std::nullopt;
        }
        const auto ordinal = literal_order.find(
            static_cast<char>(*character));
        return ordinal != std::string_view::npos
            ? std::optional { static_cast<std::int64_t>(ordinal) }
            : std::nullopt;
    }

    std::optional<std::int64_t> targetless_standard_character_index_literal(
        const TypeDefinition& array, const std::size_t dimension_index,
        const semantic::ExpressionId expression_id) const
    {
        if (!implicit_standard_character_index(
                array, dimension_index)) {
            return std::nullopt;
        }
        const auto character
            = targetless_untyped_character_literal(expression_id);
        return character
            ? std::optional { static_cast<std::int64_t>(*character) }
            : std::nullopt;
    }

    std::optional<std::int64_t> array_aggregate_index_value(
        const TypeDefinition* array,
        const std::size_t dimension_index,
        const semantic::ExpressionId expression) const
    {
        if (array == nullptr
            || dimension_index >= array->array_dimensions.size()) {
            return bound(std::nullopt, expression);
        }
        const auto index_subtype = array_dimension_index_subtype(
            *array, dimension_index);
        if (!index_subtype) {
            if (const auto ordinal
                = targetless_standard_character_index_literal(
                    *array, dimension_index, expression)) {
                return ordinal;
            }
            if (const auto literal_order
                = targetless_std_ulogic_index_literal_order(
                    *array, dimension_index)) {
                return targetless_std_ulogic_index_literal(
                    *literal_order, expression);
            }
            return bound(std::nullopt, expression);
        }
        const auto index_type = subtype_operand_type(*index_subtype, 0U);
        if (index_type.kind == OperandClass::enumeration) {
            return enumeration_index_value(*index_subtype, expression);
        }
        if (index_type.kind == OperandClass::integer) {
            const auto actual_type = operand_type(expression);
            if (actual_type.kind != OperandClass::unknown
                && (actual_type.kind != OperandClass::integer
                    || roots_conflict(index_type.root, actual_type.root))) {
                return std::nullopt;
            }
            return bound(std::nullopt, expression);
        }
        if (index_type.kind == OperandClass::unknown) {
            return bound(std::nullopt, expression);
        }
        return std::nullopt;
    }

    TypeProfile builtin_profile(const std::string_view spelling) const
    {
        const auto name = simple_name_of(spelling);
        if (equal_ignoring_ascii_case(name, "integer")) {
            return { ProfileKind::integer, std::nullopt, std::nullopt,
                { }, false };
        }
        if (equal_ignoring_ascii_case(name, "natural")) {
            return { ProfileKind::integer, 0,
                std::numeric_limits<std::int64_t>::max(), { }, false };
        }
        if (equal_ignoring_ascii_case(name, "positive")) {
            return { ProfileKind::integer, 1,
                std::numeric_limits<std::int64_t>::max(), { }, false };
        }
        if (equal_ignoring_ascii_case(name, "string")) {
            return { ProfileKind::string, std::nullopt, std::nullopt,
                { }, false };
        }
        constexpr std::array array_names {
            std::string_view { "bit_vector" },
            std::string_view { "boolean_vector" },
            std::string_view { "integer_vector" },
            std::string_view { "real_vector" },
            std::string_view { "time_vector" },
            std::string_view { "std_logic_vector" },
            std::string_view { "std_ulogic_vector" },
            std::string_view { "signed" },
            std::string_view { "unsigned" },
            std::string_view { "ufixed" },
            std::string_view { "sfixed" },
            std::string_view { "unresolved_ufixed" },
            std::string_view { "unresolved_sfixed" },
            std::string_view { "float" },
            std::string_view { "unresolved_float" },
            std::string_view { "u_float" },
        };
        if (std::ranges::any_of(array_names,
                [&](const auto candidate) {
                    return equal_ignoring_ascii_case(name, candidate);
                })) {
            TypeProfile result { ProfileKind::array, std::nullopt,
                std::nullopt, { }, false };
            result.dimensions.push_back(index_profile("integer"));
            return result;
        }
        constexpr std::array scalar_names {
            std::string_view { "bit" },
            std::string_view { "boolean" },
            std::string_view { "character" },
            std::string_view { "std_logic" },
            std::string_view { "std_ulogic" },
            std::string_view { "real" },
            std::string_view { "time" },
            std::string_view { "severity_level" },
            std::string_view { "file_open_kind" },
            std::string_view { "file_open_status" },
        };
        if (std::ranges::any_of(scalar_names,
                [&](const auto candidate) {
                    return equal_ignoring_ascii_case(name, candidate);
                })) {
            return { ProfileKind::scalar, std::nullopt, std::nullopt,
                { }, false };
        }
        return { };
    }

    TypeProfile builtin_profile(
        const SubtypeIndication& subtype,
        const semantic::ScopeId scope) const
    {
        auto result = builtin_profile(subtype.type_mark.spelling);
        if (result.kind != ProfileKind::array
            && result.kind != ProfileKind::string) {
            return result;
        }

        // A simple spelling is not enough to establish an array type. In
        // particular, two context-visible declarations can make a name such
        // as INTEGER_VECTOR ambiguous while the projection deliberately
        // retains only its source spelling. Preserve source ambiguity before
        // consulting the compiler-owned predefined identity path.
        const auto identity_resolution
            = semantic::CompiledDesignResolver { specialization_ }
                  .resolve_vhdl_type_identity(
                      subtype.type_mark.spelling, scope);
        if (identity_resolution.status
            == semantic::CompiledResolutionStatus::ambiguous) {
            return { };
        }
        if (!predefined_array_identity(subtype, scope)) {
            return { };
        }
        return result;
    }

    TypeProfile profile(const SubtypeIndication& original,
        const semantic::ScopeId use_scope = {}) const
    {
        std::unordered_set<std::uint32_t> visiting;
        return profile(original, visiting, use_scope);
    }

    TypeProfile profile(
        const SubtypeIndication& original,
        std::unordered_set<std::uint32_t>& visiting,
        const semantic::ScopeId use_scope = {}) const
    {
        const auto profile_scope = use_scope.valid()
            ? use_scope
            : specialization_.scope();
        const auto subtype = specialized_subtype(original);
        auto result = builtin_profile(subtype, profile_scope);
        if (result.kind == ProfileKind::unknown) {
            result = builtin_profile(original, profile_scope);
        }
        auto id = subtype_type(subtype);
        if (!id) {
            id = subtype_type(original);
        }
        const auto* definition = id ? type_definition(*id) : nullptr;
        if (definition != nullptr
            && visiting.insert(definition->id.value()).second) {
            const auto definition_scope
                = type_declaration_scope(*definition);
            const auto nested_scope = definition_scope.valid()
                ? definition_scope
                : profile_scope;
            switch (definition->form) {
            case TypeForm::subtype:
            case TypeForm::alias:
                result = profile(definition->base, visiting, nested_scope);
                break;
            case TypeForm::enumeration:
                result.kind = ProfileKind::enumeration;
                if (definition->scalar_range) {
                    result.minimum = bound(
                        definition->scalar_range->left,
                        definition->scalar_range->left_expression);
                    result.maximum = bound(
                        definition->scalar_range->right,
                        definition->scalar_range->right_expression);
                } else if (!definition->enumeration_literals.empty()) {
                    result.minimum = 0;
                    result.maximum = static_cast<std::int64_t>(
                        definition->enumeration_literals.size() - 1U);
                }
                break;
            case TypeForm::array:
                result.kind = ProfileKind::array;
                result.dimensions.clear();
                for (std::size_t dimension_index { };
                    dimension_index < definition->array_dimensions.size();
                    ++dimension_index) {
                    const auto& dimension
                        = definition->array_dimensions[dimension_index];
                    auto converted = index_profile(
                        dimension.index_subtype);
                    converted.constrained = !dimension.unconstrained
                        || dimension.constraint.has_value();
                    if (dimension.constraint) {
                        const auto left = array_dimension_bound(
                            *definition, dimension_index,
                            dimension.constraint->left,
                            dimension.constraint->left_expression);
                        const auto right = array_dimension_bound(
                            *definition, dimension_index,
                            dimension.constraint->right,
                            dimension.constraint->right_expression);
                        if (left && right) {
                            converted.minimum = std::min(*left, *right);
                            converted.maximum = std::max(*left, *right);
                        }
                        converted.left = left;
                        converted.right = right;
                        converted.bounds_known = left && right;
                        converted.direction_known
                            = !dimension.constraint->direction_from_bounds;
                        converted.descending
                            = dimension.constraint->descending;
                        if (!dimension.constraint->direction_from_bounds
                            && left && right) {
                            converted.null = dimension.constraint->null
                                || (dimension.constraint->descending
                                    ? *left < *right
                                    : *left > *right);
                            converted.null_known = true;
                        } else if (
                            dimension.constraint->direction_from_bounds
                            && left && right && *left == *right) {
                            converted.null = false;
                            converted.null_known = true;
                        } else {
                            converted.null = false;
                            converted.null_known = false;
                        }
                    }
                    result.dimensions.push_back(converted);
                }
                if (definition->element_subtype) {
                    const auto element = profile(
                        *definition->element_subtype, nested_scope);
                    result.indefinite_element
                        = element.kind == ProfileKind::unknown
                        || element.kind == ProfileKind::integer
                        || element.kind == ProfileKind::string
                        || (element.kind == ProfileKind::array
                            && std::ranges::any_of(
                                element.dimensions,
                                [](const auto& dimension) {
                                    return !dimension.constrained;
                                }));
                } else {
                    result.indefinite_element = true;
                }
                break;
            case TypeForm::record:
                result.kind = ProfileKind::record;
                break;
            case TypeForm::access:
            case TypeForm::file:
            case TypeForm::protected_type:
            case TypeForm::protected_body:
            case TypeForm::physical:
            case TypeForm::scalar:
                result.kind = ProfileKind::scalar;
                break;
            case TypeForm::unresolved:
                result.kind = ProfileKind::unknown;
                break;
            }
            visiting.erase(definition->id.value());
        }
        const auto* array_definition = result.kind == ProfileKind::array
            ? root_definition(subtype)
            : nullptr;
        apply_constraints_to_profile(
            result, subtype.constraints, array_definition);
        return result;
    }

    void apply_constraints_to_profile(
        TypeProfile& profile_value,
        const std::span<const RangeConstraint> constraints,
        const TypeDefinition* array_definition) const
    {
        if (profile_value.kind == ProfileKind::array) {
            for (std::size_t index { };
                index < constraints.size()
                    && index < profile_value.dimensions.size();
                ++index) {
                profile_value.dimensions[index].constrained = true;
                auto& dimension = profile_value.dimensions[index];
                const auto left = array_definition != nullptr
                    ? array_dimension_bound(
                          *array_definition, index,
                          constraints[index].left,
                          constraints[index].left_expression)
                    : bound(
                          constraints[index].left,
                          constraints[index].left_expression);
                const auto right = array_definition != nullptr
                    ? array_dimension_bound(
                          *array_definition, index,
                          constraints[index].right,
                          constraints[index].right_expression)
                    : bound(
                          constraints[index].right,
                          constraints[index].right_expression);
                if (left && right) {
                    dimension.minimum = std::min(*left, *right);
                    dimension.maximum = std::max(*left, *right);
                }
                dimension.left = left;
                dimension.right = right;
                dimension.bounds_known = left && right;
                dimension.direction_known
                    = !constraints[index].direction_from_bounds;
                dimension.descending = constraints[index].descending;
                if (!constraints[index].direction_from_bounds
                    && left && right) {
                    dimension.null = constraints[index].null
                        || (constraints[index].descending
                            ? *left < *right
                            : *left > *right);
                    dimension.null_known = true;
                } else if (constraints[index].direction_from_bounds
                    && left && right && *left == *right) {
                    dimension.null = false;
                    dimension.null_known = true;
                } else {
                    dimension.null = false;
                    dimension.null_known = false;
                }
            }
            return;
        }
        if (constraints.empty()) {
            return;
        }
        const auto& constraint = constraints.front();
        if (profile_value.kind == ProfileKind::integer
            || profile_value.kind == ProfileKind::enumeration) {
            profile_value.minimum = bound(
                constraint.left, constraint.left_expression);
            profile_value.maximum = bound(
                constraint.right, constraint.right_expression);
        }
    }

    void report(
        std::string code,
        std::string message,
        const semantic::SourceSpanId source)
    {
        const auto key = code + ":" + std::to_string(source.value());
        if (reported_.insert(key).second) {
            issues_.push_back({
                std::move(code), std::move(message), source });
        }
    }

    bool source_is_within(
        const semantic::SourceSpanId inner,
        const semantic::SourceSpanId outer) const
    {
        if (!inner.valid() || !outer.valid()) {
            return true;
        }
        const auto& spans
            = specialization_.design().semantics.source_spans();
        if (inner.value() >= spans.size() || outer.value() >= spans.size()) {
            return true;
        }
        const auto& inner_span = spans[inner.value()];
        const auto& outer_span = spans[outer.value()];
        return inner_span.file == outer_span.file
            && inner_span.logical_name == outer_span.logical_name
            && inner_span.begin.offset >= outer_span.begin.offset
            && inner_span.end.offset <= outer_span.end.offset;
    }

    void validate_ambiguous_type_mark(
        const SubtypeIndication& subtype,
        const semantic::SourceSpanId fallback,
        const semantic::ScopeId use_scope)
    {
        if (subtype.type_mark.spelling.empty()) {
            return;
        }
        const auto identity
            = semantic::CompiledDesignResolver { specialization_ }
                  .resolve_vhdl_type_identity(
                      subtype.type_mark.spelling,
                      use_scope.valid() ? use_scope
                                        : specialization_.scope());
        if (identity.status
            != semantic::CompiledResolutionStatus::ambiguous) {
            return;
        }
        report(
            "FSIM-ELAB-VHTYPE-006",
            "VHDL type '" + subtype.type_mark.spelling
                + "' is not visible at this declaration",
            subtype.type_mark.source.valid()
                ? subtype.type_mark.source
                : fallback);
    }

    void validate_constraints(
        const SubtypeIndication& original,
        const semantic::SourceSpanId fallback,
        const semantic::ScopeId use_scope = {})
    {
        const auto constraint_scope = use_scope.valid()
            ? use_scope
            : specialization_.scope();
        validate_ambiguous_type_mark(
            original, fallback, constraint_scope);
        // Constraints inherited while resolving a named subtype describe the
        // referenced subtype itself, not a second constraint at this use.
        // Only syntax-owned constraints participate in reconstraining
        // legality; their residual bounds are still evaluated below through
        // the specialization overlay.
        if (original.constraints.empty()) {
            return;
        }
        auto subtype = original;
        std::erase_if(subtype.constraints, [&](const auto& constraint) {
            return constraint.source.valid()
                && fallback.valid()
                && !source_is_within(constraint.source, fallback);
        });
        if (subtype.constraints.empty()) {
            return;
        }
        auto unconstrained = original;
        unconstrained.constraints.clear();
        const auto base = profile(unconstrained, constraint_scope);
        const auto source = subtype.constraints.front().source.valid()
            ? subtype.constraints.front().source
            : fallback;
        if (base.kind == ProfileKind::array) {
            const auto* array_definition = root_definition(unconstrained);
            if (subtype.constraints.size() != base.dimensions.size()) {
                report(
                    "FSIM-ELAB-VHARRAY-008",
                    "a VHDL array subtype constraint has "
                        + std::to_string(subtype.constraints.size())
                        + " dimensions but its base type has "
                        + std::to_string(base.dimensions.size()),
                    source);
                return;
            }
            if (std::ranges::any_of(
                    base.dimensions,
                    &ArrayDimensionProfile::constrained)) {
                report(
                    "FSIM-ELAB-VHSUBTYPE-004",
                    "a constrained VHDL array dimension cannot be "
                    "constrained again",
                    source);
                return;
            }
            for (std::size_t index { };
                index < subtype.constraints.size(); ++index) {
                const auto& constraint = subtype.constraints[index];
                const auto left = array_definition != nullptr
                    ? array_dimension_bound(
                          *array_definition, index,
                          constraint.left, constraint.left_expression)
                    : bound(
                          constraint.left, constraint.left_expression);
                const auto right = array_definition != nullptr
                    ? array_dimension_bound(
                          *array_definition, index,
                          constraint.right, constraint.right_expression)
                    : bound(
                          constraint.right, constraint.right_expression);
                if (!left || !right) {
                    continue;
                }
                const auto& dimension = base.dimensions[index];
                if ((dimension.minimum && *left < *dimension.minimum)
                    || (dimension.maximum && *left > *dimension.maximum)
                    || (dimension.minimum && *right < *dimension.minimum)
                    || (dimension.maximum && *right > *dimension.maximum)) {
                    report(
                        "FSIM-ELAB-VHARRAY-003",
                        "VHDL array constraint lies outside its index "
                        "subtype",
                        constraint.source.valid()
                            ? constraint.source
                            : source);
                }
            }
            return;
        }
        // STRING is an unbounded one-dimensional array of CHARACTER.
        if (base.kind == ProfileKind::string) {
            if (subtype.constraints.size() != 1U) {
                report(
                    "FSIM-ELAB-VHARRAY-008",
                    "a VHDL array subtype constraint has "
                        + std::to_string(subtype.constraints.size())
                        + " dimensions but its base type has 1",
                    source);
            }
            return;
        }
        const auto array_constraint = std::ranges::any_of(
            subtype.constraints, [](const auto& constraint) {
                return constraint.kind == RangeKind::array_index;
            });
        if (array_constraint) {
            report(
                "FSIM-ELAB-VHSUBTYPE-003",
                "a derived VHDL packed index constraint requires an "
                "unconstrained one-dimensional packed-array base",
                source);
            return;
        }
        const auto& constraint = subtype.constraints.front();
        const auto left = bound(
            constraint.left, constraint.left_expression);
        const auto right = bound(
            constraint.right, constraint.right_expression);
        if (!left || !right || !base.minimum || !base.maximum) {
            return;
        }
        const auto base_low = std::min(*base.minimum, *base.maximum);
        const auto base_high = std::max(*base.minimum, *base.maximum);
        const auto derived_low = std::min(*left, *right);
        const auto derived_high = std::max(*left, *right);
        if (derived_low >= base_low && derived_high <= base_high) {
            return;
        }
        if (base.kind == ProfileKind::enumeration) {
            report(
                "FSIM-ELAB-VHENUMRANGE-003",
                "derived VHDL enumeration subtype constraint lies "
                "outside its resolved base subtype range",
                source);
        } else if (base.kind == ProfileKind::integer) {
            report(
                "FSIM-ELAB-VHSUBTYPE-002",
                "derived VHDL integer subtype constraint lies outside "
                "its resolved base subtype range",
                source);
        }
    }

    void collect_subtype(const SubtypeIndication& original)
    {
        const auto subtype = specialized_subtype(original);
        if (const auto id = subtype_type(subtype)) {
            collect_type(*id);
        }
    }

    void collect_type(const semantic::TypeId id)
    {
        if (!id.valid() || !relevant_types_.insert(id.value()).second) {
            return;
        }
        const auto* type = type_definition(id);
        if (type == nullptr) {
            return;
        }
        collect_subtype(type->base);
        if (type->element_subtype) {
            collect_subtype(*type->element_subtype);
        }
        for (const auto& element : type->record_elements) {
            collect_subtype(element.subtype);
        }
    }

    void collect_unit_types(const semantic::vhdl::Unit& unit)
    {
        for (const auto declaration_id : unit.declarations) {
            const auto declaration = specialization_.find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                continue;
            }
            if (declaration->vhdl->declared_type) {
                collect_type(*declaration->vhdl->declared_type);
            }
            if (declaration->vhdl->subtype) {
                collect_subtype(*declaration->vhdl->subtype);
            }
        }
    }

    // IEEE 1076-2008 5.2.1, 5.3.2.1: both bounds of a range have the same
    // type, integer or floating point in a scalar type definition.
    void validate_range_bound_types(
        const std::optional<RangeConstraint>& range,
        const bool scalar_definition, const semantic::SourceSpanId fallback)
    {
        if (!range) {
            return;
        }
        const auto bound_type = [&](const std::optional<std::int64_t> value,
                                    const std::optional<semantic::ExpressionId> id) {
            if (id) {
                return operand_type(*id);
            }
            return value ? OperandType { OperandClass::integer, { } } : OperandType { };
        };
        const auto left = bound_type(range->left, range->left_expression);
        const auto right = bound_type(range->right, range->right_expression);
        if (!known_class(left.kind) || !known_class(right.kind)) {
            return;
        }
        // The bounds of an integer or floating type definition need only
        // be of some integer or floating type (IEEE 1076-1993 3.1.2, 3.1.4).
        const auto mismatch = left.kind != right.kind
            || (!scalar_definition && left.kind == OperandClass::enumeration
                && roots_conflict(left.root, right.root))
            || (scalar_definition && !integer_or_floating_class(left.kind));
        if (mismatch) {
            report("FSIM-ELAB-VHRANGE-001",
                "the bounds of a VHDL range must have the same type"
                    + std::string { scalar_definition
                            ? ", integer or floating point"
                            : "" },
                range->source.valid() ? range->source : fallback);
        }
    }

    // IEEE 1076-2008 5.5.1: the element of a file type is neither a file,
    // access or protected type nor a composite containing one.
    bool contains_indirect(const SubtypeIndication& subtype, std::size_t depth) const
    {
        const auto* definition = root_definition(subtype);
        if (definition == nullptr || depth > 16U) {
            return false;
        }
        switch (definition->form) {
        case TypeForm::file:
        case TypeForm::access:
        case TypeForm::protected_type:
        case TypeForm::protected_body:
            return true;
        case TypeForm::array:
            return definition->element_subtype
                && contains_indirect(*definition->element_subtype, depth + 1U);
        case TypeForm::record:
            return std::ranges::any_of(definition->record_elements,
                [&](const semantic::vhdl::RecordElement& element) {
                    return contains_indirect(element.subtype, depth + 1U);
                });
        default:
            return false;
        }
    }

    // Whether a subtype indication denotes an array subtype without an
    // index constraint (its type mark names an unconstrained array type and
    // no constraint is given here or by a subtype on the way).
    bool unconstrained_array_subtype(const SubtypeIndication& subtype) const
    {
        if (!subtype.constraints.empty()) {
            return false;
        }
        auto type = subtype_type(subtype);
        if (!type) {
            auto name = std::string_view { subtype.type_mark.spelling };
            name = simple_name_of(name);
            return equal_ignoring_ascii_case(name, "string") || equal_ignoring_ascii_case(name, "bit_vector");
        }
        std::unordered_set<std::uint32_t> visiting;
        while (type && visiting.insert(type->value()).second) {
            const auto* definition = type_definition(*type);
            if (definition == nullptr) {
                return false;
            }
            if (definition->form == TypeForm::array) {
                return std::ranges::any_of(definition->array_dimensions,
                    [](const semantic::vhdl::ArrayDimension& dimension) {
                        return dimension.unconstrained;
                    });
            }
            if ((definition->form != TypeForm::subtype
                    && definition->form != TypeForm::alias)
                || !definition->base.constraints.empty()) {
                return false;
            }
            type = subtype_type(definition->base);
        }
        return false;
    }

    // Whether an expression reads an object that is not locally static (a
    // port, signal, variable, file or generic).
    bool reads_nonstatic_object(const semantic::ExpressionId id, const std::size_t depth = 0U) const
    {
        const auto found = specialization_.find_expression(id);
        if (!found || found->vhdl == nullptr || depth > 16U) {
            return false;
        }
        const auto& value = *found->vhdl;
        if (value.kind == semantic::vhdl::ExpressionKind::name && value.referenced_name
            && value.referenced_name->selected) {
            const auto declaration = specialization_.find_declaration(
                *value.referenced_name->selected);
            using Form = semantic::vhdl::DeclarationForm;
            if (declaration && declaration->vhdl != nullptr
                && (declaration->vhdl->form == Form::port || declaration->vhdl->form == Form::signal
                    || declaration->vhdl->form == Form::variable
                    || declaration->vhdl->form == Form::file
                    || declaration->vhdl->form == Form::generic_constant)) {
                return true;
            }
        }
        return std::ranges::any_of(value.operands, [&](const semantic::ExpressionId operand) {
            return reads_nonstatic_object(operand, depth + 1U);
        });
    }

    // Whether `candidate` is declared in the region of `from` or an
    // enclosing one.
    bool type_visible_from(const TypeDefinition& candidate, const TypeDefinition& from) const
    {
        const auto declaration = specialization_.find_declaration(candidate.declaration);
        const auto user = specialization_.find_declaration(from.declaration);
        if (!declaration || declaration->vhdl == nullptr || !user || user->vhdl == nullptr) {
            return false;
        }
        const auto& scopes = specialization_.design().semantics.scopes();
        auto scope = user->vhdl->scope;
        for (std::size_t depth { }; depth < 64U && scope.valid() && scope.value() < scopes.size();
             ++depth) {
            if (scope == declaration->vhdl->scope) {
                return true;
            }
            const auto parent = scopes[scope.value()].parent;
            if (!parent) {
                break;
            }
            scope = *parent;
        }
        return false;
    }

    // Whether an expression reads a port, signal or variable.
    bool reads_signal_or_variable(const semantic::ExpressionId id, const std::size_t depth = 0U) const
    {
        const auto found = specialization_.find_expression(id);
        if (!found || found->vhdl == nullptr || depth > 16U) {
            return false;
        }
        const auto& value = *found->vhdl;
        if (value.kind == semantic::vhdl::ExpressionKind::name && value.referenced_name
            && value.referenced_name->selected) {
            const auto declaration = specialization_.find_declaration(
                *value.referenced_name->selected);
            using Form = semantic::vhdl::DeclarationForm;
            if (declaration && declaration->vhdl != nullptr
                && (declaration->vhdl->form == Form::port || declaration->vhdl->form == Form::signal
                    || declaration->vhdl->form == Form::variable)) {
                return true;
            }
        }
        return std::ranges::any_of(value.operands, [&](const semantic::ExpressionId operand) {
            return reads_signal_or_variable(operand, depth + 1U);
        });
    }

    // Whether a range bound is a type mark rather than an expression.
    bool names_type(const semantic::ExpressionId id) const
    {
        const auto found = specialization_.find_expression(id);
        if (!found || found->vhdl == nullptr
            || found->vhdl->kind != semantic::vhdl::ExpressionKind::name) {
            return false;
        }
        if (!found->vhdl->referenced_name || !found->vhdl->referenced_name->selected) {
            // STD.STANDARD's scalar types are not retained as declarations.
            for (const std::string_view predefined :
                { "integer", "natural", "positive", "boolean", "bit", "character", "real",
                    "time", "severity_level" }) {
                if (equal_ignoring_ascii_case(found->vhdl->text, predefined)) {
                    return true;
                }
            }
            return false;
        }
        const auto declaration = specialization_.find_declaration(
            *found->vhdl->referenced_name->selected);
        return declaration && declaration->vhdl != nullptr
            && (declaration->vhdl->form == semantic::vhdl::DeclarationForm::type
                || declaration->vhdl->form == semantic::vhdl::DeclarationForm::subtype);
    }

    void validate_type_bounds(const TypeDefinition& type)
    {
        const auto static_bound = [&](const std::optional<std::int64_t> value,
                                      const std::optional<semantic::ExpressionId> id)
            -> std::optional<std::int64_t> {
            return value ? value
                : id     ? specialization_.evaluate_integral_expression(*id)
                         : std::nullopt;
        };
        // IEEE 1076-1993 3.1.1, 3.1.2, 3.1.4: the range of an integer,
        // floating or physical type definition is locally static.
        {
            const auto declaration = specialization_.find_declaration(type.declaration);
            const auto* range = type.form == TypeForm::subtype && type.base.constraints.size() == 1U
                ? &type.base.constraints.front()
                : type.scalar_range ? &*type.scalar_range
                                    : nullptr;
            // `type T is range ...` is retained as a subtype declaration of
            // a new semantic type.
            const auto& semantic_types = specialization_.design().semantics.types();
            const auto new_type = declaration && declaration->vhdl != nullptr
                && (declaration->vhdl->form == semantic::vhdl::DeclarationForm::type
                    || (declaration->vhdl->form == semantic::vhdl::DeclarationForm::subtype
                        && type.id.valid() && type.id.value() < semantic_types.size()
                        && semantic_types[type.id.value()].kind
                            == semantic::TypeKind::declaration));
            if (new_type
                && (type.form == TypeForm::subtype || type.form == TypeForm::scalar
                    || type.form == TypeForm::physical)
                && range != nullptr
                && ((range->left_expression && reads_nonstatic_object(*range->left_expression))
                    || (range->right_expression
                        && reads_nonstatic_object(*range->right_expression)))) {
                report("FSIM-ELAB-VHRANGE-004",
                    "the range of a VHDL integer, floating or physical type definition must "
                    "be locally static",
                    range->source.valid() ? range->source : type.source);
            }
        }
        // IEEE 1076-2008 5.3.1: an element of a composite type is not of a
        // file or protected type.
        const auto file_or_protected = [&](const SubtypeIndication& element) {
            const auto* definition = root_definition(element);
            return definition != nullptr
                && (definition->form == TypeForm::file
                    || definition->form == TypeForm::protected_type);
        };
        if ((type.form == TypeForm::array && type.element_subtype
                && file_or_protected(*type.element_subtype))
            || (type.form == TypeForm::record
                && std::ranges::any_of(type.record_elements,
                    [&](const semantic::vhdl::RecordElement& element) {
                        return file_or_protected(element.subtype);
                    }))) {
            report("FSIM-ELAB-VHFILE-014",
                "an element of a VHDL composite type cannot be of a file or "
                "protected type",
                type.source);
        }
        // IEEE 1076-2008 5.4.1: an access type does not designate a file
        // type; 5.5.1: a file element is not a multidimensional array.
        if (type.form == TypeForm::access && type.designated_subtype) {
            const auto* designated = root_definition(*type.designated_subtype);
            if (designated != nullptr && designated->form == TypeForm::file) {
                report("FSIM-ELAB-VHACCESS-032",
                    "a VHDL access type cannot designate a file type", type.source);
            }
        }
        if (type.form == TypeForm::file && type.element_subtype) {
            const auto* element = root_definition(*type.element_subtype);
            if (element != nullptr && element->form == TypeForm::array
                && element->array_dimensions.size() > 1U) {
                report("FSIM-ELAB-VHFILE-015",
                    "the element of a VHDL file type cannot be a multidimensional "
                    "array",
                    type.source);
            }
        }
        // IEEE 1076-1993 3.2.1.1, 3.2.2: an element of an array or record is
        // of a constrained subtype before VHDL-2008.
        if (architecture_.standard < std::string_view { "2008" }) {
            const auto unconstrained = [&](const SubtypeIndication& element) {
                return unconstrained_array_subtype(element);
            };
            if ((type.form == TypeForm::array && type.element_subtype
                    && unconstrained(*type.element_subtype))
                || (type.form == TypeForm::record
                    && std::ranges::any_of(type.record_elements,
                        [&](const semantic::vhdl::RecordElement& element) {
                            return unconstrained(element.subtype);
                        }))) {
                report("FSIM-ELAB-VHARRAY-009",
                    "an element of a VHDL composite type must have a constrained "
                    "subtype before VHDL-2008",
                    type.source);
            }
        }
        if (type.form == TypeForm::file && type.element_subtype
            && contains_indirect(*type.element_subtype, 0U)) {
            report("FSIM-ELAB-VHFILE-013",
                "the element of a VHDL file type cannot be a file, access or "
                "protected type, or contain one",
                type.source);
        }
        if (type.form == TypeForm::scalar) {
            validate_range_bound_types(type.scalar_range, true, type.source);
        }
        if ((type.form == TypeForm::subtype || type.form == TypeForm::alias)
            && type.base.constraints.size() == 1U) {
            validate_range_bound_types(
                type.base.constraints.front(), false, type.source);
            // A range of an integer or floating base has numeric bounds
            // (`type t is range false to true` is kept as an INTEGER
            // subtype).
            auto unconstrained = type.base;
            unconstrained.constraints.clear();
            const auto base = subtype_operand_type(unconstrained, 0U);
            const auto& range = type.base.constraints.front();
            const auto non_numeric = [&](const std::optional<semantic::ExpressionId> id) {
                if (!id) {
                    return false;
                }
                const auto kind = operand_type(*id).kind;
                if (kind == OperandClass::enumeration || kind == OperandClass::array
                    || kind == OperandClass::record || kind == OperandClass::access) {
                    return true;
                }
                // A relational or logical operation yields BOOLEAN or BIT.
                const auto found = specialization_.find_expression(*id);
                if (!found || found->vhdl == nullptr
                    || found->vhdl->kind != semantic::vhdl::ExpressionKind::binary) {
                    return false;
                }
                const auto& operation = found->vhdl->text;
                return operation == "=" || operation == "/=" || operation == "<"
                    || operation == "<=" || operation == ">" || operation == ">="
                    || operation == "and" || operation == "or" || operation == "nand"
                    || operation == "nor" || operation == "xor" || operation == "xnor";
            };
            if (integer_or_floating_class(base.kind)
                && (non_numeric(range.left_expression)
                    || non_numeric(range.right_expression))) {
                report("FSIM-ELAB-VHRANGE-001",
                    "the bounds of a VHDL range must have the same type, "
                    "integer or floating point",
                    range.source.valid() ? range.source : type.source);
            }
        }
        if (type.form == TypeForm::array) {
            for (const auto& dimension : type.array_dimensions) {
                validate_range_bound_types(
                    dimension.constraint, false, dimension.source);
                if (dimension.constraint) {
                    const auto& range = *dimension.constraint;
                    // An anonymous discrete range (`array (a to b)`) carries a
                    // placeholder index subtype spanning its left bound.
                    const auto implicit_index = [&] {
                        const auto left = range.left_expression
                            ? specialization_.find_expression(*range.left_expression)
                            : std::nullopt;
                        const auto& spans = specialization_.design().semantics.source_spans();
                        const auto index_source = dimension.index_subtype.source;
                        if (!left || left->vhdl == nullptr || !index_source.valid()
                            || !left->vhdl->source.valid() || index_source.value() >= spans.size()
                            || left->vhdl->source.value() >= spans.size()) {
                            return !range.left_expression;
                        }
                        const auto& a = spans[index_source.value()];
                        const auto& b = spans[left->vhdl->source.value()];
                        return a.begin.line == b.begin.line && a.begin.column == b.begin.column
                            && a.end.line == b.end.line && a.end.column == b.end.column;
                    }();
                    // IEEE 1076-2008 5.3.2.2: the type of an anonymous
                    // discrete range is determined from its bounds alone.
                    const auto literal = [&](const std::optional<semantic::ExpressionId> id)
                        -> std::string {
                        const auto found = id ? specialization_.find_expression(*id) : std::nullopt;
                        return found && found->vhdl != nullptr
                                && found->vhdl->kind == semantic::vhdl::ExpressionKind::name
                            ? found->vhdl->text
                            : std::string { };
                    };
                    const auto left_literal = literal(range.left_expression);
                    const auto right_literal = literal(range.right_expression);
                    if (implicit_index && !left_literal.empty()
                        && !right_literal.empty()) {
                        std::size_t owners { };
                        for (const auto& candidate : specialization_.design().vhdl_hir.types()) {
                            const auto has = [&](const std::string& spelling) {
                                return std::ranges::any_of(candidate.enumeration_literals,
                                    [&](const auto& entry) {
                                        return equal_ignoring_ascii_case(entry.spelling, spelling);
                                    });
                            };
                            if (has(left_literal) && has(right_literal)
                                && type_visible_from(candidate, type)) {
                                ++owners;
                            }
                        }
                        if (owners > 1U) {
                            report("FSIM-ELAB-VHRANGE-001",
                                "the type of a VHDL discrete range is ambiguous",
                                range.source.valid() ? range.source : type.source);
                        }
                    }
                    const auto source = range.source.valid() ? range.source
                        : dimension.source.valid()          ? dimension.source
                                                            : type.source;
                    if ((range.left_expression && names_type(*range.left_expression))
                        || (range.right_expression && names_type(*range.right_expression))) {
                        report("FSIM-ELAB-VHRANGE-005",
                            "a VHDL range bound is an expression, not a type mark", source);
                    }
                    // The bounds of an index constraint have the index type.
                    auto index_name = implicit_index
                        ? std::string_view { }
                        : std::string_view { dimension.index_subtype.spelling };
                    index_name.remove_prefix(std::min(index_name.size(),
                        index_name.find_last_of(".:") + 1U));
                    // (An anonymous range's placeholder index is INTEGER, so
                    // INTEGER itself is not checked.)
                    const auto integer_index = equal_ignoring_ascii_case(index_name, "natural")
                        || equal_ignoring_ascii_case(index_name, "positive");
                    const auto enumeration_index = equal_ignoring_ascii_case(index_name, "character")
                        || equal_ignoring_ascii_case(index_name, "boolean")
                        || equal_ignoring_ascii_case(index_name, "bit");
                    const auto bound_kind = [&](const std::optional<semantic::ExpressionId> id) {
                        return id ? operand_type(*id).kind : OperandClass::unknown;
                    };
                    for (const auto kind :
                        { bound_kind(range.left_expression), bound_kind(range.right_expression) }) {
                        if ((integer_index && kind == OperandClass::enumeration)
                            || (enumeration_index && kind == OperandClass::integer)) {
                            report("FSIM-ELAB-VHRANGE-001",
                                "the bounds of a VHDL index constraint must have the index type",
                                source);
                            break;
                        }
                    }
                    const std::optional<std::int64_t> minimum
                        = equal_ignoring_ascii_case(index_name, "natural") ? std::optional<std::int64_t> { 0 }
                        : equal_ignoring_ascii_case(index_name, "positive")
                        ? std::optional<std::int64_t> { 1 }
                        : std::nullopt;
                    const auto left = static_bound(range.left, range.left_expression);
                    const auto right = static_bound(range.right, range.right_expression);
                    if (minimum && left && right
                        && !(range.descending ? *left < *right : *left > *right)
                        && std::min(*left, *right) < *minimum) {
                        report("FSIM-ELAB-VHARRAY-011",
                            "an index constraint bound of VHDL array type '" + type.name
                                + "' is outside the index subtype",
                            source);
                    }
                }
                // An index subtype is discrete (IEEE 1076-1993 3.2.1).
                const auto index_declaration = dimension.index_subtype.selected
                    ? specialization_.find_declaration(*dimension.index_subtype.selected)
                    : std::nullopt;
                const auto index_mark_source
                    = dimension.index_subtype.source.valid()
                    ? dimension.index_subtype.source
                    : dimension.source.valid() ? dimension.source
                                               : type.source;
                // An object alias retains its object's subtype for value
                // operations, but its identifier does not denote that type
                // when used as an array index type mark (IEEE 1076-2008
                // 6.6.2, 12.3). Type and subtype declarations, including
                // TypeForm::alias, have different declaration forms.
                if (index_declaration && index_declaration->vhdl != nullptr
                    && index_declaration->vhdl->form
                        == semantic::vhdl::DeclarationForm::alias) {
                    report(
                        "FSIM-ELAB-VHTYPE-006",
                        "VHDL type mark '" + dimension.index_subtype.spelling
                            + "' denotes an object alias, not a type",
                        index_mark_source);
                    continue;
                }
                if (!index_declaration) {
                    auto spelling = std::string_view { dimension.index_subtype.spelling };
                    spelling = simple_name_of(spelling);
                    for (const auto predefined : { "bit_vector", "string", "real", "time",
                             "std_logic_vector", "std_ulogic_vector" }) {
                        if (equal_ignoring_ascii_case(spelling, predefined)) {
                            report("FSIM-ELAB-VHARRAY-012",
                                "a VHDL array index subtype must be discrete",
                                dimension.source.valid() ? dimension.source : type.source);
                            break;
                        }
                    }
                }
                if (index_declaration && index_declaration->vhdl != nullptr
                    && index_declaration->vhdl->declared_type) {
                    SubtypeIndication index;
                    index.type_mark.spelling = dimension.index_subtype.spelling;
                    index.type_mark.target = *index_declaration->vhdl->declared_type;
                    const auto kind = subtype_operand_type(index, 0U).kind;
                    if (kind == OperandClass::array || kind == OperandClass::record
                        || kind == OperandClass::floating) {
                        report("FSIM-ELAB-VHARRAY-012",
                            "a VHDL array index subtype must be discrete",
                            dimension.source.valid() ? dimension.source : type.source);
                    }
                }
            }
        }
    }

    void validate_type_definition(const TypeDefinition& type)
    {
        if (type.form == TypeForm::subtype
            || type.form == TypeForm::alias) {
            validate_constraints(
                type.base, type.source,
                type_declaration_scope(type));
        }
        // IEEE 1076-2008 4.6: a resolution function returns a value of the
        // resolved subtype's type.
        if (type.form == TypeForm::subtype && !type.base.resolution_function.spelling.empty()) {
            const semantic::CompiledDesignResolver resolver { specialization_ };
            const auto declaration = specialization_.find_declaration(type.declaration);
            const auto candidates = declaration && declaration->vhdl != nullptr
                ? resolver.resolve_vhdl(type.base.resolution_function,
                      declaration->vhdl->scope).candidates
                : std::vector<semantic::DeclarationId> { };
            auto resolved = type.base;
            resolved.resolution_function = { };
            const auto expected = subtype_operand_type(resolved, 0U);
            const auto mismatched = [&](const semantic::DeclarationId id) {
                const auto function = specialization_.find_declaration(id);
                if (!function || function->vhdl == nullptr || !function->vhdl->callable
                    || !function->vhdl->callable->function
                    || !function->vhdl->callable->return_type) {
                    return false;
                }
                const auto returned = subtype_operand_type(*function->vhdl->callable->return_type, 0U);
                return returned.kind != OperandClass::unknown
                    && expected.kind != OperandClass::unknown
                    && types_conflict(returned, expected);
            };
            if (!candidates.empty() && std::ranges::all_of(candidates, mismatched)) {
                report("FSIM-ELAB-VHRESOLVE-002",
                    "VHDL resolution function '" + type.base.resolution_function.spelling
                        + "' does not return the type of subtype '" + type.name + "'",
                    type.source);
            }
        }
        validate_type_bounds(type);
        // IEEE 1076-1993 3.2.2: a name that denotes a record element is not
        // allowed within the record type definition that declares it.
        if (type.form == TypeForm::record) {
            for (std::size_t index = 0; index < type.record_elements.size(); ++index) {
                const auto& mark = type.record_elements[index].subtype.type_mark;
                if (mark.spelling.empty()
                    || mark.spelling.find('.') != std::string::npos) {
                    continue;
                }
                for (std::size_t earlier = 0; earlier < index; ++earlier) {
                    if (equal_ignoring_ascii_case(type.record_elements[earlier].name, mark.spelling)) {
                        report("FSIM-ELAB-VHRECORD-003",
                            "record element name '" + mark.spelling
                                + "' cannot be used within the record type definition",
                            mark.source.valid() ? mark.source : type.source);
                        break;
                    }
                }
            }
        }
        if (type.form != TypeForm::array) {
            return;
        }
        if (!type.element_subtype) {
            report(
                "FSIM-ELAB-VHARRAY-001",
                "VHDL array type '" + type.name
                    + "' does not retain a concrete element subtype",
                type.source);
            return;
        }
        const auto element = profile(*type.element_subtype);
        // VHDL-2008 permits an unbounded element subtype that object
        // declarations constrain (IEEE 1076-2008 5.3.2.1).
        const bool unbounded_elements
            = architecture_.standard >= std::string_view { "2008" };
        // A STRING element is bounded by its own index constraint or by
        // that of the subtype it names.
        const auto constrained_string = [&] {
            const SubtypeIndication* current = &*type.element_subtype;
            std::unordered_set<std::uint32_t> seen;
            while (current != nullptr) {
                if (!current->constraints.empty()) {
                    return true;
                }
                const auto id = subtype_type(*current);
                const auto* definition = id ? type_definition(*id) : nullptr;
                if (definition == nullptr
                    || !seen.insert(definition->id.value()).second
                    || (definition->form != TypeForm::subtype
                        && definition->form != TypeForm::alias)) {
                    return false;
                }
                current = &definition->base;
            }
            return false;
        };
        if (element.kind == ProfileKind::unknown
            || (element.kind == ProfileKind::string && !unbounded_elements
                && !constrained_string())
            || (element.kind == ProfileKind::array && !unbounded_elements
                && std::ranges::any_of(
                    element.dimensions,
                    [](const auto& dimension) {
                        return !dimension.constrained;
                    }))) {
            report(
                "FSIM-ELAB-VHARRAY-001",
                "VHDL array type '" + type.name
                    + "' requires a concrete bounded scalar or packed "
                    "composite element subtype",
                type.element_subtype->type_mark.source.valid()
                    ? type.element_subtype->type_mark.source
                    : type.source);
        }
    }

    void validate_object(
        const semantic::vhdl::Declaration& declaration)
    {
        if (!declaration.subtype) {
            return;
        }
        const auto subtype = specialized_subtype(*declaration.subtype);
        // Legality belongs to the syntax-owned subtype indication.  The
        // effective subtype deliberately inherits constraints from named
        // subtypes and generic-type actuals; validating that flattened view
        // makes every object of a constrained subtype look like an illegal
        // reconstraining declaration.  Profile the effective view below for
        // executable-layout checks, but retain the original indication for
        // the reconstraining decision.
        validate_constraints(
            *declaration.subtype, declaration.source,
            declaration.scope);
        const auto object = profile(subtype, declaration.scope);
        if (object.kind != ProfileKind::array) {
            return;
        }
        // An unconstrained interface array acquires its bounds from the
        // associated actual at each occurrence.  It is therefore legal (and
        // intentionally not concrete yet) in the unit specialization.  The
        // hierarchy boundary validates the actual and the lowerer consumes
        // the aliased signal's concrete array metadata.
        if (declaration.form
            == semantic::vhdl::DeclarationForm::port) {
            return;
        }
        // An unconstrained constant takes its bounds from its value (IEEE
        // 1076-2008 5.3.2.2).
        if (declaration.form == semantic::vhdl::DeclarationForm::constant
            && declaration.initializer) {
            return;
        }
        const auto unconstrained = object.dimensions.empty()
            || std::ranges::any_of(
                object.dimensions,
                [](const auto& dimension) {
                    return !dimension.constrained;
                });
        if (unconstrained) {
            report(
                "FSIM-ELAB-VHARRAY-005",
                "VHDL object '" + declaration.name
                    + "' requires a concrete array subtype",
                declaration.source);
        }
    }

    // IEEE 1076-1993 5.1: an attribute specification names entities of its
    // entity class declared in the same declarative part by simple names;
    // `others` and `all` come last for an attribute and class; the value
    // has the attribute's type.
    void validate_attribute_specifications()
    {
        using Form = semantic::vhdl::DeclarationForm;
        const auto& scopes = specialization_.design().semantics.scopes();
        const auto& declarations
            = specialization_.design().vhdl_hir.declarations();
        const auto visible_in = [&](const semantic::ScopeId inner,
                                    const semantic::ScopeId outer) {
            return semantic::scope_distance(
                specialization_.design().semantics, inner, outer)
                .has_value();
        };
        const auto class_forms = [](const std::string_view entity_class)
            -> std::vector<Form> {
            if (equal_ignoring_ascii_case(entity_class, "signal")) {
                return { Form::signal, Form::port };
            }
            if (equal_ignoring_ascii_case(entity_class, "variable")) {
                return { Form::variable };
            }
            if (equal_ignoring_ascii_case(entity_class, "constant")) {
                return { Form::constant, Form::generic_constant };
            }
            if (equal_ignoring_ascii_case(entity_class, "type")) {
                return { Form::type };
            }
            if (equal_ignoring_ascii_case(entity_class, "subtype")) {
                return { Form::subtype };
            }
            return { };
        };
        std::set<std::tuple<std::uint32_t, std::string, std::string>>
            closed;
        const auto local_scope = [&](const semantic::ScopeId scope) {
            return scope.valid() && scope.value() < scopes.size()
                && (scopes[scope.value()].unit == entity_.id
                    || scopes[scope.value()].unit == architecture_.id);
        };
        for (const auto& declaration : declarations) {
            if (declaration.form != Form::attribute_specification
                || !declaration.attribute || !local_scope(declaration.scope)) {
                continue;
            }
            const auto& profile = *declaration.attribute;
            std::string attribute_name { declaration.name };
            std::string entity_class { profile.entity_class };
            for (auto* text : { &attribute_name, &entity_class }) {
                std::ranges::transform(*text, text->begin(),
                    [](const unsigned char c) {
                        return static_cast<char>(std::tolower(c));
                    });
            }
            const auto key = std::tuple { declaration.scope.value(),
                attribute_name, entity_class };
            const auto forms = class_forms(entity_class);
            for (const auto& entity : profile.entity_names) {
                const auto& spelling = entity.spelling;
                if (equal_ignoring_ascii_case(spelling, "others")
                    || equal_ignoring_ascii_case(spelling, "all")) {
                    closed.insert(key);
                    continue;
                }
                if (closed.contains(key)) {
                    report("FSIM-ELAB-VHATTR-016",
                        "an attribute specification for '" + spelling
                            + "' follows one for others or all",
                        declaration.source);
                    continue;
                }
                if (forms.empty()) {
                    continue;
                }
                if (spelling.find('.') != std::string::npos) {
                    report("FSIM-ELAB-VHATTR-016",
                        "an attribute specification names entities by "
                        "simple names",
                        declaration.source);
                    continue;
                }
                bool declared_here { };
                bool declared_later { };
                bool class_match { };
                bool visible { };
                const auto& spans = specialization_.design().semantics.source_spans();
                const auto follows = [&](const semantic::SourceSpanId later,
                                         const semantic::SourceSpanId earlier) {
                    if (!later.valid() || !earlier.valid() || later.value() >= spans.size()
                        || earlier.value() >= spans.size()
                        || spans[later.value()].file != spans[earlier.value()].file) {
                        return false;
                    }
                    const auto& a = spans[later.value()].begin;
                    const auto& b = spans[earlier.value()].begin;
                    return a.line > b.line || (a.line == b.line && a.column > b.column);
                };
                for (const auto& candidate : declarations) {
                    if (!equal_ignoring_ascii_case(candidate.name, spelling)) {
                        continue;
                    }
                    if (candidate.scope == declaration.scope
                        && candidate.form != Form::attribute_specification
                        && follows(candidate.source, declaration.source)) {
                        declared_later = true;
                        continue;
                    }
                    if (candidate.scope == declaration.scope) {
                        declared_here = true;
                        // A subprogram formal's class follows its mode and
                        // explicit class, which the HIR form does not keep.
                        class_match = class_match
                            || std::ranges::find(forms, candidate.form)
                                != forms.end()
                            || subprogram_formal(candidate.id);
                    }
                    visible = visible
                        || visible_in(declaration.scope, candidate.scope);
                }
                if (!declared_here && declared_later) {
                    // IEEE 1076-1993 5.1: the named entity is declared
                    // before the attribute specification.
                    report("FSIM-ELAB-VHATTR-016",
                        "attribute specification names '" + spelling
                            + "' before its declaration",
                        declaration.source);
                } else if (declared_here && !class_match) {
                    report("FSIM-ELAB-VHATTR-016",
                        "'" + spelling + "' is not of entity class "
                            + entity_class,
                        declaration.source);
                } else if (!declared_here && !visible) {
                    report("FSIM-ELAB-VHATTR-016",
                        "attribute specification names undeclared '"
                            + spelling + "'",
                        declaration.source);
                }
            }
            if (!profile.value) {
                continue;
            }
            const auto value = specialization_.find_expression(*profile.value);
            semantic::vhdl::Name attribute_reference;
            attribute_reference.spelling = declaration.name;
            attribute_reference.canonical = declaration.name;
            const semantic::CompiledDeclarationPredicate attribute_declaration
                = [](const semantic::CompiledDeclarationView& candidate) {
                      return candidate.vhdl != nullptr
                          && candidate.vhdl->form
                              == Form::attribute_declaration;
                  };
            const auto resolved_attribute
                = semantic::CompiledDesignResolver { specialization_ }
                      .resolve_vhdl(attribute_reference, declaration.scope,
                          attribute_declaration)
                      .unique();
            const auto resolved_view = resolved_attribute
                ? specialization_.find_declaration(*resolved_attribute)
                : std::nullopt;
            const semantic::vhdl::Declaration* attribute
                = resolved_view && resolved_view->vhdl != nullptr
                    ? resolved_view->vhdl
                    : nullptr;
            if (!value || value->vhdl == nullptr
                || attribute == nullptr || !attribute->attribute
                || !attribute->attribute->subtype) {
                continue;
            }
            const auto expected
                = subtype_operand_type(*attribute->attribute->subtype, 0U).kind;
            using Kind = semantic::vhdl::ExpressionKind;
            if (value->vhdl->kind == Kind::aggregate) {
                validate_aggregate(
                    *profile.value, *attribute->attribute->subtype);
            }
            if ((expected == OperandClass::floating
                    && value->vhdl->kind == Kind::integer_literal)
                || (expected == OperandClass::integer
                    && value->vhdl->kind == Kind::real_literal)) {
                report("FSIM-ELAB-VHATTR-016",
                    "the value of attribute '" + declaration.name
                        + "' does not have the attribute's type",
                    value->vhdl->source);
            }
        }
    }

    void validate_unit_objects(const semantic::vhdl::Unit& unit)
    {
        for (const auto declaration_id : unit.declarations) {
            const auto declaration = specialization_.find_declaration(
                declaration_id);
            if (!declaration || declaration->vhdl == nullptr) {
                continue;
            }
            using Form = semantic::vhdl::DeclarationForm;
            const auto form = declaration->vhdl->form;
            if (form == Form::port || form == Form::signal
                || form == Form::constant || form == Form::variable) {
                validate_object(*declaration->vhdl);
            }
        }
    }

    void validate_actual_types()
    {
        for (const auto& actual :
            specialization_.specialization().actual_identities) {
            if (!actual.vhdl_type) {
                continue;
            }
            collect_subtype(*actual.vhdl_type);
            const auto actual_expression = actual.actual_expression
                ? specialization_.find_expression(
                      *actual.actual_expression)
                : std::nullopt;
            const auto actual_source = actual.source.valid()
                ? actual.source
                : actual_expression
                    && actual_expression->vhdl != nullptr
                ? actual_expression->vhdl->source
                : actual.vhdl_type->type_mark.source;
            validate_constraints(
                *actual.vhdl_type,
                actual_source,
                specialization_.scope());
        }
    }

    std::optional<semantic::TypeId> array_root(
        const semantic::TypeId id) const
    {
        const auto* root = root_type_definition(id);
        return root != nullptr && root->form == TypeForm::array
            ? std::optional { root->id }
            : std::nullopt;
    }

    std::optional<semantic::TypeId> expression_array_root(
        const semantic::ExpressionId id) const
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
        }
        if (expression->vhdl->kind == semantic::vhdl::ExpressionKind::unary
            || expression->vhdl->kind == semantic::vhdl::ExpressionKind::binary) {
            const auto subtype = operator_result_subtype(*expression->vhdl);
            const auto type = subtype ? subtype_type(*subtype) : std::nullopt;
            return type ? array_root(*type) : std::nullopt;
        }
        constexpr std::string_view user_attribute_marker {
            "@fsim-vhdl-user-attribute:"
        };
        if (expression->vhdl->nominal_type.starts_with(
                user_attribute_marker)) {
            const auto subtype = expression_subtype(id);
            const auto type = subtype ? subtype_type(*subtype) : std::nullopt;
            return type ? array_root(*type) : std::nullopt;
        }
        if (expression->vhdl->kind == semantic::vhdl::ExpressionKind::index
            || expression->vhdl->kind
                == semantic::vhdl::ExpressionKind::slice
            || (expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && expression->vhdl->text.starts_with("@vhdl-member:"))) {
            if (const auto subtype = expression_subtype(id)) {
                const auto type = subtype_type(*subtype);
                if (type) {
                    return array_root(*type);
                }
            }
        }
        if (!expression->vhdl->nominal_type.empty()) {
            const auto nominal = named_type(
                expression->vhdl->nominal_type,
                expression->vhdl->scope);
            if (nominal) {
                return array_root(*nominal);
            }
        }
        if (!expression->vhdl->referenced_name
            || !expression->vhdl->referenced_name->selected) {
            return std::nullopt;
        }
        const auto declaration = specialization_.find_declaration(
            *expression->vhdl->referenced_name->selected);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            return std::nullopt;
        }
        // An indexed name of an object denotes an element, whose type is
        // the element subtype once every dimension is selected.
        // A discrete range operand (`b(1 to 2)`) makes the name a slice,
        // which keeps the prefix's type.
        const auto slice_operand = std::ranges::any_of(
            expression->vhdl->operands,
            [&](const semantic::ExpressionId operand) {
                const auto selector = specialization_.find_expression(operand);
                return selector && selector->vhdl != nullptr
                    && ((selector->vhdl->kind
                                == semantic::vhdl::ExpressionKind::binary
                            && (equal_ignoring_ascii_case(selector->vhdl->text, "to")
                                || equal_ignoring_ascii_case(selector->vhdl->text, "downto")))
                        || selector->vhdl->text.ends_with("'range")
                        || selector->vhdl->text.ends_with("'reverse_range"));
            });
        using Form = semantic::vhdl::DeclarationForm;
        const auto object
            = semantic::vhdl::is_value_object_form(declaration->vhdl->form)
            || declaration->vhdl->form == Form::alias;
        if ((expression->vhdl->kind == semantic::vhdl::ExpressionKind::index
                || (expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::call
                    && object && !declaration->vhdl->callable))
            && !expression->vhdl->operands.empty() && !slice_operand) {
            const auto selected = expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::index
                ? expression_subtype(id)
                : indexed_subtype(
                      specialized_subtype(*declaration->vhdl->subtype),
                      expression->vhdl->operands.size());
            const auto type = selected ? subtype_type(*selected)
                                       : std::nullopt;
            return type ? array_root(*type) : std::nullopt;
        }
        const auto type = subtype_type(
            specialized_subtype(*declaration->vhdl->subtype));
        return type ? array_root(*type) : std::nullopt;
    }

    std::optional<SubtypeIndication> function_result_subtype(
        const semantic::vhdl::Expression& expression,
        bool* matching_callable = nullptr) const
    {
        if (matching_callable != nullptr) {
            *matching_callable = false;
        }
        const bool operator_expression
            = expression.kind == semantic::vhdl::ExpressionKind::unary
            || expression.kind == semantic::vhdl::ExpressionKind::binary;
        if ((expression.kind != semantic::vhdl::ExpressionKind::call
                && !operator_expression)
            || !expression.referenced_name
            || expression.text.starts_with('@')
            || expression.text.find('\'') != std::string::npos) {
            return std::nullopt;
        }
        const semantic::CompiledDesignResolver resolver { specialization_ };
        const auto candidates = resolver.resolve_vhdl_callables(
            *expression.referenced_name, expression.scope).candidates;
        std::vector<std::pair<std::string, semantic::ExpressionId>> actuals;
        actuals.reserve(expression.operands.size());
        for (std::size_t index { }; index < expression.operands.size(); ++index) {
            actuals.emplace_back(index < expression.argument_names.size()
                    ? expression.argument_names[index]
                    : std::string { },
                expression.operands[index]);
        }
        std::optional<semantic::DeclarationId> matched;
        for (const auto& candidate : candidates) {
            const auto declaration = specialization_.find_declaration(
                candidate.body);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->callable
                || !declaration->vhdl->callable->function
                || !declaration->vhdl->callable->return_type
                || !callable_accepts(candidate.body, actuals)) {
                continue;
            }
            if (operator_expression) {
                const auto& formals = declaration->vhdl->callable->formals;
                if (formals.size() != expression.operands.size()) {
                    continue;
                }
                bool compatible = true;
                for (std::size_t index { }; index < formals.size(); ++index) {
                    const auto formal = specialization_.find_declaration(
                        formals[index]);
                    if (!formal || formal->vhdl == nullptr
                        || !formal->vhdl->subtype) {
                        compatible = false;
                        break;
                    }
                    const auto expected = subtype_operand_type(
                        *formal->vhdl->subtype, 0U);
                    const auto actual_subtype = expression_subtype(
                        expression.operands[index]);
                    const auto given = actual_subtype
                        ? subtype_operand_type(*actual_subtype, 0U)
                        : operand_type(expression.operands[index]);
                    if (expected.kind != OperandClass::unknown
                        && given.kind != OperandClass::unknown
                        && types_conflict(expected, given)) {
                        compatible = false;
                        break;
                    }
                }
                if (!compatible) {
                    continue;
                }
            }
            if (matching_callable != nullptr) {
                *matching_callable = true;
            }
            if (matched && *matched != candidate.body) {
                return std::nullopt;
            }
            matched = candidate.body;
        }
        if (!matched) {
            return std::nullopt;
        }
        const auto declaration = specialization_.find_declaration(*matched);
        return declaration && declaration->vhdl != nullptr
                && declaration->vhdl->callable
                && declaration->vhdl->callable->return_type
            ? std::optional { specialized_subtype(
                  *declaration->vhdl->callable->return_type) }
            : std::nullopt;
    }

    std::optional<SubtypeIndication> operator_result_subtype(
        const semantic::vhdl::Expression& expression) const
    {
        bool matching_callable { };
        const auto result = function_result_subtype(
            expression, &matching_callable);
        if (result || matching_callable) {
            return result;
        }
        // IEEE 1076-2008 9.2.2: predefined logical array results retain
        // the array operand's type and range (the left array for two arrays).
        // An inapplicable visible overload does not remove that result type.
        const auto& operation = expression.text;
        const bool unary_not = expression.kind
                == semantic::vhdl::ExpressionKind::unary
            && operation == "not" && expression.operands.size() == 1U;
        const bool binary_logical = expression.kind
                == semantic::vhdl::ExpressionKind::binary
            && expression.operands.size() == 2U
            && (operation == "and" || operation == "or"
                || operation == "nand" || operation == "nor"
                || operation == "xor" || operation == "xnor");
        if (!unary_not && !binary_logical) {
            return std::nullopt;
        }
        for (const auto operand : expression.operands) {
            const auto subtype = expression_subtype(operand);
            const auto element = subtype
                ? array_element_subtype(*subtype) : std::nullopt;
            if (!subtype || !element) {
                continue;
            }
            const auto shape = profile(*subtype);
            const auto element_type = subtype_operand_type(*element, 0U);
            if (shape.kind != ProfileKind::array
                || shape.dimensions.size() != 1U
                || element_type.kind != OperandClass::enumeration
                || (element_type.root != "bit"
                    && element_type.root != "boolean")) {
                continue;
            }
            const auto array_type = subtype_operand_type(*subtype, 0U);
            const bool compatible = std::ranges::all_of(
                expression.operands, [&](const auto other) {
                    const auto other_subtype = expression_subtype(other);
                    const auto type = other_subtype
                        ? subtype_operand_type(*other_subtype, 0U)
                        : operand_type(other);
                    return !types_conflict(type, array_type)
                        || !types_conflict(type, element_type);
                });
            return compatible ? subtype : std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<SubtypeIndication> name_object_subtype(
        const semantic::vhdl::Expression& value) const
    {
        if (value.kind != semantic::vhdl::ExpressionKind::name
            || value.text.find('\'') != std::string::npos) {
            return std::nullopt;
        }

        using Form = semantic::vhdl::DeclarationForm;
        const semantic::CompiledDeclarationPredicate object_declaration
            = [](const semantic::CompiledDeclarationView& candidate) {
                  if (candidate.vhdl == nullptr) {
                      return false;
                  }
                  const auto form = candidate.vhdl->form;
                  return semantic::vhdl::is_value_object_form(form)
                      || form == Form::alias || form == Form::file;
              };
        const auto same_identifier = [](const std::string_view left,
                                         const std::string_view right) {
            const auto extended = [](const std::string_view name) {
                return support::is_extended_vhdl_identifier(name);
            };
            return extended(left) || extended(right)
                ? left == right
                : equal_ignoring_ascii_case(left, right);
        };
        struct NamePart {
            std::string_view spelling;
        };
        std::vector<NamePart> parts;
        const auto text = std::string_view { value.text };
        bool in_extended_identifier { };
        std::size_t begin { };
        for (std::size_t index { }; index < text.size(); ++index) {
            if (text[index] == '\\') {
                in_extended_identifier = !in_extended_identifier;
            } else if (text[index] == '.' && !in_extended_identifier) {
                parts.push_back({ text.substr(begin, index - begin) });
                begin = index + 1U;
            }
        }
        if (begin < text.size()) {
            parts.push_back({ text.substr(begin) });
        }
        if (parts.empty()) {
            return std::nullopt;
        }

        const auto resolver = semantic::CompiledDesignResolver {
            specialization_ };
        const auto resolve_object = [&](const std::string_view spelling) {
            semantic::vhdl::Name name;
            name.spelling = std::string { spelling };
            name.canonical = name.spelling;
            return resolver.resolve_vhdl(name, value.scope,
                object_declaration).unique();
        };
        auto selected_object = std::optional<semantic::DeclarationId> { };
        if (value.referenced_name && value.referenced_name->selected) {
            const auto selected = *value.referenced_name->selected;
            const auto declaration = specialization_.find_declaration(selected);
            if (declaration && object_declaration(*declaration)) {
                selected_object = selected;
            }
        }
        if (!selected_object && parts.size() == 1U) {
            selected_object = value.referenced_name
                ? resolver.resolve_vhdl(*value.referenced_name,
                      value.scope, object_declaration).unique()
                : resolve_object(parts.front().spelling);
        }
        if (!selected_object && parts.size() > 1U) {
            // Resolve the declaration prefix before interpreting later
            // components as record elements. Prefer an explicit package
            // prefix when it names a compiled package; otherwise use the
            // first component as the selected object.
            const auto& units = specialization_.design().vhdl_units();
            std::optional<std::size_t> prefix_size;
            if (parts.size() >= 3U) {
                const auto package = std::ranges::find_if(
                    units, [&](const semantic::vhdl::Unit& unit) {
                        return unit.kind
                                == semantic::vhdl::UnitKind::package
                            && unit.primary_name.empty()
                            && same_identifier(unit.library,
                                parts[0].spelling)
                            && same_identifier(unit.name,
                                parts[1].spelling);
                    });
                if (package != units.end()) {
                    prefix_size = 3U;
                }
            }
            if (!prefix_size && parts.size() >= 2U) {
                const auto package = std::ranges::find_if(
                    units, [&](const semantic::vhdl::Unit& unit) {
                        return unit.kind
                                == semantic::vhdl::UnitKind::package
                            && unit.primary_name.empty()
                            && same_identifier(unit.name,
                                parts[0].spelling);
                    });
                if (package != units.end()) {
                    prefix_size = 2U;
                }
            }
            if (!prefix_size) {
                prefix_size = 1U;
            }
            std::string prefix;
            for (std::size_t index { }; index < *prefix_size; ++index) {
                if (!prefix.empty()) {
                    prefix.push_back('.');
                }
                prefix.append(parts[index].spelling.data(),
                    parts[index].spelling.size());
            }
            selected_object = resolve_object(prefix);
        }
        if (!selected_object) {
            return std::nullopt;
        }
        const auto declaration = specialization_.find_declaration(
            *selected_object);
        if (!declaration || declaration->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto form = declaration->vhdl->form;
        if (form != Form::variable && form != Form::signal
            && form != Form::constant && form != Form::port
            && form != Form::alias && form != Form::generic_constant
            && form != Form::file) {
            return std::nullopt;
        }

        std::optional<SubtypeIndication> subtype;
        if (declaration->vhdl->subtype) {
            subtype = specialized_subtype(*declaration->vhdl->subtype);
        } else if (declaration->vhdl->declared_type) {
            const auto* definition = type_definition(
                *declaration->vhdl->declared_type);
            if (definition == nullptr) {
                return std::nullopt;
            }
            auto declared_subtype = definition->base;
            declared_subtype.type_mark.target = definition->id;
            if (declared_subtype.type_mark.spelling.empty()) {
                declared_subtype.type_mark.spelling = definition->name;
            }
            subtype = specialized_subtype(std::move(declared_subtype));
        }
        if (!subtype) {
            return std::nullopt;
        }
        if (form == Form::constant) {
            if (const auto object_subtype
                = resolver.effective_vhdl_constant_object_subtype(
                    *selected_object, value.scope)) {
                subtype = *object_subtype;
            }
        }

        // A package-qualified object is rooted at its package member. Keep
        // the declaration identity authoritative when names repeat.
        std::optional<std::string_view> package_name;
        std::optional<std::string_view> package_library;
        const auto scope_id = declaration->vhdl->scope;
        if (const auto* unit
            = specialization_.design().vhdl_scope_unit(scope_id);
            unit != nullptr && unit->kind == semantic::vhdl::UnitKind::package
            && scope_id == unit->scope) {
            package_name = unit->name;
            package_library = unit->library;
        }

        std::optional<std::size_t> object_component;
        if (package_name) {
            if (parts.size() >= 3U && package_library
                && same_identifier(parts[0].spelling, *package_library)
                && same_identifier(parts[1].spelling, *package_name)
                && same_identifier(parts[2].spelling,
                    declaration->vhdl->name)) {
                object_component = 2U;
            } else if (parts.size() >= 2U
                && same_identifier(parts[0].spelling, *package_name)
                && same_identifier(parts[1].spelling,
                    declaration->vhdl->name)) {
                object_component = 1U;
            }
        }
        if (!object_component
            && same_identifier(parts.front().spelling,
                declaration->vhdl->name)) {
            // A use-visible package member and a local object both begin at
            // the first component when they are unqualified.
            object_component = 0U;
        }
        if (!object_component) {
            return std::nullopt;
        }

        for (std::size_t index = *object_component + 1U;
             index < parts.size(); ++index) {
            const auto* record = root_definition(*subtype);
            if (record == nullptr || record->form != TypeForm::record) {
                return std::nullopt;
            }
            const auto member = std::ranges::find_if(
                record->record_elements,
                [&](const semantic::vhdl::RecordElement& element) {
                    return same_identifier(element.name,
                        parts[index].spelling);
                });
            if (member == record->record_elements.end()) {
                return std::nullopt;
            }
            subtype = record_member_subtype(*subtype, *member);
        }
        return subtype;
    }

    std::optional<SubtypeIndication> expression_subtype(
        const semantic::ExpressionId id) const
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto& value = *expression->vhdl;
        if (value.kind == semantic::vhdl::ExpressionKind::unary
            || value.kind == semantic::vhdl::ExpressionKind::binary) {
            // A retained operator selection may only be a visible candidate.
            // Its result subtype is usable after matching the operand types.
            return operator_result_subtype(value);
        }
        constexpr std::string_view user_attribute_marker {
            "@fsim-vhdl-user-attribute:"
        };
        if (value.nominal_type.starts_with(user_attribute_marker)) {
            return resolve_vhdl_user_attribute_subtype(
                specialization_, value.nominal_type, value.scope);
        }
        if (value.kind == semantic::vhdl::ExpressionKind::call) {
            if (const auto result = function_result_subtype(value)) {
                return result;
            }
            if (const auto selection = array_selection(id);
                selection && !selection->slice_result) {
                return selection_result_subtype(*selection);
            }
            constexpr std::string_view qualified_prefix {
                semantic::vhdl::qualified_expression_prefix
            };
            if (value.text.starts_with(qualified_prefix)
                && value.operands.size() == 1U) {
                const auto mark = std::string_view { value.text }.substr(
                    qualified_prefix.size());
                if (mark.empty()) {
                    return std::nullopt;
                }
                SubtypeIndication subtype;
                subtype.type_mark.spelling = std::string { mark };
                if (const auto type = named_type(mark, value.scope)) {
                    subtype.type_mark.target = *type;
                }
                return specialized_subtype(std::move(subtype));
            }
        }
        if (value.referenced_name) {
            const semantic::CompiledDeclarationPredicate typed_declaration
                = [](const semantic::CompiledDeclarationView& candidate) {
                      if (candidate.vhdl == nullptr) {
                          return false;
                      }
                      using Form = semantic::vhdl::DeclarationForm;
                      const auto form = candidate.vhdl->form;
                      // Enumeration literals carry their exact declared enum type.
                      // Object-only name recovery below does not cover these values.
                      return form == Form::type
                          || form == Form::subtype
                          || form == Form::generic_type
                          || form == Form::enumeration_literal;
                  };
            const auto selected
                = semantic::CompiledDesignResolver { specialization_ }
                      .resolve_expression_name(
                          id, typed_declaration, true)
                      .unique();
            const auto declaration = selected
                ? specialization_.find_declaration(*selected)
                : std::nullopt;
            if (declaration && declaration->vhdl != nullptr) {
                SubtypeIndication subtype;
                if (declaration->vhdl->subtype) {
                    subtype = *declaration->vhdl->subtype;
                }
                if (declaration->vhdl->declared_type) {
                    subtype.type_mark.target
                        = *declaration->vhdl->declared_type;
                    subtype.type_mark.spelling
                        = declaration->vhdl->name;
                }
                return specialized_subtype(std::move(subtype));
            }
        }
        if (value.kind == semantic::vhdl::ExpressionKind::name) {
            if (const auto subtype = name_object_subtype(value)) {
                return subtype;
            }
        }
        if (value.kind == semantic::vhdl::ExpressionKind::call
            && value.text.starts_with("@vhdl-member:")
            && value.operands.size() == 1U) {
            const auto prefix = expression_subtype(value.operands.front());
            const auto* record = prefix ? root_definition(*prefix) : nullptr;
            if (record != nullptr && record->form == TypeForm::record) {
                const auto member_name = std::string_view { value.text }
                    .substr(std::string_view { "@vhdl-member:" }.size());
                const auto member = std::ranges::find_if(
                    record->record_elements,
                    [&](const semantic::vhdl::RecordElement& element) {
                        return equal_ignoring_ascii_case(
                            element.name, member_name);
                    });
                if (member != record->record_elements.end()) {
                    return record_member_subtype(*prefix, *member);
                }
            }
        }
        if (!value.nominal_type.empty()) {
            SubtypeIndication subtype;
            subtype.type_mark.spelling = value.nominal_type;
            if (const auto type = named_type(
                    value.nominal_type, value.scope)) {
                subtype.type_mark.target = *type;
            }
            return specialized_subtype(std::move(subtype));
        }
        if (value.kind != semantic::vhdl::ExpressionKind::name) {
            // A selected element name (`v.f`) whose object the reference did not
            // select: resolve the object by the name's prefix.
            auto selected_object = value.referenced_name
                ? value.referenced_name->selected
                : std::nullopt;
            if (!selected_object && value.kind == semantic::vhdl::ExpressionKind::name
                && value.text.find('.') != std::string::npos
                && value.text.find('\'') == std::string::npos) {
                semantic::vhdl::Name prefix;
                prefix.spelling = value.text.substr(0U, value.text.find('.'));
                prefix.canonical = prefix.spelling;
                const semantic::CompiledDeclarationPredicate object
                    = [](const semantic::CompiledDeclarationView& candidate) {
                          return candidate.vhdl != nullptr
                              && semantic::vhdl::is_value_object_form(
                                  candidate.vhdl->form);
                      };
                selected_object = semantic::CompiledDesignResolver {
                    specialization_ }
                                      .resolve_vhdl(prefix, value.scope, object)
                                      .unique();
            }
            if (selected_object) {
                const auto declaration = specialization_.find_declaration(
                    *selected_object);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->subtype) {
                    auto subtype = specialized_subtype(
                        *declaration->vhdl->subtype);
                    using Form = semantic::vhdl::DeclarationForm;
                    const auto form = declaration->vhdl->form;
                    if (form == Form::constant) {
                        if (const auto object_subtype
                            = semantic::CompiledDesignResolver {
                                  specialization_
                              }.effective_vhdl_constant_object_subtype(
                                  *selected_object, value.scope)) {
                            subtype = *object_subtype;
                        }
                    }
                    const auto object
                        = semantic::vhdl::is_value_object_form(form)
                        || form == Form::alias;
                    auto remaining = std::string_view { value.text };
                    const auto separator = remaining.find('.');
                    if (!object || separator == std::string_view::npos) {
                        return subtype;
                    }
                    remaining.remove_prefix(separator + 1U);
                    while (!remaining.empty()) {
                        const auto* record = root_definition(subtype);
                        if (record == nullptr
                            || record->form != TypeForm::record) {
                            return std::nullopt;
                        }
                        const auto member_separator = remaining.find('.');
                        const auto member_name = remaining.substr(
                            0U, member_separator);
                        const auto member = std::ranges::find_if(
                            record->record_elements,
                            [&](const semantic::vhdl::RecordElement& element) {
                                return equal_ignoring_ascii_case(element.name, member_name);
                            });
                        if (member == record->record_elements.end()) {
                            return std::nullopt;
                        }
                        subtype = record_member_subtype(subtype, *member);
                        if (member_separator == std::string_view::npos) {
                            return subtype;
                        }
                        remaining.remove_prefix(member_separator + 1U);
                    }
                    return subtype;
                }
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->declared_type) {
                    const auto* definition = type_definition(
                        *declaration->vhdl->declared_type);
                    if (definition != nullptr) {
                        auto subtype = definition->base;
                        subtype.type_mark.target = definition->id;
                        if (subtype.type_mark.spelling.empty()) {
                            subtype.type_mark.spelling = definition->name;
                        }
                        return specialized_subtype(std::move(subtype));
                    }
                }
            }
        }
        if ((value.kind == semantic::vhdl::ExpressionKind::index
                || value.kind
                    == semantic::vhdl::ExpressionKind::slice)
            && !value.operands.empty()) {
            if (value.kind == semantic::vhdl::ExpressionKind::index) {
                if (const auto selection = array_selection(id); selection) {
                    if (!selection->slice_result) {
                        return selection_result_subtype(*selection);
                    }
                    if (!selection->invalid_index_group
                        && selection->selectors.size() == 1U) {
                        const auto selector = specialization_.find_expression(
                            selection->selectors.front());
                        if (selector && selector->vhdl != nullptr
                            && selector->vhdl->kind
                                == semantic::vhdl::ExpressionKind::call
                            && (selector->vhdl->text == "'range"
                                || selector->vhdl->text
                                    == "'reverse_range")) {
                            // A range attribute used as the sole index is a
                            // slice value of the selected array's subtype.
                            // Keep the composite type instead of consuming
                            // the dimension as an element index.
                            return selection->subtype;
                        }
                    }
                }
            }
            const auto base = expression_subtype(value.operands.front());
            if (!base) {
                return std::nullopt;
            }
            if (value.kind == semantic::vhdl::ExpressionKind::slice) {
                if (value.operands.size() != 3U
                    || (value.text != "to" && value.text != "downto")) {
                    return base;
                }
                // A slice keeps the array type but has its own index range.
                // In particular, its width is not the width of the prefix.
                auto sliced = *base;
                semantic::vhdl::RangeConstraint range;
                range.kind = semantic::vhdl::RangeKind::array_index;
                range.left_expression = value.operands[1];
                range.right_expression = value.operands[2];
                range.descending = value.text == "downto";
                range.source = value.source;
                if (sliced.constraints.empty()) {
                    sliced.constraints.push_back(range);
                } else {
                    sliced.constraints.front() = range;
                }
                sliced.executable_width.reset();
                sliced.unconstrained = false;
                return specialized_subtype(std::move(sliced));
            }
            return indexed_subtype(*base, value.operands.size() - 1U);
        }
        return std::nullopt;
    }

    // The subtype denoted by indexing `selectors` dimensions of an array
    // subtype: the remaining dimensions, or the element subtype once every
    // dimension is selected (IEEE 1076-2008 8.4).
    std::optional<SubtypeIndication> indexed_subtype(
        const SubtypeIndication& base, const std::size_t selectors) const
    {
        const auto type = subtype_type(base);
        const auto root = type ? array_root(*type) : std::nullopt;
        const auto* definition = root ? type_definition(*root) : nullptr;
        const auto dimensions = definition != nullptr
            ? definition->array_dimensions.size()
            : base.constraints.size();
        if (selectors < dimensions && selectors != 0U) {
            auto reduced = base;
            const auto erased = std::min(selectors, reduced.constraints.size());
            reduced.constraints.erase(reduced.constraints.begin(),
                reduced.constraints.begin()
                    + static_cast<std::ptrdiff_t>(erased));
            reduced.executable_width.reset();
            reduced.unconstrained = false;
            return specialized_subtype(std::move(reduced));
        }
        return array_element_subtype(base);
    }

    std::optional<TypeProfile> expression_profile(
        const semantic::ExpressionId id) const
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
        }
        if (expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::integer_literal) {
            return TypeProfile { ProfileKind::integer, std::nullopt,
                std::nullopt, { }, false };
        }
        if (const auto subtype = expression_subtype(id)) {
            return profile(*subtype, expression->vhdl->scope);
        }
        return std::nullopt;
    }

    enum class OperandClass : std::uint8_t {
        unknown,
        integer,
        floating,
        physical,
        enumeration,
        array,
        record,
        access,
        null,
        other,
    };

    // Scalar types are the enumeration, integer, physical and floating
    // types; integer, physical and floating types are the numeric types
    // (IEEE 1076-2008 5.2.1).
    static constexpr bool known_class(const OperandClass kind) noexcept
    {
        return kind != OperandClass::unknown;
    }
    static constexpr bool integer_or_floating_class(
        const OperandClass kind) noexcept
    {
        return kind == OperandClass::integer || kind == OperandClass::floating;
    }
    static constexpr bool numeric_class(const OperandClass kind) noexcept
    {
        return integer_or_floating_class(kind)
            || kind == OperandClass::physical;
    }
    static constexpr bool scalar_class(const OperandClass kind) noexcept
    {
        return numeric_class(kind) || kind == OperandClass::enumeration;
    }

    struct OperandType {
        OperandClass kind { OperandClass::unknown };
        // Root type identity; empty for universal literals and unknown roots.
        // `#<id>` names a retained type definition, a plain name a
        // predefined type without one.
        std::string root;
    };

    // Two roots denote different types. Predefined types are named, types
    // declared in the design are definition identities.
    static bool roots_conflict(const std::string& left, const std::string& right)
    {
        return !left.empty() && !right.empty() && left != right;
    }

    // Two operand types differ: different classes or conflicting roots.
    static bool types_conflict(const OperandType& left, const OperandType& right)
    {
        return left.kind != right.kind || roots_conflict(left.root, right.root);
    }

    // The type declared by the nearest `type` declaration on a subtype's
    // chain: `type x1 is range 1.0 to 9.0` is kept as a subtype of REAL.
    std::optional<std::string> declared_scalar_root(const SubtypeIndication& subtype) const
    {
        const auto root_for_type = [&](std::optional<semantic::TypeId> type)
            -> std::optional<std::string> {
            std::unordered_set<std::uint32_t> visiting;
            while (type && visiting.insert(type->value()).second) {
                const auto* definition = type_definition(*type);
                if (definition == nullptr) {
                    return std::nullopt;
                }
                const auto declaration = specialization_.find_declaration(
                    definition->declaration);
                const auto& semantic_types =
                    specialization_.design().semantics.types();
                // An integer or floating type definition is retained as a
                // subtype of INTEGER or REAL but recorded as a type declaration.
                const bool scalar_type_definition =
                    type->value() < semantic_types.size()
                    && semantic_types[type->value()].id == *type
                    && semantic_types[type->value()].kind
                        == semantic::TypeKind::declaration;
                if (declaration && declaration->vhdl != nullptr
                    && (declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::type
                        || scalar_type_definition)) {
                    return library_type(*definition)
                        ? std::nullopt
                        : std::optional {
                              "#" + std::to_string(definition->id.value()) };
                }
                if (definition->form != TypeForm::subtype
                    && definition->form != TypeForm::alias) {
                    return std::nullopt;
                }
                type = subtype_type(definition->base);
            }
            return std::nullopt;
        };
        // Preserve a retained source type declaration before resolving a
        // generic actual or reducing a named type to its predefined base.
        if (const auto source_root = root_for_type(subtype_type(subtype))) {
            return source_root;
        }
        return root_for_type(subtype_type(specialized_subtype(subtype)));
    }

    static OperandClass builtin_operand_class(const std::string_view spelling)
    {
        const auto name = simple_name_of(spelling);
        const auto any = [&](const std::initializer_list<std::string_view> names) {
            return std::ranges::any_of(names,
                [&](const auto candidate) { return equal_ignoring_ascii_case(name, candidate); });
        };
        if (any({ "integer", "natural", "positive" })) {
            return OperandClass::integer;
        }
        if (any({ "real" })) {
            return OperandClass::floating;
        }
        if (any({ "time", "delay_length" })) {
            return OperandClass::physical;
        }
        if (any({ "boolean", "bit", "character", "severity_level",
                "file_open_kind", "file_open_status", "std_ulogic",
                "std_logic" })) {
            return OperandClass::enumeration;
        }
        if (any({ "string", "bit_vector", "std_ulogic_vector",
                "std_logic_vector", "boolean_vector", "integer_vector",
                "real_vector", "time_vector", "signed", "unsigned" })) {
            return OperandClass::array;
        }
        // STD.TEXTIO's TEXT is a file type and LINE an access type.
        if (any({ "text" })) {
            return OperandClass::other;
        }
        if (any({ "line" })) {
            return OperandClass::access;
        }
        return OperandClass::unknown;
    }

    static std::string builtin_root(const std::string_view spelling)
    {
        const auto name = simple_name_of(spelling);
        if (equal_ignoring_ascii_case(name, "natural") || equal_ignoring_ascii_case(name, "positive")) {
            return "integer";
        }
        if (equal_ignoring_ascii_case(name, "delay_length")) {
            return "time";
        }
        if (equal_ignoring_ascii_case(name, "std_logic")) {
            return "std_ulogic";
        }
        std::string result { name };
        lowercase_ascii(result);
        return result;
    }

    OperandType subtype_operand_type(
        const SubtypeIndication& subtype, const std::size_t depth) const
    {
        auto result = subtype_operand_class(subtype, depth);
        if (scalar_class(result.kind)) {
            if (const auto declared = declared_scalar_root(subtype)) {
                result.root = *declared;
            }
        }
        return result;
    }

    OperandType subtype_operand_class(
        const SubtypeIndication& subtype, const std::size_t depth) const
    {
        const auto* definition = root_definition(subtype);
        if (definition == nullptr) {
            // The chain may end at a predefined type without a retained
            // definition (`subtype r is real range ...`).
            auto spelling = std::string_view { subtype.type_mark.spelling };
            auto type = subtype_type(subtype);
            std::unordered_set<std::uint32_t> visiting;
            while (type && visiting.insert(type->value()).second) {
                const auto* link = type_definition(*type);
                if (link == nullptr) {
                    break;
                }
                if (link->form != TypeForm::subtype
                    && link->form != TypeForm::alias) {
                    spelling = link->name;
                    break;
                }
                if (!link->base.type_mark.spelling.empty()) {
                    // A source subtype's name is not its base type's class.
                    // In particular, a local integer subtype named LINE must
                    // not inherit STD.TEXTIO.LINE's access classification.
                    spelling = link->base.type_mark.spelling;
                }
                type = subtype_type(link->base);
            }
            const auto kind = builtin_operand_class(spelling);
            return { kind,
                kind == OperandClass::unknown
                    ? std::string { }
                    : builtin_root(spelling) };
        }
        const auto named = builtin_operand_class(definition->name);
        if (named != OperandClass::unknown && library_type(*definition)) {
            return { named, builtin_root(definition->name) };
        }
        auto root = "#" + std::to_string(definition->id.value());
        if (library_type(*definition)) {
            root = "@" + root;
        }
        switch (definition->form) {
        case TypeForm::enumeration:
            return { OperandClass::enumeration, root };
        case TypeForm::array:
            return { OperandClass::array, root };
        case TypeForm::record:
            return { OperandClass::record, root };
        case TypeForm::access:
            return { OperandClass::access, root };
        case TypeForm::physical:
            return { OperandClass::physical, root };
        case TypeForm::file:
        case TypeForm::protected_type:
        case TypeForm::protected_body:
            return { OperandClass::other, root };
        case TypeForm::scalar: {
            if (!definition->scalar_range || depth > 8U) {
                return { };
            }
            const auto bound_class = [&](const std::optional<semantic::ExpressionId> id) {
                return id ? operand_type(*id, depth + 1U).kind : OperandClass::unknown;
            };
            const auto left = bound_class(definition->scalar_range->left_expression);
            const auto right = bound_class(definition->scalar_range->right_expression);
            if (left == OperandClass::floating || right == OperandClass::floating) {
                return { OperandClass::floating, root };
            }
            if (left == OperandClass::integer || right == OperandClass::integer
                || (definition->scalar_range->left && definition->scalar_range->right
                    && !definition->scalar_range->left_expression
                    && !definition->scalar_range->right_expression)) {
                return { OperandClass::integer, root };
            }
            return { };
        }
        default:
            return { };
        }
    }

    OperandType operand_type(
        const semantic::ExpressionId id, const std::size_t depth = 0U) const
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr || depth > 8U) {
            return { };
        }
        const auto& value = *expression->vhdl;
        using Kind = semantic::vhdl::ExpressionKind;
        switch (value.kind) {
        case Kind::integer_literal:
            if (!value.nominal_type.empty()) {
                break;
            }
            return { OperandClass::integer, { } };
        case Kind::real_literal:
            if (!value.nominal_type.empty()) {
                break;
            }
            return { OperandClass::floating, { } };
        case Kind::boolean_literal:
            return { OperandClass::enumeration, "boolean" };
        case Kind::string_literal:
            return { OperandClass::array, "@literal" };
        case Kind::logic_literal:
            if (value.text.starts_with('\'')) {
                return { OperandClass::enumeration, { } };
            }
            return { };
        case Kind::call:
            if (value.text == "@vhdl-null") {
                return { OperandClass::null, { } };
            }
            if (value.text == "@vhdl-new" || value.text == "@vhdl-new-qualified") {
                return { OperandClass::access, { } };
            }
            // `10 ns`: a physical literal of the type declaring the unit.
            if (value.text.starts_with("@vhdl-physical:")) {
                return { OperandClass::physical, { } };
            }
            // T'(e) has the type of its type mark (9.3.5).
            if (constexpr std::string_view qualified {
                    semantic::vhdl::qualified_expression_prefix };
                value.text.starts_with(qualified)
                && value.operands.size() == 1U) {
                const auto mark = std::string_view { value.text }.substr(qualified.size());
                SubtypeIndication subtype;
                subtype.type_mark.spelling = std::string { mark };
                if (const auto type = named_type(mark, value.scope)) {
                    subtype.type_mark.target = *type;
                }
                return subtype_operand_type(subtype, depth + 1U);
            }
            // T'LEFT, T'RIGHT, T'LOW and T'HIGH of a scalar type mark have
            // the type of T (IEEE 1076-1993 14.1).
            if ((semantic::vhdl::is_scalar_bound_attribute(value.text))
                && value.operands.size() == 1U) {
                const auto prefix = specialization_.find_expression(
                    value.operands.front());
                if (prefix && prefix->vhdl != nullptr
                    && prefix->vhdl->kind == Kind::name) {
                    if (const auto type = named_type(
                            prefix->vhdl->text, value.scope)) {
                        SubtypeIndication subtype;
                        subtype.type_mark.spelling = prefix->vhdl->text;
                        subtype.type_mark.target = *type;
                        const auto result
                            = subtype_operand_type(subtype, depth + 1U);
                        if (result.kind == OperandClass::enumeration
                            || result.kind == OperandClass::integer
                            || result.kind == OperandClass::physical
                            || result.kind == OperandClass::floating) {
                            return result;
                        }
                    }
                }
            }
            return { };
        case Kind::unary:
            if (value.operands.size() == 1U
                && (value.text == "-" || value.text == "+" || value.text == "abs")
                && predefined_operator(value)) {
                const auto operand = operand_type(value.operands.front(), depth + 1U);
                if (operand.kind == OperandClass::integer
                    || operand.kind == OperandClass::floating
                    || operand.kind == OperandClass::physical) {
                    return operand;
                }
            }
            return { };
        case Kind::binary: {
            if (value.operands.size() != 2U || !predefined_operator(value)) {
                return { };
            }
            const auto left = operand_type(value.operands[0], depth + 1U);
            const auto right = operand_type(value.operands[1], depth + 1U);
            const auto same = [&]() -> OperandType {
                if (types_conflict(left, right)) {
                    return { };
                }
                return { left.kind, left.root.empty() ? right.root : left.root };
            };
            const auto& operation = value.text;
            if (operation == "+" || operation == "-" || operation == "mod"
                || operation == "rem") {
                return numeric_class(left.kind)
                    ? same()
                    : OperandType { };
            }
            if (operation == "*" || operation == "/") {
                if (integer_or_floating_class(left.kind)
                    && integer_or_floating_class(right.kind)) {
                    return same();
                }
                if (left.kind == OperandClass::physical
                    && integer_or_floating_class(right.kind)) {
                    return left;
                }
                if (operation == "*" && right.kind == OperandClass::physical
                    && integer_or_floating_class(left.kind)) {
                    return right;
                }
                if (operation == "/" && left.kind == OperandClass::physical
                    && right.kind == OperandClass::physical) {
                    return { OperandClass::integer, { } };
                }
                return { };
            }
            if (operation == "**" && integer_or_floating_class(left.kind)) {
                return left;
            }
            return { };
        }
        case Kind::name:
        case Kind::index:
        case Kind::slice:
            if (value.text.find('\'') != std::string::npos) {
                return { };
            }
            // `q(1 to 8)` may be retained as an index whose argument is a
            // range: a slice, not an element.
            if (value.kind == Kind::index
                && std::ranges::any_of(value.operands, [&](const semantic::ExpressionId id) {
                       const auto operand = specialization_.find_expression(id);
                       return operand && operand->vhdl != nullptr
                           && operand->vhdl->kind == Kind::binary
                           && (operand->vhdl->text == "to"
                               || operand->vhdl->text == "downto");
                   })) {
                return { };
            }
            // An enumeration literal the design's context-typed resolution
            // selected has the type declaring it (IEEE 1076-2008 9.3.2).
            if (value.kind == Kind::name && value.resolved_declaration) {
                const auto declaration = specialization_.find_declaration(
                    *value.resolved_declaration);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->form
                        == semantic::vhdl::DeclarationForm::enumeration_literal) {
                    for (const auto& type
                        : specialization_.design().vhdl_hir.types()) {
                        if (std::ranges::any_of(type.enumeration_literals,
                                [&](const semantic::vhdl::EnumerationLiteral& literal) {
                                    return literal.declaration
                                        == *value.resolved_declaration;
                                })) {
                            return { OperandClass::enumeration,
                                "#" + std::to_string(type.id.value()) };
                        }
                    }
                }
            }
            // A CHARACTER control literal (NUL, ESC, ...) of STD.STANDARD,
            // which is not retained as a declaration.
            if (value.kind == Kind::name
                && (!value.referenced_name
                    || !value.referenced_name->selected)
                && vhdl_character_control_literal(value.text)) {
                return { OperandClass::enumeration, "character" };
            }
            // A loop parameter is retained as an INTEGER constant without a
            // value, whatever its discrete range's type.
            if (value.kind == Kind::name && value.referenced_name
                && value.referenced_name->selected) {
                const auto declaration = specialization_.find_declaration(
                    *value.referenced_name->selected);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->form
                        == semantic::vhdl::DeclarationForm::constant
                    && !declaration->vhdl->initializer) {
                    return { };
                }
            }
            break;
        default:
            return { };
        }
        // A selected object hides a type of the same name (`a` formal and
        // type `A`); use the object's subtype.
        if (value.kind == Kind::name && value.referenced_name
            && value.referenced_name->selected
            && value.text.find('.') == std::string::npos) {
            const auto declaration = specialization_.find_declaration(
                *value.referenced_name->selected);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
                && semantic::vhdl::is_value_object_form(
                    declaration->vhdl->form)) {
                const auto& declared_subtype = *declaration->vhdl->subtype;
                auto result = subtype_operand_type(
                    specialized_subtype(declared_subtype), depth);
                // Effective subtype resolution may reduce a named integer
                // type to INTEGER while retaining its bounds. Keep the
                // declaration's exact scalar type identity for nominal
                // comparisons such as array-index and slice-bound checks.
                // Predefined INTEGER and transparent INTEGER subtypes have
                // no declared scalar root and keep the effective result.
                if (result.kind == OperandClass::integer) {
                    if (const auto root = declared_scalar_root(declared_subtype)) {
                        result.root = *root;
                    }
                }
                return result;
            }
        }
        const auto subtype = expression_subtype(id);
        if (!subtype) {
            return { };
        }
        return subtype_operand_type(*subtype, depth);
    }

    // A type declared in a library package (ieee, std), whose arithmetic
    // may be provided natively.
    bool library_type(const TypeDefinition& definition) const
    {
        const auto declaration = specialization_.find_declaration(definition.declaration);
        if (!declaration || declaration->vhdl == nullptr) {
            return true;
        }
        const auto& scopes = specialization_.design().semantics.scopes();
        const auto scope = declaration->vhdl->scope;
        if (!scope.valid() || scope.value() >= scopes.size()) {
            return true;
        }
        const auto unit = specialization_.design().find_unit(scopes[scope.value()].unit);
        if (!unit || unit->vhdl == nullptr) {
            return true;
        }
        return equal_ignoring_ascii_case(unit->vhdl->library, "ieee")
            || equal_ignoring_ascii_case(unit->vhdl->library, "std")
            || !unit->vhdl->standard_package_revision.empty();
    }

    // True when no function named like the operator is visible, so only the
    // predefined operator can apply.
    bool predefined_operator(const semantic::vhdl::Expression& expression) const
    {
        if (!expression.referenced_name) {
            return true;
        }
        if (expression.referenced_name->selected
            || !expression.referenced_name->overloads.empty()) {
            return false;
        }
        return semantic::CompiledDesignResolver { specialization_ }
            .resolve_vhdl_callables(
                *expression.referenced_name, expression.scope)
            .candidates.empty();
    }

    // Names the resolver does not model as declarations at every use:
    // generate parameters, the unit's own declarations, and physical unit
    // names (`x := sec;`).
    // Whether a generate statement of the units under check iterates a
    // parameter of this name.
    bool generate_iterator(const std::string_view name) const
    {
        const auto search = [&](const auto& self,
                                const std::vector<semantic::vhdl::GenerateRegion>& regions)
            -> bool {
            return std::ranges::any_of(regions, [&](const semantic::vhdl::GenerateRegion& region) {
                return equal_ignoring_ascii_case(region.iterator, name)
                    || self(self, region.nested);
            });
        };
        return search(search, entity_.generates) || search(search, architecture_.generates);
    }

    // Whether a scope lies within a subprogram body or declaration.
    bool within_subprogram(semantic::ScopeId scope)
    {
        if (!callable_scopes_) {
            callable_scopes_.emplace();
            for (const auto& candidate : specialization_.design().vhdl_hir.declarations()) {
                if (candidate.callable && candidate.nested_scope) {
                    callable_scopes_->insert(candidate.nested_scope->value());
                }
            }
        }
        const auto& scopes = specialization_.design().semantics.scopes();
        for (std::size_t depth { }; depth < 64U && scope.valid() && scope.value() < scopes.size();
             ++depth) {
            if (callable_scopes_->contains(scope.value())) {
                return true;
            }
            const auto parent = scopes[scope.value()].parent;
            if (!parent) {
                break;
            }
            scope = *parent;
        }
        return false;
    }

    bool name_exempt(const std::string_view name)
    {
        if (!exempt_names_) {
            exempt_names_.emplace();
            const auto add = [&](const std::string_view value) {
                std::string lowered { value };
                lowercase_ascii(lowered);
                exempt_names_->insert(std::move(lowered));
            };
            const auto add_generates = [&](const auto& self,
                                           const std::vector<semantic::vhdl::GenerateRegion>& regions)
                -> void {
                for (const auto& region : regions) {
                    if (!region.iterator.empty()) {
                        add(region.iterator);
                    }
                    self(self, region.nested);
                }
            };
            for (const auto* unit : { &entity_, &architecture_ }) {
                add_generates(add_generates, unit->generates);
                for (const auto id : unit->declarations) {
                    const auto declaration = specialization_.find_declaration(id);
                    if (declaration && declaration->vhdl != nullptr
                        && declaration->vhdl->form
                            != semantic::vhdl::DeclarationForm::generated) {
                        add(declaration->vhdl->name);
                    }
                }
            }
            for (const auto& type : specialization_.design().vhdl_hir.types()) {
                for (const auto& unit : type.physical_units) {
                    add(unit.name);
                }
            }
            for (const auto& unit : elaboration_detail::vhdl_time_units) {
                add(unit.first);
            }
            // A name declared anywhere in the design may be made visible in
            // ways the resolver does not model (compiler-supplied VITAL and
            // STD.ENV declarations, selected visibility); labels, design
            // units and record elements are not declarations.
            const auto& scopes = specialization_.design().semantics.scopes();
            for (const auto& declaration :
                specialization_.design().vhdl_hir.declarations()) {
                // Formals and locals of the STD and IEEE packages (`l`, `r`)
                // are never visible by their simple names.
                const auto unit = declaration.scope.valid()
                        && declaration.scope.value() < scopes.size()
                    ? specialization_.design().find_unit(
                          scopes[declaration.scope.value()].unit)
                    : std::nullopt;
                if (unit && unit->vhdl != nullptr
                    && (equal_ignoring_ascii_case(unit->vhdl->library, "ieee")
                        || equal_ignoring_ascii_case(unit->vhdl->library, "std"))) {
                    continue;
                }
                // Block and generate labels are not values.
                if (declaration.form == semantic::vhdl::DeclarationForm::generated) {
                    continue;
                }
                // A formal or local of a subprogram in another design unit
                // is visible only within that subprogram.
                if (unit && unit->vhdl != nullptr && unit->vhdl->id != entity_.id
                    && unit->vhdl->id != architecture_.id
                    && within_subprogram(declaration.scope)) {
                    continue;
                }
                add(declaration.name);
            }
            // STD.ENV directory and file status literals, provided by the
            // compiler without retained declarations (VHDL-2019 16.5).
            for (const auto literal : { "status_ok", "status_not_found",
                     "status_no_directory", "status_item_exists",
                     "status_not_empty", "status_no_file",
                     "status_access_denied", "status_error" }) {
                add(literal);
            }
            // Enumeration literals also become visible through an alias of
            // their type (6.6.3).
            for (const auto& type : specialization_.design().vhdl_hir.types()) {
                for (const auto& literal : type.enumeration_literals) {
                    add(literal.spelling);
                }
            }
        }
        std::string lowered { name };
        lowercase_ascii(lowered);
        return exempt_names_->contains(lowered);
    }

    static bool vhdl_character_control_literal(const std::string_view name)
    {
        constexpr std::array literals {
            "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel", "bs", "ht",
            "lf", "vt", "ff", "cr", "so", "si", "dle", "dc1", "dc2", "dc3",
            "dc4", "nak", "syn", "etb", "can", "em", "sub", "esc", "fsp", "gsp",
            "rsp", "usp", "del", "c128", "c129", "c130", "c131", "c132", "c133",
            "c134", "c135", "c136", "c137", "c138", "c139", "c140", "c141",
            "c142", "c143", "c144", "c145", "c146", "c147", "c148", "c149",
            "c150", "c151", "c152", "c153", "c154", "c155", "c156", "c157",
            "c158", "c159",
        };
        return std::ranges::any_of(literals,
            [&](const char* literal) { return equal_ignoring_ascii_case(name, literal); });
    }

    // IEEE 1076-2008 9.3.7: the subtype indication of an allocator denotes a
    // constrained subtype; an unconstrained array type mark needs an index
    // constraint.
    void validate_allocator(const semantic::vhdl::Expression& expression)
    {
        if (expression.kind != semantic::vhdl::ExpressionKind::call
            || expression.text != "@vhdl-new" || expression.operands.empty()) {
            return;
        }
        const auto subtype = specialization_.find_expression(expression.operands.front());
        constexpr std::string_view prefix { "@vhdl-subtype:" };
        if (!subtype || subtype->vhdl == nullptr
            || !subtype->vhdl->text.starts_with(prefix)
            || !subtype->vhdl->operands.empty()) {
            return;
        }
        const auto mark = std::string_view { subtype->vhdl->text }.substr(prefix.size());
        bool unconstrained { };
        if (const auto type = named_type(mark, expression.scope)) {
            const auto* definition = type_definition(*type);
            unconstrained = definition != nullptr && definition->form == TypeForm::array
                && std::ranges::any_of(definition->array_dimensions,
                    [](const semantic::vhdl::ArrayDimension& dimension) {
                        return dimension.unconstrained;
                    });
        } else {
            auto simple = mark;
            simple.remove_prefix(simple.find_last_of('.') + 1U);
            static constexpr std::array<std::string_view, 6> predefined {
                "bit_vector", "string", "boolean_vector", "integer_vector",
                "real_vector", "time_vector"
            };
            unconstrained = std::ranges::any_of(predefined,
                [&](const std::string_view name) { return equal_ignoring_ascii_case(simple, name); });
        }
        if (unconstrained) {
            report("FSIM-ELAB-VHACCESS-031",
                "the subtype indication of a VHDL allocator must be constrained",
                expression.source);
        }
    }

    // The type of an array dimension's index: from the bounds of its index
    // constraint, else its index subtype name (which defaults to INTEGER).
    OperandType dimension_index_type(const semantic::vhdl::ArrayDimension& dimension,
        const semantic::ScopeId scope)
    {
        // The bounds of an index constraint give its type; the recorded
        // index subtype name defaults to INTEGER.
        if (dimension.constraint && dimension.constraint->left_expression) {
            if (const auto bound = operand_type(*dimension.constraint->left_expression);
                bound.kind != OperandClass::unknown) {
                return bound;
            }
        }
        const auto spelling = dimension.index_subtype.text();
        if (!spelling.empty()) {
            SubtypeIndication subtype;
            subtype.type_mark.spelling = spelling;
            if (const auto type = named_type(spelling, scope)) {
                subtype.type_mark.target = *type;
            }
            return subtype_operand_type(subtype, 0U);
        }
        if (dimension.constraint && dimension.constraint->left_expression) {
            return operand_type(*dimension.constraint->left_expression);
        }
        if (dimension.constraint && dimension.constraint->left) {
            return { OperandClass::integer, { } };
        }
        return { };
    }

    // IEEE 1076-1993 7.3.5: a conversion between array types requires the
    // index types of each dimension to be closely related: the same type, or
    // both integer types.
    void validate_array_conversion(const semantic::vhdl::Expression& expression)
    {
        if (expression.operands.size() != 1U || !expression.referenced_name) {
            return;
        }
        const auto mark = expression.referenced_name->text();
        const auto target_id = named_type(mark, expression.scope);
        const auto operand = operand_type(expression.operands.front());
        if (!target_id || !operand.root.starts_with('#')) {
            return;
        }
        const auto array_of = [&](const semantic::TypeId id)
            -> const TypeDefinition* {
            const auto* base
                = semantic::vhdl_base_type_definition(specialization_, id);
            return base != nullptr && base->form == TypeForm::array
                ? base
                : nullptr;
        };
        std::uint32_t operand_id { };
        const auto digits = std::string_view { operand.root }.substr(1U);
        if (std::from_chars(digits.data(), digits.data() + digits.size(), operand_id).ec
            != std::errc { }) {
            return;
        }
        const auto* target = array_of(*target_id);
        const TypeDefinition* source { };
        for (const auto& stored : specialization_.design().vhdl_hir.types()) {
            if (stored.id.value() == operand_id) {
                source = array_of(stored.id);
                break;
            }
        }
        if (target == nullptr || source == nullptr || target == source
            || target->array_dimensions.size() != source->array_dimensions.size()) {
            return;
        }
        const auto index_type = [&](const semantic::vhdl::ArrayDimension& dimension) {
            return dimension_index_type(dimension, expression.scope);
        };
        for (std::size_t index { }; index < target->array_dimensions.size(); ++index) {
            const auto left = index_type(target->array_dimensions[index]);
            const auto right = index_type(source->array_dimensions[index]);
            if (left.kind == OperandClass::unknown || right.kind == OperandClass::unknown) {
                continue;
            }
            const bool related = left.kind == OperandClass::integer
                ? right.kind == OperandClass::integer
                : !types_conflict(left, right);
            if (!related) {
                report("FSIM-ELAB-VHCONV-006",
                    "a VHDL array type conversion requires closely related index types",
                    expression.source);
                return;
            }
        }
    }

    // IEEE 1076-2008 9.3.5: a qualified expression's operand has the type
    // of its type mark; 9.3.6: a type conversion's operand is not a string
    // literal, aggregate, allocator or null.
    void validate_conversion_and_qualification(const semantic::vhdl::Expression& expression)
    {
        if (expression.kind != semantic::vhdl::ExpressionKind::call) {
            return;
        }
        constexpr std::string_view qualified_prefix {
            semantic::vhdl::qualified_expression_prefix
        };
        if (expression.operands.empty()) {
            return;
        }
        if (expression.text.starts_with(qualified_prefix)
            && expression.operands.size() == 1U) {
            const auto mark = std::string_view { expression.text }.substr(
                qualified_prefix.size());
            const auto type = named_type(mark, expression.scope);
            SubtypeIndication subtype;
            subtype.type_mark.spelling = std::string { mark };
            if (type) {
                subtype.type_mark.target = *type;
            }
            const auto expected = subtype_operand_type(subtype, 0U);
            const auto actual = operand_type(expression.operands.front());
            if (const auto operand = specialization_.find_expression(
                    expression.operands.front());
                operand && operand->vhdl != nullptr
                && operand->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate) {
                // 9.3.3.3: OTHERS needs the bounds of a constrained context,
                // also in the subaggregates of a multidimensional array.
                const auto* qualified_array = root_definition(subtype);
                const auto dimensions = qualified_array != nullptr
                        && qualified_array->form == TypeForm::array
                    ? qualified_array->array_dimensions.size()
                    : std::size_t { 1U };
                const auto has_others = [&](const auto& self, const semantic::vhdl::Expression& value,
                                            const std::size_t level) -> bool {
                    return std::ranges::any_of(value.associations,
                        [&](const semantic::vhdl::AggregateAssociation& association) {
                            if (choice_is_others(association)) {
                                return true;
                            }
                            const auto nested = specialization_.find_expression(association.value);
                            return level + 1U < dimensions && nested && nested->vhdl != nullptr
                                && nested->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
                                && self(self, *nested->vhdl, level + 1U);
                        });
                };
                const bool others = has_others(has_others, *operand->vhdl, 0U);
                if (others && unconstrained_array_subtype(subtype)) {
                    report("FSIM-ELAB-VHAGG-016",
                        "a VHDL aggregate with OTHERS requires a constrained array "
                        "context, not unconstrained type mark '" + std::string { mark } + "'",
                        expression.source);
                    return;
                }
                validate_aggregate(expression.operands.front(), subtype);
            }
            // A character literal operand must be a literal of the type mark's
            // predefined enumeration (BIT, STD_ULOGIC).
            if (const auto operand = specialization_.find_expression(
                    expression.operands.front());
                operand && operand->vhdl != nullptr
                && operand->vhdl->kind == semantic::vhdl::ExpressionKind::logic_literal) {
                auto literal = std::string_view { operand->vhdl->text };
                if (literal.size() == 3U && literal.front() == '\'' && literal.back() == '\'') {
                    literal = literal.substr(1U, 1U);
                }
                auto simple = mark;
                simple.remove_prefix(simple.find_last_of('.') + 1U);
                const auto in = [&](const std::string_view literals) {
                    return literal.size() == 1U
                        && literals.find(literal.front()) != std::string_view::npos;
                };
                if ((equal_ignoring_ascii_case(simple, "bit") && !in("01"))
                    || ((equal_ignoring_ascii_case(simple, "std_ulogic") || equal_ignoring_ascii_case(simple, "std_logic"))
                        && !in("UX01ZWLH-"))) {
                    report("FSIM-ELAB-VHQUAL-005",
                        "the operand of a VHDL qualified expression does not have "
                        "the type of its type mark",
                        expression.source);
                    return;
                }
            }
            // A constrained predefined STRING qualification is legal only
            // when the operand has the constrained subtype's exact length.
            // Check the fully static case while the type identity and bounds
            // are both available.
            const auto effective_qualified_type
                = semantic::CompiledDesignResolver { specialization_ }
                      .effective_vhdl_subtype(subtype, expression.scope);
            std::optional<std::string> qualified_array_identity;
            if (effective_qualified_type) {
                qualified_array_identity = predefined_array_identity(
                    *effective_qualified_type, expression.scope);
            }
            if (effective_qualified_type
                && !effective_qualified_type->constraints.empty()
                && qualified_array_identity
                && *qualified_array_identity == "std.standard.string") {
                const auto value
                    = specialization_.evaluate_string_expression(
                        expression.operands.front());
                if (value
                    && effective_qualified_type->constraints.size() == 1U
                    && effective_qualified_type->constraints.front().kind
                        == semantic::vhdl::RangeKind::array_index) {
                    const auto& constraint
                        = effective_qualified_type->constraints.front();
                    const auto left = bound(
                        constraint.left, constraint.left_expression);
                    const auto right = bound(
                        constraint.right, constraint.right_expression);
                    if (left && right
                        && (!constraint.direction_from_bounds
                            || *left == *right)) {
                        const bool null_range
                            = !constraint.direction_from_bounds
                            && (constraint.null
                                || (constraint.descending
                                    ? *left < *right
                                    : *left > *right));
                        const auto extent = null_range
                            ? std::optional<std::uint64_t> { 0U }
                            : inclusive_extent(std::min(*left, *right),
                                  std::max(*left, *right));
                        if (extent && *extent != value->size()) {
                            report(
                                "FSIM-ELAB-VHQUAL-003",
                                "a VHDL qualified STRING operand does not "
                                "have the length required by its subtype",
                                expression.source);
                            return;
                        }
                    }
                }
            }
            const auto comparable = scalar_class(expected.kind)
                || expected.kind == OperandClass::record;
            if (comparable && actual.kind != OperandClass::unknown
                && actual.kind != OperandClass::null
                && types_conflict(actual, expected)) {
                report("FSIM-ELAB-VHQUAL-005",
                    "the operand of a VHDL qualified expression does not have "
                    "the type of its type mark",
                    expression.source);
            }
            return;
        }
        if (expression.text.starts_with('@') || !expression.referenced_name) {
            return;
        }
        const semantic::CompiledDeclarationPredicate type_declaration
            = [](const semantic::CompiledDeclarationView& candidate) {
                  using Form = semantic::vhdl::DeclarationForm;
                  return candidate.vhdl != nullptr
                      && (candidate.vhdl->form == Form::type
                          || candidate.vhdl->form == Form::subtype);
              };
        if (!semantic::CompiledDesignResolver { specialization_ }
                 .resolve_vhdl(*expression.referenced_name, expression.scope,
                     type_declaration)
                 .unique()) {
            return;
        }
        validate_array_conversion(expression);
        const auto operand = specialization_.find_expression(expression.operands.front());
        if (expression.operands.size() > 1U
            || (operand && operand->vhdl != nullptr
            && (operand->vhdl->kind == semantic::vhdl::ExpressionKind::string_literal
                || operand->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
                || operand->vhdl->text == "@vhdl-null"
                || operand->vhdl->text.starts_with("@vhdl-new")))) {
            report("FSIM-ELAB-VHCONV-005",
                "the operand of a VHDL type conversion cannot be a string "
                "literal, aggregate, allocator or null",
                expression.source);
            return;
        }
        // The operand's type is determined without the context: an
        // enumeration literal of two visible types is ambiguous.
        if (operand && operand->vhdl != nullptr
            && operand->vhdl->kind == semantic::vhdl::ExpressionKind::name
            && operand->vhdl->text.find('.') == std::string::npos) {
            const auto& scopes = specialization_.design().semantics.scopes();
            const auto visible_here = [&](const semantic::ScopeId declared) {
                for (auto scope = expression.scope; scope.valid() && scope.value() < scopes.size();) {
                    if (scope == declared) {
                        return true;
                    }
                    if (!scopes[scope.value()].parent) {
                        break;
                    }
                    scope = *scopes[scope.value()].parent;
                }
                return false;
            };
            std::size_t owners { };
            for (const auto& type : specialization_.design().vhdl_hir.types()) {
                const auto declaration = specialization_.find_declaration(type.declaration);
                if (declaration && declaration->vhdl != nullptr
                    && visible_here(declaration->vhdl->scope)
                    && std::ranges::any_of(type.enumeration_literals, [&](const auto& literal) {
                           return equal_ignoring_ascii_case(literal.spelling, operand->vhdl->text);
                       })) {
                    ++owners;
                }
            }
            if (owners > 1U) {
                report("FSIM-ELAB-VHCONV-005",
                    "the operand of a VHDL type conversion is an enumeration literal of "
                    "more than one type",
                    expression.source);
            }
        }
    }

    bool locally_static_subtype(
        const SubtypeIndication& subtype,
        const semantic::ScopeId scope,
        std::unordered_set<std::uint32_t>& active_expressions,
        std::unordered_set<std::uint32_t>& active_declarations,
        const std::size_t depth) const
    {
        if (depth > 64U) {
            return false;
        }
        const auto range_is_static = [&](const RangeConstraint& constraint) {
            return (constraint.left_expression
                       ? locally_static_expression(
                             *constraint.left_expression,
                             active_expressions, active_declarations,
                             depth + 1U)
                       : constraint.left.has_value())
                && (constraint.right_expression
                    ? locally_static_expression(
                          *constraint.right_expression,
                          active_expressions, active_declarations,
                          depth + 1U)
                    : constraint.right.has_value());
        };
        const auto constraints_are_static = [&](const auto& candidate) {
            return std::ranges::all_of(
                       candidate.constraints, range_is_static)
                && std::ranges::all_of(
                    candidate.element_constraints,
                    [&](const auto& element) {
                        return std::ranges::all_of(
                            element.constraints, range_is_static);
                    });
        };
        const auto& design = specialization_.design();
        const auto source_resolver = semantic::CompiledDesignResolver {
            design, specialization_.unit()
        };
        const auto source_expression_depends_on_generic
            = [&](const auto& self, const semantic::ExpressionId id,
                  std::unordered_set<std::uint32_t>& active_expressions,
                  std::unordered_set<std::uint32_t>& active_declarations)
            -> bool {
            if (active_expressions.size() >= 64U
                || !active_expressions.insert(id.value()).second) {
                return true;
            }
            const auto expression = design.find_expression(id);
            if (!expression || expression->vhdl == nullptr) {
                active_expressions.erase(id.value());
                return true;
            }
            const auto& value = *expression->vhdl;
            using Kind = semantic::vhdl::ExpressionKind;
            using Form = semantic::vhdl::DeclarationForm;
            if (value.kind == Kind::name) {
                const auto declaration_id = value.referenced_name
                        && value.referenced_name->selected
                    ? value.referenced_name->selected
                    : source_resolver.resolve_expression_name(id).unique();
                if (declaration_id
                    && !active_declarations.insert(
                                               declaration_id->value())
                        .second) {
                    active_expressions.erase(id.value());
                    return true;
                }
                if (declaration_id) {
                    const auto declaration
                        = design.find_declaration(*declaration_id);
                    if (declaration && declaration->vhdl != nullptr) {
                        const auto& stored = *declaration->vhdl;
                        if (stored.form == Form::generic_constant) {
                            active_declarations.erase(
                                declaration_id->value());
                            active_expressions.erase(id.value());
                            return true;
                        }
                        if (stored.form == Form::constant
                            && stored.initializer
                            && self(self, *stored.initializer,
                                active_expressions, active_declarations)) {
                            active_declarations.erase(
                                declaration_id->value());
                            active_expressions.erase(id.value());
                            return true;
                        }
                    }
                    active_declarations.erase(declaration_id->value());
                }
            }
            const bool operand_depends = std::ranges::any_of(
                value.operands, [&](const semantic::ExpressionId operand) {
                    return self(self, operand, active_expressions,
                        active_declarations);
                });
            const bool association_depends = std::ranges::any_of(
                value.associations, [&](const auto& association) {
                    return self(self, association.value, active_expressions,
                               active_declarations)
                        || std::ranges::any_of(
                            association.choices,
                            [&](const semantic::ExpressionId choice) {
                                return self(self, choice, active_expressions,
                                    active_declarations);
                            });
                });
            active_expressions.erase(id.value());
            return operand_depends || association_depends;
        };
        const auto source_range_is_generic_free
            = [&](const RangeConstraint& constraint) {
                  std::unordered_set<std::uint32_t> active_expressions;
                  std::unordered_set<std::uint32_t> active_declarations;
                  return (!constraint.left_expression
                             || !source_expression_depends_on_generic(
                                 source_expression_depends_on_generic,
                                 *constraint.left_expression,
                                 active_expressions,
                                 active_declarations))
                      && (!constraint.right_expression
                          || !source_expression_depends_on_generic(
                              source_expression_depends_on_generic,
                              *constraint.right_expression,
                              active_expressions,
                              active_declarations));
              };
        const auto source_constraints_are_generic_free
            = [&](const auto& candidate) {
                  return std::ranges::all_of(
                             candidate.constraints,
                             source_range_is_generic_free)
                      && std::ranges::all_of(
                          candidate.element_constraints,
                          [&](const auto& element) {
                              return std::ranges::all_of(
                                  element.constraints,
                                  source_range_is_generic_free);
                          });
              };
        // Check every source declaration in the subtype/alias chain before
        // consulting the specialized view. That view can fold generic bounds.
        if (!constraints_are_static(subtype)
            || !source_constraints_are_generic_free(subtype)) {
            return false;
        }
        auto type_id = source_resolver.resolve_vhdl_named_type(
            subtype.type_mark.spelling, scope);
        if (!type_id && subtype.type_mark.target.valid()) {
            type_id = subtype.type_mark.target;
        }
        std::unordered_set<std::uint32_t> active_types;
        std::size_t type_depth { };
        while (type_id) {
            if (type_depth++ >= 64U) {
                return false;
            }
            if (!active_types.insert(type_id->value()).second) {
                return false;
            }
            const auto source_type = design.find_type(*type_id);
            const auto* definition
                = source_type && source_type->vhdl != nullptr
                ? source_type->vhdl
                : nullptr;
            if (definition == nullptr) {
                const auto* specialized_definition
                    = type_definition(*type_id);
                if (specialized_definition == nullptr
                    || specialized_definition->form == TypeForm::subtype
                    || specialized_definition->form == TypeForm::alias) {
                    return false;
                }
                break;
            }
            const auto declaration = design.find_declaration(
                definition->declaration);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::generic_type) {
                return false;
            }
            if (!constraints_are_static(definition->base)
                || !source_constraints_are_generic_free(
                    definition->base)
                || (definition->scalar_range
                    && (!range_is_static(*definition->scalar_range)
                        || !source_range_is_generic_free(
                            *definition->scalar_range)))) {
                return false;
            }
            if (definition->form != TypeForm::subtype
                && definition->form != TypeForm::alias) {
                break;
            }
            const auto next = definition->base.type_mark.target;
            type_id = next.valid() ? std::optional { next } : std::nullopt;
        }
        const auto effective
            = semantic::CompiledDesignResolver { specialization_ }
                  .effective_vhdl_subtype(subtype, scope);
        return effective && !effective->unconstrained
            && constraints_are_static(*effective);
    }

    bool vhdl_package_owner_matches(
        const semantic::DeclarationId declaration_id,
        const std::string_view library,
        const std::string_view package) const
    {
        const auto declaration
            = specialization_.find_declaration(declaration_id);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->scope.valid()) {
            return false;
        }
        return specialization_.design().vhdl_scope_in_package(
            declaration->vhdl->scope, library, package);
    }

    bool declaration_is_ieee_function(
        const semantic::DeclarationId id,
        const std::string_view package) const
    {
        const auto declaration = specialization_.find_declaration(id);
        return declaration && declaration->vhdl != nullptr
            && declaration->vhdl->callable
            && declaration->vhdl->callable->function
            && declaration->vhdl->callable->pure
            && vhdl_package_owner_matches(id, "ieee", package);
    }

    bool exact_ieee_callable(
        const semantic::CompiledVhdlCallableResolution& candidate) const
    {
        for (const auto package : {
                 std::string_view { "std_logic_1164" },
                 std::string_view { "numeric_bit" },
                 std::string_view { "numeric_std" },
                 std::string_view { "numeric_bit_unsigned" },
                 std::string_view { "numeric_std_unsigned" },
             }) {
            if (declaration_is_ieee_function(candidate.key, package)
                && declaration_is_ieee_function(candidate.body, package)) {
                return true;
            }
        }
        return false;
    }

    bool ieee_callable_arity_matches(
        const semantic::CompiledVhdlCallableResolution& candidate,
        const std::size_t arity) const
    {
        for (const auto id : { candidate.key, candidate.body }) {
            const auto declaration = specialization_.find_declaration(id);
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->callable
                && declaration->vhdl->callable->function) {
                return declaration->vhdl->callable->formals.size() == arity;
            }
        }
        return false;
    }

    bool exact_imported_ieee_member(
        const semantic::vhdl::Name& name,
        const semantic::ScopeId scope) const
    {
        if (name.selected || !name.overloads.empty()) {
            return false;
        }
        auto spelling = std::string_view { name.spelling };
        spelling = semantic::vhdl::without_builtin_prefix(spelling);
        const auto first_separator = spelling.find_first_of(".:");
        const auto last_separator = spelling.find_last_of(".:");
        const auto is_package = [](const std::string_view package) {
            return equal_ignoring_ascii_case(package, "std_logic_1164")
                || equal_ignoring_ascii_case(package, "numeric_bit")
                || equal_ignoring_ascii_case(package, "numeric_std")
                || equal_ignoring_ascii_case(
                    package, "numeric_bit_unsigned")
                || equal_ignoring_ascii_case(
                    package, "numeric_std_unsigned");
        };
        const auto resolver
            = semantic::CompiledDesignResolver { specialization_ };
        if (first_separator != std::string_view::npos
            && last_separator != first_separator) {
            const auto library = spelling.substr(0U, first_separator);
            const auto package = spelling.substr(
                first_separator + 1U,
                last_separator - first_separator - 1U);
            if (equal_ignoring_ascii_case(library, "ieee")
                && is_package(package)
                && resolver.vhdl_standard_package_member_visible(name, scope)) {
                return true;
            }
        }
        const auto member = last_separator == std::string_view::npos
            ? spelling
            : spelling.substr(last_separator + 1U);
        for (const auto package : {
                 std::string_view { "std_logic_1164" },
                 std::string_view { "numeric_bit" },
                 std::string_view { "numeric_std" },
                 std::string_view { "numeric_bit_unsigned" },
                 std::string_view { "numeric_std_unsigned" },
             }) {
            if (resolver.vhdl_builtin_package_member_imported(
                    "ieee", package, member, scope)) {
                return true;
            }
        }
        return false;
    }

    bool locally_static_operator(
        const semantic::vhdl::Expression& expression) const
    {
        semantic::vhdl::Name operation_name;
        if (expression.referenced_name) {
            operation_name = *expression.referenced_name;
        } else {
            operation_name.spelling = expression.text;
            operation_name.canonical = expression.text;
        }
        const auto resolver
            = semantic::CompiledDesignResolver { specialization_ };
        const auto resolutions = resolver.resolve_vhdl_callables(
            operation_name, expression.scope);
        if (resolutions.candidates.size() == 1U) {
            const auto& candidate = resolutions.candidates.front();
            const auto arity
                = expression.kind == semantic::vhdl::ExpressionKind::unary
                ? std::size_t { 1U }
                : std::size_t { 2U };
            return exact_ieee_callable(candidate)
                && ieee_callable_arity_matches(candidate, arity);
        }
        if (!resolutions.candidates.empty() || operation_name.selected
            || !operation_name.overloads.empty()) {
            return false;
        }
        if (!resolver.resolve_vhdl_callable_candidates(
                         operation_name, expression.scope)
                .candidates.empty()) {
            return false;
        }
        if (expression.builtin_operator
            != semantic::vhdl::BuiltinOperatorIdentity::none) {
            return resolver.vhdl_builtin_package_member_imported(
                "ieee", "std_logic_1164", expression.text,
                expression.scope);
        }
        if (exact_imported_ieee_member(operation_name, expression.scope)) {
            return true;
        }
        const auto is_operator = [&](const auto operators) {
            return std::ranges::any_of(
                operators, [&](const std::string_view op) {
                    return equal_ignoring_ascii_case(expression.text, op);
                });
        };
        if (expression.kind == semantic::vhdl::ExpressionKind::unary) {
            constexpr auto implicit_unary = std::array<std::string_view, 4> {
                "+", "-", "abs", "not"
            };
            return is_operator(implicit_unary);
        }
        if (expression.kind == semantic::vhdl::ExpressionKind::binary) {
            constexpr auto implicit_binary
                = std::array<std::string_view, 20> {
                      "+", "-", "*", "/", "mod", "rem", "**", "=",
                      "/=", "<", "<=", ">", ">=", "and", "or",
                      "nand", "nor", "xor", "xnor", "&"
                  };
            return is_operator(implicit_binary);
        }
        return false;
    }


    bool locally_static_expression(
        const semantic::ExpressionId id,
        std::unordered_set<std::uint32_t>& active_expressions,
        std::unordered_set<std::uint32_t>& active_declarations,
        const std::size_t depth = 0U) const
    {
        if (depth > 64U
            || !active_expressions.insert(id.value()).second) {
            return false;
        }
        const auto finish = [&](const bool result) {
            active_expressions.erase(id.value());
            return result;
        };
        const auto view = specialization_.find_expression(id);
        if (!view || view->vhdl == nullptr) {
            return finish(false);
        }
        const auto& expression = *view->vhdl;
        const auto all_operands_static = [&] {
            return std::ranges::all_of(
                expression.operands,
                [&](const semantic::ExpressionId operand) {
                    return locally_static_expression(
                        operand, active_expressions,
                        active_declarations, depth + 1U);
                });
        };
        using Kind = semantic::vhdl::ExpressionKind;
        using Form = semantic::vhdl::DeclarationForm;
        switch (expression.kind) {
        case Kind::integer_literal:
        case Kind::real_literal:
        case Kind::boolean_literal:
        case Kind::logic_literal:
        case Kind::string_literal:
            return finish(!equal_ignoring_ascii_case(
                expression.nominal_type, "time"));
        case Kind::name: {
            const auto declaration_id = expression.referenced_name
                    && expression.referenced_name->selected
                ? expression.referenced_name->selected
                : semantic::CompiledDesignResolver { specialization_ }
                      .resolve_expression_name(id)
                      .unique();
            if (!declaration_id
                || !active_declarations.insert(
                                           declaration_id->value())
                    .second) {
                return finish(false);
            }
            const auto declaration
                = specialization_.find_declaration(*declaration_id);
            bool result { };
            if (declaration && declaration->vhdl != nullptr) {
                const auto& stored = *declaration->vhdl;
                if (stored.form == Form::enumeration_literal) {
                    result = true;
                } else if (stored.form == Form::constant
                    && !stored.deferred && stored.initializer
                    && stored.subtype) {
                    result = locally_static_subtype(
                                 *stored.subtype, stored.scope,
                                 active_expressions, active_declarations,
                                 depth + 1U)
                        && locally_static_expression(
                            *stored.initializer, active_expressions,
                            active_declarations, depth + 1U);
                }
            }
            active_declarations.erase(declaration_id->value());
            return finish(result);
        }
        // Validator::predefined_operator rejects overloaded user-defined
        // operators unless they are the exact pure IEEE declarations.
        case Kind::unary:
            return finish(expression.operands.size() == 1U
                && locally_static_operator(expression)
                && all_operands_static());
        case Kind::binary: {
            if (expression.operands.size() != 2U) {
                return finish(false);
            }
            if (equal_ignoring_ascii_case(expression.text, "to")
                || equal_ignoring_ascii_case(expression.text, "downto")) {
                // Aggregate discrete ranges are encoded as binary nodes, but
                // their direction keyword is range syntax rather than a
                // callable operator. Only their bounds need to be locally
                // static.
                const auto& direction = expression.referenced_name;
                const auto resolver
                    = semantic::CompiledDesignResolver { specialization_ };
                const bool unbound_range_direction = direction
                    && equal_ignoring_ascii_case(
                        direction->spelling, expression.text)
                    && equal_ignoring_ascii_case(
                        direction->text(), expression.text)
                    && !direction->selected && direction->overloads.empty()
                    && expression.builtin_operator
                        == semantic::vhdl::BuiltinOperatorIdentity::none
                    && resolver
                           .resolve_vhdl_callables(
                               *direction, expression.scope)
                           .candidates.empty()
                    && resolver
                           .resolve_vhdl_callable_candidates(
                               *direction, expression.scope)
                           .candidates.empty();
                return finish(unbound_range_direction && all_operands_static());
            }
            return finish(locally_static_operator(expression)
                && all_operands_static());
        }
        case Kind::call: {
            constexpr auto qualified_prefix
                = semantic::vhdl::qualified_expression_prefix;
            const auto conversion_subtype = [&](const auto& name,
                                                const semantic::ScopeId scope)
                -> std::optional<SubtypeIndication> {
                const auto selected = name.selected
                    ? name.selected
                    : name.overloads.size() == 1U
                    ? std::optional { name.overloads.front() }
                    : std::nullopt;
                if (selected) {
                    const auto declaration
                        = specialization_.find_declaration(*selected);
                    if (!declaration || declaration->vhdl == nullptr
                        || (declaration->vhdl->form != Form::type
                            && declaration->vhdl->form != Form::subtype)) {
                        return std::nullopt;
                    }
                    if (declaration->vhdl->subtype) {
                        return declaration->vhdl->subtype;
                    }
                    if (declaration->vhdl->declared_type) {
                        const auto* type = type_definition(
                            *declaration->vhdl->declared_type);
                        if (type != nullptr) {
                            auto result = type->base;
                            result.type_mark.target
                                = *declaration->vhdl->declared_type;
                            return result;
                        }
                    }
                    return std::nullopt;
                }
                const auto type_id
                    = semantic::CompiledDesignResolver { specialization_ }
                          .resolve_vhdl_named_type(name.spelling, scope);
                const auto* type = type_id
                    ? type_definition(*type_id)
                    : nullptr;
                if (type == nullptr) {
                    return std::nullopt;
                }
                auto result = type->base;
                result.type_mark.target = *type_id;
                return result;
            };
            if (expression.text.starts_with(qualified_prefix)
                && expression.operands.size() == 1U) {
                semantic::vhdl::Name type_name;
                type_name.spelling
                    = expression.text.substr(qualified_prefix.size());
                type_name.canonical = type_name.spelling;
                const auto target = conversion_subtype(
                    type_name, expression.scope);
                return finish(target
                    && locally_static_subtype(
                        *target, expression.scope, active_expressions,
                        active_declarations, depth + 1U)
                    && locally_static_expression(
                        expression.operands.front(), active_expressions,
                        active_declarations, depth + 1U));
            }
            if (equal_ignoring_ascii_case(expression.text, "'left")
                || equal_ignoring_ascii_case(expression.text, "'right")
                || equal_ignoring_ascii_case(expression.text, "'low")
                || equal_ignoring_ascii_case(expression.text, "'high")
                || equal_ignoring_ascii_case(expression.text, "'length")
                || equal_ignoring_ascii_case(
                    expression.text, "'ascending")) {
                if (expression.operands.empty()) {
                    return finish(false);
                }
                const auto prefix = expression_subtype(
                    expression.operands.front());
                if (!prefix
                    || !locally_static_subtype(
                        *prefix, expression.scope, active_expressions,
                        active_declarations, depth + 1U)) {
                    return finish(false);
                }
                return finish(std::ranges::all_of(
                    std::span { expression.operands }.subspan(1U),
                    [&](const semantic::ExpressionId operand) {
                        return locally_static_expression(
                            operand, active_expressions,
                            active_declarations, depth + 1U);
                    }));
            }
            if (!expression.referenced_name) {
                return finish(false);
            }
            const auto resolver
                = semantic::CompiledDesignResolver { specialization_ };
            // Only a one-operand call may be a type conversion. Preserve the
            // original arity for exact IEEE callable matching below.
            if (expression.operands.size() == 1U) {
                const auto target = conversion_subtype(
                    *expression.referenced_name, expression.scope);
                if (target) {
                    return finish(locally_static_subtype(
                                      *target, expression.scope,
                                      active_expressions,
                                      active_declarations, depth + 1U)
                        && locally_static_expression(
                            expression.operands.front(),
                            active_expressions, active_declarations,
                            depth + 1U));
                }
            }
            const auto resolutions = resolver.resolve_vhdl_callables(
                *expression.referenced_name, expression.scope);
            if (resolutions.candidates.size() == 1U) {
                const auto& candidate = resolutions.candidates.front();
                return finish(exact_ieee_callable(candidate)
                    && ieee_callable_arity_matches(
                        candidate, expression.operands.size())
                    && all_operands_static());
            }
            if (!resolutions.candidates.empty()
                || !resolver.resolve_vhdl_callable_candidates(
                                *expression.referenced_name,
                                expression.scope)
                    .candidates.empty()
                || expression.referenced_name->selected
                || !expression.referenced_name->overloads.empty()) {
                return finish(false);
            }
            return finish(exact_imported_ieee_member(
                              *expression.referenced_name,
                              expression.scope)
                && all_operands_static());
        }
        case Kind::index:
            return finish(expression.operands.size() >= 2U
                && all_operands_static());
        case Kind::slice:
            return finish(expression.operands.size() == 3U
                && all_operands_static());
        case Kind::concatenation:
            return finish(!expression.operands.empty()
                && all_operands_static());
        case Kind::aggregate:
            return finish(std::ranges::all_of(
                expression.associations, [&](const auto& association) {
                    return locally_static_expression(
                               association.value, active_expressions,
                               active_declarations, depth + 1U)
                        && (equal_ignoring_ascii_case(
                                association.choice_spelling, "others")
                            || std::ranges::all_of(
                                association.choices,
                                [&](const semantic::ExpressionId choice) {
                                    return locally_static_expression(
                                        choice, active_expressions,
                                        active_declarations,
                                        depth + 1U);
                                }));
                }));
        default:
            return finish(false);
        }
    }

    bool aggregate_target_name_is_locally_static(
        const semantic::ExpressionId id,
        const std::size_t depth = 0U) const
    {
        if (depth > 64U) {
            return false;
        }
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return false;
        }
        const auto& value = *expression->vhdl;
        using Kind = semantic::vhdl::ExpressionKind;
        if (value.kind == Kind::aggregate) {
            return std::ranges::all_of(
                value.associations, [&](const auto& association) {
                    return aggregate_target_name_is_locally_static(
                        association.value, depth + 1U);
                });
        }
        if (value.kind == Kind::name) {
            return true;
        }
        if (value.kind == Kind::index && value.operands.size() >= 2U) {
            return std::ranges::all_of(
                       std::span { value.operands }.subspan(1U),
                       [&](const semantic::ExpressionId index) {
                           return locally_static(index);
                       })
                && aggregate_target_name_is_locally_static(
                    value.operands.front(), depth + 1U);
        }
        if (value.kind == Kind::slice && value.operands.size() == 3U) {
            return locally_static(value.operands[1])
                && locally_static(value.operands[2])
                && aggregate_target_name_is_locally_static(
                    value.operands.front(), depth + 1U);
        }
        if (value.kind == Kind::call
            && value.text.starts_with("@vhdl-member:")
            && value.operands.size() == 1U) {
            return aggregate_target_name_is_locally_static(
                value.operands.front(), depth + 1U);
        }
        return false;
    }

    // IEEE 1076-2008 10.5.2.1: the target of a signal assignment denotes a
    // signal.
    void validate_signal_target(semantic::ExpressionId target_id,
        const semantic::SourceSpanId source,
        const bool check_aggregate_names = true)
    {
        if (const auto aggregate = specialization_.find_expression(target_id);
            aggregate && aggregate->vhdl != nullptr
            && aggregate->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate) {
            if (check_aggregate_names
                && !aggregate_target_name_is_locally_static(target_id)) {
                report(
                    "FSIM-ELAB-VHASSIGN-005",
                    "each element association of a VHDL signal aggregate "
                    "assignment target must be a locally static name "
                    "(IEEE 1076-2008 10.5.2.1)",
                    aggregate->vhdl->source.valid()
                        ? aggregate->vhdl->source
                        : source);
            }
            for (const auto& association : aggregate->vhdl->associations) {
                validate_signal_target(association.value, source, false);
            }
            return;
        }
        // An indexed or sliced target denotes part of its prefix.
        for (std::size_t depth { }; depth < 8U; ++depth) {
            const auto part = specialization_.find_expression(target_id);
            if (!part || part->vhdl == nullptr
                || (part->vhdl->kind != semantic::vhdl::ExpressionKind::index
                    && part->vhdl->kind != semantic::vhdl::ExpressionKind::slice)
                || part->vhdl->operands.empty()) {
                break;
            }
            target_id = part->vhdl->operands.front();
        }
        const auto target = specialization_.find_expression(target_id);
        if (!target || target->vhdl == nullptr
            || target->vhdl->kind != semantic::vhdl::ExpressionKind::name
            || !target->vhdl->referenced_name
            || target->vhdl->text.find('.') != std::string::npos) {
            return;
        }
        const auto selected = target->vhdl->referenced_name->selected
            ? target->vhdl->referenced_name->selected
            : semantic::CompiledDesignResolver { specialization_ }
                  .resolve_vhdl(*target->vhdl->referenced_name, target->vhdl->scope)
                  .unique();
        if (!selected && (generate_parameter_name(target->vhdl->text)
                || physical_unit_name(target->vhdl->text))) {
            report("FSIM-ELAB-VHASSIGN-002",
                "the target of a VHDL signal assignment must be a signal",
                target->vhdl->source.valid() ? target->vhdl->source : source);
            return;
        }
        const auto declaration = selected
            ? specialization_.find_declaration(*selected)
            : std::nullopt;
        using Form = semantic::vhdl::DeclarationForm;
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->form != Form::alias
            && declaration->vhdl->form != Form::signal
            && (declaration->vhdl->object_class == semantic::vhdl::ObjectClass::variable
                || declaration->vhdl->object_class == semantic::vhdl::ObjectClass::constant)) {
            report("FSIM-ELAB-VHASSIGN-002",
                "the target of a VHDL signal assignment must be a signal",
                target->vhdl->source.valid() ? target->vhdl->source : source);
        }
    }

    // IEEE 1076-2008 4.2.2.2: the attributes 'STABLE, 'QUIET, 'TRANSACTION
    // and 'DELAYED of a formal signal parameter cannot be read.
    // Before VHDL-2008 an out port is not read: its signal attributes and
    // its use in a sensitivity list are errors (IEEE 1076-1993 1.1.1.2).
    [[nodiscard]] bool out_port_name(const semantic::ExpressionId id) const
    {
        return port_name(id, semantic::vhdl::Direction::output);
    }

    [[nodiscard]] bool linkage_port_name(const semantic::ExpressionId id) const
    {
        return port_name(id, semantic::vhdl::Direction::inout, true);
    }

    [[nodiscard]] bool port_name(const semantic::ExpressionId id,
        const semantic::vhdl::Direction direction, const bool linkage = false) const
    {
        const auto prefix = specialization_.find_expression(id);
        if (!prefix || prefix->vhdl == nullptr || !prefix->vhdl->referenced_name) {
            return false;
        }
        const auto selected = prefix->vhdl->referenced_name->selected
            ? prefix->vhdl->referenced_name->selected
            : semantic::CompiledDesignResolver { specialization_ }
                  .resolve_vhdl(*prefix->vhdl->referenced_name, prefix->vhdl->scope)
                  .unique();
        const auto declaration = selected
            ? specialization_.find_declaration(*selected)
            : std::nullopt;
        return declaration && declaration->vhdl != nullptr
            && declaration->vhdl->form == semantic::vhdl::DeclarationForm::port
            && declaration->vhdl->direction == direction
            && (!linkage || declaration->vhdl->linkage);
    }

    [[nodiscard]] bool before_2008() const
    {
        return architecture_.standard == "1987" || architecture_.standard == "1993"
            || architecture_.standard == "2000" || architecture_.standard == "2002";
    }

    // Whether an expression reads a port of mode out, other than through an
    // attribute.
    bool reads_out_port(const semantic::ExpressionId id, const std::size_t depth) const
    {
        const auto found = specialization_.find_expression(id);
        if (!found || found->vhdl == nullptr || depth > 32U) {
            return false;
        }
        if (found->vhdl->kind == semantic::vhdl::ExpressionKind::call
            && found->vhdl->text.find('\'') != std::string::npos) {
            return false;
        }
        if (out_port_name(id)) {
            return true;
        }
        return std::ranges::any_of(found->vhdl->operands, [&](const semantic::ExpressionId operand) {
            return reads_out_port(operand, depth + 1U);
        });
    }

    void validate_out_port_attribute(const semantic::vhdl::Expression& expression)
    {
        if (expression.kind != semantic::vhdl::ExpressionKind::call
            || expression.operands.empty()) {
            return;
        }
        static constexpr std::array<std::string_view, 9> attributes { "'stable", "'quiet",
            "'delayed", "'transaction", "'event", "'active", "'last_event", "'last_active",
            "'last_value" };
        if (std::ranges::none_of(attributes, [&](const std::string_view attribute) {
                return equal_ignoring_ascii_case(expression.text, attribute);
            })) {
            return;
        }
        // The value of a linkage port is not read, including through its
        // signal attributes (IEEE 1076-1993 4.3.2).
        if (linkage_port_name(expression.operands.front())) {
            report("FSIM-ELAB-VHPORT-003",
                "the attribute " + expression.text
                    + " of a port of mode linkage cannot be read",
                expression.source);
            return;
        }
        if (before_2008() && out_port_name(expression.operands.front())) {
            report("FSIM-ELAB-VHPORT-003",
                "the attribute " + expression.text
                    + " of a port of mode out cannot be read before VHDL-2008",
                expression.source);
        }
    }

    void validate_out_port_sensitivity(const std::vector<semantic::vhdl::Sensitivity>& list,
        const semantic::ScopeId scope)
    {
        if (!before_2008()) {
            return;
        }
        for (const auto& entry : list) {
            // 10.2: a sensitivity list names signals statically.
            if (entry.expression) {
                const auto named = specialization_.find_expression(*entry.expression);
                if (named && named->vhdl != nullptr) {
                    const auto& value = *named->vhdl;
                    using Kind = semantic::vhdl::ExpressionKind;
                    bool static_name = value.kind == Kind::name
                        || (value.kind == Kind::call
                            && value.text.starts_with("@vhdl-member:"));
                    if (value.kind == Kind::index || value.kind == Kind::slice) {
                        // Index expressions of a static name are globally
                        // static: literals, constants and generics, and
                        // operators over them (IEEE 1076-2008 8.1, 9.4.3).
                        const auto globally_static = [&](const auto& self,
                                                         const semantic::ExpressionId id,
                                                         const std::size_t depth) -> bool {
                            if (specialization_.evaluate_integral_expression(id)) {
                                return true;
                            }
                            const auto operand = specialization_.find_expression(id);
                            if (depth > 16U || !operand || operand->vhdl == nullptr) {
                                return false;
                            }
                            const auto& node = *operand->vhdl;
                            if (node.kind == Kind::name && node.referenced_name
                                && node.referenced_name->selected) {
                                const auto declaration = specialization_.find_declaration(
                                    *node.referenced_name->selected);
                                if (!declaration || declaration->vhdl == nullptr) {
                                    return false;
                                }
                                // A loop parameter is a constant that is not
                                // globally static (IEEE 1076-2008 9.4.3).
                                const auto& scopes
                                    = specialization_.design().semantics.scopes();
                                const auto scope = declaration->vhdl->scope;
                                if (scope.valid() && scope.value() < scopes.size()
                                    && scopes[scope.value()].name == "<loop>") {
                                    return false;
                                }
                                return declaration->vhdl->form
                                        == semantic::vhdl::DeclarationForm::generic_constant
                                    || declaration->vhdl->form
                                        == semantic::vhdl::DeclarationForm::constant;
                            }
                            return (node.kind == Kind::binary || node.kind == Kind::unary)
                                && !node.operands.empty()
                                && std::ranges::all_of(node.operands,
                                    [&](const semantic::ExpressionId child) {
                                        return self(self, child, depth + 1U);
                                    });
                        };
                        static_name = std::ranges::all_of(
                            std::span { value.operands }.subspan(1U),
                            [&](const semantic::ExpressionId operand) {
                                return globally_static(globally_static, operand, 0U);
                            });
                    }
                    if (!static_name
                        && (value.kind == Kind::aggregate
                            || value.kind == Kind::index
                            || value.kind == Kind::slice
                            || value.text.starts_with(
                                semantic::vhdl::qualified_expression_prefix)
                            || value.kind == Kind::string_literal)) {
                        report("FSIM-ELAB-VHSENS-001",
                            "a VHDL sensitivity list names signals with static names",
                            entry.source);
                        continue;
                    }
                }
            }
            bool out_port = entry.expression && out_port_name(*entry.expression);
            if (!entry.expression && !entry.signal.empty()
                && entry.signal.find_first_of(".(") == std::string::npos) {
                semantic::vhdl::Name name;
                name.spelling = entry.signal;
                name.canonical = entry.signal;
                const auto selected = semantic::CompiledDesignResolver { specialization_ }
                                          .resolve_vhdl(name, scope)
                                          .unique();
                const auto declaration = selected
                    ? specialization_.find_declaration(*selected)
                    : std::nullopt;
                out_port = declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->form == semantic::vhdl::DeclarationForm::port
                    && declaration->vhdl->direction == semantic::vhdl::Direction::output;
            }
            if (out_port) {
                report("FSIM-ELAB-VHPORT-003",
                    "a port of mode out cannot be read in a sensitivity list "
                    "before VHDL-2008",
                    entry.source);
            }
        }
    }

    void validate_formal_signal_attribute(const semantic::vhdl::Expression& expression)
    {
        if (expression.kind != semantic::vhdl::ExpressionKind::call
            || expression.operands.empty()
            || !(equal_ignoring_ascii_case(expression.text, "'stable")
                || equal_ignoring_ascii_case(expression.text, "'quiet")
                || equal_ignoring_ascii_case(expression.text, "'transaction")
                || equal_ignoring_ascii_case(expression.text, "'delayed"))) {
            return;
        }
        if (!signal_formals_) {
            signal_formals_.emplace();
            for (const auto& declaration :
                specialization_.design().vhdl_hir.declarations()) {
                if (!declaration.callable) {
                    continue;
                }
                for (const auto formal : declaration.callable->formals) {
                    const auto record = specialization_.find_declaration(formal);
                    if (record && record->vhdl != nullptr
                        && record->vhdl->object_class
                            == semantic::vhdl::ObjectClass::signal) {
                        signal_formals_->insert(formal.value());
                    }
                }
            }
        }
        const auto prefix = specialization_.find_expression(expression.operands.front());
        if (!prefix || prefix->vhdl == nullptr || !prefix->vhdl->referenced_name) {
            return;
        }
        const auto selected = prefix->vhdl->referenced_name->selected
            ? prefix->vhdl->referenced_name->selected
            : semantic::CompiledDesignResolver { specialization_ }
                  .resolve_vhdl(*prefix->vhdl->referenced_name, prefix->vhdl->scope)
                  .unique();
        if (selected && signal_formals_->contains(selected->value())) {
            report("FSIM-ELAB-VHATTR-012",
                "the attribute " + expression.text
                    + " of a formal signal parameter cannot be read",
                expression.source);
        }
    }

    // IEEE 1076-2008 9.3.4, 4.2.2.1: each formal of the called function is
    // associated with exactly one actual, or has a default.
    // Whether a subprogram accepts actuals given as (formal name or empty
    // for positional, actual): every association names a formal once, each
    // formal without a default is associated, and a scalar actual has its
    // formal's type (IEEE 1076-2008 4.2.2.1, 6.5.7.1).
    bool callable_accepts(const semantic::DeclarationId body,
        const std::vector<std::pair<std::string, semantic::ExpressionId>>& actuals) const
    {
        const auto declaration = specialization_.find_declaration(body);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->callable) {
            return true;
        }
        const auto& formals = declaration->vhdl->callable->formals;
        std::vector<bool> associated(formals.size(), false);
        const auto type_matches = [&](const semantic::DeclarationId formal,
                                      const semantic::ExpressionId actual) {
            const auto formal_declaration = specialization_.find_declaration(formal);
            if (!formal_declaration || formal_declaration->vhdl == nullptr
                || !formal_declaration->vhdl->subtype) {
                return true;
            }
            // 4.2.2.3, 6.5.7.1: a signal formal takes a signal actual, and a
            // variable formal takes a variable (not a signal).
            if (const auto actual_expression = specialization_.find_expression(actual);
                actual_expression && actual_expression->vhdl != nullptr
                && actual_expression->vhdl->kind == semantic::vhdl::ExpressionKind::name
                && actual_expression->vhdl->referenced_name
                && actual_expression->vhdl->referenced_name->selected) {
                const auto object = specialization_.find_declaration(
                    *actual_expression->vhdl->referenced_name->selected);
                using Form = semantic::vhdl::DeclarationForm;
                using Class = semantic::vhdl::ObjectClass;
                if (object && object->vhdl != nullptr) {
                    const bool signal_object = object->vhdl->form == Form::signal
                        || (object->vhdl->form == Form::port
                            && object->vhdl->object_class == Class::signal);
                    const bool variable_object = object->vhdl->form == Form::variable
                        || (object->vhdl->form == Form::port
                            && object->vhdl->object_class == Class::variable);
                    const auto formal_class = formal_declaration->vhdl->object_class;
                    if ((formal_class == Class::variable && signal_object)
                        || (formal_class == Class::signal && variable_object)) {
                        return false;
                    }
                }
            }
            // 9.3.3.3: an aggregate with OTHERS needs a constrained context,
            // which an unconstrained formal does not give.
            if (const auto aggregate = specialization_.find_expression(actual);
                aggregate && aggregate->vhdl != nullptr
                && aggregate->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
                && unconstrained_array_subtype(*formal_declaration->vhdl->subtype)
                && std::ranges::any_of(aggregate->vhdl->associations,
                    [&](const semantic::vhdl::AggregateAssociation& association) {
                        return choice_is_others(association);
                    })) {
                return false;
            }
            const auto expected = subtype_operand_type(*formal_declaration->vhdl->subtype, 0U);
            const auto given = operand_type(actual);
            return !scalar_class(expected.kind) || !scalar_class(given.kind)
                || (!types_conflict(given, expected));
        };
        for (std::size_t index { }; index < actuals.size(); ++index) {
            const auto& [name, actual] = actuals[index];
            if (name.empty()) {
                if (index >= formals.size() || associated[index]) {
                    return false;
                }
                associated[index] = true;
                if (!type_matches(formals[index], actual)) {
                    return false;
                }
                continue;
            }
            const auto formal = std::ranges::find_if(formals, [&](const auto id) {
                const auto formal_declaration = specialization_.find_declaration(id);
                // A partial association names a subelement (`f.x => a`).
                const auto base = std::string_view { name }.substr(
                    0U, std::string_view { name }.find_first_of(".("));
                return formal_declaration && formal_declaration->vhdl != nullptr
                    && equal_ignoring_ascii_case(formal_declaration->vhdl->name, base);
            });
            if (formal == formals.end()) {
                return false;
            }
            const auto position = static_cast<std::size_t>(formal - formals.begin());
            if (associated[position]
                && name.find_first_of(".(") == std::string::npos) {
                return false;
            }
            associated[position] = true;
            if (name.find_first_of(".(") == std::string::npos
                && !type_matches(*formal, actual)) {
                return false;
            }
        }
        for (std::size_t index { }; index < formals.size(); ++index) {
            const auto formal_declaration = specialization_.find_declaration(formals[index]);
            if (!associated[index]
                && (!formal_declaration || formal_declaration->vhdl == nullptr
                    || !formal_declaration->vhdl->initializer)) {
                return false;
            }
        }
        return true;
    }

    void validate_aggregate_actuals(
        const semantic::DeclarationId body,
        const std::vector<std::pair<std::string, semantic::ExpressionId>>& actuals)
    {
        const auto declaration = specialization_.find_declaration(body);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->callable) {
            return;
        }
        const auto& formals = declaration->vhdl->callable->formals;
        std::vector<std::optional<semantic::ExpressionId>> associated(
            formals.size());
        std::size_t positional { };
        for (const auto& [name, actual] : actuals) {
            std::size_t index { };
            if (name.empty()) {
                index = positional++;
            } else {
                const auto base = std::string_view { name }.substr(
                    0U, std::string_view { name }.find_first_of(".("));
                const auto formal = std::ranges::find_if(
                    formals, [&](const auto id) {
                        const auto candidate
                            = specialization_.find_declaration(id);
                        return candidate && candidate->vhdl != nullptr
                            && equal_ignoring_ascii_case(
                                candidate->vhdl->name, base);
                    });
                if (formal == formals.end()
                    || name.find_first_of(".(") != std::string::npos) {
                    continue;
                }
                index = static_cast<std::size_t>(formal - formals.begin());
            }
            if (index < associated.size()) {
                associated[index] = actual;
            }
        }
        for (std::size_t index { }; index < associated.size(); ++index) {
            if (!associated[index]) {
                continue;
            }
            const auto actual = specialization_.find_expression(
                *associated[index]);
            const auto formal = specialization_.find_declaration(
                formals[index]);
            if (!actual || actual->vhdl == nullptr || formal == std::nullopt
                || formal->vhdl == nullptr || !formal->vhdl->subtype
                || actual->vhdl->kind
                    != semantic::vhdl::ExpressionKind::aggregate) {
                continue;
            }
            validate_aggregate(
                *associated[index], *formal->vhdl->subtype);
        }
    }

    // The procedure call statement form of validate_call_arity.
    void validate_procedure_call(const semantic::vhdl::Statement& statement)
    {
        if (statement.kind != semantic::vhdl::StatementKind::procedure_call
            || statement.procedure.spelling.empty()
            || statement.procedure.spelling.find_first_of(".'") != std::string::npos) {
            return;
        }
        const semantic::CompiledDesignResolver resolver { specialization_ };
        const auto visible = resolver.resolve_vhdl(statement.procedure, statement.scope);
        if (visible.candidates.empty()
            || !std::ranges::all_of(visible.candidates, [&](const auto id) {
                   const auto declaration = specialization_.find_declaration(id);
                   return declaration && declaration->vhdl != nullptr
                       && declaration->vhdl->callable
                       && !declaration->vhdl->callable->function;
               })) {
            return;
        }
        const auto candidates = resolver.resolve_vhdl_callables(
            statement.procedure, statement.scope).candidates;
        if (candidates.empty()) {
            return;
        }
        // Compiler-supplied STD and IEEE subprograms have overloads the
        // design does not retain (STD.TEXTIO); their calls are left alone.
        const auto& scopes = specialization_.design().semantics.scopes();
        if (std::ranges::any_of(candidates, [&](const auto& candidate) {
                const auto declaration = specialization_.find_declaration(candidate.body);
                if (!declaration || declaration->vhdl == nullptr
                    || !declaration->vhdl->scope.valid()
                    || declaration->vhdl->scope.value() >= scopes.size()) {
                    return true;
                }
                const auto unit = specialization_.design().find_unit(
                    scopes[declaration->vhdl->scope.value()].unit);
                return unit && unit->vhdl != nullptr
                    && (equal_ignoring_ascii_case(unit->vhdl->library, "ieee")
                        || equal_ignoring_ascii_case(unit->vhdl->library, "std"));
            })) {
            return;
        }
        std::vector<std::pair<std::string, semantic::ExpressionId>> actuals;
        for (const auto& association : statement.procedure_arguments) {
            actuals.emplace_back(association.formal
                    ? (association.formal->canonical.empty()
                            ? association.formal->spelling
                            : association.formal->canonical)
                    : std::string { },
                association.actual);
        }
        std::set<semantic::DeclarationId> accepted;
        for (const auto& candidate : candidates) {
            if (callable_accepts(candidate.body, actuals)) {
                accepted.insert(candidate.body);
            }
        }
        if (accepted.empty()) {
            report("FSIM-ELAB-VHCALL-002",
                "no visible procedure '" + statement.procedure.spelling
                    + "' has formals matching this call's actuals",
                statement.source);
        } else if (accepted.size() == 1U) {
            validate_aggregate_actuals(
                *accepted.begin(), actuals);
        }
    }

    // IEEE 1076-2008 12.3: a declaration is visible only after it; a call
    // that precedes every declaration of its subprogram name is undeclared.
    bool called_before_declaration(const semantic::vhdl::Expression& name,
        const semantic::SourceSpanId source)
    {
        if (!name.referenced_name || name.text.empty() || name.text.starts_with('@')
            || name.text.find_first_of(".'") != std::string::npos) {
            return false;
        }
        const semantic::CompiledDesignResolver resolver { specialization_ };
        const auto visible = resolver.resolve_vhdl(*name.referenced_name, name.scope);
        const auto& spans = specialization_.design().semantics.source_spans();
        const auto after_call = [&](const semantic::DeclarationId id) {
            const auto declaration = specialization_.find_declaration(id);
            if (!declaration || declaration->vhdl == nullptr || !declaration->vhdl->callable
                || !declaration->vhdl->source.valid() || !source.valid()
                || declaration->vhdl->source.value() >= spans.size()
                || source.value() >= spans.size()) {
                return false;
            }
            const auto& declared = spans[declaration->vhdl->source.value()];
            const auto& used = spans[source.value()];
            return declared.file == used.file
                && (declared.begin.line > used.begin.line
                    || (declared.begin.line == used.begin.line
                        && declared.begin.column > used.begin.column));
        };
        if (visible.candidates.empty() || !std::ranges::all_of(visible.candidates, after_call)) {
            return false;
        }
        report("FSIM-ELAB-VHNAME-002",
            "VHDL subprogram '" + name.text + "' is called before its declaration", source);
        return true;
    }

    // IEEE 1076-1993 2.1.1.1: before VHDL-2008 a formal of mode out is not
    // read, so an out access formal cannot be dereferenced (`x.f`).
    void validate_out_access_dereference(const semantic::vhdl::Expression& expression)
    {
        if (architecture_.standard >= std::string_view { "2008" }
            || expression.kind != semantic::vhdl::ExpressionKind::name) {
            return;
        }
        const auto dot = expression.text.find('.');
        if (dot == std::string::npos || dot == 0U) {
            return;
        }
        const auto prefix = std::string_view { expression.text }.substr(0U, dot);
        const auto& scopes = specialization_.design().semantics.scopes();
        for (const auto& declaration : specialization_.design().vhdl_hir.declarations()) {
            if (declaration.direction != semantic::vhdl::Direction::output
                || !declaration.subtype
                || (declaration.form != semantic::vhdl::DeclarationForm::variable
                    && declaration.form != semantic::vhdl::DeclarationForm::port)
                || !equal_ignoring_ascii_case(declaration.name, prefix)) {
                continue;
            }
            const auto* definition = root_definition(*declaration.subtype);
            if (definition == nullptr || definition->form != TypeForm::access) {
                continue;
            }
            for (auto scope = expression.scope; scope.valid() && scope.value() < scopes.size();) {
                if (scope == declaration.scope) {
                    report("FSIM-ELAB-VHACCESS-034",
                        "a VHDL formal of mode out cannot be read to designate an object "
                        "before VHDL-2008",
                        expression.source);
                    return;
                }
                if (!scopes[scope.value()].parent) {
                    break;
                }
                scope = *scopes[scope.value()].parent;
            }
        }
    }

    void validate_call_arity(const semantic::vhdl::Expression& expression)
    {
        if (expression.kind != semantic::vhdl::ExpressionKind::call
            || !expression.referenced_name || expression.text.empty()
            || expression.text.starts_with('@')
            || expression.text.find('\'') != std::string::npos
            || expression.operands.empty()) {
            return;
        }
        const semantic::CompiledDesignResolver resolver { specialization_ };
        const auto visible = resolver.resolve_vhdl(
            *expression.referenced_name, expression.scope);
        if (called_before_declaration(expression, expression.source)) {
            return;
        }
        // IEEE 1076-2008 4.2: a procedure call is a statement, never an
        // expression.
        if (!visible.candidates.empty()
            && std::ranges::all_of(visible.candidates, [&](const auto id) {
                   const auto declaration = specialization_.find_declaration(id);
                   return declaration && declaration->vhdl != nullptr
                       && declaration->vhdl->callable
                       && !declaration->vhdl->callable->function;
               })) {
            report("FSIM-ELAB-VHCALL-004",
                "a VHDL procedure cannot be called in an expression",
                expression.source);
            return;
        }
        // An indexed object, a type conversion or anything not a function
        // leaves the call to other rules.
        if (visible.candidates.empty()
            || !std::ranges::all_of(visible.candidates, [&](const auto id) {
                   const auto declaration = specialization_.find_declaration(id);
                   return declaration && declaration->vhdl != nullptr
                       && declaration->vhdl->callable
                       && declaration->vhdl->callable->function;
               })) {
            return;
        }
        const auto candidates = resolver.resolve_vhdl_callables(
            *expression.referenced_name, expression.scope).candidates;
        if (candidates.empty()) {
            return;
        }
        std::vector<std::pair<std::string, semantic::ExpressionId>> actuals;
        for (std::size_t index { }; index < expression.operands.size(); ++index) {
            actuals.emplace_back(index < expression.argument_names.size()
                    ? expression.argument_names[index]
                    : std::string { },
                expression.operands[index]);
        }
        const auto accepts = [&](const semantic::DeclarationId body) {
            return callable_accepts(body, actuals);
        };
        std::set<semantic::DeclarationId> accepted;
        for (const auto& candidate : candidates) {
            if (accepts(candidate.body)) {
                accepted.insert(candidate.body);
            }
        }
        const auto signal_formal_mismatch = [&] {
            // A raw overload set is ambiguous even when no candidate accepts
            // the actuals. Only classify the signal association when there is
            // one declaration whose formal mapping is unambiguous.
            if (candidates.size() != 1U) {
                return false;
            }
            const auto body
                = specialization_.find_declaration(candidates.front().body);
            if (!body || body->vhdl == nullptr || !body->vhdl->callable) {
                return false;
            }
            const auto& formals = body->vhdl->callable->formals;
            const auto signal_name = [&](semantic::ExpressionId id) {
                for (std::size_t depth { }; depth < 16U; ++depth) {
                    const auto actual = specialization_.find_expression(id);
                    if (!actual || actual->vhdl == nullptr) {
                        return true;
                    }
                    if ((actual->vhdl->kind
                                == semantic::vhdl::ExpressionKind::index
                            || actual->vhdl->kind
                                == semantic::vhdl::ExpressionKind::slice)
                        && !actual->vhdl->operands.empty()) {
                        id = actual->vhdl->operands.front();
                        continue;
                    }
                    if (actual->vhdl->kind
                            != semantic::vhdl::ExpressionKind::name
                        || !actual->vhdl->referenced_name
                        || !actual->vhdl->referenced_name->selected) {
                        return false;
                    }
                    const auto declaration = specialization_.find_declaration(
                        *actual->vhdl->referenced_name->selected);
                    return !declaration || declaration->vhdl == nullptr
                        || declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::signal
                        || declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::port
                        || declaration->vhdl->object_class
                            == semantic::vhdl::ObjectClass::signal;
                }
                return true;
            };
            for (std::size_t index { };
                index < actuals.size() && index < formals.size(); ++index) {
                if (!actuals[index].first.empty()) {
                    break;
                }
                const auto formal = specialization_.find_declaration(
                    formals[index]);
                if (formal && formal->vhdl != nullptr
                    && formal->vhdl->object_class
                        == semantic::vhdl::ObjectClass::signal
                    && !signal_name(actuals[index].second)) {
                    report("FSIM-ELAB-VHCALL-003",
                        "the actual for signal formal '" + formal->vhdl->name
                            + "' of function '" + expression.text
                            + "' is not a signal name",
                        expression.source);
                    return true;
                }
            }
            return false;
        };
        if (accepted.empty()) {
            if (signal_formal_mismatch()) {
                return;
            }
            report("FSIM-ELAB-VHCALL-001",
                "no visible function '" + expression.text
                    + "' has formals matching this call's actuals",
                expression.source);
            return;
        }
        if (accepted.size() == 1U) {
            validate_aggregate_actuals(
                *accepted.begin(), actuals);
        }
        // A formal of class signal is associated with a signal name (IEEE
        // 1076-1993 2.1.1.2).
        (void)signal_formal_mismatch();
    }

    // IEEE 1076-2008 4.2.1: a subprogram declared in a declarative part has
    // a body in the same declarative part.
    void validate_subprogram_bodies(
        const std::function<bool(semantic::ScopeId)>& selected_scope)
    {
        const auto& declarations = specialization_.design().vhdl_hir.declarations();
        // A protected type's methods have their bodies in the protected type
        // body, which may be in a package body.
        std::unordered_set<std::uint32_t> protected_methods;
        for (const auto& type : specialization_.design().vhdl_hir.types()) {
            for (const auto member : type.protected_members) {
                protected_methods.insert(member.value());
            }
        }
        for (const auto& declaration : declarations) {
            if (protected_methods.contains(declaration.id.value())) {
                continue;
            }
            if (!declaration.callable || declaration.callable->defined
                || !selected_scope(declaration.scope)
                || (declaration.form != semantic::vhdl::DeclarationForm::function
                    && declaration.form != semantic::vhdl::DeclarationForm::procedure)) {
                continue;
            }
            // A protected type's methods have their bodies in the protected
            // type body, another region of the same design unit.
            const auto& scopes = specialization_.design().semantics.scopes();
            const auto unit_of = [&](const semantic::ScopeId scope) {
                return scope.valid() && scope.value() < scopes.size()
                    ? std::optional { scopes[scope.value()].unit }
                    : std::nullopt;
            };
            // A body in a nested region (a block or process inside the
            // declaring architecture) declares a different subprogram.
            const auto nested_in_declaring_region = [&](semantic::ScopeId scope) {
                for (std::size_t depth { }; depth < 64U && scope.valid()
                     && scope.value() < scopes.size(); ++depth) {
                    const auto parent = scopes[scope.value()].parent;
                    if (!parent) {
                        return false;
                    }
                    if (*parent == declaration.scope) {
                        return true;
                    }
                    scope = *parent;
                }
                return false;
            };
            const auto has_body = std::ranges::any_of(declarations, [&](const auto& other) {
                return other.callable && other.callable->defined
                    && equal_ignoring_ascii_case(other.name, declaration.name)
                    && (other.scope == declaration.scope
                        || (unit_of(other.scope) == unit_of(declaration.scope)
                            && !nested_in_declaring_region(other.scope)));
            });
            if (!has_body) {
                report("FSIM-ELAB-VHBODY-001",
                    "VHDL subprogram '" + declaration.name
                        + "' is declared without a body in the same declarative part",
                    declaration.source);
            }
        }
    }

    // The VHDL revision of the design unit that contains a scope.
    std::string_view unit_standard(const semantic::ScopeId scope) const
    {
        const auto& scopes = specialization_.design().semantics.scopes();
        if (!scope.valid() || scope.value() >= scopes.size()) {
            return architecture_.standard;
        }
        const auto unit = scopes[scope.value()].unit;
        for (const auto& candidate : specialization_.design().vhdl_hir.units()) {
            if (candidate.id == unit) {
                return candidate.standard;
            }
        }
        return architecture_.standard;
    }

    // Whether the entity or component an instance names declares a type,
    // subprogram or package generic, whose actual is not a value.
    bool instance_has_type_generic(const semantic::vhdl::Instance& instance) const
    {
        const auto& hir = specialization_.design().vhdl_hir;
        auto target = std::string_view { instance.target.spelling };
        if (const auto open = target.find('('); open != std::string_view::npos) {
            target = target.substr(0U, open);
        }
        const auto name = simple_name_of(target);
        std::vector<semantic::ScopeId> scopes;
        for (const auto& unit : hir.units()) {
            if (unit.kind == semantic::vhdl::UnitKind::entity
                && equal_ignoring_ascii_case(unit.name, name)) {
                scopes.push_back(unit.scope);
            }
        }
        for (const auto& declaration : hir.declarations()) {
            if (declaration.form == semantic::vhdl::DeclarationForm::component
                && equal_ignoring_ascii_case(declaration.name, name)
                && declaration.nested_scope) {
                scopes.push_back(*declaration.nested_scope);
            }
        }
        return std::ranges::any_of(hir.declarations(),
            [&](const semantic::vhdl::Declaration& declaration) {
                using Form = semantic::vhdl::DeclarationForm;
                return (declaration.form == Form::generic_type
                           || declaration.form == Form::generic_package
                           || declaration.form == Form::generic_function
                           || declaration.form == Form::generic_procedure)
                    && std::ranges::find(scopes, declaration.scope) != scopes.end();
            });
    }

    // IEEE 1076-2008 12.3: a simple name in an expression denotes a visible
    // declaration (not a label or a design unit).
    void validate_value_name(const semantic::ExpressionId id,
        const bool allow_type_mark = false)
    {
        const auto expression = specialization_.find_expression(id);
        if (expression && expression->vhdl != nullptr) {
            validate_simple_name(*expression->vhdl, allow_type_mark);
            validate_selected_element(*expression->vhdl);
        }
    }

    // IEEE 1076-2008 8.3: a selected name whose prefix is an object denotes
    // an element of a record (or the object an access value designates).
    void validate_selected_element(const semantic::vhdl::Expression& expression)
    {
        if (expression.kind != semantic::vhdl::ExpressionKind::name
            || expression.text.find('\'') != std::string::npos
            || expression.text.starts_with('@')) {
            return;
        }
        const std::string_view text { expression.text };
        const auto separator = text.find('.');
        if (separator == std::string_view::npos || separator == 0U) {
            return;
        }
        semantic::vhdl::Name prefix;
        prefix.spelling = std::string { text.substr(0U, separator) };
        prefix.canonical = prefix.spelling;
        const auto selected = semantic::CompiledDesignResolver { specialization_ }
                                  .resolve_vhdl(prefix, expression.scope)
                                  .unique();
        const auto declaration = selected
            ? specialization_.find_declaration(*selected)
            : std::nullopt;
        using Form = semantic::vhdl::DeclarationForm;
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype
            || (declaration->vhdl->form != Form::variable
                && declaration->vhdl->form != Form::signal
                && declaration->vhdl->form != Form::constant
                && declaration->vhdl->form != Form::port)) {
            return;
        }
        auto subtype = specialized_subtype(*declaration->vhdl->subtype);
        auto remaining = text.substr(separator + 1U);
        while (!remaining.empty()) {
            const auto next = remaining.find('.');
            const auto member = remaining.substr(0U, next);
            const auto* definition = root_definition(subtype);
            if (definition == nullptr
                || (definition->form != TypeForm::record
                    && definition->form != TypeForm::array
                    && definition->form != TypeForm::scalar
                    && definition->form != TypeForm::enumeration
                    && definition->form != TypeForm::physical)) {
                return;
            }
            const auto element = definition->form == TypeForm::record
                ? std::ranges::find_if(definition->record_elements,
                      [&](const semantic::vhdl::RecordElement& candidate) {
                          return equal_ignoring_ascii_case(candidate.name, member);
                      })
                : definition->record_elements.end();
            if (element == definition->record_elements.end()) {
                report("FSIM-ELAB-VHNAME-003",
                    "'" + std::string { member }
                        + "' is not an element of the selected VHDL object",
                    expression.source);
                return;
            }
            subtype = record_member_subtype(subtype, *element);
            if (next == std::string_view::npos) {
                return;
            }
            remaining.remove_prefix(next + 1U);
        }
    }

    // IEEE 1076-2008 9.1: a label, an architecture name or an entity name
    // is not a primary, so it cannot be an actual of a generic or port map.
    void validate_label_actual(const semantic::ExpressionId id)
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr
            || expression->vhdl->kind != semantic::vhdl::ExpressionKind::name
            || expression->vhdl->text.empty()
            || expression->vhdl->text.find_first_of(".'@") != std::string::npos
            || (expression->vhdl->referenced_name
                && expression->vhdl->referenced_name->selected)) {
            return;
        }
        const auto& text = expression->vhdl->text;
        const auto& design = specialization_.design();
        const auto& scopes = design.semantics.scopes();
        const auto in_design_unit = [&](const semantic::ScopeId scope) {
            return scope.valid() && scope.value() < scopes.size()
                && (scopes[scope.value()].unit == entity_.id
                    || scopes[scope.value()].unit == architecture_.id);
        };
        bool label = equal_ignoring_ascii_case(text, architecture_.name)
            || equal_ignoring_ascii_case(text, entity_.name);
        for (const auto& statement : design.vhdl_hir.statements()) {
            if (label) {
                break;
            }
            label = !statement.label.empty() && in_design_unit(statement.scope)
                && equal_ignoring_ascii_case(statement.label, text);
        }
        for (const auto& process : design.vhdl_hir.processes()) {
            if (label) {
                break;
            }
            label = in_design_unit(process.scope)
                && equal_ignoring_ascii_case(process.name, text);
        }
        for (const auto& candidate : design.vhdl_hir.instances()) {
            if (label) {
                break;
            }
            label = in_design_unit(candidate.scope)
                && equal_ignoring_ascii_case(candidate.name, text);
        }
        const auto generate_label = [&](const auto& self,
                                        const std::vector<semantic::vhdl::GenerateRegion>& regions)
            -> bool {
            return std::ranges::any_of(regions, [&](const auto& region) {
                return equal_ignoring_ascii_case(region.label, text)
                    || self(self, region.nested);
            });
        };
        label = label || generate_label(generate_label, architecture_.generates);
        if (!label) {
            return;
        }
        // An object of that name declared where the actual appears hides
        // nothing here: labels and objects share one declarative region.
        if (expression->vhdl->referenced_name
            && !semantic::CompiledDesignResolver { specialization_ }
                    .resolve_vhdl(*expression->vhdl->referenced_name,
                        expression->vhdl->scope)
                    .candidates.empty()) {
            return;
        }
        report("FSIM-ELAB-VHNAME-015",
            "VHDL label or design unit name '" + text
                + "' is not a value and cannot be an actual",
            expression->vhdl->source);
    }

    // IEEE 1076-2008 12.2: a loop parameter is visible only within its loop
    // statement; it is retained as a constant without a value.
    bool loop_parameter_outside_loop(const semantic::DeclarationId id,
        const semantic::vhdl::Expression& expression) const
    {
        const auto declaration = specialization_.find_declaration(id);
        if (!declaration || declaration->vhdl == nullptr
            || declaration->vhdl->form != semantic::vhdl::DeclarationForm::constant
            || declaration->vhdl->initializer || !declaration->vhdl->source.valid()
            || !expression.source.valid()) {
            return false;
        }
        for (const auto& statement : specialization_.design().vhdl_hir.statements()) {
            if (statement.kind == semantic::vhdl::StatementKind::loop
                && equal_ignoring_ascii_case(statement.loop_variable, declaration->vhdl->name)
                && statement.source.valid()
                && source_is_within(declaration->vhdl->source, statement.source)) {
                return !source_is_within(expression.source, statement.source);
            }
        }
        return false;
    }

    void validate_simple_name(const semantic::vhdl::Expression& expression,
        const bool allow_type_mark = false)
    {
        if (expression.kind != semantic::vhdl::ExpressionKind::name
            || !expression.referenced_name || expression.text.empty()
            || expression.text.starts_with('@')
            || expression.text.find_first_of(".'") != std::string::npos) {
            return;
        }
        const auto& reference = *expression.referenced_name;
        if (reference.selected) {
            // A type, subtype or component name is not a value (IEEE
            // 1076-2008 9.1: a primary is not a type mark alone).
            const auto declaration = specialization_.find_declaration(*reference.selected);
            using Form = semantic::vhdl::DeclarationForm;
            if (declaration && declaration->vhdl != nullptr
                && (((declaration->vhdl->form == Form::type
                         || declaration->vhdl->form == Form::subtype)
                        && !allow_type_mark)
                    || declaration->vhdl->form == Form::component
                    || (declaration->vhdl->form == Form::package_instance
                        && !allow_type_mark)
                    || declaration->vhdl->form == Form::generated)) {
                report("FSIM-ELAB-VHNAME-004",
                    "VHDL name '" + expression.text
                        + "' denotes a type, subtype or design unit, not a value",
                    expression.source);
            }
            if (loop_parameter_outside_loop(*reference.selected, expression)) {
                report("FSIM-ELAB-VHNAME-002",
                    "VHDL loop parameter '" + expression.text
                        + "' is not visible outside its loop",
                    expression.source);
            }
            return;
        }
        if (!reference.overloads.empty()) {
            return;
        }
        // Literals of STD.STANDARD types the compiler provides natively.
        constexpr std::array predefined_literals {
            std::string_view { "note" }, std::string_view { "warning" },
            std::string_view { "error" }, std::string_view { "failure" },
            std::string_view { "read_mode" }, std::string_view { "write_mode" },
            std::string_view { "append_mode" }, std::string_view { "open_ok" },
            std::string_view { "status_error" }, std::string_view { "name_error" },
            std::string_view { "mode_error" }, std::string_view { "now" },
            std::string_view { "true" }, std::string_view { "false" },
        };
        const semantic::CompiledDesignResolver resolver { specialization_ };
        // SIDE and its RIGHT/LEFT literals predate VHDL-2008 in STD.TEXTIO.
        // Accept declaration-free literals only when the exact compiler-owned
        // member is imported and lexical declaration lookup finds no shadow.
        const bool has_textio_side_literal
            = (equal_ignoring_ascii_case(expression.text, "right")
                || equal_ignoring_ascii_case(expression.text, "left"))
            && resolver.resolve_vhdl(reference, expression.scope)
                    .candidates.empty()
            && resolver.vhdl_builtin_package_member_imported(
                "std", "textio", expression.text, expression.scope);
        if (std::ranges::any_of(predefined_literals, [&](const auto literal) {
                return equal_ignoring_ascii_case(expression.text, literal);
            })
            || has_textio_side_literal
            || builtin_operand_class(expression.text) != OperandClass::unknown
            || vhdl_character_control_literal(expression.text)) {
            return;
        }
        // Every declaration of the name is a loop parameter whose loop does
        // not enclose this use (a generate parameter is not retained as a
        // declaration).
        if (!generate_iterator(expression.text)) {
            bool loop_parameters_only { };
            for (const auto& declaration : specialization_.design().vhdl_hir.declarations()) {
                if (!equal_ignoring_ascii_case(declaration.name, expression.text)) {
                    continue;
                }
                if (!loop_parameter_outside_loop(declaration.id, expression)) {
                    loop_parameters_only = false;
                    break;
                }
                loop_parameters_only = true;
            }
            if (loop_parameters_only
                && resolver.resolve_vhdl(reference, expression.scope).candidates.empty()) {
                report("FSIM-ELAB-VHNAME-002",
                    "VHDL loop parameter '" + expression.text
                        + "' is not visible outside its loop",
                    expression.source);
                return;
            }
        }
        if (name_exempt(expression.text)) {
            return;
        }
        if (!resolver.resolve_vhdl(reference, expression.scope).candidates.empty()
            || !resolver.resolve_vhdl_callables(reference, expression.scope)
                    .candidates.empty()) {
            return;
        }
        report("FSIM-ELAB-VHNAME-002",
            "VHDL name '" + expression.text + "' does not denote a visible declaration",
            expression.source);
    }

    // IEEE 1076-2008 10.5.2.1, 10.6.2.1: the value assigned to a scalar
    // target has the target's type.
    void validate_assignment_type(const semantic::ExpressionId target_id,
        const semantic::ExpressionId value_id, const semantic::SourceSpanId source)
    {
        // An indexed or sliced target may be a slice by a subtype name
        // (`v(2)(idx)`); only whole objects and record elements are checked.
        const auto target_expression = specialization_.find_expression(target_id);
        if (!target_expression || target_expression->vhdl == nullptr) {
            return;
        }
        if (target_expression->vhdl->kind
            == semantic::vhdl::ExpressionKind::aggregate) {
            // The target aggregate has the composite type of the source
            // value. Its associations are checked against that type just as
            // for an ordinary aggregate expression; each association value
            // is a writable target rather than an element value.
            if (const auto source_type = expression_subtype(value_id)) {
                validate_aggregate(target_id, *source_type);
            } else if (const auto value = specialization_.find_expression(value_id);
                value && value->vhdl != nullptr
                && value->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate) {
                // An aggregate target cannot supply the context needed to
                // determine the type of another unqualified aggregate.
                report("FSIM-ELAB-VHAGG-001",
                    "a VHDL aggregate assigned to an aggregate target requires "
                    "an independently determined array or record type",
                    value->vhdl->source);
            }
            return;
        }
        if (target_expression->vhdl->kind
            != semantic::vhdl::ExpressionKind::name) {
            return;
        }
        const auto target = operand_type(target_id);
        const auto value = operand_type(value_id);
        if (!scalar_class(target.kind) && target.kind != OperandClass::array
            && target.kind != OperandClass::record) {
            return;
        }
        // A scalar value (a parenthesized one, as `(25)`, is not an
        // aggregate) is not of a composite type.
        if (!scalar_class(target.kind) && scalar_class(value.kind)) {
            const auto expression = specialization_.find_expression(value_id);
            report("FSIM-ELAB-VHASSIGN-001",
                "a scalar value is assigned to a VHDL target of a composite type",
                expression && expression->vhdl != nullptr
                    ? expression->vhdl->source
                    : source);
            return;
        }
        // An allocator or `null` is an access value (9.3.7).
        if (!scalar_class(target.kind)) {
            if (value.kind == OperandClass::access || value.kind == OperandClass::null) {
                const auto expression = specialization_.find_expression(value_id);
                report("FSIM-ELAB-VHASSIGN-001",
                    "an access value is assigned to a VHDL target that is not of an "
                    "access type",
                    expression && expression->vhdl != nullptr
                        ? expression->vhdl->source
                        : source);
            }
            return;
        }
        if (value.kind == OperandClass::unknown) {
            return;
        }
        const auto mismatch = value.kind != target.kind
            || roots_conflict(target.root, value.root);
        if (mismatch) {
            const auto expression = specialization_.find_expression(value_id);
            report("FSIM-ELAB-VHASSIGN-001",
                "the value assigned to a VHDL scalar target does not have the "
                "target's type",
                expression && expression->vhdl != nullptr
                    ? expression->vhdl->source
                    : source);
        }
    }

    // IEEE 1076-2008 9.3.3: an aggregate in a context that determines its
    // type. Associations are all positional or all named (except a final
    // OTHERS); a choice is given once; each element value has the element
    // type; the elements of a multidimensional array aggregate are
    // aggregates or string literals of one dimension less.
    void validate_aggregate(const semantic::ExpressionId aggregate_id,
        const SubtypeIndication& context, const std::size_t depth = 0U)
    {
        const auto aggregate = specialization_.find_expression(aggregate_id);
        if (depth > 8U || !aggregate || aggregate->vhdl == nullptr) {
            return;
        }
        auto expression = *aggregate->vhdl;
        if (depth == 0U
            && expression.kind
                == semantic::vhdl::ExpressionKind::aggregate) {
            if (profile(context).kind
                == ProfileKind::array) {
                // Function-return validation enters this legacy ExpressionId
                // overload. Run the rank-aware array walk here too.
                validate_aggregate(expression, context);
            }
        }
        if (expression.kind == semantic::vhdl::ExpressionKind::call
            && expression.text.starts_with(
                semantic::vhdl::qualified_expression_prefix)
            && expression.operands.size() == 1U) {
            const auto operand = specialization_.find_expression(
                expression.operands.front());
            const auto context_kind = profile(
                context).kind;
            const bool aggregate_context
                = context_kind == ProfileKind::array
                || context_kind == ProfileKind::record;
            if (aggregate_context && operand && operand->vhdl != nullptr
                && operand->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate) {
                const auto* context_definition = root_definition(context);
                const auto expected_declaration_scope
                    = context_definition != nullptr
                        && context.type_mark.target.valid()
                    ? type_declaration_scope(*context_definition)
                    : semantic::ScopeId { };
                if (aggregate_element_compatible(
                        context, aggregate_id,
                        expected_declaration_scope)) {
                    const SubtypeIndication* validation_context = &context;
                    std::optional<SubtypeIndication> qualified_context;
                    const bool named_array_aggregate
                        = context_kind == ProfileKind::array
                        && !operand->vhdl->associations.empty()
                        && std::ranges::any_of(
                            operand->vhdl->associations,
                            [](const auto& association) {
                                return !association.positional();
                            });
                    if (named_array_aggregate) {
                        // Named choices establish an unconstrained source
                        // array's bounds. Validate them in that exact source
                        // type context; lowering still checks the packed
                        // extent against the target row width.
                        const auto actual_subtype
                            = expression_subtype(aggregate_id);
                        if (actual_subtype) {
                            const auto expected_profile = profile(context);
                            const auto actual_profile
                                = profile(*actual_subtype);
                            const auto named_choices_fit_index_subtype
                                = !actual_profile.dimensions.empty()
                                && actual_profile.dimensions.front().minimum
                                && actual_profile.dimensions.front().maximum
                                && std::ranges::all_of(
                                    operand->vhdl->associations,
                                    [&](const auto& association) {
                                        const auto& index_range
                                            = actual_profile.dimensions.front();
                                        return !association.choices.empty()
                                            && std::ranges::all_of(
                                                association.choices,
                                                [&](const auto choice) {
                                                    const auto index = bound(
                                                        std::nullopt, choice);
                                                    return index
                                                        && *index >= *index_range.minimum
                                                        && *index <= *index_range.maximum;
                                                });
                                    });
                            const auto expected_type = subtype_type(context);
                            const auto actual_type
                                = subtype_type(*actual_subtype);
                            const auto expected_root = expected_type
                                ? array_root(*expected_type)
                                : std::nullopt;
                            const auto actual_root = actual_type
                                ? array_root(*actual_type)
                                : std::nullopt;
                            const auto* actual_definition = actual_type
                                ? type_definition(*actual_type) : nullptr;
                            const bool unconstrained_actual_type
                                = actual_subtype->constraints.empty()
                                && !actual_subtype->executable_width
                                && actual_type && actual_root
                                && *actual_type == *actual_root
                                && actual_definition != nullptr
                                && actual_definition->form == TypeForm::array
                                && actual_definition->array_dimensions.size()
                                    == 1U
                                && actual_definition->array_dimensions.front()
                                       .unconstrained
                                && !actual_definition->array_dimensions.front()
                                        .constraint;
                            if (expected_profile.kind == ProfileKind::array
                                && actual_profile.kind == ProfileKind::array
                                && expected_profile.dimensions.size() == 1U
                                && actual_profile.dimensions.size() == 1U
                                && unconstrained_actual_type
                                && named_choices_fit_index_subtype
                                && expected_root && actual_root
                                && *expected_root == *actual_root) {
                                qualified_context = *actual_subtype;
                                validation_context = &*qualified_context;
                            }
                        }
                    }
                    validate_aggregate(
                        *operand->vhdl, *validation_context);
                } else {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-009",
                        "VHDL array aggregate element requires its exact "
                            "contextual subtype",
                        expression.source);
                }
            }
            return;
        }
        // 9.3.2: each character of a string literal is a literal of the
        // element type (BIT, STD_ULOGIC, CHARACTER).
        if (expression.kind == semantic::vhdl::ExpressionKind::string_literal
            && expression.decoded_string && expression.text.starts_with('"')) {
            const auto* array = root_definition(context);
            if (array != nullptr && array->form == TypeForm::array && array->element_subtype) {
                auto element = std::string_view { array->element_subtype->type_mark.spelling };
                element = simple_name_of(element);
                const std::string_view allowed = equal_ignoring_ascii_case(element, "bit") ? "01"
                    : (equal_ignoring_ascii_case(element, "std_ulogic") || equal_ignoring_ascii_case(element, "std_logic"))
                        ? "UX01ZWLH-"
                        : std::string_view { };
                if (!allowed.empty()
                    && !std::ranges::all_of(*expression.decoded_string, [&](const char c) {
                           return allowed.find(c) != std::string_view::npos;
                       })) {
                    report("FSIM-ELAB-VHAGG-019",
                        "a character of a VHDL string literal is not a literal of the "
                        "array element type",
                        expression.source);
                }
            }
            return;
        }
        if (expression.kind != semantic::vhdl::ExpressionKind::aggregate) {
            return;
        }
        // IEEE 1076-2008 9.3.3.3: a named choice of a one-dimensional array
        // aggregate is given once.  Only with `others` do the bounds come
        // from the context, so only then must each choice lie in its range;
        // otherwise the choices give the bounds and the value converts by
        // length.
        {
            const bool has_others = std::ranges::any_of(
                expression.associations, [&](const auto& association) {
                    return equal_ignoring_ascii_case(
                        association.choice_spelling, "others");
                });
            const auto shape = profile(context);
            if (shape.kind == ProfileKind::array && shape.dimensions.size() == 1U) {
                const auto& range = shape.dimensions.front();
                std::set<std::int64_t> chosen;
                for (const auto& association : expression.associations) {
                    for (const auto choice : association.choices) {
                        const auto found = specialization_.find_expression(choice);
                        if (!found || found->vhdl == nullptr
                            || found->vhdl->kind
                                != semantic::vhdl::ExpressionKind::integer_literal) {
                            continue;
                        }
                        const auto value = specialization_.evaluate_integral_expression(choice);
                        if (!value) {
                            continue;
                        }
                        if (!chosen.insert(*value).second) {
                            report("FSIM-ELAB-VHAGG-013",
                                "a VHDL aggregate choice is given more than once",
                                expression.source);
                            return;
                        }
                        if (has_others && range.constrained && range.minimum
                            && range.maximum
                            && *range.minimum <= *range.maximum
                            && (*value < *range.minimum || *value > *range.maximum)) {
                            report("FSIM-ELAB-VHARRAYAGG-003",
                                "VHDL array aggregate index is outside the contextual range",
                                found->vhdl->source);
                            return;
                        }
                    }
                }
            }
        }
        const auto* definition = root_definition(context);
        if (definition == nullptr
            || (definition->form != TypeForm::array
                && definition->form != TypeForm::record)) {
            return;
        }
        // Component subtype names are resolved where their containing array
        // or record type was declared, separately from an aggregate literal's
        // use scope.
        // A global name fallback is insufficient to establish the owner of
        // a targetless predefined mark; retain owner scope only for a resolved
        // contextual type identity.
        const auto expected_declaration_scope
            = context.type_mark.target.valid()
            ? type_declaration_scope(*definition)
            : semantic::ScopeId { };
        const auto is_others = [&](const semantic::vhdl::AggregateAssociation& association) {
            return choice_is_others(association);
        };
        const auto positional = [&](const semantic::vhdl::AggregateAssociation& association) {
            return association.positional();
        };
        bool any_positional { };
        bool any_named { };
        for (const auto& association : expression.associations) {
            if (positional(association)) {
                any_positional = true;
            } else if (!is_others(association)) {
                any_named = true;
            }
        }
        if (definition->form == TypeForm::array && any_positional && any_named) {
            report("FSIM-ELAB-VHAGG-012",
                "the element associations of a VHDL array aggregate are all positional "
                "or all named",
                expression.source);
            return;
        }
        const auto compatible = [&](const semantic::ExpressionId value,
                                    const SubtypeIndication& element) {
            const auto expression
                = specialization_.find_expression(value);
            if (expression && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal) {
                const auto scope = expression->vhdl->scope;
                // A string literal in an array-valued range association has
                // the enclosing array's context. In an array-of-arrays it
                // can instead have the element array's context.
                return contextual_string_array_value_for(
                           value, context, scope,
                           expected_declaration_scope)
                    || contextual_string_array_value_for(
                        value, element, scope,
                        expected_declaration_scope);
            }
            const auto expected = subtype_operand_type(element, 0U);
            auto actual = operand_type(value);
            if (scalar_class(expected.kind)
                && actual.kind == OperandClass::unknown) {
                const auto found = specialization_.find_expression(value);
                if (found && found->vhdl != nullptr
                    && (found->vhdl->kind == semantic::vhdl::ExpressionKind::unary
                        || found->vhdl->kind == semantic::vhdl::ExpressionKind::binary)) {
                    // `operand_type` intentionally recognizes only a subset of
                    // operators. For an overloaded operator, use its resolved
                    // callable's declared result subtype instead of treating
                    // an unclassified result as compatible with every scalar.
                    bool matching_callable { };
                    if (const auto subtype = function_result_subtype(
                            *found->vhdl, &matching_callable)) {
                        actual = subtype_operand_type(*subtype, 0U);
                    } else if (const auto operator_subtype = operator_result_subtype(
                                   *found->vhdl)) {
                        actual = subtype_operand_type(*operator_subtype, 0U);
                    }
                    // Scalar predefined NOT has the same discrete type as its
                    // BOOLEAN or BIT operand. The array-result rule is handled
                    // by operator_result_subtype.
                    if (actual.kind == OperandClass::unknown
                        && found->vhdl->kind == semantic::vhdl::ExpressionKind::unary
                        && found->vhdl->text == "not"
                        && found->vhdl->operands.size() == 1U
                        && !matching_callable) {
                        const auto operand = operand_type(
                            found->vhdl->operands.front());
                        if (operand.kind == OperandClass::enumeration
                            && (operand.root == "boolean" || operand.root == "bit")) {
                            actual = operand;
                        }
                    }
                }
            }
            // A relational operation is BOOLEAN (IEEE 1076-2008 9.2.3).
            if (const auto found = specialization_.find_expression(value);
                actual.kind == OperandClass::unknown && found && found->vhdl != nullptr
                && found->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                && predefined_operator(*found->vhdl)
                && (found->vhdl->text == "=" || found->vhdl->text == "/="
                    || found->vhdl->text == "<" || found->vhdl->text == "<="
                    || found->vhdl->text == ">" || found->vhdl->text == ">=")) {
                actual = { OperandClass::enumeration, "boolean" };
            }
            return !scalar_class(expected.kind)
                || actual.kind == OperandClass::unknown
                || (!types_conflict(actual, expected));
        };
        if (definition->form == TypeForm::array) {
            if (!definition->element_subtype) {
                return;
            }
            std::set<std::int64_t> chosen;
            for (const auto& association : expression.associations) {
                for (const auto choice : association.choices) {
                    const auto found = specialization_.find_expression(choice);
                    if (!found || found->vhdl == nullptr
                        || found->vhdl->kind != semantic::vhdl::ExpressionKind::integer_literal) {
                        continue;
                    }
                    const auto value = specialization_.evaluate_integral_expression(choice);
                    if (value && !chosen.insert(*value).second) {
                        report("FSIM-ELAB-VHAGG-013",
                            "a VHDL aggregate choice is given more than once",
                            expression.source);
                        return;
                    }
                }
            }
            const bool multidimensional = definition->array_dimensions.size() > 1U;
            for (const auto& association : expression.associations) {
                const auto value = specialization_.find_expression(association.value);
                if (!value || value->vhdl == nullptr) {
                    continue;
                }
                if (multidimensional) {
                    // Each subaggregate covers the remaining dimension.
                    const auto& inner = definition->array_dimensions[1];
                    if (value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
                        && inner.constraint && inner.constraint->left && inner.constraint->right
                        && std::ranges::all_of(value->vhdl->associations,
                            [](const semantic::vhdl::AggregateAssociation& association) {
                                return association.positional();
                            })) {
                        const auto expected = (inner.constraint->descending
                                ? *inner.constraint->left - *inner.constraint->right
                                : *inner.constraint->right - *inner.constraint->left) + 1;
                        if (expected > 0
                            && static_cast<std::int64_t>(value->vhdl->associations.size())
                                != expected) {
                            report("FSIM-ELAB-VHAGG-017",
                                "a subaggregate of a multidimensional VHDL aggregate does not "
                                "match the length of its dimension",
                                value->vhdl->source);
                            return;
                        }
                    }
                    if (value->vhdl->kind != semantic::vhdl::ExpressionKind::aggregate
                        && value->vhdl->kind != semantic::vhdl::ExpressionKind::string_literal) {
                        report("FSIM-ELAB-VHAGG-015",
                            "an element association of a multidimensional VHDL array "
                            "aggregate is an aggregate of one dimension less",
                            expression.source);
                        return;
                    }
                    continue;
                }
                if (value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate) {
                    validate_aggregate(association.value, *definition->element_subtype,
                        depth + 1U);
                    continue;
                }
                if (positional(association)
                    && positional_array_segment_count(
                        association.value, context,
                        *definition->element_subtype,
                        value->vhdl->scope)) {
                    continue;
                }
                // IEEE 1076-2008 9.3.3.3 permits an array-valued range
                // association to provide a zero-length segment for a null
                // choice. The legacy expression-ID validator below only
                // handles positional array segments, so recognize this
                // bounded case before comparing the array value with the
                // scalar element subtype. The shared segment helper proves
                // the exact nominal array type, rank, revision and zero
                // cardinality; require a null contextual range and the
                // aggregate's sole, single binary range choice as well.
                if (!positional(association)
                    && expression.associations.size() == 1U
                    && association.choices.size() == 1U) {
                    const auto choice = specialization_.find_expression(
                        association.choices.front());
                    if (choice && choice->vhdl != nullptr
                        && choice->vhdl->kind
                            == semantic::vhdl::ExpressionKind::binary
                        && choice->vhdl->operands.size() == 2U
                        && (choice->vhdl->text == "to"
                            || choice->vhdl->text == "downto")) {
                        const auto left = bound(
                            std::nullopt, choice->vhdl->operands[0]);
                        const auto right = bound(
                            std::nullopt, choice->vhdl->operands[1]);
                        const bool null_choice = left && right
                            && (choice->vhdl->text == "to"
                                ? *left > *right
                                : *left < *right);
                        const auto context_profile
                            = profile(context);
                        const bool null_context
                            = context_profile.kind
                                    == ProfileKind::array
                            && context_profile.dimensions.size() == 1U
                            && context_profile.dimensions.front().null_known
                            && context_profile.dimensions.front().null;
                        const auto segment_count = null_choice
                                && null_context
                            ? positional_array_segment_count(
                                  association.value, context,
                                  *definition->element_subtype,
                                  value->vhdl->scope)
                            : std::nullopt;
                        if (segment_count && *segment_count == 0U) {
                            continue;
                        }
                    }
                }
                if (!compatible(association.value, *definition->element_subtype)) {
                    report("FSIM-ELAB-VHAGG-014",
                        "a VHDL aggregate element does not have the element type",
                        value->vhdl->source);
                    return;
                }
            }
            return;
        }
        // 9.3.3.2: OTHERS (or several choices) in a record aggregate stands
        // for at least one element, all of the same type.
        {
            std::vector<bool> covered(definition->record_elements.size(), false);
            std::size_t next { };
            const semantic::vhdl::AggregateAssociation* others = nullptr;
            for (const auto& association : expression.associations) {
                if (positional(association)) {
                    if (next < covered.size()) {
                        covered[next] = true;
                    }
                    ++next;
                    continue;
                }
                if (is_others(association)) {
                    others = &association;
                    continue;
                }
                std::vector<const semantic::vhdl::RecordElement*> named;
                for (const auto choice : association.choices) {
                    const auto found = specialization_.find_expression(choice);
                    const auto spelling = found && found->vhdl != nullptr
                        ? found->vhdl->text
                        : association.choice_spelling;
                    for (std::size_t index { }; index < covered.size(); ++index) {
                        if (equal_ignoring_ascii_case(definition->record_elements[index].name, spelling)) {
                            covered[index] = true;
                            named.push_back(&definition->record_elements[index]);
                        }
                    }
                }
                if (named.size() == 1U && !compatible(association.value, named.front()->subtype)) {
                    report("FSIM-ELAB-VHAGG-014",
                        "a VHDL aggregate element does not have the element type",
                        association.source);
                    return;
                }
                if (named.size() > 1U) {
                    const auto first = subtype_operand_type(named.front()->subtype, 0U);
                    if (std::ranges::any_of(named, [&](const auto* element) {
                            const auto type = subtype_operand_type(element->subtype, 0U);
                            return type.kind != first.kind
                                || roots_conflict(type.root, first.root);
                        })) {
                        report("FSIM-ELAB-VHAGG-018",
                            "the elements named by one choice of a VHDL record aggregate "
                            "must have the same type",
                            expression.source);
                        return;
                    }
                }
            }
            if (others != nullptr) {
                std::vector<const semantic::vhdl::RecordElement*> rest;
                for (std::size_t index { }; index < covered.size(); ++index) {
                    if (!covered[index]) {
                        rest.push_back(&definition->record_elements[index]);
                    }
                }
                const bool mixed = !rest.empty() && std::ranges::any_of(rest,
                    [&](const auto* element) {
                        const auto first = subtype_operand_type(rest.front()->subtype, 0U);
                        const auto type = subtype_operand_type(element->subtype, 0U);
                        return type.kind != first.kind || roots_conflict(type.root, first.root);
                    });
                if (rest.empty() || mixed) {
                    report("FSIM-ELAB-VHAGG-018",
                        "OTHERS in a VHDL record aggregate must stand for at least one "
                        "element, all of the same type",
                        expression.source);
                    return;
                }
            }
        }
        std::size_t position { };
        for (const auto& association : expression.associations) {
            const semantic::vhdl::RecordElement* element = nullptr;
            if (positional(association)) {
                if (position < definition->record_elements.size()) {
                    element = &definition->record_elements[position];
                }
                ++position;
            } else if (!is_others(association)) {
                const auto found = std::ranges::find_if(definition->record_elements,
                    [&](const semantic::vhdl::RecordElement& candidate) {
                        return equal_ignoring_ascii_case(candidate.name, association.choice_spelling);
                    });
                if (found != definition->record_elements.end()) {
                    element = &*found;
                }
            }
            if (element == nullptr) {
                continue;
            }
            const auto value = specialization_.find_expression(association.value);
            if (!value || value->vhdl == nullptr) {
                continue;
            }
            if (value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate) {
                validate_aggregate(association.value, element->subtype, depth + 1U);
                continue;
            }
            if (!compatible(association.value, element->subtype)) {
                report("FSIM-ELAB-VHAGG-014",
                    "a VHDL aggregate element does not have the element type",
                    value->vhdl->source);
                return;
            }
        }
    }

    bool generate_parameter_name(const std::string_view name) const
    {
        bool found { };
        const auto visit = [&](const auto& self,
                               const std::vector<semantic::vhdl::GenerateRegion>& regions)
            -> void {
            for (const auto& region : regions) {
                found = found || (!region.iterator.empty() && equal_ignoring_ascii_case(region.iterator, name));
                self(self, region.nested);
            }
        };
        visit(visit, architecture_.generates);
        return found;
    }

    bool physical_unit_name(const std::string_view name) const
    {
        for (const auto& unit : elaboration_detail::vhdl_time_units) {
            if (equal_ignoring_ascii_case(name, unit.first)) {
                return true;
            }
        }
        return std::ranges::any_of(specialization_.design().vhdl_hir.types(),
            [&](const TypeDefinition& type) {
                return std::ranges::any_of(type.physical_units,
                    [&](const semantic::vhdl::PhysicalUnit& unit) {
                        return equal_ignoring_ascii_case(unit.name, name);
                    });
            });
    }

    // IEEE 1076-2008 10.6.2.1: the target of a variable assignment denotes a
    // variable (not a signal, constant or constant-class formal).
    void validate_variable_target(const semantic::ExpressionId target_id,
        const semantic::SourceSpanId source,
        const bool check_aggregate_names = true)
    {
        if (const auto aggregate = specialization_.find_expression(target_id);
            aggregate && aggregate->vhdl != nullptr
            && aggregate->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate) {
            if (check_aggregate_names
                && !aggregate_target_name_is_locally_static(target_id)) {
                report(
                    "FSIM-ELAB-VHASSIGN-005",
                    "each element association of a VHDL variable aggregate "
                    "assignment target must be a locally static name "
                    "(IEEE 1076-2008 10.6.2.1)",
                    aggregate->vhdl->source.valid()
                        ? aggregate->vhdl->source
                        : source);
            }
            for (const auto& association : aggregate->vhdl->associations) {
                validate_variable_target(association.value, source, false);
            }
            return;
        }
        auto prefix_id = target_id;
        for (std::size_t depth { }; depth < 8U; ++depth) {
            const auto target = specialization_.find_expression(prefix_id);
            if (!target || target->vhdl == nullptr) {
                return;
            }
            if ((target->vhdl->kind == semantic::vhdl::ExpressionKind::index
                    || target->vhdl->kind == semantic::vhdl::ExpressionKind::slice)
                && !target->vhdl->operands.empty()) {
                prefix_id = target->vhdl->operands.front();
                continue;
            }
            if (target->vhdl->kind != semantic::vhdl::ExpressionKind::name
                || !target->vhdl->referenced_name
                || target->vhdl->text.find('.') != std::string::npos) {
                return;
            }
            if (!target->vhdl->referenced_name->selected) {
                if (name_check_ && prefix_id != target_id) {
                    validate_simple_name(*target->vhdl);
                }
                // A generate parameter is a constant (11.8); a unit name is
                // not an object.
                if (generate_parameter_name(target->vhdl->text)
                    || physical_unit_name(target->vhdl->text)) {
                    report("FSIM-ELAB-VHASSIGN-003",
                        "the target of a VHDL variable assignment must be a variable, not '"
                            + target->vhdl->text + "'",
                        source);
                }
                return;
            }
            const auto declaration = specialization_.find_declaration(
                *target->vhdl->referenced_name->selected);
            if (!declaration || declaration->vhdl == nullptr) {
                return;
            }
            using Form = semantic::vhdl::DeclarationForm;
            using Class = semantic::vhdl::ObjectClass;
            const auto& found = *declaration->vhdl;
            const bool not_variable = found.form == Form::signal
                || found.form == Form::constant || found.form == Form::generic_constant
                || found.form == Form::function || found.form == Form::procedure
                || (found.form == Form::port && found.object_class != Class::variable);
            if (not_variable) {
                report("FSIM-ELAB-VHASSIGN-003",
                    "the target of a VHDL variable assignment must be a variable, not '"
                        + found.name + "'",
                    source);
            }
            return;
        }
    }

    // IEEE 1076-2008 5.2.1, 5.3.2.2, 14.4.2: a static initial value lies in
    // its scalar subtype; an INTEGER range lies in the implementation's
    // INTEGER (32 bits before VHDL-2019); an index constraint lies in the
    // index subtype (NATURAL or POSITIVE).
    void validate_declaration_ranges(const semantic::vhdl::Declaration& stored)
    {
        using Form = semantic::vhdl::DeclarationForm;
        const auto& subtype = *stored.subtype;
        const auto bound_value = [&](const std::optional<std::int64_t> value,
                                     const std::optional<semantic::ExpressionId> expression)
            -> std::optional<std::int64_t> {
            if (value) {
                return value;
            }
            return expression ? specialization_.evaluate_integral_expression(*expression)
                              : std::nullopt;
        };
        const auto type = subtype_operand_type(subtype, 0U);
        // A bare type mark takes its range from the nearest constrained
        // subtype or the integer/physical type definition.
        const semantic::vhdl::RangeConstraint* marked_range = nullptr;
        if (subtype.constraints.empty()
            && (type.kind == OperandClass::integer
                || type.kind == OperandClass::physical)) {
            auto link_type = subtype_type(subtype);
            std::unordered_set<std::uint32_t> visiting;
            while (link_type && visiting.insert(link_type->value()).second) {
                const auto* link = type_definition(*link_type);
                if (link == nullptr) {
                    break;
                }
                if (link->form == TypeForm::subtype
                    || link->form == TypeForm::alias) {
                    if (link->base.constraints.size() == 1U) {
                        marked_range = &link->base.constraints.front();
                        break;
                    }
                    link_type = subtype_type(link->base);
                    continue;
                }
                if ((link->form == TypeForm::scalar
                        || link->form == TypeForm::physical)
                    && link->scalar_range) {
                    marked_range = &*link->scalar_range;
                }
                break;
            }
        }
        if ((type.kind == OperandClass::integer && subtype.constraints.size() == 1U)
            || marked_range != nullptr) {
            const auto& range = marked_range != nullptr
                ? *marked_range
                : subtype.constraints.front();
            const auto left = bound_value(range.left, range.left_expression);
            const auto right = bound_value(range.right, range.right_expression);
            if (left && right) {
                const auto low = std::min(*left, *right);
                const auto high = std::max(*left, *right);
                if (architecture_.standard < std::string_view { "2019" }
                    && type.root == "integer"
                    && (low < std::numeric_limits<std::int32_t>::min()
                        || high > std::numeric_limits<std::int32_t>::max())) {
                    report("FSIM-ELAB-VHRANGE-002",
                        "a VHDL INTEGER range exceeds the implementation's 32-bit INTEGER",
                        stored.source);
                    return;
                }
                const bool null_range = range.descending ? *left < *right : *left > *right;
                if ((stored.form == Form::constant || stored.form == Form::variable
                        || stored.form == Form::signal)
                    && stored.initializer && !null_range) {
                    if (const auto value = specialization_.evaluate_integral_expression(
                            *stored.initializer);
                        value && (*value < low || *value > high)) {
                        report("FSIM-ELAB-VHRANGE-003",
                            "the initial value of VHDL object '" + stored.name
                                + "' is outside its subtype's range",
                            stored.source);
                        return;
                    }
                }
            }
        }
        // Index constraints of the predefined NATURAL/POSITIVE-indexed arrays.
        auto mark = std::string_view { subtype.type_mark.spelling };
        mark = simple_name_of(mark);
        const std::optional<std::int64_t> minimum
            = equal_ignoring_ascii_case(mark, "bit_vector") ? std::optional<std::int64_t> { 0 }
            : equal_ignoring_ascii_case(mark, "string")     ? std::optional<std::int64_t> { 1 }
                                             : std::nullopt;
        if (minimum && subtype.constraints.size() == 1U) {
            const auto& range = subtype.constraints.front();
            const auto left = bound_value(range.left, range.left_expression);
            const auto right = bound_value(range.right, range.right_expression);
            const bool null_range = left && right
                && (range.descending ? *left < *right : *left > *right);
            if (left && right && !null_range && std::min(*left, *right) < *minimum) {
                report("FSIM-ELAB-VHARRAY-011",
                    "an index constraint bound of VHDL object '" + stored.name
                        + "' is outside the index subtype",
                    stored.source);
            }
        }
    }

    // IEEE 1076-2008 10.9: a case expression is discrete or a
    // one-dimensional character array; each choice has its type, lies in
    // its (locally static) subtype, and an array choice has its length.
    void validate_case(const semantic::vhdl::Statement& statement)
    {
        if (statement.kind != semantic::vhdl::StatementKind::selection
            || !statement.condition || statement.matching_case) {
            return;
        }
        const auto selector_type = operand_type(*statement.condition);
        // A concatenation of BOOLEAN values is an array whose element type
        // is not a character type.
        if (const auto selector = specialization_.find_expression(*statement.condition);
            selector && selector->vhdl != nullptr
            && selector->vhdl->kind == semantic::vhdl::ExpressionKind::binary
            && selector->vhdl->text == "&" && selector->vhdl->operands.size() == 2U
            && std::ranges::all_of(selector->vhdl->operands, [&](const semantic::ExpressionId id) {
                   const auto type = operand_type(id);
                   return type.kind == OperandClass::enumeration && type.root == "boolean";
               })) {
            report("FSIM-ELAB-VHDLCASE-007",
                "a VHDL case expression must be of a discrete type or a one-dimensional "
                "character array type",
                statement.source);
            return;
        }
        if (selector_type.kind == OperandClass::floating) {
            report("FSIM-ELAB-VHDLCASE-007",
                "a VHDL case expression must be of a discrete type or a one-dimensional "
                "character array type",
                statement.source);
            return;
        }
        // The selector's subtype, when it names an object.
        std::optional<SubtypeIndication> selector_subtype;
        if (const auto selector = specialization_.find_expression(*statement.condition);
            selector && selector->vhdl != nullptr
            && selector->vhdl->kind == semantic::vhdl::ExpressionKind::name
            && selector->vhdl->referenced_name
            && selector->vhdl->referenced_name->selected) {
            const auto declaration = specialization_.find_declaration(
                *selector->vhdl->referenced_name->selected);
            if (declaration && declaration->vhdl != nullptr && declaration->vhdl->subtype) {
                selector_subtype = specialized_subtype(*declaration->vhdl->subtype);
            }
        }
        // The selector subtype's static range: its own constraint, else the
        // constraint of the subtype its type mark names.
        std::optional<std::pair<std::int64_t, std::int64_t>> range;
        if (selector_subtype
            && (selector_type.kind == OperandClass::integer
                || selector_type.kind == OperandClass::enumeration)) {
            auto current = *selector_subtype;
            std::unordered_set<std::uint32_t> visiting;
            for (std::size_t depth { }; depth < 8U && !range; ++depth) {
                if (current.constraints.size() == 1U) {
                    const auto& constraint = current.constraints.front();
                    const auto evaluate = [&](const std::optional<std::int64_t> value,
                                              const std::optional<semantic::ExpressionId> id)
                        -> std::optional<std::int64_t> {
                        return value ? value
                            : id ? specialization_.evaluate_integral_expression(*id)
                                 : std::nullopt;
                    };
                    const auto left = evaluate(constraint.left, constraint.left_expression);
                    const auto right = evaluate(constraint.right, constraint.right_expression);
                    if (left && right) {
                        range = std::pair { std::min(*left, *right), std::max(*left, *right) };
                    }
                    break;
                }
                auto name = std::string_view { current.type_mark.spelling };
                name = simple_name_of(name);
                const bool positive
                    = equal_ignoring_ascii_case(name, "positive");
                if (positive || equal_ignoring_ascii_case(name, "natural")) {
                    range = std::pair {
                        std::int64_t { positive ? 1 : 0 },
                        elaboration_detail::vhdl_integer_maximum(
                            architecture_.standard == "2019" ? 64U : 32U)
                    };
                    break;
                }
                const auto type = subtype_type(current);
                if (!type || !visiting.insert(type->value()).second) {
                    break;
                }
                const auto* definition = type_definition(*type);
                if (definition == nullptr
                    || (definition->form != TypeForm::subtype
                        && definition->form != TypeForm::alias)) {
                    break;
                }
                current = definition->base;
            }
        }
        // A fixed selector length for a one-dimensional array selector.
        std::optional<std::int64_t> selector_length;
        if (selector_subtype && selector_type.kind == OperandClass::array) {
            if (const auto shape = profile(*selector_subtype);
                shape.dimensions.size() == 1U && shape.dimensions.front().minimum
                && shape.dimensions.front().maximum) {
                selector_length = *shape.dimensions.front().maximum
                    - *shape.dimensions.front().minimum + 1;
            }
        }
        const auto check_value = [&](const semantic::ExpressionId choice) {
            const auto found = specialization_.find_expression(choice);
            if (!found || found->vhdl == nullptr) {
                return true;
            }
            if (found->vhdl->kind == semantic::vhdl::ExpressionKind::string_literal) {
                if (selector_length && found->vhdl->decoded_string
                    && static_cast<std::int64_t>(found->vhdl->decoded_string->size())
                        != *selector_length) {
                    report("FSIM-ELAB-VHDLCASE-010",
                        "a VHDL case choice does not have the length of the case expression",
                        found->vhdl->source);
                    return false;
                }
                return true;
            }
            if (found->vhdl->kind == semantic::vhdl::ExpressionKind::name
                && found->vhdl->referenced_name && found->vhdl->referenced_name->selected) {
                const auto declaration = specialization_.find_declaration(
                    *found->vhdl->referenced_name->selected);
                if (declaration && declaration->vhdl != nullptr
                    && (declaration->vhdl->form == semantic::vhdl::DeclarationForm::variable
                        || declaration->vhdl->form == semantic::vhdl::DeclarationForm::signal
                        || declaration->vhdl->form == semantic::vhdl::DeclarationForm::port)) {
                    report("FSIM-ELAB-VHDLCASE-011",
                        "a VHDL case choice must be locally static; it names a "
                        "variable or signal",
                        found->vhdl->source);
                    return false;
                }
            }
            const auto type = operand_type(choice);
            if (type.kind != OperandClass::unknown && selector_type.kind != OperandClass::unknown
                && types_conflict(type, selector_type)) {
                report("FSIM-ELAB-VHDLCASE-008",
                    "a VHDL case choice does not have the type of the case expression",
                    found->vhdl->source);
                return false;
            }
            if (range) {
                if (const auto value = specialization_.evaluate_integral_expression(choice);
                    value && (*value < range->first || *value > range->second)) {
                    report("FSIM-ELAB-VHDLCASE-009",
                        "a VHDL case choice lies outside the subtype of the case expression",
                        found->vhdl->source);
                    return false;
                }
            }
            return true;
        };
        for (const auto& alternative : statement.alternatives) {
            if (alternative.is_default) {
                continue;
            }
            for (const auto choice : alternative.choices) {
                const auto found = specialization_.find_expression(choice);
                if (!found || found->vhdl == nullptr) {
                    continue;
                }
                const bool range_choice = found->vhdl->operands.size() == 2U
                    && ((found->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                            && (found->vhdl->text == "to" || found->vhdl->text == "downto"))
                        || (found->vhdl->kind == semantic::vhdl::ExpressionKind::call
                            && found->vhdl->text.starts_with("@vhdl-case-range-")));
                if (range_choice) {
                    if (!check_value(found->vhdl->operands.front())
                        || !check_value(found->vhdl->operands.back())) {
                        return;
                    }
                    continue;
                }
                if (!check_value(choice)) {
                    return;
                }
            }
        }
    }

    // IEEE 1076-2008 8.4: an indexed name has one index for each dimension
    // of its array prefix, and only an array object is indexed. A chain of
    // indices (`a(1)(2)`, or `a(1, 2)`, which the HIR keeps as a chain) is
    // consumed dimension by dimension through the element types.
    void validate_index_chain(const semantic::ExpressionId outer)
    {
        std::vector<semantic::ExpressionId> indices;
        auto current = outer;
        for (std::size_t depth { }; depth < 16U; ++depth) {
            const auto found = specialization_.find_expression(current);
            if (!found || found->vhdl == nullptr) {
                return;
            }
            if (found->vhdl->kind == semantic::vhdl::ExpressionKind::index) {
                if (found->vhdl->operands.size() != 2U) {
                    return;
                }
                const auto index = specialization_.find_expression(
                    found->vhdl->operands.back());
                // A range or subtype name makes a slice, not an element; the
                // prefix of a slice is still an array.
                if (index && index->vhdl != nullptr
                    && index->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                    && (index->vhdl->text == "to" || index->vhdl->text == "downto")) {
                    validate_slice_prefix(found->vhdl->operands.front(), found->vhdl->source);
                    if (index->vhdl->operands.size() == 2U) {
                        validate_slice_bounds(found->vhdl->operands.front(),
                            index->vhdl->operands[0], index->vhdl->operands[1],
                            found->vhdl->source);
                    }
                }
                if (!index || index->vhdl == nullptr
                    || (index->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                        && (index->vhdl->text == "to" || index->vhdl->text == "downto"))
                    || (index->vhdl->kind == semantic::vhdl::ExpressionKind::name
                        && index->vhdl->referenced_name
                        && index->vhdl->referenced_name->selected
                        && [&] {
                               const auto declaration = specialization_.find_declaration(
                                   *index->vhdl->referenced_name->selected);
                               return declaration && declaration->vhdl != nullptr
                                   && (declaration->vhdl->form
                                           == semantic::vhdl::DeclarationForm::type
                                       || declaration->vhdl->form
                                           == semantic::vhdl::DeclarationForm::subtype);
                           }())) {
                    return;
                }
                indices.push_back(found->vhdl->operands.back());
                current = found->vhdl->operands.front();
                continue;
            }
            if (found->vhdl->kind != semantic::vhdl::ExpressionKind::name
                || !found->vhdl->referenced_name
                || !found->vhdl->referenced_name->selected
                || found->vhdl->text.find_first_of(".'") != std::string::npos) {
                return;
            }
            break;
        }
        const auto base = specialization_.find_expression(current);
        const auto declaration = specialization_.find_declaration(
            *base->vhdl->referenced_name->selected);
        using Form = semantic::vhdl::DeclarationForm;
        if (!declaration || declaration->vhdl == nullptr || !declaration->vhdl->subtype
            || (declaration->vhdl->form != Form::variable
                && declaration->vhdl->form != Form::signal
                && declaration->vhdl->form != Form::constant)) {
            return;
        }
        std::ranges::reverse(indices);
        const auto source = specialization_.find_expression(outer);
        auto subtype = specialized_subtype(*declaration->vhdl->subtype);
        std::size_t position { };
        for (std::size_t level { }; level < 8U && position < indices.size(); ++level) {
            const auto* definition = root_definition(subtype);
            if (definition == nullptr) {
                // A predefined scalar type (INTEGER, REAL, TIME, BOOLEAN,
                // CHARACTER) has no retained definition.
                const auto type = subtype_operand_type(subtype, 0U);
                if (numeric_class(type.kind)) {
                    report("FSIM-ELAB-VHARRAYSEL-005",
                        "a VHDL indexed name requires an array prefix",
                        source->vhdl->source);
                }
                return;
            }
            if (definition->form != TypeForm::array) {
                const auto type = subtype_operand_type(subtype, 0U);
                if (scalar_class(type.kind)
                    || type.kind == OperandClass::record) {
                    report("FSIM-ELAB-VHARRAYSEL-005",
                        "a VHDL indexed name requires an array prefix",
                        source->vhdl->source);
                }
                return;
            }
            const auto dimensions = definition->array_dimensions.size();
            if (indices.size() - position < dimensions) {
                report("FSIM-ELAB-VHARRAYSEL-005",
                    "a VHDL indexed name requires one index for each array dimension",
                    source->vhdl->source);
                return;
            }
            position += dimensions;
            if (!definition->element_subtype) {
                return;
            }
            subtype = constrained_element(
                subtype, "", *definition->element_subtype);
        }
    }

    void validate_object_alias(const semantic::vhdl::Declaration& alias)
    {
        const auto* alias_definition = root_definition(*alias.subtype);
        if (architecture_.standard < std::string_view { "2008" }
            && alias_definition != nullptr && alias_definition->form == TypeForm::array
            && alias_definition->array_dimensions.size() > 1U) {
            report("FSIM-ELAB-VHALIAS-001",
                "an object alias of a multidimensional array is not allowed before "
                "VHDL-2008",
                alias.source);
            return;
        }
        if (!alias.alias_target->selected) {
            return;
        }
        const auto target = specialization_.find_declaration(*alias.alias_target->selected);
        using Form = semantic::vhdl::DeclarationForm;
        // IEEE 1076-1993 4.3.3: only an object alias has a subtype
        // indication; a type cannot be aliased as an object.
        if (target && target->vhdl != nullptr
            && (target->vhdl->form == Form::type || target->vhdl->form == Form::subtype)) {
            report("FSIM-ELAB-VHALIAS-004",
                "a VHDL alias with a subtype indication must denote an object, not a type",
                alias.source);
            return;
        }
        if (!target || target->vhdl == nullptr || !target->vhdl->subtype
            || (target->vhdl->form != Form::signal && target->vhdl->form != Form::variable
                && target->vhdl->form != Form::constant && target->vhdl->form != Form::port
                && target->vhdl->form != Form::generic_constant)) {
            return;
        }
        // 6.6.2: an array alias has a matching element for each element of
        // the named object.
        if (alias.initializer) {
            const auto slice = specialization_.find_expression(*alias.initializer);
            if (slice && slice->vhdl != nullptr
                && slice->vhdl->kind == semantic::vhdl::ExpressionKind::slice
                && slice->vhdl->operands.size() == 3U) {
                const auto low = specialization_.evaluate_integral_expression(slice->vhdl->operands[1]);
                const auto high = specialization_.evaluate_integral_expression(slice->vhdl->operands[2]);
                std::optional<std::int64_t> alias_length;
                for (const auto& constraint : alias.subtype->constraints) {
                    if (constraint.kind != semantic::vhdl::RangeKind::array_index) {
                        continue;
                    }
                    const auto left = constraint.left ? constraint.left
                        : constraint.left_expression
                        ? specialization_.evaluate_integral_expression(*constraint.left_expression)
                        : std::nullopt;
                    const auto right = constraint.right ? constraint.right
                        : constraint.right_expression
                        ? specialization_.evaluate_integral_expression(*constraint.right_expression)
                        : std::nullopt;
                    if (left && right) {
                        alias_length = std::max<std::int64_t>(
                            0, (constraint.descending ? *left - *right : *right - *left) + 1);
                    }
                    break;
                }
                if (low && high && alias_length) {
                    const auto slice_length = std::max<std::int64_t>(0,
                        std::max(*low, *high) - std::min(*low, *high) + 1);
                    if (slice_length != *alias_length) {
                        report("FSIM-ELAB-VHALIAS-003",
                            "VHDL alias '" + alias.name
                                + "' does not have one element for each element of the named object",
                            alias.source);
                        return;
                    }
                }
            }
        }
        // An element or slice target has the type of that name.
        const auto expected = alias.initializer
            ? operand_type(*alias.initializer)
            : subtype_operand_type(*target->vhdl->subtype, 0U);
        const auto given = subtype_operand_type(*alias.subtype, 0U);
        if ((scalar_class(expected.kind) || scalar_class(given.kind))
            && expected.kind != OperandClass::unknown && given.kind != OperandClass::unknown
            && types_conflict(expected, given)) {
            report("FSIM-ELAB-VHALIAS-002",
                "the subtype of VHDL alias '" + alias.name
                    + "' does not have the type of the aliased object",
                alias.source);
        }
    }

    // The bound expressions and direction of a slice name, kept as a slice
    // or as an index whose argument is a range.
    struct SliceBounds {
        semantic::ExpressionId left;
        semantic::ExpressionId right;
        bool descending { };
    };
    std::optional<SliceBounds> slice_bounds(
        const semantic::vhdl::Expression& target) const
    {
        if (target.kind == semantic::vhdl::ExpressionKind::slice
            && target.operands.size() == 3U) {
            return SliceBounds { target.operands[1U], target.operands[2U],
                target.text == "downto" };
        }
        if (target.kind != semantic::vhdl::ExpressionKind::index
            || target.operands.size() != 2U) {
            return std::nullopt;
        }
        const auto range = specialization_.find_expression(target.operands[1U]);
        if (!range || range->vhdl == nullptr
            || range->vhdl->kind != semantic::vhdl::ExpressionKind::binary
            || (range->vhdl->text != "to" && range->vhdl->text != "downto")
            || range->vhdl->operands.size() != 2U) {
            return std::nullopt;
        }
        return SliceBounds { range->vhdl->operands[0U],
            range->vhdl->operands[1U], range->vhdl->text == "downto" };
    }

    // Return the subtype determined by a slice target. When its bounds are
    // dynamic, retain the symbolic outer range so aggregate element checking
    // remains contextual without asserting a whole-prefix length. Keep the
    // prefix's nested element constraints while replacing only the
    // selected outer array range (IEEE 1076-2008 9.3.3.3).
    std::optional<SubtypeIndication> slice_assignment_context(
        const semantic::ExpressionId target_id) const
    {
        const auto target = specialization_.find_expression(target_id);
        if (!target || target->vhdl == nullptr
            || target->vhdl->operands.empty()) {
            return std::nullopt;
        }
        const auto bounds = slice_bounds(*target->vhdl);
        if (!bounds) {
            return std::nullopt;
        }
        const auto [left, right, descending] = *bounds;
        const auto low = specialization_.evaluate_integral_expression(left);
        const auto high = specialization_.evaluate_integral_expression(right);
        auto context = expression_subtype(target->vhdl->operands.front());
        if (!context) {
            const auto prefix = specialization_.find_expression(
                target->vhdl->operands.front());
            if (prefix && prefix->vhdl != nullptr
                && prefix->vhdl->referenced_name
                && prefix->vhdl->referenced_name->selected) {
                const auto declaration = specialization_.find_declaration(
                    *prefix->vhdl->referenced_name->selected);
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->subtype) {
                    context = specialized_subtype(
                        *declaration->vhdl->subtype);
                }
            }
        }
        if (!context) {
            return std::nullopt;
        }
        RangeConstraint selected_range;
        selected_range.kind = semantic::vhdl::RangeKind::array_index;
        selected_range.left = low;
        selected_range.right = high;
        selected_range.left_expression = left;
        selected_range.right_expression = right;
        selected_range.descending = descending;
        selected_range.null = low && high
            && (descending ? *low < *high : *low > *high);
        selected_range.source = target->vhdl->source;
        const auto outer_range = std::ranges::find_if(
            context->constraints, [](const RangeConstraint& constraint) {
                return constraint.kind
                    == semantic::vhdl::RangeKind::array_index;
            });
        if (outer_range != context->constraints.end()) {
            *outer_range = std::move(selected_range);
        } else {
            context->constraints.insert(
                context->constraints.begin(), std::move(selected_range));
        }
        context->unconstrained = false;
        context->executable_width.reset();
        return context;
    }

    // IEEE 1076-2008 10.6.2.1: a slice target and an aggregate or string
    // value; the value has the target's element type and, when both are
    // static, its length.
    void validate_slice_assignment(const semantic::ExpressionId target_id,
        const semantic::ExpressionId value_id, const semantic::SourceSpanId source)
    {
        const auto target = specialization_.find_expression(target_id);
        if (!target || target->vhdl == nullptr || target->vhdl->operands.empty()) {
            return;
        }
        const auto bounds = slice_bounds(*target->vhdl);
        if (!bounds) {
            return;
        }
        const auto [left, right, descending] = *bounds;
        validate_slice_prefix(target->vhdl->operands.front(), source);
        // A slice is an array: a scalar value cannot be assigned to it.
        {
            const auto value_kind = operand_type(value_id).kind;
            if (scalar_class(value_kind)) {
                report("FSIM-ELAB-VHASSIGN-001",
                    "a scalar value cannot be assigned to a VHDL slice", source);
                return;
            }
        }
        if (const auto context = slice_assignment_context(target_id)) {
            validate_aggregate(value_id, *context);
        }
        const auto low = specialization_.evaluate_integral_expression(left);
        const auto high = specialization_.evaluate_integral_expression(right);
        if (!low || !high) {
            return;
        }
        const auto length = descending
            ? (*low >= *high ? *low - *high + 1 : 0)
            : (*high >= *low ? *high - *low + 1 : 0);
        const auto value = specialization_.find_expression(value_id);
        if (!value || value->vhdl == nullptr) {
            return;
        }
        std::optional<std::int64_t> value_length;
        if (value->vhdl->kind == semantic::vhdl::ExpressionKind::string_literal
            && value->vhdl->decoded_string) {
            value_length = static_cast<std::int64_t>(value->vhdl->decoded_string->size());
        } else if (value->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate
            && !value->vhdl->associations.empty()
            && std::ranges::all_of(value->vhdl->associations,
                [](const semantic::vhdl::AggregateAssociation& association) {
                    return association.positional();
                })) {
            value_length = static_cast<std::int64_t>(value->vhdl->associations.size());
        }
        if (value_length && *value_length != length) {
            report("FSIM-ELAB-VHASSIGN-004",
                "the value assigned to a VHDL slice does not have the slice's length",
                source);
        }
    }

    // IEEE 1076-2008 10.13: the value of a return statement has the
    // function's result type.
    void validate_return_value(const semantic::vhdl::Statement& statement)
    {
        const auto& scopes = specialization_.design().semantics.scopes();
        std::unordered_set<std::uint32_t> enclosing;
        for (auto scope = statement.scope; scope.valid() && scope.value() < scopes.size();) {
            enclosing.insert(scope.value());
            if (!scopes[scope.value()].parent) {
                break;
            }
            scope = *scopes[scope.value()].parent;
        }
        const semantic::vhdl::Declaration* function = nullptr;
        for (const auto& declaration : specialization_.design().vhdl_hir.declarations()) {
            if (declaration.callable && declaration.callable->function
                && declaration.callable->return_type && declaration.nested_scope
                && declaration.nested_scope == statement.scope) {
                function = &declaration;
                break;
            }
        }
        if (function == nullptr) {
            // The nearest enclosing function body.
            std::size_t best = std::numeric_limits<std::size_t>::max();
            for (const auto& declaration : specialization_.design().vhdl_hir.declarations()) {
                if (!declaration.callable || !declaration.callable->function
                    || !declaration.callable->return_type || !declaration.nested_scope
                    || !enclosing.contains(declaration.nested_scope->value())) {
                    continue;
                }
                std::size_t depth { };
                for (auto scope = statement.scope; scope.valid() && scope.value() < scopes.size()
                     && scope != *declaration.nested_scope;
                     ++depth) {
                    if (!scopes[scope.value()].parent) {
                        break;
                    }
                    scope = *scopes[scope.value()].parent;
                }
                if (depth < best) {
                    best = depth;
                    function = &declaration;
                }
            }
        }
        if (function == nullptr) {
            return;
        }
        // An aggregate result takes the function's result subtype.
        validate_aggregate(*statement.value, *function->callable->return_type);
        const auto expected = subtype_operand_type(*function->callable->return_type, 0U);
        const auto given = operand_type(*statement.value);
        if (scalar_class(expected.kind) && scalar_class(given.kind)
            && types_conflict(given, expected)) {
            report("FSIM-ELAB-VHRETURN-001",
                "the value of a VHDL return statement does not have the function's "
                "result type",
                statement.source);
        }
    }

    void validate_slice_prefix(const semantic::ExpressionId prefix_id,
        const semantic::SourceSpanId source)
    {
        const auto prefix = specialization_.find_expression(prefix_id);
        if (!prefix || prefix->vhdl == nullptr
            || prefix->vhdl->kind != semantic::vhdl::ExpressionKind::name
            || !prefix->vhdl->referenced_name || !prefix->vhdl->referenced_name->selected
            || prefix->vhdl->text.find_first_of(".'") != std::string::npos) {
            return;
        }
        const auto declaration = specialization_.find_declaration(
            *prefix->vhdl->referenced_name->selected);
        using Form = semantic::vhdl::DeclarationForm;
        if (!declaration || declaration->vhdl == nullptr || !declaration->vhdl->subtype
            || (declaration->vhdl->form != Form::variable
                && declaration->vhdl->form != Form::signal
                && declaration->vhdl->form != Form::constant)) {
            return;
        }
        const auto type = subtype_operand_type(*declaration->vhdl->subtype, 0U);
        if (scalar_class(type.kind) || type.kind == OperandClass::record) {
            report("FSIM-ELAB-VHARRAYSEL-005",
                "a VHDL slice requires a one-dimensional array prefix", source);
        }
    }

    // IEEE 1076-1993 6.5: the bounds of a slice's discrete range have one
    // type, the index type of the prefix.
    // The type of an array dimension's explicitly named index subtype.
    OperandType index_operand_type(const semantic::vhdl::ArrayDimension& dimension) const
    {
        const auto& index_name = dimension.index_subtype;
        if (index_name.selected) {
            const auto index_declaration = specialization_.find_declaration(*index_name.selected);
            if (index_declaration && index_declaration->vhdl != nullptr
                && index_declaration->vhdl->declared_type) {
                SubtypeIndication index;
                index.type_mark.spelling = index_name.spelling;
                index.type_mark.target = *index_declaration->vhdl->declared_type;
                return subtype_operand_type(index, 0U);
            }
            return { };
        }
        auto spelling = std::string_view { index_name.spelling };
        spelling.remove_prefix(std::min(spelling.size(), spelling.find_last_of(".:") + 1U));
        for (const std::string_view name : { "character", "boolean", "bit" }) {
            if (equal_ignoring_ascii_case(spelling, name)) {
                return { OperandClass::enumeration, std::string { name } };
            }
        }
        return { };
    }

    // IEEE 1076-2008 5.3.2.2: each discrete range of an index constraint
    // has the type of the corresponding index.
    void validate_index_constraint_types(const semantic::vhdl::Declaration& declaration)
    {
        if (!declaration.subtype) {
            return;
        }
        const auto* array = root_definition(*declaration.subtype);
        if (array == nullptr || array->form != TypeForm::array
            || declaration.subtype->constraints.size() != array->array_dimensions.size()) {
            return;
        }
        for (std::size_t index { }; index < array->array_dimensions.size(); ++index) {
            const auto index_type = index_operand_type(array->array_dimensions[index]);
            const auto& range = declaration.subtype->constraints[index];
            if (index_type.kind != OperandClass::integer
                && index_type.kind != OperandClass::enumeration) {
                continue;
            }
            bool conflict { };
            const auto bound_type = [&](const std::optional<std::int64_t> value,
                                        const std::optional<semantic::ExpressionId> id) {
                return id ? operand_type(*id)
                    : value ? OperandType { OperandClass::integer, { } }
                            : OperandType { };
            };
            for (const auto& type : { bound_type(range.left, range.left_expression),
                     bound_type(range.right, range.right_expression) }) {
                conflict = conflict
                    || (type.kind != OperandClass::unknown && types_conflict(type, index_type));
            }
            if (conflict) {
                report("FSIM-ELAB-VHRANGE-001",
                    "the bounds of a VHDL index constraint must have the index type",
                    range.source.valid() ? range.source : declaration.source);
                return;
            }
        }
    }

    void validate_slice_bounds(const semantic::ExpressionId prefix_id,
        const semantic::ExpressionId left_id, const semantic::ExpressionId right_id,
        const semantic::SourceSpanId source)
    {
        const auto left = operand_type(left_id);
        const auto right = operand_type(right_id);
        if (known_class(left.kind) && known_class(right.kind)
            && types_conflict(left, right)) {
            report("FSIM-ELAB-VHRANGE-001",
                "the bounds of a VHDL range must have the same type", source);
            return;
        }
        const auto prefix = specialization_.find_expression(prefix_id);
        if (!prefix || prefix->vhdl == nullptr
            || prefix->vhdl->kind != semantic::vhdl::ExpressionKind::name
            || !prefix->vhdl->referenced_name || !prefix->vhdl->referenced_name->selected) {
            return;
        }
        const auto declaration = specialization_.find_declaration(
            *prefix->vhdl->referenced_name->selected);
        if (!declaration || declaration->vhdl == nullptr || !declaration->vhdl->subtype) {
            return;
        }
        const auto* array = root_definition(*declaration->vhdl->subtype);
        if (array == nullptr || array->form != TypeForm::array
            || array->array_dimensions.size() != 1U) {
            return;
        }
        const auto& index_name = array->array_dimensions.front().index_subtype;
        const auto index_declaration = index_name.selected
            ? specialization_.find_declaration(*index_name.selected)
            : std::nullopt;
        if (!index_declaration || index_declaration->vhdl == nullptr
            || !index_declaration->vhdl->declared_type) {
            return;
        }
        SubtypeIndication index;
        index.type_mark.spelling = index_name.spelling;
        index.type_mark.target = *index_declaration->vhdl->declared_type;
        const auto index_type = subtype_operand_type(index, 0U);
        if (index_type.kind != OperandClass::integer
            && index_type.kind != OperandClass::enumeration) {
            return;
        }
        for (const auto& bound : { left, right }) {
            if (known_class(bound.kind) && known_class(index_type.kind)
                && types_conflict(bound, index_type)) {
                report("FSIM-ELAB-VHRANGE-001",
                    "a VHDL slice range must have the index type of its prefix",
                    source);
                return;
            }
        }
    }

    // IEEE 1076-2008 9.2.6-9.2.8: the predefined adding, multiplying,
    // remainder and exponentiation operators apply only to numeric operands.
    void validate_predefined_operator(const semantic::vhdl::Expression& expression)
    {
        const auto& operation = expression.text;
        const bool logical = operation == "and" || operation == "or"
            || operation == "nand" || operation == "nor"
            || operation == "xor" || operation == "xnor"
            || operation == "not";
        bool predefined = predefined_operator(expression);
        if (logical && !predefined) {
            bool matching_callable { };
            (void)function_result_subtype(expression, &matching_callable);
            predefined = !matching_callable;
        }
        if (!predefined
            || expression.builtin_operator
                != semantic::vhdl::BuiltinOperatorIdentity::none) {
            return;
        }
        const auto reject = [&] {
            report("FSIM-ELAB-VHOPER-001",
                "the predefined VHDL operator \"" + operation
                    + "\" is not defined for these operand types",
                expression.source);
        };
        // IEEE 1076-2008 9.2.2 predefines array logical operations only
        // for one-dimensional arrays. A qualified multidimensional value
        // must not acquire a logical operator from its packed representation.
        if (logical
            && (expression.kind == semantic::vhdl::ExpressionKind::unary
                || expression.kind == semantic::vhdl::ExpressionKind::binary)) {
            for (const auto operand : expression.operands) {
                const auto shape = expression_profile(operand);
                if (shape && shape->kind == ProfileKind::array
                    && shape->dimensions.size() > 1U) {
                    reject();
                    return;
                }
            }
        }
        if (expression.kind == semantic::vhdl::ExpressionKind::unary
            && expression.operands.size() == 1U) {
            if (operation != "+" && operation != "-" && operation != "abs") {
                return;
            }
            const auto operand = operand_type(expression.operands.front());
            // STRING has no arithmetic; other library arrays may have it
            // (NUMERIC_STD).
            if (known_class(operand.kind) && !numeric_class(operand.kind)
                && !(operand.kind == OperandClass::array
                    && !operand.root.starts_with('#') && operand.root != "string")) {
                reject();
            }
            return;
        }
        if (expression.kind != semantic::vhdl::ExpressionKind::binary
            || expression.operands.size() != 2U) {
            return;
        }
        // IEEE 1076-2008 9.2.3: relational operands have the same type, and
        // no relational operator is predefined for file or protected types.
        if (operation == "=" || operation == "/=" || operation == "<"
            || operation == "<=" || operation == ">" || operation == ">=") {
            const auto left = operand_type(expression.operands[0]);
            const auto right = operand_type(expression.operands[1]);
            if (left.kind == OperandClass::other || right.kind == OperandClass::other) {
                reject();
                return;
            }
            if (!known_class(left.kind) || !known_class(right.kind)) {
                return;
            }
            const auto library_array = [](const OperandType& operand) {
                return operand.kind == OperandClass::array
                    && !operand.root.starts_with('#');
            };
            const auto standard_vector = [](const OperandType& operand) {
                return operand.kind == OperandClass::array
                    && (operand.root == "string" || operand.root == "bit_vector");
            };
            if ((library_array(left) || library_array(right))
                && !(standard_vector(left) && standard_vector(right))) {
                return;
            }
            // Ordering is defined for scalar and discrete array types only
            // (9.2.3).
            if ((operation == "<" || operation == "<=" || operation == ">"
                    || operation == ">=")
                && (left.kind == OperandClass::record || right.kind == OperandClass::record
                    || left.kind == OperandClass::access
                    || right.kind == OperandClass::access)) {
                reject();
                return;
            }
            // A universal literal converts to any type of its class.
            const auto mismatch = left.kind != right.kind
                ? !(left.kind == OperandClass::null || right.kind == OperandClass::null)
                : roots_conflict(left.root, right.root);
            if (mismatch) {
                reject();
            }
            return;
        }
        const auto adding = operation == "+" || operation == "-";
        const auto multiplying = operation == "*" || operation == "/";
        const auto remainder = operation == "mod" || operation == "rem";
        const auto power = operation == "**";
        if (!adding && !multiplying && !remainder && !power) {
            return;
        }
        const auto left = operand_type(expression.operands[0]);
        const auto right = operand_type(expression.operands[1]);
        // Array arithmetic comes from library packages (numeric_std,
        // numeric_bit) whose operators the elaborator provides natively
        // rather than as visible declarations.
        const auto library_array = [&](const OperandType& operand,
                                       const semantic::ExpressionId id) {
            if (operand.kind != OperandClass::array) {
                return false;
            }
            if (!operand.root.starts_with('#')) {
                return true;
            }
            // A retained numeric_std type has a declaration-backed root ID.
            // Resolve its package identity in the operand's lexical scope;
            // the root's spelling alone cannot distinguish it from a user
            // array or a locally shadowed SIGNED/UNSIGNED declaration.
            const auto subtype = expression_subtype(id);
            const auto value = specialization_.find_expression(id);
            if (!subtype || !value || value->vhdl == nullptr) {
                return false;
            }
            const auto identity = predefined_array_identity(
                *subtype, value->vhdl->scope);
            return identity == "ieee.numeric_std.signed"
                || identity == "ieee.numeric_std.unsigned";
        };
        // STRING has no arithmetic, and BIT_VECTOR has none before
        // VHDL-2008 (NUMERIC_BIT_UNSIGNED); string and bit-string literals
        // take their type from the other operand.
        const auto pre_2008 = architecture_.standard == "1987"
            || architecture_.standard == "1993" || architecture_.standard == "2000"
            || architecture_.standard == "2002";
        const auto no_arithmetic = [&](const OperandType& operand) {
            return operand.kind == OperandClass::array
                && (operand.root == "string"
                    || (pre_2008 && operand.root == "bit_vector"));
        };
        const auto literal = [](const OperandType& operand) {
            return operand.kind == OperandClass::array && operand.root == "@literal";
        };
        if (no_arithmetic(left) || no_arithmetic(right)
            || (literal(left) && literal(right))) {
            reject();
            return;
        }
        if (library_array(left, expression.operands[0])
            || library_array(right, expression.operands[1])) {
            return;
        }
        const auto non_numeric = [&](const OperandType& operand) {
            return known_class(operand.kind) && !numeric_class(operand.kind);
        };
        if (non_numeric(left) || non_numeric(right)) {
            reject();
            return;
        }
        if (!known_class(left.kind) || !known_class(right.kind)) {
            return;
        }
        const auto same_type = !types_conflict(left, right);
        if (adding) {
            if (!same_type) {
                reject();
            }
            return;
        }
        if (remainder) {
            // VHDL-2008 also predefines mod and rem for physical types.
            const auto physical_remainder = left.kind == OperandClass::physical
                && architecture_.standard != "1987" && architecture_.standard != "1993"
                && architecture_.standard != "2000" && architecture_.standard != "2002";
            if ((left.kind != OperandClass::integer && !physical_remainder)
                || !same_type) {
                reject();
            }
            return;
        }
        if (power) {
            if (left.kind == OperandClass::physical
                || right.kind != OperandClass::integer
                || (!right.root.empty() && right.root != "integer")) {
                reject();
            }
            return;
        }
        // Multiplying operators.
        const auto left_physical = left.kind == OperandClass::physical;
        const auto right_physical = right.kind == OperandClass::physical;
        if (!left_physical && !right_physical) {
            const auto universal_mix = left.root.empty() && right.root.empty();
            if (!same_type && !universal_mix) {
                reject();
            }
            return;
        }
        if (left_physical && right_physical) {
            if (operation != "/" || !same_type) {
                reject();
            }
            return;
        }
        if (right_physical && operation == "/") {
            reject();
        }
    }

    std::optional<SubtypeIndication> array_element_subtype(
        const SubtypeIndication& original) const
    {
        auto subtype = specialized_subtype(original);
        auto type = subtype_type(subtype);
        if (!type) {
            type = subtype_type(original);
        }
        std::unordered_set<std::uint32_t> visiting;
        while (type && type->valid()
            && visiting.insert(type->value()).second) {
            const auto* definition = type_definition(*type);
            if (definition == nullptr) {
                return std::nullopt;
            }
            if (definition->form == TypeForm::array) {
                return definition->element_subtype
                    ? std::optional { constrained_element(
                          subtype, "", *definition->element_subtype) }
                    : std::nullopt;
            }
            if ((definition->form != TypeForm::subtype
                    && definition->form != TypeForm::alias)
                || !definition->base.type_mark.target.valid()) {
                return std::nullopt;
            }
            type = definition->base.type_mark.target;
        }
        return std::nullopt;
    }

    bool aggregate_element_compatible(
        const SubtypeIndication& expected,
        const semantic::ExpressionId actual_id,
        const semantic::ScopeId expected_declaration_scope = {}) const
    {
        const auto actual = specialization_.find_expression(actual_id);
        if (!actual || actual->vhdl == nullptr) {
            return true;
        }
        if (actual->vhdl->kind
            == semantic::vhdl::ExpressionKind::logic_literal) {
            return true;
        }
        if (actual->vhdl->kind
            == semantic::vhdl::ExpressionKind::string_literal) {
            return contextual_string_array_value_for(
                actual_id, expected, actual->vhdl->scope,
                expected_declaration_scope);
        }
        if (actual->vhdl->kind
            == semantic::vhdl::ExpressionKind::aggregate) {
            const auto expected_kind = profile(expected).kind;
            return expected_kind == ProfileKind::array
                || expected_kind == ProfileKind::record;
        }
        const auto actual_subtype = expression_subtype(actual_id);
        if (!actual_subtype) {
            return true;
        }
        const auto expected_kind = profile(expected).kind;
        const auto actual_kind = profile(*actual_subtype).kind;
        // An array value cannot serve as a scalar aggregate element (or vice
        // versa). This also rejects a full-rank array where a nested
        // multidimensional aggregate requires a single scalar element.
        if (expected_kind != ProfileKind::unknown
            && actual_kind != ProfileKind::unknown) {
            const bool expected_array
                = expected_kind == ProfileKind::array;
            const bool actual_array = actual_kind == ProfileKind::array;
            const bool expected_record
                = expected_kind == ProfileKind::record;
            const bool actual_record
                = actual_kind == ProfileKind::record;
            if (expected_array != actual_array
                || expected_record != actual_record) {
                return false;
            }
        }
        const auto expected_type = subtype_type(expected);
        const auto actual_type = subtype_type(*actual_subtype);
        const auto expected_root = expected_type
            ? array_root(*expected_type)
            : std::nullopt;
        const auto actual_root = actual_type
            ? array_root(*actual_type)
            : std::nullopt;
        return !expected_root || !actual_root
            || *expected_root == *actual_root;
    }

    std::optional<std::uint64_t> positional_array_segment_count(
        const semantic::ExpressionId value_id,
        const SubtypeIndication& expected_array,
        const SubtypeIndication& expected_element,
        const semantic::ScopeId scope,
        const std::size_t depth = 0U) const
    {
        if (depth >= 64U) {
            return std::nullopt;
        }
        // Array-valued positional segments and their nested positional form
        // are a VHDL-2008 aggregate extension, regardless of whether the
        // segment is written as a slice, a typed array object, or an
        // untyped nested aggregate.
        if (unit_standard(scope) < std::string_view { "2008" }) {
            return std::nullopt;
        }
        const auto expected_profile = profile(
            expected_array);
        if (expected_profile.kind != ProfileKind::array
            || expected_profile.dimensions.size() != 1U) {
            return std::nullopt;
        }
        const auto* expected_definition
            = expected_array.type_mark.target.valid()
            ? root_definition(expected_array)
            : nullptr;
        const auto expected_declaration_scope
            = expected_definition != nullptr
            ? type_declaration_scope(*expected_definition)
            : semantic::ScopeId { };
        const auto value = specialization_.find_expression(value_id);
        if (!value || value->vhdl == nullptr) {
            return std::nullopt;
        }
        if (value->vhdl->kind
            == semantic::vhdl::ExpressionKind::string_literal) {
            if (contextual_string_array_value_for(
                    value_id, expected_array, scope,
                    expected_declaration_scope)) {
                return literal_width(value_id);
            }
            return std::nullopt;
        }
        if (value->vhdl->kind
            == semantic::vhdl::ExpressionKind::slice) {
            if (value->vhdl->operands.size() != 3U
                || (value->vhdl->text != "to"
                    && value->vhdl->text != "downto")
                || unit_standard(value->vhdl->scope)
                    < std::string_view { "2008" }) {
                return std::nullopt;
            }
            const auto prefix = expression_subtype(
                value->vhdl->operands.front());
            if (!prefix || !same_nominal_array_type(
                    expected_array, *prefix, scope)) {
                return std::nullopt;
            }
            const auto left = bound(
                std::nullopt, value->vhdl->operands[1]);
            const auto right = bound(
                std::nullopt, value->vhdl->operands[2]);
            if (!left || !right) {
                return std::nullopt;
            }
            const bool descending = value->vhdl->text == "downto";
            if ((descending && *left < *right)
                || (!descending && *left > *right)) {
                return std::uint64_t { 0U };
            }
            return inclusive_extent(
                std::min(*left, *right), std::max(*left, *right));
        }

        const auto actual = expression_subtype(value_id);
        if (actual) {
            if (!same_nominal_array_type(expected_array, *actual, scope)) {
                return std::nullopt;
            }
            const auto actual_profile = profile(*actual);
            if (actual_profile.kind != ProfileKind::array
                || actual_profile.dimensions.size() != 1U) {
                return std::nullopt;
            }
            const auto& dimension = actual_profile.dimensions.front();
            if (dimension.null_known && dimension.null) {
                return std::uint64_t { 0U };
            }
            if (dimension.constrained) {
                if (!dimension.bounds_known || !dimension.null_known
                    || !dimension.minimum || !dimension.maximum) {
                    return std::nullopt;
                }
                return inclusive_extent(
                    *dimension.minimum, *dimension.maximum);
            }

            // A qualified array aggregate may have an unconstrained subtype
            // in HIR even though its operand gives an exact positional
            // cardinality. Unwrap only the compiler's qualified-expression
            // marker after verifying exact nominal identity and rank above.
            constexpr std::string_view qualified_prefix {
                semantic::vhdl::qualified_expression_prefix
            };
            if (value->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && value->vhdl->text.starts_with(qualified_prefix)
                && value->vhdl->operands.size() == 1U) {
                const auto operand_id = value->vhdl->operands.front();
                const auto operand
                    = specialization_.find_expression(operand_id);
                const auto operand_scope
                    = operand && operand->vhdl != nullptr
                        ? operand->vhdl->scope
                        : value->vhdl->scope;
                return positional_array_segment_count(
                    operand_id, expected_array, expected_element,
                    operand_scope, depth + 1U);
            }
            return std::nullopt;
        }

        // An untyped positional subaggregate can be counted recursively only
        // when its element values are scalar. For array-valued elements, each
        // nested aggregate is one element, not a flattened segment.
        if (value->vhdl->kind
                != semantic::vhdl::ExpressionKind::aggregate) {
            return std::nullopt;
        }
        const auto element_profile = profile(
            expected_element);
        const bool scalar_element
            = element_profile.kind == ProfileKind::scalar
            || element_profile.kind == ProfileKind::integer
            || element_profile.kind == ProfileKind::enumeration;
        if (!scalar_element
            || !std::ranges::all_of(
                value->vhdl->associations, [](const auto& association) {
                    return association.positional();
                })) {
            return std::nullopt;
        }
        std::uint64_t count { };
        for (const auto& association : value->vhdl->associations) {
            const auto child
                = specialization_.find_expression(association.value);
            const auto child_scope = child && child->vhdl != nullptr
                ? child->vhdl->scope
                : value->vhdl->scope;
            auto increment = positional_array_segment_count(
                association.value, expected_array, expected_element,
                child_scope, depth + 1U);
            if (!increment) {
                const auto child_type
                    = expression_subtype(association.value);
                if (child_type) {
                    if (profile(*child_type).kind
                        == ProfileKind::array) {
                        // Do not silently count an array-valued expression
                        // as one scalar element when its segment length is
                        // unavailable.
                        return std::nullopt;
                    }
                }
                if (!aggregate_element_compatible(
                        expected_element, association.value,
                        expected_declaration_scope)) {
                    return std::nullopt;
                }
                increment = 1U;
            }
            if (*increment
                > std::numeric_limits<std::uint64_t>::max() - count) {
                return std::nullopt;
            }
            count += *increment;
        }
        return count;
    }

    const TypeDefinition* root_definition(
        const SubtypeIndication& original) const
    {
        auto type = subtype_type(specialized_subtype(original));
        if (!type) {
            type = subtype_type(original);
        }
        return root_type_definition(type);
    }

    // The definition at the end of a subtype/alias chain, resolving each
    // base by its type mark (or spelling when the mark has no target).
    const TypeDefinition* root_type_definition(
        std::optional<semantic::TypeId> type) const
    {
        std::unordered_set<std::uint32_t> visiting;
        while (type && visiting.insert(type->value()).second) {
            const auto* definition = type_definition(*type);
            if (definition == nullptr) {
                return nullptr;
            }
            if (definition->form != TypeForm::subtype
                && definition->form != TypeForm::alias) {
                return definition;
            }
            type = subtype_type(definition->base);
        }
        return nullptr;
    }

    std::optional<std::string> predefined_array_identity(
        const SubtypeIndication& subtype,
        const semantic::ScopeId scope,
        std::unordered_set<std::uint32_t>* visiting = nullptr) const
    {
        std::unordered_set<std::uint32_t> local_visiting;
        if (visiting == nullptr) {
            visiting = &local_visiting;
        }
        std::optional<semantic::TypeId> target;
        if (subtype.type_mark.target.valid()) {
            target = subtype.type_mark.target;
        } else if (scope.valid()) {
            // Some linked package subtype marks retain only their source
            // spelling. Resolve that spelling in the use scope before
            // classifying its base; a same-name hidden or sibling type must
            // not become a predefined vector by name alone.
            target = named_type(subtype.type_mark.spelling, scope);
        }
        if (target) {
            if (visiting->size() >= 64U
                || !visiting->insert(target->value()).second) {
                return std::nullopt;
            }
        }
        if (subtype.builtin_type != semantic::vhdl::BuiltinTypeIdentity::none) {
            return std::string { semantic::vhdl::builtin_type_package(
                       subtype.builtin_type) }
                + "." + std::string { semantic::vhdl::builtin_type_name(
                    subtype.builtin_type) };
        }
        if (target) {
            const auto* retained = type_definition(*target);
            if (retained != nullptr
                && (retained->form == TypeForm::subtype
                    || retained->form == TypeForm::alias)) {
                const auto declaration = retained->declaration.valid()
                    ? specialization_.find_declaration(
                          retained->declaration)
                    : std::nullopt;
                const auto declaration_scope
                    = declaration && declaration->vhdl != nullptr
                        ? declaration->vhdl->scope
                        : semantic::ScopeId { };
                if (declaration_scope.valid()) {
                    // Resolve a retained subtype's base where that subtype
                    // was declared. A use-site scope or design-wide fallback
                    // can select an unrelated same-name array, while the
                    // subtype's lexical scope preserves both imported
                    // standard types and local shadows.
                    return predefined_array_identity(
                        retained->base, declaration_scope, visiting);
                }
                // A declaration-backed alias with no recoverable owner is
                // ambiguous; do not let a global same-name lookup establish
                // its predefined identity. Compiler-created aliases without
                // declarations can still use the ownerless-root path below.
                if (retained->declaration.valid()) {
                    return std::nullopt;
                }
            }
        }
        constexpr std::array standard_arrays {
            std::string_view { "string" },
            std::string_view { "bit_vector" },
            std::string_view { "boolean_vector" },
            std::string_view { "integer_vector" },
            std::string_view { "real_vector" },
            std::string_view { "time_vector" },
        };
        // An array type's root definition and the unit declaring it.
        const auto root_and_owner
            = [&](const std::optional<semantic::TypeId> root)
            -> std::pair<const semantic::vhdl::TypeDefinition*,
                const semantic::vhdl::Unit*> {
            const auto* definition = root ? type_definition(*root) : nullptr;
            const auto declaration = definition != nullptr
                ? specialization_.find_declaration(definition->declaration)
                : std::nullopt;
            return { definition,
                declaration && declaration->vhdl != nullptr
                    ? specialization_.design().vhdl_scope_unit(
                          declaration->vhdl->scope)
                    : nullptr };
        };
        const auto standard_array = [&](const std::string_view name) {
            return std::ranges::find(standard_arrays, name)
                != standard_arrays.end();
        };
        const auto standard_revision_available = [&](const std::string_view name) {
            return (name != "boolean_vector" && name != "integer_vector"
                    && name != "real_vector" && name != "time_vector")
                || unit_standard(scope) >= std::string_view { "2008" };
        };
        const auto standard_qualified_name = [&](const std::string_view name) {
            return "std.standard." + std::string { name };
        };
        if (target) {
            // A retained array declaration is authoritative, including a
            // user type that shadows a predefined spelling. A constrained
            // subtype can retain its own TypeDefinition while the linked
            // standard-library terminal type is omitted, so follow retained
            // subtype and alias bases before rejecting an unresolved root.
            const auto root = array_root(*target);
            if (root) {
                const auto* definition = type_definition(*root);
                if (definition == nullptr) {
                    return std::nullopt;
                }
                const auto declaration = specialization_.find_declaration(
                    definition->declaration);
                const semantic::vhdl::Unit* owner
                    = declaration && declaration->vhdl != nullptr
                    ? specialization_.design().vhdl_scope_unit(
                          declaration->vhdl->scope)
                    : nullptr;
                const auto type_name = lowercase_ascii_copy(
                    simple_name_of(definition->name));
                if (semantic::vhdl::is_package(owner, "std", "standard")
                    && standard_array(type_name)
                    && standard_revision_available(type_name)) {
                    return standard_qualified_name(type_name);
                }
                {
                    if (semantic::vhdl::is_package(
                            owner, "ieee", "std_logic_1164")
                        && (type_name == "std_logic_vector"
                            || type_name == "std_ulogic_vector")) {
                        return "ieee.std_logic_1164." + type_name;
                    }
                    const bool numeric_std_package
                        = semantic::vhdl::is_package(
                            owner, "ieee", "numeric_std");
                    const bool numeric_bit_package
                        = semantic::vhdl::is_package(
                            owner, "ieee", "numeric_bit");
                    if (numeric_std_package || numeric_bit_package) {
                        const auto numeric_type
                            = std::string_view { type_name };
                        const auto resolved_numeric_type
                            = numeric_std_package
                                && numeric_type.starts_with("unresolved_")
                            ? numeric_type.substr(
                                  std::string_view { "unresolved_" }.size())
                            : numeric_type;
                        if (resolved_numeric_type == "signed"
                            || resolved_numeric_type == "unsigned") {
                            return std::string { "ieee." }
                                + (numeric_bit_package
                                    ? "numeric_bit." : "numeric_std.")
                                + std::string { resolved_numeric_type };
                        }
                    }
                }
                // Compiler-created predefined array roots can carry a
                // TypeDefinition without a source declaration or owner unit.
                // The resolved target chain above proves the referenced type;
                // only use this name fallback when there is no declaration
                // that could represent a user shadow.
                if (!definition->declaration.valid()
                    && standard_array(type_name)
                    && standard_revision_available(type_name)) {
                    return standard_qualified_name(type_name);
                }
                return std::nullopt;
            }
            const auto* retained = type_definition(*target);
            if (retained != nullptr) {
                if (retained->form == TypeForm::subtype
                    || retained->form == TypeForm::alias) {
                    if (const auto inherited = predefined_array_identity(
                            retained->base, scope, visiting)) {
                        return inherited;
                    }
                }
                return std::nullopt;
            }
        }
        auto spelling = std::string_view { subtype.type_mark.spelling };
        constexpr auto compiler_builtin = semantic::vhdl::builtin_prefix;
        const bool compiler_owned = spelling.starts_with(compiler_builtin);
        if (compiler_owned) {
            spelling.remove_prefix(compiler_builtin.size());
        }
        auto canonical = lowercase_ascii_copy(spelling);
        for (const auto prefix : { std::string_view { "standard." },
                 std::string_view { "std.standard." } }) {
            if (!canonical.starts_with(prefix)) {
                continue;
            }
            const auto name = std::string_view { canonical }.substr(
                prefix.size());
            if (standard_array(name) && standard_revision_available(name)) {
                return standard_qualified_name(name);
            }
            return std::nullopt;
        }
        constexpr std::array ieee_arrays {
            std::string_view { "ieee.std_logic_1164.std_logic_vector" },
            std::string_view { "ieee.std_logic_1164.std_ulogic_vector" },
            std::string_view { "ieee.numeric_std.signed" },
            std::string_view { "ieee.numeric_std.unsigned" },
            std::string_view { "ieee.numeric_bit.signed" },
            std::string_view { "ieee.numeric_bit.unsigned" },
        };
        if (std::ranges::find(ieee_arrays, canonical) != ieee_arrays.end()) {
            return canonical;
        }
        const bool numeric_bit_or_std_vector
            = canonical == "signed" || canonical == "unsigned";
        if (numeric_bit_or_std_vector) {
            semantic::vhdl::Name reference;
            reference.spelling = canonical;
            reference.canonical = canonical;
            const semantic::CompiledDeclarationPredicate type_declaration
                = semantic::is_vhdl_type_declaration;
            const semantic::CompiledDesignResolver resolver {
                specialization_ };
            const auto resolution = resolver.resolve_vhdl(
                reference, scope, type_declaration);
            if (resolution.status
                == semantic::CompiledResolutionStatus::ambiguous) {
                return std::nullopt;
            }
            if (const auto declaration_id = resolution.unique()) {
                const auto declaration
                    = specialization_.find_declaration(*declaration_id);
                const auto root = declaration
                        && declaration->vhdl != nullptr
                        && declaration->vhdl->declared_type
                    ? array_root(*declaration->vhdl->declared_type)
                    : std::nullopt;
                const auto [definition, owner] = root_and_owner(root);
                if (definition != nullptr
                    && (semantic::vhdl::is_package(owner, "ieee", "numeric_std")
                        || semantic::vhdl::is_package(
                            owner, "ieee", "numeric_bit"))
                    && equal_ignoring_ascii_case(
                        simple_name_of(definition->name), canonical)) {
                    const auto package = equal_ignoring_ascii_case(
                        owner->name, "numeric_bit")
                        ? std::string_view { "numeric_bit" }
                        : std::string_view { "numeric_std" };
                    return "ieee." + std::string { package }
                        + "." + canonical;
                }
                return std::nullopt;
            }
            if (resolution.status
                    != semantic::CompiledResolutionStatus::not_found
                || !resolution.candidates.empty()) {
                return std::nullopt;
            }
            std::optional<std::string> imported_identity;
            for (const auto package : {
                     std::string_view { "numeric_std" },
                     std::string_view { "numeric_bit" } }) {
                if (!resolver.vhdl_builtin_package_member_imported(
                        "ieee", package, canonical, scope)) {
                    continue;
                }
                if (imported_identity) {
                    // Importing the same simple vector name from both IEEE
                    // numeric packages is ambiguous without a retained type.
                    return std::nullopt;
                }
                imported_identity = "ieee." + std::string { package }
                    + "." + canonical;
            }
            if (imported_identity) {
                return imported_identity;
            }
            if (compiler_owned) {
                // Preserve the existing compiler-provided numeric_std
                // fallback only when no source declaration or package import
                // can identify the unqualified vector name.
                return "ieee.numeric_std." + canonical;
            }
            return std::nullopt;
        }
        // A targetless IEEE array type is compiler-owned only when its package
        // member is imported and ordinary declaration lookup finds no
        // shadowing type. The linked design may omit the package's HIR type
        // declaration while still retaining the import.
        const auto ieee_package = [&]() -> std::optional<std::string_view> {
            if (canonical == "std_logic_vector"
                || canonical == "std_ulogic_vector") {
                return "std_logic_1164";
            }
            return std::nullopt;
        }();
        if (ieee_package) {
            const auto qualified
                = "ieee." + std::string { *ieee_package } + "." + canonical;
            if (compiler_owned) {
                return qualified;
            }
            semantic::vhdl::Name reference;
            reference.spelling = canonical;
            reference.canonical = canonical;
            const semantic::CompiledDeclarationPredicate type_declaration
                = semantic::is_vhdl_type_declaration;
            const semantic::CompiledDesignResolver resolver {
                specialization_ };
            const auto resolution = resolver.resolve_vhdl(
                reference, scope, type_declaration);
            if (resolution.status
                    == semantic::CompiledResolutionStatus::not_found
                && resolution.candidates.empty()
                && resolver.vhdl_builtin_package_member_imported(
                    "ieee", *ieee_package, canonical, scope)) {
                return qualified;
            }
            const auto declaration_id = resolution.unique();
            const auto declaration = declaration_id
                ? specialization_.find_declaration(*declaration_id)
                : std::nullopt;
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->declared_type) {
                const auto root = array_root(
                    *declaration->vhdl->declared_type);
                const auto [definition, owner] = root_and_owner(root);
                if (definition != nullptr
                    && semantic::vhdl::is_package(owner, "ieee", *ieee_package)
                    && equal_ignoring_ascii_case(
                        simple_name_of(definition->name), canonical)) {
                    return qualified;
                }
            }
            return std::nullopt;
        }
        if (!standard_array(canonical)
            || !standard_revision_available(canonical)) {
            return std::nullopt;
        }
        if (compiler_owned) {
            return standard_qualified_name(canonical);
        }
        semantic::vhdl::Name reference;
        reference.spelling = canonical;
        reference.canonical = reference.spelling;
        const semantic::CompiledDeclarationPredicate type_declaration
            = semantic::is_vhdl_type_declaration;
        const auto resolution = semantic::CompiledDesignResolver {
            specialization_ }
                                      .resolve_vhdl(
                                          reference, scope,
                                          type_declaration);
        if (resolution.status
            == semantic::CompiledResolutionStatus::not_found) {
            return standard_qualified_name(canonical);
        }
        const auto declaration_id = resolution.unique();
        const auto declaration = declaration_id
            ? specialization_.find_declaration(*declaration_id)
            : std::nullopt;
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->declared_type) {
            return std::nullopt;
        }
        const auto root = array_root(
            *declaration->vhdl->declared_type);
        const auto* definition = root ? type_definition(*root) : nullptr;
        const auto definition_declaration = definition != nullptr
            ? specialization_.find_declaration(definition->declaration)
            : std::nullopt;
        const semantic::vhdl::Unit* owner
            = definition_declaration && definition_declaration->vhdl != nullptr
            ? specialization_.design().vhdl_scope_unit(
                  definition_declaration->vhdl->scope)
            : nullptr;
        return definition != nullptr
                && semantic::vhdl::is_package(owner, "std", "standard")
                && equal_ignoring_ascii_case(
                    simple_name_of(definition->name), canonical)
            ? std::optional { standard_qualified_name(canonical) }
            : std::nullopt;
    }

    // Whether every character of a string literal is a CHARACTER literal of
    // the enumeration type `element` denotes through unconstrained subtypes
    // and aliases; empty when `element` denotes no such type.
    std::optional<bool> string_enumeration_membership(const std::string& text,
        const SubtypeIndication& element,
        const semantic::ScopeId type_scope) const
    {
        auto element_type = element.type_mark.target;
        if (!element_type.valid()) {
            semantic::vhdl::Name reference;
            reference.spelling = element.type_mark.spelling;
            reference.canonical = reference.spelling;
            const semantic::CompiledDeclarationPredicate type_declaration
                = semantic::declares_vhdl_type;
            const auto declaration
                = semantic::CompiledDesignResolver { specialization_ }
                      .resolve_vhdl(
                          reference, type_scope, type_declaration)
                      .unique();
            const auto view = declaration
                ? specialization_.find_declaration(*declaration)
                : std::nullopt;
            if (!view || view->vhdl == nullptr
                || !view->vhdl->declared_type) {
                return std::nullopt;
            }
            element_type = *view->vhdl->declared_type;
        }
        std::unordered_set<std::uint32_t> visited;
        while (element_type.valid()
            && visited.insert(element_type.value()).second) {
            const auto* definition = type_definition(element_type);
            if (definition == nullptr) {
                return std::nullopt;
            }
            if (definition->form == TypeForm::enumeration) {
                return std::ranges::all_of(
                    text, [&](const char character) {
                        std::string literal_spelling { '\'' };
                        literal_spelling.push_back(character);
                        literal_spelling.push_back('\'');
                        return std::ranges::any_of(
                            definition->enumeration_literals,
                            [&](const auto& literal) {
                                return literal.spelling
                                    == literal_spelling;
                            });
                    });
            }
            if (definition->form != TypeForm::subtype
                && definition->form != TypeForm::alias) {
                return std::nullopt;
            }
            if (definition->scalar_range
                || !definition->base.constraints.empty()) {
                return std::nullopt;
            }
            element_type = definition->base.type_mark.target;
        }
        return std::nullopt;
    }

    bool contextual_string_array_value_for(
        const semantic::ExpressionId value,
        const SubtypeIndication& expected_array,
        const semantic::ScopeId operand_scope,
        const semantic::ScopeId expected_declaration_scope = {}) const
    {
        const auto expression = specialization_.find_expression(value);
        if (!expression || expression->vhdl == nullptr
            || expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::string_literal
            || !expression->vhdl->decoded_string) {
            return false;
        }
        const auto& text = *expression->vhdl->decoded_string;
        const auto type_scope = expected_declaration_scope.valid()
            ? expected_declaration_scope
            : operand_scope;
        if (const auto identity
            = predefined_array_identity(expected_array, type_scope)) {
            if (*identity == "std.standard.string") {
                return true;
            }
            const auto allowed = *identity == "std.standard.bit_vector"
                ? std::string_view { "01" }
                : (*identity == "ieee.std_logic_1164.std_logic_vector"
                      || *identity
                          == "ieee.std_logic_1164.std_ulogic_vector"
                      || *identity == "ieee.numeric_std.signed"
                      || *identity == "ieee.numeric_std.unsigned")
                ? std::string_view { "UX01ZWLH-" }
                : std::string_view { };
            return !allowed.empty()
                && std::ranges::all_of(text, [&](const char character) {
                       return allowed.find(character)
                           != std::string_view::npos;
                   });
        }

        const auto* array = root_definition(expected_array);
        if (array == nullptr || array->form != TypeForm::array
            || array->array_dimensions.size() != 1U
            || !array->element_subtype) {
            return false;
        }
        const auto element = array_element_subtype(expected_array);
        if (!element || !element->constraints.empty()) {
            return false;
        }
        if (profile(*element).kind
            != ProfileKind::enumeration) {
            // STD.STANDARD.CHARACTER may not have a retained TypeDefinition.
            // Resolve the spelling in the expected type's declaration scope
            // before accepting this compiler-owned implicit type, so a visible
            // user `character` type never inherits the builtin identity.
            auto spelling = std::string_view {
                element->type_mark.spelling
            };
            const auto separator = spelling.find_last_of(".:");
            if (separator != std::string_view::npos) {
                spelling.remove_prefix(separator + 1U);
            }
            if (!equal_ignoring_ascii_case(spelling, "character")) {
                return false;
            }
            const auto qualified
                = lowercase_ascii_copy(element->type_mark.spelling);
            if (qualified == "standard.character"
                || qualified == "std.standard.character") {
                return true;
            }
            semantic::vhdl::Name reference;
            reference.spelling = element->type_mark.spelling;
            reference.canonical = reference.spelling;
            const semantic::CompiledDeclarationPredicate type_declaration
                = semantic::is_vhdl_type_declaration;
            const auto visible = semantic::CompiledDesignResolver {
                specialization_ }
                                      .resolve_vhdl(
                                          reference, type_scope, type_declaration);
            if (visible.status
                    == semantic::CompiledResolutionStatus::not_found
                && visible.candidates.empty()) {
                return true;
            }
            // A standard CHARACTER declaration can be present in the linked
            // design. Accept that exact owner while keeping a local or
            // imported user-defined `character` distinct.
            const auto visible_id = visible.unique();
            const auto declaration = visible_id
                ? specialization_.find_declaration(*visible_id)
                : std::nullopt;
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->scope.valid()
                || declaration->vhdl->scope.value()
                    >= specialization_.design().semantics.scopes().size()) {
                return false;
            }
            return specialization_.design().vhdl_scope_in_package(
                       declaration->vhdl->scope, "std", "standard")
                && equal_ignoring_ascii_case(
                    declaration->vhdl->name, "character");
        }

        return string_enumeration_membership(text, *element, type_scope)
            .value_or(false);
    }

    std::optional<bool> contextual_string_enum_membership_for(
        const semantic::ExpressionId value,
        const SubtypeIndication& expected_element,
        const semantic::ScopeId operand_scope,
        const semantic::ScopeId expected_declaration_scope = {}) const
    {
        const auto expression = specialization_.find_expression(value);
        if (!expression || expression->vhdl == nullptr
            || expression->vhdl->kind
                != semantic::vhdl::ExpressionKind::string_literal
            || !expression->vhdl->decoded_string
            || !expected_element.constraints.empty()) {
            return std::nullopt;
        }
        if (profile(expected_element).kind
            != ProfileKind::enumeration) {
            return std::nullopt;
        }

        const auto type_scope = expected_declaration_scope.valid()
            ? expected_declaration_scope
            : operand_scope;
        return string_enumeration_membership(
            *expression->vhdl->decoded_string, expected_element, type_scope);
    }

    std::optional<SubtypeIndication> predefined_array_element_subtype(
        const SubtypeIndication& array_subtype,
        const semantic::ScopeId scope) const
    {
        const auto identity = predefined_array_identity(
            array_subtype, scope);
        if (!identity) {
            return std::nullopt;
        }
        std::string_view element;
        if (*identity == "std.standard.string") {
            element = "std.standard.character";
        } else if (*identity == "std.standard.bit_vector") {
            element = "std.standard.bit";
        } else if (*identity == "std.standard.boolean_vector") {
            element = "std.standard.boolean";
        } else if (*identity == "std.standard.integer_vector") {
            element = "std.standard.integer";
        } else if (*identity == "std.standard.real_vector") {
            element = "std.standard.real";
        } else if (*identity == "std.standard.time_vector") {
            element = "std.standard.time";
        } else if (*identity
            == "ieee.std_logic_1164.std_logic_vector") {
            element = "ieee.std_logic_1164.std_logic";
        } else if (*identity
            == "ieee.std_logic_1164.std_ulogic_vector") {
            element = "ieee.std_logic_1164.std_ulogic";
        } else if (*identity == "ieee.numeric_std.signed"
            || *identity == "ieee.numeric_std.unsigned") {
            element = "ieee.std_logic_1164.std_ulogic";
        } else {
            return std::nullopt;
        }
        SubtypeIndication result;
        result.type_mark.spelling = std::string { element };
        return result;
    }

    bool same_nominal_array_type(
        const SubtypeIndication& expected,
        const SubtypeIndication& actual,
        const semantic::ScopeId scope) const
    {
        // Only resolved type marks carry a safe declaration identity here.
        // `subtype_type()` has a useful global fallback for validation, but
        // that fallback can guess across scopes and is unsuitable for deciding
        // whether two expressions have the same nominal type.
        const auto expected_root = expected.type_mark.target.valid()
            ? array_root(expected.type_mark.target)
            : std::nullopt;
        const auto actual_root = actual.type_mark.target.valid()
            ? array_root(actual.type_mark.target)
            : std::nullopt;
        const auto expected_predefined
            = predefined_array_identity(expected, scope);
        const auto actual_predefined
            = predefined_array_identity(actual, scope);
        if (expected_root && actual_root
            && *expected_root == *actual_root) {
            const auto* definition = type_definition(*expected_root);
            return definition != nullptr
                && definition->form == TypeForm::array
                && definition->array_dimensions.size() == 1U;
        }
        if (expected_predefined && actual_predefined) {
            return *expected_predefined == *actual_predefined;
        }
        return false;
    }

    // Whether an element association is `others => ...`. OTHERS is a
    // reserved word, so a choice spelled or named `others` can be nothing
    // else.
    bool choice_is_others(
        const semantic::vhdl::AggregateAssociation& association) const
    {
        if (equal_ignoring_ascii_case(simple_name_of(association.choice_spelling),
                "others")) {
            return true;
        }
        return std::ranges::any_of(association.choices,
            [&](const semantic::ExpressionId id) {
                const auto choice = specialization_.find_expression(id);
                return choice && choice->vhdl != nullptr
                    && (choice->vhdl->kind
                            == semantic::vhdl::ExpressionKind::default_choice
                        || equal_ignoring_ascii_case(
                            choice->vhdl->text, "others"));
            });
    }

    std::optional<std::size_t> literal_width(
        const semantic::ExpressionId id) const
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto& value = *expression->vhdl;
        if (value.kind == semantic::vhdl::ExpressionKind::logic_literal) {
            // A character literal takes its type, and so its width, from the
            // context: one bit for BIT, eight for CHARACTER (IEEE 1076-2008
            // 9.3.2).
            return value.text.starts_with('\'')
                ? std::nullopt
                : std::optional<std::size_t> { 1U };
        }
        if (value.kind == semantic::vhdl::ExpressionKind::string_literal) {
            if (value.decoded_string) {
                return value.decoded_string->size();
            }
            return value.text.size() >= 2U
                ? std::optional { value.text.size() - 2U }
                : std::nullopt;
        }
        if (const auto subtype = expression_subtype(id)) {
            if (subtype->executable_width) {
                return static_cast<std::size_t>(
                    *subtype->executable_width);
            }
            if (subtype->domain == semantic::vhdl::ValueDomain::bit2
                || subtype->domain
                    == semantic::vhdl::ValueDomain::logic4
                || subtype->domain
                    == semantic::vhdl::ValueDomain::logic9
                || subtype->domain
                    == semantic::vhdl::ValueDomain::boolean) {
                return 1U;
            }
        }
        return std::nullopt;
    }

    void validate_aggregate_element(
        const SubtypeIndication& expected,
        const semantic::ExpressionId actual_id,
        const semantic::SourceSpanId source,
        const semantic::ScopeId expected_declaration_scope = {})
    {
        if (!aggregate_element_compatible(
                expected, actual_id, expected_declaration_scope)) {
            report(
                "FSIM-ELAB-VHARRAYAGG-009",
                "VHDL array aggregate element requires its exact "
                    "contextual subtype",
                source);
            return;
        }
        const auto actual = specialization_.find_expression(actual_id);
        if (actual && actual->vhdl != nullptr
            && actual->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal) {
            return;
        }
        const auto actual_subtype = expression_subtype(actual_id);
        if (actual && actual->vhdl != nullptr
            && actual->vhdl->kind
                != semantic::vhdl::ExpressionKind::string_literal) {
            const auto expected_operand = subtype_operand_type(expected, 0U);
            const auto actual_operand = operand_type(actual_id);
            if (scalar_class(expected_operand.kind)
                && scalar_class(actual_operand.kind)
                && types_conflict(actual_operand, expected_operand)) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-014",
                    "VHDL aggregate element does not have the element type",
                    source);
                return;
            }
        }
        const auto context_compatible_bit_literal = actual
            && actual->vhdl != nullptr
            && actual->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal
            && actual->vhdl->text.size() == 3U
            && (actual->vhdl->text[1] == '0'
                || actual->vhdl->text[1] == '1');
        const bool context_compatible_string_literal
            = actual && actual->vhdl != nullptr
            && actual->vhdl->kind
                == semantic::vhdl::ExpressionKind::string_literal
            && contextual_string_array_value_for(
                actual_id, expected, actual->vhdl->scope,
                expected_declaration_scope);
        if (expected.domain == semantic::vhdl::ValueDomain::bit2
            && actual_subtype
            && actual_subtype->domain
                != semantic::vhdl::ValueDomain::unknown
            && actual_subtype->domain
                != semantic::vhdl::ValueDomain::bit2
            && !context_compatible_bit_literal
            && !context_compatible_string_literal) {
            report(
                "FSIM-ELAB-VHARRAYAGG-007",
                "two-state VHDL aggregate element requires an "
                    "explicit conversion",
                source);
        }
        if (actual && actual->vhdl != nullptr
            && actual->vhdl->kind
                == semantic::vhdl::ExpressionKind::string_literal) {
            const auto expected_profile = profile(
                expected);
            if (expected_profile.kind == ProfileKind::array
                && expected_profile.dimensions.size() == 1U) {
                const auto& dimension
                    = expected_profile.dimensions.front();
                const auto actual_characters = literal_width(actual_id);
                if (dimension.constrained && dimension.null_known
                    && dimension.null
                    && actual_characters && *actual_characters != 0U) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-006",
                        "VHDL array aggregate element width does not "
                            "match its context",
                        source);
                } else if (dimension.constrained && dimension.null_known
                    && !dimension.null && dimension.bounds_known
                    && dimension.minimum && dimension.maximum
                    && actual_characters) {
                    const auto extent = inclusive_extent(
                        *dimension.minimum, *dimension.maximum);
                    if (extent && *extent != *actual_characters) {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-006",
                            "VHDL array aggregate element width does not "
                                "match its context",
                            source);
                    }
                }
                return;
            }
        }
        const auto expected_width = expected.executable_width
            ? std::optional<std::size_t> {
                  static_cast<std::size_t>(*expected.executable_width) }
            : expected.domain == semantic::vhdl::ValueDomain::bit2
                    || expected.domain
                        == semantic::vhdl::ValueDomain::logic4
                    || expected.domain
                        == semantic::vhdl::ValueDomain::logic9
                    || expected.domain
                        == semantic::vhdl::ValueDomain::boolean
            ? std::optional<std::size_t> { 1U }
            : std::nullopt;
        const auto actual_width = literal_width(actual_id);
        // A STRING element holds 8 bits per CHARACTER; a literal or
        // concatenation of literals is counted in characters.
        const bool string_characters
            = expected.domain == semantic::vhdl::ValueDomain::string
            && expected_width && actual_width
            && *expected_width == 8U * *actual_width;
        if (expected_width && actual_width && !string_characters
            && *expected_width != *actual_width) {
            report(
                "FSIM-ELAB-VHARRAYAGG-006",
                "VHDL aggregate element width does not match its "
                    "context",
                source);
        }
    }

    void validate_record_aggregate(
        const semantic::vhdl::Expression& aggregate,
        const TypeDefinition& record)
    {
        for (const auto& association : aggregate.associations) {
            if (!association.choice_spelling.empty()
                && association.choices.empty()) {
                report(
                    "FSIM-ELAB-VHAGG-002",
                    "record aggregate association is missing its "
                        "compiled choice metadata",
                    association.source);
                continue;
            }
            if (association.positional()) {
                continue;
            }
            if (choice_is_others(association)) {
                continue;
            }
            for (const auto choice_id : association.choices) {
                const auto choice = specialization_.find_expression(
                    choice_id);
                const auto named = choice && choice->vhdl != nullptr
                    && choice->vhdl->kind
                        == semantic::vhdl::ExpressionKind::name
                    && std::ranges::any_of(
                        record.record_elements,
                        [&](const auto& element) {
                            return equal_ignoring_ascii_case(
                                element.name, choice->vhdl->text);
                        });
                if (!named) {
                    report(
                        "FSIM-ELAB-VHAGG-008",
                        "record aggregate choices must be element names",
                        association.source);
                }
            }
        }
    }

    void validate_array_aggregate_element_types(
        const semantic::vhdl::Expression& aggregate,
        const TypeProfile& context,
        const SubtypeIndication& array_subtype,
        const SubtypeIndication& element,
        const semantic::ScopeId expected_declaration_scope,
        const std::size_t depth = 0U)
    {
        if (depth > 8U) {
            report(
                "FSIM-ELAB-VHARRAYAGG-009",
                "VHDL array aggregate element requires its exact "
                    "contextual subtype",
                aggregate.source);
            return;
        }
        if (context.dimensions.empty()) {
            report(
                "FSIM-ELAB-VHARRAYAGG-009",
                "VHDL array aggregate element requires its exact "
                    "contextual subtype",
                aggregate.source);
            return;
        }
        const auto element_profile = profile(element);
        constexpr std::string_view qualified_prefix {
            semantic::vhdl::qualified_expression_prefix };
        for (const auto& association : aggregate.associations) {
            const auto value = specialization_.find_expression(
                association.value);
            if (!value || value->vhdl == nullptr) {
                continue;
            }
            const auto& expression = *value->vhdl;
            if (depth == 0U && !context.dimensions.empty()
                && context.dimensions.front().null_known
                && context.dimensions.front().null) {
                const auto actual_subtype = expression_subtype(
                    association.value);
                const auto scope = expression.scope;
                if (actual_subtype
                    && same_nominal_array_type(
                        array_subtype, *actual_subtype, scope)) {
                    // This prepass sees the null outer context before
                    // individual choices are dispatched. Leave a same-type
                    // array value to that assignment path instead of
                    // treating it as one scalar element; null choices get
                    // segment-width validation there.
                    continue;
                }
            }
            if (context.dimensions.size() > 1U) {
                auto nested = context;
                nested.dimensions.erase(nested.dimensions.begin());
                if (expression.kind
                    == semantic::vhdl::ExpressionKind::aggregate) {
                    validate_array_aggregate_element_types(
                        expression, nested, array_subtype, element,
                        expected_declaration_scope, depth + 1U);
                    continue;
                }
                if (expression.kind
                        == semantic::vhdl::ExpressionKind::string_literal) {
                    continue;
                }
                if (expression.kind
                        == semantic::vhdl::ExpressionKind::call
                    && expression.text.starts_with(qualified_prefix)
                    && expression.operands.size() == 1U) {
                    const auto operand = specialization_.find_expression(
                        expression.operands.front());
                    const auto actual_subtype = expression_subtype(
                        association.value);
                    const auto actual_profile = actual_subtype
                        ? profile(*actual_subtype)
                        : TypeProfile { };
                    const auto expected_type = subtype_type(array_subtype);
                    const auto actual_type = actual_subtype
                        ? subtype_type(*actual_subtype)
                        : std::nullopt;
                    const auto expected_root = expected_type
                        ? array_root(*expected_type)
                        : std::nullopt;
                    const auto actual_root = actual_type
                        ? array_root(*actual_type)
                        : std::nullopt;
                    // A missing nominal root is not evidence that a
                    // qualified row has the expected array type.
                    const bool same_root = expected_root && actual_root
                        && *expected_root == *actual_root;
                    if (operand && operand->vhdl != nullptr
                        && operand->vhdl->kind
                            == semantic::vhdl::ExpressionKind::aggregate
                        && actual_profile.kind == ProfileKind::array
                        && actual_profile.dimensions.size()
                            == nested.dimensions.size()
                        && same_root) {
                        validate_array_aggregate_element_types(
                            *operand->vhdl, nested, array_subtype, element,
                            expected_declaration_scope, depth + 1U);
                    } else {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-009",
                            "VHDL array aggregate element requires its exact "
                                "contextual subtype",
                            association.source);
                    }
                    continue;
                }
                report(
                    "FSIM-ELAB-VHAGG-015",
                    "an element association of a multidimensional VHDL "
                        "array aggregate is an aggregate of one dimension "
                        "less",
                    association.source);
                continue;
            }

            if (expression.kind
                    == semantic::vhdl::ExpressionKind::call
                && expression.text.starts_with(qualified_prefix)
                && expression.operands.size() == 1U
                && (element_profile.kind == ProfileKind::array
                    || element_profile.kind == ProfileKind::record)) {
                const auto operand = specialization_.find_expression(
                    expression.operands.front());
                if (operand && operand->vhdl != nullptr
                    && operand->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate) {
                    if (!aggregate_element_compatible(
                            element, association.value,
                            expected_declaration_scope)) {
                        validate_aggregate_element(
                            element, association.value, association.source,
                            expected_declaration_scope);
                        continue;
                    }
                    if (element_profile.kind == ProfileKind::array) {
                        auto nested_element
                            = array_element_subtype(element);
                        if (!nested_element) {
                            nested_element = predefined_array_element_subtype(
                                element, expected_declaration_scope);
                        }
                        if (nested_element) {
                            validate_array_aggregate_element_types(
                                *operand->vhdl, element_profile, element,
                                *nested_element, expected_declaration_scope,
                                depth + 1U);
                        }
                    } else {
                        validate_aggregate(*operand->vhdl, element);
                    }
                    continue;
                }
            }
            if (expression.kind
                    == semantic::vhdl::ExpressionKind::aggregate
                && (element_profile.kind == ProfileKind::array
                    || element_profile.kind == ProfileKind::record)) {
                if (element_profile.kind == ProfileKind::array) {
                    auto nested_element
                        = array_element_subtype(element);
                    if (!nested_element) {
                        nested_element = predefined_array_element_subtype(
                            element, expected_declaration_scope);
                    }
                    if (nested_element) {
                        validate_array_aggregate_element_types(
                            expression, element_profile, element,
                            *nested_element, expected_declaration_scope,
                            depth + 1U);
                    }
                } else {
                    validate_aggregate(expression, element);
                }
                continue;
            }
            validate_aggregate_element(
                element, association.value, association.source,
                expected_declaration_scope);
        }
    }

    void validate_array_aggregate(
        const semantic::vhdl::Expression& aggregate,
        TypeProfile context,
        const SubtypeIndication& array_subtype,
        const SubtypeIndication& element,
        const std::optional<semantic::TypeId> array_identity,
        const std::size_t array_dimension_index = 0U)
    {
        if (context.dimensions.empty()) {
            return;
        }
        const auto* array_definition
            = array_subtype.type_mark.target.valid()
            ? root_definition(array_subtype)
            : nullptr;
        // Preserve the declaration scope of element subtype marks only when
        // the expected array has a retained type identity; the aggregate
        // expression may live in a different design unit.
        const auto expected_declaration_scope
            = array_definition != nullptr
            ? type_declaration_scope(*array_definition)
            : semantic::ScopeId { };
        auto dimension = context.dimensions.front();
        if (!dimension.direction_known
            || (dimension.constrained
                && (!dimension.bounds_known || !dimension.null_known))) {
            validate_array_aggregate_element_types(
                aggregate, context, array_subtype, element,
                expected_declaration_scope);
            return;
        }
        // The direction is context-determined only if the original subtype
        // constrained it. Positional self-bounding below must not turn an
        // unconstrained profile into a fixed-direction context.
        const bool context_direction_determined
            = dimension.constrained && dimension.direction_known;
        const bool context_descending = dimension.descending;
        const auto positional_count = [&]() -> std::optional<std::uint64_t> {
            if (!semantic::vhdl::positional_aggregate(aggregate)) {
                return std::nullopt;
            }
            std::uint64_t count { };
            for (const auto& association : aggregate.associations) {
                const auto value
                    = specialization_.find_expression(association.value);
                const auto scope = value && value->vhdl != nullptr
                    ? value->vhdl->scope
                    : aggregate.scope;
                const auto segment_count = positional_array_segment_count(
                    association.value, array_subtype, element, scope);
                const auto increment = segment_count.value_or(1U);
                if (increment
                    > std::numeric_limits<std::uint64_t>::max() - count) {
                    return std::nullopt;
                }
                count += increment;
            }
            return count;
        }();
        if (!dimension.constrained) {
            if (!positional_count || !dimension.minimum
                || *positional_count == 0U) {
                validate_array_aggregate_element_types(
                    aggregate, context, array_subtype, element,
                    expected_declaration_scope);
                return;
            }
            const auto last_offset = *positional_count - 1U;
            if (last_offset > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate positional bounds exceed the "
                        "index subtype",
                    aggregate.associations.back().source);
                return;
            }
            const auto signed_offset = static_cast<std::int64_t>(last_offset);
            const auto first = dimension.descending
                ? dimension.maximum
                : dimension.minimum;
            const bool outside_index_subtype
                = !first || !dimension.maximum
                || (dimension.descending
                    ? static_cast<std::uint64_t>(*first)
                            - static_cast<std::uint64_t>(
                                *dimension.minimum)
                        < last_offset
                    : static_cast<std::uint64_t>(*dimension.maximum)
                            - static_cast<std::uint64_t>(*first)
                        < last_offset);
            if (outside_index_subtype) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate positional bounds exceed the "
                        "index subtype",
                    aggregate.associations.back().source);
                return;
            }
            const auto last = dimension.descending
                ? *first - signed_offset
                : *first + signed_offset;
            dimension.minimum = std::min(*first, last);
            dimension.maximum = std::max(*first, last);
            dimension.constrained = true;
            context.dimensions.front() = dimension;
        }
        struct AggregateChoiceRange {
            std::int64_t left { };
            std::int64_t right { };
            bool descending { };
            bool null { };
        };
        const auto range_choice = [&](const semantic::ExpressionId id)
            -> std::optional<AggregateChoiceRange> {
            const auto choice = specialization_.find_expression(id);
            if (!choice || choice->vhdl == nullptr
                || choice->vhdl->kind
                    != semantic::vhdl::ExpressionKind::call
                || (choice->vhdl->text != "'range"
                    && choice->vhdl->text != "'reverse_range")
                || choice->vhdl->operands.empty()
                || choice->vhdl->operands.size() > 2U) {
                return std::nullopt;
            }
            auto prefix = expression_subtype(
                choice->vhdl->operands.front());
            if (!prefix) {
                return std::nullopt;
            }
            const auto prefix_profile = profile(*prefix);
            if (prefix_profile.kind != ProfileKind::array) {
                return std::nullopt;
            }
            std::int64_t dimension { 1 };
            if (choice->vhdl->operands.size() == 2U) {
                std::unordered_set<std::uint32_t> active;
                const auto selected = static_integer(
                    choice->vhdl->operands[1], active);
                if (!selected) {
                    return std::nullopt;
                }
                dimension = *selected;
            }
            if (dimension <= 0
                || static_cast<std::uint64_t>(dimension)
                    > prefix_profile.dimensions.size()) {
                return std::nullopt;
            }
            const auto selected_index
                = static_cast<std::size_t>(dimension - 1);
            std::optional<RangeConstraint> selected_range;
            std::size_t constrained_dimension { };
            for (const auto& constraint : prefix->constraints) {
                if (constraint.kind != RangeKind::array_index) {
                    continue;
                }
                if (constrained_dimension++ == selected_index) {
                    selected_range = constraint;
                    break;
                }
            }
            if (!selected_range) {
                const auto* definition = root_definition(*prefix);
                if (definition != nullptr
                    && definition->form == TypeForm::array
                    && selected_index
                        < definition->array_dimensions.size()) {
                    const auto& array_dimension
                        = definition->array_dimensions[selected_index];
                    selected_range = array_dimension.constraint;
                }
            }
            if (!selected_range) {
                return std::nullopt;
            }
            const auto left = bound(
                selected_range->left,
                selected_range->left_expression);
            const auto right = bound(
                selected_range->right,
                selected_range->right_expression);
            if (!left || !right) {
                return std::nullopt;
            }
            auto selected_left = *left;
            auto selected_right = *right;
            auto descending = selected_range->descending;
            const auto null = selected_range->null
                || (descending ? *left < *right : *left > *right);
            if (choice->vhdl->text == "'reverse_range") {
                std::swap(selected_left, selected_right);
                descending = !descending;
            }
            return AggregateChoiceRange {
                selected_left, selected_right, descending, null };
        };
        // A named aggregate without `others` takes its bounds from its
        // smallest and largest choices; only its direction comes from the
        // context (IEEE 1076-2008 9.3.3.3).  It then converts to the
        // contextual subtype by length (9.3.3.3, 14.7.3.4 implicit subtype
        // conversion), so its choices are checked against its own bounds.
        const auto named_bounds = [&]()
            -> std::optional<std::pair<std::int64_t, std::int64_t>> {
            std::optional<std::int64_t> low;
            std::optional<std::int64_t> high;
            const auto include = [&](const std::int64_t index) {
                low = low ? std::min(*low, index) : index;
                high = high ? std::max(*high, index) : index;
            };
            const auto include_choice = [&](const auto& self,
                                            const semantic::ExpressionId id)
                -> bool {
                const auto choice = specialization_.find_expression(id);
                if (!choice || choice->vhdl == nullptr) {
                    return false;
                }
                const auto& value = *choice->vhdl;
                if (value.kind == semantic::vhdl::ExpressionKind::binary
                    && value.text == "|" && value.operands.size() == 2U) {
                    return self(self, value.operands[0])
                        && self(self, value.operands[1]);
                }
                if (value.kind == semantic::vhdl::ExpressionKind::binary
                    && (value.text == "to" || value.text == "downto")
                    && value.operands.size() == 2U) {
                    const auto left = array_aggregate_index_value(
                        array_definition, array_dimension_index,
                        value.operands[0]);
                    const auto right = array_aggregate_index_value(
                        array_definition, array_dimension_index,
                        value.operands[1]);
                    if (!left || !right
                        || (value.text == "to" ? *left > *right
                                               : *left < *right)) {
                        return false;
                    }
                    include(*left);
                    include(*right);
                    return true;
                }
                if (value.kind == semantic::vhdl::ExpressionKind::call
                    && (value.text == "'range"
                        || value.text == "'reverse_range")) {
                    const auto bounds = range_choice(id);
                    if (!bounds || bounds->null) {
                        return false;
                    }
                    include(bounds->left);
                    include(bounds->right);
                    return true;
                }
                const auto index = array_aggregate_index_value(
                    array_definition, array_dimension_index, id);
                if (!index) {
                    return false;
                }
                include(*index);
                return true;
            };
            for (const auto& association : aggregate.associations) {
                if (association.positional() || association.choices.empty()
                    || choice_is_others(association)
                    || (!association.choice_spelling.empty()
                        && association.choice_spelling != "@array")) {
                    return std::nullopt;
                }
                for (const auto choice : association.choices) {
                    if (!include_choice(include_choice, choice)) {
                        return std::nullopt;
                    }
                }
            }
            if (!low || !high) {
                return std::nullopt;
            }
            return std::pair { *low, *high };
        };
        if (dimension.constrained && !dimension.null && dimension.minimum
            && dimension.maximum) {
            if (const auto natural = named_bounds()) {
                const auto contextual = inclusive_extent(
                    *dimension.minimum, *dimension.maximum);
                const auto own = inclusive_extent(
                    natural->first, natural->second);
                if (contextual && own && *contextual != *own) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-006",
                        "VHDL named array aggregate length does not match "
                            "its context",
                        aggregate.source);
                }
                dimension.minimum = natural->first;
                dimension.maximum = natural->second;
                context.dimensions.front() = dimension;
            }
        }
        if (!dimension.minimum || !dimension.maximum) {
            validate_array_aggregate_element_types(
                aggregate, context, array_subtype, element,
                expected_declaration_scope);
            return;
        }
        const auto minimum = *dimension.minimum;
        const auto maximum = *dimension.maximum;
        const auto distance = static_cast<std::uint64_t>(maximum)
            - static_cast<std::uint64_t>(minimum);
        if (distance == std::numeric_limits<std::uint64_t>::max()) {
            validate_array_aggregate_element_types(
                aggregate, context, array_subtype, element,
                expected_declaration_scope);
            return;
        }
        const auto element_count = dimension.null_known && dimension.null
            ? std::uint64_t { 0U }
            : distance + 1U;
        if (dimension.null) {
            validate_array_aggregate_element_types(
                aggregate, context, array_subtype, element,
                expected_declaration_scope);
        }
        std::unordered_set<std::int64_t> assigned;
        std::vector<std::pair<std::int64_t, std::int64_t>> assigned_segments;
        std::uint64_t assigned_segment_count { };
        bool has_others = false;
        bool dynamic_sole_choice = false;
        bool positional_exhausted = dimension.null;
        auto positional = dimension.descending ? maximum : minimum;
        const auto covered_by_segment = [&](const std::int64_t index) {
            return std::ranges::any_of(
                assigned_segments, [&](const auto& segment) {
                    return index >= segment.first && index <= segment.second;
                });
        };

        const auto validate_value = [&](const semantic::ExpressionId value,
                                        const semantic::SourceSpanId source)
            -> void {
            const auto expression = specialization_.find_expression(value);
            constexpr std::string_view qualified_prefix {
                semantic::vhdl::qualified_expression_prefix };
            if (expression && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::call
                && expression->vhdl->text.starts_with(qualified_prefix)
                && expression->vhdl->operands.size() == 1U) {
                const auto element_profile = profile(
                    element);
                const auto operand = specialization_.find_expression(
                    expression->vhdl->operands.front());
                if ((element_profile.kind == ProfileKind::array
                        || element_profile.kind == ProfileKind::record)
                    && operand && operand->vhdl != nullptr
                    && operand->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate) {
                    if (aggregate_element_compatible(
                            element, value, expected_declaration_scope)) {
                        validate_aggregate(value, element);
                    } else {
                        validate_aggregate_element(
                            element, value, source,
                            expected_declaration_scope);
                    }
                    return;
                }
            }
            if (context.dimensions.size() > 1U && expression
                && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate) {
                auto nested = context;
                nested.dimensions.erase(nested.dimensions.begin());
                validate_array_aggregate(
                    *expression->vhdl, std::move(nested), array_subtype,
                    element, array_identity, array_dimension_index + 1U);
                return;
            }
            if (expression && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate) {
                const auto element_profile = profile(
                    element);
                if (element_profile.kind == ProfileKind::array
                    || element_profile.kind == ProfileKind::record) {
                    validate_aggregate(value, element);
                    return;
                }
            }
            if (context.dimensions.size() > 1U && expression
                && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal) {
                // Enum membership does not depend on the row bounds. Check
                // it before an unconstrained dimension exits the width path.
                const auto string_enum_membership
                    = contextual_string_enum_membership_for(
                        value, element, expression->vhdl->scope,
                        expected_declaration_scope);
                if (string_enum_membership
                    && !*string_enum_membership) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-014",
                        "VHDL aggregate element does not have the element type",
                        source);
                    return;
                }
                std::uint64_t expected_elements { 1U };
                for (auto nested_dimension
                         = context.dimensions.begin() + 1;
                    nested_dimension != context.dimensions.end();
                    ++nested_dimension) {
                    // An unconstrained index subtype range does not fix a
                    // row's length. Only a constrained dimension supplies
                    // an exact width for this contextual string check.
                    if (!nested_dimension->constrained
                        || !nested_dimension->minimum
                        || !nested_dimension->maximum) {
                        return;
                    }
                    const auto extent = inclusive_extent(
                        *nested_dimension->minimum,
                        *nested_dimension->maximum);
                    if (!extent) {
                        return;
                    }
                    const auto count = *extent;
                    if (count != 0U
                        && expected_elements
                            > std::numeric_limits<std::uint64_t>::max()
                                / count) {
                        return;
                    }
                    expected_elements *= count;
                }
                const auto actual_width = literal_width(value);
                if (actual_width
                    && expected_elements != *actual_width) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-006",
                        "VHDL aggregate element width does not match its "
                            "context",
                        source);
                }
                return;
            }
            validate_aggregate_element(
                element, value, source, expected_declaration_scope);
        };
        const auto assign = [&](const std::int64_t index,
                                const semantic::ExpressionId value,
                                const semantic::SourceSpanId source) {
            if (dimension.null || index < minimum || index > maximum) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate index is outside the "
                        "contextual range",
                    source);
                return;
            }
            if (covered_by_segment(index)) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-004",
                    "VHDL array aggregate index overlaps a positional "
                        "segment",
                    source);
                return;
            }
            if (!assigned.insert(index).second) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-004",
                    "VHDL array aggregate index is assigned more than "
                        "once",
                    source);
                return;
            }
            validate_value(value, source);
        };
        const auto assign_positional_segment = [&](
                                                      const std::uint64_t count,
                                                      const semantic::SourceSpanId source) {
            if (count == 0U || positional_exhausted) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate segment is outside the "
                        "contextual range",
                    source);
                return;
            }
            const auto offset = count - 1U;
            if (offset > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate segment exceeds the "
                        "contextual range",
                    source);
                return;
            }
            const bool index_overflow = dimension.descending
                ? static_cast<std::uint64_t>(positional)
                        - static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::min())
                    < offset
                : static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max())
                        - static_cast<std::uint64_t>(positional)
                    < offset;
            if (index_overflow) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate segment exceeds the "
                        "contextual range",
                    source);
                return;
            }
            const auto signed_offset = static_cast<std::int64_t>(offset);
            const auto last = dimension.descending
                ? positional - signed_offset
                : positional + signed_offset;
            if (positional < minimum || positional > maximum
                || last < minimum || last > maximum) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate segment is outside the "
                        "contextual range",
                    source);
                return;
            }
            const auto low = std::min(positional, last);
            const auto high = std::max(positional, last);
            const bool overlaps_segment = std::ranges::any_of(
                assigned_segments, [&](const auto& segment) {
                    return low <= segment.second && high >= segment.first;
                });
            const bool overlaps_index = std::ranges::any_of(
                assigned, [&](const auto index) {
                    return index >= low && index <= high;
                });
            if (overlaps_segment || overlaps_index
                || count > std::numeric_limits<std::uint64_t>::max()
                    - assigned_segment_count) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-004",
                    "VHDL array aggregate has overlapping positional "
                        "segments",
                    source);
                return;
            }
            assigned_segments.emplace_back(low, high);
            assigned_segment_count += count;
            if ((dimension.descending
                    && last
                        == std::numeric_limits<std::int64_t>::min())
                || (!dimension.descending
                    && last
                        == std::numeric_limits<std::int64_t>::max())) {
                positional_exhausted = true;
            } else {
                positional = last + (dimension.descending ? -1 : 1);
            }
        };

        const auto contextual_string_array_value =
            [&](const semantic::ExpressionId value,
                const semantic::ScopeId scope) {
                return contextual_string_array_value_for(
                    value, array_subtype, scope,
                    expected_declaration_scope);
            };
        const auto same_nominal_array_value =
            [&](const semantic::ExpressionId value) {
                const auto expression = specialization_.find_expression(value);
                const auto scope = expression && expression->vhdl != nullptr
                    ? expression->vhdl->scope
                    : aggregate.scope;
                if (expression && expression->vhdl != nullptr
                    && expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::string_literal) {
                    // Untyped string literals acquire the array context here;
                    // their parser-provided STRING placeholder is not a
                    // nominal type identity.
                    return contextual_string_array_value(value, scope);
                }
                const auto subtype = expression_subtype(value);
                return subtype && same_nominal_array_type(
                    array_subtype, *subtype, scope);
            };
        const auto validate_null_range_value = [&](
            const semantic::ExpressionId value,
            const semantic::SourceSpanId source) {
            const bool same_nominal = same_nominal_array_value(value);
            if (same_nominal) {
                const auto expression
                    = specialization_.find_expression(value);
                const auto scope = expression && expression->vhdl != nullptr
                    ? expression->vhdl->scope
                    : aggregate.scope;
                const auto segment_count = positional_array_segment_count(
                    value, array_subtype, element, scope);
                if (!segment_count || *segment_count != 0U) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-006",
                        "VHDL array aggregate element width does not match "
                            "its context",
                        source);
                }
                return;
            }
            const auto expression
                = specialization_.find_expression(value);
            std::optional<SubtypeIndication> actual_subtype;
            if (expression && expression->vhdl != nullptr
                && expression->vhdl->kind
                    != semantic::vhdl::ExpressionKind::string_literal) {
                actual_subtype = expression_subtype(value);
            }
            if (actual_subtype) {
                const auto actual_profile = profile(*actual_subtype);
                const auto element_profile = profile(element);
                const auto scope = expression->vhdl->scope;
                if (actual_profile.kind == ProfileKind::array
                    && (element_profile.kind != ProfileKind::array
                        || !same_nominal_array_type(
                            element, *actual_subtype, scope))) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-009",
                        "VHDL array aggregate element requires its exact "
                            "contextual subtype",
                        source);
                    return;
                }
            }
            validate_value(value, source);
        };
        const auto null_range_has_single_association =
            [&](const auto& association) {
                bool has_choice_union { };
                if (association.choices.size() == 1U) {
                    const auto choice = specialization_.find_expression(
                        association.choices.front());
                    has_choice_union = choice && choice->vhdl != nullptr
                        && choice->vhdl->kind
                            == semantic::vhdl::ExpressionKind::binary
                        && choice->vhdl->text == "|";
                }
                if (aggregate.associations.size() == 1U
                    && association.choices.size() == 1U
                    && !has_choice_union) {
                    return true;
                }
                report(
                    "FSIM-ELAB-VHARRAYAGG-011",
                    "a VHDL array aggregate choice that is a null range "
                        "must be the sole association and have one choice "
                        "(IEEE 1076-2008 9.3.3.3)",
                    association.source);
                return false;
            };

        // Whether a range choice supplies a slice is distinct from whether
        // its value has the aggregate's nominal array type.
        const auto slice_value = [&](const semantic::ExpressionId value,
                                     const std::int64_t left,
                                     const std::int64_t right) {
            const auto extent = inclusive_extent(
                std::min(left, right), std::max(left, right));
            if (!extent) {
                return false;
            }
            const auto count = *extent;
            if (!same_nominal_array_value(value)) {
                return false;
            }
            const auto expression = specialization_.find_expression(value);
            if (!expression || expression->vhdl == nullptr) {
                return false;
            }
            const auto segment_count = positional_array_segment_count(
                value, array_subtype, element, expression->vhdl->scope);
            return segment_count && *segment_count == count;
        };
        const auto validate_array_range_direction =
            [&](const semantic::ExpressionId value,
                const bool choice_descending,
                const semantic::SourceSpanId source) {
                if (!context_direction_determined
                    || choice_descending == context_descending
                    || !same_nominal_array_value(value)) {
                    return true;
                }
                // IEEE 1076-2008/2019 9.3.3.3: an array-valued range
                // association of the aggregate's nominal array type follows
                // the direction fixed by the surrounding subtype.
                report(
                    "FSIM-ELAB-VHARRAYAGG-010",
                    "VHDL array-valued range choice direction differs from "
                        "the context-determined aggregate direction",
                    source);
                return false;
            };
        const auto validate_untyped_nested_segment =
            [&](const auto& self, const semantic::ExpressionId value_id)
                -> void {
            const auto expression
                = specialization_.find_expression(value_id);
            if (!expression || expression->vhdl == nullptr
                || expression->vhdl->kind
                    != semantic::vhdl::ExpressionKind::aggregate) {
                return;
            }
            for (const auto& association : expression->vhdl->associations) {
                const auto child
                    = specialization_.find_expression(association.value);
                const auto scope = child && child->vhdl != nullptr
                    ? child->vhdl->scope
                    : expression->vhdl->scope;
                if (child && child->vhdl != nullptr
                    && child->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate
                    && !expression_subtype(association.value)) {
                    self(self, association.value);
                    continue;
                }
                if (positional_array_segment_count(
                        association.value, array_subtype, element, scope)) {
                    continue;
                }
                validate_aggregate_element(
                    element, association.value, association.source,
                    expected_declaration_scope);
            }
        };

        for (const auto& association : aggregate.associations) {
            if (!association.choice_spelling.empty()
                && association.choices.empty()) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-002",
                    "VHDL array aggregate association is missing its "
                        "compiled choice metadata",
                    association.source);
                continue;
            }
            if (association.positional()) {
                const auto value
                    = specialization_.find_expression(association.value);
                const auto scope = value && value->vhdl != nullptr
                    ? value->vhdl->scope
                    : aggregate.scope;
                const auto segment_count = positional_array_segment_count(
                    association.value, array_subtype, element, scope);
                if (segment_count) {
                    if (*segment_count != 0U) {
                        assign_positional_segment(
                            *segment_count, association.source);
                    }
                    if (value && value->vhdl != nullptr
                        && value->vhdl->kind
                            == semantic::vhdl::ExpressionKind::aggregate
                        && !expression_subtype(association.value)) {
                        validate_untyped_nested_segment(
                            validate_untyped_nested_segment,
                            association.value);
                    }
                    continue;
                }
                if (positional_exhausted) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-003",
                        "VHDL array aggregate index is outside the "
                            "contextual range",
                        association.source);
                    continue;
                }
                assign(positional, association.value, association.source);
                if ((dimension.descending
                        && positional
                            == std::numeric_limits<std::int64_t>::min())
                    || (!dimension.descending
                        && positional
                            == std::numeric_limits<std::int64_t>::max())) {
                    positional_exhausted = true;
                } else {
                    positional += dimension.descending ? -1 : 1;
                }
                continue;
            }
            if (choice_is_others(association)) {
                if (association.choices.size() != 1U) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-008",
                        "an others array association cannot be "
                            "combined with another choice",
                        association.source);
                } else if (has_others) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-004",
                        "VHDL array aggregate has more than one others "
                            "association",
                        association.source);
                }
                has_others = true;
                validate_value(association.value, association.source);
                continue;
            }
            const auto assign_choice = [&](const auto& self,
                                           const semantic::ExpressionId choice_id)
                -> void {
                const auto choice = specialization_.find_expression(
                    choice_id);
                if (!choice || choice->vhdl == nullptr) {
                    return;
                }
                const auto& value = *choice->vhdl;
                if (value.kind
                        == semantic::vhdl::ExpressionKind::binary
                    && value.text == "|"
                    && value.operands.size() == 2U) {
                    self(self, value.operands[0]);
                    self(self, value.operands[1]);
                    return;
                }
                if (value.kind
                        == semantic::vhdl::ExpressionKind::binary
                    && (value.text == "to"
                        || value.text == "downto")
                    && value.operands.size() == 2U) {
                    const auto left = array_aggregate_index_value(
                        array_definition, array_dimension_index,
                        value.operands[0]);
                    const auto right = array_aggregate_index_value(
                        array_definition, array_dimension_index,
                        value.operands[1]);
                    if (!left || !right) {
                        // A non-static range is allowed as the only choice
                        // of the only association (IEEE 1076-2008 9.3.3.3);
                        // its length is checked when it is evaluated.
                        if (aggregate.associations.size() == 1U
                            && association.choices.size() == 1U) {
                            dynamic_sole_choice = true;
                            validate_value(
                                association.value, association.source);
                            return;
                        }
                        report(
                            "FSIM-ELAB-DYNINDEX-002",
                            "VHDL aggregate choice requires a locally "
                                "static value of the declared index type",
                            value.source);
                        return;
                    }
                    const auto step = value.text == "downto" ? -1 : 1;
                    if (!validate_array_range_direction(
                            association.value, step < 0, value.source)) {
                        return;
                    }
                    if ((step < 0 && *left < *right)
                        || (step > 0 && *left > *right)) {
                        // A null choice contributes no indices, but its
                        // expression still has the aggregate element or
                        // array-segment context and must be type-checked.
                        if (!null_range_has_single_association(
                                association)) {
                            return;
                        }
                        validate_null_range_value(
                            association.value, association.source);
                        return;
                    }
                    // An array-valued range choice supplies a slice (IEEE
                    // 1076-2008 9.3.3.3); its indices are assigned without
                    // the element check.
                    const bool slice = slice_value(
                        association.value, *left, *right);
                    for (auto index = *left;; index += step) {
                        if (slice) {
                            if (index < minimum || index > maximum
                                || covered_by_segment(index)
                                || !assigned.insert(index).second) {
                                report(
                                    "FSIM-ELAB-VHARRAYAGG-003",
                                    "VHDL array aggregate slice is outside "
                                        "the contextual range or overlaps "
                                        "another choice",
                                    association.source);
                                break;
                            }
                        } else {
                            assign(
                                index, association.value,
                                association.source);
                        }
                        if (index == *right) {
                            break;
                        }
                        if ((step < 0 && index < *right)
                            || (step > 0 && index > *right)) {
                            break;
                        }
                    }
                    return;
                }
                if (value.kind
                        == semantic::vhdl::ExpressionKind::call
                    && (value.text == "'range"
                        || value.text == "'reverse_range")) {
                    const auto bounds = range_choice(choice_id);
                    if (!bounds) {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-003",
                            "VHDL array range choices require a concrete, "
                                "bounded array range",
                            value.source);
                        return;
                    }
                    const auto left = bounds->left;
                    const auto right = bounds->right;
                    if (!validate_array_range_direction(
                            association.value, bounds->descending,
                            value.source)) {
                        return;
                    }
                    if (bounds->null) {
                        // A null range contributes no indices, but the
                        // associated value still has to be type-correct and
                        // an array-valued slice must have zero elements.
                        if (!null_range_has_single_association(
                                association)) {
                            return;
                        }
                        validate_null_range_value(
                            association.value, association.source);
                        return;
                    }
                    const auto low = std::min(left, right);
                    const auto high = std::max(left, right);
                    if (low < minimum || high > maximum) {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-003",
                            "VHDL array aggregate index is outside the "
                                "contextual range",
                            value.source);
                        return;
                    }
                    const auto distance
                        = static_cast<std::uint64_t>(high)
                        - static_cast<std::uint64_t>(low);
                    if (distance >= element_count) {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-003",
                            "VHDL array range choice exceeds the contextual "
                                "range",
                            value.source);
                        return;
                    }
                    auto index = left;
                    while (true) {
                        assign(index, association.value, association.source);
                        if (index == right) {
                            break;
                        }
                        if (bounds->descending) {
                            if (index
                                == std::numeric_limits<std::int64_t>::min()) {
                                return;
                            }
                            --index;
                        } else {
                            if (index
                                == std::numeric_limits<std::int64_t>::max()) {
                                return;
                            }
                            ++index;
                        }
                    }
                    return;
                }
                const auto index = array_aggregate_index_value(
                    array_definition, array_dimension_index, choice_id);
                if (!index) {
                    report(
                        "FSIM-ELAB-DYNINDEX-002",
                        "VHDL aggregate choice requires a locally "
                            "static value of the declared index type",
                        value.source);
                    return;
                }
                assign(*index, association.value, association.source);
            };
            for (const auto choice_id : association.choices) {
                assign_choice(assign_choice, choice_id);
            }
        }
        if (!has_others && !dynamic_sole_choice
            && static_cast<std::uint64_t>(assigned.size())
                    + assigned_segment_count
                < element_count) {
            report(
                "FSIM-ELAB-VHARRAYAGG-005",
                "VHDL array aggregate is missing an index",
                aggregate.source);
        }
    }

    void validate_aggregate(
        const semantic::vhdl::Expression& aggregate,
        const SubtypeIndication& target)
    {
        const auto context = profile(target);
        // A named subtype of STRING is an array context too.
        if (context.kind == ProfileKind::array
            || context.kind == ProfileKind::string) {
            auto element = array_element_subtype(target);
            if (!element) {
                element = predefined_array_element_subtype(
                    target, aggregate.scope);
            }
            if (element) {
                const auto* definition = root_definition(target);
                const auto array_identity = definition != nullptr
                        && definition->form == TypeForm::array
                    ? std::optional { definition->id }
                    : std::nullopt;
                validate_array_aggregate(
                    aggregate, context, target, *element, array_identity);
            }
            return;
        }
        const auto* definition = root_definition(target);
        if (definition != nullptr
            && definition->form == TypeForm::record) {
            validate_record_aggregate(aggregate, *definition);
            return;
        }
        report(
            "FSIM-ELAB-VHAGG-001",
            "a VHDL aggregate requires an array or record context",
            aggregate.source);
    }

    void validate_array_attribute(
        const semantic::vhdl::Expression& expression,
        const bool discrete_range_context)
    {
        const auto attribute
            = semantic::vhdl::is_scalar_bound_attribute(expression.text)
            || expression.text == "'length" || expression.text == "'ascending"
            || expression.text == "'range"
            || expression.text == "'reverse_range";
        if (!attribute || expression.operands.empty()) {
            return;
        }
        auto prefix = expression_subtype(
            expression.operands.front());
        if (!prefix) {
            const auto prefix_expression
                = specialization_.find_expression(
                    expression.operands.front());
            const auto prefix_type = prefix_expression
                    && prefix_expression->vhdl != nullptr
                    && prefix_expression->vhdl->kind
                        == semantic::vhdl::ExpressionKind::name
                ? named_type(prefix_expression->vhdl->text,
                      prefix_expression->vhdl->scope)
                : std::nullopt;
            const auto* prefix_definition = prefix_type
                ? type_definition(*prefix_type)
                : nullptr;
            if (prefix_definition != nullptr) {
                auto subtype = prefix_definition->base;
                subtype.type_mark.target = prefix_definition->id;
                if (subtype.type_mark.spelling.empty()) {
                    subtype.type_mark.spelling
                        = prefix_definition->name;
                }
                prefix = specialized_subtype(std::move(subtype));
            }
        }
        if (!prefix) {
            return;
        }
        const auto prefix_profile = profile(*prefix);
        // Only an array attribute takes a dimension argument; 'LEFT, 'RIGHT,
        // 'LOW and 'HIGH of a scalar have none (IEEE 1076-1993 14.1).
        if (expression.operands.size() == 2U
            && (prefix_profile.kind == ProfileKind::scalar
                || prefix_profile.kind == ProfileKind::integer
                || prefix_profile.kind == ProfileKind::enumeration)
            && (semantic::vhdl::is_scalar_bound_attribute(expression.text))) {
            report("FSIM-ELAB-VHATTR-015",
                "the scalar attribute " + expression.text
                    + " does not take an argument",
                expression.source);
            return;
        }
        if (prefix_profile.kind == ProfileKind::unknown
            || (prefix_profile.kind != ProfileKind::array
                && prefix_profile.kind != ProfileKind::record)) {
            return;
        }
        if (prefix_profile.kind == ProfileKind::array
            && prefix_profile.dimensions.empty()) {
            return;
        }
        // An unconstrained interface object (a port or subprogram formal)
        // takes its index ranges from its actual (IEEE 1076-2008 6.5.6.3).
        const auto interface_prefix = [&] {
            auto prefix_expression = specialization_.find_expression(
                expression.operands.front());
            // An element of an interface object also takes its bounds from
            // the actual (IEEE 1076-2008 5.3.2.2).
            for (std::size_t depth { }; depth < 16U && prefix_expression
                 && prefix_expression->vhdl != nullptr
                 && (prefix_expression->vhdl->kind
                         == semantic::vhdl::ExpressionKind::index
                     || prefix_expression->vhdl->kind
                         == semantic::vhdl::ExpressionKind::slice
                     || (prefix_expression->vhdl->kind
                             == semantic::vhdl::ExpressionKind::call
                         && prefix_expression->vhdl->text.starts_with(
                             "@vhdl-member:")))
                 && !prefix_expression->vhdl->operands.empty();
                 ++depth) {
                prefix_expression = specialization_.find_expression(
                    prefix_expression->vhdl->operands.front());
            }
            if (!prefix_expression || prefix_expression->vhdl == nullptr) {
                return false;
            }
            auto selected = prefix_expression->vhdl->referenced_name
                    ? prefix_expression->vhdl->referenced_name->selected
                    : std::nullopt;
            // A selected record-field name may have no retained object
            // identity. Recover its object prefix only through ordinary
            // scoped declaration resolution.
            if (prefix_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::name
                && !selected) {
                const auto& text = prefix_expression->vhdl->text;
                auto separator = text.find('.');
                if (!text.empty() && text.front() == '\\') {
                    const auto closing = text.find('\\', 1U);
                    separator = closing != std::string::npos
                            && closing + 1U < text.size()
                            && text[closing + 1U] == '.'
                        ? closing + 1U
                        : std::string::npos;
                }
                using Form = semantic::vhdl::DeclarationForm;
                const semantic::CompiledDeclarationPredicate object
                    = [](const semantic::CompiledDeclarationView& candidate) {
                          if (candidate.vhdl == nullptr) {
                              return false;
                          }
                          const auto form = candidate.vhdl->form;
                          return semantic::vhdl::is_value_object_form(form)
                              || form == Form::alias;
                      };
                if (separator != std::string::npos) {
                    semantic::vhdl::Name object_name;
                    object_name.spelling = text.substr(0U, separator);
                    object_name.canonical = object_name.spelling;
                    selected = semantic::CompiledDesignResolver {
                                   specialization_ }
                                   .resolve_vhdl(object_name,
                                       prefix_expression->vhdl->scope,
                                       object)
                                   .unique();
                }
            }
            if (!selected) {
                return false;
            }
            const auto selected_id = *selected;
            // A formal's unconstrained bounds come from each actual; this
            // applies to both the formal itself and a selected element.
            if (subprogram_formal(selected_id)) {
                return true;
            }
            const auto declaration = specialization_.find_declaration(
                selected_id);
            // Block generic actuals are bound when the generated occurrence
            // is elaborated, after this unit-level validation. Like a port,
            // this interface object obtains its bounds from that actual.
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->form
                    == semantic::vhdl::DeclarationForm::generic_constant) {
                const auto block_formal = [&](const auto& self,
                                              const auto& regions) -> bool {
                    return std::ranges::any_of(regions, [&](const auto& region) {
                        return (region.kind == semantic::vhdl::GenerateKind::block
                                   && std::ranges::find(region.declarations, selected_id)
                                       != region.declarations.end())
                            || self(self, region.nested);
                    });
                };
                if (block_formal(block_formal, architecture_.generates)) {
                    return true;
                }
            }
            // A constant with a value takes its index ranges from it
            // (IEEE 1076-2008 5.3.2.2).
            if (!declaration || declaration->vhdl == nullptr) {
                return false;
            }
            const auto& vhdl_declaration = *declaration->vhdl;
            if (vhdl_declaration.form
                == semantic::vhdl::DeclarationForm::port) {
                return true;
            }
            if (vhdl_declaration.form
                    != semantic::vhdl::DeclarationForm::constant) {
                return false;
            }
            if (vhdl_declaration.initializer) {
                return true;
            }
            // A deferred package constant gets its value from its validated
            // package-body completion, so it has the same value-bounded
            // array attributes as an initialized constant declaration.
            if (!vhdl_declaration.deferred
                || !vhdl_declaration.completion) {
                return false;
            }
            const auto completion = specialization_.find_declaration(
                *vhdl_declaration.completion);
            return completion && completion->vhdl != nullptr
                && completion->vhdl->form
                    == semantic::vhdl::DeclarationForm::constant
                && completion->vhdl->initializer.has_value();
        };
        // An entity generic of an unconstrained array type obtains its
        // bounds from the generic actual.  In particular, a later generic
        // default may use an earlier generic's 'RANGE in its subtype
        // indication (IEEE 1076-2008 5.3.2.3).  Accept that context only
        // when this exact generic has an actual whose constrained subtype
        // can be resolved in this specialization.
        const auto entity_generic_actual_prefix = [&] {
            const auto prefix_expression = specialization_.find_expression(
                expression.operands.front());
            if (!prefix_expression || prefix_expression->vhdl == nullptr
                || prefix_expression->vhdl->kind
                    != semantic::vhdl::ExpressionKind::name
                || !prefix_expression->vhdl->referenced_name
                || !prefix_expression->vhdl->referenced_name->selected) {
                return false;
            }
            const auto formal_id
                = *prefix_expression->vhdl->referenced_name->selected;
            const auto formal
                = specialization_.find_declaration(formal_id);
            if (!formal || formal->vhdl == nullptr
                || formal->vhdl->form
                    != semantic::vhdl::DeclarationForm::generic_constant) {
                return false;
            }
            const auto& actuals
                = specialization_.specialization().actual_identities;
            const auto actual = std::ranges::find(
                actuals, formal_id,
                &semantic::SpecializedHirActualIdentity::declaration);
            if (actual == actuals.end()) {
                return false;
            }
            std::optional<SubtypeIndication> actual_subtype;
            if (actual->actual_expression) {
                actual_subtype = expression_subtype(
                    *actual->actual_expression);
            }
            if (!actual_subtype && actual->vhdl_type) {
                actual_subtype = specialized_subtype(*actual->vhdl_type);
            }
            if (!actual_subtype && actual->actual_declaration) {
                const auto actual_declaration
                    = specialization_.find_declaration(
                        *actual->actual_declaration);
                if (actual_declaration && actual_declaration->vhdl != nullptr
                    && actual_declaration->vhdl->subtype) {
                    actual_subtype = specialized_subtype(
                        *actual_declaration->vhdl->subtype);
                }
            }
            if (!actual_subtype) {
                return false;
            }
            const auto formal_type = subtype_type(
                specialized_subtype(*prefix));
            const auto actual_type = subtype_type(
                specialized_subtype(*actual_subtype));
            const auto formal_root = formal_type
                ? array_root(*formal_type) : std::nullopt;
            const auto actual_root = actual_type
                ? array_root(*actual_type) : std::nullopt;
            if (!formal_root || !actual_root
                || *formal_root != *actual_root) {
                return false;
            }
            const auto actual_profile = profile(
                *actual_subtype);
            return actual_profile.kind == ProfileKind::array
                && actual_profile.dimensions.size() == 1U
                && std::ranges::all_of(
                    actual_profile.dimensions,
                    [](const ArrayDimensionProfile& dimension) {
                        return dimension.constrained
                            && dimension.minimum.has_value()
                            && dimension.maximum.has_value();
                    });
        };
        const auto contextual_result
            = prefix_profile.kind == ProfileKind::array
            && (contextual_result_prefix(
                    expression.operands.front(), *prefix)
                || interface_prefix()
                || entity_generic_actual_prefix());
        if (prefix_profile.kind == ProfileKind::record
            || std::ranges::any_of(
                prefix_profile.dimensions,
                [](const auto& dimension) {
                    return !dimension.constrained;
                })) {
            if (!contextual_result) {
                report(
                    "FSIM-ELAB-VHARRAYATTR-001",
                    "VHDL array attribute prefix does not have a concrete "
                        "bounded array range",
                    expression.source);
                return;
            }
        }
        if (expression.operands.size() == 2U) {
            const auto dimension
                = specialization_.evaluate_integral_expression(
                    expression.operands[1]);
            if (!dimension || *dimension <= 0
                || static_cast<std::uint64_t>(*dimension)
                    > prefix_profile.dimensions.size()) {
                report(
                    "FSIM-ELAB-VHARRAYATTR-002",
                    "VHDL array attribute dimension is nonstatic or "
                        "outside the array rank",
                    expression.source);
            }
        }
        if (!discrete_range_context
            && (expression.text == "'range"
                || expression.text == "'reverse_range")) {
            report(
                "FSIM-ELAB-VHARRAYATTR-003",
                "VHDL range attribute cannot be used as a scalar "
                    "expression",
                expression.source);
        }
    }

    struct Selection {
        SubtypeIndication subtype;
        std::vector<semantic::ExpressionId> selectors;
        semantic::SourceSpanId source;
        bool slice_result { };
        bool invalid_index_group { };
    };

    std::optional<RangeConstraint> slice_selection_range(
        const semantic::ExpressionId selector_id,
        const SubtypeIndication& array_subtype) const
    {
        const auto selector = specialization_.find_expression(selector_id);
        if (!selector || selector->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto array_profile = profile(array_subtype);
        if (array_profile.kind != ProfileKind::array
            || array_profile.dimensions.size() != 1U) {
            return std::nullopt;
        }
        const auto& expression = *selector->vhdl;
        if (expression.kind == semantic::vhdl::ExpressionKind::slice) {
            if (expression.operands.size() != 3U
                || (expression.text != "to"
                    && expression.text != "downto")) {
                return std::nullopt;
            }
            RangeConstraint result;
            result.kind = RangeKind::array_index;
            result.left_expression = expression.operands[1];
            result.right_expression = expression.operands[2];
            result.left = bound(std::nullopt, expression.operands[1]);
            result.right = bound(std::nullopt, expression.operands[2]);
            result.descending = expression.text == "downto";
            result.source = expression.source;
            result.null = result.left && result.right
                && (result.descending
                    ? *result.left < *result.right
                    : *result.left > *result.right);
            return result;
        }
        if (expression.kind == semantic::vhdl::ExpressionKind::binary
            && expression.operands.size() == 2U
            && (expression.text == "to"
                || expression.text == "downto")) {
            RangeConstraint result;
            result.kind = RangeKind::array_index;
            result.left_expression = expression.operands[0];
            result.right_expression = expression.operands[1];
            result.left = bound(std::nullopt, expression.operands[0]);
            result.right = bound(std::nullopt, expression.operands[1]);
            result.descending = expression.text == "downto";
            result.source = expression.source;
            result.null = result.left && result.right
                && (result.descending
                    ? *result.left < *result.right
                    : *result.left > *result.right);
            return result;
        }
        if (expression.kind != semantic::vhdl::ExpressionKind::call
            || (expression.text != "'range"
                && expression.text != "'reverse_range")
            || expression.operands.empty()
            || expression.operands.size() > 2U) {
            return std::nullopt;
        }
        if (expression.operands.size() == 2U) {
            const auto dimension = bound(
                std::nullopt, expression.operands[1]);
            if (!dimension || *dimension != 1) {
                return std::nullopt;
            }
        }
        const auto prefix_id = expression.operands.front();
        std::optional<RangeConstraint> selected_prefix_range;
        if (const auto prefix_selection = array_selection(prefix_id);
            prefix_selection && prefix_selection->slice_result
            && prefix_selection->selectors.size() == 1U) {
            selected_prefix_range = slice_selection_range(
                prefix_selection->selectors.front(),
                prefix_selection->subtype);
        } else {
            const auto prefix_subtype = expression_subtype(prefix_id);
            if (!prefix_subtype) {
                return std::nullopt;
            }
            const auto prefix_profile = profile(
                *prefix_subtype);
            if (prefix_profile.kind != ProfileKind::array
                || prefix_profile.dimensions.size() != 1U
                || !prefix_profile.dimensions.front().constrained) {
                return std::nullopt;
            }
            const RangeConstraint* prefix_range { };
            if (!prefix_subtype->constraints.empty()
                && prefix_subtype->constraints.front().kind
                    == RangeKind::array_index) {
                prefix_range = &prefix_subtype->constraints.front();
            } else {
                const auto* prefix_array = root_definition(*prefix_subtype);
                if (prefix_array != nullptr
                    && prefix_array->form == TypeForm::array
                    && prefix_array->array_dimensions.size() == 1U
                    && prefix_array->array_dimensions.front().constraint) {
                    prefix_range
                        = &*prefix_array->array_dimensions.front().constraint;
                }
            }
            if (prefix_range == nullptr) {
                return std::nullopt;
            }
            selected_prefix_range = *prefix_range;
        }
        if (!selected_prefix_range) {
            return std::nullopt;
        }
        // Apply range-attribute direction handling to both a direct retained
        // prefix range and a recursively recovered slice range. In
        // particular, an outer 'reverse_range must still reverse the range
        // when its prefix is itself a slice.
        RangeConstraint result = *selected_prefix_range;
        result.left = bound(result.left, result.left_expression);
        result.right = bound(result.right, result.right_expression);
        if (!result.left || !result.right
            || result.direction_from_bounds) {
            return std::nullopt;
        }
        auto descending = result.descending;
        if (expression.text == "'reverse_range") {
            std::swap(result.left, result.right);
            std::swap(result.left_expression, result.right_expression);
            descending = !descending;
        }
        result.descending = descending;
        result.direction_from_bounds = false;
        result.source = expression.source;
        if (result.left && result.right) {
            result.null = result.descending
                ? *result.left < *result.right
                : *result.left > *result.right;
        }
        return result;
    }

    std::optional<Selection> array_selection(
        const semantic::ExpressionId id) const
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto& value = *expression->vhdl;
        if (value.kind == semantic::vhdl::ExpressionKind::call) {
            if (value.text.empty() || value.text.front() == '\''
                || value.text.front() == '@'
                || !value.referenced_name
                || !value.referenced_name->selected) {
                return std::nullopt;
            }
            const semantic::CompiledDesignResolver resolver {
                specialization_ };
            const auto callable_resolution = resolver.resolve_vhdl_callables(
                *value.referenced_name, value.scope);
            if (callable_resolution.status
                != semantic::CompiledResolutionStatus::not_found) {
                return std::nullopt;
            }
            const auto declaration = specialization_.find_declaration(
                *value.referenced_name->selected);
            if (!declaration || declaration->vhdl == nullptr
                || !declaration->vhdl->subtype) {
                return std::nullopt;
            }
            using Form = semantic::vhdl::DeclarationForm;
            const auto form = declaration->vhdl->form;
            const auto indexed_object
                = semantic::vhdl::is_value_object_form(form)
                || form == Form::alias;
            if (!indexed_object) {
                return std::nullopt;
            }
            const auto subtype = specialized_subtype(
                *declaration->vhdl->subtype);
            const auto selected_profile = profile(subtype);
            if (selected_profile.kind != ProfileKind::array) {
                return std::nullopt;
            }
            Selection selection {
                subtype, value.operands, value.source, false };
            selection.invalid_index_group
                = selection.selectors.size()
                    != selected_profile.dimensions.size();
            if (selection.selectors.size() == 1U
                && selected_profile.dimensions.size() == 1U) {
                const auto selector = specialization_.find_expression(
                    selection.selectors.front());
                selection.slice_result = selector
                    && selector->vhdl != nullptr
                    && selector->vhdl->kind
                        == semantic::vhdl::ExpressionKind::call
                    && (selector->vhdl->text == "'range"
                        || selector->vhdl->text == "'reverse_range");
            }
            return selection;
        }
        if ((value.kind != semantic::vhdl::ExpressionKind::index
                && value.kind
                    != semantic::vhdl::ExpressionKind::slice)
            || value.operands.size() < 2U) {
            return std::nullopt;
        }
        auto base = array_selection(value.operands.front());
        if (!base) {
            const auto subtype = expression_subtype(
                value.operands.front());
            if (!subtype) {
                return std::nullopt;
            }
            const auto base_profile = profile(*subtype);
            if (base_profile.kind != ProfileKind::array) {
                return std::nullopt;
            }
            base = Selection { *subtype, { }, value.source, false };
        }
        const auto base_profile = profile(base->subtype);
        if (base->slice_result) {
            if (base->selectors.size() == 1U) {
                if (const auto selected_range = slice_selection_range(
                        base->selectors.front(), base->subtype)) {
                    if (!base->subtype.constraints.empty()
                        && base->subtype.constraints.front().kind
                            == RangeKind::array_index) {
                        base->subtype.constraints.front() = *selected_range;
                    } else {
                        base->subtype.constraints.insert(
                            base->subtype.constraints.begin(),
                            *selected_range);
                    }
                }
            }
            base->selectors.clear();
        } else if (!base_profile.dimensions.empty()
            && base->selectors.size()
                == base_profile.dimensions.size()) {
            if (const auto element
                = array_element_subtype(base->subtype)) {
                base->subtype = *element;
                base->selectors.clear();
            }
        }
        if (value.kind == semantic::vhdl::ExpressionKind::slice) {
            base->selectors.push_back(id);
            base->slice_result = true;
        } else {
            const auto group_size = value.operands.size() - 1U;
            const auto group_profile = profile(base->subtype);
            const auto rank = group_profile.dimensions.size();
            if (!base->selectors.empty() || group_size != rank) {
                base->invalid_index_group = true;
            }
            for (std::size_t index = 1U;
                index < value.operands.size(); ++index) {
                base->selectors.push_back(value.operands[index]);
            }
            base->slice_result = false;
            if (base->selectors.size() == 1U && rank == 1U) {
                const auto selector = specialization_.find_expression(
                    base->selectors.front());
                base->slice_result = selector
                    && selector->vhdl != nullptr
                    && ((selector->vhdl->kind
                            == semantic::vhdl::ExpressionKind::binary
                        && (selector->vhdl->text == "to"
                            || selector->vhdl->text == "downto"))
                        || (selector->vhdl->kind
                                == semantic::vhdl::ExpressionKind::call
                            && (selector->vhdl->text == "'range"
                                || selector->vhdl->text
                                    == "'reverse_range")));
            }
        }
        base->source = value.source;
        return base;
    }

    std::optional<SubtypeIndication> selection_result_subtype(
        const Selection& selection) const
    {
        if (selection.invalid_index_group) {
            return std::nullopt;
        }
        const auto* array = root_definition(selection.subtype);
        if (array == nullptr || array->form != TypeForm::array
            || array->array_dimensions.empty()
            || selection.selectors.size()
                != array->array_dimensions.size()) {
            return std::nullopt;
        }
        return array_element_subtype(selection.subtype);
    }

    // A static index of an object of a constrained STRING subtype must lie
    // in its index range (IEEE 1076-2008 8.4).
    void validate_string_index(const semantic::ExpressionId id)
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return;
        }
        const auto& value = *expression->vhdl;
        std::optional<semantic::DeclarationId> prefix;
        std::optional<semantic::ExpressionId> index;
        if (value.kind == semantic::vhdl::ExpressionKind::call
            && value.operands.size() == 1U && value.referenced_name) {
            prefix = value.referenced_name->selected;
            index = value.operands.front();
        } else if (value.kind == semantic::vhdl::ExpressionKind::index
            && value.operands.size() == 2U) {
            const auto base = specialization_.find_expression(
                value.operands.front());
            if (base && base->vhdl != nullptr
                && base->vhdl->kind == semantic::vhdl::ExpressionKind::name
                && base->vhdl->referenced_name) {
                prefix = base->vhdl->referenced_name->selected;
            }
            index = value.operands.back();
        }
        if (!prefix || !index) {
            return;
        }
        const auto declaration = specialization_.find_declaration(*prefix);
        if (!declaration || declaration->vhdl == nullptr
            || !declaration->vhdl->subtype) {
            return;
        }
        using Form = semantic::vhdl::DeclarationForm;
        const auto form = declaration->vhdl->form;
        if (form != Form::signal && form != Form::constant
            && form != Form::variable) {
            return;
        }
        const auto& subtype = *declaration->vhdl->subtype;
        if (!equal_ignoring_ascii_case(
                simple_name_of(subtype.type_mark.spelling), "string")
            || subtype.constraints.size() != 1U
            || subtype.constraints.front().kind
                != semantic::vhdl::RangeKind::array_index) {
            return;
        }
        const auto selector = specialization_.find_expression(*index);
        if (!selector || selector->vhdl == nullptr
            || selector->vhdl->kind
                != semantic::vhdl::ExpressionKind::integer_literal) {
            return;
        }
        const auto& constraint = subtype.constraints.front();
        const auto left = bound(constraint.left, constraint.left_expression);
        const auto right = bound(constraint.right, constraint.right_expression);
        const auto position = bound(std::nullopt, *index);
        if (left && right && position
            && (*position < std::min(*left, *right)
                || *position > std::max(*left, *right))) {
            report(
                "FSIM-ELAB-VHARRAYSEL-003",
                "VHDL array index is outside its selected source "
                    "dimension",
                selector->vhdl->source);
        }
    }

    void validate_selection(const semantic::ExpressionId id)
    {
        const auto selected = array_selection(id);
        if (!selected) {
            validate_string_index(id);
            return;
        }
        const auto selected_profile = profile(
            selected->subtype);
        if (selected->invalid_index_group
            || selected->selectors.size()
                > selected_profile.dimensions.size()
            || (!selected->slice_result
                && selected->selectors.size()
                    != selected_profile.dimensions.size())) {
            report(
                "FSIM-ELAB-VHARRAYSEL-004",
                "VHDL array selection has incompatible contextual shape",
                selected->source);
            return;
        }
        // IEEE 1076-2008 8.4: an indexed name has one index per dimension,
        // each of the dimension's index type. Resolve the declared index mark
        // in the array type's declaration scope so a consumer's same-named
        // local type cannot shadow the array's retained index type.
        const auto* array_definition = root_definition(selected->subtype);
        const auto declared_index_scope = array_definition != nullptr
            ? type_declaration_scope(*array_definition)
            : semantic::ScopeId { };
        const auto index_type_scope = declared_index_scope.valid()
            ? declared_index_scope
            : specialization_.scope();
        const bool any_slice = std::ranges::any_of(selected->selectors,
            [&](const semantic::ExpressionId selector_id) {
                const auto selector = specialization_.find_expression(selector_id);
                return selector && selector->vhdl != nullptr
                    && (selector->vhdl->kind == semantic::vhdl::ExpressionKind::slice
                        || (selector->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                            && (selector->vhdl->text == "to"
                                || selector->vhdl->text == "downto"))
                        || (selector->vhdl->kind
                                == semantic::vhdl::ExpressionKind::call
                            && (selector->vhdl->text == "'range"
                                || selector->vhdl->text
                                    == "'reverse_range")));
            });
        if (array_definition != nullptr && array_definition->form == TypeForm::array
            && !any_slice) {
            for (std::size_t index { };
                index < selected->selectors.size()
                && index < array_definition->array_dimensions.size();
                ++index) {
                const auto expected = dimension_index_type(
                    array_definition->array_dimensions[index], index_type_scope);
                const auto actual = operand_type(selected->selectors[index]);
                if (expected.kind == OperandClass::unknown
                    || actual.kind == OperandClass::unknown) {
                    continue;
                }
                if (types_conflict(actual, expected)) {
                    report("FSIM-ELAB-VHARRAYSEL-006",
                        "a VHDL array index is not of the dimension's index type",
                        selected->source);
                    return;
                }
            }
        }
        for (std::size_t index { };
            index < selected->selectors.size(); ++index) {
            const auto selector = specialization_.find_expression(
                selected->selectors[index]);
            if (!selector || selector->vhdl == nullptr) {
                continue;
            }
            const auto& dimension = selected_profile.dimensions[index];
            const auto& expression = *selector->vhdl;
            const bool range_attribute
                = expression.kind
                    == semantic::vhdl::ExpressionKind::call
                && (expression.text == "'range"
                    || expression.text == "'reverse_range");
            const auto slice
                = expression.kind
                    == semantic::vhdl::ExpressionKind::slice
                || (expression.kind
                        == semantic::vhdl::ExpressionKind::binary
                    && (expression.text == "to"
                        || expression.text == "downto"))
                || range_attribute;
            if (slice) {
                if (range_attribute) {
                    const auto range_prefix
                        = expression.operands.empty()
                        ? std::nullopt
                        : expression_subtype(
                              expression.operands.front());
                    const auto range_profile = range_prefix
                        ? profile(*range_prefix)
                        : TypeProfile { };
                    if (range_profile.kind != ProfileKind::array
                        || range_profile.dimensions.size() != 1U) {
                        report(
                            "FSIM-ELAB-VHARRAYSEL-004",
                            "a VHDL array range selector requires a "
                                "one-dimensional array",
                            expression.source);
                        return;
                    }
                    const auto index_type = [&](const SubtypeIndication& array,
                                                const semantic::ScopeId scope) {
                        const auto* definition = root_definition(array);
                        if (definition != nullptr
                            && definition->form == TypeForm::array
                            && definition->array_dimensions.size() == 1U) {
                            const auto declaration_scope
                                = type_declaration_scope(*definition);
                            return dimension_index_type(
                                definition->array_dimensions.front(),
                                declaration_scope.valid()
                                    ? declaration_scope : scope);
                        }
                        // The compiler-owned predefined one-dimensional
                        // arrays use INTEGER-family index subtypes (NATURAL
                        // or POSITIVE). Their nominal array identity is not
                        // the type required by a slice range.
                        if (predefined_array_identity(array, scope)) {
                            return OperandType {
                                OperandClass::integer, { } };
                        }
                        return OperandType { };
                    };
                    const auto expected_index_type = index_type(
                        selected->subtype, expression.scope);
                    const auto range_index_type = index_type(
                        *range_prefix, expression.scope);
                    if (expected_index_type.kind == OperandClass::unknown
                        || range_index_type.kind == OperandClass::unknown
                        || types_conflict(
                            range_index_type, expected_index_type)) {
                        report(
                            "FSIM-ELAB-VHARRAYSEL-006",
                            "a VHDL array range selector must use the "
                                "selected array's index type",
                            expression.source);
                        return;
                    }
                    const auto& range_dimension
                        = range_profile.dimensions.front();
                    if (dimension.constrained
                        && range_dimension.constrained
                        && dimension.bounds_known
                        && dimension.direction_known
                        && dimension.null_known
                        && range_dimension.bounds_known
                        && range_dimension.direction_known
                        && range_dimension.null_known
                        && dimension.minimum && dimension.maximum
                        && range_dimension.left && range_dimension.right) {
                        auto left = range_dimension.left;
                        auto right = range_dimension.right;
                        auto descending = range_dimension.descending;
                        if (expression.text == "'reverse_range") {
                            std::swap(left, right);
                            descending = !descending;
                        }
                        const bool null_range = range_dimension.null
                            || (descending
                                ? *left < *right
                                : *left > *right);
                        const auto outside
                            = !null_range
                            && (std::min(*left, *right)
                                    < *dimension.minimum
                                || std::max(*left, *right)
                                    > *dimension.maximum);
                        if (outside
                            || descending != dimension.descending) {
                            report(
                                "FSIM-ELAB-VHARRAYSEL-004",
                                "VHDL array range has incompatible "
                                    "direction, bounds, placement, or "
                                    "contextual shape",
                                expression.source);
                        }
                    }
                    continue;
                }
                const auto operands = expression.operands;
                const auto first = expression.kind
                        == semantic::vhdl::ExpressionKind::slice
                    ? 1U
                    : 0U;
                if (array_definition != nullptr
                    && array_definition->form == TypeForm::array
                    && index < array_definition->array_dimensions.size()) {
                    const auto expected = dimension_index_type(
                        array_definition->array_dimensions[index],
                        index_type_scope);
                    const auto bound_has_wrong_type
                        = [&](const std::size_t operand) {
                              if (operand >= operands.size()
                                  || expected.kind == OperandClass::unknown) {
                                  return false;
                              }
                              const auto actual = operand_type(operands[operand]);
                              const auto conflict
                                  = actual.kind != OperandClass::unknown
                                  && types_conflict(actual, expected);
                              return conflict;
                          };
                    if (bound_has_wrong_type(first)
                        || bound_has_wrong_type(first + 1U)) {
                        report("FSIM-ELAB-VHARRAYSEL-006",
                            "a VHDL array slice bound is not of the "
                            "dimension's index type",
                            expression.source);
                        return;
                    }
                }
                const auto left = operands.size() > first
                    ? bound(std::nullopt, operands[first])
                    : std::nullopt;
                const auto right = operands.size() > first + 1U
                    ? bound(std::nullopt, operands[first + 1U])
                    : std::nullopt;
                const auto descending = expression.text == "downto";
                // A null slice need not lie within the index range
                // (IEEE 1076-2008 8.5).
                const bool null_slice = left && right
                    && (descending ? *left < *right : *left > *right);
                const auto outside = dimension.bounds_known && left && right
                    && !null_slice
                    && ((dimension.minimum
                            && (std::min(*left, *right)
                                < *dimension.minimum))
                        || (dimension.maximum
                            && (std::max(*left, *right)
                                > *dimension.maximum)));
                if (outside
                    || (dimension.constrained
                        && dimension.direction_known
                        && dimension.descending != descending)) {
                    report(
                        "FSIM-ELAB-VHARRAYSEL-004",
                        "VHDL array slice has incompatible direction, "
                            "bounds, placement, or contextual shape",
                        expression.source);
                }
                continue;
            }
            const auto index_profile = expression_profile(
                selected->selectors[index]);
            // An index is a value of the dimension's discrete index type;
            // enumeration values select by position (IEEE 1076-2008 8.4).
            if (index_profile
                && index_profile->kind != ProfileKind::integer
                && index_profile->kind != ProfileKind::enumeration
                && operand_type(selected->selectors[index]).kind
                    != OperandClass::enumeration) {
                report(
                    "FSIM-ELAB-VHARRAYSEL-002",
                    "VHDL array index requires an integer-family signed "
                        "32-bit value with representable bounds",
                    expression.source);
                continue;
            }
            auto value = bound(
                std::nullopt, selected->selectors[index]);
            // A character literal indexes a CHARACTER dimension by its
            // CHARACTER position ('0' is 48), not its BIT position.
            if (const auto literal = specialization_.find_expression(selected->selectors[index]);
                value && literal && literal->vhdl != nullptr
                && literal->vhdl->kind == semantic::vhdl::ExpressionKind::logic_literal
                && literal->vhdl->text.size() == 3U && literal->vhdl->text.front() == '\''
                && ((dimension.minimum && *value < *dimension.minimum)
                    || (dimension.maximum && *value > *dimension.maximum))) {
                value = static_cast<std::int64_t>(
                    static_cast<unsigned char>(literal->vhdl->text[1]));
            }
            if (value
                && ((dimension.null_known && dimension.null)
                    || (dimension.minimum
                        && *value < *dimension.minimum)
                    || (dimension.maximum
                        && *value > *dimension.maximum))) {
                report(
                    "FSIM-ELAB-VHARRAYSEL-003",
                    "VHDL array index is outside its selected source "
                        "dimension",
                    expression.source);
            }
        }
    }

    static std::string canonical_name(const std::string_view spelling)
    {
        std::string result;
        result.reserve(spelling.size());
        for (const char value : spelling) {
            result.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(value))));
        }
        return result;
    }

    void validate_instance_boundaries()
    {
        for (const auto instance_id : architecture_.instances) {
            const auto instance = specialization_.find_instance(
                instance_id);
            if (!instance || instance->vhdl == nullptr) {
                continue;
            }
            const auto target_spelling = canonical_name(
                instance->vhdl->target.spelling);
            const semantic::vhdl::Unit* target_entity = nullptr;
            for (const auto& candidate :
                specialization_.design().vhdl_hir.units()) {
                if (candidate.kind
                        != semantic::vhdl::UnitKind::entity
                    || target_spelling.find(canonical_name(candidate.name))
                        == std::string::npos) {
                    continue;
                }
                target_entity = &candidate;
                break;
            }
            if (target_entity == nullptr) {
                continue;
            }
            std::size_t positional { };
            for (const auto& association :
                instance->vhdl->port_map) {
                const semantic::vhdl::Declaration* formal = nullptr;
                if (association.formal
                    && association.formal->selected) {
                    const auto declaration
                        = specialization_.find_declaration(
                            *association.formal->selected);
                    formal = declaration
                            && declaration->vhdl != nullptr
                        ? declaration->vhdl
                        : nullptr;
                }
                if (formal == nullptr) {
                    std::size_t port_index { };
                    for (const auto declaration_id :
                        target_entity->declarations) {
                        const auto declaration
                            = specialization_.find_declaration(
                                declaration_id);
                        if (!declaration
                            || declaration->vhdl == nullptr
                            || declaration->vhdl->form
                                != semantic::vhdl::DeclarationForm::port) {
                            continue;
                        }
                        const auto selected = association.formal
                            ? equal_ignoring_ascii_case(
                                  declaration->vhdl->name,
                                  association.formal->spelling)
                            : port_index == positional;
                        if (selected) {
                            formal = declaration->vhdl;
                            break;
                        }
                        ++port_index;
                    }
                }
                ++positional;
                if (formal == nullptr || !formal->subtype
                    || !association.expression) {
                    continue;
                }
                const auto actual = expression_subtype(
                    *association.expression);
                const auto formal_type = subtype_type(
                    specialized_subtype(*formal->subtype));
                const auto actual_type = actual
                    ? subtype_type(*actual)
                    : std::nullopt;
                const auto formal_root = formal_type
                    ? array_root(*formal_type)
                    : std::nullopt;
                const auto actual_root = actual_type
                    ? array_root(*actual_type)
                    : std::nullopt;
                if (!formal_root || !actual_root
                    || *formal_root == *actual_root) {
                    continue;
                }
                report(
                    "FSIM-ELAB-BIND-056",
                    "VHDL hierarchy boundary connects different nominal "
                        "array types",
                    association.source);
            }
        }
    }

    void validate_operations()
    {
        const auto selected_scope = [&](const semantic::ScopeId scope) {
            const auto& scopes
                = specialization_.design().semantics.scopes();
            return scope.valid() && scope.value() < scopes.size()
                && (scopes[scope.value()].unit == entity_.id
                    || scopes[scope.value()].unit == architecture_.id);
        };
        validate_subprogram_bodies(selected_scope);
        for (const auto& stored : specialization_.design().vhdl_hir.declarations()) {
            using Form = semantic::vhdl::DeclarationForm;
            // IEEE 1076-2008 6.6.2: an object alias's subtype has the type of
            // the aliased object, and is one-dimensional before VHDL-2008.
            if (stored.form == Form::alias && stored.subtype && stored.alias_target
                && selected_scope(stored.scope)) {
                validate_object_alias(stored);
            }
            if (stored.subtype && stored.initializer && selected_scope(stored.scope)
                && (stored.form == Form::constant || stored.form == Form::variable
                    || stored.form == Form::signal || stored.form == Form::generic_constant
                    || (stored.form == Form::port
                        && stored.object_class == semantic::vhdl::ObjectClass::signal))) {
                validate_aggregate(*stored.initializer, *stored.subtype);
                // 6.4.2: an initial value has the object's type.
                const auto expected = subtype_operand_type(*stored.subtype, 0U);
                const auto given = operand_type(*stored.initializer);
                if (scalar_class(expected.kind) && scalar_class(given.kind)
                    && types_conflict(given, expected)) {
                    report("FSIM-ELAB-VHASSIGN-001",
                        "the initial value of VHDL object '" + stored.name
                            + "' does not have its type",
                        stored.source);
                }
                // An access object's initializer is an access value (or the
                // universal null access value). A qualified designated
                // record value such as CELL'(...) is not an allocator and
                // cannot initialize LINK; requiring the access type here
                // prevents silently accepting the missing `new`.
                if (expected.kind == OperandClass::access
                    && given.kind != OperandClass::access
                    && given.kind != OperandClass::null
                    && given.kind != OperandClass::unknown) {
                    report("FSIM-ELAB-VHASSIGN-001",
                        "the initial value of VHDL access object '"
                            + stored.name + "' is not an access value",
                        stored.source);
                }
            }
            // 6.7: an attribute is not of an access or file type.
            if (stored.form == Form::attribute_declaration && stored.attribute
                && stored.attribute->subtype && selected_scope(stored.scope)) {
                const auto* attribute_type = root_definition(*stored.attribute->subtype);
                if (attribute_type != nullptr
                    && (attribute_type->form == TypeForm::access
                        || attribute_type->form == TypeForm::file)) {
                    report("FSIM-ELAB-VHATTR-014",
                        "the type of VHDL attribute '" + stored.name
                            + "' cannot be an access or file type",
                        stored.source);
                }
            }
            if (!stored.subtype || !selected_scope(stored.scope)) {
                continue;
            }
            if (stored.form == Form::constant || stored.form == Form::variable
                || stored.form == Form::signal) {
                validate_index_constraint_types(stored);
            }
            // IEEE 1076-2008 6.4.2.3, 6.4.2.4: a signal or variable has a
            // constrained (fully bounded) array subtype. VHDL-2019 takes the
            // constraints of one with an initial value from that value.
            if ((stored.form == Form::signal || stored.form == Form::variable)
                && unconstrained_array_subtype(*stored.subtype)
                && !(stored.initializer && unit_standard(stored.scope) == "2019")) {
                report("FSIM-ELAB-VHARRAY-010",
                    "VHDL " + std::string { stored.form == Form::signal ? "signal" : "variable" }
                        + " '" + stored.name + "' requires a constrained array subtype",
                    stored.source);
            }
            const auto* root = root_definition(*stored.subtype);
            validate_declaration_ranges(stored);
            // 6.5.2: an interface constant or signal is not of a file type.
            if ((stored.form == Form::generic_constant
                    || (stored.form == Form::port
                        && stored.object_class != semantic::vhdl::ObjectClass::file))
                && ((root != nullptr && root->form == TypeForm::file)
                    // STD.TEXTIO's TEXT has no HIR type definition.
                    || (root == nullptr && !stored.subtype->type_mark.target.valid()
                        && equal_ignoring_ascii_case(simple_name_of(stored.subtype->type_mark.spelling),
                            "text")))) {
                report("FSIM-ELAB-VHFILE-016",
                    "VHDL interface object '" + stored.name
                        + "' cannot be of a file type",
                    stored.source);
            }
        }
        // IEEE 1076-2008 5.2.1: the bounds of a range constraint are
        // expressions, so a type, label or design-unit name is not a bound.
        if (name_check_) {
            for (const auto& type : specialization_.design().vhdl_hir.types()) {
                const auto declaration = specialization_.find_declaration(type.declaration);
                if (!declaration || declaration->vhdl == nullptr
                    || !selected_scope(declaration->vhdl->scope) || !type.scalar_range) {
                    continue;
                }
                for (const auto bound : { type.scalar_range->left_expression,
                         type.scalar_range->right_expression }) {
                    if (bound) {
                        validate_value_name(*bound);
                    }
                }
            }
        }
        // IEEE 1076-2008 6.4.2.1, 6.5.2: only variables are of access types.
        for (const auto& stored :
            specialization_.design().vhdl_hir.declarations()) {
            using Form = semantic::vhdl::DeclarationForm;
            // Subprogram formals are kept as ports of their object class;
            // a variable-class formal may be of an access type.
            if (!stored.subtype || !selected_scope(stored.scope)
                || (stored.form != Form::signal && stored.form != Form::constant
                    && !(stored.form == Form::port
                        && stored.object_class == semantic::vhdl::ObjectClass::signal)
                    && stored.form != Form::generic_constant)) {
                continue;
            }
            const auto* definition = root_definition(*stored.subtype);
            if (definition != nullptr && definition->form == TypeForm::access) {
                report("FSIM-ELAB-VHACCESS-030",
                    "only a VHDL variable can be of an access type",
                    stored.source);
            }
        }
        if (name_check_) {
            using Form = semantic::vhdl::DeclarationForm;
            for (const auto& stored :
                specialization_.design().vhdl_hir.declarations()) {
                // Interface defaults are checked only when used.
                if (stored.initializer && selected_scope(stored.scope)
                    && (stored.form == Form::signal || stored.form == Form::variable
                        || stored.form == Form::constant)) {
                    validate_value_name(*stored.initializer);
                }
                // The bounds of an object's index constraint are values.
                if (stored.subtype && selected_scope(stored.scope)
                    && (stored.form == Form::signal || stored.form == Form::variable
                        || stored.form == Form::constant)) {
                    for (const auto& range : stored.subtype->constraints) {
                        for (const auto bound : { range.left_expression, range.right_expression }) {
                            if (bound) {
                                validate_value_name(*bound);
                            }
                        }
                    }
                }
            }
        }
        // Types declared in processes and subprograms are not object types of
        // the unit, but their range bounds are still checked.
        for (const auto& stored :
            specialization_.design().vhdl_hir.declarations()) {
            if (!stored.declared_type || !selected_scope(stored.scope)
                || relevant_types_.contains(stored.declared_type->value())) {
                continue;
            }
            const auto type = specialization_.find_type(*stored.declared_type);
            if (type && type->vhdl != nullptr) {
                validate_type_bounds(*type->vhdl);
            }
        }
        struct InactiveGenerateBranch {
            semantic::ScopeId body_scope;
            semantic::ScopeId active_else_scope;
        };
        std::vector<InactiveGenerateBranch> inactive_generate_branches;
        std::unordered_set<std::uint32_t> generate_condition_expressions;
        // A generate condition can share its branch scope, but remains active
        // because it decides whether that branch exists.
        const auto collect_expression_tree = [&](const auto& self,
                                                 const semantic::ExpressionId id)
            -> void {
            if (!generate_condition_expressions.insert(id.value()).second) {
                return;
            }
            const auto expression = specialization_.find_expression(id);
            if (!expression || expression->vhdl == nullptr) {
                return;
            }
            for (const auto operand : expression->vhdl->operands) {
                self(self, operand);
            }
            for (const auto& association : expression->vhdl->associations) {
                for (const auto choice : association.choices) {
                    self(self, choice);
                }
                self(self, association.value);
            }
        };
        const auto is_within_scope = [&](semantic::ScopeId scope,
                                         const semantic::ScopeId ancestor) {
            const auto& scopes
                = specialization_.design().semantics.scopes();
            std::size_t remaining = scopes.size();
            while (scope.valid() && scope.value() < scopes.size()
                && remaining > 0U) {
                --remaining;
                if (scope == ancestor) {
                    return true;
                }
                const auto& parent = scopes[scope.value()].parent;
                if (!parent) {
                    return false;
                }
                scope = *parent;
            }
            return false;
        };
        const auto inactive_generate_scope = [&](const semantic::ScopeId scope) {
            return std::ranges::any_of(
                inactive_generate_branches,
                [&](const InactiveGenerateBranch& branch) {
                    return is_within_scope(scope, branch.body_scope)
                        && (!branch.active_else_scope.valid()
                            || !is_within_scope(
                                scope, branch.active_else_scope));
                });
        };
        const auto collect_inactive_generate_branches =
            [&](const auto& self,
                const std::span<const semantic::vhdl::GenerateRegion> regions)
                -> void {
            for (const auto& region : regions) {
                if (region.kind == semantic::vhdl::GenerateKind::conditional
                    && region.condition) {
                    const auto condition
                        = specialization_.evaluate_integral_expression(
                            *region.condition);
                    if (condition && *condition == 0) {
                        if (!inactive_generate_scope(region.scope)) {
                            collect_expression_tree(
                                collect_expression_tree, *region.condition);
                        }
                        auto else_scope = semantic::ScopeId { };
                        const auto else_label
                            = region.alternative_label.empty()
                            ? region.label + ".else"
                            : region.alternative_label;
                        const auto alternative = std::ranges::find_if(
                            region.nested,
                            [&](const semantic::vhdl::GenerateRegion& child) {
                                return equal_ignoring_ascii_case(child.label, else_label);
                            });
                        if (alternative != region.nested.end()) {
                            else_scope = alternative->scope;
                        }
                        if (region.scope.valid()) {
                            // The retained else arm is represented as a nested
                            // scope under the then arm in VHDL HIR.
                            inactive_generate_branches.push_back(
                                { region.scope, else_scope });
                        }
                    }
                }
                self(self, region.nested);
            }
        };
        collect_inactive_generate_branches(
            collect_inactive_generate_branches, architecture_.generates);
        // Both sequential loops and iterative generate regions have discrete
        // parameter ranges. Share the existing bound/type checks so their
        // rules cannot drift apart.
        const auto nondiscrete_range_bound = [&](const auto& self,
                                                 const semantic::ExpressionId bound,
                                                 const std::size_t depth) -> bool {
            const auto kind = operand_type(bound).kind;
            if (kind == OperandClass::floating
                || (depth == 0U && kind == OperandClass::physical)) {
                return true;
            }
            const auto found = specialization_.find_expression(bound);
            if (!found || found->vhdl == nullptr || depth > 4U) {
                return false;
            }
            const auto& value = *found->vhdl;
            using Kind = semantic::vhdl::ExpressionKind;
            // A conversion to a floating or physical type (`real(i)`).
            if (value.kind == Kind::call && value.operands.size() == 1U
                && !value.text.starts_with('@')
                && value.text.find('\'') == std::string::npos) {
                // STD.STANDARD's REAL and TIME are not retained.
                if (equal_ignoring_ascii_case(value.text, "real")) {
                    return true;
                }
                if (equal_ignoring_ascii_case(value.text, "time")) {
                    return depth == 0U;
                }
                if (const auto type = named_type(value.text, value.scope)) {
                    SubtypeIndication subtype;
                    subtype.type_mark.spelling = value.text;
                    subtype.type_mark.target = *type;
                    const auto converted = subtype_operand_type(subtype, 1U).kind;
                    return converted == OperandClass::floating
                        || (depth == 0U && converted == OperandClass::physical);
                }
            }
            // A bare unit name of a physical type (`ps`).
            if (depth == 0U && value.kind == Kind::name
                && value.text.find('.') == std::string::npos
                && (!value.referenced_name || !value.referenced_name->selected)) {
                for (const std::string_view unit :
                    { "fs", "ps", "ns", "us", "ms", "sec", "min", "hr" }) {
                    if (equal_ignoring_ascii_case(unit, value.text)) {
                        return true;
                    }
                }
                for (const auto& type : specialization_.design().vhdl_hir.types()) {
                    if (std::ranges::any_of(type.physical_units, [&](const auto& unit) {
                            return equal_ignoring_ascii_case(unit.name, value.text);
                        })) {
                        return true;
                    }
                }
            }
            return value.kind == Kind::binary
                && (value.text == "+" || value.text == "-" || value.text == "*"
                    || value.text == "/" || value.text == "**")
                && std::ranges::any_of(value.operands,
                    [&](const semantic::ExpressionId operand) {
                        return self(self, operand, depth + 1U);
                    });
        };
        const auto validate_range_bounds = [&](const auto& initial,
                                               const auto& limit,
                                               const auto& source) {
            if ((initial
                    && nondiscrete_range_bound(
                        nondiscrete_range_bound, *initial, 0U))
                || (limit
                    && nondiscrete_range_bound(
                        nondiscrete_range_bound, *limit, 0U))) {
                report("FSIM-ELAB-VHLOOP-001",
                    "a VHDL loop parameter range must be discrete",
                    source);
            }
            if (!initial || !limit) {
                return;
            }
            const auto left = operand_type(*initial);
            const auto right = operand_type(*limit);
            if (left.kind != OperandClass::unknown
                && right.kind != OperandClass::unknown
                && types_conflict(left, right)) {
                report("FSIM-ELAB-VHRANGE-001",
                    "the bounds of a VHDL range must have the same type",
                    source);
            }
        };
        std::unordered_set<std::uint32_t> discrete_range_attributes;
        std::unordered_set<std::uint32_t> negated_integer_minimum_literals;
        for (const auto& stored :
            specialization_.design().vhdl_hir.statements()) {
            const auto effective = specialization_.find_statement(
                stored.id);
            if (!effective || effective->vhdl == nullptr
                || !selected_scope(effective->vhdl->scope)) {
                continue;
            }
            const auto& statement = *effective->vhdl;
            validate_procedure_call(statement);
            validate_case(statement);
            if (statement.kind == semantic::vhdl::StatementKind::return_statement
                && statement.value) {
                validate_return_value(statement);
            }
            // IEEE 1076-2008 10.2, 10.5.2.1: a timeout and a waveform delay are
            // of type TIME.
            const auto validate_delay = [&](const semantic::vhdl::Delay& delay) {
                for (const auto* value : { &delay.primary }) {
                    if (!value->expression) {
                        continue;
                    }
                    const auto type = operand_type(*value->expression);
                    if (type.kind == OperandClass::unknown) {
                        continue;
                    }
                    if (type.kind != OperandClass::physical
                        || (!type.root.empty() && type.root != "time")) {
                        report("FSIM-ELAB-VHTIME-004",
                            "a VHDL delay or timeout must be of type TIME",
                            value->source);
                    }
                }
            };
            if (statement.delay) {
                validate_delay(*statement.delay);
            }
            // 10.5.2.1: the static delays of a waveform's elements increase.
            {
                std::optional<long double> previous;
                for (const auto& element : statement.waveform) {
                    if (!element.delay || element.delay->primary.expression
                        || element.delay->primary.unit.empty()) {
                        previous.reset();
                        continue;
                    }
                    const auto& value = element.delay->primary;
                    long double scale { };
                    for (const auto& [unit, factor] :
                        elaboration_detail::vhdl_time_units) {
                        if (equal_ignoring_ascii_case(value.unit, unit)) {
                            scale = static_cast<long double>(factor);
                        }
                    }
                    if (scale == 0.0L || value.divisor == 0U) {
                        previous.reset();
                        continue;
                    }
                    const auto time = static_cast<long double>(value.magnitude) * scale
                        / static_cast<long double>(value.divisor);
                    if (previous && time <= *previous) {
                        report("FSIM-ELAB-VHWAVE-001",
                            "the delays of the elements of a VHDL waveform must increase",
                            element.source.valid() ? element.source : statement.source);
                        break;
                    }
                    previous = time;
                }
            }
            for (const auto& element : statement.waveform) {
                if (element.delay) {
                    validate_delay(*element.delay);
                }
            }
            // IEEE 1076-1993 1.1.1.2: a port of mode out is not read before
            // VHDL-2008 (attributes are checked separately).
            if (before_2008()) {
                const auto read_ids = [&] {
                    std::vector<semantic::ExpressionId> ids;
                    if (statement.value) {
                        ids.push_back(*statement.value);
                    }
                    if (statement.condition) {
                        ids.push_back(*statement.condition);
                    }
                    for (const auto& element : statement.waveform) {
                        if (!element.disconnect && element.value.valid()) {
                            ids.push_back(element.value);
                        }
                    }
                    return ids;
                }();
                for (const auto id : read_ids) {
                    if (reads_out_port(id, 0U)) {
                        report("FSIM-ELAB-VHPORT-003",
                            "a port of mode out cannot be read before VHDL-2008",
                            statement.source);
                        break;
                    }
                }
            }
            if (name_check_) {
                if (statement.value) {
                    validate_value_name(*statement.value);
                }
                if (statement.condition) {
                    validate_value_name(*statement.condition);
                }
                for (const auto& element : statement.waveform) {
                    if (!element.disconnect && element.value.valid()) {
                        validate_value_name(element.value);
                    }
                    if (element.delay && element.delay->primary.expression) {
                        validate_value_name(*element.delay->primary.expression);
                    }
                }
                // Case and selected-assignment choices are values too.
                for (const auto& alternative : statement.alternatives) {
                    for (const auto choice : alternative.choices) {
                        validate_value_name(choice);
                    }
                }
            }
            if (statement.kind
                    == semantic::vhdl::StatementKind::variable_assignment
                && statement.target && statement.value) {
                validate_assignment_type(
                    *statement.target, *statement.value, statement.source);
                if (name_check_) {
                    validate_value_name(*statement.target);
                }
                validate_variable_target(*statement.target, statement.source);
                validate_slice_assignment(*statement.target, *statement.value,
                    statement.source);
                if (const auto target = specialization_.find_expression(*statement.target);
                    target && target->vhdl != nullptr
                    && target->vhdl->kind == semantic::vhdl::ExpressionKind::name
                    && target->vhdl->referenced_name
                    && target->vhdl->referenced_name->selected) {
                    const auto declaration = specialization_.find_declaration(
                        *target->vhdl->referenced_name->selected);
                    if (declaration && declaration->vhdl != nullptr
                        && declaration->vhdl->subtype) {
                        validate_aggregate(*statement.value, *declaration->vhdl->subtype);
                    }
                }
            }
            // A null waveform element disconnects a driver, so its target
            // must be a guarded signal (IEEE 1076-2008 10.5.2.2).
            if (statement.kind
                    == semantic::vhdl::StatementKind::signal_assignment
                && statement.target && !statement.guarded_assignment
                && std::ranges::any_of(statement.waveform,
                    [](const semantic::vhdl::WaveformElement& element) {
                        return element.disconnect;
                    })) {
                const auto target = specialization_.find_expression(
                    *statement.target);
                const auto declaration = target && target->vhdl != nullptr
                        && target->vhdl->kind
                            == semantic::vhdl::ExpressionKind::name
                        && target->vhdl->referenced_name
                        && target->vhdl->referenced_name->selected
                    ? specialization_.find_declaration(
                          *target->vhdl->referenced_name->selected)
                    : std::nullopt;
                if (declaration && declaration->vhdl != nullptr
                    && declaration->vhdl->form
                        == semantic::vhdl::DeclarationForm::signal
                    && declaration->vhdl->guarded_kind == 0U) {
                    report("FSIM-VHDL-SEM-129",
                        "a null waveform element targets the unguarded "
                        "signal '" + declaration->vhdl->name + "'",
                        statement.source);
                }
            }
            if (statement.kind
                    == semantic::vhdl::StatementKind::signal_assignment
                && statement.target) {
                // An aggregate signal value is context-determined by its
                // target subtype just like an aggregate variable value.
                const auto direct_range_selection = [&] {
                    const auto target_expression
                        = specialization_.find_expression(*statement.target);
                    if (!target_expression
                        || target_expression->vhdl == nullptr
                        || target_expression->vhdl->operands.empty()) {
                        return false;
                    }
                    if (!slice_bounds(*target_expression->vhdl)) {
                        return false;
                    }
                    const std::optional prefix_id
                        = target_expression->vhdl->operands.front();
                    const auto prefix
                        = specialization_.find_expression(*prefix_id);
                    if (!prefix || prefix->vhdl == nullptr) {
                        return false;
                    }
                    // array_selection(index) can recover a subtype from its
                    // prefix even when recursion did not prove an object.
                    // Require the prefix chain to end at a selected object;
                    // call syntax must also be resolved away from callables.
                    const auto has_object_root = [&] {
                        auto current_id = *prefix_id;
                        std::unordered_set<std::uint32_t> seen;
                        for (std::size_t depth { }; depth < 64U; ++depth) {
                            if (!seen.insert(current_id.value()).second) {
                                return false;
                            }
                            const auto current
                                = specialization_.find_expression(current_id);
                            if (!current || current->vhdl == nullptr) {
                                return false;
                            }
                            const auto& current_value = *current->vhdl;
                            if (current_value.kind
                                == semantic::vhdl::ExpressionKind::index) {
                                if (current_value.operands.empty()) {
                                    return false;
                                }
                                current_id = current_value.operands.front();
                                continue;
                            }
                            if (current_value.kind
                                    != semantic::vhdl::ExpressionKind::name
                                && current_value.kind
                                    != semantic::vhdl::ExpressionKind::call) {
                                return false;
                            }
                            if (!current_value.referenced_name
                                || !current_value.referenced_name->selected) {
                                return false;
                            }
                            if (current_value.kind
                                == semantic::vhdl::ExpressionKind::call) {
                                if (current_value.text.empty()
                                    || current_value.text.front() == '\''
                                    || current_value.text.front() == '@') {
                                    return false;
                                }
                                const semantic::CompiledDesignResolver resolver {
                                    specialization_ };
                                const auto callable_resolution
                                    = resolver.resolve_vhdl_callables(
                                        *current_value.referenced_name,
                                        current_value.scope);
                                if (callable_resolution.status
                                    != semantic::CompiledResolutionStatus::not_found) {
                                    return false;
                                }
                            }
                            const auto declaration
                                = specialization_.find_declaration(
                                    *current_value.referenced_name->selected);
                            if (!declaration || declaration->vhdl == nullptr
                                || !declaration->vhdl->subtype) {
                                return false;
                            }
                            return semantic::vhdl::is_value_object_form(
                                       declaration->vhdl->form)
                                || declaration->vhdl->form
                                    == semantic::vhdl::DeclarationForm::alias;
                        }
                        return false;
                    }();
                    if (!has_object_root) {
                        return false;
                    }
                    std::optional<SubtypeIndication> prefix_subtype;
                    if (prefix->vhdl->kind
                        == semantic::vhdl::ExpressionKind::name) {
                        if (!prefix->vhdl->referenced_name
                            || !prefix->vhdl->referenced_name->selected) {
                            return false;
                        }
                        const auto declaration
                            = specialization_.find_declaration(
                                *prefix->vhdl->referenced_name->selected);
                        if (!declaration || declaration->vhdl == nullptr) {
                            return false;
                        }
                        using Form = semantic::vhdl::DeclarationForm;
                        const auto form = declaration->vhdl->form;
                        if (form != Form::generic_constant
                            && form != Form::port
                            && form != Form::signal
                            && form != Form::constant
                            && form != Form::variable
                            && form != Form::alias) {
                            return false;
                        }
                        prefix_subtype = expression_subtype(*prefix_id);
                    } else if (prefix->vhdl->kind
                            == semantic::vhdl::ExpressionKind::call
                        || prefix->vhdl->kind
                            == semantic::vhdl::ExpressionKind::index) {
                        // A selected array object such as rows(i) may be the
                        // parent of a final slice. Require a complete object
                        // selection whose result remains an array; do not
                        // treat a function result or an incomplete index
                        // group as an assignable parent.
                        const auto selection = array_selection(*prefix_id);
                        if (!selection || selection->slice_result
                            || selection->invalid_index_group) {
                            return false;
                        }
                        const auto selected_subtype
                            = selection_result_subtype(*selection);
                        if (!selected_subtype) {
                            return false;
                        }
                        prefix_subtype = *selected_subtype;
                    } else {
                        return false;
                    }
                    if (!prefix_subtype) {
                        return false;
                    }
                    return profile(*prefix_subtype).kind
                        == ProfileKind::array;
                }();
                auto target_subtype = expression_subtype(
                    *statement.target);
                if (direct_range_selection) {
                    if (auto slice_context = slice_assignment_context(
                            *statement.target)) {
                        target_subtype = std::move(slice_context);
                    }
                }
                if (target_subtype) {
                    for (const auto& element : statement.waveform) {
                        if (element.disconnect || !element.value.valid()) {
                            continue;
                        }
                        const auto value = specialization_.find_expression(
                            element.value);
                        if (value && value->vhdl != nullptr
                            && value->vhdl->kind
                                == semantic::vhdl::ExpressionKind::aggregate) {
                            validate_aggregate(
                                element.value, *target_subtype);
                        }
                    }
                }
                // IEEE 1076-2008 3.2.4: entity statements are passive.
                // A subprogram declared in the entity is not an entity
                // statement.
                if (const auto& all_scopes = specialization_.design().semantics.scopes();
                    statement.scope.valid() && statement.scope.value() < all_scopes.size()
                    && all_scopes[statement.scope.value()].unit == entity_.id
                    && entity_.id != architecture_.id
                    && !within_subprogram(statement.scope)) {
                    report("FSIM-ELAB-VHPASSIVE-001",
                        "a VHDL entity statement must be passive (it assigns a signal)",
                        statement.source);
                }
                validate_signal_target(*statement.target, statement.source);
                for (const auto& element : statement.waveform) {
                    if (!element.disconnect && element.value.valid()) {
                        validate_slice_assignment(*statement.target, element.value,
                            statement.source);
                    }
                }
                for (const auto& element : statement.waveform) {
                    if (!element.disconnect && element.value.valid()) {
                        validate_assignment_type(
                            *statement.target, element.value, statement.source);
                    }
                }
                if (name_check_) {
                    validate_value_name(*statement.target);
                }
            }
            if (statement.kind == semantic::vhdl::StatementKind::loop) {
                validate_range_bounds(
                    statement.loop_initial,
                    statement.loop_limit,
                    statement.source);
            }
            if (statement.kind == semantic::vhdl::StatementKind::loop
                && statement.loop_initial && !statement.loop_limit) {
                discrete_range_attributes.insert(
                    statement.loop_initial->value());
            }
        }
        const auto validate_generate_ranges = [&](const auto& self,
                const std::span<const semantic::vhdl::GenerateRegion> regions)
            -> void {
            for (const auto& region : regions) {
                if (region.kind == semantic::vhdl::GenerateKind::iterative
                    && region.initial && region.condition) {
                    const auto condition = specialization_.find_expression(
                        *region.condition);
                    if (condition && condition->vhdl != nullptr
                        && condition->vhdl->kind
                            == semantic::vhdl::ExpressionKind::binary
                        && condition->vhdl->operands.size() == 2U
                        && (condition->vhdl->text == "<="
                            || condition->vhdl->text == ">=")) {
                        // The parser stores the range's upper bound as the
                        // right operand of the iterator comparison.
                        const auto limit = std::optional {
                            condition->vhdl->operands[1U] };
                        validate_range_bounds(
                            region.initial, limit, region.source);
                    }
                }
                self(self, region.nested);
            }
        };
        validate_generate_ranges(
            validate_generate_ranges, entity_.generates);
        validate_generate_ranges(
            validate_generate_ranges, architecture_.generates);
        const auto mark_aggregate_range_choice = [&](const auto& self,
                                                     const semantic::ExpressionId id)
            -> void {
            const auto choice = specialization_.find_expression(id);
            if (!choice || choice->vhdl == nullptr) {
                return;
            }
            const auto& source = *choice->vhdl;
            if (source.kind == semantic::vhdl::ExpressionKind::binary
                && source.text == "|" && source.operands.size() == 2U) {
                self(self, source.operands[0]);
                self(self, source.operands[1]);
                return;
            }
            if (source.kind == semantic::vhdl::ExpressionKind::call
                && (source.text == "'range"
                    || source.text == "'reverse_range")) {
                discrete_range_attributes.insert(id.value());
            }
        };
        for (const auto& stored :
            specialization_.design().vhdl_hir.expressions()) {
            const auto effective = specialization_.find_expression(
                stored.id);
            if (!effective || effective->vhdl == nullptr
                || effective->vhdl->kind
                    != semantic::vhdl::ExpressionKind::aggregate
                || !selected_scope(effective->vhdl->scope)) {
                continue;
            }
            for (const auto& association :
                effective->vhdl->associations) {
                for (const auto choice : association.choices) {
                    mark_aggregate_range_choice(
                        mark_aggregate_range_choice, choice);
                }
            }
        }
        // A range attribute has a discrete-range context when it is used as
        // an array selector or as the prefix of scalar 'LEFT/'RIGHT. Keep
        // naked range values in scalar contexts outside this set so
        // VHARRAYATTR-003 still applies; the selector validator checks the
        // index subtype and array rank.
        for (const auto& stored :
            specialization_.design().vhdl_hir.expressions()) {
            const auto effective = specialization_.find_expression(
                stored.id);
            if (!effective || effective->vhdl == nullptr) {
                continue;
            }
            const auto& expression = *effective->vhdl;
            if (!selected_scope(expression.scope)) {
                continue;
            }
            if (expression.kind
                    != semantic::vhdl::ExpressionKind::call
                && expression.kind
                    != semantic::vhdl::ExpressionKind::index
                && expression.kind
                    != semantic::vhdl::ExpressionKind::slice) {
                continue;
            }
            if ((expression.text == "'left"
                    || expression.text == "'right")
                && !expression.operands.empty()) {
                const auto range = specialization_.find_expression(
                    expression.operands.front());
                if (range && range->vhdl != nullptr
                    && range->vhdl->kind
                        == semantic::vhdl::ExpressionKind::call
                    && (range->vhdl->text == "'range"
                        || range->vhdl->text == "'reverse_range")
                    && !range->vhdl->operands.empty()) {
                    const auto range_prefix = expression_subtype(
                        range->vhdl->operands.front());
                    if (range_prefix) {
                        if (profile(*range_prefix).kind
                            == ProfileKind::array) {
                            discrete_range_attributes.insert(
                                range->vhdl->id.value());
                        }
                    }
                }
            }
            const auto selection = array_selection(expression.id);
            if (!selection || selection->invalid_index_group
                || selection->selectors.size() != 1U) {
                continue;
            }
            const auto selector = specialization_.find_expression(
                selection->selectors.front());
            if (!selector || selector->vhdl == nullptr
                || selector->vhdl->kind
                    != semantic::vhdl::ExpressionKind::call
                || (selector->vhdl->text != "'range"
                    && selector->vhdl->text != "'reverse_range")
                || selector->vhdl->operands.empty()) {
                continue;
            }
            const auto selector_subtype = expression_subtype(
                selector->vhdl->operands.front());
            if (selector_subtype) {
                const auto selector_profile = profile(
                    *selector_subtype);
                if (selector_profile.kind != ProfileKind::array) {
                    continue;
                }
                discrete_range_attributes.insert(
                    selector->vhdl->id.value());
            }
        }
        if (architecture_.standard != "2019") {
            for (const auto& stored :
                specialization_.design().vhdl_hir.expressions()) {
                const auto effective = specialization_.find_expression(
                    stored.id);
                if (!effective || effective->vhdl == nullptr
                    || !selected_scope(effective->vhdl->scope)) {
                    continue;
                }
                const auto& expression = *effective->vhdl;
                if (expression.kind
                        != semantic::vhdl::ExpressionKind::unary
                    || expression.text != "-"
                    || expression.operands.size() != 1U) {
                    continue;
                }
                const auto operand = specialization_.find_expression(
                    expression.operands.front());
                if (!operand || operand->vhdl == nullptr
                    || operand->vhdl->kind
                        != semantic::vhdl::ExpressionKind::integer_literal) {
                    continue;
                }
                const auto magnitude = bound(
                    std::nullopt, expression.operands.front());
                if (magnitude
                    && *magnitude
                        == -static_cast<std::int64_t>(
                            std::numeric_limits<std::int32_t>::min())) {
                    negated_integer_minimum_literals.insert(
                        expression.operands.front().value());
                }
            }
        }
        // A linkage port is read or updated only as an actual of a port
        // association; its signal attributes are checked separately (IEEE
        // 1076-1993 4.3.2).
        std::unordered_set<std::uint32_t> linkage_uses;
        for (const auto& instance : specialization_.design().vhdl_hir.instances()) {
            for (const auto& association : instance.port_map) {
                if (association.expression) {
                    linkage_uses.insert(association.expression->value());
                }
            }
        }
        for (const auto& stored : specialization_.design().vhdl_hir.expressions()) {
            if (stored.kind == semantic::vhdl::ExpressionKind::call
                && stored.text.starts_with('\'') && !stored.operands.empty()) {
                linkage_uses.insert(stored.operands.front().value());
            }
        }
        for (const auto& stored : specialization_.design().vhdl_hir.expressions()) {
            const auto effective = specialization_.find_expression(stored.id);
            if (!effective || effective->vhdl == nullptr
                || effective->vhdl->kind != semantic::vhdl::ExpressionKind::name
                || !selected_scope(effective->vhdl->scope)
                || linkage_uses.contains(stored.id.value())) {
                continue;
            }
            if (linkage_port_name(stored.id)) {
                report("FSIM-ELAB-VHPORT-004",
                    "a port of mode linkage cannot be read or updated",
                    effective->vhdl->source);
            }
        }
        // Index expressions that are the prefix of another index: a name
        // with several indices (`k(5,5)`) is retained as nested indices.
        std::unordered_set<std::uint32_t> inner_indices;
        for (const auto& stored :
            specialization_.design().vhdl_hir.expressions()) {
            if (stored.kind == semantic::vhdl::ExpressionKind::index
                && !stored.operands.empty()) {
                inner_indices.insert(stored.operands.front().value());
            }
        }
        for (const auto& stored :
            specialization_.design().vhdl_hir.expressions()) {
            const auto effective = specialization_.find_expression(
                stored.id);
            if (!effective || effective->vhdl == nullptr
                || !selected_scope(effective->vhdl->scope)) {
                continue;
            }
            const auto& expression = *effective->vhdl;
            if (expression.kind == semantic::vhdl::ExpressionKind::index
                && !inner_indices.contains(expression.id.value())) {
                validate_index_chain(expression.id);
            }
            if (expression.kind == semantic::vhdl::ExpressionKind::slice
                && expression.operands.size() == 3U) {
                validate_slice_bounds(expression.operands[0],
                    expression.operands[1], expression.operands[2],
                    expression.source);
            }
            if (architecture_.standard != "2019"
                && expression.kind
                    == semantic::vhdl::ExpressionKind::integer_literal
                && !negated_integer_minimum_literals.contains(
                    expression.id.value())
                && !equal_ignoring_ascii_case(
                    simple_name_of(expression.nominal_type), "time")) {
                const auto value = bound(
                    std::nullopt, expression.id);
                if (value
                    && (*value
                            < std::numeric_limits<std::int32_t>::min()
                        || *value
                            > std::numeric_limits<std::int32_t>::max())) {
                    report(
                        "FSIM-ELAB-INTEGER-002",
                        "VHDL integer literal lies outside the portable "
                            "signed 32-bit range before VHDL-2019",
                        expression.source);
                }
            }
            if (expression.kind
                == semantic::vhdl::ExpressionKind::call) {
                validate_array_attribute(expression,
                    discrete_range_attributes.contains(
                        expression.id.value()));
            }
            if (expression.kind
                    == semantic::vhdl::ExpressionKind::call
                || expression.kind
                    == semantic::vhdl::ExpressionKind::index
                || expression.kind
                    == semantic::vhdl::ExpressionKind::slice) {
                if (!generate_condition_expressions.contains(
                        expression.id.value())
                    && !inactive_generate_scope(expression.scope)) {
                    validate_selection(expression.id);
                }
            }
            if (operator_check_) {
                validate_predefined_operator(expression);
            }
            validate_formal_signal_attribute(expression);
            validate_out_port_attribute(expression);
            // 8.3: `.all` designates the object of an access value.
            if (expression.kind == semantic::vhdl::ExpressionKind::call
                && expression.text == "@vhdl-dereference" && expression.operands.size() == 1U) {
                const auto prefix = operand_type(expression.operands.front());
                if (prefix.kind != OperandClass::unknown && prefix.kind != OperandClass::access
                    && prefix.kind != OperandClass::null) {
                    report("FSIM-ELAB-VHACCESS-033",
                        "the prefix of a VHDL .all name is not of an access type",
                        expression.source);
                }
            }
            // 9.3.3.3: an aggregate takes its type from the context, which two
            // aggregate operands of a relational operator do not give.
            const auto is_aggregate_operand
                = [&](const semantic::ExpressionId id) {
                const auto operand = specialization_.find_expression(id);
                return operand && operand->vhdl != nullptr
                    && operand->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate;
            };
            if (expression.kind == semantic::vhdl::ExpressionKind::binary
                && expression.operands.size() == 2U
                && (expression.text == "=" || expression.text == "/="
                    || expression.text == "<" || expression.text == "<="
                    || expression.text == ">" || expression.text == ">=")
                && is_aggregate_operand(expression.operands[0])
                    != is_aggregate_operand(expression.operands[1])) {
                const auto aggregate_index = is_aggregate_operand(
                    expression.operands[0])
                    ? 0U
                    : 1U;
                const auto sibling_index = 1U - aggregate_index;
                const auto aggregate = specialization_.find_expression(
                    expression.operands[aggregate_index]);
                auto context = expression_subtype(
                    expression.operands[sibling_index]);
                if (aggregate && aggregate->vhdl != nullptr && context) {
                    // The sibling gives the aggregate its type but not its
                    // bounds: relational operands may differ in length
                    // (IEEE 1076-2008 9.2.3), and a named aggregate takes
                    // its bounds from its choices (9.3.3.3).
                    {
                        const auto predefined
                            = !context->type_mark.target.valid()
                            ? predefined_array_identity(
                                  *context, expression.scope)
                            : std::nullopt;
                        auto scoped_context = *context;
                        if (!scoped_context.type_mark.target.valid()
                            && (expression.scope.valid()
                                || scoped_context.type_mark.spelling
                                        .find('.')
                                    != std::string::npos)) {
                            if (const auto resolved = named_type(
                                    scoped_context.type_mark.spelling,
                                    expression.scope)) {
                                scoped_context.type_mark.target = *resolved;
                            }
                        }
                        const auto* root
                            = scoped_context.type_mark.target.valid()
                            ? root_definition(scoped_context)
                            : nullptr;
                        if (predefined) {
                            context->type_mark.spelling = *predefined;
                            context->type_mark.target
                                = semantic::TypeId { };
                            context->constraints.clear();
                            context->executable_width.reset();
                        } else if (root != nullptr
                            && root->form
                                == semantic::vhdl::TypeForm::array) {
                            context->type_mark.spelling = root->name;
                            context->type_mark.target = root->id;
                            context->constraints.clear();
                            context->executable_width.reset();
                        }
                    }
                    validate_aggregate(
                        expression.operands[aggregate_index], *context);
                }
            }
            if (expression.kind == semantic::vhdl::ExpressionKind::binary
                && expression.operands.size() == 2U
                && (expression.text == "=" || expression.text == "/="
                    || expression.text == "<" || expression.text == "<="
                    || expression.text == ">" || expression.text == ">=")
                && std::ranges::all_of(expression.operands, [&](const semantic::ExpressionId id) {
                       const auto operand = specialization_.find_expression(id);
                       return operand && operand->vhdl != nullptr
                           && operand->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate;
                   })
                && std::ranges::none_of(expression.operands,
                    [&](const semantic::ExpressionId id) {
                        return expression_subtype(id).has_value();
                    })) {
                report("FSIM-ELAB-VHAGG-020",
                    "the type of a VHDL aggregate operand of a relational operator "
                    "cannot be determined",
                    expression.source);
            }
            // IEEE 1076-2008 9.2.7: division by a static zero is an error.
            if (expression.kind == semantic::vhdl::ExpressionKind::binary
                && expression.operands.size() == 2U
                && (expression.text == "/" || expression.text == "mod"
                    || expression.text == "rem")
                && predefined_operator(expression)) {
                const auto divisor = specialization_.find_expression(expression.operands.back());
                const bool zero = divisor && divisor->vhdl != nullptr
                    && ((divisor->vhdl->kind == semantic::vhdl::ExpressionKind::integer_literal
                            && specialization_.evaluate_integral_expression(
                                   expression.operands.back()) == std::optional<std::int64_t> { 0 })
                        || (divisor->vhdl->kind == semantic::vhdl::ExpressionKind::real_literal
                            && std::strtod(divisor->vhdl->text.c_str(), nullptr) == 0.0));
                // Only a static division is evaluated at analysis; otherwise
                // the error occurs if and when the operation is evaluated.
                const auto dividend = specialization_.find_expression(expression.operands.front());
                const bool static_dividend = dividend && dividend->vhdl != nullptr
                    && (dividend->vhdl->kind == semantic::vhdl::ExpressionKind::real_literal
                        || specialization_.evaluate_integral_expression(
                            expression.operands.front()).has_value());
                if (zero && static_dividend) {
                    report("FSIM-ELAB-VHOPER-002",
                        "a VHDL division by the static value zero",
                        expression.source);
                }
            }
            // IEEE 1076-2008 9.2.3: `'0' = '1'` compares two literals that are
            // both BIT and CHARACTER values, so the operator is ambiguous.
            if (expression.kind == semantic::vhdl::ExpressionKind::binary
                && expression.operands.size() == 2U && predefined_operator(expression)
                && (expression.text == "=" || expression.text == "/=" || expression.text == "<"
                    || expression.text == "<=" || expression.text == ">"
                    || expression.text == ">=")
                && std::ranges::all_of(expression.operands, [&](const semantic::ExpressionId id) {
                       const auto operand = specialization_.find_expression(id);
                       return operand && operand->vhdl != nullptr
                           && operand->vhdl->kind == semantic::vhdl::ExpressionKind::logic_literal
                           && (operand->vhdl->text == "'0'" || operand->vhdl->text == "'1'");
                   })
                && std::ranges::none_of(expression.operands,
                    [&](const semantic::ExpressionId id) {
                        return expression_subtype(id).has_value();
                    })) {
                report("FSIM-ELAB-VHOPER-003",
                    "the operand type of a VHDL comparison of two character literals is "
                    "ambiguous",
                    expression.source);
            }
            validate_out_access_dereference(expression);
            validate_conversion_and_qualification(expression);
            validate_allocator(expression);
            validate_call_arity(expression);
            if (expression.kind == semantic::vhdl::ExpressionKind::index
                && expression.operands.size() >= 2U) {
                if (const auto prefix = specialization_.find_expression(expression.operands.front());
                    prefix && prefix->vhdl != nullptr
                    && prefix->vhdl->kind == semantic::vhdl::ExpressionKind::name) {
                    (void)called_before_declaration(*prefix->vhdl, expression.source);
                }
            }
            if (name_check_
                && (expression.kind == semantic::vhdl::ExpressionKind::binary
                    || expression.kind == semantic::vhdl::ExpressionKind::unary
                    || (expression.kind == semantic::vhdl::ExpressionKind::call
                        && expression.operands.size() == 1U
                        && expression.text.starts_with(
                            semantic::vhdl::qualified_expression_prefix)))) {
                for (const auto operand : expression.operands) {
                    validate_value_name(operand);
                }
            }
            // An index may be a subtype name (a slice by subtype), but not
            // a label or design unit; a type conversion's operand is a value.
            if (name_check_
                && expression.kind == semantic::vhdl::ExpressionKind::index) {
                for (std::size_t operand = 1U; operand < expression.operands.size();
                     ++operand) {
                    validate_value_name(expression.operands[operand], true);
                }
            }
            if (name_check_
                && expression.kind == semantic::vhdl::ExpressionKind::call
                && expression.operands.size() == 1U
                && !expression.text.starts_with('@') && expression.referenced_name
                && expression.referenced_name->selected) {
                const auto callee = specialization_.find_declaration(
                    *expression.referenced_name->selected);
                if (callee && callee->vhdl != nullptr
                    && (callee->vhdl->form == semantic::vhdl::DeclarationForm::type
                        || callee->vhdl->form
                            == semantic::vhdl::DeclarationForm::subtype)) {
                    validate_value_name(expression.operands.front());
                }
            }
            if (name_check_
                && expression.kind == semantic::vhdl::ExpressionKind::aggregate) {
                for (const auto& association : expression.associations) {
                    validate_value_name(association.value);
                }
            }
            if (expression.kind
                    != semantic::vhdl::ExpressionKind::binary
                || expression.operands.size() != 2U) {
                continue;
            }
            const auto left = expression_array_root(
                expression.operands[0]);
            const auto right = expression_array_root(
                expression.operands[1]);
            const auto relational = expression.text == "<"
                || expression.text == "<=" || expression.text == ">"
                || expression.text == ">=";
            // Ordering is defined for one-dimensional arrays of discrete
            // elements (IEEE 1076-2008 9.2.3), which the lowerer supports.
            const auto discrete_array = [&](const std::optional<semantic::TypeId> root) {
                const auto* definition = root ? type_definition(*root) : nullptr;
                if (definition == nullptr || definition->form != TypeForm::array
                    || definition->array_dimensions.size() != 1U
                    || !definition->element_subtype) {
                    return false;
                }
                const auto element = subtype_operand_type(
                    *definition->element_subtype, 0U);
                return element.kind == OperandClass::integer
                    || element.kind == OperandClass::enumeration;
            };
            if (relational && (left || right)
                && !(discrete_array(left) || discrete_array(right))) {
                report(
                    "FSIM-ELAB-VHARRAY-007",
                    "only equality and inequality are supported for VHDL "
                    "array values in the bounded runtime",
                    expression.source);
            }
            const auto equality = expression.text == "="
                || expression.text == "/=";
            if (equality && left && right && *left != *right) {
                report(
                    "FSIM-ELAB-VHARRAY-006",
                    "VHDL array comparison requires compatible nominal "
                    "array types",
                    expression.source);
            }
        }
        for (const auto& process : specialization_.design().vhdl_hir.processes()) {
            if (selected_scope(process.scope)) {
                validate_out_port_sensitivity(process.sensitivities, process.scope);
            }
        }
        // IEEE 1076-2008 11.7.1: a component instantiation names a declared
        // component; an actual is an expression, not a type or design unit.
        for (const auto& instance : specialization_.design().vhdl_hir.instances()) {
            if (!selected_scope(instance.scope)) {
                continue;
            }
            if (instance.component && !instance.configuration
                && instance.target.spelling.find('.') == std::string::npos
                && std::ranges::none_of(specialization_.design().vhdl_hir.declarations(),
                    [&](const semantic::vhdl::Declaration& candidate) {
                        return candidate.form == semantic::vhdl::DeclarationForm::component
                            && equal_ignoring_ascii_case(candidate.name, simple_name_of(instance.target.spelling));
                    })) {
                report("FSIM-ELAB-VHCOMP-016",
                    "VHDL component '" + instance.target.spelling + "' is not declared",
                    instance.source);
            }
            // IEEE 1076-2008 6.5.7.1: a positional port actual has the type
            // of its component formal.
            if (instance.component && !instance.configuration) {
                const auto& declarations = specialization_.design().vhdl_hir.declarations();
                const auto component = std::ranges::find_if(declarations,
                    [&](const semantic::vhdl::Declaration& candidate) {
                        return candidate.form == semantic::vhdl::DeclarationForm::component
                            && candidate.nested_scope
                            && equal_ignoring_ascii_case(candidate.name,
                                simple_name_of(instance.target.spelling));
                    });
                if (component != declarations.end()) {
                    std::vector<const semantic::vhdl::Declaration*> formals;
                    for (const auto& candidate : declarations) {
                        if (candidate.form == semantic::vhdl::DeclarationForm::port
                            && candidate.scope == *component->nested_scope && candidate.subtype) {
                            formals.push_back(&candidate);
                        }
                    }
                    for (std::size_t index { };
                         index < instance.port_map.size() && index < formals.size(); ++index) {
                        const auto& association = instance.port_map[index];
                        if (association.formal
                            || association.kind != semantic::vhdl::AssociationKind::expression
                            || !association.expression) {
                            continue;
                        }
                        // Only an object name is compared: an aggregate or
                        // literal takes its type from the formal.
                        const auto actual_expression
                            = specialization_.find_expression(*association.expression);
                        if (!actual_expression || actual_expression->vhdl == nullptr
                            || (actual_expression->vhdl->kind != semantic::vhdl::ExpressionKind::name
                                && actual_expression->vhdl->kind
                                    != semantic::vhdl::ExpressionKind::index
                                && actual_expression->vhdl->kind
                                    != semantic::vhdl::ExpressionKind::slice)) {
                            continue;
                        }
                        const auto actual = operand_type(*association.expression);
                        const auto formal = subtype_operand_type(*formals[index]->subtype, 0U);
                        if (actual.kind != OperandClass::unknown
                            && formal.kind != OperandClass::unknown
                            && actual.kind != formal.kind
                            && (actual.kind == OperandClass::array
                                || formal.kind == OperandClass::array)) {
                            report("FSIM-ELAB-VHPORT-006",
                                "the actual of VHDL port '" + formals[index]->name
                                    + "' does not have the formal's type",
                                association.source.valid() ? association.source : instance.source);
                        }
                    }
                }
            }
            // IEEE 1076-1993 1.1.1.2: a port actual that is a name is a static
            // name; its index expressions do not read signals or variables.
            // (VHDL-2008 admits an expression as the actual of an input.)
            for (const auto& association : instance.port_map) {
                if (architecture_.standard >= std::string_view { "2008" }
                    || association.kind != semantic::vhdl::AssociationKind::expression
                    || !association.expression) {
                    continue;
                }
                auto current = specialization_.find_expression(*association.expression);
                for (std::size_t depth { }; depth < 8U && current && current->vhdl != nullptr
                     && (current->vhdl->kind == semantic::vhdl::ExpressionKind::index
                         || current->vhdl->kind == semantic::vhdl::ExpressionKind::slice)
                     && !current->vhdl->operands.empty();
                     ++depth) {
                    const auto& operands = current->vhdl->operands;
                    if (std::any_of(operands.begin() + 1, operands.end(),
                            [&](const semantic::ExpressionId index) {
                                return reads_signal_or_variable(index);
                            })) {
                        report("FSIM-ELAB-VHPORT-005",
                            "the actual of a VHDL port association must be a static name",
                            current->vhdl->source);
                        break;
                    }
                    current = specialization_.find_expression(operands.front());
                }
            }
            if (name_check_) {
                // The actual of a type or package generic is a subtype
                // indication or a package (IEEE 1076-2008 6.5.7.2).
                const bool type_generics = instance_has_type_generic(instance);
                for (const auto* map : { &instance.generic_map, &instance.port_map }) {
                    for (const auto& association : *map) {
                        if (association.kind == semantic::vhdl::AssociationKind::expression
                            && association.expression) {
                            validate_value_name(*association.expression,
                                map == &instance.generic_map && type_generics);
                            validate_label_actual(*association.expression);
                        }
                    }
                }
            }
        }
        for (const auto& stored :
            specialization_.design().vhdl_hir.statements()) {
            const auto effective = specialization_.find_statement(
                stored.id);
            if (!effective || effective->vhdl == nullptr
                || !selected_scope(effective->vhdl->scope)) {
                continue;
            }
            const auto& statement = *effective->vhdl;
            validate_out_port_sensitivity(statement.sensitivities, statement.scope);
            if (!statement.target || !statement.value) {
                continue;
            }
            const auto target = expression_array_root(
                *statement.target);
            const auto value = expression_array_root(
                *statement.value);
            if (target && value && *target != *value) {
                report(
                    "FSIM-ELAB-VHARRAY-006",
                    "VHDL array assignment requires compatible nominal "
                    "type and bounds",
                    statement.source);
            }
            const auto value_expression
                = specialization_.find_expression(*statement.value);
            auto target_subtype = expression_subtype(*statement.target);
            if (auto slice_context = slice_assignment_context(
                    *statement.target)) {
                target_subtype = std::move(slice_context);
            }
            if (value_expression
                && value_expression->vhdl != nullptr
                && value_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate
                && target_subtype) {
                validate_aggregate(
                    *value_expression->vhdl, *target_subtype);
            }
        }
    }

    static const semantic::vhdl::Unit& detached_unit() noexcept
    {
        static const semantic::vhdl::Unit unit { };
        return unit;
    }

    const semantic::SpecializedHirUnit& specialization_;
    const semantic::vhdl::Unit& entity_;
    const semantic::vhdl::Unit& architecture_;
    std::unordered_set<std::uint32_t> relevant_types_;
    bool name_check_ { true };
    bool operator_check_ { true };
    bool names_only_ { };
    std::optional<std::unordered_set<std::uint32_t>> callable_scopes_;
    std::optional<std::unordered_set<std::uint32_t>> signal_formals_;
    std::optional<std::unordered_set<std::string>> exempt_names_;
    std::unordered_set<std::string> reported_;
    std::vector<VhdlHirTypeValidationIssue> issues_;
};

} // namespace

std::optional<semantic::vhdl::SubtypeIndication>
resolve_vhdl_user_attribute_subtype(
    const semantic::SpecializedHirUnit& specialization,
    const std::string_view nominal_type,
    const semantic::ScopeId use_scope)
{
    constexpr std::string_view marker {
        "@fsim-vhdl-user-attribute:"
    };
    if (!nominal_type.starts_with(marker)) {
        return std::nullopt;
    }
    const auto spelling = nominal_type.substr(marker.size());
    const auto separator = spelling.rfind(':');
    if (separator == std::string_view::npos
        || separator == 0U || separator + 1U == spelling.size()) {
        return std::nullopt;
    }
    const auto prefix = spelling.substr(0U, separator);
    const auto attribute_name = spelling.substr(separator + 1U);
    const auto last_dot = prefix.rfind('.');
    const auto target_name = last_dot == std::string_view::npos
        ? prefix
        : prefix.substr(last_dot + 1U);
    if (target_name.empty()) {
        return std::nullopt;
    }

    const auto& scopes = specialization.design().semantics.scopes();
    const auto& units = specialization.design().semantics.units();
    const auto& declarations
        = specialization.design().vhdl_hir.declarations();
    // Declarations of an entity are visible in its architectures (IEEE
    // 1076-2008 12.1).
    const auto entity_of_architecture = [&](const semantic::ScopeId inner,
                                            const semantic::ScopeId outer) {
        if (!inner.valid() || inner.value() >= scopes.size()
            || !outer.valid() || outer.value() >= scopes.size()) {
            return false;
        }
        const auto inner_unit = scopes[inner.value()].unit;
        const auto outer_unit = scopes[outer.value()].unit;
        if (!inner_unit.valid() || inner_unit.value() >= units.size()
            || !outer_unit.valid() || outer_unit.value() >= units.size()) {
            return false;
        }
        const auto& architecture = units[inner_unit.value()];
        const auto& entity = units[outer_unit.value()];
        return architecture.kind == semantic::UnitKind::vhdl_architecture
            && entity.kind == semantic::UnitKind::vhdl_entity
            && equal_ignoring_ascii_case(architecture.library, entity.library)
            && (equal_ignoring_ascii_case(architecture.name, entity.name)
                || equal_ignoring_ascii_case(
                    architecture.secondary_name, entity.name));
    };
    const auto visible_in = [&](const semantic::ScopeId inner,
                                const semantic::ScopeId outer) {
        return semantic::scope_distance(
            specialization.design().semantics, inner, outer)
            .has_value()
            || entity_of_architecture(inner, outer);
    };
    const auto unit_matches_prefix = [&](const semantic::ScopeId scope) {
        if (!scope.valid() || scope.value() >= scopes.size()) {
            return false;
        }
        const auto unit = scopes[scope.value()].unit;
        if (!unit.valid() || unit.value() >= units.size()) {
            return false;
        }
        const auto& unit_name = units[unit.value()].name;
        auto remaining = prefix;
        while (!remaining.empty()) {
            const auto dot = remaining.find('.');
            const auto component = remaining.substr(0U, dot);
            if (equal_ignoring_ascii_case(component, unit_name)) {
                return true;
            }
            if (dot == std::string_view::npos) {
                break;
            }
            remaining.remove_prefix(dot + 1U);
        }
        return false;
    };
    const auto scope_distance = [&](const semantic::ScopeId inner,
                                    const semantic::ScopeId outer) {
        return semantic::scope_distance(
            specialization.design().semantics, inner, outer)
            .value_or(scopes.size() + 1U);
    };

    const semantic::vhdl::Declaration* selected_specification { };
    auto selected_score = std::tuple {
        std::numeric_limits<int>::max(),
        std::numeric_limits<int>::max(),
        std::numeric_limits<std::size_t>::max()
    };
    bool ambiguous_specification { };
    for (const auto& candidate : declarations) {
        using Form = semantic::vhdl::DeclarationForm;
        if (candidate.form != Form::attribute_specification
            || !candidate.attribute
            || !equal_ignoring_ascii_case(candidate.name, attribute_name)) {
            continue;
        }
        int entity_rank = std::numeric_limits<int>::max();
        for (const auto& entity : candidate.attribute->entity_names) {
            if (equal_ignoring_ascii_case(entity.spelling, target_name)) {
                entity_rank = std::min(entity_rank, 0);
            } else if (equal_ignoring_ascii_case(entity.spelling, "others")) {
                entity_rank = std::min(entity_rank, 1);
            } else if (equal_ignoring_ascii_case(entity.spelling, "all")) {
                entity_rank = std::min(entity_rank, 2);
            }
        }
        if (entity_rank == std::numeric_limits<int>::max()) {
            continue;
        }
        const bool visible = visible_in(use_scope, candidate.scope);
        if (!visible && !unit_matches_prefix(candidate.scope)) {
            continue;
        }
        const auto score = std::tuple {
            entity_rank,
            visible ? 0 : 1,
            visible ? scope_distance(use_scope, candidate.scope)
                    : std::size_t { }
        };
        if (score < selected_score) {
            selected_score = score;
            selected_specification = &candidate;
            ambiguous_specification = false;
        } else if (score == selected_score
            && selected_specification != &candidate) {
            ambiguous_specification = true;
        }
    }
    if (selected_specification == nullptr || ambiguous_specification) {
        return std::nullopt;
    }

    semantic::vhdl::Name reference;
    reference.spelling = std::string { attribute_name };
    reference.canonical = reference.spelling;
    const semantic::CompiledDeclarationPredicate attribute_declaration
        = [](const semantic::CompiledDeclarationView& candidate) {
              return candidate.vhdl != nullptr
                  && candidate.vhdl->form
                      == semantic::vhdl::DeclarationForm::attribute_declaration;
          };
    const auto resolved
        = semantic::CompiledDesignResolver { specialization }
              .resolve_vhdl(reference, selected_specification->scope,
                  attribute_declaration)
              .unique();
    if (resolved) {
        const auto declaration = specialization.find_declaration(*resolved);
        if (declaration && declaration->vhdl != nullptr
            && declaration->vhdl->attribute
            && declaration->vhdl->attribute->subtype) {
            return *declaration->vhdl->attribute->subtype;
        }
    }

    return std::nullopt;
}

bool vhdl_locally_static_expression(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression)
{
    return Validator { specialization }.locally_static(expression);
}

std::vector<VhdlHirTypeValidationIssue> validate_vhdl_hir_types(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::vhdl::Unit& entity,
    const semantic::vhdl::Unit& architecture)
{
    return Validator { specialization, entity, architecture }.run();
}

std::vector<Diagnostic> validate_vhdl_architecture_names(
    const semantic::CompiledDesign& compiled,
    const semantic::UnitId architecture_id)
{
    std::vector<Diagnostic> result;
    const auto& units = compiled.vhdl_units();
    const auto architecture = std::ranges::find(
        units, architecture_id, &semantic::vhdl::Unit::id);
    if (architecture == units.end()
        || architecture->kind != semantic::vhdl::UnitKind::architecture) {
        return result;
    }
    const auto entity = std::ranges::find_if(units,
        [&](const semantic::vhdl::Unit& candidate) {
            return candidate.kind == semantic::vhdl::UnitKind::entity
                && candidate.library == architecture->library
                && equal_ignoring_ascii_case(candidate.name, architecture->primary_name);
        });
    if (entity == units.end()) {
        return result;
    }
    const auto specialization = semantic::make_specialized_hir_unit(
        compiled, architecture_id,
        std::span<const semantic::SpecializedHirActualIdentity> { });
    if (!specialization) {
        return result;
    }
    Validator validator { *specialization, *entity, *architecture };
    validator.set_names_only();
    for (const auto& issue : validator.run()) {
        // Without generic values only name rules are decidable here.
        if (issue.code != "FSIM-ELAB-VHNAME-002"
            && issue.code != "FSIM-ELAB-VHNAME-004"
            && issue.code != "FSIM-ELAB-VHNAME-015"
            && issue.code != "FSIM-ELAB-VHCOMP-016"
            && issue.code != "FSIM-ELAB-VHCALL-004"
            && issue.code != "FSIM-ELAB-VHDLCASE-011"
            && issue.code != "FSIM-ELAB-VHPORT-005"
            && issue.code != "FSIM-ELAB-VHPORT-006") {
            continue;
        }
        Diagnostic diagnostic;
        diagnostic.code = issue.code;
        diagnostic.message = issue.message;
        diagnostic.span = elaboration_detail::compiled_source_span(
            compiled, issue.source);
        result.push_back(std::move(diagnostic));
    }
    return result;
}

} // namespace fsim::elaboration


