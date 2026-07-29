// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

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

struct OutputEvent {
  std::string text;
  bool newline{};
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const OutputEvent&, const OutputEvent&) = default;
};

struct ReportEvent {
  std::string message;
  fsim::runtime::simir::AssertionSeverity severity{
      fsim::runtime::simir::AssertionSeverity::note};
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const ReportEvent&, const ReportEvent&) = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<OutputEvent> output;
  std::vector<ReportEvent> reports;
  std::vector<std::string> keys;
  std::string selected;
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
  config.project.name = "sv-string-parameters";
  config.project.top = "sv:work.string_parameter_top";
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
  assert(project->design.specializations().size() == 3);
  Capture capture;
  capture.keys = project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  simulation.set_output_hook(
      [&capture](
          fsim::runtime::simir::ProcessId,
          const std::string_view text,
          const bool newline,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        capture.output.push_back(
            {std::string{text}, newline, time, delta});
      });
  simulation.set_report_hook(
      [&capture](
          fsim::runtime::simir::ProcessId,
          const std::string_view message,
          const fsim::runtime::simir::AssertionSeverity severity,
          const fsim::runtime::simir::SourceLocation&,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        capture.reports.push_back(
            {std::string{message}, severity, time, delta});
      });
  const auto selected =
      simulation.find_signal("string_parameter_top.selected");
  assert(selected);
  capture.result = simulation.run();
  capture.selected =
      simulation.read_signal(*selected).to_msb_string();
  return capture;
}

void verify(
    const Capture& capture,
    const std::string_view selected,
    const std::string_view selected_label) {
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  assert(capture.selected == selected);
  const auto decorated =
      std::string{selected_label} + "!";
  const std::vector<OutputEvent> expected_output{
      {"base", true, 0, 0},
      {"base!", false, 0, 0},
      {std::string{selected_label}, true, 0, 0},
      {decorated, false, 0, 0},
      {"base!", true, 0, 0},
      {decorated, true, 0, 0},
  };
  assert(capture.output == expected_output);
  const std::vector<ReportEvent> expected_reports{
      {"base", fsim::runtime::simir::AssertionSeverity::note, 0, 0},
      {"base!", fsim::runtime::simir::AssertionSeverity::warning, 0, 0},
      {std::string{selected_label},
       fsim::runtime::simir::AssertionSeverity::note,
       0,
       0},
      {decorated,
       fsim::runtime::simir::AssertionSeverity::warning,
       0,
       0},
  };
  assert(capture.reports == expected_reports);
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-string-parameters-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto child_source = directory.path / "child.sv";
  const auto top_source = directory.path / "top.sv";
  {
    std::ofstream output(child_source, std::ios::binary);
    output << R"(
module string_emitter #(
  parameter string LABEL = "base",
  parameter string DECORATED = {LABEL, "!"}
) (
  output logic selected
);
  if (LABEL == "go") begin : chosen
    initial selected = 1'b1;
  end else begin : other
    initial selected = 1'b0;
  end
  initial begin
    $display(LABEL);
    $write("%s", DECORATED);
    $strobe(DECORATED);
    $info(LABEL);
    assert (1'b0) else $warning(DECORATED);
  end
endmodule
)";
    assert(output.good());
  }
  const auto write_top =
      [&](const std::string_view label) {
        std::ofstream output(
            top_source, std::ios::binary | std::ios::trunc);
        output << "module string_parameter_top;\n"
               << "  logic default_selected;\n"
               << "  logic selected;\n"
               << "  string_emitter defaults(default_selected);\n"
               << "  string_emitter #(.LABEL(\"" << label
               << "\")) configured(selected);\n"
               << "  initial begin #1; $finish; end\n"
               << "endmodule\n";
        assert(output.good());
      };
  write_top("go");

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
    verify(reference, "1", "go");
    verify(cold, "1", "go");
    verify(warm, "1", "go");
    assert(reference.output == cold.output);
    assert(cold.output == warm.output);
    assert(reference.reports == cold.reports);
    assert(cold.reports == warm.reports);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
    if (optimization == fsim::project::Optimization::o2) {
      baseline_o2_keys = warm.keys;
    }
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 5);
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

  auto changed_seed_config = make_config(
      directory.path,
      child_source,
      top_source,
      fsim::project::Optimization::o2);
  changed_seed_config.project.seed = 91'337;
  const auto changed_seed = run_once(
      changed_seed_config,
      fsim::app::SimulationEngine::compiled);
  verify(changed_seed, "1", "go");
  assert(changed_seed.keys == baseline_o2_keys);
#if defined(FSIM_HAS_LLVM)
  assert(changed_seed.cache.hits == 3);
  assert(changed_seed.cache.misses == 0);
#endif

  write_top("edited");
  const auto changed = run_once(
      make_config(
          directory.path,
          child_source,
          top_source,
          fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled);
  verify(changed, "0", "edited");
  assert(baseline_o2_keys.size() == 3);
  assert(changed.keys.size() == 3);
  assert(changed.keys[0] != baseline_o2_keys[0]);
  assert(changed.keys[1] == baseline_o2_keys[1]);
  assert(changed.keys[2] != baseline_o2_keys[2]);
#if defined(FSIM_HAS_LLVM)
  assert(changed.cache.hits == 1);
  assert(changed.cache.misses == 2);
  assert(changed.cache.stores == 2);
#endif
}
