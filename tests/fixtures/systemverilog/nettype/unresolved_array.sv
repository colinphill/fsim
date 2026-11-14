// SPDX-License-Identifier: Apache-2.0
// array of a nettype without a resolution function: one driver per element
module top;
  nettype logic [3:0] plain;
  plain w [2];
  assign w[0] = 4'd3;
  assign w[1] = 4'd5;
  initial #1 $display("w=%0d %0d", w[0], w[1]);
endmodule
