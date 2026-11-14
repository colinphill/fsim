// SPDX-License-Identifier: Apache-2.0
// array of struct nettype nets
module top;
  typedef struct { logic [3:0] a; logic b; } S;
  function automatic S orall(input S d[]);
    orall = '{4'h0, 1'b0}; $display("size=%0d", d.size()); foreach (d[i]) $display(" d[%0d].a=%h b=%b", i, d[i].a, d[i].b);
    foreach (d[i]) begin orall.a |= d[i].a; orall.b |= d[i].b; end
  endfunction
  nettype S wS with orall;
  wS w [2];
  S v1 = '{4'h1, 1'b0}, v2 = '{4'h4, 1'b1};
  assign w[0] = v1;
  assign w[0] = v2;
  assign w[1] = v2;
  initial #1 $display("w0.a=%h b=%b w1.a=%h", w[0].a, w[0].b, w[1].a);
endmodule
