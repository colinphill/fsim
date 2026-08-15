// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_execution.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelLoopbackRevision = 1U;

struct SystemCKernelLoopbackLimits {
    std::size_t max_replay_entries { 4096U };
    std::size_t max_buffer_bytes {
        kSystemCKernelMessageHeaderBytes + 1024U * 1024U
    };
    std::uint64_t max_forwarded_exchanges { 1'000'000U };
};

enum class SystemCKernelLoopbackCode : std::uint16_t {
    none = 0,
    order = 1,
    response = 2,
    resource = 3,
    peer = 4,
};

struct SystemCKernelLoopbackStats {
    std::uint64_t forwarded { };
    std::uint64_t replayed { };
    std::uint64_t rejected { };
    std::uint64_t evicted { };
    std::uint64_t disconnected { };
    std::size_t cached { };

    friend bool operator==(const SystemCKernelLoopbackStats&,
        const SystemCKernelLoopbackStats&) = default;
};

class SystemCKernelLoopbackBackend : public SystemCKernelBackend {
public:
    ~SystemCKernelLoopbackBackend() override = default;

    [[nodiscard]] virtual SystemCKernelLoopbackStats stats() const = 0;
    [[nodiscard]] virtual SystemCKernelLoopbackCode last_code()
        const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<SystemCKernelLoopbackBackend>
make_systemc_kernel_loopback_backend(
    std::unique_ptr<SystemCKernelBackend> peer,
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelLoopbackLimits& loopback_limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::unique_ptr<SystemCKernelLoopbackBackend>
make_systemc_kernel_loopback_session_backend(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const SystemCKernelLoopbackLimits& loopback_limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_loopback_diagnostic_code(
    SystemCKernelLoopbackCode code) noexcept;

[[nodiscard]] std::size_t systemc_kernel_loopback_live_transports() noexcept;

} // namespace fsim::systemc
