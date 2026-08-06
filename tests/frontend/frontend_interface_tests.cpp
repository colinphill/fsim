// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

}  // namespace

void test_systemverilog_interfaces() {
  const auto parsed = parse_text(
      "interfaces.sv",
      R"(interface bus_if #(parameter int WIDTH = 8);
  logic [WIDTH-1:0] data;
  logic valid;
  logic ready;
  logic cfg;
  function automatic logic [WIDTH-1:0] sample();
    return data;
  endfunction
  task automatic clear();
    data = '0;
  endtask
  clocking cb @(posedge ready);
    input #0 data;
  endclocking
  modport initiator(output data, valid, input ready,
                    import function sample, import task clear,
                    ref cfg, clocking cb),
          target(input data, valid, output ready),
          service(export function sample, export task clear);
endinterface : bus_if

module interface_user(bus_if.initiator bus, interface monitor);
  bus_if #(.WIDTH(16)) link[1:0]();
  bus_if concrete();
  virtual bus_if.initiator selected = concrete, spare;
  virtual interface bus_if generic;
  interface class observation_contract;
  endclass
  class observer implements observation_contract;
    virtual bus_if.initiator view;
  endclass
  assign bus.data = '0;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "bounded interface and modport HIR must parse");
  require(
      parsed.design.units.size() == 2
          && parsed.design.units[0].kind
              == UnitKind::SystemVerilogInterface
          && parsed.design.units[1].kind
              == UnitKind::VerilogModule,
      "interfaces retain a distinct design-unit kind");
  const auto& interface_unit = parsed.design.units[0];
  require(
      interface_unit.parameters.size() == 1
          && interface_unit.signals.size() == 4
          && interface_unit.systemverilog_clocking_blocks.size() == 1
          && interface_unit.systemverilog_modports.size() == 3
          && interface_unit.systemverilog_modports[0].name
              == "initiator"
          && interface_unit.systemverilog_modports[0].members.size() == 7
          && interface_unit.systemverilog_modports[0].members[6].kind
              == SystemVerilogModportMemberKind::Clocking
          && interface_unit.systemverilog_modports[0].members[0].name
              == "data"
          && interface_unit.systemverilog_modports[0].members[0].direction
              == PortDirection::Output
          && interface_unit.systemverilog_modports[0].members[2].direction
              == PortDirection::Input
          && interface_unit.systemverilog_modports[0].members[3].kind
              == SystemVerilogModportMemberKind::FunctionImport
          && interface_unit.systemverilog_modports[0].members[4].kind
              == SystemVerilogModportMemberKind::TaskImport
          && interface_unit.systemverilog_modports[0].members[5].direction
              == PortDirection::Ref
          && interface_unit.systemverilog_modports[2].members[0].kind
              == SystemVerilogModportMemberKind::FunctionExport,
      "modports retain signal directions and callable access entries");
  const auto& module = parsed.design.units[1];
  require(
      module.ports.size() == 2
          && module.ports[0].interface_type == "bus_if"
          && module.ports[0].modport == "initiator"
          && module.ports[0].type.spelling == "interface"
          && module.ports[1].interface_type.empty()
          && module.ports[1].modport.empty()
          && module.signals.empty()
          && module.instances.size() == 2
          && module.instances[0].unit_name == "bus_if"
          && module.instances[0].array_indices
              == std::vector<std::int64_t>({1, 0})
          && module.instances[0].parameter_overrides.size() == 1
          && module.variables.size() == 3
          && module.variables[0].name == "selected"
          && module.variables[0].type.systemverilog_virtual_interface
          && module.variables[0].type.systemverilog_interface_type
              == "bus_if"
          && module.variables[0].type.systemverilog_interface_modport
              == "initiator"
          && module.variables[0].initializer
          && module.variables[0].initializer->text == "concrete"
          && module.variables[2].type.systemverilog_interface_modport.empty()
          && module.systemverilog_classes.size() == 2
          && module.systemverilog_classes[0].is_interface
          && module.systemverilog_classes[1].implemented_interfaces.size() == 1
          && module.systemverilog_classes[1].implemented_interfaces[0].name
              == "observation_contract"
          && module.systemverilog_classes[1].properties.size() == 1
          && module.systemverilog_classes[1].properties[0]
                 .declaration.type.systemverilog_virtual_interface,
      "interface ports and parameterized interface instances retain HIR");

  const auto invalid = parse_text(
      "invalid_interfaces.sv",
      R"(interface invalid_if;
  logic value;
  function automatic logic sample(); return value; endfunction
  modport broken(value),
          unknown(input missing),
          duplicate(input value, value),
          duplicate(output value),
          callable(import missing),
          wrong(import task sample),
          clocked(clocking value);
endinterface
module invalid_module;
  modport illegal(input missing);
  virtual invalid_if duplicate;
  virtual invalid_if duplicate;
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok()
          && has_code("FSIM-SV-SEM-115")
          && has_code("FSIM-SV-SEM-116")
          && has_code("FSIM-SV-SEM-117")
          && has_code("FSIM-SV-SEM-118")
          && has_code("FSIM-SV-SEM-119")
          && has_code("FSIM-SV-SEM-122")
          && has_code("FSIM-SV-SEM-123")
          && has_code("FSIM-SV-SEM-189")
          && has_code("FSIM-SV-SEM-190"),
      "invalid interface and modport forms have stable diagnostics");
}

void test_systemverilog_programs() {
  const auto parsed = parse_text(
      "programs.sv",
      R"(program driver #(parameter int WIDTH = 8) (
  input logic clock,
  output logic [WIDTH-1:0] data
);
  typedef logic [WIDTH-1:0] word_t;
  integer count;
  function automatic logic ready();
    return count != 0;
  endfunction
  task automatic drive(input logic [WIDTH-1:0] value);
    data = value;
  endtask
  initial begin
    count = 1;
    drive('1);
  end
  final count = 0;
endprogram : driver

module holder;
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "bounded program blocks must parse");
  require(
      parsed.design.units.size() == 2
          && parsed.design.units[0].kind
              == UnitKind::SystemVerilogProgram
          && parsed.design.units[1].kind
              == UnitKind::VerilogModule,
      "program blocks retain a distinct design-unit kind");
  const auto& program = parsed.design.units[0];
  require(
      program.name == "driver"
          && program.parameters.size() == 1
          && program.ports.size() == 2
          && program.type_aliases.size() == 1
          && program.functions.size() == 1
          && program.tasks.size() == 1
          && program.processes.size() == 2,
      "program declarations and processes remain owned by their program");

  const auto mismatched = parse_text(
      "mismatched-program-label.sv",
      "program opening; endprogram : closing\n",
      Language::SystemVerilog2017);
  require(
      !mismatched.ok()
          && std::ranges::any_of(
              mismatched.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-129";
              }),
      "mismatched program end names are rejected exactly");
}

void test_systemverilog_clocking_blocks() {
  const auto parsed = parse_text(
      "clocking-blocks.sv",
      R"(module module_owner(
  input logic clock,
  input logic observed,
  output logic driven
);
  clocking module_cb @(posedge clock);
    default input #1step output negedge #2ns;
    input #3ns observed;
    output posedge #4ns driven;
  endclocking : module_cb
endmodule

interface interface_owner(input logic clock);
  logic request;
  logic response;
  clocking interface_cb @(negedge clock);
    input sampled_request = request;
    output response;
    inout request;
  endclocking
endinterface

program program_owner(
  input logic clock,
  input logic observed,
  output logic driven
);
  clocking program_cb @(posedge clock);
    input observed;
    output driven;
  endclocking : program_cb
  default clocking program_cb;
  initial begin
    ##2;
    ##(3 + 1);
  end
endprogram
)",
      Language::SystemVerilog2017);
  require(
      parsed.ok() && parsed.design.units.size() == 3,
      "module, interface, and program clocking blocks must parse");

  const auto& module = parsed.design.units[0];
  const auto& interface_unit = parsed.design.units[1];
  const auto& program = parsed.design.units[2];
  require(
      module.kind == UnitKind::VerilogModule
          && module.systemverilog_clocking_blocks.size() == 1
          && module.systemverilog_clocking_blocks[0].name
              == "module_cb"
          && module.systemverilog_clocking_blocks[0].event.size() == 1
          && module.systemverilog_clocking_blocks[0].event[0].edge
              == EdgeKind::Positive
          && module.systemverilog_clocking_blocks[0].event[0].signal
              == "clock"
          && module.systemverilog_clocking_blocks[0].default_input_skew
          && module.systemverilog_clocking_blocks[0]
                 .default_input_skew->one_step
          && module.systemverilog_clocking_blocks[0].default_output_skew
          && module.systemverilog_clocking_blocks[0]
                 .default_output_skew->edge
              == EdgeKind::Negative
          && module.systemverilog_clocking_blocks[0]
                 .default_output_skew->delay
          && module.systemverilog_clocking_blocks[0]
                 .default_output_skew->delay->magnitude
              == 2
          && module.systemverilog_clocking_blocks[0]
                 .default_output_skew->delay->unit
              == "ns"
          && module.systemverilog_clocking_blocks[0].signals.size() == 2
          && module.systemverilog_clocking_blocks[0].signals[0].direction
              == PortDirection::Input
          && module.systemverilog_clocking_blocks[0].signals[0].skew
          && module.systemverilog_clocking_blocks[0]
                 .signals[0].skew->delay
          && module.systemverilog_clocking_blocks[0]
                 .signals[0].skew->delay->magnitude
              == 3
          && module.systemverilog_clocking_blocks[0].signals[1].direction
              == PortDirection::Output
          && module.systemverilog_clocking_blocks[0].signals[1].skew
          && module.systemverilog_clocking_blocks[0].signals[1].skew->edge
              == EdgeKind::Positive
          && module.systemverilog_clocking_blocks[0]
                 .signals[1].skew->delay
          && module.systemverilog_clocking_blocks[0]
                 .signals[1].skew->delay->magnitude
              == 4,
      "module clocking ownership retains default and per-signal skews");
  require(
      interface_unit.kind == UnitKind::SystemVerilogInterface
          && interface_unit.systemverilog_clocking_blocks.size() == 1
          && interface_unit.systemverilog_clocking_blocks[0].name
              == "interface_cb"
          && interface_unit.systemverilog_clocking_blocks[0].event[0].edge
              == EdgeKind::Negative
          && interface_unit.systemverilog_clocking_blocks[0].signals.size()
              == 3
          && interface_unit.systemverilog_clocking_blocks[0].signals[0].name
              == "sampled_request"
          && interface_unit.systemverilog_clocking_blocks[0]
                 .signals[0].expression
          && interface_unit.systemverilog_clocking_blocks[0]
                 .signals[0].expression->text
              == "request"
          && interface_unit.systemverilog_clocking_blocks[0]
                 .signals[2].direction
              == PortDirection::Inout,
      "interface clocking aliases and inout declarations remain unit-owned");
  require(
      program.kind == UnitKind::SystemVerilogProgram
          && program.systemverilog_clocking_blocks.size() == 1
          && program.systemverilog_clocking_blocks[0].name
              == "program_cb"
          && program.systemverilog_default_clocking_block
          && *program.systemverilog_default_clocking_block
              == "program_cb"
          && program.systemverilog_clocking_blocks[0].signals.size() == 2
          && program.processes.size() == 1
          && program.processes[0].statements.size() == 2
          && program.processes[0].statements[0].clocking_cycle_delay
          && program.processes[0].statements[0]
                 .clocking_cycle_count.text
              == "2"
          && program.processes[0].statements[1].clocking_cycle_delay
          && program.processes[0].statements[1]
                 .clocking_cycle_count.kind
              == ExpressionKind::Binary
          && program.processes[0].statements[1]
                 .clocking_cycle_count.text
              == "+",
      "program clocking declarations and cycle delays remain unit-owned");

  const auto invalid = parse_text(
      "invalid-clocking-blocks.sv",
      R"(module invalid;
  logic clock;
  logic value;
  clocking duplicate @(posedge clock);
    default input;
    default output #1ns output #2ns;
    inout #1ns value;
    input value, value;
    sideways value;
    input missing;
  endclocking : wrong
  clocking duplicate @(posedge clock);
    input value;
  endclocking
  default clocking missing;
  default clocking duplicate;
  default clocking duplicate;
  default sideways;
endmodule
)",
      Language::SystemVerilog2017);
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic& diagnostic) {
          return diagnostic.code == code;
        });
  };
  require(
      !invalid.ok()
          && has_code("FSIM-SV-SEM-176")
          && has_code("FSIM-SV-SEM-177")
          && has_code("FSIM-SV-SEM-178")
          && has_code("FSIM-SV-SEM-179")
          && has_code("FSIM-SV-SEM-180")
          && has_code("FSIM-SV-SEM-181")
          && has_code("FSIM-SV-SEM-182")
          && has_code("FSIM-SV-SEM-183")
          && has_code("FSIM-SV-SEM-185")
          && has_code("FSIM-SV-SEM-186")
          && has_code("FSIM-SV-SEM-187"),
      "invalid clocking ownership, skew, and declarations are diagnosed");
}

}  // namespace fsim::tests::frontend
