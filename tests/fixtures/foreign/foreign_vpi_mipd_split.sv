// SPDX-License-Identifier: Apache-2.0
// A module input port delay put by a VPI plug-in on a port that no SDF
// annotation split (IEEE 1800-2023 38.32, vpiMIPDelay; IEEE 1364-2005 16.2.4):
// the first put splits the port's net at the next safe point, so that u1 reads
// a delayed copy of w while u0 and the testbench see w at once. The design is
// elaborated without the plug-in; nothing is split until the first put.
//
// d changes at 10, 20, ..., 70 (1, 0, 1, 0, 1, 0, 1) and u1.a follows w:
//   t=10  w rises, no delay yet:                        u1.a=1 at 10
//   t=15  the plug-in puts rise 5 / fall 3 (u1.a is 1): the copy starts at 1,
//         u1 sees no change
//   t=20  w falls, 3 to fall:                           u1.a=0 at 23
//   t=30  w rises, 5 to rise:                           u1.a=1 at 35
//   t=40  w falls:                                      u1.a=0 at 43
//   t=50  w rises, pending until 55
//   t=52  the plug-in puts rise 1 / fall 1: the pending change keeps its time
//                                                       u1.a=1 at 55
//   t=60  w falls, now 1 to fall:                       u1.a=0 at 61
//   t=70  w rises, 1 to rise:                           u1.a=1 at 71
//
// A put on a port bit: c changes at 12 (2'b10), 20 (2'b11), 30 (2'b01) and 40
// (2'b11) and u3.a follows w2. The plug-in puts rise 4 / fall 2 on bit 1 of
// u3.a at 15, so only that bit is delayed:
//   t=12  bit 1 rises, no delay yet:                    u3.a=10 at 12
//   t=15  the plug-in puts the bit's delay
//   t=20  bit 0 rises, undelayed:                       u3.a=11 at 20
//   t=30  bit 1 falls, 2 to fall:                       u3.a=01 at 32
//   t=40  bit 1 rises, 4 to rise:                       u3.a=11 at 44
`timescale 1ns/1ps
module drv(input logic a, output logic z);
  assign z = a;
endmodule

module sink(input logic a);
  always @(a) if ($time >= 10) $display("#c sink a=%b t=%0d", a, $time);
endmodule

module sinkv(input logic [1:0] a);
  always @(a) if ($time >= 10) $display("#c bus a=%b t=%0d", a, $time);
endmodule

module top;
  logic d = 1'b0;
  wire w;
  drv u0(.a(d), .z(w));
  sink u1(.a(w));
  logic [1:0] c = 2'b00;
  wire [1:0] w2;
  assign w2 = c;
  sinkv u3(.a(w2));
  initial begin
    #12 c = 2'b10;
    #8 c = 2'b11;
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
