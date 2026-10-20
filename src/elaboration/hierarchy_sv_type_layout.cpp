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
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>

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
                        || record.form
                            == semantic::sv::TypeForm::tagged_union
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
        = [](const semantic::sv::TypeReference& candidate,
              const std::uint64_t element_width)
        -> std::optional<std::uint64_t> {
        if (!candidate.executable_width || element_width == 0U) {
            return element_width;
        }
        if (*candidate.executable_width % element_width != 0U) {
            return std::nullopt;
        }
        return std::max(*candidate.executable_width, element_width);
    };
    std::unordered_set<std::uint32_t> visiting;
    const auto profile = [&](const auto& self,
                             const semantic::sv::TypeReference& candidate,
                             const bool box_leaf) -> std::optional<ContainerType> {
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
            leaf = type_resolver.effective_systemverilog_type(
                                    leaf, working_specialization.scope())
                       .value_or(leaf);
            auto element = self(self, leaf, false);
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
                semantic::sv::TypeReference index_reference;
                index_reference.target = index;
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
            if (type.form
                    == semantic::sv::TypeForm::unpacked_structure
                || type.form
                    == semantic::sv::TypeForm::unpacked_union) {
                result.element_kind
                    = ContainerElementKind::Aggregate;
                result.element_width = 0U;
                result.union_aggregate = type.form
                    == semantic::sv::TypeForm::unpacked_union;
                result.aggregate_value = box_leaf;
                result.element_nominal_type
                    = nominal_identity(candidate);
                for (const auto& member : type.members) {
                    auto member_type = self(
                        self, member.type, true);
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
                    const auto member_type = self(
                        self, member.type, false);
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
            auto base = self(self, type.base, box_leaf);
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
    auto result = profile(profile, materialized, aggregate_value);
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
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return result;
}

} // namespace fsim::elaboration::hierarchy_sv_type_layout_detail
