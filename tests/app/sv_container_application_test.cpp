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
  assert(objects.size() == 3);
  capture.values =
      simulation.read_container_object(objects[0].id);
  capture.pending =
      simulation.read_container_object(objects[1].id);
  capture.lookup =
      simulation.read_container_object(objects[2].id);
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
      && resumed.time == 2);
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
    std::ofstream output(source, std::ios::binary);
    output << R"(
module container_top;
  typedef logic signed [31:0] key_t;
  int values[];
  byte pending[$:2];
  byte lookup[key_t];
  function automatic int count(input byte source[$:2]);
    return source.size();
  endfunction
  function automatic int lookup_count(input byte source[key_t]);
    return source.size();
  endfunction
  function automatic int isolated_count(input byte source[key_t]);
    byte copy[key_t];
    copy = source;
    copy.delete(3);
    return copy.size();
  endfunction
  task automatic mutate(inout byte target[$:2]);
    target.push_back(4);
    #1;
    target.pop_front();
  endtask
  task automatic mutate_lookup(inout byte target[key_t]);
    target[-1] = 9;
    #1;
    target.delete(3);
  endtask
  initial begin
    key_t key;
    values = new[2];
    values[0] = 7;
    lookup[3] = 30;
    lookup[-1] = 10;
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
    pending.push_back(1);
    pending.push_back(2);
    mutate(pending);
    $display("%0d:%0d:%0d",
             values[0], pending[0], count(pending));
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
        && reference.result.time == 2
        && reference.output
            == (std::vector<std::string>{"7", ":2", ":2"}));
    assert(reference.output == compiled.output);
    assert(reference.values == compiled.values);
    assert(reference.pending == compiled.pending);
    assert(reference.lookup == compiled.lookup);
    assert(
        compiled.pending.elements.size() == 2
        && compiled.pending.elements[0].low_word().aval == 2
        && compiled.pending.elements[1].low_word().aval == 4);
    assert(
        compiled.lookup.keys.size() == 1
        && compiled.lookup.keys[0].low_word().aval
            == UINT64_C(0xffffffff)
        && compiled.lookup.elements[0].low_word().aval == 9);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled == 1);
#endif
    inspect_suspended(
        config, fsim::app::SimulationEngine::interpreter);
    inspect_suspended(
        config, fsim::app::SimulationEngine::compiled);
  }
}
