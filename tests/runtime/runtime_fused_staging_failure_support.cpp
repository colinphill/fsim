// SPDX-License-Identifier: Apache-2.0

#include "runtime_fused_staging_failure_support.hpp"

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {

thread_local std::ptrdiff_t allocations_until_failure { -1 };
thread_local bool allocation_failure_injected { };
thread_local std::size_t observed_allocation_count { };
thread_local bool count_allocations { };
thread_local void* allocation_failure_observer_context { };
thread_local fsim::tests::runtime::staging_failure_support::AllocationFailureObserver
    allocation_failure_observer { };

void note_allocation(const std::size_t) noexcept
{
    if (count_allocations)
        ++observed_allocation_count;
}

void fail_selected_allocation()
{
    if (allocations_until_failure < 0) {
        return;
    }
    if (allocations_until_failure == 0) {
        allocations_until_failure = -1;
        allocation_failure_injected = true;
        if (allocation_failure_observer != nullptr) {
            allocation_failure_observer(allocation_failure_observer_context);
        }
        throw std::bad_alloc { };
    }
    --allocations_until_failure;
}

} // namespace

namespace fsim::tests::runtime::staging_failure_support {

void arm_allocation_failure(const std::size_t successful_allocations)
{
    allocation_failure_injected = false;
    allocations_until_failure = static_cast<std::ptrdiff_t>(
        successful_allocations);
}

void set_allocation_failure_observer(
    void* const context, const AllocationFailureObserver observer) noexcept
{
    allocation_failure_observer_context = context;
    allocation_failure_observer = observer;
}

void clear_allocation_failure() noexcept
{
    allocations_until_failure = -1;
}

bool allocation_failure_was_injected() noexcept
{
    return allocation_failure_injected;
}

void begin_allocation_count() noexcept
{
    observed_allocation_count = 0U;
    count_allocations = true;
}

std::size_t end_allocation_count() noexcept
{
    count_allocations = false;
    return observed_allocation_count;
}

void require(const bool condition, const char* const message)
{
    if (!condition) {
        throw std::runtime_error { message };
    }
}

void print_observation(
    const char* const label, const Observation& observation)
{
    std::cerr << label << " signal=" << observation.signal << " drivers=";
    for (const auto& driver : observation.drivers) {
        std::cerr << '[' << driver << ']';
    }
    std::cerr << " publications=";
    for (const auto& [time, delta, value] : observation.publications) {
        std::cerr << '[' << time << ',' << delta << ',' << value << ']';
    }
    std::cerr << " semantic_publications=";
    for (std::size_t index = 0U;
         index < observation.publication_trace.count; ++index) {
        const auto& event = observation.publication_trace.records[index];
        std::cerr << '[' << static_cast<unsigned>(event.kind) << ','
                  << event.signal << ',' << event.time << ',' << event.delta
                  << ',' << event.systemverilog_round << ']';
    }
    std::cerr << " trace_overflow=" << observation.publication_trace.overflow
              << '\n';
}

void AllocationFailureWindow::trace(
    void* const context,
    const fsim::runtime::SchedulerTraceRecord& record) noexcept
{
    auto& window = *static_cast<AllocationFailureWindow*>(context);
    const bool publication
        = record.kind == fsim::runtime::SchedulerTraceKind::signal_transaction
        || record.kind == fsim::runtime::SchedulerTraceKind::signal_change;
    if (publication
        && (record.signal == window.observed_signals[0U]
            || record.signal == window.observed_signals[1U])) {
        auto& captured = window.publications;
        if (captured.count == captured.records.size()) {
            captured.overflow = true;
        } else {
            captured.records[captured.count++] = {
                record.kind, record.time, record.delta, record.phase,
                record.signal, record.systemverilog_round,
                record.systemverilog, record.end_of_time_slot
            };
        }
    }
    // Publication capture continues after allocation injection is disarmed,
    // so both normal completion and recovery retain their complete traces.
    if (window.closed || window.executor_called == nullptr
        || !*window.executor_called) {
        return;
    }
    const bool completed_batch
        = record.kind == fsim::runtime::SchedulerTraceKind::batch_end
        && record.count != 0U;
    const bool completed_task
        = record.kind == fsim::runtime::SchedulerTraceKind::task_end
        && record.count != 0U;
    if (completed_batch || completed_task) {
        clear_allocation_failure();
        window.closed = true;
    }
}

} // namespace fsim::tests::runtime::staging_failure_support

void* operator new(const std::size_t size)
{
    note_allocation(size);
    fail_selected_allocation();
    if (void* const allocation = std::malloc(size == 0U ? 1U : size)) {
        return allocation;
    }
    throw std::bad_alloc { };
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

void* operator new[](
    const std::size_t size, const std::nothrow_t&) noexcept
{
    try {
        return ::operator new[](size);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* const allocation) noexcept
{
    std::free(allocation);
}

void operator delete[](void* const allocation) noexcept
{
    std::free(allocation);
}

void operator delete(
    void* const allocation, const std::nothrow_t&) noexcept
{
    ::operator delete(allocation);
}

void operator delete[](
    void* const allocation, const std::nothrow_t&) noexcept
{
    ::operator delete[](allocation);
}
