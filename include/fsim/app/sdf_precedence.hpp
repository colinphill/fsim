// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_pulse_timing.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfEffectiveValueRole {
    Delay,
    TimingCheck,
    PulseReject,
    PulseError,
    Retain,
};

enum class SdfEffectiveValueSource {
    Disabled,
    SourceSpecify,
    SourcePrimitiveOrNet,
    SourceTimingCheck,
    SdfAbsolute,
    SdfIncrement,
    SdfTimingCheck,
    SdfPathPulse,
    SdfPathPulsePercent,
    SdfRetain,
};

struct SdfTimingPrecedencePolicy {
    SdfDelaySelection command_selection { SdfDelaySelection::Typical };
    bool specify_paths_enabled { true };
    bool endpoint_delays_enabled { true };
    bool timing_checks_enabled { true };
    bool pulse_rejection_enabled { true };

    friend bool operator==(const SdfTimingPrecedencePolicy&,
        const SdfTimingPrecedencePolicy&) = default;
};

struct SdfEffectiveTimingValue {
    SdfEffectiveValueRole role { SdfEffectiveValueRole::Delay };
    SdfTimingTargetKind target_kind { SdfTimingTargetKind::SpecifyPath };
    std::string target_identity;
    std::size_t value_index { };
    SdfEffectiveValueSource selected_source {
        SdfEffectiveValueSource::SourceSpecify
    };
    SdfDelayApplicationMode annotation_mode { SdfDelayApplicationMode::None };
    SdfDelaySelection command_selection { SdfDelaySelection::Typical };
    bool enabled { true };
    std::optional<std::uint64_t> source_delay_ticks;
    std::optional<std::uint64_t> effective_delay_ticks;
    std::optional<std::int64_t> source_check_ticks;
    std::optional<std::int64_t> effective_check_ticks;
    frontend::SourceSpan annotation_source;
    std::string annotation_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfEffectiveTimingValue&,
        const SdfEffectiveTimingValue&) = default;
};

class SdfPrecedenceApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationPlan>& plan() const
        noexcept;
    [[nodiscard]] const SdfTimingPrecedencePolicy& policy() const noexcept;
    [[nodiscard]] std::span<const SdfEffectiveTimingValue> values() const
        noexcept;
    [[nodiscard]] std::span<const SdfEffectiveTimingValue> find_target(
        SdfTimingTargetKind kind, std::string_view identity) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfPrecedenceApplication(std::shared_ptr<const SdfAnnotationPlan> plan,
        SdfTimingPrecedencePolicy policy,
        std::vector<SdfEffectiveTimingValue> values,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfAnnotationPlan> plan_;
    SdfTimingPrecedencePolicy policy_;
    std::vector<SdfEffectiveTimingValue> values_;
    std::string semantic_identity_;
};

struct SdfPrecedenceLimits {
    std::size_t max_values { 12'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfPrecedenceResult {
    std::shared_ptr<const SdfPrecedenceApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfPrecedenceResult apply_sdf_precedence(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    SdfTimingPrecedencePolicy policy = { },
    SdfPrecedenceLimits limits = { });

} // namespace fsim::app
