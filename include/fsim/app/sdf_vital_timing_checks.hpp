// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_vital_scheduling.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfVitalScheduledTimingCheck {
    SdfVitalCallReference call;
    runtime::simir::VitalTimingCheckKind kind {
        runtime::simir::VitalTimingCheckKind::setup_hold
    };
    std::vector<runtime::simir::SignalId> endpoint_signals;
    std::vector<std::string> edge_identities;
    std::string condition_identity;
    std::array<std::uint64_t, 4> source_limits { };
    std::array<std::uint64_t, 4> effective_limits { };
    bool check_enabled { true };
    std::array<bool, 4> enables { true, true, true, true };
    bool x_on { true };
    bool message_on { true };
    runtime::simir::AssertionSeverity severity {
        runtime::simir::AssertionSeverity::warning
    };
    std::string message;
    runtime::simir::SourceLocation violation_source;
    frontend::SourceSpan annotation_source;
    std::string canonical_identity;

    friend bool operator==(const SdfVitalScheduledTimingCheck&,
        const SdfVitalScheduledTimingCheck&) = default;
};

class SdfVitalTimingCheckApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalTimingCheckApplication(
        std::shared_ptr<const SdfVitalSchedulingApplication> scheduling,
        elaboration::ElaboratedDesign design,
        std::vector<SdfVitalScheduledTimingCheck> checks,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfVitalSchedulingApplication>&
    scheduling() const noexcept;
    [[nodiscard]] const elaboration::ElaboratedDesign& design() const noexcept;
    [[nodiscard]] std::span<const SdfVitalScheduledTimingCheck> checks() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfVitalSchedulingApplication> scheduling_;
    elaboration::ElaboratedDesign design_;
    std::vector<SdfVitalScheduledTimingCheck> checks_;
    std::string semantic_identity_;
};

struct SdfVitalTimingCheckLimits {
    std::size_t max_checks { 1'000'000U };
    std::size_t max_values { 4'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfVitalTimingCheckResult {
    std::shared_ptr<const SdfVitalTimingCheckApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalTimingCheckResult apply_sdf_vital_timing_checks(
    std::shared_ptr<const SdfVitalSchedulingApplication> scheduling,
    SdfVitalTimingCheckLimits limits = { });

} // namespace fsim::app
