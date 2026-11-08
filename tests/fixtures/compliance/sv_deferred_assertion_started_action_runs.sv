// fsim: top=sv_deferred_assertion_started_action_runs
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4.1: a pending report matures in the Observed region,
// and "once a report matures, it may no longer be flushed"; its subroutine
// call runs in the Reactive region. A flush point (16.4.2) reached later
// clears only the reports still pending, so an action task that started and
// waits runs to its end.
//   time 1: a = 1 wakes the always block; its report matures, act() starts
//           (started == 1) and waits #2;
//   time 2: a = 0 wakes the always block again (a flush point); the first
//           act() has started and keeps running; the new report matures and
//           a second act() starts (started == 2);
//   time 3, 4: the two calls finish (finished == 2).
module sv_deferred_assertion_started_action_runs;
  timeunit 1ns; timeprecision 1ns;
  logic a = 1'b0;
  int started, finished, failures;

  task automatic act;
    started++;
    #2;
    finished++;
  endtask

  always @(a) begin
    assert #0 (1'b1) act();
  end

  initial begin
    #1;
    a = 1'b1;
    #1;
    a = 1'b0;
    #3;
    if (started != 2) failures++;
    if (finished != 2) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL started %0d finished %0d", started, finished);
  end
endmodule
