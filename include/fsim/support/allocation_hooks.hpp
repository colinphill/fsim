// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

namespace fsim::support {

using AllocateHook = void* (*)(std::size_t size);
using DeallocateHook = void (*)(void* pointer) noexcept;

// Routes the fsim libraries' operator new and delete, other than the aligned
// forms, through these functions; null restores malloc and free. A test or
// profiler that replaces operator new points them at its replacement. Windows
// binds operator new separately in each module, so only these reach the fsim
// DLLs there. Elsewhere the replacement already covers every module and this
// does nothing. The hooks must not call the fsim libraries' operator new, and
// what they return must be freeable with std::free.
void set_allocation_hooks(
    AllocateHook allocate, DeallocateHook deallocate) noexcept;

} // namespace fsim::support
