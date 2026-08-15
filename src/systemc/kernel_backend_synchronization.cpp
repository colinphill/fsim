// SPDX-License-Identifier: Apache-2.0
#include "fsim/systemc/kernel_backend_synchronization.hpp"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <ranges>
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
        Synchronizer(SystemCKernelProtocolLimits protocol_limits,
            SystemCKernelExecutionLimits execution_limits,
            SystemCKernelSynchronizationLimits synchronization_limits)
            : protocol_limits_ { protocol_limits }
            , execution_limits_ { execution_limits }
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
            const SystemCKernelOperation operation,
            const SystemCKernelEndpointIdentity& endpoint,
            std::vector<std::byte> payload, std::string& error)
        {
            if (!island.registration.next_request_sequence.valid()) {
                error = "request sequence space is exhausted";
                return std::nullopt;
            }
            SystemCKernelMessage request;
            request.header.operation = operation;
            request.header.direction = SystemCKernelMessageDirection::request;
            request.header.sequence
                = island.registration.next_request_sequence;
            request.header.island = island.registration.island;
            request.header.hierarchy = endpoint.hierarchy;
            request.header.object = endpoint.object;
            request.header.endpoint = endpoint.endpoint;
            request.payload = std::move(payload);
            diagnostic::Engine local_diagnostics;
            const auto encoded = serialize_systemc_kernel_message(
                request, protocol_limits_, local_diagnostics);
            if (!encoded) {
                error = "request serialization failed";
                return std::nullopt;
            }
            const auto transport = island.backend->exchange(*encoded);
            if (transport.status != SystemCKernelTransportStatus::ok) {
                error = "serialized backend rejected or disconnected";
                return std::nullopt;
            }
            const auto response = deserialize_systemc_kernel_message(
                transport.bytes, protocol_limits_, local_diagnostics);
            if (!response || response->header.direction != SystemCKernelMessageDirection::response
                || response->header.status != SystemCKernelMessageStatus::ok
                || response->header.operation != operation
                || response->header.correlation != request.header.sequence
                || response->header.island != request.header.island
                || response->header.hierarchy != request.header.hierarchy
                || response->header.object != request.header.object
                || response->header.endpoint != request.header.endpoint
                || response->header.transaction != request.header.transaction) {
                error = "backend response identity or correlation is invalid";
                return std::nullopt;
            }
            const auto receipt = deserialize_systemc_execution_receipt(
                response->payload, execution_limits_, local_diagnostics);
            if (!receipt) {
                error = "backend execution receipt is malformed";
                return std::nullopt;
            }
            const auto status_is_safe
                = receipt->status == SystemCKernelExecutionStatus::quiescent
                || ((operation == SystemCKernelOperation::apply_inputs
                        || operation == SystemCKernelOperation::advance)
                    && receipt->status
                        == SystemCKernelExecutionStatus::paused
                    && receipt->current_activity);
            if (receipt->session_state != SystemCKernelSessionState::quiescent
                || !status_is_safe
                || receipt->code != SystemCKernelExecutionCode::none
                || !receipt->published) {
                error = "backend execution receipt has session/status/code/published values "
                    + std::to_string(static_cast<unsigned>(receipt->session_state))
                    + "/"
                    + std::to_string(static_cast<unsigned>(receipt->status))
                    + "/"
                    + std::to_string(static_cast<unsigned>(receipt->code))
                    + "/" + (receipt->published ? "true" : "false");
                return std::nullopt;
            }
            if (receipt->order.island != island.registration.island) {
                error = "backend execution receipt has the wrong island order";
                return std::nullopt;
            }
            const auto sequence = request.header.sequence;
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
                diagnostic::Engine local_diagnostics;
                std::string error;
                const auto payload = serialize_systemc_apply_inputs_payload(
                    { input.value }, execution_limits_, local_diagnostics);
                const auto receipt = payload
                    ? exchange(island, SystemCKernelOperation::apply_inputs,
                          input.target, *payload, error)
                    : std::nullopt;
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
                diagnostic::Engine local_diagnostics;
                std::string error;
                const auto payload = serialize_systemc_advance_payload(
                    advance, execution_limits_, local_diagnostics);
                const auto receipt = payload
                    ? exchange(island, SystemCKernelOperation::advance, { },
                          *payload, error)
                    : std::nullopt;
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
                diagnostic::Engine local_diagnostics;
                std::string error;
                const auto payload = serialize_systemc_advance_payload(
                    { SystemCKernelAdvanceKind::delta, 0U }, execution_limits_,
                    local_diagnostics);
                const auto receipt = payload
                    ? exchange(island, SystemCKernelOperation::advance, { },
                          *payload, error)
                    : std::nullopt;
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
                const auto receipt = exchange(island,
                    SystemCKernelOperation::drain_outputs, endpoint, { }, error);
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

        SystemCKernelProtocolLimits protocol_limits_;
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
    const SystemCKernelProtocolLimits& protocol_limits,
    const SystemCKernelExecutionLimits& execution_limits,
    const SystemCKernelSynchronizationLimits& synchronization_limits,
    diagnostic::Engine& diagnostics)
{
    if (!valid_limits(synchronization_limits, diagnostics)) {
        return nullptr;
    }
    return std::make_unique<Synchronizer>(protocol_limits, execution_limits,
        synchronization_limits);
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
