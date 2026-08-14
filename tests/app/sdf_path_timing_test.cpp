// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_path_timing.hpp"

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

fsim::frontend::SourceSpan source_span()
{
    fsim::frontend::SourceSpan result;
    result.source_name = "path.sdf";
    result.begin = { 12U, 4U, 3U };
    result.end = { 18U, 4U, 9U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_design(
    std::vector<fsim::runtime::SimulationTick> delays = { 17U, 19U },
    const bool conditional = false,
    const fsim::frontend::VerilogSpecifyEdge edge
    = fsim::frontend::VerilogSpecifyEdge::None)
{
    fsim::elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.u.A", "top.u.Z" }) {
        const auto id = static_cast<fsim::runtime::simir::SignalId>(
            state.signal_info.size());
        fsim::elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = fsim::frontend::ValueDomain::Logic4;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(
            std::string { name }, fsim::runtime::PackedLogic4(1U));
        state.signal_names.emplace_back(name, id);
    }
    fsim::elaboration::VerilogSpecifyPathInfo path;
    path.id = 0U;
    path.identity = "sdf:iopath:top.u:A=>Z:path-0";
    path.instance = "top.u";
    path.sources.push_back({ 0U, 0U, 1U });
    path.destinations.push_back({ 1U, 0U, 1U });
    path.delays = std::move(delays);
    path.conditional = conditional;
    path.source_edge = edge;
    if (conditional) {
        using namespace fsim::runtime::simir;
        path.condition_program.nodes.push_back(ModulePathExpressionNode {
            ModulePathExpressionOperator::terminal,
            { },
            fsim::runtime::PackedLogic4 { },
            { 0U, 0U, 1U },
            BinaryOperator::bit_and,
            LogicalBinaryOperator::logical_and,
            ShiftOperator::logical_left,
            ReductionOperator::bit_and,
            1U,
            false });
    }
    path.source = source_span();
    state.verilog_specify_paths.push_back(std::move(path));
    auto design
        = fsim::elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "path-timing design fixture must be valid");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfAnnotationSummary> make_summary()
{
    using namespace fsim;
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "path.sdf", std::nullopt, std::vector<frontend::SdfIrCell> { },
        std::vector<frontend::SdfIrNode> { }, "path-ir-identity");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir, "path.sdf",
        "path-source-identity", std::nullopt, '.', "project", "design",
        std::vector<app::SdfAnnotationRoot> { }, "path-scope-identity");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "path-cells-identity");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { },
        "path-endpoint-identity");
    return std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, 1U, 0U, 1U, 0U, 0U,
        "path-summary-identity");
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> make_plan(
    const fsim::app::SdfDelayApplicationMode mode,
    std::vector<std::uint64_t> before,
    std::vector<std::uint64_t> after, std::string condition = { },
    std::vector<std::string> edges = { },
    std::string target = "sdf:iopath:top.u:A=>Z:path-0")
{
    fsim::app::SdfPlannedAnnotation annotation;
    annotation.node_id = 1U;
    annotation.cell_id = 1U;
    annotation.construct_kind = fsim::frontend::SdfConstructKind::Iopath;
    annotation.target_kind = fsim::app::SdfTimingTargetKind::SpecifyPath;
    annotation.delay_mode = mode;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = std::move(target);
    annotation.condition_identity = std::move(condition);
    annotation.edge_identities = std::move(edges);
    annotation.endpoint_signals = { 0U, 1U };
    annotation.before_ticks = std::move(before);
    annotation.after_ticks = std::move(after);
    annotation.source = source_span();
    annotation.source_identity = "path-source";
    annotation.canonical_identity = "planned-path-identity";
    return std::make_shared<const fsim::app::SdfAnnotationPlan>(
        make_summary(), fsim::app::SdfValuePolicy { },
        std::vector<fsim::app::SdfPlannedAnnotation> {
            std::move(annotation) },
        "target-plan-identity");
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfPathTimingResult& result, const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error(
            "missing path-timing diagnostic " + std::string { code });
    return *found;
}

void test_absolute_and_incremental_application()
{
    auto design = make_design();
    auto result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute,
            { 17U, 19U }, { 5U, 7U }),
        design);
    require(result.ok() && result.application->paths().size() == 1U,
        "absolute IOPATH application should publish one path");
    const auto* applied = result.application->find_path(0U);
    require(applied != nullptr
            && applied->source_delays
                == std::vector<fsim::runtime::SimulationTick>({ 17U, 19U })
            && applied->effective_delays
                == std::vector<fsim::runtime::SimulationTick>({ 5U, 7U })
            && applied->effective_path.delays == applied->effective_delays,
        "absolute IOPATH should replace only the overlay delay profile");
    require(design.verilog_specify_paths().front().delays
            == std::vector<fsim::runtime::SimulationTick>({ 17U, 19U }),
        "path application must not mutate the source design");

    result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Increment,
            { 17U, 19U }, { 2U, 3U }),
        design);
    require(result.ok()
            && result.application->paths().front().effective_delays
                == std::vector<fsim::runtime::SimulationTick>({ 19U, 22U }),
        "incremental IOPATH should add matching values with checked arithmetic");
}

void test_stale_condition_edge_and_arity_rejection()
{
    auto design = make_design();
    auto result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute,
            { 16U, 19U }, { 5U, 7U }),
        design);
    require(!result.ok() && result.application == nullptr,
        "stale before-values must prevent publication");
    require_diagnostic(result, "FSIM-SDF-PATH-001");

    result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute,
            { 17U, 19U }, { 5U, 7U }, "A===1'b1"),
        design);
    require(!result.ok(), "condition mismatch must reject");
    require_diagnostic(result, "FSIM-SDF-PATH-002");

    result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute,
            { 17U, 19U }, { 5U, 7U }, { }, { "posedge" }),
        design);
    require(!result.ok(), "edge mismatch must reject");
    require_diagnostic(result, "FSIM-SDF-PATH-002");

    result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Increment,
            { 17U, 19U }, { 1U }),
        design);
    require(!result.ok(), "incremental arity mismatch must reject");
    require_diagnostic(result, "FSIM-SDF-PATH-003");
}

void test_conditional_edge_and_resource_boundaries()
{
    auto design = make_design({ 17U, 19U }, true,
        fsim::frontend::VerilogSpecifyEdge::Posedge);
    auto result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute,
            { 17U, 19U }, { 5U, 7U }, "A===1'b1", { "posedge" }),
        design);
    require(result.ok(),
        "matching conditional edge-sensitive IOPATH should apply");
    require(result.application->paths().front().effective_path.conditional
            && result.application->paths().front().effective_path.source_edge
                == fsim::frontend::VerilogSpecifyEdge::Posedge,
        "overlay must preserve conditional and edge semantics");

    auto limits = fsim::app::SdfPathTimingLimits { };
    limits.max_identity_bytes = 8U;
    result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Absolute,
            { 17U, 19U }, { 5U, 7U }),
        design, limits);
    require(!result.ok(), "identity-byte boundary must reject atomically");
    require_diagnostic(result, "FSIM-SDF-PATH-004");

    design = make_design(
        { std::numeric_limits<fsim::runtime::SimulationTick>::max() });
    result = fsim::app::apply_sdf_path_timing(
        make_plan(fsim::app::SdfDelayApplicationMode::Increment,
            { std::numeric_limits<std::uint64_t>::max() }, { 1U }),
        design);
    require(!result.ok(), "incremental tick overflow must reject atomically");
    require_diagnostic(result, "FSIM-SDF-PATH-004");
}
} // namespace

int main()
{
    try {
        test_absolute_and_incremental_application();
        test_stale_condition_edge_and_arity_rejection();
        test_conditional_edge_and_resource_boundaries();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
