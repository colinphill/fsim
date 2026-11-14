// SPDX-License-Identifier: Apache-2.0
// a struct nettype of four-state members with one driver
module top;
  typedef struct { logic [3:0] a; logic b; } S;
  function automatic S first(input S d[]);
    first = d[0];
  endfunction
  nettype S wS with first;
  wS s;
  assign s = '{4'h5, 1'b1};
  initial #1 $display("s.a=%h", s.a);
endmodule
