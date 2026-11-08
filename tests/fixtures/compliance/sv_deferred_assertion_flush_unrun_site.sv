// fsim: top=sv_deferred_assertion_flush_unrun_site
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4.2: resuming from an event control clears the whole
// deferred assertion report queue of the process, including the report of
// an assertion statement that the new activation does not run.
// At time 1 the driver sets a = 1, then (after #0, so the always block runs
// first) a = 0.
//   run 1: a == 1 runs site A; its report (site_a) is queued;
//   run 2: the process resumed from @(a): the queue is flushed, so the
//          site A report is gone although site A is not run again; a == 0
//          runs site B; its report (site_b) is queued.
// Expected: a_runs == 0, b_runs == 1.
module sv_deferred_assertion_flush_unrun_site;
  timeunit 1ns; timeprecision 1ns;
  logic a = 1'b0;
  int a_runs, b_runs, failures;

  task automatic site_a; a_runs++; endtask
  task automatic site_b; b_runs++; endtask

  always @(a) begin
    if (a) begin
      assert #0 (1'b1) site_a();
    end else begin
      assert #0 (1'b1) site_b();
    end
  end

  initial begin
    #1;
    a = 1'b1;
    #0;
    a = 1'b0;
    #1;
    if (a_runs != 0) failures++;
    if (b_runs != 1) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL a_runs %0d b_runs %0d", a_runs, b_runs);
  end
endmodule
