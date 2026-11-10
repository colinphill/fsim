// SPDX-License-Identifier: Apache-2.0
//
// accept_on and reject_on are asynchronous aborts (IEEE 1800-2023
// 16.12.14): their abort condition is evaluated with sampled values at the
// granularity of the simulation time step, not only at the ticks of the
// clock, while sync_accept_on and sync_reject_on evaluate it only at the
// time steps of the clock's ticks. The abort is delivered at the time steps
// that exist in the run: none is made for it.
//
// b stays 0, so every attempt that starts (a sampled 1 at its tick) fails
// at the end of its sequence unless an abort ends it first:
//   p1  a1 |-> reject_on(r) ##3 b, attempts from 15, 65 and 95.
//       r is 1 from 22 to 23: its sampled value is 1 in the time step 23,
//       so the attempt from 15 fails at 23, not at 45.
//       r rises and falls within the time step 70 (#0): its sampled value
//       never is 1, so the attempt from 65 fails at its end, 95.
//       r is 1 from 100 to 104: the next time step is 104, where its
//       sampled value is 1, so the attempt from 95 fails at 104.
//   p2  a2 |-> sync_reject_on(r) ##3 b, attempts from 15 and 95: no tick
//       sees r sampled 1 (the ticks 25 and 105 sample 0), so the attempts
//       fail at their ends, 45 and 125.
//   p3  cover property accept_on(c) (a3 ##2 b), attempts at every tick;
//       those where a3 is sampled 0 (all but 25 and 55) fail at once. c is
//       1 from 43 to 44: in the time step 44 its sampled value is 1 and the
//       attempt from 25 succeeds there, so its pass statement runs (16.14.3);
//       the one from 55 fails at 75 (a cover has no fail statement).
module top;
  bit clk, a1, a2, a3, b, r, c;
  always #5 clk = ~clk;

  always @(negedge clk) begin
    a1 <= int'($time) + 5 inside {15, 65, 95};
    a2 <= int'($time) + 5 inside {15, 95};
    a3 <= int'($time) + 5 inside {25, 55};
  end

  p1: assert property (@(posedge clk) a1 |-> reject_on(r) ##3 b)
    else $display("inl p1 fail t=%0t", $time);
  p2: assert property (@(posedge clk) a2 |-> sync_reject_on(r) ##3 b)
    else $display("inl p2 fail t=%0t", $time);
  p3: cover property (@(posedge clk) accept_on(c) (a3 ##2 b))
    $display("inl p3 pass t=%0t", $time);

  initial begin
    #22 r = 1;
    #1 r = 0;     // 23
    #20 c = 1;    // 43
    #1 c = 0;     // 44
    #26 r = 1;    // 70
    #0 r = 0;
    #30 r = 1;    // 100
    #4 r = 0;     // 104
    #26 $finish;  // 130
  end
endmodule
