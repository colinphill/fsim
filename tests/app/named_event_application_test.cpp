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
  struct Change {
    fsim::runtime::SimulationTick time{};
    std::uint64_t delta{};
    std::string value;
  };

  fsim::runtime::RunResult result;
  std::string event;
  std::string observed;
  std::vector<Change> event_changes;
  std::vector<Change> observed_changes;
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
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        auto* changes =
            signal == *event
            ? &capture.event_changes
            : signal == *observed
                ? &capture.observed_changes
                : nullptr;
        if (changes != nullptr) {
          changes->push_back(
              Capture::Change{
                  time, delta, value.to_msb_string()});
        }
      });
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
    assert(capture->result.time == 4);
    assert(capture->event == "0");
    assert(capture->observed == "10");
    assert(capture->event_changes.size() == 2);
    assert(capture->event_changes[0].time == 1);
    assert(capture->event_changes[0].value == "1");
    assert(capture->event_changes[1].time == 3);
    assert(capture->event_changes[1].value == "0");
    assert(capture->observed_changes.size() == 3);
    assert(capture->observed_changes[0].time == 0);
    assert(capture->observed_changes[0].value == "00");
    assert(capture->observed_changes[1].time == 1);
    assert(capture->observed_changes[1].value == "01");
    assert(
        capture->observed_changes[1].delta
        > capture->event_changes[0].delta);
    assert(capture->observed_changes[2].time == 3);
    assert(capture->observed_changes[2].value == "10");
    assert(
        capture->observed_changes[2].delta
        > capture->event_changes[1].delta);
  }
  assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 2);
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
    #1 ->> #1 fired;
    #2 $finish;
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
