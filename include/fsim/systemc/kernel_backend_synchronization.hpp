// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_execution.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace fsim::systemc {

inline constexpr std::uint32_t kSystemCKernelSynchronizationRevision = 1U;

enum class SystemCKernelHostLanguage : std::uint8_t {
    verilog = 1,
    system_verilog = 2,
    vhdl = 3,
};

enum class SystemCKernelCrossingStage : std::uint8_t {
    input_batch = 1,
    kernel_arrival = 2,
    kernel_quiescent = 3,
    output_batch = 4,
};

enum class SystemCKernelSynchronizationCode : std::uint16_t {
    none = 0,
    order = 1,
    payload = 2,
    resource = 3,
    transport = 4,
};

struct SystemCKernelSynchronizationLimits {
    std::size_t max_islands { 64U };
    std::size_t max_inputs_per_batch { 4096U };
    std::size_t max_outputs_per_island { 4096U };
    std::uint64_t max_batches { 1'000'000U };
};

struct SystemCKernelEndpointIdentity {
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId endpoint;

    friend auto operator<=>(const SystemCKernelEndpointIdentity&,
        const SystemCKernelEndpointIdentity&) = default;
};

struct SystemCKernelSynchronizedIsland {
    SystemCIslandId island;
    SystemCKernelHostLanguage host_language {
        SystemCKernelHostLanguage::system_verilog
    };
    SystemCSequenceId next_request_sequence;
    std::uint64_t current_time_fs { };
    std::vector<SystemCKernelEndpointIdentity> output_endpoints;
};

struct SystemCKernelSynchronizationPoint {
    std::uint64_t time_fs { };
    std::uint64_t delta { };

    friend auto operator<=>(const SystemCKernelSynchronizationPoint&,
        const SystemCKernelSynchronizationPoint&) = default;
};

struct SystemCKernelSynchronizationInput {
    SystemCIslandId island;
    SystemCKernelEndpointIdentity target;
    SystemCKernelScalarValue value;

    friend bool operator==(const SystemCKernelSynchronizationInput&,
        const SystemCKernelSynchronizationInput&) = default;
};

struct SystemCKernelCrossingRecord {
    SystemCKernelSynchronizationPoint point;
    SystemCKernelCrossingStage stage {
        SystemCKernelCrossingStage::kernel_quiescent
    };
    SystemCIslandId island;
    SystemCEndpointId endpoint;
    SystemCSequenceId request_sequence;
    SystemCKernelExecutionOrder execution_order;

    friend bool operator==(const SystemCKernelCrossingRecord&,
        const SystemCKernelCrossingRecord&) = default;
};

struct SystemCKernelSynchronizationReceipt {
    SystemCKernelSynchronizationPoint point;
    std::vector<SystemCKernelCrossingRecord> crossings;
    std::vector<SystemCKernelExecutionSample> dirty_outputs;
    bool current_activity { };
    bool future_activity { };
    std::optional<std::uint64_t> next_activity_time_fs;

    friend bool operator==(const SystemCKernelSynchronizationReceipt&,
        const SystemCKernelSynchronizationReceipt&) = default;
};

class SystemCKernelSynchronizer {
public:
    virtual ~SystemCKernelSynchronizer() = default;

    [[nodiscard]] virtual bool attach_island(
        SystemCKernelSynchronizedIsland registration,
        std::unique_ptr<SystemCKernelBackend> backend,
        diagnostic::Engine& diagnostics)
        = 0;
    [[nodiscard]] virtual std::optional<SystemCKernelSynchronizationReceipt>
    synchronize(const SystemCKernelSynchronizationPoint& point,
        std::span<const SystemCKernelSynchronizationInput> inputs,
        diagnostic::Engine& diagnostics)
        = 0;
    virtual void close() noexcept = 0;
    [[nodiscard]] virtual std::size_t island_count() const noexcept = 0;
    [[nodiscard]] virtual bool failed() const noexcept = 0;
};

[[nodiscard]] std::unique_ptr<SystemCKernelSynchronizer>
make_systemc_kernel_synchronizer(
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const SystemCKernelSynchronizationLimits& synchronization_limits,
    diagnostic::Engine& diagnostics);

[[nodiscard]] const char* systemc_kernel_synchronization_diagnostic_code(
    SystemCKernelSynchronizationCode code) noexcept;

} // namespace fsim::systemc
