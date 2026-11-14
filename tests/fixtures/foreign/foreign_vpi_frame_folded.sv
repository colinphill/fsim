// SPDX-License-Identifier: Apache-2.0
// A user function call in procedural code is no constant expression context
// (IEEE 1800-2023 13.4.3): `twice(4)`, whose argument is a constant, runs
// like `twice(n)`, so each call's frame starts and ends (37.43, 38.36.1).
module top;
  int n, m;
  function automatic int twice(int x);
    return x * 2;
  endfunction
  initial begin : main
    n = twice(4);
    m = twice(n);
    #1 $display("#c done n=%0d m=%0d", n, m);
  end
endmodule
