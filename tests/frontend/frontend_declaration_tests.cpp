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

[[maybe_unused]] std::filesystem::path make_test_directory(
    std::string_view name) {
  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory =
      std::filesystem::temp_directory_path()
      / ("fsim-" + std::string{name} + "-"
         + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  return directory;
}

[[maybe_unused]] void write_text(
    const std::filesystem::path& path,
    const std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << text;
  require(
      output.good(),
      "frontend test fixture must be writable");
}

} // namespace

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

  const auto sized = parse_text(
      "sized-parameters.sv",
      R"(
module sized_parameters #(
  parameter byte SIGNED_BYTE = 8'hff,
  parameter byte unsigned UNSIGNED_BYTE = 8'hff,
  parameter shortint SHORT_VALUE = 16'h8000,
  parameter longint LONG_VALUE = 1,
  parameter longint unsigned UNSIGNED_LONG_VALUE = 1,
  parameter time TIME_VALUE = 2,
  parameter logic signed [7:0] SIGNED_VECTOR = 8'h80,
  parameter int unsigned UNSIGNED_INT = 3
) ();
endmodule
)",
      Language::SystemVerilog2017);
  require(sized.ok(), "SystemVerilog integral parameter types must parse");
  const auto* sized_unit =
      sized.design.find(UnitKind::VerilogModule, "sized_parameters");
  require(
      sized_unit != nullptr && sized_unit->parameters.size() == 8,
      "integral parameter declarations retain source order");
  require(
      sized_unit->parameters[0].type.spelling == "byte"
          && sized_unit->parameters[0].type.domain
              == ValueDomain::Bit2
          && sized_unit->parameters[0].type.is_signed
          && sized_unit->parameters[0].type.width() == 8
          && sized_unit->parameters[1].type.spelling == "byte"
          && !sized_unit->parameters[1].type.is_signed
          && sized_unit->parameters[2].type.spelling == "shortint"
          && sized_unit->parameters[2].type.width() == 16
          && sized_unit->parameters[3].type.spelling == "longint"
          && sized_unit->parameters[3].type.width() == 64
          && sized_unit->parameters[3].type.is_signed
          && sized_unit->parameters[4].type.spelling == "longint"
          && !sized_unit->parameters[4].type.is_signed
          && sized_unit->parameters[4].type.width() == 64
          && sized_unit->parameters[5].type.spelling == "time"
          && sized_unit->parameters[5].type.domain
              == ValueDomain::Logic4
          && !sized_unit->parameters[5].type.is_signed
          && sized_unit->parameters[5].type.width() == 64
          && sized_unit->parameters[6].type.is_signed
          && sized_unit->parameters[6].type.width() == 8
          && !sized_unit->parameters[7].type.is_signed
          && sized_unit->parameters[7].type.width() == 32,
      "integral parameter widths, domains, and signedness are typed");

  const auto type_parameters = parse_text(
      "type-parameters.sv",
      R"(
package types_pkg;
  typedef logic [11:0] word_t;
endpackage
module typed_child #(
  parameter type ELEMENT = logic,
  parameter type WORD = types_pkg::word_t,
  parameter WIDTH = 3
) (
  input ELEMENT element,
  input WORD word
);
endmodule
module typed_parent;
  typedef bit [7:0] byte_t;
  typed_child #(
    .ELEMENT(bit),
    .WORD(byte_t),
    .WIDTH(5)
  ) child();
endmodule
)",
      Language::SystemVerilog2017);
  require(
      type_parameters.ok(),
      "bounded SystemVerilog type parameters must parse");
  const auto* typed_child =
      type_parameters.design.find(
          UnitKind::VerilogModule, "typed_child");
  const auto* typed_parent =
      type_parameters.design.find(
          UnitKind::VerilogModule, "typed_parent");
  require(
      typed_child != nullptr
          && typed_child->parameters.size() == 3
          && typed_child->parameters[0].kind
              == ParameterKind::Type
          && typed_child->parameters[0].default_type
          && typed_child->parameters[0].default_type->spelling
              == "logic"
          && typed_child->parameters[1].kind
              == ParameterKind::Type
          && typed_child->parameters[1].default_type
          && typed_child->parameters[1].default_type->named_type
              == "types_pkg::word_t"
          && typed_child->parameters[2].kind
              == ParameterKind::Value,
      "type and value formals retain ordered distinct HIR");
  require(
      typed_parent != nullptr
          && typed_parent->instances.size() == 1
          && typed_parent->instances[0]
                 .parameter_overrides.size()
              == 3
          && typed_parent->instances[0]
                 .parameter_overrides[0].type_value
          && typed_parent->instances[0]
                 .parameter_overrides[0].type_value->spelling
              == "bit"
          && !typed_parent->instances[0]
                  .parameter_overrides[1].type_value
          && typed_parent->instances[0]
                 .parameter_overrides[1].value.text
              == "byte_t",
      "unambiguous and identifier type actuals retain tentative HIR");

  const auto string_parameters = parse_text(
      "string-parameters.sv",
      R"(
package string_pkg;
  localparam string PACKAGE_LABEL = "package";
endpackage
module string_parameters #(
  parameter string LABEL = "fsim\n"
) ();
  localparam string DECORATED = {LABEL, "!"};
endmodule
)",
      Language::SystemVerilog2017);
  require(
      string_parameters.ok(),
      "string parameters and localparams must parse");
  const auto* string_unit =
      string_parameters.design.find(
          UnitKind::VerilogModule, "string_parameters");
  require(
      string_unit != nullptr
          && string_unit->parameters.size() == 2
          && string_unit->parameters[0].type.spelling == "string"
          && string_unit->parameters[0].default_value.kind
              == ExpressionKind::StringLiteral
          && string_unit->parameters[0].default_value.decoded_string
              == std::optional<std::string>{"fsim\n"}
          && string_unit->parameters[1].local
          && string_unit->parameters[1].type.spelling == "string"
          && string_unit->parameters[1].default_value.kind
              == ExpressionKind::Concatenation,
      "string HIR retains type, locality, expression, and decoded bytes");

  const auto mutable_strings = parse_text(
      "mutable-strings.sv",
      R"(
module mutable_strings;
  string title = "fsim";
  string empty;

  function automatic string decorate(input string value);
    string suffix = "!";
    return {value, suffix};
  endfunction

  task automatic remember(input string value, output string copied);
    string temporary;
    temporary = value;
    copied = temporary;
  endtask

  initial begin : worker
    string scratch = {title, "-local"};
    title = decorate(scratch);
    if (title != "")
      title[0] = "F";
    if (title.len() == 0)
      title = empty;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      mutable_strings.ok(),
      "bounded mutable string syntax must parse without integral-type "
      "fallback diagnostics");
  const auto* mutable_unit =
      mutable_strings.design.find(
          UnitKind::VerilogModule, "mutable_strings");
  require(
      mutable_unit != nullptr
          && mutable_unit->variables.size() == 2
          && mutable_unit->variables[0].type.domain
              == ValueDomain::String
          && mutable_unit->variables[0].initializer
          && mutable_unit->variables[0].initializer->decoded_string
              == std::optional<std::string>{"fsim"}
          && !mutable_unit->variables[1].initializer,
      "module string variables retain distinct type and initialization");
  require(
      mutable_unit != nullptr
          && mutable_unit->functions.size() == 1
          && mutable_unit->functions[0].return_type.domain
              == ValueDomain::String
          && mutable_unit->functions[0].arguments.size() == 1
          && mutable_unit->functions[0].arguments[0].type.domain
              == ValueDomain::String
          && mutable_unit->functions[0].variables.size() == 1
          && mutable_unit->functions[0].variables[0].type.domain
              == ValueDomain::String,
      "string function result, formal, and automatic local retain typing");
  require(
      mutable_unit != nullptr
          && mutable_unit->tasks.size() == 1
          && mutable_unit->tasks[0].arguments.size() == 2
          && mutable_unit->tasks[0].arguments[0].type.domain
              == ValueDomain::String
          && mutable_unit->tasks[0].arguments[1].type.domain
              == ValueDomain::String
          && mutable_unit->tasks[0].variables.size() == 1
          && mutable_unit->tasks[0].variables[0].type.domain
              == ValueDomain::String,
      "string task formals and automatic local retain typing");
  require(
      mutable_unit != nullptr
          && mutable_unit->processes.size() == 1
          && mutable_unit->processes[0].statements.size() == 1
          && mutable_unit->processes[0].statements[0]
                 .declarations.size() == 1
          && mutable_unit->processes[0].statements[0]
                 .declarations[0].type.domain
              == ValueDomain::String
          && mutable_unit->processes[0].statements[0]
                 .statements.size() == 3
          && mutable_unit->processes[0].statements[0]
                 .statements[2].condition.operands[0].kind
              == ExpressionKind::Call
          && mutable_unit->processes[0].statements[0]
                 .statements[2].condition.operands[0].text
              == ".len",
      "block string declarations and len method retain executable HIR");

  const auto invalid_string_escape = parse_text(
      "invalid-string-escape.sv",
      R"(
module invalid_string_escape #(
  parameter string LABEL = "bad\q"
) ();
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !invalid_string_escape.ok()
          && std::ranges::any_of(
              invalid_string_escape.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-040";
              }),
      "invalid string parameter escapes retain the shared diagnostic");

  const auto type_namespace_conflict = parse_text(
      "type-parameter-conflict.sv",
      R"(
module type_parameter_conflict #(
  parameter type element_t = logic
) ();
  typedef logic element_t;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      !type_namespace_conflict.ok()
          && std::ranges::any_of(
              type_namespace_conflict.diagnostics,
              [](const auto& diagnostic) {
                return diagnostic.code == "FSIM-SV-SEM-055";
              }),
      "type parameters share the bounded typedef namespace");

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

void test_systemverilog_function_declarations() {
  const auto parsed = parse_text(
      "functions.sv",
      R"(
package math_pkg;
  function automatic logic [7:0] increment(
      input logic [7:0] value);
    return value + 1;
  endfunction : increment
endpackage

module function_user(input logic select, output logic [7:0] result);
  logic observed;
  function automatic logic [7:0] choose(
      input logic condition,
      input logic [7:0] when_true,
      when_false);
    logic [7:0] temporary;
    if (condition)
      temporary = when_true;
    else
      temporary = when_false;
    choose = temporary;
  endfunction

  function automatic logic observe(input logic value);
    observed = value;
    observe = observed;
  endfunction

  initial result = choose(select, 8'h2a, 8'h11);
endmodule
)",
      Language::SystemVerilog2017);
  require(parsed.ok(), "automatic SystemVerilog functions must parse");
  require(
      parsed.design.units.size() == 2,
      "package and module function design units");
  const auto& package_function =
      parsed.design.units.front().functions.front();
  require(
      package_function.name == "increment"
          && package_function.automatic
          && package_function.arguments.size() == 1
          && package_function.statements.size() == 1
          && package_function.statements.front().kind
              == StatementKind::Return,
      "package function HIR");
  const auto& module_function =
      parsed.design.units.back().functions.front();
  require(
      module_function.name == "choose"
          && module_function.arguments.size() == 3
          && module_function.variables.size() == 1
          && module_function.statements.size() == 2
          && module_function.statements.back().kind
              == StatementKind::Assignment,
      "module function arguments, locals, and body HIR");
  require(
      parsed.design.units.back().functions.size() == 2
          && parsed.design.units.back().functions[1].name == "observe"
          && parsed.design.units.back().functions[1].statements.size() == 2
          && parsed.design.units.back().functions[1].statements.front()
                 .target.text == "observed",
      "time-free function writes to nonlocal variables remain observable");

  const auto classic = parse_text(
      "classic_function.v",
      R"(
module classic_function;
  function automatic [7:0] answer;
    answer = 8'h2a;
  endfunction
endmodule
)",
      Language::Verilog2005);
  require(
      classic.ok()
          && classic.design.units.front().functions.size() == 1
          && classic.design.units.front().functions.front()
                 .arguments.empty(),
      "classic no-argument Verilog function");

  const auto invalid = parse_text(
      "invalid_functions.sv",
      R"(
module invalid_functions;
  function static logic bad(output logic argument);
    #1 bad = argument;
  endfunction
  function automatic logic writes_input(input logic argument);
    argument = 1'b0;
    writes_input = argument;
  endfunction
endmodule
)",
      Language::SystemVerilog2017);
  require(!invalid.ok(), "invalid function forms must be rejected");
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      has_code("FSIM-SV-UNSUPPORTED-033")
          && has_code("FSIM-SV-UNSUPPORTED-035")
          && has_code("FSIM-SV-SEM-062")
          && has_code("FSIM-SV-SEM-064"),
      "function lifetime, direction, input-write, and timing diagnostics");
}

void test_vhdl_function_declarations() {
  const auto parsed = parse_text(
      "functions.vhd",
      R"(
package math_pkg is
  pure function twice parameter (value : in integer) return integer;
end package;

package body math_pkg is
  pure function twice parameter (value : in integer) return integer is
  begin
    return value + value;
  end function twice;
end package body math_pkg;

entity function_user is
  generic (
    function transform parameter (value : integer) return integer is <>;
    impure function observe(value : integer) return integer is selected);
  port (
    input_value : in integer;
    result : out integer);
end entity;

architecture rtl of function_user is
  pure function increment(constant value : in integer) return integer is
    variable temporary : integer := value;
  begin
    temporary := temporary + 1;
    return temporary;
  end function increment;
begin
  result <= increment(input_value);
end architecture;
)",
      Language::Vhdl2008);
  require(parsed.ok(), "bounded VHDL function forms must parse");
  require(
      parsed.design.units.size() == 4,
      "package declaration, body, entity, and architecture function units");

  const auto& package_function =
      parsed.design.units.front().functions.front();
  require(
      package_function.name == "twice"
          && package_function.language == Language::Vhdl2008
          && package_function.pure
          && !package_function.defined
          && package_function.arguments.size() == 1,
      "package function declaration HIR");
  const auto& package_body =
      parsed.design.units[1].functions.front();
  require(
      parsed.design.units[1].primary_name == "math_pkg"
          && package_body.name == "twice"
          && package_body.defined
          && package_body.statements.size() == 1
          && package_body.statements.front().kind
              == StatementKind::Return,
      "package function body HIR");

  const auto& entity = parsed.design.units[2];
  require(
      entity.parameters.size() == 2
          && entity.parameters[0].kind
              == ParameterKind::Function
          && entity.parameters[1].kind
              == ParameterKind::Function,
      "interface functions remain distinct generic formals");
  const auto& boxed =
      *entity.parameters[0].function_profile;
  const auto& named =
      *entity.parameters[1].function_profile;
  require(
      entity.parameters[0].name == "transform"
          && boxed.pure && boxed.default_box
          && !boxed.default_name
          && boxed.arguments.size() == 1
          && boxed.arguments.front().name == "value"
          && boxed.return_type.domain
              == ValueDomain::Integer,
      "boxed interface function profile HIR");
  require(
      entity.parameters[1].name == "observe"
          && !named.pure && !named.default_box
          && named.default_name
          && *named.default_name == "selected",
      "named impure interface function default HIR");

  const auto& body =
      parsed.design.units.back().functions.front();
  require(
      body.name == "increment"
          && body.language == Language::Vhdl2008
          && body.pure && body.defined && body.automatic
          && body.variables.size() == 1
          && body.statements.size() == 2
          && body.statements.front().kind
              == StatementKind::Assignment
          && body.statements.back().kind
              == StatementKind::Return
          && body.statements.back().value.kind
              == ExpressionKind::Identifier,
      "VHDL function body, locals, and return HIR");

  const auto invalid = parse_text(
      "invalid_functions.vhd",
      R"(
entity invalid_functions is
  generic (
    function bad(signal value : out integer := 1)
      return integer is "not_a_name";
    procedure bad_procedure(signal value : buffer integer));
end entity;

architecture rtl of invalid_functions is
  function "+"(value : integer) return integer is
  begin
    return;
  end function "+";
  function timed(value : integer) return integer is
  begin
    wait for 1 ns;
    return value;
  end function;
begin
  process
  begin
    return 1;
  end process;
end architecture;
)",
      Language::Vhdl2008);
  require(
      !invalid.ok(),
      "invalid bounded VHDL function forms must be rejected");
  const auto has_code =
      [&](const std::string_view code) {
        return std::ranges::any_of(
            invalid.diagnostics,
            [&](const Diagnostic& diagnostic) {
              return diagnostic.code == code;
            });
      };
  require(
      has_code("FSIM-VHDL-UNSUPPORTED-029")
          && has_code("FSIM-VHDL-UNSUPPORTED-030")
          && has_code("FSIM-VHDL-UNSUPPORTED-032")
          && has_code("FSIM-VHDL-UNSUPPORTED-033")
          && has_code("FSIM-VHDL-UNSUPPORTED-038")
          && has_code("FSIM-VHDL-UNSUPPORTED-039")
          && has_code("FSIM-VHDL-UNSUPPORTED-037")
          && has_code("FSIM-VHDL-PARSE-157")
          && has_code("FSIM-VHDL-PARSE-164")
          && has_code("FSIM-VHDL-SEM-046"),
      "VHDL function class, mode, default, designator, procedure, "
      "return, and placement diagnostics");
}

void test_systemverilog_task_declarations() {
  const auto parsed = parse_text("tasks.sv",
                                 R"(
package transform_pkg;
  timeunit 1ns;
  timeprecision 1ns;
  task automatic exchange(
      input logic [7:0] source,
      output logic [7:0] destination,
      inout logic [7:0] accumulator);
    logic [7:0] temporary;
    temporary = source;
    destination = temporary;
    accumulator = accumulator + temporary;
    return;
  endtask : exchange
endpackage

module task_owner;
  event wake;
  logic ready;
  task automatic clear;
    logic temporary;
    temporary = 1'b0;
    #1;
    @(wake);
    wait (ready);
    -> wake;
  endtask
endmodule
)",
                                 Language::SystemVerilog2017);
  require(parsed.ok(), "automatic SystemVerilog tasks must parse");
  require(parsed.design.units.size() == 2 &&
              parsed.design.units.front().tasks.size() == 1 &&
              parsed.design.units.back().tasks.size() == 1,
          "package and module task design units");
  require(parsed.design.units.front().time_unit == "1ns" &&
              parsed.design.units.front().time_precision == "1ns",
          "package task timing context");
  const auto &task = parsed.design.units.front().tasks.front();
  require(task.name == "exchange" && task.automatic &&
              task.arguments.size() == 3 &&
              task.arguments[0].direction == PortDirection::Input &&
              task.arguments[1].direction == PortDirection::Output &&
              task.arguments[2].direction == PortDirection::Inout &&
              task.variables.size() == 1 && task.statements.size() == 4 &&
              task.statements.back().kind == StatementKind::Return &&
              !task.statements.back().value.valid(),
          "task HIR preserves formals, locals, body, lifetime, and return");
  require(parsed.design.units.back().tasks.front().arguments.empty(),
          "classic no-argument task header");
  const auto &timed_task = parsed.design.units.back().tasks.front();
  require(timed_task.statements.size() == 5 &&
              timed_task.statements[1].kind == StatementKind::Delay &&
              timed_task.statements[2].kind == StatementKind::WaitOn &&
              timed_task.statements[3].kind == StatementKind::WaitUntil &&
              timed_task.statements[4].kind == StatementKind::EventTrigger,
          "task HIR admits bounded timing, event waits, condition waits, "
          "and named-event triggers");

  const auto invalid = parse_text("invalid_tasks.sv",
                                  R"(
module invalid_tasks;
  task static bad(
      ref logic value,
      output string text,
      output logic value);
    #1 value <= 1'b1;
    return value;
  endtask : mismatched
endmodule
)",
                                  Language::SystemVerilog2017);
  require(!invalid.ok(), "invalid task forms must be rejected");
  const auto has_code = [&](const std::string_view code) {
    return std::ranges::any_of(
        invalid.diagnostics,
        [&](const Diagnostic &diagnostic) { return diagnostic.code == code; });
  };
  require(has_code("FSIM-SV-UNSUPPORTED-037") &&
              has_code("FSIM-SV-UNSUPPORTED-038") &&
              has_code("FSIM-SV-SEM-067") && has_code("FSIM-SV-SEM-068") &&
              has_code("FSIM-SV-SEM-070") && has_code("FSIM-SV-SEM-071"),
          "task lifetime, ref formal, body, return, and closing-name "
          "diagnostics");

  const auto duplicate = parse_text("duplicate_tasks.sv",
                                    R"(
package duplicate_tasks;
  task automatic same;
  endtask
  task automatic same;
  endtask
endpackage
)",
                                    Language::SystemVerilog2017);
  require(!duplicate.ok() &&
              std::ranges::any_of(duplicate.diagnostics,
                                  [](const Diagnostic &diagnostic) {
                                    return diagnostic.code == "FSIM-SV-SEM-073";
                                  }),
          "duplicate task declarations are rejected");
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

} // namespace fsim::tests::frontend
