// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <chrono>
#include <optional>
#include <time.h>

namespace fsim::diagnostic {

using ThreadCpuTime = std::chrono::nanoseconds;

inline std::optional<ThreadCpuTime> thread_cpu_now() noexcept
{
#if defined(__linux__) && defined(CLOCK_THREAD_CPUTIME_ID)
    timespec value { };
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) {
        return std::nullopt;
    }
    return std::chrono::seconds { value.tv_sec }
        + std::chrono::nanoseconds { value.tv_nsec };
#else
    return std::nullopt;
#endif
}

inline std::optional<ThreadCpuTime> thread_cpu_elapsed(
    const std::optional<ThreadCpuTime> begin,
    const std::optional<ThreadCpuTime> end) noexcept
{
    if (!begin || !end || *end < *begin) {
        return std::nullopt;
    }
    return *end - *begin;
}

class ThreadCpuAccumulation {
public:
    ThreadCpuAccumulation(std::optional<ThreadCpuTime>& total,
        const bool enabled) noexcept
        : total_(total)
        , enabled_(enabled)
        , begin_(enabled ? thread_cpu_now() : std::nullopt)
    {
    }

    ~ThreadCpuAccumulation()
    {
        if (!enabled_) {
            return;
        }
        const auto elapsed = thread_cpu_elapsed(begin_, thread_cpu_now());
        if (!total_ || !elapsed) {
            total_.reset();
            return;
        }
        *total_ += *elapsed;
    }

private:
    std::optional<ThreadCpuTime>& total_;
    bool enabled_;
    std::optional<ThreadCpuTime> begin_;
};

} // namespace fsim::diagnostic
