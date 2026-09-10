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

void test_vhdl_component_defaults_and_revision()
{
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
  channel.request <= channel.response;
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
  link.response <= '1';
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
            == fsim::frontend::PortDirection::Output
        && mode_view_binding.elements[0].signal == *mode_view_link
        && mode_view_binding.elements[0].lsb_offset == 1U
        && mode_view_binding.elements[0].width == 1U);
    assert(
        mode_view_binding.elements[1].formal_path
            == "view_port_top.child.channel.response"
        && mode_view_binding.elements[1].actual_path
            == "view_port_top.link.response"
        && mode_view_binding.elements[1].direction
            == fsim::frontend::PortDirection::Input
        && mode_view_binding.elements[1].signal == *mode_view_link
        && mode_view_binding.elements[1].lsb_offset == 0U
        && mode_view_binding.elements[1].width == 1U);
    auto mode_view_interpreter =
        mode_view_elaborated.design->create_interpreter();
    assert(mode_view_interpreter->run().status
        == fsim::runtime::RunStatus::completed);
    assert(mode_view_interpreter->signal_value(*mode_view_link).to_msb_string()
        == "11");

    const auto illegal_mode_view_write = fsim::frontend::parse_text(
        "illegal-mode-view-write.vhd",
        R"(
package illegal_view_write_types is
  type request_bus is record
    request : bit;
    response : bit;
  end record;
  view initiator of request_bus is
    request : out;
    response : in;
  end view;
end package;
use work.illegal_view_write_types.all;
entity illegal_view_write_leaf is
  port (channel : view initiator);
end entity;
architecture rtl of illegal_view_write_leaf is
begin
  channel.response <= '0';
end architecture;
entity illegal_view_write_top is end entity;
use work.illegal_view_write_types.all;
architecture rtl of illegal_view_write_top is
  signal link : request_bus;
begin
  child : entity work.illegal_view_write_leaf(rtl)
    port map (channel => link);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    assert(illegal_mode_view_write.ok());
    const auto illegal_mode_view_write_elaborated =
        fsim::elaboration::elaborate(
            illegal_mode_view_write.design,
            "vhdl:work.illegal_view_write_top(rtl)");
    assert(!illegal_mode_view_write_elaborated.ok());
    assert(has_diagnostic(
        illegal_mode_view_write_elaborated,
        "FSIM-ELAB-VHVIEW-007"));

    const auto nested_mode_view_port = fsim::frontend::parse_text(
        "nested-mode-view-port.vhd",
        R"(
package nested_view_port_types is
  type lane_t is record
    request : bit;
    response : bit;
  end record;
  type pair_t is record
    left : lane_t;
    right : lane_t;
  end record;
  type lane_array_t is array (natural range <>) of lane_t;
  type bus_t is record
    pair : pair_t;
    lanes : lane_array_t(0 to 1);
  end record;
  view initiator of lane_t is
    request : out;
    response : in;
  end view;
  view pair_view of pair_t is
    left, right : view initiator;
  end view;
  view bus_view of bus_t is
    pair : view pair_view;
    lanes : view (initiator);
  end view;
end package;

use work.nested_view_port_types.all;
entity nested_view_leaf is
  port (channel : view bus_view);
end entity;
architecture rtl of nested_view_leaf is
begin
  channel.pair.left.request <= channel.pair.left.response;
  channel.lanes(0).request <= channel.lanes(0).response;
end architecture;

entity nested_view_top is end entity;
use work.nested_view_port_types.all;
architecture rtl of nested_view_top is
  signal link : bus_t;
begin
  child : entity work.nested_view_leaf(rtl)
    port map (channel => link);
end architecture;
)",
        fsim::frontend::Language::Vhdl2008,
        fsim::frontend::VhdlStandard::Vhdl2019);
    if (!nested_mode_view_port.ok()) {
        for (const auto& diagnostic : nested_mode_view_port.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(nested_mode_view_port.ok());
    const auto nested_mode_view_elaborated = fsim::elaboration::elaborate(
        nested_mode_view_port.design,
        "vhdl:work.nested_view_top(rtl)");
    if (!nested_mode_view_elaborated.ok()) {
        for (const auto& diagnostic : nested_mode_view_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(nested_mode_view_elaborated.ok());
    const auto nested_mode_view_link =
        nested_mode_view_elaborated.design->find_signal(
            "nested_view_top.link");
    assert(nested_mode_view_link);
    const auto& nested_bindings =
        nested_mode_view_elaborated.design->signals()
            .at(*nested_mode_view_link).vhdl_mode_view_bindings;
    assert(nested_bindings.size() == 1U);
    const auto& nested_endpoints = nested_bindings.front().elements;
    assert(nested_endpoints.size() == 8U);
    const std::array<std::string_view, 8> expected_suffixes {
        "pair.left.request", "pair.left.response",
        "pair.right.request", "pair.right.response",
        "lanes(0).request", "lanes(1).request",
        "lanes(0).response", "lanes(1).response" };
    const std::array<std::uint64_t, 8> expected_offsets {
        7U, 6U, 5U, 4U, 3U, 1U, 2U, 0U };
    const std::array<fsim::frontend::PortDirection, 8>
        expected_directions {
            fsim::frontend::PortDirection::Output,
            fsim::frontend::PortDirection::Input,
            fsim::frontend::PortDirection::Output,
            fsim::frontend::PortDirection::Input,
            fsim::frontend::PortDirection::Output,
            fsim::frontend::PortDirection::Output,
            fsim::frontend::PortDirection::Input,
            fsim::frontend::PortDirection::Input };
    for (std::size_t index = 0; index < nested_endpoints.size(); ++index) {
        const auto& endpoint = nested_endpoints[index];
        assert(endpoint.formal_path
            == "nested_view_top.child.channel."
                + std::string { expected_suffixes[index] });
        assert(endpoint.actual_path
            == "nested_view_top.link."
                + std::string { expected_suffixes[index] });
        assert(endpoint.signal == *nested_mode_view_link);
        assert(endpoint.lsb_offset == expected_offsets[index]);
        assert(endpoint.width == 1U);
        assert(endpoint.direction == expected_directions[index]);
    }

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

} // namespace fsim::tests::elaboration
