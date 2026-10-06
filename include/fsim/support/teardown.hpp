// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

namespace fsim::support {

/// The fsim executable exits right after one command, so freeing a large
/// design's heap only delays the exit. It calls this once at startup; library
/// and test callers never do, and keep ordinary destruction.
void enable_exit_without_teardown() noexcept;

[[nodiscard]] bool exit_without_teardown() noexcept;

/// Keep `object` reachable until process exit without destroying it. Callers
/// first perform every side effect of its destructor (flushes, callbacks,
/// joins); only freeing memory is skipped.
void retain_until_exit(std::shared_ptr<void> object);

/// Retain `object` when exit_without_teardown() holds; otherwise destroy it
/// now.
template <typename T>
void release_at_exit(std::unique_ptr<T> object)
{
    if (object && exit_without_teardown()) {
        retain_until_exit(std::shared_ptr<T> { std::move(object) });
    }
}

}  // namespace fsim::support
