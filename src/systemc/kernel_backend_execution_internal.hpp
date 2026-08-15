// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/systemc/accellera.hpp"
#include "fsim/systemc/kernel_backend_execution.hpp"
#include "fsim/systemc/kernel_backend_value_endpoint.hpp"

#include <systemc.h>

#include <optional>
#include <string>

namespace fsim::systemc::detail {

[[nodiscard]] bool systemc_kernel_execution_limits_valid(
    const SystemCKernelExecutionLimits& limits,
    diagnostic::Engine& diagnostics);

struct SystemCKernelNativeObservation {
    SystemCKernelExecutionStatus status {
        SystemCKernelExecutionStatus::quiescent
    };
    std::uint64_t time_fs { };
    std::uint64_t delta { };
    bool current_activity { };
    bool future_activity { };
    std::optional<std::uint64_t> next_activity_time_fs;
};

[[nodiscard]] bool systemc_kernel_apply_scalar(
    backend_scalar_endpoint& endpoint,
    const SystemCKernelScalarValue& value,
    std::string& error) noexcept;

[[nodiscard]] std::optional<SystemCKernelScalarValue>
systemc_kernel_sample_scalar(
    const backend_scalar_endpoint& endpoint,
    std::string& error) noexcept;

[[nodiscard]] bool systemc_kernel_apply_value(
    backend_value_endpoint& endpoint, const SystemCKernelValue& value,
    std::string& error) noexcept;

[[nodiscard]] std::optional<SystemCKernelValue>
systemc_kernel_sample_value(
    const backend_value_endpoint& endpoint, std::string& error) noexcept;

[[nodiscard]] std::optional<SystemCKernelNativeObservation>
systemc_kernel_observe_native(sc_core::sc_simcontext& context,
    std::uint64_t time_resolution_fs, std::string& error) noexcept;

[[nodiscard]] std::optional<SystemCKernelNativeObservation>
systemc_kernel_advance_native(sc_core::sc_simcontext& context,
    const SystemCKernelAdvancePayload& payload,
    const SystemCKernelExecutionLimits& limits,
    std::uint64_t time_resolution_fs, std::string& error) noexcept;

} // namespace fsim::systemc::detail
