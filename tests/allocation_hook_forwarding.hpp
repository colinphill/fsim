// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/support/allocation_hooks.hpp"

#include <cstddef>
#include <new>

// Included by a test that replaces operator new and delete. On Windows each
// fsim DLL binds operator new when it is linked, so this routes the DLLs'
// allocations through the test's replacement; elsewhere the replacement
// already covers them.
namespace fsim::tests::allocation_hook_forwarding {

inline void* allocate(const std::size_t size)
{
    return ::operator new(size);
}

inline void deallocate(void* const pointer) noexcept
{
    ::operator delete(pointer);
}

inline const bool installed = [] {
    fsim::support::set_allocation_hooks(allocate, deallocate);
    return true;
}();

} // namespace fsim::tests::allocation_hook_forwarding
