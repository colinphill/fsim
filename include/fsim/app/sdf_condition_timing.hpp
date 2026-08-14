// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_target_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfTimingConditionDisposition : std::uint8_t {
    Unconditional,
    ElaboratedOnly,
    SdfMatched,
};

struct SdfAppliedConditionTiming {
    std::uint32_t check_id { };
    runtime::StableOrder stable_order { };
    SdfTimingConditionDisposition condition_disposition {
        SdfTimingConditionDisposition::Unconditional
    };
    std::vector<runtime::simir::SignalId> condition_signals;
    std::optional<runtime::simir::SignalId> notifier;
    bool edge_qualified { };
    bool negative_limits { };
    runtime::simir::ModuleTimingCheck effective_check;
    frontend::SourceSpan annotation_source;
    std::string annotation_identity;
    std::string condition_program_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfAppliedConditionTiming&,
        const SdfAppliedConditionTiming&) = delete;
};

class SdfConditionTimingApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationPlan>& plan() const
        noexcept;
    [[nodiscard]] std::span<const SdfAppliedConditionTiming> checks() const
        noexcept;
    [[nodiscard]] const SdfAppliedConditionTiming* find_check(
        std::uint32_t id) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfConditionTimingApplication(
        std::shared_ptr<const SdfAnnotationPlan> plan,
        std::vector<SdfAppliedConditionTiming> checks,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfAnnotationPlan> plan_;
    std::vector<SdfAppliedConditionTiming> checks_;
    std::string semantic_identity_;
};

struct SdfConditionTimingLimits {
    std::size_t max_checks { 1'000'000U };
    std::size_t max_expression_nodes_per_check { 1U << 20U };
    std::size_t max_condition_signals_per_check { 1U << 16U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfConditionTimingResult {
    std::shared_ptr<const SdfConditionTimingApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfConditionTimingResult apply_sdf_condition_timing(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    SdfConditionTimingLimits limits = { });

} // namespace fsim::app
