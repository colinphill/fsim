// SPDX-License-Identifier: Apache-2.0
#include "allocation_profile.hpp"

#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#  include <malloc.h>
#endif

namespace {

[[nodiscard]] void* try_allocate(
    const std::size_t size, const std::size_t alignment) noexcept
{
    const auto allocation_size = size == 0U ? 1U : size;
    if (alignment == 0U || alignment <= alignof(std::max_align_t)) {
        return std::malloc(allocation_size);
    }
#if defined(_WIN32)
    return _aligned_malloc(allocation_size, alignment);
#else
    void* result { };
    if (::posix_memalign(&result, alignment, allocation_size) != 0) {
        return nullptr;
    }
    return result;
#endif
}

[[nodiscard]] void* allocate(
    const std::size_t size, const std::size_t alignment)
{
    const bool counted
        = fsim::app::allocation_profile::record_request(size);
    for (;;) {
        if (auto* result = try_allocate(size, alignment)) {
            return result;
        }
        const auto handler = std::get_new_handler();
        if (handler == nullptr) {
            fsim::app::allocation_profile::record_failure(counted);
            throw std::bad_alloc { };
        }
        try {
            handler();
        } catch (...) {
            fsim::app::allocation_profile::record_failure(counted);
            throw;
        }
    }
}

void deallocate(void* const pointer) noexcept
{
    std::free(pointer);
}

void deallocate_aligned(
    void* const pointer, const std::size_t alignment) noexcept
{
    if (pointer == nullptr) {
        return;
    }
    if (alignment <= alignof(std::max_align_t)) {
        deallocate(pointer);
        return;
    }
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

} // namespace

void* operator new(const std::size_t size)
{
    return allocate(size, 0U);
}

void* operator new[](const std::size_t size)
{
    return allocate(size, 0U);
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

void* operator new(const std::size_t size, const std::align_val_t alignment)
{
    return allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new[](const std::size_t size, const std::align_val_t alignment)
{
    return allocate(size, static_cast<std::size_t>(alignment));
}

void* operator new(const std::size_t size, const std::align_val_t alignment,
    const std::nothrow_t&) noexcept
{
    try {
        return ::operator new(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void* operator new[](const std::size_t size,
    const std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try {
        return ::operator new[](size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* const pointer) noexcept
{
    deallocate(pointer);
}

void operator delete[](void* const pointer) noexcept
{
    deallocate(pointer);
}

#if defined(__cpp_sized_deallocation)
void operator delete(void* const pointer, const std::size_t) noexcept
{
    deallocate(pointer);
}

void operator delete[](void* const pointer, const std::size_t) noexcept
{
    deallocate(pointer);
}
#endif

void operator delete(void* const pointer, const std::nothrow_t&) noexcept
{
    deallocate(pointer);
}

void operator delete[](void* const pointer, const std::nothrow_t&) noexcept
{
    deallocate(pointer);
}

void operator delete(void* const pointer,
    const std::align_val_t alignment) noexcept
{
    deallocate_aligned(pointer, static_cast<std::size_t>(alignment));
}

void operator delete[](void* const pointer,
    const std::align_val_t alignment) noexcept
{
    deallocate_aligned(pointer, static_cast<std::size_t>(alignment));
}

#if defined(__cpp_sized_deallocation)
void operator delete(void* const pointer, const std::size_t,
    const std::align_val_t alignment) noexcept
{
    deallocate_aligned(pointer, static_cast<std::size_t>(alignment));
}

void operator delete[](void* const pointer, const std::size_t,
    const std::align_val_t alignment) noexcept
{
    deallocate_aligned(pointer, static_cast<std::size_t>(alignment));
}
#endif

void operator delete(void* const pointer, const std::align_val_t alignment,
    const std::nothrow_t&) noexcept
{
    deallocate_aligned(pointer, static_cast<std::size_t>(alignment));
}

void operator delete[](void* const pointer, const std::align_val_t alignment,
    const std::nothrow_t&) noexcept
{
    deallocate_aligned(pointer, static_cast<std::size_t>(alignment));
}
