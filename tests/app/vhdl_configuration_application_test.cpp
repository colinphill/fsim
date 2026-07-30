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
  std::array<std::string, 4> values;
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
    const std::filesystem::path& hierarchy,
    const std::filesystem::path& wrapper_configuration,
    const std::filesystem::path& configuration,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-configurations";
  config.project.top = "vhdl:work.runtime_configuration";
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
      leaf, hierarchy, wrapper_configuration, configuration};
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
    const std::string_view instance) {
  const auto found = std::ranges::find_if(
      capture.keys,
      [&](const auto& item) {
        return item.first == instance;
      });
  assert(found != capture.keys.end());
  return found->second;
}
#endif

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const bool fast_wrapper = true) {
  fsim::diagnostic::Engine diagnostics;
  auto project =
      fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic :
         diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  assert(project->design.specializations().size() == 7);
  assert(
      specialization(
          *project,
          "runtime_configuration.direct_child").unit
      == "vhdl:work.configuration_leaf(rtl)");
  assert(
      specialization(
          *project,
          "runtime_configuration.wrapper_child.nested").unit
      == std::string{"vhdl:work.configuration_leaf("}
             + (fast_wrapper ? "fast)" : "rtl)"));
  const auto& root =
      specialization(*project, "runtime_configuration");
  assert(std::ranges::any_of(
      root.parameter_identity_values,
      [](const auto& item) {
        return item.first == "__configuration"
            && item.second.find(
                   "vhdl-configuration-v2")
                != std::string::npos;
      }));
  assert(std::ranges::find(
      root.source_dependencies,
      config.source_sets.front().files[3].string())
      != root.source_dependencies.end());
  for (const auto path : {
           "runtime_configuration.configured_child",
           "runtime_configuration.remaining_child"}) {
    assert(std::ranges::find(
        specialization(*project, path).source_dependencies,
        config.source_sets.front().files[3].string())
        != specialization(*project, path)
               .source_dependencies.end());
  }
  assert(std::ranges::find(
      specialization(
          *project,
          "runtime_configuration.direct_child")
          .source_dependencies,
      config.source_sets.front().files[3].string())
      == specialization(
             *project,
             "runtime_configuration.direct_child")
             .source_dependencies.end());
  for (const auto path : {
           "runtime_configuration.wrapper_child",
           "runtime_configuration.wrapper_child.nested"}) {
    assert(std::ranges::find(
        specialization(*project, path).source_dependencies,
        config.source_sets.front().files[2].string())
        != specialization(*project, path)
               .source_dependencies.end());
  }

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

  constexpr std::array<std::string_view, 4> inputs{
      "runtime_configuration.configured_input",
      "runtime_configuration.remaining_input",
      "runtime_configuration.direct_input",
      "runtime_configuration.stable_input"};
  constexpr std::array<std::string_view, 4> outputs{
      "runtime_configuration.configured_output",
      "runtime_configuration.remaining_output",
      "runtime_configuration.direct_output",
      "runtime_configuration.stable_output"};
  constexpr std::array<std::uint32_t, 4> values{
      10, 20, 30, 40};
  std::array<fsim::runtime::simir::SignalId, 4>
      output_ids{};
  for (std::size_t index = 0;
       index < inputs.size(); ++index) {
    const auto input = simulation.find_signal(inputs[index]);
    const auto output = simulation.find_signal(outputs[index]);
    assert(input && output);
    simulation.deposit_signal(
        *input,
        fsim::runtime::PackedLogic4::from_msb_string(
            bits(values[index])));
    output_ids[index] = *output;
  }

  capture.result = simulation.run();
  for (std::size_t index = 0;
       index < output_ids.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(output_ids[index])
            .to_msb_string();
  }
  return capture;
}

void verify(
    const Capture& capture,
    const bool fast_configured) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 0);
  assert((capture.values
          == std::array<std::string, 4>{
              bits(fast_configured ? 113 : 13),
              bits(fast_configured ? 24 : 124),
              bits(35),
              bits(40)}));
  assert(
      std::ranges::count_if(
          capture.points,
          [](const auto& point) {
            return point.kind
                == fsim::runtime::simir::
                    ExecutionPointKind::statement;
          })
      >= 4);
  assert(std::ranges::any_of(
      capture.points,
      [](const auto& point) {
        return point.source.path.find(
                   "configuration_leaf.vhd")
            != std::string::npos;
      }));
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now()
          .time_since_epoch()
          .count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-configuration-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const auto leaf =
      directory.path / "configuration_leaf.vhd";
  const auto hierarchy =
      directory.path / "configuration_hierarchy.vhd";
  const auto wrapper_configuration =
      directory.path / "wrapper_configuration.vhd";
  const auto configuration =
      directory.path / "runtime_configuration.vhd";

  {
    std::ofstream output(leaf, std::ios::binary);
    output << R"(
entity configuration_leaf is
  generic (amount : integer := 1);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of configuration_leaf is
begin
  output_value <= input_value + amount;
end architecture;
architecture fast of configuration_leaf is
begin
  output_value <= input_value + amount + 100;
end architecture;

entity configuration_stable is
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of configuration_stable is
begin
  output_value <= input_value;
end architecture;
)";
    assert(output.good());
  }

  {
    std::ofstream output(hierarchy, std::ios::binary);
    output << R"(
entity configuration_wrapper is
end entity;
architecture rtl of configuration_wrapper is
  signal input_value : integer;
  signal output_value : integer;
  component configuration_leaf is
    generic (amount : integer := 1);
    port (
      input_value : in integer;
      output_value : out integer);
  end component;
  for all : configuration_leaf
    use entity work.configuration_leaf(fast);
begin
  nested: configuration_leaf
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;

entity configuration_top is
end entity;
architecture rtl of configuration_top is
  signal configured_input : integer;
  signal configured_output : integer;
  signal remaining_input : integer;
  signal remaining_output : integer;
  signal direct_input : integer;
  signal direct_output : integer;
  signal stable_input : integer;
  signal stable_output : integer;
  component configuration_leaf is
    generic (component_amount : integer := 1);
    port (
      component_input : in integer;
      component_output : out integer);
  end component;
  component configuration_wrapper is
  end component;
  for wrapper_child : configuration_wrapper
    use configuration work.wrapper_configuration;
begin
  configured_child: configuration_leaf
    generic map (component_amount => 3)
    port map (
      component_input => configured_input,
      component_output => configured_output);
  remaining_child: configuration_leaf
    generic map (component_amount => 4)
    port map (
      component_input => remaining_input,
      component_output => remaining_output);
  direct_child: entity work.configuration_leaf(rtl)
    generic map (amount => 5)
    port map (
      input_value => direct_input,
      output_value => direct_output);
  stable_child: entity work.configuration_stable(rtl)
    port map (
      input_value => stable_input,
      output_value => stable_output);
  wrapper_child: configuration_wrapper
    port map ();
end architecture;
)";
    assert(output.good());
  }

  const auto write_wrapper_configuration =
      [&](const bool fast_nested) {
        std::ofstream output(
            wrapper_configuration,
            std::ios::binary | std::ios::trunc);
        output << R"(
configuration wrapper_configuration of configuration_wrapper is
  for rtl
    for all : configuration_leaf
      use entity work.configuration_leaf()"
               << (fast_nested ? "fast" : "rtl")
               << R"();
    end for;
  end for;
end configuration;
)";
        assert(output.good());
      };

  const auto write_configuration =
      [&](const bool fast_configured) {
        std::ofstream output(
            configuration,
            std::ios::binary | std::ios::trunc);
        output << R"(
configuration runtime_configuration of configuration_top is
  for rtl
    for configured_child : configuration_leaf
      use entity work.configuration_leaf()"
               << (fast_configured ? "fast" : "rtl")
               << R"()
        generic map (amount => component_amount)
        port map (
          input_value => component_input,
          output_value => component_output);
    end for;
    for others : configuration_leaf
      use entity work.configuration_leaf()"
               << (fast_configured ? "rtl" : "fast")
               << R"();
    end for;
  end for;
end configuration;
)";
        assert(output.good());
      };

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_wrapper_configuration(true);
    write_configuration(true);
    const auto config = make_config(
        directory.path,
        leaf,
        hierarchy,
        wrapper_configuration,
        configuration,
        optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(reference, true);
    verify(cold, true);
    verify(warm, true);
    assert(reference.values == cold.values);
    assert(cold.values == warm.values);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores == cold.cache.misses);
    assert(warm.cache.hits > 0);
    assert(warm.cache.misses == 0);

    write_configuration(false);
    const auto changed_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(changed_reference, false);
    verify(changed, false);
    for (const auto path : {
             "runtime_configuration",
             "runtime_configuration.configured_child",
             "runtime_configuration.remaining_child"}) {
      assert(
          key_for(cold, path)
          != key_for(changed, path));
    }
    for (const auto path : {
             "runtime_configuration.direct_child",
             "runtime_configuration.stable_child",
             "runtime_configuration.wrapper_child",
             "runtime_configuration.wrapper_child.nested"}) {
      assert(
          key_for(cold, path)
          == key_for(changed, path));
    }

    write_wrapper_configuration(false);
    const auto nested_changed_reference = run_once(
        config,
        fsim::app::SimulationEngine::interpreter,
        false);
    const auto nested_changed = run_once(
        config,
        fsim::app::SimulationEngine::compiled,
        false);
    verify(nested_changed_reference, false);
    verify(nested_changed, false);
    for (const auto path : {
             "runtime_configuration.wrapper_child",
             "runtime_configuration.wrapper_child.nested"}) {
      assert(
          key_for(changed, path)
          != key_for(nested_changed, path));
    }
    for (const auto path : {
             "runtime_configuration",
             "runtime_configuration.configured_child",
             "runtime_configuration.remaining_child",
             "runtime_configuration.direct_child",
             "runtime_configuration.stable_child"}) {
      assert(
          key_for(changed, path)
          == key_for(nested_changed, path));
    }
    assert(nested_changed.cache.hits > 0);
    assert(nested_changed.cache.misses > 0);
#endif
  }
}
