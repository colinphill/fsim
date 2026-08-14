// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_secondary_timing_checks.hpp"

#include <limits>
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
    using runtime::simir::ModuleTimingCheck;
    using runtime::simir::ModuleTimingCheckKind;

    void diagnose(std::vector<Diagnostic>& out, std::string code,
        std::string message, const SourceSpan& span)
    {
        out.push_back(Diagnostic { DiagnosticSeverity::Error, std::move(code),
            std::move(message), span, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] bool secondary_kind(const frontend::SdfConstructKind construct,
        const ModuleTimingCheckKind kind) noexcept
    {
        using frontend::SdfConstructKind;
        switch (construct) {
        case SdfConstructKind::Skew:
            return kind == ModuleTimingCheckKind::skew
                || kind == ModuleTimingCheckKind::timeskew;
        case SdfConstructKind::BidirectSkew:
            return kind == ModuleTimingCheckKind::fullskew;
        case SdfConstructKind::Width:
            return kind == ModuleTimingCheckKind::width;
        case SdfConstructKind::Period:
            return kind == ModuleTimingCheckKind::period;
        case SdfConstructKind::NoChange:
            return kind == ModuleTimingCheckKind::nochange;
        default:
            return false;
        }
    }

    [[nodiscard]] bool secondary_construct(
        const frontend::SdfConstructKind construct) noexcept
    {
        using frontend::SdfConstructKind;
        return construct == SdfConstructKind::Skew
            || construct == SdfConstructKind::BidirectSkew
            || construct == SdfConstructKind::Width
            || construct == SdfConstructKind::Period
            || construct == SdfConstructKind::NoChange;
    }

    [[nodiscard]] const ModuleTimingCheck* find_check(
        const elaboration::ElaboratedDesign& design,
        const std::string_view identity)
    {
        const auto& checks = design.verilog_timing_checks();
        const auto found
            = std::ranges::find(checks, identity, &ModuleTimingCheck::identity);
        return found == checks.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool event_roles_match(const SdfPlannedAnnotation& annotation,
        const ModuleTimingCheck& check)
    {
        const bool reference = std::ranges::any_of(annotation.endpoints,
            [&](const auto& endpoint) {
                return endpoint.role == SdfEndpointRole::TimingReference
                    && endpoint.signal == check.reference.terminal.signal;
            });
        if (check.kind == ModuleTimingCheckKind::width
            || check.kind == ModuleTimingCheckKind::period) {
            return reference;
        }
        return reference && check.data
            && std::ranges::any_of(annotation.endpoints,
                [&](const auto& endpoint) {
                    return endpoint.role == SdfEndpointRole::TimingData
                        && endpoint.signal == check.data->terminal.signal;
                });
    }

    [[nodiscard]] bool valid_shape(const ModuleTimingCheckKind kind,
        const std::span<const std::int64_t> values) noexcept
    {
        const bool compound = kind == ModuleTimingCheckKind::fullskew
            || kind == ModuleTimingCheckKind::nochange;
        if (values.size() != (compound ? 2U : 1U))
            return false;
        if (kind == ModuleTimingCheckKind::nochange)
            return values[0] <= values[1];
        return std::ranges::none_of(
            values, [](const auto value) { return value < 0; });
    }

    [[nodiscard]] std::string applied_identity(
        const SdfPlannedAnnotation& annotation, const ModuleTimingCheck& check,
        const std::vector<std::int64_t>& limits)
    {
        std::string result = "sdf-secondary-timing-check-v2";
        append_field(result, annotation.canonical_identity);
        append_field(result, check.identity);
        append_field(result, std::to_string(static_cast<unsigned>(check.kind)));
        append_field(result, check.threshold ? std::to_string(*check.threshold) : "none");
        append_field(result, check.event_based ? "event" : "ordinary");
        append_field(result, check.remain_active ? "active" : "one-shot");
        for (const auto value : limits)
            append_field(result, std::to_string(value));
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const SdfAnnotationPlan& plan,
        const std::vector<SdfAppliedSecondaryTimingCheck>& checks)
    {
        std::string result = "sdf-secondary-check-application-v2";
        append_field(result, plan.semantic_identity());
        for (const auto& check : checks)
            append_field(result, check.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfAnnotationPlan>&
SdfSecondaryTimingCheckApplication::plan() const noexcept
{
    return plan_;
}

std::span<const SdfAppliedSecondaryTimingCheck>
SdfSecondaryTimingCheckApplication::checks() const noexcept
{
    return checks_;
}

const SdfAppliedSecondaryTimingCheck*
SdfSecondaryTimingCheckApplication::find_check(const std::uint32_t id) const
    noexcept
{
    const auto found = std::ranges::find(
        checks_, id, &SdfAppliedSecondaryTimingCheck::check_id);
    return found == checks_.end() ? nullptr : &*found;
}

std::string_view SdfSecondaryTimingCheckApplication::semantic_identity() const
    noexcept
{
    return semantic_identity_;
}

SdfSecondaryTimingCheckApplication::SdfSecondaryTimingCheckApplication(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    std::vector<SdfAppliedSecondaryTimingCheck> checks,
    std::string semantic_identity)
    : plan_(std::move(plan))
    , checks_(std::move(checks))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfSecondaryTimingCheckResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfSecondaryTimingCheckResult apply_sdf_secondary_timing_checks(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfSecondaryTimingCheckLimits limits)
{
    SdfSecondaryTimingCheckResult result;
    if (!plan || !plan->summary() || !plan->summary()->endpoint_resolution()
        || !plan->summary()->endpoint_resolution()->cells()
        || !plan->summary()->endpoint_resolution()->cells()->scope()
        || !plan->summary()->endpoint_resolution()->cells()->scope()->normalized_ir()
        || plan->summary()->semantic_identity().empty()
        || plan->semantic_identity().empty()
        || plan->summary()->annotation_count() != plan->annotations().size()
        || limits.max_checks == 0U || limits.max_limits_per_check < 2U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-SECONDARY-CHECK-001",
            "Secondary SDF timing-check application requires a complete plan and nonzero limits",
            { });
        return result;
    }
    std::vector<SdfAppliedSecondaryTimingCheck> checks;
    std::set<std::uint32_t> check_ids;
    std::size_t identity_bytes { };
    for (const auto& annotation : plan->annotations()) {
        if (!secondary_construct(annotation.construct_kind))
            continue;
        const auto* check = find_check(elaborated, annotation.target_identity);
        if (!check || annotation.target_kind != SdfTimingTargetKind::TimingCheck
            || !secondary_kind(annotation.construct_kind, check->kind)
            || !check_ids.insert(check ? check->id : 0U).second) {
            diagnose(result.diagnostics, "FSIM-SDF-SECONDARY-CHECK-001",
                "Secondary SDF timing-check target is missing, repeated, or kind-mismatched",
                annotation.source);
            continue;
        }
        if (!event_roles_match(annotation, *check)) {
            diagnose(result.diagnostics, "FSIM-SDF-SECONDARY-CHECK-002",
                "Secondary SDF timing-check event roles do not match elaboration",
                annotation.source);
            continue;
        }
        if (annotation.before_check_ticks != check->limits
            || !valid_shape(check->kind, check->limits)
            || !valid_shape(check->kind, annotation.after_check_ticks)
            || annotation.after_check_ticks.size()
                > limits.max_limits_per_check) {
            diagnose(result.diagnostics, "FSIM-SDF-SECONDARY-CHECK-003",
                "Secondary SDF timing-check has stale or incompatible limits",
                annotation.source);
            continue;
        }
        if (checks.size() >= limits.max_checks) {
            diagnose(result.diagnostics, "FSIM-SDF-SECONDARY-CHECK-004",
                "Secondary SDF timing-check exceeds a runtime or resource limit",
                annotation.source);
            continue;
        }
        SdfAppliedSecondaryTimingCheck applied;
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
            diagnose(result.diagnostics, "FSIM-SDF-SECONDARY-CHECK-004",
                "Secondary SDF timing-check exceeds its identity-byte limit",
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
        diagnose(result.diagnostics, "FSIM-SDF-SECONDARY-CHECK-004",
            "Secondary SDF timing-check semantic identity exceeds its limit", { });
        return result;
    }
    result.application = std::make_shared<const SdfSecondaryTimingCheckApplication>(
        std::move(plan), std::move(checks), identity);
    return result;
}

} // namespace fsim::app
