// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"

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
    const fsim::project::DelayMode delay_mode) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "delay-modes";
  config.project.top = "sv:work.delay_modes";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;
  config.run.delay_mode = delay_mode;
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
  const auto marker = simulation.find_signal("delay_modes.marker");
  assert(marker);

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, capture.resolution, 64);
  const auto vcd_marker = vcd.declare_signal("delay_modes.marker", 2);
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

struct Expected {
  fsim::runtime::SimulationTick first;
  fsim::runtime::SimulationTick second;
  fsim::runtime::SimulationTick finish;
};

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::project::DelayMode mode,
    const Expected expected) {
  const auto config = config_for(directory, source, optimization, mode);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == expected.finish);
  assert(reference.resolution == "1ps");
  assert(reference.final_value == "10");
  assert(reference.changes.size() == 3);
  assert(std::get<1>(reference.changes[0]) == 0);
  assert(std::get<1>(reference.changes[1]) == expected.first);
  assert(std::get<1>(reference.changes[2]) == expected.second);
  assert(
      reference.vcd.find("#" + std::to_string(expected.second))
      != std::string::npos);

  for (const auto* actual : {&cold, &warm}) {
    assert(reference.result.status == actual->result.status);
    assert(reference.result.time == actual->result.time);
    assert(reference.changes == actual->changes);
    assert(reference.final_value == actual->final_value);
    assert(reference.vcd == actual->vcd);
  }
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

void verify_auto_resolution_selects_first(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(module delay_modes;
  timeunit 1ns;
  initial begin
    #(1ns:2ps:3fs);
    $finish;
  end
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
        directory, source, fsim::project::Optimization::o2, modes[index]);
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    assert(project->time_resolution == resolutions[index]);
  }
}

void verify_cli_override(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  const auto manifest = directory / "fsim.toml";
  {
    std::ofstream output(manifest, std::ios::binary);
    output
        << "schema = 2\n"
        << "[project]\n"
        << "top = \"sv:work.delay_modes\"\n"
        << "[[source_set]]\n"
        << "language = \"systemverilog\"\n"
        << "files = [\"" << source.filename().generic_string() << "\"]\n"
        << "[run]\n"
        << "delay_mode = \"max\"\n";
    assert(output.good());
  }

  bool invoked = false;
  fsim::cli::Services services;
  services.run =
      [&](const fsim::cli::Invocation& invocation,
          const fsim::project::Config& config,
          fsim::diagnostic::Engine&,
          std::ostream&,
          std::ostream&) {
        invoked = true;
        assert(
            invocation.delay_mode
                == fsim::project::DelayMode::minimum);
        assert(
            config.run.delay_mode
                == fsim::project::DelayMode::minimum);
        return 0;
      };
  const auto manifest_text = manifest.generic_string();
  const std::array arguments{
      "fsim", "run", "--project", manifest_text.c_str(),
      "--delay-mode", "min"};
  std::ostringstream output;
  std::ostringstream error;
  assert(
      fsim::cli::run(
          static_cast<int>(arguments.size()),
          arguments.data(),
          services,
          output,
          error)
      == 0);
  assert(invoked);
  assert(error.str().empty());

  fsim::diagnostic::Engine diagnostics;
  const auto source_text = source.filename().generic_string();
  const std::array invalid{
      "fsim", "run", "--delay-mode", "slow",
      source_text.c_str()};
  const auto invocation = fsim::cli::parse_arguments(
      static_cast<int>(invalid.size()), invalid.data(), diagnostics);
  assert(!invocation);
  assert(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-CLI-0001"
                && diagnostic.message.find("delay-mode")
                    != std::string::npos;
          }));
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-delay-mode-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "delay_modes.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module delay_modes;
  logic [1:0] marker;
  initial begin
    marker = 0;
    #(0.0004ns:0.0005ns:0.0016ns) marker = 1;
    #(1.1ns:2.2ns:3.3ns) marker = 2;
    #1ns $finish;
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
        Expected{0, 1100, 2100});
    verify_mode(
        directory.path,
        source,
        optimization,
        fsim::project::DelayMode::typical,
        Expected{1, 2201, 3201});
    verify_mode(
        directory.path,
        source,
        optimization,
        fsim::project::DelayMode::maximum,
        Expected{2, 3302, 4302});
  }

  verify_auto_resolution_selects_first(directory.path, source);
  verify_cli_override(directory.path, source);
  std::cout << "delay mode application tests passed\n";
  return 0;
}
