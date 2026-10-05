// SPDX-License-Identifier: Apache-2.0
#include "runtime_direct_artifact_rows_test_support.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <string>

namespace {

std::atomic<std::ptrdiff_t> allocation_failure_index { -1 };
std::atomic<std::size_t> allocation_call_count { };

[[nodiscard]] bool should_fail_allocation() noexcept
{
    const auto failure_index = allocation_failure_index.load();
    if (failure_index < 0) {
        return false;
    }
    const auto call_index = allocation_call_count.fetch_add(1U);
    return call_index == static_cast<std::size_t>(failure_index);
}

} // namespace

void* operator new(const std::size_t size)
{
    if (should_fail_allocation()) {
        throw std::bad_alloc { };
    }
    if (auto* const allocation = std::malloc(size == 0U ? 1U : size)) {
        return allocation;
    }
    throw std::bad_alloc { };
}

void* operator new[](const std::size_t size) { return ::operator new(size); }

void operator delete(void* const allocation) noexcept { std::free(allocation); }

void operator delete[](void* const allocation) noexcept
{
    std::free(allocation);
}

namespace {

using namespace fsim::app::direct_artifact_rows_test;

void test_lazy_process_materialization_retries_after_allocation_failure()
{
    const auto source_bytes = encode_source_design();
    assert(source_bytes.has_value());
    bool saw_allocation_failure = false;
    bool reached_successful_materialization = false;

    for (std::size_t fail_after = 0U; fail_after < 128U; ++fail_after) {
        auto design = decode_design(*source_bytes);
        assert_view_rows(*design);
        const auto original_table = fsim::elaboration::detail::ElaboratedDesignProcessAccess::process_table(
            *design);
        allocation_call_count.store(0U);
        allocation_failure_index.store(static_cast<std::ptrdiff_t>(fail_after));
        bool failed = false;
        try {
            (void)design->processes();
        } catch (const std::bad_alloc&) {
            failed = true;
        }
        const auto calls = allocation_call_count.load();
        allocation_failure_index.store(-1);

        if (failed) {
            saw_allocation_failure = true;
            assert(calls == fail_after + 1U);
            assert(
                fsim::elaboration::detail::ElaboratedDesignProcessAccess::row_backed(
                    *design));
            assert(design->process_count() == 3U);
            assert(
                fsim::elaboration::detail::ElaboratedDesignProcessAccess::process_table(
                    *design)
                == original_table);
            const auto& retried_processes = design->processes();
            assert_facade_rows(retried_processes);
            assert(
                !fsim::elaboration::detail::ElaboratedDesignProcessAccess::row_backed(
                    *design));
            continue;
        }

        assert(calls <= fail_after);
        assert_facade_rows(design->processes());
        assert(
            !fsim::elaboration::detail::ElaboratedDesignProcessAccess::row_backed(
                *design));
        reached_successful_materialization = true;
        break;
    }

    assert(saw_allocation_failure);
    assert(reached_successful_materialization);
}

} // namespace

int main()
{
    test_lazy_process_materialization_retries_after_allocation_failure();
}
