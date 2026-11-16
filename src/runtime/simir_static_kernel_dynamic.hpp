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

namespace fsim::runtime::simir::static_kernel_detail {

/// Whether the kernel holds a container register of this type in a
/// StaticKernelDynamicContainer: a queue or dynamic array of packed or
/// scalar elements of at most 64 bits.
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

/// The reference's message for a DynFailure.
[[nodiscard]] const char* dyn_fail_message(DynFailure failure) noexcept;

/// Executes KOp::dyn `inst` on the bytecode tier: `container` is the
/// instruction's container, `other` its second (container z) or null.
/// Returns DynFailure::none, or the reference's error to report.
[[nodiscard]] DynFailure execute_dyn(const KInst& inst,
    StaticKernelDynamicContainer& container,
    const StaticKernelDynamicContainer* other, kernel_word::Word* registers);

} // namespace fsim::runtime::simir::static_kernel_detail
