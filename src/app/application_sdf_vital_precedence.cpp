// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_precedence.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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

    [[nodiscard]] std::string value_key(const std::string_view call,
        const bool timing_check, const std::size_t index)
    {
        std::string result;
        append_field(result, call);
        append_field(result, timing_check ? "check" : "delay");
        append_field(result, std::to_string(index));
        return result;
    }

    [[nodiscard]] bool valid_plan(
        const std::shared_ptr<const SdfVitalModelPlan>& plan) noexcept
    {
        return plan && plan->paths() && !plan->semantic_identity().empty()
            && !plan->paths()->semantic_identity().empty()
            && plan->records().size() == plan->paths()->records().size();
    }

    [[nodiscard]] std::string value_identity(
        const SdfVitalEffectiveTimingValue& value)
    {
        auto result = std::string { "sdf-vital-effective-value-v1" };
        append_field(result, value.call_identity);
        append_field(result, std::to_string(value.value_index));
        append_field(result, value.timing_check ? "check" : "delay");
        append_field(result,
            std::to_string(static_cast<unsigned>(value.command_selection)));
        append_field(result,
            std::to_string(static_cast<unsigned>(value.base_source)));
        append_field(result,
            std::to_string(static_cast<unsigned>(value.selected_source)));
        append_field(result, value.source_delay_ticks ? std::to_string(*value.source_delay_ticks) : "none");
        append_field(result, value.effective_delay_ticks ? std::to_string(*value.effective_delay_ticks) : "none");
        append_field(result, value.source_check_ticks ? std::to_string(*value.source_check_ticks) : "none");
        append_field(result, value.effective_check_ticks ? std::to_string(*value.effective_check_ticks) : "none");
        append_field(result, value.generic_identity);
        append_field(result,
            value.no_annotation ? "no-annotation" : "annotated");
        for (const auto& step : value.steps) {
            append_field(result, std::to_string(step.revision_index));
            append_field(result, step.plan_identity);
            append_field(result,
                std::to_string(static_cast<unsigned>(step.mode)));
            append_field(result, step.enabled ? "enabled" : "disabled");
            append_field(result, step.delay_ticks ? std::to_string(*step.delay_ticks) : "none");
            append_field(result, step.check_ticks ? std::to_string(*step.check_ticks) : "none");
            append_field(result, step.annotation_identity);
        }
        return result;
    }

    struct ValueIndex {
        std::vector<SdfVitalEffectiveTimingValue> values;
        std::unordered_map<std::string, std::size_t> by_key;
    };

    [[nodiscard]] bool add_source_values(const SdfVitalPathTimingRecord& path,
        const SdfDelaySelection selection, const SdfVitalPrecedenceLimits limits,
        ValueIndex& index, std::vector<Diagnostic>& diagnostics)
    {
        const bool timing_check
            = path.call.kind == SdfVitalCallKind::TimingCheck;
        const auto count = timing_check ? path.before_check_ticks.size()
                                        : path.before_delay_ticks.size();
        if (count == 0U || count > limits.max_values
            || index.values.size() > limits.max_values - count) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-006",
                "SDF VITAL source records exceed the effective-value limit",
                path.source);
            return false;
        }
        for (std::size_t value_index = 0; value_index < count; ++value_index) {
            SdfVitalEffectiveTimingValue value;
            value.call_identity = path.call.canonical_identity;
            value.value_index = value_index;
            value.timing_check = timing_check;
            value.command_selection = selection;
            if (timing_check) {
                value.source_check_ticks = path.before_check_ticks[value_index];
                value.effective_check_ticks = value.source_check_ticks;
            } else {
                value.source_delay_ticks = path.before_delay_ticks[value_index];
                value.effective_delay_ticks = value.source_delay_ticks;
            }
            const auto key = value_key(
                value.call_identity, timing_check, value_index);
            if (!index.by_key.emplace(key, index.values.size()).second) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-006",
                    "SDF VITAL source records duplicate one call/value owner",
                    path.source);
                return false;
            }
            index.values.push_back(std::move(value));
        }
        return true;
    }

    [[nodiscard]] bool apply_generic(const SdfVitalTimingGenericValue& generic,
        ValueIndex& index, std::unordered_set<std::string>& owners,
        std::vector<Diagnostic>& diagnostics)
    {
        const bool has_delay = generic.delay_ticks.has_value();
        const bool has_check = generic.check_ticks.has_value();
        if (generic.call_identity.empty() || generic.generic_identity.empty()
            || has_delay == has_check) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-003",
                "SDF VITAL timing generic has incomplete or ambiguous value identity",
                { });
            return false;
        }
        const auto key = value_key(
            generic.call_identity, has_check, generic.value_index);
        const auto found = index.by_key.find(key);
        if (found == index.by_key.end() || !owners.insert(key).second) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-003",
                "SDF VITAL timing generic is stale or duplicates one source value",
                { });
            return false;
        }
        auto& value = index.values[found->second];
        value.base_source = SdfVitalEffectiveValueSource::TimingGeneric;
        value.selected_source = value.base_source;
        value.generic_identity = generic.generic_identity;
        if (has_delay) {
            value.source_delay_ticks = generic.delay_ticks;
            value.effective_delay_ticks = generic.delay_ticks;
        } else {
            value.source_check_ticks = generic.check_ticks;
            value.effective_check_ticks = generic.check_ticks;
        }
        return true;
    }

    [[nodiscard]] bool add_delay(
        const std::uint64_t lhs, const std::uint64_t rhs,
        std::uint64_t& result) noexcept
    {
        if (lhs > std::numeric_limits<std::uint64_t>::max() - rhs)
            return false;
        result = lhs + rhs;
        return true;
    }

    [[nodiscard]] bool add_check(
        const std::int64_t lhs, const std::int64_t rhs,
        std::int64_t& result) noexcept
    {
        if ((rhs > 0 && lhs > std::numeric_limits<std::int64_t>::max() - rhs)
            || (rhs < 0
                && lhs < std::numeric_limits<std::int64_t>::min() - rhs)) {
            return false;
        }
        result = lhs + rhs;
        return true;
    }

    [[nodiscard]] bool apply_step(SdfVitalEffectiveTimingValue& value,
        const SdfVitalPrecedenceStep& step,
        std::vector<Diagnostic>& diagnostics)
    {
        value.steps.push_back(step);
        if (!step.enabled)
            return true;
        value.no_annotation = false;
        if (step.mode == SdfDelayApplicationMode::Absolute) {
            value.selected_source = SdfVitalEffectiveValueSource::SdfAbsolute;
            value.effective_delay_ticks = step.delay_ticks;
            value.effective_check_ticks = step.check_ticks;
            return true;
        }
        if (step.mode != SdfDelayApplicationMode::Increment) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-004",
                "SDF VITAL annotation has no absolute/increment precedence mode",
                step.source);
            return false;
        }
        value.selected_source = SdfVitalEffectiveValueSource::SdfIncrement;
        if (value.timing_check) {
            std::int64_t effective { };
            if (!value.effective_check_ticks || !step.check_ticks
                || !add_check(*value.effective_check_ticks, *step.check_ticks,
                    effective)) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-004",
                    "SDF VITAL timing-check increment overflows its effective value",
                    step.source);
                return false;
            }
            value.effective_check_ticks = effective;
        } else {
            std::uint64_t effective { };
            if (!value.effective_delay_ticks || !step.delay_ticks
                || !add_delay(*value.effective_delay_ticks, *step.delay_ticks,
                    effective)) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-004",
                    "SDF VITAL delay increment overflows its effective value",
                    step.source);
                return false;
            }
            value.effective_delay_ticks = effective;
        }
        return true;
    }

    [[nodiscard]] bool apply_path_revision(
        const SdfVitalPathTimingRecord& path,
        const SdfVitalPathTimingRecord& source,
        const std::size_t revision_index, const std::string_view plan_identity,
        const SdfVitalAnnotationControl* control,
        const SdfVitalPrecedencePolicy& policy,
        const SdfVitalPrecedenceLimits& limits, ValueIndex& index,
        std::vector<Diagnostic>& diagnostics)
    {
        const bool timing_check
            = path.call.kind == SdfVitalCallKind::TimingCheck;
        if (path.call.kind != source.call.kind
            || path.before_delay_ticks != source.before_delay_ticks
            || path.before_check_ticks != source.before_check_ticks) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-002",
                "SDF VITAL revision disagrees with the source call/value record",
                path.source);
            return false;
        }
        auto mode = path.annotation_mode;
        if (timing_check && mode == SdfDelayApplicationMode::None)
            mode = SdfDelayApplicationMode::Absolute;
        const auto count = timing_check ? path.after_checks.size()
                                        : path.after_delays.size();
        if (count == 0U) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-002",
                "SDF VITAL revision has no selected annotation values",
                path.source);
            return false;
        }
        const bool enabled = timing_check
            ? policy.timing_check_annotations_enabled
                && (control == nullptr || control->timing_check_enabled)
            : policy.delay_annotations_enabled
                && (control == nullptr || control->delay_enabled);
        for (std::size_t value_index = 0; value_index < count; ++value_index) {
            const auto key = value_key(
                path.call.canonical_identity, timing_check, value_index);
            const auto found = index.by_key.find(key);
            if (found == index.by_key.end()) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-002",
                    "SDF VITAL revision value has no source-record owner",
                    path.source);
                return false;
            }
            auto& value = index.values[found->second];
            if (value.steps.size() >= limits.max_steps_per_value) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-006",
                    "SDF VITAL effective value exceeds its revision-step limit",
                    path.source);
                return false;
            }
            SdfVitalPrecedenceStep step;
            step.revision_index = revision_index;
            step.plan_identity = std::string { plan_identity };
            step.mode = mode;
            step.enabled = enabled;
            step.source = path.source;
            if (timing_check) {
                const auto& selected = path.after_checks[value_index];
                if (selected.selection != policy.command_selection) {
                    diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-002",
                        "SDF VITAL timing-check selection conflicts with command policy",
                        path.source);
                    return false;
                }
                step.check_ticks = selected.ticks;
                step.annotation_identity = selected.canonical_identity;
            } else {
                const auto& selected = path.after_delays[value_index];
                if (selected.selection != policy.command_selection) {
                    diagnose(diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-002",
                        "SDF VITAL delay selection conflicts with command policy",
                        path.source);
                    return false;
                }
                step.delay_ticks = selected.ticks;
                step.annotation_identity = selected.canonical_identity;
            }
            if (!apply_step(value, step, diagnostics))
                return false;
        }
        return true;
    }
} // namespace

SdfVitalPrecedenceApplication::SdfVitalPrecedenceApplication(
    std::shared_ptr<const SdfVitalModelPlan> source,
    std::vector<std::shared_ptr<const SdfVitalModelPlan>> revisions,
    std::vector<SdfVitalTimingGenericValue> generics,
    std::vector<SdfVitalAnnotationControl> controls,
    const SdfVitalPrecedencePolicy policy,
    std::vector<SdfVitalEffectiveTimingValue> values,
    std::string semantic_identity)
    : source_(std::move(source))
    , revisions_(std::move(revisions))
    , generics_(std::move(generics))
    , controls_(std::move(controls))
    , policy_(policy)
    , values_(std::move(values))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfVitalModelPlan>&
SdfVitalPrecedenceApplication::source() const noexcept
{
    return source_;
}

std::span<const std::shared_ptr<const SdfVitalModelPlan>>
SdfVitalPrecedenceApplication::revisions() const noexcept
{
    return revisions_;
}

std::span<const SdfVitalTimingGenericValue>
SdfVitalPrecedenceApplication::generics() const noexcept
{
    return generics_;
}

std::span<const SdfVitalAnnotationControl>
SdfVitalPrecedenceApplication::controls() const noexcept
{
    return controls_;
}

const SdfVitalPrecedencePolicy& SdfVitalPrecedenceApplication::policy()
    const noexcept
{
    return policy_;
}

std::span<const SdfVitalEffectiveTimingValue>
SdfVitalPrecedenceApplication::values() const noexcept
{
    return values_;
}

std::string_view SdfVitalPrecedenceApplication::semantic_identity()
    const noexcept
{
    return semantic_identity_;
}

bool SdfVitalPrecedenceResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfVitalPrecedenceResult apply_sdf_vital_precedence(
    std::shared_ptr<const SdfVitalModelPlan> source,
    const std::span<const std::shared_ptr<const SdfVitalModelPlan>> revisions,
    const std::span<const SdfVitalTimingGenericValue> generics,
    const std::span<const SdfVitalAnnotationControl> controls,
    const SdfVitalPrecedencePolicy policy,
    const SdfVitalPrecedenceLimits limits)
{
    SdfVitalPrecedenceResult result;
    if (!valid_plan(source) || source->paths()->value_policy().selection != policy.command_selection
        || limits.max_revisions == 0U || limits.max_values == 0U
        || limits.max_steps_per_value == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-001",
            "SDF VITAL precedence requires a complete source plan, matching selection, and nonzero limits",
            { });
        return result;
    }
    if (revisions.size() > limits.max_revisions
        || generics.size() > limits.max_values
        || controls.size() > limits.max_revisions) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-006",
            "SDF VITAL precedence inputs exceed their resource limits", { });
        return result;
    }

    ValueIndex index;
    index.values.reserve(source->paths()->records().size());
    std::unordered_map<std::string_view, const SdfVitalPathTimingRecord*>
        source_paths;
    for (const auto& path : source->paths()->records()) {
        if (path.call.canonical_identity.empty()
            || !source_paths.emplace(path.call.canonical_identity, &path).second
            || !add_source_values(path, policy.command_selection, limits, index,
                result.diagnostics)) {
            if (result.diagnostics.empty()) {
                diagnose(result.diagnostics,
                    "FSIM-SDF-VITAL-PRECEDENCE-006",
                    "SDF VITAL source plan duplicates one stable call owner",
                    path.source);
            }
            return result;
        }
    }
    std::unordered_set<std::string> generic_owners;
    for (const auto& generic : generics) {
        if (!apply_generic(
                generic, index, generic_owners, result.diagnostics)) {
            return result;
        }
    }

    std::vector<const SdfVitalAnnotationControl*> revision_controls(
        revisions.size(), nullptr);
    for (const auto& control : controls) {
        if (control.revision_index >= revisions.size()
            || control.control_identity.empty()
            || revision_controls[control.revision_index] != nullptr) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-005",
                "SDF VITAL annotation control is stale, incomplete, or duplicated",
                { });
            return result;
        }
        revision_controls[control.revision_index] = &control;
    }
    for (std::size_t revision_index = 0;
        revision_index < revisions.size(); ++revision_index) {
        const auto& revision = revisions[revision_index];
        const auto* control = revision_controls[revision_index];
        if (!valid_plan(revision)
            || revision->paths()->value_policy().selection
                != policy.command_selection
            || (control != nullptr
                && control->plan_identity != revision->semantic_identity())) {
            diagnose(result.diagnostics,
                control != nullptr ? "FSIM-SDF-VITAL-PRECEDENCE-005"
                                   : "FSIM-SDF-VITAL-PRECEDENCE-002",
                "SDF VITAL revision or its annotation control is stale",
                { });
            return result;
        }
        for (const auto& path : revision->paths()->records()) {
            const auto found = source_paths.find(path.call.canonical_identity);
            if (found == source_paths.end()
                || !apply_path_revision(path, *found->second, revision_index,
                    revision->semantic_identity(), control, policy, limits,
                    index, result.diagnostics)) {
                if (result.diagnostics.empty()) {
                    diagnose(result.diagnostics,
                        "FSIM-SDF-VITAL-PRECEDENCE-002",
                        "SDF VITAL revision has no stable source call owner",
                        path.source);
                }
                return result;
            }
        }
    }

    std::size_t identity_bytes { };
    for (auto& value : index.values) {
        value.canonical_identity = value_identity(value);
        if (value.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - value.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-006",
                "SDF VITAL effective-value identities exceed their byte limit",
                { });
            return result;
        }
        identity_bytes += value.canonical_identity.size();
    }
    std::vector<std::shared_ptr<const SdfVitalModelPlan>> retained_revisions(
        revisions.begin(), revisions.end());
    std::vector<SdfVitalTimingGenericValue> retained_generics(
        generics.begin(), generics.end());
    std::vector<SdfVitalAnnotationControl> retained_controls(
        controls.begin(), controls.end());
    auto semantic_identity = std::string { "sdf-vital-precedence-v1" };
    append_field(semantic_identity, source->semantic_identity());
    append_field(semantic_identity,
        std::to_string(static_cast<unsigned>(policy.command_selection)));
    append_field(semantic_identity,
        policy.delay_annotations_enabled ? "delay-on" : "delay-off");
    append_field(semantic_identity,
        policy.timing_check_annotations_enabled ? "check-on" : "check-off");
    append_field(semantic_identity,
        std::ranges::all_of(index.values,
            &SdfVitalEffectiveTimingValue::no_annotation)
            ? "no-annotation"
            : "annotated");
    for (const auto& value : index.values)
        append_field(semantic_identity, value.canonical_identity);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PRECEDENCE-006",
            "SDF VITAL precedence semantic identity exceeds its byte limit",
            { });
        return result;
    }
    result.application
        = std::make_shared<const SdfVitalPrecedenceApplication>(
            std::move(source), std::move(retained_revisions),
            std::move(retained_generics), std::move(retained_controls), policy,
            std::move(index.values), std::move(semantic_identity));
    return result;
}

} // namespace fsim::app
