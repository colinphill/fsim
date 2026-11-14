// SPDX-License-Identifier: Apache-2.0
#pragma once

// The integer identities of SimIR registers, objects, signals, processes and
// instructions, for headers that name them without needing the IR itself.

#include <cstdint>

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

} // namespace fsim::runtime::simir
