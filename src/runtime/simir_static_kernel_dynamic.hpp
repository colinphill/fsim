// SPDX-License-Identifier: Apache-2.0
//
// The static kernel's queues and dynamic arrays of packed elements of at
// most 64 bits (KOp::dyn; IEEE 1800-2023 7.5, 7.10): their headers
// (StaticKernelDynamicContainer), growth, conversion to and from the
// reference's ContainerValue, and the bytecode tier's operations. Generated
// code inlines the same operations (static_kernel_codegen) and calls only
// the growth routine.
#pragma once

#include "simir_kernel_word_ops.hpp"
#include "simir_static_kernel_ir.hpp"
#include "simir_static_kernel_native.hpp"

#include "fsim/runtime/simir_container_value.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime::simir::static_kernel_detail {

/// StaticKernelDynamicContainer::flags beyond two-state (bit 0) and queue
/// (bit 1).
inline constexpr std::uint32_t dynamic_associative_flag = 4U;
inline constexpr std::uint32_t dynamic_default_flag = 8U;
inline constexpr std::uint32_t dynamic_signed_keys_flag = 16U;
inline constexpr std::uint32_t dynamic_sparse_flag = 32U;
inline constexpr std::uint32_t dynamic_dense_flag = 64U;
/// Scalar (real) elements, which start at zero.
inline constexpr std::uint32_t dynamic_scalar_flag = 128U;
/// String elements: each element slot is a DynTextSlot.
inline constexpr std::uint32_t dynamic_text_flag = 256U;
/// String keys (an associative array): each key slot is a DynTextSlot,
/// two words of `keys`, in std::string order.
inline constexpr std::uint32_t dynamic_text_keys_flag = 512U;
/// A flat aggregate's leaves (flat_aggregate_type): every leaf is held,
/// its word pair masked to the leaf's width.
inline constexpr std::uint32_t dynamic_aggregate_flag = 1024U;

/// A text slot's bytes (DynTextSlot, as the slot's two words).
[[nodiscard]] inline std::string_view text_of(const kernel_word::Word slot) noexcept
{
    return { reinterpret_cast<const char*>(static_cast<std::uintptr_t>(slot.a)),
        static_cast<std::size_t>(slot.b & 0xFFFF'FFFFU) };
}

/// Text slot `slot` with room for `needed` bytes, its bytes kept: a larger
/// block when the slot's capacity is short. Throws std::bad_alloc.
[[nodiscard]] kernel_word::Word reserve_text(kernel_word::Word slot, std::uint64_t needed);

/// Text slot `slot` holding `text` (which is not the slot's own bytes).
[[nodiscard]] kernel_word::Word assign_text(kernel_word::Word slot, std::string_view text);

/// Whether the kernel holds a container register of this type in a
/// StaticKernelDynamicContainer: a queue, dynamic array, associative array
/// (integral or string keys) or sparse array of packed or scalar elements
/// of at most 64 bits or of strings.
[[nodiscard]] bool native_dynamic_type(const ContainerType& type) noexcept;
/// Why `type` is no container the kernel holds: nullptr when it is one.
[[nodiscard]] const char* native_dynamic_type_reason(
    const ContainerType& type) noexcept;

/// Whether `type` is a flat unpacked aggregate (dyn_layout_aggregate): a
/// structure value, or a dense fixed-size array of structures, whose
/// members are packed or scalar values of at most 64 bits or such
/// structures (IEEE 1800-2023 7.2, 7.4). The kernel holds its elements'
/// leaves: leaf l of element e at ordinal e * leaves + l, an element's
/// leaves in member order, a nested structure's in its place.
[[nodiscard]] bool flat_aggregate_type(const ContainerType& type) noexcept;
/// The leaves of each element of flat aggregate `type`.
[[nodiscard]] std::uint32_t aggregate_leaf_count(const ContainerType& type) noexcept;
/// The elements of flat aggregate `type`: one for a structure value.
[[nodiscard]] std::uint64_t aggregate_element_count(const ContainerType& type) noexcept;
/// The widths of an element's leaves of flat aggregate `type`, in leaf
/// order: the fields of its packed bridge (container_signal_bridge_width),
/// the first leaf leftmost.
[[nodiscard]] std::vector<std::uint32_t> aggregate_leaf_widths(const ContainerType& type);
/// Visits each leaf value of flat aggregate value `value` in leaf order.
template <typename Value, typename Visit>
void for_each_aggregate_leaf(Value& value, Visit&& visit)
{
    for (auto& element : value.nested_elements) {
        if (element.type.element_kind == ContainerElementKind::Aggregate) {
            for_each_aggregate_leaf(element, visit);
        } else {
            visit(element);
        }
    }
}
/// The types of an element's leaves of flat aggregate `type`, in leaf order.
[[nodiscard]] std::vector<const ContainerType*> aggregate_leaf_types(const ContainerType& type);
/// The field widths of an element of an object bridged to a packed slot: a
/// flat aggregate's leaves, else the one packed element.
[[nodiscard]] std::vector<std::uint32_t> bridged_leaf_widths(const ContainerType& type);
/// The bits of flat aggregate `type`'s packed bridge: every element's leaves.
[[nodiscard]] std::uint64_t aggregate_bridge_bits(const ContainerType& type);
/// The leaf member path `members` names in an element of flat aggregate
/// `type`: its ordinal among the element's leaves and its type.
struct AggregateLeaf {
    std::uint32_t offset { };
    const ContainerType* type { };
};
[[nodiscard]] std::optional<AggregateLeaf> aggregate_leaf(const ContainerType& type,
    std::span<const std::uint32_t> members) noexcept;
/// The leaves of the aggregate member `members` selects in an element of
/// flat aggregate `type` (the whole element when `members` is empty): its
/// first leaf, its leaf count and its type; none when it selects a leaf.
struct AggregateRange {
    std::uint32_t offset { };
    std::uint32_t leaves { };
    const ContainerType* type { };
};
[[nodiscard]] std::optional<AggregateRange> aggregate_range(const ContainerType& type,
    std::span<const std::uint32_t> members) noexcept;

/// An empty header for a container of `type` (native_dynamic_type).
[[nodiscard]] StaticKernelDynamicContainer make_dynamic_header(
    const ContainerType& type);

/// Room for `needed` elements, the elements moved to slots 0 .. size - 1
/// (head 0). Throws std::bad_alloc when the memory is not available.
void grow_dynamic(StaticKernelDynamicContainer& container, std::uint64_t needed);

/// Element `ordinal` (< size).
[[nodiscard]] inline kernel_word::Word dynamic_element(
    const StaticKernelDynamicContainer& container, const std::uint64_t ordinal) noexcept
{
    const auto slot = (container.head + ordinal) & (container.capacity - 1U);
    return { container.data[2U * slot], container.data[2U * slot + 1U] };
}

inline void set_dynamic_element(StaticKernelDynamicContainer& container,
    const std::uint64_t ordinal, const kernel_word::Word value) noexcept
{
    const auto slot = (container.head + ordinal) & (container.capacity - 1U);
    container.data[2U * slot] = value.a;
    container.data[2U * slot + 1U] = value.b;
}

/// The sparse array `target` takes the storage layout (dense image or
/// stored elements) and capacity of `source`, of its type, for a whole
/// copy; its elements are not copied. Throws std::bad_alloc.
void reshape_dynamic(StaticKernelDynamicContainer& target,
    const StaticKernelDynamicContainer& source);

/// The reference's value of the container (type `type`).
[[nodiscard]] ContainerValue dynamic_value(
    const StaticKernelDynamicContainer& container, const ContainerType& type);

/// Replaces the elements with those of `value` (of the container's type).
void assign_dynamic_value(StaticKernelDynamicContainer& container,
    const ContainerValue& value);

/// The kernel's headers: stable addresses for StaticKernelContainerInfo,
/// element blocks freed with the kernel.
class DynamicHeaders {
public:
    DynamicHeaders() = default;
    DynamicHeaders(const DynamicHeaders&) = delete;
    DynamicHeaders& operator=(const DynamicHeaders&) = delete;
    ~DynamicHeaders();

    [[nodiscard]] std::uint32_t add(StaticKernelDynamicContainer header);
    /// Removes the headers from `count` on (a member that did not compile
    /// gives its tentative containers back).
    void truncate(std::size_t count) noexcept;
    [[nodiscard]] std::size_t size() const noexcept
    {
        return headers_.size();
    }
    [[nodiscard]] StaticKernelDynamicContainer& operator[](const std::size_t index) noexcept
    {
        return headers_[index];
    }
    [[nodiscard]] const StaticKernelDynamicContainer& operator[](
        const std::size_t index) const noexcept
    {
        return headers_[index];
    }

private:
    std::deque<StaticKernelDynamicContainer> headers_;
};

/// Executes KOp::str `inst` on the bytecode tier on the member's string
/// registers `strings` (IEEE 1800-2023 6.16, 11.4). Returns DynFailure::none,
/// or the reference's error to report.
[[nodiscard]] DynFailure execute_str(const KInst& inst, const CompiledBody& body,
    KernelString* strings, kernel_word::Word* registers);

/// KOp::str method: the reference's execute_string_method of the operator
/// in `operation` (KInst::imm_a, with the integer operands' widths) on
/// `source`, `argument` and the integer words `first` and `second` (none
/// when unknown, or when `flags` names no such operand). Returns the 64-bit
/// result (flags str_method_scalar); a string result goes to `destination`.
/// Throws the reference's errors.
[[nodiscard]] std::uint64_t execute_str_method(std::uint64_t operation, std::uint8_t flags,
    KernelString& source, std::string_view argument, kernel_word::Word first,
    kernel_word::Word second, KernelString* destination,
    std::uint64_t resolution_femtoseconds = 0U);

/// KOp::str format: appends to `destination` the reference's
/// execute_string_format of the format record `record`
/// (string_format_method) on `value` (unused when the record reads none)
/// or on `argument`, a dynamic field form with the field width
/// `field_width` (checked_vhdl_textio_field_width). Throws the reference's
/// errors.
void append_str_format(const std::uint64_t* record, KernelString& destination,
    const PackedLogic4& value, std::string_view argument, const PackedLogic4& field_width,
    std::uint64_t tick, const SystemVerilogTimeFormat& time_format);

/// Executes KOp::scalar `inst` on the bytecode tier with the reference's
/// scalar routines. Returns the reference's failure message, or nothing.
[[nodiscard]] std::optional<std::string> execute_scalar(const KInst& inst,
    kernel_word::Word* registers);

/// The message of a KOp::scalar failure with the reference's error code
/// `error` (as the behavioral tier reports it).
[[nodiscard]] std::string scalar_failure_message(const KInst& inst, std::uint32_t error);

/// The predicate graph imm_b of a KOp::dyn instruction (dyn_graph) as the
/// reference's nodes.
[[nodiscard]] std::vector<ContainerPredicateNode> dyn_graph_nodes(
    const CompiledBody& body, std::uint64_t imm);

/// Executes a bit-stream KOp::dyn `inst` (dyn_stream) on the bytecode tier
/// through the reference's ContainerBitstream: `bits` (type `bits_type`)
/// is aux and `other` container z. Errors are the reference's
/// InterpreterError of `process` at `instruction`.
void execute_dyn_stream(const KInst& inst, StaticKernelDynamicContainer& bits,
    const ContainerType& bits_type, StaticKernelDynamicContainer* other,
    const ContainerType* other_type, kernel_word::Word* registers, ProcessId process,
    InstructionIndex instruction);

/// Executes a whole-array KOp::dyn `inst` (dyn_whole) on the bytecode tier
/// through the reference's routines: `container` (type `type`) is aux,
/// `other` container z and `third` container y; `random` draws the
/// process's next random number (shuffle).
void execute_dyn_whole(const KInst& inst, const CompiledBody& body,
    StaticKernelDynamicContainer& container, const ContainerType& type,
    const StaticKernelDynamicContainer* other, const ContainerType* other_type,
    const StaticKernelDynamicContainer* third, kernel_word::Word* registers,
    const std::function<std::uint32_t()>& random);

/// The reference's message for a DynFailure.
[[nodiscard]] const char* dyn_fail_message(DynFailure failure) noexcept;

/// Executes KOp::dyn `inst` on the bytecode tier: `container` is the
/// instruction's container, `other` its second (container z) or null, and
/// `strings` the member's string registers when z is its string file
/// (dyn_uses_string_file).
/// Returns DynFailure::none, or the reference's error to report.
[[nodiscard]] DynFailure execute_dyn(const KInst& inst,
    StaticKernelDynamicContainer& container,
    const StaticKernelDynamicContainer* other, kernel_word::Word* registers,
    KernelString* strings);
/// DynOp::aggregate_element_read / aggregate_element_write: `box` is the
/// structure register's container, `array` the indexed one; `nodes` the
/// instruction's leaf nodes (dyn_graph_node_words words each: width | the
/// zero-initial flag << 32).
[[nodiscard]] DynFailure execute_dyn_aggregate_element(const KInst& inst,
    StaticKernelDynamicContainer& array, StaticKernelDynamicContainer& box,
    const kernel_word::Word* registers, const std::uint64_t* nodes);

} // namespace fsim::runtime::simir::static_kernel_detail
