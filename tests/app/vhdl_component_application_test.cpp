// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "path_test_support.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  std::array<std::string, 3> values;
  std::array<std::string, 2> packets;
  std::string nonvalue;
  std::string defaulted;
  std::string dynamic_expression;
  std::array<std::string, 2> expression_inputs;
  std::array<std::string, 2> direct_default_inputs;
  std::array<std::string, 3> dependent_generic_outputs;
  std::array<std::string, 2> aggregate_generic_outputs;
  std::array<std::string, 2> static_generic_outputs;
  std::array<std::string, 2> composite_expression_outputs;
  std::array<std::string, 2> open_mode_outputs;
  std::array<std::string, 3> dependent_port_outputs;
  std::string vcd;
  std::vector<std::pair<std::string, std::string>> keys;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  fsim::app::NativeCacheStatistics cache;
};

std::string bits(const std::uint32_t value) {
  return std::bitset<32>{value}.to_string();
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& leaf,
    const std::filesystem::path& profiles,
    const std::filesystem::path& default_profiles,
    const std::filesystem::path& hierarchy,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-components";
  config.project.top = "vhdl:work.component_runtime_top(rtl)";
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
      profiles, leaf, default_profiles, hierarchy};
  config.source_sets.push_back(std::move(sources));
  return config;
}

const fsim::elaboration::SpecializationInfo&
specialization(
    const fsim::app::BuiltProject& project,
    const std::string_view path) {
  const auto found = std::ranges::find_if(
      project.design.specializations(),
      [&](const auto& candidate) {
        return candidate.instance == path;
      });
  assert(found != project.design.specializations().end());
  return *found;
}

#if defined(FSIM_HAS_LLVM)
std::string key_for(
    const Capture& capture,
    const std::string_view path) {
  const auto found = std::ranges::find_if(
      capture.keys,
      [&](const auto& item) {
        return item.first == path;
      });
  assert(found != capture.keys.end());
  return found->second;
}
#endif

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
  assert(project->design.specializations().size() == 24);
  for (const auto path : {
           "component_runtime_top.positional_child",
           "component_runtime_top.default_child"}) {
    const auto& selected = specialization(*project, path);
    assert(
        selected.unit
        == "vhdl:work.component_runtime_leaf(rtl)");
    assert(fsim::test::has_source_dependency(
        selected.source_dependencies,
        config.source_sets.front().files[0]));
    assert(std::ranges::any_of(
        selected.parameter_identity_values,
        [](const auto& item) {
          return item.first == "__component"
              && item.second.starts_with(
                  "vhdl-component-binding-v7")
              && item.second.find(
                     "region=2;scope=;owner=work."
                     "component_runtime_profiles")
                  != std::string::npos
              && item.second.find(
                     "component_runtime_profiles.vhd:")
                  != std::string::npos;
        }));
  }
  assert(
      specialization(
          *project,
          "component_runtime_top.direct_child").unit
      == "vhdl:work.component_runtime_stable(rtl)");
  assert(!fsim::test::has_source_dependency(
      specialization(
          *project,
          "component_runtime_top.direct_child")
          .source_dependencies,
      config.source_sets.front().files[0]));
  const auto& nonvalue = specialization(
      *project, "component_runtime_top.nonvalue_child");
  assert(
      nonvalue.unit
      == "vhdl:work.component_runtime_nonvalue(rtl)");
  for (const auto& dependency :
       {config.source_sets.front().files[0],
        config.source_sets.front().files[3]}) {
    assert(fsim::test::has_source_dependency(
        nonvalue.source_dependencies, dependency));
  }
  assert(std::ranges::any_of(
      nonvalue.parameter_identity_values,
      [](const auto& item) {
        return item.first == "__component"
            && item.second.starts_with(
                "vhdl-component-binding-v7")
            && item.second.find(
                   "actual=component_t:vhdl-type-v1")
                != std::string::npos
            && item.second.find(
                   "actual=component_function:vhdl-function-v1")
                != std::string::npos
            && item.second.find(
                   "actual=component_procedure:vhdl-procedure-v1")
                != std::string::npos
            && item.second.find(
                   "actual=component_helpers:vhdl-package-v1")
                != std::string::npos;
      }));
  const auto& defaulted = specialization(
      *project, "component_runtime_top.defaulted_child");
  assert(
      defaulted.unit
      == "vhdl:work.component_runtime_defaulted(rtl)");
  assert(fsim::test::has_source_dependency(
      defaulted.source_dependencies,
      config.source_sets.front().files[2]));
  assert(std::ranges::any_of(
      defaulted.parameter_identity_values,
      [](const auto& item) {
        return item.first == "__component"
            && item.second.starts_with(
                "vhdl-component-binding-v7")
            && item.second.find("state=1")
                != std::string::npos
            && item.second.find("state=2")
                != std::string::npos;
      }));

  Capture capture;
  for (std::size_t index = 0;
       index < project->specialization_cache_keys.size();
       ++index) {
    capture.keys.emplace_back(
        project->design.specializations()[index].instance,
        project->specialization_cache_keys[index]);
  }

  fsim::app::Simulation simulation{
      std::move(*project),
      config.run.max_deltas,
      engine};
  capture.cache = simulation.native_cache_statistics();
  simulation.set_execution_point_hook(
      [&capture](
          fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
      });

  constexpr std::array<std::string_view, 3> inputs{
      "component_runtime_top.positional_input",
      "component_runtime_top.default_input",
      "component_runtime_top.direct_input"};
  constexpr std::array<std::string_view, 3> outputs{
      "component_runtime_top.positional_output",
      "component_runtime_top.default_output",
      "component_runtime_top.direct_output"};
  constexpr std::array<std::uint32_t, 3> values{10, 20, 30};
  constexpr std::array<std::string_view, 2> packet_inputs{
      "component_runtime_top.positional_packet_input",
      "component_runtime_top.default_packet_input"};
  constexpr std::array<std::string_view, 2> packet_outputs{
      "component_runtime_top.positional_packet_output",
      "component_runtime_top.default_packet_output"};
  constexpr std::array<std::string_view, 2> packet_values{
      "10101", "01010"};
  std::array<fsim::runtime::simir::SignalId, 3> output_ids{};
  std::array<fsim::runtime::simir::SignalId, 2>
      packet_output_ids{};
  constexpr std::array<std::string_view, 3> dependent_outputs{
      "component_runtime_top.direct_dependent_default_output",
      "component_runtime_top.direct_dependent_open_output",
      "component_runtime_top.component_dependent_default_output"};
  std::array<fsim::runtime::simir::SignalId, 3>
      dependent_output_ids{};
  constexpr std::array<std::string_view, 2> aggregate_outputs{
      "component_runtime_top.aggregate_generic_default_output",
      "component_runtime_top.aggregate_generic_explicit_output"};
  std::array<fsim::runtime::simir::SignalId, 2>
      aggregate_output_ids{};
  constexpr std::array<std::string_view, 2> static_outputs{
      "component_runtime_top.static_generic_default_output",
      "component_runtime_top.static_generic_override_output"};
  std::array<fsim::runtime::simir::SignalId, 2>
      static_output_ids{};
  const auto nonvalue_input = simulation.find_signal(
      "component_runtime_top.nonvalue_input");
  const auto nonvalue_output = simulation.find_signal(
      "component_runtime_top.nonvalue_output");
  const auto defaulted_output = simulation.find_signal(
      "component_runtime_top.defaulted_child."
      "entity_default_output");
  const auto dynamic_input = simulation.find_signal(
      "component_runtime_top.dynamic_expression_input");
  const auto dynamic_output = simulation.find_signal(
      "component_runtime_top.dynamic_expression_output");
  const auto vector_input = simulation.find_signal(
      "component_runtime_top.vector_expression_input");
  constexpr std::array<std::string_view, 2> composite_outputs{
      "component_runtime_top.slice_expression_output",
      "component_runtime_top.concat_expression_output"};
  std::array<fsim::runtime::simir::SignalId, 2>
      composite_output_ids{};
  assert(
      nonvalue_input && nonvalue_output
      && defaulted_output && dynamic_input && dynamic_output
      && vector_input);
  simulation.deposit_signal(
      *nonvalue_input,
      fsim::runtime::PackedLogic4::from_msb_string(bits(7)));
  simulation.deposit_signal(
      *dynamic_input,
      fsim::runtime::PackedLogic4::from_msb_string(bits(43)));
  simulation.deposit_signal(
      *vector_input,
      fsim::runtime::PackedLogic4::from_msb_string("11010110"));
  for (std::size_t index = 0;
       index < composite_outputs.size(); ++index) {
    const auto output = simulation.find_signal(composite_outputs[index]);
    assert(output);
    composite_output_ids[index] = *output;
  }
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    const auto input = simulation.find_signal(inputs[index]);
    const auto output = simulation.find_signal(outputs[index]);
    assert(input && output);
    simulation.deposit_signal(
        *input,
        fsim::runtime::PackedLogic4::from_msb_string(
            bits(values[index])));
    output_ids[index] = *output;
  }
  for (std::size_t index = 0;
       index < packet_inputs.size(); ++index) {
    const auto input =
        simulation.find_signal(packet_inputs[index]);
    const auto output =
        simulation.find_signal(packet_outputs[index]);
    assert(input && output);
    simulation.deposit_signal(
        *input,
        fsim::runtime::PackedLogic4::from_msb_string(
            packet_values[index]));
    packet_output_ids[index] = *output;
  }
  for (std::size_t index = 0;
       index < dependent_outputs.size(); ++index) {
    const auto output = simulation.find_signal(dependent_outputs[index]);
    assert(output);
    dependent_output_ids[index] = *output;
  }
  for (std::size_t index = 0;
       index < aggregate_outputs.size(); ++index) {
    const auto output = simulation.find_signal(aggregate_outputs[index]);
    assert(output);
    aggregate_output_ids[index] = *output;
  }
  for (std::size_t index = 0;
       index < static_outputs.size(); ++index) {
    const auto output = simulation.find_signal(static_outputs[index]);
    assert(output);
    static_output_ids[index] = *output;
  }
  constexpr std::array<std::string_view, 3> dependent_port_inputs{
      "component_runtime_top.component_width_input",
      "component_runtime_top.component_width_twin_input",
      "component_runtime_top.direct_width_input"};
  constexpr std::array<std::string_view, 3> dependent_port_outputs{
      "component_runtime_top.component_width_output",
      "component_runtime_top.component_width_twin_output",
      "component_runtime_top.direct_width_output"};
  constexpr std::array<std::string_view, 3> dependent_port_values{
      "1010", "0110", "11001010"};
  std::array<fsim::runtime::simir::SignalId, 3>
      dependent_port_output_ids{};
  for (std::size_t index = 0;
       index < dependent_port_inputs.size(); ++index) {
    const auto input = simulation.find_signal(
        dependent_port_inputs[index]);
    const auto output = simulation.find_signal(
        dependent_port_outputs[index]);
    assert(input && output);
    simulation.deposit_signal(
        *input,
        fsim::runtime::PackedLogic4::from_msb_string(
            dependent_port_values[index]));
    dependent_port_output_ids[index] = *output;
  }

  constexpr std::array<std::uint32_t, 3> dependent_port_widths{
      4, 4, 8};
  std::array<fsim::runtime::VcdSignal, 3> dependent_port_traces{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 32};
  for (std::size_t index = 0;
       index < dependent_port_outputs.size(); ++index) {
    dependent_port_traces[index] = vcd.declare_signal(
        std::string{dependent_port_outputs[index]},
        dependent_port_widths[index]);
  }
  vcd.begin(simulation.now());
  for (std::size_t index = 0;
       index < dependent_port_output_ids.size(); ++index) {
    vcd.change(
        dependent_port_traces[index],
        simulation.read_signal(dependent_port_output_ids[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        const auto found = std::ranges::find(
            dependent_port_output_ids, signal);
        if (found == dependent_port_output_ids.end()) {
          return;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(dependent_port_output_ids.begin(), found));
        vcd.set_time(time);
        vcd.change(dependent_port_traces[index], value);
      });
  capture.result = simulation.run();
  for (std::size_t index = 0; index < output_ids.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(output_ids[index]).to_msb_string();
  }
  for (std::size_t index = 0;
       index < packet_output_ids.size(); ++index) {
    capture.packets[index] =
        simulation.read_signal(
            packet_output_ids[index]).to_msb_string();
  }
  for (std::size_t index = 0;
       index < dependent_output_ids.size(); ++index) {
    capture.dependent_generic_outputs[index] =
        simulation.read_signal(dependent_output_ids[index])
            .to_msb_string();
  }
  for (std::size_t index = 0;
       index < aggregate_output_ids.size(); ++index) {
    capture.aggregate_generic_outputs[index] =
        simulation.read_signal(aggregate_output_ids[index])
            .to_msb_string();
  }
  for (std::size_t index = 0;
       index < static_output_ids.size(); ++index) {
    capture.static_generic_outputs[index] =
        simulation.read_signal(static_output_ids[index]).to_msb_string();
  }
  for (std::size_t index = 0;
       index < dependent_port_output_ids.size(); ++index) {
    capture.dependent_port_outputs[index] = simulation.read_signal(
        dependent_port_output_ids[index]).to_msb_string();
  }
  capture.nonvalue =
      simulation.read_signal(*nonvalue_output).to_msb_string();
  capture.defaulted =
      simulation.read_signal(*defaulted_output).to_msb_string();
  capture.dynamic_expression =
      simulation.read_signal(*dynamic_output).to_msb_string();
  for (std::size_t index = 0;
       index < composite_output_ids.size(); ++index) {
    capture.composite_expression_outputs[index] =
        simulation.read_signal(composite_output_ids[index])
            .to_msb_string();
  }
  constexpr std::array<std::string_view, 2> expression_inputs{
      "component_runtime_top.component_expression_child."
      "entity_default_input",
      "component_runtime_top.direct_expression_child.input_value"};
  for (std::size_t index = 0;
       index < expression_inputs.size(); ++index) {
    const auto signal = simulation.find_signal(expression_inputs[index]);
    assert(signal);
    capture.expression_inputs[index] =
        simulation.read_signal(*signal).to_msb_string();
  }
  constexpr std::array<std::string_view, 2> direct_default_inputs{
      "component_runtime_top.direct_default_omitted."
      "entity_default_input",
      "component_runtime_top.direct_default_open."
      "entity_default_input"};
  for (std::size_t index = 0;
       index < direct_default_inputs.size(); ++index) {
    const auto signal = simulation.find_signal(direct_default_inputs[index]);
    assert(signal);
    capture.direct_default_inputs[index] =
        simulation.read_signal(*signal).to_msb_string();
  }
  constexpr std::array<std::string_view, 2> open_mode_outputs{
      "component_runtime_top.open_mode_child.output_value",
      "component_runtime_top.open_mode_child.buffer_value"};
  for (std::size_t index = 0;
       index < open_mode_outputs.size(); ++index) {
    const auto signal = simulation.find_signal(open_mode_outputs[index]);
    assert(signal);
    capture.open_mode_outputs[index] =
        simulation.read_signal(*signal).to_msb_string();
  }
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify(
    const Capture& capture,
    const std::uint32_t expected_nonvalue = 27,
    const std::uint32_t expected_default = 7) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 0);
  assert((capture.values
          == std::array<std::string, 3>{
              bits(15), bits(23), bits(30)}));
  assert((capture.packets
          == std::array<std::string, 2>{
              "10101", "01010"}));
  assert(capture.nonvalue == bits(expected_nonvalue));
  assert(capture.defaulted == bits(expected_default));
  assert(capture.dynamic_expression == bits(44));
  assert((capture.composite_expression_outputs
          == std::array<std::string, 2>{"0101", "1011"}));
  assert((capture.expression_inputs
          == std::array<std::string, 2>{bits(41), bits(42)}));
  assert((capture.direct_default_inputs
          == std::array<std::string, 2>{bits(13), bits(13)}));
  assert((capture.open_mode_outputs
          == std::array<std::string, 2>{bits(5), bits(7)}));
  assert((capture.dependent_port_outputs
          == std::array<std::string, 3>{
              "1010", "0110", "11001010"}));
  assert(
      capture.vcd.find("$timescale 1ns $end")
      != std::string::npos);
  assert(
      capture.vcd.find("component_width_output")
      != std::string::npos);
  assert(
      capture.vcd.find("component_width_twin_output")
      != std::string::npos);
  assert(
      capture.vcd.find("direct_width_output")
      != std::string::npos);
  assert(capture.vcd.find("b1010") != std::string::npos);
  assert(capture.vcd.find("b0110") != std::string::npos);
  assert(capture.vcd.find("b11001010") != std::string::npos);
  assert((capture.dependent_generic_outputs
          == std::array<std::string, 3>{bits(8), bits(6), bits(6)}));
  assert((capture.aggregate_generic_outputs
          == std::array<std::string, 2>{"0001", "0010"}));
  assert((capture.static_generic_outputs
          == std::array<std::string, 2>{bits(11), bits(8)}));
  assert(std::ranges::count_if(
             capture.points,
             [](const auto& point) {
               return point.kind
                   == fsim::runtime::simir::
                       ExecutionPointKind::statement;
             })
         >= 3);
  assert(std::ranges::any_of(
      capture.points,
      [](const auto& point) {
        return point.source.path.find(
                   "component_runtime_leaf.vhd")
            != std::string::npos;
      }));
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-component-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto leaf =
      directory.path / "component_runtime_leaf.vhd";
  const auto hierarchy =
      directory.path / "component_runtime_top.vhd";
  const auto profiles =
      directory.path / "component_runtime_profiles.vhd";
  const auto default_profiles =
      directory.path / "component_default_profiles.vhd";

  {
    std::ofstream output(leaf, std::ios::binary);
    output << R"(
use work.component_runtime_profiles.all;
entity component_runtime_leaf is
  generic (entity_amount : integer := 9);
  port (
    entity_input : in integer;
    entity_output : out integer;
    entity_packet_input : in packet_t;
    entity_packet_output : out packet_t);
end entity;
architecture rtl of component_runtime_leaf is
begin
  entity_output <= entity_input + entity_amount;
  entity_packet_output <= entity_packet_input;
end architecture;

entity component_runtime_stable is
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of component_runtime_stable is
begin
  output_value <= input_value;
end architecture;

entity component_runtime_defaulted is
  port (
    entity_default_input : in integer := 13;
    entity_default_output : out integer);
end entity;
architecture rtl of component_runtime_defaulted is
begin
  entity_default_output <= entity_default_input;
end architecture;

entity component_runtime_nonvalue is
  generic (
    type entity_t;
    function entity_function(value : entity_t) return entity_t is <>;
    procedure entity_procedure(variable value : inout entity_t) is <>;
    package entity_helpers is new work.component_runtime_helper_template
      generic map (<>));
  port (
    entity_input : in entity_t;
    entity_output : out entity_t);
end entity;
architecture rtl of component_runtime_nonvalue is
begin
  process(entity_input)
    variable temporary : entity_t;
  begin
    temporary := entity_function(entity_input);
    entity_procedure(temporary);
    entity_output <= temporary;
  end process;
end architecture;

entity component_runtime_vector is
  port (
    input_value : in std_logic_vector(3 downto 0);
    output_value : out std_logic_vector(3 downto 0));
end entity;
architecture rtl of component_runtime_vector is
begin
  output_value <= input_value;
end architecture;

entity component_runtime_open_modes is
  port (
    output_value : out integer;
    buffer_value : buffer integer);
end entity;
architecture rtl of component_runtime_open_modes is
begin
  output_value <= 5;
  buffer_value <= 7;
end architecture;

entity component_runtime_width is
  generic (width : positive := 4);
  port (
    input_value : in bit_vector(width - 1 downto 0);
    output_value : out bit_vector(width - 1 downto 0));
end entity;
architecture rtl of component_runtime_width is
begin
  output_value <= input_value;
end architecture;

entity component_runtime_generic_defaults is
  generic (
    constant base_value : in integer := 4;
    derived_value : integer := base_value * 2);
  port (output_value : out integer);
end entity;
architecture rtl of component_runtime_generic_defaults is
begin
  output_value <= derived_value;
end architecture;

use work.component_runtime_profiles.all;
entity component_runtime_aggregate_generic is
  generic (mask_value : mask_t := (0 => '1', others => '0'));
  port (output_value : out mask_t);
end entity;
architecture rtl of component_runtime_aggregate_generic is
begin
  output_value <= mask_value;
end architecture;

use work.component_runtime_profiles.all;
entity component_runtime_static_defaults is
  generic (
    package_value : integer := package_base;
    length_value : integer := mask_t'length;
    converted_value : positive := positive(package_value + length_value);
    function_value : integer := scale(converted_value));
  port (output_value : out integer);
end entity;
architecture rtl of component_runtime_static_defaults is
begin
  output_value <= function_value;
end architecture;
)";
    assert(output.good());
  }

  const auto write_profiles = [&](const bool revised_profile) {
    const auto amount_name =
        revised_profile ? "revised_amount" : "component_amount";
    const auto input_name =
        revised_profile ? "revised_input" : "component_input";
    const auto output_name =
        revised_profile ? "revised_output" : "component_output";
    const auto member_name =
        revised_profile ? "revised_payload" : "payload";
    std::ofstream output(
        profiles, std::ios::binary | std::ios::trunc);
    output << R"(
package component_runtime_helper_template is
  generic (seed : integer := 1);
  constant selected_seed : integer := seed;
end package;

package component_runtime_profiles is
  subtype mask_t is bit_vector(3 downto 0);
  constant package_base : integer := 6;
  function scale(value : integer) return integer;
  type packet_t is record
    )"
           << member_name
           << R"( : std_logic_vector(3 downto 0);
    valid : bit;
  end record;
  component component_runtime_leaf is
    generic ()"
           << amount_name
           << R"( : integer := 3);
    port ()"
           << input_name
           << R"( : in integer; )"
           << output_name
           << R"( : out integer;
      component_packet_input : in packet_t;
      component_packet_output : out packet_t);
  end component;
  component component_runtime_nonvalue is
    generic (
      type component_t;
      function component_function(value : component_t)
        return component_t is <>;
      procedure component_procedure(
        variable value : inout component_t) is <>;
      package component_helpers is new
        work.component_runtime_helper_template generic map (<>));
    port (
      component_input : in component_t;
      component_output : out component_t);
  end component;
  component component_runtime_width is
    generic (width : positive := 4);
    port (
      input_value : in bit_vector(width - 1 downto 0);
      output_value : out bit_vector(width - 1 downto 0));
  end component;
end package;

package body component_runtime_profiles is
  function scale(value : integer) return integer is
  begin
    return value + 1;
  end function;
end package body;
)";
    assert(output.good());
  };

  const auto write_default_profiles =
      [&](const std::uint32_t default_value) {
        std::ofstream output(
            default_profiles,
            std::ios::binary | std::ios::trunc);
        output << R"(
package component_default_profiles is
  component component_runtime_defaulted is
    port (
      component_default_input : in integer := )"
               << default_value
               << R"(;
      component_default_output : out integer);
  end component;
end package;
)";
        assert(output.good());
      };

  const auto write_hierarchy = [&](const bool revised_function) {
    const auto increment = revised_function ? 3 : 2;
    std::ofstream output(
        hierarchy, std::ios::binary | std::ios::trunc);
    output << R"(
entity component_runtime_top is
end entity;
use work.component_runtime_profiles.all;
use work.component_default_profiles.all;
architecture rtl of component_runtime_top is
  function increment(value : integer) return integer is
  begin
    return value + )"
           << increment
           << R"(;
  end function;
  procedure triple_value(variable value : inout integer) is
  begin
    value := value * 3;
  end procedure;
  package helper_instance is new work.component_runtime_helper_template
    generic map (seed => 4);
  component component_runtime_generic_defaults is
    generic (
      constant component_base : in integer := 5;
      component_derived : integer := component_base + 1);
    port (component_output : out integer);
  end component;
  signal positional_input : integer;
  signal positional_output : integer;
  signal default_input : integer;
  signal default_output : integer;
  signal direct_input : integer;
  signal direct_output : integer;
  signal positional_packet_input : packet_t;
  signal positional_packet_output : packet_t;
  signal default_packet_input : packet_t;
  signal default_packet_output : packet_t;
  signal nonvalue_input : integer;
  signal nonvalue_output : integer;
  signal dynamic_expression_input : integer;
  signal dynamic_expression_output : integer;
  signal vector_expression_input : std_logic_vector(7 downto 0);
  signal slice_expression_output : std_logic_vector(3 downto 0);
  signal concat_expression_output : std_logic_vector(3 downto 0);
  signal direct_dependent_default_output : integer;
  signal direct_dependent_open_output : integer;
  signal component_dependent_default_output : integer;
  signal aggregate_generic_default_output : mask_t;
  signal aggregate_generic_explicit_output : mask_t;
  signal static_generic_default_output : integer;
  signal static_generic_override_output : integer;
  signal component_width_input : bit_vector(3 downto 0);
  signal component_width_output : bit_vector(3 downto 0);
  signal component_width_twin_input : bit_vector(3 downto 0);
  signal component_width_twin_output : bit_vector(3 downto 0);
  signal direct_width_input : bit_vector(7 downto 0);
  signal direct_width_output : bit_vector(7 downto 0);
begin
  positional_child: component_runtime_leaf
    generic map (5)
    port map (
      positional_input,
      positional_output,
      positional_packet_input,
      positional_packet_output);
  default_child: component_runtime_leaf
    port map (
      default_input,
      default_output,
      default_packet_input,
      default_packet_output);
  direct_child: entity work.component_runtime_stable(rtl)
    port map (
      input_value => direct_input,
      output_value => direct_output);
  nonvalue_child: component_runtime_nonvalue
    generic map (
      component_t => integer,
      component_function => increment,
      component_procedure => triple_value,
      component_helpers => helper_instance)
    port map (
      component_input => nonvalue_input,
      component_output => nonvalue_output);
  defaulted_child: component_runtime_defaulted
    port map (
      component_default_input => open,
      component_default_output => open);
  component_expression_child: component_runtime_defaulted
    port map (
      component_default_input => 20 + 21,
      component_default_output => open);
  direct_expression_child: entity work.component_runtime_stable(rtl)
    port map (
      input_value => 42,
      output_value => open);
  dynamic_expression_child: entity work.component_runtime_stable(rtl)
    port map (
      input_value => dynamic_expression_input + 1,
      output_value => dynamic_expression_output);
  slice_expression_child: entity work.component_runtime_vector(rtl)
    port map (
      input_value => std_logic_vector'(
        vector_expression_input(5 downto 2)),
      output_value => slice_expression_output);
  concat_expression_child: entity work.component_runtime_vector(rtl)
    port map (
      input_value => std_logic_vector(
        vector_expression_input(1 downto 0)
          & vector_expression_input(7 downto 6)),
      output_value => concat_expression_output);
  open_mode_child: entity work.component_runtime_open_modes(rtl)
    port map (
      output_value => open,
      buffer_value => open);
  component_width_child: component_runtime_width
    generic map (width => 4)
    port map (
      input_value => component_width_input,
      output_value => component_width_output);
  component_width_twin: component_runtime_width
    generic map (width => 4)
    port map (
      input_value => component_width_twin_input,
      output_value => component_width_twin_output);
  direct_width_child: entity work.component_runtime_width(rtl)
    generic map (width => 8)
    port map (
      input_value => direct_width_input,
      output_value => direct_width_output);
  direct_default_omitted: entity work.component_runtime_defaulted(rtl)
    port map (entity_default_output => open);
  direct_default_open: entity work.component_runtime_defaulted(rtl)
    port map (
      entity_default_input => open,
      entity_default_output => open);
  direct_dependent_default: entity work.component_runtime_generic_defaults(rtl)
    port map (output_value => direct_dependent_default_output);
  direct_dependent_open: entity work.component_runtime_generic_defaults(rtl)
    generic map (
      base_value => 3,
      derived_value => open)
    port map (output_value => direct_dependent_open_output);
  component_dependent_default: component_runtime_generic_defaults
    port map (component_output => component_dependent_default_output);
  aggregate_generic_default: entity work.component_runtime_aggregate_generic(rtl)
    port map (output_value => aggregate_generic_default_output);
  aggregate_generic_explicit: entity work.component_runtime_aggregate_generic(rtl)
    generic map (mask_value => (1 => '1', others => '0'))
    port map (output_value => aggregate_generic_explicit_output);
  static_generic_default: entity work.component_runtime_static_defaults(rtl)
    port map (output_value => static_generic_default_output);
  static_generic_override: entity work.component_runtime_static_defaults(rtl)
    generic map (
      package_value => 3,
      length_value => open,
      converted_value => open,
      function_value => open)
    port map (output_value => static_generic_override_output);
end architecture;
)";
    assert(output.good());
  };

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_profiles(false);
    write_default_profiles(7);
    write_hierarchy(false);
    const auto config =
        make_config(
            directory.path,
            leaf,
            profiles,
            default_profiles,
            hierarchy,
            optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(reference);
    verify(cold);
    verify(warm);
    assert(reference.values == cold.values);
    assert(cold.values == warm.values);
    assert(reference.nonvalue == cold.nonvalue);
    assert(cold.nonvalue == warm.nonvalue);
    assert(reference.defaulted == cold.defaulted);
    assert(cold.defaulted == warm.defaulted);
    assert(reference.expression_inputs == cold.expression_inputs);
    assert(cold.expression_inputs == warm.expression_inputs);
    assert(reference.direct_default_inputs == cold.direct_default_inputs);
    assert(cold.direct_default_inputs == warm.direct_default_inputs);
    assert(reference.open_mode_outputs == cold.open_mode_outputs);
    assert(cold.open_mode_outputs == warm.open_mode_outputs);
    assert(reference.dependent_port_outputs
           == cold.dependent_port_outputs);
    assert(cold.dependent_port_outputs
           == warm.dependent_port_outputs);
    assert(reference.vcd == cold.vcd);
    assert(cold.vcd == warm.vcd);
    assert(reference.dependent_generic_outputs
           == cold.dependent_generic_outputs);
    assert(cold.dependent_generic_outputs
           == warm.dependent_generic_outputs);
    assert(reference.aggregate_generic_outputs
           == cold.aggregate_generic_outputs);
    assert(cold.aggregate_generic_outputs
           == warm.aggregate_generic_outputs);
    assert(reference.static_generic_outputs
           == cold.static_generic_outputs);
    assert(cold.static_generic_outputs
           == warm.static_generic_outputs);
    assert(reference.dynamic_expression == cold.dynamic_expression);
    assert(cold.dynamic_expression == warm.dynamic_expression);
    assert(reference.composite_expression_outputs
           == cold.composite_expression_outputs);
    assert(cold.composite_expression_outputs
           == warm.composite_expression_outputs);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores == cold.cache.misses);
    assert(
        key_for(cold, "component_runtime_top.component_width_child")
        == key_for(cold, "component_runtime_top.component_width_twin"));
    assert(
        key_for(cold, "component_runtime_top.component_width_child")
        != key_for(cold, "component_runtime_top.direct_width_child"));
    assert(warm.cache.hits > 0);
    assert(warm.cache.misses == 0);

    write_default_profiles(11);
    const auto default_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto default_changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(default_reference, 27, 11);
    verify(default_changed, 27, 11);
    assert(default_reference.defaulted
           == default_changed.defaulted);
    for (const auto path : {
             "component_runtime_top",
             "component_runtime_top.defaulted_child"}) {
      assert(
          key_for(cold, path)
          != key_for(default_changed, path));
    }
    for (const auto path : {
             "component_runtime_top.positional_child",
             "component_runtime_top.default_child",
             "component_runtime_top.direct_child",
             "component_runtime_top.nonvalue_child"}) {
      assert(
          key_for(cold, path)
          == key_for(default_changed, path));
    }
    assert(default_changed.cache.misses > 0);
    assert(default_changed.cache.hits > 0);

    write_default_profiles(7);
    write_hierarchy(true);
    const auto function_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto function_changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(function_reference, 30);
    verify(function_changed, 30);
    assert(function_reference.nonvalue
           == function_changed.nonvalue);
    for (const auto path : {
             "component_runtime_top",
             "component_runtime_top.nonvalue_child"}) {
      assert(
          key_for(cold, path)
          != key_for(function_changed, path));
    }
    for (const auto path : {
             "component_runtime_top.positional_child",
             "component_runtime_top.default_child",
             "component_runtime_top.direct_child",
             "component_runtime_top.defaulted_child"}) {
      assert(
          key_for(cold, path)
          == key_for(function_changed, path));
    }
    assert(function_changed.cache.misses > 0);
    assert(function_changed.cache.hits > 0);

    write_profiles(true);
    const auto changed_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(changed_reference, 30);
    verify(changed, 30);
    for (const auto path : {
             "component_runtime_top",
             "component_runtime_top.positional_child",
             "component_runtime_top.default_child",
             "component_runtime_top.nonvalue_child"}) {
      assert(
          key_for(function_changed, path)
          != key_for(changed, path));
    }
    assert(
        key_for(function_changed, "component_runtime_top.direct_child")
        == key_for(changed, "component_runtime_top.direct_child"));
    assert(
        key_for(
            function_changed,
            "component_runtime_top.defaulted_child")
        == key_for(
            changed,
            "component_runtime_top.defaulted_child"));
#endif
  }
  return 0;
}
