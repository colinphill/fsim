// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_synchronization.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::systemc {
namespace {

    bool report_error(diagnostic::Engine& diagnostics,
        const SystemCKernelSynchronizationCode code,
        const std::string_view message)
    {
        diagnostics.error(
            systemc_kernel_synchronization_diagnostic_code(code),
            std::string { message });
        return false;
    }

    bool known_host_language(const SystemCKernelHostLanguage language) noexcept
    {
        switch (language) {
        case SystemCKernelHostLanguage::verilog:
        case SystemCKernelHostLanguage::system_verilog:
        case SystemCKernelHostLanguage::vhdl:
            return true;
        }
        return false;
    }

    bool valid_limits(const SystemCKernelSynchronizationLimits& limits,
        diagnostic::Engine& diagnostics)
    {
        if (limits.max_islands == 0U || limits.max_inputs_per_batch == 0U
            || limits.max_outputs_per_island == 0U
            || limits.max_batches == 0U) {
            return report_error(diagnostics,
                SystemCKernelSynchronizationCode::resource,
                "SystemC synchronization limits must all be nonzero");
        }
        return true;
    }

    bool valid_endpoint(const SystemCKernelEndpointIdentity& endpoint) noexcept
    {
        return endpoint.hierarchy.valid() && endpoint.object.valid()
            && endpoint.endpoint.valid();
    }

    bool valid_scalar(const SystemCKernelScalarValue& value) noexcept
    {
        if (value.typed) {
            return value.bits == 0U && value.width == 0U && !value.is_signed;
        }
        return value.width > 0U && value.width <= 64U
            && (value.width == 64U || (value.bits >> value.width) == 0U);
    }

    bool known_region(const SystemCAccelleraRegion region) noexcept
    {
        using enum SystemCAccelleraRegion;
        return region == evaluate || region == update || region == notification
            || region == quiescent || region == terminal;
    }

    bool valid_execution_scalar(const SystemCKernelScalarValue& value,
        const SystemCKernelExecutionLimits& limits)
    {
        if (!valid_scalar(value)) {
            return false;
        }
        if (!value.typed) {
            return true;
        }
        diagnostic::Engine value_diagnostics;
        return validate_systemc_kernel_value(
            *value.typed, limits.value_limits, value_diagnostics);
    }

    bool valid_execution_order(const SystemCKernelExecutionOrder& order)
    {
        return known_region(order.region) && order.island.valid()
            && order.sequence.valid();
    }

    bool valid_execution_limits(const SystemCKernelExecutionLimits& limits)
    {
        constexpr auto maximum = static_cast<std::size_t>(
            std::numeric_limits<std::uint32_t>::max());
        const auto& values = limits.value_limits;
        return limits.max_samples_per_message > 0U
            && limits.max_samples_per_message <= maximum
            && limits.max_detail_bytes > 0U
            && limits.max_detail_bytes <= maximum
            && limits.max_delta_cycles_per_advance > 0U
            && limits.max_advance_fs > 0U && values.max_width_bits > 0U
            && values.max_encoded_bytes >= 56U
            && values.max_encoded_bytes <= maximum
            && values.max_type_name_bytes > 0U
            && values.max_type_name_bytes <= maximum
            && values.max_enum_literals > 0U
            && values.max_enum_literals <= maximum
            && values.max_enum_literal_bytes > 0U
            && values.max_enum_literal_bytes <= maximum
            && values.max_enum_text_bytes > 0U
            && values.max_enum_text_bytes <= maximum;
    }

    bool valid_execution_receipt(
        const SystemCKernelExecutionReceipt& receipt,
        const SystemCKernelExecutionLimits& limits,
        const SystemCIslandId expected_island)
    {
        if (receipt.session_state != SystemCKernelSessionState::quiescent
            || receipt.code != SystemCKernelExecutionCode::none
            || !receipt.published || !valid_execution_order(receipt.order)
            || receipt.order.island != expected_island
            || receipt.detail.size() > limits.max_detail_bytes
            || receipt.detail.find('\0') != std::string::npos
            || ((receipt.current_activity || receipt.future_activity)
                != receipt.next_activity_time_fs.has_value())
            || (receipt.next_activity_time_fs
                && *receipt.next_activity_time_fs < receipt.order.time_fs)
            || receipt.samples.size() > limits.max_samples_per_message) {
            return false;
        }

        std::set<SystemCEndpointId> endpoints;
        std::optional<SystemCKernelExecutionOrder> previous_order;
        for (const auto& sample : receipt.samples) {
            if (!sample.endpoint.valid()
                || !valid_execution_order(sample.order)
                || sample.order.island != expected_island
                || !valid_execution_scalar(sample.value, limits)
                || (previous_order && !(*previous_order < sample.order))
                || !endpoints.insert(sample.endpoint).second) {
                return false;
            }
            previous_order = sample.order;
        }
        return true;
    }

    struct IslandState {
        SystemCKernelSynchronizedIsland registration;
        std::unique_ptr<SystemCKernelBackend> backend;
    };

    struct ExchangeReceipt {
        SystemCSequenceId request_sequence;
        SystemCKernelExecutionReceipt receipt;
    };

    class Synchronizer final : public SystemCKernelSynchronizer {
    public:
        Synchronizer(SystemCKernelExecutionLimits execution_limits,
            SystemCKernelSynchronizationLimits synchronization_limits)
            : execution_limits_ { execution_limits }
            , synchronization_limits_ { synchronization_limits }
        {
        }

        ~Synchronizer() override { close(); }

        [[nodiscard]] bool attach_island(
            SystemCKernelSynchronizedIsland registration,
            std::unique_ptr<SystemCKernelBackend> backend,
            diagnostic::Engine& diagnostics) override
        {
            std::lock_guard lock { mutex_ };
            if (closed_ || failed_.load(std::memory_order_relaxed)
                || last_point_) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::order,
                    "SystemC synchronization cannot attach after execution or to a terminal coordinator");
            }
            if (!backend || !valid_registration(registration, diagnostics)) {
                if (!backend) {
                    report_error(diagnostics,
                        SystemCKernelSynchronizationCode::payload,
                        "SystemC synchronization requires a live serialized backend");
                }
                return false;
            }
            if (islands_.size() == synchronization_limits_.max_islands) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::resource,
                    "SystemC synchronization exceeds its island limit");
            }
            std::ranges::sort(registration.output_endpoints);
            if (islands_.contains(registration.island)) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::payload,
                    "SystemC synchronization duplicates an island identity");
            }
            const auto island = registration.island;
            islands_.emplace(island,
                IslandState { std::move(registration), std::move(backend) });
            for (const auto& endpoint :
                islands_.at(island).registration.output_endpoints) {
                outputs_.emplace_back(island, endpoint);
            }
            std::ranges::sort(outputs_);
            island_count_.store(islands_.size(), std::memory_order_relaxed);
            return true;
        }

        [[nodiscard]] std::optional<SystemCKernelSynchronizationReceipt>
        synchronize(const SystemCKernelSynchronizationPoint& point,
            const std::span<const SystemCKernelSynchronizationInput> inputs,
            diagnostic::Engine& diagnostics) override
        {
            std::lock_guard lock { mutex_ };
            if (!validate_batch(point, inputs, diagnostics)) {
                return std::nullopt;
            }
            auto ordered_inputs = std::vector<SystemCKernelSynchronizationInput> {
                inputs.begin(), inputs.end()
            };
            std::ranges::sort(ordered_inputs,
                [](const auto& lhs, const auto& rhs) {
                    return std::tie(lhs.island, lhs.target)
                        < std::tie(rhs.island, rhs.target);
                });
            SystemCKernelSynchronizationReceipt result;
            result.point = point;
            if (!apply_inputs(point, ordered_inputs, result, diagnostics)
                || !advance_islands(point, result, diagnostics)
                || !drain_outputs(point, result, diagnostics)) {
                return std::nullopt;
            }
            std::ranges::sort(result.dirty_outputs, { },
                &SystemCKernelExecutionSample::order);
            last_point_ = point;
            ++batches_;
            return result;
        }

        void close() noexcept override
        {
            std::lock_guard lock { mutex_ };
            close_locked();
        }

        [[nodiscard]] std::size_t island_count() const noexcept override
        {
            return island_count_.load(std::memory_order_relaxed);
        }

        [[nodiscard]] bool failed() const noexcept override
        {
            return failed_.load(std::memory_order_relaxed);
        }

    private:
        bool valid_registration(
            const SystemCKernelSynchronizedIsland& registration,
            diagnostic::Engine& diagnostics) const
        {
            if (!registration.island.valid()
                || !known_host_language(registration.host_language)
                || !registration.next_request_sequence.valid()) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::payload,
                    "SystemC synchronized island has an invalid identity, host language, or sequence");
            }
            if (registration.output_endpoints.size()
                > synchronization_limits_.max_outputs_per_island) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::resource,
                    "SystemC synchronized island exceeds its output endpoint limit");
            }
            auto endpoints = registration.output_endpoints;
            std::ranges::sort(endpoints);
            if (std::ranges::any_of(endpoints,
                    [](const auto& endpoint) {
                        return !valid_endpoint(endpoint);
                    })
                || std::adjacent_find(endpoints.begin(), endpoints.end())
                    != endpoints.end()) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::payload,
                    "SystemC synchronized island has an invalid or duplicate output endpoint");
            }
            return true;
        }

        bool validate_batch(const SystemCKernelSynchronizationPoint& point,
            const std::span<const SystemCKernelSynchronizationInput> inputs,
            diagnostic::Engine& diagnostics) const
        {
            if (closed_ || failed_.load(std::memory_order_relaxed)
                || islands_.empty()) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::order,
                    "SystemC synchronization requires a live nonempty coordinator");
            }
            if ((last_point_ && point <= *last_point_)
                || std::ranges::any_of(islands_, [&](const auto& entry) {
                       return point.time_fs
                           < entry.second.registration.current_time_fs;
                   })) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::order,
                    "SystemC synchronization points must advance in exact time and delta order");
            }
            if (batches_ >= synchronization_limits_.max_batches
                || inputs.size()
                    > synchronization_limits_.max_inputs_per_batch) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::resource,
                    "SystemC synchronization exceeds its batch or input limit");
            }
            auto ordered = std::vector<SystemCKernelSynchronizationInput> {
                inputs.begin(), inputs.end()
            };
            std::ranges::sort(ordered, [](const auto& lhs, const auto& rhs) {
                return std::tie(lhs.island, lhs.target)
                    < std::tie(rhs.island, rhs.target);
            });
            for (const auto& input : ordered) {
                if (!islands_.contains(input.island)
                    || !valid_endpoint(input.target)
                    || !valid_scalar(input.value)) {
                    return report_error(diagnostics,
                        SystemCKernelSynchronizationCode::payload,
                        "SystemC synchronization input has an unknown island, invalid endpoint, or invalid scalar");
                }
            }
            if (std::adjacent_find(ordered.begin(), ordered.end(),
                    [](const auto& lhs, const auto& rhs) {
                        return lhs.island == rhs.island
                            && lhs.target == rhs.target;
                    })
                != ordered.end()) {
                return report_error(diagnostics,
                    SystemCKernelSynchronizationCode::payload,
                    "SystemC synchronization input batch duplicates an endpoint");
            }
            return true;
        }

        std::optional<ExchangeReceipt> exchange(IslandState& island,
            const SystemCKernelDirectRequest& request,
            const bool allow_paused, std::string& error)
        {
            if (!valid_execution_limits(execution_limits_)) {
                error = "execution limits are invalid";
                return std::nullopt;
            }
            if (!island.registration.next_request_sequence.valid()) {
                error = "request sequence space is exhausted";
                return std::nullopt;
            }
            const auto sequence = island.registration.next_request_sequence;
            const auto request_matches = std::visit(
                [&](const auto& operation) {
                    return operation.island == island.registration.island
                        && operation.sequence == sequence;
                },
                request);
            if (!request_matches) {
                error = "direct request has the wrong island or sequence";
                return std::nullopt;
            }
            const auto response = island.backend->request(request);
            if (response.status != SystemCKernelDirectResultStatus::ok) {
                error = "direct backend rejected or disconnected";
                return std::nullopt;
            }
            const auto* receipt
                = std::get_if<SystemCKernelExecutionReceipt>(&response.receipt);
            if (receipt == nullptr
                || !valid_execution_receipt(
                    *receipt, execution_limits_, island.registration.island)) {
                error = "direct backend returned an invalid execution receipt";
                return std::nullopt;
            }
            const auto status_is_safe
                = receipt->status == SystemCKernelExecutionStatus::quiescent
                || (allow_paused
                    && receipt->status == SystemCKernelExecutionStatus::paused
                    && receipt->current_activity);
            if (!status_is_safe) {
                error = "direct backend execution receipt is not at a safe point";
                return std::nullopt;
            }
            if (sequence.value == std::numeric_limits<std::uint64_t>::max()) {
                island.registration.next_request_sequence = { };
            } else {
                ++island.registration.next_request_sequence.value;
            }
            return ExchangeReceipt { sequence, *receipt };
        }

        void append_exchange(const SystemCKernelSynchronizationPoint& point,
            const SystemCKernelCrossingStage stage,
            const SystemCEndpointId endpoint,
            const ExchangeReceipt& exchange_receipt,
            SystemCKernelSynchronizationReceipt& result)
        {
            result.crossings.push_back({ point, stage,
                exchange_receipt.receipt.order.island, endpoint,
                exchange_receipt.request_sequence,
                exchange_receipt.receipt.order });
        }

        bool apply_inputs(const SystemCKernelSynchronizationPoint& point,
            const std::vector<SystemCKernelSynchronizationInput>& inputs,
            SystemCKernelSynchronizationReceipt& result,
            diagnostic::Engine& diagnostics)
        {
            for (const auto& input : inputs) {
                auto& island = islands_.at(input.island);
                if (!valid_execution_scalar(input.value, execution_limits_)) {
                    return fail_locked(diagnostics,
                        "SystemC input batch contains a malformed typed value");
                }
                std::string error;
                const SystemCKernelDirectRequest request {
                    SystemCKernelApplyInputsRequest { input.island,
                        island.registration.next_request_sequence,
                        input.target.object, input.target.endpoint,
                        { input.value } }
                };
                const auto receipt = exchange(island, request, true, error);
                if (!receipt) {
                    return fail_locked(diagnostics,
                        "SystemC input batch failed before its kernel safe point: "
                            + error);
                }
                append_exchange(point, SystemCKernelCrossingStage::input_batch,
                    input.target.endpoint, *receipt, result);
            }
            return true;
        }

        bool advance_islands(const SystemCKernelSynchronizationPoint& point,
            SystemCKernelSynchronizationReceipt& result,
            diagnostic::Engine& diagnostics)
        {
            std::vector<SystemCIslandId> arrivals;
            for (auto& [identity, island] : islands_) {
                const auto duration
                    = point.time_fs - island.registration.current_time_fs;
                const SystemCKernelAdvancePayload advance {
                    duration == 0U ? SystemCKernelAdvanceKind::delta
                                   : SystemCKernelAdvanceKind::time,
                    duration
                };
                if ((advance.kind == SystemCKernelAdvanceKind::delta
                        && advance.duration_fs != 0U)
                    || (advance.kind == SystemCKernelAdvanceKind::time
                        && (advance.duration_fs == 0U
                            || advance.duration_fs
                                > execution_limits_.max_advance_fs))) {
                    return fail_locked(diagnostics,
                        "SystemC synchronization advance exceeds its governed limits");
                }
                std::string error;
                const SystemCKernelDirectRequest request {
                    SystemCKernelAdvanceRequest { identity,
                        island.registration.next_request_sequence, advance }
                };
                const auto receipt = exchange(island, request, true, error);
                if (!receipt
                    || receipt->receipt.order.time_fs != point.time_fs) {
                    return fail_locked(diagnostics,
                        "SystemC island failed to reach the exact kernel safe point: "
                            + error);
                }
                island.registration.current_time_fs = point.time_fs;
                if (duration > 0U) {
                    append_exchange(point,
                        SystemCKernelCrossingStage::kernel_arrival, { },
                        *receipt, result);
                    arrivals.push_back(identity);
                } else {
                    if (receipt->receipt.status
                            != SystemCKernelExecutionStatus::quiescent
                        || receipt->receipt.order.region
                            != SystemCAccelleraRegion::quiescent) {
                        return fail_locked(diagnostics,
                            "SystemC delta crossing did not reach quiescence");
                    }
                    append_exchange(point,
                        SystemCKernelCrossingStage::kernel_quiescent, { },
                        *receipt, result);
                    merge_activity(receipt->receipt, result);
                }
            }
            for (const auto identity : arrivals) {
                auto& island = islands_.at(identity);
                std::string error;
                const SystemCKernelDirectRequest request {
                    SystemCKernelAdvanceRequest { identity,
                        island.registration.next_request_sequence,
                        { SystemCKernelAdvanceKind::delta, 0U } }
                };
                const auto receipt = exchange(island, request, true, error);
                if (!receipt
                    || receipt->receipt.status
                        != SystemCKernelExecutionStatus::quiescent
                    || receipt->receipt.order.time_fs != point.time_fs
                    || receipt->receipt.order.region
                        != SystemCAccelleraRegion::quiescent) {
                    return fail_locked(diagnostics,
                        "SystemC evaluate/update/notification drain failed at the exact time boundary: "
                            + error);
                }
                append_exchange(point,
                    SystemCKernelCrossingStage::kernel_quiescent, { }, *receipt,
                    result);
                merge_activity(receipt->receipt, result);
            }
            return true;
        }

        void merge_activity(const SystemCKernelExecutionReceipt& receipt,
            SystemCKernelSynchronizationReceipt& result) const
        {
            result.current_activity
                = result.current_activity || receipt.current_activity;
            result.future_activity
                = result.future_activity || receipt.future_activity;
            if (receipt.next_activity_time_fs
                && (!result.next_activity_time_fs
                    || *receipt.next_activity_time_fs
                        < *result.next_activity_time_fs)) {
                result.next_activity_time_fs = receipt.next_activity_time_fs;
            }
        }

        bool drain_outputs(const SystemCKernelSynchronizationPoint& point,
            SystemCKernelSynchronizationReceipt& result,
            diagnostic::Engine& diagnostics)
        {
            for (const auto& [identity, endpoint] : outputs_) {
                auto& island = islands_.at(identity);
                std::string error;
                const SystemCKernelDirectRequest request {
                    SystemCKernelDrainOutputsRequest { identity,
                        island.registration.next_request_sequence,
                        endpoint.object, endpoint.endpoint }
                };
                const auto receipt = exchange(island, request, false, error);
                if (!receipt || receipt->receipt.samples.size() > 1U) {
                    return fail_locked(diagnostics,
                        "SystemC dirty-output batch failed after its kernel safe point: "
                            + error);
                }
                append_exchange(point, SystemCKernelCrossingStage::output_batch,
                    endpoint.endpoint, *receipt, result);
                if (receipt->receipt.samples.empty()) {
                    continue;
                }
                const auto& sample = receipt->receipt.samples.front();
                if (sample.endpoint != endpoint.endpoint || !sample.dirty
                    || sample.order.island != identity
                    || sample.order.time_fs != point.time_fs
                    || sample.order.region != SystemCAccelleraRegion::update) {
                    return fail_locked(diagnostics,
                        "SystemC dirty output lost its endpoint or update-region order");
                }
                result.dirty_outputs.push_back(sample);
            }
            return true;
        }

        bool fail_locked(diagnostic::Engine& diagnostics,
            const std::string_view message)
        {
            failed_.store(true, std::memory_order_relaxed);
            close_locked();
            return report_error(diagnostics,
                SystemCKernelSynchronizationCode::transport, message);
        }

        void close_locked() noexcept
        {
            if (closed_) {
                return;
            }
            closed_ = true;
            for (auto& [identity, island] : islands_) {
                static_cast<void>(identity);
                island.backend->close();
            }
            islands_.clear();
            outputs_.clear();
            island_count_.store(0U, std::memory_order_relaxed);
        }

        SystemCKernelExecutionLimits execution_limits_;
        SystemCKernelSynchronizationLimits synchronization_limits_;
        mutable std::mutex mutex_;
        bool closed_ { };
        std::atomic_bool failed_ { false };
        std::atomic_size_t island_count_ { 0U };
        std::uint64_t batches_ { };
        std::optional<SystemCKernelSynchronizationPoint> last_point_;
        std::map<SystemCIslandId, IslandState> islands_;
        std::vector<
            std::pair<SystemCIslandId, SystemCKernelEndpointIdentity>>
            outputs_;
    };

} // namespace

std::unique_ptr<SystemCKernelSynchronizer> make_systemc_kernel_synchronizer(
    const SystemCKernelProtocolLimits&,
    const SystemCKernelExecutionLimits& execution_limits,
    const SystemCKernelSynchronizationLimits& synchronization_limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(synchronization_limits, diagnostics)) {
        return nullptr;
    }
    return std::make_unique<Synchronizer>(
        execution_limits, synchronization_limits);
}

const char* systemc_kernel_synchronization_diagnostic_code(
    const SystemCKernelSynchronizationCode code) noexcept
{
    switch (code) {
    case SystemCKernelSynchronizationCode::none:
        return "";
    case SystemCKernelSynchronizationCode::order:
        return "FSIM-SC-N001";
    case SystemCKernelSynchronizationCode::payload:
        return "FSIM-SC-N002";
    case SystemCKernelSynchronizationCode::resource:
        return "FSIM-SC-N003";
    case SystemCKernelSynchronizationCode::transport:
        return "FSIM-SC-N004";
    }
    return "FSIM-SC-N001";
}

} // namespace fsim::systemc
