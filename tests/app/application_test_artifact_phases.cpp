// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/support/path.hpp"
#include "fsim/version.hpp"

#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_artifact_phase_semantics() {
  const auto sv_source = directory / "artifact_phase.sv";
  const auto vhdl_source = directory / "artifact_phase.vhd";
  const auto sv_object = directory / "artifact-sv.fsimobj";
  const auto vhdl_object = directory / "artifact-vhdl.fsimobj";
  const auto design = directory / "artifact-mixed.fsimdesign";
  const auto trace = directory / "artifact-mixed.vcd";
  {
    std::ofstream output(sv_source);
    output << R"(
module phase_child #(
  parameter logic [7:0] MASK = 8'hff
) (
  input logic clk,
  input logic [7:0] value,
  output logic [7:0] transformed
);
  reg notifier;
  specify
    (value => transformed) = 0;
    $setup(posedge value[0], posedge clk, 0, notifier);
  endspecify
  assign transformed = value ^ MASK;
endmodule

module phase_tb;
  logic clk;
  logic reset;
  logic [7:0] counter_q;
  logic [7:0] transformed;
  phase_counter counter (
    .clk(clk), .reset(reset), .q(counter_q));
  phase_child #(.MASK(8'h0f)) child (
    .clk(clk), .value(counter_q), .transformed(transformed));
  initial begin
    clk = 1'b0;
    reset = 1'b1;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 reset = 1'b0;
    #1 clk = 1'b1;
    #1 clk = 1'b0;
    #1 $finish;
  end
endmodule

module phase_watch;
  logic watched;
  initial begin
    watched = 1'b0;
    #2 watched = 1'b1;
  end
endmodule
)";
  }
  {
    std::ofstream output(vhdl_source);
    output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package phase_values is
  constant reset_value : integer := 0;
end package;

context phase_context is
  library ieee;
  use ieee.std_logic_1164.all;
  use ieee.numeric_std.all;
  use ieee.vital_timing.all;
  use ieee.vital_primitives.all;
  library work;
  use work.phase_values.all;
end context;

context work.phase_context;
entity phase_counter is
  generic (step : natural := 1);
  port (
    clk : in std_logic;
    reset : in std_logic;
    q : out unsigned(7 downto 0));
end entity;

architecture rtl of phase_counter is
  signal attribute_source : std_logic;
  signal stable_probe : boolean;
  signal vital_probe : std_logic;
begin
  vital_probe <= VitalMUX2('H', '1', 'X');
  process (clk)
  begin
    if rising_edge(clk) then
      if reset = '1' then
        q <= "00000000";
      else
        q <= q + step;
      end if;
    end if;
  end process;
  attribute_stimulus: process
  begin
    attribute_source <= '0';
    wait for 1 ns;
    attribute_source <= '1';
    wait;
  end process;
  attribute_probe: process
  begin
    wait for 3 ns;
    wait for 0 ns;
    stable_probe <= attribute_source'stable(1 ns);
    wait;
  end process;
end architecture;

configuration phase_counter_configuration of phase_counter is
  for rtl
  end for;
end configuration;

entity phase_watch is
end entity;
architecture rtl of phase_watch is
begin
end architecture;
)";
  }

  const auto sv_source_text = support::path_to_utf8(sv_source);
  const auto vhdl_source_text = support::path_to_utf8(vhdl_source);
  const auto sv_object_text = support::path_to_utf8(sv_object);
  const auto vhdl_object_text = support::path_to_utf8(vhdl_object);
  const auto design_text = support::path_to_utf8(design);
  const auto trace_text = support::path_to_utf8(trace);
  auto services = app::make_cli_services();
  std::ostringstream output;
  std::ostringstream error;

  const std::vector<const char*> vhdl_compile{
      "fsim", "compile", "--lang", "vhdl", "--standard", "2008",
      "--library", "work", "--output", vhdl_object_text.c_str(),
      vhdl_source_text.c_str()};
  const auto vhdl_result = cli::run(
      static_cast<int>(vhdl_compile.size()), vhdl_compile.data(), services,
      output, error);
  if (vhdl_result != 0) {
    std::cerr << error.str();
  }
  assert(vhdl_result == 0);
  assert(error.str().empty());
  diagnostic::Engine vhdl_inspection_diagnostics;
  const auto vhdl_inspection = app::inspect_artifact(
      vhdl_object, vhdl_inspection_diagnostics);
  assert(vhdl_inspection && !vhdl_inspection_diagnostics.has_error());
  assert(vhdl_inspection->phase == app::ArtifactPhaseKind::compilation);
  assert(vhdl_inspection->language == "vhdl");
  assert(vhdl_inspection->library == "work");
  assert(vhdl_inspection->units.size() >= 5);
  output.str({});
  error.str({});
  const std::vector<const char*> sv_compile{
      "fsim", "compile", "--lang", "systemverilog", "--standard", "2017",
      "--library", "work", "--output", sv_object_text.c_str(),
      sv_source_text.c_str()};
  assert(cli::run(
      static_cast<int>(sv_compile.size()), sv_compile.data(), services,
      output, error) == 0);
  assert(error.str().empty());

  output.str({});
  error.str({});
  const std::vector<const char*> elaborate{
      "fsim", "elaborate", "--object", vhdl_object_text.c_str(),
      "--object", sv_object_text.c_str(), "--top",
      "main=sv:work.phase_tb", "--top", "observer=sv:work.phase_watch",
      "--output", design_text.c_str(), "--seed", "23"};
  const auto elaborate_result = cli::run(
      static_cast<int>(elaborate.size()), elaborate.data(), services,
      output, error);
  if (elaborate_result != 0) {
    std::cerr << error.str();
  }
  assert(elaborate_result == 0);
  assert(error.str().empty());
  diagnostic::Engine design_inspection_diagnostics;
  const auto design_inspection = app::inspect_artifact(
      design, design_inspection_diagnostics);
  assert(design_inspection && !design_inspection_diagnostics.has_error());
  assert(design_inspection->phase == app::ArtifactPhaseKind::elaboration);
  assert(design_inspection->compatible);
  assert(design_inspection->runtime_abi == runtime_abi_version);
  assert(design_inspection->roots
      == std::vector<std::string>({"main", "observer"}));
  assert(design_inspection->process_count != 0);
  auto active_design = design;

  const auto missing_design = directory / "artifact-missing.fsimdesign";
  const auto missing_design_text = support::path_to_utf8(missing_design);
  const std::vector<const char*> missing_elaborate{
      "fsim", "elaborate", "--object", sv_object_text.c_str(), "--top",
      "missing=sv:work.absent", "--output", missing_design_text.c_str()};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(missing_elaborate.size()), missing_elaborate.data(),
      services, output, error) != 0);
  assert(error.str().find("absent") != std::string::npos);
  assert(!std::filesystem::exists(missing_design));

  const auto ambiguous_design = directory / "artifact-ambiguous.fsimdesign";
  const auto ambiguous_design_text = support::path_to_utf8(ambiguous_design);
  const std::vector<const char*> ambiguous_elaborate{
      "fsim", "elaborate", "--object", vhdl_object_text.c_str(),
      "--object", sv_object_text.c_str(), "--top",
      "collision=phase_watch", "--output", ambiguous_design_text.c_str()};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(ambiguous_elaborate.size()),
      ambiguous_elaborate.data(), services, output, error) != 0);
  assert(error.str().find("ambiguous") != std::string::npos);
  assert(!std::filesystem::exists(ambiguous_design));

  const auto run_engine = [&](const app::SimulationEngine engine) {
    diagnostic::Engine diagnostics;
    auto built = app::load_design_artifact(active_design, diagnostics);
    assert(built && !diagnostics.has_error());
    assert(built->design.roots()
        == std::vector<std::string>({"main", "observer"}));
    assert(built->semantics.source_files().size() >= 2);
    assert(built->design.verilog_specify_paths().size() == 1);
    assert(built->design.verilog_timing_checks().size() == 1);
    for (const auto& file : built->semantics.source_files()) {
      assert(!std::filesystem::path(file.physical_name).is_absolute());
    }
    built->cache_path = directory / "artifact-phase-cache";
    std::filesystem::create_directories(built->cache_path);
    app::Simulation simulation{std::move(*built), 1000, engine};
    const auto counter = simulation.find_signal("main.counter_q");
    const auto watch = simulation.find_signal("observer.watched");
    const auto stable_probe =
        simulation.find_signal("main.counter.stable_probe");
    const auto vital_probe =
        simulation.find_signal("main.counter.vital_probe");
    assert(counter && watch && stable_probe && vital_probe);
    std::size_t callbacks{};
    simulation.set_signal_change_hook(
        [&](runtime::simir::SignalId, const runtime::PackedLogic4&,
            runtime::SimulationTick, std::uint64_t) { ++callbacks; });
    const auto result = simulation.run();
    assert(result.status == runtime::RunStatus::stopped);
    assert(result.time == 6);
    assert(callbacks != 0);
    assert(simulation.read_signal(*stable_probe).to_msb_string() == "1");
    assert(simulation.read_signal(*vital_probe).to_msb_string() == "1");
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    app::DebuggerControl debugger{
        simulation, debugger_output, debugger_error};
    debugger.execute({
        "show", "main.counter.attribute_source'stable(1)"});
    assert(debugger_error.str().empty());
    assert(debugger_output.str().find("1") != std::string::npos);
    return std::pair{
        simulation.read_signal(*counter).to_msb_string(),
        simulation.read_signal(*watch).to_msb_string()};
  };
  const auto interpreted = run_engine(app::SimulationEngine::interpreter);
  const auto compiled = run_engine(app::SimulationEngine::compiled);
  assert(interpreted == compiled);
  assert(interpreted.first == "00000001");
  assert(interpreted.second == "1");

  const auto relocated_design = directory / "relocated.fsimdesign";
  std::filesystem::rename(active_design, relocated_design);
  active_design = relocated_design;
  const auto relocated = run_engine(app::SimulationEngine::interpreter);
  assert(relocated == interpreted);

  output.str({});
  error.str({});
  const auto active_design_text = support::path_to_utf8(active_design);
  const std::vector<const char*> simulate{
      "fsim", "simulate", "--design", active_design_text.c_str(), "--engine",
      "compiled", "--trace", trace_text.c_str(), "--trace-filter",
      "main.*", "--trace-filter", "observer.*"};
  assert(cli::run(
      static_cast<int>(simulate.size()), simulate.data(), services,
      output, error) == 0);
  assert(error.str().empty());
  const auto trace_bytes = [&] {
    std::ifstream input(trace, std::ios::binary);
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
  }();
  assert(trace_bytes.find("main") != std::string::npos);
  assert(trace_bytes.find("observer") != std::string::npos);
  assert(trace_bytes.find("stable_probe") != std::string::npos);
  assert(trace_bytes.find("vital_probe") != std::string::npos);
  assert(
      trace_bytes.find("attribute_source'stable(1)")
      != std::string::npos);

  const auto systemc_phase_source = directory / "artifact_phase.cpp";
  const auto systemc_phase_object = directory / "artifact-systemc.fsimobj";
  {
    std::ofstream systemc_output(systemc_phase_source);
    systemc_output << "SC_MODULE(ArtifactPhase) {};\n";
  }
  const auto systemc_source_text = support::path_to_utf8(systemc_phase_source);
  const auto systemc_object_text = support::path_to_utf8(systemc_phase_object);
  const std::vector<const char*> systemc_compile{
      "fsim", "compile", "--lang", "systemc", "--standard", "2023",
      "--library", "work", "--output", systemc_object_text.c_str(),
      systemc_source_text.c_str()};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(systemc_compile.size()), systemc_compile.data(),
      services, output, error) != 0);
  assert(error.str().find("Batch 138") != std::string::npos);
  assert(!std::filesystem::exists(systemc_phase_object));

  project::Config api_compile_config;
  api_compile_config.manifest_path = "<artifact-api>";
  api_compile_config.base_directory = directory;
  api_compile_config.project.name = "artifact-api-compile";
  project::SourceSet api_sources;
  api_sources.language = project::Language::system_verilog;
  api_sources.standard = "2017";
  api_sources.library = "work";
  api_sources.compilation_unit = "source-set";
  api_sources.file_patterns = {sv_source};
  api_sources.files = {sv_source};
  api_compile_config.source_sets.push_back(std::move(api_sources));
  const auto api_object = directory / "artifact-api.fsimobj";
  diagnostic::Engine api_compile_diagnostics;
  assert(app::compile_artifact(
      api_compile_config, api_object, api_compile_diagnostics));
  assert(!api_compile_diagnostics.has_error());

  project::Config api_elaborate_config;
  api_elaborate_config.manifest_path = "<artifact-api>";
  api_elaborate_config.base_directory = directory;
  api_elaborate_config.project.name = "artifact-api-elaborate";
  api_elaborate_config.project.tops.push_back(
      {"sv:work.phase_watch", "api"});
  api_elaborate_config.project.time_resolution = "1ns";
  api_elaborate_config.build.cache_path = directory / "artifact-api-cache";
  const auto api_design = directory / "artifact-api.fsimdesign";
  const std::array api_objects{api_object};
  diagnostic::Engine api_elaborate_diagnostics;
  assert(app::elaborate_artifact(
      api_elaborate_config, api_objects, api_design,
      api_elaborate_diagnostics));
  assert(!api_elaborate_diagnostics.has_error());
  diagnostic::Engine api_load_diagnostics;
  const auto api_loaded = app::load_design_artifact(
      api_design, api_load_diagnostics);
  assert(api_loaded && !api_load_diagnostics.has_error());
  assert(api_loaded->design.roots() == std::vector<std::string>{"api"});

  const auto verilog_source = directory / "artifact_phase.v";
  const auto verilog_object = directory / "artifact-verilog.fsimobj";
  const auto verilog_design = directory / "artifact-verilog.fsimdesign";
  {
    std::ofstream verilog_output(verilog_source);
    verilog_output << R"(
module legacy_phase;
  reg value;
  initial begin
    value = 1'b0;
    #1 value = 1'b1;
    #1 $finish;
  end
endmodule
)";
  }
  const auto verilog_source_text = support::path_to_utf8(verilog_source);
  const auto verilog_object_text = support::path_to_utf8(verilog_object);
  const auto verilog_design_text = support::path_to_utf8(verilog_design);
  const std::vector<const char*> verilog_compile{
      "fsim", "compile", "--lang", "verilog", "--standard", "2005",
      "--library", "work", "--output", verilog_object_text.c_str(),
      verilog_source_text.c_str()};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(verilog_compile.size()), verilog_compile.data(),
      services, output, error) == 0);
  assert(error.str().empty());
  const std::vector<const char*> verilog_elaborate{
      "fsim", "elaborate", "--object", verilog_object_text.c_str(),
      "--top", "legacy=verilog:work.legacy_phase", "--output",
      verilog_design_text.c_str()};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(verilog_elaborate.size()), verilog_elaborate.data(),
      services, output, error) == 0);
  assert(error.str().empty());
  const std::vector<const char*> verilog_simulate{
      "fsim", "simulate", "--design", verilog_design_text.c_str(),
      "--engine", "interpreter"};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(verilog_simulate.size()), verilog_simulate.data(),
      services, output, error) == 0);
  assert(error.str().empty());
  assert(output.str().find("simulation stopped at tick 2")
      != std::string::npos);
}

}  // namespace fsim::test
