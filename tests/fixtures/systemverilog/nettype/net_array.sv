// SPDX-License-Identifier: Apache-2.0
// arrays of nettype nets
module top;
  function automatic logic [7:0] rsum(input logic [7:0] d[]);
    rsum = 0;
    foreach (d[i]) rsum += d[i];
  endfunction
  nettype logic [7:0] wsum with rsum;
  wsum w [2];
  logic [7:0] a = 1, b = 2, c = 5;
  assign w[0] = a;
  assign w[0] = b;
  assign w[1] = c;
  assign w[1] = a;
  initial begin #1 $display("w0=%0d w1=%0d", w[0], w[1]); a = 10; #1 $display("w0=%0d w1=%0d", w[0], w[1]); end
endmodule
