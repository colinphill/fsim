// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_delay_modes.hpp"
#include "fsim/app/sdf_precedence.hpp"

#include <algorithm>
#include <array>
#include <map>
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

    [[nodiscard]] bool valid_policy(
        const SdfTimingPrecedencePolicy& policy) noexcept
    {
        return policy.command_selection >= SdfDelaySelection::Minimum
            && policy.command_selection <= SdfDelaySelection::Maximum;
    }

    [[nodiscard]] bool delay_enabled(const SdfTimingTargetKind kind,
        const SdfTimingPrecedencePolicy& policy) noexcept
    {
        return kind == SdfTimingTargetKind::SpecifyPath
            ? policy.specify_paths_enabled
            : policy.endpoint_delays_enabled;
    }

    [[nodiscard]] SdfEffectiveValueSource delay_source(
        const SdfDelayApplicationMode mode) noexcept
    {
        return mode == SdfDelayApplicationMode::Absolute
            ? SdfEffectiveValueSource::SdfAbsolute
            : SdfEffectiveValueSource::SdfIncrement;
    }

    [[nodiscard]] std::string value_identity(
        const SdfEffectiveTimingValue& value)
    {
        std::string result = "sdf-effective-timing-value-v1";
        append_field(result,
            std::to_string(static_cast<unsigned>(value.role)));
        append_field(result,
            std::to_string(static_cast<unsigned>(value.target_kind)));
        append_field(result, value.target_identity);
        append_field(result, std::to_string(value.value_index));
        append_field(result,
            std::to_string(static_cast<unsigned>(value.selected_source)));
        append_field(result,
            std::to_string(static_cast<unsigned>(value.annotation_mode)));
        append_field(result,
            std::to_string(static_cast<unsigned>(value.command_selection)));
        append_field(result, value.enabled ? "enabled" : "disabled");
        append_field(result, value.source_delay_ticks ? std::to_string(*value.source_delay_ticks) : "none");
        append_field(result, value.effective_delay_ticks ? std::to_string(*value.effective_delay_ticks) : "none");
        append_field(result, value.source_check_ticks ? std::to_string(*value.source_check_ticks) : "none");
        append_field(result, value.effective_check_ticks ? std::to_string(*value.effective_check_ticks) : "none");
        append_field(result, value.annotation_identity);
        return result;
    }

    struct RecordState {
        const SdfTimingPrecedencePolicy& policy;
        const SdfPrecedenceLimits& limits;
        std::vector<SdfEffectiveTimingValue>& values;
        std::vector<Diagnostic>& diagnostics;
        std::size_t identity_bytes { };
    };

    void record_value(SdfEffectiveTimingValue value, RecordState& state)
    {
        if (state.values.size() >= state.limits.max_values) {
            diagnose(state.diagnostics, "FSIM-SDF-PRECEDENCE-004",
                "SDF timing precedence exceeds its configured value limit",
                value.annotation_source);
            return;
        }
        value.command_selection = state.policy.command_selection;
        value.canonical_identity = value_identity(value);
        if (value.canonical_identity.size() > state.limits.max_identity_bytes
            || state.identity_bytes
                > state.limits.max_identity_bytes
                    - value.canonical_identity.size()) {
            diagnose(state.diagnostics, "FSIM-SDF-PRECEDENCE-004",
                "SDF timing precedence exceeds its identity-byte limit",
                value.annotation_source);
            return;
        }
        state.identity_bytes += value.canonical_identity.size();
        state.values.push_back(std::move(value));
    }

    void record_delay_step(const SdfAppliedDelayModeStep& step,
        RecordState& state)
    {
        const bool enabled = delay_enabled(step.target_kind, state.policy);
        for (std::size_t index = 0; index < step.effective_profile.size();
            ++index) {
            SdfEffectiveTimingValue value;
            value.role = SdfEffectiveValueRole::Delay;
            value.target_kind = step.target_kind;
            value.target_identity = step.target_identity;
            value.value_index = index;
            value.selected_source = enabled
                ? delay_source(step.mode)
                : SdfEffectiveValueSource::Disabled;
            value.annotation_mode = step.mode;
            value.enabled = enabled;
            value.source_delay_ticks = step.before_profile[index];
            value.effective_delay_ticks = step.effective_profile[index];
            value.annotation_source = step.annotation_source;
            value.annotation_identity = step.annotation_identity;
            record_value(std::move(value), state);
        }
    }

    void record_source_path(
        const elaboration::VerilogSpecifyPathInfo& path,
        RecordState& state)
    {
        const auto profile = expand_sdf_transition_delays(path.delays);
        if (!profile) {
            diagnose(state.diagnostics, "FSIM-SDF-PRECEDENCE-003",
                "Source specify path has an unsupported delay profile",
                path.source);
            return;
        }
        for (std::size_t index = 0; index < profile->size(); ++index) {
            SdfEffectiveTimingValue value;
            value.role = SdfEffectiveValueRole::Delay;
            value.target_kind = SdfTimingTargetKind::SpecifyPath;
            value.target_identity = path.identity;
            value.value_index = index;
            value.selected_source = state.policy.specify_paths_enabled
                ? SdfEffectiveValueSource::SourceSpecify
                : SdfEffectiveValueSource::Disabled;
            value.enabled = state.policy.specify_paths_enabled;
            value.source_delay_ticks = (*profile)[index];
            value.effective_delay_ticks = (*profile)[index];
            record_value(std::move(value), state);
        }
    }

    using CheckAnnotations
        = std::map<std::string_view, const SdfPlannedAnnotation*, std::less<>>;

    [[nodiscard]] CheckAnnotations index_check_annotations(
        const SdfAnnotationPlan& plan)
    {
        CheckAnnotations result;
        for (const auto& annotation : plan.annotations()) {
            if (annotation.target_kind == SdfTimingTargetKind::TimingCheck)
                result.emplace(annotation.target_identity, &annotation);
        }
        return result;
    }

    void record_timing_check(
        const runtime::simir::ModuleTimingCheck& check,
        const SdfPlannedAnnotation* annotation, RecordState& state)
    {
        if (annotation
            && (annotation->before_check_ticks != check.limits
                || annotation->after_check_ticks.size()
                    != check.limits.size())) {
            diagnose(state.diagnostics, "FSIM-SDF-PRECEDENCE-003",
                "SDF timing-check precedence found stale or mismatched limits",
                annotation->source);
            return;
        }
        for (std::size_t index = 0; index < check.limits.size(); ++index) {
            SdfEffectiveTimingValue value;
            value.role = SdfEffectiveValueRole::TimingCheck;
            value.target_kind = SdfTimingTargetKind::TimingCheck;
            value.target_identity = check.identity;
            value.value_index = index;
            value.selected_source = state.policy.timing_checks_enabled
                ? annotation ? SdfEffectiveValueSource::SdfTimingCheck
                             : SdfEffectiveValueSource::SourceTimingCheck
                : SdfEffectiveValueSource::Disabled;
            value.enabled = state.policy.timing_checks_enabled;
            value.source_check_ticks = check.limits[index];
            value.effective_check_ticks = annotation
                ? annotation->after_check_ticks[index]
                : check.limits[index];
            if (annotation) {
                value.annotation_source = annotation->source;
                value.annotation_identity = annotation->canonical_identity;
            }
            record_value(std::move(value), state);
        }
    }

    void record_pulse_table(const SdfAppliedPulseTiming& path,
        const SdfEffectiveValueRole role,
        const std::vector<runtime::SimulationTick>& source,
        const std::vector<runtime::SimulationTick>& effective,
        const SdfEffectiveValueSource selected, RecordState& state)
    {
        for (std::size_t index = 0; index < effective.size(); ++index) {
            SdfEffectiveTimingValue value;
            value.role = role;
            value.target_kind = SdfTimingTargetKind::Pulse;
            value.target_identity = path.effective_path.identity;
            value.value_index = index;
            value.selected_source = state.policy.pulse_rejection_enabled
                ? selected
                : SdfEffectiveValueSource::Disabled;
            value.enabled = state.policy.pulse_rejection_enabled;
            if (index < source.size())
                value.source_delay_ticks = source[index];
            value.effective_delay_ticks = effective[index];
            if (!path.annotation_sources.empty())
                value.annotation_source = path.annotation_sources.front();
            if (!path.annotation_identities.empty())
                value.annotation_identity = path.annotation_identities.front();
            record_value(std::move(value), state);
        }
    }

    void record_pulse_path(
        const SdfAppliedPulseTiming& path, RecordState& state)
    {
        const auto source = path.pulse_construct_kind
                == frontend::SdfConstructKind::PathPulsePercent
            ? SdfEffectiveValueSource::SdfPathPulsePercent
            : SdfEffectiveValueSource::SdfPathPulse;
        record_pulse_table(path, SdfEffectiveValueRole::PulseReject,
            path.source_reject_delays, path.effective_reject_delays, source,
            state);
        record_pulse_table(path, SdfEffectiveValueRole::PulseError,
            path.source_error_delays, path.effective_error_delays, source,
            state);
        record_pulse_table(path, SdfEffectiveValueRole::Retain,
            path.source_retain_delays, path.effective_retain_delays,
            SdfEffectiveValueSource::SdfRetain, state);
    }

    [[nodiscard]] bool record_delay_precedence(
        const std::shared_ptr<const SdfAnnotationPlan>& plan,
        const elaboration::ElaboratedDesign& elaborated, RecordState& state)
    {
        std::map<std::string_view, const elaboration::VerilogSpecifyPathInfo*,
            std::less<>>
            source_paths;
        for (const auto& path : elaborated.verilog_specify_paths())
            source_paths.emplace(path.identity, &path);
        std::set<std::string> annotated_targets;
        if (plan) {
            for (const auto& annotation : plan->annotations()) {
                if (annotation.target_kind
                    != SdfTimingTargetKind::SpecifyPath)
                    continue;
                const auto source
                    = source_paths.lower_bound(annotation.target_identity);
                const bool source_matches = source != source_paths.end()
                    && source->first == annotation.target_identity;
                const auto source_profile = !source_matches
                    ? std::optional<SdfTransitionDelayProfile> { }
                    : expand_sdf_transition_delays(source->second->delays);
                const auto before_profile
                    = expand_sdf_transition_delays(annotation.before_ticks);
                if (!source_profile || !before_profile
                    || *source_profile != *before_profile) {
                    diagnose(state.diagnostics, "FSIM-SDF-PRECEDENCE-003",
                        "SDF specify-path precedence found a missing or stale source delay profile",
                        annotation.source);
                    return false;
                }
            }
            const std::array plans { plan };
            const auto delays = apply_sdf_delay_modes(plans);
            if (!delays.ok()) {
                diagnose(state.diagnostics, "FSIM-SDF-PRECEDENCE-003",
                    "SDF absolute/increment precedence could not produce a complete delay profile",
                    { });
                return false;
            }
            for (const auto& step : delays.application->steps()) {
                annotated_targets.insert(step.target_identity);
                record_delay_step(step, state);
            }
        }
        for (const auto& path : elaborated.verilog_specify_paths()) {
            const auto annotated = annotated_targets.lower_bound(path.identity);
            if (annotated == annotated_targets.end()
                || *annotated != path.identity)
                record_source_path(path, state);
        }
        return true;
    }

    void record_check_precedence(
        const std::shared_ptr<const SdfAnnotationPlan>& plan,
        const elaboration::ElaboratedDesign& elaborated, RecordState& state)
    {
        const auto annotations
            = plan ? index_check_annotations(*plan) : CheckAnnotations { };
        std::set<std::string_view, std::less<>> consumed;
        for (const auto& check : elaborated.verilog_timing_checks()) {
            const auto found = annotations.lower_bound(check.identity);
            const bool matched = found != annotations.end()
                && found->first == check.identity;
            if (matched)
                consumed.insert(found->first);
            record_timing_check(check,
                matched ? found->second : nullptr, state);
        }
        if (consumed.size() != annotations.size()) {
            diagnose(state.diagnostics, "FSIM-SDF-PRECEDENCE-003",
                "SDF timing-check precedence found a missing source check",
                { });
        }
    }

    [[nodiscard]] bool record_pulse_precedence(
        const std::shared_ptr<const SdfAnnotationPlan>& plan,
        const elaboration::ElaboratedDesign& elaborated, RecordState& state)
    {
        const auto pulse = apply_sdf_pulse_timing(plan, elaborated);
        if (!pulse.ok()) {
            diagnose(state.diagnostics, "FSIM-SDF-PRECEDENCE-003",
                "SDF pulse precedence could not produce a complete path overlay",
                { });
            return false;
        }
        for (const auto& path : pulse.application->paths())
            record_pulse_path(path, state);
        return true;
    }

    [[nodiscard]] std::string application_identity(
        const std::shared_ptr<const SdfAnnotationPlan>& plan,
        const SdfTimingPrecedencePolicy& policy,
        const std::vector<SdfEffectiveTimingValue>& values)
    {
        std::string result = "sdf-precedence-application-v1";
        append_field(result, plan ? plan->semantic_identity() : "no-annotation");
        append_field(result,
            std::to_string(static_cast<unsigned>(policy.command_selection)));
        append_field(result, policy.specify_paths_enabled ? "paths" : "no-paths");
        append_field(result,
            policy.endpoint_delays_enabled ? "endpoints" : "no-endpoints");
        append_field(result,
            policy.timing_checks_enabled ? "checks" : "no-checks");
        append_field(result,
            policy.pulse_rejection_enabled ? "pulse" : "no-pulse");
        for (const auto& value : values)
            append_field(result, value.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfAnnotationPlan>& SdfPrecedenceApplication::plan()
    const noexcept
{
    return plan_;
}

const SdfTimingPrecedencePolicy& SdfPrecedenceApplication::policy() const noexcept
{
    return policy_;
}

std::span<const SdfEffectiveTimingValue> SdfPrecedenceApplication::values()
    const noexcept
{
    return values_;
}

std::span<const SdfEffectiveTimingValue> SdfPrecedenceApplication::find_target(
    const SdfTimingTargetKind kind, const std::string_view identity) const noexcept
{
    const auto first = std::ranges::find_if(values_, [&](const auto& value) {
        return value.target_kind == kind && value.target_identity == identity;
    });
    if (first == values_.end())
        return { };
    auto last = first;
    while (last != values_.end() && last->target_kind == kind
        && last->target_identity == identity) {
        ++last;
    }
    return { first, last };
}

std::string_view SdfPrecedenceApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfPrecedenceApplication::SdfPrecedenceApplication(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    SdfTimingPrecedencePolicy policy,
    std::vector<SdfEffectiveTimingValue> values, std::string semantic_identity)
    : plan_(std::move(plan))
    , policy_(policy)
    , values_(std::move(values))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfPrecedenceResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfPrecedenceResult apply_sdf_precedence(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfTimingPrecedencePolicy policy, const SdfPrecedenceLimits limits)
{
    SdfPrecedenceResult result;
    if (!valid_policy(policy) || limits.max_values == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-PRECEDENCE-001",
            "SDF timing precedence requires a valid policy and nonzero limits",
            { });
        return result;
    }
    if (plan && (plan->semantic_identity().empty() || !plan->summary() || plan->value_policy().selection != policy.command_selection)) {
        diagnose(result.diagnostics, "FSIM-SDF-PRECEDENCE-002",
            "SDF plan selection conflicts with the command-selected delay mode",
            { });
        return result;
    }
    const bool has_pulse = plan
        && std::ranges::any_of(plan->annotations(), [](const auto& annotation) {
               return annotation.target_kind == SdfTimingTargetKind::Pulse
                   || !annotation.retain_ticks.empty();
           });
    if (has_pulse && !policy.specify_paths_enabled
        && policy.pulse_rejection_enabled) {
        diagnose(result.diagnostics, "FSIM-SDF-PRECEDENCE-002",
            "Enabled SDF pulse rejection conflicts with disabled specify paths",
            { });
        return result;
    }

    std::vector<SdfEffectiveTimingValue> values;
    RecordState state { policy, limits, values, result.diagnostics };
    if (!record_delay_precedence(plan, elaborated, state))
        return result;
    record_check_precedence(plan, elaborated, state);
    if (plan && has_pulse
        && !record_pulse_precedence(plan, elaborated, state))
        return result;
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(plan, policy, values);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-PRECEDENCE-004",
            "SDF timing precedence semantic identity exceeds its configured limit",
            { });
        return result;
    }
    result.application = std::make_shared<const SdfPrecedenceApplication>(
        std::move(plan), policy, std::move(values), identity);
    return result;
}

} // namespace fsim::app
