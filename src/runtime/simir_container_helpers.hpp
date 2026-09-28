// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "simir_internal.hpp"

#include <algorithm>

namespace fsim::runtime::simir {

[[nodiscard]] inline bool aggregate_box(
    const ContainerType& type) noexcept
{
    return type.element_kind == ContainerElementKind::Aggregate
        && type.aggregate_value;
}

[[nodiscard]] inline ContainerType aggregate_element_type(
    const ContainerType& type)
{
    auto result = type;
    result.queue = false;
    result.associative = false;
    result.fixed = false;
    result.aggregate_value = true;
    result.maximum_elements.reset();
    result.dimensions.clear();
    result.index_left = 0;
    result.index_right = 0;
    return result;
}

[[nodiscard]] inline PackedLogic4 initial_packed_element(
    const ContainerType& type)
{
    if (type.element_kind == ContainerElementKind::Scalar) {
        return PackedLogic4(type.element_width, Logic4::zero);
    }
    return type.two_state
        ? PackedLogic4(type.element_width, Logic4::zero)
        : PackedLogic4 { type.element_width, Logic4::x };
}

inline void append_default_element(ContainerValue& value)
{
    switch (value.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        value.elements.push_back(initial_packed_element(value.type));
        return;
    case ContainerElementKind::String:
        value.string_elements.emplace_back();
        return;
    case ContainerElementKind::Container:
        value.nested_elements.push_back(
            default_container_value(value.type.element_types.front()));
        return;
    case ContainerElementKind::Aggregate:
        value.nested_elements.push_back(
            default_container_value(aggregate_element_type(value.type)));
        return;
    }
}

inline void copy_element_prefix(
    ContainerValue& target,
    const ContainerValue& source,
    const std::size_t count)
{
    switch (target.type.element_kind) {
    case ContainerElementKind::Packed:
    case ContainerElementKind::Scalar:
        std::ranges::copy_n(
            source.elements.begin(), static_cast<std::ptrdiff_t>(count),
            target.elements.begin());
        return;
    case ContainerElementKind::String:
        std::ranges::copy_n(
            source.string_elements.begin(),
            static_cast<std::ptrdiff_t>(count),
            target.string_elements.begin());
        return;
    case ContainerElementKind::Container:
    case ContainerElementKind::Aggregate:
        std::ranges::copy_n(
            source.nested_elements.begin(),
            static_cast<std::ptrdiff_t>(count),
            target.nested_elements.begin());
        return;
    }
}

template <typename Container>
[[nodiscard]] inline auto iterator_at(
    Container& container,
    const std::size_t offset)
{
    using Difference = typename Container::difference_type;
    return container.begin() + static_cast<Difference>(offset);
}

[[nodiscard]] inline std::size_t fixed_offset(
    const ContainerType& type,
    const std::int32_t index)
{
    return static_cast<std::size_t>(
        type.index_left >= type.index_right
            ? static_cast<std::int64_t>(type.index_left) - index
            : static_cast<std::int64_t>(index) - type.index_left);
}

[[noreturn]] inline void container_error(
    const ProcessId process,
    const InstructionIndex instruction,
    const std::string_view message)
{
    throw InterpreterError {
        process, instruction, std::string { message }
    };
}

[[nodiscard]] inline std::size_t known_index(
    const ProcessId process,
    const InstructionIndex instruction,
    const PackedLogic4& value,
    const bool signed_index,
    const std::string_view role)
{
    if (signed_index) {
        const auto converted = value.known_signed_value();
        if (!converted) {
            container_error(
                process, instruction,
                std::string { role } + " must be a known integral value");
        }
        if (*converted < 0) {
            container_error(
                process, instruction,
                std::string { role } + " cannot be negative");
        }
        if (static_cast<std::uint64_t>(*converted)
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            container_error(
                process, instruction,
                std::string { role } + " is too large");
        }
        return static_cast<std::size_t>(*converted);
    }

    const auto converted = value.known_unsigned_value();
    if (!converted) {
        container_error(
            process, instruction,
            std::string { role }
                + " must be a known integral value");
    }
    if (*converted
        > static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        container_error(
            process, instruction,
            std::string { role } + " is too large");
    }
    return static_cast<std::size_t>(*converted);
}

[[nodiscard]] inline std::int32_t known_fixed_index(
    const ProcessId process,
    const InstructionIndex instruction,
    const PackedLogic4& value)
{
    const auto converted = value.known_signed_value();
    if (!converted
        || *converted < std::numeric_limits<std::int32_t>::min()
        || *converted > std::numeric_limits<std::int32_t>::max()) {
        container_error(
            process, instruction,
            "static-array index must be a known 32-bit integral value");
    }
    return static_cast<std::int32_t>(*converted);
}

[[nodiscard]] inline std::size_t fixed_offset(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const PackedLogic4& value)
{
    const auto index = known_fixed_index(process, instruction, value);
    const auto low = std::min(type.index_left, type.index_right);
    const auto high = std::max(type.index_left, type.index_right);
    if (index < low || index > high) {
        container_error(
            process, instruction,
            "static-array index is out of range");
    }
    return static_cast<std::size_t>(
        type.index_left >= type.index_right
            ? static_cast<std::int64_t>(type.index_left) - index
            : static_cast<std::int64_t>(index) - type.index_left);
}

inline void require_same_type(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& target,
    const ContainerType& source)
{
    if (target != source) {
        container_error(
            process, instruction, "container value type mismatch");
    }
}

[[maybe_unused]] inline void require_queue(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value)
{
    if (!value.type.queue) {
        container_error(
            process, instruction,
            value.type.associative
                ? "queue method used on an associative array"
                : "queue method used on a dynamic array");
    }
}

[[maybe_unused]] inline void require_associative(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerValue& value,
    const std::string_view operation)
{
    if (!value.type.associative) {
        container_error(
            process, instruction,
            std::string { operation }
                + " requires an associative array");
    }
}

[[nodiscard]] inline PackedLogic4 associative_key(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const PackedLogic4& value)
{
    if (type.string_indices) {
        container_error(
            process, instruction,
            "string-indexed associative array requires a string index");
    }
    const auto unknown = value.is_logic9()
        || std::ranges::any_of(
            value.bval_words(), [](const auto word) { return word != 0; });
    if (value.width() != type.index_width || unknown) {
        container_error(
            process, instruction,
            unknown
                ? "associative-array index must be a known integral value"
                : "associative-array index type mismatch");
    }
    return value;
}

[[nodiscard]] inline const std::string& associative_string_key(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const std::string& value)
{
    if (!type.associative || !type.string_indices) {
        container_error(
            process, instruction,
            "integral-indexed associative array requires an integral index");
    }
    if (value.size() > maximum_string_bytes) {
        container_error(
            process, instruction,
            "associative-array string index exceeds the byte limit");
    }
    return value;
}

[[nodiscard]] inline bool associative_index_key_less_impl(
    const ContainerType& type,
    const PackedLogic4& left,
    const PackedLogic4& right)
{
    if (type.signed_indices) {
        const auto lhs_negative = left.get(type.index_width - 1U) == Logic4::one;
        const auto rhs_negative = right.get(type.index_width - 1U) == Logic4::one;
        if (lhs_negative != rhs_negative) {
            return lhs_negative;
        }
    }
    const auto lhs = left.aval_words();
    const auto rhs = right.aval_words();
    for (auto index = lhs.size(); index != 0; --index) {
        if (lhs[index - 1U] != rhs[index - 1U]) {
            return lhs[index - 1U] < rhs[index - 1U];
        }
    }
    return false;
}

[[nodiscard]] inline std::size_t lower_key(
    const ContainerValue& value,
    const PackedLogic4& key)
{
    return static_cast<std::size_t>(
        std::lower_bound(
            value.keys.begin(), value.keys.end(), key,
            [&](const PackedLogic4& candidate,
                const PackedLogic4& sought) {
                return associative_index_key_less_impl(
                    value.type, candidate, sought);
            })
        - value.keys.begin());
}

[[nodiscard]] inline bool key_equal(
    const PackedLogic4& left,
    const PackedLogic4& right)
{
    return left == right;
}

[[nodiscard]] inline std::size_t lower_string_key(
    const ContainerValue& value,
    const std::string_view key)
{
    return static_cast<std::size_t>(
        std::lower_bound(
            value.string_keys.begin(), value.string_keys.end(), key)
        - value.string_keys.begin());
}

} // namespace fsim::runtime::simir
