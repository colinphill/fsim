// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_value_policy.hpp"
#include "fsim/app/sdf_vital_target_plan.hpp"
#include "fsim/app/sdf_target_plan.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfVitalCallKind {
    Delay,
    TimingCheck,
};

struct SdfVitalCallReference {
    runtime::simir::ProcessId process { };
    std::uint32_t instruction { };
    SdfVitalCallKind kind { SdfVitalCallKind::Delay };
    runtime::simir::SourceLocation source;
    std::string canonical_identity;

    friend bool operator==(const SdfVitalCallReference&,
        const SdfVitalCallReference&) = default;
};

struct SdfVitalPathTimingRecord {
    std::uint64_t node_id { };
    std::uint64_t cell_id { };
    frontend::SdfConstructKind construct_kind {
        frontend::SdfConstructKind::Unknown
    };
    SdfDelayApplicationMode annotation_mode { SdfDelayApplicationMode::None };
    std::string instance_path;
    SdfVitalCallReference call;
    std::vector<runtime::simir::SignalId> endpoint_signals;
    std::vector<std::string> edge_identities;
    std::string condition_identity;
    std::vector<std::uint64_t> before_delay_ticks;
    std::vector<SdfSelectedDelay> after_delays;
    std::vector<std::int64_t> before_check_ticks;
    std::vector<SdfSelectedTimingCheckValue> after_checks;
    frontend::SourceSpan source;
    std::string source_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfVitalPathTimingRecord&,
        const SdfVitalPathTimingRecord&) = default;
};

class SdfVitalPathTimingPlan final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    SdfVitalPathTimingPlan(std::shared_ptr<const SdfVitalTargetPlan> targets,
        SdfValuePolicy value_policy,
        std::vector<SdfVitalPathTimingRecord> records,
        std::string semantic_identity);

    [[nodiscard]] const std::shared_ptr<const SdfVitalTargetPlan>& targets()
        const noexcept;
    [[nodiscard]] const SdfValuePolicy& value_policy() const noexcept;
    [[nodiscard]] std::span<const SdfVitalPathTimingRecord> records() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

private:
    std::shared_ptr<const SdfVitalTargetPlan> targets_;
    SdfValuePolicy value_policy_;
    std::vector<SdfVitalPathTimingRecord> records_;
    std::string semantic_identity_;
};

struct SdfVitalPathTimingLimits {
    std::size_t max_records { 1'000'000U };
    std::size_t max_values_per_record { 12U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfVitalPathTimingResult {
    std::shared_ptr<const SdfVitalPathTimingPlan> plan;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfVitalPathTimingResult build_sdf_vital_path_timing_plan(
    std::shared_ptr<const SdfVitalTargetPlan> targets,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfValuePolicy& value_policy,
    SdfVitalPathTimingLimits limits = { });

} // namespace fsim::app
