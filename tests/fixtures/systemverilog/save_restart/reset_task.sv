// SPDX-License-Identifier: Apache-2.0
// $reset(1, 7) (IEEE 1800-2023 D.8): disables all activity, takes time back to
// 0 and the variables back to their initial values, and starts every initial
// and always procedure again; $reset_count and $reset_value are the only state
// that survives. stop_value 1 is nonzero, so the simulation goes on at once
// (a zero or absent stop_value enters interactive mode, a batch $stop).
//
//  life 1: t=0, a=3, count 0 value 0 (nothing reset yet); at t=10 the block
//          sets a=9 and, as the count is 0, calls $reset(1, 7) as its last
//          statement.
//  life 2: time is 0 again and the initial block runs from the top: count 1,
//          value 7, a=3. At t=10 the count is 1, so it waits 10 more and
//          prints t=20, a=9; nothing else is scheduled, so the run ends.
`timescale 1ns/1ns
module top;
  reg [7:0] a;
  initial begin
    a = 3;
    $display("run count=%0d value=%0d t=%0d a=%0d", $reset_count,
      $reset_value, $time, a);
    #10;
    a = 9;
    if ($reset_count == 0) $reset(1, 7);
    else begin
      #10 $display("done count=%0d value=%0d t=%0d a=%0d", $reset_count,
        $reset_value, $time, a);
    end
  end
endmodule
