// SPDX-License-Identifier: Apache-2.0
// $reset after a value-change watch registered once the run had started (IEEE
// 1800-2023 D.8, 38.36.3). The plug-in (foreign_vpi_reset_watch_plugin.c) watches
// sig at t=5.
//
//  life 1: sig is 0 at t=0, 3 at t=10 and 4 at t=20 (the watch reports 3 and
//          4); at t=21 the count is 0, so $reset(1, 5) runs: cbStartOfReset at
//          t=21 (the watch is removed there), then the time is 0 again and
//          cbEndOfReset reports t=0.
//  life 2: the same assignments, at t=0, 10 and 20; at t=21 the count is 1 and
//          nothing resets; at t=30 the design prints sig=4, count=1, value=5.
`timescale 1ns/1ns
module top;
  reg [3:0] sig;
  initial begin
    sig = 0;
    #10 sig = 3;
    #10 sig = 4;
    #1;
    if ($reset_count == 0) $reset(1, 5);
    #9 $display("#c done t=%0d sig=%0d count=%0d value=%0d", $time, sig,
      $reset_count, $reset_value);
  end
endmodule
