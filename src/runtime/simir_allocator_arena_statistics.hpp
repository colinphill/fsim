// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <cstdio>

#if defined(__GLIBC__)
#if __GLIBC_PREREQ(2, 33)
#include <malloc.h>
#define FSIM_HAS_GLIBC_MALLINFO2 1
#endif
#endif

namespace fsim::runtime::simir {

inline void report_prepared_allocator_arena_statistics(
    const std::uint64_t generation) noexcept
{
#if defined(FSIM_HAS_GLIBC_MALLINFO2)
    // Sample before census containers are allocated. These glibc chunk-space
    // counters include allocator bookkeeping and fragmentation; they are
    // neither requested live payload bytes nor process RSS.
    const auto statistics = ::mallinfo2();
    std::fprintf(stderr,
        "[fsim allocator-arena-census] generation=%llu available=1 "
        "scope=glibc-allocator-bookkeeping checkpoint=prepared-before-census "
        "arena_extent_bytes=%zu arena_in_use_chunk_bytes=%zu "
        "arena_free_chunk_bytes=%zu mmap_extent_bytes=%zu mmap_regions=%zu "
        "excluded=requested-payload-accounting,non-glibc-allocators,process-RSS\n",
        static_cast<unsigned long long>(generation), statistics.arena,
        statistics.uordblks, statistics.fordblks, statistics.hblkhd,
        statistics.hblks);
#else
    std::fprintf(stderr,
        "[fsim allocator-arena-census] generation=%llu available=0 "
        "scope=allocator-bookkeeping checkpoint=prepared-before-census "
        "reason=glibc-mallinfo2-unavailable\n",
        static_cast<unsigned long long>(generation));
#endif
}

} // namespace fsim::runtime::simir

#if defined(FSIM_HAS_GLIBC_MALLINFO2)
#undef FSIM_HAS_GLIBC_MALLINFO2
#endif
