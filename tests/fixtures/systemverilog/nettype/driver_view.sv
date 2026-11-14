// SPDX-License-Identifier: Apache-2.0
// the drivers a summing resolution function sees, and their count
module top;
  function automatic logic [7:0] rsum(input logic [7:0] d[]);
    rsum = 0;
    $display("rsum size=%0d", d.size());
    foreach (d[i]) begin $display("  d[%0d]=%b", i, d[i]); rsum += d[i]; end
  endfunction
  nettype logic [7:0] wsum with rsum;
  wsum w;
  logic [7:0] a = 1, b = 2;
  assign w = a;
  assign w = b;
  initial #1 $display("w=%0d", w);
endmodule
