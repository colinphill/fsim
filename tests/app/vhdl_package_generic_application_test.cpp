// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "path_test_support.hpp"

#include <algorithm>
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
  std::string output;
  std::string total;
  std::vector<std::pair<std::string, std::string>> keys;
  std::vector<fsim::runtime::simir::ExecutionPoint> points;
  fsim::app::NativeCacheStatistics cache;
};

std::string bits(const std::uint32_t value) {
  return std::bitset<32>{value}.to_string();
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& declaration,
    const std::filesystem::path& body,
    const std::filesystem::path& leaf,
    const std::filesystem::path& top,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-package-generics";
  config.project.top = "vhdl:work.package_top(rtl)";
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
  sources.files = {declaration, body, leaf, top};
  config.source_sets.push_back(std::move(sources));
  return config;
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
  assert(project->design.specializations().size() == 5);

  const auto direct = std::ranges::find_if(
      project->design.specializations(),
      [](const auto& specialization) {
        return specialization.instance
            == "package_top.direct_instance";
      });
  const auto nested = std::ranges::find_if(
      project->design.specializations(),
      [](const auto& specialization) {
        return specialization.instance
            == "package_top.wrapper_instance.nested";
      });
  assert(direct != project->design.specializations().end());
  assert(nested != project->design.specializations().end());
  assert(std::ranges::any_of(
      direct->parameter_identity_values,
      [](const auto& value) {
        return value.first == "api"
            && value.second.find("work.service_template")
                != std::string::npos;
      }));
  assert(fsim::test::has_source_dependency(
      direct->source_dependencies,
      config.source_sets.front().files[1]));
  assert(fsim::test::has_source_dependency(
      nested->source_dependencies,
      config.source_sets.front().files[1]));

  std::size_t package_procedure_formals = 0;
  for (const auto& process : project->design.processes()) {
    for (const auto& local : process.debug_locals) {
      package_procedure_formals +=
          local.name.find("api.update.")
                  != std::string::npos
              ? 1U
              : 0U;
    }
  }
  assert(package_procedure_formals >= 2);

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

  const auto input =
      simulation.find_signal("package_top.direct_input");
  const auto output =
      simulation.find_signal("package_top.direct_output");
  const auto total =
      simulation.find_signal("package_top.direct_total");
  assert(input && output && total);
  simulation.deposit_signal(
      *input,
      fsim::runtime::PackedLogic4::from_msb_string(bits(5)));
  simulation.deposit_signal(
      *total,
      fsim::runtime::PackedLogic4::from_msb_string(bits(50)));
  capture.result = simulation.run();
  capture.output =
      simulation.read_signal(*output).to_msb_string();
  capture.total =
      simulation.read_signal(*total).to_msb_string();
  return capture;
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

void verify(
    const Capture& capture,
    const std::uint32_t body_increment) {
  const auto adjusted = 5U + 1U + body_increment + 5U;
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.output == bits(adjusted));
  assert(capture.total == bits(adjusted));
  assert(std::ranges::count_if(
      capture.points,
      [](const auto& point) {
        return point.kind
            == fsim::runtime::simir::ExecutionPointKind::call;
      }) >= 4);
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-package-generic-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto declaration =
      directory.path / "service_template.vhd";
  const auto body =
      directory.path / "service_template_body.vhd";
  const auto leaf =
      directory.path / "package_leaf.vhd";
  const auto top =
      directory.path / "package_top.vhd";

  {
    std::ofstream output(declaration, std::ios::binary);
    output << R"(
package service_template is
  generic (
    offset : integer := 4;
    type item_type;
    function transform(value : item_type) return item_type;
    procedure accumulate(
      value : item_type;
      variable total : inout item_type));
  subtype value_type is item_type;
  constant seed : integer := offset + 1;
  function apply(value : item_type) return item_type;
  procedure update(
    value : item_type;
    variable total : inout item_type);
end package;
)";
    assert(output.good());
  }

  const auto write_body =
      [&](const std::uint32_t increment) {
        std::ofstream output(
            body, std::ios::binary | std::ios::trunc);
        output << R"(
package body service_template is
  function apply(value : item_type) return item_type is
  begin
    return transform(value) + )"
               << increment << R"( + seed;
  end function;
  procedure update(
    value : item_type;
    variable total : inout item_type) is
  begin
    accumulate(apply(value), total);
  end procedure;
end package body;
)";
        assert(output.good());
      };

  {
    std::ofstream output(leaf, std::ios::binary);
    output << R"(
entity package_leaf is
  generic (
    package api is new work.service_template
      generic map (<>));
  port (
    input_value : in api.value_type;
    output_value : out api.value_type;
    total_value : inout api.value_type);
end entity;
architecture rtl of package_leaf is
begin
  process(input_value)
  begin
    output_value <= api.apply(input_value);
    api.update(input_value, total_value);
  end process;
end architecture;

entity package_wrapper is
  generic (
    package forwarded is new work.service_template
      generic map (<>));
end entity;
architecture rtl of package_wrapper is
  signal input_value : integer;
  signal output_value : integer;
  signal total_value : integer;
begin
  nested: entity work.package_leaf(rtl)
    generic map (api => forwarded)
    port map (
      input_value => input_value,
      output_value => output_value,
      total_value => total_value);
end architecture;

entity stable_leaf is
end entity;
architecture rtl of stable_leaf is
begin
end architecture;
)";
    assert(output.good());
  }

  {
    std::ofstream output(top, std::ios::binary);
    output << R"(
entity package_top is
end entity;
architecture rtl of package_top is
  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure set_total(
    value : integer;
    variable total : inout integer) is
  begin
    total := value;
  end procedure;
  package selected is new work.service_template
    generic map (
      offset => 4,
      item_type => integer,
      transform => increment,
      accumulate => set_total);
  signal direct_input : integer;
  signal direct_output : integer;
  signal direct_total : integer;
begin
  direct_instance: entity work.package_leaf(rtl)
    generic map (api => selected)
    port map (
      input_value => direct_input,
      output_value => direct_output,
      total_value => direct_total);
  wrapper_instance: entity work.package_wrapper(rtl)
    generic map (forwarded => selected)
    port map ();
  stable_instance: entity work.stable_leaf(rtl)
    port map ();
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_body(0);
    const auto config = make_config(
        directory.path,
        declaration,
        body,
        leaf,
        top,
        optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(reference, 0);
    verify(cold, 0);
    verify(warm, 0);
    assert(reference.output == cold.output);
    assert(reference.total == cold.total);
    assert(cold.output == warm.output);
    assert(cold.total == warm.total);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores == cold.cache.misses);
    assert(warm.cache.hits > 0);
    assert(warm.cache.misses == 0);

    write_body(2);
    const auto changed_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(changed_reference, 2);
    verify(changed, 2);
    assert(
        key_for(cold, "package_top.direct_instance")
        != key_for(changed, "package_top.direct_instance"));
    assert(
        key_for(
            cold, "package_top.wrapper_instance.nested")
        != key_for(
            changed, "package_top.wrapper_instance.nested"));
    assert(
        key_for(cold, "package_top.stable_instance")
        == key_for(changed, "package_top.stable_instance"));
#endif
  }
  return 0;
}
