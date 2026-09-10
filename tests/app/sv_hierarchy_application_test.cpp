// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct Capture {
    fsim::runtime::RunResult result;
    std::vector<std::string> values;
    std::vector<std::pair<std::string, std::string>> libraries;
    std::size_t compiled_processes { };
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
}

void write_source(
    const std::filesystem::path& path,
    const std::string_view contents)
{
    std::ofstream output(path);
    output << contents;
    assert(output.good());
}

void add_source(
    fsim::project::Config& config,
    const std::string_view library,
    const std::filesystem::path& source,
    const std::string_view standard = "2017")
{
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = standard;
    sources.library = library;
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
}

Capture run(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    const std::array paths {
        "configured.left.value",
        "configured.right.value",
        "configured.lanes[0].generated.value",
        "configured.lanes[1].generated.value",
        "configured.left.all_leaf_monitor.hit",
        "configured.right.all_leaf_monitor.hit",
        "configured.lanes[0].generated.all_leaf_monitor.hit",
        "configured.lanes[1].generated.all_leaf_monitor.hit",
        "configured.lanes[1].generated.selected_monitor.hit"
    };
    std::array<fsim::runtime::simir::SignalId, paths.size()> signals { };
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto signal = project.design.find_signal(paths[index]);
        if (!signal) {
            std::cerr << "missing hierarchy signal " << paths[index] << '\n';
        }
        assert(signal);
        signals[index] = *signal;
    }

    Capture capture;
    for (const auto& specialization : project.design.specializations()) {
        if (specialization.instance == "configured.left"
            || specialization.instance == "configured.right"
            || specialization.instance
                == "configured.lanes[0].generated"
            || specialization.instance
                == "configured.lanes[1].generated") {
            capture.libraries.emplace_back(
                specialization.instance, specialization.library);
        }
    }
    std::ranges::sort(capture.libraries);
    fsim::app::Simulation simulation(std::move(project), 1000, engine);
    capture.compiled_processes = simulation.compiled_process_count();
    capture.result = simulation.run();
    for (const auto signal : signals) {
        capture.values.push_back(
            simulation.read_signal(signal).to_msb_string());
    }
    return capture;
}

void test_revision_modes(const std::filesystem::path& directory)
{
    const auto source = directory / "hierarchy-revision-modes.v";
    write_source(
        source,
        R"(
module revision_top;
  initial begin
    #1;
    $finish;
  end
endmodule

config revision_cfg;
  design work.revision_top;
endconfig
)");

    fsim::project::Config legal;
    legal.base_directory = directory;
    legal.project.name = "sv-hierarchy-revision-modes";
    legal.project.top = "sv:work.revision_cfg";
    legal.project.time_resolution = "1ns";
    legal.build.optimization = fsim::project::Optimization::o0;
    legal.build.cache_path = directory / "hierarchy-revision-cache";
    legal.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::verilog;
    sources.standard = "2001";
    sources.library = "work";
    sources.files.push_back(source);
    legal.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(legal, diagnostics);
    auto compiled_project = fsim::app::build_project(legal, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project && compiled_project);
    fsim::app::Simulation reference(
        std::move(*reference_project), 1000,
        fsim::app::SimulationEngine::interpreter);
    fsim::app::Simulation compiled(
        std::move(*compiled_project), 1000,
        fsim::app::SimulationEngine::compiled);
    const auto reference_result = reference.run();
    const auto compiled_result = compiled.run();
    assert(reference_result.status == fsim::runtime::RunStatus::stopped);
    assert(reference_result.status == compiled_result.status);
    assert(reference_result.time == compiled_result.time);

    auto disabled = legal;
    disabled.project.name = "sv-hierarchy-revision-noconfig";
    disabled.source_sets.front().standard = "2001-noconfig";
    fsim::diagnostic::Engine disabled_diagnostics;
    assert(!fsim::app::check_project(disabled, disabled_diagnostics));
    assert(std::ranges::any_of(
        disabled_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-PARSE-348";
        }));
}

void test_hierarchy(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization)
{
    const auto work_source = directory / "hierarchy-work.sv";
    const auto fast_source = directory / "hierarchy-fast.sv";
    const auto slow_source = directory / "hierarchy-slow.sv";
    write_source(
        work_source,
        R"(
extern module monitor #(
  parameter logic [136:0] MAGIC = 137'h1
) (
  input logic [136:0] value
);

module monitor #(
  parameter logic [136:0] MAGIC = 137'h1
) (
  input logic [136:0] value
);
  logic hit;
  initial begin
    #1;
    hit = value === MAGIC;
  end
endmodule

module configured_top;
  logic [136:0] left_value;
  logic [136:0] right_value;
  leaf left(.value(left_value));
  leaf right(.value(right_value));
  defparam right.P =
    137'h1_0000_0000_0000_0000_0000_0000_0000_0001;

  for (genvar index = 0; index < 2; ++index) begin : lanes
    leaf generated();
  end
  body_parameter_target #(.BASE(3)) body_parameters();

  bind configured_top.left monitor #(.MAGIC(P))
    all_leaf_monitor(.value(value));
  bind configured_top.right monitor #(.MAGIC(P))
    all_leaf_monitor(.value(value));
  bind configured_top.lanes[0].generated monitor #(.MAGIC(P))
    all_leaf_monitor(.value(value));
  bind configured_top.lanes[1].generated monitor #(.MAGIC(P))
    all_leaf_monitor(.value(value));
  bind configured_top.lanes[1].generated monitor #(.MAGIC(P))
    selected_monitor(.value(value));

  initial begin
    #2;
    $finish;
  end
endmodule

module body_parameter_target #(parameter int BASE = 1);
  parameter int DERIVED = BASE + 1;
endmodule

module invalid_body_parameter_override;
  body_parameter_target #(.DERIVED(9)) rejected();
endmodule

config configured;
  design work.configured_top;
  instance configured_top.left use fast.leaf;
  instance configured_top.lanes[1].generated use fast.leaf;
  cell leaf liblist slow;
endconfig : configured
)");
    write_source(
        fast_source,
        R"(
module leaf #(
  parameter logic [136:0] P = 137'h11
) (
  output logic [136:0] value
);
  parameter logic [136:0] DERIVED = P;
  assign value = DERIVED;
endmodule
)");
    write_source(
        slow_source,
        R"(
module leaf #(
  parameter logic [136:0] P = 137'h22
) (
  output logic [136:0] value
);
  parameter logic [136:0] DERIVED = P;
  assign value = DERIVED;
endmodule
)");

    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sv-hierarchy-application-test";
    config.project.top = "sv:work.configured";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "hierarchy-cache-o0"
                : "hierarchy-cache-o2");
    config.run.max_deltas = 1000;
    add_source(config, "work", work_source);
    add_source(config, "fast", fast_source);
    add_source(config, "slow", slow_source);

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project && compiled_project);
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = run(
        std::move(*compiled_project), fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert(reference.libraries == compiled.libraries);
    assert((reference.libraries
        == std::vector<std::pair<std::string, std::string>> {
            { "configured.lanes[0].generated", "slow" },
            { "configured.lanes[1].generated", "fast" },
            { "configured.left", "fast" },
            { "configured.right", "slow" } }));
    assert(reference.values.size() == 9U);
    assert(reference.values[0].size() == 137U);
    assert(reference.values[0].ends_with("10001"));
    assert(reference.values[1].size() == 137U);
    assert(std::ranges::count(reference.values[1], '1') == 2);
    assert(reference.values[2].ends_with("100010"));
    assert(reference.values[3] == reference.values[0]);
    for (std::size_t index = 4; index < reference.values.size(); ++index) {
        assert(reference.values[index] == "1");
    }
    assert(reference.compiled_processes == 0U);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes >= 6U);
#else
    assert(compiled.compiled_processes == 0U);
#endif

    auto invalid_config = config;
    invalid_config.project.name = "sv-hierarchy-body-parameter-negative";
    invalid_config.project.top = "sv:work.invalid_body_parameter_override";
    invalid_config.build.cache_path = directory / "hierarchy-invalid-cache";
    fsim::diagnostic::Engine invalid_diagnostics;
    assert(!fsim::app::build_project(invalid_config, invalid_diagnostics));
    assert(std::ranges::any_of(
        invalid_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-PARAM-001"
                && diagnostic.message.find("DERIVED") != std::string::npos;
        }));
}

void test_systemverilog_2023_bind_mapping(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization)
{
    const auto work_source = directory / "bind-mapping-work.sv";
    const auto fast_source = directory / "bind-mapping-fast.sv";
    const auto slow_source = directory / "bind-mapping-slow.sv";
    const auto mapped_source = directory / "bind-mapping-mapped.sv";
    const auto poison_source = directory / "bind-mapping-poison.sv";
    write_source(
        work_source,
        R"(module monitor;
  logic selected;
  initial selected = 1'b0;
endmodule
module top;
  leaf fast();
  leaf slow();
  bind leaf monitor bound_probe();
  initial begin
    #1;
    $finish;
  end
endmodule
config selected;
  design work.top;
  instance top.fast use fast.leaf;
  cell leaf liblist slow;
  cell monitor liblist mapped;
  instance top.slow.bound_probe use poison.monitor;
endconfig
)" );
    write_source(fast_source, "module leaf; endmodule\n");
    write_source(slow_source, "module leaf; endmodule\n");
    write_source(
        mapped_source,
        "module monitor; logic selected; initial selected = 1'b1; endmodule\n");
    write_source(
        poison_source,
        "module monitor; logic selected; initial selected = 1'b0; endmodule\n");

    fsim::project::Config config;
    config.base_directory = directory;
    const std::string optimization_name =
        optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
    config.project.name = "sv-2023-bind-mapping-" + optimization_name;
    config.project.top = "sv:work.selected";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / ("bind-mapping-cache-" + optimization_name);
    config.run.max_deltas = 1000;
    add_source(config, "work", work_source, "2023");
    add_source(config, "fast", fast_source, "2023");
    add_source(config, "slow", slow_source, "2023");
    add_source(config, "mapped", mapped_source, "2023");
    add_source(config, "poison", poison_source, "2023");

    const auto execute = [&](fsim::app::BuiltProject project,
                             const fsim::app::SimulationEngine engine) {
        assert(!project.design.find_signal(
            "selected.fast.bound_probe.selected"));
        const auto selected = project.design.find_signal(
            "selected.slow.bound_probe.selected");
        assert(selected);
        const auto specialization = std::ranges::find_if(
            project.design.specializations(), [](const auto& candidate) {
                return candidate.instance == "selected.slow.bound_probe";
            });
        assert(specialization != project.design.specializations().end());
        assert(specialization->library == "mapped");
        fsim::app::Simulation simulation(
            std::move(project), 1000, engine);
        const auto compiled_processes = simulation.compiled_process_count();
        const auto result = simulation.run();
        return std::tuple {
            result,
            simulation.read_signal(*selected).to_msb_string(),
            compiled_processes
        };
    };

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    const auto reference = execute(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);

    auto compiled_project = fsim::app::build_project(config, diagnostics);
    assert(compiled_project);
    const auto compiled = execute(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled);
    auto warm_project = fsim::app::build_project(config, diagnostics);
    assert(warm_project);
    const auto warm = execute(
        std::move(*warm_project),
        fsim::app::SimulationEngine::compiled);
    assert(std::get<0>(reference).status == fsim::runtime::RunStatus::stopped);
    assert(std::get<0>(reference).status == std::get<0>(compiled).status);
    assert(std::get<0>(reference).time == std::get<0>(compiled).time);
    assert(std::get<0>(reference).delta == std::get<0>(compiled).delta);
    assert(std::get<1>(reference) == "1");
    assert(std::get<1>(reference) == std::get<1>(compiled));
    assert(std::get<0>(compiled).status == std::get<0>(warm).status);
    assert(std::get<0>(compiled).time == std::get<0>(warm).time);
    assert(std::get<0>(compiled).delta == std::get<0>(warm).delta);
    assert(std::get<1>(compiled) == std::get<1>(warm));
#if defined(FSIM_HAS_LLVM)
    assert(std::get<2>(reference) == 0U);
    assert(std::get<2>(compiled) >= 2U);
    assert(std::get<2>(warm) == std::get<2>(compiled));
#else
    assert(std::get<2>(reference) == 0U);
    assert(std::get<2>(compiled) == 0U);
    assert(std::get<2>(warm) == 0U);
#endif

    auto retained = config;
    retained.project.name =
        "sv-2017-retained-bind-mapping-" + optimization_name;
    retained.build.cache_path = directory
        / ("bind-mapping-cache-2017-" + optimization_name);
    for (auto& sources : retained.source_sets) {
        sources.standard = "2017";
    }
    fsim::diagnostic::Engine retained_diagnostics;
    const auto retained_project =
        fsim::app::build_project(retained, retained_diagnostics);
    if (!retained_project) {
        print_diagnostics(retained_diagnostics);
    }
    assert(retained_project && !retained_diagnostics.has_error());
    assert(retained_project->design.find_signal(
        "selected.fast.bound_probe.selected"));
    assert(retained_project->design.find_signal(
        "selected.slow.bound_probe.selected"));
    const auto retained_fast = std::ranges::find_if(
        retained_project->design.specializations(), [](const auto& candidate) {
            return candidate.instance == "selected.fast.bound_probe";
        });
    const auto retained_slow = std::ranges::find_if(
        retained_project->design.specializations(), [](const auto& candidate) {
            return candidate.instance == "selected.slow.bound_probe";
        });
    assert(
        retained_fast != retained_project->design.specializations().end()
        && retained_fast->library == "mapped");
    assert(
        retained_slow != retained_project->design.specializations().end()
        && retained_slow->library == "poison");
}

void test_systemverilog_2023_nested_configuration(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization)
{
    const auto work_source = directory / "nested-configuration-work.sv";
    const auto fast_source = directory / "nested-configuration-fast.sv";
    const auto mapped_source = directory / "nested-configuration-mapped.sv";
    write_source(
        work_source,
        R"(module top;
  wrapper selected();
  initial begin
    #1;
    $finish;
  end
endmodule
config nested;
  design fast.wrapper;
  instance wrapper.payload use mapped.leaf;
endconfig
config selected;
  design work.top;
  instance top.selected use work.nested : config;
endconfig
)" );
    write_source(
        fast_source,
        "module wrapper; leaf payload(); endmodule\n");
    write_source(
        mapped_source,
        "module leaf; logic value; initial value = 1'b1; endmodule\n");

    fsim::project::Config config;
    config.base_directory = directory;
    const std::string optimization_name =
        optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
    config.project.name = "sv-2023-nested-configuration-"
        + optimization_name;
    config.project.top = "sv:work.selected";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / ("nested-configuration-cache-" + optimization_name);
    config.run.max_deltas = 1000;
    add_source(config, "work", work_source, "2023");
    add_source(config, "fast", fast_source, "2023");
    add_source(config, "mapped", mapped_source, "2023");

    const auto execute = [](fsim::app::BuiltProject project,
                            const fsim::app::SimulationEngine engine) {
        const auto value = project.design.find_signal(
            "selected.selected.payload.value");
        assert(value);
        const auto wrapper = std::ranges::find_if(
            project.design.specializations(), [](const auto& candidate) {
                return candidate.instance == "selected.selected";
            });
        const auto leaf = std::ranges::find_if(
            project.design.specializations(), [](const auto& candidate) {
                return candidate.instance == "selected.selected.payload";
            });
        assert(wrapper != project.design.specializations().end());
        assert(leaf != project.design.specializations().end());
        assert(wrapper->library == "fast");
        assert(leaf->library == "mapped");
        fsim::app::Simulation simulation(std::move(project), 1000, engine);
        const auto result = simulation.run();
        return std::pair {
            result,
            simulation.read_signal(*value).to_msb_string()
        };
    };

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project && !diagnostics.has_error());
    const auto reference = execute(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    assert(compiled_project && !diagnostics.has_error());
    const auto compiled = execute(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled);
    assert(reference.first.status == fsim::runtime::RunStatus::stopped);
    assert(reference.first.status == compiled.first.status);
    assert(reference.first.time == compiled.first.time);
    assert(reference.first.delta == compiled.first.delta);
    assert(reference.second == "1");
    assert(reference.second == compiled.second);

    auto missing = config;
    missing.project.name = "sv-2023-missing-nested-configuration-"
        + optimization_name;
    missing.project.top = "sv:work.missing";
    missing.build.cache_path = directory
        / ("missing-nested-configuration-cache-" + optimization_name);
    write_source(
        work_source,
        R"(module top;
  wrapper selected();
endmodule
config missing;
  design work.top;
  instance top.selected use work.unknown : config;
endconfig
)" );
    fsim::diagnostic::Engine missing_diagnostics;
    assert(!fsim::app::build_project(missing, missing_diagnostics));
    assert(std::ranges::any_of(
        missing_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVCONFIG-003"
                && diagnostic.message.find("missing.selected")
                    != std::string::npos;
        }));
}

} // namespace

int main()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-sv-hierarchy-application-test-" + std::to_string(suffix))
    };
    std::filesystem::create_directories(directory.path);
    test_revision_modes(directory.path);
    test_hierarchy(directory.path, fsim::project::Optimization::o0);
    test_hierarchy(directory.path, fsim::project::Optimization::o2);
    test_systemverilog_2023_bind_mapping(
        directory.path, fsim::project::Optimization::o0);
    test_systemverilog_2023_bind_mapping(
        directory.path, fsim::project::Optimization::o2);
    test_systemverilog_2023_nested_configuration(
        directory.path, fsim::project::Optimization::o0);
    test_systemverilog_2023_nested_configuration(
        directory.path, fsim::project::Optimization::o2);
    return 0;
}
