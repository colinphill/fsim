// SPDX-License-Identifier: Apache-2.0
// array of a two-state nettype, read whole and per element
module top;
  function automatic int isum(input int d[]);
    isum = 0;
    foreach (d[i]) isum += d[i];
  endfunction
  nettype int nsum with isum;
  nsum w [3];
  int a = 1, b = 2;
  assign w[0] = a;
  assign w[0] = b;
  assign w[2] = 7;
  always @(w[0]) $display("%0t w0 change %0d", $time, w[0]);
  initial begin #1 $display("w=%0d %0d %0d", w[0], w[1], w[2]); a = 10; #1 $display("w0=%0d", w[0]); end
endmodule
