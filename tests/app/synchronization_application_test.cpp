// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
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
    const fsim::project::Optimization optimization,
    const std::string_view standard) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "synchronization";
  config.project.top = "sv:work.synchronization";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / ("cache-" + std::string{standard}
         + (optimization == fsim::project::Optimization::o0
                ? "-o0"
                : "-o2"));
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = std::string{standard};
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture execute(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::string_view result_name = "synchronization.result") {
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
  const auto result = simulation.find_signal(result_name);
  assert(result);

  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  capture.result = simulation.run();
  capture.value = simulation.read_signal(*result).to_msb_string();
  if (result_name == "clocking_skew.result" && capture.value != "111") {
    for (const auto& signal : simulation.design().signals()) {
      if (signal.name.find("clocking") != std::string::npos
          || signal.name.find(".cb") != std::string::npos
          || signal.name == "clocking_skew.observed"
          || signal.name == "clocking_skew.driven") {
        std::cerr << signal.name << '='
                  << simulation.read_signal(signal.id).to_msb_string()
                  << '\n';
      }
    }
  }
  return capture;
}

void test_clocking_skew_contract(
    const std::filesystem::path& directory) {
  const auto source = directory / "clocking-skew.sv";
  {
    std::ofstream output(source);
    output << R"(
module clocking_skew #(
  parameter int INPUT_SKEW = 1,
  parameter int OUTPUT_SKEW = 2
);
  timeunit 1ns / 1ns;
  logic clk;
  logic observed = 1;
  logic driven;
  logic sampled_ok;
  logic early_ok;
  logic late_ok;
  logic [2:0] result;
  clocking cb @(posedge clk);
    default input #(INPUT_SKEW + 0) output #(OUTPUT_SKEW + 0);
    input sampled = observed;
    output driven;
  endclocking
  default clocking cb;
  initial begin
    clk = 0;
    sampled_ok = 0;
    early_ok = 0;
    late_ok = 0;
    result = 0;
    fork
      begin
        #2 clk = 1;
        #1 clk = 0;
      end
      begin
        ##1;
        sampled_ok = (cb.sampled == 1);
        cb.driven = 1;
        #1 early_ok = (driven !== 1);
        #2 late_ok = (driven == 1);
        result = {late_ok, early_ok, sampled_ok};
        $finish;
      end
    join
  end
endmodule
)";
  }
  auto config = make_config(
      directory, source, fsim::project::Optimization::o2, "2023");
  config.project.name = "clocking-skew";
  config.project.top = "sv:work.clocking_skew";
  config.build.cache_path = directory / "cache-clocking-skew";
  for (const auto engine : {
           fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::compiled}) {
    const auto capture = execute(
        config, engine, "clocking_skew.result");
    if (capture.result.status != fsim::runtime::RunStatus::stopped
        || capture.result.time != 5 || capture.value != "111") {
      std::cerr << "clocking skew mismatch: status="
                << static_cast<int>(capture.result.status)
                << " time=" << capture.result.time
                << " value=" << capture.value << '\n';
    }
    assert(
        capture.result.status == fsim::runtime::RunStatus::stopped
        && capture.result.time == 5
        && capture.value == "111");
  }

  const auto invalid_source = directory / "clocking-skew-invalid.sv";
  {
    std::ofstream output(invalid_source);
    output << R"(
module clocking_skew_invalid;
  logic clk;
  logic dynamic_skew;
  logic sampled;
  clocking cb @(posedge clk);
    input #(dynamic_skew) sampled;
  endclocking
endmodule
)";
  }
  auto invalid_config = make_config(
      directory,
      invalid_source,
      fsim::project::Optimization::o0,
      "2023");
  invalid_config.project.name = "clocking-skew-invalid";
  invalid_config.project.top = "sv:work.clocking_skew_invalid";
  invalid_config.build.cache_path = directory / "cache-clocking-skew-invalid";
  fsim::diagnostic::Engine diagnostics;
  const auto invalid = fsim::app::build_project(
      invalid_config, diagnostics);
  assert(!invalid);
  assert(std::ranges::any_of(
      diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-CLOCK-008";
      }));

  auto retained_config = make_config(
      directory,
      invalid_source,
      fsim::project::Optimization::o0,
      "2017");
  retained_config.project.name = "clocking-skew-retained";
  retained_config.project.top = "sv:work.clocking_skew_invalid";
  retained_config.build.cache_path = directory / "cache-clocking-skew-retained";
  fsim::diagnostic::Engine retained_diagnostics;
  const auto retained = fsim::app::build_project(
      retained_config, retained_diagnostics);
  if (!retained) {
    fsim::diagnostic::print_text(std::cerr, retained_diagnostics);
  }
  assert(retained && !retained_diagnostics.has_error());
}

void test_optimization(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const std::string_view standard) {
  const auto config = make_config(
      directory, source, optimization, standard);
  const auto reference =
      execute(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      execute(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      execute(config, fsim::app::SimulationEngine::compiled);
  for (const auto* capture : {&reference, &cold, &warm}) {
    if (capture->result.status != fsim::runtime::RunStatus::stopped
        || capture->result.time != 2
        || capture->value != "11111111111111") {
      std::cerr << "synchronization mismatch: status="
                << static_cast<int>(capture->result.status)
                << " time=" << capture->result.time
                << " value=" << capture->value << '\n';
    }
    assert(
        capture->result.status == fsim::runtime::RunStatus::stopped
        && capture->result.time == 2
        && capture->value == "11111111111111");
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
  logic [13:0] result;
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
    begin
      semaphore signed_sem = new(-1);
      semaphore empty_sem = new(0);
      signed_sem.put(0);
      signed_sem.put(2);
      ok = signed_sem.try_get(1);
      result[10] = (ok == 1);
      ok = empty_sem.try_get(0);
      result[11] = (ok == 1);
      empty_sem.get(0);
      result[12] = 1;
      signed_sem.put(0);
      result[13] = 1;
    end
    $finish;
  end
endmodule
)";
  }
  test_optimization(
      directory.path, source, fsim::project::Optimization::o0, "2017");
  test_optimization(
      directory.path, source, fsim::project::Optimization::o2, "2017");
  test_optimization(
      directory.path, source, fsim::project::Optimization::o0, "2023");
  test_optimization(
      directory.path, source, fsim::project::Optimization::o2, "2023");
  test_clocking_skew_contract(directory.path);
  std::cout << "synchronization application tests passed\n";
  return 0;
}
