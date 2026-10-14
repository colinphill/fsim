// fsim: top=top
// SPDX-License-Identifier: Apache-2.0
module top;
  parameter inferred_logical = 8'shaa >> 3'sb110;
  parameter inferred_arithmetic = 8'shaa >>> 3'sb110;
  localparam logic [7:0] logical_right = 8'shaa >> 3'sb110;
  localparam logic signed [7:0] arithmetic_right = 8'shaa >>> 3'sb110;
  localparam logic [7:0] left_shift = 8'h03 << 3'sb110;
  localparam logic [7:0] large_logical = 8'shaa >> -1;
  localparam logic signed [7:0] large_arithmetic = 8'shaa >>> -1;
  localparam logic [7:0] unknown_count = 8'hff >> 3'bx01;
  localparam logic [7:0] wide_count = 8'hff << 80'h80000000000000000000;
  logic signed [7:0] value_in;
  logic signed [2:0] count;

  initial begin
    value_in = 8'shaa;
    count = 3'sb110;
    #1;
    if (inferred_logical !== 8'h02 || inferred_arithmetic !== 8'hfe)
      $fatal(1, "inferred parameter comparison lost width or signedness");
    if (logical_right !== 8'h02 || arithmetic_right !== 8'hfe
        || left_shift !== 8'hc0 || large_logical !== 8'h00
        || large_arithmetic !== 8'hff || unknown_count !== 8'hxx
        || wide_count !== 8'h00
        || (value_in >> count) !== logical_right
        || (value_in >>> count) !== arithmetic_right)
      $fatal(1, "shift count unsigned interpretation failed");
    $display("PASS");
    $finish;
  end
endmodule
