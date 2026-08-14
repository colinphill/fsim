// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_delay_modes.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceSpan;

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

    [[nodiscard]] bool delay_target(const SdfTimingTargetKind kind) noexcept
    {
        return kind == SdfTimingTargetKind::SpecifyPath
            || kind == SdfTimingTargetKind::Interconnect
            || kind == SdfTimingTargetKind::Port
            || kind == SdfTimingTargetKind::Mipd
            || kind == SdfTimingTargetKind::Device;
    }

    [[nodiscard]] std::string target_key(const SdfTimingTargetKind kind,
        const std::string_view identity)
    {
        auto result = std::to_string(static_cast<unsigned>(kind));
        append_field(result, identity);
        return result;
    }

    [[nodiscard]] bool complete_plan(const SdfAnnotationPlan& plan)
    {
        return plan.summary() && plan.summary()->endpoint_resolution()
            && plan.summary()->endpoint_resolution()->cells()
            && plan.summary()->endpoint_resolution()->cells()->scope()
            && plan.summary()->endpoint_resolution()->cells()->scope()->normalized_ir()
            && !plan.summary()->semantic_identity().empty()
            && !plan.semantic_identity().empty()
            && plan.summary()->annotation_count() == plan.annotations().size();
    }

    [[nodiscard]] bool checked_add(const SdfTransitionDelayProfile& base,
        const SdfTransitionDelayProfile& increment,
        SdfTransitionDelayProfile& result) noexcept
    {
        for (std::size_t index = 0; index < result.size(); ++index) {
            if (increment[index]
                > std::numeric_limits<runtime::SimulationTick>::max()
                    - base[index]) {
                return false;
            }
            result[index] = base[index] + increment[index];
        }
        return true;
    }

    [[nodiscard]] std::string step_identity(
        const SdfAppliedDelayModeStep& step)
    {
        std::string result = "sdf-delay-mode-step-v1";
        append_field(result, std::to_string(step.plan_index));
        append_field(result, std::to_string(step.node_id));
        append_field(result,
            std::to_string(static_cast<unsigned>(step.target_kind)));
        append_field(result,
            std::to_string(static_cast<unsigned>(step.mode)));
        append_field(result, step.target_identity);
        append_field(result, step.annotation_identity);
        for (const auto value : step.source_values)
            append_field(result, std::to_string(value));
        result.push_back('|');
        for (const auto value : step.before_profile)
            append_field(result, std::to_string(value));
        result.push_back('|');
        for (const auto value : step.effective_profile)
            append_field(result, std::to_string(value));
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const std::vector<std::shared_ptr<const SdfAnnotationPlan>>& plans,
        const std::vector<SdfAppliedDelayModeStep>& steps)
    {
        std::string result = "sdf-delay-mode-application-v1";
        for (const auto& plan : plans)
            append_field(result, plan->semantic_identity());
        result.push_back('|');
        for (const auto& step : steps)
            append_field(result, step.canonical_identity);
        return result;
    }

    struct DelayModeState {
        const SdfDelayModeLimits& limits;
        std::vector<Diagnostic>& diagnostics;
        std::vector<SdfAppliedDelayModeStep>& steps;
        std::map<std::string, SdfTransitionDelayProfile>& current_profiles;
        std::size_t& identity_bytes;
    };

    void apply_annotation(const SdfPlannedAnnotation& annotation,
        const std::size_t plan_index, DelayModeState& state)
    {
        if (!delay_target(annotation.target_kind))
            return;
        if (state.steps.size() >= state.limits.max_steps) {
            diagnose(state.diagnostics, "FSIM-SDF-DELAY-MODE-004",
                "SDF delay-mode application exceeds its configured step limit",
                annotation.source);
            return;
        }
        const auto proposed
            = expand_sdf_transition_delays(annotation.after_ticks);
        const auto declared_before
            = expand_sdf_transition_delays(annotation.before_ticks);
        if (!proposed) {
            diagnose(state.diagnostics, "FSIM-SDF-DELAY-MODE-002",
                "SDF delay list does not have a governed 1, 2, 3, 6, or 12-value shape",
                annotation.source);
            return;
        }
        const auto key
            = target_key(annotation.target_kind, annotation.target_identity);
        const auto current = state.current_profiles.find(key);
        SdfTransitionDelayProfile before { };
        if (current != state.current_profiles.end())
            before = current->second;
        else if (declared_before)
            before = *declared_before;
        if (declared_before && current != state.current_profiles.end()
            && *declared_before != current->second) {
            diagnose(state.diagnostics, "FSIM-SDF-DELAY-MODE-003",
                "Ordered SDF delay application found stale before-values for a repeated target",
                annotation.source);
            return;
        }

        SdfTransitionDelayProfile effective { };
        if (annotation.delay_mode == SdfDelayApplicationMode::Absolute) {
            effective = *proposed;
        } else if (annotation.delay_mode != SdfDelayApplicationMode::Increment
            || (current == state.current_profiles.end() && !declared_before)) {
            diagnose(state.diagnostics, "FSIM-SDF-DELAY-MODE-003",
                "Incremental SDF delay application has no current profile or uses an invalid mode",
                annotation.source);
            return;
        } else if (!checked_add(before, *proposed, effective)) {
            diagnose(state.diagnostics, "FSIM-SDF-DELAY-MODE-004",
                "Incremental SDF delay application overflows simulator ticks",
                annotation.source);
            return;
        }

        SdfAppliedDelayModeStep step;
        step.plan_index = plan_index;
        step.node_id = annotation.node_id;
        step.target_kind = annotation.target_kind;
        step.mode = annotation.delay_mode;
        step.target_identity = annotation.target_identity;
        step.source_values.assign(
            annotation.after_ticks.begin(), annotation.after_ticks.end());
        step.before_profile = before;
        step.effective_profile = effective;
        step.annotation_source = annotation.source;
        step.annotation_identity = annotation.canonical_identity;
        step.canonical_identity = step_identity(step);
        if (step.canonical_identity.size() > state.limits.max_identity_bytes
            || state.identity_bytes
                > state.limits.max_identity_bytes
                    - step.canonical_identity.size()) {
            diagnose(state.diagnostics, "FSIM-SDF-DELAY-MODE-004",
                "SDF delay-mode application exceeds its configured identity-byte limit",
                annotation.source);
            return;
        }
        state.identity_bytes += step.canonical_identity.size();
        state.current_profiles.insert_or_assign(key, effective);
        state.steps.push_back(std::move(step));
    }
} // namespace

std::optional<SdfTransitionDelayProfile> expand_sdf_transition_delays(
    const std::span<const std::uint64_t> values) noexcept
{
    if (values.size() != 1U && values.size() != 2U && values.size() != 3U
        && values.size() != 6U && values.size() != 12U) {
        return std::nullopt;
    }
    SdfTransitionDelayProfile result { };
    if (values.size() == 1U) {
        result.fill(values.front());
        return result;
    }
    if (values.size() == 12U) {
        std::ranges::copy(values, result.begin());
        return result;
    }
    const auto rise = values[0];
    const auto fall = values[1];
    const auto turnoff
        = values.size() == 2U ? std::min(rise, fall) : values[2];
    result[0] = rise;
    result[1] = fall;
    result[2] = turnoff;
    result[3] = values.size() >= 6U ? values[3] : rise;
    result[4] = values.size() >= 6U ? values[4] : turnoff;
    result[5] = values.size() >= 6U ? values[5] : fall;
    result[6] = std::min(result[0], result[2]);
    result[7] = std::max(result[0], result[3]);
    result[8] = std::min(result[1], result[4]);
    result[9] = std::max(result[1], result[5]);
    result[10] = std::max(result[2], result[4]);
    result[11] = std::min(result[3], result[5]);
    return result;
}

std::span<const std::shared_ptr<const SdfAnnotationPlan>>
SdfDelayModeApplication::plans() const noexcept
{
    return plans_;
}

std::span<const SdfAppliedDelayModeStep> SdfDelayModeApplication::steps() const
    noexcept
{
    return steps_;
}

const SdfAppliedDelayModeStep* SdfDelayModeApplication::find_final(
    const SdfTimingTargetKind kind, const std::string_view identity) const
    noexcept
{
    const auto found = std::ranges::find_if(
        steps_.rbegin(), steps_.rend(), [&](const auto& step) {
            return step.target_kind == kind && step.target_identity == identity;
        });
    return found == steps_.rend() ? nullptr : &*found;
}

std::string_view SdfDelayModeApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfDelayModeApplication::SdfDelayModeApplication(
    std::vector<std::shared_ptr<const SdfAnnotationPlan>> plans,
    std::vector<SdfAppliedDelayModeStep> steps, std::string semantic_identity)
    : plans_(std::move(plans))
    , steps_(std::move(steps))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfDelayModeResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfDelayModeResult apply_sdf_delay_modes(
    const std::span<const std::shared_ptr<const SdfAnnotationPlan>> ordered_plans,
    const SdfDelayModeLimits limits)
{
    SdfDelayModeResult result;
    if (ordered_plans.empty() || ordered_plans.size() > limits.max_plans
        || limits.max_plans == 0U || limits.max_steps == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-DELAY-MODE-001",
            "SDF delay-mode application requires ordered plans within nonzero resource limits",
            { });
        return result;
    }
    std::vector<std::shared_ptr<const SdfAnnotationPlan>> plans(
        ordered_plans.begin(), ordered_plans.end());
    std::vector<SdfAppliedDelayModeStep> steps;
    std::map<std::string, SdfTransitionDelayProfile> current_profiles;
    std::size_t identity_bytes { };
    DelayModeState state {
        limits, result.diagnostics, steps, current_profiles, identity_bytes
    };
    for (std::size_t plan_index = 0; plan_index < plans.size(); ++plan_index) {
        const auto& plan = plans[plan_index];
        if (!plan || !complete_plan(*plan)) {
            diagnose(result.diagnostics, "FSIM-SDF-DELAY-MODE-001",
                "SDF delay-mode application received an incomplete ordered plan",
                { });
            continue;
        }
        for (const auto& annotation : plan->annotations())
            apply_annotation(annotation, plan_index, state);
    }
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(plans, steps);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-DELAY-MODE-004",
            "SDF delay-mode application semantic identity exceeds its configured limit",
            { });
        return result;
    }
    result.application = std::make_shared<const SdfDelayModeApplication>(
        std::move(plans), std::move(steps), identity);
    return result;
}

} // namespace fsim::app
