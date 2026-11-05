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
    /// `begin` and `end` bound the operations a block disable covers (the
    /// whole process for a kill): the import call chain is disabled only if
    /// its call is among them.
    using Notice = void (*)(void* user_data, std::uint64_t process,
        std::uint32_t begin, std::uint32_t end);

    /// The process that makes the DPI call being dispatched on this thread.
    static void set_current(const std::uint64_t process) noexcept
    {
        current_ = process;
    }
    [[nodiscard]] static std::uint64_t current() noexcept { return current_; }

    /// A call whose position in its process is not known; a disable always
    /// covers it.
    static constexpr std::uint32_t unknown_position = UINT32_MAX;

    /// The position (operation index) of the DPI call being dispatched on
    /// this thread in its process, set by the kernel before the call.
    static void set_position(const std::uint32_t position) noexcept
    {
        position_ = position;
    }
    [[nodiscard]] static std::uint32_t position() noexcept
    {
        return position_;
    }

    static void install(const Notice notice, void* const user_data) noexcept
    {
        user_data_.store(user_data, std::memory_order_release);
        notice_.store(notice, std::memory_order_release);
    }

    /// A disable ends or redirects `process`, which may be inside an import
    /// call chain.
    static void disabled(const std::uint64_t process)
    {
        disabled(process, 0U, UINT32_MAX);
    }

    /// A disable of the operations `begin` to `end` (exclusive) redirects
    /// `process`. An import call chain whose call lies outside them is not
    /// disabled: the target is an exported subroutine inside the chain
    /// (IEEE 1800-2023 35.9).
    static void disabled(const std::uint64_t process,
        const std::uint32_t begin, const std::uint32_t end)
    {
        const auto notice = notice_.load(std::memory_order_acquire);
        if (notice != nullptr) {
            notice(user_data_.load(std::memory_order_acquire), process, begin,
                end);
        }
    }

private:
    static inline thread_local std::uint64_t current_ {};
    static inline thread_local std::uint32_t position_ {};
    static inline std::atomic<Notice> notice_ {};
    static inline std::atomic<void*> user_data_ {};
};

} // namespace fsim::runtime
