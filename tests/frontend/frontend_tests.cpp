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

namespace {

using namespace fsim::frontend;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

std::filesystem::path make_test_directory(std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-" + std::string{name} + "-"
         + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

void write_text(
    const std::filesystem::path& path,
    const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(
      output.good(),
      "frontend test fixture must be writable");
}

void test_systemverilog_preprocessor() {
  const auto directory =
      make_test_directory("verilog-preprocessor");
  const auto include_directory = directory / "include";
  const auto root = directory / "root.sv";
  write_text(
      include_directory / "nested" / "values.svh",
      R"(`define FROM_NESTED 4'b1010
)");
  write_text(
      include_directory / "definitions.svh",
      R"(`include <nested/values.svh>
`define JOIN(left,right) left``right
`define RANGE [3:0]
`define MESSAGE(value) `"value`"
`define DEFAULTED(value=4'b0011) value
`define ASSIGN_VALUES \
    value = `FROM_NESTED; \
    command_value = `FROM_COMMAND;
)");
  write_text(
      root,
      R"(`define HEADER "definitions.svh"
`include `HEADER
`define SELECT_EXPECTED
`undef SELECT_EXPECTED
`ifndef SELECT_EXPECTED
`define SELECT_EXPECTED
`endif
`ifdef MISSING
module inactive_bad(;
`elsif SELECT_EXPECTED
`timescale 1ns/1ps
module `JOIN(pre,processed)(
  output logic `RANGE value,
  output logic [3:0] command_value
);
  logic [7:0] source_line;
  logic [3:0] default_value;
  initial begin
    `ASSIGN_VALUES
    default_value = `DEFAULTED();
    source_line = `__LINE__;
    assert (1'b1) else $error(`MESSAGE(preprocessed));
    assert (1'b1) else $error(`__FILE__);
  end
endmodule
`else
module also_inactive_bad(;
`endif
)");

  PreprocessorOptions options;
  options.include_directories = {include_directory};
  options.defines = {"FROM_COMMAND=4'b0101"};
  auto preprocessed = preprocess_verilog_file(
      root, Language::SystemVerilog2017, options);
  require(
      preprocessed.ok(),
      "includes, command definitions, conditionals, function macros, and "
      "token concatenation must preprocess");
  require(
      preprocessed.dependencies.size() == 3,
      "root and two transitive include snapshots");
  require(
      preprocessed.dependencies[1].path.filename()
          == "definitions.svh"
          && preprocessed.dependencies[2].path.filename()
              == "values.svh",
      "dependencies retain deterministic first-use order");

  const auto parsed =
      parse_verilog(std::move(preprocessed.lexed), true);
  require(parsed.ok(), "preprocessed SystemVerilog must parse");
  require(
      parsed.design.units.size() == 1
          && parsed.design.units.front().name == "preprocessed",
      "conditional selection and token concatenation");
  const auto& unit = parsed.design.units.front();
  require(
      unit.time_unit == "1ns" && unit.time_precision == "1ps",
      "timescale directive survives preprocessing");
  require(
      unit.ports.size() == 2
          && unit.ports.front().type.width()
              == std::optional<std::uint64_t>{4},
      "object macro expands into a packed range");
  require(
      unit.processes.size() == 1
          && unit.processes.front().statements.size() == 6
          && unit.processes.front().statements[0].value.text
              == "4'b1010"
          && unit.processes.front().statements[1].value.text
              == "4'b0101",
      "transitive and command-line macro values reach the AST");
  require(
      unit.processes.front().statements[2].value.text
          == "4'b0011",
      "omitted function-macro argument uses its default");
  require(
      unit.processes.front().statements[3].value.kind
          == ExpressionKind::IntegerLiteral,
      "`__LINE__ expands to an integer literal");
  require(
      unit.processes.front().statements[4].assertion_message
          == "preprocessed",
      "SystemVerilog macro stringification");
  require(
      std::filesystem::path{
          unit.processes.front().statements[5].assertion_message}
              .filename()
          == "root.sv",
      "`__FILE__ expands to the normalized source name");

  const auto compilation_first = directory / "shared-first.sv";
  const auto compilation_second = directory / "shared-second.sv";
  write_text(
      compilation_first,
      R"(`timescale 10ns/1ns
`define SHARED_VALUE 4'b1100
`ifdef ENABLE_SHARED_UNIT
)");
  write_text(
      compilation_second,
      R"(module shared_compilation_unit;
  logic [3:0] value;
  initial begin
    value = `SHARED_VALUE;
    #1 $finish;
  end
endmodule
`endif
)");
  PreprocessorOptions shared_options;
  shared_options.defines = {"ENABLE_SHARED_UNIT=1"};
  auto shared = preprocess_verilog_compilation_unit(
      {compilation_first, compilation_second},
      Language::SystemVerilog2017,
      shared_options);
  require(
      shared.ok() && shared.roots.size() == 2
          && shared.inputs.size() == 2,
      "ordered roots form one exact preprocessing compilation unit");
  const auto shared_parsed =
      parse_verilog(std::move(shared.lexed), true);
  require(
      shared_parsed.ok()
          && shared_parsed.design.units.size() == 1
          && shared_parsed.design.units.front().name
              == "shared_compilation_unit"
          && shared_parsed.design.units.front().time_unit
              == "10ns"
          && shared_parsed.design.units.front()
                  .processes.front()
                  .statements.front()
                  .value.text
              == "4'b1100",
      "macro, conditional, and timescale state persist across roots");

  const auto bad_root = directory / "bad.sv";
  write_text(
      bad_root,
      R"(`define BAD_ASSIGN target = )
module bad;
  logic target;
  initial begin
    `BAD_ASSIGN;
  end
endmodule
)");
  auto bad_preprocessed = preprocess_verilog_file(
      bad_root, Language::SystemVerilog2017);
  const auto bad = parse_verilog(
      std::move(bad_preprocessed.lexed), true);
  require(!bad.ok(), "invalid macro expansion must be rejected");
  require(
      std::any_of(
          bad.diagnostics.begin(),
          bad.diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return !diagnostic.expansion_stack.empty()
                && diagnostic.expansion_stack.front().find(
                       "macro `BAD_ASSIGN'")
                    != std::string::npos;
          }),
      "parser diagnostics retain macro definition/invocation ancestry");

  const auto bad_include = include_directory / "bad.svh";
  write_text(bad_include, "module included_bad(;\n");
  const auto include_error_root = directory / "include-error.sv";
  write_text(
      include_error_root,
      "`include \"bad.svh\"\n");
  auto include_error_preprocessed = preprocess_verilog_file(
      include_error_root, Language::SystemVerilog2017, options);
  const auto include_error = parse_verilog(
      std::move(include_error_preprocessed.lexed), true);
  require(
      !include_error.ok()
          && std::any_of(
              include_error.diagnostics.begin(),
              include_error.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return !diagnostic.expansion_stack.empty()
                    && diagnostic.expansion_stack.front().find(
                           "included '")
                        != std::string::npos;
              }),
      "diagnostics retain nested include ancestry");

  const auto missing_root = directory / "missing.sv";
  write_text(missing_root, "`include \"absent.svh\"\n");
  const auto missing = preprocess_verilog_file(
      missing_root, Language::SystemVerilog2017, options);
  require(
      !missing.ok()
          && std::any_of(
              missing.lexed.diagnostics.begin(),
              missing.lexed.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PP-022";
              }),
      "missing include receives a targeted diagnostic");

  const auto directive_error_root =
      directory / "directive-errors.sv";
  write_text(
      directive_error_root,
      R"(`define TWO(first,second) first
`TWO(one)
`resetall
`pragma protect
`ifdef LEFT_OPEN
)");
  const auto directive_errors = preprocess_verilog_file(
      directive_error_root, Language::SystemVerilog2017);
  const auto has_preprocessor_code =
      [&](const std::string_view code) {
        return std::any_of(
            directive_errors.lexed.diagnostics.begin(),
            directive_errors.lexed.diagnostics.end(),
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      !directive_errors.ok()
          && has_preprocessor_code("FSIM-SV-PP-030")
          && has_preprocessor_code("FSIM-SV-PP-011")
          && has_preprocessor_code("FSIM-SV-PP-002"),
      "macro arity, unsupported pragma, and open conditional diagnostics");

  const auto cycle_a = directory / "cycle-a.svh";
  const auto cycle_b = directory / "cycle-b.svh";
  write_text(cycle_a, "`include \"cycle-b.svh\"\n");
  write_text(cycle_b, "`include \"cycle-a.svh\"\n");
  const auto cycle = preprocess_verilog_file(
      cycle_a, Language::SystemVerilog2017);
  require(
      !cycle.ok()
          && std::any_of(
              cycle.lexed.diagnostics.begin(),
              cycle.lexed.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PP-005";
              }),
      "recursive include receives a targeted diagnostic");
  const auto empty_compilation_unit =
      preprocess_verilog_compilation_unit(
          {}, Language::SystemVerilog2017);
  require(
      !empty_compilation_unit.ok()
          && std::any_of(
              empty_compilation_unit.lexed.diagnostics.begin(),
              empty_compilation_unit.lexed.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PP-031";
              }),
      "an empty preprocessing compilation unit is rejected");

  std::error_code cleanup_error;
  std::filesystem::remove_all(directory, cleanup_error);
}

void test_vhdl_vertical_slice() {
  constexpr std::string_view source = R"(
library ieee;
use ieee.std_logic_1164.all;
context work.shared;

entity Counter is
  port (
    clk : in std_logic;
    q   : out std_logic_vector(7 downto 0)
  );
end entity Counter;

architecture RTL of Counter is
  signal count : std_logic_vector(7 downto 0);
begin
  q <= count;
  Direct_Child: entity Work.Child(Gates)
    port map (Clock => clk, q);
  Component_Child: Child
    port map (clk, Result => count);

  update: process(clk)
  begin
    if rising_edge(clk) then
      count <= count + 1;
    else
      null;
    end if;
  end process update;
end architecture RTL;
)";

  const auto result =
      parse_text("counter.vhd", source, Language::Vhdl2008);
  require(result.ok(), "VHDL vertical slice must parse without errors");
  require(result.design.units.size() == 2,
          "VHDL entity and architecture must remain distinct units");

  const auto* entity =
      result.design.find(UnitKind::VhdlEntity, "counter");
  require(entity != nullptr, "VHDL names must be canonicalized");
  require(entity->vhdl_context.size() == 3,
          "VHDL context items must attach to the following unit");
  require(
      entity->vhdl_context[0].kind ==
              VhdlContextItemKind::LibraryClause &&
          entity->vhdl_context[0].selected_names ==
              std::vector<std::string>{"ieee"},
      "library clause representation");
  require(
      entity->vhdl_context[1].kind == VhdlContextItemKind::UseClause &&
          entity->vhdl_context[1].selected_names ==
              std::vector<std::string>{"ieee.std_logic_1164.all"},
      "use clause representation");
  require(
      entity->vhdl_context[2].kind ==
              VhdlContextItemKind::ContextReference &&
          entity->vhdl_context[2].selected_names ==
              std::vector<std::string>{"work.shared"},
      "context reference representation");
  require(entity->ports.size() == 2, "entity port count");
  require(entity->ports[1].type.width() == 8, "VHDL vector width");
  require(entity->ports[1].type.domain == ValueDomain::Logic9,
          "std_logic_vector must retain nine-state domain");

  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(architecture != nullptr, "architecture lookup");
  require(architecture->vhdl_context.empty(),
          "a context clause applies only to its following library unit");
  require(architecture->primary_name == "counter",
          "architecture must identify its entity");
  require(architecture->signals.size() == 1, "architecture signal count");
  require(architecture->concurrent_statements.size() == 1,
          "concurrent assignment count");
  require(architecture->instances.size() == 2, "VHDL instance count");
  const auto& direct_instance = architecture->instances[0];
  require(direct_instance.name == "direct_child",
          "VHDL instance names must be canonicalized");
  require(direct_instance.unit_name == "work.child(gates)",
          "direct entity library, unit, and architecture");
  require(direct_instance.connections.size() == 2,
          "direct entity connection count");
  require(direct_instance.connections[0].port ==
              std::optional<std::string>{"clock"},
          "VHDL named formal must be canonicalized");
  require(direct_instance.connections[0].value.kind ==
              ExpressionKind::Identifier &&
              direct_instance.connections[0].value.text == "clk",
          "VHDL named actual identifier");
  require(!direct_instance.connections[1].port &&
              direct_instance.connections[1].value.text == "q",
          "VHDL positional connection");
  require(direct_instance.span.source_name == "counter.vhd" &&
              direct_instance.span.begin.line > 1 &&
              direct_instance.connections[0].span.begin.line > 1,
          "VHDL instance and connection source spans");
  const auto& component_instance = architecture->instances[1];
  require(component_instance.name == "component_child" &&
              component_instance.unit_name == "child",
          "component-style VHDL instance");
  require(component_instance.connections.size() == 2 &&
              component_instance.connections[1].port ==
                  std::optional<std::string>{"result"} &&
              component_instance.connections[1].value.text == "count",
          "component-style named and positional connections");
  require(architecture->processes.size() == 1, "process count");
  const auto& process = architecture->processes.front();
  require(process.name == "update", "process label");
  require(process.sensitivities.size() == 1, "VHDL sensitivity count");
  require(process.sensitivities.front().edge == EdgeKind::Any,
          "an edge guard with an else branch must retain any-edge sensitivity");
  require(process.statements.size() == 1 &&
              process.statements.front().kind == StatementKind::If,
          "VHDL if statement");
  require(process.statements.front().statements.front().assignment_kind ==
              AssignmentKind::VhdlSignal,
          "VHDL sequential signal assignment");
}

void test_vhdl_instance_diagnostics() {
  constexpr std::string_view source = R"(
entity top is
  port (clk : in std_logic);
end entity;

architecture rtl of top is
  signal data : std_logic_vector(1 downto 0);
begin
  with_generic: entity work.child(rtl)
    generic map (Width => 2)
    port map (clk);
  indexed_actual: child
    port map (input => data(0), output => open);
end architecture;
)";

  const auto result =
      parse_text("instances.vhd", source, Language::Vhdl2008);
  require(!result.ok(), "unsupported VHDL instance forms must fail");
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(architecture != nullptr && architecture->instances.size() == 2,
          "unsupported associations must recover to following instances");

  std::size_t complex_actuals = 0;
  for (const auto& diagnostic : result.diagnostics) {
    if (diagnostic.code == "FSIM-VHDL-UNSUPPORTED-010") {
      ++complex_actuals;
      require(diagnostic.span.source_name == "instances.vhd",
              "unsupported actual diagnostic source span");
    }
  }
  require(complex_actuals == 2,
          "indexed and open actuals need targeted diagnostics");
  require(
      architecture->instances[0].parameter_overrides.size() == 1
          && architecture->instances[0].parameter_overrides[0].name
              == std::optional<std::string>{"width"}
          && architecture->instances[0]
                 .parameter_overrides[0].value.text
              == "2",
      "named generic maps must remain in the instance HIR");
  require(architecture->instances[1].connections.size() == 2 &&
              architecture->instances[1].connections[0].value.kind ==
                  ExpressionKind::Invalid &&
              architecture->instances[1].connections[1].value.kind ==
                  ExpressionKind::Invalid,
          "unsupported actuals must not appear as valid expressions");
}

void test_vhdl_generics() {
  const auto result = parse_text(
      "generics.vhd",
      R"(
entity generic_child is
  generic (
    Width, Depth : positive := 8;
    Enabled : boolean := true;
    Required : integer
  );
  port (
    data : out bit_vector(Width - 1 downto 0)
  );
end entity;

architecture rtl of generic_child is
  signal local_data : bit_vector(Width - 1 downto 0);
begin
  data <= local_data;
end architecture;

entity generic_top is
end entity;

architecture rtl of generic_top is
  signal named_data : bit_vector(3 downto 0);
  signal mixed_data : bit_vector(1 downto 0);
begin
  named_child: entity work.generic_child(rtl)
    generic map (
      Width => 4,
      Required => 2
    )
    port map (data => named_data);
  mixed_child: entity work.generic_child(rtl)
    generic map (
      2,
      Enabled => false,
      Required => 1
    )
    port map (data => mixed_data);
end architecture;
)",
      Language::Vhdl2008);
  require(
      result.ok(),
      "bounded VHDL generic declarations and maps must parse");
  const auto* child =
      result.design.find(UnitKind::VhdlEntity, "generic_child");
  require(
      child != nullptr && child->parameters.size() == 4,
      "VHDL generics are retained in declaration order");
  require(
      child->parameters[0].name == "width"
          && child->parameters[0].type.spelling == "positive"
          && child->parameters[0].default_value.text == "8"
          && child->parameters[1].name == "depth"
          && child->parameters[2].type.domain
              == ValueDomain::Boolean
          && child->parameters[2].default_value.kind
              == ExpressionKind::BooleanLiteral
          && child->parameters[3].default_value.kind
              == ExpressionKind::Invalid,
      "typed defaults and required generics");
  require(
      child->ports.size() == 1
          && !child->ports[0].type.packed_range
          && child->ports[0].type.packed_range_expression
          && child->ports[0]
                 .type.packed_range_expression->descending
              == std::optional<bool>{true},
      "VHDL symbolic ranges retain explicit direction");
  const auto top_architecture = std::find_if(
      result.design.units.begin(),
      result.design.units.end(),
      [](const DesignUnit& unit) {
        return unit.kind == UnitKind::VhdlArchitecture
            && unit.primary_name == "generic_top";
      });
  require(
      top_architecture != result.design.units.end()
          && top_architecture->instances.size() == 2
          && top_architecture->instances[0]
                 .parameter_overrides.size()
              == 2
          && top_architecture->instances[0]
                 .parameter_overrides[0].name
              == std::optional<std::string>{"width"}
          && !top_architecture->instances[1]
                  .parameter_overrides[0].name
          && top_architecture->instances[1]
                 .parameter_overrides[1].name
              == std::optional<std::string>{"enabled"},
      "named and positional-then-named generic maps are represented");

  const auto invalid = parse_text(
      "invalid-generics.vhd",
      R"(
entity invalid_generic is
  generic (
    Clash : integer := 1;
    Clash : integer := 2;
    Vector_Value : bit_vector(1 downto 0) := "00"
  );
  port (Clash : in bit);
end entity;
architecture rtl of invalid_generic is
begin
end architecture;
entity invalid_top is
end entity;
architecture rtl of invalid_top is
begin
  bad: entity work.invalid_generic(rtl)
    generic map (
      Clash => 1,
      Clash => 2,
      3
    )
    port map ();
end architecture;
)",
      Language::Vhdl2008);
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        invalid.diagnostics.begin(),
        invalid.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok()
          && has_code("FSIM-VHDL-SEM-013")
          && has_code("FSIM-VHDL-SEM-014")
          && has_code("FSIM-VHDL-SEM-015")
          && has_code("FSIM-VHDL-SEM-016")
          && has_code("FSIM-VHDL-UNSUPPORTED-018"),
      "VHDL generic diagnostics are stable and targeted");
}

void test_vhdl_select_and_concatenation_expressions() {
  const auto result = parse_text(
      "select_concat.vhd",
      R"(
entity select_concat is
  port (
    descending : in std_logic_vector(7 downto 4);
    ascending : in std_logic_vector(2 to 5);
    selected : out std_logic;
    part : out std_logic_vector(1 downto 0);
    joined : out std_logic_vector(5 downto 0)
  );
end entity;

architecture rtl of select_concat is
begin
  observe: process(descending, ascending)
  begin
    selected <= descending(5);
    part <= ascending(3 to 4);
    joined <= descending(7 downto 6) & "10" & ascending(4 to 5);
    descending(4) <= selected;
    ascending(4 to 5) <= part;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      result.ok(),
      "VHDL indexed, slice, and concatenation expressions must parse");
  const auto* entity =
      result.design.find(UnitKind::VhdlEntity, "select_concat");
  require(
      entity != nullptr
          && entity->ports[0].type.packed_range
          && entity->ports[0].type.packed_range->descending
          && entity->ports[1].type.packed_range
          && !entity->ports[1].type.packed_range->descending,
      "VHDL port range directions");
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
          architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 5,
      "VHDL select/concatenation process");
  const auto& statements =
      architecture->processes.front().statements;
  require(
      statements[0].value.kind == ExpressionKind::Call
          && statements[0].value.text == "descending"
          && statements[0].value.operands.size() == 1,
      "ambiguous VHDL indexed-name syntax retained for semantic resolution");
  require(
      statements[1].value.kind == ExpressionKind::Slice
          && statements[1].value.text == "to"
          && statements[1].value.operands.size() == 3,
      "VHDL ascending slice expression");
  require(
      statements[2].value.kind == ExpressionKind::Binary
          && statements[2].value.text == "&"
          && statements[2].value.operands[0].kind
              == ExpressionKind::Binary,
      "left-associated VHDL concatenation expression");
  require(
      statements[3].target.kind == ExpressionKind::Index
          && statements[3].target.operands.size() == 2
          && statements[3].target.operands[1].text == "4"
          && statements[4].target.kind == ExpressionKind::Slice
          && statements[4].target.text == "to",
      "VHDL selected assignment target nodes");
}

void test_signed_type_and_expression_nodes() {
  const auto vhdl = parse_text(
      "signed_ops.vhd",
      R"(
entity signed_ops is
  port (
    lhs : in signed(7 downto 0);
    rhs : in signed(7 downto 0);
    quotient : out signed(7 downto 0);
    remainder : out signed(7 downto 0);
    modulo : out signed(7 downto 0)
  );
end entity;
architecture rtl of signed_ops is
begin
  quotient <= lhs / rhs;
  remainder <= lhs rem rhs;
  modulo <= lhs mod rhs;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "VHDL signed arithmetic source must parse");
  const auto* entity =
      vhdl.design.find(UnitKind::VhdlEntity, "signed_ops");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      entity != nullptr && entity->ports.size() == 5
          && std::ranges::all_of(
              entity->ports,
              [](const SignalDeclaration& port) {
                return port.type.is_signed;
              }),
      "VHDL signed subtype metadata");
  require(
      architecture != nullptr
          && architecture->concurrent_statements.size() == 3
          && architecture->concurrent_statements[0].value.text == "/"
          && architecture->concurrent_statements[1].value.text
              == "rem"
          && architecture->concurrent_statements[2].value.text
              == "mod",
      "VHDL signed division/remainder/modulo expression nodes");

  const auto systemverilog = parse_text(
      "signed_ops.sv",
      R"(
module signed_ops;
  logic signed [7:0] lhs;
  logic signed [7:0] rhs;
  logic unsigned [7:0] mixed;
  logic signed [7:0] quotient;
  logic comparison;
  always_comb begin
    quotient = lhs / rhs;
    comparison = lhs < rhs;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog signed arithmetic source must parse");
  const auto& unit = systemverilog.design.units.front();
  require(
      unit.signals.size() == 5
          && unit.signals[0].type.is_signed
          && unit.signals[1].type.is_signed
          && !unit.signals[2].type.is_signed,
      "SystemVerilog explicit signedness metadata");
  require(
      unit.processes.size() == 1
          && unit.processes.front().statements.size() == 2
          && unit.processes.front().statements[0].value.text == "/"
          && unit.processes.front().statements[1].value.text == "<",
      "SystemVerilog signed arithmetic expression nodes");
}

void test_systemverilog_vertical_slice() {
  constexpr std::string_view source = R"(
module counter(
  input logic clk,
  output logic [7:0] q
);
  logic [7:0] next;
  assign next = q + 1;
  child u_child(.value(q), .result(next));

  always_ff @(posedge clk) begin
    q <= next;
  end

  initial begin
    #10 $finish;
  end
endmodule
)";

  const auto result =
      parse_text("counter.sv", source, Language::SystemVerilog2017);
  require(result.ok(),
          "SystemVerilog vertical slice must parse without errors");
  require(result.design.units.size() == 1, "SV module count");
  const auto& module = result.design.units.front();
  require(module.kind == UnitKind::VerilogModule, "SV unit kind");
  require(module.name == "counter", "SV module name");
  require(module.ports.size() == 2, "SV ANSI port count");
  require(module.ports[1].type.width() == 8, "SV vector width");
  require(module.signals.size() == 1, "SV signal declaration");
  require(module.concurrent_statements.size() == 1,
          "SV continuous assignment");
  require(module.instances.size() == 1, "SV module instance");
  require(module.instances.front().unit_name == "child"
              && module.instances.front().name == "u_child",
          "SV instance unit and name");
  require(module.instances.front().connections.size() == 2
              && module.instances.front().connections.front().port
                  == std::optional<std::string>{"value"},
          "SV named port connection");
  require(module.processes.size() == 2, "SV process count");

  const auto& always = module.processes[0];
  require(always.kind == ProcessKind::SystemVerilogAlwaysFF,
          "always_ff process kind");
  require(always.sensitivities.size() == 1 &&
              always.sensitivities.front().edge == EdgeKind::Positive,
          "SV posedge sensitivity");
  require(always.statements.size() == 1 &&
              always.statements.front().assignment_kind ==
                  AssignmentKind::NonBlocking,
          "SV nonblocking assignment");

  const auto& initial = module.processes[1];
  require(initial.kind == ProcessKind::Initial, "initial process kind");
  require(initial.statements.size() == 1 &&
              initial.statements.front().kind == StatementKind::Delay,
          "initial delay statement");
  require(initial.statements.front().delay->magnitude == 10,
          "Verilog delay magnitude");
  require(initial.statements.front().statements.size() == 1 &&
              initial.statements.front().statements.front().kind ==
                  StatementKind::Finish,
          "delayed $finish");
}

void test_non_ansi_verilog_ports() {
  constexpr std::string_view source = R"(
module edge_reg(clk, d, q);
  input clk, d;
  output q;
  reg q;
  always @(posedge clk) q <= d;
endmodule
)";
  const auto result =
      parse_text("edge_reg.v", source, Language::Verilog2005);
  require(result.ok(), "Verilog-2005 non-ANSI module must parse");
  const auto& module = result.design.units.front();
  require(module.ports.size() == 3,
          "body port declarations must update header placeholders");
  require(module.signals.empty(),
          "a non-ANSI output reg must not become a duplicate signal");
  require(module.ports[2].type.spelling == "reg",
          "a non-ANSI reg redeclaration must refine the port type");
  require(module.processes.front().kind == ProcessKind::VerilogAlways,
          "Verilog always process kind");
}

void test_diagnostics_and_spans() {
  const auto result = parse_text(
      "broken.sv", "module broken(input logic a)\nassign = a;\nendmodule",
      Language::SystemVerilog2017);
  require(!result.ok(), "malformed source must fail");
  require(!result.diagnostics.empty(), "malformed source diagnostic");
  require(result.diagnostics.front().span.source_name == "broken.sv",
          "diagnostic source name");
  require(result.diagnostics.front().span.begin.line >= 1,
          "diagnostic line is one-based");
  require(result.diagnostics.front().code.starts_with("FSIM-"),
          "diagnostics have stable codes");
}

void test_vhdl_context_diagnostics() {
  const auto declaration = parse_text(
      "context_declaration.vhd",
      R"(
context shared is
  library ieee;
  use ieee.std_logic_1164.all;
  context work.base;
end context shared;
context work.shared;
entity context_user is
end entity;
)",
      Language::Vhdl2008);
  require(
      declaration.ok() && declaration.design.units.size() == 2,
      "bounded context declaration and following unit must parse");
  require(
      declaration.design.units[0].kind == UnitKind::VhdlContext
          && declaration.design.units[0].name == "shared"
          && declaration.design.units[0].vhdl_context.size() == 3
          && declaration.design.units[0]
                 .vhdl_context.back()
                 .kind
              == VhdlContextItemKind::ContextReference,
      "context declaration retains reusable context items");
  require(
      declaration.design.units[1].vhdl_context.size() == 1
          && declaration.design.units[1]
                 .vhdl_context.front()
                 .selected_names.front()
              == "work.shared",
      "context reference remains attached to the following unit");

  const auto malformed = parse_text(
      "malformed_context.vhd",
      "use ieee.; entity context_user is end entity;",
      Language::Vhdl2008);
  require(!malformed.ok(), "a malformed context clause must be rejected");
  require(
      std::any_of(
          malformed.diagnostics.begin(),
          malformed.diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-VHDL-PARSE-044";
          }),
      "malformed context clause needs a targeted diagnostic");

  const auto invalid_declaration = parse_text(
      "invalid_context_declaration.vhd",
      R"(
context invalid is
  signal not_a_context_item : bit;
end context mismatched;
entity recovered is
end entity recovered;
)",
      Language::Vhdl2008);
  require(
      !invalid_declaration.ok()
          && invalid_declaration.design.units.size() == 2
          && std::any_of(
              invalid_declaration.diagnostics.begin(),
              invalid_declaration.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-UNSUPPORTED-024";
              })
          && std::any_of(
              invalid_declaration.diagnostics.begin(),
              invalid_declaration.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-092";
              }),
      "invalid context declaration items and end names are targeted");
}

void test_vhdl_package_constants() {
  const auto parsed = parse_text(
      "package_constants.vhd",
      R"(
library support;
package constants is
  constant width, lanes : natural := 4;
  constant next_value : integer := width + 1;
  constant enabled : boolean := true;
  constant initial_bit : bit := '1';
end package constants;

use work.constants.all;
entity package_user is
end entity package_user;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "bounded VHDL package constants must parse");
  require(
      parsed.design.units.size() == 2,
      "package and following entity are retained");
  const auto& package = parsed.design.units[0];
  require(
      package.kind == UnitKind::VhdlPackage
          && package.name == "constants"
          && package.parameters.size() == 5,
      "package declaration records each constant");
  require(
      package.parameters[0].name == "width"
          && package.parameters[1].name == "lanes"
          && package.parameters[2].default_value.kind
              == ExpressionKind::Binary
          && package.parameters[3].type.domain
              == ValueDomain::Boolean
          && package.parameters[4].type.domain
              == ValueDomain::Bit2,
      "package constant names, expressions, and scalar types survive");
  require(
      package.vhdl_context.size() == 1
          && package.vhdl_context.front().kind
              == VhdlContextItemKind::LibraryClause,
      "package retains its own context");
  require(
      parsed.design.units[1].vhdl_context.size() == 1
          && parsed.design.units[1]
                 .vhdl_context.front()
                 .selected_names.front()
              == "work.constants.all",
      "following unit retains package use visibility");

  const auto selected_names = parse_text(
      "selected_package_names.vhd",
      R"(
entity selected_package_names is
  port (
    observed : out unsigned(
      work.constants.width - 1 downto 0)
  );
end entity selected_package_names;
architecture rtl of selected_package_names is
begin
  observed <= constants.next_value;
end architecture rtl;
)",
      Language::Vhdl2008);
  require(
      selected_names.ok()
          && selected_names.design.units.size() == 2
          && selected_names.design.units[0]
                 .ports.front()
                 .type.packed_range_expression
          && selected_names.design.units[0]
                 .ports.front()
                 .type.packed_range_expression
                 ->left.operands.front().text
              == "work.constants.width"
          && selected_names.design.units[1]
                 .concurrent_statements.front()
                 .value.text
              == "constants.next_value",
      "two- and three-part selected package names survive HIR parsing");

  const auto invalid = parse_text(
      "invalid_package_constants.vhd",
      R"(
package invalid_constants is
  constant duplicate : natural := 1;
  constant duplicate : natural := 2;
  constant missing : natural;
  constant vector_value : bit_vector(3 downto 0) := "0000";
end package invalid_constants;
)",
      Language::Vhdl2008);
  require(!invalid.ok(), "invalid package constants must fail");
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        invalid.diagnostics.begin(),
        invalid.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      has_code("FSIM-VHDL-SEM-020")
          && has_code("FSIM-VHDL-PARSE-088")
          && has_code("FSIM-VHDL-UNSUPPORTED-023"),
      "invalid package constants have targeted diagnostics");

  const auto body = parse_text(
      "package_body.vhd",
      R"(
package body unsupported is
  function identity(value : integer) return integer is
  begin
    return value;
  end function identity;
end package body unsupported;
entity recovered is
end entity recovered;
)",
      Language::Vhdl2008);
  require(
      !body.ok()
          && body.design.units.size() == 1
          && body.design.units.front().name == "recovered"
          && std::any_of(
              body.diagnostics.begin(),
              body.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-UNSUPPORTED-022";
              }),
      "package-body rejection recovers at the outer end clause");
}

void test_ignored_initializers_are_rejected() {
  const auto vhdl = parse_text(
      "initializers.vhd",
      R"(
entity initializers is
  port (input : in std_logic := '0');
end entity;
architecture rtl of initializers is
  signal state : std_logic := '1';
begin
end architecture;
)",
      Language::Vhdl2008);
  require(!vhdl.ok(), "VHDL defaults and initializers must be rejected");
  bool port_default = false;
  bool signal_initializer = false;
  for (const auto& diagnostic : vhdl.diagnostics) {
    port_default =
        port_default
        || diagnostic.code == "FSIM-VHDL-UNSUPPORTED-011";
    signal_initializer =
        signal_initializer
        || diagnostic.code == "FSIM-VHDL-UNSUPPORTED-012";
  }
  require(port_default, "VHDL port default needs a targeted diagnostic");
  require(
      signal_initializer,
      "VHDL signal initializer needs a targeted diagnostic");

  const auto sv = parse_text(
      "initializers.sv",
      R"(
module initializers(
  input logic input_value = 1'b0
);
  logic state = 1'b1;
endmodule
)",
      Language::SystemVerilog2017);
  require(!sv.ok(), "SV defaults and initializers must be rejected");
  bool port_default_sv = false;
  bool declaration_initializer = false;
  for (const auto& diagnostic : sv.diagnostics) {
    port_default_sv =
        port_default_sv
        || diagnostic.code == "FSIM-SV-UNSUPPORTED-010";
    declaration_initializer =
        declaration_initializer
        || diagnostic.code == "FSIM-SV-UNSUPPORTED-011";
  }
  require(port_default_sv, "SV port default needs a targeted diagnostic");
  require(
      declaration_initializer,
      "SV declaration initializer needs a targeted diagnostic");
}

void test_duplicate_declarations_are_rejected() {
  const auto vhdl = parse_text(
      "duplicates.vhd",
      R"(
entity duplicates is
  port (value : in std_logic; VALUE : out std_logic);
end entity;
architecture rtl of duplicates is
  signal state : std_logic;
  signal STATE : std_logic;
begin
end architecture;
)",
      Language::Vhdl2008);
  require(!vhdl.ok(), "VHDL duplicate declarations must be rejected");
  bool duplicate_port = false;
  bool duplicate_signal = false;
  for (const auto& diagnostic : vhdl.diagnostics) {
    duplicate_port =
        duplicate_port || diagnostic.code == "FSIM-VHDL-SEM-002";
    duplicate_signal =
        duplicate_signal || diagnostic.code == "FSIM-VHDL-SEM-003";
  }
  require(duplicate_port, "VHDL duplicate port diagnostic");
  require(duplicate_signal, "VHDL duplicate signal diagnostic");

  const auto sv = parse_text(
      "duplicates.sv",
      R"(
module duplicates(input logic value, input logic value);
  logic state;
  logic state;
endmodule
)",
      Language::SystemVerilog2017);
  require(!sv.ok(), "SV duplicate declarations must be rejected");
  duplicate_port = false;
  duplicate_signal = false;
  for (const auto& diagnostic : sv.diagnostics) {
    duplicate_port =
        duplicate_port || diagnostic.code == "FSIM-SV-SEM-003";
    duplicate_signal =
        duplicate_signal || diagnostic.code == "FSIM-SV-SEM-006";
  }
  require(duplicate_port, "SV duplicate port diagnostic");
  require(duplicate_signal, "SV duplicate signal diagnostic");
}

void test_systemverilog_timescale_context() {
  const auto result = parse_text(
      "timescale.sv",
      R"(`timescale 10ns/100ps
module timed;
  initial #2 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "a legal `timescale must be represented");
  require(result.design.units.size() == 1, "timescale module count");
  const auto& unit = result.design.units.front();
  require(unit.time_unit == "10ns", "module time unit");
  require(unit.time_precision == "100ps", "module time precision");
  require(
      unit.processes.size() == 1
          && unit.processes.front().statements.size() == 1,
      "timed initial process");
  const auto& delay = *unit.processes.front().statements.front().delay;
  require(delay.magnitude == 20 && delay.unit == "ns",
          "integer delay must inherit and scale by `timescale");

  const auto mid_module_directive = parse_text(
      "mid_module_timescale.sv",
      R"(`timescale 1ns/1ns
module first;
  `timescale 10ns/1ns
  initial #2 $finish;
endmodule
module second;
  initial #2 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      mid_module_directive.ok(),
      "a directive inside a module must affect only following modules");
  require(
      mid_module_directive.design.units.size() == 2,
      "mid-module timescale design unit count");
  const auto& first = mid_module_directive.design.units[0];
  const auto& second = mid_module_directive.design.units[1];
  require(
      first.time_unit == "1ns"
          && first.processes.front().statements.front().delay->magnitude == 2,
      "current module must retain the timescale active at its start");
  require(
      second.time_unit == "10ns"
          && second.processes.front().statements.front().delay->magnitude == 20,
      "mid-module directive must apply to a following module");

  const auto invalid_precision = parse_text(
      "bad_timescale.sv",
      "`timescale 1ps/1ns\nmodule bad; endmodule\n",
      Language::SystemVerilog2017);
  require(!invalid_precision.ok(), "coarse precision must be rejected");
  require(
      std::any_of(
          invalid_precision.diagnostics.begin(),
          invalid_precision.diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-009";
          }),
      "coarse precision diagnostic");

}

void test_systemverilog_compiler_directives() {
  const auto directives = parse_text(
      "directives.sv",
      R"(`default_nettype none
module strict;
  assign forbidden = 1'b1;
endmodule
`default_nettype tri1
module implicit_net;
  assign created = 1'b0;
endmodule
`celldefine
module cell_unit;
endmodule
`endcelldefine
module child(input logic value);
endmodule
`unconnected_drive pull1
module driven_parent;
  child child_instance();
endmodule
`nounconnected_drive
`begin_keywords "1800-2009"
module soft;
endmodule
`end_keywords
`begin_keywords "1364-2005"
module logic;
endmodule
`end_keywords
`timescale 10ns/1ns
`default_nettype tri0
`celldefine
`unconnected_drive pull0
`resetall
module reset_unit;
  initial #2 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !directives.ok(),
      "`default_nettype none must reject an implicit declaration");
  require(
      std::any_of(
          directives.diagnostics.begin(),
          directives.diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-015";
          }),
      "forbidden implicit net diagnostic");
  require(
      directives.design.units.size() == 8,
      "all directive-state modules remain represented");

  const auto* implicit_net =
      directives.design.find(UnitKind::VerilogModule, "implicit_net");
  require(
      implicit_net != nullptr
          && implicit_net->default_nettype == "tri1"
          && implicit_net->signals.size() == 1
          && implicit_net->signals.front().name == "created"
          && implicit_net->signals.front().type.spelling == "tri1",
      "`default_nettype creates a typed implicit scalar net");
  const auto* cell =
      directives.design.find(UnitKind::VerilogModule, "cell_unit");
  require(cell != nullptr && cell->is_cell,
          "`celldefine marks following modules");
  const auto* child =
      directives.design.find(UnitKind::VerilogModule, "child");
  require(child != nullptr && !child->is_cell,
          "`endcelldefine restores ordinary module metadata");
  const auto* parent =
      directives.design.find(UnitKind::VerilogModule, "driven_parent");
  require(
      parent != nullptr && parent->instances.size() == 1
          && parent->instances.front().unconnected_drive
              == VerilogUnconnectedDrive::Pull1,
      "`unconnected_drive state is captured by an instance");
  require(
      directives.design.find(UnitKind::VerilogModule, "soft") != nullptr,
      "1800-2009 keyword scope precedes the 1800-2012 soft keyword");
  require(
      directives.design.find(UnitKind::VerilogModule, "logic") != nullptr,
      "legacy begin_keywords permits a later SystemVerilog keyword as a name");
  const auto* reset =
      directives.design.find(UnitKind::VerilogModule, "reset_unit");
  require(
      reset != nullptr && reset->default_nettype == "wire"
          && !reset->is_cell && reset->time_unit.empty()
          && reset->processes.front().statements.front().delay->magnitude == 2
          && reset->processes.front().statements.front().delay->unit.empty(),
      "`resetall restores directive defaults before a following module");

  const auto port_none = parse_text(
      "default-none-port.sv",
      "`default_nettype none\nmodule bad(input value); endmodule\n",
      Language::SystemVerilog2017);
  require(
      !port_none.ok()
          && std::any_of(
              port_none.diagnostics.begin(),
              port_none.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-016";
              }),
      "`default_nettype none rejects an untyped ANSI port");

  const auto reserved_name = parse_text(
      "reserved-name.sv",
      R"(`begin_keywords "1800-2012"
module soft;
endmodule
`end_keywords
)",
      Language::SystemVerilog2017);
  require(
      !reserved_name.ok()
          && std::any_of(
              reserved_name.diagnostics.begin(),
              reserved_name.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-001";
              }),
      "1800-2012 keyword scope rejects soft as an identifier");

  const auto malformed = parse_text(
      "malformed-directives.sv",
      R"(`default_nettype banana
`unconnected_drive highz
`begin_keywords "1800-2099"
`end_keywords
`resetall extra
`begin_keywords "1800-2017"
module valid;
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        malformed.diagnostics.begin(),
        malformed.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !malformed.ok() && has_code("FSIM-SV-PP-032")
          && has_code("FSIM-SV-PP-034")
          && has_code("FSIM-SV-PP-035")
          && has_code("FSIM-SV-PP-036")
          && has_code("FSIM-SV-PP-037")
          && has_code("FSIM-SV-PP-038"),
      "malformed directive forms have stable targeted diagnostics");
}

void test_systemverilog_parameters() {
  const auto result = parse_text(
      "parameters.sv",
      R"(
module parameterized #(
  parameter int WIDTH = 8,
  parameter logic [1:0] MODE = 2'b01,
  localparam int LAST = WIDTH - 1
) (
  input logic [WIDTH - 1:0] data,
  output logic [WIDTH - 1:0] result
);
  localparam int DOUBLE_WIDTH = WIDTH * 2;
  assign result = data;
endmodule

module parameter_top;
  logic [3:0] input_value;
  logic [3:0] named_value;
  logic [1:0] positional_value;
  parameterized #(.WIDTH(4), .MODE(2'b10)) named_instance(
    .data(input_value),
    .result(named_value)
  );
  parameterized #(2, 2'b11) positional_instance(
    .data(input_value[1:0]),
    .result(positional_value)
  );
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "module parameters and overrides must parse");
  require(result.design.units.size() == 2, "parameterized unit count");
  const auto* parameterized =
      result.design.find(UnitKind::VerilogModule, "parameterized");
  require(
      parameterized != nullptr && parameterized->parameters.size() == 4,
      "parameter and localparam declarations are retained in source order");
  require(
      parameterized->parameters[0].name == "WIDTH"
          && parameterized->parameters[0].type.spelling == "int"
          && !parameterized->parameters[0].local
          && parameterized->parameters[1].name == "MODE"
          && parameterized->parameters[1].type.packed_range
          && parameterized->parameters[1].type.packed_range->width() == 2
          && parameterized->parameters[2].name == "LAST"
          && parameterized->parameters[2].local
          && parameterized->parameters[3].name == "DOUBLE_WIDTH"
          && parameterized->parameters[3].local,
      "typed parameter metadata");
  require(
      parameterized->ports.size() == 2
          && !parameterized->ports.front().type.packed_range
          && parameterized->ports.front().type.packed_range_expression
          && parameterized->ports.front()
                 .type.packed_range_expression->left.kind
              == ExpressionKind::Binary,
      "symbolic packed range remains in typed HIR");
  const auto* top =
      result.design.find(UnitKind::VerilogModule, "parameter_top");
  require(
      top != nullptr && top->instances.size() == 2
          && top->instances[0].parameter_overrides.size() == 2
          && top->instances[0].parameter_overrides[0].name
              == std::optional<std::string>{"WIDTH"}
          && top->instances[1].parameter_overrides.size() == 2
          && !top->instances[1].parameter_overrides[0].name,
      "named and positional parameter overrides are represented");

  const auto invalid = parse_text(
      "invalid-parameters.sv",
      R"(
module invalid_parameters #(
  parameter WIDTH,
  parameter WIDTH = 2
);
endmodule
module invalid_parameter_top;
  invalid_parameters #(.WIDTH(1), .WIDTH(2)) duplicate();
  invalid_parameters #(.WIDTH(1), 2) mixed();
endmodule
module parameter_object_conflict #(
  parameter CLASH = 1
) (
  input logic CLASH
);
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        invalid.diagnostics.begin(),
        invalid.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok() && has_code("FSIM-SV-PARSE-050")
          && has_code("FSIM-SV-SEM-017")
          && has_code("FSIM-SV-SEM-018")
          && has_code("FSIM-SV-SEM-019")
          && has_code("FSIM-SV-SEM-020"),
      "parameter declaration and override diagnostics are targeted");
}

void test_systemverilog_packages() {
  const auto parsed = parse_text(
      "packages.sv",
      R"(
package base_values;
  parameter int WIDTH = 4;
  localparam int BASE = 5;
  typedef logic [WIDTH-1:0] word_t;
  typedef enum logic [1:0] {
    IDLE,
    RUN = 2,
    DONE
  } state_t;
  typedef enum logic signed [1:0] {
    LOW = -2,
    NEXT_LOW,
    ZERO,
    HIGH
  } signed_state_t;
  typedef struct packed {
    logic [WIDTH-1:0] payload;
    bit valid;
  } packet_t;
  typedef union packed {
    logic [WIDTH-1:0] payload;
    logic [WIDTH-1:0] mirror;
  } overlay_t;
endpackage : base_values

import base_values::*;
package derived_values;
  localparam int NEXT = BASE + 1;
  typedef base_values::word_t derived_word_t;
endpackage : derived_values

import base_values::WIDTH, base_values::packet_t,
       base_values::overlay_t, derived_values::NEXT;
import derived_values::derived_word_t;
module package_user #(
  parameter derived_word_t INITIAL = NEXT
)(output derived_word_t observed);
  typedef derived_word_t local_word_t;
  local_word_t staged;
  base_values::packet_t packet;
  base_values::overlay_t overlay;
  generate
    if (WIDTH) begin : typed
      base_values::word_t generated;
    end
  endgenerate
  initial begin
    derived_word_t local_value = NEXT;
    staged = local_value;
    packet.payload = local_value;
    packet.valid = 1'b1;
    overlay.payload = local_value;
  end
  assign observed = staged;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "bounded SystemVerilog packages must parse");
  require(
      parsed.design.units.size() == 3
          && parsed.design.units[0].kind
              == UnitKind::SystemVerilogPackage
          && parsed.design.units[1].kind
              == UnitKind::SystemVerilogPackage
          && parsed.design.units[2].kind
              == UnitKind::VerilogModule,
      "packages and module retain distinct unit kinds");
  require(
      parsed.design.units[0].parameters.size() == 9
          && parsed.design.units[0].parameters[0].local
          && parsed.design.units[1].parameters.front()
                 .default_value.operands.front().text
              == "BASE",
      "package parameters are immutable declaration-ordered constants");
  require(
      parsed.design.units[0].type_aliases.size() == 5
          && parsed.design.units[0].type_aliases.front()
                 .name
              == "word_t"
          && parsed.design.units[0].type_aliases.front()
                 .type.packed_range_expression
          && parsed.design.units[1].type_aliases.size() == 1
          && parsed.design.units[1].type_aliases.front()
                 .type.named_type
              == "base_values::word_t",
      "package typedef targets and parameterized ranges survive parsing");
  const auto& state_type =
      parsed.design.units[0].type_aliases[1];
  require(
      state_type.name == "state_t"
          && state_type.enum_literals.size() == 3
          && state_type.enum_literals[0].value.text == "0"
          && state_type.enum_literals[1].value.text == "2"
          && state_type.enum_literals[2].value.kind
              == ExpressionKind::Binary
          && state_type.enum_literals[2].value.operands[0].text
              == "RUN",
      "packed enum typedefs retain explicit and implicit literal values");
  require(
      parsed.design.units[0].type_aliases[2].type.is_signed
          && parsed.design.units[0].type_aliases[2]
                 .enum_literals.size()
              == 4,
      "signed packed enum bases and literal sequences survive parsing");
  const auto& packet_type =
      parsed.design.units[0].type_aliases[3].type;
  require(
      packet_type.packed_members.size() == 2
          && packet_type.packed_members[0].name == "payload"
          && packet_type.packed_members[0]
                 .packed_range_expression
          && packet_type.packed_members[1].name == "valid"
          && packet_type.domain == ValueDomain::Logic4,
      "parameterized packed struct members survive typed HIR parsing");
  const auto& overlay_type =
      parsed.design.units[0].type_aliases[4].type;
  require(
      overlay_type.packed_aggregate
              == PackedAggregateKind::Union
          && overlay_type.packed_members.size() == 2
          && overlay_type.packed_members[0].lsb_offset == 0
          && overlay_type.packed_members[1].lsb_offset == 0,
      "packed union members retain a shared overlay layout");
  require(
      parsed.design.units[1].systemverilog_imports.size() == 1
          && parsed.design.units[1]
                 .systemverilog_imports.front()
                 .name.empty()
          && parsed.design.units[2]
                 .systemverilog_imports.size()
              == 6,
      "compilation-unit wildcard and selected imports reach later units");
  require(
      parsed.design.units[2].ports.front().type.named_type
              == "derived_word_t"
          && parsed.design.units[2].type_aliases.front()
                 .type.named_type
              == "derived_word_t"
          && parsed.design.units[2].parameters.back()
                 .type.named_type
              == "derived_word_t"
          && parsed.design.units[2].signals.front()
                 .type.named_type
              == "local_word_t"
          && parsed.design.units[2].signals[1]
                 .type.named_type
              == "base_values::packet_t"
          && parsed.design.units[2].signals[2]
                 .type.named_type
              == "base_values::overlay_t"
          && parsed.design.units[2].generate_regions.front()
                 .then_body.signals.front().type.named_type
              == "base_values::word_t"
          && parsed.design.units[2].processes.front()
                 .variables.front().type.named_type
              == "derived_word_t"
          && parsed.design.units[2]
                 .concurrent_statements.front()
                 .value.text
              == "staged",
      "imported/scoped aliases survive port, signal, and local HIR parsing");

  const auto invalid = parse_text(
      "invalid_packages.sv",
      R"(
package invalid_values;
  logic unsupported;
  typedef string unsupported_t;
  typedef logic duplicate_t;
  typedef bit duplicate_t;
  typedef logic unpacked_t [2];
  typedef enum { MISSING_BASE } missing_base_t;
  typedef enum logic [1:0] {} empty_enum_t;
  typedef enum logic [1:0] A, B } missing_open_t;
  typedef enum logic [1:0] { C missing_close_t;
  typedef struct invalid_unpacked_t;
  typedef struct packed {
    int unsupported;
  } unsupported_member_t;
  typedef struct packed {
    logic duplicate;
    bit duplicate;
    logic unpacked [2];
    logic initialized = 1'b0;
  } invalid_members_t;
  typedef struct packed {} empty_struct_t;
  typedef struct packed logic open_member; } missing_struct_open_t;
  typedef struct packed { logic missing_semicolon } missing_member_semicolon_t;
endpackage : wrong_name
import invalid_values;
module recovered;
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::any_of(
        invalid.diagnostics.begin(),
        invalid.diagnostics.end(),
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok()
          && has_code("FSIM-SV-UNSUPPORTED-023")
          && has_code("FSIM-SV-UNSUPPORTED-024")
          && has_code("FSIM-SV-UNSUPPORTED-025")
          && has_code("FSIM-SV-UNSUPPORTED-026")
          && has_code("FSIM-SV-UNSUPPORTED-027")
          && has_code("FSIM-SV-UNSUPPORTED-028")
          && has_code("FSIM-SV-UNSUPPORTED-029")
          && has_code("FSIM-SV-PARSE-083")
          && has_code("FSIM-SV-PARSE-084")
          && has_code("FSIM-SV-PARSE-085")
          && has_code("FSIM-SV-PARSE-086")
          && has_code("FSIM-SV-PARSE-088")
          && has_code("FSIM-SV-PARSE-089")
          && has_code("FSIM-SV-SEM-023")
          && has_code("FSIM-SV-SEM-024")
          && has_code("FSIM-SV-SEM-025")
          && has_code("FSIM-SV-PARSE-078"),
      "invalid package items, end names, and imports are targeted");

  const auto missing_struct_close = parse_text(
      "missing_struct_close.sv",
      R"(
package incomplete_struct;
  typedef struct packed {
    logic member;
)",
      Language::SystemVerilog2017);
  require(
      !missing_struct_close.ok()
          && std::any_of(
              missing_struct_close.diagnostics.begin(),
              missing_struct_close.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-087";
              }),
      "unterminated packed structs have a targeted closing-brace diagnostic");
}

void test_immediate_assertions() {
  const auto vhdl = parse_text(
      "assertions.vhd",
      R"(
entity assertions is
end entity;
architecture rtl of assertions is
begin
  check: process
  begin
    assert 0 = 1 report "vhdl mismatch" severity failure;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "bounded VHDL assertion syntax");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr && architecture->processes.size() == 1,
      "VHDL assertion process");
  const auto& vhdl_assertion =
      architecture->processes.front().statements.front();
  require(
      vhdl_assertion.kind == StatementKind::Assert
          && vhdl_assertion.assertion_message == "vhdl mismatch"
          && vhdl_assertion.assertion_severity
              == AssertionSeverity::Failure
          && vhdl_assertion.span.source_name == "assertions.vhd"
          && vhdl_assertion.span.begin.line == 8,
      "VHDL assertion metadata");

  const auto system_verilog = parse_text(
      "assertions.sv",
      R"(
module assertions;
  initial assert (1'b0) else $error("sv mismatch");
endmodule
)",
      Language::SystemVerilog2017);
  require(system_verilog.ok(), "bounded SystemVerilog assertion syntax");
  const auto& sv_assertion =
      system_verilog.design.units.front().processes.front().statements.front();
  require(
      sv_assertion.kind == StatementKind::Assert
          && sv_assertion.assertion_message == "sv mismatch"
          && sv_assertion.assertion_severity == AssertionSeverity::Error
          && sv_assertion.span.source_name == "assertions.sv"
          && sv_assertion.span.begin.line == 3,
      "SystemVerilog assertion metadata");

  const auto invalid_vhdl_severity = parse_text(
      "bad_assertion.vhd",
      R"(
entity bad_assertion is end entity;
architecture rtl of bad_assertion is begin
  check: process begin
    assert 1 severity panic;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl_severity.ok()
          && std::any_of(
              invalid_vhdl_severity.diagnostics.begin(),
              invalid_vhdl_severity.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-011";
              }),
      "invalid VHDL assertion severity diagnostic");

  const auto invalid_sv_action = parse_text(
      "bad_assertion.sv",
      R"(
module bad_assertion;
  initial assert (1'b0) else $fatal;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_sv_action.ok()
          && std::any_of(
              invalid_sv_action.diagnostics.begin(),
              invalid_sv_action.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-041";
              }),
      "unsupported SystemVerilog assertion action diagnostic");
}

void test_process_variable_declarations() {
  const auto vhdl = parse_text(
      "locals.vhd",
      R"(
entity locals is end entity;
architecture rtl of locals is begin
  worker: process
    variable state : std_logic := '1';
    variable flags : bit_vector(1 downto 0);
  begin
    state := '0';
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "bounded VHDL process variables must parse");
  const auto& vhdl_variables =
      vhdl.design.units.back().processes.front().variables;
  require(
      vhdl_variables.size() == 2
          && vhdl_variables[0].name == "state"
          && vhdl_variables[0].type.width() == 1
          && vhdl_variables[0].initializer
          && vhdl_variables[1].name == "flags"
          && vhdl_variables[1].type.width() == 2
          && !vhdl_variables[1].initializer,
      "VHDL process-variable metadata");

  const auto system_verilog = parse_text(
      "locals.sv",
      R"(
module locals;
  initial begin
    logic [3:0] state = 4'b0011;
    bit ready;
    state = 4'b1010;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      system_verilog.ok(),
      "bounded SystemVerilog procedural variables must parse");
  const auto& sv_variables =
      system_verilog.design.units.front().processes.front().variables;
  require(
      sv_variables.size() == 2
          && sv_variables[0].name == "state"
          && sv_variables[0].type.width() == 4
          && sv_variables[0].initializer
          && sv_variables[1].name == "ready"
          && sv_variables[1].type.domain == ValueDomain::Bit2
          && !sv_variables[1].initializer,
      "SystemVerilog procedural-variable metadata");
}

void test_procedural_wait_statements() {
  const auto vhdl = parse_text(
      "waits.vhd",
      R"(
entity waits is end entity;
architecture rtl of waits is
  signal trigger : std_logic;
begin
  timer: process
  begin
    wait for 2 ns;
    wait on trigger;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "bounded VHDL wait statements must parse");
  const auto& vhdl_statements =
      vhdl.design.units.back().processes.front().statements;
  require(
      vhdl_statements.size() == 2
          && vhdl_statements[0].kind == StatementKind::Delay
          && vhdl_statements[0].delay
          && vhdl_statements[0].delay->magnitude == 2
          && vhdl_statements[0].delay->unit == "ns"
          && vhdl_statements[1].kind == StatementKind::WaitOn
          && vhdl_statements[1].sensitivities.size() == 1
          && vhdl_statements[1].sensitivities.front().signal
              == "trigger",
      "VHDL wait metadata");

  const auto system_verilog = parse_text(
      "events.sv",
      R"(
module events;
  logic trigger;
  logic observed;
  initial begin
    @(posedge trigger);
    @(negedge trigger) observed = trigger;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      system_verilog.ok(),
      "bounded SystemVerilog procedural event controls must parse");
  const auto& sv_statements =
      system_verilog.design.units.front().processes.front().statements;
  require(
      sv_statements.size() == 2
          && sv_statements[0].kind == StatementKind::WaitOn
          && sv_statements[0].sensitivities.size() == 1
          && sv_statements[0].sensitivities.front().signal == "trigger"
          && sv_statements[0].sensitivities.front().edge
              == EdgeKind::Positive
          && sv_statements[0].statements.empty()
          && sv_statements[1].kind == StatementKind::WaitOn
          && sv_statements[1].sensitivities.front().edge
              == EdgeKind::Negative
          && sv_statements[1].statements.size() == 1
          && sv_statements[1].statements.front().kind
              == StatementKind::Assignment,
      "SystemVerilog procedural event metadata");

  const auto invalid_vhdl = parse_text(
      "bad_wait.vhd",
      R"(
entity bad_wait is end entity;
architecture rtl of bad_wait is
  signal trigger : std_logic;
begin
  worker: process(trigger)
  begin
    wait on trigger;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl.ok()
          && std::any_of(
              invalid_vhdl.diagnostics.begin(),
              invalid_vhdl.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-012";
              }),
      "VHDL sensitivity-list/wait conflict diagnostic");

  const auto nested_vhdl = parse_text(
      "nested_wait.vhd",
      R"(
entity nested_wait is end entity;
architecture rtl of nested_wait is begin
  worker: process begin
    if 1 = 1 then
      wait for 1 ns;
    end if;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !nested_vhdl.ok()
          && std::any_of(
              nested_vhdl.diagnostics.begin(),
              nested_vhdl.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-UNSUPPORTED-017";
              }),
      "nested VHDL wait diagnostic");

  const auto wildcard_sv = parse_text(
      "wildcard_event.sv",
      R"(
module wildcard_event;
  logic trigger;
  logic observed;
  initial @* observed = trigger;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      wildcard_sv.ok(),
      "dynamic wildcard procedural event must parse");
  const auto& wildcard_wait =
      wildcard_sv.design.units.front()
          .processes.front()
          .statements.front();
  require(
      wildcard_wait.kind == StatementKind::WaitOn
          && wildcard_wait.sensitivities.size() == 1
          && wildcard_wait.sensitivities.front().signal == "*"
          && wildcard_wait.statements.size() == 1,
      "dynamic wildcard event metadata");
}

void test_wildcard_and_always_comb_processes() {
  const auto result = parse_text(
      "combinational.sv",
      R"(
module combinational;
  logic a;
  logic q;
  logic y;
  logic latch_q;
  always @* q = a;
  always_comb y = ~q;
  always_latch if (a) latch_q = q;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "wildcard always and bounded always_comb must parse");
  const auto& processes = result.design.units.front().processes;
  require(
      processes.size() == 3
          && processes[0].kind == ProcessKind::VerilogAlways
          && processes[0].sensitivities.size() == 1
          && processes[0].sensitivities.front().signal == "*"
          && processes[1].kind
              == ProcessKind::SystemVerilogAlwaysComb
          && processes[1].sensitivities.size() == 1
          && processes[1].sensitivities.front().signal == "*"
          && processes[2].kind
              == ProcessKind::SystemVerilogAlwaysLatch
          && processes[2].sensitivities.size() == 1
          && processes[2].sensitivities.front().signal == "*",
      "wildcard process metadata");

  const auto invalid_system_verilog = parse_text(
      "bad_comb.sv",
      R"(
module bad_comb;
  logic a;
  logic q;
  always_comb @(a) q = a;
  always_comb #1 q = a;
  always_comb q <= a;
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid_system_verilog.ok(), "invalid always_comb forms");
  for (const auto code : {
           std::string_view{"FSIM-SV-SEM-011"},
           std::string_view{"FSIM-SV-SEM-012"},
           std::string_view{"FSIM-SV-SEM-013"}}) {
    require(
        std::any_of(
            invalid_system_verilog.diagnostics.begin(),
            invalid_system_verilog.diagnostics.end(),
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted always_comb diagnostic");
  }

  const auto invalid_verilog = parse_text(
      "bad_comb.v",
      R"(
module bad_comb;
  reg q;
  always_comb q = 1'b0;
  always_latch q = 1'b0;
endmodule
)",
      Language::Verilog2005);
  require(
      !invalid_verilog.ok()
          && std::any_of(
              invalid_verilog.diagnostics.begin(),
              invalid_verilog.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-002";
              })
          && std::any_of(
              invalid_verilog.diagnostics.begin(),
              invalid_verilog.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-003";
              }),
      "always_comb/always_latch language-version diagnostics");
}

void test_systemverilog_case_statements() {
  const auto result = parse_text(
      "case_statement.sv",
      R"(
module case_statement;
  logic [1:0] selector;
  logic [1:0] result;
  always_comb case (selector)
    2'b00: result = 2'b01;
    2'b01, 2'b10: begin
      result = 2'b10;
    end
    default: result = 2'b11;
  endcase
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "exact SystemVerilog case statement must parse");
  const auto& statement =
      result.design.units.front().processes.front().statements.front();
  require(
      statement.kind == StatementKind::Case
          && statement.condition.kind == ExpressionKind::Identifier
          && statement.condition.text == "selector"
          && statement.case_alternatives.size() == 3,
      "case selector and ordered alternatives");
  require(
      statement.case_alternatives[0].choices.size() == 1
          && statement.case_alternatives[1].choices.size() == 2
          && statement.case_alternatives[1].statements.size() == 1
          && statement.case_alternatives[1].statements.front().kind
              == StatementKind::Block
          && statement.case_alternatives[2].is_default
          && statement.case_alternatives[2].choices.empty(),
      "case choices, block body, and default metadata");

  const auto invalid = parse_text(
      "bad_case.sv",
      R"(
module bad_case;
  logic selector;
  logic result;
  always_comb case (selector)
    default: result = 1'b0;
    default: result = 1'b1;
  endcase
  initial casex (selector)
    1'bx: result = 1'b0;
  endcase
  initial unique case (selector)
    1'b0: result = 1'b0;
  endcase
  initial unique0 case (selector)
    1'b0: result = 1'b0;
  endcase
  initial case (selector) inside
    [1'b0:1'b1]: result = 1'b0;
  endcase
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid.ok(), "unsupported and duplicate case forms must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-SEM-014"},
           std::string_view{"FSIM-SV-UNSUPPORTED-016"},
           std::string_view{"FSIM-SV-UNSUPPORTED-017"},
           std::string_view{"FSIM-SV-UNSUPPORTED-018"}}) {
    require(
        std::any_of(
            invalid.diagnostics.begin(),
            invalid.diagnostics.end(),
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted case diagnostic");
  }
  require(
      invalid.design.units.front().processes.size() == 5,
      "case diagnostics must recover to following processes");
}

void test_systemverilog_conditional_expression() {
  const auto result = parse_text(
      "conditional.sv",
      R"(
module conditional;
  logic select;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [3:0] result;
  always_comb result = select ? lhs : rhs;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(), "SystemVerilog conditional expression must parse");
  const auto& value =
      result.design.units.front().processes.front()
          .statements.front().value;
  require(
      value.kind == ExpressionKind::Call
          && value.text == "?:"
          && value.operands.size() == 3
          && value.operands[0].text == "select"
          && value.operands[1].text == "lhs"
          && value.operands[2].text == "rhs",
      "conditional-expression operand order");
}

void test_systemverilog_comparison_expressions() {
  const auto result = parse_text(
      "comparisons.sv",
      R"(
module comparisons;
  logic [3:0] lhs;
  logic [3:0] rhs;
  logic [2:0] amount;
  logic [3:0] shifted;
  logic result;
  always_comb begin
    result = !lhs;
    result = lhs != rhs;
    result = lhs < rhs;
    result = lhs <= rhs;
    result = lhs > rhs;
    result = lhs >= rhs;
    result = lhs && result;
    result = result || rhs;
    result = &lhs;
    result = |lhs;
    result = ^lhs;
    shifted = lhs << amount;
    shifted = lhs >> amount;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(), "SystemVerilog comparison expressions must parse");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 13
          && statements[0].value.kind == ExpressionKind::Unary
          && statements[0].value.text == "!",
      "logical-negation expression node");
  const std::array<std::string_view, 5> operators{
      "!=", "<", "<=", ">", ">="};
  for (std::size_t index = 0; index < operators.size(); ++index) {
    require(
        statements[index + 1].value.kind
                == ExpressionKind::Binary
            && statements[index + 1].value.text
                == operators[index],
        "comparison expression node");
  }
  require(
      statements[6].value.kind == ExpressionKind::Binary
          && statements[6].value.text == "&&"
          && statements[7].value.kind == ExpressionKind::Binary
          && statements[7].value.text == "||",
      "logical binary expression nodes");
  require(
      statements[8].value.kind == ExpressionKind::Unary
          && statements[8].value.text == "&"
          && statements[9].value.kind == ExpressionKind::Unary
          && statements[9].value.text == "|"
          && statements[10].value.kind == ExpressionKind::Unary
          && statements[10].value.text == "^",
      "reduction expression nodes");
  require(
      statements[11].value.kind == ExpressionKind::Binary
          && statements[11].value.text == "<<"
          && statements[12].value.kind == ExpressionKind::Binary
          && statements[12].value.text == ">>",
      "logical-shift expression nodes");
}

void test_systemverilog_arithmetic_expressions() {
  const auto result = parse_text(
      "arithmetic.sv",
      R"(
module arithmetic;
  logic [7:0] lhs;
  logic [7:0] rhs;
  logic [7:0] result;
  always_comb begin
    result = +lhs;
    result = -lhs;
    result = lhs - rhs;
    result = lhs * rhs;
    result = lhs / rhs;
    result = lhs % rhs;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(), "SystemVerilog arithmetic expressions must parse");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 6
          && statements[0].value.kind == ExpressionKind::Unary
          && statements[0].value.text == "+"
          && statements[1].value.kind == ExpressionKind::Unary
          && statements[1].value.text == "-",
      "unary arithmetic expression nodes");
  const std::array<std::string_view, 4> operators{
      "-", "*", "/", "%"};
  for (std::size_t index = 0; index < operators.size(); ++index) {
    require(
        statements[index + 2].value.kind
                == ExpressionKind::Binary
            && statements[index + 2].value.text
                == operators[index],
        "binary arithmetic expression node");
  }
}

void test_systemverilog_select_and_concatenation_expressions() {
  const auto result = parse_text(
      "select_concat.sv",
      R"(
module select_concat;
  logic [15:8] descending;
  logic [0:7] ascending;
  logic selected;
  logic [3:0] part;
  logic [8:0] combined;
  always_comb begin
    selected = descending[10];
    part = ascending[2:5];
    combined = {
      descending[15:12], descending[10], ascending[4:7]
    };
    descending[9] = selected;
    ascending[4:5] = part;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "SystemVerilog select and concatenation expressions must parse");
  const auto& signals = result.design.units.front().signals;
  require(
      signals[0].type.packed_range
          && signals[0].type.packed_range->left == 15
          && signals[0].type.packed_range->right == 8
          && signals[0].type.packed_range->descending,
      "descending packed range metadata");
  require(
      signals[1].type.packed_range
          && signals[1].type.packed_range->left == 0
          && signals[1].type.packed_range->right == 7
          && !signals[1].type.packed_range->descending,
      "ascending packed range metadata");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 5
          && statements[0].value.kind == ExpressionKind::Index
          && statements[0].value.operands.size() == 2
          && statements[0].value.operands[1].text == "10",
      "bit-select expression node");
  require(
      statements[1].value.kind == ExpressionKind::Slice
          && statements[1].value.operands.size() == 3
          && statements[1].value.operands[1].text == "2"
          && statements[1].value.operands[2].text == "5",
      "ascending part-select expression node");
  require(
      statements[2].value.kind == ExpressionKind::Concatenation
          && statements[2].value.operands.size() == 3
          && statements[2].value.operands[0].kind
              == ExpressionKind::Slice
          && statements[2].value.operands[1].kind
              == ExpressionKind::Index
          && statements[2].value.operands[2].kind
              == ExpressionKind::Slice,
      "concatenation expression node and operand order");
  require(
      statements[3].target.kind == ExpressionKind::Index
          && statements[3].target.operands.size() == 2
          && statements[3].target.operands[1].text == "9"
          && statements[4].target.kind == ExpressionKind::Slice
          && statements[4].target.operands.size() == 3,
      "SystemVerilog selected assignment target nodes");
}

void test_conditional_statement_trees() {
  const auto vhdl = parse_text(
      "conditionals.vhd",
      R"(
entity conditionals is end entity;
architecture rtl of conditionals is
  signal result : boolean;
begin
  choose: process
  begin
    if true then
      result <= false;
    elsif false then
      if true then
        result <= true;
      else
        result <= false;
      end if;
    else
      result <= true;
    end if;
    result <= (true nand false) and (false nor false)
              and (true xnor true) and (true /= false);
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !vhdl.ok(),
      "the unsupported bare wait should be the only VHDL diagnostic");
  require(
      vhdl.diagnostics.size() == 1
          && vhdl.diagnostics.front().code
              == "FSIM-VHDL-UNSUPPORTED-016",
      "VHDL conditional parsing must recover through a trailing bare wait");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 2,
      "VHDL conditional process representation");
  const auto& outer =
      architecture->processes.front().statements.front();
  require(
      outer.kind == StatementKind::If
          && outer.condition.kind == ExpressionKind::BooleanLiteral
          && outer.condition.text == "true"
          && outer.statements.size() == 1
          && outer.statements.front().value.kind
              == ExpressionKind::BooleanLiteral
          && outer.else_statements.size() == 1,
      "VHDL true branch and boolean literal nodes");
  const auto& elsif = outer.else_statements.front();
  require(
      elsif.kind == StatementKind::If
          && elsif.condition.kind == ExpressionKind::BooleanLiteral
          && elsif.condition.text == "false"
          && elsif.statements.size() == 1
          && elsif.statements.front().kind == StatementKind::If
          && elsif.else_statements.size() == 1,
      "VHDL elsif is retained as an ordered nested false branch");
  require(
      elsif.statements.front().else_statements.size() == 1,
      "nested VHDL else branch representation");
  const auto& boolean_assignment =
      architecture->processes.front().statements[1];
  require(
      boolean_assignment.kind == StatementKind::Assignment
          && boolean_assignment.value.kind == ExpressionKind::Binary
          && boolean_assignment.value.text == "and",
      "VHDL Boolean operator expression root");
  const auto contains_operator =
      [](const auto& self,
         const Expression& expression,
         const std::string_view operation) -> bool {
        if (expression.kind == ExpressionKind::Binary
            && expression.text == operation) {
          return true;
        }
        return std::any_of(
            expression.operands.begin(),
            expression.operands.end(),
            [&](const Expression& operand) {
              return self(self, operand, operation);
            });
      };
  for (const auto operation : {"nand", "nor", "xnor", "/="}) {
    require(
        contains_operator(
            contains_operator,
            boolean_assignment.value,
            operation),
        "VHDL Boolean operator node");
  }

  const auto systemverilog = parse_text(
      "conditionals.sv",
      R"(
module conditionals;
  logic [3:0] selector;
  logic result;
  initial begin
    if (selector)
      if (selector[3])
        result = 1'b1;
      else
        result = 1'b0;
    else begin
      result = 1'bx;
    end
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "nested SystemVerilog conditional statements must parse");
  const auto& sv_outer =
      systemverilog.design.units.front()
          .processes.front()
          .statements.front();
  require(
      sv_outer.kind == StatementKind::If
          && sv_outer.condition.kind == ExpressionKind::Identifier
          && sv_outer.statements.size() == 1
          && sv_outer.statements.front().kind == StatementKind::If
          && sv_outer.else_statements.size() == 1,
      "SystemVerilog dangling else binds to the nested if");
  require(
      sv_outer.statements.front().else_statements.size() == 1
          && sv_outer.else_statements.front().kind
              == StatementKind::Assignment,
      "SystemVerilog nested and outer false branches");
}

void test_conditional_generate_hierarchy() {
  const auto systemverilog = parse_text(
      "generate.sv",
      R"(
module leaf(input logic value, output logic result);
  assign result = value;
endmodule

module generated #(parameter ENABLED = 1) (
  input logic value,
  output logic result
);
  generate
    if (ENABLED) begin : selected
      leaf active(.value(value), .result(result));
      if (ENABLED) begin : nested_selected
        leaf nested_active(.value(value), .result(result));
      end else begin : nested_rejected
        leaf nested_inactive(.value(value), .result(result));
      end
    end else begin : rejected
      leaf inactive(.value(value), .result(result));
    end
  endgenerate
endmodule

module generated_loop #(parameter COUNT = 3) (
  input logic value,
  output logic result
);
  genvar i;
  generate
    for (i = 0; i < COUNT; i++) begin : lane
      logic [3:0] generated_value;
      assign generated_value = i;
      initial generated_value = i + 1;
      leaf child(.value(value), .result(result));
    end
  endgenerate
endmodule

module generated_case #(parameter MODE = 1) (
  input logic value,
  output logic result
);
  generate
    case (MODE)
      0: begin : zero
        leaf child(.value(value), .result(result));
      end
      1, 2: begin : selected
        leaf child(.value(value), .result(result));
      end
      default: begin : fallback
        leaf child(.value(value), .result(result));
      end
    endcase
  endgenerate
endmodule

module implicit_generated #(parameter ENABLED = 1);
  if (ENABLED) begin : implicit_scope
    localparam int BASE = 5;
    parameter int NEXT_VALUE = BASE + 1;
    logic [3:0] generated_value;
    initial generated_value = NEXT_VALUE;
  end
endmodule

module implicit_loop_generated #(parameter COUNT = 2);
  for (genvar i = 0; i < COUNT; i += 1) begin : lane
    logic [3:0] generated_value;
    initial generated_value = i;
  end
endmodule

module descending_loop_generated;
  generate
    for (genvar j = 2; j >= 0; --j) begin : lane
      logic [3:0] generated_value;
      initial generated_value = j;
    end
  endgenerate
endmodule

module compound_subtract_loop_generated;
  for (genvar m = 2; m >= 0; m -= 1) begin : lane
    logic generated_value;
  end
endmodule

module late_genvar_generated;
  generate
    for (k = 0; k < 1; ++k) begin : lane
      logic generated_value;
    end
  endgenerate
  genvar k;
endmodule

module implicit_case_generated #(parameter MODE = 1);
  case (MODE)
    1: begin : selected
      logic generated_value;
    end
    default: begin : fallback
      logic generated_value;
    end
  endcase
endmodule

module direct_generated;
  generate
    localparam int DIRECT_BASE = 3;
    logic [3:0] direct_value;
    initial direct_value = 4'd2;
    begin : named_scope
      localparam int NESTED_VALUE = DIRECT_BASE;
      logic [3:0] nested_value;
      initial nested_value = NESTED_VALUE;
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog conditional instance generate must parse");
  const auto* sv_unit =
      systemverilog.design.find(UnitKind::VerilogModule, "generated");
  require(
      sv_unit != nullptr
          && sv_unit->generate_regions.size() == 1,
      "SystemVerilog generate HIR count");
  const auto& sv_generate =
      sv_unit->generate_regions.front();
  require(
      sv_generate.then_scope == "selected"
          && sv_generate.else_scope == "rejected"
          && sv_generate.condition.kind
              == ExpressionKind::Identifier
          && sv_generate.condition.text == "ENABLED"
          && sv_generate.then_body.instances.size() == 1
          && sv_generate.then_body.instances.front().name == "active"
          && sv_generate.else_body.instances.size() == 1
          && sv_generate.else_body.instances.front().name == "inactive"
          && sv_generate.then_body.generate_regions.size() == 1
          && sv_generate.then_body.generate_regions.front().then_scope
              == "nested_selected"
          && sv_generate.then_body.generate_regions.front()
                 .then_body.instances
                 .front()
                 .name
              == "nested_active",
      "SystemVerilog conditional generate branches and labels");
  const auto* sv_loop_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule, "generated_loop");
  require(
      sv_loop_unit != nullptr
          && sv_loop_unit->generate_regions.size() == 1,
      "SystemVerilog loop-generate HIR count");
  const auto& sv_loop = sv_loop_unit->generate_regions.front();
  require(
      sv_loop.kind == GenerateKind::Iterative
          && sv_loop.then_scope == "lane"
          && sv_loop.variable == "i"
          && sv_loop.initial.kind == ExpressionKind::IntegerLiteral
          && sv_loop.condition.kind == ExpressionKind::Binary
          && sv_loop.condition.text == "<"
          && sv_loop.iteration.kind == ExpressionKind::Binary
          && sv_loop.iteration.text == "+"
          && sv_loop.then_body.signals.size() == 1
          && sv_loop.then_body.signals.front().name
              == "generated_value"
          && sv_loop.then_body.concurrent_statements.size() == 1
          && sv_loop.then_body.processes.size() == 1
          && sv_loop.then_body.instances.front().name == "child"
          && std::none_of(
              sv_loop_unit->signals.begin(),
              sv_loop_unit->signals.end(),
              [](const auto& signal) {
                return signal.name == "i";
              }),
      "SystemVerilog canonical genvar-for region");
  const auto* sv_descending_loop_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "descending_loop_generated");
  const auto* sv_late_genvar_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "late_genvar_generated");
  const auto* sv_compound_subtract_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "compound_subtract_loop_generated");
  require(
      sv_descending_loop_unit != nullptr
          && sv_descending_loop_unit->generate_regions.size() == 1
          && sv_descending_loop_unit->generate_regions.front()
                 .iteration.text
              == "-"
          && sv_late_genvar_unit != nullptr
          && sv_late_genvar_unit->generate_regions.size() == 1
          && sv_late_genvar_unit->generate_regions.front()
                 .iteration.text
              == "+"
          && sv_compound_subtract_unit != nullptr
          && sv_compound_subtract_unit->generate_regions.size() == 1
          && sv_compound_subtract_unit->generate_regions.front()
                 .iteration.text
              == "-",
      "SystemVerilog prefix updates and late module genvar");
  const auto* sv_case_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule, "generated_case");
  require(
      sv_case_unit != nullptr
          && sv_case_unit->generate_regions.size() == 1,
      "SystemVerilog case-generate HIR count");
  const auto& sv_case = sv_case_unit->generate_regions.front();
  require(
      sv_case.kind == GenerateKind::Selection
          && sv_case.condition.text == "MODE"
          && sv_case.alternatives.size() == 3
          && sv_case.alternatives[0].scope == "zero"
          && sv_case.alternatives[1].scope == "selected"
          && sv_case.alternatives[1].choices.size() == 2
          && sv_case.alternatives[2].scope == "fallback"
          && sv_case.alternatives[2].is_default,
      "SystemVerilog case-generate alternatives");
  const auto* sv_implicit_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule, "implicit_generated");
  require(
      sv_implicit_unit != nullptr
          && sv_implicit_unit->generate_regions.size() == 1
          && sv_implicit_unit->generate_regions.front().kind
              == GenerateKind::Conditional
          && sv_implicit_unit->generate_regions.front()
                 .then_scope
              == "implicit_scope"
          && sv_implicit_unit->generate_regions.front()
                 .then_body.constants.size()
              == 2
          && sv_implicit_unit->generate_regions.front()
                 .then_body.signals.size()
              == 1,
      "implicit SystemVerilog generate-if body");
  const auto* sv_implicit_loop_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "implicit_loop_generated");
  require(
      sv_implicit_loop_unit != nullptr
          && sv_implicit_loop_unit->generate_regions.size() == 1
          && sv_implicit_loop_unit->generate_regions.front().kind
              == GenerateKind::Iterative
          && sv_implicit_loop_unit->generate_regions.front()
                 .then_scope
              == "lane",
      "implicit SystemVerilog generate-for body");
  const auto* sv_implicit_case_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule,
          "implicit_case_generated");
  require(
      sv_implicit_case_unit != nullptr
          && sv_implicit_case_unit->generate_regions.size() == 1
          && sv_implicit_case_unit->generate_regions.front().kind
              == GenerateKind::Selection
          && sv_implicit_case_unit->generate_regions.front()
                 .alternatives.size()
              == 2,
      "implicit SystemVerilog generate-case body");
  const auto* sv_direct_unit =
      systemverilog.design.find(
          UnitKind::VerilogModule, "direct_generated");
  require(
      sv_direct_unit != nullptr
          && sv_direct_unit->generate_regions.size() == 1
          && sv_direct_unit->generate_regions[0].kind
              == GenerateKind::StaticBlock
          && sv_direct_unit->generate_regions[0]
                 .then_body.constants.size()
              == 1
          && sv_direct_unit->generate_regions[0]
                 .then_body.signals.size()
              == 1
          && sv_direct_unit->generate_regions[0]
                 .then_body.generate_regions.size()
              == 1
          && sv_direct_unit->generate_regions[0]
                 .then_body.generate_regions[0].kind
              == GenerateKind::StaticBlock
          && sv_direct_unit->generate_regions[0]
                 .then_body.generate_regions[0].then_scope
              == "named_scope"
          && sv_direct_unit->generate_regions[0]
                 .then_body.generate_regions[0]
                 .then_body.constants.size()
              == 1,
      "direct and named SystemVerilog generate blocks");

  const auto vhdl = parse_text(
      "generate.vhd",
      R"(
entity generated is
  generic (enabled : boolean := true);
  port (
    value : in std_logic;
    result : out std_logic
  );
end entity;

architecture rtl of generated is
begin
  selection: if enabled generate
    active: entity work.leaf(rtl)
      port map (value => value, result => result);
    nested: if enabled generate
      nested_active: entity work.leaf(rtl)
        port map (value => value, result => result);
    else generate
      nested_inactive: entity work.leaf(rtl)
        port map (value => value, result => result);
    end generate nested;
  else generate
    inactive: entity work.leaf(rtl)
      port map (value => value, result => result);
  end generate selection;
end architecture;

entity generated_loop is
  generic (count : natural := 3);
  port (
    value : in std_logic;
    result : out std_logic
  );
end entity;

architecture rtl of generated_loop is
begin
  lanes: for i in 2 downto 0 generate
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= i;
    worker: process(generated_value)
    begin
      result <= generated_value(0);
    end process;
    child: entity work.leaf(rtl)
      port map (value => value, result => result);
  end generate lanes;
end architecture;

entity generated_case is
  generic (mode : integer := 1);
  port (
    value : in std_logic;
    result : out std_logic
  );
end entity;

architecture rtl of generated_case is
begin
  selection: case mode generate
    zero: when 0 =>
      child: entity work.leaf(rtl)
        port map (value => value, result => result);
    selected: when 1 to 2 | 7 downto 5 =>
      child: entity work.leaf(rtl)
        port map (value => value, result => result);
    empty_choice: when 3 to 1 =>
      child: entity work.leaf(rtl)
        port map (value => value, result => result);
    fallback: when others =>
      child: entity work.leaf(rtl)
        port map (value => value, result => result);
  end generate selection;
end architecture;

entity block_generated is
  port (
    observed : out unsigned(3 downto 0)
  );
end entity;

architecture rtl of block_generated is
begin
  static_scope: block is
    constant base_value : natural := 4;
    constant generated_constant : natural := base_value + 1;
    signal generated_value : unsigned(3 downto 0);
  begin
    generated_value <= generated_constant;
    worker: process(generated_value)
    begin
      observed <= generated_value + 1;
    end process;
  end block static_scope;
end architecture;
)",
      Language::Vhdl2008);
  require(
      vhdl.ok(),
      "VHDL conditional instance generate must parse");
  const auto* vhdl_unit =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      vhdl_unit != nullptr
          && vhdl_unit->generate_regions.size() == 1,
      "VHDL generate HIR count");
  const auto& vhdl_generate =
      vhdl_unit->generate_regions.front();
  require(
      vhdl_generate.then_scope == "selection"
          && vhdl_generate.else_scope == "selection"
          && vhdl_generate.condition.text == "enabled"
          && vhdl_generate.then_body.instances.front().name == "active"
          && vhdl_generate.else_body.instances.front().name == "inactive"
          && vhdl_generate.then_body.generate_regions.size() == 1
          && vhdl_generate.then_body.generate_regions.front().then_scope
              == "nested"
          && vhdl_generate.then_body.generate_regions.front()
                 .then_body.instances
                 .front()
                 .name
              == "nested_active",
      "VHDL conditional generate branches and canonical label");
  const auto vhdl_loop_unit = std::find_if(
      vhdl.design.units.begin(),
      vhdl.design.units.end(),
      [](const auto& unit) {
        return unit.kind == UnitKind::VhdlArchitecture
            && unit.primary_name == "generated_loop";
      });
  require(
      vhdl_loop_unit != vhdl.design.units.end()
          && vhdl_loop_unit->generate_regions.size() == 1,
      "VHDL loop-generate HIR count");
  const auto& vhdl_loop =
      vhdl_loop_unit->generate_regions.front();
  require(
      vhdl_loop.kind == GenerateKind::Iterative
          && vhdl_loop.then_scope == "lanes"
          && vhdl_loop.variable == "i"
          && vhdl_loop.condition.text == ">="
          && vhdl_loop.iteration.text == "-"
          && vhdl_loop.then_body.signals.size() == 1
          && vhdl_loop.then_body.concurrent_statements.size() == 1
          && vhdl_loop.then_body.processes.size() == 1
          && vhdl_loop.then_body.instances.front().name == "child",
      "VHDL descending for-generate region");
  const auto vhdl_case_unit = std::find_if(
      vhdl.design.units.begin(),
      vhdl.design.units.end(),
      [](const auto& unit) {
        return unit.kind == UnitKind::VhdlArchitecture
            && unit.primary_name == "generated_case";
      });
  require(
      vhdl_case_unit != vhdl.design.units.end()
          && vhdl_case_unit->generate_regions.size() == 1,
      "VHDL case-generate HIR count");
  const auto& vhdl_case =
      vhdl_case_unit->generate_regions.front();
  require(
      vhdl_case.kind == GenerateKind::Selection
          && vhdl_case.condition.text == "mode"
          && vhdl_case.alternatives.size() == 4
          && vhdl_case.alternatives[0].scope == "zero"
          && vhdl_case.alternatives[1].scope == "selected"
          && vhdl_case.alternatives[1].choices.size() == 2
          && vhdl_case.alternatives[1].choices[0].right
          && !vhdl_case.alternatives[1].choices[0].descending
          && vhdl_case.alternatives[1].choices[1].right
          && vhdl_case.alternatives[1].choices[1].descending
          && vhdl_case.alternatives[2].scope == "empty_choice"
          && vhdl_case.alternatives[2].choices.front().right
          && !vhdl_case.alternatives[2].choices.front().descending
          && vhdl_case.alternatives[3].scope == "fallback"
          && vhdl_case.alternatives[3].is_default,
      "VHDL case-generate alternatives");
  const auto vhdl_block_unit = std::find_if(
      vhdl.design.units.begin(),
      vhdl.design.units.end(),
      [](const auto& unit) {
        return unit.kind == UnitKind::VhdlArchitecture
            && unit.primary_name == "block_generated";
      });
  require(
      vhdl_block_unit != vhdl.design.units.end()
          && vhdl_block_unit->generate_regions.size() == 1
          && vhdl_block_unit->generate_regions.front().kind
              == GenerateKind::StaticBlock
          && vhdl_block_unit->generate_regions.front().then_scope
              == "static_scope"
          && vhdl_block_unit->generate_regions.front()
                 .then_body.constants.size()
              == 2
          && vhdl_block_unit->generate_regions.front()
                 .then_body.signals.size()
              == 1
          && vhdl_block_unit->generate_regions.front()
                 .then_body.processes.size()
              == 1,
      "VHDL unguarded static block body");

  const auto unlabeled_systemverilog = parse_text(
      "unlabeled_generate.sv",
      R"(
module invalid #(parameter ENABLED = 1);
  generate
    if (ENABLED) begin
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !unlabeled_systemverilog.ok()
          && std::any_of(
              unlabeled_systemverilog.diagnostics.begin(),
              unlabeled_systemverilog.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-062";
              }),
      "unlabeled SystemVerilog generate branch is targeted");

  const auto unsupported_vhdl_body = parse_text(
      "unsupported_generate.vhd",
      R"(
architecture rtl of generated is
  signal q : std_logic;
begin
  selection: if true generate
    report "unsupported";
  end generate selection;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !unsupported_vhdl_body.ok()
          && std::any_of(
              unsupported_vhdl_body.diagnostics.begin(),
              unsupported_vhdl_body.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-UNSUPPORTED-020";
              }),
      "unsupported VHDL generate body is targeted");

  const auto invalid_systemverilog_loop = parse_text(
      "invalid_loop_generate.sv",
      R"(
module invalid_loop;
  generate
    for (i = 0; i < 2; i = i + 1) begin : lane
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_systemverilog_loop.ok()
          && std::any_of(
              invalid_systemverilog_loop.diagnostics.begin(),
              invalid_systemverilog_loop.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-066";
              }),
      "non-inline-genvar loop generate is targeted");

  const auto duplicate_systemverilog_genvar = parse_text(
      "duplicate_genvar.sv",
      R"(
module duplicate_genvar;
  genvar i, i;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !duplicate_systemverilog_genvar.ok()
          && std::any_of(
              duplicate_systemverilog_genvar.diagnostics.begin(),
              duplicate_systemverilog_genvar.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-022";
              }),
      "duplicate module-scope genvar is targeted");

  const auto generated_port_direction = parse_text(
      "generated_port_direction.sv",
      R"(
module invalid_generated_port;
  generate
    if (1) begin : selected
      output logic invalid_port;
    end
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !generated_port_direction.ok()
          && std::any_of(
              generated_port_direction.diagnostics.begin(),
              generated_port_direction.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-UNSUPPORTED-022";
              }),
      "generated port direction is targeted");

  const auto invalid_vhdl_loop = parse_text(
      "invalid_loop_generate.vhd",
      R"(
architecture rtl of invalid_loop is
begin
  lanes: for i in 0 generate
  end generate lanes;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl_loop.ok()
          && std::any_of(
              invalid_vhdl_loop.diagnostics.begin(),
              invalid_vhdl_loop.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-063";
              }),
      "missing VHDL generate range direction is targeted");

  const auto duplicate_systemverilog_default = parse_text(
      "duplicate_case_generate.sv",
      R"(
module duplicate_case_generate #(parameter MODE = 0);
  generate
    case (MODE)
      default: begin : first
      end
      default: begin : second
      end
    endcase
  endgenerate
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !duplicate_systemverilog_default.ok()
          && std::any_of(
              duplicate_systemverilog_default.diagnostics.begin(),
              duplicate_systemverilog_default.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-021";
              }),
      "duplicate SystemVerilog generate default is targeted");

  const auto unlabeled_vhdl_alternative = parse_text(
      "unlabeled_case_generate.vhd",
      R"(
architecture rtl of invalid_case is
begin
  selection: case 0 generate
    when 0 =>
  end generate selection;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !unlabeled_vhdl_alternative.ok()
          && std::any_of(
              unlabeled_vhdl_alternative.diagnostics.begin(),
              unlabeled_vhdl_alternative.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-PARSE-070";
              }),
      "unlabeled VHDL case-generate alternative is targeted");

  const auto nonfinal_vhdl_others = parse_text(
      "nonfinal_case_generate_others.vhd",
      R"(
architecture rtl of invalid_case is
begin
  selection: case 0 generate
    fallback: when others =>
    late: when 0 =>
  end generate selection;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !nonfinal_vhdl_others.ok()
          && std::any_of(
              nonfinal_vhdl_others.diagnostics.begin(),
              nonfinal_vhdl_others.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-018";
              }),
      "nonfinal VHDL case-generate others is targeted");

  const auto guarded_vhdl_block = parse_text(
      "guarded_block.vhd",
      R"(
architecture rtl of guarded is
begin
  guarded_scope: block (true)
  begin
  end block guarded_scope;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !guarded_vhdl_block.ok()
          && std::any_of(
              guarded_vhdl_block.diagnostics.begin(),
              guarded_vhdl_block.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-UNSUPPORTED-021";
              }),
      "guarded VHDL block is targeted");

  const auto mismatched_vhdl_block = parse_text(
      "mismatched_block.vhd",
      R"(
architecture rtl of mismatched is
begin
  opening_name: block
  begin
  end block closing_name;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !mismatched_vhdl_block.ok()
          && std::any_of(
              mismatched_vhdl_block.diagnostics.begin(),
              mismatched_vhdl_block.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-081";
              }),
      "VHDL block end-label mismatch is targeted");

  const auto invalid_generated_constants = parse_text(
      "invalid_generated_constants.vhd",
      R"(
architecture rtl of invalid_constants is
begin
  invalid_scope: block
    constant missing_value : natural;
    constant duplicate_name : natural := 1;
    signal duplicate_name : bit;
  begin
  end block invalid_scope;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_generated_constants.ok()
          && std::any_of(
              invalid_generated_constants.diagnostics.begin(),
              invalid_generated_constants.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-084";
              })
          && std::any_of(
              invalid_generated_constants.diagnostics.begin(),
              invalid_generated_constants.diagnostics.end(),
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-019";
              }),
      "invalid VHDL generated constants are targeted");
}

}  // namespace

int main() {
  try {
    require(
        PackedRange{
            std::numeric_limits<std::int64_t>::max(),
            std::numeric_limits<std::int64_t>::min(),
            true}
                .width()
            == 0,
        "unrepresentable 2^64-element range must not overflow");
    test_vhdl_vertical_slice();
    test_vhdl_instance_diagnostics();
    test_vhdl_generics();
    test_vhdl_select_and_concatenation_expressions();
    test_signed_type_and_expression_nodes();
    test_systemverilog_vertical_slice();
    test_systemverilog_preprocessor();
    test_non_ansi_verilog_ports();
    test_diagnostics_and_spans();
    test_vhdl_context_diagnostics();
    test_vhdl_package_constants();
    test_ignored_initializers_are_rejected();
    test_duplicate_declarations_are_rejected();
    test_systemverilog_timescale_context();
    test_systemverilog_compiler_directives();
    test_systemverilog_parameters();
    test_systemverilog_packages();
    test_immediate_assertions();
    test_process_variable_declarations();
    test_procedural_wait_statements();
    test_wildcard_and_always_comb_processes();
    test_systemverilog_case_statements();
    test_systemverilog_conditional_expression();
    test_systemverilog_comparison_expressions();
    test_systemverilog_arithmetic_expressions();
    test_systemverilog_select_and_concatenation_expressions();
    test_conditional_statement_trees();
    test_conditional_generate_hierarchy();
    std::cout << "frontend tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "frontend test failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
