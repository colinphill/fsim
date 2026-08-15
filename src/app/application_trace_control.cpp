// SPDX-License-Identifier: Apache-2.0
#include "application_trace_control.hpp"

#include "application_trace_observation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace fsim::app::application_detail {
namespace {

    void validate_owner(
        const std::string_view owner,
        const TraceSelectionLimits& limits,
        std::size_t& total_owner_bytes)
    {
        if (owner.empty() || owner.size() > limits.maximum_owner_bytes
            || owner.find('\0') != std::string_view::npos) {
            throw std::invalid_argument(
                "trace selection declaration owner is invalid");
        }
        if (owner.size() > limits.maximum_total_owner_bytes - total_owner_bytes) {
            throw std::length_error(
                "trace selection declaration ownership exceeds its byte limit");
        }
        total_owner_bytes += owner.size();
    }

    void require_append_only_snapshot(
        const TraceObservationRecorder& observations,
        const runtime::SimulationTick time,
        const std::uint64_t delta)
    {
        const auto records = observations.records();
        const auto precedes_existing = std::ranges::any_of(
            records, [&](const TraceObservationRecord& record) {
                return std::tuple { time, delta,
                    static_cast<std::uint8_t>(
                        runtime::TraceRegion::Callback) }
                < std::tuple { record.time, record.delta,
                      static_cast<std::uint8_t>(record.region) };
            });
        if (precedes_existing) {
            throw std::logic_error(
                "late trace snapshot would reorder an accepted record");
        }
    }

} // namespace

TraceSelectionControl::TraceSelectionControl(
    const runtime::TraceDeclarationModel& declarations,
    const std::span<const TraceSelectionDeclaration> signals,
    TraceSelectionLimits limits)
{
    if (limits.maximum_signals == 0U
        || limits.maximum_owner_bytes == 0U
        || limits.maximum_total_owner_bytes == 0U) {
        throw std::invalid_argument(
            "trace selection limits must be positive");
    }
    if (signals.size() > limits.maximum_signals) {
        throw std::length_error("trace selection signal limit exceeded");
    }

    std::set<runtime::simir::SignalId> runtime_signals;
    std::set<std::uint64_t> trace_signals;
    std::set<std::string, std::less<>> owners;
    std::size_t maximum_runtime_signal = 0U;
    std::size_t total_owner_bytes = 0U;
    for (const auto& signal : signals) {
        validate_owner(signal.owner, limits, total_owner_bytes);
        if (signal.trace_signal.value == 0U
            || !runtime_signals.insert(signal.runtime_signal).second
            || !trace_signals.insert(signal.trace_signal.value).second
            || !owners.insert(signal.owner).second) {
            throw std::invalid_argument(
                "trace selection declaration identity is duplicated or invalid");
        }
        const auto& variable = [&]()
            -> const runtime::TraceVariableDeclaration& {
            try {
                return declarations.variable(signal.trace_signal);
            } catch (const std::out_of_range&) {
                throw std::invalid_argument(
                    "trace selection signal is not declared");
            }
        }();
        if (variable.hierarchical_name != signal.owner) {
            throw std::invalid_argument(
                "trace selection owner does not own its declaration");
        }
        maximum_runtime_signal = std::max(
            maximum_runtime_signal,
            static_cast<std::size_t>(signal.runtime_signal));
    }
    if (!signals.empty()
        && maximum_runtime_signal >= limits.maximum_signals) {
        throw std::length_error(
            "trace selection runtime signal exceeds its limit");
    }

    slots_.resize(signals.empty() ? 0U : maximum_runtime_signal + 1U);
    declared_signals_.reserve(signals.size());
    for (const auto& signal : signals) {
        auto& slot = slots_[signal.runtime_signal];
        slot.trace_signal = signal.trace_signal;
        slot.owner = signal.owner;
        slot.declared = true;
        slot.selected = signal.selected;
        declared_signals_.push_back(signal.runtime_signal);
    }
    std::ranges::sort(
        declared_signals_, { }, [&](const runtime::simir::SignalId signal) {
            return slots_[signal].owner;
        });
}

bool TraceSelectionControl::selected(
    const runtime::simir::SignalId signal) const noexcept
{
    return signal < slots_.size()
        && slots_[signal].declared
        && slots_[signal].selected;
}

bool TraceSelectionControl::set_enabled(
    const runtime::simir::SignalId signal,
    const bool enable,
    const runtime::SimulationTick time,
    const std::uint64_t delta,
    const runtime::PackedLogic4& value,
    TraceObservationRecorder& observations)
{
    if (signal >= slots_.size() || !slots_[signal].declared) {
        throw std::logic_error("debug trace signal is not declared");
    }
    auto& slot = slots_[signal];
    if (slot.selected == enable) {
        return false;
    }
    if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("trace selection generations exhausted");
    }
    if (enable) {
        require_append_only_snapshot(observations, time, delta);
        const std::array values { TraceObservationValue {
            slot.trace_signal, value, signal } };
        static_cast<void>(observations.accept(
            TraceObservationKind::Signal, time, delta,
            runtime::TraceRegion::Callback,
            "late-snapshot:" + std::to_string(signal), values));
        barrier_time_ = time;
        barrier_delta_ = delta;
        barrier_active_ = true;
    }
    slot.selected = enable;
    ++generation_;
    return true;
}

TraceSelectionStatus TraceSelectionControl::status() const
{
    TraceSelectionStatus result;
    result.declared = declared_signals_.size();
    result.generation = generation_;
    result.selected_owners.reserve(declared_signals_.size());
    for (const auto signal : declared_signals_) {
        if (slots_[signal].selected) {
            result.selected_owners.push_back(slots_[signal].owner);
        }
    }
    result.selected = result.selected_owners.size();
    return result;
}

runtime::TraceRegion TraceSelectionControl::observation_region(
    const runtime::SimulationTick time,
    const std::uint64_t delta,
    const runtime::TraceRegion requested) const noexcept
{
    if (barrier_active_ && time == barrier_time_ && delta == barrier_delta_
        && static_cast<std::uint8_t>(requested)
            < static_cast<std::uint8_t>(runtime::TraceRegion::Callback)) {
        return runtime::TraceRegion::Callback;
    }
    return requested;
}

std::span<const runtime::simir::SignalId>
TraceSelectionControl::declared_signals() const noexcept
{
    return declared_signals_;
}

} // namespace fsim::app::application_detail
