// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
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
  fsim::runtime::simir::ContainerValue values;
  fsim::runtime::simir::ContainerValue pending;
  fsim::runtime::simir::ContainerValue lookup;
  fsim::runtime::simir::ContainerValue memory;
  fsim::runtime::simir::ContainerValue binary;
  fsim::runtime::simir::ContainerValue port_result;
  fsim::runtime::simir::ContainerValue port_shared;
  fsim::runtime::simir::ContainerValue dynamic_result;
  fsim::runtime::simir::ContainerValue dynamic_bounded;
  fsim::runtime::simir::ContainerValue dynamic_scores;
  fsim::runtime::simir::ContainerValue dynamic_work;
  std::vector<std::string> output;
  std::size_t compiled{};
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-containers";
  config.project.top = "sv:work.container_top";
  config.project.time_resolution = "1ns";
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
  sources.compilation_unit = "file";
  sources.files = {source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  Capture capture;
  capture.compiled = simulation.compiled_process_count();
  simulation.set_output_hook(
      [&](const auto,
          const std::string_view text,
          const bool,
          const auto,
          const auto) {
        capture.output.emplace_back(text);
      });
  capture.result = simulation.run();
  const auto& objects = simulation.design().container_objects();
  assert(objects.size() == 13);
  capture.values =
      simulation.read_container_object(objects[0].id);
  capture.pending =
      simulation.read_container_object(objects[1].id);
  capture.lookup =
      simulation.read_container_object(objects[2].id);
  capture.memory =
      simulation.read_container_object(objects[3].id);
  capture.binary =
      simulation.read_container_object(objects[4].id);
  const auto port_result =
      simulation.design().find_container(
          "container_top.port_result");
  const auto port_shared =
      simulation.design().find_container(
          "container_top.port_shared");
  assert(port_result && port_shared);
  capture.port_result =
      simulation.read_container_object(*port_result);
  capture.port_shared =
      simulation.read_container_object(*port_shared);
  const auto dynamic_result =
      simulation.design().find_container(
          "container_top.dynamic_result");
  const auto dynamic_bounded =
      simulation.design().find_container(
          "container_top.dynamic_bounded");
  const auto dynamic_scores =
      simulation.design().find_container(
          "container_top.dynamic_scores");
  const auto dynamic_work =
      simulation.design().find_container(
          "container_top.dynamic_work");
  assert(
      dynamic_result && dynamic_bounded
      && dynamic_scores && dynamic_work);
  capture.dynamic_result =
      simulation.read_container_object(*dynamic_result);
  capture.dynamic_bounded =
      simulation.read_container_object(*dynamic_bounded);
  capture.dynamic_scores =
      simulation.read_container_object(*dynamic_scores);
  capture.dynamic_work =
      simulation.read_container_object(*dynamic_work);
  return capture;
}

void inspect_suspended(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  std::optional<fsim::runtime::simir::ProcessId> process_id;
  std::optional<std::size_t> target_index;
  for (const auto& process : simulation.design().processes()) {
    for (std::size_t index = 0;
         index < process.debug_container_locals.size(); ++index) {
      if (process.debug_container_locals[index].name
          == "mutate_lookup.target") {
        process_id = process.id;
        target_index = index;
      }
    }
  }
  assert(process_id && target_index);
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"show", "pending"});
  assert(
      debugger_output.str()
          == "container_top.pending = []\n"
      && debugger_error.str().empty());
  bool requested = false;
  simulation.set_execution_point_hook(
      [&](fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::simir::ExecutionPoint& point) {
        if (!requested
            && point.process == *process_id
            && point.kind
                == fsim::runtime::simir::ExecutionPointKind::
                    process_suspend) {
          requested = true;
          scheduler.request_stop();
        }
      });
  const auto stopped = simulation.run();
  assert(
      requested
      && stopped.status == fsim::runtime::RunStatus::stopped);
  const auto local = simulation.read_process_container_local(
      *process_id, *target_index);
  assert(
      local.keys.size() == 2
      && local.keys[0].low_word().aval
          == UINT64_C(0xffffffff)
      && local.keys[1].low_word().aval == 3
      && local.elements[0].low_word().aval == 9
      && local.elements[1].low_word().aval == 30);
  debugger.execute({"show", "lookup"});
  assert(
      debugger_output.str().find("=>")
      != std::string::npos);
  simulation.clear_stop();
  const auto resumed = simulation.run();
  assert(
      resumed.status == fsim::runtime::RunStatus::completed
      && resumed.time == 3);
}

void inspect_ordering_suspended(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  std::optional<fsim::runtime::simir::ProcessId> process_id;
  std::optional<std::size_t> local_index;
  for (const auto& process : simulation.design().processes()) {
    for (std::size_t index = 0;
         index < process.debug_container_locals.size(); ++index) {
      if (process.debug_container_locals[index].name
          == "mutate.ordered") {
        process_id = process.id;
        local_index = index;
      }
    }
  }
  assert(process_id && local_index);
  std::size_t suspensions{};
  simulation.set_execution_point_hook(
      [&](fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::simir::ExecutionPoint& point) {
        if (point.process == *process_id
            && point.kind
                == fsim::runtime::simir::ExecutionPointKind::
                    process_suspend
            && ++suspensions == 2) {
          scheduler.request_stop();
        }
      });
  const auto stopped = simulation.run();
  assert(
      stopped.status == fsim::runtime::RunStatus::stopped
      && stopped.time == 1);
  const auto local = simulation.read_process_container_local(
      *process_id, *local_index);
  assert(
      local.elements.size() == 5
      && local.elements[0].low_word().aval == 0xfe
      && local.elements[1].low_word().aval == 0xff
      && local.elements[2].low_word().aval == 2
      && local.elements[3].low_word().aval == 3
      && local.elements[4].low_word().aval == 3);
  simulation.clear_stop();
  const auto resumed = simulation.run();
  assert(
      resumed.status == fsim::runtime::RunStatus::completed
      && resumed.time == 3);
}

void inspect_static_suspended(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  std::optional<fsim::runtime::simir::ProcessId> process_id;
  std::optional<std::size_t> target_index;
  for (const auto& process : simulation.design().processes()) {
    for (std::size_t index = 0;
         index < process.debug_container_locals.size(); ++index) {
      if (process.debug_container_locals[index].name
          == "mutate_memory.target") {
        process_id = process.id;
        target_index = index;
      }
    }
  }
  assert(process_id && target_index);
  std::size_t suspensions{};
  simulation.set_execution_point_hook(
      [&](fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::simir::ExecutionPoint& point) {
        if (point.process == *process_id
            && point.kind
                == fsim::runtime::simir::ExecutionPointKind::
                    process_suspend
            && ++suspensions == 3) {
          scheduler.request_stop();
        }
      });
  const auto stopped = simulation.run();
  assert(
      stopped.status == fsim::runtime::RunStatus::stopped
      && stopped.time == 2);
  const auto local = simulation.read_process_container_local(
      *process_id, *target_index);
  assert(
      local.type.fixed
      && local.type.index_left == 3
      && local.type.index_right == 0
      && local.elements[1].low_word().aval == 0x11
      && local.elements[0].low_word().aval == 0x0f);
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"show", "memory"});
  assert(
      debugger_error.str().empty()
      && debugger_output.str().find("3:")
          != std::string::npos);
  simulation.clear_stop();
  const auto resumed = simulation.run();
  assert(
      resumed.status == fsim::runtime::RunStatus::completed
      && resumed.time == 3);
}

void inspect_static_port_aliases(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto parent =
      simulation.design().find_container(
          "container_top.port_result");
  const auto child =
      simulation.design().find_container(
          "container_top.port_child.result");
  assert(parent && child && *parent == *child);
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"scope", "port_child"});
  debugger.execute({"show", "source"});
  assert(
      debugger_error.str().empty()
      && debugger_output.str().find(
             "container_top.port_child.source = [3:")
          != std::string::npos);
  const auto result = simulation.run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 3);
  debugger.execute({"show", "result"});
  assert(
      debugger_output.str().find("3:00110010")
          != std::string::npos
      && debugger_output.str().find("0:00000110")
          != std::string::npos);
}

void inspect_dynamic_port_aliases(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto parent =
      simulation.design().find_container(
          "container_top.dynamic_work");
  const auto child =
      simulation.design().find_container(
          "container_top.dynamic_mid.generated.child.work");
  assert(parent && child && *parent == *child);
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"scope", "dynamic_mid.generated.child"});
  debugger.execute({"show", "source"});
  assert(
      debugger_error.str().empty()
      && debugger_output.str().find(
             "container_top.dynamic_mid.generated.child.source = []")
          != std::string::npos);
  const auto result = simulation.run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 3);
  debugger.execute({"show", "result"});
  debugger.execute({"show", "scores"});
  debugger.execute({"show", "observed_bits"});
  debugger.execute({"show", "observed_sum"});
  assert(
      debugger_output.str().find("00100001")
          != std::string::npos
      && debugger_output.str().find("0001001000110100")
          != std::string::npos
      && debugger_output.str().find(
             "observed_bits = "
             "00000000000000000000000001000000")
          != std::string::npos
      && debugger_output.str().find(
             "observed_sum = "
             "00000000000000000000000001010011")
          != std::string::npos);
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-containers-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "containers.sv";
  {
    std::ofstream output(
        directory.path / "image.hex", std::ios::binary);
    output << "a5 /* exact */ xz @3 0f // tail\n";
    assert(output.good());
  }
  {
    std::ofstream output(
        directory.path / "image.bin", std::ios::binary);
    output << "0001 10z1 0011\n";
    assert(output.good());
  }
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
module static_port_leaf #(
    parameter int LEFT = 3,
    parameter int RIGHT = 0) (
    input logic [7:0] source[LEFT:RIGHT],
    output logic [7:0] result[LEFT:RIGHT],
    inout bit [3:0] shared[-1:1]);
  initial begin
    logic [7:0] located[$];
    int locations[$];
    #1;
    assert ($left(source) == LEFT);
    assert ($right(source) == RIGHT);
    assert ($low(source) == RIGHT);
    assert ($high(source) == LEFT);
    assert ($increment(source) == 1);
    assert ($size(source, LEFT - LEFT + 1) == 4);
    assert ($bits(source) == 32);
    assert ($dimensions(source) == 2);
    assert ($unpacked_dimensions(source) == 1);
    located = source.min();
    assert (located.size() == 1);
    assert (located[0] == 8'h04);
    located = source.max();
    assert ($isunknown(located[0]));
    located = source.unique();
    assert (located.size() == 3);
    locations = source.unique_index();
    assert (locations.size() == 3);
    assert (locations[0] == LEFT);
    assert (locations[2] == RIGHT);
    located = source.find() with (item < 8'h30);
    assert (located.size() == 1);
    assert (located[0] == 8'h04);
    locations = source.find_index() with (item < 8'h30);
    assert (locations.size() == 1);
    assert (locations[0] == RIGHT);
    result = '{8'h10, 8'h20, 8'h30, 8'h40};
    assert (result.sum() == 8'ha0);
    result.reverse();
    assert (result[LEFT] == 8'h40);
    result.reverse();
    result.sort();
    assert (result[LEFT] == 8'h10);
    result.rsort();
    assert (result[LEFT] == 8'h40);
    result.reverse();
    assert (result[LEFT] == 8'h10);
    assert (result[RIGHT] == 8'h40);
    assert (source[LEFT] == 8'h31);
    assert (source[RIGHT] == 8'h04);
    result = source;
    result[LEFT] = source[LEFT] + 8'h01;
    result[RIGHT] = source[RIGHT] + 8'h02;
    shared[-1] = 4'ha;
    shared[1] = 4'hc;
  end
endmodule

module dynamic_port_leaf #(
    parameter int LIMIT = 3,
    parameter type KEY = logic signed [3:0]) (
    input int source[],
    output byte result[$],
    inout bit bounded[$:LIMIT],
    inout logic [15:0] scores[KEY],
    inout int work[]);
  int observed_bits;
  int observed_sum;
  task automatic suspend_mutate(
      inout int target[],
      inout byte queue_target[$]);
    int scratch[];
    #1;
    scratch = '{99};
    assert (scratch.sum() == 99);
    target = '{41, 42};
    queue_target = '{8'h21, 8'h22};
    assert ($left(target) == 0);
    assert ($right(target) == 1);
    assert ($size(target) == 2);
    assert ($bits(queue_target) == 16);
    assert ($dimensions(target) == 2);
    assert ($unpacked_dimensions(target) == 1);
    assert ($size(scratch) == 1);
    assert ($bits(scratch) == 32);
  endtask
  initial begin
    KEY cursor;
    int located[$];
    int locations[$];
    byte byte_located[$];
    #1;
    assert ($left(source) == 0);
    assert ($right(source) == 1);
    assert ($low(source) == 0);
    assert ($high(source) == 1);
    assert ($increment(source) == -1);
    assert ($size(source, LIMIT - LIMIT + 1) == 2);
    assert ($bits(source) == 64);
    assert ($dimensions(source) == 2);
    assert ($unpacked_dimensions(source) == 1);
    assert ($right(result) == -1);
    assert ($high(result) == -1);
    assert ($bits(result) == 0);
    assert (source.size() == 2);
    assert (source[0] == 11);
    assert (source.sum() == 23);
    located = source.min();
    assert (located[0] == 11);
    located = source.max();
    assert (located[0] == 12);
    locations = source.unique_index();
    assert (locations[0] == 0);
    located = source.find() with (item > 11);
    assert (located.size() == 1);
    assert (located[0] == 12);
    locations = source.find_last_index() with (item >= LIMIT + 8);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    bounded = '{};
    assert ($size(bounded) == 0);
    bounded = '{1'b0, 1'b1};
    bounded.reverse();
    assert (bounded[0] == 1);
    bounded.sort();
    scores = '{};
    assert ($size(scores) == 0);
    assert (scores.sum() == 0);
    assert (scores.product() == 1);
    assert (scores.and() == 16'hffff);
    assert (scores.or() == 0);
    assert (scores.xor() == 0);
    scores = '{-1: 16'h1234, 2: 16'h5678};
    assert (scores.sum() == 16'h68ac);
    assert (scores.first(cursor) == 1);
    assert (cursor == -1);
    assert (scores.next(cursor) == 1);
    assert (cursor == 2);
    suspend_mutate(work, result);
    byte_located = result.unique();
    assert (byte_located.size() == 2);
    locations = work.unique_index();
    assert (locations[0] == 0);
    result.reverse();
    assert (result[0] == 8'h22);
    result.sort();
    work.rsort();
    assert (work[0] == 42);
    work.reverse();
    work.sort();
    observed_bits = $bits(work);
    observed_sum = work.sum();
    assert (observed_bits == 64);
    assert (observed_sum == 83);
    assert ($right(work) == 1);
    assert ($high(work) == 1);
    assert ($size(bounded) == 2);
    assert ($bits(bounded) == 2);
    assert ($size(scores) == 2);
    assert ($bits(scores) == 32);
    assert ($dimensions(scores) == 2);
    assert ($unpacked_dimensions(scores) == 1);
  end
endmodule

module dynamic_port_mid #(
    parameter int MAXIMUM = 3,
    parameter type INDEX = logic signed [3:0]) (
    input int source[],
    output byte result[$],
    inout bit bounded[$:MAXIMUM],
    inout logic [15:0] scores[INDEX],
    inout int work[]);
  generate
    if (MAXIMUM == 3) begin : generated
      dynamic_port_leaf #(
          .LIMIT(MAXIMUM), .KEY(INDEX)) child(
          .source(source),
          .result(result),
          .bounded(bounded),
          .scores(scores),
          .work(work));
    end
  endgenerate
endmodule

module container_top;
  typedef logic signed [31:0] key_t;
  typedef logic signed [3:0] dynamic_key_t;
  int values[];
  byte pending[$:2];
  byte lookup[key_t];
  logic [7:0] memory[3:0];
  logic [7:0] binary[-1:1];
  logic [7:0] port_source[3:0];
  logic [7:0] port_result[3:0];
  bit [3:0] port_shared[-1:1];
  int dynamic_source[];
  byte dynamic_result[$];
  bit dynamic_bounded[$:3];
  logic [15:0] dynamic_scores[dynamic_key_t];
  int dynamic_work[];
  static_port_leaf port_child(
      .source(port_source),
      .result(port_result),
      .shared(port_shared));
  dynamic_port_mid #(
      .MAXIMUM(3), .INDEX(dynamic_key_t)) dynamic_mid(
      .source(dynamic_source),
      .result(dynamic_result),
      .bounded(dynamic_bounded),
      .scores(dynamic_scores),
      .work(dynamic_work));
  function automatic int count(input byte source[$:2]);
    byte copy[$:2];
    copy = '{5, 6};
    assert (copy[1] == 6);
    assert ($left(source) == 0);
    assert ($right(source) == 1);
    assert ($bits(source) == 16);
    assert (source.sum() == 6);
    return $size(source);
  endfunction
  function automatic int lookup_count(input byte source[key_t]);
    assert (source.sum() == 40);
    return source.size();
  endfunction
  function automatic int isolated_count(input byte source[key_t]);
    byte copy[key_t];
    copy = source;
    copy.delete(3);
    return copy.size();
  endfunction
  task automatic mutate(inout byte target[$:2]);
    byte ordered[$];
    byte located[$];
    int locations[$];
    ordered = '{8'h03, 8'hff, 8'h03, 8'h02, 8'hfe};
    ordered.sort();
    assert (ordered[0] == -2);
    assert (ordered[1] == -1);
    assert (ordered[4] == 3);
    located = ordered.min();
    assert (located[0] == -2);
    located = ordered.max();
    assert (located[0] == 3);
    located = ordered.unique();
    assert (located.size() == 4);
    locations = ordered.unique_index();
    assert (locations.size() == 4);
    located = ordered.find() with (item < 0);
    assert (located.size() == 2);
    assert (located[0] == -2);
    assert (located[1] == -1);
    locations =
        ordered.find_first_index() with (item == 3);
    assert (locations.size() == 1);
    assert (locations[0] == 2);
    target.push_back(4);
    target.rsort();
    assert (target[0] == 4);
    target.reverse();
    target.sort();
    #1;
    target.pop_front();
  endtask
  task automatic mutate_lookup(inout byte target[key_t]);
    target[-1] = 9;
    #1;
    target.delete(3);
  endtask
  task automatic mutate_memory(
      inout logic [7:0] target[3:0]);
    target[2] = 8'h11;
    #1;
    target[3] = 8'h22;
  endtask
  initial begin
    key_t key;
    int located[$];
    int locations[$];
    logic [7:0] logic_located[$];
    port_source[3] = 8'h31;
    port_source[0] = 8'h04;
    dynamic_source = '{11, 12};
    binary = '{8'h01, 8'b10z1, 8'h03};
    assert ($isunknown(binary[0]));
    assert ($isunknown(binary.sum()));
    assert ($isunknown(binary.product()));
    assert (binary.and() == 8'h01);
    assert (binary.or() == 8'h0b);
    assert ($isunknown(binary.xor()));
    binary.reverse();
    assert (binary[-1] == 8'h03);
    binary.reverse();
    binary.sort();
    assert (binary[-1] == 8'h01);
    assert (binary[0] == 8'h03);
    assert ($isunknown(binary[1]));
    binary.rsort();
    assert ($isunknown(binary[-1]));
    assert (binary[0] == 8'h03);
    assert (binary[1] == 8'h01);
    binary = '{8'h01, 8'b10z1, 8'h03};
    $readmemh("image.hex", memory);
    $readmemb("image.bin", binary, -1, 1);
    assert (memory[0] == 8'ha5);
    assert ($isunknown(memory[1]));
    assert ($isunknown(memory[2]));
    assert (memory[3] == 8'h0f);
    assert (binary[-1] == 8'h01);
    assert ($isunknown(binary[0]));
    assert (binary[1] == 8'h03);
    assert ($left(binary) == -1);
    assert ($right(binary) == 1);
    assert ($low(binary) == -1);
    assert ($high(binary) == 1);
    assert ($increment(binary) == -1);
    assert ($size(binary) == 3);
    assert ($bits(binary) == 24);
    logic_located = binary.min();
    assert (logic_located[0] == 8'h01);
    logic_located = binary.max();
    assert ($isunknown(logic_located[0]));
    logic_located = binary.unique();
    assert (logic_located.size() == 3);
    locations = binary.unique_index();
    assert (locations[0] == -1);
    logic_located = binary.find() with (item == 8'h03);
    assert (logic_located.size() == 1);
    assert (logic_located[0] == 8'h03);
    locations = binary.find_last_index() with (item != 8'h01);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    values = '{};
    assert (values.sum() == 0);
    assert (values.product() == 1);
    assert (values.and() == -1);
    assert (values.or() == 0);
    assert (values.xor() == 0);
    values = '{7, -8, 7};
    assert (values[1] == -8);
    assert (values.sum() == 6);
    assert (values.product() == -392);
    assert (values.and() == 0);
    assert (values.or() == -1);
    assert (values.xor() == -8);
    located = values.min();
    assert (located[0] == -8);
    located = values.max();
    assert (located[0] == 7);
    located = values.unique();
    assert (located.size() == 2);
    assert (located[0] == 7);
    assert (located[1] == -8);
    locations = values.unique_index();
    assert (locations.size() == 2);
    assert (locations[0] == 0);
    assert (locations[1] == 1);
    located = values.find() with (item == 7);
    assert (located.size() == 2);
    assert (located[0] == 7);
    assert (located[1] == 7);
    locations =
        values.find_index() with (item < 0 || item > 8);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    located = values.find_first() with (!(item == 7));
    assert (located.size() == 1);
    assert (located[0] == -8);
    locations =
        values.find_last_index() with (item >= -8 && item < 0);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    values.sort();
    assert (values[0] == -8);
    values.rsort();
    assert (values[0] == 7);
    values.reverse();
    assert (values[0] == -8);
    values.reverse();
    lookup = '{};
    assert (lookup.size() == 0);
    lookup = '{3: 30, -1: 10};
    assert (lookup.sum() == 40);
    assert (lookup.product() == 44);
    assert (lookup.and() == 10);
    assert (lookup.or() == 30);
    assert (lookup.xor() == 20);
    assert (lookup_count(lookup) == 2);
    assert (isolated_count(lookup) == 1);
    assert (lookup.size() == 2);
    assert (lookup.exists(3) == 1);
    assert (lookup[4] == 0);
    assert (lookup.first(key) == 1);
    assert (key == -1);
    assert (lookup.next(key) == 1);
    assert (key == 3);
    assert (lookup.last(key) == 1);
    assert (key == 3);
    assert (lookup.prev(key) == 1);
    assert (key == -1);
    mutate_lookup(lookup);
    pending = '{};
    assert (pending.size() == 0);
    assert (pending.sum() == 0);
    assert (pending.product() == 1);
    assert (pending.and() == 8'hff);
    assert (pending.or() == 0);
    assert (pending.xor() == 0);
    pending = '{1, 2};
    mutate(pending);
    assert (pending.sum() == 6);
    assert (pending.product() == 8);
    assert (pending.and() == 0);
    assert (pending.or() == 6);
    assert (pending.xor() == 6);
    mutate_memory(memory);
    assert (port_result[3] == 8'h32);
    assert (port_result[0] == 8'h06);
    assert (port_shared[-1] == 4'ha);
    assert (port_shared[0] == 0);
    assert (port_shared[1] == 4'hc);
    assert (dynamic_result.size() == 2);
    assert (dynamic_result[0] == 8'h21);
    assert (dynamic_result[1] == 8'h22);
    assert (dynamic_bounded.size() == 2);
    assert (dynamic_bounded[0] == 0);
    assert (dynamic_bounded[1] == 1);
    assert (dynamic_scores.size() == 2);
    assert (dynamic_scores[-1] == 16'h1234);
    assert (dynamic_scores[2] == 16'h5678);
    assert (dynamic_work.size() == 2);
    assert (dynamic_work[0] == 41);
    assert (dynamic_work[1] == 42);
    assert ($left(dynamic_work) == 0);
    assert ($right(dynamic_work) == 1);
    assert ($low(dynamic_work) == 0);
    assert ($high(dynamic_work) == 1);
    assert ($increment(dynamic_work) == -1);
    assert ($size(dynamic_work) == 2);
    assert ($bits(dynamic_work) == 64);
    assert ($dimensions(dynamic_work) == 2);
    assert ($unpacked_dimensions(dynamic_work) == 1);
    $display("%0d:%0d:%0d:%0d:%0d",
             values[0], pending[0], count(pending),
             $size(dynamic_result), pending.sum());
  end
endmodule
)";
    assert(output.good());
  }
  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config =
        config_for(directory.path, source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto compiled =
        run_once(config, fsim::app::SimulationEngine::compiled);
    assert(
        reference.result.status
            == fsim::runtime::RunStatus::completed
        && reference.result.time == 3
        && reference.output
            == (std::vector<std::string>{
                "7", ":2", ":2", ":2", ":6"}));
    assert(reference.output == compiled.output);
    assert(reference.values == compiled.values);
    assert(reference.pending == compiled.pending);
    assert(reference.lookup == compiled.lookup);
    assert(reference.memory == compiled.memory);
    assert(reference.binary == compiled.binary);
    assert(reference.port_result == compiled.port_result);
    assert(reference.port_shared == compiled.port_shared);
    assert(reference.dynamic_result == compiled.dynamic_result);
    assert(
        reference.dynamic_bounded == compiled.dynamic_bounded);
    assert(reference.dynamic_scores == compiled.dynamic_scores);
    assert(reference.dynamic_work == compiled.dynamic_work);
    assert(
        compiled.pending.elements.size() == 2
        && compiled.pending.elements[0].low_word().aval == 2
        && compiled.pending.elements[1].low_word().aval == 4);
    assert(
        compiled.lookup.keys.size() == 1
        && compiled.lookup.keys[0].low_word().aval
            == UINT64_C(0xffffffff)
        && compiled.lookup.elements[0].low_word().aval == 9);
    assert(
        compiled.memory.type.fixed
        && compiled.memory.type.index_left == 3
        && compiled.memory.type.index_right == 0
        && compiled.memory.elements[0].to_msb_string()
            == "00100010"
        && compiled.memory.elements[1].to_msb_string()
            == "00010001"
        && compiled.memory.elements[2].to_msb_string()
            == "XXXXZZZZ"
        && compiled.memory.elements[3].to_msb_string()
            == "10100101");
    assert(
        compiled.binary.type.fixed
        && compiled.binary.type.index_left == -1
        && compiled.binary.type.index_right == 1
        && compiled.binary.elements[0].to_msb_string()
            == "00000001"
        && compiled.binary.elements[1].to_msb_string()
            == "000010Z1"
        && compiled.binary.elements[2].to_msb_string()
            == "00000011");
    assert(
        compiled.port_result.elements[0].low_word().aval
            == 0x32
        && compiled.port_result.elements[3].low_word().aval
            == 0x06
        && compiled.port_shared.elements[0].low_word().aval
            == 0xa
        && compiled.port_shared.elements[1].low_word().aval
            == 0
        && compiled.port_shared.elements[2].low_word().aval
            == 0xc);
    assert(
        compiled.dynamic_result.elements.size() == 2
        && compiled.dynamic_result.elements[0].low_word().aval
            == 0x21
        && compiled.dynamic_result.elements[1].low_word().aval
            == 0x22
        && !compiled.dynamic_result.type.maximum_elements);
    assert(
        compiled.dynamic_bounded.type.maximum_elements
        && *compiled.dynamic_bounded.type.maximum_elements == 4
        && compiled.dynamic_bounded.elements.size() == 2
        && compiled.dynamic_scores.keys.size() == 2
        && compiled.dynamic_scores.type.index_width == 4
        && compiled.dynamic_work.elements.size() == 2
        && compiled.dynamic_work.elements[0].low_word().aval
            == 41
        && compiled.dynamic_work.elements[1].low_word().aval
            == 42);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled == 3);
#endif
    inspect_suspended(
        config, fsim::app::SimulationEngine::interpreter);
    inspect_suspended(
        config, fsim::app::SimulationEngine::compiled);
    inspect_ordering_suspended(
        config, fsim::app::SimulationEngine::interpreter);
    inspect_ordering_suspended(
        config, fsim::app::SimulationEngine::compiled);
    inspect_static_suspended(
        config, fsim::app::SimulationEngine::interpreter);
    inspect_static_suspended(
        config, fsim::app::SimulationEngine::compiled);
    inspect_static_port_aliases(
        config, fsim::app::SimulationEngine::interpreter);
    inspect_static_port_aliases(
        config, fsim::app::SimulationEngine::compiled);
    inspect_dynamic_port_aliases(
        config, fsim::app::SimulationEngine::interpreter);
    inspect_dynamic_port_aliases(
        config, fsim::app::SimulationEngine::compiled);
  }
}
