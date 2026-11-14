// SPDX-License-Identifier: Apache-2.0
// array of real nettype nets
module top;
  function automatic real ravg(input real d[]);
    ravg = 0.0;
    foreach (d[i]) ravg += d[i];
    if (d.size() > 0) ravg = ravg / d.size();
  endfunction
  nettype real wreal_avg with ravg;
  wreal_avg w [2];
  real a = 1.0, b = 2.0;
  assign w[0] = a;
  assign w[0] = b;
  assign w[1] = a;
  initial begin #1 $display("w=%f %f", w[0], w[1]); b = 5.0; #1 $display("w=%f %f", w[0], w[1]); end
endmodule
