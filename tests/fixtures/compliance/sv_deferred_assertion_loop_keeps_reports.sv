// fsim: top=sv_deferred_assertion_loop_keeps_reports
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4.1, 16.4.2: every evaluation of a deferred assertion
// queues its own pending report, and only the listed flush points (resume
// from an event control or wait, an always_comb wake, disable of the
// outermost scope) clear the queue.  A loop that runs the same assertion
// statement three times in one activation, with no suspension, therefore
// keeps all three reports, each with the argument value of its own
// evaluation:
//   i = 1: passes (i != 2) -> note_pass(1)
//   i = 2: fails           -> note_fail(2)
//   i = 3: passes          -> note_pass(3)
// Expected: passes == 2, pass_sum == 4, fails == 1, last_fail == 2.
module sv_deferred_assertion_loop_keeps_reports;
  timeunit 1ns; timeprecision 1ns;
  int passes, pass_sum, fails, last_fail, failures;

  task automatic note_pass(input int v);
    passes++;
    pass_sum += v;
  endtask
  task automatic note_fail(input int v);
    fails++;
    last_fail = v;
  endtask

  initial begin
    for (int i = 1; i <= 3; i++)
      assert #0 (i != 2) note_pass(i); else note_fail(i);
    #1;
    if (passes != 2) failures++;
    if (pass_sum != 4) failures++;
    if (fails != 1) failures++;
    if (last_fail != 2) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL passes %0d sum %0d fails %0d last %0d",
        passes, pass_sum, fails, last_fail);
  end
endmodule
