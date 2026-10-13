// fsim: top=sv_conditional_intrinsic_width_batch56 std=2017
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2017 11.6, 11.8.2: evaluate at the common operand/context width.
module sv_conditional_intrinsic_width_batch56;
  logic [5:0] bits6;
  logic [7:0] addend;
  logic select_value;
  logic [15:0] result_value;
  wire [15:0] constant_result = 1 ? ~bits6 >>> 5 : 0;
  initial begin
    bits6 = '1;
    addend = 8'h80;
    select_value = 1;
    result_value = select_value ? ~bits6 >>> 5 : 0;
    if (result_value !== 16'hfffe) $fatal(1, "conditional narrowed shift too early");
    result_value = select_value ? addend + addend : 0;
    if (result_value !== 16'h0100) $fatal(1, "common branch width lost carry");
    select_value = 1'bx;
    result_value = select_value ? ~bits6 >>> 5 : 0;
    if (result_value !== 16'bxxxxxxxxxxxxxxx0) $fatal(1, "unknown conditional merge");
    #1;
    if (constant_result !== 16'hfffe) $fatal(1, "constant conditional narrowed shift");
    $display("PASS");
  end
endmodule
