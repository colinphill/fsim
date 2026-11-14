// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment cannot write an automatic variable (IEEE 1800-2023 6.21, 10.4.2): a local of an automatic function.
module top;
  function automatic int f(int v);
    int loc = 0;
    loc <= v;
    return loc;
  endfunction
  initial begin
    int r;
    r = f(5);
    $finish;
  end
endmodule
