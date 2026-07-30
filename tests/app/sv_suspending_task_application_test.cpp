// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
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
  struct Change {
    fsim::runtime::SimulationTick time{};
    std::uint64_t delta{};
    std::string value;

    bool operator==(const Change&) const = default;
  };

  fsim::runtime::RunResult result;
  std::array<std::string, 3> values;
  std::vector<std::string> keys;
  std::vector<std::string> locals;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  std::vector<Change> result_changes;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-suspending-tasks";
  config.project.top = "sv:work.task_suspend_top";
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

  Capture capture;
  capture.keys = project->specialization_cache_keys;
  assert(project->design.processes().size() == 2);
  for (const auto& process : project->design.processes()) {
    for (const auto& local : process.debug_locals) {
      capture.locals.push_back(local.name);
    }
  }

  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  simulation.set_execution_point_hook(
      [&capture](
          fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
      });

  const auto result =
      simulation.find_signal("task_suspend_top.result");
  const auto total =
      simulation.find_signal("task_suspend_top.total");
  const auto early =
      simulation.find_signal("task_suspend_top.early");
  assert(result && total && early);
  simulation.set_signal_change_hook(
      [&capture, result](
          const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        if (signal == *result) {
          capture.result_changes.push_back(
              Capture::Change{
                  time, delta, value.to_msb_string()});
        }
      });
  capture.result = simulation.run();
  capture.values = {
      simulation.read_signal(*result).to_msb_string(),
      simulation.read_signal(*total).to_msb_string(),
      simulation.read_signal(*early).to_msb_string()};
  return capture;
}

void verify(
    const Capture& capture,
    const std::array<std::string, 3>& expected) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 6);
  assert(capture.values == expected);
  assert(std::ranges::find(
             capture.locals, "outer.temporary")
         != capture.locals.end());
  assert(std::ranges::find(
             capture.locals, "delayed_transform.value")
         != capture.locals.end());
  assert(std::ranges::count_if(
             capture.points,
             [](const auto& point) {
               return point.kind
                   == fsim::runtime::simir::
                       ExecutionPointKind::call;
             })
         == 2);
  assert(std::ranges::count_if(
             capture.points,
             [](const auto& point) {
               return point.kind
                   == fsim::runtime::simir::
                       ExecutionPointKind::wait;
             })
         >= 6);
  assert(std::ranges::count_if(
             capture.points,
             [](const auto& point) {
               return point.kind
                   == fsim::runtime::simir::
                       ExecutionPointKind::process_suspend;
             })
         >= 7);
  assert(capture.result_changes.size() == 2);
  assert(capture.result_changes.front().time == 1);
  assert(capture.result_changes.front().value == "00000000");
  assert(capture.result_changes.back().time == 6);
  assert(capture.result_changes.back().value == expected[0]);
}

bool same_points(
    const std::vector<
        fsim::runtime::simir::ExecutionPoint>& left,
    const std::vector<
        fsim::runtime::simir::ExecutionPoint>& right) {
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

void verify_suspended_locals(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};

  std::optional<fsim::runtime::simir::ProcessId> process_id;
  std::optional<std::size_t> package_value;
  std::optional<std::size_t> outer_value;
  const auto& processes = simulation.design().processes();
  for (const auto& process : processes) {
    for (std::size_t index = 0;
         index < process.debug_locals.size(); ++index) {
      const auto& name = process.debug_locals[index].name;
      if (name == "delayed_transform.value") {
        process_id = process.id;
        package_value = index;
      } else if (name == "outer.value") {
        outer_value = index;
      }
    }
  }
  assert(process_id && package_value && outer_value);

  std::size_t task_waits = 0;
  bool armed = false;
  bool requested = false;
  simulation.set_execution_point_hook(
      [&](fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::simir::ExecutionPoint& point) {
        if (point.process != *process_id) {
          return;
        }
        if (point.kind
            == fsim::runtime::simir::
                ExecutionPointKind::wait) {
          ++task_waits;
          armed = task_waits == 2;
        } else if (
            armed && !requested
            && point.kind
                == fsim::runtime::simir::
                    ExecutionPointKind::process_suspend) {
          requested = true;
          scheduler.request_stop();
        }
      });
  const auto suspended = simulation.run();
  assert(requested);
  assert(
      suspended.status
      == fsim::runtime::RunStatus::stopped);
  assert(suspended.time == 2);
  assert(
      simulation
          .read_process_local(*process_id, *package_value)
          .to_msb_string()
      == "00101000");
  assert(
      simulation
          .read_process_local(*process_id, *outer_value)
          .to_msb_string()
      == "00101000");

  simulation.clear_stop();
  const auto resumed = simulation.run();
  assert(
      resumed.status
      == fsim::runtime::RunStatus::completed);
  assert(resumed.time == 6);
  const auto result =
      simulation.find_signal("task_suspend_top.result");
  assert(result);
  assert(
      simulation.read_signal(*result).to_msb_string()
      == "00101010");
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-suspending-tasks-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "tasks.sv";

  const auto write_source =
      [&](const unsigned increment) {
        std::ofstream output(
            source, std::ios::binary | std::ios::trunc);
        output
            << "`timescale 1ns/1ns\n"
            << "package suspend_pkg;\n"
            << "  task automatic delayed_transform(\n"
            << "      input logic [7:0] value,\n"
            << "      output logic [7:0] transformed);\n"
            << "    #2;\n"
            << "    transformed = value + "
            << increment << ";\n"
            << "  endtask\n"
            << "endpackage\n"
            << R"(
module task_suspend_top;
  import suspend_pkg::*;
  event kick;
  logic ready;
  logic launch;
  logic [7:0] result;
  logic [7:0] total;
  logic [7:0] early;

  task automatic outer(
      input logic [7:0] value,
      output logic [7:0] transformed,
      inout logic [7:0] accumulator);
    logic [7:0] temporary;
    #1;
    ->> #3 kick;
    delayed_transform(value, temporary);
    @(kick);
    wait (ready);
    transformed = temporary;
    accumulator = accumulator + temporary;
    return;
    transformed = 0;
    accumulator = 0;
  endtask

  always @(posedge launch) begin
    result = 0;
    total = 1;
    outer(40, result, total);
  end

  initial begin
    launch = 0;
    ready = 0;
    early = 8'hff;
    #1 launch = 1;
    #4;
    early = result;
    #1 ready <= 1;
  end
endmodule
)";
        assert(output.good());
      };

  write_source(2);
  std::vector<std::string> baseline_keys;
  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config =
        make_config(directory.path, source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const std::array<std::string, 3> expected{
        "00101010", "00101011", "00000000"};
    verify(reference, expected);
    verify(cold, expected);
    verify(warm, expected);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
    assert(reference.result_changes == cold.result_changes);
    assert(cold.result_changes == warm.result_changes);
    assert(same_points(reference.points, cold.points));
    assert(same_points(cold.points, warm.points));
    if (optimization == fsim::project::Optimization::o2) {
      baseline_keys = warm.keys;
    }
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 2);
    assert(cold.cache.misses == 1);
    assert(cold.cache.stores == 1);
    assert(warm.cache.hits == 1);
#else
    assert(cold.compiled_processes == 0);
#endif
    verify_suspended_locals(
        config, fsim::app::SimulationEngine::interpreter);
    verify_suspended_locals(
        config, fsim::app::SimulationEngine::compiled);
  }

  write_source(3);
  const auto changed = run_once(
      make_config(
          directory.path,
          source,
          fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled);
  verify(
      changed,
      {"00101011", "00101100", "00000000"});
  assert(baseline_keys.size() == 1);
  assert(changed.keys.size() == 1);
  assert(changed.keys.front() != baseline_keys.front());
#if defined(FSIM_HAS_LLVM)
  assert(changed.cache.hits == 0);
  assert(changed.cache.misses == 1);
  assert(changed.cache.stores == 1);
#endif
}
