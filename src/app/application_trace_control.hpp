// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/trace_model.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fsim::app::application_detail {

class TraceObservationRecorder;

struct TraceSelectionDeclaration {
    runtime::simir::SignalId runtime_signal { };
    runtime::TraceSignalId trace_signal;
    std::string owner;
    bool selected { };
};

struct TraceSelectionLimits {
    std::size_t maximum_signals { 1U << 20U };
    std::size_t maximum_owner_bytes { 4'096U };
    std::size_t maximum_total_owner_bytes { 1U << 24U };
};

struct TraceSelectionStatus {
    std::size_t declared { };
    std::size_t selected { };
    std::uint64_t generation { };
    std::vector<std::string> selected_owners;

    bool operator==(const TraceSelectionStatus&) const = default;
};

class TraceSelectionControl final {
public:
    TraceSelectionControl(
        const runtime::TraceDeclarationModel& declarations,
        std::span<const TraceSelectionDeclaration> signals,
        TraceSelectionLimits limits = { });

    [[nodiscard]] bool selected(
        runtime::simir::SignalId signal) const noexcept;

    [[nodiscard]] bool set_enabled(
        runtime::simir::SignalId signal,
        bool enable,
        runtime::SimulationTick time,
        std::uint64_t delta,
        const runtime::PackedLogic4& value,
        TraceObservationRecorder& observations);

    [[nodiscard]] TraceSelectionStatus status() const;

    [[nodiscard]] runtime::TraceRegion observation_region(
        runtime::SimulationTick time,
        std::uint64_t delta,
        runtime::TraceRegion requested) const noexcept;

    [[nodiscard]] std::span<const runtime::simir::SignalId>
    declared_signals() const noexcept;

private:
    struct Slot {
        runtime::TraceSignalId trace_signal;
        std::string owner;
        bool declared { };
        bool selected { };
    };

    std::vector<Slot> slots_;
    std::vector<runtime::simir::SignalId> declared_signals_;
    std::uint64_t generation_ { };
    runtime::SimulationTick barrier_time_ { };
    std::uint64_t barrier_delta_ { };
    bool barrier_active_ { };
};

} // namespace fsim::app::application_detail
