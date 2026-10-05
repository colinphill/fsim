// SPDX-License-Identifier: Apache-2.0
#include "allocation_profile.hpp"

#include <atomic>
#include <ostream>

namespace fsim::app::allocation_profile {
namespace {

constinit std::atomic<bool> enabled { false };
constinit std::atomic<std::uint64_t> requests { 0U };
constinit std::atomic<std::uint64_t> requested_bytes { 0U };
constinit std::atomic<std::uint64_t> failures { 0U };

void write_phase(std::ostream& output, const char* name,
    const Snapshot& before, const Snapshot& after)
{
    output << "FSIM-ALLOC phase=" << name
           << " cpp_new_requests=" << after.requests - before.requests
           << " cpp_requested_bytes="
           << after.requested_bytes - before.requested_bytes
           << " cpp_failed_requests=" << after.failures - before.failures
           << '\n';
}

} // namespace

bool exchange_enabled(const bool value) noexcept
{
    return enabled.exchange(value, std::memory_order_relaxed);
}

Snapshot snapshot() noexcept
{
    return {
        requests.load(std::memory_order_relaxed),
        requested_bytes.load(std::memory_order_relaxed),
        failures.load(std::memory_order_relaxed)
    };
}

bool record_request(const std::size_t bytes) noexcept
{
    if (!enabled.load(std::memory_order_relaxed)) {
        return false;
    }
    requests.fetch_add(1U, std::memory_order_relaxed);
    requested_bytes.fetch_add(
        static_cast<std::uint64_t>(bytes), std::memory_order_relaxed);
    return true;
}

void record_failure(const bool counted) noexcept
{
    if (counted) {
        failures.fetch_add(1U, std::memory_order_relaxed);
    }
}

Session::Session(const bool enabled_value) noexcept
    : active_(enabled_value)
{
    if (active_) {
        previous_enabled_ = exchange_enabled(true);
        checkpoints_[0U] = snapshot();
    }
}

Session::~Session()
{
    restore();
}

void Session::after_setup() noexcept
{
    if (active_) {
        checkpoints_[1U] = snapshot();
    }
}

void Session::after_prepare() noexcept
{
    if (active_) {
        checkpoints_[2U] = snapshot();
    }
}

void Session::after_run() noexcept
{
    if (active_) {
        checkpoints_[3U] = snapshot();
    }
}

void Session::finish(std::ostream& output)
{
    if (!active_) {
        return;
    }
    checkpoints_[4U] = snapshot();
    restore();
    output << "FSIM-ALLOC-SCOPE counters=global-new-new-array"
           << " interception=linked-cli-global-new-only"
           << " bytes=requested-payload"
           << " failures=failed-new-requests"
           << " attribution=all-thread-snapshot-intervals"
           << " excludes=direct-malloc-calloc-realloc-live-bytes-rss"
           << " shared-library-custom-allocators-and-DLL-allocations-may-not-interpose"
           << " asynchronous=worker-allocation-attribution-is-interval-based"
           << '\n';
    write_phase(output, "setup", checkpoints_[0U], checkpoints_[1U]);
    write_phase(output, "prepare", checkpoints_[1U], checkpoints_[2U]);
    write_phase(output, "run", checkpoints_[2U], checkpoints_[3U]);
    write_phase(output, "finalize", checkpoints_[3U], checkpoints_[4U]);
}

void Session::restore() noexcept
{
    if (active_) {
        static_cast<void>(exchange_enabled(previous_enabled_));
        active_ = false;
    }
}

} // namespace fsim::app::allocation_profile
