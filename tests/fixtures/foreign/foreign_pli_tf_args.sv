// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 argument description (IEEE 1364-2001 25.15, 25.26, 25.27, 25.35,
// 25.36, 25.50, 25.53, 25.61, 21.3.2): $pli_tf_args describes seventeen
// arguments, one of them omitted; $pli_tf_scales reads the time scale of its
// module; $pli_tf_drc watches the driver of a net.
`timescale 100ns / 10ns
module slow;
  initial #1 $pli_tf_scales;
endmodule

`timescale 1ns / 1ps
module top;
  reg [99:0] wide;
  reg signed [39:0] neg;
  reg [9:0] mixed;
  reg [3:0] ax;
  reg [3:0] az;
  reg [3:0] pz;
  real r;
  integer i;
  time t;
  reg [7:0] mem [0:3];
  wire sc;
  wire [3:0] nv;
  assign (pull1, strong0) sc = 1'b1;
  assign nv = 4'b0110;
  initial begin
    wide = 100'd1 << 80;
    neg = 40'sh80_0000_0000;
    mixed = 10'b1x0z1x0z10;
    ax = 4'bxxxx;
    az = 4'bzzzz;
    pz = 4'b1z01;
    r = 2.5;
    i = -3;
    t = 64'h1_0000_0000;
    mem[0] = 8'hA5;
    mem[1] = 8'h5A;
    mem[2] = 8'h0F;
    mem[3] = 8'hFF;
    #50 $pli_tf_scales;
    $pli_tf_args(wide, neg, mixed, ax, az, pz, "hello", r, i, t, mem, sc, nv,
                 mem[2], , wide[7:4], wide[3]);
    #100 $finish;
  end
endmodule

module drc;
  reg d;
  wire n;
  buf b1(n, d);
  initial begin
    d = 0;
    #1 $pli_tf_drc(n);
    #5 d = 1;
    #5 d = 0;
  end
endmodule
