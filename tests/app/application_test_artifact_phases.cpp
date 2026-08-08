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
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_artifact_phase_semantics() {
  static_assert(app::kRuntimeStateSchema == 18);
  static_assert(app::kSemanticStateSchema == 2);
  static_assert(app::kDesignIrStateSchema == 2);
  static_assert(app::kClassStateSchema == 9);
  static_assert(app::kSystemVerilogConstraintHirStateSchema == 3);
  static_assert(app::kSystemVerilogCoverageStateSchema == 1);
  static_assert(app::kSystemVerilogUvmStateSchema == 1);
  const auto sv_source = directory / "artifact_phase.sv";
  const auto vhdl_source = directory / "artifact_phase.vhd";
  const auto sv_object = directory / "artifact-sv.fsimobj";
  const auto vhdl_object = directory / "artifact-vhdl.fsimobj";
  const auto design = directory / "artifact-mixed.fsimdesign";
  const auto trace = directory / "artifact-mixed.vcd";
  {
    std::ofstream output(sv_source);
    output << R"(
package artifact_class_pkg;
  class process_box;
    process current;
    extern function bit same_process(process observed);
  endclass

  function bit process_box::same_process(process observed);
    return observed == current;
  endfunction
endpackage

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

module scalar_artifact;
  localparam logic [136:0] WIDE_SEED =
      {1'b1, 62'b0, 1'b1, 69'b0, 4'b1000};
  typedef enum logic [136:0] {
    WIDE_ZERO = 137'b0,
    WIDE_ENUM = {1'b1, 62'b0, 1'b1, 69'b0, 4'b1000}
  } wide_enum_t;
  typedef struct packed {
    logic [72:0] high = {1'b1, 71'b0, 1'b1};
    logic [63:0] low = 64'h8;
  } initialized_wide_t;
  typedef union tagged packed {
    logic [135:0] wide;
    logic [7:0] narrow;
  } tagged_wide_t;
  real r;
  shortreal s;
  realtime rt;
  time ticks;
  chandle handle;
  logic [136:0] wide_value;
  wide_enum_t wide_enum;
  initialized_wide_t initialized_wide;
  tagged_wide_t tagged_wide;
  logic [4:0] checks;
  class ArtifactCoverageOwner;
    covergroup artifact_coverage with function sample(
      input logic [7:0] sample_value
    );
      option.goal = 75;
      type_option.merge_instances = 1;
      value_point: coverpoint sample_value {
        bins low[] = {[0:3]};
        bins path = (1 => 2);
        illegal_bins rejected = {8};
      }
    endgroup : artifact_coverage
  endclass : ArtifactCoverageOwner
  assign wide_value = WIDE_SEED;
  initial begin
    r = 1.25;
    s = -2.5;
    rt = 3.75;
    ticks = 64'd9007199254740993;
    handle = null;
    checks[0] = r == 1.25;
    checks[1] = s == -2.5;
    checks[2] = rt == 3.75;
    checks[3] = ticks == 64'd9007199254740993;
    checks[4] = handle == null;
  end
endmodule

interface artifact_if #(parameter int WIDTH = 4);
  logic clock;
  logic [WIDTH-1:0] data;
  initial begin
    clock = 1'b0;
    data = 4'ha;
    #1 clock = 1'b1;
  end
  clocking cb @(posedge clock);
    input #0 data;
  endclocking
  modport view(input data, clocking cb);
endinterface

module artifact_virtual_leaf(artifact_if.view bus);
  virtual artifact_if #(.WIDTH(4)).view selected = bus;
endmodule

program interface_artifact;
  artifact_if #(.WIDTH(4)) link();
  artifact_virtual_leaf leaf(link);
  virtual artifact_if #(.WIDTH(4)).view selected = link;
endprogram
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
  const auto sv_result = cli::run(
      static_cast<int>(sv_compile.size()), sv_compile.data(), services,
      output, error);
  if (sv_result != 0) {
    std::cerr << error.str();
  }
  assert(sv_result == 0);
  assert(error.str().empty());

  output.str({});
  error.str({});
  const std::vector<const char*> elaborate{
      "fsim", "elaborate", "--object", vhdl_object_text.c_str(),
      "--object", sv_object_text.c_str(), "--top",
      "main=sv:work.phase_tb", "--top", "observer=sv:work.phase_watch",
      "--top", "scalar=sv:work.scalar_artifact",
      "--top", "virtual=sv:work.interface_artifact",
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
      == std::vector<std::string>(
          {"main", "observer", "scalar", "virtual"}));
  assert(design_inspection->process_count != 0);
  auto active_design = design;

  diagnostic::Engine coverage_load_diagnostics;
  auto coverage_checkpoint = app::load_design_artifact(
      active_design, coverage_load_diagnostics);
  assert(coverage_checkpoint && !coverage_load_diagnostics.has_error());
  assert(coverage_checkpoint->systemverilog_uvm_checkpoint);
  const auto& uvm_checkpoint =
      *coverage_checkpoint->systemverilog_uvm_checkpoint;
  assert(
      uvm_checkpoint.schema
          == runtime::systemverilog_uvm_checkpoint_schema
      && uvm_checkpoint.foreign_abi == FSIM_UVM_FOREIGN_ABI_VERSION
      && uvm_checkpoint.provenance.roots
          == std::vector<std::string>(
              {"main", "observer", "scalar", "virtual"})
      && uvm_checkpoint.records.size()
          == runtime::kSystemVerilogUvmStandardPhaseCount
      && uvm_checkpoint.external_state.phase_processes == 0
      && uvm_checkpoint.external_state.callbacks == 0);
  diagnostic::Engine uvm_codec_diagnostics;
  const auto uvm_bytes = app::serialize_systemverilog_uvm_state(
      uvm_checkpoint, uvm_codec_diagnostics);
  assert(uvm_bytes && !uvm_codec_diagnostics.has_error());
  const auto restored_uvm = app::deserialize_systemverilog_uvm_state(
      *uvm_bytes, "sv-uvm.bin", uvm_codec_diagnostics);
  assert(restored_uvm == uvm_checkpoint);
  const auto repeated_uvm_bytes = app::serialize_systemverilog_uvm_state(
      *restored_uvm, uvm_codec_diagnostics);
  assert(repeated_uvm_bytes == uvm_bytes);
  auto future_uvm = *uvm_bytes;
  future_uvm[8] = static_cast<char>(app::kSystemVerilogUvmStateSchema + 1U);
  diagnostic::Engine future_uvm_diagnostics;
  assert(!app::deserialize_systemverilog_uvm_state(
      future_uvm, "future-sv-uvm.bin", future_uvm_diagnostics));
  const auto truncated_uvm = uvm_bytes->substr(0, uvm_bytes->size() - 1U);
  diagnostic::Engine truncated_uvm_diagnostics;
  assert(!app::deserialize_systemverilog_uvm_state(
      truncated_uvm, "truncated-sv-uvm.bin", truncated_uvm_diagnostics));
  auto nonportable_uvm = uvm_checkpoint;
  nonportable_uvm.records.front().kind = FSIM_UVM_FOREIGN_PHASE_PROCESS;
  diagnostic::Engine nonportable_uvm_diagnostics;
  assert(!app::serialize_systemverilog_uvm_state(
      nonportable_uvm, nonportable_uvm_diagnostics));
  assert(std::ranges::any_of(
      nonportable_uvm_diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-UVM-STATE-001";
      }));
  const auto corrupt_uvm_design =
      directory / "artifact-corrupt-uvm.fsimdesign";
  std::filesystem::create_directories(corrupt_uvm_design);
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(active_design)) {
    const auto destination = corrupt_uvm_design
        / entry.path().lexically_relative(active_design);
    if (entry.is_directory()) {
      std::filesystem::create_directories(destination);
    } else if (entry.is_regular_file()) {
      std::filesystem::copy_file(entry.path(), destination);
      std::filesystem::permissions(
          destination, std::filesystem::perms::owner_write,
          std::filesystem::perm_options::add);
    }
  }
  {
    std::ofstream corrupt(
        corrupt_uvm_design / "state" / "sv-uvm.bin",
        std::ios::binary | std::ios::app);
    corrupt.put('\0');
  }
  diagnostic::Engine corrupt_uvm_diagnostics;
  assert(!app::load_design_artifact(
      corrupt_uvm_design, corrupt_uvm_diagnostics));
  assert(corrupt_uvm_diagnostics.has_error());
  std::filesystem::remove_all(corrupt_uvm_design);
  auto coverage_state = coverage_checkpoint->systemverilog_coverage;
  assert(coverage_state.declarations.size() == 1);
  assert(coverage_state.instances.size() == 1);
  assert(coverage_state.reports.size() == 1);
  const auto& coverage_declaration = coverage_state.declarations.front();
  assert(coverage_declaration.name == "artifact_coverage");
  assert(coverage_declaration.effective_instance_goal == 75);
  assert(coverage_declaration.effective_merge_instances);
  assert(coverage_declaration.coverage_declarations.size() == 1);
  assert(coverage_declaration.coverage_declarations.front().bins.size() == 6);
  auto& coverage_instance = coverage_state.instances.front();
  assert(coverage_instance.runtime_identity.find("0x") == std::string::npos);
  const auto& first_bin =
      coverage_declaration.coverage_declarations.front().bins.front();
  frontend::SystemVerilogCoverageBinHit hit;
  hit.coverage_declaration_index = 0;
  hit.bin_declaration_index = first_bin.declaration_index;
  hit.identity = first_bin.name;
  hit.hit_count = 3;
  hit.at_least = first_bin.at_least;
  hit.covered = true;
  coverage_instance.bin_hits.push_back(hit);
  coverage_instance.transition_progress.push_back({0, 4, 0, 1, 2, 3});
  coverage_instance.previous_samples.push_back({0, 2, 0, 64});
  coverage_instance.cross_bin_state.push_back(
      {0, std::nullopt, "tuple", {first_bin.name}, 4, 1, 1, 100, 1,
       true, false});
  coverage_instance.illegal_bin_reports.push_back(
      {"rejected", 8, first_bin.span});
  frontend::SystemVerilogCoverageCallbackEvent callback;
  callback.sequence = 9;
  callback.kind = frontend::SystemVerilogCoverageCallbackKind::Hit;
  callback.trigger =
      frontend::SystemVerilogCoverageSampleTrigger::Procedural;
  callback.mode = frontend::SystemVerilogCoverageExecutionMode::LlvmO2;
  callback.runtime_identity = coverage_instance.runtime_identity;
  callback.bin_identity = first_bin.name;
  callback.value = 2;
  coverage_state.callback_events.push_back(callback);
  coverage_state.trace_events.push_back(
      {9, 42, 7, "alias.coverage.hit", "coverage.hit",
       frontend::SystemVerilogCoverageCallbackKind::Hit, 2, true});
  coverage_state.aliases.push_back({"alias.coverage", "coverage"});
  frontend::refresh_systemverilog_coverage_reports(coverage_state);
  diagnostic::Engine coverage_codec_diagnostics;
  const auto coverage_bytes = app::serialize_systemverilog_coverage_state(
      coverage_state, coverage_codec_diagnostics);
  assert(coverage_bytes && !coverage_codec_diagnostics.has_error());
  auto restored_coverage = app::deserialize_systemverilog_coverage_state(
      *coverage_bytes, "coverage-state.bin", coverage_codec_diagnostics);
  assert(restored_coverage && !coverage_codec_diagnostics.has_error());
  const auto restored_bytes = app::serialize_systemverilog_coverage_state(
      *restored_coverage, coverage_codec_diagnostics);
  assert(restored_bytes == coverage_bytes);
  assert(restored_coverage->instances.front().bin_hits.front().hit_count == 3);
  assert(restored_coverage->instances.front().transition_progress.size() == 1);
  assert(restored_coverage->instances.front().previous_samples.size() == 1);
  assert(restored_coverage->instances.front().cross_bin_state.size() == 1);
  assert(restored_coverage->instances.front().illegal_bin_reports.size() == 1);
  assert(restored_coverage->callback_events.front().runtime_identity
      == coverage_instance.runtime_identity);
  assert(restored_coverage->trace_events.front().time == 42);
  assert(restored_coverage->aliases.front().canonical_root == "coverage");
  assert(frontend::render_systemverilog_coverage_report(
             restored_coverage->reports.front())
      == frontend::render_systemverilog_coverage_report(
             coverage_state.reports.front()));

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

  std::vector<runtime::SystemVerilogUvmCheckpointArtifact>
      replay_checkpoints;
  const auto run_engine = [&](const app::SimulationEngine engine) {
    diagnostic::Engine diagnostics;
    auto built = app::load_design_artifact(active_design, diagnostics);
    assert(built && !diagnostics.has_error());
    assert(built->design.roots()
        == std::vector<std::string>(
            {"main", "observer", "scalar", "virtual"}));
    assert(built->semantics.source_files().size() >= 2);
    assert(built->design.verilog_specify_paths().size() == 1);
    assert(built->design.verilog_timing_checks().size() == 1);
    assert(built->systemverilog_coverage.declarations.size() == 1);
    assert(built->systemverilog_coverage.instances.size() == 1);
    assert(built->systemverilog_coverage.reports.size() == 1);
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
    const auto scalar_checks = simulation.find_signal("scalar.checks");
    const auto scalar_real = simulation.find_signal("scalar.r");
    const auto scalar_short = simulation.find_signal("scalar.s");
    const auto scalar_realtime = simulation.find_signal("scalar.rt");
    const auto scalar_time = simulation.find_signal("scalar.ticks");
    const auto scalar_handle = simulation.find_signal("scalar.handle");
    const auto scalar_wide = simulation.find_signal("scalar.wide_value");
    const auto virtual_selected =
        simulation.find_signal("virtual.selected");
    const auto forwarded_selected =
        simulation.find_signal("virtual.leaf.selected");
    const auto clocking_sample =
        simulation.find_signal("virtual.leaf.bus.cb.data");
    assert(counter && watch && stable_probe && vital_probe && scalar_checks
        && scalar_real && scalar_short && scalar_realtime && scalar_time
        && scalar_handle && scalar_wide && virtual_selected
        && forwarded_selected && clocking_sample);
    std::size_t callbacks{};
    simulation.set_signal_change_hook(
        [&](runtime::simir::SignalId, const runtime::PackedLogic4&,
            runtime::SimulationTick, std::uint64_t) { ++callbacks; });
    const auto result = simulation.run();
    assert(result.status == runtime::RunStatus::stopped);
    assert(result.time == 6);
    const auto replay_checkpoint = simulation.capture_uvm_checkpoint();
    assert(
        replay_checkpoint
        && replay_checkpoint.artifact.time == result.time
        && replay_checkpoint.artifact.delta == simulation.delta()
        && !replay_checkpoint.artifact.provenance.content_identity.empty()
        && !replay_checkpoint.artifact.provenance.cache_identity.empty()
        && !replay_checkpoint.artifact.provenance.artifact_identity.empty()
        && replay_checkpoint.artifact.external_state.phase_processes == 0);
    replay_checkpoints.push_back(replay_checkpoint.artifact);
    assert(callbacks != 0);
    assert(simulation.read_signal(*stable_probe).to_msb_string() == "1");
    assert(simulation.read_signal(*vital_probe).to_msb_string() == "1");
    assert(simulation.read_signal(*scalar_checks).to_msb_string() == "11111");
    auto expected_wide = runtime::PackedLogic4{
        137, runtime::Logic4::zero};
    expected_wide.set(136, runtime::Logic4::one);
    expected_wide.set(73, runtime::Logic4::one);
    expected_wide.set(3, runtime::Logic4::one);
    assert(simulation.read_signal(*scalar_wide) == expected_wide);
    assert(simulation.read_scalar_signal(*scalar_real).as_real() == 1.25);
    assert(simulation.read_scalar_signal(*scalar_short).as_shortreal() == -2.5F);
    assert(simulation.read_scalar_signal(*scalar_realtime).as_real() == 3.75);
    assert(simulation.read_scalar_signal(*scalar_time).as_time()
        == UINT64_C(9007199254740993));
    assert(simulation.read_scalar_signal(*scalar_handle).as_chandle() == 0);
    const auto virtual_handle =
        simulation.read_signal(*virtual_selected).low_word();
    assert(virtual_handle.aval != 0 && virtual_handle.bval == 0);
    assert(simulation.read_signal(*forwarded_selected).low_word()
           == virtual_handle);
    assert(simulation.read_signal(*clocking_sample).to_msb_string()
           == "1010");
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
  const auto compiled_warm = run_engine(app::SimulationEngine::compiled);
  assert(interpreted == compiled);
  assert(compiled == compiled_warm);
  assert(
      replay_checkpoints.size() == 3
      && replay_checkpoints[0] == replay_checkpoints[1]
      && replay_checkpoints[1] == replay_checkpoints[2]);
  assert(interpreted.first == "00000001");
  assert(interpreted.second == "1");

  const auto relocated_design = directory / "relocated.fsimdesign";
  std::filesystem::rename(active_design, relocated_design);
  active_design = relocated_design;
  const auto relocated = run_engine(app::SimulationEngine::interpreter);
  assert(relocated == interpreted);
  assert(replay_checkpoints.back() == replay_checkpoints.front());

  output.str({});
  error.str({});
  const auto active_design_text = support::path_to_utf8(active_design);
  const std::vector<const char*> simulate{
      "fsim", "simulate", "--design", active_design_text.c_str(), "--engine",
      "compiled", "--trace", trace_text.c_str(), "--trace-filter",
      "main.*", "--trace-filter", "observer.*", "--trace-filter",
      "scalar.*", "--trace-filter", "virtual.*"};
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
  assert(trace_bytes.find("scalar") != std::string::npos);
  assert(trace_bytes.find("ticks") != std::string::npos);
  assert(trace_bytes.find("handle") != std::string::npos);
  assert(trace_bytes.find("virtual") != std::string::npos);
  assert(trace_bytes.find("selected") != std::string::npos);
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
  auto api_loaded = app::load_design_artifact(
      api_design, api_load_diagnostics);
  assert(api_loaded && !api_load_diagnostics.has_error());
  assert(api_loaded->design.roots() == std::vector<std::string>{"api"});
  auto& malformed_scalar_signals =
      const_cast<std::vector<elaboration::SignalInfo>&>(
          api_loaded->design.signals());
  assert(!malformed_scalar_signals.empty());
  malformed_scalar_signals.front().systemverilog_scalar =
      static_cast<frontend::SystemVerilogScalarKind>(255);
  diagnostic::Engine malformed_scalar_artifact_diagnostics;
  assert(!app::serialize_runtime_state(
      api_loaded->design, malformed_scalar_artifact_diagnostics));
  assert(std::ranges::any_of(
      malformed_scalar_artifact_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ART-0013"
            && diagnostic.message.find("invalid scalar enumeration")
                != std::string::npos;
      }));

  auto scalar_export_config = api_compile_config;
  scalar_export_config.project.name = "scalar-artifact-library";
  scalar_export_config.project.top = "sv:work.scalar_artifact";
  scalar_export_config.project.time_resolution = "1ns";
  scalar_export_config.build.cache_path = directory / "scalar-export-cache";
  auto scalar_library = directory / "scalar-artifact.fsimlib";
  diagnostic::Engine scalar_export_diagnostics;
  assert(app::export_library(
      scalar_export_config, "work", scalar_library,
      scalar_export_diagnostics));
  assert(!scalar_export_diagnostics.has_error());
  struct ScalarLibraryCapture {
    std::string checks;
    std::string wide;
    std::array<std::uint64_t, 5> payloads{};
    std::vector<std::string> keys;
    app::NativeCacheStatistics cache;
    std::size_t compiled_processes{};
    std::string coverage_identity;
    std::string coverage_report;
  };
  ScalarLibraryCapture scalar_o2_reference;
  const auto run_scalar_library = [&](
      const project::Optimization optimization,
      const app::SimulationEngine engine) {
    project::Config mapped;
    mapped.base_directory = directory;
    mapped.project.name = "scalar-artifact-consumer";
    mapped.project.top = "sv:work.scalar_artifact";
    mapped.project.time_resolution = "1ns";
    mapped.build.optimization = optimization;
    mapped.build.cache_path = directory
        / (optimization == project::Optimization::o0
               ? "scalar-mapped-o0" : "scalar-mapped-o2");
    mapped.library_mappings.push_back({"work", scalar_library});
    diagnostic::Engine diagnostics;
    auto built = app::build_project(mapped, diagnostics);
    if (!built) diagnostic::print_text(std::cerr, diagnostics);
    assert(built && built->mapped_libraries.size() == 1);
    ScalarLibraryCapture capture;
    capture.keys = built->specialization_cache_keys;
    assert(built->systemverilog_coverage.declarations.size() == 1);
    assert(built->systemverilog_coverage.instances.size() == 1);
    capture.coverage_identity =
        built->systemverilog_coverage.instances.front().runtime_identity;
    capture.coverage_report =
        frontend::render_systemverilog_coverage_report(
            built->systemverilog_coverage.reports.front());
    app::Simulation simulation{std::move(*built), 1000, engine};
    capture.cache = simulation.native_cache_statistics();
    capture.compiled_processes = simulation.compiled_process_count();
    const auto checks = simulation.find_signal("scalar_artifact.checks");
    const auto wide = simulation.find_signal("scalar_artifact.wide_value");
    const std::array signals{
        simulation.find_signal("scalar_artifact.r"),
        simulation.find_signal("scalar_artifact.s"),
        simulation.find_signal("scalar_artifact.rt"),
        simulation.find_signal("scalar_artifact.ticks"),
        simulation.find_signal("scalar_artifact.handle")};
    assert(checks && wide && std::ranges::all_of(
        signals, [](const auto& signal) { return signal.has_value(); }));
    assert(simulation.run().status == runtime::RunStatus::completed);
    capture.checks = simulation.read_signal(*checks).to_msb_string();
    capture.wide = simulation.read_signal(*wide).to_msb_string();
    for (std::size_t index = 0; index < signals.size(); ++index) {
      capture.payloads[index] =
          simulation.read_scalar_signal(*signals[index]).bits;
    }
    return capture;
  };
  for (const auto optimization :
       {project::Optimization::o0, project::Optimization::o2}) {
    const auto scalar_interpreted = run_scalar_library(
        optimization, app::SimulationEngine::interpreter);
    const auto scalar_cold = run_scalar_library(
        optimization, app::SimulationEngine::compiled);
    const auto scalar_warm = run_scalar_library(
        optimization, app::SimulationEngine::compiled);
    assert(scalar_interpreted.checks == "11111");
    assert(
        scalar_interpreted.wide == scalar_cold.wide
        && scalar_cold.wide == scalar_warm.wide);
    assert(scalar_interpreted.payloads == scalar_cold.payloads
        && scalar_cold.payloads == scalar_warm.payloads);
    assert(scalar_interpreted.keys == scalar_cold.keys
        && scalar_cold.keys == scalar_warm.keys);
    assert(scalar_interpreted.coverage_identity
        == scalar_cold.coverage_identity);
    assert(scalar_cold.coverage_identity
        == scalar_warm.coverage_identity);
    assert(scalar_interpreted.coverage_report
        == scalar_cold.coverage_report);
    assert(scalar_cold.coverage_report == scalar_warm.coverage_report);
#if defined(FSIM_HAS_LLVM)
    assert(scalar_cold.compiled_processes == 1);
    assert(scalar_warm.cache.hits == 1);
#endif
    if (optimization == project::Optimization::o2) {
      scalar_o2_reference = scalar_warm;
    }
  }
  const auto relocated_scalar_library =
      directory / "relocated-scalar-artifact.fsimlib";
  std::filesystem::rename(scalar_library, relocated_scalar_library);
  scalar_library = relocated_scalar_library;
  const auto relocated_scalar = run_scalar_library(
      project::Optimization::o2, app::SimulationEngine::compiled);
  assert(
      relocated_scalar.checks == "11111"
      && relocated_scalar.wide == scalar_o2_reference.wide
      && relocated_scalar.payloads == scalar_o2_reference.payloads
      && relocated_scalar.keys == scalar_o2_reference.keys
      && relocated_scalar.coverage_identity
          == scalar_o2_reference.coverage_identity
      && relocated_scalar.coverage_report
          == scalar_o2_reference.coverage_report);
#if defined(FSIM_HAS_LLVM)
  assert(relocated_scalar.cache.hits == 1);
#endif

  std::ifstream scalar_source_input(sv_source, std::ios::binary);
  std::string edited_scalar_source{
      std::istreambuf_iterator<char>{scalar_source_input}, {}};
  assert(scalar_source_input.good() || scalar_source_input.eof());
  const auto scalar_assignment = edited_scalar_source.find("r = 1.25;");
  assert(scalar_assignment != std::string::npos);
  edited_scalar_source.replace(
      scalar_assignment, std::string{"r = 1.25;"}.size(), "r = 1.5;");
  {
    std::ofstream scalar_source_output(
        sv_source, std::ios::binary | std::ios::trunc);
    scalar_source_output << edited_scalar_source;
    assert(scalar_source_output.good());
  }
  scalar_library = directory / "edited-scalar-artifact.fsimlib";
  diagnostic::Engine edited_scalar_export_diagnostics;
  assert(app::export_library(
      scalar_export_config, "work", scalar_library,
      edited_scalar_export_diagnostics));
  assert(!edited_scalar_export_diagnostics.has_error());
  const auto edited_scalar = run_scalar_library(
      project::Optimization::o2, app::SimulationEngine::compiled);
  assert(
      edited_scalar.checks == "11110"
      && edited_scalar.wide == scalar_o2_reference.wide
      && edited_scalar.payloads != scalar_o2_reference.payloads
      && edited_scalar.keys != scalar_o2_reference.keys);
#if defined(FSIM_HAS_LLVM)
  assert(
      edited_scalar.compiled_processes == 1
      && edited_scalar.cache.hits + edited_scalar.cache.misses == 1);
#endif

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
