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

void test_vhdl_component_declarations() {
  const auto parsed = parse_text(
      "components.vhd",
      R"(
entity component_top is
end entity;
architecture rtl of component_top is
  signal source_value : std_logic_vector(3 downto 0);
  signal result_value : std_logic_vector(3 downto 0);
  component vector_copy is
    generic (
      width : positive := 4;
      enabled : boolean := true);
    port (
      source_value : in std_logic_vector(width - 1 downto 0) := "0000";
      result_value : out std_logic_vector(width - 1 downto 0));
  end component vector_copy;
begin
  child: vector_copy
    generic map (4, enabled => true)
    port map (source_value, result_value => result_value);
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "bounded component declaration should parse");
  require(
      parsed.design.units.size() == 2,
      "entity and architecture retained");
  const auto& architecture = parsed.design.units.back();
  require(
      architecture.vhdl_component_declarations.size() == 1,
      "component declaration retained");
  const auto& component =
      architecture.vhdl_component_declarations.front();
  require(
      component.name == "vector_copy"
          && component.end_name
          == std::optional<std::string>{"vector_copy"}
          && component.declaration_order == 0
          && component.generics.size() == 2
          && component.generics[0].name == "width"
          && component.generics[0].default_value.valid()
          && component.ports.size() == 2
          && component.ports[0].direction
              == PortDirection::Input
          && component.ports[0].default_value.has_value()
          && component.ports[1].direction
              == PortDirection::Output,
      "component generic, port, mode, default, order, and end metadata");
  require(
      architecture.instances.size() == 1
          && architecture.instances.front().vhdl_component_instance
          && architecture.instances.front().parameter_overrides.size() == 2
          && !architecture.instances.front()
                  .parameter_overrides.front().name
          && architecture.instances.front().connections.size() == 2
          && !architecture.instances.front().connections.front().port,
      "positional and named component associations retained");

  const auto visibility = parse_text(
      "component_visibility.vhd",
      R"(
package component_profiles is
  type mode_t is (idle, active);
  type packet_t is record
    payload : std_logic_vector(3 downto 0);
    valid : bit;
  end record;
  type lane_t is array (natural range <>) of std_logic;
  subtype nibble_t is lane_t(3 downto 0);
  component shared_leaf is
    port (value : in integer);
  end component;
  component composite_leaf is
    port (
      mode : in mode_t;
      packet : in packet_t;
      lane : in nibble_t;
      selected_packet : in component_profiles.packet_t);
  end component;
end package;

entity visible_components is
  component entity_leaf is
    port (value : in integer);
  end component;
end entity;
architecture rtl of visible_components is
  component overloaded_leaf is
    port (value : in integer);
  end component;
  component overloaded_leaf is
    port (value : in bit);
  end component;
  for all : shared_leaf
    use entity work.shared_leaf(rtl);
begin
  local_block: block
    component scoped_leaf is
      port (value : in integer);
    end component;
  begin
    child: scoped_leaf port map ();
  end block;
  selected: if true generate
    component generated_leaf is
      port (value : in integer);
    end component;
  begin
    child: generated_leaf port map ();
  end generate;
end architecture;
)",
      Language::Vhdl2008);
  require(
      visibility.ok()
          && visibility.design.units.size() == 3,
      "package, entity, architecture, block, and generate components parse");
  require(
      visibility.design.units[0]
              .vhdl_component_declarations.size()
              == 2
          && visibility.design.units[0]
                 .vhdl_component_declarations.front()
                 .region
              == VhdlComponentDeclarationRegion::Package,
      "package component ownership retained");
  const auto& composite_component =
      visibility.design.units[0]
          .vhdl_component_declarations[1];
  require(
      composite_component.ports.size() == 4
          && composite_component.ports[0].type.named_type
              == "mode_t"
          && composite_component.ports[1].type.named_type
              == "packet_t"
          && composite_component.ports[2].type.named_type
              == "nibble_t"
          && composite_component.ports[3].type.named_type
              == "component_profiles.packet_t",
      "composite and selected component subtype indications retained");
  require(
      visibility.design.units[1]
              .vhdl_component_declarations.size()
              == 1
          && visibility.design.units[1]
                 .vhdl_component_declarations.front()
                 .region
              == VhdlComponentDeclarationRegion::Entity,
      "entity component ownership retained");
  const auto& visible_architecture =
      visibility.design.units[2];
  require(
      visible_architecture.vhdl_component_declarations.size()
              == 2
          && visible_architecture
                 .vhdl_component_declarations[0].name
              == "overloaded_leaf"
          && visible_architecture
                 .vhdl_component_declarations[1].name
              == "overloaded_leaf",
      "distinct architecture component overload profiles retained");
  require(
      visible_architecture
              .vhdl_configuration_specifications.size()
              == 1,
      "package-visible component configuration specification retained");
  require(
      visible_architecture.generate_regions.size() == 2
          && visible_architecture.generate_regions[0]
                 .then_body.vhdl_component_declarations.size()
              == 1
          && visible_architecture.generate_regions[0]
                 .then_body.vhdl_component_declarations.front()
                 .region
              == VhdlComponentDeclarationRegion::Block
          && visible_architecture.generate_regions[1]
                 .then_body.vhdl_component_declarations.size()
              == 1
          && visible_architecture.generate_regions[1]
                 .then_body.vhdl_component_declarations.front()
                 .region
              == VhdlComponentDeclarationRegion::Generate,
      "block and selected-generate component declarations retained");

  const auto invalid = parse_text(
      "invalid_components.vhd",
      R"(
entity invalid_components is
end entity;
architecture rtl of invalid_components is
  for all : late_component
    use entity work.target(rtl);
  component duplicate is
    generic (item : integer := 1);
    port (item : in integer);
  end component wrong_name;
  component duplicate is
  end component;
  component duplicate is
  end component;
  component typed is
    generic (type data_type);
    constant ignored : integer := 1;
  end component;
  component late_component is
  end component;
begin
end architecture;
)",
      Language::Vhdl2008);
  require(!invalid.ok(), "invalid component declarations should fail");
  for (const auto code : {
           "FSIM-VHDL-SEM-071",
           "FSIM-VHDL-SEM-069",
           "FSIM-VHDL-SEM-068",
           "FSIM-VHDL-UNSUPPORTED-051",
           "FSIM-VHDL-UNSUPPORTED-052"}) {
    require(
        has_code(invalid, code),
        "expected bounded component declaration diagnostic");
  }
}

}  // namespace fsim::tests::frontend
