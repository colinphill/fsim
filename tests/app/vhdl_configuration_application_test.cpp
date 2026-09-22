// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "path_test_support.hpp"

#include "../../src/app/application_internal.hpp"

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
  std::string resolved_sub_elements;
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
    const std::filesystem::path& wrapper_hierarchy,
    const std::filesystem::path& wrapper_configuration,
    const std::filesystem::path& top_hierarchy,
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
      leaf, wrapper_hierarchy, wrapper_configuration,
      top_hierarchy, configuration};
  config.source_sets.push_back(std::move(sources));
  return config;
}

fsim::elaboration::ElaborationResult elaborate_compiled(
    const fsim::semantic::CompiledDesign& compiled) {
  const std::array<fsim::elaboration::Root, 1> roots{{{
      "vhdl:work.runtime_configuration",
      "runtime_configuration"}}};
  return fsim::elaboration::elaborate(
      compiled,
      roots,
      std::span<const fsim::elaboration::Binding>{},
      std::span<const fsim::elaboration::SystemCInstanceDescription>{},
      nullptr,
      std::span<const std::string>{});
}

std::vector<std::pair<std::string, std::string>> selected_units(
    const fsim::elaboration::ElaborationResult& result) {
  assert(result.ok());
  std::vector<std::pair<std::string, std::string>> selected;
  for (const auto& item : result.design->specializations()) {
    selected.emplace_back(item.instance, item.unit);
  }
  return selected;
}

const fsim::elaboration::SpecializationInfo& elaborated_specialization(
    const fsim::elaboration::ElaborationResult& result,
    const std::string_view path) {
  const auto found = std::ranges::find_if(
      result.design->specializations(),
      [&](const auto& candidate) {
        return candidate.instance == path;
      });
  if (found == result.design->specializations().end()) {
    std::cerr << "missing specialization '" << path << "'; available:";
    for (const auto& candidate : result.design->specializations()) {
      std::cerr << "\n  " << candidate.instance;
    }
    std::cerr << '\n';
  }
  assert(found != result.design->specializations().end());
  return *found;
}

bool has_diagnostic(
    const fsim::elaboration::ElaborationResult& result,
    const std::string_view code) {
  return std::ranges::any_of(
      result.diagnostics,
      [&](const auto& diagnostic) {
        return diagnostic.code == code;
      });
}

std::string identity_value(
    const fsim::elaboration::SpecializationInfo& specialization,
    const std::string_view name) {
  const auto found = std::ranges::find_if(
      specialization.parameter_identity_values,
      [&](const auto& value) {
        return value.first == name;
      });
  assert(found != specialization.parameter_identity_values.end());
  return found->second;
}

void verify_compiled_hir_handoff(
    const fsim::project::Config& config) {
  fsim::diagnostic::Engine diagnostics;
  const auto compiled = fsim::app::check_project(config, diagnostics);
  assert(compiled && !diagnostics.has_error());
  auto corrupt_ownership = *compiled;
  const auto corrupt_unit = std::ranges::find_if(
      corrupt_ownership.vhdl_hir.mutable_units(),
      [](const auto& unit) {
        return !unit.instances.empty();
      });
  assert(corrupt_unit
         != corrupt_ownership.vhdl_hir.mutable_units().end());
  corrupt_unit->instances.clear();
  corrupt_ownership.refresh_lookup_indexes();
  fsim::diagnostic::Engine corrupt_diagnostics;
  assert(!fsim::app::serialize_compiled_hir_bundle(
      corrupt_ownership, corrupt_diagnostics));
  assert(corrupt_diagnostics.has_error());
  auto corrupt_generate_ownership = *compiled;
  const auto generate_owner = std::ranges::find_if(
      corrupt_generate_ownership.vhdl_hir.mutable_units(),
      [](const auto& unit) {
        return std::ranges::any_of(
            unit.generates,
            [](const auto& generate) {
              return !generate.instances.empty();
            });
      });
  assert(generate_owner
         != corrupt_generate_ownership.vhdl_hir.mutable_units().end());
  const auto corrupt_generate = std::ranges::find_if(
      generate_owner->generates,
      [](const auto& generate) {
        return !generate.instances.empty();
      });
  assert(corrupt_generate != generate_owner->generates.end());
  corrupt_generate->instances.clear();
  corrupt_generate_ownership.refresh_lookup_indexes();
  fsim::diagnostic::Engine corrupt_generate_diagnostics;
  assert(!fsim::app::serialize_compiled_hir_bundle(
      corrupt_generate_ownership, corrupt_generate_diagnostics));
  assert(corrupt_generate_diagnostics.has_error());
  const auto direct = elaborate_compiled(*compiled);
  if (!direct.ok()) {
    for (const auto& diagnostic : direct.diagnostics) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(direct.ok());
  assert(
      elaborated_specialization(
          direct, "runtime_configuration.configured_child").unit
      == "vhdl:work.configuration_leaf(fast)");
  assert(
      elaborated_specialization(
          direct, "runtime_configuration.remaining_child").unit
      == "vhdl:work.configuration_leaf(rtl)");
  assert(
      elaborated_specialization(
          direct,
          "runtime_configuration.outer.block.extended.child")
          .unit
      == "vhdl:work.configuration_marker(fast)");
  assert(
      elaborated_specialization(
          direct, "runtime_configuration.loop_gen[1].generated_marker")
          .unit
      == "vhdl:work.configuration_marker(fast)");

  auto portable = *compiled;
  const auto source_mappings
      = fsim::app::application_detail::compiled_cache_source_mappings(
          *compiled, config.base_directory, diagnostics);
  assert(source_mappings);
  assert(fsim::app::application_detail::relocate_compiled_design_sources(
      portable, *source_mappings, diagnostics));
  const auto bytes = fsim::app::serialize_compiled_hir_bundle(
      portable, diagnostics);
  if (!bytes) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(bytes && !diagnostics.has_error());
  auto decoded = fsim::app::deserialize_compiled_hir_bundle(
      *bytes, "vhdl-configuration-handoff", diagnostics);
  assert(decoded && !diagnostics.has_error());
  std::vector<fsim::library::SourceNameMapping> consumer_mappings;
  consumer_mappings.reserve(source_mappings->size());
  for (const auto& mapping : *source_mappings) {
    consumer_mappings.push_back(
        { mapping.logical_name, mapping.producer_name });
  }
  assert(fsim::app::application_detail::relocate_compiled_design_sources(
      *decoded, consumer_mappings, diagnostics));
  const auto restored = elaborate_compiled(*decoded);
  assert(restored.ok());
  assert(selected_units(restored) == selected_units(direct));

  auto missing_rule_design = *compiled;
  const auto missing_configuration = std::ranges::find_if(
      missing_rule_design.vhdl_hir.mutable_units(),
      [](const auto& unit) {
        return unit.kind
                == fsim::semantic::vhdl::UnitKind::configuration
            && unit.name == "runtime_configuration";
      });
  assert(missing_configuration
         != missing_rule_design.vhdl_hir.mutable_units().end());
  assert(missing_configuration->configuration);
  auto& missing_rules = missing_configuration->configuration->components;
  assert(!missing_rules.empty());
  missing_rules.erase(missing_rules.begin());
  missing_rule_design.refresh_lookup_indexes();
  const auto missing = elaborate_compiled(missing_rule_design);
  assert(!missing.ok());
  assert(has_diagnostic(missing, "FSIM-ELAB-VHCONFIG-016"));

  auto distinct_identity = *compiled;
  const auto configuration_unit = std::ranges::find_if(
      distinct_identity.vhdl_hir.mutable_units(),
      [](const auto& unit) {
        return unit.kind == fsim::semantic::vhdl::UnitKind::configuration
            && unit.name == "runtime_configuration";
      });
  assert(configuration_unit
         != distinct_identity.vhdl_hir.mutable_units().end());
  assert(configuration_unit->configuration);
  auto& bindings = configuration_unit->configuration->components;
  const auto expression = std::ranges::find_if(
      bindings,
      [](const auto& rule) {
        return !rule.binding.generic_map.empty()
            && rule.binding.generic_map.front().expression.has_value();
      });
  assert(expression != bindings.end());
  const auto expression_id =
      *expression->binding.generic_map.front().expression;
  const auto expression_record = std::ranges::find(
      distinct_identity.vhdl_hir.mutable_expressions(),
      expression_id,
      &fsim::semantic::vhdl::Expression::id);
  assert(expression_record
         != distinct_identity.vhdl_hir.mutable_expressions().end());
  expression_record->argument_names.push_back("identity-discriminator");
  distinct_identity.refresh_lookup_indexes();
  const auto distinct = elaborate_compiled(distinct_identity);
  assert(distinct.ok());
  assert(
      identity_value(
          elaborated_specialization(
              direct, "runtime_configuration.configured_child"),
          "__component")
      != identity_value(
          elaborated_specialization(
              distinct, "runtime_configuration.configured_child"),
          "__component"));
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
  assert(project->design.specializations().size() == 15);
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
  assert(
      specialization(
          *project,
          "runtime_configuration.direct_configuration_child.nested")
          .unit
      == std::string{"vhdl:work.configuration_leaf("}
             + (fast_wrapper ? "fast)" : "rtl)"));
  assert(std::ranges::any_of(
      specialization(
          *project,
          "runtime_configuration.direct_configuration_child")
          .parameter_identity_values,
      [](const auto& item) {
        return item.first == "__configuration"
            && item.second.find("wrapper_configuration")
                != std::string::npos;
      }));
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
  assert(fsim::test::has_source_dependency(
      root.source_dependencies,
      config.source_sets.front().files[4]));
  for (const auto path : {
           "runtime_configuration.configured_child",
           "runtime_configuration.remaining_child"}) {
    assert(fsim::test::has_source_dependency(
        specialization(*project, path).source_dependencies,
        config.source_sets.front().files[4]));
  }
  assert(!fsim::test::has_source_dependency(
      specialization(
          *project,
          "runtime_configuration.direct_child")
          .source_dependencies,
      config.source_sets.front().files[4]));
  for (const auto path : {
           "runtime_configuration.wrapper_child",
           "runtime_configuration.wrapper_child.nested",
           "runtime_configuration.direct_configuration_child",
           "runtime_configuration.direct_configuration_child.nested"}) {
    assert(fsim::test::has_source_dependency(
        specialization(*project, path).source_dependencies,
        config.source_sets.front().files[2]));
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
  const auto resolved_source = simulation.find_signal(
      "runtime_configuration.resolved_source");
  const auto resolved_sub_elements = simulation.find_signal(
      "runtime_configuration.resolved_sub_elements");
  assert(resolved_source && resolved_sub_elements);
  simulation.deposit_signal(
      *resolved_source,
      fsim::runtime::PackedLogic4::from_msb_string("0"));

  capture.result = simulation.run();
  for (std::size_t index = 0;
       index < output_ids.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(output_ids[index])
            .to_msb_string();
  }
  capture.resolved_sub_elements =
      simulation.read_signal(*resolved_sub_elements).to_msb_string();
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
  assert(capture.resolved_sub_elements == "1X");
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
  const auto wrapper_hierarchy =
      directory.path / "configuration_wrapper.vhd";
  const auto wrapper_configuration =
      directory.path / "wrapper_configuration.vhd";
  const auto top_hierarchy =
      directory.path / "configuration_top.vhd";
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

library ieee;
use ieee.std_logic_1164.all;
entity configuration_resolved_leaf is
  port (
    input_value : in std_logic;
    output_value : out std_logic);
end entity;
architecture rtl of configuration_resolved_leaf is
begin
  output_value <= input_value;
end architecture;

entity configuration_marker is
end entity;
architecture rtl of configuration_marker is
begin
end architecture;
architecture fast of configuration_marker is
begin
end architecture;
)";
    assert(output.good());
  }

  {
    std::ofstream output(wrapper_hierarchy, std::ios::binary);
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
)";
    assert(output.good());
  }

  {
    std::ofstream output(top_hierarchy, std::ios::binary);
    output << R"(
library ieee;
use ieee.std_logic_1164.all;
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
  signal resolved_source : std_logic;
  signal resolved_sub_elements : std_logic_vector(1 downto 0);
  component configuration_leaf is
    generic (component_amount : integer := 1);
    port (
      component_input : in integer;
      component_output : out integer);
  end component;
  component configuration_wrapper is
  end component;
  component configuration_marker is
  end component;
  for wrapper_child : configuration_wrapper
    use configuration work.wrapper_configuration;
  for all : configuration_marker
    use entity work.configuration_marker(rtl);
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
  direct_configuration_child:
    configuration work.wrapper_configuration;
  resolved_first: entity work.configuration_resolved_leaf(rtl)
    port map (
      input_value => not resolved_source,
      output_value => resolved_sub_elements(0));
  resolved_second: entity work.configuration_resolved_leaf(rtl)
    port map (
      input_value => '0',
      output_value => resolved_sub_elements(0));
  resolved_upper: entity work.configuration_resolved_leaf(rtl)
    port map (
      input_value => '1',
      output_value => resolved_sub_elements(1));
  \outer.block\: block
  begin
    \extended.child\: configuration_marker
      port map ();
  end block;
  loop_gen: for index in 0 to 1 generate
    generated_marker: configuration_marker
      port map ();
  end generate;
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
    for CONFIGURED_CHILD : CONFIGURATION_LEAF
      use entity work.configuration_leaf()"
               << (fast_configured ? "fast" : "rtl")
               << R"()
        generic map (AMOUNT => COMPONENT_AMOUNT)
        port map (
          INPUT_VALUE => COMPONENT_INPUT,
          OUTPUT_VALUE => COMPONENT_OUTPUT);
    end for;
    for others : configuration_leaf
      use entity work.configuration_leaf()"
               << (fast_configured ? "rtl" : "fast")
               << R"();
    end for;
    for \outer.block\
      for all : configuration_marker
        use entity work.configuration_marker(fast);
      end for;
    end for;
    for loop_gen(16#1#)
      for all : configuration_marker
        use entity work.configuration_marker(fast);
      end for;
    end for;
  end for;
end configuration;
)";
        assert(output.good());
      };

  write_wrapper_configuration(true);
  write_configuration(true);
  verify_compiled_hir_handoff(make_config(
      directory.path,
      leaf,
      wrapper_hierarchy,
      wrapper_configuration,
      top_hierarchy,
      configuration,
      fsim::project::Optimization::o0));

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_wrapper_configuration(true);
    write_configuration(true);
    const auto config = make_config(
        directory.path,
        leaf,
        wrapper_hierarchy,
        wrapper_configuration,
        top_hierarchy,
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
    assert(
        reference.resolved_sub_elements
        == cold.resolved_sub_elements);
    assert(
        cold.resolved_sub_elements
        == warm.resolved_sub_elements);
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
             "runtime_configuration.wrapper_child.nested",
             "runtime_configuration.direct_configuration_child",
             "runtime_configuration.direct_configuration_child.nested",
             "runtime_configuration.resolved_first",
             "runtime_configuration.resolved_second",
             "runtime_configuration.resolved_upper"}) {
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
             "runtime_configuration.wrapper_child.nested",
             "runtime_configuration.direct_configuration_child",
             "runtime_configuration.direct_configuration_child.nested"}) {
      assert(
          key_for(changed, path)
          != key_for(nested_changed, path));
    }
    for (const auto path : {
             "runtime_configuration",
             "runtime_configuration.configured_child",
             "runtime_configuration.remaining_child",
             "runtime_configuration.direct_child",
             "runtime_configuration.stable_child",
             "runtime_configuration.resolved_first",
             "runtime_configuration.resolved_second",
             "runtime_configuration.resolved_upper"}) {
      assert(
          key_for(changed, path)
          == key_for(nested_changed, path));
    }
    assert(nested_changed.cache.hits > 0);
    assert(nested_changed.cache.misses > 0);
#endif
  }
  return 0;
}
