// SPDX-License-Identifier: Apache-2.0

#include "fsim/frontend/design.hpp"
#include "fsim/frontend/design_core.hpp"
#include "fsim/frontend/diagnostic.hpp"
#include "fsim/frontend/parser.hpp"
#include "fsim/frontend/source.hpp"
#include "fsim/frontend/token.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <cstddef>
#include <span>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

    void require(const bool condition, const std::string_view message)
    {
        if (!condition) {
            throw std::runtime_error(std::string { message });
        }
    }

} // namespace

void test_systemverilog_interfaces()
{
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
                == std::vector<std::int64_t>({ 1, 0 })
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
            && module.systemverilog_classes[1].properties[0].declaration.type.systemverilog_virtual_interface,
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

void test_systemverilog_programs()
{
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

void test_systemverilog_clocking_blocks()
{
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
                .signals[0]
                .skew->delay
            && module.systemverilog_clocking_blocks[0]
                    .signals[0]
                    .skew->delay->magnitude
                == 3
            && module.systemverilog_clocking_blocks[0].signals[1].direction
                == PortDirection::Output
            && module.systemverilog_clocking_blocks[0].signals[1].skew
            && module.systemverilog_clocking_blocks[0].signals[1].skew->edge
                == EdgeKind::Positive
            && module.systemverilog_clocking_blocks[0]
                .signals[1]
                .skew->delay
            && module.systemverilog_clocking_blocks[0]
                    .signals[1]
                    .skew->delay->magnitude
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
                .signals[0]
                .expression
            && interface_unit.systemverilog_clocking_blocks[0]
                    .signals[0]
                    .expression->text
                == "request"
            && interface_unit.systemverilog_clocking_blocks[0]
                    .signals[2]
                    .direction
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
            && program.processes[0].statements[0].clocking_cycle_count.text
                == "2"
            && program.processes[0].statements[1].clocking_cycle_delay
            && program.processes[0].statements[1].clocking_cycle_count.kind
                == ExpressionKind::Binary
            && program.processes[0].statements[1].clocking_cycle_count.text
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

void test_systemverilog_assertion_declarations()
{
    const auto parsed = parse_text(
        "assertion-declarations.sv",
        R"(module assertion_owner(
  input logic clock,
  input logic reset_n,
  input logic request,
  input logic acknowledge
);
  sequence request_acknowledge(
      int bound = 2,
      sequence tail,
      untyped gate
  );
    int attempt = bound;
    logic sampled, enabled = gate;
    @(posedge clock) attempt >= 0 && request
      ##[1:bound] acknowledge[*2:4];
  endsequence : request_acknowledge

  property follows_request(
      logic enable = 1'b1,
      property override_property
  );
    @(posedge clock) disable iff (!reset_n)
      enable && assertion_pkg::guard && monitor.ready
        |-> request_acknowledge(3);
  endproperty : follows_request

  checker bus_checker(
      input logic checker_clock,
      output bit failed
  );
    default clocking cb @(posedge checker_clock); endclocking
    assert property (follows_request(1));
  endchecker : bus_checker
endmodule
)",
        Language::SystemVerilog2017);
    require(
        parsed.ok() && parsed.design.units.size() == 1,
        "sequence, property, and checker declarations must parse");
    const auto& declarations = parsed.design.units[0].systemverilog_assertion_declarations;
    const auto has_token = [](
                               const std::vector<Token>& tokens,
                               const std::string_view text) {
        return std::ranges::any_of(
            tokens,
            [&](const Token& token) { return token.text == text; });
    };
    const auto has_reference = [](
                                   const SystemVerilogAssertionDeclaration& declaration,
                                   const std::string_view name,
                                   const SystemVerilogAssertionReferenceKind kind) {
        return std::ranges::any_of(
            declaration.references,
            [&](const SystemVerilogAssertionReference& reference) {
                return reference.canonical_name == name
                    && reference.kind == kind
                    && reference.span.source_name
                    == "assertion-declarations.sv";
            });
    };
    require(
        declarations.size() == 3
            && declarations[0].kind
                == SystemVerilogAssertionDeclarationKind::Sequence
            && declarations[0].name == "request_acknowledge"
            && has_token(declarations[0].header_tokens, "bound")
            && has_token(declarations[0].header_tokens, "2")
            && declarations[0].formals.size() == 3
            && declarations[0].formals[0].kind
                == SystemVerilogAssertionFormalKind::Value
            && declarations[0].formals[0].name == "bound"
            && has_token(declarations[0].formals[0].type_tokens, "int")
            && has_token(declarations[0].formals[0].default_tokens, "2")
            && declarations[0].formals[1].kind
                == SystemVerilogAssertionFormalKind::Sequence
            && declarations[0].formals[1].name == "tail"
            && declarations[0].formals[2].kind
                == SystemVerilogAssertionFormalKind::Untyped
            && declarations[0].formals[2].name == "gate"
            && declarations[0].local_variables.size() == 3
            && declarations[0].local_variables[0].name == "attempt"
            && has_token(
                declarations[0].local_variables[0].type_tokens,
                "int")
            && has_token(
                declarations[0].local_variables[0].initializer_tokens,
                "bound")
            && declarations[0].local_variables[1].name == "sampled"
            && declarations[0].local_variables[2].name == "enabled"
            && has_token(
                declarations[0].local_variables[2].initializer_tokens,
                "gate")
            && declarations[0].clock.has_value()
            && has_token(declarations[0].clock->event_tokens, "posedge")
            && has_token(declarations[0].clock->event_tokens, "clock")
            && declarations[0].clock->span.source_name
                == "assertion-declarations.sv"
            && !declarations[0].disable.has_value()
            && has_token(declarations[0].expression_tokens, "request")
            && has_reference(
                declarations[0],
                "bound",
                SystemVerilogAssertionReferenceKind::Formal)
            && has_reference(
                declarations[0],
                "gate",
                SystemVerilogAssertionReferenceKind::Formal)
            && has_reference(
                declarations[0],
                "attempt",
                SystemVerilogAssertionReferenceKind::LocalVariable)
            && has_reference(
                declarations[0],
                "clock",
                SystemVerilogAssertionReferenceKind::DesignUnitObject)
            && has_reference(
                declarations[0],
                "request",
                SystemVerilogAssertionReferenceKind::DesignUnitObject)
            && declarations[0].sequence_expression.has_value()
            && declarations[0].sequence_expression->elements.size() == 2
            && declarations[0].sequence_expression->delays.size() == 1
            && has_token(
                declarations[0].sequence_expression->delays[0].range.minimum_tokens,
                "1")
            && has_token(
                declarations[0].sequence_expression->delays[0].range.maximum_tokens,
                "bound")
            && declarations[0].sequence_expression->elements[1].repetition
                == SystemVerilogSequenceRepetitionKind::Consecutive
            && has_token(
                declarations[0].sequence_expression->elements[1].repetition_range->minimum_tokens,
                "2")
            && has_token(
                declarations[0].sequence_expression->elements[1].repetition_range->maximum_tokens,
                "4")
            && has_token(declarations[0].body_tokens, "request")
            && has_token(declarations[0].body_tokens, "acknowledge")
            && declarations[1].kind
                == SystemVerilogAssertionDeclarationKind::Property
            && declarations[1].name == "follows_request"
            && has_token(declarations[1].header_tokens, "enable")
            && declarations[1].formals.size() == 2
            && declarations[1].formals[0].kind
                == SystemVerilogAssertionFormalKind::Value
            && has_token(
                declarations[1].formals[0].default_tokens,
                "1'b1")
            && declarations[1].formals[1].kind
                == SystemVerilogAssertionFormalKind::Property
            && declarations[1].clock.has_value()
            && has_token(declarations[1].clock->event_tokens, "posedge")
            && has_token(declarations[1].clock->event_tokens, "clock")
            && declarations[1].disable.has_value()
            && has_token(
                declarations[1].disable->condition_tokens,
                "reset_n")
            && declarations[1].disable->span.source_name
                == "assertion-declarations.sv"
            && has_token(declarations[1].expression_tokens, "enable")
            && has_token(
                declarations[1].expression_tokens,
                "request_acknowledge")
            && declarations[1].property_expression.has_value()
            && declarations[1].property_expression->implications.size() == 1
            && declarations[1].property_expression->implications[0].kind
                == SystemVerilogPropertyImplicationKind::Overlapped
            && has_token(
                declarations[1].property_expression->implications[0].antecedent_tokens,
                "enable")
            && has_token(
                declarations[1].property_expression->implications[0].consequent_tokens,
                "request_acknowledge")
            && has_reference(
                declarations[1],
                "assertion_pkg::guard",
                SystemVerilogAssertionReferenceKind::Package)
            && has_reference(
                declarations[1],
                "monitor.ready",
                SystemVerilogAssertionReferenceKind::Hierarchical)
            && has_reference(
                declarations[1],
                "request_acknowledge",
                SystemVerilogAssertionReferenceKind::AssertionDeclaration)
            && has_token(declarations[1].body_tokens, "disable")
            && has_token(
                declarations[1].body_tokens,
                "request_acknowledge")
            && declarations[2].kind
                == SystemVerilogAssertionDeclarationKind::Checker
            && declarations[2].name == "bus_checker"
            && has_token(declarations[2].header_tokens, "checker_clock")
            && declarations[2].formals.size() == 2
            && declarations[2].formals[0].direction
                == PortDirection::Input
            && declarations[2].formals[0].name == "checker_clock"
            && declarations[2].formals[1].direction
                == PortDirection::Output
            && declarations[2].formals[1].name == "failed"
            && has_token(declarations[2].body_tokens, "assert")
            && has_token(declarations[2].body_tokens, "follows_request")
            && declarations[0].name_span.source_name
                == "assertion-declarations.sv"
            && declarations[0].header_span.source_name
                == "assertion-declarations.sv"
            && declarations[0].body_span.source_name
                == "assertion-declarations.sv"
            && declarations[0].span.source_name
                == "assertion-declarations.sv",
        "assertion declaration HIR retains kinds, names, tokens, and spans");

    const auto invalid = parse_text(
        "invalid-assertion-declarations.sv",
        R"(module invalid;
  sequence repeated(int value, bit value);
    int value;
    1;
  endsequence
  property repeated; 1; endproperty
  checker mismatched; endchecker : wrong
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
            && has_code("FSIM-SV-SEM-192")
            && has_code("FSIM-SV-SEM-193")
            && has_code("FSIM-SV-SEM-194")
            && has_code("FSIM-SV-SEM-195"),
        "duplicate formals/locals/declarations and end names are rejected");

    const auto malformed_structure = parse_text(
        "malformed-assertion-structure.sv",
        R"(module malformed_structure;
  sequence bad_header int value; 1; endsequence
  property bad_formals(, int value); 1; endproperty
  sequence bad_local; int = 1; 1; endsequence
  checker bad_checker_local; int = 1; endchecker
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::any_of(
            malformed_structure.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-293";
            })
            && std::ranges::any_of(
                malformed_structure.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-PARSE-294";
                })
            && std::ranges::any_of(
                malformed_structure.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-PARSE-295";
                })
            // A checker variable is an ordinary data declaration (17.7).
            && std::ranges::any_of(
                malformed_structure.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-PARSE-001";
                }),
        "malformed assertion formal and local structure is diagnosed");

    const auto malformed_clock_disable = parse_text(
        "malformed-assertion-clock-disable.sv",
        R"(module malformed_clock_disable;
  property bad_clock; @(posedge clock 1; endproperty
  property bad_disable; disable iff (!reset_n; endproperty
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::any_of(
            malformed_clock_disable.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-296";
            })
            && std::ranges::any_of(
                malformed_clock_disable.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-PARSE-297";
                }),
        "malformed assertion clocks and disable conditions are diagnosed");

    const auto unresolved_reference = parse_text(
        "unresolved-assertion-reference.sv",
        R"(module unresolved_reference(input logic request);
  property unresolved; missing |-> request; endproperty
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::any_of(
            unresolved_reference.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-196";
            }),
        "an unresolved unqualified assertion reference is rejected");

    const auto repetition_forms = parse_text(
        "sequence-repetition-forms.sv",
        R"(module sequence_repetition_forms(input logic request);
  sequence forms;
    request[*] ##0 request[=1:3] ##[0:4] request[->2];
  endsequence
endmodule
)",
        Language::SystemVerilog2017);
    const auto& forms = repetition_forms.design.units[0]
                            .systemverilog_assertion_declarations[0]
                            .sequence_expression;
    require(
        repetition_forms.ok()
            && forms.has_value()
            && forms->elements.size() == 3
            && forms->delays.size() == 2
            && forms->elements[0].repetition
                == SystemVerilogSequenceRepetitionKind::Consecutive
            && forms->elements[1].repetition
                == SystemVerilogSequenceRepetitionKind::Nonconsecutive
            && forms->elements[2].repetition
                == SystemVerilogSequenceRepetitionKind::Goto
            && has_token(forms->delays[0].range.minimum_tokens, "0")
            && forms->delays[0].fusion
            && !forms->delays[1].fusion
            && has_token(forms->delays[1].range.minimum_tokens, "0")
            && has_token(forms->delays[1].range.maximum_tokens, "4"),
        "sequence concatenation owns scalar/ranged delays and repetition forms");

    const auto intersection = parse_text(
        "sequence-intersection.sv",
        R"(module sequence_intersection(
  input logic request,
  input logic acknowledge
);
  sequence both;
    request intersect acknowledge intersect (request && acknowledge);
  endsequence
endmodule
)",
        Language::SystemVerilog2017);
    const auto& intersection_expression = intersection.design.units[0]
                                              .systemverilog_assertion_declarations[0]
                                              .sequence_expression;
    require(
        intersection.ok()
            && intersection_expression.has_value()
            && intersection_expression->intersection_operands.size() == 3
            && has_token(
                intersection_expression->intersection_operands[0].tokens,
                "request")
            && has_token(
                intersection_expression->intersection_operands[1].tokens,
                "acknowledge")
            && intersection_expression->intersection_operands[2]
                    .span.source_name
                == "sequence-intersection.sv",
        "sequence intersection owns every top-level operand and exact span");

    const auto scalar_combinators = parse_text(
        "scalar-sequence-combinators.sv",
        R"(module scalar_sequence_combinators(
  input logic left,
  input logic right
);
  sequence both; left intersect right; endsequence
  sequence held; left throughout right; endsequence
  sequence nested; left within right; endsequence
endmodule
)",
        Language::SystemVerilog2017);
    const auto& scalar_declarations = scalar_combinators.design.units[0]
                                          .systemverilog_assertion_declarations;
    require(
        scalar_combinators.ok()
            && scalar_declarations.size() == 3
            && std::ranges::all_of(
                scalar_declarations,
                [](const SystemVerilogAssertionDeclaration& declaration) {
                    return declaration.local_variables.empty()
                        && declaration.sequence_expression.has_value();
                })
            && scalar_declarations[0].sequence_expression->intersection_operands.size() == 2
            && scalar_declarations[1].sequence_expression->binary_operations.front().kind
                == SystemVerilogSequenceBinaryKind::Throughout
            && scalar_declarations[2].sequence_expression->binary_operations.front().kind
                == SystemVerilogSequenceBinaryKind::Within,
        "scalar sequence combinators are expressions rather than named local "
        "declarations");

    const auto qualified_sequence = parse_text(
        "qualified-sequence.sv",
        R"(module qualified_sequence(
  input logic clock,
  input logic request,
  input logic acknowledge
);
  sequence qualified;
    int attempt;
    @(posedge clock)
      request throughout (request ##1 acknowledge)
        within first_match(request ##[1:2] acknowledge, attempt = 1);
  endsequence
endmodule
)",
        Language::SystemVerilog2017);
    const auto& qualified = qualified_sequence.design.units[0]
                                .systemverilog_assertion_declarations[0]
                                .sequence_expression;
    require(
        qualified_sequence.ok()
            && qualified.has_value()
            && qualified->binary_operations.size() == 2
            && qualified->binary_operations[0].kind
                == SystemVerilogSequenceBinaryKind::Throughout
            && qualified->binary_operations[1].kind
                == SystemVerilogSequenceBinaryKind::Within
            && has_token(
                qualified->binary_operations[0].left_tokens,
                "request")
            && qualified->first_matches.size() == 1
            && has_token(qualified->first_matches[0].sequence_tokens, "request")
            && has_token(
                qualified->first_matches[0].match_item_tokens,
                "attempt")
            && qualified->first_matches[0].span.source_name
                == "qualified-sequence.sv",
        "throughout/within and first_match own operands, items, and spans");

    const auto endpoints = parse_text(
        "sequence-endpoints.sv",
        R"(module sequence_endpoints(
  input logic clock,
  input logic request,
  input logic acknowledge
);
  sequence handshake(int delay = 1);
    @(posedge clock) request ##delay acknowledge;
  endsequence
  property endpoint_property(sequence supplied);
    @(posedge clock)
      handshake(2).triggered && supplied.matched()
        && monitor.remote_sequence.triggered;
  endproperty
endmodule
)",
        Language::SystemVerilog2017);
    const auto& endpoint_declaration = endpoints.design.units[0]
                                           .systemverilog_assertion_declarations[1];
    require(
        endpoints.ok()
            && endpoint_declaration.sequence_endpoints.size() == 3
            && endpoint_declaration.sequence_endpoints[0].kind
                == SystemVerilogSequenceEndpointKind::Triggered
            && endpoint_declaration.sequence_endpoints[0].receiver_name
                == "handshake(2)"
            && !endpoint_declaration.sequence_endpoints[0].method_parentheses
            && endpoint_declaration.sequence_endpoints[1].kind
                == SystemVerilogSequenceEndpointKind::Matched
            && endpoint_declaration.sequence_endpoints[1].method_parentheses
            && endpoint_declaration.sequence_endpoints[1].receiver_name
                == "supplied"
            && endpoint_declaration.sequence_endpoints[2].receiver_name
                == "monitor.remote_sequence"
            && endpoint_declaration.sequence_endpoints[2].span.source_name
                == "sequence-endpoints.sv",
        "matched/triggered endpoints own receivers, calls, kinds, and spans");

    const auto property_implications = parse_text(
        "property-implications.sv",
        R"(module property_implications(
  input logic clock,
  input logic request,
  input logic acknowledge
);
  property delayed;
    @(posedge clock) request |=> ##[1:3] acknowledge;
  endproperty
  property fused;
    @(posedge clock) request |-> ##0 acknowledge;
  endproperty
endmodule
)",
        Language::SystemVerilog2017);
    const auto& delayed_property = property_implications.design.units[0]
                                       .systemverilog_assertion_declarations[0]
                                       .property_expression;
    const auto& fused_property = property_implications.design.units[0]
                                     .systemverilog_assertion_declarations[1]
                                     .property_expression;
    require(
        property_implications.ok()
            && delayed_property.has_value()
            && fused_property.has_value(),
        "property implication fixtures parse and retain expression HIR");
    require(
        delayed_property->implications.size() == 1
            && fused_property->implications.size() == 1
            && delayed_property->implications[0].kind
                == SystemVerilogPropertyImplicationKind::Nonoverlapped
            && fused_property->implications[0].kind
                == SystemVerilogPropertyImplicationKind::Overlapped,
        "property implications own overlapped and nonoverlapped operands");
    require(
        delayed_property->delays.size() == 1
            && has_token(
                delayed_property->delays[0].range.minimum_tokens,
                "1")
            && has_token(
                delayed_property->delays[0].range.maximum_tokens,
                "3")
            && !delayed_property->delays[0].fusion,
        "a ranged property consequent delay owns both bounds");
    require(
        fused_property->delays.size() == 1
            && fused_property->delays[0].fusion
            && fused_property->span.source_name
                == "property-implications.sv",
        "a scalar zero property delay owns explicit fusion and source span");

    const auto property_temporal = parse_text(
        "property-temporal.sv",
        R"(module property_temporal(
  input logic clock,
  input logic request,
  input logic acknowledge
);
  property plain_until;
    @(posedge clock) request until acknowledge;
  endproperty
  property strong_until;
    @(posedge clock) request s_until acknowledge;
  endproperty
  property inclusive_until;
    @(posedge clock) request until_with acknowledge;
  endproperty
  property strong_inclusive_until;
    @(posedge clock) request s_until_with acknowledge;
  endproperty
  property counted_next;
    @(posedge clock) nexttime[2] request;
  endproperty
  property strong_next;
    @(posedge clock) s_nexttime acknowledge;
  endproperty
endmodule
)",
        Language::SystemVerilog2017);
    const auto& temporal_declarations = property_temporal.design.units[0]
                                            .systemverilog_assertion_declarations;
    require(
        property_temporal.ok()
            && temporal_declarations.size() == 6
            && temporal_declarations[0].property_expression->until_operations[0].kind
                == SystemVerilogPropertyUntilKind::Until
            && temporal_declarations[1].property_expression->until_operations[0].kind
                == SystemVerilogPropertyUntilKind::StrongUntil
            && temporal_declarations[2].property_expression->until_operations[0].kind
                == SystemVerilogPropertyUntilKind::UntilWith
            && temporal_declarations[3].property_expression->until_operations[0].kind
                == SystemVerilogPropertyUntilKind::StrongUntilWith
            && has_token(
                temporal_declarations[0].property_expression->until_operations[0].left_tokens,
                "request")
            && has_token(
                temporal_declarations[0].property_expression->until_operations[0].right_tokens,
                "acknowledge"),
        "all until variants own typed left and right property operands");
    require(
        temporal_declarations[4].property_expression->nexttimes[0].kind
                == SystemVerilogPropertyNexttimeKind::Nexttime
            && has_token(
                temporal_declarations[4].property_expression->nexttimes[0].count_tokens,
                "2")
            && has_token(
                temporal_declarations[4].property_expression->nexttimes[0].operand_tokens,
                "request")
            && temporal_declarations[5].property_expression->nexttimes[0].kind
                == SystemVerilogPropertyNexttimeKind::StrongNexttime
            && temporal_declarations[5].property_expression->nexttimes[0].count_tokens.empty()
            && temporal_declarations[5].property_expression->nexttimes[0].span.source_name
                == "property-temporal.sv",
        "nexttime variants own optional counts, operands, kinds, and spans");

    const auto property_recurrence = parse_text(
        "property-recurrence.sv",
        R"(module property_recurrence(
  input logic clock,
  input logic request,
  input logic acknowledge
);
  property plain_always;
    @(posedge clock) always request;
  endproperty
  property ranged_always;
    @(posedge clock) s_always[1:3] request;
  endproperty
  property ranged_eventually;
    @(posedge clock) eventually[2:4] acknowledge;
  endproperty
  property strong_eventually;
    @(posedge clock) s_eventually[1:5] acknowledge;
  endproperty
  property strong_sequence;
    @(posedge clock) strong(request ##1 acknowledge);
  endproperty
  property weak_sequence;
    @(posedge clock) weak(request ##[1:2] acknowledge);
  endproperty
endmodule
)",
        Language::SystemVerilog2017);
    const auto& recurrence_declarations = property_recurrence.design.units[0]
                                              .systemverilog_assertion_declarations;
    require(
        property_recurrence.ok()
            && recurrence_declarations.size() == 6
            && recurrence_declarations[0].property_expression->recurrences[0].kind
                == SystemVerilogPropertyRecurrenceKind::Always
            && !recurrence_declarations[0].property_expression->recurrences[0].range.has_value()
            && recurrence_declarations[1].property_expression->recurrences[0].kind
                == SystemVerilogPropertyRecurrenceKind::StrongAlways
            && has_token(
                recurrence_declarations[1].property_expression->recurrences[0].range->minimum_tokens,
                "1")
            && has_token(
                recurrence_declarations[1].property_expression->recurrences[0].range->maximum_tokens,
                "3")
            && recurrence_declarations[2].property_expression->recurrences[0].kind
                == SystemVerilogPropertyRecurrenceKind::Eventually
            && recurrence_declarations[3].property_expression->recurrences[0].kind
                == SystemVerilogPropertyRecurrenceKind::StrongEventually
            && recurrence_declarations[3].property_expression->recurrences[0].span.source_name
                == "property-recurrence.sv",
        "always/eventually variants own typed operands, ranges, and spans");
    require(
        recurrence_declarations[4].property_expression->sequence_strengths[0].kind
                == SystemVerilogPropertySequenceStrengthKind::Strong
            && has_token(
                recurrence_declarations[4].property_expression->sequence_strengths[0].sequence_tokens,
                "request")
            && recurrence_declarations[5].property_expression->sequence_strengths[0].kind
                == SystemVerilogPropertySequenceStrengthKind::Weak
            && has_token(
                recurrence_declarations[5].property_expression->sequence_strengths[0].sequence_tokens,
                "acknowledge")
            && recurrence_declarations[5].property_expression->sequence_strengths[0].span.source_name
                == "property-recurrence.sv",
        "strong/weak wrappers own sequence operands, kinds, and spans");

    const auto property_aborts = parse_text(
        "property-aborts.sv",
        R"(module property_aborts(
  input logic clock,
  input logic reset,
  input logic enable,
  input logic request
);
  property async_accept;
    @(posedge clock) accept_on(reset || !enable) request;
  endproperty
  property async_reject;
    @(posedge clock) reject_on(reset) request;
  endproperty
  property synchronous_accept;
    @(posedge clock) sync_accept_on(reset) request;
  endproperty
  property synchronous_reject;
    @(posedge clock) sync_reject_on(reset) request;
  endproperty
endmodule
)",
        Language::SystemVerilog2017);
    const auto& abort_declarations = property_aborts.design.units[0]
                                         .systemverilog_assertion_declarations;
    require(
        property_aborts.ok()
            && abort_declarations.size() == 4
            && abort_declarations[0].property_expression->aborts[0].outcome
                == SystemVerilogPropertyAbortOutcome::VacuousSuccess
            && !abort_declarations[0].property_expression->aborts[0].synchronous
            && has_token(
                abort_declarations[0].property_expression->aborts[0].condition_tokens,
                "enable")
            && has_token(
                abort_declarations[0].property_expression->aborts[0].property_tokens,
                "request")
            && abort_declarations[1].property_expression->aborts[0].outcome
                == SystemVerilogPropertyAbortOutcome::Failure
            && !abort_declarations[1].property_expression->aborts[0].synchronous
            && abort_declarations[2].property_expression->aborts[0].outcome
                == SystemVerilogPropertyAbortOutcome::VacuousSuccess
            && abort_declarations[2].property_expression->aborts[0].synchronous
            && abort_declarations[3].property_expression->aborts[0].outcome
                == SystemVerilogPropertyAbortOutcome::Failure
            && abort_declarations[3].property_expression->aborts[0].synchronous
            && abort_declarations[3].property_expression->aborts[0].span.source_name
                == "property-aborts.sv",
        "accept/reject aborts own synchronization, vacuity, operands, and spans");

    const auto malformed_sequence = parse_text(
        "malformed-sequence-structure.sv",
        R"(module malformed_sequence(input logic request);
  sequence bad_delay; request ## ; endsequence
  sequence bad_element; request ##1; endsequence
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::any_of(
            malformed_sequence.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-298";
            })
            && std::ranges::any_of(
                malformed_sequence.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-PARSE-299";
                }),
        "malformed sequence concatenation structure is diagnosed");

    const auto malformed_intersection = parse_text(
        "malformed-sequence-intersection.sv",
        R"(module malformed_intersection(input logic request);
  sequence missing_left; @(posedge request) intersect request; endsequence
  sequence missing_right; @(posedge request) request intersect; endsequence
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_intersection.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-300";
            })
            == 2,
        "empty sequence intersection operands are rejected exactly");

    const auto malformed_qualified_sequence = parse_text(
        "malformed-qualified-sequence.sv",
        R"(module malformed_qualified_sequence(input logic request);
  sequence missing_left; @(posedge request) throughout request; endsequence
  sequence missing_right; @(posedge request) request within; endsequence
  sequence empty_first; @(posedge request) first_match(); endsequence
  sequence open_first; @(posedge request) first_match(request; endsequence
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_qualified_sequence.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-301";
            })
            == 4,
        "malformed throughout/within and first_match operands reject exactly");

    const auto malformed_endpoints = parse_text(
        "malformed-sequence-endpoints.sv",
        R"(module malformed_endpoints(input logic request);
  sequence receiver; @(posedge request) request; endsequence
  property missing_receiver; @(posedge request) matched; endproperty
  property endpoint_arguments;
    @(posedge request) receiver.triggered(1);
  endproperty
  property wrong_receiver; @(posedge request) request.matched; endproperty
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_endpoints.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-302";
            }) == 2
            && std::ranges::any_of(
                malformed_endpoints.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-197";
                }),
        "malformed endpoint syntax and non-sequence receivers reject exactly");

    const auto malformed_property = parse_text(
        "malformed-property-expression.sv",
        R"(module malformed_property(input logic request);
  property missing_left; @(posedge request) |-> request; endproperty
  property missing_right; @(posedge request) request |=>; endproperty
  property empty_range;
    @(posedge request) request |-> ##[] request;
  endproperty
  property missing_delay;
    @(posedge request) request |-> ##;
  endproperty
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_property.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-303";
            })
            == 4,
        "empty implication operands and malformed property delays reject exactly");

    const auto malformed_temporal = parse_text(
        "malformed-property-temporal.sv",
        R"(module malformed_temporal(input logic request);
  property until_left; @(posedge request) until request; endproperty
  property until_right; @(posedge request) request s_until; endproperty
  property next_operand; @(posedge request) nexttime; endproperty
  property next_count; @(posedge request) s_nexttime[] request; endproperty
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_temporal.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-304";
            })
            == 4,
        "malformed until operands and nexttime count/operand reject exactly");

    const auto malformed_recurrence = parse_text(
        "malformed-property-recurrence.sv",
        R"(module malformed_recurrence(input logic request);
  property always_operand; @(posedge request) always; endproperty
  property eventually_range;
    @(posedge request) s_eventually[] request;
  endproperty
  property strong_operand; @(posedge request) strong(); endproperty
  property weak_balance; @(posedge request) weak(request; endproperty
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_recurrence.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-305";
            })
            == 4,
        "malformed recurrence ranges/operands and strength wrappers reject");

    const auto malformed_aborts = parse_text(
        "malformed-property-aborts.sv",
        R"(module malformed_aborts(input logic reset, request);
  property missing_parens;
    @(posedge request) accept_on reset request;
  endproperty
  property empty_condition;
    @(posedge request) reject_on() request;
  endproperty
  property unbalanced_condition;
    @(posedge request) sync_accept_on(reset;
  endproperty
  property missing_operand;
    @(posedge request) sync_reject_on(reset);
  endproperty
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_aborts.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-306";
            })
            == 4,
        "malformed abort conditions and missing property operands reject");

    const auto concurrent_assertions = parse_text(
        "concurrent-assertions.sv",
        R"(module concurrent_assertions(
  input logic clock,
  input logic request
);
  property requested;
    @(posedge clock) request;
  endproperty
  sequence requested_twice;
    @(posedge clock) request ##1 request;
  endsequence
  request_check: assert property (requested)
    $display("pass"); else $error("fail");
  assume property (requested) else $warning("assume");
  cover property (requested) begin
    $display("covered");
  end
  named_sequence: cover sequence (requested_twice)
    $display("named sequence covered");
  inline_sequence: cover sequence (
    @(posedge clock) disable iff (!request) request ##1 request
  ) $display("inline sequence covered");
  restrict property (requested);
  initial begin
    $assertcontrol(1);
    $asserton(0);
    $assertoff;
    $assertkill;
    $assertpasson;
    $assertpassoff;
    $assertfailon;
    $assertfailoff;
    $assertnonvacuouson;
    $assertvacuousoff;
  end
endmodule
)",
        Language::SystemVerilog2017);
    const auto& directives = concurrent_assertions.design.units[0]
                                 .systemverilog_concurrent_assertions;
    require(
        concurrent_assertions.ok()
            && directives.size() == 6
            && directives[0].kind
                == SystemVerilogConcurrentAssertionKind::Assert
            && directives[0].label == "request_check"
            && directives[0].label_span.source_name
                == "concurrent-assertions.sv"
            && directives[0].has_pass_action
            && has_token(directives[0].pass_action_tokens, "$display")
            && directives[0].pass_action_span.source_name
                == "concurrent-assertions.sv"
            && directives[0].has_failure_action
            && has_token(directives[0].failure_action_tokens, "$error")
            && directives[0].failure_action_span.source_name
                == "concurrent-assertions.sv"
            && directives[1].kind
                == SystemVerilogConcurrentAssertionKind::Assume
            && !directives[1].has_pass_action
            && directives[1].has_failure_action
            && has_token(directives[1].failure_action_tokens, "$warning")
            && directives[2].kind
                == SystemVerilogConcurrentAssertionKind::Cover
            && directives[2].has_pass_action
            && has_token(directives[2].pass_action_tokens, "begin")
            && has_token(directives[2].pass_action_tokens, "end")
            && !directives[2].has_failure_action
            && directives[3].kind
                == SystemVerilogConcurrentAssertionKind::Cover
            && directives[3].form
                == SystemVerilogConcurrentAssertionForm::Sequence
            && directives[3].label == "named_sequence"
            && directives[3].inline_sequence
            && !directives[3].inline_sequence->clock
            && directives[3].inline_sequence->sequence_expression
            && directives[4].kind
                == SystemVerilogConcurrentAssertionKind::Cover
            && directives[4].form
                == SystemVerilogConcurrentAssertionForm::Sequence
            && directives[4].label == "inline_sequence"
            && directives[4].inline_sequence
            && directives[4].inline_sequence->clock
            && directives[4].inline_sequence->disable
            && directives[4].inline_sequence->sequence_expression
            && directives[4].inline_sequence->sequence_expression->delays.size()
                == 1
            && directives[5].kind
                == SystemVerilogConcurrentAssertionKind::Restrict
            && !directives[5].has_pass_action
            && !directives[5].has_failure_action
            && has_token(directives[0].property_tokens, "requested")
            && directives[0].sampling_region
                == SystemVerilogAssertionRegion::Preponed
            && directives[0].evaluation_region
                == SystemVerilogAssertionRegion::Observed
            && directives[0].action_region
                == SystemVerilogAssertionRegion::Reactive
            && directives[5].span.source_name
                == "concurrent-assertions.sv",
        "concurrent directives own property and sequence forms, labels, "
        "actions, structure, and regions");
    const auto& concurrent_processes
        = concurrent_assertions.design.units[0].processes;
    require(
        concurrent_processes.size() == 7
            && !concurrent_processes.front()
                    .systemverilog_concurrent_assertion
            && std::ranges::all_of(
                std::span { concurrent_processes }.subspan(1U),
                &Process::systemverilog_concurrent_assertion),
        "resolved concurrent assertion processes retain explicit execution identity");
    const auto& controls = concurrent_processes[0]
                               .statements;
    require(
        controls.size() == 10
            && controls[0].assertion_control
                == SystemVerilogAssertionControlKind::Control
            && controls[0].task_arguments.size() == 1
            && controls[1].assertion_control
                == SystemVerilogAssertionControlKind::On
            && controls[2].assertion_control
                == SystemVerilogAssertionControlKind::Off
            && controls[3].assertion_control
                == SystemVerilogAssertionControlKind::Kill
            && controls[4].assertion_control
                == SystemVerilogAssertionControlKind::PassOn
            && controls[5].assertion_control
                == SystemVerilogAssertionControlKind::PassOff
            && controls[6].assertion_control
                == SystemVerilogAssertionControlKind::FailOn
            && controls[7].assertion_control
                == SystemVerilogAssertionControlKind::FailOff
            && controls[8].assertion_control
                == SystemVerilogAssertionControlKind::NonvacuousOn
            && controls[9].assertion_control
                == SystemVerilogAssertionControlKind::VacuousOff,
        "procedural assertion controls retain typed policy and arguments");

    const auto malformed_actions = parse_text(
        "malformed-concurrent-actions.sv",
        R"(module malformed_actions(input logic request);
  assert property (request) else
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_actions.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-308";
            })
            == 1,
        "missing concurrent assertion failure action rejects exactly");

    const auto illegal_restrict_action = parse_text(
        "illegal-restrict-action.sv",
        R"(module illegal_restrict_action(input logic request);
  restrict property (request) $display("illegal");
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            illegal_restrict_action.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-198";
            })
            == 1,
        "restrict property actions reject exactly");

    const auto malformed_directives = parse_text(
        "malformed-concurrent-assertions.sv",
        R"(module malformed_directives(input logic request);
  property requested; request; endproperty
  assert (requested);
  assume property requested;
  cover property ();
  restrict property (requested)
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            malformed_directives.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-307";
            })
            == 4,
        "malformed concurrent property syntax rejects exactly");

    const auto illegal_sequence_directive = parse_text(
        "illegal-sequence-directive.sv",
        R"(module illegal_sequence_directive(input logic clock, request);
  assert sequence (@(posedge clock) request);
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            illegal_sequence_directive.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-251";
            })
            == 1,
        "only cover accepts a concurrent sequence directive");

    const auto retained_cover_sequence = [](const StandardRevision revision) {
        return parse_verilog(
            SourceText {
                "cover-sequence-profile.sv",
                R"(module cover_sequence_profile;
  cover sequence (1'b1);
endmodule
)" },
            revision);
    };
    require(
        retained_cover_sequence(StandardRevision::SystemVerilog2005).ok()
            && retained_cover_sequence(StandardRevision::SystemVerilog2023).ok(),
        "cover sequence remains available in its retained and exact 2023 profiles");
    const auto verilog_cover_sequence = retained_cover_sequence(
        StandardRevision::Verilog2005);
    require(
        std::ranges::count_if(
            verilog_cover_sequence.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-348";
            })
            == 1,
        "cover sequence remains unavailable in the Verilog profile");

    const auto supported_formals = parse_text(
        "supported-executable-property-formals.sv",
        R"(module unsupported_formals(input logic request);
  property requested(logic value); value; endproperty
  assert property (requested(request));
endmodule
)",
        Language::SystemVerilog2017);
    require(
        supported_formals.ok()
            && std::ranges::none_of(
                supported_formals.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-199";
                }),
        "executable property value formals and actuals are accepted");

    // Properties with local variables run on the concurrent-property
    // monitor (IEEE 1800-2023 16.10): each directive becomes a monitor
    // process whose tick evaluates the automaton, with the subroutine-call
    // match items run from the process (16.11).
    constexpr std::string_view locals_module_head = R"(module supported_locals(
    input logic clock, request,
    input logic [136:0] wide_value);
)";
    constexpr std::string_view locals_module_body = R"(  task automatic record_match(input int value); endtask
  property requested(logic value);
    int attempt = value;
    logic [136:0] captured = wide_value;
    attempt == value;
  endproperty
  sequence matched_sequence;
    int match_count = 0;
    first_match(request, match_count = 1, match_count += 2,
                match_count++, record_match(match_count));
  endsequence
  property matched_request;
    matched_sequence;
  endproperty
  sequence delayed_matched_sequence;
    int delayed_count = 0;
    first_match(request ##[1:2] request, delayed_count = 1);
  endsequence
  property delayed_matched_request;
    @(posedge clock) delayed_matched_sequence;
  endproperty
  local_property: assert property (requested(request));
  sequence_property: assert property (matched_request);
  delayed_sequence_property: assert property (delayed_matched_request);
endmodule
)";
    const auto statement_tree_any
        = [](const auto& self, const Statement& statement,
              const auto& predicate) -> bool {
        if (predicate(statement)) {
            return true;
        }
        const auto child = [&](const Statement& nested) {
            return self(self, nested, predicate);
        };
        return std::ranges::any_of(statement.statements, child)
            || std::ranges::any_of(statement.else_statements, child)
            || std::ranges::any_of(statement.case_alternatives,
                [&](const auto& alternative) {
                    return std::ranges::any_of(alternative.statements, child);
                });
    };
    const auto process_has = [&](const Process* process,
                                 const auto& predicate) {
        return process != nullptr
            && std::ranges::any_of(process->statements,
                [&](const Statement& statement) {
                    return statement_tree_any(
                        statement_tree_any, statement, predicate);
                });
    };
    const auto is_monitor_tick = [](const Statement& statement) {
        return statement.kind == StatementKind::Assignment
            && statement.value.kind == ExpressionKind::Call
            && statement.value.text == "$__fsim_sva_tick";
    };
    const auto find_process = [](const auto& result, std::string_view name) {
        const Process* found { nullptr };
        if (!result.design.units.empty()) {
            for (const auto& process : result.design.units.front().processes) {
                if (process.name == name) {
                    found = &process;
                }
            }
        }
        return found;
    };
    // Each local is a variable of the monitor process's block under its
    // unique name (`__fsim_lvN`), with its declared type.
    const auto local_widths = [&](const Process* process) {
        std::vector<std::uint64_t> widths;
        (void)process_has(process, [&](const Statement& statement) {
            for (const auto& variable : statement.declarations) {
                if (variable.name.starts_with("__fsim_lv")) {
                    widths.push_back(variable.type.width().value_or(0U));
                }
            }
            return false;
        });
        std::ranges::sort(widths);
        return widths;
    };

    // Without a default clocking, the two directives whose properties give
    // no clock are illegal: no clock can be inferred for them (16.16, the
    // example "illegal if it is not within the scope of a default clocking
    // because no clock can be inferred"; neither is procedural, 16.14.6, and
    // no let, sequence or property formal supplies a clocking event).
    const auto unclocked_locals = parse_text(
        "unclocked-executable-property-locals.sv",
        std::string { locals_module_head } + std::string { locals_module_body },
        Language::SystemVerilog2017);
    std::string unclocked_failure;
    std::vector<std::size_t> no_clock_lines;
    for (const auto& diagnostic : unclocked_locals.diagnostics) {
        unclocked_failure += " " + diagnostic.code + ":" + diagnostic.message;
        if (diagnostic.code == "FSIM-SV-SEM-472") {
            no_clock_lines.push_back(diagnostic.span.begin.line);
        }
    }
    std::ranges::sort(no_clock_lines);
    require(
        no_clock_lines == std::vector<std::size_t> { 25U, 26U }
            && std::ranges::none_of(
                unclocked_locals.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-199"
                        || diagnostic.code == "FSIM-SV-SEM-200";
                }),
        "unclocked properties with local variables report that no clock can be inferred"
            + unclocked_failure);
    require(
        process_has(find_process(unclocked_locals, "delayed_sequence_property"),
            is_monitor_tick),
        "a clocked property with local variables is a monitor process");

    // Under a default clocking every directive has a clock.
    const auto supported_locals = parse_text(
        "supported-executable-property-locals.sv",
        std::string { locals_module_head }
            + "  default clocking @(posedge clock); endclocking\n"
            + std::string { locals_module_body },
        Language::SystemVerilog2017);
    std::string local_failure;
    for (const auto& diagnostic : supported_locals.diagnostics) {
        local_failure += " " + diagnostic.code + ":" + diagnostic.message;
    }
    const auto* local_process = find_process(supported_locals, "local_property");
    const auto* sequence_process
        = find_process(supported_locals, "sequence_property");
    const auto* delayed_sequence_process
        = find_process(supported_locals, "delayed_sequence_property");
    require(
        supported_locals.ok()
            && process_has(local_process, is_monitor_tick)
            && process_has(sequence_process, is_monitor_tick)
            && process_has(delayed_sequence_process, is_monitor_tick),
        "properties with local variables are concurrent-property monitor processes"
            + local_failure);
    require(
        local_widths(local_process) == std::vector<std::uint64_t> { 32U, 137U }
            && local_widths(sequence_process) == std::vector<std::uint64_t> { 32U }
            && local_widths(delayed_sequence_process)
                == std::vector<std::uint64_t> { 32U },
        "property locals retain their declared types and arbitrary widths:"
            + [&] {
                  std::string listed;
                  for (const auto* process :
                      { local_process, sequence_process,
                          delayed_sequence_process }) {
                      listed += " |";
                      for (const auto width : local_widths(process)) {
                          listed += " " + std::to_string(width);
                      }
                  }
                  return listed;
              }());
    // The subroutine-call match item runs from the monitor process in the
    // Reactive region (16.11).
    require(
        process_has(sequence_process,
            [](const Statement& statement) {
                return statement.kind == StatementKind::TaskCall
                    && statement.task_name == "record_match";
            })
            && process_has(sequence_process,
                [](const Statement& statement) {
                    return statement.kind == StatementKind::Display
                        && statement.output_text.starts_with(
                            "\x1f"
                            "fsim.concurrent-assertion-call-region|");
                }),
        "a first_match subroutine-call match item is run from the monitor process");

    const auto unsupported_type = parse_text(
        "unsupported-executable-property-type.sv",
        R"(module unsupported_type;
  string message;
  assert property (message);
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            unsupported_type.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-200";
            })
            == 1,
        "unsupported executable property type rejects exactly");

    const auto unsupported_clock = parse_text(
        "unsupported-executable-property-clock.sv",
        R"(module unsupported_clock(
    input logic clock, reset, request);
  property requested;
    @(posedge clock or negedge reset) request;
  endproperty
  assert property (requested);
endmodule
)",
        Language::SystemVerilog2017);
    require(
        std::ranges::count_if(
            unsupported_clock.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-201";
            })
            == 0,
        "a property clock may be any event expression (16.5)");

    // The clock inferred for a procedure's context (IEEE 1800-2023 16.14.6).
    const auto count_code = [](const auto& result, const std::string_view code) {
        return std::ranges::count_if(
            result.diagnostics, [&](const Diagnostic& diagnostic) {
                return diagnostic.code == code;
            });
    };
    const auto inferred_clock_text = [](const auto& result,
                                         const std::size_t index) {
        std::string text;
        const auto& units = result.design.units;
        if (units.empty()
            || index >= units.front().systemverilog_concurrent_assertions.size()) {
            return std::string { "<none>" };
        }
        for (const auto& token : units.front()
                 .systemverilog_concurrent_assertions[index]
                 .procedural_clock) {
            text += text.empty() ? "" : " ";
            text += token.text;
        }
        return text;
    };
    // posedge reset is used in the body, so posedge clock is the one event
    // expression that qualifies; the `iff` guard belongs to the clock.
    const auto guarded_clock = parse_text(
        "inferred-clock-guard.sv",
        R"(module inferred_guard(input logic clock, reset, d1, d, output logic q);
  property r2; q != d; endproperty
  always @(posedge clock iff reset == 0 or posedge reset) begin
    q <= reset ? 0 : d1;
    r2_p: assert property (r2);
  end
endmodule
)",
        Language::SystemVerilog2017);
    // A guard on one of several events is that event's own (9.4.2.3).
    require(guarded_clock.diagnostics.empty(),
        "a guard on one of several events parses and infers the clock");
    // Each event keeps the guard written after it; a control of one event
    // keeps its guard on the process instead.
    const auto per_event_guards = parse_text(
        "event-guards.sv",
        R"(module event_guards(input logic a, b, c, d);
  int n;
  always @(posedge a iff c or negedge b iff d or a) n++;
  always @(posedge a iff c) n++;
endmodule
)",
        Language::SystemVerilog2017);
    require(per_event_guards.diagnostics.empty()
            && per_event_guards.design.units.front().processes.size() == 2U,
        "per-event iff guards parse");
    {
        const auto& processes = per_event_guards.design.units.front().processes;
        require(processes[0].sensitivities.size() == 3U
                && processes[0].sensitivities[0].guard
                && processes[0].sensitivities[1].guard
                && !processes[0].sensitivities[2].guard,
            "each event of a multi-event control keeps its own guard");
        require(processes[1].sensitivities.size() == 1U
                && !processes[1].sensitivities[0].guard,
            "a one-event control keeps its guard on the process");
    }
    const auto edge_clock = parse_text(
        "inferred-clock-edge.sv",
        R"(module inferred_edge(input logic clock, reset, d1, d, output logic q);
  property r1; q != d; endproperty
  always @(posedge clock or negedge reset) begin
    if (!reset) q <= 0; else q <= d1;
    r1_p1: assert property (r1);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(edge_clock.ok() && inferred_clock_text(edge_clock, 0U) == "posedge clock",
        "the edge expression whose terms the body leaves unused is the inferred clock");
    const auto initial_clock = parse_text(
        "inferred-clock-initial.sv",
        R"(module inferred_initial(input logic clock, d1, d, output logic q);
  property r1; q != d; endproperty
  initial begin
    @(posedge clock);
    q <= d1;
    r1_p1: assert property (r1);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(initial_clock.ok()
            && inferred_clock_text(initial_clock, 0U) == "posedge clock",
        "an initial procedure with one event control infers its clock");
    const auto delayed_clock = parse_text(
        "inferred-clock-delay.sv",
        R"(module inferred_delay(input logic mclk, d1, d, output logic q, q1);
  property r4; q != d; endproperty
  always @(posedge mclk) begin
    #10 q <= d1;
    @(negedge mclk) q1 <= !d1;
    r4_p: assert property (r4);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(count_code(delayed_clock, "FSIM-SV-SEM-470") == 1,
        "a delay and a second event control give no inferred clock and no default clocking is an error");
    const auto used_clock = parse_text(
        "inferred-clock-used.sv",
        R"(module inferred_used(input logic clock, reset, d1, d, output logic q);
  property r1; q != d; endproperty
  always @(posedge clock) begin
    q <= clock ? d1 : 1'b0;
    r1_p: assert property (r1);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(count_code(used_clock, "FSIM-SV-SEM-470") == 1,
        "a clock term that the procedure uses cannot be inferred");
    const auto default_clocking_clock = parse_text(
        "inferred-clock-default.sv",
        R"(module inferred_default(input logic clock, other, d1, d, output logic q);
  default clocking cb @(posedge other); endclocking
  property r1; q != d; endproperty
  always @(posedge clock) begin
    #1 q <= d1;
    r1_p: assert property (r1);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(count_code(default_clocking_clock, "FSIM-SV-SEM-470") == 0
            && default_clocking_clock.ok(),
        "the default clocking clocks an assertion when no clock is inferred");
    const auto automatic_clock = parse_text(
        "inferred-clock-automatic.sv",
        R"(module inferred_automatic(input logic clock, d1, d, output logic q);
  property r1; q != d; endproperty
  initial begin
    automatic logic c = 1'b0;
    r1_p: assert property (@(posedge c) r1);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(count_code(automatic_clock, "FSIM-SV-SEM-471") == 1,
        "an automatic variable in a clocking event is illegal");

    // A sampled value function other than $sampled in a disable condition or
    // a clock expression shall be explicitly clocked (IEEE 1800-2023 16.9.3,
    // 16.12): no clock is inferred there.
    const auto unclocked_disable = parse_text(
        "sampled-unclocked-disable.sv",
        R"(module sampled_unclocked_disable(input logic clock, reset, a, b);
  property p1; @(posedge clock) disable iff ($rose(reset)) a |=> b; endproperty
  a1: assert property (p1);
  a2: assert property (@(posedge clock) disable iff ($past(reset, 2)) a);
  c1: cover sequence (@(posedge clock) disable iff ($changed(reset)) a ##1 b);
endmodule
)",
        Language::SystemVerilog2017);
    require(count_code(unclocked_disable, "FSIM-SV-SEM-473") == 3,
        "$rose, $past and $changed without a clocking argument in disable iff are errors");
    const auto unclocked_default_disable = parse_text(
        "sampled-unclocked-default-disable.sv",
        R"(module sampled_unclocked_default(input logic clock, reset, a);
  default clocking cb @(posedge clock); endclocking
  default disable iff $fell(reset);
  a1: assert property (a);
endmodule
)",
        Language::SystemVerilog2017);
    require(count_code(unclocked_default_disable, "FSIM-SV-SEM-473") == 1,
        "$fell without a clocking argument in default disable iff is an error");
    const auto unclocked_clock = parse_text(
        "sampled-unclocked-clock.sv",
        R"(module sampled_unclocked_clock(input logic clock, enable, a, b);
  sequence s1; @(posedge clock iff $stable(enable)) a ##1 b; endsequence
  a1: assert property (@(posedge clock iff $past(enable)) a);
  a2: assert property (s1);
  always @(posedge clock) begin
    a3: assert property (@(negedge clock iff $rose(enable)) b);
  end
endmodule
)",
        Language::SystemVerilog2017);
    require(count_code(unclocked_clock, "FSIM-SV-SEM-473") == 3,
        "$stable, $past and $rose without a clocking argument in a clock expression are errors");
    const auto explicitly_clocked = parse_text(
        "sampled-explicitly-clocked.sv",
        R"(module sampled_explicit(input logic clock, reset, enable, a, b);
  default clocking cb @(posedge clock); endclocking
  default disable iff $fell(reset, @(posedge clock));
  property p1;
    @(posedge clock) disable iff ($rose(reset, @(posedge clock))) a |=> $rose(b);
  endproperty
  a1: assert property (p1);
  a2: assert property (@(posedge clock iff $past(enable, 1, 1'b1, @(negedge clock)))
      disable iff ($sampled(reset) || $changed(reset, @cb)) a ##1 $stable(b));
  a3: assert property (@(posedge clock) disable iff ($past(reset, 2, , @(posedge clock))) a);
endmodule
)",
        Language::SystemVerilog2017);
    require(count_code(explicitly_clocked, "FSIM-SV-SEM-473") == 0
            && explicitly_clocked.ok(),
        "explicitly clocked sampled value functions, $sampled, and inferred clocks in the property body are legal");

    std::string resource_source {
        "module assertion_resource(input logic request);\n"
    };
    for (std::size_t index = 0; index < 257U; ++index) {
        resource_source += "assert property (request);\n";
    }
    resource_source += "endmodule\n";
    const auto unbounded_assertions = parse_text(
        "concurrent-assertions-unbounded.sv",
        resource_source,
        Language::SystemVerilog2017);
    require(
        unbounded_assertions.ok()
            && unbounded_assertions.design.units.size() == 1
            && unbounded_assertions.design.units.front().processes.size() == 257,
        "executable concurrent assertions have no arbitrary per-unit limit");

    const auto malformed_name = parse_text(
        "missing-assertion-name.sv",
        "module missing_name; sequence ; 1; endsequence endmodule\n",
        Language::SystemVerilog2017);
    const auto malformed_header = parse_text(
        "missing-assertion-header-semicolon.sv",
        "module missing_header; property p endproperty endmodule\n",
        Language::SystemVerilog2017);
    const auto malformed_end = parse_text(
        "missing-assertion-end.sv",
        "module missing_end; checker c; assert (1); endmodule\n",
        Language::SystemVerilog2017);
    require(
        std::ranges::any_of(
            malformed_name.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-290";
            })
            && std::ranges::any_of(
                malformed_header.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-PARSE-291";
                })
            && std::ranges::any_of(
                malformed_end.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-PARSE-292";
                }),
        "malformed assertion declaration boundaries are diagnosed exactly");

    const auto wrong_language = parse_text(
        "verilog-sequence.v",
        "module wrong_language; sequence s; 1; endsequence endmodule\n",
        Language::Verilog2005);
    require(
        std::ranges::any_of(
            wrong_language.diagnostics,
            [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-191";
            }),
        "assertion declarations require SystemVerilog-2017");
}

} // namespace fsim::tests::frontend
