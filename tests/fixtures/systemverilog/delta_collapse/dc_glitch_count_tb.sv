// SPDX-License-Identifier: Apache-2.0
module dc_glitch_count_tb;
  logic clk = 1'b0;
  logic a = 1'b0;
  wire q;
  dc_glitch_count dut(.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial begin
    #12 a = 1'b1;
    #20 a = 1'b0;
    #20 a = 1'b1;
    #20 a = 1'b0;
  end
  initial #100 $finish;
  always @(posedge clk) $display("%0t q=%b", $time, q);
endmodule
