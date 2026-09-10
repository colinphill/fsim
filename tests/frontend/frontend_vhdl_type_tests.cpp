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

[[maybe_unused]] std::filesystem::path
make_test_directory(std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path() /
      ("fsim-" + std::string{name} + "-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

[[maybe_unused]] void write_text(const std::filesystem::path &path,
                                 const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(output.good(), "frontend test fixture must be writable");
}

} // namespace

void test_vhdl_runtime_integer_nodes() {
  const auto parsed = parse_text("integer_shift.vhd",
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
  require(parsed.ok(),
          "base integer ports, signals, variables, and dynamic shifts must "
          "parse");
  const auto *entity =
      parsed.design.find(UnitKind::VhdlEntity, "integer_shift");
  const auto *architecture =
      parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(entity != nullptr && entity->ports.size() == 4 &&
              entity->ports[1].type.domain == ValueDomain::Integer &&
              entity->ports[1].type.is_signed &&
              entity->ports[1].type.width() == 32 &&
              entity->ports[3].type.domain == ValueDomain::Integer,
          "base integer port type metadata");
  require(
      architecture != nullptr && architecture->signals.size() == 1 &&
          architecture->signals.front().type.domain == ValueDomain::Integer &&
          architecture->concurrent_statements.size() == 1 &&
          architecture->concurrent_statements.front().value.text == "sll" &&
          architecture->concurrent_statements.front().value.operands[1].kind ==
              ExpressionKind::Identifier &&
          architecture->processes.size() == 1 &&
          architecture->processes.front().variables.size() == 1 &&
          architecture->processes.front().variables.front().type.domain ==
              ValueDomain::Integer,
      "runtime integer objects and dynamic-count HIR");

  const auto constrained_subtypes = parse_text("integer_subtypes.vhd",
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
  require(constrained_subtypes.ok(),
          "runtime natural, positive, and explicit integer constraints parse");
  const auto *constrained_entity = constrained_subtypes.design.find(
      UnitKind::VhdlEntity, "integer_subtypes");
  require(constrained_entity != nullptr &&
              constrained_entity->ports.size() == 4 &&
              constrained_entity->ports[0].type.integer_range &&
              constrained_entity->ports[0].type.integer_range->left == 0 &&
              constrained_entity->ports[0].type.integer_range->right ==
                  std::numeric_limits<std::int32_t>::max() &&
              constrained_entity->ports[1].type.integer_range &&
              constrained_entity->ports[1].type.integer_range->left == 1 &&
              constrained_entity->ports[2].type.integer_range &&
              constrained_entity->ports[2].type.integer_range->left == -5 &&
              constrained_entity->ports[2].type.integer_range->right == 7 &&
              !constrained_entity->ports[2].type.integer_range->descending &&
              constrained_entity->ports[2].type.integer_range_expression &&
              constrained_entity->ports[3].type.integer_range &&
              constrained_entity->ports[3].type.integer_range->left == 3 &&
              constrained_entity->ports[3].type.integer_range->right == -2 &&
              constrained_entity->ports[3].type.integer_range->descending &&
              constrained_entity->ports[3].type.width() == 32,
          "integer subtype HIR retains fixed width, bounds, and direction");
}

void test_vhdl_subtype_declarations() {
  const auto result = parse_text("subtype_declarations.vhd",
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
  require(result.ok(),
          "package, entity, and architecture VHDL subtype declarations parse");
  require(result.design.units.size() == 3,
          "subtype fixture retains package, entity, and architecture units");
  const auto &package = result.design.units[0];
  require(package.type_aliases.size() == 9 &&
              package.type_aliases[0].name == "nibble_t" &&
              package.type_aliases[0].declaration_kind ==
                  TypeDeclarationKind::VhdlSubtype &&
              package.type_aliases[0].type.domain == ValueDomain::Logic9 &&
              package.type_aliases[0].type.packed_range &&
              package.type_aliases[0].type.packed_range->left == 3 &&
              package.type_aliases[0].type.packed_range->right == 0,
          "packed subtype HIR retains declaration kind, domain, and direction");
  require(package.type_aliases[1].type.is_signed &&
              package.type_aliases[1].type.width() == 4 &&
              package.type_aliases[2].type.domain == ValueDomain::Boolean &&
              package.type_aliases[3].type.domain == ValueDomain::Bit2 &&
              package.type_aliases[4].type.domain == ValueDomain::Logic9,
          "signed and scalar logic/bit/Boolean subtype bases retain their "
          "domains");
  require(package.type_aliases[5].type.domain == ValueDomain::Integer &&
              package.type_aliases[5].type.integer_range &&
              package.type_aliases[5].type.integer_range->left == 0 &&
              package.type_aliases[5].type.integer_range->right == 15 &&
              package.type_aliases[5].type.integer_base_range &&
              package.type_aliases[5].type.integer_base_range->left == 0,
          "built-in integer subtype HIR retains derived and base constraints");
  require(package.type_aliases[6].type.named_type == "count_base_t" &&
              package.parameters.size() == 1 &&
              package.parameters[0].type.named_type == "count_t" &&
              package.type_aliases[6].type.discrete_range_expression &&
              package.type_aliases[7].declaration_kind ==
                  TypeDeclarationKind::VhdlRecord &&
              package.type_aliases[8].type.named_type == "packet_t",
          "chained integer and record subtype indications remain named until "
          "semantic resolution");
  const auto &entity = result.design.units[1];
  require(entity.type_aliases.size() == 1 &&
              entity.type_aliases[0].name == "port_count_t" &&
              entity.parameters.size() == 1 &&
              entity.parameters[0].type.named_type == "count_t" &&
              entity.ports.size() == 2 &&
              entity.ports[1].type.named_type == "count_t",
          "entity interfaces retain package subtype references before the "
          "declarative region");
  const auto &architecture = result.design.units[2];
  require(architecture.type_aliases.size() == 2 &&
              architecture.type_aliases[0].type.packed_range &&
              !architecture.type_aliases[0].type.packed_range->descending &&
              architecture.signals[0].type.named_type == "ascending_t" &&
              architecture.signals[2].type.named_type == "port_count_t" &&
              architecture.processes[0].variables[0].type.named_type ==
                  "packet_alias_t",
          "architecture subtype directions and object references are retained");

  const auto invalid = parse_text("invalid_subtype_declarations.vhd",
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
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const auto &diagnostic) { return diagnostic.code == code; });
  };
  require(
      !invalid.ok() && has_code("FSIM-VHDL-PARSE-134") &&
          has_code("FSIM-VHDL-PARSE-135") && has_code("FSIM-VHDL-SEM-036"),
      "malformed and duplicate subtype declarations have stable diagnostics");
}

void test_vhdl_array_type_declarations() {
  const auto parsed = parse_text("array_types.vhd",
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
  require(parsed.ok(),
          "constrained and unconstrained one-dimensional VHDL arrays parse");
  const auto *package =
      parsed.design.find(UnitKind::VhdlPackage, "array_types");
  require(
      package != nullptr && package->type_aliases.size() == 6,
      "array package retains scalar alias, array declarations, and subtype");
  const auto &flags = package->type_aliases[1];
  require(flags.declaration_kind == TypeDeclarationKind::VhdlArray &&
              flags.type.vhdl_array && flags.type.vhdl_array->unconstrained &&
              flags.type.vhdl_array->index_subtype == "natural" &&
              flags.type.vhdl_array->index_base_range &&
              flags.type.vhdl_array->index_base_range->left == 0 &&
              flags.type.vhdl_array->element_domain == ValueDomain::Boolean &&
              flags.type.nominal_type.find(":flags_t") != std::string::npos &&
              !flags.type.width(),
          "unconstrained array HIR retains nominal/index/element metadata");
  const auto &quartet = package->type_aliases[2];
  require(
      quartet.declaration_kind == TypeDeclarationKind::VhdlSubtype &&
          quartet.type.named_type == "flags_t" && quartet.type.packed_range &&
          quartet.type.packed_range->left == 0 &&
          quartet.type.packed_range->right == 3 &&
          quartet.type.packed_range_expression && quartet.type.width() == 4,
      "constrained array subtype HIR retains its unresolved base and bounds");
  const auto &logic_bus = package->type_aliases[3];
  require(logic_bus.declaration_kind == TypeDeclarationKind::VhdlArray &&
              logic_bus.type.vhdl_array &&
              !logic_bus.type.vhdl_array->unconstrained &&
              logic_bus.type.domain == ValueDomain::Logic9 &&
              logic_bus.type.packed_range &&
              logic_bus.type.packed_range->left == 7 &&
              logic_bus.type.packed_range->right == 0 &&
              logic_bus.type.packed_range->descending &&
              logic_bus.type.width() == 8,
          "constrained array declaration retains direction and packed width");
  const auto &selected = package->type_aliases[4];
  require(selected.type.vhdl_array &&
              selected.type.vhdl_array->index_subtype == "positive" &&
              selected.type.vhdl_array->element_named_type ==
                  "logic_element_t" &&
              selected.type.domain == ValueDomain::Unknown &&
              selected.type.width() == 4,
          "named scalar element subtype remains unresolved in frontend HIR");
  require(package->type_aliases[5].type.vhdl_array &&
              package->type_aliases[5].type.vhdl_array->element_named_type ==
                  "work.scalar_types.external_logic_t",
          "directly selected package element subtype remains qualified in HIR");
  const auto *entity =
      parsed.design.find(UnitKind::VhdlEntity, "array_endpoint");
  require(
      entity != nullptr && entity->type_aliases.size() == 2 &&
          entity->type_aliases[0].declaration_kind ==
              TypeDeclarationKind::VhdlArray &&
          entity->type_aliases[0].type.vhdl_array &&
          entity->type_aliases[0].type.vhdl_array->index_subtype == "integer" &&
          entity->type_aliases[1].type.named_type == "local_bits_t",
      "entity-local array declarations and constrained subtypes are retained");
  const auto &architecture = parsed.design.units.back();
  require(architecture.processes.size() == 1 &&
              architecture.processes[0].variables.size() == 2,
          "array aggregate initializers remain attached to process variables");
  const auto &positional = *architecture.processes[0].variables[0].initializer;
  const auto &named = *architecture.processes[0].variables[1].initializer;
  require(positional.kind == ExpressionKind::Aggregate &&
              positional.operands.size() == 4 &&
              positional.aggregate_choice_expressions.size() == 4 &&
              std::ranges::all_of(
                  positional.aggregate_choice_expressions,
                  [](const auto &choices) { return choices.empty(); }),
          "positional array aggregate associations have no choice expressions");
  require(named.kind == ExpressionKind::Aggregate &&
              named.operands.size() == 2 &&
              named.aggregate_choices ==
                  std::vector<std::string>({"@array", "@array"}) &&
              named.aggregate_choice_expressions.size() == 2 &&
              named.aggregate_choice_expressions[0].size() == 2 &&
              named.aggregate_choice_expressions[1].size() == 2 &&
              named.aggregate_choice_expressions[0][0].kind ==
                  ExpressionKind::IntegerLiteral &&
              named.aggregate_choice_expressions[0][0].span.source_name ==
                  "array_types.vhd",
          "discrete array choice lists retain ordered expression HIR");
  const auto &aggregate_statements = architecture.processes[0].statements;
  require(
      aggregate_statements.size() == 3 &&
          aggregate_statements[0].value.kind == ExpressionKind::Aggregate &&
          aggregate_statements[0]
                  .value.aggregate_choice_expressions[0][0]
                  .kind == ExpressionKind::Binary &&
          aggregate_statements[0]
                  .value.aggregate_choice_expressions[0][0]
                  .text == "to" &&
          aggregate_statements[0]
                  .value.aggregate_choice_expressions[0][0]
                  .span.source_name == "array_types.vhd" &&
          aggregate_statements[0].value.aggregate_choices[1] == "others" &&
          aggregate_statements[1].value.kind == ExpressionKind::Aggregate &&
          aggregate_statements[1]
                  .value.aggregate_choice_expressions[0][0]
                  .text == "downto",
      "range and others array aggregate choices retain direction and order");

  const auto retained = parse_text("multidimensional_array_types.vhd",
                                   R"(
package Composite_Arrays is
  type Matrix_T is array
    (natural range <>, 3 downto 1) of bit;
  type Packed_Rows_T is array (0 to 1)
    of bit_vector(3 downto 0);
  type Row_T is array (natural range <>) of bit;
  type Nested_Rows_T is array (0 to 1) of Row_T;
  function Read_Cell(
    Value : Matrix_T;
    Row : integer;
    Column : integer) return bit;
  procedure Copy_Rows(
    Value : in Packed_Rows_T;
    Result : out Packed_Rows_T);
end package;

use work.composite_arrays.all;
entity Composite_Endpoint is
  port (
    Matrix : in Matrix_T(0 to 1, 3 downto 1);
    Packed_Rows : out Packed_Rows_T
  );
end entity;

use work.composite_arrays.all;
architecture rtl of Composite_Endpoint is
  signal Local_Matrix : Matrix_T(0 to 1, 3 downto 1);
begin
  retain_forms : process
    variable Nested : Packed_Rows_T :=
      ((0 => '1', others => '0'),
       (3 downto 2 => '1', others => '0'));
    variable Cell : bit;
    variable Pair : bit_vector(1 downto 0);
  begin
    Cell := Local_Matrix(1, 2);
    Pair := Local_Matrix(1, 3 downto 2);
    Local_Matrix(1, 2) <= '1';
    Local_Matrix(0, 3 downto 2) <= "10";
    Nested(1)(2) := '0';
    Packed_Rows <= Nested;
    wait;
  end process;
end architecture;
)",
                                   Language::Vhdl2008);
  require(retained.ok(),
          "multidimensional and composite-array syntax remains in typed HIR");
  const auto *composite_package =
      retained.design.find(UnitKind::VhdlPackage, "composite_arrays");
  require(composite_package != nullptr &&
              composite_package->type_aliases.size() == 4 &&
              composite_package->functions.size() == 1 &&
              composite_package->functions[0].arguments.size() == 3 &&
              composite_package->functions[0].arguments[0].type.named_type ==
                  "matrix_t" &&
              composite_package->functions[0].return_type.domain ==
                  ValueDomain::Bit2 &&
              composite_package->procedures.size() == 1 &&
              composite_package->procedures[0].arguments.size() == 2 &&
              composite_package->procedures[0].arguments[1].type.named_type ==
                  "packed_rows_t",
          "array declarations and callable boundaries are retained");
  const auto &matrix = composite_package->type_aliases[0].type;
  require(matrix.vhdl_array && matrix.vhdl_array->dimensions.size() == 2 &&
              matrix.vhdl_array->dimensions[0].index_subtype == "natural" &&
              matrix.vhdl_array->dimensions[0].unconstrained &&
              matrix.vhdl_array->dimensions[1].constraint &&
              matrix.vhdl_array->dimensions[1].constraint->descending &&
              matrix.vhdl_array->dimensions[1].constraint->span.source_name ==
                  "multidimensional_array_types.vhd" &&
              matrix.vhdl_array->element_types.size() == 1 &&
              matrix.vhdl_array->element_types[0].domain == ValueDomain::Bit2 &&
              !matrix.width(),
          "array HIR retains ordered dimensions and the complete element type");
  const auto &packed_rows = composite_package->type_aliases[1].type;
  require(packed_rows.vhdl_array &&
              packed_rows.vhdl_array->element_types.size() == 1 &&
              packed_rows.vhdl_array->element_types[0].packed_range &&
              packed_rows.vhdl_array->element_types[0].width() == 4 &&
              !packed_rows.width(),
          "direct composite elements retain their packed subtype separately");
  const auto &nested_rows = composite_package->type_aliases[3].type;
  require(nested_rows.vhdl_array &&
              nested_rows.vhdl_array->element_types.size() == 1 &&
              nested_rows.vhdl_array->element_types[0].named_type == "row_t",
          "nested array elements retain their unresolved nominal type");
  const auto *composite_entity =
      retained.design.find(UnitKind::VhdlEntity, "composite_endpoint");
  require(
      composite_entity != nullptr && composite_entity->ports.size() == 2 &&
          composite_entity->ports[0].type.vhdl_array_constraints.size() == 2 &&
          composite_entity->ports[0].type.vhdl_array_constraints[1].descending,
      "multidimensional port constraints retain ordered ranges");
  const auto &composite_architecture = retained.design.units.back();
  require(
      composite_architecture.signals.size() == 1 &&
          composite_architecture.signals[0]
                  .type.vhdl_array_constraints.size() == 2 &&
          composite_architecture.processes.size() == 1 &&
          composite_architecture.processes[0].variables[0].initializer->kind ==
              ExpressionKind::Aggregate,
      "composite array objects and nested aggregates retain HIR");
  const auto &selection_statements =
      composite_architecture.processes[0].statements;
  require(selection_statements.size() == 7 &&
              selection_statements[0].value.kind == ExpressionKind::Call &&
              selection_statements[0].value.operands.size() == 2 &&
              selection_statements[1].value.operands[1].kind ==
                  ExpressionKind::Binary &&
              selection_statements[1].value.operands[1].text == "downto",
          "multidimensional reads and subarray slices retain HIR");
  require(selection_statements[2].target.kind == ExpressionKind::Index &&
              selection_statements[2].target.operands[0].kind ==
                  ExpressionKind::Index &&
              selection_statements[3].target.kind == ExpressionKind::Slice &&
              selection_statements[3].target.operands[0].kind ==
                  ExpressionKind::Index &&
              selection_statements[4].target.operands[0].kind ==
                  ExpressionKind::Index,
          "multidimensional targets and chained selections retain HIR");

  const auto rejected = parse_text("invalid_array_index_type.vhd",
                                   R"(
package Invalid_Arrays is
  type Bad_Index_T is array (boolean range <>) of bit;
end package;
)",
                                   Language::Vhdl2008);
  require(!rejected.ok() &&
              std::ranges::count_if(rejected.diagnostics,
                                    [](const Diagnostic &diagnostic) {
                                      return diagnostic.code ==
                                             "FSIM-VHDL-UNSUPPORTED-027";
                                    }) == 1,
          "unsupported noninteger array index subtypes remain targeted");

  const auto invalid_aggregates = parse_text("invalid_array_aggregates.vhd",
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
  const auto has_aggregate_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid_aggregates.diagnostics,
        [&](const Diagnostic &diagnostic) { return diagnostic.code == code; });
  };
  require(!invalid_aggregates.ok() &&
              has_aggregate_code("FSIM-VHDL-PARSE-149") &&
              has_aggregate_code("FSIM-VHDL-PARSE-150") &&
              has_aggregate_code("FSIM-VHDL-SEM-041"),
          "malformed array ranges, choice lists, and others associations have "
          "stable parser diagnostics");
}

void test_vhdl_access_protected_physical_hir() {
  const auto parsed = parse_text("advanced_vhdl_types.vhd",
                                 R"(
package Advanced_Types is
  type Integer_Pointer is access integer;
  type Distance is range -100 to 1000 units
    um;
    mm = 1000 um;
    meter = 1000 mm;
  end units Distance;
  type Counter is protected
    procedure Add(Value : integer);
    impure function Read return integer;
  end protected Counter;
  constant One_Millimeter : Distance := 1 mm;
end package;

package body Advanced_Types is
  type Counter is protected body
    variable Current_Value : integer := 0;
    procedure Add(Value : integer) is
    begin
      Current_Value := Current_Value + Value;
    end procedure;
    impure function Read return integer is
    begin
      return Current_Value;
    end function;
  end protected body Counter;

  function Allocate(Value : integer) return Integer_Pointer is
    variable Result : Integer_Pointer := null;
  begin
    Result := new integer'(Value);
    Result.all := Result.all + 1;
    return Result;
  end function;
end package body;
)",
                                 Language::Vhdl2008);
  if (!parsed.ok()) {
    for (const auto &diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  require(parsed.ok() && parsed.design.units.size() == 2,
          "access, protected, and physical declarations must parse");

  const auto &declaration = parsed.design.units[0];
  require(declaration.type_aliases.size() == 3,
          "advanced type declarations remain source ordered");
  const auto &access = declaration.type_aliases[0];
  require(access.name == "integer_pointer" &&
              access.declaration_kind == TypeDeclarationKind::VhdlAccess &&
              access.type.vhdl_access &&
              access.type.vhdl_access->designated_types.size() == 1 &&
              access.type.vhdl_access->designated_types[0].domain ==
                  ValueDomain::Integer &&
              access.type.vhdl_access->designated_span.source_name ==
                  "advanced_vhdl_types.vhd",
          "access HIR retains nominal identity and designated subtype span");

  const auto &physical = declaration.type_aliases[1];
  require(physical.name == "distance" &&
              physical.declaration_kind == TypeDeclarationKind::VhdlPhysical &&
              physical.type.vhdl_physical &&
              physical.type.vhdl_physical->range &&
              physical.type.vhdl_physical->range->left.kind ==
                  ExpressionKind::Unary &&
              physical.type.vhdl_physical->range->descending == false &&
              physical.type.vhdl_physical->units.size() == 3 &&
              !physical.type.vhdl_physical->units[0].scale &&
              physical.type.vhdl_physical->units[1].scale &&
              physical.type.vhdl_physical->units[1].scale->kind ==
                  ExpressionKind::Call &&
              physical.type.vhdl_physical->units[1].scale->text ==
                  "@vhdl-physical:um" &&
              physical.type.vhdl_physical->units[2].name == "meter",
          "physical HIR retains range direction and ordered unit scales");
  require(declaration.parameters.size() == 1 &&
              declaration.parameters[0].default_value.kind ==
                  ExpressionKind::Call &&
              declaration.parameters[0].default_value.text ==
                  "@vhdl-physical:mm",
          "physical literals retain their unit independently from magnitude");

  const auto &protected_declaration = declaration.type_aliases[2];
  require(
      protected_declaration.declaration_kind ==
              TypeDeclarationKind::VhdlProtected &&
          protected_declaration.type.vhdl_protected &&
          !protected_declaration.type.vhdl_protected->body &&
          protected_declaration.type.vhdl_protected->procedures.size() == 1 &&
          protected_declaration.type.vhdl_protected->functions.size() == 1 &&
          !protected_declaration.type.vhdl_protected->functions[0].pure,
      "protected declaration HIR retains method profiles and purity");

  const auto &body = parsed.design.units[1];
  require(body.type_aliases.size() == 1 &&
              body.type_aliases[0].declaration_kind ==
                  TypeDeclarationKind::VhdlProtectedBody &&
              body.type_aliases[0].type.vhdl_protected &&
              body.type_aliases[0].type.vhdl_protected->body &&
              body.type_aliases[0].type.vhdl_protected->variables.size() == 1 &&
              body.type_aliases[0].type.vhdl_protected->variables[0]
                  .vhdl_private &&
              body.type_aliases[0].type.vhdl_protected->procedures[0].defined &&
              body.type_aliases[0].type.vhdl_protected->functions[0].defined,
          "protected body HIR retains private state and method bodies");
  require(
      body.functions.size() == 1 && body.functions[0].variables.size() == 1 &&
          body.functions[0].variables[0].initializer &&
          body.functions[0].variables[0].initializer->text == "@vhdl-null" &&
          body.functions[0].statements.size() == 3 &&
          body.functions[0].statements[0].value.text == "@vhdl-new-qualified" &&
          body.functions[0].statements[1].target.text == "@vhdl-dereference" &&
          body.functions[0].statements[1].value.kind ==
              ExpressionKind::Binary &&
          body.functions[0].statements[1].value.operands[0].text ==
              "@vhdl-dereference",
      "null, allocators, and dereference reads/writes retain distinct HIR");

  const auto malformed = parse_text("invalid_advanced_vhdl_types.vhd",
                                    R"(
package Invalid_Advanced_Types is
  type Distance is range 0 to 10 units
    step;
  end units Wrong_Name;
end package;
)",
                                    Language::Vhdl2008);
  require(
      !malformed.ok() &&
          std::ranges::any_of(malformed.diagnostics,
                              [](const auto &diagnostic) {
                                return diagnostic.code == "FSIM-VHDL-SEM-090" &&
                                       diagnostic.span.source_name ==
                                           "invalid_advanced_vhdl_types.vhd";
                              }),
      "physical closing-name failures retain a targeted source span");
}

void test_vhdl_2019_protected_type_updates() {
  constexpr std::string_view source = R"(
package Generic_Protected_Types is
  type Integer_File is file of integer;
  type Integer_Access is access integer;
  type Store is protected
    generic (
      type Element_T;
      Limit : integer := 4;
      function Equivalent(Left, Right : Element_T) return boolean);
    variable Count : integer := 0;
    private shared variable Hidden : Store;
    procedure Load(file Source : Integer_File; Pointer : Integer_Access);
    impure function Same(Peer : Store) return boolean;
    alias Reload is Load [Integer_File, Integer_Access];
  end protected Store;
end package;
)";
  const auto parsed = parse_text(
      "vhdl2019-protected.vhd", source, Language::Vhdl2008,
      VhdlStandard::Vhdl2019);
  if (!parsed.ok()) {
    for (const auto &diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  require(parsed.ok() && parsed.design.units.size() == 1,
          "VHDL-2019 protected type updates must parse");
  const auto &unit = parsed.design.units.front();
  require(unit.type_aliases.size() == 3 &&
              unit.type_aliases.back().type.vhdl_protected,
          "generic protected type retains its nominal declaration");
  const auto &info = *unit.type_aliases.back().type.vhdl_protected;
  require(info.generic_parameters.size() == 3 &&
              info.generic_parameters[0].kind == ParameterKind::Type &&
              info.generic_parameters[1].kind == ParameterKind::Value &&
              info.generic_parameters[1].default_value.text == "4" &&
              info.generic_parameters[2].kind == ParameterKind::Function,
          "protected generic interface retains type, value, and function "
          "formals");
  require(info.variables.size() == 2 &&
              info.variables[0].name == "count" &&
              !info.variables[0].vhdl_shared &&
              !info.variables[0].vhdl_private &&
              info.variables[1].name == "hidden" &&
              info.variables[1].vhdl_shared &&
              info.variables[1].vhdl_private,
          "protected public and private variables retain their qualifiers");
  require(info.procedures.size() == 1 &&
              info.procedures[0].arguments.size() == 2 &&
              info.procedures[0].arguments[0].object_class ==
                  InterfaceObjectClass::File &&
              info.functions.size() == 1 &&
              info.functions[0].arguments.size() == 1 &&
              info.functions[0].arguments[0].type.named_type == "store",
          "protected methods retain VHDL-2019 parameter classes and types");
  require(info.method_aliases.size() == 1 &&
              info.method_aliases[0].name == "reload" &&
              info.method_aliases[0].actual == "load",
          "protected method aliases retain their selected target");

  const auto rejected = parse_text(
      "vhdl2008-protected-updates.vhd", source, Language::Vhdl2008,
      VhdlStandard::Vhdl2008);
  require(!rejected.ok() &&
              std::ranges::count_if(
                  rejected.diagnostics, [](const Diagnostic &diagnostic) {
                    return diagnostic.code == "FSIM-FE-VHSTD-003";
                  }) >= 6,
          "VHDL-2019 protected type updates stay isolated from VHDL-2008");

  const auto legacy_private = parse_text(
      "vhdl2008-private-name.vhd",
      "package private is end package private;", Language::Vhdl2008,
      VhdlStandard::Vhdl2008);
  const auto reserved_private = parse_text(
      "vhdl2019-private-name.vhd",
      "package private is end package private;", Language::Vhdl2008,
      VhdlStandard::Vhdl2019);
  require(legacy_private.ok() && !reserved_private.ok() &&
              std::ranges::any_of(
                  reserved_private.diagnostics,
                  [](const Diagnostic &diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-LEX-002";
                  }),
          "private becomes reserved only in the VHDL-2019 lexical profile");
}

void test_vhdl_2019_unspecified_types() {
  constexpr std::string_view source = R"(
entity Unspecified_Interfaces is
  generic (
    type Any_T is private;
    type Scalar_T is <>;
    type Discrete_T is (<>);
    type Integer_T is range <>;
    type Physical_T is units <>;
    type Floating_T is range <>.<>;
    type Array_T is array (type is (<>)) of type is private;
    type Access_T is access type is private;
    type File_T is file of type is private
  );
  port (
    Left, Right : in type is private;
    Values : in type Value_Array is array (type is (<>)) of type is <>
  );
end entity;
)";
  const auto parsed = parse_text(
      "vhdl2019-unspecified.vhd", source, Language::Vhdl2008,
      VhdlStandard::Vhdl2019);
  if (!parsed.ok()) {
    for (const auto &diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  require(parsed.ok() && parsed.design.units.size() == 1,
          "VHDL-2019 unspecified type categories must parse");
  const auto &unit = parsed.design.units.front();
  require(unit.parameters.size() == 9 && unit.ports.size() == 3,
          "unspecified generic and port profiles retain every formal");
  constexpr std::array expected{
      VhdlUnspecifiedTypeClass::Private,
      VhdlUnspecifiedTypeClass::Scalar,
      VhdlUnspecifiedTypeClass::Discrete,
      VhdlUnspecifiedTypeClass::Integer,
      VhdlUnspecifiedTypeClass::Physical,
      VhdlUnspecifiedTypeClass::Floating,
      VhdlUnspecifiedTypeClass::Array,
      VhdlUnspecifiedTypeClass::Access,
      VhdlUnspecifiedTypeClass::File};
  for (std::size_t index = 0; index < expected.size(); ++index) {
    require(unit.parameters[index].kind == ParameterKind::Type &&
                unit.parameters[index].type.vhdl_unspecified &&
                unit.parameters[index].type.vhdl_unspecified->type_class ==
                    expected[index],
            "each generic retains its exact unspecified type category");
  }
  const auto &array = *unit.parameters[6].type.vhdl_unspecified;
  require(array.array_index_count == 1 && array.component_types.size() == 2 &&
              array.component_types[0].vhdl_unspecified &&
              array.component_types[0].vhdl_unspecified->type_class ==
                  VhdlUnspecifiedTypeClass::Discrete &&
              array.component_types[1].vhdl_unspecified &&
              array.component_types[1].vhdl_unspecified->type_class ==
                  VhdlUnspecifiedTypeClass::Private,
          "array unspecified profiles retain index and element categories");
  require(unit.ports[0].type.vhdl_unspecified &&
              unit.ports[1].type.vhdl_unspecified &&
              unit.ports[2].type.vhdl_unspecified &&
              unit.ports[0].type.vhdl_unspecified->inference_identity ==
                  unit.ports[1].type.vhdl_unspecified->inference_identity &&
              unit.ports[0].type.vhdl_unspecified->inference_identity !=
                  unit.ports[2].type.vhdl_unspecified->inference_identity &&
              unit.ports[2].type.vhdl_unspecified->inference_identity
                  .ends_with(":value_array"),
          "names in one interface declaration share one inference identity");

  const Type integer_actual{
      ValueDomain::Integer, "integer", std::nullopt, true};
  const Type string_actual{
      ValueDomain::String, "string", std::nullopt, false};
  require(vhdl_unspecified_type_accepts(
              unit.parameters[3].type, integer_actual) &&
              !vhdl_unspecified_type_accepts(
                  unit.parameters[3].type, string_actual) &&
              vhdl_unspecified_type_accepts(
                  unit.parameters[0].type, string_actual),
          "unspecified type categories accept only legal actual types");
  require(vhdl_inferred_type_identity(integer_actual) !=
              vhdl_inferred_type_identity(string_actual),
          "distinct actual types retain distinct inference identities");

  const auto rejected = parse_text(
      "vhdl2008-unspecified.vhd", source, Language::Vhdl2008,
      VhdlStandard::Vhdl2008);
  require(!rejected.ok() &&
              std::ranges::count_if(
                  rejected.diagnostics, [](const Diagnostic &diagnostic) {
                    return diagnostic.code == "FSIM-FE-VHSTD-003";
                  }) >= 12,
          "unspecified types stay isolated from VHDL-2008");

  const auto malformed = parse_text(
      "vhdl2019-unspecified-invalid.vhd",
      "entity Bad is generic (type T is nonsense); end entity;",
      Language::Vhdl2008, VhdlStandard::Vhdl2019);
  require(!malformed.ok() &&
              std::ranges::any_of(
                  malformed.diagnostics, [](const Diagnostic &diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-PARSE-286";
                  }),
          "malformed unspecified categories have one stable diagnostic");
}

void test_vhdl_2019_mode_view_declarations() {
  constexpr std::string_view source = R"(
package Mode_Views is
  type Lane_T is record
    Valid : bit;
    Data : bit_vector(7 downto 0);
    Ready : bit;
    Hold : bit;
  end record Lane_T;
  type Lane_Array_T is array (natural range <>) of Lane_T;
  type Bus_T is record
    Lane : Lane_T;
    Lanes : Lane_Array_T(0 to 1);
  end record Bus_T;

  view Producer of Lane_T is
    Valid, Data : out;
    Ready : in;
    Hold : buffer;
  end view Producer;
  view Bus_View of Bus_T is
    Lane : view Producer;
    Lanes : view (Producer);
  end view Bus_View;
end package;
)";
  const auto parsed = parse_text(
      "vhdl2019-mode-views.vhd", source, Language::Vhdl2008,
      VhdlStandard::Vhdl2019);
  if (!parsed.ok()) {
    for (const auto &diagnostic : parsed.diagnostics) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  require(parsed.ok() && parsed.design.units.size() == 1,
          "VHDL-2019 mode view declarations must parse");
  const auto &declarations = parsed.design.units.front().type_aliases;
  const auto producer = std::ranges::find_if(
      declarations, [](const TypeAliasDeclaration &declaration) {
        return declaration.name == "producer";
      });
  const auto bus_view = std::ranges::find_if(
      declarations, [](const TypeAliasDeclaration &declaration) {
        return declaration.name == "bus_view";
      });
  require(producer != declarations.end() &&
              producer->declaration_kind ==
                  TypeDeclarationKind::VhdlModeView &&
              producer->type.named_type == "lane_t" &&
              producer->vhdl_mode_view_elements.size() == 4,
          "a mode view retains its record subtype and expands grouped names");
  require(producer->vhdl_mode_view_elements[0].name == "valid" &&
              producer->vhdl_mode_view_elements[0].direction ==
                  PortDirection::Output &&
              producer->vhdl_mode_view_elements[1].name == "data" &&
              producer->vhdl_mode_view_elements[1].direction ==
                  PortDirection::Output &&
              producer->vhdl_mode_view_elements[2].direction ==
                  PortDirection::Input &&
              producer->vhdl_mode_view_elements[3].direction ==
                  PortDirection::Buffer,
          "each simple mode view element retains its explicit direction");
  require(bus_view != declarations.end() &&
              bus_view->vhdl_mode_view_elements.size() == 2 &&
              bus_view->vhdl_mode_view_elements[0].kind ==
                  VhdlModeViewElementKind::record_view &&
              bus_view->vhdl_mode_view_elements[0].referenced_view ==
                  "producer" &&
              bus_view->vhdl_mode_view_elements[1].kind ==
                  VhdlModeViewElementKind::array_view &&
              bus_view->vhdl_mode_view_elements[1].referenced_view ==
                  "producer",
          "nested record and array view indications retain distinct nodes");

  const auto older = parse_text(
      "vhdl2008-mode-views.vhd", source, Language::Vhdl2008,
      VhdlStandard::Vhdl2008);
  require(!older.ok() && std::ranges::any_of(
              older.diagnostics, [](const Diagnostic &diagnostic) {
                return diagnostic.code == "FSIM-FE-VHSTD-003";
              }),
          "mode view declarations stay isolated from VHDL-2008");

  const auto malformed = parse_text(
      "vhdl2019-mode-view-invalid.vhd", R"(
package Bad_Views is
  type Pair_T is record Left, Right : bit; end record;
  view Broken of Pair_T is
    Left : linkage;
    Left : sideways;
  end view Different;
end package;
)", Language::Vhdl2008, VhdlStandard::Vhdl2019);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        malformed.diagnostics, [&](const Diagnostic &diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(!malformed.ok() && has_code("FSIM-VHDL-PARSE-287") &&
              has_code("FSIM-VHDL-SEM-107") &&
              has_code("FSIM-VHDL-SEM-108"),
          "malformed, duplicate, mismatched, and linkage elements use stable "
          "mode view diagnostics");

  const auto legacy_identifier = parse_text(
      "vhdl2008-view-identifier.vhd",
      "package view is end package view;", Language::Vhdl2008,
      VhdlStandard::Vhdl2008);
  const auto reserved_identifier = parse_text(
      "vhdl2019-view-identifier.vhd",
      "package view is end package view;", Language::Vhdl2008,
      VhdlStandard::Vhdl2019);
  require(legacy_identifier.ok() && !reserved_identifier.ok() &&
              std::ranges::any_of(
                  reserved_identifier.diagnostics,
                  [](const Diagnostic &diagnostic) {
                    return diagnostic.code == "FSIM-VHDL-LEX-002";
                  }),
          "view becomes reserved only in the VHDL-2019 lexical profile");
}

void test_vhdl_revision_declaration_profiles()
{
    const auto diagnostics_with_code = [](const ParseResult& parsed,
                                           const std::string_view code) {
        return std::ranges::count_if(
            parsed.diagnostics, [&](const Diagnostic& diagnostic) {
                return diagnostic.code == code;
            });
    };

    const auto vhdl_1987 = parse_text(
        "vhdl87-declarations.vhd",
        R"(
package Legacy_Types is
  type Text_File is file of string;
  type Wide_T is array (136 downto 0) of bit;
  subtype Descending_T is Wide_T(105 downto 8);
  attribute Trace_Id : integer;
end package;
architecture rtl of legacy_endpoint is
  signal Bus_Value : Wide_T;
  file Input_File : Text_File is in "legacy.txt";
  alias Bus_Alias : Wide_T is Bus_Value;
begin
end architecture;
)",
        Language::Vhdl2008, VhdlStandard::Vhdl1987);
    require(vhdl_1987.ok() && vhdl_1987.design.units.size() == 2,
        "VHDL-1987 declaration, alias, attribute, access/file, and subtype "
        "forms remain available");
    const auto& legacy_package = vhdl_1987.design.units.front();
    const auto& legacy_architecture = vhdl_1987.design.units.back();
    require(
        legacy_package.type_aliases.size() == 3 && legacy_package.type_aliases[1].type.width() == 137U && legacy_package.type_aliases[2].type.packed_range && legacy_package.type_aliases[2].type.packed_range->left == 105 && legacy_package.type_aliases[2].type.packed_range->right == 8 && legacy_package.type_aliases[2].type.packed_range->descending && legacy_package.vhdl_attributes.size() == 1 && legacy_architecture.variables.size() == 1 && legacy_architecture.variables.front().vhdl_file_open_kind && legacy_architecture.variables.front().vhdl_file_open_kind->text == "read_mode" && legacy_architecture.signal_aliases.size() == 1,
        "VHDL-1987 HIR retains exact 137-bit bounds, descending direction, "
        "legacy file mode, attribute, and alias identity");

    const auto vhdl_1993 = parse_text(
        "vhdl93-declarations.vhd",
        R"(
architecture rtl of shared_endpoint is
  shared variable Shared_State : integer := 0;
  group Signal_Group is (signal <>);
begin
end architecture;
)",
        Language::Vhdl2008, VhdlStandard::Vhdl1993);
    require(vhdl_1993.ok() && vhdl_1993.design.units.front().variables.size() == 1 && vhdl_1993.design.units.front().variables.front().vhdl_shared && vhdl_1993.design.units.front().vhdl_groups.size() == 1,
        "VHDL-1993 admits shared variables and group declarations");

    const auto vhdl_2000 = parse_text(
        "vhdl2000-declarations.vhd",
        R"(
package Protected_Types is
  type Guard_T is protected
    procedure Lock;
  end protected Guard_T;
end package;
)",
        Language::Vhdl2008, VhdlStandard::Vhdl2000);
    require(vhdl_2000.ok() && vhdl_2000.design.units.front().type_aliases.size() == 1 && vhdl_2000.design.units.front().type_aliases.front().type.vhdl_protected,
        "VHDL-2000 admits protected type declarations");

    const auto vhdl_2002 = parse_text(
        "vhdl2002-declarations.vhd",
        R"(
package File_Types is
  type Text_File is file of string;
end package;
architecture rtl of file_endpoint is
  file Output_File : Text_File open write_mode is "modern.txt";
begin
end architecture;
)",
        Language::Vhdl2008, VhdlStandard::Vhdl2002);
    require(vhdl_2002.ok() && vhdl_2002.design.units.back().variables.front().vhdl_file && vhdl_2002.design.units.back().variables.front().vhdl_file_open_kind->text == "write_mode",
        "VHDL-2002 retains the VHDL-1993 file open-kind declaration form");

    const auto vhdl_2008 = parse_text(
        "vhdl2008-declarations.vhd",
        R"(
package Generic_Types is
  generic (
    type Element_T;
    function Convert(Value : integer) return integer is <>;
    procedure Observe(Value : integer) is <>);
  type Matrix_T is array (7 downto 0) of bit_vector;
end package;
entity Generic_Endpoint is
  generic (
    package Selected is new work.generic_types generic map (<>));
end entity;
)",
        Language::Vhdl2008, VhdlStandard::Vhdl2008);
    require(
        vhdl_2008.ok() && vhdl_2008.design.units.front().parameters.size() == 3 && vhdl_2008.design.units.front().type_aliases.size() == 1 && vhdl_2008.design.units.front().type_aliases.front().type.vhdl_array->element_spelling == "bit_vector" && vhdl_2008.design.units.back().parameters.front().kind == ParameterKind::Package,
        "VHDL-2008 admits type/subprogram/package interfaces and unconstrained "
        "array elements");

    for (const auto standard : { VhdlStandard::Vhdl1987,
             VhdlStandard::Vhdl1993,
             VhdlStandard::Vhdl2000,
             VhdlStandard::Vhdl2002 }) {
        const auto aggregate = parse_text(
            "older-static-aggregate.vhd",
            R"(
architecture rtl of aggregate_endpoint is
  signal Result : bit_vector(136 downto 0);
begin
  Result <= (136 downto 69 => '1', others => '0');
end architecture;
)",
            Language::Vhdl2008, standard);
        require(
            aggregate.ok() && aggregate.design.units.front().signals.front().type.width() == 137U && aggregate.design.units.front().concurrent_statements.front().value.kind == ExpressionKind::Aggregate && aggregate.design.units.front().concurrent_statements.front().value.aggregate_choice_expressions.front().front().text == "downto",
            "all older revisions retain exact static aggregate choices, width, "
            "and descending direction");
    }

    constexpr std::array<std::string_view, 4> synopsys_packages {
        "std_logic_signed", "std_logic_unsigned", "std_logic_arith",
        "std_logic_misc"
    };
    for (const auto standard : { VhdlStandard::Vhdl1987,
             VhdlStandard::Vhdl1993,
             VhdlStandard::Vhdl2000,
             VhdlStandard::Vhdl2002 }) {
        for (const auto package_name : synopsys_packages) {
            const auto source = "package " + std::string { package_name } + " is\n" + "  subtype operand_t is std_logic_vector;\n" + "  subtype ascending_t is std_logic_vector(8 to 105);\n" + "  function \"+\" (left, right : operand_t) return operand_t;\n" + "end package;\n";
            const auto parsed = parse_text(
                "ieee/" + std::string { package_name } + ".vhd", source,
                Language::Vhdl2008, standard);
            require(
                parsed.ok() && parsed.design.units.front().type_aliases.size() == 2 && parsed.design.units.front().functions.size() == 1 && parsed.design.units.front().functions.front().name == "+" && parsed.design.units.front().type_aliases[1].type.width() == 98U && !parsed.design.units.front().type_aliases[1].type.packed_range->descending,
                "Synopsys package declaration profiles retain unconstrained "
                "operands, operator names, exact ascending bounds, and direction "
                "in every older revision");
        }
    }

    const auto unavailable = [&](const std::string_view name,
                                 const std::string_view source,
                                 const VhdlStandard standard) {
        const auto parsed = parse_text(std::string(name), std::string(source),
            Language::Vhdl2008, standard);
        require(!parsed.ok() && diagnostics_with_code(parsed, "FSIM-FE-VHSTD-003") == 1,
            "a declaration unavailable in the selected revision has one "
            "revision diagnostic");
        return parsed;
    };

    const auto shared_in_1987 = unavailable(
        "vhdl87-shared.vhd",
        "architecture rtl of e is\n  shared variable Value : integer;\n"
        "begin\nend architecture;\n",
        VhdlStandard::Vhdl1987);
    const auto shared_diagnostic = std::ranges::find_if(
        shared_in_1987.diagnostics, [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-FE-VHSTD-003";
        });
    require(shared_diagnostic != shared_in_1987.diagnostics.end() && shared_diagnostic->span.begin.line == 2U && shared_diagnostic->span.begin.column == 3U && shared_diagnostic->message.find("VHDL-1993") != std::string::npos,
        "revision diagnostics retain the exact feature token and migration "
        "revision");

    (void)unavailable(
        "vhdl87-group.vhd",
        "package p is\n  group G is (signal <>);\nend package;\n",
        VhdlStandard::Vhdl1987);
    (void)unavailable(
        "vhdl93-protected.vhd",
        "package p is\n  type Guard is protected\n  end protected Guard;\n"
        "end package;\n",
        VhdlStandard::Vhdl1993);
    (void)unavailable(
        "vhdl2002-interface-type.vhd",
        "entity e is\n  generic (type T);\nend entity;\n",
        VhdlStandard::Vhdl2002);
    (void)unavailable(
        "vhdl2002-interface-package.vhd",
        "entity e is\n  generic (package P is new work.template generic "
        "map (<>));\nend entity;\n",
        VhdlStandard::Vhdl2002);
    (void)unavailable(
        "vhdl2002-unconstrained-element.vhd",
        "package p is\n  type Matrix_T is array (0 to 3) of bit_vector;\n"
        "end package;\n",
        VhdlStandard::Vhdl2002);
    (void)unavailable(
        "vhdl87-open-kind.vhd",
        "architecture rtl of e is\n  file F : text open read_mode is "
        "\"input.txt\";\nbegin\nend architecture;\n",
        VhdlStandard::Vhdl1987);
    (void)unavailable(
        "vhdl93-legacy-file-mode.vhd",
        "architecture rtl of e is\n  file F : text is in \"input.txt\";\n"
        "begin\nend architecture;\n",
        VhdlStandard::Vhdl1993);
    (void)unavailable(
        "vhdl87-alias-signature.vhd",
        "architecture rtl of e is\n  signal Value : bit;\n"
        "  alias Selected : bit is Value [integer return integer];\n"
        "begin\nend architecture;\n",
        VhdlStandard::Vhdl1987);

    const auto malformed = parse_text(
        "vhdl2008-malformed-alias-signature.vhd",
        "architecture rtl of e is\n  signal Value : bit;\n"
        "  alias Selected : bit is Value [];\nbegin\nend architecture;\n",
        Language::Vhdl2008, VhdlStandard::Vhdl2008);
    require(!malformed.ok() && diagnostics_with_code(malformed, "FSIM-VHDL-PARSE-236") == 1 && diagnostics_with_code(malformed, "FSIM-FE-VHSTD-003") == 0,
        "malformed syntax is distinguished from revision availability");
}

void test_vhdl_incomplete_type_declarations() {
    const auto parsed = parse_text("incomplete_vhdl_types.vhd",
        R"(
package Recursive_Types is
  type Node;
  type Node_Access is access Node;
  type Node is record
    Value : integer;
    Next_Link : Node_Access;
  end record Node;
end package;
)",
        Language::Vhdl2008);
    require(parsed.ok(),
        "a VHDL incomplete type must admit its full declaration");
    const auto& package = parsed.design.units.front();
    require(
        package.type_aliases.size() == 2 && package.type_aliases[0].name == "node_access" && package.type_aliases[0].declaration_kind == TypeDeclarationKind::VhdlAccess && package.type_aliases[0].type.vhdl_access && package.type_aliases[0].type.vhdl_access->designated_types.front().named_type == "node" && package.type_aliases[1].name == "node" && package.type_aliases[1].declaration_kind == TypeDeclarationKind::VhdlRecord && package.type_aliases[1].type.packed_members.size() == 2 && package.type_aliases[1].type.packed_members[0].domain == ValueDomain::Integer && package.type_aliases[1].type.packed_members[1].nested_types.front().named_type == "node_access",
        "completion replaces the incomplete marker while preserving recursive "
        "access identity and integer record members");

    const auto malformed = parse_text("invalid_incomplete_vhdl_types.vhd",
        R"(
package Invalid_Recursive_Types is
  type Missing;
  type Duplicate;
  type DUPLICATE;
end package;
)",
        Language::Vhdl2008);
    const auto count_code = [&](const std::string_view code) {
        return std::ranges::count_if(
            malformed.diagnostics,
            [&](const Diagnostic& diagnostic) { return diagnostic.code == code; });
    };
    require(!malformed.ok() && count_code("FSIM-VHDL-SEM-036") == 1 && count_code("FSIM-VHDL-SEM-097") == 2,
        "duplicate and uncompleted incomplete types need exact diagnostics");
}

} // namespace fsim::tests::frontend
