// SPDX-License-Identifier: Apache-2.0

#include "simir_static_kernel_dynamic.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace fsim::runtime::simir::static_kernel_detail {

namespace {

using kernel_word::Word;

constexpr std::uint32_t two_state_flag = 1U;
constexpr std::uint32_t queue_flag = 2U;

// An associative array's order key (StaticKernelDynamicContainer::keys)
// of the index value `bits` (`width` bits): unsigned order is the value's,
// signed order the sign-extended value's with its sign bit flipped.
[[nodiscard]] std::uint64_t order_key(const std::uint64_t bits,
    const std::uint32_t width, const bool is_signed) noexcept
{
    if (!is_signed) {
        return bits;
    }
    auto value = bits;
    if (width < 64U && ((bits >> (width - 1U)) & 1U) != 0U) {
        value |= ~kernel_word::mask(width);
    }
    return value ^ (std::uint64_t { 1 } << 63U);
}

[[nodiscard]] std::uint64_t key_value(const std::uint64_t key,
    const std::uint32_t width, const bool is_signed) noexcept
{
    return (is_signed ? key ^ (std::uint64_t { 1 } << 63U) : key)
        & kernel_word::mask(width);
}

[[nodiscard]] bool signed_keys(const StaticKernelDynamicContainer& container) noexcept
{
    return (container.flags & dynamic_signed_keys_flag) != 0U;
}

// The position of the first key not below `key` (0 .. size).
[[nodiscard]] std::uint64_t lower_key(const StaticKernelDynamicContainer& container,
    const std::uint64_t key) noexcept
{
    std::uint64_t low = 0U;
    std::uint64_t high = container.size;
    while (low < high) {
        const auto middle = low + (high - low) / 2U;
        if (container.keys[middle] < key) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

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

[[nodiscard]] bool sparse_layout(const StaticKernelDynamicContainer& container) noexcept
{
    return (container.flags & dynamic_sparse_flag) != 0U;
}

[[nodiscard]] bool dense_layout(const StaticKernelDynamicContainer& container) noexcept
{
    return (container.flags & dynamic_dense_flag) != 0U;
}

// The words of one plane of a sparse array's dense image (compact_planes).
[[nodiscard]] std::uint64_t plane_words(const StaticKernelDynamicContainer& container) noexcept
{
    return (container.maximum * container.element_width + 63U) / 64U;
}

// `width` (<= 64) bits of a plane from bit `position`.
[[nodiscard]] std::uint64_t plane_bits(const std::uint64_t* plane,
    const std::uint64_t position, const std::uint32_t width) noexcept
{
    const auto word = position / 64U;
    const auto shift = position % 64U;
    auto bits = plane[word] >> shift;
    if (shift != 0U && shift + width > 64U) {
        bits |= plane[word + 1U] << (64U - shift);
    }
    return bits & kernel_word::mask(width);
}

void set_plane_bits(std::uint64_t* plane, const std::uint64_t position,
    const std::uint32_t width, const std::uint64_t value) noexcept
{
    const auto word = position / 64U;
    const auto shift = position % 64U;
    const auto bits = value & kernel_word::mask(width);
    const auto low_mask = kernel_word::mask(width) << shift;
    plane[word] = (plane[word] & ~low_mask) | (bits << shift);
    if (shift != 0U && shift + width > 64U) {
        const auto high_width = static_cast<std::uint32_t>(shift + width - 64U);
        const auto high_mask = kernel_word::mask(high_width);
        plane[word + 1U] = (plane[word + 1U] & ~high_mask) | (bits >> (64U - shift));
    }
}

[[nodiscard]] Word dense_element(const StaticKernelDynamicContainer& container,
    const std::uint64_t ordinal) noexcept
{
    const auto position = ordinal * container.element_width;
    return { plane_bits(container.data, position, container.element_width),
        plane_bits(container.data + container.capacity, position, container.element_width) };
}

void set_dense_element(StaticKernelDynamicContainer& container,
    const std::uint64_t ordinal, const Word value) noexcept
{
    const auto position = ordinal * container.element_width;
    set_plane_bits(container.data, position, container.element_width, value.a);
    set_plane_bits(container.data + container.capacity, position, container.element_width,
        value.b);
}

// Replaces a sparse array's stored elements by its dense image (the
// reference's compact_when_dense): the background everywhere, then the
// stored elements; the bits past the last element stay zero.
void make_dense(StaticKernelDynamicContainer& container)
{
    const auto words = plane_words(container);
    if (words > std::numeric_limits<std::size_t>::max() / (2U * sizeof(std::uint64_t))) {
        throw std::bad_alloc { };
    }
    auto* data = static_cast<std::uint64_t*>(
        std::calloc(static_cast<std::size_t>(2U * words), sizeof(std::uint64_t)));
    if (data == nullptr) {
        throw std::bad_alloc { };
    }
    StaticKernelDynamicContainer dense = container;
    dense.data = data;
    dense.capacity = words;
    const auto width = std::max<std::uint64_t>(container.element_width, 1U);
    const auto period_words = width / std::gcd(width, std::uint64_t { 64 });
    const auto period_elements = period_words * 64U / width;
    const Word background { container.fill_a, container.fill_b };
    for (std::uint64_t ordinal = 0U;
         ordinal < std::min(container.maximum, period_elements + 1U); ++ordinal) {
        set_dense_element(dense, ordinal, background);
    }
    for (std::uint64_t plane = 0U; plane < 2U; ++plane) {
        auto* base = data + plane * words;
        for (auto word = period_words; word < words; ++word) {
            base[word] = base[word - period_words];
        }
        const auto used = container.maximum * width % 64U;
        if (used != 0U && words != 0U) {
            base[words - 1U] &= (std::uint64_t { 1 } << used) - 1U;
        }
    }
    for (std::uint64_t index = 0U; index < container.size; ++index) {
        set_dense_element(dense, container.keys[index], dynamic_element(container, index));
    }
    std::free(container.data);
    std::free(container.keys);
    container.data = data;
    container.keys = nullptr;
    container.capacity = words;
    container.size = 0U;
    container.head = 0U;
    container.flags |= dynamic_dense_flag;
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

// The ordinal a sparse array's index addresses (KInst imm_a
// dyn_layout_linear: a linear ordinal; else the declared range in imm_b,
// left in the low half). A read of an unknown or out-of-range index has no
// ordinal (sparse_read_ordinal); a write reports the reference's error
// (sparse_offset, known_index).
struct SparseOrdinal {
    std::optional<std::uint64_t> ordinal;
    DynFailure failure { DynFailure::none };
};

[[nodiscard]] SparseOrdinal sparse_ordinal(const KInst& inst, const Word value,
    const std::uint64_t count, const bool writing) noexcept
{
    const auto index = signed_index(value, inst.width);
    if ((inst.imm_a & dyn_layout_linear) != 0U) {
        if (!index) {
            return { std::nullopt, writing ? DynFailure::linear_unknown : DynFailure::none };
        }
        if (*index < 0) {
            return { std::nullopt, writing ? DynFailure::linear_negative : DynFailure::none };
        }
        if (static_cast<std::uint64_t>(*index) >= count) {
            return { std::nullopt, writing ? DynFailure::linear_range : DynFailure::none };
        }
        return { static_cast<std::uint64_t>(*index), DynFailure::none };
    }
    const auto left = static_cast<std::int32_t>(static_cast<std::uint32_t>(inst.imm_b));
    const auto right = static_cast<std::int32_t>(static_cast<std::uint32_t>(inst.imm_b >> 32U));
    const auto low = std::min(left, right);
    const auto high = std::max(left, right);
    if (!index
        || (writing
            && (*index < std::numeric_limits<std::int32_t>::min()
                || *index > std::numeric_limits<std::int32_t>::max()))) {
        return { std::nullopt, writing ? DynFailure::fixed_unknown : DynFailure::none };
    }
    if (*index < low || *index > high) {
        return { std::nullopt, writing ? DynFailure::fixed_range : DynFailure::none };
    }
    const auto ordinal = static_cast<std::uint64_t>(left >= right
            ? static_cast<std::int64_t>(left) - *index
            : *index - static_cast<std::int64_t>(left));
    if (ordinal >= count) {
        // The reference's check after sparse_offset (ContainerWrite).
        return { std::nullopt, writing ? DynFailure::linear_range : DynFailure::none };
    }
    return { ordinal, DynFailure::none };
}

// Stores element `ordinal` of a sparse array as the reference does
// (write_sparse_container_element): the dense image once enough elements
// are stored; else only elements that differ from the background are kept.
[[nodiscard]] DynFailure write_sparse(StaticKernelDynamicContainer& container,
    const std::uint64_t ordinal, const Word value)
{
    if (!dense_layout(container) && container.size >= container.threshold) {
        make_dense(container);
    }
    if (dense_layout(container)) {
        set_dense_element(container, ordinal, value);
        return DynFailure::none;
    }
    const auto mask = kernel_word::mask(container.element_width);
    const bool background = (value.a & mask) == (container.fill_a & mask)
        && (value.b & mask) == (container.fill_b & mask);
    const auto at = lower_key(container, ordinal);
    const bool present = at < container.size && container.keys[at] == ordinal;
    if (background) {
        if (present) {
            for (auto index = at; index + 1U < container.size; ++index) {
                container.keys[index] = container.keys[index + 1U];
                set_dynamic_element(container, index, dynamic_element(container, index + 1U));
            }
            --container.size;
        }
        return DynFailure::none;
    }
    if (present) {
        set_dynamic_element(container, at, value);
        return DynFailure::none;
    }
    if (container.size >= container.limit) {
        return DynFailure::sparse_budget;
    }
    ensure_capacity(container, container.size + 1U);
    for (auto index = container.size; index > at; --index) {
        container.keys[index] = container.keys[index - 1U];
        set_dynamic_element(container, index, dynamic_element(container, index - 1U));
    }
    container.keys[at] = ordinal;
    set_dynamic_element(container, at, value);
    ++container.size;
    return DynFailure::none;
}

// The byte a string index register selects (known_string_index): none for
// an unknown, negative (signed) or out-of-range index.
[[nodiscard]] std::optional<std::size_t> string_byte_index(const KInst& inst,
    const Word value, const std::size_t size) noexcept
{
    const auto mask = kernel_word::mask(inst.width);
    if ((value.b & mask) != 0U) {
        return std::nullopt;
    }
    const auto bits = value.a & mask;
    if ((inst.flags & flag_signed) != 0U && ((bits >> (inst.width - 1U)) & 1U) != 0U) {
        return std::nullopt;
    }
    if (bits >= size) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(bits);
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
    if (type.associative
        && (type.string_indices || type.index_width == 0U || type.index_width > 64U)) {
        return false;
    }
    if (type.fixed && !type.sparse) {
        return false;
    }
    return !type.vhdl_shaped
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
        | (type.queue ? queue_flag : 0U)
        | (type.element_kind == ContainerElementKind::Scalar ? dynamic_scalar_flag : 0U);
    if (type.associative) {
        // A missing key reads the default element, all zero
        // (default_container_element), until a `default:` is set.
        header.fill_a = 0U;
        header.fill_b = 0U;
        header.flags |= dynamic_associative_flag
            | (type.signed_indices ? dynamic_signed_keys_flag : 0U);
        header.key_width = type.index_width;
    }
    if (type.sparse) {
        header.flags |= dynamic_sparse_flag;
        header.maximum = sparse_container_element_count(type);
        // The reference compacts at a write that finds the stored elements
        // costing at least the image, while the image fits the budget
        // (compact_when_dense).
        const auto words = (header.maximum > std::numeric_limits<std::uint64_t>::max() / 64U
                               / std::max<std::uint64_t>(width, 1U))
            ? std::numeric_limits<std::uint64_t>::max() / 2U
            : (header.maximum * width + 63U) / 64U;
        const auto image_bytes = words > maximum_container_storage_bytes / 16U
            ? maximum_container_storage_bytes + 1U
            : static_cast<std::size_t>(words) * 16U;
        const auto per_element = 2U * sizeof(PackedLogic4);
        header.threshold = image_bytes > maximum_container_storage_bytes
            ? std::numeric_limits<std::uint64_t>::max()
            : (image_bytes + per_element - 1U) / per_element - 1U;
    }
    return header;
}

void grow_dynamic(StaticKernelDynamicContainer& container, const std::uint64_t needed)
{
    if (sparse_layout(container)) {
        // A sparse array's write: its dense image once it stores enough
        // (the reference's compact_when_dense), else room for one more.
        if (dense_layout(container)) {
            return;
        }
        if (container.size >= container.threshold) {
            make_dense(container);
            return;
        }
        if (needed <= container.capacity) {
            return;
        }
    }
    const auto capacity = std::bit_ceil(std::max<std::uint64_t>(needed, 4U));
    if (capacity > std::numeric_limits<std::size_t>::max() / (2U * sizeof(std::uint64_t))) {
        throw std::bad_alloc { };
    }
    auto* data = static_cast<std::uint64_t*>(
        std::malloc(static_cast<std::size_t>(capacity) * 2U * sizeof(std::uint64_t)));
    if (data == nullptr) {
        throw std::bad_alloc { };
    }
    std::uint64_t* keys = nullptr;
    if ((container.flags & (dynamic_associative_flag | dynamic_sparse_flag)) != 0U) {
        keys = static_cast<std::uint64_t*>(
            std::malloc(static_cast<std::size_t>(capacity) * sizeof(std::uint64_t)));
        if (keys == nullptr) {
            std::free(data);
            throw std::bad_alloc { };
        }
        if (container.size != 0U) {
            std::memcpy(keys, container.keys,
                static_cast<std::size_t>(container.size) * sizeof(std::uint64_t));
        }
    }
    for (std::uint64_t ordinal = 0U; ordinal < container.size; ++ordinal) {
        const auto value = dynamic_element(container, ordinal);
        data[2U * ordinal] = value.a;
        data[2U * ordinal + 1U] = value.b;
    }
    std::free(container.data);
    std::free(container.keys);
    container.data = data;
    container.keys = keys;
    container.capacity = capacity;
    container.head = 0U;
}

void reshape_dynamic(StaticKernelDynamicContainer& target,
    const StaticKernelDynamicContainer& source)
{
    if (dense_layout(source)) {
        if (!dense_layout(target) || target.capacity != source.capacity) {
            auto* data = static_cast<std::uint64_t*>(std::malloc(
                static_cast<std::size_t>(2U * source.capacity) * sizeof(std::uint64_t)));
            if (data == nullptr) {
                throw std::bad_alloc { };
            }
            std::free(target.data);
            std::free(target.keys);
            target.data = data;
            target.keys = nullptr;
            target.capacity = source.capacity;
            target.flags |= dynamic_dense_flag;
        }
        target.size = 0U;
        return;
    }
    if (dense_layout(target)) {
        std::free(target.data);
        target.data = nullptr;
        target.capacity = 0U;
        target.flags &= ~dynamic_dense_flag;
    }
    target.size = 0U;
    if (source.size > target.capacity) {
        grow_dynamic(target, source.size);
    }
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
    if (sparse_layout(container)) {
        if (dense_layout(container)) {
            value.elements.clear();
            value.compact_planes.assign(container.data, container.data + 2U * container.capacity);
        } else {
            value.keys.reserve(static_cast<std::size_t>(container.size));
            for (std::uint64_t ordinal = 0U; ordinal < container.size; ++ordinal) {
                value.keys.push_back(
                    PackedLogic4::from_aval_bval(32U, container.keys[ordinal], 0U));
            }
        }
        if ((container.flags & dynamic_default_flag) != 0U) {
            value.associative_default = PackedLogic4::from_aval_bval(
                container.element_width, container.fill_a, container.fill_b);
        }
    }
    if ((container.flags & dynamic_associative_flag) != 0U) {
        value.keys.reserve(static_cast<std::size_t>(container.size));
        for (std::uint64_t ordinal = 0U; ordinal < container.size; ++ordinal) {
            value.keys.push_back(PackedLogic4::from_aval_bval(container.key_width,
                key_value(container.keys[ordinal], container.key_width,
                    signed_keys(container)),
                0U));
        }
        if ((container.flags & dynamic_default_flag) != 0U) {
            value.associative_default = PackedLogic4::from_aval_bval(
                container.element_width, container.fill_a, container.fill_b);
        }
    }
    return value;
}

void assign_dynamic_value(StaticKernelDynamicContainer& container,
    const ContainerValue& value)
{
    if (sparse_layout(container)) {
        // Back to the stored-element layout, then the value's own.
        if (dense_layout(container)) {
            std::free(container.data);
            container.data = nullptr;
            container.capacity = 0U;
            container.flags &= ~dynamic_dense_flag;
        }
        container.size = 0U;
        if (value.associative_default) {
            const auto a = value.associative_default->aval_words();
            const auto b = value.associative_default->bval_words();
            container.fill_a = a.empty() ? 0U : a.front();
            container.fill_b = b.empty() ? 0U : b.front();
            container.flags |= dynamic_default_flag;
        } else {
            const auto initial
                = (container.flags & (two_state_flag | dynamic_scalar_flag)) != 0U
                ? Word { } : kernel_word::all_x(container.element_width);
            container.fill_a = initial.a;
            container.fill_b = initial.b;
            container.flags &= ~dynamic_default_flag;
        }
        if (!value.compact_planes.empty()) {
            if (value.compact_planes.size() != 2U * plane_words(container)) {
                throw std::invalid_argument { "sparse array image does not match its type" };
            }
            auto* data = static_cast<std::uint64_t*>(
                std::malloc(value.compact_planes.size() * sizeof(std::uint64_t)));
            if (data == nullptr) {
                throw std::bad_alloc { };
            }
            std::memcpy(data, value.compact_planes.data(),
                value.compact_planes.size() * sizeof(std::uint64_t));
            std::free(container.keys);
            container.keys = nullptr;
            container.data = data;
            container.capacity = plane_words(container);
            container.flags |= dynamic_dense_flag;
            return;
        }
        if (value.keys.size() != value.elements.size()) {
            throw std::invalid_argument { "sparse array keys do not match its elements" };
        }
        const auto size = static_cast<std::uint64_t>(value.elements.size());
        ensure_capacity(container, size);
        for (std::uint64_t index = 0U; index < size; ++index) {
            const auto key = value.keys[static_cast<std::size_t>(index)].aval_words();
            const auto a = value.elements[static_cast<std::size_t>(index)].aval_words();
            const auto b = value.elements[static_cast<std::size_t>(index)].bval_words();
            container.keys[index] = key.empty() ? 0U : key.front();
            container.data[2U * index] = a.empty() ? 0U : a.front();
            container.data[2U * index + 1U] = b.empty() ? 0U : b.front();
        }
        container.size = size;
        return;
    }
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
    if ((container.flags & dynamic_associative_flag) != 0U) {
        if (value.keys.size() != value.elements.size()) {
            throw std::invalid_argument { "associative array keys do not match its elements" };
        }
        for (std::uint64_t ordinal = 0U; ordinal < size; ++ordinal) {
            const auto a = value.keys[static_cast<std::size_t>(ordinal)].aval_words();
            container.keys[ordinal] = order_key(a.empty() ? 0U : a.front(),
                container.key_width, signed_keys(container));
        }
        if (value.associative_default) {
            const auto a = value.associative_default->aval_words();
            const auto b = value.associative_default->bval_words();
            container.fill_a = a.empty() ? 0U : a.front();
            container.fill_b = b.empty() ? 0U : b.front();
            container.flags |= dynamic_default_flag;
        } else {
            container.fill_a = 0U;
            container.fill_b = 0U;
            container.flags &= ~dynamic_default_flag;
        }
    }
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
        std::free(headers_.back().keys);
        headers_.pop_back();
    }
}

DynFailure execute_str(const KInst& inst, const CompiledBody& body, std::string* strings,
    Word* registers)
{
    switch (static_cast<StrOp>(inst.sub)) {
    case StrOp::copy:
        if (inst.offset != inst.z) {
            strings[inst.offset] = strings[inst.z];
        }
        return DynFailure::none;
    case StrOp::load_constant:
        strings[inst.offset].assign(body.string_bytes, static_cast<std::size_t>(inst.imm_a),
            static_cast<std::size_t>(inst.imm_b));
        return DynFailure::none;
    case StrOp::concat: {
        const auto* operands = body.string_operands.data() + inst.imm_a;
        std::size_t total = 0U;
        for (std::uint64_t index = 0U; index < inst.imm_b; ++index) {
            const auto size = strings[operands[index]].size();
            if (size > maximum_string_bytes - total) {
                return DynFailure::concat_limit;
            }
            total += size;
        }
        std::string result;
        result.reserve(total);
        for (std::uint64_t index = 0U; index < inst.imm_b; ++index) {
            result += strings[operands[index]];
        }
        strings[inst.offset] = std::move(result);
        return DynFailure::none;
    }
    case StrOp::compare: {
        const bool equal = strings[inst.z] == strings[inst.imm_b];
        registers[inst.d] = { equal != (inst.imm_a != 0U) ? 1U : 0U, 0U };
        return DynFailure::none;
    }
    case StrOp::length:
        registers[inst.d] = { static_cast<std::uint32_t>(strings[inst.z].size()), 0U };
        return DynFailure::none;
    case StrOp::index: {
        const auto& source = strings[inst.z];
        const auto at = string_byte_index(inst, registers[inst.y], source.size());
        registers[inst.d] = { at ? static_cast<unsigned char>(source[*at]) : 0U, 0U };
        return DynFailure::none;
    }
    case StrOp::replace_byte: {
        auto& target = strings[inst.offset];
        const auto at = string_byte_index(inst, registers[inst.y], target.size());
        if (!at) {
            // A write out of range leaves the string unchanged (6.16).
            return DynFailure::none;
        }
        const auto byte = registers[inst.x];
        if ((byte.b & kernel_word::mask(static_cast<std::uint32_t>(inst.imm_a))) != 0U) {
            return DynFailure::byte_unknown;
        }
        if (target.size() > maximum_string_bytes) {
            return DynFailure::string_limit;
        }
        if ((byte.a & 0xffU) != 0U) {
            target[*at] = static_cast<char>(byte.a & 0xffU);
        }
        return DynFailure::none;
    }
    }
    return DynFailure::none;
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
    case DynFailure::key_unknown:
        return "associative-array index must be a known integral value";
    case DynFailure::assoc_budget:
        return "associative array exceeds the per-container owning-storage budget";
    case DynFailure::fixed_unknown:
        return "static-array index must be a known 32-bit integral value";
    case DynFailure::fixed_range:
        return "static-array index is out of range";
    case DynFailure::linear_unknown:
        return "multidimensional linear index must be a known integral value";
    case DynFailure::linear_negative:
        return "multidimensional linear index cannot be negative";
    case DynFailure::linear_range:
        return "multidimensional linear index is out of range";
    case DynFailure::sparse_budget:
        return "sparse SimIR array exceeds its owning-storage budget";
    case DynFailure::concat_limit:
        return "string concatenation exceeds the string length limit";
    case DynFailure::byte_unknown:
        return "string replacement byte contains X or Z";
    case DynFailure::string_limit:
        return "string value exceeds the byte limit";
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
            if (sparse_layout(container)) {
                // An unknown or out-of-range index reads the element type's
                // invalid-index value (sparse_read_ordinal; 7.4.6).
                const auto ordinal = sparse_ordinal(inst, registers[inst.y],
                    container.maximum, false);
                if (!ordinal.ordinal) {
                    registers[inst.d] = { container.empty_a, container.empty_b };
                } else if (dense_layout(container)) {
                    registers[inst.d] = dense_element(container, *ordinal.ordinal);
                } else {
                    const auto at = lower_key(container, *ordinal.ordinal);
                    registers[inst.d] = at < container.size
                            && container.keys[at] == *ordinal.ordinal
                        ? dynamic_element(container, at)
                        : Word { container.fill_a, container.fill_b };
                }
                return DynFailure::none;
            }
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
            if (sparse_layout(container)) {
                const auto value = registers[inst.x];
                if (two_state && (value.b & element_mask) != 0U) {
                    return DynFailure::write_mismatch;
                }
                const auto ordinal = sparse_ordinal(inst, registers[inst.y],
                    container.maximum, true);
                if (ordinal.failure != DynFailure::none) {
                    return ordinal.failure;
                }
                return write_sparse(container, *ordinal.ordinal, value);
            }
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
            if (sparse_layout(container)) {
                assign_dynamic_value(container, dynamic_value(*other, ContainerType { }));
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
            if ((container.flags & dynamic_associative_flag) != 0U) {
                // The keys and the `default:` value go with the elements.
                if (other->size != 0U) {
                    std::memcpy(container.keys, other->keys,
                        static_cast<std::size_t>(other->size) * sizeof(std::uint64_t));
                }
                container.fill_a = other->fill_a;
                container.fill_b = other->fill_b;
                container.flags = (container.flags & ~dynamic_default_flag)
                    | (other->flags & dynamic_default_flag);
            }
            container.size = other->size;
            return DynFailure::none;
        }
        case DynOp::assoc_read:
        case DynOp::assoc_write:
        case DynOp::assoc_delete:
        case DynOp::exists: {
            const auto key_word = registers[inst.y];
            const auto width = container.key_width;
            if ((key_word.b & kernel_word::mask(width)) != 0U) {
                return DynFailure::key_unknown;
            }
            if (op == DynOp::assoc_write && two_state
                && (registers[inst.x].b & element_mask) != 0U) {
                return DynFailure::write_mismatch;
            }
            const auto key = order_key(key_word.a & kernel_word::mask(width), width,
                signed_keys(container));
            const auto at = lower_key(container, key);
            const bool present = at < container.size && container.keys[at] == key;
            switch (op) {
            case DynOp::assoc_read:
                registers[inst.d] = present ? dynamic_element(container, at)
                                            : Word { container.fill_a, container.fill_b };
                break;
            case DynOp::exists:
                registers[inst.d] = { present ? 1U : 0U, 0U };
                break;
            case DynOp::assoc_delete:
                if (present) {
                    for (auto ordinal = at; ordinal + 1U < container.size; ++ordinal) {
                        container.keys[ordinal] = container.keys[ordinal + 1U];
                        set_dynamic_element(container, ordinal,
                            dynamic_element(container, ordinal + 1U));
                    }
                    --container.size;
                }
                break;
            default:
                if (present) {
                    set_dynamic_element(container, at, registers[inst.x]);
                    break;
                }
                if (container.size >= container.limit) {
                    return DynFailure::assoc_budget;
                }
                ensure_capacity(container, container.size + 1U);
                for (auto ordinal = container.size; ordinal > at; --ordinal) {
                    container.keys[ordinal] = container.keys[ordinal - 1U];
                    set_dynamic_element(container, ordinal,
                        dynamic_element(container, ordinal - 1U));
                }
                container.keys[at] = key;
                set_dynamic_element(container, at, registers[inst.x]);
                ++container.size;
                break;
            }
            return DynFailure::none;
        }
        case DynOp::assoc_default: {
            const auto value = registers[inst.x];
            if (two_state && (value.b & element_mask) != 0U) {
                return DynFailure::write_mismatch;
            }
            container.fill_a = value.a;
            container.fill_b = value.b;
            container.flags |= dynamic_default_flag;
            return DynFailure::none;
        }
        case DynOp::first:
        case DynOp::last:
        case DynOp::next:
        case DynOp::prev: {
            // An empty array selects nothing without reading the key
            // (the reference's traversal).
            std::optional<std::uint64_t> selected;
            if (container.size != 0U) {
                if (op == DynOp::first) {
                    selected = 0U;
                } else if (op == DynOp::last) {
                    selected = container.size - 1U;
                } else {
                    const auto key_word = registers[inst.y];
                    const auto width = container.key_width;
                    if ((key_word.b & kernel_word::mask(width)) != 0U) {
                        return DynFailure::key_unknown;
                    }
                    const auto key = order_key(key_word.a & kernel_word::mask(width),
                        width, signed_keys(container));
                    const auto at = lower_key(container, key);
                    if (op == DynOp::next) {
                        const auto following
                            = at < container.size && container.keys[at] == key ? at + 1U : at;
                        if (following < container.size) {
                            selected = following;
                        }
                    } else if (at != 0U) {
                        selected = at - 1U;
                    }
                }
            }
            if (selected) {
                registers[inst.y] = { key_value(container.keys[*selected],
                                          container.key_width, signed_keys(container)),
                    0U };
            }
            registers[inst.d] = { selected ? 1U : 0U, 0U };
            return DynFailure::none;
        }
        }
    } catch (const std::bad_alloc&) {
        return DynFailure::out_of_memory;
    }
    return DynFailure::none;
}

} // namespace fsim::runtime::simir::static_kernel_detail
