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
#include <optional>
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

struct Capture {
  fsim::runtime::RunResult result;
  std::array<std::string, 7> values;
  std::string state_local;
  std::string symbol_local;
  std::string debugger_output;
  std::string vcd;
  std::vector<std::string> specialization_keys;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_source,
    const std::filesystem::path& child_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-enumeration";
  config.project.top = "vhdl:work.enumeration_top(rtl)";
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
  sources.compilation_unit = "file";
  sources.files = {
      package_source, child_source, top_source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const std::filesystem::path& package_source,
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
  assert(project->design.specializations().size() == 2);
  for (const auto& specialization :
       project->design.specializations()) {
    assert(std::find(
               specialization.source_dependencies.begin(),
               specialization.source_dependencies.end(),
               package_source.string())
           != specialization.source_dependencies.end());
  }

  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> state_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> symbol_local;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      const auto& local = process.debug_locals[index];
      if (local.name == "local_state") {
        assert((
            local.enumeration_literals
            == std::vector<std::string>{
                "idle", "load", "running", "done"}));
        state_local = std::pair{process.id, index};
      } else if (local.name == "local_symbol") {
        assert((
            local.enumeration_literals
            == std::vector<std::string>{"'A'", "'B'", "'C'"}));
        symbol_local = std::pair{process.id, index};
      }
    }
  }
  assert(state_local && symbol_local);

  Capture capture;
  capture.specialization_keys =
      project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 7> paths{
      "enumeration_top.source",
      "enumeration_top.result",
      "enumeration_top.selected",
      "enumeration_top.symbol_source",
      "enumeration_top.symbol_result",
      "enumeration_top.equal_result",
      "enumeration_top.ordered_result"};
  constexpr std::array<std::size_t, 7> widths{
      2, 2, 2, 2, 2, 1, 1};
  std::array<fsim::runtime::simir::SignalId, 7> signals{};
  std::array<fsim::runtime::VcdSignal, 7> traces{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{paths[index]}, widths[index]);
  }
  vcd.begin();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(
        traces[index], simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        const auto found =
            std::find(signals.begin(), signals.end(), signal);
        if (found == signals.end()) {
          return;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(signals.begin(), found));
        vcd.set_time(time);
        vcd.change(traces[index], value);
      });

  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  capture.state_local = simulation.read_process_local(
      state_local->first, state_local->second).to_msb_string();
  capture.symbol_local = simulation.read_process_local(
      symbol_local->first, symbol_local->second).to_msb_string();
  {
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    fsim::app::DebuggerControl debugger{
        simulation, debugger_output, debugger_error};
    debugger.execute({"show", "source"});
    debugger.execute({"show", "selected"});
    debugger.execute({"show", "symbol_result"});
    assert(debugger_error.str().empty());
    capture.debugger_output = debugger_output.str();
  }
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_capture(const Capture& capture) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert((
      capture.values
      == std::array<std::string, 7>{
          "01", "01", "11", "10", "10", "1", "1"}));
  assert(capture.state_local == "10");
  assert(capture.symbol_local == "10");
  assert(
      capture.debugger_output.find("source = load (01)")
      != std::string::npos);
  assert(
      capture.debugger_output.find("selected = done (11)")
      != std::string::npos);
  assert(
      capture.debugger_output.find("symbol_result = 'C' (10)")
      != std::string::npos);
  assert(capture.vcd.find("b01") != std::string::npos);
  assert(capture.vcd.find("b10") != std::string::npos);
  assert(capture.vcd.find("b11") != std::string::npos);
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-enumeration-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto package_source =
      directory.path / "state_types.vhd";
  const auto child_source =
      directory.path / "enumeration_child.vhd";
  const auto top_source =
      directory.path / "enumeration_top.vhd";

  const auto write_package =
      [&](const std::string_view revision) {
        std::ofstream output{package_source};
        output << "-- " << revision << R"(
package State_Types is
  type State_T is (Idle, Load, Running, Done);
  subtype State_Alias_T is State_T;
  constant Initial_State : State_T := Load;
  type Symbol_T is ('A', 'B', 'C');
end package;
)";
        assert(output.good());
      };
  write_package("revision one");
  {
    std::ofstream output{child_source};
    output << R"(
use work.state_types.all;
entity Enumeration_Child is
  generic (Reset_State : State_T := Running);
  port (
    Source : in State_T;
    Result : out State_T;
    Symbol_Source : in Symbol_T;
    Symbol_Result : out Symbol_T
  );
end entity;

use work.state_types.all;
architecture rtl of enumeration_child is
begin
  result <= source when source /= Idle else Reset_State;
  symbol_result <= symbol_source;
end architecture;
)";
    assert(output.good());
  }
  {
    std::ofstream output{top_source};
    output << R"(
use work.state_types.all;
entity Enumeration_Top is
end entity;

use work.state_types.all;
architecture rtl of enumeration_top is
  signal source : State_Alias_T;
  signal result : State_T;
  signal selected : State_T;
  signal symbol_source : Symbol_T;
  signal symbol_result : Symbol_T;
  signal equal_result : boolean;
  signal ordered_result : boolean;
begin
  drive : process
    variable local_state : State_T := Initial_State;
    variable local_symbol : Symbol_T := 'A';
  begin
    source <= local_state;
    local_state := Running;
    case local_state is
      when Running =>
        selected <= state_types.Done;
      when others =>
        selected <= Idle;
    end case;
    local_symbol := 'C';
    symbol_source <= local_symbol;
    wait;
  end process;

  child : entity work.Enumeration_Child(rtl)
    generic map (Reset_State => Running)
    port map (
      Source => source,
      Result => result,
      Symbol_Source => symbol_source,
      Symbol_Result => symbol_result
    );

  equal_result <= result = Load;
  ordered_result <= source < Done;
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path,
        package_source,
        child_source,
        top_source,
        optimization);
    const auto reference = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    verify_capture(cold);
    verify_capture(warm);
    assert(reference.result.status == cold.result.status);
    assert(reference.result.time == cold.result.time);
    assert(reference.result.delta == cold.result.delta);
    assert(reference.values == cold.values);
    assert(reference.state_local == cold.state_local);
    assert(reference.symbol_local == cold.symbol_local);
    assert(reference.debugger_output == cold.debugger_output);
    assert(reference.vcd == cold.vcd);
    assert(cold.values == warm.values);
    assert(cold.state_local == warm.state_local);
    assert(cold.symbol_local == warm.symbol_local);
    assert(cold.debugger_output == warm.debugger_output);
    assert(cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 5);
    assert(cold.compiled_modules == 2);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 2);
    assert(cold.cache.stores == 2);
    assert(warm.compiled_processes == 5);
    assert(warm.compiled_modules == 2);
    assert(warm.cache.hits == 2);
    assert(warm.cache.misses == 0);
    assert(warm.cache.stores == 0);
#endif

    write_package("revision two");
    const auto changed = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    verify_capture(changed);
    assert(
        changed.specialization_keys
        != warm.specialization_keys);
#if defined(FSIM_HAS_LLVM)
    assert(changed.compiled_processes == 5);
    assert(changed.compiled_modules == 2);
    assert(changed.cache.hits == 0);
    assert(changed.cache.misses == 2);
    assert(changed.cache.stores == 2);
#endif
    write_package("revision one");
  }

  std::cout << "VHDL enumeration application tests passed\n";
  return 0;
}
