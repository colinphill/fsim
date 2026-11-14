// SPDX-License-Identifier: Apache-2.0
// nettype net through ports: drivers inside two child instances
function automatic logic [7:0] rsum(input logic [7:0] d[]);
  rsum = 0;
  foreach (d[i]) rsum += d[i];
endfunction
nettype logic [7:0] wsum with rsum;
module drv(output wsum o, input logic [7:0] v);
  assign o = v;
endmodule
module top;
  wsum w;
  logic [7:0] a = 1, b = 2;
  drv d1(w, a);
  drv d2(w, b);
  always @(w) $display("%0t change w=%0d", $time, w);
  initial begin #1 $display("w=%0d", w); a = 10; #1 $display("w=%0d", w); end
endmodule
