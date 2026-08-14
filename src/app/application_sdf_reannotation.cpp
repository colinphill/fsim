// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_reannotation.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), { }, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] bool same_expression(
        const runtime::simir::ModulePathExpression& left,
        const runtime::simir::ModulePathExpression& right)
    {
        return left.root == right.root && left.nodes.size() == right.nodes.size()
            && std::ranges::equal(left.nodes, right.nodes,
                [](const runtime::simir::ModulePathExpressionNode& left_node,
                    const runtime::simir::ModulePathExpressionNode& right_node) {
                    return left_node.operation == right_node.operation
                        && left_node.operands == right_node.operands
                        && left_node.constant == right_node.constant
                        && left_node.terminal == right_node.terminal
                        && left_node.binary == right_node.binary
                        && left_node.logical == right_node.logical
                        && left_node.shift == right_node.shift
                        && left_node.reduction == right_node.reduction
                        && left_node.width == right_node.width
                        && left_node.is_signed == right_node.is_signed;
                });
    }

    [[nodiscard]] bool same_path_topology(
        const elaboration::VerilogSpecifyPathInfo& left,
        const elaboration::VerilogSpecifyPathInfo& right)
    {
        return left.id == right.id && left.identity == right.identity
            && left.instance == right.instance && left.sources == right.sources
            && left.destinations == right.destinations
            && left.drivers == right.drivers
            && same_expression(left.condition_program, right.condition_program)
            && same_expression(
                left.data_source_program, right.data_source_program)
            && left.selection_group == right.selection_group
            && left.kind == right.kind && left.source_edge == right.source_edge
            && left.polarity == right.polarity
            && left.pulse_style == right.pulse_style
            && left.show_cancelled == right.show_cancelled
            && left.conditional == right.conditional
            && left.ifnone == right.ifnone;
    }

    [[nodiscard]] bool same_event_topology(
        const runtime::simir::ModuleTimingEvent& left,
        const runtime::simir::ModuleTimingEvent& right)
    {
        return left.terminal == right.terminal && left.edge == right.edge
            && left.edge_descriptors == right.edge_descriptors
            && same_expression(left.condition, right.condition);
    }

    [[nodiscard]] bool same_check_topology(
        const runtime::simir::ModuleTimingCheck& left,
        const runtime::simir::ModuleTimingCheck& right)
    {
        return left.id == right.id && left.identity == right.identity
            && left.kind == right.kind
            && same_event_topology(left.reference, right.reference)
            && left.data.has_value() == right.data.has_value()
            && (!left.data || same_event_topology(*left.data, *right.data))
            && left.notifier == right.notifier
            && same_expression(
                left.timestamp_condition, right.timestamp_condition)
            && same_expression(
                left.timecheck_condition, right.timecheck_condition)
            && left.delayed_reference == right.delayed_reference
            && left.delayed_data == right.delayed_data
            && left.event_based == right.event_based
            && left.remain_active == right.remain_active
            && left.limits.size() == right.limits.size();
    }

    [[nodiscard]] bool same_path_timing(
        const elaboration::VerilogSpecifyPathInfo& left,
        const elaboration::VerilogSpecifyPathInfo& right)
    {
        return left.delays == right.delays
            && left.pulse_reject_limit == right.pulse_reject_limit
            && left.pulse_error_limit == right.pulse_error_limit
            && left.pulse_reject_delays == right.pulse_reject_delays
            && left.pulse_error_delays == right.pulse_error_delays
            && left.retain_delays == right.retain_delays;
    }

    [[nodiscard]] bool same_check_timing(
        const runtime::simir::ModuleTimingCheck& left,
        const runtime::simir::ModuleTimingCheck& right)
    {
        return left.limits == right.limits && left.threshold == right.threshold;
    }

    void copy_path_timing(elaboration::VerilogSpecifyPathInfo& target,
        const elaboration::VerilogSpecifyPathInfo& source)
    {
        target.delays = source.delays;
        target.pulse_reject_limit = source.pulse_reject_limit;
        target.pulse_error_limit = source.pulse_error_limit;
        target.pulse_reject_delays = source.pulse_reject_delays;
        target.pulse_error_delays = source.pulse_error_delays;
        target.retain_delays = source.retain_delays;
        target.source = source.source;
    }

    void copy_check_timing(runtime::simir::ModuleTimingCheck& target,
        const runtime::simir::ModuleTimingCheck& source)
    {
        target.limits = source.limits;
        target.threshold = source.threshold;
        target.source = source.source;
    }

    [[nodiscard]] bool glob_matches(
        const std::string_view pattern, const std::string_view value) noexcept
    {
        std::size_t pattern_index { };
        std::size_t value_index { };
        std::size_t star = std::string_view::npos;
        std::size_t retry { };
        while (value_index < value.size()) {
            if (pattern_index < pattern.size()
                && (pattern[pattern_index] == '?'
                    || pattern[pattern_index] == value[value_index])) {
                ++pattern_index;
                ++value_index;
            } else if (pattern_index < pattern.size()
                && pattern[pattern_index] == '*') {
                star = pattern_index++;
                retry = value_index;
            } else if (star != std::string_view::npos) {
                pattern_index = star + 1U;
                value_index = ++retry;
            } else {
                return false;
            }
        }
        while (pattern_index < pattern.size()
            && pattern[pattern_index] == '*') {
            ++pattern_index;
        }
        return pattern_index == pattern.size();
    }

    [[nodiscard]] bool instance_in_root(
        const std::string_view instance, const std::string_view root) noexcept
    {
        return instance == root
            || (instance.size() > root.size()
                && instance.starts_with(root) && instance[root.size()] == '.');
    }

    [[nodiscard]] bool scope_matches(const std::string_view instance,
        const SdfReannotationLayer& layer) noexcept
    {
        if (!instance_in_root(instance, layer.root))
            return false;
        const auto relative = instance == layer.root
            ? std::string_view { }
            : instance.substr(layer.root.size() + 1U);
        return glob_matches(layer.cell_pattern, instance)
            || glob_matches(layer.cell_pattern, relative);
    }

    [[nodiscard]] std::string_view check_instance(
        const std::string_view identity) noexcept
    {
        constexpr std::string_view prefix = "sdf:timingcheck:";
        if (!identity.starts_with(prefix))
            return { };
        const auto begin = prefix.size();
        const auto end = identity.find(':', begin);
        return end == std::string_view::npos
            ? identity.substr(begin)
            : identity.substr(begin, end - begin);
    }

    struct Winner {
        std::size_t layer { };
        std::size_t source_index { };
        bool timing_check { };
    };

    [[nodiscard]] auto precedence_key(const SdfReannotationLayer& layer)
    {
        return std::tie(layer.file_precedence, layer.cell_precedence);
    }

    [[nodiscard]] std::string timing_identity(
        const elaboration::VerilogSpecifyPathInfo& path)
    {
        std::string result = "path";
        for (const auto value : path.delays)
            append_field(result, std::to_string(value));
        append_field(result, path.pulse_reject_limit ? std::to_string(*path.pulse_reject_limit) : "-");
        append_field(result, path.pulse_error_limit ? std::to_string(*path.pulse_error_limit) : "-");
        for (const auto value : path.pulse_reject_delays)
            append_field(result, std::to_string(value));
        for (const auto value : path.pulse_error_delays)
            append_field(result, std::to_string(value));
        for (const auto value : path.retain_delays)
            append_field(result, std::to_string(value));
        return result;
    }

    [[nodiscard]] std::string timing_identity(
        const runtime::simir::ModuleTimingCheck& check)
    {
        std::string result = "check";
        for (const auto value : check.limits)
            append_field(result, std::to_string(value));
        append_field(result,
            check.threshold ? std::to_string(*check.threshold) : "-");
        return result;
    }

    [[nodiscard]] SdfReannotationRevision make_revision(
        const std::string_view target_identity,
        const SdfReannotationLayer& layer,
        const bool timing_check,
        const std::string_view timing)
    {
        SdfReannotationRevision revision;
        revision.target_identity = target_identity;
        revision.file_identity = layer.file_identity;
        revision.root = layer.root;
        revision.cell_pattern = layer.cell_pattern;
        revision.file_precedence = layer.file_precedence;
        revision.cell_precedence = layer.cell_precedence;
        revision.timing_check = timing_check;
        revision.canonical_identity = "sdf-reannotation-revision-v1";
        append_field(revision.canonical_identity, revision.target_identity);
        append_field(revision.canonical_identity, revision.file_identity);
        append_field(revision.canonical_identity, revision.root);
        append_field(revision.canonical_identity, revision.cell_pattern);
        append_field(revision.canonical_identity,
            std::to_string(revision.file_precedence));
        append_field(revision.canonical_identity,
            std::to_string(revision.cell_precedence));
        append_field(revision.canonical_identity,
            revision.timing_check ? "check" : "path");
        append_field(revision.canonical_identity, timing);
        return revision;
    }

    [[nodiscard]] runtime::simir::ModulePath runtime_path(
        const elaboration::VerilogSpecifyPathInfo& path)
    {
        runtime::simir::ModulePath result;
        result.id = path.id;
        result.identity = path.identity;
        const auto append_terminals = [](const auto& source, auto& destination) {
            destination.reserve(source.size());
            for (const auto& terminal : source) {
                destination.push_back(runtime::simir::ModulePathTerminal {
                    terminal.signal, terminal.offset, terminal.width });
            }
        };
        append_terminals(path.sources, result.sources);
        append_terminals(path.destinations, result.destinations);
        result.drivers = path.drivers;
        result.delays = path.delays;
        result.condition = path.condition_program;
        result.data_source = path.data_source_program;
        result.selection_group = path.selection_group;
        result.full = path.kind == frontend::VerilogModulePathKind::Full;
        result.conditional = path.conditional;
        result.ifnone = path.ifnone;
        switch (path.source_edge) {
        case frontend::VerilogSpecifyEdge::None:
            result.source_edge = runtime::simir::ModulePathEdge::none;
            break;
        case frontend::VerilogSpecifyEdge::Posedge:
            result.source_edge = runtime::simir::ModulePathEdge::posedge;
            break;
        case frontend::VerilogSpecifyEdge::Negedge:
            result.source_edge = runtime::simir::ModulePathEdge::negedge;
            break;
        case frontend::VerilogSpecifyEdge::Edge:
            result.source_edge = runtime::simir::ModulePathEdge::edge;
            break;
        }
        switch (path.polarity) {
        case frontend::VerilogPathPolarity::None:
            result.polarity = runtime::simir::ModulePathPolarity::none;
            break;
        case frontend::VerilogPathPolarity::Positive:
            result.polarity = runtime::simir::ModulePathPolarity::positive;
            break;
        case frontend::VerilogPathPolarity::Negative:
            result.polarity = runtime::simir::ModulePathPolarity::negative;
            break;
        }
        result.pulse_style
            = path.pulse_style == frontend::VerilogPulseStyle::Ondetect
            ? runtime::simir::ModulePathPulseStyle::ondetect
            : runtime::simir::ModulePathPulseStyle::onevent;
        result.show_cancelled = path.show_cancelled;
        result.pulse_reject_limit = path.pulse_reject_limit;
        result.pulse_error_limit = path.pulse_error_limit;
        result.pulse_reject_delays = path.pulse_reject_delays;
        result.pulse_error_delays = path.pulse_error_delays;
        result.retain_delays = path.retain_delays;
        result.source = runtime::simir::SourceLocation {
            path.source.source_name,
            static_cast<std::uint32_t>(path.source.begin.line),
            static_cast<std::uint32_t>(path.source.begin.column)
        };
        return result;
    }

    [[nodiscard]] bool validate_layer_topology(
        const elaboration::ElaboratedDesign& baseline,
        const elaboration::ElaboratedDesign& layer)
    {
        if (!std::ranges::equal(layer.roots(), baseline.roots())
            || layer.verilog_specify_paths().size()
                != baseline.verilog_specify_paths().size()
            || layer.verilog_timing_checks().size()
                != baseline.verilog_timing_checks().size()) {
            return false;
        }
        for (std::size_t index = 0;
            index < baseline.verilog_specify_paths().size(); ++index) {
            if (!same_path_topology(baseline.verilog_specify_paths()[index],
                    layer.verilog_specify_paths()[index])) {
                return false;
            }
        }
        for (std::size_t index = 0;
            index < baseline.verilog_timing_checks().size(); ++index) {
            if (!same_check_topology(baseline.verilog_timing_checks()[index],
                    layer.verilog_timing_checks()[index])) {
                return false;
            }
        }
        return true;
    }
}

const std::shared_ptr<const SdfDriveTimingApplication>&
SdfReannotationApplication::baseline() const noexcept
{
    return baseline_;
}

const elaboration::ElaboratedDesign&
SdfReannotationApplication::design() const noexcept
{
    return design_;
}

std::span<const SdfReannotationRevision>
SdfReannotationApplication::revisions() const noexcept
{
    return revisions_;
}

std::uint64_t SdfReannotationApplication::generation() const noexcept
{
    return generation_;
}

SdfPendingEventPolicy
SdfReannotationApplication::pending_event_policy() const noexcept
{
    return SdfPendingEventPolicy::PreserveScheduledTiming;
}

SdfTimingCheckStatePolicy
SdfReannotationApplication::timing_check_state_policy() const noexcept
{
    return SdfTimingCheckStatePolicy::PreserveHistory;
}

std::string_view SdfReannotationApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfReannotationApplication::SdfReannotationApplication(
    std::shared_ptr<const SdfDriveTimingApplication> baseline,
    elaboration::ElaboratedDesign design,
    std::vector<SdfReannotationRevision> revisions,
    const std::uint64_t generation,
    std::string semantic_identity)
    : baseline_(std::move(baseline))
    , design_(std::move(design))
    , revisions_(std::move(revisions))
    , generation_(generation)
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfReannotationResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

bool SdfReannotationCommitResult::ok() const noexcept
{
    return committed && diagnostics.empty();
}

SdfReannotationResult apply_sdf_reannotation(
    std::shared_ptr<const SdfDriveTimingApplication> baseline,
    const std::span<const SdfReannotationLayer> layers,
    const std::uint64_t generation,
    const SdfReannotationLimits limits)
{
    SdfReannotationResult result;
    if (!baseline || !baseline->scheduling()
        || baseline->semantic_identity().empty() || layers.empty()
        || generation == 0U || limits.max_files == 0U
        || limits.max_targets == 0U || limits.max_pattern_bytes == 0U
        || limits.max_identity_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-001",
            "SDF reannotation requires a baseline, layers, generation and nonzero limits");
        return result;
    }
    if (layers.size() > limits.max_files) {
        diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-004",
            "SDF reannotation exceeds its configured file limit");
        return result;
    }

    const auto& baseline_design = baseline->scheduling()->design();
    const std::set<std::string_view> baseline_roots(
        baseline_design.roots().begin(), baseline_design.roots().end());
    std::vector<const elaboration::ElaboratedDesign*> layer_designs;
    layer_designs.reserve(layers.size());
    std::size_t pattern_bytes { };
    for (const auto& layer : layers) {
        if (!layer.timing || !layer.timing->scheduling()
            || layer.timing->semantic_identity().empty()
            || layer.file_identity.empty() || layer.root.empty()
            || layer.cell_pattern.empty()
            || !baseline_roots.contains(layer.root)) {
            diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-001",
                "SDF reannotation contains an incomplete file or cell scope");
            return result;
        }
        const auto added_bytes = layer.file_identity.size() + layer.root.size()
            + layer.cell_pattern.size();
        if (added_bytes > limits.max_pattern_bytes
            || pattern_bytes > limits.max_pattern_bytes - added_bytes) {
            diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-004",
                "SDF reannotation exceeds its configured scope-byte limit");
            return result;
        }
        pattern_bytes += added_bytes;
        const auto& design = layer.timing->scheduling()->design();
        if (!validate_layer_topology(baseline_design, design)) {
            diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-003",
                "SDF reannotation layer changes the elaborated timing topology");
            return result;
        }
        layer_designs.push_back(&design);
    }

    std::map<std::string, Winner> winners;
    std::vector<bool> matched(layers.size());
    const auto select = [&](const std::string& identity,
                            const std::size_t source_index,
                            const bool timing_check,
                            const std::size_t layer_index) -> bool {
        matched[layer_index] = true;
        const Winner candidate { layer_index, source_index, timing_check };
        const auto [position, inserted] = winners.emplace(identity, candidate);
        if (inserted)
            return true;
        auto& winner = position->second;
        const auto& winner_layer = layers[winner.layer];
        const auto& candidate_layer = layers[layer_index];
        if (precedence_key(candidate_layer) == precedence_key(winner_layer)) {
            const bool duplicate = timing_check
                ? same_check_timing(
                      layer_designs[winner.layer]
                          ->verilog_timing_checks()[winner.source_index],
                      layer_designs[layer_index]
                          ->verilog_timing_checks()[source_index])
                : same_path_timing(
                      layer_designs[winner.layer]
                          ->verilog_specify_paths()[winner.source_index],
                      layer_designs[layer_index]
                          ->verilog_specify_paths()[source_index]);
            diagnose(result.diagnostics,
                duplicate ? "FSIM-SDF-REANNOTATION-002"
                          : "FSIM-SDF-REANNOTATION-003",
                duplicate
                    ? "SDF reannotation contains an exact duplicate target at equal precedence"
                    : "SDF reannotation contains conflicting target values at equal precedence");
            return false;
        }
        if (precedence_key(winner_layer) < precedence_key(candidate_layer))
            winner = candidate;
        return true;
    };

    for (std::size_t layer_index = 0; layer_index < layers.size();
        ++layer_index) {
        const auto& layer = layers[layer_index];
        const auto& design = *layer_designs[layer_index];
        for (std::size_t index = 0;
            index < design.verilog_specify_paths().size(); ++index) {
            const auto& path = design.verilog_specify_paths()[index];
            if (scope_matches(path.instance, layer)
                && !select(path.identity, index, false, layer_index)) {
                return result;
            }
        }
        for (std::size_t index = 0;
            index < design.verilog_timing_checks().size(); ++index) {
            const auto& check = design.verilog_timing_checks()[index];
            if (scope_matches(check_instance(check.identity), layer)
                && !select(check.identity, index, true, layer_index)) {
                return result;
            }
        }
    }
    if (std::ranges::find(matched, false) != matched.end()) {
        diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-001",
            "SDF reannotation cell scope matches no timing target");
        return result;
    }
    if (winners.size() > limits.max_targets) {
        diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-004",
            "SDF reannotation exceeds its configured target limit");
        return result;
    }

    auto effective_state = baseline_design.state();
    std::vector<SdfReannotationRevision> revisions;
    revisions.reserve(winners.size());
    std::size_t identity_bytes { };
    for (const auto& [identity, winner] : winners) {
        const auto& layer = layers[winner.layer];
        if (winner.timing_check) {
            const auto& source = layer_designs[winner.layer]
                                     ->verilog_timing_checks()[winner.source_index];
            copy_check_timing(
                effective_state.verilog_timing_checks[winner.source_index],
                source);
            revisions.push_back(make_revision(
                identity, layer, true, timing_identity(source)));
        } else {
            const auto& source = layer_designs[winner.layer]
                                     ->verilog_specify_paths()[winner.source_index];
            copy_path_timing(
                effective_state.verilog_specify_paths[winner.source_index],
                source);
            revisions.push_back(make_revision(
                identity, layer, false, timing_identity(source)));
        }
        const auto size = revisions.back().canonical_identity.size();
        if (size > limits.max_identity_bytes
            || identity_bytes > limits.max_identity_bytes - size) {
            diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-004",
                "SDF reannotation exceeds its configured identity-byte limit");
            return result;
        }
        identity_bytes += size;
    }

    auto design = elaboration::ElaboratedDesign::from_state(
        std::move(effective_state));
    if (!design) {
        diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-003",
            "SDF reannotation produced invalid effective timing");
        return result;
    }
    std::string semantic_identity = "sdf-reannotation-application-v1";
    append_field(semantic_identity, baseline->semantic_identity());
    append_field(semantic_identity, std::to_string(generation));
    for (const auto& revision : revisions)
        append_field(semantic_identity, revision.canonical_identity);
    if (semantic_identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-004",
            "SDF reannotation semantic identity exceeds its configured limit");
        return result;
    }
    result.application = std::make_shared<const SdfReannotationApplication>(
        std::move(baseline), std::move(*design), std::move(revisions),
        generation, std::move(semantic_identity));
    return result;
}

SdfReannotationCommitResult commit_sdf_reannotation(
    const SdfReannotationApplication& application,
    runtime::simir::Interpreter& interpreter)
{
    SdfReannotationCommitResult result;
    result.generation = application.generation();
    try {
        std::vector<runtime::simir::ModulePath> paths;
        paths.reserve(application.design().verilog_specify_paths().size());
        for (const auto& path : application.design().verilog_specify_paths())
            paths.push_back(runtime_path(path));
        interpreter.reannotate_module_timing(
            paths, application.design().verilog_timing_checks());
        result.committed = true;
    } catch (const std::exception& error) {
        diagnose(result.diagnostics, "FSIM-SDF-REANNOTATION-001",
            std::string { "SDF reannotation commit rejected: " }
                + error.what());
    }
    return result;
}

} // namespace fsim::app
