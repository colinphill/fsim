// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_execution.hpp"

#include "kernel_backend_execution_internal.hpp"

#include <limits>
#include <string_view>

namespace fsim::systemc {
namespace {

    bool report_resource_error(
        diagnostic::Engine& diagnostics, const std::string_view message)
    {
        diagnostics.error("FSIM-SC-E003", std::string { message });
        return false;
    }

    bool valid_limits(const SystemCKernelExecutionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        constexpr auto maximum = static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max());
        if (limits.max_samples_per_message == 0U
            || limits.max_samples_per_message > maximum
            || limits.max_detail_bytes == 0U
            || limits.max_detail_bytes > maximum
            || limits.max_delta_cycles_per_advance == 0U
            || limits.max_advance_fs == 0U
            || limits.value_limits.max_width_bits == 0U
            || limits.value_limits.max_encoded_bytes < 56U
            || limits.value_limits.max_encoded_bytes > maximum
            || limits.value_limits.max_type_name_bytes == 0U
            || limits.value_limits.max_type_name_bytes > maximum
            || limits.value_limits.max_enum_literals == 0U
            || limits.value_limits.max_enum_literals > maximum
            || limits.value_limits.max_enum_literal_bytes == 0U
            || limits.value_limits.max_enum_literal_bytes > maximum
            || limits.value_limits.max_enum_text_bytes == 0U
            || limits.value_limits.max_enum_text_bytes > maximum) {
            return report_resource_error(diagnostics,
                "SystemC execution limits must be nonzero bounded values");
        }
        return true;
    }

} // namespace

const char* systemc_kernel_execution_diagnostic_code(
    const SystemCKernelExecutionCode code) noexcept
{
    switch (code) {
    case SystemCKernelExecutionCode::none:
        return "";
    case SystemCKernelExecutionCode::state:
        return "FSIM-SC-E001";
    case SystemCKernelExecutionCode::payload:
        return "FSIM-SC-E002";
    case SystemCKernelExecutionCode::resource:
        return "FSIM-SC-E003";
    case SystemCKernelExecutionCode::upstream:
        return "FSIM-SC-E004";
    }
    return "FSIM-SC-E001";
}

namespace detail {

    bool systemc_kernel_execution_limits_valid(
        const SystemCKernelExecutionLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        return valid_limits(limits, diagnostics);
    }

    bool systemc_kernel_apply_scalar(backend_scalar_endpoint& endpoint,
        const SystemCKernelScalarValue& value, std::string& error) noexcept
    {
        try {
            if (endpoint.backend_direction() != backend_endpoint_direction::input) {
                error = "SystemC input application targeted a non-input endpoint";
                return false;
            }
            if (!endpoint.apply_backend_scalar(
                    value.bits, value.width, value.is_signed)) {
                error = "SystemC input value does not match the bound scalar endpoint";
                return false;
            }
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC scalar-input failure";
        }
        return false;
    }

    std::optional<SystemCKernelScalarValue> systemc_kernel_sample_scalar(
        const backend_scalar_endpoint& endpoint, std::string& error) noexcept
    {
        try {
            SystemCKernelScalarValue value;
            value.width = endpoint.backend_width();
            value.is_signed = endpoint.backend_signed();
            if (!endpoint.sample_backend_scalar(value.bits)) {
                error = "SystemC scalar endpoint cannot be sampled";
                return std::nullopt;
            }
            return value;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC scalar-sampling failure";
        }
        return std::nullopt;
    }

    bool systemc_kernel_apply_value(backend_value_endpoint& endpoint,
        const SystemCKernelValue& value, std::string& error) noexcept
    {
        try {
            if (endpoint.backend_direction()
                != backend_endpoint_direction::input) {
                error = "SystemC typed input application targeted a non-input endpoint";
                return false;
            }
            if (!endpoint.apply_backend_value(value)) {
                error = "SystemC typed input does not match the bound value endpoint";
                return false;
            }
            return true;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC typed-input failure";
        }
        return false;
    }

    std::optional<SystemCKernelValue> systemc_kernel_sample_value(
        const backend_value_endpoint& endpoint, std::string& error) noexcept
    {
        try {
            auto value = endpoint.sample_backend_value();
            if (!value) {
                error = "SystemC value endpoint cannot be sampled";
                return std::nullopt;
            }
            return value;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC value-sampling failure";
        }
        return std::nullopt;
    }

    namespace {

        std::optional<std::uint64_t> to_femtoseconds(const std::uint64_t ticks,
            const std::uint64_t resolution, std::string& error)
        {
            if (resolution == 0U
                || ticks > std::numeric_limits<std::uint64_t>::max() / resolution) {
                error = "upstream SystemC time exceeds the execution protocol range";
                return std::nullopt;
            }
            return ticks * resolution;
        }

        SystemCKernelExecutionStatus native_status(
            const sc_core::sc_simcontext& context)
        {
            if (context.sim_status() == sc_core::SC_SIM_ERROR) {
                return SystemCKernelExecutionStatus::error;
            }
            const auto status = context.get_status();
            if (context.sim_status() == sc_core::SC_SIM_USER_STOP
                || status == sc_core::SC_STOPPED
                || status == sc_core::SC_END_OF_SIMULATION) {
                return SystemCKernelExecutionStatus::stopped;
            }
            if (status == sc_core::SC_RUNNING) {
                return SystemCKernelExecutionStatus::running;
            }
            if (status == sc_core::SC_SUSPENDED
                || (status == sc_core::SC_PAUSED
                    && context.pending_activity_at_current_time())) {
                return SystemCKernelExecutionStatus::paused;
            }
            return SystemCKernelExecutionStatus::quiescent;
        }

        bool terminal_status(const SystemCKernelExecutionStatus status) noexcept
        {
            return status == SystemCKernelExecutionStatus::stopped
                || status == SystemCKernelExecutionStatus::error;
        }

        bool observe_next_activity(sc_core::sc_simcontext& context,
            const std::uint64_t time_resolution_fs,
            SystemCKernelNativeObservation& result, std::string& error)
        {
            if (result.current_activity) {
                result.next_activity_time_fs = result.time_fs;
                return true;
            }
            if (!result.future_activity) {
                return true;
            }
            const auto distance = sc_core::sc_time_to_pending_activity(&context);
            if (distance.value() > std::numeric_limits<std::uint64_t>::max()
                    - context.time_stamp().value()) {
                error = "next upstream SystemC activity exceeds the protocol range";
                return false;
            }
            result.next_activity_time_fs = to_femtoseconds(
                context.time_stamp().value() + distance.value(),
                time_resolution_fs, error);
            return result.next_activity_time_fs.has_value();
        }

    } // namespace

    std::optional<SystemCKernelNativeObservation> systemc_kernel_observe_native(
        sc_core::sc_simcontext& context, const std::uint64_t time_resolution_fs,
        std::string& error) noexcept
    {
        try {
            SystemCKernelNativeObservation result;
            result.status = native_status(context);
            const auto time = to_femtoseconds(
                context.time_stamp().value(), time_resolution_fs, error);
            if (!time) {
                return std::nullopt;
            }
            result.time_fs = *time;
            result.delta = sc_core::sc_delta_count();
            result.current_activity = context.pending_activity_at_current_time();
            result.future_activity = sc_core::sc_pending_activity_at_future_time(
                &context);
            if (!observe_next_activity(
                    context, time_resolution_fs, result, error)) {
                return std::nullopt;
            }
            return result;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC observation failure";
        }
        return std::nullopt;
    }

    namespace {

        std::optional<SystemCKernelNativeObservation> advance_delta(
            sc_core::sc_simcontext& context,
            SystemCKernelNativeObservation observation,
            const SystemCKernelExecutionLimits& limits,
            const std::uint64_t time_resolution_fs, std::string& error)
        {
            std::uint64_t cycles { };
            while (observation.current_activity
                && cycles < limits.max_delta_cycles_per_advance) {
                sc_core::sc_start(sc_core::SC_ZERO_TIME,
                    sc_core::SC_EXIT_ON_STARVATION);
                ++cycles;
                auto next = systemc_kernel_observe_native(
                    context, time_resolution_fs, error);
                if (!next) {
                    return std::nullopt;
                }
                observation = *next;
                if (terminal_status(observation.status)) {
                    break;
                }
            }
            if (observation.current_activity
                && !terminal_status(observation.status)) {
                error = "SystemC delta advancement exhausted its governed cycle limit";
                return std::nullopt;
            }
            return observation;
        }

        std::optional<SystemCKernelNativeObservation> advance_time(
            sc_core::sc_simcontext& context,
            const SystemCKernelNativeObservation& before,
            const SystemCKernelAdvancePayload& payload,
            const SystemCKernelExecutionLimits& limits,
            const std::uint64_t time_resolution_fs, std::string& error)
        {
            if (payload.duration_fs == 0U
                || payload.duration_fs > limits.max_advance_fs
                || time_resolution_fs == 0U
                || payload.duration_fs % time_resolution_fs != 0U) {
                error = "time advancement is not exact at the session resolution";
                return std::nullopt;
            }
            if (before.time_fs > std::numeric_limits<std::uint64_t>::max()
                    - payload.duration_fs) {
                error = "requested SystemC advancement exceeds the protocol range";
                return std::nullopt;
            }
            const auto duration = sc_core::sc_time::from_value(
                payload.duration_fs / time_resolution_fs);
            sc_core::sc_start(duration, sc_core::SC_RUN_TO_TIME);
            auto after = systemc_kernel_observe_native(
                context, time_resolution_fs, error);
            if (!after) {
                return std::nullopt;
            }
            const auto target_time_fs = before.time_fs + payload.duration_fs;
            if (after->time_fs < target_time_fs
                && !terminal_status(after->status)) {
                after->status = SystemCKernelExecutionStatus::paused;
            }
            return after;
        }

    } // namespace

    std::optional<SystemCKernelNativeObservation> systemc_kernel_advance_native(
        sc_core::sc_simcontext& context,
        const SystemCKernelAdvancePayload& payload,
        const SystemCKernelExecutionLimits& limits,
        const std::uint64_t time_resolution_fs, std::string& error) noexcept
    {
        try {
            auto before = systemc_kernel_observe_native(
                context, time_resolution_fs, error);
            if (!before) {
                return std::nullopt;
            }
            if (terminal_status(before->status)) {
                error = "upstream SystemC execution is already terminal";
                return std::nullopt;
            }
            if (payload.kind == SystemCKernelAdvanceKind::delta) {
                if (payload.duration_fs != 0U) {
                    error = "delta advancement requires a zero duration";
                    return std::nullopt;
                }
                return advance_delta(context, *before, limits,
                    time_resolution_fs, error);
            }
            if (payload.kind == SystemCKernelAdvanceKind::time) {
                return advance_time(context, *before, payload, limits,
                    time_resolution_fs, error);
            }
            error = "SystemC advancement has an unknown operation kind";
            return std::nullopt;
        } catch (const std::exception& exception) {
            error = exception.what();
        } catch (...) {
            error = "unknown upstream SystemC advancement failure";
        }
        return std::nullopt;
    }

} // namespace detail
} // namespace fsim::systemc
