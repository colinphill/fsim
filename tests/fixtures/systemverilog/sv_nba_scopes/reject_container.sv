// SPDX-License-Identifier: Apache-2.0
//
// A nonblocking assignment to an element of an automatic container variable is
// the automatic-variable error, not a container error (IEEE 1800-2023 6.21,
// 10.4.2). The function and the task are never called.
module top;
  function automatic int f(int v);
    string loc[2];
    loc[0] <= "x";
    return v;
  endfunction
  task automatic t(int v);
    string loc[$][$];
    loc[0][0] <= "y";
  endtask
endmodule
