// SPDX-License-Identifier: Apache-2.0
// PLI 1.0 module input port delays (IEEE 1364-2005 22.5.2, 23.1, 23.14,
// 23.90, Table 130): the SDF PORT delay of an input port, the intermodule
// paths that end at it, accMapToMipd, and the delay of a port bit.
`timescale 1ns/1ps
module drv(input a, output z);
  assign z = a;
endmodule

module vec(input [1:0] v, output z);
  assign z = v[0];
endmodule

module top;
  reg a = 1'b1;
  reg [1:0] vb = 2'b00;
  wire w, y, vz;
  drv u0(.a(a), .z(w));
  drv u2(.a(a), .z(w));
  drv u1(.a(w), .z(y));
  vec u3(.v(vb), .z(vz));
  initial begin
    #1 $acc_mipd;
    #10 $finish;
  end
endmodule
