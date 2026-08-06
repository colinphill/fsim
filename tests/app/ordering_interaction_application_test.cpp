// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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
  fsim::runtime::RunResult result;
  std::uint64_t aval{};
  std::uint64_t bval{};
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
  std::size_t callbacks{};
  std::vector<std::uint64_t> shared;
  std::string debugger;
  std::string trace;
  std::size_t mapped_libraries{};
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "ordering-interactions";
  config.project.tops = {
      {"sv:work.ordering_interactions", "ordering_interactions"},
      {"sv:work.ordering_passive", "ordering_passive"}};
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
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture execute(
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
  assert((project->design.roots()
      == std::vector<std::string>{
          "ordering_interactions", "ordering_passive"}));
  fsim::diagnostic::Engine artifact_diagnostics;
  const auto encoded = fsim::app::serialize_runtime_state(
      project->design, artifact_diagnostics);
  assert(encoded && !artifact_diagnostics.has_error());
  auto restored = fsim::app::deserialize_runtime_state(
      *encoded, "ordering-runtime", artifact_diagnostics);
  assert(restored && !artifact_diagnostics.has_error());
  assert(fsim::app::serialize_runtime_state(
      *restored, artifact_diagnostics) == encoded);
  assert(std::ranges::any_of(
      restored->processes(), [](const auto& process) {
        return std::ranges::any_of(
            process.operations, [](const auto& operation) {
              const auto* ordering =
                  fsim::runtime::simir::operation_get_if<
                      fsim::runtime::simir::OrderContainer>(&operation);
              return ordering
                  && ordering->operation
                      == fsim::runtime::simir::
                          ContainerOrderingOperator::shuffle;
            });
      }));
  auto malformed = *restored;
  bool corrupted = false;
  auto& malformed_processes =
      const_cast<std::vector<fsim::runtime::simir::Process>&>(
          malformed.processes());
  for (auto& process : malformed_processes) {
    for (auto& operation : process.operations) {
      auto* ordering = fsim::runtime::simir::operation_get_if<
          fsim::runtime::simir::OrderContainer>(&operation);
      if (ordering
          && ordering->operation
              == fsim::runtime::simir::
                  ContainerOrderingOperator::shuffle) {
        ordering->operation = static_cast<
            fsim::runtime::simir::ContainerOrderingOperator>(255);
        corrupted = true;
        break;
      }
    }
    if (corrupted) break;
  }
  assert(corrupted);
  fsim::diagnostic::Engine malformed_diagnostics;
  assert(!fsim::app::serialize_runtime_state(
      malformed, malformed_diagnostics));
  assert(std::ranges::any_of(
      malformed_diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ART-0013"
            && diagnostic.message.find("invalid scalar enumeration")
                != std::string::npos;
      }));
  project->design = std::move(*restored);
  Capture capture;
  {
    fsim::app::Simulation simulation{
        std::move(*project), config.run.max_deltas, engine};
    const auto result =
        simulation.find_signal("ordering_interactions.result");
    assert(result);
    const auto shared =
        simulation.design().find_container("ordering_interactions.shared");
    assert(shared);
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    fsim::app::DebuggerControl debugger{
        simulation, debugger_output, debugger_error};
    debugger.execute({"show", "ordering_interactions.shared"});
    std::ostringstream trace_output;
    fsim::runtime::VcdWriter trace{
        trace_output, std::string{simulation.time_resolution()}, 128};
    const auto trace_result = trace.declare_signal(
        "ordering_interactions.result", 48);
    trace.begin(simulation.now());
    trace.change(trace_result, simulation.read_signal(*result));
    std::size_t callbacks = 0;
    const auto callback = simulation.add_signal_change_hook(
        [&](const auto signal, const auto& value,
            const auto time, const auto) {
          if (signal == *result) {
            ++callbacks;
            trace.set_time(time);
            trace.change(trace_result, value);
          }
        });

    capture.compiled_processes = simulation.compiled_process_count();
    capture.cache = simulation.native_cache_statistics();
    capture.mapped_libraries = simulation.mapped_libraries().size();
    capture.result = simulation.run();
    simulation.remove_signal_change_hook(callback);
    debugger.execute({"show", "ordering_interactions.shared"});
    assert(debugger_error.str().empty());
    capture.callbacks = callbacks;
    capture.debugger = debugger_output.str();
    for (const auto& element :
         simulation.read_container_object(*shared).elements) {
      capture.shared.push_back(element.low_word().aval);
    }
    const auto word = simulation.read_signal(*result).low_word();
    capture.aval = word.aval;
    capture.bval = word.bval;
    trace.flush();
    capture.trace = trace_output.str();
  }
  return capture;
}

void validate_capture(const Capture& capture) {
  assert(
      capture.result.status == fsim::runtime::RunStatus::stopped
      && capture.result.time == 2
      && capture.bval == 0
      && (capture.aval & UINT64_C(0xff)) == UINT64_C(0xff)
      && ((capture.aval >> 8U) & UINT64_C(0xff)) == UINT64_C(0x12));
  std::array<std::uint8_t, 4> elements{};
  for (std::size_t index = 0; index < elements.size(); ++index) {
    elements[index] = static_cast<std::uint8_t>(
        (capture.aval >> (16U + index * 8U)) & UINT64_C(0xff));
  }
  std::ranges::sort(elements);
  assert((elements == std::array<std::uint8_t, 4>{1, 2, 3, 4}));
  auto shared = capture.shared;
  std::ranges::sort(shared);
  assert((shared == std::vector<std::uint64_t>{1, 2, 3, 4}));
  assert(capture.callbacks != 0);
  assert(capture.debugger.find("ordering_interactions.shared")
      != std::string::npos);
  assert(capture.trace.find("$var") != std::string::npos);
}

void test_optimization(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  const auto config = make_config(directory, source, optimization);
  const auto reference =
      execute(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      execute(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      execute(config, fsim::app::SimulationEngine::compiled);
  validate_capture(reference);
  validate_capture(cold);
  validate_capture(warm);
  assert(reference.aval == cold.aval && cold.aval == warm.aval);
  assert(reference.shared == cold.shared && cold.shared == warm.shared);
  assert(reference.callbacks == cold.callbacks
      && cold.callbacks == warm.callbacks);
  assert(reference.debugger == cold.debugger
      && cold.debugger == warm.debugger);
  assert(reference.trace == cold.trace && cold.trace == warm.trace);
  assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 3);
  assert(cold.cache.hits == 0 && cold.cache.misses == 1);
  assert(warm.cache.hits == 1 && warm.cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
#endif
}

void test_relocated_library(
    const std::filesystem::path& directory,
    const std::filesystem::path& source) {
  auto export_config = make_config(
      directory, source, fsim::project::Optimization::o2);
  export_config.project.name = "ordering-relocation-export";
  const auto original = directory / "ordering.fsimlib";
  fsim::diagnostic::Engine export_diagnostics;
  assert(fsim::app::export_library(
      export_config, "work", original, export_diagnostics));
  assert(!export_diagnostics.has_error());
  const auto relocated = directory / "relocated-ordering.fsimlib";
  std::filesystem::rename(original, relocated);

  auto mapped = make_config(
      directory, source, fsim::project::Optimization::o2);
  mapped.project.name = "ordering-relocation-consumer";
  mapped.source_sets.clear();
  mapped.library_mappings.push_back({"work", relocated});
  mapped.build.cache_path = directory / "relocated-cache";
  const auto reference = execute(
      mapped, fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      mapped, fsim::app::SimulationEngine::compiled);
  validate_capture(reference);
  validate_capture(compiled);
  assert(reference.mapped_libraries == 1);
  assert(compiled.mapped_libraries == 1);
  assert(reference.aval == compiled.aval);
  assert(reference.shared == compiled.shared);
  assert(reference.debugger == compiled.debugger);
  assert(reference.trace == compiled.trace);
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-ordering-interactions-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "ordering_interactions.sv";
  {
    std::ofstream output(source);
    output << R"(
module ordering_interactions;
  event coordinate;
  int shared[$];
  int wake_order[$];
  int observations[$];
  logic [47:0] result;

  initial begin
    @(coordinate);
    observations.push_back(shared.size());
    wake_order.push_back(1);
    shared.shuffle();
  end

  initial begin
    @(coordinate);
    observations.push_back(wake_order[0]);
    wake_order.push_back(2);
    shared.reverse();
  end

  initial begin
    result = 0;
    shared = '{1, 2, 3, 4};
    wake_order = '{};
    observations = '{};
    #1;
    -> coordinate;
    #1;
    result[0] = (observations.size() == 2);
    result[1] = (observations[0] == 4);
    result[2] = (observations[1] == 1);
    result[3] = (wake_order.size() == 2);
    result[4] = (shared.size() == 4);
    result[5] = (shared.sum() == 10);
    result[6] =
        wake_order[0] == 1 && wake_order[1] == 2;
    result[7] = result[6:0] == 7'b1111111;
    result[15:8] = (wake_order[0] << 4) | wake_order[1];
    result[23:16] = shared[0];
    result[31:24] = shared[1];
    result[39:32] = shared[2];
    result[47:40] = shared[3];
    $finish;
  end
endmodule
module ordering_passive;
endmodule
)";
  }
  test_optimization(
      directory.path, source, fsim::project::Optimization::o0);
  test_optimization(
      directory.path, source, fsim::project::Optimization::o2);
  test_relocated_library(directory.path, source);
  std::cout << "ordering interaction application tests passed\n";
  return 0;
}
