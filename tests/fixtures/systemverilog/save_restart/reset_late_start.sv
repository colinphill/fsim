// SPDX-License-Identifier: Apache-2.0
// $reset of a design whose initial procedure reads a variable an always
// procedure writes (IEEE 1800-2023 D.8): every procedure starts again at the
// new time zero, the initial one too, whatever engine runs it (on the compiled
// kernel the initial procedure starts in the kernel's late activation of time
// zero, which the reset's time-zero state has not had yet).
//
//  life 1: cnt counts the posedges at 5, 15 and 25; at t=32 cnt is 3 and the
//          count is 0, so $reset(1, 5) runs.
//  life 2: the same at 5, 15 and 25; at t=32 the count is 1, so the block
//          waits 40 more and prints cnt=7 at t=72.
`timescale 1ns/1ns
module top;
  reg clk = 0;
  reg [7:0] cnt = 0;
  always #5 clk = ~clk;
  always @(posedge clk) cnt <= cnt + 1;
  // Ends a run whose initial procedure did not start again.
  initial #500 $finish;
  initial begin
    #32;
    $display("#c pre t=%0d count=%0d cnt=%0d", $time, $reset_count, cnt);
    if ($reset_count == 0) $reset(1, 5);
    else begin
      #40 $display("#c done t=%0d count=%0d value=%0d cnt=%0d", $time,
        $reset_count, $reset_value, cnt);
      $finish;
    end
  end
endmodule
