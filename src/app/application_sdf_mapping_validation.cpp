// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_mapping_validation.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;
    using frontend::PortDirection;
    using frontend::SdfConstructKind;
    using frontend::SdfInstanceSelectorKind;
    using frontend::SdfIrNode;
    using frontend::SourceSpan;

    struct TargetAccumulator {
        SdfAnnotationTargetSummary summary;
    };

    struct ValidationCounts {
        std::size_t annotations { };
        std::size_t endpoints { };
        std::size_t delays { };
        std::size_t timing_checks { };
        std::size_t timing_environment { };
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

    [[nodiscard]] bool annotation_kind(const SdfConstructKind kind) noexcept
    {
        static constexpr auto kinds = std::to_array<SdfConstructKind>({
            SdfConstructKind::Iopath,
            SdfConstructKind::Interconnect,
            SdfConstructKind::NetDelay,
            SdfConstructKind::Port,
            SdfConstructKind::Device,
            SdfConstructKind::PathPulse,
            SdfConstructKind::PathPulsePercent,
            SdfConstructKind::Mipd,
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
            SdfConstructKind::PathConstraint,
            SdfConstructKind::PeriodConstraint,
            SdfConstructKind::SkewConstraint,
            SdfConstructKind::Arrival,
            SdfConstructKind::Departure,
            SdfConstructKind::Slack,
            SdfConstructKind::Waveform,
        });
        return std::ranges::find(kinds, kind) != kinds.end();
    }

    [[nodiscard]] bool delay_kind(const SdfConstructKind kind) noexcept
    {
        static constexpr auto kinds = std::to_array<SdfConstructKind>({
            SdfConstructKind::Iopath,
            SdfConstructKind::Interconnect,
            SdfConstructKind::NetDelay,
            SdfConstructKind::Port,
            SdfConstructKind::Device,
            SdfConstructKind::PathPulse,
            SdfConstructKind::PathPulsePercent,
            SdfConstructKind::Mipd,
        });
        return std::ranges::find(kinds, kind) != kinds.end();
    }

    [[nodiscard]] bool timing_check_kind(
        const SdfConstructKind kind) noexcept
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

    [[nodiscard]] bool valid_object_kind(
        const SdfEndpointObjectKind kind) noexcept
    {
        static constexpr auto kinds = std::to_array<SdfEndpointObjectKind>({
            SdfEndpointObjectKind::HdlPort,
            SdfEndpointObjectKind::HdlNet,
            SdfEndpointObjectKind::SystemCPort,
            SdfEndpointObjectKind::SystemCSignal,
        });
        return std::ranges::find(kinds, kind) != kinds.end();
    }

    [[nodiscard]] std::string mapping_key(
        const std::uint64_t cell_id, const std::uint64_t node_id,
        const std::string_view target)
    {
        std::string result;
        append_field(result, std::to_string(cell_id));
        append_field(result, std::to_string(node_id));
        append_field(result, target);
        return result;
    }

    [[nodiscard]] SourceSpan node_span(
        const frontend::SdfIr& ir, const std::uint64_t node_id)
    {
        const auto* node = ir.find_node(node_id);
        return node ? node->span : SourceSpan { };
    }

    [[nodiscard]] const elaboration::SignalInfo* find_signal(
        const elaboration::ElaboratedDesign& elaborated,
        const runtime::simir::SignalId signal)
    {
        const auto found = std::ranges::find(
            elaborated.signals(), signal, &elaboration::SignalInfo::id);
        return found == elaborated.signals().end() ? nullptr : &*found;
    }

    [[nodiscard]] bool is_systemc_object(
        const SdfResolvedEndpoint& endpoint,
        const elaboration::ElaboratedDesign& elaborated, const bool port)
    {
        return std::ranges::any_of(elaborated.systemc_objects(),
            [&](const elaboration::SystemCNamedObjectInfo& object) {
                const bool object_is_port
                    = object.kind == elaboration::SystemCNamedObjectKind::port;
                return object.signal && *object.signal == endpoint.signal
                    && object.name == endpoint.object_path
                    && object_is_port == port;
            });
    }

    [[nodiscard]] bool object_kind_matches(
        const SdfResolvedEndpoint& endpoint,
        const elaboration::SignalInfo& signal,
        const elaboration::ElaboratedDesign& elaborated)
    {
        if (!valid_object_kind(endpoint.object_kind))
            return false;
        if (endpoint.object_kind == SdfEndpointObjectKind::HdlPort) {
            return signal.is_port
                && !is_systemc_object(endpoint, elaborated, true);
        }
        if (endpoint.object_kind == SdfEndpointObjectKind::HdlNet) {
            return !signal.is_port
                && !is_systemc_object(endpoint, elaborated, false);
        }
        return is_systemc_object(endpoint, elaborated,
            endpoint.object_kind == SdfEndpointObjectKind::SystemCPort);
    }

    [[nodiscard]] bool selector_matches_range(
        const SdfEndpointSelect& select,
        const elaboration::SignalInfo& signal)
    {
        const auto distance = select.left >= select.right
            ? static_cast<std::uint64_t>(select.left)
                - static_cast<std::uint64_t>(select.right)
            : static_cast<std::uint64_t>(select.right)
                - static_cast<std::uint64_t>(select.left);
        if (distance >= std::numeric_limits<std::size_t>::max()
            || select.width != static_cast<std::size_t>(distance + 1U)
            || select.width > signal.width) {
            return false;
        }
        if (signal.packed_range) {
            const auto low
                = std::min(signal.packed_range->left, signal.packed_range->right);
            const auto high
                = std::max(signal.packed_range->left, signal.packed_range->right);
            return select.left >= low && select.left <= high
                && select.right >= low && select.right <= high;
        }
        if (signal.vhdl_array && !signal.vhdl_array->dimensions.empty()
            && signal.vhdl_array->dimensions.front().range) {
            const auto& range = *signal.vhdl_array->dimensions.front().range;
            const auto low = std::min(range.left, range.right);
            const auto high = std::max(range.left, range.right);
            return select.left >= low && select.left <= high
                && select.right >= low && select.right <= high;
        }
        return select.left >= 0 && select.right >= 0
            && static_cast<std::uint64_t>(select.left) < signal.width
            && static_cast<std::uint64_t>(select.right) < signal.width;
    }

    [[nodiscard]] bool role_direction_matches(
        const SdfResolvedEndpoint& endpoint)
    {
        if (endpoint.role == SdfEndpointRole::Input)
            return endpoint.direction != PortDirection::Output;
        if (endpoint.role == SdfEndpointRole::Output
            || endpoint.role == SdfEndpointRole::Device) {
            return endpoint.direction != PortDirection::Input;
        }
        return true;
    }

    [[nodiscard]] bool conversion_matches(
        const SdfResolvedEndpoint& endpoint,
        const elaboration::ElaboratedDesign& elaborated)
    {
        if (endpoint.conversion.has_value()
            != endpoint.conversion_peer.has_value()) {
            return false;
        }
        if (!endpoint.conversion)
            return true;
        return std::ranges::any_of(elaborated.boundary_conversions(),
            [&](const elaboration::BoundaryConversionInfo& conversion) {
                const bool same_pair
                    = (conversion.formal_signal == endpoint.signal
                          && conversion.actual_signal
                              == *endpoint.conversion_peer)
                    || (conversion.actual_signal == endpoint.signal
                        && conversion.formal_signal
                            == *endpoint.conversion_peer);
                return same_pair && conversion.kind == *endpoint.conversion
                    && conversion.path == endpoint.object_path;
            });
    }

    [[nodiscard]] std::string endpoint_identity(
        const SdfResolvedEndpoint& endpoint)
    {
        std::string result;
        append_field(result,
            std::to_string(static_cast<unsigned>(endpoint.role)));
        append_field(result,
            std::to_string(static_cast<unsigned>(endpoint.object_kind)));
        append_field(result,
            std::to_string(static_cast<std::uint64_t>(endpoint.signal)));
        append_field(result, endpoint.object_path);
        append_field(result,
            endpoint.select
                ? std::to_string(endpoint.select->left) + ":"
                    + std::to_string(endpoint.select->right)
                : std::string { });
        append_field(result, endpoint.edge_identity);
        append_field(result, endpoint.condition_identity);
        return result;
    }

    [[nodiscard]] std::string delay_mode_identity(
        const SdfIrNode& node, const frontend::SdfIr& ir)
    {
        auto parent_id = node.parent_id;
        while (parent_id != 0U) {
            const auto* parent = ir.find_node(parent_id);
            if (!parent)
                break;
            if (parent->kind == SdfConstructKind::Absolute
                || parent->kind == SdfConstructKind::Increment) {
                return std::to_string(static_cast<unsigned>(parent->kind));
            }
            parent_id = parent->parent_id;
        }
        return { };
    }

    [[nodiscard]] std::string application_identity(
        const SdfResolvedNodeEndpoints& mapping, const SdfIrNode& node,
        const frontend::SdfIr& ir)
    {
        std::string result;
        append_field(result,
            std::to_string(static_cast<unsigned>(mapping.construct_kind)));
        append_field(result, mapping.target_instance_path);
        append_field(result, delay_mode_identity(node, ir));
        append_field(result, mapping.condition_identity);
        for (const auto& endpoint : mapping.endpoints)
            append_field(result, endpoint_identity(endpoint));
        return result;
    }

    [[nodiscard]] bool validate_endpoint(
        const SdfResolvedNodeEndpoints& mapping,
        const SdfResolvedEndpoint& endpoint,
        const SdfResolvedInstance& target,
        const elaboration::ElaboratedDesign& elaborated,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        const auto* signal = find_signal(elaborated, endpoint.signal);
        const bool systemc_kind
            = endpoint.object_kind == SdfEndpointObjectKind::SystemCPort
            || endpoint.object_kind == SdfEndpointObjectKind::SystemCSignal;
        if (!signal || endpoint.instance_path != mapping.target_instance_path
            || endpoint.instance_path != target.instance_path
            || (!systemc_kind && endpoint.object_path != signal->name)
            || endpoint.language != target.language) {
            diagnose(diagnostics, "FSIM-SDF-MAP-001",
                "SDF endpoint mapping is stale relative to the elaborated design",
                span);
            return false;
        }
        if (!object_kind_matches(endpoint, *signal, elaborated)) {
            diagnose(diagnostics, "FSIM-SDF-MAP-003",
                "SDF endpoint mapping uses an unsupported or mismatched object kind",
                span);
            return false;
        }
        if (endpoint.object_width == 0U
            || endpoint.object_width != signal->width
            || endpoint.direction != signal->direction
            || (endpoint.select
                && !selector_matches_range(*endpoint.select, *signal))) {
            diagnose(diagnostics, "FSIM-SDF-MAP-003",
                "SDF endpoint mapping has a width, direction, or selector conflict",
                span);
            return false;
        }
        if (!role_direction_matches(endpoint)) {
            diagnose(diagnostics, "FSIM-SDF-MAP-003",
                "SDF endpoint role conflicts with the elaborated port direction",
                span);
            return false;
        }
        if (!conversion_matches(endpoint, elaborated)) {
            diagnose(diagnostics, "FSIM-SDF-MAP-003",
                "SDF endpoint conversion identity conflicts with elaboration",
                span);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validate_mapping_endpoints(
        const SdfResolvedNodeEndpoints& mapping,
        const SdfResolvedInstance& target,
        const elaboration::ElaboratedDesign& elaborated,
        std::vector<Diagnostic>& diagnostics, const SourceSpan& span)
    {
        if (mapping.endpoints.empty()) {
            if (mapping.construct_kind == SdfConstructKind::PathPulsePercent
                && !mapping.target_instance_path.empty()) {
                return true;
            }
            diagnose(diagnostics, "FSIM-SDF-MAP-004",
                "SDF annotation mapping has no consumed endpoint", span);
            return false;
        }
        std::unordered_set<std::string> endpoint_identities;
        for (const auto& endpoint : mapping.endpoints) {
            if (!validate_endpoint(
                    mapping, endpoint, target, elaborated, diagnostics, span)) {
                return false;
            }
            if (!endpoint_identities.emplace(endpoint_identity(endpoint)).second) {
                diagnose(diagnostics, "FSIM-SDF-MAP-002",
                    "SDF annotation mapping contains a duplicate endpoint", span);
                return false;
            }
        }
        return true;
    }

    void update_counts(
        ValidationCounts& counts, const SdfResolvedNodeEndpoints& mapping)
    {
        ++counts.annotations;
        counts.endpoints += mapping.endpoints.size();
        if (delay_kind(mapping.construct_kind)) {
            ++counts.delays;
        } else if (timing_check_kind(mapping.construct_kind)) {
            ++counts.timing_checks;
        } else {
            ++counts.timing_environment;
        }
    }

    [[nodiscard]] bool validate_overlap(const SdfCellResolution& cells,
        const frontend::SdfIr& ir, std::vector<Diagnostic>& diagnostics)
    {
        struct Owner {
            std::uint64_t cell_id { };
            SdfInstanceSelectorKind selector { SdfInstanceSelectorKind::Empty };
        };
        std::unordered_map<std::string, Owner> owners;
        for (const auto& cell : cells.cells()) {
            for (const auto& target : cell.targets) {
                const auto [found, inserted] = owners.emplace(
                    target.instance_path, Owner { cell.cell_id, cell.selector });
                if (inserted || found->second.cell_id == cell.cell_id)
                    continue;
                const bool wildcard_overlap
                    = found->second.selector == SdfInstanceSelectorKind::Wildcard
                    || cell.selector == SdfInstanceSelectorKind::Wildcard;
                if (!wildcard_overlap)
                    continue;
                const auto* ir_cell = ir.find_cell(cell.cell_id);
                diagnose(diagnostics, "FSIM-SDF-MAP-003",
                    "SDF wildcard and explicit cell mappings overlap at target '"
                        + target.instance_path + "'",
                    ir_cell ? ir_cell->span : SourceSpan { });
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::optional<std::string> summary_identity(
        const SdfEndpointResolution& resolution,
        const std::vector<SdfAnnotationTargetSummary>& targets,
        const ValidationCounts& counts, const std::size_t limit)
    {
        std::string identity = "sdf-annotation-summary-v1";
        append_field(identity, resolution.semantic_identity());
        append_field(identity, std::to_string(counts.annotations));
        append_field(identity, std::to_string(counts.endpoints));
        append_field(identity, std::to_string(counts.delays));
        append_field(identity, std::to_string(counts.timing_checks));
        append_field(identity, std::to_string(counts.timing_environment));
        for (const auto& target : targets) {
            append_field(identity, std::to_string(target.cell_id));
            append_field(identity, target.target_instance_path);
            append_field(identity,
                std::to_string(static_cast<unsigned>(target.language)));
            append_field(identity, std::to_string(target.annotation_count));
            append_field(identity, std::to_string(target.endpoint_count));
            for (const auto signal : target.signals)
                append_field(identity,
                    std::to_string(static_cast<std::uint64_t>(signal)));
            if (identity.size() > limit)
                return std::nullopt;
        }
        return identity.size() <= limit
            ? std::optional<std::string> { std::move(identity) }
            : std::nullopt;
    }

    using NodesByCell
        = std::unordered_map<std::uint64_t, std::vector<const SdfIrNode*>>;
    using ExpectedMappings = std::map<std::string, SourceSpan>;
    using AccumulatorMap
        = std::map<std::pair<std::uint64_t, std::string>, TargetAccumulator>;
    using TargetIndex
        = std::unordered_map<std::string, const SdfResolvedInstance*>;

    [[nodiscard]] std::string owned_target_key(
        const std::uint64_t cell_id, const std::string_view target)
    {
        std::string result;
        append_field(result, std::to_string(cell_id));
        append_field(result, target);
        return result;
    }

    [[nodiscard]] bool validate_resolution_input(
        const SdfEndpointResolution& resolution,
        const SdfMappingValidationLimits& limits,
        std::vector<Diagnostic>& diagnostics)
    {
        const auto& cells = *resolution.cells();
        const auto& ir = *cells.scope()->normalized_ir();
        if (cells.cells().size() != ir.cells().size()) {
            diagnose(diagnostics, "FSIM-SDF-MAP-001",
                "SDF mapping validation encountered an incomplete cell resolution",
                ir.cells().empty() ? SourceSpan { } : ir.cells().front().span);
            return false;
        }
        std::unordered_set<std::uint64_t> cell_ids;
        for (const auto& cell : cells.cells()) {
            const auto* ir_cell = ir.find_cell(cell.cell_id);
            if (!ir_cell || ir_cell->source_identity != cell.source_identity
                || !cell_ids.emplace(cell.cell_id).second) {
                diagnose(diagnostics, "FSIM-SDF-MAP-001",
                    "SDF mapping validation encountered stale or duplicate cell ownership",
                    ir_cell ? ir_cell->span : SourceSpan { });
                return false;
            }
        }
        if (resolution.nodes().size() > limits.max_mappings) {
            diagnose(diagnostics, "FSIM-SDF-MAP-005",
                "SDF mapping validation exceeds the configured mapping limit",
                ir.nodes().empty() ? SourceSpan { } : ir.nodes().front().span);
            return false;
        }
        return validate_overlap(cells, ir, diagnostics);
    }

    [[nodiscard]] bool add_target_expectations(const SdfResolvedCell& cell,
        const SdfResolvedInstance& target,
        const std::vector<const SdfIrNode*>& nodes,
        const SdfMappingValidationLimits& limits, ExpectedMappings& expected,
        AccumulatorMap& accumulators, TargetIndex& target_index,
        std::vector<Diagnostic>& diagnostics)
    {
        const auto key = owned_target_key(cell.cell_id, target.instance_path);
        if (!target_index.emplace(key, &target).second) {
            diagnose(diagnostics, "FSIM-SDF-MAP-001",
                "SDF cell resolution contains a duplicate target owner", { });
            return false;
        }
        auto& summary
            = accumulators[{ cell.cell_id, target.instance_path }].summary;
        summary.cell_id = cell.cell_id;
        summary.target_instance_path = target.instance_path;
        summary.language = target.language;
        for (const auto* node : nodes) {
            if (expected.size() >= limits.max_mappings) {
                diagnose(diagnostics, "FSIM-SDF-MAP-005",
                    "SDF mapping validation exceeds the expected-mapping limit",
                    node->span);
                return false;
            }
            expected.emplace(
                mapping_key(cell.cell_id, node->id, target.instance_path),
                node->span);
        }
        return true;
    }

    [[nodiscard]] bool build_validation_indexes(const SdfCellResolution& cells,
        const frontend::SdfIr& ir,
        const SdfMappingValidationLimits& limits, NodesByCell& nodes_by_cell,
        ExpectedMappings& expected, AccumulatorMap& accumulators,
        TargetIndex& target_index, std::vector<Diagnostic>& diagnostics)
    {
        nodes_by_cell.reserve(ir.cells().size());
        for (const auto& node : ir.nodes()) {
            if (!annotation_kind(node.kind))
                continue;
            if (!cells.find_cell(node.cell_id)) {
                diagnose(diagnostics, "FSIM-SDF-MAP-001",
                    "SDF annotation construct has no resolved owning cell",
                    node.span);
                return false;
            }
            nodes_by_cell[node.cell_id].push_back(&node);
        }
        for (const auto& cell : cells.cells()) {
            static const std::vector<const SdfIrNode*> no_nodes;
            const auto found = nodes_by_cell.find(cell.cell_id);
            const auto& nodes
                = found == nodes_by_cell.end() ? no_nodes : found->second;
            for (const auto& target : cell.targets) {
                if (!add_target_expectations(cell, target, nodes, limits,
                        expected, accumulators, target_index, diagnostics)) {
                    return false;
                }
            }
        }
        if (accumulators.size() > limits.max_targets) {
            diagnose(diagnostics, "FSIM-SDF-MAP-005",
                "SDF mapping validation exceeds the configured target limit",
                ir.cells().empty() ? SourceSpan { } : ir.cells().front().span);
            return false;
        }
        return true;
    }

    struct MappingValidationState {
        const frontend::SdfIr& ir;
        const elaboration::ElaboratedDesign& elaborated;
        const SdfMappingValidationLimits& limits;
        ExpectedMappings& expected;
        AccumulatorMap& accumulators;
        const TargetIndex& target_index;
        std::vector<Diagnostic>& diagnostics;
        ValidationCounts counts;
        std::unordered_set<std::string> seen_mappings;
        std::unordered_map<std::string, std::string> applications;
        std::pair<std::uint64_t, std::string> prior_order;
        bool have_prior { };
    };

    [[nodiscard]] bool validate_mapping_order(MappingValidationState& state,
        const SdfResolvedNodeEndpoints& mapping, const SourceSpan& span)
    {
        const auto order
            = std::pair { mapping.node_id, mapping.target_instance_path };
        if (state.have_prior && order < state.prior_order) {
            diagnose(state.diagnostics, "FSIM-SDF-MAP-001",
                "SDF endpoint mappings are not in deterministic node/target order",
                span);
            return false;
        }
        state.prior_order = order;
        state.have_prior = true;
        const auto key = mapping_key(
            mapping.cell_id, mapping.node_id, mapping.target_instance_path);
        if (!state.seen_mappings.emplace(key).second) {
            diagnose(state.diagnostics, "FSIM-SDF-MAP-002",
                "SDF endpoint resolution contains a duplicate node/target mapping",
                span);
            return false;
        }
        if (state.expected.erase(key) != 1U) {
            diagnose(state.diagnostics, "FSIM-SDF-MAP-004",
                "SDF endpoint resolution contains an unsupported or unowned mapping",
                span);
            return false;
        }
        return true;
    }

    [[nodiscard]] bool validate_one_mapping(MappingValidationState& state,
        const SdfResolvedNodeEndpoints& mapping)
    {
        const auto span = node_span(state.ir, mapping.node_id);
        if (!validate_mapping_order(state, mapping, span))
            return false;
        const auto* node = state.ir.find_node(mapping.node_id);
        const auto target = state.target_index.find(
            owned_target_key(mapping.cell_id, mapping.target_instance_path));
        if (!node || target == state.target_index.end()
            || node->cell_id != mapping.cell_id
            || node->kind != mapping.construct_kind
            || !annotation_kind(mapping.construct_kind)) {
            diagnose(state.diagnostics, "FSIM-SDF-MAP-001",
                "SDF endpoint mapping is stale relative to normalized IR or cells",
                span);
            return false;
        }
        if (state.counts.endpoints > state.limits.max_endpoints
            || mapping.endpoints.size()
                > state.limits.max_endpoints - state.counts.endpoints) {
            diagnose(state.diagnostics, "FSIM-SDF-MAP-005",
                "SDF mapping validation exceeds the configured endpoint limit",
                span);
            return false;
        }
        if (!validate_mapping_endpoints(mapping, *target->second,
                state.elaborated, state.diagnostics, span)) {
            return false;
        }
        const auto application = application_identity(mapping, *node, state.ir);
        const auto [prior, inserted] = state.applications.emplace(
            application, node->canonical_identity);
        if (!inserted) {
            const bool duplicate = prior->second == node->canonical_identity;
            diagnose(state.diagnostics,
                duplicate ? "FSIM-SDF-MAP-002" : "FSIM-SDF-MAP-003",
                duplicate
                    ? "SDF mapping contains a duplicate semantic annotation"
                    : "SDF mappings conflict at the same semantic annotation target",
                span);
            return false;
        }
        auto& summary
            = state
                  .accumulators[{ mapping.cell_id,
                      mapping.target_instance_path }]
                  .summary;
        ++summary.annotation_count;
        summary.endpoint_count += mapping.endpoints.size();
        for (const auto& endpoint : mapping.endpoints)
            summary.signals.push_back(endpoint.signal);
        update_counts(state.counts, mapping);
        return true;
    }

    [[nodiscard]] bool validate_all_mappings(MappingValidationState& state,
        const std::span<const SdfResolvedNodeEndpoints> mappings)
    {
        for (const auto& mapping : mappings) {
            if (!validate_one_mapping(state, mapping))
                return false;
        }
        if (state.expected.empty())
            return true;
        diagnose(state.diagnostics, "FSIM-SDF-MAP-004",
            "SDF endpoint resolution left an annotation construct unconsumed",
            state.expected.begin()->second);
        return false;
    }

    [[nodiscard]] std::vector<SdfAnnotationTargetSummary> finalize_targets(
        AccumulatorMap& accumulators)
    {
        std::vector<SdfAnnotationTargetSummary> targets;
        targets.reserve(accumulators.size());
        for (auto& [key, accumulator] : accumulators) {
            (void)key;
            auto& signals = accumulator.summary.signals;
            std::ranges::sort(signals);
            signals.erase(
                std::unique(signals.begin(), signals.end()), signals.end());
            targets.push_back(std::move(accumulator.summary));
        }
        return targets;
    }
} // namespace

SdfAnnotationSummary::SdfAnnotationSummary(
    std::shared_ptr<const SdfEndpointResolution> endpoint_resolution,
    std::vector<SdfAnnotationTargetSummary> targets,
    const std::size_t annotation_count, const std::size_t endpoint_count,
    const std::size_t delay_annotation_count,
    const std::size_t timing_check_annotation_count,
    const std::size_t timing_environment_annotation_count,
    std::string semantic_identity)
    : endpoint_resolution_(std::move(endpoint_resolution))
    , targets_(std::move(targets))
    , annotation_count_(annotation_count)
    , endpoint_count_(endpoint_count)
    , delay_annotation_count_(delay_annotation_count)
    , timing_check_annotation_count_(timing_check_annotation_count)
    , timing_environment_annotation_count_(timing_environment_annotation_count)
    , semantic_identity_(std::move(semantic_identity))
{
}

const std::shared_ptr<const SdfEndpointResolution>&
SdfAnnotationSummary::endpoint_resolution() const noexcept
{
    return endpoint_resolution_;
}

std::span<const SdfAnnotationTargetSummary> SdfAnnotationSummary::targets()
    const noexcept
{
    return targets_;
}

std::size_t SdfAnnotationSummary::annotation_count() const noexcept
{
    return annotation_count_;
}

std::size_t SdfAnnotationSummary::endpoint_count() const noexcept
{
    return endpoint_count_;
}

std::size_t SdfAnnotationSummary::delay_annotation_count() const noexcept
{
    return delay_annotation_count_;
}

std::size_t SdfAnnotationSummary::timing_check_annotation_count() const noexcept
{
    return timing_check_annotation_count_;
}

std::size_t SdfAnnotationSummary::timing_environment_annotation_count() const
    noexcept
{
    return timing_environment_annotation_count_;
}

std::string_view SdfAnnotationSummary::semantic_identity() const noexcept
{
    return semantic_identity_;
}

bool SdfMappingValidationResult::ok() const noexcept
{
    return summary && !frontend::has_errors(diagnostics);
}

SdfMappingValidationResult validate_sdf_mapping(
    std::shared_ptr<const SdfEndpointResolution> endpoint_resolution,
    const elaboration::ElaboratedDesign& elaborated,
    const SdfMappingValidationLimits limits)
{
    SdfMappingValidationResult result;
    if (!endpoint_resolution || !endpoint_resolution->cells()
        || !endpoint_resolution->cells()->scope()
        || !endpoint_resolution->cells()->scope()->normalized_ir()
        || endpoint_resolution->semantic_identity().empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-MAP-001",
            "SDF mapping validation requires a complete endpoint resolution",
            { });
        return result;
    }
    const auto& cells = *endpoint_resolution->cells();
    const auto& ir = *cells.scope()->normalized_ir();
    if (!validate_resolution_input(
            *endpoint_resolution, limits, result.diagnostics)) {
        return result;
    }
    NodesByCell nodes_by_cell;
    ExpectedMappings expected;
    AccumulatorMap accumulators;
    TargetIndex target_index;
    if (!build_validation_indexes(cells, ir, limits, nodes_by_cell, expected,
            accumulators, target_index, result.diagnostics)) {
        return result;
    }
    MappingValidationState state { ir, elaborated, limits, expected,
        accumulators, target_index, result.diagnostics, { }, { }, { }, { },
        false };
    if (!validate_all_mappings(state, endpoint_resolution->nodes()))
        return result;
    auto targets = finalize_targets(accumulators);
    auto identity = summary_identity(*endpoint_resolution, targets,
        state.counts, limits.max_identity_bytes);
    if (!identity) {
        diagnose(result.diagnostics, "FSIM-SDF-MAP-005",
            "SDF annotation summary identity exceeds the configured byte limit",
            ir.nodes().empty() ? SourceSpan { } : ir.nodes().front().span);
        return result;
    }
    result.summary = std::make_shared<const SdfAnnotationSummary>(
        std::move(endpoint_resolution), std::move(targets),
        state.counts.annotations, state.counts.endpoints, state.counts.delays,
        state.counts.timing_checks, state.counts.timing_environment,
        std::move(*identity));
    return result;
}

} // namespace fsim::app
