// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_type_layout_internal.hpp"

#include "elaborator_internal.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "hierarchy_sv_constant_evaluator.hpp"
#include "hierarchy_sv_parameters_internal.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <exception>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fsim::elaboration::hierarchy_sv_type_layout_detail {
using namespace runtime::simir;
using namespace elaboration_detail;
using namespace hierarchy_sv_parameters_detail;

namespace {

// The type-parameter formal a type reference names: the one formal whose
// declared type is the reference's target among the specialization's
// actuals, else the type parameter its spelling resolves to in `scope`.
// Empty when none is found or the target matches several formals.
// `name_only_without_target` limits the name lookup to an unresolved target.
std::optional<semantic::DeclarationId> specialized_type_parameter_formal(
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::CompiledDesignResolver& resolver,
    const semantic::sv::TypeReference& reference,
    const semantic::ScopeId scope, const bool name_only_without_target)
{
    std::optional<semantic::DeclarationId> formal_id;
    if (reference.target.target.valid()) {
        for (const auto& actual :
            working_specialization.specialization().actual_identities) {
            const auto formal = working_specialization.find_declaration(
                actual.declaration);
            if (!formal || formal->systemverilog == nullptr
                || formal->systemverilog->form
                    != semantic::sv::DeclarationForm::type_parameter
                || !formal->systemverilog->declared_type
                || *formal->systemverilog->declared_type
                    != reference.target.target) {
                continue;
            }
            if (formal_id && *formal_id != actual.declaration) {
                return std::nullopt;
            }
            formal_id = actual.declaration;
        }
    }
    if (formal_id || reference.target.spelling.empty()
        || (name_only_without_target && reference.target.target.valid())) {
        return formal_id;
    }
    const auto named = resolver
        .resolve_systemverilog_named_type(
            reference.target.spelling, scope, false)
        .unique();
    const auto formal = named
        ? working_specialization.find_declaration(*named)
        : std::nullopt;
    return formal && formal->systemverilog != nullptr
            && formal->systemverilog->form
                == semantic::sv::DeclarationForm::type_parameter
        ? named
        : std::nullopt;
}

} // namespace

std::optional<std::int64_t> sized_range_bound(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::ExpressionId expression)
{
    // A range bound is a self-determined constant expression: sized operands
    // keep their widths (IEEE 1800-2017 11.6.1, 11.8.1). Only a bound with a
    // sized literal operand can wrap, so the width-aware evaluator decides
    // just those.
    const auto sized_literal = [&](const auto& self,
                                   const semantic::ExpressionId id,
                                   const std::size_t depth) -> bool {
        const auto view = specialization.find_expression(id);
        if (!view || view->systemverilog == nullptr || depth > 64U) {
            return false;
        }
        const auto& source = *view->systemverilog;
        const auto quote = source.text.find('\'');
        if ((source.kind == semantic::sv::ExpressionKind::integer_literal
                || source.kind == semantic::sv::ExpressionKind::logic_literal)
            && quote != std::string::npos && quote != 0U) {
            return true;
        }
        return std::ranges::any_of(source.operands,
            [&](const semantic::ExpressionId operand) {
                return self(self, operand, depth + 1U);
            });
    };
    const auto width_aware = [&]() -> std::optional<std::int64_t> {
        std::string error;
        if (const auto value = evaluate_hir_systemverilog_constant(
                specialization, expression, error);
            value && !value->unbounded) {
            return value->integer_value();
        }
        return std::nullopt;
    };
    if (sized_literal(sized_literal, expression, 0U)) {
        if (const auto integer = width_aware()) {
            return integer;
        }
    }
    if (const auto integer
        = specialization.evaluate_integral_expression(expression)) {
        return integer;
    }
    // An operand wider than 64 bits (a wide parameter) still folds to a
    // bound when evaluated at its own width.
    return width_aware();
}

std::optional<std::size_t> systemverilog_declaration_width(
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::sv::Declaration& declaration)
{
    if (!declaration.interface_type.empty()
        || (declaration.type
            && declaration.type->target.spelling == "interface")) {
        return 64U;
    }
    if (!declaration.type) {
        return std::nullopt;
    }
    const auto boundary = [&](const std::optional<std::int64_t> value,
                              const std::optional<semantic::ExpressionId>
                                  expression) {
        return expression
            ? sized_range_bound(working_specialization, *expression)
            : value;
    };
    std::unordered_set<std::uint32_t> visiting;
    const semantic::CompiledDesignResolver type_resolver {
        working_specialization
    };
    // Use a width stamped on the actual for its exact type-parameter formal
    // before resolver expansion replaces that formal with its base type.
    const auto specialized_type_parameter_actual_width =
        [&working_specialization, &type_resolver, &declaration, &boundary](
            const semantic::sv::TypeReference& reference)
        -> std::optional<std::size_t> {
        if (reference.container_form
            || !reference.unpacked_dimensions.empty()) {
            return std::nullopt;
        }

        const auto formal_id = specialized_type_parameter_formal(
            working_specialization, type_resolver, reference,
            declaration.scope, false);
        if (!formal_id) {
            return std::nullopt;
        }

        const auto& actuals
            = working_specialization.specialization().actual_identities;
        const auto actual = std::ranges::find(actuals, *formal_id,
            &semantic::SpecializedHirActualIdentity::declaration);
        if (actual == actuals.end() || !actual->systemverilog_type
            || actual->systemverilog_type->container_form
            || !actual->systemverilog_type->unpacked_dimensions.empty()
            || !actual->systemverilog_type->executable_width
            || *actual->systemverilog_type->executable_width == 0U
            || *actual->systemverilog_type->executable_width
                > std::numeric_limits<std::size_t>::max()) {
            return std::nullopt;
        }
        auto width = static_cast<std::size_t>(
            *actual->systemverilog_type->executable_width);
        std::size_t extent { 1U };
        const auto include_range =
            [&](const semantic::sv::PackedRange& range) {
                const auto left = boundary(
                    range.left, range.left_expression);
                const auto right = boundary(
                    range.right, range.right_expression);
                if (!left || !right) {
                    return false;
                }
                const auto distance = elaboration_detail::index_distance(
                    *left, *right);
                if (distance >= std::numeric_limits<std::size_t>::max()) {
                    return false;
                }
                const auto dimension_extent
                    = static_cast<std::size_t>(distance + 1U);
                if (dimension_extent == 0U
                    || dimension_extent
                        > std::numeric_limits<std::size_t>::max() / extent) {
                    return false;
                }
                extent *= dimension_extent;
                return true;
            };
        for (const auto& dimension : reference.packed_dimensions) {
            if (!include_range(dimension)) {
                return std::nullopt;
            }
        }
        // The actual's own flat range, projected onto the formal reference,
        // is already its width (7.2.1, 6.20.3).
        const auto& actual_type = *actual->systemverilog_type;
        const bool range_is_actual_width = reference.packed_range
            && actual_type.packed_range
            && actual_type.packed_dimensions.empty()
            && reference.packed_range->left == actual_type.packed_range->left
            && reference.packed_range->right
                == actual_type.packed_range->right
            && !reference.packed_range->left_expression
            && !reference.packed_range->right_expression
            && actual_type.packed_range->left
            && actual_type.packed_range->right
            && elaboration_detail::index_distance(
                   *actual_type.packed_range->left,
                   *actual_type.packed_range->right)
                    + 1U
                == width;
        if (reference.packed_dimensions.empty()
            && reference.packed_range && !range_is_actual_width
            && !include_range(*reference.packed_range)) {
            return std::nullopt;
        }
        if (extent > std::numeric_limits<std::size_t>::max() / width) {
            return std::nullopt;
        }
        width *= extent;
        return width;
    };
    std::size_t tagged_member_depth { };
    const auto resolve = [&](const auto& self,
                             const semantic::sv::TypeReference& reference)
        -> std::optional<std::size_t> {
        if (reference.virtual_interface) {
            return 64U;
        }
        // Dimensions over a type parameter compose with its actual here.
        if (reference.packed_over_named_type) {
            const auto effective
                = type_resolver.effective_systemverilog_type(
                    reference, declaration.scope);
            if (!effective || effective->packed_over_named_type) {
                return std::nullopt;
            }
            return self(self, *effective);
        }
        std::optional<std::size_t> width
            = specialized_type_parameter_actual_width(reference);
        if (!width && reference.packed_range) {
            const auto& range = *reference.packed_range;
            const auto left = boundary(
                range.left, range.left_expression);
            const auto right = boundary(
                range.right, range.right_expression);
            if (!left || !right) {
                return std::nullopt;
            }
            const auto distance = elaboration_detail::index_distance(
                *left, *right);
            if (distance < std::numeric_limits<std::size_t>::max()) {
                width = static_cast<std::size_t>(distance + 1U);
            }
        }
        if (!width) {
            const auto effective
                = type_resolver.effective_systemverilog_type(
                    reference, declaration.scope);
            if (effective && *effective != reference) {
                return self(self, *effective);
            }
        }
        if (!width && reference.target.target.valid()) {
            if (visiting.insert(reference.target.target.value()).second) {
                const auto definition = working_specialization.find_type(
                    reference.target.target);
                if (definition && definition->systemverilog != nullptr) {
                    const auto& record = *definition->systemverilog;
                    // A tagged union is laid out as tag and payload; an
                    // unpacked structure member of one shares that bit
                    // layout (the representation is not observable,
                    // IEEE 1800-2017 7.3.2).
                    const bool packed_aggregate
                        = record.form
                            == semantic::sv::TypeForm::packed_structure
                        || record.form
                            == semantic::sv::TypeForm::packed_union
                        || (record.form
                                == semantic::sv::TypeForm::tagged_union
                            && !systemverilog_tagged_union_aggregate(
                                working_specialization, record))
                        || (tagged_member_depth != 0U
                            && record.form
                                == semantic::sv::TypeForm::unpacked_structure);
                    if (packed_aggregate && !record.members.empty()) {
                        std::size_t aggregate_width { };
                        bool valid = true;
                        const bool union_layout
                            = record.form
                                == semantic::sv::TypeForm::packed_union
                            || record.form
                                == semantic::sv::TypeForm::tagged_union;
                        const bool tagged
                            = record.form
                            == semantic::sv::TypeForm::tagged_union;
                        tagged_member_depth += tagged ? 1U : 0U;
                        for (const auto& member : record.members) {
                            const auto member_width
                                = self(self, member.type);
                            if (!member_width || *member_width == 0U
                                || (!union_layout
                                    && *member_width
                                        > std::numeric_limits<
                                              std::size_t>::max()
                                            - aggregate_width)) {
                                valid = false;
                                break;
                            }
                            if (union_layout) {
                                aggregate_width = std::max(
                                    aggregate_width, *member_width);
                            } else {
                                aggregate_width += *member_width;
                            }
                        }
                        tagged_member_depth -= tagged ? 1U : 0U;
                        if (valid
                            && record.form
                                == semantic::sv::TypeForm::tagged_union) {
                            const auto tag_width
                                = std::max<std::size_t>(
                                    1U,
                                    static_cast<std::size_t>(
                                        std::bit_width(
                                            record.members.size() - 1U)));
                            if (tag_width
                                > std::numeric_limits<std::size_t>::max()
                                    - aggregate_width) {
                                valid = false;
                            } else {
                                aggregate_width += tag_width;
                            }
                        }
                        if (valid && aggregate_width != 0U) {
                            width = aggregate_width;
                        }
                    }
                    if (!width && !packed_aggregate) {
                        width = self(self, record.base);
                    }
                    visiting.erase(reference.target.target.value());
                } else {
                    visiting.erase(reference.target.target.value());
                }
            }
        }
        if (!width && reference.executable_width) {
            const auto executable = *reference.executable_width;
            if (executable == 0U
                || executable > std::numeric_limits<std::size_t>::max()) {
                return std::nullopt;
            }
            width = static_cast<std::size_t>(executable);
        }
        if (!width) {
            width = 1U;
        }
        if (!width
            || reference.container_form
                != semantic::sv::TypeForm::static_array) {
            return width;
        }
        for (const auto& dimension : reference.unpacked_dimensions) {
            const auto left = boundary(
                dimension.left, dimension.left_expression);
            const auto right = boundary(
                dimension.right, dimension.right_expression);
            if (!left || !right) {
                return std::nullopt;
            }
            const auto count
                = elaboration_detail::index_distance(*left, *right) + 1U;
            if (count == 0U
                || count
                    > std::numeric_limits<std::size_t>::max() / *width) {
                return std::nullopt;
            }
            *width *= static_cast<std::size_t>(count);
        }
        return width;
    };
    return resolve(resolve, *declaration.type);
}

bool systemverilog_tagged_union_aggregate(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeDefinition& type)
{
    if (type.form != semantic::sv::TypeForm::tagged_union) {
        return false;
    }
    const semantic::CompiledDesignResolver resolver { specialization };
    std::unordered_set<std::uint32_t> visiting;
    const auto unpacked = [&](const auto& self,
                              const semantic::sv::TypeDefinition& record)
        -> bool {
        const auto declaration = record.declaration.valid()
            ? specialization.find_declaration(record.declaration)
            : std::nullopt;
        const auto scope = declaration && declaration->systemverilog != nullptr
            ? declaration->systemverilog->scope
            : specialization.scope();
        return std::ranges::any_of(record.members,
            [&](const semantic::sv::PackedMember& member) {
                const auto effective
                    = resolver.effective_systemverilog_type(member.type, scope)
                          .value_or(member.type);
                const auto& spelling = effective.target.spelling;
                if (effective.container_form
                    || !effective.unpacked_dimensions.empty()
                    || effective.value_form == semantic::sv::TypeForm::string
                    || spelling == "string" || spelling == "real"
                    || spelling == "shortreal" || spelling == "realtime") {
                    return true;
                }
                if (!effective.target.target.valid()
                    || !visiting.insert(effective.target.target.value())
                            .second) {
                    return false;
                }
                const auto definition
                    = specialization.find_type(effective.target.target);
                if (!definition || definition->systemverilog == nullptr) {
                    return false;
                }
                // A nested structure or union is laid out with the tagged
                // union unless one of its own members has no packed form.
                const auto form = definition->systemverilog->form;
                return (form == semantic::sv::TypeForm::unpacked_structure
                           || form == semantic::sv::TypeForm::unpacked_union
                           || form == semantic::sv::TypeForm::tagged_union)
                    && self(self, *definition->systemverilog);
            });
    };
    return unpacked(unpacked, type);
}

std::optional<runtime::SystemVerilogVpiTypeDescriptor>
systemverilog_vpi_declared_type(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeReference& reference,
    const semantic::ScopeId scope)
{
    using Descriptor = runtime::SystemVerilogVpiTypeDescriptor;
    using Kind = runtime::SystemVerilogVpiDescriptorKind;
    using Category = runtime::SystemVerilogVpiValueCategory;
    using Form = semantic::sv::TypeForm;
    const semantic::CompiledDesignResolver resolver { specialization };
    const auto bound = [&](const std::optional<std::int64_t> value,
                           const std::optional<semantic::ExpressionId> expression)
        -> std::optional<std::int64_t> {
        return expression ? sized_range_bound(specialization, *expression)
                          : value;
    };
    const auto scope_of = [&](const semantic::sv::TypeDefinition& record) {
        const auto declaration = record.declaration.valid()
            ? specialization.find_declaration(record.declaration)
            : std::nullopt;
        return declaration && declaration->systemverilog != nullptr
            ? declaration->systemverilog->scope
            : specialization.scope();
    };
    const auto shape = [&](const auto& self,
                           const semantic::sv::TypeReference& input,
                           const semantic::ScopeId use_scope,
                           const unsigned depth) -> std::optional<Descriptor> {
        if (depth > 32U || input.virtual_interface
            || !input.class_identity.empty() || input.type_of) {
            return std::nullopt;
        }
        // A fixed unpacked array of its element type, its dimensions
        // outermost first (IEEE 1800-2023 7.4.2).
        if (!input.unpacked_dimensions.empty()
            && (!input.container_form
                || *input.container_form == Form::static_array)) {
            auto element_type = input;
            element_type.unpacked_dimensions.clear();
            element_type.container_form.reset();
            element_type.container_element_types.clear();
            if (element_type.value_form == Form::static_array) {
                element_type.value_form.reset();
            }
            auto element = self(self, element_type, use_scope, depth + 1U);
            if (!element) {
                return std::nullopt;
            }
            Descriptor array;
            array.kind = Kind::UnpackedArray;
            array.category = Category::None;
            array.width = 0U;
            for (const auto& range : input.unpacked_dimensions) {
                const auto left = bound(range.left, range.left_expression);
                const auto right = bound(range.right, range.right_expression);
                if (!left || !right) {
                    return std::nullopt;
                }
                array.ranges.push_back({ *left, *right });
            }
            array.children.push_back(std::move(*element));
            return array;
        }
        if (input.container_form || !input.unpacked_dimensions.empty()) {
            return std::nullopt;
        }
        if (const auto effective
            = resolver.effective_systemverilog_type(input, use_scope);
            effective && *effective != input) {
            return self(self, *effective, use_scope, depth + 1U);
        }
        if (input.packed_over_named_type) {
            return std::nullopt;
        }
        const auto definition = input.target.target.valid()
            ? specialization.find_type(input.target.target)
            : std::nullopt;
        const auto* const record = definition
            ? definition->systemverilog
            : nullptr;
        // A resolved named type's reference carries its flattened width as
        // its packed range and as the last packed dimension; the dimensions
        // declared over it come first.
        std::vector<std::pair<std::int64_t, std::int64_t>> dimensions;
        auto declared = input.packed_dimensions;
        if (record != nullptr) {
            if (!declared.empty()) {
                declared.pop_back();
            }
        } else if (declared.empty() && input.packed_range) {
            declared.push_back(*input.packed_range);
        }
        std::uint64_t repeat = 1U;
        for (const auto& range : declared) {
            const auto left = bound(range.left, range.left_expression);
            const auto right = bound(range.right, range.right_expression);
            if (!left || !right) {
                return std::nullopt;
            }
            dimensions.emplace_back(*left, *right);
            repeat *= elaboration_detail::index_distance(*left, *right) + 1U;
        }
        std::optional<Descriptor> element;
        if (record != nullptr) {
            const auto record_scope = scope_of(*record);
            switch (record->form) {
            case Form::alias:
            case Form::type_parameter:
            case Form::packed_integral:
                element = self(self, record->base, record_scope, depth + 1U);
                if (!element) {
                    return std::nullopt;
                }
                break;
            case Form::packed_structure:
            case Form::packed_union:
            case Form::unpacked_structure:
            case Form::unpacked_union: {
                Descriptor aggregate;
                aggregate.kind = record->form == Form::packed_union
                        || record->form == Form::unpacked_union
                    ? Kind::Union
                    : Kind::Struct;
                aggregate.category = Category::None;
                aggregate.width = 0U;
                aggregate.packed = record->form == Form::packed_structure
                    || record->form == Form::packed_union;
                aggregate.is_signed = aggregate.packed && record->base.signed_value;
                for (const auto& member : record->members) {
                    auto child = self(self, member.type, record_scope, depth + 1U);
                    if (!child) {
                        return std::nullopt;
                    }
                    aggregate.children.push_back(std::move(*child));
                    aggregate.member_names.push_back(member.name);
                }
                if (aggregate.children.empty()) {
                    return std::nullopt;
                }
                element = std::move(aggregate);
                break;
            }
            case Form::enumeration: {
                // An enumeration without a base type is an int (6.19).
                auto base = record->base.target.resolved()
                        || !record->base.target.spelling.empty()
                    ? self(self, record->base, record_scope, depth + 1U)
                    : std::optional<Descriptor> { };
                if (!base) {
                    base.emplace();
                    base->category = Category::Integer2;
                    base->width = 32U;
                    base->is_signed = true;
                }
                if (base->kind != Kind::Scalar || base->width > 64U) {
                    return std::nullopt;
                }
                base->kind = Kind::Enum;
                // A literal without a value is the previous one plus one,
                // the first one 0 (6.19).
                std::int64_t next = 0;
                for (const auto& literal : record->enumeration_literals) {
                    if (literal.value) {
                        const auto value = specialization
                            .evaluate_integral_expression(*literal.value);
                        if (!value) {
                            return std::nullopt;
                        }
                        next = *value;
                    }
                    base->enum_literals.push_back({ literal.name,
                        runtime::PackedLogic4::from_aval_bval(base->width,
                            static_cast<std::uint64_t>(next), 0U) });
                    ++next;
                }
                element = std::move(base);
                break;
            }
            case Form::string:
                element.emplace();
                element->kind = Kind::String;
                element->category = Category::String;
                element->width = 0U;
                break;
            default:
                return std::nullopt;
            }
        }
        if (!element) {
            const auto& spelling = input.target.spelling;
            if (input.value_form == Form::string || spelling == "string") {
                if (!dimensions.empty()) {
                    return std::nullopt;
                }
                Descriptor text;
                text.kind = Kind::String;
                text.category = Category::String;
                text.width = 0U;
                return text;
            }
            // An integer atom has its own width; its packed range is the
            // implicit one (6.11).
            const std::uint32_t atom = spelling == "byte" ? 8U
                : spelling == "shortint"                  ? 16U
                : spelling == "int" || spelling == "integer" ? 32U
                : spelling == "longint" || spelling == "time" ? 64U
                                                          : 0U;
            // An enumeration's base type keeps the enumeration form.
            if (atom == 0U && input.value_form
                && *input.value_form != Form::packed_integral
                && *input.value_form != Form::enumeration) {
                return std::nullopt;
            }
            if (atom == 0U && !spelling.empty() && spelling != "bit"
                && spelling != "logic" && spelling != "reg"
                && !semantic::sv::is_builtin_net_kind(spelling)) {
                return std::nullopt;
            }
            Descriptor scalar;
            scalar.category = spelling == "time" ? Category::Time
                : atom != 0U
                ? (spelling == "integer" || input.four_state
                          ? Category::Integer4
                          : Category::Integer2)
                : input.four_state || spelling != "bit"
                ? Category::Logic4
                : Category::Bit2;
            const auto width = atom != 0U ? std::uint64_t { atom } : repeat;
            if (width > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            scalar.width = static_cast<std::uint32_t>(width);
            scalar.is_signed = input.signed_value;
            return scalar;
        }
        if (dimensions.empty()) {
            return element;
        }
        if (element->kind == Kind::Scalar) {
            const auto width = std::uint64_t { element->width } * repeat;
            if (width > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            element->width = static_cast<std::uint32_t>(width);
            return element;
        }
        if (element->kind != Kind::Enum && !element->packed) {
            return std::nullopt;
        }
        // A packed array of structures, unions or enumerations (37.18).
        Descriptor array;
        array.kind = Kind::PackedArray;
        array.category = Category::None;
        array.width = 0U;
        for (const auto& [left, right] : dimensions) {
            array.ranges.push_back({ left, right });
        }
        array.children.push_back(std::move(*element));
        return array;
    };
    return shape(shape, reference, scope, 0U);
}

std::optional<runtime::SystemVerilogVpiTypespec> systemverilog_vpi_typespec(
    const semantic::SpecializedHirUnit& specialization,
    const semantic::sv::TypeReference& reference, const semantic::ScopeId scope)
{
    using Typespec = runtime::SystemVerilogVpiTypespec;
    using Kind = runtime::SystemVerilogVpiTypespecKind;
    using ArrayKind = runtime::SystemVerilogVpiArrayKind;
    using Form = semantic::sv::TypeForm;
    const semantic::CompiledDesignResolver resolver { specialization };
    const auto range_of = [&](const semantic::sv::PackedRange& range)
        -> std::optional<runtime::SystemVerilogVpiRange> {
        const auto left = range.left_expression
            ? sized_range_bound(specialization, *range.left_expression)
            : range.left;
        const auto right = range.right_expression
            ? sized_range_bound(specialization, *range.right_expression)
            : range.right;
        if (!left || !right) {
            return std::nullopt;
        }
        return runtime::SystemVerilogVpiRange { *left, *right };
    };
    const auto scope_of = [&](const semantic::sv::TypeDefinition& record) {
        const auto declaration = record.declaration.valid()
            ? specialization.find_declaration(record.declaration)
            : std::nullopt;
        return declaration && declaration->systemverilog != nullptr
            ? declaration->systemverilog->scope
            : specialization.scope();
    };
    // The bits of an integral or packed typespec.
    const auto bits = [](const auto& self, const Typespec& type) -> std::uint64_t {
        switch (type.kind) {
        case Kind::Struct:
        case Kind::Union: {
            std::uint64_t result = 0U;
            for (const auto& member : type.members) {
                const auto member_bits = self(self, member);
                result = type.kind == Kind::Struct
                    ? result + member_bits
                    : std::max(result, member_bits);
            }
            return result;
        }
        case Kind::Enum:
            return type.element.empty() ? 32U : self(self, type.element.front());
        case Kind::PackedArray: {
            auto result = type.element.empty() ? 0U
                                               : self(self, type.element.front());
            for (const auto& range : type.ranges) {
                result *= elaboration_detail::index_distance(
                              range.left, range.right)
                    + 1U;
            }
            return result;
        }
        case Kind::Byte:
            return 8U;
        case Kind::ShortInt:
            return 16U;
        case Kind::Int:
        case Kind::Integer:
            return 32U;
        case Kind::LongInt:
        case Kind::Time:
            return 64U;
        case Kind::Logic:
        case Kind::Bit: {
            std::uint64_t result = 1U;
            for (const auto& range : type.ranges) {
                result *= elaboration_detail::index_distance(
                              range.left, range.right)
                    + 1U;
            }
            return result;
        }
        default:
            return 0U;
        }
    };
    // Binding composes a typedef's own dimensions into the reference that
    // names it; they belong to the named level.
    const auto drop_named = [](std::vector<runtime::SystemVerilogVpiRange>& ranges,
                                const Typespec& named) {
        if (named.name.empty() || named.ranges.empty()
            || named.ranges.size() > ranges.size()
            || !std::equal(named.ranges.begin(), named.ranges.end(),
                ranges.end()
                    - static_cast<std::ptrdiff_t>(named.ranges.size()))) {
            return;
        }
        ranges.resize(ranges.size() - named.ranges.size());
    };
    const auto build = [&](const auto& self,
                           const semantic::sv::TypeReference& input,
                           const semantic::ScopeId use_scope,
                           const unsigned depth) -> std::optional<Typespec> {
        if (depth > 32U || input.virtual_interface || input.type_of) {
            return std::nullopt;
        }
        // An unpacked dimension, leftmost first, or a dynamic array, queue
        // or associative array (IEEE 1800-2023 7.4, 7.5, 7.8, 7.10).
        if (input.container_form || !input.unpacked_dimensions.empty()) {
            Typespec array;
            array.kind = Kind::Array;
            auto element = input;
            if (input.container_form) {
                array.array_kind
                    = *input.container_form == Form::dynamic_array
                    ? ArrayKind::Dynamic
                    : *input.container_form == Form::queue ? ArrayKind::Queue
                    : *input.container_form == Form::associative_array
                    ? ArrayKind::Associative
                    : ArrayKind::Static;
                if (array.array_kind != ArrayKind::Static) {
                    if (!input.container_element_types.empty()) {
                        element = input.container_element_types.front();
                    } else {
                        element.container_form.reset();
                    }
                    auto inner = self(self, element, use_scope, depth + 1U);
                    if (!inner) {
                        return std::nullopt;
                    }
                    array.element.push_back(std::move(*inner));
                    // The key type, unless the index is the wildcard (7.8.1).
                    if (array.array_kind == ArrayKind::Associative
                        && input.associative_index) {
                        semantic::sv::TypeReference index_reference
                            = input.associative_index_metadata
                            ? semantic::sv::
                                type_reference_from_associative_index_metadata(
                                    *input.associative_index_metadata)
                            : semantic::sv::TypeReference { };
                        index_reference.target = *input.associative_index;
                        if (auto key = self(
                                self, index_reference, use_scope, depth + 1U)) {
                            array.index.push_back(std::move(*key));
                        }
                    }
                    return array;
                }
                element.container_form.reset();
            }
            for (const auto& range : input.unpacked_dimensions) {
                const auto bounds = range_of(range);
                if (!bounds) {
                    return std::nullopt;
                }
                array.ranges.push_back(*bounds);
            }
            element.unpacked_dimensions.clear();
            auto inner = self(self, element, use_scope, depth + 1U);
            if (!inner) {
                return std::nullopt;
            }
            if (inner->kind == Kind::Array) {
                drop_named(array.ranges, *inner);
                if (array.ranges.empty()) {
                    return inner;
                }
            }
            array.element.push_back(std::move(*inner));
            return array;
        }
        const auto definition = input.target.target.valid()
            ? specialization.find_type(input.target.target)
            : std::nullopt;
        const auto* const record = definition
            ? definition->systemverilog
            : nullptr;
        // A bound typedef is followed through its own record, which keeps
        // its name; the effective type, which expands it, is only for an
        // unbound name or a type parameter.
        if (record == nullptr || record->form == Form::type_parameter) {
            if (const auto effective
                = resolver.effective_systemverilog_type(input, use_scope);
                effective && *effective != input) {
                return self(self, *effective, use_scope, depth + 1U);
            }
        }
        // A resolved named type's reference carries its flattened width as
        // its last packed dimension; the dimensions declared over it come
        // first. Dimensions declared over a named type whose shape is
        // selected later are all its own (7.4.1).
        auto declared = input.packed_dimensions;
        if (input.packed_over_named_type) {
            if (declared.empty() && input.packed_range) {
                declared.push_back(*input.packed_range);
            }
        } else if (record != nullptr) {
            if (!declared.empty()) {
                declared.pop_back();
            }
        } else if (declared.empty() && input.packed_range) {
            declared.push_back(*input.packed_range);
        }
        std::vector<runtime::SystemVerilogVpiRange> dimensions;
        for (const auto& range : declared) {
            const auto bounds = range_of(range);
            if (!bounds) {
                return std::nullopt;
            }
            dimensions.push_back(*bounds);
        }
        if (record == nullptr) {
            const auto& spelling = input.target.spelling;
            // A class type names the specialization its actuals select
            // (8.25); a built-in class is named by its spelling (37.32).
            if (!input.class_identity.empty()
                || input.value_form == Form::class_handle) {
                Typespec handle;
                handle.kind = Kind::Class;
                handle.name = input.class_specialization.empty()
                    ? spelling
                    : input.class_specialization;
                return handle;
            }
            // The built-in classes of package std (15.4, 15.3, 9.7).
            if (const std::string_view leaf = spelling.starts_with("std::")
                        ? std::string_view { spelling }.substr(5U)
                        : std::string_view { spelling };
                leaf == "mailbox" || leaf == "semaphore" || leaf == "process") {
                Typespec handle;
                handle.kind = Kind::Class;
                handle.name = std::string { leaf };
                return handle;
            }
            Typespec builtin;
            builtin.is_signed = input.signed_value;
            if (input.value_form == Form::string || spelling == "string") {
                builtin.kind = Kind::String;
                builtin.is_signed = false;
                return builtin;
            }
            if (input.value_form && *input.value_form != Form::packed_integral
                && *input.value_form != Form::enumeration) {
                return std::nullopt;
            }
            static constexpr std::pair<std::string_view, Kind> atoms[] {
                { "byte", Kind::Byte }, { "shortint", Kind::ShortInt },
                { "int", Kind::Int }, { "longint", Kind::LongInt },
                { "integer", Kind::Integer }, { "time", Kind::Time },
                { "real", Kind::Real }, { "realtime", Kind::Real },
                { "shortreal", Kind::ShortReal }, { "event", Kind::Event },
                { "chandle", Kind::Chandle },
            };
            if (const auto* const atom = std::ranges::find(
                    atoms, spelling, &std::pair<std::string_view, Kind>::first);
                atom != std::end(atoms)) {
                // An integer atom's packed range is its implicit one (6.11).
                builtin.kind = atom->second;
                return builtin;
            }
            if (!spelling.empty() && spelling != "bit" && spelling != "logic"
                && spelling != "reg"
                && !semantic::sv::is_builtin_net_kind(spelling)) {
                return std::nullopt;
            }
            builtin.kind = spelling == "bit" ? Kind::Bit : Kind::Logic;
            builtin.ranges = std::move(dimensions);
            return builtin;
        }
        const auto record_scope = scope_of(*record);
        std::optional<Typespec> named;
        // A typedef of an unpacked array (7.4.2), or of packed dimensions
        // over a named structure or union (7.4.1): an array named by it.
        // A typedef of a structure or union that its own declaration
        // defines names an anonymous record (37.25 detail 5).
        const auto base_definition = record->base.target.target.valid()
                && record->base.target.target != record->id
            ? specialization.find_type(record->base.target.target)
            : std::nullopt;
        // Binding points a typedef of dimensions over a named structure at
        // that structure's own record; its declared range then spans more
        // than the structure's width.
        const bool dimensions_over_named
            = (record->form == Form::packed_structure
                  || record->form == Form::packed_union)
            && base_definition && base_definition->systemverilog != nullptr
            && (record->base.packed_range
                || !record->base.packed_dimensions.empty());
        if (record->container || dimensions_over_named) {
            Typespec array;
            auto element = record->base;
            if (record->container) {
                array.kind = Kind::Array;
                const auto form = record->container->form;
                array.array_kind = form == Form::dynamic_array
                    ? ArrayKind::Dynamic
                    : form == Form::queue           ? ArrayKind::Queue
                    : form == Form::associative_array ? ArrayKind::Associative
                                                      : ArrayKind::Static;
                for (const auto& range : record->container->static_dimensions) {
                    const auto bounds = range_of(range);
                    if (!bounds) {
                        return std::nullopt;
                    }
                    array.ranges.push_back(*bounds);
                }
                if (form == Form::associative_array
                    && record->container->associative_index) {
                    if (auto key = self(self,
                            *record->container->associative_index,
                            record_scope, depth + 1U)) {
                        array.index.push_back(std::move(*key));
                    }
                }
                element.container_form.reset();
                element.unpacked_dimensions.clear();
            } else {
                array.kind = Kind::PackedArray;
                auto own = record->base.packed_dimensions;
                if (own.empty() && record->base.packed_range) {
                    own.push_back(*record->base.packed_range);
                }
                for (const auto& range : own) {
                    const auto bounds = range_of(range);
                    if (!bounds) {
                        return std::nullopt;
                    }
                    array.ranges.push_back(*bounds);
                }
                element.packed_dimensions.clear();
                element.packed_range.reset();
                element.packed_over_named_type = false;
            }
            auto inner = self(self, element, record_scope, depth + 1U);
            if (!inner) {
                return std::nullopt;
            }
            // The last range may be the flattened width of all of them.
            if (array.kind == Kind::PackedArray && !array.ranges.empty()) {
                std::uint64_t outer = 1U;
                for (std::size_t index = 0U; index + 1U < array.ranges.size();
                     ++index) {
                    outer *= elaboration_detail::index_distance(
                                 array.ranges[index].left,
                                 array.ranges[index].right)
                        + 1U;
                }
                const auto last = elaboration_detail::index_distance(
                                      array.ranges.back().left,
                                      array.ranges.back().right)
                    + 1U;
                if (last == bits(bits, *inner)
                    || last == outer * bits(bits, *inner)) {
                    array.ranges.pop_back();
                }
            }
            if (array.ranges.empty() && array.kind == Kind::PackedArray) {
                // A typedef of another named type is an alias of it; one of
                // the structure its own declaration defines is that type.
                auto aliased = std::move(*inner);
                named = aliased;
                named->alias.clear();
                if (!base_definition->systemverilog->name.starts_with('@')) {
                    named->alias.push_back(std::move(aliased));
                }
                named->name = record->name;
                named->ranges.clear();
                drop_named(dimensions, *named);
                if (dimensions.empty()) {
                    return named;
                }
                Typespec over;
                over.kind = Kind::PackedArray;
                over.ranges = std::move(dimensions);
                over.element.push_back(std::move(*named));
                return over;
            }
            array.element.push_back(std::move(*inner));
            array.name = record->name;
            named = std::move(array);
        } else {
    switch (record->form) {
            case Form::alias:
            case Form::type_parameter:
            case Form::packed_integral:
            case Form::static_array:
            case Form::dynamic_array:
            case Form::queue:
            case Form::associative_array:
            case Form::string: {
                named = self(self, record->base, record_scope, depth + 1U);
                if (!named) {
                    return std::nullopt;
                }
                // A type parameter's typespec is its actual (37.25 detail 11).
                if (record->form == Form::type_parameter) {
                    break;
                }
                // A typedef of a named type, or of a built-in type without
                // dimensions, is an alias of it (37.25 details 1, 6).
                if (!named->name.empty()
                    || (named->ranges.empty() && named->kind != Kind::Array
                        && named->kind != Kind::PackedArray
                        && named->kind != Kind::Struct
                        && named->kind != Kind::Union
                        && named->kind != Kind::Enum)) {
                    auto aliased = *named;
                    named->alias.clear();
                    named->alias.push_back(std::move(aliased));
                }
                named->name = record->name;
                break;
            }
            case Form::packed_structure:
            case Form::packed_union:
            case Form::unpacked_structure:
            case Form::unpacked_union:
            case Form::tagged_union: {
                Typespec aggregate;
                aggregate.kind = record->form == Form::packed_structure
                        || record->form == Form::unpacked_structure
                    ? Kind::Struct
                    : Kind::Union;
                aggregate.packed = record->form == Form::packed_structure
                    || record->form == Form::packed_union;
                aggregate.tagged = record->form == Form::tagged_union;
                aggregate.is_signed = aggregate.packed && record->base.signed_value;
                for (const auto& member : record->members) {
                    auto child = self(self, member.type, record_scope, depth + 1U);
                    if (!child) {
                        return std::nullopt;
                    }
                    aggregate.members.push_back(std::move(*child));
                    aggregate.member_names.push_back(member.name);
                }
                if (!record->name.starts_with('@')) {
                    aggregate.name = record->name;
                }
                named = std::move(aggregate);
                break;
            }
            case Form::enumeration: {
                Typespec enumeration;
                enumeration.kind = Kind::Enum;
                // An enumeration without a base type is an int (6.19).
                auto base = record->base.target.resolved()
                        || !record->base.target.spelling.empty()
                    ? self(self, record->base, record_scope, depth + 1U)
                    : std::optional<Typespec> { };
                if (!base) {
                    base.emplace();
                    base->kind = Kind::Int;
                    base->is_signed = true;
                }
                const auto width = bits(bits, *base);
                if (width == 0U || width > 64U) {
                    return std::nullopt;
                }
                // A literal without a value is the previous one plus one, the
                // first one 0 (6.19).
                std::int64_t next = 0;
                for (const auto& literal : record->enumeration_literals) {
                    if (literal.value) {
                        const auto value = specialization
                            .evaluate_integral_expression(*literal.value);
                        if (!value) {
                            return std::nullopt;
                        }
                        next = *value;
                    }
                    enumeration.enum_constants.push_back({ literal.name,
                        runtime::PackedLogic4::from_aval_bval(
                            static_cast<std::uint32_t>(width),
                            static_cast<std::uint64_t>(next), 0U) });
                    ++next;
                }
                enumeration.element.push_back(std::move(*base));
                if (!record->name.starts_with('@')) {
                    enumeration.name = record->name;
                }
                named = std::move(enumeration);
                break;
            }
            case Form::class_handle: {
                Typespec handle;
                handle.kind = Kind::Class;
                handle.name = record->name;
                named = std::move(handle);
                break;
            }
            default:
                return std::nullopt;
            }
        }
        drop_named(dimensions, *named);
        if (dimensions.empty()) {
            return named;
        }
        // Packed dimensions over a named type (7.4.1).
        Typespec array;
        array.kind = Kind::PackedArray;
        array.ranges = std::move(dimensions);
        array.element.push_back(std::move(*named));
        return array;
    };
    return build(build, reference, scope, 0U);
}

std::optional<ContainerType> systemverilog_container_type(
    const semantic::SpecializedHirUnit& working_specialization,
    const semantic::sv::TypeReference& reference)
{
    const auto nominal_identity = [&working_specialization](
                                      const semantic::sv::TypeReference& type) {
        if (!type.target.target.valid()) {
            return std::string { };
        }
        const auto definition = working_specialization.find_type(
            type.target.target);
        return definition && definition->systemverilog != nullptr
                && semantic::sv::has_nominal_type_identity(
                    definition->systemverilog->form)
            ? semantic::sv::nominal_type_identity(type.target.target)
            : std::string { };
    };
    const auto boundary = [&](const std::optional<std::int64_t> value,
                              const std::optional<semantic::ExpressionId> expression) {
        return expression
            ? working_specialization.evaluate_integral_expression(
                  *expression)
            : value;
    };
    // A reference to a named packed type with its own packed dimensions
    // (`packet_t [1:0]`, IEEE 1800-2017 7.4.1) is resolved to its whole
    // width; the named type gives the width of one element.
    const auto named_packed_width
        = [&](const semantic::sv::TypeReference& candidate,
              const std::uint64_t element_width)
        -> std::optional<std::uint64_t> {
        // A resolved named type's reference carries the dimensions declared
        // over it, then its own flattened width as the last packed
        // dimension; its executable width is only the type's when the type
        // is in another compilation unit.
        if (element_width != 0U && candidate.packed_dimensions.size() > 1U) {
            std::uint64_t count { element_width };
            for (std::size_t at = 0U; at < candidate.packed_dimensions.size(); ++at) {
                const auto& dimension = candidate.packed_dimensions[at];
                const auto left
                    = boundary(dimension.left, dimension.left_expression);
                const auto right
                    = boundary(dimension.right, dimension.right_expression);
                if (!left || !right) {
                    return std::nullopt;
                }
                const auto length = static_cast<std::uint64_t>(
                    *left >= *right ? *left - *right : *right - *left) + 1U;
                if (at + 1U == candidate.packed_dimensions.size()) {
                    if (length != element_width) {
                        count = 0U;
                    }
                    break;
                }
                if (count > std::numeric_limits<std::uint32_t>::max() / length) {
                    return std::nullopt;
                }
                count *= length;
            }
            if (count != 0U) {
                return count;
            }
        }
        if (element_width == 0U) {
            return element_width;
        }
        if (candidate.executable_width
            && *candidate.executable_width % element_width != 0U) {
            return std::nullopt;
        }
        auto whole = std::max(
            candidate.executable_width.value_or(0U), element_width);
        // Its packed range is the flattened whole width of a named type
        // from another compilation unit.
        if (candidate.packed_range) {
            const auto left = boundary(candidate.packed_range->left,
                candidate.packed_range->left_expression);
            const auto right = boundary(candidate.packed_range->right,
                candidate.packed_range->right_expression);
            if (left && right) {
                const auto length = static_cast<std::uint64_t>(
                    *left >= *right ? *left - *right : *right - *left) + 1U;
                if (length % element_width == 0U) {
                    whole = std::max(whole, length);
                }
            }
        }
        return whole;
    };
    std::unordered_set<std::uint32_t> visiting;
    // A typedef's own declaration scope resolves the names in it; a
    // reference into another unit (`$unit`, an imported package) has no
    // bound target until it is resolved there (IEEE 1800-2017 3.12.1).
    const auto definition_scope = [&](const semantic::sv::TypeDefinition& type,
                                      const semantic::ScopeId fallback) {
        const auto declaration = type.declaration.valid()
            ? working_specialization.find_declaration(type.declaration)
            : std::nullopt;
        return declaration && declaration->systemverilog != nullptr
            ? declaration->systemverilog->scope
            : fallback;
    };
    const auto profile = [&](const auto& self,
                             const semantic::sv::TypeReference& unresolved,
                             const bool box_leaf,
                             const semantic::ScopeId scope)
        -> std::optional<ContainerType> {
        auto candidate = unresolved;
        if (!candidate.container_form && !candidate.target.target.valid()
            && !candidate.target.spelling.empty()) {
            candidate = semantic::CompiledDesignResolver {
                working_specialization
            }.effective_systemverilog_type(candidate, scope)
                            .value_or(candidate);
        }
        if (candidate.container_form) {
            if (candidate.container_element_types.size() > 1U) {
                return std::nullopt;
            }
            auto leaf = candidate.container_element_types.empty()
                ? candidate
                : candidate.container_element_types.front();
            const auto form = *candidate.container_form;
            if (candidate.container_element_types.empty()) {
                leaf.container_form.reset();
                leaf.queue_maximum.reset();
                leaf.associative_index.reset();
                leaf.unpacked_dimensions.clear();
                leaf.container_element_types.clear();
            }
            const auto specialized_type_parameter_width = [&]()
                -> std::optional<std::size_t> {
                if (leaf.container_form
                    || !leaf.unpacked_dimensions.empty()) {
                    return std::nullopt;
                }
                const semantic::CompiledDesignResolver resolver {
                    working_specialization
                };
                const auto formal_id = specialized_type_parameter_formal(
                    working_specialization, resolver, leaf,
                    working_specialization.scope(), true);
                if (!formal_id) {
                    return std::nullopt;
                }
                const auto& actuals
                    = working_specialization.specialization()
                          .actual_identities;
                const auto actual = std::ranges::find(
                    actuals, *formal_id,
                    &semantic::SpecializedHirActualIdentity::declaration);
                if (actual == actuals.end() || !actual->systemverilog_type) {
                    return std::nullopt;
                }
                const auto formal
                    = working_specialization.find_declaration(*formal_id);
                if (!formal || formal->systemverilog == nullptr) {
                    return std::nullopt;
                }
                semantic::sv::Declaration width_declaration;
                width_declaration.scope = formal->systemverilog->scope;
                width_declaration.type = leaf;
                return systemverilog_declaration_width(
                    working_specialization, width_declaration);
            }();
            const semantic::CompiledDesignResolver type_resolver {
                working_specialization
            };
            leaf = type_resolver.effective_systemverilog_type(leaf, scope)
                       .value_or(leaf);
            auto element = self(self, leaf, false, scope);
            if (!element) {
                return std::nullopt;
            }
            if (specialized_type_parameter_width
                && !element->fixed && !element->queue
                && !element->associative
                && (element->element_kind == ContainerElementKind::Packed
                    || element->element_kind
                        == ContainerElementKind::Scalar)) {
                if (*specialized_type_parameter_width == 0U
                    || *specialized_type_parameter_width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                // The resolved type parameter may expand a typedef using its
                // unspecialized declaration body. Keep the exact packed width
                // retained for this owner specialization on the container
                // element that runtime indexing exposes.
                element->element_width = static_cast<std::uint32_t>(
                    *specialized_type_parameter_width);
            }
            ContainerType result;
            // A dynamic array has no dedicated ContainerType flag: it is
            // represented by fixed/queue/associative all being false.
            // Use the retained element type instead of those runtime
            // flags so an outer container does not flatten a nested
            // dynamic array into its leaf element profile.
            const auto nested_container
                = leaf.container_form.has_value();
            // A fixed array of a fixed array type (`row_t m[2]` with
            // `typedef logic [6:0] row_t[3:1]`) is one multi-dimensional
            // array, equivalent to `logic [6:0] m[2][3:1]` (IEEE 1800-2017
            // 6.22.2, 7.4.5).
            std::vector<ContainerDimension> inner_dimensions;
            if (nested_container
                && form == semantic::sv::TypeForm::static_array
                && element->fixed && !element->queue
                && !element->associative && !element->aggregate_value
                && !element->dimensions.empty()
                && element->element_kind != ContainerElementKind::Container) {
                inner_dimensions = element->dimensions;
            }
            if (nested_container && inner_dimensions.empty()) {
                result.element_kind = ContainerElementKind::Container;
                result.element_width = 0U;
                result.element_types.push_back(std::move(*element));
            } else {
                result.element_kind = element->element_kind;
                result.scalar_kind = element->scalar_kind;
                result.element_width = element->element_width;
                result.two_state = element->two_state;
                result.signed_elements = element->signed_elements;
                result.union_aggregate = element->union_aggregate;
                result.element_nominal_type
                    = element->element_nominal_type;
                result.element_types
                    = std::move(element->element_types);
                result.member_names
                    = std::move(element->member_names);
            }
            result.fixed
                = form == semantic::sv::TypeForm::static_array;
            result.queue = form == semantic::sv::TypeForm::queue;
            result.associative
                = form == semantic::sv::TypeForm::associative_array;
            if (result.associative
                && candidate.associative_index) {
                const auto& index = *candidate.associative_index;
                // The metadata keeps the index's packed range
                // (`[bit [63:0]]`, `[*]`).
                semantic::sv::TypeReference index_reference
                    = candidate.associative_index_metadata
                    ? semantic::sv::type_reference_from_associative_index_metadata(
                          *candidate.associative_index_metadata)
                    : semantic::sv::TypeReference { };
                index_reference.target = index;
                const bool packed_index = index_reference.packed_range
                    || !index_reference.packed_dimensions.empty();
                const auto effective_index
                    = type_resolver.underlying_systemverilog_type(
                                       index_reference,
                                       working_specialization.scope())
                          .value_or(index_reference);
                const auto index_spelling
                    = effective_index.target.spelling.empty()
                    ? index.spelling
                    : effective_index.target.spelling;
                if (index_spelling == "string") {
                    result.index_width = 0U;
                    result.two_state_indices = true;
                    result.signed_indices = false;
                    result.string_indices = true;
                } else {
                    semantic::sv::Declaration index_declaration;
                    index_declaration.type = index_reference;
                    const auto width = systemverilog_declaration_width(
                        working_specialization,
                        index_declaration);
                    const auto builtin_index_width
                        = [](const std::string_view spelling)
                        -> std::optional<std::uint32_t> {
                        if (spelling == "bit" || spelling == "logic"
                            || spelling == "reg") {
                            return 1U;
                        }
                        if (spelling == "byte") {
                            return 8U;
                        }
                        if (spelling == "shortint") {
                            return 16U;
                        }
                        if (spelling == "int"
                            || spelling == "integer") {
                            return 32U;
                        }
                        if (spelling == "longint"
                            || spelling == "time") {
                            return 64U;
                        }
                        return std::nullopt;
                    };
                    const auto direct_builtin_width
                        = builtin_index_width(index.spelling);
                    const auto resolved_builtin_width
                        = builtin_index_width(index_spelling);
                    if (width && *width > std::numeric_limits<std::uint32_t>::max()) {
                        return std::nullopt;
                    }
                    // A direct builtin index uses its language-defined
                    // width. A typedef can retain a builtin base spelling
                    // while adding a packed range, so use the fully
                    // resolved declaration width for non-builtin names.
                    result.index_width = direct_builtin_width
                            && !(packed_index && width)
                        ? *direct_builtin_width
                        : width
                        ? static_cast<std::uint32_t>(*width)
                        : resolved_builtin_width
                        ? *resolved_builtin_width
                        : 32U;
                    result.two_state_indices
                        = index.target.valid()
                        ? !effective_index.four_state
                        : index_spelling == "bit";
                    result.signed_indices
                        = index.target.valid()
                        ? effective_index.signed_value
                        : index_spelling != "bit"
                            && index_spelling != "time";
                }
            }
            if (result.queue && candidate.queue_maximum) {
                const auto maximum = working_specialization
                                         .evaluate_integral_expression(
                                             *candidate.queue_maximum);
                if (!maximum || *maximum < 0
                    || static_cast<std::uint64_t>(*maximum)
                        == std::numeric_limits<std::uint64_t>::max()) {
                    return std::nullopt;
                }
                result.maximum_elements
                    = static_cast<std::uint64_t>(*maximum) + 1U;
            }
            if (!result.fixed) {
                return result;
            }
            for (const auto& dimension :
                candidate.unpacked_dimensions) {
                const auto left = boundary(
                    dimension.left, dimension.left_expression);
                const auto right = boundary(
                    dimension.right, dimension.right_expression);
                if (!left || !right
                    || *left
                        < std::numeric_limits<std::int32_t>::min()
                    || *left
                        > std::numeric_limits<std::int32_t>::max()
                    || *right
                        < std::numeric_limits<std::int32_t>::min()
                    || *right
                        > std::numeric_limits<std::int32_t>::max()) {
                    return std::nullopt;
                }
                result.dimensions.emplace_back(
                    static_cast<std::int32_t>(*left),
                    static_cast<std::int32_t>(*right));
            }
            if (result.dimensions.empty()) {
                return std::nullopt;
            }
            result.dimensions.insert(result.dimensions.end(),
                inner_dimensions.begin(), inner_dimensions.end());
            result.index_left = result.dimensions.front().first;
            result.index_right = result.dimensions.front().second;
            return result;
        }

        ContainerType result;
        result.element_nominal_type = nominal_identity(candidate);
        if (candidate.value_form == semantic::sv::TypeForm::string
            || candidate.target.spelling == "string") {
            result.element_kind = ContainerElementKind::String;
            result.element_width = 0U;
        } else if (candidate.target.target.valid()) {
            if (!visiting.insert(
                             candidate.target.target.value())
                    .second) {
                return std::nullopt;
            }
            const auto definition
                = working_specialization.find_type(
                    candidate.target.target);
            if (!definition
                || definition->systemverilog == nullptr) {
                visiting.erase(candidate.target.target.value());
                return std::nullopt;
            }
            const auto& type = *definition->systemverilog;
            const bool tagged_aggregate
                = systemverilog_tagged_union_aggregate(
                    working_specialization, type);
            if (type.form
                    == semantic::sv::TypeForm::unpacked_structure
                || type.form
                    == semantic::sv::TypeForm::unpacked_union
                || tagged_aggregate) {
                result.element_kind
                    = ContainerElementKind::Aggregate;
                result.element_width = 0U;
                result.union_aggregate = type.form
                    == semantic::sv::TypeForm::unpacked_union;
                result.aggregate_value = box_leaf;
                result.element_nominal_type
                    = nominal_identity(candidate);
                // A boxed packed leaf of `width` four-state bits.
                const auto packed_leaf = [](const std::uint32_t width) {
                    ContainerType leaf;
                    leaf.element_kind = ContainerElementKind::Packed;
                    leaf.element_width = width;
                    leaf.fixed = true;
                    leaf.index_left = 0;
                    leaf.index_right = 0;
                    leaf.dimensions.emplace_back(0, 0);
                    return leaf;
                };
                // The tag leads the members; a void member holds no value.
                if (tagged_aggregate) {
                    result.member_names.emplace_back(
                        systemverilog_tagged_union_tag_member);
                    result.element_types.push_back(packed_leaf(
                        std::max<std::uint32_t>(1U,
                            static_cast<std::uint32_t>(std::bit_width(
                                type.members.size() - 1U)))));
                }
                for (const auto& member : type.members) {
                    if (tagged_aggregate
                        && member.type.target.spelling == "void") {
                        result.member_names.push_back(member.name);
                        result.element_types.push_back(packed_leaf(1U));
                        continue;
                    }
                    auto member_type = self(self, member.type, true,
                        definition_scope(type, scope));
                    if (!member_type) {
                        visiting.erase(
                            candidate.target.target.value());
                        return std::nullopt;
                    }
                    result.member_names.push_back(member.name);
                    result.element_types.push_back(
                        std::move(*member_type));
                }
                visiting.erase(candidate.target.target.value());
                return result;
            }
            if (type.form
                    == semantic::sv::TypeForm::packed_structure
                || type.form
                    == semantic::sv::TypeForm::packed_union) {
                std::uint64_t width { };
                bool two_state = !candidate.four_state;
                for (const auto& member : type.members) {
                    const auto member_type = self(self, member.type,
                        false, definition_scope(type, scope));
                    if (!member_type
                        || member_type->fixed
                        || member_type->queue
                        || member_type->associative
                        || member_type->element_width == 0U) {
                        visiting.erase(
                            candidate.target.target.value());
                        return std::nullopt;
                    }
                    if (type.form
                        == semantic::sv::TypeForm::packed_union) {
                        width = std::max<std::uint64_t>(
                            width, member_type->element_width);
                    } else if (width
                        > std::numeric_limits<std::uint64_t>::max()
                            - member_type->element_width) {
                        visiting.erase(
                            candidate.target.target.value());
                        return std::nullopt;
                    } else {
                        width += member_type->element_width;
                    }
                    two_state = two_state
                        && member_type->two_state;
                }
                visiting.erase(candidate.target.target.value());
                const auto whole = named_packed_width(candidate, width);
                if (!whole || *whole == 0U
                    || *whole
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                width = *whole;
                result.element_kind = ContainerElementKind::Packed;
                result.element_width
                    = static_cast<std::uint32_t>(width);
                result.two_state = two_state;
                result.signed_elements = candidate.signed_value;
                if (box_leaf) {
                    result.fixed = true;
                    result.index_left = 0;
                    result.index_right = 0;
                    result.dimensions.emplace_back(0, 0);
                }
                return result;
            }
            auto base = self(self, type.base, box_leaf,
                definition_scope(type, scope));
            visiting.erase(candidate.target.target.value());
            if (!base) {
                return std::nullopt;
            }
            // Packed dimensions on a typedef of a packed type widen it.
            const bool boxed = base->fixed && box_leaf
                && base->dimensions.size() == 1U
                && base->dimensions.front()
                    == std::pair<std::int32_t, std::int32_t> { 0, 0 };
            if ((!base->fixed || boxed) && !base->queue && !base->associative
                && (base->element_kind == ContainerElementKind::Packed
                    || base->element_kind == ContainerElementKind::Scalar)) {
                const auto whole
                    = named_packed_width(candidate, base->element_width);
                if (!whole
                    || *whole > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                if (*whole != base->element_width) {
                    base->element_width = static_cast<std::uint32_t>(*whole);
                    base->element_kind = ContainerElementKind::Packed;
                    base->scalar_kind
                        = frontend::SystemVerilogScalarKind::None;
                }
            }
            if (!result.element_nominal_type.empty()) {
                base->element_nominal_type
                    = result.element_nominal_type;
            }
            return base;
        } else {
            std::optional<std::uint64_t> width
                = candidate.executable_width;
            if (!width && candidate.packed_range) {
                const auto left = boundary(
                    candidate.packed_range->left,
                    candidate.packed_range->left_expression);
                const auto right = boundary(
                    candidate.packed_range->right,
                    candidate.packed_range->right_expression);
                if (left && right) {
                    width = index_distance(*left, *right) + 1U;
                }
            }
            if (!width || *width == 0U
                || *width
                    > std::numeric_limits<std::uint32_t>::max()) {
                return std::nullopt;
            }
            result.scalar_kind = frontend::systemverilog_scalar_kind(
                candidate.target.spelling);
            result.element_kind
                = result.scalar_kind
                    == frontend::SystemVerilogScalarKind::None
                ? ContainerElementKind::Packed
                : ContainerElementKind::Scalar;
            result.element_width
                = static_cast<std::uint32_t>(*width);
            result.two_state
                = result.scalar_kind
                    != frontend::SystemVerilogScalarKind::None
                || !candidate.four_state;
            result.signed_elements = candidate.signed_value;
        }
        if (box_leaf) {
            result.fixed = true;
            result.index_left = 0;
            result.index_right = 0;
            result.dimensions.emplace_back(0, 0);
        }
        return result;
    };
    const semantic::CompiledDesignResolver resolver {
        working_specialization
    };
    const auto materialized = reference.container_form
        ? reference
        : resolver.effective_systemverilog_type(
                      reference, working_specialization.scope())
              .value_or(reference);
    const auto aggregate_value = !materialized.container_form;
    auto result = profile(profile, materialized, aggregate_value,
        working_specialization.scope());
    if (aggregate_value
        && (!result
            || result->element_kind
                != ContainerElementKind::Aggregate
            || !result->aggregate_value)) {
        return std::nullopt;
    }
    if (!result) {
        return std::nullopt;
    }
    try {
        static_cast<void>(default_container_value(*result));
    } catch (const std::length_error&) {
        // A one-dimensional packed array beyond the dense budget stores only
        // its written elements.
        if (!runtime::simir::sparse_container_eligible(*result)) {
            return std::nullopt;
        }
        result->sparse = true;
        try {
            static_cast<void>(default_container_value(*result));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return result;
}

} // namespace fsim::elaboration::hierarchy_sv_type_layout_detail
