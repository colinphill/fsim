// SPDX-License-Identifier: Apache-2.0
#include "application_workflow_test_support.hpp"

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
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
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

// A specparam written as a minimum:typical:maximum triple takes the delay
// mode's alternative wherever it is named (IEEE 1800-2023 6.20.5, 30.4.1): a
// gate delay `#(d)` and a module path delay `(i => o) = dp`. Hand derivation
// (timeunit 1ns / 1ps, so 1 ns = 1000 ticks): the gate's input rises at 10 ns
// and the path's at 20 ns; the marker's bit 0 sets when the gate output
// rises (10 ns + d) and bit 1 when the path output rises (20 ns + dp), with
// d = 1:2:3 ns and dp = 2:4:6 ns.
//   min: 11000 and 22000   typ: 12000 and 24000   max: 13000 and 26000
void verify_specparam_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::DelayMode mode,
    const fsim::runtime::SimulationTick gate_rise,
    const fsim::runtime::SimulationTick path_rise) {
  const auto config = config_for(
      directory, source, fsim::project::Optimization::o2, mode);
  for (const auto engine : {fsim::app::SimulationEngine::interpreter,
                            fsim::app::SimulationEngine::compiled}) {
    const auto capture = run_once(config, engine);
    assert(capture.changes.size() == 3);
    assert(std::get<1>(capture.changes[0]) == 0);
    assert(std::get<1>(capture.changes[1]) == gate_rise);
    assert(std::get<1>(capture.changes[2]) == path_rise);
    assert(capture.final_value == "11");
  }
}

void verify_cli_override() {
  bool invoked = false;
  fsim::cli::Services services;
  services.simulate =
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
  const std::array arguments{
      "fsim", "simulate", "--delay-mode", "min"};
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
  const std::array invalid{
      "fsim", "simulate", "--delay-mode", "slow"};
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

  {
    const auto specparam_source = directory.path / "specparam_modes.sv";
    std::ofstream output(specparam_source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module leaf(input i, output o);
  assign o = i;
  specify
    specparam dp = 2:4:6;
    (i => o) = dp;
  endspecify
endmodule

module delay_modes;
  logic [1:0] marker;
  specparam d = 1:2:3;
  reg a, b;
  wire w, p;
  buf #(d) g(w, a);
  leaf u(b, p);
  always @(w) if (w === 1'b1) marker[0] = 1'b1;
  always @(p) if (p === 1'b1) marker[1] = 1'b1;
  initial begin
    marker = 0;
    a = 0;
    b = 0;
    #10ns a = 1;
    #10ns b = 1;
    #20ns $finish;
  end
endmodule
)";
    assert(output.good());
    output.close();
    verify_specparam_mode(
        directory.path, specparam_source,
        fsim::project::DelayMode::minimum, 11000, 22000);
    verify_specparam_mode(
        directory.path, specparam_source,
        fsim::project::DelayMode::typical, 12000, 24000);
    verify_specparam_mode(
        directory.path, specparam_source,
        fsim::project::DelayMode::maximum, 13000, 26000);
  }

  verify_auto_resolution_selects_first(directory.path, source);
  verify_cli_override();
  std::cout << "delay mode application tests passed\n";
  return 0;
}
