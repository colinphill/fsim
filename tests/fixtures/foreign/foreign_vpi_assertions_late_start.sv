// SPDX-License-Identifier: Apache-2.0
// A cbAssertionStart callback registered mid-run (IEEE 1800-2023 39.4.2).
// clk rises at 5, 15, 25 and 35; $finish runs at 32. The plug-in registers
// the callback at time 12, so the attempts that start at 15 and 25 are
// reported and the one at 5 is not.
module top;
  logic clk = 0, a = 1;
  always #5 clk = ~clk;
  p1: assert property (@(posedge clk) a);
  initial #32 $finish;
endmodule
