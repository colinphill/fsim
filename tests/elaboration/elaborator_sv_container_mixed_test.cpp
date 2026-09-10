// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

void test_mixed_container_port_rejections(
    const fsim::frontend::ParsedDesign& port_design,
    const fsim::frontend::ParsedDesign& dynamic_port_design)
{
    const auto mixed_parent = fsim::frontend::parse_text(
        "mixed-container-port.vhd",
        R"(
entity mixed_port_top is
end entity;

architecture rtl of mixed_port_top is
  signal source : std_logic_vector(7 downto 0);
  signal result : std_logic_vector(7 downto 0);
  signal \shared\ : std_logic_vector(3 downto 0);
  component static_port_leaf is
    port (
      source : in std_logic_vector(7 downto 0);
      result : out std_logic_vector(7 downto 0);
      \shared\ : inout std_logic_vector(3 downto 0));
  end component;
begin
  child: static_port_leaf
    port map (
      source => source,
      result => result,
      \shared\ => \shared\);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(mixed_parent.ok());
    auto mixed_design = port_design;
    mixed_design.units.insert(
        mixed_design.units.end(),
        mixed_parent.design.units.begin(),
        mixed_parent.design.units.end());
    const std::vector<fsim::elaboration::Binding> bindings {
        { "mixed_port_top.child",
            "sv:work.static_port_leaf",
            std::nullopt }
    };
    const auto mixed_rejected = fsim::elaboration::elaborate(
        mixed_design,
        "vhdl:work.mixed_port_top(rtl)",
        bindings);
    assert(!mixed_rejected.ok());
    assert(has_diagnostic(
        mixed_rejected, "FSIM-ELAB-SVPORT-004"));

    const auto mixed_dynamic_parent = fsim::frontend::parse_text(
        "mixed-dynamic-container-port.vhd",
        R"(
entity mixed_dynamic_port_top is
end entity;

architecture rtl of mixed_dynamic_port_top is
  signal source : integer;
  signal result : std_logic_vector(7 downto 0);
  signal bounded : bit;
  signal scores : std_logic_vector(15 downto 0);
  signal work : integer;
  component dynamic_port_leaf is
    port (
      source : in integer;
      result : out std_logic_vector(7 downto 0);
      bounded : inout bit;
      scores : inout std_logic_vector(15 downto 0);
      work : inout integer);
  end component;
begin
  child: dynamic_port_leaf
    port map (
      source => source,
      result => result,
      bounded => bounded,
      scores => scores,
      work => work);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(mixed_dynamic_parent.ok());
    auto mixed_dynamic_design = dynamic_port_design;
    mixed_dynamic_design.units.insert(
        mixed_dynamic_design.units.end(),
        mixed_dynamic_parent.design.units.begin(),
        mixed_dynamic_parent.design.units.end());
    const std::vector<fsim::elaboration::Binding>
        dynamic_bindings {
            { "mixed_dynamic_port_top.child",
                "sv:work.dynamic_port_leaf",
                std::nullopt }
        };
    const auto mixed_dynamic_rejected = fsim::elaboration::elaborate(
        mixed_dynamic_design,
        "vhdl:work.mixed_dynamic_port_top(rtl)",
        dynamic_bindings);
    assert(!mixed_dynamic_rejected.ok());
    assert(has_diagnostic(
        mixed_dynamic_rejected, "FSIM-ELAB-SVPORT-004"));
}

} // namespace fsim::tests::elaboration
