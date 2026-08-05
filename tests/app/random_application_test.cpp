// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <ranges>
#include <sstream>
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
  std::array<fsim::runtime::Logic4Word, 12> values{};
  std::vector<std::string> output;
  std::size_t compiled_processes{};
};

int run_cli(
    const std::vector<std::string>& arguments,
    std::istream& input,
    std::ostream& output,
    std::ostream& error) {
  std::vector<const char*> raw;
  raw.reserve(arguments.size());
  for (const auto& argument : arguments) {
    raw.push_back(argument.c_str());
  }
  return fsim::cli::run(
      static_cast<int>(raw.size()),
      raw.data(),
      fsim::app::make_cli_services(input),
      output,
      error);
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine) {
  fsim::app::Simulation simulation{
      std::move(project), 1000, engine};
  constexpr std::array<std::string_view, 12> names{
      "a", "b", "c", "d", "e", "f", "u", "p0", "p1",
      "scope_result", "scope_value", "scope_mode"};
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  simulation.set_output_hook(
      [&capture](
          const fsim::runtime::simir::ProcessId,
          const std::string_view text,
          const bool,
          const fsim::runtime::SimulationTick,
          const std::uint64_t) {
        capture.output.emplace_back(text);
      });
  capture.result = simulation.run();
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal =
        simulation.find_signal(
            "random_test." + std::string{names[index]});
    assert(signal);
    capture.values[index] =
        simulation.read_signal(*signal).low_word();
  }
  return capture;
}

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const std::uint64_t seed) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "random-test";
  config.project.top = "sv:work.random_test";
  config.project.time_resolution = "1ns";
  config.project.seed = seed;
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
  assert(project->seed == config.project.seed);
  assert(!project->entropy_seed);
  return execute(std::move(*project), engine);
}

void test_random(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  const auto config =
      config_for(directory, source, optimization, 123);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto compiled =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto repeated =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  auto changed_config = config;
  changed_config.project.seed = 124;
  const auto changed_seed =
      run_once(
          changed_config,
          fsim::app::SimulationEngine::interpreter);

  assert(reference.result.status == fsim::runtime::RunStatus::completed);
  assert(compiled.result.status == fsim::runtime::RunStatus::completed);
  assert(reference.values == compiled.values);
  assert(reference.output == compiled.output);
  assert(
      reference.output.size() == 2
      && reference.output[0].starts_with("cli=")
      && reference.output[1].starts_with("signed="));
  assert(reference.values == repeated.values);
  assert(reference.values[0] != changed_seed.values[0]);
  assert(
      reference.values[4].bval == 0
      && reference.values[4].aval <= 9);
  assert(
      reference.values[5].bval == 0
      && reference.values[5].aval >= 3
      && reference.values[5].aval <= 9);
  assert(
      reference.values[6].bval
      == std::numeric_limits<std::uint32_t>::max());
  assert(reference.values[7] != reference.values[8]);
  assert(reference.values[9].aval == 1 && reference.values[9].bval == 0);
  assert(reference.values[10].aval <= 7 && reference.values[10].bval == 0);
  assert(reference.values[11].aval <= 2 && reference.values[11].bval == 0);
  assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 2);
#else
  assert(compiled.compiled_processes == 0);
#endif
}

std::string run_seeded_cli(
    const std::filesystem::path& manifest,
    const std::string_view seed) {
  std::istringstream input;
  std::ostringstream output;
  std::ostringstream error;
  const auto result = run_cli(
      {
          "fsim",
          "run",
          "-p",
          manifest.string(),
          "--seed",
          std::string{seed},
      },
      input,
      output,
      error);
  assert(result == 0);
  assert(error.str().empty());
  return output.str();
}

void test_cli_seed_selection(
    const std::filesystem::path& directory,
    const std::filesystem::path& manifest) {
  std::istringstream default_input;
  std::ostringstream default_output;
  std::ostringstream default_error;
  assert(
      run_cli(
          {"fsim", "run", "-p", manifest.string()},
          default_input,
          default_output,
          default_error)
      == 0);
  assert(default_error.str().empty());
  assert(default_output.str() == run_seeded_cli(manifest, "1"));

  const auto first = run_seeded_cli(manifest, "555");
  const auto repeated = run_seeded_cli(manifest, "555");
  const auto changed = run_seeded_cli(manifest, "556");
  assert(first == repeated);
  assert(first != changed);
  assert(first.find("cli=") != std::string::npos);

  const auto random = run_seeded_cli(manifest, "random");
  const std::string_view marker{"random seed "};
  const auto marker_position = random.find(marker);
  assert(marker_position != std::string::npos);
  const auto digits_start = marker_position + marker.size();
  const auto digits_end = random.find('\n', digits_start);
  assert(digits_end != std::string::npos && digits_end > digits_start);
  for (const auto character :
       std::string_view{random}.substr(
           digits_start, digits_end - digits_start)) {
    assert(character >= '0' && character <= '9');
  }
  assert(random.find("cli=") != std::string::npos);
  static_cast<void>(directory);
}

void test_invalid_scope_randomize(
    const std::filesystem::path& directory) {
  const auto source = directory / "invalid_scope_randomize.sv";
  {
    std::ofstream output(source);
    output << R"(
module invalid_scope_randomize;
  logic module_value;
  initial begin
    logic local_value;
    logic [64:0] wide_value;
    int result;
    result = std::randomize();
    result = std::randomize(local_value + 1);
    result = std::randomize(module_value);
    result = std::randomize(wide_value);
  end
endmodule
)";
  }
  auto config = config_for(
      directory, source, fsim::project::Optimization::o0, 1);
  config.project.name = "invalid-scope-randomize";
  config.project.top = "sv:work.invalid_scope_randomize";
  config.build.cache_path = directory / "invalid-scope-randomize-cache";
  fsim::diagnostic::Engine diagnostics;
  const auto project = fsim::app::build_project(config, diagnostics);
  assert(!project);
  for (const auto code : {
           "FSIM-ELAB-SVRAND-001",
           "FSIM-ELAB-SVRAND-002",
           "FSIM-ELAB-SVRAND-003",
           "FSIM-ELAB-SVRAND-004"}) {
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
          return diagnostic.code == code;
        }));
  }
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-random-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "random_test.sv";
  {
    std::ofstream output(source);
    output << R"(
module random_test;
  typedef enum logic [1:0] {MODE_ZERO, MODE_ONE, MODE_TWO} mode_t;
  logic [31:0] a, b, c, d, e, f, u, p0, p1;
  int scope_result;
  logic [2:0] scope_value;
  mode_t scope_mode;
  initial begin
    logic [2:0] scoped;
    mode_t mode;
    u = $urandom_range(4'bx);
    a = $urandom;
    b = $urandom();
    c = $random;
    d = $random();
    e = $urandom_range(9);
    f = $urandom_range(3, 9);
    scope_result = std::randomize(scoped, mode);
    scope_value = scoped;
    scope_mode = mode;
    $display("cli=%h", $urandom);
    $display("signed=%d", $random);
  end
  initial p0 = $urandom;
  initial p1 = $urandom;
endmodule
)";
  }
  const auto manifest = directory.path / "fsim.toml";
  {
    std::ofstream output(manifest);
    output
        << "schema = 2\n"
        << "[project]\n"
        << "name = \"random-test\"\n"
        << "top = \"sv:work.random_test\"\n"
        << "time_resolution = \"1ns\"\n"
        << "[[source_set]]\n"
        << "language = \"systemverilog\"\n"
        << "standard = \"2017\"\n"
        << "library = \"work\"\n"
        << "files = [\"random_test.sv\"]\n"
        << "[build]\n"
        << "optimization = \"O2\"\n"
        << "cache_path = \"cli-cache\"\n"
        << "[run]\n"
        << "max_deltas = 1000\n";
  }

  test_random(
      directory.path, source, fsim::project::Optimization::o0);
  test_random(
      directory.path, source, fsim::project::Optimization::o2);
  test_invalid_scope_randomize(directory.path);
  test_cli_seed_selection(directory.path, manifest);
  std::cout << "random application tests passed\n";
  return 0;
}
