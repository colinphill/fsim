// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_vital_target_plan.hpp"

#include <algorithm>
#include <map>
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

    [[nodiscard]] const elaboration::SpecializationInfo* find_specialization(
        const elaboration::ElaboratedDesignState& state,
        const std::string_view instance_path)
    {
        const elaboration::SpecializationInfo* result = nullptr;
        for (const auto& specialization : state.specializations) {
            if (specialization.instance != instance_path
                || specialization.language != frontend::Language::Vhdl2008) {
                continue;
            }
            if (result != nullptr)
                return nullptr;
            result = &specialization;
        }
        return result;
    }

    [[nodiscard]] const elaboration::SignalInfo* find_signal(
        const elaboration::ElaboratedDesignState& state,
        const runtime::simir::SignalId signal) noexcept
    {
        if (signal >= state.signal_info.size()
            || state.signal_info[signal].id != signal) {
            return nullptr;
        }
        return &state.signal_info[signal];
    }

    [[nodiscard]] std::string port_identity(
        const SdfVitalPortBinding& port)
    {
        auto result = std::string { "sdf-vital-port-v1" };
        append_field(result, port.object_path);
        append_field(result, std::to_string(port.signal));
        append_field(result, std::to_string(port.width));
        append_field(result,
            std::to_string(static_cast<unsigned>(port.role)));
        append_field(result,
            std::to_string(static_cast<unsigned>(port.object_kind)));
        append_field(result,
            std::to_string(static_cast<unsigned>(port.direction)));
        append_field(result,
            std::to_string(static_cast<unsigned>(port.value_domain)));
        append_field(result,
            std::to_string(static_cast<unsigned>(port.resolution)));
        append_field(result, port.type_name);
        append_field(result, port.edge_identity);
        if (port.select) {
            append_field(result, std::to_string(port.select->left));
            append_field(result, std::to_string(port.select->right));
            append_field(result, std::to_string(port.select->width));
        } else {
            append_field(result, "whole");
        }
        return result;
    }

    [[nodiscard]] bool bind_ports(const SdfResolvedNodeEndpoints& mapping,
        const elaboration::ElaboratedDesignState& state,
        const SdfVitalTargetPlanLimits& limits,
        SdfVitalPlannedTarget& target, std::vector<Diagnostic>& diagnostics,
        const SourceSpan& span)
    {
        if (mapping.endpoints.empty()
            || mapping.endpoints.size() > limits.max_ports_per_target) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PLAN-006",
                "SDF VITAL target exceeds its configured port limit or has no ports",
                span);
            return false;
        }
        bool valid = true;
        std::set<std::string> paths;
        target.ports.reserve(mapping.endpoints.size());
        for (const auto& endpoint : mapping.endpoints) {
            const auto* signal = find_signal(state, endpoint.signal);
            const auto selected_width
                = endpoint.select ? endpoint.select->width : endpoint.object_width;
            if (endpoint.language != SdfScopeRootLanguage::Vhdl
                || endpoint.instance_path != mapping.target_instance_path
                || endpoint.conversion || endpoint.conversion_peer
                || signal == nullptr || signal->name != endpoint.object_path
                || signal->width != endpoint.object_width
                || signal->is_port
                    != (endpoint.object_kind == SdfEndpointObjectKind::HdlPort)
                || signal->direction != endpoint.direction
                || endpoint.direction == frontend::PortDirection::Unknown
                || selected_width == 0U
                || (endpoint.select
                    && (endpoint.select->left < 0 || endpoint.select->right < 0
                        || static_cast<std::uint64_t>(std::max(
                               endpoint.select->left, endpoint.select->right))
                            >= endpoint.object_width
                        || endpoint.select->width > endpoint.object_width))
                || !paths.insert(endpoint.object_path).second) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-PLAN-003",
                    "SDF VITAL endpoint disagrees with its elaborated VHDL port type or shape",
                    span);
                valid = false;
                continue;
            }
            SdfVitalPortBinding port;
            port.role = endpoint.role;
            port.object_kind = endpoint.object_kind;
            port.object_path = endpoint.object_path;
            port.signal = endpoint.signal;
            port.width = endpoint.object_width;
            port.select = endpoint.select;
            port.direction = endpoint.direction;
            port.value_domain = signal->source_domain;
            port.resolution = signal->resolution;
            port.type_name = signal->type_name;
            port.edge_identity = endpoint.edge_identity;
            port.canonical_identity = port_identity(port);
            target.ports.push_back(std::move(port));
        }
        return valid;
    }

    [[nodiscard]] bool bind_generics(
        const elaboration::SpecializationInfo& specialization,
        const SdfVitalTargetPlanLimits& limits, SdfVitalPlannedTarget& target,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        if (specialization.parameter_values.size()
            > limits.max_generics_per_target) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PLAN-006",
                "SDF VITAL target exceeds its configured generic limit", span);
            return false;
        }
        std::map<std::string, std::string> identities;
        for (const auto& [name, identity] :
            specialization.parameter_identity_values) {
            if (name.empty() || identity.empty()
                || !identities.emplace(name, identity).second) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-PLAN-004",
                    "SDF VITAL generic semantic identities are missing or duplicated",
                    span);
                return false;
            }
        }
        std::set<std::string> names;
        for (const auto& [name, value] : specialization.parameter_values) {
            const auto identity = identities.empty()
                ? std::string_view { value }
                : std::string_view { identities[name] };
            if (name.empty() || value.empty() || identity.empty()
                || !names.insert(name).second) {
                diagnose(diagnostics, "FSIM-SDF-VITAL-PLAN-004",
                    "SDF VITAL generic values lack unique semantic identities",
                    span);
                return false;
            }
            target.generics.push_back(
                { name, value, std::string { identity } });
        }
        if (!identities.empty()
            && (identities.size() != target.generics.size()
                || !std::ranges::all_of(identities,
                    [&](const auto& entry) { return names.contains(entry.first); }))) {
            diagnose(diagnostics, "FSIM-SDF-VITAL-PLAN-004",
                "SDF VITAL generic value and semantic-identity sets disagree",
                span);
            return false;
        }
        std::ranges::sort(target.generics, { }, &SdfVitalGenericBinding::name);
        return true;
    }

    [[nodiscard]] std::string target_identity(
        const SdfVitalPlannedTarget& target)
    {
        auto result = std::string { "sdf-vital-target-v1" };
        append_field(result, std::to_string(target.node_id));
        append_field(result, std::to_string(target.cell_id));
        append_field(result,
            std::to_string(static_cast<unsigned>(target.construct_kind)));
        append_field(result, target.instance_path);
        append_field(result, target.unit_identity);
        append_field(result, target.specialization_unit);
        append_field(result, target.library);
        append_field(result, target.condition_identity);
        for (const auto& port : target.ports)
            append_field(result, port.canonical_identity);
        for (const auto& generic : target.generics) {
            append_field(result, generic.name);
            append_field(result, generic.value);
            append_field(result, generic.semantic_identity);
        }
        append_field(result, target.source_identity);
        return result;
    }

    [[nodiscard]] std::string plan_identity(const SdfAnnotationSummary& summary,
        const std::span<const SdfVitalPlannedTarget> targets)
    {
        auto result = std::string { "sdf-vital-target-plan-v1" };
        append_field(result, summary.semantic_identity());
        for (const auto& target : targets)
            append_field(result, target.canonical_identity);
        return result;
    }
} // namespace

SdfVitalTargetPlan::SdfVitalTargetPlan(
    std::shared_ptr<const SdfAnnotationSummary> summary,
    std::vector<SdfVitalPlannedTarget> targets, std::string semantic_identity)
    : summary_(std::move(summary))
    , targets_(std::move(targets))
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfAnnotationSummary>& SdfVitalTargetPlan::summary()
    const noexcept
{
    return summary_;
}

std::span<const SdfVitalPlannedTarget> SdfVitalTargetPlan::targets() const
    noexcept
{
    return targets_;
}

std::string_view SdfVitalTargetPlan::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfVitalTargetPlanResult::ok() const noexcept
{
    return plan != nullptr && diagnostics.empty();
}

SdfVitalTargetPlanResult build_sdf_vital_target_plan(
    std::shared_ptr<const SdfAnnotationSummary> summary,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfVitalTargetPlanLimits limits)
{
    SdfVitalTargetPlanResult result;
    if (!summary || !summary->endpoint_resolution()
        || !summary->endpoint_resolution()->cells()
        || !summary->endpoint_resolution()->cells()->scope()
        || !summary->endpoint_resolution()->cells()->scope()->normalized_ir()
        || summary->semantic_identity().empty() || limits.max_targets == 0U
        || limits.max_ports_per_target == 0U
        || limits.max_generics_per_target == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PLAN-001",
            "SDF VITAL target planning requires a complete validated mapping and nonzero limits",
            { });
        return result;
    }
    const auto& resolution = *summary->endpoint_resolution();
    const auto& cells = *resolution.cells();
    const auto& ir = *cells.scope()->normalized_ir();
    if (resolution.nodes().size() != summary->annotation_count()
        || resolution.nodes().size() > limits.max_targets) {
        diagnose(result.diagnostics,
            resolution.nodes().size() > limits.max_targets
                ? "FSIM-SDF-VITAL-PLAN-006"
                : "FSIM-SDF-VITAL-PLAN-001",
            "SDF VITAL target count disagrees with its summary or configured limit",
            { });
        return result;
    }

    const auto state = elaborated.state();
    std::set<std::string> owners;
    std::vector<SdfVitalPlannedTarget> targets;
    targets.reserve(resolution.nodes().size());
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
            || instance->language != SdfScopeRootLanguage::Vhdl) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PLAN-001",
                "SDF target mapping is stale, cell-type mismatched, or outside VHDL",
                span);
            continue;
        }
        const auto* specialization
            = find_specialization(state, mapping.target_instance_path);
        if (specialization == nullptr || specialization->unit.empty()
            || specialization->library.empty()
            || instance->unit_identity.empty()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PLAN-002",
                "SDF VITAL cell has no unique elaborated entity architecture or configuration binding",
                span);
            continue;
        }

        SdfVitalPlannedTarget target;
        target.node_id = mapping.node_id;
        target.cell_id = mapping.cell_id;
        target.construct_kind = mapping.construct_kind;
        target.instance_path = mapping.target_instance_path;
        target.unit_identity = instance->unit_identity;
        target.specialization_unit = specialization->unit;
        target.library = specialization->library;
        target.condition_identity = mapping.condition_identity;
        target.language = specialization->language;
        target.source = node->span;
        target.source_identity = node->source_identity;
        bool valid = bind_ports(mapping, state, limits, target,
            result.diagnostics, span);
        valid = bind_generics(*specialization, limits, target,
                    result.diagnostics, span)
            && valid;
        if (!valid)
            continue;
        auto owner = target.instance_path;
        append_field(owner, std::to_string(target.node_id));
        append_field(owner,
            std::to_string(static_cast<unsigned>(target.construct_kind)));
        if (!owners.insert(owner).second) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PLAN-005",
                "SDF VITAL plan contains duplicate timing-target ownership",
                span);
            continue;
        }
        target.canonical_identity = target_identity(target);
        if (target.canonical_identity.size() > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes
                    - target.canonical_identity.size()) {
            diagnose(result.diagnostics, "FSIM-SDF-VITAL-PLAN-006",
                "SDF VITAL plan exceeds its configured identity-byte limit",
                span);
            continue;
        }
        identity_bytes += target.canonical_identity.size();
        targets.push_back(std::move(target));
    }
    if (!result.diagnostics.empty()
        || targets.size() != resolution.nodes().size()) {
        return result;
    }
    auto semantic_identity = plan_identity(*summary, targets);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-VITAL-PLAN-006",
            "SDF VITAL plan semantic identity exceeds its configured limit",
            { });
        return result;
    }
    result.plan = std::make_shared<const SdfVitalTargetPlan>(
        std::move(summary), std::move(targets), std::move(semantic_identity));
    return result;
}

} // namespace fsim::app
