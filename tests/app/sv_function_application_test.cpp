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

struct Capture {
  fsim::runtime::RunResult result;
  std::array<std::string, 10> values;
  fsim::runtime::simir::ContainerValue returned;
  fsim::runtime::simir::ContainerValue qualified_returned;
  fsim::runtime::simir::ContainerValue selected;
  fsim::runtime::simir::ContainerValue dynamic_returned;
  fsim::runtime::simir::ContainerValue queue_returned;
  fsim::runtime::simir::ContainerValue associative_returned;
  fsim::runtime::simir::ContainerValue partial_queue_first;
  fsim::runtime::simir::ContainerValue partial_queue_second;
  fsim::runtime::simir::ContainerValue conditional_returned;
  fsim::runtime::simir::ContainerValue located_returned;
  std::vector<std::string> keys;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  std::vector<std::string> locals;
  std::string vcd;
  std::size_t call_operations{};
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-functions";
  config.project.top = "sv:work.function_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language =
      fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {package_source, top_source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

void print_diagnostics(
    const fsim::diagnostic::Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.span.path << ':'
              << diagnostic.span.begin.line << ':'
              << diagnostic.span.begin.column << ": "
              << diagnostic.code << ": "
              << diagnostic.message << '\n';
  }
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    print_diagnostics(diagnostics);
  }
  assert(project);
  assert(project->design.specializations().size() == 1);

  Capture capture;
  capture.keys = project->specialization_cache_keys;
  assert(project->design.processes().size() == 1);
  capture.call_operations = std::ranges::count_if(
      project->design.processes().front().operations,
      [](const auto& operation) {
        return std::holds_alternative<
            fsim::runtime::simir::Call>(operation);
      });
  for (const auto& local :
       project->design.processes().front().debug_locals) {
    capture.locals.push_back(local.name);
  }
  for (const auto& local :
       project->design.processes().front().debug_container_locals) {
    capture.locals.push_back(local.name);
  }

  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  simulation.set_execution_point_hook(
      [&capture](
          fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
      });

  constexpr std::array<std::string_view, 10> paths{
      "function_top.imported_result",
      "function_top.qualified_result",
      "function_top.array_witness",
      "function_top.reset_witness",
      "function_top.nonstatic_witness",
      "function_top.nonstatic_reset_witness",
      "function_top.task_witness",
      "function_top.consumer_query",
      "function_top.consumer_reduction",
      "function_top.consumer_condition"};
  std::array<fsim::runtime::simir::SignalId, paths.size()>
      signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 32};
  const auto witness_trace = vcd.declare_signal(
      "function_top.array_witness", 8);
  const auto nonstatic_witness_trace = vcd.declare_signal(
      "function_top.nonstatic_witness", 8);
  const auto consumer_trace = vcd.declare_signal(
      "function_top.consumer_condition", 8);
  vcd.begin(simulation.now());
  vcd.change(
      witness_trace, simulation.read_signal(signals[2]));
  vcd.change(
      nonstatic_witness_trace,
      simulation.read_signal(signals[4]));
  vcd.change(consumer_trace, simulation.read_signal(signals[9]));
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        if (signal == signals[2]) {
          vcd.set_time(time);
          vcd.change(witness_trace, value);
        } else if (signal == signals[4]) {
          vcd.set_time(time);
          vcd.change(nonstatic_witness_trace, value);
        } else if (signal == signals[9]) {
          vcd.set_time(time);
          vcd.change(consumer_trace, value);
        }
      });
  capture.result = simulation.run();
  vcd.flush();
  capture.vcd = vcd_output.str();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  const auto returned = simulation.design().find_container(
      "function_top.returned");
  const auto selected = simulation.design().find_container(
      "function_top.selected");
  const auto qualified_returned =
      simulation.design().find_container(
          "function_top.qualified_returned");
  const auto dynamic_returned = simulation.design().find_container(
      "function_top.dynamic_returned");
  const auto queue_returned = simulation.design().find_container(
      "function_top.queue_returned");
  const auto associative_returned =
      simulation.design().find_container(
          "function_top.associative_returned");
  const auto partial_queue_first =
      simulation.design().find_container(
          "function_top.partial_queue_first");
  const auto partial_queue_second =
      simulation.design().find_container(
          "function_top.partial_queue_second");
  const auto conditional_returned =
      simulation.design().find_container(
          "function_top.conditional_returned");
  const auto located_returned =
      simulation.design().find_container(
          "function_top.located_returned");
  assert(
      returned && qualified_returned && selected
      && dynamic_returned && queue_returned
      && associative_returned && partial_queue_first
      && partial_queue_second && conditional_returned
      && located_returned);
  capture.returned = simulation.read_container_object(*returned);
  capture.qualified_returned =
      simulation.read_container_object(*qualified_returned);
  capture.selected = simulation.read_container_object(*selected);
  capture.dynamic_returned =
      simulation.read_container_object(*dynamic_returned);
  capture.queue_returned =
      simulation.read_container_object(*queue_returned);
  capture.associative_returned =
      simulation.read_container_object(*associative_returned);
  capture.partial_queue_first =
      simulation.read_container_object(*partial_queue_first);
  capture.partial_queue_second =
      simulation.read_container_object(*partial_queue_second);
  capture.conditional_returned =
      simulation.read_container_object(*conditional_returned);
  capture.located_returned =
      simulation.read_container_object(*located_returned);
  return capture;
}

void verify(
    const Capture& capture,
    const std::array<std::string, 10>& expected) {
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  if (capture.values != expected) {
    for (std::size_t index = 0; index < expected.size(); ++index) {
      std::cerr << "function value " << index << " expected "
                << expected[index] << " observed "
                << capture.values[index] << '\n';
    }
    const auto print_container = [](const std::string_view name,
                                    const auto& value) {
      std::cerr << name << " size " << value.elements.size()
                << " values";
      for (const auto& element : value.elements) {
        std::cerr << ' ' << element.low_word().aval;
      }
      std::cerr << '\n';
    };
    print_container("dynamic", capture.dynamic_returned);
    print_container("queue", capture.queue_returned);
    print_container("associative", capture.associative_returned);
  }
  assert(capture.values == expected);
  const auto calls = std::ranges::count_if(
      capture.points,
      [](const auto& point) {
        return point.kind
            == fsim::runtime::simir::ExecutionPointKind::call;
      });
  if (calls != 11) {
    std::cerr << "unexpected function call point count: "
              << calls << '\n';
  }
  assert(calls == 11);
  if (capture.call_operations != 27) {
    std::cerr << "unexpected lowered Call operation count: "
              << capture.call_operations << '\n';
  }
  assert(capture.call_operations == 27);
  assert(std::ranges::find(
             capture.locals, "inner.temporary")
         != capture.locals.end());
  assert(std::ranges::find(
             capture.locals, "relay.relay")
         != capture.locals.end());
  assert(std::ranges::find(
             capture.locals, "nested_dynamic.nested_dynamic")
         != capture.locals.end());
  assert(std::ranges::find(
             capture.locals, "nested_dynamic.scratch")
         != capture.locals.end());
  assert(
      capture.returned.type.fixed
      && capture.returned.type.index_left == 10
      && capture.returned.type.index_right == 7
      && capture.returned.elements.size() == 4
      && capture.qualified_returned.type.fixed
      && capture.qualified_returned.type.index_left == 3
      && capture.qualified_returned.type.index_right == 0
      && capture.qualified_returned.elements.size() == 4
      && capture.selected.type.fixed
      && capture.selected.type.index_left == 5
      && capture.selected.type.index_right == 0
      && capture.selected.elements.size() == 6);
  for (std::size_t ordinal = 0; ordinal < 4; ++ordinal) {
    const auto expected_element =
        UINT64_C(41) + ordinal
        + (expected[0] == "00101011" ? UINT64_C(1) : UINT64_C(0));
    assert(
        capture.returned.elements[ordinal].low_word().aval
        == expected_element);
    const auto shifted_expected =
        ordinal < 3
            ? expected_element + 1U
            : UINT64_C(0xee);
    assert(
        capture.selected.elements[ordinal + 1]
            .low_word().aval == shifted_expected);
    const auto qualified_expected =
        UINT64_C(2) + ordinal
        + (expected[0] == "00101011"
               ? UINT64_C(1)
               : UINT64_C(0));
    if (capture.qualified_returned.elements[ordinal]
            .low_word().aval != qualified_expected) {
      std::cerr << "qualified return ordinal " << ordinal
                << " expected " << qualified_expected
                << " observed "
                << capture.qualified_returned.elements[ordinal]
                       .low_word().aval
                << '\n';
    }
    assert(
        capture.qualified_returned.elements[ordinal]
            .low_word().aval == qualified_expected);
  }
  assert(
      capture.selected.elements.front().low_word().aval == 0xee
      && capture.selected.elements.back().low_word().aval == 0xee
      && capture.vcd.find(
             expected[2] == "00101100"
                 ? "b00101100"
                 : "b00101101")
          != std::string::npos
      && capture.vcd.find("b01001011") != std::string::npos);
  const auto avals = [](const auto& value) {
    std::vector<std::uint64_t> result;
    for (const auto& element : value.elements) {
      result.push_back(element.low_word().aval);
    }
    return result;
  };
  assert(
      !capture.dynamic_returned.type.fixed
      && !capture.dynamic_returned.type.queue
      && !capture.dynamic_returned.type.associative
      && avals(capture.dynamic_returned)
          == std::vector<std::uint64_t>({51, 52, 53}));
  assert(
      capture.queue_returned.type.queue
      && capture.queue_returned.type.maximum_elements
          == std::optional<std::uint32_t>{4}
      && avals(capture.queue_returned)
          == std::vector<std::uint64_t>({61, 62, 63})
      && capture.partial_queue_first == capture.queue_returned
      && capture.partial_queue_second.elements.empty());
  assert(
      capture.associative_returned.type.associative
      && capture.associative_returned.keys.size() == 2
      && capture.associative_returned.keys[0].low_word().aval
          == UINT64_C(0xfffffffe)
      && capture.associative_returned.keys[1].low_word().aval == 5
      && avals(capture.associative_returned)
          == std::vector<std::uint64_t>({71, 75}));
  assert(
      avals(capture.conditional_returned)
          == std::vector<std::uint64_t>({51, 32, 53})
      && avals(capture.located_returned)
          == std::vector<std::uint64_t>({62, 63})
      && capture.vcd.find("b00100000") != std::string::npos);
}

bool same_points(
    const std::vector<fsim::runtime::simir::ExecutionPoint>& left,
    const std::vector<fsim::runtime::simir::ExecutionPoint>& right) {
  return std::ranges::equal(
      left,
      right,
      [](const auto& first, const auto& second) {
        return first.process == second.process
            && first.instruction == second.instruction
            && first.kind == second.kind
            && first.source == second.source;
      });
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-functions-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto package_source =
      directory.path / "function_package.sv";
  const auto top_source = directory.path / "function_top.sv";

  const auto write_package =
      [&](const unsigned increment) {
        std::ofstream output(
            package_source,
            std::ios::binary | std::ios::trunc);
        output
            << "package function_pkg;\n"
            << "  typedef int key_t;\n"
            << "  function automatic logic [7:0] package_step(\n"
            << "      input logic [7:0] value);\n"
            << "    return value + " << increment << ";\n"
            << "  endfunction\n"
            << "  function automatic logic [7:0] package_words[3:0](\n"
            << "      input logic [7:0] value);\n"
            << "    package_words[3] = value + " << increment << ";\n"
            << "    package_words[2] = value + " << increment + 1 << ";\n"
            << "    package_words[1] = value + " << increment + 2 << ";\n"
            << "    package_words[0] = value + " << increment + 3 << ";\n"
            << "  endfunction\n"
            << "  function automatic byte package_dynamic[](\n"
            << "      input byte value[]);\n"
            << "    return value;\n"
            << "  endfunction\n"
            << "  function automatic byte package_queue[$:3](\n"
            << "      input byte value[$:3]);\n"
            << "    package_queue = value;\n"
            << "  endfunction\n"
            << "  function automatic byte package_associative[key_t](\n"
            << "      input byte value[key_t]);\n"
            << "    return value;\n"
            << "  endfunction\n"
            << "endpackage\n";
        assert(output.good());
      };
  write_package(1);
  {
    std::ofstream output(top_source, std::ios::binary);
    output << R"(
module function_top #(
    parameter int RETURN_LEFT = 10,
    parameter int QUEUE_MAXIMUM = 3);
  import function_pkg::*;

  function automatic int width_for(input int value);
    logic [31:0] width;
    width = 4;
    for (int index = 0; index < 2; index++)
      width = width + 2;
    case (value)
      5: return width;
      default: return 4;
    endcase
  endfunction

  localparam int WIDTH = width_for(5);
  logic [WIDTH-1:0] imported_result;
  logic [WIDTH-1:0] qualified_result;
  logic [7:0] returned[RETURN_LEFT:RETURN_LEFT-3];
  logic [7:0] qualified_returned[3:0];
  logic [7:0] selected[5:0];
  logic [7:0] partial_first[3:0];
  logic [7:0] partial_second[3:0];
  logic [7:0] array_witness;
  logic reset_witness;
  byte dynamic_source[];
  byte dynamic_returned[];
  byte queue_source[$:QUEUE_MAXIMUM];
  byte queue_returned[$:QUEUE_MAXIMUM];
  byte associative_source[key_t];
  byte associative_returned[key_t];
  byte partial_queue_first[$:QUEUE_MAXIMUM];
  byte partial_queue_second[$:QUEUE_MAXIMUM];
  byte consumer_alternative[];
  byte conditional_returned[];
  byte located_returned[$];
  logic [7:0] nonstatic_witness;
  logic nonstatic_reset_witness;
  logic [7:0] task_witness;
  logic [31:0] consumer_query;
  logic [7:0] consumer_reduction;
  logic [7:0] consumer_condition;

  function automatic logic [WIDTH-1:0] inner(
      input logic [WIDTH-1:0] value);
    logic [WIDTH-1:0] temporary;
    begin : calculate
      temporary = value;
      for (int index = 0; index < 2; index++)
        temporary = temporary + 1;
      case (value[0])
        1'b0: temporary = temporary - 1;
        default: temporary = temporary - 1;
      endcase
      if (value == value)
        return temporary;
      return value;
    end
  endfunction

  function automatic logic [WIDTH-1:0] outer(
      input logic [WIDTH-1:0] value);
    outer = inner(value);
  endfunction

  function automatic logic [7:0] relay[
      RETURN_LEFT:RETURN_LEFT-3](
      input logic [7:0] value);
    return package_words(value);
  endfunction

  function automatic logic [7:0] slice_return[3:0](
      input logic [7:0] value[
          RETURN_LEFT:RETURN_LEFT-3]);
    return value[RETURN_LEFT -: 4];
  endfunction

  function automatic logic [7:0] partial[3:0](
      input logic complete);
    partial[3] = 8'h91;
    if (complete) begin
      partial[2] = 8'h82;
      partial[1] = 8'h73;
      partial[0] = 8'h64;
    end
  endfunction

  function automatic byte nested_dynamic[](
      input byte value[]);
    byte scratch[];
    scratch = package_dynamic(value);
    return package_dynamic(scratch);
  endfunction

  function automatic byte partial_queue[$:QUEUE_MAXIMUM](
      input logic complete,
      input byte value[$:QUEUE_MAXIMUM]);
    if (complete)
      partial_queue = value;
  endfunction

  task automatic consume_queue(
      input byte value[$:QUEUE_MAXIMUM],
      output logic [7:0] observed);
    observed = value[1];
  endtask

  initial begin
    imported_result = package_step(outer(8'd40));
    qualified_result =
        function_pkg::package_step(outer(8'd1));
    returned = relay(8'd40);
    qualified_returned =
        function_pkg::package_words(8'd1);
    selected = '{default: 8'hee};
    selected[4 -: 4] = slice_return(returned);
    selected[4 -: 4] =
        slice_return(selected[3 -: 4]);
    partial_first = partial(1'b1);
    partial_second = partial(1'b0);
    dynamic_source = '{51, 52, 53};
    queue_source = '{61, 62, 63};
    associative_source = '{-2: 71, 5: 75};
    consumer_alternative = '{51, 99, 53};
    dynamic_returned = nested_dynamic(dynamic_source);
    queue_returned =
        function_pkg::package_queue(queue_source);
    associative_returned =
        package_associative(associative_source);
    partial_queue_first = partial_queue(1'b1, queue_source);
    partial_queue_second = partial_queue(1'b0, queue_source);
    array_witness = selected[2];
    reset_witness =
        $isunknown(partial_second[2])
        && $isunknown(partial_second[1])
        && $isunknown(partial_second[0]);
    nonstatic_witness = associative_returned[5];
    nonstatic_reset_witness =
        partial_queue_first.size() == 3
        && partial_queue_second.size() == 0;
    consume_queue(package_queue(queue_source), task_witness);
    consumer_query = $bits(nested_dynamic(dynamic_source))
        + package_queue(queue_source).size();
    consumer_reduction =
        nested_dynamic(dynamic_source).sum() with (
            item.index == 1 ? item : 0);
    conditional_returned = 1'bx
        ? nested_dynamic(dynamic_source)
        : package_dynamic(consumer_alternative);
    consumer_condition = conditional_returned[1];
    located_returned =
        package_queue(queue_source).find() with (item > 61);
    #1;
    $finish;
  end
endmodule
)";
    assert(output.good());
  }

  std::vector<std::string> baseline_o2_keys;
  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path,
        package_source,
        top_source,
        optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const std::array<std::string, 10> expected{
        "00101010", "00000011", "00101100", "1",
        "01001011", "1", "00111110",
        "00000000000000000000000000011011",
        "00110100", "00100000"};
    verify(reference, expected);
    verify(cold, expected);
    verify(warm, expected);
    assert(same_points(reference.points, cold.points));
    assert(same_points(cold.points, warm.points));
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
    if (optimization == fsim::project::Optimization::o2) {
      baseline_o2_keys = warm.keys;
    }
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 1);
    assert(cold.compiled_modules == 1);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 1);
    assert(cold.cache.stores == 1);
    assert(warm.cache.hits == 1);
    assert(warm.cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(cold.compiled_modules == 0);
#endif
  }

  write_package(2);
  const auto changed = run_once(
      make_config(
          directory.path,
          package_source,
          top_source,
          fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled);
  verify(
      changed,
      {"00101011", "00000100", "00101101", "1",
       "01001011", "1", "00111110",
       "00000000000000000000000000011011",
       "00110100", "00100000"});
  assert(baseline_o2_keys.size() == 1);
  assert(changed.keys.size() == 1);
  assert(changed.keys.front() != baseline_o2_keys.front());
#if defined(FSIM_HAS_LLVM)
  assert(changed.cache.hits == 0);
  assert(changed.cache.misses == 1);
  assert(changed.cache.stores == 1);
#endif
}
