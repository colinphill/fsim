// SPDX-License-Identifier: Apache-2.0
module dc_slice_tb;
  logic clk = 1'b0;
  logic [11:0] a = '0;
  wire [7:0] q;
  dc_slice dut(.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial repeat (16) #7 a = a + 12'd11;
  initial #130 $finish;
  always @(posedge clk) $display("%0t q=%h", $time, q);
endmodule
