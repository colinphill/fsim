// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

namespace fsim::tests::elaboration {

namespace {

void append_design(
    fsim::frontend::ParsedDesign& destination,
    fsim::frontend::ParsedDesign source) {
    for (auto& unit : source.units) {
        destination.units.push_back(std::move(unit));
    }
}

const fsim::elaboration::SpecializationInfo&
specialization(
    const fsim::elaboration::ElaborationResult& result,
    const std::string_view path) {
    const auto found = std::ranges::find_if(
        result.design->specializations(),
        [&](const auto& candidate) {
          return candidate.instance == path;
        });
    assert(found != result.design->specializations().end());
    return *found;
}

fsim::elaboration::ElaborationResult elaborate_text(
    const std::string_view name,
    const std::string_view source,
    const std::string_view top) {
    const auto parsed = fsim::frontend::parse_text(
        name, source, fsim::frontend::Language::Vhdl2008);
    assert(parsed.ok());
    return fsim::elaboration::elaborate(parsed.design, top);
}

}  // namespace

void test_vhdl_components() {
    auto leaf = fsim::frontend::parse_text(
        "component_leaf.vhd",
        R"(
entity component_leaf is
  generic (constant entity_width : in positive := 9);
  port (
    entity_input : in std_logic_vector(entity_width - 1 downto 0);
    entity_output : out std_logic_vector(entity_width - 1 downto 0));
end entity;
architecture rtl of component_leaf is
begin
  entity_output <= entity_input;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    auto top = fsim::frontend::parse_text(
        "component_top.vhd",
        R"(
entity component_top is
end entity;
architecture rtl of component_top is
  signal first_input : std_logic_vector(3 downto 0);
  signal first_output : std_logic_vector(3 downto 0);
  signal second_input : std_logic_vector(3 downto 0);
  signal second_output : std_logic_vector(3 downto 0);
  signal direct_input : std_logic_vector(8 downto 0);
  signal direct_output : std_logic_vector(8 downto 0);
  component component_leaf is
    generic (constant component_width : in positive := 4);
    port (
      component_input :
        in std_logic_vector(component_width - 1 downto 0);
      component_output :
        out std_logic_vector(component_width - 1 downto 0));
  end component component_leaf;
begin
  positional_child: component_leaf
    generic map (4)
    port map (first_input, first_output);
  default_child: component_leaf
    generic map (component_width => open)
    port map (
      component_input => second_input,
      component_output => second_output);
  direct_open_child: entity work.component_leaf(rtl)
    generic map (entity_width => open)
    port map (direct_input, direct_output);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(leaf.ok() && top.ok());
    append_design(leaf.design, std::move(top.design));
    const auto positive = fsim::elaboration::elaborate(
        leaf.design, "vhdl:work.component_top(rtl)");
    if (!positive.ok()) {
        for (const auto& diagnostic : positive.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(positive.ok());
    for (const auto child : {
             "component_top.positional_child",
             "component_top.default_child"}) {
        const auto& selected = specialization(positive, child);
        assert(selected.unit == "vhdl:work.component_leaf(rtl)");
        assert(std::ranges::find(
                   selected.source_dependencies,
                   "component_top.vhd")
               != selected.source_dependencies.end());
        const auto identity = std::ranges::find_if(
            selected.parameter_identity_values,
            [](const auto& value) {
              return value.first == "__component";
            });
        assert(identity != selected.parameter_identity_values.end());
        assert(identity->second.starts_with(
            "vhdl-component-binding-v7;name=component_leaf"));
    }
    assert(std::ranges::any_of(
        specialization(
            positive,
            "component_top.positional_child")
            .parameter_values,
        [](const auto& value) {
          return value.first == "entity_width"
              && value.second == "4";
        }));
    assert(std::ranges::any_of(
        specialization(positive, "component_top.direct_open_child")
            .parameter_values,
        [](const auto& value) {
          return value.first == "entity_width"
              && value.second == "9";
        }));
    for (const auto& [parent, child] :
         std::array<std::pair<std::string_view, std::string_view>, 3>{
             {{"component_top.first_input",
               "component_top.positional_child.entity_input"},
              {"component_top.second_input",
               "component_top.default_child.entity_input"},
              {"component_top.direct_input",
               "component_top.direct_open_child.entity_input"}}}) {
        assert(positive.design->find_signal(parent));
        assert(
            positive.design->find_signal(parent)
            == positive.design->find_signal(child));
    }

    const auto required_open = elaborate_text(
        "required_open_generic.vhd",
        R"(
entity required_open_leaf is
  generic (required_value : integer);
end entity;
architecture rtl of required_open_leaf is
begin
end architecture;
entity required_open_top is
end entity;
architecture rtl of required_open_top is
begin
  child: entity work.required_open_leaf(rtl)
    generic map (required_value => open)
    port map ();
end architecture;
)",
        "vhdl:work.required_open_top(rtl)");
    assert(!required_open.ok());
    assert(has_diagnostic(
        required_open, "FSIM-ELAB-GENERIC-001"));

    const auto invalid_packed_generics = elaborate_text(
        "invalid_packed_generics.vhd",
        R"(
package packed_generic_types is
  subtype mask_t is bit_vector(3 downto 0);
end package;
use work.packed_generic_types.all;
entity packed_generic_leaf is
  generic (mask_value : mask_t);
end entity;
architecture rtl of packed_generic_leaf is
begin
end architecture;
entity invalid_packed_generic_top is
end entity;
architecture rtl of invalid_packed_generic_top is
begin
  unknown_child: entity work.packed_generic_leaf(rtl)
    generic map (mask_value => (others => 'X'))
    port map ();
  wide_child: entity work.packed_generic_leaf(rtl)
    generic map (mask_value => 16)
    port map ();
end architecture;
)",
        "vhdl:work.invalid_packed_generic_top(rtl)");
    assert(!invalid_packed_generics.ok());
    assert(has_diagnostic(
        invalid_packed_generics, "FSIM-ELAB-GENERIC-004"));
    assert(has_diagnostic(
        invalid_packed_generics, "FSIM-ELAB-GENERIC-008"));

    auto visible_profiles = fsim::frontend::parse_text(
        "visible_component_profiles.vhd",
        R"(
package visible_component_profiles is
  component package_leaf is
    port (value : in integer);
  end component;
  component shadow_leaf is
    port (value : in bit);
  end component;
  component overload_leaf is
    port (value : in integer);
  end component;
  component overload_leaf is
    port (value : in bit);
  end component;
  component mode_leaf is
    port (value : in integer);
  end component;
  component mode_leaf is
    port (value : out integer);
  end component;
  component vector_leaf is
    generic (width : positive := 4);
    port (
      value : in std_logic_vector(width - 1 downto 0));
  end component;
  component vector_leaf is
    generic (width : positive := 8);
    port (value : in std_logic_vector(7 downto 0));
  end component;
end package;
)",
        fsim::frontend::Language::Vhdl2008);
    auto visible_hierarchy = fsim::frontend::parse_text(
        "visible_component_hierarchy.vhd",
        R"(
entity package_leaf is
  port (value : in integer);
end entity;
architecture rtl of package_leaf is
begin
end architecture;
architecture fast of package_leaf is
begin
end architecture;
entity entity_leaf is
  port (value : in integer);
end entity;
architecture rtl of entity_leaf is
begin
end architecture;
entity shadow_leaf is
  port (value : in integer);
end entity;
architecture rtl of shadow_leaf is
begin
end architecture;
entity overload_leaf is
  port (value : in integer);
end entity;
architecture first of overload_leaf is
begin
end architecture;
architecture second of overload_leaf is
begin
end architecture;
entity mode_leaf is
  port (value : in integer);
end entity;
architecture rtl of mode_leaf is
begin
end architecture;
entity vector_leaf is
  generic (width : positive := 4);
  port (
    value : in std_logic_vector(width - 1 downto 0));
end entity;
architecture rtl of vector_leaf is
begin
end architecture;
entity lexical_leaf is
  port (value : in integer);
end entity;
architecture rtl of lexical_leaf is
begin
end architecture;
entity generated_leaf is
  port (value : in integer);
end entity;
architecture rtl of generated_leaf is
begin
end architecture;

use work.visible_component_profiles.all;
entity visible_component_top is
  component entity_leaf is
    port (value : in integer);
  end component;
end entity;
architecture rtl of visible_component_top is
  signal value : integer;
  signal vector_value : std_logic_vector(3 downto 0);
  component shadow_leaf is
    port (value : in integer);
  end component;
  for package_child : package_leaf
    use entity work.package_leaf(rtl);
  for overload_child : overload_leaf
    use entity work.overload_leaf(second);
begin
  package_child: package_leaf port map (value);
  entity_child: entity_leaf port map (value);
  shadow_child: shadow_leaf port map (value);
  overload_child: overload_leaf port map (value);
  mode_child: mode_leaf port map (value);
  vector_child: vector_leaf
    generic map (4)
    port map (vector_value);
  local_block: block
    component lexical_leaf is
      port (value : in integer);
    end component;
  begin
    lexical_child: lexical_leaf port map (value);
  end block;
  selected: if true generate
    component generated_leaf is
      port (value : in integer);
    end component;
  begin
    generated_child: generated_leaf port map (value);
  end generate;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(visible_profiles.ok() && visible_hierarchy.ok());
    append_design(
        visible_profiles.design,
        std::move(visible_hierarchy.design));
    const auto visible_result =
        fsim::elaboration::elaborate(
            visible_profiles.design,
            "vhdl:work.visible_component_top(rtl)");
    if (!visible_result.ok()) {
        for (const auto& diagnostic :
             visible_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(visible_result.ok());
    for (const auto path : {
             "visible_component_top.package_child",
             "visible_component_top.entity_child",
             "visible_component_top.shadow_child",
             "visible_component_top.mode_child",
             "visible_component_top.vector_child",
             "visible_component_top.local_block.lexical_child",
             "visible_component_top.selected.generated_child"}) {
        assert(
            specialization(visible_result, path).unit.ends_with(
                "(rtl)"));
    }
    assert(
        specialization(
            visible_result,
            "visible_component_top.overload_child").unit
        == "vhdl:work.overload_leaf(second)");
    assert(
        specialization(
            visible_result,
            "visible_component_top.package_child").unit
        == "vhdl:work.package_leaf(rtl)");
    const auto& package_child =
        specialization(
            visible_result,
            "visible_component_top.package_child");
    assert(std::ranges::find(
        package_child.source_dependencies,
        "visible_component_profiles.vhd")
        != package_child.source_dependencies.end());
    const auto package_identity = std::ranges::find_if(
        package_child.parameter_identity_values,
        [](const auto& value) {
          return value.first == "__component";
        });
    assert(
        package_identity
            != package_child.parameter_identity_values.end()
        && package_identity->second.find(
               "region=2;scope=;owner=work."
               "visible_component_profiles")
            != std::string::npos);

    auto composite_types = fsim::frontend::parse_text(
        "composite_component_types.vhd",
        R"(
package composite_component_types is
  type mode_t is (idle, active, done);
  subtype active_mode_t is mode_t range active to done;
  type packet_t is record
    payload : std_logic_vector(3 downto 0);
    valid : bit;
  end record;
  type lane_t is array (natural range <>) of std_logic;
  subtype nibble_t is lane_t(3 downto 0);
  subtype ascending_nibble_t is lane_t(0 to 3);
  subtype small_t is integer range 0 to 15;
end package;
)",
        fsim::frontend::Language::Vhdl2008);
    auto composite_profiles = fsim::frontend::parse_text(
        "composite_component_profiles.vhd",
        R"(
use work.composite_component_types.all;
package composite_component_profiles is
  component record_leaf is
    port (value : in packet_t);
  end component;
  component enum_leaf is
    port (value : in active_mode_t);
  end component;
  component array_leaf is
    port (value : in nibble_t);
  end component;
  component array_leaf is
    port (value : in ascending_nibble_t);
  end component;
  component scalar_leaf is
    port (value : in small_t);
  end component;
  component selected_leaf is
    port (
      value : in work.composite_component_types.packet_t);
  end component;
  component overload_leaf is
    port (value : in packet_t);
  end component;
  component overload_leaf is
    port (value : in nibble_t);
  end component;
end package;
)",
        fsim::frontend::Language::Vhdl2008);
    auto composite_hierarchy = fsim::frontend::parse_text(
        "composite_component_hierarchy.vhd",
        R"(
use work.composite_component_types.all;
entity record_leaf is
  port (target_value : in packet_t);
end entity;
architecture rtl of record_leaf is
begin
end architecture;
architecture configured of record_leaf is
begin
end architecture;

use work.composite_component_types.all;
entity enum_leaf is
  port (value : in active_mode_t);
end entity;
architecture rtl of enum_leaf is
begin
end architecture;

use work.composite_component_types.all;
entity array_leaf is
  port (value : in nibble_t);
end entity;
architecture rtl of array_leaf is
begin
end architecture;

use work.composite_component_types.all;
entity scalar_leaf is
  port (value : in small_t);
end entity;
architecture rtl of scalar_leaf is
begin
end architecture;

entity selected_leaf is
  port (
    value : in work.composite_component_types.packet_t);
end entity;
architecture rtl of selected_leaf is
begin
end architecture;

use work.composite_component_types.all;
entity overload_leaf is
  port (value : in packet_t);
end entity;
architecture rtl of overload_leaf is
begin
end architecture;

entity composite_component_top is
end entity;
use work.composite_component_types.all;
use work.composite_component_profiles.all;
architecture rtl of composite_component_top is
  signal packet_value : packet_t;
  signal mode_value : active_mode_t;
  signal lane_value : nibble_t;
  signal scalar_value : small_t;
  for configured_child : record_leaf
    use entity work.record_leaf(configured)
    port map (target_value => value);
begin
  configured_child: record_leaf port map (packet_value);
  enum_child: enum_leaf port map (mode_value);
  array_child: array_leaf port map (lane_value);
  scalar_child: scalar_leaf port map (scalar_value);
  selected_child: selected_leaf port map (packet_value);
  overload_child: overload_leaf port map (packet_value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(
        composite_types.ok()
        && composite_profiles.ok()
        && composite_hierarchy.ok());
    append_design(
        composite_types.design,
        std::move(composite_profiles.design));
    append_design(
        composite_types.design,
        std::move(composite_hierarchy.design));
    const auto composite_result =
        fsim::elaboration::elaborate(
            composite_types.design,
            "vhdl:work.composite_component_top(rtl)");
    if (!composite_result.ok()) {
        for (const auto& diagnostic :
             composite_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(composite_result.ok());
    assert(
        specialization(
            composite_result,
            "composite_component_top.configured_child").unit
        == "vhdl:work.record_leaf(configured)");
    for (const auto path : {
             "composite_component_top.enum_child",
             "composite_component_top.array_child",
             "composite_component_top.scalar_child",
             "composite_component_top.selected_child",
             "composite_component_top.overload_child"}) {
        assert(
            specialization(composite_result, path).unit.ends_with(
                "(rtl)"));
    }
    const auto& composite_child =
        specialization(
            composite_result,
            "composite_component_top.configured_child");
    for (const auto dependency : {
             "composite_component_types.vhd",
             "composite_component_profiles.vhd"}) {
        assert(std::ranges::find(
                   composite_child.source_dependencies,
                   dependency)
               != composite_child.source_dependencies.end());
    }
    const auto composite_identity = std::ranges::find_if(
        composite_child.parameter_identity_values,
        [](const auto& value) {
          return value.first == "__component";
        });
    assert(
        composite_identity
            != composite_child.parameter_identity_values.end()
        && composite_identity->second.starts_with(
            "vhdl-component-binding-v7")
        && composite_identity->second.find(
               ";nominal=composite_component_types.vhd:")
            != std::string::npos
        && composite_identity->second.find(
               ":type-source=composite_component_types.vhd:")
            != std::string::npos);

    auto nonvalue_template = fsim::frontend::parse_text(
        "nonvalue_component_template.vhd",
        R"(
package nonvalue_component_template is
  generic (seed : integer := 1);
  constant selected_seed : integer := seed;
end package;
)",
        fsim::frontend::Language::Vhdl2008);
    auto nonvalue_leaf = fsim::frontend::parse_text(
        "nonvalue_component_leaf.vhd",
        R"(
entity nonvalue_component_leaf is
  generic (
    type entity_t;
    function entity_function(value : entity_t) return entity_t is <>;
    procedure entity_procedure(variable value : inout entity_t) is <>;
    package entity_helpers is new work.nonvalue_component_template
      generic map (<>));
  port (
    entity_input : in entity_t;
    entity_output : out entity_t);
end entity;
architecture rtl of nonvalue_component_leaf is
begin
  process(entity_input)
    variable temporary : entity_t;
  begin
    temporary := entity_function(entity_input);
    entity_procedure(temporary);
    entity_output <= temporary;
  end process;
end architecture;
architecture configured of nonvalue_component_leaf is
begin
  process(entity_input)
    variable temporary : entity_t;
  begin
    temporary := entity_function(entity_input);
    entity_procedure(temporary);
    entity_output <= temporary;
  end process;
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    auto nonvalue_top = fsim::frontend::parse_text(
        "nonvalue_component_top.vhd",
        R"(
entity nonvalue_component_top is
end entity;
architecture rtl of nonvalue_component_top is
  function increment(value : integer) return integer is
  begin
    return value + 1;
  end function;
  procedure double_value(variable value : inout integer) is
  begin
    value := value * 2;
  end procedure;
  package helper_instance is new work.nonvalue_component_template
    generic map (seed => 3);
  signal input_value : integer;
  signal output_value : integer;
  signal omitted_output : integer;
  signal explicit_output : integer;
  component nonvalue_component_leaf is
    generic (
      type component_t;
      function component_function(value : component_t)
        return component_t is <>;
      procedure component_procedure(
        variable value : inout component_t) is <>;
      package component_helpers is new work.nonvalue_component_template
        generic map (<>));
    port (
      component_input : in component_t;
      component_output : out component_t);
  end component;
  component nonvalue_component_leaf is
    generic (
      type component_t;
      function component_function(value : component_t)
        return component_t is <>;
      procedure component_procedure(
        variable value : inout component_t) is <>;
      package component_helpers is new work.nonvalue_component_template
        generic map (<>));
    port (
      component_input : in bit;
      component_output : out bit);
  end component;
  for configured_child : nonvalue_component_leaf
    use entity work.nonvalue_component_leaf(configured)
    generic map (
      entity_t => component_t,
      entity_function => component_function,
      entity_procedure => component_procedure,
      entity_helpers => component_helpers)
    port map (
      entity_input => component_input,
      entity_output => component_output);
  for omitted_child : nonvalue_component_leaf
    use entity work.nonvalue_component_leaf(configured);
  for explicit_child : nonvalue_component_leaf
    use entity work.nonvalue_component_leaf(configured);
begin
  configured_child: nonvalue_component_leaf
    generic map (
      component_t => integer,
      component_function => <>,
      component_procedure => <>,
      component_helpers => helper_instance)
    port map (
      component_input => input_value,
      component_output => output_value);
  omitted_child: nonvalue_component_leaf
    generic map (
      component_t => integer,
      component_helpers => helper_instance)
    port map (
      component_input => input_value,
      component_output => omitted_output);
  explicit_child: nonvalue_component_leaf
    generic map (
      integer,
      increment,
      double_value,
      helper_instance)
    port map (input_value, explicit_output);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(
        nonvalue_template.ok()
        && nonvalue_leaf.ok()
        && nonvalue_top.ok());
    append_design(
        nonvalue_template.design,
        std::move(nonvalue_leaf.design));
    append_design(
        nonvalue_template.design,
        std::move(nonvalue_top.design));
    const auto nonvalue_result =
        fsim::elaboration::elaborate(
            nonvalue_template.design,
            "vhdl:work.nonvalue_component_top(rtl)");
    if (!nonvalue_result.ok()) {
        for (const auto& diagnostic :
             nonvalue_result.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(nonvalue_result.ok());
    const auto& nonvalue_child =
        specialization(
            nonvalue_result,
            "nonvalue_component_top.configured_child");
    assert(
        nonvalue_child.unit
        == "vhdl:work.nonvalue_component_leaf(configured)");
    for (const auto path : {
             "nonvalue_component_top.omitted_child",
             "nonvalue_component_top.explicit_child"}) {
        assert(
            specialization(nonvalue_result, path).unit
                == "vhdl:work.nonvalue_component_leaf(configured)");
    }
    const auto nonvalue_identity = std::ranges::find_if(
        nonvalue_child.parameter_identity_values,
        [](const auto& value) {
          return value.first == "__component";
        });
    assert(
        nonvalue_identity
            != nonvalue_child.parameter_identity_values.end()
        && nonvalue_identity->second.starts_with(
            "vhdl-component-binding-v7")
        && nonvalue_identity->second.find(
               "actual=component_t:vhdl-type-v1")
            != std::string::npos
        && nonvalue_identity->second.find(
               "actual=component_function:vhdl-function-v1")
            != std::string::npos
        && nonvalue_identity->second.find(
               "actual=component_procedure:vhdl-procedure-v1")
            != std::string::npos
        && nonvalue_identity->second.find(
               "actual=component_helpers:vhdl-package-v1")
            != std::string::npos);
    for (const auto dependency : {
             "nonvalue_component_top.vhd",
             "nonvalue_component_template.vhd"}) {
        assert(std::ranges::find(
                   nonvalue_child.source_dependencies,
                   dependency)
               != nonvalue_child.source_dependencies.end());
    }

    const auto hidden_sibling = elaborate_text(
        "hidden_sibling_component.vhd",
        R"(
entity lexical_only_leaf is
  port (value : in integer);
end entity;
architecture rtl of lexical_only_leaf is
begin
end architecture;
entity hidden_sibling_top is
end entity;
architecture rtl of hidden_sibling_top is
  signal value : integer;
begin
  declaring_block: block
    component lexical_only_leaf is
      port (value : in integer);
    end component;
  begin
    visible_child: lexical_only_leaf port map (value);
  end block;
  sibling_block: block
  begin
    hidden_child: lexical_only_leaf port map (value);
  end block;
end architecture;
)",
        "vhdl:work.hidden_sibling_top(rtl)");
    assert(!hidden_sibling.ok());
    assert(has_diagnostic(
        hidden_sibling, "FSIM-ELAB-VHCOMP-001"));

    const auto unmatched_overload = elaborate_text(
        "unmatched_component_overload.vhd",
        R"(
entity overloaded_target is
  port (value : in integer);
end entity;
architecture rtl of overloaded_target is
begin
end architecture;
entity unmatched_overload_top is
end entity;
architecture rtl of unmatched_overload_top is
  signal value : std_logic;
  component overloaded_target is
    port (value : in integer);
  end component;
  component overloaded_target is
    port (value : in bit);
  end component;
begin
  child: overloaded_target port map (value);
end architecture;
)",
        "vhdl:work.unmatched_overload_top(rtl)");
    assert(!unmatched_overload.ok());
    assert(has_diagnostic(
        unmatched_overload, "FSIM-ELAB-VHCOMP-012"));

    const auto ambiguous_packages = elaborate_text(
        "ambiguous_package_components.vhd",
        R"(
package first_component_profiles is
  component shared_package_leaf is
    port (value : in integer);
  end component;
end package;
package second_component_profiles is
  component shared_package_leaf is
    port (value : in integer);
  end component;
end package;
entity shared_package_leaf is
  port (value : in integer);
end entity;
architecture rtl of shared_package_leaf is
begin
end architecture;
use work.first_component_profiles.all;
use work.second_component_profiles.all;
entity ambiguous_package_top is
end entity;
architecture rtl of ambiguous_package_top is
  signal value : integer;
begin
  child: shared_package_leaf port map (value);
end architecture;
)",
        "vhdl:work.ambiguous_package_top(rtl)");
    assert(!ambiguous_packages.ok());
    assert(has_diagnostic(
        ambiguous_packages, "FSIM-ELAB-VHCOMP-002"));

    const auto nominal_profile_mismatch = elaborate_text(
        "composite_component_nominal_mismatch.vhd",
        R"(
package first_record_types is
  type packet_t is record
    value : std_logic_vector(3 downto 0);
  end record;
end package;
package second_record_types is
  type packet_t is record
    value : std_logic_vector(3 downto 0);
  end record;
end package;
use work.second_record_types.all;
entity nominal_profile_leaf is
  port (target : in packet_t);
end entity;
architecture rtl of nominal_profile_leaf is
begin
end architecture;
entity nominal_profile_top is
end entity;
use work.first_record_types.all;
architecture rtl of nominal_profile_top is
  signal value : packet_t;
  component nominal_profile_leaf is
    port (source : in packet_t);
  end component;
begin
  child: nominal_profile_leaf port map (value);
end architecture;
)",
        "vhdl:work.nominal_profile_top(rtl)");
    assert(!nominal_profile_mismatch.ok());
    assert(has_diagnostic(
        nominal_profile_mismatch, "FSIM-ELAB-VHCOMP-007"));

    const auto nominal_actual_mismatch = elaborate_text(
        "composite_component_actual_mismatch.vhd",
        R"(
package component_record_types is
  type first_packet_t is record
    value : std_logic_vector(3 downto 0);
  end record;
  type second_packet_t is record
    value : std_logic_vector(3 downto 0);
  end record;
end package;
use work.component_record_types.all;
entity nominal_actual_leaf is
  port (target : in first_packet_t);
end entity;
architecture rtl of nominal_actual_leaf is
begin
end architecture;
entity nominal_actual_top is
end entity;
use work.component_record_types.all;
architecture rtl of nominal_actual_top is
  signal value : second_packet_t;
  component nominal_actual_leaf is
    port (source : in first_packet_t);
  end component;
begin
  child: nominal_actual_leaf port map (value);
end architecture;
)",
        "vhdl:work.nominal_actual_top(rtl)");
    assert(!nominal_actual_mismatch.ok());
    assert(has_diagnostic(
        nominal_actual_mismatch, "FSIM-ELAB-BIND-057"));

    const auto composite_default = elaborate_text(
        "composite_component_default.vhd",
        R"(
package component_default_types is
  type packet_t is record
    value : std_logic_vector(3 downto 0);
  end record;
end package;
use work.component_default_types.all;
entity composite_default_leaf is
  port (target : in packet_t);
end entity;
architecture rtl of composite_default_leaf is
begin
end architecture;
entity composite_default_top is
end entity;
use work.component_default_types.all;
architecture rtl of composite_default_top is
  signal value : packet_t;
  component composite_default_leaf is
    port (source : in packet_t := default_packet);
  end component;
begin
  child: composite_default_leaf port map (value);
end architecture;
)",
        "vhdl:work.composite_default_top(rtl)");
    assert(composite_default.ok());

    const auto missing_component_type = elaborate_text(
        "missing_component_type.vhd",
        R"(
entity missing_component_type_leaf is
  port (value : in bit);
end entity;
architecture rtl of missing_component_type_leaf is
begin
end architecture;
entity missing_component_type_top is
end entity;
architecture rtl of missing_component_type_top is
  signal value : bit;
  component missing_component_type_leaf is
    port (value : in missing_t);
  end component;
begin
  child: missing_component_type_leaf port map (value);
end architecture;
)",
        "vhdl:work.missing_component_type_top(rtl)");
    assert(!missing_component_type.ok());
    assert(has_diagnostic(
        missing_component_type, "FSIM-ELAB-VHTYPE-001"));

    const auto missing_nonvalue_default = elaborate_text(
        "component_missing_nonvalue_default.vhd",
        R"(
entity component_missing_nonvalue_leaf is
  generic (type entity_t);
  port (value : in entity_t);
end entity;
architecture rtl of component_missing_nonvalue_leaf is
begin
end architecture;
entity component_missing_nonvalue_top is
end entity;
architecture rtl of component_missing_nonvalue_top is
  signal value : integer;
  component component_missing_nonvalue_leaf is
    generic (type component_t);
    port (value : in component_t);
  end component;
begin
  child: component_missing_nonvalue_leaf
    generic map (<>)
    port map (value);
end architecture;
)",
        "vhdl:work.component_missing_nonvalue_top(rtl)");
    assert(!missing_nonvalue_default.ok());
    assert(has_diagnostic(
        missing_nonvalue_default,
        "FSIM-ELAB-VHCOMP-014"));

    const auto nonvalue_profile_mismatch = elaborate_text(
        "component_nonvalue_profile_mismatch.vhd",
        R"(
entity component_nonvalue_profile_leaf is
  generic (
    function entity_function(value : integer)
      return integer is <>);
end entity;
architecture rtl of component_nonvalue_profile_leaf is
begin
end architecture;
entity component_nonvalue_profile_top is
end entity;
architecture rtl of component_nonvalue_profile_top is
  function bit_identity(value : bit) return bit is
  begin
    return value;
  end function;
  component component_nonvalue_profile_leaf is
    generic (
      function component_function(value : bit)
        return bit is <>);
  end component;
begin
  child: component_nonvalue_profile_leaf
    generic map (bit_identity)
    port map ();
end architecture;
)",
        "vhdl:work.component_nonvalue_profile_top(rtl)");
    assert(!nonvalue_profile_mismatch.ok());
    assert(has_diagnostic(
        nonvalue_profile_mismatch,
        "FSIM-ELAB-VHCOMP-006"));

    const auto wrong_nonvalue_kind = elaborate_text(
        "component_wrong_nonvalue_kind.vhd",
        R"(
entity component_wrong_nonvalue_leaf is
  generic (type entity_t);
end entity;
architecture rtl of component_wrong_nonvalue_leaf is
begin
end architecture;
entity component_wrong_nonvalue_top is
end entity;
architecture rtl of component_wrong_nonvalue_top is
  function value_function(value : integer) return integer is
  begin
    return value;
  end function;
  component component_wrong_nonvalue_leaf is
    generic (type component_t);
  end component;
begin
  child: component_wrong_nonvalue_leaf
    generic map (value_function)
    port map ();
end architecture;
)",
        "vhdl:work.component_wrong_nonvalue_top(rtl)");
    assert(!wrong_nonvalue_kind.ok());
    assert(has_diagnostic(
        wrong_nonvalue_kind,
        "FSIM-ELAB-GENTYPE-003"));

    const auto missing_package_actual = elaborate_text(
        "component_missing_package_actual.vhd",
        R"(
package missing_package_template is
  generic (value : integer := 1);
end package;
entity component_missing_package_leaf is
  generic (
    package entity_helpers is new work.missing_package_template
      generic map (<>));
end entity;
architecture rtl of component_missing_package_leaf is
begin
end architecture;
entity component_missing_package_top is
end entity;
architecture rtl of component_missing_package_top is
  component component_missing_package_leaf is
    generic (
      package component_helpers is new work.missing_package_template
        generic map (<>));
  end component;
begin
  child: component_missing_package_leaf port map ();
end architecture;
)",
        "vhdl:work.component_missing_package_top(rtl)");
    assert(!missing_package_actual.ok());
    assert(has_diagnostic(
        missing_package_actual,
        "FSIM-ELAB-VHCOMP-008"));

    const auto package_profile_mismatch = elaborate_text(
        "component_package_profile_mismatch.vhd",
        R"(
package first_component_template is
  generic (value : integer := 1);
end package;
package second_component_template is
  generic (value : integer := 1);
end package;
entity component_package_profile_leaf is
  generic (
    package entity_helpers is new work.first_component_template
      generic map (<>));
end entity;
architecture rtl of component_package_profile_leaf is
begin
end architecture;
entity component_package_profile_top is
end entity;
architecture rtl of component_package_profile_top is
  package second_instance is new work.second_component_template
    generic map (1);
  component component_package_profile_leaf is
    generic (
      package component_helpers is new work.second_component_template
        generic map (<>));
  end component;
begin
  child: component_package_profile_leaf
    generic map (second_instance)
    port map ();
end architecture;
)",
        "vhdl:work.component_package_profile_top(rtl)");
    assert(!package_profile_mismatch.ok());
    assert(has_diagnostic(
        package_profile_mismatch,
        "FSIM-ELAB-VHCOMP-006"));

    const auto missing_declaration = elaborate_text(
        "missing_component_declaration.vhd",
        R"(
entity undeclared_leaf is
end entity;
architecture rtl of undeclared_leaf is
begin
end architecture;
entity missing_component_declaration is
end entity;
architecture rtl of missing_component_declaration is
begin
  child: undeclared_leaf port map ();
end architecture;
)",
        "vhdl:work.missing_component_declaration(rtl)");
    assert(!missing_declaration.ok());
    assert(has_diagnostic(
        missing_declaration, "FSIM-ELAB-VHCOMP-001"));

    auto duplicate_hir = fsim::frontend::parse_text(
        "duplicate_component_hir.vhd",
        R"(
entity duplicate_component_leaf is
end entity;
architecture rtl of duplicate_component_leaf is
begin
end architecture;
entity duplicate_component_top is
end entity;
architecture rtl of duplicate_component_top is
  component duplicate_component_leaf is
  end component;
begin
  child: duplicate_component_leaf port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(duplicate_hir.ok());
    auto& duplicate_architecture = duplicate_hir.design.units.back();
    duplicate_architecture.vhdl_component_declarations.push_back(
        duplicate_architecture.vhdl_component_declarations.front());
    const auto duplicate_result = fsim::elaboration::elaborate(
        duplicate_hir.design,
        "vhdl:work.duplicate_component_top(rtl)");
    assert(!duplicate_result.ok());
    assert(has_diagnostic(
        duplicate_result, "FSIM-ELAB-VHCOMP-002"));

    const auto missing_target = elaborate_text(
        "missing_component_target.vhd",
        R"(
entity missing_component_target is
end entity;
architecture rtl of missing_component_target is
  component absent_leaf is
  end component;
begin
  child: absent_leaf port map ();
end architecture;
)",
        "vhdl:work.missing_component_target(rtl)");
    assert(!missing_target.ok());
    assert(has_diagnostic(
        missing_target, "FSIM-ELAB-VHCOMP-003"));

    const auto ambiguous_architecture = elaborate_text(
        "ambiguous_component_architecture.vhd",
        R"(
entity ambiguous_leaf is
end entity;
architecture first of ambiguous_leaf is
begin
end architecture;
architecture second of ambiguous_leaf is
begin
end architecture;
entity ambiguous_component_top is
end entity;
architecture rtl of ambiguous_component_top is
  component ambiguous_leaf is
  end component;
begin
  child: ambiguous_leaf port map ();
end architecture;
)",
        "vhdl:work.ambiguous_component_top(rtl)");
    assert(!ambiguous_architecture.ok());
    assert(has_diagnostic(
        ambiguous_architecture, "FSIM-ELAB-VHCOMP-005"));

    const auto ambiguous_entity = elaborate_text(
        "ambiguous_component_entity.vhd",
        R"(
entity duplicate_leaf is
end entity;
entity duplicate_leaf is
end entity;
architecture rtl of duplicate_leaf is
begin
end architecture;
entity duplicate_component_top is
end entity;
architecture rtl of duplicate_component_top is
  component duplicate_leaf is
  end component;
begin
  child: duplicate_leaf port map ();
end architecture;
)",
        "vhdl:work.duplicate_component_top(rtl)");
    assert(!ambiguous_entity.ok());
    assert(has_diagnostic(
        ambiguous_entity, "FSIM-ELAB-VHCOMP-005"));

    const auto generic_profile = elaborate_text(
        "incompatible_component_generic.vhd",
        R"(
entity incompatible_generic_leaf is
  generic (amount : integer := 2);
end entity;
architecture rtl of incompatible_generic_leaf is
begin
end architecture;
entity incompatible_generic_top is
end entity;
architecture rtl of incompatible_generic_top is
  component incompatible_generic_leaf is
    generic (amount : boolean := true);
  end component;
begin
  child: incompatible_generic_leaf port map ();
end architecture;
)",
        "vhdl:work.incompatible_generic_top(rtl)");
    assert(!generic_profile.ok());
    assert(has_diagnostic(
        generic_profile, "FSIM-ELAB-VHCOMP-006"));

    const auto port_profile = elaborate_text(
        "incompatible_component_port.vhd",
        R"(
entity incompatible_port_leaf is
  port (value : in integer);
end entity;
architecture rtl of incompatible_port_leaf is
begin
end architecture;
entity incompatible_port_top is
end entity;
architecture rtl of incompatible_port_top is
  signal value : integer;
  component incompatible_port_leaf is
    port (value : out integer);
  end component;
begin
  child: incompatible_port_leaf port map (value);
end architecture;
)",
        "vhdl:work.incompatible_port_top(rtl)");
    assert(!port_profile.ok());
    assert(has_diagnostic(
        port_profile, "FSIM-ELAB-VHCOMP-007"));

    const auto invalid_maps = elaborate_text(
        "invalid_component_maps.vhd",
        R"(
entity mapped_leaf is
  generic (amount : integer);
  port (value : in integer);
end entity;
architecture rtl of mapped_leaf is
begin
end architecture;
entity invalid_component_maps is
end entity;
architecture rtl of invalid_component_maps is
  signal value : integer;
  component mapped_leaf is
    generic (component_amount : integer);
    port (component_value : in integer);
  end component;
begin
  child: mapped_leaf
    generic map (unknown_amount => 1)
    port map (unknown_value => value);
end architecture;
)",
        "vhdl:work.invalid_component_maps(rtl)");
    assert(!invalid_maps.ok());
    assert(has_diagnostic(
        invalid_maps, "FSIM-ELAB-VHCOMP-008"));
    assert(has_diagnostic(
        invalid_maps, "FSIM-ELAB-VHCOMP-009"));

    const auto invalid_configuration_map = elaborate_text(
        "invalid_component_configuration_map.vhd",
        R"(
entity configured_profile_leaf is
  generic (amount : integer := 1);
  port (value : in integer);
end entity;
architecture rtl of configured_profile_leaf is
begin
end architecture;
entity configured_profile_top is
end entity;
architecture rtl of configured_profile_top is
  signal value : integer;
  component configured_profile_leaf is
    generic (component_amount : integer := 1);
    port (component_value : in integer);
  end component;
  for child : configured_profile_leaf
    use entity work.configured_profile_leaf(rtl)
      generic map (missing_amount => component_amount);
begin
  child: configured_profile_leaf
    generic map (component_amount => 2)
    port map (component_value => value);
end architecture;
)",
        "vhdl:work.configured_profile_top(rtl)");
    assert(!invalid_configuration_map.ok());
    assert(has_diagnostic(
        invalid_configuration_map,
        "FSIM-ELAB-VHCOMP-010"));

    const auto defaulted_ports = elaborate_text(
        "component_port_defaults.vhd",
        R"(
entity defaulted_port_leaf is
  port (
    entity_input : in integer := 13;
    entity_output : out integer);
end entity;
architecture rtl of defaulted_port_leaf is
begin
  entity_output <= entity_input;
end architecture;
entity defaulted_port_top is
end entity;
architecture rtl of defaulted_port_top is
  component defaulted_port_leaf is
    port (
      component_input : in integer := 7;
      component_output : out integer);
  end component;
begin
  omitted_child: defaulted_port_leaf port map ();
  open_child: defaulted_port_leaf
    port map (
      component_input => open,
      component_output => open);
  direct_omitted_child: entity work.defaulted_port_leaf(rtl)
    port map (entity_output => open);
  direct_open_child: entity work.defaulted_port_leaf(rtl)
    port map (
      entity_input => open,
      entity_output => open);
end architecture;
)",
        "vhdl:work.defaulted_port_top(rtl)");
    if (!defaulted_ports.ok()) {
        for (const auto& diagnostic :
             defaulted_ports.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(defaulted_ports.ok());
    for (const auto child : {
             "defaulted_port_top.omitted_child",
             "defaulted_port_top.open_child"}) {
        const auto& selected =
            specialization(defaulted_ports, child);
        const auto identity = std::ranges::find_if(
            selected.parameter_identity_values,
            [](const auto& value) {
              return value.first == "__component";
            });
        assert(
            identity
                != selected.parameter_identity_values.end()
            && identity->second.starts_with(
                "vhdl-component-binding-v7")
            && identity->second.find("state=1")
                != std::string::npos
            && identity->second.find("state=2")
                != std::string::npos);
    }
    auto defaulted_interpreter =
        defaulted_ports.design->create_interpreter();
    for (const auto input : {
             "defaulted_port_top.omitted_child.entity_input",
             "defaulted_port_top.open_child.entity_input"}) {
        const auto signal =
            defaulted_ports.design->find_signal(input);
        assert(signal);
        assert(
            defaulted_interpreter->signal_value(*signal)
                .to_msb_string()
            == "00000000000000000000000000000111");
    }
    for (const auto input : {
             "defaulted_port_top.direct_omitted_child.entity_input",
             "defaulted_port_top.direct_open_child.entity_input"}) {
        const auto signal =
            defaulted_ports.design->find_signal(input);
        assert(signal);
        assert(
            defaulted_interpreter->signal_value(*signal)
                .to_msb_string()
            == "00000000000000000000000000001101");
    }

    const auto expression_ports = elaborate_text(
        "component_expression_ports.vhd",
        R"(
entity expression_port_leaf is
  port (
    entity_input : in bit_vector(3 downto 0);
    entity_output : out bit_vector(3 downto 0));
end entity;
architecture rtl of expression_port_leaf is
begin
  entity_output <= entity_input;
end architecture;
entity expression_port_top is
end entity;
architecture rtl of expression_port_top is
  component expression_port_leaf is
    port (
      component_input : in bit_vector(3 downto 0);
      component_output : out bit_vector(3 downto 0));
  end component;
begin
  literal_child: expression_port_leaf
    port map (bit_vector'("1010"), open);
  aggregate_child: entity work.expression_port_leaf(rtl)
    port map (
      entity_input => bit_vector'(
        0 => '1', others => '0'),
      entity_output => open);
end architecture;
)",
        "vhdl:work.expression_port_top(rtl)");
    if (!expression_ports.ok()) {
        for (const auto& diagnostic :
             expression_ports.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(expression_ports.ok());
    auto expression_interpreter =
        expression_ports.design->create_interpreter();
    for (const auto& [name, expected] :
         std::array<std::pair<std::string_view, std::string_view>, 2>{
             {{"expression_port_top.literal_child.entity_input", "1010"},
              {"expression_port_top.aggregate_child.entity_input", "0001"}}}) {
        const auto signal =
            expression_ports.design->find_signal(name);
        assert(signal);
        assert(expression_interpreter->signal_value(*signal)
                   .to_msb_string()
               == expected);
    }

    const auto dynamic_expression_port = elaborate_text(
        "dynamic_expression_port.vhd",
        R"(
entity dynamic_expression_leaf is
  port (
    input_value : in bit;
    output_value : out bit);
end entity;
architecture rtl of dynamic_expression_leaf is
begin
end architecture;
entity dynamic_expression_top is
end entity;
architecture rtl of dynamic_expression_top is
  signal data : bit_vector(1 downto 0);
begin
  child: entity work.dynamic_expression_leaf(rtl)
    port map (
      input_value => data(0),
      output_value => open);
end architecture;
)",
        "vhdl:work.dynamic_expression_top(rtl)");
    assert(dynamic_expression_port.ok());

    const auto mismatched_qualification = elaborate_text(
        "mismatched_port_qualification.vhd",
        R"(
entity qualified_leaf is
  port (input_value : in bit_vector(3 downto 0));
end entity;
architecture rtl of qualified_leaf is
begin
end architecture;
entity qualified_top is
end entity;
architecture rtl of qualified_top is
begin
  child: entity work.qualified_leaf(rtl)
    port map (input_value => bit'("1010"));
end architecture;
)",
        "vhdl:work.qualified_top(rtl)");
    assert(!mismatched_qualification.ok());
    assert(has_diagnostic(
        mismatched_qualification, "FSIM-ELAB-VHPORT-001"));

    const auto invalid_output_expression = elaborate_text(
        "invalid_output_expression.vhd",
        R"(
entity invalid_output_leaf is
  port (output_value : out bit);
end entity;
architecture rtl of invalid_output_leaf is
begin
end architecture;
entity invalid_output_top is
end entity;
architecture rtl of invalid_output_top is
  signal data : bit;
begin
  child: entity work.invalid_output_leaf(rtl)
    port map (output_value => not data);
end architecture;
)",
        "vhdl:work.invalid_output_top(rtl)");
    assert(!invalid_output_expression.ok());
    assert(has_diagnostic(
        invalid_output_expression, "FSIM-ELAB-VHPORT-002"));

    const auto composite_defaults = elaborate_text(
        "component_composite_defaults.vhd",
        R"(
package component_default_types is
  type mode_t is (idle, active);
  type packet_t is record
    payload : std_logic_vector(3 downto 0);
    valid : bit;
  end record;
  type lane_t is array (natural range <>) of std_logic;
  subtype nibble_t is lane_t(3 downto 0);
end package;
use work.component_default_types.all;
entity composite_default_leaf is
  port (
    entity_mode : in mode_t;
    entity_packet : in packet_t;
    entity_lane : in nibble_t);
end entity;
architecture rtl of composite_default_leaf is
begin
end architecture;
entity composite_default_top is
end entity;
use work.component_default_types.all;
architecture rtl of composite_default_top is
  component composite_default_leaf is
    port (
      component_mode : in mode_t := active;
      component_packet : in packet_t :=
        (payload => "1010", valid => '1');
      component_lane : in nibble_t :=
        (3 => '1', others => '0'));
  end component;
begin
  child: composite_default_leaf port map ();
end architecture;
)",
        "vhdl:work.composite_default_top(rtl)");
    if (!composite_defaults.ok()) {
        for (const auto& diagnostic :
             composite_defaults.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(composite_defaults.ok());
    auto composite_interpreter =
        composite_defaults.design->create_interpreter();
    const auto require_initial =
        [&](const std::string_view name,
            const std::string_view expected) {
          const auto signal =
              composite_defaults.design->find_signal(name);
          assert(signal);
          assert(
              composite_interpreter->signal_value(*signal)
                  .to_msb_string()
              == expected);
        };
    require_initial(
        "composite_default_top.child.entity_mode", "1");
    require_initial(
        "composite_default_top.child.entity_packet", "10101");
    require_initial(
        "composite_default_top.child.entity_lane", "1000");

    const auto visible_default = elaborate_text(
        "component_visible_default.vhd",
        R"(
package visible_default_profiles is
  constant bias : integer := 1;
  component visible_default_leaf is
    generic (component_seed : integer := 2);
    port (
      component_input : in integer :=
        component_seed + bias);
  end component;
end package;
entity visible_default_leaf is
  generic (entity_seed : integer := 2);
  port (entity_input : in integer);
end entity;
architecture rtl of visible_default_leaf is
begin
end architecture;
use work.visible_default_profiles.all;
entity visible_default_top is
end entity;
architecture rtl of visible_default_top is
begin
  child: visible_default_leaf
    generic map (component_seed => 4)
    port map ();
end architecture;
)",
        "vhdl:work.visible_default_top(rtl)");
    if (!visible_default.ok()) {
        for (const auto& diagnostic :
             visible_default.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(visible_default.ok());
    auto visible_interpreter =
        visible_default.design->create_interpreter();
    const auto visible_input =
        visible_default.design->find_signal(
            "visible_default_top.child.entity_input");
    assert(visible_input);
    assert(
        visible_interpreter->signal_value(*visible_input)
            .to_msb_string()
        == "00000000000000000000000000000101");

    const auto dynamic_default = elaborate_text(
        "component_dynamic_default.vhd",
        R"(
entity dynamic_default_leaf is
  port (entity_input : in integer);
end entity;
architecture rtl of dynamic_default_leaf is
begin
end architecture;
entity dynamic_default_top is
end entity;
architecture rtl of dynamic_default_top is
  signal runtime_value : integer;
  component dynamic_default_leaf is
    port (component_input : in integer := runtime_value);
  end component;
begin
  child: dynamic_default_leaf port map ();
end architecture;
)",
        "vhdl:work.dynamic_default_top(rtl)");
    assert(!dynamic_default.ok());
    assert(has_diagnostic(
        dynamic_default, "FSIM-ELAB-VHCOMP-013"));

    const auto illegal_open = elaborate_text(
        "component_required_open.vhd",
        R"(
entity required_open_leaf is
  port (entity_input : in integer);
end entity;
architecture rtl of required_open_leaf is
begin
end architecture;
entity required_open_top is
end entity;
architecture rtl of required_open_top is
  component required_open_leaf is
    port (component_input : in integer);
  end component;
begin
  child: required_open_leaf
    port map (component_input => open);
end architecture;
)",
        "vhdl:work.required_open_top(rtl)");
    assert(!illegal_open.ok());
    assert(has_diagnostic(
        illegal_open, "FSIM-ELAB-VHCOMP-009"));

    const auto direct_missing_input = elaborate_text(
        "direct_required_input.vhd",
        R"(
entity direct_required_leaf is
  port (
    input_value : in integer;
    output_value : out integer);
end entity;
architecture rtl of direct_required_leaf is
begin
  output_value <= input_value;
end architecture;
entity direct_required_top is
end entity;
architecture rtl of direct_required_top is
begin
  child: entity work.direct_required_leaf(rtl)
    port map (output_value => open);
end architecture;
)",
        "vhdl:work.direct_required_top(rtl)");
    assert(!direct_missing_input.ok());
    assert(has_diagnostic(
        direct_missing_input, "FSIM-ELAB-BIND-027"));

    const auto open_output_modes = elaborate_text(
        "open_output_modes.vhd",
        R"(
entity open_mode_leaf is
  port (
    output_value : out integer;
    buffer_value : buffer integer);
end entity;
architecture rtl of open_mode_leaf is
begin
  output_value <= 5;
  buffer_value <= 7;
end architecture;
entity open_mode_top is
end entity;
architecture rtl of open_mode_top is
begin
  omitted_child: entity work.open_mode_leaf(rtl);
  explicit_child: entity work.open_mode_leaf(rtl)
    port map (
      output_value => open,
      buffer_value => open);
end architecture;
)",
        "vhdl:work.open_mode_top(rtl)");
    assert(open_output_modes.ok());
    auto open_output_interpreter =
        open_output_modes.design->create_interpreter();
    assert(
        open_output_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    for (const auto child :
         {"omitted_child", "explicit_child"}) {
        const auto output_signal =
            open_output_modes.design->find_signal(
                "open_mode_top." + std::string{child}
                + ".output_value");
        const auto buffer_signal =
            open_output_modes.design->find_signal(
                "open_mode_top." + std::string{child}
                + ".buffer_value");
        assert(output_signal && buffer_signal);
        assert(open_output_interpreter
                   ->signal_value(*output_signal).low_word().aval
               == 5);
        assert(open_output_interpreter
                   ->signal_value(*buffer_signal).low_word().aval
               == 7);
    }

    auto invalid_port_order = fsim::frontend::parse_text(
        "invalid_component_port_order.vhd",
        R"(
entity order_leaf is
  port (first_value, second_value : in integer);
end entity;
architecture rtl of order_leaf is
begin
end architecture;
entity order_top is
end entity;
architecture rtl of order_top is
  signal first_value, second_value : integer;
  component order_leaf is
    port (first_value, second_value : in integer);
  end component;
begin
  child: order_leaf
    port map (
      first_value => first_value,
      second_value => second_value);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(invalid_port_order.ok());
    invalid_port_order.design.units.back()
        .instances.front().connections.back().port.reset();
    const auto invalid_port_order_result =
        fsim::elaboration::elaborate(
            invalid_port_order.design,
            "vhdl:work.order_top(rtl)");
    assert(!invalid_port_order_result.ok());
    assert(has_diagnostic(
        invalid_port_order_result,
        "FSIM-ELAB-VHCOMP-009"));

    const auto configured_default = elaborate_text(
        "configured_component_default.vhd",
        R"(
entity configured_default_leaf is
  port (
    entity_input : in integer;
    entity_output : out integer);
end entity;
architecture rtl of configured_default_leaf is
begin
  entity_output <= entity_input;
end architecture;
entity configured_default_top is
end entity;
architecture rtl of configured_default_top is
  component configured_default_leaf is
    port (
      component_input : in integer := 12;
      component_output : out integer);
  end component;
  for child : configured_default_leaf
    use entity work.configured_default_leaf(rtl)
      port map (
        entity_input => component_input,
        entity_output => component_output);
begin
  child: configured_default_leaf
    port map (component_output => open);
end architecture;
)",
        "vhdl:work.configured_default_top(rtl)");
    if (!configured_default.ok()) {
        for (const auto& diagnostic :
             configured_default.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(configured_default.ok());
    auto configured_interpreter =
        configured_default.design->create_interpreter();
    const auto configured_input =
        configured_default.design->find_signal(
            "configured_default_top.child.entity_input");
    assert(configured_input);
    assert(
        configured_interpreter->signal_value(*configured_input)
            .to_msb_string()
        == "00000000000000000000000000001100");
    const auto& configured_specialization =
        specialization(
            configured_default,
            "configured_default_top.child");
    assert(std::ranges::any_of(
        configured_specialization.parameter_identity_values,
        [](const auto& value) {
          return value.first == "__component"
              && value.second.find("configuration=")
                  != std::string::npos
              && value.second.find(
                     "mapped-port=entity_input:state=1")
                  != std::string::npos
              && value.second.find(
                     "mapped-port=entity_output:state=2")
                  != std::string::npos;
        }));

    auto foreign = fsim::frontend::parse_text(
        "foreign_default.sv",
        "module foreign_default; endmodule",
        fsim::frontend::Language::SystemVerilog2017);
    auto foreign_parent = fsim::frontend::parse_text(
        "foreign_default_parent.vhd",
        R"(
entity foreign_default_parent is
end entity;
architecture rtl of foreign_default_parent is
  component foreign_default is
  end component;
begin
  child: foreign_default port map ();
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(foreign.ok() && foreign_parent.ok());
    append_design(
        foreign.design, std::move(foreign_parent.design));
    const auto cross_language = fsim::elaboration::elaborate(
        foreign.design,
        "vhdl:work.foreign_default_parent(rtl)");
    assert(cross_language.ok());
    assert(
        specialization(
            cross_language, "foreign_default_parent.child").unit
        == "sv:work.foreign_default");

    const auto mode_view_port = fsim::frontend::parse_text(
        "mode-view-port.vhd",
        R"(
package view_port_types is
  type request_bus is record
    request : bit;
    response : bit;
  end record;
  view initiator of request_bus is
    request : out;
    response : in;
  end view;
end package;

use work.view_port_types.all;
entity view_port_leaf is
  port (channel : view initiator);
end entity;
architecture rtl of view_port_leaf is
begin
end architecture;

entity view_port_top is
end entity;
use work.view_port_types.all;
architecture rtl of view_port_top is
  signal link : request_bus;
  component view_port_leaf is
    port (channel : view initiator of request_bus);
  end component;
begin
  child : view_port_leaf port map (channel => link);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    if (!mode_view_port.ok()) {
        for (const auto& diagnostic : mode_view_port.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(mode_view_port.ok());
    const auto mode_view_elaborated = fsim::elaboration::elaborate(
        mode_view_port.design, "vhdl:work.view_port_top(rtl)");
    if (!mode_view_elaborated.ok()) {
        for (const auto& diagnostic : mode_view_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(mode_view_elaborated.ok());
    const auto mode_view_link =
        mode_view_elaborated.design->find_signal("view_port_top.link");
    assert(mode_view_link);
    const auto& mode_view_bindings =
        mode_view_elaborated.design->signals()
            .at(*mode_view_link).vhdl_mode_view_bindings;
    assert(mode_view_bindings.size() == 1U);
    const auto& mode_view_binding = mode_view_bindings.front();
    assert(mode_view_binding.formal == "view_port_top.child.channel");
    assert(mode_view_binding.view == "initiator");
    assert(
        mode_view_binding.kind
        == fsim::frontend::VhdlModeViewIndicationKind::record);
    assert(mode_view_binding.elements.size() == 2U);
    assert(
        mode_view_binding.elements[0].formal_path
            == "view_port_top.child.channel.request"
        && mode_view_binding.elements[0].actual_path
            == "view_port_top.link.request"
        && mode_view_binding.elements[0].direction
            == fsim::frontend::PortDirection::Output);
    assert(
        mode_view_binding.elements[1].formal_path
            == "view_port_top.child.channel.response"
        && mode_view_binding.elements[1].actual_path
            == "view_port_top.link.response"
        && mode_view_binding.elements[1].direction
            == fsim::frontend::PortDirection::Input);

    const auto invalid_mode_view_port = fsim::frontend::parse_text(
        "invalid-mode-view-port.vhd",
        R"(
package invalid_view_port_types is
  type lane_t is record
    request : bit;
    response : bit;
  end record;
  type other_t is record
    other : bit;
  end record;
  view lane_view of lane_t is
    request : out;
    response : in;
  end view;
  view incomplete_view of lane_t is
    request : in;
  end view;
end package;

use work.invalid_view_port_types.all;
entity incomplete_view_leaf is
  port (channel : view incomplete_view);
end entity;
architecture rtl of incomplete_view_leaf is begin end architecture;

use work.invalid_view_port_types.all;
entity incompatible_view_leaf is
  port (channel : view lane_view of other_t);
end entity;
architecture rtl of incompatible_view_leaf is begin end architecture;

entity invalid_view_port_top is end entity;
use work.invalid_view_port_types.all;
architecture rtl of invalid_view_port_top is
  signal lane : lane_t;
  signal other : other_t;
begin
  incomplete_child : entity work.incomplete_view_leaf(rtl)
    port map (channel => lane);
  incompatible_child : entity work.incompatible_view_leaf(rtl)
    port map (channel => other);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    assert(invalid_mode_view_port.ok());
    const auto invalid_mode_view_elaborated = fsim::elaboration::elaborate(
        invalid_mode_view_port.design,
        "vhdl:work.invalid_view_port_top(rtl)");
    assert(!invalid_mode_view_elaborated.ok());
    assert(has_diagnostic(
        invalid_mode_view_elaborated, "FSIM-ELAB-VHVIEW-003"));
    assert(has_diagnostic(
        invalid_mode_view_elaborated, "FSIM-ELAB-VHVIEW-004"));

    const auto expression_port_source = R"(
entity revision_port_leaf is
  port (
    input_value : in bit_vector(136 downto 0);
    output_value : out bit_vector(136 downto 0));
end entity;
architecture rtl of revision_port_leaf is
begin
  output_value <= input_value;
end architecture;
entity revision_port_top is end entity;
architecture rtl of revision_port_top is
  signal left_value : bit_vector(136 downto 0);
  signal right_value : bit_vector(136 downto 0);
  signal observed : bit_vector(136 downto 0);
  component revision_port_leaf
    port (
      input_value : in bit_vector(136 downto 0);
      output_value : out bit_vector(136 downto 0));
  end component;
begin
  child : revision_port_leaf
    port map (
      input_value => left_value xor right_value,
      output_value => observed);
end architecture;)";
    const auto elaborate_expression_port = [&](
                                               const fsim::frontend::VhdlStandard standard) {
        const auto parsed = fsim::frontend::parse_text(
            "revision-expression-port.vhd",
            expression_port_source,
            fsim::frontend::Language::Vhdl2008,
            standard);
        assert(parsed.ok());
        return fsim::elaboration::elaborate(
            parsed.design, "vhdl:work.revision_port_top(rtl)");
    };
    const auto rejected_expression_port = elaborate_expression_port(
        fsim::frontend::VhdlStandard::Vhdl1993);
    assert(
        !rejected_expression_port.ok()
        && has_diagnostic(
            rejected_expression_port,
            "FSIM-ELAB-VHPORT-001"));
    const auto accepted_expression_port = elaborate_expression_port(
        fsim::frontend::VhdlStandard::Vhdl2008);
    assert(accepted_expression_port.ok());
    const auto wide_observed = accepted_expression_port.design->find_signal(
        "revision_port_top.observed");
    assert(
        wide_observed
        && accepted_expression_port.design->signals().at(*wide_observed).width
            == 137U);

    const auto exact_137 = std::string { "1" } + std::string(135U, '0') + "1";
    const auto vhdl93_source = std::string {
        R"(entity revision_statement_leaf is
  port (
    input_value : in bit_vector(136 downto 0);
    output_value : out bit_vector(136 downto 0));
end entity;
architecture rtl of revision_statement_leaf is
begin
  output_value <= input_value;
end architecture;
entity revision_statement_top is end entity;
architecture rtl of revision_statement_top is
  signal source : bit_vector(136 downto 0);
  signal observed : bit_vector(136 downto 0);
begin
  drive : process
  begin
    source <= B")"
    } + exact_137
        + R"(";
    wait;
  end process drive;
  child : entity work.revision_statement_leaf(rtl)
    port map (source(136 downto 0), observed);
end architecture;)";
    const auto vhdl93_parsed = fsim::frontend::parse_text(
        "revision-statement-execution.vhd",
        vhdl93_source,
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl1993);
    if (!vhdl93_parsed.ok()) {
        for (const auto& diagnostic : vhdl93_parsed.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(vhdl93_parsed.ok());
    const auto vhdl93_elaborated = fsim::elaboration::elaborate(
        vhdl93_parsed.design,
        "vhdl:work.revision_statement_top(rtl)");
    assert(vhdl93_elaborated.ok());
    const auto vhdl93_observed = vhdl93_elaborated.design->find_signal(
        "revision_statement_top.observed");
    assert(vhdl93_observed);
    auto vhdl93_interpreter = vhdl93_elaborated.design->create_interpreter();
    assert(
        vhdl93_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(
        vhdl93_interpreter->signal_value(*vhdl93_observed).to_msb_string()
        == exact_137);
}

}  // namespace fsim::tests::elaboration
