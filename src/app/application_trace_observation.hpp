// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/simir.hpp"
#include "fsim/runtime/trace_model.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace fsim::app::application_detail {

enum class TraceObservationKind : std::uint8_t {
    Signal,
    Uvm,
    DynamicClass,
    Container,
    Coverage,
    Assertion,
    Sdf
};

struct TraceObservationValue {
    runtime::TraceSignalId signal;
    runtime::PackedLogic4 value;
    std::optional<runtime::simir::SignalId> runtime_signal;
};

struct TraceObservationRecord {
    TraceObservationKind kind { TraceObservationKind::Signal };
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    runtime::TraceRegion region { runtime::TraceRegion::Active };
    std::uint64_t sequence { };
    std::string identity;
    std::vector<TraceObservationValue> values;
};

struct TraceObservationLimits {
    std::size_t maximum_records { 1U << 20U };
    std::size_t maximum_values_per_record { 4'096U };
    std::size_t maximum_payload_bits { 1U << 24U };
    std::size_t maximum_identity_bytes { 4'096U };
    std::size_t maximum_observers { 16U };
};

class TraceObservationRecorder final {
public:
    using Observer = std::function<void(const TraceObservationRecord&)>;

    explicit TraceObservationRecorder(
        const runtime::TraceDeclarationModel& declarations,
        TraceObservationLimits limits = { });

    [[nodiscard]] std::uint64_t add_observer(Observer observer);
    void remove_observer(std::uint64_t token);

    [[nodiscard]] std::uint64_t accept(
        TraceObservationKind kind,
        runtime::SimulationTick time,
        std::uint64_t delta,
        runtime::TraceRegion region,
        std::string identity,
        std::span<const TraceObservationValue> values);
    [[nodiscard]] std::uint64_t claim_sequence();

    [[nodiscard]] std::span<const TraceObservationRecord>
    records() const noexcept;
    [[nodiscard]] std::uint64_t callback_failures() const noexcept;
    [[nodiscard]] std::exception_ptr callback_failure() const noexcept;

private:
    void fan_out(const TraceObservationRecord& record) noexcept;

    const runtime::TraceDeclarationModel* declarations_ { };
    TraceObservationLimits limits_;
    std::vector<TraceObservationRecord> records_;
    std::map<std::uint64_t, Observer> observers_;
    std::uint64_t next_observer_ { 1 };
    std::uint64_t next_sequence_ { 1 };
    std::uint64_t callback_failures_ { };
    std::exception_ptr callback_failure_;
    bool fanning_out_ { };
};

} // namespace fsim::app::application_detail
