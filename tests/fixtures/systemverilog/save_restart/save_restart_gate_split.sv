// SPDX-License-Identifier: Apache-2.0
// A gate split by vpi_put_delays across $save and a fresh-process restart
// (IEEE 1800-2023 38.32, 28.16, D.9). buf g has no delay; the plug-in
// (foreign_vpi_save_gate_plugin.c) puts rise 3 and fall 5 at t=10, which
// gives it a delay slot at the next safe point. The design saves at t=30, at
// the end of that slot, after the gate was split.
//
// y follows a through the delay: a rises at 20 so y rises at 23 (0 at 22, 1 at
// 23); a falls at 40 so y falls at 45 (still 1 at 44, 0 at 45). A restart
// that ran the gate's original program would show y=0 at 44.
//
// saving run: the put at 10 and its readback (minimum, typical and maximum of
//   rise 2,3,4 and fall 4,5,6, which the simulation does not use), then the
//   four strobes.
// restart run: the plug-in's cbEndOfRestart reads the delays back from the
//   restored tables (nothing of the put runs again), then the strobes after
//   the save: 44 and 45.
`timescale 1ns/1ns
module top;
  reg a;
  wire y;
  buf g (y, a);
  initial begin
    a = 0;
    #20 a = 1;
    #2 $strobe("#c t=%0d y=%b", $time, y);
    #1 $strobe("#c t=%0d y=%b", $time, y);
    #17 a = 0;
    #4 $strobe("#c t=%0d y=%b", $time, y);
    #1 $strobe("#c t=%0d y=%b", $time, y);
    #5 $finish;
  end
  initial #30 $save("gate.state");
endmodule
