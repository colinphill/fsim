// SPDX-License-Identifier: Apache-2.0
#include "frontend_test_support.hpp"
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::frontend {
using namespace fsim::frontend;

namespace {

void require(
    const bool condition,
    const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

bool has_code(
    const ParseResult& parsed,
    const std::string_view code) {
  return std::ranges::any_of(
      parsed.diagnostics,
      [&](const Diagnostic& diagnostic) {
        return diagnostic.code == code;
      });
}

}  // namespace

void test_vhdl_configurations() {
  const auto parsed = parse_text(
      "configurations.vhd",
      R"(
entity configured_top is
end entity;
architecture rtl of configured_top is
  signal source_value : integer;
  signal result_value : integer;
  component leaf is
    generic (selected_amount : integer := 1);
    port (
      component_input : in integer;
      component_output : out integer);
  end component;
  for exact_child : leaf
    use entity work.leaf_impl(fast)
      generic map (amount => selected_amount)
      port map (
        input_value => component_input,
        output_value => component_output);
  for others : leaf
    use entity work.leaf_impl(rtl);
begin
  exact_child: leaf
    generic map (selected_amount => 3)
    port map (
      component_input => source_value,
      component_output => result_value);
  direct_child: entity work.leaf_impl(rtl)
    port map (
      input_value => source_value,
      output_value => result_value);
  configured_child: configuration work.leaf_configuration;
end architecture;

configuration configured_top_fast of configured_top is
  for rtl
    for all : leaf
      use entity work.leaf_impl(fast)
        generic map (amount => selected_amount)
        port map (
          input_value => component_input,
          output_value => component_output);
    end for;
  end for;
end configuration configured_top_fast;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "bounded configurations should parse");
  require(
      parsed.design.units.size() == 3,
      "entity, architecture, and configuration units retained");
  const auto& architecture = parsed.design.units[1];
  require(
      architecture.kind == UnitKind::VhdlArchitecture
          && architecture.vhdl_configuration_specifications.size() == 2,
      "architecture configuration specifications retained");
  const auto& exact =
      architecture.vhdl_configuration_specifications.front();
  require(
      exact.selection
              == VhdlInstantiationSelectionKind::Labels
          && exact.labels
              == std::vector<std::string>{"exact_child"}
          && exact.component_name == "leaf"
          && exact.binding.entity_name == "work.leaf_impl"
          && exact.binding.architecture_name == "fast"
          && exact.binding.generic_map.size() == 1
          && exact.binding.port_map.size() == 2,
      "explicit configuration binding profile retained");
  require(
      architecture.instances.size() == 3
          && architecture.instances[0].vhdl_component_instance
          && !architecture.instances[1].vhdl_component_instance
          && !architecture.instances[1].vhdl_configuration_instance
          && architecture.instances[2].vhdl_configuration_instance
          && architecture.instances[2].unit_name
              == "work.leaf_configuration",
      "component, direct entity, and direct configuration forms remain distinct");

  const auto& configuration = parsed.design.units[2];
  require(
      configuration.kind == UnitKind::VhdlConfiguration
          && configuration.name == "configured_top_fast"
          && configuration.primary_name == "configured_top"
          && configuration.vhdl_configuration.has_value(),
      "configuration declaration identity retained");
  const auto& block =
      configuration.vhdl_configuration->block;
  require(
      block.block_name == "rtl"
          && block.component_configurations.size() == 1
          && block.component_configurations.front().selection
              == VhdlInstantiationSelectionKind::All,
      "top block and all component configuration retained");

  const auto extended = parse_text(
      "nested_configurations.vhd",
      R"(
configuration nested_configuration of configured_top is
  for rtl
    for outer_block
      for loop_gen(1)
        for all : leaf
          use configuration work.leaf_configuration
            generic map (amount => selected_amount)
            port map (output_value => component_output);
        end for;
      end for;
      for selected_case
        for others : leaf
          use open;
        end for;
      end for;
    end for;
  end for;
end configuration;
)",
      Language::Vhdl2008);
  require(
      extended.ok()
          && extended.design.units.size() == 1,
      "nested and referenced configurations should parse");
  const auto& nested_root =
      extended.design.units.front()
          .vhdl_configuration->block;
  require(
      nested_root.block_configurations.size() == 1
          && nested_root.block_configurations.front()
                 .block_name == "outer_block"
          && nested_root.block_configurations.front()
                 .block_configurations.size() == 2,
      "recursive block configurations retained");
  const auto& indexed =
      nested_root.block_configurations.front()
          .block_configurations.front();
  require(
      indexed.generate_index.has_value()
          && indexed.component_configurations.size() == 1
          && indexed.component_configurations.front()
                 .binding.kind
              == VhdlBindingAspectKind::Configuration
          && indexed.component_configurations.front()
                 .binding.configuration_name
              == "work.leaf_configuration",
      "indexed generate and configuration reference retained");
  require(
      nested_root.block_configurations.front()
              .block_configurations.back()
              .component_configurations.front()
              .binding.kind
          == VhdlBindingAspectKind::Open,
      "open binding aspect retained");

  const auto invalid = parse_text(
      "invalid_configurations.vhd",
      R"(
entity invalid_configuration is
end entity;
architecture rtl of invalid_configuration is
  for duplicate, duplicate : leaf
    use entity work.leaf_impl(rtl);
  for all, stray : leaf
    use entity work.leaf_impl(rtl);
  for selected : leaf
    use configuration work.other;
begin
end architecture;
configuration bad_end of invalid_configuration is
  for rtl
    for selected : leaf
      use entity work.leaf_impl(rtl);
      for nested
      end for;
    end for;
  end for;
end configuration different;
)",
      Language::Vhdl2008);
  require(!invalid.ok(), "invalid configurations should fail");
  for (const auto code : {
           "FSIM-VHDL-SEM-065",
           "FSIM-VHDL-SEM-066",
           "FSIM-VHDL-SEM-067"}) {
    require(
        has_code(invalid, code),
        "expected bounded configuration diagnostic");
  }
}

}  // namespace fsim::tests::frontend
