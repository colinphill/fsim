// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "path_test_support.hpp"

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

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& body_values_source,
    const std::filesystem::path& package_declaration_source,
    const std::filesystem::path& package_body_source,
    const std::filesystem::path& leaf_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-function-generics";
  config.project.top = "vhdl:work.function_top(rtl)";
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
      body_values_source,
      package_declaration_source,
      package_body_source,
      leaf_source,
      top_source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

std::string bits(const std::uint32_t value) {
  return std::bitset<32>{value}.to_string();
}

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
  assert(project->design.specializations().size() == 7);
  const auto package_specialization = std::ranges::find_if(
      project->design.specializations(),
      [](const auto& specialization) {
        return specialization.instance
            == "function_top.package_instance.nested";
      });
  assert(
      package_specialization
      != project->design.specializations().end());
  assert(fsim::test::has_source_dependency(
      package_specialization->source_dependencies,
      config.source_sets.front().files[2]));
  assert(fsim::test::has_source_dependency(
      package_specialization->source_dependencies,
      config.source_sets.front().files[0]));

  Capture capture;
  for (std::size_t index = 0;
       index < project->specialization_cache_keys.size();
       ++index) {
    capture.keys.emplace_back(
        project->design.specializations()[index].instance,
        project->specialization_cache_keys[index]);
  }

  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.cache = simulation.native_cache_statistics();
  simulation.set_execution_point_hook(
      [&capture](
          fsim::runtime::Scheduler&,
          const fsim::runtime::simir::ExecutionPoint& point) {
        capture.points.push_back(point);
      });

  constexpr std::array<std::string_view, 4> inputs{
      "function_top.direct_input",
      "function_top.nested_instance.input_value",
      "function_top.boxed_input",
      "function_top.package_instance.input_value"};
  constexpr std::array<std::string_view, 4> outputs{
      "function_top.direct_output",
      "function_top.nested_instance.output_value",
      "function_top.boxed_output",
      "function_top.package_instance.output_value"};
  constexpr std::array<std::uint32_t, 4> values{5, 10, 20, 30};
  std::array<fsim::runtime::simir::SignalId, 4> output_ids{};
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

  capture.result = simulation.run();
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(output_ids[index]).to_msb_string();
  }
  return capture;
}

void verify(
    const Capture& capture,
    const std::uint32_t increment,
    const std::uint32_t package_increment = 2) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 0);
  assert((capture.values
          == std::array<std::string, 4>{
              bits(5 + increment),
              bits(10 + increment),
              bits(20 + increment),
              bits(30 + package_increment)}));
  assert(
      std::ranges::count_if(
          capture.points,
          [](const auto& point) {
            return point.kind
                == fsim::runtime::simir::
                    ExecutionPointKind::call;
          })
      == 4);
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

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-function-generic-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto leaf_source = directory.path / "function_leaf.vhd";
  const auto body_values_source =
      directory.path / "function_body_values.vhd";
  const auto package_declaration_source =
      directory.path / "function_package.vhd";
  const auto package_body_source =
      directory.path / "function_package_body.vhd";
  const auto top_source = directory.path / "function_top.vhd";

  const auto write_body_values =
      [&](const std::uint32_t increment) {
        std::ofstream output(
            body_values_source,
            std::ios::binary | std::ios::trunc);
        output << R"(
package function_body_values is
  constant body_increment : integer := )"
               << increment << R"(;
end package;
)";
        assert(output.good());
      };
  {
    std::ofstream output(
        package_declaration_source, std::ios::binary);
    output << R"(
package function_pkg is
  function package_increment(value : integer) return integer;
end package;
)";
    assert(output.good());
  }
  {
    std::ofstream output(package_body_source, std::ios::binary);
    output << R"(
use work.function_body_values.all;
package body function_pkg is
  function package_increment(value : integer) return integer is
  begin
    return value + body_increment;
  end function;
end package body;
)";
    assert(output.good());
  }
  {
    std::ofstream output(leaf_source, std::ios::binary);
    output << R"(
entity function_leaf is
  generic (
    function transform(value : integer) return integer;
    Width : positive := transform(3));
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of function_leaf is
begin
  output_value <= transform(input_value);
end architecture;

entity boxed_leaf is
  generic (
    function transform(value : integer) return integer is <>);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of boxed_leaf is
begin
  output_value <= transform(input_value);
end architecture;

entity function_wrapper is
  generic (
    function forwarded(value : integer) return integer);
end entity;
architecture rtl of function_wrapper is
  signal input_value : integer;
  signal output_value : integer;
begin
  nested: entity work.function_leaf(rtl)
    generic map (forwarded)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;

entity package_wrapper is
end entity;
use work.function_pkg.all;
architecture rtl of package_wrapper is
  signal input_value : integer;
  signal output_value : integer;
begin
  nested: entity work.function_leaf(rtl)
    generic map (package_increment)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;
)";
    assert(output.good());
  }

  const auto write_top =
      [&](const std::uint32_t increment) {
        std::ofstream output(
            top_source,
            std::ios::binary | std::ios::trunc);
        output << R"(
entity function_top is
end entity;
architecture rtl of function_top is
  function increment(value : integer) return integer is
  begin
    return value + )"
               << increment << R"(;
  end function;
  signal direct_input : integer;
  signal direct_output : integer;
  signal boxed_input : integer;
  signal boxed_output : integer;
begin
  direct_instance: entity work.function_leaf(rtl)
    generic map (increment)
    port map (
      input_value => direct_input,
      output_value => direct_output);
  nested_instance: entity work.function_wrapper(rtl)
    generic map (increment)
    port map ();
  boxed_instance: entity work.boxed_leaf(rtl)
    port map (
      input_value => boxed_input,
      output_value => boxed_output);
  package_instance: entity work.package_wrapper(rtl)
    port map ();
end architecture;
)";
        assert(output.good());
      };

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_body_values(2);
    write_top(1);
    const auto config = make_config(
        directory.path,
        body_values_source,
        package_declaration_source,
        package_body_source,
        leaf_source,
        top_source,
        optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(reference, 1);
    verify(cold, 1);
    verify(warm, 1);
    assert(reference.values == cold.values);
    assert(cold.values == warm.values);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores == cold.cache.misses);
    assert(warm.cache.hits > 0);
    assert(warm.cache.misses == 0);

    write_top(2);
    const auto changed_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(changed_reference, 2);
    verify(changed, 2);
    assert(
        key_for(cold, "function_top.direct_instance")
        != key_for(changed, "function_top.direct_instance"));
    assert(
        key_for(cold, "function_top.nested_instance.nested")
        != key_for(
            changed,
            "function_top.nested_instance.nested"));
    assert(
        key_for(cold, "function_top.boxed_instance")
        != key_for(changed, "function_top.boxed_instance"));
    assert(
        key_for(
            cold,
            "function_top.package_instance.nested")
        == key_for(
            changed,
            "function_top.package_instance.nested"));

    write_body_values(3);
    const auto body_changed_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto body_changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(body_changed_reference, 2, 3);
    verify(body_changed, 2, 3);
    assert(
        key_for(changed, "function_top.direct_instance")
        == key_for(body_changed, "function_top.direct_instance"));
    assert(
        key_for(
            changed,
            "function_top.package_instance.nested")
        != key_for(
            body_changed,
            "function_top.package_instance.nested"));
#endif
  }
  return 0;
}
