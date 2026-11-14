// SPDX-License-Identifier: Apache-2.0
// The VPI callbacks of the assertion control tasks (IEEE 1800-2023 20.11
// Table 20-8): a call without arguments is an assertion system callback
// (39.4.1), a call with arguments an assertion callback of each assertion it
// changes (39.4.2), and a kill with arguments a reset and then a disable. The
// plug-in prints each callback with its time; the design calls one task per
// time unit from t=1.
module top;
  logic clk = 0, a = 1;
  always #50 clk = ~clk;
  a1: assert property (@(posedge clk) a);
  c1: cover property (@(posedge clk) a);

  initial begin
    #1 $assertoff;                      // 1: sys-off
    #1 $asserton;                       // 2: sys-on
    #1 $assertkill;                     // 3: sys-kill
    #1 $asserton(0, a1);                // 4: enable a1
    #1 $assertoff(0, top);              // 5: disable a1, c1
    #1 $assertkill(0, a1);              // 6: reset a1, disable a1
    #1 $assertpassoff;                  // 7: sys-disable-pass-action
    #1 $assertpasson(0, a1);            // 8: enable-pass-action a1
    #1 $assertfailoff(0, c1);           // 9: disable-fail-action c1
    #1 $assertfailon;                   // 10: sys-enable-fail-action
    #1 $assertnonvacuouson(0, a1);      // 11: enable-nonvacuous-action a1
    #1 $assertvacuousoff;               // 12: sys-disable-vacuous-action
    #1 $assertcontrol(1);               // 13: sys-lock
    #1 $assertcontrol(2);               // 14: sys-unlock
    #1 $assertcontrol(1, , , 0, a1);    // 15: lock a1
    #1 $assertoff(0, a1);               // 16: a1 is locked: no callback
    #1 $assertcontrol(2, , , 0, a1);    // 17: unlock a1
    #1 $assertcontrol(4, 1);            // 18: an argument: disable a1, c1
    #1 $assertcontrol(3);               // 19: sys-on
    #1 $assertcontrol(7, 1, 2);         // 20: disable-pass-action c1 only
    #1 $finish;
  end
endmodule
