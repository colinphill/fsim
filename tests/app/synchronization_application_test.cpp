// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>

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
  std::string value;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "synchronization";
  config.project.top = "sv:work.synchronization";
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
  fsim::diagnostic::Engine artifact_diagnostics;
  const auto encoded = fsim::app::serialize_runtime_state(
      project->design, artifact_diagnostics);
  assert(encoded && !artifact_diagnostics.has_error());
  auto restored = fsim::app::deserialize_runtime_state(
      *encoded, "synchronization-runtime", artifact_diagnostics);
  assert(restored && !artifact_diagnostics.has_error());
  assert(fsim::app::serialize_runtime_state(
      *restored, artifact_diagnostics) == encoded);
  project->design = std::move(*restored);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto result =
      simulation.find_signal("synchronization.result");
  assert(result);

  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  capture.result = simulation.run();
  capture.value = simulation.read_signal(*result).to_msb_string();
  return capture;
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
  for (const auto* capture : {&reference, &cold, &warm}) {
    if (capture->result.status != fsim::runtime::RunStatus::stopped
        || capture->result.time != 2
        || capture->value != "1111111111") {
      std::cerr << "synchronization mismatch: status="
                << static_cast<int>(capture->result.status)
                << " time=" << capture->result.time
                << " value=" << capture->value << '\n';
    }
    assert(
        capture->result.status == fsim::runtime::RunStatus::stopped
        && capture->result.time == 2
        && capture->value == "1111111111");
  }
  assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 1);
  assert(cold.cache.hits == 0 && cold.cache.misses == 1);
  assert(warm.cache.hits == 1 && warm.cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
#endif
}

} // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-synchronization-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "synchronization.sv";
  {
    std::ofstream output(source);
    output << R"(
module synchronization;
  logic [9:0] result;
  initial begin : root
    mailbox #(byte) mb = new(1);
    semaphore sem = new(0);
    byte value;
    int ok;
    result = 0;
    value = 0;
    ok = 0;
    ok = mb.try_put(8'h11);
    result[0] = (ok == 1);
    ok = mb.try_put(8'h22);
    result[1] = (ok == 0);
    ok = mb.num();
    result[2] = (ok == 1);
    ok = mb.try_peek(value);
    result[3] = (ok == 1);
    result[4] = (value == 8'h11);
    mb.get(value);
    result[5] = (value == 8'h11);
    fork
      begin
        mb.get(value);
        result[6] = (value == 8'h22);
      end
      begin
        #1;
        mb.put(8'h22);
      end
    join
    fork
      begin
        sem.get(2);
        result[8] = 1;
      end
      begin
        #1;
        ok = sem.try_get();
        result[7] = (ok == 0);
        sem.put(2);
      end
    join
    ok = sem.try_get();
    result[9] = (ok == 0);
    $finish;
  end
endmodule
)";
  }
  test_optimization(
      directory.path, source, fsim::project::Optimization::o0);
  test_optimization(
      directory.path, source, fsim::project::Optimization::o2);
  std::cout << "synchronization application tests passed\n";
  return 0;
}
