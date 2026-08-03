// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
  fsim::runtime::simir::SourceLocation report_source;
  fsim::runtime::simir::SourceLocation debug_source;
  fsim::runtime::Logic4Word marker{};
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

void write_source(
    const std::filesystem::path& source,
    const std::string_view logical_name,
    const std::size_t logical_line) {
  std::ofstream output(source, std::ios::binary);
  output
      << "`line " << logical_line << " \"" << logical_name << "\" 0\n"
      << "module line_provenance;\n"
      << "  logic marker;\n"
      << "  initial begin\n"
      << "    marker = 1'b0;\n"
      << "    $info(\"mapped report\");\n"
      << "    marker = 1'b1;\n"
      << "    #1 $finish;\n"
      << "  end\n"
      << "endmodule\n";
  assert(output.good());
}

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "line-provenance";
  config.project.top = "sv:work.line_provenance";
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
  Capture capture;
  for (const auto& process : project->design.processes()) {
    for (const auto& operation : process.operations) {
      if (const auto* report =
              fsim::runtime::simir::operation_get_if<fsim::runtime::simir::Report>(&operation)) {
        capture.report_source = report->source;
      }
      if (const auto* point =
              fsim::runtime::simir::operation_get_if<fsim::runtime::simir::DebugPoint>(&operation);
          point != nullptr
          && point->kind
              == fsim::runtime::simir::DebugPointKind::statement
          && point->source.line >= capture.debug_source.line) {
        capture.debug_source = point->source;
      }
    }
  }

  fsim::app::Simulation simulation{
      std::move(*project), 1000, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();
  std::optional<fsim::runtime::simir::SourceLocation> callback_source;
  simulation.set_report_hook(
      [&](const fsim::runtime::simir::ProcessId,
          const std::string_view message,
          const fsim::runtime::simir::AssertionSeverity severity,
          const fsim::runtime::simir::SourceLocation& source,
          const fsim::runtime::SimulationTick,
          const std::uint64_t) {
        assert(message == "mapped report");
        assert(
            severity
            == fsim::runtime::simir::AssertionSeverity::note);
        callback_source = source;
      });
  (void)simulation.run();
  assert(callback_source);
  assert(*callback_source == capture.report_source);
  const auto marker =
      simulation.find_signal("line_provenance.marker");
  assert(marker);
  capture.marker = simulation.read_signal(*marker).low_word();
  return capture;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  constexpr std::string_view first_name{
      "virtual/windows/line_provenance.sv"};
  constexpr std::size_t first_line = 400;
  write_source(source, first_name, first_line);
  const auto config = config_for(directory, source, optimization);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.marker == cold.marker);
  assert(reference.marker == warm.marker);
  assert(reference.marker.aval == 1 && reference.marker.bval == 0);
  assert(reference.report_source == cold.report_source);
  assert(reference.report_source == warm.report_source);
  assert(reference.report_source.path == first_name);
  assert(reference.report_source.line == first_line + 4);
  assert(reference.debug_source.path == first_name);
  assert(reference.debug_source.line >= first_line);
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 1);
  assert(cold.native_cache.hits == 0);
  assert(cold.native_cache.misses == 1);
  assert(cold.native_cache.stores == 1);
  assert(warm.compiled_processes == 1);
  assert(warm.native_cache.hits == 1);
  assert(warm.native_cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif

  constexpr std::string_view changed_name{
      "virtual/windows/remapped_line_provenance.sv"};
  write_source(source, changed_name, first_line + 100);
  const auto changed =
      run_once(config, fsim::app::SimulationEngine::compiled);
  assert(changed.marker == reference.marker);
  assert(changed.report_source.path == changed_name);
  assert(changed.report_source.line == first_line + 104);
#if defined(FSIM_HAS_LLVM)
  assert(changed.compiled_processes == 1);
  assert(changed.native_cache.hits == 0);
  assert(changed.native_cache.misses == 1);
  assert(changed.native_cache.stores == 1);
#endif
}

}  // namespace

int main() {
  // FSIM-CONFORMANCE CF-COMMON-SOURCE-001 source=SRC-COCOTB expectation=execute
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-line-directive-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "line_provenance.sv";

  verify_mode(
      directory.path,
      source,
      fsim::project::Optimization::o0);
  verify_mode(
      directory.path,
      source,
      fsim::project::Optimization::o2);
  std::cout << "line directive application tests passed\n";
  return 0;
}
