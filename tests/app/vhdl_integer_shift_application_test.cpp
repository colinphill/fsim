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
  std::vector<std::string> final_values;
  std::string vcd;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-integer-shift";
  config.project.top =
      "vhdl:work.vhdl_integer_shift(rtl)";
  config.project.time_resolution = "1ns";
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

  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  Capture capture;
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.native_cache =
      simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 12> names{
      "vhdl_integer_shift.count",
      "vhdl_integer_shift.logical_left",
      "vhdl_integer_shift.logical_right",
      "vhdl_integer_shift.arithmetic_left",
      "vhdl_integer_shift.arithmetic_right",
      "vhdl_integer_shift.rotate_left",
      "vhdl_integer_shift.rotate_right",
      "vhdl_integer_shift.integer_result",
      "vhdl_integer_shift.bounded_result",
      "vhdl_integer_shift.natural_result",
      "vhdl_integer_shift.positive_result",
      "vhdl_integer_shift.minimum_result"};
  constexpr std::array<std::uint32_t, names.size()> widths{
      32, 8, 8, 8, 8, 8, 8, 32, 32, 32, 32, 32};
  std::array<
      fsim::runtime::simir::SignalId,
      names.size()> signals{};
  std::array<
      fsim::runtime::VcdSignal,
      names.size()> vcd_signals{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, "1ns", 128);
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = simulation.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
    vcd_signals[index] = vcd.declare_signal(
        std::string{names[index]}, widths[index]);
  }
  vcd.begin(simulation.now());
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(
        vcd_signals[index],
        simulation.read_signal(signals[index]));
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
  for (const auto signal : signals) {
    capture.final_values.push_back(
        simulation.read_signal(signal).to_msb_string());
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
  assert(capture.result.status
         == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 2);
  assert((
      changes_for(capture, "vhdl_integer_shift.count")
      == std::vector<TimedValue>{
          {
              "11111111111111111111111111111111",
              0},
          {
              "00000000000000000000000000000010",
              1},
          {
              "11111111111111111111111111110111",
              2}}));
  assert((
      capture.final_values
      == std::vector<std::string>{
          "11111111111111111111111111110111",
          "00000000",
          "00000000",
          "11111111",
          "ZZZZZZZZ",
          "Z10X0000",
          "0X0000Z1",
          "00000000000000000000000000001000",
          "00000000000000000000000000000001",
          "00000000000000000000000000001000",
          "00000000000000000000000000001001",
          "10000000000000000000000000000000"}));
  assert(
      capture.vcd.find("$timescale 1ns $end")
      != std::string::npos);
  assert(capture.vcd.find("#1") != std::string::npos);
  assert(capture.vcd.find("#2") != std::string::npos);
  assert(
      capture.vcd.find(
          "b11111111111111111111111111110111")
      != std::string::npos);
  assert(
      capture.vcd.find(
          "b00000000000000000000000000001000")
      != std::string::npos);
}

std::string run_expected_failure(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  try {
    (void)simulation.run();
    assert(false && "checked VHDL integer overflow was not reported");
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    const auto message = std::string{error.what()};
    assert(
        message.find("VHDL integer arithmetic overflow")
        != std::string::npos);
    return message;
  }
  return {};
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-integer-shift-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "integer_shift.vhd";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
entity vhdl_integer_shift is
end entity;

architecture rtl of vhdl_integer_shift is
  signal value : std_logic_vector(7 downto 0);
  signal count : integer;
  signal logical_left : std_logic_vector(7 downto 0);
  signal logical_right : std_logic_vector(7 downto 0);
  signal arithmetic_left : std_logic_vector(7 downto 0);
  signal arithmetic_right : std_logic_vector(7 downto 0);
  signal rotate_left : std_logic_vector(7 downto 0);
  signal rotate_right : std_logic_vector(7 downto 0);
  signal integer_result : integer;
  signal bounded_result : integer range -3 to 4;
  signal natural_result : natural;
  signal positive_result : positive;
  signal minimum_result : integer;
begin
  value <= "10X0000Z";

  drive_count: process
  begin
    count <= -1;
    wait for 1 ns;
    count <= 2;
    wait for 1 ns;
    count <= -9;
    wait;
  end process;

  calculate: process(value, count)
    variable adjusted : integer := 0;
  begin
    adjusted := count + 1;
    integer_result <= abs adjusted;
    bounded_result <= adjusted mod 3;
    natural_result <= abs adjusted;
    positive_result <= (abs adjusted mod 100) + 1;
    minimum_result <= -2147483648;
    logical_left <= value sll count;
    logical_right <= value srl count;
    arithmetic_left <= value sla count;
    arithmetic_right <= value sra count;
    rotate_left <= value rol count;
    rotate_right <= value ror count;
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
        run_once(
            config,
            fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(
            config,
            fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(
            config,
            fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    assert(reference.result.status == cold.result.status);
    assert(reference.result.time == cold.result.time);
    assert(reference.result.delta == cold.result.delta);
    assert(reference.changes == cold.changes);
    assert(reference.final_values == cold.final_values);
    assert(reference.vcd == cold.vcd);
    assert(reference.changes == warm.changes);
    assert(reference.final_values == warm.final_values);
    assert(reference.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 3);
    assert(cold.compiled_modules == 1);
    assert(cold.native_cache.hits == 0);
    assert(cold.native_cache.misses == 1);
    assert(cold.native_cache.stores == 1);
    assert(warm.compiled_processes == 3);
    assert(warm.compiled_modules == 1);
    assert(warm.native_cache.hits == 1);
    assert(warm.native_cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
  }

  const auto failure_source =
      directory.path / "integer_overflow.vhd";
  {
    std::ofstream output(failure_source, std::ios::binary);
    output << R"(
entity vhdl_integer_shift is
end entity;

architecture rtl of vhdl_integer_shift is
  signal sink : integer;
begin
  overflow: process
    variable maximum : integer := 2147483647;
  begin
    maximum := maximum + 1;
    sink <= maximum;
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
        make_config(
            directory.path, failure_source, optimization);
    const auto interpreter_error = run_expected_failure(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled_error = run_expected_failure(
        config, fsim::app::SimulationEngine::compiled);
    assert(interpreter_error == compiled_error);
  }
  std::cout
      << "VHDL integer shift application tests passed\n";
}
