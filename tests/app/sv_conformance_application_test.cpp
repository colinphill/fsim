// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
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
  std::array<std::uint64_t, 4> values{};
  std::string output_file;
  std::string memory_file;
  std::vector<std::string> cache_keys;
  fsim::app::NativeCacheStatistics cache;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

void write_text(
    const std::filesystem::path& path,
    const std::string_view contents) {
  std::ofstream output(
      path, std::ios::binary | std::ios::trunc);
  output << contents;
  assert(output.good());
}

[[nodiscard]] std::string read_text(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-public-conformance";
  config.project.top = "sv:work.conformance_runtime_top";
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
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);

  const std::array<std::string_view, 4> paths{
      "conformance_runtime_top.core_result",
      "conformance_runtime_top.timed_result",
      "conformance_runtime_top.assertion_seen",
      "conformance_runtime_top.data_result"};
  std::array<fsim::runtime::simir::SignalId, 4> signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = project->design.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }

  Capture capture;
  capture.cache_keys = project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    const auto word = simulation.read_signal(signals[index]).low_word();
    assert(word.bval == 0);
    capture.values[index] = word.aval;
  }
  capture.output_file =
      read_text(config.base_directory / "conformance-output.txt");
  capture.memory_file =
      read_text(config.base_directory / "conformance-memory.hex");
  return capture;
}

void compare_captures(
    const Capture& reference,
    const Capture& candidate) {
  assert(reference.result.status == candidate.result.status);
  assert(reference.result.time == candidate.result.time);
  assert(reference.result.delta == candidate.result.delta);
  assert(reference.values == candidate.values);
  assert(reference.output_file == candidate.output_file);
  assert(reference.memory_file == candidate.memory_file);
  assert(reference.cache_keys == candidate.cache_keys);
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-public-conformance-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "conformance.sv";

  write_text(directory.path / "conformance-input.txt", "13 fsim\n");
  write_text(
      directory.path / "conformance-memory-input.hex",
      "0a\n0b\n0c\n0d\n");
  write_text(
      source,
      R"(module conformance_core(
  output logic [31:0] core_result,
  output logic [31:0] timed_result,
  output logic assertion_seen
);
  event completed;
  logic side_effect;

  // FSIM-CONFORMANCE CF-SV-EXPR-001 source=SRC-SV-TESTS expectation=execute
  function automatic logic touch();
    side_effect = 1'b1;
    return 1'b1;
  endfunction

  // FSIM-CONFORMANCE CF-SV-CALL-001 source=SRC-SLANG expectation=execute
  function automatic int transform(input int value);
    return value * 3 + 1;
  endfunction

  task automatic delayed_add(input int seed, output int value);
    int local_value;
    local_value = seed;
    #1;
    value = local_value + 2;
  endtask

  // FSIM-CONFORMANCE CF-SV-PROC-001 source=SRC-SURELOG expectation=execute
  // FSIM-CONFORMANCE CF-SV-TIME-001 source=SRC-SV-TESTS expectation=execute
  initial begin : producer
    int total;
    total = 0;
    side_effect = 1'b0;
    core_result = 0;
    assertion_seen = 1'b0;
    for (int index = 0; index < 4; index++) begin
      if (index == 1)
        continue;
      total = total + index;
    end
    casez (4'b10z1)
      4'b1?01: total = total + 4;
      default: total = 99;
    endcase
    if (1'b0 && touch())
      total = 99;
    delayed_add(transform(total), core_result);
    -> completed;
    // FSIM-CONFORMANCE CF-SV-ASSERT-001 source=SRC-SLANG expectation=execute
    assert (core_result == 30 && side_effect == 1'b0)
      assertion_seen = 1'b1;
    else
      $fatal(1, "public conformance core mismatch");
  end

  initial begin : observer
    @completed;
    #2 timed_result <= core_result + 4;
  end
endmodule

module conformance_data(output logic [31:0] data_result);
  integer reader;
  integer writer;
  integer scan_count;
  integer scanned;
  string label;
  string formatted;
  logic [7:0] memory[0:3];
  int dynamic_values[];
  byte queue_values[$:3];
  byte associative_values[int];

  initial begin : worker
    // FSIM-CONFORMANCE CF-SV-STRING-001 source=SRC-SV-TESTS expectation=execute
    // FSIM-CONFORMANCE CF-SV-FILE-001 source=SRC-SURELOG expectation=execute
    reader = $fopen("conformance-input.txt", "r");
    scan_count = $fscanf(reader, "%d %s", scanned, label);
    $fclose(reader);
    formatted = $sformatf("%s-%0d", label.toupper(), 7);

    // FSIM-CONFORMANCE CF-SV-MEMORY-001 source=SRC-SV-TESTS expectation=execute
    $readmemh("conformance-memory-input.hex", memory);
    $writememh("conformance-memory.hex", memory);

    // FSIM-CONFORMANCE CF-SV-DYNAMIC-001 source=SRC-SV-TESTS expectation=execute
    dynamic_values = '{4, 5, 6};
    // FSIM-CONFORMANCE CF-SV-QUEUE-001 source=SRC-SV-TESTS expectation=execute
    queue_values = '{3, 1};
    queue_values.push_back(2);
    queue_values.sort();
    // FSIM-CONFORMANCE CF-SV-ASSOC-001 source=SRC-SV-TESTS expectation=execute
    associative_values[4] = 9;
    associative_values[-1] = 5;

    data_result = scanned + memory[0]
        + dynamic_values.sum() + queue_values.sum()
        + associative_values.sum() + formatted.len();
    assert (scan_count == 2);
    assert (queue_values[0] == 1 && queue_values[2] == 3);
    formatted = $sformatf("result=%0d text=%s", data_result, formatted);
    writer = $fopen("conformance-output.txt", "w");
    $fdisplay(writer, "%s", formatted);
    $fclose(writer);
  end
endmodule

module conformance_runtime_top;
  logic [31:0] core_result;
  logic [31:0] timed_result;
  logic assertion_seen;
  logic [31:0] data_result;
  conformance_core core(core_result, timed_result, assertion_seen);
  conformance_data data(data_result);
endmodule
)");

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    const auto config = make_config(directory.path, source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);

    compare_captures(reference, cold);
    compare_captures(reference, warm);
    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.result.time == 3);
    assert((reference.values == std::array<std::uint64_t, 4>{30, 34, 1, 64}));
    assert(reference.output_file == "result=64 text=FSIM-7\n");
    assert(reference.memory_file == "0a\n0b\n0c\n0d\n");
    assert(reference.compiled_processes == 0);
    assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0);
    assert(cold.compiled_modules > 0);
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores > 0);
    assert(warm.cache.hits > 0);
    assert(warm.cache.misses == 0);
#endif
  }
  return 0;
}
