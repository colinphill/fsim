// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/parallel_for.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void test_serial_outside_a_scope()
{
    assert(fsim::support::ParallelJobsScope::jobs() == 1U);
    assert(fsim::support::parallel_workers(1'000'000U) == 1U);
    std::vector<std::size_t> order;
    fsim::support::parallel_for(100U, [&](const std::size_t worker,
                                         const std::size_t index) {
        assert(worker == 0U);
        order.push_back(index);
    });
    assert(order.size() == 100U);
    assert(std::ranges::is_sorted(order));
}

void test_scopes_nest_and_restore()
{
    {
        const fsim::support::ParallelJobsScope outer { 4U };
        assert(fsim::support::ParallelJobsScope::jobs() == 4U);
        {
            const fsim::support::ParallelJobsScope inner { 0U };
            assert(fsim::support::ParallelJobsScope::jobs() >= 1U);
        }
        assert(fsim::support::ParallelJobsScope::jobs() == 4U);
        // One worker for every grain items, at most the job count.
        assert(fsim::support::parallel_workers(100U, 64U) == 1U);
        assert(fsim::support::parallel_workers(200U, 64U) == 3U);
        assert(fsim::support::parallel_workers(100'000U, 64U) == 4U);
    }
    assert(fsim::support::ParallelJobsScope::jobs() == 1U);
}

void test_every_index_once_on_its_worker()
{
    const fsim::support::ParallelJobsScope jobs { 4U };
    constexpr std::size_t count = 100'000U;
    const auto workers = fsim::support::parallel_workers(count);
    assert(workers == 4U);
    std::vector<std::uint32_t> visits(count, 0U);
    std::vector<std::size_t> per_worker(workers, 0U);
    fsim::support::parallel_for(count, [&](const std::size_t worker,
                                          const std::size_t index) {
        assert(worker < workers);
        // Work started on a worker thread runs serially there.
        assert(worker == 0U || fsim::support::ParallelJobsScope::jobs() == 1U);
        ++visits[index];
        ++per_worker[worker];
    });
    assert(std::ranges::all_of(visits, [](const std::uint32_t visit) {
        return visit == 1U;
    }));
    std::size_t total { };
    for (const auto counted : per_worker) {
        total += counted;
    }
    assert(total == count);
}

void test_lowest_failing_index_is_rethrown()
{
    const fsim::support::ParallelJobsScope jobs { 4U };
    try {
        fsim::support::parallel_for(50'000U, [](std::size_t,
                                               const std::size_t index) {
            if (index == 40'001U || index == 777U || index == 12'345U) {
                throw std::runtime_error { std::to_string(index) };
            }
        });
        assert(false);
    } catch (const std::runtime_error& error) {
        assert(std::string { error.what() } == "777");
    }
}

void test_parallel_sort_matches_std_sort()
{
    std::vector<std::uint32_t> values(200'003U);
    std::uint32_t state = 12345U;
    for (auto& value : values) {
        state = state * 1103515245U + 12345U;
        value = (state >> 8U) % 5000U;
    }
    auto expected = values;
    std::ranges::sort(expected);
    for (const std::uint32_t job_count : { 1U, 2U, 3U, 8U }) {
        const fsim::support::ParallelJobsScope jobs { job_count };
        auto sorted = values;
        fsim::support::parallel_sort(sorted.begin(), sorted.end());
        assert(sorted == expected);
    }
    std::vector<std::string> names;
    for (std::size_t index = 0U; index < 20'000U; ++index) {
        names.push_back("top.u" + std::to_string((index * 7919U) % 3001U)
            + ".sig");
    }
    auto expected_names = names;
    std::ranges::sort(expected_names);
    const fsim::support::ParallelJobsScope jobs { 5U };
    fsim::support::parallel_sort(names.begin(), names.end(), std::less<> { },
        512U);
    assert(names == expected_names);
}

} // namespace

int main()
{
    test_serial_outside_a_scope();
    test_scopes_nest_and_restore();
    test_every_index_once_on_its_worker();
    test_lowest_failing_index_is_rethrown();
    test_parallel_sort_matches_std_sort();
    return 0;
}
