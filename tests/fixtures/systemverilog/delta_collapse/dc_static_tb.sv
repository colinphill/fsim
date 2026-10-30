// SPDX-License-Identifier: Apache-2.0
module dc_static_tb;
  logic clk = 1'b0;
  logic [3:0] a = 4'd0;
  wire [15:0] q;
  dc_static dut(.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial repeat (12) #7 a = a + 4'd3;
  initial #100 $finish;
  always @(posedge clk) $display("%0t q=%h", $time, q);
endmodule
