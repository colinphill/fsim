// SPDX-License-Identifier: Apache-2.0
// real nettype with averaging resolver (wreal style)
module top;
  function automatic real ravg(input real d[]);
    ravg = 0.0;
    foreach (d[i]) ravg += d[i];
    if (d.size() > 0) ravg = ravg / d.size();
  endfunction
  nettype real wreal_avg with ravg;
  nettype real wreal_u;
  wreal_avg w;
  wreal_u u;
  real a = 1.0, b = 2.0;
  assign w = a;
  assign w = b;
  assign u = a * 3.0;
  initial begin
    #1 $display("w=%f u=%f", w, u);
    b = 5.0; #1 $display("w=%f u=%f", w, u);
  end
endmodule
