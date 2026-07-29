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
      unit.processes.front().statements[4].else_statements.size() == 1
          && unit.processes.front().statements[4]
                 .else_statements.front().output_text
              == "preprocessed",
      "SystemVerilog macro stringification");
  require(
      std::filesystem::path{
          unit.processes.front().statements[5]
              .else_statements.front().output_text}
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

void test_systemverilog_line_directive() {
  const auto remapped = preprocess_verilog(
      SourceText{
          "physical.sv",
          R"(`line 100 "C:\\rtl\\logical.sv" 0
`define LOGICAL_TOKEN remapped_token
`line 200 "C:\\rtl\\logical\"name.sv" 2
`LOGICAL_TOKEN
`__FILE__
`__LINE__
)"},
      Language::SystemVerilog2017);
  require(remapped.ok(), "valid `line directives must preprocess");
  const auto token_named =
      [&](const std::string_view spelling) -> const Token* {
    const auto found = std::ranges::find(
        remapped.lexed.tokens, spelling, &Token::text);
    return found == remapped.lexed.tokens.end()
        ? nullptr
        : &*found;
  };
  const auto* macro_token = token_named("remapped_token");
  const auto* file_token =
      token_named("\"C:\\\\rtl\\\\logical\\\"name.sv\"");
  const auto* line_token = token_named("202");
  require(
      macro_token != nullptr
          && macro_token->span.source_name
              == R"(C:\rtl\logical"name.sv)"
          && std::filesystem::path{
                 physical_source(macro_token->span)}
                 .filename()
              == "physical.sv"
          && macro_token->span.begin.line == 200,
      "macro invocations use the active logical source and line");
  require(
      !macro_token->expansion_stack.empty()
          && macro_token->expansion_stack.front().find(
                 R"(defined at C:\rtl\logical.sv:100)")
              != std::string::npos
          && macro_token->expansion_stack.front().find(
                 R"(expanded at C:\rtl\logical"name.sv:200)")
              != std::string::npos,
      "macro ancestry retains remapped definition and invocation locations");
  require(
      file_token != nullptr
          && file_token->span.source_name
              == R"(C:\rtl\logical"name.sv)"
          && file_token->span.begin.line == 201,
      "`__FILE__ is safely re-escaped from the logical source name");
  require(
      line_token != nullptr
          && line_token->span.source_name
              == R"(C:\rtl\logical"name.sv)"
          && line_token->span.begin.line == 202,
      "`__LINE__ uses the active logical line");

  const auto inactive = preprocess_verilog(
      SourceText{
          "inactive-line.sv",
          R"(`ifdef NEVER
`line 900 "ignored.sv" 0
`endif
physical_token
)"},
      Language::SystemVerilog2017);
  const auto inactive_token = std::ranges::find(
      inactive.lexed.tokens, "physical_token", &Token::text);
  require(
      inactive.ok() && inactive_token != inactive.lexed.tokens.end()
          && inactive_token->span.source_name.find("inactive-line.sv")
              != std::string::npos
          && inactive_token->span.begin.line == 4,
      "inactive `line directives do not alter source provenance");

  const auto directory = make_test_directory("verilog-line");
  const auto parent = directory / "parent.sv";
  const auto child = directory / "child.svh";
  write_text(
      child,
      R"(`line 700 "logical-child.svh" 1
child_token
)");
  write_text(
      parent,
      R"(`line 50 "logical-parent.sv" 0
parent_before
`include "child.svh"
parent_after
)");
  const auto included = preprocess_verilog_file(
      parent, Language::SystemVerilog2017);
  const auto included_token =
      [&](const std::string_view spelling) -> const Token* {
    const auto found = std::ranges::find(
        included.lexed.tokens, spelling, &Token::text);
    return found == included.lexed.tokens.end()
        ? nullptr
        : &*found;
  };
  const auto* before = included_token("parent_before");
  const auto* child_value = included_token("child_token");
  const auto* after = included_token("parent_after");
  require(
      included.ok() && before != nullptr && child_value != nullptr
          && after != nullptr
          && before->span.source_name == "logical-parent.sv"
          && before->span.begin.line == 50
          && child_value->span.source_name == "logical-child.svh"
          && child_value->span.begin.line == 700
          && after->span.source_name == "logical-parent.sv"
          && after->span.begin.line == 52,
      "include-local `line state does not leak into its parent");

  const auto second = directory / "second.sv";
  write_text(second, "second_root_token\n");
  const auto roots = preprocess_verilog_compilation_unit(
      {parent, second}, Language::SystemVerilog2017);
  const auto second_token = std::ranges::find(
      roots.lexed.tokens, "second_root_token", &Token::text);
  require(
      roots.ok() && second_token != roots.lexed.tokens.end()
          && second_token->span.source_name
              == std::filesystem::weakly_canonical(second).generic_string()
          && second_token->span.begin.line == 1,
      "`line state resets at each compilation-unit root");

  const auto malformed = preprocess_verilog(
      SourceText{
          "malformed-line.sv",
          R"(`ifdef NEVER
`line 0 broken 9 extra
`endif
`line
`line 0 "zero.sv" 0
`line 4294967296 "overflow.sv" 0
`line 1 not_a_string 0
`line 1 "bad\q.sv" 0
`line 1 "bad-level.sv" 3
)"},
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        malformed.lexed.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !malformed.ok() && has_code("FSIM-SV-PP-039")
          && has_code("FSIM-SV-PP-040")
          && has_code("FSIM-SV-PP-041")
          && has_code("FSIM-SV-PP-042")
          && has_code("FSIM-SV-PP-043"),
      "malformed `line fields receive targeted diagnostics");
  require(
      std::ranges::none_of(
          malformed.lexed.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.span.source_name == "ignored.sv";
          }),
      "malformed inactive `line directives are ignored");

  auto parser_error = preprocess_verilog(
      SourceText{
          "parser-physical.sv",
          "`line 900 \"parser-logical.sv\" 0\nmodule broken(;\n"},
      Language::SystemVerilog2017);
  const auto parsed_error =
      parse_verilog(std::move(parser_error.lexed), true);
  require(
      !parsed_error.ok()
          && std::ranges::any_of(
              parsed_error.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.span.source_name
                        == "parser-logical.sv"
                    && diagnostic.span.begin.line == 900;
              }),
      "parser diagnostics retain logical `line provenance");

  const auto lexical_error = preprocess_verilog(
      SourceText{
          "lexer-physical.sv",
          "`line 300 \"lexer-logical.sv\" 0\n\"unterminated\n"},
      Language::SystemVerilog2017);
  require(
      !lexical_error.ok()
          && std::ranges::any_of(
              lexical_error.lexed.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.span.source_name
                        == "lexer-logical.sv"
                    && diagnostic.span.begin.line == 300;
              }),
      "lexer diagnostics after `line are remapped");

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

void test_vhdl_falling_edge_guard() {
  const auto result = parse_text(
      "falling_edge.vhd",
      R"(
entity falling_edge is
  port (clk : in std_logic; hit : out std_logic);
end entity;

architecture rtl of falling_edge is
begin
  capture: process(clk)
  begin
    if falling_edge(clk) then
      hit <= '1';
    end if;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(result.ok(), "VHDL falling_edge guard must parse");
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture && architecture->processes.size() == 1,
      "VHDL falling_edge process");
  const auto& process = architecture->processes.front();
  require(
      process.sensitivities.size() == 1
          && process.sensitivities.front().signal == "clk"
          && process.sensitivities.front().edge
              == EdgeKind::Negative,
      "sole VHDL falling_edge guard must refine static sensitivity");
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

void test_vhdl_record_types() {
  const auto parsed = parse_text(
      "record_types.vhd",
      R"(
entity record_types is
end entity;

architecture rtl of record_types is
  type Packet_T is record
    Data, Shadow : std_logic_vector(7 downto 0);
    Valid : boolean;
    Parity : bit;
  end record packet_t;
  signal source, result : PACKET_T;
begin
  copy_fields : process
    variable Local_Value : packet_t;
  begin
    local_value.data := source.DATA;
    local_value.shadow(3 downto 0) :=
      source.shadow(7 downto 4);
    result.data <= local_value.data;
    result.valid <= local_value.valid;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "bounded VHDL record declarations must parse");
  require(
      parsed.design.units.size() == 2,
      "record fixture has an entity and architecture");
  const auto& architecture = parsed.design.units.back();
  require(
      architecture.type_aliases.size() == 1,
      "architecture retains its record type declaration");
  const auto& alias = architecture.type_aliases.front();
  require(
      alias.name == "packet_t"
          && alias.type.packed_aggregate
              == PackedAggregateKind::Struct
          && alias.type.domain == ValueDomain::Logic9
          && alias.type.width() == 18
          && alias.type.packed_members.size() == 4,
      "record type retains flattened width, domain, and elements");
  require(
      alias.type.packed_members[0].name == "data"
          && alias.type.packed_members[0].lsb_offset == 10
          && alias.type.packed_members[1].name == "shadow"
          && alias.type.packed_members[1].lsb_offset == 2
          && alias.type.packed_members[2].name == "valid"
          && alias.type.packed_members[2].lsb_offset == 1
          && alias.type.packed_members[3].name == "parity"
          && alias.type.packed_members[3].lsb_offset == 0,
      "record elements use declaration-order flattened offsets");
  require(
      architecture.signals.size() == 2
          && architecture.signals[0].type.named_type == "packet_t"
          && architecture.processes.size() == 1
          && architecture.processes.front().variables.size() == 1
          && architecture.processes.front()
                 .variables.front().type.named_type
              == "packet_t",
      "record-typed signals and variables retain the local named type");
  const auto& statements =
      architecture.processes.front().statements;
  require(
      statements.size() == 5
          && statements[0].target.text == "local_value.data"
          && statements[0].value.text == "source.data"
          && statements[1].target.kind == ExpressionKind::Slice
          && statements[1].target.operands.front().text
              == "local_value.shadow"
          && statements[1].value.kind == ExpressionKind::Slice
          && statements[1].value.operands.front().text
              == "source.shadow"
          && statements[2].target.text == "result.data"
          && statements[3].target.text == "result.valid",
      "record element selections are canonical in reads and writes");

  const auto packaged = parse_text(
      "package_records.vhd",
      R"(
package Packet_Types is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
end package;

use work.packet_types.all;
entity Packet_Endpoint is
  port (
    Source : in packet_t;
    Result : out work.packet_types.packet_t
  );
end entity;

use work.packet_types.all;
architecture rtl of packet_endpoint is
  signal Direct_Value : packet_types.packet_t;
begin
  copy_value : process(source)
    variable Local_Value : PACKET_T;
  begin
    local_value := source;
    direct_value <= local_value;
    result <= direct_value;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      packaged.ok(),
      "package records and consumer named-type references must parse");
  require(
      packaged.design.units.size() == 3
          && packaged.design.units[0].kind
              == UnitKind::VhdlPackage
          && packaged.design.units[0].type_aliases.size() == 1
          && packaged.design.units[0].type_aliases.front().name
              == "packet_t"
          && packaged.design.units[0]
                 .type_aliases.front().type.width()
              == 5,
      "a package retains its bounded record declaration");
  const auto& packaged_entity = packaged.design.units[1];
  const auto& packaged_architecture = packaged.design.units[2];
  require(
      packaged_entity.ports.size() == 2
          && packaged_entity.ports[0].type.named_type
              == "packet_t"
          && packaged_entity.ports[1].type.named_type
              == "work.packet_types.packet_t"
          && packaged_architecture.signals.size() == 1
          && packaged_architecture.signals[0].type.named_type
              == "packet_types.packet_t"
          && packaged_architecture.processes[0]
                 .variables[0].type.named_type
              == "packet_t",
      "package record references retain bare and selected names for semantics");

  const auto aggregates = parse_text(
      "record_aggregates.vhd",
      R"(
entity record_aggregates is
end entity;

architecture rtl of record_aggregates is
  type Packet_T is record
    Data : std_logic_vector(3 downto 0);
    Valid : boolean;
  end record Packet_T;
  signal Result : packet_t;
  signal Grouped : boolean;
begin
  aggregate_forms : process
    variable Named_Value : packet_t :=
      (Valid => true, Data => "ULH-");
    variable Positional_Value : packet_t :=
      ("10Z-", false);
  begin
    named_value := (Data => "01LH", others => false);
    positional_value := ("ZZZZ", true);
    result <= (Data => "1010", Valid => true);
    grouped <= (true);
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      aggregates.ok(),
      "VHDL positional, named, and others record aggregates must parse");
  const auto& aggregate_process =
      aggregates.design.units.back().processes.front();
  require(
      aggregate_process.variables.size() == 2
          && aggregate_process.variables[0].initializer
          && aggregate_process.variables[1].initializer,
      "record aggregate variable initializers are retained");
  const auto& named_initializer =
      *aggregate_process.variables[0].initializer;
  const auto& positional_initializer =
      *aggregate_process.variables[1].initializer;
  require(
      named_initializer.kind == ExpressionKind::Aggregate
          && named_initializer.operands.size() == 2
          && named_initializer.aggregate_choices
              == std::vector<std::string>({"valid", "data"})
          && named_initializer.operands[0].kind
              == ExpressionKind::BooleanLiteral
          && named_initializer.operands[1].kind
              == ExpressionKind::StringLiteral,
      "named record aggregate associations preserve canonical choices and "
      "source order");
  require(
      positional_initializer.kind == ExpressionKind::Aggregate
          && positional_initializer.operands.size() == 2
          && positional_initializer.aggregate_choices
              == std::vector<std::string>({"", ""}),
      "positional record aggregate associations remain distinct from named "
      "choices");
  const auto& aggregate_statements = aggregate_process.statements;
  require(
      aggregate_statements.size() == 5
          && aggregate_statements[0].value.kind
              == ExpressionKind::Aggregate
          && aggregate_statements[0].value.aggregate_choices
              == std::vector<std::string>({"data", "others"})
          && aggregate_statements[1].value.kind
              == ExpressionKind::Aggregate
          && aggregate_statements[2].value.kind
              == ExpressionKind::Aggregate
          && aggregate_statements[3].value.kind
              == ExpressionKind::BooleanLiteral,
      "aggregate assignments retain choices while single parentheses remain "
      "ordinary grouping");

  const auto invalid_aggregates = parse_text(
      "invalid_record_aggregates.vhd",
      R"(
entity invalid_record_aggregates is
end entity;
architecture rtl of invalid_record_aggregates is
  type Packet_T is record
    Data : bit_vector(1 downto 0);
    Valid : boolean;
  end record Packet_T;
  signal Result : packet_t;
begin
  invalid_forms : process
  begin
    result <= (Data => "00", false);
    result <= (others => false, Data => "00");
    result <= (others => false, others => true);
    result <= (true => false, Data => "00");
    result <= (Data => , Valid => true);
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  const auto has_aggregate_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid_aggregates.diagnostics,
            [&](const auto& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      !invalid_aggregates.ok()
          && has_aggregate_code("FSIM-VHDL-PARSE-132")
          && has_aggregate_code("FSIM-VHDL-PARSE-133")
          && has_aggregate_code("FSIM-VHDL-SEM-038")
          && has_aggregate_code("FSIM-VHDL-SEM-039"),
      "record aggregate association syntax failures have stable targeted "
      "diagnostics");

  const auto invalid = parse_text(
      "invalid_records.vhd",
      R"(
entity invalid_records is
end entity;
architecture rtl of invalid_records is
  type empty_t is record
  end record empty_t;
  type duplicate_t is record
    Item, ITEM : bit;
  end record wrong_name;
  type duplicate_t is record
    nested : empty_t;
  end record duplicate_t;
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
      has_code("FSIM-VHDL-PARSE-130")
          && has_code("FSIM-VHDL-SEM-035")
          && has_code("FSIM-VHDL-SEM-036")
          && has_code("FSIM-VHDL-SEM-037")
          && has_code("FSIM-VHDL-UNSUPPORTED-026"),
      "record declaration failures have targeted stable diagnostics");
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
    modulo : out signed(7 downto 0);
    shifted_left : out signed(7 downto 0);
    shifted_right : out signed(7 downto 0);
    shifted_arithmetic : out signed(7 downto 0);
    shifted_arithmetic_left : out signed(7 downto 0);
    rotated_left : out signed(7 downto 0);
    rotated_right : out signed(7 downto 0);
    absolute : out signed(7 downto 0)
  );
end entity;
architecture rtl of signed_ops is
begin
  quotient <= lhs / rhs;
  remainder <= lhs rem rhs;
  modulo <= lhs mod rhs;
  shifted_left <= lhs sll 1;
  shifted_right <= lhs srl 1;
  shifted_arithmetic <= lhs sra 1;
  shifted_arithmetic_left <= lhs sla 1;
  rotated_left <= lhs rol 1;
  rotated_right <= lhs ror 1;
  absolute <= abs lhs;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "VHDL signed arithmetic source must parse");
  const auto* entity =
      vhdl.design.find(UnitKind::VhdlEntity, "signed_ops");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      entity != nullptr && entity->ports.size() == 12
          && std::ranges::all_of(
              entity->ports,
              [](const SignalDeclaration& port) {
                return port.type.is_signed;
              }),
      "VHDL signed subtype metadata");
  require(
      architecture != nullptr
          && architecture->concurrent_statements.size() == 10
          && architecture->concurrent_statements[0].value.text == "/"
          && architecture->concurrent_statements[1].value.text
              == "rem"
          && architecture->concurrent_statements[2].value.text
              == "mod"
          && architecture->concurrent_statements[3].value.text
              == "sll"
          && architecture->concurrent_statements[4].value.text
              == "srl"
          && architecture->concurrent_statements[5].value.text
              == "sra"
          && architecture->concurrent_statements[6].value.text
              == "sla"
          && architecture->concurrent_statements[7].value.text
              == "rol"
          && architecture->concurrent_statements[8].value.text
              == "ror"
          && architecture->concurrent_statements[9].value.kind
              == ExpressionKind::Unary
          && architecture->concurrent_statements[9].value.text
              == "abs",
      "VHDL signed arithmetic, shift, rotate, and absolute expression nodes");

  const auto systemverilog = parse_text(
      "signed_ops.sv",
      R"(
module signed_ops;
  logic signed [7:0] lhs;
  logic signed [7:0] rhs;
  logic unsigned [7:0] mixed;
  logic signed [7:0] quotient;
  logic comparison;
  logic cast_comparison;
  logic [7:0] cast_shift;
  logic unknown_present;
  logic [31:0] packed_width;
  logic signed [31:0] left_bound;
  logic signed [31:0] right_bound;
  logic signed [31:0] low_bound;
  logic signed [31:0] high_bound;
  logic signed [31:0] packed_size;
  logic signed [31:0] packed_increment;
  logic signed [31:0] dimensions;
  logic signed [31:0] unpacked_dimensions;
  logic one_hot;
  logic one_hot_or_zero;
  logic signed [31:0] one_count;
  logic signed [31:0] selected_count;
  always_comb begin
    quotient = lhs / rhs;
    comparison = lhs < rhs;
    cast_comparison = $signed(mixed) < rhs;
    cast_shift = $unsigned(lhs) >>> 1;
    unknown_present = $isunknown(mixed);
    packed_width = $bits({lhs, mixed});
    left_bound = $left(mixed);
    right_bound = $right(mixed);
    low_bound = $low(mixed);
    high_bound = $high(mixed);
    packed_size = $size(mixed, 1);
    packed_increment = $increment(mixed);
    dimensions = $dimensions(mixed);
    unpacked_dimensions = $unpacked_dimensions(mixed);
    one_hot = $onehot(mixed);
    one_hot_or_zero = $onehot0(mixed);
    one_count = $countones(mixed);
    selected_count = $countbits(mixed, 1'b0, 1'bx);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog signed arithmetic source must parse");
  const auto& unit = systemverilog.design.units.front();
  require(
      unit.signals.size() == 21
          && unit.signals[0].type.is_signed
          && unit.signals[1].type.is_signed
          && !unit.signals[2].type.is_signed,
      "SystemVerilog explicit signedness metadata");
  require(
      unit.processes.size() == 1
          && unit.processes.front().statements.size() == 18
          && unit.processes.front().statements[0].value.text == "/"
          && unit.processes.front().statements[1].value.text == "<"
          && unit.processes.front().statements[2].value.text == "<"
          && unit.processes.front().statements[2]
                 .value.operands.front().kind
              == ExpressionKind::Call
          && unit.processes.front().statements[2]
                 .value.operands.front().text
              == "$signed"
          && unit.processes.front().statements[3].value.text == ">>>"
          && unit.processes.front().statements[3]
                 .value.operands.front().text
              == "$unsigned"
          && unit.processes.front().statements[4].value.kind
              == ExpressionKind::Call
          && unit.processes.front().statements[4].value.text
              == "$isunknown"
          && unit.processes.front().statements[5].value.kind
              == ExpressionKind::Call
          && unit.processes.front().statements[5].value.text
              == "$bits"
          && unit.processes.front().statements[5]
                 .value.operands.front().kind
              == ExpressionKind::Concatenation
          && unit.processes.front().statements[6].value.text == "$left"
          && unit.processes.front().statements[7].value.text == "$right"
          && unit.processes.front().statements[8].value.text == "$low"
          && unit.processes.front().statements[9].value.text == "$high"
          && unit.processes.front().statements[10].value.text == "$size"
          && unit.processes.front().statements[10]
                 .value.operands.size()
              == 2
          && unit.processes.front().statements[11].value.text
              == "$increment"
          && unit.processes.front().statements[12].value.text
              == "$dimensions"
          && unit.processes.front().statements[13].value.text
              == "$unpacked_dimensions"
          && unit.processes.front().statements[14].value.text == "$onehot"
          && unit.processes.front().statements[15].value.text == "$onehot0"
          && unit.processes.front().statements[16].value.text
              == "$countones"
          && unit.processes.front().statements[17].value.text
              == "$countbits"
          && unit.processes.front().statements[17]
                 .value.operands.size()
              == 3,
      "SystemVerilog signed arithmetic, casts, and system-function nodes");
}

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

void test_vhdl_array_attributes() {
  const auto result = parse_text(
      "array_attributes.vhd",
      R"(
entity array_attributes is
end entity;

architecture rtl of array_attributes is
  signal descending : std_logic_vector(7 downto 4);
  signal result : signed(31 downto 0);
  signal direction : boolean;
begin
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
    direction <= descending'stable;
    direction <= descending'active;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(result.ok(), "bounded VHDL array attributes must parse");
  const auto& statements =
      result.design.units.back().processes.front().statements;
  const std::array<std::string_view, 11> attributes{
      "'left", "'right", "'low", "'high", "'length",
      "'ascending", "'event", "'last_value", "'last_event",
      "'stable", "'active"};
  require(statements.size() == 12, "VHDL attribute statement count");
  for (std::size_t index = 0; index < attributes.size(); ++index) {
    require(
        statements[index].value.kind == ExpressionKind::Call
            && statements[index].value.text == attributes[index]
            && statements[index].value.operands.front().text
                == "descending",
        "VHDL attribute call HIR");
  }
  require(
      statements[1].value.operands.size() == 2
          && statements[1].value.operands[1].text == "1",
      "VHDL attribute optional dimension");

  const auto unsupported = parse_text(
      "unsupported_attribute.vhd",
      R"(
architecture rtl of unsupported_attribute is
  signal value : bit_vector(3 downto 0);
  signal result : signed(31 downto 0);
begin
  result <= value'instance_name;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !unsupported.ok()
          && std::ranges::any_of(
              unsupported.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-030";
              }),
      "unsupported VHDL attributes must be targeted");
}

void test_vhdl_selected_assignments() {
  const auto result = parse_text(
      "selected_assignment.vhd",
      R"(
entity selected_assignment is
end entity;

architecture rtl of selected_assignment is
  signal selector : std_logic_vector(1 downto 0);
  signal a : std_logic_vector(3 downto 0);
  signal b : std_logic_vector(3 downto 0);
  signal result : std_logic_vector(3 downto 0);
begin
  choose: with selector select
    result <=
      a after 2 ns when "00" | "01",
      b when others;
end architecture;
)",
      Language::Vhdl2008);
  require(result.ok(), "VHDL selected signal assignments must parse");
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture
          && architecture->concurrent_statements.size() == 1,
      "a selected signal assignment must be retained as one statement");
  const auto& selected =
      architecture->concurrent_statements.front();
  require(
      selected.kind == StatementKind::Case
          && selected.label == "choose"
          && selected.condition.text == "selector"
          && selected.case_alternatives.size() == 2
          && selected.case_alternatives[0].choices.size() == 2
          && selected.case_alternatives[0].statements.size() == 1
          && selected.case_alternatives[0]
                 .statements.front().delay
                 .has_value()
          && selected.case_alternatives[0]
                 .statements.front().delay->magnitude
              == 2
          && selected.case_alternatives[1].is_default,
      "selected assignment choices, waveform delay, default, and label");

  const auto missing_others = parse_text(
      "selected_missing_others.vhd",
      R"(
entity selected_missing_others is
end entity;
architecture rtl of selected_missing_others is
  signal selector : std_logic;
  signal result : std_logic;
begin
  with selector select result <= '0' when '0';
end architecture;
)",
      Language::Vhdl2008);
  require(
      !missing_others.ok()
          && std::ranges::any_of(
              missing_others.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-029";
              }),
      "bounded selected assignments without others must be diagnosed");

  const auto invalid_others = parse_text(
      "selected_invalid_others.vhd",
      R"(
entity selected_invalid_others is
end entity;
architecture rtl of selected_invalid_others is
  signal selector : std_logic;
  signal result : std_logic;
begin
  with selector select
    result <= '0' when others, '1' when '1', '0' when others;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_others.ok()
          && std::ranges::any_of(
              invalid_others.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-027";
              })
          && std::ranges::any_of(
              invalid_others.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-028";
              }),
      "duplicate and nonfinal selected-assignment others alternatives "
      "must be diagnosed");
}

void test_vhdl_delay_mechanisms() {
  const auto parsed = parse_text(
      "delay_mechanisms.vhd",
      R"(
entity delay_mechanisms is
end entity;

architecture rtl of delay_mechanisms is
  signal selector : std_logic;
  signal source : std_logic;
  signal implicit_value : std_logic;
  signal inertial_value : std_logic;
  signal transport_value : std_logic;
  signal rejected_value : std_logic;
  signal selected_value : std_logic;
begin
  implicit_value <= source after 5 ns;
  inertial_value <= inertial source after 5 ns;
  transport_value <= transport source after 5 ns;
  rejected_value <= reject 2 ns inertial source after 5 ns;
  with selector select
    selected_value <= reject 1 ns inertial
      source after 3 ns when '1',
      '0' after 3 ns when others;

  sequential: process(source)
    variable local_value : std_logic;
  begin
    local_value := source;
    transport_value <= transport source after 4 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "VHDL delay mechanisms must parse");
  const auto* architecture =
      parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture
          && architecture->concurrent_statements.size() == 5
          && architecture->processes.size() == 1,
      "VHDL delay mechanism statement contexts");
  const auto& implicit_value =
      architecture->concurrent_statements[0];
  const auto& inertial_value =
      architecture->concurrent_statements[1];
  const auto& transport_value =
      architecture->concurrent_statements[2];
  const auto& rejected_value =
      architecture->concurrent_statements[3];
  require(
      implicit_value.vhdl_delay_mechanism
              == VhdlDelayMechanism::ImplicitInertial
          && inertial_value.vhdl_delay_mechanism
              == VhdlDelayMechanism::Inertial
          && transport_value.vhdl_delay_mechanism
              == VhdlDelayMechanism::Transport
          && rejected_value.vhdl_rejection_limit
          && rejected_value.vhdl_rejection_limit->magnitude == 2
          && rejected_value.vhdl_rejection_limit->unit == "ns",
      "simple VHDL delay mechanism HIR");
  const auto& alternatives =
      architecture->concurrent_statements[4].case_alternatives;
  require(
      alternatives.size() == 2
          && alternatives[0].statements[0].vhdl_delay_mechanism
              == VhdlDelayMechanism::Inertial
          && alternatives[1].statements[0].vhdl_rejection_limit
          && alternatives[1]
                 .statements[0]
                 .vhdl_rejection_limit->magnitude
              == 1,
      "a selected assignment propagates its common delay mechanism");
  const auto& sequential =
      architecture->processes.front().statements;
  require(
      sequential.size() == 2
          && !sequential[0].vhdl_delay_mechanism
          && sequential[1].vhdl_delay_mechanism
              == VhdlDelayMechanism::Transport,
      "VHDL variable assignments stay distinct from signal mechanisms");

  const auto malformed = parse_text(
      "bad_delay_mechanisms.vhd",
      R"(
architecture rtl of bad_delay_mechanisms is
  signal source : std_logic;
  signal result : std_logic;
begin
  result <= reject -1 ns transport source after 5 ns;
  result <= source transport after 5 ns;
end architecture;
)",
      Language::Vhdl2008);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        malformed.diagnostics,
        [code](const auto& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !malformed.ok()
          && has_code("FSIM-VHDL-SEM-031")
          && has_code("FSIM-VHDL-PARSE-123")
          && has_code("FSIM-VHDL-PARSE-124"),
      "malformed and misplaced VHDL delay mechanisms are targeted");
}

void test_vhdl_ordered_waveforms() {
  const auto parsed = parse_text(
      "ordered_waveforms.vhd",
      R"(
architecture rtl of ordered_waveforms is
  signal choose : boolean;
  signal selector : std_logic;
  signal source : std_logic_vector(1 downto 0);
  signal result : std_logic_vector(1 downto 0);
begin
  result <= transport "00" after 1 ns, "11" after 4 ns;
  result <= "01" after 2 ns, "10" after 6 ns
            when choose else unaffected;
  with selector select
    result <= "00" after 1 ns, "11" after 3 ns when '0',
              unaffected when others;
  process
  begin
    result(1 downto 0) <=
        inertial "10" after 2 ns, "01" after 5 ns;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "ordered VHDL waveforms must parse");
  const auto& unit = parsed.design.units.front();
  require(
      unit.concurrent_statements.size() == 3
          && unit.processes.size() == 1,
      "ordered waveform statement contexts");
  const auto& simple = unit.concurrent_statements[0];
  require(
      simple.kind == StatementKind::Assignment
          && simple.vhdl_waveform.size() == 2
          && simple.vhdl_waveform[0].delay
          && simple.vhdl_waveform[0].delay->magnitude == 1
          && simple.vhdl_waveform[1].delay
          && simple.vhdl_waveform[1].delay->magnitude == 4,
      "simple ordered waveform HIR");
  const auto& conditional = unit.concurrent_statements[1];
  require(
      conditional.kind == StatementKind::If
          && conditional.statements[0].vhdl_waveform.size() == 2
          && conditional.else_statements[0].vhdl_unaffected,
      "conditional alternatives retain independent waveform state");
  const auto& selected = unit.concurrent_statements[2];
  require(
      selected.kind == StatementKind::Case
          && selected.case_alternatives.size() == 2
          && selected.case_alternatives[0]
                 .statements[0]
                 .vhdl_waveform.size()
              == 2
          && selected.case_alternatives[1]
                 .statements[0]
                 .vhdl_unaffected,
      "selected alternatives retain waveform or unaffected");
  require(
      unit.processes[0].statements[0].vhdl_waveform.size() == 2
          && unit.processes[0]
                 .statements[0]
                 .target.kind
              == ExpressionKind::Slice,
      "sequential slice waveform HIR");

  const auto malformed = parse_text(
      "malformed_waveforms.vhd",
      R"(
architecture rtl of malformed_waveforms is
  signal result : std_logic;
begin
  result <= unaffected, '1' after 1 ns;
  result <= ;
  result <= null after 1 ns;
end architecture;
)",
      Language::Vhdl2008);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        malformed.diagnostics,
        [code](const auto& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !malformed.ok()
          && has_code("FSIM-VHDL-PARSE-125")
          && has_code("FSIM-VHDL-PARSE-126")
          && has_code("FSIM-VHDL-UNSUPPORTED-025"),
      "malformed, empty, and null VHDL waveforms are targeted (mixed="
          + std::to_string(has_code("FSIM-VHDL-PARSE-125"))
          + ", empty="
          + std::to_string(has_code("FSIM-VHDL-PARSE-126"))
          + ", null="
          + std::to_string(has_code("FSIM-VHDL-UNSUPPORTED-025"))
          + ")");
}

void test_vhdl_case_statements() {
  const auto result = parse_text(
      "case_statement.vhd",
      R"(
entity case_statement is
end entity;
architecture rtl of case_statement is
  signal selector : std_logic_vector(1 downto 0);
  signal result : std_logic;
begin
  choose: process(selector)
  begin
    case selector is
      when "00" | "01" =>
        result <= '0';
      when others =>
        result <= '1';
    end case;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(result.ok(), "VHDL sequential case statement must parse");
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 1
          && architecture->processes.front().statements[0].kind
              == StatementKind::Case
          && architecture->processes.front()
                 .statements[0]
                 .case_alternatives.size()
              == 2
          && architecture->processes.front()
                 .statements[0]
                 .case_alternatives[0]
                 .choices.size()
              == 2
          && architecture->processes.front()
                 .statements[0]
                 .case_alternatives[1]
                 .is_default,
      "VHDL case alternatives and choices are retained");

  const auto duplicate_others = parse_text(
      "duplicate_others.vhd",
      R"(
architecture rtl of duplicate_others is
begin
  choose: process
  begin
    case "00" is
      when others => null;
      when others => null;
    end case;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !duplicate_others.ok()
          && std::ranges::any_of(
              duplicate_others.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-021";
              })
          && std::ranges::any_of(
              duplicate_others.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-022";
              }),
      "duplicate and nonfinal VHDL case others alternatives are targeted");
}

void test_vhdl_sequential_for_loops() {
  const auto result = parse_text(
      "sequential_for.vhd",
      R"(
entity sequential_for is
end entity;
architecture rtl of sequential_for is
  signal ascending : std_logic_vector(2 downto 0);
  signal descending : std_logic_vector(3 downto 1);
begin
  populate: process
  begin
    for lane in 0 to 2 loop
      ascending(lane) <= '1';
    end loop;
    for lane in 3 downto 1 loop
      descending(lane) <= '0';
    end loop;
    while false loop
      null;
    end loop;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(result.ok(), "VHDL sequential for loops must parse");
  const auto* architecture =
      result.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 4,
      "VHDL sequential for-loop process");
  const auto& ascending =
      architecture->processes.front().statements[0];
  const auto& descending =
      architecture->processes.front().statements[1];
  require(
      ascending.kind == StatementKind::Loop
          && ascending.loop_variable == "lane"
          && !ascending.loop_descending
          && ascending.loop_initial.text == "0"
          && ascending.loop_limit.text == "2"
          && ascending.statements.size() == 1
          && descending.kind == StatementKind::Loop
          && descending.loop_descending
          && descending.loop_initial.text == "3"
          && descending.loop_limit.text == "1"
          && architecture->processes.front()
                 .statements[2]
                 .kind == StatementKind::Loop
          && architecture->processes.front()
                 .statements[2]
                 .loop_runtime
          && architecture->processes.front()
                 .statements[2]
                 .condition.kind
              == ExpressionKind::BooleanLiteral,
      "VHDL sequential for-loop ranges and bodies are retained");

  const auto labeled_end = parse_text(
      "labeled_sequential_for.vhd",
      R"(
architecture rtl of labeled_sequential_for is
begin
  populate: process
  begin
    populate_loop: for lane in 0 to 1 loop
      null;
    end loop populate_loop;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      labeled_end.ok()
          && labeled_end.design.units.back()
                 .processes.front()
                 .statements.front()
                 .loop_label
              == "populate_loop",
      "matching opening and end labels are retained on VHDL loops");
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

void test_systemverilog_time_declarations() {
  const auto parsed = parse_text(
      "time-declarations.sv",
      R"(timeunit 10ns;
timeprecision 1ps;
module inherited_time;
  logic marker;
  initial begin
    #1.25 marker = 1'b1;
    #(2.5ps) marker = 1'b0;
  end
endmodule
module local_time;
  timeunit 1us / 10ns;
  logic marker;
  initial begin
    #1.25e-1 marker = 1'b1;
  end
endmodule
module local_precision;
  timeprecision 100ps;
  initial #1 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "SystemVerilog time declarations must parse");
  require(
      parsed.design.units.size() == 3,
      "time declaration module count");
  const auto& inherited = parsed.design.units[0];
  require(
      inherited.time_unit == "10ns"
          && inherited.time_precision == "1ps",
      "compilation-unit time declarations attach to a module");
  const auto& inherited_delays =
      inherited.processes.front().statements;
  require(
      inherited_delays.size() == 2
          && inherited_delays[0].delay
          && inherited_delays[0].delay->magnitude == 50
          && inherited_delays[0].delay->divisor == 4
          && inherited_delays[0].delay->unit == "ns",
      "fractional delay retains an exact rational under inherited timeunit");
  require(
      inherited_delays[1].delay
          && inherited_delays[1].delay->magnitude == 5
          && inherited_delays[1].delay->divisor == 2
          && inherited_delays[1].delay->unit == "ps",
      "an explicit time-literal suffix overrides the module timeunit");

  const auto& local = parsed.design.units[1];
  require(
      local.time_unit == "1us"
          && local.time_precision == "10ns",
      "combined module-local timeunit/timeprecision overrides inheritance");
  const auto& local_delay =
      *local.processes.front().statements.front().delay;
  require(
      local_delay.magnitude == 1
          && local_delay.divisor == 8
          && local_delay.unit == "us",
      "scientific fractional delays remain exact in typed HIR");
  const auto& local_precision = parsed.design.units[2];
  require(
      local_precision.time_unit == "10ns"
          && local_precision.time_precision == "100ps",
      "a module-local timeprecision overrides inherited precision only");

  const auto directive_precedence = parse_text(
      "time-directive-precedence.sv",
      R"(`timescale 100ns/10ns
timeunit 1ns / 1ps;
`resetall
module declared_time_wins;
  initial #1 $finish;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      directive_precedence.ok()
          && directive_precedence.design.units.front().time_unit == "1ns"
          && directive_precedence.design.units.front().time_precision
              == "1ps"
          && directive_precedence.design.units.front()
                 .processes.front().statements.front().delay->magnitude
              == 1
          && directive_precedence.design.units.front()
                 .processes.front().statements.front().delay->unit
              == "ns",
      "declarations override directive context and survive `resetall");

  const auto malformed = parse_text(
      "bad-time-declarations.sv",
      R"(timeunit 2ns;
timeunit 1ns;
timeunit 10ns;
module bad;
  timeprecision 100ns;
  timeprecision 1ps;
  logic marker;
  timeunit 1ns;
  initial #0.00000000000000000001 marker = 1'b1;
endmodule
timeprecision 1ps;
)",
      Language::SystemVerilog2017);
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            malformed.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      !malformed.ok()
          && has_code("FSIM-SV-SEM-045")
          && has_code("FSIM-SV-SEM-046")
          && has_code("FSIM-SV-SEM-047")
          && has_code("FSIM-SV-SEM-048")
          && has_code("FSIM-SV-SEM-049"),
      "invalid, duplicate, late, coarse, and overflowing "
      "fractional time forms receive targeted diagnostics");

  const auto unitless = parse_text(
      "unitless-fractional.sv",
      "module bad; initial #1.5 $finish; endmodule\n",
      Language::SystemVerilog2017);
  require(
      !unitless.ok()
          && std::ranges::any_of(
              unitless.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-050";
              }),
      "a unitless fractional delay without time context is rejected");

  const auto verilog = parse_text(
      "verilog-timeunit.v",
      "timeunit 1ns; module bad; endmodule\n",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && std::ranges::any_of(
              verilog.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-044";
              }),
      "time declarations are rejected in Verilog-2005");

  const auto verilog_suffix = parse_text(
      "verilog-time-suffix.v",
      "module bad; initial #1ns $finish; endmodule\n",
      Language::Verilog2005);
  require(
      !verilog_suffix.ok()
          && std::ranges::any_of(
              verilog_suffix.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-051";
              }),
      "explicit delay-unit suffixes are rejected in Verilog-2005");

  const auto verilog_identifier = parse_text(
      "verilog-timeunit-identifier.v",
      R"(module timeunit;
endmodule
module top;
  timeunit child();
endmodule
)",
      Language::Verilog2005);
  require(
      verilog_identifier.ok()
          && verilog_identifier.design.units[1].instances.size() == 1,
      "timeunit remains an ordinary Verilog-2005 identifier when its "
      "syntax is not a declaration");
}

void test_systemverilog_delay_triples() {
  const auto parsed = parse_text(
      "delay-triples.sv",
      R"(timeunit 1ns / 1ps;
module delay_triples;
  logic source;
  logic continuous_result;
  logic gate_result;
  event fired;

  assign #(
      1e-3:2e-3:3e-3,
      4ps:5ps:6ps,
      7fs:8fs:9fs) continuous_result = source;
  buf #(1ps:2ps:3ps, 4ps:5ps:6ps) (gate_result, source);

  initial begin
    #(0.1:0.2:0.3) source = 1'b1;
    source = #(4ps:5ps:6ps) 1'b0;
    source <= #(7ps:8ps:9ps) 1'b1;
    ->> #(10ps:11ps:12ps) fired;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "SystemVerilog delay triples must parse");
  const auto& unit = parsed.design.units.front();
  require(
      unit.concurrent_statements.size() == 2,
      "continuous and gate delay triples are retained");
  const auto& process = unit.processes.front().statements;
  require(
      process.size() == 4
          && process[0].kind == StatementKind::Delay
          && process[0].statements.size() == 1
          && process[1].kind == StatementKind::Assignment
          && process[1].assignment_kind == AssignmentKind::Blocking
          && process[2].kind == StatementKind::Assignment
          && process[2].assignment_kind == AssignmentKind::NonBlocking
          && process[3].kind == StatementKind::EventTrigger
          && process[3].assignment_kind == AssignmentKind::NonBlocking,
      "procedural, assignment, and named-event triple forms");

  const auto verify =
      [](const Delay& delay,
         const std::array<std::uint64_t, 3>& magnitudes,
         const std::array<std::uint64_t, 3>& divisors,
         const std::string_view unit_name) {
        require(
            delay.minimum && delay.typical && delay.maximum,
            "all min:typ:max HIR branches are present");
        require(
            delay.minimum->magnitude == magnitudes[0]
                && delay.typical->magnitude == magnitudes[1]
                && delay.maximum->magnitude == magnitudes[2]
                && delay.minimum->divisor == divisors[0]
                && delay.typical->divisor == divisors[1]
                && delay.maximum->divisor == divisors[2]
                && delay.minimum->unit == unit_name
                && delay.typical->unit == unit_name
                && delay.maximum->unit == unit_name,
            "delay triple values retain exact magnitudes, divisors, and units");
        require(
            delay.magnitude == magnitudes[1]
                && delay.divisor == divisors[1]
                && delay.unit == unit_name,
            "typed HIR defaults a delay triple to its typical branch");
      };

  verify(
      *unit.concurrent_statements[0].delay,
      {1, 1, 3},
      {1000, 500, 1000},
      "ns");
  verify(
      *unit.concurrent_statements[1].delay,
      {1, 2, 3},
      {1, 1, 1},
      "ps");
  require(
      unit.concurrent_statements[0].delay->additional_values.size() == 2
          && unit.concurrent_statements[1].delay
                 ->additional_values.size()
              == 1,
      "continuous and gate rise/fall/turnoff delay-list arity");
  verify(
      unit.concurrent_statements[0].delay->additional_values[0],
      {4, 5, 6},
      {1, 1, 1},
      "ps");
  verify(
      unit.concurrent_statements[0].delay->additional_values[1],
      {7, 8, 9},
      {1, 1, 1},
      "fs");
  verify(
      unit.concurrent_statements[1].delay->additional_values[0],
      {4, 5, 6},
      {1, 1, 1},
      "ps");
  verify(*process[0].delay, {1, 1, 3}, {10, 5, 10}, "ns");
  verify(*process[1].delay, {4, 5, 6}, {1, 1, 1}, "ps");
  verify(*process[2].delay, {7, 8, 9}, {1, 1, 1}, "ps");
  verify(*process[3].delay, {10, 11, 12}, {1, 1, 1}, "ps");

  const auto malformed = parse_text(
      "bad-delay-triples.sv",
      R"(module bad_delay_triples;
  wire source;
  wire result;
  assign #(1, 2, 3, 4) result = source;
  buf #(1, 2, 3) (result, source);
  initial begin
    #1:2:3;
    #(1:2);
    #(1::3);
    #(1, 2);
    #(1,);
  end
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            malformed.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      !malformed.ok()
          && has_code("FSIM-SV-SEM-052")
          && has_code("FSIM-SV-SEM-053")
          && has_code("FSIM-SV-PARSE-134")
          && has_code("FSIM-SV-PARSE-135")
          && has_code("FSIM-SV-PARSE-023"),
      "malformed triples and illegal transition-delay lists are targeted");
}

void test_systemverilog_procedural_assignment_controls() {
  const auto parsed = parse_text(
      "assignment-controls.sv",
      R"(timeunit 1ns / 1ps;
module assignment_controls;
  logic clock;
  logic enable;
  logic source;
  logic result;

  initial begin
    result = #3 source;
    result <= #(1:2:3) source;
    result = @(posedge clock) source;
    result <= @(negedge clock or enable) source;
    result = @clock source;
    result <= @* source;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      parsed.ok(),
      "blocking and nonblocking procedural assignment controls parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      statements.size() == 6,
      "all procedural assignment control forms are retained");
  require(
      statements[0].procedural_assignment_control
              == ProceduralAssignmentControl::Delay
          && statements[0].assignment_kind
              == AssignmentKind::Blocking
          && statements[0].delay
          && statements[0].delay->magnitude == 3,
      "blocking intra-assignment delay has typed HIR");
  require(
      statements[1].procedural_assignment_control
              == ProceduralAssignmentControl::Delay
          && statements[1].assignment_kind
              == AssignmentKind::NonBlocking
          && statements[1].delay
          && statements[1].delay->minimum
          && statements[1].delay->typical
          && statements[1].delay->maximum,
      "nonblocking min/typ/max assignment delay is retained");
  require(
      statements[2].procedural_assignment_control
              == ProceduralAssignmentControl::Event
          && statements[2].sensitivities.size() == 1
          && statements[2].sensitivities.front().edge
              == EdgeKind::Positive
          && statements[2].sensitivities.front().signal
              == "clock",
      "blocking edge event control has typed sensitivity HIR");
  require(
      statements[3].procedural_assignment_control
              == ProceduralAssignmentControl::Event
          && statements[3].assignment_kind
              == AssignmentKind::NonBlocking
          && statements[3].sensitivities.size() == 2
          && statements[3].sensitivities[0].edge
              == EdgeKind::Negative
          && statements[3].sensitivities[1].edge
              == EdgeKind::Any,
      "nonblocking event lists preserve source order and edge kinds");
  require(
      statements[4].sensitivities.size() == 1
          && statements[4].sensitivities.front().signal == "clock",
      "an unparenthesized scalar event expression is accepted");
  require(
      statements[5].sensitivities.size() == 1
          && statements[5].sensitivities.front().signal == "*",
      "wildcard assignment event control is retained for RHS inference");

  const auto has_code =
      [](const ParseResult& result, const std::string_view code) {
        return std::ranges::any_of(
            result.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  const auto empty = parse_text(
      "empty-assignment-event.sv",
      R"(module empty_assignment_event;
  logic source;
  logic result;
  initial result = @() source;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !empty.ok() && has_code(empty, "FSIM-SV-PARSE-136"),
      "an empty assignment event list has a stable diagnostic");

  const auto repeated = parse_text(
      "repeated-assignment-control.sv",
      R"(module repeated_assignment_control;
  logic clock;
  logic source;
  logic result;
  initial result <= #1 @(posedge clock) source;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !repeated.ok()
          && has_code(repeated, "FSIM-SV-SEM-054"),
      "a repeated assignment control has a stable diagnostic");

  const auto repeat_event = parse_text(
      "repeat-assignment-event.sv",
      R"(module repeat_assignment_event;
  logic clock;
  logic source;
  logic result;
  initial result <= repeat (2) @(posedge clock) source;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !repeat_event.ok()
          && has_code(repeat_event, "FSIM-SV-UNSUPPORTED-032"),
      "unsupported repeated NBA event control is diagnosed explicitly");

  const auto restricted = parse_text(
      "restricted-assignment-controls.sv",
      R"(module restricted_assignment_controls;
  logic clock;
  logic source;
  logic result;
  always_comb result = #1 source;
  final result = @(posedge clock) source;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !restricted.ok()
          && has_code(restricted, "FSIM-SV-SEM-012")
          && has_code(restricted, "FSIM-SV-SEM-032"),
      "assignment controls participate in procedural restriction checks");
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
  localparam int CLOG_WIDTH = $clog2(WIDTH);
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
      parameterized != nullptr && parameterized->parameters.size() == 5,
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
          && parameterized->parameters[3].local
          && parameterized->parameters[4].name == "CLOG_WIDTH"
          && parameterized->parameters[4].local
          && parameterized->parameters[4].default_value.kind
              == ExpressionKind::Call
          && parameterized->parameters[4].default_value.text
              == "$clog2"
          && parameterized->parameters[4]
                 .default_value.operands.size()
              == 1,
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

  const auto verilog = parse_text(
      "clog2.v",
      R"(
module clog2 #(
  parameter WIDTH = 9
) ();
  localparam CLOG_WIDTH = $clog2(WIDTH);
endmodule
)",
      Language::Verilog2005);
  require(verilog.ok(), "Verilog-2005 $clog2 call must parse");
  const auto* clog2 =
      verilog.design.find(UnitKind::VerilogModule, "clog2");
  require(
      clog2 != nullptr && clog2->parameters.size() == 2
          && clog2->parameters[1].default_value.kind
              == ExpressionKind::Call
          && clog2->parameters[1].default_value.text == "$clog2"
          && clog2->parameters[1].default_value.operands.size() == 1,
      "Verilog-2005 $clog2 parameter-call HIR");

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
    packet.payload[WIDTH-2:0] = local_value[WIDTH-2:0];
    overlay.mirror[WIDTH-1] = local_value[WIDTH-1];
    packet.payload[0 +: WIDTH-1] =
      local_value[0 +: WIDTH-1];
    overlay.mirror[WIDTH-1 -: 2] =
      local_value[WIDTH-1 -: 2];
    overlay.payload = {WIDTH/2{2'b10}};
    staged = staged >>> 1;
    staged = staged <<< 1;
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
  const auto& aggregate_statements =
      parsed.design.units[2].processes.front().statements;
  require(
      aggregate_statements.size() == 11
          && aggregate_statements[4].target.kind
              == ExpressionKind::Slice
          && aggregate_statements[4].target.operands.front().text
              == "packet.payload"
          && aggregate_statements[4].value.kind
              == ExpressionKind::Slice
          && aggregate_statements[5].target.kind
              == ExpressionKind::Index
          && aggregate_statements[5].target.operands.front().text
              == "overlay.mirror"
          && aggregate_statements[5].value.kind
              == ExpressionKind::Index
          && aggregate_statements[6].target.kind
              == ExpressionKind::Slice
          && aggregate_statements[6].target.text == "+:"
          && aggregate_statements[6].value.text == "+:"
          && aggregate_statements[7].target.kind
              == ExpressionKind::Slice
          && aggregate_statements[7].target.text == "-:"
          && aggregate_statements[7].value.text == "-:"
          && aggregate_statements[8].value.kind
              == ExpressionKind::Replication
          && aggregate_statements[8].value.operands.size() == 2
          && aggregate_statements[8].value.operands[0].kind
              == ExpressionKind::Binary
          && aggregate_statements[9].value.kind
              == ExpressionKind::Binary
          && aggregate_statements[9].value.text == ">>>"
          && aggregate_statements[10].value.kind
              == ExpressionKind::Binary
          && aggregate_statements[10].value.text == "<<<",
      "packed aggregate members retain fixed/indexed selects and "
      "replication/shift expressions");

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
  logic value;
  initial value = {2{}};
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
          && has_code("FSIM-SV-PARSE-090")
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
  signal ready : boolean;
begin
  concurrent_check: assert ready
    report "concurrent mismatch" severity warning;
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
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->concurrent_statements.size() == 1,
      "VHDL assertion process");
  const auto& vhdl_concurrent_assertion =
      architecture->concurrent_statements.front();
  require(
      vhdl_concurrent_assertion.kind == StatementKind::Assert
          && vhdl_concurrent_assertion.label == "concurrent_check"
          && vhdl_concurrent_assertion.condition.text == "ready"
          && vhdl_concurrent_assertion.assertion_message
              == "concurrent mismatch"
          && vhdl_concurrent_assertion.assertion_severity
              == AssertionSeverity::Warning,
      "labeled concurrent VHDL assertion metadata");
  const auto& vhdl_assertion =
      architecture->processes.front().statements.front();
  require(
      vhdl_assertion.kind == StatementKind::Assert
          && vhdl_assertion.assertion_message == "vhdl mismatch"
          && vhdl_assertion.assertion_severity
              == AssertionSeverity::Failure
          && vhdl_assertion.span.source_name == "assertions.vhd"
          && vhdl_assertion.span.begin.line == 11,
      "VHDL assertion metadata");

  const auto system_verilog = parse_text(
      "assertions.sv",
      R"(
module assertions;
  initial begin
    assert (1'b1) $info("pass"); else $fatal("unexpected");
    assert (1'b0) begin
      $info("unreached");
    end else begin
      $warning;
      $error("sv\nmismatch");
    end
    assert (1'b0);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      system_verilog.ok(),
      "SystemVerilog immediate-assertion action blocks must parse");
  const auto& sv_assertions =
      system_verilog.design.units.front().processes.front().statements;
  require(
      sv_assertions.size() == 3
          && sv_assertions[0].kind == StatementKind::Assert
          && sv_assertions[0].assertion_has_pass_action
          && sv_assertions[0].assertion_has_failure_action
          && sv_assertions[0].statements.size() == 1
          && sv_assertions[0].statements.front().kind
              == StatementKind::Report
          && sv_assertions[0].statements.front().assertion_severity
              == AssertionSeverity::Note
          && sv_assertions[0].else_statements.size() == 1
          && sv_assertions[0].else_statements.front().assertion_severity
              == AssertionSeverity::Failure
          && sv_assertions[1].statements.size() == 1
          && sv_assertions[1].statements.front().kind
              == StatementKind::Block
          && sv_assertions[1].else_statements.size() == 1
          && sv_assertions[1].else_statements.front().kind
              == StatementKind::Block
          && sv_assertions[1].else_statements.front().statements.size()
              == 2
          && sv_assertions[2].assertion_has_pass_action
          && !sv_assertions[2].assertion_has_failure_action
          && sv_assertions[2].span.source_name == "assertions.sv",
      "SystemVerilog pass/failure action metadata");

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

  const auto invalid_sv_report = parse_text(
      "bad_report.sv",
      R"(
module bad_report;
  initial $warning(1'b1, "extra");
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_sv_report.ok()
          && std::any_of(
              invalid_sv_report.diagnostics.begin(),
              invalid_sv_report.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-043";
              }),
      "nonliteral severity-task arguments need a targeted diagnostic");

  const auto missing_assertion_action = parse_text(
      "missing_assertion_action.sv",
      R"(
module missing_assertion_action;
  initial assert (1'b1)
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !missing_assertion_action.ok()
          && std::ranges::any_of(
              missing_assertion_action.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-044";
              }),
      "missing immediate-assertion actions need a targeted diagnostic");

  const auto fatal = parse_text(
      "fatal.sv",
      R"(
module fatal_tasks;
  initial begin
    $fatal;
    $fatal("standalone\nfatal");
    $fatal(1, "controlled\tfatal");
    assert (1'b0) else $fatal("assertion \"fatal\"");
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(fatal.ok(), "bounded SystemVerilog $fatal forms must parse");
  const auto& fatal_statements =
      fatal.design.units.front().processes.front().statements;
  require(
      fatal_statements.size() == 4
          && fatal_statements[0].kind == StatementKind::Report
          && fatal_statements[1].kind == StatementKind::Report
          && fatal_statements[2].kind == StatementKind::Report
          && fatal_statements[0].output_text == "$fatal"
          && fatal_statements[1].output_text == "standalone\nfatal"
          && fatal_statements[2].output_text == "controlled\tfatal"
          && fatal_statements[3].kind == StatementKind::Assert
          && fatal_statements[3].assertion_has_failure_action
          && fatal_statements[3].else_statements.size() == 1
          && fatal_statements[3].else_statements.front().kind
              == StatementKind::Report
          && fatal_statements[3].else_statements.front().output_text
              == "assertion \"fatal\"",
      "standalone and assertion-action $fatal metadata");

  const auto severity_tasks = parse_text(
      "severity_tasks.sv",
      R"(
module severity_tasks;
  initial begin
    $info;
    $info();
    $info("note");
    $warning;
    $warning();
    $warning("warning");
    $error;
    $error();
    $error("error");
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      severity_tasks.ok(),
      "bounded standalone severity tasks must parse");
  const auto& reports =
      severity_tasks.design.units.front().processes.front().statements;
  require(
      reports.size() == 9
          && std::ranges::all_of(
              reports,
              [](const auto& statement) {
                return statement.kind == StatementKind::Report;
              })
          && reports[0].assertion_severity == AssertionSeverity::Note
          && reports[2].output_text == "note"
          && reports[3].assertion_severity
              == AssertionSeverity::Warning
          && reports[5].output_text == "warning"
          && reports[6].assertion_severity
              == AssertionSeverity::Error
          && reports[8].output_text == "error",
      "standalone severity task forms and metadata");

  const auto verilog_fatal = parse_text(
      "fatal.v",
      "module fatal_v; initial $fatal; endmodule",
      Language::Verilog2005);
  require(
      !verilog_fatal.ok()
          && std::ranges::any_of(
              verilog_fatal.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-007";
              }),
      "$fatal must remain SystemVerilog-only");

  const auto verilog_report = parse_text(
      "report.v",
      "module report_v; initial $error; endmodule",
      Language::Verilog2005);
  require(
      !verilog_report.ok()
          && std::ranges::any_of(
              verilog_report.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-009";
              }),
      "severity report tasks must remain SystemVerilog-only");
}

void test_vhdl_literal_report() {
  const auto parsed = parse_text(
      "report.vhd",
      R"(
entity reporter is
end entity;
architecture rtl of reporter is
begin
  process
  begin
    report "vhdl ""quote""" severity note;
    report "" severity warning;
    report "error" severity error;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "literal VHDL report statements must parse");
  const auto* architecture =
      parsed.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 4
          && architecture->processes.front().statements[0].kind
              == StatementKind::Report
          && architecture->processes.front().statements[0].output_text
              == "vhdl \"quote\""
          && architecture->processes.front().statements[0]
                 .assertion_severity == AssertionSeverity::Note
          && architecture->processes.front().statements[1].kind
              == StatementKind::Report
          && architecture->processes.front().statements[1].output_text.empty()
          && architecture->processes.front().statements[1]
                 .assertion_severity == AssertionSeverity::Warning
          && architecture->processes.front().statements[2]
                 .assertion_severity == AssertionSeverity::Error,
      "VHDL report literal HIR, severity, and doubled-quote decoding");

  const auto failure = parse_text(
      "report_failure.vhd",
      R"(
entity reporter is end entity;
architecture rtl of reporter is
begin
  process
  begin
    report "failure" severity failure;
    wait;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      failure.ok()
          && failure.design
                 .find(UnitKind::VhdlArchitecture, "rtl")
                 ->processes.front()
                 .statements.front()
                 .assertion_severity
              == AssertionSeverity::Failure,
      "failure VHDL report severity HIR");
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

void test_systemverilog_procedural_block_scopes() {
  const auto result = parse_text(
      "block_scopes.sv",
      R"(
module block_scopes;
  logic result;
  initial begin : root_scope
    logic value = 1'b0;
    begin : inner_scope
      logic value = 1'b1;
      result = value;
    end : inner_scope
    begin
      logic anonymous_value;
      result = anonymous_value;
    end
    for (int lane = 0; lane < 2; lane++) begin : iteration
      bit temporary;
      temporary = value;
    end : iteration
    if (result) begin : selected
      logic branch_value;
      result = branch_value;
    end : selected
    else begin : alternate
      logic branch_value;
      result = branch_value;
    end : alternate
  end : root_scope
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "named procedural blocks and loop-local declarations must parse");
  const auto& process =
      result.design.units.front().processes.front();
  require(
      process.variables.empty()
          && process.statements.size() == 1
          && process.statements.front().kind
              == StatementKind::Block
          && process.statements.front().label == "root_scope"
          && process.statements.front().declarations.size() == 1,
      "a named process body must remain a lexical HIR block");
  const auto& root = process.statements.front();
  require(
      root.statements.size() == 4
          && root.statements.front().kind
              == StatementKind::Block
          && root.statements.front().label == "inner_scope"
          && root.statements.front().declarations.size() == 1,
      "nested named block declarations and labels");
  require(
      root.statements[1].kind == StatementKind::Block
          && root.statements[1].label.empty()
          && root.statements[1].declarations.size() == 1
          && root.statements[1].declarations.front().name
              == "anonymous_value",
      "an anonymous block with declarations must retain its lexical HIR");
  const auto& loop = root.statements[2];
  require(
      loop.kind == StatementKind::Loop
          && loop.statements.size() == 1
          && loop.statements.front().kind
              == StatementKind::Block
          && loop.statements.front().label == "iteration"
          && loop.statements.front().declarations.size() == 1,
      "a procedural loop must retain its declared lexical block");
  const auto& conditional = root.statements[3];
  require(
      conditional.kind == StatementKind::If
          && conditional.statements.size() == 1
          && conditional.statements.front().kind
              == StatementKind::Block
          && conditional.statements.front().label == "selected"
          && conditional.statements.front().declarations.size() == 1
          && conditional.else_statements.size() == 1
          && conditional.else_statements.front().kind
              == StatementKind::Block
          && conditional.else_statements.front().label == "alternate"
          && conditional.else_statements.front().declarations.size() == 1,
      "conditional branches must retain declared lexical blocks");

  const auto mismatched = parse_text(
      "bad_block_labels.sv",
      R"(
module bad_block_labels;
  initial begin : opening
  end : closing
  initial begin
  end : orphan
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !mismatched.ok()
          && std::ranges::count_if(
                 mismatched.diagnostics,
                 [](const Diagnostic& diagnostic) {
                   return diagnostic.code == "FSIM-SV-SEM-034";
                 })
              == 2,
      "mismatched and orphan procedural end labels need stable diagnostics");
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
    wait until trigger = '1';
    wait;
    wait on trigger until trigger = '0';
    wait on trigger for 3 ns;
    wait until trigger = '1' for 4 ns;
    wait on trigger until trigger = '0' for 5 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(vhdl.ok(), "bounded VHDL wait statements must parse");
  const auto& vhdl_statements =
      vhdl.design.units.back().processes.front().statements;
  require(
      vhdl_statements.size() == 8
          && vhdl_statements[0].kind == StatementKind::Delay
          && vhdl_statements[0].delay
          && vhdl_statements[0].delay->magnitude == 2
          && vhdl_statements[0].delay->unit == "ns"
          && vhdl_statements[1].kind == StatementKind::WaitOn
          && vhdl_statements[1].sensitivities.size() == 1
          && vhdl_statements[1].sensitivities.front().signal
              == "trigger"
          && vhdl_statements[2].kind
              == StatementKind::WaitUntil
          && vhdl_statements[2].condition.kind
              == ExpressionKind::Binary
          && vhdl_statements[2].condition.text == "="
          && vhdl_statements[3].kind
              == StatementKind::WaitUntil
          && vhdl_statements[3].condition.kind
              == ExpressionKind::BooleanLiteral
          && vhdl_statements[3].condition.text == "true"
          && vhdl_statements[4].kind
              == StatementKind::WaitUntil
          && vhdl_statements[4].sensitivities.size() == 1
          && vhdl_statements[4].sensitivities.front().signal
              == "trigger"
          && !vhdl_statements[4].delay
          && vhdl_statements[5].kind
              == StatementKind::WaitOn
          && vhdl_statements[5].delay
          && vhdl_statements[5].delay->magnitude == 3
          && vhdl_statements[6].kind
              == StatementKind::WaitUntil
          && vhdl_statements[6].sensitivities.empty()
          && vhdl_statements[6].delay
          && vhdl_statements[6].delay->magnitude == 4
          && vhdl_statements[7].kind
              == StatementKind::WaitUntil
          && vhdl_statements[7].sensitivities.size() == 1
          && vhdl_statements[7].delay
          && vhdl_statements[7].delay->magnitude == 5,
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
    wait (trigger) observed = 1'b1;
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
      sv_statements.size() == 3
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
              == StatementKind::Assignment
          && sv_statements[2].kind
              == StatementKind::WaitUntil
          && sv_statements[2].condition.kind
              == ExpressionKind::Identifier
          && sv_statements[2].statements.size() == 1
          && sv_statements[2].statements.front().kind
              == StatementKind::Assignment,
      "SystemVerilog procedural event metadata");

  const auto malformed_sv_wait = parse_text(
      "bad_wait.sv",
      R"(
module bad_wait;
  logic trigger;
  initial wait trigger;
  initial wait (trigger;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !malformed_sv_wait.ok()
          && std::ranges::any_of(
              malformed_sv_wait.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-109";
              })
          && std::ranges::any_of(
              malformed_sv_wait.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-110";
              }),
      "malformed condition waits receive stable diagnostics");

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
  initial casez (selector)
    2'b0?: result = 2'b01;
    default: result = 2'b00;
  endcase
  initial casex (selector)
    2'b0x: result = 2'b10;
    default: result = 2'b00;
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
  require(
      statement.case_match_kind == CaseMatchKind::Exact
          && result.design.units.front().processes[1]
                     .statements.front().case_match_kind
                 == CaseMatchKind::WildcardZ
          && result.design.units.front().processes[2]
                     .statements.front().case_match_kind
                 == CaseMatchKind::WildcardXZ,
      "exact, casez, and casex matching modes remain distinct in HIR");

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

void test_systemverilog_procedural_for_loops() {
  const auto result = parse_text(
      "procedural_for.sv",
      R"(
module procedural_for;
  logic [3:0] result;
  initial begin
    result = 4'b0000;
    for (int lane = 0; lane < 4; lane++)
      result[lane] = 1'b1;
    for (integer lane = 3; lane >= 2; --lane) begin
      result[lane] = 1'b0;
    end
    for (int lane = 2; lane < 1; lane += 1)
      result[0] = 1'b0;
    for (int lane = 0; lane <= 0; lane = lane + 1)
      result[0] = result[0];
    for (int lane = 0; lane > 0; lane -= 1)
      result[0] = 1'b0;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "bounded SystemVerilog procedural for loops must parse");
  require(
      result.design.units.front().signals.size() == 1
          && result.design.units.front().signals.front().name
              == "result",
      "inline procedural loop indices must not become implicit nets");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 6
          && statements[1].kind == StatementKind::Loop
          && statements[1].loop_variable == "lane"
          && statements[1].loop_initial.text == "0"
          && statements[1].loop_limit.text == "4"
          && !statements[1].loop_descending
          && statements[1].loop_limit_exclusive
          && statements[2].kind == StatementKind::Loop
          && statements[2].loop_descending
          && !statements[2].loop_limit_exclusive
          && statements[2].statements.size() == 1
          && statements[3].kind == StatementKind::Loop
          && statements[4].kind == StatementKind::Loop
          && !statements[4].loop_limit_exclusive
          && statements[5].kind == StatementKind::Loop
          && statements[5].loop_descending
          && statements[5].loop_limit_exclusive,
      "SystemVerilog loop range normalization and bodies");

  const auto invalid = parse_text(
      "bad_procedural_for.sv",
      R"(
module bad_procedural_for;
  initial begin
    for (lane = 0; lane < 4; lane++);
    for (int lane = 0; other < 4; lane++);
    for (int lane = 0; lane < 4; other++);
    for (int lane = 0; lane < 4; lane--);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid.ok(), "noncanonical procedural loops must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-UNSUPPORTED-031"},
           std::string_view{"FSIM-SV-SEM-027"},
           std::string_view{"FSIM-SV-SEM-028"},
           std::string_view{"FSIM-SV-SEM-029"}}) {
    require(
        std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted bounded procedural loop diagnostic");
  }
}

void test_verilog_repeat_statements() {
  const auto systemverilog = parse_text(
      "repeat_statement.sv",
      R"(
module repeat_statement;
  logic [1:0] result;
  initial begin
    result = 2'b00;
    repeat (3) result = result + 1;
    repeat (0) result = 2'b11;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog repeat statements must parse");
  const auto& statements =
      systemverilog.design.units.front()
          .processes.front()
          .statements;
  require(
      statements.size() == 3
          && statements[1].kind == StatementKind::Loop
          && statements[1].loop_repeat
          && statements[1].loop_limit_exclusive
          && statements[1].loop_initial.text == "0"
          && statements[1].loop_limit.text == "3"
          && statements[1].loop_variable.empty()
          && statements[1].statements.size() == 1
          && statements[2].kind == StatementKind::Loop
          && statements[2].loop_repeat
          && statements[2].loop_limit.text == "0",
      "repeat count and body HIR");

  const auto verilog = parse_text(
      "repeat_statement.v",
      R"(
module repeat_statement;
  reg result;
  initial begin
    result = 1'b0;
    repeat (2) result = ~result;
  end
endmodule
)",
      Language::Verilog2005);
  require(
      verilog.ok()
          && verilog.design.units.front()
                 .processes.front()
                 .statements[1]
                 .loop_repeat,
      "Verilog-2005 repeat statements share the loop HIR");

  const auto malformed = parse_text(
      "bad_repeat.sv",
      R"(
module bad_repeat;
  initial repeat 2;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !malformed.ok()
          && std::ranges::any_of(
              malformed.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-100";
              })
          && std::ranges::any_of(
              malformed.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-SV-PARSE-101";
              }),
      "malformed repeat delimiters receive stable diagnostics");
}

void test_runtime_loop_statements() {
  const auto systemverilog = parse_text(
      "runtime_loops.sv",
      R"(
module runtime_loops;
  logic flag;
  logic [2:0] count;
  initial begin
    count = 3'b000;
    while (count < 3) count = count + 1;
    forever #1 flag = ~flag;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "SystemVerilog while and timed forever loops must parse");
  const auto& statements =
      systemverilog.design.units.front()
          .processes.front()
          .statements;
  require(
      statements.size() == 3
          && statements[1].kind == StatementKind::Loop
          && statements[1].loop_runtime
          && statements[1].condition.kind
              == ExpressionKind::Binary
          && statements[1].condition.text == "<"
          && statements[2].kind == StatementKind::Loop
          && statements[2].loop_runtime
          && statements[2].condition.kind
              == ExpressionKind::LogicLiteral
          && statements[2].statements.size() == 1
          && statements[2].statements.front().kind
              == StatementKind::Delay,
      "runtime loop conditions and suspension body HIR");

  const auto verilog = parse_text(
      "runtime_loops.v",
      R"(
module runtime_loops;
  reg flag;
  initial begin
    flag = 1'b1;
    while (flag) flag = 1'b0;
    forever #1 flag = ~flag;
  end
endmodule
)",
      Language::Verilog2005);
  require(
      verilog.ok()
          && verilog.design.units.front()
                 .processes.front()
                 .statements[1]
                 .loop_runtime
          && verilog.design.units.front()
                 .processes.front()
                 .statements[2]
                 .loop_runtime,
      "Verilog-2005 runtime loops share the common HIR");

  const auto invalid = parse_text(
      "bad_runtime_loops.sv",
      R"(
module bad_runtime_loops;
  logic flag;
  initial while flag;
  initial forever flag = ~flag;
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid.ok(), "malformed or nonsuspending loops must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-PARSE-102"},
           std::string_view{"FSIM-SV-PARSE-103"},
           std::string_view{"FSIM-SV-SEM-030"}}) {
    require(
        std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted runtime-loop diagnostic");
  }
}

void test_loop_control_statements() {
  const auto systemverilog = parse_text(
      "loop_control.sv",
      R"(
module loop_control;
  logic flag;
  initial begin
    for (int outer = 0; outer < 2; outer++) begin
      continue;
      while (flag) begin
        break;
      end
      break;
    end
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      systemverilog.ok(),
      "nested SystemVerilog break and continue statements must parse");
  const auto& sv_loop =
      systemverilog.design.units.front()
          .processes.front()
          .statements.front();
  require(
      sv_loop.kind == StatementKind::Loop
          && sv_loop.statements.size() == 3
          && sv_loop.statements[0].kind
              == StatementKind::Continue
          && sv_loop.statements[1].kind == StatementKind::Loop
          && sv_loop.statements[1].statements.size() == 1
          && sv_loop.statements[1].statements[0].kind
              == StatementKind::Break
          && sv_loop.statements[2].kind
              == StatementKind::Break,
      "SystemVerilog loop controls retain their nested HIR scopes");

  const auto invalid_systemverilog = parse_text(
      "invalid_loop_control.sv",
      R"(
module invalid_loop_control;
  initial begin
    break;
    continue
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_systemverilog.ok()
          && std::ranges::any_of(
              invalid_systemverilog.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-031";
              })
          && std::ranges::any_of(
              invalid_systemverilog.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-104";
              }),
      "out-of-loop and malformed SystemVerilog controls are diagnosed");

  const auto vhdl = parse_text(
      "loop_control.vhd",
      R"(
entity loop_control is
end entity;
architecture rtl of loop_control is
begin
  exercise: process
  begin
    outer_loop: for outer in 0 to 1 loop
      next outer_loop when outer = 0;
      inner_loop: while true loop
        exit inner_loop;
      end loop inner_loop;
      exit outer_loop when outer = 1;
    end loop outer_loop;
    plain_loop: loop
      next when false;
      exit plain_loop;
    end loop plain_loop;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      vhdl.ok(),
      "nested VHDL exit and next statements must parse");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  const auto& vhdl_loop =
      architecture->processes.front().statements.front();
  const auto& unconditional_vhdl_loop =
      architecture->processes.front().statements[1];
  require(
      vhdl_loop.kind == StatementKind::Loop
          && vhdl_loop.loop_label == "outer_loop"
          && vhdl_loop.statements.size() == 3
          && vhdl_loop.statements[0].kind == StatementKind::If
          && vhdl_loop.statements[0].statements.front().kind
              == StatementKind::Continue
          && vhdl_loop.statements[0].statements.front()
                 .loop_control_label
              == "outer_loop"
          && vhdl_loop.statements[1].kind == StatementKind::Loop
          && vhdl_loop.statements[1].loop_label == "inner_loop"
          && vhdl_loop.statements[1].statements.front().kind
              == StatementKind::Break
          && vhdl_loop.statements[1].statements.front()
                 .loop_control_label
              == "inner_loop"
          && vhdl_loop.statements[2].kind == StatementKind::If
          && vhdl_loop.statements[2].statements.front().kind
              == StatementKind::Break
          && unconditional_vhdl_loop.kind == StatementKind::Loop
          && unconditional_vhdl_loop.loop_label == "plain_loop"
          && unconditional_vhdl_loop.loop_runtime
          && unconditional_vhdl_loop.condition.kind
              == ExpressionKind::BooleanLiteral
          && unconditional_vhdl_loop.condition.text == "true"
          && unconditional_vhdl_loop.statements.size() == 2,
      "conditional controls and unconditional VHDL loops retain HIR");

  const auto invalid_vhdl = parse_text(
      "invalid_loop_control.vhd",
      R"(
architecture rtl of invalid_loop_control is
begin
  exercise: process
  begin
    exit;
    for lane in 0 to 1 loop
      next missing_loop when lane = 0
    end loop;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_vhdl.ok()
          && std::ranges::any_of(
              invalid_vhdl.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-VHDL-SEM-023";
              })
          && std::ranges::any_of(
              invalid_vhdl.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-024";
              })
          && std::ranges::any_of(
              invalid_vhdl.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-110";
              }),
      "out-of-loop, unknown-target, and malformed controls are diagnosed");

  const auto invalid_labels = parse_text(
      "invalid_loop_labels.vhd",
      R"(
architecture rtl of invalid_loop_labels is
begin
  exercise: process
  begin
    outer_loop: loop
      outer_loop: loop
        exit;
      end loop wrong_loop;
    end loop outer_loop;
    sibling_loop: loop
      exit;
    end loop sibling_loop;
    sibling_loop: loop
      exit;
    end loop sibling_loop;
    loop
      exit;
    end loop orphan_label;
    wait for 1 ns;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid_labels.ok()
          && std::ranges::any_of(
              invalid_labels.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-025";
              })
          && std::ranges::any_of(
              invalid_labels.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-SEM-026";
              }),
      "mismatched, orphan, and duplicate VHDL loop labels are diagnosed");

  const auto malformed_vhdl_loop = parse_text(
      "malformed_unconditional_loop.vhd",
      R"(
architecture rtl of malformed_unconditional_loop is
begin
  exercise: process
  begin
    loop
      exit;
    end loop
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !malformed_vhdl_loop.ok()
          && std::ranges::any_of(
              malformed_vhdl_loop.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VHDL-PARSE-113";
              }),
      "malformed unconditional VHDL loops receive a stable diagnostic");
}

void test_systemverilog_do_while_statements() {
  const auto result = parse_text(
      "do_while.sv",
      R"(
module do_while;
  logic [2:0] count;
  initial begin
    do begin
      count = count + 1;
      if (count == 1) continue;
      if (count == 2) break;
    end while (count < 3);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(),
      "SystemVerilog do-while statements must parse");
  const auto& statement =
      result.design.units.front()
          .processes.front()
          .statements.front();
  require(
      statement.kind == StatementKind::Loop
          && statement.loop_runtime
          && statement.loop_post_test
          && statement.condition.kind == ExpressionKind::Binary
          && statement.condition.text == "<"
          && statement.statements.size() == 3
          && statement.statements[1].statements.front().kind
              == StatementKind::Continue
          && statement.statements[2].statements.front().kind
              == StatementKind::Break,
      "do-while retains its post-test condition and loop controls");

  const auto malformed = parse_text(
      "malformed_do_while.sv",
      R"(
module malformed_do_while;
  initial do ; (1'b0)
  initial do ; while 1'b0;
endmodule
)",
      Language::SystemVerilog2017);
  require(!malformed.ok(), "malformed do-while statements must fail");
  for (const auto code : {
           std::string_view{"FSIM-SV-PARSE-105"},
           std::string_view{"FSIM-SV-PARSE-106"},
           std::string_view{"FSIM-SV-PARSE-107"},
           std::string_view{"FSIM-SV-PARSE-108"}}) {
    require(
        std::ranges::any_of(
            malformed.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            }),
        "targeted SystemVerilog do-while diagnostic");
  }
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
    result = lhs === rhs;
    result = lhs !== rhs;
    result = lhs ==? rhs;
    result = lhs !=? rhs;
    result = lhs < rhs;
    result = lhs <= rhs;
    result = lhs > rhs;
    result = lhs >= rhs;
    result = lhs && result;
    result = result || rhs;
    result = &lhs;
    result = |lhs;
    result = ^lhs;
    result = ~&lhs;
    result = ~|lhs;
    result = ~^lhs;
    result = ^~lhs;
    shifted = lhs ~^ rhs;
    shifted = lhs ^~ rhs;
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
      statements.size() == 23
          && statements[0].value.kind == ExpressionKind::Unary
          && statements[0].value.text == "!",
      "logical-negation expression node");
  const std::array<std::string_view, 9> operators{
      "!=", "===", "!==", "==?", "!=?", "<", "<=", ">", ">="};
  for (std::size_t index = 0; index < operators.size(); ++index) {
    require(
        statements[index + 1].value.kind
                == ExpressionKind::Binary
            && statements[index + 1].value.text
                == operators[index],
        "comparison expression node");
  }
  require(
      statements[10].value.kind == ExpressionKind::Binary
          && statements[10].value.text == "&&"
          && statements[11].value.kind == ExpressionKind::Binary
          && statements[11].value.text == "||",
      "logical binary expression nodes");
  require(
      statements[12].value.kind == ExpressionKind::Unary
          && statements[12].value.text == "&"
          && statements[13].value.kind == ExpressionKind::Unary
          && statements[13].value.text == "|"
          && statements[14].value.kind == ExpressionKind::Unary
          && statements[14].value.text == "^",
      "reduction expression nodes");
  require(
      statements[15].value.kind == ExpressionKind::Unary
          && statements[15].value.text == "~&"
          && statements[16].value.kind == ExpressionKind::Unary
          && statements[16].value.text == "~|"
          && statements[17].value.kind == ExpressionKind::Unary
          && statements[17].value.text == "~^"
          && statements[18].value.kind == ExpressionKind::Unary
          && statements[18].value.text == "^~",
      "complemented reduction expression nodes");
  require(
      statements[19].value.kind == ExpressionKind::Binary
          && statements[19].value.text == "~^"
          && statements[20].value.kind == ExpressionKind::Binary
          && statements[20].value.text == "^~",
      "binary XNOR expression nodes");
  require(
      statements[21].value.kind == ExpressionKind::Binary
          && statements[21].value.text == "<<"
          && statements[22].value.kind == ExpressionKind::Binary
          && statements[22].value.text == ">>",
      "logical-shift expression nodes");

  const auto verilog = parse_text(
      "wildcard_equality.v",
      R"(
module wildcard_equality;
  reg [3:0] lhs;
  reg [3:0] rhs;
  reg result;
  always @* result = lhs ==? rhs;
endmodule
)",
      Language::Verilog2005);
  require(
      !verilog.ok()
          && std::any_of(
              verilog.diagnostics.begin(),
              verilog.diagnostics.end(),
              [](const Diagnostic& diagnostic) {
                return diagnostic.code
                    == "FSIM-VERILOG-SEM-004";
              }),
      "wildcard equality requires SystemVerilog");
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

void test_gate_primitives() {
  const auto result = parse_text(
      "gates.sv",
      R"(
module gates;
  logic a;
  logic b;
  logic c;
  logic y_buf;
  logic y_buf_second;
  logic y_not;
  logic y_and;
  logic y_nand;
  logic y_or;
  logic y_nor;
  logic y_xor;
  logic y_xnor;
  buf #2 (y_buf, a), named_buf (y_buf_second, b);
  not named_not (y_not, a);
  and (y_and, a, b, c);
  nand (y_nand, a, b, c);
  or (y_or, a, b, c);
  nor (y_nor, a, b, c);
  xor (y_xor, a, b, c);
  xnor (y_xnor, a, b, c);
endmodule
)",
      Language::SystemVerilog2017);
  require(result.ok(), "built-in gate primitives must parse");
  const auto& statements =
      result.design.units.front().concurrent_statements;
  require(
      statements.size() == 9
          && std::ranges::all_of(
              statements,
              [](const Statement& statement) {
                return statement.kind == StatementKind::Assignment
                    && statement.assignment_kind
                        == AssignmentKind::Continuous;
              })
          && statements[0].delay
          && statements[0].delay->magnitude == 2
          && statements[1].delay
          && statements[1].delay->magnitude == 2
          && statements[0].value.kind == ExpressionKind::Identifier
          && statements[2].value.kind == ExpressionKind::Unary
          && statements[3].value.kind == ExpressionKind::Binary
          && statements[4].value.kind == ExpressionKind::Unary,
      "gate primitives lower into continuous expression HIR");

  const auto invalid = parse_text(
      "invalid_gate.sv",
      R"(
module invalid_gate;
  logic a;
  logic y;
  and (y, a);
  not (y, a, a);
  and (strong1, pull0) (y, a, a);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid.ok()
          && std::ranges::count_if(
                 invalid.diagnostics,
                 [](const Diagnostic& diagnostic) {
                   return diagnostic.code == "FSIM-SV-SEM-026";
                 })
              == 2,
      "invalid gate terminal counts are targeted");
  require(
      std::ranges::any_of(
          invalid.diagnostics,
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-SV-UNSUPPORTED-030";
          }),
      "unsupported gate strengths are targeted");
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
      vhdl.ok(),
      "VHDL conditional parsing must retain a trailing bare wait");
  const auto* architecture =
      vhdl.design.find(UnitKind::VhdlArchitecture, "rtl");
  require(
      architecture != nullptr
          && architecture->processes.size() == 1
          && architecture->processes.front().statements.size() == 3
          && architecture->processes.front().statements.back().kind
              == StatementKind::WaitUntil,
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

void test_systemverilog_named_events() {
  const auto parsed = parse_text(
      "named_events.sv",
      R"(
module named_events;
  event fired, acknowledged;
  logic observed;
  initial begin
    -> fired;
    @(acknowledged);
  end
  always @(fired) observed = 1'b1;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "SystemVerilog named events must parse");
  const auto& unit = parsed.design.units.front();
  require(
      unit.signals.size() == 3
          && unit.signals[0].name == "fired"
          && unit.signals[0].type.spelling == "event"
          && unit.signals[1].name == "acknowledged"
          && unit.signals[1].type.spelling == "event",
      "named event declarations");
  require(
      unit.processes.size() == 2
          && unit.processes[0].statements.size() == 2
          && unit.processes[0].statements[0].kind
              == StatementKind::EventTrigger
          && unit.processes[0].statements[0].target.text == "fired"
          && unit.processes[0].statements[1].kind
              == StatementKind::WaitOn
          && unit.processes[0].statements[1]
                 .sensitivities.front().signal
              == "acknowledged"
          && unit.processes[1].sensitivities.front().signal == "fired",
      "named event trigger and controls");

  const auto nonblocking = parse_text(
      "nonblocking_event.sv",
      R"(
module nonblocking_event;
  event fired;
  initial ->> fired;
  initial ->> #2 fired;
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [](const auto& result, const std::string_view code) {
    return std::any_of(
        result.diagnostics.begin(),
        result.diagnostics.end(),
        [&](const auto& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      nonblocking.ok()
          && nonblocking.design.units.front()
                 .processes.front().statements.front()
                 .kind
              == StatementKind::EventTrigger
          && nonblocking.design.units.front()
                 .processes.front().statements.front()
                 .assignment_kind
              == AssignmentKind::NonBlocking
          && !nonblocking.design.units.front()
                  .processes.front().statements.front().delay
          && nonblocking.design.units.front().processes.size() == 2
          && nonblocking.design.units.front()
                 .processes[1].statements.front().delay
          && nonblocking.design.units.front()
                 .processes[1].statements.front().delay->magnitude
              == 2,
      "nonblocking event trigger HIR");

  const auto verilog_nonblocking = parse_text(
      "nonblocking_event.v",
      R"(
module nonblocking_event;
  event fired;
  initial ->> fired;
endmodule
)",
      Language::Verilog2005);
  require(
      has_code(
          verilog_nonblocking, "FSIM-VERILOG-SEM-008"),
      "nonblocking event trigger language diagnostic");

  const auto invalid_immediate_delay = parse_text(
      "immediate_delay.sv",
      R"(
module immediate_delay;
  event fired;
  initial -> #1 fired;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      has_code(invalid_immediate_delay, "FSIM-SV-SEM-036"),
      "delayed immediate event trigger diagnostic");

  const auto duplicate = parse_text(
      "duplicate_event.sv",
      R"(
module duplicate_event;
  logic fired;
  event fired;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      has_code(duplicate, "FSIM-SV-SEM-035"),
      "duplicate named event diagnostic");
}

void test_verilog_literal_display() {
  const auto parsed = parse_text(
      "display.v",
      R"(
module display;
  initial begin
    $display("hello\nworld\t\"quote\"\\slash\101");
    $display();
    $display;
  end
endmodule
)",
      Language::Verilog2005);
  require(parsed.ok(), "literal $display tasks must parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      statements.size() == 3
          && std::all_of(
              statements.begin(),
              statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline;
              })
          && statements.front().output_text
              == "hello\nworld\t\"quote\"\\slashA"
          && statements[1].output_text.empty()
          && statements[2].output_text.empty(),
      "literal and empty $display HIR");

  const auto formatted = parse_text(
      "formatted_display.sv",
      R"(
module formatted_display;
  logic q;
  initial begin
    $display("q=%%:%b!", q);
    $write("%b", q);
    $display("%h", q);
    $display("%o", q);
    $display("%d", q);
    $display("%c", q);
    $display("%s", q);
    $display("%0h", q);
    $display("%B", q);
    $display("%X", q);
    $display("%O", q);
    $display("%D", q);
    $display("%C", q);
    $display("%S", q);
    $display("%8h", q);
    $display("%-8x", q);
    $display("%08d", q);
    $display("scope=%m q=%B", q);
    $display("time=%08T");
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      formatted.ok()
          && formatted.design.units.front().processes.front()
                 .statements.size() == 19
          && formatted.design.units.front().processes.front()
                 .statements[0].output_format
              == OutputFormat::Binary
          && formatted.design.units.front().processes.front()
                 .statements[0].output_prefix == "q=%:"
          && formatted.design.units.front().processes.front()
                 .statements[0].output_suffix == "!"
          && formatted.design.units.front().processes.front()
                 .statements[0].value.text == "q"
          && !formatted.design.units.front().processes.front()
                  .statements[1].output_newline
          && formatted.design.units.front().processes.front()
                 .statements[2].output_format
              == OutputFormat::Hexadecimal
          && formatted.design.units.front().processes.front()
                 .statements[3].output_format
              == OutputFormat::Octal
          && formatted.design.units.front().processes.front()
                 .statements[4].output_format
              == OutputFormat::Decimal
          && formatted.design.units.front().processes.front()
                 .statements[5].output_format
              == OutputFormat::Character
          && formatted.design.units.front().processes.front()
                 .statements[6].output_format
              == OutputFormat::String
          && formatted.design.units.front().processes.front()
                 .statements[7].output_format
              == OutputFormat::Hexadecimal
          && formatted.design.units.front().processes.front()
                 .statements[7].output_suppress_leading_zero,
          "single-value formats, %0 suppression, and %% decoding");
  const auto& formatted_statements =
      formatted.design.units.front().processes.front().statements;
  require(
      formatted_statements[8].output_format
              == OutputFormat::Binary
          && formatted_statements[9].output_format
              == OutputFormat::Hexadecimal
          && formatted_statements[10].output_format
              == OutputFormat::Octal
          && formatted_statements[11].output_format
              == OutputFormat::Decimal
          && formatted_statements[12].output_format
              == OutputFormat::Character
          && formatted_statements[13].output_format
              == OutputFormat::String,
      "uppercase and %x conversion aliases");
  require(
      formatted_statements[14].output_minimum_width == 8
          && !formatted_statements[14].output_left_justify
          && !formatted_statements[14].output_zero_pad
          && formatted_statements[15].output_minimum_width == 8
          && formatted_statements[15].output_left_justify
          && !formatted_statements[15].output_zero_pad
          && formatted_statements[16].output_minimum_width == 8
          && !formatted_statements[16].output_left_justify
          && formatted_statements[16].output_zero_pad,
      "minimum-width, left-justification, and zero-padding metadata");
  require(
      formatted_statements[17].output_values.size() == 2
          && formatted_statements[17].output_values[0].format
              == OutputFormat::Hierarchy
          && !formatted_statements[17].output_values[0].value.valid()
          && formatted_statements[17].output_values[0].prefix
              == "scope="
          && formatted_statements[17].output_values[1].format
              == OutputFormat::Binary
          && formatted_statements[17].output_values[1].value.text
              == "q",
      "%m hierarchy substitution does not consume a runtime value");
  require(
      formatted_statements[18].output_values.size() == 1
          && formatted_statements[18].output_values[0].format
              == OutputFormat::Time
          && !formatted_statements[18].output_values[0].value.valid()
          && formatted_statements[18].output_values[0].minimum_width
              == 8
          && formatted_statements[18].output_values[0].zero_pad,
      "%t current-time substitution and width metadata");

  const auto invalid_format_width = parse_text(
      "invalid_format_width.sv",
      R"(
module invalid_format_width;
  logic q;
  initial begin
    $display("%-h", q);
    $display("%0s", q);
    $display("%999999999999999999999h", q);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::count_if(
          invalid_format_width.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-042";
          })
          == 3,
      "invalid field modifiers need targeted diagnostics");

  const auto unsupported = parse_text(
      "unsupported_format.sv",
      R"(
module unsupported_format;
  logic q;
  initial $display("%v", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          unsupported.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-042";
          }),
      "unsupported output conversions need a targeted diagnostic");

  const auto bad_escape = parse_text(
      "bad_escape.sv",
      R"(
module bad_escape;
  initial $display("bad\q");
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::any_of(
          bad_escape.diagnostics.begin(),
          bad_escape.diagnostics.end(),
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-040";
          }),
      "unsupported output-string escapes need a targeted diagnostic");

  const auto wide_octal_escape = parse_text(
      "wide_octal_escape.sv",
      R"(
module wide_octal_escape;
  initial $display("\777");
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::any_of(
          wide_octal_escape.diagnostics.begin(),
          wide_octal_escape.diagnostics.end(),
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-040";
          }),
      "out-of-byte-range octal escapes need a targeted diagnostic");

  const auto write = parse_text(
      "write.sv",
      R"(
module write;
  initial begin
    $write("hello");
    $write();
    $write;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(write.ok(), "literal $write tasks must parse");
  const auto& write_statements =
      write.design.units.front().processes.front().statements;
  require(
      write_statements.size() == 3
          && std::all_of(
              write_statements.begin(),
              write_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && !statement.output_newline;
              })
          && write_statements.front().output_text == "hello"
          && write_statements[1].output_text.empty()
          && write_statements[2].output_text.empty(),
      "literal and empty $write HIR");

  const auto multiple_write = parse_text(
      "multiple_write.sv",
      R"(
module formatted_write;
  logic [3:0] a;
  logic [7:0] b;
  initial begin
    $write("a=%b b=%h tail=", a, b, a);
    $display(a, b);
    $display("prefix=", a);
  end
endmodule
)",
      Language::SystemVerilog2017);
  const auto& multi_statements =
      multiple_write.design.units.front().processes.front().statements;
  require(
      multiple_write.ok() && multi_statements.size() == 3
          && multi_statements[0].output_values.size() == 3
          && multi_statements[0].output_values[0].format
              == OutputFormat::Binary
          && multi_statements[0].output_values[0].prefix == "a="
          && multi_statements[0].output_values[1].format
              == OutputFormat::Hexadecimal
          && multi_statements[0].output_values[1].prefix == " b="
          && multi_statements[0].output_values[2].format
              == OutputFormat::Decimal
          && multi_statements[0].output_values[2].prefix == " tail="
          && multi_statements[1].output_values.size() == 2
          && multi_statements[1].output_values[0].format
              == OutputFormat::Decimal
          && multi_statements[2].output_values.size() == 1
          && multi_statements[2].output_values[0].prefix == "prefix=",
      "multiple conversions and default runtime arguments in source order");

  const auto missing_format_value = parse_text(
      "missing_format_value.sv",
      R"(
module missing_format_value;
  logic q;
  initial $display("%b %h", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          missing_format_value.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-037";
          }),
      "missing formatted values need a task-specific diagnostic");

  const auto strobe = parse_text(
      "strobe.sv",
      R"(
module strobe;
  initial begin
    $strobe("later");
    $strobe();
    $strobe;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(strobe.ok(), "literal $strobe tasks must parse");
  const auto& strobe_statements =
      strobe.design.units.front().processes.front().statements;
  require(
      strobe_statements.size() == 3
          && std::all_of(
              strobe_statements.begin(),
              strobe_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline
                    && statement.output_postponed;
              })
          && strobe_statements.front().output_text == "later"
          && strobe_statements[1].output_text.empty()
          && strobe_statements[2].output_text.empty(),
      "literal and empty $strobe HIR");

  const auto unsupported_strobe = parse_text(
      "formatted_strobe.sv",
      R"(
module formatted_strobe;
  logic q;
  initial $strobe("q=%b", q);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      unsupported_strobe.ok()
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_format
              == OutputFormat::Binary
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_postponed
          && unsupported_strobe.design.units.front().processes.front()
                 .statements.front().output_prefix == "q=",
      "formatted $strobe HIR and postponed policy");

  const auto monitor = parse_text(
      "monitor.sv",
      R"(
module monitor;
  initial begin
    $monitor("once");
    $monitor();
    $monitor;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(monitor.ok(), "literal $monitor tasks must parse");
  const auto& monitor_statements =
      monitor.design.units.front().processes.front().statements;
  require(
      monitor_statements.size() == 3
          && std::all_of(
              monitor_statements.begin(),
              monitor_statements.end(),
              [](const Statement& statement) {
                return statement.kind == StatementKind::Display
                    && statement.output_newline
                    && statement.output_postponed;
              })
          && monitor_statements.front().output_text == "once",
      "literal $monitor initial-publication HIR");

  const auto formatted_monitor = parse_text(
      "formatted_monitor.sv",
      R"(
module formatted_monitor;
  logic q;
  initial begin
    $monitor("q=%b t=%t", q);
    $monitoroff;
    $monitoron();
  end
endmodule
)",
      Language::SystemVerilog2017);
  const auto& formatted_monitor_statements =
      formatted_monitor.design.units.front().processes.front().statements;
  require(
      formatted_monitor.ok()
          && formatted_monitor_statements.size() == 3
          && formatted_monitor_statements[0].output_monitor
          && formatted_monitor_statements[0].output_postponed
          && formatted_monitor_statements[0].output_values.size() == 2
          && formatted_monitor_statements[0].output_values[0].format
              == OutputFormat::Binary
          && formatted_monitor_statements[0].output_values[1].format
              == OutputFormat::Time
          && formatted_monitor_statements[1].kind
              == StatementKind::MonitorControl
          && !formatted_monitor_statements[1].monitor_enabled
          && formatted_monitor_statements[2].kind
              == StatementKind::MonitorControl
          && formatted_monitor_statements[2].monitor_enabled,
      "value-sensitive monitor registration and on/off controls");

  const auto numeric = parse_text(
      "numeric_output.sv",
      R"(
module numeric_output;
  initial begin
    $display(42);
    $write(8'h2a);
    $strobe(6'b10_1010);
    $display(8'shff);
    $write(4'sb0111);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(numeric.ok(), "constant numeric output tasks must parse");
  const auto& numeric_statements =
      numeric.design.units.front().processes.front().statements;
  require(
      numeric_statements.size() == 5
          && numeric_statements[0].output_text == "42"
          && numeric_statements[0].output_newline
          && numeric_statements[1].output_text == "42"
          && !numeric_statements[1].output_newline
          && numeric_statements[2].output_text == "42"
          && numeric_statements[2].output_postponed
          && numeric_statements[3].output_text == "-1"
          && numeric_statements[3].output_newline
          && numeric_statements[4].output_text == "7"
          && !numeric_statements[4].output_newline,
      "unsigned and signed numeric output literal folding");

  const auto unknown_numeric = parse_text(
      "unknown_numeric_output.sv",
      R"(
module unknown_numeric_output;
  initial $display(4'bx001);
endmodule
)",
      Language::SystemVerilog2017);
  require(
      std::ranges::any_of(
          unknown_numeric.diagnostics,
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-037";
          }),
      "unknown numeric output literals need a targeted diagnostic");
}

void test_systemverilog_random_functions() {
  const auto parsed = parse_text(
      "random_functions.sv",
      R"(
module random_functions;
  logic [31:0] a, b, c, d, e, f, u;
  initial begin
    a = $urandom;
    b = $urandom();
    c = $random;
    d = $random();
    e = $urandom_range(9);
    f = $urandom_range(9, 3);
    u = $urandom_range(4'bx);
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "random system functions must parse");
  const auto& statements =
      parsed.design.units.front().processes.front().statements;
  require(
      statements.size() == 7
          && std::ranges::all_of(
              statements,
              [](const Statement& statement) {
                return statement.kind == StatementKind::Assignment
                    && statement.value.kind == ExpressionKind::Call;
              })
          && statements[0].value.text == "$urandom"
          && statements[0].value.operands.empty()
          && statements[1].value.text == "$urandom"
          && statements[1].value.operands.empty()
          && statements[2].value.text == "$random"
          && statements[2].value.operands.empty()
          && statements[3].value.text == "$random"
          && statements[3].value.operands.empty()
          && statements[4].value.text == "$urandom_range"
          && statements[4].value.operands.size() == 1
          && statements[5].value.text == "$urandom_range"
          && statements[5].value.operands.size() == 2
          && statements[6].value.text == "$urandom_range"
          && statements[6].value.operands.size() == 1
          && statements[6].value.operands[0].kind
              == ExpressionKind::LogicLiteral,
      "bare/empty random calls and range arguments in typed HIR");
}

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
    test_vhdl_falling_edge_guard();
    test_vhdl_instance_diagnostics();
    test_vhdl_generics();
    test_vhdl_record_types();
    test_vhdl_select_and_concatenation_expressions();
    test_signed_type_and_expression_nodes();
    test_vhdl_runtime_integer_nodes();
    test_exponentiation_expression_nodes();
    test_systemverilog_procedural_updates();
    test_systemverilog_final_procedures();
    test_verilog_stop_task();
    test_vhdl_conditional_assignments();
    test_vhdl_array_attributes();
    test_vhdl_selected_assignments();
    test_vhdl_delay_mechanisms();
    test_vhdl_ordered_waveforms();
    test_vhdl_case_statements();
    test_vhdl_sequential_for_loops();
    test_systemverilog_vertical_slice();
    test_systemverilog_preprocessor();
    test_systemverilog_line_directive();
    test_non_ansi_verilog_ports();
    test_diagnostics_and_spans();
    test_vhdl_context_diagnostics();
    test_vhdl_package_constants();
    test_ignored_initializers_are_rejected();
    test_duplicate_declarations_are_rejected();
    test_systemverilog_timescale_context();
    test_systemverilog_time_declarations();
    test_systemverilog_delay_triples();
    test_systemverilog_procedural_assignment_controls();
    test_systemverilog_compiler_directives();
    test_systemverilog_parameters();
    test_systemverilog_packages();
    test_immediate_assertions();
    test_vhdl_literal_report();
    test_process_variable_declarations();
    test_systemverilog_procedural_block_scopes();
    test_procedural_wait_statements();
    test_wildcard_and_always_comb_processes();
    test_systemverilog_case_statements();
    test_systemverilog_procedural_for_loops();
    test_verilog_repeat_statements();
    test_runtime_loop_statements();
    test_loop_control_statements();
    test_systemverilog_do_while_statements();
    test_systemverilog_conditional_expression();
    test_systemverilog_comparison_expressions();
    test_systemverilog_arithmetic_expressions();
    test_gate_primitives();
    test_systemverilog_select_and_concatenation_expressions();
    test_conditional_statement_trees();
    test_conditional_generate_hierarchy();
    test_systemverilog_named_events();
    test_verilog_literal_display();
    test_systemverilog_random_functions();
    std::cout << "frontend tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "frontend test failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
