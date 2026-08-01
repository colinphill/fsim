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

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<std::tuple<
      std::string,
      fsim::runtime::SimulationTick,
      std::uint64_t>> changes;
  std::string final_value;
  std::string vcd;
  std::string resolution;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    std::string resolution = "auto") {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "time-rounding";
  config.project.top = "sv:work.time_rounding";
  config.project.time_resolution = std::move(resolution);
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
      std::move(*project), 1000, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();
  const auto marker =
      simulation.find_signal("time_rounding.marker");
  assert(marker);

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, "1ps", 64);
  const auto vcd_marker = vcd.declare_signal("time_rounding.marker", 4);
  vcd.begin(simulation.now());
  vcd.change(vcd_marker, simulation.read_signal(*marker));
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        if (signal != *marker) {
          return;
        }
        capture.changes.emplace_back(
            value.to_msb_string(), time, delta);
        vcd.set_time(time);
        vcd.change(vcd_marker, value);
      });
  capture.result = simulation.run();
  capture.final_value =
      simulation.read_signal(*marker).to_msb_string();
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  const auto config = config_for(
      directory, source, optimization);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 2835);
  assert(reference.resolution == "1ps");
  assert(reference.final_value == "0101");
  assert(reference.changes.size() == 6);
  constexpr std::array expected_times{
      fsim::runtime::SimulationTick{0},
      fsim::runtime::SimulationTick{0},
      fsim::runtime::SimulationTick{1},
      fsim::runtime::SimulationTick{1235},
      fsim::runtime::SimulationTick{1835},
      fsim::runtime::SimulationTick{2835},
  };
  for (std::size_t index = 0; index < expected_times.size(); ++index) {
    assert(std::get<1>(reference.changes[index]) == expected_times[index]);
  }
  assert(reference.vcd.find("#1235") != std::string::npos);
  assert(reference.vcd.find("#2835") != std::string::npos);
  assert(reference.result.status == cold.result.status);
  assert(reference.result.time == cold.result.time);
  assert(reference.changes == cold.changes);
  assert(reference.final_value == cold.final_value);
  assert(reference.vcd == cold.vcd);
  assert(reference.result.status == warm.result.status);
  assert(reference.result.time == warm.result.time);
  assert(reference.changes == warm.changes);
  assert(reference.final_value == warm.final_value);
  assert(reference.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 1);
  assert(cold.native_cache.hits == 0);
  assert(cold.native_cache.misses == 1);
  assert(cold.native_cache.stores == 1);
  assert(warm.compiled_processes == 1);
  assert(warm.native_cache.hits == 1);
  assert(warm.native_cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
}

void verify_resolution_diagnostic(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  auto config = config_for(
      directory,
      source,
      fsim::project::Optimization::o2,
      "10ps");
  fsim::diagnostic::Engine diagnostics;
  const auto project = fsim::app::build_project(config, diagnostics);
  assert(!project);
  assert(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-TIME-0004";
          }));
}

void verify_overflow_diagnostic(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1s / 1s;
module time_rounding;
  initial #18446744073709551615 $finish;
endmodule
)";
    assert(output.good());
  }
  const auto config = config_for(
      directory,
      source,
      fsim::project::Optimization::o2,
      "1fs");
  fsim::diagnostic::Engine diagnostics;
  const auto project = fsim::app::build_project(config, diagnostics);
  assert(!project);
  assert(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-TIME-0003"
                && diagnostic.message.find("overflows")
                    != std::string::npos;
          }));
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-time-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "time_rounding.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module time_rounding;
  logic [3:0] marker;
  initial begin
    marker = 0;
    #0.0004 marker = 1;
    #0.0005 marker = 2;
    #1.2344ns marker = 3;
    #0.0006us marker = 4;
    #1 marker = 5;
    $finish;
  end
endmodule
)";
    assert(output.good());
  }

  verify_mode(
      directory.path,
      source,
      fsim::project::Optimization::o0);
  verify_mode(
      directory.path,
      source,
      fsim::project::Optimization::o2);
  verify_resolution_diagnostic(directory.path, source);
  verify_overflow_diagnostic(directory.path, source);
  std::cout << "time application tests passed\n";
  return 0;
}
