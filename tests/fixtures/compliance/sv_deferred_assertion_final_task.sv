// fsim: top=sv_deferred_assertion_final_task
// SPDX-License-Identifier: Apache-2.0
// IEEE 1800-2023 16.4: a final deferred assertion's action may be any
// subroutine "that may be legally called in the Postponed region" (4.4.2.9),
// so a user task that only reads and prints is accepted.  By-value actuals
// (x, s and the function call) are evaluated when the assertion is: a = 3,
// str = "three", f = next_value() = 10 (called once).  The ref actual
// `shared` is read in the Postponed region, after the later assignment:
// live = 99.  The task cannot write a variable (that is illegal in the
// Postponed region), so it reports its own verdict: it prints PASS when it
// sees exactly these values.  The final assertion in `second` fails with
// else-action `verdict_fail`, queued at time 1 with y = 7, and prints PASS
// only when it sees 7.
module sv_deferred_assertion_final_task;
  timeunit 1ns; timeprecision 1ns;
  int x, y, fcalls;
  int shared = 1;
  string s;

  function automatic int next_value();
    fcalls++;
    return fcalls * 10;
  endfunction

  task automatic verdict(input int a, input string str, input int f,
      ref int live);
    if (a == 3 && str == "three" && f == 10 && live == 99)
      $display("PASS");
    else
      $display("FAIL a=%0d s=%s f=%0d live=%0d", a, str, f, live);
  endtask

  task automatic verdict_fail(input int v);
    if (v == 7) $display("PASS");
    else $display("FAIL second v=%0d", v);
  endtask

  initial begin
    x = 3; s = "three"; shared = 2;
    assert final (x == 3) verdict(x, s, next_value(), shared);
      else $display("FAIL first assertion failed");
    x = 50; s = "fifty"; shared = 99;
    #1;
    y = 7;
    assert final (y == 0) $display("FAIL second assertion passed");
      else verdict_fail(y);
    y = 8;
  end
endmodule
