// SPDX-License-Identifier: Apache-2.0
module cd_divider_tb;
  logic clk = 1'b0;
  logic rst = 1'b1; logic [3:0] q2, q4;
  cd_divider dut(.clk(clk), .rst(rst), .q2(q2), .q4(q4));
  always #5 clk = ~clk;
  initial begin
    #3 rst = 1'b0;
  end
  always @(negedge clk) $display("%0t q2=%h q4=%h", $time, q2, q4);
  initial #200 $finish;
endmodule
