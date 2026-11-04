// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 TF routines (IEEE 1364-2001 clause 25). The plug-in's $pli_tf_core
// task reads and writes the arguments; the design prints the values it wrote
// to real variables.
`timescale 1ns / 1ns
module top;
  reg [7:0] xval;
  reg [39:0] wide;
  real r;
  real r2;
  real r3;
  reg [31:0] s32;
  wire w = 1'b1;
  initial begin
    xval = 8'b10xz1010;
    wide = 40'h12_3456_78xa;
    r = 9.6;
    r2 = 0.0;
    r3 = 0.0;
    s32 = 32'h0000_4142;
    #5;
    $pli_tf_core(xval, wide, r, r2, r3, s32, "text", w);
    $display("#c design r2=%0.1f r3=%0.1f", r2, r3);
    $finish;
  end
endmodule
