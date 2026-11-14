// SPDX-License-Identifier: Apache-2.0
// scalar struct nettype net with an OR resolver
module top;
  typedef struct { logic [3:0] a; logic b; } S;
  function automatic S orall(input S d[]);
    orall = '{4'h0, 1'b0};
    $display("size=%0d", d.size());
    foreach (d[i]) begin $display(" d[%0d].a=%h b=%b", i, d[i].a, d[i].b); orall.a |= d[i].a; orall.b |= d[i].b; end
    $display(" r.a=%h", orall.a);
  endfunction
  nettype S wS with orall;
  wS w;
  S v1 = '{4'h1, 1'b0}, v2 = '{4'h4, 1'b1};
  assign w = v1;
  assign w = v2;
  initial #1 $display("w.a=%h b=%b", w.a, w.b);
endmodule
