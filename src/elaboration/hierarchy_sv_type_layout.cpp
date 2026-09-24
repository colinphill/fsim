// SPDX-License-Identifier: Apache-2.0
#include "hierarchy_sv_type_layout_internal.hpp"

#include "elaborator_internal.hpp"
#include "fsim/semantic/compiled_design_resolver.hpp"
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
            ? working_specialization
                  .evaluate_integral_expression(*expression)
            : value;
    };
    std::unordered_set<std::uint32_t> visiting;
    const semantic::CompiledDesignResolver type_resolver {
        working_specialization
    };
    const auto resolve = [&](const auto& self,
                             const semantic::sv::TypeReference& reference)
        -> std::optional<std::size_t> {
        if (reference.virtual_interface) {
            return 64U;
        }
        std::optional<std::size_t> width;
        if (reference.packed_range) {
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
                    const bool packed_aggregate
                        = record.form
                            == semantic::sv::TypeForm::packed_structure
                        || record.form
                            == semantic::sv::TypeForm::packed_union
                        || record.form
                            == semantic::sv::TypeForm::tagged_union;
                    if (packed_aggregate && !record.members.empty()) {
                        std::size_t aggregate_width { };
                        bool valid = true;
                        const bool union_layout
                            = record.form
                                == semantic::sv::TypeForm::packed_union
                            || record.form
                                == semantic::sv::TypeForm::tagged_union;
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
        if (!definition || definition->systemverilog == nullptr) {
            return std::string { };
        }
        using Form = semantic::sv::TypeForm;
        const auto form = definition->systemverilog->form;
        if (form != Form::enumeration
            && form != Form::packed_structure
            && form != Form::packed_union
            && form != Form::unpacked_structure
            && form != Form::unpacked_union
            && form != Form::tagged_union) {
            return std::string { };
        }
        return std::string { "@sv-type:" }
        + std::to_string(type.target.target.value());
    };
    const auto boundary = [&](const std::optional<std::int64_t> value,
                              const std::optional<semantic::ExpressionId> expression) {
        return expression
            ? working_specialization.evaluate_integral_expression(
                  *expression)
            : value;
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
            ContainerType result;
            // A dynamic array has no dedicated ContainerType flag: it is
            // represented by fixed/queue/associative all being false.
            // Use the retained element type instead of those runtime
            // flags so an outer container does not flatten a nested
            // dynamic array into its leaf element profile.
            const auto nested_container
                = leaf.container_form.has_value();
            if (nested_container) {
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
                if (width == 0U
                    || width
                        > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
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
            result.scalar_kind = compiled_systemverilog_scalar_kind(
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
