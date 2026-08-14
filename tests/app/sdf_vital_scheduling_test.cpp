// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/app/sdf_vital_scheduling.hpp"
#include "fsim/diagnostic/diagnostic.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <unordered_map>
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
    result.source_name = "vital-scheduling.sdf";
    result.begin = { offset, 1U, offset + 1U };
    result.end = { offset + 1U, 1U, offset + 2U };
    return result;
}

std::size_t delay_count(const fsim::runtime::simir::VitalDelayShape shape)
{
    using fsim::runtime::simir::VitalDelayShape;
    switch (shape) {
    case VitalDelayShape::single:
        return 1U;
    case VitalDelayShape::delay01:
        return 2U;
    case VitalDelayShape::delay01z:
        return 6U;
    }
    return 0U;
}

std::uint64_t constant_tick(const fsim::runtime::simir::Process& process,
    const fsim::runtime::simir::RegisterId target,
    const std::size_t instruction)
{
    std::unordered_map<fsim::runtime::simir::RegisterId,
        fsim::runtime::PackedLogic4>
        values;
    for (std::size_t index = 0; index < instruction; ++index) {
        const auto* load = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::LoadConstant>(&process.operations[index]);
        if (load != nullptr) {
            values[load->destination] = load->value;
            continue;
        }
        const auto* extract = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::Extract>(&process.operations[index]);
        if (extract == nullptr)
            continue;
        const auto source = values.find(extract->source);
        if (source == values.end())
            continue;
        fsim::runtime::PackedLogic4 value(
            extract->width, fsim::runtime::Logic4::zero);
        for (std::size_t bit = 0; bit < extract->width; ++bit)
            value.set(bit, source->second.get(extract->offset + bit));
        values[extract->destination] = std::move(value);
    }
    const auto found = values.find(target);
    if (found == values.end()) {
        throw std::runtime_error("fixture delay register "
            + std::to_string(target) + " has no retained definition before "
            + std::to_string(instruction) + " in " + process.name);
    }
    const auto word = found->second.low_word();
    require(word.bval == 0U, "fixture delay must be a known tick value");
    return word.aval;
}

std::shared_ptr<const fsim::app::SdfVitalModelPlan> make_model_plan(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::string& identity, const bool revision,
    const std::uint64_t replacement)
{
    using namespace fsim;
    using namespace app;
    std::vector<SdfVitalPathTimingRecord> paths;
    std::vector<SdfVitalModelRecord> models;
    std::uint64_t node_id = 1U;
    for (const auto& process : design.processes()) {
        for (std::size_t instruction = 0;
            instruction < process.operations.size(); ++instruction) {
            const auto* delay = runtime::simir::operation_get_if<
                runtime::simir::VitalDelay>(&process.operations[instruction]);
            if (delay == nullptr)
                continue;
            SdfVitalPathTimingRecord path;
            path.node_id = node_id;
            path.cell_id = node_id;
            path.construct_kind = frontend::SdfConstructKind::Iopath;
            path.annotation_mode = revision
                ? SdfDelayApplicationMode::Absolute
                : SdfDelayApplicationMode::None;
            path.instance_path = "vital_sched";
            path.call.process = process.id;
            path.call.instruction = static_cast<std::uint32_t>(instruction);
            path.call.kind = SdfVitalCallKind::Delay;
            path.call.source = delay->source_location;
            path.call.canonical_identity = "vital-call-"
                + std::to_string(process.id) + '-'
                + std::to_string(instruction);
            path.endpoint_signals = { delay->output };
            path.source = span(static_cast<std::size_t>(node_id) * 10U);
            path.source_identity = identity + "-source-"
                + std::to_string(node_id);
            for (std::size_t index = 0; index < delay_count(delay->shape);
                ++index) {
                const auto before = constant_tick(
                    process, delay->default_delays[index], instruction);
                path.before_delay_ticks.push_back(before);
                SdfSelectedDelay selected;
                selected.selection = SdfDelaySelection::Typical;
                selected.ticks = revision ? replacement + index : before;
                selected.canonical_identity = identity + "-value-"
                    + std::to_string(node_id) + '-'
                    + std::to_string(index);
                path.after_delays.push_back(std::move(selected));
            }
            path.canonical_identity
                = identity + "-path-" + std::to_string(node_id);
            SdfVitalModelRecord model;
            model.node_id = node_id;
            model.cell_id = node_id;
            model.instance_path = path.instance_path;
            model.owned_processes = { process.id };
            model.call = path.call;
            model.target_identity
                = identity + "-target-" + std::to_string(node_id);
            model.path_identity = path.canonical_identity;
            model.source = path.source;
            model.canonical_identity
                = identity + "-model-" + std::to_string(node_id);
            models.push_back(std::move(model));
            paths.push_back(std::move(path));
            ++node_id;
        }
    }
    require(!paths.empty(), "fixture must contain at least one VITAL delay");
    SdfValuePolicy policy;
    policy.selection = SdfDelaySelection::Typical;
    auto path_plan = std::make_shared<const SdfVitalPathTimingPlan>(nullptr,
        policy, std::move(paths), identity + "-paths");
    return std::make_shared<const SdfVitalModelPlan>(path_plan,
        std::vector<SdfVitalWrapperRegistration> { }, std::move(models),
        identity);
}

std::shared_ptr<const fsim::app::SdfVitalPrecedenceApplication>
make_precedence(const fsim::elaboration::ElaboratedDesign& design,
    const std::uint64_t replacement)
{
    const auto source = make_model_plan(design, "source", false, replacement);
    const auto revision
        = make_model_plan(design, "revision", true, replacement);
    const std::array revisions { revision };
    const auto result = fsim::app::apply_sdf_vital_precedence(
        source, revisions, { }, { }, { });
    require(result.ok(), "VITAL precedence fixture must publish");
    return result.application;
}

fsim::elaboration::ElaboratedDesign make_design()
{
    using namespace fsim;
    using namespace runtime::simir;
    elaboration::ElaboratedDesignState state;
    state.top = "top";
    state.roots = { "top" };
    for (const auto name : { "top.a", "top.z" }) {
        const auto id = static_cast<SignalId>(state.signal_info.size());
        elaboration::SignalInfo info;
        info.id = id;
        info.name = name;
        info.width = 1U;
        info.type_name = "std_ulogic";
        info.source_domain = frontend::ValueDomain::Logic9;
        state.signal_info.push_back(std::move(info));
        state.signals.emplace_back(
            std::string { name }, runtime::PackedLogic4::from_msb_string("0"));
        state.signal_names.emplace_back(name, id);
    }
    Process process;
    process.id = 0U;
    process.name = "top.vital-delay";
    process.language_standard = "vhdl-2008";
    process.register_count = 7U;
    process.register_value_kinds.assign(7U, ValueKind::logic4);
    process.register_value_kinds[0] = ValueKind::logic9;
    process.driver_regions = { { 1U, 0U, 1U, true } };
    process.operations.emplace_back(ReadSignal { 0U, 0U });
    const std::array<std::uint64_t, 6> delays { 3U, 5U, 0U, 0U, 0U, 0U };
    for (std::size_t index = 0; index < delays.size(); ++index) {
        process.operations.emplace_back(LoadConstant {
            static_cast<RegisterId>(index + 1U),
            runtime::PackedLogic4::from_aval_bval(
                64U, delays[index], 0U) });
    }
    VitalDelay delay;
    delay.kind = VitalDelayKind::path;
    delay.shape = VitalDelayShape::delay01;
    delay.output = 1U;
    delay.source = 0U;
    delay.default_delays = { 1U, 2U, 3U, 4U, 5U, 6U };
    delay.mode = VitalGlitchMode::inertial;
    delay.reject_fast_path = true;
    delay.negative_preemption = true;
    delay.source_location = { "fixture.vhd", 12U, 3U };
    process.operations.emplace_back(std::move(delay));
    process.operations.emplace_back(Halt { });
    state.processes.push_back(std::move(process));
    auto design
        = elaboration::ElaboratedDesign::from_state(std::move(state));
    require(design.has_value(), "VITAL scheduling design fixture must validate");
    return std::move(*design);
}

void require_diagnostic(const fsim::app::SdfVitalSchedulingResult& result,
    const std::string_view code)
{
    require(!result.ok()
            && std::ranges::any_of(result.diagnostics,
                [&](const auto& diagnostic) {
                    return diagnostic.code == code;
                }),
        "expected VITAL scheduling diagnostic was not emitted");
}

void test_publication_modes_and_atomic_rejection()
{
    using namespace fsim;
    const auto design = make_design();
    const auto precedence = make_precedence(design, 7U);
    const auto result
        = app::apply_sdf_vital_scheduling(precedence, design);
    require(result.ok() && result.application->delays().size() == 1U,
        "annotated VITAL scheduling must publish one call");
    const auto& delay = result.application->delays().front();
    require(delay.annotated
            && delay.mode == runtime::simir::VitalGlitchMode::inertial
            && delay.reject_fast_path && delay.negative_preemption
            && delay.source_delay_ticks
                == std::vector<std::uint64_t> { 3U, 5U }
            && delay.effective_delay_ticks
                == std::vector<std::uint64_t> { 7U, 8U },
        "scheduled call must retain exact mode, rejection and effective values");
    const auto& process = result.application->design().processes().front();
    require(constant_tick(process, 1U, 7U) == 7U
            && constant_tick(process, 2U, 7U) == 8U
            && constant_tick(design.processes().front(), 1U, 7U) == 3U,
        "publication must rewrite only the copied design atomically");

    auto corrupt_values = std::vector<app::SdfVitalEffectiveTimingValue> {
        precedence->values().begin(), precedence->values().end()
    };
    corrupt_values.front().effective_delay_ticks.reset();
    auto corrupt = std::make_shared<const app::SdfVitalPrecedenceApplication>(
        precedence->source(),
        std::vector<std::shared_ptr<const app::SdfVitalModelPlan>> {
            precedence->revisions().begin(), precedence->revisions().end() },
        std::vector<app::SdfVitalTimingGenericValue> { },
        std::vector<app::SdfVitalAnnotationControl> { }, precedence->policy(),
        std::move(corrupt_values), "corrupt-precedence");
    require_diagnostic(app::apply_sdf_vital_scheduling(corrupt, design),
        "FSIM-SDF-VITAL-SCHEDULING-003");
    require_diagnostic(app::apply_sdf_vital_scheduling(precedence, design,
                           { 1U, 1U, 1024U }),
        "FSIM-SDF-VITAL-SCHEDULING-006");
    require_diagnostic(app::apply_sdf_vital_scheduling(precedence, design,
                           { 0U, 1U, 1024U }),
        "FSIM-SDF-VITAL-SCHEDULING-001");
}

struct TemporaryDirectory {
    std::filesystem::path path;
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

fsim::project::Config make_config(const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vital-scheduling";
    config.project.top = "vhdl:work.vital_sched(rtl)";
    config.project.time_resolution = "1ns";
    config.build.jobs = 8;
    config.build.optimization = fsim::project::Optimization::o0;
    config.build.cache_path = directory / "cache";
    config.run.max_deltas = 1000U;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

struct EngineCapture {
    std::vector<std::tuple<std::uint64_t, std::string>> changes;
    std::size_t compiled_processes { };
};

EngineCapture run_engine(fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation { std::move(project), 1000U, engine };
    const auto signal = simulation.find_signal("vital_sched.delayed");
    require(signal.has_value(), "annotated VITAL output must resolve");
    EngineCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId changed,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time, const std::uint64_t) {
            if (changed == *signal)
                capture.changes.emplace_back(time, value.to_msb_string());
        });
    const auto result = simulation.run();
    require(result.status == fsim::runtime::RunStatus::completed
            || result.status == fsim::runtime::RunStatus::stopped,
        "annotated VITAL simulation must terminate cleanly");
    return capture;
}

void test_interpreter_llvm_logic9_differential()
{
    using namespace fsim;
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryDirectory directory { std::filesystem::temp_directory_path()
        / ("fsim-sdf-vital-scheduling-" + unique) };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "vital_sched.vhd";
    {
        std::ofstream output(source);
        output << R"(library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;

entity vital_sched is
end entity;

architecture rtl of vital_sched is
  signal input_value : std_ulogic;
  signal delayed : std_ulogic;
begin
  stimulus : process
  begin
    input_value <= '0';
    wait for 2 ns;
    input_value <= '1';
    wait for 20 ns;
    input_value <= 'Z';
    wait for 20 ns;
    wait;
  end process;

  delay_process : process(input_value)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay(
        OutSignal => delayed,
        GlitchData => glitch,
        OutSignalName => "delayed",
        OutTemp => input_value,
        Paths => (0 => (input_value'last_event, 3 ns, true)),
        DefaultDelay => 3 ns,
        Mode => VitalTransport,
        XOn => false);
  end process;
end architecture;
)";
        require(output.good(), "VITAL scheduling source must be retained");
    }
    diagnostic::Engine diagnostics;
    auto project = app::build_project(
        make_config(directory.path, source), diagnostics);
    if (!project)
        diagnostic::print_text(std::cerr, diagnostics);
    require(project.has_value(), "VITAL scheduling source must compile");
    const auto precedence = make_precedence(project->design, 7U);
    const auto scheduled = app::apply_sdf_vital_scheduling(
        precedence, project->design);
    require(scheduled.ok() && scheduled.application->delays().size() == 1U,
        "compiled VITAL delay annotation must publish");
    project->design = scheduled.application->design();
    auto compiled_project = *project;
    const auto reference
        = run_engine(std::move(*project), app::SimulationEngine::interpreter);
    const auto compiled = run_engine(
        std::move(compiled_project), app::SimulationEngine::compiled);
#if defined(FSIM_HAS_LLVM)
    const bool compiled_process_count_ok = compiled.compiled_processes > 0U;
#else
    const bool compiled_process_count_ok = compiled.compiled_processes == 0U;
#endif
    require(compiled_process_count_ok
            && compiled.changes == reference.changes
            && std::ranges::any_of(reference.changes, [](const auto& change) {
                   return std::get<0>(change) == 9U
                       && std::get<1>(change) == "1";
               })
            && std::ranges::any_of(reference.changes, [](const auto& change) {
                   return std::get<0>(change) == 29U
                       && std::get<1>(change) == "Z";
               }),
        "interpreter and LLVM must share annotated Logic9 transition timing");
}
} // namespace

int main()
{
    try {
        test_publication_modes_and_atomic_rejection();
        test_interpreter_llvm_logic9_differential();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
