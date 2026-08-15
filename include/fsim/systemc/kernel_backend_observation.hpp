// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_execution.hpp"
#include "fsim/systemc/kernel_backend_inventory.hpp"
#include "fsim/systemc/kernel_backend_tlm1.hpp"
#include "fsim/systemc/kernel_backend_tlm2.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelObservationVersion = 1U;

enum class SystemCKernelObservationKind : std::uint8_t {
    debugger_read = 1,
    debugger_write = 2,
    inventory_query = 3,
    report = 4,
    tlm1_begin = 5,
    tlm1_update = 6,
    tlm1_end = 7,
    tlm2_begin = 8,
    tlm2_phase = 9,
    tlm2_dmi = 10,
    tlm2_debug = 11,
    tlm2_end = 12,
};

enum class SystemCKernelObservationCode : std::uint16_t {
    none = 0,
    metadata = 1,
    safe_point = 2,
    unsupported = 3,
    resource = 4,
};

struct SystemCKernelObservationLimits {
    std::size_t max_adapters { 4096U };
    std::size_t max_records { 1U << 20U };
    std::size_t max_detail_bytes { 4096U };
    std::size_t max_transaction_bytes { 32U * 1024U * 1024U };
    std::size_t max_encoded_bytes { 64U * 1024U * 1024U };
    SystemCKernelValueLimits value_limits;
    SystemCKernelTlm1Limits tlm1_limits;
    SystemCKernelTlm2Limits tlm2_limits;
};

struct SystemCKernelObservationRecord {
    SystemCKernelObservationKind kind {
        SystemCKernelObservationKind::inventory_query
    };
    SystemCKernelExecutionOrder order;
    SystemCEndpointId endpoint;
    SystemCEndpointId peer;
    SystemCTransactionId transaction;
    std::optional<SystemCKernelValue> value;
    std::vector<std::byte> transaction_bytes;
    std::string detail;

    friend bool operator==(const SystemCKernelObservationRecord&,
        const SystemCKernelObservationRecord&) = default;
};

struct SystemCKernelObservationBatch {
    SystemCIslandId island;
    std::vector<SystemCKernelObservationRecord> records;

    friend bool operator==(const SystemCKernelObservationBatch&,
        const SystemCKernelObservationBatch&) = default;
};

struct SystemCKernelChannelObservationAdapter {
    SystemCEndpointId endpoint;
    std::function<std::optional<SystemCKernelValue>(diagnostic::Engine&)> read;
    std::function<bool(const SystemCKernelValue&, diagnostic::Engine&)> write;
};

class SystemCKernelSafePointObserver {
public:
    explicit SystemCKernelSafePointObserver(
        SystemCKernelChannelInventorySnapshot inventory,
        SystemCKernelObservationLimits limits = { });

    [[nodiscard]] bool register_adapter(
        SystemCKernelChannelObservationAdapter adapter,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool query_inventory(const SystemCKernelExecutionOrder& order,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] std::optional<std::string> report(
        const SystemCKernelExecutionOrder& order,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] std::optional<SystemCKernelValue> read(
        SystemCEndpointId endpoint, const SystemCKernelExecutionOrder& order,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool write(SystemCEndpointId endpoint,
        const SystemCKernelValue& value,
        const SystemCKernelExecutionOrder& order,
        diagnostic::Engine& diagnostics);
    [[nodiscard]] bool observe_tlm1(const SystemCKernelTlm1Transaction& value,
        SystemCKernelObservationKind kind, diagnostic::Engine& diagnostics);
    [[nodiscard]] bool observe_tlm2(const SystemCKernelTlm2Transaction& value,
        SystemCKernelObservationKind kind, diagnostic::Engine& diagnostics);

    [[nodiscard]] const SystemCKernelChannelInventorySnapshot& inventory()
        const noexcept;
    [[nodiscard]] const SystemCKernelObservationBatch& batch() const noexcept;

private:
    [[nodiscard]] bool append(SystemCKernelObservationRecord record,
        diagnostic::Engine& diagnostics);

    SystemCKernelObservationLimits limits_;
    SystemCKernelChannelInventorySnapshot inventory_;
    SystemCKernelObservationBatch batch_;
    std::vector<SystemCKernelChannelObservationAdapter> adapters_;
};

[[nodiscard]] bool validate_systemc_kernel_observation_batch(
    const SystemCKernelObservationBatch& batch,
    const SystemCKernelObservationLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<std::vector<std::byte>>
serialize_systemc_kernel_observation_batch(
    const SystemCKernelObservationBatch& batch,
    const SystemCKernelObservationLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] std::optional<SystemCKernelObservationBatch>
deserialize_systemc_kernel_observation_batch(std::span<const std::byte> bytes,
    const SystemCKernelObservationLimits& limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_observation_diagnostic_code(
    SystemCKernelObservationCode code) noexcept;

} // namespace fsim::systemc
