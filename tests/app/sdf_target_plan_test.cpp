// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_target_plan.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
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

fsim::frontend::SourceSpan span(const std::size_t offset)
{
    fsim::frontend::SourceSpan result;
    result.source_name = "plan.sdf";
    result.begin = { offset, 10U, offset + 1U };
    result.end = { offset + 1U, 10U, offset + 2U };
    return result;
}

fsim::frontend::SdfExactDecimal decimal(
    std::string coefficient, const std::int64_t exponent)
{
    fsim::frontend::SdfExactDecimal result;
    result.coefficient = std::move(coefficient);
    result.exponent10 = exponent;
    result.canonical
        = result.coefficient + 'e' + std::to_string(result.exponent10);
    return result;
}

fsim::frontend::SdfExactValue scalar(
    fsim::frontend::SdfExactDecimal value)
{
    fsim::frontend::SdfExactValue result;
    result.kind = fsim::frontend::SdfExactValueKind::Scalar;
    result.components[0] = std::move(value);
    return result;
}

fsim::frontend::SdfNormalizedTimescale nanoseconds()
{
    fsim::frontend::SdfNormalizedTimescale result;
    result.unit = fsim::frontend::SdfTimeUnit::Nanosecond;
    result.magnitude = decimal("1", 0);
    result.femtoseconds = decimal("1", 6);
    result.canonical = "1ns";
    return result;
}

fsim::elaboration::ElaboratedDesign make_design()
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
    path.delays = { 17U, 19U };
    path.source = span(40U);
    state.verilog_specify_paths.push_back(std::move(path));
    auto design
        = fsim::elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "target-plan design fixture must be valid");
    return std::move(*design);
}

fsim::app::SdfResolvedEndpoint endpoint(
    const fsim::app::SdfEndpointRole role,
    const fsim::runtime::simir::SignalId signal, std::string object)
{
    fsim::app::SdfResolvedEndpoint result;
    result.role = role;
    result.object_kind = fsim::app::SdfEndpointObjectKind::HdlPort;
    result.instance_path = "top.u";
    result.object_path = std::move(object);
    result.signal = signal;
    result.object_width = 1U;
    result.language = fsim::app::SdfScopeRootLanguage::SystemVerilog;
    return result;
}

struct Fixture {
    fsim::elaboration::ElaboratedDesign design;
    std::shared_ptr<const fsim::app::SdfAnnotationSummary> summary;
};

Fixture make_fixture(const bool duplicate = false,
    const bool missing_path = false,
    const fsim::app::SdfScopeRootLanguage language
    = fsim::app::SdfScopeRootLanguage::SystemVerilog)
{
    using namespace fsim;
    std::vector<frontend::SdfIrNode> nodes;
    const auto add_annotation = [&](const std::uint64_t root_id) {
        frontend::SdfIrNode root;
        root.id = root_id;
        root.cell_id = 1U;
        root.kind = frontend::SdfConstructKind::Iopath;
        root.span = span(static_cast<std::size_t>(root_id));
        root.source_identity = "source-iopath-" + std::to_string(root_id);
        root.canonical_identity = "canonical-iopath-" + std::to_string(root_id);
        nodes.push_back(std::move(root));

        frontend::SdfIrNode value;
        value.id = root_id + 1U;
        value.cell_id = 1U;
        value.parent_id = root_id;
        value.depth = 1U;
        value.kind = frontend::SdfConstructKind::Value;
        value.exact_value = scalar(decimal("2", 0));
        value.span = span(static_cast<std::size_t>(root_id + 1U));
        value.source_identity = "source-value-" + std::to_string(root_id);
        value.canonical_identity = "canonical-value-" + std::to_string(root_id);
        nodes.push_back(std::move(value));
    };
    add_annotation(1U);
    if (duplicate)
        add_annotation(3U);

    frontend::SdfIrCell ir_cell;
    ir_cell.id = 1U;
    ir_cell.cell_type = "BUF";
    ir_cell.node_count = nodes.size();
    ir_cell.source_identity = "cell-source";
    ir_cell.canonical_identity = "cell-canonical";
    auto ir = std::make_shared<const frontend::SdfIr>(
        frontend::SdfRevision::Sdf40, frontend::SdfRevisionAdapter::None,
        "plan.sdf", nanoseconds(),
        std::vector<frontend::SdfIrCell> { std::move(ir_cell) },
        std::move(nodes), "ir-identity");

    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir, "plan.sdf",
        "source-semantic", std::nullopt, '.', "project", "design",
        std::vector<app::SdfAnnotationRoot> { }, "scope-identity");
    app::SdfResolvedInstance target;
    target.declaration_id = 11U;
    target.root_alias = "top";
    target.instance_path = "top.u";
    target.unit_identity = "sv:work.buf";
    target.cell_type = "BUF";
    target.language = language;
    app::SdfResolvedCell resolved_cell;
    resolved_cell.cell_id = 1U;
    resolved_cell.source_identity = "cell-source";
    resolved_cell.targets.push_back(std::move(target));
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { std::move(resolved_cell) },
        "cells-identity");

    std::vector<app::SdfResolvedNodeEndpoints> mappings;
    const auto add_mapping = [&](const std::uint64_t node_id) {
        app::SdfResolvedNodeEndpoints mapping;
        mapping.node_id = node_id;
        mapping.cell_id = 1U;
        mapping.construct_kind = frontend::SdfConstructKind::Iopath;
        mapping.target_instance_path = "top.u";
        mapping.endpoints = {
            endpoint(app::SdfEndpointRole::Input, 0U, "top.u.A"),
            endpoint(app::SdfEndpointRole::Output, 1U, "top.u.Z"),
        };
        mapping.specify_path = missing_path ? 99U : 0U;
        mappings.push_back(std::move(mapping));
    };
    add_mapping(1U);
    if (duplicate)
        add_mapping(3U);
    auto resolution = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::move(mappings), "endpoint-identity");

    app::SdfAnnotationTargetSummary target_summary;
    target_summary.cell_id = 1U;
    target_summary.target_instance_path = "top.u";
    target_summary.language = language;
    target_summary.annotation_count = duplicate ? 2U : 1U;
    target_summary.endpoint_count = duplicate ? 4U : 2U;
    target_summary.signals = duplicate
        ? std::vector<runtime::simir::SignalId> { 0U, 1U, 0U, 1U }
        : std::vector<runtime::simir::SignalId> { 0U, 1U };
    auto summary = std::make_shared<const app::SdfAnnotationSummary>(resolution,
        std::vector<app::SdfAnnotationTargetSummary> {
            std::move(target_summary) },
        duplicate ? 2U : 1U, duplicate ? 4U : 2U, duplicate ? 2U : 1U,
        0U, 0U, "summary-identity");
    return { make_design(), std::move(summary) };
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfTargetPlanResult& result, const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error(
            "missing target-plan diagnostic " + std::string { code });
    return *found;
}

void test_atomic_path_plan()
{
    auto fixture = make_fixture();
    const auto result = fsim::app::build_sdf_annotation_plan(
        fixture.summary, fixture.design, fsim::app::SdfValuePolicy { });
    require(result.ok(), "valid target plan should publish atomically");
    require(result.plan->annotations().size() == 1U,
        "valid plan should contain one annotation");
    const auto& annotation = result.plan->annotations().front();
    require(annotation.target_kind
            == fsim::app::SdfTimingTargetKind::SpecifyPath,
        "IOPATH should own a specify-path target");
    require(annotation.target_identity == "sdf:iopath:top.u:A=>Z:path-0",
        "plan should retain the stable elaborated path identity");
    require(annotation.before_ticks
            == std::vector<std::uint64_t>({ 17U, 19U }),
        "plan should retain source delay values before publication");
    require(annotation.after_ticks == std::vector<std::uint64_t>({ 2'000U }),
        "plan should retain selected proposed delay values");
    require(annotation.endpoint_signals
            == std::vector<fsim::runtime::simir::SignalId>({ 0U, 1U }),
        "plan should retain stable deduplicated endpoint IDs");
    require(result.plan->semantic_identity().find("sdf-annotation-plan-v3")
            == 0U,
        "plan should publish a versioned semantic identity");
    require(fixture.design.verilog_specify_paths().front().delays
            == std::vector<fsim::runtime::SimulationTick>({ 17U, 19U }),
        "planning must not mutate elaborated timing objects");
}

void test_missing_duplicate_and_language_rejection()
{
    auto fixture = make_fixture(false, true);
    auto result = fsim::app::build_sdf_annotation_plan(
        fixture.summary, fixture.design, fsim::app::SdfValuePolicy { });
    require(!result.ok() && result.plan == nullptr,
        "missing path must prevent partial plan publication");
    require_diagnostic(result, "FSIM-SDF-PLAN-002");

    fixture = make_fixture(true);
    result = fsim::app::build_sdf_annotation_plan(
        fixture.summary, fixture.design, fsim::app::SdfValuePolicy { });
    require(!result.ok() && result.plan == nullptr,
        "duplicate ownership must prevent partial plan publication");
    require_diagnostic(result, "FSIM-SDF-PLAN-003");

    fixture = make_fixture(false, false,
        fsim::app::SdfScopeRootLanguage::Vhdl);
    result = fsim::app::build_sdf_annotation_plan(
        fixture.summary, fixture.design, fsim::app::SdfValuePolicy { });
    require(!result.ok() && result.plan == nullptr,
        "VHDL targets must remain outside Batch 169 plans");
    require_diagnostic(result, "FSIM-SDF-PLAN-001");
}

void test_policy_and_resource_rejection()
{
    auto fixture = make_fixture();
    auto policy = fsim::app::SdfValuePolicy { };
    policy.simulation_precision_femtoseconds = 0U;
    auto result = fsim::app::build_sdf_annotation_plan(
        fixture.summary, fixture.design, policy);
    require(!result.ok() && result.plan == nullptr,
        "invalid value policy must prevent plan publication");
    require_diagnostic(result, "FSIM-SDF-VALUE-001");

    auto limits = fsim::app::SdfTargetPlanLimits { };
    limits.max_identity_bytes = 8U;
    result = fsim::app::build_sdf_annotation_plan(
        fixture.summary, fixture.design, fsim::app::SdfValuePolicy { }, limits);
    require(!result.ok() && result.plan == nullptr,
        "identity resource limit must prevent plan publication");
    require_diagnostic(result, "FSIM-SDF-PLAN-004");
}
} // namespace

int main()
{
    try {
        test_atomic_path_plan();
        test_missing_duplicate_and_language_rejection();
        test_policy_and_resource_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
