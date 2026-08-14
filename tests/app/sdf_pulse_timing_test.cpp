// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_pulse_timing.hpp"

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
using fsim::runtime::SimulationTick;

void require(const bool condition, const std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string { message });
}

fsim::frontend::SourceSpan span(const std::size_t offset)
{
    fsim::frontend::SourceSpan result;
    result.source_name = "pulse.sdf";
    result.begin = { offset, 4U, offset + 1U };
    result.end = { offset + 1U, 4U, offset + 2U };
    return result;
}

fsim::frontend::SdfExactDecimal decimal(
    std::string coefficient, const std::int64_t exponent,
    const bool negative = false)
{
    fsim::frontend::SdfExactDecimal result;
    result.negative = negative;
    result.coefficient = std::move(coefficient);
    result.exponent10 = exponent;
    result.canonical = std::string { negative ? "-" : "" }
        + result.coefficient + 'e' + std::to_string(exponent);
    return result;
}

fsim::app::SdfSelectedPercentage percentage(
    std::string coefficient, const std::int64_t exponent)
{
    fsim::frontend::SdfExactValue value;
    value.kind = fsim::frontend::SdfExactValueKind::Scalar;
    value.components[0] = decimal(std::move(coefficient), exponent);
    const auto selected = fsim::app::select_sdf_percentage(
        value, fsim::app::SdfValuePolicy { });
    require(selected.ok(), "percentage fixture should select exactly");
    return *selected.selected;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    elaboration::SpecializationInfo root;
    root.id = 0U;
    root.unit = "sv:work.top";
    root.instance = "top";
    root.language = frontend::Language::SystemVerilog2017;
    state.specializations.push_back(std::move(root));
    elaboration::SpecializationInfo specialization;
    specialization.id = 1U;
    specialization.unit = "sv:work.BUF";
    specialization.instance = "top.u";
    specialization.language = frontend::Language::SystemVerilog2017;
    specialization.is_cell = true;
    state.specializations.push_back(std::move(specialization));
    for (const auto name : { "top.u.A", "top.u.Z", "top.u.B", "top.u.Y" }) {
        const auto id = static_cast<runtime::simir::SignalId>(
            state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        info.is_port = true;
        info.direction = std::string_view { name }.ends_with("A")
                || std::string_view { name }.ends_with("B")
            ? frontend::PortDirection::Input
            : frontend::PortDirection::Output;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(
            std::string { name }, runtime::PackedLogic4(1U));
        state.signal_names.emplace_back(name, id);
    }
    elaboration::VerilogSpecifyPathInfo first;
    first.id = 0U;
    first.identity = "sdf:iopath:top.u:A=>Z:path-0";
    first.instance = "top.u";
    first.sources.push_back({ 0U, 0U, 1U });
    first.destinations.push_back({ 1U, 0U, 1U });
    first.delays = { 10U, 20U, 30U };
    first.polarity = frontend::VerilogPathPolarity::Negative;
    first.pulse_style = frontend::VerilogPulseStyle::Ondetect;
    first.show_cancelled = true;
    first.source = span(10U);
    state.verilog_specify_paths.push_back(std::move(first));

    elaboration::VerilogSpecifyPathInfo second;
    second.id = 1U;
    second.identity = "sdf:iopath:top.u:B=>Y:path-1";
    second.instance = "top.u";
    second.sources.push_back({ 2U, 0U, 1U });
    second.destinations.push_back({ 3U, 0U, 1U });
    second.delays = { 5U, 7U };
    second.source = span(20U);
    state.verilog_specify_paths.push_back(std::move(second));
    auto design
        = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "pulse application design fixture must validate");
    return std::move(*design);
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> build_real_plan(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::string_view source)
{
    using namespace fsim;
    const auto parsed = frontend::parse_sdf(
        { "pulse-real.sdf", std::string { source } });
    if (!parsed.ok()) {
        for (const auto& diagnostic : parsed.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    require(parsed.ok(), "real pulse SDF fixture must parse");
    app::SdfAnnotationScopeRequest request;
    request.selection = app::SdfScopeSelection::All;
    request.expected_project_identity = "project:pulse";
    request.expected_design_identity = "design:pulse";
    const auto scope = app::bind_sdf_annotation_scope(parsed.file, design,
        "project:pulse", "design:pulse", request);
    if (!scope.ok()) {
        for (const auto& diagnostic : scope.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    require(scope.ok(), "real pulse scope must bind");
    const auto cells = app::resolve_sdf_cells(scope.scope, design);
    require(cells.ok(), "real pulse cells must resolve");
    const auto endpoints
        = app::resolve_sdf_endpoints(cells.resolution, design);
    if (!endpoints.ok()) {
        for (const auto& diagnostic : endpoints.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    require(endpoints.ok(), "real pulse endpoints must resolve");
    const auto summary
        = app::validate_sdf_mapping(endpoints.resolution, design);
    if (!summary.ok()) {
        for (const auto& mapping : endpoints.resolution->nodes()) {
            std::cerr << "mapping "
                      << static_cast<unsigned>(mapping.construct_kind) << ' '
                      << mapping.node_id << " endpoints="
                      << mapping.endpoints.size() << " path="
                      << (mapping.specify_path
                                 ? std::to_string(*mapping.specify_path)
                                 : std::string { "none" })
                      << '\n';
        }
        for (const auto& diagnostic : summary.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    require(summary.ok(), "real pulse mapping must validate");
    const auto plan = app::build_sdf_annotation_plan(
        summary.summary, design, app::SdfValuePolicy { });
    if (!plan.ok()) {
        for (const auto& diagnostic : plan.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    require(plan.ok(), "real pulse target plan must publish");
    return plan.plan;
}

struct Fixture {
    fsim::elaboration::ElaboratedDesign design;
    std::shared_ptr<const fsim::app::SdfAnnotationPlan> plan;
};

Fixture make_fixture(std::vector<fsim::app::SdfPlannedAnnotation> annotations,
    const fsim::frontend::SdfRevision revision
    = fsim::frontend::SdfRevision::Sdf40,
    const bool legacy_global = false)
{
    using namespace fsim;
    std::vector<frontend::SdfIrNode> nodes;
    const auto maximum_id = std::ranges::max(
        annotations, { }, &app::SdfPlannedAnnotation::node_id)
                                .node_id;
    nodes.reserve(static_cast<std::size_t>(maximum_id));
    for (std::uint64_t id = 1U; id <= maximum_id; ++id) {
        frontend::SdfIrNode node;
        node.id = id;
        node.cell_id = 1U;
        const auto annotation = std::ranges::find(
            annotations, id, &app::SdfPlannedAnnotation::node_id);
        if (annotation != annotations.end()) {
            node.kind = annotation->construct_kind;
            node.span = annotation->source;
            node.source_identity = annotation->source_identity;
            node.canonical_identity = annotation->canonical_identity;
            if (legacy_global
                && annotation->construct_kind
                    == frontend::SdfConstructKind::PathPulsePercent) {
                node.profile_identity = "sdf21:globalpathpulse";
            }
        }
        nodes.push_back(std::move(node));
    }
    frontend::SdfIrCell ir_cell;
    ir_cell.id = 1U;
    ir_cell.cell_type = "BUF";
    ir_cell.node_count = nodes.size();
    ir_cell.source_identity = "cell-source";
    ir_cell.canonical_identity = "cell-canonical";
    auto ir = std::make_shared<const frontend::SdfIr>(revision,
        revision == frontend::SdfRevision::Sdf21
            ? frontend::SdfRevisionAdapter::Sdf21
            : frontend::SdfRevisionAdapter::None,
        "pulse.sdf", std::nullopt,
        std::vector<frontend::SdfIrCell> { std::move(ir_cell) },
        std::move(nodes), "pulse-ir");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "pulse.sdf", "source-semantic", std::nullopt, '.', "project",
        "design", std::vector<app::SdfAnnotationRoot> { }, "scope-identity");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "cells-identity");
    auto resolution = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { }, "endpoint-identity");
    auto summary = std::make_shared<const app::SdfAnnotationSummary>(resolution,
        std::vector<app::SdfAnnotationTargetSummary> { }, annotations.size(),
        0U, annotations.size(), 0U, 0U, "summary-identity");
    auto plan = std::make_shared<const app::SdfAnnotationPlan>(summary,
        app::SdfValuePolicy { }, std::move(annotations), "plan-identity");
    return { make_design(), std::move(plan) };
}

fsim::app::SdfPlannedAnnotation path_annotation(const bool retain = true)
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 1U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::Iopath;
    annotation.target_kind = app::SdfTimingTargetKind::SpecifyPath;
    annotation.delay_mode = app::SdfDelayApplicationMode::Absolute;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = "sdf:iopath:top.u:A=>Z:path-0";
    annotation.before_ticks = { 10U, 20U, 30U };
    annotation.after_ticks = { 10U, 20U, 30U };
    if (retain)
        annotation.retain_ticks = { 6U, 7U, 8U };
    annotation.source = span(100U);
    annotation.source_identity = "source-iopath";
    annotation.canonical_identity = "canonical-iopath";
    return annotation;
}

fsim::app::SdfPlannedAnnotation absolute_pulse(
    const SimulationTick reject, const SimulationTick error,
    const std::uint64_t id = 2U)
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = id;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::PathPulse;
    annotation.target_kind = app::SdfTimingTargetKind::Pulse;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = "sdf:iopath:top.u:A=>Z:path-0";
    annotation.after_ticks = { reject, error };
    annotation.source = span(static_cast<std::size_t>(id) + 100U);
    annotation.source_identity = "source-pathpulse-" + std::to_string(id);
    annotation.canonical_identity
        = "canonical-pathpulse-" + std::to_string(id);
    return annotation;
}

fsim::app::SdfPlannedAnnotation global_percent()
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 2U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::PathPulsePercent;
    annotation.target_kind = app::SdfTimingTargetKind::Pulse;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = "sdf:globalpathpulse:top.u";
    annotation.after_percentages = {
        percentage("125", -1), percentage("50", 0)
    };
    annotation.source = span(102U);
    annotation.source_identity = "source-global-percent";
    annotation.canonical_identity = "canonical-global-percent";
    return annotation;
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfPulseTimingResult& result,
    const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error(
            "missing pulse diagnostic " + std::string { code });
    return *found;
}

void test_absolute_pulse_retain_and_source_preservation()
{
    auto fixture = make_fixture({ path_annotation(), absolute_pulse(2U, 8U) });
    const auto result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design);
    require(result.ok() && result.application->paths().size() == 1U,
        "targeted PATHPULSE and embedded RETAIN should publish atomically");
    const auto& path = result.application->paths().front();
    require(path.path_id == 0U && !path.global_annotation,
        "targeted pulse application should retain stable path identity");
    require(path.effective_reject_delays
                == std::vector<SimulationTick>({ 2U })
            && path.effective_error_delays
                == std::vector<SimulationTick>({ 8U })
            && path.effective_retain_delays
                == std::vector<SimulationTick>({ 6U, 7U, 8U }),
        "absolute thresholds and RETAIN transition table should remain exact");
    require(path.effective_path.polarity
                == fsim::frontend::VerilogPathPolarity::Negative
            && path.effective_path.pulse_style
                == fsim::frontend::VerilogPulseStyle::Ondetect
            && path.effective_path.show_cancelled,
        "pulse application must preserve polarity, pulse style, and cancellation identity");
    require(fixture.design.verilog_specify_paths().front().retain_delays.empty()
            && fixture.design.verilog_specify_paths()
                .front()
                .pulse_reject_delays.empty(),
        "pulse application must not mutate elaborated source timing");
    require(result.application->semantic_identity().find(
                "sdf-pulse-application-v1")
            == 0U,
        "pulse application should publish a versioned semantic identity");
}

void test_real_pipeline_planning()
{
    using namespace fsim;
    const auto design = make_design();
    const auto targeted = build_real_plan(design, R"(
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1ns)
  (CELL (CELLTYPE "BUF") (INSTANCE top.u)
    (DELAY
      (PATHPULSE A Z (2) (8))
      (ABSOLUTE
        (IOPATH A Z (RETAIN (6)) (10) (20) (30))))))
)");
    require(targeted->annotations().size() == 2U,
        "real pipeline should retain IOPATH/RETAIN and PATHPULSE annotations");
    const auto pulse = std::ranges::find(targeted->annotations(),
        app::SdfTimingTargetKind::Pulse,
        &app::SdfPlannedAnnotation::target_kind);
    const auto path = std::ranges::find(targeted->annotations(),
        app::SdfTimingTargetKind::SpecifyPath,
        &app::SdfPlannedAnnotation::target_kind);
    require(pulse != targeted->annotations().end()
            && pulse->target_identity
                == "sdf:iopath:top.u:A=>Z:path-0"
            && pulse->after_ticks
                == std::vector<std::uint64_t>({ 2'000U, 8'000U }),
        "real PATHPULSE should link its elaborated path and exact tick thresholds");
    require(path != targeted->annotations().end()
            && path->retain_ticks
                == std::vector<std::uint64_t>({ 6'000U })
            && path->after_ticks
                == std::vector<std::uint64_t>(
                    { 10'000U, 20'000U, 30'000U }),
        "real embedded RETAIN should remain separate from ordinary IOPATH delays");
    auto applied = app::apply_sdf_pulse_timing(targeted, design);
    require(applied.ok() && applied.application->paths().size() == 1U
            && applied.application->paths().front().effective_reject_delays
                == std::vector<SimulationTick>({ 2'000U })
            && applied.application->paths().front().effective_error_delays
                == std::vector<SimulationTick>({ 8'000U })
            && applied.application->paths().front().effective_retain_delays
                == std::vector<SimulationTick>({ 6'000U }),
        "real target planning should feed exact PATHPULSE and RETAIN application");

    const auto global = build_real_plan(design, R"(
(DELAYFILE
  (SDFVERSION "4.0")
  (TIMESCALE 1ns)
  (CELL (CELLTYPE "BUF") (INSTANCE top.u)
    (DELAY (PATHPULSEPERCENT (12.5) (50)))))
)");
    require(global->annotations().size() == 1U
            && global->annotations().front().target_identity
                == "sdf:globalpathpulse:top.u"
            && global->annotations().front().endpoints.empty()
            && global->annotations().front().after_percentages.size() == 2U,
        "real endpoint-free PATHPULSEPERCENT should retain exact global planning identity");
    applied = app::apply_sdf_pulse_timing(global, design);
    require(applied.ok() && applied.application->paths().size() == 2U
            && applied.application->paths().front().global_annotation,
        "real global percentage planning should expand atomically in application");

    const auto legacy = build_real_plan(design, R"(
(DELAYFILE
  (SDFVERSION "OVI 2.1")
  (TIMESCALE 1ns)
  (CELL (CELLTYPE "BUF") (INSTANCE top.u)
    (DELAY (GLOBALPATHPULSE A Z (10) (20)))))
)");
    require(legacy->annotations().size() == 1U
            && legacy->annotations().front().construct_kind
                == frontend::SdfConstructKind::PathPulsePercent
            && legacy->annotations().front().target_identity
                == "sdf:iopath:top.u:A=>Z:path-0",
        "real SDF 2.1 GLOBALPATHPULSE should retain revision profile and path linkage");
    applied = app::apply_sdf_pulse_timing(legacy, design);
    require(applied.ok() && applied.application->paths().size() == 1U
            && !applied.application->paths().front().global_annotation,
        "real SDF 2.1 GLOBALPATHPULSE endpoint form should apply to its linked path");
}

void test_exact_global_percentage_and_revision()
{
    auto fixture = make_fixture({ global_percent() });
    auto result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design);
    if (!result.ok()) {
        for (const auto& diagnostic : result.diagnostics)
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
    require(result.ok() && result.application->paths().size() == 2U,
        "endpoint-free PATHPULSEPERCENT should expand to every stable instance path");
    require(result.application->paths()[0].path_id == 0U
            && result.application->paths()[1].path_id == 1U,
        "global pulse expansion should retain stable path-id order");
    require(result.application->paths()[0].effective_reject_delays
                == std::vector<SimulationTick>({ 1U, 3U, 4U })
            && result.application->paths()[0].effective_error_delays
                == std::vector<SimulationTick>({ 5U, 10U, 15U }),
        "12.5 and 50 percent should scale each transition exactly with one half-up rounding boundary");
    require(result.application->paths()[1].effective_reject_delays
                == std::vector<SimulationTick>({ 1U, 1U })
            && result.application->paths()[1].effective_error_delays
                == std::vector<SimulationTick>({ 3U, 4U }),
        "global percentage scaling should use each path's own transition delays");

    fixture = make_fixture({ global_percent() },
        fsim::frontend::SdfRevision::Sdf40, true);
    result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design);
    require(!result.ok() && result.application == nullptr,
        "legacy GLOBALPATHPULSE profile must reject outside SDF 2.1");
    require_diagnostic(result, "FSIM-SDF-PULSE-001");

    fixture = make_fixture({ global_percent() },
        fsim::frontend::SdfRevision::Sdf21, true);
    result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design);
    require(result.ok(),
        "legacy GLOBALPATHPULSE should remain accepted in SDF 2.1");
}

void test_conflict_threshold_and_resource_rejection()
{
    auto fixture = make_fixture(
        { absolute_pulse(2U, 8U), absolute_pulse(3U, 9U, 3U) });
    auto result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design);
    require(!result.ok() && result.application == nullptr,
        "conflicting pulse ownership must prevent partial publication");
    require_diagnostic(result, "FSIM-SDF-PULSE-002");

    fixture = make_fixture({ absolute_pulse(9U, 8U) });
    result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design);
    require(!result.ok(), "reject thresholds above error thresholds must reject");
    require_diagnostic(result, "FSIM-SDF-PULSE-003");

    auto stale = absolute_pulse(2U, 8U);
    stale.target_identity = "sdf:iopath:top.u:missing";
    fixture = make_fixture({ std::move(stale) });
    result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design);
    require(!result.ok(), "stale pulse targets must reject atomically");
    require_diagnostic(result, "FSIM-SDF-PULSE-001");

    auto invalid_percentage = global_percent();
    invalid_percentage.after_percentages.front().exact_source_value
        = decimal("101", 0);
    fixture = make_fixture({ std::move(invalid_percentage) });
    result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design);
    require(!result.ok(),
        "forged or out-of-range selected percentages must reject atomically");
    require_diagnostic(result, "FSIM-SDF-PULSE-004");

    fixture = make_fixture({ global_percent() });
    auto limits = fsim::app::SdfPulseTimingLimits { };
    limits.max_paths = 1U;
    result = fsim::app::apply_sdf_pulse_timing(
        fixture.plan, fixture.design, limits);
    require(!result.ok(),
        "global expansion beyond the configured path limit must reject atomically");
    require_diagnostic(result, "FSIM-SDF-PULSE-004");
}

std::vector<std::pair<SimulationTick, std::string>> run_runtime_pulse(
    const SimulationTick width, const fsim::runtime::Logic4 pulse_value,
    const bool retain, const bool transition_tables = false,
    const bool ondetect = false)
{
    using namespace fsim::runtime;
    using namespace fsim::runtime::simir;
    Interpreter interpreter { { 1000U, 64U } };
    const auto source = interpreter.add_signal(
        { "pulse.source", PackedLogic4::from_msb_string("0") });
    const auto output = interpreter.add_signal(
        { "pulse.output", PackedLogic4::from_msb_string("0") });
    Process stimulus;
    stimulus.id = 0U;
    stimulus.name = "pulse-stimulus";
    stimulus.register_count = 2U;
    stimulus.driver_regions = { { source, 0U, 1U, true } };
    stimulus.operations = {
        LoadConstant { 0U, PackedLogic4 { 1U, pulse_value } },
        WriteUpdate { source, 0U },
        WaitFor { width },
        LoadConstant { 1U, PackedLogic4::from_msb_string("0") },
        WriteUpdate { source, 1U },
        WaitFor { 24U },
        Halt { }
    };
    (void)interpreter.add_process(std::move(stimulus));
    Process driver;
    driver.id = 1U;
    driver.name = "pulse-driver";
    driver.register_count = 1U;
    driver.static_sensitivity = { { source, EdgeKind::any } };
    driver.driver_regions = { { output, 0U, 1U, true } };
    driver.operations = {
        ReadSignal { 0U, source },
        WriteUpdate { output, 0U },
        WaitSensitivity { },
        Jump { 0U }
    };
    (void)interpreter.add_process(std::move(driver));
    ModulePath path;
    path.identity = "runtime:sdf-pulse:0";
    path.sources = { { source, 0U, 1U } };
    path.destinations = { { output, 0U, 1U } };
    path.drivers = { 1U };
    path.pulse_style = ondetect ? ModulePathPulseStyle::ondetect
                                : ModulePathPulseStyle::onevent;
    if (transition_tables) {
        path.delays = { 10U, 10U, 12U, 10U, 10U, 11U,
            10U, 10U, 10U, 10U, 10U, 10U };
        path.pulse_reject_delays = { 2U, 2U, 3U, 2U, 2U, 2U,
            2U, 2U, 2U, 2U, 2U, 2U };
        path.pulse_error_delays = { 8U, 8U, 9U, 8U, 8U, 8U,
            8U, 8U, 8U, 8U, 8U, 8U };
        if (retain) {
            path.retain_delays = { 6U, 6U, 7U, 6U, 6U, 6U,
                6U, 6U, 6U, 6U, 6U, 6U };
        }
    } else {
        path.delays = { 10U };
        path.pulse_reject_delays = { 2U };
        path.pulse_error_delays = { 8U };
        if (retain)
            path.retain_delays = { 6U };
    }
    (void)interpreter.add_module_path(std::move(path));
    std::vector<std::pair<SimulationTick, std::string>> changes;
    interpreter.set_signal_change_hook(
        [&](const SignalId signal, const PackedLogic4& value,
            const SimulationTick time) {
            if (signal == output)
                changes.emplace_back(time, value.to_msb_string());
        });
    interpreter.start();
    const auto run = interpreter.run();
    require(run.status == RunStatus::completed,
        "pulse runtime fixture should complete");
    return changes;
}

void test_runtime_boundaries_unknowns_and_retain()
{
    using fsim::runtime::Logic4;
    require(run_runtime_pulse(1U, Logic4::one, true).empty(),
        "pulse widths below the reject threshold should be cancelled");
    require(run_runtime_pulse(2U, Logic4::one, true)
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 6U, "X" }, { 12U, "0" } },
        "the inclusive reject boundary should corrupt while RETAIN holds the prior value");
    require(run_runtime_pulse(4U, Logic4::one, true)
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 6U, "X" }, { 14U, "0" } },
        "RETAIN should delay X publication without leaving stale recovery events");
    require(run_runtime_pulse(8U, Logic4::one, true).empty(),
        "the inclusive error boundary should not corrupt the pulse");
    require(run_runtime_pulse(0U, Logic4::one, true).empty(),
        "zero-width pulses should reject without a spurious output event");
    require(run_runtime_pulse(4U, Logic4::z, true)
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 6U, "X" }, { 14U, "0" } },
        "four-state Z transitions should retain pulse identity and recover deterministically");
    require(run_runtime_pulse(4U, Logic4::z, true, true)
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 7U, "X" }, { 15U, "0" } },
        "twelve-entry delay, threshold, and RETAIN tables should select exact Z transition identities");
    require(run_runtime_pulse(4U, Logic4::one, false)
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 10U, "X" }, { 14U, "0" } },
        "onevent corruption without RETAIN should publish X at the pending boundary");
    require(run_runtime_pulse(4U, Logic4::one, false, false, true)
            == std::vector<std::pair<SimulationTick, std::string>> {
                { 4U, "X" }, { 14U, "0" } },
        "ondetect corruption should publish X at cancellation detection and recover once");
}
} // namespace

int main()
{
    try {
        test_absolute_pulse_retain_and_source_preservation();
        test_real_pipeline_planning();
        test_exact_global_percentage_and_revision();
        test_conflict_threshold_and_resource_rejection();
        test_runtime_boundaries_unknowns_and_retain();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
