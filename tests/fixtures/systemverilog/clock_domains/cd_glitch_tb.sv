// SPDX-License-Identifier: Apache-2.0
module cd_glitch_tb;
  logic clk = 1'b0;
  logic en_d = 1'b0; logic [3:0] q;
  cd_glitch dut(.clk(clk), .en_d(en_d), .q(q));
  always #5 clk = ~clk;
  initial begin
    #12 en_d = 1'b1; #40 en_d = 1'b0; #20 en_d = 1'b1;
  end
  always @(negedge clk) $display("%0t q=%h", $time, q);
  initial #200 $finish;
endmodule
