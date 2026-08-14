// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_vital_precedence.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfVitalScheduledDelay {
    SdfVitalCallReference call;
    runtime::simir::VitalDelayKind kind {
        runtime::simir::VitalDelayKind::signal
    };
    runtime::simir::VitalDelayShape shape {
        runtime::simir::VitalDelayShape::single
    };
    runtime::simir::VitalGlitchMode mode {
        runtime::simir::VitalGlitchMode::on_event
    };
    std::vector<runtime::simir::SignalId> endpoint_signals;
    std::vector<std::uint64_t> source_delay_ticks;
    std::vector<std::uint64_t> effective_delay_ticks;
    bool annotated { };
    bool reject_fast_path { };
    bool negative_preemption { };
    std::string canonical_identity;

    friend bool operator==(const SdfVitalScheduledDelay&,
        const SdfVitalScheduledDelay&) = default;
};

class SdfVitalSchedulingApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalSchedulingApplication(
        std::shared_ptr<const SdfVitalPrecedenceApplication> precedence,
        elaboration::ElaboratedDesign design,
        std::vector<SdfVitalScheduledDelay> delays,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfVitalPrecedenceApplication>&
    precedence() const noexcept;
    [[nodiscard]] const elaboration::ElaboratedDesign& design() const noexcept;
    [[nodiscard]] std::span<const SdfVitalScheduledDelay> delays() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfVitalPrecedenceApplication> precedence_;
    elaboration::ElaboratedDesign design_;
    std::vector<SdfVitalScheduledDelay> delays_;
    std::string semantic_identity_;
};

struct SdfVitalSchedulingLimits {
    std::size_t max_calls { 1'000'000U };
    std::size_t max_values { 6'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfVitalSchedulingResult {
    std::shared_ptr<const SdfVitalSchedulingApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalSchedulingResult apply_sdf_vital_scheduling(
    std::shared_ptr<const SdfVitalPrecedenceApplication> precedence,
    const elaboration::ElaboratedDesign& elaborated,
    SdfVitalSchedulingLimits limits = { });

} // namespace fsim::app
