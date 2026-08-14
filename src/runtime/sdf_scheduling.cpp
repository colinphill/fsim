// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_scheduling.hpp"
#include "fsim/app/sdf_delay_modes.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SourceSpan;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message, const SourceSpan& span = { })
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

    using TargetKey
        = std::tuple<SdfTimingTargetKind, std::string, SdfEffectiveValueRole>;
    using ValueGroups
        = std::map<TargetKey, std::vector<const SdfEffectiveTimingValue*>>;

    [[nodiscard]] ValueGroups group_values(
        const SdfPrecedenceApplication& precedence)
    {
        ValueGroups result;
        for (const auto& value : precedence.values()) {
            result[{ value.target_kind, value.target_identity, value.role }]
                .push_back(&value);
        }
        for (auto& [key, values] : result) {
            (void)key;
            std::ranges::sort(values, { },
                &SdfEffectiveTimingValue::value_index);
        }
        return result;
    }

    [[nodiscard]] bool contiguous_values(
        const std::vector<const SdfEffectiveTimingValue*>& values)
    {
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (values[index]->value_index != index
                || values[index]->enabled != values.front()->enabled) {
                return false;
            }
        }
        return !values.empty();
    }

    [[nodiscard]] std::optional<SdfScheduledTimingTarget> make_target(
        const TargetKey& key,
        const std::vector<const SdfEffectiveTimingValue*>& values,
        std::vector<Diagnostic>& diagnostics)
    {
        if (!contiguous_values(values)) {
            diagnose(diagnostics, "FSIM-SDF-SCHEDULING-002",
                "SDF scheduling values have duplicate, missing, or inconsistent indexes");
            return std::nullopt;
        }
        SdfScheduledTimingTarget target;
        target.target_kind = std::get<0>(key);
        target.target_identity = std::get<1>(key);
        target.role = std::get<2>(key);
        target.selected_source = values.front()->selected_source;
        target.enabled = values.front()->enabled;
        if (!std::ranges::all_of(values, [&](const auto* value) {
                return value->selected_source == target.selected_source;
            })) {
            diagnose(diagnostics, "FSIM-SDF-SCHEDULING-002",
                "SDF scheduling target combines inconsistent selected sources");
            return std::nullopt;
        }
        for (const auto* value : values) {
            if (target.role == SdfEffectiveValueRole::TimingCheck) {
                if (!value->effective_check_ticks) {
                    diagnose(diagnostics, "FSIM-SDF-SCHEDULING-002",
                        "SDF scheduling timing-check value is missing its effective tick",
                        value->annotation_source);
                    return std::nullopt;
                }
                target.check_ticks.push_back(*value->effective_check_ticks);
            } else {
                if (!value->effective_delay_ticks) {
                    diagnose(diagnostics, "FSIM-SDF-SCHEDULING-002",
                        "SDF scheduling delay value is missing its effective tick",
                        value->annotation_source);
                    return std::nullopt;
                }
                target.delay_ticks.push_back(*value->effective_delay_ticks);
            }
        }
        std::string identity = "sdf-scheduled-target-v1";
        append_field(identity,
            std::to_string(static_cast<unsigned>(target.target_kind)));
        append_field(identity,
            std::to_string(static_cast<unsigned>(target.role)));
        append_field(identity, target.target_identity);
        append_field(identity,
            std::to_string(static_cast<unsigned>(target.selected_source)));
        append_field(identity, target.enabled ? "enabled" : "disabled");
        for (const auto* value : values)
            append_field(identity, value->canonical_identity);
        target.canonical_identity = std::move(identity);
        return target;
    }

    [[nodiscard]] const SdfScheduledTimingTarget* find_target(
        const std::vector<SdfScheduledTimingTarget>& targets,
        const SdfTimingTargetKind kind, const SdfEffectiveValueRole role,
        const std::string_view identity)
    {
        const auto found = std::ranges::find_if(
            targets, [&](const auto& target) {
                return target.target_kind == kind && target.role == role
                    && target.target_identity == identity;
            });
        return found == targets.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool source_path_matches(
        const elaboration::VerilogSpecifyPathInfo& path,
        const SdfPrecedenceApplication& precedence)
    {
        const auto values
            = precedence.find_target(SdfTimingTargetKind::SpecifyPath,
                path.identity);
        const auto profile = expand_sdf_transition_delays(path.delays);
        return profile && values.size() == profile->size()
            && std::ranges::equal(values, *profile,
                [](const auto& value, const auto tick) {
                    return value.role == SdfEffectiveValueRole::Delay
                        && value.source_delay_ticks == tick;
                });
    }

    [[nodiscard]] bool apply_path_targets(
        elaboration::ElaboratedDesignState& state,
        const SdfPrecedenceApplication& precedence,
        const std::vector<SdfScheduledTimingTarget>& targets,
        std::vector<Diagnostic>& diagnostics)
    {
        for (auto& path : state.verilog_specify_paths) {
            if (!source_path_matches(path, precedence)) {
                diagnose(diagnostics, "FSIM-SDF-SCHEDULING-003",
                    "SDF scheduling found a stale specify-path source profile");
                return false;
            }
            const auto* delay = find_target(targets,
                SdfTimingTargetKind::SpecifyPath,
                SdfEffectiveValueRole::Delay, path.identity);
            if (!delay || delay->delay_ticks.size() != 12U) {
                diagnose(diagnostics, "FSIM-SDF-SCHEDULING-003",
                    "SDF scheduling did not receive a complete specify-path transition profile");
                return false;
            }
            if (delay->enabled
                && (delay->selected_source
                        == SdfEffectiveValueSource::SdfAbsolute
                    || delay->selected_source
                        == SdfEffectiveValueSource::SdfIncrement))
                path.delays = delay->delay_ticks;
            for (const auto& [role, member] : {
                     std::pair { SdfEffectiveValueRole::PulseReject,
                         &elaboration::VerilogSpecifyPathInfo::pulse_reject_delays },
                     std::pair { SdfEffectiveValueRole::PulseError,
                         &elaboration::VerilogSpecifyPathInfo::pulse_error_delays },
                     std::pair { SdfEffectiveValueRole::Retain,
                         &elaboration::VerilogSpecifyPathInfo::retain_delays } }) {
                const auto* pulse = find_target(targets,
                    SdfTimingTargetKind::Pulse, role, path.identity);
                if (pulse && pulse->enabled)
                    path.*member = pulse->delay_ticks;
            }
        }
        return true;
    }

    [[nodiscard]] bool apply_check_targets(
        elaboration::ElaboratedDesignState& state,
        const SdfPrecedenceApplication& precedence,
        const std::vector<SdfScheduledTimingTarget>& targets,
        std::vector<Diagnostic>& diagnostics)
    {
        for (auto& check : state.verilog_timing_checks) {
            const auto values = precedence.find_target(
                SdfTimingTargetKind::TimingCheck, check.identity);
            const auto* target = find_target(targets,
                SdfTimingTargetKind::TimingCheck,
                SdfEffectiveValueRole::TimingCheck, check.identity);
            if (!target || values.size() != check.limits.size()
                || target->check_ticks.size() != check.limits.size()
                || !std::ranges::equal(values, check.limits,
                    [](const auto& value, const auto tick) {
                        return value.source_check_ticks == tick;
                    })) {
                diagnose(diagnostics, "FSIM-SDF-SCHEDULING-003",
                    "SDF scheduling found a stale or incomplete timing-check profile");
                return false;
            }
            if (target->enabled
                && target->selected_source
                    == SdfEffectiveValueSource::SdfTimingCheck)
                check.limits = target->check_ticks;
        }
        return true;
    }

    [[nodiscard]] std::string application_identity(
        const SdfPrecedenceApplication& precedence,
        const std::vector<SdfScheduledTimingTarget>& targets)
    {
        std::string result = "sdf-scheduling-application-v1";
        append_field(result, precedence.semantic_identity());
        for (const auto& target : targets)
            append_field(result, target.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfPrecedenceApplication>&
SdfSchedulingApplication::precedence() const noexcept
{
    return precedence_;
}

const elaboration::ElaboratedDesign& SdfSchedulingApplication::design() const
    noexcept
{
    return design_;
}

std::span<const SdfScheduledTimingTarget>
SdfSchedulingApplication::targets() const noexcept
{
    return targets_;
}

std::string_view SdfSchedulingApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfSchedulingApplication::SdfSchedulingApplication(
    std::shared_ptr<const SdfPrecedenceApplication> precedence,
    elaboration::ElaboratedDesign design,
    std::vector<SdfScheduledTimingTarget> targets,
    std::string semantic_identity)
    : precedence_(std::move(precedence))
    , design_(std::move(design))
    , targets_(std::move(targets))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfSchedulingResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfSchedulingResult apply_sdf_scheduling(
    std::shared_ptr<const SdfPrecedenceApplication> precedence,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfSchedulingLimits limits)
{
    SdfSchedulingResult result;
    if (!precedence || precedence->semantic_identity().empty()
        || limits.max_targets == 0U || limits.max_values == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEDULING-001",
            "SDF scheduling requires complete precedence and nonzero resource limits");
        return result;
    }
    if (precedence->values().size() > limits.max_values) {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEDULING-004",
            "SDF scheduling exceeds its configured value limit");
        return result;
    }
    const auto groups = group_values(*precedence);
    if (groups.size() > limits.max_targets) {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEDULING-004",
            "SDF scheduling exceeds its configured target limit");
        return result;
    }
    std::vector<SdfScheduledTimingTarget> targets;
    targets.reserve(groups.size());
    std::size_t identity_bytes { };
    for (const auto& [key, values] : groups) {
        auto target = make_target(key, values, result.diagnostics);
        if (!target)
            return result;
        if (target->canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - target->canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-SCHEDULING-004",
                "SDF scheduling exceeds its configured identity-byte limit");
            return result;
        }
        identity_bytes += target->canonical_identity.size();
        targets.push_back(std::move(*target));
    }
    auto state = elaborated.state();
    if (!apply_path_targets(state, *precedence, targets, result.diagnostics)
        || !apply_check_targets(
            state, *precedence, targets, result.diagnostics)) {
        return result;
    }
    auto design = elaboration::ElaboratedDesign::from_state(std::move(state));
    if (!design) {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEDULING-003",
            "SDF scheduling produced an invalid runtime design");
        return result;
    }
    const auto identity = application_identity(*precedence, targets);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-SCHEDULING-004",
            "SDF scheduling semantic identity exceeds its configured limit");
        return result;
    }
    result.application = std::make_shared<const SdfSchedulingApplication>(
        std::move(precedence), std::move(*design), std::move(targets),
        identity);
    return result;
}

} // namespace fsim::app
