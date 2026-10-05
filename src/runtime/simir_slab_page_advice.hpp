// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>

#if defined(__linux__)
#include <cerrno>
#include <cstdint>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace fsim::runtime::simir {

// Best-effort advice for a reserved process slab. Only whole pages wholly
// inside this allocation are included: allocator metadata and neighboring
// allocations are never advised. No global huge-page setting is changed.
// Failure or a platform without this advice leaves ordinary allocation intact.
inline void advise_slab_large_pages(void* memory, const std::size_t bytes) noexcept
{
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    constexpr std::size_t minimum_bytes = 2U * 1024U * 1024U;
    if (memory == nullptr || bytes < minimum_bytes) {
        return;
    }
    const auto saved_errno = errno;
    const auto configured_page_size = ::sysconf(_SC_PAGESIZE);
    if (configured_page_size > 0) {
        const auto page_size = static_cast<std::uintptr_t>(configured_page_size);
        const auto address = reinterpret_cast<std::uintptr_t>(memory);
        if (bytes <= std::numeric_limits<std::uintptr_t>::max() - address) {
            const auto end = address + bytes;
            const auto remainder = address % page_size;
            const auto prefix = remainder == 0U ? 0U : page_size - remainder;
            if (prefix < bytes) {
                const auto first = address + prefix;
                const auto last = end - end % page_size;
                if (first < last) {
                    (void)::madvise(reinterpret_cast<void*>(first),
                        static_cast<std::size_t>(last - first), MADV_HUGEPAGE);
                }
            }
        }
    }
    errno = saved_errno;
#else
    (void)memory;
    (void)bytes;
#endif
}

} // namespace fsim::runtime::simir
