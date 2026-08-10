// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {
using namespace fsim::frontend;

namespace {
void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}
} // namespace

void test_vhdl_array_attributes() {
  const auto result = parse_text("array_attributes.vhd",
                                 R"(
entity array_attributes is
end entity;

architecture rtl of array_attributes is
  type User_Array_T is array (integer range <>) of bit;
  subtype Desc_Array_T is User_Array_T(3 downto -2);
  signal descending : std_logic_vector(7 downto 4);
  signal user_array : Desc_Array_T;
  signal result : signed(31 downto 0);
  signal direction : boolean;
begin
  attribute_sensitive: process(descending'transaction)
  begin
    null;
  end process;

  attribute_waiter: process
  begin
    wait on descending'delayed(1 ns);
    wait;
  end process;

  observe: process
  begin
    result <= descending'left;
    result <= descending'right(1);
    result <= descending'low;
    result <= descending'high;
    result <= descending'length;
    direction <= descending'ascending;
    direction <= descending'event;
    result <= descending'last_value;
    result <= descending'last_event;
    result <= descending'last_active;
    direction <= descending'stable;
    direction <= descending'active;
    direction <= descending'quiet(1 ns);
    direction <= descending'transaction;
    result <= descending'delayed(1 ns);
    direction <= descending'driving;
    result <= descending'driving_value;
    result <= Desc_Array_T'left;
    direction <= Desc_Array_T'ascending(1);
    for Index in user_array'range loop
      null;
    end loop;
    for Index in Desc_Array_T'reverse_range(1) loop
      null;
    end loop;
    wait;
  end process;
end architecture;
)",
                                 Language::Vhdl2008);
  require(result.ok(), "bounded VHDL array attributes must parse");
  const auto &statements =
      result.design.units.back().processes.back().statements;
  const std::array<std::string_view, 17> attributes{
      "'left",         "'right",       "'low",     "'high",
      "'length",       "'ascending",   "'event",   "'last_value",
      "'last_event",   "'last_active", "'stable",  "'active",
      "'quiet",        "'transaction", "'delayed", "'driving",
      "'driving_value"};
  require(statements.size() == 22, "VHDL attribute statement count");
  for (std::size_t index = 0; index < attributes.size(); ++index) {
    require(statements[index].value.kind == ExpressionKind::Call &&
                statements[index].value.text == attributes[index] &&
                statements[index].value.operands.front().text == "descending",
            "VHDL attribute call HIR");
  }
  require(statements[1].value.operands.size() == 2 &&
              statements[1].value.operands[1].text == "1",
          "VHDL attribute optional dimension");
  require(statements[17].value.kind == ExpressionKind::Call &&
              statements[17].value.text == "'left" &&
              statements[17].value.operands.front().text == "desc_array_t" &&
              statements[18].value.text == "'ascending" &&
              statements[18].value.operands.size() == 2,
          "VHDL user-array subtype-mark scalar attributes retain HIR");
  require(statements[19].kind == StatementKind::Loop &&
              statements[19].loop_initial.kind == ExpressionKind::Call &&
              statements[19].loop_initial.text == "'range" &&
              statements[19].loop_initial.operands.front().text ==
                  "user_array" &&
              statements[19].loop_limit.kind == ExpressionKind::Invalid &&
              statements[20].kind == StatementKind::Loop &&
              statements[20].loop_initial.text == "'reverse_range" &&
              statements[20].loop_initial.operands.size() == 2,
          "range attributes form complete sequential-loop discrete ranges");
  const auto &processes = result.design.units.back().processes;
  require(processes.size() == 3 && processes[0].sensitivities.size() == 1 &&
              processes[0].sensitivities.front().expression.text ==
                  "'transaction" &&
              processes[1]
                      .statements.front()
                      .sensitivities.front()
                      .expression.text == "'delayed",
          "implicit VHDL signals retain process and wait sensitivity HIR");

  const auto unsupported = parse_text("unsupported_attribute.vhd",
                                      R"(
architecture rtl of unsupported_attribute is
  signal value : bit_vector(3 downto 0);
  signal result : signed(31 downto 0);
begin
  result <= value'instance_name;
end architecture;
)",
                                      Language::Vhdl2008);
  require(!unsupported.ok() && std::ranges::any_of(unsupported.diagnostics,
                                                   [](const auto &diagnostic) {
                                                     return diagnostic.code ==
                                                            "FSIM-VHDL-SEM-030";
                                                   }),
          "unsupported VHDL attributes must be targeted");
}

void test_vhdl_user_attribute_and_group_declarations() {
  const auto parsed = parse_text("vhdl_attribute_groups.vhd",
                                 R"(
package Attribute_Groups is
  type State_T is (Idle, Active);
  attribute Trace_Name : string;
  attribute Trace_Name of State_T : type is "state";
  group Signal_Template is (signal <>);
  group State_Group : Signal_Template (State_T);
end package;
)",
                                 Language::Vhdl2008);
  require(parsed.ok(), "VHDL user attributes and groups must parse");
  const auto &package = parsed.design.units.front();
  require(package.vhdl_attributes.size() == 2 &&
              package.vhdl_attributes[0].name == "trace_name" &&
              !package.vhdl_attributes[0].specification &&
              package.vhdl_attributes[0].type.domain == ValueDomain::String &&
              package.vhdl_attributes[1].specification &&
              package.vhdl_attributes[1].entity_names ==
                  std::vector<std::string>{"state_t"} &&
              package.vhdl_attributes[1].entity_class == "type" &&
              package.vhdl_attributes[1].value.decoded_string == "state",
          "attribute declaration/specification ownership retains type, target, "
          "class, value, and source order");
  require(package.vhdl_groups.size() == 2 &&
              package.vhdl_groups[0].template_declaration &&
              package.vhdl_groups[0].entries ==
                  std::vector<std::string>{"signal<>"} &&
              !package.vhdl_groups[1].template_declaration &&
              package.vhdl_groups[1].template_name == "signal_template" &&
              package.vhdl_groups[1].entries ==
                  std::vector<std::string>{"state_t"},
          "group templates and instances retain canonical entries and linkage");

  const auto nested = parse_text("vhdl_nested_attribute_groups.vhd",
                                 R"(
entity nested_attribute_groups is end entity;
architecture rtl of nested_attribute_groups is
  function annotated return integer is
    constant local_value : integer := 1;
    attribute local_mark : integer;
    attribute local_mark of local_value : constant is 1;
    group local_template is (constant <>);
    group local_group : local_template (local_value);
  begin
    return local_value;
  end function;
  procedure observed is
    attribute procedure_mark : boolean;
  begin
    null;
  end procedure;
begin
  observer: process
    attribute process_mark : boolean;
  begin
    wait;
  end process;
  nested_block: block
    constant block_value : integer := 2;
    attribute block_mark : integer;
    attribute block_mark of block_value : constant is 2;
    group block_template is (constant <>);
    group block_group : block_template (block_value);
  begin
  end block nested_block;
end architecture;
)", Language::Vhdl2008);
  require(nested.ok(),
          "nested VHDL user attributes and groups must parse");
  const auto &architecture = nested.design.units.back();
  require(architecture.functions.size() == 1 &&
              architecture.functions[0].vhdl_attributes.size() == 2 &&
              architecture.functions[0].vhdl_groups.size() == 2 &&
              architecture.procedures.size() == 1 &&
              architecture.procedures[0].vhdl_attributes.size() == 1 &&
              architecture.processes.size() == 1 &&
              architecture.processes[0].vhdl_attributes.size() == 1 &&
              architecture.generate_regions.size() == 1 &&
              architecture.generate_regions[0]
                      .then_body.vhdl_attributes.size() == 2 &&
              architecture.generate_regions[0]
                      .then_body.vhdl_groups.size() == 2,
          "attributes and groups propagate through callable, process, and "
          "generated declarative regions");

  const auto malformed = parse_text("invalid_vhdl_attribute_groups.vhd",
                                    R"(
entity Invalid_Attribute_Groups is
  attribute Duplicate : boolean;
  attribute DUPLICATE : boolean;
  attribute Missing of all : entity is true;
  group Template is (signal <>);
  group TEMPLATE is (signal <>);
  group Instance : Unknown_Template (all);
end entity;
)",
                                    Language::Vhdl2008);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        malformed.diagnostics,
        [&](const Diagnostic &diagnostic) { return diagnostic.code == code; });
  };
  require(!malformed.ok() && has_code("FSIM-VHDL-SEM-098") &&
              has_code("FSIM-VHDL-SEM-099") && has_code("FSIM-VHDL-SEM-100") &&
              has_code("FSIM-VHDL-SEM-101"),
          "attribute/group duplicates and unresolved references need exact "
          "diagnostics");
}

} // namespace fsim::tests::frontend
