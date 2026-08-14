// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_precedence.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
constexpr std::string_view path_identity = "sdf:iopath:top.u:A=>Z:path-0";
constexpr std::string_view check_identity = "sdf:timingcheck:top.u:setup:0";

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

fsim::frontend::SourceSpan span(const std::size_t offset)
{
    fsim::frontend::SourceSpan result;
    result.source_name = "precedence.sdf";
    result.begin = { offset, 3U, 1U };
    result.end = { offset + 8U, 3U, 9U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.u.A", "top.u.Z" }) {
        const auto id = static_cast<runtime::simir::SignalId>(
            state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(
            std::string { name }, runtime::PackedLogic4(1U));
        state.signal_names.emplace_back(name, id);
    }
    elaboration::VerilogSpecifyPathInfo path;
    path.id = 0U;
    path.identity = path_identity;
    path.instance = "top.u";
    path.sources.push_back({ 0U, 0U, 1U });
    path.destinations.push_back({ 1U, 0U, 1U });
    path.delays = { 0U };
    path.source = span(10U);
    state.verilog_specify_paths.push_back(std::move(path));

    runtime::simir::ModuleTimingCheck check;
    check.id = 0U;
    check.identity = check_identity;
    check.kind = runtime::simir::ModuleTimingCheckKind::setup;
    check.reference.terminal = { 0U, 0U, 1U };
    check.data = runtime::simir::ModuleTimingEvent { };
    check.data->terminal = { 1U, 0U, 1U };
    check.limits = { 5 };
    state.verilog_timing_checks.push_back(std::move(check));

    auto design
        = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "precedence design fixture must validate");
    return std::move(*design);
}

fsim::app::SdfPlannedAnnotation path_annotation(
    const fsim::app::SdfDelayApplicationMode mode,
    std::vector<std::uint64_t> after, const bool retain = false)
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 1U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::Iopath;
    annotation.target_kind = app::SdfTimingTargetKind::SpecifyPath;
    annotation.delay_mode = mode;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = path_identity;
    annotation.before_ticks = { 0U };
    annotation.after_ticks = std::move(after);
    if (retain)
        annotation.retain_ticks = { 3U };
    annotation.source = span(100U);
    annotation.source_identity = "source-path";
    annotation.canonical_identity = "canonical-path";
    return annotation;
}

fsim::app::SdfPlannedAnnotation pulse_annotation()
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 2U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::PathPulse;
    annotation.target_kind = app::SdfTimingTargetKind::Pulse;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = path_identity;
    annotation.after_ticks = { 1U, 2U };
    annotation.source = span(110U);
    annotation.source_identity = "source-pulse";
    annotation.canonical_identity = "canonical-pulse";
    return annotation;
}

fsim::app::SdfPlannedAnnotation check_annotation(
    const std::int64_t before = 5, const std::int64_t after = 7)
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 3U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::Setup;
    annotation.target_kind = app::SdfTimingTargetKind::TimingCheck;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = check_identity;
    annotation.before_check_ticks = { before };
    annotation.after_check_ticks = { after };
    annotation.source = span(120U);
    annotation.source_identity = "source-check";
    annotation.canonical_identity = "canonical-check";
    return annotation;
}

fsim::app::SdfPlannedAnnotation endpoint_annotation()
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 4U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::Interconnect;
    annotation.target_kind = app::SdfTimingTargetKind::Interconnect;
    annotation.delay_mode = app::SdfDelayApplicationMode::Absolute;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = "sdf:interconnect:top.u:A=>Z:0";
    annotation.before_ticks = { 3U };
    annotation.after_ticks = { 4U };
    annotation.source = span(130U);
    annotation.source_identity = "source-interconnect";
    annotation.canonical_identity = "canonical-interconnect";
    return annotation;
}

std::shared_ptr<const fsim::app::SdfAnnotationSummary> make_summary(
    const std::span<const fsim::app::SdfPlannedAnnotation> annotations)
{
    using namespace fsim;
    std::vector<frontend::SdfIrNode> nodes;
    nodes.reserve(annotations.size());
    for (const auto& annotation : annotations) {
        frontend::SdfIrNode node;
        node.id = annotation.node_id;
        node.cell_id = annotation.cell_id;
        node.kind = annotation.construct_kind;
        node.span = annotation.source;
        node.source_identity = annotation.source_identity;
        node.canonical_identity = annotation.canonical_identity;
        nodes.push_back(std::move(node));
    }
    frontend::SdfIrCell cell;
    cell.id = 1U;
    cell.cell_type = "BUF";
    cell.node_count = nodes.size();
    cell.source_identity = "cell-source";
    cell.canonical_identity = "cell-canonical";
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "precedence.sdf", std::nullopt,
        std::vector<frontend::SdfIrCell> { std::move(cell) },
        std::move(nodes), "precedence-ir");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "precedence.sdf", "source-semantic", std::nullopt, '.', "project",
        "design", std::vector<app::SdfAnnotationRoot> { }, "scope-identity");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "cells-identity");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { },
        "endpoint-identity");
    return std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, annotations.size(),
        0U, annotations.size(), 0U, 0U, "summary-identity");
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> make_plan(
    std::vector<fsim::app::SdfPlannedAnnotation> annotations,
    const fsim::app::SdfDelaySelection selection
    = fsim::app::SdfDelaySelection::Typical)
{
    auto summary = make_summary(annotations);
    fsim::app::SdfValuePolicy policy;
    policy.selection = selection;
    return std::make_shared<const fsim::app::SdfAnnotationPlan>(
        std::move(summary), policy, std::move(annotations),
        "precedence-plan");
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfPrecedenceResult& result, const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error(
            "missing precedence diagnostic " + std::string { code });
    return *found;
}

void test_no_annotation_zero_delay_is_stable()
{
    using namespace fsim;
    const auto design = make_design();
    const auto first = app::apply_sdf_precedence(nullptr, design);
    const auto second = app::apply_sdf_precedence(nullptr, design);
    require(first.ok() && second.ok()
            && std::ranges::equal(first.application->values(),
                second.application->values())
            && first.application->semantic_identity()
                == second.application->semantic_identity(),
        "no-annotation precedence must be deterministic and immutable");
    const auto path = first.application->find_target(
        app::SdfTimingTargetKind::SpecifyPath, path_identity);
    require(path.size() == 12U
            && std::ranges::all_of(path, [](const auto& value) {
                   return value.selected_source
                       == app::SdfEffectiveValueSource::SourceSpecify
                       && value.enabled && value.source_delay_ticks == 0U
                       && value.effective_delay_ticks == 0U;
               }),
        "zero source specify delay must remain exact in all transition slots");
    const auto check = first.application->find_target(
        app::SdfTimingTargetKind::TimingCheck, check_identity);
    require(check.size() == 1U
            && check.front().selected_source
                == app::SdfEffectiveValueSource::SourceTimingCheck
            && check.front().source_check_ticks == 5
            && check.front().effective_check_ticks == 5,
        "unannotated timing checks must retain their exact source limit");
    require(design.verilog_specify_paths().front().delays
                == std::vector<std::uint64_t> { 0U }
            && design.verilog_timing_checks().front().limits
                == std::vector<std::int64_t> { 5 },
        "precedence publication must not mutate zero-delay source state");
}

void test_annotated_sources_and_pulse_precedence()
{
    using namespace fsim;
    const auto design = make_design();
    auto result = app::apply_sdf_precedence(
        make_plan({ path_annotation(app::SdfDelayApplicationMode::Absolute,
                        { 2U }, true),
            pulse_annotation(), check_annotation(), endpoint_annotation() }),
        design);
    require(result.ok(), "complete annotated precedence must publish");
    const auto path = result.application->find_target(
        app::SdfTimingTargetKind::SpecifyPath, path_identity);
    require(path.size() == 12U
            && std::ranges::all_of(path, [](const auto& value) {
                   return value.selected_source
                       == app::SdfEffectiveValueSource::SdfAbsolute
                       && value.source_delay_ticks == 0U
                       && value.effective_delay_ticks == 2U;
               }),
        "absolute SDF delay must override every expanded source transition");
    const auto check = result.application->find_target(
        app::SdfTimingTargetKind::TimingCheck, check_identity);
    require(check.size() == 1U
            && check.front().selected_source
                == app::SdfEffectiveValueSource::SdfTimingCheck
            && check.front().source_check_ticks == 5
            && check.front().effective_check_ticks == 7,
        "SDF timing-check limits must identify both source and effective value");
    const auto endpoint = result.application->find_target(
        app::SdfTimingTargetKind::Interconnect,
        "sdf:interconnect:top.u:A=>Z:0");
    require(endpoint.size() == 12U
            && std::ranges::all_of(endpoint, [](const auto& value) {
                   return value.selected_source
                       == app::SdfEffectiveValueSource::SdfAbsolute
                       && value.source_delay_ticks == 3U
                       && value.effective_delay_ticks == 4U;
               }),
        "primitive/net source delays and SDF overrides must remain auditable");
    const auto pulse = result.application->find_target(
        app::SdfTimingTargetKind::Pulse, path_identity);
    require(pulse.size() == 3U
            && pulse[0].role == app::SdfEffectiveValueRole::PulseReject
            && pulse[0].selected_source
                == app::SdfEffectiveValueSource::SdfPathPulse
            && pulse[0].effective_delay_ticks == 1U
            && pulse[1].role == app::SdfEffectiveValueRole::PulseError
            && pulse[1].effective_delay_ticks == 2U
            && pulse[2].role == app::SdfEffectiveValueRole::Retain
            && pulse[2].selected_source
                == app::SdfEffectiveValueSource::SdfRetain
            && pulse[2].effective_delay_ticks == 3U,
        "PATHPULSE reject/error and RETAIN values must retain selected sources");

    result = app::apply_sdf_precedence(
        make_plan({ path_annotation(app::SdfDelayApplicationMode::Increment,
            { 2U }) }),
        design);
    const auto increment = result.application->find_target(
        app::SdfTimingTargetKind::SpecifyPath, path_identity);
    require(result.ok() && increment.size() == 12U
            && increment.front().selected_source
                == app::SdfEffectiveValueSource::SdfIncrement
            && increment.front().effective_delay_ticks == 2U,
        "incremental SDF delay must retain its selected-source identity");
}

void test_disabled_controls_preserve_audit_values()
{
    using namespace fsim;
    app::SdfTimingPrecedencePolicy policy;
    policy.specify_paths_enabled = false;
    policy.endpoint_delays_enabled = false;
    policy.timing_checks_enabled = false;
    policy.pulse_rejection_enabled = false;
    const auto result = app::apply_sdf_precedence(
        make_plan({ path_annotation(app::SdfDelayApplicationMode::Absolute,
                        { 2U }, true),
            pulse_annotation(), check_annotation(), endpoint_annotation() }),
        make_design(), policy);
    require(result.ok() && !result.application->values().empty()
            && std::ranges::all_of(result.application->values(),
                [](const auto& value) {
                    return !value.enabled
                        && value.selected_source
                        == app::SdfEffectiveValueSource::Disabled
                        && (value.effective_delay_ticks
                            || value.effective_check_ticks);
                }),
        "disabled timing controls must retain immutable audit values only");
}

void test_contradiction_stale_and_resource_rejection()
{
    using namespace fsim;
    const auto design = make_design();
    app::SdfTimingPrecedencePolicy policy;
    policy.command_selection = app::SdfDelaySelection::Maximum;
    auto result = app::apply_sdf_precedence(
        make_plan({ path_annotation(app::SdfDelayApplicationMode::Absolute,
            { 2U }) }),
        design, policy);
    require(!result.ok() && !result.application,
        "command-selected delay mode conflict must reject atomically");
    require_diagnostic(result, "FSIM-SDF-PRECEDENCE-002");

    policy = { };
    policy.specify_paths_enabled = false;
    result = app::apply_sdf_precedence(
        make_plan({ path_annotation(app::SdfDelayApplicationMode::Absolute,
                        { 2U }, true),
            pulse_annotation() }),
        design, policy);
    require(!result.ok(), "enabled pulse policy cannot target disabled paths");
    require_diagnostic(result, "FSIM-SDF-PRECEDENCE-002");

    result = app::apply_sdf_precedence(
        make_plan({ check_annotation(6, 7) }), design);
    require(!result.ok(), "stale timing-check source limits must reject");
    require_diagnostic(result, "FSIM-SDF-PRECEDENCE-003");

    auto stale_path = path_annotation(
        app::SdfDelayApplicationMode::Absolute, { 2U });
    stale_path.before_ticks = { 9U };
    result = app::apply_sdf_precedence(
        make_plan({ std::move(stale_path) }), design);
    require(!result.ok(), "stale source path delays must reject");
    require_diagnostic(result, "FSIM-SDF-PRECEDENCE-003");

    auto missing_check = check_annotation();
    missing_check.target_identity = "sdf:timingcheck:top.u:missing:0";
    result = app::apply_sdf_precedence(
        make_plan({ std::move(missing_check) }), design);
    require(!result.ok(), "missing source timing checks must reject");
    require_diagnostic(result, "FSIM-SDF-PRECEDENCE-003");

    policy = { };
    policy.command_selection = static_cast<app::SdfDelaySelection>(99U);
    result = app::apply_sdf_precedence(nullptr, design, policy);
    require(!result.ok(), "invalid command delay mode must reject");
    require_diagnostic(result, "FSIM-SDF-PRECEDENCE-001");

    app::SdfPrecedenceLimits limits;
    limits.max_values = 1U;
    result = app::apply_sdf_precedence(nullptr, design, { }, limits);
    require(!result.ok(), "effective-value resource limit must reject");
    require_diagnostic(result, "FSIM-SDF-PRECEDENCE-004");

    limits = { };
    limits.max_identity_bytes = 1U;
    result = app::apply_sdf_precedence(nullptr, design, { }, limits);
    require(!result.ok(), "semantic-identity resource limit must reject");
    require_diagnostic(result, "FSIM-SDF-PRECEDENCE-004");
}
} // namespace

int main()
{
    try {
        test_no_annotation_zero_delay_is_stable();
        test_annotated_sources_and_pulse_precedence();
        test_disabled_controls_preserve_audit_values();
        test_contradiction_stale_and_resource_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
