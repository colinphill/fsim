// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::runtime {

/// The DPI-C foreign-call service's view of the calling process. The kernel
/// names the process that makes each DPI call before the call is dispatched,
/// and tells the service before a disable ends or redirects a process, so
/// that the service can hand the disable to the C code of the import call
/// chain that process is suspended in. Nothing is installed (and a notice is
/// one atomic load) unless a DPI library is loaded. The state lives in
/// fsim_runtime: header-inline storage would give each Windows DLL its own
/// copy, and the kernel would never see the service's notice.
class DpiCallProcess {
public:
    /// `begin` and `end` bound the operations a block disable covers (the
    /// whole process for a kill): the import call chain is disabled only if
    /// its call is among them.
    using Notice = void (*)(void* user_data, std::uint64_t process,
        std::uint32_t begin, std::uint32_t end);

    /// The process that makes the DPI call being dispatched on this thread.
    static void set_current(std::uint64_t process) noexcept;
    [[nodiscard]] static std::uint64_t current() noexcept;

    /// A call whose position in its process is not known; a disable always
    /// covers it.
    static constexpr std::uint32_t unknown_position = UINT32_MAX;

    /// The position (operation index) of the DPI call being dispatched on
    /// this thread in its process, set by the kernel before the call.
    static void set_position(std::uint32_t position) noexcept;
    [[nodiscard]] static std::uint32_t position() noexcept;

    static void install(Notice notice, void* user_data) noexcept;

    /// A disable ends or redirects `process`, which may be inside an import
    /// call chain.
    static void disabled(std::uint64_t process);

    /// A disable of the operations `begin` to `end` (exclusive) redirects
    /// `process`. An import call chain whose call lies outside them is not
    /// disabled: the target is an exported subroutine inside the chain
    /// (IEEE 1800-2023 35.9).
    static void disabled(std::uint64_t process, std::uint32_t begin,
        std::uint32_t end);
};

} // namespace fsim::runtime
