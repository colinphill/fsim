// SPDX-License-Identifier: Apache-2.0
//
// A local declared `automatic` in a static function is automatic (IEEE
// 1800-2023 6.21): a nonblocking assignment cannot write it (10.4.2). The
// function is never called.
module top;
  function int f(int v);
    begin
      automatic int t;
      t <= v;
    end
    return 0;
  endfunction
endmodule
