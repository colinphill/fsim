// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "native_frontier_generic_update_test_support.hpp"

#include <cstddef>
#include <cstdint>

namespace fsim::tests::app::frontier::allocation_failure {

struct FailureSample final {
    GenericFrontierProbe frontier;
    bool observed { };
};

static_assert(std::is_trivially_copyable_v<FailureSample>);

using FailureObserver = void (*)(void*) noexcept;
using FailurePredicate = bool (*)(void*) noexcept;

void arm(std::size_t successful_allocations);
void arm_when(FailurePredicate predicate, void* context) noexcept;
void reset() noexcept;
void clear() noexcept;
[[nodiscard]] bool injected() noexcept;
void set_observer(FailureObserver observer, void* context) noexcept;
void clear_observer() noexcept;

class ScopedFailure final {
public:
    explicit ScopedFailure(std::size_t successful_allocations);
    ~ScopedFailure();

    ScopedFailure(const ScopedFailure&) = delete;
    ScopedFailure& operator=(const ScopedFailure&) = delete;

    [[nodiscard]] bool was_injected() const noexcept;
};

} // namespace fsim::tests::app::frontier::allocation_failure
