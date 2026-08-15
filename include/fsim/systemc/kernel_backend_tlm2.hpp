// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_protocol.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelTlm2Version = 1U;

enum class SystemCKernelTlm2SocketKind : std::uint8_t {
    initiator = 1,
    target = 2,
    multi_passthrough_initiator = 3,
    multi_passthrough_target = 4,
};

enum class SystemCKernelTlm2Command : std::uint8_t {
    ignore = 1,
    read = 2,
    write = 3,
};

enum class SystemCKernelTlm2Response : std::uint8_t {
    incomplete = 1,
    ok = 2,
    generic_error = 3,
    address_error = 4,
    command_error = 5,
    burst_error = 6,
    byte_enable_error = 7,
};

enum class SystemCKernelTlm2Operation : std::uint8_t {
    blocking_transport = 1,
    nonblocking_forward = 2,
    nonblocking_backward = 3,
    direct_memory = 4,
    debug_transport = 5,
    invalidate_direct_memory = 6,
};

enum class SystemCKernelTlm2Phase : std::uint8_t {
    none = 0,
    begin_request = 1,
    end_request = 2,
    begin_response = 3,
    end_response = 4,
};

enum class SystemCKernelTlm2Sync : std::uint8_t {
    accepted = 1,
    updated = 2,
    completed = 3,
};

enum class SystemCKernelTlm2DmiAccess : std::uint8_t {
    none = 0,
    read = 1,
    write = 2,
    read_write = 3,
};

enum class SystemCKernelTlm2State : std::uint8_t {
    begun = 1,
    updated = 2,
    completed = 3,
    rejected = 4,
    canceled = 5,
};

enum class SystemCKernelTlm2Code : std::uint16_t {
    none = 0,
    metadata = 1,
    lifecycle = 2,
    payload = 3,
    resource = 4,
};

struct SystemCKernelTlm2Limits {
    std::size_t max_endpoints { 4096U };
    std::size_t max_peers_per_endpoint { 1024U };
    std::size_t max_transactions { 1U << 20U };
    std::size_t max_data_bytes { 16U * 1024U * 1024U };
    std::size_t max_byte_enable_bytes { 1U << 20U };
    std::size_t max_extensions { 1024U };
    std::size_t max_extension_type_bytes { 1024U };
    std::size_t max_extension_bytes { 1U << 20U };
    std::size_t max_encoded_bytes { 32U * 1024U * 1024U };
};

struct SystemCKernelTlm2Endpoint {
    SystemCEndpointId endpoint;
    SystemCKernelTlm2SocketKind kind { SystemCKernelTlm2SocketKind::initiator };
    std::uint32_t bus_width_bits { 32U };
    std::vector<SystemCEndpointId> peers;
    bool explicit_bridge { };

    friend bool operator==(const SystemCKernelTlm2Endpoint&,
        const SystemCKernelTlm2Endpoint&) = default;
};

struct SystemCKernelTlm2Extension {
    std::string type_name;
    std::vector<std::byte> bytes;

    friend bool operator==(const SystemCKernelTlm2Extension&,
        const SystemCKernelTlm2Extension&) = default;
};

struct SystemCKernelTlm2Payload {
    std::uint64_t address { };
    SystemCKernelTlm2Command command { SystemCKernelTlm2Command::ignore };
    SystemCKernelTlm2Response response {
        SystemCKernelTlm2Response::incomplete
    };
    bool dmi_allowed { };
    std::uint32_t streaming_width { };
    std::vector<std::byte> data;
    std::vector<std::byte> byte_enables;
    std::vector<SystemCKernelTlm2Extension> extensions;

    friend bool operator==(const SystemCKernelTlm2Payload&,
        const SystemCKernelTlm2Payload&) = default;
};

struct SystemCKernelTlm2Dmi {
    std::uint64_t start_address { };
    std::uint64_t end_address { };
    SystemCKernelTlm2DmiAccess access { SystemCKernelTlm2DmiAccess::none };
    std::uint64_t read_latency_fs { };
    std::uint64_t write_latency_fs { };

    friend bool operator==(const SystemCKernelTlm2Dmi&,
        const SystemCKernelTlm2Dmi&) = default;
};

struct SystemCKernelTlm2Transaction {
    SystemCTransactionId transaction;
    SystemCEndpointId endpoint;
    SystemCEndpointId peer;
    SystemCSequenceId sequence;
    SystemCKernelTlm2Operation operation {
        SystemCKernelTlm2Operation::blocking_transport
    };
    SystemCKernelTlm2Phase phase { SystemCKernelTlm2Phase::none };
    SystemCKernelTlm2Sync sync { SystemCKernelTlm2Sync::accepted };
    SystemCKernelTlm2State state { SystemCKernelTlm2State::begun };
    std::uint64_t time_fs { };
    std::uint64_t delta { };
    std::uint64_t delay_fs { };
    std::uint32_t transferred { };
    bool explicit_bridge { };
    SystemCKernelTlm2Payload payload;
    std::optional<SystemCKernelTlm2Dmi> dmi;

    friend bool operator==(const SystemCKernelTlm2Transaction&,
        const SystemCKernelTlm2Transaction&) = default;
};

class SystemCKernelTlm2Registry {
public:
    explicit SystemCKernelTlm2Registry(SystemCIslandId island,
        SystemCKernelTlm2Limits limits = { });

    [[nodiscard]] bool register_endpoint(
        SystemCKernelTlm2Endpoint endpoint, diagnostic::Engine& diagnostics);

    [[nodiscard]] std::optional<SystemCTransactionId> begin(
        SystemCEndpointId endpoint, SystemCEndpointId peer,
        SystemCSequenceId sequence, SystemCKernelTlm2Operation operation,
        SystemCKernelTlm2Phase phase, SystemCKernelTlm2Payload payload,
        std::uint64_t time_fs, std::uint64_t delta,
        diagnostic::Engine& diagnostics);

    [[nodiscard]] bool update(SystemCTransactionId transaction,
        SystemCKernelTlm2State state, SystemCKernelTlm2Phase phase,
        SystemCKernelTlm2Sync sync, SystemCKernelTlm2Payload payload,
        std::uint64_t delay_fs, std::uint32_t transferred,
        std::optional<SystemCKernelTlm2Dmi> dmi,
        diagnostic::Engine& diagnostics);

    [[nodiscard]] const std::vector<SystemCKernelTlm2Endpoint>& endpoints()
        const noexcept;
    [[nodiscard]] const std::vector<SystemCKernelTlm2Transaction>& transactions()
        const noexcept;

private:
    SystemCIslandId island_;
    SystemCKernelTlm2Limits limits_;
    std::vector<SystemCKernelTlm2Endpoint> endpoints_;
    std::vector<SystemCKernelTlm2Transaction> transactions_;
};

[[nodiscard]] bool validate_systemc_kernel_tlm2_endpoint(
    const SystemCKernelTlm2Endpoint& endpoint,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool validate_systemc_kernel_tlm2_payload(
    const SystemCKernelTlm2Payload& payload,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool validate_systemc_kernel_tlm2_transaction(
    const SystemCKernelTlm2Transaction& transaction,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_kernel_tlm2_transaction(
    const SystemCKernelTlm2Transaction& transaction,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<SystemCKernelTlm2Transaction>
deserialize_systemc_kernel_tlm2_transaction(std::span<const std::byte> bytes,
    const SystemCKernelTlm2Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_tlm2_diagnostic_code(
    SystemCKernelTlm2Code code) noexcept;

} // namespace fsim::systemc
