// SPDX-License-Identifier: Apache-2.0
module dc_attributes_tb;
  logic clk = 1'b0;
  logic a = 1'b0;
  wire [7:0] q;
  wire [1:0] r;
  dc_attributes dut(.clk(clk), .a(a), .q(q), .r(r));
  always #5 clk = ~clk;
  initial repeat (9) #7 a = ~a;
  initial #100 $finish;
  always @(posedge clk) $display("%0t q=%0d r=%b", $time, q, r);
endmodule
