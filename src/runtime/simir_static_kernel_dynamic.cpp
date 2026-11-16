// SPDX-License-Identifier: Apache-2.0

#include "simir_static_kernel_dynamic.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <optional>

namespace fsim::runtime::simir::static_kernel_detail {

namespace {

using kernel_word::Word;

constexpr std::uint32_t two_state_flag = 1U;
constexpr std::uint32_t queue_flag = 2U;

// The index register's value as the reference reads it: signed (sign
// extended from `width` bits) or unsigned; empty when it has unknown bits.
[[nodiscard]] std::optional<std::int64_t> signed_index(const Word value,
    const std::uint32_t width) noexcept
{
    if ((value.b & kernel_word::mask(width)) != 0U) {
        return std::nullopt;
    }
    auto bits = value.a & kernel_word::mask(width);
    if (width < 64U && width != 0U && ((bits >> (width - 1U)) & 1U) != 0U) {
        bits |= ~kernel_word::mask(width);
    }
    return static_cast<std::int64_t>(bits);
}

[[nodiscard]] std::optional<std::uint64_t> unsigned_index(const Word value,
    const std::uint32_t width) noexcept
{
    if ((value.b & kernel_word::mask(width)) != 0U) {
        return std::nullopt;
    }
    return value.a & kernel_word::mask(width);
}

// The index of a write or insertion: the reference's requested position
// (signed, or unsigned when it fits a signed 64-bit value).
[[nodiscard]] std::optional<std::int64_t> requested_index(const Word value,
    const std::uint32_t width, const bool is_signed) noexcept
{
    if (is_signed) {
        return signed_index(value, width);
    }
    const auto index = unsigned_index(value, width);
    if (!index || *index > static_cast<std::uint64_t>(
                      std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(*index);
}

void ensure_capacity(StaticKernelDynamicContainer& container, const std::uint64_t needed)
{
    if (needed > container.capacity) {
        grow_dynamic(container, needed);
    }
}

// Inserts `value` at `at` (<= size), shifting the later elements up.
void insert_at(StaticKernelDynamicContainer& container, const std::uint64_t at,
    const Word value)
{
    ensure_capacity(container, container.size + 1U);
    if (at == 0U) {
        container.head = (container.head - 1U) & (container.capacity - 1U);
    } else {
        for (auto ordinal = container.size; ordinal > at; --ordinal) {
            set_dynamic_element(container, ordinal,
                dynamic_element(container, ordinal - 1U));
        }
    }
    ++container.size;
    set_dynamic_element(container, at, value);
}

// Removes element `at` (< size), shifting the later elements down.
void erase_at(StaticKernelDynamicContainer& container, const std::uint64_t at) noexcept
{
    if (at == 0U) {
        container.head = (container.head + 1U) & (container.capacity - 1U);
    } else {
        for (auto ordinal = at; ordinal + 1U < container.size; ++ordinal) {
            set_dynamic_element(container, ordinal,
                dynamic_element(container, ordinal + 1U));
        }
    }
    --container.size;
    if (container.size == 0U) {
        container.head = 0U;
    }
}

} // namespace

bool native_dynamic_type(const ContainerType& type) noexcept
{
    return !type.fixed && !type.associative && !type.sparse && !type.vhdl_shaped
        && !type.aggregate_value && !type.union_aggregate
        && (type.element_kind == ContainerElementKind::Packed
            || type.element_kind == ContainerElementKind::Scalar)
        && type.element_width != 0U && type.element_width <= 64U;
}

StaticKernelDynamicContainer make_dynamic_header(const ContainerType& type)
{
    StaticKernelDynamicContainer header;
    header.maximum = type.maximum_elements ? *type.maximum_elements
                                           : std::numeric_limits<std::uint64_t>::max();
    header.limit = maximum_container_elements(type);
    const auto width = type.element_width;
    // initial_packed_element: a scalar element (a real) starts at zero.
    const auto fill = type.element_kind == ContainerElementKind::Scalar || type.two_state
        ? Word { }
        : kernel_word::all_x(width);
    header.fill_a = fill.a;
    header.fill_b = fill.b;
    const auto empty = type.two_state ? Word { } : kernel_word::all_x(width);
    header.empty_a = empty.a;
    header.empty_b = empty.b;
    header.element_width = width;
    header.flags = (type.two_state ? two_state_flag : 0U)
        | (type.queue ? queue_flag : 0U);
    return header;
}

void grow_dynamic(StaticKernelDynamicContainer& container, const std::uint64_t needed)
{
    const auto capacity = std::bit_ceil(std::max<std::uint64_t>(needed, 4U));
    if (capacity > std::numeric_limits<std::size_t>::max() / (2U * sizeof(std::uint64_t))) {
        throw std::bad_alloc { };
    }
    auto* data = static_cast<std::uint64_t*>(
        std::malloc(static_cast<std::size_t>(capacity) * 2U * sizeof(std::uint64_t)));
    if (data == nullptr) {
        throw std::bad_alloc { };
    }
    for (std::uint64_t ordinal = 0U; ordinal < container.size; ++ordinal) {
        const auto value = dynamic_element(container, ordinal);
        data[2U * ordinal] = value.a;
        data[2U * ordinal + 1U] = value.b;
    }
    std::free(container.data);
    container.data = data;
    container.capacity = capacity;
    container.head = 0U;
}

ContainerValue dynamic_value(const StaticKernelDynamicContainer& container,
    const ContainerType& type)
{
    ContainerValue value;
    value.type = type;
    value.elements.reserve(static_cast<std::size_t>(container.size));
    for (std::uint64_t ordinal = 0U; ordinal < container.size; ++ordinal) {
        const auto element = dynamic_element(container, ordinal);
        value.elements.push_back(PackedLogic4::from_aval_bval(
            container.element_width, element.a, element.b));
    }
    return value;
}

void assign_dynamic_value(StaticKernelDynamicContainer& container,
    const ContainerValue& value)
{
    const auto size = static_cast<std::uint64_t>(value.elements.size());
    container.size = 0U;
    container.head = 0U;
    ensure_capacity(container, size);
    for (std::uint64_t ordinal = 0U; ordinal < size; ++ordinal) {
        const auto& element = value.elements[static_cast<std::size_t>(ordinal)];
        const auto a = element.aval_words();
        const auto b = element.bval_words();
        container.data[2U * ordinal] = a.empty() ? 0U : a.front();
        container.data[2U * ordinal + 1U] = b.empty() ? 0U : b.front();
    }
    container.size = size;
}

DynamicHeaders::~DynamicHeaders()
{
    truncate(0U);
}

std::uint32_t DynamicHeaders::add(const StaticKernelDynamicContainer header)
{
    headers_.push_back(header);
    return static_cast<std::uint32_t>(headers_.size() - 1U);
}

void DynamicHeaders::truncate(const std::size_t count) noexcept
{
    while (headers_.size() > count) {
        std::free(headers_.back().data);
        headers_.pop_back();
    }
}

const char* dyn_fail_message(const DynFailure failure) noexcept
{
    switch (failure) {
    case DynFailure::none:
        break;
    case DynFailure::index_unknown:
        return "container index must be a known integral value";
    case DynFailure::index_negative:
        return "container index cannot be negative";
    case DynFailure::index_range:
        return "container index is out of range";
    case DynFailure::size_unknown:
        return "dynamic-array size must be a known integral value";
    case DynFailure::array_budget:
        return "dynamic array exceeds its owning-storage budget";
    case DynFailure::queue_budget:
        return "queue exceeds the per-container owning-storage budget";
    case DynFailure::write_mismatch:
        return "container element write type mismatch";
    case DynFailure::queue_write_mismatch:
        return "queue element write type mismatch";
    case DynFailure::out_of_memory:
        return "static kernel queue or dynamic array could not grow";
    }
    return "static kernel container operation failed";
}

DynFailure execute_dyn(const KInst& inst, StaticKernelDynamicContainer& container,
    const StaticKernelDynamicContainer* other, Word* registers)
{
    const auto op = static_cast<DynOp>(inst.sub);
    const bool is_signed = (inst.flags & flag_signed) != 0U;
    const bool two_state = (container.flags & two_state_flag) != 0U;
    const auto element_mask = kernel_word::mask(container.element_width);
    try {
        switch (op) {
        case DynOp::read: {
            std::uint64_t at { };
            if (is_signed) {
                const auto index = signed_index(registers[inst.y], inst.width);
                if (!index) {
                    return DynFailure::index_unknown;
                }
                if (*index < 0) {
                    return DynFailure::index_negative;
                }
                at = static_cast<std::uint64_t>(*index);
            } else {
                const auto index = unsigned_index(registers[inst.y], inst.width);
                if (!index) {
                    return DynFailure::index_unknown;
                }
                at = *index;
            }
            if (at >= container.size) {
                return DynFailure::index_range;
            }
            registers[inst.d] = dynamic_element(container, at);
            return DynFailure::none;
        }
        case DynOp::write: {
            const auto value = registers[inst.x];
            if (two_state && (value.b & element_mask) != 0U) {
                return DynFailure::write_mismatch;
            }
            const auto requested = requested_index(registers[inst.y], inst.width,
                is_signed);
            const auto size = container.size;
            const bool queue = (container.flags & queue_flag) != 0U;
            if (!requested || *requested < 0
                || static_cast<std::uint64_t>(*requested) > size
                || (static_cast<std::uint64_t>(*requested) == size
                    && (!queue || size >= container.maximum))) {
                return DynFailure::none;
            }
            const auto at = static_cast<std::uint64_t>(*requested);
            if (at == size) {
                if (size >= container.limit) {
                    return DynFailure::queue_budget;
                }
                ensure_capacity(container, size + 1U);
                ++container.size;
            }
            set_dynamic_element(container, at, value);
            return DynFailure::none;
        }
        case DynOp::size:
            registers[inst.d] = { container.size & 0xFFFF'FFFFU, 0U };
            return DynFailure::none;
        case DynOp::resize: {
            const auto size = unsigned_index(registers[inst.x], inst.width);
            if (!size) {
                return DynFailure::size_unknown;
            }
            if (*size > container.limit) {
                return DynFailure::array_budget;
            }
            const auto kept = other != nullptr ? std::min(*size, other->size) : 0U;
            StaticKernelDynamicContainer replacement = container;
            replacement.data = nullptr;
            replacement.size = 0U;
            replacement.capacity = 0U;
            replacement.head = 0U;
            if (*size != 0U) {
                grow_dynamic(replacement, *size);
            }
            for (std::uint64_t ordinal = 0U; ordinal < *size; ++ordinal) {
                const auto value = ordinal < kept
                    ? dynamic_element(*other, ordinal)
                    : Word { container.fill_a, container.fill_b };
                replacement.data[2U * ordinal] = value.a;
                replacement.data[2U * ordinal + 1U] = value.b;
            }
            replacement.size = *size;
            std::free(container.data);
            container = replacement;
            return DynFailure::none;
        }
        case DynOp::delete_all:
            container.size = 0U;
            container.head = 0U;
            return DynFailure::none;
        case DynOp::delete_at: {
            const auto requested = signed_index(registers[inst.y], inst.width);
            if (!requested || *requested < 0
                || static_cast<std::uint64_t>(*requested) >= container.size) {
                return DynFailure::none;
            }
            erase_at(container, static_cast<std::uint64_t>(*requested));
            return DynFailure::none;
        }
        case DynOp::push_back:
        case DynOp::push_front:
        case DynOp::insert: {
            const auto value = registers[inst.x];
            if (two_state && (value.b & element_mask) != 0U) {
                return DynFailure::queue_write_mismatch;
            }
            std::uint64_t at = op == DynOp::push_front ? 0U : container.size;
            if (op == DynOp::insert) {
                const auto requested = signed_index(registers[inst.y], inst.width);
                // An unknown, negative or out-of-range index inserts nothing
                // (IEEE 1800-2023 7.10.2.2).
                if (!requested || *requested < 0
                    || static_cast<std::uint64_t>(*requested) > container.size) {
                    return DynFailure::none;
                }
                at = static_cast<std::uint64_t>(*requested);
            }
            if (container.size + 1U > container.limit
                && container.maximum > container.limit) {
                return DynFailure::queue_budget;
            }
            insert_at(container, at, value);
            if (container.size > container.maximum) {
                // A bounded queue keeps its first `maximum` elements.
                --container.size;
            }
            return DynFailure::none;
        }
        case DynOp::pop_front:
        case DynOp::pop_back: {
            // Popping an empty queue returns the element type's default
            // value (IEEE 1800-2023 7.10.2.4, 7.10.2.5).
            if (container.size == 0U) {
                registers[inst.d] = { container.empty_a, container.empty_b };
                return DynFailure::none;
            }
            const auto at = op == DynOp::pop_front ? 0U : container.size - 1U;
            registers[inst.d] = dynamic_element(container, at);
            erase_at(container, at);
            return DynFailure::none;
        }
        case DynOp::copy: {
            if (other == nullptr || other == &container) {
                return DynFailure::none;
            }
            container.size = 0U;
            container.head = 0U;
            ensure_capacity(container, other->size);
            for (std::uint64_t ordinal = 0U; ordinal < other->size; ++ordinal) {
                const auto value = dynamic_element(*other, ordinal);
                container.data[2U * ordinal] = value.a;
                container.data[2U * ordinal + 1U] = value.b;
            }
            container.size = other->size;
            return DynFailure::none;
        }
        }
    } catch (const std::bad_alloc&) {
        return DynFailure::out_of_memory;
    }
    return DynFailure::none;
}

} // namespace fsim::runtime::simir::static_kernel_detail
