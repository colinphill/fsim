// SPDX-License-Identifier: Apache-2.0
// The frames of a nettype's resolution function (IEEE 1800-2023 37.43
// detail 6, 6.6.7): each call's frame has the net it resolves as its
// vpiOrigin, the element net for an array of nettype nets, and no thread.
module top;
  function automatic logic [7:0] rsum(input logic [7:0] d[]);
    rsum = 0;
    foreach (d[i]) rsum += d[i];
  endfunction
  nettype logic [7:0] wsum with rsum;
  wsum n;
  wsum a [2];
  logic [7:0] x = 1, y = 2;
  assign n = x;
  assign n = y;
  assign a[1] = x;
  initial begin
    #1 x = 5;
    #1 $display("n=%0d a1=%0d", n, a[1]);
    $finish;
  end
endmodule
