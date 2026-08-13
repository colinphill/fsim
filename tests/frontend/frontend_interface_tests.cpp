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
  checker bad_local; int = 1; endchecker
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
  sequence bad_element; ##1 request; endsequence
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
  request_check: assert property (requested)
    $display("pass"); else $error("fail");
  assume property (requested) else $warning("assume");
  cover property (requested) begin
    $display("covered");
  end
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
            && directives.size() == 4
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
                == SystemVerilogConcurrentAssertionKind::Restrict
            && !directives[3].has_pass_action
            && !directives[3].has_failure_action
            && has_token(directives[0].property_tokens, "requested")
            && directives[0].sampling_region
                == SystemVerilogAssertionRegion::Preponed
            && directives[0].evaluation_region
                == SystemVerilogAssertionRegion::Observed
            && directives[0].action_region
                == SystemVerilogAssertionRegion::Reactive
            && directives[3].span.source_name
                == "concurrent-assertions.sv",
        "concurrent directives own kinds, labels, properties, and regions");
    const auto& controls = concurrent_assertions.design.units[0]
                               .processes[0]
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

    const auto supported_locals = parse_text(
        "supported-executable-property-locals.sv",
        R"(module supported_locals(
    input logic clock, request,
    input logic [136:0] wide_value);
  task automatic record_match(input int value); endtask
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
)",
        Language::SystemVerilog2017);
    const Process* local_process { nullptr };
    const Process* sequence_process { nullptr };
    const Process* delayed_sequence_process { nullptr };
    if (!supported_locals.design.units.empty()) {
        for (const auto& process : supported_locals.design.units.front().processes) {
            if (process.name == "local_property") {
                local_process = &process;
            } else if (process.name == "sequence_property") {
                sequence_process = &process;
            } else if (process.name == "delayed_sequence_property") {
                delayed_sequence_process = &process;
            }
        }
    }
    std::string local_failure;
    for (const auto& diagnostic : supported_locals.diagnostics) {
        local_failure += " " + diagnostic.code + ":" + diagnostic.message;
    }
    require(
        supported_locals.ok()
            && std::ranges::none_of(
                supported_locals.diagnostics,
                [](const Diagnostic& diagnostic) {
                    return diagnostic.code == "FSIM-SV-SEM-199";
                })
            && local_process != nullptr
            && local_process->variables.size() == 2U
            && local_process->variables[0].name == "attempt"
            && local_process->variables[0].type.width() == 32U
            && local_process->variables[1].name == "captured"
            && local_process->variables[1].type.width() == 137U
            && std::ranges::any_of(
                local_process->statements,
                [](const Statement& statement) {
                    return statement.kind == StatementKind::Fork
                        && statement.statements.size() >= 3U
                        && statement.statements[0].kind
                        == StatementKind::Assignment
                        && statement.statements[1].kind
                        == StatementKind::Assignment;
                }),
        "property locals retain arbitrary-width types and per-attempt initializer assignments"
            + local_failure);
    require(
        sequence_process != nullptr
            && sequence_process->variables.size() == 1U
            && sequence_process->variables.front().name == "match_count"
            && std::ranges::any_of(
                sequence_process->statements,
                [](const Statement& statement) {
                    if (statement.kind != StatementKind::Fork
                        || statement.statements.size() < 2U) {
                        return false;
                    }
                    const auto& evaluation = statement.statements.back();
                    return evaluation.kind == StatementKind::Assert
                        && evaluation.statements.size() >= 5U
                        && std::ranges::count(
                               evaluation.statements,
                               StatementKind::Assignment,
                               &Statement::kind)
                        == 3
                        && evaluation.statements[0].target.text == "match_count"
                        && evaluation.statements[1].value.kind
                        == ExpressionKind::Binary
                        && evaluation.statements[2].value.kind
                        == ExpressionKind::Binary
                        && evaluation.statements[3].kind
                        == StatementKind::TaskCall
                        && evaluation.statements[3].task_name == "record_match";
                }),
        "sequence first_match assignments, updates, and calls execute at the match point");
    const auto statement_tree_has_target
        = [&](const auto& self, const Statement& statement,
              const std::string_view target) -> bool {
        if (statement.kind == StatementKind::Assignment
            && statement.target.text == target) {
            return true;
        }
        return std::ranges::any_of(
                   statement.statements,
                   [&](const Statement& child) {
                       return self(self, child, target);
                   })
            || std::ranges::any_of(
                statement.else_statements,
                [&](const Statement& child) {
                    return self(self, child, target);
                });
    };
    require(
        delayed_sequence_process != nullptr
            && delayed_sequence_process->variables.size() == 1U
            && delayed_sequence_process->variables.front().name
                == "delayed_count"
            && std::ranges::any_of(
                delayed_sequence_process->statements,
                [&](const Statement& statement) {
                    return statement_tree_has_target(
                        statement_tree_has_target,
                        statement, "delayed_count");
                }),
        "ranged first_match sequences update attempt locals only after a completed match");

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
            == 1,
        "unsupported executable property clock rejects exactly");

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
