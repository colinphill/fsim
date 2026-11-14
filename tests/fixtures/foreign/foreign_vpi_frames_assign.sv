// SPDX-License-Identifier: Apache-2.0
// The frames of function calls in continuous assignments and net declaration
// assignments (IEEE 1800-2023 37.43 detail 1): a frame without a thread whose
// vpiOrigin is the call, cbStartOfFrame placed on the function a
// hierarchical call reaches, and cbStmt on a statement of that function
// (38.36.1). A delay value's call runs when the statement does (9.4.1).
module sub;
  function automatic int h(int v);
    int r;
    r = v + 1;
    return r;
  endfunction
endmodule
module top;
  sub u();
  logic [7:0] x = 0;
  wire [7:0] y;
  assign y = u.h(x);
  function automatic int lf(int v);
    return v * 2;
  endfunction
  wire [7:0] w = lf(x);
  int z;
  function automatic int d(int n);
    return n;
  endfunction
  initial begin
    #1 x = 3;
    #(d(1)) z = u.h(5);
    #1 $finish;
  end
endmodule
