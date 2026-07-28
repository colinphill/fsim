// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

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

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<std::string> values;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    for (const auto& note : diagnostic.notes) {
      std::cerr << "  " << note.message << '\n';
    }
  }
}

[[nodiscard]] Capture run(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine,
    const std::array<std::string, 5>& signal_paths) {
  std::array<fsim::runtime::simir::SignalId, 5> signals{};
  for (std::size_t index = 0; index < signal_paths.size(); ++index) {
    const auto signal = project.design.find_signal(signal_paths[index]);
    assert(signal);
    signals[index] = *signal;
  }

  fsim::app::Simulation simulation(
      std::move(project), 1000, engine);
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.result = simulation.run();
  capture.values.reserve(signals.size());
  for (const auto signal : signals) {
    capture.values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  return capture;
}

void test_wildcard_equality(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "wildcard-equality-expression-test";
  config.project.top = "sv:work.wildcard_equality_app";
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
  assert(reference_project);
  assert(compiled_project);

  const std::array<std::string, 5> signal_paths{
      "wildcard_equality_app.masked",
      "wildcard_equality_app.left_unknown",
      "wildcard_equality_app.known_mismatch",
      "wildcard_equality_app.wildcard_neq",
      "wildcard_equality_app.logical_equal"};
  const auto reference = run(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter,
      signal_paths);
  const auto compiled = run(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled,
      signal_paths);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.status == compiled.result.status);
  assert(reference.result.time == compiled.result.time);
  assert(reference.result.delta == compiled.result.delta);
  assert(reference.values == compiled.values);
  assert((
      compiled.values
      == std::vector<std::string>{"1", "X", "0", "0", "X"}));
  assert(reference.compiled_processes == 0);
  assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 1);
  assert(compiled.compiled_modules == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

}  // namespace

int main() {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-expression-application-test-"
         + std::to_string(suffix))};
  std::filesystem::create_directories(directory.path);

  const auto source = directory.path / "wildcard_equality.sv";
  {
    std::ofstream output(source);
    output << R"(
module wildcard_equality_app;
  logic masked;
  logic left_unknown;
  logic known_mismatch;
  logic wildcard_neq;
  logic logical_equal;
  initial begin
    masked = 4'b10x1 ==? 4'b10?1;
    left_unknown = 4'b10x1 ==? 4'b1011;
    known_mismatch = 4'b1101 ==? 4'b10?1;
    wildcard_neq = 4'b1001 !=? 4'b10z1;
    logical_equal = 2'bx0 == 2'bx1;
    $finish;
  end
endmodule
)";
  }

  test_wildcard_equality(
      directory.path, source, fsim::project::Optimization::o0);
  test_wildcard_equality(
      directory.path, source, fsim::project::Optimization::o2);
}
