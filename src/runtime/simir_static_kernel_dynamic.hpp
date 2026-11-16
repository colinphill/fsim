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
#include <string>
#include <string_view>

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
    std::string* strings, kernel_word::Word* registers);

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
    std::string* strings);

} // namespace fsim::runtime::simir::static_kernel_detail
