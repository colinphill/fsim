// SPDX-License-Identifier: Apache-2.0
// A save of a run that holds concurrent assertion attempts. a is high at the
// rising edges of clk at t=15 and t=25 (clk rises at 5, 15, 25, 35, 45; a rises
// at 12 and falls at 32), so two attempts of `a |-> ##3 1'b1` are in flight at
// the --save-at time of 30 (they end at 45 and 55 of the clock).
//
// The attempts' storage is saved with the run (task #186), so a restart from
// the file finishes both attempts and ends with the same output. The
// uninterrupted run prints one line, and so does the restart.
`timescale 1ns/1ns
module top;
  reg clk;
  reg a;
  initial begin
    clk = 0;
    a = 0;
    repeat (10) #5 clk = ~clk;
  end
  initial begin
    #12 a = 1;
    #20 a = 0;
  end
  assert property (@(posedge clk) a |-> ##3 1'b1);
  initial #50 $display("done t=%0d", $time);
endmodule
