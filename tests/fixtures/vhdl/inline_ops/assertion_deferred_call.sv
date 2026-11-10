// SPDX-License-Identifier: Apache-2.0
//
// The action of a deferred assertion is one subroutine call, which may be
// a void function (IEEE 1800-2023 16.4). Its by-value actuals -- function
// calls included -- are evaluated when the assertion is, and the call is
// queued and made in the Reactive region (assert #0) or the Postponed
// region (assert final) unless a flush point clears the queue first
// (16.4.1, 16.4.2).
//
//   t1  v = 1, x = 1             a1 and a2 pass: hits 1 (Reactive), 2
//                                (Postponed)
//   t2  v = 3, x = 0, #0 v = 4   both fail with the actuals of v = 3
//   t3  x = 1, #0 x = 0          the passes are flushed when the process
//                                resumes; both fail with v = 4
module top;
  int hits;
  int v;
  logic x = 1'b0;
  function void pass_call();
    hits++;
    $display("inl pass t=%0t hits=%0d", $time, hits);
  endfunction
  function void fail_call(int value, int twice);
    $display("inl fail t=%0t value=%0d twice=%0d", $time, value, twice);
  endfunction
  function int doubled(int value);
    return 2 * value;
  endfunction
  always @(x) begin
    a1: assert #0 (x) pass_call(); else fail_call(v, doubled(v));
    a2: assert final (x) pass_call(); else fail_call(v + 100, doubled(v + 100));
  end
  initial begin
    #1 v = 1;
    x = 1'b1;
    #1 v = 3;
    x = 1'b0;
    #0 v = 4;
    #1 x = 1'b1;
    #0 x = 1'b0;
  end
endmodule
