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
  std::array<std::string, 5> values;
  std::vector<std::pair<std::string, std::string>> keys;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  fsim::app::NativeCacheStatistics cache;
};

std::string bits(const std::uint32_t value) {
  return std::bitset<32>{value}.to_string();
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_declaration,
    const std::filesystem::path& package_body,
    const std::filesystem::path& hierarchy,
    const std::filesystem::path& provider,
    const std::filesystem::path& top,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-generic-subprograms";
  config.project.top =
      "vhdl:work.generic_subprogram_top(rtl)";
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
      package_declaration,
      package_body,
      hierarchy,
      provider,
      top};
  config.source_sets.push_back(std::move(sources));
  return config;
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
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(
      config, diagnostics);
  if (!project) {
    for (const auto& diagnostic :
         diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  assert(project->design.specializations().size() == 6);

  const auto local = std::ranges::find_if(
      project->design.specializations(),
      [](const auto& specialization) {
        return specialization.instance
            == "generic_subprogram_top.local_instance";
      });
  const auto nested = std::ranges::find_if(
      project->design.specializations(),
      [](const auto& specialization) {
        return specialization.instance
            == "generic_subprogram_top.local_instance"
               ".nested.nested";
      });
  const auto package = std::ranges::find_if(
      project->design.specializations(),
      [](const auto& specialization) {
        return specialization.instance
            == "generic_subprogram_top.package_instance";
      });
  assert(local != project->design.specializations().end());
  assert(nested != project->design.specializations().end());
  assert(package != project->design.specializations().end());
  assert(std::ranges::any_of(
      local->parameter_identity_values,
      [](const auto& value) {
        return value.first == "mapped_two"
            && value.second.find(
                   "vhdl-generic-subprogram-v1")
                != std::string::npos;
      }));
  assert(std::ranges::any_of(
      local->parameter_identity_values,
      [](const auto& value) {
        return value.first == "update_three"
            && value.second.find(
                   "vhdl-generic-subprogram-v1")
                != std::string::npos;
      }));
  assert(std::ranges::any_of(
      nested->parameter_identity_values,
      [](const auto& value) {
        return value.first == "transform"
            && value.second.find(
                   "vhdl-generic-subprogram-v1")
                != std::string::npos;
      }));
  assert(std::ranges::find(
      package->source_dependencies,
      config.source_sets.front().files[1].string())
      != package->source_dependencies.end());

  std::size_t procedure_debug_locals = 0;
  for (const auto& process : project->design.processes()) {
    for (const auto& local_metadata :
         process.debug_locals) {
      procedure_debug_locals +=
          local_metadata.name.find("update_three")
                  != std::string::npos
              ? 1U
              : 0U;
    }
  }
  assert(procedure_debug_locals >= 1);

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

  constexpr std::array<std::string_view, 5> inputs{
      "generic_subprogram_top.direct_input",
      "generic_subprogram_top.nested_input",
      "generic_subprogram_top.procedure_input",
      "generic_subprogram_top.package_input",
      "generic_subprogram_top.stable_input"};
  constexpr std::array<std::string_view, 5> outputs{
      "generic_subprogram_top.direct_output",
      "generic_subprogram_top.nested_output",
      "generic_subprogram_top.procedure_output",
      "generic_subprogram_top.package_output",
      "generic_subprogram_top.stable_output"};
  constexpr std::array<std::uint32_t, 5> values{
      10, 20, 30, 40, 50};
  std::array<fsim::runtime::simir::SignalId, 5>
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
    const std::uint32_t revision) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 0);
  assert((capture.values
          == std::array<std::string, 5>{
              bits(12 + revision),
              bits(22 + revision),
              bits(33 + revision),
              bits(44),
              bits(50)}));
  assert(
      std::ranges::count_if(
          capture.points,
          [](const auto& point) {
            return point.kind
                == fsim::runtime::simir::
                    ExecutionPointKind::call;
          })
      >= 8);
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now()
          .time_since_epoch()
          .count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-generic-subprogram-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const auto package_declaration =
      directory.path / "algorithm_pkg.vhd";
  const auto package_body =
      directory.path / "algorithm_pkg_body.vhd";
  const auto hierarchy =
      directory.path / "generic_hierarchy.vhd";
  const auto provider =
      directory.path / "generic_provider.vhd";
  const auto top =
      directory.path / "generic_top.vhd";

  {
    std::ofstream output(
        package_declaration, std::ios::binary);
    output << R"(
package algorithm_pkg is
  generic (amount : integer := 4)
  function package_add(value : integer) return integer;
  function add_four is new package_add
    generic map (amount => 4);
end package;
)";
    assert(output.good());
  }
  {
    std::ofstream output(package_body, std::ios::binary);
    output << R"(
package body algorithm_pkg is
  generic (amount : integer := 4)
  function package_add(value : integer) return integer is
  begin
    return value + amount;
  end function;
end package body;
)";
    assert(output.good());
  }
  {
    std::ofstream output(hierarchy, std::ios::binary);
    output << R"(
entity generic_leaf is
  generic (
    function transform(value : integer) return integer);
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of generic_leaf is
begin
  output_value <= transform(input_value);
end architecture;

entity generic_wrapper is
  generic (
    function forwarded(value : integer) return integer);
  port (
    input_value : in integer);
end entity;
architecture rtl of generic_wrapper is
  signal output_value : integer;
begin
  nested: entity work.generic_leaf(rtl)
    generic map (transform => forwarded)
    port map (
      input_value => input_value,
      output_value => output_value);
end architecture;

entity package_provider is
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
use work.algorithm_pkg.all;
architecture rtl of package_provider is
begin
  output_value <= add_four(input_value);
end architecture;

entity stable_provider is
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of stable_provider is
begin
  output_value <= input_value;
end architecture;
)";
    assert(output.good());
  }

  const auto write_provider =
      [&](const std::uint32_t revision) {
        std::ofstream output(
            provider,
            std::ios::binary | std::ios::trunc);
        output << R"(
entity local_provider is
  port (
    direct_input : in integer;
    direct_output : out integer;
    nested_input : in integer;
    nested_output : out integer;
    procedure_input : in integer;
    procedure_output : out integer);
end entity;
architecture rtl of local_provider is
  function adjust(value : integer) return integer is
  begin
    return value + )"
               << revision << R"(;
  end function;

  procedure observe(value : integer) is
  begin
    null;
  end procedure;

  generic (
    type item_t;
    amount : integer := 2;
    function apply(value : item_t) return item_t)
  function mapped(value : item_t) return item_t is
  begin
    return apply(value) + amount;
  end function;

  function mapped_two is new mapped
    generic map (
      item_t => integer,
      amount => 2,
      apply => adjust);

  generic (
    type item_t;
    amount : integer := 3;
    function apply(value : item_t) return item_t;
    procedure publish(value : item_t))
  procedure mapped_update(variable value : inout item_t) is
  begin
    value := apply(value) + amount;
    publish(value);
  end procedure;

  procedure update_three is new mapped_update
    generic map (
      item_t => integer,
      amount => 3,
      apply => adjust,
      publish => observe);
begin
  direct_output <= mapped_two(direct_input);
  nested_output <= mapped_two(nested_input);
  nested: entity work.generic_wrapper(rtl)
    generic map (forwarded => mapped_two)
    port map (
      input_value => nested_input);
  process(procedure_input)
    variable value : integer;
  begin
    value := procedure_input;
    update_three(value);
    procedure_output <= value;
  end process;
end architecture;
)";
        assert(output.good());
      };

  {
    std::ofstream output(top, std::ios::binary);
    output << R"(
entity generic_subprogram_top is
end entity;
architecture rtl of generic_subprogram_top is
  signal direct_input : integer;
  signal direct_output : integer;
  signal nested_input : integer;
  signal nested_output : integer;
  signal procedure_input : integer;
  signal procedure_output : integer;
  signal package_input : integer;
  signal package_output : integer;
  signal stable_input : integer;
  signal stable_output : integer;
begin
  local_instance: entity work.local_provider(rtl)
    port map (
      direct_input => direct_input,
      direct_output => direct_output,
      nested_input => nested_input,
      nested_output => nested_output,
      procedure_input => procedure_input,
      procedure_output => procedure_output);
  package_instance: entity work.package_provider(rtl)
    port map (
      input_value => package_input,
      output_value => package_output);
  stable_instance: entity work.stable_provider(rtl)
    port map (
      input_value => stable_input,
      output_value => stable_output);
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_provider(1);
    const auto config = make_config(
        directory.path,
        package_declaration,
        package_body,
        hierarchy,
        provider,
        top,
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

    write_provider(2);
    const auto changed_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(changed_reference, 2);
    verify(changed, 2);
    assert(
        key_for(
            cold,
            "generic_subprogram_top.local_instance")
        != key_for(
            changed,
            "generic_subprogram_top.local_instance"));
    assert(
        key_for(
            cold,
            "generic_subprogram_top.local_instance"
            ".nested.nested")
        != key_for(
            changed,
            "generic_subprogram_top.local_instance"
            ".nested.nested"));
    assert(
        key_for(
            cold,
            "generic_subprogram_top.package_instance")
        == key_for(
            changed,
            "generic_subprogram_top.package_instance"));
    assert(
        key_for(
            cold,
            "generic_subprogram_top.stable_instance")
        == key_for(
            changed,
            "generic_subprogram_top.stable_instance"));
#endif
  }
  return 0;
}
