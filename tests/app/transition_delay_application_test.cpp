// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
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

struct Change {
  std::string signal;
  std::string value;
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const Change&, const Change&) = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<Change> changes;
  std::vector<std::pair<std::string, std::string>> final_values;
  std::string vcd;
  std::string resolution;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::project::DelayMode mode) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "transition-delays";
  config.project.top = "sv:work.transition_delays";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.delay_mode = mode;
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
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

  Capture capture;
  capture.resolution = project->time_resolution;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 7> names{
      "transition_delays.mode_output",
      "transition_delays.vector_output",
      "transition_delays.slice_output",
      "transition_delays.pulse_output",
      "transition_delays.one_output",
      "transition_delays.two_output",
      "transition_delays.gate_output"};
  std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, capture.resolution, 128);
  std::array<fsim::runtime::VcdSignal, names.size()> vcd_signals{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = simulation.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
    const auto width =
        names[index].find("vector") != std::string_view::npos
                || names[index].find("slice") != std::string_view::npos
            ? 4U
            : 1U;
    vcd_signals[index] =
        vcd.declare_signal(std::string{names[index]}, width);
  }
  vcd.begin(simulation.now());
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(
        vcd_signals[index], simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        const auto found =
            std::find(signals.begin(), signals.end(), signal);
        if (found == signals.end()) {
          return;
        }
        const auto index =
            static_cast<std::size_t>(
                std::distance(signals.begin(), found));
        capture.changes.push_back(
            {
                std::string{names[index]},
                value.to_msb_string(),
                time,
                delta});
        vcd.set_time(time);
        vcd.change(vcd_signals[index], value);
      });
  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.final_values.emplace_back(
        names[index],
        simulation.read_signal(signals[index]).to_msb_string());
  }
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

std::vector<std::pair<std::string, fsim::runtime::SimulationTick>>
changes_for(
    const Capture& capture,
    const std::string_view signal) {
  std::vector<std::pair<
      std::string,
      fsim::runtime::SimulationTick>> result;
  for (const auto& change : capture.changes) {
    if (change.signal == signal) {
      result.emplace_back(change.value, change.time);
    }
  }
  return result;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::project::DelayMode mode,
    const std::array<fsim::runtime::SimulationTick, 5>& mode_times) {
  const auto config = config_for(
      directory, source, optimization, mode);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 100);
  assert(reference.resolution == "1ps");
  const std::vector<std::pair<
      std::string,
      fsim::runtime::SimulationTick>> expected_mode{
      {"0", mode_times[0]},
      {"1", mode_times[1]},
      {"0", mode_times[2]},
      {"Z", mode_times[3]},
      {"X", mode_times[4]}};
  assert(
      changes_for(reference, "transition_delays.mode_output")
      == expected_mode);
  assert((
      changes_for(reference, "transition_delays.vector_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"0000", 7}, {"10Z0", 23}, {"0000", 47}}));
  assert((
      changes_for(reference, "transition_delays.slice_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"Z00Z", 6}, {"Z11Z", 22}}));
  assert((
      changes_for(reference, "transition_delays.pulse_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{{"0", 7}}));
  assert((
      changes_for(reference, "transition_delays.one_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"0", 4}, {"1", 24}, {"0", 44}, {"Z", 64}, {"X", 84}}));
  assert((
      changes_for(reference, "transition_delays.two_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"0", 3}, {"1", 29}, {"0", 43}, {"Z", 63}, {"X", 83}}));
  assert(
      changes_for(reference, "transition_delays.gate_output")
      == changes_for(reference, "transition_delays.two_output"));
  assert(
      reference.vcd.find(
          "#" + std::to_string(mode_times.back()))
      != std::string::npos);

  for (const auto* actual : {&cold, &warm}) {
    assert(reference.result.status == actual->result.status);
    assert(reference.result.time == actual->result.time);
    assert(reference.changes == actual->changes);
    assert(reference.final_values == actual->final_values);
    assert(reference.vcd == actual->vcd);
  }
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 8);
  assert(cold.native_cache.hits == 0);
  assert(cold.native_cache.misses == 1);
  assert(cold.native_cache.stores == 1);
  assert(warm.compiled_processes == 8);
  assert(warm.native_cache.hits == 1);
  assert(warm.native_cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
}

void verify_auto_resolution_uses_all_values(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(module transition_delays;
  timeunit 1ns;
  logic source;
  wire result;
  assign #(
      1ns:2ps:3fs,
      4ns:5ps:6fs,
      7ns:8ps:9fs) result = source;
endmodule
)";
    assert(output.good());
  }
  constexpr std::array modes{
      fsim::project::DelayMode::minimum,
      fsim::project::DelayMode::typical,
      fsim::project::DelayMode::maximum};
  constexpr std::array<std::string_view, 3> resolutions{
      "1ns", "1ps", "1fs"};
  for (std::size_t index = 0; index < modes.size(); ++index) {
    auto config = config_for(
        directory,
        source,
        fsim::project::Optimization::o2,
        modes[index]);
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    assert(project->time_resolution == resolutions[index]);
  }
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-transition-delay-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "transition_delays.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module transition_delays;
  logic mode_drive;
  logic [3:0] vector_drive;
  logic [1:0] slice_drive;
  logic pulse_drive;
  wire mode_output;
  wire [3:0] vector_output;
  wire [3:0] slice_output;
  wire pulse_output;
  wire one_output;
  wire two_output;
  wire gate_output;

  assign #(
      1ps:2ps:3ps,
      4ps:5ps:6ps,
      7ps:8ps:9ps) mode_output = mode_drive;
  assign #(3ps, 7ps, 11ps) vector_output = vector_drive;
  assign #(2ps, 6ps, 10ps) slice_output[2:1] = slice_drive;
  assign #(5ps, 7ps, 9ps) pulse_output = pulse_drive;
  assign #4ps one_output = mode_drive;
  assign #(9ps, 3ps) two_output = mode_drive;
  buf #(9ps, 3ps) (gate_output, mode_drive);

  initial begin
    mode_drive = 1'b0;
    vector_drive = 4'b0000;
    slice_drive = 2'b00;
    pulse_drive = 1'b0;
    #20ps;
    mode_drive = 1'b1;
    vector_drive = 4'b10z0;
    slice_drive = 2'b11;
    pulse_drive = 1'b1;
    #2ps pulse_drive = 1'b0;
    #18ps;
    mode_drive = 1'b0;
    vector_drive = 4'b0000;
    #20ps mode_drive = 1'bz;
    #20ps mode_drive = 1'bx;
    #20ps $finish;
  end
endmodule
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    verify_mode(
        directory.path,
        source,
        optimization,
        fsim::project::DelayMode::minimum,
        {4, 21, 44, 67, 81});
    verify_mode(
        directory.path,
        source,
        optimization,
        fsim::project::DelayMode::typical,
        {5, 22, 45, 68, 82});
    verify_mode(
        directory.path,
        source,
        optimization,
        fsim::project::DelayMode::maximum,
        {6, 23, 46, 69, 83});
  }
  verify_auto_resolution_uses_all_values(
      directory.path, source);
  std::cout << "transition delay application tests passed\n";
}
