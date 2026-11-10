// SPDX-License-Identifier: Apache-2.0
//
// A disable condition reads current values at any time of an attempt (IEEE
// 1800-2023 16.12). A sampled value function in it changes value at the
// start of a time step without a change of its operands there: $sampled
// gives the value from before the time step (16.5.1, 16.9.3), and a value
// change function compares the sampled value in the time step with the one
// at the most recent strictly prior tick of its clock (16.9.3). The
// attempts in progress are disabled in the first time step where the
// condition is true (no outcome, no action block), between the ticks of the
// assertion's clock too.
//
// b stays 0, so each attempt fails at its end unless it is disabled:
//   p1  disable iff ($sampled(r)), a1 |-> ##2 b, attempts from 15, 55, 95.
//       r is 1 from 22 to 24: $sampled(r) is 1 in the time steps 23 and
//       24, so the attempt from 15 is disabled at 23; the ticks 25 and 35
//       read $sampled(r) 0, so a reading at the ticks only would not
//       disable it. r is 1 from 62 to 64: the attempt from 55 is disabled
//       at 63. The attempt from 95 fails at 115.
//   p2  disable iff ($rose(r2, @(posedge clk2))), a2 |-> ##2 b, attempts
//       from 15 and 45. r2 rises at 21; clk2 rises at 22 and 24. At its
//       first tick, 22, $rose compares r2's sampled value 1 with its
//       default sampled value 0 (16.5.1, 16.9.3): true, so the attempt from
//       15 is disabled at 22. At the tick 24 and at the ticks of clk (25,
//       35) it is 0 (r2 sampled 1 at 22 and at 24). The attempt from 45
//       fails at 65.
module top;
  bit clk, clk2, a1, a2, b, r, r2;
  always #5 clk = ~clk;

  always @(negedge clk) begin
    a1 <= int'($time) + 5 inside {15, 55, 95};
    a2 <= int'($time) + 5 inside {15, 45};
  end

  p1: assert property (@(posedge clk) disable iff ($sampled(r)) a1 |-> ##2 b)
    else $display("inl p1 fail t=%0t", $time);
  p2: assert property (@(posedge clk) disable iff ($rose(r2, @(posedge clk2)))
      a2 |-> ##2 b)
    else $display("inl p2 fail t=%0t", $time);

  initial begin
    #21 r2 = 1;
    #1 clk2 = 1;  // 22
    r = 1;
    #1 clk2 = 0;  // 23
    #1 clk2 = 1;  // 24
    r = 0;
    #1 clk2 = 0;  // 25
    #37 r = 1;    // 62
    #2 r = 0;     // 64
    #56 $finish;  // 120
  end
endmodule
