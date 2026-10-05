// SPDX-License-Identifier: Apache-2.0
#include "native_frontier_generic_update_failure_interposer.hpp"

#include "../allocation_hook_forwarding.hpp"

#include <cstdlib>
#include <new>

namespace {

thread_local std::ptrdiff_t allocations_until_failure { -1 };
thread_local bool allocation_failure_injected { };
thread_local fsim::tests::app::frontier::allocation_failure::FailureObserver
    failure_observer { };
thread_local void* failure_observer_context { };
thread_local fsim::tests::app::frontier::allocation_failure::FailurePredicate
    failure_predicate { };
thread_local void* failure_predicate_context { };
thread_local bool failure_predicate_active { };

void fail_selected_allocation()
{
    if (failure_predicate != nullptr && !failure_predicate_active) {
        const auto predicate = failure_predicate;
        void* const predicate_context = failure_predicate_context;
        failure_predicate_active = true;
        const bool selected = predicate(predicate_context);
        failure_predicate_active = false;
        if (selected) {
            failure_predicate = nullptr;
            failure_predicate_context = nullptr;
            allocations_until_failure = -1;
            allocation_failure_injected = true;
            const auto observer = failure_observer;
            void* const context = failure_observer_context;
            failure_observer = nullptr;
            failure_observer_context = nullptr;
            if (observer != nullptr) {
                observer(context);
            }
            throw std::bad_alloc { };
        }
    }

    if (allocations_until_failure < 0) {
        return;
    }
    if (allocations_until_failure != 0) {
        --allocations_until_failure;
        return;
    }

    allocations_until_failure = -1;
    allocation_failure_injected = true;
    const auto observer = failure_observer;
    void* const context = failure_observer_context;
    failure_observer = nullptr;
    failure_observer_context = nullptr;
    if (observer != nullptr) {
        observer(context);
    }
    throw std::bad_alloc { };
}

} // namespace

namespace fsim::tests::app::frontier::allocation_failure {

void arm(const std::size_t successful_allocations)
{
    allocation_failure_injected = false;
    failure_predicate = nullptr;
    failure_predicate_context = nullptr;
    failure_predicate_active = false;
    allocations_until_failure
        = static_cast<std::ptrdiff_t>(successful_allocations);
}

void arm_when(const FailurePredicate predicate, void* const context) noexcept
{
    allocation_failure_injected = false;
    allocations_until_failure = -1;
    failure_predicate = predicate;
    failure_predicate_context = context;
    failure_predicate_active = false;
}

void reset() noexcept
{
    allocations_until_failure = -1;
    allocation_failure_injected = false;
    failure_observer = nullptr;
    failure_observer_context = nullptr;
    failure_predicate = nullptr;
    failure_predicate_context = nullptr;
    failure_predicate_active = false;
}

void clear() noexcept
{
    allocations_until_failure = -1;
    failure_predicate = nullptr;
    failure_predicate_context = nullptr;
    failure_predicate_active = false;
}

bool injected() noexcept
{
    return allocation_failure_injected;
}

void set_observer(
    const FailureObserver observer, void* const context) noexcept
{
    failure_observer = observer;
    failure_observer_context = context;
}

void clear_observer() noexcept
{
    failure_observer = nullptr;
    failure_observer_context = nullptr;
}

ScopedFailure::ScopedFailure(const std::size_t successful_allocations)
{
    arm(successful_allocations);
}

ScopedFailure::~ScopedFailure()
{
    clear_observer();
    clear();
}

bool ScopedFailure::was_injected() const noexcept
{
    return injected();
}

} // namespace fsim::tests::app::frontier::allocation_failure

void* operator new(const std::size_t size)
{
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
