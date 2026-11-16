// SPDX-License-Identifier: Apache-2.0
#pragma once

// The integer identities of SimIR registers, objects, signals, processes and
// instructions, for headers that name them without needing the IR itself.

#include <cstddef>
#include <cstdint>
#include <span>

namespace fsim::runtime::simir {

using RegisterId = std::uint32_t;
using StringRegisterId = std::uint32_t;
using StringObjectId = std::uint32_t;
using ContainerRegisterId = std::uint32_t;
using ContainerObjectId = std::uint32_t;
using FileHandle = std::uint32_t;
using SignalId = std::uint32_t;
using ProcessId = std::uint32_t;
using InstructionIndex = std::uint32_t;

// What one actual of a call stands for. An actual that is not an argument
// of the called subprogram is a hidden one the call adds: the string a
// function's string result comes back in, or the list of classes whose
// constructor chains the calling process runs after a lowered registry
// create. A call that has no hidden actual keeps an empty list of roles,
// which means every actual is an argument.
enum class CallActualRole : std::uint8_t {
    argument,
    string_result,
    lowered_constructors,
    last = lowered_constructors,
};

// The role of actual `index` in a call's list of roles.
[[nodiscard]] constexpr CallActualRole call_actual_role(
    const std::span<const CallActualRole> roles,
    const std::size_t index) noexcept
{
    return index < roles.size() ? roles[index] : CallActualRole::argument;
}

// Whether the last of `count` actuals has `role`.
[[nodiscard]] constexpr bool last_call_actual_is(
    const std::span<const CallActualRole> roles, const std::size_t count,
    const CallActualRole role) noexcept
{
    return count != 0U && call_actual_role(roles, count - 1U) == role;
}

} // namespace fsim::runtime::simir
