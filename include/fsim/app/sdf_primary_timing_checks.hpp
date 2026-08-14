// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_target_plan.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

struct SdfAppliedPrimaryTimingCheck {
    std::uint32_t check_id { };
    runtime::simir::ModuleTimingCheckKind kind {
        runtime::simir::ModuleTimingCheckKind::setup
    };
    std::vector<std::int64_t> source_limits;
    std::vector<std::int64_t> effective_limits;
    runtime::simir::ModuleTimingCheck effective_check;
    frontend::SourceSpan annotation_source;
    std::string annotation_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfAppliedPrimaryTimingCheck&,
        const SdfAppliedPrimaryTimingCheck&) = delete;
};

class SdfPrimaryTimingCheckApplication final {
public:
    static constexpr std::uint32_t schema_version = 2U;

    [[nodiscard]] const std::shared_ptr<const SdfAnnotationPlan>& plan() const
        noexcept;
    [[nodiscard]] std::span<const SdfAppliedPrimaryTimingCheck> checks() const
        noexcept;
    [[nodiscard]] const SdfAppliedPrimaryTimingCheck* find_check(
        std::uint32_t id) const noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfPrimaryTimingCheckApplication(
        std::shared_ptr<const SdfAnnotationPlan> plan,
        std::vector<SdfAppliedPrimaryTimingCheck> checks,
        std::string semantic_identity);

private:
    std::shared_ptr<const SdfAnnotationPlan> plan_;
    std::vector<SdfAppliedPrimaryTimingCheck> checks_;
    std::string semantic_identity_;
};

struct SdfPrimaryTimingCheckLimits {
    std::size_t max_checks { 1'000'000U };
    std::size_t max_limits_per_check { 2U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfPrimaryTimingCheckResult {
    std::shared_ptr<const SdfPrimaryTimingCheckApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfPrimaryTimingCheckResult apply_sdf_primary_timing_checks(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    SdfPrimaryTimingCheckLimits limits = { });

} // namespace fsim::app
