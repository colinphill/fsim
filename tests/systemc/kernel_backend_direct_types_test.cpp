// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_direct.hpp"

#include <concepts>
#include <type_traits>
#include <variant>

namespace {

using namespace fsim::systemc;

template<typename Type>
concept SequencedRequest = std::same_as<decltype(Type::sequence),
    SystemCSequenceId>;

static_assert(std::variant_size_v<SystemCKernelDirectRequest> == 13U);
static_assert(std::is_constructible_v<SystemCKernelDirectRequest,
    SystemCKernelCreateObjectRequest>);
static_assert(SequencedRequest<SystemCKernelCreateSessionRequest>
    && SequencedRequest<SystemCKernelCreateObjectRequest>
    && SequencedRequest<SystemCKernelBindEndpointRequest>
    && SequencedRequest<SystemCKernelElaborateRequest>
    && SequencedRequest<SystemCKernelStartRequest>
    && SequencedRequest<SystemCKernelApplyInputsRequest>
    && SequencedRequest<SystemCKernelAdvanceRequest>
    && SequencedRequest<SystemCKernelNextActivityRequest>
    && SequencedRequest<SystemCKernelDrainOutputsRequest>
    && SequencedRequest<SystemCKernelReportRequest>
    && SequencedRequest<SystemCKernelInspectRequest>
    && SequencedRequest<SystemCKernelSnapshotRequest>
    && SequencedRequest<SystemCKernelTeardownRequest>);
static_assert(std::same_as<
    decltype(SystemCKernelCreateSessionRequest::payload),
    SystemCKernelCreateSessionPayload>);
static_assert(std::same_as<decltype(SystemCKernelCreateObjectRequest::payload),
    SystemCKernelCreateObjectPayload>);
static_assert(std::same_as<decltype(SystemCKernelBindEndpointRequest::payload),
    SystemCKernelBindEndpointPayload>);
static_assert(std::same_as<decltype(SystemCKernelApplyInputsRequest::payload),
    SystemCKernelApplyInputsPayload>);
static_assert(std::same_as<decltype(SystemCKernelAdvanceRequest::payload),
    SystemCKernelAdvancePayload>);
static_assert(std::variant_size_v<SystemCKernelDirectReceipt> == 3U);
static_assert(std::is_same_v<std::variant_alternative_t<1U,
    SystemCKernelDirectReceipt>, SystemCKernelLifecycleReceipt>);
static_assert(std::is_same_v<std::variant_alternative_t<2U,
    SystemCKernelDirectReceipt>, SystemCKernelExecutionReceipt>);

void compile_direct_types_contract()
{
    const SystemCKernelDirectRequest request {
        SystemCKernelAdvanceRequest { { }, { }, { } }
    };
    const SystemCKernelDirectResult result {
        SystemCKernelDirectResultStatus::ok,
        SystemCKernelExecutionReceipt { }
    };
    static_cast<void>(request);
    static_cast<void>(result);
}

} // namespace

int main()
{
    compile_direct_types_contract();
}
