// SPDX-License-Identifier: Apache-2.0
module top;
  logic clk = 1'b0;
  logic d = 1'b0;
  logic q = 1'b0;
  logic s = 1'b0;
  logic [3:0] v = 4'h0;
  real r = 0.0;
  always #5 clk = ~clk;
  always @(posedge clk) q <= d;
  initial #42 $finish;
endmodule
