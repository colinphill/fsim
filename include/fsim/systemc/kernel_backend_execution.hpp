// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_session.hpp"
#include "fsim/systemc/kernel_backend_value_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelExecutionPayloadVersion = 2U;

struct SystemCKernelExecutionLimits {
    std::size_t max_samples_per_message { 4096U };
    std::size_t max_detail_bytes { 65536U };
    std::uint64_t max_delta_cycles_per_advance { 1'000'000U };
    std::uint64_t max_advance_fs { 1'000'000'000'000'000ULL };
    SystemCKernelValueLimits value_limits;
};

enum class SystemCAccelleraRegion : std::uint8_t {
    evaluate = 1,
    update = 2,
    notification = 3,
    quiescent = 4,
    terminal = 5,
};

enum class SystemCKernelAdvanceKind : std::uint8_t {
    delta = 1,
    time = 2,
};

enum class SystemCKernelExecutionStatus : std::uint8_t {
    quiescent = 1,
    running = 2,
    paused = 3,
    stopped = 4,
    error = 5,
    terminal = 6,
};

enum class SystemCKernelExecutionCode : std::uint16_t {
    none = 0,
    state = 1,
    payload = 2,
    resource = 3,
    upstream = 4,
};

struct SystemCKernelExecutionOrder {
    std::uint64_t time_fs { };
    std::uint64_t delta { };
    SystemCAccelleraRegion region { SystemCAccelleraRegion::quiescent };
    SystemCIslandId island;
    SystemCSequenceId sequence;

    friend auto operator<=>(const SystemCKernelExecutionOrder&,
        const SystemCKernelExecutionOrder&) = default;
};

struct SystemCKernelScalarValue {
    std::uint64_t bits { };
    std::uint16_t width { };
    bool is_signed { };
    std::optional<SystemCKernelValue> typed;

    constexpr SystemCKernelScalarValue() = default;
    constexpr SystemCKernelScalarValue(const std::uint64_t scalar_bits,
        const std::uint16_t scalar_width, const bool scalar_signed) noexcept
        : bits { scalar_bits }
        , width { scalar_width }
        , is_signed { scalar_signed }
    {
    }

    friend bool operator==(const SystemCKernelScalarValue&,
        const SystemCKernelScalarValue&) = default;
};

struct SystemCKernelApplyInputsPayload {
    SystemCKernelScalarValue value;

    friend bool operator==(const SystemCKernelApplyInputsPayload&,
        const SystemCKernelApplyInputsPayload&) = default;
};

struct SystemCKernelAdvancePayload {
    SystemCKernelAdvanceKind kind { SystemCKernelAdvanceKind::delta };
    std::uint64_t duration_fs { };

    friend bool operator==(const SystemCKernelAdvancePayload&,
        const SystemCKernelAdvancePayload&) = default;
};

struct SystemCKernelExecutionSample {
    SystemCEndpointId endpoint;
    SystemCKernelExecutionOrder order;
    SystemCKernelScalarValue value;
    bool dirty { };

    friend bool operator==(const SystemCKernelExecutionSample&,
        const SystemCKernelExecutionSample&) = default;
};

struct SystemCKernelExecutionReceipt {
    SystemCKernelSessionState session_state {
        SystemCKernelSessionState::vacant
    };
    SystemCKernelExecutionStatus status {
        SystemCKernelExecutionStatus::quiescent
    };
    SystemCKernelExecutionCode code { SystemCKernelExecutionCode::none };
    bool published { };
    bool current_activity { };
    bool future_activity { };
    std::optional<std::uint64_t> next_activity_time_fs;
    SystemCKernelExecutionOrder order;
    std::vector<SystemCKernelExecutionSample> samples;
    std::string detail;

    friend bool operator==(const SystemCKernelExecutionReceipt&,
        const SystemCKernelExecutionReceipt&) = default;
};

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_apply_inputs_payload(
    const SystemCKernelApplyInputsPayload& payload,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCKernelApplyInputsPayload>
deserialize_systemc_apply_inputs_payload(
    std::span<const std::byte> bytes,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_advance_payload(
    const SystemCKernelAdvancePayload& payload,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCKernelAdvancePayload>
deserialize_systemc_advance_payload(
    std::span<const std::byte> bytes,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_execution_receipt(
    const SystemCKernelExecutionReceipt& receipt,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCKernelExecutionReceipt>
deserialize_systemc_execution_receipt(
    std::span<const std::byte> bytes,
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_execution_diagnostic_code(
    SystemCKernelExecutionCode code) noexcept;

[[nodiscard]] std::unique_ptr<SystemCKernelBackend>
make_systemc_kernel_session_backend(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    diagnostic::Engine& diagnostics);

} // namespace fsim::systemc
