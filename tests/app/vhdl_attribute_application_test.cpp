// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <system_error>

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
  std::array<std::string, 8> values;
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
  config.project.name = "vhdl-attributes";
  config.project.top = "vhdl:work.attribute_execution(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path = directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
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
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  constexpr std::array<std::string_view, 8> paths{
      "attribute_execution.scalar_result",
      "attribute_execution.bound_result",
      "attribute_execution.nested_result",
      "attribute_execution.static_result",
      "attribute_execution.loop_result",
      "attribute_execution.boolean_result",
      "attribute_execution.bit_position",
      "attribute_execution.dynamic_result"};
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
  const auto integer = [](const std::uint32_t value) {
    auto bits = std::string(32, '0');
    for (std::size_t bit = 0; bit < 32; ++bit) {
      if ((value & (std::uint32_t{1} << bit)) != 0) {
        bits[31 - bit] = '1';
      }
    }
    return bits;
  };
  assert(capture.result.status == fsim::runtime::RunStatus::completed);
  assert((capture.values == std::array<std::string, 8>{
      integer(1), integer(6), integer(3), integer(7), integer(14),
      "1", integer(1), integer(std::numeric_limits<std::uint32_t>::max())}));
}

std::string run_dynamic_failure(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  const auto source = simulation.find_signal(
      "attribute_execution.dynamic_source");
  assert(source);
  simulation.deposit_signal(
      *source,
      fsim::runtime::PackedLogic4::from_msb_string(
          "00000000000000000000000000000010"));
  try {
    static_cast<void>(simulation.run());
  } catch (const std::exception& error) {
    return error.what();
  }
  assert(false);
  return {};
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-attributes-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "attribute_execution.vhd";
  {
    std::ofstream output{source};
    output << R"(
entity Attribute_Execution is end entity;
architecture rtl of attribute_execution is
  subtype Count_T is integer range -2 to 2;
  type Mode_T is (Idle, Ready, Busy);
  subtype Reverse_Mode_T is Mode_T range Busy downto Idle;
  type Matrix_T is array (2 downto 1, -1 to 1) of bit;
  type Holder_T is record
    Matrix : Matrix_T;
    Mode : Reverse_Mode_T;
  end record;
  constant Static_Total : integer :=
    Matrix_T'length(1) + Matrix_T'length(2) + Count_T'high;
  signal Holder : Holder_T;
  signal Scalar_Result : integer;
  signal Bound_Result : integer;
  signal Nested_Result : integer;
  signal Static_Result : integer;
  signal Loop_Result : integer;
  signal Boolean_Result : boolean;
  signal Bit_Position : integer;
  signal Dynamic_Source : Count_T;
  signal Dynamic_Result : integer;
begin
  drive : process
    variable Count : integer := 0;
  begin
    Scalar_Result <= Count_T'succ(-2)
      + Count_T'rightof(0) + Count_T'pos(1);
    Bound_Result <= Matrix_T'left(1)
      + Matrix_T'right(2) + Matrix_T'length(2);
    Nested_Result <= Holder.Matrix'length(2);
    Static_Result <= Static_Total;
    Boolean_Result <= boolean'succ(false);
    Bit_Position <= bit'pos('1');
    Dynamic_Result <= Count_T'succ(Dynamic_Source);
    for Index in Count_T'range loop Count := Count + 1; end loop;
    for Index in Matrix_T'reverse_range(2) loop
      Count := Count + 1;
    end loop;
    for Item in Reverse_Mode_T'range loop Count := Count + 1; end loop;
    for Index in Holder.Matrix'range(2) loop
      Count := Count + 1;
    end loop;
    Loop_Result <= Count;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(directory.path, source, optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    verify_capture(cold);
    verify_capture(warm);
    assert(reference.result.time == cold.result.time);
    assert(reference.result.delta == cold.result.delta);
    assert(reference.values == cold.values && cold.values == warm.values);
    const auto reference_failure = run_dynamic_failure(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled_failure = run_dynamic_failure(
        config, fsim::app::SimulationEngine::compiled);
    assert(reference_failure.find(
               "VHDL integer subtype range check failed")
           != std::string::npos);
    assert(compiled_failure.find(
               "VHDL integer subtype range check failed")
           != std::string::npos);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 1 && cold.compiled_modules == 1);
    assert(cold.cache.hits == 0 && cold.cache.misses == 1);
    assert(warm.compiled_processes == 1 && warm.compiled_modules == 1);
    assert(warm.cache.hits == 1 && warm.cache.misses == 0);
#endif
  }
  std::cout << "VHDL attribute application tests passed\n";
  return 0;
}
