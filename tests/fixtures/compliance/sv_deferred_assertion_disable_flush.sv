// fsim: top=sv_deferred_assertion_disable_flush
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4.2 and 16.4.4: a deferred assertion flush point is
// reached when the outermost scope of the process is disabled, so the
// pending report of the failed assertion in `worker` never runs; the
// assertion in `kept` is not disabled and its report runs in the Reactive
// region.  Expected: dropped == 0, kept_runs == 1.
module sv_deferred_assertion_disable_flush;
  timeunit 1ns; timeprecision 1ns;
  int dropped, kept_runs, failures;

  task automatic note_dropped; dropped++; endtask
  task automatic note_kept; kept_runs++; endtask

  initial begin : worker
    assert #0 (1'b0) else note_dropped();
    disable worker;
  end

  initial begin : kept
    assert #0 (1'b0) else note_kept();
  end

  initial begin
    #1;
    if (dropped != 0) failures++;
    if (kept_runs != 1) failures++;
    if (failures == 0) $display("PASS");
    else $display("FAIL dropped %0d kept %0d", dropped, kept_runs);
  end
endmodule
