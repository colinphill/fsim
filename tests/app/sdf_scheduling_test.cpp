// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/app/sdf_precedence.hpp"
#include "fsim/app/sdf_scheduling.hpp"
#include "fsim/diagnostic/diagnostic.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
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
    result.source_name = "scheduling.sdf";
    result.begin = { offset, 2U, 1U };
    result.end = { offset + 8U, 2U, 9U };
    return result;
}

fsim::elaboration::ElaboratedDesign make_runtime_design()
{
    using namespace fsim;
    using namespace runtime::simir;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.u.A", "top.u.Z" }) {
        const auto id = static_cast<SignalId>(state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "logic";
        info.source_domain = frontend::ValueDomain::Logic4;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(
            std::string { name },
            runtime::PackedLogic4::from_msb_string("0"));
        state.signal_names.emplace_back(name, id);
    }
    Process stimulus;
    stimulus.id = 0U;
    stimulus.name = "sdf-scheduling-stimulus";
    stimulus.register_count = 2U;
    stimulus.driver_regions = { { 0U, 0U, 1U, true } };
    stimulus.operations = {
        LoadConstant { 0U, runtime::PackedLogic4::from_msb_string("1") },
        WriteUpdate { 0U, 0U },
        WaitFor { 1U },
        LoadConstant { 1U, runtime::PackedLogic4::from_msb_string("0") },
        WriteUpdate { 0U, 1U },
        WaitFor { 10U },
        Halt { }
    };
    state.processes.push_back(std::move(stimulus));
    Process driver;
    driver.id = 1U;
    driver.name = "sdf-scheduling-driver";
    driver.register_count = 1U;
    driver.static_sensitivity = { { 0U, EdgeKind::any } };
    driver.driver_regions = { { 1U, 0U, 1U, true } };
    driver.operations = { ReadSignal { 0U, 0U }, WriteUpdate { 1U, 0U },
        WaitSensitivity { }, Jump { 0U } };
    state.processes.push_back(std::move(driver));

    elaboration::VerilogSpecifyPathInfo path;
    path.id = 0U;
    path.identity = path_identity;
    path.instance = "top.u";
    path.sources = { { 0U, 0U, 1U } };
    path.destinations = { { 1U, 0U, 1U } };
    path.drivers = { 1U };
    path.delays = { 5U };
    path.source = span(10U);
    state.verilog_specify_paths.push_back(std::move(path));
    ModuleTimingCheck check;
    check.id = 0U;
    check.identity = check_identity;
    check.kind = ModuleTimingCheckKind::setup;
    check.reference.terminal = { 0U, 0U, 1U };
    check.data = ModuleTimingEvent { };
    check.data->terminal = { 1U, 0U, 1U };
    check.limits = { 5 };
    state.verilog_timing_checks.push_back(std::move(check));
    auto design
        = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "scheduling design fixture must validate");
    return std::move(*design);
}

fsim::app::SdfPlannedAnnotation path_annotation(
    const std::string_view identity, const std::vector<std::uint64_t>& before,
    const std::uint64_t after, const bool retain)
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 1U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::Iopath;
    annotation.target_kind = app::SdfTimingTargetKind::SpecifyPath;
    annotation.delay_mode = app::SdfDelayApplicationMode::Absolute;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = identity;
    annotation.before_ticks = before;
    annotation.after_ticks = { after };
    if (retain)
        annotation.retain_ticks = { 2U };
    annotation.source = span(100U);
    annotation.source_identity = "source-path";
    annotation.canonical_identity = "canonical-path";
    return annotation;
}

fsim::app::SdfPlannedAnnotation pulse_annotation(
    const std::string_view identity)
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 2U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::PathPulse;
    annotation.target_kind = app::SdfTimingTargetKind::Pulse;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = identity;
    annotation.after_ticks = { 1U, 2U };
    annotation.source = span(110U);
    annotation.source_identity = "source-pulse";
    annotation.canonical_identity = "canonical-pulse";
    return annotation;
}

fsim::app::SdfPlannedAnnotation check_annotation()
{
    using namespace fsim;
    app::SdfPlannedAnnotation annotation;
    annotation.node_id = 3U;
    annotation.cell_id = 1U;
    annotation.construct_kind = frontend::SdfConstructKind::Setup;
    annotation.target_kind = app::SdfTimingTargetKind::TimingCheck;
    annotation.target_instance_path = "top.u";
    annotation.target_identity = check_identity;
    annotation.before_check_ticks = { 5 };
    annotation.after_check_ticks = { 7 };
    annotation.source = span(120U);
    annotation.source_identity = "source-check";
    annotation.canonical_identity = "canonical-check";
    return annotation;
}

std::shared_ptr<const fsim::app::SdfAnnotationPlan> make_plan(
    std::vector<fsim::app::SdfPlannedAnnotation> annotations)
{
    using namespace fsim;
    std::vector<frontend::SdfIrNode> nodes;
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
        "scheduling.sdf", std::nullopt,
        std::vector<frontend::SdfIrCell> { std::move(cell) },
        std::move(nodes), "scheduling-ir");
    auto scope = std::make_shared<const app::SdfAnnotationScope>(ir,
        "scheduling.sdf", "source-semantic", std::nullopt, '.', "project",
        "design", std::vector<app::SdfAnnotationRoot> { }, "scope-identity");
    auto cells = std::make_shared<const app::SdfCellResolution>(scope,
        std::vector<app::SdfResolvedCell> { }, "cells-identity");
    auto endpoints = std::make_shared<const app::SdfEndpointResolution>(cells,
        std::vector<app::SdfResolvedNodeEndpoints> { },
        "endpoint-identity");
    auto summary = std::make_shared<const app::SdfAnnotationSummary>(endpoints,
        std::vector<app::SdfAnnotationTargetSummary> { }, annotations.size(),
        0U, annotations.size(), 0U, 0U, "summary-identity");
    return std::make_shared<const app::SdfAnnotationPlan>(summary,
        app::SdfValuePolicy { }, std::move(annotations), "scheduling-plan");
}

std::shared_ptr<const fsim::app::SdfPrecedenceApplication> make_precedence(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::string_view identity = path_identity,
    const bool include_pulse_check = true)
{
    const auto& source_path = design.verilog_specify_paths().front();
    std::vector<fsim::app::SdfPlannedAnnotation> annotations;
    annotations.push_back(
        path_annotation(identity, source_path.delays, 3U, include_pulse_check));
    if (include_pulse_check) {
        annotations.push_back(pulse_annotation(identity));
        annotations.push_back(check_annotation());
    }
    const auto result = fsim::app::apply_sdf_precedence(
        make_plan(std::move(annotations)), design);
    require(result.ok(), "scheduling precedence fixture must publish");
    return result.application;
}

const fsim::frontend::Diagnostic& require_diagnostic(
    const fsim::app::SdfSchedulingResult& result, const std::string_view code)
{
    const auto found = std::ranges::find(
        result.diagnostics, code, &fsim::frontend::Diagnostic::code);
    if (found == result.diagnostics.end())
        throw std::runtime_error(
            "missing scheduling diagnostic " + std::string { code });
    return *found;
}

void test_publication_cancellation_and_immutability()
{
    using namespace fsim;
    const auto design = make_runtime_design();
    const auto result
        = app::apply_sdf_scheduling(make_precedence(design), design);
    require(result.ok(), "complete SDF scheduling must publish");
    const auto& scheduled = result.application->design();
    const auto& path = scheduled.verilog_specify_paths().front();
    require(path.delays.size() == 12U
            && std::ranges::all_of(
                path.delays, [](const auto tick) { return tick == 3U; })
            && path.pulse_reject_delays == std::vector<std::uint64_t> { 1U }
            && path.pulse_error_delays == std::vector<std::uint64_t> { 2U }
            && path.retain_delays == std::vector<std::uint64_t> { 2U }
            && scheduled.verilog_timing_checks().front().limits
                == std::vector<std::int64_t> { 7 },
        "published runtime design must contain exact path/check/pulse values");
    require(design.verilog_specify_paths().front().delays
                == std::vector<std::uint64_t> { 5U }
            && design.verilog_timing_checks().front().limits
                == std::vector<std::int64_t> { 5 },
        "SDF scheduling must not mutate elaborated source timing");

    auto interpreter = scheduled.create_interpreter({ 1000U, 64U });
    std::vector<std::pair<runtime::SimulationTick, std::string>> changes;
    interpreter->set_signal_change_hook(
        [&](const runtime::simir::SignalId signal,
            const runtime::PackedLogic4& value,
            const runtime::SimulationTick time) {
            if (signal == 1U)
                changes.emplace_back(time, value.to_msb_string());
        });
    const auto run = interpreter->run();
    require(run.status == runtime::RunStatus::completed
            && changes == std::vector<std::pair<runtime::SimulationTick, std::string>> { { 2U, "X" }, { 4U, "0" } },
        "annotated inertial cancellation must retain onset/recovery timing");
}

void test_disabled_and_no_annotation_behavior()
{
    using namespace fsim;
    const auto design = make_runtime_design();
    auto source = app::apply_sdf_precedence(nullptr, design);
    require(source.ok(), "source-only precedence must publish");
    auto scheduled = app::apply_sdf_scheduling(source.application, design);
    require(scheduled.ok()
            && scheduled.application->design().verilog_specify_paths().front().delays
                == design.verilog_specify_paths().front().delays
            && scheduled.application->design().verilog_timing_checks().front().limits
                == design.verilog_timing_checks().front().limits,
        "no-annotation scheduling must retain source runtime values exactly");

    app::SdfTimingPrecedencePolicy policy;
    policy.specify_paths_enabled = false;
    policy.timing_checks_enabled = false;
    policy.pulse_rejection_enabled = false;
    source = app::apply_sdf_precedence(
        make_plan({ path_annotation(path_identity,
                        design.verilog_specify_paths().front().delays, 3U, true),
            pulse_annotation(path_identity), check_annotation() }),
        design, policy);
    scheduled = app::apply_sdf_scheduling(source.application, design);
    require(source.ok() && scheduled.ok()
            && scheduled.application->design().verilog_specify_paths().front().delays
                == std::vector<std::uint64_t> { 5U }
            && scheduled.application->design().verilog_timing_checks().front().limits
                == std::vector<std::int64_t> { 5 },
        "disabled scheduling domains must leave source runtime state untouched");
}

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct EngineCapture {
    std::vector<std::tuple<
        fsim::runtime::SimulationTick, std::string, std::string>>
        changes;
    std::size_t compiled_processes { };
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sdf-scheduling-differential";
    config.project.time_resolution = "1ns";
    config.project.tops = { { "sv:work.top", "top" } };
    config.build.optimization = fsim::project::Optimization::o2;
    config.build.cache_path = directory / "cache";
    config.run.max_deltas = 1000U;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    return config;
}

EngineCapture run_engine(
    fsim::app::BuiltProject project, const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation { std::move(project), 1000U, engine };
    const auto first = simulation.find_signal("top.z0");
    const auto second = simulation.find_signal("top.z1");
    require(first && second, "compiled differential outputs must resolve");
    EngineCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t) {
            if (signal == *first)
                capture.changes.emplace_back(
                    time, "top.z0", value.to_msb_string());
            if (signal == *second)
                capture.changes.emplace_back(
                    time, "top.z1", value.to_msb_string());
        });
    const auto result = simulation.run();
    require(result.status == fsim::runtime::RunStatus::stopped,
        "compiled differential simulation must finish by design");
    return capture;
}

void test_interpreter_llvm_differential()
{
    using namespace fsim;
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-sdf-scheduling-" + unique)
    };
    std::filesystem::create_directories(directory.path);
    const auto source_path = directory.path / "top.sv";
    {
        std::ofstream source(source_path);
        source << R"(module delay_buf(input logic a, output wire z);
  assign z = a;
  specify
    (a => z) = 5;
  endspecify
endmodule

module top;
  logic a;
  wire z0;
  wire z1;
  delay_buf u0(.a(a), .z(z0));
  delay_buf u1(.a(a), .z(z1));
  initial begin
    a = 0;
    #10 a = 1;
    #20 a = 0;
    #20 $finish;
  end
endmodule
)";
    }
    diagnostic::Engine diagnostics;
    auto project = app::build_project(
        make_config(directory.path, source_path), diagnostics);
    if (!project)
        diagnostic::print_text(std::cerr, diagnostics);
    require(project.has_value()
            && project->design.verilog_specify_paths().size() == 2U,
        "compiled differential project must elaborate two specify paths");
    std::vector<app::SdfPlannedAnnotation> annotations;
    std::uint64_t node_id = 1U;
    for (const auto& path : project->design.verilog_specify_paths()) {
        auto annotation
            = path_annotation(path.identity, path.delays, 3U, false);
        annotation.node_id = node_id;
        annotation.source_identity += "-" + std::to_string(node_id);
        annotation.canonical_identity += "-" + std::to_string(node_id);
        annotations.push_back(std::move(annotation));
        ++node_id;
    }
    const auto precedence_result = app::apply_sdf_precedence(
        make_plan(std::move(annotations)), project->design);
    require(precedence_result.ok(),
        "compiled differential precedence must publish");
    const auto scheduled
        = app::apply_sdf_scheduling(precedence_result.application,
            project->design);
    require(scheduled.ok(), "compiled differential timing must publish");
    project->design = scheduled.application->design();
    auto compiled_project = *project;
    const auto reference
        = run_engine(std::move(*project), app::SimulationEngine::interpreter);
    const auto compiled
        = run_engine(std::move(compiled_project), app::SimulationEngine::compiled);
    require(compiled.compiled_processes > 0U
            && compiled.changes == reference.changes
            && std::ranges::any_of(reference.changes, [](const auto& change) {
                   return std::get<0>(change) == 13U
                       && std::get<2>(change) == "1";
               })
            && std::ranges::any_of(reference.changes, [](const auto& change) {
                   return std::get<0>(change) == 33U
                       && std::get<2>(change) == "0";
               })
            && std::ranges::count_if(reference.changes, [](const auto& change) {
                   return std::get<0>(change) == 13U;
               })
                == 2U,
        "LLVM and interpreter must share exact multi-producer annotated ordering");
}

void test_atomic_rejection()
{
    using namespace fsim;
    const auto design = make_runtime_design();
    auto result = app::apply_sdf_scheduling(nullptr, design);
    require(!result.ok(), "missing precedence must reject");
    require_diagnostic(result, "FSIM-SDF-SCHEDULING-001");

    const auto precedence = make_precedence(design);
    app::SdfSchedulingLimits limits;
    limits.max_targets = 1U;
    result = app::apply_sdf_scheduling(precedence, design, limits);
    require(!result.ok(), "target resource limit must reject atomically");
    require_diagnostic(result, "FSIM-SDF-SCHEDULING-004");

    auto state = design.state();
    state.verilog_specify_paths.front().delays = { 9U };
    const auto stale = elaboration::ElaboratedDesign::from_state(
        std::move(state));
    require(stale.has_value(), "stale scheduling fixture must remain valid");
    result = app::apply_sdf_scheduling(precedence, *stale);
    require(!result.ok(), "stale runtime source delay must reject atomically");
    require_diagnostic(result, "FSIM-SDF-SCHEDULING-003");

    auto values = std::vector<app::SdfEffectiveTimingValue> {
        precedence->values().begin(), precedence->values().end()
    };
    values.front().value_index = 1U;
    auto malformed = std::make_shared<const app::SdfPrecedenceApplication>(
        precedence->plan(), precedence->policy(), std::move(values),
        "malformed-precedence");
    result = app::apply_sdf_scheduling(std::move(malformed), design);
    require(!result.ok(), "noncontiguous effective values must reject");
    require_diagnostic(result, "FSIM-SDF-SCHEDULING-002");

    limits = { };
    limits.max_identity_bytes = 1U;
    result = app::apply_sdf_scheduling(precedence, design, limits);
    require(!result.ok(), "identity resource limit must reject atomically");
    require_diagnostic(result, "FSIM-SDF-SCHEDULING-004");
}
} // namespace

int main()
{
    try {
        test_publication_cancellation_and_immutability();
        test_disabled_and_no_annotation_behavior();
        test_interpreter_llvm_differential();
        test_atomic_rejection();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    return 0;
}
