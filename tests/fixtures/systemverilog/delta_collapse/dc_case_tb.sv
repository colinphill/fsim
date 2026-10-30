// SPDX-License-Identifier: Apache-2.0
module dc_case_tb;
  logic clk = 1'b0;
  logic [3:0] a = 4'd0;
  wire [7:0] q;
  dc_case dut(.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial repeat (16) #7 a = a + 4'd7;
  initial #130 $finish;
  always @(posedge clk) $display("%0t q=%h", $time, q);
endmodule
