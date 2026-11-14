// SPDX-License-Identifier: Apache-2.0
//
// An automatic local hides the module variable of the same name (IEEE
// 1800-2023 6.21, 23.9), so the nonblocking assignment in its block writes the
// local. The function is never called.
module top;
  int g;
  function automatic int f(int v);
    int g;
    begin
      g <= v;
    end
    return 0;
  endfunction
endmodule
