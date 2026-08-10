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

void test_vhdl_incomplete_type_declarations() {
  const auto parsed = parse_text("incomplete_vhdl_types.vhd",
                                 R"(
package Recursive_Types is
  type Node;
  type Node_Access is access Node;
  type Node is record
    Value : integer;
    Next : Node_Access;
  end record Node;
end package;
)",
                                 Language::Vhdl2008);
  require(parsed.ok(),
          "a VHDL incomplete type must admit its full declaration");
  const auto &package = parsed.design.units.front();
  require(
      package.type_aliases.size() == 2 &&
          package.type_aliases[0].name == "node_access" &&
          package.type_aliases[0].declaration_kind ==
              TypeDeclarationKind::VhdlAccess &&
          package.type_aliases[0].type.vhdl_access &&
          package.type_aliases[0]
                  .type.vhdl_access->designated_types.front()
                  .named_type == "node" &&
          package.type_aliases[1].name == "node" &&
          package.type_aliases[1].declaration_kind ==
              TypeDeclarationKind::VhdlRecord &&
          package.type_aliases[1].type.packed_members.size() == 2 &&
          package.type_aliases[1].type.packed_members[0].domain ==
              ValueDomain::Integer &&
          package.type_aliases[1]
                  .type.packed_members[1]
                  .nested_types.front()
                  .named_type == "node_access",
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
        [&](const Diagnostic &diagnostic) { return diagnostic.code == code; });
  };
  require(!malformed.ok() && count_code("FSIM-VHDL-SEM-036") == 1 &&
              count_code("FSIM-VHDL-SEM-097") == 2,
          "duplicate and uncompleted incomplete types need exact diagnostics");
}

void test_vhdl_nested_composite_hir() {
  const auto retained = parse_text("nested_composite_hir.vhd",
                                   R"(
package Composite_Types is
  type Mode_T is (Idle, Ready, Busy);
  type Payload_T is record
    Lane : std_logic_vector(3 downto 0);
    Mode : Mode_T;
  end record Payload_T;
  type Payload_Array_T is array (0 to 1) of Payload_T;
  type Envelope_T is record
    Payload : Payload_T;
    Items : Payload_Array_T;
    Tag : Mode_T;
  end record Envelope_T;
end package;

use work.composite_types.all;
entity Nested_Composite_Hir is
end entity;

use work.composite_types.all;
architecture rtl of nested_composite_hir is
  signal Left_Value : Envelope_T;
  signal Right_Value : Envelope_T;
  signal Equal_Value : boolean;
  signal Array_Length : integer;
begin
  observe: process
    variable Local_Value : Envelope_T := Envelope_T'(
      Payload => Payload_T'(Lane => "1010", Mode => Ready),
      Items => Payload_Array_T'(
        0 => Payload_T'(Lane => "0001", Mode => Idle),
        others => Payload_T'(Lane => "0010", Mode => Busy)),
      Tag => Ready);
  begin
    Left_Value <= Local_Value;
    Equal_Value <= Left_Value = Right_Value;
    Array_Length <= Payload_Array_T'length;
    wait;
  end process;
end architecture;
)",
                                   Language::Vhdl2008);
  require(retained.ok(),
          "nested composite types and qualified expressions must retain HIR");
  const auto *package =
      retained.design.find(UnitKind::VhdlPackage, "composite_types");
  require(package != nullptr && package->type_aliases.size() == 4,
          "nested composite fixture retains all declarations");
  const auto &payload = package->type_aliases[1];
  require(payload.declaration_kind == TypeDeclarationKind::VhdlRecord &&
              payload.type.packed_members.size() == 2 &&
              payload.type.packed_members[1].name == "mode" &&
              payload.type.packed_members[1].nested_types.size() == 1 &&
              payload.type.packed_members[1].nested_types.front().named_type ==
                  "mode_t" &&
              payload.type.packed_members[1].span.begin.line == 6,
          "record enumeration member retains its named type and exact span");
  const auto &payload_array = package->type_aliases[2].type;
  require(payload_array.vhdl_array &&
              payload_array.vhdl_array->element_types.size() == 1 &&
              payload_array.vhdl_array->element_types.front().named_type ==
                  "payload_t",
          "array-of-record element type remains recursive HIR");
  const auto &envelope = package->type_aliases[3].type;
  require(envelope.packed_members.size() == 3 &&
              envelope.packed_members[0].nested_types.front().named_type ==
                  "payload_t" &&
              envelope.packed_members[1].nested_types.front().named_type ==
                  "payload_array_t" &&
              envelope.packed_members[2].nested_types.front().named_type ==
                  "mode_t",
          "nested record, array, and enumeration members retain source order");

  const auto *architecture =
      retained.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(architecture != nullptr && architecture->processes.size() == 1 &&
              architecture->processes.front().variables.size() == 1,
          "nested composite expression fixture retains its process region");
  const auto &initializer =
      *architecture->processes.front().variables.front().initializer;
  require(initializer.kind == ExpressionKind::Call &&
              initializer.text == "@vhdl-qualified:envelope_t" &&
              initializer.operands.size() == 1 &&
              initializer.operands.front().kind == ExpressionKind::Aggregate &&
              initializer.span.begin.line == 28,
          "qualified nested aggregate retains its type mark and source span");
  const auto &outer_aggregate = initializer.operands.front();
  require(outer_aggregate.aggregate_choices ==
                  std::vector<std::string>{"payload", "items", "tag"} &&
              outer_aggregate.operands[1].kind == ExpressionKind::Call &&
              outer_aggregate.operands[1].text ==
                  "@vhdl-qualified:payload_array_t",
          "record choices and nested array qualification retain source order");
  const auto &array_aggregate = outer_aggregate.operands[1].operands.front();
  require(array_aggregate.kind == ExpressionKind::Aggregate &&
              array_aggregate.aggregate_choices ==
                  std::vector<std::string>{"@array", "others"} &&
              array_aggregate.aggregate_choice_expressions[0].size() == 1 &&
              array_aggregate.aggregate_choice_expressions[0][0].kind ==
                  ExpressionKind::IntegerLiteral,
          "array discrete and others choices remain source-spanned HIR");
  const auto &statements = architecture->processes.front().statements;
  require(
      statements.size() == 4 &&
          statements[1].value.kind == ExpressionKind::Binary &&
          statements[1].value.text == "=" &&
          statements[2].value.kind == ExpressionKind::Call &&
          statements[2].value.text == "'length" &&
          statements[2].value.operands.front().text == "payload_array_t",
      "composite operation and type attribute remain explicit expression HIR");
}

void test_vhdl_enumeration_declarations() {
  const auto result = parse_text("enumeration_declarations.vhd",
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
  require(result.ok() && result.design.units.size() == 3,
          "package, entity, and architecture enumeration declarations parse");
  const auto &package = result.design.units[0];
  require(package.type_aliases.size() == 2 &&
              package.type_aliases[0].declaration_kind ==
                  TypeDeclarationKind::VhdlEnumeration &&
              package.type_aliases[0].type.domain == ValueDomain::Bit2 &&
              package.type_aliases[0].type.width() == 2 &&
              !package.type_aliases[0].type.nominal_type.empty() &&
              package.type_aliases[0].type.enumeration_literals ==
                  std::vector<std::string>{"idle", "running", "'A'", "'0'"},
          "enumeration HIR retains nominal identity, literal spelling, and "
          "minimum ordinal width");
  require(package.type_aliases[0].enum_literals.size() == 4 &&
              package.type_aliases[0].enum_literals[2].name == "'A'" &&
              package.type_aliases[0].enum_literals[3].value.text == "3" &&
              package.type_aliases[1].declaration_kind ==
                  TypeDeclarationKind::VhdlSubtype &&
              package.type_aliases[1].type.named_type == "state_t" &&
              package.parameters[0].type.named_type == "state_t",
          "enumeration ordinals, aliases, and typed constants remain explicit");
  require(result.design.units[1].type_aliases[0].type.width() == 1 &&
              result.design.units[2].type_aliases[0].type.width() == 2 &&
              result.design.units[2].signals[0].type.named_type == "phase_t",
          "entity- and architecture-local enumeration types retain scope and "
          "object references");

  const auto invalid = parse_text("invalid_enumeration_declarations.vhd",
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
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const auto &diagnostic) { return diagnostic.code == code; });
  };
  require(!invalid.ok() && has_code("FSIM-VHDL-PARSE-137") &&
              has_code("FSIM-VHDL-PARSE-138") &&
              has_code("FSIM-VHDL-PARSE-139") &&
              has_code("FSIM-VHDL-SEM-040") && has_code("FSIM-VHDL-SEM-036"),
          "malformed and duplicate enumeration declarations have stable "
          "diagnostics");
}

void test_vhdl_enumeration_attributes() {
  const auto result = parse_text("enumeration_attributes.vhd",
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
  require(result.ok(), "VHDL enumeration scalar attributes must parse");
  const auto &statements =
      result.design.units.back().processes.front().statements;
  constexpr std::array<std::string_view, 12> attributes{
      "'left", "'right", "'low",  "'high", "'length", "'ascending",
      "'pos",  "'val",   "'succ", "'pred", "'leftof", "'rightof"};
  require(statements.size() == attributes.size() + 1,
          "enumeration attribute statement count");
  for (std::size_t index = 0; index < attributes.size(); ++index) {
    require(statements[index].value.kind == ExpressionKind::Call &&
                statements[index].value.text == attributes[index] &&
                statements[index].value.operands.front().text == "state_t" &&
                (index < 6 ? statements[index].value.operands.size() == 1
                           : statements[index].value.operands.size() == 2),
            "enumeration attribute HIR retains type prefix and arity");
  }

  const auto invalid = parse_text("invalid_enumeration_attributes.vhd",
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
  require(!invalid.ok() && std::ranges::any_of(invalid.diagnostics,
                                               [](const auto &diagnostic) {
                                                 return diagnostic.code ==
                                                        "FSIM-VHDL-PARSE-142";
                                               }),
          "missing enumeration attribute arguments have a stable diagnostic");
}

void test_vhdl_enumeration_subtype_ranges() {
  const auto result = parse_text("enumeration_subtype_ranges.vhd",
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
  require(result.ok() && result.design.units.size() == 3,
          "constrained enumeration subtype indications parse in package, "
          "interface, and architecture regions");
  const auto &package = result.design.units[0];
  require(package.type_aliases.size() == 4 &&
              package.type_aliases[0].type.enumeration_range &&
              package.type_aliases[0].type.enumeration_range->left == 0 &&
              package.type_aliases[0].type.enumeration_range->right == 3 &&
              !package.type_aliases[0].type.enumeration_range->descending,
          "base enumeration declarations retain their complete ascending "
          "ordinal range");
  for (std::size_t index = 1; index < 4; ++index) {
    const auto &subtype = package.type_aliases[index];
    require(subtype.declaration_kind == TypeDeclarationKind::VhdlSubtype &&
                subtype.type.discrete_range_expression &&
                subtype.type.discrete_range_expression->left.kind ==
                    ExpressionKind::Identifier &&
                subtype.type.discrete_range_expression->right.kind ==
                    ExpressionKind::Identifier,
            "named enumeration subtype ranges retain typed bound expressions");
  }
  require(
      package.type_aliases[1].type.discrete_range_expression->left.text ==
              "load" &&
          package.type_aliases[1].type.discrete_range_expression->right.text ==
              "done" &&
          !package.type_aliases[1].type.discrete_range_expression->descending &&
          package.type_aliases[2].type.discrete_range_expression->descending,
      "enumeration subtype range direction and literal spelling are "
      "preserved");
  require(result.design.units[1].ports[0].type.discrete_range_expression &&
              result.design.units[2]
                  .type_aliases[0]
                  .type.discrete_range_expression &&
              result.design.units[2].signals[0].type.named_type == "local_t",
          "direct object constraints and local constrained subtype references "
          "remain explicit");

  const auto invalid = parse_text("invalid_enumeration_subtype_ranges.vhd",
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
  require(!invalid.ok() && std::ranges::any_of(invalid.diagnostics,
                                               [](const auto &diagnostic) {
                                                 return diagnostic.code ==
                                                        "FSIM-VHDL-PARSE-009";
                                               }),
          "malformed enumeration subtype ranges have a stable parser "
          "diagnostic");
}

void test_vhdl_case_generate_enumeration_choices() {
  const auto parsed = parse_text("case_generate_enumeration.vhd",
                                 R"(
package choice_types is
  type state_t is (idle, ready, 'Z', done);
end package;
use work.choice_types.all;
entity case_generate_enumeration is
  generic (mode : state_t := 'Z');
end entity;
architecture rtl of case_generate_enumeration is
begin
  selection: case mode generate
    selected: when ready | 'Z' to done =>
    empty_range: when 'Z' downto done =>
    fallback: when others =>
  end generate selection;
end architecture;
)",
                                 Language::Vhdl2008);
  require(parsed.ok(), "enumeration case-generate choices must parse");
  const auto *unit = parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(unit != nullptr && unit->generate_regions.size() == 1 &&
              unit->generate_regions.front().alternatives.size() == 3,
          "enumeration case-generate alternative count");
  const auto &alternatives = unit->generate_regions.front().alternatives;
  require(alternatives[0].choices.size() == 2 &&
              alternatives[0].choices[0].left.text == "ready" &&
              alternatives[0].choices[1].left.text == "'Z'" &&
              alternatives[0].choices[1].right &&
              alternatives[0].choices[1].right->text == "done" &&
              !alternatives[0].choices[1].descending &&
              alternatives[1].choices.front().descending &&
              alternatives[2].is_default,
          "grouped identifier/character/range case-generate HIR");
}

void test_exponentiation_expression_nodes() {
  const auto systemverilog = parse_text("power.sv",
                                        R"(
module power;
  logic [15:0] result;
  always_comb result = 2 ** 3 ** 2;
endmodule
)",
                                        Language::SystemVerilog2017);
  require(systemverilog.ok(), "SystemVerilog exponentiation source must parse");
  const auto &systemverilog_value = systemverilog.design.units.front()
                                        .processes.front()
                                        .statements.front()
                                        .value;
  require(systemverilog_value.kind == ExpressionKind::Binary &&
              systemverilog_value.text == "**" &&
              systemverilog_value.operands[0].kind == ExpressionKind::Binary &&
              systemverilog_value.operands[0].text == "**",
          "SystemVerilog exponentiation must associate left-to-right");

  const auto verilog = parse_text("power.v",
                                  R"(
module verilog_power;
  reg [15:0] result;
  initial result = 16'd2 ** 16'd3 ** 16'd2;
endmodule
)",
                                  Language::Verilog2005);
  require(verilog.ok(), "Verilog-2005 exponentiation source must parse");
  const auto &verilog_value =
      verilog.design.units.front().processes.front().statements.front().value;
  require(verilog_value.kind == ExpressionKind::Binary &&
              verilog_value.text == "**" &&
              verilog_value.operands[0].kind == ExpressionKind::Binary &&
              verilog_value.operands[0].text == "**",
          "Verilog-2005 exponentiation must associate left-to-right");

  const auto vhdl = parse_text("power.vhd",
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
  const auto *architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture && architecture->concurrent_statements.size() == 2 &&
          architecture->concurrent_statements.front().value.text == "**" &&
          architecture->concurrent_statements[1].value.kind ==
              ExpressionKind::Unary &&
          architecture->concurrent_statements[1].value.operands.front().text ==
              "**",
      "VHDL exponentiation precedence relative to a leading sign");

  const auto invalid_vhdl_chain = parse_text("invalid_power_chain.vhd",
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
  require(!invalid_vhdl_chain.ok() &&
              std::ranges::any_of(invalid_vhdl_chain.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-VHDL-PARSE-114";
                                  }),
          "unparenthesized VHDL exponentiation chains must be rejected");

  const auto invalid_vhdl_signed_exponent =
      parse_text("invalid_signed_exponent.vhd",
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
  require(!invalid_vhdl_signed_exponent.ok() &&
              std::ranges::any_of(invalid_vhdl_signed_exponent.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-VHDL-PARSE-114";
                                  }),
          "an unparenthesized signed VHDL exponent must be rejected");
}

void test_systemverilog_procedural_updates() {
  const auto result = parse_text("procedural_updates.sv",
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
    value += #2 8'd1;
    value ^= @(posedge signed_value) 8'h01;
    value = value++;
    value = --signed_value;
    force value[3:0] = 4'ha;
    release value[3:0];
  end
endmodule
)",
                                 Language::SystemVerilog2017);
  require(result.ok(), "SystemVerilog procedural update statements must parse");
  const auto &statements =
      result.design.units.front().processes.front().statements;
  const std::array<std::string_view, 18> operations{
      "+",  "-",   "*",   "/", "%", "&", "|", "^", "<<",
      ">>", "<<<", ">>>", "+", "-", "+", "-", "+", "^"};
  require(statements.size() == operations.size() + 4,
          "all procedural update statements must be retained");
  for (std::size_t index = 0; index < operations.size(); ++index) {
    require(statements[index].kind == StatementKind::Assignment &&
                statements[index].assignment_kind == AssignmentKind::Blocking &&
                statements[index].value.kind == ExpressionKind::Binary &&
                statements[index].value.text == operations[index] &&
                statements[index].value.operands.size() == 2,
            "procedural updates must normalize to blocking binary assignments");
  }
  require(statements[12].value.operands[1].kind ==
                  ExpressionKind::IntegerLiteral &&
              statements[12].value.operands[1].text == "1" &&
              statements[15].value.operands[1].text == "1",
          "standalone increment/decrement must use a contextual unit step");
  require(statements[0].procedural_update_kind ==
                  ProceduralUpdateKind::Compound &&
              statements[0].procedural_update_operator == "+" &&
              statements[12].procedural_update_kind ==
                  ProceduralUpdateKind::Prefix &&
              statements[14].procedural_update_kind ==
                  ProceduralUpdateKind::Postfix &&
              statements[16].procedural_assignment_control ==
                  ProceduralAssignmentControl::Delay &&
              statements[16].delay && statements[16].delay->magnitude == 2 &&
              statements[17].procedural_assignment_control ==
                  ProceduralAssignmentControl::Event &&
              statements[17].sensitivities.size() == 1 &&
              statements[17].sensitivities.front().edge == EdgeKind::Positive,
          "procedural updates must retain syntax kind, operator, and controls");
  require(statements[18].value.kind == ExpressionKind::Update &&
              statements[18].value.text == "post++" &&
              statements[18].value.operands.size() == 1 &&
              statements[19].value.kind == ExpressionKind::Update &&
              statements[19].value.text == "pre--",
          "prefix/postfix expression updates must retain result-value order");
  require(
      statements[20].kind == StatementKind::Force &&
          statements[20].target.kind == ExpressionKind::Slice &&
          statements[20].value.kind == ExpressionKind::LogicLiteral &&
          statements[21].kind == StatementKind::Release &&
          statements[21].target.kind == ExpressionKind::Slice,
      "procedural force/release must retain selected targets and force value");

  const auto unsupported = parse_text("unsupported_update.sv",
                                      R"(
module unsupported_update;
  logic [7:0] value;
  initial value **= 8'd2;
endmodule
)",
                                      Language::SystemVerilog2017);
  require(!unsupported.ok() &&
              std::ranges::any_of(unsupported.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-SV-UNSUPPORTED-008";
                                  }),
          "an unsupported procedural update operator must be diagnosed");

  const auto verilog = parse_text("verilog_update.v",
                                  R"(
module verilog_update;
  reg [7:0] value;
  initial value += 8'd1;
endmodule
)",
                                  Language::Verilog2005);
  require(!verilog.ok() && std::ranges::any_of(verilog.diagnostics,
                                               [](const auto &diagnostic) {
                                                 return diagnostic.code ==
                                                        "FSIM-VERILOG-SEM-005";
                                               }),
          "procedural compound assignments must remain SystemVerilog-only");

  const auto verilog_expressions = parse_text("verilog_expression_updates.v",
                                              R"(
module verilog_expression_updates;
  reg [7:0] value;
  initial begin
    value = value++;
    force value = 8'h12;
    release value;
  end
endmodule
)",
                                              Language::Verilog2005);
  require(!verilog_expressions.ok() &&
              std::ranges::any_of(verilog_expressions.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-VERILOG-SEM-010";
                                  }) &&
              std::ranges::any_of(verilog_expressions.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-VERILOG-SEM-011";
                                  }),
          "expression updates and procedural force/release remain "
          "SystemVerilog-only");

  const auto malformed_force = parse_text(
      "malformed_force.sv",
      "module malformed_force; logic value; initial force value 1'b1; "
      "endmodule",
      Language::SystemVerilog2017);
  require(!malformed_force.ok() &&
              std::ranges::any_of(malformed_force.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-SV-PARSE-195";
                                  }),
          "procedural force must require an equals sign");

  const auto malformed_release =
      parse_text("malformed_release.sv",
                 "module malformed_release; logic value; initial release value "
                 "endmodule",
                 Language::SystemVerilog2017);
  require(!malformed_release.ok() &&
              std::ranges::any_of(malformed_release.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-SV-PARSE-196";
                                  }),
          "procedural force/release must require a semicolon");
}

void test_systemverilog_final_procedures() {
  const auto result = parse_text("final_procedure.sv",
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
  const auto &processes = result.design.units.front().processes;
  require(processes.size() == 2 && processes[0].kind == ProcessKind::Initial &&
              processes[1].kind == ProcessKind::Final &&
              processes[1].statements.size() == 1 &&
              processes[1].statements.front().value.text == "+",
          "final procedure kind and normalized statement body");

  const auto suspending = parse_text("invalid_final.sv",
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
  require(!suspending.ok() &&
              std::ranges::any_of(suspending.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code == "FSIM-SV-SEM-032";
                                  }) &&
              std::ranges::any_of(suspending.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code == "FSIM-SV-SEM-033";
                                  }),
          "suspending and nonblocking final-procedure statements are rejected");

  const auto verilog = parse_text("verilog_final.v",
                                  R"(
module verilog_final;
  reg value;
  final value = 1'b1;
endmodule
)",
                                  Language::Verilog2005);
  require(!verilog.ok() && std::ranges::any_of(verilog.diagnostics,
                                               [](const auto &diagnostic) {
                                                 return diagnostic.code ==
                                                        "FSIM-VERILOG-SEM-006";
                                               }),
          "final procedures must remain SystemVerilog-only");
}

void test_verilog_stop_task() {
  for (const auto language :
       {Language::Verilog2005, Language::SystemVerilog2017}) {
    const auto result = parse_text(
        language == Language::Verilog2005 ? "stop_task.v" : "stop_task.sv",
        R"(
module stop_task;
  initial begin
    $stop;
    $stop(1);
  end
endmodule
)",
        language);
    require(result.ok(), "Verilog/SystemVerilog $stop tasks must parse");
    const auto &statements =
        result.design.units.front().processes.front().statements;
    require(statements.size() == 2 &&
                statements[0].kind == StatementKind::Pause &&
                statements[1].kind == StatementKind::Pause,
            "$stop and $stop(argument) must retain resumable pause HIR");
  }

  const auto missing_close =
      parse_text("invalid_stop_close.sv",
                 "module invalid_stop_close; initial $stop(1; endmodule",
                 Language::SystemVerilog2017);
  require(!missing_close.ok() &&
              std::ranges::any_of(missing_close.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-SV-PARSE-112";
                                  }),
          "a missing $stop closing parenthesis must be targeted");

  const auto missing_semicolon =
      parse_text("invalid_stop_semicolon.v",
                 "module invalid_stop_semicolon; initial $stop endmodule",
                 Language::Verilog2005);
  require(!missing_semicolon.ok() &&
              std::ranges::any_of(missing_semicolon.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-SV-PARSE-113";
                                  }),
          "a missing $stop semicolon must be targeted");
}

void test_vhdl_conditional_assignments() {
  const auto result = parse_text("conditional_assignment.vhd",
                                 R"(
entity conditional_assignment is
end entity;

architecture rtl of conditional_assignment is
  constant declaration_value : std_logic_vector(3 downto 0) :=
      "0001" when true else "0010";
  constant case_declaration : std_logic_vector(3 downto 0) :=
      (case true is
         when true => "0011",
         when false => "1100");
  signal select_a : boolean;
  signal select_b : boolean;
  signal a : std_logic_vector(3 downto 0);
  signal b : std_logic_vector(3 downto 0);
  signal c : std_logic_vector(3 downto 0);
  signal concurrent_result : std_logic_vector(3 downto 0);
  signal sequential_result : std_logic_vector(3 downto 0);
  signal expression_result : std_logic_vector(3 downto 0);
  signal case_expression_result : std_logic_vector(3 downto 0);
  signal external_result : std_logic_vector(3 downto 0);
begin
  concurrent_result <=
      a when select_a else
      b when select_b else
      c;
  expression_result <=
      (a when select_a else declaration_value);
  case_expression_result <=
      (case select_a is
         when true => a,
         when others => b);
  external_result <=
      << signal .conditional_assignment.a : std_logic_vector(3 downto 0) >>;

  choose: process(a, b, select_a)
  begin
    sequential_result <= a when select_a else b;
  end process;
end architecture;
)",
                                 Language::Vhdl2008);
  require(result.ok(), "VHDL conditional assignments must parse");
  const auto *architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(architecture && architecture->parameters.size() == 2 &&
              architecture->concurrent_statements.size() == 4 &&
              architecture->processes.size() == 1,
          "VHDL conditional assignments must retain their statement contexts");
  const auto &concurrent = architecture->concurrent_statements.front();
  const auto &sequential = architecture->processes.front().statements.front();
  const auto &declaration = architecture->parameters.front().default_value;
  const auto &expression = architecture->concurrent_statements[1].value;
  const auto &case_declaration =
      architecture->parameters[1].default_value;
  const auto &case_expression =
      architecture->concurrent_statements[2].value;
  const auto &external_expression =
      architecture->concurrent_statements[3].value;
  require(declaration.kind == ExpressionKind::Call &&
              declaration.text == "?:" && declaration.operands.size() == 3 &&
              declaration.operands[0].text == "true" &&
              declaration.operands[1].text == "\"0001\"" &&
              declaration.operands[2].text == "\"0010\"" &&
              expression.kind == ExpressionKind::Call &&
              expression.text == "?:" && expression.operands.size() == 3 &&
              expression.operands[0].text == "select_a" &&
              expression.operands[1].text == "a" &&
              expression.operands[2].text == "declaration_value" &&
              case_declaration.kind == ExpressionKind::Call &&
              case_declaration.text == "?:" &&
              case_declaration.operands[0].kind == ExpressionKind::Binary &&
              case_declaration.operands[0].text == "=" &&
              case_declaration.operands[1].text == "\"0011\"" &&
              case_declaration.operands[2].text == "\"1100\"" &&
              case_expression.kind == ExpressionKind::Call &&
              case_expression.text == "?:" &&
              case_expression.operands[0].kind == ExpressionKind::Binary &&
              case_expression.operands[0].text == "=" &&
              case_expression.operands[1].text == "a" &&
              case_expression.operands[2].text == "b" &&
              external_expression.kind == ExpressionKind::Call &&
              external_expression.text == "@vhdl-external" &&
              external_expression.operands.size() == 2 &&
              external_expression.operands[0].text ==
                  "conditional_assignment.a" &&
              external_expression.operands[1].call_result_width == 4 &&
              external_expression.operands[1].call_result_domain ==
                  ValueDomain::Logic9,
          "VHDL conditional, case, and external-name expressions must retain "
          "executable HIR");
  require(concurrent.kind == StatementKind::If &&
              concurrent.statements.size() == 1 &&
              concurrent.statements[0].value.text == "a" &&
              concurrent.else_statements.size() == 1 &&
              concurrent.else_statements[0].kind == StatementKind::If &&
              concurrent.else_statements[0].statements[0].value.text == "b" &&
              concurrent.else_statements[0].else_statements[0].value.text ==
                  "c",
          "chained VHDL conditional assignments must nest in source order");
  require(sequential.kind == StatementKind::If &&
              sequential.statements.size() == 1 &&
              sequential.statements[0].value.text == "a" &&
              sequential.else_statements.size() == 1 &&
              sequential.else_statements[0].value.text == "b",
          "sequential VHDL-2008 conditional assignments must use common HIR");

  const auto missing_else = parse_text("conditional_missing_else.vhd",
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
  require(!missing_else.ok() &&
              std::ranges::any_of(missing_else.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-VHDL-PARSE-115";
                                  }),
          "a VHDL conditional assignment without else must be diagnosed");

  const auto incomplete_case = parse_text("case_expression_incomplete.vhd",
                                          R"(
entity case_expression_incomplete is
end entity;

architecture rtl of case_expression_incomplete is
  signal selector : integer;
  signal result : integer;
begin
  result <= (case selector is when 0 => 1);
end architecture;
)",
                                          Language::Vhdl2008);
  require(!incomplete_case.ok() &&
              std::ranges::any_of(incomplete_case.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-VHDL-PARSE-275";
                                  }),
          "a nonexhaustive bounded VHDL case expression must be diagnosed");

  const auto repeated_others = parse_text("case_expression_others.vhd",
                                          R"(
entity case_expression_others is
end entity;

architecture rtl of case_expression_others is
  signal selector : boolean;
  signal result : integer;
begin
  result <= (case selector is
               when others => 1,
               when others => 2);
end architecture;
)",
                                          Language::Vhdl2008);
  require(!repeated_others.ok() &&
              std::ranges::any_of(repeated_others.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-VHDL-PARSE-274";
                                  }),
          "duplicate or nonfinal others case-expression alternatives must "
          "be diagnosed");

  const auto external_variable = parse_text("external_variable.vhd",
                                            R"(
entity external_variable is
end entity;

architecture rtl of external_variable is
  signal result : integer;
begin
  result <= << variable hidden : integer >>;
end architecture;
)",
                                            Language::Vhdl2008);
  require(!external_variable.ok() &&
              std::ranges::any_of(external_variable.diagnostics,
                                  [](const auto &diagnostic) {
                                    return diagnostic.code ==
                                           "FSIM-VHDL-PARSE-276";
                                  }),
          "unsupported external-name object classes must be diagnosed");
}

} // namespace fsim::tests::frontend
