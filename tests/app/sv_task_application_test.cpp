// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
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
  std::array<std::string, 2> values;
  std::vector<std::string> keys;
  std::vector<std::string> locals;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-tasks";
  config.project.top = "sv:work.task_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language =
      fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  Capture capture;
  capture.keys = project->specialization_cache_keys;
  assert(project->design.processes().size() == 1);
  for (const auto& local :
       project->design.processes().front().debug_locals) {
    capture.locals.push_back(local.name);
  }

  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  simulation.set_execution_point_hook(
      [&capture](
          fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
      });
  const auto result =
      simulation.find_signal("task_top.result");
  const auto total =
      simulation.find_signal("task_top.total");
  assert(result && total);
  capture.result = simulation.run();
  capture.values = {
      simulation.read_signal(*result).to_msb_string(),
      simulation.read_signal(*total).to_msb_string()};
  return capture;
}

void verify(
    const Capture& capture,
    const std::array<std::string, 2>& expected) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.values == expected);
  assert(std::ranges::find(
             capture.locals, "transform.temporary")
         != capture.locals.end());
  assert(std::ranges::find(
             capture.locals, "transform.value")
         != capture.locals.end());
  assert(std::ranges::count_if(
             capture.points,
             [](const auto& point) {
               return point.kind
                   == fsim::runtime::simir::
                       ExecutionPointKind::call;
             })
         == 2);
}

bool same_points(
    const std::vector<
        fsim::runtime::simir::ExecutionPoint>& left,
    const std::vector<
        fsim::runtime::simir::ExecutionPoint>& right) {
  return std::ranges::equal(
      left,
      right,
      [](const auto& first, const auto& second) {
        return first.process == second.process
            && first.instruction == second.instruction
            && first.kind == second.kind
            && first.source == second.source;
      });
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-tasks-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "tasks.sv";

  const auto write_source =
      [&](const unsigned increment) {
        std::ofstream output(
            source, std::ios::binary | std::ios::trunc);
        output
            << "package task_pkg;\n"
            << "  task automatic step(input logic [7:0] value,\n"
            << "      output logic [7:0] result);\n"
            << "    result = value + " << increment << ";\n"
            << "  endtask\n"
            << "endpackage\n"
            << R"(
module task_top;
  import task_pkg::*;
  logic [7:0] result;
  logic [7:0] total;

  task automatic transform(
      input logic [7:0] value,
      output logic [7:0] transformed,
      inout logic [7:0] accumulator);
    logic [7:0] temporary;
    step(value, temporary);
    transformed = temporary;
    accumulator = accumulator + temporary;
  endtask

  initial begin
    total = 8'd1;
    transform(8'd40, result, total);
  end
endmodule
)";
        assert(output.good());
      };

  write_source(2);
  std::vector<std::string> baseline_keys;
  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config =
        make_config(directory.path, source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    verify(reference, {"00101010", "00101011"});
    verify(cold, {"00101010", "00101011"});
    verify(warm, {"00101010", "00101011"});
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
    assert(same_points(reference.points, cold.points));
    assert(same_points(cold.points, warm.points));
    if (optimization == fsim::project::Optimization::o2) {
      baseline_keys = warm.keys;
    }
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 1);
    assert(cold.cache.misses == 1);
    assert(cold.cache.stores == 1);
    assert(warm.cache.hits == 1);
#else
    assert(cold.compiled_processes == 0);
#endif
  }

  write_source(3);
  const auto changed = run_once(
      make_config(
          directory.path,
          source,
          fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled);
  verify(changed, {"00101011", "00101100"});
  assert(baseline_keys.size() == 1);
  assert(changed.keys.size() == 1);
  assert(changed.keys.front() != baseline_keys.front());
#if defined(FSIM_HAS_LLVM)
  assert(changed.cache.hits == 0);
  assert(changed.cache.misses == 1);
  assert(changed.cache.stores == 1);
#endif
}
