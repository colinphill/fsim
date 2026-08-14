// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_primary_timing_checks.hpp"

#include <limits>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceSpan;
    using runtime::simir::ModuleTimingCheckKind;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), span, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] std::optional<ModuleTimingCheckKind> primary_kind(
        const frontend::SdfConstructKind kind) noexcept
    {
        using frontend::SdfConstructKind;
        switch (kind) {
        case SdfConstructKind::Setup:
            return ModuleTimingCheckKind::setup;
        case SdfConstructKind::Hold:
            return ModuleTimingCheckKind::hold;
        case SdfConstructKind::SetupHold:
            return ModuleTimingCheckKind::setuphold;
        case SdfConstructKind::Recovery:
            return ModuleTimingCheckKind::recovery;
        case SdfConstructKind::Removal:
            return ModuleTimingCheckKind::removal;
        case SdfConstructKind::RecRem:
            return ModuleTimingCheckKind::recrem;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] const runtime::simir::ModuleTimingCheck* find_check(
        const elaboration::ElaboratedDesign& elaborated,
        const std::string_view identity)
    {
        const auto& checks = elaborated.verilog_timing_checks();
        const auto found = std::ranges::find(
            checks, identity, &runtime::simir::ModuleTimingCheck::identity);
        return found == checks.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool event_roles_match(
        const SdfPlannedAnnotation& annotation,
        const runtime::simir::ModuleTimingCheck& check)
    {
        const bool reference = std::ranges::any_of(annotation.endpoints,
            [&](const auto& endpoint) {
                return endpoint.role == SdfEndpointRole::TimingReference
                    && endpoint.signal == check.reference.terminal.signal;
            });
        const bool data = check.data && std::ranges::any_of(annotation.endpoints, [&](const auto& endpoint) {
            return endpoint.role == SdfEndpointRole::TimingData
                && endpoint.signal == check.data->terminal.signal;
        });
        return reference && data;
    }

    [[nodiscard]] bool limit_shape(const ModuleTimingCheckKind kind,
        const std::size_t count) noexcept
    {
        const bool combined = kind == ModuleTimingCheckKind::setuphold
            || kind == ModuleTimingCheckKind::recrem;
        return count == (combined ? 2U : 1U);
    }

    [[nodiscard]] bool valid_limits(const ModuleTimingCheckKind kind,
        const std::span<const std::int64_t> values) noexcept
    {
        if (!limit_shape(kind, values.size()))
            return false;
        if (kind != ModuleTimingCheckKind::setuphold
            && kind != ModuleTimingCheckKind::recrem) {
            return values.front() >= 0;
        }
        const auto left = values[0];
        const auto right = values[1];
        if ((right > 0
                && left > std::numeric_limits<std::int64_t>::max() - right)
            || (right < 0
                && left < std::numeric_limits<std::int64_t>::min() - right)) {
            return false;
        }
        return left + right > 0;
    }

    [[nodiscard]] std::string applied_identity(
        const SdfPlannedAnnotation& annotation,
        const runtime::simir::ModuleTimingCheck& check,
        const std::vector<std::int64_t>& limits)
    {
        std::string result = "sdf-primary-timing-check-v2";
        append_field(result, annotation.canonical_identity);
        append_field(result, check.identity);
        append_field(result, std::to_string(static_cast<unsigned>(check.kind)));
        append_field(result, std::to_string(check.reference.terminal.signal));
        append_field(result,
            check.data ? std::to_string(check.data->terminal.signal) : "none");
        append_field(result,
            check.delayed_reference
                ? std::to_string(check.delayed_reference->signal)
                : "none");
        append_field(result,
            check.delayed_data ? std::to_string(check.delayed_data->signal)
                               : "none");
        for (const auto value : limits)
            append_field(result, std::to_string(value));
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const SdfAnnotationPlan& plan,
        const std::vector<SdfAppliedPrimaryTimingCheck>& checks)
    {
        std::string result = "sdf-primary-timing-check-application-v2";
        append_field(result, plan.semantic_identity());
        for (const auto& check : checks)
            append_field(result, check.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfAnnotationPlan>&
SdfPrimaryTimingCheckApplication::plan() const noexcept
{
    return plan_;
}

std::span<const SdfAppliedPrimaryTimingCheck>
SdfPrimaryTimingCheckApplication::checks() const noexcept
{
    return checks_;
}

const SdfAppliedPrimaryTimingCheck*
SdfPrimaryTimingCheckApplication::find_check(const std::uint32_t id) const
    noexcept
{
    const auto found
        = std::ranges::find(checks_, id, &SdfAppliedPrimaryTimingCheck::check_id);
    return found == checks_.end() ? nullptr : &*found;
}

std::string_view SdfPrimaryTimingCheckApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

SdfPrimaryTimingCheckApplication::SdfPrimaryTimingCheckApplication(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    std::vector<SdfAppliedPrimaryTimingCheck> checks,
    std::string semantic_identity)
    : plan_(std::move(plan))
    , checks_(std::move(checks))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfPrimaryTimingCheckResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfPrimaryTimingCheckResult apply_sdf_primary_timing_checks(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfPrimaryTimingCheckLimits limits)
{
    SdfPrimaryTimingCheckResult result;
    if (!plan || !plan->summary() || !plan->summary()->endpoint_resolution()
        || !plan->summary()->endpoint_resolution()->cells()
        || !plan->summary()->endpoint_resolution()->cells()->scope()
        || !plan->summary()->endpoint_resolution()->cells()->scope()->normalized_ir()
        || plan->summary()->semantic_identity().empty()
        || plan->semantic_identity().empty()
        || plan->summary()->annotation_count() != plan->annotations().size()
        || limits.max_checks == 0U || limits.max_limits_per_check < 2U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-PRIMARY-CHECK-001",
            "Primary SDF timing-check application requires a complete plan and nonzero limits",
            { });
        return result;
    }
    std::vector<SdfAppliedPrimaryTimingCheck> checks;
    std::set<std::uint32_t> check_ids;
    std::size_t identity_bytes { };
    for (const auto& annotation : plan->annotations()) {
        const auto expected = primary_kind(annotation.construct_kind);
        if (!expected)
            continue;
        if (checks.size() >= limits.max_checks) {
            diagnose(result.diagnostics, "FSIM-SDF-PRIMARY-CHECK-004",
                "Primary SDF timing-check application exceeds its configured check limit",
                annotation.source);
            continue;
        }
        const auto* check = find_check(elaborated, annotation.target_identity);
        if (!check || check->kind != *expected
            || annotation.target_kind != SdfTimingTargetKind::TimingCheck
            || !check_ids.insert(check ? check->id : 0U).second) {
            diagnose(result.diagnostics, "FSIM-SDF-PRIMARY-CHECK-001",
                "Primary SDF timing-check plan refers to a missing, repeated, or kind-mismatched target",
                annotation.source);
            continue;
        }
        if (!event_roles_match(annotation, *check)) {
            diagnose(result.diagnostics, "FSIM-SDF-PRIMARY-CHECK-002",
                "Primary SDF timing-check reference/data event roles do not match elaboration",
                annotation.source);
            continue;
        }
        if (annotation.before_check_ticks != check->limits
            || !valid_limits(check->kind, check->limits)
            || !valid_limits(check->kind, annotation.after_check_ticks)
            || annotation.after_check_ticks.size()
                > limits.max_limits_per_check) {
            diagnose(result.diagnostics, "FSIM-SDF-PRIMARY-CHECK-003",
                "Primary SDF timing-check has stale, illegal, or incompatible signed limits",
                annotation.source);
            continue;
        }
        SdfAppliedPrimaryTimingCheck applied;
        applied.check_id = check->id;
        applied.kind = check->kind;
        applied.source_limits = check->limits;
        applied.effective_limits = annotation.after_check_ticks;
        applied.effective_check = *check;
        applied.effective_check.limits = applied.effective_limits;
        applied.annotation_source = annotation.source;
        applied.annotation_identity = annotation.canonical_identity;
        applied.canonical_identity
            = applied_identity(annotation, *check, applied.effective_limits);
        if (applied.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes
                > limits.max_identity_bytes - applied.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-PRIMARY-CHECK-004",
                "Primary SDF timing-check application exceeds its identity-byte limit",
                annotation.source);
            continue;
        }
        identity_bytes += applied.canonical_identity.size();
        checks.push_back(std::move(applied));
    }
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(*plan, checks);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-PRIMARY-CHECK-004",
            "Primary SDF timing-check semantic identity exceeds its configured limit",
            { });
        return result;
    }
    result.application = std::make_shared<const SdfPrimaryTimingCheckApplication>(
        std::move(plan), std::move(checks), identity);
    return result;
}

} // namespace fsim::app
