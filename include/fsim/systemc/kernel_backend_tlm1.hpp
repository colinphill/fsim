// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_value_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelTlm1Version = 1U;

enum class SystemCKernelTlm1InterfaceKind : std::uint8_t {
    fifo = 1,
    blocking_put = 2,
    nonblocking_put = 3,
    blocking_get = 4,
    nonblocking_get = 5,
    blocking_peek = 6,
    nonblocking_peek = 7,
    transport = 8,
    analysis = 9,
};

enum class SystemCKernelTlm1Operation : std::uint8_t {
    put = 1,
    get = 2,
    peek = 3,
    transport = 4,
    analysis = 5,
};

enum class SystemCKernelTlm1State : std::uint8_t {
    begun = 1,
    blocked = 2,
    completed = 3,
    unavailable = 4,
    canceled = 5,
};

enum class SystemCKernelTlm1Code : std::uint16_t {
    none = 0,
    metadata = 1,
    lifecycle = 2,
    payload = 3,
    resource = 4,
};

struct SystemCKernelTlm1Limits {
    std::size_t max_endpoints { 4096U };
    std::size_t max_peers_per_endpoint { 1024U };
    std::size_t max_transactions { 1U << 20U };
    std::size_t max_type_name_bytes { 1024U };
    std::size_t max_encoded_bytes { 16U * 1024U * 1024U };
    SystemCKernelValueLimits value_limits;
};

struct SystemCKernelTlm1Endpoint {
    SystemCEndpointId endpoint;
    SystemCKernelTlm1InterfaceKind kind { SystemCKernelTlm1InterfaceKind::fifo };
    std::string type_name;
    std::vector<SystemCEndpointId> peers;
    bool explicit_bridge { };

    friend bool operator==(const SystemCKernelTlm1Endpoint&,
        const SystemCKernelTlm1Endpoint&) = default;
};

struct SystemCKernelTlm1Transaction {
    SystemCTransactionId transaction;
    SystemCEndpointId endpoint;
    SystemCEndpointId peer;
    SystemCSequenceId sequence;
    SystemCKernelTlm1Operation operation { SystemCKernelTlm1Operation::put };
    SystemCKernelTlm1State state { SystemCKernelTlm1State::begun };
    std::uint64_t time_fs { };
    std::uint64_t delta { };
    bool explicit_bridge { };
    std::optional<SystemCKernelValue> request;
    std::optional<SystemCKernelValue> response;

    friend bool operator==(const SystemCKernelTlm1Transaction&,
        const SystemCKernelTlm1Transaction&) = default;
};

class SystemCKernelTlm1Registry {
public:
    explicit SystemCKernelTlm1Registry(SystemCIslandId island,
        SystemCKernelTlm1Limits limits = { });

    [[nodiscard]] bool register_endpoint(
        SystemCKernelTlm1Endpoint endpoint, diagnostic::Engine& diagnostics);

    [[nodiscard]] std::optional<SystemCTransactionId> begin(
        SystemCEndpointId endpoint, SystemCEndpointId peer,
        SystemCSequenceId sequence, SystemCKernelTlm1Operation operation,
        std::optional<SystemCKernelValue> request, std::uint64_t time_fs,
        std::uint64_t delta, diagnostic::Engine& diagnostics);

    [[nodiscard]] bool transition(SystemCTransactionId transaction,
        SystemCKernelTlm1State state,
        std::optional<SystemCKernelValue> response,
        diagnostic::Engine& diagnostics);

    [[nodiscard]] const std::vector<SystemCKernelTlm1Endpoint>& endpoints()
        const noexcept;
    [[nodiscard]] const std::vector<SystemCKernelTlm1Transaction>& transactions()
        const noexcept;

private:
    SystemCIslandId island_;
    SystemCKernelTlm1Limits limits_;
    std::vector<SystemCKernelTlm1Endpoint> endpoints_;
    std::vector<SystemCKernelTlm1Transaction> transactions_;
};

[[nodiscard]] bool validate_systemc_kernel_tlm1_endpoint(
    const SystemCKernelTlm1Endpoint& endpoint,
    const SystemCKernelTlm1Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] bool validate_systemc_kernel_tlm1_transaction(
    const SystemCKernelTlm1Transaction& transaction,
    const SystemCKernelTlm1Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_kernel_tlm1_transaction(
    const SystemCKernelTlm1Transaction& transaction,
    const SystemCKernelTlm1Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<SystemCKernelTlm1Transaction>
deserialize_systemc_kernel_tlm1_transaction(std::span<const std::byte> bytes,
    const SystemCKernelTlm1Limits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_tlm1_diagnostic_code(
    SystemCKernelTlm1Code code) noexcept;

} // namespace fsim::systemc
