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

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-projected";
  config.project.top = "vhdl:work.vhdl_projected(rtl)";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
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
      "vhdl_projected.default_output",
      "vhdl_projected.explicit_output",
      "vhdl_projected.transport_output",
      "vhdl_projected.selected_output",
      "vhdl_projected.slice_output",
      "vhdl_projected.conditional_output",
      "vhdl_projected.sequential_output"};
  std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
  std::array<fsim::runtime::VcdSignal, names.size()> vcd_signals{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(
      vcd_output, capture.resolution, 128);
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = simulation.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
    vcd_signals[index] = vcd.declare_signal(
        std::string{names[index]},
        names[index].find("slice") != std::string_view::npos
            ? 4U
            : 1U);
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
        const auto index = static_cast<std::size_t>(
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

void verify_capture(const Capture& capture) {
  using TimedValue =
      std::pair<std::string, fsim::runtime::SimulationTick>;
  assert(capture.result.status == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 33);
  assert(capture.resolution == "1ps");
  assert((
      changes_for(capture, "vhdl_projected.default_output")
      == std::vector<TimedValue>{{"0", 5}}));
  assert((
      changes_for(capture, "vhdl_projected.explicit_output")
      == std::vector<TimedValue>{
          {"0", 5}, {"1", 25}, {"0", 28}}));
  assert((
      changes_for(capture, "vhdl_projected.transport_output")
      == std::vector<TimedValue>{
          {"0", 5}, {"1", 25}, {"0", 27}}));
  assert((
      changes_for(capture, "vhdl_projected.selected_output")
      == std::vector<TimedValue>{
          {"0", 4}, {"1", 24}, {"0", 27}}));
  assert((
      changes_for(capture, "vhdl_projected.slice_output")
      == std::vector<TimedValue>{
          {"X00X", 3}, {"X11X", 23}}));
  assert((
      changes_for(capture, "vhdl_projected.conditional_output")
      == std::vector<TimedValue>{
          {"0", 6}, {"1", 26}, {"0", 28}}));
  assert((
      changes_for(capture, "vhdl_projected.sequential_output")
      == std::vector<TimedValue>{
          {"0", 0}, {"1", 25}, {"0", 28}}));
  assert(capture.vcd.find("$timescale 1ps $end")
         != std::string::npos);
  assert(capture.vcd.find("#28") != std::string::npos);
}

void verify_invalid_rejection(
    const std::filesystem::path& directory) {
  const auto source = directory / "invalid_rejection.vhd";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
entity invalid_rejection is
end entity;
architecture rtl of invalid_rejection is
  signal result : std_logic;
begin
  result <= reject 6 ps inertial '1' after 5 ps;
end architecture;
)";
    assert(output.good());
  }
  auto config = make_config(
      directory, source, fsim::project::Optimization::o2);
  config.project.top =
      "vhdl:work.invalid_rejection(rtl)";
  fsim::diagnostic::Engine diagnostics;
  assert(!fsim::app::build_project(config, diagnostics));
  assert(std::ranges::any_of(
      diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-VHDL-SEM-032";
      }));

  const auto inexact_source =
      directory / "inexact_rejection.vhd";
  {
    std::ofstream output(inexact_source, std::ios::binary);
    output << R"(
entity inexact_rejection is
end entity;
architecture rtl of inexact_rejection is
  signal result : std_logic;
begin
  result <= reject 1 ps inertial '1' after 2 ps;
end architecture;
)";
    assert(output.good());
  }
  auto inexact_config = make_config(
      directory,
      inexact_source,
      fsim::project::Optimization::o2);
  inexact_config.project.top =
      "vhdl:work.inexact_rejection(rtl)";
  inexact_config.project.time_resolution = "2ps";
  fsim::diagnostic::Engine inexact_diagnostics;
  assert(!fsim::app::build_project(
      inexact_config, inexact_diagnostics));
  assert(std::ranges::any_of(
      inexact_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-TIME-0003";
      }));
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-projected-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "vhdl_projected.vhd";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
entity vhdl_projected is
end entity;

architecture rtl of vhdl_projected is
  signal default_drive : std_logic;
  signal explicit_drive : std_logic;
  signal transport_drive : std_logic;
  signal selected_drive : std_logic;
  signal slice_drive : std_logic_vector(1 downto 0);
  signal selector : boolean;
  signal default_output : std_logic;
  signal explicit_output : std_logic;
  signal transport_output : std_logic;
  signal selected_output : std_logic;
  signal slice_output : std_logic_vector(3 downto 0);
  signal conditional_output : std_logic;
  signal sequential_output : std_logic;
begin
  default_output <= default_drive after 5 ps;
  explicit_output <=
      reject 2 ps inertial explicit_drive after 5 ps;
  transport_output <= transport transport_drive after 5 ps;
  with selector select
    selected_output <= reject 2 ps inertial
      selected_drive after 4 ps when true,
      '0' after 4 ps when others;
  slice_output(2 downto 1) <=
      transport slice_drive after 3 ps;
  conditional_output <= transport
      transport_drive when selector else '0' after 6 ps;

  stimulus: process
  begin
    default_drive <= '0';
    explicit_drive <= '0';
    transport_drive <= '0';
    selected_drive <= '0';
    slice_drive <= "00";
    selector <= false;
    wait for 20 ps;
    default_drive <= '1';
    explicit_drive <= '1';
    transport_drive <= '1';
    selected_drive <= '1';
    slice_drive <= "11";
    selector <= true;
    wait for 2 ps;
    default_drive <= '0';
    transport_drive <= '0';
    wait for 1 ps;
    explicit_drive <= '0';
    selector <= false;
    wait for 10 ps;
    wait;
  end process;

  sequential: process
  begin
    sequential_output <= '0';
    wait for 20 ps;
    sequential_output <= reject 2 ps inertial '1' after 5 ps;
    wait for 3 ps;
    sequential_output <= reject 2 ps inertial '0' after 5 ps;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config =
        make_config(directory.path, source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    assert(reference.result.status == cold.result.status);
    assert(reference.result.time == cold.result.time);
    assert(reference.changes == cold.changes);
    assert(reference.final_values == cold.final_values);
    assert(reference.vcd == cold.vcd);
    assert(reference.changes == warm.changes);
    assert(reference.final_values == warm.final_values);
    assert(reference.vcd == warm.vcd);
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
  verify_invalid_rejection(directory.path);
  std::cout << "VHDL projected waveform application tests passed\n";
}
