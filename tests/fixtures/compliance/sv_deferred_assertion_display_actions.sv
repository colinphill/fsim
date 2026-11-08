// fsim: top=sv_deferred_assertion_display_actions
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4: the pass and fail statements of a deferred assertion
// are each a single subroutine call, a system task included.  An observed
// deferred assertion (#0) runs its action in the Reactive region; a final
// deferred assertion runs it in the Postponed region, where $display is
// legal (4.4.2.9).  Expected simulation output, derived from 16.4.1:
//   t=0 Reactive: "obs settled pass", "cover hit" (each statement's own
//        report; the values were sampled when the statement ran, 16.4);
//   t=0 Postponed: "final fail" (glitch is 1 when the final assertion
//        ran, so !glitch fails);
//   t=1 Reactive: "obs fail" (ok is 0), then "PASS".
// The self-check inspects only the counters the task actions maintain;
// the $display lines are the printed evidence of the scheduling above.
module sv_deferred_assertion_display_actions;
  timeunit 1ns; timeprecision 1ns;
  logic glitch;
  logic ok = 1'b1;
  int passes = 0;
  int fails = 0;
  int failures = 0;

  task automatic note_pass;
    passes++;
  endtask
  task automatic note_fail;
    fails++;
  endtask

  initial begin
    // glitch is 0 when the first assertion runs: it fails, note_fail runs
    // in the Reactive region.  The third passes: note_pass runs.
    glitch = 1'b0;
    assert #0 (glitch) note_pass(); else note_fail();
    glitch = 1'b1;
    assert #0 (glitch) $display("obs settled pass");
      else $display("obs settled fail");
    assert #0 (glitch) note_pass();
      else note_fail();
    // A $display action: a failing assertion runs the else statement.
    assert final (!glitch) $display("final pass");
      else $display("final fail");
    cover #0 (glitch) $display("cover hit");
    assume #0 (ok) else $display("assume fail");
    #1;
    // Both observed task actions have run; the $display ones printed.
    if (passes != 1 || fails != 1) failures++;
    ok = 1'b0;
    assert #0 (ok) $display("obs pass");
      else $display("obs fail");
    #1;
    if (failures == 0) $display("PASS");
    else $display("FAIL count %0d", failures);
  end
endmodule
