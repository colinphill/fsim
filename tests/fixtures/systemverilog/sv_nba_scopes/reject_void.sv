// SPDX-License-Identifier: Apache-2.0
//
// A void function obeys the rule of every function: a nonblocking assignment
// cannot write an automatic local (IEEE 1800-2023 6.21, 10.4.2, 13.4.4). The
// function is never called.
module top;
  function automatic void f(int v);
    int loc;
    loc <= v;
  endfunction
endmodule
