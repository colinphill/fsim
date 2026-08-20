// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
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
  fsim::runtime::RunResult run;
  std::array<std::string, 10> values;
  std::vector<std::string> keys;
  std::vector<std::string> locals;
  std::vector<std::string> local_values;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  std::string vcd;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-callable-closure";
  config.project.top = "sv:work.callable_closure";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path = directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files = {source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture execute(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  Capture capture;
  capture.keys = project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  simulation.set_execution_point_hook(
      [&capture](
          fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
      });
  constexpr std::array<std::string_view, 10> paths{
      "callable_closure.static_first",
      "callable_closure.static_second",
      "callable_closure.copied",
      "callable_closure.returned",
      "callable_closure.ref_result",
      "callable_closure.task_first",
      "callable_closure.task_second",
      "callable_closure.task_ref_result",
      "callable_closure.generated_function_result",
      "callable_closure.generated_task_result"};
  std::array<fsim::runtime::simir::SignalId, paths.size()> signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }
  std::ostringstream vcd_text;
  fsim::runtime::VcdWriter vcd{vcd_text, "1ns", 32};
  const auto static_trace = vcd.declare_signal(paths[1], 8);
  const auto ref_trace = vcd.declare_signal(paths[7], 8);
  const auto generated_trace = vcd.declare_signal(paths[9], 8);
  vcd.begin(simulation.now());
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        vcd.set_time(time);
        if (signal == signals[1]) {
          vcd.change(static_trace, value);
        } else if (signal == signals[7]) {
          vcd.change(ref_trace, value);
        } else if (signal == signals[9]) {
          vcd.change(generated_trace, value);
        }
      });
  capture.run = simulation.run();
  vcd.flush();
  capture.vcd = vcd_text.str();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  for (std::size_t id = 0;
       id < simulation.design_ir().processes().size(); ++id) {
    const auto& process = simulation.process_program(
        static_cast<fsim::runtime::simir::ProcessId>(id));
    for (std::size_t index = 0;
         index < process.debug_locals.size(); ++index) {
      const auto& local = process.debug_locals[index];
      capture.locals.push_back(local.name);
      try {
        capture.local_values.push_back(
            local.name + "="
            + simulation.read_process_local(process.id, index)
                  .to_msb_string());
      } catch (const std::logic_error&) {
      }
    }
  }
  return capture;
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

void verify(
    const Capture& capture,
    const std::array<std::string, 10>& expected) {
  assert(capture.run.status == fsim::runtime::RunStatus::completed);
  assert(capture.values == expected);
  assert(std::ranges::find(capture.locals, "retained.state")
      != capture.locals.end());
  assert(std::ranges::any_of(
      capture.locals,
      [](const std::string& name) {
        return name.find(".target") != std::string::npos;
      }));
  assert(std::ranges::find(capture.locals, "retained_task.state")
      != capture.locals.end());
  assert(std::ranges::find(
             capture.local_values,
             "retained.state=00000011")
      != capture.local_values.end());
  assert(std::ranges::find(
             capture.local_values,
             "retained_task.state=00000010")
      != capture.local_values.end());
  const auto call_points = std::ranges::count_if(
      capture.points,
      [](const auto& point) {
        return point.kind
            == fsim::runtime::simir::ExecutionPointKind::call;
      });
  // Optimized native callables are deliberately inlined and therefore do
  // not expose the interpreter's internal Call boundaries.
  assert(capture.compiled_processes == 0
      ? call_points >= 9
      : call_points == 0);
  assert(capture.vcd.find(expected[1]) != std::string::npos);
  assert(capture.vcd.find(expected[7]) != std::string::npos);
  assert(capture.vcd.find(expected[9]) != std::string::npos);
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-callable-closure-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "callables.sv";
  const auto write_source = [&](const unsigned generated_value) {
    std::ofstream output(source, std::ios::binary | std::ios::trunc);
    output << R"(
module callable_closure(
    output logic [7:0] static_first, static_second, copied, returned,
    output logic [7:0] ref_result, task_first, task_second, task_ref_result,
    output logic [7:0] generated_function_result, generated_task_result);
  function logic [7:0] retained(input logic [7:0] amount = 8'd1);
    logic [7:0] state = 8'd0;
    state = state + amount;
    return state;
  endfunction
  function automatic logic [7:0] exchange(
      output logic [7:0] copied_value,
      input logic [7:0] value = 8'd40);
    copied_value = value;
    return value + 2;
  endfunction
  function automatic logic [7:0] bump(
      ref logic [7:0] target,
      input logic [7:0] amount = 8'd2);
    target = target + amount;
    return target;
  endfunction
  task retained_task;
    output logic [7:0] value;
    logic [7:0] state = 8'd0;
    state = state + 1;
    value = state;
  endtask
  task automatic bump_task(
      ref logic [7:0] target,
      input logic [7:0] amount = 8'd3);
    target = target + amount;
  endtask
  if (1) begin : selected
    function automatic logic [7:0] generated_function;
      return 8'd43;
    endfunction
    task automatic generated_task(output logic [7:0] value);
      value = 8'd)"
           << generated_value << R"(;
    endtask
    initial begin
      generated_function_result = generated_function();
      generated_task(.value(generated_task_result));
    end
  end
  initial begin
    logic [7:0] local_value;
    static_first = retained();
    static_second = retained(.amount(8'd2));
    returned = exchange(.copied_value(copied));
    local_value = 8'd10;
    ref_result = bump(.target(local_value));
    retained_task(.value(task_first));
    retained_task(.value(task_second));
    bump_task(.target(local_value));
    task_ref_result = local_value;
  end
endmodule
)";
    assert(output.good());
  };
  const std::array<std::string, 10> expected{
      "00000001", "00000011", "00101000", "00101010", "00001100",
      "00000001", "00000010", "00001111", "00101011", "00101100"};
  write_source(44);
  std::vector<std::string> baseline_keys;
  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config = config_for(directory.path, source, optimization);
    const auto reference = execute(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = execute(config, fsim::app::SimulationEngine::compiled);
    const auto warm = execute(config, fsim::app::SimulationEngine::compiled);
    verify(reference, expected);
    verify(cold, expected);
    verify(warm, expected);
    assert(reference.keys == cold.keys && cold.keys == warm.keys);
    assert(same_points(cold.points, warm.points));
    if (optimization == fsim::project::Optimization::o2) {
      baseline_keys = warm.keys;
    }
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 2);
    assert(cold.cache.misses >= 1 && cold.cache.stores >= 1);
    assert(warm.cache.hits >= 1);
#else
    assert(cold.compiled_processes == 0);
#endif
  }
  write_source(45);
  auto changed_expected = expected;
  changed_expected.back() = "00101101";
  const auto changed = execute(
      config_for(
          directory.path,
          source,
          fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled);
  verify(changed, changed_expected);
  assert(changed.keys.size() == baseline_keys.size());
  assert(changed.keys != baseline_keys);
#if defined(FSIM_HAS_LLVM)
  assert(changed.cache.misses >= 1);
  assert(changed.cache.stores >= 1);
#endif
  return 0;
}
