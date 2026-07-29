// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
  std::array<std::string, 5> values;
  std::string local_value;
  std::string vcd;
  std::vector<std::string> specialization_keys;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-record-aggregate";
  config.project.top =
      "vhdl:work.record_aggregate_top(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {package_source, top_source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const std::filesystem::path& package_source,
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
  assert(project->design.specializations().size() == 1);
  assert(std::find(
             project->design.specializations().front()
                 .source_dependencies.begin(),
             project->design.specializations().front()
                 .source_dependencies.end(),
             package_source.string())
         != project->design.specializations().front()
                .source_dependencies.end());

  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> aggregate_local;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      const auto& local = process.debug_locals[index];
      if (local.name == "local") {
        assert(local.width == 5);
        assert(
            local.value_kind
            == fsim::runtime::simir::ValueKind::logic9);
        aggregate_local = std::pair{process.id, index};
      }
    }
  }
  assert(aggregate_local);

  Capture capture;
  capture.specialization_keys =
      project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 5> paths{
      "record_aggregate_top.source",
      "record_aggregate_top.result",
      "record_aggregate_top.conditional_result",
      "record_aggregate_top.equal_result",
      "record_aggregate_top.different_result"};
  std::array<fsim::runtime::simir::SignalId, 5> signals{};
  std::array<fsim::runtime::VcdSignal, 5> traces{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{paths[index]}, index < 3 ? 5U : 1U);
  }
  vcd.begin();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(
        traces[index], simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        const auto found =
            std::find(signals.begin(), signals.end(), signal);
        if (found == signals.end()) {
          return;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(signals.begin(), found));
        vcd.set_time(time);
        vcd.change(traces[index], value);
      });

  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  capture.local_value = simulation.read_process_local(
      aggregate_local->first,
      aggregate_local->second).to_msb_string();
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_capture(const Capture& capture) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert((
      capture.values
      == std::array<std::string, 5>{
          "10Z-0", "ULH-1", "01LH1", "1", "1"}));
  assert(capture.local_value == "10Z-0");
  assert(
      capture.vcd.find("bxxxx0")
      != std::string::npos);
  assert(
      capture.vcd.find("b10zx0")
      != std::string::npos);
  assert(
      capture.vcd.find("bx01x1")
      != std::string::npos);
  assert(
      capture.vcd.find("b01011")
      != std::string::npos);
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-record-aggregate-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto package_source =
      directory.path / "packet_types.vhd";
  const auto top_source =
      directory.path / "record_aggregate_top.vhd";

  const auto write_package =
      [&](const std::string_view revision) {
        std::ofstream output{package_source};
        output << "-- " << revision << R"(
package Packet_Types is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
end package;
)";
        assert(output.good());
      };
  write_package("revision one");
  {
    std::ofstream output{top_source};
    output << R"(
use work.packet_types.packet_t;
entity Record_Aggregate_Top is
end entity;

use work.packet_types.packet_t;
architecture rtl of record_aggregate_top is
  signal source : packet_t;
  signal result : packet_types.packet_t;
  signal conditional_result : packet_t;
  signal equal_result : boolean;
  signal different_result : boolean;
begin
  drive_source : process
    variable local : packet_t :=
      (Valid => true, Data => "ULH-");
  begin
    local := ("10Z-", false);
    source <= local;
    wait;
  end process;

  result <= (Data => "ULH-", others => true);
  conditional_result <=
    (Data => "01LH", Valid => true) when true else
    ("0000", false);
  equal_result <=
    result = (Valid => true, Data => "ULH-");
  different_result <=
    (Data => "ULH-", Valid => true) /= source;
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path,
        package_source,
        top_source,
        optimization);
    const auto reference = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    verify_capture(cold);
    verify_capture(warm);
    assert(reference.result.status == cold.result.status);
    assert(reference.result.time == cold.result.time);
    assert(reference.result.delta == cold.result.delta);
    assert(reference.values == cold.values);
    assert(reference.local_value == cold.local_value);
    assert(reference.vcd == cold.vcd);
    assert(cold.values == warm.values);
    assert(cold.local_value == warm.local_value);
    assert(cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 5);
    assert(cold.compiled_modules == 1);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 1);
    assert(cold.cache.stores == 1);
    assert(warm.compiled_processes == 5);
    assert(warm.compiled_modules == 1);
    assert(warm.cache.hits == 1);
    assert(warm.cache.misses == 0);
    assert(warm.cache.stores == 0);
#endif

    write_package("revision two");
    const auto changed = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    verify_capture(changed);
    assert(
        changed.specialization_keys
        != warm.specialization_keys);
#if defined(FSIM_HAS_LLVM)
    assert(changed.compiled_processes == 5);
    assert(changed.compiled_modules == 1);
    assert(changed.cache.hits == 0);
    assert(changed.cache.misses == 1);
    assert(changed.cache.stores == 1);
#endif
    write_package("revision one");
  }

  std::cout
      << "VHDL record aggregate application tests passed\n";
  return 0;
}
