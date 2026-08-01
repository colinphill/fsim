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
#include <type_traits>
#include <utility>
#include <variant>
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
  std::string value;
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const Change&, const Change&) = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<Change> changes;
  std::string final_value;
  std::vector<std::pair<std::string, std::string>> drivers;
  std::string vcd;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

bool writes_signal(
    const fsim::runtime::simir::Process& process,
    const fsim::runtime::simir::SignalId signal) {
  return std::ranges::any_of(
      process.operations,
      [signal](const fsim::runtime::simir::Operation& operation) {
        return fsim::runtime::simir::visit_operation(
            [signal](const auto& op) {
              using T = std::decay_t<decltype(op)>;
              if constexpr (
                  std::is_same_v<T, fsim::runtime::simir::WriteBlocking>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteUpdate>
                  || std::is_same_v<T, fsim::runtime::simir::WriteAfter>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteInertial>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteProjected>
                  || std::is_same_v<
                      T,
                      fsim::runtime::simir::WriteProjectedWaveform>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteBlockingSlice>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteUpdateSlice>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteAfterSlice>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteInertialSlice>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteProjectedSlice>
                  || std::is_same_v<
                      T,
                      fsim::runtime::simir::
                          WriteProjectedWaveformSlice>) {
                return op.signal == signal;
              } else {
                return false;
              }
            },
            operation);
      });
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& sv_source,
    const std::filesystem::path& vhdl_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "mixed-resolution";
  config.project.top = "sv:work.resolved_top";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sv_sources;
  sv_sources.language = fsim::project::Language::system_verilog;
  sv_sources.standard = "2017";
  sv_sources.library = "work";
  sv_sources.files.push_back(sv_source);
  config.source_sets.push_back(std::move(sv_sources));

  fsim::project::SourceSet vhdl_sources;
  vhdl_sources.language = fsim::project::Language::vhdl;
  vhdl_sources.standard = "2008";
  vhdl_sources.library = "work";
  vhdl_sources.files.push_back(vhdl_source);
  config.source_sets.push_back(std::move(vhdl_sources));

  config.bindings = {
      {
          "resolved_top.u_vhdl",
          "vhdl:work.vhdl_driver(rtl)",
          std::string{"std_logic"}},
      {
          "resolved_top.u_sv",
          "sv:work.sv_driver",
          std::string{"std_logic"}},
  };
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
  const auto shared =
      project->design.find_signal("resolved_top.shared");
  assert(shared);
  assert(
      project->design.signals().at(*shared).resolution
      == fsim::runtime::simir::ResolutionKind::std_logic);

  std::vector<std::pair<
      fsim::runtime::simir::ProcessId,
      std::string>>
      drivers;
  for (const auto& process : project->design.processes()) {
    if (writes_signal(process, *shared)) {
      drivers.emplace_back(process.id, process.name);
    }
  }
  assert(drivers.size() == 2);

  Capture capture;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, "1ps", 64);
  const auto vcd_signal =
      vcd.declare_signal("resolved_top.shared", 1);
  vcd.begin(simulation.now());
  vcd.change(vcd_signal, simulation.read_signal(*shared));
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        if (signal != *shared) {
          return;
        }
        capture.changes.push_back(
            {value.to_msb_string(), time, delta});
        vcd.set_time(time);
        vcd.change(vcd_signal, value);
      });
  capture.result = simulation.run();
  capture.final_value =
      simulation.read_signal(*shared).to_msb_string();
  for (const auto& [process, name] : drivers) {
    capture.drivers.emplace_back(
        name,
        simulation.read_driver(process, *shared).to_msb_string());
  }
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& sv_source,
    const std::filesystem::path& vhdl_source,
    const fsim::project::Optimization optimization) {
  const auto config =
      make_config(directory, sv_source, vhdl_source, optimization);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 6000);
  if (reference.changes
      != std::vector<Change>{
          {"1", 3000, 0},
          {"Z", 5000, 0},
      }) {
    for (const auto& change : reference.changes) {
      std::cerr << "change " << change.value << " @ "
                << change.time << " delta " << change.delta
                << '\n';
    }
  }
  assert((
      reference.changes
      == std::vector<Change>{
          {"1", 3000, 0},
          {"Z", 5000, 0},
      }));
  assert(reference.final_value == "Z");
  std::vector<std::string> driver_values;
  for (const auto& [name, value] : reference.drivers) {
    (void)name;
    driver_values.push_back(value);
  }
  std::ranges::sort(driver_values);
  assert((
      driver_values == std::vector<std::string>{"Z", "Z"}));
  assert(reference.vcd.find("$timescale 1ps $end")
         != std::string::npos);
  assert(reference.vcd.find("#3000") != std::string::npos);
  assert(reference.vcd.find("#5000") != std::string::npos);

  for (const auto* actual : {&cold, &warm}) {
    assert(reference.result.status == actual->result.status);
    assert(reference.result.time == actual->result.time);
    assert(reference.result.delta == actual->result.delta);
    assert(reference.changes == actual->changes);
    assert(reference.final_value == actual->final_value);
    assert(reference.drivers == actual->drivers);
    assert(reference.vcd == actual->vcd);
  }
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 3);
  assert(cold.cache.hits == 0);
  assert(cold.cache.misses == 3);
  assert(cold.cache.stores == 3);
  assert(warm.compiled_processes == 3);
  assert(warm.cache.hits == 3);
  assert(warm.cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-resolution-application-test-"
         + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);

  const auto sv_source = directory.path / "resolved_top.sv";
  {
    std::ofstream output(sv_source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module sv_driver(output logic value);
  initial begin
    value = 1'b1;
    #5ns value = 1'bz;
  end
endmodule

module resolved_top;
  logic shared;
  vhdl_driver u_vhdl(.value(shared));
  sv_driver u_sv(.value(shared));
  initial #6ns $finish;
endmodule
)";
  }

  const auto vhdl_source = directory.path / "vhdl_driver.vhd";
  {
    std::ofstream output(vhdl_source, std::ios::binary);
    output << R"(entity vhdl_driver is
  port (value : out std_logic);
end entity;

architecture rtl of vhdl_driver is
begin
  drive: process
  begin
    value <= '0';
    wait for 3 ns;
    value <= 'Z';
    wait;
  end process;
end architecture;
)";
  }

  verify_mode(
      directory.path,
      sv_source,
      vhdl_source,
      fsim::project::Optimization::o0);
  verify_mode(
      directory.path,
      sv_source,
      vhdl_source,
      fsim::project::Optimization::o2);
  return 0;
}
