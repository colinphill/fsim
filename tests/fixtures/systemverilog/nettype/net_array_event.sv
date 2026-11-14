// SPDX-License-Identifier: Apache-2.0
// an array of nettype nets: element value changes and a continuous read of one element
module top;
  function automatic logic [7:0] rsum(input logic [7:0] d[]);
    rsum = 0;
    foreach (d[i]) rsum += d[i];
  endfunction
  nettype logic [7:0] wsum with rsum;
  wsum w [2];
  logic [7:0] a = 1, b = 2, c = 5;
  wire [7:0] r0 = w[0];
  assign w[0] = a;
  assign w[0] = b;
  assign w[1] = c;
  always @(w[0]) $display("%0t w0 change %0d", $time, w[0]);
  initial begin #1 $display("w0=%0d w1=%0d r0=%0d", w[0], w[1], r0); end
endmodule
