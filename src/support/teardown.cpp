// SPDX-License-Identifier: Apache-2.0
#include "fsim/support/teardown.hpp"

#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

namespace fsim::support {
namespace {

std::atomic<bool> skip_teardown { false };

struct Retained {
    std::mutex mutex;
    std::vector<std::shared_ptr<void>> objects;
};

Retained& retained()
{
    // Never destroyed: the objects stay reachable through exit.
    static auto* const storage = new Retained;
    return *storage;
}

}  // namespace

void enable_exit_without_teardown() noexcept
{
    skip_teardown.store(true, std::memory_order_relaxed);
}

bool exit_without_teardown() noexcept
{
    return skip_teardown.load(std::memory_order_relaxed);
}

void retain_until_exit(std::shared_ptr<void> object)
{
    auto& storage = retained();
    const std::scoped_lock lock { storage.mutex };
    storage.objects.push_back(std::move(object));
}

}  // namespace fsim::support
