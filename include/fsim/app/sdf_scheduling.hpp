// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_precedence.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfScheduledTimingTarget {
    SdfEffectiveValueRole role { SdfEffectiveValueRole::Delay };
    SdfTimingTargetKind target_kind { SdfTimingTargetKind::SpecifyPath };
    std::string target_identity;
    SdfEffectiveValueSource selected_source {
        SdfEffectiveValueSource::SourceSpecify
    };
    bool enabled { true };
    std::vector<std::uint64_t> delay_ticks;
    std::vector<std::int64_t> check_ticks;
    std::string canonical_identity;

    friend bool operator==(const SdfScheduledTimingTarget&,
        const SdfScheduledTimingTarget&) = default;
};

class SdfSchedulingApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfPrecedenceApplication>&
    precedence() const noexcept;
    [[nodiscard]] const elaboration::ElaboratedDesign& design() const noexcept;
    [[nodiscard]] std::span<const SdfScheduledTimingTarget> targets() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfSchedulingApplication(
        std::shared_ptr<const SdfPrecedenceApplication> precedence,
        elaboration::ElaboratedDesign design,
        std::vector<SdfScheduledTimingTarget> targets,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfPrecedenceApplication> precedence_;
    elaboration::ElaboratedDesign design_;
    std::vector<SdfScheduledTimingTarget> targets_;
    std::string semantic_identity_;
};

struct SdfSchedulingLimits {
    std::size_t max_targets { 1'000'000U };
    std::size_t max_values { 12'000'000U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfSchedulingResult {
    std::shared_ptr<const SdfSchedulingApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfSchedulingResult apply_sdf_scheduling(
    std::shared_ptr<const SdfPrecedenceApplication> precedence,
    const elaboration::ElaboratedDesign& elaborated,
    SdfSchedulingLimits limits = { });

} // namespace fsim::app
