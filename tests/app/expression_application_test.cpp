// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
  std::vector<std::string> values;
  fsim::app::NativeCacheStatistics native_cache;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    for (const auto& note : diagnostic.notes) {
      std::cerr << "  " << note.message << '\n';
    }
  }
}

template <std::size_t SignalCount>
[[nodiscard]] Capture run(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine,
    const std::array<std::string, SignalCount>& signal_paths) {
  std::array<
      fsim::runtime::simir::SignalId, SignalCount> signals{};
  for (std::size_t index = 0; index < signal_paths.size(); ++index) {
    const auto signal = project.design.find_signal(signal_paths[index]);
    assert(signal);
    signals[index] = *signal;
  }

  fsim::app::Simulation simulation(
      std::move(project), 1000, engine);
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.native_cache = simulation.native_cache_statistics();
  capture.result = simulation.run();
  capture.values.reserve(signals.size());
  for (const auto signal : signals) {
    capture.values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  return capture;
}

void test_systemverilog_clog2(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "systemverilog-clog2-expression-test";
  config.project.top = "sv:work.clog2_application";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "clog2-cache-o0"
             : "clog2-cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project =
      fsim::app::build_project(config, diagnostics);
  auto compiled_project =
      fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project);
  assert(compiled_project);
  assert(reference_project->design.specializations().size() == 3);
  assert(reference_project->specialization_cache_keys.size() == 3);

  std::optional<std::size_t> narrow_specialization;
  std::optional<std::size_t> wide_specialization;
  for (std::size_t index = 0;
       index < reference_project->design.specializations().size();
       ++index) {
    const auto& specialization =
        reference_project->design.specializations()[index];
    if (specialization.instance == "clog2_application.narrow") {
      narrow_specialization = index;
      assert((
          specialization.parameter_values
          == std::vector<std::pair<std::string, std::string>>{
              {"DEPTH", "9"}, {"WIDTH", "4"}}));
    } else if (
        specialization.instance == "clog2_application.wide") {
      wide_specialization = index;
      assert((
          specialization.parameter_values
          == std::vector<std::pair<std::string, std::string>>{
              {"DEPTH", "17"}, {"WIDTH", "5"}}));
    }
  }
  assert(narrow_specialization && wide_specialization);
  assert(
      reference_project->specialization_cache_keys
          .at(*narrow_specialization)
      != reference_project->specialization_cache_keys
             .at(*wide_specialization));

  const std::array<std::string, 2> signal_paths{
      "clog2_application.narrow_result",
      "clog2_application.wide_result"};
  const auto reference = run(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter,
      signal_paths);
  const auto compiled = run(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled,
      signal_paths);

  assert(reference.result.status == fsim::runtime::RunStatus::completed);
  assert(reference.result.status == compiled.result.status);
  assert(reference.result.time == compiled.result.time);
  assert(reference.result.delta == compiled.result.delta);
  assert(reference.values == compiled.values);
  assert((
      compiled.values
      == std::vector<std::string>{"0100", "00101"}));
  assert(reference.compiled_processes == 0);
  assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 2);
  assert(compiled.compiled_modules == 2);
  assert(compiled.native_cache.hits == 0);
  assert(compiled.native_cache.misses == 2);
  assert(compiled.native_cache.stores == 2);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_vhdl_shift_rotate(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-shift-rotate-expression-test";
  config.project.top = "vhdl:work.shift_rotate_app(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "vhdl-cache-o0"
             : "vhdl-cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project =
      fsim::app::build_project(config, diagnostics);
  auto compiled_project =
      fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project);
  assert(compiled_project);

  const std::array<std::string, 9> signal_paths{
      "shift_rotate_app.arithmetic_left",
      "shift_rotate_app.rotated_left",
      "shift_rotate_app.rotated_right",
      "shift_rotate_app.reversed_logical_left",
      "shift_rotate_app.reversed_arithmetic_right",
      "shift_rotate_app.reversed_rotate_left",
      "shift_rotate_app.oversized_arithmetic_left",
      "shift_rotate_app.wrapped_rotate_left",
      "shift_rotate_app.wrapped_rotate_right"};
  const auto reference = run(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter,
      signal_paths);
  const auto compiled = run(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled,
      signal_paths);

  assert(reference.result.status == fsim::runtime::RunStatus::completed);
  assert(reference.result.status == compiled.result.status);
  assert(reference.result.time == compiled.result.time);
  assert(reference.result.delta == compiled.result.delta);
  assert(reference.values == compiled.values);
  assert((
      compiled.values
      == std::vector<std::string>{
          "0X0000ZZ",
          "0X0000Z1",
          "Z10X0000",
          "010X0000",
          "0X0000ZZ",
          "Z10X0000",
          "ZZZZZZZZ",
          "0X0000Z1",
          "Z10X0000"}));
  assert(reference.compiled_processes == 0);
  assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 2);
  assert(compiled.compiled_modules == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_wildcard_equality(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "wildcard-equality-expression-test";
  config.project.top = "sv:work.wildcard_equality_app";
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

  fsim::diagnostic::Engine diagnostics;
  auto reference_project =
      fsim::app::build_project(config, diagnostics);
  auto compiled_project =
      fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    print_diagnostics(diagnostics);
  }
  assert(reference_project);
  assert(compiled_project);

  const std::array<std::string, 5> signal_paths{
      "wildcard_equality_app.masked",
      "wildcard_equality_app.left_unknown",
      "wildcard_equality_app.known_mismatch",
      "wildcard_equality_app.wildcard_neq",
      "wildcard_equality_app.logical_equal"};
  const auto reference = run(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter,
      signal_paths);
  const auto compiled = run(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled,
      signal_paths);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.status == compiled.result.status);
  assert(reference.result.time == compiled.result.time);
  assert(reference.result.delta == compiled.result.delta);
  assert(reference.values == compiled.values);
  assert((
      compiled.values
      == std::vector<std::string>{"1", "X", "0", "0", "X"}));
  assert(reference.compiled_processes == 0);
  assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 1);
  assert(compiled.compiled_modules == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

}  // namespace

int main() {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-expression-application-test-"
         + std::to_string(suffix))};
  std::filesystem::create_directories(directory.path);

  const auto source = directory.path / "wildcard_equality.sv";
  {
    std::ofstream output(source);
    output << R"(
module wildcard_equality_app;
  logic masked;
  logic left_unknown;
  logic known_mismatch;
  logic wildcard_neq;
  logic logical_equal;
  initial begin
    masked = 4'b10x1 ==? 4'b10?1;
    left_unknown = 4'b10x1 ==? 4'b1011;
    known_mismatch = 4'b1101 ==? 4'b10?1;
    wildcard_neq = 4'b1001 !=? 4'b10z1;
    logical_equal = 2'bx0 == 2'bx1;
    $finish;
  end
endmodule
)";
  }
  const auto vhdl_source = directory.path / "shift_rotate.vhd";
  {
    std::ofstream output(vhdl_source);
    output << R"(
entity shift_rotate_app is
end entity;

architecture rtl of shift_rotate_app is
  signal value : signed(7 downto 0);
  signal arithmetic_left : signed(7 downto 0);
  signal rotated_left : signed(7 downto 0);
  signal rotated_right : signed(7 downto 0);
  signal reversed_logical_left : signed(7 downto 0);
  signal reversed_arithmetic_right : signed(7 downto 0);
  signal reversed_rotate_left : signed(7 downto 0);
  signal oversized_arithmetic_left : signed(7 downto 0);
  signal wrapped_rotate_left : signed(7 downto 0);
  signal wrapped_rotate_right : signed(7 downto 0);
begin
  value <= "10X0000Z";
  calculate: process(value)
  begin
    arithmetic_left <= value sla 1;
    rotated_left <= value rol 1;
    rotated_right <= value ror 1;
    reversed_logical_left <= value sll -1;
    reversed_arithmetic_right <= value sra -1;
    reversed_rotate_left <= value rol -1;
    oversized_arithmetic_left <= value sla 8;
    wrapped_rotate_left <= value rol 9;
    wrapped_rotate_right <= value ror 9;
  end process;
end architecture;
)";
  }
  const auto clog2_source = directory.path / "clog2.sv";
  {
    std::ofstream output(clog2_source);
    output << R"(
module clog2_child #(
  parameter int DEPTH = 1,
  localparam int WIDTH = $clog2(DEPTH)
) (
  output logic [WIDTH-1:0] result
);
  assign result = WIDTH;
endmodule

module clog2_application;
  logic [3:0] narrow_result;
  logic [4:0] wide_result;
  clog2_child #(.DEPTH(9)) narrow(.result(narrow_result));
  clog2_child #(.DEPTH(17)) wide(.result(wide_result));
endmodule
)";
  }

  test_wildcard_equality(
      directory.path, source, fsim::project::Optimization::o0);
  test_wildcard_equality(
      directory.path, source, fsim::project::Optimization::o2);
  test_vhdl_shift_rotate(
      directory.path,
      vhdl_source,
      fsim::project::Optimization::o0);
  test_vhdl_shift_rotate(
      directory.path,
      vhdl_source,
      fsim::project::Optimization::o2);
  test_systemverilog_clog2(
      directory.path,
      clog2_source,
      fsim::project::Optimization::o0);
  test_systemverilog_clog2(
      directory.path,
      clog2_source,
      fsim::project::Optimization::o2);
}
