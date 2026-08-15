// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "application_trace_observation.hpp"
#include "fsim/systemc/kernel_backend_binding_inventory.hpp"

#include <systemc>

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace fsim::app::application_detail {

enum class SystemCTraceState : std::uint8_t {
    open,
    closed,
    failed,
};

enum class SystemCTraceCode : std::uint8_t {
    none,
    metadata,
    lifecycle,
    backpressure,
    writer,
};

struct SystemCTraceLimits {
    std::size_t maximum_routes { 1U << 20U };
    std::size_t maximum_aliases_per_route { 4096U };
    std::size_t maximum_pending_batches { 4096U };
    std::size_t maximum_values_per_batch { 4096U };
    std::size_t maximum_bits_per_batch { 1U << 24U };
    std::size_t maximum_records { 1U << 20U };
};

struct SystemCTraceRoute {
    systemc::SystemCEndpointId endpoint;
    runtime::TraceSignalId signal;
    std::vector<runtime::TraceSignalId> aliases;
    bool initially_enabled { };
};

struct SystemCTraceDirtyValue {
    systemc::SystemCEndpointId endpoint;
    runtime::PackedLogic4 value;
};

struct SystemCTraceStatus {
    SystemCTraceState state { SystemCTraceState::open };
    SystemCTraceCode code { SystemCTraceCode::none };
    std::size_t pending_batches { };
    std::uint64_t accepted_batches { };
    std::uint64_t delivered_batches { };
    std::uint64_t backpressure_events { };
    std::string detail;
};

class SystemCTracePipeline final {
public:
    SystemCTracePipeline(
        const runtime::TraceDeclarationModel& declarations,
        const systemc::SystemCKernelChannelInventorySnapshot& channels,
        const systemc::SystemCKernelBindingInventorySnapshot& bindings,
        std::span<const SystemCTraceRoute> routes,
        std::ostream& vcd_output,
        std::ostream& fst_output,
        SystemCTraceLimits limits = { });
    ~SystemCTracePipeline();

    SystemCTracePipeline(const SystemCTracePipeline&) = delete;
    SystemCTracePipeline& operator=(const SystemCTracePipeline&) = delete;
    SystemCTracePipeline(SystemCTracePipeline&&) = delete;
    SystemCTracePipeline& operator=(SystemCTracePipeline&&) = delete;

    [[nodiscard]] bool selected(systemc::SystemCEndpointId endpoint) const
        noexcept;
    [[nodiscard]] bool set_enabled(systemc::SystemCEndpointId endpoint,
        bool enable, runtime::SimulationTick time, std::uint64_t delta,
        const runtime::PackedLogic4& snapshot);
    [[nodiscard]] bool try_post_update(systemc::SystemCEndpointId endpoint,
        runtime::SimulationTick time, std::uint64_t delta,
        const runtime::PackedLogic4& value);
    [[nodiscard]] bool try_post_update_batch(runtime::SimulationTick time,
        std::uint64_t delta, std::span<const SystemCTraceDirtyValue> values);

    void flush();
    void close(runtime::SimulationTick final_time);

    [[nodiscard]] SystemCTraceStatus status() const;
    [[nodiscard]] std::span<const TraceObservationRecord> observations()
        const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] runtime::PackedLogic4 systemc_trace_value(
    const systemc::SystemCKernelValue& value);

[[nodiscard]] inline runtime::PackedLogic4 systemc_trace_value(const bool value)
{
    return runtime::PackedLogic4::from_aval_bval(1U, value ? 1U : 0U, 0U);
}

[[nodiscard]] inline runtime::PackedLogic4 systemc_trace_value(
    const sc_dt::sc_bit value)
{
    return systemc_trace_value(value.to_bool());
}

[[nodiscard]] inline runtime::PackedLogic4 systemc_trace_value(
    const sc_dt::sc_logic value)
{
    switch (value.to_char()) {
    case '0':
        return runtime::PackedLogic4::from_msb_string("0");
    case '1':
        return runtime::PackedLogic4::from_msb_string("1");
    case 'Z':
        return runtime::PackedLogic4::from_msb_string("Z");
    default:
        return runtime::PackedLogic4::from_msb_string("X");
    }
}

template <typename Integral>
    requires(std::is_integral_v<Integral>
        && !std::is_same_v<std::remove_cv_t<Integral>, bool>)
[[nodiscard]] runtime::PackedLogic4 systemc_trace_value(const Integral value)
{
    using Unsigned = std::make_unsigned_t<Integral>;
    return runtime::PackedLogic4::from_aval_bval(sizeof(Integral) * 8U,
        static_cast<std::uint64_t>(static_cast<Unsigned>(value)), 0U);
}

template <int Width>
[[nodiscard]] runtime::PackedLogic4 systemc_trace_value(
    const sc_dt::sc_lv<Width>& value)
{
    runtime::PackedLogic4 packed(static_cast<std::size_t>(Width));
    for (int bit = 0; bit < Width; ++bit) {
        const auto parsed = runtime::parse_logic4(value[bit].to_char());
        packed.set(static_cast<std::size_t>(bit),
            parsed.value_or(runtime::Logic4::x));
    }
    return packed;
}

template <int Width>
[[nodiscard]] runtime::PackedLogic4 systemc_trace_value(
    const sc_dt::sc_bv<Width>& value)
{
    runtime::PackedLogic4 packed(static_cast<std::size_t>(Width),
        runtime::Logic4::zero);
    for (int bit = 0; bit < Width; ++bit) {
        packed.set(static_cast<std::size_t>(bit), value[bit].to_bool() ? runtime::Logic4::one : runtime::Logic4::zero);
    }
    return packed;
}

template <typename Value, int Width>
[[nodiscard]] runtime::PackedLogic4 systemc_trace_integral_vector(
    const Value& value)
{
    runtime::PackedLogic4 packed(static_cast<std::size_t>(Width),
        runtime::Logic4::zero);
    for (int bit = 0; bit < Width; ++bit) {
        packed.set(static_cast<std::size_t>(bit), value[bit].to_bool() ? runtime::Logic4::one : runtime::Logic4::zero);
    }
    return packed;
}

template <int Width>
[[nodiscard]] runtime::PackedLogic4 systemc_trace_value(
    const sc_dt::sc_int<Width>& value)
{
    return systemc_trace_integral_vector<sc_dt::sc_int<Width>, Width>(value);
}

template <int Width>
[[nodiscard]] runtime::PackedLogic4 systemc_trace_value(
    const sc_dt::sc_uint<Width>& value)
{
    return systemc_trace_integral_vector<sc_dt::sc_uint<Width>, Width>(value);
}

template <int Width>
[[nodiscard]] runtime::PackedLogic4 systemc_trace_value(
    const sc_dt::sc_bigint<Width>& value)
{
    return systemc_trace_integral_vector<sc_dt::sc_bigint<Width>, Width>(value);
}

template <int Width>
[[nodiscard]] runtime::PackedLogic4 systemc_trace_value(
    const sc_dt::sc_biguint<Width>& value)
{
    return systemc_trace_integral_vector<sc_dt::sc_biguint<Width>, Width>(value);
}

template <typename Value>
class AccelleraSystemCTraceHook final : public sc_core::sc_module {
public:
    AccelleraSystemCTraceHook(sc_core::sc_module_name name,
        const sc_core::sc_signal_in_if<Value>& channel,
        SystemCTracePipeline& pipeline, systemc::SystemCEndpointId endpoint)
        : sc_core::sc_module(name)
        , channel_(&channel)
        , pipeline_(&pipeline)
        , endpoint_(endpoint)
    {
        SC_METHOD(observe_post_update);
        sensitive << channel.value_changed_event();
        dont_initialize();
    }

    [[nodiscard]] std::uint64_t capture_attempts() const noexcept
    {
        return capture_attempts_;
    }

    [[nodiscard]] std::uint64_t backpressure_events() const noexcept
    {
        return backpressure_events_;
    }

    [[nodiscard]] std::uint64_t callback_failures() const noexcept
    {
        return callback_failures_;
    }

private:
    void observe_post_update()
    {
        if (!pipeline_->selected(endpoint_)) {
            return;
        }
        ++capture_attempts_;
        try {
            const auto value = systemc_trace_value(channel_->read());
            if (!pipeline_->try_post_update(endpoint_,
                    static_cast<runtime::SimulationTick>(
                        sc_core::sc_time_stamp().value()),
                    sc_core::sc_delta_count(), value)) {
                ++backpressure_events_;
            }
        } catch (...) {
            ++callback_failures_;
        }
    }

    const sc_core::sc_signal_in_if<Value>* channel_;
    SystemCTracePipeline* pipeline_;
    systemc::SystemCEndpointId endpoint_;
    std::uint64_t capture_attempts_ { };
    std::uint64_t backpressure_events_ { };
    std::uint64_t callback_failures_ { };
};

[[nodiscard]] const char* systemc_trace_diagnostic_code(
    SystemCTraceCode code) noexcept;

} // namespace fsim::app::application_detail
