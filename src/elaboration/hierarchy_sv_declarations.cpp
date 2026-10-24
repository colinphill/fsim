// SPDX-License-Identifier: Apache-2.0
#include "fsim/semantic/compiled_design_resolver.hpp"
#include "fsim/support/index_distance.hpp"
#include "hierarchy_builder_internal.hpp"
#include "hierarchy_sv_parameters_internal.hpp"
#include "hierarchy_sv_type_layout_internal.hpp"
#include "hierarchy_sv_constant_evaluator.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;
using namespace hierarchy_sv_parameters_detail;

namespace {

    // A fixed unpacked array of packed elements holding `values`, leftmost
    // element first through every dimension.
    [[nodiscard]] std::optional<ContainerValue> array_parameter_container(
        const ContainerType& type, const std::vector<std::int64_t>& values)
    {
        if (!type.fixed || type.associative || type.aggregate_value
            || type.element_kind != ContainerElementKind::Packed) {
            return std::nullopt;
        }
        auto value = default_container_value(type);
        if (value.elements.size() != values.size()) {
            return std::nullopt;
        }
        for (std::size_t at = 0; at < values.size(); ++at) {
            PackedLogic4 element(type.element_width, Logic4::zero);
            const auto raw = static_cast<std::uint64_t>(values[at]);
            for (std::size_t bit = 0; bit < type.element_width; ++bit) {
                const auto set = bit < 64U
                    ? ((raw >> bit) & 1U) != 0U
                    : values[at] < 0;
                element.set(bit, set ? Logic4::one : Logic4::zero);
            }
            value.elements[at] = std::move(element);
        }
        return value;
    }

    // A fixed unpacked array of packed, real-family or string elements
    // holding an actual's element identities, leftmost element first
    // (IEEE 1800-2017 6.20.2).
    [[nodiscard]] std::optional<ContainerValue> typed_array_parameter_container(
        const ContainerType& type, const std::vector<std::string>& elements)
    {
        if (!type.fixed || type.associative || type.aggregate_value) {
            return std::nullopt;
        }
        auto value = default_container_value(type);
        const auto count = type.element_kind == ContainerElementKind::String
            ? value.string_elements.size()
            : value.elements.size();
        if (count != elements.size()) {
            return std::nullopt;
        }
        // A decimal integer, or a canonical packed constant.
        const auto integral = [](const std::string& text)
            -> std::optional<HirSystemVerilogConstant> {
            std::int64_t integer { };
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), integer);
            if (parsed.ec == std::errc { }
                && parsed.ptr == text.data() + text.size()) {
                HirSystemVerilogConstant constant;
                constant.packed = PackedLogic4::from_aval_bval(
                    64U, static_cast<std::uint64_t>(integer), 0U);
                constant.width = 64U;
                constant.signed_value = true;
                constant.domain = frontend::ValueDomain::Bit2;
                return constant;
            }
            return decode_hir_systemverilog_constant(text);
        };
        std::string error;
        for (std::size_t at = 0; at < elements.size(); ++at) {
            const auto& text = elements[at];
            if (type.element_kind == ContainerElementKind::String) {
                auto bytes = semantic::parse_systemverilog_string_identity(
                    text);
                if (!bytes) {
                    return std::nullopt;
                }
                value.string_elements[at] = std::move(*bytes);
                continue;
            }
            if (type.element_kind == ContainerElementKind::Scalar) {
                auto scalar = decode_hir_systemverilog_scalar_constant(text);
                if (!scalar) {
                    auto constant = integral(text);
                    if (constant) {
                        constant = convert_hir_systemverilog_value_kind(
                            std::move(*constant), type.scalar_kind, error);
                    }
                    if (!constant || !constant->scalar) {
                        return std::nullopt;
                    }
                    scalar = constant->scalar;
                }
                scalar = frontend::convert_systemverilog_scalar_constant(
                    *scalar, type.scalar_kind, error);
                if (!scalar) {
                    return std::nullopt;
                }
                value.elements[at] = PackedLogic4::from_aval_bval(
                    type.element_width, scalar->bits, 0U);
                continue;
            }
            const auto constant = integral(text);
            if (type.element_kind != ContainerElementKind::Packed
                || !constant || constant->unbounded) {
                return std::nullopt;
            }
            const auto sign = constant->packed.width() != 0U
                    && constant->signed_value
                ? runtime::to_logic4(constant->packed.get_logic9(
                      constant->packed.width() - 1U))
                : Logic4::zero;
            PackedLogic4 bits(type.element_width, Logic4::zero);
            for (std::size_t bit = 0; bit < type.element_width; ++bit) {
                bits.set(bit, bit < constant->packed.width()
                        ? runtime::to_logic4(constant->packed.get_logic9(bit))
                        : sign);
            }
            value.elements[at] = std::move(bits);
        }
        return value;
    }

    // The constant value of a container declaration initializer: a positional
    // assignment pattern, nested patterns for the dimensions of a fixed
    // array, `default:`, or for a queue or dynamic array an unpacked
    // concatenation (IEEE 1800-2017 10.9, 10.10, 6.8).
    [[nodiscard]] std::optional<ContainerValue>
    constant_container_initializer(
        const semantic::SpecializedHirUnit& specialization,
        const ContainerType& type,
        const semantic::ExpressionId initializer,
        const semantic::sv::TypeReference* const element_type = nullptr,
        std::string* const failure = nullptr)
    {
        using semantic::sv::ExpressionKind;
        // `'{key: value, ...}` for an associative array (7.9.11, 10.9.2).
        if (type.associative && !type.aggregate_value
            && (type.element_kind == ContainerElementKind::Packed
                || type.element_kind == ContainerElementKind::String)) {
            const auto pattern = specialization.find_expression(initializer);
            if (!pattern || pattern->systemverilog == nullptr
                || pattern->systemverilog->kind
                    != ExpressionKind::assignment_pattern) {
                return std::nullopt;
            }
            auto value = default_container_value(type);
            std::string error;
            const auto packed_of = [&](const semantic::ExpressionId id,
                                       const std::uint32_t width)
                -> std::optional<PackedLogic4> {
                const auto constant = evaluate_hir_systemverilog_constant(
                    specialization, id, error);
                if (!constant || constant->unbounded) {
                    return std::nullopt;
                }
                PackedLogic4 result(width, Logic4::zero);
                const auto sign = constant->packed.width() != 0U
                    && constant->signed_value
                    ? runtime::to_logic4(constant->packed.get_logic9(
                          constant->packed.width() - 1U))
                    : Logic4::zero;
                for (std::uint32_t bit = 0; bit < width; ++bit) {
                    result.set(bit, bit < constant->packed.width()
                            ? runtime::to_logic4(
                                  constant->packed.get_logic9(bit))
                            : sign);
                }
                return result;
            };
            for (const auto& association : pattern->systemverilog->associations) {
                if (association.choice_spelling != "@key"
                    || association.choices.size() != 1U) {
                    return std::nullopt;
                }
                const auto key_id = association.choices.front();
                std::size_t position { };
                if (type.string_indices) {
                    const auto key = specialization.evaluate_string_expression(
                        key_id);
                    if (!key) {
                        return std::nullopt;
                    }
                    const auto found = std::ranges::lower_bound(
                        value.string_keys, *key);
                    position = static_cast<std::size_t>(
                        found - value.string_keys.begin());
                    if (found == value.string_keys.end() || *found != *key) {
                        value.string_keys.insert(found, *key);
                        if (type.element_kind == ContainerElementKind::String) {
                            value.string_elements.insert(
                                value.string_elements.begin()
                                    + static_cast<std::ptrdiff_t>(position),
                                std::string { });
                        } else {
                            value.elements.insert(value.elements.begin()
                                    + static_cast<std::ptrdiff_t>(position),
                                PackedLogic4(type.element_width, Logic4::zero));
                        }
                    }
                } else {
                    const auto key = packed_of(key_id, type.index_width);
                    if (!key || key->is_logic9()) {
                        return std::nullopt;
                    }
                    const auto found = std::ranges::lower_bound(value.keys,
                        *key, [&](const PackedLogic4& left,
                                  const PackedLogic4& right) {
                            return associative_index_key_less(
                                type, left, right);
                        });
                    position = static_cast<std::size_t>(
                        found - value.keys.begin());
                    if (found == value.keys.end() || *found != *key) {
                        value.keys.insert(found, *key);
                        if (type.element_kind == ContainerElementKind::String) {
                            value.string_elements.insert(
                                value.string_elements.begin()
                                    + static_cast<std::ptrdiff_t>(position),
                                std::string { });
                        } else {
                            value.elements.insert(value.elements.begin()
                                    + static_cast<std::ptrdiff_t>(position),
                                PackedLogic4(type.element_width, Logic4::zero));
                        }
                    }
                }
                if (type.element_kind == ContainerElementKind::String) {
                    const auto text = specialization.evaluate_string_expression(
                        association.value);
                    if (!text) {
                        return std::nullopt;
                    }
                    value.string_elements[position] = *text;
                } else {
                    const auto element = packed_of(
                        association.value, type.element_width);
                    if (!element) {
                        return std::nullopt;
                    }
                    value.elements[position] = *element;
                }
            }
            return value;
        }
        // A fixed array of unpacked structures: one structure pattern per
        // element, its members by position or name (IEEE 1800-2017
        // 10.9.1, 10.9.2).
        if (type.fixed && !type.associative && type.dimensions.size() == 1U
            && type.element_kind == ContainerElementKind::Aggregate) {
            const auto pattern = specialization.find_expression(initializer);
            if (!pattern || pattern->systemverilog == nullptr
                || pattern->systemverilog->kind
                    != ExpressionKind::assignment_pattern) {
                return std::nullopt;
            }
            auto value = default_container_value(type);
            std::string error;
            const auto fill_member = [&](ContainerValue& leaf,
                                         const semantic::ExpressionId item) {
                if (leaf.type.element_kind == ContainerElementKind::String
                    && leaf.string_elements.size() == 1U) {
                    const auto text
                        = specialization.evaluate_string_expression(item);
                    if (!text) {
                        return false;
                    }
                    leaf.string_elements.front() = *text;
                    return true;
                }
                if (leaf.type.element_kind != ContainerElementKind::Packed
                    || leaf.elements.size() != 1U) {
                    return false;
                }
                const auto constant = evaluate_hir_systemverilog_constant(
                    specialization, item, error);
                if (!constant || constant->unbounded) {
                    return false;
                }
                const auto width = leaf.type.element_width;
                const auto sign = constant->packed.width() != 0U
                        && constant->signed_value
                    ? runtime::to_logic4(constant->packed.get_logic9(
                          constant->packed.width() - 1U))
                    : Logic4::zero;
                PackedLogic4 bits(width, Logic4::zero);
                for (std::size_t bit = 0; bit < width; ++bit) {
                    bits.set(bit, bit < constant->packed.width()
                            ? runtime::to_logic4(
                                  constant->packed.get_logic9(bit))
                            : sign);
                }
                leaf.elements.front() = std::move(bits);
                return true;
            };
            const auto fill_structure = [&](ContainerValue& box,
                                            const semantic::ExpressionId item) {
                const auto structure = specialization.find_expression(item);
                if (!structure || structure->systemverilog == nullptr
                    || structure->systemverilog->kind
                        != ExpressionKind::assignment_pattern) {
                    return false;
                }
                const auto& associations
                    = structure->systemverilog->associations;
                const auto& names = box.type.member_names;
                std::optional<semantic::ExpressionId> fallback;
                std::vector<std::optional<semantic::ExpressionId>> members(
                    box.nested_elements.size());
                std::size_t position { };
                for (const auto& association : associations) {
                    if (association.choice_spelling == "default") {
                        fallback = association.value;
                    } else if (association.choice_spelling.empty()) {
                        if (position >= members.size()) {
                            return false;
                        }
                        members[position++] = association.value;
                    } else {
                        // `name: value`, kept as a keyed choice expression.
                        auto member_name = association.choice_spelling;
                        if (member_name == "@key"
                            && association.choices.size() == 1U) {
                            const auto choice = specialization.find_expression(
                                association.choices.front());
                            if (choice && choice->systemverilog != nullptr
                                && choice->systemverilog->kind
                                    == ExpressionKind::name) {
                                member_name = choice->systemverilog->text;
                            }
                        }
                        const auto found = std::ranges::find(
                            names, member_name);
                        if (found == names.end()) {
                            return false;
                        }
                        members[static_cast<std::size_t>(
                            found - names.begin())] = association.value;
                    }
                }
                for (std::size_t member = 0; member < members.size();
                    ++member) {
                    const auto item_value = members[member]
                        ? members[member]
                        : fallback;
                    if (!item_value
                        || !fill_member(box.nested_elements[member],
                            *item_value)) {
                        return false;
                    }
                }
                return true;
            };
            const auto& items = pattern->systemverilog->associations;
            std::optional<semantic::ExpressionId> fallback;
            std::vector<semantic::ExpressionId> positional;
            for (const auto& association : items) {
                if (association.choice_spelling == "default") {
                    fallback = association.value;
                } else if (association.choice_spelling.empty()) {
                    positional.push_back(association.value);
                } else {
                    return std::nullopt;
                }
            }
            if (value.nested_elements.size() != positional.size()
                && !(positional.empty() && fallback)) {
                return std::nullopt;
            }
            for (std::size_t index = 0; index < value.nested_elements.size();
                ++index) {
                const auto item = positional.empty()
                    ? *fallback
                    : positional[index];
                if (!fill_structure(value.nested_elements[index], item)) {
                    return std::nullopt;
                }
            }
            return value;
        }
        if (type.associative || type.aggregate_value
            || (type.element_kind != ContainerElementKind::Packed
                && type.element_kind != ContainerElementKind::Scalar
                && type.element_kind != ContainerElementKind::String)) {
            return std::nullopt;
        }
        std::optional<std::size_t> leaf_count;
        if (type.fixed) {
            std::size_t product = 1U;
            for (const auto& [left, right] : type.dimensions) {
                const auto extent = static_cast<std::size_t>(
                    left >= right
                        ? static_cast<std::int64_t>(left) - right
                        : static_cast<std::int64_t>(right) - left) + 1U;
                if (extent > (std::size_t { 1 } << 24U) / product) {
                    return std::nullopt;
                }
                product *= extent;
            }
            leaf_count = product;
        }
        if (type.fixed && (!leaf_count || *leaf_count == 0U)) {
            return std::nullopt;
        }
        const auto initializer_expression
            = specialization.find_expression(initializer);
        const auto* const initializer_source = initializer_expression
            ? initializer_expression->systemverilog
            : nullptr;
        // An array parameter, an array-valued constant function call or a
        // slice of one gives its elements from the left bound (7.6, 13.4).
        if (initializer_source != nullptr && type.fixed
            && type.dimensions.size() == 1U
            && type.element_kind == ContainerElementKind::Packed
            && (initializer_source->kind == ExpressionKind::name
                || initializer_source->kind == ExpressionKind::call
                || initializer_source->kind == ExpressionKind::slice)) {
            std::string array_error;
            const auto array = evaluate_hir_systemverilog_array_constant(
                specialization, initializer, array_error);
            if (failure != nullptr) {
                *failure = array ? std::string { "the array has "
                                       + std::to_string(array->elements.size())
                                       + " elements" }
                                 : array_error;
            }
            if (array && array->elements.size() == *leaf_count) {
                auto value = default_container_value(type);
                for (std::size_t at = 0; at < *leaf_count; ++at) {
                    const auto& element = array->elements[at];
                    const auto sign = element.packed.width() != 0U
                            && element.signed_value
                        ? runtime::to_logic4(element.packed.get_logic9(
                              element.packed.width() - 1U))
                        : Logic4::zero;
                    PackedLogic4 bits(type.element_width, Logic4::zero);
                    for (std::size_t bit = 0; bit < type.element_width;
                         ++bit) {
                        bits.set(bit, bit < element.packed.width()
                                ? runtime::to_logic4(
                                      element.packed.get_logic9(bit))
                                : sign);
                    }
                    value.elements[at] = std::move(bits);
                }
                return value;
            }
        }
        // A string literal initializes an unpacked array of bytes one
        // character per element, right-justified like the packed value it
        // denotes (5.9).
        if (initializer_source != nullptr
            && initializer_source->kind == ExpressionKind::string_literal
            && type.fixed && type.dimensions.size() == 1U
            && type.element_kind == ContainerElementKind::Packed
            && type.element_width == 8U) {
            const auto text
                = specialization.evaluate_string_expression(initializer);
            if (!text) {
                return std::nullopt;
            }
            auto value = default_container_value(type);
            for (std::size_t at = 0; at < *leaf_count; ++at) {
                // Element `at` holds the character `count - at` from the end.
                const auto from_end = *leaf_count - at;
                const auto character = from_end <= text->size()
                    ? static_cast<unsigned char>(
                          (*text)[text->size() - from_end])
                    : 0U;
                value.elements[at] = PackedLogic4::from_aval_bval(
                    8U, character, 0U);
            }
            return value;
        }
        // `new[n]` with a constant size: n default elements (7.5.1).
        const auto new_array_size = [&]() -> std::optional<semantic::ExpressionId> {
            if (initializer_source == nullptr) {
                return std::nullopt;
            }
            if (initializer_source->kind == ExpressionKind::call
                && initializer_source->text == "@new-array"
                && (initializer_source->operands.size() == 1U
                    || initializer_source->operands.size() == 2U)) {
                return initializer_source->operands.front();
            }
            if (initializer_source->kind == ExpressionKind::index
                && initializer_source->operands.size() == 2U) {
                const auto callee = specialization.find_expression(
                    initializer_source->operands.front());
                if (callee && callee->systemverilog != nullptr
                    && callee->systemverilog->kind == ExpressionKind::name
                    && callee->systemverilog->text == "new") {
                    return initializer_source->operands.back();
                }
            }
            return std::nullopt;
        }();
        if (new_array_size && !type.fixed && !type.queue
            && type.element_kind != ContainerElementKind::String) {
            std::string size_error;
            const auto size = evaluate_hir_systemverilog_constant(
                specialization, *new_array_size, size_error);
            const auto count = size && !size->unbounded
                ? size->packed.known_unsigned_value()
                : std::nullopt;
            if (!count || *count > (std::uint64_t { 1 } << 24U)) {
                return std::nullopt;
            }
            auto value = default_container_value(type);
            if (initializer_source->kind == ExpressionKind::call
                && initializer_source->operands.size() == 2U) {
                // `new[n](init)` copies init and resizes it to n elements.
                auto initial = constant_container_initializer(
                    specialization, type, initializer_source->operands.back());
                if (!initial) {
                    return std::nullopt;
                }
                value = std::move(*initial);
            }
            value.elements.resize(static_cast<std::size_t>(*count),
                PackedLogic4(type.element_width,
                    type.two_state ? Logic4::zero : Logic4::x));
            return value;
        }
        // An index-keyed pattern for a one-dimensional fixed array places
        // each value at its index; `default:` fills the rest (10.9.1).
        if (initializer_source != nullptr && type.fixed
            && type.dimensions.size() == 1U
            && initializer_source->kind == ExpressionKind::assignment_pattern
            && std::ranges::any_of(initializer_source->associations,
                [](const auto& association) {
                    return association.choice_spelling == "@key";
                })) {
            const auto [left, right] = type.dimensions.front();
            std::vector<std::optional<semantic::ExpressionId>> slots(
                *leaf_count);
            std::optional<semantic::ExpressionId> keyed_default;
            std::string key_error;
            for (const auto& association : initializer_source->associations) {
                if (association.choice_spelling == "default") {
                    keyed_default = association.value;
                    continue;
                }
                if (association.choice_spelling != "@key"
                    || association.choices.size() != 1U) {
                    return std::nullopt;
                }
                const auto key = evaluate_hir_systemverilog_constant(
                    specialization, association.choices.front(), key_error);
                // An unsigned key such as 4'd8 keeps its unsigned value.
                const auto unsigned_key = key && !key->unbounded
                        && !key->signed_value
                    ? key->packed.known_unsigned_value()
                    : std::nullopt;
                const auto index = key && !key->unbounded
                    ? (key->signed_value
                          ? key->packed.known_signed_value()
                          : unsigned_key
                              && *unsigned_key <= static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int64_t>::max())
                          ? std::optional<std::int64_t> {
                                static_cast<std::int64_t>(*unsigned_key) }
                          : std::nullopt)
                    : std::nullopt;
                if (!index || *index < std::min(left, right)
                    || *index > std::max(left, right)) {
                    return std::nullopt;
                }
                const auto ordinal = static_cast<std::size_t>(left >= right
                        ? left - *index
                        : *index - left);
                slots[ordinal] = association.value;
            }
            std::vector<semantic::ExpressionId> keyed_leaves;
            for (const auto& slot : slots) {
                if (!slot && !keyed_default) {
                    return std::nullopt;
                }
                keyed_leaves.push_back(slot ? *slot : *keyed_default);
            }
            auto value = default_container_value(type);
            std::string leaf_error;
            for (std::size_t at = 0; at < keyed_leaves.size(); ++at) {
                if (type.element_kind == ContainerElementKind::String) {
                    const auto text = specialization.evaluate_string_expression(
                        keyed_leaves[at]);
                    if (!text) {
                        return std::nullopt;
                    }
                    value.string_elements[at] = *text;
                    continue;
                }
                if (type.element_kind != ContainerElementKind::Packed) {
                    return std::nullopt;
                }
                const auto constant = evaluate_hir_systemverilog_constant(
                    specialization, keyed_leaves[at], leaf_error);
                if (!constant || constant->unbounded) {
                    // `'0`, `'1`: an unsized fill of the element.
                    const auto fill = specialization.find_expression(
                        keyed_leaves[at]);
                    const auto text = fill && fill->systemverilog != nullptr
                        ? fill->systemverilog->text
                        : std::string { };
                    if (text != "'0" && text != "'1") {
                        return std::nullopt;
                    }
                    value.elements[at] = PackedLogic4(type.element_width,
                        text == "'1" ? Logic4::one : Logic4::zero);
                    continue;
                }
                PackedLogic4 element(type.element_width,
                    type.two_state ? Logic4::zero : Logic4::x);
                for (std::size_t bit = 0; bit < type.element_width; ++bit) {
                    element.set(bit, bit < constant->packed.width()
                            ? runtime::to_logic4(
                                  constant->packed.get_logic9(bit))
                            : Logic4::zero);
                }
                value.elements[at] = std::move(element);
            }
            return value;
        }
        std::vector<semantic::ExpressionId> leaves;
        std::optional<semantic::ExpressionId> default_value;
        bool valid = true;
        const auto collect = [&](const auto& self,
                                 const semantic::ExpressionId id,
                                 const std::size_t depth) -> void {
            const auto expression = specialization.find_expression(id);
            if (!expression || expression->systemverilog == nullptr) {
                valid = false;
                return;
            }
            const auto& source = *expression->systemverilog;
            const auto nested_dimensions
                = type.fixed ? type.dimensions.size() : 1U;
            if (source.kind == ExpressionKind::assignment_pattern
                && depth < nested_dimensions) {
                for (const auto& association : source.associations) {
                    if (association.choice_spelling == "default") {
                        if (depth != 0U || default_value) {
                            valid = false;
                        }
                        default_value = association.value;
                        continue;
                    }
                    if (!association.choice_spelling.empty()) {
                        valid = false;
                        return;
                    }
                    self(self, association.value, depth + 1U);
                }
                return;
            }
            // An unpacked array concatenation lists the elements of one
            // dimension (10.10).
            if (source.kind == ExpressionKind::concatenation
                && ((depth == 0U && !type.fixed)
                    || (type.fixed && depth < nested_dimensions))) {
                for (const auto operand : source.operands) {
                    self(self, operand, depth + 1U);
                }
                return;
            }
            if (depth != nested_dimensions) {
                valid = false;
                return;
            }
            leaves.push_back(id);
        };
        collect(collect, initializer, 0U);
        if (!valid
            || (type.fixed && !default_value && leaves.size() != *leaf_count)
            || (type.fixed && leaves.size() > *leaf_count)
            || (!type.fixed && default_value)
            || (type.maximum_elements && leaves.size() > *type.maximum_elements)) {
            return std::nullopt;
        }
        auto value = default_container_value(type);
        const auto count = type.fixed ? *leaf_count : leaves.size();
        std::string error;
        const auto leaf_value = [&](const semantic::ExpressionId id,
                                    const std::size_t at) -> bool {
            if (type.element_kind == ContainerElementKind::String) {
                const auto text = specialization.evaluate_string_expression(id);
                if (!text) {
                    return false;
                }
                value.string_elements[at] = *text;
                return true;
            }
            if (type.element_kind == ContainerElementKind::Scalar) {
                auto scalar = evaluate_hir_systemverilog_scalar_constant(
                    specialization, id, error);
                if (scalar) {
                    scalar = frontend::convert_systemverilog_scalar_constant(
                        *scalar, type.scalar_kind, error);
                }
                if (!scalar) {
                    return false;
                }
                value.elements[at] = PackedLogic4::from_aval_bval(
                    type.element_width, scalar->bits, 0U);
                return true;
            }
            auto constant = evaluate_hir_systemverilog_constant(
                specialization, id, error);
            // A keyed pattern for a packed structure element takes the
            // element type's layout (10.9.2).
            if ((!constant || constant->unbounded) && element_type != nullptr) {
                std::string typed_error;
                constant = evaluate_hir_systemverilog_typed_constant(
                    specialization, id, *element_type, typed_error);
            }
            if (!constant || constant->unbounded) {
                // A string literal is its characters, eight bits each,
                // right-justified in the element (5.9).
                const auto literal = specialization.find_expression(id);
                if (!literal || literal->systemverilog == nullptr
                    || literal->systemverilog->kind
                        != ExpressionKind::string_literal) {
                    return false;
                }
                const auto text = specialization.evaluate_string_expression(id);
                if (!text) {
                    return false;
                }
                PackedLogic4 element(type.element_width, Logic4::zero);
                for (std::size_t character = 0; character < text->size();
                    ++character) {
                    const auto byte = static_cast<unsigned char>(
                        (*text)[text->size() - 1U - character]);
                    for (std::size_t bit = 0; bit < 8U; ++bit) {
                        const auto position = character * 8U + bit;
                        if (position < type.element_width) {
                            element.set(position, ((byte >> bit) & 1U) != 0U
                                    ? Logic4::one
                                    : Logic4::zero);
                        }
                    }
                }
                value.elements[at] = std::move(element);
                return true;
            }
            PackedLogic4 element(type.element_width,
                type.two_state ? Logic4::zero : Logic4::x);
            const auto fill = constant->signed_value && constant->width != 0U
                ? constant->packed.get(constant->width - 1U)
                : Logic4::zero;
            for (std::size_t bit = 0; bit < type.element_width; ++bit) {
                auto state = bit < constant->packed.width()
                    ? runtime::to_logic4(constant->packed.get_logic9(bit))
                    : fill;
                if (type.two_state
                    && state != Logic4::zero && state != Logic4::one) {
                    state = Logic4::zero;
                }
                element.set(bit, state);
            }
            value.elements[at] = std::move(element);
            return true;
        };
        if (!type.fixed) {
            if (type.element_kind == ContainerElementKind::String) {
                value.string_elements.assign(count, std::string { });
            } else {
                value.elements.assign(count,
                    PackedLogic4(type.element_width,
                        type.two_state ? Logic4::zero : Logic4::x));
            }
        }
        for (std::size_t at = 0; at < count; ++at) {
            const auto source = at < leaves.size() ? leaves[at]
                                                    : *default_value;
            if (!leaf_value(source, at)) {
                return std::nullopt;
            }
        }
        return value;
    }

    template<typename LocalMap, typename GlobalMap>
    bool register_static_storage_alias(LocalMap& local, GlobalMap& global,
        const std::string& name, const std::string& path,
        const std::string& key)
    {
        const auto found = local.find(name);
        if (found == local.end()) {
            return false;
        }
        const auto id = found->second;
        local.emplace(key, id);
        global.emplace(path + "." + key, id);
        return true;
    }

} // namespace

std::optional<ContainerValue>
hierarchy_sv_type_layout_detail::systemverilog_constant_container_initializer(
    const semantic::SpecializedHirUnit& specialization,
    const ContainerType& type,
    const semantic::ExpressionId initializer,
    const semantic::sv::TypeReference* const element_type)
{
    return constant_container_initializer(
        specialization, type, initializer, element_type);
}

bool HierarchyBuilder::materialize_compiled_systemverilog_static_storage(
    const semantic::sv::Unit& unit,
    SystemVerilogHirMaterialization& materialization,
    const std::span<const semantic::DeclarationId> declarations,
    const std::span<const semantic::StatementId> statements,
    const std::string& scope_path,
    const bool automatic,
    const std::string& path,
    std::vector<PendingVirtualInterfaceInitializer>& initializers,
    const SystemVerilogPackedTypeResolver& packed_type_resolver,
    const SystemVerilogPackedDefaultResolver& packed_default_resolver,
    const SystemVerilogPackedFallbackResolver& packed_fallback_resolver)
{
    const auto& specialization = *materialization.specialization;
    for (const auto id : declarations) {
        const auto record = specialization.find_declaration(id);
        if (!record || record->systemverilog == nullptr) {
            return false;
        }
        const auto& source = *record->systemverilog;
        if (scope_path.empty()
            || (source.form != semantic::sv::DeclarationForm::variable
                && source.form != semantic::sv::DeclarationForm::port)
            || !source.type
            || source.lifetime == semantic::sv::Lifetime::automatic
            || (automatic
                && source.lifetime != semantic::sv::Lifetime::static_lifetime)) {
            continue;
        }
        const bool string = compiled_systemverilog_string_declaration(
            specialization, source);
        const bool event_array
            = source.type
            && source.type->container_form
                == semantic::sv::TypeForm::static_array
            && source.type->container_element_types.size() == 1U
            && source.type->container_element_types.front()
                    .target.spelling == "event";
        const auto container
            = hierarchy_sv_type_layout_detail::systemverilog_container_type(
                specialization, *source.type);
        const auto width
            = hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
                specialization, source);
        if (!width && !string && !container && !event_array) {
            continue;
        }
        std::optional<HirSystemVerilogConstant> packed_initializer;
        // Runtime initializers still use the callable initialization path.
        if (source.form == semantic::sv::DeclarationForm::variable
            && source.initializer) {
            if (string) {
                if (!specialization.evaluate_string_expression(*source.initializer)) {
                    continue;
                }
            } else if (container) {
                auto element_type = *source.type;
                element_type.unpacked_dimensions.clear();
                element_type.container_form.reset();
                element_type.container_element_types.clear();
                if (!constant_container_initializer(specialization, *container,
                        *source.initializer, &element_type)) {
                    continue;
                }
            } else {
                std::string error;
                packed_initializer = evaluate_hir_systemverilog_typed_constant(
                    specialization, *source.initializer, *source.type, error);
                if (!packed_initializer || packed_initializer->unbounded
                    || packed_initializer->width != *width) {
                    continue;
                }
            }
        }
        const auto key = "@static:" + std::to_string(id.value());
        if (materialization.signals.contains(key)
            || materialization.string_objects.contains(key)
            || materialization.container_objects.contains(key)) {
            continue;
        }
        auto storage = source;
        // Callable formals are variables, not design input/output nets.
        storage.form = semantic::sv::DeclarationForm::variable;
        storage.direction = semantic::sv::Direction::unknown;
        if (source.form == semantic::sv::DeclarationForm::port
            || packed_initializer) {
            storage.initializer.reset();
        }
        storage.name = scope_path + "." + source.name;
        if (!materialize_compiled_systemverilog_declaration(unit, id,
                materialization, storage, path,
                semantic::sv::UnconnectedDrive::none, initializers,
                packed_type_resolver, packed_default_resolver,
                packed_fallback_resolver)) {
            return false;
        }
        const auto signal = register_static_storage_alias(materialization.signals,
            design_.signal_by_name_, storage.name, materialization.path, key);
        if (signal) {
            static_callable_signals_.insert(materialization.signals.at(key));
        }
        if (signal && packed_initializer) {
            design_.signals_[materialization.signals.at(key)].initial_value
                = std::move(packed_initializer->packed);
        }
        const auto string_object = register_static_storage_alias(
            materialization.string_objects, design_.string_by_name_,
            storage.name, materialization.path, key);
        const auto container_object = register_static_storage_alias(
            materialization.container_objects, design_.container_by_name_,
            storage.name, materialization.path, key);
        if (!signal && !string_object && !container_object
            && !event_array) {
            return false;
        }
    }
    for (const auto id : statements) {
        const auto record = specialization.find_statement(id);
        if (!record || record->systemverilog == nullptr) {
            return false;
        }
        const auto& statement = *record->systemverilog;
        // A retained class task body belongs to the called method.
        if (statement.kind == semantic::sv::StatementKind::task_call) {
            continue;
        }
        auto nested_path = scope_path;
        if (statement.nested_scope
            && (!statement.label.empty() || !scope_path.empty())) {
            const auto name = !statement.label.empty()
                ? statement.label
                : "$block_" + std::to_string(statement.nested_scope->value());
            nested_path = scope_path.empty() ? name : scope_path + "." + name;
        }
        if (!materialize_compiled_systemverilog_static_storage(unit,
                materialization,
                statement.kind == semantic::sv::StatementKind::loop
                    ? std::span<const semantic::DeclarationId> { }
                    : statement.declarations,
                statement.statements,
                nested_path, automatic, path, initializers,
                packed_type_resolver, packed_default_resolver,
                packed_fallback_resolver)
            || !materialize_compiled_systemverilog_static_storage(unit,
                materialization, { }, statement.else_statements,
                nested_path, automatic, path, initializers,
                packed_type_resolver, packed_default_resolver,
                packed_fallback_resolver)) {
            return false;
        }
        for (const auto& alternative : statement.case_alternatives) {
            if (!materialize_compiled_systemverilog_static_storage(unit,
                    materialization, { }, alternative.statements,
                    nested_path, automatic, path, initializers,
                    packed_type_resolver, packed_default_resolver,
                    packed_fallback_resolver)) {
                return false;
            }
        }
    }
    return true;
}

bool HierarchyBuilder::materialize_compiled_systemverilog_declaration(
    const semantic::sv::Unit& unit,
    const semantic::DeclarationId declaration_id,
    SystemVerilogHirMaterialization& materialization,
    const semantic::sv::Declaration& declaration,
    const std::string& path,
    const semantic::sv::UnconnectedDrive unconnected_drive,
    std::vector<PendingVirtualInterfaceInitializer>&
        pending_virtual_interface_initializers,
    const SystemVerilogPackedTypeResolver& packed_type_resolver,
    const SystemVerilogPackedDefaultResolver& packed_default_resolver,
    const SystemVerilogPackedFallbackResolver& packed_fallback_resolver,
    const bool runtime_package_initializer)
{
    const auto whole_alias_signal = [&](
                                        const SystemVerilogHirMaterialization& value,
                                        const std::string_view name) -> std::optional<SignalId> {
        const auto& signals = value.signals;
        // A generate block's own declaration may share the name of an
        // enclosing scope's object without denoting it (IEEE 1800-2017
        // 27.4).
        if (const auto direct = signals.find_local(std::string { name });
            direct != signals.end()) {
            return direct->second;
        }
        const auto group = std::ranges::find_if(
            value.alias_plan.whole_groups,
            [&](const auto& candidate) {
                return std::ranges::find(candidate, name)
                    != candidate.end();
            });
        if (group == value.alias_plan.whole_groups.end()) {
            return std::nullopt;
        }
        for (const auto& alias : *group) {
            if (const auto found = signals.find(alias);
                found != signals.end()) {
                return found->second;
            }
        }
        return std::nullopt;
    };
    const auto coverage_owner
        = std::string { compiled_systemverilog_library(unit) }
        + "." + unit.name;
    const auto is_covergroup_object
        = [&](const semantic::sv::Declaration& value) {
              const auto& instances
                  = compiled_->systemverilog_hir.covergroup_instances();
              return std::ranges::any_of(
                  instances,
                  [&](const semantic::sv::CovergroupInstance& instance) {
                      return !instance.class_member_template
                          && instance.owner_identity == coverage_owner
                          && instance.name == value.name
                          && instance.source == value.source;
                  });
          };
    const auto compiled_interface_type = [&](
                                             const std::string_view name,
                                             const std::string_view library) -> const semantic::sv::Unit* {
        const auto found = compiled_->find_unit(
            semantic::UnitKind::systemverilog_interface,
            library, name);
        return found && found->systemverilog != nullptr
            ? found->systemverilog
            : nullptr;
    };
    auto& signals = materialization.signals;
    auto& string_objects = materialization.string_objects;
    auto& container_objects = materialization.container_objects;
    auto& container_declaration_bindings
        = materialization.container_declaration_bindings;
    auto& read_only_strings = materialization.read_only_strings;
    auto& read_only_containers
        = materialization.read_only_containers;
    auto& read_only_signals = materialization.read_only_signals;
    auto& declared_signal_names
        = materialization.declared_signal_names;
    const auto& working_specialization
        = *materialization.specialization;
    const semantic::CompiledDesignResolver net_legality_type_resolver {
        working_specialization
    };
    const auto& materialized_path = materialization.path;
    using Form = semantic::sv::DeclarationForm;
    if (declaration.callable) {
        if (declaration.callable->function
            && declaration.callable->lifetime != semantic::sv::Lifetime::automatic
            && declaration.type
            && declaration.type->target.spelling != "void"
            && (compiled_systemverilog_string_declaration(declaration)
                || hierarchy_sv_type_layout_detail::systemverilog_container_type(
                    working_specialization, *declaration.type)
                || hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
                    working_specialization, declaration))) {
            auto result = declaration;
            result.form = Form::variable;
            result.callable.reset();
            result.initializer.reset();
            if (!materialize_compiled_systemverilog_declaration(unit,
                    declaration_id, materialization, result, path,
                    semantic::sv::UnconnectedDrive::none,
                    pending_virtual_interface_initializers,
                    packed_type_resolver, packed_default_resolver,
                    packed_fallback_resolver)) {
                return false;
            }
            const auto key = "@static:" + std::to_string(declaration_id.value());
            const auto signal = register_static_storage_alias(materialization.signals,
                design_.signal_by_name_, result.name, materialization.path, key);
            const auto string_object = register_static_storage_alias(
                materialization.string_objects, design_.string_by_name_,
                result.name, materialization.path, key);
            const auto container_object = register_static_storage_alias(
                materialization.container_objects, design_.container_by_name_,
                result.name, materialization.path, key);
            if (!signal && !string_object && !container_object) {
                return false;
            }
            if (signal) {
                static_callable_signals_.insert(
                    materialization.signals.at(key));
            }
        }
        return materialize_compiled_systemverilog_static_storage(unit,
            materialization, declaration.children, declaration.statements,
            declaration.name,
            declaration.callable->lifetime == semantic::sv::Lifetime::automatic,
            path, pending_virtual_interface_initializers,
            packed_type_resolver, packed_default_resolver,
            packed_fallback_resolver);
    }
    const auto net_type_for = [](const semantic::sv::Declaration& value,
                                  const semantic::sv::TypeReference& type) {
        const auto explicit_net_type = [](
                                           const semantic::sv::TypeReference& candidate) {
            auto* current = &candidate;
            while (true) {
                if (!current->systemverilog_net_type.empty()) {
                    return std::string_view {
                        current->systemverilog_net_type
                    };
                }
                const auto spelling
                    = std::string_view { current->target.spelling };
                if (semantic::sv::is_builtin_net_kind(spelling)) {
                    return spelling;
                }
                if (current->container_element_types.size() != 1U) {
                    return std::string_view { };
                }
                current = &current->container_element_types.front();
            }
        };
        const auto declared = explicit_net_type(*value.type);
        const auto effective = explicit_net_type(type);
        return !declared.empty() ? declared
            : !effective.empty() ? effective
            : value.form == Form::net
            ? std::string_view { value.type->target.spelling }
            : std::string_view { };
    };
    const auto configure_net_signal = [](
                                          Signal& signal,
                                          const semantic::sv::Declaration& value,
                                          const std::string_view net_type) {
        if (net_type == "trireg") {
            signal.charge_strength = value.charge_strength
                ? static_cast<StrengthRank>(*value.charge_strength)
                : StrengthRank::medium;
            if (value.charge_decay) {
                signal.charge_decay
                    = value.charge_decay->primary.magnitude;
            }
        }
        if (net_type == "tri0" || net_type == "tri1") {
            signal.implicit_driver = net_type == "tri0"
                ? Logic4::zero
                : Logic4::one;
        } else if (net_type == "supply0"
            || net_type == "supply1") {
            signal.implicit_driver = net_type == "supply0"
                ? Logic4::zero
                : Logic4::one;
            signal.implicit_drive_strength = {
                StrengthRank::supply, StrengthRank::supply
            };
        }
    };
    const auto net_initial_for = [](
                                     const frontend::ValueDomain domain,
                                     const std::string_view net_type) {
        if (net_type == "tri0" || net_type == "supply0") {
            return Logic4::zero;
        }
        if (net_type == "tri1" || net_type == "supply1") {
            return Logic4::one;
        }
        return domain == frontend::ValueDomain::Logic4
            ? Logic4::z
            : Logic4::zero;
    };
    const auto register_resolution = [&](const SignalId id) {
        if (!declaration.type
                ->systemverilog_resolution_function.empty()) {
            resolver_by_signal_.insert_or_assign(
                id,
                declaration.type
                    ->systemverilog_resolution_function);
        }
    };
    if (declaration.form != Form::port
        && declaration.form != Form::net
        && declaration.form != Form::variable) {
        return true;
    }
    if (declaration.type
        && declaration.type->virtual_interface) {
        const auto* interface = compiled_interface_type(
            declaration.type->interface_type,
            compiled_systemverilog_library(unit));
        if (interface == nullptr) {
            report(
                "FSIM-ELAB-SVIFACE-003",
                "virtual interface '" + materialized_path + "."
                    + declaration.name + "' requires unknown type '"
                    + declaration.type->interface_type + "'",
                compiled_source_span(
                    *compiled_, declaration.source));
        } else if (!declaration.type->interface_modport.empty()
            && std::ranges::none_of(
                interface->modports,
                [&](const semantic::sv::Modport& modport) {
                    return modport.name
                        == declaration.type->interface_modport;
                })) {
            report(
                "FSIM-ELAB-SVIFACE-004",
                "virtual interface '" + materialized_path + "."
                    + declaration.name + "' selects unknown modport '"
                    + declaration.type->interface_modport
                    + "' on interface type '"
                    + declaration.type->interface_type + "'",
                compiled_source_span(
                    *compiled_, declaration.source));
        }
    }
    // Covergroup objects are executable coverage inventory, not
    // packed simulation signals. Their declaration and mutable state
    // remain owned by the compiled HIR coverage handoff.
    if (is_covergroup_object(declaration)) {
        return true;
    }
    if (compiled_systemverilog_string_declaration(
            working_specialization, declaration)) {
        return materialize_compiled_systemverilog_string_declaration(
            declaration,
            working_specialization,
            materialized_path,
            string_objects,
            read_only_strings,
            compiled_port_direction(declaration),
            compiled_source_span(*compiled_, declaration.source));
    }
    const auto builtin_net_kind = declaration.type
        ? net_type_for(declaration, *declaration.type)
        : std::string_view { };
    if (declaration.type
        && semantic::sv::is_builtin_net_kind(builtin_net_kind)) {
        struct BuiltinNetTypeProperties final {
            bool packed_integral { };
            bool four_state { };
            bool valid_net_type { };
        };
        std::vector<semantic::TypeId> active_types;
        const auto classify_net_type = [&](const auto& self,
                                           const semantic::sv::TypeReference& input,
                                           const semantic::ScopeId scope)
            -> BuiltinNetTypeProperties {
            const auto effective
                = net_legality_type_resolver.effective_systemverilog_type(input, scope)
                      .value_or(input);
            const auto effective_definition = effective.target.target.valid()
                ? working_specialization.find_type(
                      effective.target.target)
                : std::nullopt;
            const auto resolved
                = net_legality_type_resolver.underlying_systemverilog_type(
                                   effective, scope)
                      .value_or(effective);
            const auto definition = resolved.target.target.valid()
                ? working_specialization.find_type(
                      resolved.target.target)
                : std::nullopt;
            auto form = resolved.value_form.value_or(
                semantic::sv::TypeForm::unresolved);
            if (form == semantic::sv::TypeForm::unresolved
                && definition && definition->systemverilog != nullptr) {
                form = definition->systemverilog->form;
            }
            if (form == semantic::sv::TypeForm::unresolved) {
                form = effective.value_form.value_or(
                    effective_definition
                            && effective_definition->systemverilog != nullptr
                        ? effective_definition->systemverilog->form
                        : semantic::sv::TypeForm::unresolved);
            }
            // The resolver unwraps an enum to its base type, but the inline
            // base can still carry the source enum form. Recover a builtin
            // only when that resolved base has no nominal type target; named
            // unresolved types remain fail-closed.
            if (form == semantic::sv::TypeForm::unresolved
                || (form == semantic::sv::TypeForm::enumeration
                    && !resolved.target.target.valid())) {
                const auto& spelling = resolved.target.spelling;
                if (spelling == "bit" || spelling == "logic"
                    || spelling == "reg" || spelling == "byte"
                    || spelling == "shortint" || spelling == "int"
                    || spelling == "integer" || spelling == "longint"
                    || spelling == "time") {
                    form = semantic::sv::TypeForm::packed_integral;
                }
            }
            const auto invalid = BuiltinNetTypeProperties { };

            const bool is_fixed_array
                = resolved.container_form
                    == semantic::sv::TypeForm::static_array
                || effective.container_form
                    == semantic::sv::TypeForm::static_array
                || !resolved.unpacked_dimensions.empty()
                || !effective.unpacked_dimensions.empty()
                || form == semantic::sv::TypeForm::static_array;
            if (is_fixed_array) {
                const auto& dimensions = effective.unpacked_dimensions.empty()
                    ? resolved.unpacked_dimensions
                    : effective.unpacked_dimensions;
                const auto& array_element_types
                    = effective.container_element_types.empty()
                    ? resolved.container_element_types
                    : effective.container_element_types;
                const auto array_definition
                    = form == semantic::sv::TypeForm::static_array
                        && effective_definition
                    ? effective_definition
                    : definition;
                if ((resolved.container_form
                        && *resolved.container_form
                            != semantic::sv::TypeForm::static_array)
                    || (effective.container_form
                        && *effective.container_form
                            != semantic::sv::TypeForm::static_array)
                    || (form != semantic::sv::TypeForm::static_array
                        && resolved.container_form
                            != semantic::sv::TypeForm::static_array
                        && effective.container_form
                            != semantic::sv::TypeForm::static_array)
                    || dimensions.empty()
                    || !std::ranges::all_of(
                        dimensions, [&](const semantic::sv::PackedRange& range) {
                            // Static array dimensions may use parameters;
                            // dynamic identifiers remain unresolved here.
                            const auto left = range.left_expression
                                ? working_specialization
                                      .evaluate_integral_expression(
                                          *range.left_expression)
                                : range.left;
                            const auto right = range.right_expression
                                ? working_specialization
                                      .evaluate_integral_expression(
                                          *range.right_expression)
                                : range.right;
                            return left.has_value() && right.has_value();
                        })) {
                    return invalid;
                }
                const semantic::sv::TypeReference* element { };
                if (array_element_types.size() == 1U) {
                    element = &array_element_types.front();
                } else if (array_element_types.empty()
                    && array_definition
                    && array_definition->systemverilog != nullptr) {
                    element = &array_definition->systemverilog->base;
                }
                if (element == nullptr) {
                    return invalid;
                }
                auto element_scope = scope;
                if (array_definition
                    && array_definition->systemverilog != nullptr) {
                    const auto type_declaration
                        = working_specialization.find_declaration(
                            array_definition->systemverilog->declaration);
                    if (type_declaration
                        && type_declaration->systemverilog != nullptr) {
                        element_scope
                            = type_declaration->systemverilog->scope;
                    }
                }
                const auto child = self(self, *element, element_scope);
                return { false, false, child.valid_net_type };
            }

            if (form == semantic::sv::TypeForm::packed_structure
                || form == semantic::sv::TypeForm::packed_union) {
                if (!definition || definition->systemverilog == nullptr
                    || definition->systemverilog->members.empty()
                    || std::ranges::find(active_types,
                        resolved.target.target) != active_types.end()) {
                    return invalid;
                }
                active_types.push_back(resolved.target.target);
                const auto type_declaration
                    = working_specialization.find_declaration(
                        definition->systemverilog->declaration);
                const auto member_scope = type_declaration
                        && type_declaration->systemverilog != nullptr
                    ? type_declaration->systemverilog->scope
                    : scope;
                bool aggregate_four_state { };
                bool members_are_integral = true;
                for (const auto& member
                     : definition->systemverilog->members) {
                    const auto child = self(
                        self, member.type, member_scope);
                    if (!child.packed_integral) {
                        members_are_integral = false;
                        break;
                    }
                    aggregate_four_state
                        = aggregate_four_state || child.four_state;
                }
                active_types.pop_back();
                if (!members_are_integral) {
                    return invalid;
                }
                return { true, aggregate_four_state,
                    aggregate_four_state };
            }

            // IEEE 1800-2017 6.7.1 permits unpacked arrays and structures,
            // but not unpacked unions, as built-in net data types.
            if (form == semantic::sv::TypeForm::unpacked_structure) {
                if (!definition || definition->systemverilog == nullptr
                    || definition->systemverilog->members.empty()
                    || std::ranges::find(active_types,
                        resolved.target.target) != active_types.end()) {
                    return invalid;
                }
                active_types.push_back(resolved.target.target);
                const auto type_declaration
                    = working_specialization.find_declaration(
                        definition->systemverilog->declaration);
                const auto member_scope = type_declaration
                        && type_declaration->systemverilog != nullptr
                    ? type_declaration->systemverilog->scope
                    : scope;
                bool members_are_net_types = true;
                for (const auto& member
                     : definition->systemverilog->members) {
                    if (!self(self, member.type, member_scope)
                             .valid_net_type) {
                        members_are_net_types = false;
                        break;
                    }
                }
                active_types.pop_back();
                return { false, false, members_are_net_types };
            }

            if (form == semantic::sv::TypeForm::packed_integral) {
                bool four_state = resolved.four_state;
                const auto& spelling = resolved.target.spelling;
                if (spelling == "logic" || spelling == "reg"
                    || spelling == "integer" || spelling == "time") {
                    four_state = true;
                } else if (spelling == "bit" || spelling == "byte"
                    || spelling == "shortint" || spelling == "int"
                    || spelling == "longint") {
                    four_state = false;
                }
                return { true, four_state, four_state };
            }

            if (form == semantic::sv::TypeForm::enumeration
                && definition && definition->systemverilog != nullptr) {
                const auto type_declaration
                    = working_specialization.find_declaration(
                        definition->systemverilog->declaration);
                const auto base_scope = type_declaration
                        && type_declaration->systemverilog != nullptr
                    ? type_declaration->systemverilog->scope
                    : scope;
                const auto base = self(self,
                    definition->systemverilog->base, base_scope);
                return { base.packed_integral, base.four_state,
                    base.valid_net_type };
            }

            return invalid;
        };
        const auto net_type_properties = classify_net_type(
            classify_net_type, *declaration.type, declaration.scope);
        if (!net_type_properties.valid_net_type) {
            report(
                "FSIM-ELAB-SVNETTYPE-005",
                "built-in net '" + declaration.name
                    + "' requires a four-state integral data type or a "
                      "fixed aggregate recursively composed of such types",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
    }
    const bool event_array
        = declaration.type
        && declaration.type->container_form
            == semantic::sv::TypeForm::static_array
        && declaration.type->container_element_types.size() == 1U
        && declaration.type->container_element_types.front()
                .target.spelling == "event";
    if (event_array) {
        const auto full_name
            = materialized_path + "." + declaration.name;
        if (declared_signal_names.contains(declaration.name)) {
            report(
                "FSIM-ELAB-HIR-001",
                "duplicate compiled signal declaration '"
                    + declaration.name + "'",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        if (declaration.type->unpacked_dimensions.size() != 1U
            || declaration.type->container_element_types.size() != 1U) {
            report(
                "FSIM-ELAB-SVEVENT-014",
                "this implementation supports fixed one-dimensional named "
                "event arrays",
                compiled_source_span(
                    *compiled_, declaration.source));
            return true;
        }
        const auto& dimension
            = declaration.type->unpacked_dimensions.front();
        const auto boundary = [&](
                                   const std::optional<std::int64_t> value,
                                   const std::optional<semantic::ExpressionId>
                                       expression) {
            return expression
                ? working_specialization.evaluate_integral_expression(
                      *expression)
                : value;
        };
        const auto left_value = boundary(
            dimension.left, dimension.left_expression);
        const auto right_value = boundary(
            dimension.right, dimension.right_expression);
        if (!left_value || !right_value
            || *left_value < std::numeric_limits<std::int32_t>::min()
            || *left_value > std::numeric_limits<std::int32_t>::max()
            || *right_value < std::numeric_limits<std::int32_t>::min()
            || *right_value > std::numeric_limits<std::int32_t>::max()) {
            report(
                "FSIM-ELAB-SVCONTAINER-020",
                "named event array requires a resolvable 32-bit index range",
                compiled_source_span(
                    *compiled_, declaration.source));
            return true;
        }
        const auto left = *left_value;
        const auto right = *right_value;
        const auto count = static_cast<std::uint64_t>(
            left <= right ? right - left + 1 : left - right + 1);
        if (count > maximum_compiled_event_array_elements
            || count > std::numeric_limits<SignalId>::max()
            || design_.signals_.size()
                > std::numeric_limits<SignalId>::max() - count) {
            report(
                "FSIM-ELAB-SVCONTAINER-020",
                "named event array exceeds the bounded event-signal "
                "materialization or dispatch limit",
                compiled_source_span(
                    *compiled_, declaration.source));
            return true;
        }
        const auto span = compiled_source_span(
            *compiled_, declaration.source);
        const auto step = left <= right ? std::int64_t { 1 }
                                        : std::int64_t { -1 };
        for (auto index = left;; index += step) {
            const auto index_text = std::to_string(index);
            const auto element_name
                = full_name + "[" + index_text + "]";
            const auto signal_id = static_cast<SignalId>(
                design_.signals_.size());
            SignalInfo info;
            info.id = signal_id;
            info.name = element_name;
            info.width = 1U;
            info.type_name = "event";
            info.source_domain = frontend::ValueDomain::Logic4;
            info.declaration_span = span;
            Signal signal {
                element_name,
                PackedLogic4(1U, Logic4::zero),
                ResolutionKind::none,
                value_kind(frontend::ValueDomain::Logic4),
                std::nullopt,
                { StrengthRank::pull, StrengthRank::pull },
                std::nullopt,
                std::nullopt,
                frontend::SystemVerilogScalarKind::None,
            };
            signal.event_variable = true;
            design_.signal_info_.push_back(std::move(info));
            design_.signals_.push_back(std::move(signal));
            const auto source_name
                = declaration.name + "[" + index_text + "]";
            const auto stable_name
                = "@event-array-declaration:"
                + std::to_string(declaration_id.value())
                + "[" + index_text + "]";
            signals.insert_or_assign(source_name, signal_id);
            signals.insert_or_assign(element_name, signal_id);
            signals.insert_or_assign(stable_name, signal_id);
            design_.signal_by_name_.insert_or_assign(
                element_name, signal_id);
            if (design_.roots_.size() == 1U) {
                design_.signal_by_name_.insert_or_assign(
                    source_name, signal_id);
                const auto root_prefix = active_root_ + ".";
                if (materialized_path.starts_with(root_prefix)) {
                    design_.signal_by_name_.insert_or_assign(
                        materialized_path.substr(root_prefix.size())
                            + "." + source_name,
                        signal_id);
                }
            }
            if (index == right) {
                break;
            }
        }
        declared_signal_names.emplace(declaration.name);
        return true;
    }
    const auto executable_container_type = declaration.type
        ? hierarchy_sv_type_layout_detail::
              systemverilog_container_type(
                  working_specialization, *declaration.type)
        : std::nullopt;
    if (declaration.type
        && (declaration.type->container_form
            || executable_container_type)) {
        const auto full_name
            = materialized_path + "." + declaration.name;
        if (declaration.form == Form::port) {
            const auto alias = container_objects.find(
                declaration.name);
            if (alias != container_objects.end()) {
                const auto info = std::ranges::find_if(
                    design_.container_object_info_.rbegin(),
                    design_.container_object_info_.rend(),
                    [&](const ContainerObjectInfo& candidate) {
                        return candidate.id == alias->second;
                    });
                if (info
                    == design_.container_object_info_.rend()) {
                    report(
                        "FSIM-ELAB-HIR-001",
                        "compiled container port association for '"
                            + full_name
                            + "' has an invalid object",
                        compiled_source_span(
                            *compiled_, declaration.source));
                    return false;
                }
                container_declaration_bindings.insert_or_assign(
                    declaration_id.value(),
                    ContainerDeclarationBinding {
                        alias->second,
                        compiled_port_direction(declaration)
                            == frontend::PortDirection::Input,
                        materialization.specialization == nullptr
                            ? nullptr
                            : &materialization.specialization
                                   ->specialization(),
                    });
                container_objects.insert_or_assign(
                    full_name, alias->second);
                design_.container_by_name_.insert_or_assign(
                    full_name, alias->second);
                if (design_.roots_.size() == 1U
                    && materialized_path == active_root_) {
                    design_.container_by_name_.insert_or_assign(
                        declaration.name, alias->second);
                }
                if (compiled_port_direction(declaration)
                    == frontend::PortDirection::Input) {
                    read_only_containers.emplace(
                        declaration.name);
                    read_only_containers.emplace(full_name);
                }
                return true;
            }
        }
        std::optional<ContainerValue> initial_container;
        std::string initializer_failure;
        // An instance's value for an unpacked array parameter (6.20.2).
        const auto array_actual = declaration.array_parameter
            ? working_specialization.systemverilog_array_actual(
                  declaration_id)
            : std::nullopt;
        const auto array_elements = declaration.array_parameter
            ? working_specialization.systemverilog_array_elements(
                  declaration_id)
            : std::nullopt;
        if (array_elements) {
            initial_container = executable_container_type && array_actual
                ? array_parameter_container(
                      *executable_container_type, *array_actual)
                : std::nullopt;
            if (!initial_container && executable_container_type) {
                initial_container = typed_array_parameter_container(
                    *executable_container_type, *array_elements);
            }
            if (!initial_container) {
                report(
                    "FSIM-ELAB-PARAM-001",
                    "unpacked array parameter '" + declaration.name
                        + "' has an actual with the wrong number of "
                          "elements",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return false;
            }
        } else if (declaration.initializer && executable_container_type) {
            auto element_type = *declaration.type;
            element_type.unpacked_dimensions.clear();
            element_type.container_form.reset();
            element_type.container_element_types.clear();
            initial_container = constant_container_initializer(
                working_specialization, *executable_container_type,
                *declaration.initializer, &element_type, &initializer_failure);
        }
        // A variable's initializer that is not a constant runs as its
        // time-zero declaration initializer process (6.8).
        const bool procedural_initializer = declaration.initializer
            && !initial_container && declaration.form == Form::variable
            && std::ranges::any_of(unit.processes,
                [&](const semantic::ProcessId process_id) {
                    const auto process = compiled_->find_process(process_id);
                    return process && process->systemverilog != nullptr
                        && process->systemverilog->name
                        == "$declaration_initializer_" + declaration.name;
                });
        if (declaration.initializer && !initial_container
            && !procedural_initializer) {
            report(
                "FSIM-ELAB-HIR-001",
                "compiled container declaration '"
                    + declaration.name
                    + "' has an unsupported initializer"
                    + (initializer_failure.empty()
                            ? std::string { }
                            : ": " + initializer_failure),
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        if (declaration.type->container_form
                == semantic::sv::TypeForm::associative_array
            && declaration.type->associative_index) {
            semantic::sv::TypeReference index_reference;
            index_reference.target
                = *declaration.type->associative_index;
            const semantic::CompiledDesignResolver type_resolver {
                working_specialization
            };
            const auto index_type
                = type_resolver.underlying_systemverilog_type(
                                   index_reference,
                                   working_specialization.scope())
                      .value_or(index_reference);
            const auto spelling = index_type.target.spelling.empty()
                ? declaration.type->associative_index->spelling
                : index_type.target.spelling;
            const auto form = index_type.value_form.value_or(
                semantic::sv::TypeForm::unresolved);
            const bool nonintegral_scalar
                = spelling == "shortreal" || spelling == "real"
                || spelling == "realtime" || spelling == "chandle";
            const bool invalid_form
                = form == semantic::sv::TypeForm::unpacked_structure
                || form == semantic::sv::TypeForm::tagged_union
                || form == semantic::sv::TypeForm::unpacked_union
                || form == semantic::sv::TypeForm::class_handle
                || index_type.container_form.has_value();
            if (nonintegral_scalar || invalid_form) {
                report(
                    "FSIM-ELAB-SVCONTAINER-013",
                    "associative-array indices require a resolved string "
                    "or integral type with an executable width",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return true;
            }
        }
        auto type = executable_container_type;
        if (!type) {
            if (declaration.type->container_form
                == semantic::sv::TypeForm::static_array) {
                report(
                    "FSIM-ELAB-SVCONTAINER-020",
                    "static unpacked-array bounds and storage must be "
                    "locally constant, 32-bit, and within the "
                    "per-container owning-storage budget",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return true;
            }
            report(
                "FSIM-ELAB-HIR-001",
                "compiled container declaration '"
                    + declaration.name
                    + "' has no executable container layout",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        const auto index = design_.container_objects_.size();
        const auto id = static_cast<ContainerObjectId>(index);
        if (static_cast<std::size_t>(id) != index) {
            throw std::length_error(
                "too many elaborated container objects");
        }
        design_.container_object_info_.push_back(
            ContainerObjectInfo {
                id,
                full_name,
                *type,
                compiled_source_span(
                    *compiled_, declaration.source),
                declaration.form == Form::port,
                compiled_port_direction(declaration),
                std::nullopt,
            });
        // The packed dimensions of the elements, for selects within an
        // element named from another scope (IEEE 1800-2017 7.4.5, 23.6).
        if (declaration.type
            && (type->element_kind == ContainerElementKind::Packed
                || type->element_kind == ContainerElementKind::Scalar)) {
            const semantic::CompiledDesignResolver type_resolver {
                working_specialization
            };
            auto element_type = *declaration.type;
            element_type.unpacked_dimensions.clear();
            element_type.container_form.reset();
            element_type.container_element_types.clear();
            if (const auto element
                = type_resolver.underlying_systemverilog_type(
                    element_type, declaration.scope)) {
                std::vector<std::pair<std::int64_t, std::int64_t>> shape;
                bool resolved = true;
                std::uint64_t width = 1U;
                const auto append = [&](const semantic::sv::PackedRange&
                                            range) {
                    // Bounds can name parameters of this scope.
                    const auto bound
                        = [&](const std::optional<std::int64_t> value,
                              const std::optional<semantic::ExpressionId>
                                  expression) {
                              return value ? value
                                  : expression
                                  ? working_specialization
                                        .evaluate_integral_expression(
                                            *expression)
                                  : std::nullopt;
                          };
                    const auto left
                        = bound(range.left, range.left_expression);
                    const auto right
                        = bound(range.right, range.right_expression);
                    if (!left || !right) {
                        resolved = false;
                        return;
                    }
                    shape.emplace_back(*left, *right);
                    width *= support::index_distance(*left, *right) + 1U;
                };
                if (!element->packed_dimensions.empty()) {
                    for (const auto& range : element->packed_dimensions) {
                        append(range);
                    }
                } else if (element->packed_range) {
                    append(*element->packed_range);
                }
                if (resolved && !shape.empty()
                    && width == type->element_width) {
                    design_.container_element_packed_shape_.emplace(
                        id, std::move(shape));
                }
            }
        }
        // Members of an unpacked structure take their declared defaults
        // (IEEE 1800-2017 7.2.2).
        if (!initial_container && declaration.type
            && type->element_kind == ContainerElementKind::Aggregate) {
            const semantic::CompiledDesignResolver type_resolver {
                working_specialization
            };
            const auto structure = type_resolver.effective_systemverilog_type(
                *declaration.type, declaration.scope);
            const auto definition = structure
                    && structure->target.target.valid()
                ? working_specialization.find_type(structure->target.target)
                : std::nullopt;
            if (definition && definition->systemverilog != nullptr
                && definition->systemverilog->form
                    == semantic::sv::TypeForm::unpacked_structure) {
                auto value = default_container_value(*type);
                const auto& members = definition->systemverilog->members;
                bool defaulted { };
                for (std::size_t member_index { };
                    member_index < members.size()
                    && member_index < value.nested_elements.size();
                    ++member_index) {
                    auto& leaf = value.nested_elements[member_index];
                    if (!members[member_index].initializer
                        || leaf.type.element_kind
                            != ContainerElementKind::Packed
                        || leaf.elements.size() != 1U) {
                        continue;
                    }
                    const auto width = leaf.type.element_width;
                    // `'0` and `'1` fill the member (5.7.1).
                    const auto fill = working_specialization.find_expression(
                        *members[member_index].initializer);
                    const auto text = fill && fill->systemverilog != nullptr
                        ? fill->systemverilog->text
                        : std::string { };
                    if (text == "'0" || text == "'1") {
                        leaf.elements.front() = PackedLogic4(width,
                            text == "'1" ? Logic4::one : Logic4::zero);
                        defaulted = true;
                        continue;
                    }
                    std::string member_error;
                    const auto constant = evaluate_hir_systemverilog_constant(
                        working_specialization, *members[member_index].initializer,
                        member_error);
                    if (!constant || constant->unbounded) {
                        continue;
                    }
                    PackedLogic4 element(width, Logic4::zero);
                    for (std::size_t bit = 0; bit < width; ++bit) {
                        element.set(bit, bit < constant->packed.width()
                                ? runtime::to_logic4(
                                      constant->packed.get_logic9(bit))
                                : Logic4::zero);
                    }
                    leaf.elements.front() = std::move(element);
                    defaulted = true;
                }
                if (defaulted) {
                    initial_container = std::move(value);
                }
            }
        }
        design_.container_objects_.push_back(ContainerObject {
            full_name,
            initial_container ? std::move(*initial_container)
                              : default_container_value(*type),
            std::nullopt,
        });
        materialization.container_objects.emplace(
            declaration.name, id);
        materialization.container_objects.emplace(full_name, id);
        container_declaration_bindings.insert_or_assign(
            declaration_id.value(),
            ContainerDeclarationBinding {
                id,
                declaration.form == Form::port
                    && compiled_port_direction(declaration)
                        == frontend::PortDirection::Input,
                materialization.specialization == nullptr
                    ? nullptr
                    : &materialization.specialization->specialization(),
            });
        design_.container_by_name_.emplace(full_name, id);
        if (design_.roots_.size() == 1U
            && materialized_path == active_root_) {
            design_.container_by_name_.emplace(
                declaration.name, id);
        }
        const auto declared_net_type = net_type_for(
            declaration, *declaration.type);
        const bool net_bridge
            = declaration.form == Form::net
            || !declared_net_type.empty();
        const bool variable_bridge
            = !net_bridge
            && (declaration.form == Form::variable
                || declaration.form == Form::port);
        // Unpacked structures and fixed arrays of them are bridged too, so
        // implicit sensitivity sees their members (IEEE 1800-2017
        // 9.2.2.2.1, 10.3).
        const bool bridgeable_elements
            = type->element_kind == ContainerElementKind::Packed
            || type->element_kind == ContainerElementKind::Scalar
            || (type->element_kind == ContainerElementKind::Aggregate
                && !type->union_aggregate);
        const auto bridge_width
            = (net_bridge || variable_bridge)
                && (type->fixed || type->aggregate_value)
                && bridgeable_elements
            ? container_signal_bridge_width(*type)
            : std::nullopt;
        constexpr auto maximum_bridge_width
            = maximum_container_storage_bytes * 8U;
        if (bridge_width
            && *bridge_width <= maximum_bridge_width) {
            if (design_.signals_.size()
                > std::numeric_limits<SignalId>::max()) {
                report(
                    "FSIM-ELAB-011",
                    "the design has too many signals for dense 32-bit IDs",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return false;
            }
            const auto signal_id = static_cast<SignalId>(
                design_.signals_.size());
            const semantic::CompiledDesignResolver type_resolver {
                working_specialization
            };
            const auto effective_type
                = type_resolver.underlying_systemverilog_type(
                                   *declaration.type, declaration.scope)
                      .value_or(*declaration.type);
            const auto domain = type->two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4;
            const auto net_type = net_bridge
                ? net_type_for(declaration, effective_type)
                : std::string_view { };
            SignalInfo info;
            info.id = signal_id;
            info.name = full_name;
            info.width = *bridge_width;
            info.type_name = declaration.type->target.spelling;
            info.source_domain = domain;
            info.systemverilog_net_type = net_type;
            info.is_signed = type->signed_elements;
            info.declaration_span = compiled_source_span(
                *compiled_, declaration.source);
            const auto initial_value = net_bridge
                ? net_initial_for(domain, net_type)
                : domain == frontend::ValueDomain::Logic4
                ? Logic4::x
                : Logic4::zero;
            // A declaration initializer also sets the bridge's value; a
            // variable's bridge starts from the container's own initial value
            // (two-state members are 0, IEEE 1800-2017 6.8).
            const auto& initial_object
                = design_.container_objects_.at(id).initial_value;
            const bool initialized_bridge
                = (declaration.initializer || array_actual || variable_bridge)
                && container_signal_bridge_width(initial_object.type)
                    == bridge_width;
            Signal signal {
                full_name,
                initialized_bridge
                    ? pack_container_signal_value(initial_object, false)
                    : PackedLogic4(*bridge_width, initial_value),
                ResolutionKind::none,
                value_kind(domain),
                std::nullopt,
                { StrengthRank::pull, StrengthRank::pull },
                std::nullopt,
                std::nullopt,
                frontend::SystemVerilogScalarKind::None,
            };
            if (net_bridge) {
                configure_net_signal(signal, declaration, net_type);
            }
            design_.signal_info_.push_back(std::move(info));
            design_.signals_.push_back(std::move(signal));
            declared_signal_names.insert(declaration.name);
            signals.insert_or_assign(
                declaration.name, signal_id);
            signals.insert_or_assign(full_name, signal_id);
            design_.signal_by_name_.insert_or_assign(
                full_name, signal_id);
            if (design_.roots_.size() == 1U) {
                design_.signal_by_name_.insert_or_assign(
                    declaration.name, signal_id);
                const auto root_prefix = active_root_ + ".";
                if (materialized_path.starts_with(root_prefix)) {
                    design_.signal_by_name_.insert_or_assign(
                        materialized_path.substr(root_prefix.size())
                            + "." + declaration.name,
                        signal_id);
                }
            }
            if (net_bridge) {
                register_resolution(signal_id);
            }
            const bool alias_writable = net_bridge
                || compiled_port_direction(declaration)
                    != frontend::PortDirection::Input;
            design_.container_signal_aliases_.push_back(
                ContainerSignalAlias {
                    id, signal_id, true, alias_writable });
            // The bridge holds a fixed array's equally wide elements, the
            // leftmost in the most significant bits.
            if (type->fixed && !type->dimensions.empty()
                && (type->element_kind == ContainerElementKind::Packed
                    || type->element_kind == ContainerElementKind::Scalar)) {
                design_.signal_unpacked_shape_.emplace(signal_id,
                    std::vector<std::pair<std::int64_t, std::int64_t>> {
                        type->dimensions.begin(), type->dimensions.end() });
                auto element_type = effective_type;
                element_type.unpacked_dimensions.clear();
                element_type.container_form.reset();
                if (const auto element = packed_type_resolver(
                        working_specialization, element_type);
                    element && !element->packed_members.empty()) {
                    design_.signal_unpacked_element_members_.emplace(
                        signal_id, element->packed_members);
                }
            }
        }
        if (compiled_port_direction(declaration)
            == frontend::PortDirection::Input) {
            materialization.read_only_containers.emplace(
                declaration.name);
            materialization.read_only_containers.emplace(full_name);
        }
        return true;
    }
    const auto executable_width
        = hierarchy_sv_type_layout_detail::systemverilog_declaration_width(
            working_specialization, declaration);
    if (!executable_width) {
        report(
            "FSIM-ELAB-TYPE-001",
            "signal '" + declaration.name
                + "' has a type that the packed simulation runtime "
                  "cannot represent",
            compiled_source_span(
                *compiled_, declaration.source));
        return false;
    }
    if (declared_signal_names.contains(declaration.name)) {
        report(
            "FSIM-ELAB-HIR-001",
            "duplicate compiled signal declaration '"
                + declaration.name + "'",
            compiled_source_span(
                *compiled_, declaration.source));
        return false;
    }
    const auto width = *executable_width;
    const semantic::CompiledDesignResolver type_resolver {
        working_specialization
    };
    const auto effective_type
        = type_resolver.underlying_systemverilog_type(
                           *declaration.type, declaration.scope)
              .value_or(*declaration.type);
    const auto full_name
        = materialized_path + "." + declaration.name;
    // Only a variable input port is read-only in its module (IEEE 1800-2017
    // 23.3.3.2). A net input port may also be driven inside the module,
    // which coerces it to inout (23.3.3.6). A port whose type is not a
    // four-state integral cannot be a net, so it is a variable.
    const auto variable_input_port = [&] {
        return compiled_port_direction(declaration)
                == frontend::PortDirection::Input
            && (declaration.form == semantic::sv::DeclarationForm::variable
                || !(declaration.type->four_state
                    || effective_type.four_state));
    };
    if (const auto alias = whole_alias_signal(
            materialization, declaration.name)) {
        if (*alias >= design_.signal_info_.size()
            || design_.signal_info_[*alias].width != width) {
            const auto actual_width
                = *alias < design_.signal_info_.size()
                ? std::to_string(
                      design_.signal_info_[*alias].width)
                : std::string { "unavailable" };
            report(
                "FSIM-ELAB-HIR-001",
                "compiled port association for '" + full_name
                    + "' has an incompatible signal width "
                      "(formal "
                    + std::to_string(width)
                    + ", actual " + actual_width + ")",
                compiled_source_span(
                    *compiled_, declaration.source));
            return false;
        }
        signals.insert_or_assign(declaration.name, *alias);
        signals.insert_or_assign(full_name, *alias);
        design_.signal_by_name_.insert_or_assign(full_name, *alias);
        if (design_.roots_.size() == 1U
            && materialized_path == active_root_) {
            design_.signal_by_name_.insert_or_assign(
                declaration.name, *alias);
        }
        if (variable_input_port()) {
            read_only_signals.insert(*alias);
        }
        return true;
    }
    if (design_.signals_.size()
        > std::numeric_limits<SignalId>::max()) {
        report(
            "FSIM-ELAB-011",
            "the design has too many signals for dense 32-bit IDs",
            compiled_source_span(
                *compiled_, declaration.source));
        return false;
    }
    const auto id = static_cast<SignalId>(
        design_.signals_.size());
    if (materialized_path == path
        && signals.contains(declaration.name)) {
        report(
            "FSIM-ELAB-HIR-001",
            "duplicate compiled signal declaration '"
                + declaration.name + "'",
            compiled_source_span(
                *compiled_, declaration.source));
        return false;
    }
    const auto domain = (declaration.type->four_state
                            || effective_type.four_state)
        ? frontend::ValueDomain::Logic4
        : frontend::ValueDomain::Bit2;
    const bool event_variable
        = declaration.type->target.spelling == "event"
        || effective_type.target.spelling == "event";
    auto direction = compiled_port_direction(declaration);
    SignalInfo info;
    info.id = id;
    info.name = full_name;
    info.width = width;
    info.type_name = event_variable
        ? "event"
        : declaration.type->target.spelling;
    info.source_domain = domain;
    auto scalar_kind = frontend::systemverilog_scalar_kind(
        effective_type.target.spelling);
    if (scalar_kind
        == frontend::SystemVerilogScalarKind::None) {
        scalar_kind = frontend::systemverilog_scalar_kind(
            declaration.type->target.spelling);
    }
    if (declaration.type->virtual_interface
        || !declaration.interface_type.empty()
        || declaration.type->target.spelling == "interface") {
        scalar_kind = frontend::SystemVerilogScalarKind::Chandle;
    }
    info.systemverilog_scalar = scalar_kind;
    const auto net_type = net_type_for(
        declaration, effective_type);
    info.systemverilog_net_type = net_type;
    info.is_signed = declaration.type->signed_value
        || effective_type.signed_value;
    if (declaration.type->container_form
            != semantic::sv::TypeForm::static_array
        && effective_type.packed_range) {
        const auto& range = *effective_type.packed_range;
        const auto left = range.left_expression
            ? hierarchy_sv_type_layout_detail::sized_range_bound(
                  working_specialization,
                  *range.left_expression)
            : range.left;
        const auto right = range.right_expression
            ? hierarchy_sv_type_layout_detail::sized_range_bound(
                  working_specialization,
                  *range.right_expression)
            : range.right;
        if (left && right) {
            info.packed_range = frontend::PackedRange {
                *left,
                *right,
                range.descending,
            };
        }
    }
    if (declaration.type->container_form
            != semantic::sv::TypeForm::static_array
        && effective_type.packed_dimensions.size() > 1U) {
        std::vector<std::pair<std::int64_t, std::int64_t>> shape;
        std::uint64_t elements { 1U };
        for (const auto& range : effective_type.packed_dimensions) {
            const auto left = range.left_expression
                ? hierarchy_sv_type_layout_detail::sized_range_bound(
                      working_specialization, *range.left_expression)
                : range.left;
            const auto right = range.right_expression
                ? hierarchy_sv_type_layout_detail::sized_range_bound(
                      working_specialization, *range.right_expression)
                : range.right;
            if (!left || !right) {
                shape.clear();
                break;
            }
            shape.emplace_back(*left, *right);
            elements *= support::index_distance(*left, *right) + 1U;
        }
        if (!shape.empty() && elements != 0U && width % elements == 0U) {
            design_.signal_packed_shape_.emplace(id, std::move(shape));
        }
    }
    const auto packed_type = packed_type_resolver(
        working_specialization, *declaration.type);
    if (packed_type) {
        info.packed_members = packed_type->packed_members;
    }
    info.is_port = declaration.form == Form::port;
    info.direction = direction;
    info.declaration_span = compiled_source_span(
        *compiled_, declaration.source);

    const auto four_state
        = domain == frontend::ValueDomain::Logic4;
    // An input port net floats until it is connected (23.3.3).
    auto initial = declaration.form == Form::net
            || (declaration.form == Form::port
                && direction == frontend::PortDirection::Input
                && !net_type.empty())
        ? net_initial_for(domain, net_type)
        : four_state
        ? Logic4::x
        : Logic4::zero;
    if (event_variable) {
        initial = Logic4::zero;
    }
    if (declaration.form == Form::port
        && direction == frontend::PortDirection::Input) {
        if (unconnected_drive
            == semantic::sv::UnconnectedDrive::pull_zero) {
            initial = Logic4::zero;
        } else if (unconnected_drive
            == semantic::sv::UnconnectedDrive::pull_one) {
            initial = Logic4::one;
        }
    }
    auto initial_value = scalar_kind
                != frontend::SystemVerilogScalarKind::None
            && scalar_kind
                != frontend::SystemVerilogScalarKind::Time
        ? runtime::encode_systemverilog_scalar_payload(
              runtime::SystemVerilogScalarValue {
                  scalar_kind, 0U })
              .value
        : PackedLogic4(width, initial);
    const bool forced_unconnected_value
        = declaration.form == Form::port
        && direction == frontend::PortDirection::Input
        && unconnected_drive
            != semantic::sv::UnconnectedDrive::none;
    if (packed_type && declaration.form != Form::net
        && !event_variable && !forced_unconnected_value) {
        initial_value = packed_default_resolver(
            working_specialization,
            *declaration.type,
            width)
                            .value_or(
                                packed_fallback_resolver(
                                    *packed_type, width));
    }
    if (declaration.initializer && !runtime_package_initializer) {
        const auto value
            = working_specialization.evaluate_integral_expression(
                *declaration.initializer);
        if (value && width <= 64U
            && !declaration.type->virtual_interface) {
            initial_value = PackedLogic4::from_aval_bval(
                width, static_cast<std::uint64_t>(*value), 0U);
        } else if (declaration.type->virtual_interface) {
            const auto expression
                = working_specialization.find_expression(
                    *declaration.initializer);
            std::string actual_name;
            if (expression
                && expression->systemverilog != nullptr) {
                const auto& record = *expression->systemverilog;
                if (record.kind
                    == semantic::sv::ExpressionKind::name) {
                    actual_name = record.text;
                    if (record.referenced_name
                        && record.referenced_name->selected) {
                        const auto selected
                            = working_specialization
                                  .find_declaration(
                                      *record.referenced_name
                                          ->selected);
                        if (selected
                            && selected->systemverilog != nullptr) {
                            actual_name
                                = selected->systemverilog->name;
                        }
                    }
                } else if (record.kind
                        == semantic::sv::ExpressionKind::index
                    && record.operands.size() == 2U) {
                    const auto base
                        = working_specialization.find_expression(
                            record.operands.front());
                    const auto index = working_specialization
                                           .evaluate_integral_expression(
                                               record.operands.back());
                    if (base && base->systemverilog != nullptr
                        && index) {
                        actual_name = base->systemverilog->text
                            + "[" + std::to_string(*index) + "]";
                    }
                } else if (record.kind
                    == semantic::sv::ExpressionKind::class_null) {
                    initial_value = PackedLogic4::from_aval_bval(
                        width, 0U, 0U);
                }
            }
            if (!actual_name.empty()) {
                std::optional<semantic::sv::TypeReference>
                    source_type;
                if (expression
                    && expression->systemverilog != nullptr
                    && expression->systemverilog->referenced_name
                    && expression->systemverilog
                        ->referenced_name->selected) {
                    const auto selected
                        = working_specialization.find_declaration(
                            *expression->systemverilog
                                ->referenced_name->selected);
                    if (selected
                        && selected->systemverilog != nullptr
                        && selected->systemverilog->type
                        && selected->systemverilog->type
                            ->virtual_interface) {
                        source_type
                            = *selected->systemverilog->type;
                    }
                }
                pending_virtual_interface_initializers.push_back({
                    id,
                    materialized_path,
                    std::move(actual_name),
                    *declaration.type,
                    std::move(source_type),
                    declaration.source,
                    &working_specialization,
                });
                initial_value = PackedLogic4::from_aval_bval(
                    width, 0U, 0U);
            } else if (!expression
                || expression->systemverilog == nullptr
                || expression->systemverilog->kind
                    != semantic::sv::ExpressionKind::class_null) {
                report(
                    "FSIM-ELAB-SVIFACE-002",
                    "virtual-interface initializer for '"
                        + full_name
                        + "' must name an interface instance or "
                          "virtual-interface value, or null",
                    compiled_source_span(
                        *compiled_, declaration.source));
            }
        } else {
            const auto expression
                = working_specialization.find_expression(
                    *declaration.initializer);
            const bool class_handle
                = declaration.type->value_form
                        == semantic::sv::TypeForm::class_handle
                || effective_type.value_form
                        == semantic::sv::TypeForm::class_handle
                || !declaration.type->class_identity.empty()
                || !effective_type.class_identity.empty();
            const bool package_initializer
                = unit.kind == semantic::sv::UnitKind::package;
            // A real-family value initializes an integral variable with its
            // rounded integer (`time t = 5ns;`, IEEE 1800-2017 6.12.2).
            const bool integral_declaration
                = scalar_kind == frontend::SystemVerilogScalarKind::None
                || scalar_kind == frontend::SystemVerilogScalarKind::Time;
            const auto integral_initializer
                = [&](std::optional<HirSystemVerilogConstant> value) {
                      if (value && value->scalar
                          && (!integral_declaration
                              || value->scalar->kind
                                  == frontend::SystemVerilogScalarKind::Chandle)) {
                          value.reset();
                      }
                      return value;
                  };
            if (class_handle && package_initializer && expression
                && expression->systemverilog != nullptr
                && (expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::class_allocation
                    || expression->systemverilog->kind
                        == semantic::sv::ExpressionKind::class_null)) {
                // Package class handles are initialized before any process
                // executes. The application setup phase
                // evaluates `new` after the class heap has been configured;
                // the signal's elaborated value is the null handle meanwhile.
                initial_value = PackedLogic4::from_aval_bval(
                    width, 0U, 0U);
            } else if (std::string constant_error; const auto constant
                = !class_handle && width <= 64U
                    ? integral_initializer(evaluate_hir_systemverilog_value(
                          working_specialization, *declaration.initializer,
                          constant_error))
                    : std::nullopt) {
                // A four-state constant initializer (`= 1'bz`) keeps its
                // unknown bits, sized to the declaration (IEEE 1800-2017
                // 10.5, 6.8).
                auto sized = PackedLogic4(width, Logic4::zero);
                const auto extension = constant->signed_value
                        && constant->packed.width() != 0U
                    ? constant->packed.get(constant->packed.width() - 1U)
                    : Logic4::zero;
                for (std::size_t bit { }; bit < width; ++bit) {
                    sized.set(bit, bit < constant->packed.width()
                            ? constant->packed.get(bit)
                            : extension == Logic4::one && constant->known()
                            ? Logic4::one
                            : extension == Logic4::x
                                || extension == Logic4::z
                            ? extension
                            : Logic4::zero);
                }
                initial_value = std::move(sized);
            } else {
                report(
                    "FSIM-ELAB-HIR-001",
                    "compiled initializer for '" + declaration.name
                        + "' is not a bounded integral value",
                    compiled_source_span(
                        *compiled_, declaration.source));
                return false;
            }
        }
    }
    runtime::simir::Signal signal {
        full_name,
        std::move(initial_value),
        ResolutionKind::none,
        value_kind(domain),
        std::nullopt,
        { StrengthRank::pull, StrengthRank::pull },
        std::nullopt,
        std::nullopt,
        scalar_kind,
    };
    signal.event_variable = event_variable;
    configure_net_signal(signal, declaration, net_type);
    design_.signal_info_.push_back(std::move(info));
    design_.signals_.push_back(std::move(signal));
    declared_signal_names.insert(declaration.name);
    if (materialized_path == path) {
        signals.emplace(declaration.name, id);
    } else {
        signals.insert_or_assign(declaration.name, id);
    }
    signals.emplace(full_name, id);
    design_.signal_by_name_.emplace(full_name, id);
    if (design_.roots_.size() == 1U) {
        design_.signal_by_name_.emplace(declaration.name, id);
        const auto root_prefix = active_root_ + ".";
        if (materialized_path.starts_with(root_prefix)) {
            design_.signal_by_name_.emplace(
                materialized_path.substr(root_prefix.size())
                    + "." + declaration.name,
                id);
        }
    }
    register_resolution(id);
    if (variable_input_port()) {
        read_only_signals.insert(id);
    }
    return true;
}

} // namespace fsim::elaboration
