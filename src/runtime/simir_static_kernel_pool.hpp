// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

/// Thread-local state the kernel's helpers read on every call: initial-exec
/// keeps the access one thread-pointer-relative load on ELF targets.
#if defined(__ELF__)
#define FSIM_STATIC_KERNEL_TLS [[gnu::tls_model("initial-exec")]]
#else
#define FSIM_STATIC_KERNEL_TLS
#endif

namespace fsim::runtime::simir::static_kernel_detail {

/// Worker threads for the static kernel's parallel batches (multithread
/// proposal §3.1). The calling thread is lane 0 and takes items too. Between
/// batches a worker spins for a while, then sleeps until the next batch.
class KernelPool {
public:
    using Body = std::function<void(std::uint32_t lane, std::uint32_t item)>;

    /// `lanes` threads in all, the caller included; `spin` is the number of
    /// pause iterations a worker waits before it sleeps.
    KernelPool(std::uint32_t lanes, std::uint32_t spin);
    ~KernelPool();
    KernelPool(const KernelPool&) = delete;
    KernelPool& operator=(const KernelPool&) = delete;

    [[nodiscard]] std::uint32_t lanes() const noexcept
    {
        return lanes_;
    }

    /// Runs `body(lane, item)` once for each item in [0, count), spread over
    /// the lanes, and returns when every item has finished. `body` must not
    /// throw; `count` is below 2^24.
    void run(std::uint32_t count, const Body& body);

private:
    void work(std::uint32_t lane);
    /// Takes items of the batch `word` names until none is left or a newer
    /// batch has started.
    void take(std::uint32_t lane, std::uint64_t word);

    std::uint32_t lanes_ { };
    std::uint32_t spin_ { };
    /// The batch number (16 bits), its item count and the next item (24
    /// bits each): one compare-and-swap both claims items and checks that
    /// the batch is still the one the claimer saw.
    static constexpr unsigned count_shift = 24U;
    static constexpr unsigned epoch_shift = 48U;
    static constexpr std::uint64_t item_mask = (std::uint64_t { 1 } << 24U) - 1U;
    static constexpr std::uint64_t epoch_mask = 0xffffU;
    alignas(64) std::atomic<std::uint64_t> state_ { };
    alignas(64) std::atomic<std::uint32_t> completed_ { };
    std::atomic<std::uint32_t> chunk_ { 1U };
    std::atomic<const Body*> body_ { };
    std::atomic<bool> stop_ { };
    std::vector<std::thread> threads_;
};

} // namespace fsim::runtime::simir::static_kernel_detail
