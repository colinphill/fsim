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
#include <variant>
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
  std::string local_value;
  std::vector<std::string> resolved_drivers;
  std::vector<std::string> reports;
  std::string vcd;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-logic9";
  config.project.top = "vhdl:work.vhdl_logic9(rtl)";
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
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

constexpr std::array<std::string_view, 30> signal_names{
    "vhdl_logic9.source",
    "vhdl_logic9.inverted",
    "vhdl_logic9.anded",
    "vhdl_logic9.ored",
    "vhdl_logic9.xored",
    "vhdl_logic9.nanded",
    "vhdl_logic9.nored",
    "vhdl_logic9.xnored",
    "vhdl_logic9.extracted",
    "vhdl_logic9.concatenated",
    "vhdl_logic9.delayed",
    "vhdl_logic9.equal_result",
    "vhdl_logic9.different_result",
    "vhdl_logic9.local_result",
    "vhdl_logic9.last_value_result",
    "vhdl_logic9.resolved_value",
    "vhdl_logic9.converted_ulogic",
    "vhdl_logic9.converted_logic",
    "vhdl_logic9.rise_seen",
    "vhdl_logic9.fall_seen",
    "vhdl_logic9.bit_mapped",
    "vhdl_logic9.x01_mapped",
    "vhdl_logic9.x01z_mapped",
    "vhdl_logic9.ux01_mapped",
    "vhdl_logic9.promoted",
    "vhdl_logic9.unknown_seen",
    "vhdl_logic9.known_seen",
    "vhdl_logic9.mapped_01",
    "vhdl_logic9.scalar_bit",
    "vhdl_logic9.scalar_unknown"};

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
  const auto resolved_signal =
      project->design.find_signal("vhdl_logic9.resolved_value");
  assert(resolved_signal);
  std::vector<fsim::runtime::simir::ProcessId>
      resolved_driver_processes;
  for (const auto& process : project->design.processes()) {
    const auto writes_resolved =
        std::ranges::any_of(
            process.operations,
            [&](const auto& operation) {
              const auto writes =
                  [&](const auto* value) {
                    return value != nullptr
                        && value->signal == *resolved_signal;
                  };
              return writes(fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::WriteBlocking>(
                            &operation))
                  || writes(fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::WriteUpdate>(
                            &operation))
                  || writes(fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::WriteAfter>(
                            &operation))
                  || writes(fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::WriteProjected>(
                            &operation))
                  || writes(fsim::runtime::simir::operation_get_if<
                            fsim::runtime::simir::
                                WriteProjectedWaveform>(
                            &operation));
            });
    if (writes_resolved) {
      resolved_driver_processes.push_back(process.id);
    }
  }
  assert(resolved_driver_processes.size() == 2);
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> exact_local;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      if (process.debug_locals[index].name
          == "exact_local") {
        assert(
            process.debug_locals[index].value_kind
            == fsim::runtime::simir::ValueKind::logic9);
        exact_local = std::pair{process.id, index};
      }
    }
  }
  assert(exact_local);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};

  Capture capture;
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.native_cache =
      simulation.native_cache_statistics();
  simulation.set_report_hook(
      [&](const fsim::runtime::simir::ProcessId,
          const std::string_view message,
          const fsim::runtime::simir::AssertionSeverity,
          const fsim::runtime::simir::SourceLocation&,
          const fsim::runtime::SimulationTick,
          const std::uint64_t) {
        capture.reports.emplace_back(message);
      });

  std::array<
      fsim::runtime::simir::SignalId,
      signal_names.size()> signals{};
  std::array<
      fsim::runtime::VcdSignal,
      signal_names.size()> traces{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0;
       index < signal_names.size();
       ++index) {
    const auto signal =
        simulation.find_signal(signal_names[index]);
    assert(signal);
    signals[index] = *signal;
    const auto width =
        index == 8 ? 4U
        : index == 11 || index == 12
              || index == 18 || index == 19
              || index == 25 || index == 26
              || index == 28 || index == 29 ? 1U
        : index == 24 ? 4U
                                             : 8U;
    traces[index] = vcd.declare_signal(
        std::string{signal_names[index]}, width);
  }
  vcd.begin();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(
        traces[index],
        simulation.read_signal(signals[index]));
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
  for (const auto signal : signals) {
    capture.values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  capture.local_value = simulation.read_process_local(
      exact_local->first, exact_local->second).to_msb_string();
  for (const auto process : resolved_driver_processes) {
    capture.resolved_drivers.push_back(
        simulation.read_driver(
            process, *resolved_signal).to_msb_string());
  }
  std::ranges::sort(capture.resolved_drivers);
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_capture(const Capture& capture) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 2);
  assert((
      capture.values
      == std::vector<std::string>{
          "ULH-WZ01",
          "U10XXX10",
          "U01XXX01",
          "U01XXX01",
          "U10XXX10",
          "U10XXX10",
          "U10XXX10",
          "U01XXX01",
          "ULH-",
          "ULH-WZ01",
          "ZWLH-U01",
          "1",
          "1",
          "ULH-WZ01",
          "ULH-WZ01",
          "HLXWUXXW",
          "ULH-WZ01",
          "ULH-WZ01",
          "1",
          "1",
          "10111101",
          "X01XXX01",
          "X01XXZ01",
          "U01XXX01",
          "1010",
          "1",
          "0",
          "10111101",
          "1",
          "0"}));
  assert((capture.reports
          == std::vector<std::string>{"ULH-WZ01", "065", "AC"}));
  assert(capture.local_value == "ULH-WZ01");
  assert((
      capture.resolved_drivers
      == std::vector<std::string>{
          "HZZLZ10W", "ZL-HU01Z"}));
  assert(
      capture.vcd.find("bx01xxz01")
      != std::string::npos);
  assert(
      capture.vcd.find("bzx01xx01")
      != std::string::npos);
  assert(capture.vcd.find("#2") != std::string::npos);
}

struct MixedCapture {
  fsim::runtime::RunResult result;
  std::string value;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

fsim::project::Config make_mixed_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& sv_source,
    const std::filesystem::path& vhdl_source,
    const std::string& top,
    const fsim::project::Binding& binding,
    const std::string& cache_name,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = cache_name;
  config.project.top = top;
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (cache_name
         + (optimization == fsim::project::Optimization::o0
                ? "-o0"
                : "-o2"));
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sv_sources;
  sv_sources.language =
      fsim::project::Language::system_verilog;
  sv_sources.standard = "2017";
  sv_sources.library = "work";
  sv_sources.files.push_back(sv_source);
  config.source_sets.push_back(std::move(sv_sources));

  fsim::project::SourceSet vhdl_sources;
  vhdl_sources.language = fsim::project::Language::vhdl;
  vhdl_sources.standard = "2008";
  vhdl_sources.library = "work";
  vhdl_sources.files.push_back(vhdl_source);
  config.source_sets.push_back(std::move(vhdl_sources));
  config.bindings.push_back(binding);
  return config;
}

MixedCapture run_mixed(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::string_view signal_path) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  const auto signal = project->design.find_signal(signal_path);
  assert(signal);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  MixedCapture capture;
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.result = simulation.run();
  capture.value =
      simulation.read_signal(*signal).to_msb_string();
  return capture;
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-logic9-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "logic9.vhd";
  {
    std::ofstream output{source};
    output << R"(
library ieee;
use ieee.std_logic_1164.all;

entity vhdl_logic9 is
  port (
    source : out std_logic_vector(7 downto 0);
    inverted : out std_logic_vector(7 downto 0);
    anded : out std_logic_vector(7 downto 0);
    ored : out std_logic_vector(7 downto 0);
    xored : out std_logic_vector(7 downto 0);
    nanded : out std_logic_vector(7 downto 0);
    nored : out std_logic_vector(7 downto 0);
    xnored : out std_logic_vector(7 downto 0);
    extracted : out std_logic_vector(3 downto 0);
    concatenated : out std_logic_vector(7 downto 0);
    delayed : out std_logic_vector(7 downto 0);
    equal_result : out boolean;
    different_result : out boolean;
    local_result : out std_logic_vector(7 downto 0);
    last_value_result : out std_logic_vector(7 downto 0);
    resolved_value : out std_logic_vector(7 downto 0);
    converted_ulogic : out std_ulogic_vector(7 downto 0);
    converted_logic : out std_logic_vector(7 downto 0);
    rise_seen : out std_logic;
    fall_seen : out std_logic;
    bit_mapped : out bit_vector(7 downto 0);
    x01_mapped : out std_logic_vector(7 downto 0);
    x01z_mapped : out std_logic_vector(7 downto 0);
    ux01_mapped : out std_logic_vector(7 downto 0);
    promoted : out std_logic_vector(3 downto 0);
    unknown_seen : out boolean;
    known_seen : out boolean;
    mapped_01 : out std_logic_vector(7 downto 0);
    scalar_bit : out bit;
    scalar_unknown : out boolean
  );
end entity;

architecture rtl of vhdl_logic9 is
  signal attribute_source : std_logic_vector(7 downto 0);
  signal edge_clock : std_logic;
begin
  source <= "ULH-WZ01";
  inverted <= not source;
  anded <= source and "11111111";
  ored <= source or "00000000";
  xored <= source xor "11111111";
  nanded <= source nand "11111111";
  nored <= source nor "00000000";
  xnored <= source xnor "11111111";
  extracted <= source(7 downto 4);
  concatenated <= source(7 downto 4) & source(3 downto 0);
  delayed <= transport "ZWLH-U01" after 2 ns;
  equal_result <= true when source = "ULH-WZ01" else false;
  different_result <= true when source /= "ULH-WZ00" else false;
  local_copy : process
    variable exact_local :
      std_logic_vector(7 downto 0) := "ULH-WZ01";
  begin
    local_result <= exact_local;
    wait;
  end process;
  attribute_source <= transport
    "ULH-WZ01" after 1 ns,
    "ZWLH-U01" after 2 ns;
  last_value_result <= attribute_source'last_value;
  resolved_value <= "ZL-HU01Z";
  resolved_value <= "HZZLZ10W";
  converted_ulogic <= std_ulogic_vector(source);
  converted_logic <= std_logic_vector(std_ulogic_vector(source));
  bit_mapped <= to_bitvector(source, '1');
  x01_mapped <= to_x01(source);
  x01z_mapped <= to_x01z(source);
  ux01_mapped <= to_ux01(source);
  promoted <= to_stdlogicvector("1010");
  unknown_seen <= is_x(source);
  known_seen <= is_x("01LH");
  mapped_01 <= to_01(source, '1');
  scalar_bit <= to_bit('U', '1');
  scalar_unknown <= is_x('H');
  edge_clock <= transport '0', '1' after 1 ns, '0' after 2 ns;
  rising_probe : process(edge_clock)
  begin
    if rising_edge(edge_clock) then
      rise_seen <= '1';
    end if;
  end process;
  falling_probe : process(edge_clock)
  begin
    if falling_edge(edge_clock) then
      fall_seen <= '1';
    end if;
  end process;
  string_probe : process
  begin
    report to_string("ULH-WZ01");
    report to_ostring("00110101");
    report to_hstring("10101100");
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  {
    const auto config = make_config(
        directory.path,
        source,
        fsim::project::Optimization::o0);
    fsim::diagnostic::Engine diagnostics;
    const auto checked = fsim::app::check_project(config, diagnostics);
    assert(checked);
    assert(!diagnostics.has_error());
    assert(checked->source_count == 1);
    assert(checked->hdl_sources.size() == 1);
    assert(checked->standard_sources.size() == 2);
    assert(
        checked->standard_sources[0].content_digest
        == "2a34c7d7b2c8ba21b1e91153741399cf2cd23c8b04028dcf53765efeea76de55");
    assert(
        checked->standard_sources[1].content_digest
        == "6534fe4842c1133199db93725e36a9e973ea8e2ab03890433c013af813d5ce2c");
    assert(
        checked->standard_sources[0].path.filename()
        == "std_logic_1164.vhdl");
    assert(
        checked->standard_sources[1].path.filename()
        == "std_logic_1164-body.vhdl");
    const auto package = std::ranges::find_if(
        checked->parsed.units,
        [](const fsim::frontend::DesignUnit& unit) {
          return unit.kind == fsim::frontend::UnitKind::VhdlPackage
              && unit.library == "ieee"
              && unit.name == "std_logic_1164"
              && unit.primary_name.empty();
        });
    assert(package != checked->parsed.units.end());
    assert(package->type_aliases.empty());
    assert(
        package->standard_package_revision
        == "ieee-p1076:1076-2019:16a012320947d378611cc7457f64ed76cb52bac4");
    assert(std::ranges::find(
               package->standard_package_declarations, "std_ulogic")
           != package->standard_package_declarations.end());
    assert(std::ranges::find(
               package->standard_package_declarations, "std_logic_vector")
           != package->standard_package_declarations.end());
    assert(std::ranges::find(
               package->standard_package_declarations, "resolved")
           != package->standard_package_declarations.end());
    assert(std::ranges::find(
               package->standard_package_declarations, "rising_edge")
           != package->standard_package_declarations.end());
    assert(std::ranges::any_of(
        checked->parsed.units,
        [](const fsim::frontend::DesignUnit& unit) {
          return unit.kind == fsim::frontend::UnitKind::VhdlPackage
              && unit.library == "ieee"
              && unit.name == "std_logic_1164"
              && !unit.primary_name.empty();
        }));
  }

  const auto conflict_source =
      directory.path / "std_logic_1164_conflict.vhd";
  {
    std::ofstream output{conflict_source};
    output << R"(
library ieee;
use ieee.std_logic_1164.all;
package std_logic_1164 is
end package std_logic_1164;
)";
    assert(output.good());
  }
  {
    auto config = make_config(
        directory.path,
        conflict_source,
        fsim::project::Optimization::o0);
    config.source_sets.front().library = "ieee";
    fsim::diagnostic::Engine diagnostics;
    const auto checked = fsim::app::check_project(config, diagnostics);
    assert(!checked);
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [](const fsim::diagnostic::Diagnostic& diagnostic) {
          return diagnostic.code == "FSIM-FE-VHSTD-004";
        }));
  }

  const auto textio_source = directory.path / "std_logic_textio_use.vhd";
  {
    std::ofstream output{textio_source};
    output << R"(
library ieee;
use ieee.std_logic_textio.all;
entity std_logic_textio_use is
end entity;
)";
    assert(output.good());
  }
  {
    const auto config = make_config(
        directory.path,
        textio_source,
        fsim::project::Optimization::o0);
    fsim::diagnostic::Engine diagnostics;
    const auto checked = fsim::app::check_project(config, diagnostics);
    assert(checked);
    assert(!diagnostics.has_error());
    assert(checked->standard_sources.size() == 3);
    assert(std::ranges::any_of(
        checked->standard_sources,
        [](const fsim::app::CheckedSource& candidate) {
          return candidate.content_digest
              == "526a2e1e0a05f35ae97fb046ec90ebe8250324390aab2f3adccde73e605e3937";
        }));
    assert(std::ranges::any_of(
        checked->parsed.units,
        [](const fsim::frontend::DesignUnit& unit) {
          return unit.kind == fsim::frontend::UnitKind::VhdlPackage
              && unit.library == "ieee"
              && unit.name == "std_logic_textio";
        }));
  }

  const auto dynamic_string_source =
      directory.path / "dynamic_logic_string.vhd";
  {
    std::ofstream output{dynamic_string_source};
    output << R"(
library ieee;
use ieee.std_logic_1164.all;
entity dynamic_logic_string is
end entity;
architecture rtl of dynamic_logic_string is
  signal value : std_logic_vector(3 downto 0);
begin
  value <= "1010";
  probe : process(value)
  begin
    report to_hstring(value);
  end process;
end architecture;
)";
    assert(output.good());
  }
  {
    auto config = make_config(
        directory.path,
        dynamic_string_source,
        fsim::project::Optimization::o0);
    config.project.top = "vhdl:work.dynamic_logic_string(rtl)";
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project);
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [](const fsim::diagnostic::Diagnostic& diagnostic) {
          return diagnostic.code == "FSIM-ELAB-VHLOGIC-003";
        }));
  }

  const auto sv_parent_source =
      directory.path / "sv_parent.sv";
  {
    std::ofstream output{sv_parent_source};
    output << R"(
module sv_logic9_parent;
  logic [7:0] result;
  vhdl_logic9_child child(.result(result));
endmodule
)";
    assert(output.good());
  }
  const auto vhdl_child_source =
      directory.path / "vhdl_child.vhd";
  {
    std::ofstream output{vhdl_child_source};
    output << R"(
entity vhdl_logic9_child is
  port (result : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of vhdl_logic9_child is
begin
  result <= "ULH-WZ01";
end architecture;
)";
    assert(output.good());
  }

  const auto vhdl_parent_source =
      directory.path / "vhdl_parent.vhd";
  {
    std::ofstream output{vhdl_parent_source};
    output << R"(
entity vhdl_logic9_parent is
  port (result : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of vhdl_logic9_parent is
  signal source : std_logic_vector(7 downto 0);
begin
  source <= "ULH-WZ01";
  child : entity work.sv_logic9_child
    port map (data => source, result => result);
end architecture;
)";
    assert(output.good());
  }
  const auto sv_child_source =
      directory.path / "sv_child.sv";
  {
    std::ofstream output{sv_child_source};
    output << R"(
module sv_logic9_child(
  input logic [7:0] data,
  output logic [7:0] result
);
  assign result = data;
endmodule
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config =
        make_config(directory.path, source, optimization);
    const auto reference =
        run_once(
            config, fsim::app::SimulationEngine::interpreter);
    const auto compiled =
        run_once(
            config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    verify_capture(compiled);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert(reference.local_value == compiled.local_value);
    assert(reference.reports == compiled.reports);
    assert(
        reference.resolved_drivers
        == compiled.resolved_drivers);
    assert(reference.vcd == compiled.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes > 0);
    assert(compiled.compiled_modules > 0);
    assert(
        compiled.compiled_modules
        <= compiled.compiled_processes);
    assert(compiled.native_cache.hits == 0);
    assert(
        compiled.native_cache.misses
        == compiled.compiled_modules);
    assert(
        compiled.native_cache.stores
        == compiled.compiled_modules);
#endif

    const auto sv_parent_config = make_mixed_config(
        directory.path,
        sv_parent_source,
        vhdl_child_source,
        "sv:work.sv_logic9_parent",
        {
            "sv_logic9_parent.child",
            "vhdl:work.vhdl_logic9_child(rtl)",
            std::nullopt},
        "sv-parent-logic9",
        optimization);
    const auto sv_parent_reference = run_mixed(
        sv_parent_config,
        fsim::app::SimulationEngine::interpreter,
        "sv_logic9_parent.result");
    const auto sv_parent_compiled = run_mixed(
        sv_parent_config,
        fsim::app::SimulationEngine::compiled,
        "sv_logic9_parent.result");
    assert(
        sv_parent_reference.result.status
        == fsim::runtime::RunStatus::completed);
    assert(sv_parent_reference.value == "X01XXZ01");
    assert(
        sv_parent_reference.value
        == sv_parent_compiled.value);

    const auto vhdl_parent_config = make_mixed_config(
        directory.path,
        sv_child_source,
        vhdl_parent_source,
        "vhdl:work.vhdl_logic9_parent(rtl)",
        {
            "vhdl_logic9_parent.child",
            "sv:work.sv_logic9_child",
            std::nullopt},
        "vhdl-parent-logic9",
        optimization);
    const auto vhdl_parent_reference = run_mixed(
        vhdl_parent_config,
        fsim::app::SimulationEngine::interpreter,
        "vhdl_logic9_parent.result");
    const auto vhdl_parent_compiled = run_mixed(
        vhdl_parent_config,
        fsim::app::SimulationEngine::compiled,
        "vhdl_logic9_parent.result");
    assert(
        vhdl_parent_reference.result.status
        == fsim::runtime::RunStatus::completed);
    assert(vhdl_parent_reference.value == "X01XXZ01");
    assert(
        vhdl_parent_reference.value
        == vhdl_parent_compiled.value);
#if defined(FSIM_HAS_LLVM)
    assert(sv_parent_compiled.compiled_processes > 0);
    assert(sv_parent_compiled.compiled_modules > 0);
    assert(vhdl_parent_compiled.compiled_processes > 0);
    assert(vhdl_parent_compiled.compiled_modules > 0);
#endif
  }

  std::cout << "VHDL Logic9 application tests passed\n";
  return 0;
}
