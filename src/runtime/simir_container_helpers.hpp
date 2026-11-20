// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "simir_internal.hpp"
#include "simir_container_ops.hpp"
#include "fsim/runtime/simir_interpreter.hpp"

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
    // An element value is indexed by nothing: the owning array's index
    // profile (an associative key type, IEEE 1800-2023 7.8) is not part of
    // the element's type, so `m[k] <= s` matches a plain structure `s`
    // whatever the key type of `m`.
    const ContainerType unindexed { };
    result.index_width = unindexed.index_width;
    result.two_state_indices = unindexed.two_state_indices;
    result.signed_indices = unindexed.signed_indices;
    result.string_indices = unindexed.string_indices;
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
    if (source.type.sparse) {
        // A sparse source holds its elements by ordinal.
        ContainerElementCursor cursor { source };
        for (std::size_t index = 0; index < count; ++index) {
            if (target.type.element_kind == ContainerElementKind::String) {
                target.string_elements[index]
                    = container_string_element(source, index);
            } else {
                target.elements[index] = cursor.at(index);
            }
        }
        return;
    }
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

// Dense storage offsets; a sparse array is addressed by ordinal through
// read_sparse_container_element and write_sparse_container_element.
[[nodiscard]] inline std::size_t fixed_offset(
    const ContainerType& type,
    const std::int32_t index)
{
    if (type.sparse) {
        throw std::logic_error {
            "this operation does not support a sparse static array"
        };
    }
    return static_cast<std::size_t>(
        type.index_left >= type.index_right
            ? static_cast<std::int64_t>(type.index_left) - index
            : static_cast<std::int64_t>(index) - type.index_left);
}

// The storage ordinal of index `index` of a one-dimensional fixed array,
// dense or sparse (a slice alias selects elements of its first dimension).
[[nodiscard]] inline std::uint64_t slice_element_ordinal(
    const ContainerType& type,
    const std::int32_t index)
{
    return static_cast<std::uint64_t>(
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

// Whether a write through a fixed-size array index proceeds: an unknown
// or out-of-range index writes nothing (IEEE 1800-2017 7.4.6). An index
// that is not a 32-bit value (such as the lowering's invalid-index
// sentinel) proceeds, so fixed_offset reports it.
[[nodiscard]] inline bool fixed_index_in_range(
    const ContainerType& type,
    const PackedLogic4& value)
{
    if (std::ranges::any_of(value.bval_words(),
            [](const auto word) { return word != 0U; })) {
        return false;
    }
    const auto converted = value.known_signed_value();
    if (!converted
        || *converted < std::numeric_limits<std::int32_t>::min()
        || *converted > std::numeric_limits<std::int32_t>::max()) {
        return true;
    }
    const auto low = std::min(type.index_left, type.index_right);
    const auto high = std::max(type.index_left, type.index_right);
    return *converted >= low && *converted <= high;
}

[[nodiscard]] inline std::size_t fixed_offset(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const PackedLogic4& value)
{
    if (type.sparse) {
        container_error(process, instruction,
            "this operation does not support a sparse static array");
    }
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

// The ordinal of a known, in-range index of a sparse static array.
[[nodiscard]] inline std::uint64_t sparse_offset(
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
    return static_cast<std::uint64_t>(
        type.index_left >= type.index_right
            ? static_cast<std::int64_t>(type.index_left) - index
            : static_cast<std::int64_t>(index) - type.index_left);
}

// The ordinal a read of a sparse static array addresses, or none for an
// unknown, negative or out-of-range index (the read yields the element
// type's invalid-index value, IEEE 1800-2023 7.4.6).
[[nodiscard]] inline std::optional<std::uint64_t> sparse_read_ordinal(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const PackedLogic4& value,
    const bool linear_index)
{
    const auto index = value.known_signed_value();
    if (!index) {
        return std::nullopt;
    }
    std::optional<std::uint64_t> ordinal;
    if (linear_index) {
        if (*index >= 0) {
            ordinal = static_cast<std::uint64_t>(*index);
        }
    } else if (*index >= std::min(type.index_left, type.index_right)
        && *index <= std::max(type.index_left, type.index_right)) {
        ordinal = sparse_offset(process, instruction, type, value);
    }
    if (ordinal && *ordinal >= sparse_container_element_count(type)) {
        return std::nullopt;
    }
    return ordinal;
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

// True when an integral associative-array index holds x or z bits, so a
// read or write with it is invalid (7.8.6); an index of another width is
// still the error associative_key reports.
[[nodiscard]] inline bool associative_index_invalid(
    const ContainerType& type, const PackedLogic4& value)
{
    return !type.string_indices && value.width() == type.index_width
        && (value.is_logic9()
            || std::ranges::any_of(
                value.bval_words(), [](const auto word) { return word != 0; }));
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

[[nodiscard]] inline std::string_view associative_string_key(
    const ProcessId process,
    const InstructionIndex instruction,
    const ContainerType& type,
    const std::string_view value)
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

// The reference interpreter's register file for the shared container
// operations: a process's frame, reached the way its handlers always did.
class Interpreter::Impl::ProcessContainerAccess final
    : public ContainerRegisterAccess {
public:
    ProcessContainerAccess(Impl& impl, ProcessState& process)
        : impl_(impl)
        , process_(process)
    {
    }

    [[nodiscard]] const PackedLogic4& read(const RegisterId id) override
    {
        return impl_.get_register(process_, id);
    }

    void write(const RegisterId id, PackedLogic4 value) override
    {
        impl_.get_register(process_, id) = std::move(value);
    }

    [[nodiscard]] std::string_view read_string(
        const StringRegisterId id) override
    {
        return impl_.get_string_register(process_, id);
    }

    [[nodiscard]] std::string& string(const StringRegisterId id) override
    {
        return impl_.get_string_register(process_, id);
    }

    [[nodiscard]] ContainerValue& container(
        const ContainerRegisterId id) override
    {
        return impl_.get_container_register(process_, id);
    }

    [[nodiscard]] const ContainerValue& read_container(
        const ContainerRegisterId id) override
    {
        return impl_.read_container_register(process_, id);
    }

    [[nodiscard]] std::uint32_t next_random() override
    {
        return Impl::next_random(process_);
    }
    void warn(const std::string_view message) override
    {
        impl_.warn_process(process_.id, message);
    }

private:
    Impl& impl_;
    ProcessState& process_;
};

} // namespace fsim::runtime::simir
