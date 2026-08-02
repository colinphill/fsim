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
  std::array<std::string, 30> values;
  std::array<std::string, 19> composite_values;
  std::string top_local;
  std::string dynamic_local;
  std::string dynamic_slice_local;
  std::string top_aggregate;
  std::string child_local;
  std::string generic_aggregate;
  std::string null_signal;
  std::string null_copy;
  std::string null_local;
  std::string null_local_copy;
  std::string null_equal;
  std::string null_not_equal;
  std::string null_slice_equal;
  std::string null_iterations;
  std::array<std::string, 2> boundary_matrix_inputs;
  std::array<std::string, 2> boundary_matrix_outputs;
  std::array<std::string, 2> boundary_cell_inputs;
  std::array<std::string, 2> boundary_cell_outputs;
  std::array<std::string, 2> boundary_selected;
  std::array<std::string, 2> boundary_identities;
  std::string generic_boundary_matrix_input;
  std::string generic_boundary_matrix_output;
  std::string generic_boundary_cells_input;
  std::string generic_boundary_cells_output;
  std::string callable_matrix_input;
  std::string callable_package_result;
  std::string callable_generated_result;
  std::string callable_cells_input;
  std::string callable_local_result;
  std::string callable_procedure_result;
  std::string disjoint_driver_equal;
  std::string resolved_driver_equal;
  std::string composite_driver_equal;
  std::string partial_sensitivity_result;
  std::string debugger_output;
  std::string vcd;
  std::string application_vcd;
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
  config.project.name = "vhdl-array";
  config.project.top = "vhdl:work.array_top(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.trace_file = config.build.cache_path / "arrays.vcd";
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
  assert(project->design.specializations().size() == 5);
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
      std::size_t>> top_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> child_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> top_aggregate;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> dynamic_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> dynamic_slice_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> generic_aggregate;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> null_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> null_local_copy;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      const auto& local = process.debug_locals[index];
      if (local.name == "top_local") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        top_local = std::pair{process.id, index};
      } else if (local.name == "dynamic_local") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        dynamic_local = std::pair{process.id, index};
      } else if (local.name == "dynamic_slice_local") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        dynamic_slice_local = std::pair{process.id, index};
      } else if (local.name == "top_aggregate") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        top_aggregate = std::pair{process.id, index};
      } else if (local.name == "child_local") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        child_local = std::pair{process.id, index};
      } else if (local.name == "generic_aggregate") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        generic_aggregate = std::pair{process.id, index};
      } else if (local.name == "null_local") {
        assert(local.width == 0);
        null_local = std::pair{process.id, index};
      } else if (local.name == "null_local_copy") {
        assert(local.width == 0);
        null_local_copy = std::pair{process.id, index};
      }
    }
  }
  assert(
      top_local && dynamic_local && dynamic_slice_local && top_aggregate
      && child_local && generic_aggregate && null_local
      && null_local_copy);

  Capture capture;
  capture.specialization_keys =
      project->specialization_cache_keys;
  bool nested_shape_identity = false;
  for (const auto& specialization :
       project->design.specializations()) {
    if (specialization.instance == "array_top.boundary_a") {
      capture.boundary_identities[0] =
          project->specialization_cache_keys.at(specialization.id);
    } else if (
        specialization.instance == "array_top.boundary_b") {
      capture.boundary_identities[1] =
          project->specialization_cache_keys.at(specialization.id);
    }
    for (const auto& [name, identity] :
         specialization.parameter_identity_values) {
      if (name.starts_with("__vhdl_port_shape.cells_")
          && identity.find("vhdl-array-shape-v2")
              != std::string::npos
          && identity.find("boundary_detail_t")
              != std::string::npos
          && identity.find("literal=run") != std::string::npos) {
        nested_shape_identity = true;
      }
    }
  }
  assert(nested_shape_identity);
  assert(
      !capture.boundary_identities[0].empty()
      && !capture.boundary_identities[1].empty()
      && capture.boundary_identities[0]
          != capture.boundary_identities[1]);
  assert(
      project->design.find_signal(
          "array_top.boundary_matrix_a_input")
      == project->design.find_signal(
          "array_top.boundary_a.matrix_input"));
  assert(
      project->design.find_signal(
          "array_top.boundary_matrix_a_output")
      == project->design.find_signal(
          "array_top.boundary_a.matrix_output"));
  assert(
      project->design.find_signal(
          "array_top.boundary_cells_b_input")
      == project->design.find_signal(
          "array_top.boundary_b.cells_input"));
  assert(
      project->design.find_signal(
          "array_top.boundary_cells_b_output")
      == project->design.find_signal(
          "array_top.boundary_b.cells_output"));
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 30> paths{
      "array_top.source",
      "array_top.result",
      "array_top.conditional_result",
      "array_top.slice_result",
      "array_top.indexed_result",
      "array_top.equal_result",
      "array_top.ascending_result",
      "array_top.boolean_result",
      "array_top.aggregate_positional",
      "array_top.aggregate_named",
      "array_top.aggregate_equal",
      "array_top.attribute_left",
      "array_top.attribute_right",
      "array_top.attribute_length",
      "array_top.attribute_ascending",
      "array_top.range_order",
      "array_top.reverse_order",
      "array_top.attribute_slice",
      "array_top.dynamic_read",
      "array_top.dynamic_result",
      "array_top.dynamic_slice_read",
      "array_top.dynamic_slice_local_result",
      "array_top.dynamic_slice_signal",
      "array_top.dynamic_slice_waveform",
      "array_top.boundary_matrix_a_output",
      "array_top.callable_package_result",
      "array_top.disjoint_driver_matrix",
      "array_top.resolved_driver_matrix",
      "array_top.composite_driver_cells",
      "array_top.partial_sensitivity_result"};
  constexpr std::array<std::size_t, 30> widths{
      8, 8, 8, 4, 1, 1, 8, 4, 8, 8, 1,
      32, 32, 32, 1, 32, 32, 4, 1, 8, 4, 8, 8, 8,
      6, 6, 6, 6, 8, 1};
  std::array<fsim::runtime::simir::SignalId, 30> signals{};
  std::array<fsim::runtime::VcdSignal, 30> traces{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{paths[index]}, widths[index]);
  }
  constexpr std::array<std::string_view, 19> composite_paths{
      "array_top.matrix_positional",
      "array_top.matrix_named",
      "array_top.matrix_ranged",
      "array_top.matrix_conditional",
      "array_top.matrix_local_result",
      "array_top.nested",
      "array_top.cells",
      "array_top.matrix_cell_read",
      "array_top.matrix_slice_read",
      "array_top.matrix_dynamic_read",
      "array_top.nested_read",
      "array_top.vector_rows",
      "array_top.vector_read",
      "array_top.vector_slice_read",
      "array_top.cell_selected",
      "array_top.matrix_signal_target",
      "array_top.matrix_dynamic_signal_target",
      "array_top.vector_target",
      "array_top.cell_targets"};
  std::array<fsim::runtime::simir::SignalId, 19>
      composite_signals{};
  for (std::size_t index = 0;
       index < composite_paths.size(); ++index) {
    const auto signal =
        simulation.find_signal(composite_paths[index]);
    assert(signal);
    composite_signals[index] = *signal;
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
  for (std::size_t index = 0;
       index < composite_signals.size(); ++index) {
    capture.composite_values[index] =
        simulation.read_signal(composite_signals[index])
            .to_msb_string();
  }
  capture.top_local = simulation.read_process_local(
      top_local->first, top_local->second).to_msb_string();
  capture.dynamic_local = simulation.read_process_local(
      dynamic_local->first,
      dynamic_local->second).to_msb_string();
  capture.dynamic_slice_local = simulation.read_process_local(
      dynamic_slice_local->first,
      dynamic_slice_local->second).to_msb_string();
  capture.top_aggregate = simulation.read_process_local(
      top_aggregate->first,
      top_aggregate->second).to_msb_string();
  capture.child_local = simulation.read_process_local(
      child_local->first, child_local->second).to_msb_string();
  capture.generic_aggregate = simulation.read_process_local(
      generic_aggregate->first,
      generic_aggregate->second).to_msb_string();
  const auto read = [&](const std::string_view path) {
    const auto signal = simulation.find_signal(path);
    assert(signal);
    return simulation.read_signal(*signal).to_msb_string();
  };
  capture.null_signal = read("array_top.null_signal");
  capture.null_copy = read("array_top.null_copy");
  capture.null_equal = read("array_top.null_equal");
  capture.null_not_equal = read("array_top.null_not_equal");
  capture.null_slice_equal = read("array_top.null_slice_equal");
  capture.null_iterations = read("array_top.null_iterations");
  capture.null_local = simulation.read_process_local(
      null_local->first, null_local->second).to_msb_string();
  capture.null_local_copy = simulation.read_process_local(
      null_local_copy->first,
      null_local_copy->second).to_msb_string();
  for (std::size_t index = 0; index < 2; ++index) {
    const auto suffix = index == 0 ? "a" : "b";
    capture.boundary_matrix_inputs[index] = read(
        "array_top.boundary_matrix_" + std::string{suffix} + "_input");
    capture.boundary_matrix_outputs[index] = read(
        "array_top.boundary_matrix_" + std::string{suffix} + "_output");
    capture.boundary_cell_inputs[index] = read(
        "array_top.boundary_cells_" + std::string{suffix} + "_input");
    capture.boundary_cell_outputs[index] = read(
        "array_top.boundary_cells_" + std::string{suffix} + "_output");
    capture.boundary_selected[index] = read(
        "array_top.boundary_selected_" + std::string{suffix});
  }
  capture.generic_boundary_matrix_input =
      read("array_top.generic_boundary_matrix_input");
  capture.generic_boundary_matrix_output =
      read("array_top.generic_boundary_matrix_output");
  capture.generic_boundary_cells_input =
      read("array_top.generic_boundary_cells_input");
  capture.generic_boundary_cells_output =
      read("array_top.generic_boundary_cells_output");
  capture.callable_matrix_input =
      read("array_top.callable_matrix_input");
  capture.callable_package_result =
      read("array_top.callable_package_result");
  capture.callable_generated_result =
      read("array_top.callable_generated_result");
  capture.callable_cells_input =
      read("array_top.callable_cells_input");
  capture.callable_local_result =
      read("array_top.callable_local_result");
  capture.callable_procedure_result =
      read("array_top.callable_procedure_result");
  capture.disjoint_driver_equal =
      read("array_top.disjoint_driver_equal");
  capture.resolved_driver_equal =
      read("array_top.resolved_driver_equal");
  capture.composite_driver_equal =
      read("array_top.composite_driver_equal");
  capture.partial_sensitivity_result =
      read("array_top.partial_sensitivity_result");
  {
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    fsim::diagnostic::Engine trace_diagnostics;
    fsim::app::DebuggerControl debugger{
        simulation, debugger_output, debugger_error,
        config, trace_diagnostics};
    debugger.execute({"show", "source"});
    debugger.execute({"show", "result"});
    debugger.execute({"show", "aggregate_named"});
    debugger.execute({"show", "attribute_slice"});
    debugger.execute({"show", "slice_result"});
    debugger.execute({"show", "dynamic_slice_read"});
    debugger.execute({"show", "dynamic_slice_local_result"});
    debugger.execute({"show", "dynamic_slice_signal"});
    debugger.execute({"show", "dynamic_slice_waveform"});
    debugger.execute({"show", "boundary_matrix_a_output"});
    debugger.execute({"show", "callable_package_result"});
    debugger.execute({"show", "resolved_driver_matrix"});
    debugger.execute({"show", "composite_driver_cells"});
    debugger.execute({"show", "null_signal"});
    assert(debugger_error.str().empty());
    assert(!trace_diagnostics.has_error());
    capture.debugger_output = debugger_output.str();
  }
  {
    std::ifstream input{*config.run.trace_file};
    capture.application_vcd = {
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
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
      == std::array<std::string, 30>{
          "01LH10Z-",
          "11LH10Z-",
          "1111Z0ZH",
          "10Z-",
          "1",
          "0",
          "1111Z0Z0",
          "1010",
          "01LH10Z-",
          "1111Z0ZH",
          "1",
          "00000000000000000000000000000111",
          "00000000000000000000000000000000",
          "00000000000000000000000000001000",
          "0",
          "00000000011101001010000001001010",
          "00000000000000000011000000111001",
          "1111",
          "L",
          "00H00000",
          "1HH1",
          "0Z10X000",
          "010XZ000",
          "0Z01X000",
          "111111",
          "111111",
          "101010",
          "10Z0H1",
          "11111111",
          "1"}));
  assert((
      capture.composite_values
      == std::array<std::string, 19>{
          "101010",
          "101000",
          "111001",
          "101010",
          "101100",
          "10000101",
          "110001",
          "1",
          "10",
          "1",
          "1",
          "10100101",
          "0101",
          "01",
          "110",
          "000010",
          "000010",
          "10101111",
          "000111"}));
  assert(capture.top_local == "00LH10Z-");
  assert(capture.dynamic_local == "01HH10Z-");
  assert(capture.dynamic_slice_local == "0Z10X000");
  assert(capture.top_aggregate == "1111Z0ZH");
  assert(capture.child_local == "11LH10Z-");
  assert(capture.generic_aggregate == "10000000");
  assert(capture.null_signal.empty());
  assert(capture.null_copy.empty());
  assert(capture.null_local.empty());
  assert(capture.null_local_copy.empty());
  assert(capture.null_equal == "1");
  assert(capture.null_not_equal == "0");
  assert(capture.null_slice_equal == "1");
  assert(capture.null_iterations
         == "00000000000000000000000000000000");
  assert((capture.boundary_matrix_inputs
          == std::array<std::string, 2>{"111111", "111111"}));
  assert(capture.boundary_matrix_outputs
         == capture.boundary_matrix_inputs);
  assert((capture.boundary_cell_inputs
          == std::array<std::string, 2>{
              "11111111", "11111111"}));
  assert(capture.boundary_cell_outputs
         == capture.boundary_cell_inputs);
  assert((capture.boundary_selected
          == std::array<std::string, 2>{"1", "1"}));
  assert(capture.generic_boundary_matrix_input == "111111");
  assert(
      capture.generic_boundary_matrix_output
      == capture.generic_boundary_matrix_input);
  assert(capture.generic_boundary_cells_input == "11111111");
  assert(
      capture.generic_boundary_cells_output
      == capture.generic_boundary_cells_input);
  assert(capture.callable_matrix_input == "111111");
  assert(
      capture.callable_package_result
      == capture.callable_matrix_input);
  assert(
      capture.callable_generated_result
      == capture.callable_matrix_input);
  assert(capture.callable_cells_input == "11111111");
  assert(
      capture.callable_local_result
      == capture.callable_cells_input);
  assert(
      capture.callable_procedure_result
      == capture.callable_cells_input);
  assert(capture.disjoint_driver_equal == "1");
  assert(capture.resolved_driver_equal == "1");
  assert(capture.composite_driver_equal == "1");
  assert(capture.partial_sensitivity_result == "1");
  assert(
      !capture.boundary_identities[0].empty()
      && capture.boundary_identities[0]
          != capture.boundary_identities[1]);
  assert(
      capture.debugger_output.find(
          "source = 01LH10Z-")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "result = 11LH10Z-")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "aggregate_named = 1111Z0ZH")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "attribute_slice = 1111")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "slice_result = 10Z-")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "dynamic_slice_read = 1HH1")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "dynamic_slice_local_result = 0Z10X000")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "dynamic_slice_signal = 010XZ000")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "dynamic_slice_waveform = 0Z01X000")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "boundary_matrix_a_output = 111111")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "callable_package_result = 111111")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "resolved_driver_matrix = 10Z0H1")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "composite_driver_cells = 11111111")
      != std::string::npos);
  assert(
      capture.debugger_output.find("null_signal = <null>")
      != std::string::npos);
  assert(!capture.application_vcd.empty());
  assert(capture.application_vcd.find("null_signal")
         == std::string::npos);
  assert(
      capture.application_vcd.find("boundary_matrix_a_output")
      != std::string::npos);
  assert(
      capture.application_vcd.find("callable_package_result")
      != std::string::npos);
  assert(
      capture.application_vcd.find("resolved_driver_matrix")
      != std::string::npos);
  assert(
      capture.application_vcd.find("composite_driver_cells")
      != std::string::npos);
  assert(capture.vcd.find("b010110zx") != std::string::npos);
  assert(capture.vcd.find("b110110zx") != std::string::npos);
  assert(capture.vcd.find("b010xz000") != std::string::npos);
  assert(capture.vcd.find("b0z01x000") != std::string::npos);
  assert(
      capture.vcd.find("boundary_matrix_a_output")
      != std::string::npos);
  assert(
      capture.vcd.find("callable_package_result")
      != std::string::npos);
  assert(
      capture.vcd.find("resolved_driver_matrix")
      != std::string::npos);
  assert(
      capture.vcd.find("composite_driver_cells")
      != std::string::npos);
}

std::string run_expected_dynamic_failure(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::string_view expected_message) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  try {
    (void)simulation.run();
    assert(false && "dynamic packed-index failure was not reported");
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    const auto message = std::string{error.what()};
    assert(message.find(expected_message) != std::string::npos);
    return message;
  }
  return {};
}

void expect_build_failure(
    const fsim::project::Config& config,
    const std::string_view expected_code) {
  fsim::diagnostic::Engine diagnostics;
  const auto project = fsim::app::build_project(config, diagnostics);
  assert(!project);
  assert(std::ranges::any_of(
      diagnostics.diagnostics(),
      [&](const auto& diagnostic) {
        return diagnostic.code == expected_code;
      }));
}

fsim::project::Config make_failure_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const std::string& name,
    const std::string& top) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = name;
  config.project.top = top;
  config.project.time_resolution = "1ns";
  config.build.optimization = fsim::project::Optimization::o0;
  config.build.cache_path = directory / (name + "-cache");
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-array-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto package_source =
      directory.path / "array_types.vhd";
  const auto child_source =
      directory.path / "array_child.vhd";
  const auto top_source =
      directory.path / "array_top.vhd";
  const auto failure_source =
      directory.path / "dynamic_failure.vhd";
  const auto slice_failure_source =
      directory.path / "dynamic_slice_failure.vhd";
  const auto multidimensional_failure_source =
      directory.path / "multidimensional_failure.vhd";
  const auto nested_composite_failure_source =
      directory.path / "nested_composite_failure.vhd";
  const auto boundary_shape_failure_source =
      directory.path / "boundary_shape_failure.vhd";
  const auto component_shape_failure_source =
      directory.path / "component_shape_failure.vhd";
  const auto callable_shape_failure_source =
      directory.path / "callable_shape_failure.vhd";
  const auto overlapping_driver_failure_source =
      directory.path / "overlapping_driver_failure.vhd";

  const auto write_package =
      [&](const std::string_view revision) {
        std::ofstream output{package_source};
        output << "-- " << revision << R"(
package Array_Types is
  constant Byte_Width : positive := 8;
  constant First_Index : natural := 0;
  type Logic_Array_T is array (natural range <>) of std_logic;
  subtype Byte_T is Logic_Array_T(Byte_Width - 1 downto 0);
  subtype Nibble_T is Logic_Array_T(3 downto 0);
  subtype Ascending_Byte_T is Logic_Array_T(0 to 7);
  subtype Null_T is Logic_Array_T(3 to 0);
  type Boolean_Array_T is array (natural range <>) of boolean;
  subtype Boolean_Nibble_T is Boolean_Array_T(0 to 3);
  type Boundary_Matrix_T is array
    (natural range <>, positive range <>) of bit;
  type Resolved_Matrix_T is array
    (natural range <>, positive range <>) of std_logic;
  type Boundary_Mode_T is (Idle, Run);
  type Boundary_Detail_T is record
    Mode : Boundary_Mode_T;
    Data : bit_vector(1 downto 0);
  end record;
  type Boundary_Cell_T is record
    Flag : boolean;
    Detail : Boundary_Detail_T;
  end record;
  type Boundary_Cells_T is array
    (natural range <>) of Boundary_Cell_T;
  subtype Callable_Matrix_T is
    Boundary_Matrix_T(0 to 1, 3 downto 1);
  subtype Callable_Cells_T is Boundary_Cells_T(0 to 1);
  function Package_Matrix_Copy(
    Value : Callable_Matrix_T) return Callable_Matrix_T;
  procedure Package_Cells_Copy(
    constant Source : in Callable_Cells_T;
    variable Target : inout Callable_Cells_T);
end package;

package body Array_Types is
  function Package_Matrix_Copy(
    Value : Callable_Matrix_T) return Callable_Matrix_T is
    variable Result : Callable_Matrix_T :=
      (others => (others => '0'));
  begin
    Result := Value;
    return Result;
  end function;

  procedure Package_Cells_Copy(
    constant Source : in Callable_Cells_T;
    variable Target : inout Callable_Cells_T) is
    variable Snapshot : Callable_Cells_T := Source;
  begin
    Target := Snapshot;
  end procedure;
end package body;
)";
        assert(output.good());
      };
  write_package("revision one");
  {
    std::ofstream output{child_source};
    output << R"(
use work.array_types.all;
entity Array_Child is
  generic (Width : positive := Byte_Width);
  port (
    Source : in Logic_Array_T(Width - 1 downto 0);
    Result : out Logic_Array_T(Width - 1 downto 0)
  );
end entity;

use work.array_types.all;
architecture rtl of Array_Child is
begin
  transform : process(Source)
    variable Child_Local : Byte_T;
    variable Generic_Aggregate :
      Logic_Array_T(Width - 1 downto 0) :=
        (Width - 1 => '1', others => '0');
  begin
    Child_Local := Source;
    Child_Local(7) := '1';
    Child_Local(3 downto 0) := "10Z-";
    Result <= Child_Local;
  end process;
end architecture;

use work.array_types.all;
entity Array_Boundary_Child is
  port (
    Matrix_Input : in Boundary_Matrix_T;
    Matrix_Output : out Boundary_Matrix_T;
    Cells_Input : in Boundary_Cells_T;
    Cells_Output : out Boundary_Cells_T;
    Selected : out bit
  );
end entity;

use work.array_types.all;
architecture rtl of Array_Boundary_Child is
begin
  Matrix_Output <= Matrix_Input;
  Cells_Output <= Cells_Input;
  Selected <= Matrix_Input(0, 1);
end architecture;

use work.array_types.all;
entity Array_Generic_Boundary_Child is
  generic (
    Rows : positive := 2;
    Columns : positive := 3
  );
  port (
    Matrix_Input : in Boundary_Matrix_T(
      0 to Rows - 1, Columns downto 1);
    Matrix_Output : out Boundary_Matrix_T(
      0 to Rows - 1, Columns downto 1);
    Cells_Input : in Boundary_Cells_T(0 to Rows - 1);
    Cells_Output : out Boundary_Cells_T(0 to Rows - 1)
  );
end entity;

use work.array_types.all;
architecture rtl of Array_Generic_Boundary_Child is
begin
  Matrix_Output <= Matrix_Input;
  Cells_Output <= Cells_Input;
end architecture;
)";
    assert(output.good());
  }
  {
    std::ofstream output{top_source};
    output << R"(
use work.array_types.all;
entity Array_Top is
end entity;

use work.array_types.all;
architecture rtl of Array_Top is
  component Array_Boundary_Child is
    port (
      Matrix_Input : in Boundary_Matrix_T;
      Matrix_Output : out Boundary_Matrix_T;
      Cells_Input : in Boundary_Cells_T;
      Cells_Output : out Boundary_Cells_T;
      Selected : out bit
    );
  end component;
  component Array_Generic_Boundary_Child is
    generic (
      Component_Rows : positive := 2;
      Component_Columns : positive := 3
    );
    port (
      Matrix_Input : in Boundary_Matrix_T(
        0 to Component_Rows - 1,
        Component_Columns downto 1);
      Matrix_Output : out Boundary_Matrix_T(
        0 to Component_Rows - 1,
        Component_Columns downto 1);
      Cells_Input : in Boundary_Cells_T(
        0 to Component_Rows - 1);
      Cells_Output : out Boundary_Cells_T(
        0 to Component_Rows - 1)
    );
  end component;
  function Local_Cells_Copy(
    Value : Callable_Cells_T) return Callable_Cells_T is
    variable First : Callable_Cells_T := Value;
    variable Second : Callable_Cells_T :=
      (others =>
         (Flag => false,
          Detail => (Mode => Idle, Data => "00")));
  begin
    Second := First;
    return Second;
  end function;
  type Matrix_T is array (0 to 1, 3 downto 1) of bit;
  type Nibble_Array_T is array (3 downto 0) of bit;
  type Nibble_Memory_T is array (0 to 1) of Nibble_Array_T;
  type Vector_Rows_T is array (0 to 1) of bit_vector(3 downto 0);
  type Cell_T is record
    Flag : boolean;
    Data : bit_vector(1 downto 0);
  end record;
  type Cells_T is array (2 to 3) of Cell_T;
  signal Source : Byte_T;
  signal Result : Byte_T;
  signal Conditional_Result : Byte_T;
  signal Slice_Result : Nibble_T;
  signal Indexed_Result : std_logic;
  signal Equal_Result : boolean;
  signal Ascending_Result : Ascending_Byte_T;
  signal Boolean_Result : Boolean_Nibble_T;
  signal Aggregate_Positional : Byte_T;
  signal Aggregate_Named : Byte_T;
  signal Aggregate_Equal : boolean;
  signal Attribute_Left : integer;
  signal Attribute_Right : integer;
  signal Attribute_Length : integer;
  signal Attribute_Ascending : boolean;
  signal Range_Order : integer;
  signal Reverse_Order : integer;
  signal Attribute_Slice : Nibble_T;
  signal Dynamic_Read : std_logic;
  signal Dynamic_Result : Byte_T;
  signal Dynamic_Slice_Read : Nibble_T;
  signal Dynamic_Slice_Local_Result : Byte_T;
  signal Dynamic_Slice_Signal : Byte_T;
  signal Dynamic_Slice_Waveform : Byte_T;
  signal Matrix_Positional : Matrix_T;
  signal Matrix_Named : Matrix_T;
  signal Matrix_Ranged : Matrix_T;
  signal Matrix_Conditional : Matrix_T;
  signal Matrix_Local_Result : Matrix_T;
  signal Nested : Nibble_Memory_T;
  signal Cells : Cells_T;
  signal Matrix_Cell_Read : bit;
  signal Matrix_Slice_Read : bit_vector(1 downto 0);
  signal Matrix_Dynamic_Read : bit;
  signal Nested_Read : bit;
  signal Vector_Rows : Vector_Rows_T;
  signal Vector_Read : bit_vector(3 downto 0);
  signal Vector_Slice_Read : bit_vector(1 downto 0);
  signal Cell_Selected : Cell_T;
  signal Matrix_Signal_Target : Matrix_T;
  signal Matrix_Dynamic_Signal_Target : Matrix_T;
  signal Vector_Target : Vector_Rows_T;
  signal Cell_Targets : Cells_T;
  signal Null_Signal : Null_T;
  signal Null_Copy : Null_T;
  signal Null_Equal : boolean;
  signal Null_Not_Equal : boolean;
  signal Null_Slice_Equal : boolean;
  signal Null_Iterations : integer;
  signal Boundary_Matrix_A_Input :
    Boundary_Matrix_T(0 to 1, 3 downto 1);
  signal Boundary_Matrix_A_Output :
    Boundary_Matrix_T(0 to 1, 3 downto 1);
  signal Boundary_Matrix_B_Input :
    Boundary_Matrix_T(2 downto 0, 1 to 2);
  signal Boundary_Matrix_B_Output :
    Boundary_Matrix_T(2 downto 0, 1 to 2);
  signal Boundary_Cells_A_Input : Boundary_Cells_T(2 to 3);
  signal Boundary_Cells_A_Output : Boundary_Cells_T(2 to 3);
  signal Boundary_Cells_B_Input : Boundary_Cells_T(4 downto 3);
  signal Boundary_Cells_B_Output : Boundary_Cells_T(4 downto 3);
  signal Boundary_Selected_A : bit;
  signal Boundary_Selected_B : bit;
  signal Generic_Boundary_Matrix_Input :
    Boundary_Matrix_T(0 to 1, 3 downto 1);
  signal Generic_Boundary_Matrix_Output :
    Boundary_Matrix_T(0 to 1, 3 downto 1);
  signal Generic_Boundary_Cells_Input :
    Boundary_Cells_T(0 to 1);
  signal Generic_Boundary_Cells_Output :
    Boundary_Cells_T(0 to 1);
  signal Callable_Matrix_Input : Callable_Matrix_T;
  signal Callable_Package_Result : Callable_Matrix_T;
  signal Callable_Generated_Result : Callable_Matrix_T;
  signal Callable_Cells_Input : Callable_Cells_T;
  signal Callable_Local_Result : Callable_Cells_T;
  signal Callable_Procedure_Result : Callable_Cells_T;
  signal Disjoint_Driver_Matrix : Callable_Matrix_T;
  signal Disjoint_Driver_Expected : Callable_Matrix_T;
  signal Disjoint_Driver_Equal : boolean;
  signal Resolved_Driver_Matrix :
    Resolved_Matrix_T(0 to 1, 3 downto 1);
  signal Resolved_Driver_Expected :
    Resolved_Matrix_T(0 to 1, 3 downto 1);
  signal Resolved_Driver_Equal : boolean;
  signal Composite_Driver_Cells : Callable_Cells_T;
  signal Composite_Driver_Expected : Callable_Cells_T;
  signal Composite_Driver_Equal : boolean;
  signal Partial_Sensitivity_Result : bit;
begin
  drive : process
    variable Top_Local : Byte_T := "01LH10Z-";
    variable Dynamic_Local : Byte_T := "01LH10Z-";
    variable Dynamic_Slice_Local : Byte_T := "00000000";
    variable Dynamic_Index : integer := 5;
    variable Dynamic_Left : integer := 6;
    variable Dynamic_Right : integer := 3;
    variable Top_Aggregate : Byte_T :=
      (Byte_T'left downto Byte_T'high - 3 => '1',
       3 | 1 => 'Z',
       First_Index => 'H', others => '0');
    variable Forward_Order : integer := 0;
    variable Backward_Order : integer := 0;
    variable Matrix_Local : Matrix_T :=
      ((3 => '0', others => '1'),
       (3 downto 2 => '1', others => '0'));
    variable Matrix_Row : integer := 1;
    variable Matrix_Column : integer := 2;
    variable Vector_Local : Vector_Rows_T := ("1010", "0101");
    variable Cell_Local : Cells_T :=
      (others => (Flag => false, Data => "00"));
    variable Null_Local : Null_T := (others => '0');
    variable Null_Local_Copy : Null_T;
    variable Null_Count : integer := 0;
  begin
    Source <= Top_Local;
    Aggregate_Named <= Top_Aggregate;
    Top_Local(6) := '0';
    Dynamic_Read <= Dynamic_Local(Dynamic_Index);
    Dynamic_Local(Dynamic_Index) := 'H';
    Dynamic_Result <= (others => '0');
    Dynamic_Result(Dynamic_Index) <= 'H';
    Dynamic_Slice_Read <=
      Dynamic_Local(Dynamic_Left downto Dynamic_Right);
    Dynamic_Slice_Local(Dynamic_Left downto Dynamic_Right) := "Z10X";
    Dynamic_Slice_Local_Result <= Dynamic_Slice_Local;
    Dynamic_Slice_Signal <= (others => '0');
    Dynamic_Slice_Signal(Dynamic_Left downto Dynamic_Right) <=
      transport "10XZ" after 2 ns;
    Dynamic_Slice_Waveform <= (others => '0');
    Dynamic_Slice_Waveform(Dynamic_Left downto Dynamic_Right) <=
      transport "1010" after 1 ns, "Z01X" after 3 ns;
    Top_Aggregate :=
      (Byte_T'left downto Byte_T'high - 3 => '1',
       3 | 1 => 'Z',
       work.array_types.byte_t'right => 'H',
       others => '0');
    Top_Aggregate(Byte_T'right) := 'H';
    for Index in Byte_T'range loop
      if Index = 5 then
        next;
      end if;
      Forward_Order := Forward_Order * 10 + Index;
    end loop;
    for Index in
      work.array_types.byte_t'reverse_range(1) loop
      if Index = 6 then
        exit;
      end if;
      Backward_Order := Backward_Order * 10 + Index;
    end loop;
    Attribute_Left <= Top_Aggregate'left;
    Attribute_Right <=
      work.array_types.byte_t'right(1);
    Attribute_Length <= Byte_T'length;
    Attribute_Ascending <= Byte_T'ascending;
    Range_Order <= Forward_Order;
    Reverse_Order <= Backward_Order;
    Matrix_Dynamic_Read <=
      Matrix_Local(Matrix_Row, Matrix_Column);
    Matrix_Local(Matrix_Row, Matrix_Column) := '0';
    Matrix_Local(0, 3 downto 2) := "10";
    Matrix_Local_Result <= Matrix_Local;
    Matrix_Signal_Target(1, 2) <= '1';
    Matrix_Dynamic_Signal_Target(Matrix_Row, Matrix_Column) <= '1';
    Vector_Local(1) := "1111";
    Vector_Target <= Vector_Local;
    Cell_Local(3) := (Flag => true, Data => "11");
    Cell_Targets <= Cell_Local;
    Null_Local := (others => '1');
    Null_Local_Copy := Null_Local;
    Null_Copy <= Null_Signal;
    Null_Equal <= Null_Local = Null_Local_Copy;
    Null_Not_Equal <= Null_Local /= Null_Local_Copy;
    Null_Slice_Equal <= Source(0 downto 1) = Null_Local;
    for Index in Null_Local'range loop
      Null_Count := Null_Count + 1;
    end loop;
    Null_Iterations <= Null_Count + Null_Local'length;
    wait;
  end process;

  child : entity work.Array_Child(rtl)
    generic map (Width => Byte_Width)
    port map (
      Source => Source,
      Result => Result
    );

  boundary_a : entity work.Array_Boundary_Child(rtl)
    port map (
      Matrix_Input => Boundary_Matrix_A_Input,
      Matrix_Output => Boundary_Matrix_A_Output,
      Cells_Input => Boundary_Cells_A_Input,
      Cells_Output => Boundary_Cells_A_Output,
      Selected => Boundary_Selected_A
    );
  boundary_b : Array_Boundary_Child
    port map (
      Matrix_Input => Boundary_Matrix_B_Input,
      Matrix_Output => Boundary_Matrix_B_Output,
      Cells_Input => Boundary_Cells_B_Input,
      Cells_Output => Boundary_Cells_B_Output,
      Selected => Boundary_Selected_B
    );
  generic_boundary : Array_Generic_Boundary_Child
    generic map (
      Component_Rows => 2,
      Component_Columns => 3
    )
    port map (
      Matrix_Input => Generic_Boundary_Matrix_Input,
      Matrix_Output => Generic_Boundary_Matrix_Output,
      Cells_Input => Generic_Boundary_Cells_Input,
      Cells_Output => Generic_Boundary_Cells_Output
    );

  Boundary_Matrix_A_Input <= (others => (others => '1'));
  Boundary_Matrix_B_Input <= (others => (others => '1'));
  Boundary_Cells_A_Input <=
    (others =>
       (Flag => true,
        Detail => (Mode => Run, Data => "11")));
  Boundary_Cells_B_Input <=
    (others =>
       (Flag => true,
        Detail => (Mode => Run, Data => "11")));
  Generic_Boundary_Matrix_Input <=
    (others => (others => '1'));
  Generic_Boundary_Cells_Input <=
    (others =>
       (Flag => true,
        Detail => (Mode => Run, Data => "11")));

  Callable_Matrix_Input <= (others => (others => '1'));
  Callable_Package_Result <=
    Package_Matrix_Copy(Callable_Matrix_Input);
  Callable_Cells_Input <=
    (others =>
       (Flag => true,
        Detail => (Mode => Run, Data => "11")));
  Callable_Local_Result <=
    Local_Cells_Copy(Local_Cells_Copy(Callable_Cells_Input));

  callable_procedure : process(Callable_Cells_Input)
    variable Target : Callable_Cells_T :=
      (others =>
         (Flag => false,
          Detail => (Mode => Idle, Data => "00")));
  begin
    Package_Cells_Copy(Callable_Cells_Input, Target);
    Callable_Procedure_Result <= Target;
  end process;

  callable_generate : if true generate
    function Generated_Matrix_Copy(
      Value : Callable_Matrix_T) return Callable_Matrix_T is
      variable Result : Callable_Matrix_T :=
        (others => (others => '0'));
    begin
      Result := Value;
      return Result;
    end function;
  begin
    Callable_Generated_Result <=
      Generated_Matrix_Copy(Callable_Matrix_Input);
  end generate;

  Disjoint_Driver_Matrix(0, 3 downto 1) <= "101";
  Disjoint_Driver_Matrix(1, 3 downto 1) <= "010";
  Disjoint_Driver_Expected <= ("101", "010");
  Disjoint_Driver_Equal <=
    Disjoint_Driver_Matrix = Disjoint_Driver_Expected;

  Resolved_Driver_Matrix(0, 3 downto 1) <= "10Z";
  Resolved_Driver_Matrix(1, 3 downto 1) <= "0H1";
  Resolved_Driver_Expected <= ("10Z", "0H1");
  Resolved_Driver_Equal <=
    Resolved_Driver_Matrix = Resolved_Driver_Expected;
  Partial_Sensitivity_Result <=
    Composite_Driver_Cells(1).Detail.Data(0);

  composite_zero : process
  begin
    Composite_Driver_Cells(0).Flag <= true;
    Composite_Driver_Cells(0).Detail <=
      (Mode => Run, Data => "11");
    wait;
  end process;
  composite_one : process
  begin
    Composite_Driver_Cells(1).Flag <= true;
    Composite_Driver_Cells(1).Detail <=
      (Mode => Run, Data => "11");
    wait;
  end process;
  Composite_Driver_Expected <=
    (others =>
       (Flag => true,
        Detail => (Mode => Run, Data => "11")));
  Composite_Driver_Equal <=
    Composite_Driver_Cells = Composite_Driver_Expected;

  Conditional_Result <=
    (others => '0') when false else
    (7 downto 4 => '1', 3 | 1 => 'Z',
     work.array_types.first_index => 'H', others => '0');
  Slice_Result <= Result(3 downto 0);
  Indexed_Result <= Result(7);
  Equal_Result <= Result = Source;
  Ascending_Result <=
    (0 to 3 => '1', 4 | 6 => 'Z', others => '0');
  Boolean_Result <=
    (0 | 2 => true, others => false);
  Aggregate_Positional <=
    ('0', '1', 'L', 'H', '1', '0', 'Z', '-');
  Aggregate_Equal <=
    Aggregate_Named =
      (7 downto 4 => '1', 3 | 1 => 'Z',
       First_Index => 'H', others => '0');
  Attribute_Slice <=
    Aggregate_Named(
      Byte_T'high downto Byte_T'high - 3);
  Matrix_Positional <=
    (('1', '0', '1'), ('0', '1', '0'));
  Matrix_Named <=
    (0 => (3 => '1', 2 => '0', others => '1'),
     1 => (others => '0'));
  Matrix_Ranged <=
    (0 to 0 => (others => '1'),
     others => ('0', '0', '1'));
  Matrix_Conditional <=
    (others => (others => '0')) when false else
    (0 => ('1', '0', '1'), others => ('0', '1', '0'));
  Nested <=
    ((3 => '1', others => '0'),
     ('0', '1', '0', '1'));
  Cells <=
    (2 => (Flag => true, Data => "10"),
     others => (Data => "01", Flag => false));
  Matrix_Cell_Read <= Matrix_Positional(1, 2);
  Matrix_Slice_Read <= Matrix_Positional(0, 3 downto 2);
  Nested_Read <= Nested(1)(2);
  Vector_Rows <= ("1010", "0101");
  Vector_Read <= Vector_Rows(1);
  Vector_Slice_Read <= Vector_Rows(0)(2 downto 1);
  Cell_Selected <= Cells(2);
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
    assert(reference.composite_values == cold.composite_values);
    assert(reference.top_local == cold.top_local);
    assert(reference.dynamic_local == cold.dynamic_local);
    assert(
        reference.dynamic_slice_local
        == cold.dynamic_slice_local);
    assert(reference.top_aggregate == cold.top_aggregate);
    assert(reference.child_local == cold.child_local);
    assert(
        reference.generic_aggregate
        == cold.generic_aggregate);
    assert(reference.debugger_output == cold.debugger_output);
    assert(reference.vcd == cold.vcd);
    assert(cold.values == warm.values);
    assert(cold.composite_values == warm.composite_values);
    assert(cold.top_local == warm.top_local);
    assert(cold.dynamic_local == warm.dynamic_local);
    assert(
        cold.dynamic_slice_local
        == warm.dynamic_slice_local);
    assert(cold.top_aggregate == warm.top_aggregate);
    assert(cold.child_local == warm.child_local);
    assert(
        cold.generic_aggregate
        == warm.generic_aggregate);
    assert(cold.debugger_output == warm.debugger_output);
    assert(cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0);
    assert(cold.compiled_modules == 5);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 5);
    assert(cold.cache.stores == 5);
    assert(warm.compiled_processes == cold.compiled_processes);
    assert(warm.compiled_modules == cold.compiled_modules);
    assert(warm.cache.hits == 5);
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
    assert(changed.compiled_processes == cold.compiled_processes);
    assert(changed.compiled_modules == 5);
    assert(changed.cache.hits == 0);
    assert(changed.cache.misses == 5);
    assert(changed.cache.stores == 5);
#endif
    write_package("revision one");
  }

  {
    std::ofstream output{failure_source};
    output << R"(
entity Dynamic_Failure is
end entity;

architecture rtl of Dynamic_Failure is
begin
  fail : process
    variable Value : std_logic_vector(7 downto 0) :=
      "00000000";
    variable Index : integer := 8;
  begin
    Value(Index) := '1';
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "vhdl-dynamic-index-failure";
    config.project.top = "vhdl:work.dynamic_failure(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path =
        directory.path
        / (optimization == fsim::project::Optimization::o0
               ? "failure-cache-o0"
               : "failure-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = {failure_source};
    config.source_sets.push_back(std::move(sources));
    const auto reference = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::interpreter,
        "dynamic packed index is outside the declared range");
    const auto compiled = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::compiled,
        "dynamic packed index is outside the declared range");
    assert(reference == compiled);
  }

  {
    std::ofstream output{slice_failure_source};
    output << R"(
entity Dynamic_Slice_Failure is
end entity;

architecture rtl of Dynamic_Slice_Failure is
begin
  fail : process
    variable Value : std_logic_vector(7 downto 0) := "00000000";
    variable Result : std_logic_vector(3 downto 0);
    variable Left_Bound : integer := 6;
    variable Right_Bound : integer := 4;
  begin
    Result := Value(Left_Bound downto Right_Bound);
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "vhdl-dynamic-slice-failure";
    config.project.top = "vhdl:work.dynamic_slice_failure(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path =
        directory.path
        / (optimization == fsim::project::Optimization::o0
               ? "slice-failure-cache-o0"
               : "slice-failure-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = {slice_failure_source};
    config.source_sets.push_back(std::move(sources));
    const auto reference = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::interpreter,
        "VHDL integer subtype range check failed");
    const auto compiled = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::compiled,
        "VHDL integer subtype range check failed");
    assert(reference == compiled);
  }

  {
    std::ofstream output{nested_composite_failure_source};
    output << R"(
entity Nested_Composite_Failure is
end entity;

architecture rtl of Nested_Composite_Failure is
  type Detail_T is record
    Data : bit_vector(1 downto 0);
  end record;
  type Cell_T is record
    Flag : boolean;
    Detail : Detail_T;
  end record;
  type Cells_T is array (0 to 1) of Cell_T;
begin
  fail : process
    variable Value : Cells_T :=
      (others =>
         (Flag => false, Detail => (Data => "00")));
    variable Index : integer := 2;
  begin
    Value(Index).Detail.Data(0) := '1';
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "vhdl-nested-composite-failure";
    config.project.top =
        "vhdl:work.nested_composite_failure(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path =
        directory.path
        / (optimization == fsim::project::Optimization::o0
               ? "nested-composite-failure-cache-o0"
               : "nested-composite-failure-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = {nested_composite_failure_source};
    config.source_sets.push_back(std::move(sources));
    const auto reference = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::interpreter,
        "VHDL integer subtype range check failed");
    const auto compiled = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::compiled,
        "VHDL integer subtype range check failed");
    assert(reference == compiled);
  }

  {
    std::ofstream output{multidimensional_failure_source};
    output << R"(
entity Multidimensional_Failure is
end entity;

architecture rtl of Multidimensional_Failure is
  type Matrix_T is array (0 to 1, 3 downto 1) of bit;
begin
  fail : process
    variable Value : Matrix_T := (others => (others => '0'));
    variable Row : integer := 2;
    variable Column : integer := 2;
  begin
    Value(Row, Column) := '1';
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "vhdl-multidimensional-failure";
    config.project.top =
        "vhdl:work.multidimensional_failure(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path =
        directory.path
        / (optimization == fsim::project::Optimization::o0
               ? "multidimensional-failure-cache-o0"
               : "multidimensional-failure-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = {multidimensional_failure_source};
    config.source_sets.push_back(std::move(sources));
    const auto reference = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::interpreter,
        "VHDL integer subtype range check failed");
    const auto compiled = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::compiled,
        "VHDL integer subtype range check failed");
    assert(reference == compiled);
  }

  {
    std::ofstream output{boundary_shape_failure_source};
    output << R"(
package Boundary_Shape_Types is
  type Matrix_T is array
    (natural range <>, positive range <>) of bit;
end package;

use work.boundary_shape_types.all;
entity Boundary_Shape_Child is
  port (Value : in Matrix_T(0 to 1, 3 downto 1));
end entity;

architecture rtl of Boundary_Shape_Child is
begin
end architecture;

use work.boundary_shape_types.all;
entity Boundary_Shape_Top is
end entity;

use work.boundary_shape_types.all;
architecture rtl of Boundary_Shape_Top is
  signal Value : Matrix_T(1 downto 0, 1 to 3);
begin
  child : entity work.Boundary_Shape_Child(rtl)
    port map (Value => Value);
end architecture;
)";
    assert(output.good());
  }
  expect_build_failure(
      make_failure_config(
          directory.path,
          boundary_shape_failure_source,
          "vhdl-boundary-shape-failure",
          "vhdl:work.boundary_shape_top(rtl)"),
      "FSIM-ELAB-BIND-031");

  {
    std::ofstream output{component_shape_failure_source};
    output << R"(
package Component_Shape_Types is
  type Matrix_T is array
    (natural range <>, positive range <>) of bit;
end package;

use work.component_shape_types.all;
entity Component_Shape_Child is
  port (Value : in Matrix_T(0 to 1, 3 downto 1));
end entity;

architecture rtl of Component_Shape_Child is
begin
end architecture;

use work.component_shape_types.all;
entity Component_Shape_Top is
end entity;

use work.component_shape_types.all;
architecture rtl of Component_Shape_Top is
  component Component_Shape_Child is
    port (Value : in Matrix_T(1 downto 0, 1 to 3));
  end component;
  signal Value : Matrix_T(1 downto 0, 1 to 3);
begin
  child : Component_Shape_Child port map (Value => Value);
end architecture;
)";
    assert(output.good());
  }
  expect_build_failure(
      make_failure_config(
          directory.path,
          component_shape_failure_source,
          "vhdl-component-shape-failure",
          "vhdl:work.component_shape_top(rtl)"),
      "FSIM-ELAB-VHCOMP-007");

  {
    std::ofstream output{callable_shape_failure_source};
    output << R"(
package Callable_Shape_Types is
  type Matrix_T is array
    (natural range <>, positive range <>) of bit;
end package;

use work.callable_shape_types.all;
entity Callable_Shape_Top is
end entity;

use work.callable_shape_types.all;
architecture rtl of Callable_Shape_Top is
  function Copy_Matrix(
    Value : Matrix_T(0 to 1, 3 downto 1))
    return Matrix_T(0 to 1, 3 downto 1) is
  begin
    return Value;
  end function;
  signal Source : Matrix_T(1 downto 0, 1 to 3);
  signal Result : Matrix_T(0 to 1, 3 downto 1);
begin
  Result <= Copy_Matrix(Source);
end architecture;
)";
    assert(output.good());
  }
  expect_build_failure(
      make_failure_config(
          directory.path,
          callable_shape_failure_source,
          "vhdl-callable-shape-failure",
          "vhdl:work.callable_shape_top(rtl)"),
      "FSIM-ELAB-VHOVER-002");

  {
    std::ofstream output{overlapping_driver_failure_source};
    output << R"(
entity Overlapping_Driver_Failure is
end entity;

architecture rtl of Overlapping_Driver_Failure is
  type Matrix_T is array (0 to 1, 3 downto 1) of bit;
  signal Value : Matrix_T;
begin
  Value(0, 3 downto 2) <= "10";
  Value(0, 2 downto 1) <= "01";
end architecture;
)";
    assert(output.good());
  }
  expect_build_failure(
      make_failure_config(
          directory.path,
          overlapping_driver_failure_source,
          "vhdl-overlapping-driver-failure",
          "vhdl:work.overlapping_driver_failure(rtl)"),
      "FSIM-ELAB-DRV-001");

  std::cout << "VHDL array application tests passed\n";
  return 0;
}
