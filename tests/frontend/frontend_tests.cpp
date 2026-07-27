// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
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
    std::cout << "frontend tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << "frontend test failure: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
