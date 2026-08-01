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
  std::array<std::string, 5> outputs;
  std::array<std::string, 5> totals;
  std::vector<std::pair<std::string, std::string>> keys;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_declaration_source,
    const std::filesystem::path& package_body_source,
    const std::filesystem::path& leaf_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-procedure-generics";
  config.project.top = "vhdl:work.procedure_top(rtl)";
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
  assert(project->design.specializations().size() == 8);
  const auto package_specialization = std::ranges::find_if(
      project->design.specializations(),
      [](const auto& specialization) {
        return specialization.instance
            == "procedure_top.package_instance.nested";
      });
  assert(
      package_specialization
      != project->design.specializations().end());
  assert(
      std::ranges::find(
          package_specialization->source_dependencies,
          config.source_sets.front().files[1].string())
      != package_specialization->source_dependencies.end());
  std::size_t source_formals = 0;
  std::size_t destination_formals = 0;
  std::size_t accumulator_formals = 0;
  std::size_t procedure_locals = 0;
  for (const auto& process : project->design.processes()) {
    for (const auto& local : process.debug_locals) {
      source_formals += local.name == "selected.source" ? 1U : 0U;
      destination_formals +=
          local.name == "selected.destination" ? 1U : 0U;
      accumulator_formals +=
          local.name == "selected.accumulator" ? 1U : 0U;
      procedure_locals +=
          local.name == "selected.temporary" ? 1U : 0U;
    }
  }
  assert(source_formals == 5);
  assert(destination_formals == 5);
  assert(accumulator_formals == 5);
  assert(procedure_locals == 4);

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

  constexpr std::array<std::string_view, 5> inputs{
      "procedure_top.direct_input",
      "procedure_top.nested_instance.input_value",
      "procedure_top.boxed_input",
      "procedure_top.named_input",
      "procedure_top.package_instance.input_value"};
  constexpr std::array<std::string_view, 5> outputs{
      "procedure_top.direct_output",
      "procedure_top.nested_instance.output_value",
      "procedure_top.boxed_output",
      "procedure_top.named_output",
      "procedure_top.package_instance.output_value"};
  constexpr std::array<std::string_view, 5> totals{
      "procedure_top.direct_total",
      "procedure_top.nested_instance.total_value",
      "procedure_top.boxed_total",
      "procedure_top.named_total",
      "procedure_top.package_instance.total_value"};
  constexpr std::array<std::uint32_t, 5> input_values{
      5, 10, 20, 25, 30};
  constexpr std::array<std::uint32_t, 5> total_values{
      50, 60, 70, 75, 80};
  std::array<fsim::runtime::simir::SignalId, 5> output_ids{};
  std::array<fsim::runtime::simir::SignalId, 5> total_ids{};
  for (std::size_t index = 0; index < inputs.size(); ++index) {
    const auto input = simulation.find_signal(inputs[index]);
    const auto output = simulation.find_signal(outputs[index]);
    const auto total = simulation.find_signal(totals[index]);
    assert(input && output && total);
    simulation.deposit_signal(
        *input,
        fsim::runtime::PackedLogic4::from_msb_string(
            bits(input_values[index])));
    simulation.deposit_signal(
        *total,
        fsim::runtime::PackedLogic4::from_msb_string(
            bits(total_values[index])));
    output_ids[index] = *output;
    total_ids[index] = *total;
  }

  capture.result = simulation.run();
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    capture.outputs[index] =
        simulation.read_signal(output_ids[index]).to_msb_string();
    capture.totals[index] =
        simulation.read_signal(total_ids[index]).to_msb_string();
  }
  return capture;
}

void verify(
    const Capture& capture,
    const std::uint32_t increment) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 0);
  assert((capture.outputs
          == std::array<std::string, 5>{
              bits(5 + increment),
              bits(10 + increment),
              bits(20 + increment),
              bits(25 + increment),
              bits(32)}));
  assert((capture.totals
          == std::array<std::string, 5>{
              bits(50 + 5 + increment),
              bits(60 + 10 + increment),
              bits(70 + 20 + increment),
              bits(75 + 25 + increment),
              bits(80 + 32)}));
  assert(
      std::ranges::count_if(
          capture.points,
          [](const auto& point) {
            return point.kind
                == fsim::runtime::simir::
                    ExecutionPointKind::call;
          })
      == 8);
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
      / ("fsim-vhdl-procedure-generic-"
         + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto leaf_source = directory.path / "procedure_leaf.vhd";
  const auto package_declaration_source =
      directory.path / "procedure_package.vhd";
  const auto package_body_source =
      directory.path / "procedure_package_body.vhd";
  const auto top_source = directory.path / "procedure_top.vhd";

  {
    std::ofstream output(
        package_declaration_source, std::ios::binary);
    output << R"(
package procedure_pkg is
  procedure package_exchange(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer);
end package;
)";
    assert(output.good());
  }
  {
    std::ofstream output(package_body_source, std::ios::binary);
    output << R"(
package body procedure_pkg is
  procedure package_exchange(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer) is
  begin
    destination := source + 2;
    accumulator := accumulator + destination;
  end procedure;
end package body;
)";
    assert(output.good());
  }
  {
    std::ofstream output(leaf_source, std::ios::binary);
    output << R"(
entity procedure_leaf is
  generic (
    procedure selected(
      source : in integer;
      variable destination : out integer;
      variable accumulator : inout integer));
  port (
    input_value : in integer;
    output_value : out integer;
    total_value : inout integer);
end entity;
architecture rtl of procedure_leaf is
  procedure invoke(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer) is
  begin
    selected(source, destination, accumulator);
  end procedure;
begin
  process(input_value)
  begin
    invoke(input_value, output_value, total_value);
  end process;
end architecture;

entity boxed_leaf is
  generic (
    procedure selected(
      source : in integer;
      variable destination : out integer;
      variable accumulator : inout integer) is <>);
  port (
    input_value : in integer;
    output_value : out integer;
    total_value : inout integer);
end entity;
architecture rtl of boxed_leaf is
begin
  process(input_value)
  begin
    selected(
      source => input_value,
      destination => output_value,
      accumulator => total_value);
  end process;
end architecture;

entity named_leaf is
  generic (
    procedure selected(
      source : in integer;
      variable destination : out integer;
      variable accumulator : inout integer) is exchange);
  port (
    input_value : in integer;
    output_value : out integer;
    total_value : inout integer);
end entity;
architecture rtl of named_leaf is
begin
  process(input_value)
  begin
    selected(input_value, output_value, total_value);
  end process;
end architecture;

entity procedure_wrapper is
  generic (
    procedure forwarded(
      source : in integer;
      variable destination : out integer;
      variable accumulator : inout integer));
end entity;
architecture rtl of procedure_wrapper is
  signal input_value : integer;
  signal output_value : integer;
  signal total_value : integer;
begin
  nested: entity work.procedure_leaf(rtl)
    generic map (forwarded)
    port map (
      input_value => input_value,
      output_value => output_value,
      total_value => total_value);
end architecture;

entity package_wrapper is
end entity;
use work.procedure_pkg.all;
architecture rtl of package_wrapper is
  signal input_value : integer;
  signal output_value : integer;
  signal total_value : integer;
begin
  nested: entity work.procedure_leaf(rtl)
    generic map (package_exchange)
    port map (
      input_value => input_value,
      output_value => output_value,
      total_value => total_value);
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
entity procedure_top is
end entity;
architecture rtl of procedure_top is
  procedure exchange(
    source : in integer;
    variable destination : out integer;
    variable accumulator : inout integer) is
    variable temporary : integer := source;
  begin
    destination := temporary + )"
               << increment << R"(;
    accumulator := accumulator + destination;
  end procedure;
  signal direct_input : integer;
  signal direct_output : integer;
  signal direct_total : integer;
  signal boxed_input : integer;
  signal boxed_output : integer;
  signal boxed_total : integer;
  signal named_input : integer;
  signal named_output : integer;
  signal named_total : integer;
begin
  direct_instance: entity work.procedure_leaf(rtl)
    generic map (selected => exchange)
    port map (
      input_value => direct_input,
      output_value => direct_output,
      total_value => direct_total);
  nested_instance: entity work.procedure_wrapper(rtl)
    generic map (exchange)
    port map ();
  boxed_instance: entity work.boxed_leaf(rtl)
    port map (
      input_value => boxed_input,
      output_value => boxed_output,
      total_value => boxed_total);
  named_instance: entity work.named_leaf(rtl)
    port map (
      input_value => named_input,
      output_value => named_output,
      total_value => named_total);
  package_instance: entity work.package_wrapper(rtl)
    port map ();
end architecture;
)";
        assert(output.good());
      };

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_top(1);
    const auto config = make_config(
        directory.path,
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
    assert(reference.outputs == cold.outputs);
    assert(reference.totals == cold.totals);
    assert(cold.outputs == warm.outputs);
    assert(cold.totals == warm.totals);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores == cold.cache.misses);
    assert(warm.cache.hits > 0);
    assert(warm.cache.misses == 0);

    write_top(3);
    const auto changed_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(changed_reference, 3);
    verify(changed, 3);
    assert(
        key_for(cold, "procedure_top.direct_instance")
        != key_for(changed, "procedure_top.direct_instance"));
    assert(
        key_for(
            cold, "procedure_top.nested_instance.nested")
        != key_for(
            changed, "procedure_top.nested_instance.nested"));
    assert(
        key_for(cold, "procedure_top.boxed_instance")
        != key_for(changed, "procedure_top.boxed_instance"));
    assert(
        key_for(cold, "procedure_top.named_instance")
        != key_for(changed, "procedure_top.named_instance"));
    assert(
        key_for(
            cold, "procedure_top.package_instance.nested")
        == key_for(
            changed, "procedure_top.package_instance.nested"));
#endif
  }
  return 0;
}
