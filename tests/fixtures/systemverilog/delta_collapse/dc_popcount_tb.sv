// SPDX-License-Identifier: Apache-2.0
module dc_popcount_tb;
  logic clk = 1'b0;
  logic [23:0] a = '0;
  wire [7:0] q;
  dc_popcount dut(.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial repeat (16) #7 a = a + 24'h13579b;
  initial #130 $finish;
  always @(posedge clk) $display("%0t q=%h", $time, q);
endmodule
