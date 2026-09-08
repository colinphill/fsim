// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

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
  std::array<std::string, 12> values;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

struct ViewCapture {
  fsim::runtime::RunResult result;
  std::string value;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-composite-operations";
  config.project.top = "vhdl:work.composite_operations(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path = directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0" : "cache-o2");
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
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  constexpr std::array<std::string_view, 12> paths{
      "composite_operations.record_result",
      "composite_operations.pair_result",
      "composite_operations.converted_result",
      "composite_operations.selected_result",
      "composite_operations.legacy_concat",
      "composite_operations.record_equal",
      "composite_operations.record_different",
      "composite_operations.pair_equal",
      "composite_operations.length_different",
      "composite_operations.match_equal",
      "composite_operations.match_different",
      "composite_operations.case_result"};
  std::array<fsim::runtime::simir::SignalId, paths.size()> signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }
  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  return capture;
}

void verify_capture(const Capture& capture) {
  assert(capture.result.status == fsim::runtime::RunStatus::completed);
  assert((capture.values == std::array<std::string, 12>{
      "10110", "1011001001", "1011001001", "10110", "10H-",
      "1", "1", "1", "1", "1", "1", "01"}));
}

fsim::project::Config make_view_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-2019-view-execution";
  config.project.top = "vhdl:work.view_execution_top(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path = directory
      / (optimization == fsim::project::Optimization::o0
             ? "view-cache-o0" : "view-cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2019";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

ViewCapture run_view_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto link = simulation.find_signal("view_execution_top.link");
  assert(link);
  ViewCapture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  capture.result = simulation.run();
  capture.value = simulation.read_signal(*link).to_msb_string();
  return capture;
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-composite-operations-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "composite_operations.vhd";
  const auto view_source = directory.path / "view_execution.vhd";
  {
    std::ofstream output{source};
    output << R"(
library ieee;
use ieee.std_logic_1164.all;
entity Composite_Operations is end entity;
architecture rtl of composite_operations is
  type Mode_T is (Idle, Ready, Busy);
  type Cell_T is record
    Flag : bit;
    Mode : Mode_T;
    Data : bit_vector(1 downto 0);
  end record;
  type Pair_T is array (0 to 1) of Cell_T;
  type Bits_Base_T is array (natural range <>) of bit;
  subtype Short_T is Bits_Base_T(0 to 1);
  subtype Long_T is Bits_Base_T(3 downto 0);
  signal Record_Result : Cell_T;
  signal Pair_Result : Pair_T;
  signal Converted_Result : Pair_T;
  signal Selected_Result : Cell_T;
  signal Legacy_Concat : std_logic_vector(3 downto 0);
  signal Record_Equal, Record_Different, Pair_Equal : boolean;
  signal Length_Different, Match_Equal, Match_Different : boolean;
  signal Case_Result : Mode_T;
begin
  exercise : process
    variable Left_Cell : Cell_T :=
      (Flag => '1', Mode => Ready, Data => "10");
    variable Right_Cell : Cell_T :=
      (Flag => '0', Mode => Busy, Data => "01");
    variable Pair_Value : Pair_T;
    variable Short_Value : Short_T := "10";
    variable Long_Value : Long_T := "1001";
    variable Logic_Value : std_logic_vector(3 downto 0) := "1LH-";
  begin
    Pair_Value := Left_Cell & Right_Cell;
    Pair_Value(1) := Right_Cell;
    Pair_Value(0).Mode := Ready;
    Record_Result <= Left_Cell when true else Right_Cell;
    Pair_Result <= Pair_Value;
    Converted_Result <= Pair_T(Pair_Value);
    Selected_Result <= Pair_Value(0);
    Legacy_Concat <= '1' & "0H" & '-';
    Record_Equal <= Left_Cell = Pair_Value(0);
    Record_Different <= Left_Cell /= Right_Cell;
    Pair_Equal <= Pair_Value = (Left_Cell & Right_Cell);
    Length_Different <= Short_Value /= Long_Value;
    Match_Equal <= Logic_Value ?= "1---";
    Match_Different <= Logic_Value ?/= "0---";
    case Right_Cell.Mode is
      when Busy => Case_Result <= Pair_Value(0).Mode;
      when others => Case_Result <= Idle;
    end case;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  {
    std::ofstream output{view_source};
    output << R"(
package view_execution_types is
  type request_bus is record
    request : bit;
    response : bit;
  end record;
  view initiator of request_bus is
    request : out;
    response : in;
  end view;
end package;

use work.view_execution_types.all;
entity view_execution_leaf is
  port (channel : view initiator);
end entity;
architecture rtl of view_execution_leaf is
begin
  copy : process(all)
    variable staged : request_bus;
  begin
    staged := channel;
    staged.request := staged.response;
    channel.request <= staged.request;
  end process;
end architecture;

entity view_execution_top is end entity;
use work.view_execution_types.all;
architecture rtl of view_execution_top is
  signal link : request_bus;
begin
  link.response <= '1';
  child : entity work.view_execution_leaf(rtl)
    port map (channel => link);
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(directory.path, source, optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    verify_capture(cold);
    verify_capture(warm);
    assert(reference.result.time == cold.result.time);
    assert(reference.result.delta == cold.result.delta);
    assert(reference.values == cold.values && cold.values == warm.values);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 1 && cold.compiled_modules == 1);
    assert(cold.cache.hits == 0 && cold.cache.misses == 1);
    assert(warm.compiled_processes == 1 && warm.compiled_modules == 1);
    assert(warm.cache.hits == 1 && warm.cache.misses == 0);
#endif

    const auto view_config = make_view_config(
        directory.path, view_source, optimization);
    const auto view_reference = run_view_once(
        view_config, fsim::app::SimulationEngine::interpreter);
    const auto view_cold = run_view_once(
        view_config, fsim::app::SimulationEngine::compiled);
    const auto view_warm = run_view_once(
        view_config, fsim::app::SimulationEngine::compiled);
    assert(view_reference.result.status
        == fsim::runtime::RunStatus::completed);
    assert(view_reference.value == "11");
    assert(view_reference.result.time == view_cold.result.time);
    assert(view_reference.result.delta == view_cold.result.delta);
    assert(view_reference.value == view_cold.value
        && view_cold.value == view_warm.value);
#if defined(FSIM_HAS_LLVM)
    assert(view_cold.compiled_processes == 2
        && view_cold.compiled_modules == 2);
    assert(view_cold.cache.hits == 0 && view_cold.cache.misses == 2);
    assert(view_warm.compiled_processes == 2
        && view_warm.compiled_modules == 2);
    assert(view_warm.cache.hits == 2 && view_warm.cache.misses == 0);
#endif
  }
  std::cout << "VHDL composite operation application tests passed\n";
  return 0;
}
