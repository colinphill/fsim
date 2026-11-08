// SPDX-License-Identifier: Apache-2.0
// $reset after a gate was split by vpi_put_delays (IEEE 1800-2023 D.8, 38.32,
// 28.16). buf g has no delay; the plug-in (foreign_vpi_save_gate_plugin.c)
// puts rise 3 and fall 5 at t=10 of the first life. At t=53 of the first life
// the count is 0, so $reset(1, 5) runs: the time is 0 again and the design
// starts over. The reset builds the time-zero state of a twin that takes the
// same split (the plan journal), so the restore fits and the gate keeps its
// delay slot: in both lives a rises at 20 and y rises at 23 (0 at 22, 1 at
// 23). A twin without the split would not fit the run, or the second life
// would see y=1 at 22.
//
// life 0 (count 0): the put at t=10 and its readback, y=0 at 22, y=1 at 23.
// life 1 (count 1): the same strobes; the plug-in's put does not run again.
`timescale 1ns/1ns
module top;
  reg a;
  wire y;
  buf g (y, a);
  initial begin
    a = 0;
    #20 a = 1;
    #2 $strobe("#c life=%0d t=%0d y=%b", $reset_count, $time, y);
    #1 $strobe("#c life=%0d t=%0d y=%b", $reset_count, $time, y);
    #30;
    if ($reset_count == 0) $reset(1, 5);
    #5 $finish;
  end
endmodule
