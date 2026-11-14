// SPDX-License-Identifier: Apache-2.0
// a packed-member struct nettype: the drivers its resolution function sees
module top;
  typedef struct { logic [3:0] a; logic b; } S;
  function automatic S first(input S d[]);
    $display("size=%0d", d.size());
    foreach (d[i]) $display("  d[%0d].a=%h b=%b", i, d[i].a, d[i].b);
    first = d[0];
    $display("  first.a=%h b=%b", first.a, first.b);
  endfunction
  nettype S wS with first;
  wS s;
  S v = '{4'h5, 1'b1};
  assign s = v;
  initial #1 $display("s.a=%h s.b=%b", s.a, s.b);
endmodule
