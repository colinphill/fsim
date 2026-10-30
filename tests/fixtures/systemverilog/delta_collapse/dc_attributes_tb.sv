// SPDX-License-Identifier: Apache-2.0
module dc_attributes_tb;
  logic clk = 1'b0;
  logic a = 1'b0;
  wire [7:0] q;
  dc_attributes dut(.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial repeat (9) #7 a = ~a;
  initial #100 $finish;
  always @(posedge clk) $display("%0t q=%0d", $time, q);
endmodule
