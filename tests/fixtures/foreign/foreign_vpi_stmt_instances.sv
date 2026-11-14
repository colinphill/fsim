// SPDX-License-Identifier: Apache-2.0
// cbStmt and frames of the tasks and functions of other instances (IEEE
// 1800-2023 38.36.1.1, 37.43): a call of a.bump runs instance a's task, so
// its statement belongs to a; a call of b.bump runs b's. From the LRM: a's
// statement callback fires for the calls with 1 and 3 (t=1, t=3), b's for
// the call with 2 (t=2), a's function statement for the call at t=4; each
// frame's origin call names the instance's own task or function.
module cnt;
  integer n = 0;
  task bump(input integer k);
    n = n + k;
  endtask
  function integer twice(input integer k);
    twice = 2 * k;
  endfunction
endmodule

module top;
  cnt a ();
  cnt b ();
  integer r;
  initial begin
    #1 a.bump(1);
    #1 b.bump(2);
    #1 a.bump(3);
    #1 r = a.twice(4);
    #1 $display("#c done a.n=%0d b.n=%0d r=%0d", a.n, b.n, r);
  end
endmodule
