// SPDX-License-Identifier: Apache-2.0
//
// A function has no time-controlled statement (IEEE 1800-2023 13.4.4): an intra-assignment delay is one.
module top;
  int total = 0;
  function int f(int v);
    total <= #1 v;
    return v;
  endfunction
  initial begin
    int r;
    r = f(5);
    $finish;
  end
endmodule
