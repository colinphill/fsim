// SPDX-License-Identifier: Apache-2.0
#include "vhdl_hir_type_validation.hpp"

#include "fsim/semantic/compiled_design_resolver.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

namespace fsim::elaboration {
namespace {

using semantic::vhdl::RangeConstraint;
using semantic::vhdl::RangeKind;
using semantic::vhdl::SubtypeIndication;
using semantic::vhdl::TypeDefinition;
using semantic::vhdl::TypeForm;

bool name_equal(const std::string_view left, const std::string_view right)
{
    return std::ranges::equal(
        left, right, [](const char lhs, const char rhs) {
            return std::tolower(static_cast<unsigned char>(lhs))
                == std::tolower(static_cast<unsigned char>(rhs));
        });
}

std::string_view simple_name(const std::string_view spelling)
{
    const auto separator = spelling.find_last_of(".:");
    return spelling.substr(separator == std::string_view::npos
            ? 0U
            : separator + 1U);
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
        collect_unit_types(entity_);
        collect_unit_types(architecture_);
        validate_unit_objects(entity_);
        validate_unit_objects(architecture_);
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
        const auto name = simple_name(spelling);
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
                            || !name_equal(type->vhdl->name, name)
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
                || !name_equal(type->vhdl->name, name)) {
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
        const auto name = simple_name(spelling);
        if (name_equal(name, "natural")) {
            return { 0, std::numeric_limits<std::int64_t>::max(), false,
                false };
        }
        if (name_equal(name, "positive")) {
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
        const auto name = simple_name(spelling);
        if (name_equal(name, "integer")) {
            return { ProfileKind::integer, std::nullopt, std::nullopt,
                { }, false };
        }
        if (name_equal(name, "natural")) {
            return { ProfileKind::integer, 0,
                std::numeric_limits<std::int64_t>::max(), { }, false };
        }
        if (name_equal(name, "positive")) {
            return { ProfileKind::integer, 1,
                std::numeric_limits<std::int64_t>::max(), { }, false };
        }
        if (name_equal(name, "string")) {
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
                    return name_equal(name, candidate);
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
        };
        if (std::ranges::any_of(scalar_names,
                [&](const auto candidate) {
                    return name_equal(name, candidate);
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

    void validate_type_definition(const TypeDefinition& type)
    {
        if (type.form == TypeForm::subtype
            || type.form == TypeForm::alias) {
            validate_constraints(type.base, type.source);
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
        if (element.kind == ProfileKind::unknown
            || element.kind == ProfileKind::string
            || (element.kind == ProfileKind::array
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
        if (value.referenced_name
            && value.referenced_name->selected) {
            const auto declaration = specialization_.find_declaration(
                *value.referenced_name->selected);
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
                            return name_equal(element.name, member_name);
                        });
                    if (member == record->record_elements.end()) {
                        return std::nullopt;
                    }
                    subtype = specialized_subtype(member->subtype);
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
            if (base->constraints.size() > 1U) {
                auto reduced = *base;
                reduced.constraints.erase(reduced.constraints.begin());
                reduced.executable_width.reset();
                reduced.unconstrained = false;
                return specialized_subtype(std::move(reduced));
            }
            return array_element_subtype(*base);
        }
        return std::nullopt;
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
                    ? std::optional {
                          specialized_subtype(*definition->element_subtype) }
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
        if (name_equal(simple_name(association.choice_spelling),
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
                       && name_equal(choice->vhdl->text, "others");
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
            return 1U;
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
                            return name_equal(
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
                    for (auto index = *left;; index += step) {
                        assign(
                            index, association.value,
                            association.source);
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
        if (prefix_profile.kind == ProfileKind::unknown
            || (prefix_profile.kind != ProfileKind::array
                && prefix_profile.kind != ProfileKind::record)) {
            return;
        }
        if (prefix_profile.kind == ProfileKind::array
            && prefix_profile.dimensions.empty()) {
            return;
        }
        const auto contextual_result
            = prefix_profile.kind == ProfileKind::array
            && contextual_result_prefix(
                expression.operands.front(), *prefix);
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
            if (index_profile
                && index_profile->kind != ProfileKind::integer) {
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
                            ? name_equal(
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
            if (statement.kind == semantic::vhdl::StatementKind::loop
                && statement.loop_initial && !statement.loop_limit) {
                discrete_range_attributes.insert(
                    statement.loop_initial->value());
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
        for (const auto& stored :
            specialization_.design().vhdl_hir.expressions()) {
            const auto effective = specialization_.find_expression(
                stored.id);
            if (!effective || effective->vhdl == nullptr
                || !selected_scope(effective->vhdl->scope)) {
                continue;
            }
            const auto& expression = *effective->vhdl;
            if (architecture_.standard != "2019"
                && expression.kind
                    == semantic::vhdl::ExpressionKind::integer_literal
                && !negated_integer_minimum_literals.contains(
                    expression.id.value())
                && !name_equal(
                    simple_name(expression.nominal_type), "time")) {
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
                validate_selection(expression.id);
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
            if (relational && (left || right)) {
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
        for (const auto& stored :
            specialization_.design().vhdl_hir.statements()) {
            const auto effective = specialization_.find_statement(
                stored.id);
            if (!effective || effective->vhdl == nullptr
                || !selected_scope(effective->vhdl->scope)) {
                continue;
            }
            const auto& statement = *effective->vhdl;
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

} // namespace fsim::elaboration
