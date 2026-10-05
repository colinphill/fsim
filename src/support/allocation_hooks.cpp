// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/allocation_hooks.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace fsim::support {

#if defined(FSIM_ALLOCATION_HOOKS)
namespace {

    std::atomic<AllocateHook> allocate_hook { nullptr };
    std::atomic<DeallocateHook> deallocate_hook { nullptr };

} // namespace
#endif

void set_allocation_hooks(
    const AllocateHook allocate, const DeallocateHook deallocate) noexcept
{
#if defined(FSIM_ALLOCATION_HOOKS)
    allocate_hook.store(allocate, std::memory_order_release);
    deallocate_hook.store(deallocate, std::memory_order_release);
#else
    (void)allocate;
    (void)deallocate;
#endif
}

} // namespace fsim::support

#if defined(FSIM_ALLOCATION_HOOKS)

// The fsim DLLs link fsim_base ahead of the C++ runtime, so they bind these
// rather than libc++'s. Without hooks they match libc++: malloc, the new
// handler, and free.
void* operator new(std::size_t size)
{
    if (const auto hook
        = fsim::support::allocate_hook.load(std::memory_order_acquire)) {
        return hook(size);
    }
    if (size == 0U) {
        size = 1U;
    }
    for (;;) {
        if (void* const allocation = std::malloc(size)) {
            return allocation;
        }
        const auto handler = std::get_new_handler();
        if (handler == nullptr) {
            throw std::bad_alloc { };
        }
        handler();
    }
}

void* operator new[](const std::size_t size)
{
    return ::operator new(size);
}

void* operator new(const std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return ::operator new(size);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* const pointer) noexcept
{
    if (const auto hook
        = fsim::support::deallocate_hook.load(std::memory_order_acquire)) {
        hook(pointer);
        return;
    }
    std::free(pointer);
}

void operator delete[](void* const pointer) noexcept
{
    ::operator delete(pointer);
}

void operator delete(void* const pointer, std::size_t) noexcept
{
    ::operator delete(pointer);
}

void operator delete[](void* const pointer, std::size_t) noexcept
{
    ::operator delete(pointer);
}

void operator delete(void* const pointer, const std::nothrow_t&) noexcept
{
    ::operator delete(pointer);
}

void operator delete[](void* const pointer, const std::nothrow_t&) noexcept
{
    ::operator delete(pointer);
}

#endif
