// SPDX-License-Identifier: Apache-2.0
module dc_chain_tb;
  logic clk = 1'b0;
  logic [3:0] a = 4'd1;
  wire [3:0] q;
  dc_chain dut(.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial repeat (14) #7 a = a + 4'd5;
  initial #120 $finish;
  always @(posedge clk) $display("%0t q=%h", $time, q);
endmodule
