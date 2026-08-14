// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_path_timing.hpp"

#include <algorithm>
#include <array>
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

    [[nodiscard]] const elaboration::VerilogSpecifyPathInfo* find_path(
        const elaboration::ElaboratedDesign& elaborated,
        const std::string_view identity)
    {
        const auto& paths = elaborated.verilog_specify_paths();
        const auto found = std::ranges::find(
            paths, identity, &elaboration::VerilogSpecifyPathInfo::identity);
        return found == paths.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool legal_delay_count(const std::size_t count) noexcept
    {
        static constexpr auto counts
            = std::to_array<std::size_t>({ 1U, 2U, 3U, 6U, 12U });
        return std::ranges::find(counts, count) != counts.end();
    }

    [[nodiscard]] std::string applied_identity(
        const SdfPlannedAnnotation& annotation,
        const elaboration::VerilogSpecifyPathInfo& path,
        const std::vector<runtime::SimulationTick>& effective)
    {
        std::string result = "sdf-path-timing-v1";
        append_field(result, annotation.canonical_identity);
        append_field(result, path.identity);
        append_field(
            result, std::to_string(static_cast<unsigned>(annotation.delay_mode)));
        append_field(result, std::to_string(static_cast<unsigned>(path.kind)));
        append_field(result, std::to_string(static_cast<unsigned>(path.source_edge)));
        append_field(result, std::to_string(static_cast<unsigned>(path.polarity)));
        append_field(result, path.conditional ? "conditional" : "unconditional");
        append_field(result, path.ifnone ? "ifnone" : "selected");
        for (const auto value : effective)
            append_field(result, std::to_string(value));
        return result;
    }

    [[nodiscard]] bool validate_path_semantics(
        const SdfPlannedAnnotation& annotation,
        const elaboration::VerilogSpecifyPathInfo& path,
        std::vector<Diagnostic>& diagnostics)
    {
        if (path.instance != annotation.target_instance_path
            || path.identity != annotation.target_identity
            || annotation.delay_mode == SdfDelayApplicationMode::None
            || annotation.before_ticks != path.delays) {
            diagnose(diagnostics, "FSIM-SDF-PATH-001",
                "SDF path plan is stale relative to its elaborated specify target or delay mode",
                annotation.source);
            return false;
        }
        if ((!annotation.condition_identity.empty()
                && !path.conditional && !path.ifnone)
            || (!annotation.edge_identities.empty()
                && path.source_edge == frontend::VerilogSpecifyEdge::None)) {
            diagnose(diagnostics, "FSIM-SDF-PATH-002",
                "SDF path condition or edge does not match the elaborated specify path",
                annotation.source);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool effective_delays(const SdfPlannedAnnotation& annotation,
        const elaboration::VerilogSpecifyPathInfo& path,
        const SdfPathTimingLimits& limits,
        std::vector<runtime::SimulationTick>& effective,
        std::vector<Diagnostic>& diagnostics)
    {
        if (!legal_delay_count(annotation.after_ticks.size())
            || annotation.after_ticks.size() > limits.max_values_per_path) {
            diagnose(diagnostics, "FSIM-SDF-PATH-003",
                "SDF IOPATH has an empty, unsupported, or over-limit delay arity",
                annotation.source);
            return false;
        }
        effective.reserve(annotation.after_ticks.size());
        if (annotation.delay_mode == SdfDelayApplicationMode::Absolute) {
            effective.assign(
                annotation.after_ticks.begin(), annotation.after_ticks.end());
            return true;
        }
        if (annotation.delay_mode != SdfDelayApplicationMode::Increment
            || annotation.after_ticks.size() != path.delays.size()) {
            diagnose(diagnostics, "FSIM-SDF-PATH-003",
                "Incremental SDF IOPATH arity does not match its source delay profile",
                annotation.source);
            return false;
        }
        for (std::size_t index = 0; index < path.delays.size(); ++index) {
            const auto increment = annotation.after_ticks[index];
            const auto source = path.delays[index];
            if (increment > std::numeric_limits<runtime::SimulationTick>::max()
                    - source) {
                diagnose(diagnostics, "FSIM-SDF-PATH-004",
                    "Incremental SDF IOPATH overflows the simulator tick range",
                    annotation.source);
                return false;
            }
            effective.push_back(source + increment);
        }
        return true;
    }

    [[nodiscard]] std::string application_identity(
        const SdfAnnotationPlan& plan,
        const std::vector<SdfAppliedPathTiming>& paths)
    {
        std::string result = "sdf-path-application-v1";
        append_field(result, plan.semantic_identity());
        for (const auto& path : paths)
            append_field(result, path.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfAnnotationPlan>& SdfPathTimingApplication::plan()
    const noexcept
{
    return plan_;
}

std::span<const SdfAppliedPathTiming> SdfPathTimingApplication::paths() const
    noexcept
{
    return paths_;
}

const SdfAppliedPathTiming* SdfPathTimingApplication::find_path(
    const elaboration::VerilogSpecifyPathId id) const noexcept
{
    const auto found
        = std::ranges::find(paths_, id, &SdfAppliedPathTiming::path_id);
    return found == paths_.end() ? nullptr : &*found;
}

std::string_view SdfPathTimingApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfPathTimingApplication::SdfPathTimingApplication(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    std::vector<SdfAppliedPathTiming> paths, std::string semantic_identity)
    : plan_(std::move(plan))
    , paths_(std::move(paths))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfPathTimingResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

SdfPathTimingResult apply_sdf_path_timing(
    std::shared_ptr<const SdfAnnotationPlan> plan,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfPathTimingLimits limits)
{
    SdfPathTimingResult result;
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
        diagnose(result.diagnostics, "FSIM-SDF-PATH-001",
            "SDF path application requires a complete plan and nonzero limits",
            { });
        return result;
    }
    std::vector<SdfAppliedPathTiming> paths;
    std::set<elaboration::VerilogSpecifyPathId> path_ids;
    std::size_t identity_bytes { };
    for (const auto& annotation : plan->annotations()) {
        if (annotation.target_kind != SdfTimingTargetKind::SpecifyPath)
            continue;
        if (paths.size() >= limits.max_paths) {
            diagnose(result.diagnostics, "FSIM-SDF-PATH-004",
                "SDF path application exceeds its configured path limit",
                annotation.source);
            continue;
        }
        const auto* path = find_path(elaborated, annotation.target_identity);
        if (path == nullptr
            || !validate_path_semantics(annotation, *path, result.diagnostics)) {
            if (path == nullptr) {
                diagnose(result.diagnostics, "FSIM-SDF-PATH-001",
                    "SDF path plan refers to a missing elaborated specify identity",
                    annotation.source);
            }
            continue;
        }
        std::vector<runtime::SimulationTick> effective;
        if (!effective_delays(
                annotation, *path, limits, effective, result.diagnostics)) {
            continue;
        }
        if (!path_ids.insert(path->id).second) {
            diagnose(result.diagnostics, "FSIM-SDF-PATH-001",
                "SDF path application repeats one elaborated specify target",
                annotation.source);
            continue;
        }
        SdfAppliedPathTiming applied;
        applied.path_id = path->id;
        applied.mode = annotation.delay_mode;
        applied.source_delays = path->delays;
        applied.effective_delays = effective;
        applied.effective_path = *path;
        applied.effective_path.delays = effective;
        applied.annotation_source = annotation.source;
        applied.annotation_identity = annotation.canonical_identity;
        applied.canonical_identity
            = applied_identity(annotation, *path, effective);
        if (applied.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes
                > limits.max_identity_bytes - applied.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-PATH-004",
                "SDF path application exceeds its configured identity-byte limit",
                annotation.source);
            continue;
        }
        identity_bytes += applied.canonical_identity.size();
        paths.push_back(std::move(applied));
    }
    if (!result.diagnostics.empty())
        return result;
    const auto identity = application_identity(*plan, paths);
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-PATH-004",
            "SDF path application semantic identity exceeds its configured limit",
            { });
        return result;
    }
    result.application = std::make_shared<const SdfPathTimingApplication>(
        std::move(plan), std::move(paths), identity);
    return result;
}

} // namespace fsim::app
