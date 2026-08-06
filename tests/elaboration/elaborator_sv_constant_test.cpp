// SPDX-License-Identifier: Apache-2.0
#include "elaborator_test_support.hpp"

#include "fsim/frontend/parser.hpp"

#include <cassert>
#include <iostream>
#include <ranges>
#include <string_view>

namespace fsim::tests::elaboration {
namespace {

const fsim::elaboration::SpecializationInfo* specialization_at(
    const fsim::elaboration::ElaboratedDesign& design,
    const std::string_view instance) {
    const auto found = std::ranges::find_if(
        design.specializations(),
        [&](const auto& specialization) {
            return specialization.instance == instance;
        });
    return found == design.specializations().end()
        ? nullptr
        : &*found;
}

std::string parameter_value(
    const fsim::elaboration::SpecializationInfo& specialization,
    const std::string_view name) {
    const auto found = std::ranges::find_if(
        specialization.parameter_values,
        [&](const auto& parameter) {
            return parameter.first == name;
        });
    assert(found != specialization.parameter_values.end());
    return found->second;
}

std::string parameter_identity(
    const fsim::elaboration::SpecializationInfo& specialization,
    const std::string_view name) {
    const auto found = std::ranges::find_if(
        specialization.parameter_identity_values,
        [&](const auto& parameter) {
            return parameter.first == name;
        });
    assert(found != specialization.parameter_identity_values.end());
    return found->second;
}

} // namespace

void test_systemverilog_typed_constants() {
    const auto parsed = fsim::frontend::parse_text(
        "typed-constants.sv",
        R"(
package wide_constant_values;
  localparam logic [127:0] PACKAGE_WIDE =
      128'h80000000000000000000000000000001;
endpackage

module typed_constant_child #(
  parameter longint unsigned VALUE = 0
) (
  output logic [63:0] q
);
  initial q = VALUE;
endmodule

module wide_constant_child #(
  parameter logic [127:0] VALUE = 0
) ();
endmodule

module typed_constant_top #(
  parameter longint unsigned MAX_VALUE = 64'hffffffffffffffff,
  parameter longint unsigned MAX_MINUS_ONE = MAX_VALUE - 1,
  parameter longint unsigned LOGICAL_SHIFT = MAX_VALUE >> 60,
  parameter longint SIGNED_CAST = $signed(8'hff),
  parameter logic [7:0] CONCAT_VALUE = {4'ha, 4'h5},
  parameter logic [7:0] REPEAT_VALUE = {2{4'hc}},
  parameter logic [7:0] MIXED_WIDTH = 4'hf + 8'h01,
  parameter logic [7:0] CONDITIONAL_VALUE =
      1'b0 ? 4'hf : 8'h81,
  parameter logic [3:0] UNKNOWN_VALUE = 4'b10x1,
  parameter logic UNKNOWN_FLAG = $isunknown(UNKNOWN_VALUE),
  parameter logic MEMBER_EXACT = 8'h15 inside {8'h14, 8'h15},
  parameter logic MEMBER_RANGE = 8'h15 inside {[8'h10:8'h1f]},
  parameter logic MEMBER_WILDCARD = 8'ha5 inside {8'b10xz_0101},
  parameter logic MEMBER_UNKNOWN = 8'bx001_0001 inside {8'b0001_0001},
  parameter logic MEMBER_REVERSED = 8'h15 inside {[8'h1f:8'h10]},
  parameter IMPLICIT_EIGHT = 8'hff,
  parameter IMPLICIT_SIXTEEN = 16'h00ff,
  parameter logic [127:0] WIDE_VALUE =
      128'h80000000000000000000000000000001,
  parameter logic [95:0] WIDE_UNKNOWN =
      96'hzzzzzzzzzzzzzzzzzzzzzzzz,
  parameter bit [127:0] WIDE_TWO_STATE =
      128'h80000000000000000000000000000001,
  parameter logic [127:0] WIDE_DECIMAL =
      128'd170141183460469231731687303715884105729,
  parameter WIDE_UNSIZED_DECIMAL =
      170141183460469231731687303715884105729,
  parameter logic [127:0] WIDE_CONCAT =
      {64'h8000000000000000, 64'h0000000000000001},
  parameter logic [127:0] WIDE_REPEAT = {16{8'ha5}},
  parameter logic [127:0] WIDE_PACKAGE =
      PACKAGE_WIDE,
  parameter logic [127:0] WIDE_QUALIFIED =
      wide_constant_values::PACKAGE_WIDE,
  parameter logic [127:0] WIDE_ALTERNATE =
      128'h40000000000000000000000000000001,
  parameter logic [127:0] WIDE_SMALL = 128'd4,
  parameter logic [127:0] WIDE_ADD =
      128'h80000000000000000000000000000001 + 128'd2,
  parameter logic [127:0] WIDE_SUBTRACT =
      WIDE_ADD - 128'd4,
  parameter logic [127:0] WIDE_MULTIPLY =
      128'd123456789 * 128'd1000,
  parameter logic [127:0] WIDE_DIVIDE =
      128'h80000000000000000000000000000000 / 128'd2,
  parameter logic [127:0] WIDE_MODULO =
      128'h80000000000000000000000000000006 % 128'd7,
  parameter logic [127:0] WIDE_POWER = 128'd3 ** 128'd20,
  parameter logic signed [127:0] WIDE_NEGATE =
      -$signed(128'd5),
  parameter logic signed [127:0] WIDE_SIGNED_DIVIDE =
      (-$signed(128'd1000)) / $signed(128'd7),
  parameter logic signed [127:0] WIDE_SIGNED_MODULO =
      (-$signed(128'd1000)) % $signed(128'd7),
  parameter logic signed [127:0] WIDE_NEGATIVE_POWER =
      $signed(128'hffffffffffffffffffffffffffffffff)
      ** (-$signed(128'd3)),
  parameter logic [127:0] WIDE_BITWISE_NOT =
      ~128'h80000000000000000000000000000001,
  parameter logic [127:0] WIDE_WRAP =
      128'hffffffffffffffffffffffffffffffff + 128'd1,
  parameter logic [127:0] WIDE_UNKNOWN_ARITHMETIC =
      128'hxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx + 128'd1,
  parameter logic [95:0] WIDE_TRUNCATED =
      128'h10000000000000000000000000000001 + 128'd2,
  parameter logic WIDE_LOGICAL_TRUE =
      128'h80000000000000000000000000000000 && 128'd1,
  parameter logic WIDE_LOGICAL_UNKNOWN =
      128'hxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx && 128'd1,
  parameter logic WIDE_EQUAL =
      $signed(8'hff)
      == $signed(128'hffffffffffffffffffffffffffffffff),
  parameter logic WIDE_CASE_EQUAL =
      128'h8000000000000000000000000000000x
      === 128'h8000000000000000000000000000000x,
  parameter logic WIDE_CASE_DIFFERENT =
      128'h8000000000000000000000000000000x
      === 128'h8000000000000000000000000000000z,
  parameter logic WIDE_WILDCARD_EQUAL =
      128'h8000000000000000000000000000000x
      ==? 128'h8???????????????????????????????,
  parameter logic WIDE_WILDCARD_NOT_EQUAL =
      128'h8000000000000000000000000000000x
      !=? 128'h8???????????????????????????????,
  parameter logic WIDE_WILDCARD_UNKNOWN =
      128'h8000000000000000000000000000000x
      ==? 128'h80000000000000000000000000000001,
  parameter logic WIDE_UNSIGNED_LESS =
      128'h7fffffffffffffffffffffffffffffff
      < 128'h80000000000000000000000000000000,
  parameter logic WIDE_SIGNED_LESS =
      $signed(128'h80000000000000000000000000000000)
      < $signed(128'h7fffffffffffffffffffffffffffffff),
  parameter logic WIDE_SIGN_EXTENDED_LESS =
      $signed(8'hff) < $signed(128'd1),
  parameter logic [127:0] WIDE_BITWISE_AND =
      128'h80000000000000000000000000000001
      & 128'hfffffffffffffffffffffffffffffffe,
  parameter logic [127:0] WIDE_BITWISE_DOMINANCE =
      128'hxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx & 128'd0,
  parameter logic [127:0] WIDE_BITWISE_UNKNOWN =
      128'hxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx | 128'd0,
  parameter logic [127:0] WIDE_LEFT_SHIFT = 128'd1 << 128'd127,
  parameter logic [127:0] WIDE_LOGICAL_RIGHT =
      128'h80000000000000000000000000000000 >> 128'd127,
  parameter logic signed [127:0] WIDE_ARITHMETIC_RIGHT =
      $signed(128'h80000000000000000000000000000001)
      >>> 128'd124,
  parameter logic [127:0] WIDE_UNKNOWN_SHIFT =
      128'd1 << 128'hxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,
  parameter logic signed [127:0] WIDE_Z_ARITHMETIC_SHIFT =
      $signed(128'hzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz)
      >>> 128'd127,
  parameter logic [127:0] WIDE_EXCESSIVE_SHIFT =
      128'hffffffffffffffffffffffffffffffff
      >> 128'h80000000000000000000000000000000,
  parameter logic [127:0] WIDE_STREAM_LEFT =
      {<<8{128'h00000000000000000000000000000001}},
  parameter logic [127:0] WIDE_STREAM_RIGHT =
      {>>{WIDE_VALUE}},
  parameter logic WIDE_REDUCE_AND =
      &128'hffffffffffffffffffffffffffffffff,
  parameter logic WIDE_REDUCE_OR = |WIDE_VALUE,
  parameter logic WIDE_REDUCE_XOR = ^WIDE_VALUE,
  parameter logic WIDE_REDUCE_XNOR = ^~WIDE_VALUE,
  parameter logic WIDE_REDUCE_UNKNOWN =
      ^128'hxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,
  parameter logic WIDE_SELECTED_BIT = WIDE_VALUE[127],
  parameter logic [63:0] WIDE_PART_SELECT = WIDE_VALUE[127:64],
  parameter logic [63:0] WIDE_INDEXED_PLUS = WIDE_VALUE[64 +: 64],
  parameter logic [63:0] WIDE_INDEXED_MINUS = WIDE_VALUE[127 -: 64],
  parameter logic [63:0] WIDE_LOW_SELECT = WIDE_VALUE[63:0],
  parameter logic [3:0] WIDE_OUT_OF_RANGE = WIDE_VALUE[129:126],
  parameter logic [3:0] WIDE_ASCENDING_SELECT = WIDE_VALUE[0:3],
  parameter logic [127:0] WIDE_UNKNOWN_CONDITIONAL =
      1'bx
          ? 128'h80000000000000000000000000000001
          : 128'h80000000000000000000000000000003,
  parameter logic WIDE_INSIDE =
      WIDE_VALUE inside {128'h8???????????????????????????????},
  parameter logic [127:0] WIDE_POSITIONAL_PATTERN =
      '{64'h8000000000000000, 64'h0000000000000001},
  parameter logic [127:0] WIDE_DEFAULT_PATTERN = '{default: 1'b1},
  parameter int WIDE_BITS = $bits(WIDE_VALUE),
  parameter int WIDE_UNKNOWN_BITS = $bits(WIDE_UNKNOWN),
  parameter int WIDE_LEFT = $left(WIDE_VALUE),
  parameter int WIDE_RIGHT = $right(WIDE_VALUE),
  parameter int WIDE_LOW = $low(WIDE_VALUE),
  parameter int WIDE_HIGH = $high(WIDE_VALUE),
  parameter int WIDE_SIZE = $size(WIDE_VALUE, 1),
  parameter int WIDE_INCREMENT = $increment(WIDE_VALUE),
  parameter int WIDE_DIMENSIONS = $dimensions(WIDE_VALUE),
  parameter int WIDE_UNPACKED_DIMENSIONS =
      $unpacked_dimensions(WIDE_VALUE),
  parameter logic [0:127] WIDE_ASCENDING_VALUE = WIDE_VALUE,
  parameter int WIDE_ASCENDING_LEFT = $left(WIDE_ASCENDING_VALUE),
  parameter int WIDE_ASCENDING_RIGHT = $right(WIDE_ASCENDING_VALUE),
  parameter int WIDE_ASCENDING_LOW = $low(WIDE_ASCENDING_VALUE),
  parameter int WIDE_ASCENDING_HIGH = $high(WIDE_ASCENDING_VALUE),
  parameter int WIDE_ASCENDING_INCREMENT =
      $increment(WIDE_ASCENDING_VALUE),
  parameter int WIDE_CLOG2_POWER =
      $clog2(128'h80000000000000000000000000000000),
  parameter int WIDE_CLOG2_NONPOWER =
      $clog2(128'h80000000000000000000000000000001),
  parameter int WIDE_INT_CAST = int'(WIDE_VALUE),
  parameter byte WIDE_BYTE_CAST = byte'(WIDE_VALUE),
  parameter shortint WIDE_SHORTINT_CAST = shortint'(WIDE_VALUE),
  parameter longint WIDE_LONGINT_CAST = longint'(WIDE_VALUE),
  parameter logic WIDE_LOGIC_CAST = logic'(WIDE_VALUE),
  parameter bit WIDE_BIT_CAST = bit'(WIDE_VALUE),
  parameter logic [31:0] WIDE_INTEGER_UNKNOWN_CAST =
      integer'(128'h0000000000000000000000000000000x),
  parameter bit WIDE_CAST_IS_UNKNOWN =
      $isunknown(WIDE_INTEGER_UNKNOWN_CAST)
) ();
  import wide_constant_values::*;
  typedef logic [127:0] local_wide_t;
  localparam local_wide_t WIDE_NAMED_CAST =
      local_wide_t'(64'h0123456789abcdef);
  localparam logic [63:0] WIDE_NAMED_CAST_HIGH =
      WIDE_NAMED_CAST[127:64];
  localparam logic [63:0] WIDE_NAMED_CAST_LOW =
      WIDE_NAMED_CAST[63:0];
  function automatic logic [127:0] update_wide(
      input logic [127:0] value);
    update_wide = value;
    update_wide[127] = 1'b0;
    update_wide[95:64] = 32'hdeadbeef;
    update_wide[63 -: 32] = 32'hcafebabe;
  endfunction
  localparam logic [127:0] WIDE_SELECTED_UPDATE =
      update_wide(128'hffffffffffffffffffffffffffffffff);
  localparam logic WIDE_UPDATE_BIT = WIDE_SELECTED_UPDATE[127];
  localparam logic [30:0] WIDE_UPDATE_RETAINED =
      WIDE_SELECTED_UPDATE[126:96];
  localparam logic [31:0] WIDE_UPDATE_HIGH =
      WIDE_SELECTED_UPDATE[95:64];
  localparam logic [31:0] WIDE_UPDATE_MIDDLE =
      WIDE_SELECTED_UPDATE[63:32];
  localparam logic [31:0] WIDE_UPDATE_LOW =
      WIDE_SELECTED_UPDATE[31:0];
  typedef enum logic [63:0] {
    ENUM_MAX = 64'hffffffffffffffff,
    ENUM_PREVIOUS = 64'hfffffffffffffffe
  } wide_enum_t;
  logic [63:0] max_q;
  logic [7:0] mixed_q;
  logic [15:0] signed_q;
  logic [3:0] unknown_q;
  logic [63:0] child_q;
  logic [WIDE_SMALL:0] wide_range_q;
  logic [127:0] wide_q;
  bit [127:0] wide_two_state_q;
  logic [95:0] wide_unknown_q;
  logic [95:0] wide_cast_source;
  local_wide_t wide_cast_q;

  initial begin
    max_q = MAX_VALUE;
    mixed_q = 4'hf + 8'h01;
    signed_q = $signed(8'h80);
    unknown_q = UNKNOWN_VALUE;
    wide_q = WIDE_PACKAGE;
    wide_two_state_q = WIDE_TWO_STATE;
    wide_unknown_q = WIDE_UNKNOWN;
    wide_cast_source = WIDE_UNKNOWN;
    wide_cast_q = local_wide_t'(wide_cast_source);
  end

  typed_constant_child #(.VALUE(MAX_VALUE)) child(.q(child_q));
  wide_constant_child #(.VALUE(WIDE_PACKAGE)) wide_child();
  wide_constant_child #(.VALUE(WIDE_ALTERNATE)) alternate_wide_child();

  generate
    if (MAX_VALUE) begin : generated_true
      localparam longint unsigned GENERATED_VALUE = MAX_VALUE - 2;
      logic [63:0] generated_q;
      initial generated_q = GENERATED_VALUE;
    end
    case (MAX_VALUE)
      64'hffffffffffffffff: begin : generated_case
        logic selected;
        initial selected = 1'b1;
      end
      default: begin : generated_default
        logic selected;
        initial selected = 1'b0;
      end
    endcase
    case (WIDE_PACKAGE)
      128'h80000000000000000000000000000001:
        begin : generated_wide_case
          logic selected;
          initial selected = 1'b1;
        end
      default: begin : generated_wide_default
        logic selected;
        initial selected = 1'b0;
      end
    endcase
  endgenerate
  specify
    specparam WIDE_PATH = WIDE_PACKAGE;
  endspecify
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(parsed.ok());
    const auto elaborated = fsim::elaboration::elaborate(
        parsed.design, "sv:work.typed_constant_top");
    if (!elaborated.ok()) {
        for (const auto& diagnostic : elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(elaborated.ok());

    const auto* top =
        specialization_at(*elaborated.design, "typed_constant_top");
    const auto* child = specialization_at(
        *elaborated.design, "typed_constant_top.child");
    const auto* wide_child = specialization_at(
        *elaborated.design, "typed_constant_top.wide_child");
    const auto* alternate_wide_child = specialization_at(
        *elaborated.design, "typed_constant_top.alternate_wide_child");
    assert(
        top != nullptr && child != nullptr && wide_child != nullptr
        && alternate_wide_child != nullptr);
    assert(
        parameter_value(*top, "MAX_VALUE")
        == "18446744073709551615");
    assert(
        parameter_value(*top, "MAX_MINUS_ONE")
        == "18446744073709551614");
    assert(parameter_value(*top, "LOGICAL_SHIFT") == "15");
    assert(parameter_value(*top, "SIGNED_CAST") == "-1");
    assert(parameter_value(*top, "CONCAT_VALUE") == "165");
    assert(parameter_value(*top, "REPEAT_VALUE") == "204");
    assert(parameter_value(*top, "MIXED_WIDTH") == "16");
    assert(parameter_value(*top, "CONDITIONAL_VALUE") == "129");
    assert(parameter_value(*top, "UNKNOWN_VALUE") == "4'b10x1");
    assert(parameter_value(*top, "UNKNOWN_FLAG") == "1");
    assert(parameter_value(*top, "MEMBER_EXACT") == "1");
    assert(parameter_value(*top, "MEMBER_RANGE") == "1");
    assert(parameter_value(*top, "MEMBER_WILDCARD") == "1");
    assert(parameter_value(*top, "MEMBER_UNKNOWN") == "1'bx");
    assert(parameter_value(*top, "MEMBER_REVERSED") == "0");
    assert(parameter_value(*top, "IMPLICIT_EIGHT") == "255");
    assert(parameter_value(*top, "IMPLICIT_SIXTEEN") == "255");
    const auto wide_expected =
        std::string{"128'b1"} + std::string(126, '0') + "1";
    assert(parameter_value(*top, "WIDE_VALUE") == wide_expected);
    assert(
        parameter_value(*top, "WIDE_UNKNOWN")
        == std::string{"96'b"} + std::string(96, 'z'));
    assert(parameter_value(*top, "WIDE_TWO_STATE") == wide_expected);
    assert(parameter_value(*top, "WIDE_DECIMAL") == wide_expected);
    assert(
        parameter_value(*top, "WIDE_UNSIZED_DECIMAL")
        == std::string{"129'sb01"} + std::string(126, '0') + "1");
    assert(parameter_value(*top, "WIDE_CONCAT") == wide_expected);
    std::string wide_repeat_expected{"128'b"};
    for (std::size_t index = 0; index < 16U; ++index) {
        wide_repeat_expected += "10100101";
    }
    assert(parameter_value(*top, "WIDE_REPEAT") == wide_repeat_expected);
    assert(parameter_value(*top, "WIDE_PACKAGE") == wide_expected);
    assert(parameter_value(*top, "WIDE_QUALIFIED") == wide_expected);
    assert(parameter_value(*top, "WIDE_SMALL") == "4");
    assert(
        parameter_value(*top, "WIDE_ADD")
        == std::string{"128'b1"} + std::string(125, '0') + "11");
    assert(
        parameter_value(*top, "WIDE_SUBTRACT")
        == std::string{"128'b0"} + std::string(127, '1'));
    assert(parameter_value(*top, "WIDE_MULTIPLY") == "123456789000");
    assert(
        parameter_value(*top, "WIDE_DIVIDE")
        == std::string{"128'b01"} + std::string(126, '0'));
    assert(parameter_value(*top, "WIDE_MODULO") == "1");
    assert(parameter_value(*top, "WIDE_POWER") == "3486784401");
    assert(parameter_value(*top, "WIDE_NEGATE") == "-5");
    assert(parameter_value(*top, "WIDE_SIGNED_DIVIDE") == "-142");
    assert(parameter_value(*top, "WIDE_SIGNED_MODULO") == "-6");
    assert(parameter_value(*top, "WIDE_NEGATIVE_POWER") == "-1");
    assert(
        parameter_value(*top, "WIDE_BITWISE_NOT")
        == std::string{"128'b0"} + std::string(126, '1') + "0");
    assert(parameter_value(*top, "WIDE_WRAP") == "0");
    assert(
        parameter_value(*top, "WIDE_UNKNOWN_ARITHMETIC")
        == std::string{"128'b"} + std::string(128, 'x'));
    assert(parameter_value(*top, "WIDE_TRUNCATED") == "3");
    assert(parameter_value(*top, "WIDE_LOGICAL_TRUE") == "1");
    assert(parameter_value(*top, "WIDE_LOGICAL_UNKNOWN") == "1'bx");
    assert(parameter_value(*top, "WIDE_EQUAL") == "1");
    assert(parameter_value(*top, "WIDE_CASE_EQUAL") == "1");
    assert(parameter_value(*top, "WIDE_CASE_DIFFERENT") == "0");
    assert(parameter_value(*top, "WIDE_WILDCARD_EQUAL") == "1");
    assert(parameter_value(*top, "WIDE_WILDCARD_NOT_EQUAL") == "0");
    assert(parameter_value(*top, "WIDE_WILDCARD_UNKNOWN") == "1'bx");
    assert(parameter_value(*top, "WIDE_UNSIGNED_LESS") == "1");
    assert(parameter_value(*top, "WIDE_SIGNED_LESS") == "1");
    assert(parameter_value(*top, "WIDE_SIGN_EXTENDED_LESS") == "1");
    assert(
        parameter_value(*top, "WIDE_BITWISE_AND")
        == std::string{"128'b1"} + std::string(127, '0'));
    assert(parameter_value(*top, "WIDE_BITWISE_DOMINANCE") == "0");
    assert(
        parameter_value(*top, "WIDE_BITWISE_UNKNOWN")
        == std::string{"128'b"} + std::string(128, 'x'));
    assert(
        parameter_value(*top, "WIDE_LEFT_SHIFT")
        == std::string{"128'b1"} + std::string(127, '0'));
    assert(parameter_value(*top, "WIDE_LOGICAL_RIGHT") == "1");
    assert(parameter_value(*top, "WIDE_ARITHMETIC_RIGHT") == "-8");
    assert(
        parameter_value(*top, "WIDE_UNKNOWN_SHIFT")
        == std::string{"128'b"} + std::string(128, 'x'));
    assert(
        parameter_value(*top, "WIDE_Z_ARITHMETIC_SHIFT")
        == std::string{"128'sb"} + std::string(128, 'z'));
    assert(parameter_value(*top, "WIDE_EXCESSIVE_SHIFT") == "0");
    assert(
        parameter_value(*top, "WIDE_STREAM_LEFT")
        == std::string{"128'b00000001"} + std::string(120, '0'));
    assert(parameter_value(*top, "WIDE_STREAM_RIGHT") == wide_expected);
    assert(parameter_value(*top, "WIDE_REDUCE_AND") == "1");
    assert(parameter_value(*top, "WIDE_REDUCE_OR") == "1");
    assert(parameter_value(*top, "WIDE_REDUCE_XOR") == "0");
    assert(parameter_value(*top, "WIDE_REDUCE_XNOR") == "1");
    assert(parameter_value(*top, "WIDE_REDUCE_UNKNOWN") == "1'bx");
    assert(parameter_value(*top, "WIDE_SELECTED_BIT") == "1");
    assert(
        parameter_value(*top, "WIDE_PART_SELECT")
        == "9223372036854775808");
    assert(
        parameter_value(*top, "WIDE_INDEXED_PLUS")
        == "9223372036854775808");
    assert(
        parameter_value(*top, "WIDE_INDEXED_MINUS")
        == "9223372036854775808");
    assert(parameter_value(*top, "WIDE_LOW_SELECT") == "1");
    assert(parameter_value(*top, "WIDE_OUT_OF_RANGE") == "4'bxx10");
    assert(parameter_value(*top, "WIDE_ASCENDING_SELECT") == "8");
    assert(
        parameter_value(*top, "WIDE_UNKNOWN_CONDITIONAL")
        == std::string{"128'b1"} + std::string(125, '0') + "x1");
    assert(parameter_value(*top, "WIDE_INSIDE") == "1");
    assert(parameter_value(*top, "WIDE_POSITIONAL_PATTERN") == wide_expected);
    assert(
        parameter_value(*top, "WIDE_DEFAULT_PATTERN")
        == std::string{"128'b"} + std::string(128, '1'));
    assert(parameter_value(*top, "WIDE_BITS") == "128");
    assert(parameter_value(*top, "WIDE_UNKNOWN_BITS") == "96");
    assert(parameter_value(*top, "WIDE_LEFT") == "127");
    assert(parameter_value(*top, "WIDE_RIGHT") == "0");
    assert(parameter_value(*top, "WIDE_LOW") == "0");
    assert(parameter_value(*top, "WIDE_HIGH") == "127");
    assert(parameter_value(*top, "WIDE_SIZE") == "128");
    assert(parameter_value(*top, "WIDE_INCREMENT") == "1");
    assert(parameter_value(*top, "WIDE_DIMENSIONS") == "1");
    assert(parameter_value(*top, "WIDE_UNPACKED_DIMENSIONS") == "0");
    assert(parameter_value(*top, "WIDE_ASCENDING_LEFT") == "0");
    assert(parameter_value(*top, "WIDE_ASCENDING_RIGHT") == "127");
    assert(parameter_value(*top, "WIDE_ASCENDING_LOW") == "0");
    assert(parameter_value(*top, "WIDE_ASCENDING_HIGH") == "127");
    assert(parameter_value(*top, "WIDE_ASCENDING_INCREMENT") == "-1");
    assert(parameter_value(*top, "WIDE_CLOG2_POWER") == "127");
    assert(parameter_value(*top, "WIDE_CLOG2_NONPOWER") == "128");
    assert(parameter_value(*top, "WIDE_INT_CAST") == "1");
    assert(parameter_value(*top, "WIDE_BYTE_CAST") == "1");
    assert(parameter_value(*top, "WIDE_SHORTINT_CAST") == "1");
    assert(parameter_value(*top, "WIDE_LONGINT_CAST") == "1");
    assert(parameter_value(*top, "WIDE_LOGIC_CAST") == "1");
    assert(parameter_value(*top, "WIDE_BIT_CAST") == "1");
    assert(
        parameter_value(*top, "WIDE_INTEGER_UNKNOWN_CAST")
        == std::string{"32'b"} + std::string(28, '0') + "xxxx");
    assert(parameter_value(*top, "WIDE_CAST_IS_UNKNOWN") == "1");
    assert(parameter_value(*top, "WIDE_NAMED_CAST_HIGH") == "0");
    assert(
        parameter_value(*top, "WIDE_NAMED_CAST_LOW")
        == "81985529216486895");
    assert(parameter_value(*top, "WIDE_UPDATE_BIT") == "0");
    assert(parameter_value(*top, "WIDE_UPDATE_RETAINED") == "2147483647");
    assert(parameter_value(*top, "WIDE_UPDATE_HIGH") == "3735928559");
    assert(parameter_value(*top, "WIDE_UPDATE_MIDDLE") == "3405691582");
    assert(parameter_value(*top, "WIDE_UPDATE_LOW") == "4294967295");
    assert(parameter_value(*wide_child, "VALUE") == wide_expected);
    assert(
        parameter_value(*alternate_wide_child, "VALUE")
        == std::string{"128'b01"} + std::string(125, '0') + "1");
    assert(
        parameter_value(*top, "ENUM_MAX")
        == "18446744073709551615");
    assert(
        parameter_value(*top, "ENUM_PREVIOUS")
        == "18446744073709551614");
    assert(
        parameter_value(*child, "VALUE")
        == "18446744073709551615");

    const auto eight_identity =
        parameter_identity(*top, "IMPLICIT_EIGHT");
    const auto sixteen_identity =
        parameter_identity(*top, "IMPLICIT_SIXTEEN");
    assert(eight_identity.starts_with("svconst-v2:w=8:s=0:"));
    assert(sixteen_identity.starts_with("svconst-v2:w=16:s=0:"));
    assert(eight_identity != sixteen_identity);
    const auto wide_identity = parameter_identity(*top, "WIDE_VALUE");
    const auto wide_two_state_identity =
        parameter_identity(*top, "WIDE_TWO_STATE");
    assert(wide_identity.starts_with("svconst-v2:w=128:s=0:"));
    assert(wide_identity != wide_two_state_identity);
    assert(
        parameter_identity(*wide_child, "VALUE")
        != parameter_identity(*alternate_wide_child, "VALUE"));
    const auto wide_specparam = std::ranges::find_if(
        top->parameter_identity_values,
        [](const auto& value) {
            return value.first == "@specparam:WIDE_PATH";
        });
    assert(wide_specparam != top->parameter_identity_values.end());
    assert(wide_specparam->second.starts_with("svconst-v2:w=128:"));
    assert(
        wide_specparam->second.ends_with(
            ":v=" + wide_expected.substr(5U)));
    assert(
        parameter_identity(*top, "MAX_VALUE")
            == parameter_identity(*child, "VALUE"));

    const auto max_q =
        elaborated.design->find_signal("typed_constant_top.max_q");
    const auto mixed_q =
        elaborated.design->find_signal("typed_constant_top.mixed_q");
    const auto signed_q =
        elaborated.design->find_signal("typed_constant_top.signed_q");
    const auto unknown_q =
        elaborated.design->find_signal("typed_constant_top.unknown_q");
    const auto child_q =
        elaborated.design->find_signal(
            "typed_constant_top.child.q");
    const auto generated_q =
        elaborated.design->find_signal(
            "typed_constant_top.generated_true.generated_q");
    const auto generated_selected =
        elaborated.design->find_signal(
            "typed_constant_top.generated_case.selected");
    const auto generated_wide_selected =
        elaborated.design->find_signal(
            "typed_constant_top.generated_wide_case.selected");
    const auto wide_range_q = elaborated.design->find_signal(
        "typed_constant_top.wide_range_q");
    const auto wide_q = elaborated.design->find_signal(
        "typed_constant_top.wide_q");
    const auto wide_two_state_q = elaborated.design->find_signal(
        "typed_constant_top.wide_two_state_q");
    const auto wide_unknown_q = elaborated.design->find_signal(
        "typed_constant_top.wide_unknown_q");
    const auto wide_cast_q = elaborated.design->find_signal(
        "typed_constant_top.wide_cast_q");
    assert(
        max_q && mixed_q && signed_q && unknown_q && child_q
        && generated_q && generated_selected && generated_wide_selected
        && wide_range_q && wide_q && wide_two_state_q && wide_unknown_q
        && wide_cast_q);
    assert(
        !elaborated.design->find_signal(
            "typed_constant_top.generated_default.selected"));
    assert(
        !elaborated.design->find_signal(
            "typed_constant_top.generated_wide_default.selected"));
    assert(elaborated.design->signals()[*wide_range_q].width == 5U);
    assert(elaborated.design->signals()[*wide_q].width == 128U);
    assert(
        elaborated.design->signals()[*wide_q].source_domain
        == fsim::frontend::ValueDomain::Logic4);
    assert(elaborated.design->signals()[*wide_two_state_q].width == 128U);
    assert(
        elaborated.design->signals()[*wide_two_state_q].source_domain
        == fsim::frontend::ValueDomain::Bit2);
    assert(elaborated.design->signals()[*wide_unknown_q].width == 96U);
    assert(
        elaborated.design->signals()[*wide_unknown_q].source_domain
        == fsim::frontend::ValueDomain::Logic4);
    assert(elaborated.design->signals()[*wide_cast_q].width == 128U);
    auto interpreter = elaborated.design->create_interpreter();
    interpreter->start();
    (void)interpreter->run();
    assert(
        interpreter->signal_value(*max_q).to_msb_string()
        == "1111111111111111111111111111111111111111111111111111111111111111");
    assert(
        interpreter->signal_value(*mixed_q).to_msb_string()
        == "00010000");
    assert(
        interpreter->signal_value(*signed_q).to_msb_string()
        == "1111111110000000");
    const auto unknown_value =
        interpreter->signal_value(*unknown_q).to_msb_string();
    if (unknown_value != "10X1") {
        std::cerr << "unexpected UNKNOWN_VALUE runtime result: "
                  << unknown_value << '\n';
    }
    assert(unknown_value == "10X1");
    assert(
        interpreter->signal_value(*child_q).to_msb_string()
        == "1111111111111111111111111111111111111111111111111111111111111111");
    assert(
        interpreter->signal_value(*generated_q).to_msb_string()
        == "1111111111111111111111111111111111111111111111111111111111111101");
    assert(
        interpreter->signal_value(*generated_selected).to_msb_string()
        == "1");
    assert(
        interpreter->signal_value(*wide_q).to_msb_string()
        == wide_expected.substr(5U));
    assert(
        interpreter->signal_value(*wide_two_state_q).to_msb_string()
        == wide_expected.substr(5U));
    assert(
        interpreter->signal_value(*wide_unknown_q).to_msb_string()
        == std::string(96, 'Z'));
    assert(
        interpreter->signal_value(*wide_cast_q).to_msb_string()
        == std::string(32, '0') + std::string(96, 'Z'));

    const auto lossy = fsim::frontend::parse_text(
        "lossy-two-state-constant.sv",
        R"(
module lossy_two_state_constant #(
  parameter bit [3:0] BAD = 4'b10x1
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(lossy.ok());
    const auto rejected = fsim::elaboration::elaborate(
        lossy.design, "sv:work.lossy_two_state_constant");
    assert(!rejected.ok());
    assert(has_diagnostic(rejected, "FSIM-ELAB-SVCONST-001"));

    const auto lossy_cast = fsim::frontend::parse_text(
        "lossy-two-state-cast.sv",
        R"(
module lossy_two_state_cast #(
  parameter bit BAD = bit'(1'bx)
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(lossy_cast.ok());
    const auto rejected_cast = fsim::elaboration::elaborate(
        lossy_cast.design, "sv:work.lossy_two_state_cast");
    assert(!rejected_cast.ok());
    assert(has_diagnostic(rejected_cast, "FSIM-ELAB-PARAM-005"));

    const auto negative_clog2 = fsim::frontend::parse_text(
        "negative-wide-clog2.sv",
        R"(
module negative_wide_clog2 #(
  parameter int BAD = $clog2(
      $signed(128'hffffffffffffffffffffffffffffffff))
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(negative_clog2.ok());
    const auto rejected_clog2 = fsim::elaboration::elaborate(
        negative_clog2.design, "sv:work.negative_wide_clog2");
    assert(!rejected_clog2.ok());
    assert(has_diagnostic(rejected_clog2, "FSIM-ELAB-PARAM-005"));

    const auto unknown_clog2 = fsim::frontend::parse_text(
        "unknown-wide-clog2.sv",
        R"(
module unknown_wide_clog2 #(
  parameter int BAD =
      $clog2(128'hxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx)
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(unknown_clog2.ok());
    const auto rejected_unknown_clog2 = fsim::elaboration::elaborate(
        unknown_clog2.design, "sv:work.unknown_wide_clog2");
    assert(!rejected_unknown_clog2.ok());
    assert(has_diagnostic(
        rejected_unknown_clog2, "FSIM-ELAB-PARAM-005"));

    const auto invalid_constant_dimension = fsim::frontend::parse_text(
        "invalid-constant-dimension.sv",
        R"(
module invalid_constant_dimension #(
  parameter int BAD = $left(128'd0, 2)
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_constant_dimension.ok());
    const auto rejected_constant_dimension =
        fsim::elaboration::elaborate(
            invalid_constant_dimension.design,
            "sv:work.invalid_constant_dimension");
    assert(!rejected_constant_dimension.ok());
    assert(has_diagnostic(
        rejected_constant_dimension, "FSIM-ELAB-PARAM-005"));

    const auto excessive_work = fsim::frontend::parse_text(
        "excessive-wide-constant-work.sv",
        R"(
module excessive_wide_constant_work #(
  parameter logic [9999999:0] BAD = {10000000{1'b0}}
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(excessive_work.ok());
    const auto rejected_work = fsim::elaboration::elaborate(
        excessive_work.design, "sv:work.excessive_wide_constant_work");
    assert(!rejected_work.ok());
    assert(has_diagnostic(rejected_work, "FSIM-ELAB-PARAM-005"));
    assert(std::ranges::any_of(
        rejected_work.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.message.find("constant-evaluation work limit")
                != std::string::npos;
        }));

    const auto excessive_multiply_work = fsim::frontend::parse_text(
        "excessive-wide-multiply-work.sv",
        R"(
module excessive_wide_multiply_work #(
  parameter logic [8999:0] BAD = 9000'd1 * 9000'd1
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(excessive_multiply_work.ok());
    const auto rejected_multiply_work = fsim::elaboration::elaborate(
        excessive_multiply_work.design,
        "sv:work.excessive_wide_multiply_work");
    assert(!rejected_multiply_work.ok());
    assert(has_diagnostic(
        rejected_multiply_work, "FSIM-ELAB-PARAM-005"));
    assert(std::ranges::any_of(
        rejected_multiply_work.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.message.find("multiplicative constant evaluation")
                    != std::string::npos
                && diagnostic.message.find("work limit")
                    != std::string::npos;
        }));

    const auto signed_division_overflow = fsim::frontend::parse_text(
        "wide-signed-division-overflow.sv",
        R"(
module wide_signed_division_overflow #(
  parameter logic signed [127:0] BAD =
      $signed(128'h80000000000000000000000000000000)
      / $signed(128'hffffffffffffffffffffffffffffffff)
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(signed_division_overflow.ok());
    const auto rejected_division_overflow = fsim::elaboration::elaborate(
        signed_division_overflow.design,
        "sv:work.wide_signed_division_overflow");
    assert(!rejected_division_overflow.ok());
    assert(std::ranges::any_of(
        rejected_division_overflow.diagnostics,
        [](const auto& diagnostic) {
            return diagnostic.message.find(
                       "division overflows the signed destination width")
                != std::string::npos;
        }));

    const auto boundary_parent = fsim::frontend::parse_text(
        "unsigned-boundary.sv",
        R"(
module unsigned_boundary #(
  parameter longint unsigned VALUE = 64'hffffffffffffffff
) ();
  foreign_integer #(.value(VALUE)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    const auto boundary_child = fsim::frontend::parse_text(
        "foreign-integer.vhd",
        R"(
entity foreign_integer is
  generic (value : natural := 0);
end entity;
architecture rtl of foreign_integer is
begin
end architecture;
)",
        fsim::frontend::Language::Vhdl2008);
    assert(boundary_parent.ok() && boundary_child.ok());
    auto boundary_design = boundary_parent.design;
    boundary_design.units.insert(
        boundary_design.units.end(),
        boundary_child.design.units.begin(),
        boundary_child.design.units.end());
    const std::vector<fsim::elaboration::Binding> bindings{
        {"unsigned_boundary.child",
         "vhdl:work.foreign_integer(rtl)",
         std::nullopt},
    };
    const auto rejected_boundary = fsim::elaboration::elaborate(
        boundary_design,
        "sv:work.unsigned_boundary",
        bindings);
    assert(!rejected_boundary.ok());
    assert(has_diagnostic(
        rejected_boundary, "FSIM-ELAB-GENERIC-004"));

    const auto wide_small_parent = fsim::frontend::parse_text(
        "wide-small-boundary.sv",
        R"(
module wide_small_boundary #(
  parameter logic [127:0] VALUE = 128'd4
) ();
  foreign_integer #(.value(VALUE)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(wide_small_parent.ok());
    auto wide_small_design = wide_small_parent.design;
    wide_small_design.units.insert(
        wide_small_design.units.end(),
        boundary_child.design.units.begin(),
        boundary_child.design.units.end());
    const std::vector<fsim::elaboration::Binding> wide_small_bindings{
        {"wide_small_boundary.child",
         "vhdl:work.foreign_integer(rtl)",
         std::nullopt},
    };
    const auto wide_small_elaborated = fsim::elaboration::elaborate(
        wide_small_design,
        "sv:work.wide_small_boundary",
        wide_small_bindings);
    assert(wide_small_elaborated.ok());
    const auto* wide_small_child = specialization_at(
        *wide_small_elaborated.design,
        "wide_small_boundary.child");
    assert(wide_small_child != nullptr);
    assert(parameter_value(*wide_small_child, "value") == "4");

    const auto scalar_parameters = fsim::frontend::parse_text(
        "scalar-parameters.sv",
        R"(
timeunit 1ns / 1ps;
package scalar_values;
  localparam real PACKAGE_BASE = 1.25;
  localparam realtime PACKAGE_PERIOD = 2.5ns;
endpackage

module scalar_parameter_child #(
  parameter real VALUE = 0.0,
  parameter time TICKS = 0
) ();
endmodule

module scalar_parameter_top #(
  parameter real BASE = PACKAGE_BASE,
  localparam real DERIVED = BASE + 2.0,
  parameter realtime PERIOD = PACKAGE_PERIOD,
  localparam time TICKS = PERIOD
) ();
  import scalar_values::*;
  scalar_parameter_child #(.VALUE(DERIVED), .TICKS(TICKS)) inherited();
  scalar_parameter_child #(.VALUE(4.5), .TICKS(1.5)) override();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    if (!scalar_parameters.ok()) {
        for (const auto& diagnostic : scalar_parameters.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(scalar_parameters.ok());
    const auto scalar_elaborated = fsim::elaboration::elaborate(
        scalar_parameters.design, "sv:work.scalar_parameter_top");
    if (!scalar_elaborated.ok()) {
        for (const auto& diagnostic : scalar_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(scalar_elaborated.ok());
    const auto* scalar_top = specialization_at(
        *scalar_elaborated.design, "scalar_parameter_top");
    const auto* inherited = specialization_at(
        *scalar_elaborated.design, "scalar_parameter_top.inherited");
    const auto* overridden = specialization_at(
        *scalar_elaborated.design, "scalar_parameter_top.override");
    assert(scalar_top && inherited && overridden);
    assert(parameter_value(*scalar_top, "PACKAGE_BASE") == "1.25");
    assert(parameter_value(*scalar_top, "BASE") == "1.25");
    assert(parameter_value(*scalar_top, "DERIVED") == "3.25");
    assert(parameter_value(*scalar_top, "PERIOD") == "2.5");
    assert(parameter_value(*scalar_top, "TICKS") == "3");
    assert(parameter_value(*inherited, "VALUE") == "3.25");
    assert(parameter_value(*inherited, "TICKS") == "3");
    assert(parameter_value(*overridden, "VALUE") == "4.5");
    assert(parameter_value(*overridden, "TICKS") == "2");
    assert(parameter_identity(*scalar_top, "BASE").starts_with("svscalar-v1:"));
    assert(parameter_identity(*scalar_top, "BASE")
           != parameter_identity(*scalar_top, "DERIVED"));
    assert(parameter_identity(*inherited, "VALUE")
           != parameter_identity(*overridden, "VALUE"));

    const auto chandle_parameters = fsim::frontend::parse_text(
        "chandle-parameters.sv",
        R"(
module chandle_parameter_top #(
  parameter chandle EMPTY = null,
  parameter chandle ALIAS = EMPTY,
  parameter bit IS_NULL = (ALIAS == null),
  parameter chandle CAST_NULL = chandle'(null)
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(chandle_parameters.ok());
    const auto chandle_elaborated = fsim::elaboration::elaborate(
        chandle_parameters.design, "sv:work.chandle_parameter_top");
    if (!chandle_elaborated.ok()) {
        for (const auto& diagnostic : chandle_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(chandle_elaborated.ok());
    const auto* chandle_top = specialization_at(
        *chandle_elaborated.design, "chandle_parameter_top");
    assert(chandle_top != nullptr);
    assert(parameter_value(*chandle_top, "EMPTY") == "null");
    assert(parameter_value(*chandle_top, "ALIAS") == "null");
    assert(parameter_value(*chandle_top, "IS_NULL") == "1");
    assert(parameter_value(*chandle_top, "CAST_NULL") == "null");
    assert(parameter_identity(*chandle_top, "EMPTY").starts_with(
        "svscalar-v1:"));
    assert(parameter_identity(*chandle_top, "EMPTY")
           == parameter_identity(*chandle_top, "ALIAS"));

    const auto invalid_chandle_actual = fsim::frontend::parse_text(
        "invalid-chandle-actual.sv",
        R"(
module chandle_actual_child #(parameter chandle HANDLE = null) ();
endmodule
module invalid_chandle_actual_top;
  chandle_actual_child #(.HANDLE(1)) child();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_chandle_actual.ok());
    const auto rejected_chandle_actual = fsim::elaboration::elaborate(
        invalid_chandle_actual.design,
        "sv:work.invalid_chandle_actual_top");
    assert(!rejected_chandle_actual.ok());
    assert(has_diagnostic(
        rejected_chandle_actual, "FSIM-ELAB-PARAM-004"));

    const auto invalid_dependency = fsim::frontend::parse_text(
        "scalar-parameter-forward.sv",
        R"(
module scalar_parameter_forward #(
  parameter real FIRST = LATER + 1.0,
  parameter real LATER = 2.0
) ();
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(invalid_dependency.ok());
    const auto rejected_dependency = fsim::elaboration::elaborate(
        invalid_dependency.design, "sv:work.scalar_parameter_forward");
    assert(!rejected_dependency.ok());
    assert(has_diagnostic(rejected_dependency, "FSIM-ELAB-PARAM-005"));

    const auto scalar_profiles = fsim::frontend::parse_text(
        "scalar-profiles.sv",
        R"(
module scalar_profile_child(
  input var real source,
  output var shortreal narrowed,
  inout wire time ticks,
  input chandle foreign
);
  function automatic real scale(
      input real value,
      input realtime factor = 1.0);
    return value;
  endfunction
  task automatic adjust(
      ref real value,
      input time delay = 2);
  endtask
endmodule

module scalar_profile_top;
  wire real source;
  shortreal narrowed;
  wire time ticks;
  chandle foreign;
  scalar_profile_child child(source, narrowed, ticks, foreign);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(scalar_profiles.ok());
    const auto profile_elaborated = fsim::elaboration::elaborate(
        scalar_profiles.design, "sv:work.scalar_profile_top");
    if (!profile_elaborated.ok()) {
        for (const auto& diagnostic : profile_elaborated.diagnostics) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(profile_elaborated.ok());
    const auto source_signal = profile_elaborated.design->find_signal(
        "scalar_profile_top.source");
    const auto narrow_signal = profile_elaborated.design->find_signal(
        "scalar_profile_top.narrowed");
    const auto tick_signal = profile_elaborated.design->find_signal(
        "scalar_profile_top.ticks");
    const auto foreign_signal = profile_elaborated.design->find_signal(
        "scalar_profile_top.foreign");
    assert(source_signal && narrow_signal && tick_signal && foreign_signal);
    const auto& profile_signals = profile_elaborated.design->signals();
    assert(profile_signals[*source_signal].systemverilog_scalar
           == fsim::frontend::SystemVerilogScalarKind::Real);
    assert(profile_signals[*narrow_signal].systemverilog_scalar
           == fsim::frontend::SystemVerilogScalarKind::ShortReal);
    assert(profile_signals[*tick_signal].systemverilog_scalar
           == fsim::frontend::SystemVerilogScalarKind::Time);
    assert(profile_signals[*foreign_signal].systemverilog_scalar
           == fsim::frontend::SystemVerilogScalarKind::Chandle);

    const auto mismatched_profile = fsim::frontend::parse_text(
        "scalar-profile-mismatch.sv",
        R"(
module scalar_profile_mismatch_child(
  input shortreal value,
  input chandle foreign);
endmodule
module scalar_profile_mismatch_top;
  real value;
  real foreign;
  scalar_profile_mismatch_child child(value, foreign);
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(mismatched_profile.ok());
    const auto rejected_profile = fsim::elaboration::elaborate(
        mismatched_profile.design, "sv:work.scalar_profile_mismatch_top");
    assert(!rejected_profile.ok());
    assert(has_diagnostic(rejected_profile, "FSIM-ELAB-BIND-019"));

    const auto scalar_negative_matrix = fsim::frontend::parse_text(
        "scalar-negative-matrix.sv",
        R"(
module scalar_conversion_overflow;
  shortreal value;
  initial value = 1.0e400;
endmodule

module scalar_unsupported_operator;
  real left;
  real right;
  real result;
  initial result = left % right;
endmodule
)",
        fsim::frontend::Language::SystemVerilog2017);
    assert(scalar_negative_matrix.ok());
    const auto rejected_scalar_conversion = fsim::elaboration::elaborate(
        scalar_negative_matrix.design,
        "sv:work.scalar_conversion_overflow");
    assert(!rejected_scalar_conversion.ok());
    assert(has_diagnostic(
        rejected_scalar_conversion, "FSIM-ELAB-SVSCALAR-001"));
    const auto rejected_scalar_operator = fsim::elaboration::elaborate(
        scalar_negative_matrix.design,
        "sv:work.scalar_unsupported_operator");
    assert(!rejected_scalar_operator.ok());
    assert(has_diagnostic(
        rejected_scalar_operator, "FSIM-ELAB-SVSCALAR-002"));
}

} // namespace fsim::tests::elaboration
