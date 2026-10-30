// SPDX-License-Identifier: Apache-2.0
module cd_buffer_tb;
  logic clk = 1'b0;
  logic [3:0] count;
  cd_buffer dut(.clk(clk), .count(count));
  always #5 clk = ~clk;
  always @(negedge clk) $display("%0t count=%h", $time, count);
  initial #100 $finish;
endmodule
