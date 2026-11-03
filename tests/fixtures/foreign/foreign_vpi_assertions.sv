// SPDX-License-Identifier: Apache-2.0
// Concurrent assertions for the assertion API conformance plug-in (IEEE
// 1800-2023 39). Clock edges at 5, 15, 25, ...: a_req passes vacuously at 5,
// passes at 15 with its pass action off, fails at 25, is disabled by rst at
// 35, and is controlled from the plug-in afterwards.
module top;
  logic clk = 0, a = 0, b = 0, rst = 0;
  always #5 clk = ~clk;

  a_req: assert property (@(posedge clk) disable iff (rst) a |-> b)
    $display("#c action pass a_req %0t", $time);
  else
    $display("#c action fail a_req %0t", $time);
  c_hit: cover property (@(posedge clk) a && b);
  assume property (@(posedge clk) !(b && rst));

  initial begin
    #12 a = 1; b = 1;
    #10 b = 0;
    #10 rst = 1;
    #10 rst = 0;
    #10 b = 1;
    #20 $finish;
  end

  // Multi-cycle attempts (39.4.2, 39.5.2): one starts at every edge and
  // reaches its outcome two edges later; the plug-in kills the one that
  // started at 15.
  a_seq: assert property (@(posedge clk) a ##2 b)
  else
    $display("action fail a_seq %0t", $time);
  cs_hit: cover sequence (@(posedge clk) a ##1 b);
endmodule
