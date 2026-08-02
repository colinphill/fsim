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
  std::vector<std::string> values;
  std::string debug_local;
  std::string vcd;
  std::vector<std::pair<std::string, std::string>> keys;
  fsim::app::NativeCacheStatistics native_cache;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  std::size_t conversion_count{};
  std::size_t adapter_count{};
};

constexpr std::array<std::string_view, 7> signal_names{
    "mixed_conversion_sv_top.width_result",
    "mixed_conversion_sv_top.unsigned_to_signed_result",
    "mixed_conversion_sv_top.signed_to_unsigned_result",
    "mixed_conversion_sv_top.boolean_result",
    "mixed_conversion_sv_top.integer_result",
    "mixed_conversion_sv_top.bit_result",
    "mixed_conversion_sv_top.state_result"};

void write_leaf(
    const std::filesystem::path& source,
    const bool edited) {
  std::ofstream output{source};
  output << R"(
module mixed_conversion_sv_leaf(
  input logic [7:0] width_value,
  output logic [7:0] width_result,
  input logic signed [7:0] unsigned_to_signed_value,
  output logic signed [7:0] unsigned_to_signed_result,
  input logic [7:0] signed_to_unsigned_value,
  output logic [7:0] signed_to_unsigned_result,
  input logic boolean_value,
  output logic boolean_result,
  input bit signed [31:0] integer_value,
  output logic signed [31:0] integer_result,
  input bit [3:0] bit_value,
  output bit [3:0] bit_result,
  input logic [3:0] state_value,
  output logic [3:0] state_result
);
)";
  output << "  assign width_result = width_value"
         << (edited ? " ^ 8'h01;\n" : ";\n");
  output << R"(
  assign unsigned_to_signed_result = unsigned_to_signed_value;
  assign signed_to_unsigned_result = signed_to_unsigned_value;
  assign boolean_result = boolean_value;
  assign integer_result = integer_value + 1;
  assign bit_result = bit_value;
  assign state_result = state_value;
endmodule
)";
  assert(output.good());
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& top_source,
    const std::filesystem::path& middle_source,
    const std::filesystem::path& leaf_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "mixed-conversion-matrix";
  config.project.top = "sv:work.mixed_conversion_sv_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet vhdl_sources;
  vhdl_sources.language = fsim::project::Language::vhdl;
  vhdl_sources.standard = "2008";
  vhdl_sources.library = "work";
  vhdl_sources.compilation_unit = "file";
  vhdl_sources.files.push_back(middle_source);
  config.source_sets.push_back(std::move(vhdl_sources));

  fsim::project::SourceSet sv_sources;
  sv_sources.language = fsim::project::Language::system_verilog;
  sv_sources.standard = "2017";
  sv_sources.library = "work";
  sv_sources.compilation_unit = "file";
  sv_sources.files = {top_source, leaf_source};
  config.source_sets.push_back(std::move(sv_sources));

  config.bindings = {
      {"mixed_conversion_sv_top.middle",
       "vhdl:work.mixed_conversion_vhdl_middle(rtl)",
       std::nullopt},
      {"mixed_conversion_sv_top.middle.leaf",
       "sv:work.mixed_conversion_sv_leaf",
       std::nullopt}};
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(project);

  Capture capture;
  assert(
      project->specialization_cache_keys.size()
      == project->design.specializations().size());
  for (std::size_t index = 0;
       index < project->design.specializations().size();
       ++index) {
    capture.keys.emplace_back(
        project->design.specializations()[index].instance,
        project->specialization_cache_keys[index]);
  }

  const auto& conversions = project->design.boundary_conversions();
  capture.conversion_count = conversions.size();
  capture.adapter_count = std::ranges::count_if(
      conversions,
      [](const auto& conversion) {
        return conversion.process.has_value();
      });
  assert(capture.conversion_count == 28);
  assert(capture.adapter_count == 11);
  for (const auto& conversion : conversions) {
    const auto connection =
        fsim::frontend::physical_source(conversion.connection_span);
    const auto formal =
        fsim::frontend::physical_source(conversion.formal_span);
    const auto actual =
        fsim::frontend::physical_source(conversion.actual_span);
    assert(!conversion.path.empty());
    assert(!connection.empty());
    assert(!formal.empty());
    assert(!actual.empty());
  }

  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> debug_local;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      if (process.debug_locals[index].name == "boundary_probe") {
        debug_local = std::pair{process.id, index};
      }
    }
  }
  assert(debug_local);

  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.native_cache = simulation.native_cache_statistics();

  std::array<
      fsim::runtime::simir::SignalId,
      signal_names.size()> signals{};
  std::array<
      fsim::runtime::VcdSignal,
      signal_names.size()> traces{};
  constexpr std::array<std::size_t, signal_names.size()> widths{
      8, 8, 8, 1, 32, 4, 4};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0; index < signal_names.size(); ++index) {
    const auto signal = simulation.find_signal(signal_names[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{signal_names[index]}, widths[index]);
  }
  vcd.begin();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(traces[index], simulation.read_signal(signals[index]));
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
  capture.debug_local = simulation.read_process_local(
      debug_local->first, debug_local->second).to_msb_string();
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void compare_captures(
    const Capture& reference,
    const Capture& compiled) {
  assert(reference.result.status == compiled.result.status);
  assert(reference.result.time == compiled.result.time);
  assert(reference.result.delta == compiled.result.delta);
  assert(reference.values == compiled.values);
  assert(reference.debug_local == compiled.debug_local);
  assert(reference.vcd == compiled.vcd);
  assert(reference.keys == compiled.keys);
  assert(reference.conversion_count == compiled.conversion_count);
  assert(reference.adapter_count == compiled.adapter_count);
}

void verify_capture(const Capture& capture, const bool edited) {
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  assert((capture.values == std::vector<std::string>{
      edited ? "00001011" : "00001010",
      "00001010",
      "11111010",
      "1",
      "11111111111111111111111111111111",
      "0110",
      "01XZ"}));
  assert(capture.debug_local == "01XZ");
  assert(capture.vcd.find("b01xz") != std::string::npos);
  assert(capture.vcd.find("$enddefinitions $end") != std::string::npos);
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-mixed-conversions-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const auto top_source = directory.path / "top.sv";
  {
    std::ofstream output{top_source};
    output << R"(
module mixed_conversion_sv_top;
  logic [3:0] width_value;
  logic [7:0] width_result;
  logic [3:0] unsigned_to_signed_value;
  logic signed [7:0] unsigned_to_signed_result;
  logic signed [3:0] signed_to_unsigned_value;
  logic [7:0] signed_to_unsigned_result;
  logic boolean_value;
  logic boolean_result;
  bit signed [31:0] integer_value;
  logic signed [31:0] integer_result;
  bit [3:0] bit_value;
  bit [3:0] bit_result;
  logic [3:0] state_value;
  logic [3:0] state_result;

  assign width_value = 4'b1010;
  assign unsigned_to_signed_value = 4'b1010;
  assign signed_to_unsigned_value = 4'b1010;
  assign boolean_value = 1'b1;
  assign integer_value = -2;
  assign bit_value = 4'b0110;
  assign state_value = 4'b01xz;

  mixed_conversion_vhdl_middle middle(
    .width_value(width_value),
    .width_result(width_result),
    .unsigned_to_signed_value(unsigned_to_signed_value),
    .unsigned_to_signed_result(unsigned_to_signed_result),
    .signed_to_unsigned_value(signed_to_unsigned_value),
    .signed_to_unsigned_result(signed_to_unsigned_result),
    .boolean_value(boolean_value),
    .boolean_result(boolean_result),
    .integer_value(integer_value),
    .integer_result(integer_result),
    .bit_value(bit_value),
    .bit_result(bit_result),
    .state_value(state_value),
    .state_result(state_result)
  );

  initial #1 $finish;
endmodule
)";
    assert(output.good());
  }

  const auto middle_source = directory.path / "middle.vhd";
  {
    std::ofstream output{middle_source};
    output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity Mixed_Conversion_Vhdl_Middle is
  port (
    Width_Value : in unsigned(7 downto 0);
    Width_Result : out unsigned(7 downto 0);
    Unsigned_To_Signed_Value : in signed(7 downto 0);
    Unsigned_To_Signed_Result : out signed(7 downto 0);
    Signed_To_Unsigned_Value : in unsigned(7 downto 0);
    Signed_To_Unsigned_Result : out unsigned(7 downto 0);
    Boolean_Value : in boolean;
    Boolean_Result : out boolean;
    Integer_Value : in integer range -8 to 7;
    Integer_Result : out integer range -8 to 7;
    Bit_Value : in bit_vector(3 downto 0);
    Bit_Result : out bit_vector(3 downto 0);
    State_Value : in std_ulogic_vector(3 downto 0);
    State_Result : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of Mixed_Conversion_Vhdl_Middle is
begin
  leaf : mixed_conversion_sv_leaf
    port map (
      width_value => Width_Value,
      width_result => Width_Result,
      unsigned_to_signed_value => Unsigned_To_Signed_Value,
      unsigned_to_signed_result => Unsigned_To_Signed_Result,
      signed_to_unsigned_value => Signed_To_Unsigned_Value,
      signed_to_unsigned_result => Signed_To_Unsigned_Result,
      boolean_value => Boolean_Value,
      boolean_result => Boolean_Result,
      integer_value => Integer_Value,
      integer_result => Integer_Result,
      bit_value => Bit_Value,
      bit_result => Bit_Result,
      state_value => State_Value,
      state_result => State_Result
    );

  probe : process(State_Value)
    variable Boundary_Probe : std_logic_vector(3 downto 0);
  begin
    Boundary_Probe := std_logic_vector(State_Value);
  end process;
end architecture;
)";
    assert(output.good());
  }

  const auto leaf_source = directory.path / "leaf.sv";
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    write_leaf(leaf_source, false);
    const auto config = make_config(
        directory.path,
        top_source,
        middle_source,
        leaf_source,
        optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference, false);
    verify_capture(cold, false);
    verify_capture(warm, false);
    compare_captures(reference, cold);
    compare_captures(reference, warm);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0);
    assert(cold.compiled_modules > 0);
    assert(cold.native_cache.hits == 0);
    assert(cold.native_cache.misses == cold.compiled_modules);
    assert(cold.native_cache.stores == cold.compiled_modules);
    assert(warm.native_cache.hits == warm.compiled_modules);
    assert(warm.native_cache.misses == 0);
#endif

    write_leaf(leaf_source, true);
    const auto edited_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto edited = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify_capture(edited_reference, true);
    verify_capture(edited, true);
    compare_captures(edited_reference, edited);
    assert(edited.keys != cold.keys);
#if defined(FSIM_HAS_LLVM)
    assert(edited.native_cache.misses > 0);
    assert(edited.native_cache.stores == edited.native_cache.misses);
#endif
  }

  std::cout << "mixed conversion application tests passed\n";
  return 0;
}
