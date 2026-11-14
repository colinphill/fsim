// SPDX-License-Identifier: Apache-2.0
// The arguments of a system function call are read on demand (IEEE 1800-2023
// 37.42 detail 7): a name no scope holds is an automatic variable of the
// running frame, an operation over it is evaluated, and $bits gives the size of
// its value. A user function call has a value once it has been evaluated
// (38.15), so the call of g, not yet evaluated, has none.

module top;
  int sig = 6;
  int r1, r2, r3, r4, r5;
  function automatic int g(input int a); return a + 1; endfunction
  task automatic t(input int n);
    int loc;
    begin
      loc = 7;
      r1 = $clog2(sig);
      r2 = $clog2(loc);
      r3 = $clog2(n + loc);
      r4 = $clog2(g(sig));
      r5 = $countones(sig) + $bits(loc);
    end
  endtask
  initial begin
    t(9);
  end
endmodule
