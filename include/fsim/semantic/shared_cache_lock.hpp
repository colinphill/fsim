// SPDX-License-Identifier: Apache-2.0
//
// Locking for the caches that copies of a SpecializedHirUnit share
// (ResolutionMemo, the VHDL operator selections, the elaboration's
// specialization constants). Elaboration lowers on one thread except in its
// parallel stage; only then does each access lock.
#pragma once

#include <mutex>

namespace fsim::semantic::detail {

/// Whether lowerers run on several threads, so the shared caches lock on
/// each access. Set before the threads start and cleared after they join.
[[nodiscard]] bool shared_caches_concurrent() noexcept;
void set_shared_caches_concurrent(bool concurrent) noexcept;

/// Guards the lazy creation of SharedCacheSlot slots and of the caches they
/// hold.
[[nodiscard]] std::mutex& shared_cache_creation_mutex() noexcept;

/// Holds `mutex` for its scope while the shared caches are concurrent.
class SharedCacheLock final {
public:
    explicit SharedCacheLock(std::mutex& mutex) noexcept
        : mutex_ { shared_caches_concurrent() ? &mutex : nullptr }
    {
        if (mutex_ != nullptr) {
            mutex_->lock();
        }
    }
    ~SharedCacheLock()
    {
        if (mutex_ != nullptr) {
            mutex_->unlock();
        }
    }
    SharedCacheLock(const SharedCacheLock&) = delete;
    SharedCacheLock& operator=(const SharedCacheLock&) = delete;

private:
    std::mutex* mutex_;
};

} // namespace fsim::semantic::detail
