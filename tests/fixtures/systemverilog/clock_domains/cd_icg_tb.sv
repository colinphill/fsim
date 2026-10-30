// SPDX-License-Identifier: Apache-2.0
module cd_icg_tb;
  logic clk = 1'b0;
  logic en = 1'b0; logic [3:0] q;
  cd_icg dut(.clk(clk), .en(en), .q(q));
  always #5 clk = ~clk;
  initial begin
    #12 en = 1'b1; #40 en = 1'b0; #20 en = 1'b1;
  end
  always @(negedge clk) $display("%0t q=%h", $time, q);
  initial #200 $finish;
endmodule
