// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
      leaf, profiles, default_profiles, hierarchy};
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
  assert(project->design.specializations().size() == 6);
  for (const auto path : {
           "component_runtime_top.positional_child",
           "component_runtime_top.default_child"}) {
    const auto& selected = specialization(*project, path);
    assert(
        selected.unit
        == "vhdl:work.component_runtime_leaf(rtl)");
    assert(std::ranges::find(
               selected.source_dependencies,
               config.source_sets.front().files[1].string())
           != selected.source_dependencies.end());
    assert(std::ranges::any_of(
        selected.parameter_identity_values,
        [](const auto& item) {
          return item.first == "__component"
              && item.second.starts_with(
                  "vhdl-component-binding-v5")
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
  assert(std::ranges::find(
             specialization(
                 *project,
                 "component_runtime_top.direct_child")
                 .source_dependencies,
             config.source_sets.front().files[1].string())
         == specialization(
                *project,
                "component_runtime_top.direct_child")
                .source_dependencies.end());
  const auto& nonvalue = specialization(
      *project, "component_runtime_top.nonvalue_child");
  assert(
      nonvalue.unit
      == "vhdl:work.component_runtime_nonvalue(rtl)");
  for (const auto& dependency :
       {config.source_sets.front().files[1].string(),
        config.source_sets.front().files[3].string()}) {
    assert(std::ranges::find(
               nonvalue.source_dependencies,
               dependency)
           != nonvalue.source_dependencies.end());
  }
  assert(std::ranges::any_of(
      nonvalue.parameter_identity_values,
      [](const auto& item) {
        return item.first == "__component"
            && item.second.starts_with(
                "vhdl-component-binding-v5")
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
  assert(std::ranges::find(
             defaulted.source_dependencies,
             config.source_sets.front().files[2].string())
         != defaulted.source_dependencies.end());
  assert(std::ranges::any_of(
      defaulted.parameter_identity_values,
      [](const auto& item) {
        return item.first == "__component"
            && item.second.starts_with(
                "vhdl-component-binding-v5")
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
  const auto nonvalue_input = simulation.find_signal(
      "component_runtime_top.nonvalue_input");
  const auto nonvalue_output = simulation.find_signal(
      "component_runtime_top.nonvalue_output");
  const auto defaulted_output = simulation.find_signal(
      "component_runtime_top.defaulted_child."
      "entity_default_output");
  assert(
      nonvalue_input && nonvalue_output
      && defaulted_output);
  simulation.deposit_signal(
      *nonvalue_input,
      fsim::runtime::PackedLogic4::from_msb_string(bits(7)));
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
  capture.nonvalue =
      simulation.read_signal(*nonvalue_output).to_msb_string();
  capture.defaulted =
      simulation.read_signal(*defaulted_output).to_msb_string();
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
    entity_default_input : in integer;
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
end package;
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
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores == cold.cache.misses);
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
}
