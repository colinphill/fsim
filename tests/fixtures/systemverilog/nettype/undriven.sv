// SPDX-License-Identifier: Apache-2.0
// no drivers: resolver called with empty array at time zero; unresolved default x
module top;
  function automatic logic [3:0] rc(input logic [3:0] d[]);
    rc = d.size();
  endfunction
  nettype logic [3:0] wc with rc;
  nettype logic [3:0] wu;
  wc c;
  wu u;
  initial #1 $display("c=%0d u=%b", c, u);
endmodule
