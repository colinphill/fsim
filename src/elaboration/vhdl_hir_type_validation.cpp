// SPDX-License-Identifier: Apache-2.0
#include "vhdl_hir_type_validation.hpp"

#include "elaborator_internal.hpp"

#include "fsim/elaboration/elaborator.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <functional>
#include <cstdlib>
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

    std::optional<semantic::TypeId> named_type(
        const std::string_view spelling,
        const semantic::ScopeId use_scope = { }) const
    {
        if (use_scope.valid()) {
            semantic::vhdl::Name reference;
            reference.spelling = std::string { spelling };
            reference.canonical = reference.spelling;
            const semantic::CompiledDeclarationPredicate type_declaration
                = [](const semantic::CompiledDeclarationView& candidate) {
                      return candidate.vhdl != nullptr
                          && candidate.vhdl->declared_type.has_value();
                  };
            const auto declaration
                = semantic::CompiledDesignResolver { specialization_ }
                      .resolve_vhdl(
                          reference, use_scope, type_declaration)
                      .unique();
            if (declaration) {
                const auto view
                    = specialization_.find_declaration(*declaration);
                if (view && view->vhdl != nullptr
                    && view->vhdl->declared_type) {
                    return view->vhdl->declared_type;
                }
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
            && (value.text == "'left" || value.text == "'right"
                || value.text == "'low" || value.text == "'high"
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
                const semantic::CompiledDeclarationPredicate
                    type_declaration
                    = [](const semantic::CompiledDeclarationView& candidate) {
                          return candidate.vhdl != nullptr
                              && candidate.vhdl->declared_type.has_value();
                      };
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
        std::unordered_set<std::uint32_t> profile_visiting;
        const auto prefix_profile = profile(*prefix, profile_visiting);
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
        guard();
        if (!range.minimum || !range.maximum) {
            return std::nullopt;
        }
        if (value.text == "'left") {
            return range.descending ? range.maximum : range.minimum;
        }
        if (value.text == "'right") {
            return range.descending ? range.minimum : range.maximum;
        }
        if (value.text == "'low") {
            return range.minimum;
        }
        if (value.text == "'high") {
            return range.maximum;
        }
        if (value.text == "'ascending") {
            return range.descending ? 0 : 1;
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

    static ArrayDimensionProfile index_profile(
        const std::string_view spelling)
    {
        const auto name = simple_name_of(spelling);
        if (equal_ignoring_ascii_case(name, "natural")) {
            return { 0, std::numeric_limits<std::int64_t>::max(), false,
                false };
        }
        if (equal_ignoring_ascii_case(name, "positive")) {
            return { 1, std::numeric_limits<std::int64_t>::max(), false,
                false };
        }
        return {
            std::numeric_limits<std::int64_t>::min(),
            std::numeric_limits<std::int64_t>::max(),
            false,
            false,
        };
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

    TypeProfile profile(
        const SubtypeIndication& original,
        std::unordered_set<std::uint32_t>& visiting) const
    {
        const auto subtype = specialized_subtype(original);
        auto result = builtin_profile(subtype.type_mark.spelling);
        if (result.kind == ProfileKind::unknown) {
            result = builtin_profile(original.type_mark.spelling);
        }
        auto id = subtype_type(subtype);
        if (!id) {
            id = subtype_type(original);
        }
        const auto* definition = id ? type_definition(*id) : nullptr;
        if (definition != nullptr
            && visiting.insert(definition->id.value()).second) {
            switch (definition->form) {
            case TypeForm::subtype:
            case TypeForm::alias:
                result = profile(definition->base, visiting);
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
                for (const auto& dimension :
                    definition->array_dimensions) {
                    auto converted = index_profile(
                        dimension.index_subtype.spelling);
                    converted.constrained = !dimension.unconstrained
                        || dimension.constraint.has_value();
                    if (dimension.constraint) {
                        const auto left = bound(
                            dimension.constraint->left,
                            dimension.constraint->left_expression);
                        const auto right = bound(
                            dimension.constraint->right,
                            dimension.constraint->right_expression);
                        if (left && right) {
                            converted.minimum = std::min(*left, *right);
                            converted.maximum = std::max(*left, *right);
                        }
                        converted.descending
                            = dimension.constraint->descending;
                    }
                    result.dimensions.push_back(converted);
                }
                if (definition->element_subtype) {
                    std::unordered_set<std::uint32_t> element_visiting;
                    const auto element = profile(
                        *definition->element_subtype,
                        element_visiting);
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
        apply_constraints_to_profile(result, subtype.constraints);
        return result;
    }

    void apply_constraints_to_profile(
        TypeProfile& profile_value,
        const std::span<const RangeConstraint> constraints) const
    {
        if (profile_value.kind == ProfileKind::array) {
            for (std::size_t index { };
                index < constraints.size()
                    && index < profile_value.dimensions.size();
                ++index) {
                profile_value.dimensions[index].constrained = true;
                const auto left = bound(
                    constraints[index].left,
                    constraints[index].left_expression);
                const auto right = bound(
                    constraints[index].right,
                    constraints[index].right_expression);
                if (left && right) {
                    profile_value.dimensions[index].minimum
                        = std::min(*left, *right);
                    profile_value.dimensions[index].maximum
                        = std::max(*left, *right);
                }
                profile_value.dimensions[index].descending
                    = constraints[index].descending;
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

    void validate_constraints(
        const SubtypeIndication& original,
        const semantic::SourceSpanId fallback)
    {
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
        std::unordered_set<std::uint32_t> visiting;
        const auto base = profile(unconstrained, visiting);
        const auto source = subtype.constraints.front().source.valid()
            ? subtype.constraints.front().source
            : fallback;
        if (base.kind == ProfileKind::array) {
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
                const auto left = bound(
                    constraint.left, constraint.left_expression);
                const auto right = bound(
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
        const auto known = [](const OperandClass kind) {
            return kind != OperandClass::unknown;
        };
        if (!known(left.kind) || !known(right.kind)) {
            return;
        }
        const auto numeric_class = [](const OperandClass kind) {
            return kind == OperandClass::integer || kind == OperandClass::floating;
        };
        // The bounds of an integer or floating type definition need only
        // be of some integer or floating type (IEEE 1076-1993 3.1.2, 3.1.4).
        const auto mismatch = left.kind != right.kind
            || (!scalar_definition && left.kind == OperandClass::enumeration
                && roots_conflict(left.root, right.root))
            || (scalar_definition && !numeric_class(left.kind));
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
            name.remove_prefix(name.find_last_of(".:") + 1U);
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

    void validate_type_bounds(const TypeDefinition& type)
    {
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
                return kind == OperandClass::enumeration || kind == OperandClass::array
                    || kind == OperandClass::record || kind == OperandClass::access;
            };
            if ((base.kind == OperandClass::integer || base.kind == OperandClass::floating)
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
                // An index subtype is discrete (IEEE 1076-1993 3.2.1).
                const auto index_declaration = dimension.index_subtype.selected
                    ? specialization_.find_declaration(*dimension.index_subtype.selected)
                    : std::nullopt;
                if (!index_declaration) {
                    auto spelling = std::string_view { dimension.index_subtype.spelling };
                    spelling.remove_prefix(spelling.find_last_of(".:") + 1U);
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
            validate_constraints(type.base, type.source);
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
        std::unordered_set<std::uint32_t> visiting;
        const auto element = profile(*type.element_subtype, visiting);
        // VHDL-2008 permits an unbounded element subtype that object
        // declarations constrain (IEEE 1076-2008 5.3.2.1).
        const bool unbounded_elements
            = architecture_.standard >= std::string_view { "2008" };
        if (element.kind == ProfileKind::unknown
            || (element.kind == ProfileKind::string && !unbounded_elements)
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
        validate_constraints(*declaration.subtype, declaration.source);
        std::unordered_set<std::uint32_t> visiting;
        const auto object = profile(subtype, visiting);
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
            auto scope = inner;
            for (std::size_t depth { }; depth <= scopes.size(); ++depth) {
                if (scope == outer) {
                    return true;
                }
                if (!scope.valid() || scope.value() >= scopes.size()
                    || !scopes[scope.value()].parent) {
                    return false;
                }
                scope = *scopes[scope.value()].parent;
            }
            return false;
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
                bool class_match { };
                bool visible { };
                for (const auto& candidate : declarations) {
                    if (!equal_ignoring_ascii_case(candidate.name, spelling)) {
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
                if (declared_here && !class_match) {
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
            const auto attribute = std::ranges::find_if(declarations,
                [&](const semantic::vhdl::Declaration& candidate) {
                    return candidate.form == Form::attribute_declaration
                        && candidate.attribute
                        && candidate.attribute->subtype
                        && equal_ignoring_ascii_case(candidate.name, declaration.name)
                        && visible_in(declaration.scope, candidate.scope);
                });
            if (!value || value->vhdl == nullptr
                || attribute == declarations.end()) {
                continue;
            }
            const auto expected
                = subtype_operand_type(*attribute->attribute->subtype, 0U).kind;
            using Kind = semantic::vhdl::ExpressionKind;
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
                actual_source);
        }
    }

    std::optional<semantic::TypeId> array_root(
        semantic::TypeId id) const
    {
        std::unordered_set<std::uint32_t> visiting;
        while (id.valid() && visiting.insert(id.value()).second) {
            const auto* type = type_definition(id);
            if (type == nullptr) {
                return std::nullopt;
            }
            if (type->form == TypeForm::array) {
                return id;
            }
            if (type->form != TypeForm::subtype
                && type->form != TypeForm::alias) {
                return std::nullopt;
            }
            const auto base = subtype_type(type->base);
            if (!base) {
                return std::nullopt;
            }
            id = *base;
        }
        return std::nullopt;
    }

    std::optional<semantic::TypeId> expression_array_root(
        const semantic::ExpressionId id) const
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
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
        const auto object = declaration->vhdl->form == Form::signal
            || declaration->vhdl->form == Form::variable
            || declaration->vhdl->form == Form::constant
            || declaration->vhdl->form == Form::port
            || declaration->vhdl->form == Form::generic_constant
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

    std::optional<SubtypeIndication> expression_subtype(
        const semantic::ExpressionId id) const
    {
        const auto expression = specialization_.find_expression(id);
        if (!expression || expression->vhdl == nullptr) {
            return std::nullopt;
        }
        const auto& value = *expression->vhdl;
        if (value.referenced_name) {
            const semantic::CompiledDeclarationPredicate type_declaration
                = [](const semantic::CompiledDeclarationView& candidate) {
                      if (candidate.vhdl == nullptr) {
                          return false;
                      }
                      using Form = semantic::vhdl::DeclarationForm;
                      const auto form = candidate.vhdl->form;
                      return form == Form::type
                          || form == Form::subtype
                          || form == Form::generic_type;
                  };
            const auto selected
                = semantic::CompiledDesignResolver { specialization_ }
                      .resolve_expression_name(
                          id, type_declaration, true)
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
        if (!value.nominal_type.empty()) {
            SubtypeIndication subtype;
            subtype.type_mark.spelling = value.nominal_type;
            if (const auto type = named_type(
                    value.nominal_type, value.scope)) {
                subtype.type_mark.target = *type;
            }
            return specialized_subtype(std::move(subtype));
        }
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
                      using Form = semantic::vhdl::DeclarationForm;
                      return candidate.vhdl != nullptr
                          && (candidate.vhdl->form == Form::variable
                              || candidate.vhdl->form == Form::signal
                              || candidate.vhdl->form == Form::constant
                              || candidate.vhdl->form == Form::port
                              || candidate.vhdl->form
                                  == Form::generic_constant);
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
                const auto object = form == Form::generic_constant
                    || form == Form::port || form == Form::signal
                    || form == Form::constant || form == Form::variable
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
        if ((value.kind == semantic::vhdl::ExpressionKind::index
                || value.kind
                    == semantic::vhdl::ExpressionKind::slice)
            && !value.operands.empty()) {
            const auto base = expression_subtype(value.operands.front());
            if (!base
                || value.kind == semantic::vhdl::ExpressionKind::slice) {
                return base;
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
            std::unordered_set<std::uint32_t> visiting;
            return profile(*subtype, visiting);
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

    // The type declared by the nearest `type` declaration on a subtype's
    // chain: `type x1 is range 1.0 to 9.0` is kept as a subtype of REAL.
    std::optional<std::string> declared_scalar_root(const SubtypeIndication& subtype) const
    {
        auto type = subtype_type(specialized_subtype(subtype));
        if (!type) {
            type = subtype_type(subtype);
        }
        std::unordered_set<std::uint32_t> visiting;
        while (type && visiting.insert(type->value()).second) {
            const auto* definition = type_definition(*type);
            if (definition == nullptr) {
                return std::nullopt;
            }
            const auto declaration = specialization_.find_declaration(definition->declaration);
            const auto& semantic_types = specialization_.design().semantics.types();
            // An integer or floating type definition is retained as a
            // subtype of INTEGER or REAL but recorded as a type declaration.
            const bool scalar_type_definition = type->value() < semantic_types.size()
                && semantic_types[type->value()].id == *type
                && semantic_types[type->value()].kind == semantic::TypeKind::declaration;
            if (declaration && declaration->vhdl != nullptr
                && (declaration->vhdl->form == semantic::vhdl::DeclarationForm::type
                    || scalar_type_definition)) {
                return library_type(*definition)
                    ? std::nullopt
                    : std::optional { "#" + std::to_string(definition->id.value()) };
            }
            if (definition->form != TypeForm::subtype && definition->form != TypeForm::alias) {
                return std::nullopt;
            }
            type = subtype_type(definition->base);
        }
        return std::nullopt;
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
        if (result.kind == OperandClass::integer || result.kind == OperandClass::floating
            || result.kind == OperandClass::physical
            || result.kind == OperandClass::enumeration) {
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
            while (builtin_operand_class(spelling) == OperandClass::unknown
                && type && visiting.insert(type->value()).second) {
                const auto* link = type_definition(*type);
                if (link == nullptr) {
                    break;
                }
                spelling = builtin_operand_class(link->name) != OperandClass::unknown
                    ? std::string_view { link->name }
                    : std::string_view { link->base.type_mark.spelling };
                type = subtype_type(link->base);
            }
            const auto kind = builtin_operand_class(spelling);
            return { kind,
                kind == OperandClass::unknown
                    ? std::string { }
                    : builtin_root(spelling) };
        }
        const auto named = builtin_operand_class(definition->name);
        if (named != OperandClass::unknown) {
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
            if (constexpr std::string_view qualified { "@vhdl-qualified:" };
                value.text.starts_with(qualified) && value.operands.size() == 1U) {
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
            if ((value.text == "'left" || value.text == "'right"
                    || value.text == "'low" || value.text == "'high")
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
            const auto numeric = [](const OperandClass kind) {
                return kind == OperandClass::integer
                    || kind == OperandClass::floating;
            };
            const auto same = [&]() -> OperandType {
                if (left.kind != right.kind || roots_conflict(left.root, right.root)) {
                    return { };
                }
                return { left.kind, left.root.empty() ? right.root : left.root };
            };
            const auto& operation = value.text;
            if (operation == "+" || operation == "-" || operation == "mod"
                || operation == "rem") {
                return numeric(left.kind) || left.kind == OperandClass::physical
                    ? same()
                    : OperandType { };
            }
            if (operation == "*" || operation == "/") {
                if (numeric(left.kind) && numeric(right.kind)) {
                    return same();
                }
                if (left.kind == OperandClass::physical && numeric(right.kind)) {
                    return left;
                }
                if (operation == "*" && right.kind == OperandClass::physical
                    && numeric(left.kind)) {
                    return right;
                }
                if (operation == "/" && left.kind == OperandClass::physical
                    && right.kind == OperandClass::physical) {
                    return { OperandClass::integer, { } };
                }
                return { };
            }
            if (operation == "**" && numeric(left.kind)) {
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
            using Form = semantic::vhdl::DeclarationForm;
            if (declaration && declaration->vhdl != nullptr
                && declaration->vhdl->subtype
                && (declaration->vhdl->form == Form::variable
                    || declaration->vhdl->form == Form::signal
                    || declaration->vhdl->form == Form::constant
                    || declaration->vhdl->form == Form::port
                    || declaration->vhdl->form == Form::generic_constant)) {
                return subtype_operand_type(
                    specialized_subtype(*declaration->vhdl->subtype), depth);
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
        const auto& spelling = dimension.index_subtype.canonical.empty()
            ? dimension.index_subtype.spelling
            : dimension.index_subtype.canonical;
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
        const auto mark = expression.referenced_name->canonical.empty()
            ? expression.referenced_name->spelling
            : expression.referenced_name->canonical;
        const auto target_id = named_type(mark, expression.scope);
        const auto operand = operand_type(expression.operands.front());
        if (!target_id || !operand.root.starts_with('#')) {
            return;
        }
        const auto array_of = [&](semantic::TypeId id) -> const TypeDefinition* {
            std::unordered_set<std::uint32_t> visiting;
            while (visiting.insert(id.value()).second) {
                const auto* definition = type_definition(id);
                if (definition == nullptr) {
                    return nullptr;
                }
                if (definition->form == TypeForm::array) {
                    return definition;
                }
                if (definition->form != TypeForm::subtype
                    || !definition->base.type_mark.target.valid()) {
                    return nullptr;
                }
                id = definition->base.type_mark.target;
            }
            return nullptr;
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
                : left.kind == right.kind && !roots_conflict(left.root, right.root);
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
        if (expression.kind != semantic::vhdl::ExpressionKind::call
            || expression.operands.empty()) {
            return;
        }
        constexpr std::string_view qualified_prefix { "@vhdl-qualified:" };
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
                // 9.3.3.3: OTHERS needs the bounds of a constrained context.
                const bool others = std::ranges::any_of(operand->vhdl->associations,
                    [&](const semantic::vhdl::AggregateAssociation& association) {
                        return equal_ignoring_ascii_case(association.choice_spelling, "others")
                            || std::ranges::any_of(association.choices,
                                [&](const semantic::ExpressionId choice) {
                                    const auto found = specialization_.find_expression(choice);
                                    return found && found->vhdl != nullptr
                                        && found->vhdl->kind
                                            == semantic::vhdl::ExpressionKind::default_choice;
                                });
                    });
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
            const auto scalar = [](const OperandClass kind) {
                return kind == OperandClass::integer || kind == OperandClass::floating
                    || kind == OperandClass::physical || kind == OperandClass::enumeration;
            };
            const auto comparable = scalar(expected.kind)
                || expected.kind == OperandClass::record;
            if (comparable && actual.kind != OperandClass::unknown
                && actual.kind != OperandClass::null
                && (actual.kind != expected.kind
                    || roots_conflict(actual.root, expected.root))) {
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
        }
    }

    // IEEE 1076-2008 10.5.2.1: the target of a signal assignment denotes a
    // signal.
    void validate_signal_target(semantic::ExpressionId target_id,
        const semantic::SourceSpanId source)
    {
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
                        static_name = std::ranges::all_of(
                            std::span { value.operands }.subspan(1U),
                            [&](const semantic::ExpressionId operand) {
                                return specialization_.evaluate_integral_expression(operand)
                                    .has_value();
                            });
                    }
                    if (!static_name
                        && (value.kind == Kind::aggregate || value.kind == Kind::index
                            || value.kind == Kind::slice
                            || value.text.starts_with("@vhdl-qualified:")
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
        const std::vector<std::pair<std::string, semantic::ExpressionId>>& actuals)
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
                        return equal_ignoring_ascii_case(association.choice_spelling, "others")
                            || std::ranges::any_of(association.choices,
                                [&](const semantic::ExpressionId choice) {
                                    const auto found = specialization_.find_expression(choice);
                                    return found && found->vhdl != nullptr
                                        && found->vhdl->kind
                                            == semantic::vhdl::ExpressionKind::default_choice;
                                });
                    })) {
                return false;
            }
            const auto expected = subtype_operand_type(*formal_declaration->vhdl->subtype, 0U);
            const auto given = operand_type(actual);
            const auto scalar = [](const OperandClass kind) {
                return kind == OperandClass::integer || kind == OperandClass::floating
                    || kind == OperandClass::physical || kind == OperandClass::enumeration;
            };
            return !scalar(expected.kind) || !scalar(given.kind)
                || (given.kind == expected.kind
                    && !roots_conflict(given.root, expected.root));
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
        if (std::ranges::none_of(candidates, [&](const auto& candidate) {
                return callable_accepts(candidate.body, actuals);
            })) {
            report("FSIM-ELAB-VHCALL-002",
                "no visible procedure '" + statement.procedure.spelling
                    + "' has formals matching this call's actuals",
                statement.source);
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
        if (std::ranges::none_of(candidates, [&](const auto& candidate) {
                return accepts(candidate.body);
            })) {
            report("FSIM-ELAB-VHCALL-001",
                "no visible function '" + expression.text
                    + "' has formals matching this call's actuals",
                expression.source);
            return;
        }
        // A formal of class signal is associated with a signal name (IEEE
        // 1076-1993 2.1.1.2).
        if (candidates.size() != 1U) {
            return;
        }
        const auto body = specialization_.find_declaration(candidates.front().body);
        if (!body || body->vhdl == nullptr || !body->vhdl->callable) {
            return;
        }
        const auto& formals = body->vhdl->callable->formals;
        const auto signal_name = [&](semantic::ExpressionId id) {
            for (std::size_t depth { }; depth < 16U; ++depth) {
                const auto actual = specialization_.find_expression(id);
                if (!actual || actual->vhdl == nullptr) {
                    return true;
                }
                if ((actual->vhdl->kind == semantic::vhdl::ExpressionKind::index
                        || actual->vhdl->kind == semantic::vhdl::ExpressionKind::slice)
                    && !actual->vhdl->operands.empty()) {
                    id = actual->vhdl->operands.front();
                    continue;
                }
                if (actual->vhdl->kind != semantic::vhdl::ExpressionKind::name
                    || !actual->vhdl->referenced_name
                    || !actual->vhdl->referenced_name->selected) {
                    return false;
                }
                const auto declaration = specialization_.find_declaration(
                    *actual->vhdl->referenced_name->selected);
                return !declaration || declaration->vhdl == nullptr
                    || declaration->vhdl->form == semantic::vhdl::DeclarationForm::signal
                    || declaration->vhdl->form == semantic::vhdl::DeclarationForm::port
                    || declaration->vhdl->object_class
                        == semantic::vhdl::ObjectClass::signal;
            }
            return true;
        };
        for (std::size_t index { }; index < actuals.size() && index < formals.size();
            ++index) {
            if (!actuals[index].first.empty()) {
                break;
            }
            const auto formal = specialization_.find_declaration(formals[index]);
            if (formal && formal->vhdl != nullptr
                && formal->vhdl->object_class == semantic::vhdl::ObjectClass::signal
                && !signal_name(actuals[index].second)) {
                report("FSIM-ELAB-VHCALL-003",
                    "the actual for signal formal '" + formal->vhdl->name
                        + "' of function '" + expression.text
                        + "' is not a signal name",
                    expression.source);
            }
        }
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
            const auto has_body = std::ranges::any_of(declarations, [&](const auto& other) {
                return other.callable && other.callable->defined
                    && equal_ignoring_ascii_case(other.name, declaration.name)
                    && (other.scope == declaration.scope
                        || unit_of(other.scope) == unit_of(declaration.scope));
            });
            if (!has_body) {
                report("FSIM-ELAB-VHBODY-001",
                    "VHDL subprogram '" + declaration.name
                        + "' is declared without a body in the same declarative part",
                    declaration.source);
            }
        }
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
                    || declaration->vhdl->form == Form::package_instance
                    || declaration->vhdl->form == Form::generated)) {
                report("FSIM-ELAB-VHNAME-004",
                    "VHDL name '" + expression.text
                        + "' denotes a type, subtype or design unit, not a value",
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
        if (std::ranges::any_of(predefined_literals, [&](const auto literal) {
                return equal_ignoring_ascii_case(expression.text, literal);
            })
            || builtin_operand_class(expression.text) != OperandClass::unknown
            || vhdl_character_control_literal(expression.text)) {
            return;
        }
        if (name_exempt(expression.text)) {
            return;
        }
        const semantic::CompiledDesignResolver resolver { specialization_ };
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
        if (!target_expression || target_expression->vhdl == nullptr
            || target_expression->vhdl->kind != semantic::vhdl::ExpressionKind::name) {
            return;
        }
        const auto target = operand_type(target_id);
        const auto value = operand_type(value_id);
        const auto scalar = [](const OperandClass kind) {
            return kind == OperandClass::integer || kind == OperandClass::floating
                || kind == OperandClass::physical
                || kind == OperandClass::enumeration;
        };
        if (!scalar(target.kind) && target.kind != OperandClass::array
            && target.kind != OperandClass::record) {
            return;
        }
        // A scalar value (a parenthesized one, as `(25)`, is not an
        // aggregate) is not of a composite type.
        if (!scalar(target.kind) && scalar(value.kind)) {
            const auto expression = specialization_.find_expression(value_id);
            report("FSIM-ELAB-VHASSIGN-001",
                "a scalar value is assigned to a VHDL target of a composite type",
                expression && expression->vhdl != nullptr
                    ? expression->vhdl->source
                    : source);
            return;
        }
        // An allocator or `null` is an access value (9.3.7).
        if (!scalar(target.kind)) {
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
        if (expression.kind == semantic::vhdl::ExpressionKind::call
            && expression.text.starts_with("@vhdl-qualified:")
            && expression.operands.size() == 1U) {
            return;
        }
        // 9.3.2: each character of a string literal is a literal of the
        // element type (BIT, STD_ULOGIC, CHARACTER).
        if (expression.kind == semantic::vhdl::ExpressionKind::string_literal
            && expression.decoded_string && expression.text.starts_with('"')) {
            const auto* array = root_definition(context);
            if (array != nullptr && array->form == TypeForm::array && array->element_subtype) {
                auto element = std::string_view { array->element_subtype->type_mark.spelling };
                element.remove_prefix(element.find_last_of(".:") + 1U);
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
        const auto* definition = root_definition(context);
        if (definition == nullptr
            || (definition->form != TypeForm::array
                && definition->form != TypeForm::record)) {
            return;
        }
        const auto is_others = [&](const semantic::vhdl::AggregateAssociation& association) {
            if (equal_ignoring_ascii_case(association.choice_spelling, "others")) {
                return true;
            }
            return std::ranges::any_of(association.choices, [&](const semantic::ExpressionId choice) {
                const auto found = specialization_.find_expression(choice);
                return found && found->vhdl != nullptr
                    && (found->vhdl->kind == semantic::vhdl::ExpressionKind::default_choice
                        || equal_ignoring_ascii_case(found->vhdl->text, "others"));
            });
        };
        const auto positional = [&](const semantic::vhdl::AggregateAssociation& association) {
            return association.choices.empty() && association.choice_spelling.empty();
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
            const auto expected = subtype_operand_type(element, 0U);
            const auto actual = operand_type(value);
            const auto scalar = [](const OperandClass kind) {
                return kind == OperandClass::integer || kind == OperandClass::floating
                    || kind == OperandClass::physical || kind == OperandClass::enumeration;
            };
            return !scalar(expected.kind) || actual.kind == OperandClass::unknown
                || (actual.kind == expected.kind
                    && !roots_conflict(actual.root, expected.root));
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
                                return association.choices.empty()
                                    && association.choice_spelling.empty();
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
        const semantic::SourceSpanId source)
    {
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
        if (type.kind == OperandClass::integer && subtype.constraints.size() == 1U) {
            const auto& range = subtype.constraints.front();
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
        mark.remove_prefix(mark.find_last_of(".:") + 1U);
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
                name.remove_prefix(name.find_last_of(".:") + 1U);
                if (equal_ignoring_ascii_case(name, "natural")) {
                    range = std::pair { std::int64_t { 0 },
                        std::int64_t { std::numeric_limits<std::int32_t>::max() } };
                    break;
                }
                if (equal_ignoring_ascii_case(name, "positive")) {
                    range = std::pair { std::int64_t { 1 },
                        std::int64_t { std::numeric_limits<std::int32_t>::max() } };
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
            std::unordered_set<std::uint32_t> visiting;
            if (const auto shape = profile(*selector_subtype, visiting);
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
            const auto type = operand_type(choice);
            if (type.kind != OperandClass::unknown && selector_type.kind != OperandClass::unknown
                && (type.kind != selector_type.kind
                    || roots_conflict(type.root, selector_type.root))) {
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
                return;
            }
            if (definition->form != TypeForm::array) {
                const auto type = subtype_operand_type(subtype, 0U);
                if (type.kind == OperandClass::integer || type.kind == OperandClass::floating
                    || type.kind == OperandClass::enumeration
                    || type.kind == OperandClass::physical
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
        const auto scalar = [](const OperandClass kind) {
            return kind == OperandClass::integer || kind == OperandClass::floating
                || kind == OperandClass::physical || kind == OperandClass::enumeration;
        };
        if ((scalar(expected.kind) || scalar(given.kind))
            && expected.kind != OperandClass::unknown && given.kind != OperandClass::unknown
            && (expected.kind != given.kind || roots_conflict(expected.root, given.root))) {
            report("FSIM-ELAB-VHALIAS-002",
                "the subtype of VHDL alias '" + alias.name
                    + "' does not have the type of the aliased object",
                alias.source);
        }
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
        // A slice is kept as a slice or as an index whose argument is a range.
        std::optional<semantic::ExpressionId> left;
        std::optional<semantic::ExpressionId> right;
        bool descending { };
        if (target->vhdl->kind == semantic::vhdl::ExpressionKind::slice
            && target->vhdl->operands.size() == 3U) {
            left = target->vhdl->operands[1];
            right = target->vhdl->operands[2];
            descending = target->vhdl->text == "downto";
        } else if (target->vhdl->kind == semantic::vhdl::ExpressionKind::index
            && target->vhdl->operands.size() == 2U) {
            const auto range = specialization_.find_expression(target->vhdl->operands[1]);
            if (range && range->vhdl != nullptr
                && range->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                && (range->vhdl->text == "to" || range->vhdl->text == "downto")
                && range->vhdl->operands.size() == 2U) {
                left = range->vhdl->operands[0];
                right = range->vhdl->operands[1];
                descending = range->vhdl->text == "downto";
            }
        }
        if (!left || !right) {
            return;
        }
        const auto prefix = specialization_.find_expression(target->vhdl->operands.front());
        if (prefix && prefix->vhdl != nullptr && prefix->vhdl->referenced_name
            && prefix->vhdl->referenced_name->selected) {
            const auto declaration = specialization_.find_declaration(
                *prefix->vhdl->referenced_name->selected);
            if (declaration && declaration->vhdl != nullptr && declaration->vhdl->subtype) {
                validate_aggregate(value_id, *declaration->vhdl->subtype);
            }
        }
        const auto low = specialization_.evaluate_integral_expression(*left);
        const auto high = specialization_.evaluate_integral_expression(*right);
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
                    return association.choices.empty() && association.choice_spelling.empty();
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
        const auto expected = subtype_operand_type(*function->callable->return_type, 0U);
        const auto given = operand_type(*statement.value);
        const auto scalar = [](const OperandClass kind) {
            return kind == OperandClass::integer || kind == OperandClass::floating
                || kind == OperandClass::physical || kind == OperandClass::enumeration;
        };
        if (scalar(expected.kind) && scalar(given.kind)
            && (given.kind != expected.kind || roots_conflict(given.root, expected.root))) {
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
        if (type.kind == OperandClass::integer || type.kind == OperandClass::floating
            || type.kind == OperandClass::enumeration || type.kind == OperandClass::physical
            || type.kind == OperandClass::record) {
            report("FSIM-ELAB-VHARRAYSEL-005",
                "a VHDL slice requires a one-dimensional array prefix", source);
        }
    }

    // IEEE 1076-1993 6.5: the bounds of a slice's discrete range have one
    // type, the index type of the prefix.
    void validate_slice_bounds(const semantic::ExpressionId prefix_id,
        const semantic::ExpressionId left_id, const semantic::ExpressionId right_id,
        const semantic::SourceSpanId source)
    {
        const auto left = operand_type(left_id);
        const auto right = operand_type(right_id);
        const auto known = [](const OperandType& type) {
            return type.kind != OperandClass::unknown;
        };
        if (known(left) && known(right)
            && (left.kind != right.kind || roots_conflict(left.root, right.root))) {
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
            if (known(bound) && known(index_type)
                && (bound.kind != index_type.kind
                    || roots_conflict(bound.root, index_type.root))) {
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
        if (!predefined_operator(expression)
            || expression.builtin_operator
                != semantic::vhdl::BuiltinOperatorIdentity::none) {
            return;
        }
        const auto& operation = expression.text;
        const auto numeric = [](const OperandClass kind) {
            return kind == OperandClass::integer || kind == OperandClass::floating
                || kind == OperandClass::physical;
        };
        const auto known = [](const OperandClass kind) {
            return kind != OperandClass::unknown;
        };
        const auto reject = [&] {
            report("FSIM-ELAB-VHOPER-001",
                "the predefined VHDL operator \"" + operation
                    + "\" is not defined for these operand types",
                expression.source);
        };
        if (expression.kind == semantic::vhdl::ExpressionKind::unary
            && expression.operands.size() == 1U) {
            if (operation != "+" && operation != "-" && operation != "abs") {
                return;
            }
            const auto operand = operand_type(expression.operands.front());
            // STRING has no arithmetic; other library arrays may have it
            // (NUMERIC_STD).
            if (known(operand.kind) && !numeric(operand.kind)
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
            if (!known(left.kind) || !known(right.kind)) {
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
        const auto library_array = [](const OperandType& operand) {
            return operand.kind == OperandClass::array
                && !operand.root.starts_with('#');
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
        if (library_array(left) || library_array(right)) {
            return;
        }
        const auto non_numeric = [&](const OperandType& operand) {
            return known(operand.kind) && !numeric(operand.kind);
        };
        if (non_numeric(left) || non_numeric(right)) {
            reject();
            return;
        }
        if (!known(left.kind) || !known(right.kind)) {
            return;
        }
        const auto same_type = left.kind == right.kind
            && !roots_conflict(left.root, right.root);
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
        const semantic::ExpressionId actual_id) const
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
                == semantic::vhdl::ExpressionKind::string_literal
            || actual->vhdl->kind
                == semantic::vhdl::ExpressionKind::aggregate) {
            std::unordered_set<std::uint32_t> visiting;
            const auto expected_kind = profile(expected, visiting).kind;
            return expected_kind == ProfileKind::array
                || (actual->vhdl->kind
                        == semantic::vhdl::ExpressionKind::string_literal
                    && expected_kind == ProfileKind::scalar)
                || (actual->vhdl->kind
                        == semantic::vhdl::ExpressionKind::aggregate
                    && expected_kind == ProfileKind::record);
        }
        const auto actual_subtype = expression_subtype(actual_id);
        if (!actual_subtype) {
            return true;
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

    const TypeDefinition* root_definition(
        const SubtypeIndication& original) const
    {
        auto type = subtype_type(specialized_subtype(original));
        if (!type) {
            type = subtype_type(original);
        }
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

    bool choice_is_others(
        const semantic::vhdl::AggregateAssociation& association) const
    {
        if (equal_ignoring_ascii_case(simple_name_of(association.choice_spelling),
                "others")) {
            return true;
        }
        return association.choices.size() == 1U
            && [&] {
                   const auto choice = specialization_.find_expression(
                       association.choices.front());
                   return choice && choice->vhdl != nullptr
                       && choice->vhdl->kind
                           == semantic::vhdl::ExpressionKind::name
                       && equal_ignoring_ascii_case(choice->vhdl->text, "others");
               }();
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
        const semantic::SourceSpanId source)
    {
        if (!aggregate_element_compatible(expected, actual_id)) {
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
        const auto context_compatible_bit_literal = actual
            && actual->vhdl != nullptr
            && actual->vhdl->kind
                == semantic::vhdl::ExpressionKind::logic_literal
            && actual->vhdl->text.size() == 3U
            && (actual->vhdl->text[1] == '0'
                || actual->vhdl->text[1] == '1');
        if (expected.domain == semantic::vhdl::ValueDomain::bit2
            && actual_subtype
            && actual_subtype->domain
                != semantic::vhdl::ValueDomain::unknown
            && actual_subtype->domain
                != semantic::vhdl::ValueDomain::bit2
            && !context_compatible_bit_literal) {
            report(
                "FSIM-ELAB-VHARRAYAGG-007",
                "two-state VHDL aggregate element requires an "
                    "explicit conversion",
                source);
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
        if (expected_width && actual_width
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
            if (association.choice_spelling.empty()
                && association.choices.empty()) {
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

    void validate_array_aggregate(
        const semantic::vhdl::Expression& aggregate,
        TypeProfile context,
        const SubtypeIndication& element)
    {
        if (context.dimensions.empty()) {
            return;
        }
        const auto dimension = context.dimensions.front();
        if (!dimension.minimum || !dimension.maximum) {
            return;
        }
        const auto minimum = *dimension.minimum;
        const auto maximum = *dimension.maximum;
        const auto element_count = static_cast<std::uint64_t>(
            maximum - minimum) + 1U;
        std::unordered_set<std::int64_t> assigned;
        bool has_others = false;
        auto positional = dimension.descending ? maximum : minimum;

        const auto validate_value = [&](const semantic::ExpressionId value,
                                        const semantic::SourceSpanId source)
            -> void {
            const auto expression = specialization_.find_expression(value);
            if (context.dimensions.size() > 1U && expression
                && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate) {
                auto nested = context;
                nested.dimensions.erase(nested.dimensions.begin());
                validate_array_aggregate(
                    *expression->vhdl, std::move(nested), element);
                return;
            }
            if (context.dimensions.size() > 1U && expression
                && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal) {
                std::uint64_t expected_elements { 1U };
                for (auto nested_dimension
                         = context.dimensions.begin() + 1;
                    nested_dimension != context.dimensions.end();
                    ++nested_dimension) {
                    if (!nested_dimension->minimum
                        || !nested_dimension->maximum) {
                        return;
                    }
                    const auto count = static_cast<std::uint64_t>(
                        *nested_dimension->maximum
                        - *nested_dimension->minimum) + 1U;
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
            validate_aggregate_element(element, value, source);
        };
        const auto assign = [&](const std::int64_t index,
                                const semantic::ExpressionId value,
                                const semantic::SourceSpanId source) {
            if (index < minimum || index > maximum) {
                report(
                    "FSIM-ELAB-VHARRAYAGG-003",
                    "VHDL array aggregate index is outside the "
                        "contextual range",
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

        // Whether a range choice's value is an array of the element type,
        // as long as the range: a subtype of array profile, or a string
        // literal with one element per index.
        const auto slice_value = [&](const semantic::ExpressionId value,
                                     const std::int64_t left,
                                     const std::int64_t right) {
            const auto count = left > right
                ? static_cast<std::uint64_t>(left - right) + 1U
                : static_cast<std::uint64_t>(right - left) + 1U;
            if (count < 2U) {
                return false;
            }
            const auto expression = specialization_.find_expression(value);
            if (expression && expression->vhdl != nullptr
                && expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::string_literal) {
                const auto width = literal_width(value);
                return width && *width == count;
            }
            const auto subtype = expression_subtype(value);
            if (!subtype) {
                return false;
            }
            std::unordered_set<std::uint32_t> visiting;
            return profile(*subtype, visiting).kind == ProfileKind::array;
        };
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
            std::unordered_set<std::uint32_t> visiting;
            const auto prefix_profile = profile(*prefix, visiting);
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
            if (association.choice_spelling.empty()
                && association.choices.empty()) {
                assign(positional, association.value, association.source);
                positional += dimension.descending ? -1 : 1;
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
                    const auto left = bound(
                        std::nullopt, value.operands[0]);
                    const auto right = bound(
                        std::nullopt, value.operands[1]);
                    if (!left || !right) {
                        report(
                            "FSIM-ELAB-DYNINDEX-002",
                            "VHDL aggregate choice requires a locally "
                                "static integer index",
                            value.source);
                        return;
                    }
                    const auto step = value.text == "downto" ? -1 : 1;
                    // An array-valued range choice supplies a slice (IEEE
                    // 1076-2008 9.3.3.3); its indices are assigned without
                    // the element check.
                    const bool slice = slice_value(
                        association.value, *left, *right);
                    for (auto index = *left;; index += step) {
                        if (slice) {
                            if (index < minimum || index > maximum
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
                    if (bounds->null) {
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
                const auto index = bound(std::nullopt, choice_id);
                if (!index) {
                    report(
                        "FSIM-ELAB-DYNINDEX-002",
                        "VHDL aggregate choice requires a locally "
                            "static integer index",
                        value.source);
                    return;
                }
                assign(*index, association.value, association.source);
            };
            for (const auto choice_id : association.choices) {
                assign_choice(assign_choice, choice_id);
            }
        }
        if (!has_others && assigned.size() < element_count) {
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
        std::unordered_set<std::uint32_t> visiting;
        const auto context = profile(target, visiting);
        if (context.kind == ProfileKind::array) {
            const auto element = array_element_subtype(target);
            if (element) {
                validate_array_aggregate(
                    aggregate, context, *element);
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
        const auto attribute = expression.text == "'left"
            || expression.text == "'right"
            || expression.text == "'low"
            || expression.text == "'high"
            || expression.text == "'length"
            || expression.text == "'ascending"
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
        std::unordered_set<std::uint32_t> visiting;
        const auto prefix_profile = profile(*prefix, visiting);
        // Only an array attribute takes a dimension argument; 'LEFT, 'RIGHT,
        // 'LOW and 'HIGH of a scalar have none (IEEE 1076-1993 14.1).
        if (expression.operands.size() == 2U
            && (prefix_profile.kind == ProfileKind::scalar
                || prefix_profile.kind == ProfileKind::integer
                || prefix_profile.kind == ProfileKind::enumeration)
            && (expression.text == "'left" || expression.text == "'right"
                || expression.text == "'low" || expression.text == "'high")) {
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
            bool element { };
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
                element = true;
            }
            if (!prefix_expression || prefix_expression->vhdl == nullptr
                || !prefix_expression->vhdl->referenced_name
                || !prefix_expression->vhdl->referenced_name->selected) {
                return false;
            }
            const auto selected
                = *prefix_expression->vhdl->referenced_name->selected;
            if (element && subprogram_formal(selected)) {
                return true;
            }
            const auto declaration = specialization_.find_declaration(
                selected);
            // A constant with a value takes its index ranges from it
            // (IEEE 1076-2008 5.3.2.2).
            return declaration && declaration->vhdl != nullptr
                && (declaration->vhdl->form
                        == semantic::vhdl::DeclarationForm::port
                    || (declaration->vhdl->form
                            == semantic::vhdl::DeclarationForm::constant
                        && declaration->vhdl->initializer));
        };
        const auto contextual_result
            = prefix_profile.kind == ProfileKind::array
            && (contextual_result_prefix(
                    expression.operands.front(), *prefix)
                || interface_prefix());
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
    };

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
            if (resolver.resolve_vhdl_callables(
                    *value.referenced_name, value.scope)
                    .status
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
            const auto indexed_object = form == Form::generic_constant
                || form == Form::port || form == Form::signal
                || form == Form::constant || form == Form::variable
                || form == Form::alias;
            if (!indexed_object) {
                return std::nullopt;
            }
            const auto subtype = specialized_subtype(
                *declaration->vhdl->subtype);
            std::unordered_set<std::uint32_t> visiting;
            if (profile(subtype, visiting).kind
                != ProfileKind::array) {
                return std::nullopt;
            }
            return Selection {
                subtype, value.operands, value.source, false };
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
            std::unordered_set<std::uint32_t> visiting;
            if (profile(*subtype, visiting).kind
                != ProfileKind::array) {
                return std::nullopt;
            }
            base = Selection { *subtype, { }, value.source, false };
        }
        std::unordered_set<std::uint32_t> visiting;
        const auto base_profile = profile(base->subtype, visiting);
        if (base->slice_result) {
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
            base->selectors.push_back(value.operands[1]);
            base->slice_result = false;
        }
        base->source = value.source;
        return base;
    }

    void validate_selection(const semantic::ExpressionId id)
    {
        const auto selected = array_selection(id);
        if (!selected) {
            return;
        }
        std::unordered_set<std::uint32_t> visiting;
        const auto selected_profile = profile(
            selected->subtype, visiting);
        if (selected->selectors.size()
            > selected_profile.dimensions.size()) {
            report(
                "FSIM-ELAB-VHARRAYSEL-004",
                "VHDL array selection has incompatible contextual shape",
                selected->source);
            return;
        }
        // IEEE 1076-2008 8.4: an indexed name has one index per dimension,
        // each of the dimension's index type.
        const auto* array_definition = root_definition(selected->subtype);
        const bool any_slice = std::ranges::any_of(selected->selectors,
            [&](const semantic::ExpressionId selector_id) {
                const auto selector = specialization_.find_expression(selector_id);
                return selector && selector->vhdl != nullptr
                    && (selector->vhdl->kind == semantic::vhdl::ExpressionKind::slice
                        || (selector->vhdl->kind == semantic::vhdl::ExpressionKind::binary
                            && (selector->vhdl->text == "to"
                                || selector->vhdl->text == "downto")));
            });
        if (array_definition != nullptr && array_definition->form == TypeForm::array
            && !any_slice) {
            for (std::size_t index { };
                index < selected->selectors.size()
                && index < array_definition->array_dimensions.size();
                ++index) {
                const auto expected = dimension_index_type(
                    array_definition->array_dimensions[index], specialization_.scope());
                const auto actual = operand_type(selected->selectors[index]);
                if (expected.kind == OperandClass::unknown
                    || actual.kind == OperandClass::unknown) {
                    continue;
                }
                if (actual.kind != expected.kind
                    || roots_conflict(actual.root, expected.root)) {
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
            const auto slice
                = expression.kind
                    == semantic::vhdl::ExpressionKind::slice
                || (expression.kind
                        == semantic::vhdl::ExpressionKind::binary
                    && (expression.text == "to"
                        || expression.text == "downto"));
            if (slice) {
                const auto operands = expression.operands;
                const auto first = expression.kind
                        == semantic::vhdl::ExpressionKind::slice
                    ? 1U
                    : 0U;
                const auto left = operands.size() > first
                    ? bound(std::nullopt, operands[first])
                    : std::nullopt;
                const auto right = operands.size() > first + 1U
                    ? bound(std::nullopt, operands[first + 1U])
                    : std::nullopt;
                const auto descending = expression.text == "downto";
                const auto outside = left && right
                    && ((dimension.minimum
                            && (std::min(*left, *right)
                                < *dimension.minimum))
                        || (dimension.maximum
                            && (std::max(*left, *right)
                                > *dimension.maximum)));
                if (outside
                    || (dimension.constrained
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
                && index_profile->kind != ProfileKind::enumeration) {
                report(
                    "FSIM-ELAB-VHARRAYSEL-002",
                    "VHDL array index requires an integer-family signed "
                        "32-bit value with representable bounds",
                    expression.source);
                continue;
            }
            const auto value = bound(
                std::nullopt, selected->selectors[index]);
            if (value
                && ((dimension.minimum
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
                const auto scalar = [](const OperandClass kind) {
                    return kind == OperandClass::integer || kind == OperandClass::floating
                        || kind == OperandClass::physical || kind == OperandClass::enumeration;
                };
                if (scalar(expected.kind) && scalar(given.kind)
                    && (given.kind != expected.kind
                        || roots_conflict(given.root, expected.root))) {
                    report("FSIM-ELAB-VHASSIGN-001",
                        "the initial value of VHDL object '" + stored.name
                            + "' does not have its type",
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
            // IEEE 1076-2008 6.4.2.3, 6.4.2.4: a signal or variable has a
            // constrained (fully bounded) array subtype.
            if ((stored.form == Form::signal || stored.form == Form::variable)
                && unconstrained_array_subtype(*stored.subtype)) {
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
            if (statement.kind
                    == semantic::vhdl::StatementKind::signal_assignment
                && statement.target) {
                // IEEE 1076-2008 3.2.4: entity statements are passive.
                // A subprogram declared in the entity is not an entity
                // statement.
                const auto in_subprogram = [&](semantic::ScopeId scope) {
                    const auto& scopes = specialization_.design().semantics.scopes();
                    for (std::size_t depth { }; depth < 64U && scope.valid()
                         && scope.value() < scopes.size(); ++depth) {
                        if (std::ranges::any_of(
                                specialization_.design().vhdl_hir.declarations(),
                                [&](const semantic::vhdl::Declaration& candidate) {
                                    return candidate.callable
                                        && candidate.nested_scope == scope;
                                })) {
                            return true;
                        }
                        const auto parent = scopes[scope.value()].parent;
                        if (!parent) {
                            break;
                        }
                        scope = *parent;
                    }
                    return false;
                };
                if (const auto& all_scopes = specialization_.design().semantics.scopes();
                    statement.scope.valid() && statement.scope.value() < all_scopes.size()
                    && all_scopes[statement.scope.value()].unit == entity_.id
                    && entity_.id != architecture_.id
                    && !in_subprogram(statement.scope)) {
                    report("FSIM-ELAB-VHPASSIVE-001",
                        "a VHDL entity statement must be passive (it assigns a signal)",
                        statement.source);
                }
                validate_signal_target(*statement.target, statement.source);
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
                for (const auto bound : { statement.loop_initial, statement.loop_limit }) {
                    if (bound && operand_type(*bound).kind == OperandClass::floating) {
                        report("FSIM-ELAB-VHLOOP-001",
                            "a VHDL loop parameter range must be discrete",
                            statement.source);
                        break;
                    }
                }
                if (statement.loop_initial && statement.loop_limit) {
                    const auto left = operand_type(*statement.loop_initial);
                    const auto right = operand_type(*statement.loop_limit);
                    if (left.kind != OperandClass::unknown
                        && right.kind != OperandClass::unknown
                        && (left.kind != right.kind
                            || roots_conflict(left.root, right.root))) {
                        report("FSIM-ELAB-VHRANGE-001",
                            "the bounds of a VHDL range must have the same type",
                            statement.source);
                    }
                }
            }
            if (statement.kind == semantic::vhdl::StatementKind::loop
                && statement.loop_initial && !statement.loop_limit) {
                discrete_range_attributes.insert(
                    statement.loop_initial->value());
            }
        }
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
            if (expression.kind == semantic::vhdl::ExpressionKind::binary
                && expression.operands.size() == 2U
                && (expression.text == "=" || expression.text == "/="
                    || expression.text == "<" || expression.text == "<="
                    || expression.text == ">" || expression.text == ">=")
                && std::ranges::all_of(expression.operands, [&](const semantic::ExpressionId id) {
                       const auto operand = specialization_.find_expression(id);
                       return operand && operand->vhdl != nullptr
                           && operand->vhdl->kind == semantic::vhdl::ExpressionKind::aggregate;
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
            validate_conversion_and_qualification(expression);
            validate_allocator(expression);
            validate_call_arity(expression);
            if (name_check_
                && (expression.kind == semantic::vhdl::ExpressionKind::binary
                    || expression.kind == semantic::vhdl::ExpressionKind::unary
                    || (expression.kind == semantic::vhdl::ExpressionKind::call
                        && expression.operands.size() == 1U
                        && expression.text.starts_with("@vhdl-qualified:")))) {
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
            if (name_check_) {
                for (const auto* map : { &instance.generic_map, &instance.port_map }) {
                    for (const auto& association : *map) {
                        if (association.kind == semantic::vhdl::AssociationKind::expression
                            && association.expression) {
                            validate_value_name(*association.expression);
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
            const auto target_subtype
                = expression_subtype(*statement.target);
            if (value_expression
                && value_expression->vhdl != nullptr
                && value_expression->vhdl->kind
                    == semantic::vhdl::ExpressionKind::aggregate
                && target_subtype) {
                validate_aggregate(
                    *value_expression->vhdl, *target_subtype);
            }
            if (!value_expression
                || value_expression->vhdl == nullptr
                || value_expression->vhdl->kind
                    != semantic::vhdl::ExpressionKind::aggregate
                || !target_subtype) {
                continue;
            }
            const auto element = array_element_subtype(*target_subtype);
            if (!element) {
                continue;
            }
            std::unordered_set<std::uint32_t> visiting;
            const auto target_profile = profile(
                *target_subtype, visiting);
            if (target_profile.kind != ProfileKind::array
                || target_profile.dimensions.size() != 1U) {
                continue;
            }
            for (const auto& association :
                value_expression->vhdl->associations) {
                if (!aggregate_element_compatible(
                        *element, association.value)) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-009",
                        "VHDL array aggregate element requires its exact "
                            "contextual subtype",
                        association.source);
                }
            }
        }
    }

    const semantic::SpecializedHirUnit& specialization_;
    const semantic::vhdl::Unit& entity_;
    const semantic::vhdl::Unit& architecture_;
    std::unordered_set<std::uint32_t> relevant_types_;
    bool name_check_ { true };
    bool operator_check_ { true };
    std::optional<std::unordered_set<std::uint32_t>> signal_formals_;
    std::optional<std::unordered_set<std::string>> exempt_names_;
    std::unordered_set<std::string> reported_;
    std::vector<VhdlHirTypeValidationIssue> issues_;
};

} // namespace

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
    for (const auto& issue :
        Validator { *specialization, *entity, *architecture }.run()) {
        // Without generic values only name rules are decidable here.
        if (issue.code != "FSIM-ELAB-VHNAME-002"
            && issue.code != "FSIM-ELAB-VHNAME-004"
            && issue.code != "FSIM-ELAB-VHNAME-015") {
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
