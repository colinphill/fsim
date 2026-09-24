// SPDX-License-Identifier: Apache-2.0
#include "application_trace_observation.hpp"

#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace fsim::app::application_detail {
namespace {

    [[nodiscard]] bool valid_kind(const TraceObservationKind kind) noexcept
    {
        switch (kind) {
        case TraceObservationKind::Signal:
        case TraceObservationKind::Uvm:
        case TraceObservationKind::DynamicClass:
        case TraceObservationKind::Container:
        case TraceObservationKind::Coverage:
        case TraceObservationKind::Assertion:
        case TraceObservationKind::Sdf:
            return true;
        }
        return false;
    }

    [[nodiscard]] bool valid_region(const runtime::TraceRegion region) noexcept
    {
        switch (region) {
        case runtime::TraceRegion::Snapshot:
        case runtime::TraceRegion::Preponed:
        case runtime::TraceRegion::Active:
        case runtime::TraceRegion::Inactive:
        case runtime::TraceRegion::NonblockingAssign:
        case runtime::TraceRegion::Observed:
        case runtime::TraceRegion::Reactive:
        case runtime::TraceRegion::Postponed:
        case runtime::TraceRegion::Callback:
            return true;
        }
        return false;
    }

    void validate_request(
        const runtime::TraceDeclarationModel& declarations,
        const TraceObservationLimits& limits,
        const TraceObservationKind kind,
        const runtime::TraceRegion region,
        const std::string_view identity,
        const std::span<const TraceObservationValue> values)
    {
        if (!valid_kind(kind) || !valid_region(region)) {
            throw std::invalid_argument("trace observation kind or region is invalid");
        }
        if (identity.empty() || identity.size() > limits.maximum_identity_bytes
            || identity.find('\0') != std::string_view::npos) {
            throw std::invalid_argument("trace observation identity is invalid");
        }
        if (values.empty() || values.size() > limits.maximum_values_per_record) {
            throw std::length_error("trace observation value count is invalid");
        }
        std::set<std::uint64_t> signals;
        std::size_t payload_bits = 0U;
        for (const auto& observed : values) {
            if (observed.signal.value == 0U
                || !signals.insert(observed.signal.value).second) {
                throw std::invalid_argument(
                    "trace observation signal identity is invalid or duplicated");
            }
            const auto& variable = [&]() -> const runtime::TraceVariableDeclaration& {
                try {
                    return declarations.variable(observed.signal);
                } catch (const std::out_of_range&) {
                    throw std::invalid_argument(
                        "trace observation signal is not declared");
                }
            }();
            const auto& type = declarations.type(variable.type);
            if (type.kind == runtime::TraceTypeKind::SystemVerilogString
                || observed.value.width() != type.width) {
                throw std::invalid_argument(
                    "trace observation value does not match its declaration");
            }
            if (observed.value.width() > limits.maximum_payload_bits - payload_bits) {
                throw std::length_error("trace observation payload limit exceeded");
            }
            payload_bits += observed.value.width();
        }
    }

} // namespace

TraceObservationRecorder::TraceObservationRecorder(
    const runtime::TraceDeclarationModel& declarations,
    TraceObservationLimits limits,
    const TraceObservationRetention retention)
    : declarations_(&declarations)
    , limits_(limits)
    , retention_(retention)
{
    if (limits_.maximum_records == 0U
        || limits_.maximum_values_per_record == 0U
        || limits_.maximum_payload_bits == 0U
        || limits_.maximum_identity_bytes == 0U
        || limits_.maximum_observers == 0U) {
        throw std::invalid_argument("trace observation limits must be positive");
    }
    if (retention_ != TraceObservationRetention::Streaming
        && retention_ != TraceObservationRetention::BoundedCapture) {
        throw std::invalid_argument("trace observation retention is invalid");
    }
}

std::uint64_t TraceObservationRecorder::add_observer(Observer observer)
{
    if (fanning_out_) {
        throw std::logic_error(
            "trace observation fanout cannot mutate its observer set");
    }
    if (!observer) {
        throw std::invalid_argument("trace observation observer cannot be empty");
    }
    if (observers_.size() >= limits_.maximum_observers) {
        throw std::length_error("trace observation observer limit exceeded");
    }
    if (next_observer_ == 0U) {
        throw std::overflow_error("trace observation observer ids exhausted");
    }
    const auto token = next_observer_++;
    observers_.emplace(token, std::move(observer));
    return token;
}

void TraceObservationRecorder::remove_observer(const std::uint64_t token)
{
    if (fanning_out_) {
        throw std::logic_error(
            "trace observation fanout cannot mutate its observer set");
    }
    observers_.erase(token);
}

std::uint64_t TraceObservationRecorder::claim_sequence()
{
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("trace observation sequence ids exhausted");
    }
    return next_sequence_++;
}

bool TraceObservationRecorder::would_reorder(
    const runtime::SimulationTick time,
    const std::uint64_t delta,
    const runtime::TraceRegion region) const noexcept
{
    if (!maximum_order_) {
        return false;
    }
    return std::tuple { time, delta, static_cast<std::uint8_t>(region) }
        < *maximum_order_;
}

void TraceObservationRecorder::note_order(
    const runtime::SimulationTick time,
    const std::uint64_t delta,
    const runtime::TraceRegion region) noexcept
{
    const auto order
        = std::tuple { time, delta, static_cast<std::uint8_t>(region) };
    if (!maximum_order_ || *maximum_order_ < order) {
        maximum_order_ = order;
    }
}

std::uint64_t TraceObservationRecorder::accept(
    const TraceObservationKind kind,
    const runtime::SimulationTick time,
    const std::uint64_t delta,
    const runtime::TraceRegion region,
    std::string identity,
    const std::span<const TraceObservationValue> values)
{
    if (fanning_out_) {
        throw std::logic_error("trace observation fanout cannot be reentrant");
    }
    validate_request(
        *declarations_, limits_, kind, region, identity, values);
    if (retention_ == TraceObservationRetention::BoundedCapture
        && records_.size() >= limits_.maximum_records) {
        throw std::length_error("trace observation record limit exceeded");
    }

    TraceObservationRecord record;
    record.kind = kind;
    record.time = time;
    record.delta = delta;
    record.region = region;
    record.sequence = claim_sequence();
    record.identity = std::move(identity);
    record.values.assign(values.begin(), values.end());
    const auto sequence = record.sequence;
    if (retention_ == TraceObservationRetention::BoundedCapture) {
        records_.push_back(std::move(record));
        note_order(time, delta, region);
        fan_out(records_.back());
    } else {
        note_order(time, delta, region);
        fan_out(record);
    }
    return sequence;
}

void TraceObservationRecorder::fan_out(
    const TraceObservationRecord& record) noexcept
{
    fanning_out_ = true;
    for (const auto& [token, observer] : observers_) {
        static_cast<void>(token);
        try {
            observer(record);
        } catch (...) {
            if (!callback_failure_) {
                callback_failure_ = std::current_exception();
            }
            if (callback_failures_
                != std::numeric_limits<std::uint64_t>::max()) {
                ++callback_failures_;
            }
        }
    }
    fanning_out_ = false;
}

std::span<const TraceObservationRecord>
TraceObservationRecorder::records() const noexcept
{
    return records_;
}

std::uint64_t TraceObservationRecorder::callback_failures() const noexcept
{
    return callback_failures_;
}

std::exception_ptr TraceObservationRecorder::callback_failure() const noexcept
{
    return callback_failure_;
}

} // namespace fsim::app::application_detail
