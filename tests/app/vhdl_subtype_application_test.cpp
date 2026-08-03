// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "path_test_support.hpp"

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
  std::array<std::string, 14> values;
  std::string count_local;
  std::string packet_local;
  std::string vcd;
  std::vector<std::string> specialization_keys;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_source,
    const std::filesystem::path& child_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-subtype";
  config.project.top = "vhdl:work.subtype_top(rtl)";
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
  sources.files = {
      package_source, child_source, top_source};
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
  assert(project->design.specializations().size() == 3);
  for (const auto& specialization :
       project->design.specializations()) {
    assert(fsim::test::has_source_dependency(
        specialization.source_dependencies, package_source));
  }

  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> count_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> packet_local;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      const auto& local = process.debug_locals[index];
      if (local.name == "local_count") {
        assert(local.width == 32);
        assert(
            local.value_kind
            == fsim::runtime::simir::ValueKind::logic4);
        assert(
            local.integer_lower
            && *local.integer_lower == 4
            && local.integer_upper
            && *local.integer_upper == 7);
        count_local = std::pair{process.id, index};
      } else if (local.name == "local_packet") {
        assert(local.width == 5);
        assert(
            local.value_kind
            == fsim::runtime::simir::ValueKind::logic9);
        packet_local = std::pair{process.id, index};
      }
    }
  }
  assert(count_local && packet_local);

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

  constexpr std::array<std::string_view, 14> paths{
      "subtype_top.source",
      "subtype_top.result",
      "subtype_top.direct_value",
      "subtype_top.wide_source",
      "subtype_top.wide_result",
      "subtype_top.count",
      "subtype_top.count_result",
      "subtype_top.wide_count",
      "subtype_top.wide_count_result",
      "subtype_top.flag",
      "subtype_top.level",
      "subtype_top.signed_value",
      "subtype_top.packet",
      "subtype_top.packet_equal"};
  constexpr std::array<std::size_t, 14> widths{
      4, 4, 4, 6, 6, 32, 32, 32, 32, 1, 1, 4, 5, 1};
  std::array<fsim::runtime::simir::SignalId, 14> signals{};
  std::array<fsim::runtime::VcdSignal, 14> traces{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{paths[index]}, widths[index]);
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
  capture.count_local = simulation.read_process_local(
      count_local->first, count_local->second).to_msb_string();
  capture.packet_local = simulation.read_process_local(
      packet_local->first, packet_local->second).to_msb_string();
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_capture(const Capture& capture) {
  constexpr std::string_view count_five =
      "00000000000000000000000000000101";
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert((
      capture.values
      == std::array<std::string, 14>{
          "10Z-",
          "10Z-",
          "01LH",
          "10Z-01",
          "10Z-01",
          std::string{count_five},
          std::string{count_five},
          std::string{count_five},
          std::string{count_five},
          "1",
          "H",
          "1010",
          "ULH-1",
          "1"}));
  assert(capture.count_local == count_five);
  assert(capture.packet_local == "ULH-1");
  assert(
      capture.vcd.find("b10zx")
      != std::string::npos);
  assert(
      capture.vcd.find("bx01x1")
      != std::string::npos);
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-subtype-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto package_source =
      directory.path / "subtype_types.vhd";
  const auto child_source =
      directory.path / "subtype_child.vhd";
  const auto top_source =
      directory.path / "subtype_top.vhd";

  const auto write_package =
      [&](const std::string_view revision) {
        std::ofstream output{package_source};
        output << "-- " << revision << R"(
package Subtype_Types is
  subtype Word_T is std_logic_vector(3 downto 0);
  subtype Signed_Word_T is signed(3 downto 0);
  subtype Flag_T is boolean;
  subtype Level_T is std_logic;
  subtype Count_Base_T is natural range 0 to 15;
  subtype Count_T is Count_Base_T range 2 to 9;
  constant Default_Count : Count_T := 5;
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  subtype Packet_Alias_T is Packet_T;
end package;
)";
        assert(output.good());
      };
  write_package("revision one");
  {
    std::ofstream output{child_source};
    output << R"(
use work.subtype_types.all;
entity Subtype_Child is
  generic (
    Width : natural := 4;
    Initial_Count : Count_T := Default_Count
  );
  port (
    Source : in std_logic_vector(Width - 1 downto 0);
    Result : out std_logic_vector(Width - 1 downto 0);
    Count_In : in Count_T;
    Count_Out : out Count_T
  );
  subtype Internal_Word_T is
    std_logic_vector(Width - 1 downto 0);
  subtype Internal_Count_T is Count_T range 3 to 8;
end entity;

use work.subtype_types.all;
architecture rtl of subtype_child is
  signal Internal_Value : Internal_Word_T;
  signal Internal_Count : Internal_Count_T;
begin
  internal_value <= source;
  result <= internal_value;
  internal_count <= count_in;
  count_out <= internal_count;
end architecture;
)";
    assert(output.good());
  }
  {
    std::ofstream output{top_source};
    output << R"(
use work.subtype_types.all;
entity Subtype_Top is
end entity;

use work.subtype_types.all;
architecture rtl of subtype_top is
  subtype Actual_Count_T is Count_T range 4 to 7;
  subtype Wide_Word_T is std_logic_vector(5 downto 0);
  subtype Local_Packet_T is Packet_Alias_T;
  signal source : Word_T;
  signal result : Word_T;
  signal direct_value : work.subtype_types.word_t;
  signal wide_source : Wide_Word_T;
  signal wide_result : Wide_Word_T;
  signal count : Actual_Count_T;
  signal count_result : Count_T;
  signal wide_count : Actual_Count_T;
  signal wide_count_result : Count_T;
  signal flag : Flag_T;
  signal level : Level_T;
  signal signed_value : Signed_Word_T;
  signal packet : Local_Packet_T;
  signal packet_equal : boolean;
begin
  drive : process
    variable local_count : Actual_Count_T := Default_Count;
    variable local_packet : Local_Packet_T :=
      (Valid => true, Data => "ULH-");
  begin
    source <= "10Z-";
    direct_value <= "01LH";
    wide_source <= "10Z-01";
    count <= local_count;
    wide_count <= local_count;
    flag <= true;
    level <= 'H';
    signed_value <= "1010";
    packet <= local_packet;
    wait;
  end process;

  child : entity work.subtype_child(rtl)
    generic map (
      Width => 4,
      Initial_Count => Default_Count
    )
    port map (
      Source => source,
      Result => result,
      Count_In => count,
      Count_Out => count_result
    );

  wide_child : entity work.subtype_child(rtl)
    generic map (
      Width => 6,
      Initial_Count => Default_Count
    )
    port map (
      Source => wide_source,
      Result => wide_result,
      Count_In => wide_count,
      Count_Out => wide_count_result
    );

  packet_equal <=
    packet = (Data => "ULH-", Valid => true);
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
        child_source,
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
    assert(reference.count_local == cold.count_local);
    assert(reference.packet_local == cold.packet_local);
    assert(reference.vcd == cold.vcd);
    assert(cold.values == warm.values);
    assert(cold.count_local == warm.count_local);
    assert(cold.packet_local == warm.packet_local);
    assert(cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 10);
    assert(cold.compiled_modules == 3);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 3);
    assert(cold.cache.stores == 3);
    assert(warm.compiled_processes == 10);
    assert(warm.compiled_modules == 3);
    assert(warm.cache.hits == 3);
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
    assert(changed.compiled_processes == 10);
    assert(changed.compiled_modules == 3);
    assert(changed.cache.hits == 0);
    assert(changed.cache.misses == 3);
    assert(changed.cache.stores == 3);
#endif
    write_package("revision one");
  }

  std::cout << "VHDL subtype application tests passed\n";
  return 0;
}
