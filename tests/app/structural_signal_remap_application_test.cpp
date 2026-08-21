// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
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
  fsim::runtime::RunResult run;
  std::array<std::string, 6> values;
  std::size_t process_count{};
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine,
    const std::string_view root = "structural_signal_remap_app",
    const std::optional<fsim::runtime::SimulationTick> until = std::nullopt) {
  fsim::app::Simulation simulation{std::move(project), 1000, engine};
  const std::array suffixes{
      std::string_view{"source_a"},
      std::string_view{"source_b"},
      std::string_view{"result_a"},
      std::string_view{"result_b"},
      std::string_view{"a.result"},
      std::string_view{"b.result"},
  };
  std::array<fsim::runtime::simir::SignalId, suffixes.size()> signals{};
  for (std::size_t index = 0; index < suffixes.size(); ++index) {
    const auto path = std::string{root} + "." + std::string{suffixes[index]};
    const auto signal = simulation.find_signal(path);
    assert(signal);
    signals[index] = *signal;
  }

  Capture capture;
  capture.process_count = simulation.design_ir().processes().size();
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.run = simulation.run(until);
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index]
        = simulation.read_signal(signals[index]).to_msb_string();
  }
  return capture;
}

void test_vhdl_projected_slice_level(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-structural-signal-remap-application-test";
  config.project.top = "vhdl:work.structural_signal_remap_vhdl_app(test)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path
      = directory
      / (optimization == fsim::project::Optimization::o0
              ? "vhdl-cache-o0"
              : "vhdl-cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project = fsim::app::build_project(config, diagnostics);
  auto compiled_project = fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project && compiled_project);

  constexpr std::string_view root = "structural_signal_remap_vhdl_app";
  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter,
      root,
      4U);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled,
      root,
      4U);
  assert(compiled.run.status == reference.run.status);
  assert(compiled.run.time == reference.run.time);
  assert(compiled.run.delta == reference.run.delta);
  assert(compiled.run.callbacks_executed
      <= reference.run.callbacks_executed);
  assert(reference.run.callbacks_executed
          - compiled.run.callbacks_executed
      <= compiled.compiled_processes);
  assert(compiled.values == reference.values);
  assert((reference.values == std::array<std::string, 6>{
                                  "10100101",
                                  "00111100",
                                  "11111111",
                                  "01100110",
                                  "11111111",
                                  "01100110"}));
#if defined(FSIM_HAS_LLVM)
  assert(reference.process_count == 131);
  assert(compiled.compiled_processes == 4);
  assert(compiled.compiled_modules == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_level(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "structural-signal-remap-application-test";
  config.project.top = "sv:work.structural_signal_remap_app";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path
      = directory
      / (optimization == fsim::project::Optimization::o0
              ? "cache-o0"
              : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2005";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project = fsim::app::build_project(config, diagnostics);
  auto compiled_project = fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project && compiled_project);

  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  assert(reference.run.status == fsim::runtime::RunStatus::stopped);
  assert(compiled.run.status == reference.run.status);
  assert(compiled.run.time == reference.run.time);
  assert(compiled.run.delta == reference.run.delta);
  assert(compiled.run.callbacks_executed == reference.run.callbacks_executed);
  assert(compiled.values == reference.values);
  assert((reference.values == std::array<std::string, 6>{
                                  "10100101",
                                  "00111100",
                                  "00000010",
                                  "01101001",
                                  "00000010",
                                  "01101001"}));
#if defined(FSIM_HAS_LLVM)
  assert(reference.process_count == 3);
  assert(compiled.compiled_processes == 3);
  assert(compiled.compiled_modules == 3);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

}  // namespace

int main() {
  const auto nonce
      = std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-structural-signal-remap-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "structural_signal_remap.sv";
  {
    std::ofstream output(source);
    output << R"(
module structural_remap_leaf(
  input  logic       clock,
  input  logic [7:0] source,
  output logic [7:0] result
);
  always_ff @(posedge clock) begin
    result <= (source ^ 8'h5a) + 8'h03;
  end
endmodule

module structural_signal_remap_app;
  logic       clock;
  logic [7:0] source_a;
  logic [7:0] source_b;
  logic [7:0] result_a;
  logic [7:0] result_b;

  structural_remap_leaf a(clock, source_a, result_a);
  structural_remap_leaf b(clock, source_b, result_b);

  initial begin
    clock = 1'b0;
    source_a = 8'h12;
    source_b = 8'hc3;
    #1;
    clock = 1'b1;
    #1;
    clock = 1'b0;
    source_a = 8'ha5;
    source_b = 8'h3c;
    #1;
    clock = 1'b1;
    #1;
    $finish;
  end
endmodule
)";
  }
  const auto vhdl_source
      = directory.path / "structural_signal_remap.vhd";
  {
    std::ofstream output(vhdl_source);
    output << R"(
library ieee;
use ieee.std_logic_1164.all;

entity projected_slice_leaf is
  port (
    clock  : in  std_logic;
    source : in  std_logic_vector(7 downto 0);
    result : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of projected_slice_leaf is
begin
  process (clock)
  begin
    if rising_edge(clock) then
      assert not is_x(source)
        report "structurally shared input contains an unknown value"
        severity warning;
      result(3 downto 0) <= source(3 downto 0) xor "1010";
      result(7 downto 4) <= source(7 downto 4) xor "0101";
    end if;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity structural_signal_remap_vhdl_app is
end entity;

architecture test of structural_signal_remap_vhdl_app is
  signal clock    : std_logic := '0';
  signal source_a : std_logic_vector(7 downto 0) := x"12";
  signal source_b : std_logic_vector(7 downto 0) := x"c3";
  signal result_a : std_logic_vector(7 downto 0);
  signal result_b : std_logic_vector(7 downto 0);
  signal result_c : std_logic_vector(7 downto 0);
  signal result_d : std_logic_vector(7 downto 0);
begin
  a : entity work.projected_slice_leaf
    port map (clock => clock, source => source_a, result => result_a);
  b : entity work.projected_slice_leaf
    port map (clock => clock, source => source_b, result => result_b);
  c : entity work.projected_slice_leaf
    port map (clock => clock, source => source_a, result => result_c);
  d : entity work.projected_slice_leaf
    port map (clock => clock, source => source_b, result => result_d);

  dummy_processes : for index in 0 to 125 generate
    dormant : process
    begin
      wait;
    end process;
  end generate;

  stimulus : process
  begin
    wait for 1 ns;
    clock <= '1';
    wait for 1 ns;
    clock <= '0';
    source_a <= x"a5";
    source_b <= x"3c";
    wait for 1 ns;
    clock <= '1';
    wait for 1 ns;
    wait;
  end process;
end architecture;
)";
  }

  test_level(
      directory.path, source, fsim::project::Optimization::o0);
  test_level(
      directory.path, source, fsim::project::Optimization::o2);
  test_vhdl_projected_slice_level(
      directory.path, vhdl_source, fsim::project::Optimization::o0);
  test_vhdl_projected_slice_level(
      directory.path, vhdl_source, fsim::project::Optimization::o2);
  return 0;
}
