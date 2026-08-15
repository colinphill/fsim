// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelSessionPayloadVersion = 1U;

struct SystemCKernelSessionLimits {
    std::size_t max_plugin_path_bytes { 4096U };
    std::size_t max_identity_bytes { 4096U };
    std::size_t max_name_bytes { 1024U };
    std::size_t max_objects { 4096U };
    std::size_t max_bindings { 16384U };
    std::size_t max_parameters_per_object { 64U };
    std::size_t max_detail_bytes { 4096U };
};

enum class SystemCKernelSessionState : std::uint8_t {
    vacant = 0,
    constructing = 1,
    elaborated = 2,
    quiescent = 3,
    terminal = 4,
    failed = 5,
};

enum class SystemCKernelLifecycleCode : std::uint16_t {
    none = 0,
    session_state = 1,
    payload = 2,
    resource = 3,
    upstream = 4,
};

struct SystemCKernelCreateSessionPayload {
    std::string canonical_identity;
    std::string plugin_path;
    std::uint64_t time_resolution_fs { 1U };

    friend bool operator==(const SystemCKernelCreateSessionPayload&,
        const SystemCKernelCreateSessionPayload&) = default;
};

struct SystemCKernelConstructionParameter {
    std::string name;
    std::int64_t value { };

    friend bool operator==(const SystemCKernelConstructionParameter&,
        const SystemCKernelConstructionParameter&) = default;
};

struct SystemCKernelCreateObjectPayload {
    std::string hierarchy_path;
    std::string object_path;
    std::string factory;
    std::string instance;
    std::vector<SystemCKernelConstructionParameter> parameters;

    friend bool operator==(const SystemCKernelCreateObjectPayload&,
        const SystemCKernelCreateObjectPayload&) = default;
};

struct SystemCKernelBindEndpointPayload {
    std::string endpoint_path;
    std::string interface_path;

    friend bool operator==(const SystemCKernelBindEndpointPayload&,
        const SystemCKernelBindEndpointPayload&) = default;
};

struct SystemCKernelLifecycleReceipt {
    SystemCKernelSessionState state { SystemCKernelSessionState::vacant };
    bool published { };
    SystemCKernelLifecycleCode code { SystemCKernelLifecycleCode::none };
    std::uint32_t staged_objects { };
    std::uint32_t staged_bindings { };
    std::uint32_t published_objects { };
    std::uint64_t context_generation { };
    std::string detail;

    friend bool operator==(const SystemCKernelLifecycleReceipt&,
        const SystemCKernelLifecycleReceipt&) = default;
};

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_create_session_payload(
    const SystemCKernelCreateSessionPayload& payload,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCKernelCreateSessionPayload>
deserialize_systemc_create_session_payload(
    std::span<const std::byte> bytes,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_create_object_payload(
    const SystemCKernelCreateObjectPayload& payload,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCKernelCreateObjectPayload>
deserialize_systemc_create_object_payload(
    std::span<const std::byte> bytes,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_bind_endpoint_payload(
    const SystemCKernelBindEndpointPayload& payload,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCKernelBindEndpointPayload>
deserialize_systemc_bind_endpoint_payload(
    std::span<const std::byte> bytes,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_lifecycle_receipt(
    const SystemCKernelLifecycleReceipt& receipt,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics);
[[nodiscard]] std::optional<SystemCKernelLifecycleReceipt>
deserialize_systemc_lifecycle_receipt(
    std::span<const std::byte> bytes,
    const SystemCKernelSessionLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_lifecycle_diagnostic_code(
    SystemCKernelLifecycleCode code) noexcept;

[[nodiscard]] std::unique_ptr<SystemCKernelBackend>
make_systemc_kernel_session_backend(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelSessionLimits& session_limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::size_t
systemc_kernel_backend_live_contexts() noexcept;

} // namespace fsim::systemc
