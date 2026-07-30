// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
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
  fsim::runtime::RunResult result;
  std::string output_file;
  std::string saved_line;
  std::vector<std::string> keys;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  fsim::app::NativeCacheStatistics cache;
  std::size_t compiled_processes{};
};

void write_text(
    const std::filesystem::path& path,
    const std::string_view text) {
  std::ofstream output(
      path, std::ios::binary | std::ios::trunc);
  output << text;
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
    const std::filesystem::path& stable,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-files";
  config.project.top = "sv:work.file_top";
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
  sources.files = {stable, source};
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
  capture.result = simulation.run();
  const auto saved = std::ranges::find_if(
      simulation.design().string_objects(),
      [](const auto& object) {
        return object.name == "file_top.saved";
      });
  assert(saved != simulation.design().string_objects().end());
  capture.saved_line = simulation.read_string_object(saved->id);
  capture.output_file = read_text(config.base_directory / "output.txt");
  return capture;
}

void verify_suspension(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  std::optional<fsim::runtime::simir::ProcessId> process_id;
  std::optional<std::size_t> handle_local;
  for (const auto& process : simulation.design().processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size(); ++index) {
      if (process.debug_locals[index].name
          == "read_one.file_handle") {
        process_id = process.id;
        handle_local = index;
      }
    }
  }
  assert(process_id && handle_local);
  bool armed = false;
  bool requested = false;
  simulation.set_execution_point_hook(
      [&](fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::simir::ExecutionPoint& point) {
        if (point.process != *process_id) {
          return;
        }
        if (point.kind
            == fsim::runtime::simir::ExecutionPointKind::wait) {
          armed = true;
        } else if (
            armed && !requested
            && point.kind
                == fsim::runtime::simir::
                    ExecutionPointKind::process_suspend) {
          requested = true;
          scheduler.request_stop();
        }
      });
  const auto stopped = simulation.run();
  assert(requested);
  assert(stopped.status == fsim::runtime::RunStatus::stopped);
  assert(stopped.time == 0);
  const auto handle = simulation.read_process_local(
      *process_id, *handle_local).low_word();
  assert(handle.bval == 0 && handle.aval != 0);
  simulation.clear_stop();
  const auto resumed = simulation.run();
  assert(resumed.status == fsim::runtime::RunStatus::completed);
  assert(
      read_text(config.base_directory / "output.txt")
      == "value=7\ntail");
}

void verify_bad_path(
    const fsim::project::Config& config,
    const std::filesystem::path& source,
    const fsim::app::SimulationEngine engine) {
  write_text(
      source,
      R"(
module file_top;
  integer handle;
  initial handle = $fopen("../escape.txt", "w");
endmodule
)");
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  try {
    static_cast<void>(simulation.run());
    assert(false);
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    assert(
        std::string{error.what()}.find(
            "manifest root")
        != std::string::npos);
  }
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-sv-files-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto stable = directory.path / "stable.sv";
  const auto source = directory.path / "file.sv";
  const auto input = directory.path / "input.txt";
  write_text(
      stable,
      R"(
module stable_child;
  initial begin end
endmodule
)");
  const auto write_source =
      [&](const std::string_view label) {
        std::ofstream output(
            source, std::ios::binary | std::ios::trunc);
        output
            << "`timescale 1ns/1ns\n"
            << "module file_top;\n"
            << "  stable_child stable();\n"
            << "  integer writer;\n"
            << "  integer reader;\n"
            << "  integer count;\n"
            << "  integer eof_status;\n"
            << "  integer error_status;\n"
            << "  string line;\n"
            << "  string saved;\n"
            << "  string error;\n"
            << "  task automatic read_one(\n"
            << "      input integer file_handle,\n"
            << "      output string value,\n"
            << "      output integer result);\n"
            << "    #1;\n"
            << "    result = $fgets(value, file_handle);\n"
            << "  endtask\n"
            << "  initial begin : worker\n"
            << "    writer = $fopen(\"output.txt\", \"w\");\n"
            << "    $fdisplay(writer, \"" << label
            << "=%0d\", 7);\n"
            << "    $fwrite(writer, \"%s\", \"tail\");\n"
            << "    $fclose(writer);\n"
            << "    reader = $fopen(\"input.txt\", \"r\");\n"
            << "    read_one(reader, line, count);\n"
            << "    saved = line;\n"
            << "    count = $fgets(line, reader);\n"
            << "    eof_status = $feof(reader);\n"
            << "    error_status = $ferror(reader, error);\n"
            << "    $fclose(reader);\n"
            << "  end\n"
            << "endmodule\n";
        assert(output.good());
      };

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_source("value");
    write_text(input, "alpha\n");
    const auto config =
        make_config(directory.path, stable, source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    assert(reference.result.status
           == fsim::runtime::RunStatus::completed);
    assert(reference.result.time == 1);
    assert(reference.output_file == "value=7\ntail");
    assert(reference.saved_line == "alpha\n");
    assert(reference.output_file == cold.output_file);
    assert(reference.saved_line == cold.saved_line);
    assert(cold.output_file == warm.output_file);
    assert(cold.saved_line == warm.saved_line);
    assert(reference.keys == cold.keys && cold.keys == warm.keys);
    assert(
        std::ranges::count_if(
            reference.points,
            [](const auto& point) {
              return point.kind
                  == fsim::runtime::simir::
                      ExecutionPointKind::call;
            })
        >= 7);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 2);
    assert(cold.cache.misses == 2 && cold.cache.stores == 2);
    assert(warm.cache.hits == 2);
#endif
    verify_suspension(
        config, fsim::app::SimulationEngine::interpreter);
    verify_suspension(
        config, fsim::app::SimulationEngine::compiled);

    write_text(input, "beta\n");
    const auto changed_input =
        run_once(config, fsim::app::SimulationEngine::compiled);
    assert(changed_input.saved_line == "beta\n");
    assert(changed_input.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
    assert(changed_input.cache.hits == 2);
#endif

    write_source("changed");
    const auto changed_source =
        run_once(config, fsim::app::SimulationEngine::compiled);
    assert(changed_source.output_file == "changed=7\ntail");
    assert(changed_source.keys != changed_input.keys);
#if defined(FSIM_HAS_LLVM)
    assert(changed_source.cache.hits == 1);
    assert(changed_source.cache.misses == 1);
    assert(changed_source.cache.stores == 1);
#endif
    verify_bad_path(
        config, source, fsim::app::SimulationEngine::interpreter);
    verify_bad_path(
        config, source, fsim::app::SimulationEngine::compiled);
  }
}
