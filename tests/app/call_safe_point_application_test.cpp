// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
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

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult stopped;
  fsim::runtime::RunResult resumed;
  std::string before;
  std::string after;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  std::size_t compiled_processes{};
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": "
              << diagnostic.message << '\n';
  }
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine) {
  fsim::app::Simulation simulation{
      std::move(project), 1000, engine};
  const auto result =
      simulation.find_signal("call_safe_point.result");
  assert(result);

  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  bool stopped_at_call = false;
  simulation.set_execution_point_hook(
      [&](fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
        if (!stopped_at_call
            && point.kind
                == fsim::runtime::simir::ExecutionPointKind::call) {
          stopped_at_call = true;
          scheduler.request_stop();
        }
      });
  capture.stopped = simulation.run();
  capture.before = simulation.read_signal(*result).to_msb_string();
  simulation.clear_stop();
  capture.resumed = simulation.run();
  capture.after = simulation.read_signal(*result).to_msb_string();
  return capture;
}

void test_call_safe_points(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "call-safe-point-test";
  config.project.top = "sv:work.call_safe_point";
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

  fsim::diagnostic::Engine diagnostics;
  auto reference_project =
      fsim::app::build_project(config, diagnostics);
  auto compiled_project =
      fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project && compiled_project);

  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  for (const auto* capture : {&reference, &compiled}) {
    assert(capture->stopped.status == fsim::runtime::RunStatus::stopped);
    assert(capture->resumed.status == fsim::runtime::RunStatus::completed);
    assert(capture->before == "X");
    assert(capture->after == "0");
    std::vector<const fsim::runtime::simir::ExecutionPoint*> calls;
    for (const auto& point : capture->points) {
      if (point.kind
          == fsim::runtime::simir::ExecutionPointKind::call) {
        calls.push_back(&point);
      }
    }
    assert(calls.size() == 2);
    for (const auto* call : calls) {
      assert(
          std::filesystem::path{call->source.path}.filename()
          == source.filename());
      assert(call->source.line == 7);
    }
    assert(calls[0]->source.column == 14);
    assert(calls[1]->source.column == 25);
  }
  assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 1);
#else
  assert(compiled.compiled_processes == 0);
#endif
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-call-safe-point-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "call_safe_point.sv";
  {
    std::ofstream output(source);
    output << R"(
module call_safe_point;
  logic input_value;
  logic result;
  initial begin
    input_value = 1'b0;
    result = $isunknown($signed(input_value));
  end
endmodule
)";
  }

  test_call_safe_points(
      directory.path, source, fsim::project::Optimization::o0);
  test_call_safe_points(
      directory.path, source, fsim::project::Optimization::o2);
  std::cout << "call safe-point application tests passed\n";
}
