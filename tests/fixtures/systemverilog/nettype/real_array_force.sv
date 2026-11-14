// SPDX-License-Identifier: Apache-2.0
// force and release of elements of arrays of real nettype nets, resolved and
// unresolved, and of a scalar real nettype net: the forced value is real
// (IEEE 1800-2023 10.6.2, 6.6.7), and a release returns each net to its
// drivers' value
module top;
  function automatic real ravg(input real d[]);
    ravg = 0.0;
    foreach (d[i]) ravg += d[i];
    if (d.size() > 0) ravg = ravg / d.size();
  endfunction
  nettype real wavg with ravg;
  nettype real wu;
  wavg s;
  wavg r [2];
  wu u [2];
  real p = 1.0, q = 2.0;
  assign s = p;
  assign s = q;
  assign r[1] = p;
  assign r[1] = q;
  assign u[1] = p;
  always @(r[1]) $display("%0t r1=%f", $time, r[1]);
  initial begin
    #5 force s = 9.5; force r[1] = 9.5; force u[1] = 7;
    #1 $display("%0t s=%f u1=%f", $time, s, u[1]);
    #4 q = 4.0;
    #5 release s; release r[1]; release u[1];
    #1 $display("%0t s=%f u1=%f", $time, s, u[1]);
  end
endmodule
