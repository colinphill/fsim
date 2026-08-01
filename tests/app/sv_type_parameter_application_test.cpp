// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
  std::array<std::string, 3> values;
  std::vector<std::string> keys;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& child_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-type-parameters";
  config.project.top = "sv:work.type_parameter_top";
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
  sources.files = {child_source, top_source};
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
  assert(project->design.specializations().size() == 4);
  Capture capture;
  capture.keys = project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  constexpr std::array<std::string_view, 3> paths{
      "type_parameter_top.default_value",
      "type_parameter_top.selected_value",
      "type_parameter_top.unrelated_value"};
  std::array<fsim::runtime::simir::SignalId, paths.size()> signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }
  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  return capture;
}

void verify(
    const Capture& capture,
    const std::string_view selected) {
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  assert(capture.values[0] == "0011");
  assert(capture.values[1] == selected);
  assert(capture.values[2] == "1");
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-type-parameters-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto child_source = directory.path / "children.sv";
  const auto top_source = directory.path / "top.sv";
  {
    std::ofstream output(child_source, std::ios::binary);
    output << R"(
module typed_value #(
  parameter type T = logic [3:0],
  parameter T INIT = 4'h3
) (
  output T value
);
  initial value = INIT;
endmodule

module unrelated_value(output logic value);
  initial value = 1'b1;
endmodule
)";
    assert(output.good());
  }
  const auto write_top =
      [&](const std::string_view range,
          const std::string_view initializer) {
        std::ofstream output(
            top_source, std::ios::binary | std::ios::trunc);
        output << "module type_parameter_top;\n"
               << "  logic [3:0] default_value;\n"
               << "  logic " << range << " selected_value;\n"
               << "  logic unrelated_value;\n"
               << "  typed_value defaults(default_value);\n"
               << "  typed_value #(.T(logic " << range
               << "), .INIT(" << initializer
               << ")) selected(selected_value);\n"
               << "  unrelated_value stable(unrelated_value);\n"
               << "  initial begin #1; $finish; end\n"
               << "endmodule\n";
        assert(output.good());
      };
  write_top("[7:0]", "8'ha5");

  std::vector<std::string> baseline_o2_keys;
  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path, child_source, top_source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    verify(reference, "10100101");
    verify(cold, "10100101");
    verify(warm, "10100101");
    assert(reference.values == cold.values);
    assert(cold.values == warm.values);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
    if (optimization == fsim::project::Optimization::o2) {
      baseline_o2_keys = warm.keys;
    }
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 4);
    assert(cold.compiled_modules == 4);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 4);
    assert(cold.cache.stores == 4);
    assert(warm.cache.hits == 4);
    assert(warm.cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(cold.compiled_modules == 0);
#endif
  }

  write_top("[5:0]", "6'h25");
  const auto changed = run_once(
      make_config(
          directory.path,
          child_source,
          top_source,
          fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled);
  verify(changed, "100101");
  assert(baseline_o2_keys.size() == 4);
  assert(changed.keys.size() == 4);
  assert(changed.keys[0] != baseline_o2_keys[0]);
  assert(changed.keys[1] == baseline_o2_keys[1]);
  assert(changed.keys[2] != baseline_o2_keys[2]);
  assert(changed.keys[3] == baseline_o2_keys[3]);
#if defined(FSIM_HAS_LLVM)
  assert(changed.cache.hits == 2);
  assert(changed.cache.misses == 2);
  assert(changed.cache.stores == 2);
#endif
  return 0;
}
