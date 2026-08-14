// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_foreign_interfaces.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfVitalObservationSurface : std::uint8_t {
    Debugger,
    Callback,
    InternalTrace,
    Vcd,
};

enum class SdfVitalObservationEventKind : std::uint8_t {
    EffectiveTiming,
    PendingTransaction,
    Violation,
};

enum class SdfVitalObservationStatus : std::uint8_t {
    Recorded,
    Disabled,
    UnknownObject,
    InvalidEvent,
    OutOfOrder,
    LimitReached,
};

struct SdfVitalObservedObject {
    SdfForeignTimingObject timing;
    std::uint64_t stable_id { };
    std::string vcd_name;
    std::string canonical_identity;
};

struct SdfVitalObservationEvent {
    static constexpr std::size_t max_values = 12U;

    std::uint64_t sequence { };
    std::uint64_t object_id { };
    SdfVitalObservationEventKind kind {
        SdfVitalObservationEventKind::EffectiveTiming
    };
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    runtime::SchedulerPhase region { runtime::SchedulerPhase::active };
    runtime::SimulationTick transaction_time { };
    std::uint8_t before_count { };
    std::uint8_t after_count { };
    std::array<runtime::SimulationTick, max_values> before { };
    std::array<runtime::SimulationTick, max_values> after { };

    friend bool operator==(const SdfVitalObservationEvent&,
        const SdfVitalObservationEvent&) = default;
};

class SdfVitalObservabilityApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalObservabilityApplication(
        std::shared_ptr<const SdfForeignInterfaceApplication> foreign,
        std::vector<SdfVitalObservedObject> objects,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfForeignInterfaceApplication>&
    foreign() const noexcept;
    [[nodiscard]] std::span<const SdfVitalObservedObject> objects() const
        noexcept;
    [[nodiscard]] const SdfVitalObservedObject* find_object(
        std::string_view identity) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfForeignInterfaceApplication> foreign_;
    std::vector<SdfVitalObservedObject> objects_;
    std::string semantic_identity_;
};

struct SdfVitalObservabilityLimits {
    std::size_t max_objects { 1'000'000U };
    std::size_t max_values { 6'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfVitalObservabilityResult {
    std::shared_ptr<const SdfVitalObservabilityApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalObservabilityResult build_sdf_vital_observability(
    std::shared_ptr<const SdfForeignInterfaceApplication> foreign,
    SdfVitalObservabilityLimits limits = { });

class SdfVitalObservationRecorder final {
public:
    SdfVitalObservationRecorder(
        std::shared_ptr<const SdfVitalObservabilityApplication> application,
        std::span<const SdfVitalObservationSurface> enabled_surfaces,
        std::size_t max_events_per_surface = 65'536U);

    [[nodiscard]] SdfVitalObservationStatus record(
        SdfVitalObservationEventKind kind,
        std::string_view object_identity,
        runtime::SimulationTick time,
        std::uint64_t delta,
        runtime::SchedulerPhase region,
        runtime::SimulationTick transaction_time,
        std::span<const runtime::SimulationTick> before,
        std::span<const runtime::SimulationTick> after) noexcept;
    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] std::size_t reserved_event_slots() const noexcept;
    [[nodiscard]] std::span<const SdfVitalObservationEvent> events(
        SdfVitalObservationSurface surface) const noexcept;

private:
    std::shared_ptr<const SdfVitalObservabilityApplication> application_;
    std::array<std::vector<SdfVitalObservationEvent>, 4> events_;
    std::uint8_t enabled_mask_ { };
    std::size_t max_events_per_surface_ { };
    std::uint64_t next_sequence_ { 1U };
    runtime::SimulationTick last_time_ { };
    std::uint64_t last_delta_ { };
    runtime::SchedulerPhase last_region_ { runtime::SchedulerPhase::active };
    bool has_last_ { };
};

} // namespace fsim::app
