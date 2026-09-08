// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "path_test_support.hpp"

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
  std::array<std::string, 28> values;
  std::string state_local;
  std::string symbol_local;
  std::string reverse_local;
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
    assert(fsim::test::has_source_dependency(
        specialization.source_dependencies, package_source));
  }

  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> state_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> symbol_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> reverse_local;
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
      } else if (local.name == "local_reverse") {
        assert((
            local.enumeration_literals
            == std::vector<std::string>{
                "idle", "load", "running", "done"}));
        reverse_local = std::pair{process.id, index};
      }
    }
  }
  assert(state_local && symbol_local && reverse_local);

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

  constexpr std::array<std::string_view, 28> paths{
      "enumeration_top.source",
      "enumeration_top.result",
      "enumeration_top.selected",
      "enumeration_top.symbol_source",
      "enumeration_top.symbol_result",
      "enumeration_top.equal_result",
      "enumeration_top.ordered_result",
      "enumeration_top.left_result",
      "enumeration_top.right_result",
      "enumeration_top.low_result",
      "enumeration_top.high_result",
      "enumeration_top.length_result",
      "enumeration_top.ascending_result",
      "enumeration_top.position_result",
      "enumeration_top.value_result",
      "enumeration_top.successor_result",
      "enumeration_top.predecessor_result",
      "enumeration_top.leftof_result",
      "enumeration_top.rightof_result",
      "enumeration_top.reverse_default",
      "enumeration_top.constrained_left",
      "enumeration_top.constrained_right",
      "enumeration_top.constrained_length",
      "enumeration_top.constrained_ascending",
      "enumeration_top.constrained_leftof",
      "enumeration_top.constrained_rightof",
      "enumeration_top.checked_target",
      "enumeration_top.waveform_target"};
  constexpr std::array<std::size_t, 28> widths{
      2, 2, 2, 2, 2, 1, 1, 2, 2, 2, 2,
      32, 1, 32, 2, 2, 2, 2, 2,
      2, 2, 2, 32, 1, 2, 2, 2, 2};
  std::array<fsim::runtime::simir::SignalId, 28> signals{};
  std::array<fsim::runtime::VcdSignal, 28> traces{};
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
  capture.reverse_local = simulation.read_process_local(
      reverse_local->first,
      reverse_local->second).to_msb_string();
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
      == std::array<std::string, 28>{
          "01", "01", "11", "10", "10", "1", "1",
          "00", "11", "00", "11",
          "00000000000000000000000000000100",
          "1",
          "00000000000000000000000000000001",
          "10", "10", "10", "10", "10",
          "11", "11", "01",
          "00000000000000000000000000000011",
          "0", "10", "10", "01", "10"}));
  assert(capture.state_local == "10");
  assert(capture.symbol_local == "10");
  assert(capture.reverse_local == "10");
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

std::string run_out_of_range_successor(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto source =
      simulation.find_signal("enumeration_top.source");
  assert(source);
  simulation.deposit_signal(
      *source,
      fsim::runtime::PackedLogic4::from_msb_string("11"));
  try {
    (void)simulation.run();
  } catch (const std::exception& error) {
    return error.what();
  }
  assert(false);
  return {};
}

std::string run_out_of_range_store(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto enable =
      simulation.find_signal("enumeration_top.store_enable");
  assert(enable);
  simulation.deposit_signal(
      *enable,
      fsim::runtime::PackedLogic4::from_msb_string("1"));
  try {
    (void)simulation.run();
  } catch (const std::exception& error) {
    return error.what();
  }
  assert(false);
  return {};
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
  subtype Active_T is State_T range Load to Done;
  subtype Reverse_T is State_T range Done downto Load;
  constant Initial_State : State_T := Load;
  constant Attribute_Default : State_T := State_T'val(1);
  constant Active_Default : Active_T := Load;
  constant State_Count : integer :=
    State_T'pos(State_T'high) - State_T'pos(State_T'low) + 1;
  constant Reverse_Left : Reverse_T := Reverse_T'left;
  constant Reverse_Length : integer :=
    State_T'pos(Reverse_T'left) - State_T'pos(Reverse_T'right) + 1;
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
  generic (
    Lower_State : State_T := Load;
    Upper_State : State_T := Done;
    Reset_State : State_T range Lower_State to Upper_State :=
      Active_T'succ(Active_Default)
  );
  port (
    Source : in State_T;
    Result : out Active_T;
    Symbol_Source : in Symbol_T;
    Symbol_Result : out Symbol_T
  );
end entity;

use work.state_types.all;
architecture rtl of enumeration_child is
begin
  result <= source when source /= Done else Reset_State;
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
  generic (
    Top_Default : Active_T := Active_T'succ(Active_Default)
  );
end entity;

use work.state_types.all;
architecture rtl of enumeration_top is
  signal source : Active_T;
  signal result : State_T;
  signal selected : State_T;
  signal symbol_source : Symbol_T;
  signal symbol_result : Symbol_T;
  signal equal_result : boolean;
  signal ordered_result : boolean;
  signal left_result : State_T;
  signal right_result : State_T;
  signal low_result : State_T;
  signal high_result : State_T;
  signal length_result : integer;
  signal ascending_result : boolean;
  signal position_result : integer;
  signal value_result : State_T;
  signal successor_result : State_T;
  signal predecessor_result : State_T;
  signal leftof_result : State_T;
  signal rightof_result : State_T;
  signal reverse_default : Reverse_T;
  signal constrained_left : Reverse_T;
  signal constrained_right : Reverse_T;
  signal constrained_length : integer;
  signal constrained_ascending : boolean;
  signal constrained_leftof : Reverse_T;
  signal constrained_rightof : Reverse_T;
  signal store_source : State_T;
  signal store_enable : boolean;
  signal checked_target : Active_T;
  signal waveform_target : Active_T;
begin
  drive : process
    variable local_state : Active_T := Active_Default;
    variable local_symbol : Symbol_T := 'A';
    variable local_reverse : Reverse_T;
  begin
    source <= local_state;
    local_state := Running;
    local_reverse := Reverse_T'rightof(local_reverse);
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
    generic map (Reset_State => Active_T'val(2))
    port map (
      Source => source,
      Result => result,
      Symbol_Source => symbol_source,
      Symbol_Result => symbol_result
    );

  equal_result <= result = Load;
  ordered_result <= source < Done;
  left_result <= State_T'left;
  right_result <= State_T'right;
  low_result <= State_T'low;
  high_result <= State_T'high;
  length_result <=
    State_T'pos(State_T'high) - State_T'pos(State_T'low) + 1;
  ascending_result <= State_T'ascending;
  position_result <= State_Alias_T'pos(source);
  value_result <= Top_Default
    when State_T'val(State_Count - 2) = Top_Default
    else State_T'left;
  successor_result <= State_T'succ(source);
  predecessor_result <= State_T'pred(State_T'high);
  leftof_result <= State_T'leftof(State_T'high);
  rightof_result <= State_T'rightof(source);
  constrained_left <= Reverse_T'left
    when Reverse_Left = Done else Load;
  constrained_right <= Reverse_T'right;
  constrained_length <=
    (State_T'pos(Reverse_T'left) - State_T'pos(Reverse_T'right) + 1)
    + Reverse_Length - 3;
  constrained_ascending <= Reverse_T'ascending;
  constrained_leftof <= Reverse_T'leftof(Load);
  constrained_rightof <= Reverse_T'rightof(Done);
  checked_target <= store_source when store_enable else Load;
  waveform_target <= transport
    Load after 1 ns, Running after 2 ns;
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
    assert(reference.reverse_local == cold.reverse_local);
    assert(reference.debugger_output == cold.debugger_output);
    assert(reference.vcd == cold.vcd);
    assert(cold.values == warm.values);
    assert(cold.state_local == warm.state_local);
    assert(cold.symbol_local == warm.symbol_local);
    assert(cold.reverse_local == warm.reverse_local);
    assert(cold.debugger_output == warm.debugger_output);
    assert(cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 25);
    assert(cold.compiled_modules == 2);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 2);
    assert(cold.cache.stores == 2);
    assert(warm.compiled_processes == 25);
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
    assert(changed.compiled_processes == 25);
    assert(changed.compiled_modules == 2);
    assert(changed.cache.hits == 0);
    assert(changed.cache.misses == 2);
    assert(changed.cache.stores == 2);
#endif
    const auto reference_failure =
        run_out_of_range_successor(
            config,
            fsim::app::SimulationEngine::interpreter);
    const auto compiled_failure =
        run_out_of_range_successor(
            config,
            fsim::app::SimulationEngine::compiled);
    assert(
        reference_failure.find(
            "VHDL integer subtype range check failed")
        != std::string::npos);
    assert(
        compiled_failure.find(
            "VHDL integer subtype range check failed")
        != std::string::npos);
    const auto reference_store_failure =
        run_out_of_range_store(
            config,
            fsim::app::SimulationEngine::interpreter);
    const auto compiled_store_failure =
        run_out_of_range_store(
            config,
            fsim::app::SimulationEngine::compiled);
    assert(
        reference_store_failure.find(
            "VHDL integer subtype range check failed")
        != std::string::npos);
    assert(
        compiled_store_failure.find(
            "VHDL integer subtype range check failed")
        != std::string::npos);
    write_package("revision one");
  }

  std::cout << "VHDL enumeration application tests passed\n";
  return 0;
}
