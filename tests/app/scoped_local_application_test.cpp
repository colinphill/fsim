// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
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
  fsim::runtime::RunResult run;
  std::array<std::string, 3> signals;
  std::vector<std::string> locals;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
  bool inactive_local_unavailable{};
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
  const std::array paths{
      std::string_view{"scoped_local_app.result"},
      std::string_view{"scoped_local_app.count"},
      std::string_view{"scoped_local_app.value"},
  };
  std::array<fsim::runtime::simir::SignalId, paths.size()> signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }

  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  capture.run = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.signals[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  const auto& locals =
      simulation.design().processes().front().debug_locals;
  // The untaken branch has a stable metadata object but its automatic local
  // never enters scope, so it intentionally has no readable runtime value.
  assert(locals.size() == 7);
  capture.locals.reserve(6);
  for (std::size_t index = 0; index < 6; ++index) {
    capture.locals.push_back(
        simulation.read_process_local(0, index).to_msb_string());
  }
  try {
    (void)simulation.read_process_local(0, 6);
  } catch (const std::logic_error&) {
    capture.inactive_local_unavailable = true;
  }
  return capture;
}

void test_scoped_locals(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  const auto optimization_name =
      optimization == fsim::project::Optimization::o0 ? "O0" : "O2";
  const auto checkpoint =
      [optimization_name](const std::string_view phase) {
        std::cerr << "scoped_locals " << optimization_name << ": "
                  << phase << '\n';
      };
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "scoped-local-application-test";
  config.project.top = "sv:work.scoped_local_app";
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
  checkpoint("building reference project");
  auto reference_project =
      fsim::app::build_project(config, diagnostics);
  checkpoint("building compiled project");
  auto compiled_project =
      fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project && compiled_project);
  const auto& debug_locals =
      reference_project->design.processes().front().debug_locals;
  assert(debug_locals.size() == 7);
  assert(debug_locals[0].name == "root_scope.value");
  assert(
      debug_locals[1].name
      == "root_scope.inner_scope.value");
  assert(
      debug_locals[2].name.starts_with("root_scope.$block_")
      && debug_locals[2].name.ends_with(".anonymous"));
  assert(debug_locals[3].name == "root_scope.each.scratch");
  assert(
      debug_locals[4].name
      == "root_scope.dynamic.scratch");
  assert(
      debug_locals[5].name
      == "root_scope.selected.branch");
  assert(
      debug_locals[6].name
      == "root_scope.alternate.branch");

  checkpoint("running interpreter");
  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  checkpoint("running compiled engine");
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  assert(reference.run.status == fsim::runtime::RunStatus::completed);
  assert(compiled.run.status == reference.run.status);
  assert(compiled.run.time == reference.run.time);
  assert(compiled.run.delta == reference.run.delta);
  assert(
      compiled.run.callbacks_executed
      == reference.run.callbacks_executed);
  assert(compiled.signals == reference.signals);
  assert(compiled.locals == reference.locals);
  assert(reference.inactive_local_unavailable);
  assert(compiled.inactive_local_unavailable);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 1);
#else
  assert(compiled.compiled_processes == 0);
#endif
  assert((
      reference.signals
      == std::array<std::string, 3>{
          "00010101", "10", "XXXXXXXX"}));
  assert((
      reference.locals
      == std::vector<std::string>{
          "00000001",
          "00000100",
          "00000001",
          "00001001",
          "00000111",
          "00000101"}));

  checkpoint("building warm project");
  auto warm_project =
      fsim::app::build_project(config, diagnostics);
  assert(warm_project);
  checkpoint("running warm compiled engine");
  const auto warm = execute(
      std::move(*warm_project),
      fsim::app::SimulationEngine::compiled);
  assert(warm.signals == reference.signals);
  assert(warm.locals == reference.locals);
#if defined(FSIM_HAS_LLVM)
  assert(warm.cache.hits >= 1);
#else
  assert(warm.cache.hits == 0);
#endif
  checkpoint("complete");
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-scoped-local-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "scoped_local.sv";
  {
    std::ofstream output(source);
    output << R"(
module scoped_local_app;
  logic [7:0] result;
  logic [1:0] count;
  logic [7:0] value;
  initial begin : root_scope
    logic [7:0] value = 8'd1;
    result = 8'd0;
    count = 2'd0;
    begin : inner_scope
      logic [7:0] value = 8'd4;
      result = result + value;
    end : inner_scope
    result = result + value;
    begin
      logic [7:0] anonymous = 8'd1;
      result = result + anonymous;
    end
    for (int lane = 0; lane < 2; lane++) begin : each
      logic [7:0] scratch = 8'd2;
      result = result + scratch;
      scratch = 8'd9;
    end : each
    while (count < 2) begin : dynamic
      logic [7:0] scratch = 8'd3;
      result = result + scratch;
      scratch = 8'd7;
      count++;
    end : dynamic
    if (count == 2) begin : selected
      logic [7:0] branch = 8'd5;
      result = result + branch;
    end : selected
    else begin : alternate
      logic [7:0] branch = 8'd8;
      result = result + branch;
    end : alternate
  end : root_scope
endmodule
)";
  }

  test_scoped_locals(
      directory.path, source, fsim::project::Optimization::o0);
  test_scoped_locals(
      directory.path, source, fsim::project::Optimization::o2);
  std::cout << "scoped local application tests passed\n";
  return 0;
}
