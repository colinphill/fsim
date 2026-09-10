// SPDX-License-Identifier: Apache-2.0
#include "fsim/frontend/frontend.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::frontend {

using namespace fsim::frontend;

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

}  // namespace

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
  typedef struct {
    packet_t packet;
    logic [1:0] count;
  } record_t;
endpackage : base_values

import base_values::*;
package derived_values;
  export base_values::BASE, base_values::word_t;
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
      parsed.design.units[0].type_aliases.size() == 6
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
  const auto& record_type =
      parsed.design.units[0].type_aliases[5].type;
  require(
      record_type.packed_aggregate
              == PackedAggregateKind::UnpackedStruct
          && record_type.packed_members.size() == 2
          && record_type.packed_members[0].name == "packet"
          && record_type.packed_members[0].nested_types.size() == 1
          && record_type.packed_members[1].name == "count",
      "unpacked structs retain ordered scalar and nested aggregate members");

  const auto anonymous_aggregates = parse_text(
      "anonymous_aggregates.sv",
      R"(
module anonymous_aggregate_top;
  struct packed {
    logic [31:0] header;
    struct packed {
      bit [95:0] payload;
      logic valid;
    } body;
    bit [7:0] tail;
  } value;
  initial begin
    struct packed {
      logic [15:0] prefix;
      struct packed {
        logic [7:0] data;
        logic ready;
      } inner;
    } local_value;
    local_value.inner.ready = 1'b1;
  end
endmodule
)",
      Language::SystemVerilog2017);
  require(
      anonymous_aggregates.ok()
          && anonymous_aggregates.design.units.size() == 1
          && anonymous_aggregates.design.units[0].signals.size() == 1
          && anonymous_aggregates.design.units[0].processes.size() == 1
          && anonymous_aggregates.design.units[0]
                 .processes[0].variables.size()
              == 1,
      "anonymous packed aggregate signal and local declarations parse");
  const auto& anonymous_type =
      anonymous_aggregates.design.units[0].signals[0].type;
  require(
      anonymous_type.packed_aggregate
              == PackedAggregateKind::Struct
          && anonymous_type.width() == 137
          && anonymous_type.packed_members.size() == 3
          && anonymous_type.packed_members[0].name == "header"
          && anonymous_type.packed_members[0].lsb_offset == 105
          && anonymous_type.packed_members[1].name == "body"
          && anonymous_type.packed_members[1].lsb_offset == 8
          && anonymous_type.packed_members[1].nested_types.size() == 1
          && anonymous_type.packed_members[2].name == "tail"
          && anonymous_type.packed_members[2].lsb_offset == 0,
      "anonymous packed struct retains exact outer layout");
  const auto& anonymous_body =
      anonymous_type.packed_members[1].nested_types.front();
  require(
      anonymous_body.packed_aggregate
              == PackedAggregateKind::Struct
          && anonymous_body.width() == 97
          && anonymous_body.packed_members.size() == 2
          && anonymous_body.packed_members[0].lsb_offset == 1
          && anonymous_body.packed_members[1].lsb_offset == 0,
      "nested anonymous packed struct retains exact member layout");
  const auto& local_type = anonymous_aggregates.design.units[0]
                               .processes[0].variables[0].type;
  require(
      local_type.packed_aggregate == PackedAggregateKind::Struct
          && local_type.width() == 25
          && local_type.packed_members[1].nested_types.size() == 1
          && local_type.packed_members[1].nested_types.front().width() == 9,
      "procedural anonymous packed aggregates retain nested layout");
  const auto anonymous_unpacked = parse_text(
      "anonymous_unpacked.sv",
      R"(
module anonymous_unpacked;
  struct {
    logic value;
    struct { logic [3:0] code; } nested;
  } values[1:0];
  union {
    logic [7:0] primary;
    logic [7:0] alias_value;
  } choices[1:0];
endmodule
)",
      Language::SystemVerilog2017);
  require(
      anonymous_unpacked.ok()
          && anonymous_unpacked.design.units.size() == 1
          && anonymous_unpacked.design.units[0].variables.size() == 2
          && anonymous_unpacked.design.units[0].variables[0]
                 .type.systemverilog_container
          && anonymous_unpacked.design.units[0].variables[0]
                 .type.systemverilog_container->element_types.front()
                 .packed_aggregate
              == PackedAggregateKind::UnpackedStruct
          && anonymous_unpacked.design.units[0].variables[1]
                 .type.systemverilog_container
          && anonymous_unpacked.design.units[0].variables[1]
                 .type.systemverilog_container->element_types.front()
                 .packed_aggregate
              == PackedAggregateKind::UnpackedUnion,
      "anonymous unpacked structs and unions retain recursive array elements");
  const auto union_forms = parse_text(
      "union_forms.sv",
      R"(
package union_shapes;
  typedef union packed {
    logic [15:0] wide;
    logic [15:0] mirror;
  } ordinary_t;
  typedef union tagged packed {
    logic [15:0] wide;
    logic [7:0] narrow;
  } tagged_t;
  typedef union {
    logic [7:0] primary;
    logic [7:0] alias_value;
  } unpacked_t;
endpackage
import union_shapes::*;
module union_syntax;
  tagged_t value;
  initial value = tagged narrow 8'hab;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      union_forms.ok()
          && union_forms.design.units.size() == 2
          && union_forms.design.units[0].type_aliases.size() == 3,
      "packed, tagged, and unpacked unions parse");
  const auto& ordinary_type = union_forms.design.units[0].type_aliases[0].type;
  const auto& tagged_type =
      union_forms.design.units[0].type_aliases[1].type;
  const auto& unpacked_type =
      union_forms.design.units[0].type_aliases[2].type;
  require(
      ordinary_type.packed_aggregate == PackedAggregateKind::Union
          && ordinary_type.width() == 16
          && ordinary_type.packed_members.size() == 2
          && ordinary_type.packed_members[0].lsb_offset == 0
          && ordinary_type.packed_members[1].lsb_offset == 0
          && tagged_type.packed_aggregate
              == PackedAggregateKind::TaggedUnion
          && tagged_type.width() == 17
          && tagged_type.packed_members.size() == 2
          && unpacked_type.packed_aggregate
              == PackedAggregateKind::UnpackedUnion
          && unpacked_type.packed_members.size() == 2,
      "union storage uses the maximum payload plus a tagged discriminator");
  const auto& tagged_constructor =
      union_forms.design.units[1].processes[0].statements[0].value;
  require(
      tagged_constructor.kind == ExpressionKind::Call
          && tagged_constructor.text == "@sv-tagged:narrow"
          && tagged_constructor.operands.size() == 1
          && tagged_constructor.operands[0].text == "8'hab",
      "tagged-union construction retains member identity and payload");
  const auto enum_and_member_defaults = parse_text(
      "enum_and_member_defaults.sv",
      R"(
module enum_and_member_defaults;
  enum { START = 5, NEXT } anonymous_value;
  typedef enum { DEFAULT_ZERO, DEFAULT_ONE } default_enum_t;
  typedef enum signed [2:0] { NEGATIVE = -2, FOLLOWING } ranged_enum_t;
  typedef struct packed {
    bit [1:0] code;
    logic valid;
  } initialized_inner_t;
  typedef struct packed {
    logic [7:0] payload = 8'ha5;
    initialized_inner_t nested = '{code: 2'b01, valid: 1'b0};
  } initialized_t;
endmodule
)",
      Language::SystemVerilog2017);
  require(
      enum_and_member_defaults.ok(),
      "anonymous/default-base enums and aggregate member defaults parse");
  require(
      enum_and_member_defaults.design.units.size() == 1,
      "anonymous enum source retains one module");
  const auto& enum_unit = enum_and_member_defaults.design.units[0];
  const auto anonymous_object = std::ranges::find_if(
      enum_unit.signals,
      [](const SignalDeclaration& signal) {
        return signal.name == "anonymous_value";
      });
  require(
      anonymous_object != enum_unit.signals.end(),
      "anonymous enum retains its declared object");
  require(
      enum_unit.parameters.size() == 6,
      std::string{"anonymous and typedef enum literal count was "}
          + std::to_string(enum_unit.parameters.size()));
  require(
      enum_unit.type_aliases.size() == 4,
      std::string{"enum and initialized typedef count was "}
          + std::to_string(enum_unit.type_aliases.size()));
  const auto& anonymous_enum =
      anonymous_object->type;
  const auto& initialized =
      enum_unit.type_aliases[3].type;
  require(
      anonymous_enum.spelling == "int"
          && anonymous_enum.is_signed
          && anonymous_enum.width() == 32
          && anonymous_enum.enumeration_literals.size() == 2
          && anonymous_enum.systemverilog_enumeration_values.size() == 2
          && initialized.packed_members.size() == 2
          && initialized.packed_members[0].initializer
          && initialized.packed_members[1].initializer
          && initialized.packed_members[1].nested_types.size() == 1,
      "enum inference and recursive member initializer expressions survive parsing");
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
      parsed.design.units[1].systemverilog_exports.size() == 2
          && parsed.design.units[1].systemverilog_exports[0].package
              == "base_values"
          && parsed.design.units[1].systemverilog_exports[0].name
              == "BASE"
          && parsed.design.units[1].systemverilog_exports[1].name
              == "word_t",
      "package exports retain selected source package and item metadata");
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

  const auto header_imports = parse_verilog(
      SourceText{
          "package_header_imports.sv",
          R"(
package header_types;
  parameter int WIDTH = 6;
  typedef logic [WIDTH-1:0] word_t;
endpackage
module header_consumer import header_types::word_t, header_types::WIDTH;
    #(parameter int LOCAL_WIDTH = WIDTH)
    (input word_t source, output word_t result);
  assign result = source;
endmodule
interface header_interface import header_types::*; (input word_t source);
endinterface
program header_program import header_types::word_t; (input word_t source);
endprogram
)"},
      StandardRevision::SystemVerilog2023);
  require(
      header_imports.ok()
          && header_imports.design.units.size() == 4,
      "package imports in design-unit headers parse in the 2023 profile");
  for (std::size_t index = 1; index < header_imports.design.units.size();
       ++index) {
    require(
        !header_imports.design.units[index].systemverilog_imports.empty(),
        "header imports retain package visibility metadata");
  }
  const auto& header_module = header_imports.design.units[1];
  require(
      header_module.systemverilog_imports.size() == 2
          && header_module.parameters.front().default_value.text == "WIDTH"
          && header_module.ports.size() == 2
          && header_module.ports.front().type.named_type == "word_t",
      "header imports precede parameter and port parsing");

  const auto missing_header_surface = parse_verilog(
      SourceText{
          "missing_package_header_surface.sv",
          "package p; typedef int value_t; endpackage "
          "module invalid import p::*; ; endmodule"},
      StandardRevision::SystemVerilog2023);
  require(
      !missing_header_surface.ok()
          && std::ranges::any_of(
              missing_header_surface.diagnostics,
              [](const Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-SV-PARSE-385";
              }),
      "a header import requires a following parameter or port surface");

  const auto invalid = parse_text(
      "invalid_packages.sv",
      R"(
package invalid_values;
  initial;
  typedef string unsupported_t;
  typedef logic duplicate_t;
  typedef bit duplicate_t;
  typedef enum { DEFAULT_BASE } default_base_t;
  typedef enum real { INVALID_BASE } invalid_base_t;
  typedef enum logic [1:0] {} empty_enum_t;
  typedef enum logic [1:0] A, B } missing_open_t;
  typedef enum logic [1:0] { C missing_close_t;
  typedef union invalid_unpacked_t;
  typedef struct packed {
    real unsupported;
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
  typedef union packed {
    logic [15:0] wide;
    logic [7:0] narrow;
  } unequal_union_t;
endpackage : wrong_name
import invalid_values;
module recovered;
  logic value;
  initial begin
    value = {2{}};
    value = type();
  end
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
          && has_code("FSIM-SV-UNSUPPORTED-026")
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
          && has_code("FSIM-SV-SEM-241")
          && has_code("FSIM-SV-SEM-242")
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

}  // namespace fsim::tests::frontend
