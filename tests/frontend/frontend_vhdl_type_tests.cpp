// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

[[maybe_unused]] std::filesystem::path make_test_directory(
    std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-" + std::string{name} + "-"
         + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

[[maybe_unused]] void write_text(
    const std::filesystem::path& path,
    const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(
      output.good(),
      "frontend test fixture must be writable");
}

} // namespace

void test_vhdl_runtime_integer_nodes() {
  const auto parsed = parse_text(
      "integer_shift.vhd",
      R"(
entity integer_shift is
  port (
    value : in std_logic_vector(7 downto 0);
    count : in integer;
    shifted : out std_logic_vector(7 downto 0);
    observed : out integer
  );
end entity;

architecture rtl of integer_shift is
  signal accumulated : integer;
begin
  shifted <= value sll count;
  calculate: process(count)
    variable local_count : integer := -1;
  begin
    local_count := count + 1;
    accumulated <= abs local_count;
    observed <= accumulated;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      parsed.ok(),
      "base integer ports, signals, variables, and dynamic shifts must "
      "parse");
  const auto* entity =
      parsed.design.find(UnitKind::VhdlEntity, "integer_shift");
  const auto* architecture =
      parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      entity != nullptr && entity->ports.size() == 4
          && entity->ports[1].type.domain
              == ValueDomain::Integer
          && entity->ports[1].type.is_signed
          && entity->ports[1].type.width() == 32
          && entity->ports[3].type.domain
              == ValueDomain::Integer,
      "base integer port type metadata");
  require(
      architecture != nullptr
          && architecture->signals.size() == 1
          && architecture->signals.front().type.domain
              == ValueDomain::Integer
          && architecture->concurrent_statements.size() == 1
          && architecture->concurrent_statements.front()
                 .value.text
              == "sll"
          && architecture->concurrent_statements.front()
                 .value.operands[1].kind
              == ExpressionKind::Identifier
          && architecture->processes.size() == 1
          && architecture->processes.front().variables.size()
              == 1
          && architecture->processes.front().variables.front()
                 .type.domain
              == ValueDomain::Integer,
      "runtime integer objects and dynamic-count HIR");

  const auto constrained_subtypes = parse_text(
      "integer_subtypes.vhd",
      R"(
entity integer_subtypes is
  port (
    count : in natural;
    index : in positive;
    bounded : out integer range -5 to 7;
    reverse : out integer range 3 downto -2
  );
end entity;
architecture rtl of integer_subtypes is
begin
end architecture;
)",
      Language::Vhdl2008);
  require(
      constrained_subtypes.ok(),
      "runtime natural, positive, and explicit integer constraints parse");
  const auto* constrained_entity =
      constrained_subtypes.design.find(
          UnitKind::VhdlEntity, "integer_subtypes");
  require(
      constrained_entity != nullptr
          && constrained_entity->ports.size() == 4
          && constrained_entity->ports[0].type.integer_range
          && constrained_entity->ports[0].type.integer_range->left == 0
          && constrained_entity->ports[0].type.integer_range->right
              == std::numeric_limits<std::int32_t>::max()
          && constrained_entity->ports[1].type.integer_range
          && constrained_entity->ports[1].type.integer_range->left == 1
          && constrained_entity->ports[2].type.integer_range
          && constrained_entity->ports[2].type.integer_range->left == -5
          && constrained_entity->ports[2].type.integer_range->right == 7
          && !constrained_entity->ports[2]
                  .type.integer_range->descending
          && constrained_entity->ports[2]
                 .type.integer_range_expression
          && constrained_entity->ports[3].type.integer_range
          && constrained_entity->ports[3].type.integer_range->left == 3
          && constrained_entity->ports[3].type.integer_range->right == -2
          && constrained_entity->ports[3]
                 .type.integer_range->descending
          && constrained_entity->ports[3].type.width() == 32,
      "integer subtype HIR retains fixed width, bounds, and direction");
}

void test_vhdl_subtype_declarations() {
  const auto result = parse_text(
      "subtype_declarations.vhd",
      R"(
package Subtype_Types is
  subtype Nibble_T is std_logic_vector(3 downto 0);
  subtype Signed_Nibble_T is signed(3 downto 0);
  subtype Flag_T is boolean;
  subtype Bit_Flag_T is bit;
  subtype Level_T is std_logic;
  subtype Count_Base_T is natural range 0 to 15;
  subtype Count_T is Count_Base_T range 2 to 9;
  constant Default_Count : Count_T := 4;
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  subtype Packet_Alias_T is Packet_T;
end package;

use work.subtype_types.all;
entity Subtype_Endpoint is
  generic (Initial_Count : Count_T := 5);
  port (
    Source : in Nibble_T;
    Count : in Count_T
  );
  subtype Port_Count_T is Count_T range 3 to 8;
end entity;

use work.subtype_types.all;
architecture rtl of subtype_endpoint is
  subtype Ascending_T is bit_vector(0 to 3);
  subtype Local_Count_T is Count_T range 4 to 7;
  signal Result : Ascending_T;
  signal Current : Local_Count_T;
  signal Port_Current : Port_Count_T;
begin
  observe : process
    variable Local : Packet_Alias_T;
  begin
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      result.ok(),
      "package, entity, and architecture VHDL subtype declarations parse");
  require(
      result.design.units.size() == 3,
      "subtype fixture retains package, entity, and architecture units");
  const auto& package = result.design.units[0];
  require(
      package.type_aliases.size() == 9
          && package.type_aliases[0].name == "nibble_t"
          && package.type_aliases[0].declaration_kind
              == TypeDeclarationKind::VhdlSubtype
          && package.type_aliases[0].type.domain
              == ValueDomain::Logic9
          && package.type_aliases[0].type.packed_range
          && package.type_aliases[0].type.packed_range->left == 3
          && package.type_aliases[0].type.packed_range->right == 0,
      "packed subtype HIR retains declaration kind, domain, and direction");
  require(
      package.type_aliases[1].type.is_signed
          && package.type_aliases[1].type.width() == 4
          && package.type_aliases[2].type.domain
              == ValueDomain::Boolean
          && package.type_aliases[3].type.domain
              == ValueDomain::Bit2
          && package.type_aliases[4].type.domain
              == ValueDomain::Logic9,
      "signed and scalar logic/bit/Boolean subtype bases retain their "
      "domains");
  require(
      package.type_aliases[5].type.domain
              == ValueDomain::Integer
          && package.type_aliases[5].type.integer_range
          && package.type_aliases[5].type.integer_range->left == 0
          && package.type_aliases[5].type.integer_range->right == 15
          && package.type_aliases[5].type.integer_base_range
          && package.type_aliases[5].type.integer_base_range->left == 0,
      "built-in integer subtype HIR retains derived and base constraints");
  require(
      package.type_aliases[6].type.named_type
              == "count_base_t"
          && package.parameters.size() == 1
          && package.parameters[0].type.named_type
              == "count_t"
          && package.type_aliases[6].type.discrete_range_expression
          && package.type_aliases[7].declaration_kind
              == TypeDeclarationKind::VhdlRecord
          && package.type_aliases[8].type.named_type
              == "packet_t",
      "chained integer and record subtype indications remain named until "
      "semantic resolution");
  const auto& entity = result.design.units[1];
  require(
      entity.type_aliases.size() == 1
          && entity.type_aliases[0].name == "port_count_t"
          && entity.parameters.size() == 1
          && entity.parameters[0].type.named_type == "count_t"
          && entity.ports.size() == 2
          && entity.ports[1].type.named_type == "count_t",
      "entity interfaces retain package subtype references before the "
      "declarative region");
  const auto& architecture = result.design.units[2];
  require(
      architecture.type_aliases.size() == 2
          && architecture.type_aliases[0].type.packed_range
          && !architecture.type_aliases[0]
                  .type.packed_range->descending
          && architecture.signals[0].type.named_type
              == "ascending_t"
          && architecture.signals[2].type.named_type
              == "port_count_t"
          && architecture.processes[0].variables[0]
                 .type.named_type
              == "packet_alias_t",
      "architecture subtype directions and object references are retained");

  const auto invalid = parse_text(
      "invalid_subtype_declarations.vhd",
      R"(
entity invalid_subtype_declarations is
end entity;
architecture rtl of invalid_subtype_declarations is
  subtype Duplicate_T is bit;
  subtype DUPLICATE_T is boolean;
  subtype Missing_Is bit;
  subtype Missing_Semicolon is bit
begin
end architecture;
)",
      Language::Vhdl2008);
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid.diagnostics,
            [&](const auto& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      !invalid.ok()
          && has_code("FSIM-VHDL-PARSE-134")
          && has_code("FSIM-VHDL-PARSE-135")
          && has_code("FSIM-VHDL-SEM-036"),
      "malformed and duplicate subtype declarations have stable diagnostics");
}

void test_vhdl_array_type_declarations() {
  const auto parsed = parse_text(
      "array_types.vhd",
      R"(
package Scalar_Types is
  subtype External_Logic_T is std_logic;
end package;

package Array_Types is
  subtype Logic_Element_T is std_logic;
  type Flags_T is array (natural range <>) of boolean;
  subtype Quartet_T is Flags_T(0 to 3);
  type Logic_Bus_T is array (7 downto 0) of std_logic;
  type Selected_T is array (positive range 1 to 4)
    of Logic_Element_T;
  type Direct_Selected_T is array (0 to 1)
    of work.scalar_types.external_logic_t;
end package;

use work.array_types.all;
entity Array_Endpoint is
  port (
    Flags : in Quartet_T;
    Logic_Bus : out Logic_Bus_T
  );
  type Local_Bits_T is array (integer range <>) of bit;
  subtype Local_Byte_T is Local_Bits_T(0 to 7);
end entity;

use work.array_types.all;
architecture rtl of Array_Endpoint is
  signal Local : Local_Byte_T;
  signal Aggregate_Result : Logic_Bus_T;
begin
  aggregate_forms : process
    variable Positional : Quartet_T :=
      (true, false, true, false);
    variable Named : Quartet_T :=
      (0 | 2 => true, 1 | 3 => false);
  begin
    Named := (0 to 1 => true, others => false);
    Aggregate_Result <=
      (7 downto 4 => '1', 3 | 1 => 'Z', others => '0');
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      parsed.ok(),
      "constrained and unconstrained one-dimensional VHDL arrays parse");
  const auto* package =
      parsed.design.find(UnitKind::VhdlPackage, "array_types");
  require(
      package != nullptr && package->type_aliases.size() == 6,
      "array package retains scalar alias, array declarations, and subtype");
  const auto& flags = package->type_aliases[1];
  require(
      flags.declaration_kind == TypeDeclarationKind::VhdlArray
          && flags.type.vhdl_array
          && flags.type.vhdl_array->unconstrained
          && flags.type.vhdl_array->index_subtype == "natural"
          && flags.type.vhdl_array->index_base_range
          && flags.type.vhdl_array->index_base_range->left == 0
          && flags.type.vhdl_array->element_domain
              == ValueDomain::Boolean
          && flags.type.nominal_type.find(":flags_t")
              != std::string::npos
          && !flags.type.width(),
      "unconstrained array HIR retains nominal/index/element metadata");
  const auto& quartet = package->type_aliases[2];
  require(
      quartet.declaration_kind == TypeDeclarationKind::VhdlSubtype
          && quartet.type.named_type == "flags_t"
          && quartet.type.packed_range
          && quartet.type.packed_range->left == 0
          && quartet.type.packed_range->right == 3
          && quartet.type.packed_range_expression
          && quartet.type.width() == 4,
      "constrained array subtype HIR retains its unresolved base and bounds");
  const auto& logic_bus = package->type_aliases[3];
  require(
      logic_bus.declaration_kind == TypeDeclarationKind::VhdlArray
          && logic_bus.type.vhdl_array
          && !logic_bus.type.vhdl_array->unconstrained
          && logic_bus.type.domain == ValueDomain::Logic9
          && logic_bus.type.packed_range
          && logic_bus.type.packed_range->left == 7
          && logic_bus.type.packed_range->right == 0
          && logic_bus.type.packed_range->descending
          && logic_bus.type.width() == 8,
      "constrained array declaration retains direction and packed width");
  const auto& selected = package->type_aliases[4];
  require(
      selected.type.vhdl_array
          && selected.type.vhdl_array->index_subtype == "positive"
          && selected.type.vhdl_array->element_named_type
              == "logic_element_t"
          && selected.type.domain == ValueDomain::Unknown
          && selected.type.width() == 4,
      "named scalar element subtype remains unresolved in frontend HIR");
  require(
      package->type_aliases[5].type.vhdl_array
          && package->type_aliases[5]
                 .type.vhdl_array->element_named_type
              == "work.scalar_types.external_logic_t",
      "directly selected package element subtype remains qualified in HIR");
  const auto* entity =
      parsed.design.find(UnitKind::VhdlEntity, "array_endpoint");
  require(
      entity != nullptr && entity->type_aliases.size() == 2
          && entity->type_aliases[0].declaration_kind
              == TypeDeclarationKind::VhdlArray
          && entity->type_aliases[0].type.vhdl_array
          && entity->type_aliases[0].type.vhdl_array->index_subtype
              == "integer"
          && entity->type_aliases[1].type.named_type
              == "local_bits_t",
      "entity-local array declarations and constrained subtypes are retained");
  const auto& architecture = parsed.design.units.back();
  require(
      architecture.processes.size() == 1
          && architecture.processes[0].variables.size() == 2,
      "array aggregate initializers remain attached to process variables");
  const auto& positional =
      *architecture.processes[0].variables[0].initializer;
  const auto& named =
      *architecture.processes[0].variables[1].initializer;
  require(
      positional.kind == ExpressionKind::Aggregate
          && positional.operands.size() == 4
          && positional.aggregate_choice_expressions.size() == 4
          && std::ranges::all_of(
              positional.aggregate_choice_expressions,
              [](const auto& choices) {
                return choices.empty();
              }),
      "positional array aggregate associations have no choice expressions");
  require(
      named.kind == ExpressionKind::Aggregate
          && named.operands.size() == 2
          && named.aggregate_choices
              == std::vector<std::string>({"@array", "@array"})
          && named.aggregate_choice_expressions.size() == 2
          && named.aggregate_choice_expressions[0].size() == 2
          && named.aggregate_choice_expressions[1].size() == 2
          && named.aggregate_choice_expressions[0][0].kind
              == ExpressionKind::IntegerLiteral
          && named.aggregate_choice_expressions[0][0]
                 .span.source_name
              == "array_types.vhd",
      "discrete array choice lists retain ordered expression HIR");
  const auto& aggregate_statements =
      architecture.processes[0].statements;
  require(
      aggregate_statements.size() == 3
          && aggregate_statements[0].value.kind
              == ExpressionKind::Aggregate
          && aggregate_statements[0]
                 .value.aggregate_choice_expressions[0][0].kind
              == ExpressionKind::Binary
          && aggregate_statements[0]
                 .value.aggregate_choice_expressions[0][0].text
              == "to"
          && aggregate_statements[0]
                 .value.aggregate_choice_expressions[0][0]
                 .span.source_name
              == "array_types.vhd"
          && aggregate_statements[0].value.aggregate_choices[1]
              == "others"
          && aggregate_statements[1].value.kind
              == ExpressionKind::Aggregate
          && aggregate_statements[1]
                 .value.aggregate_choice_expressions[0][0].text
              == "downto",
      "range and others array aggregate choices retain direction and order");

  const auto rejected = parse_text(
      "invalid_array_types.vhd",
      R"(
package Invalid_Arrays is
  type Matrix_T is array (0 to 1, 0 to 1) of bit;
  type Composite_T is array (0 to 1) of bit_vector(1 downto 0);
  type Bad_Index_T is array (boolean range <>) of bit;
end package;
)",
      Language::Vhdl2008);
  require(
      !rejected.ok()
          && std::ranges::count_if(
                 rejected.diagnostics,
                 [](const Diagnostic& diagnostic) {
                   return diagnostic.code
                       == "FSIM-VHDL-UNSUPPORTED-027";
                 })
              >= 3,
      "multidimensional, composite-element, and invalid-index arrays are "
      "targeted rather than silently accepted");

  const auto invalid_aggregates = parse_text(
      "invalid_array_aggregates.vhd",
      R"(
entity Invalid_Array_Aggregates is
end entity;
architecture rtl of Invalid_Array_Aggregates is
  type Bits_T is array (0 to 3) of bit;
  signal Result : Bits_T;
begin
  Result <= (0 to => '0', others => '1');
  Result <= (0 | => '0', others => '1');
  Result <= (others | 1 => '0');
end architecture;
)",
      Language::Vhdl2008);
  const auto has_aggregate_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid_aggregates.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      !invalid_aggregates.ok()
          && has_aggregate_code("FSIM-VHDL-PARSE-149")
          && has_aggregate_code("FSIM-VHDL-PARSE-150")
          && has_aggregate_code("FSIM-VHDL-SEM-041"),
      "malformed array ranges, choice lists, and others associations have "
      "stable parser diagnostics");
}

void test_vhdl_enumeration_declarations() {
  const auto result = parse_text(
      "enumeration_declarations.vhd",
      R"(
package State_Types is
  type State_T is (Idle, RUNNING, 'A', '0');
  subtype State_Alias_T is State_T;
  constant Initial_State : State_T := Idle;
end package;

entity Enumeration_Endpoint is
  type Local_Flag_T is (Clear, Set);
end entity;

architecture rtl of enumeration_endpoint is
  type Phase_T is (Start, Middle, Finish);
  signal Phase : Phase_T;
begin
end architecture;
)",
      Language::Vhdl2008);
  require(
      result.ok() && result.design.units.size() == 3,
      "package, entity, and architecture enumeration declarations parse");
  const auto& package = result.design.units[0];
  require(
      package.type_aliases.size() == 2
          && package.type_aliases[0].declaration_kind
              == TypeDeclarationKind::VhdlEnumeration
          && package.type_aliases[0].type.domain
              == ValueDomain::Bit2
          && package.type_aliases[0].type.width() == 2
          && !package.type_aliases[0].type.nominal_type.empty()
          && package.type_aliases[0].type.enumeration_literals
              == std::vector<std::string>{
                  "idle", "running", "'A'", "'0'"},
      "enumeration HIR retains nominal identity, literal spelling, and "
      "minimum ordinal width");
  require(
      package.type_aliases[0].enum_literals.size() == 4
          && package.type_aliases[0].enum_literals[2].name == "'A'"
          && package.type_aliases[0].enum_literals[3].value.text == "3"
          && package.type_aliases[1].declaration_kind
              == TypeDeclarationKind::VhdlSubtype
          && package.type_aliases[1].type.named_type == "state_t"
          && package.parameters[0].type.named_type == "state_t",
      "enumeration ordinals, aliases, and typed constants remain explicit");
  require(
      result.design.units[1].type_aliases[0].type.width() == 1
          && result.design.units[2].type_aliases[0].type.width() == 2
          && result.design.units[2].signals[0].type.named_type
              == "phase_t",
      "entity- and architecture-local enumeration types retain scope and "
      "object references");

  const auto invalid = parse_text(
      "invalid_enumeration_declarations.vhd",
      R"(
entity invalid_enumeration_declarations is
end entity;
architecture rtl of invalid_enumeration_declarations is
  type Empty_T is ();
  type Duplicate_T is (Idle, IDLE);
  type Trailing_T is (First,);
  type Missing_Comma_T is (Left Right);
  type DUPLICATE_T is (Other);
begin
end architecture;
)",
      Language::Vhdl2008);
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid.diagnostics,
            [&](const auto& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      !invalid.ok()
          && has_code("FSIM-VHDL-PARSE-137")
          && has_code("FSIM-VHDL-PARSE-138")
          && has_code("FSIM-VHDL-PARSE-139")
          && has_code("FSIM-VHDL-SEM-040")
          && has_code("FSIM-VHDL-SEM-036"),
      "malformed and duplicate enumeration declarations have stable "
      "diagnostics");
}

void test_vhdl_enumeration_attributes() {
  const auto result = parse_text(
      "enumeration_attributes.vhd",
      R"(
entity enumeration_attributes is
end entity;
architecture rtl of enumeration_attributes is
  type State_T is (Idle, Load, Running, Done);
  signal state_result : State_T;
  signal integer_result : integer;
  signal boolean_result : boolean;
begin
  observe : process
  begin
    state_result <= State_T'left;
    state_result <= State_T'right;
    state_result <= State_T'low;
    state_result <= State_T'high;
    integer_result <= State_T'length;
    boolean_result <= State_T'ascending;
    integer_result <= State_T'pos(Running);
    state_result <= State_T'val(2);
    state_result <= State_T'succ(Load);
    state_result <= State_T'pred(Running);
    state_result <= State_T'leftof(Running);
    state_result <= State_T'rightof(Load);
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      result.ok(),
      "VHDL enumeration scalar attributes must parse");
  const auto& statements =
      result.design.units.back().processes.front().statements;
  constexpr std::array<std::string_view, 12> attributes{
      "'left", "'right", "'low", "'high", "'length",
      "'ascending", "'pos", "'val", "'succ", "'pred",
      "'leftof", "'rightof"};
  require(
      statements.size() == attributes.size() + 1,
      "enumeration attribute statement count");
  for (std::size_t index = 0; index < attributes.size(); ++index) {
    require(
        statements[index].value.kind == ExpressionKind::Call
            && statements[index].value.text == attributes[index]
            && statements[index].value.operands.front().text
                == "state_t"
            && (index < 6
                    ? statements[index].value.operands.size() == 1
                    : statements[index].value.operands.size() == 2),
        "enumeration attribute HIR retains type prefix and arity");
  }

  const auto invalid = parse_text(
      "invalid_enumeration_attributes.vhd",
      R"(
entity invalid_enumeration_attributes is
end entity;
architecture rtl of invalid_enumeration_attributes is
  type State_T is (Idle, Done);
  signal result : integer;
begin
  result <= State_T'pos;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid.ok()
          && std::ranges::any_of(
              invalid.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-142";
              }),
      "missing enumeration attribute arguments have a stable diagnostic");
}

void test_vhdl_enumeration_subtype_ranges() {
  const auto result = parse_text(
      "enumeration_subtype_ranges.vhd",
      R"(
package State_Types is
  type State_T is (Idle, Load, Running, Done);
  subtype Active_T is State_T range Load to Done;
  subtype Reverse_T is State_T range Done downto Load;
  subtype Narrow_T is Active_T range Running to Done;
  constant First_Active : Active_T := Load;
end package;

use work.state_types.all;
entity Enumeration_Range_Endpoint is
  port (
    source : in State_T range Load to Running;
    result : out Reverse_T
  );
end entity;

use work.state_types.all;
architecture rtl of enumeration_range_endpoint is
  subtype Local_T is Reverse_T range Running downto Load;
  signal local_value : Local_T;
begin
  result <= local_value;
end architecture;
)",
      Language::Vhdl2008);
  require(
      result.ok() && result.design.units.size() == 3,
      "constrained enumeration subtype indications parse in package, "
      "interface, and architecture regions");
  const auto& package = result.design.units[0];
  require(
      package.type_aliases.size() == 4
          && package.type_aliases[0].type.enumeration_range
          && package.type_aliases[0].type.enumeration_range->left == 0
          && package.type_aliases[0].type.enumeration_range->right == 3
          && !package.type_aliases[0]
                  .type.enumeration_range->descending,
      "base enumeration declarations retain their complete ascending "
      "ordinal range");
  for (std::size_t index = 1; index < 4; ++index) {
    const auto& subtype = package.type_aliases[index];
    require(
        subtype.declaration_kind
                == TypeDeclarationKind::VhdlSubtype
            && subtype.type.discrete_range_expression
            && subtype.type.discrete_range_expression->left.kind
                == ExpressionKind::Identifier
            && subtype.type.discrete_range_expression->right.kind
                == ExpressionKind::Identifier,
        "named enumeration subtype ranges retain typed bound expressions");
  }
  require(
      package.type_aliases[1]
              .type.discrete_range_expression->left.text
              == "load"
          && package.type_aliases[1]
                 .type.discrete_range_expression->right.text
              == "done"
          && !package.type_aliases[1]
                  .type.discrete_range_expression->descending
          && package.type_aliases[2]
                 .type.discrete_range_expression->descending,
      "enumeration subtype range direction and literal spelling are "
      "preserved");
  require(
      result.design.units[1].ports[0].type.discrete_range_expression
          && result.design.units[2]
                 .type_aliases[0]
                 .type.discrete_range_expression
          && result.design.units[2]
                 .signals[0]
                 .type.named_type
              == "local_t",
      "direct object constraints and local constrained subtype references "
      "remain explicit");

  const auto invalid = parse_text(
      "invalid_enumeration_subtype_ranges.vhd",
      R"(
entity invalid_enumeration_subtype_ranges is
end entity;
architecture rtl of invalid_enumeration_subtype_ranges is
  type State_T is (Idle, Done);
  subtype Missing_Direction_T is State_T range Idle Done;
begin
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid.ok()
          && std::ranges::any_of(
              invalid.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-009";
              }),
      "malformed enumeration subtype ranges have a stable parser "
      "diagnostic");
}

void test_exponentiation_expression_nodes() {
  const auto systemverilog = parse_text(
      "power.sv",
      R"(
module power;
  logic [15:0] result;
  always_comb result = 2 ** 3 ** 2;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog exponentiation source must parse");
  const auto& systemverilog_value =
      systemverilog.design.units.front()
          .processes.front()
          .statements.front()
          .value;
  require(
      systemverilog_value.kind == ExpressionKind::Binary
          && systemverilog_value.text == "**"
          && systemverilog_value.operands[0].kind
              == ExpressionKind::Binary
          && systemverilog_value.operands[0].text == "**",
      "SystemVerilog exponentiation must associate left-to-right");

  const auto verilog = parse_text(
      "power.v",
      R"(
module verilog_power;
  reg [15:0] result;
  initial result = 16'd2 ** 16'd3 ** 16'd2;
endmodule
)",
      Language::Verilog2005);
  require(verilog.ok(), "Verilog-2005 exponentiation source must parse");
  const auto& verilog_value =
      verilog.design.units.front()
          .processes.front()
          .statements.front()
          .value;
  require(
      verilog_value.kind == ExpressionKind::Binary
          && verilog_value.text == "**"
          && verilog_value.operands[0].kind
              == ExpressionKind::Binary
          && verilog_value.operands[0].text == "**",
      "Verilog-2005 exponentiation must associate left-to-right");

  const auto vhdl = parse_text(
      "power.vhd",
      R"(
entity power is
end entity;

architecture rtl of power is
  signal result : unsigned(15 downto 0);
  signal signed_result : signed(15 downto 0);
begin
  result <= 2 ** 3;
  signed_result <= -2 ** 2;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "VHDL exponentiation source must parse");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture
          && architecture->concurrent_statements.size() == 2
          && architecture->concurrent_statements.front()
                 .value.text
              == "**"
          && architecture->concurrent_statements[1]
                 .value.kind
              == ExpressionKind::Unary
          && architecture->concurrent_statements[1]
                 .value.operands.front().text
              == "**",
      "VHDL exponentiation precedence relative to a leading sign");

  const auto invalid_vhdl_chain = parse_text(
      "invalid_power_chain.vhd",
      R"(
entity invalid_power_chain is
end entity;

architecture rtl of invalid_power_chain is
  signal result : unsigned(15 downto 0);
begin
  result <= 2 ** 3 ** 2;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl_chain.ok()
          && std::ranges::any_of(
              invalid_vhdl_chain.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-114";
              }),
      "unparenthesized VHDL exponentiation chains must be rejected");

  const auto invalid_vhdl_signed_exponent = parse_text(
      "invalid_signed_exponent.vhd",
      R"(
entity invalid_signed_exponent is
end entity;

architecture rtl of invalid_signed_exponent is
  signal result : unsigned(15 downto 0);
begin
  result <= 2 ** -1;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl_signed_exponent.ok()
          && std::ranges::any_of(
              invalid_vhdl_signed_exponent.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-114";
              }),
      "an unparenthesized signed VHDL exponent must be rejected");
}

void test_systemverilog_procedural_updates() {
  const auto result = parse_text(
      "procedural_updates.sv",
      R"(
module procedural_updates;
  logic [7:0] value;
  logic signed [7:0] signed_value;
  initial begin
    value += 8'd1;
    value -= 8'd1;
    value *= 8'd2;
    value /= 8'd2;
    value %= 8'd3;
    value &= 8'hf0;
    value |= 8'h0f;
    value ^= 8'haa;
    value <<= 8'd1;
    value >>= 8'd1;
    value <<<= 8'd1;
    signed_value >>>= 8'sd1;
    ++value;
    --value;
    value++;
    value--;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "SystemVerilog procedural update statements must parse");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  const std::array<std::string_view, 16> operations{
      "+", "-", "*", "/", "%", "&", "|", "^",
      "<<", ">>", "<<<", ">>>", "+", "-", "+", "-"};
  require(
      statements.size() == operations.size(),
      "all procedural update statements must be retained");
  for (std::size_t index = 0; index < operations.size(); ++index) {
    require(
        statements[index].kind == StatementKind::Assignment
            && statements[index].assignment_kind
                == AssignmentKind::Blocking
            && statements[index].value.kind
                == ExpressionKind::Binary
            && statements[index].value.text == operations[index]
            && statements[index].value.operands.size() == 2,
        "procedural updates must normalize to blocking binary assignments");
  }
  require(
      statements[12].value.operands[1].kind
              == ExpressionKind::IntegerLiteral
          && statements[12].value.operands[1].text == "1"
          && statements[15].value.operands[1].text == "1",
      "standalone increment/decrement must use a contextual unit step");

  const auto unsupported = parse_text(
      "unsupported_update.sv",
      R"(
module unsupported_update;
  logic [7:0] value;
  initial value **= 8'd2;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !unsupported.ok()
          && std::ranges::any_of(
              unsupported.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-UNSUPPORTED-008";
              }),
      "an unsupported procedural update operator must be diagnosed");

  const auto verilog = parse_text(
      "verilog_update.v",
      R"(
module verilog_update;
  reg [7:0] value;
  initial value += 8'd1;
endmodule
)",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && std::ranges::any_of(
              verilog.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-005";
              }),
      "procedural compound assignments must remain SystemVerilog-only");
}

void test_systemverilog_final_procedures() {
  const auto result = parse_text(
      "final_procedure.sv",
      R"(
module final_procedure;
  logic [7:0] value;
  initial value = 8'h12;
  final begin
    value += 8'h03;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "SystemVerilog final procedures must parse");
  const auto& processes = result.design.units.front().processes;
  require(
      processes.size() == 2
          && processes[0].kind == ProcessKind::Initial
          && processes[1].kind == ProcessKind::Final
          && processes[1].statements.size() == 1
          && processes[1].statements.front().value.text == "+",
      "final procedure kind and normalized statement body");

  const auto suspending = parse_text(
      "invalid_final.sv",
      R"(
module invalid_final;
  logic value;
  final begin
    #1;
    value <= 1'b1;
    $stop;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !suspending.ok()
          && std::ranges::any_of(
              suspending.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-032";
              })
          && std::ranges::any_of(
              suspending.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-033";
              }),
      "suspending and nonblocking final-procedure statements are rejected");

  const auto verilog = parse_text(
      "verilog_final.v",
      R"(
module verilog_final;
  reg value;
  final value = 1'b1;
endmodule
)",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && std::ranges::any_of(
              verilog.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-006";
              }),
      "final procedures must remain SystemVerilog-only");
}

void test_verilog_stop_task() {
  for (const auto language :
       {Language::Verilog2005, Language::SystemVerilog2017}) {
    const auto result = parse_text(
        language == Language::Verilog2005
            ? "stop_task.v"
            : "stop_task.sv",
        R"(
module stop_task;
  initial begin
    $stop;
    $stop(1);
  end
endmodule
)",
        language);
    require(
        result.ok(),
        "Verilog/SystemVerilog $stop tasks must parse");
    const auto& statements =
        result.design.units.front().processes.front().statements;
    require(
        statements.size() == 2
            && statements[0].kind == StatementKind::Pause
            && statements[1].kind == StatementKind::Pause,
        "$stop and $stop(argument) must retain resumable pause HIR");
  }

  const auto missing_close = parse_text(
      "invalid_stop_close.sv",
      "module invalid_stop_close; initial $stop(1; endmodule",
      Language::SystemVerilog2017);
  require(
      !missing_close.ok()
          && std::ranges::any_of(
              missing_close.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-112";
              }),
      "a missing $stop closing parenthesis must be targeted");

  const auto missing_semicolon = parse_text(
      "invalid_stop_semicolon.v",
      "module invalid_stop_semicolon; initial $stop endmodule",
      Language::Verilog2005);
  require(
      !missing_semicolon.ok()
          && std::ranges::any_of(
              missing_semicolon.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-113";
              }),
      "a missing $stop semicolon must be targeted");
}

void test_vhdl_conditional_assignments() {
  const auto result = parse_text(
      "conditional_assignment.vhd",
      R"(
entity conditional_assignment is
end entity;

architecture rtl of conditional_assignment is
  signal select_a : boolean;
  signal select_b : boolean;
  signal a : std_logic_vector(3 downto 0);
  signal b : std_logic_vector(3 downto 0);
  signal c : std_logic_vector(3 downto 0);
  signal concurrent_result : std_logic_vector(3 downto 0);
  signal sequential_result : std_logic_vector(3 downto 0);
begin
  concurrent_result <=
      a when select_a else
      b when select_b else
      c;

  choose: process(a, b, select_a)
  begin
    sequential_result <= a when select_a else b;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(result.ok(), "VHDL conditional assignments must parse");
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture
          && architecture->concurrent_statements.size() == 1
          && architecture->processes.size() == 1,
      "VHDL conditional assignments must retain their statement contexts");
  const auto& concurrent =
      architecture->concurrent_statements.front();
  const auto& sequential =
      architecture->processes.front().statements.front();
  require(
      concurrent.kind == StatementKind::If
          && concurrent.statements.size() == 1
          && concurrent.statements[0].value.text == "a"
          && concurrent.else_statements.size() == 1
          && concurrent.else_statements[0].kind == StatementKind::If
          && concurrent.else_statements[0].statements[0].value.text == "b"
          && concurrent.else_statements[0]
                 .else_statements[0]
                 .value.text
              == "c",
      "chained VHDL conditional assignments must nest in source order");
  require(
      sequential.kind == StatementKind::If
          && sequential.statements.size() == 1
          && sequential.statements[0].value.text == "a"
          && sequential.else_statements.size() == 1
          && sequential.else_statements[0].value.text == "b",
      "sequential VHDL-2008 conditional assignments must use common HIR");

  const auto missing_else = parse_text(
      "conditional_missing_else.vhd",
      R"(
entity conditional_missing_else is
end entity;

architecture rtl of conditional_missing_else is
  signal choose : boolean;
  signal a : std_logic;
  signal result : std_logic;
begin
  result <= a when choose;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !missing_else.ok()
          && std::ranges::any_of(
              missing_else.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-115";
              }),
      "a VHDL conditional assignment without else must be diagnosed");
}

} // namespace fsim::tests::frontend
