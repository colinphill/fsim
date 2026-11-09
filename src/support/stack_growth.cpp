// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/stack_growth.hpp"

#include <new>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace fsim::support::stack_growth_detail {

thread_local StackBounds current_bounds;

bool has_room() noexcept
{
    if (!current_bounds.known) {
        discover_bounds();
    }
    char marker;
    const auto here = reinterpret_cast<std::uintptr_t>(&marker);
    return here - current_bounds.low >= kStackRedZone;
}

#if defined(_WIN32)

void discover_bounds() noexcept
{
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    current_bounds.low = static_cast<std::uintptr_t>(low);
    current_bounds.known = true;
}

namespace {

struct Launch {
    void (*entry)(void*);
    void* argument;
    void* back;
};

// A thread that was turned into a fiber here is turned back when it ends.
struct FiberConversion {
    bool converted { false };
    ~FiberConversion()
    {
        if (converted) {
            ConvertFiberToThread();
        }
    }
};

thread_local FiberConversion fiber_conversion;

VOID CALLBACK segment_main(PVOID raw)
{
    auto* const launch = static_cast<Launch*>(raw);
    launch->entry(launch->argument);
    // A fiber function must not return: switch back to the caller.
    SwitchToFiber(launch->back);
}

} // namespace

void run_on_segment(void (*entry)(void*), void* argument)
{
    if (!IsThreadAFiber()) {
        if (ConvertThreadToFiberEx(nullptr, 0) == nullptr) {
            throw std::bad_alloc { };
        }
        fiber_conversion.converted = true;
    }
    Launch launch { entry, argument, GetCurrentFiber() };
    void* const fiber = CreateFiberEx(
        0, kStackSegmentBytes, 0, segment_main, &launch);
    if (fiber == nullptr) {
        throw std::bad_alloc { };
    }
    const auto saved = current_bounds;
    // The bounds of the fiber's stack are read again on first use.
    current_bounds.known = false;
    SwitchToFiber(fiber);
    current_bounds = saved;
    DeleteFiber(fiber);
}

#else

void discover_bounds() noexcept
{
    current_bounds.known = true;
    current_bounds.low = 0U;
    // The only stack the thread reaches this code on is its own; segments
    // set their bounds when they are entered.
#if defined(__APPLE__)
    const auto self = pthread_self();
    const auto high = reinterpret_cast<std::uintptr_t>(
        pthread_get_stackaddr_np(self));
    current_bounds.low = high - pthread_get_stacksize_np(self);
#else
    pthread_attr_t attributes;
    if (pthread_getattr_np(pthread_self(), &attributes) != 0) {
        return;
    }
    void* address = nullptr;
    std::size_t size = 0U;
    if (pthread_attr_getstack(&attributes, &address, &size) == 0) {
        current_bounds.low = reinterpret_cast<std::uintptr_t>(address);
    }
    pthread_attr_destroy(&attributes);
#endif
}

namespace {

struct Launch {
    void (*entry)(void*);
    void* argument;
};

// The launch record for the context that is about to start; read once at its
// first instruction.
thread_local Launch* starting = nullptr;

// One released segment is kept per thread for the next switch.
struct SegmentCache {
    void* base { nullptr };
    ~SegmentCache()
    {
        if (base != nullptr) {
            munmap(base, kStackSegmentBytes);
        }
    }
};

thread_local SegmentCache segment_cache;

[[nodiscard]] std::size_t guard_bytes() noexcept
{
    const auto page = sysconf(_SC_PAGESIZE);
    return page > 0 ? static_cast<std::size_t>(page) : std::size_t { 4096 };
}

void segment_main()
{
    const auto* const launch = starting;
    launch->entry(launch->argument);
}

} // namespace

void run_on_segment(void (*entry)(void*), void* argument)
{
    const auto guard = guard_bytes();
    void* base = std::exchange(segment_cache.base, nullptr);
    if (base == nullptr) {
        base = mmap(nullptr, kStackSegmentBytes, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_STACK, -1, 0);
        if (base == MAP_FAILED) {
            throw std::bad_alloc { };
        }
        // The lowest page is a guard: an overrun faults instead of
        // corrupting memory.
        static_cast<void>(mprotect(base, guard, PROT_NONE));
    }
    const auto release = [&] {
        if (segment_cache.base == nullptr) {
            segment_cache.base = base;
        } else {
            munmap(base, kStackSegmentBytes);
        }
    };
    ucontext_t caller;
    ucontext_t segment;
    if (getcontext(&segment) != 0) {
        release();
        throw std::bad_alloc { };
    }
    segment.uc_stack.ss_sp = static_cast<char*>(base) + guard;
    segment.uc_stack.ss_size = kStackSegmentBytes - guard;
    segment.uc_link = &caller;
    makecontext(&segment, segment_main, 0);
    Launch launch { entry, argument };
    starting = &launch;
    const auto saved = current_bounds;
    current_bounds.low = reinterpret_cast<std::uintptr_t>(base) + guard;
    current_bounds.known = true;
    const int switched = swapcontext(&caller, &segment);
    current_bounds = saved;
    release();
    if (switched != 0) {
        throw std::bad_alloc { };
    }
}

#endif

} // namespace fsim::support::stack_growth_detail
