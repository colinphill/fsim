// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult result;
  std::string value;
  std::string local;
  std::string vcd;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  bool child_debug_safe{};
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "fork-processes";
  config.project.top = "sv:work.fork_processes";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture execute(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto result =
      simulation.find_signal("fork_processes.result");
  assert(result);
  const auto& process = simulation.design().processes().front();
  const auto local = std::find_if(
      process.debug_locals.begin(), process.debug_locals.end(),
      [](const auto& value) { return value.name == "root.shared"; });
  assert(local != process.debug_locals.end());
  const auto local_index = static_cast<std::size_t>(
      std::distance(process.debug_locals.begin(), local));

  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  std::ostringstream vcd_text;
  fsim::runtime::VcdWriter vcd{vcd_text, "1ns", 8};
  const auto trace = vcd.declare_signal("fork_processes.result", 8);
  vcd.begin(simulation.now());
  vcd.change(trace, simulation.read_signal(*result));
  simulation.set_signal_change_hook(
      [&](const auto signal,
          const fsim::runtime::PackedLogic4& value,
          const auto time,
          const auto) {
        if (signal == *result) {
          vcd.set_time(time);
          vcd.change(trace, value);
        }
      });
  simulation.set_execution_point_hook(
      [&](fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
        if (point.process != point.design_process) {
          assert(point.design_process == 0);
          assert(!simulation.design().processes()
                      .at(point.design_process).name.empty());
          (void)simulation.read_process_local(
              point.process, local_index);
          capture.child_debug_safe = true;
        }
        return false;
      });
  capture.result = simulation.run();
  capture.value = simulation.read_signal(*result).to_msb_string();
  capture.local =
      simulation.read_process_local(0, local_index).to_msb_string();
  vcd.flush();
  capture.vcd = vcd_text.str();
  return capture;
}

void test_optimization(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  const auto config = make_config(directory, source, optimization);
  const auto reference =
      execute(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      execute(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      execute(config, fsim::app::SimulationEngine::compiled);
  for (const auto* capture : {&reference, &cold, &warm}) {
    assert(
        capture->result.status == fsim::runtime::RunStatus::stopped
        && capture->result.time == 11
        && capture->value == "10111111"
        && capture->local == "10111111"
        && capture->child_debug_safe);
    assert(std::any_of(
        capture->points.begin(), capture->points.end(),
        [](const auto& point) { return point.process > 0; }));
  }
  assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 1);
  assert(cold.cache.hits == 0 && cold.cache.misses == 1);
  assert(warm.cache.hits == 1 && warm.cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
#endif
  assert(reference.vcd == cold.vcd && cold.vcd == warm.vcd);
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-fork-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "fork_processes.sv";
  {
    std::ofstream output(source);
    output << R"(
module fork_processes;
  logic [7:0] result;
  initial begin : root
    logic [7:0] shared = 0;
    result = 0;
    fork : all_children
      shared[0] = 1;
      #2 shared[1] = 1;
    join : all_children
    shared[2] = 1;
    fork
      #1 shared[3] = 1;
      #3 shared[4] = 1;
    join_any
    shared[5] = 1;
    wait fork;
    fork
      #5 shared[6] = 1;
    join_none
    shared[7] = 1;
    disable fork;
    result = shared;
    #6;
    $finish;
  end
endmodule
)";
  }
  test_optimization(
      directory.path, source, fsim::project::Optimization::o0);
  test_optimization(
      directory.path, source, fsim::project::Optimization::o2);
  std::cout << "fork application tests passed\n";
  return 0;
}
