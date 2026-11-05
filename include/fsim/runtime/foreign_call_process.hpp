// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fsim::runtime {

/// The link between the kernel and the DPI service for the disable protocol
/// (IEEE 1800-2023 35.9). The kernel names the process that makes a DPI call
/// and tells the service before a disable ends or redirects a process, so
/// that the service can hand the disable to the C code of the import call
/// chain that process is suspended in. Nothing is installed (and a notice is
/// one atomic load) unless a DPI library is loaded. The state lives in
/// fsim_runtime: header-inline storage would give each Windows DLL its own
/// copy, and the kernel would never see the service's notice.
class DpiCallProcess {
public:
    using Notice = void (*)(void* user_data, std::uint64_t process);

    /// The process that makes the DPI call being dispatched on this thread.
    static void set_current(std::uint64_t process) noexcept;
    [[nodiscard]] static std::uint64_t current() noexcept;

    static void install(Notice notice, void* user_data) noexcept;

    /// A disable ends or redirects `process`, which may be inside an import
    /// call chain.
    static void disabled(std::uint64_t process);
};

} // namespace fsim::runtime
