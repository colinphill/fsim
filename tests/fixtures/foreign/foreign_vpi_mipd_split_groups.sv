// SPDX-License-Identifier: Apache-2.0
// The lazy split of foreign_vpi_mipd_split.sv on a load whose processes are
// edge-triggered (IEEE 1800-2023 38.32, vpiMIPDelay): u1 has two
// always @(posedge a) blocks, which the compiled kernel may dispatch as one
// group, and an always @(negedge a); they read the same net, so the first put
// moves all three to the delayed copy together. The plug-in is the one of
// foreign_vpi_mipd_split.sv (puts at 15 and 52 on top.u1 port a, and at 15 on
// bit 1 of top.u3 port a). d changes at 10, 20, ..., 70 (1, 0, 1, 0, 1, 0, 1):
//   t=10  w rises, no delay yet:                   u1 up at 10
//   t=15  the plug-in puts rise 5 / fall 3; the copy starts at 1
//   t=20  w falls, 3 to fall:                      u1 down at 23 (cnt=1)
//   t=30  w rises, 5 to rise:                      u1 up at 35
//   t=40  w falls:                                 u1 down at 43 (cnt=2)
//   t=50  w rises, pending until 55
//   t=52  the plug-in puts rise 1 / fall 1: the pending change keeps its time
//                                                  u1 up at 55
//   t=60  w falls, now 1 to fall:                  u1 down at 61 (cnt=3)
//   t=70  w rises, 1 to rise:                      u1 up at 71
// c changes at 12 (2'b10), 22 (2'b11), 32 (2'b01) and 42 (2'b11); only bit 1
// of u3.a is delayed (rise 4 / fall 2, from 15), so the bus shows 10 at 12,
// 11 at 22, 01 at 34 and 11 at 46. cnt counts the rises u1 has seen; no two
// lines of the trace share a time.
`timescale 1ns/1ps
module drv(input logic a, output logic z);
  assign z = a;
endmodule

module sinkp(input logic a);
  int cnt = 0;
  always @(posedge a) if ($time >= 10) cnt <= cnt + 1;
  always @(posedge a) if ($time >= 10) $display("#c up t=%0d", $time);
  always @(negedge a) if ($time >= 10) $display("#c down t=%0d cnt=%0d", $time, cnt);
endmodule

module sinkv(input logic [1:0] a);
  always @(a) if ($time >= 10) $display("#c bus a=%b t=%0d", a, $time);
endmodule

module top;
  logic d = 1'b0;
  wire w;
  drv u0(.a(d), .z(w));
  sinkp u1(.a(w));
  logic [1:0] c = 2'b00;
  wire [1:0] w2;
  assign w2 = c;
  sinkv u3(.a(w2));
  initial begin
    #12 c = 2'b10;
    #10 c = 2'b11;
    #10 c = 2'b01;
    #10 c = 2'b11;
  end
  initial begin
    #10 d = 1'b1;
    #10 d = 1'b0;
    #10 d = 1'b1;
    #10 d = 1'b0;
    #10 d = 1'b1;
    #10 d = 1'b0;
    #10 d = 1'b1;
  end
endmodule
