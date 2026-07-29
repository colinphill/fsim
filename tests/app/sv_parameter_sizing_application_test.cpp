// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  std::array<std::string, 12> values;
  std::vector<std::string> specialization_keys;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-parameter-sizing";
  config.project.top = "sv:work.sized_parameter_top";
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
  assert(project->design.specializations().size() == 3);

  Capture capture;
  capture.specialization_keys =
      project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 12> paths{
      "sized_parameter_top.default_signed_byte",
      "sized_parameter_top.default_unsigned_byte",
      "sized_parameter_top.default_short",
      "sized_parameter_top.default_signed_vector",
      "sized_parameter_top.default_unsigned_vector",
      "sized_parameter_top.default_unsigned_int",
      "sized_parameter_top.override_signed_byte",
      "sized_parameter_top.override_unsigned_byte",
      "sized_parameter_top.override_short",
      "sized_parameter_top.override_signed_vector",
      "sized_parameter_top.override_unsigned_vector",
      "sized_parameter_top.override_unsigned_int"};
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

void verify_capture(const Capture& capture) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  assert((
      capture.values
      == std::array<std::string, 12>{
          "11111111111111111111111111111111",
          "00000000000000000000000011111111",
          "11111111111111111000000000000000",
          "11111111111111111111111110000000",
          "00000000000000000000000011111111",
          "11111111111111111111111111111111",
          "11111111111111111111111110000000",
          "00000000000000000000000011111110",
          "11111111111111111111111111111111",
          "11111111111111111111111111111111",
          "00000000000000000000000011111110",
          "11111111111111111111111111111110"}));
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-parameter-sizing-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "parameter_sizing.sv";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
module sized_parameter_child #(
  parameter byte SIGNED_BYTE = 8'hff,
  parameter byte unsigned UNSIGNED_BYTE = -1,
  parameter shortint SHORT_VALUE = 16'h8000,
  parameter logic signed [7:0] SIGNED_VECTOR = 8'h80,
  parameter logic [7:0] UNSIGNED_VECTOR = -1,
  parameter int unsigned UNSIGNED_INT = -1
) (
  output logic [31:0] signed_byte_value,
  output logic [31:0] unsigned_byte_value,
  output logic [31:0] short_value,
  output logic [31:0] signed_vector_value,
  output logic [31:0] unsigned_vector_value,
  output logic [31:0] unsigned_int_value
);
  initial begin
    signed_byte_value = SIGNED_BYTE;
    unsigned_byte_value = UNSIGNED_BYTE;
    short_value = SHORT_VALUE;
    signed_vector_value = SIGNED_VECTOR;
    unsigned_vector_value = UNSIGNED_VECTOR;
    unsigned_int_value = UNSIGNED_INT;
  end
endmodule

module sized_parameter_top;
  logic [31:0] default_signed_byte;
  logic [31:0] default_unsigned_byte;
  logic [31:0] default_short;
  logic [31:0] default_signed_vector;
  logic [31:0] default_unsigned_vector;
  logic [31:0] default_unsigned_int;
  logic [31:0] override_signed_byte;
  logic [31:0] override_unsigned_byte;
  logic [31:0] override_short;
  logic [31:0] override_signed_vector;
  logic [31:0] override_unsigned_vector;
  logic [31:0] override_unsigned_int;

  sized_parameter_child defaults(
    default_signed_byte,
    default_unsigned_byte,
    default_short,
    default_signed_vector,
    default_unsigned_vector,
    default_unsigned_int
  );
  sized_parameter_child #(
    .SIGNED_BYTE(8'h80),
    .UNSIGNED_BYTE(-2),
    .SHORT_VALUE(16'hffff),
    .SIGNED_VECTOR(8'hff),
    .UNSIGNED_VECTOR(-2),
    .UNSIGNED_INT(-2)
  ) overrides(
    override_signed_byte,
    override_unsigned_byte,
    override_short,
    override_signed_vector,
    override_unsigned_vector,
    override_unsigned_int
  );

  initial begin
    #1;
    $finish;
  end
endmodule
)";
    assert(output.good());
  }

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
    verify_capture(reference);
    verify_capture(cold);
    verify_capture(warm);
    assert(reference.values == cold.values);
    assert(reference.values == warm.values);
    assert(reference.specialization_keys == cold.specialization_keys);
    assert(cold.specialization_keys == warm.specialization_keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 3);
    assert(cold.compiled_modules == 3);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 3);
    assert(cold.cache.stores == 3);
    assert(warm.cache.hits == 3);
    assert(warm.cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(cold.compiled_modules == 0);
#endif
  }
}
