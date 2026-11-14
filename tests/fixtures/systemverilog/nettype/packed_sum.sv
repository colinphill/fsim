// SPDX-License-Identifier: Apache-2.0
// packed 4-state, summing resolver, three drivers, time-zero activation
module top;
  function automatic logic [7:0] rsum(input logic [7:0] d[]);
    rsum = 0;
    foreach (d[i]) rsum += d[i];
  endfunction
  nettype logic [7:0] wsum with rsum;
  wsum w, e;
  logic [7:0] a = 1, b = 2;
  assign w = a;
  assign w = b;
  assign w = 8'd4;
  initial begin
    #1 $display("w=%0d e=%0d", w, e);
    a = 10; #1 $display("w=%0d", w);
  end
endmodule
