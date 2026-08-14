// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_pulse_timing.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::app {
namespace {
    using boost::multiprecision::cpp_int;
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfConstructKind;
    using frontend::SdfExactDecimal;
    using frontend::SdfIr;
    using frontend::SourceSpan;
    using runtime::SimulationTick;

    struct PulseDraft {
        const elaboration::VerilogSpecifyPathInfo* source_path { };
        elaboration::VerilogSpecifyPathInfo effective_path;
        std::vector<SimulationTick> source_reject;
        std::vector<SimulationTick> source_error;
        std::vector<SimulationTick> source_retain;
        std::vector<SimulationTick> effective_reject;
        std::vector<SimulationTick> effective_error;
        std::vector<SimulationTick> effective_retain;
        std::vector<SdfSelectedPercentage> percentages;
        SdfConstructKind pulse_kind { SdfConstructKind::Unknown };
        bool global { };
        bool sdf_pulse { };
        bool sdf_retain { };
        std::vector<SourceSpan> sources;
        std::vector<std::string> identities;
    };

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

    [[nodiscard]] bool legal_delay_count(const std::size_t count) noexcept
    {
        static constexpr auto counts
            = std::to_array<std::size_t>({ 1U, 2U, 3U, 6U, 12U });
        return std::ranges::find(counts, count) != counts.end();
    }

    [[nodiscard]] cpp_int decimal_coefficient(const std::string_view digits)
    {
        cpp_int result { };
        for (const auto digit : digits) {
            if (digit < '0' || digit > '9')
                return -1;
            result *= 10U;
            result += static_cast<unsigned>(digit - '0');
        }
        return result;
    }

    [[nodiscard]] cpp_int power_of_ten(const std::size_t exponent)
    {
        cpp_int result { 1U };
        for (std::size_t index = 0; index < exponent; ++index)
            result *= 10U;
        return result;
    }

    [[nodiscard]] std::optional<SimulationTick> percentage_delay(
        const SimulationTick delay, const SdfExactDecimal& percentage,
        const SdfValuePolicy& policy)
    {
        if (percentage.coefficient.empty()
            || percentage.coefficient.size() > policy.max_decimal_digits
            || (percentage.negative && percentage.coefficient != "0")) {
            return std::nullopt;
        }
        const auto coefficient = decimal_coefficient(percentage.coefficient);
        if (coefficient < 0)
            return std::nullopt;
        auto numerator = cpp_int { delay } * coefficient;
        auto denominator = cpp_int { 100U };
        if (percentage.exponent10 >= 0) {
            if (static_cast<std::uint64_t>(percentage.exponent10)
                > policy.max_power10) {
                return std::nullopt;
            }
            numerator *= power_of_ten(
                static_cast<std::size_t>(percentage.exponent10));
        } else {
            if (percentage.exponent10
                    == std::numeric_limits<std::int64_t>::min()
                || static_cast<std::uint64_t>(-percentage.exponent10)
                    > policy.max_power10) {
                return std::nullopt;
            }
            denominator *= power_of_ten(
                static_cast<std::size_t>(-percentage.exponent10));
        }
        cpp_int selected = numerator / denominator;
        if ((numerator % denominator) * 2U >= denominator)
            ++selected;
        if (selected > std::numeric_limits<SimulationTick>::max())
            return std::nullopt;
        return selected.convert_to<SimulationTick>();
    }

    [[nodiscard]] std::optional<std::vector<SimulationTick>> percentage_table(
        const std::vector<SimulationTick>& delays,
        const SdfSelectedPercentage& percentage,
        const SdfValuePolicy& policy)
    {
        frontend::SdfExactValue source;
        source.kind = frontend::SdfExactValueKind::Scalar;
        source.components[0] = percentage.exact_source_value;
        const auto validated = select_sdf_percentage(source, policy);
        if (!validated.ok() || *validated.selected != percentage)
            return std::nullopt;
        std::vector<SimulationTick> result;
        result.reserve(delays.size());
        for (const auto delay : delays) {
            const auto selected
                = percentage_delay(delay, percentage.exact_source_value, policy);
            if (!selected)
                return std::nullopt;
            result.push_back(*selected);
        }
        return result;
    }

    [[nodiscard]] std::vector<SimulationTick> source_reject_table(
        const elaboration::VerilogSpecifyPathInfo& path)
    {
        if (!path.pulse_reject_delays.empty())
            return path.pulse_reject_delays;
        if (path.pulse_reject_limit)
            return { *path.pulse_reject_limit };
        return { };
    }

    [[nodiscard]] std::vector<SimulationTick> source_error_table(
        const elaboration::VerilogSpecifyPathInfo& path)
    {
        if (!path.pulse_error_delays.empty())
            return path.pulse_error_delays;
        if (path.pulse_error_limit)
            return { *path.pulse_error_limit };
        return { };
    }

    [[nodiscard]] bool valid_paired_tables(
        const std::vector<SimulationTick>& reject,
        const std::vector<SimulationTick>& error) noexcept
    {
        return reject.size() == error.size()
            && (reject.empty() || legal_delay_count(reject.size()))
            && std::ranges::equal(reject, error,
                [](const auto reject_value, const auto error_value) {
                    return reject_value <= error_value;
                });
    }

    [[nodiscard]] bool valid_source_path(
        const elaboration::VerilogSpecifyPathInfo& path,
        const SdfPulseTimingLimits& limits) noexcept
    {
        const auto reject = source_reject_table(path);
        const auto error = source_error_table(path);
        return !path.identity.empty() && !path.instance.empty()
            && legal_delay_count(path.delays.size())
            && path.delays.size() <= limits.max_values_per_path
            && valid_paired_tables(reject, error)
            && (path.retain_delays.empty()
                || (legal_delay_count(path.retain_delays.size())
                    && path.retain_delays.size()
                        <= limits.max_values_per_path));
    }

    [[nodiscard]] PulseDraft make_draft(
        const elaboration::VerilogSpecifyPathInfo& source,
        const elaboration::VerilogSpecifyPathInfo& effective)
    {
        PulseDraft draft;
        draft.source_path = &source;
        draft.effective_path = effective;
        draft.source_reject = source_reject_table(source);
        draft.source_error = source_error_table(source);
        draft.source_retain = source.retain_delays;
        draft.effective_reject = source_reject_table(effective);
        draft.effective_error = source_error_table(effective);
        draft.effective_retain = effective.retain_delays;
        return draft;
    }

    [[nodiscard]] bool set_retain(const SdfPlannedAnnotation& annotation,
        PulseDraft& draft, const SdfPulseTimingLimits& limits,
        std::vector<Diagnostic>& diagnostics)
    {
        if (draft.sdf_retain || !legal_delay_count(annotation.retain_ticks.size())
            || annotation.retain_ticks.size() > limits.max_values_per_path) {
            diagnose(diagnostics, "FSIM-SDF-PULSE-003",
                "SDF RETAIN timing is duplicated or has unsupported transition arity",
                annotation.source);
            return false;
        }
        draft.sdf_retain = true;
        draft.effective_retain.assign(
            annotation.retain_ticks.begin(), annotation.retain_ticks.end());
        draft.effective_path.retain_delays = draft.effective_retain;
        draft.sources.push_back(annotation.source);
        draft.identities.push_back(annotation.canonical_identity);
        return true;
    }

    [[nodiscard]] bool set_pulse(const SdfPlannedAnnotation& annotation,
        const bool global, PulseDraft& draft,
        const SdfPulseTimingLimits& limits, const SdfValuePolicy& policy,
        std::vector<Diagnostic>& diagnostics)
    {
        if (draft.sdf_pulse) {
            diagnose(diagnostics, "FSIM-SDF-PULSE-002",
                "SDF pulse annotations conflict on one elaborated path",
                annotation.source);
            return false;
        }
        std::vector<SimulationTick> reject;
        std::vector<SimulationTick> error;
        if (annotation.construct_kind == SdfConstructKind::PathPulse) {
            if (annotation.after_ticks.empty()
                || annotation.after_ticks.size() > 2U) {
                diagnose(diagnostics, "FSIM-SDF-PULSE-003",
                    "SDF PATHPULSE requires one or two exact thresholds",
                    annotation.source);
                return false;
            }
            reject = { annotation.after_ticks.front() };
            error = { annotation.after_ticks.size() == 2U
                    ? annotation.after_ticks.back()
                    : annotation.after_ticks.front() };
        } else if (annotation.construct_kind
            == SdfConstructKind::PathPulsePercent) {
            if (annotation.after_percentages.empty()
                || annotation.after_percentages.size() > 2U) {
                diagnose(diagnostics, "FSIM-SDF-PULSE-003",
                    "SDF PATHPULSEPERCENT requires one or two exact percentages",
                    annotation.source);
                return false;
            }
            const auto selected_reject = percentage_table(
                draft.effective_path.delays,
                annotation.after_percentages.front(), policy);
            const auto selected_error = percentage_table(
                draft.effective_path.delays,
                annotation.after_percentages.size() == 2U
                    ? annotation.after_percentages.back()
                    : annotation.after_percentages.front(),
                policy);
            if (!selected_reject || !selected_error) {
                diagnose(diagnostics, "FSIM-SDF-PULSE-004",
                    "SDF pulse percentage cannot be represented within exact conversion limits",
                    annotation.source);
                return false;
            }
            reject = *selected_reject;
            error = *selected_error;
            draft.percentages = annotation.after_percentages;
        } else {
            diagnose(diagnostics, "FSIM-SDF-PULSE-001",
                "SDF pulse plan has an unsupported construct kind",
                annotation.source);
            return false;
        }
        if (reject.size() > limits.max_values_per_path
            || !valid_paired_tables(reject, error)) {
            diagnose(diagnostics, "FSIM-SDF-PULSE-003",
                "SDF pulse reject thresholds exceed error thresholds or resource limits",
                annotation.source);
            return false;
        }
        draft.sdf_pulse = true;
        draft.global = global;
        draft.pulse_kind = annotation.construct_kind;
        draft.effective_reject = std::move(reject);
        draft.effective_error = std::move(error);
        draft.effective_path.pulse_reject_limit.reset();
        draft.effective_path.pulse_error_limit.reset();
        draft.effective_path.pulse_reject_delays = draft.effective_reject;
        draft.effective_path.pulse_error_delays = draft.effective_error;
        draft.sources.push_back(annotation.source);
        draft.identities.push_back(annotation.canonical_identity);
        return true;
    }

    [[nodiscard]] std::string applied_identity(const PulseDraft& draft)
    {
        std::string result = "sdf-pulse-timing-v1";
        append_field(result, std::to_string(draft.source_path->id));
        append_field(result, draft.source_path->identity);
        append_field(result,
            std::to_string(static_cast<unsigned>(draft.pulse_kind)));
        append_field(result, draft.global ? "global" : "targeted");
        append_field(result,
            std::to_string(static_cast<unsigned>(draft.effective_path.polarity)));
        append_field(result,
            std::to_string(static_cast<unsigned>(draft.effective_path.pulse_style)));
        append_field(result,
            draft.effective_path.show_cancelled ? "show" : "hide");
        const auto append_values = [&](const auto& values) {
            result.push_back('|');
            for (const auto value : values)
                append_field(result, std::to_string(value));
        };
        append_values(draft.source_reject);
        append_values(draft.source_error);
        append_values(draft.source_retain);
        append_values(draft.effective_reject);
        append_values(draft.effective_error);
        append_values(draft.effective_retain);
        for (const auto& percentage : draft.percentages)
            append_field(result, percentage.canonical_identity);
        for (const auto& identity : draft.identities)
            append_field(result, identity);
        return result;
    }

    [[nodiscard]] std::string application_identity(
        const SdfAnnotationPlan& plan,
        const std::vector<SdfAppliedPulseTiming>& paths)
    {
        std::string result = "sdf-pulse-application-v1";
        append_field(result, plan.semantic_identity());
        for (const auto& path : paths)
            append_field(result, path.canonical_identity);
        return result;
    }

    using SourcePathByIdentity
        = std::map<std::string,
            const elaboration::VerilogSpecifyPathInfo*>;
    using SourcePathById
        = std::map<elaboration::VerilogSpecifyPathId,
            const elaboration::VerilogSpecifyPathInfo*>;
    using SourcePathsByInstance
        = std::map<std::string,
            std::vector<const elaboration::VerilogSpecifyPathInfo*>>;
    using EffectivePathById
        = std::map<elaboration::VerilogSpecifyPathId,
            elaboration::VerilogSpecifyPathInfo>;
    using PulseDraftById
        = std::map<elaboration::VerilogSpecifyPathId, PulseDraft>;

    [[nodiscard]] bool collect_source_paths(
        const elaboration::ElaboratedDesign& elaborated,
        const SdfPulseTimingLimits& limits,
        SourcePathByIdentity& by_identity, SourcePathById& by_id,
        SourcePathsByInstance& by_instance,
        std::vector<Diagnostic>& diagnostics)
    {
        for (const auto& path : elaborated.verilog_specify_paths()) {
            if (!valid_source_path(path, limits)
                || !by_identity.emplace(path.identity, &path).second
                || !by_id.emplace(path.id, &path).second) {
                diagnose(diagnostics, "FSIM-SDF-PULSE-001",
                    "Elaborated pulse path identity, delay profile, or source limits are invalid",
                    path.source);
                continue;
            }
            by_instance[path.instance].push_back(&path);
        }
        return diagnostics.empty();
    }

    [[nodiscard]] EffectivePathById collect_effective_paths(
        const SourcePathById& sources,
        const SdfPathTimingApplication& application)
    {
        EffectivePathById result;
        for (const auto& [id, path] : sources)
            result.emplace(id, *path);
        for (const auto& applied : application.paths())
            result[applied.path_id] = applied.effective_path;
        return result;
    }

    [[nodiscard]] PulseDraft& draft_for(
        const elaboration::VerilogSpecifyPathInfo& path,
        const EffectivePathById& effective, PulseDraftById& drafts)
    {
        auto [found, inserted] = drafts.try_emplace(path.id);
        if (inserted)
            found->second = make_draft(path, effective.at(path.id));
        return found->second;
    }

    struct PulseAnnotationContext {
        const SdfAnnotationPlan& plan;
        const SdfIr& ir;
        const SdfPulseTimingLimits& limits;
        const SourcePathByIdentity& source_by_identity;
        const SourcePathsByInstance& paths_by_instance;
        const EffectivePathById& effective_by_id;
        PulseDraftById& drafts;
        std::vector<Diagnostic>& diagnostics;
    };

    void apply_planned_pulse(const SdfPlannedAnnotation& annotation,
        PulseAnnotationContext& context)
    {
        if (annotation.target_kind == SdfTimingTargetKind::SpecifyPath
            && !annotation.retain_ticks.empty()) {
            const auto found
                = context.source_by_identity.find(annotation.target_identity);
            if (found == context.source_by_identity.end()) {
                diagnose(context.diagnostics, "FSIM-SDF-PULSE-001",
                    "SDF RETAIN plan refers to a missing elaborated path",
                    annotation.source);
                return;
            }
            (void)set_retain(annotation,
                draft_for(*found->second, context.effective_by_id,
                    context.drafts),
                context.limits, context.diagnostics);
            return;
        }
        if (annotation.target_kind != SdfTimingTargetKind::Pulse)
            return;
        const auto* node = context.ir.find_node(annotation.node_id);
        if (node == nullptr || node->kind != annotation.construct_kind) {
            diagnose(context.diagnostics, "FSIM-SDF-PULSE-001",
                "SDF pulse plan refers to a missing or stale normalized node",
                annotation.source);
            return;
        }
        const bool legacy_global
            = node->profile_identity == "sdf21:globalpathpulse";
        const bool global = annotation.construct_kind
                == SdfConstructKind::PathPulsePercent
            && annotation.endpoints.empty();
        if ((legacy_global
                && context.ir.revision() != frontend::SdfRevision::Sdf21)
            || (global && !legacy_global
                && context.ir.revision() == frontend::SdfRevision::Sdf21)) {
            diagnose(context.diagnostics, "FSIM-SDF-PULSE-001",
                "GLOBALPATHPULSE spelling is legal only in its governed SDF revision",
                annotation.source);
            return;
        }
        if (global) {
            const auto found = context.paths_by_instance.find(
                annotation.target_instance_path);
            if (found == context.paths_by_instance.end()
                || found->second.empty()) {
                diagnose(context.diagnostics, "FSIM-SDF-PULSE-001",
                    "Global SDF pulse annotation has no elaborated paths in its target instance",
                    annotation.source);
                return;
            }
            for (const auto* path : found->second) {
                (void)set_pulse(annotation, true,
                    draft_for(*path, context.effective_by_id, context.drafts),
                    context.limits, context.plan.value_policy(),
                    context.diagnostics);
            }
            return;
        }
        const auto found
            = context.source_by_identity.find(annotation.target_identity);
        if (found == context.source_by_identity.end()
            || found->second->instance != annotation.target_instance_path) {
            diagnose(context.diagnostics, "FSIM-SDF-PULSE-001",
                "SDF pulse plan refers to a missing or stale elaborated path",
                annotation.source);
            return;
        }
        (void)set_pulse(annotation, false,
            draft_for(
                *found->second, context.effective_by_id, context.drafts),
            context.limits, context.plan.value_policy(), context.diagnostics);
    }

    [[nodiscard]] std::vector<SdfAppliedPulseTiming> finalize_drafts(
        PulseDraftById& drafts, const SdfPulseTimingLimits& limits,
        std::vector<Diagnostic>& diagnostics)
    {
        std::vector<SdfAppliedPulseTiming> paths;
        paths.reserve(drafts.size());
        std::size_t identity_bytes { };
        for (auto& [id, draft] : drafts) {
            SdfAppliedPulseTiming applied;
            applied.canonical_identity = applied_identity(draft);
            applied.path_id = id;
            applied.pulse_construct_kind = draft.pulse_kind;
            applied.global_annotation = draft.global;
            applied.source_reject_delays = draft.source_reject;
            applied.source_error_delays = draft.source_error;
            applied.source_retain_delays = draft.source_retain;
            applied.effective_reject_delays = draft.effective_reject;
            applied.effective_error_delays = draft.effective_error;
            applied.effective_retain_delays = draft.effective_retain;
            applied.selected_percentages = draft.percentages;
            applied.effective_path = std::move(draft.effective_path);
            applied.annotation_sources = std::move(draft.sources);
            applied.annotation_identities = std::move(draft.identities);
            if (applied.canonical_identity.size() > limits.max_identity_bytes
                || identity_bytes
                    > limits.max_identity_bytes
                        - applied.canonical_identity.size()) {
                diagnose(diagnostics, "FSIM-SDF-PULSE-004",
                    "SDF pulse application exceeds its identity-byte limit",
                    applied.annotation_sources.empty()
                        ? SourceSpan { }
                        : applied.annotation_sources.front());
                continue;
            }
            identity_bytes += applied.canonical_identity.size();
            paths.push_back(std::move(applied));
        }
        return paths;
    }
} // namespace

const std::shared_ptr<const SdfAnnotationPlan>& SdfPulseTimingApplication::plan()
    const noexcept
{
    return plan_;
}

std::span<const SdfAppliedPulseTiming> SdfPulseTimingApplication::paths() const
    noexcept
{
    return paths_;
}

const SdfAppliedPulseTiming* SdfPulseTimingApplication::find_path(
    const elaboration::VerilogSpecifyPathId id) const noexcept
{
    const auto found
        = std::ranges::find(paths_, id, &SdfAppliedPulseTiming::path_id);
    return found == paths_.end() ? nullptr : &*found;
}

std::string_view SdfPulseTimingApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfPulseTimingApplication::SdfPulseTimingApplication(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    std::vector<SdfAppliedPulseTiming> paths, std::string semantic_identity)
    : plan_(std::move(plan))
    , paths_(std::move(paths))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfPulseTimingResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfPulseTimingResult apply_sdf_pulse_timing(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfPulseTimingLimits limits)
{
    SdfPulseTimingResult result;
    if (!plan || !plan->summary() || !plan->summary()->endpoint_resolution()
        || !plan->summary()->endpoint_resolution()->cells()
        || !plan->summary()->endpoint_resolution()->cells()->scope()
        || !plan->summary()->endpoint_resolution()->cells()->scope()->normalized_ir()
        || plan->summary()->semantic_identity().empty()
        || plan->semantic_identity().empty()
        || plan->summary()->annotation_count() != plan->annotations().size()
        || limits.max_paths == 0U
        || limits.max_values_per_path == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-PULSE-001",
            "SDF pulse application requires a complete plan and nonzero limits",
            { });
        return result;
    }
    const auto path_application
        = apply_sdf_path_timing(plan, elaborated,
            { limits.max_paths, limits.max_values_per_path,
                limits.max_identity_bytes });
    if (!path_application.ok()) {
        result.diagnostics = path_application.diagnostics;
        return result;
    }
    const auto& ir = *plan->summary()
                          ->endpoint_resolution()
                          ->cells()
                          ->scope()
                          ->normalized_ir();
    SourcePathByIdentity source_by_identity;
    SourcePathById source_by_id;
    SourcePathsByInstance paths_by_instance;
    if (!collect_source_paths(elaborated, limits, source_by_identity,
            source_by_id, paths_by_instance, result.diagnostics)) {
        return result;
    }
    const auto effective_by_id = collect_effective_paths(
        source_by_id, *path_application.application);
    PulseDraftById drafts;
    PulseAnnotationContext context { *plan, ir, limits, source_by_identity,
        paths_by_instance, effective_by_id, drafts, result.diagnostics };
    for (const auto& annotation : plan->annotations())
        apply_planned_pulse(annotation, context);
    if (!result.diagnostics.empty())
        return result;
    if (drafts.size() > limits.max_paths) {
        diagnose(result.diagnostics, "FSIM-SDF-PULSE-004",
            "SDF pulse application exceeds its configured path limit", { });
        return result;
    }
    auto paths = finalize_drafts(drafts, limits, result.diagnostics);
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(*plan, paths);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-PULSE-004",
            "SDF pulse semantic identity exceeds its configured limit", { });
        return result;
    }
    result.application = std::make_shared<const SdfPulseTimingApplication>(
        std::move(plan), std::move(paths), identity);
    return result;
}

} // namespace fsim::app
