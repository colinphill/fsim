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
#include <string_view>
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
  std::array<std::string, 2> values;
  std::vector<std::string> keys;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  std::vector<std::string> locals;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-functions";
  config.project.top = "sv:work.function_top";
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
  sources.files = {package_source, top_source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

void print_diagnostics(
    const fsim::diagnostic::Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.span.path << ':'
              << diagnostic.span.begin.line << ':'
              << diagnostic.span.begin.column << ": "
              << diagnostic.code << ": "
              << diagnostic.message << '\n';
  }
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    print_diagnostics(diagnostics);
  }
  assert(project);
  assert(project->design.specializations().size() == 1);

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
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  simulation.set_execution_point_hook(
      [&capture](
          fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
      });

  constexpr std::array<std::string_view, 2> paths{
      "function_top.imported_result",
      "function_top.qualified_result"};
  std::array<fsim::runtime::simir::SignalId, paths.size()>
      signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }
  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  return capture;
}

void verify(
    const Capture& capture,
    const std::array<std::string, 2>& expected) {
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  assert(capture.values == expected);
  const auto calls = std::ranges::count_if(
      capture.points,
      [](const auto& point) {
        return point.kind
            == fsim::runtime::simir::ExecutionPointKind::call;
      });
  assert(calls == 6);
  assert(std::ranges::find(
             capture.locals, "inner.temporary")
         != capture.locals.end());
}

bool same_points(
    const std::vector<fsim::runtime::simir::ExecutionPoint>& left,
    const std::vector<fsim::runtime::simir::ExecutionPoint>& right) {
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
      / ("fsim-sv-functions-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto package_source =
      directory.path / "function_package.sv";
  const auto top_source = directory.path / "function_top.sv";

  const auto write_package =
      [&](const unsigned increment) {
        std::ofstream output(
            package_source,
            std::ios::binary | std::ios::trunc);
        output
            << "package function_pkg;\n"
            << "  function automatic logic [7:0] package_step(\n"
            << "      input logic [7:0] value);\n"
            << "    return value + " << increment << ";\n"
            << "  endfunction\n"
            << "endpackage\n";
        assert(output.good());
      };
  write_package(1);
  {
    std::ofstream output(top_source, std::ios::binary);
    output << R"(
module function_top;
  import function_pkg::*;

  function automatic int width_for(input int value);
    logic [31:0] width;
    width = 4;
    for (int index = 0; index < 2; index++)
      width = width + 2;
    case (value)
      5: return width;
      default: return 4;
    endcase
  endfunction

  localparam int WIDTH = width_for(5);
  logic [WIDTH-1:0] imported_result;
  logic [WIDTH-1:0] qualified_result;

  function automatic logic [WIDTH-1:0] inner(
      input logic [WIDTH-1:0] value);
    logic [WIDTH-1:0] temporary;
    begin : calculate
      temporary = value;
      for (int index = 0; index < 2; index++)
        temporary = temporary + 1;
      case (value[0])
        1'b0: temporary = temporary - 1;
        default: temporary = temporary - 1;
      endcase
      if (value == value)
        return temporary;
      return value;
    end
  endfunction

  function automatic logic [WIDTH-1:0] outer(
      input logic [WIDTH-1:0] value);
    outer = inner(value);
  endfunction

  initial begin
    imported_result = package_step(outer(8'd40));
    qualified_result =
        function_pkg::package_step(outer(8'd1));
    #1;
    $finish;
  end
endmodule
)";
    assert(output.good());
  }

  std::vector<std::string> baseline_o2_keys;
  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path,
        package_source,
        top_source,
        optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const std::array<std::string, 2> expected{
        "00101010", "00000011"};
    verify(reference, expected);
    verify(cold, expected);
    verify(warm, expected);
    assert(same_points(reference.points, cold.points));
    assert(same_points(cold.points, warm.points));
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
    if (optimization == fsim::project::Optimization::o2) {
      baseline_o2_keys = warm.keys;
    }
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 1);
    assert(cold.compiled_modules == 1);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 1);
    assert(cold.cache.stores == 1);
    assert(warm.cache.hits == 1);
    assert(warm.cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(cold.compiled_modules == 0);
#endif
  }

  write_package(2);
  const auto changed = run_once(
      make_config(
          directory.path,
          package_source,
          top_source,
          fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled);
  verify(changed, {"00101011", "00000100"});
  assert(baseline_o2_keys.size() == 1);
  assert(changed.keys.size() == 1);
  assert(changed.keys.front() != baseline_o2_keys.front());
#if defined(FSIM_HAS_LLVM)
  assert(changed.cache.hits == 0);
  assert(changed.cache.misses == 1);
  assert(changed.cache.stores == 1);
#endif
}
