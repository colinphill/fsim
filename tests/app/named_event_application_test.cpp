// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

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
  std::string event;
  std::string observed;
  std::size_t compiled_processes{};
};

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine) {
  fsim::app::Simulation simulation{
      std::move(project), 1000, engine};
  const auto event = simulation.find_signal("named_event_test.fired");
  const auto observed =
      simulation.find_signal("named_event_test.observed");
  assert(event && observed);

  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.result = simulation.run();
  capture.event = simulation.read_signal(*event).to_msb_string();
  capture.observed =
      simulation.read_signal(*observed).to_msb_string();
  return capture;
}

void test_named_events(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "named-event-test";
  config.project.top = "sv:work.named_event_test";
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
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(reference_project && compiled_project);

  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  for (const auto* capture : {&reference, &compiled}) {
    assert(
        capture->result.status
        == fsim::runtime::RunStatus::stopped);
    assert(capture->result.time == 3);
    assert(capture->event == "0");
    assert(capture->observed == "10");
  }
  assert(reference.compiled_processes == 0);
  assert(compiled.compiled_processes == 2);
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-named-event-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "named_event_test.sv";
  {
    std::ofstream output(source);
    output << R"(
module named_event_test;
  event fired;
  logic [1:0] observed;
  initial begin
    #1 -> fired;
    #1 -> fired;
    #1 $finish;
  end
  initial begin
    observed = 2'b00;
    repeat (2) begin
      @(fired);
      observed = observed + 2'b01;
    end
  end
endmodule
)";
  }

  test_named_events(
      directory.path, source, fsim::project::Optimization::o0);
  test_named_events(
      directory.path, source, fsim::project::Optimization::o2);
  std::cout << "named event application tests passed\n";
}
