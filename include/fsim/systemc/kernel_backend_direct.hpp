// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/kernel_backend_execution.hpp"

#include <cstdint>
#include <variant>

namespace fsim::systemc {

// Direct requests keep the operation and its owning payload in the same
// alternative. Callers cannot pair a payload with a different operation.
struct SystemCKernelCreateSessionRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
    SystemCKernelCreateSessionPayload payload;
};

struct SystemCKernelCreateObjectRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCKernelCreateObjectPayload payload;
};

struct SystemCKernelBindEndpointRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
    SystemCHierarchyId hierarchy;
    SystemCObjectId object;
    SystemCEndpointId endpoint;
    SystemCKernelBindEndpointPayload payload;
};

struct SystemCKernelElaborateRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
};

struct SystemCKernelStartRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
};

struct SystemCKernelApplyInputsRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
    SystemCObjectId object;
    SystemCEndpointId endpoint;
    SystemCKernelApplyInputsPayload payload;
};

struct SystemCKernelAdvanceRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
    SystemCKernelAdvancePayload payload;
};

struct SystemCKernelNextActivityRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
};

struct SystemCKernelDrainOutputsRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
    SystemCObjectId object;
    SystemCEndpointId endpoint;
};

struct SystemCKernelReportRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
};

struct SystemCKernelInspectRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
    SystemCObjectId object;
    SystemCEndpointId endpoint;
};

struct SystemCKernelSnapshotRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
};

struct SystemCKernelTeardownRequest {
    SystemCIslandId island;
    SystemCSequenceId sequence;
};

using SystemCKernelDirectRequest = std::variant<
    SystemCKernelCreateSessionRequest,
    SystemCKernelCreateObjectRequest,
    SystemCKernelBindEndpointRequest,
    SystemCKernelElaborateRequest,
    SystemCKernelStartRequest,
    SystemCKernelApplyInputsRequest,
    SystemCKernelAdvanceRequest,
    SystemCKernelNextActivityRequest,
    SystemCKernelDrainOutputsRequest,
    SystemCKernelReportRequest,
    SystemCKernelInspectRequest,
    SystemCKernelSnapshotRequest,
    SystemCKernelTeardownRequest>;

enum class SystemCKernelDirectResultStatus : std::uint8_t {
    ok = 1,
    rejected = 2,
    failed = 3,
    disconnected = 4,
};

using SystemCKernelDirectReceipt = std::variant<std::monostate,
    SystemCKernelLifecycleReceipt, SystemCKernelExecutionReceipt>;

struct SystemCKernelDirectResult {
    SystemCKernelDirectResultStatus status {
        SystemCKernelDirectResultStatus::failed
    };
    SystemCKernelDirectReceipt receipt;
};

class SystemCKernelBackend {
public:
    virtual ~SystemCKernelBackend() = default;

    [[nodiscard]] virtual SystemCKernelDirectResult request(
        const SystemCKernelDirectRequest& request) noexcept = 0;
    virtual void close() noexcept = 0;
};

} // namespace fsim::systemc
