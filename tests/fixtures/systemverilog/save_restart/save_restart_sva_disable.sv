// SPDX-License-Identifier: Apache-2.0
// A save of a run whose concurrent assertion has a `disable iff`. clk rises
// at 5, 15, 25, ...; a is high at the edge of 15 only and b stays low, so the
// attempt of `a |-> ##3 b` that starts at 15 fails at 45 unless it is
// disabled. rst pulses from 38 to 42, between the edges: the disable
// condition reads current values and disables the attempts in progress when
// it becomes true (IEEE 1800-2023 16.12), so p1's attempt is disabled at 38
// and only p2, which has no disable iff, fails.
//
// The run is saved at 30, with both attempts in flight. Which monitor the
// disable net disables is saved with the run, so the restart disables p1's
// attempt at 38 too and ends with the same output.
`timescale 1ns/1ns
module top;
  reg clk;
  reg a;
  reg b;
  reg rst;
  initial begin
    clk = 0;
    a = 0;
    b = 0;
    rst = 0;
    repeat (14) #5 clk = ~clk;
  end
  initial begin
    #12 a = 1;
    #6 a = 0;
    #20 rst = 1;
    #4 rst = 0;
  end
  p1: assert property (@(posedge clk) disable iff (rst) a |-> ##3 b)
    else $display("p1 fail t=%0d", $time);
  p2: assert property (@(posedge clk) a |-> ##3 b)
    else $display("p2 fail t=%0d", $time);
  initial #60 $display("done t=%0d", $time);
endmodule
