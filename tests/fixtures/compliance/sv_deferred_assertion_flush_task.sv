// fsim: top=sv_deferred_assertion_flush_task
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4.2: a process that resumes from an event control
// reaches a deferred assertion flush point, which clears its deferred
// assertion report queue: pending reports "will not be executed".
// At time 1 the driver sets a = 1, then (after #0, so the always block runs
// first) a = 0.  The always block runs twice in the time step:
//   run 1: a == 1, the assertion passes; report_pass(1) is queued;
//   run 2: the process resumed from @(a), so the queue is flushed (the
//          pass report never runs); a == 0, the assertion fails and
//          report_fail(0) is queued.
// The Observed region matures only the second report: passes == 0 and
// fails == 1 with last_fail == 0.
module sv_deferred_assertion_flush_task;
  timeunit 1ns; timeprecision 1ns;
  logic a = 1'b0;
  int passes, fails, last_pass, last_fail, failures;

  task automatic report_pass(input int v);
    passes++;
    last_pass = v;
  endtask
  task automatic report_fail(input int v);
    fails++;
    last_fail = v;
  endtask

  always @(a) begin
    assert #0 (a == 1'b1) report_pass(int'(a)); else report_fail(int'(a));
  end

  initial begin
    last_fail = -1;
    #1;
    a = 1'b1;
    #0;
    a = 1'b0;
    #1;
    if (passes != 0) failures++;
    if (fails != 1) failures++;
    if (last_fail != 0) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL passes %0d fails %0d last_fail %0d", passes, fails, last_fail);
  end
endmodule
