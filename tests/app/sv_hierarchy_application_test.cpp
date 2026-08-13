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
    const std::filesystem::path& source)
{
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
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

  bind leaf monitor #(.MAGIC(P))
    all_leaf_monitor(.value(value));
  bind configured_top.lanes[1].generated monitor #(.MAGIC(P))
    selected_monitor(.value(value));

  initial begin
    #2;
    $finish;
  end
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
  assign value = P;
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
  assign value = P;
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
    test_hierarchy(directory.path, fsim::project::Optimization::o0);
    test_hierarchy(directory.path, fsim::project::Optimization::o2);
    return 0;
}
