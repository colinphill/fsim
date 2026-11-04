// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstdint>

namespace fsim::runtime {

/// The link between the kernel and the DPI service for the disable protocol
/// (IEEE 1800-2023 35.9). The kernel names the process that makes a DPI call
/// and tells the service before a disable ends or redirects a process, so
/// that the service can hand the disable to the C code of the import call
/// chain that process is suspended in. Nothing is installed (and a notice is
/// one atomic load) unless a DPI library is loaded.
class DpiCallProcess {
public:
    using Notice = void (*)(void* user_data, std::uint64_t process);

    /// The process that makes the DPI call being dispatched on this thread.
    static void set_current(const std::uint64_t process) noexcept
    {
        current_ = process;
    }
    [[nodiscard]] static std::uint64_t current() noexcept { return current_; }

    static void install(const Notice notice, void* const user_data) noexcept
    {
        user_data_.store(user_data, std::memory_order_release);
        notice_.store(notice, std::memory_order_release);
    }

    /// A disable ends or redirects `process`, which may be inside an import
    /// call chain.
    static void disabled(const std::uint64_t process)
    {
        const auto notice = notice_.load(std::memory_order_acquire);
        if (notice != nullptr) {
            notice(user_data_.load(std::memory_order_acquire), process);
        }
    }

private:
    static inline thread_local std::uint64_t current_ {};
    static inline std::atomic<Notice> notice_ {};
    static inline std::atomic<void*> user_data_ {};
};

} // namespace fsim::runtime
