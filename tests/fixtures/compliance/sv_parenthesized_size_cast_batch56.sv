// fsim: top=sv_parenthesized_size_cast_batch56 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 6.24.1: a parenthesized constant expression may size a cast.
module sv_parenthesized_size_cast_batch56;
  parameter int TOP_W = 8;

  logic [TOP_W*8-1:0] all_ones;
  logic [TOP_W*4-1:0] padded_value;
  logic signed [7:0] signed_input;
  logic signed [TOP_W*2-1:0] signed_result;
  bit [7:0] two_state_input;
  bit [TOP_W*2-1:0] two_state_result;
  logic [7:0] four_state_input;
  logic [TOP_W*2-1:0] four_state_result;
  logic [63:0] function_sized;
  localparam logic [7:0] constant_inner = (3)'(17);
  localparam logic [7:0] constant_nested = (8)'((3)'(17));

  function automatic int constant_size(input int value);
    constant_size = value * 8;
  endfunction

  initial begin
    all_ones = (TOP_W * 8)'('1);
    padded_value = (TOP_W * 4)'(8'h12);
    signed_input = -8'sd2;
    signed_result = (TOP_W * 2)'(signed_input);
    two_state_input = 8'h12;
    two_state_result = (TOP_W * 2)'(two_state_input);
    four_state_input = 8'h0x;
    four_state_result = (TOP_W * 2)'(four_state_input);
    function_sized = (constant_size(TOP_W))'('1);
    if (all_ones !== 64'hffff_ffff_ffff_ffff)
      $fatal(1, "parenthesized size cast of unbased unsized literal");
    if (padded_value !== 32'h0000_0012)
      $fatal(1, "parenthesized size cast of ordinary operand");
    if (signed_result !== -16'sd2)
      $fatal(1, "signed operand size cast");
    if (two_state_result !== 16'h0012)
      $fatal(1, "two-state operand size cast");
    if (four_state_result[0] !== 1'bx)
      $fatal(1, "four-state operand size cast");
    if (function_sized !== 64'hffff_ffff_ffff_ffff)
      $fatal(1, "constant-function size cast");
    if (constant_inner !== 8'h01)
      $fatal(1, "constant initializer applies inner size cast");
    if (constant_nested !== 8'h01)
      $fatal(1, "nested constant initializer preserves inner size");
    $display("PASS");
  end
endmodule
