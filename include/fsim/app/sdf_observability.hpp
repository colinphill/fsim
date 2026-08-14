// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_effective_archive.hpp"
#include "fsim/runtime/scheduler.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfObservationSurface : std::uint8_t {
    Debugger,
    Callback,
    InternalTrace,
    Vpi,
    Vcd
};

enum class SdfObservationStatus : std::uint8_t {
    Recorded,
    Disabled,
    UnknownTarget,
    InvalidEvent,
    LimitReached
};

struct SdfObservationTarget {
    SdfEffectiveValueKind kind { SdfEffectiveValueKind::PathDelay };
    std::string target_identity;
    std::string source_identity;
    frontend::SourceSpan source_span;
    std::vector<std::int64_t> original_values;
    std::vector<std::int64_t> effective_values;

    friend bool operator==(const SdfObservationTarget&,
        const SdfObservationTarget&) = default;
};

struct SdfObservedObject {
    std::uint64_t stable_id { };
    std::uint64_t vpi_handle { };
    SdfEffectiveValueKind kind { SdfEffectiveValueKind::PathDelay };
    std::string target_identity;
    std::string source_identity;
    frontend::SourceSpan source_span;
    std::vector<std::int64_t> original_values;
    std::vector<std::int64_t> effective_values;
    std::string canonical_identity;
    std::string vcd_name;
};

struct SdfObservedViolation {
    static constexpr std::size_t max_values = 12U;

    std::uint64_t sequence { };
    std::uint64_t object_id { };
    runtime::SimulationTick time { };
    std::uint64_t delta { };
    runtime::SchedulerPhase region { runtime::SchedulerPhase::active };
    std::uint8_t value_count { };
    std::array<std::int64_t, max_values> before { };
    std::array<std::int64_t, max_values> after { };
};

class SdfObservabilityApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] std::span<const SdfObservedObject> objects() const noexcept;
    [[nodiscard]] const SdfObservedObject* find_object(
        std::string_view target_identity) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfObservabilityApplication(std::vector<SdfObservedObject> objects,
        std::string semantic_identity);

private:
    std::vector<SdfObservedObject> objects_;
    std::string semantic_identity_;
};

struct SdfObservabilityLimits {
    std::size_t max_objects { 1'000'000U };
    std::size_t max_events_per_surface { 65'536U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfObservabilityResult {
    std::shared_ptr<const SdfObservabilityApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfObservabilityResult build_sdf_observability(
    std::span<const SdfObservationTarget> targets,
    SdfObservabilityLimits limits = { });

[[nodiscard]] std::string_view sdf_observation_status_diagnostic_code(
    SdfObservationStatus status) noexcept;

class SdfObservationRecorder final {
public:
    SdfObservationRecorder(
        std::shared_ptr<const SdfObservabilityApplication> application,
        std::span<const SdfObservationSurface> enabled_surfaces,
        std::size_t max_events_per_surface = 65'536U);

    [[nodiscard]] SdfObservationStatus record_violation(
        std::string_view target_identity,
        runtime::SimulationTick time,
        std::uint64_t delta,
        runtime::SchedulerPhase region,
        std::span<const std::int64_t> before,
        std::span<const std::int64_t> after) noexcept;

    [[nodiscard]] bool enabled() const noexcept;
    [[nodiscard]] std::size_t reserved_event_slots() const noexcept;
    [[nodiscard]] std::span<const SdfObservedViolation> debugger_events() const
        noexcept;
    [[nodiscard]] std::span<const SdfObservedViolation> callback_events() const
        noexcept;
    [[nodiscard]] std::span<const SdfObservedViolation> internal_trace_events()
        const noexcept;
    [[nodiscard]] std::span<const SdfObservedViolation> vpi_events() const
        noexcept;
    [[nodiscard]] std::span<const SdfObservedViolation> vcd_events() const
        noexcept;

private:
    std::shared_ptr<const SdfObservabilityApplication> application_;
    std::array<std::vector<SdfObservedViolation>, 5> events_;
    std::uint8_t enabled_mask_ { };
    std::size_t max_events_per_surface_ { };
    std::uint64_t next_sequence_ { 1U };
};

} // namespace fsim::app
