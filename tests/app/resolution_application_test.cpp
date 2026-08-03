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
#include <tuple>
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

struct BoundaryChange {
  std::string signal;
  std::string value;
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const BoundaryChange&, const BoundaryChange&) = default;
};

struct BoundaryCapture {
  fsim::runtime::RunResult paused;
  fsim::runtime::RunResult result;
  std::vector<BoundaryChange> changes;
  std::vector<std::tuple<
      std::string, fsim::runtime::SimulationTick, std::uint64_t>> outputs;
  std::vector<std::string> final_values;
  std::vector<std::string> specialization_keys;
  std::vector<std::pair<std::string, std::string>> construction_identities;
  std::string debugger;
  std::string vcd;
  std::size_t observer_changes{};
  std::size_t boundary_conversions{};
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

void verify_boundary_delays(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization) {
  const auto sv_source = directory / "mixed_timing_top.sv";
  {
    std::ofstream output(sv_source, std::ios::binary);
    output << R"(timeunit 1ps / 1ps;
module sv_transition_leaf(output wire result);
  logic drive;
  assign #(2ps, 3ps, 4ps) result = drive;
  initial begin
    drive = 1'b0;
    #10ps drive = 1'b1;
    #10ps drive = 1'bz;
  end
endmodule

module sv_region_leaf(output logic [1:0] result);
  initial begin
    result = 2'b00;
    #0 result = 2'b01;
    result <= 2'b10;
    result <= 2'b11;
    $strobe("region=%b", result);
    #1ps $stop;
  end
endmodule

module mixed_timing_top;
  logic [7:0] zero_result;
  logic [7:0] inertial_result;
  logic [7:0] transport_result;
  logic [7:0] reject_result;
  logic [7:0] transition_result;
  logic [7:0] region_result;
  logic [7:0] postponed_result;
  logic [7:0] loop_source;
  logic [7:0] loop_result;
  vhdl_timing_mid #(.Enabled(1'b1)) mid(
      .zero_result(zero_result),
      .inertial_result(inertial_result),
      .transport_result(transport_result),
      .reject_result(reject_result),
      .transition_result(transition_result),
      .region_result(region_result),
      .postponed_result(postponed_result),
      .loop_source(loop_source),
      .loop_result(loop_result));
  always @(loop_result) begin
    if (loop_result[3:0] === 4'bxxxx)
      loop_source = 8'h00;
    else if (loop_result < 8'h03)
      loop_source = loop_result + 8'h01;
  end
  initial #30ps $finish;
endmodule
)";
    assert(output.good());
  }
  const auto vhdl_source = directory / "mixed_timing_mid.vhd";
  {
    std::ofstream output(vhdl_source, std::ios::binary);
    output << R"(entity Mixed_Timing_Mid is
  generic (Enabled : boolean := false);
  port (
    Zero_Result : out std_logic_vector(3 downto 0);
    Inertial_Result : out std_logic_vector(3 downto 0);
    Transport_Result : out std_logic_vector(3 downto 0);
    Reject_Result : out std_logic_vector(3 downto 0);
    Transition_Result : out std_logic_vector(3 downto 0);
    Region_Result : out std_logic_vector(3 downto 0);
    Postponed_Result : out std_logic_vector(3 downto 0);
    Loop_Source : in std_logic_vector(3 downto 0);
    Loop_Result : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of Mixed_Timing_Mid is
begin
  Transition_Leaf : sv_transition_leaf
    port map (Result => Transition_Result);
  Region_Leaf : sv_region_leaf
    port map (Result => Region_Result);
  Postponed_Result <= transport Region_Result;
  Loop_Result <= transport Loop_Source;
  Zero_Result <= transport "1010" when Enabled else "0101";

  inertial_driver: process
  begin
    Inertial_Result <= "0000" after 5 ps;
    wait for 10 ps;
    Inertial_Result <= "1111" after 5 ps;
    wait for 1 ps;
    Inertial_Result <= "0000" after 5 ps;
    wait for 9 ps;
    Inertial_Result <= "1111" after 5 ps;
    wait;
  end process;

  transport_driver: process
  begin
    Transport_Result <= transport "0000" after 5 ps;
    wait for 10 ps;
    Transport_Result <= transport "1111" after 5 ps;
    wait for 1 ps;
    Transport_Result <= transport "0000" after 5 ps;
    wait for 9 ps;
    Transport_Result <= transport "1111" after 5 ps;
    wait;
  end process;

  reject_driver: process
  begin
    Reject_Result <= reject 2 ps inertial "0000" after 5 ps;
    wait for 10 ps;
    Reject_Result <= reject 2 ps inertial "1111" after 5 ps;
    wait for 1 ps;
    Reject_Result <= reject 2 ps inertial "0000" after 5 ps;
    wait for 9 ps;
    Reject_Result <= reject 2 ps inertial "1111" after 5 ps;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "mixed-boundary-timing";
  config.project.top = "sv:work.mixed_timing_top";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path = directory
      / (optimization == fsim::project::Optimization::o0
             ? "timing-cache-o0"
             : "timing-cache-o2");
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
      {"mixed_timing_top.mid", "vhdl:work.mixed_timing_mid(rtl)",
       std::nullopt},
      {"mixed_timing_top.mid.transition_leaf",
       "sv:work.sv_transition_leaf", std::nullopt},
      {"mixed_timing_top.mid.region_leaf",
       "sv:work.sv_region_leaf", std::nullopt},
  };
  constexpr std::array<std::string_view, 11> names{
      "mixed_timing_top.zero_result",
      "mixed_timing_top.inertial_result",
      "mixed_timing_top.transport_result",
      "mixed_timing_top.reject_result",
      "mixed_timing_top.transition_result",
      "mixed_timing_top.region_result",
      "mixed_timing_top.postponed_result",
      "mixed_timing_top.mid.region_result",
      "mixed_timing_top.mid.region_leaf.result",
      "mixed_timing_top.loop_source",
      "mixed_timing_top.loop_result"};
  const auto execute = [&](const fsim::app::SimulationEngine engine) {
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(project);
    assert(project->time_resolution == "1ps");
    BoundaryCapture capture;
    capture.boundary_conversions =
        project->design.boundary_conversions().size();
    capture.specialization_keys = project->specialization_cache_keys;
    for (const auto& specialization : project->design.specializations()) {
      if (specialization.instance == "mixed_timing_top.mid") {
        capture.construction_identities =
            specialization.parameter_identity_values;
      }
    }
    std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
    fsim::app::Simulation simulation{
        std::move(*project), config.run.max_deltas, engine};
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.cache = simulation.native_cache_statistics();
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto signal = simulation.find_signal(names[index]);
      assert(signal);
      signals[index] = *signal;
    }
    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd(vcd_output, "1ps", 64);
    std::array<fsim::runtime::VcdSignal, names.size()> vcd_signals{};
    for (std::size_t index = 0; index < names.size(); ++index) {
      vcd_signals[index] = vcd.declare_signal(
          std::string{names[index]},
          simulation.read_signal(signals[index]).width());
    }
    vcd.begin(simulation.now());
    for (std::size_t index = 0; index < names.size(); ++index) {
      vcd.change(
          vcd_signals[index], simulation.read_signal(signals[index]));
    }
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
          const auto found = std::find(
              signals.begin(), signals.end(), signal);
          if (found == signals.end()) {
            return;
          }
          const auto index = static_cast<std::size_t>(
              std::distance(signals.begin(), found));
          capture.changes.push_back({
              std::string{names[index]}, value.to_msb_string(), time, delta});
          vcd.set_time(time);
          vcd.change(vcd_signals[index], value);
        });
    const auto observer = simulation.add_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4&,
            const fsim::runtime::SimulationTick,
            const std::uint64_t) {
          if (std::find(signals.begin(), signals.end(), signal)
              != signals.end()) {
            ++capture.observer_changes;
          }
        });
    simulation.set_output_hook(
        [&](const fsim::runtime::simir::ProcessId,
            const std::string_view text,
            const bool,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
          capture.outputs.emplace_back(text, time, delta);
        });
    capture.paused = simulation.run();
    assert(!simulation.finished());
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    fsim::app::DebuggerControl debugger{
        simulation, debugger_output, debugger_error};
    debugger.execute({"show", "region_result"});
    debugger.execute({"show", "postponed_result"});
    assert(debugger_error.str().empty());
    capture.debugger = debugger_output.str();
    simulation.clear_stop();
    capture.result = simulation.run();
    assert(simulation.finished());
    simulation.remove_signal_change_hook(observer);
    vcd.flush();
    capture.vcd = vcd_output.str();
    for (const auto signal : signals) {
      capture.final_values.push_back(
          simulation.read_signal(signal).to_msb_string());
    }
    return capture;
  };

  const auto reference = execute(fsim::app::SimulationEngine::interpreter);
  const auto cold = execute(fsim::app::SimulationEngine::compiled);
  const auto warm = execute(fsim::app::SimulationEngine::compiled);
  std::ifstream edited_input(vhdl_source, std::ios::binary);
  std::ostringstream edited_text;
  edited_text << edited_input.rdbuf();
  auto edited_source = edited_text.str();
  const auto original_literal = std::string{
      "Zero_Result <= transport \"1010\""};
  const auto edited_literal = std::string{
      "Zero_Result <= transport \"0011\""};
  const auto edit_offset = edited_source.find(original_literal);
  assert(edit_offset != std::string::npos);
  edited_source.replace(
      edit_offset, original_literal.size(), edited_literal);
  {
    std::ofstream edited_output(vhdl_source, std::ios::binary);
    edited_output << edited_source;
    assert(edited_output.good());
  }
  const auto edited = execute(fsim::app::SimulationEngine::compiled);
  const auto& changes = reference.changes;

  const auto observed = [&](const std::string_view name,
                            const bool positive_only = false) {
    std::vector<std::pair<std::string, fsim::runtime::SimulationTick>> values;
    for (const auto& change : changes) {
      if (change.signal == name && (!positive_only || change.time > 0)) {
        values.emplace_back(change.value, change.time);
      }
    }
    return values;
  };
  const auto observed_with_delta = [&](const std::string_view name) {
    std::vector<std::tuple<
        std::string, fsim::runtime::SimulationTick, std::uint64_t>> values;
    for (const auto& change : changes) {
      if (change.signal == name) {
        values.emplace_back(change.value, change.time, change.delta);
      }
    }
    return values;
  };
  assert((observed(names[0])
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"0000XXXX", 0}, {"00001010", 0}}));
  assert((observed(names[1])
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"0000XXXX", 0}, {"00000000", 5},
              {"00001111", 25}}));
  assert((observed(names[2])
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"0000XXXX", 0}, {"00000000", 5},
              {"00001111", 15},
              {"00000000", 16}, {"00001111", 25}}));
  assert((observed(names[3])
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"0000XXXX", 0}, {"00000000", 5},
              {"00001111", 25}}));
  assert((observed(names[4], true)
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"00000000", 3}, {"00000001", 12},
              {"0000000Z", 24}}));
  assert((observed_with_delta(names[8])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"00", 0, 0}, {"01", 0, 0}, {"11", 0, 0}}));
  assert((observed_with_delta(names[7])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"00XX", 0, 0}, {"0011", 0, 1}}));
  assert((observed_with_delta(names[5])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"0000XXXX", 0, 0}, {"000000XX", 0, 1},
              {"00000011", 0, 2}}));
  assert((observed_with_delta(names[6])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"0000XXXX", 0, 0}, {"000000XX", 0, 2},
              {"00000011", 0, 3}}));
  assert((observed_with_delta(names[9])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"00000000", 0, 1}, {"00000001", 0, 5},
              {"00000010", 0, 9}, {"00000011", 0, 13}}));
  assert((observed_with_delta(names[10])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"0000XXXX", 0, 0}, {"00000000", 0, 4},
              {"00000001", 0, 8}, {"00000010", 0, 12},
              {"00000011", 0, 16}}));
  assert(reference.paused.status == fsim::runtime::RunStatus::stopped);
  assert(reference.paused.time == 1);
  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 30);
  assert(reference.boundary_conversions == 11);
  assert(std::ranges::any_of(
      reference.construction_identities,
      [](const auto& identity) {
        return identity.first == "enabled"
            && identity.second.starts_with("vhdlconst-v1;")
            && identity.second.find(";type=boolean;")
                != std::string::npos;
      }));
  assert(reference.observer_changes == reference.changes.size());
  assert(reference.debugger.find("region_result")
         != std::string::npos);
  assert(reference.debugger.find("postponed_result")
         != std::string::npos);
  assert(reference.vcd.find("$timescale 1ps $end")
         != std::string::npos);
  assert((reference.outputs == std::vector<std::tuple<
      std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
      {"region=11", 0, 0}}));
  assert(reference.final_values[9] == "00000011");
  assert(reference.final_values[10] == "00000011");
  for (const auto* actual : {&cold, &warm}) {
    assert(actual->paused.status == reference.paused.status);
    assert(actual->paused.time == reference.paused.time);
    assert(actual->paused.delta == reference.paused.delta);
    assert(actual->result.status == reference.result.status);
    assert(actual->result.time == reference.result.time);
    assert(actual->result.delta == reference.result.delta);
    assert(actual->changes == reference.changes);
    assert(actual->outputs == reference.outputs);
    assert(actual->final_values == reference.final_values);
    assert(actual->specialization_keys == reference.specialization_keys);
    assert(actual->construction_identities
           == reference.construction_identities);
    assert(actual->debugger == reference.debugger);
    assert(actual->vcd == reference.vcd);
    assert(actual->observer_changes == actual->changes.size());
    assert(actual->boundary_conversions == reference.boundary_conversions);
  }
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes > 0);
  assert(cold.compiled_modules > 0);
  assert(cold.cache.hits == 0);
  assert(cold.cache.misses == cold.compiled_modules);
  assert(cold.cache.stores == cold.compiled_modules);
  assert(warm.compiled_processes == cold.compiled_processes);
  assert(warm.compiled_modules == cold.compiled_modules);
  assert(warm.cache.hits == warm.compiled_modules);
  assert(warm.cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
  assert(edited.final_values[0] == "00000011");
  assert(std::equal(
      edited.final_values.begin() + 1,
      edited.final_values.end(),
      warm.final_values.begin() + 1));
  assert(edited.specialization_keys != warm.specialization_keys);
  assert(edited.construction_identities == warm.construction_identities);
  assert(edited.boundary_conversions == warm.boundary_conversions);
#if defined(FSIM_HAS_LLVM)
  assert(edited.cache.misses > 0);
  assert(edited.cache.hits + edited.cache.misses
         == edited.compiled_modules);
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
  verify_boundary_delays(
      directory.path, fsim::project::Optimization::o0);
  verify_boundary_delays(
      directory.path, fsim::project::Optimization::o2);
  return 0;
}
