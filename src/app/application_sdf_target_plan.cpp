// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_target_plan.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::SdfConstructKind;
    using frontend::SdfIr;
    using frontend::SdfIrNode;
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

    [[nodiscard]] bool timing_check_kind(const SdfConstructKind kind) noexcept
    {
        static constexpr auto kinds = std::to_array<SdfConstructKind>({
            SdfConstructKind::Setup,
            SdfConstructKind::Hold,
            SdfConstructKind::SetupHold,
            SdfConstructKind::Recovery,
            SdfConstructKind::Removal,
            SdfConstructKind::RecRem,
            SdfConstructKind::Skew,
            SdfConstructKind::BidirectSkew,
            SdfConstructKind::Width,
            SdfConstructKind::Period,
            SdfConstructKind::NoChange,
        });
        return std::ranges::find(kinds, kind) != kinds.end();
    }

    [[nodiscard]] SdfTimingTargetKind target_kind(
        const SdfConstructKind kind) noexcept
    {
        if (timing_check_kind(kind))
            return SdfTimingTargetKind::TimingCheck;
        switch (kind) {
        case SdfConstructKind::Iopath:
            return SdfTimingTargetKind::SpecifyPath;
        case SdfConstructKind::Interconnect:
        case SdfConstructKind::NetDelay:
            return SdfTimingTargetKind::Interconnect;
        case SdfConstructKind::Port:
            return SdfTimingTargetKind::Port;
        case SdfConstructKind::Mipd:
            return SdfTimingTargetKind::Mipd;
        case SdfConstructKind::Device:
            return SdfTimingTargetKind::Device;
        case SdfConstructKind::PathPulse:
        case SdfConstructKind::PathPulsePercent:
            return SdfTimingTargetKind::Pulse;
        default:
            return SdfTimingTargetKind::TimingEnvironment;
        }
    }

    [[nodiscard]] std::string target_kind_identity(
        const SdfTimingTargetKind kind)
    {
        switch (kind) {
        case SdfTimingTargetKind::SpecifyPath:
            return "specify-path";
        case SdfTimingTargetKind::Interconnect:
            return "interconnect";
        case SdfTimingTargetKind::Port:
            return "port";
        case SdfTimingTargetKind::Mipd:
            return "mipd";
        case SdfTimingTargetKind::Device:
            return "device";
        case SdfTimingTargetKind::TimingCheck:
            return "timing-check";
        case SdfTimingTargetKind::Pulse:
            return "pulse";
        case SdfTimingTargetKind::TimingEnvironment:
            return "timing-environment";
        }
        return "invalid";
    }

    [[nodiscard]] SdfDelayApplicationMode annotation_mode(
        const SdfIr& ir, const std::uint64_t node_id) noexcept
    {
        const auto* node = ir.find_node(node_id);
        while (node != nullptr && node->parent_id != 0U) {
            node = ir.find_node(node->parent_id);
            if (node == nullptr)
                return SdfDelayApplicationMode::None;
            if (node->kind == SdfConstructKind::Absolute)
                return SdfDelayApplicationMode::Absolute;
            if (node->kind == SdfConstructKind::Increment)
                return SdfDelayApplicationMode::Increment;
        }
        return SdfDelayApplicationMode::None;
    }

    [[nodiscard]] const SdfResolvedInstance* find_instance(
        const SdfCellResolution& cells, const std::uint64_t cell_id,
        const std::string_view instance_path)
    {
        const auto* cell = cells.find_cell(cell_id);
        if (cell == nullptr)
            return nullptr;
        const auto found = std::ranges::find(
            cell->targets, instance_path, &SdfResolvedInstance::instance_path);
        return found == cell->targets.end() ? nullptr : &*found;
    }

    [[nodiscard]] bool verilog_language(
        const SdfScopeRootLanguage language) noexcept
    {
        return language == SdfScopeRootLanguage::Verilog
            || language == SdfScopeRootLanguage::SystemVerilog;
    }

    [[nodiscard]] const elaboration::VerilogSpecifyPathInfo* find_specify_path(
        const elaboration::ElaboratedDesign& elaborated,
        const elaboration::VerilogSpecifyPathId id)
    {
        const auto& paths = elaborated.verilog_specify_paths();
        const auto found
            = std::ranges::find(paths, id, &elaboration::VerilogSpecifyPathInfo::id);
        return found == paths.end() ? nullptr : &*found;
    }

    [[nodiscard]] const runtime::simir::ModuleTimingCheck* find_timing_check(
        const elaboration::ElaboratedDesign& elaborated, const std::uint32_t id)
    {
        const auto& checks = elaborated.verilog_timing_checks();
        const auto found
            = std::ranges::find(checks, id, &runtime::simir::ModuleTimingCheck::id);
        return found == checks.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::string endpoint_target_identity(
        const SdfResolvedNodeEndpoints& mapping)
    {
        std::string result = mapping.target_instance_path;
        for (const auto& endpoint : mapping.endpoints) {
            append_field(result, endpoint.object_path);
            append_field(result, std::to_string(endpoint.signal));
            append_field(result, endpoint.edge_identity);
            append_field(result, endpoint.condition_identity);
        }
        return result;
    }

    [[nodiscard]] std::vector<const SdfIrNode*> annotation_value_nodes(
        const SdfIr& ir, const std::uint64_t root_id)
    {
        std::unordered_set<std::uint64_t> descendants { root_id };
        std::vector<const SdfIrNode*> values;
        for (const auto& node : ir.nodes()) {
            if (node.id != root_id && !descendants.contains(node.parent_id))
                continue;
            descendants.insert(node.id);
            if (node.exact_value)
                values.push_back(&node);
        }
        return values;
    }

    [[nodiscard]] bool descends_from_kind(const SdfIr& ir,
        const SdfIrNode& node, const SdfConstructKind kind) noexcept
    {
        const auto* ancestor = &node;
        while (ancestor->parent_id != 0U) {
            ancestor = ir.find_node(ancestor->parent_id);
            if (ancestor == nullptr)
                return false;
            if (ancestor->kind == kind)
                return true;
        }
        return false;
    }

    [[nodiscard]] bool timing_check_limits_valid(
        const runtime::simir::ModuleTimingCheck& check) noexcept
    {
        using runtime::simir::ModuleTimingCheckKind;
        const bool compound = check.kind == ModuleTimingCheckKind::setuphold
            || check.kind == ModuleTimingCheckKind::recrem
            || check.kind == ModuleTimingCheckKind::fullskew
            || check.kind == ModuleTimingCheckKind::nochange;
        if (check.limits.size() != (compound ? 2U : 1U))
            return false;
        if (check.kind == ModuleTimingCheckKind::nochange)
            return check.limits[0] <= check.limits[1];
        if (check.kind == ModuleTimingCheckKind::setuphold
            || check.kind == ModuleTimingCheckKind::recrem) {
            const auto left = check.limits[0];
            const auto right = check.limits[1];
            if ((right > 0
                    && left > std::numeric_limits<std::int64_t>::max() - right)
                || (right < 0
                    && left
                        < std::numeric_limits<std::int64_t>::min() - right)) {
                return false;
            }
            return left + right > 0;
        }
        return std::ranges::none_of(
            check.limits, [](const auto limit) { return limit < 0; });
    }

    [[nodiscard]] bool planned_values(const SdfIr& ir,
        const SdfResolvedNodeEndpoints& mapping, const SdfValuePolicy& policy,
        const SdfTargetPlanLimits& limits, SdfPlannedAnnotation& annotation,
        std::vector<Diagnostic>& diagnostics)
    {
        if (!ir.timescale()) {
            diagnose(diagnostics, "FSIM-SDF-PLAN-001",
                "SDF target planning requires a normalized timescale", { });
            return false;
        }
        const auto nodes = annotation_value_nodes(ir, mapping.node_id);
        if (nodes.empty() || nodes.size() > limits.max_values_per_annotation) {
            const auto* root = ir.find_node(mapping.node_id);
            diagnose(diagnostics, nodes.empty() ? "FSIM-SDF-PLAN-002" : "FSIM-SDF-PLAN-004",
                nodes.empty()
                    ? "SDF annotation has no exact delay or timing-check value"
                    : "SDF annotation exceeds its configured target-value limit",
                root ? root->span : SourceSpan { });
            return false;
        }
        bool valid = true;
        for (const auto* node : nodes) {
            if (annotation.target_kind == SdfTimingTargetKind::TimingCheck) {
                const auto selected = select_sdf_timing_check_value(
                    *node->exact_value, *ir.timescale(), policy, node->span);
                if (!selected.ok()) {
                    diagnostics.insert(diagnostics.end(),
                        selected.diagnostics.begin(),
                        selected.diagnostics.end());
                    valid = false;
                    continue;
                }
                annotation.after_check_ticks.push_back(selected.selected->ticks);
                continue;
            }
            if (annotation.construct_kind
                == SdfConstructKind::PathPulsePercent) {
                const auto selected = select_sdf_percentage(
                    *node->exact_value, policy, node->span);
                if (!selected.ok()) {
                    diagnostics.insert(diagnostics.end(),
                        selected.diagnostics.begin(),
                        selected.diagnostics.end());
                    valid = false;
                    continue;
                }
                annotation.after_percentages.push_back(*selected.selected);
                continue;
            }
            const auto selected
                = select_sdf_delay(*node->exact_value, *ir.timescale(), policy,
                    node->span);
            if (!selected.ok()) {
                diagnostics.insert(diagnostics.end(), selected.diagnostics.begin(),
                    selected.diagnostics.end());
                valid = false;
                continue;
            }
            if (descends_from_kind(ir, *node, SdfConstructKind::Retain)) {
                annotation.retain_ticks.push_back(selected.selected->ticks);
            } else {
                annotation.after_ticks.push_back(selected.selected->ticks);
            }
        }
        return valid;
    }

    [[nodiscard]] bool resolve_target(const SdfResolvedNodeEndpoints& mapping,
        const elaboration::ElaboratedDesign& elaborated,
        SdfPlannedAnnotation& annotation, std::vector<Diagnostic>& diagnostics,
        const SourceSpan& span)
    {
        switch (annotation.target_kind) {
        case SdfTimingTargetKind::SpecifyPath: {
            const auto* path = mapping.specify_path
                ? find_specify_path(elaborated, *mapping.specify_path)
                : nullptr;
            if (path == nullptr || path->identity.empty()
                || path->instance != mapping.target_instance_path
                || path->delays.empty()) {
                diagnose(diagnostics, "FSIM-SDF-PLAN-002",
                    "SDF path target is missing, stale, or has no source delays",
                    span);
                return false;
            }
            annotation.target_identity = path->identity;
            annotation.before_ticks.assign(path->delays.begin(), path->delays.end());
            return true;
        }
        case SdfTimingTargetKind::TimingCheck: {
            const auto* check = mapping.timing_check
                ? find_timing_check(elaborated, *mapping.timing_check)
                : nullptr;
            if (check == nullptr || check->identity.empty()
                || !timing_check_limits_valid(*check)) {
                diagnose(diagnostics, "FSIM-SDF-PLAN-002",
                    "SDF timing-check target is missing, stale, or has invalid limits",
                    span);
                return false;
            }
            annotation.target_identity = check->identity;
            annotation.before_check_ticks = check->limits;
            return true;
        }
        case SdfTimingTargetKind::Pulse: {
            const auto* path = mapping.specify_path
                ? find_specify_path(elaborated, *mapping.specify_path)
                : nullptr;
            if (path != nullptr && !path->identity.empty()
                && path->instance == mapping.target_instance_path) {
                annotation.target_identity = path->identity;
                if (path->pulse_reject_limit && path->pulse_error_limit) {
                    annotation.before_ticks = { *path->pulse_reject_limit,
                        *path->pulse_error_limit };
                }
                return true;
            }
            if (mapping.construct_kind == SdfConstructKind::PathPulsePercent
                && mapping.endpoints.empty()) {
                annotation.target_identity = "sdf:globalpathpulse:"
                    + mapping.target_instance_path;
                return true;
            }
            diagnose(diagnostics, "FSIM-SDF-PLAN-002",
                "SDF pulse target is missing, ambiguous, or not linked to an elaborated path",
                span);
            return false;
        }
        default:
            if (mapping.endpoints.empty()) {
                diagnose(diagnostics, "FSIM-SDF-PLAN-002",
                    "SDF endpoint timing target has no stable elaborated endpoint",
                    span);
                return false;
            }
            annotation.target_identity = endpoint_target_identity(mapping);
            return !annotation.target_identity.empty();
        }
    }

    [[nodiscard]] std::string annotation_identity(
        const SdfPlannedAnnotation& annotation)
    {
        std::string result = "sdf-planned-annotation-v4";
        append_field(result, std::to_string(annotation.node_id));
        append_field(result, std::to_string(annotation.cell_id));
        append_field(result, target_kind_identity(annotation.target_kind));
        append_field(result,
            std::to_string(static_cast<unsigned>(annotation.delay_mode)));
        append_field(result, annotation.target_identity);
        append_field(result, annotation.condition_identity);
        for (const auto& edge : annotation.edge_identities)
            append_field(result, edge);
        result.push_back('|');
        for (const auto& endpoint : annotation.endpoints) {
            append_field(result,
                std::to_string(static_cast<unsigned>(endpoint.role)));
            append_field(result,
                std::to_string(static_cast<unsigned>(endpoint.object_kind)));
            append_field(result, endpoint.instance_path);
            append_field(result, endpoint.object_path);
            append_field(result, std::to_string(endpoint.signal));
            append_field(result, std::to_string(endpoint.object_width));
            append_field(result,
                std::to_string(static_cast<unsigned>(endpoint.direction)));
        }
        result.push_back('|');
        append_field(result, annotation.source_identity);
        for (const auto value : annotation.before_ticks)
            append_field(result, std::to_string(value));
        result.push_back('|');
        for (const auto value : annotation.after_ticks)
            append_field(result, std::to_string(value));
        result.push_back('|');
        for (const auto value : annotation.before_check_ticks)
            append_field(result, std::to_string(value));
        result.push_back('|');
        for (const auto value : annotation.after_check_ticks)
            append_field(result, std::to_string(value));
        result.push_back('|');
        for (const auto value : annotation.retain_ticks)
            append_field(result, std::to_string(value));
        result.push_back('|');
        for (const auto& percentage : annotation.after_percentages)
            append_field(result, percentage.canonical_identity);
        return result;
    }

    [[nodiscard]] std::string plan_identity(
        const SdfAnnotationSummary& summary, const SdfValuePolicy& policy,
        const std::vector<SdfPlannedAnnotation>& annotations)
    {
        std::string result = "sdf-annotation-plan-v3";
        append_field(result, summary.semantic_identity());
        append_field(result, std::to_string(static_cast<unsigned>(policy.selection)));
        append_field(result, std::to_string(policy.design_time_unit_femtoseconds));
        append_field(
            result, std::to_string(policy.simulation_precision_femtoseconds));
        for (const auto& annotation : annotations)
            append_field(result, annotation.canonical_identity);
        return result;
    }
} // namespace

const std::shared_ptr<const SdfAnnotationSummary>& SdfAnnotationPlan::summary()
    const noexcept
{
    return summary_;
}

const SdfValuePolicy& SdfAnnotationPlan::value_policy() const noexcept
{
    return value_policy_;
}

std::span<const SdfPlannedAnnotation> SdfAnnotationPlan::annotations() const
    noexcept
{
    return annotations_;
}

std::string_view SdfAnnotationPlan::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfAnnotationPlan::SdfAnnotationPlan(
    std::shared_ptr<const SdfAnnotationSummary> summary,
    SdfValuePolicy value_policy,
    std::vector<SdfPlannedAnnotation> annotations,
    std::string semantic_identity)
    : summary_(std::move(summary))
    , value_policy_(value_policy)
    , annotations_(std::move(annotations))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfTargetPlanResult::ok() const noexcept
{
    return plan != nullptr && diagnostics.empty();
}

SdfTargetPlanResult build_sdf_annotation_plan(
    std::shared_ptr<const SdfAnnotationSummary> summary,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfValuePolicy& value_policy, const SdfTargetPlanLimits limits)
{
    SdfTargetPlanResult result;
    if (!summary || !summary->endpoint_resolution()
        || !summary->endpoint_resolution()->cells()
        || !summary->endpoint_resolution()->cells()->scope()
        || !summary->endpoint_resolution()->cells()->scope()->normalized_ir()
        || summary->semantic_identity().empty()
        || limits.max_annotations == 0U
        || limits.max_values_per_annotation == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-PLAN-001",
            "SDF target planning requires a complete validated mapping and nonzero limits",
            { });
        return result;
    }
    const auto& resolution = *summary->endpoint_resolution();
    const auto& cells = *resolution.cells();
    const auto& ir = *cells.scope()->normalized_ir();
    if (resolution.nodes().size() != summary->annotation_count()
        || resolution.nodes().size() > limits.max_annotations) {
        diagnose(result.diagnostics,
            resolution.nodes().size() > limits.max_annotations
                ? "FSIM-SDF-PLAN-004"
                : "FSIM-SDF-PLAN-001",
            "SDF target plan count disagrees with the validated summary or exceeds its limit",
            { });
        return result;
    }

    std::set<std::string> target_owners;
    std::vector<SdfPlannedAnnotation> annotations;
    annotations.reserve(resolution.nodes().size());
    std::size_t identity_bytes { };
    for (const auto& mapping : resolution.nodes()) {
        const auto* node = ir.find_node(mapping.node_id);
        const auto* ir_cell = ir.find_cell(mapping.cell_id);
        const auto* instance
            = find_instance(cells, mapping.cell_id, mapping.target_instance_path);
        const auto span = node ? node->span : SourceSpan { };
        if (node == nullptr || ir_cell == nullptr || instance == nullptr
            || node->cell_id != mapping.cell_id
            || node->kind != mapping.construct_kind
            || instance->cell_type != ir_cell->cell_type
            || !verilog_language(instance->language)) {
            diagnose(result.diagnostics, "FSIM-SDF-PLAN-001",
                "SDF target mapping is stale, cell-type mismatched, or outside Verilog/SystemVerilog",
                span);
            continue;
        }

        SdfPlannedAnnotation annotation;
        annotation.node_id = mapping.node_id;
        annotation.cell_id = mapping.cell_id;
        annotation.construct_kind = mapping.construct_kind;
        annotation.target_kind = target_kind(mapping.construct_kind);
        annotation.delay_mode = annotation_mode(ir, mapping.node_id);
        annotation.target_instance_path = mapping.target_instance_path;
        annotation.condition_identity = mapping.condition_identity;
        annotation.endpoints = mapping.endpoints;
        annotation.source = node->span;
        annotation.source_identity = node->source_identity;
        for (const auto& endpoint : mapping.endpoints) {
            annotation.endpoint_signals.push_back(endpoint.signal);
            if (!endpoint.edge_identity.empty())
                annotation.edge_identities.push_back(endpoint.edge_identity);
        }
        std::ranges::sort(annotation.endpoint_signals);
        annotation.endpoint_signals.erase(
            std::ranges::unique(annotation.endpoint_signals).begin(),
            annotation.endpoint_signals.end());
        std::ranges::sort(annotation.edge_identities);
        annotation.edge_identities.erase(
            std::ranges::unique(annotation.edge_identities).begin(),
            annotation.edge_identities.end());

        bool valid = resolve_target(
            mapping, elaborated, annotation, result.diagnostics, span);
        valid = planned_values(ir, mapping, value_policy, limits, annotation,
                    result.diagnostics)
            && valid;
        if (!valid)
            continue;
        const auto before_count = annotation.target_kind
                == SdfTimingTargetKind::TimingCheck
            ? annotation.before_check_ticks.size()
            : annotation.before_ticks.size();
        const auto after_count = annotation.target_kind
                == SdfTimingTargetKind::TimingCheck
            ? annotation.after_check_ticks.size()
            : annotation.construct_kind == SdfConstructKind::PathPulsePercent
            ? annotation.after_percentages.size()
            : annotation.after_ticks.size();
        if (after_count == 0U
            || after_count > limits.max_values_per_annotation
            || before_count > limits.max_values_per_annotation) {
            diagnose(result.diagnostics, "FSIM-SDF-PLAN-002",
                "SDF target delay/check arity is empty or unsupported", span);
            continue;
        }
        if (annotation.retain_ticks.size()
            > limits.max_values_per_annotation) {
            diagnose(result.diagnostics, "FSIM-SDF-PLAN-004",
                "SDF RETAIN list exceeds the configured target-value limit",
                span);
            continue;
        }

        auto owner = target_kind_identity(annotation.target_kind);
        append_field(owner, annotation.target_identity);
        append_field(owner, std::to_string(static_cast<unsigned>(annotation.construct_kind)));
        if (!target_owners.insert(owner).second) {
            diagnose(result.diagnostics, "FSIM-SDF-PLAN-003",
                "SDF annotation plan contains duplicate ownership of one timing target",
                span);
            continue;
        }
        annotation.canonical_identity = annotation_identity(annotation);
        if (annotation.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes
                > limits.max_identity_bytes - annotation.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-PLAN-004",
                "SDF annotation plan exceeds its configured identity-byte limit",
                span);
            continue;
        }
        identity_bytes += annotation.canonical_identity.size();
        annotations.push_back(std::move(annotation));
    }
    if (!result.diagnostics.empty()
        || annotations.size() != resolution.nodes().size()) {
        return result;
    }

    const auto semantic_identity
        = plan_identity(*summary, value_policy, annotations);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-PLAN-004",
            "SDF annotation plan semantic identity exceeds its configured limit",
            { });
        return result;
    }
    result.plan = std::make_shared<const SdfAnnotationPlan>(std::move(summary),
        value_policy, std::move(annotations), semantic_identity);
    return result;
}

} // namespace fsim::app
