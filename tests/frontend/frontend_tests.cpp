// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <array>
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

  bool saw_generic_map = false;
  std::size_t complex_actuals = 0;
  for (const auto& diagnostic : result.diagnostics) {
    saw_generic_map =
        saw_generic_map ||
        diagnostic.code == "FSIM-VHDL-UNSUPPORTED-009";
    if (diagnostic.code == "FSIM-VHDL-UNSUPPORTED-010") {
      ++complex_actuals;
      require(diagnostic.span.source_name == "instances.vhd",
              "unsupported actual diagnostic source span");
    }
  }
  require(saw_generic_map, "generic map must have a targeted diagnostic");
  require(complex_actuals == 2,
          "indexed and open actuals need targeted diagnostics");
  require(architecture->instances[1].connections.size() == 2 &&
              architecture->instances[1].connections[0].value.kind ==
                  ExpressionKind::Invalid &&
              architecture->instances[1].connections[1].value.kind ==
                  ExpressionKind::Invalid,
          "unsupported actuals must not appear as valid expressions");
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
end context shared;
entity context_user is
end entity;
)",
      Language::Vhdl2008);
  require(!declaration.ok(),
          "an unimplemented context declaration must be rejected");
  require(
      std::any_of(
          declaration.diagnostics.begin(),
          declaration.diagnostics.end(),
          [](const Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-VHDL-UNSUPPORTED-015";
          }),
      "context declaration needs a targeted diagnostic");
  require(declaration.design.units.size() == 1,
          "context-declaration recovery must retain the following unit");

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

  const auto default_nettype = parse_text(
      "default_nettype.sv",
      "`default_nettype none\nmodule guarded; endmodule\n",
      Language::SystemVerilog2017);
  require(!default_nettype.ok(),
          "unimplemented `default_nettype semantics must not be ignored");
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
  logic result;
  always_comb begin
    result = !lhs;
    result = lhs != rhs;
    result = lhs < rhs;
    result = lhs <= rhs;
    result = lhs > rhs;
    result = lhs >= rhs;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      result.ok(), "SystemVerilog comparison expressions must parse");
  const auto& statements =
      result.design.units.front().processes.front().statements;
  require(
      statements.size() == 6
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
    test_systemverilog_vertical_slice();
    test_non_ansi_verilog_ports();
    test_diagnostics_and_spans();
    test_vhdl_context_diagnostics();
    test_ignored_initializers_are_rejected();
    test_duplicate_declarations_are_rejected();
    test_systemverilog_timescale_context();
    test_immediate_assertions();
    test_process_variable_declarations();
    test_procedural_wait_statements();
    test_wildcard_and_always_comb_processes();
    test_systemverilog_case_statements();
    test_systemverilog_conditional_expression();
    test_systemverilog_comparison_expressions();
    std::cout << "frontend tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "frontend test failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
