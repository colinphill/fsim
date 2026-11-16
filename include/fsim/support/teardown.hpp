// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

namespace fsim::support {

/// The fsim executable exits right after one command, so freeing a large
/// design's heap only delays the exit. It calls this once at startup; library
/// and test callers never do, and keep ordinary destruction.
void enable_exit_without_teardown() noexcept;

[[nodiscard]] bool exit_without_teardown() noexcept;

/// The fsim executable installs its allocator's routine that settles the heap
/// for the run: it returns the pages freed so far to the system and keeps the
/// pages freed later for reuse, so the resident size does not drop at
/// arbitrary points of the run. Library and test callers install none.
using HeapSettle = void (*)() noexcept;
void set_heap_settle(HeapSettle settle) noexcept;

/// Settle the heap for the run (nothing happens when no routine is installed).
void settle_heap_for_run() noexcept;

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
