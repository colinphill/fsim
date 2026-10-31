// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <thread>
#include <vector>

namespace fsim::support {

/// The job count for parallel work started on this thread: `--jobs`, set
/// around elaboration, artifact publish and kernel planning (0: every
/// hardware thread). Outside any scope, and on the worker threads themselves,
/// work runs serially.
class ParallelJobsScope final {
public:
    explicit ParallelJobsScope(const std::uint32_t jobs) noexcept
        : previous_ { current_ }
    {
        current_ = jobs != 0U
            ? jobs
            : std::max(1U, std::thread::hardware_concurrency());
    }
    ~ParallelJobsScope() { current_ = previous_; }
    ParallelJobsScope(const ParallelJobsScope&) = delete;
    ParallelJobsScope& operator=(const ParallelJobsScope&) = delete;

    /// The innermost scope's job count on this thread (1 outside any).
    [[nodiscard]] static std::uint32_t jobs() noexcept { return current_; }

private:
    std::uint32_t previous_;
    static inline thread_local std::uint32_t current_ { 1U };
};

/// The workers parallel_for uses for `count` items: at most the job count,
/// and one for every `grain` items.
[[nodiscard]] inline std::size_t parallel_workers(
    const std::size_t count, const std::size_t grain = 64U) noexcept
{
    return std::max<std::size_t>(1U,
        std::min<std::size_t>(ParallelJobsScope::jobs(),
            count / std::max<std::size_t>(grain, 1U)));
}

/// Calls `body(worker, index)` for every index below `count`, on
/// parallel_workers(count, grain) threads (the caller is worker 0). Each
/// call must write only state of its own index or of its worker, so results
/// do not depend on the schedule; callers merge them in index order. If calls
/// throw, the exception of the lowest failing index is rethrown, as a serial
/// loop would throw it.
template <typename Body>
void parallel_for(const std::size_t count, Body&& body,
    const std::size_t grain = 64U)
{
    const auto workers = parallel_workers(count, grain);
    if (workers <= 1U) {
        for (std::size_t index = 0U; index < count; ++index) {
            body(std::size_t { 0 }, index);
        }
        return;
    }
    // Ascending chunks, taken in order: the first failure of the lowest
    // failing chunk is the lowest failing index.
    const auto chunk_size = std::max<std::size_t>(1U, count / (workers * 8U));
    const auto chunks = (count + chunk_size - 1U) / chunk_size;
    std::vector<std::exception_ptr> failures(chunks);
    std::atomic_size_t next { 0U };
    const auto work = [&](const std::size_t worker) {
        for (auto chunk = next.fetch_add(1U); chunk < chunks;
             chunk = next.fetch_add(1U)) {
            const auto end = std::min(count, (chunk + 1U) * chunk_size);
            try {
                for (auto index = chunk * chunk_size; index < end; ++index) {
                    body(worker, index);
                }
            } catch (...) {
                failures[chunk] = std::current_exception();
            }
        }
    };
    {
        std::vector<std::jthread> threads;
        threads.reserve(workers - 1U);
        for (std::size_t worker = 1U; worker < workers; ++worker) {
            threads.emplace_back(work, worker);
        }
        work(0U);
    }
    for (const auto& failure : failures) {
        if (failure) {
            std::rethrow_exception(failure);
        }
    }
}

/// Sorts [first, last) as std::sort would, on the --jobs threads: ascending
/// parts sorted in parallel, then merged pairwise. Elements that compare
/// equal may end in a different relative order than std::sort leaves them.
template <typename RandomIt, typename Less = std::less<>>
void parallel_sort(const RandomIt first, const RandomIt last,
    Less less = { }, const std::size_t grain = 4096U)
{
    const auto count = static_cast<std::size_t>(last - first);
    const auto parts = parallel_workers(count, grain);
    if (parts <= 1U) {
        std::sort(first, last, less);
        return;
    }
    std::vector<RandomIt> bounds;
    bounds.reserve(parts + 1U);
    for (std::size_t part = 0U; part <= parts; ++part) {
        bounds.push_back(first
            + static_cast<std::ptrdiff_t>(count * part / parts));
    }
    parallel_for(parts, [&](std::size_t, const std::size_t part) {
        std::sort(bounds[part], bounds[part + 1U], less);
    }, 1U);
    for (std::size_t width = 1U; width < parts; width *= 2U) {
        const auto merges = (parts + 2U * width - 1U) / (2U * width);
        parallel_for(merges, [&](std::size_t, const std::size_t merge) {
            const auto low = merge * 2U * width;
            const auto middle = std::min(low + width, parts);
            const auto high = std::min(low + 2U * width, parts);
            if (middle < high) {
                std::inplace_merge(
                    bounds[low], bounds[middle], bounds[high], less);
            }
        }, 1U);
    }
}

} // namespace fsim::support
