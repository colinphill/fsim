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
  require(!verilog_expressions.ok() && std::ranges::any_of(verilog_expressions.diagnostics, [](const auto& diagnostic) {
      return diagnostic.code == "FSIM-VERILOG-SEM-010";
  }) && std::ranges::none_of(verilog_expressions.diagnostics, [](const auto& diagnostic) {
      return diagnostic.code == "FSIM-VERILOG-SEM-011";
  }),
      "expression updates remain SystemVerilog-only while Verilog-2005 "
      "force/release is accepted");

  const auto classic = parse_text("classic_procedural_drivers.v",
      R"(
module classic_procedural_drivers;
  reg [7:0] source;
  reg [7:0] value;
  initial begin
    {value[7:4], value[3:0]} = source;
    assign value = source;
    deassign value;
    force value[3:0] = source[3:0];
    release value[3:0];
  end
endmodule
)",
      Language::Verilog2005);
  require(classic.ok(),
      "Verilog-2005 procedural assign/deassign and force/release must "
      "parse");
  const auto& classic_statements = classic.design.units.front().processes.front().statements;
  require(classic_statements.size() == 5 && classic_statements[0].kind == StatementKind::Assignment && classic_statements[0].target.kind == ExpressionKind::Concatenation && classic_statements[1].kind == StatementKind::ProceduralAssign && classic_statements[2].kind == StatementKind::Deassign && classic_statements[3].kind == StatementKind::Force && classic_statements[4].kind == StatementKind::Release,
      "classic procedural driver statements retain distinct HIR kinds");

  const auto malformed_assign = parse_text(
      "malformed_procedural_assign.v",
      "module malformed_procedural_assign; reg value; initial assign value; "
      "endmodule",
      Language::Verilog2005);
  require(!malformed_assign.ok()
          && std::ranges::any_of(
              malformed_assign.diagnostics,
              [](const auto& diagnostic) {
                  return diagnostic.code == "FSIM-SV-PARSE-342";
              }),
      "procedural assign must require an equals sign");

  const auto malformed_deassign = parse_text(
      "malformed_deassign.v",
      "module malformed_deassign; reg value; initial deassign value "
      "endmodule",
      Language::Verilog2005);
  require(!malformed_deassign.ok()
          && std::ranges::any_of(
              malformed_deassign.diagnostics,
              [](const auto& diagnostic) {
                  return diagnostic.code == "FSIM-SV-PARSE-344";
              }),
      "procedural deassign must require a semicolon");

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

  const auto program_exit = parse_text(
      "program_exit.sv",
      "program program_exit; initial begin $exit; $exit(); end endprogram",
      Language::SystemVerilog2017);
  require(program_exit.ok(), "$exit and $exit() must parse in a program");
  const auto& exit_statements =
      program_exit.design.units.front().processes.front().statements;
  require(
      exit_statements.size() == 2
          && exit_statements[0].kind == StatementKind::Exit
          && exit_statements[1].kind == StatementKind::Exit,
      "$exit spellings must retain program-exit HIR");

  const auto exit_argument = parse_text(
      "invalid_exit_argument.sv",
      "program invalid_exit_argument; initial $exit(1); endprogram",
      Language::SystemVerilog2017);
  require(
      !exit_argument.ok()
          && std::ranges::any_of(
              exit_argument.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-075";
              }),
      "$exit arguments must be rejected by the system-task arity diagnostic");
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
                                 Language::Vhdl2008,
                                 VhdlStandard::Vhdl2019);
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
  require(declaration.kind == ExpressionKind::Conditional &&
              declaration.text == "when" &&
              declaration.operands.size() == 3 &&
              declaration.operands[0].text == "true" &&
              declaration.operands[1].text == "\"0001\"" &&
              declaration.operands[2].text == "\"0010\"" &&
              expression.kind == ExpressionKind::Conditional &&
              expression.text == "when" &&
              expression.operands.size() == 3 &&
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

  const auto legacy_expression = parse_text(
      "vhdl2008-conditional-expression.vhd",
      R"(
entity vhdl2008_conditional_expression is
end entity;
architecture rtl of vhdl2008_conditional_expression is
  constant rejected : integer := 1 when true else 2;
begin
end architecture;
)",
      Language::Vhdl2008, VhdlStandard::Vhdl2008);
  require(!legacy_expression.ok() &&
              std::ranges::any_of(
                  legacy_expression.diagnostics,
                  [](const Diagnostic &diagnostic) {
                    return diagnostic.code == "FSIM-FE-VHSTD-003";
                  }),
          "first-class conditional expressions stay isolated from VHDL-2008");
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
