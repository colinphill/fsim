// SPDX-License-Identifier: Apache-2.0
// INTERCONNECT delays of a net whose driver also reads the net (IEEE
// 1364-2005 16.2.4): the driver keeps reading the real net, and the copy of
// its contribution that reaches the load is what the driver writes.
//
// u1 drives w when e1 is set: 0 if it reads 0 on the net (it follows another
// driver's 0), else a1. u2 is a plain driver (e2, d2). INTERCONNECT u1.z ->
// p.a is 2 ns and u2.z -> p.a is 5 ns. The probe prints each change of its
// port from 10 ns on.
//   20 ns  e2=1 (d2=0): u2 drives 0; w=0 at once, the load sees it at 25.
//                                                          TRACE a=0 t=25
//   40 ns  e1=1 (a1=1): u1 reads 0 on the net and drives 0, not a1. Its 0
//          arrives at 42 and the load keeps 0. Had u1's copy been a1 (1), the
//          load would see x (1 against u2's 0) at 42.
//   60 ns  e2=0: u2 is off, u1 still drives 0; nothing changes at the load.
//   80 ns  a1=0: u1 still reads 0 and drives 0.
//  100 ns  e1=0: u1 is off; w is z, which the load sees at 102.
//                                                          TRACE a=z t=102
//  120 ns  e1=1: u1 reads z and drives a1 (0): w=0 at 120, the load at 122.
//                                                          TRACE a=0 t=122
`timescale 1ns/1ps
module rdrv(input logic a, input logic en, output tri z);
  assign z = en ? (z === 1'b0 ? 1'b0 : a) : 1'bz;
endmodule

module tdrv(input logic a, input logic en, output tri z);
  assign z = en ? a : 1'bz;
endmodule

module probe(input wire a);
  always @(a) if ($time >= 10) $display("TRACE a=%b t=%0d", a, $time);
endmodule

module top;
  logic a1 = 1, e1 = 0, d2 = 0, e2 = 0;
  wire w;
  rdrv u1(.a(a1), .en(e1), .z(w));
  tdrv u2(.a(d2), .en(e2), .z(w));
  probe p(.a(w));
  initial begin
    #20 e2 = 1;
    #20 e1 = 1;
    #20 e2 = 0;
    #20 a1 = 0;
    #20 e1 = 0;
    #20 e1 = 1;
  end
endmodule
