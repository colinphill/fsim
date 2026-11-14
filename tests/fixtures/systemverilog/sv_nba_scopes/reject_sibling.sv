// SPDX-License-Identifier: Apache-2.0
//
// A name that is static in one block and automatic in a sibling block: only
// the automatic one is rejected as a nonblocking target (IEEE 1800-2023
// 6.21, 10.4.2). The function is never called.
module top;
  function automatic int f(int v);
    begin
      static int t;
      t <= v;
    end
    begin
      int t;
      t <= v;
    end
    return 0;
  endfunction
endmodule
