// SPDX-License-Identifier: Apache-2.0
//
// A formal of an automatic function is automatic (IEEE 1800-2023 6.21, 13.3, 10.4.2).
module top;
  function automatic int f(int v);
    v <= 3;
    return v;
  endfunction
  initial begin
    int r;
    r = f(5);
    $finish;
  end
endmodule
