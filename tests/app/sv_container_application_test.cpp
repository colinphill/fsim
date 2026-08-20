// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  fsim::runtime::simir::ContainerValue packed_lookup;
  fsim::runtime::simir::ContainerValue memory;
  fsim::runtime::simir::ContainerValue binary;
  fsim::runtime::simir::ContainerValue port_result;
  fsim::runtime::simir::ContainerValue port_shared;
  fsim::runtime::simir::ContainerValue dynamic_result;
  fsim::runtime::simir::ContainerValue dynamic_bounded;
  fsim::runtime::simir::ContainerValue dynamic_scores;
  fsim::runtime::simir::ContainerValue dynamic_work;
  fsim::runtime::simir::ContainerValue slice_source;
  fsim::runtime::simir::ContainerValue slice_result;
  fsim::runtime::simir::ContainerValue slice_shared;
  std::vector<std::string> output;
  std::string vcd;
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
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(project);
  assert(project->design_ir.valid(project->semantics));
  for (const auto& [path, runtime_object] :
       project->design.container_paths()) {
    assert(
        std::ranges::count_if(
            project->design_ir.objects(), [&](const auto& object) {
              return object.kind
                      == fsim::semantic::design::ObjectKind::container
                  && object.path == path
                  && object.runtime_index == runtime_object;
            })
        == 1);
  }
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  Capture capture;
  simulation.set_output_hook(
      [&](const auto,
          const std::string_view text,
          const bool,
          const auto,
          const auto) {
        capture.output.emplace_back(text);
      });
  const auto observed =
      simulation.find_signal("container_top.slice_observed");
  assert(observed);
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{
      vcd_output, "1ns", 32};
  const auto observed_trace =
      vcd.declare_signal(
          "container_top.slice_observed", 8);
  vcd.begin(simulation.now());
  vcd.change(
      observed_trace, simulation.read_signal(*observed));
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        if (signal != *observed) {
          return;
        }
        vcd.set_time(time);
        vcd.change(observed_trace, value);
      });
  capture.result = simulation.run();
  capture.compiled = simulation.compiled_process_count();
  vcd.flush();
  capture.vcd = vcd_output.str();
  const auto& objects = simulation.design().container_objects();
  assert(objects.size() == 20);
  capture.values =
      simulation.read_container_object(objects[0].id);
  capture.pending =
      simulation.read_container_object(objects[1].id);
  capture.lookup =
      simulation.read_container_object(objects[2].id);
  const auto packed_lookup = simulation.design().find_container(
      "container_top.packed_lookup");
  assert(packed_lookup);
  capture.packed_lookup = simulation.read_container_object(*packed_lookup);
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
  const auto slice_source =
      simulation.design().find_container(
          "container_top.slice_source");
  const auto slice_result =
      simulation.design().find_container(
          "container_top.slice_result");
  const auto slice_shared =
      simulation.design().find_container(
          "container_top.slice_shared");
  assert(slice_source && slice_result && slice_shared);
  capture.slice_source =
      simulation.read_container_object(*slice_source);
  capture.slice_result =
      simulation.read_container_object(*slice_result);
  capture.slice_shared =
      simulation.read_container_object(*slice_shared);
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
  for (std::size_t id = 0;
       id < simulation.design_ir().processes().size(); ++id) {
    const auto& process = simulation.process_program(
        static_cast<fsim::runtime::simir::ProcessId>(id));
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
      && local.keys[0].known_signed_value() == -1
      && local.keys[1].known_signed_value() == 3
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
      && resumed.time == 4);
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
  for (std::size_t id = 0;
       id < simulation.design_ir().processes().size(); ++id) {
    const auto& process = simulation.process_program(
        static_cast<fsim::runtime::simir::ProcessId>(id));
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
      && resumed.time == 4);
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
  for (std::size_t id = 0;
       id < simulation.design_ir().processes().size(); ++id) {
    const auto& process = simulation.process_program(
        static_cast<fsim::runtime::simir::ProcessId>(id));
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
      && resumed.time == 4);
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
  const auto leaf =
      simulation.design().find_container(
          "container_top.port_child.generated.child.result");
  assert(
      parent && child && leaf
      && *parent == *child
      && *child == *leaf);
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"scope", "port_child.generated.child"});
  debugger.execute({"show", "source"});
  assert(
      debugger_error.str().empty()
      && debugger_output.str().find(
             "container_top.port_child.generated.child.source = [3:")
          != std::string::npos);
  const auto result = simulation.run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 4);
  debugger.execute({"show", "result"});
  assert(
      debugger_output.str().find("3:00110010")
          != std::string::npos
      && debugger_output.str().find("0:00000110")
          != std::string::npos);
}

void inspect_static_slice_port_aliases(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto parent_source =
      simulation.design().find_container(
          "container_top.slice_source");
  const auto mid_source =
      simulation.design().find_container(
          "container_top.slice_mid.source");
  const auto leaf_source =
      simulation.design().find_container(
          "container_top.slice_mid.generated.child.source");
  const auto parent_result =
      simulation.design().find_container(
          "container_top.slice_result");
  const auto mid_result =
      simulation.design().find_container(
          "container_top.slice_mid.result");
  assert(
      parent_source && mid_source && leaf_source
      && parent_result && mid_result
      && *parent_source != *mid_source
      && *mid_source == *leaf_source
      && *parent_result != *mid_result);
  const auto& source_info =
      simulation.design().container_objects().at(*mid_source);
  const auto& result_info =
      simulation.design().container_objects().at(*mid_result);
  assert(
      source_info.type.index_left == -2
      && source_info.type.index_right == 0
      && source_info.slice_alias
      && source_info.slice_alias->object == *parent_source
      && source_info.slice_alias->selected_left == 4
      && source_info.slice_alias->selected_right == 2
      && result_info.type.index_left == 9
      && result_info.type.index_right == 7
      && result_info.slice_alias
      && result_info.slice_alias->object == *parent_result
      && result_info.slice_alias->selected_left == 3
      && result_info.slice_alias->selected_right == 1);
  std::ostringstream debugger_output;
  std::ostringstream debugger_error;
  fsim::app::DebuggerControl debugger{
      simulation, debugger_output, debugger_error};
  debugger.execute({"scope", "slice_mid.generated.child"});
  debugger.execute({"show", "source"});
  assert(
      debugger_error.str().empty()
      && debugger_output.str().find(
             "slice_mid.generated.child.source = [-2:")
          != std::string::npos);
  const auto result = simulation.run();
  assert(
      result.status == fsim::runtime::RunStatus::completed
      && result.time == 4);
  debugger.execute({"show", "result"});
  debugger.execute({"show", "shared"});
  assert(
      debugger_output.str().find("9:10XZ0011")
          != std::string::npos
      && debugger_output.str().find("8:0000Z010")
          != std::string::npos
      && debugger_output.str().find("5:0000X001")
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
      && result.time == 4);
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

int run_capacity_case() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-container-capacity-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "capacity.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
module container_capacity;
  byte dynamic[];
  byte lookup[int];
  initial begin
    dynamic = new[4097];
    dynamic[4096] = 8'h5a;
    assert (dynamic.size() == 4097);
    assert (dynamic[4096] == 8'h5a);
    for (int index = 0; index < 4097; ++index)
      lookup[index] = index;
    assert (lookup.size() == 4097);
    assert (lookup[4096] == 8'h00);
  end
endmodule
)";
    assert(output.good());
  }
  const auto optimizations = {fsim::project::Optimization::o0};
  for (const auto optimization : optimizations) {
    auto config = config_for(directory.path, source, optimization);
    config.project.name = "sv-container-capacity";
    config.project.top = "sv:work.container_capacity";
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(project);
    fsim::app::Simulation simulation{
        std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::compiled};
    assert(simulation.run().status
        == fsim::runtime::RunStatus::completed);
  }
  return 0;
}

}  // namespace

int fsim_application_case_sv_container_capacity() {
  return run_capacity_case();
}

#if defined(FSIM_MERGED_APPLICATION_TESTS)
int main() {
#else
int main(const int argc, const char* const argv[]) {
  if (argc == 2 && std::string_view{argv[1]} == "capacity") {
    return run_capacity_case();
  }
#endif
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
  function automatic logic [7:0] port_slice_sum(
      input logic [7:0] value[1:0]);
    return value.sum();
  endfunction
  task automatic preserve_port_slice(
      inout logic [7:0] value[1:0]);
    assert ($isunknown(value[1]));
    assert (value[0] == 8'h04);
    value[0] = value[0] + 8'h00;
  endtask
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
    assert ($left(source[RIGHT +: 3]) == LEFT - 1);
    assert ($right(source[RIGHT +: 3]) == RIGHT);
    assert ($size(source[RIGHT +: 3], 1) == 3);
    assert ($bits(source[RIGHT +: 3]) == 24);
    assert (
        source[LEFT:RIGHT].sum(slice_item) with (
            slice_item.index == LEFT
                ? slice_item : 8'h00) == 8'h31);
    located =
        source[RIGHT +: 3].find() with (
            item < 8'h30);
    assert (located.size() == 1);
    assert (located[0] == 8'h04);
    locations =
        source[RIGHT +: 3].find_index(port_item) with (
            port_item.index == RIGHT);
    assert (locations.size() == 1);
    assert (locations[0] == RIGHT);
    assert (
        $isunknown(
            port_slice_sum(
                source[LEFT - 1 +: 2])));
    located = source.min();
    assert (located.size() == 1);
    assert (located[0] == 8'h04);
    located = source.max();
    assert ($isunknown(located[0]));
    located =
        source.min() with (
            item.index >= 2 ? 8'hff : item);
    assert (located.size() == 1);
    assert (located[0] == 8'h04);
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
    assert (
        result.sum(reduced_item) with (
            reduced_item.index < 2
                ? reduced_item : 8'h00) == 8'h70);
    located =
        result.unique() with (
            item.index >= 2 ? 8'h00 : item);
    assert (located.size() == 3);
    assert (located[0] == 8'h10);
    assert (located[1] == 8'h30);
    assert (located[2] == 8'h40);
    locations =
        result.unique_index(entry) with (
            entry.index >= 2 ? 8'h00 : entry);
    assert (locations.size() == 3);
    assert (locations[0] == LEFT);
    assert (locations[1] == RIGHT + 1);
    assert (locations[2] == RIGHT);
    result.rsort(cell) with (
        cell.index >= 2 ? 8'h00 : cell);
    assert (result[LEFT] == 8'h40);
    assert (result[LEFT - 1] == 8'h30);
    assert (result[RIGHT + 1] == 8'h10);
    assert (result[RIGHT] == 8'h20);
    result = '{8'h10, 8'h20, 8'h30, 8'h40};
    result[RIGHT + 1 +: 2].reverse();
    assert (result[LEFT] == 8'h10);
    assert (result[LEFT - 1] == 8'h30);
    assert (result[RIGHT + 1] == 8'h20);
    assert (result[RIGHT] == 8'h40);
    result[LEFT - 1 -: 2].sort();
    assert (result[LEFT] == 8'h10);
    assert (result[LEFT - 1] == 8'h20);
    assert (result[RIGHT + 1] == 8'h30);
    assert (result[RIGHT] == 8'h40);
    result[RIGHT + 1 +: 2].rsort(port_order) with (
        port_order.index == LEFT - 1 ? 8'h00 : 8'h01);
    assert (result[LEFT] == 8'h10);
    assert (result[LEFT - 1] == 8'h30);
    assert (result[RIGHT + 1] == 8'h20);
    assert (result[RIGHT] == 8'h40);
    result[LEFT - 1 -: 2].sort();
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
    result = '{
        RIGHT: source[RIGHT] + 8'h02,
        default: 8'h20,
        LEFT: source[LEFT] + 8'h01};
    result[RIGHT + 1 +: 2] =
        source[RIGHT +: 2];
    assert (result[LEFT] == 8'h32);
    assert ($isunknown(result[LEFT - 1]));
    assert (result[RIGHT + 1] == 8'h04);
    assert (result[RIGHT] == 8'h06);
    located = result[LEFT:RIGHT + 1].unique();
    assert (located.size() == 3);
    locations = result[LEFT:RIGHT + 1].unique_index();
    assert (locations.size() == 3);
    assert (locations[0] == LEFT);
    assert (locations[2] == RIGHT + 1);
    preserve_port_slice(
        result[RIGHT + 1 +: 2]);
    assert ($isunknown(result[LEFT - 1]));
    assert (result[RIGHT + 1] == 8'h04);
    shared[-1] = 4'ha;
    shared[1] = 4'hc;
  end
endmodule

module static_port_mid #(
    parameter int LEFT = 3,
    parameter int RIGHT = 0) (
    input logic [7:0] source[LEFT:RIGHT],
    output logic [7:0] result[LEFT:RIGHT],
    inout bit [3:0] shared[-1:1]);
  generate
    if (LEFT >= RIGHT) begin: generated
      static_port_leaf #(
          .LEFT(LEFT), .RIGHT(RIGHT)) child(
          .source(source),
          .result(result),
          .shared(shared));
    end
  endgenerate
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
    assert (
        source.sum(source_sum_item) with (
            source_sum_item.index == 1
                ? source_sum_item : 0) == 12);
    located = source.min();
    assert (located[0] == 11);
    located = source.max();
    assert (located[0] == 12);
    located =
        source.min() with (
            item.index == 0 ? 99 : item);
    assert (located.size() == 1);
    assert (located[0] == 12);
    locations = source.unique_index();
    assert (locations[0] == 0);
    locations =
        source.unique_index(source_item) with (
            source_item.index < 2 ? 0 : source_item);
    assert (locations.size() == 1);
    assert (locations[0] == 0);
    located =
        source.find(source_item) with (
            source_item.index == 1 && source_item > 11);
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
    work.rsort(work_item) with (
        work_item.index == 0 ? 0 : work_item);
    assert (work[0] == 42);
    assert (work[1] == 41);
    work = '{41, 42};
    result.reverse();
    assert (result[0] == 8'h22);
    result.sort();
    work.rsort();
    assert (work[0] == 42);
    work.reverse();
    work.sort();
    observed_bits = $bits(work);
    observed_sum = work.sum();
    assert (
        work.sum() with (
            item.index == 0 ? item : 0) == 41);
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

)";
    output << R"(
module slice_port_leaf(
    input logic [7:0] source[-2:0],
    output logic [7:0] result[9:7],
    inout logic [7:0] shared[4:6]);
  initial begin
    #1;
    assert (source[-2] === 8'b10xz0011);
    assert (source[-1] === 8'h32);
    assert (source[0] === 8'h22);
    assert (shared[4] === 8'h11);
    assert (shared[5] === 8'b0000x001);
    assert (shared[6] === 8'h13);
    result[9] = source[-2];
    result[8] = source[-1];
    result[7] = source[0];
    shared[4] = 8'ha1;
    shared[5] = 8'b0000z010;
    shared[6] = 8'ha3;
    #1;
    result[8] = 8'b0000z010;
    shared[5] = 8'b0000x001;
  end
endmodule

module slice_port_mid(
    input logic [7:0] source[-2:0],
    output logic [7:0] result[9:7],
    inout logic [7:0] shared[4:6]);
  generate
    if (1) begin : generated
      slice_port_leaf child(source, result, shared);
    end
  endgenerate
endmodule

module container_top;
  typedef logic signed [136:0] key_t;
  typedef struct packed {
    logic [68:0] high;
    logic [67:0] low;
  } packed_key_t;
  typedef logic signed [3:0] dynamic_key_t;
  int values[];
  byte pending[$:2];
  byte lookup[key_t];
  logic [7:0] memory[3:0];
  logic [7:0] binary[-1:1];
  byte packed_lookup[packed_key_t];
  logic [7:0] port_source[3:0];
  logic [7:0] port_result[3:0];
  bit [3:0] port_shared[-1:1];
  int dynamic_source[];
  byte dynamic_result[$];
  bit dynamic_bounded[$:3];
  logic [15:0] dynamic_scores[dynamic_key_t];
  int dynamic_work[];
  logic [7:0] slice_source[5:0];
  logic [7:0] slice_result[4:0];
  logic [7:0] slice_shared[-2:2];
  logic [7:0] slice_observed;
  static_port_mid port_child(
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
  slice_port_mid slice_mid(
      .source(slice_source[2 +: 3]),
      .result(slice_result[1 +: 3]),
      .shared(slice_shared[-1 +: 3]));
  function automatic int count(input byte source[$:2]);
    byte copy[$:2];
    copy = '{5, 6};
    assert (copy[1] == 6);
    assert ($left(source) == 0);
    assert ($right(source) == 1);
    assert ($bits(source) == 16);
    assert (source.sum() == 6);
    assert (
        source.sum(sum_item) with (
            sum_item.index > 0 ? sum_item : 0) == source[1]);
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
  function automatic byte keyed_static_value(
      input logic [7:0] source[3:0]);
    logic [7:0] copy[3:0];
    int locations[$];
    copy = '{
        3: source[3],
        default: 8'h55,
        0: source[0]};
    assert (copy[2] == 8'h55);
    assert (copy[1] == 8'h55);
    copy[2:1] = source[1:0];
    assert (copy[2] == source[1]);
    assert (copy[1] == source[0]);
    assert ($left(source[1:0]) == 1);
    assert ($size(source[1:0]) == 2);
    assert (source[1:0].sum() == 8'ha8);
    locations =
        source[1:0].find_index() with (
            item.index == 0);
    assert (locations.size() == 1);
    assert (locations[0] == 0);
    return copy[3];
  endfunction
  function automatic logic [7:0] slice_sum(
      input logic [7:0] source[6:5]);
    assert ($left(source) == 6);
    assert (source[6] == 8'h03);
    assert (source[5] == 8'ha5);
    return source.sum();
  endfunction
  function automatic logic [7:0] nested_slice_sum(
      input logic [7:0] source[3:0]);
    return slice_sum(source[0 +: 2]);
  endfunction
  task automatic mutate(inout byte target[$:2]);
    byte ordered[$];
    byte located[$];
    int locations[$];
    ordered = '{8'h03, 8'hff, 8'h03, 8'h02, 8'hfe};
    assert (
        ordered.sum(signed_item) with (
            signed_item < 0 ? signed_item : 0) == 8'hfd);
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
    located =
        ordered.unique() with (
            item < 0 ? 0 : item);
    assert (located.size() == 3);
    assert (located[0] == -2);
    assert (located[1] == 2);
    assert (located[2] == 3);
    locations = ordered.unique_index();
    assert (locations.size() == 4);
    locations =
        ordered.unique_index(sorted_item) with (
            sorted_item < 0 ? 0 : sorted_item);
    assert (locations.size() == 3);
    assert (locations[0] == 0);
    assert (locations[1] == 2);
    assert (locations[2] == 3);
    located = ordered.find() with (item < 0);
    assert (located.size() == 2);
    assert (located[0] == -2);
    assert (located[1] == -1);
    locations =
        ordered.find_first_index() with (item == 3);
    assert (locations.size() == 1);
    assert (locations[0] == 2);
    locations =
        ordered.find_index(sorted_item) with (
            sorted_item.index >= 3);
    assert (locations.size() == 2);
    assert (locations[0] == 3);
    assert (locations[1] == 4);
    locations =
        target.find_index(target_item) with (
            target_item.index == 1);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    target.push_back(4);
    target.rsort();
    assert (target[0] == 4);
    target.reverse();
    target.sort();
    target.rsort(target_item) with (
        target_item.index == 0 ? 0 : target_item);
    assert (target[0] == 4);
    assert (target[1] == 2);
    assert (target[2] == 1);
    target.sort();
    #1;
    located =
        target.unique() with (
            item.index < 2 ? 0 : item);
    assert (located.size() == 2);
    assert (located[0] == 1);
    assert (located[1] == 4);
    target.pop_front();
  endtask
  task automatic mutate_lookup(inout byte target[key_t]);
    target[-1] = 9;
    #1;
    target.delete(3);
  endtask
  task automatic mutate_memory(
      inout logic [7:0] target[3:0]);
    logic [7:0] located[$];
    int locations[$];
    target[2] = 8'h11;
    #1;
    target[2:1].sort();
    assert (target[2] == 8'h03);
    assert (target[1] == 8'h11);
    target[2:1].rsort();
    assert (target[2] == 8'h11);
    assert (target[1] == 8'h03);
    target[3:1] = target[2:0];
    assert (target[3] == 8'h11);
    assert (target[2] == 8'h03);
    assert (target[1] == 8'ha5);
    assert ($right(target[3:1]) == 1);
    locations =
        target[3:1].find_index() with (
            item.index == 2);
    assert (locations.size() == 1);
    assert (locations[0] == 2);
    located =
        target[3:1].find_first() with (
            item == 8'ha5);
    assert (located.size() == 1);
    assert (located[0] == 8'ha5);
    target = '{
        3: 8'h22,
        default: 8'bxxxxzzzz,
        0: 8'ha5};
  endtask
  task automatic transfer_slices(
      input logic [7:0] incoming[9:8],
      output logic [7:0] outgoing[1:0],
      inout logic [7:0] working[5:4],
      input bit early);
    assert ($isunknown(outgoing[1]));
    assert ($isunknown(outgoing[0]));
    outgoing[1] = incoming[9];
    working[5] = 8'h11;
    if (early)
      return;
    #1;
    outgoing[0] = incoming[8];
    working[4] = 8'hc4;
  endtask
)";
    output << R"(
  initial begin
    key_t key;
    int located[$];
    int locations[$];
    logic [7:0] logic_located[$];
    int preserved[];
    logic [7:0] fresh[];
    preserved = '{4, 5};
    preserved = new[4](preserved);
    assert (preserved.size() == 4);
    assert (preserved[0] == 4);
    assert (preserved[1] == 5);
    assert (preserved[2] == 0);
    fresh = new[2];
    assert ($isunknown(fresh[0]));
    port_source[3] = 8'h31;
    port_source[0] = 8'h04;
    slice_source = '{
        8'h52, 8'b10xz0011, 8'h32,
        8'h22, 8'h12, 8'h02};
    slice_result = '{
        8'hee, 8'h43, 8'h33, 8'h23, 8'hdd};
    slice_shared = '{
        8'hf2, 8'h11, 8'b0000x001, 8'h13, 8'he2};
    dynamic_source = '{11, 12};
    binary = '{
        32'hffffffff: 8'h01,
        default: 8'b10z1,
        1: 8'h03};
    assert ($isunknown(binary[0]));
    assert ($isunknown(binary.sum()));
    assert ($isunknown(binary.product()));
    assert (binary.and() == 8'h01);
    assert (binary.or() == 8'h0b);
    assert ($isunknown(binary.xor()));
    assert (
        binary.sum() with (
            item.index < 0 ? item : 8'h00) == 8'h01);
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
    binary.sort() with (
        item.index < 0 ? 8'h00 : item);
    assert (binary[-1] == 8'h01);
    assert (binary[0] == 8'h03);
    assert ($isunknown(binary[1]));
    binary = '{8'h01, 8'b10z1, 8'h03};
    $readmemh("image.hex", memory);
    $readmemb("image.bin", binary, -1, 1);
    binary[1 -: 2].reverse();
    assert (binary[0] == 8'h03);
    assert ($isunknown(binary[1]));
    binary[0 +: 2].reverse();
    assert ($isunknown(binary[0]));
    assert (binary[1] == 8'h03);
    assert ($size(binary[1 -: 2]) == 2);
    assert ($bits(binary[0 +: 2]) == 16);
    assert (binary[-1 +: 1].sum() == 8'h01);
    locations =
        binary[1 -: 2].find_index() with (
            item.index == 1);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    memory[1 +: 2] = binary[1 -: 2];
    assert (memory[0] == 8'ha5);
    assert (memory[1] == 8'h03);
    assert ($isunknown(memory[2]));
    assert (memory[3] == 8'h0f);
    assert (slice_sum(memory[0 +: 2]) == 8'ha8);
    assert (nested_slice_sum(memory) == 8'ha8);
    assert (keyed_static_value(memory) == 8'h0f);
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
    locations =
        binary.find_index(pixel) with (pixel.index < 0);
    assert (locations.size() == 1);
    assert (locations[0] == -1);
    logic_located =
        binary.find(pixel) with (
            pixel.index > 0 && pixel == 8'h03);
    assert (logic_located.size() == 1);
    assert (logic_located[0] == 8'h03);
    values = '{};
    assert (values.sum() == 0);
    assert (values.product() == 1);
    assert (values.and() == -1);
    assert (values.or() == 0);
    assert (values.xor() == 0);
    assert (
        values.sum() with (
            item.index >= 0 ? item : 0) == 0);
    values = '{30, 10, 20, 11};
    values.sort() with (
        item.index < 2 ? 0 : item);
    assert (values[0] == 30);
    assert (values[1] == 10);
    assert (values[2] == 11);
    assert (values[3] == 20);
    values.rsort(value_item) with (
        value_item.index < 2 ? 0 : value_item);
    assert (values[0] == 20);
    assert (values[1] == 11);
    assert (values[2] == 30);
    assert (values[3] == 10);
    values = '{7, -8, 7};
    assert (values[1] == -8);
    assert (values.sum() == 6);
    assert (values.product() == -392);
    assert (values.and() == 0);
    assert (values.or() == -1);
    assert (values.xor() == -8);
    assert (
        values.and(and_item) with (and_item) == 0);
    assert (
        values.or(or_item) with (or_item) == -1);
    assert (
        values.xor(xor_item) with (xor_item) == -8);
    assert (
        values.sum() with (
            item < 0 ? item : 0) == -8);
    assert (
        values.product(product_item) with (
            product_item.index == 1
                ? product_item : 1) == -8);
    located = values.min();
    assert (located[0] == -8);
    located = values.max();
    assert (located[0] == 7);
    located =
        values.min() with (
            item < 0 ? 99 : item);
    assert (located.size() == 1);
    assert (located[0] == 7);
    located = values.unique();
    assert (located.size() == 2);
    assert (located[0] == 7);
    assert (located[1] == -8);
    located =
        values.unique() with (
            item < 0 ? 7 : item);
    assert (located.size() == 1);
    assert (located[0] == 7);
    locations = values.unique_index();
    assert (locations.size() == 2);
    assert (locations[0] == 0);
    assert (locations[1] == 1);
    locations =
        values.unique_index(value_item) with (
            value_item < 0 ? 7 : value_item);
    assert (locations.size() == 1);
    assert (locations[0] == 0);
    located = values.find() with (item == 7);
    assert (located.size() == 2);
    assert (located[0] == 7);
    assert (located[1] == 7);
    located =
        values.find(positioned) with (
            positioned.index == 1 && positioned < 0);
    assert (located.size() == 1);
    assert (located[0] == -8);
    locations =
        values.find_index() with (item < 0 || item > 8);
    assert (locations.size() == 1);
    assert (locations[0] == 1);
    locations =
        values.find_index() with (item.index != 1);
    assert (locations.size() == 2);
    assert (locations[0] == 0);
    assert (locations[1] == 2);
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
    begin
      packed_key_t packed_key;
      packed_key = packed_key_t'('0);
      packed_key.high[68] = 1'b1;
      packed_lookup[packed_key] = 8'h5a;
      assert (packed_lookup.exists(packed_key) == 1);
      assert (packed_lookup[packed_key] == 8'h5a);
    end
    pending = '{};
    assert (pending.size() == 0);
    assert (pending.sum() == 0);
    assert (pending.product() == 1);
    assert (pending.and() == 8'hff);
    assert (pending.or() == 0);
    assert (pending.xor() == 0);
    assert (
        pending.product(empty_item) with (
            empty_item.index >= 0 ? empty_item : 1) == 1);
    pending = '{1, 2};
    pending.insert(1, 9);
    assert (pending.size() == 3);
    assert (pending[1] == 9);
    pending.delete(0);
    assert (pending.size() == 2);
    assert (pending[0] == 9);
    pending = '{1, 2};
    mutate(pending);
    assert (pending.sum() == 6);
    assert (pending.product() == 8);
    assert (pending.and() == 0);
    assert (pending.or() == 6);
    assert (pending.xor() == 6);
    assert (
        pending.sum(pending_item) with (
            pending_item.index > 0
                ? pending_item : 0) == pending[1]);
    mutate_memory(memory);
    transfer_slices(
        binary[-1 +: 2],
        memory[2 +: 2],
        memory[1 -: 2],
        1);
    assert (memory[3] == 8'h01);
    assert ($isunknown(memory[2]));
    assert (memory[1] == 8'h11);
    assert (memory[0] == 8'ha5);
    transfer_slices(
        binary[1 -: 2],
        memory[3 -: 2],
        memory[0 +: 2],
        0);
    assert ($isunknown(memory[3]));
    assert (memory[2] == 8'h03);
    assert (memory[1] == 8'h11);
    assert (memory[0] == 8'hc4);
    memory = '{
        3: 8'h22,
        default: 8'bxxxxzzzz,
        0: 8'ha5};
    assert (port_result[3] == 8'h32);
    assert (port_result[2] == 8'h00);
    assert (port_result[1] == 8'h04);
    assert (port_result[0] == 8'h06);
    assert (port_shared[-1] == 4'ha);
    assert (port_shared[0] == 0);
    assert (port_shared[1] == 4'hc);
    assert (slice_source[5] === 8'h52);
    assert (slice_source[4] === 8'b10xz0011);
    assert (slice_source[2] === 8'h22);
    assert (slice_source[0] === 8'h02);
    assert (slice_result[4] === 8'hee);
    assert (slice_result[3] === 8'b10xz0011);
    assert (slice_result[2] === 8'b0000z010);
    assert (slice_result[1] === 8'h22);
    assert (slice_result[0] === 8'hdd);
    assert (slice_shared[-2] === 8'hf2);
    assert (slice_shared[-1] === 8'ha1);
    assert (slice_shared[0] === 8'b0000x001);
    assert (slice_shared[1] === 8'ha3);
    assert (slice_shared[2] === 8'he2);
    slice_observed = slice_result[2];
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
#if defined(FSIM_HAS_LLVM)
  const auto optimizations = {
      fsim::project::Optimization::o0,
      fsim::project::Optimization::o2};
#else
  // Without the LLVM backend, compiled execution is the interpreter and the
  // project optimization setting cannot affect execution. Keep one complete
  // semantic/debugger pass so sanitizer builds exercise every container path
  // without rebuilding the same large fixture four times.
  const auto optimizations = {
      fsim::project::Optimization::o0};
#endif
  for (const auto optimization : optimizations) {
    const auto config =
        config_for(directory.path, source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
#if defined(FSIM_HAS_LLVM)
    const auto compiled =
        run_once(config, fsim::app::SimulationEngine::compiled);
#else
    const auto& compiled = reference;
#endif
    assert(
        reference.result.status
            == fsim::runtime::RunStatus::completed
        && reference.result.time == 4
        && reference.output
            == (std::vector<std::string>{
                "7", ":2", ":2", ":2", ":6"}));
    assert(reference.output == compiled.output);
    assert(reference.values == compiled.values);
    assert(reference.pending == compiled.pending);
    assert(reference.lookup == compiled.lookup);
    assert(reference.packed_lookup == compiled.packed_lookup);
    assert(reference.memory == compiled.memory);
    assert(reference.binary == compiled.binary);
    assert(reference.port_result == compiled.port_result);
    assert(reference.port_shared == compiled.port_shared);
    assert(reference.dynamic_result == compiled.dynamic_result);
    assert(
        reference.dynamic_bounded == compiled.dynamic_bounded);
    assert(reference.dynamic_scores == compiled.dynamic_scores);
    assert(reference.dynamic_work == compiled.dynamic_work);
    assert(reference.slice_source == compiled.slice_source);
    assert(reference.slice_result == compiled.slice_result);
    assert(reference.slice_shared == compiled.slice_shared);
    assert(reference.vcd == compiled.vcd);
    assert(
        compiled.pending.elements.size() == 2
        && compiled.pending.elements[0].low_word().aval == 2
        && compiled.pending.elements[1].low_word().aval == 4);
    assert(
        compiled.lookup.keys.size() == 1
        && compiled.lookup.keys[0].width() == 137
        && compiled.lookup.keys[0].to_msb_string()
            == std::string(137, '1')
        && compiled.lookup.elements[0].low_word().aval == 9);
    assert(
        compiled.packed_lookup.keys.size() == 1
        && compiled.packed_lookup.keys[0].width() == 137
        && compiled.packed_lookup.keys[0].get(136)
            == fsim::runtime::Logic4::one
        && compiled.packed_lookup.elements[0].low_word().aval
            == UINT64_C(0x5a));
    assert(
        compiled.memory.type.fixed
        && compiled.memory.type.index_left == 3
        && compiled.memory.type.index_right == 0
        && compiled.memory.elements[0].to_msb_string()
            == "00100010"
        && compiled.memory.elements[1].to_msb_string()
            == "XXXXZZZZ"
        && compiled.memory.elements[2].to_msb_string()
            == "XXXXZZZZ"
        && compiled.memory.elements[3].to_msb_string()
            == "10100101");
    const auto binary_matches =
        compiled.binary.type.fixed
        && compiled.binary.type.index_left == -1
        && compiled.binary.type.index_right == 1
        && compiled.binary.elements[0].to_msb_string()
            == "00000001"
        && compiled.binary.elements[1].to_msb_string()
            == "000010Z1"
        && compiled.binary.elements[2].to_msb_string()
            == "00000011";
    if (!binary_matches) {
      for (const auto& element : compiled.binary.elements) {
        std::cerr << element.to_msb_string() << ' ';
      }
      std::cerr << '\n';
    }
    assert(binary_matches);
    assert(
        compiled.port_result.elements[0].low_word().aval
            == 0x32
        && compiled.port_result.elements[1].to_msb_string()
            == "XXXXXXXX"
        && compiled.port_result.elements[2].low_word().aval
            == 0x04
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
    assert(
        compiled.slice_source.elements[0].low_word().aval
            == 0x52
        && compiled.slice_source.elements[1].to_msb_string()
            == "10XZ0011"
        && compiled.slice_source.elements[5].low_word().aval
            == 0x02
        && compiled.slice_result.elements[0].low_word().aval
            == 0xee
        && compiled.slice_result.elements[1].to_msb_string()
            == "10XZ0011"
        && compiled.slice_result.elements[2].to_msb_string()
            == "0000Z010"
        && compiled.slice_result.elements[4].low_word().aval
            == 0xdd
        && compiled.slice_shared.elements[0].low_word().aval
            == 0xf2
        && compiled.slice_shared.elements[2].to_msb_string()
            == "0000X001"
        && compiled.slice_shared.elements[4].low_word().aval
            == 0xe2);
    assert(
        compiled.vcd.find("#4") != std::string::npos
        && compiled.vcd.find("b0000z010")
            != std::string::npos);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled == 4);
#endif
    inspect_suspended(
        config, fsim::app::SimulationEngine::interpreter);
#if defined(FSIM_HAS_LLVM)
    inspect_suspended(
        config, fsim::app::SimulationEngine::compiled);
#endif
    inspect_ordering_suspended(
        config, fsim::app::SimulationEngine::interpreter);
#if defined(FSIM_HAS_LLVM)
    inspect_ordering_suspended(
        config, fsim::app::SimulationEngine::compiled);
#endif
    inspect_static_suspended(
        config, fsim::app::SimulationEngine::interpreter);
#if defined(FSIM_HAS_LLVM)
    inspect_static_suspended(
        config, fsim::app::SimulationEngine::compiled);
#endif
    inspect_static_port_aliases(
        config, fsim::app::SimulationEngine::interpreter);
#if defined(FSIM_HAS_LLVM)
    inspect_static_port_aliases(
        config, fsim::app::SimulationEngine::compiled);
#endif
    inspect_static_slice_port_aliases(
        config, fsim::app::SimulationEngine::interpreter);
#if defined(FSIM_HAS_LLVM)
    inspect_static_slice_port_aliases(
        config, fsim::app::SimulationEngine::compiled);
#endif
    inspect_dynamic_port_aliases(
        config, fsim::app::SimulationEngine::interpreter);
#if defined(FSIM_HAS_LLVM)
    inspect_dynamic_port_aliases(
        config, fsim::app::SimulationEngine::compiled);
#endif
  }
  return 0;
}
