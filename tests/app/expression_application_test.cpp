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

  const std::array<std::string, 14> signal_paths{
      "shift_rotate_app.arithmetic_left",
      "shift_rotate_app.rotated_left",
      "shift_rotate_app.rotated_right",
      "shift_rotate_app.reversed_logical_left",
      "shift_rotate_app.reversed_arithmetic_right",
      "shift_rotate_app.reversed_rotate_left",
      "shift_rotate_app.oversized_arithmetic_left",
      "shift_rotate_app.wrapped_rotate_left",
      "shift_rotate_app.wrapped_rotate_right",
      "shift_rotate_app.absolute_unknown",
      "shift_rotate_app.absolute_known",
      "shift_rotate_app.power_positive",
      "shift_rotate_app.power_negative",
      "shift_rotate_app.power_zero"};
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
          "Z10X0000",
          "XXXXXXXX",
          "00000101",
          "01010001",
          "11111000",
          "00000001"}));
  assert(reference.compiled_processes == 0);
  assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 3);
  assert(compiled.compiled_modules == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_verilog_power(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "verilog-power-expression-test";
  config.project.top = "verilog:work.verilog_power_app";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "verilog-power-cache-o0"
             : "verilog-power-cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::verilog;
  sources.standard = "2005";
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

  const std::array<std::string, 2> signal_paths{
      "verilog_power_app.positive",
      "verilog_power_app.left_associative"};
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
      == std::vector<std::string>{
          "01010001", "0000000001000000"}));
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 1);
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

void test_systemverilog_signedness_casts(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "signedness-cast-expression-test";
  config.project.top = "sv:work.signedness_cast_app";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "signedness-cache-o0"
             : "signedness-cache-o2");
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

  const std::array<std::string, 45> signal_paths{
      "signedness_cast_app.signed_less",
      "signedness_cast_app.unsigned_less",
      "signedness_cast_app.signed_shift",
      "signedness_cast_app.unsigned_shift",
      "signedness_cast_app.known_is_unknown",
      "signedness_cast_app.xz_is_unknown",
      "signedness_cast_app.object_bits",
      "signedness_cast_app.concatenation_bits",
      "signedness_cast_app.descending_left",
      "signedness_cast_app.descending_right",
      "signedness_cast_app.descending_low",
      "signedness_cast_app.descending_high",
      "signedness_cast_app.descending_size",
      "signedness_cast_app.descending_increment",
      "signedness_cast_app.ascending_left",
      "signedness_cast_app.ascending_right",
      "signedness_cast_app.ascending_low",
      "signedness_cast_app.ascending_high",
      "signedness_cast_app.ascending_size",
      "signedness_cast_app.ascending_increment",
      "signedness_cast_app.dimensions",
      "signedness_cast_app.unpacked_dimensions",
      "signedness_cast_app.power_positive",
      "signedness_cast_app.power_zero",
      "signedness_cast_app.power_left_associative",
      "signedness_cast_app.power_negative_exponent",
      "signedness_cast_app.power_minus_one_negative",
      "signedness_cast_app.power_zero_negative",
      "signedness_cast_app.power_unknown",
      "signedness_cast_app.power_parameter",
      "signedness_cast_app.onehot_zero",
      "signedness_cast_app.onehot_single",
      "signedness_cast_app.onehot_multiple",
      "signedness_cast_app.onehot_unknown",
      "signedness_cast_app.onehot0_zero",
      "signedness_cast_app.onehot0_single",
      "signedness_cast_app.onehot0_multiple",
      "signedness_cast_app.onehot0_unknown",
      "signedness_cast_app.countones_zero",
      "signedness_cast_app.countones_single",
      "signedness_cast_app.countones_multiple",
      "signedness_cast_app.countones_unknown",
      "signedness_cast_app.countbits_known",
      "signedness_cast_app.countbits_unknown",
      "signedness_cast_app.countbits_zero_x"};
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
      == std::vector<std::string>{
          "1",
          "1",
          "1111",
          "0000",
          "0",
          "1",
          "00000000000000000000000000000100",
          "00000000000000000000000000001000",
          "00000000000000000000000000000111",
          "00000000000000000000000000000100",
          "00000000000000000000000000000100",
          "00000000000000000000000000000111",
          "00000000000000000000000000000100",
          "00000000000000000000000000000001",
          "00000000000000000000000000000010",
          "00000000000000000000000000000101",
          "00000000000000000000000000000010",
          "00000000000000000000000000000101",
          "00000000000000000000000000000100",
          "11111111111111111111111111111111",
          "00000000000000000000000000000001",
          "00000000000000000000000000000000",
          "01010001",
          "00000001",
          "0000000001000000",
          "00000000",
          "11111111",
          "XXXXXXXX",
          "XXXXXXXX",
          "01010001",
          "0",
          "1",
          "0",
          "1",
          "1",
          "1",
          "0",
          "1",
          "00000000000000000000000000000000",
          "00000000000000000000000000000001",
          "00000000000000000000000000000011",
          "00000000000000000000000000000001",
          "00000000000000000000000000000010",
          "00000000000000000000000000000010",
          "00000000000000000000000000000010"}));
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 1);
  assert(compiled.compiled_modules == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_vhdl_concurrent_assertion(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-concurrent-assertion-test";
  config.project.top = "vhdl:work.concurrent_assertion_app(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "concurrent-assert-cache-o0"
             : "concurrent-assert-cache-o2");
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

  const std::array<std::string, 1> signal_paths{
      "concurrent_assertion_app.passed"};
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
  assert(compiled.values == std::vector<std::string>{"1"});
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 2);
  assert(compiled.compiled_modules == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(compiled.compiled_modules == 0);
#endif
}

void test_vhdl_falling_edge(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-falling-edge-test";
  config.project.top = "vhdl:work.falling_edge_app(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "falling-edge-cache-o0"
             : "falling-edge-cache-o2");
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

  const std::array<std::string, 1> signal_paths{
      "falling_edge_app.hit"};
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
  assert(compiled.values == std::vector<std::string>{"1"});
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 2);
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
  signal known_value : signed(7 downto 0);
  signal arithmetic_left : signed(7 downto 0);
  signal rotated_left : signed(7 downto 0);
  signal rotated_right : signed(7 downto 0);
  signal reversed_logical_left : signed(7 downto 0);
  signal reversed_arithmetic_right : signed(7 downto 0);
  signal reversed_rotate_left : signed(7 downto 0);
  signal oversized_arithmetic_left : signed(7 downto 0);
  signal wrapped_rotate_left : signed(7 downto 0);
  signal wrapped_rotate_right : signed(7 downto 0);
  signal absolute_unknown : signed(7 downto 0);
  signal absolute_known : signed(7 downto 0);
  signal power_positive : unsigned(7 downto 0);
  signal power_negative : signed(7 downto 0);
  signal power_zero : unsigned(7 downto 0);
begin
  value <= "10X0000Z";
  known_value <= "11111011";
  calculate: process(value, known_value)
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
    absolute_unknown <= abs value;
    absolute_known <= abs known_value;
    power_positive <= "00000011" ** 4;
    power_negative <= "11111110" ** 3;
    power_zero <= "00000111" ** 0;
  end process;
end architecture;
)";
  }
  const auto verilog_power_source =
      directory.path / "power.v";
  {
    std::ofstream output(verilog_power_source);
    output << R"(
module verilog_power_app;
  reg [7:0] positive;
  reg [15:0] left_associative;
  initial begin
    positive = 8'd3 ** 8'd4;
    left_associative = 16'd2 ** 16'd3 ** 16'd2;
    $finish;
  end
endmodule
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
  const auto signedness_source = directory.path / "signedness_cast.sv";
  {
    std::ofstream output(signedness_source);
    output << R"(
module signedness_cast_app #(
  parameter int PARAMETER_POWER = 3 ** 4
);
  logic [3:0] unsigned_value;
  logic signed [3:0] signed_value;
  logic signed_less;
  logic unsigned_less;
  logic [3:0] signed_shift;
  logic [3:0] unsigned_shift;
  logic known_is_unknown;
  logic xz_is_unknown;
  logic [31:0] object_bits;
  logic [31:0] concatenation_bits;
  logic [7:4] descending;
  logic [2:5] ascending;
  logic signed [31:0] descending_left;
  logic signed [31:0] descending_right;
  logic signed [31:0] descending_low;
  logic signed [31:0] descending_high;
  logic signed [31:0] descending_size;
  logic signed [31:0] descending_increment;
  logic signed [31:0] ascending_left;
  logic signed [31:0] ascending_right;
  logic signed [31:0] ascending_low;
  logic signed [31:0] ascending_high;
  logic signed [31:0] ascending_size;
  logic signed [31:0] ascending_increment;
  logic signed [31:0] dimensions;
  logic signed [31:0] unpacked_dimensions;
  logic [7:0] power_positive;
  logic [7:0] power_zero;
  logic [15:0] power_left_associative;
  logic signed [7:0] power_negative_exponent;
  logic signed [7:0] power_minus_one_negative;
  logic signed [7:0] power_zero_negative;
  logic [7:0] power_unknown;
  logic [7:0] power_parameter;
  logic onehot_zero;
  logic onehot_single;
  logic onehot_multiple;
  logic onehot_unknown;
  logic onehot0_zero;
  logic onehot0_single;
  logic onehot0_multiple;
  logic onehot0_unknown;
  logic signed [31:0] countones_zero;
  logic signed [31:0] countones_single;
  logic signed [31:0] countones_multiple;
  logic signed [31:0] countones_unknown;
  logic signed [31:0] countbits_known;
  logic signed [31:0] countbits_unknown;
  logic signed [31:0] countbits_zero_x;
  initial begin
    unsigned_value = 4'b1111;
    signed_value = 4'b0001;
    signed_less = $signed(unsigned_value) < signed_value;
    unsigned_less = $unsigned(signed_value) < unsigned_value;
    signed_shift = $signed(unsigned_value) >>> 1;
    unsigned_shift = $unsigned(signed_value) >>> 1;
    known_is_unknown = $isunknown(unsigned_value);
    xz_is_unknown = $isunknown(4'b10xz);
    object_bits = $bits(unsigned_value);
    concatenation_bits = $bits({unsigned_value, signed_value});
    descending_left = $left(descending, 1);
    descending_right = $right(descending);
    descending_low = $low(descending);
    descending_high = $high(descending);
    descending_size = $size(descending);
    descending_increment = $increment(descending);
    ascending_left = $left(ascending);
    ascending_right = $right(ascending);
    ascending_low = $low(ascending);
    ascending_high = $high(ascending);
    ascending_size = $size(ascending, 1);
    ascending_increment = $increment(ascending);
    dimensions = $dimensions(descending);
    unpacked_dimensions = $unpacked_dimensions(ascending);
    power_positive = 8'd3 ** 8'd4;
    power_zero = 8'd7 ** 8'd0;
    power_left_associative =
        16'd2 ** 16'd3 ** 16'd2;
    power_negative_exponent =
        $signed(8'hfe) ** $signed(8'hfd);
    power_minus_one_negative =
        $signed(8'hff) ** $signed(8'hfd);
    power_zero_negative =
        $signed(8'h00) ** $signed(8'hff);
    power_unknown = 8'b000000x1 ** 8'd2;
    power_parameter = PARAMETER_POWER;
    onehot_zero = $onehot(4'b0000);
    onehot_single = $onehot(4'b0010);
    onehot_multiple = $onehot(4'b1010);
    onehot_unknown = $onehot(4'bx001);
    onehot0_zero = $onehot0(4'b0000);
    onehot0_single = $onehot0(4'b0010);
    onehot0_multiple = $onehot0(4'b1010);
    onehot0_unknown = $onehot0(4'bz001);
    countones_zero = $countones(4'b0000);
    countones_single = $countones(4'b0010);
    countones_multiple = $countones(4'b1011);
    countones_unknown = $countones(4'bxz01);
    countbits_known = $countbits(4'b10xz, 1'b0, 1'b1);
    countbits_unknown = $countbits(4'b10xz, 1'bx, 1'bz);
    countbits_zero_x = $countbits(4'b10xz, 1'b0, 1'bx);
    $finish;
  end
endmodule
)";
  }
  const auto concurrent_assertion_source =
      directory.path / "concurrent_assertion.vhd";
  {
    std::ofstream output(concurrent_assertion_source);
    output << R"(
entity concurrent_assertion_app is
end entity;

architecture rtl of concurrent_assertion_app is
  signal passed : boolean;
begin
  passed <= true;
  constant_check: assert true
    report "unreachable concurrent assertion" severity failure;
end architecture;
)";
  }
  const auto falling_edge_source =
      directory.path / "falling_edge.vhd";
  {
    std::ofstream output(falling_edge_source);
    output << R"(
entity falling_edge_app is
end entity;

architecture rtl of falling_edge_app is
  signal clk : std_logic;
  signal hit : std_logic;
begin
  stimulus: process
  begin
    clk <= '1';
    wait for 1 ns;
    clk <= '0';
    wait for 1 ns;
    wait;
  end process;

  capture: process(clk)
  begin
    if falling_edge(clk) then
      hit <= '1';
    end if;
  end process;
end architecture;
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
  test_verilog_power(
      directory.path,
      verilog_power_source,
      fsim::project::Optimization::o0);
  test_verilog_power(
      directory.path,
      verilog_power_source,
      fsim::project::Optimization::o2);
  test_systemverilog_clog2(
      directory.path,
      clog2_source,
      fsim::project::Optimization::o0);
  test_systemverilog_clog2(
      directory.path,
      clog2_source,
      fsim::project::Optimization::o2);
  test_systemverilog_signedness_casts(
      directory.path,
      signedness_source,
      fsim::project::Optimization::o0);
  test_systemverilog_signedness_casts(
      directory.path,
      signedness_source,
      fsim::project::Optimization::o2);
  test_vhdl_concurrent_assertion(
      directory.path,
      concurrent_assertion_source,
      fsim::project::Optimization::o0);
  test_vhdl_concurrent_assertion(
      directory.path,
      concurrent_assertion_source,
      fsim::project::Optimization::o2);
  test_vhdl_falling_edge(
      directory.path,
      falling_edge_source,
      fsim::project::Optimization::o0);
  test_vhdl_falling_edge(
      directory.path,
      falling_edge_source,
      fsim::project::Optimization::o2);
}
