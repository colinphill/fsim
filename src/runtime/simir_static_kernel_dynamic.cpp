// SPDX-License-Identifier: Apache-2.0

#include "simir_static_kernel_dynamic.hpp"

#include "simir_container_ops.hpp"

#include "fsim/runtime/string_methods.hpp"
#include "fsim/runtime/systemverilog_scalar.hpp"

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <optional>
#include <span>
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

// The leaf ordinal of a flat aggregate's member read or write
// (dyn_layout_aggregate) among the `size` leaves: of the element the index
// selects as a dense fixed-size array's does (the reference's
// fixed_offset), or of the structure value itself.
[[nodiscard]] SparseOrdinal aggregate_ordinal(const KInst& inst, const Word index,
    const std::uint64_t size, const bool writing) noexcept
{
    const auto leaves = dyn_aggregate_leaves(inst.imm_a);
    const auto leaf = dyn_aggregate_leaf(inst.imm_a);
    if ((inst.imm_a & dyn_layout_aggregate_box) != 0U) {
        return { leaf, DynFailure::none };
    }
    const auto element = sparse_ordinal(inst, index, size / leaves, writing);
    if (!element.ordinal) {
        return element;
    }
    return { *element.ordinal * leaves + leaf, DynFailure::none };
}

// The value of a flat aggregate's member read with an unknown or
// out-of-range index: the leaf type's initial value (7.4.6).
[[nodiscard]] Word aggregate_leaf_initial(const KInst& inst) noexcept
{
    return (inst.imm_a & dyn_layout_leaf_zero) != 0U ? Word { }
                                                     : kernel_word::all_x(inst.offset);
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

// Removes element `at` (< size), shifting the later elements down. The
// removed slot moves to the end (a text slot keeps its block).
void erase_at(StaticKernelDynamicContainer& container, const std::uint64_t at) noexcept
{
    if (at == 0U) {
        container.head = (container.head + 1U) & (container.capacity - 1U);
    } else {
        const auto removed = dynamic_element(container, at);
        for (auto ordinal = at; ordinal + 1U < container.size; ++ordinal) {
            set_dynamic_element(container, ordinal,
                dynamic_element(container, ordinal + 1U));
        }
        set_dynamic_element(container, container.size - 1U, removed);
    }
    --container.size;
    if (container.size == 0U) {
        container.head = 0U;
    }
}

// Frees the blocks of a container's text slots.
void free_text_blocks(StaticKernelDynamicContainer& container) noexcept
{
    if ((container.flags & dynamic_text_keys_flag) != 0U) {
        for (std::uint64_t slot = 0U; container.keys != nullptr && slot < container.capacity;
             ++slot) {
            std::free(reinterpret_cast<char*>(
                static_cast<std::uintptr_t>(container.keys[2U * slot])));
            container.keys[2U * slot] = 0U;
            container.keys[2U * slot + 1U] = 0U;
        }
    }
    if ((container.flags & dynamic_text_flag) == 0U) {
        return;
    }
    for (std::uint64_t slot = 0U; container.data != nullptr && slot < container.capacity;
         ++slot) {
        std::free(reinterpret_cast<char*>(static_cast<std::uintptr_t>(container.data[2U * slot])));
        container.data[2U * slot] = 0U;
        container.data[2U * slot + 1U] = 0U;
    }
    // An associative array's `default:` text.
    std::free(reinterpret_cast<char*>(static_cast<std::uintptr_t>(container.fill_a)));
    container.fill_a = 0U;
    container.fill_b = 0U;
}

// Element `ordinal` (< capacity) of a container of strings holds `text`.
void set_text_element(StaticKernelDynamicContainer& container, const std::uint64_t ordinal,
    const std::string_view text)
{
    set_dynamic_element(container, ordinal,
        assign_text(dynamic_element(container, ordinal), text));
}

// Elements [first, first + count) of a container of strings (with room for
// them) take elements [0, count) of `source`, another container.
void copy_text_elements(StaticKernelDynamicContainer& container,
    const StaticKernelDynamicContainer& source, const std::uint64_t first,
    const std::uint64_t count)
{
    for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
        set_text_element(container, first + ordinal,
            text_of(dynamic_element(source, ordinal)));
    }
}

// An associative array (head 0) gets key `key` at `at` (<= size): the later
// keys and element slots move up, the spare slot at the end taking the new
// key's place (a text slot keeps its block). Throws std::bad_alloc.
void insert_key_slot(StaticKernelDynamicContainer& container, const std::uint64_t at,
    const std::uint64_t key)
{
    ensure_capacity(container, container.size + 1U);
    const auto spare = dynamic_element(container, container.size);
    for (auto ordinal = container.size; ordinal > at; --ordinal) {
        container.keys[ordinal] = container.keys[ordinal - 1U];
        set_dynamic_element(container, ordinal, dynamic_element(container, ordinal - 1U));
    }
    container.keys[at] = key;
    set_dynamic_element(container, at, spare);
    ++container.size;
}

// Key slot `ordinal` (< capacity) of an associative array with string keys.
[[nodiscard]] Word text_key_slot(const StaticKernelDynamicContainer& container,
    const std::uint64_t ordinal) noexcept
{
    return { container.keys[2U * ordinal], container.keys[2U * ordinal + 1U] };
}

void set_text_key_slot(StaticKernelDynamicContainer& container, const std::uint64_t ordinal,
    const Word slot) noexcept
{
    container.keys[2U * ordinal] = slot.a;
    container.keys[2U * ordinal + 1U] = slot.b;
}

// The position of the first string key not below `key`.
[[nodiscard]] std::uint64_t lower_text_key(const StaticKernelDynamicContainer& container,
    const std::string_view key) noexcept
{
    std::uint64_t low = 0U;
    std::uint64_t high = container.size;
    while (low < high) {
        const auto middle = low + (high - low) / 2U;
        if (text_of(text_key_slot(container, middle)) < key) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low;
}

// An associative array with string keys gets key `key` at `at` (<= size):
// the later key and element slots move up, the spare slots at the end
// taking the new key's place (their blocks kept). Throws std::bad_alloc.
void insert_text_key_slot(StaticKernelDynamicContainer& container, const std::uint64_t at,
    const std::string_view key)
{
    ensure_capacity(container, container.size + 1U);
    const auto new_key = assign_text(text_key_slot(container, container.size), key);
    const auto spare = dynamic_element(container, container.size);
    for (auto ordinal = container.size; ordinal > at; --ordinal) {
        set_text_key_slot(container, ordinal, text_key_slot(container, ordinal - 1U));
        set_dynamic_element(container, ordinal, dynamic_element(container, ordinal - 1U));
    }
    set_dynamic_element(container, at, spare);
    set_text_key_slot(container, at, new_key);
    ++container.size;
}

// An associative array loses key `at` (< size); the removed element slot
// moves to the end.
void erase_key_slot(StaticKernelDynamicContainer& container, const std::uint64_t at) noexcept
{
    const auto removed = dynamic_element(container, at);
    if ((container.flags & dynamic_text_keys_flag) != 0U) {
        const auto removed_key = text_key_slot(container, at);
        for (auto ordinal = at; ordinal + 1U < container.size; ++ordinal) {
            set_text_key_slot(container, ordinal, text_key_slot(container, ordinal + 1U));
            set_dynamic_element(container, ordinal, dynamic_element(container, ordinal + 1U));
        }
        set_text_key_slot(container, container.size - 1U, removed_key);
    } else {
        for (auto ordinal = at; ordinal + 1U < container.size; ++ordinal) {
            container.keys[ordinal] = container.keys[ordinal + 1U];
            set_dynamic_element(container, ordinal, dynamic_element(container, ordinal + 1U));
        }
    }
    set_dynamic_element(container, container.size - 1U, removed);
    --container.size;
}

// The default text slot of an associative array of strings (fill).
[[nodiscard]] std::string_view default_text(const StaticKernelDynamicContainer& container) noexcept
{
    return text_of({ container.fill_a, container.fill_b });
}

} // namespace

namespace {

// A structure member the kernel holds as one leaf: a packed or scalar value
// of at most 64 bits (ContainerAggregateRead's leaf).
[[nodiscard]] bool leaf_member(const ContainerType& type) noexcept
{
    return (type.element_kind == ContainerElementKind::Packed
               || type.element_kind == ContainerElementKind::Scalar)
        && type.fixed && !type.sparse && !type.associative && !type.queue
        && !type.vhdl_shaped && type.element_width != 0U && type.element_width <= 64U
        && sparse_container_element_count(type) == 1U;
}

// A structure (not a union) whose members are leaves or such structures.
[[nodiscard]] bool flat_structure_members(const ContainerType& type) noexcept
{
    return !type.union_aggregate && !type.tagged_union && !type.element_types.empty()
        && std::ranges::all_of(type.element_types, [](const ContainerType& member) {
               return member.element_kind == ContainerElementKind::Aggregate
                   ? member.aggregate_value && flat_structure_members(member)
                   : leaf_member(member);
           });
}

[[nodiscard]] std::uint32_t member_leaves(const ContainerType& type) noexcept
{
    std::uint32_t count = 0U;
    for (const auto& member : type.element_types) {
        count += member.element_kind == ContainerElementKind::Aggregate
            ? member_leaves(member) : 1U;
    }
    return count;
}

// The elements of flat aggregate `type`: one for a structure value.
[[nodiscard]] std::uint64_t aggregate_elements(const ContainerType& type) noexcept
{
    return type.aggregate_value ? 1U : sparse_container_element_count(type);
}

} // namespace

bool flat_aggregate_type(const ContainerType& type) noexcept
{
    if (type.element_kind != ContainerElementKind::Aggregate || type.vhdl_shaped
        || !flat_structure_members(type)) {
        return false;
    }
    if (!type.aggregate_value
        && (!type.fixed || type.sparse || type.associative || type.queue)) {
        return false;
    }
    // Leaf offsets and counts fit the instruction (dyn_aggregate_leaf).
    const auto leaves = member_leaves(type);
    return leaves <= 0xFFFFU
        && aggregate_elements(type) <= maximum_container_elements(type) / leaves;
}

std::uint32_t aggregate_leaf_count(const ContainerType& type) noexcept
{
    return member_leaves(type);
}

std::uint64_t aggregate_element_count(const ContainerType& type) noexcept
{
    return aggregate_elements(type);
}

namespace {

void append_leaf_types(const ContainerType& type, std::vector<const ContainerType*>& leaves)
{
    for (const auto& member : type.element_types) {
        if (member.element_kind == ContainerElementKind::Aggregate) {
            append_leaf_types(member, leaves);
        } else {
            leaves.push_back(&member);
        }
    }
}

} // namespace

std::vector<const ContainerType*> aggregate_leaf_types(const ContainerType& type)
{
    std::vector<const ContainerType*> leaves;
    append_leaf_types(type, leaves);
    return leaves;
}

std::vector<std::uint32_t> aggregate_leaf_widths(const ContainerType& type)
{
    std::vector<std::uint32_t> widths;
    for (const auto* leaf : aggregate_leaf_types(type)) {
        widths.push_back(static_cast<std::uint32_t>(leaf->element_width));
    }
    return widths;
}

std::vector<std::uint32_t> bridged_leaf_widths(const ContainerType& type)
{
    if (type.element_kind == ContainerElementKind::Aggregate) {
        return aggregate_leaf_widths(type);
    }
    return { static_cast<std::uint32_t>(type.element_width) };
}

std::uint64_t aggregate_bridge_bits(const ContainerType& type)
{
    std::uint64_t bits = 0U;
    for (const auto width : aggregate_leaf_widths(type)) {
        bits += width;
    }
    return bits * aggregate_elements(type);
}

std::optional<AggregateLeaf> aggregate_leaf(const ContainerType& type,
    const std::span<const std::uint32_t> members) noexcept
{
    const ContainerType* selected = &type;
    std::uint32_t offset = 0U;
    for (const auto member : members) {
        if (selected->element_kind != ContainerElementKind::Aggregate
            || member >= selected->element_types.size()) {
            return std::nullopt;
        }
        for (std::uint32_t before = 0U; before < member; ++before) {
            const auto& sibling = selected->element_types[before];
            offset += sibling.element_kind == ContainerElementKind::Aggregate
                ? member_leaves(sibling) : 1U;
        }
        selected = &selected->element_types[member];
    }
    if (selected->element_kind == ContainerElementKind::Aggregate) {
        return std::nullopt;
    }
    return AggregateLeaf { offset, selected };
}

std::optional<AggregateRange> aggregate_range(const ContainerType& type,
    const std::span<const std::uint32_t> members) noexcept
{
    const ContainerType* selected = &type;
    std::uint32_t offset = 0U;
    for (const auto member : members) {
        if (selected->element_kind != ContainerElementKind::Aggregate
            || member >= selected->element_types.size()) {
            return std::nullopt;
        }
        for (std::uint32_t before = 0U; before < member; ++before) {
            const auto& sibling = selected->element_types[before];
            offset += sibling.element_kind == ContainerElementKind::Aggregate
                ? member_leaves(sibling) : 1U;
        }
        selected = &selected->element_types[member];
    }
    if (selected->element_kind != ContainerElementKind::Aggregate) {
        return std::nullopt;
    }
    return AggregateRange { offset, member_leaves(*selected), selected };
}

DynFailure execute_dyn_aggregate_element(const KInst& inst,
    StaticKernelDynamicContainer& array, StaticKernelDynamicContainer& box,
    const kernel_word::Word* registers, const std::uint64_t* nodes)
{
    const bool reading = static_cast<DynOp>(inst.sub) == DynOp::aggregate_element_read;
    const auto first = aggregate_ordinal(inst, registers[inst.y], array.size, !reading);
    for (std::uint32_t leaf = 0U; leaf < inst.offset; ++leaf) {
        const auto node = nodes[static_cast<std::size_t>(leaf) * dyn_graph_node_words];
        if (reading) {
            // An invalid index reads each leaf's initial value (7.4.6).
            const auto width = static_cast<std::uint32_t>(node);
            set_dynamic_element(box, leaf, first.ordinal
                    ? dynamic_element(array, *first.ordinal + leaf)
                    : (node >> 32U) != 0U ? Word { } : kernel_word::all_x(width));
        } else {
            if (!first.ordinal) {
                return first.failure;
            }
            set_dynamic_element(array, *first.ordinal + leaf, dynamic_element(box, leaf));
        }
    }
    return reading || first.ordinal ? DynFailure::none : first.failure;
}

const char* native_dynamic_type_reason(const ContainerType& type) noexcept
{
    if (type.associative && !type.string_indices
        && (type.index_width == 0U || type.index_width > 64U)) {
        return "dynamic_containers:type:index_width";
    }
    if (type.vhdl_shaped) {
        return "dynamic_containers:type:vhdl_shaped";
    }
    if (type.element_kind == ContainerElementKind::Aggregate || type.aggregate_value
        || type.union_aggregate) {
        return flat_aggregate_type(type) ? nullptr : "dynamic_containers:type:aggregate";
    }
    if (type.element_kind == ContainerElementKind::String) {
        // Queues, dynamic arrays and associative arrays (integral keys) of
        // strings (text slots).
        return type.fixed ? "dynamic_containers:type:fixed_string" : nullptr;
    }
    if (type.element_kind == ContainerElementKind::Container) {
        return "dynamic_containers:type:nested";
    }
    if (type.element_kind != ContainerElementKind::Packed
        && type.element_kind != ContainerElementKind::Scalar) {
        return "dynamic_containers:type:element_kind";
    }
    if (type.element_width == 0U || type.element_width > 64U) {
        return "dynamic_containers:type:element_width";
    }
    return nullptr;
}

bool native_dynamic_type(const ContainerType& type) noexcept
{
    return native_dynamic_type_reason(type) == nullptr;
}

Word reserve_text(const Word slot, const std::uint64_t needed)
{
    const auto capacity = slot.b >> 32U;
    if (needed <= capacity) {
        return slot;
    }
    if (needed > std::numeric_limits<std::uint32_t>::max()) {
        throw std::bad_alloc { };
    }
    const auto grown = std::min<std::uint64_t>(std::bit_ceil(std::max<std::uint64_t>(needed, 16U)),
        std::numeric_limits<std::uint32_t>::max());
    auto* data = static_cast<char*>(std::malloc(static_cast<std::size_t>(grown)));
    if (data == nullptr) {
        throw std::bad_alloc { };
    }
    const auto text = text_of(slot);
    if (!text.empty()) {
        std::memcpy(data, text.data(), text.size());
    }
    std::free(reinterpret_cast<char*>(static_cast<std::uintptr_t>(slot.a)));
    return { static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(data)),
        (slot.b & 0xFFFF'FFFFU) | (grown << 32U) };
}

Word assign_text(const Word slot, const std::string_view text)
{
    auto result = reserve_text({ slot.a, slot.b & ~std::uint64_t { 0xFFFF'FFFFU } }, text.size());
    if (!text.empty()) {
        std::memcpy(reinterpret_cast<char*>(static_cast<std::uintptr_t>(result.a)), text.data(),
            text.size());
    }
    result.b = (result.b & ~std::uint64_t { 0xFFFF'FFFFU }) | text.size();
    return result;
}

StaticKernelDynamicContainer make_dynamic_header(const ContainerType& type)
{
    StaticKernelDynamicContainer header;
    if (type.element_kind == ContainerElementKind::Aggregate) {
        // A flat aggregate holds every leaf from the start, each its
        // initial value (default_container_value).
        const auto count = aggregate_elements(type) * member_leaves(type);
        header.maximum = count;
        header.limit = count;
        header.element_width = 64U;
        header.flags = dynamic_aggregate_flag;
        grow_dynamic(header, count);
        header.size = count;
        assign_dynamic_value(header, default_container_value(type));
        return header;
    }
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
    if (type.element_kind == ContainerElementKind::String) {
        // An empty text slot: new[n] adds empty strings.
        header.element_width = 0U;
        header.fill_a = 0U;
        header.fill_b = 0U;
        header.empty_a = 0U;
        header.empty_b = 0U;
        header.flags = (type.queue ? queue_flag : 0U) | dynamic_text_flag;
    }
    if (type.associative) {
        // A missing key reads the element type's nonexistent-entry value
        // (fill; IEEE 1800-2023 Table 7-1, default_container_element) until
        // a `default:` is set.
        header.flags |= dynamic_associative_flag
            | (type.signed_indices ? dynamic_signed_keys_flag : 0U);
        header.key_width = type.index_width;
        if (type.string_indices) {
            header.flags = (header.flags & ~dynamic_signed_keys_flag) | dynamic_text_keys_flag;
            header.key_width = 0U;
        }
    }
    if (type.fixed && !type.sparse && type.element_kind != ContainerElementKind::String) {
        // A dense fixed-size array holds every element from the start
        // (dyn_layout_fixed), each the element type's initial value
        // (default_container_value).
        const auto count = sparse_container_element_count(type);
        header.maximum = count;
        header.limit = count;
        grow_dynamic(header, count);
        for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
            set_dynamic_element(header, ordinal, Word { header.fill_a, header.fill_b });
        }
        header.size = count;
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
        // A string key takes two words; key slots own their blocks, so
        // every slot moves and the new ones start empty.
        const bool text_keys = (container.flags & dynamic_text_keys_flag) != 0U;
        const std::size_t stride = text_keys ? 2U : 1U;
        keys = static_cast<std::uint64_t*>(std::malloc(
            static_cast<std::size_t>(capacity) * stride * sizeof(std::uint64_t)));
        if (keys == nullptr) {
            std::free(data);
            throw std::bad_alloc { };
        }
        const auto kept = text_keys ? container.capacity : container.size;
        if (kept != 0U) {
            std::memcpy(keys, container.keys,
                static_cast<std::size_t>(kept) * stride * sizeof(std::uint64_t));
        }
        if (text_keys) {
            std::memset(keys + stride * kept, 0,
                static_cast<std::size_t>(capacity - kept) * stride * sizeof(std::uint64_t));
        }
    }
    // Text slots own their blocks: every slot moves (in element order), and
    // the new ones start empty.
    const auto moved = (container.flags & dynamic_text_flag) != 0U
        ? container.capacity : container.size;
    for (std::uint64_t ordinal = 0U; ordinal < moved; ++ordinal) {
        const auto value = dynamic_element(container, ordinal);
        data[2U * ordinal] = value.a;
        data[2U * ordinal + 1U] = value.b;
    }
    if ((container.flags & dynamic_text_flag) != 0U) {
        std::memset(data + 2U * moved, 0,
            static_cast<std::size_t>(capacity - moved) * 2U * sizeof(std::uint64_t));
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

namespace {

// An associative array's keys into the reference's value.
void read_keys(const StaticKernelDynamicContainer& container, ContainerValue& value)
{
    if ((container.flags & dynamic_text_keys_flag) != 0U) {
        value.string_keys.reserve(static_cast<std::size_t>(container.size));
        for (std::uint64_t ordinal = 0U; ordinal < container.size; ++ordinal) {
            value.string_keys.emplace_back(text_of(text_key_slot(container, ordinal)));
        }
        return;
    }
    value.keys.reserve(static_cast<std::size_t>(container.size));
    for (std::uint64_t ordinal = 0U; ordinal < container.size; ++ordinal) {
        value.keys.push_back(PackedLogic4::from_aval_bval(container.key_width,
            key_value(container.keys[ordinal], container.key_width, signed_keys(container)),
            0U));
    }
}

// The reference value's keys (`count` of them, matching its elements) into
// an associative array with room for them.
void write_keys(StaticKernelDynamicContainer& container, const ContainerValue& value,
    const std::uint64_t count)
{
    if ((container.flags & dynamic_text_keys_flag) != 0U) {
        if (value.string_keys.size() != count) {
            throw std::invalid_argument { "associative array keys do not match its elements" };
        }
        for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
            set_text_key_slot(container, ordinal,
                assign_text(text_key_slot(container, ordinal),
                    value.string_keys[static_cast<std::size_t>(ordinal)]));
        }
        return;
    }
    if (value.keys.size() != count) {
        throw std::invalid_argument { "associative array keys do not match its elements" };
    }
    for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
        const auto a = value.keys[static_cast<std::size_t>(ordinal)].aval_words();
        container.keys[ordinal] = order_key(a.empty() ? 0U : a.front(), container.key_width,
            signed_keys(container));
    }
}

} // namespace

ContainerValue dynamic_value(const StaticKernelDynamicContainer& container,
    const ContainerType& type)
{
    if ((container.flags & dynamic_aggregate_flag) != 0U) {
        auto value = default_container_value(type);
        std::uint64_t ordinal = 0U;
        auto visit = [&](ContainerValue& leaf) {
            const auto element = dynamic_element(container, ordinal++);
            leaf.elements.front() = PackedLogic4::from_aval_bval(
                leaf.type.element_width, element.a, element.b);
        };
        for_each_aggregate_leaf(value, visit);
        return value;
    }
    ContainerValue value;
    value.type = type;
    if ((container.flags & dynamic_text_flag) != 0U) {
        value.string_elements.reserve(static_cast<std::size_t>(container.size));
        for (std::uint64_t ordinal = 0U; ordinal < container.size; ++ordinal) {
            value.string_elements.emplace_back(text_of(dynamic_element(container, ordinal)));
        }
        if ((container.flags & dynamic_associative_flag) != 0U) {
            read_keys(container, value);
            if ((container.flags & dynamic_default_flag) != 0U) {
                value.associative_default_text = std::string { default_text(container) };
            }
        }
        return value;
    }
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
        read_keys(container, value);
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
    if ((container.flags & dynamic_aggregate_flag) != 0U) {
        std::uint64_t ordinal = 0U;
        auto visit = [&](const ContainerValue& leaf) {
            if (ordinal >= container.size || leaf.elements.size() != 1U) {
                throw std::invalid_argument { "aggregate value does not match its type" };
            }
            const auto a = leaf.elements.front().aval_words();
            const auto b = leaf.elements.front().bval_words();
            set_dynamic_element(container, ordinal++,
                { a.empty() ? 0U : a.front(), b.empty() ? 0U : b.front() });
        };
        for_each_aggregate_leaf(value, visit);
        if (ordinal != container.size) {
            throw std::invalid_argument { "aggregate value does not match its type" };
        }
        return;
    }
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
    if ((container.flags & dynamic_text_flag) != 0U) {
        const auto count = static_cast<std::uint64_t>(value.string_elements.size());
        const bool associative = (container.flags & dynamic_associative_flag) != 0U;
        container.size = 0U;
        container.head = 0U;
        ensure_capacity(container, count);
        for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
            set_text_element(container, ordinal,
                value.string_elements[static_cast<std::size_t>(ordinal)]);
        }
        if (associative) {
            write_keys(container, value, count);
        }
        container.size = count;
        if (associative) {
            const auto slot = assign_text({ container.fill_a, container.fill_b },
                value.associative_default_text.value_or(std::string { }));
            container.fill_a = slot.a;
            container.fill_b = slot.b;
            container.flags = value.associative_default_text
                ? container.flags | dynamic_default_flag
                : container.flags & ~dynamic_default_flag;
        }
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
        write_keys(container, value, size);
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
        free_text_blocks(headers_.back());
        std::free(headers_.back().data);
        std::free(headers_.back().keys);
        headers_.pop_back();
    }
}

namespace {

// A string method's integer argument (execute_string): the register's
// value (width bits, at most 32; a 32-bit one signed), none when unknown.
[[nodiscard]] std::optional<std::int32_t> string_method_integer(
    const Word value, const std::uint32_t width) noexcept
{
    if ((value.b & kernel_word::mask(width)) != 0U) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(value.a & kernel_word::mask(width)));
}

} // namespace

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
            // putc of an unknown character leaves the string unchanged.
            return inst.imm_b != 0U ? DynFailure::none : DynFailure::byte_unknown;
        }
        if (target.size() > maximum_string_bytes) {
            return DynFailure::string_limit;
        }
        if ((byte.a & 0xffU) != 0U) {
            target[*at] = static_cast<char>(byte.a & 0xffU);
        }
        return DynFailure::none;
    }
    case StrOp::change_case:
        strings[inst.offset] = string_change_case(strings[inst.z], inst.imm_a != 0U);
        return DynFailure::none;
    case StrOp::order:
        registers[inst.d] = { static_cast<std::uint32_t>(
                                  string_compare(strings[inst.z],
                                      strings[static_cast<std::size_t>(inst.imm_b)],
                                      inst.imm_a != 0U)),
            0U };
        return DynFailure::none;
    case StrOp::substr:
        strings[inst.offset] = string_substr(strings[inst.z],
            string_method_integer(registers[inst.y], inst.width),
            string_method_integer(registers[inst.x], static_cast<std::uint32_t>(inst.imm_a)));
        return DynFailure::none;
    case StrOp::to_integer:
        registers[inst.d] = { static_cast<std::uint32_t>(string_to_integer(
                                  strings[inst.z], static_cast<unsigned>(inst.imm_a))),
            0U };
        return DynFailure::none;
    case StrOp::from_integer:
        strings[inst.offset] = string_from_integer(
            string_method_integer(registers[inst.y], inst.width).value_or(0),
            static_cast<unsigned>(inst.imm_a));
        return DynFailure::none;
    case StrOp::radix:
        strings[inst.offset] = vhdl_logic9_radix_string(
            strings[inst.z], (inst.imm_a & 1U) != 0U, (inst.imm_a & 2U) != 0U);
        return DynFailure::none;
    case StrOp::plusarg:
    case StrOp::read_object:
    case StrOp::write_object:
    case StrOp::write_object_nba:
    case StrOp::format:
    case StrOp::method:
        // The kernel's own (they read the run's plusargs, variables and
        // time).
        break;
    }
    return DynFailure::none;
}

std::uint64_t execute_str_method(const std::uint64_t operation, const std::uint8_t flags,
    std::string& source, const std::string_view argument, const Word first, const Word second,
    std::string* destination)
{
    const auto operand = [&](const std::uint8_t flag, const Word word, const unsigned shift) {
        return (flags & flag) != 0U
            ? string_method_integer(word, static_cast<std::uint32_t>((operation >> shift) & 0xffU))
            : std::nullopt;
    };
    auto result = execute_string_method(static_cast<StringMethodOperator>(operation & 0xffU),
        source, argument, operand(str_method_first, first, 8U),
        operand(str_method_second, second, 16U));
    if ((flags & str_method_string) != 0U) {
        if (!result.string || destination == nullptr) {
            throw std::logic_error { "string method produced no string result" };
        }
        *destination = std::move(*result.string);
        return 0U;
    }
    if (!result.scalar_bits) {
        throw std::logic_error { "string method produced no scalar result" };
    }
    return *result.scalar_bits;
}

void append_str_format(const std::uint64_t* record, std::string& destination,
    const PackedLogic4& value, const std::string_view argument,
    const PackedLogic4& field_width, const std::uint64_t tick,
    const SystemVerilogTimeFormat& time_format)
{
    using M = StringMethodOperator;
    auto method = string_format_method(record);
    // The reference's execute_string: a dynamic field width becomes the
    // minimum width of the static form.
    if (method.operation == M::format_string_dynamic_field
        || method.operation == M::format_packed_dynamic_field) {
        method.minimum_width = checked_vhdl_textio_field_width(field_width);
        method.operation = method.operation == M::format_string_dynamic_field
            ? M::format_string
            : M::format_packed;
    }
    execute_string_format(method, destination, value, argument, tick, time_format);
}

std::string scalar_failure_message(const KInst& inst, const std::uint32_t error)
{
    const auto fields = scalar_fields(inst.imm_a);
    if (inst.sub == 0U) {
        return "SystemVerilog scalar binary operation failed (error " + std::to_string(error)
            + ", lhs kind " + std::to_string(unsigned { fields.first_kind }) + ", rhs kind "
            + std::to_string(unsigned { fields.second_kind }) + ")";
    }
    return "SystemVerilog math function failed (error " + std::to_string(error) + ")";
}

std::optional<std::string> execute_scalar(const KInst& inst, kernel_word::Word* registers)
{
    const auto fields = scalar_fields(inst.imm_a);
    const auto operand = [&](const std::uint32_t reg, const std::uint64_t bits) {
        return PackedLogic4::from_aval_bval(static_cast<std::size_t>(bits), registers[reg].a,
            registers[reg].b);
    };
    const auto first = operand(inst.x, inst.imm_b & 0xffffffffU);
    std::optional<PackedLogic4> second;
    if (fields.has_second) {
        second = operand(inst.y, inst.imm_b >> 32U);
    }
    const auto kind = [](const std::uint8_t value) {
        return static_cast<SystemVerilogScalarKind>(value);
    };
    const auto result = inst.sub == 0U
        ? systemverilog_scalar_binary_payload(
              static_cast<SystemVerilogScalarBinaryOperator>(fields.operation), first,
              kind(fields.first_kind), second ? *second : PackedLogic4 { },
              kind(fields.second_kind), kind(fields.result_kind))
        : systemverilog_math_payload(static_cast<SystemVerilogMathFunction>(fields.operation),
              first, kind(fields.first_kind), fields.first_signed,
              second ? &*second : nullptr, kind(fields.second_kind), fields.second_signed);
    if (!result) {
        return scalar_failure_message(inst, static_cast<std::uint32_t>(result.error));
    }
    const auto low = result.value.low_word();
    registers[inst.d] = { low.aval, low.bval };
    return std::nullopt;
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
    case DynFailure::missing_entry:
        return associative_missing_entry_warning;
    case DynFailure::invalid_read_index:
        return associative_invalid_read_warning;
    case DynFailure::invalid_write_index:
        return associative_invalid_write_warning;
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
    case DynFailure::text_assoc_budget:
        return "associative string array exceeds its owning-storage budget";
    case DynFailure::key_bytes:
        return "associative-array string index exceeds the byte limit";
    case DynFailure::string_limit:
        return "string value exceeds the byte limit";
    case DynFailure::stream_short:
        return "a bit stream is smaller than its targets";
    case DynFailure::stream_wide:
        return "a bit stream is wider than its target";
    case DynFailure::stream_partial:
        return "a bit stream does not fill a whole number of elements of its variable-size target";
    case DynFailure::aggregate_leaf:
        return "aggregate member write leaf type mismatch";
    }
    return "static kernel container operation failed";
}

namespace {

// A queue's or dynamic array's element ordinal from index register `index`
// (`width` bits, signed or not): none when unknown or negative.
[[nodiscard]] std::optional<std::uint64_t> text_index(const Word index,
    const std::uint32_t width, const bool is_signed)
{
    const auto requested = requested_index(index, width, is_signed);
    if (!requested || *requested < 0) {
        return std::nullopt;
    }
    return static_cast<std::uint64_t>(*requested);
}

// The keyed KOp::dyn operations of an associative array with string keys:
// key y is string register y (7.9).
DynFailure execute_text_key_dyn(const KInst& inst, StaticKernelDynamicContainer& container,
    Word* registers, std::string* strings)
{
    const auto op = static_cast<DynOp>(inst.sub);
    const bool text = (container.flags & dynamic_text_flag) != 0U;
    auto& key_register = strings[inst.y];
    if (op == DynOp::first || op == DynOp::last || op == DynOp::next || op == DynOp::prev) {
        // An empty array selects nothing and reads no key; the key changes
        // only when one is selected (7.9.4 - 7.9.7).
        std::optional<std::uint64_t> selected;
        if (container.size != 0U) {
            if (op == DynOp::first) {
                selected = 0U;
            } else if (op == DynOp::last) {
                selected = container.size - 1U;
            } else {
                if (key_register.size() > maximum_string_bytes) {
                    return DynFailure::key_bytes;
                }
                const auto at = lower_text_key(container, key_register);
                if (op == DynOp::next) {
                    const auto following = at < container.size
                            && text_of(text_key_slot(container, at)) == key_register
                        ? at + 1U : at;
                    if (following < container.size) {
                        selected = following;
                    }
                } else if (at != 0U) {
                    selected = at - 1U;
                }
            }
        }
        if (selected) {
            key_register = std::string { text_of(text_key_slot(container, *selected)) };
        }
        registers[inst.d] = { selected ? 1U : 0U, 0U };
        return DynFailure::none;
    }
    if (key_register.size() > maximum_string_bytes) {
        return DynFailure::key_bytes;
    }
    if (op == DynOp::assoc_write && (container.flags & two_state_flag) != 0U
        && (registers[inst.x].b & kernel_word::mask(container.element_width)) != 0U) {
        return DynFailure::write_mismatch;
    }
    const auto at = lower_text_key(container, key_register);
    const bool present
        = at < container.size && text_of(text_key_slot(container, at)) == key_register;
    // A missing key reads the `default:` value without a warning, else the
    // nonexistent-entry value with one (7.8.6).
    const auto missing = present || (container.flags & dynamic_default_flag) != 0U
            || (op == DynOp::assoc_read && (inst.imm_b & dyn_read_allocating) != 0U)
        ? DynFailure::none
        : DynFailure::missing_entry;
    switch (op) {
    case DynOp::assoc_read:
        registers[inst.d] = present ? dynamic_element(container, at)
                                    : Word { container.fill_a, container.fill_b };
        return missing;
    case DynOp::text_read:
        strings[inst.offset] = present
            ? std::string { text_of(dynamic_element(container, at)) }
            : std::string { default_text(container) };
        return missing;
    case DynOp::exists:
        registers[inst.d] = { present ? 1U : 0U, 0U };
        return DynFailure::none;
    case DynOp::assoc_delete:
        if (present) {
            erase_key_slot(container, at);
        }
        return DynFailure::none;
    case DynOp::assoc_write:
    case DynOp::text_write:
        if (!present) {
            if (container.size >= container.limit) {
                return text ? DynFailure::text_assoc_budget : DynFailure::assoc_budget;
            }
            insert_text_key_slot(container, at, key_register);
        }
        if (text) {
            set_text_element(container, at, strings[inst.offset]);
        } else {
            set_dynamic_element(container, at, registers[inst.x]);
        }
        return DynFailure::none;
    default:
        throw std::logic_error { "static kernel string-keyed operation is invalid" };
    }
}

// The KOp::dyn operations of a container of strings and the appends.
DynFailure execute_text_dyn(const KInst& inst, StaticKernelDynamicContainer& container,
    const StaticKernelDynamicContainer* other, Word* registers, std::string* strings)
{
    const auto op = static_cast<DynOp>(inst.sub);
    const bool is_signed = (inst.flags & flag_signed) != 0U;
    const bool text = (container.flags & dynamic_text_flag) != 0U;
    const bool associative = (container.flags & dynamic_associative_flag) != 0U;
    if (associative && (op == DynOp::text_read || op == DynOp::text_write)) {
        const auto key_word = registers[inst.y];
        const auto width = container.key_width;
        const bool user_default = (container.flags & dynamic_default_flag) != 0U;
        if ((key_word.b & kernel_word::mask(width)) != 0U) {
            // An index holding x or z bits: a read returns the `default:`
            // text or the empty string, a write is ignored (7.8.6).
            if (op == DynOp::text_write) {
                return DynFailure::invalid_write_index;
            }
            strings[inst.offset] = std::string { default_text(container) };
            return user_default ? DynFailure::none : DynFailure::invalid_read_index;
        }
        const auto key = order_key(key_word.a & kernel_word::mask(width), width,
            signed_keys(container));
        const auto at = lower_key(container, key);
        const bool present = at < container.size && container.keys[at] == key;
        if (op == DynOp::text_read) {
            // A missing key reads the `default:` text without a warning,
            // else the empty string with one (7.8.6).
            strings[inst.offset] = present
                ? std::string { text_of(dynamic_element(container, at)) }
                : std::string { default_text(container) };
            return present || user_default ? DynFailure::none
                                           : DynFailure::missing_entry;
        }
        if (!present) {
            if (container.size >= container.limit) {
                return DynFailure::text_assoc_budget;
            }
            insert_key_slot(container, at, key);
        }
        set_text_element(container, at, strings[inst.offset]);
        return DynFailure::none;
    }
    switch (op) {
    case DynOp::text_default: {
        const auto slot = assign_text({ container.fill_a, container.fill_b },
            strings[inst.offset]);
        container.fill_a = slot.a;
        container.fill_b = slot.b;
        container.flags |= dynamic_default_flag;
        return DynFailure::none;
    }
    case DynOp::text_read: {
        // An invalid or out-of-range index reads the empty string (7.4.6).
        const auto at = text_index(registers[inst.y], inst.width, is_signed);
        strings[inst.offset] = at && *at < container.size
            ? std::string { text_of(dynamic_element(container, *at)) }
            : std::string { };
        return DynFailure::none;
    }
    case DynOp::text_write: {
        const auto at = text_index(registers[inst.y], inst.width, is_signed);
        const auto size = container.size;
        const bool queue = (container.flags & queue_flag) != 0U;
        if (!at || *at > size || (*at == size && (!queue || size >= container.maximum))) {
            return DynFailure::none;
        }
        if (*at == size) {
            if (size >= container.limit) {
                return DynFailure::queue_budget;
            }
            ensure_capacity(container, size + 1U);
            ++container.size;
        }
        set_text_element(container, *at, strings[inst.offset]);
        return DynFailure::none;
    }
    case DynOp::append_clear:
    case DynOp::append_value:
    case DynOp::append_text:
    case DynOp::append_container: {
        if ((inst.imm_b & 1U) != 0U) {
            container.size = 0U;
            container.head = 0U;
        }
        const auto size = container.size;
        if (op == DynOp::append_value) {
            ensure_capacity(container, size + 1U);
            set_dynamic_element(container, size, registers[inst.x]);
            container.size = size + 1U;
        } else if (op == DynOp::append_text) {
            ensure_capacity(container, size + 1U);
            set_text_element(container, size, strings[inst.offset]);
            container.size = size + 1U;
        } else if (op == DynOp::append_container && other != nullptr) {
            // The source's count before the append (it may be the target).
            const auto count = other->size;
            ensure_capacity(container, size + count);
            if (text) {
                copy_text_elements(container, *other, size, count);
            } else {
                for (std::uint64_t ordinal = 0U; ordinal < count; ++ordinal) {
                    set_dynamic_element(container, size + ordinal,
                        dynamic_element(*other, ordinal));
                }
            }
            container.size = size + count;
        }
        // A bounded queue keeps its first elements (7.10.5).
        container.size = std::min(container.size, container.maximum);
        return DynFailure::none;
    }
    case DynOp::resize: {
        const auto size = unsigned_index(registers[inst.x], inst.width);
        if (!size) {
            return DynFailure::size_unknown;
        }
        if (*size > container.limit) {
            return DynFailure::array_budget;
        }
        // new[n](self) keeps its first elements in place; another
        // initializer's are copied; the rest are empty (7.5.1).
        const bool alias = other == &container;
        const auto kept = other != nullptr ? std::min(*size, other->size) : 0U;
        if (!alias) {
            container.size = 0U;
            container.head = 0U;
        }
        ensure_capacity(container, *size);
        if (other != nullptr && !alias) {
            copy_text_elements(container, *other, 0U, kept);
        }
        for (auto ordinal = kept; ordinal < *size; ++ordinal) {
            set_text_element(container, ordinal, { });
        }
        container.size = *size;
        return DynFailure::none;
    }
    case DynOp::copy: {
        if (other == nullptr || other == &container) {
            return DynFailure::none;
        }
        container.size = 0U;
        container.head = 0U;
        ensure_capacity(container, other->size);
        if (text) {
            copy_text_elements(container, *other, 0U, other->size);
        } else {
            for (std::uint64_t ordinal = 0U; ordinal < other->size; ++ordinal) {
                set_dynamic_element(container, ordinal, dynamic_element(*other, ordinal));
            }
        }
        if (associative) {
            // The keys and the `default:` go with the elements.
            if ((container.flags & dynamic_text_keys_flag) != 0U) {
                for (std::uint64_t ordinal = 0U; ordinal < other->size; ++ordinal) {
                    set_text_key_slot(container, ordinal,
                        assign_text(text_key_slot(container, ordinal),
                            text_of(text_key_slot(*other, ordinal))));
                }
            } else if (other->size != 0U) {
                std::memcpy(container.keys, other->keys,
                    static_cast<std::size_t>(other->size) * sizeof(std::uint64_t));
            }
            if (text) {
                const auto slot = assign_text({ container.fill_a, container.fill_b },
                    default_text(*other));
                container.fill_a = slot.a;
                container.fill_b = slot.b;
            } else {
                container.fill_a = other->fill_a;
                container.fill_b = other->fill_b;
            }
            container.flags = (container.flags & ~dynamic_default_flag)
                | (other->flags & dynamic_default_flag);
        }
        container.size = other->size;
        return DynFailure::none;
    }
    default:
        throw std::logic_error { "static kernel string container operation is invalid" };
    }
}

} // namespace

DynFailure execute_dyn(const KInst& inst, StaticKernelDynamicContainer& container,
    const StaticKernelDynamicContainer* other, Word* registers, std::string* strings)
{
    {
        const auto op = static_cast<DynOp>(inst.sub);
        const bool appending = op == DynOp::append_clear || op == DynOp::append_value
            || op == DynOp::append_text || op == DynOp::append_container;
        const bool text_keys = (container.flags & dynamic_text_keys_flag) != 0U;
        try {
            if (text_keys && (dyn_reads_index(op) || dyn_writes_index(op))) {
                return execute_text_key_dyn(inst, container, registers, strings);
            }
            if (appending || op == DynOp::text_read || op == DynOp::text_write
                || op == DynOp::text_default
                || (((container.flags & dynamic_text_flag) != 0U || text_keys)
                    && op == DynOp::copy)
                || ((container.flags & dynamic_text_flag) != 0U && op == DynOp::resize)) {
                return execute_text_dyn(inst, container, other, registers, strings);
            }
        } catch (const std::bad_alloc&) {
            return DynFailure::out_of_memory;
        }
    }
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
            if ((inst.imm_a & dyn_layout_aggregate) != 0U) {
                const auto ordinal = aggregate_ordinal(inst, registers[inst.y],
                    container.size, false);
                registers[inst.d] = ordinal.ordinal
                    ? dynamic_element(container, *ordinal.ordinal)
                    : aggregate_leaf_initial(inst);
                return DynFailure::none;
            }
            if ((inst.imm_a & dyn_layout_fixed) != 0U) {
                // A dense fixed-size array: an unknown or out-of-range
                // index reads the element type's invalid-index value
                // (7.4.6), as a sparse array's does.
                const auto ordinal = sparse_ordinal(inst, registers[inst.y],
                    container.size, false);
                registers[inst.d] = ordinal.ordinal
                    ? dynamic_element(container, *ordinal.ordinal)
                    : Word { container.empty_a, container.empty_b };
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
            if ((inst.imm_a & dyn_layout_aggregate) != 0U) {
                const auto value = registers[inst.x];
                const auto ordinal = aggregate_ordinal(inst, registers[inst.y],
                    container.size, true);
                if (ordinal.failure != DynFailure::none) {
                    return ordinal.failure;
                }
                if ((inst.imm_a & dyn_layout_leaf_two_state) != 0U
                    && (value.b & kernel_word::mask(inst.offset)) != 0U) {
                    return DynFailure::aggregate_leaf;
                }
                set_dynamic_element(container, *ordinal.ordinal, value);
                return DynFailure::none;
            }
            if ((inst.imm_a & dyn_layout_fixed) != 0U) {
                const auto value = registers[inst.x];
                if (two_state && (value.b & element_mask) != 0U) {
                    return DynFailure::write_mismatch;
                }
                const auto ordinal = sparse_ordinal(inst, registers[inst.y],
                    container.size, true);
                if (ordinal.failure != DynFailure::none) {
                    return ordinal.failure;
                }
                set_dynamic_element(container, *ordinal.ordinal, value);
                return DynFailure::none;
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
            const bool user_default = (container.flags & dynamic_default_flag) != 0U;
            if ((key_word.b & kernel_word::mask(width)) != 0U) {
                // An index holding x or z bits: a read returns the
                // nonexistent-entry value, a write is ignored, each with a
                // warning; a `default:` read has none (7.8.6).
                if (op == DynOp::assoc_read) {
                    registers[inst.d] = { container.fill_a, container.fill_b };
                    return user_default ? DynFailure::none
                                        : DynFailure::invalid_read_index;
                }
                if (op == DynOp::assoc_write) {
                    return DynFailure::invalid_write_index;
                }
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
                if (!present && !user_default
                    && (inst.imm_b & dyn_read_allocating) == 0U) {
                    return DynFailure::missing_entry;
                }
                break;
            case DynOp::exists:
                registers[inst.d] = { present ? 1U : 0U, 0U };
                break;
            case DynOp::assoc_delete:
                if (present) {
                    erase_key_slot(container, at);
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
        case DynOp::text_read:
        case DynOp::text_write:
        case DynOp::text_default:
        case DynOp::append_clear:
        case DynOp::append_value:
        case DynOp::append_text:
        case DynOp::append_container:
            // execute_text_dyn.
            break;
        case DynOp::compare:
        case DynOp::select:
        case DynOp::reduce:
        case DynOp::order:
        case DynOp::locate:
        case DynOp::stream_clear:
        case DynOp::stream_append_value:
        case DynOp::stream_append_container:
        case DynOp::stream_reverse:
        case DynOp::stream_to_value:
        case DynOp::stream_take_value:
        case DynOp::stream_to_container:
        case DynOp::stream_take_container:
        case DynOp::aggregate_unpack:
        case DynOp::aggregate_pack:
        case DynOp::aggregate_element_read:
        case DynOp::aggregate_element_write:
            throw std::logic_error { "static kernel whole-array operation misplaced" };
        }
    } catch (const std::bad_alloc&) {
        return DynFailure::out_of_memory;
    }
    return DynFailure::none;
}

std::vector<ContainerPredicateNode> dyn_graph_nodes(const CompiledBody& body,
    const std::uint64_t imm)
{
    std::vector<ContainerPredicateNode> nodes;
    const auto begin = dyn_graph_begin(imm);
    const auto count = dyn_graph_count(imm);
    nodes.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        const auto* words = body.predicate_words.data()
            + static_cast<std::size_t>((begin + index) * dyn_graph_node_words);
        ContainerPredicateNode node;
        node.operation = static_cast<ContainerPredicateOperator>(words[0] & 0xFFU);
        node.value_kind = static_cast<ContainerPredicateValueKind>((words[0] >> 8U) & 0xFFU);
        node.left = static_cast<std::uint32_t>(words[1]);
        node.right = static_cast<std::uint32_t>(words[1] >> 32U);
        node.third = static_cast<std::uint32_t>(words[2]);
        if (node.operation == ContainerPredicateOperator::constant) {
            node.constant = PackedLogic4::from_aval_bval(
                static_cast<std::size_t>(words[0] >> 16U), words[3], words[4]);
        }
        nodes.push_back(std::move(node));
    }
    return nodes;
}

void execute_dyn_whole(const KInst& inst, const CompiledBody& body,
    StaticKernelDynamicContainer& container, const ContainerType& type,
    const StaticKernelDynamicContainer* other, const ContainerType* other_type,
    const StaticKernelDynamicContainer* third, Word* registers,
    const std::function<std::uint32_t()>& random)
{
    const auto word = [](const PackedLogic4& value) {
        const auto low = value.low_word();
        return Word { low.aval, low.bval };
    };
    switch (static_cast<DynOp>(inst.sub)) {
    case DynOp::compare:
        registers[inst.d] = word(compare_container_values(dynamic_value(container, type),
            dynamic_value(*other, *other_type), (inst.imm_b & 1U) != 0U));
        return;
    case DynOp::select: {
        auto value = dynamic_value(container, type);
        const auto condition = registers[inst.x];
        select_container_value(value,
            PackedLogic4::from_aval_bval(1U, condition.a & 1U, condition.b & 1U),
            dynamic_value(*other, type), dynamic_value(*third, type));
        assign_dynamic_value(container, value);
        return;
    }
    case DynOp::reduce:
        registers[inst.d] = word(reduce_container_value(dynamic_value(container, type),
            static_cast<ContainerReductionOperator>(inst.x), dyn_graph_nodes(body, inst.imm_b)));
        return;
    case DynOp::order: {
        auto value = dynamic_value(container, type);
        order_container_value(value, static_cast<ContainerOrderingOperator>(inst.x),
            dyn_graph_nodes(body, inst.imm_b), random);
        assign_dynamic_value(container, value);
        return;
    }
    case DynOp::locate: {
        auto value = dynamic_value(container, type);
        const auto operation = static_cast<ContainerLocatorOperator>(inst.x);
        const auto graph = dyn_graph_nodes(body, inst.imm_b);
        const bool predicate = operation >= ContainerLocatorOperator::find;
        locate_container_values(value, dynamic_value(*other, *other_type), operation,
            predicate ? std::span<const ContainerPredicateNode> { graph }
                      : std::span<const ContainerPredicateNode> { },
            predicate ? std::span<const ContainerPredicateNode> { }
                      : std::span<const ContainerPredicateNode> { graph });
        assign_dynamic_value(container, value);
        return;
    }
    default:
        throw std::logic_error { "static kernel whole-array operation is invalid" };
    }
}

namespace {

/// The registers and containers of one bit-stream operation, as the
/// reference's ContainerBitstream reads them: register `value` (x or d),
/// container 0 the stream and container 1 the other container.
class StreamAccess final : public ContainerRegisterAccess {
public:
    StreamAccess(Word* registers, const RegisterId value, PackedLogic4 current,
        ContainerValue& bits, ContainerValue* other)
        : registers_ { registers }
        , value_ { value }
        , current_ { std::move(current) }
        , bits_ { bits }
        , other_ { other }
    {
    }

    [[nodiscard]] const PackedLogic4& read(const RegisterId id) override
    {
        check(id);
        return current_;
    }

    void write(const RegisterId id, PackedLogic4 value) override
    {
        check(id);
        const auto low = value.low_word();
        registers_[id] = Word { low.aval, low.bval };
    }

    [[nodiscard]] const std::string& read_string(StringRegisterId) override
    {
        throw std::logic_error { "static kernel bit stream reads no string" };
    }

    [[nodiscard]] std::string& string(StringRegisterId) override
    {
        throw std::logic_error { "static kernel bit stream writes no string" };
    }

    [[nodiscard]] ContainerValue& container(const ContainerRegisterId id) override
    {
        if (id == 0U) {
            return bits_;
        }
        if (id != 1U || other_ == nullptr) {
            throw std::logic_error { "static kernel bit stream container is invalid" };
        }
        return *other_;
    }

    [[nodiscard]] const ContainerValue& read_container(const ContainerRegisterId id) override
    {
        return container(id);
    }

    [[nodiscard]] std::uint32_t next_random() override
    {
        throw std::logic_error { "static kernel bit stream draws no random number" };
    }
    void warn(std::string_view) override
    {
        throw std::logic_error { "static kernel bit stream reads no associative entry" };
    }

private:
    void check(const RegisterId id) const
    {
        if (id != value_) {
            throw std::logic_error { "static kernel bit stream register is invalid" };
        }
    }

    Word* registers_;
    RegisterId value_;
    PackedLogic4 current_;
    ContainerValue& bits_;
    ContainerValue* other_;
};

} // namespace

void execute_dyn_stream(const KInst& inst, StaticKernelDynamicContainer& bits,
    const ContainerType& bits_type, StaticKernelDynamicContainer* other,
    const ContainerType* other_type, Word* registers, const ProcessId process,
    const InstructionIndex instruction)
{
    const auto op = static_cast<DynOp>(inst.sub);
    ContainerBitstream operation { };
    operation.bits = 0U;
    operation.container = 1U;
    operation.slice = static_cast<std::uint32_t>(inst.imm_b);
    RegisterId value = 0U;
    std::uint64_t width = 1U;
    switch (op) {
    case DynOp::stream_clear:
        operation.operation = ContainerBitstreamOperation::clear;
        break;
    case DynOp::stream_append_value:
        operation.operation = ContainerBitstreamOperation::append_value;
        value = inst.x;
        width = inst.offset;
        break;
    case DynOp::stream_append_container:
        operation.operation = ContainerBitstreamOperation::append_container;
        break;
    case DynOp::stream_reverse:
        operation.operation = ContainerBitstreamOperation::reverse;
        break;
    case DynOp::stream_to_value:
    case DynOp::stream_take_value:
        operation.operation = op == DynOp::stream_to_value
            ? ContainerBitstreamOperation::to_value
            : ContainerBitstreamOperation::take_value;
        value = inst.d;
        width = inst.imm_b;
        break;
    case DynOp::stream_to_container:
        operation.operation = ContainerBitstreamOperation::to_container;
        break;
    case DynOp::stream_take_container:
        operation.operation = ContainerBitstreamOperation::take_container;
        break;
    default:
        throw std::logic_error { "static kernel bit-stream operation is invalid" };
    }
    operation.value = value;
    auto stream = dynamic_value(bits, bits_type);
    std::optional<ContainerValue> second;
    if (other != nullptr && other_type != nullptr) {
        second = dynamic_value(*other, *other_type);
    }
    StreamAccess access { registers, value,
        PackedLogic4::from_aval_bval(static_cast<std::size_t>(width),
            registers[value].a, registers[value].b), stream,
        second ? &*second : nullptr };
    execute_container_operation(operation, access, process, instruction);
    if (op != DynOp::stream_to_value && op != DynOp::stream_to_container) {
        assign_dynamic_value(bits, stream);
    }
    if ((op == DynOp::stream_to_container || op == DynOp::stream_take_container) && second) {
        assign_dynamic_value(*other, *second);
    }
}

} // namespace fsim::runtime::simir::static_kernel_detail
