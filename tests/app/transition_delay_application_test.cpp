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
  std::vector<std::string> gate_array_processes;
  std::string vcd;
  std::string debugger;
  std::string resolution;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::project::DelayMode mode) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "transition-delays";
  config.project.top = "sv:work.transition_delays";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.delay_mode = mode;
  config.run.max_deltas = 1000;
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
  for (const auto& process : project->design.processes()) {
    if (process.name.find("gate_array[") != std::string::npos) {
      capture.gate_array_processes.push_back(process.name);
    }
  }
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 13> names{
      "transition_delays.mode_output",
      "transition_delays.vector_output",
      "transition_delays.slice_output",
      "transition_delays.pulse_output",
      "transition_delays.one_output",
      "transition_delays.two_output",
      "transition_delays.gate_output",
      "transition_delays.gate_triple_output",
      "transition_delays.gate_array_output",
      "transition_delays.tri_output",
      "transition_delays.notif_output",
      "transition_delays.same_value_output",
      "transition_delays.zero_output"};
  std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, capture.resolution, 128);
  std::array<fsim::runtime::VcdSignal, names.size()> vcd_signals{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = simulation.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
    const auto width =
        names[index].find("vector") != std::string_view::npos
                || names[index].find("gate_array")
                    != std::string_view::npos
                || names[index].find("slice") != std::string_view::npos
            ? 4U
            : 1U;
    vcd_signals[index] =
        vcd.declare_signal(std::string{names[index]}, width);
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
        const auto index =
            static_cast<std::size_t>(
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
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"show", "gate_array_output"});
  debugger.execute({"show", "tri_output"});
  debugger.execute({"show", "same_value_output"});
  assert(debugger_error.str().empty());
  capture.debugger = debugger_output.str();
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

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::project::DelayMode mode,
    const std::array<fsim::runtime::SimulationTick, 5>& mode_times) {
  const auto config = config_for(
      directory, source, optimization, mode);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 100);
  assert(reference.resolution == "1ps");
  const std::vector<std::pair<
      std::string,
      fsim::runtime::SimulationTick>> expected_mode{
      {"0", mode_times[0]},
      {"1", mode_times[1]},
      {"0", mode_times[2]},
      {"Z", mode_times[3]},
      {"X", mode_times[4]}};
  assert(
      changes_for(reference, "transition_delays.mode_output")
      == expected_mode);
  assert((
      changes_for(reference, "transition_delays.vector_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"0000", 7}, {"10Z0", 23}, {"0000", 47}}));
  assert((
      changes_for(reference, "transition_delays.slice_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"Z00Z", 6}, {"Z11Z", 22}}));
  assert((
      changes_for(reference, "transition_delays.pulse_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{{"0", 7}}));
  assert((
      changes_for(reference, "transition_delays.one_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"0", 4}, {"1", 24}, {"0", 44}, {"Z", 64}, {"X", 84}}));
  assert((
      changes_for(reference, "transition_delays.two_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"0", 3}, {"1", 29}, {"0", 43}, {"Z", 63}, {"X", 83}}));
  assert(
      changes_for(reference, "transition_delays.gate_output")
      == changes_for(reference, "transition_delays.two_output"));
  const auto mode_offset =
      static_cast<fsim::runtime::SimulationTick>(mode);
  const std::vector<std::pair<
      std::string,
      fsim::runtime::SimulationTick>> expected_gate_triple{
          {"0", 4 + mode_offset},
          {"1", 21 + mode_offset},
          {"0", 44 + mode_offset},
          {"Z", 61 + mode_offset},
          {"X", 81 + mode_offset}};
  const auto actual_gate_triple = changes_for(
      reference, "transition_delays.gate_triple_output");
  if (actual_gate_triple != expected_gate_triple) {
    std::cerr << "gate triple mode "
              << static_cast<unsigned>(mode) << ':';
    for (const auto& [value, time] : actual_gate_triple) {
      std::cerr << ' ' << value << '@' << time;
    }
    std::cerr << '\n';
  }
  assert(actual_gate_triple == expected_gate_triple);
  assert((
      changes_for(reference, "transition_delays.gate_array_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"0000", 1}, {"1010", 21}, {"0000", 41}}));
  assert((
      changes_for(reference, "transition_delays.tri_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"1", 22}, {"0", 43}, {"Z", 64}, {"X", 82}}));
  assert((
      changes_for(reference, "transition_delays.notif_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"1", 2}, {"Z", 24}}));
  assert((
      changes_for(reference, "transition_delays.same_value_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"0", 5}, {"1", 25}}));
  assert((
      changes_for(reference, "transition_delays.zero_output")
      == std::vector<std::pair<
          std::string,
          fsim::runtime::SimulationTick>>{
          {"X", 0}, {"0", 0}, {"1", 20}, {"0", 40}, {"Z", 60},
          {"X", 80}}));
  assert(
      reference.vcd.find(
          "#" + std::to_string(mode_times.back()))
      != std::string::npos);
  assert(
      reference.debugger.find(
          "transition_delays.gate_array_output = 0000")
          != std::string::npos
      && reference.debugger.find(
          "transition_delays.tri_output = X")
          != std::string::npos
      && reference.debugger.find(
          "transition_delays.same_value_output = 1")
          != std::string::npos);
  assert((
      reference.gate_array_processes == std::vector<std::string>{
          "transition_delays.gate_array[3]",
          "transition_delays.gate_array[2]",
          "transition_delays.gate_array[1]",
          "transition_delays.gate_array[0]"}));

  for (const auto* actual : {&cold, &warm}) {
    assert(reference.result.status == actual->result.status);
    assert(reference.result.time == actual->result.time);
    assert(reference.changes == actual->changes);
    assert(reference.final_values == actual->final_values);
    assert(
        reference.gate_array_processes
        == actual->gate_array_processes);
    assert(reference.vcd == actual->vcd);
    assert(reference.debugger == actual->debugger);
  }
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 17);
  assert(cold.native_cache.hits == 0);
  assert(cold.native_cache.misses == 1);
  assert(cold.native_cache.stores == 1);
  assert(warm.compiled_processes == 17);
  assert(warm.native_cache.hits == 1);
  assert(warm.native_cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
}

void verify_auto_resolution_uses_all_values(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(module transition_delays;
  timeunit 1ns;
  logic source;
  wire result;
  assign #(
      1ns:2ps:3fs,
      4ns:5ps:6fs,
      7ns:8ps:9fs) result = source;
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
        directory,
        source,
        fsim::project::Optimization::o2,
        modes[index]);
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    assert(project->time_resolution == resolutions[index]);
  }
}

struct ParameterizedCapture {
  fsim::runtime::RunResult result;
  std::vector<std::tuple<
      std::string,
      std::string,
      fsim::runtime::SimulationTick>> changes;
  std::vector<std::string> specialization_keys;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

ParameterizedCapture run_parameterized(
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
  ParameterizedCapture capture;
  capture.specialization_keys = project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();
  constexpr std::array<std::string_view, 4> names{
      "parameter_delay_top.fast",
      "parameter_delay_top.slow",
      "parameter_delay_top.net_fast",
      "parameter_delay_top.net_slow"};
  std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = simulation.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
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
        capture.changes.emplace_back(
            names[index], value.to_msb_string(), time);
      });
  capture.result = simulation.run();
  return capture;
}

void verify_parameterized_delays(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1ps / 1ps;
module delay_leaf #(
    parameter int DELAY = 2,
    localparam int FALL = DELAY + 1)(
    input logic source,
    output wire result);
  assign #(DELAY, FALL, DELAY + 2) result = source;
endmodule

module net_delay_leaf #(
    parameter int DELAY = 2,
    localparam int FALL = DELAY + 1)(
    input logic source,
    output wire result);
  generate
    if (1) begin : timed
      wire #(DELAY, FALL, DELAY + 2) delayed;
      assign #1 delayed = source;
      assign result = delayed;
    end
  endgenerate
endmodule

module parameter_delay_top;
  localparam int STEP = 10;
  logic drive;
  wire fast;
  wire slow;
  wire net_fast;
  wire net_slow;
  delay_leaf #(.DELAY(2)) fast_leaf(.source(drive), .result(fast));
  delay_leaf #(.DELAY(5)) slow_leaf(.source(drive), .result(slow));
  net_delay_leaf #(.DELAY(2)) net_fast_leaf(
      .source(drive), .result(net_fast));
  net_delay_leaf #(.DELAY(5)) net_slow_leaf(
      .source(drive), .result(net_slow));
  initial begin
    drive = 1'b0;
    #STEP drive = 1'b1;
    #STEP drive = 1'b0;
    #(STEP * 2) $finish;
  end
endmodule
)";
    assert(output.good());
  }
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    auto config = config_for(
        directory,
        source,
        optimization,
        fsim::project::DelayMode::typical);
    config.project.top = "sv:work.parameter_delay_top";
    config.build.cache_path =
        directory
        / (optimization == fsim::project::Optimization::o0
               ? "parameter-cache-o0"
               : "parameter-cache-o2");
    const auto reference = run_parameterized(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_parameterized(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_parameterized(
        config, fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.time == 40);
    const auto changes_for_signal =
        [&](const std::string_view name) {
          std::vector<std::pair<
              std::string,
              fsim::runtime::SimulationTick>> result;
          for (const auto& [signal, value, time] : reference.changes) {
            if (signal == name) {
              result.emplace_back(value, time);
            }
          }
          return result;
        };
    const auto expected_fast = std::vector<std::pair<
        std::string,
        fsim::runtime::SimulationTick>>{
        {"0", 3}, {"1", 12}, {"0", 23}};
    const auto expected_slow = std::vector<std::pair<
        std::string,
        fsim::runtime::SimulationTick>>{
        {"0", 6}, {"1", 15}, {"0", 26}};
    assert(changes_for_signal("parameter_delay_top.fast") == expected_fast);
    assert((
        changes_for_signal("parameter_delay_top.net_fast")
        == std::vector<std::pair<
            std::string,
            fsim::runtime::SimulationTick>>{
            {"0", 4}, {"1", 13}, {"0", 24}}));
    assert(changes_for_signal("parameter_delay_top.slow") == expected_slow);
    assert((
        changes_for_signal("parameter_delay_top.net_slow")
        == std::vector<std::pair<
            std::string,
            fsim::runtime::SimulationTick>>{
            {"0", 7}, {"1", 16}, {"0", 27}}));
    assert(reference.changes == cold.changes);
    assert(reference.changes == warm.changes);
    assert(reference.specialization_keys == cold.specialization_keys);
    assert(reference.specialization_keys == warm.specialization_keys);
    assert(reference.specialization_keys.size() == 5);
    assert(
        reference.specialization_keys[1]
        != reference.specialization_keys[2]);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 7);
    assert(cold.native_cache.hits == 0);
    assert(cold.native_cache.misses == 5);
    assert(cold.native_cache.stores == 5);
    assert(warm.compiled_processes == 7);
    assert(warm.native_cache.hits == 5);
    assert(warm.native_cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
  }

  {
    std::ofstream output(source, std::ios::binary);
    output << R"(module net_auto_resolution;
  logic source;
  wire #2fs delayed;
  assign delayed = source;
endmodule
)";
    assert(output.good());
  }
  auto auto_config = config_for(
      directory,
      source,
      fsim::project::Optimization::o0,
      fsim::project::DelayMode::typical);
  auto_config.project.top = "sv:work.net_auto_resolution";
  fsim::diagnostic::Engine auto_diagnostics;
  const auto auto_project = fsim::app::build_project(
      auto_config, auto_diagnostics);
  assert(auto_project);
  assert(auto_project->time_resolution == "1fs");

  {
    std::ofstream output(source, std::ios::binary);
    output << R"(module invalid_parameter_delay;
  logic runtime_delay;
  wire #18446744073709551615 overflow;
  assign #1 overflow = runtime_delay;
  initial #runtime_delay $finish;
  initial #(-1) $finish;
endmodule
)";
    assert(output.good());
  }
  auto invalid_config = config_for(
      directory,
      source,
      fsim::project::Optimization::o0,
      fsim::project::DelayMode::typical);
  invalid_config.project.top = "sv:work.invalid_parameter_delay";
  fsim::diagnostic::Engine diagnostics;
  const auto invalid = fsim::app::build_project(
      invalid_config, diagnostics);
  assert(!invalid);
  assert(
      std::ranges::count_if(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVDELAY-001";
          })
      == 2);
  assert(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVDELAY-003";
          }));
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-transition-delay-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "transition_delays.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module transition_delays;
  logic mode_drive;
  logic [3:0] vector_drive;
  logic [1:0] slice_drive;
  logic pulse_drive;
  logic [3:0] gate_array_a;
  logic [0:3] gate_array_b;
  logic gate_enable;
  logic same_left;
  logic same_right;
  wire mode_output;
  wire [3:0] vector_output;
  wire [3:0] slice_output;
  wire pulse_output;
  wire one_output;
  wire two_output;
  wire gate_output;
  wire gate_triple_output;
  wire [7:4] gate_array_output;
  wire tri_output;
  wire notif_output;
  wire same_value_output;
  wire zero_output;

  assign #(
      1ps:2ps:3ps,
      4ps:5ps:6ps,
      7ps:8ps:9ps) mode_output = mode_drive;
  assign #(3ps, 7ps, 11ps) vector_output = vector_drive;
  assign #(2ps, 6ps, 10ps) slice_output[2:1] = slice_drive;
  assign #(5ps, 7ps, 9ps) pulse_output = pulse_drive;
  assign #4ps one_output = mode_drive;
  assign #(9ps, 3ps) two_output = mode_drive;
  buf #(9ps, 3ps) (gate_output, mode_drive);
  buf #(1ps:2ps:3ps, 4ps:5ps:6ps) (
      gate_triple_output, mode_drive);
  and #1ps gate_array[3:0] (
      gate_array_output, gate_array_a, gate_array_b);
  bufif1 #(2ps, 3ps, 4ps) tri_gate(
      tri_output, mode_drive, gate_enable);
  notif0 #(2ps, 3ps, 4ps) notif_gate(
      notif_output, mode_drive, gate_enable);
  assign #5ps same_value_output = same_left & same_right;
  assign #0 zero_output = mode_drive;

  initial begin
    mode_drive = 1'b0;
    vector_drive = 4'b0000;
    slice_drive = 2'b00;
    pulse_drive = 1'b0;
    gate_array_a = 4'b0000;
    gate_array_b = 4'b1111;
    gate_enable = 1'b0;
    same_left = 1'b0;
    same_right = 1'b0;
    #1ps same_left = 1'b1;
    #19ps;
    mode_drive = 1'b1;
    vector_drive = 4'b10z0;
    slice_drive = 2'b11;
    pulse_drive = 1'b1;
    gate_array_a = 4'b1010;
    gate_enable = 1'b1;
    same_right = 1'b1;
    #2ps pulse_drive = 1'b0;
    #18ps;
    mode_drive = 1'b0;
    vector_drive = 4'b0000;
    gate_array_b = 4'b0101;
    #20ps mode_drive = 1'bz;
    #20ps mode_drive = 1'bx;
    #20ps $finish;
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
        {4, 21, 44, 67, 81});
    verify_mode(
        directory.path,
        source,
        optimization,
        fsim::project::DelayMode::typical,
        {5, 22, 45, 68, 82});
    verify_mode(
        directory.path,
        source,
        optimization,
        fsim::project::DelayMode::maximum,
        {6, 23, 46, 69, 83});
  }
  verify_auto_resolution_uses_all_values(
      directory.path, source);
  verify_parameterized_delays(directory.path, source);
  std::cout << "transition delay application tests passed\n";
  return 0;
}
