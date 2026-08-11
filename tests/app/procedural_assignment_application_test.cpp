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
#include <set>
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
  std::string debugger;
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
  config.project.name = "procedural-assignments";
  config.project.top = "sv:work.procedural_assignments";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
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
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();

  auto& vpi = simulation.systemverilog_vpi_objects();
  const auto vpi_root = vpi.find("procedural_assignments");
  assert(vpi_root);
  const auto process_iterator = vpi.iterate_children(vpi_root.value->handle);
  assert(process_iterator);
  std::set<std::string> process_names;
  bool saw_procedural_driver{};
  bool saw_disambiguated_driver{};
  while (true) {
    const auto child = vpi.scan(process_iterator.value);
    if (child.error
        == fsim::runtime::SystemVerilogVpiIteratorError::End) {
      break;
    }
    assert(child);
    const auto info = vpi.lookup(child.value);
    assert(info);
    if (info.value->kind
        != fsim::runtime::SystemVerilogVpiObjectKind::Process) {
      continue;
    }
    assert(process_names.insert(info.value->name).second);
    saw_procedural_driver
        = saw_procedural_driver
        || info.value->name == "$procedural_assign_0";
    saw_disambiguated_driver
        = saw_disambiguated_driver
        || info.value->name.starts_with("$process_");
  }
  assert(vpi.release_iterator(process_iterator.value)
      == fsim::runtime::SystemVerilogVpiIteratorError::None);
  assert(saw_procedural_driver && saw_disambiguated_driver);

  constexpr std::array<std::string_view, 69> names {
      "procedural_assignments.delayed_nba",
      "procedural_assignments.delayed_blocking",
      "procedural_assignments.event_blocking",
      "procedural_assignments.event_nba",
      "procedural_assignments.wildcard_nba",
      "procedural_assignments.same_slot",
      "procedural_assignments.overlap",
      "procedural_assignments.reverse_overlap",
      "procedural_assignments.zero_slot",
      "procedural_assignments.equal_deadline",
      "procedural_assignments.local_result",
      "procedural_assignments.dynamic_compound",
      "procedural_assignments.dynamic_partial",
      "procedural_assignments.dynamic_unknown",
      "procedural_assignments.dynamic_oob",
      "procedural_assignments.dynamic_nba",
      "procedural_assignments.update_calls",
      "procedural_assignments.expr_value",
      "procedural_assignments.post_result",
      "procedural_assignments.pre_result",
      "procedural_assignments.selected_post_result",
      "procedural_assignments.expr_calls",
      "procedural_assignments.forced_selected",
      "procedural_assignments.force_first",
      "procedural_assignments.force_masked",
      "procedural_assignments.force_partial",
      "procedural_assignments.force_released",
      "procedural_assignments.forced_whole",
      "procedural_assignments.whole_masked",
      "procedural_assignments.whole_released",
      "procedural_assignments.chained_target",
      "procedural_assignments.delayed_compound",
      "procedural_assignments.event_compound",
      "procedural_assignments.event_vector",
      "procedural_assignments.cross_slot",
      "procedural_assignments.inactive_nba",
      "procedural_assignments.dynamic_forced",
      "procedural_assignments.dynamic_force_masked",
      "procedural_assignments.dynamic_force_released",
      "procedural_assignments.edge_positive",
      "procedural_assignments.edge_negative",
      "procedural_assignments.edge_mixed_state",
      "procedural_assignments.proc_target",
      "procedural_assignments.proc_initial",
      "procedural_assignments.proc_reactive",
      "procedural_assignments.proc_replaced",
      "procedural_assignments.proc_still_replaced",
      "procedural_assignments.proc_deassigned",
      "procedural_assignments.proc_active",
      "procedural_assignments.proc_selected",
      "procedural_assignments.proc_selected_initial",
      "procedural_assignments.proc_selected_reactive",
      "procedural_assignments.proc_selected_after",
      "procedural_assignments.concat_blocking",
      "procedural_assignments.concat_forced",
      "procedural_assignments.concat_released",
      "procedural_assignments.concat_initial",
      "procedural_assignments.concat_reactive",
      "procedural_assignments.concat_deassigned",
      "procedural_assignments.concat_index_capture",
      "procedural_assignments.wide_proc_initial_high",
      "procedural_assignments.wide_proc_initial_low",
      "procedural_assignments.wide_proc_reactive_high",
      "procedural_assignments.wide_proc_reactive_low",
      "procedural_assignments.wide_proc_deassigned_high",
      "procedural_assignments.wide_proc_deassigned_low",
      "procedural_assignments.wide_selected_initial",
      "procedural_assignments.wide_selected_reactive",
      "procedural_assignments.wide_selected_deassigned"
  };
  constexpr std::array<std::uint32_t, names.size()> widths {
      1, 1, 1, 1, 1, 1, 4, 4, 1, 1, 1,
      16, 16, 16, 16, 16, 32,
      8, 8, 8, 1, 32,
      4, 4, 4, 4, 4, 4, 4, 4, 8, 4, 4, 4,
      1, 3, 4, 4, 4, 1, 1, 2,
      4, 4, 4, 4, 4, 4, 4, 8, 8, 8, 8,
      8, 8, 8, 8, 8, 8, 3,
      64, 64, 64, 64, 64, 64, 64, 64, 64
  };
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
        std::string{names[index]}, widths[index]);
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
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"show", "dynamic_partial"});
  debugger.execute({"show", "force_masked"});
  debugger.execute({"show", "force_partial"});
  debugger.execute({ "show", "proc_active" });
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

void verify_reference(const Capture& capture) {
  using TimedValue =
      std::pair<std::string, fsim::runtime::SimulationTick>;
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 10);
  assert(capture.resolution == "1ps");
  assert((
      changes_for(
          capture, "procedural_assignments.delayed_nba")
      == std::vector<TimedValue>{{"0", 5}}));
  assert((
      changes_for(
          capture, "procedural_assignments.delayed_blocking")
      == std::vector<TimedValue>{{"1", 3}}));
  assert((
      changes_for(
          capture, "procedural_assignments.event_blocking")
      == std::vector<TimedValue>{{"1", 3}}));
  assert((
      changes_for(capture, "procedural_assignments.event_nba")
      == std::vector<TimedValue>{{"1", 3}}));
  assert((
      changes_for(capture, "procedural_assignments.wildcard_nba")
      == std::vector<TimedValue>{{"0", 4}}));
  assert((
      changes_for(capture, "procedural_assignments.same_slot")
      == std::vector<TimedValue>{{"1", 0}}));
  assert((
      changes_for(capture, "procedural_assignments.overlap")
      == std::vector<TimedValue>{{"1110", 0}}));
  assert((
      changes_for(
          capture, "procedural_assignments.reverse_overlap")
      == std::vector<TimedValue>{{"1010", 0}}));
  assert((
      changes_for(capture, "procedural_assignments.zero_slot")
      == std::vector<TimedValue>{{"1", 0}}));
  assert((
      changes_for(capture, "procedural_assignments.cross_slot")
      == std::vector<TimedValue>{{"1", 0}}));
  assert((
      changes_for(capture, "procedural_assignments.inactive_nba")
      == std::vector<TimedValue>{
          {"000", 0}, {"010", 0}, {"001", 0}}));
  assert((
      changes_for(
          capture, "procedural_assignments.equal_deadline")
      == std::vector<TimedValue>{{"1", 6}}));
  assert((
      changes_for(capture, "procedural_assignments.local_result")
      == std::vector<TimedValue>{{"1", 2}}));
  const auto final_value = [&](const std::string_view name) {
    const auto found = std::ranges::find(
        capture.final_values, name, &std::pair<std::string, std::string>::first);
    assert(found != capture.final_values.end());
    return found->second;
  };
  assert(final_value("procedural_assignments.dynamic_compound")
         == "0001001000001000");
  assert(final_value("procedural_assignments.dynamic_partial")
         == "1001001000110100");
  assert(final_value("procedural_assignments.dynamic_unknown")
         == "0101011001111000");
  assert(final_value("procedural_assignments.dynamic_oob")
         == "1001101010111100");
  assert(final_value("procedural_assignments.dynamic_nba")
         == "1001001000110100");
  assert(final_value("procedural_assignments.update_calls")
         == "00000000000000000000000000000001");
  assert(final_value("procedural_assignments.expr_value")
         == "00000110");
  assert(final_value("procedural_assignments.post_result")
         == "00000101");
  assert(final_value("procedural_assignments.pre_result")
         == "00000111");
  assert(final_value("procedural_assignments.selected_post_result")
         == "1");
  assert(final_value("procedural_assignments.expr_calls")
         == "00000000000000000000000000000001");
  assert(final_value("procedural_assignments.force_first") == "1110");
  assert(final_value("procedural_assignments.force_masked") == "0111");
  assert(final_value("procedural_assignments.force_partial") == "0011");
  assert(final_value("procedural_assignments.force_released") == "0001");
  assert(final_value("procedural_assignments.forced_selected") == "0001");
  assert(final_value("procedural_assignments.whole_masked") == "1010");
  assert(final_value("procedural_assignments.whole_released") == "0101");
  assert(final_value("procedural_assignments.forced_whole") == "0101");
  assert(final_value("procedural_assignments.chained_target")
         == "00110000");
  assert(final_value("procedural_assignments.delayed_compound") == "0010");
  assert(final_value("procedural_assignments.event_compound") == "0011");
  assert(final_value("procedural_assignments.event_vector") == "0000");
  assert(final_value("procedural_assignments.dynamic_forced") == "0001");
  assert(
      final_value("procedural_assignments.dynamic_force_masked") == "0101");
  assert(
      final_value("procedural_assignments.dynamic_force_released") == "0001");
  assert(final_value("procedural_assignments.edge_positive") == "1");
  assert(final_value("procedural_assignments.edge_negative") == "1");
  assert(final_value("procedural_assignments.edge_mixed_state") == "11");
  assert(final_value("procedural_assignments.proc_target") == "1001");
  assert(final_value("procedural_assignments.proc_initial") == "0011");
  assert(final_value("procedural_assignments.proc_reactive") == "1010");
  assert(final_value("procedural_assignments.proc_replaced") == "0101");
  assert(
      final_value("procedural_assignments.proc_still_replaced") == "0101");
  assert(final_value("procedural_assignments.proc_deassigned") == "0101");
  assert(final_value("procedural_assignments.proc_active") == "1011");
  assert(final_value("procedural_assignments.proc_selected") == "00101000");
  assert(
      final_value("procedural_assignments.proc_selected_initial")
      == "11010111");
  assert(
      final_value("procedural_assignments.proc_selected_reactive")
      == "11101011");
  assert(
      final_value("procedural_assignments.proc_selected_after")
      == "00101000");
  assert(final_value("procedural_assignments.concat_blocking") == "10101011");
  assert(final_value("procedural_assignments.concat_forced") == "00111100");
  assert(final_value("procedural_assignments.concat_released") == "01010101");
  assert(final_value("procedural_assignments.concat_initial") == "10010110");
  assert(final_value("procedural_assignments.concat_reactive") == "01101001");
  assert(final_value("procedural_assignments.concat_deassigned") == "01101001");
  assert(final_value("procedural_assignments.concat_index_capture") == "101");
  assert(final_value("procedural_assignments.wide_proc_initial_high")
      == "0000000100100011010001010110011110001001101010111100110111101111");
  assert(final_value("procedural_assignments.wide_proc_initial_low")
      == "1111111011011100101110101001100001110110010101000011001000010000");
  assert(final_value("procedural_assignments.wide_proc_reactive_high")
      == "1000100110101011110011011110111100000001001000110100010101100111");
  assert(final_value("procedural_assignments.wide_proc_reactive_low")
      == "0111011001010100001100100001000011111110110111001011101010011000");
  assert(final_value("procedural_assignments.wide_proc_deassigned_high")
      == final_value("procedural_assignments.wide_proc_reactive_high"));
  assert(final_value("procedural_assignments.wide_proc_deassigned_low")
      == final_value("procedural_assignments.wide_proc_reactive_low"));
  assert(final_value("procedural_assignments.wide_selected_initial")
      == "0000000100100011010001010110011110001001101010111100110111101111");
  assert(final_value("procedural_assignments.wide_selected_reactive")
      == "1111111011011100101110101001100001110110010101000011001000010000");
  assert(final_value("procedural_assignments.wide_selected_deassigned")
      == final_value("procedural_assignments.wide_selected_reactive"));
  assert(
      capture.debugger.find(
          "procedural_assignments.dynamic_partial = 1001001000110100")
      != std::string::npos);
  assert(
      capture.debugger.find(
          "procedural_assignments.force_masked = 0111")
      != std::string::npos);
  assert(
      capture.debugger.find(
          "procedural_assignments.force_partial = 0011")
      != std::string::npos);
  assert(
      capture.debugger.find(
          "procedural_assignments.proc_active = 1011 (forced)")
      != std::string::npos);
  assert(
      capture.vcd.find("$timescale 1ps $end")
      != std::string::npos);
  assert(capture.vcd.find("#6") != std::string::npos);
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  const auto config = make_config(directory, source, optimization);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);
  verify_reference(reference);
  for (const auto* actual : {&cold, &warm}) {
    assert(reference.result.status == actual->result.status);
    assert(reference.result.time == actual->result.time);
    assert(reference.result.delta == actual->result.delta);
    assert(reference.changes == actual->changes);
    assert(reference.final_values == actual->final_values);
    assert(reference.vcd == actual->vcd);
    assert(reference.debugger == actual->debugger);
  }
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 29);
  assert(cold.native_cache.hits == 0);
  assert(cold.native_cache.misses == 1);
  assert(cold.native_cache.stores == 1);
  assert(warm.compiled_processes == 29);
  assert(warm.native_cache.hits == 1);
  assert(warm.native_cache.misses == 0);
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
      / ("fsim-procedural-assignment-test-"
         + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "procedural_assignments.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module procedural_assignments;
  logic clock;
  logic source;
  logic delayed_nba;
  logic delayed_blocking;
  logic event_blocking;
  logic event_nba;
  logic wildcard_nba;
  logic same_slot;
  logic [3:0] overlap;
  logic [3:0] reverse_overlap;
  logic zero_slot;
  logic equal_deadline;
  logic local_result;
  logic [15:0] dynamic_compound;
  logic [15:0] dynamic_partial;
  logic [15:0] dynamic_unknown;
  logic [15:0] dynamic_oob;
  logic [15:0] dynamic_nba;
  logic signed [31:0] dynamic_base;
  logic signed [31:0] update_calls;
  logic [7:0] expr_value;
  logic [7:0] post_result;
  logic [7:0] pre_result;
  logic selected_post_result;
  logic signed [31:0] expr_calls;
  logic [3:0] forced_selected;
  logic [3:0] force_first;
  logic [3:0] force_masked;
  logic [3:0] force_partial;
  logic [3:0] force_released;
  logic [3:0] forced_whole;
  logic [3:0] whole_masked;
  logic [3:0] whole_released;
  logic [7:0] chained_target;
  logic [3:0] delayed_compound;
  logic [3:0] event_compound;
  logic [3:0] event_vector;
  logic cross_slot;
  logic [2:0] inactive_nba;
  logic [3:0] dynamic_forced;
  logic [3:0] dynamic_force_masked;
  logic [3:0] dynamic_force_released;
  logic signed [31:0] force_index;
  logic edge_left;
  logic edge_right;
  logic edge_positive;
  logic edge_negative;
  logic [1:0] edge_mixed_state;
  logic [3:0] proc_source_a;
  logic [3:0] proc_source_b;
  logic [3:0] proc_target;
  logic [3:0] proc_initial;
  logic [3:0] proc_reactive;
  logic [3:0] proc_replaced;
  logic [3:0] proc_still_replaced;
  logic [3:0] proc_deassigned;
  logic [3:0] proc_active;
  logic [3:0] proc_selected_source;
  logic [7:0] proc_selected;
  logic [7:0] proc_selected_initial;
  logic [7:0] proc_selected_reactive;
  logic [7:0] proc_selected_after;
  logic [3:0] concat_left;
  logic [3:0] concat_right;
  logic [7:0] concat_source;
  logic [7:0] concat_blocking;
  logic [7:0] concat_forced;
  logic [7:0] concat_released;
  logic [7:0] concat_initial;
  logic [7:0] concat_reactive;
  logic [7:0] concat_deassigned;
  logic [1:0] concat_index;
  logic [3:0] concat_indexed;
  logic [2:0] concat_index_capture;
  logic [256:0] wide_target;
  logic [256:0] wide_proc_source;
  logic [128:0] wide_selected_source;
  logic signed [31:0] wide_base;
  logic [63:0] wide_proc_initial_high;
  logic [63:0] wide_proc_initial_low;
  logic [63:0] wide_proc_reactive_high;
  logic [63:0] wide_proc_reactive_low;
  logic [63:0] wide_proc_deassigned_high;
  logic [63:0] wide_proc_deassigned_low;
  logic [63:0] wide_selected_initial;
  logic [63:0] wide_selected_reactive;
  logic [63:0] wide_selected_deassigned;
  logic [3:0] compound_rhs;
  logic signed [31:0] event_index;

  function automatic int next_part_base();
    update_calls = update_calls + 1;
    return 2;
  endfunction

  function automatic int next_expr_index();
    expr_calls = expr_calls + 1;
    return 0;
  endfunction

  initial begin
    clock = 1'b0;
    source = 1'b0;
    compound_rhs = 4'd1;
    event_index = 0;
    delayed_nba <= #5ps source;
    source = 1'b1;
    delayed_blocking = #3ps source;
    clock = 1'b1;
    compound_rhs = 4'd2;
    event_index = 1;
    #1ps source = 1'b0;
    #6ps $finish;
  end

  initial event_blocking = @(posedge clock) source;
  initial event_nba <= @(posedge clock) source;
  initial wildcard_nba <= @* source;
  initial begin
    delayed_compound = 4'd1;
    delayed_compound += #3ps 4'd1;
  end
  initial begin
    event_compound = 4'd1;
    event_compound += @(posedge clock) compound_rhs;
  end
  initial begin
    event_vector = 4'b0001;
    event_vector[event_index] += @(posedge clock) 1'b1;
  end

  initial begin
    same_slot <= 1'b0;
    same_slot <= 1'b1;
    dynamic_compound = 16'h1204;
    update_calls = 0;
    dynamic_compound[next_part_base() +: 4] += 4'h1;
    dynamic_partial = 16'h1234;
    dynamic_base = 14;
    dynamic_partial[dynamic_base +: 4] = 4'ha;
    dynamic_unknown = 16'h5678;
    dynamic_base = 32'bx;
    dynamic_unknown[dynamic_base +: 4] = 4'hf;
    dynamic_oob = 16'h9abc;
    dynamic_base = 40;
    dynamic_oob[dynamic_base +: 4] = 4'h0;
    dynamic_nba = 16'h1234;
    dynamic_base = 14;
    dynamic_nba[dynamic_base +: 4] <= 4'ha;
    expr_value = 8'd5;
    post_result = expr_value++;
    pre_result = ++expr_value;
    expr_calls = 0;
    selected_post_result = expr_value[next_expr_index()]++;
    forced_selected = 4'b1010;
    force forced_selected[2:1] = 2'b11;
    force_first = forced_selected;
    forced_selected = 4'b0001;
    force_masked = forced_selected;
    release forced_selected[2];
    force_partial = forced_selected;
    release forced_selected[1];
    force_released = forced_selected;
    forced_whole = 4'b1111;
    force forced_whole = 4'b1010;
    forced_whole = 4'b0101;
    whole_masked = forced_whole;
    release forced_whole;
    whole_released = forced_whole;
    dynamic_forced = 4'b1010;
    force_index = 2;
    force dynamic_forced[force_index] = 1'b1;
    dynamic_forced = 4'b0001;
    dynamic_force_masked = dynamic_forced;
    release dynamic_forced[force_index];
    dynamic_force_released = dynamic_forced;
    chained_target = 8'h00;
    chained_target[7:2][3:1] = 3'b101;
    chained_target[7:2][3:1] += 3'b001;
  end

  initial begin
    proc_source_a = 4'h3;
    proc_source_b = 4'h5;
    proc_target = 4'h0;
    assign proc_target = proc_source_a;
    #1ps proc_initial = proc_target;
    proc_source_a = 4'ha;
    #1ps proc_reactive = proc_target;
    proc_target = 4'h6;
    assign proc_target = proc_source_b;
    #1ps proc_replaced = proc_target;
    proc_source_a = 4'hf;
    #1ps proc_still_replaced = proc_target;
    deassign proc_target;
    proc_deassigned = proc_target;
    proc_target = 4'h9;

    proc_active = 4'h0;
    assign proc_active = proc_source_a;
    proc_active = 4'h6;
    proc_source_a = 4'hb;
  end

  initial begin
    proc_selected = 8'hc3;
    proc_selected_source = 4'h5;
    assign proc_selected[5:2] = proc_selected_source;
    #1ps proc_selected_initial = proc_selected;
    proc_selected_source = 4'ha;
    #1ps proc_selected_reactive = proc_selected;
    proc_selected = 8'h00;
    deassign proc_selected[5:2];
    proc_selected_after = proc_selected;

    {concat_left, concat_right} = 8'hab;
    concat_blocking = {concat_left, concat_right};
    force {concat_left, concat_right} = 8'h3c;
    {concat_left, concat_right} = 8'h55;
    concat_forced = {concat_left, concat_right};
    release {concat_left, concat_right};
    concat_released = {concat_left, concat_right};
    concat_source = 8'h96;
    assign {concat_left, concat_right} = concat_source;
    #1ps concat_initial = {concat_left, concat_right};
    concat_source = 8'h69;
    #1ps concat_reactive = {concat_left, concat_right};
    {concat_left, concat_right} = 8'h12;
    deassign {concat_left, concat_right};
    concat_deassigned = {concat_left, concat_right};

    concat_index = 2'b00;
    concat_indexed = 4'b0000;
    {concat_index, concat_indexed[concat_index]} = 3'b101;
    concat_index_capture = {concat_index, concat_indexed[0]};
  end

  initial begin
    edge_left = 1'b0;
    edge_right = 1'b0;
    #1ps edge_right = 1'b1;
    #1ps edge_left = 1'b1;
    #1ps edge_right = 1'b0;
    #1ps edge_left = 1'b0;
  end
  initial begin
    wide_target = '0;
    wide_base = 64;
    wide_selected_source = 129'h1_0123456789abcdef_0123456789abcdef;
    wide_target[200:72] = wide_selected_source;
    wide_target[wide_base] = 1'b1;
    wide_target[wide_base +: 129] = wide_selected_source;
    wide_target[wide_base] <= 1'b0;
    wide_target <= #1ps wide_proc_source;
    wide_target[200:72] <= #1ps wide_selected_source;
    wide_target[wide_base] <= #1ps 1'b1;
    wide_target[wide_base +: 129] <= #1ps wide_selected_source;
    #2ps;

    wide_proc_source =
      257'h1_0123456789abcdef_0123456789abcdef_fedcba9876543210_fedcba9876543210;
    assign wide_target = wide_proc_source;
    #1ps;
    wide_proc_initial_high = wide_target[255:192];
    wide_proc_initial_low = wide_target[63:0];
    wide_proc_source =
      257'h0_89abcdef01234567_89abcdef01234567_76543210fedcba98_76543210fedcba98;
    #1ps;
    wide_proc_reactive_high = wide_target[255:192];
    wide_proc_reactive_low = wide_target[63:0];
    wide_target = '0;
    deassign wide_target;
    wide_proc_deassigned_high = wide_target[255:192];
    wide_proc_deassigned_low = wide_target[63:0];

    wide_target = '0;
    wide_selected_source = 129'h1_0123456789abcdef_0123456789abcdef;
    assign wide_target[200:72] = wide_selected_source;
    #1ps wide_selected_initial = wide_target[135:72];
    wide_selected_source = 129'h0_fedcba9876543210_fedcba9876543210;
    #1ps wide_selected_reactive = wide_target[135:72];
    wide_target = '0;
    deassign wide_target[200:72];
    wide_selected_deassigned = wide_target[135:72];
  end
  initial begin
    edge_positive = 1'b0;
    @(posedge (edge_left | edge_right));
    edge_positive = 1'b1;
  end
  initial begin
    edge_negative = 1'b0;
    @(negedge (edge_left | edge_right));
    edge_negative = 1'b1;
  end
  initial begin
    edge_mixed_state = 2'b00;
    @(negedge (edge_left | edge_right) or edge_left);
    edge_mixed_state = {edge_left, edge_right};
  end

  initial fork
    cross_slot <= 1'b0;
    cross_slot <= 1'b1;
  join
  initial begin
    inactive_nba = 3'd0;
    inactive_nba <= 3'd1;
    #0 inactive_nba = 3'd2;
  end
  initial begin
    overlap <= 4'b1010;
    overlap[2:1] <= 2'b11;
  end
  initial begin
    reverse_overlap[2:1] <= 2'b11;
    reverse_overlap <= 4'b1010;
  end
  initial begin
    zero_slot <= 1'b0;
    zero_slot <= #0 1'b1;
  end
  initial begin
    equal_deadline <= #6ps 1'b0;
    equal_deadline <= #6ps 1'b1;
  end
  initial begin
    logic local_value;
    local_value = 1'b0;
    local_value = #2ps source;
    local_result = local_value;
  end
endmodule
)";
    assert(output.good());
  }

  verify_mode(
      directory.path,
      source,
      fsim::project::Optimization::o0);
  verify_mode(
      directory.path,
      source,
      fsim::project::Optimization::o2);

  std::cout << "procedural assignment application tests passed\n";
  return 0;
}
